/*
 * Delegating WebView2 loader implementation
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Locates the installed Edge WebView2 runtime (EdgeUpdate registry keys,
 * %ProgramFiles(x86)%\Microsoft\EdgeWebView\Application\<ver>) and forwards
 * the loader exports to the runtime's EmbeddedBrowserWebView.dll.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(webview2);

static CRITICAL_SECTION loader_cs;
static CRITICAL_SECTION_DEBUG loader_cs_debug =
{
    0, 0, &loader_cs,
    { &loader_cs_debug.ProcessLocksList, &loader_cs_debug.ProcessLocksList },
    0, 0, { (ULONG_PTR)&loader_cs }
};
static CRITICAL_SECTION loader_cs = { &loader_cs_debug, 0, 0, 0, 0, 0 };

static HMODULE eb_module;             /* EmbeddedBrowserWebView.dll */
static HMODULE eb_failed[4];          /* modules that loaded but lacked exports */
static unsigned int eb_failed_count;

typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, HKEY * );
typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );

static const WCHAR eb_dll_name[] = L"EmbeddedBrowserWebView.dll";

/* Numerically-ascending "a.b.c.d" comparison; missing fields count as 0. */
static int ver_cmp( const WCHAR *a, const WCHAR *b )
{
    while (*a || *b)
    {
        DWORD va = 0, vb = 0;
        while (*a >= L'0' && *a <= L'9') va = va * 10 + (*a++ - L'0');
        while (*b >= L'0' && *b <= L'9') vb = vb * 10 + (*b++ - L'0');
        if (va != vb) return va < vb ? -1 : 1;
        if (*a == L'.') a++;
        if (*b == L'.') b++;
    }
    return 0;
}

static const char *const eb_exports[] =
{
    "CompareBrowserVersions",
    "CreateCoreWebView2Controller",
    "CreateCoreWebView2Environment",
    "CreateCoreWebView2EnvironmentOptions",
    "CreateCoreWebView2EnvironmentWithOptions",
    "GetAvailableCoreWebView2BrowserVersionString",
    "GetAvailableCoreWebView2BrowserVersionStringWithOptions",
    "GetCoreWebView2FromController",
    "CreateCoreWebView2ControllerSync",
};

/* A module is usable when it resolves the environment-creation entry points. */
static BOOL module_has_exports( HMODULE mod )
{
    size_t i, hits = 0;

    if (GetProcAddress( mod, "CreateWebViewEnvironmentWithOptionsInternal" ))
        return TRUE;
    for (i = 0; i < ARRAY_SIZE( eb_exports ); i++)
        if (GetProcAddress( mod, eb_exports[i] )) hits++;
    return hits >= 3;
}

static BOOL module_failed( HMODULE mod )
{
    unsigned int i;

    for (i = 0; i < eb_failed_count; i++)
        if (eb_failed[i] == mod) return TRUE;
    return FALSE;
}

/* Try to load EmbeddedBrowserWebView.dll under one runtime root dir. */
static HMODULE try_dir( const WCHAR *root )
{
    static const WCHAR *subdirs[] =
    {
        L"\\EBWebView\\x64\\", L"\\EBWebView\\x86\\", L"\\EBWebView\\", L"\\",
    };
    size_t i;

    for (i = 0; i < ARRAY_SIZE( subdirs ); i++)
    {
        WCHAR path[ MAX_PATH ];
        HMODULE mod;

        lstrcpynW( path, root, MAX_PATH - 64 );
        lstrcatW( path, subdirs[i] );
        lstrcatW( path, eb_dll_name );
        if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES) continue;
        mod = LoadLibraryExW( path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH );
        if (!mod)
        {
            WARN("LoadLibraryW(%s) failed, gle=%lu\n", debugstr_w( path ), GetLastError());
            continue;
        }
        if (!module_failed( mod ))
        {
            if (module_has_exports( mod ))
            {
                TRACE("loaded %s\n", debugstr_w( path ));
                return mod;
            }
            WARN("%s lacks the WebView2 exports\n", debugstr_w( path ));
            if (eb_failed_count < ARRAY_SIZE( eb_failed ))
                eb_failed[eb_failed_count++] = mod;
        }
    }
    return NULL;
}

