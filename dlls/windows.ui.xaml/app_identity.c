/* WinRT Windows.UI.Xaml app identity: AppModel package identity,
 * AppxManifest.xml DisplayName / PRI resource resolution, window icons,
 * app capability probing and the IXamlMetadataProvider face.
 *
 * Copyright (C) 2026 Wine project
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

#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "app_private.h"
#include "uwp_abi_vtbls.h"
#include "weakreference.h"
#include "wine/exception.h"
#include "windows.applicationmodel.activation.h"
#include "appmodel.h"
#include "pri.h"
#include "wincodec.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static HRESULT WINAPI app_meta_GetXamlType( void *iface, TypeName type_name, void **out )
{
    return xaml_metadata_cache_get_type_hstring( type_name.Name, out );
}
static HRESULT WINAPI app_meta_GetXamlTypeByFullName( void *iface, HSTRING name, void **out )
{
    return xaml_metadata_cache_get_type_hstring( name, out );
}
static HRESULT WINAPI app_meta_GetXmlnsDefinitions( void *iface, ULONG *count, void ***out )
{ if (count) *count = 0; if (out) *out = NULL; return S_OK; }

static HRESULT WINAPI app_meta_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_WinUI ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_meta_AddRef( void *iface ) { return 2; }
static ULONG WINAPI app_meta_Release( void *iface ) { return 1; }
static HRESULT WINAPI app_meta_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI app_meta_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Xaml.Markup.IXamlMetadataProvider", 43, name ); }
static HRESULT WINAPI app_meta_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

const void *app_meta_provider_vtbl[] =
{
    app_meta_QueryInterface,
    app_meta_AddRef,
    app_meta_Release,
    app_meta_GetIids,
    app_meta_GetRuntimeClassName,
    app_meta_GetTrustLevel,
    app_meta_GetXamlType,           /* 6: GetXamlType */
    app_meta_GetXamlTypeByFullName, /* 7: GetXamlTypeByFullName */
    app_meta_GetXmlnsDefinitions,   /* 8: GetXmlnsDefinitions */
};

/* AppModel package identity, not process-name sniffing: the deployment record
 * (AppModel\Exes\<image>, AppModel\Packages\<FullName>, seeded per prefix and
 * read by kernelbase's GetCurrentPackage*) is the source of truth. The
 * display name comes from the package manifest's uap:VisualElements
 * DisplayName, with ms-resource: keys resolved from the package
 * resources.pri (the same mrm_pri2 reader twinapi.appcore uses). */

struct xaml_package
{
    BOOL is_packaged;
    WCHAR full_name[512];
    WCHAR family_name[512];
    WCHAR installed_location[MAX_PATH];
    WCHAR display_name[512];
};

static struct xaml_package xaml_package;
static INIT_ONCE xaml_package_once = INIT_ONCE_STATIC_INIT;
static pri_index *xaml_package_pri;

static const HKEY xaml_reg_roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
static const WCHAR xaml_appmodel_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel";

static BOOL xaml_read_reg_str( HKEY root, const WCHAR *subkey, const WCHAR *value, WCHAR *buf, DWORD cch )
{
    WCHAR path[640];
    HKEY hkey;
    DWORD type = 0, size;
    LONG ret;

    if (subkey && subkey[0])
        swprintf( path, ARRAY_SIZE(path), L"%s\\%s", xaml_appmodel_key, subkey );
    else
        lstrcpynW( path, xaml_appmodel_key, ARRAY_SIZE(path) );
    if (RegOpenKeyExW( root, path, 0, KEY_READ, &hkey )) return FALSE;
    size = cch * sizeof(*buf);
    ret = RegQueryValueExW( hkey, value, NULL, &type, (BYTE *)buf, &size );
    RegCloseKey( hkey );
    if (ret != ERROR_SUCCESS || type != REG_SZ) return FALSE;
    buf[min( size / sizeof(*buf), cch - 1 )] = 0;
    return buf[0] != 0;
}

/* InstalledLocation: GetCurrentPackagePath, then Packages\<FullName>\PackagePath
 * (PackageRootPath accepted as a synonym), HKCU before HKLM. May stay empty;
 * callers fall back to the exe directory, which is the package dir for UWP. */
