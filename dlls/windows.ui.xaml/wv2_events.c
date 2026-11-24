/* WinRT Windows.UI.Xaml.Controls.WebView2 bridge support
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

#include <stdarg.h>

#include "private.h"
#include "weakreference.h"

#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);
static const GUID IID_ICoreWebView2HttpRequestHeaders_rt =
    { 0xdc2226c7, 0x3515, 0x55bb, { 0xbc, 0xb2, 0x57, 0xb7, 0x8f, 0x86, 0xb9, 0x1d } };
static const GUID IID_ICoreWebView2WebResourceRequest_rt =
    { 0x5c742259, 0x67d2, 0x5df2, { 0x83, 0x82, 0x0f, 0x20, 0x1b, 0x4d, 0x71, 0x97 } };
static const GUID IID_ICoreWebView2Deferral_rt =
    { 0x8d5284e5, 0x3376, 0x5c3c, { 0x9c, 0x05, 0x3d, 0x85, 0x3f, 0x99, 0x5b, 0x3a } };
static const GUID IID_ICoreWebView2ProcessFailedEventArgs_rt =
    { 0x25a8f8c9, 0xd944, 0x539d, { 0xaf, 0xa3, 0x24, 0x17, 0x2b, 0x48, 0xef, 0x47 } };
#include "uwp_private.h"
#include "wv2_private.h"

struct cwv2_event_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    GUID winrt_iid;
    void *com_args;
};

static inline struct cwv2_event_args *impl_from_evargs( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_event_args, IInspectable_iface );
}
static HRESULT WINAPI evargs_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->winrt_iid ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    ERR( "evargs_QueryInterface: unknown iid %s (winrt=%s)\n", debugstr_guid( iid ), debugstr_guid( &impl->winrt_iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI evargs_AddRef( IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_evargs( iface )->ref );
}
static ULONG WINAPI evargs_Release( IInspectable *iface )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->com_args) ((IUnknown *)impl->com_args)->lpVtbl->Release( impl->com_args );
        free( impl );
    }
    return r;
}

static HRESULT evargs_get_str( struct cwv2_event_args *impl, int com_slot, HSTRING *out )
{
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, LPWSTR * ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            LPWSTR raw = NULL;
            HRESULT hr = fn( impl->com_args, &raw );
            if (SUCCEEDED( hr ) && raw)
            {
                hr = WindowsCreateString( raw, lstrlenW( raw ), out );
                CoTaskMemFree( raw );
                return hr;
            }
            if (raw) CoTaskMemFree( raw );
            return hr;
        }
    }
    return WindowsCreateString( L"", 0, out );
}
static HRESULT evargs_get_bool( struct cwv2_event_args *impl, int com_slot, boolean *out )
{
    BOOL b = FALSE;
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->com_args, &b );
            *out = !!b;
            return hr;
        }
    }
    *out = FALSE;
    return S_OK;
}
static HRESULT evargs_put_bool( struct cwv2_event_args *impl, int com_slot, boolean val )
{
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, BOOL ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args, !!val );
    }
    return S_OK;
}
static HRESULT evargs_get_u64( struct cwv2_event_args *impl, int com_slot, UINT64 *out )
{
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, UINT64 * ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args, out );
    }
    *out = 1;
    return S_OK;
}

/* NavigationStartingEventArgs: 6=get_Uri, 7=get_IsUserInitiated, 8=get_IsRedirected,
 * 9=get_RequestHeaders, 10=get_Cancel, 11=put_Cancel, 12=get_NavigationId (COM 3..9) */
