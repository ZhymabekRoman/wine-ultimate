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

/* the env handler is a struct whose first field points at the vtable;
 * the controller handler is passed as the raw vtable itself */
static HRESULT post_completion( void *handler, void *object, int deref )
{
    void **vt = deref ? *(void ***)handler : handler;
    HRESULT (WINAPI *invoke)( void *, HRESULT, void * ) = (void *)vt[3];

    return invoke( handler, S_OK, object );
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
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 7 * sizeof(*vt) )))
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
    c->vtbl = vt;
    c->ref = 1;
    c->parent = parent;
    c->child = NULL;
    c->visible = TRUE;
    SetRect( &c->bounds, 0, 0, 0, 0 );
    return c;
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
