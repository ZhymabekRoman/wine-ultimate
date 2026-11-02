/*
 * Navigation client stand-in for hosted CoreWindows
 *
 * Copyright 2026 the Wine project authors
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
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "windows.h"
#include "objbase.h"
#include "roapi.h"
#include "winstring.h"
#include "inspectable.h"
#include "wine/debug.h"

#include "wineuwp.h"

WINE_DEFAULT_DEBUG_CHANNEL(uwp);

#define ASYNC_STATUS_COMPLETED 1

/* IAsyncAction that is already complete: PrepareToActivate has nothing to wait for here. */
struct done_action
{
    const struct done_action_vtbl *vtbl_action;
    const struct done_info_vtbl *vtbl_info;
    IUnknown *handler;
    IUnknown *ftm;
    LONG refcount;
};

struct done_action_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct done_action *, REFIID, void ** );
    ULONG (WINAPI *AddRef)( struct done_action * );
    ULONG (WINAPI *Release)( struct done_action * );
    HRESULT (WINAPI *GetIids)( struct done_action *, ULONG *, IID ** );
    HRESULT (WINAPI *GetRuntimeClassName)( struct done_action *, HSTRING * );
    HRESULT (WINAPI *GetTrustLevel)( struct done_action *, TrustLevel * );
    HRESULT (WINAPI *put_Completed)( struct done_action *, IUnknown * );
    HRESULT (WINAPI *get_Completed)( struct done_action *, IUnknown ** );
    HRESULT (WINAPI *GetResults)( struct done_action * );
};

struct done_info_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *, REFIID, void ** );
    ULONG (WINAPI *AddRef)( void * );
    ULONG (WINAPI *Release)( void * );
    HRESULT (WINAPI *get_Id)( void *, UINT32 * );
    HRESULT (WINAPI *get_Status)( void *, INT32 * );
    HRESULT (WINAPI *get_ErrorCode)( void *, HRESULT * );
    HRESULT (WINAPI *Cancel)( void * );
    HRESULT (WINAPI *Close)( void * );
};

/* IAsyncActionCompletedHandler::Invoke( IAsyncAction *, AsyncStatus ) */
struct completed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( IUnknown *, REFIID, void ** );
    ULONG (WINAPI *AddRef)( IUnknown * );
    ULONG (WINAPI *Release)( IUnknown * );
    HRESULT (WINAPI *Invoke)( IUnknown *, void *action, INT32 status );
};

static struct done_action *action_from_info( void *iface )
{
    return (struct done_action *)((char *)iface - offsetof( struct done_action, vtbl_info ));
}