static void xaml_package_resolve_location( struct xaml_package *pkg )
{
    WCHAR subkey[600];
    UINT32 path_len = ARRAY_SIZE( pkg->installed_location );
    SIZE_T i;

    pkg->installed_location[0] = 0;
    if (GetCurrentPackagePath( &path_len, pkg->installed_location ) == ERROR_SUCCESS && pkg->installed_location[0])
        return;
    pkg->installed_location[0] = 0;
    if (!pkg->full_name[0]) return;

    swprintf( subkey, ARRAY_SIZE(subkey), L"Packages\\%s", pkg->full_name );
    for (i = 0; i < ARRAY_SIZE( xaml_reg_roots ); i++)
    {
        static const WCHAR *const values[] = { L"PackagePath", L"PackageRootPath" };
        SIZE_T j;
        for (j = 0; j < ARRAY_SIZE( values ); j++)
            if (xaml_read_reg_str( xaml_reg_roots[i], subkey, values[j],
                                   pkg->installed_location, ARRAY_SIZE( pkg->installed_location ) ))
                return;
    }
}

static void xml_unescape( WCHAR *s )
{
    static const struct { const WCHAR *ent; WCHAR ch; } ents[] =
    {
        { L"&amp;", L'&' }, { L"&lt;", L'<' }, { L"&gt;", L'>' }, { L"&quot;", L'"' }, { L"&apos;", L'\'' },
    };
    SIZE_T i;

    for (i = 0; i < ARRAY_SIZE( ents ); i++)
    {
        WCHAR *p;
        while ((p = wcsstr( s, ents[i].ent )))
        {
            *p = ents[i].ch;
            memmove( p + 1, p + lstrlenW( ents[i].ent ),
                     (lstrlenW( p + lstrlenW( ents[i].ent ) ) + 1) * sizeof(WCHAR) );
        }
    }
}

/* <uap:VisualElements DisplayName="..." ...> attribute from AppxManifest.xml
 * in dir. Returns FALSE when the file or attribute is absent. */
static BOOL xaml_manifest_display_name( const WCHAR *dir, WCHAR *out, DWORD cch )
{
    static const WCHAR manifest_name[] = L"\\AppxManifest.xml";
    WCHAR path[MAX_PATH + 32];
    char *raw;
    WCHAR *xml;
    DWORD size, read, wlen;
    HANDLE file;
    BOOL ok = FALSE;
    UINT cp = CP_UTF8;

    swprintf( path, ARRAY_SIZE(path), L"%.260s%s", dir, manifest_name );
    if ((file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL )) == INVALID_HANDLE_VALUE)
    {
        TRACE( "manifest open %s failed, err %u\n", debugstr_w( path ), (unsigned int)GetLastError() );
        return FALSE;
    }
    size = GetFileSize( file, NULL );
    if (!size || size == INVALID_FILE_SIZE || !(raw = malloc( size + 2 )))
    {
        CloseHandle( file );
        return FALSE;
    }
    ReadFile( file, raw, size, &read, NULL );
    CloseHandle( file );
    raw[read] = 0;
    wlen = MultiByteToWideChar( cp, 0, raw, read, NULL, 0 );
    if (!wlen
            && !(wlen = MultiByteToWideChar( cp = CP_UTF8, MB_USEGLYPHCHARS, raw, read, NULL, 0 ))
            && !(wlen = MultiByteToWideChar( cp = CP_UNIXCP, 0, raw, read, NULL, 0 )))
    {
        free( raw );
        return FALSE;
    }
    if (!(xml = malloc( (wlen + 1) * sizeof(*xml) )))
    {
        free( raw );
        return FALSE;
    }
    MultiByteToWideChar( cp, cp == CP_UTF8 ? MB_USEGLYPHCHARS : 0, raw, read, xml, wlen );
    xml[wlen] = 0;
    free( raw );

    {
        WCHAR *tag = wcsstr( xml, L"uap:VisualElements" );
        WCHAR *end, *attr;
        if (tag && (end = wcschr( tag, L'>' )) && (attr = wcsstr( tag, L"DisplayName=\"" )) && attr < end)
        {
            WCHAR *val = attr + lstrlenW( L"DisplayName=\"" );
            WCHAR *close = wcschr( val, L'"' );
            if (close)
            {
                DWORD len = min( (DWORD)(close - val), cch - 1 );
                memcpy( out, val, len * sizeof(WCHAR) );
                out[len] = 0;
                xml_unescape( out );
                ok = out[0] != 0;
            }
        }
    }
    free( xml );
    return ok;
}

