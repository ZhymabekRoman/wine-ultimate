/*
 * IAppxFactory / IAppxBundleFactory / IAppxPackageReader implementation
 * for MSIX / AppX package inspection (used by winget / WindowsPackageManager).
 *
 * Copyright (C) 2026 Wine contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "objbase.h"
#include "shlwapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

/* CLSIDs & IIDs from appxpackaging.h */
const GUID CLSID_AppxFactory =
    { 0x5842a140, 0xff9f, 0x4166, { 0x8f, 0x5c, 0x62, 0xf5, 0xb7, 0xb0, 0xc7, 0x81 } };
const GUID CLSID_AppxBundleFactory =
    { 0x378e0446, 0x5384, 0x43b7, { 0x88, 0x77, 0xe7, 0xdb, 0xdd, 0x88, 0x34, 0x46 } };

static const GUID IID_IAppxFactory =
    { 0xbeb94909, 0xe451, 0x438b, { 0xb5, 0xa7, 0xd7, 0x9e, 0x76, 0x7b, 0x75, 0xd8 } };
static const GUID IID_IAppxBundleFactory =
    { 0xbba65864, 0x965f, 0x4a5f, { 0x85, 0x5f, 0xf0, 0x74, 0xbd, 0xbf, 0x3a, 0x7b } };
static const GUID IID_IAppxPackageReader =
    { 0xb5c49650, 0x99bc, 0x481c, { 0x9a, 0x34, 0x3d, 0x53, 0xa4, 0x10, 0x67, 0x08 } };
static const GUID IID_IAppxFile =
    { 0x91df827b, 0x94fd, 0x468f, { 0x82, 0x7b, 0x57, 0xf4, 0x1b, 0x2f, 0x6f, 0x2e } };
static const GUID IID_IAppxFilesEnumerator =
    { 0xf007eeaf, 0x9831, 0x411c, { 0x98, 0x47, 0x91, 0x7c, 0xdc, 0x62, 0xd1, 0xfe } };
static const GUID IID_IAppxManifestReader =
    { 0x4e1bd148, 0x55a0, 0x4480, { 0xa3, 0xd1, 0x15, 0x54, 0x47, 0x10, 0x63, 0x7c } };
static const GUID IID_IAppxManifestPackageId =
    { 0x283ce2d7, 0x7153, 0x4a91, { 0x96, 0x49, 0x7a, 0x0f, 0x72, 0x40, 0x94, 0x5f } };
static const GUID IID_IAppxManifestProperties =
    { 0x03faf64d, 0xf26f, 0x4b2c, { 0xaa, 0xf7, 0x8f, 0xe7, 0x78, 0x9b, 0x8b, 0xca } };

/* --- Minimal ZIP archive reader over IStream --- */

struct zip_entry
{
    WCHAR *name;
    UINT16 method;
    UINT32 comp_size;
    UINT32 uncomp_size;
    UINT64 lfh_offset;
};

struct zip_archive
{
    BYTE *data;
    SIZE_T size;
    struct zip_entry *entries;
    UINT32 count;
};

