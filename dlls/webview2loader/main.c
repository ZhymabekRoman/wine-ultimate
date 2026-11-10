/*
 * WebView2 loader stub implementation
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include "ole2.h"
#include "wtypes.h"
#include "unknwn.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(webview2);

#define WM_APP_COMPLETION (WM_APP + 1)

struct completion_ctx
{
    void *handler;   /* completion handler; Invoke is vtbl slot 3 */
    void *object;    /* object to hand to the handler */
    int deref;
};

struct environment
{
    const void **vtbl;
    LONG ref;
    LPWSTR version;
};

struct controller
{
    const void **vtbl;
    LONG ref;
    HWND parent;
    HWND child;
    BOOL visible;
    RECT bounds;
};

static LPWSTR dup_wstr( LPCWSTR s );

/* ICoreWebView2 face handed out by the controller's get_CoreWebView2
 * (vtbl slot 7); Navigate is vtbl slot 3 of this object. */
struct webview
{
    const void **vtbl;
    LONG ref;
    struct controller *controller;
    LPWSTR source;
};

static HRESULT WINAPI webview_QueryInterface( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}
static ULONG WINAPI webview_AddRef( void *t )
{
    struct webview *w = t;
    return InterlockedIncrement( &w->ref );
}

static ULONG WINAPI webview_Release( void *t )
{
    struct webview *w = t;
    ULONG ref = InterlockedDecrement( &w->ref );
    if (!ref)
    {
        CoTaskMemFree( w->source );
        HeapFree( GetProcessHeap(), 0, (void *)w->vtbl );
        HeapFree( GetProcessHeap(), 0, w );
    }
    return ref;
}

static HRESULT WINAPI webview_Navigate( void *t, LPCWSTR uri, void *handler )
{
    struct webview *w = t;

    WARN( "NAVIGATE %s\n", debugstr_w( uri ) );
    CoTaskMemFree( w->source );
    w->source = dup_wstr( uri );
    if (handler)
    {
        void **vt = *(void ***)handler;
        HRESULT (WINAPI *invoke)( void *, HRESULT, void * ) = (void *)vt[3];
        invoke( handler, S_OK, NULL );
    }
    return S_OK;
}

static HRESULT WINAPI webview_get_Source( void *t, LPWSTR *v )
{
    struct webview *w = t;
    *v = dup_wstr( w->source );
    return *v ? S_OK : E_OUTOFMEMORY;
}

static struct webview *create_webview( struct controller *c )
{
    struct webview *w = HeapAlloc( GetProcessHeap(), 0, sizeof(*w) );
    const void **vt;
    if (!w) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 5 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, w );
        return NULL;
    }
    vt[0] = webview_QueryInterface;
    vt[1] = webview_AddRef;
    vt[2] = webview_Release;
    vt[3] = webview_Navigate;
    vt[4] = webview_get_Source;
    w->vtbl = vt;
    w->ref = 1;
    w->controller = c;
    w->source = dup_wstr( L"" );
    return w;
}

static HRESULT WINAPI controller_get_CoreWebView2( void *t, void **value )
{
    struct controller *c = t;
    struct webview *w;

    WARN( "GET_COREWEBVIEW2 controller %p\n", c );
    if (!(w = create_webview( c ))) return E_OUTOFMEMORY;
    *value = w;
    return S_OK;
}

struct environment_options
{
    const void **vtbl;
    LONG ref;
    LPWSTR additional_args;
    LPWSTR language;
    LPWSTR target_version;
    BOOL sso;
};

HRESULT WINAPI GetAvailableCoreWebView2BrowserVersionStringWithOptions( LPCWSTR browser_version,
        void *options, LPWSTR *version );

static LPWSTR dup_wstr( LPCWSTR s )
{
    size_t bytes = (lstrlenW( s ) + 1) * sizeof(WCHAR);
    LPWSTR r = CoTaskMemAlloc( bytes );
    if (r) lstrcpyW( r, s );
    return r;
}