static pri_index *xaml_package_open_pri( struct xaml_package *pkg )
{
    static const WCHAR pri_name[] = L"\\resources.pri";
    WCHAR dir[MAX_PATH], path[MAX_PATH + 32];
    DWORD len;

    if (pkg->installed_location[0])
    {
        swprintf( path, ARRAY_SIZE(path), L"%.260s%s", pkg->installed_location, pri_name );
        if ((xaml_package_pri = pri_load( path ))) return xaml_package_pri;
    }
    len = GetModuleFileNameW( NULL, dir, ARRAY_SIZE( dir ) );
    if (len && len < ARRAY_SIZE( dir ) && wcsrchr( dir, L'\\' ))
    {
        *wcsrchr( dir, L'\\' ) = 0;
        swprintf( path, ARRAY_SIZE(path), L"%.260s%s", dir, pri_name );
        xaml_package_pri = pri_load( path );
    }
    return xaml_package_pri;
}

/* DisplayName="ms-resource:AppName" resolves through the package resource map:
 * bare key, then Resources\ and Files\ scopes (same order as twinapi's
 * ResourceLoader). Leaves display_name as the bare key on a miss. */
static void xaml_package_resolve_ms_resource( struct xaml_package *pkg )
{
    static const WCHAR ms_resource[] = L"ms-resource:";
    static const WCHAR *const scopes[] = { L"", L"Resources\\", L"Files\\" };
    WCHAR key[512], full[600], namebuf[512];
    WCHAR *val = NULL;
    SIZE_T i;

    if (wcsncmp( pkg->display_name, ms_resource, ARRAY_SIZE( ms_resource ) - 1 )) return;

    lstrcpynW( key, pkg->display_name + ARRAY_SIZE( ms_resource ) - 1, ARRAY_SIZE( key ) );
    if (key[0] == L'/' && key[1] == L'/') memmove( key, key + 2, (wcslen( key + 2 ) + 1) * sizeof(WCHAR) );
    if (key[0] == L'/') memmove( key, key + 1, (wcslen( key + 1 ) + 1) * sizeof(WCHAR) );
    for (i = 0; key[i]; i++) if (key[i] == L'/') key[i] = L'\\';

    if (xaml_package_open_pri( pkg ))
    {
        for (i = 0; i < ARRAY_SIZE( scopes ); i++)
        {
            swprintf( full, ARRAY_SIZE( full ), L"%s%s", scopes[i], key );
            if (pri_lookup( xaml_package_pri, full, NULL, 0, namebuf, ARRAY_SIZE( namebuf ),
                            &val, NULL, NULL ))
            {
                lstrcpynW( pkg->display_name, val, ARRAY_SIZE( pkg->display_name ) );
                pri_free_result( val, NULL );
                return;
            }
        }
    }
    lstrcpynW( pkg->display_name, key, ARRAY_SIZE( pkg->display_name ) );
}