static HRESULT WINAPI navstart_get_Uri( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 3, out ); }
static HRESULT WINAPI navstart_get_IsUserInitiated( void *iface, boolean *out )
{ return evargs_get_bool( impl_from_evargs( iface ), 4, out ); }
static HRESULT WINAPI navstart_get_IsRedirected( void *iface, boolean *out )
{ return evargs_get_bool( impl_from_evargs( iface ), 5, out ); }
static HRESULT WINAPI navstart_get_Cancel( void *iface, boolean *out )
{ return evargs_get_bool( impl_from_evargs( iface ), 7, out ); }
static HRESULT WINAPI navstart_put_Cancel( void *iface, boolean val )
{ return evargs_put_bool( impl_from_evargs( iface ), 8, val ); }
static HRESULT WINAPI navstart_get_NavigationId( void *iface, UINT64 *out )
{ return evargs_get_u64( impl_from_evargs( iface ), 9, out ); }

static const void *cwv2_navstart_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [9] = element_generic_slot,

    [13 ... 255] = element_generic_slot,
    [6] = navstart_get_Uri, [7] = navstart_get_IsUserInitiated, [8] = navstart_get_IsRedirected,
    [10] = navstart_get_Cancel, [11] = navstart_put_Cancel, [12] = navstart_get_NavigationId,
};

/* NavigationCompletedEventArgs: 6=get_IsSuccess, 7=get_WebErrorStatus, 8=get_NavigationId (COM 3..5) */
static HRESULT WINAPI navcomp_get_IsSuccess( void *iface, boolean *out )
{
    HRESULT hr = evargs_get_bool( impl_from_evargs( iface ), 3, out );
    ERR( "navcomp_get_IsSuccess hr=%#lx success=%d\n", hr, out ? *out : -1 );
    return hr;
}
static HRESULT WINAPI navcomp_get_WebErrorStatus( void *iface, int *out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, int * ) = (*(void ***)impl->com_args)[4];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000) return fn( impl->com_args, out );
    }
    *out = 0;
    return S_OK;
}
static HRESULT WINAPI navcomp_get_NavigationId( void *iface, UINT64 *out )
{ return evargs_get_u64( impl_from_evargs( iface ), 5, out ); }

static const void *cwv2_navcomp_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [9 ... 255] = element_generic_slot,
    [6] = navcomp_get_IsSuccess, [7] = navcomp_get_WebErrorStatus, [8] = navcomp_get_NavigationId,
};

/* WebMessageReceivedEventArgs: 6=get_Source, 7=get_WebMessageAsJson, 8=TryGetWebMessageAsString (COM 3..5) */
static HRESULT WINAPI webmsg_get_Source( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 3, out ); }
static HRESULT WINAPI webmsg_get_WebMessageAsJson( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 4, out ); }
static HRESULT WINAPI webmsg_TryGetWebMessageAsString( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 5, out ); }

static const void *cwv2_webmsg_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [9 ... 255] = element_generic_slot,
    [6] = webmsg_get_Source, [7] = webmsg_get_WebMessageAsJson, [8] = webmsg_TryGetWebMessageAsString,
};

/* DOMContentLoadedEventArgs: 6=get_NavigationId (COM 3) */
static HRESULT WINAPI domloaded_get_NavigationId( void *iface, UINT64 *out )
{ return evargs_get_u64( impl_from_evargs( iface ), 3, out ); }

static const void *cwv2_domloaded_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [7 ... 255] = element_generic_slot,
    [6] = domloaded_get_NavigationId,
};

/* WebResourceRequest: 6=get_Uri (COM 3), 8=get_Method (COM 5) */

