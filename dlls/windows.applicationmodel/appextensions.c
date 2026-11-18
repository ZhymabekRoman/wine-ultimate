/* WinRT Windows.ApplicationModel.AppExtensions.AppExtensionCatalog implementation
 *
 * Copyright (C) 2026 Wine Project Contributors
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "objbase.h"
#include "winreg.h"
#include "xmllite.h"
#include "windows.storage.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appextensions);

static const GUID IID_IAppExtensionCatalogStatics =
    { 0x3c36668a, 0x5f18, 0x4f0b, {0x9c,0xe5,0xca,0xb6,0x1d,0x19,0x6f,0x11} };
static const GUID IID_IAppExtensionCatalog =
    { 0x97872032, 0x8426, 0x4ad1, {0x93,0x12,0xb5,0x64,0x64,0x49,0xb9,0x49} };
static const GUID IID_IAppExtension =
    { 0x8450902c, 0x15ed, 0x4faf, {0xb3,0x0c,0xec,0x92,0x00,0xe8,0x95,0x66} };
static const GUID IID_IPackage4 =
    { 0x65aed1ae, 0xb95b, 0x450c, {0x88,0x2b,0x62,0x55,0x18,0x7f,0x39,0x7e} };
static const GUID IID_IAsyncInfo_local =
    { 0x00000036, 0x0000, 0x0000, {0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46} };
static const GUID IID_IXmlReader_local =
    { 0x7279fc81, 0x709d, 0x4095, {0xb6,0x3d,0x69,0xfe,0x4b,0x0d,0x90,0x30} };

/* --------------------------------------------------------------------- */
/* Minimal IStream over an in-memory buffer for xmllite */

struct mem_stream
{
    IStream IStream_iface;
    LONG ref;
    const BYTE *data;
    ULONG size;
    ULONG pos;
};

static inline struct mem_stream *mem_stream_from_iface( IStream *iface )
{
    return CONTAINING_RECORD( iface, struct mem_stream, IStream_iface );
}