static inline UINT16 rd16( const BYTE *p ) { return p[0] | (p[1] << 8); }
static inline UINT32 rd32( const BYTE *p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

static void zip_free( struct zip_archive *z )
{
    UINT32 i;
    if (!z) return;
    for (i = 0; i < z->count; i++) free( z->entries[i].name );
    free( z->entries );
    free( z->data );
    free( z );
}

static HRESULT zip_open_stream( IStream *stream, struct zip_archive **out )
{
    STATSTG st;
    LARGE_INTEGER zero = { 0 };
    BYTE *buf = NULL;
    SIZE_T total = 0, cap = 0, pos = 0;
    ULONG got;
    HRESULT hr;
    struct zip_archive *z;
    UINT32 eocd_off = 0, cd_off, cd_size, n_entries, i;
    BOOL found_eocd = FALSE;

    *out = NULL;
    if (!stream) return E_INVALIDARG;

    memset( &st, 0, sizeof(st) );
    hr = IStream_Stat( stream, &st, STATFLAG_NONAME );
    TRACE( "Stat hr=%08lx cbSize=%I64u\n", hr, st.cbSize.QuadPart );
    IStream_Seek( stream, zero, STREAM_SEEK_SET, NULL );

    if (SUCCEEDED(hr) && st.cbSize.QuadPart >= 22 && st.cbSize.QuadPart <= 256 * 1024 * 1024)
    {
        cap = (SIZE_T)st.cbSize.QuadPart;
        buf = malloc( cap );
        if (!buf) return E_OUTOFMEMORY;
        while (pos < cap)
        {
            ULONG chunk = (ULONG)min( cap - pos, 1024 * 1024 );
            got = 0;
            hr = IStream_Read( stream, buf + pos, chunk, &got );
            if (FAILED(hr) || got == 0) break;
            pos += got;
        }
        total = pos;
    }
    else
    {
        cap = 65536;
        buf = malloc( cap );
        if (!buf) return E_OUTOFMEMORY;
        for (;;)
        {
            if (total + 65536 > cap)
            {
                BYTE *nb;
                if (cap >= 256 * 1024 * 1024) { free( buf ); return E_OUTOFMEMORY; }
                cap *= 2;
                nb = realloc( buf, cap );
                if (!nb) { free( buf ); return E_OUTOFMEMORY; }
                buf = nb;
            }
            got = 0;
            hr = IStream_Read( stream, buf + total, 65536, &got );
            if (FAILED(hr) || got == 0) break;
            total += got;
        }
    }
    TRACE( "read total=%Iu\n", total );
    if (total < 22) { free( buf ); return APPX_E_MISSING_REQUIRED_FILE; }

    /* Find EOCD signature 0x06054b50 */
    {
        SIZE_T min_off = total > 65557 ? total - 65557 : 0;
        SIZE_T k = total - 22;
        for (;;)
        {
            if (buf[k] == 0x50 && buf[k+1] == 0x4b && buf[k+2] == 0x05 && buf[k+3] == 0x06)
            {
                eocd_off = (UINT32)k;
                found_eocd = TRUE;
                break;
            }
            if (k == min_off) break;
            k--;
        }
    }
    if (!found_eocd) { TRACE( "no EOCD total=%Iu head=%02x%02x%02x%02x tail=%02x%02x%02x%02x\n", total, buf[0], buf[1], buf[2], buf[3], buf[total-4], buf[total-3], buf[total-2], buf[total-1] ); free( buf ); return APPX_E_MISSING_REQUIRED_FILE; }

    n_entries = rd16( buf + eocd_off + 10 );
    cd_size = rd32( buf + eocd_off + 12 );
    cd_off = rd32( buf + eocd_off + 16 );
    if ((n_entries == 0xffff || cd_size == 0xffffffff || cd_off == 0xffffffff) && eocd_off >= 20)
    {
        UINT32 loc = eocd_off - 20;
        if (rd32( buf + loc ) == 0x07064b50)
        {
            UINT64 z64_off = (UINT64)rd32( buf + loc + 8 ) | ((UINT64)rd32( buf + loc + 12 ) << 32);
            if (z64_off + 56 <= total && rd32( buf + z64_off ) == 0x06064b50)
            {
                n_entries = rd32( buf + z64_off + 32 );
                cd_size = rd32( buf + z64_off + 40 );
                cd_off = rd32( buf + z64_off + 48 );
            }
        }
    }
    if ((SIZE_T)cd_off + cd_size > total) { TRACE( "bad CD off=%u sz=%u total=%Iu\n", cd_off, cd_size, total ); free( buf ); return APPX_E_MISSING_REQUIRED_FILE; }

    z = calloc( 1, sizeof(*z) );
    if (!z) { free( buf ); return E_OUTOFMEMORY; }
    z->data = buf;
    z->size = total;
    z->entries = calloc( n_entries ? n_entries : 1, sizeof(*z->entries) );
    if (!z->entries) { zip_free( z ); return E_OUTOFMEMORY; }

    pos = cd_off;
    for (i = 0; i < n_entries && pos + 46 <= total; i++)
    {
        UINT16 fn_len, ex_len, cm_len, wlen;
        const char *fn;
        if (rd32( buf + pos ) != 0x02014b50) break;
        z->entries[i].method = rd16( buf + pos + 10 );
        z->entries[i].comp_size = rd32( buf + pos + 20 );
        z->entries[i].uncomp_size = rd32( buf + pos + 24 );
        fn_len = rd16( buf + pos + 28 );
        ex_len = rd16( buf + pos + 30 );
        cm_len = rd16( buf + pos + 32 );
        z->entries[i].lfh_offset = rd32( buf + pos + 42 );
        if (pos + 46 + fn_len > total) break;
        fn = (const char *)(buf + pos + 46);
        wlen = MultiByteToWideChar( CP_UTF8, 0, fn, fn_len, NULL, 0 );
        z->entries[i].name = malloc( (wlen + 1) * sizeof(WCHAR) );
        if (z->entries[i].name)
        {
            UINT16 k;
            MultiByteToWideChar( CP_UTF8, 0, fn, fn_len, z->entries[i].name, wlen );
            z->entries[i].name[wlen] = 0;
            for (k = 0; k < wlen; k++)
                if (z->entries[i].name[k] == '\\') z->entries[i].name[k] = '/';
        }
        z->count++;
        pos += 46 + fn_len + ex_len + cm_len;
    }

    *out = z;
    return S_OK;
}

static const struct zip_entry *zip_find( const struct zip_archive *z, const WCHAR *name )
{
    WCHAR norm[MAX_PATH];
    SIZE_T i, len = wcslen( name );
    if (len >= MAX_PATH) len = MAX_PATH - 1;
    for (i = 0; i < len; i++) norm[i] = (name[i] == '\\') ? '/' : name[i];
    norm[len] = 0;
    for (i = 0; i < z->count; i++)
    {
        if (z->entries[i].name && !wcsicmp( z->entries[i].name, norm ))
            return &z->entries[i];
    }
    return NULL;
}

static void *z_alloc_fn( void *opaque, unsigned int items, unsigned int size )
{
    return calloc( items, size );
}

static void z_free_fn( void *opaque, void *address )
{
    free( address );
}

static HRESULT zip_extract( const struct zip_archive *z, const struct zip_entry *e, BYTE **out_buf, SIZE_T *out_len )
{
    SIZE_T lfh = (SIZE_T)e->lfh_offset;
    UINT16 fn_len, ex_len;
    SIZE_T data_off;
    BYTE *dst;

    *out_buf = NULL;
    *out_len = 0;
    TRACE( "method=%u comp=%u uncomp=%u lfh=%I64u zsize=%Iu\n", e->method, e->comp_size, e->uncomp_size, e->lfh_offset, z->size );
    if (lfh + 30 > z->size || rd32( z->data + lfh ) != 0x04034b50) return E_FAIL;
    fn_len = rd16( z->data + lfh + 26 );
    ex_len = rd16( z->data + lfh + 28 );
    data_off = lfh + 30 + fn_len + ex_len;
    if (data_off + e->comp_size > z->size) return E_FAIL;

    dst = malloc( e->uncomp_size ? e->uncomp_size : 1 );
    if (!dst) return E_OUTOFMEMORY;

    if (e->method == 0)
    {
        if (e->comp_size < e->uncomp_size) { free( dst ); return E_FAIL; }
        memcpy( dst, z->data + data_off, e->uncomp_size );
    }
    else if (e->method == 8)
    {
        z_stream zs;
        int ret;
        memset( &zs, 0, sizeof(zs) );
        zs.zalloc = z_alloc_fn;
        zs.zfree = z_free_fn;
        zs.next_in = z->data + data_off;
        zs.avail_in = e->comp_size;
        zs.next_out = dst;
        zs.avail_out = e->uncomp_size;
        if (inflateInit2( &zs, -MAX_WBITS ) != Z_OK) { TRACE( "inflateInit2 failed\n" ); free( dst ); return E_FAIL; }
        ret = inflate( &zs, Z_FINISH );
        inflateEnd( &zs );
        if (ret != Z_STREAM_END && ret != Z_OK) { TRACE( "inflate ret=%d\n", ret ); free( dst ); return E_FAIL; }
    }
    else
    {
        free( dst );
        return E_NOTIMPL;
    }

    *out_buf = dst;
    *out_len = e->uncomp_size;
    return S_OK;
}

static HRESULT make_mem_stream( const BYTE *buf, SIZE_T len, IStream **out )
{
    HGLOBAL hg;
    void *p;
    HRESULT hr;
    *out = NULL;
    hg = GlobalAlloc( GMEM_MOVEABLE, len ? len : 1 );
    if (!hg) return E_OUTOFMEMORY;
    p = GlobalLock( hg );
    if (len && p) memcpy( p, buf, len );
    GlobalUnlock( hg );
    hr = CreateStreamOnHGlobal( hg, TRUE, out );
    if (FAILED(hr)) GlobalFree( hg );
    return hr;
}

static LPWSTR co_strdupW( const WCHAR *s )
{
    SIZE_T sz = (wcslen( s ) + 1) * sizeof(WCHAR);
    LPWSTR p = CoTaskMemAlloc( sz );
    if (p) memcpy( p, s, sz );
    return p;
}

/* --- IAppxFile --- */

struct appx_file
{
    const void *lpVtbl;
    LONG ref;
    WCHAR *name;
    UINT32 comp_opt;
    BYTE *bytes;
    SIZE_T size;
};

static HRESULT WINAPI appx_file_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAppxFile ))
    {
        *out = iface;
        InterlockedIncrement( &((struct appx_file *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI appx_file_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct appx_file *)iface)->ref );
}