static HRESULT WINAPI action_QueryInterface( struct done_action *action, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAsyncActionStandIn ))
        *out = &action->vtbl_action;
    else if (IsEqualGUID( iid, &IID_IAsyncInfoStandIn ))
        *out = &action->vtbl_info;
    else if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IAgileObject ))
        return IUnknown_QueryInterface( action->ftm, iid, out );
    else
    {
        WARN( "no interface %s\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &action->refcount );
    return S_OK;
}

static ULONG WINAPI action_AddRef( struct done_action *action )
{
    return InterlockedIncrement( &action->refcount );
}

static ULONG WINAPI action_Release( struct done_action *action )
{
    ULONG ref = InterlockedDecrement( &action->refcount );
    if (!ref)
    {
        if (action->handler) IUnknown_Release( action->handler );
        if (action->ftm) IUnknown_Release( action->ftm );
        free( action );
    }
    return ref;
}

static HRESULT WINAPI action_GetIids( struct done_action *action, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}

static HRESULT WINAPI action_GetRuntimeClassName( struct done_action *action, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncAction", 31, name );
}

static HRESULT WINAPI action_GetTrustLevel( struct done_action *action, TrustLevel *level )
{
    *level = BaseTrust; return S_OK;
}

static HRESULT WINAPI action_put_Completed( struct done_action *action, IUnknown *handler )
{
    const struct completed_handler_vtbl **vtbl = (const struct completed_handler_vtbl **)handler;

    TRACE( "%p %p\n", action, handler );
    if (!handler) return S_OK;
    /* The action completed before it was handed out, so call the handler straight away. */
    return (*vtbl)->Invoke( handler, &action->vtbl_action, ASYNC_STATUS_COMPLETED );
}

static HRESULT WINAPI action_get_Completed( struct done_action *action, IUnknown **handler )
{
    *handler = NULL; return S_OK;
}

static HRESULT WINAPI action_GetResults( struct done_action *action )
{
    return S_OK;
}

static HRESULT WINAPI info_QueryInterface( void *iface, REFIID iid, void **out )
{
    return action_QueryInterface( action_from_info( iface ), iid, out );
}

static ULONG WINAPI info_AddRef( void *iface )
{
    return action_AddRef( action_from_info( iface ) );
}

static ULONG WINAPI info_Release( void *iface )
{
    return action_Release( action_from_info( iface ) );
}

static HRESULT WINAPI info_get_Id( void *iface, UINT32 *id )
{
    *id = 1; return S_OK;
}

static HRESULT WINAPI info_get_Status( void *iface, INT32 *status )
{
    *status = ASYNC_STATUS_COMPLETED; return S_OK;
}

static HRESULT WINAPI info_get_ErrorCode( void *iface, HRESULT *hr )
{
    *hr = S_OK; return S_OK;
}

static HRESULT WINAPI info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI info_Close( void *iface ) { return S_OK; }

static const struct done_action_vtbl done_action_vtbl =
{
    action_QueryInterface, action_AddRef, action_Release, action_GetIids,
    action_GetRuntimeClassName, action_GetTrustLevel,
    action_put_Completed, action_get_Completed, action_GetResults,
};
static const struct done_info_vtbl done_info_vtbl =
{
    info_QueryInterface, info_AddRef, info_Release,
    info_get_Id, info_get_Status, info_get_ErrorCode, info_Cancel, info_Close,
};

static HRESULT create_completed_action( void **out )
{
    struct done_action *action;

    if (!(action = calloc( 1, sizeof(*action) ))) return E_OUTOFMEMORY;
    action->vtbl_action = &done_action_vtbl;
    action->vtbl_info = &done_info_vtbl;
    action->refcount = 1;
    if (FAILED( CoCreateFreeThreadedMarshaler( (IUnknown *)&action->vtbl_action, &action->ftm ) ))
    {
        free( action );
        return E_OUTOFMEMORY;
    }
    *out = &action->vtbl_action;
    return S_OK;
}

/* The navigation client itself.  NAV_CLIENT_SLOTS matches the real vtable slot for slot, so a
 * caller reaching a high slot lands on a method of ours instead of off the end of the array. */
struct nav_client
{
    const void **vtbl_client;
    const void **vtbl_prepare;
    IUnknown *ftm;
    LONG refcount;
};

static struct nav_client *nav_from_prepare( void *iface )
{
    return (struct nav_client *)((char *)iface - offsetof( struct nav_client, vtbl_prepare ));
}

static HRESULT WINAPI nav_QueryInterface( struct nav_client *nav, REFIID iid, void **out );

static ULONG WINAPI nav_AddRef( struct nav_client *nav )
{
    return InterlockedIncrement( &nav->refcount );
}
static ULONG WINAPI nav_Release( struct nav_client *nav )
{
    ULONG ref = InterlockedDecrement( &nav->refcount );
    if (!ref)
    {
        if (nav->ftm) IUnknown_Release( nav->ftm );
        free( nav );
    }
    return ref;
}
static HRESULT WINAPI nav_GetIids( struct nav_client *nav, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI nav_GetRuntimeClassName( struct nav_client *nav, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Phone.UI.Core.ImmersiveNavigationClient", 47, name );
}
static HRESULT WINAPI nav_GetTrustLevel( struct nav_client *nav, TrustLevel *level )
{
    *level = BaseTrust; return S_OK;
}

/* {075be329-825a-4f2a-a8af-cd2f9a8102ec} slot 6: hand the caller a completed IAsyncAction. */
static HRESULT WINAPI nav_PrepareToActivate( void *iface, UINT64 arg, void **action )
{
    TRACE( "%p %#I64x %p\n", iface, arg, action );
    if (!action) return E_POINTER;
    return create_completed_action( action );
}

static HRESULT WINAPI prepare_QueryInterface( void *iface, REFIID iid, void **out )
{
    return nav_QueryInterface( nav_from_prepare( iface ), iid, out );
}
static ULONG WINAPI prepare_AddRef( void *iface )
{
    return nav_AddRef( nav_from_prepare( iface ) );
}
static ULONG WINAPI prepare_Release( void *iface )
{
    return nav_Release( nav_from_prepare( iface ) );
}
static HRESULT WINAPI prepare_GetIids( void *iface, ULONG *count, IID **iids )
{
    return nav_GetIids( nav_from_prepare( iface ), count, iids );
}
static HRESULT WINAPI prepare_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return nav_GetRuntimeClassName( nav_from_prepare( iface ), name );
}
static HRESULT WINAPI prepare_GetTrustLevel( void *iface, TrustLevel *level )
{
    return nav_GetTrustLevel( nav_from_prepare( iface ), level );
}

/* Every slot below is named from CoreUIComponents' real NavigationClient vtable
 * (??_7NavigationClient@Core@UI@Phone@Windows@@6BINavigationClient@1234@@, RVA 0x1af3f8 in the
 * pinned Windows 11 build, 138 slots), resolved against Microsoft's public symbols.  The values
 * describe the one view this host has: a single top-level 1280x800 landscape window with no
 * system title bar.  Slots whose parameter shape is not recoverable from the symbol name alone
 * stay numbered stubs, because writing a guessed struct would corrupt the caller's stack.
 */
#define DISPLAY_ORIENTATION_LANDSCAPE 1

/* Windows.Foundation.Rect: four floats, not a RECT. */
struct foundation_rect { FLOAT x, y, width, height; };

/* Windows.Foundation.Size */
struct foundation_size { FLOAT width, height; };

/* VIEW_BOUNDS: nine 32-bit fields copied straight out of the client (GetViewBounds at RVA
 * 0x7dc40 moves [this+0x2c0..+0x2e0] to [out+0x00..+0x20]).  get_VisibleBounds proves the first
 * two are the window width and height in pixels; the rest are unidentified, so they read 0. */
struct view_bounds { INT32 width, height, unknown[7]; };

static HRESULT WINAPI nav_get_SplashScreen( void *iface, void **value )
{
    FIXME( "%p %p: stub\n", iface, value );
    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI nav_put_SplashScreen( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsScreenCaptureEnabled( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsScreenCaptureEnabled( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_SuppressSystemOverlays( void *iface, INT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_put_SuppressSystemOverlays( void *iface, INT32 value )
{
    TRACE( "%p %d\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_VisibleBounds( void *iface, struct foundation_rect *bounds )
{
    TRACE( "%p %p\n", iface, bounds );
    if (!bounds) return E_POINTER;
    bounds->x = bounds->y = 0.0f;
    wineuwp_view_size( &bounds->width, &bounds->height );
    return S_OK;
}

static HRESULT WINAPI nav_add_OrientationChanged( void *iface, void *handler, UINT64 *token )
{
    TRACE( "%p handler %p\n", iface, handler );
    if (!token) return E_POINTER;
    /* Nothing here raises these events, so hand back a token that removes cleanly. */
    *token = 0;
    return S_OK;
}

static HRESULT WINAPI nav_remove_OrientationChanged( void *iface, UINT64 token )
{
    TRACE( "%p token %#I64x\n", iface, token );
    return S_OK;
}

static HRESULT WINAPI nav_add_TitleBarVisibilityChanged( void *iface, void *handler, UINT64 *token )
{
    TRACE( "%p handler %p\n", iface, handler );
    if (!token) return E_POINTER;
    /* Nothing here raises these events, so hand back a token that removes cleanly. */
    *token = 0;
    return S_OK;
}

static HRESULT WINAPI nav_remove_TitleBarVisibilityChanged( void *iface, UINT64 token )
{
    TRACE( "%p token %#I64x\n", iface, token );
    return S_OK;
}

static HRESULT WINAPI nav_add_TitleBarLayoutMetricsChanged( void *iface, void *handler, UINT64 *token )
{
    TRACE( "%p handler %p\n", iface, handler );
    if (!token) return E_POINTER;
    /* Nothing here raises these events, so hand back a token that removes cleanly. */
    *token = 0;
    return S_OK;
}

static HRESULT WINAPI nav_remove_TitleBarLayoutMetricsChanged( void *iface, UINT64 token )
{
    TRACE( "%p token %#I64x\n", iface, token );
    return S_OK;
}

static HRESULT WINAPI nav_add_TitleBarInputRoutingInfoChanged( void *iface, void *handler, UINT64 *token )
{
    TRACE( "%p handler %p\n", iface, handler );
    if (!token) return E_POINTER;
    /* Nothing here raises these events, so hand back a token that removes cleanly. */
    *token = 0;
    return S_OK;
}

static HRESULT WINAPI nav_remove_TitleBarInputRoutingInfoChanged( void *iface, UINT64 token )
{
    TRACE( "%p token %#I64x\n", iface, token );
    return S_OK;
}

static HRESULT WINAPI nav_GetCurrentOrientation( void *iface, INT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = DISPLAY_ORIENTATION_LANDSCAPE;
    return S_OK;
}

static DWORD view_activated_tick;   /* GetTickCount() of the app's Window.Activate(), 0 before */

static HRESULT WINAPI nav_OnActivateWindow( void *iface, void *window )
{
    HWND hwnd = wineuwp_hosted_core_window();

    TRACE( "%s: core window %p, caller %p\n", "OnActivateWindow", window,
           __builtin_return_address( 0 ) );
    if (!view_activated_tick) view_activated_tick = GetTickCount() | 1;
    /* The app's Window.Activate(): on Windows the shell activates the CoreWindow now, and the
     * window's keyboard-cue state is initialised at that point. The host shows the window earlier
     * (before XAML subclasses it), so re-initialise here: XAML reads the resulting
     * WM_UPDATEUISTATE to decide whether focus visuals should be drawn. */
    if (hwnd)
        SendMessageW( hwnd, WM_CHANGEUISTATE, MAKEWPARAM( UIS_INITIALIZE, 0 ), 0 );
    return S_OK;
}

static HRESULT WINAPI nav_OnCloseWindow( void *iface, void *window )
{
    ERR( "%s: core window %p\n", "OnCloseWindow", window );
    return S_OK;
}

static HRESULT WINAPI nav_OnDestroyWindow( void *iface, void *window )
{
    ERR( "%s: core window %p\n", "OnDestroyWindow", window );
    return S_OK;
}

static HRESULT WINAPI nav_GetNativeOrientation( void *iface, INT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = DISPLAY_ORIENTATION_LANDSCAPE;
    return S_OK;
}

static HRESULT WINAPI nav_ShowStandardSystemOverlays( void *iface )
{
    TRACE( "%p\n", iface );
    return S_OK;
}

static HRESULT WINAPI nav_GetViewInstanceId( void *iface, UINT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}

static HRESULT WINAPI nav_GetViewBounds( void *iface, struct view_bounds *bounds )
{
    TRACE( "%p %p\n", iface, bounds );
    if (!bounds) return E_POINTER;
    memset( bounds, 0, sizeof(*bounds) );
    {
        FLOAT w, h;
        wineuwp_view_size( &w, &h );
        bounds->width = (int)w;
        bounds->height = (int)h;
    }
    return S_OK;
}

static HRESULT WINAPI nav_SetPreferredMinSize( void *iface, struct foundation_size size )
{
    TRACE( "%p %gx%g\n", iface, size.width, size.height );
    return S_OK;
}

static HRESULT WINAPI nav_get_PreferredLaunchWindowingMode( void *iface, INT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_put_PreferredLaunchWindowingMode( void *iface, INT32 value )
{
    TRACE( "%p %d\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_PreferredLaunchViewSize( void *iface, struct foundation_size *size )
{
    TRACE( "%p %p\n", iface, size );
    if (!size) return E_POINTER;
    size->width = CORE_WINDOW_WIDTH;
    size->height = CORE_WINDOW_HEIGHT;
    return S_OK;
}

static HRESULT WINAPI nav_put_PreferredLaunchViewSize( void *iface, struct foundation_size size )
{
    TRACE( "%p %gx%g\n", iface, size.width, size.height );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsViewExtendedIntoTitleBar( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsViewExtendedIntoTitleBar( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarHeight( void *iface, DOUBLE *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0.0;
    return S_OK;
}

static HRESULT WINAPI nav_get_IsTitleBarVisible( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarSystemOverlayLeftInset( void *iface, DOUBLE *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0.0;
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarSystemOverlayRightInset( void *iface, DOUBLE *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0.0;
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarInputRoutingInfo( void *iface, void *value )
{
    FIXME( "%p %p: stub, parameter shape unknown\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI nav_get_TitleBarInputRoutingHwnd( void *iface, UINT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_get_IsTitleBarInputVisualSet( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsTitleBarInputVisualSet( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarTextColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarTextColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarBackgroundColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarBackgroundColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarButtonTextColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarButtonTextColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarButtonBackgroundColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarButtonBackgroundColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarButtonHoverTextColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarButtonHoverTextColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarButtonHoverBackgroundColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarButtonHoverBackgroundColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarButtonPressedTextColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarButtonPressedTextColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarButtonPressedBackgroundColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarButtonPressedBackgroundColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarInactiveTextColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarInactiveTextColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarInactiveBackgroundColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarInactiveBackgroundColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarInactiveButtonTextColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarInactiveButtonTextColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_TitleBarInactiveButtonBackgroundColorOverride( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_TitleBarInactiveButtonBackgroundColorOverride( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_OnNavigateBackRequest( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_SetAppAsComponent( void *iface )
{
    TRACE( "%p\n", iface );
    return S_OK;
}

static HRESULT WINAPI nav_DisableResponsiveAppViewPolicy( void *iface )
{
    TRACE( "%p\n", iface );
    return S_OK;
}

static HRESULT WINAPI nav_Uninitialize( void *iface, void *window )
{
    ERR( "%s: core window %p\n", "Uninitialize", window );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsCloaked( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_get_IsDisabled( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsDisabled( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsMaximizable( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsMaximizable( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsMinimizable( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsMinimizable( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsMovable( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsMovable( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsPopup( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsPopup( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsProxied( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsProxied( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsResizable( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsResizable( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsWin32NoActivate( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsWin32NoActivate( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsWin32ToolWindow( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsWin32ToolWindow( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsWin32TopLevelAppWindow( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_put_IsWin32TopLevelAppWindow( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_HideInTaskSwitcher( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_HideInTaskSwitcher( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_PreferDisableSystemUXPolicy( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_put_PreferDisableSystemUXPolicy( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_ShouldSuppressCaptionControls( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_put_ShouldSuppressCaptionControls( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_RemoteConfigurationDescription( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_RemoteConfigurationDescription( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_RemoteConfigurationName( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_RemoteConfigurationName( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_SetPreferredMaxSize( void *iface, struct foundation_size size )
{
    TRACE( "%p %gx%g\n", iface, size.width, size.height );
    return S_OK;
}

static HRESULT WINAPI nav_SetPreferredScale( void *iface, INT32 value )
{
    TRACE( "%p %d\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_GetWindowInstanceId( void *iface, UINT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}

static HRESULT WINAPI nav_GetServerTaskInstanceId( void *iface, UINT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}

static HRESULT WINAPI nav_RequestHide( void *iface, INT32 value )
{
    TRACE( "%p %d\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_GetSpanningRects( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_get_ContainerId( void *iface, GUID *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    memset( value, 0, sizeof(*value) );
    return S_OK;
}

static HRESULT WINAPI nav_put_ContainerId( void *iface, GUID value )
{
    TRACE( "%p %s\n", iface, debugstr_guid( &value ) );
    return S_OK;
}

static HRESULT WINAPI nav_get_IsTopMost( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_get_Placement( void *iface, INT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_get_ProxiedAppId( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_put_ProxiedAppId( void *iface, void *value )
{
    TRACE( "%p %p\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_ProxiedProcessId( void *iface, UINT64 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_put_ProxiedProcessId( void *iface, UINT64 value )
{
    TRACE( "%p %#I64x\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_get_ProxiedHwnd( void *iface, UINT64 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_put_ProxiedHwnd( void *iface, UINT64 value )
{
    TRACE( "%p %#I64x\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_TrySetTopMost( void *iface, boolean value )
{
    TRACE( "%p %u\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI nav_PresentationLoadingComplete( void *iface )
{
    ERR( "%s\n", "PresentationLoadingComplete" );
    return S_OK;
}

static HRESULT WINAPI nav_PresentationUnloadedComplete( void *iface )
{
    ERR( "%s\n", "PresentationUnloadedComplete" );
    return S_OK;
}

static HRESULT WINAPI nav_add_ScaleChanged( void *iface, void *handler, UINT64 *token )
{
    TRACE( "%p handler %p\n", iface, handler );
    if (!token) return E_POINTER;
    /* Nothing here raises these events, so hand back a token that removes cleanly. */
    *token = 0;
    return S_OK;
}

static HRESULT WINAPI nav_remove_ScaleChanged( void *iface, UINT64 token )
{
    TRACE( "%p token %#I64x\n", iface, token );
    return S_OK;
}

static HRESULT WINAPI nav_add_OrientationChangedInternal( void *iface, void *handler, UINT64 *token )
{
    TRACE( "%p handler %p\n", iface, handler );
    if (!token) return E_POINTER;
    /* Nothing here raises these events, so hand back a token that removes cleanly. */
    *token = 0;
    return S_OK;
}

static HRESULT WINAPI nav_remove_OrientationChangedInternal( void *iface, UINT64 token )
{
    TRACE( "%p token %#I64x\n", iface, token );
    return S_OK;
}

static HRESULT WINAPI nav_get_ConsolidationStatus( void *iface, INT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_get_IsConsolidated( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_get_IsMinimized( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_get_IsOnLockScreen( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI nav_get_IsVisible( void *iface, boolean *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI nav_get_PreferredMaxSize( void *iface, struct foundation_size *size )
{
    TRACE( "%p %p\n", iface, size );
    if (!size) return E_POINTER;
    size->width = CORE_WINDOW_WIDTH;
    size->height = CORE_WINDOW_HEIGHT;
    return S_OK;
}

static HRESULT WINAPI nav_get_PreferredMinSize( void *iface, struct foundation_size *size )
{
    TRACE( "%p %p\n", iface, size );
    if (!size) return E_POINTER;
    size->width = 0.0f;
    size->height = 0.0f;
    return S_OK;
}

static HRESULT WINAPI nav_get_PreferredScale( void *iface, UINT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 100;
    return S_OK;
}

static HRESULT WINAPI nav_get_ViewPresentationMode( void *iface, INT32 *value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI nav_get_PrimaryDisplayRegion( void *iface, void **value )
{
    TRACE( "%p %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI nav_NotifyScaleChanged( void *iface )
{
    TRACE( "%p\n", iface );
    return S_OK;
}

static HRESULT WINAPI nav_stub_72( void *iface )
{
    FIXME( "INavigationClient slot 72: stub\n" );
    return E_NOTIMPL;
}

#define NAV_CLIENT_SLOTS 138

static const void *nav_client_vtbl[NAV_CLIENT_SLOTS] =
{
    nav_QueryInterface, nav_AddRef, nav_Release,
    nav_GetIids, nav_GetRuntimeClassName, nav_GetTrustLevel,
    nav_get_SplashScreen, nav_put_SplashScreen, nav_get_IsScreenCaptureEnabled,
    nav_put_IsScreenCaptureEnabled, nav_get_SuppressSystemOverlays, nav_put_SuppressSystemOverlays,
    nav_get_VisibleBounds, nav_add_OrientationChanged, nav_remove_OrientationChanged,
    nav_add_TitleBarVisibilityChanged, nav_remove_TitleBarVisibilityChanged,
    nav_add_TitleBarLayoutMetricsChanged, nav_remove_TitleBarLayoutMetricsChanged,
    nav_add_TitleBarInputRoutingInfoChanged, nav_remove_TitleBarInputRoutingInfoChanged,
    nav_GetCurrentOrientation, nav_OnActivateWindow, nav_OnCloseWindow, nav_OnDestroyWindow,
    nav_GetNativeOrientation, nav_ShowStandardSystemOverlays, nav_GetViewInstanceId, nav_GetViewBounds,
    nav_SetPreferredMinSize, nav_get_PreferredLaunchWindowingMode, nav_put_PreferredLaunchWindowingMode,
    nav_get_PreferredLaunchViewSize, nav_put_PreferredLaunchViewSize, nav_get_IsViewExtendedIntoTitleBar,
    nav_put_IsViewExtendedIntoTitleBar, nav_get_TitleBarHeight, nav_get_IsTitleBarVisible,
    nav_get_TitleBarSystemOverlayLeftInset, nav_get_TitleBarSystemOverlayRightInset,
    nav_get_TitleBarInputRoutingInfo, nav_get_TitleBarInputRoutingHwnd, nav_get_IsTitleBarInputVisualSet,
    nav_put_IsTitleBarInputVisualSet, nav_get_TitleBarTextColorOverride, nav_put_TitleBarTextColorOverride,
    nav_get_TitleBarBackgroundColorOverride, nav_put_TitleBarBackgroundColorOverride,
    nav_get_TitleBarButtonTextColorOverride, nav_put_TitleBarButtonTextColorOverride,
    nav_get_TitleBarButtonBackgroundColorOverride, nav_put_TitleBarButtonBackgroundColorOverride,
    nav_get_TitleBarButtonHoverTextColorOverride, nav_put_TitleBarButtonHoverTextColorOverride,
    nav_get_TitleBarButtonHoverBackgroundColorOverride, nav_put_TitleBarButtonHoverBackgroundColorOverride,
    nav_get_TitleBarButtonPressedTextColorOverride, nav_put_TitleBarButtonPressedTextColorOverride,
    nav_get_TitleBarButtonPressedBackgroundColorOverride,
    nav_put_TitleBarButtonPressedBackgroundColorOverride, nav_get_TitleBarInactiveTextColorOverride,
    nav_put_TitleBarInactiveTextColorOverride, nav_get_TitleBarInactiveBackgroundColorOverride,
    nav_put_TitleBarInactiveBackgroundColorOverride, nav_get_TitleBarInactiveButtonTextColorOverride,
    nav_put_TitleBarInactiveButtonTextColorOverride, nav_get_TitleBarInactiveButtonBackgroundColorOverride,
    nav_put_TitleBarInactiveButtonBackgroundColorOverride, nav_OnNavigateBackRequest, nav_SetAppAsComponent,
    nav_DisableResponsiveAppViewPolicy, nav_Uninitialize, nav_stub_72, nav_get_IsCloaked,
    nav_get_IsDisabled, nav_put_IsDisabled, nav_get_IsMaximizable, nav_put_IsMaximizable,
    nav_get_IsMinimizable, nav_put_IsMinimizable, nav_get_IsMovable, nav_put_IsMovable, nav_get_IsPopup,
    nav_put_IsPopup, nav_get_IsProxied, nav_put_IsProxied, nav_get_IsResizable, nav_put_IsResizable,
    nav_get_IsWin32NoActivate, nav_put_IsWin32NoActivate, nav_get_IsWin32ToolWindow,
    nav_put_IsWin32ToolWindow, nav_get_IsWin32TopLevelAppWindow, nav_put_IsWin32TopLevelAppWindow,
    nav_get_HideInTaskSwitcher, nav_put_HideInTaskSwitcher, nav_get_PreferDisableSystemUXPolicy,
    nav_put_PreferDisableSystemUXPolicy, nav_get_ShouldSuppressCaptionControls,
    nav_put_ShouldSuppressCaptionControls, nav_get_RemoteConfigurationDescription,
    nav_put_RemoteConfigurationDescription, nav_get_RemoteConfigurationName,
    nav_put_RemoteConfigurationName, nav_SetPreferredMaxSize, nav_SetPreferredScale,
    nav_GetWindowInstanceId, nav_GetServerTaskInstanceId, nav_RequestHide, nav_GetSpanningRects,
    nav_get_ContainerId, nav_put_ContainerId, nav_get_IsTopMost, nav_get_Placement, nav_get_ProxiedAppId,
    nav_put_ProxiedAppId, nav_get_ProxiedProcessId, nav_put_ProxiedProcessId, nav_get_ProxiedHwnd,
    nav_put_ProxiedHwnd, nav_TrySetTopMost, nav_PresentationLoadingComplete,
    nav_PresentationUnloadedComplete, nav_add_ScaleChanged, nav_remove_ScaleChanged,
    nav_add_OrientationChangedInternal, nav_remove_OrientationChangedInternal, nav_get_ConsolidationStatus,
    nav_get_IsConsolidated, nav_get_IsMinimized, nav_get_IsOnLockScreen, nav_get_IsVisible,
    nav_get_PreferredMaxSize, nav_get_PreferredMinSize, nav_get_PreferredScale,
    nav_get_ViewPresentationMode, nav_get_PrimaryDisplayRegion, nav_NotifyScaleChanged,
};

static const void *nav_prepare_vtbl[] =
{
    prepare_QueryInterface, prepare_AddRef, prepare_Release,
    prepare_GetIids, prepare_GetRuntimeClassName, prepare_GetTrustLevel,
    nav_PrepareToActivate,
};

static HRESULT WINAPI nav_QueryInterface( struct nav_client *nav, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ))
        *out = &nav->vtbl_client;
    else if (IsEqualGUID( iid, &IID_INavigationClientPrepareToActivate ))
        *out = &nav->vtbl_prepare;
    else if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IAgileObject ))
        return IUnknown_QueryInterface( nav->ftm, iid, out );
    else
    {
        FIXME( "navigation client has no interface %s\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &nav->refcount );
    return S_OK;
}

HRESULT create_navigation_client( IUnknown **out )
{
    struct nav_client *nav;

    if (!(nav = calloc( 1, sizeof(*nav) ))) return E_OUTOFMEMORY;
    nav->vtbl_client = nav_client_vtbl;
    nav->vtbl_prepare = nav_prepare_vtbl;
    nav->refcount = 1;
    if (FAILED( CoCreateFreeThreadedMarshaler( (IUnknown *)&nav->vtbl_client, &nav->ftm ) ))
    {
        free( nav );
        return E_OUTOFMEMORY;
    }
    *out = (IUnknown *)&nav->vtbl_client;
    return S_OK;
}

/* Put the stand-in client on the window's WindowServer, the way CImmersiveWindowFactoryProxy
 * puts the real one there. */
HRESULT attach_navigation_client( IUnknown *window )
{
    struct navigation_host_vtbl
    {
        void *unknown_inspectable[6];
        HRESULT (WINAPI *get_NavigationClient)( void *iface, IUnknown **client );
        HRESULT (WINAPI *put_NavigationClient)( void *iface, IUnknown *client );
    };
    struct navigation_host { const struct navigation_host_vtbl *vtbl; } *host;
    IUnknown *client;
    HRESULT hr;

    if (FAILED( hr = IUnknown_QueryInterface( window, &IID_ICoreWindowNavigation, (void **)&host ) ))
    {
        ERR( "CoreWindow has no navigation client property, hr %#lx\n", hr );
        return hr;
    }
    if (SUCCEEDED( hr = create_navigation_client( &client ) ))
    {
        hr = host->vtbl->put_NavigationClient( host, client );
        TRACE( "navigation client %p attached to %p, hr %#lx\n", client, window, hr );
        IUnknown_Release( client );
    }
    IUnknown_Release( (IUnknown *)host );
    return hr;
}