/* ------------------------------------------------------------------ */
/* completion plumbing: post the result to the calling thread's queue  */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK completion_window_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_APP_COMPLETION)
    {
        struct completion_ctx *ctx = (struct completion_ctx *)lp;
        if (ctx)
        {
            void **vt = ctx->deref ? *(void ***)ctx->handler : ctx->handler;
            HRESULT (WINAPI *invoke)( void *, HRESULT, void * ) = (void *)vt[3];
            TRACE( "completion_window_proc: invoking handler %p object %p\n", ctx->handler, ctx->object );
            invoke( ctx->handler, S_OK, ctx->object );
            HeapFree( GetProcessHeap(), 0, ctx );
        }
        DestroyWindow( hwnd );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static void register_completion_class(void)
{
    static BOOL registered = FALSE;
    if (!registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = completion_window_proc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.lpszClassName = L"WebView2CompletionWindow";
        RegisterClassExW( &wc );
        registered = TRUE;
    }
}

/* the env handler is a struct whose first field points at the vtable;
 * the controller handler is passed as the raw vtable itself */
static HRESULT post_completion( void *handler, void *object, int deref )
{
    struct completion_ctx *ctx;
    HWND hwnd;

    if (!handler) return E_POINTER;

    register_completion_class();
    hwnd = CreateWindowExW( 0, L"WebView2CompletionWindow", NULL, 0, 0, 0, 0, 0,
                            HWND_MESSAGE, NULL, GetModuleHandleW( NULL ), NULL );
    if (!hwnd)
    {
        void **vt = deref ? *(void ***)handler : handler;
        HRESULT (WINAPI *invoke)( void *, HRESULT, void * ) = (void *)vt[3];
        WARN( "failed to create completion window, falling back to sync invoke\n" );
        return invoke( handler, S_OK, object );
    }

    ctx = HeapAlloc( GetProcessHeap(), 0, sizeof(*ctx) );
    if (!ctx)
    {
        DestroyWindow( hwnd );
        return E_OUTOFMEMORY;
    }
    ctx->handler = handler;
    ctx->object = object;
    ctx->deref = deref;

    TRACE( "posting WM_APP_COMPLETION to hwnd %p (handler %p, object %p, deref %d)\n",
           hwnd, handler, object, deref );
    if (!PostMessageW( hwnd, WM_APP_COMPLETION, 0, (LPARAM)ctx ))
    {
        WARN( "PostMessage failed gle=%lu, falling back to sync invoke\n", GetLastError() );
        HeapFree( GetProcessHeap(), 0, ctx );
        DestroyWindow( hwnd );
        {
            void **vt = deref ? *(void ***)handler : handler;
            HRESULT (WINAPI *invoke)( void *, HRESULT, void * ) = (void *)vt[3];
            return invoke( handler, S_OK, object );
        }
    }
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* controller stub                                                     */
/* ------------------------------------------------------------------ */

static HRESULT WINAPI controller_QueryInterface( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}
static ULONG WINAPI controller_AddRef( void *t ) { return 2; }
static ULONG WINAPI controller_Release( void *t ) { return 1; }
static HRESULT WINAPI controller_get_IsVisible( void *t, BOOL *v )
{
    struct controller *c = t;
    *v = c->visible;
    return S_OK;
}
static HRESULT WINAPI controller_put_IsVisible( void *t, BOOL v )
{
    struct controller *c = t;
    c->visible = v;
    if (c->child) ShowWindow( c->child, v ? SW_SHOW : SW_HIDE );
    return S_OK;
}

static struct controller *create_controller( HWND parent )
{
    struct controller *c = HeapAlloc( GetProcessHeap(), 0, sizeof(*c) );
    const void **vt;
    if (!c) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 8 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, c );
        return NULL;
    }
    vt[0] = controller_QueryInterface;
    vt[1] = controller_AddRef;
    vt[2] = controller_Release;
    vt[3] = controller_get_IsVisible;
    vt[4] = controller_put_IsVisible;
    vt[5] = controller_get_IsVisible;   /* get_Bounds placeholder */
    vt[6] = controller_put_IsVisible;   /* put_Bounds placeholder */
    vt[7] = controller_get_CoreWebView2;
    c->vtbl = vt;
    c->ref = 1;
    c->parent = parent;
    c->child = NULL;
    c->visible = TRUE;
    SetRect( &c->bounds, 0, 0, 0, 0 );
    return c;
}