static BOOL WINAPI xaml_package_init( INIT_ONCE *once, void *param, void **context )
{
    struct xaml_package *pkg = &xaml_package;
    WCHAR subkey[600];
    UINT32 len;
    SIZE_T i;

    pkg->full_name[0] = pkg->family_name[0] = 0;
    len = ARRAY_SIZE( pkg->full_name );
    pkg->is_packaged = GetCurrentPackageFullName( &len, pkg->full_name ) == ERROR_SUCCESS;
    if (pkg->is_packaged)
    {
        len = ARRAY_SIZE( pkg->family_name );
        GetCurrentPackageFamilyName( &len, pkg->family_name );
    }

    xaml_package_resolve_location( pkg );

    /* manifest in the installed location, then the exe dir (the package dir
     * for a deployed UWP app): the authoritative DisplayName. */
    if (pkg->installed_location[0] && xaml_manifest_display_name( pkg->installed_location,
                                                                  pkg->display_name, ARRAY_SIZE( pkg->display_name ) ));
    else
    {
        WCHAR dir[MAX_PATH];
        DWORD len2 = GetModuleFileNameW( NULL, dir, ARRAY_SIZE( dir ) );
        if (len2 && len2 < ARRAY_SIZE( dir ) && wcsrchr( dir, L'\\' ))
        {
            *wcsrchr( dir, L'\\' ) = 0;
            xaml_manifest_display_name( dir, pkg->display_name, ARRAY_SIZE( pkg->display_name ) );
        }
    }

    /* a DisplayName seeded on the deployment record overrides, and covers
     * packages shipping no manifest attribute. */
    if (pkg->full_name[0])
    {
        swprintf( subkey, ARRAY_SIZE(subkey), L"Packages\\%s", pkg->full_name );
        for (i = 0; i < ARRAY_SIZE( xaml_reg_roots ); i++)
            if (xaml_read_reg_str( xaml_reg_roots[i], subkey, L"DisplayName",
                                   pkg->display_name, ARRAY_SIZE( pkg->display_name ) ))
                break;
    }

    TRACE( "package init: manifest/reg display_name %s, ms-resource resolving\n", debugstr_w( pkg->display_name ) );
    xaml_package_resolve_ms_resource( pkg );
    TRACE( "package init: resolved display_name %s\n", debugstr_w( pkg->display_name ) );

    /* Publish the resolved DisplayName as the package short name on the seeded
     * AppModel identity: windows.ui names the CoreWindow window class with it,
     * and winex11 publishes that class name as WM_CLASS (taskbar identity). */
    if (pkg->full_name[0] && pkg->display_name[0])
    {
        WCHAR sub[640];
        HKEY hkey;
        swprintf( sub, ARRAY_SIZE(sub), L"%s\\Packages\\%s", xaml_appmodel_key, pkg->full_name );
        if (RegCreateKeyExW( HKEY_CURRENT_USER, sub, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL ) == ERROR_SUCCESS)
        {
            RegSetValueExW( hkey, L"ApplicationShortName", 0, REG_SZ, (const BYTE *)pkg->display_name,
                            (lstrlenW( pkg->display_name ) + 1) * sizeof(WCHAR) );
            RegCloseKey( hkey );
        }
    }
    return TRUE;
}

const WCHAR *xaml_package_display_name( void )
{
    InitOnceExecuteOnce( &xaml_package_once, xaml_package_init, NULL, NULL );
    return xaml_package.display_name[0] ? xaml_package.display_name : NULL;
}

static const WCHAR *xaml_package_short_name( void );

void xaml_package_warm( void )
{
    const WCHAR *short_name = xaml_package_short_name();
    TRACE( "package warm: short name %s\n", debugstr_w( short_name ) );
}

/* Short app identity for window lookups: the package DisplayName, else the
 * current AUMID app id (the part after '!'), else nothing. Matches what
 * windows.ui/corewindow.c picks as the CoreWindow class name. */
static const WCHAR *xaml_package_short_name( void )
{
    static WCHAR aumid_app[128];
    const WCHAR *name = xaml_package_display_name();
    WCHAR buf[256];
    UINT32 len = ARRAY_SIZE(buf);

    if (name) return name;
    if (aumid_app[0]) return aumid_app;
    if (GetCurrentApplicationUserModelId( &len, buf ) == ERROR_SUCCESS && buf[0])
    {
        WCHAR *p = wcschr( buf, L'!' );
        lstrcpynW( aumid_app, p ? p + 1 : buf, ARRAY_SIZE(aumid_app) );
        if (aumid_app[0]) return aumid_app;
    }
    return NULL;
}

HWND xaml_find_core_window( void )
{
    const WCHAR *short_name = xaml_package_short_name();
    HWND hw = NULL;
    if (short_name) hw = FindWindowW( short_name, NULL );
    if (!hw) hw = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
    if (!hw) hw = FindWindowW( L"App", NULL );
    return hw;
}