static HRESULT WINAPI mem_stream_QueryInterface( IStream *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_ISequentialStream ) ||
        IsEqualGUID( iid, &IID_IStream ))
    {
        *out = iface;
        IStream_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI mem_stream_AddRef( IStream *iface )
{
    return InterlockedIncrement( &mem_stream_from_iface( iface )->ref );
}

static ULONG WINAPI mem_stream_Release( IStream *iface )
{
    struct mem_stream *impl = mem_stream_from_iface( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI mem_stream_Read( IStream *iface, void *pv, ULONG cb, ULONG *pcbRead )
{
    struct mem_stream *impl = mem_stream_from_iface( iface );
    ULONG avail = impl->size > impl->pos ? impl->size - impl->pos : 0;
    ULONG to_read = cb < avail ? cb : avail;
    if (to_read) memcpy( pv, impl->data + impl->pos, to_read );
    impl->pos += to_read;
    if (pcbRead) *pcbRead = to_read;
    return to_read < cb ? S_FALSE : S_OK;
}

static HRESULT WINAPI mem_stream_Write( IStream *iface, const void *pv, ULONG cb, ULONG *pcbWritten ) { return E_NOTIMPL; }
static HRESULT WINAPI mem_stream_Seek( IStream *iface, LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition )
{
    struct mem_stream *impl = mem_stream_from_iface( iface );
    LONGLONG pos;
    switch (dwOrigin)
    {
    case STREAM_SEEK_SET: pos = dlibMove.QuadPart; break;
    case STREAM_SEEK_CUR: pos = (LONGLONG)impl->pos + dlibMove.QuadPart; break;
    case STREAM_SEEK_END: pos = (LONGLONG)impl->size + dlibMove.QuadPart; break;
    default: return STG_E_INVALIDFUNCTION;
    }
    if (pos < 0) return STG_E_INVALIDFUNCTION;
    impl->pos = (ULONG)pos;
    if (plibNewPosition) plibNewPosition->QuadPart = impl->pos;
    return S_OK;
}
static HRESULT WINAPI mem_stream_SetSize( IStream *iface, ULARGE_INTEGER libNewSize ) { return E_NOTIMPL; }
static HRESULT WINAPI mem_stream_CopyTo( IStream *iface, IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten ) { return E_NOTIMPL; }
static HRESULT WINAPI mem_stream_Commit( IStream *iface, DWORD grfCommitFlags ) { return S_OK; }
static HRESULT WINAPI mem_stream_Revert( IStream *iface ) { return E_NOTIMPL; }
static HRESULT WINAPI mem_stream_LockRegion( IStream *iface, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType ) { return E_NOTIMPL; }
static HRESULT WINAPI mem_stream_UnlockRegion( IStream *iface, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType ) { return E_NOTIMPL; }
static HRESULT WINAPI mem_stream_Stat( IStream *iface, STATSTG *pstatstg, DWORD grfStatFlag )
{
    struct mem_stream *impl = mem_stream_from_iface( iface );
    memset( pstatstg, 0, sizeof(*pstatstg) );
    pstatstg->type = STGTY_STREAM;
    pstatstg->cbSize.QuadPart = impl->size;
    return S_OK;
}
static HRESULT WINAPI mem_stream_Clone( IStream *iface, IStream **ppstm ) { return E_NOTIMPL; }

static const IStreamVtbl mem_stream_vtbl =
{
    mem_stream_QueryInterface,
    mem_stream_AddRef,
    mem_stream_Release,
    mem_stream_Read,
    mem_stream_Write,
    mem_stream_Seek,
    mem_stream_SetSize,
    mem_stream_CopyTo,
    mem_stream_Commit,
    mem_stream_Revert,
    mem_stream_LockRegion,
    mem_stream_UnlockRegion,
    mem_stream_Stat,
    mem_stream_Clone,
};

static HRESULT create_mem_stream( const BYTE *data, ULONG size, IStream **out )
{
    struct mem_stream *s = calloc( 1, sizeof(*s) );
    if (!s) return E_OUTOFMEMORY;
    s->IStream_iface.lpVtbl = &mem_stream_vtbl;
    s->ref = 1;
    s->data = data;
    s->size = size;
    *out = &s->IStream_iface;
    return S_OK;
}

/* --------------------------------------------------------------------- */
/* Helper: get StorageFolder for a path via Windows.Storage.StorageFolder */

static HRESULT get_storage_folder_async_for_path( const WCHAR *dir_path, void **op_out )
{
    static const WCHAR class_name[] = L"Windows.Storage.StorageFolder";
    IActivationFactory *factory;
    IStorageFolderStatics *statics;
    HSTRING class_str, path_str;
    HRESULT hr;

    if (FAILED((hr = WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, &class_str ))))
        return hr;
    hr = RoGetActivationFactory( class_str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( class_str );
    if (FAILED(hr)) return hr;

    hr = IActivationFactory_QueryInterface( factory, &IID_IStorageFolderStatics, (void **)&statics );
    IActivationFactory_Release( factory );
    if (FAILED(hr)) return hr;

    hr = WindowsCreateString( dir_path, lstrlenW( dir_path ), &path_str );
    if (SUCCEEDED(hr))
    {
        hr = IStorageFolderStatics_GetFolderFromPathAsync( statics, path_str, (IAsyncOperation_StorageFolder **)op_out );
        WindowsDeleteString( path_str );
    }
    IStorageFolderStatics_Release( statics );
    return hr;
}

static HRESULT get_storage_folder_sync_for_path( const WCHAR *dir_path, IStorageFolder **out )
{
    IAsyncOperation_StorageFolder *op = NULL;
    HRESULT hr;

    *out = NULL;
    hr = get_storage_folder_async_for_path( dir_path, (void **)&op );
    if (FAILED(hr) || !op) return hr;

    hr = IAsyncOperation_StorageFolder_GetResults( op, out );
    IAsyncOperation_StorageFolder_Release( op );
    return hr;
}

/* --------------------------------------------------------------------- */
/* Generic completed IAsyncOperation (supports object result or boolean) */

enum async_kind
{
    ASYNC_OBJECT,
    ASYNC_BOOL,
};

struct ext_async_op
{
    const void **vtbl;
    const void **info_vtbl;
    LONG ref;
    enum async_kind kind;
    IUnknown *obj_result;
    boolean bool_result;
    HRESULT error_code;
    IUnknown *handler;
    WCHAR runtime_class[160];
};

static inline struct ext_async_op *async_from_info( void *iface )
{
    return (struct ext_async_op *)((char *)iface - offsetof(struct ext_async_op, info_vtbl));
}

static HRESULT WINAPI ext_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct ext_async_op *impl = iface;
    if (IsEqualGUID( iid, &IID_IAsyncInfo_local ))
    {
        *out = &impl->info_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI ext_async_AddRef( void *iface )
{
    struct ext_async_op *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI ext_async_Release( void *iface )
{
    struct ext_async_op *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->obj_result) IUnknown_Release( impl->obj_result );
        if (impl->handler) IUnknown_Release( impl->handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI ext_async_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI ext_async_GetRuntimeClassName( void *iface, HSTRING *name )
{
    struct ext_async_op *impl = iface;
    return WindowsCreateString( impl->runtime_class, lstrlenW( impl->runtime_class ), name );
}

static HRESULT WINAPI ext_async_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

/* WinRT IAsyncOperation<T> ABI: slot 6 = put_Completed, slot 7 = get_Completed, slot 8 = GetResults */
static HRESULT WINAPI ext_async_put_Completed( void *iface, IUnknown *handler )
{
    struct ext_async_op *impl = iface;
    if (impl->handler) IUnknown_Release( impl->handler );
    impl->handler = handler;
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        IUnknown_AddRef( handler );
        if (vtbl && vtbl[6])
            ((void (WINAPI *)( void *, void *, enum AsyncStatus ))vtbl[6])( handler, iface, Completed );
    }
    return S_OK;
}

static HRESULT WINAPI ext_async_get_Completed( void *iface, IUnknown **handler )
{
    struct ext_async_op *impl = iface;
    *handler = impl->handler;
    if (*handler) IUnknown_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI ext_async_GetResults( void *iface, void *results )
{
    struct ext_async_op *impl = iface;
    if (FAILED(impl->error_code)) return impl->error_code;
    if (impl->kind == ASYNC_BOOL)
    {
        *(boolean *)results = impl->bool_result;
        return S_OK;
    }
    *(void **)results = impl->obj_result;
    if (impl->obj_result) IUnknown_AddRef( impl->obj_result );
    return S_OK;
}

static HRESULT WINAPI ext_ainfo_QueryInterface( void *iface, REFIID iid, void **out )
{
    return ext_async_QueryInterface( async_from_info( iface ), iid, out );
}
static ULONG WINAPI ext_ainfo_AddRef( void *iface ) { return ext_async_AddRef( async_from_info( iface ) ); }
static ULONG WINAPI ext_ainfo_Release( void *iface ) { return ext_async_Release( async_from_info( iface ) ); }
static HRESULT WINAPI ext_ainfo_GetIids( void *iface, ULONG *c, IID **i ) { *c = 0; *i = NULL; return S_OK; }
static HRESULT WINAPI ext_ainfo_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, n );
}
static HRESULT WINAPI ext_ainfo_GetTrustLevel( void *iface, TrustLevel *l ) { *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ext_ainfo_get_Id( void *iface, UINT32 *id ) { *id = 1; return S_OK; }
static HRESULT WINAPI ext_ainfo_get_Status( void *iface, enum AsyncStatus *status )
{
    struct ext_async_op *impl = async_from_info( iface );
    *status = FAILED(impl->error_code) ? Error : Completed;
    return S_OK;
}
static HRESULT WINAPI ext_ainfo_get_ErrorCode( void *iface, HRESULT *code )
{
    *code = async_from_info( iface )->error_code;
    return S_OK;
}
static HRESULT WINAPI ext_ainfo_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI ext_ainfo_Close( void *iface ) { return S_OK; }

static const void *ext_async_vtbl[] =
{
    ext_async_QueryInterface,
    ext_async_AddRef,
    ext_async_Release,
    ext_async_GetIids,
    ext_async_GetRuntimeClassName,
    ext_async_GetTrustLevel,
    ext_async_put_Completed,  /* 6 */
    ext_async_get_Completed,  /* 7 */
    ext_async_GetResults,     /* 8 */
};

static const void *ext_ainfo_vtbl[] =
{
    ext_ainfo_QueryInterface,
    ext_ainfo_AddRef,
    ext_ainfo_Release,
    ext_ainfo_GetIids,
    ext_ainfo_GetRuntimeClassName,
    ext_ainfo_GetTrustLevel,
    ext_ainfo_get_Id,           /* 6 */
    ext_ainfo_get_Status,       /* 7 */
    ext_ainfo_get_ErrorCode,    /* 8 */
    ext_ainfo_Cancel,           /* 9 */
    ext_ainfo_Close,            /* 10 */
};

static HRESULT create_async_op_obj( const WCHAR *runtime_class, IUnknown *result, HRESULT err, void **out )
{
    struct ext_async_op *op = calloc( 1, sizeof(*op) );
    if (!op) return E_OUTOFMEMORY;
    op->vtbl = ext_async_vtbl;
    op->info_vtbl = ext_ainfo_vtbl;
    op->ref = 1;
    op->kind = ASYNC_OBJECT;
    op->obj_result = result;
    if (result) IUnknown_AddRef( result );
    op->error_code = err;
    lstrcpynW( op->runtime_class, runtime_class, ARRAY_SIZE(op->runtime_class) );
    *out = op;
    return S_OK;
}

static HRESULT create_async_op_bool( const WCHAR *runtime_class, boolean val, HRESULT err, void **out )
{
    struct ext_async_op *op = calloc( 1, sizeof(*op) );
    if (!op) return E_OUTOFMEMORY;
    op->vtbl = ext_async_vtbl;
    op->info_vtbl = ext_ainfo_vtbl;
    op->ref = 1;
    op->kind = ASYNC_BOOL;
    op->bool_result = val;
    op->error_code = err;
    lstrcpynW( op->runtime_class, runtime_class, ARRAY_SIZE(op->runtime_class) );
    *out = op;
    return S_OK;
}

/* --------------------------------------------------------------------- */
/* Package metadata parsed from AppModel registry + AppxManifest.xml */

struct pkg_metadata
{
    LONG ref;
    WCHAR name[256];
    WCHAR full_name[256];
    WCHAR family_name[256];
    WCHAR publisher[512];
    WCHAR publisher_id[64];
    WCHAR resource_id[128];
    WCHAR path[MAX_PATH];
    WCHAR display_name[256];
    WCHAR publisher_display_name[256];
    WCHAR description[512];
    struct PackageVersion version;
    enum ProcessorArchitecture arch;
    BOOL is_framework;
};

static void pkg_metadata_addref( struct pkg_metadata *m ) { InterlockedIncrement( &m->ref ); }
static void pkg_metadata_release( struct pkg_metadata *m )
{
    if (InterlockedDecrement( &m->ref ) == 0) free( m );
}

/* --------------------------------------------------------------------- */
/* IPackageId for an installed package */

struct ext_pkg_id
{
    IPackageId IPackageId_iface;
    LONG ref;
    struct pkg_metadata *meta;
};

static inline struct ext_pkg_id *pkg_id_from_iface( IPackageId *iface )
{
    return CONTAINING_RECORD( iface, struct ext_pkg_id, IPackageId_iface );
}

static HRESULT WINAPI ext_pkg_id_QueryInterface( IPackageId *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPackageId ))
    {
        *out = iface;
        IPackageId_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ext_pkg_id_AddRef( IPackageId *iface )
{
    return InterlockedIncrement( &pkg_id_from_iface( iface )->ref );
}

static ULONG WINAPI ext_pkg_id_Release( IPackageId *iface )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        pkg_metadata_release( impl->meta );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI ext_pkg_id_GetIids( IPackageId *iface, ULONG *count, IID **iids )
{
    IID *arr = CoTaskMemAlloc( sizeof(IID) );
    if (!arr) return E_OUTOFMEMORY;
    arr[0] = IID_IPackageId;
    *count = 1;
    *iids = arr;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_id_GetRuntimeClassName( IPackageId *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.PackageId", 34, name );
}

static HRESULT WINAPI ext_pkg_id_GetTrustLevel( IPackageId *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_id_get_Name( IPackageId *iface, HSTRING *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    return WindowsCreateString( impl->meta->name, lstrlenW( impl->meta->name ), value );
}

static HRESULT WINAPI ext_pkg_id_get_Version( IPackageId *iface, struct PackageVersion *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->meta->version;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_id_get_Architecture( IPackageId *iface, enum ProcessorArchitecture *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->meta->arch;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_id_get_ResourceId( IPackageId *iface, HSTRING *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    return WindowsCreateString( impl->meta->resource_id, lstrlenW( impl->meta->resource_id ), value );
}

static HRESULT WINAPI ext_pkg_id_get_Publisher( IPackageId *iface, HSTRING *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    return WindowsCreateString( impl->meta->publisher, lstrlenW( impl->meta->publisher ), value );
}

static HRESULT WINAPI ext_pkg_id_get_PublisherId( IPackageId *iface, HSTRING *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    return WindowsCreateString( impl->meta->publisher_id, lstrlenW( impl->meta->publisher_id ), value );
}

static HRESULT WINAPI ext_pkg_id_get_FullName( IPackageId *iface, HSTRING *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    return WindowsCreateString( impl->meta->full_name, lstrlenW( impl->meta->full_name ), value );
}

static HRESULT WINAPI ext_pkg_id_get_FamilyName( IPackageId *iface, HSTRING *value )
{
    struct ext_pkg_id *impl = pkg_id_from_iface( iface );
    return WindowsCreateString( impl->meta->family_name, lstrlenW( impl->meta->family_name ), value );
}

static const struct IPackageIdVtbl ext_pkg_id_vtbl =
{
    ext_pkg_id_QueryInterface,
    ext_pkg_id_AddRef,
    ext_pkg_id_Release,
    ext_pkg_id_GetIids,
    ext_pkg_id_GetRuntimeClassName,
    ext_pkg_id_GetTrustLevel,
    ext_pkg_id_get_Name,
    ext_pkg_id_get_Version,
    ext_pkg_id_get_Architecture,
    ext_pkg_id_get_ResourceId,
    ext_pkg_id_get_Publisher,
    ext_pkg_id_get_PublisherId,
    ext_pkg_id_get_FullName,
    ext_pkg_id_get_FamilyName,
};

/* --------------------------------------------------------------------- */
/* Empty IVectorView<Package*> for IPackage::get_Dependencies */

struct empty_pkg_vec
{
    IVectorView_Package IVectorView_Package_iface;
    LONG ref;
};

static HRESULT WINAPI empty_pkg_vec_QI( IVectorView_Package *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IVectorView_Package ))
    {
        *out = iface;
        IVectorView_Package_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI empty_pkg_vec_AddRef( IVectorView_Package *iface )
{
    return InterlockedIncrement( &CONTAINING_RECORD( iface, struct empty_pkg_vec, IVectorView_Package_iface )->ref );
}
static ULONG WINAPI empty_pkg_vec_Release( IVectorView_Package *iface )
{
    struct empty_pkg_vec *impl = CONTAINING_RECORD( iface, struct empty_pkg_vec, IVectorView_Package_iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI empty_pkg_vec_GetIids( IVectorView_Package *iface, ULONG *c, IID **i ) { *c = 0; *i = NULL; return S_OK; }
static HRESULT WINAPI empty_pkg_vec_GetRCN( IVectorView_Package *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Package>", 78, n );
}
static HRESULT WINAPI empty_pkg_vec_GetTL( IVectorView_Package *iface, TrustLevel *l ) { *l = BaseTrust; return S_OK; }
static HRESULT WINAPI empty_pkg_vec_GetAt( IVectorView_Package *iface, UINT32 idx, IPackage **item ) { return E_BOUNDS; }
static HRESULT WINAPI empty_pkg_vec_get_Size( IVectorView_Package *iface, UINT32 *size ) { *size = 0; return S_OK; }
static HRESULT WINAPI empty_pkg_vec_IndexOf( IVectorView_Package *iface, IPackage *item, UINT32 *idx, boolean *found )
{
    *idx = 0; *found = FALSE; return S_OK;
}
static HRESULT WINAPI empty_pkg_vec_GetMany( IVectorView_Package *iface, UINT32 start, UINT32 cap, IPackage **items, UINT32 *actual )
{
    *actual = 0; return S_OK;
}
static const struct IVectorView_PackageVtbl empty_pkg_vec_vtbl =
{
    empty_pkg_vec_QI,
    empty_pkg_vec_AddRef,
    empty_pkg_vec_Release,
    empty_pkg_vec_GetIids,
    empty_pkg_vec_GetRCN,
    empty_pkg_vec_GetTL,
    empty_pkg_vec_GetAt,
    empty_pkg_vec_get_Size,
    empty_pkg_vec_IndexOf,
    empty_pkg_vec_GetMany,
};

/* --------------------------------------------------------------------- */
/* IPackage / IPackage2 / IPackage4 for an installed package */

struct ext_pkg
{
    IPackage IPackage_iface;
    IPackage2 IPackage2_iface;
    const void **IPackage4_vtbl;
    LONG ref;
    struct pkg_metadata *meta;
};

static inline struct ext_pkg *ext_pkg_from_IPackage( IPackage *iface )
{
    return CONTAINING_RECORD( iface, struct ext_pkg, IPackage_iface );
}
static inline struct ext_pkg *ext_pkg_from_IPackage2( IPackage2 *iface )
{
    return CONTAINING_RECORD( iface, struct ext_pkg, IPackage2_iface );
}
static inline struct ext_pkg *ext_pkg_from_IPackage4( void *iface )
{
    return CONTAINING_RECORD( iface, struct ext_pkg, IPackage4_vtbl );
}

static HRESULT WINAPI ext_pkg_QueryInterface( IPackage *iface, REFIID iid, void **out )
{
    struct ext_pkg *impl = ext_pkg_from_IPackage( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPackage ))
    {
        *out = &impl->IPackage_iface;
        IPackage_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPackage2 ))
    {
        *out = &impl->IPackage2_iface;
        IPackage_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPackage4 ))
    {
        *out = &impl->IPackage4_vtbl;
        IPackage_AddRef( iface );
        return S_OK;
    }
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ext_pkg_AddRef( IPackage *iface )
{
    return InterlockedIncrement( &ext_pkg_from_IPackage( iface )->ref );
}

static ULONG WINAPI ext_pkg_Release( IPackage *iface )
{
    struct ext_pkg *impl = ext_pkg_from_IPackage( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        pkg_metadata_release( impl->meta );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI ext_pkg_GetIids( IPackage *iface, ULONG *count, IID **iids )
{
    IID *arr = CoTaskMemAlloc( 3 * sizeof(IID) );
    if (!arr) return E_OUTOFMEMORY;
    arr[0] = IID_IPackage;
    arr[1] = IID_IPackage2;
    arr[2] = IID_IPackage4;
    *count = 3;
    *iids = arr;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_GetRuntimeClassName( IPackage *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Package", 32, name );
}

static HRESULT WINAPI ext_pkg_GetTrustLevel( IPackage *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_get_Id( IPackage *iface, IPackageId **value )
{
    struct ext_pkg *impl = ext_pkg_from_IPackage( iface );
    struct ext_pkg_id *id = calloc( 1, sizeof(*id) );
    if (!id) return E_OUTOFMEMORY;
    id->IPackageId_iface.lpVtbl = &ext_pkg_id_vtbl;
    id->ref = 1;
    id->meta = impl->meta;
    pkg_metadata_addref( impl->meta );
    *value = &id->IPackageId_iface;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_get_InstalledLocation( IPackage *iface, IStorageFolder **value )
{
    struct ext_pkg *impl = ext_pkg_from_IPackage( iface );
    return get_storage_folder_sync_for_path( impl->meta->path, value );
}

static HRESULT WINAPI ext_pkg_get_IsFramework( IPackage *iface, boolean *value )
{
    struct ext_pkg *impl = ext_pkg_from_IPackage( iface );
    *value = impl->meta->is_framework;
    return S_OK;
}

static HRESULT WINAPI ext_pkg_get_Dependencies( IPackage *iface, IVectorView_Package **value )
{
    struct empty_pkg_vec *vec = calloc( 1, sizeof(*vec) );
    if (!vec) return E_OUTOFMEMORY;
    vec->IVectorView_Package_iface.lpVtbl = &empty_pkg_vec_vtbl;
    vec->ref = 1;
    *value = &vec->IVectorView_Package_iface;
    return S_OK;
}

static const struct IPackageVtbl ext_pkg_vtbl =
{
    ext_pkg_QueryInterface,
    ext_pkg_AddRef,
    ext_pkg_Release,
    ext_pkg_GetIids,
    ext_pkg_GetRuntimeClassName,
    ext_pkg_GetTrustLevel,
    ext_pkg_get_Id,
    ext_pkg_get_InstalledLocation,
    ext_pkg_get_IsFramework,
    ext_pkg_get_Dependencies,
};

DEFINE_IINSPECTABLE( ext_pkg2, IPackage2, struct ext_pkg, IPackage_iface )

static HRESULT WINAPI ext_pkg2_get_DisplayName( IPackage2 *iface, HSTRING *value )
{
    struct ext_pkg *impl = impl_from_IPackage2( iface );
    return WindowsCreateString( impl->meta->display_name, lstrlenW( impl->meta->display_name ), value );
}

static HRESULT WINAPI ext_pkg2_get_PublisherDisplayName( IPackage2 *iface, HSTRING *value )
{
    struct ext_pkg *impl = impl_from_IPackage2( iface );
    return WindowsCreateString( impl->meta->publisher_display_name, lstrlenW( impl->meta->publisher_display_name ), value );
}

static HRESULT WINAPI ext_pkg2_get_Description( IPackage2 *iface, HSTRING *value )
{
    struct ext_pkg *impl = impl_from_IPackage2( iface );
    return WindowsCreateString( impl->meta->description, lstrlenW( impl->meta->description ), value );
}

static HRESULT WINAPI ext_pkg2_get_Logo( IPackage2 *iface, IUriRuntimeClass **value )
{
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI ext_pkg2_get_IsResourcePackage( IPackage2 *iface, boolean *value )
{
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI ext_pkg2_get_IsBundle( IPackage2 *iface, boolean *value )
{
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI ext_pkg2_get_IsDevelopmentMode( IPackage2 *iface, boolean *value )
{
    *value = FALSE;
    return S_OK;
}

static const struct IPackage2Vtbl ext_pkg2_vtbl =
{
    ext_pkg2_QueryInterface,
    ext_pkg2_AddRef,
    ext_pkg2_Release,
    ext_pkg2_GetIids,
    ext_pkg2_GetRuntimeClassName,
    ext_pkg2_GetTrustLevel,
    ext_pkg2_get_DisplayName,
    ext_pkg2_get_PublisherDisplayName,
    ext_pkg2_get_Description,
    ext_pkg2_get_Logo,
    ext_pkg2_get_IsResourcePackage,
    ext_pkg2_get_IsBundle,
    ext_pkg2_get_IsDevelopmentMode,
};

static HRESULT WINAPI ext_pkg4_QueryInterface( void *iface, REFIID iid, void **out )
{
    return ext_pkg_QueryInterface( &ext_pkg_from_IPackage4( iface )->IPackage_iface, iid, out );
}
static ULONG WINAPI ext_pkg4_AddRef( void *iface ) { return ext_pkg_AddRef( &ext_pkg_from_IPackage4( iface )->IPackage_iface ); }
static ULONG WINAPI ext_pkg4_Release( void *iface ) { return ext_pkg_Release( &ext_pkg_from_IPackage4( iface )->IPackage_iface ); }
static HRESULT WINAPI ext_pkg4_GetIids( void *iface, ULONG *c, IID **i ) { return ext_pkg_GetIids( &ext_pkg_from_IPackage4( iface )->IPackage_iface, c, i ); }
static HRESULT WINAPI ext_pkg4_GetRCN( void *iface, HSTRING *n ) { return ext_pkg_GetRuntimeClassName( &ext_pkg_from_IPackage4( iface )->IPackage_iface, n ); }
static HRESULT WINAPI ext_pkg4_GetTL( void *iface, TrustLevel *l ) { return ext_pkg_GetTrustLevel( &ext_pkg_from_IPackage4( iface )->IPackage_iface, l ); }
static HRESULT WINAPI ext_pkg4_get_SignatureKind( void *iface, INT32 *value ) { *value = 3; /* Store */ return S_OK; }
static HRESULT WINAPI ext_pkg4_get_IsOptional( void *iface, boolean *value ) { *value = FALSE; return S_OK; }
static HRESULT WINAPI ext_pkg4_VerifyContentIntegrityAsync( void *iface, void **op )
{
    TRACE( "iface %p, op %p.\n", iface, op );
    return create_async_op_bool( L"Windows.Foundation.IAsyncOperation`1<Boolean>", TRUE, S_OK, op );
}

static const void *ext_pkg4_vtbl[] =
{
    ext_pkg4_QueryInterface,
    ext_pkg4_AddRef,
    ext_pkg4_Release,
    ext_pkg4_GetIids,
    ext_pkg4_GetRCN,
    ext_pkg4_GetTL,
    ext_pkg4_get_SignatureKind,           /* 6 */
    ext_pkg4_get_IsOptional,              /* 7 */
    ext_pkg4_VerifyContentIntegrityAsync, /* 8 */
};

static HRESULT create_ext_pkg( struct pkg_metadata *meta, IPackage **out )
{
    struct ext_pkg *pkg = calloc( 1, sizeof(*pkg) );
    if (!pkg) return E_OUTOFMEMORY;
    pkg->IPackage_iface.lpVtbl = &ext_pkg_vtbl;
    pkg->IPackage2_iface.lpVtbl = &ext_pkg2_vtbl;
    pkg->IPackage4_vtbl = ext_pkg4_vtbl;
    pkg->ref = 1;
    pkg->meta = meta;
    pkg_metadata_addref( meta );
    *out = &pkg->IPackage_iface;
    return S_OK;
}

/* --------------------------------------------------------------------- */
/* IAppInfo / IAppInfo2 for an AppExtension's enclosing Application */

struct ext_appinfo
{
    IAppInfo IAppInfo_iface;
    IAppInfo2 IAppInfo2_iface;
    LONG ref;
    struct pkg_metadata *meta;
    WCHAR app_id[128];
};

static inline struct ext_appinfo *ext_appinfo_from_IAppInfo( IAppInfo *iface )
{
    return CONTAINING_RECORD( iface, struct ext_appinfo, IAppInfo_iface );
}

static HRESULT WINAPI ext_appinfo_QueryInterface( IAppInfo *iface, REFIID iid, void **out )
{
    struct ext_appinfo *impl = ext_appinfo_from_IAppInfo( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAppInfo ))
    {
        *out = &impl->IAppInfo_iface;
        IAppInfo_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppInfo2 ))
    {
        *out = &impl->IAppInfo2_iface;
        IAppInfo_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ext_appinfo_AddRef( IAppInfo *iface )
{
    return InterlockedIncrement( &ext_appinfo_from_IAppInfo( iface )->ref );
}

static ULONG WINAPI ext_appinfo_Release( IAppInfo *iface )
{
    struct ext_appinfo *impl = ext_appinfo_from_IAppInfo( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        pkg_metadata_release( impl->meta );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI ext_appinfo_GetIids( IAppInfo *iface, ULONG *count, IID **iids )
{
    IID *arr = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!arr) return E_OUTOFMEMORY;
    arr[0] = IID_IAppInfo;
    arr[1] = IID_IAppInfo2;
    *count = 2;
    *iids = arr;
    return S_OK;
}

static HRESULT WINAPI ext_appinfo_GetRuntimeClassName( IAppInfo *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.AppInfo", 32, name );
}

static HRESULT WINAPI ext_appinfo_GetTrustLevel( IAppInfo *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI ext_appinfo_get_Id( IAppInfo *iface, HSTRING *value )
{
    struct ext_appinfo *impl = ext_appinfo_from_IAppInfo( iface );
    return WindowsCreateString( impl->app_id, lstrlenW( impl->app_id ), value );
}

static HRESULT WINAPI ext_appinfo_get_AppUserModelId( IAppInfo *iface, HSTRING *value )
{
    struct ext_appinfo *impl = ext_appinfo_from_IAppInfo( iface );
    WCHAR aumid[512];
    swprintf( aumid, ARRAY_SIZE(aumid), L"%ls!%ls", impl->meta->family_name, impl->app_id );
    return WindowsCreateString( aumid, lstrlenW( aumid ), value );
}

static HRESULT WINAPI ext_appinfo_get_DisplayInfo( IAppInfo *iface, IAppDisplayInfo **value )
{
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI ext_appinfo_get_PackageFamilyName( IAppInfo *iface, HSTRING *value )
{
    struct ext_appinfo *impl = ext_appinfo_from_IAppInfo( iface );
    return WindowsCreateString( impl->meta->family_name, lstrlenW( impl->meta->family_name ), value );
}

static const struct IAppInfoVtbl ext_appinfo_vtbl =
{
    ext_appinfo_QueryInterface,
    ext_appinfo_AddRef,
    ext_appinfo_Release,
    ext_appinfo_GetIids,
    ext_appinfo_GetRuntimeClassName,
    ext_appinfo_GetTrustLevel,
    ext_appinfo_get_Id,
    ext_appinfo_get_AppUserModelId,
    ext_appinfo_get_DisplayInfo,
    ext_appinfo_get_PackageFamilyName,
};

DEFINE_IINSPECTABLE( ext_appinfo2, IAppInfo2, struct ext_appinfo, IAppInfo_iface )

static HRESULT WINAPI ext_appinfo2_get_Package( IAppInfo2 *iface, IPackage **value )
{
    struct ext_appinfo *impl = impl_from_IAppInfo2( iface );
    return create_ext_pkg( impl->meta, value );
}

static const struct IAppInfo2Vtbl ext_appinfo2_vtbl =
{
    ext_appinfo2_QueryInterface,
    ext_appinfo2_AddRef,
    ext_appinfo2_Release,
    ext_appinfo2_GetIids,
    ext_appinfo2_GetRuntimeClassName,
    ext_appinfo2_GetTrustLevel,
    ext_appinfo2_get_Package,
};

static HRESULT create_ext_appinfo( struct pkg_metadata *meta, const WCHAR *app_id, IAppInfo **out )
{
    struct ext_appinfo *ai = calloc( 1, sizeof(*ai) );
    if (!ai) return E_OUTOFMEMORY;
    ai->IAppInfo_iface.lpVtbl = &ext_appinfo_vtbl;
    ai->IAppInfo2_iface.lpVtbl = &ext_appinfo2_vtbl;
    ai->ref = 1;
    ai->meta = meta;
    pkg_metadata_addref( meta );
    lstrcpynW( ai->app_id, app_id, ARRAY_SIZE(ai->app_id) );
    *out = &ai->IAppInfo_iface;
    return S_OK;
}

/* --------------------------------------------------------------------- */
/* IAppExtension */

struct app_extension
{
    const void **vtbl;
    LONG ref;
    struct pkg_metadata *meta;
    WCHAR id[256];
    WCHAR display_name[256];
    WCHAR description[512];
    WCHAR public_folder[MAX_PATH];
    WCHAR app_id[128];
};

static HRESULT WINAPI appext_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct app_extension *impl = iface;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAppExtension ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI appext_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct app_extension *)iface)->ref );
}

static ULONG WINAPI appext_Release( void *iface )
{
    struct app_extension *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        pkg_metadata_release( impl->meta );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI appext_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *arr = CoTaskMemAlloc( sizeof(IID) );
    if (!arr) return E_OUTOFMEMORY;
    arr[0] = IID_IAppExtension;
    *count = 1;
    *iids = arr;
    return S_OK;
}

static HRESULT WINAPI appext_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.AppExtensions.AppExtension", 51, name );
}

static HRESULT WINAPI appext_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI appext_get_Id( void *iface, HSTRING *value )
{
    struct app_extension *impl = iface;
    return WindowsCreateString( impl->id, lstrlenW( impl->id ), value );
}

static HRESULT WINAPI appext_get_DisplayName( void *iface, HSTRING *value )
{
    struct app_extension *impl = iface;
    return WindowsCreateString( impl->display_name, lstrlenW( impl->display_name ), value );
}

static HRESULT WINAPI appext_get_Description( void *iface, HSTRING *value )
{
    struct app_extension *impl = iface;
    return WindowsCreateString( impl->description, lstrlenW( impl->description ), value );
}

static HRESULT WINAPI appext_get_Package( void *iface, IPackage **value )
{
    struct app_extension *impl = iface;
    return create_ext_pkg( impl->meta, value );
}

static HRESULT WINAPI appext_get_AppInfo( void *iface, IAppInfo **value )
{
    struct app_extension *impl = iface;
    return create_ext_appinfo( impl->meta, impl->app_id, value );
}

static HRESULT WINAPI appext_GetExtensionPropertiesAsync( void *iface, void **op )
{
    static const WCHAR propset_cls[] = L"Windows.Foundation.Collections.PropertySet";
    HSTRING cls;
    IInspectable *propset = NULL;
    HRESULT hr;

    if (FAILED((hr = WindowsCreateString( propset_cls, ARRAY_SIZE(propset_cls) - 1, &cls ))))
        return hr;
    hr = RoActivateInstance( cls, &propset );
    WindowsDeleteString( cls );
    if (FAILED(hr)) return hr;

    hr = create_async_op_obj( L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IPropertySet>",
                              (IUnknown *)propset, S_OK, op );
    IInspectable_Release( propset );
    return hr;
}

static HRESULT WINAPI appext_GetPublicFolderAsync( void *iface, void **op )
{
    struct app_extension *impl = iface;
    if (!impl->public_folder[0])
        return create_async_op_obj( L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
                                    NULL, S_OK, op );
    else
    {
        WCHAR full[MAX_PATH];
        swprintf( full, ARRAY_SIZE(full), L"%ls\\%ls", impl->meta->path, impl->public_folder );
        return get_storage_folder_async_for_path( full, op );
    }
}

static const void *appext_vtbl[] =
{
    appext_QueryInterface,
    appext_AddRef,
    appext_Release,
    appext_GetIids,
    appext_GetRuntimeClassName,
    appext_GetTrustLevel,
    appext_get_Id,                        /* 6 */
    appext_get_DisplayName,               /* 7 */
    appext_get_Description,               /* 8 */
    appext_get_Package,                   /* 9 */
    appext_get_AppInfo,                   /* 10 */
    appext_GetExtensionPropertiesAsync,   /* 11 */
    appext_GetPublicFolderAsync,          /* 12 */
};

/* --------------------------------------------------------------------- */
/* IVectorView<AppExtension*> + IIterator<AppExtension*> */

struct appext_list
{
    const void **vtbl;
    LONG ref;
    struct app_extension **items;
    UINT32 count;
};

struct appext_iter
{
    const void **vtbl;
    LONG ref;
    struct appext_list *list;
    UINT32 index;
};

static HRESULT WINAPI appext_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( &((struct appext_iter *)iface)->ref );
    return S_OK;
}
static ULONG WINAPI appext_iter_AddRef( void *iface ) { return InterlockedIncrement( &((struct appext_iter *)iface)->ref ); }
static ULONG WINAPI appext_iter_Release( void *iface )
{
    struct appext_iter *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        ((ULONG (WINAPI *)( void * ))impl->list->vtbl[2])( impl->list );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI appext_iter_GetIids( void *iface, ULONG *c, IID **i ) { *c = 0; *i = NULL; return S_OK; }
static HRESULT WINAPI appext_iter_GetRCN( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterator`1<Windows.ApplicationModel.AppExtensions.AppExtension>", 101, n );
}
static HRESULT WINAPI appext_iter_GetTL( void *iface, TrustLevel *l ) { *l = BaseTrust; return S_OK; }
static HRESULT WINAPI appext_iter_get_Current( void *iface, void **item )
{
    struct appext_iter *impl = iface;
    if (impl->index >= impl->list->count) return E_BOUNDS;
    *item = impl->list->items[impl->index];
    appext_AddRef( *item );
    return S_OK;
}
static HRESULT WINAPI appext_iter_get_HasCurrent( void *iface, boolean *value )
{
    struct appext_iter *impl = iface;
    *value = impl->index < impl->list->count;
    return S_OK;
}
static HRESULT WINAPI appext_iter_MoveNext( void *iface, boolean *value )
{
    struct appext_iter *impl = iface;
    if (impl->index < impl->list->count) impl->index++;
    *value = impl->index < impl->list->count;
    return S_OK;
}
static HRESULT WINAPI appext_iter_GetMany( void *iface, UINT32 count, void **items, UINT32 *read )
{
    struct appext_iter *impl = iface;
    UINT32 n = 0;
    while (n < count && impl->index < impl->list->count)
    {
        items[n] = impl->list->items[impl->index++];
        appext_AddRef( items[n] );
        n++;
    }
    *read = n;
    return S_OK;
}

static const void *appext_iter_vtbl[] =
{
    appext_iter_QueryInterface,
    appext_iter_AddRef,
    appext_iter_Release,
    appext_iter_GetIids,
    appext_iter_GetRCN,
    appext_iter_GetTL,
    appext_iter_get_Current,     /* 6 */
    appext_iter_get_HasCurrent,  /* 7 */
    appext_iter_MoveNext,        /* 8 */
    appext_iter_GetMany,         /* 9 */
};

static HRESULT WINAPI appext_view_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( &((struct appext_list *)iface)->ref );
    return S_OK;
}
static ULONG WINAPI appext_view_AddRef( void *iface ) { return InterlockedIncrement( &((struct appext_list *)iface)->ref ); }
static ULONG WINAPI appext_view_Release( void *iface )
{
    struct appext_list *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; i++) appext_Release( impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI appext_view_GetIids( void *iface, ULONG *c, IID **i ) { *c = 0; *i = NULL; return S_OK; }
static HRESULT WINAPI appext_view_GetRCN( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.AppExtensions.AppExtension>", 103, n );
}
static HRESULT WINAPI appext_view_GetTL( void *iface, TrustLevel *l ) { *l = BaseTrust; return S_OK; }
static HRESULT WINAPI appext_view_GetAt( void *iface, UINT32 index, void **item )
{
    struct appext_list *impl = iface;
    if (index >= impl->count) return E_BOUNDS;
    *item = impl->items[index];
    appext_AddRef( *item );
    return S_OK;
}
static HRESULT WINAPI appext_view_get_Size( void *iface, UINT32 *size )
{
    *size = ((struct appext_list *)iface)->count;
    return S_OK;
}
static HRESULT WINAPI appext_view_IndexOf( void *iface, void *item, UINT32 *index, boolean *found )
{
    struct appext_list *impl = iface;
    UINT32 i;
    for (i = 0; i < impl->count; i++)
    {
        if (impl->items[i] == item) { *index = i; *found = TRUE; return S_OK; }
    }
    *index = 0;
    *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI appext_view_GetMany( void *iface, UINT32 start, UINT32 cap, void **items, UINT32 *actual )
{
    struct appext_list *impl = iface;
    UINT32 n = 0;
    while (n < cap && start + n < impl->count)
    {
        items[n] = impl->items[start + n];
        appext_AddRef( items[n] );
        n++;
    }
    *actual = n;
    return S_OK;
}
static HRESULT WINAPI appext_view_First( void *iface, void **first )
{
    struct appext_list *impl = iface;
    struct appext_iter *it = calloc( 1, sizeof(*it) );
    if (!it) return E_OUTOFMEMORY;
    it->vtbl = appext_iter_vtbl;
    it->ref = 1;
    it->list = impl;
    appext_view_AddRef( impl );
    *first = it;
    return S_OK;
}

/* Note: IVectorView<T> vtable: 6 GetAt, 7 get_Size, 8 IndexOf, 9 GetMany;
 * IIterable<T> vtable: 6 First. Put GetMany at 9 or First?
 * In WinRT ABI, IVectorView<T> does NOT inherit IIterable<T> in the same vtable —
 * IIterable<T> is a separate QI interface, though some C++ callers expect First on slot 9
 * only if they don't QI IIterable. Keep GetMany at 9 and handle IIterable if needed;
 * WindowsPackageManager uses get_Size(7) + GetAt(6) in a loop. */
static const void *appext_view_vtbl[] =
{
    appext_view_QueryInterface,
    appext_view_AddRef,
    appext_view_Release,
    appext_view_GetIids,
    appext_view_GetRCN,
    appext_view_GetTL,
    appext_view_GetAt,      /* 6 */
    appext_view_get_Size,   /* 7 */
    appext_view_IndexOf,    /* 8 */
    appext_view_GetMany,    /* 9 */
    appext_view_First,      /* 10 */
};

/* --------------------------------------------------------------------- */
/* Generic AppxManifest.xml scanner over registered AppModel\Packages */

typedef HRESULT (WINAPI *pCreateXmlReader)( REFIID, void **, IMalloc * );

static enum ProcessorArchitecture parse_arch( const WCHAR *s )
{
    if (!s || !wcsicmp( s, L"neutral" )) return ProcessorArchitecture_Neutral;
    if (!wcsicmp( s, L"x86" )) return ProcessorArchitecture_X86;
    if (!wcsicmp( s, L"x64" )) return ProcessorArchitecture_X64;
    if (!wcsicmp( s, L"arm" )) return ProcessorArchitecture_Arm;
    if (!wcsicmp( s, L"arm64" )) return ProcessorArchitecture_Arm64;
    return ProcessorArchitecture_Unknown;
}

static int compare_version( const struct PackageVersion *a, const struct PackageVersion *b )
{
    if (a->Major != b->Major) return a->Major > b->Major ? 1 : -1;
    if (a->Minor != b->Minor) return a->Minor > b->Minor ? 1 : -1;
    if (a->Build != b->Build) return a->Build > b->Build ? 1 : -1;
    if (a->Revision != b->Revision) return a->Revision > b->Revision ? 1 : -1;
    return 0;
}

static void scan_package_manifest( pCreateXmlReader fn_create_reader, const WCHAR *ext_name,
                                   const WCHAR *pkg_full_name, const WCHAR *pkg_family_name,
                                   const WCHAR *pkg_pub_id, const WCHAR *pkg_path,
                                   struct appext_list *list )
{
    WCHAR manifest_path[MAX_PATH];
    HANDLE hfile;
    LARGE_INTEGER fsz;
    BYTE *buf;
    DWORD rd = 0;
    IStream *stream = NULL;
    IXmlReader *reader = NULL;
    XmlNodeType node_type;
    struct pkg_metadata *meta;
    WCHAR cur_app_id[128] = L"App";
    WCHAR cur_elem[64] = {0};
    BOOL in_props = FALSE;

    swprintf( manifest_path, ARRAY_SIZE(manifest_path), L"%ls\\AppxManifest.xml", pkg_path );
    hfile = CreateFileW( manifest_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL );
    if (hfile == INVALID_HANDLE_VALUE) return;

    if (!GetFileSizeEx( hfile, &fsz ) || fsz.QuadPart <= 0 || fsz.QuadPart > 4 * 1024 * 1024)
    {
        CloseHandle( hfile );
        return;
    }
    buf = malloc( (SIZE_T)fsz.QuadPart );
    if (!buf || !ReadFile( hfile, buf, (DWORD)fsz.QuadPart, &rd, NULL ))
    {
        free( buf );
        CloseHandle( hfile );
        return;
    }
    CloseHandle( hfile );

    if (FAILED(create_mem_stream( buf, rd, &stream )))
    {
        free( buf );
        return;
    }
    if (FAILED(fn_create_reader( &IID_IXmlReader_local, (void **)&reader, NULL )))
    {
        IStream_Release( stream );
        free( buf );
        return;
    }
    IXmlReader_SetInput( reader, (IUnknown *)stream );

    meta = calloc( 1, sizeof(*meta) );
    if (!meta)
    {
        IXmlReader_Release( reader );
        IStream_Release( stream );
        free( buf );
        return;
    }
    meta->ref = 1;
    lstrcpynW( meta->full_name, pkg_full_name, ARRAY_SIZE(meta->full_name) );
    lstrcpynW( meta->family_name, pkg_family_name, ARRAY_SIZE(meta->family_name) );
    lstrcpynW( meta->publisher_id, pkg_pub_id, ARRAY_SIZE(meta->publisher_id) );
    lstrcpynW( meta->path, pkg_path, ARRAY_SIZE(meta->path) );

    while (IXmlReader_Read( reader, &node_type ) == S_OK)
    {
        const WCHAR *local_name = NULL;
        if (node_type == XmlNodeType_Element)
        {
            BOOL is_empty = IXmlReader_IsEmptyElement( reader );
            IXmlReader_GetLocalName( reader, &local_name, NULL );
            if (!local_name) continue;

            if (!wcscmp( local_name, L"Identity" ))
            {
                if (IXmlReader_MoveToFirstAttribute( reader ) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName( reader, &aname, NULL );
                        IXmlReader_GetValue( reader, &aval, NULL );
                        if (!aname || !aval) continue;
                        if (!wcscmp( aname, L"Name" ))
                            lstrcpynW( meta->name, aval, ARRAY_SIZE(meta->name) );
                        else if (!wcscmp( aname, L"Publisher" ))
                            lstrcpynW( meta->publisher, aval, ARRAY_SIZE(meta->publisher) );
                        else if (!wcscmp( aname, L"Version" ))
                            swscanf( aval, L"%hu.%hu.%hu.%hu", &meta->version.Major, &meta->version.Minor,
                                     &meta->version.Build, &meta->version.Revision );
                        else if (!wcscmp( aname, L"ProcessorArchitecture" ))
                            meta->arch = parse_arch( aval );
                        else if (!wcscmp( aname, L"ResourceId" ))
                            lstrcpynW( meta->resource_id, aval, ARRAY_SIZE(meta->resource_id) );
                    } while (IXmlReader_MoveToNextAttribute( reader ) == S_OK);
                    IXmlReader_MoveToElement( reader );
                }
            }
            else if (!wcscmp( local_name, L"Properties" ) && !is_empty)
            {
                in_props = TRUE;
            }
            else if (in_props)
            {
                lstrcpynW( cur_elem, local_name, ARRAY_SIZE(cur_elem) );
            }
            else if (!wcscmp( local_name, L"Application" ))
            {
                if (IXmlReader_MoveToFirstAttribute( reader ) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName( reader, &aname, NULL );
                        IXmlReader_GetValue( reader, &aval, NULL );
                        if (aname && aval && !wcscmp( aname, L"Id" ))
                            lstrcpynW( cur_app_id, aval, ARRAY_SIZE(cur_app_id) );
                    } while (IXmlReader_MoveToNextAttribute( reader ) == S_OK);
                    IXmlReader_MoveToElement( reader );
                }
            }
            else if (!wcscmp( local_name, L"AppExtension" ))
            {
                WCHAR name[256] = {0}, id[256] = {0}, disp[256] = {0}, desc[512] = {0}, pub_dir[MAX_PATH] = {0};
                if (IXmlReader_MoveToFirstAttribute( reader ) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName( reader, &aname, NULL );
                        IXmlReader_GetValue( reader, &aval, NULL );
                        if (!aname || !aval) continue;
                        if (!wcscmp( aname, L"Name" )) lstrcpynW( name, aval, ARRAY_SIZE(name) );
                        else if (!wcscmp( aname, L"Id" )) lstrcpynW( id, aval, ARRAY_SIZE(id) );
                        else if (!wcscmp( aname, L"DisplayName" )) lstrcpynW( disp, aval, ARRAY_SIZE(disp) );
                        else if (!wcscmp( aname, L"Description" )) lstrcpynW( desc, aval, ARRAY_SIZE(desc) );
                        else if (!wcscmp( aname, L"PublicFolder" )) lstrcpynW( pub_dir, aval, ARRAY_SIZE(pub_dir) );
                    } while (IXmlReader_MoveToNextAttribute( reader ) == S_OK);
                    IXmlReader_MoveToElement( reader );
                }
                if (name[0] && !wcsicmp( name, ext_name ))
                {
                    /* Deduplicate by PackageFamilyName + ExtensionId, keeping the highest PackageVersion. */
                    UINT32 k;
                    BOOL replaced = FALSE;
                    for (k = 0; k < list->count; k++)
                    {
                        struct app_extension *existing = list->items[k];
                        if (!wcsicmp( existing->meta->family_name, meta->family_name ) &&
                            !wcsicmp( existing->id, id ))
                        {
                            if (compare_version( &meta->version, &existing->meta->version ) > 0)
                            {
                                pkg_metadata_release( existing->meta );
                                existing->meta = meta;
                                pkg_metadata_addref( meta );
                                lstrcpynW( existing->display_name, disp, ARRAY_SIZE(existing->display_name) );
                                lstrcpynW( existing->description, desc, ARRAY_SIZE(existing->description) );
                                lstrcpynW( existing->public_folder, pub_dir, ARRAY_SIZE(existing->public_folder) );
                                lstrcpynW( existing->app_id, cur_app_id, ARRAY_SIZE(existing->app_id) );
                            }
                            replaced = TRUE;
                            break;
                        }
                    }
                    if (!replaced)
                    {
                        struct app_extension *ext = calloc( 1, sizeof(*ext) );
                        if (ext)
                        {
                            struct app_extension **new_arr = realloc( list->items, (list->count + 1) * sizeof(*new_arr) );
                            if (new_arr)
                            {
                                ext->vtbl = appext_vtbl;
                                ext->ref = 1;
                                ext->meta = meta;
                                pkg_metadata_addref( meta );
                                lstrcpynW( ext->id, id, ARRAY_SIZE(ext->id) );
                                lstrcpynW( ext->display_name, disp, ARRAY_SIZE(ext->display_name) );
                                lstrcpynW( ext->description, desc, ARRAY_SIZE(ext->description) );
                                lstrcpynW( ext->public_folder, pub_dir, ARRAY_SIZE(ext->public_folder) );
                                lstrcpynW( ext->app_id, cur_app_id, ARRAY_SIZE(ext->app_id) );
                                list->items = new_arr;
                                list->items[list->count++] = ext;
                            }
                            else
                                free( ext );
                        }
                    }
                }
            }
        }
        else if (node_type == XmlNodeType_Text && in_props && cur_elem[0])
        {
            const WCHAR *val = NULL;
            IXmlReader_GetValue( reader, &val, NULL );
            if (val)
            {
                if (!wcscmp( cur_elem, L"DisplayName" ))
                    lstrcpynW( meta->display_name, val, ARRAY_SIZE(meta->display_name) );
                else if (!wcscmp( cur_elem, L"PublisherDisplayName" ))
                    lstrcpynW( meta->publisher_display_name, val, ARRAY_SIZE(meta->publisher_display_name) );
                else if (!wcscmp( cur_elem, L"Description" ))
                    lstrcpynW( meta->description, val, ARRAY_SIZE(meta->description) );
                else if (!wcscmp( cur_elem, L"Framework" ) && !wcsicmp( val, L"true" ))
                    meta->is_framework = TRUE;
            }
        }
        else if (node_type == XmlNodeType_EndElement)
        {
            IXmlReader_GetLocalName( reader, &local_name, NULL );
            if (local_name && !wcscmp( local_name, L"Properties" ))
                in_props = FALSE;
            cur_elem[0] = 0;
        }
    }

    pkg_metadata_release( meta );
    IXmlReader_Release( reader );
    IStream_Release( stream );
    free( buf );
}

static HRESULT find_extensions_for_name( const WCHAR *ext_name, struct appext_list **out_list )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    HMODULE hxml;
    pCreateXmlReader fn_create_reader;
    struct appext_list *list;
    SIZE_T r;

    list = calloc( 1, sizeof(*list) );
    if (!list) return E_OUTOFMEMORY;
    list->vtbl = appext_view_vtbl;
    list->ref = 1;

    hxml = LoadLibraryW( L"xmllite.dll" );
    fn_create_reader = hxml ? (pCreateXmlReader)GetProcAddress( hxml, "CreateXmlReader" ) : NULL;
    if (!fn_create_reader)
    {
        if (hxml) FreeLibrary( hxml );
        *out_list = list;
        return S_OK;
    }

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY key;
        DWORD idx = 0;
        WCHAR kname[256];

        if (RegOpenKeyExW( roots[r], L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages",
                           0, KEY_READ, &key ))
            continue;

        for (;;)
        {
            DWORD klen = ARRAY_SIZE(kname), type, size;
            HKEY sub;
            WCHAR pkg_path[MAX_PATH] = {0}, fam[256] = {0}, pub_id[64] = {0};

            if (RegEnumKeyExW( key, idx++, kname, &klen, NULL, NULL, NULL, NULL )) break;
            if (RegOpenKeyExW( key, kname, 0, KEY_READ, &sub )) continue;

            size = sizeof(pkg_path);
            if (RegQueryValueExW( sub, L"PackagePath", NULL, &type, (BYTE *)pkg_path, &size ) || type != REG_SZ)
            {
                size = sizeof(pkg_path);
                if (RegQueryValueExW( sub, L"PackageRootPath", NULL, &type, (BYTE *)pkg_path, &size ) || type != REG_SZ)
                    pkg_path[0] = 0;
            }
            size = sizeof(fam);
            if (RegQueryValueExW( sub, L"PackageFamilyName", NULL, &type, (BYTE *)fam, &size ) || type != REG_SZ)
                fam[0] = 0;
            size = sizeof(pub_id);
            if (RegQueryValueExW( sub, L"PackagePublisherId", NULL, &type, (BYTE *)pub_id, &size ) || type != REG_SZ)
                pub_id[0] = 0;
            RegCloseKey( sub );

            if (pkg_path[0] && GetFileAttributesW( pkg_path ) != INVALID_FILE_ATTRIBUTES)
                scan_package_manifest( fn_create_reader, ext_name, kname, fam, pub_id, pkg_path, list );
        }
        RegCloseKey( key );
    }

    FreeLibrary( hxml );
    *out_list = list;
    return S_OK;
}

/* --------------------------------------------------------------------- */
/* IAppExtensionCatalog */

struct appext_catalog
{
    const void **vtbl;
    LONG ref;
    WCHAR extension_name[256];
};

static HRESULT WINAPI cat_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct appext_catalog *impl = iface;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAppExtensionCatalog ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI cat_AddRef( void *iface ) { return InterlockedIncrement( &((struct appext_catalog *)iface)->ref ); }
static ULONG WINAPI cat_Release( void *iface )
{
    struct appext_catalog *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cat_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *arr = CoTaskMemAlloc( sizeof(IID) );
    if (!arr) return E_OUTOFMEMORY;
    arr[0] = IID_IAppExtensionCatalog;
    *count = 1;
    *iids = arr;
    return S_OK;
}
static HRESULT WINAPI cat_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.AppExtensions.AppExtensionCatalog", 58, name );
}
static HRESULT WINAPI cat_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }

static HRESULT WINAPI cat_FindAllAsync( void *iface, void **op )
{
    struct appext_catalog *impl = iface;
    struct appext_list *list = NULL;
    HRESULT hr;

    TRACE( "iface %p, ext %s, op %p.\n", iface, debugstr_w( impl->extension_name ), op );

    hr = find_extensions_for_name( impl->extension_name, &list );
    if (FAILED(hr)) return hr;

    hr = create_async_op_obj(
        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.AppExtensions.AppExtension>>",
        (IUnknown *)list, S_OK, op );
    appext_view_Release( list );
    return hr;
}

static HRESULT WINAPI cat_RequestRemovePackageAsync( void *iface, HSTRING full_name, void **op )
{
    return create_async_op_bool( L"Windows.Foundation.IAsyncOperation`1<Boolean>", FALSE, S_OK, op );
}

static HRESULT WINAPI cat_add_Event( void *iface, void *handler, EventRegistrationToken *token )
{
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI cat_remove_Event( void *iface, EventRegistrationToken token )
{
    return S_OK;
}

static const void *cat_vtbl[] =
{
    cat_QueryInterface,
    cat_AddRef,
    cat_Release,
    cat_GetIids,
    cat_GetRuntimeClassName,
    cat_GetTrustLevel,
    cat_FindAllAsync,              /* 6 */
    cat_RequestRemovePackageAsync, /* 7 */
    cat_add_Event,                 /* 8 add_PackageInstalled */
    cat_remove_Event,              /* 9 remove_PackageInstalled */
    cat_add_Event,                 /* 10 add_PackageUpdating */
    cat_remove_Event,              /* 11 remove_PackageUpdating */
    cat_add_Event,                 /* 12 add_PackageUpdated */
    cat_remove_Event,              /* 13 remove_PackageUpdated */
    cat_add_Event,                 /* 14 add_PackageUninstalling */
    cat_remove_Event,              /* 15 remove_PackageUninstalling */
    cat_add_Event,                 /* 16 add_PackageStatusChanged */
    cat_remove_Event,              /* 17 remove_PackageStatusChanged */
};

/* --------------------------------------------------------------------- */
/* AppExtensionCatalog activation factory + IAppExtensionCatalogStatics */

struct catalog_factory
{
    IActivationFactory IActivationFactory_iface;
    const void **statics_vtbl;
    LONG ref;
};

static inline struct catalog_factory *factory_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct catalog_factory, IActivationFactory_iface );
}
static inline struct catalog_factory *factory_from_statics( void *iface )
{
    return CONTAINING_RECORD( iface, struct catalog_factory, statics_vtbl );
}

static HRESULT WINAPI catalog_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct catalog_factory *impl = factory_from_IActivationFactory( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppExtensionCatalogStatics ))
    {
        *out = &impl->statics_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI catalog_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI catalog_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI catalog_factory_GetIids( IActivationFactory *iface, ULONG *c, IID **i )
{
    IID *arr = CoTaskMemAlloc( sizeof(IID) );
    if (!arr) return E_OUTOFMEMORY;
    arr[0] = IID_IAppExtensionCatalogStatics;
    *c = 1;
    *i = arr;
    return S_OK;
}
static HRESULT WINAPI catalog_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.AppExtensions.AppExtensionCatalog", 58, name );
}
static HRESULT WINAPI catalog_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI catalog_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    *instance = NULL;
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl catalog_factory_vtbl =
{
    catalog_factory_QueryInterface,
    catalog_factory_AddRef,
    catalog_factory_Release,
    catalog_factory_GetIids,
    catalog_factory_GetRuntimeClassName,
    catalog_factory_GetTrustLevel,
    catalog_factory_ActivateInstance,
};

static HRESULT WINAPI catalog_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    return catalog_factory_QueryInterface( &factory_from_statics( iface )->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI catalog_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI catalog_statics_Release( void *iface ) { return 1; }
static HRESULT WINAPI catalog_statics_GetIids( void *iface, ULONG *c, IID **i )
{
    return catalog_factory_GetIids( &factory_from_statics( iface )->IActivationFactory_iface, c, i );
}
static HRESULT WINAPI catalog_statics_GetRCN( void *iface, HSTRING *n )
{
    return catalog_factory_GetRuntimeClassName( &factory_from_statics( iface )->IActivationFactory_iface, n );
}
static HRESULT WINAPI catalog_statics_GetTL( void *iface, TrustLevel *l )
{
    return catalog_factory_GetTrustLevel( &factory_from_statics( iface )->IActivationFactory_iface, l );
}

static HRESULT WINAPI catalog_statics_Open( void *iface, HSTRING name, void **out )
{
    const WCHAR *buf = WindowsGetStringRawBuffer( name, NULL );
    struct appext_catalog *cat;

    TRACE( "name %s, out %p.\n", debugstr_hstring( name ), out );

    if (!buf || !buf[0] || !out) return E_INVALIDARG;
    cat = calloc( 1, sizeof(*cat) );
    if (!cat) return E_OUTOFMEMORY;
    cat->vtbl = cat_vtbl;
    cat->ref = 1;
    lstrcpynW( cat->extension_name, buf, ARRAY_SIZE(cat->extension_name) );
    *out = cat;
    return S_OK;
}

static const void *catalog_statics_vtbl[] =
{
    catalog_statics_QueryInterface,
    catalog_statics_AddRef,
    catalog_statics_Release,
    catalog_statics_GetIids,
    catalog_statics_GetRCN,
    catalog_statics_GetTL,
    catalog_statics_Open, /* 6 */
};

static struct catalog_factory catalog_factory_obj =
{
    { &catalog_factory_vtbl },
    catalog_statics_vtbl,
    1,
};

IActivationFactory *app_extension_catalog_factory = &catalog_factory_obj.IActivationFactory_iface;