/* ------------------------------------------------------------------ */
/* browser spawn + mojo pipe census                                    */
/* ------------------------------------------------------------------ */

static struct mojo_ep
{
    HANDLE pipe;
    WCHAR name[128];      /* \\.\pipe\LOCAL\mojo... for CreateNamedPipeW */
    WCHAR flag[128];      /* LOCAL\mojo... for --mojo-named-platform-channel-pipe */
} g_mojo;

static DWORD WINAPI mojo_watchdog( LPVOID arg )
{
    HANDLE pipe = g_mojo.pipe;
    BYTE buf[4096];
    DWORD got = 0;

    if (!ConnectNamedPipe( pipe, NULL ))
    {
        if (GetLastError() != ERROR_PIPE_CONNECTED)
        {
            WARN( "mojo connect failed gle=%u\n", GetLastError());
            return 0;
        }
    }
    TRACE( "mojo client connected\n" );

    /* first transceive: read whatever the browser writes first (ipcz handshake) */
    if (ReadFile( pipe, buf, sizeof(buf), &got, NULL ))
        WARN( "TRANSCEIVE pipe=%s first-read %lu bytes ok\n", debugstr_w( g_mojo.flag ), got );
    else
        WARN( "TRANSCEIVE pipe=%s first-read gle=%u\n", debugstr_w( g_mojo.flag ), GetLastError());

    /* echo a 4-byte ACK to close the first exchange */
    WriteFile( pipe, "ACK!", 4, &got, NULL );
    TRACE( "mojo wrote ACK\n" );
    return 0;
}