static ULONG WINAPI appx_file_Release( void *iface )
{
    struct appx_file *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->name );
        free( impl->bytes );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI appx_file_GetCompressionOption( void *iface, UINT32 *opt )
{
    struct appx_file *impl = iface;
    if (!opt) return E_POINTER;
    *opt = impl->comp_opt;
    return S_OK;
}

static HRESULT WINAPI appx_file_GetContentType( void *iface, LPWSTR *type )
{
    if (!type) return E_POINTER;
    *type = co_strdupW( L"application/octet-stream" );
    return *type ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI appx_file_GetName( void *iface, LPWSTR *name )
{
    struct appx_file *impl = iface;
    if (!name) return E_POINTER;
    *name = co_strdupW( impl->name );
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI appx_file_GetSize( void *iface, UINT64 *size )
{
    struct appx_file *impl = iface;
    if (!size) return E_POINTER;
    *size = impl->size;
    return S_OK;
}

static HRESULT WINAPI appx_file_GetStream( void *iface, IStream **stream )
{
    struct appx_file *impl = iface;
    if (!stream) return E_POINTER;
    return make_mem_stream( impl->bytes, impl->size, stream );
}

static const void *appx_file_vtbl[] =
{
    appx_file_QueryInterface,
    appx_file_AddRef,
    appx_file_Release,
    appx_file_GetCompressionOption,
    appx_file_GetContentType,
    appx_file_GetName,
    appx_file_GetSize,
    appx_file_GetStream,
};

static HRESULT appx_file_create_from_entry( const struct zip_archive *z, const struct zip_entry *e, void **out )
{
    struct appx_file *impl;
    BYTE *buf = NULL;
    SIZE_T len = 0;
    HRESULT hr;

    *out = NULL;
    hr = zip_extract( z, e, &buf, &len );
    if (FAILED(hr)) return hr;

    impl = calloc( 1, sizeof(*impl) );
    if (!impl) { free( buf ); return E_OUTOFMEMORY; }
    impl->lpVtbl = appx_file_vtbl;
    impl->ref = 1;
    impl->name = wcsdup( e->name );
    impl->comp_opt = (e->method == 0) ? 0 : 1;
    impl->bytes = buf;
    impl->size = len;
    *out = impl;
    return S_OK;
}

/* --- IAppxManifestPackageId / IAppxManifestProperties / IAppxManifestReader --- */

struct appx_manifest
{
    const void *lpVtbl_reader;
    const void *lpVtbl_pkgid;
    const void *lpVtbl_props;
    LONG ref;
    BYTE *xml_bytes;
    SIZE_T xml_len;
    WCHAR name[256];
    WCHAR publisher[512];
    WCHAR arch[64];
    WCHAR resource_id[128];
    UINT64 version;
    WCHAR display_name[256];
    WCHAR publisher_display[256];
    WCHAR description[512];
};

static BOOL extract_xml_attr( const char *xml, const char *tag, const char *attr, WCHAR *out, SIZE_T out_cap )
{
    const char *t = strstr( xml, tag );
    const char *gt, *p, *q;
    char pat[128];
    if (!t) return FALSE;
    gt = strchr( t, '>' );
    if (!gt) return FALSE;
    snprintf( pat, sizeof(pat), "%s=\"", attr );
    p = strstr( t, pat );
    if (!p || p > gt) return FALSE;
    p += strlen( pat );
    q = strchr( p, '"' );
    if (!q || q > gt) return FALSE;
    MultiByteToWideChar( CP_UTF8, 0, p, (int)(q - p), out, (int)(out_cap - 1) );
    out[min((SIZE_T)(q - p), out_cap - 1)] = 0;
    return TRUE;
}

static UINT64 parse_version_str( const WCHAR *s )
{
    UINT64 a = 0, b = 0, c = 0, d = 0;
    swscanf( s, L"%I64u.%I64u.%I64u.%I64u", &a, &b, &c, &d );
    return (a << 48) | (b << 32) | (c << 16) | d;
}

static HRESULT WINAPI pkgid_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAppxManifestPackageId ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI pkgid_AddRef( void *iface )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI pkgid_Release( void *iface )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    return ((ULONG (WINAPI *)(void *))((void **)impl->lpVtbl_reader)[2])( &impl->lpVtbl_reader );
}

static HRESULT WINAPI pkgid_GetName( void *iface, LPWSTR *name )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    if (!name) return E_POINTER;
    *name = co_strdupW( impl->name );
    return *name ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI pkgid_GetArchitecture( void *iface, UINT32 *arch )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    if (!arch) return E_POINTER;
    if (!wcsicmp( impl->arch, L"x86" )) *arch = 0;
    else if (!wcsicmp( impl->arch, L"arm" )) *arch = 5;
    else if (!wcsicmp( impl->arch, L"x64" )) *arch = 9;
    else if (!wcsicmp( impl->arch, L"arm64" )) *arch = 12;
    else *arch = 11; /* neutral */
    return S_OK;
}