static BOOL xaml_package_logo_path( WCHAR *out_path, DWORD cch )
{
    typedef HRESULT (WINAPI *p_AppxGetCurrentPackageSquare44x44Logo)( WCHAR *, DWORD );
    p_AppxGetCurrentPackageSquare44x44Logo fn;
    HMODULE mod;
    WCHAR key[640];
    SIZE_T i;

    if (!out_path || !cch) return FALSE;
    out_path[0] = 0;

    InitOnceExecuteOnce( &xaml_package_once, xaml_package_init, NULL, NULL );
    if (xaml_package.full_name[0])
    {
        swprintf( key, ARRAY_SIZE(key), L"Software\\Classes\\AppModel\\Repository\\Packages\\%s\\VisualElements",
                  xaml_package.full_name );
        for (i = 0; i < ARRAY_SIZE( xaml_reg_roots ); i++)
        {
            HKEY hkey;
            DWORD type = 0, size = cch * sizeof(WCHAR), attr;
            if (RegOpenKeyExW( xaml_reg_roots[i], key, 0, KEY_READ, &hkey ) == ERROR_SUCCESS)
            {
                LONG ret = RegQueryValueExW( hkey, L"Square44x44LogoPath", NULL, &type, (BYTE *)out_path, &size );
                RegCloseKey( hkey );
                if (ret == ERROR_SUCCESS && type == REG_SZ && out_path[0] &&
                    (attr = GetFileAttributesW( out_path )) != INVALID_FILE_ATTRIBUTES &&
                    !(attr & FILE_ATTRIBUTE_DIRECTORY))
                    return TRUE;
                out_path[0] = 0;
            }
        }
    }

    mod = GetModuleHandleW( L"appxdeploymentclient.dll" );
    if (!mod) mod = LoadLibraryW( L"appxdeploymentclient.dll" );
    if (mod && (fn = (p_AppxGetCurrentPackageSquare44x44Logo)GetProcAddress( mod, "AppxGetCurrentPackageSquare44x44Logo" )))
    {
        if (SUCCEEDED( fn( out_path, cch ) ) && out_path[0])
            return TRUE;
        out_path[0] = 0;
    }
    return FALSE;
}

static HICON xaml_wic_create_icon_from_frame( IWICImagingFactory *wic, IWICBitmapSource *src, UINT w, UINT h )
{
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *conv = NULL;
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC hdc;
    HBITMAP color = NULL, mask = NULL;
    HICON icon = NULL;
    HRESULT hr;

    hr = IWICImagingFactory_CreateBitmapScaler( wic, &scaler );
    if (SUCCEEDED( hr ))
        hr = IWICBitmapScaler_Initialize( scaler, src, w, h, WICBitmapInterpolationModeFant );
    if (SUCCEEDED( hr ))
        hr = IWICImagingFactory_CreateFormatConverter( wic, &conv );
    if (SUCCEEDED( hr ))
        hr = IWICFormatConverter_Initialize( conv, (IWICBitmapSource *)scaler, &GUID_WICPixelFormat32bppBGRA_local,
                                             WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom );
    if (SUCCEEDED( hr ))
    {
        memset( &bmi, 0, sizeof(bmi) );
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)w;
        bmi.bmiHeader.biHeight = -(LONG)h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        hdc = GetDC( NULL );
        color = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0 );
        ReleaseDC( NULL, hdc );
        if (color && bits &&
            SUCCEEDED( IWICFormatConverter_CopyPixels( conv, NULL, w * 4, w * h * 4, bits ) ))
        {
            mask = CreateBitmap( w, h, 1, 1, NULL );
            if (mask)
            {
                ICONINFO ii = {0};
                ii.fIcon = TRUE;
                ii.hbmMask = mask;
                ii.hbmColor = color;
                icon = CreateIconIndirect( &ii );
                DeleteObject( mask );
            }
        }
        if (color) DeleteObject( color );
    }

    if (conv) IWICFormatConverter_Release( conv );
    if (scaler) IWICBitmapScaler_Release( scaler );
    return icon;
}