static HRESULT WINAPI wrr_req_get_Uri( void *iface, HSTRING *out )
{
    HRESULT hr = evargs_get_str( impl_from_evargs( iface ), 3, out );
    ERR( "wrr_req_get_Uri hr=%#lx uri=%s\n", hr, out ? debugstr_hstring( *out ) : "(null)" );
    return hr;
}
static HRESULT WINAPI wrr_req_get_Method( void *iface, HSTRING *out )
{
    HRESULT hr = evargs_get_str( impl_from_evargs( iface ), 5, out );
    ERR( "wrr_req_get_Method hr=%#lx\n", hr );
    return hr;
}
static HRESULT WINAPI wrr_req_get_Content( void *iface, void **out )
{
    ERR( "wrr_req_get_Content\n" );
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI wrr_headers_GetHeader( void *iface, HSTRING name, HSTRING *out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    const WCHAR *n = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    LPWSTR raw = NULL;
    HRESULT hr = E_FAIL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, LPWSTR * ) = (*(void ***)impl->com_args)[3];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            hr = fn( impl->com_args, n, &raw );
    }
    if (SUCCEEDED( hr ) && raw)
    {
        hr = WindowsCreateString( raw, lstrlenW( raw ), out );
        CoTaskMemFree( raw );
        return hr;
    }
    if (raw) CoTaskMemFree( raw );
    return hr;
}
static HRESULT WINAPI wrr_headers_Contains( void *iface, HSTRING name, boolean *out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    const WCHAR *n = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    BOOL b = FALSE;
    HRESULT hr = S_OK;
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, BOOL * ) = (*(void ***)impl->com_args)[5];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            hr = fn( impl->com_args, n, &b );
    }
    *out = !!b;
    return hr;
}
static HRESULT WINAPI wrr_headers_SetHeader( void *iface, HSTRING name, HSTRING val )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    const WCHAR *n = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    const WCHAR *v = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    ERR( "wrr_headers_SetHeader %s=%s\n", debugstr_w( n ), debugstr_w( v ) );
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, LPCWSTR ) = (*(void ***)impl->com_args)[6];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args, n, v );
    }
    return S_OK;
}
static HRESULT WINAPI wrr_headers_RemoveHeader( void *iface, HSTRING name )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    const WCHAR *n = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = (*(void ***)impl->com_args)[7];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args, n );
    }
    return S_OK;
}
static const void *cwv2_wrr_headers_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [7] = element_generic_slot,

    [11 ... 255] = element_generic_slot,
    [6] = wrr_headers_GetHeader,
    [8] = wrr_headers_Contains,
    [9] = wrr_headers_SetHeader,
    [10] = wrr_headers_RemoveHeader,
};
static HRESULT WINAPI wrr_req_get_Headers( void *iface, void **out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    void *com_hdr = NULL;
    struct cwv2_event_args *wrap;
    HRESULT hr = E_FAIL;

    if (!out) return E_POINTER;
    *out = NULL;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, void ** ) = (*(void ***)impl->com_args)[9];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            hr = fn( impl->com_args, &com_hdr );
    }
    ERR( "wrr_req_get_Headers hr=%#lx com_hdr=%p\n", hr, com_hdr );
    if (FAILED( hr ) || !com_hdr) return SUCCEEDED( hr ) ? E_FAIL : hr;
    wrap = calloc( 1, sizeof(*wrap) );
    if (!wrap)
    {
        ((IUnknown *)com_hdr)->lpVtbl->Release( com_hdr );
        return E_OUTOFMEMORY;
    }
    wrap->IInspectable_iface.lpVtbl = (const IInspectableVtbl *)cwv2_wrr_headers_vtbl;
    wrap->ref = 1;
    wrap->winrt_iid = IID_ICoreWebView2HttpRequestHeaders_rt;
    wrap->com_args = com_hdr;
    *out = &wrap->IInspectable_iface;
    return S_OK;
}

static const void *cwv2_wrr_req_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [7] = element_generic_slot,

    [9] = element_generic_slot,

    [11] = element_generic_slot,

    [13 ... 255] = element_generic_slot,
    [6] = wrr_req_get_Uri,
    [8] = wrr_req_get_Method,
    [10] = wrr_req_get_Content,
    [12] = wrr_req_get_Headers,
};

/* WebResourceRequestedEventArgs: 6=get_Request (COM 3), 7=get_Response (COM 4),
 * 8=put_Response (COM 5), 9=GetDeferral (COM 6), 10=get_ResourceContext (COM 7) */