static HRESULT WINAPI pkgid_GetPublisher( void *iface, LPWSTR *pub )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    if (!pub) return E_POINTER;
    *pub = co_strdupW( impl->publisher );
    return *pub ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI pkgid_GetVersion( void *iface, UINT64 *ver )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    if (!ver) return E_POINTER;
    *ver = impl->version;
    return S_OK;
}

static HRESULT WINAPI pkgid_GetResourceId( void *iface, LPWSTR *res )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    if (!res) return E_POINTER;
    *res = impl->resource_id[0] ? co_strdupW( impl->resource_id ) : NULL;
    return S_OK;
}

static HRESULT WINAPI pkgid_ComparePublisher( void *iface, LPCWSTR other, BOOL *same )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    if (!same) return E_POINTER;
    *same = other && !wcsicmp( impl->publisher, other );
    return S_OK;
}

static HRESULT WINAPI pkgid_GetPackageFullName( void *iface, LPWSTR *full )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    WCHAR buf[512];
    if (!full) return E_POINTER;
    swprintf( buf, ARRAY_SIZE(buf), L"%s_%u.%u.%u.%u_%s_%s_8wekyb3d8bbwe",
              impl->name,
              (UINT)(impl->version >> 48), (UINT)((impl->version >> 32) & 0xffff),
              (UINT)((impl->version >> 16) & 0xffff), (UINT)(impl->version & 0xffff),
              impl->arch[0] ? impl->arch : L"neutral",
              impl->resource_id );
    *full = co_strdupW( buf );
    return *full ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI pkgid_GetPackageFamilyName( void *iface, LPWSTR *fam )
{
    struct appx_manifest *impl = (struct appx_manifest *)((BYTE *)iface - offsetof(struct appx_manifest, lpVtbl_pkgid));
    WCHAR buf[384];
    if (!fam) return E_POINTER;
    swprintf( buf, ARRAY_SIZE(buf), L"%s_8wekyb3d8bbwe", impl->name );
    *fam = co_strdupW( buf );
    return *fam ? S_OK : E_OUTOFMEMORY;
}