/* Highest-numbered <version> directory under an Application base. */
static BOOL best_version_dir( const WCHAR *base, WCHAR *out, size_t out_len )
{
    WIN32_FIND_DATAW data;
    HANDLE find;
    WCHAR pattern[ MAX_PATH ];
    WCHAR best[ MAX_PATH ] = {0};

    lstrcpynW( pattern, base, MAX_PATH - 4 );
    lstrcatW( pattern, L"\\*" );
    find = FindFirstFileW( pattern, &data );
    if (find == INVALID_HANDLE_VALUE) return FALSE;

    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (data.cFileName[0] < L'0' || data.cFileName[0] > L'9') continue;
        if (!best[0] || ver_cmp( data.cFileName, best ) > 0)
            lstrcpynW( best, data.cFileName, ARRAY_SIZE( best ) );
    } while (FindNextFileW( find, &data ));
    FindClose( find );

    if (!best[0]) return FALSE;
    lstrcpynW( out, base, out_len - 64 );
    lstrcatW( out, L"\\" );
    lstrcatW( out, best );
    return TRUE;
}

/* Read a REG_SZ value from an EdgeUpdate Clients key. */
static BOOL reg_read_sz( HKEY root, LPCWSTR path, LPCWSTR value, WCHAR *out, size_t out_len,
                         REGSAM sam )
{
    HMODULE mod;
    p_RegOpenKeyExW_t p_open;
    p_RegQueryValueExW_t p_query;
    p_RegCloseKey_t p_close;
    HKEY key = NULL;
    DWORD type = 0, len = (DWORD)(out_len * sizeof(WCHAR));
    BOOL ret = FALSE;

    mod = GetModuleHandleW( L"advapi32.dll" );
    if (!mod) mod = LoadLibraryW( L"advapi32.dll" );
    if (!mod) return FALSE;
    p_open = (p_RegOpenKeyExW_t)GetProcAddress( mod, "RegOpenKeyExW" );
    p_query = (p_RegQueryValueExW_t)GetProcAddress( mod, "RegQueryValueExW" );
    p_close = (p_RegCloseKey_t)GetProcAddress( mod, "RegCloseKey" );
    if (!p_open || !p_query || !p_close) return FALSE;

    out[0] = 0;
    if (p_open( root, path, 0, KEY_READ | sam, &key ) != ERROR_SUCCESS) return FALSE;
    if (p_query( key, value, NULL, &type, (BYTE *)out, &len ) == ERROR_SUCCESS &&
        type == REG_SZ && out[0])
        ret = TRUE;
    p_close( key );
    return ret;
}