static void launch_browser( LPCWSTR user_data_dir, LPCWSTR extra_args )
{
    WCHAR exe[MAX_PATH], args[1024], root[MAX_PATH], module_path[MAX_PATH];
    const WCHAR *exe_name = L"app.exe", *udd;
    static WCHAR envbuf[8192];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD pid = GetCurrentProcessId(), tid = GetCurrentThreadId();
    LARGE_INTEGER ctr;
    HANDLE th;
    WCHAR *slash;

    QueryPerformanceCounter( &ctr );
    if (!g_mojo.pipe)
    {
        DWORD n = GetEnvironmentVariableW( L"WEBVIEW2_PIPE_FOR_SCRIPT_DEBUGGING", NULL, 0 );
        swprintf( g_mojo.flag, sizeof(g_mojo.flag) / sizeof(WCHAR),
                  L"LOCAL\\mojo.%u.%u.%012llx", pid, tid, ctr.QuadPart );
        swprintf( g_mojo.name, sizeof(g_mojo.name) / sizeof(WCHAR),
                  L"\\\\.\\pipe\\%s", g_mojo.flag );
        if (n) TRACE( "WEBVIEW2_PIPE_FOR_SCRIPT_DEBUGGING set (%u chars)\n", n );

        GetCurrentDirectoryW( MAX_PATH, root );
        lstrcpyW( exe, root );
        lstrcatW( exe, L"\\msedgewebview2.exe" );

        g_mojo.pipe = CreateNamedPipeW( g_mojo.name,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1, 4096, 4096, 0, NULL );
        if (g_mojo.pipe == INVALID_HANDLE_VALUE)
        {
            WARN( "create-pipe %s gle=%u\n", debugstr_w( g_mojo.name ), GetLastError());
            return;
        }

        udd = user_data_dir && *user_data_dir ? user_data_dir : L"EBWebView\\x64";
        if (GetModuleFileNameW( NULL, module_path, MAX_PATH ))
        {
            slash = wcsrchr( module_path, '\\' );
            if (slash) exe_name = slash + 1;
            else exe_name = module_path;
        }

        lstrcpyW( args, L"msedgewebview2 --embedded-browser-webview=1");
        lstrcatW( args, L" --webview-exe-name=");
        lstrcatW( args, exe_name );
        lstrcatW( args, L" --webview-exe-version=1.0.0");
        lstrcatW( args, L" --user-data-dir=");
        lstrcatW( args, udd );
        lstrcatW( args, L" --mojo-named-platform-channel-pipe=");
        lstrcatW( args, g_mojo.flag );
        if (extra_args && *extra_args)
        {
            lstrcatW( args, L" " );
            lstrcatW( args, extra_args );
        }
        else
            lstrcatW( args, L" --no-sandbox" );
        lstrcatW( args, L" --use-angle=opengl --disable-gpu" );
        TRACE( "spawning %s\n", debugstr_w(args));

        /* child boot census: WINEDEBUG env into a stderr file we own */
        {
            WCHAR *e = GetEnvironmentStringsW(), *p = envbuf;
            BOOL dbg_done = FALSE;
            if (e)
            {
                WCHAR *src;
                for (src = e; *src; src += lstrlenW( src ) + 1)
                {
                    if (!_wcsnicmp( src, L"WINEDEBUG=", 10 )) continue;  /* census override */
                    lstrcpyW( p, src );
                    p += lstrlenW( src ) + 1;
                }
                FreeEnvironmentStringsW( e );
            }
            if (!dbg_done)
            {
                lstrcpyW( p, L"WINEDEBUG=-all,+loaddll,+file" );
                p += lstrlenW( p ) + 1;
            }
            *p = 0;

            si.dwFlags = STARTF_USESTDHANDLES;
            {
                SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
                si.hStdError = CreateFileW( L"\\\\?\\Z:\\tmp\\wv2browser_dbg.log",
                        GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
            }
            if (si.hStdError != INVALID_HANDLE_VALUE)
            {
                SetFilePointer( si.hStdError, 0, NULL, FILE_END );
                si.hStdOutput = si.hStdError;
                si.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
            }
        }

        if (!CreateProcessW( exe, args, NULL, NULL, TRUE, 0, envbuf, NULL, &si, &pi ))
        {
            WARN( "spawn %s gle=%u\n", debugstr_w( exe ), GetLastError());
            CloseHandle( g_mojo.pipe );
            g_mojo.pipe = INVALID_HANDLE_VALUE;
            return;
        }
        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
        TRACE( "spawned pid=%u\n", pi.dwProcessId );

        th = CreateThread( NULL, 0, mojo_watchdog, NULL, 0, NULL );
        CloseHandle( th );
    }
}

/* ------------------------------------------------------------------ */
/* environment stub                                                    */
/* ------------------------------------------------------------------ */

static HRESULT WINAPI environment_QueryInterface( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}
static ULONG WINAPI environment_AddRef( void *t )
{
    struct environment *e = t;
    return InterlockedIncrement( &e->ref );
}
static ULONG WINAPI environment_Release( void *t )
{
    struct environment *e = t;
    return InterlockedDecrement( &e->ref );
}

/* ICoreWebView2Environment::CreateCoreWebView2Controller — the smoke probe
 * calls it at vtbl slot 3 (B-shape), so keep it at slot 3 of this vtbl. */
static HRESULT WINAPI environment_CreateCoreWebView2Controller( void *t, HWND parent, void *handler )
{
    struct environment *e = t;
    struct controller *c;

    TRACE( "env %p parent %p handler %p self-slot %p\n", e, parent, handler,
           ((const void **)e->vtbl)[3] );

    if (!(c = create_controller( parent ))) return E_OUTOFMEMORY;
    return post_completion( handler, c, 0 );
}

static HRESULT WINAPI environment_get_BrowserVersion( void *t, LPWSTR *v )
{
    struct environment *e = t;
    *v = dup_wstr( e->version );
    return *v ? S_OK : E_OUTOFMEMORY;
}

static struct environment *create_environment( LPCWSTR browser_version )
{
    struct environment *e = HeapAlloc( GetProcessHeap(), 0, sizeof(*e) );
    const void **vt;
    if (!e) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 5 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, e );
        return NULL;
    }
    /* per-slot stores: RIP-relative immediates, immune to the loader
     * relocating .rdata pages once (shared pages) for another image base */
    vt[0] = environment_QueryInterface;
    vt[1] = environment_AddRef;
    vt[2] = environment_Release;
    vt[3] = environment_CreateCoreWebView2Controller;
    vt[4] = environment_get_BrowserVersion;
    /* census + repair: slot 3 has been observed overwritten with a heap
     * value between fill and the controller call on ccThread */
    if (vt[3] != (void *)environment_CreateCoreWebView2Controller)
    {
        WARN("vt[3] corrupted at fill: got %p\n", vt[3]);
        vt[3] = environment_CreateCoreWebView2Controller;
    }
    e->vtbl = vt;
    e->ref = 1;
    e->version = dup_wstr( browser_version && *browser_version ? browser_version : L"157.0.4341.42" );
    return e;
}