static void xaml_load_package_icons( HICON *out_big, HICON *out_small )
{
    WCHAR logo_path[MAX_PATH];
    IWICImagingFactory *wic = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    BOOL com_init = FALSE;
    HRESULT hr;

    *out_big = NULL;
    *out_small = NULL;
    if (!xaml_package_logo_path( logo_path, ARRAY_SIZE( logo_path ) )) return;

    hr = CoCreateInstance( &CLSID_WICImagingFactory_local, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IWICImagingFactory_local, (void **)&wic );
    if (hr == CO_E_NOTINITIALIZED && SUCCEEDED( CoInitializeEx( NULL, COINIT_APARTMENTTHREADED ) ))
    {
        com_init = TRUE;
        hr = CoCreateInstance( &CLSID_WICImagingFactory_local, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IWICImagingFactory_local, (void **)&wic );
    }
    if (SUCCEEDED( hr ) && wic)
    {
        if (SUCCEEDED( IWICImagingFactory_CreateDecoderFromFilename( wic, logo_path, NULL, GENERIC_READ,
                                                                     WICDecodeMetadataCacheOnLoad, &dec ) ) && dec)
        {
            if (SUCCEEDED( IWICBitmapDecoder_GetFrame( dec, 0, &frame ) ) && frame)
            {
                *out_big = xaml_wic_create_icon_from_frame( wic, (IWICBitmapSource *)frame, 48, 48 );
                *out_small = xaml_wic_create_icon_from_frame( wic, (IWICBitmapSource *)frame, 16, 16 );
                TRACE( "loaded package icon from %s -> big=%p small=%p\n",
                       debugstr_w( logo_path ), *out_big, *out_small );
                IWICBitmapFrameDecode_Release( frame );
            }
            IWICBitmapDecoder_Release( dec );
        }
        IWICImagingFactory_Release( wic );
    }
    if (com_init) CoUninitialize();
}

void xaml_apply_window_icon( HWND top )
{
    static HICON big_icon, small_icon;
    static BOOL icons_init;
    static HWND last_icon_hwnd;

    if (!top) return;
    if (!icons_init)
    {
        xaml_load_package_icons( &big_icon, &small_icon );
        if (!big_icon)
            big_icon = LoadImageW( 0, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 32, 32, LR_SHARED );
        if (!small_icon)
            small_icon = LoadImageW( 0, (LPCWSTR)IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_SHARED );
        icons_init = TRUE;
    }
    if (top != last_icon_hwnd)
    {
        last_icon_hwnd = top;
        if (big_icon)
        {
            SetClassLongPtrW( top, GCLP_HICON, (LONG_PTR)big_icon );
            SendMessageW( top, WM_SETICON, ICON_BIG, (LPARAM)big_icon );
        }
        if (small_icon)
        {
            SetClassLongPtrW( top, GCLP_HICONSM, (LONG_PTR)small_icon );
            SendMessageW( top, WM_SETICON, ICON_SMALL, (LPARAM)small_icon );
        }
    }
}

static struct xaml_app_caps g_caps;
static BOOL g_caps_ready;

/* Ask the application's own IXamlMetadataProvider (slot 7 GetXamlTypeByFullName,
 * shared layout of the UWP and WinUI providers) whether a type is registered. */
static BOOL caps_provider_has( void *provider, const WCHAR *full_name )
{
    const struct abi_xaml_metadata_provider *mp = (const struct abi_xaml_metadata_provider *)provider;
    HRESULT (WINAPI *get_by_name)(void *, HSTRING, void **) = mp->lpVtbl->GetXamlTypeByFullName;
    HRESULT hr = E_FAIL;
    HSTRING h = NULL;
    void *xt = NULL;

    if (!get_by_name || !full_name || !full_name[0]) return FALSE;
    if (FAILED(WindowsCreateString( full_name, lstrlenW( full_name ), &h )) || !h) return FALSE;
    __TRY
    {
        hr = get_by_name( provider, h, &xt );
    }
    __EXCEPT_ALL
    {
        hr = E_FAIL;
    }
    __ENDTRY;
    WindowsDeleteString( h );
    if (SUCCEEDED( hr ) && xt)
    {
        IInspectable_Release( (IInspectable *)xt );
        return TRUE;
    }
    return FALSE;
}

/* Namespace of the running App class: "CalculatorApp.App" -> "CalculatorApp". */
static void caps_app_namespace( IInspectable *outer, WCHAR *ns, DWORD size )
{
    HSTRING h = NULL;
    ns[0] = 0;
    __TRY
    {
        if (SUCCEEDED( IInspectable_GetRuntimeClassName( outer, &h ) ) && h)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( h, NULL );
            if (raw)
            {
                WCHAR *dot;
                lstrcpynW( ns, raw, (int)size );
                dot = wcsrchr( ns, L'.' );
                if (dot) *dot = 0;
            }
        }
    }
    __EXCEPT_ALL
    {
        ns[0] = 0;
    }
    __ENDTRY;
    if (h) WindowsDeleteString( h );
}

