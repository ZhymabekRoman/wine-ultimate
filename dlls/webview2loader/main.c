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
    0, 0, { (ULONG_PTR)&loader_cs, 0, 0 }
};
static CRITICAL_SECTION loader_cs = { &loader_cs_debug, 0, 0, 0, 0, 0 };

static HMODULE eb_module;             /* EmbeddedBrowserWebView.dll */

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
        FARPROC proc;

        lstrcpynW( path, root, MAX_PATH - 64 );
        lstrcatW( path, subdirs[i] );
        lstrcatW( path, eb_dll_name );
        if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES) continue;
        mod = LoadLibraryW( path );
        if (!mod)
        {
            WARN("LoadLibraryW(%s) failed, gle=%lu\n", debugstr_w( path ), GetLastError());
            continue;
        }
        proc = GetProcAddress( mod, "CreateCoreWebView2EnvironmentWithOptions" );
        if (!proc)
        {
            WARN("%s lacks CreateCoreWebView2EnvironmentWithOptions\n", debugstr_w( path ));
            FreeLibrary( mod );
            continue;
        }
        TRACE("loaded %s\n", debugstr_w( path ));
        return mod;
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
    if (!(p = get_eb_export( "CreateCoreWebView2Environment" ))) return E_FAIL;
    return p( browser_folder, handler );
}

HRESULT WINAPI CreateCoreWebView2EnvironmentOptions( LPCWSTR language, void *options )
{
    HRESULT (WINAPI *p)( LPCWSTR, void * );
    if (!(p = get_eb_export( "CreateCoreWebView2EnvironmentOptions" ))) return E_FAIL;
    return p( language, options );
}

HRESULT WINAPI CreateCoreWebView2EnvironmentWithOptions( LPCWSTR browser_folder,
        LPCWSTR user_data_folder, void *options, void *handler )
{
    HRESULT (WINAPI *p)( LPCWSTR, LPCWSTR, void *, void * );
    if (!(p = get_eb_export( "CreateCoreWebView2EnvironmentWithOptions" ))) return E_FAIL;
    return p( browser_folder, user_data_folder, options, handler );
}

HRESULT WINAPI GetAvailableCoreWebView2BrowserVersionString( LPCWSTR browser_folder,
        LPCWSTR *version )
{
    HRESULT (WINAPI *p)( LPCWSTR, LPCWSTR * );
    if (!(p = get_eb_export( "GetAvailableCoreWebView2BrowserVersionString" ))) return E_FAIL;
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