static HRESULT WINAPI wrr_evargs_get_Request( void *iface, void **out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    void *com_req = NULL;
    struct cwv2_event_args *wrap;
    HRESULT hr = E_FAIL;

    if (!out) return E_POINTER;
    *out = NULL;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, void ** ) = (*(void ***)impl->com_args)[3];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            hr = fn( impl->com_args, &com_req );
    }
    if (FAILED( hr ) || !com_req) return SUCCEEDED( hr ) ? E_FAIL : hr;
    wrap = calloc( 1, sizeof(*wrap) );
    if (!wrap)
    {
        ((IUnknown *)com_req)->lpVtbl->Release( com_req );
        return E_OUTOFMEMORY;
    }
    wrap->IInspectable_iface.lpVtbl = (const IInspectableVtbl *)cwv2_wrr_req_vtbl;
    wrap->ref = 1;
    wrap->winrt_iid = IID_ICoreWebView2WebResourceRequest_rt;
    wrap->com_args = com_req;
    *out = &wrap->IInspectable_iface;
    ERR( "wrr_evargs_get_Request -> %p (com=%p)\n", *out, com_req );
    return S_OK;
}
static HRESULT WINAPI wrr_evargs_get_Response( void *iface, void **out )
{
    ERR( "wrr_evargs_get_Response\n" );
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI wrr_evargs_put_Response( void *iface, void *resp )
{
    ERR( "wrr_evargs_put_Response resp=%p\n", resp );
    return S_OK;
}
static HRESULT WINAPI wrr_deferral_Complete( void *iface )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    ERR( "wrr_deferral_Complete com=%p\n", impl->com_args );
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void * ) = (*(void ***)impl->com_args)[3];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args );
    }
    return S_OK;
}
static const void *cwv2_deferral_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [7 ... 255] = element_generic_slot,
    [6] = wrr_deferral_Complete,
};
static HRESULT WINAPI wrr_evargs_GetDeferral( void *iface, void **out )
{
    struct cwv2_event_args *wrap;

    if (!out) return E_POINTER;
    *out = NULL;
    /* Diagnostic: do not hold browser-side deferral open if app never calls Complete() */
    ERR( "wrr_evargs_GetDeferral (non-blocking)\n" );
    wrap = calloc( 1, sizeof(*wrap) );
    if (!wrap) return E_OUTOFMEMORY;
    wrap->IInspectable_iface.lpVtbl = (const IInspectableVtbl *)cwv2_deferral_vtbl;
    wrap->ref = 1;
    wrap->winrt_iid = IID_ICoreWebView2Deferral_rt;
    wrap->com_args = NULL;
    *out = &wrap->IInspectable_iface;
    return S_OK;
}
static HRESULT WINAPI wrr_evargs_get_ResourceContext( void *iface, int *out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    ERR( "wrr_evargs_get_ResourceContext iface=%p out=%p\n", iface, out );
    if (!out) return E_POINTER;
    *out = 0;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, int * ) = (*(void ***)impl->com_args)[7];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args, out );
    }
    return S_OK;
}

static const void *cwv2_wrr_evargs_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [11 ... 255] = element_generic_slot,
    [6] = wrr_evargs_get_Request,
    [7] = wrr_evargs_get_Response,
    [8] = wrr_evargs_put_Response,
    [9] = wrr_evargs_GetDeferral,
    [10] = wrr_evargs_get_ResourceContext,
};

/* ProcessFailedEventArgs: 6=get_ProcessFailedKind (COM 3) */

static HRESULT WINAPI procfail_get_ProcessFailedKind( void *iface, int *out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    HRESULT hr = S_OK;
    if (!out) return E_POINTER;
    *out = 0;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, int * ) = (*(void ***)impl->com_args)[3];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            hr = fn( impl->com_args, out );
    }
    ERR( "procfail_get_ProcessFailedKind hr=%#lx kind=%d\n", hr, *out );
    return hr;
}

