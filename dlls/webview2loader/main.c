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
    void *handler;   /* completion handler; Invoke is vtbl slot 3 (B) / 6 (WinRT face) */
    void *object;    /* args to hand to the handler */
    void *sender;    /* TypedEventHandler sender (the environment) */
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
    void *h_frame_nav;
    LONG token;
};

static LPWSTR dup_wstr( LPCWSTR s );
static HRESULT WINAPI webview_stub( void *t, ... );

/* ICoreWebView2 face handed out by the controller's get_CoreWebView2
 * (vtbl slot 7); Navigate is vtbl slot 3 of this object. */
struct webview
{
    const void **vtbl;
    LONG ref;
    struct controller *controller;
    LPWSTR source;
    void *h_nav_starting;
    void *h_dom_loaded;
    void *h_nav_completed;
    void *h_source_changed;
    void *h_permission;
    LONG token;
};

static HRESULT WINAPI webview_QueryInterface( void *t, REFIID riid, void **obj )
{
    ERR( "webview_QI %p iid %s\n", t, debugstr_guid( riid ) );
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

static BOOL CALLBACK loader_find_child_cb( HWND hwnd, LPARAM lparam )
{
    WCHAR cls[128] = {0};
    GetClassNameW( hwnd, cls, ARRAY_SIZE( cls ) );
    if (wcsstr( cls, L"Chrome_WidgetWin" ) || wcsstr( cls, L"Intermediate D3D" ) ||
        wcsstr( cls, L"RenderWidget" ))
    {
        *(HWND *)lparam = hwnd;
        return FALSE;
    }
    return TRUE;
}

/* Completion delegates are COM objects: the vtable slot 3 entry is the
 * TypedEventHandler Invoke (sender, args). Mirrors post_completion(). */
static void fire_handler( void *handler, void *sender, void *args )
{
    void **vt;
    HRESULT (WINAPI *invoke)( void *, void *, void * );

    if (!handler) return;
    vt = *(void ***)handler;
    invoke = (void *)vt[3];
    invoke( handler, sender, args );
}

static HRESULT WINAPI webview_Navigate( void *t, LPCWSTR uri, void *handler )
{
    struct webview *w = t;

    ERR( "NAVIGATE %p uri %s handler %p\n", w, debugstr_w( uri ), handler );
    CoTaskMemFree( w->source );
    w->source = dup_wstr( uri );
    {
        struct event_args *ea = create_event_args( uri );
        void *args = ea ? (void *)ea : (void *)w;
        struct controller *c = w->controller;
        if (c && c->h_frame_nav) fire_handler( c->h_frame_nav, w, args );
        if (w->h_nav_starting) fire_handler( w->h_nav_starting, w, args );
        if (w->h_dom_loaded) fire_handler( w->h_dom_loaded, w, args );
        if (w->h_nav_completed) fire_handler( w->h_nav_completed, w, args );
        if (w->h_source_changed) fire_handler( w->h_source_changed, w, w );
    }
    if (handler) fire_handler( handler, w, NULL );
    return S_OK;
}

static HRESULT WINAPI webview_get_Source( void *t, LPWSTR *v )
{
    struct webview *w = t;
    ERR( "get_Source %p out %p\n", w, v );
    if (!v) return E_POINTER;
    *v = dup_wstr( w->source );
    return *v ? S_OK : E_OUTOFMEMORY;
}

/* WinRT IInspectable prologue slots (3-5) + ICoreWebView2 body. The app's RCW
 * (ComWrappers) reads GetIids/GetRuntimeClassName/GetTrustLevel right after
 * QI, and the WhatsApp bridge then drives Navigate/add_* (r43 AV: the RCW
 * called Navigate-as-GetIids when the vtbl omitted the prologue). */
static HRESULT WINAPI webview_GetIids( void *t, ULONG *count, GUID **ids )
{
    ERR( "webview_GetIids %p\n", t );
    *count = 0;
    *ids = NULL;
    return S_OK;
}
static HRESULT WINAPI webview_GetRuntimeClassName( void *t, LPWSTR *name )
{
    ERR( "webview_GetRuntimeClassName %p\n", t );
    *name = dup_wstr( L"Microsoft.Web.WebView2.Core.CoreWebView2" );
    return *name ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI webview_GetTrustLevel( void *t, LONG *level )
{
    ERR( "webview_GetTrustLevel %p\n", t );
    *level = 0;
    return S_OK;
}
static HRESULT WINAPI webview_stub( void *t, ... )
{
    va_list ap;
    void *a1;

    va_start( ap, t );
    a1 = va_arg( ap, void * );
    va_end( ap );
    TRACE( "stub this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_put_Source( void *t, LPCWSTR v )
{
    struct webview *w = t;
    CoTaskMemFree( w->source );
    w->source = dup_wstr( v );
    return S_OK;
}

/* per-slot traced stubs for the webview face (gap-hunting) */
static HRESULT WINAPI webview_stub_0( void *t, void *a1 )
{
    TRACE( "webview slot 0 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_1( void *t, void *a1 )
{
    TRACE( "webview slot 1 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_2( void *t, void *a1 )
{
    TRACE( "webview slot 2 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_3( void *t, void *a1 )
{
    TRACE( "webview slot 3 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_4( void *t, void *a1 )
{
    TRACE( "webview slot 4 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_5( void *t, void *a1 )
{
    TRACE( "webview slot 5 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_6( void *t, void *a1 )
{
    TRACE( "webview slot 6 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_7( void *t, void *a1 )
{
    TRACE( "webview slot 7 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_8( void *t, void *a1 )
{
    TRACE( "webview slot 8 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_9( void *t, void *a1 )
{
    TRACE( "webview slot 9 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_10( void *t, void *a1 )
{
    TRACE( "webview slot 10 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_11( void *t, void *a1 )
{
    TRACE( "webview slot 11 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_12( void *t, void *a1 )
{
    TRACE( "webview slot 12 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_13( void *t, void *a1 )
{
    TRACE( "webview slot 13 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_14( void *t, void *a1 )
{
    TRACE( "webview slot 14 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_15( void *t, void *a1 )
{
    TRACE( "webview slot 15 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_16( void *t, void *a1 )
{
    TRACE( "webview slot 16 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_17( void *t, void *a1 )
{
    TRACE( "webview slot 17 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_18( void *t, void *a1 )
{
    TRACE( "webview slot 18 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_19( void *t, void *a1 )
{
    TRACE( "webview slot 19 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_20( void *t, void *a1 )
{
    TRACE( "webview slot 20 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_21( void *t, void *a1 )
{
    TRACE( "webview slot 21 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_22( void *t, void *a1 )
{
    TRACE( "webview slot 22 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_23( void *t, void *a1 )
{
    TRACE( "webview slot 23 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_24( void *t, void *a1 )
{
    TRACE( "webview slot 24 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_25( void *t, void *a1 )
{
    TRACE( "webview slot 25 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_26( void *t, void *a1 )
{
    TRACE( "webview slot 26 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_27( void *t, void *a1 )
{
    TRACE( "webview slot 27 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_28( void *t, void *a1 )
{
    TRACE( "webview slot 28 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_29( void *t, void *a1 )
{
    TRACE( "webview slot 29 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_30( void *t, void *a1 )
{
    TRACE( "webview slot 30 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_31( void *t, void *a1 )
{
    TRACE( "webview slot 31 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_32( void *t, void *a1 )
{
    TRACE( "webview slot 32 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_33( void *t, void *a1 )
{
    TRACE( "webview slot 33 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_34( void *t, void *a1 )
{
    TRACE( "webview slot 34 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_35( void *t, void *a1 )
{
    TRACE( "webview slot 35 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_36( void *t, void *a1 )
{
    TRACE( "webview slot 36 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_37( void *t, void *a1 )
{
    TRACE( "webview slot 37 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_38( void *t, void *a1 )
{
    TRACE( "webview slot 38 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_39( void *t, void *a1 )
{
    TRACE( "webview slot 39 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_40( void *t, void *a1 )
{
    TRACE( "webview slot 40 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_41( void *t, void *a1 )
{
    TRACE( "webview slot 41 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_42( void *t, void *a1 )
{
    TRACE( "webview slot 42 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_43( void *t, void *a1 )
{
    TRACE( "webview slot 43 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_44( void *t, void *a1 )
{
    TRACE( "webview slot 44 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_45( void *t, void *a1 )
{
    TRACE( "webview slot 45 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_46( void *t, void *a1 )
{
    TRACE( "webview slot 46 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_47( void *t, void *a1 )
{
    TRACE( "webview slot 47 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_48( void *t, void *a1 )
{
    TRACE( "webview slot 48 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_49( void *t, void *a1 )
{
    TRACE( "webview slot 49 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_50( void *t, void *a1 )
{
    TRACE( "webview slot 50 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_51( void *t, void *a1 )
{
    TRACE( "webview slot 51 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_52( void *t, void *a1 )
{
    TRACE( "webview slot 52 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_53( void *t, void *a1 )
{
    TRACE( "webview slot 53 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_54( void *t, void *a1 )
{
    TRACE( "webview slot 54 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_55( void *t, void *a1 )
{
    TRACE( "webview slot 55 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_56( void *t, void *a1 )
{
    TRACE( "webview slot 56 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_57( void *t, void *a1 )
{
    TRACE( "webview slot 57 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_58( void *t, void *a1 )
{
    TRACE( "webview slot 58 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_59( void *t, void *a1 )
{
    TRACE( "webview slot 59 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_60( void *t, void *a1 )
{
    TRACE( "webview slot 60 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_61( void *t, void *a1 )
{
    TRACE( "webview slot 61 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_62( void *t, void *a1 )
{
    TRACE( "webview slot 62 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static HRESULT WINAPI webview_stub_63( void *t, void *a1 )
{
    TRACE( "webview slot 63 this %p a1 %p\n", t, a1 );
    return S_OK;
}
static const void *webview_stub_table[64] = {
    webview_stub_0,
    webview_stub_1,
    webview_stub_2,
    webview_stub_3,
    webview_stub_4,
    webview_stub_5,
    webview_stub_6,
    webview_stub_7,
    webview_stub_8,
    webview_stub_9,
    webview_stub_10,
    webview_stub_11,
    webview_stub_12,
    webview_stub_13,
    webview_stub_14,
    webview_stub_15,
    webview_stub_16,
    webview_stub_17,
    webview_stub_18,
    webview_stub_19,
    webview_stub_20,
    webview_stub_21,
    webview_stub_22,
    webview_stub_23,
    webview_stub_24,
    webview_stub_25,
    webview_stub_26,
    webview_stub_27,
    webview_stub_28,
    webview_stub_29,
    webview_stub_30,
    webview_stub_31,
    webview_stub_32,
    webview_stub_33,
    webview_stub_34,
    webview_stub_35,
    webview_stub_36,
    webview_stub_37,
    webview_stub_38,
    webview_stub_39,
    webview_stub_40,
    webview_stub_41,
    webview_stub_42,
    webview_stub_43,
    webview_stub_44,
    webview_stub_45,
    webview_stub_46,
    webview_stub_47,
    webview_stub_48,
    webview_stub_49,
    webview_stub_50,
    webview_stub_51,
    webview_stub_52,
    webview_stub_53,
    webview_stub_54,
    webview_stub_55,
    webview_stub_56,
    webview_stub_57,
    webview_stub_58,
    webview_stub_59,
    webview_stub_60,
    webview_stub_61,
    webview_stub_62,
    webview_stub_63
};

#define WV_ADD(name, field) static HRESULT WINAPI name( void *t, void *handler, UINT64 *token ) \
{ \
    struct webview *w = t; \
    ERR( #name " %p handler %p\n", w, handler ); \
    w->field = handler; \
    if (token) *token = InterlockedIncrement( &w->token ); \
    return S_OK; \
}
#define WV_REM(name, field) static HRESULT WINAPI name( void *t, UINT64 token ) \
{ \
    struct webview *w = t; \
    w->field = NULL; \
    return S_OK; \
}
WV_ADD( webview_add_NavigationStarting, h_nav_starting )
WV_ADD( webview_add_DOMContentLoaded, h_dom_loaded )
WV_ADD( webview_add_NavigationCompleted, h_nav_completed )
WV_ADD( webview_add_SourceChanged, h_source_changed )
WV_ADD( webview_add_PermissionRequested, h_permission )
WV_REM( webview_remove_NavigationStarting, h_nav_starting )
WV_REM( webview_remove_DOMContentLoaded, h_dom_loaded )
WV_REM( webview_remove_NavigationCompleted, h_nav_completed )
WV_REM( webview_remove_SourceChanged, h_source_changed )
WV_REM( webview_remove_PermissionRequested, h_permission )

/* Shared WinRT event-args facade: OfficeHub invokes delegates with
 * (webview, args) and reads args vtable slot 7. Dual-face layout:
 * Win32-face body 3.. and WinRT-face body 6.. overlap (get_Uri at 3 and 6,
 * get_IsSuccess at 3-completed/7-completed etc). */
struct event_args
{
    const void **vtbl;
    LPWSTR uri;
};
static HRESULT WINAPI event_args_QI( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}
static HRESULT WINAPI event_args_get_Uri( void *t, LPWSTR *v )
{
    struct event_args *a = t;
    if (v) *v = dup_wstr( a->uri );
    return S_OK;
}
static HRESULT WINAPI event_args_get_IsSuccess( void *t, BOOL *v )
{
    if (v) *v = TRUE;
    return S_OK;
}
static HRESULT WINAPI event_args_get_id( void *t, UINT64 *v )
{
    if (v) *v = 1;
    return S_OK;
}
static struct event_args *create_event_args( LPCWSTR uri )
{
    static struct event_args *a;
    const void **vt;
    unsigned int i;

    if (a) return a;
    if (!(a = HeapAlloc( GetProcessHeap(), 0, sizeof(*a) ))) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 32 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, a );
        return NULL;
    }
    for (i = 0; i < 32; i++) vt[i] = webview_stub;
    vt[0] = event_args_QI;
    vt[3] = event_args_get_Uri;         /* NavigationStarting.get_Uri / DOMContentLoaded.get_Uri */
    vt[4] = event_args_get_IsSuccess;   /* get_IsUserInitiated / Completed.get_NavigationId */
    vt[5] = event_args_get_id;
    vt[6] = event_args_get_Uri;         /* WinRT-face get_Uri */
    vt[7] = event_args_get_IsSuccess;   /* WinRT-face get_IsSuccess */
    a->vtbl = vt;
    a->uri = dup_wstr( uri ? uri : L"" );
    return a;
}

static void fire_handler( void *handler, void *sender, void *args );

static HRESULT WINAPI webview_get_DocumentTitle( void *t, LPWSTR *v )
{
    if (v) *v = dup_wstr( L"" );
    return S_OK;
}
static HRESULT WINAPI webview_NavigateToString( void *t, LPCWSTR html )
{
    struct webview *w = t;
    CoTaskMemFree( w->source );
    w->source = dup_wstr( L"about:blank" );
    return S_OK;
}

static struct webview *create_webview( struct controller *c )
{
    struct webview *w = HeapAlloc( GetProcessHeap(), 0, sizeof(*w) );
    const void **vt;
    unsigned int i;
    if (!w) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 64 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, w );
        return NULL;
    }
    for (i = 0; i < 64; i++) vt[i] = webview_stub_table[i];
    /* ICoreWebView2 Win32 face (WRL clients: OfficeHub, msedgewebview2). */
    vt[0] = webview_QueryInterface;
    vt[1] = webview_AddRef;
    vt[2] = webview_Release;
    vt[3] = webview_Navigate;
    vt[4] = webview_NavigateToString;
    vt[7] = webview_get_DocumentTitle;
    vt[8] = webview_add_NavigationStarting;
    vt[9] = webview_remove_NavigationStarting;
    vt[12] = webview_add_NavigationCompleted;
    vt[13] = webview_remove_NavigationCompleted;
    vt[14] = webview_add_DOMContentLoaded;
    vt[15] = webview_remove_DOMContentLoaded;
    vt[16] = webview_add_SourceChanged;
    vt[17] = webview_remove_SourceChanged;
    vt[22] = webview_add_PermissionRequested;
    vt[23] = webview_remove_PermissionRequested;
    vt[24] = webview_get_Source;
    vt[25] = webview_put_Source;
    /* WinRT face (IInspectable prologue then the same body): keep the
     * dual-mapped entries at their shifted positions. */
    vt[6] = webview_Navigate;
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

    WARN( "GET_COREWEBVIEW2 controller %p out %p\n", c, value );
    if (!(w = create_webview( c ))) return E_OUTOFMEMORY;
    /* OfficeHub calls the WinRT-face slot with a null out pointer; the
     * webview is then (re)fetched via a slot that does take one, so keep
     * the object alive for that caller instead of crashing. */
    if (value) *value = w;
    else w->ref++;
    return S_OK;
}

/* ICoreWebView2CreateCoreWebView2ControllerCompletedEventArgs (Win32 face):
 * the app's Invoke QIs the second argument and calls get_Controller (slot 3)
 * / get_CoreWebView2 (slot 4) on it. Handing out the raw controller as the
 * args (the WhatsApp smoke shape) makes OfficeHub's handler read its
 * vtbl[3] = get_IsVisible placeholder as a controller pointer, and the next
 * call through that QIs a null member. A real args object serves both. */
struct create_args
{
    const void **vtbl;
    struct controller *controller;
    struct webview *webview;
};

static HRESULT WINAPI create_args_QueryInterface( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}
static ULONG WINAPI create_args_AddRef( void *t ) { return 2; }
static ULONG WINAPI create_args_Release( void *t ) { return 1; }
static HRESULT WINAPI create_args_get_Controller( void *t, void **v )
{
    struct create_args *a = t;
    *v = a->controller;
    return S_OK;
}
static HRESULT WINAPI create_args_get_CoreWebView2( void *t, void **v )
{
    struct create_args *a = t;
    return controller_get_CoreWebView2( a->controller, v );
}

static struct create_args *create_create_args( struct controller *c )
{
    struct create_args *a = HeapAlloc( GetProcessHeap(), 0, sizeof(*a) );
    const void **vt;
    unsigned int i;
    if (!a) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 64 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, a );
        return NULL;
    }
    for (i = 0; i < 64; i++) vt[i] = webview_stub_table[i];
    /* Win32 face: get_Controller=3, get_CoreWebView2=4;
     * WinRT face (prologue through 5): 6 and 7. */
    vt[0] = create_args_QueryInterface;
    vt[1] = create_args_AddRef;
    vt[2] = create_args_Release;
    vt[3] = create_args_get_Controller;
    vt[4] = create_args_get_CoreWebView2;
    vt[6] = create_args_get_Controller;
    vt[7] = create_args_get_CoreWebView2;
    /* Windows.Web.UI.Interop (UWP WebViewControl) projection: OfficeHub
     * reads the controller off the completion args at slots 21/25. */
    vt[21] = create_args_get_Controller;
    vt[25] = create_args_get_Controller;
    a->vtbl = vt;
    a->controller = c;
    a->webview = NULL;
    return a;
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
static HRESULT post_completion( void *handler, void *sender, void *object, int deref )
{
    struct completion_ctx *ctx;
    HWND hwnd;

    if (!handler) return E_POINTER;

    /* s310: invoke synchronously. All loader calls happen on the UI thread,
     * and the app's ShowWindow cycle overflow (unwinding the message loop
     * mid-pump) drops the posted WM_APP_COMPLETION: the env/controller chain
     * stalls with the browser spawned and the mojo pipe never opened.
     *
     * Handler shape depends on the client: the msedgewebview2 child hands us
     * the raw vtable (deref 0), a UWP app's C++/WinRT callback object hands
     * us a COM object whose first field is the vptr (deref 1 from
     * CreateCoreWebView2EnvironmentWithOptions; OfficeHub's
     * CreateCoreWebView2Controller handler arrives deref 0 in COM shape
     * too). Probe slot 3 of the raw interpretation: if it is not a code
     * pointer, take the vptr one level deeper. */
    {
        void **vt = deref ? *(void ***)handler : handler;
        void *invoke_slot;
        HRESULT (WINAPI *invoke)( void *, void *, void * );
        if (!deref && IsBadCodePtr( (FARPROC)vt[3] )) vt = *(void ***)handler;
        /* Invoke lives at slot 3 of the B-shape table and at slot 6 of the
         * WinRT-face delegate (IInspectable prologue through 5, as used by
         * C++/WinRT apps like OfficeHub). Prefer slot 6 when it is code.
         * TypedEventHandler::Invoke(this, sender, args): sender is the
         * environment; the B-shape second operand is an HRESULT, and any
         * non-null environment pointer reads as S_OK there. */
        invoke_slot = (!deref && !IsBadCodePtr( (FARPROC)vt[6] )) ? vt[6] : vt[3];
        invoke = (void *)invoke_slot;
        ERR( "post_completion handler %p sender %p object %p deref %d vt %p invoke %p\n",
             handler, sender, object, deref, vt, invoke_slot );
        return invoke( handler, sender, object );
    }
#if 0
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
#endif
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
    ERR( "ctrl get_IsVisible %p out %p\n", c, v );
    if (v) *v = c->visible;
    return S_OK;
}
static HRESULT WINAPI controller_put_IsVisible( void *t, BOOL v )
{
    struct controller *c = t;
    ERR( "ctrl put_IsVisible %p v %d\n", c, v );
    c->visible = v;
    if (!c->child && c->parent && IsWindow( c->parent ))
        EnumChildWindows( c->parent, loader_find_child_cb, (LPARAM)&c->child );
    if (!c->child)
        EnumWindows( loader_find_child_cb, (LPARAM)&c->child );
    if (c->child)
    {
        if (c->parent && IsWindow( c->parent ) && GetParent( c->child ) != c->parent)
            SetParent( c->child, c->parent );
        ShowWindow( c->child, v ? SW_SHOW : SW_HIDE );
        SetWindowPos( c->child, HWND_TOP, 0, 0, 1024, 768, SWP_SHOWWINDOW | SWP_NOACTIVATE );
    }
    return S_OK;
}

/* WinRT-face ICoreWebView2Controller tail used by OfficeHub:
 * slot 21 get_ContainingBrowserWindow (ICoreWebView2),
 * slot 25 add_FrameNavigationStarting (event registration with an
 * event-token out). */
static HRESULT WINAPI controller_get_ContainingBrowserWindow( void *t, void **value )
{
    struct controller *c = t;
    struct webview *w;
    ERR( "ctrl get_ContainingBrowserWindow %p out %p\n", c, value );
    if (value)
    {
        if ((w = create_webview( c ))) *value = w;
        else return E_OUTOFMEMORY;
    }
    return S_OK;
}
static HRESULT WINAPI controller_add_FrameNavigationStarting( void *t, void *handler, UINT64 *token )
{
    struct controller *c = t;
    ERR( "ctrl add_FrameNavigationStarting %p handler %p\n", c, handler );
    c->h_frame_nav = handler;
    if (token) *token = InterlockedIncrement( &c->token );
    return S_OK;
}

static HRESULT WINAPI controller_get_Bounds( void *t, RECT *rect )
{
    struct controller *c = t;
    ERR( "ctrl get_Bounds %p out %p\n", c, rect );
    if (rect) *rect = c->bounds;
    return S_OK;
}
/* RECT (16 bytes) is passed in two registers on x86_64: left/top in edx,
 * right/bottom in r8d. */
static HRESULT WINAPI controller_put_Bounds( void *t, LONG left, LONG top, LONG right, LONG bottom )
{
    struct controller *c = t;
    c->bounds.left = left;
    c->bounds.top = top;
    c->bounds.right = right;
    c->bounds.bottom = bottom;
    ERR( "ctrl put_Bounds %p %ld,%ld %ldx%ld\n", c, left, top, right - left, bottom - top );
    return S_OK;
}

/* Heap vtbl filled with per-slot stores (RIP-relative immediates): static
 * .rdata vtbl arrays go stale because the shared pages are relocated once,
 * by whichever process (app or msedgewebview2 child) maps them first.
 *
 * Two consumption shapes exist for ICoreWebView2Controller:
 *  - the WhatsApp bridge (raw handler, 3-slot prologue): IsVisible get/put
 *    at 3/4, Bounds get/put at 5/6, CoreWebView2 get at 7;
 *  - a C++/WinRT UWP app (OfficeHub): IInspectable prologue through 5,
 *    IsVisible get/put at 6/7, Bounds get/put at 8/9, CoreWebView2 get at
 *    10, then the v1 body (ResetBrightness, DefaultBackgroundColor, zoom,
 *    NewWindowRequested, Navigation* events) through ~40.
 * One table serves both: 3-7 keep the B-shape slots, 8-10 mirror them in
 * WinRT-face positions, 11-40 are S_OK stubs. */
static struct controller *create_controller( HWND parent )
{
    struct controller *c = HeapAlloc( GetProcessHeap(), 0, sizeof(*c) );
    const void **vt;
    unsigned int i;
    if (!c) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 64 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, c );
        return NULL;
    }
    for (i = 0; i < 64; i++) vt[i] = webview_stub;
    vt[0] = controller_QueryInterface;
    vt[1] = controller_AddRef;
    vt[2] = controller_Release;
    vt[3] = controller_get_IsVisible;
    vt[4] = controller_put_IsVisible;
    vt[5] = controller_get_IsVisible;   /* get_Bounds placeholder (B-shape) */
    vt[6] = controller_put_IsVisible;   /* put_Bounds placeholder (B-shape) */
    vt[7] = controller_get_CoreWebView2;
    vt[5] = controller_get_Bounds;      /* B-shape get_Bounds */
    vt[6] = controller_put_Bounds;      /* B-shape put_Bounds */
    vt[8] = controller_get_Bounds;      /* WinRT-face Bounds get */
    vt[9] = controller_put_Bounds;      /* WinRT-face Bounds put */
    vt[10] = controller_get_CoreWebView2;
    vt[21] = controller_get_ContainingBrowserWindow;
    vt[25] = controller_add_FrameNavigationStarting;
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

        udd = user_data_dir && *user_data_dir ? user_data_dir : L"EBWebView\\x64";
        exe[0] = 0;
        if (GetModuleFileNameW( NULL, module_path, MAX_PATH ))
        {
            slash = wcsrchr( module_path, '\\' );
            if (slash)
            {
                *slash = 0;
                lstrcpyW( exe, module_path );
                lstrcatW( exe, L"\\msedgewebview2.exe" );
                *slash = '\\';
                exe_name = slash + 1;
            }
            else exe_name = module_path;
        }
        if (!exe[0] || GetFileAttributesW( exe ) == INVALID_FILE_ATTRIBUTES)
        {
            GetCurrentDirectoryW( MAX_PATH, root );
            lstrcpyW( exe, root );
            lstrcatW( exe, L"\\msedgewebview2.exe" );
        }

        g_mojo.pipe = CreateNamedPipeW( g_mojo.name,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1, 4096, 4096, 0, NULL );
        if (g_mojo.pipe == INVALID_HANDLE_VALUE)
        {
            WARN( "create-pipe %s gle=%u\n", debugstr_w( g_mojo.name ), GetLastError());
            return;
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
    struct create_args *a;

    ERR( "env %p parent %p handler %p self-slot %p\n", e, parent, handler,
         ((const void **)e->vtbl)[3] );

    (void)a;
    if (!(c = create_controller( parent ))) return E_OUTOFMEMORY;
    /* OfficeHub is a WRL (Win32-face) client: the controller-completed
     * delegate is Invoke(this, HRESULT status, ICoreWebView2Controller*),
     * i.e. the controller pointer straight through as the second operand,
     * not a WinRT-face event args object. */
    return post_completion( handler, (void *)(INT_PTR)S_OK, c, 0 );
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
    unsigned int i;
    if (!e) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 32 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, e );
        return NULL;
    }
    for (i = 0; i < 32; i++) vt[i] = webview_stub;
    /* per-slot stores: RIP-relative immediates, immune to the shared .rdata
     * pages being relocated once (app vs msedgewebview2 child image base).
     * Slots 3-4 serve the B-shape (WhatsApp) face; slots 5+ are the WinRT
     * face tail (BrowserVersion/GetOptions/events) for C++/WinRT apps:
     * OfficeHub calls get_BrowserVersion at 5 and GetOptions at 6. */
    vt[0] = environment_QueryInterface;
    vt[1] = environment_AddRef;
    vt[2] = environment_Release;
    vt[3] = environment_CreateCoreWebView2Controller;
    vt[4] = environment_get_BrowserVersion;
    vt[5] = environment_get_BrowserVersion;
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
    hr = post_completion( handler, e, e, 1 );
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
    unsigned int i;
    if (!o) return E_OUTOFMEMORY;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 24 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, o );
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < 24; i++) vt[i] = webview_stub;
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

/* s380: vtbl slots of the env/controller objects live in shared builtin
 * pages relocated once (app vs msedgewebview2 child image base); a stale slot
 * dispatch faults. This export lets callers fetch the CoreWebView2 without
 * reading controller vtbl[7]. */
HRESULT WINAPI GetCoreWebView2FromController( void *controller, void **core )
{
    return controller_get_CoreWebView2( controller, core );
}

/* s430: synchronous environment -> controller -> core chain in one export
 * call. The async handler completions dispatch through vtbl slots that have
 * been observed to read back as child image-base VAs (r35-r41: heap-filled
 * vtbl slots clobbered mid-chain), so the env handler calls this and gets the
 * CoreWebView2 directly, with no handler vtbl hops at all. */
void *WINAPI CreateCoreWebView2ControllerSync( HWND parent, HRESULT *phr )
{
    struct environment *e;
    struct controller *c;
    void *core = NULL;
    HRESULT hr;

    if (!(e = create_environment( NULL ))) { if (phr) *phr = E_OUTOFMEMORY; return NULL; }
    if (!(c = create_controller( parent ))) { if (phr) *phr = E_OUTOFMEMORY; return NULL; }
    hr = controller_get_CoreWebView2( c, &core );
    if (phr) *phr = hr;
    return core;
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