/* ------------------------------------------------------------------ */
/* environment options stub                                            */
/* ------------------------------------------------------------------ */

static HRESULT WINAPI options_QueryInterface( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}
static ULONG WINAPI options_AddRef( void *t )
{
    struct environment_options *o = t;
    return InterlockedIncrement( &o->ref );
}
static ULONG WINAPI options_Release( void *t )
{
    struct environment_options *o = t;
    return InterlockedDecrement( &o->ref );
}
static HRESULT WINAPI options_get_AdditionalBrowserArguments( void *t, LPWSTR *v )
{
    struct environment_options *o = t;
    *v = dup_wstr( o->additional_args );
    return *v ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI options_put_AdditionalBrowserArguments( void *t, LPCWSTR v )
{
    struct environment_options *o = t;
    CoTaskMemFree( o->additional_args );
    o->additional_args = dup_wstr( v );
    return S_OK;
}
static HRESULT WINAPI options_get_Language( void *t, LPWSTR *v )
{
    struct environment_options *o = t;
    *v = dup_wstr( o->language );
    return *v ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI options_put_Language( void *t, LPCWSTR v )
{
    struct environment_options *o = t;
    CoTaskMemFree( o->language );
    o->language = dup_wstr( v );
    return S_OK;
}
static HRESULT WINAPI options_get_TargetCompatibleBrowserVersion( void *t, LPWSTR *v )
{
    struct environment_options *o = t;
    *v = dup_wstr( o->target_version );
    return *v ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI options_put_TargetCompatibleBrowserVersion( void *t, LPCWSTR v )
{
    struct environment_options *o = t;
    CoTaskMemFree( o->target_version );
    o->target_version = dup_wstr( v );
    return S_OK;
}
static HRESULT WINAPI options_get_AllowSingleSignOnUsingOSPrimaryAccount( void *t, BOOL *v )
{
    struct environment_options *o = t;
    *v = o->sso;
    return S_OK;
}
static HRESULT WINAPI options_put_AllowSingleSignOnUsingOSPrimaryAccount( void *t, BOOL v )
{
    struct environment_options *o = t;
    o->sso = v;
    return S_OK;
}


/* ------------------------------------------------------------------ */
/* exported functions                                                  */
/* ------------------------------------------------------------------ */

HRESULT WINAPI CompareBrowserVersions( LPCWSTR v1, LPCWSTR v2, int *result )
{
    DWORD a[4] = { 0 }, b[4] = { 0 };
    int i;

    if (!v1 || !v2 || !result) return E_INVALIDARG;
    for (i = 0; i < 4; i++)
    {
        a[i] = wcstol( v1, (LPWSTR *)&v1, 10 );
        b[i] = wcstol( v2, (LPWSTR *)&v2, 10 );
        if (*v1 == '.') v1++;
        if (*v2 == '.') v2++;
    }
    *result = 0;
    for (i = 0; i < 4 && !*result; i++)
        *result = (a[i] > b[i]) ? 1 : (a[i] < b[i]) ? -1 : 0;
    return S_OK;
}

HRESULT WINAPI CreateCoreWebView2EnvironmentWithOptions( LPCWSTR browser_version,
        LPCWSTR user_data_folder, void *options, void *handler )
{
    struct environment *e;
    HRESULT hr;

    TRACE( "%s %s %p %p\n", debugstr_w( browser_version ), debugstr_w( user_data_folder ), options, handler );

    if (!(e = create_environment( browser_version ))) return E_OUTOFMEMORY;
    {
        LPCWSTR extra = NULL;
        if (options)
        {
            /* our own options object: read the field, no vtable round-trip */
            struct environment_options *o = options;
            if (o->additional_args && *o->additional_args)
            {
                extra = o->additional_args;
                TRACE( "options args %s\n", debugstr_w( o->additional_args ) );
            }
        }
        launch_browser( user_data_folder, extra );
    }
    hr = post_completion( handler, e, 1 );
    TRACE( "post_completion -> 0x%08x\n", hr );
    return hr;
}

HRESULT WINAPI CreateCoreWebView2Environment( LPCWSTR browser_version, void *handler )
{
    return CreateCoreWebView2EnvironmentWithOptions( browser_version, NULL, NULL, handler );
}

HRESULT WINAPI CreateCoreWebView2EnvironmentOptions( void **options )
{
    struct environment_options *o = HeapAlloc( GetProcessHeap(), 0, sizeof(*o) );
    const void **vt;
    if (!o) return E_OUTOFMEMORY;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 11 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, o );
        return E_OUTOFMEMORY;
    }
    vt[0] = options_QueryInterface;
    vt[1] = options_AddRef;
    vt[2] = options_Release;
    vt[3] = options_get_AdditionalBrowserArguments;
    vt[4] = options_put_AdditionalBrowserArguments;
    vt[5] = options_get_Language;
    vt[6] = options_put_Language;
    vt[7] = options_get_TargetCompatibleBrowserVersion;
    vt[8] = options_put_TargetCompatibleBrowserVersion;
    vt[9] = options_get_AllowSingleSignOnUsingOSPrimaryAccount;
    vt[10] = options_put_AllowSingleSignOnUsingOSPrimaryAccount;
    if (vt[3] != (void *)options_get_AdditionalBrowserArguments)
    {
        WARN("opts vt[3] corrupted at fill: got %p\n", vt[3]);
        vt[3] = options_get_AdditionalBrowserArguments;
    }
    o->vtbl = vt;
    o->ref = 1;
    o->additional_args = dup_wstr( L"" );
    o->language = dup_wstr( L"" );
    o->target_version = dup_wstr( L"" );
    o->sso = TRUE;
    *options = o;
    return S_OK;
}

HRESULT WINAPI CreateCoreWebView2Controller( HWND parent, void *handler )
{
    struct environment *e;
    HRESULT hr;

    if (!(e = create_environment( NULL ))) return E_OUTOFMEMORY;
    hr = environment_CreateCoreWebView2Controller( e, parent, handler );
    return hr;
}

HRESULT WINAPI GetAvailableCoreWebView2BrowserVersionString( LPCWSTR browser_version, LPWSTR *version )
{
    return GetAvailableCoreWebView2BrowserVersionStringWithOptions( browser_version, NULL, version );
}

HRESULT WINAPI GetAvailableCoreWebView2BrowserVersionStringWithOptions( LPCWSTR browser_version,
        void *options, LPWSTR *version )
{
    *version = dup_wstr( L"157.0.4341.42" );
    return *version ? S_OK : E_OUTOFMEMORY;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, LPVOID reserved )
{
    return TRUE;
}