static const void *cwv2_procfail_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [7 ... 255] = element_generic_slot,
    [6] = procfail_get_ProcessFailedKind,
};

/* Generic EventArgs fallback (ContextMenuRequested) */
static const void *cwv2_generic_evargs_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
};

/* COM event handler -> WinRT TypedEventHandler<CoreWebView2, TArgs> bridge */
struct cwv2_event_bridge
{
    const void **vtbl;
    LONG ref;
    struct core_webview2 *owner;
    IInspectable *winrt_handler;
    const void **args_vtbl;
    GUID args_iid;
};

static HRESULT WINAPI evbridge_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = t;
    InterlockedIncrement( &((struct cwv2_event_bridge *)t)->ref );
    return S_OK;
}
static ULONG WINAPI evbridge_AddRef( void *t )
{
    return InterlockedIncrement( &((struct cwv2_event_bridge *)t)->ref );
}
static ULONG WINAPI evbridge_Release( void *t )
{
    struct cwv2_event_bridge *b = t;
    ULONG r = InterlockedDecrement( &b->ref );
    if (!r)
    {
        if (b->winrt_handler) IInspectable_Release( b->winrt_handler );
        if (b->owner) cwv2_Release( &b->owner->IInspectable_iface );
        HeapFree( GetProcessHeap(), 0, (void *)b->vtbl );
        HeapFree( GetProcessHeap(), 0, b );
    }
    return r;
}
static HRESULT WINAPI evbridge_Invoke( void *t, void *com_sender, void *com_args )
{
    struct cwv2_event_bridge *b = t;
    struct cwv2_typed_handler *wh = (struct cwv2_typed_handler *)b->winrt_handler;
    struct cwv2_event_args *ea = NULL;
    HRESULT hr = S_OK;
    if (!wh || !b->owner) return S_OK;
    ERR( "evbridge_Invoke: slot=%d iid=%s com_args=%p\n", 0, debugstr_guid( &b->args_iid ), com_args );
    if (com_args && (ea = calloc( 1, sizeof(*ea) )))
    {
        ea->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( b->args_vtbl ? b->args_vtbl : cwv2_generic_evargs_vtbl );
        ea->ref = 1;
        ea->winrt_iid = b->args_iid;
        ea->com_args = com_args;
        ((IUnknown *)com_args)->lpVtbl->AddRef( com_args );
    }
    hr = wh->lpVtbl->Invoke( wh, &b->owner->IInspectable_iface, ea ? &ea->IInspectable_iface : NULL );
    ERR( "evbridge_Invoke: done iid=%s hr=%#lx\n", debugstr_guid( &b->args_iid ), hr );
    if (ea) evargs_Release( &ea->IInspectable_iface );
    return hr;
}

HRESULT cwv2_forward_add_event( struct core_webview2 *impl, void *com_obj, int com_slot,
        void *winrt_handler, EventRegistrationToken *token, const void **args_vtbl, const GUID *args_iid )
{
    static LONG fallback_tok = 100;
    if (token) token->value = InterlockedIncrement( &fallback_tok );
    if (com_obj && impl->real_loader && winrt_handler)
    {
        HRESULT (WINAPI *fn)( void *, void *, EventRegistrationToken * ) = wv2_slot_fn( com_obj, TRUE, com_slot, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            struct cwv2_event_bridge *b = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b) );
            const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
            HRESULT hr;
            if (!b || !vt) { HeapFree( GetProcessHeap(), 0, b ); HeapFree( GetProcessHeap(), 0, vt ); return E_OUTOFMEMORY; }
            vt[0] = evbridge_QueryInterface;
            vt[1] = evbridge_AddRef;
            vt[2] = evbridge_Release;
            vt[3] = evbridge_Invoke;
            b->vtbl = vt;
            b->ref = 1;
            b->owner = impl;
            cwv2_AddRef( &impl->IInspectable_iface );
            b->winrt_handler = (IInspectable *)winrt_handler;
            IInspectable_AddRef( b->winrt_handler );
            b->args_vtbl = args_vtbl;
            if (args_iid) b->args_iid = *args_iid;
            hr = fn( com_obj, b, token );
            TRACE( "cwv2_forward_add_event slot=%d com_obj=%p hr=%#lx\n", com_slot, com_obj, hr );
            evbridge_Release( b );
            return hr;
        }
    }
    if (!com_obj && winrt_handler)
    {
        enum cwv2_pending_kind ekind = (com_slot == 61 || com_slot == 64) ? CWV2_OP_ADD_EVENT2 :
                                       (com_slot == 100) ? CWV2_OP_ADD_EVENT11 : CWV2_OP_ADD_EVENT;
        cwv2_queue_op( impl, ekind, com_slot, FALSE, NULL, NULL, winrt_handler, args_vtbl, args_iid );
    }
    return S_OK;
}