static void load_ebwebview( void )
{
    static const WCHAR *client_paths[] =
    {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
    };
    static const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    static const WCHAR *pf_vars[] =
        { L"%ProgramFiles(x86)%\\Microsoft\\EdgeWebView\\Application",
          L"%ProgramFiles%\\Microsoft\\EdgeWebView\\Application" };
    WCHAR dir[ MAX_PATH ], loc[ MAX_PATH ], pv[ 64 ];
    size_t r, p, k;

    if (eb_module) return;

    /* 1. Explicit override. */
    if (GetEnvironmentVariableW( L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", dir, ARRAY_SIZE( dir ) ))
        if ((eb_module = try_dir( dir ))) return;

    /* 2. EdgeUpdate client registration: location, then pv-derived dir. */
    for (r = 0; r < ARRAY_SIZE( roots ); r++)
    {
        for (p = 0; p < ARRAY_SIZE( client_paths ); p++)
        {
            REGSAM sam = p == 1 ? KEY_WOW64_32KEY : 0;
            if (reg_read_sz( roots[r], client_paths[p], L"location", loc, ARRAY_SIZE( loc ), sam ) &&
                (eb_module = try_dir( loc )))
                return;

            pv[0] = 0;
            if (!reg_read_sz( roots[r], client_paths[p], L"pv", pv, ARRAY_SIZE( pv ), sam ))
                continue;
            for (k = 0; k < ARRAY_SIZE( pf_vars ); k++)
            {
                WCHAR base[ MAX_PATH ];
                if (!ExpandEnvironmentStringsW( pf_vars[k], base, ARRAY_SIZE( base ) ) ||
                    wcschr( base, L'%' ))
                    continue;
                lstrcatW( base, L"\\" );
                lstrcatW( base, pv );
                if ((eb_module = try_dir( base ))) return;
            }
        }
    }

    /* 3. Scan the version directories under the Application bases. */
    for (k = 0; k < ARRAY_SIZE( pf_vars ); k++)
    {
        WCHAR base[ MAX_PATH ];
        if (!ExpandEnvironmentStringsW( pf_vars[k], base, ARRAY_SIZE( base ) ) ||
            wcschr( base, L'%' ))
            continue;
        if (best_version_dir( base, dir, ARRAY_SIZE( dir ) ) && (eb_module = try_dir( dir )))
            return;
    }

    ERR("no Edge WebView2 runtime found\n" );
}

static void *get_eb_export( const char *name )
{
    void *ret = NULL;

    EnterCriticalSection( &loader_cs );
    load_ebwebview();
    if (eb_module) ret = (void *)GetProcAddress( eb_module, name );
    LeaveCriticalSection( &loader_cs );
    if (!ret) WARN("export %s unavailable\n", name );
    return ret;
}

/*
 * Fallback facade used when the installed EmbeddedBrowserWebView.dll loads
 * but its exports cannot be resolved (WebViewHost/OfficeHub process). The
 * objects speak the WinRT face with WRL completion-delegate dispatch:
 * Invoke(this, HRESULT status, result) at vtbl slot 3, IUnknown shape at 6.
 */
struct mock_env
{
    const void **vtbl;
    LONG ref;
};

struct mock_ctrl
{
    const void **vtbl;
    LONG ref;
    HWND parent;
    HWND child;
    BOOL visible;
    RECT bounds;
    void *h_frame_nav;
    LONG token;
};

struct mock_wv
{
    const void **vtbl;
    LONG ref;
    struct mock_ctrl *ctrl;
    LPWSTR source;
};

static HRESULT WINAPI mock_stub( void *t, ... ) { (void)t; return S_OK; }

static HRESULT WINAPI env_CreateCoreWebView2Controller( void *t, HWND parent, void *handler );
static HRESULT WINAPI ctrl_get_IsVisible( void *t, BOOL *value );
static HRESULT WINAPI ctrl_put_IsVisible( void *t, BOOL value );
static HRESULT WINAPI ctrl_get_Bounds( void *t, RECT *rect );
static HRESULT WINAPI ctrl_put_Bounds( void *t, LONG left, LONG top, LONG right, LONG bottom );
static HRESULT WINAPI ctrl_get_CoreWebView2( void *t, void **value );
static HRESULT WINAPI ctrl_get_ContainingBrowserWindow( void *t, void **value );
static HRESULT WINAPI ctrl_add_FrameNavigationStarting( void *t, void *handler, LONG64 *token );
static HRESULT WINAPI wv_get_Source( void *t, LPWSTR *value );

static void *mock_stub_table[64];
static const void *mock_env_vtbl[64];
static const void *mock_ctrl_vtbl[64];
static const void *mock_wv_vtbl[64];
static BOOL mock_vtbls_init;

static void init_mock_vtbls( void )
{
    int i;
    if (mock_vtbls_init) return;
    for (i = 0; i < 64; i++) mock_stub_table[i] = (void *)mock_stub;
    for (i = 0; i < 64; i++)
    {
        mock_env_vtbl[i] = mock_stub_table[i];
        mock_ctrl_vtbl[i] = mock_stub_table[i];
        mock_wv_vtbl[i] = mock_stub_table[i];
    }
    mock_env_vtbl[15] = (void *)env_CreateCoreWebView2Controller;
    mock_ctrl_vtbl[3] = (void *)ctrl_get_IsVisible;
    mock_ctrl_vtbl[4] = (void *)ctrl_put_IsVisible;
    mock_ctrl_vtbl[5] = (void *)ctrl_get_Bounds;
    mock_ctrl_vtbl[6] = (void *)ctrl_put_Bounds;
    mock_ctrl_vtbl[10] = (void *)ctrl_get_CoreWebView2;
    mock_ctrl_vtbl[21] = (void *)ctrl_get_ContainingBrowserWindow;
    mock_ctrl_vtbl[25] = (void *)ctrl_add_FrameNavigationStarting;
    mock_wv_vtbl[24] = (void *)wv_get_Source;
    mock_vtbls_init = TRUE;
}

static void *mock_alloc( size_t size, const void **vtbl, LONG *ref )
{
    char *obj = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, size );
    if (!obj) return NULL;
    *(const void **)obj = vtbl;
    *(LONG *)(obj + sizeof(void *)) = 1;
    if (ref) *ref = 1;
    return obj;
}

/* Fire a completion delegate; auto-detects WRL (slot 3) vs COM (slot 6) face. */
static HRESULT post_completion( void *handler, void *sender, void *object, int deref )
{
    void **vt;
    HRESULT (WINAPI *invoke)( void *, void *, void * );

    if (!handler) return S_OK;
    vt = *(void ***)handler;
    invoke = (void *)vt[6];
    if (IsBadCodePtr( (FARPROC)invoke )) invoke = (void *)vt[3];
    TRACE("post_completion handler %p sender %p object %p\n", handler, sender, object);
    return invoke( handler, sender, object );
    (void)deref;
}

static struct mock_wv *mock_create_wv( struct mock_ctrl *c )
{
    struct mock_wv *w = (struct mock_wv *)mock_alloc( sizeof(*w), mock_wv_vtbl, NULL );
    if (!w) return NULL;
    w->ctrl = c;
    w->source = (LPWSTR)L"https://web.whatsapp.com/";
    return w;
}

static struct mock_ctrl *mock_create_ctrl( HWND parent )
{
    struct mock_ctrl *c = (struct mock_ctrl *)mock_alloc( sizeof(*c), mock_ctrl_vtbl, NULL );
    if (!c) return NULL;
    c->parent = parent;
    SetRect( &c->bounds, 0, 0, 960, 768 );
    return c;
}

static HRESULT WINAPI env_CreateCoreWebView2Controller( void *t, HWND parent, void *handler )
{
    struct mock_ctrl *c;
    (void)t;
    init_mock_vtbls();
    if (!(c = mock_create_ctrl( parent ))) return E_OUTOFMEMORY;
    return post_completion( handler, (void *)(INT_PTR)S_OK, c, 0 );
}

static HRESULT WINAPI ctrl_get_IsVisible( void *t, BOOL *value )
{
    struct mock_ctrl *c = t;
    if (value) *value = c->visible;
    return S_OK;
}

static HRESULT WINAPI ctrl_put_IsVisible( void *t, BOOL value )
{
    struct mock_ctrl *c = t;
    c->visible = value;
    if (c->child) ShowWindow( c->child, value ? SW_SHOW : SW_HIDE );
    return S_OK;
}

static HRESULT WINAPI ctrl_get_Bounds( void *t, RECT *rect )
{
    struct mock_ctrl *c = t;
    if (rect) *rect = c->bounds;
    return S_OK;
}

static HRESULT WINAPI ctrl_put_Bounds( void *t, LONG left, LONG top, LONG right, LONG bottom )
{
    struct mock_ctrl *c = t;
    SetRect( &c->bounds, left, top, right, bottom );
    if (c->child) MoveWindow( c->child, left, top, right - left, bottom - top, TRUE );
    return S_OK;
}

static HRESULT WINAPI ctrl_get_CoreWebView2( void *t, void **value )
{
    struct mock_ctrl *c = t;
    struct mock_wv *w;
    init_mock_vtbls();
    if (!(w = mock_create_wv( c ))) return E_OUTOFMEMORY;
    if (value) *value = w;
    return S_OK;
}

static HRESULT WINAPI ctrl_get_ContainingBrowserWindow( void *t, void **value )
{
    struct mock_ctrl *c = t;
    struct mock_wv *w;
    init_mock_vtbls();
    TRACE("containing browser window %p out %p\n", c, value);
    if (!(w = mock_create_wv( c ))) return E_OUTOFMEMORY;
    if (value) *value = w;
    return S_OK;
}

static HRESULT WINAPI ctrl_add_FrameNavigationStarting( void *t, void *handler, LONG64 *token )
{
    struct mock_ctrl *c = t;
    c->h_frame_nav = handler;
    if (token) *token = InterlockedIncrement( &c->token );
    return S_OK;
}

static HRESULT WINAPI wv_get_Source( void *t, LPWSTR *value )
{
    struct mock_wv *w = t;
    if (value) *value = w->source;
    return S_OK;
}

static HRESULT mock_create_environment( void *handler )
{
    struct mock_env *e;
    init_mock_vtbls();
    if (!(e = (struct mock_env *)mock_alloc( sizeof(*e), mock_env_vtbl, NULL )))
        return E_OUTOFMEMORY;
    return post_completion( handler, e, e, 1 );
}

HRESULT WINAPI CompareBrowserVersions( LPCSTR v1, LPCSTR v2, int *result )
{
    HRESULT (WINAPI *p)( LPCSTR, LPCSTR, int * );
    if (!(p = get_eb_export( "CompareBrowserVersions" ))) return E_FAIL;
    return p( v1, v2, result );
}

HRESULT WINAPI CreateCoreWebView2Controller( HWND parent, void *handler )
{
    HRESULT (WINAPI *p)( HWND, void * );
    if (!(p = get_eb_export( "CreateCoreWebView2Controller" ))) return E_FAIL;
    return p( parent, handler );
}

HRESULT WINAPI CreateCoreWebView2Environment( LPCWSTR browser_folder, void *handler )
{
    HRESULT (WINAPI *p)( LPCWSTR, void * );
    if (!(p = get_eb_export( "CreateCoreWebView2Environment" ))) return mock_create_environment( handler );
    return p( browser_folder, handler );
}

HRESULT WINAPI CreateCoreWebView2EnvironmentOptions( LPCWSTR language, void *options )
{
    HRESULT (WINAPI *p)( LPCWSTR, void * );
    if (!(p = get_eb_export( "CreateCoreWebView2EnvironmentOptions" ))) return E_FAIL;
    return p( language, options );
}

struct default_env_options
{
    const void **vtbl;
    LONG ref;
    WCHAR args[128];
};

static void *loader_cotaskmem_alloc( SIZE_T sz )
{
    static LPVOID (WINAPI *p_alloc)( SIZE_T );
    if (!p_alloc)
    {
        HMODULE ole = GetModuleHandleW( L"ole32.dll" );
        if (!ole) ole = LoadLibraryW( L"ole32.dll" );
        if (ole) p_alloc = (void *)GetProcAddress( ole, "CoTaskMemAlloc" );
    }
    return p_alloc ? p_alloc( sz ) : HeapAlloc( GetProcessHeap(), 0, sz );
}

static HRESULT WINAPI def_opts_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IUnknown_local =
        { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    static const GUID IID_ICoreWebView2EnvironmentOptions_g =
        { 0x2fde08a8, 0x1e9a, 0x4766, { 0x8c, 0x05, 0x95, 0xa9, 0xce, 0xb9, 0xd1, 0xc5 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown_local ) || IsEqualGUID( iid, &IID_ICoreWebView2EnvironmentOptions_g ))
    {
        *out = iface;
        InterlockedIncrement( &((struct default_env_options *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI def_opts_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct default_env_options *)iface)->ref );
}
static ULONG WINAPI def_opts_Release( void *iface )
{
    struct default_env_options *o = iface;
    LONG r = InterlockedDecrement( &o->ref );
    if (!r)
    {
        HeapFree( GetProcessHeap(), 0, (void *)o->vtbl );
        HeapFree( GetProcessHeap(), 0, o );
    }
    return r;
}
static HRESULT WINAPI def_opts_get_AdditionalBrowserArguments( void *iface, LPWSTR *val )
{
    struct default_env_options *o = iface;
    size_t sz;
    if (!val) return E_POINTER;
    sz = (lstrlenW( o->args ) + 1) * sizeof(WCHAR);
    *val = loader_cotaskmem_alloc( sz );
    if (!*val) return E_OUTOFMEMORY;
    memcpy( *val, o->args, sz );
    return S_OK;
}
static HRESULT WINAPI def_opts_put_AdditionalBrowserArguments( void *iface, LPCWSTR val ) { return S_OK; }
static HRESULT WINAPI def_opts_get_Language( void *iface, LPWSTR *val )
{
    if (!val) return E_POINTER;
    *val = loader_cotaskmem_alloc( sizeof(L"en-US") );
    if (*val) memcpy( *val, L"en-US", sizeof(L"en-US") );
    return *val ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI def_opts_put_Language( void *iface, LPCWSTR val ) { return S_OK; }
static HRESULT WINAPI def_opts_get_TargetCompatibleBrowserVersion( void *iface, LPWSTR *val )
{
    static const WCHAR ver[] = L"156.0.4314.42";
    if (!val) return E_POINTER;
    *val = loader_cotaskmem_alloc( sizeof(ver) );
    if (*val) memcpy( *val, ver, sizeof(ver) );
    return *val ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI def_opts_put_TargetCompatibleBrowserVersion( void *iface, LPCWSTR val ) { return S_OK; }
static HRESULT WINAPI def_opts_get_AllowSSO( void *iface, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI def_opts_put_AllowSSO( void *iface, BOOL val ) { return S_OK; }

static void *create_default_env_options( void )
{
    struct default_env_options *o;
    const void **vt;
    int cy = GetSystemMetrics( SM_CYSCREEN );
    if (cy <= 0 || cy > 1024) return NULL;
    o = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*o) );
    vt = HeapAlloc( GetProcessHeap(), 0, 11 * sizeof(*vt) );
    if (!o || !vt) { HeapFree( GetProcessHeap(), 0, o ); HeapFree( GetProcessHeap(), 0, vt ); return NULL; }
    vt[0] = def_opts_QueryInterface;
    vt[1] = def_opts_AddRef;
    vt[2] = def_opts_Release;
    vt[3] = def_opts_get_AdditionalBrowserArguments;
    vt[4] = def_opts_put_AdditionalBrowserArguments;
    vt[5] = def_opts_get_Language;
    vt[6] = def_opts_put_Language;
    vt[7] = def_opts_get_TargetCompatibleBrowserVersion;
    vt[8] = def_opts_put_TargetCompatibleBrowserVersion;
    vt[9] = def_opts_get_AllowSSO;
    vt[10] = def_opts_put_AllowSSO;
    o->vtbl = vt;
    o->ref = 1;
    lstrcpyW( o->args, L"--force-device-scale-factor=0.50" );
    return o;
}

HRESULT WINAPI CreateCoreWebView2EnvironmentWithOptions( LPCWSTR browser_folder,
        LPCWSTR user_data_folder, void *options, void *handler )
{
    HRESULT (WINAPI *p)( LPCWSTR, LPCWSTR, void *, void * );
    HRESULT (WINAPI *p_int)( BOOL, int, LPCWSTR, void *, void * );
    void *def_opts = NULL;
    HRESULT hr;

    if (!options)
    {
        def_opts = create_default_env_options();
        if (def_opts) options = def_opts;
    }
    if ((p = get_eb_export( "CreateCoreWebView2EnvironmentWithOptions" )))
        hr = p( browser_folder, user_data_folder, options, handler );
    else if ((p_int = get_eb_export( "CreateWebViewEnvironmentWithOptionsInternal" )))
        hr = p_int( TRUE, 0, user_data_folder, options, handler );
    else
        hr = mock_create_environment( handler );
    if (def_opts) def_opts_Release( def_opts );
    return hr;
}

HRESULT WINAPI GetAvailableCoreWebView2BrowserVersionString( LPCWSTR browser_folder,
        LPCWSTR *version )
{
    static const WCHAR version_str[] = L"156.0.4314.42";
    HRESULT (WINAPI *p)( LPCWSTR, LPCWSTR * );
    if (!(p = get_eb_export( "GetAvailableCoreWebView2BrowserVersionString" )))
    {
        size_t len = (wcslen( version_str ) + 1) * sizeof(WCHAR);
        WCHAR *w = HeapAlloc( GetProcessHeap(), 0, len );
        if (!w) return E_OUTOFMEMORY;
        memcpy( w, version_str, len );
        *version = w;
        return S_OK;
    }
    return p( browser_folder, version );
}

HRESULT WINAPI GetAvailableCoreWebView2BrowserVersionStringWithOptions( LPCWSTR browser_folder,
        void *options, LPCWSTR *version )
{
    HRESULT (WINAPI *p)( LPCWSTR, void *, LPCWSTR * );
    if (!(p = get_eb_export( "GetAvailableCoreWebView2BrowserVersionStringWithOptions" )))
        return E_FAIL;
    return p( browser_folder, options, version );
}

HRESULT WINAPI GetCoreWebView2FromController( void *controller, void *webview )
{
    HRESULT (WINAPI *p)( void *, void * );
    if (!(p = get_eb_export( "GetCoreWebView2FromController" ))) return E_FAIL;
    return p( controller, webview );
}

HRESULT WINAPI CreateCoreWebView2ControllerSync( HWND parent, void *webview_controller )
{
    HRESULT (WINAPI *p)( HWND, void * );
    if (!(p = get_eb_export( "CreateCoreWebView2ControllerSync" ))) return E_FAIL;
    return p( parent, webview_controller );
}