/* Probe ns + each suffix against the app's provider; on a hit store the full
 * registered type name in out. */
static BOOL caps_find_type( void *provider, const WCHAR *ns, const WCHAR *const *suffixes,
                            SIZE_T count, WCHAR *out, DWORD out_chars )
{
    SIZE_T i;
    WCHAR full[160];

    for (i = 0; i < count; i++)
    {
        full[0] = 0;
        if (ns && ns[0]) lstrcpynW( full, ns, ARRAY_SIZE( full ) );
        lstrcpynW( full + lstrlenW( full ), suffixes[i], (int)(ARRAY_SIZE( full ) - lstrlenW( full )) );
        if (caps_provider_has( provider, full ))
        {
            lstrcpynW( out, full, (int)out_chars );
            return TRUE;
        }
    }
    return FALSE;
}

/* Probe the running application's COM surface: which IXamlMetadataProvider face
 * it answers (UWP vs WinUI 3), and which XAML types it has registered. */
static BOOL caps_probe( IInspectable *outer )
{
    static const WCHAR *page_suffixes[] =
        { L".Controls.Calculator", L".Calculator", L".StoreManifestPage" };
    static const WCHAR *vm_suffixes[] =
        { L".ViewModel.StandardCalculatorViewModel", L".StandardCalculatorViewModel" };
    static const WCHAR *design_suffixes[] =
        { L".Design.FontScaleControlPanel" };
    WCHAR ns[80] = {0};
    void *provider = NULL;

    if (SUCCEEDED( IInspectable_QueryInterface( outer, &IID_IXamlMetadataProvider_WinUI, &provider ) ) && provider)
        g_caps.winui3 = TRUE;
    else if (SUCCEEDED( IInspectable_QueryInterface( outer, &IID_IXamlMetadataProvider_UWP, &provider ) ) && provider)
        g_caps.winui3 = FALSE;
    else
        return FALSE; /* app object not metadata-ready yet: probe again later */

    caps_app_namespace( outer, ns, ARRAY_SIZE( ns ) );
    lstrcpynW( g_caps.app_ns, ns, ARRAY_SIZE( g_caps.app_ns ) );

    if (!g_caps.winui3)
    {
        /* CoreApplication-driven UWP app: a registered root page enables the seed tree. */
        WCHAR full[160] = {0};
        if (caps_find_type( provider, ns, page_suffixes, ARRAY_SIZE( page_suffixes ),
                            full, ARRAY_SIZE( full ) ))
        {
            const WCHAR *dot = wcsrchr( full, L'.' );
            lstrcpynW( g_caps.seed_page, dot ? dot + 1 : full, ARRAY_SIZE( g_caps.seed_page ) );
        }
        g_caps.coreapp_seed = !!g_caps.seed_page[0];
    }

    /* A StandardCalculatorViewModel-style VM registered in the app's provider
     * drives the display sync/teardown behaviour for any app exposing one. */
    if (caps_find_type( provider, ns, vm_suffixes, ARRAY_SIZE( vm_suffixes ),
                        g_caps.vm_type, ARRAY_SIZE( g_caps.vm_type ) ))
    {
        g_caps.teardown_vm = TRUE;
        g_caps.display_sync = TRUE;
    }

    /* Design-time panel (e.g. FontScaleControlPanel) registered in the provider. */
    caps_find_type( provider, ns, design_suffixes, ARRAY_SIZE( design_suffixes ),
                    g_caps.design_component, ARRAY_SIZE( g_caps.design_component ) );

    IInspectable_Release( (IInspectable *)provider );
    return TRUE;
}

const struct xaml_app_caps *xaml_get_app_caps( void )
{
    static BOOL probing;
    if (!g_caps_ready && !probing && current_application && current_application->outer)
    {
        probing = TRUE;
        if (caps_probe( (IInspectable *)current_application->outer ))
            g_caps_ready = TRUE;
        probing = FALSE;
    }
    return &g_caps;
}

void *uwp_get_app_outer( void )
{
    return current_application ? current_application->outer : NULL;
}