static HRESULT cwv2_forward_remove_event( struct core_webview2 *impl, void *com_obj, int com_slot,
        EventRegistrationToken token )
{
    if (com_obj && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, EventRegistrationToken ) = wv2_slot_fn( com_obj, TRUE, com_slot, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( com_obj, token );
    }
    return S_OK;
}

#define CWV2_EVENT_PAIR(name, com_add, com_rem, avtbl, aiid) \
    HRESULT WINAPI cwv2_add_##name( void *iface, void *handler, EventRegistrationToken *token ) \
    { struct core_webview2 *impl = impl_from_cwv2( iface ); \
      return cwv2_forward_add_event( impl, impl->loader_core, com_add, handler, token, avtbl, aiid ); } \
    HRESULT WINAPI cwv2_remove_##name( void *iface, EventRegistrationToken token ) \
    { struct core_webview2 *impl = impl_from_cwv2( iface ); \
      return cwv2_forward_remove_event( impl, impl->loader_core, com_rem, token ); }

CWV2_EVENT_PAIR( NavigationStarting, 7, 8, cwv2_navstart_args_vtbl, &IID_ICoreWebView2NavigationStartingEventArgs_rt )
CWV2_EVENT_PAIR( ContentLoading, 9, 10, NULL, NULL )
CWV2_EVENT_PAIR( SourceChanged, 11, 12, NULL, NULL )
CWV2_EVENT_PAIR( HistoryChanged, 13, 14, NULL, NULL )
CWV2_EVENT_PAIR( NavigationCompleted, 15, 16, cwv2_navcomp_args_vtbl, &IID_ICoreWebView2NavigationCompletedEventArgs_rt )
CWV2_EVENT_PAIR( FrameNavigationStarting, 17, 18, cwv2_navstart_args_vtbl, &IID_ICoreWebView2NavigationStartingEventArgs_rt )
CWV2_EVENT_PAIR( FrameNavigationCompleted, 19, 20, cwv2_navcomp_args_vtbl, &IID_ICoreWebView2NavigationCompletedEventArgs_rt )
CWV2_EVENT_PAIR( ScriptDialogOpening, 21, 22, NULL, NULL )
CWV2_EVENT_PAIR( PermissionRequested, 23, 24, NULL, NULL )
CWV2_EVENT_PAIR( ProcessFailed, 25, 26, cwv2_procfail_args_vtbl, &IID_ICoreWebView2ProcessFailedEventArgs_rt )
CWV2_EVENT_PAIR( WebMessageReceived, 34, 35, cwv2_webmsg_args_vtbl, &IID_ICoreWebView2WebMessageReceivedEventArgs_rt )
CWV2_EVENT_PAIR( NewWindowRequested, 44, 45, NULL, NULL )
CWV2_EVENT_PAIR( DocumentTitleChanged, 46, 47, NULL, NULL )
CWV2_EVENT_PAIR( ContainsFullScreenElementChanged, 52, 53, NULL, NULL )
CWV2_EVENT_PAIR( WebResourceRequested, 55, 56, cwv2_wrr_evargs_vtbl, &IID_ICoreWebView2WebResourceRequestedEventArgs_rt )
CWV2_EVENT_PAIR( WindowCloseRequested, 59, 60, NULL, NULL )