static const void *appx_pkgid_vtbl[] =
{
    pkgid_QueryInterface,
    pkgid_AddRef,
    pkgid_Release,
    pkgid_GetName,
    pkgid_GetArchitecture,
    pkgid_GetPublisher,
    pkgid_GetVersion,
    pkgid_GetResourceId,
    pkgid_ComparePublisher,
    pkgid_GetPackageFullName,
    pkgid_GetPackageFamilyName,
};

static HRESULT WINAPI manifest_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct appx_manifest *impl = iface;
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAppxManifestReader ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI manifest_AddRef( void *iface )
{
    struct appx_manifest *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI manifest_Release( void *iface )
{
    struct appx_manifest *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->xml_bytes );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI manifest_GetPackageId( void *iface, void **pkgid )
{
    struct appx_manifest *impl = iface;
    if (!pkgid) return E_POINTER;
    *pkgid = &impl->lpVtbl_pkgid;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI manifest_GetProperties( void *iface, void **props )
{
    FIXME( "stub\n" );
    if (props) *props = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI manifest_GetPackageDependencies( void *iface, void **deps )
{
    FIXME( "stub\n" );
    if (deps) *deps = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI manifest_GetCapabilities( void *iface, UINT32 *caps )
{
    if (caps) *caps = 0;
    return S_OK;
}

static HRESULT WINAPI manifest_GetResources( void *iface, void **res )
{
    FIXME( "stub\n" );
    if (res) *res = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI manifest_GetDeviceCapabilities( void *iface, void **dc )
{
    FIXME( "stub\n" );
    if (dc) *dc = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI manifest_GetPrerequisite( void *iface, LPCWSTR name, UINT64 *val )
{
    if (val) *val = 0;
    return S_OK;
}

static HRESULT WINAPI manifest_GetApplications( void *iface, void **apps )
{
    FIXME( "stub\n" );
    if (apps) *apps = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI manifest_GetStream( void *iface, IStream **stream )
{
    struct appx_manifest *impl = iface;
    if (!stream) return E_POINTER;
    return make_mem_stream( impl->xml_bytes, impl->xml_len, stream );
}

static const void *appx_manifest_vtbl[] =
{
    manifest_QueryInterface,
    manifest_AddRef,
    manifest_Release,
    manifest_GetPackageId,
    manifest_GetProperties,
    manifest_GetPackageDependencies,
    manifest_GetCapabilities,
    manifest_GetResources,
    manifest_GetDeviceCapabilities,
    manifest_GetPrerequisite,
    manifest_GetApplications,
    manifest_GetStream,
};

/* --- IAppxPackageReader --- */

struct appx_package_reader
{
    const void *lpVtbl;
    LONG ref;
    struct zip_archive *zip;
};

static HRESULT WINAPI pkg_reader_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct appx_package_reader *impl = iface;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAppxPackageReader ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI pkg_reader_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct appx_package_reader *)iface)->ref );
}

static ULONG WINAPI pkg_reader_Release( void *iface )
{
    struct appx_package_reader *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        zip_free( impl->zip );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI pkg_reader_GetBlockMap( void *iface, void **bm )
{
    FIXME( "GetBlockMap stub\n" );
    if (bm) *bm = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI pkg_reader_GetFootprintFile( void *iface, UINT32 type, void **file )
{
    struct appx_package_reader *impl = iface;
    const WCHAR *name = NULL;
    const struct zip_entry *e;

    TRACE( "type %u\n", type );
    if (!file) return E_POINTER;
    *file = NULL;
    switch (type)
    {
    case 0: name = L"AppxManifest.xml"; break;
    case 1: name = L"AppxBlockMap.xml"; break;
    case 2: name = L"AppxSignature.p7x"; break;
    case 3: name = L"AppxMetadata/CodeIntegrity.cat"; break;
    default: return E_INVALIDARG;
    }
    e = zip_find( impl->zip, name );
    if (!e) return APPX_E_MISSING_REQUIRED_FILE;
    return appx_file_create_from_entry( impl->zip, e, file );
}

static HRESULT WINAPI pkg_reader_GetPayloadFile( void *iface, LPCWSTR file_name, void **file )
{
    struct appx_package_reader *impl = iface;
    const struct zip_entry *e;

    TRACE( "file %s\n", debugstr_w( file_name ) );
    if (!file || !file_name) return E_INVALIDARG;
    *file = NULL;
    e = zip_find( impl->zip, file_name );
    if (!e) return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    return appx_file_create_from_entry( impl->zip, e, file );
}

static HRESULT WINAPI pkg_reader_GetPayloadFiles( void *iface, void **enum_out )
{
    FIXME( "GetPayloadFiles stub\n" );
    if (enum_out) *enum_out = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI pkg_reader_GetManifest( void *iface, void **manifest_out )
{
    struct appx_package_reader *impl = iface;
    const struct zip_entry *e;
    BYTE *buf = NULL;
    SIZE_T len = 0;
    char *xml_str;
    struct appx_manifest *m;
    WCHAR ver_w[64] = { 0 };
    HRESULT hr;

    TRACE( "\n" );
    if (!manifest_out) return E_POINTER;
    *manifest_out = NULL;
    e = zip_find( impl->zip, L"AppxManifest.xml" );
    if (!e) return APPX_E_MISSING_REQUIRED_FILE;
    hr = zip_extract( impl->zip, e, &buf, &len );
    TRACE( "zip_extract AppxManifest.xml hr=%08lx len=%Iu\n", hr, len );
    if (FAILED(hr)) return hr;

    xml_str = malloc( len + 1 );
    if (!xml_str) { free( buf ); return E_OUTOFMEMORY; }
    memcpy( xml_str, buf, len );
    xml_str[len] = 0;

    m = calloc( 1, sizeof(*m) );
    if (!m) { free( xml_str ); free( buf ); return E_OUTOFMEMORY; }
    m->lpVtbl_reader = appx_manifest_vtbl;
    m->lpVtbl_pkgid = appx_pkgid_vtbl;
    m->ref = 1;
    m->xml_bytes = buf;
    m->xml_len = len;
    extract_xml_attr( xml_str, "<Identity", "Name", m->name, ARRAY_SIZE(m->name) );
    extract_xml_attr( xml_str, "<Identity", "Publisher", m->publisher, ARRAY_SIZE(m->publisher) );
    extract_xml_attr( xml_str, "<Identity", "ProcessorArchitecture", m->arch, ARRAY_SIZE(m->arch) );
    extract_xml_attr( xml_str, "<Identity", "ResourceId", m->resource_id, ARRAY_SIZE(m->resource_id) );
    if (extract_xml_attr( xml_str, "<Identity", "Version", ver_w, ARRAY_SIZE(ver_w) ))
        m->version = parse_version_str( ver_w );

    free( xml_str );
    *manifest_out = &m->lpVtbl_reader;
    return S_OK;
}

static const void *appx_package_reader_vtbl[] =
{
    pkg_reader_QueryInterface,
    pkg_reader_AddRef,
    pkg_reader_Release,
    pkg_reader_GetBlockMap,
    pkg_reader_GetFootprintFile,
    pkg_reader_GetPayloadFile,
    pkg_reader_GetPayloadFiles,
    pkg_reader_GetManifest,
};

/* --- IAppxFactory --- */

static HRESULT WINAPI appx_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAppxFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI appx_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI appx_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI appx_factory_CreatePackageWriter( void *iface, IStream *s, void *settings, void **w )
{
    FIXME( "stub\n" );
    if (w) *w = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI appx_factory_CreatePackageReader( void *iface, IStream *stream, void **reader_out )
{
    struct zip_archive *z = NULL;
    struct appx_package_reader *r;
    HRESULT hr;

    TRACE( "stream %p\n", stream );
    if (!reader_out) return E_POINTER;
    *reader_out = NULL;
    hr = zip_open_stream( stream, &z );
    if (FAILED(hr)) { TRACE( "zip_open_stream failed %08lx\n", hr ); return hr; }
    TRACE( "zip entries=%u has_manifest=%d\n", z->count, !!zip_find( z, L"AppxManifest.xml" ) );
    if (!zip_find( z, L"AppxManifest.xml" ))
    {
        zip_free( z );
        return APPX_E_MISSING_REQUIRED_FILE;
    }
    r = calloc( 1, sizeof(*r) );
    if (!r) { zip_free( z ); return E_OUTOFMEMORY; }
    r->lpVtbl = appx_package_reader_vtbl;
    r->ref = 1;
    r->zip = z;
    *reader_out = r;
    return S_OK;
}
static HRESULT WINAPI appx_factory_CreateManifestReader( void *iface, IStream *s, void **m )
{
    FIXME( "stub\n" );
    if (m) *m = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI appx_factory_CreateBlockMapReader( void *iface, IStream *s, void **bm )
{
    FIXME( "stub\n" );
    if (bm) *bm = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI appx_factory_CreateValidatedBlockMapReader( void *iface, IStream *s, LPCWSTR sig, void **bm )
{
    FIXME( "stub\n" );
    if (bm) *bm = NULL;
    return E_NOTIMPL;
}

static const void *appx_factory_vtbl[] =
{
    appx_factory_QueryInterface,
    appx_factory_AddRef,
    appx_factory_Release,
    appx_factory_CreatePackageWriter,
    appx_factory_CreatePackageReader,
    appx_factory_CreateManifestReader,
    appx_factory_CreateBlockMapReader,
    appx_factory_CreateValidatedBlockMapReader,
};
static const void *appx_factory_obj = appx_factory_vtbl;

/* --- IAppxBundleFactory --- */

static HRESULT WINAPI bundle_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAppxBundleFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI bundle_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI bundle_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI bundle_factory_CreateBundleWriter( void *iface, IStream *s, UINT64 ver, void **w )
{
    FIXME( "stub\n" );
    if (w) *w = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI bundle_factory_CreateBundleReader( void *iface, IStream *stream, void **reader_out )
{
    struct zip_archive *z = NULL;
    HRESULT hr;

    TRACE( "stream %p\n", stream );
    if (!reader_out) return E_POINTER;
    *reader_out = NULL;
    hr = zip_open_stream( stream, &z );
    if (FAILED(hr)) return hr;
    if (!zip_find( z, L"AppxMetadata/AppxBundleManifest.xml" ))
    {
        zip_free( z );
        return APPX_E_MISSING_REQUIRED_FILE;
    }
    zip_free( z );
    FIXME( "bundle reader not implemented\n" );
    return E_NOTIMPL;
}
static HRESULT WINAPI bundle_factory_CreateBundleManifestReader( void *iface, IStream *s, void **m )
{
    FIXME( "stub\n" );
    if (m) *m = NULL;
    return E_NOTIMPL;
}

static const void *appx_bundle_factory_vtbl[] =
{
    bundle_factory_QueryInterface,
    bundle_factory_AddRef,
    bundle_factory_Release,
    bundle_factory_CreateBundleWriter,
    bundle_factory_CreateBundleReader,
    bundle_factory_CreateBundleManifestReader,
};
static const void *appx_bundle_factory_obj = appx_bundle_factory_vtbl;

HRESULT appx_factory_create( REFIID riid, void **out )
{
    return appx_factory_QueryInterface( &appx_factory_obj, riid, out );
}

HRESULT appx_bundle_factory_create( REFIID riid, void **out )
{
    return bundle_factory_QueryInterface( &appx_bundle_factory_obj, riid, out );
}

static void mkdir_recursive( const WCHAR *dir )
{
    WCHAR tmp[MAX_PATH], *p;
    lstrcpynW( tmp, dir, ARRAY_SIZE(tmp) );
    for (p = tmp; *p; p++)
    {
        if (*p == L'\\' || *p == L'/')
        {
            WCHAR c = *p;
            *p = 0;
            if (p > tmp && p[-1] != L':') CreateDirectoryW( tmp, NULL );
            *p = c;
        }
    }
    CreateDirectoryW( tmp, NULL );
}

HRESULT appx_extract_msix_to_windowsapps( const WCHAR *msix_path, WCHAR *out_dir, SIZE_T out_cap )
{
    HANDLE hfile;
    LARGE_INTEGER fsz;
    BYTE *fbuf;
    DWORD rd = 0;
    IStream *stream = NULL;
    struct zip_archive *z = NULL;
    const struct zip_entry *me;
    BYTE *mbuf = NULL;
    SIZE_T mlen = 0;
    char *mstr;
    WCHAR name[256] = { 0 }, arch[64] = { 0 }, res_id[128] = { 0 }, ver[64] = { 0 };
    UINT32 i;
    HRESULT hr;

    if (!msix_path || !out_dir || !out_cap) return E_INVALIDARG;
    out_dir[0] = 0;

    hfile = CreateFileW( msix_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (hfile == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    if (!GetFileSizeEx( hfile, &fsz ) || fsz.QuadPart < 22 || fsz.QuadPart > 256 * 1024 * 1024)
    {
        CloseHandle( hfile );
        return E_FAIL;
    }
    fbuf = malloc( (SIZE_T)fsz.QuadPart );
    if (!fbuf) { CloseHandle( hfile ); return E_OUTOFMEMORY; }
    if (!ReadFile( hfile, fbuf, (DWORD)fsz.QuadPart, &rd, NULL ) || rd != (DWORD)fsz.QuadPart)
    {
        free( fbuf );
        CloseHandle( hfile );
        return E_FAIL;
    }
    CloseHandle( hfile );

    hr = make_mem_stream( fbuf, rd, &stream );
    free( fbuf );
    if (FAILED(hr)) return hr;

    hr = zip_open_stream( stream, &z );
    IStream_Release( stream );
    if (FAILED(hr)) return hr;

    me = zip_find( z, L"AppxManifest.xml" );
    if (!me) { zip_free( z ); return APPX_E_MISSING_REQUIRED_FILE; }
    hr = zip_extract( z, me, &mbuf, &mlen );
    if (FAILED(hr)) { zip_free( z ); return hr; }

    mstr = malloc( mlen + 1 );
    if (!mstr) { free( mbuf ); zip_free( z ); return E_OUTOFMEMORY; }
    memcpy( mstr, mbuf, mlen );
    mstr[mlen] = 0;
    free( mbuf );

    extract_xml_attr( mstr, "<Identity", "Name", name, ARRAY_SIZE(name) );
    extract_xml_attr( mstr, "<Identity", "Version", ver, ARRAY_SIZE(ver) );
    extract_xml_attr( mstr, "<Identity", "ProcessorArchitecture", arch, ARRAY_SIZE(arch) );
    extract_xml_attr( mstr, "<Identity", "ResourceId", res_id, ARRAY_SIZE(res_id) );
    free( mstr );

    if (!name[0] || !ver[0]) { zip_free( z ); return E_FAIL; }
    swprintf( out_dir, out_cap, L"C:\\Program Files\\WindowsApps\\%s_%s_%s_%s_8wekyb3d8bbwe",
              name, ver, arch[0] ? arch : L"neutral", res_id );
    mkdir_recursive( out_dir );

    for (i = 0; i < z->count; i++)
    {
        const struct zip_entry *e = &z->entries[i];
        WCHAR dest[MAX_PATH], *slash;
        BYTE *ebuf = NULL;
        SIZE_T elen = 0;
        HANDLE out_h;
        DWORD wr = 0;
        UINT32 k;

        if (!e->name || !e->name[0]) continue;
        if (e->name[wcslen(e->name) - 1] == L'/') continue;
        swprintf( dest, ARRAY_SIZE(dest), L"%s\\%s", out_dir, e->name );
        for (k = 0; dest[k]; k++) if (dest[k] == L'/') dest[k] = L'\\';
        slash = wcsrchr( dest, L'\\' );
        if (slash)
        {
            *slash = 0;
            mkdir_recursive( dest );
            *slash = L'\\';
        }
        if (SUCCEEDED( zip_extract( z, e, &ebuf, &elen ) ))
        {
            out_h = CreateFileW( dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
            if (out_h != INVALID_HANDLE_VALUE)
            {
                if (elen) WriteFile( out_h, ebuf, (DWORD)elen, &wr, NULL );
                CloseHandle( out_h );
            }
            free( ebuf );
        }
    }

    TRACE( "extracted %u entries from %s to %s\n", z->count, debugstr_w(msix_path), debugstr_w(out_dir) );
    zip_free( z );
    return S_OK;
}