HRESULT WINAPI cwv2_2_add_WebResourceResponseReceived( void *iface, void *handler, EventRegistrationToken *token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_add_event( impl, c2, 61, handler, token, NULL, NULL );
}
HRESULT WINAPI cwv2_2_remove_WebResourceResponseReceived( void *iface, EventRegistrationToken token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_remove_event( impl, c2, 62, token );
}
HRESULT WINAPI cwv2_2_NavigateWithWebResourceRequest( void *iface, void *request )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    ERR( "cwv2_2_NavigateWithWebResourceRequest request=%p c2=%p\n", request, c2 );
    if (impl->owner) impl->owner->navigated = TRUE;
    if (c2 && request)
    {
        HRESULT (WINAPI *fn)( void *, void * ) = wv2_slot_fn( c2, TRUE, 63, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( c2, request );
    }
    return S_OK;
}
HRESULT WINAPI cwv2_2_add_DOMContentLoaded( void *iface, void *handler, EventRegistrationToken *token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_add_event( impl, c2, 64, handler, token, cwv2_domloaded_args_vtbl, &IID_ICoreWebView2DOMContentLoadedEventArgs_rt );
}
HRESULT WINAPI cwv2_2_remove_DOMContentLoaded( void *iface, EventRegistrationToken token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_remove_event( impl, c2, 65, token );
}

HRESULT WINAPI cwv2_11_add_ContextMenuRequested( void *iface, void *handler, EventRegistrationToken *token )
{
    struct core_webview2 *impl = impl_from_cwv2_11( iface );
    void *c11 = cwv2_get_com_iface( impl, &impl->loader_core11, &IID_ICoreWebView2_11_com );
    return cwv2_forward_add_event( impl, c11, 100, handler, token, cwv2_generic_evargs_vtbl, &IID_ICoreWebView2ContextMenuRequestedEventArgs_rt );
}
HRESULT WINAPI cwv2_11_remove_ContextMenuRequested( void *iface, EventRegistrationToken token )
{
    struct core_webview2 *impl = impl_from_cwv2_11( iface );
    void *c11 = cwv2_get_com_iface( impl, &impl->loader_core11, &IID_ICoreWebView2_11_com );
    return cwv2_forward_remove_event( impl, c11, 101, token );
}
HRESULT WINAPI cwv2_11_CallDevToolsProtocolMethodForSessionAsync( void *iface, HSTRING sid, HSTRING method, HSTRING params, void **op )
{
    if (!op) return E_POINTER;
    return cwv2_asyncop_create( NULL, op );
}

HRESULT WINAPI cwv2_get_Profile( void *iface, void **out )
{
    struct cwv2_profile *p;
    (void)iface;
    ERR( "cwv2_get_Profile\n" );
    if (!out) return E_POINTER;
    if (!(p = calloc( 1, sizeof(*p) ))) return E_OUTOFMEMORY;
    p->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_profile_vtbl );
    p->ref = 1;
    *out = &p->IInspectable_iface;
    return S_OK;
}

/* Breadcrumb generic slot for unimplemented CoreWebView2 slots */
HRESULT WINAPI cwv2_generic_slot( void *iface, ... )
{
    const void **vt = *(const void ***)iface;
    int slot = 0;
    va_list arg;
    while (slot < 256 && vt[slot] != (const void *)cwv2_generic_slot) slot++;
    va_start( arg, iface );
    {
        void *a1 = va_arg( arg, void * );
        void *a2 = va_arg( arg, void * );
        ERR( "cwv2_slot %d iface %p a1 %p a2 %p\n", slot, iface, a1, a2 );
    }
    va_end( arg );
    return S_OK;
}
