/* WinRT Windows.UI.Core.CoreWindow Implementation
 *
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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

#include "private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "wine/debug.h"
#include <stdio.h>
#define UIDBG(...) fprintf(stderr, "UID " __VA_ARGS__)
#include "appmodel.h"
#include "roapi.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

/* Cached primary-monitor work area (device pixels). CoreWindow creation, the
 * WM_WINDOWPOSCHANGING zero-size clamp and the get_Bounds fallback all use it
 * instead of a hardcoded 1024x768/960x768. Refreshed on WM_DISPLAYCHANGE. */
static SIZE work_area_size;

static void refresh_work_area( void )
{
    MONITORINFO mi = { sizeof(mi) };
    HMONITOR monitor;
    RECT rect;
    UINT dpi;

    if (SystemParametersInfoW( SPI_GETWORKAREA, 0, &rect, 0 )
        && rect.right > rect.left && rect.bottom > rect.top)
    {
        work_area_size.cx = rect.right - rect.left;
        work_area_size.cy = rect.bottom - rect.top;
    }
    else if ((monitor = MonitorFromWindow( GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY ))
             && GetMonitorInfoW( monitor, &mi )
             && mi.rcWork.right > mi.rcWork.left && mi.rcWork.bottom > mi.rcWork.top)
    {
        work_area_size.cx = mi.rcWork.right - mi.rcWork.left;
        work_area_size.cy = mi.rcWork.bottom - mi.rcWork.top;
    }
    else
    {
        work_area_size.cx = 1024;
        work_area_size.cy = 768;
        if ((dpi = GetDpiForSystem()))
        {
            work_area_size.cx = MulDiv( work_area_size.cx, dpi, 96 );
            work_area_size.cy = MulDiv( work_area_size.cy, dpi, 96 );
        }
    }
    {
        int sx = GetSystemMetrics( SM_CXSCREEN ), sy = GetSystemMetrics( SM_CYSCREEN );
        if (sx > 0 && work_area_size.cx > sx) work_area_size.cx = sx;
        if (sy > 0 && work_area_size.cy > sy) work_area_size.cy = sy;
    }
    TRACE( "work area %lux%lu\n", work_area_size.cx, work_area_size.cy );
}

static SIZE get_work_area_size( void )
{
    if (!work_area_size.cx || !work_area_size.cy) refresh_work_area();
    return work_area_size;
}

struct corewindow_statics
{
    IActivationFactory IActivationFactory_iface;
    ICoreWindowStatic ICoreWindowStatic_iface;
    LONG ref;
};

static inline struct corewindow_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct corewindow_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct corewindow_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_ICoreWindowStatic ))
    {
        *out = &impl->ICoreWindowStatic_iface;
        ICoreWindowStatic_AddRef( &impl->ICoreWindowStatic_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct corewindow_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct corewindow_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p.\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

/* ---- minimal CoreWindow ---- */

static const GUID IID_ICoreWindowInterop = { 0x45d64a29, 0xa63e, 0x4cb6, { 0xb4, 0x98, 0x57, 0x81, 0xd2, 0x98, 0xcb, 0x4f } };

typedef struct ICoreWindowInterop ICoreWindowInterop;
typedef struct ICoreWindowInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWindowInterop *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWindowInterop *This );
    ULONG (WINAPI *Release)( ICoreWindowInterop *This );
    HRESULT (WINAPI *GetWindowHandle)( ICoreWindowInterop *This, HWND *hwnd );
    HRESULT (WINAPI *SetMessageHandled)( ICoreWindowInterop *This, boolean value );
} ICoreWindowInteropVtbl;

struct ICoreWindowInterop
{
    const ICoreWindowInteropVtbl *lpVtbl;
};

/* WindowServer's navigation client property, private, named from CoreUIComponents' symbols. Windows
 * puts the shell's INavigationClient on a CoreWindow when its window factory makes the window; a
 * view whose window has none fails CoreApplicationView::PrepareToActivateAsync with E_NOTIMPL, so
 * the activation never reaches the app. wineuwp's in-process broker puts its stand-in client here
 * (dlls/wineuwp/navigation.c). */
typedef struct ICoreWindowNavigation ICoreWindowNavigation;
typedef struct ICoreWindowNavigationVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWindowNavigation *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWindowNavigation *This );
    ULONG (WINAPI *Release)( ICoreWindowNavigation *This );
    HRESULT (WINAPI *GetIids)( ICoreWindowNavigation *This, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( ICoreWindowNavigation *This, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( ICoreWindowNavigation *This, TrustLevel *level );
    HRESULT (WINAPI *get_NavigationClient)( ICoreWindowNavigation *This, IUnknown **client );
    HRESULT (WINAPI *put_NavigationClient)( ICoreWindowNavigation *This, IUnknown *client );
} ICoreWindowNavigationVtbl;

struct ICoreWindowNavigation
{
    const ICoreWindowNavigationVtbl *lpVtbl;
};

static const GUID IID_ICoreWindowNavigation =
    { 0xbb4cbbe9, 0x65af, 0x4730, { 0x9e, 0xd2, 0xab, 0xbd, 0xb5, 0xcc, 0x15, 0xb9 } };

/* The shell's window client adapter property (ICoreWindowAdapterInterop), installed with
 * SetWindowClientAdapter. On Windows this is CoreUIComponents' NavigationClientWindowClientAdapter,
 * and Windows.UI's WindowServer hands its sub-adapters to twinapi.appcore. This module's builtins
 * read none of them, so the getters answer NULL — the state a window with no adapter is in — and the
 * adapter itself is kept for whoever asks. */
typedef struct ICoreWindowAdapterInterop ICoreWindowAdapterInterop;
typedef struct ICoreWindowAdapterInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWindowAdapterInterop *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWindowAdapterInterop *This );
    ULONG (WINAPI *Release)( ICoreWindowAdapterInterop *This );
    HRESULT (WINAPI *GetIids)( ICoreWindowAdapterInterop *This, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( ICoreWindowAdapterInterop *This, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( ICoreWindowAdapterInterop *This, TrustLevel *level );
    void *sub_adapters[7];  /* app activation, display, frame, positioner, presenter, title bar ... */
    HRESULT (WINAPI *SetWindowClientAdapter)( ICoreWindowAdapterInterop *This, IUnknown *adapter );
} ICoreWindowAdapterInteropVtbl;

struct ICoreWindowAdapterInterop
{
    const ICoreWindowAdapterInteropVtbl *lpVtbl;
};

static const GUID IID_ICoreWindowAdapterInterop =
    { 0x7a5b6fd1, 0xcd73, 0x4b6c, { 0x9c, 0xf4, 0x2e, 0x86, 0x9e, 0xaf, 0x47, 0x0a } };

struct core_window_weakref;

struct core_window
{
    ICoreWindow ICoreWindow_iface;
    ICoreWindowInterop ICoreWindowInterop_iface;
    ICoreWindowNavigation ICoreWindowNavigation_iface;
    ICoreWindowAdapterInterop ICoreWindowAdapterInterop_iface;
    const void *IWeakReferenceSource_iface;
    IInspectable dummy_iface;
    struct core_window_weakref *weakref;
    ICoreDispatcher *dispatcher;
    IUnknown *navigation_client;
    IUnknown *window_client_adapter;
    HWND hwnd;
    HWND csd_overlay_hwnd;
    LONG ref;
    float width;
    float height;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs *visibility_changed_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *size_changed_handlers[16];
    INT64 size_changed_tokens[16];
    INT64 next_size_changed_token;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs *activated_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *character_received_handlers[16];
    INT64 character_received_tokens[16];
    INT64 next_char_token;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *keydown_handlers[16];
    INT64 keydown_tokens[16];
    INT64 next_keydown_token;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *keyup_handlers[16];
    INT64 keyup_tokens[16];
    INT64 next_keyup_token;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *pointer_pressed_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *pointer_released_handler;
    __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet *custom_properties;
    ICoreCursor *pointer_cursor;
    CoreWindowFlowDirection flow_direction;
    boolean input_enabled;
};

static HRESULT WINAPI cursor_factory_CreateCursor( ICoreCursorFactory *iface, CoreCursorType type, UINT32 id, ICoreCursor **cursor );

static __thread struct core_window *thread_core_window;
static LONG process_core_window_count;

static struct core_window *create_core_window( const WCHAR *title, INT x, INT y, UINT width, UINT height,
                                               BOOL set_geometry );

DEFINE_IINSPECTABLE( corewindow_static, ICoreWindowStatic, struct corewindow_statics, IActivationFactory_iface )

static BOOL ondemand_corewindow_enabled( void )
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *v = getenv( "WINE_COREWINDOW_ONDEMAND" );
        if (v)
            cached = strcmp( v, "0" ) != 0;
        else if (GetModuleHandleW( L"Microsoft.UI.Xaml.dll" ))
            cached = 0;
        else
            cached = 1;
    }
    return cached;
}

static HRESULT STDMETHODCALLTYPE corewindow_static_GetForCurrentThread( ICoreWindowStatic *iface, ICoreWindow **windows )
{
    SIZE work_area;

    TRACE( "iface %p, windows %p.\n", iface, windows );
    if (!windows) return E_POINTER;

    /* Windows answers NULL on a thread that has no CoreWindow (the thread's window is created by
     * the activation machinery, not by asking).  Live bisect (gate57/58, WhatsApp Desktop):
     * on-demand creation here spawned a phantom CoreWindow + dispatcher for EVERY asking thread
     * (.NET TP workers among them) and the client blocked in early managed init.  Calculator's
     * WinUI3 startup caches the asking thread's window, so create-on-ask is the default there;
     * WINE_COREWINDOW_ONDEMAND=0 restores the Windows NULL-when-unowned contract. */
    if (!thread_core_window)
    {
        if (!ondemand_corewindow_enabled() ||
            InterlockedCompareExchange( &process_core_window_count, 0, 0 ) > 0)
        {
            *windows = NULL;
            return S_OK;
        }
        work_area = get_work_area_size();
        if (!create_core_window( NULL, CW_USEDEFAULT, CW_USEDEFAULT, work_area.cx, work_area.cy, FALSE ))
            return E_OUTOFMEMORY;
    }

    *windows = &thread_core_window->ICoreWindow_iface;
    ICoreWindow_AddRef( *windows );
    return S_OK;
}

static const struct ICoreWindowStaticVtbl corewindow_static_vtbl =
{
    corewindow_static_QueryInterface,
    corewindow_static_AddRef,
    corewindow_static_Release,
    /* IInspectable methods */
    corewindow_static_GetIids,
    corewindow_static_GetRuntimeClassName,
    corewindow_static_GetTrustLevel,
    /* ICoreWindowStatic methods */
    corewindow_static_GetForCurrentThread
};

static struct corewindow_statics corewindow_statics =
{
    {&factory_vtbl},
    {&corewindow_static_vtbl},
    1,
};

IActivationFactory *corewindow_factory = &corewindow_statics.IActivationFactory_iface;

/* ---- minimal CoreDispatcher ---- */

/* The owner thread's window queue is the dispatcher's queue: a foreign-thread RunAsync posts the
 * callback to the window, and the owner's ProcessEvents (which pumps the thread's messages) drains
 * it. The owner thread's RunAsync stays inline — an app like an unpackaged WinUI 3 process drives
 * its own dispatcher on the window thread and gets the callback exactly where it always did. */
#define WM_CORE_DISPATCHER_POST (WM_APP + 1)

struct dispatcher_post
{
    void *handler;   /* IDispatchedHandler / IIdleDispatchedHandler, AddRef'd for the flight */
    BOOL idle;
};

struct core_dispatcher_weakref;

struct core_dispatcher
{
    ICoreDispatcher ICoreDispatcher_iface;
    ICoreAcceleratorKeys ICoreAcceleratorKeys_iface;
    const void *IWeakReferenceSource_iface;
    struct core_dispatcher_weakref *weakref;
    struct core_window *window;   /* the owning window: its hwnd is the queue's door, its thread the owner */
    DWORD owner_tid;
    LONG ref;
};

struct core_dispatcher_weakref
{
    const void **lpVtbl;
    LONG ref;
    struct core_dispatcher *target;
};

static const GUID IID_IWeakReference_cd =
    { 0x00000037, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IWeakReferenceSource_cd =
    { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

static HRESULT WINAPI cd_wr_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_dispatcher_weakref *wr = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWeakReference_cd ))
    {
        InterlockedIncrement( &wr->ref );
        *out = wr;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cd_wr_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct core_dispatcher_weakref *)iface)->ref );
}
static ULONG WINAPI cd_wr_Release( void *iface )
{
    struct core_dispatcher_weakref *wr = iface;
    ULONG ref = InterlockedDecrement( &wr->ref );
    if (!ref) free( wr );
    return ref;
}
static HRESULT WINAPI cd_wr_Resolve( void *iface, REFIID iid, IInspectable **out )
{
    struct core_dispatcher_weakref *wr = iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!wr->target) return S_OK;
    return ICoreDispatcher_QueryInterface( &wr->target->ICoreDispatcher_iface, iid, (void **)out );
}
static const void *cd_wr_vtbl[] =
{
    cd_wr_QueryInterface,
    cd_wr_AddRef,
    cd_wr_Release,
    cd_wr_Resolve,
};

static inline struct core_dispatcher *impl_from_IWeakReferenceSource_cd( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_dispatcher, IWeakReferenceSource_iface );
}
static HRESULT WINAPI cd_wrs_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_dispatcher *impl = impl_from_IWeakReferenceSource_cd( iface );
    return ICoreDispatcher_QueryInterface( &impl->ICoreDispatcher_iface, iid, out );
}
static ULONG WINAPI cd_wrs_AddRef( void *iface )
{
    struct core_dispatcher *impl = impl_from_IWeakReferenceSource_cd( iface );
    return ICoreDispatcher_AddRef( &impl->ICoreDispatcher_iface );
}
static ULONG WINAPI cd_wrs_Release( void *iface )
{
    struct core_dispatcher *impl = impl_from_IWeakReferenceSource_cd( iface );
    return ICoreDispatcher_Release( &impl->ICoreDispatcher_iface );
}
static HRESULT WINAPI cd_wrs_GetWeakReference( void *iface, void **out )
{
    struct core_dispatcher *impl = impl_from_IWeakReferenceSource_cd( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl->weakref)
    {
        struct core_dispatcher_weakref *wr = calloc( 1, sizeof(*wr) );
        if (!wr) return E_OUTOFMEMORY;
        wr->lpVtbl = cd_wr_vtbl;
        wr->ref = 1;
        wr->target = impl;
        impl->weakref = wr;
    }
    InterlockedIncrement( &impl->weakref->ref );
    *out = impl->weakref;
    return S_OK;
}
static const void *cd_wrs_vtbl[] =
{
    cd_wrs_QueryInterface,
    cd_wrs_AddRef,
    cd_wrs_Release,
    cd_wrs_GetWeakReference,
};

static inline struct core_dispatcher *impl_from_ICoreDispatcher( ICoreDispatcher *iface )
{
    return CONTAINING_RECORD( iface, struct core_dispatcher, ICoreDispatcher_iface );
}

static inline struct core_dispatcher *impl_from_ICoreAcceleratorKeys( ICoreAcceleratorKeys *iface )
{
    return CONTAINING_RECORD( iface, struct core_dispatcher, ICoreAcceleratorKeys_iface );
}

static HRESULT WINAPI core_dispatcher_keys_QueryInterface( ICoreAcceleratorKeys *iface, REFIID iid, void **out )
{
    struct core_dispatcher *impl = impl_from_ICoreAcceleratorKeys( iface );
    return ICoreDispatcher_QueryInterface( &impl->ICoreDispatcher_iface, iid, out );
}
static ULONG WINAPI core_dispatcher_keys_AddRef( ICoreAcceleratorKeys *iface )
{
    struct core_dispatcher *impl = impl_from_ICoreAcceleratorKeys( iface );
    return ICoreDispatcher_AddRef( &impl->ICoreDispatcher_iface );
}
static ULONG WINAPI core_dispatcher_keys_Release( ICoreAcceleratorKeys *iface )
{
    struct core_dispatcher *impl = impl_from_ICoreAcceleratorKeys( iface );
    return ICoreDispatcher_Release( &impl->ICoreDispatcher_iface );
}
static HRESULT WINAPI core_dispatcher_keys_GetIids( ICoreAcceleratorKeys *iface, ULONG *count, IID **iids )
{ return E_NOTIMPL; }
static HRESULT WINAPI core_dispatcher_keys_GetRuntimeClassName( ICoreAcceleratorKeys *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreDispatcher", 30, name ); }
static HRESULT WINAPI core_dispatcher_keys_GetTrustLevel( ICoreAcceleratorKeys *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI core_dispatcher_keys_add( ICoreAcceleratorKeys *iface,
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreDispatcher_Windows__CUI__CCore__CAcceleratorKeyEventArgs *handler,
        EventRegistrationToken *cookie )
{ if (cookie) cookie->value = 1; return S_OK; }
static HRESULT WINAPI core_dispatcher_keys_remove( ICoreAcceleratorKeys *iface, EventRegistrationToken cookie )
{ return S_OK; }

static const struct ICoreAcceleratorKeysVtbl core_dispatcher_keys_vtbl =
{
    core_dispatcher_keys_QueryInterface,
    core_dispatcher_keys_AddRef,
    core_dispatcher_keys_Release,
    core_dispatcher_keys_GetIids,
    core_dispatcher_keys_GetRuntimeClassName,
    core_dispatcher_keys_GetTrustLevel,
    core_dispatcher_keys_add,
    core_dispatcher_keys_remove,
};

static HRESULT WINAPI core_dispatcher_QueryInterface( ICoreDispatcher *iface, REFIID iid, void **out )
{
    struct core_dispatcher *impl = impl_from_ICoreDispatcher( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICoreDispatcher ))
    { ICoreDispatcher_AddRef( &impl->ICoreDispatcher_iface ); *out = &impl->ICoreDispatcher_iface; return S_OK; }
    if (IsEqualGUID( iid, &IID_ICoreAcceleratorKeys ) ||
        !memcmp( iid, "\xf5\xf7\xfd\x9f\xc9\xb8\xf0\x4e\xb7\xd2\x1d\xe6\x26\x56\x1f\xc8", 16 ))
    {
        ICoreDispatcher_AddRef( &impl->ICoreDispatcher_iface );
        *out = &impl->ICoreAcceleratorKeys_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_cd ))
    {
        ICoreDispatcher_AddRef( &impl->ICoreDispatcher_iface );
        *out = &impl->IWeakReferenceSource_iface;
        return S_OK;
    }
    FIXME( "%s E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI core_dispatcher_AddRef( ICoreDispatcher *iface )
{ return InterlockedIncrement( &impl_from_ICoreDispatcher( iface )->ref ); }
static ULONG WINAPI core_dispatcher_Release( ICoreDispatcher *iface )
{
    struct core_dispatcher *impl = impl_from_ICoreDispatcher( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->weakref)
        {
            impl->weakref->target = NULL;
            cd_wr_Release( impl->weakref );
            impl->weakref = NULL;
        }
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI core_dispatcher_GetIids( ICoreDispatcher *iface, ULONG *count, IID **iids ) { FIXME("stub\n"); return E_NOTIMPL; }
static HRESULT WINAPI core_dispatcher_GetRuntimeClassName( ICoreDispatcher *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreDispatcher", 30, name ); }
static HRESULT WINAPI core_dispatcher_GetTrustLevel( ICoreDispatcher *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }static HRESULT WINAPI core_dispatcher_get_AutomationProviderDelegate( ICoreDispatcher *iface, IInspectable **value )
{ FIXME( "stub\n" ); if (value) *value = NULL; return S_OK; }
static HRESULT WINAPI core_dispatcher_put_AutomationProviderDelegate( ICoreDispatcher *iface, IInspectable *value )
{ FIXME( "stub\n" ); return S_OK; }
static HRESULT WINAPI core_dispatcher_AcceleratorKeyActivated_add( ICoreDispatcher *iface, void *handler, EventRegistrationToken *cookie )
{ FIXME( "stub\n" ); if (cookie) cookie->value = 0; return S_OK; }
static HRESULT WINAPI core_dispatcher_AcceleratorKeyActivated_remove( ICoreDispatcher *iface, EventRegistrationToken cookie )
{ FIXME( "stub\n" ); return S_OK; }
static HRESULT WINAPI core_dispatcher_ProcessEvents( ICoreDispatcher *iface, CoreProcessEventsOption options )
{
    MSG msg;
    TRACE( "iface %p, options %d\n", iface, options );

    if (options == CoreProcessEventsOption_ProcessUntilQuit)
    {
        while (GetMessageW( &msg, NULL, 0, 0 ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
    }
    else if (options == CoreProcessEventsOption_ProcessOneIfPresent)
    {
        if (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
    }
    else
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT) break;
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
    }
    return S_OK;
}

struct dispatcher_async_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
};

static inline struct dispatcher_async_action *impl_from_dispatcher_IAsyncAction( IAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_async_action, IAsyncAction_iface );
}

static inline struct dispatcher_async_action *impl_from_dispatcher_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_async_action, IAsyncInfo_iface );
}

static HRESULT WINAPI dispatcher_async_action_QueryInterface( IAsyncAction *iface, REFIID iid, void **out )
{
    struct dispatcher_async_action *impl = impl_from_dispatcher_IAsyncAction( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IAsyncAction_iface;
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IAsyncInfo_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dispatcher_async_action_AddRef( IAsyncAction *iface )
{
    return InterlockedIncrement( &impl_from_dispatcher_IAsyncAction( iface )->ref );
}
static ULONG WINAPI dispatcher_async_action_Release( IAsyncAction *iface )
{
    struct dispatcher_async_action *impl = impl_from_dispatcher_IAsyncAction( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI dispatcher_async_action_GetIids( IAsyncAction *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_async_action_GetRuntimeClassName( IAsyncAction *iface, HSTRING *name ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_async_action_GetTrustLevel( IAsyncAction *iface, TrustLevel *level ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_async_action_put_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler *handler )
{
    if (handler)
        handler->lpVtbl->Invoke( handler, iface, 1 /* Completed */ );
    return S_OK;
}
static HRESULT WINAPI dispatcher_async_action_get_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}
static HRESULT WINAPI dispatcher_async_action_GetResults( IAsyncAction *iface ) { return S_OK; }

static const struct IAsyncActionVtbl dispatcher_async_action_vtbl =
{
    dispatcher_async_action_QueryInterface,
    dispatcher_async_action_AddRef,
    dispatcher_async_action_Release,
    dispatcher_async_action_GetIids,
    dispatcher_async_action_GetRuntimeClassName,
    dispatcher_async_action_GetTrustLevel,
    dispatcher_async_action_put_Completed,
    dispatcher_async_action_get_Completed,
    dispatcher_async_action_GetResults,
};

static HRESULT WINAPI dispatcher_async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return dispatcher_async_action_QueryInterface( &impl_from_dispatcher_IAsyncInfo( iface )->IAsyncAction_iface, iid, out );
}
static ULONG WINAPI dispatcher_async_info_AddRef( IAsyncInfo *iface )
{
    return dispatcher_async_action_AddRef( &impl_from_dispatcher_IAsyncInfo( iface )->IAsyncAction_iface );
}
static ULONG WINAPI dispatcher_async_info_Release( IAsyncInfo *iface )
{
    return dispatcher_async_action_Release( &impl_from_dispatcher_IAsyncInfo( iface )->IAsyncAction_iface );
}
static HRESULT WINAPI dispatcher_async_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_async_info_get_Id( IAsyncInfo *iface, unsigned int *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI dispatcher_async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status ) { if (status) *status = 1 /* Completed */; return S_OK; }
static HRESULT WINAPI dispatcher_async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *code ) { if (code) *code = S_OK; return S_OK; }
static HRESULT WINAPI dispatcher_async_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI dispatcher_async_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const struct IAsyncInfoVtbl dispatcher_async_info_vtbl =
{
    dispatcher_async_info_QueryInterface,
    dispatcher_async_info_AddRef,
    dispatcher_async_info_Release,
    dispatcher_async_info_GetIids,
    dispatcher_async_info_GetRuntimeClassName,
    dispatcher_async_info_GetTrustLevel,
    dispatcher_async_info_get_Id,
    dispatcher_async_info_get_Status,
    dispatcher_async_info_get_ErrorCode,
    dispatcher_async_info_Cancel,
    dispatcher_async_info_Close,
};

static IAsyncAction *dispatcher_async_action_create(void)
{
    struct dispatcher_async_action *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IAsyncAction_iface.lpVtbl = &dispatcher_async_action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &dispatcher_async_info_vtbl;
    impl->ref = 1;
    return &impl->IAsyncAction_iface;
}

/* Owner-thread call: run inline. Foreign call: hand the callback to the owner thread's queue via
 * its window; it runs when the owner pumps (ProcessEvents, and the Run/ProcessUntilQuit loops).
 * With no window or a dead queue to post to, inline is the only way the callback is not lost. */
static BOOL strict_dispatch_enabled( void )
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *v = getenv( "WINE_COREWINDOW_STRICT_DISPATCH" );
        if (v)
            cached = strcmp( v, "0" ) != 0;
        else
            cached = GetModuleHandleW( L"Windows.UI.Xaml.dll" ) != NULL &&
                     GetModuleHandleW( L"Microsoft.WindowsAppRuntime.dll" ) == NULL;
    }
    return cached;
}

static const GUID IID_IIdleDispatchedHandlerArgs_local =
    { 0x98bb6a24, 0xdc1c, 0x43cb, { 0xb4, 0xed, 0xd1, 0xc0, 0xeb, 0x23, 0x91, 0xf3 } };

static HRESULT WINAPI idle_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIdleDispatchedHandlerArgs_local ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI idle_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs *iface ) { return 2; }
static ULONG WINAPI idle_args_Release( __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs *iface ) { return 1; }
static HRESULT WINAPI idle_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI idle_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.UI.Core.IdleDispatchedHandlerArgs";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI idle_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI idle_args_get_IsDispatcherIdle( __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}
static const struct __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgsVtbl idle_args_vtbl =
{
    idle_args_QueryInterface,
    idle_args_AddRef,
    idle_args_Release,
    idle_args_GetIids,
    idle_args_GetRuntimeClassName,
    idle_args_GetTrustLevel,
    idle_args_get_IsDispatcherIdle,
};
static __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandlerArgs idle_args_obj = { &idle_args_vtbl };

/* The callback is the RunAsync/RunIdleAsync argument — an IDispatchedHandler (Invoke()) or an
 * IIdleDispatchedHandler (Invoke(e)); the idle flag stored with it tells the two faces apart. */
static void dispatcher_invoke( void *handler, BOOL idle )
{
    HRESULT hr;
    LONG n;
    {
        static LONG ui_cnt;
        n = InterlockedIncrement( &ui_cnt );
        if (n <= 400) UIDBG( "invoke#%ld tid %04x handler %p idle %d\n", (long)n, (unsigned)GetCurrentThreadId(), handler, idle );
    }
    if (idle)
    {
        __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandler *h =
                (__x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandler *)handler;
        hr = h->lpVtbl->Invoke( h, &idle_args_obj );
        if (FAILED(hr) && n <= 400) UIDBG( "invoke#%ld idle returned hr=%#lx\n", (long)n, (unsigned long)hr );
        return;
    }
    hr = ((__x_ABI_CWindows_CUI_CCore_CIDispatchedHandler *)handler)->lpVtbl->Invoke(
            (__x_ABI_CWindows_CUI_CCore_CIDispatchedHandler *)handler );
    if (FAILED(hr) && n <= 400) UIDBG( "invoke#%ld returned hr=%#lx\n", (long)n, (unsigned long)hr );
}

static void dispatcher_run_or_post( struct core_dispatcher *impl, void *agileCallback, BOOL idle )
{
    /* Live bisect (WhatsApp Desktop p53 wave, gate57/58): queue-routed foreign RunAsync was the
     * 1.10-vs-1.11 behavior delta that blocked the unpackaged WinUI3 client's managed init; the
     * 1.10 baseline runs every callback inline on the caller.  The window-queue route is opt-in
     * via WINE_COREWINDOW_STRICT_DISPATCH; the owner_tid recording stays live in both modes. */
    if (!strict_dispatch_enabled() || GetCurrentThreadId() == impl->owner_tid)
    {
        dispatcher_invoke( agileCallback, idle );
        return;
    }
    if (impl->window && impl->window->hwnd)
    {
        struct dispatcher_post *post = malloc( sizeof(*post) );
        if (post)
        {
            post->handler = agileCallback;
            post->idle = idle;
            IUnknown_AddRef( (IUnknown *)agileCallback );
            if (PostMessageW( impl->window->hwnd, WM_CORE_DISPATCHER_POST, 0, (LPARAM)post ))
            {
                TRACE( "dispatcher %p: posted callback %p (idle %d) to owner tid %lu.\n",
                       impl, agileCallback, idle, impl->owner_tid );
                return;
            }
            free( post );
            IUnknown_Release( (IUnknown *)agileCallback );
        }
    }
    dispatcher_invoke( agileCallback, idle );
}

static HRESULT WINAPI core_dispatcher_RunAsync( ICoreDispatcher *iface, CoreDispatcherPriority priority,
        __x_ABI_CWindows_CUI_CCore_CIDispatchedHandler *agileCallback, IAsyncAction **asyncAction )
{
    struct core_dispatcher *impl = impl_from_ICoreDispatcher( iface );

    TRACE( "iface %p, priority %d, agileCallback %p, asyncAction %p\n", iface, priority, agileCallback, asyncAction );
    UIDBG( "RunAsync tid %04x prio %d cb %p\n", (unsigned)GetCurrentThreadId(), priority, (void*)agileCallback );
    if (agileCallback) dispatcher_run_or_post( impl, agileCallback, FALSE );
    if (asyncAction)
    {
        *asyncAction = dispatcher_async_action_create();
    }
    return S_OK;
}

static HRESULT WINAPI core_dispatcher_RunIdleAsync( ICoreDispatcher *iface,
        __x_ABI_CWindows_CUI_CCore_CIIdleDispatchedHandler *agileCallback, IAsyncAction **asyncAction )
{
    struct core_dispatcher *impl = impl_from_ICoreDispatcher( iface );

    TRACE( "iface %p, agileCallback %p, asyncAction %p\n", iface, agileCallback, asyncAction );
    if (agileCallback) dispatcher_run_or_post( impl, agileCallback, TRUE );
    if (asyncAction)
    {
        *asyncAction = dispatcher_async_action_create();
    }
    return S_OK;
}
static HRESULT WINAPI core_dispatcher_HasThreadAccess( ICoreDispatcher *iface, boolean *value )
{
    struct core_dispatcher *impl = impl_from_ICoreDispatcher( iface );

    TRACE( "iface %p, owner tid %lu, current tid %lu.\n", iface, impl->owner_tid, GetCurrentThreadId() );
    if (!value) return E_POINTER;
    *value = !strict_dispatch_enabled() || GetCurrentThreadId() == impl->owner_tid;  /* 1.10 stub shape (TRUE) when not strict */
    return S_OK;
}
static HRESULT WINAPI core_dispatcher_get_CurrentPriority( ICoreDispatcher *iface, CoreDispatcherPriority *value )
{ FIXME( "stub\n" ); if (value) *value = CoreDispatcherPriority_Normal; return S_OK; }
static HRESULT WINAPI core_dispatcher_put_CurrentPriority( ICoreDispatcher *iface, CoreDispatcherPriority value )
{ FIXME( "stub\n" ); return S_OK; }
static HRESULT WINAPI core_dispatcher_StopProcessEvents( ICoreDispatcher *iface )
{ FIXME( "stub\n" ); return S_OK; }

static const struct ICoreDispatcherVtbl core_dispatcher_vtbl =
{
    core_dispatcher_QueryInterface,
    core_dispatcher_AddRef,
    core_dispatcher_Release,
    core_dispatcher_GetIids,
    core_dispatcher_GetRuntimeClassName,
    core_dispatcher_GetTrustLevel,
    core_dispatcher_HasThreadAccess,
    core_dispatcher_ProcessEvents,
    core_dispatcher_RunAsync,
    core_dispatcher_RunIdleAsync,
};

static ICoreDispatcher *core_dispatcher_create( struct core_window *window )
{
    struct core_dispatcher *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICoreDispatcher_iface.lpVtbl = &core_dispatcher_vtbl;
    impl->ICoreAcceleratorKeys_iface.lpVtbl = &core_dispatcher_keys_vtbl;
    impl->IWeakReferenceSource_iface = cd_wrs_vtbl;
    impl->window = window;
    impl->owner_tid = GetCurrentThreadId();   /* the creating thread is the dispatcher's and window's owner */
    impl->ref = 1;
    return &impl->ICoreDispatcher_iface;
}

static inline struct core_window *impl_from_dummy( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct core_window, dummy_iface );
}

static HRESULT WINAPI dummy_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct core_window *impl = impl_from_dummy( iface );
    return ICoreWindow_QueryInterface( &impl->ICoreWindow_iface, iid, out );
}
static ULONG WINAPI dummy_AddRef( IInspectable *iface )
{
    struct core_window *impl = impl_from_dummy( iface );
    return ICoreWindow_AddRef( &impl->ICoreWindow_iface );
}
static ULONG WINAPI dummy_Release( IInspectable *iface )
{
    struct core_window *impl = impl_from_dummy( iface );
    return ICoreWindow_Release( &impl->ICoreWindow_iface );
}
static HRESULT WINAPI dummy_GetIids( IInspectable *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI dummy_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreWindow", 26, name ); }
static HRESULT WINAPI dummy_GetTrustLevel( IInspectable *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI dummy_event_add( void *iface, void *handler, EventRegistrationToken *cookie )
{
    if (cookie) cookie->value = 1;
    return S_OK;
}
static HRESULT WINAPI dummy_event_remove( void *iface, EventRegistrationToken cookie )
{
    return S_OK;
}

static void *dummy_vtbl[] =
{
    dummy_QueryInterface,
    dummy_AddRef,
    dummy_Release,
    dummy_GetIids,
    dummy_GetRuntimeClassName,
    dummy_GetTrustLevel,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
    dummy_event_add, dummy_event_remove,
};

static inline struct core_window *impl_from_ICoreWindow( ICoreWindow *iface )
{
    return CONTAINING_RECORD( iface, struct core_window, ICoreWindow_iface );
}

static inline struct core_window *impl_from_ICoreWindowInterop( ICoreWindowInterop *iface )
{
    return CONTAINING_RECORD( iface, struct core_window, ICoreWindowInterop_iface );
}

static HRESULT WINAPI core_window_interop_QueryInterface( ICoreWindowInterop *iface, REFIID iid, void **out )
{
    struct core_window *impl = impl_from_ICoreWindowInterop( iface );
    return ICoreWindow_QueryInterface( &impl->ICoreWindow_iface, iid, out );
}
static ULONG WINAPI core_window_interop_AddRef( ICoreWindowInterop *iface )
{
    struct core_window *impl = impl_from_ICoreWindowInterop( iface );
    return ICoreWindow_AddRef( &impl->ICoreWindow_iface );
}
static ULONG WINAPI core_window_interop_Release( ICoreWindowInterop *iface )
{
    struct core_window *impl = impl_from_ICoreWindowInterop( iface );
    return ICoreWindow_Release( &impl->ICoreWindow_iface );
}
static HRESULT WINAPI core_window_interop_GetWindowHandle( ICoreWindowInterop *iface, HWND *hwnd )
{
    struct core_window *impl = impl_from_ICoreWindowInterop( iface );
    if (!hwnd) return E_POINTER;
    *hwnd = impl->hwnd;
    return S_OK;
}
static HRESULT WINAPI core_window_interop_SetMessageHandled( ICoreWindowInterop *iface, boolean value )
{
    return S_OK;
}

static const struct ICoreWindowInteropVtbl core_window_interop_vtbl =
{
    core_window_interop_QueryInterface,
    core_window_interop_AddRef,
    core_window_interop_Release,
    core_window_interop_GetWindowHandle,
    core_window_interop_SetMessageHandled,
};

struct core_window_weakref
{
    const void **lpVtbl;
    LONG ref;
    struct core_window *target;
};

static const GUID IID_IWeakReference_cw =
    { 0x00000037, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IWeakReferenceSource_cw =
    { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

static HRESULT WINAPI cw_wr_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_window_weakref *wr = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWeakReference_cw ))
    {
        InterlockedIncrement( &wr->ref );
        *out = wr;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_wr_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct core_window_weakref *)iface)->ref );
}
static ULONG WINAPI cw_wr_Release( void *iface )
{
    struct core_window_weakref *wr = iface;
    ULONG ref = InterlockedDecrement( &wr->ref );
    if (!ref) free( wr );
    return ref;
}
static HRESULT WINAPI cw_wr_Resolve( void *iface, REFIID iid, IInspectable **out )
{
    struct core_window_weakref *wr = iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!wr->target) return S_OK;
    return ICoreWindow_QueryInterface( &wr->target->ICoreWindow_iface, iid, (void **)out );
}
static const void *cw_wr_vtbl[] =
{
    cw_wr_QueryInterface,
    cw_wr_AddRef,
    cw_wr_Release,
    cw_wr_Resolve,
};

static inline struct core_window *impl_from_IWeakReferenceSource_cw( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_window, IWeakReferenceSource_iface );
}
static HRESULT WINAPI cw_wrs_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_window *impl = impl_from_IWeakReferenceSource_cw( iface );
    return ICoreWindow_QueryInterface( &impl->ICoreWindow_iface, iid, out );
}
static ULONG WINAPI cw_wrs_AddRef( void *iface )
{
    struct core_window *impl = impl_from_IWeakReferenceSource_cw( iface );
    return ICoreWindow_AddRef( &impl->ICoreWindow_iface );
}
static ULONG WINAPI cw_wrs_Release( void *iface )
{
    struct core_window *impl = impl_from_IWeakReferenceSource_cw( iface );
    return ICoreWindow_Release( &impl->ICoreWindow_iface );
}
static HRESULT WINAPI cw_wrs_GetWeakReference( void *iface, void **out )
{
    struct core_window *impl = impl_from_IWeakReferenceSource_cw( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl->weakref)
    {
        struct core_window_weakref *wr = calloc( 1, sizeof(*wr) );
        if (!wr) return E_OUTOFMEMORY;
        wr->lpVtbl = cw_wr_vtbl;
        wr->ref = 1;
        wr->target = impl;
        impl->weakref = wr;
    }
    InterlockedIncrement( &impl->weakref->ref );
    *out = impl->weakref;
    return S_OK;
}
static const void *cw_wrs_vtbl[] =
{
    cw_wrs_QueryInterface,
    cw_wrs_AddRef,
    cw_wrs_Release,
    cw_wrs_GetWeakReference,
};

static HRESULT WINAPI core_window_QueryInterface( ICoreWindow *iface, REFIID iid, void **out )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICoreWindow ))
    {
        ICoreWindow_AddRef( &impl->ICoreWindow_iface );
        *out = &impl->ICoreWindow_iface;
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_ICoreWindowInterop ) ||
             !memcmp( iid, "\x29\x4a\xd6\x45\x3e\xa6\xb6\x4c\xb4\x98\x57\x81\xd2\x98\xcb\x4f", 16 ))
    {
        ICoreWindow_AddRef( &impl->ICoreWindow_iface );
        *out = &impl->ICoreWindowInterop_iface;
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_ICoreWindowNavigation ))
    {
        ICoreWindow_AddRef( &impl->ICoreWindow_iface );
        *out = &impl->ICoreWindowNavigation_iface;
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_ICoreWindowAdapterInterop ))
    {
        ICoreWindow_AddRef( &impl->ICoreWindow_iface );
        *out = &impl->ICoreWindowAdapterInterop_iface;
        return S_OK;
    }
    else if (!memcmp( iid, "\x3d\x7e\xa1\x42\x71\x71\x9a\x43\xb1\xfa\xa3\x1b\x7b\x95\x74\x89", 16 ) ||
             !memcmp( iid, "\xd8\x79\x27\xc1\xd2\x85\xe5\x43\x90\x1a\x95\xdd\x4f\x8e\xcb\xa3", 16 ) ||
             !memcmp( iid, "\x05\x2a\x88\x5f\x94\xd5\xbc\x48\x9a\x5a\xfd\x3b\x9c\xe9\x03\x90", 16 ) ||
             !memcmp( iid, "\x85\x1b\x2b\x7c\x17\x69\x61\x43\x9c\x02\x0d\x9e\x3a\x42\x0b\x95", 16 ) ||
             !memcmp( iid, "\xd8\x0d\xc2\x32\xef\xfa\x75\x43\xa2\xab\x32\x64\x0e\x48\x15\xc7", 16 ) ||
             !memcmp( iid, "\xd0\xf0\xca\x35\xf0\x47\x6c\x43\xaf\x97\x0d\xd8\x8f\x6f\x5f\x02", 16 ) ||
             !memcmp( iid, "\xe1\xe1\x4a\x4b\x6d\x2e\xaa\x4e\xbd\xa1\x1c\x5c\xc1\xbe\xe1\x41", 16 ))
    {
        ICoreWindow_AddRef( &impl->ICoreWindow_iface );
        *out = &impl->dummy_iface;
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IWeakReferenceSource_cw ))
    {
        ICoreWindow_AddRef( &impl->ICoreWindow_iface );
        *out = &impl->IWeakReferenceSource_iface;
        return S_OK;
    }
    FIXME( "%s E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI core_window_AddRef( ICoreWindow *iface )
{ return InterlockedIncrement( &impl_from_ICoreWindow( iface )->ref ); }

static ULONG WINAPI core_window_Release( ICoreWindow *iface )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->weakref)
        {
            impl->weakref->target = NULL;
            cw_wr_Release( impl->weakref );
            impl->weakref = NULL;
        }
        if (thread_core_window == impl) thread_core_window = NULL;
        if (impl->hwnd) DestroyWindow( impl->hwnd );
        if (impl->dispatcher) ICoreDispatcher_Release( impl->dispatcher );
        if (impl->navigation_client) IUnknown_Release( impl->navigation_client );
        if (impl->window_client_adapter) IUnknown_Release( impl->window_client_adapter );
        if (impl->visibility_changed_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_Release( impl->visibility_changed_handler );
        {
            unsigned int i;
            for (i = 0; i < ARRAY_SIZE( impl->size_changed_handlers ); i++)
            {
                if (impl->size_changed_handlers[i])
                    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_Release( impl->size_changed_handlers[i] );
            }
        }
        if (impl->activated_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs_Release( impl->activated_handler );
        {
            unsigned int i;
            for (i = 0; i < ARRAY_SIZE( impl->character_received_handlers ); i++)
            {
                if (impl->character_received_handlers[i])
                    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Release( impl->character_received_handlers[i] );
            }
            for (i = 0; i < ARRAY_SIZE( impl->keydown_handlers ); i++)
            {
                if (impl->keydown_handlers[i])
                    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keydown_handlers[i] );
            }
            for (i = 0; i < ARRAY_SIZE( impl->keyup_handlers ); i++)
            {
                if (impl->keyup_handlers[i])
                    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keyup_handlers[i] );
            }
        }
        if (impl->pointer_pressed_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Release( impl->pointer_pressed_handler );
        if (impl->pointer_released_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Release( impl->pointer_released_handler );
        if (impl->custom_properties)
            __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet_Release( impl->custom_properties );
        if (impl->pointer_cursor)
            ICoreCursor_Release( impl->pointer_cursor );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI core_window_GetIids( ICoreWindow *iface, ULONG *count, IID **iids ) { FIXME("stub\n"); return E_NOTIMPL; }
static HRESULT WINAPI core_window_GetRuntimeClassName( ICoreWindow *iface, HSTRING *name ) { FIXME("stub\n"); return E_NOTIMPL; }
static HRESULT WINAPI core_window_GetTrustLevel( ICoreWindow *iface, TrustLevel *level ) { FIXME("stub\n"); return E_NOTIMPL; }
static HRESULT WINAPI core_window_get_AutomationHostProvider( ICoreWindow *iface, IInspectable **value )
{ FIXME( "stub\n" ); if (value) *value = NULL; return S_OK; }
static HRESULT WINAPI core_window_get_Bounds( ICoreWindow *iface, Rect *value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    RECT r;
    if (!value) return E_POINTER;
    if (impl->hwnd && GetClientRect( impl->hwnd, &r ))
    {
        value->X = r.left;
        value->Y = r.top;
        value->Width = r.right - r.left;
        value->Height = r.bottom - r.top;
    }
    else
    {
        SIZE work_area = get_work_area_size();
        value->X = 0; value->Y = 0;
        value->Width = work_area.cx;
        value->Height = work_area.cy;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_get_CustomProperties( ICoreWindow *iface, __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet **value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if (!impl->custom_properties)
    {
        HSTRING class_str;
        IInspectable *insp = NULL;
        if (SUCCEEDED( WindowsCreateString( L"Windows.Foundation.Collections.PropertySet", 42, &class_str ) ))
        {
            if (SUCCEEDED( RoActivateInstance( class_str, &insp ) ) && insp)
            {
                __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet *ps = NULL;
                if (SUCCEEDED( IInspectable_QueryInterface( insp, &IID_IPropertySet, (void **)&ps ) ) && ps)
                {
                    if (InterlockedCompareExchangePointer( (void **)&impl->custom_properties, ps, NULL ))
                        __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet_Release( ps );
                }
                IInspectable_Release( insp );
            }
            WindowsDeleteString( class_str );
        }
    }
    if (impl->custom_properties)
        __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet_AddRef( impl->custom_properties );
    *value = impl->custom_properties;
    return S_OK;
}
static HRESULT WINAPI core_window_get_Dispatcher( ICoreWindow *iface, ICoreDispatcher **value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p semi-stub.\n", iface );
    if (value)
    {
        if (impl->dispatcher) ICoreDispatcher_AddRef( impl->dispatcher );
        *value = impl->dispatcher;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_get_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection *value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->flow_direction;
    return S_OK;
}
static HRESULT WINAPI core_window_put_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );

    TRACE( "iface %p, value %d.\n", iface, value );
    if (value != CoreWindowFlowDirection_LeftToRight && value != CoreWindowFlowDirection_RightToLeft)
        return E_INVALIDARG;
    impl->flow_direction = value;
    return S_OK;
}
static HRESULT WINAPI core_window_get_IsInputEnabled( ICoreWindow *iface, boolean *value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->input_enabled;
    return S_OK;
}
static HRESULT WINAPI core_window_put_IsInputEnabled( ICoreWindow *iface, boolean value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );

    TRACE( "iface %p, value %d.\n", iface, value );
    impl->input_enabled = !!value;
    if (impl->hwnd && IsWindow( impl->hwnd ) && !!IsWindowEnabled( impl->hwnd ) != impl->input_enabled)
        EnableWindow( impl->hwnd, impl->input_enabled );
    return S_OK;
}
static HRESULT WINAPI core_window_get_PointerCursor( ICoreWindow *iface, ICoreCursor **value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if (!impl->pointer_cursor)
    {
        ICoreCursor *cur = NULL;
        if (SUCCEEDED( cursor_factory_CreateCursor( NULL, CoreCursorType_Arrow, 0, &cur ) ) && cur)
        {
            if (InterlockedCompareExchangePointer( (void **)&impl->pointer_cursor, cur, NULL ))
                ICoreCursor_Release( cur );
        }
    }
    if (impl->pointer_cursor) ICoreCursor_AddRef( impl->pointer_cursor );
    *value = impl->pointer_cursor;
    return S_OK;
}
static HRESULT WINAPI core_window_put_PointerCursor( ICoreWindow *iface, ICoreCursor *value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) ICoreCursor_AddRef( value );
    if (impl->pointer_cursor) ICoreCursor_Release( impl->pointer_cursor );
    impl->pointer_cursor = value;
    return S_OK;
}
static HRESULT WINAPI core_window_get_PointerPosition( ICoreWindow *iface, Point *value )
{ FIXME( "stub\n" ); if (value) memset( value, 0, sizeof(*value) ); return S_OK; }
static HRESULT WINAPI core_window_get_Visible( ICoreWindow *iface, boolean *value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (!value) return E_POINTER;
    *value = impl->hwnd ? IsWindowVisible( impl->hwnd ) : TRUE;
    return S_OK;
}
static __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *visibility_changed_args_create( boolean visible );
static void core_window_deliver_activation( struct core_window *impl, CoreWindowActivationState state );

static HRESULT WINAPI core_window_Activate( ICoreWindow *iface )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, hwnd %p\n", iface, impl->hwnd );
    if (impl->hwnd)
    {
        ShowWindow( impl->hwnd, SW_SHOW );
        SetForegroundWindow( impl->hwnd );
        UpdateWindow( impl->hwnd );
        /* If window was already active, win32u set_active_window early-outs
         * (previous == hwnd) without sending WM_ACTIVATE. Queue a synthetic
         * WM_ACTIVATE and deliver the activation event to the subscriber. */
        PostMessageW( impl->hwnd, WM_ACTIVATE, MAKEWPARAM( WA_ACTIVE, 0 ), 0 );
        if (impl->activated_handler)
            core_window_deliver_activation( impl, CoreWindowActivationState_CodeActivated );
    }
    return S_OK;
}
static HRESULT WINAPI core_window_Close( ICoreWindow *iface )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, hwnd %p\n", iface, impl->hwnd );
    if (impl->hwnd) PostMessageW( impl->hwnd, WM_CLOSE, 0, 0 );
    return S_OK;
}
static HRESULT WINAPI core_window_GetAsyncKeyState( ICoreWindow *iface, __x_ABI_CWindows_CSystem_CVirtualKey key, CoreVirtualKeyStates *state )
{
    SHORT ks;
    if (!state) return E_POINTER;
    ks = GetAsyncKeyState( (int)key );
    *state = (ks & 0x8000) ? CoreVirtualKeyStates_Down : CoreVirtualKeyStates_None;
    return S_OK;
}
static HRESULT WINAPI core_window_GetKeyState( ICoreWindow *iface, __x_ABI_CWindows_CSystem_CVirtualKey key, CoreVirtualKeyStates *state )
{
    SHORT ks;
    CoreVirtualKeyStates s = CoreVirtualKeyStates_None;
    if (!state) return E_POINTER;
    ks = GetKeyState( (int)key );
    if (ks & 0x8000) s |= CoreVirtualKeyStates_Down;
    if (ks & 1) s |= CoreVirtualKeyStates_Locked;
    *state = s;
    return S_OK;
}
static HRESULT WINAPI core_window_ReleasePointerCapture( ICoreWindow *iface )
{ FIXME( "stub\n" ); return S_OK; }
static HRESULT WINAPI core_window_SetPointerCapture( ICoreWindow *iface )
{ FIXME( "stub\n" ); return S_OK; }

/* event add/remove — semi-stub: accept + return token 0 */
#define CORE_WINDOW_EVENT_ADD(name, argtype) \
    static HRESULT WINAPI core_window_add_##name( ICoreWindow *iface, \
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_##argtype *handler, EventRegistrationToken *cookie ) \
    { FIXME( "iface %p semi-stub.\n", iface ); if (cookie) cookie->value = 0; return S_OK; }
#define CORE_WINDOW_EVENT_REMOVE(name) \
    static HRESULT WINAPI core_window_remove_##name( ICoreWindow *iface, EventRegistrationToken cookie ) \
    { FIXME( "iface %p semi-stub.\n", iface ); return S_OK; }

CORE_WINDOW_EVENT_ADD( AutomationProviderRequested, Windows__CUI__CCore__CAutomationProviderRequestedEventArgs )
CORE_WINDOW_EVENT_REMOVE( AutomationProviderRequested )

static HRESULT WINAPI core_window_add_CharacterReceived( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 0;
    if (!handler) return S_OK;
    for (i = 0; i < ARRAY_SIZE( impl->character_received_handlers ); i++)
    {
        if (!impl->character_received_handlers[i])
        {
            INT64 tok = ++impl->next_char_token;
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_AddRef( handler );
            impl->character_received_handlers[i] = handler;
            impl->character_received_tokens[i] = tok;
            if (cookie) cookie->value = tok;
            return S_OK;
        }
    }
    return E_OUTOFMEMORY;
}
static HRESULT WINAPI core_window_remove_CharacterReceived( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE( impl->character_received_handlers ); i++)
    {
        if (impl->character_received_handlers[i] && impl->character_received_tokens[i] == cookie.value)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Release( impl->character_received_handlers[i] );
            impl->character_received_handlers[i] = NULL;
            impl->character_received_tokens[i] = 0;
            break;
        }
    }
    return S_OK;
}

CORE_WINDOW_EVENT_ADD( Closed, Windows__CUI__CCore__CCoreWindowEventArgs )
CORE_WINDOW_EVENT_REMOVE( Closed )
CORE_WINDOW_EVENT_ADD( InputEnabled, Windows__CUI__CCore__CInputEnabledEventArgs )
CORE_WINDOW_EVENT_REMOVE( InputEnabled )

static HRESULT WINAPI core_window_add_KeyDown( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 0;
    if (!handler) return S_OK;
    for (i = 0; i < ARRAY_SIZE( impl->keydown_handlers ); i++)
    {
        if (!impl->keydown_handlers[i])
        {
            INT64 tok = ++impl->next_keydown_token;
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_AddRef( handler );
            impl->keydown_handlers[i] = handler;
            impl->keydown_tokens[i] = tok;
            if (cookie) cookie->value = tok;
            return S_OK;
        }
    }
    return E_OUTOFMEMORY;
}
static HRESULT WINAPI core_window_remove_KeyDown( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE( impl->keydown_handlers ); i++)
    {
        if (impl->keydown_handlers[i] && impl->keydown_tokens[i] == cookie.value)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keydown_handlers[i] );
            impl->keydown_handlers[i] = NULL;
            impl->keydown_tokens[i] = 0;
            break;
        }
    }
    return S_OK;
}

static HRESULT WINAPI core_window_add_KeyUp( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 0;
    if (!handler) return S_OK;
    for (i = 0; i < ARRAY_SIZE( impl->keyup_handlers ); i++)
    {
        if (!impl->keyup_handlers[i])
        {
            INT64 tok = ++impl->next_keyup_token;
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_AddRef( handler );
            impl->keyup_handlers[i] = handler;
            impl->keyup_tokens[i] = tok;
            if (cookie) cookie->value = tok;
            return S_OK;
        }
    }
    return E_OUTOFMEMORY;
}
static HRESULT WINAPI core_window_remove_KeyUp( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE( impl->keyup_handlers ); i++)
    {
        if (impl->keyup_handlers[i] && impl->keyup_tokens[i] == cookie.value)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keyup_handlers[i] );
            impl->keyup_handlers[i] = NULL;
            impl->keyup_tokens[i] = 0;
            break;
        }
    }
    return S_OK;
}

CORE_WINDOW_EVENT_ADD( PointerCaptureLost, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerCaptureLost )
CORE_WINDOW_EVENT_ADD( PointerEntered, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerEntered )
CORE_WINDOW_EVENT_ADD( PointerExited, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerExited )
CORE_WINDOW_EVENT_ADD( PointerMoved, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerMoved )

static HRESULT WINAPI core_window_add_PointerPressed( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_AddRef( handler );
        impl->pointer_pressed_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_PointerPressed( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (impl->pointer_pressed_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Release( impl->pointer_pressed_handler );
        impl->pointer_pressed_handler = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI core_window_add_PointerReleased( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_AddRef( handler );
        impl->pointer_released_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_PointerReleased( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (impl->pointer_released_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Release( impl->pointer_released_handler );
        impl->pointer_released_handler = NULL;
    }
    return S_OK;
}
CORE_WINDOW_EVENT_ADD( TouchHitTesting, Windows__CUI__CCore__CTouchHitTestingEventArgs )
CORE_WINDOW_EVENT_REMOVE( TouchHitTesting )
CORE_WINDOW_EVENT_ADD( PointerWheelChanged, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerWheelChanged )
struct visibility_changed_args
{
    __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs IVisibilityChangedEventArgs_iface;
    LONG ref;
    boolean visible;
};

static inline struct visibility_changed_args *impl_from_IVisibilityChangedEventArgs( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct visibility_changed_args, IVisibilityChangedEventArgs_iface );
}

static HRESULT WINAPI visibility_changed_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface, REFIID iid, void **out )
{
    struct visibility_changed_args *impl = impl_from_IVisibilityChangedEventArgs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IVisibilityChangedEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI visibility_changed_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface )
{
    struct visibility_changed_args *impl = impl_from_IVisibilityChangedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI visibility_changed_args_Release( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface )
{
    struct visibility_changed_args *impl = impl_from_IVisibilityChangedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI visibility_changed_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface, ULONG *count, IID **iids )
{ return E_NOTIMPL; }
static HRESULT WINAPI visibility_changed_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.VisibilityChangedEventArgs", 41, name ); }
static HRESULT WINAPI visibility_changed_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI visibility_changed_args_get_Visible( __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *iface, boolean *value )
{
    struct visibility_changed_args *impl = impl_from_IVisibilityChangedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->visible;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgsVtbl visibility_changed_args_vtbl =
{
    visibility_changed_args_QueryInterface,
    visibility_changed_args_AddRef,
    visibility_changed_args_Release,
    visibility_changed_args_GetIids,
    visibility_changed_args_GetRuntimeClassName,
    visibility_changed_args_GetTrustLevel,
    visibility_changed_args_get_Visible,
};

static __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *visibility_changed_args_create( boolean visible )
{
    struct visibility_changed_args *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IVisibilityChangedEventArgs_iface.lpVtbl = &visibility_changed_args_vtbl;
    impl->ref = 1;
    impl->visible = visible;
    return &impl->IVisibilityChangedEventArgs_iface;
}

struct window_size_changed_args
{
    __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs IWindowSizeChangedEventArgs_iface;
    LONG ref;
    Size size;
};

static inline struct window_size_changed_args *impl_from_IWindowSizeChangedEventArgs( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct window_size_changed_args, IWindowSizeChangedEventArgs_iface );
}

static HRESULT WINAPI window_size_changed_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface, REFIID iid, void **out )
{
    struct window_size_changed_args *impl = impl_from_IWindowSizeChangedEventArgs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IWindowSizeChangedEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI window_size_changed_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface )
{
    struct window_size_changed_args *impl = impl_from_IWindowSizeChangedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI window_size_changed_args_Release( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface )
{
    struct window_size_changed_args *impl = impl_from_IWindowSizeChangedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI window_size_changed_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface, ULONG *count, IID **iids )
{ return E_NOTIMPL; }
static HRESULT WINAPI window_size_changed_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.WindowSizeChangedEventArgs", 41, name ); }
static HRESULT WINAPI window_size_changed_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI window_size_changed_args_get_Size( __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *iface, Size *value )
{
    struct window_size_changed_args *impl = impl_from_IWindowSizeChangedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->size;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgsVtbl window_size_changed_args_vtbl =
{
    window_size_changed_args_QueryInterface,
    window_size_changed_args_AddRef,
    window_size_changed_args_Release,
    window_size_changed_args_GetIids,
    window_size_changed_args_GetRuntimeClassName,
    window_size_changed_args_GetTrustLevel,
    window_size_changed_args_get_Size,
};

static __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *window_size_changed_args_create( float width, float height )
{
    struct window_size_changed_args *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IWindowSizeChangedEventArgs_iface.lpVtbl = &window_size_changed_args_vtbl;
    impl->ref = 1;
    impl->size.Width = width;
    impl->size.Height = height;
    return &impl->IWindowSizeChangedEventArgs_iface;
}

struct window_activated_args
{
    __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs IWindowActivatedEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    CoreWindowActivationState state;
    boolean handled;
};

static inline struct window_activated_args *impl_from_IWindowActivatedEventArgs( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct window_activated_args, IWindowActivatedEventArgs_iface );
}

static inline struct window_activated_args *impl_from_ICoreWindowEventArgs( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct window_activated_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI window_activated_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface, REFIID iid, void **out )
{
    struct window_activated_args *impl = impl_from_IWindowActivatedEventArgs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IWindowActivatedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI window_activated_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface )
{
    struct window_activated_args *impl = impl_from_IWindowActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI window_activated_args_Release( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface )
{
    struct window_activated_args *impl = impl_from_IWindowActivatedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI window_activated_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface, ULONG *count, IID **iids )
{ return E_NOTIMPL; }
static HRESULT WINAPI window_activated_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.WindowActivatedEventArgs", 39, name ); }
static HRESULT WINAPI window_activated_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI window_activated_args_get_WindowActivationState( __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *iface,
    __x_ABI_CWindows_CUI_CCore_CCoreWindowActivationState *value )
{
    struct window_activated_args *impl = impl_from_IWindowActivatedEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->state;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgsVtbl window_activated_args_vtbl =
{
    window_activated_args_QueryInterface,
    window_activated_args_AddRef,
    window_activated_args_Release,
    window_activated_args_GetIids,
    window_activated_args_GetRuntimeClassName,
    window_activated_args_GetTrustLevel,
    window_activated_args_get_WindowActivationState,
};

static HRESULT WINAPI window_activated_args_core_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    struct window_activated_args *impl = impl_from_ICoreWindowEventArgs( iface );
    return window_activated_args_QueryInterface( &impl->IWindowActivatedEventArgs_iface, iid, out );
}

static ULONG WINAPI window_activated_args_core_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct window_activated_args *impl = impl_from_ICoreWindowEventArgs( iface );
    return window_activated_args_AddRef( &impl->IWindowActivatedEventArgs_iface );
}

static ULONG WINAPI window_activated_args_core_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    struct window_activated_args *impl = impl_from_ICoreWindowEventArgs( iface );
    return window_activated_args_Release( &impl->IWindowActivatedEventArgs_iface );
}

static HRESULT WINAPI window_activated_args_core_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{ return E_NOTIMPL; }

static HRESULT WINAPI window_activated_args_core_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.WindowActivatedEventArgs", 39, name ); }

static HRESULT WINAPI window_activated_args_core_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI window_activated_args_core_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    struct window_activated_args *impl = impl_from_ICoreWindowEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->handled;
    return S_OK;
}

static HRESULT WINAPI window_activated_args_core_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    struct window_activated_args *impl = impl_from_ICoreWindowEventArgs( iface );
    impl->handled = value;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl window_activated_args_core_vtbl =
{
    window_activated_args_core_QueryInterface,
    window_activated_args_core_AddRef,
    window_activated_args_core_Release,
    window_activated_args_core_GetIids,
    window_activated_args_core_GetRuntimeClassName,
    window_activated_args_core_GetTrustLevel,
    window_activated_args_core_get_Handled,
    window_activated_args_core_put_Handled,
};

static __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *window_activated_args_create( CoreWindowActivationState state )
{
    struct window_activated_args *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IWindowActivatedEventArgs_iface.lpVtbl = &window_activated_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &window_activated_args_core_vtbl;
    impl->ref = 1;
    impl->state = state;
    impl->handled = FALSE;
    return &impl->IWindowActivatedEventArgs_iface;
}

static void core_window_deliver_activation( struct core_window *impl, CoreWindowActivationState state )
{
    __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs *args;

    if (!impl || !impl->activated_handler) return;
    if (!(args = window_activated_args_create( state ))) return;

    TRACE( "invoking activated_handler %p with state %d\n", impl->activated_handler, state );
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs_Invoke(
        impl->activated_handler, &impl->ICoreWindow_iface, args );
    __x_ABI_CWindows_CUI_CCore_CIWindowActivatedEventArgs_Release( args );
}

static HRESULT WINAPI core_window_add_Activated( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs_AddRef( handler );
        impl->activated_handler = handler;

        /* If the window is already created and active/foreground, the initial Win32 WM_ACTIVATE
         * happened before the handler was registered. WinUI 3 / XAML apps wait for this
         * activation event to proceed past early initialization. Deliver synthetic activation
         * immediately so the subscriber receives the event without waiting for a message. */
        if (impl->hwnd && IsWindow( impl->hwnd ) &&
            (GetActiveWindow() == impl->hwnd || GetForegroundWindow() == impl->hwnd || IsWindowVisible( impl->hwnd )))
        {
            TRACE( "window %p already active/foreground (%p/%p), delivering synthetic activation.\n",
                   impl->hwnd, GetActiveWindow(), GetForegroundWindow() );
            core_window_deliver_activation( impl, CoreWindowActivationState_CodeActivated );
            PostMessageW( impl->hwnd, WM_ACTIVATE, MAKEWPARAM( WA_ACTIVE, 0 ), 0 );
        }
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_Activated( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p\n", iface );
    if (impl->activated_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs_Release( impl->activated_handler );
        impl->activated_handler = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI core_window_add_SizeChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 0;
    if (!handler) return E_INVALIDARG;
    for (i = 0; i < ARRAY_SIZE( impl->size_changed_handlers ); i++)
    {
        if (!impl->size_changed_handlers[i])
        {
            INT64 tok = ++impl->next_size_changed_token;
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_AddRef( handler );
            impl->size_changed_handlers[i] = handler;
            impl->size_changed_tokens[i] = tok;
            if (cookie) cookie->value = tok;
            return S_OK;
        }
    }
    return E_OUTOFMEMORY;
}
static HRESULT WINAPI core_window_remove_SizeChanged( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE( impl->size_changed_handlers ); i++)
    {
        if (impl->size_changed_handlers[i] && impl->size_changed_tokens[i] == cookie.value)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_Release( impl->size_changed_handlers[i] );
            impl->size_changed_handlers[i] = NULL;
            impl->size_changed_tokens[i] = 0;
            break;
        }
    }
    return S_OK;
}

static HRESULT WINAPI core_window_add_VisibilityChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_AddRef( handler );
        impl->visibility_changed_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_VisibilityChanged( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (impl->visibility_changed_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_Release( impl->visibility_changed_handler );
        impl->visibility_changed_handler = NULL;
    }
    return S_OK;
}

/* ---- ICoreWindowInputProperties (minimal live face) ----
 * The real class vtbl puts this at slot 9 (after Dispatcher); apps probe it on startup
 * (KeyboardPresent gates the whole keyboard-input path), so return a live face. */
static const IID IID_ICoreWindowInputProperties_local =
    { 0x46cb65bf, 0x0471, 0x42ab, { 0xa9, 0x48, 0x71, 0x00, 0x86, 0xf2, 0xd9, 0x88 } };

struct input_properties
{
    __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties ICoreWindowInputProperties_iface;
    LONG ref;
};

static inline struct input_properties *impl_from_ICoreWindowInputProperties(
    __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface )
{
    return CONTAINING_RECORD( iface, struct input_properties, ICoreWindowInputProperties_iface );
}

static HRESULT WINAPI input_props_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface,
                                                  REFIID iid, void **out )
{
    struct input_properties *impl = impl_from_ICoreWindowInputProperties( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWindowInputProperties_local ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowInputProperties_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI input_props_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface )
{
    return InterlockedIncrement( &impl_from_ICoreWindowInputProperties( iface )->ref );
}
static ULONG WINAPI input_props_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface )
{
    struct input_properties *impl = impl_from_ICoreWindowInputProperties( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI input_props_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI input_props_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreWindowInputProperties", 41, name ); }
static HRESULT WINAPI input_props_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI input_props_get_KeyboardPresent( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, boolean *value )
{ if (!value) return E_POINTER; *value = TRUE; return S_OK; }
static HRESULT WINAPI input_props_get_MousePresent( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, boolean *value )
{ if (!value) return E_POINTER; *value = TRUE; return S_OK; }
static HRESULT WINAPI input_props_get_TouchPresentation( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, boolean *value )
{ if (!value) return E_POINTER; *value = FALSE; return S_OK; }
static HRESULT WINAPI input_props_get_PointerPointerPromoted( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, boolean *value )
{ if (!value) return E_POINTER; *value = FALSE; return S_OK; }
static HRESULT WINAPI input_props_put_PointerPointerPromoted( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, boolean value )
{ return S_OK; }
static HRESULT WINAPI input_props_get_PointerPromotion( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, boolean *value )
{ if (!value) return E_POINTER; *value = FALSE; return S_OK; }
static HRESULT WINAPI input_props_put_PointerPromotion( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, boolean value )
{ return S_OK; }
static HRESULT WINAPI input_props_get_FirstSignature( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, INT32 *value )
{ if (!value) return E_POINTER; *value = 1; return S_OK; }
static HRESULT WINAPI input_props_get_MaxTouchPoints( __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties *iface, INT32 *value )
{ if (!value) return E_POINTER; *value = 10; return S_OK; }

static const struct __x_ABI_CWindows_CUI_CCore_CICoreWindowInputPropertiesVtbl input_props_vtbl =
{
    input_props_QueryInterface,
    input_props_AddRef,
    input_props_Release,
    input_props_GetIids,
    input_props_GetRuntimeClassName,
    input_props_GetTrustLevel,
    input_props_get_KeyboardPresent,
    input_props_get_MousePresent,
    input_props_get_TouchPresentation,
    input_props_get_PointerPointerPromoted,
    input_props_put_PointerPointerPromoted,
    input_props_get_PointerPromotion,
    input_props_put_PointerPromotion,
    input_props_get_FirstSignature,
    input_props_get_MaxTouchPoints,
};

static HRESULT WINAPI core_window_get_InputProperties( ICoreWindow *iface,
    __x_ABI_CWindows_CUI_CCore_CICoreWindowInputProperties **value )
{
    struct input_properties *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->ICoreWindowInputProperties_iface.lpVtbl = &input_props_vtbl;
    impl->ref = 1;
    *value = &impl->ICoreWindowInputProperties_iface;
    return S_OK;
}

static const struct ICoreWindowVtbl core_window_vtbl =
{
    core_window_QueryInterface,
    core_window_AddRef,
    core_window_Release,
    core_window_GetIids,
    core_window_GetRuntimeClassName,
    core_window_GetTrustLevel,
    core_window_get_AutomationHostProvider,
    /* ICoreWindow properties, real class-vtbl order */
    core_window_get_Bounds,
    core_window_get_CustomProperties,
    core_window_get_Dispatcher,
    core_window_get_InputProperties,
    core_window_get_PointerCursor,
    core_window_put_PointerCursor,
    core_window_get_PointerPosition,
    core_window_get_Visible,
    core_window_Activate,
    core_window_Close,
    core_window_GetAsyncKeyState,
    core_window_GetKeyState,
    core_window_ReleasePointerCapture,
    core_window_SetPointerCapture,
    /* events */
    core_window_add_Activated,
    core_window_remove_Activated,
    core_window_add_AutomationProviderRequested,
    core_window_remove_AutomationProviderRequested,
    core_window_add_CharacterReceived,
    core_window_remove_CharacterReceived,
    core_window_add_Closed,
    core_window_remove_Closed,
    core_window_add_InputEnabled,
    core_window_remove_InputEnabled,
    core_window_add_KeyDown,
    core_window_remove_KeyDown,
    core_window_add_KeyUp,
    core_window_remove_KeyUp,
    core_window_add_PointerCaptureLost,
    core_window_remove_PointerCaptureLost,
    core_window_add_PointerEntered,
    core_window_remove_PointerEntered,
    core_window_add_PointerExited,
    core_window_remove_PointerExited,
    core_window_add_PointerMoved,
    core_window_remove_PointerMoved,
    core_window_add_PointerPressed,
    core_window_remove_PointerPressed,
    core_window_add_PointerReleased,
    core_window_remove_PointerReleased,    core_window_add_PointerWheelChanged,
    core_window_remove_PointerWheelChanged,
    core_window_add_SizeChanged,
    core_window_remove_SizeChanged,
    core_window_add_VisibilityChanged,
    core_window_remove_VisibilityChanged,
    core_window_add_TouchHitTesting,
    core_window_remove_TouchHitTesting,
    /* appended versions */
    core_window_get_FlowDirection,
    core_window_put_FlowDirection,
    core_window_get_IsInputEnabled,
    core_window_put_IsInputEnabled,};

/* KeyEventArgs */
struct key_event_args
{
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs IKeyEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    __x_ABI_CWindows_CSystem_CVirtualKey key;
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus status;
    boolean handled;
};

static inline struct key_event_args *impl_from_IKeyEventArgs( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct key_event_args, IKeyEventArgs_iface );
}
static inline struct key_event_args *impl_from_ICoreWindowEventArgs_key( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct key_event_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI key_event_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, REFIID iid, void **out )
{
    struct key_event_args *impl = impl_from_IKeyEventArgs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIKeyEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IKeyEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI key_event_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_IKeyEventArgs( iface )->ref );
}
static ULONG WINAPI key_event_args_Release( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface )
{
    struct key_event_args *impl = impl_from_IKeyEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI key_event_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI key_event_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.KeyEventArgs", 28, name ); }
static HRESULT WINAPI key_event_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI key_event_args_get_VirtualKey( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, __x_ABI_CWindows_CSystem_CVirtualKey *value )
{
    struct key_event_args *impl = impl_from_IKeyEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->key;
    return S_OK;
}
static HRESULT WINAPI key_event_args_get_KeyStatus( __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *iface, __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *value )
{
    struct key_event_args *impl = impl_from_IKeyEventArgs( iface );
    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CIKeyEventArgsVtbl key_event_args_vtbl =
{
    key_event_args_QueryInterface,
    key_event_args_AddRef,
    key_event_args_Release,
    key_event_args_GetIids,
    key_event_args_GetRuntimeClassName,
    key_event_args_GetTrustLevel,
    key_event_args_get_VirtualKey,
    key_event_args_get_KeyStatus,
};

static HRESULT WINAPI key_event_args_core_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    return key_event_args_QueryInterface( &impl_from_ICoreWindowEventArgs_key( iface )->IKeyEventArgs_iface, iid, out );
}
static ULONG WINAPI key_event_args_core_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return key_event_args_AddRef( &impl_from_ICoreWindowEventArgs_key( iface )->IKeyEventArgs_iface );
}
static ULONG WINAPI key_event_args_core_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return key_event_args_Release( &impl_from_ICoreWindowEventArgs_key( iface )->IKeyEventArgs_iface );
}
static HRESULT WINAPI key_event_args_core_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI key_event_args_core_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.KeyEventArgs", 28, name ); }
static HRESULT WINAPI key_event_args_core_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI key_event_args_core_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_ICoreWindowEventArgs_key( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI key_event_args_core_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_ICoreWindowEventArgs_key( iface )->handled = value;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl key_event_args_core_vtbl =
{
    key_event_args_core_QueryInterface,
    key_event_args_core_AddRef,
    key_event_args_core_Release,
    key_event_args_core_GetIids,
    key_event_args_core_GetRuntimeClassName,
    key_event_args_core_GetTrustLevel,
    key_event_args_core_get_Handled,
    key_event_args_core_put_Handled,
};

static __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *key_event_args_create( __x_ABI_CWindows_CSystem_CVirtualKey key )
{
    struct key_event_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IKeyEventArgs_iface.lpVtbl = &key_event_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &key_event_args_core_vtbl;
    impl->ref = 1;
    impl->key = key;
    return &impl->IKeyEventArgs_iface;
}

static BOOL core_window_deliver_keydown( struct core_window *impl, __x_ABI_CWindows_CSystem_CVirtualKey key )
{
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *args;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *snap[16];
    unsigned int i, cnt = 0;
    BOOL handled;

    if (!impl) return FALSE;
    for (i = 0; i < ARRAY_SIZE( impl->keydown_handlers ); i++)
    {
        if (impl->keydown_handlers[i])
        {
            snap[cnt] = impl->keydown_handlers[i];
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_AddRef( snap[cnt] );
            cnt++;
        }
    }
    if (!cnt) return FALSE;
    if (!(args = key_event_args_create( key )))
    {
        for (i = 0; i < cnt; i++)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( snap[i] );
        return FALSE;
    }
    for (i = 0; i < cnt; i++)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Invoke(
            snap[i], &impl->ICoreWindow_iface, args );
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( snap[i] );
    }
    handled = impl_from_IKeyEventArgs( args )->handled != 0;
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( args );
    return handled;
}

static BOOL core_window_deliver_keyup( struct core_window *impl, __x_ABI_CWindows_CSystem_CVirtualKey key )
{
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *args;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *snap[16];
    unsigned int i, cnt = 0;
    BOOL handled;

    if (!impl) return FALSE;
    for (i = 0; i < ARRAY_SIZE( impl->keyup_handlers ); i++)
    {
        if (impl->keyup_handlers[i])
        {
            snap[cnt] = impl->keyup_handlers[i];
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_AddRef( snap[cnt] );
            cnt++;
        }
    }
    if (!cnt) return FALSE;
    if (!(args = key_event_args_create( key )))
    {
        for (i = 0; i < cnt; i++)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( snap[i] );
        return FALSE;
    }
    for (i = 0; i < cnt; i++)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Invoke(
            snap[i], &impl->ICoreWindow_iface, args );
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( snap[i] );
    }
    handled = impl_from_IKeyEventArgs( args )->handled != 0;
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( args );
    return handled;
}

/* CharacterReceivedEventArgs */
struct character_received_args
{
    __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs ICharacterReceivedEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    UINT32 key_code;
    __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus status;
    boolean handled;
};

static inline struct character_received_args *impl_from_ICharacterReceivedEventArgs( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct character_received_args, ICharacterReceivedEventArgs_iface );
}
static inline struct character_received_args *impl_from_ICoreWindowEventArgs_char( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct character_received_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI character_received_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, REFIID iid, void **out )
{
    struct character_received_args *impl = impl_from_ICharacterReceivedEventArgs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICharacterReceivedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI character_received_args_AddRef( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_ICharacterReceivedEventArgs( iface )->ref );
}
static ULONG WINAPI character_received_args_Release( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface )
{
    struct character_received_args *impl = impl_from_ICharacterReceivedEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI character_received_args_GetIids( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI character_received_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CharacterReceivedEventArgs", 42, name ); }
static HRESULT WINAPI character_received_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI character_received_args_get_KeyCode( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_ICharacterReceivedEventArgs( iface )->key_code;
    return S_OK;
}
static HRESULT WINAPI character_received_args_get_KeyStatus( __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *iface, __x_ABI_CWindows_CUI_CCore_CCorePhysicalKeyStatus *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_ICharacterReceivedEventArgs( iface )->status;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgsVtbl character_received_args_vtbl =
{
    character_received_args_QueryInterface,
    character_received_args_AddRef,
    character_received_args_Release,
    character_received_args_GetIids,
    character_received_args_GetRuntimeClassName,
    character_received_args_GetTrustLevel,
    character_received_args_get_KeyCode,
    character_received_args_get_KeyStatus,
};

static HRESULT WINAPI character_received_args_core_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    return character_received_args_QueryInterface( &impl_from_ICoreWindowEventArgs_char( iface )->ICharacterReceivedEventArgs_iface, iid, out );
}
static ULONG WINAPI character_received_args_core_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return character_received_args_AddRef( &impl_from_ICoreWindowEventArgs_char( iface )->ICharacterReceivedEventArgs_iface );
}
static ULONG WINAPI character_received_args_core_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return character_received_args_Release( &impl_from_ICoreWindowEventArgs_char( iface )->ICharacterReceivedEventArgs_iface );
}
static HRESULT WINAPI character_received_args_core_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI character_received_args_core_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CharacterReceivedEventArgs", 42, name ); }
static HRESULT WINAPI character_received_args_core_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI character_received_args_core_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_ICoreWindowEventArgs_char( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI character_received_args_core_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_ICoreWindowEventArgs_char( iface )->handled = value;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl character_received_args_core_vtbl =
{
    character_received_args_core_QueryInterface,
    character_received_args_core_AddRef,
    character_received_args_core_Release,
    character_received_args_core_GetIids,
    character_received_args_core_GetRuntimeClassName,
    character_received_args_core_GetTrustLevel,
    character_received_args_core_get_Handled,
    character_received_args_core_put_Handled,
};

static __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *character_received_args_create( UINT32 key_code )
{
    struct character_received_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->ICharacterReceivedEventArgs_iface.lpVtbl = &character_received_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &character_received_args_core_vtbl;
    impl->ref = 1;
    impl->key_code = key_code;
    return &impl->ICharacterReceivedEventArgs_iface;
}

static BOOL core_window_deliver_character_received( struct core_window *impl, UINT32 key_code )
{
    __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *args;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *snap[16];
    unsigned int i, cnt = 0;
    BOOL handled;

    if (!impl) return FALSE;
    for (i = 0; i < ARRAY_SIZE( impl->character_received_handlers ); i++)
    {
        if (impl->character_received_handlers[i])
        {
            snap[cnt] = impl->character_received_handlers[i];
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_AddRef( snap[cnt] );
            cnt++;
        }
    }
    if (!cnt) return FALSE;
    if (!(args = character_received_args_create( key_code )))
    {
        for (i = 0; i < cnt; i++)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Release( snap[i] );
        return FALSE;
    }
    for (i = 0; i < cnt; i++)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Invoke(
            snap[i], &impl->ICoreWindow_iface, args );
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Release( snap[i] );
    }
    handled = impl_from_ICharacterReceivedEventArgs( args )->handled != 0;
    __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_Release( args );
    return handled;
}

/* PointerEventArgs */
struct pointer_event_args
{
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs IPointerEventArgs_iface;
    __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ICoreWindowEventArgs_iface;
    LONG ref;
    boolean handled;
};

static inline struct pointer_event_args *impl_from_IPointerEventArgs( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct pointer_event_args, IPointerEventArgs_iface );
}
static inline struct pointer_event_args *impl_from_ICoreWindowEventArgs_pointer( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct pointer_event_args, ICoreWindowEventArgs_iface );
}

static HRESULT WINAPI pointer_event_args_QueryInterface( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, REFIID iid, void **out )
{
    struct pointer_event_args *impl = impl_from_IPointerEventArgs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CIPointerEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IPointerEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->ICoreWindowEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pointer_event_args_AddRef( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_IPointerEventArgs( iface )->ref );
}
static ULONG WINAPI pointer_event_args_Release( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface )
{
    struct pointer_event_args *impl = impl_from_IPointerEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI pointer_event_args_GetIids( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI pointer_event_args_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.PointerEventArgs", 31, name ); }
static HRESULT WINAPI pointer_event_args_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI pointer_event_args_get_CurrentPoint( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, __x_ABI_CWindows_CUI_CInput_CIPointerPoint **value )
{ if (value) *value = NULL; return S_OK; }
static HRESULT WINAPI pointer_event_args_get_KeyModifiers( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, UINT32 *value )
{ if (value) *value = 0; return S_OK; }
static HRESULT WINAPI pointer_event_args_GetIntermediatePoints( __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *iface, __FIVector_1_Windows__CUI__CInput__CPointerPoint **value )
{ if (value) *value = NULL; return S_OK; }

static const struct __x_ABI_CWindows_CUI_CCore_CIPointerEventArgsVtbl pointer_event_args_vtbl =
{
    pointer_event_args_QueryInterface,
    pointer_event_args_AddRef,
    pointer_event_args_Release,
    pointer_event_args_GetIids,
    pointer_event_args_GetRuntimeClassName,
    pointer_event_args_GetTrustLevel,
    pointer_event_args_get_CurrentPoint,
    pointer_event_args_get_KeyModifiers,
    pointer_event_args_GetIntermediatePoints,
};

static HRESULT WINAPI pointer_event_args_core_QueryInterface( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, REFIID iid, void **out )
{
    return pointer_event_args_QueryInterface( &impl_from_ICoreWindowEventArgs_pointer( iface )->IPointerEventArgs_iface, iid, out );
}
static ULONG WINAPI pointer_event_args_core_AddRef( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return pointer_event_args_AddRef( &impl_from_ICoreWindowEventArgs_pointer( iface )->IPointerEventArgs_iface );
}
static ULONG WINAPI pointer_event_args_core_Release( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface )
{
    return pointer_event_args_Release( &impl_from_ICoreWindowEventArgs_pointer( iface )->IPointerEventArgs_iface );
}
static HRESULT WINAPI pointer_event_args_core_GetIids( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI pointer_event_args_core_GetRuntimeClassName( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.PointerEventArgs", 31, name ); }
static HRESULT WINAPI pointer_event_args_core_GetTrustLevel( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI pointer_event_args_core_get_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_ICoreWindowEventArgs_pointer( iface )->handled;
    return S_OK;
}
static HRESULT WINAPI pointer_event_args_core_put_Handled( __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgs *iface, boolean value )
{
    impl_from_ICoreWindowEventArgs_pointer( iface )->handled = value;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CCore_CICoreWindowEventArgsVtbl pointer_event_args_core_vtbl =
{
    pointer_event_args_core_QueryInterface,
    pointer_event_args_core_AddRef,
    pointer_event_args_core_Release,
    pointer_event_args_core_GetIids,
    pointer_event_args_core_GetRuntimeClassName,
    pointer_event_args_core_GetTrustLevel,
    pointer_event_args_core_get_Handled,
    pointer_event_args_core_put_Handled,
};

static __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *pointer_event_args_create( void )
{
    struct pointer_event_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IPointerEventArgs_iface.lpVtbl = &pointer_event_args_vtbl;
    impl->ICoreWindowEventArgs_iface.lpVtbl = &pointer_event_args_core_vtbl;
    impl->ref = 1;
    return &impl->IPointerEventArgs_iface;
}

static void core_window_deliver_pointer_pressed( struct core_window *impl, int x, int y )
{
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args;
    if (!impl || !impl->pointer_pressed_handler) return;
    if (!(args = pointer_event_args_create())) return;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Invoke(
        impl->pointer_pressed_handler, &impl->ICoreWindow_iface, args );
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_Release( args );
}

static void core_window_deliver_pointer_released( struct core_window *impl, int x, int y )
{
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args;
    if (!impl || !impl->pointer_released_handler) return;
    if (!(args = pointer_event_args_create())) return;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Invoke(
        impl->pointer_released_handler, &impl->ICoreWindow_iface, args );
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_Release( args );
}

#ifndef WM_POINTERDOWN
#define WM_POINTERDOWN 0x0246
#endif
#ifndef WM_POINTERUP
#define WM_POINTERUP 0x0247
#endif

#define WM_COREWINDOW_EXTEND_TITLEBAR (WM_APP + 0x42)
#define CSD_BG_SAMPLE_TIMER_ID 1

struct csd_overlay_state
{
    HWND parent;
    int hover_btn;
    int pressed_btn;
    BOOL tracking;
    BOOL has_sampled_bg;
    COLORREF sampled_bg;
    int sample_ticks;
    int stable_ticks;
};

static DPI_AWARENESS_CONTEXT csd_enter_pm_dpi( void )
{
    DPI_AWARENESS_CONTEXT prev = SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    if (!prev) prev = SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE );
    return prev;
}

static void csd_leave_pm_dpi( DPI_AWARENESS_CONTEXT prev )
{
    if (prev) SetThreadDpiAwarenessContext( prev );
}

static BOOL csd_get_color_prop( HWND parent, const WCHAR *prop, COLORREF *out, BOOL require_opaque )
{
    UINT_PTR val = (UINT_PTR)GetPropW( parent, prop );
    if (!val) return FALSE;
    if (require_opaque && !(val & 0x01000000u)) return FALSE;
    if (!require_opaque && !(val & 0x03000000u)) return FALSE;
    *out = (COLORREF)(val & 0x00ffffffu);
    return TRUE;
}

static BOOL csd_try_sample_parent_bg( HWND overlay, struct csd_overlay_state *st, BOOL allow_black )
{
    DPI_AWARENESS_CONTEXT prev_dpi;
    RECT prc, orc;
    HDC pdc;
    int sx, sy;
    COLORREF c;
    BOOL content_painted = FALSE;

    if (!st || !st->parent || !IsWindow( st->parent )) return FALSE;

    prev_dpi = csd_enter_pm_dpi();
    if (!GetClientRect( st->parent, &prc ) || !GetClientRect( overlay, &orc ) ||
        prc.right <= 0 || prc.bottom <= 0 || orc.right <= 0 || orc.bottom <= 0)
    {
        csd_leave_pm_dpi( prev_dpi );
        return FALSE;
    }

    sx = (int)prc.right - (int)orc.right - 6;
    sy = (int)orc.bottom / 2;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;

    pdc = GetDC( st->parent );
    if (!pdc)
    {
        csd_leave_pm_dpi( prev_dpi );
        return FALSE;
    }
    c = GetPixel( pdc, sx, sy );

    /* If the titlebar pixel is pure black (#000000), check whether the parent
     * window has already rendered content elsewhere (e.g. a black-titlebar dark
     * app like Store vs an uninitialized X11 window before first present). */
    if (c == RGB( 0, 0, 0 ) && !allow_black)
    {
        int probes[4][2] = {
            { (int)prc.right / 2, (int)prc.bottom / 2 },
            { (int)prc.right / 3, (int)prc.bottom / 3 },
            { (int)prc.right / 2, sy },
            { min( 24, (int)prc.right / 4 ), sy },
        };
        int i;
        for (i = 0; i < 4; i++)
        {
            COLORREF pc = GetPixel( pdc, probes[i][0], probes[i][1] );
            if (pc != CLR_INVALID && pc != RGB( 0, 0, 0 ))
            {
                content_painted = TRUE;
                break;
            }
        }
    }
    ReleaseDC( st->parent, pdc );
    csd_leave_pm_dpi( prev_dpi );

    if (c == CLR_INVALID) return FALSE;
    if (c == RGB( 0, 0, 0 ) && !allow_black && !content_painted) return FALSE;

    st->sampled_bg = c;
    st->has_sampled_bg = TRUE;
    return TRUE;
}

static COLORREF csd_blend_color( COLORREF bg, COLORREF fg, int alpha )
{
    int r = (GetRValue( fg ) * alpha + GetRValue( bg ) * (255 - alpha)) / 255;
    int g = (GetGValue( fg ) * alpha + GetGValue( bg ) * (255 - alpha)) / 255;
    int b = (GetBValue( fg ) * alpha + GetBValue( bg ) * (255 - alpha)) / 255;
    return RGB( r, g, b );
}

static BOOL csd_is_dark_theme( HWND parent )
{
    static const WCHAR subkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    COLORREF c;
    UINT_PTR dark_prop;
    DWORD val = 1, len = sizeof(val), type = 0;
    HKEY hkey;

    if (csd_get_color_prop( parent, L"__wine_tb_btn_fg", &c, TRUE ))
    {
        unsigned int lum = (299 * GetRValue( c ) + 587 * GetGValue( c ) + 114 * GetBValue( c )) / 1000;
        return lum >= 128;
    }
    if (csd_get_color_prop( parent, L"__wine_tb_bg", &c, TRUE ))
    {
        unsigned int lum = (299 * GetRValue( c ) + 587 * GetGValue( c ) + 114 * GetBValue( c )) / 1000;
        return lum < 128;
    }
    if ((dark_prop = (UINT_PTR)GetPropW( parent, L"__wine_tb_dark" )))
        return dark_prop == 2;

    if (!RegOpenKeyExW( HKEY_CURRENT_USER, subkey, 0, KEY_QUERY_VALUE, &hkey ))
    {
        if (RegQueryValueExW( hkey, L"AppsUseLightTheme", NULL, &type, (BYTE *)&val, &len ) || type != REG_DWORD)
            val = 1;
        RegCloseKey( hkey );
    }
    return val == 0;
}

static void csd_draw_rect_outline( HDC hdc, int left, int top, int right, int bottom )
{
    MoveToEx( hdc, left, top, NULL );
    LineTo( hdc, right, top );
    LineTo( hdc, right, bottom );
    LineTo( hdc, left, bottom );
    LineTo( hdc, left, top );
}

static void csd_overlay_paint( HWND hwnd, struct csd_overlay_state *st )
{
    DPI_AWARENESS_CONTEXT prev_dpi;
    PAINTSTRUCT ps;
    HDC hdc, memdc;
    HBITMAP bmp, old_bmp;
    RECT rc;
    int w, h, i, dpi, s, pen_w;
    BOOL dark, active, zoomed;
    COLORREF bg_norm, fg_norm, bg_hover, bg_press, fg_hover;

    prev_dpi = csd_enter_pm_dpi();
    hdc = BeginPaint( hwnd, &ps );
    if (!hdc)
    {
        csd_leave_pm_dpi( prev_dpi );
        return;
    }

    GetClientRect( hwnd, &rc );
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        EndPaint( hwnd, &ps );
        csd_leave_pm_dpi( prev_dpi );
        return;
    }

    memdc = CreateCompatibleDC( hdc );
    bmp = CreateCompatibleBitmap( hdc, w, h );
    if (!memdc || !bmp)
    {
        if (bmp) DeleteObject( bmp );
        if (memdc) DeleteDC( memdc );
        EndPaint( hwnd, &ps );
        csd_leave_pm_dpi( prev_dpi );
        return;
    }
    old_bmp = SelectObject( memdc, bmp );

    dpi = GetDpiForWindow( hwnd );
    if (!dpi) dpi = GetDpiForWindow( st->parent );
    if (!dpi) dpi = 96;
    s = MulDiv( 10, dpi, 96 );
    if (s < 8) s = 8;
    pen_w = max( 1, MulDiv( 1, dpi, 96 ) );

    dark = csd_is_dark_theme( st->parent );
    active = (GetActiveWindow() == st->parent || GetForegroundWindow() == st->parent);
    zoomed = IsZoomed( st->parent );

    {
        COLORREF app_bg = 0, app_fg = 0;
        BOOL has_app_bg = csd_get_color_prop( st->parent, L"__wine_tb_btn_bg", &app_bg, TRUE ) ||
                          csd_get_color_prop( st->parent, L"__wine_tb_bg", &app_bg, TRUE );
        BOOL has_app_fg = csd_get_color_prop( st->parent, L"__wine_tb_btn_fg", &app_fg, TRUE );
        /* If the app requested transparent button background, or left bg and fg
         * identical (e.g. both 0x000000 from unresolved defaults), sample the
         * adjacent rendered titlebar pixel so the overlay blends seamlessly. */
        BOOL use_sampled = !has_app_bg || (has_app_fg && app_bg == app_fg);

        if (use_sampled)
        {
            csd_try_sample_parent_bg( hwnd, st, st->sample_ticks >= 10 );
            if (st->sample_ticks == 0)
            {
                /* Re-sample briefly during startup so any early pre-present pixel
                 * (unpainted black or COLOR_WINDOW white) updates once the first
                 * Vulkan/D3D11 frame lands. */
                st->sample_ticks = 1;
                SetTimer( hwnd, CSD_BG_SAMPLE_TIMER_ID, 250, NULL );
            }
            if (st->has_sampled_bg)
            {
                bg_norm = st->sampled_bg;
                dark = ((299 * GetRValue( bg_norm ) + 587 * GetGValue( bg_norm ) + 114 * GetBValue( bg_norm )) / 1000) < 128;
            }
            else
            {
                bg_norm = dark ? RGB( 32, 32, 32 ) : RGB( 243, 243, 243 );
            }
        }
        else
        {
            bg_norm = app_bg;
            dark = ((299 * GetRValue( bg_norm ) + 587 * GetGValue( bg_norm ) + 114 * GetBValue( bg_norm )) / 1000) < 128;
        }

        if (!active)
        {
            if (use_sampled || !csd_get_color_prop( st->parent, L"__wine_tb_btn_inact_fg", &fg_norm, TRUE ))
                fg_norm = dark ? RGB( 175, 175, 175 ) : RGB( 120, 120, 120 );
        }
        else
        {
            if (use_sampled || !has_app_fg)
                fg_norm = dark ? RGB( 242, 242, 242 ) : RGB( 26, 26, 26 );
            else
                fg_norm = app_fg;
        }
    }

    /* Guarantee visible glyph contrast against background (reject same-polarity or low contrast). */
    {
        int bg_lum = (299 * GetRValue( bg_norm ) + 587 * GetGValue( bg_norm ) + 114 * GetBValue( bg_norm )) / 1000;
        int fg_lum = (299 * GetRValue( fg_norm ) + 587 * GetGValue( fg_norm ) + 114 * GetBValue( fg_norm )) / 1000;
        if (((bg_lum < 128) == (fg_lum < 128)) || abs( bg_lum - fg_lum ) < 100)
            fg_norm = (bg_lum < 128) ? RGB( 242, 242, 242 ) : RGB( 26, 26, 26 );
    }

    if (!csd_get_color_prop( st->parent, L"__wine_tb_btn_hover_bg", &bg_hover, TRUE ))
        bg_hover = dark ? csd_blend_color( bg_norm, RGB( 255, 255, 255 ), 28 )
                        : csd_blend_color( bg_norm, RGB( 0, 0, 0 ), 22 );
    bg_press = dark ? csd_blend_color( bg_norm, RGB( 255, 255, 255 ), 18 )
                    : csd_blend_color( bg_norm, RGB( 0, 0, 0 ), 38 );
    if (!csd_get_color_prop( st->parent, L"__wine_tb_btn_hover_fg", &fg_hover, TRUE ) || fg_hover == bg_norm)
        fg_hover = dark ? RGB( 255, 255, 255 ) : RGB( 0, 0, 0 );
    {
        int hbg_lum = (299 * GetRValue( bg_hover ) + 587 * GetGValue( bg_hover ) + 114 * GetBValue( bg_hover )) / 1000;
        int hfg_lum = (299 * GetRValue( fg_hover ) + 587 * GetGValue( fg_hover ) + 114 * GetBValue( fg_hover )) / 1000;
        if (((hbg_lum < 128) == (hfg_lum < 128)) || abs( hbg_lum - hfg_lum ) < 100)
            fg_hover = (hbg_lum < 128) ? RGB( 255, 255, 255 ) : RGB( 0, 0, 0 );
    }

    TRACE( "csd_overlay_paint hwnd %p (%dx%d) dark=%d active=%d zoomed=%d bg=%06lx fg=%06lx hover=%d\n",
           hwnd, w, h, dark, active, zoomed, (unsigned long)bg_norm, (unsigned long)fg_norm, st->hover_btn );

    for (i = 0; i < 3; i++)
    {
        RECT brc;
        COLORREF bg = bg_norm;
        COLORREF fg = fg_norm;
        HBRUSH brush;
        HPEN pen, old_pen;
        int cx, cy, x0, y0;

        brc.left = (i * w) / 3;
        brc.top = 0;
        brc.right = ((i + 1) * w) / 3;
        brc.bottom = h;

        if (st->pressed_btn == i && st->hover_btn == i)
        {
            bg = (i == 2) ? RGB( 178, 43, 30 ) : bg_press;
            fg = (i == 2) ? RGB( 255, 255, 255 ) : fg_hover;
        }
        else if (st->hover_btn == i)
        {
            bg = (i == 2) ? RGB( 196, 43, 28 ) : bg_hover;
            fg = (i == 2) ? RGB( 255, 255, 255 ) : fg_hover;
        }

        brush = CreateSolidBrush( bg );
        FillRect( memdc, &brc, brush );
        DeleteObject( brush );

        pen = CreatePen( PS_SOLID, pen_w, fg );
        old_pen = SelectObject( memdc, pen );
        cx = (brc.left + brc.right) / 2;
        cy = (brc.top + brc.bottom) / 2;
        x0 = cx - s / 2;
        y0 = cy - s / 2;

        if (i == 0)
        {
            /* Minimize */
            MoveToEx( memdc, x0, cy, NULL );
            LineTo( memdc, x0 + s, cy );
        }
        else if (i == 1)
        {
            /* Maximize / Restore */
            if (!zoomed)
            {
                csd_draw_rect_outline( memdc, x0, y0, x0 + s - 1, y0 + s - 1 );
            }
            else
            {
                int off = max( 2, MulDiv( 2, dpi, 96 ) );
                int rs = s - off;
                int fx = x0;
                int fy = y0 + off;
                csd_draw_rect_outline( memdc, fx, fy, fx + rs - 1, fy + rs - 1 );
                MoveToEx( memdc, fx + off, fy - 1, NULL );
                LineTo( memdc, fx + off, fy - off );
                LineTo( memdc, fx + off + rs - 1, fy - off );
                LineTo( memdc, fx + off + rs - 1, fy - off + rs - 1 );
                LineTo( memdc, fx + rs - 1, fy - off + rs - 1 );
            }
        }
        else
        {
            /* Close */
            MoveToEx( memdc, x0, y0, NULL );
            LineTo( memdc, x0 + s, y0 + s );
            MoveToEx( memdc, x0 + s - 1, y0, NULL );
            LineTo( memdc, x0 - 1, y0 + s );
        }

        SelectObject( memdc, old_pen );
        DeleteObject( pen );
    }

    BitBlt( hdc, 0, 0, w, h, memdc, 0, 0, SRCCOPY );
    SelectObject( memdc, old_bmp );
    DeleteObject( bmp );
    DeleteDC( memdc );
    EndPaint( hwnd, &ps );
    csd_leave_pm_dpi( prev_dpi );
}

static int csd_overlay_hit_button( HWND hwnd, int x, int y )
{
    DPI_AWARENESS_CONTEXT prev_dpi = csd_enter_pm_dpi();
    RECT rc;
    int w, h, idx;

    GetClientRect( hwnd, &rc );
    csd_leave_pm_dpi( prev_dpi );
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0 || x < 0 || x >= w || y < 0 || y >= h) return -1;
    idx = (x * 3) / w;
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    return idx;
}

static LRESULT CALLBACK csd_overlay_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct csd_overlay_state *st = (struct csd_overlay_state *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        st = calloc( 1, sizeof(*st) );
        if (!st) return FALSE;
        st->parent = cs->hwndParent;
        st->hover_btn = -1;
        st->pressed_btn = -1;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)st );
        SetPropW( hwnd, L"__wine_csd_overlay", (HANDLE)1 );
        return TRUE;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        if (st) csd_overlay_paint( hwnd, st );
        return 0;

    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_MOUSEMOVE:
        if (st)
        {
            int btn = csd_overlay_hit_button( hwnd, (short)LOWORD( lparam ), (short)HIWORD( lparam ) );
            if (!st->tracking)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent( &tme );
                st->tracking = TRUE;
            }
            if (st->hover_btn != btn)
            {
                st->hover_btn = btn;
                InvalidateRect( hwnd, NULL, FALSE );
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        if (st)
        {
            st->tracking = FALSE;
            if (st->hover_btn != -1)
            {
                st->hover_btn = -1;
                InvalidateRect( hwnd, NULL, FALSE );
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (st)
        {
            int btn = csd_overlay_hit_button( hwnd, (short)LOWORD( lparam ), (short)HIWORD( lparam ) );
            st->hover_btn = btn;
            st->pressed_btn = btn;
            SetCapture( hwnd );
            InvalidateRect( hwnd, NULL, FALSE );
        }
        return 0;

    case WM_LBUTTONUP:
        if (st)
        {
            int btn = csd_overlay_hit_button( hwnd, (short)LOWORD( lparam ), (short)HIWORD( lparam ) );
            int action = (st->pressed_btn != -1 && st->pressed_btn == btn) ? btn : -1;
            st->pressed_btn = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect( hwnd, NULL, FALSE );
            if (action == 0)
            {
                TRACE( "csd_overlay SC_MINIMIZE parent %p\n", st->parent );
                PostMessageW( st->parent, WM_SYSCOMMAND, SC_MINIMIZE, 0 );
            }
            else if (action == 1)
            {
                WPARAM cmd = IsZoomed( st->parent ) ? SC_RESTORE : SC_MAXIMIZE;
                TRACE( "csd_overlay %s parent %p\n", cmd == SC_MAXIMIZE ? "SC_MAXIMIZE" : "SC_RESTORE", st->parent );
                PostMessageW( st->parent, WM_SYSCOMMAND, cmd, 0 );
            }
            else if (action == 2)
            {
                TRACE( "csd_overlay SC_CLOSE parent %p\n", st->parent );
                PostMessageW( st->parent, WM_SYSCOMMAND, SC_CLOSE, 0 );
            }
        }
        return 0;

    case WM_TIMER:
        if (wparam == CSD_BG_SAMPLE_TIMER_ID && st)
        {
            BOOL had_bg = st->has_sampled_bg;
            COLORREF prev_bg = st->sampled_bg;
            BOOL ok;

            st->sample_ticks++;
            ok = csd_try_sample_parent_bg( hwnd, st, st->sample_ticks >= 10 );
            if (ok)
            {
                if (!had_bg || st->sampled_bg != prev_bg)
                {
                    st->stable_ticks = 0;
                    InvalidateRect( hwnd, NULL, FALSE );
                }
                else
                {
                    st->stable_ticks++;
                }
            }
            if ((ok && st->sample_ticks >= 4 && st->stable_ticks >= 2) || st->sample_ticks >= 14)
                KillTimer( hwnd, CSD_BG_SAMPLE_TIMER_ID );
            return 0;
        }
        return DefWindowProcW( hwnd, msg, wparam, lparam );

    case WM_CAPTURECHANGED:
        if (st && st->pressed_btn != -1)
        {
            st->pressed_btn = -1;
            InvalidateRect( hwnd, NULL, FALSE );
        }
        return 0;

    case WM_NCDESTROY:
        RemovePropW( hwnd, L"__wine_csd_overlay" );
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, 0 );
        free( st );
        return DefWindowProcW( hwnd, msg, wparam, lparam );

    default:
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }
}

static void core_window_sync_csd_overlay( struct core_window *impl )
{
    static const WCHAR overlay_class[] = L"__wine_csd_caption_overlay";
    static BOOL class_registered;
    DPI_AWARENESS_CONTEXT prev_dpi;
    int extend_dip, dpi, cluster_w, titlebar_h, overlay_w, overlay_h;
    RECT rc;

    if (!impl || !impl->hwnd || !IsWindow( impl->hwnd )) return;

    extend_dip = (int)(INT_PTR)GetPropW( impl->hwnd, L"__wine_extend_titlebar" );
    if (extend_dip <= 0)
    {
        if (impl->csd_overlay_hwnd && IsWindow( impl->csd_overlay_hwnd ))
            ShowWindow( impl->csd_overlay_hwnd, SW_HIDE );
        return;
    }

    if (!class_registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = csd_overlay_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.lpszClassName = overlay_class;
        wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
        RegisterClassExW( &wc );
        class_registered = TRUE;
    }

    /* Operate in per-monitor physical DPI so the overlay HWND and its direct X11
     * subwindow always have identical 1:1 physical pixel geometry and never clip
     * on scaled monitors or DPI-unaware/system-DPI-aware apps. */
    prev_dpi = csd_enter_pm_dpi();

    if (!GetClientRect( impl->hwnd, &rc ) || rc.right <= 0 || rc.bottom <= 0)
    {
        csd_leave_pm_dpi( prev_dpi );
        return;
    }

    if (!impl->csd_overlay_hwnd || !IsWindow( impl->csd_overlay_hwnd ))
    {
        impl->csd_overlay_hwnd = CreateWindowExW(
            0, overlay_class, NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 1, 1,
            impl->hwnd, NULL, GetModuleHandleW( NULL ), NULL );
    }

    if (impl->csd_overlay_hwnd)
    {
        dpi = GetDpiForWindow( impl->csd_overlay_hwnd );
        if (!dpi) dpi = GetDpiForWindow( impl->hwnd );
        if (!dpi) dpi = 96;
        cluster_w = MulDiv( 138, dpi, 96 );
        titlebar_h = MulDiv( extend_dip, dpi, 96 );
        if (titlebar_h < 24) titlebar_h = 24;
        overlay_w = min( cluster_w, (int)rc.right );
        overlay_h = min( titlebar_h, (int)rc.bottom );

        SetWindowPos( impl->csd_overlay_hwnd, HWND_TOP,
                      rc.right - overlay_w, 0, overlay_w, overlay_h,
                      SWP_SHOWWINDOW | SWP_NOACTIVATE );
        InvalidateRect( impl->csd_overlay_hwnd, NULL, FALSE );
    }

    csd_leave_pm_dpi( prev_dpi );
}

static LRESULT CALLBACK core_window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct core_window *impl = (struct core_window *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );

    if (!impl && msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        impl = (struct core_window *)cs->lpCreateParams;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)impl );
    }
    if (!impl)
        impl = thread_core_window;

    {
        static int cwp_depth;
        if (cwp_depth < 10) TRACE( "CWP d=%d hwnd %p msg %x\n", cwp_depth, hwnd, msg );
        cwp_depth++;
    }
    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (GetPropW( hwnd, L"__wine_extend_titlebar" ))
            return 0;
        return DefWindowProcW( hwnd, msg, wparam, lparam );

    case WM_NCPAINT:
        if (GetPropW( hwnd, L"__wine_extend_titlebar" ))
            return 0;
        return DefWindowProcW( hwnd, msg, wparam, lparam );

    case WM_NCACTIVATE:
        if (GetPropW( hwnd, L"__wine_extend_titlebar" ))
        {
            if (impl && impl->csd_overlay_hwnd && IsWindow( impl->csd_overlay_hwnd ))
                InvalidateRect( impl->csd_overlay_hwnd, NULL, FALSE );
            return TRUE;
        }
        return DefWindowProcW( hwnd, msg, wparam, lparam );

    case WM_NCHITTEST:
    {
        int extend_dip = (int)(INT_PTR)GetPropW( hwnd, L"__wine_extend_titlebar" );
        if (extend_dip > 0)
        {
            POINT pt = { (short)LOWORD( lparam ), (short)HIWORD( lparam ) };
            RECT wr;
            UINT dpi = GetDpiForWindow( hwnd );
            int border, cluster_w, extend_h;

            if (!dpi) dpi = 96;
            border = MulDiv( 6, dpi, 96 );
            cluster_w = MulDiv( 138, dpi, 96 );
            extend_h = MulDiv( extend_dip, dpi, 96 );

            if (GetWindowRect( hwnd, &wr ))
            {
                if (!IsZoomed( hwnd ))
                {
                    BOOL on_left   = pt.x >= wr.left && pt.x < wr.left + border;
                    BOOL on_right  = pt.x < wr.right && pt.x >= wr.right - border;
                    BOOL on_top    = pt.y >= wr.top && pt.y < wr.top + border;
                    BOOL on_bottom = pt.y < wr.bottom && pt.y >= wr.bottom - border;

                    if (on_top && on_left) return HTTOPLEFT;
                    if (on_top && on_right) return HTTOPRIGHT;
                    if (on_bottom && on_left) return HTBOTTOMLEFT;
                    if (on_bottom && on_right) return HTBOTTOMRIGHT;
                    if (on_left) return HTLEFT;
                    if (on_right) return HTRIGHT;
                    if (on_top) return HTTOP;
                    if (on_bottom) return HTBOTTOM;
                }
                if (pt.y >= wr.top && pt.y < wr.top + extend_h &&
                    pt.x >= wr.left && pt.x < wr.right - cluster_w)
                    return HTCAPTION;
            }
            return HTCLIENT;
        }
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }

    case WM_COREWINDOW_EXTEND_TITLEBAR:
        if (wparam == (WPARAM)-1)
        {
            if (impl && impl->csd_overlay_hwnd && IsWindow( impl->csd_overlay_hwnd ))
                InvalidateRect( impl->csd_overlay_hwnd, NULL, FALSE );
        }
        else if (impl)
        {
            core_window_sync_csd_overlay( impl );
        }
        return 0;

    case WM_WINDOWPOSCHANGED:
    {
        LRESULT ret = DefWindowProcW( hwnd, msg, wparam, lparam );
        if (impl) core_window_sync_csd_overlay( impl );
        return ret;
    }

    case WM_WINDOWPOSCHANGING:
    {
        /* The WinUI3 Window defaults Width/Height to NaN; when the app activates before the
         * XAML-set size is applied, the window lands at 0x0, never maps, and WM_PAINT (hence
         * layout, Loaded and the whole WebView2 chain) never fires. Clamp to a sane default. */
        WINDOWPOS *pos = (WINDOWPOS *)lparam;
        if (!pos->cx || !pos->cy)
        {
            SIZE work_area = get_work_area_size();
            if (!pos->cx) pos->cx = work_area.cx;
            if (!pos->cy) pos->cy = work_area.cy;
        }
        return 0;
    }

    case WM_DISPLAYCHANGE:
        refresh_work_area();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint( hwnd, &ps );
        EndPaint( hwnd, &ps );
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_POINTERDOWN:
    {
        int x = LOWORD( lparam );
        int y = HIWORD( lparam );
        if (impl && impl->pointer_pressed_handler)
            core_window_deliver_pointer_pressed( impl, x, y );
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_POINTERUP:
    {
        int x = LOWORD( lparam );
        int y = HIWORD( lparam );
        if (impl && impl->pointer_released_handler)
            core_window_deliver_pointer_released( impl, x, y );
        return 0;
    }

    case WM_CHAR:
    {
        if (impl)
            core_window_deliver_character_received( impl, (UINT32)wparam );
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        if (impl)
            core_window_deliver_keydown( impl, (__x_ABI_CWindows_CSystem_CVirtualKey)wparam );
        return 0;
    }

    case WM_KEYUP:
    {
        if (impl)
            core_window_deliver_keyup( impl, (__x_ABI_CWindows_CSystem_CVirtualKey)wparam );
        return 0;
    }

    case WM_SIZE:
    {
        float width = (float)LOWORD( lparam );
        float height = (float)HIWORD( lparam );
        if (impl && (width != impl->width || height != impl->height))
        {
            unsigned int i;
            __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *args = NULL;

            impl->width = width;
            impl->height = height;
            if (width > 0 && height > 0)
            {
                HWND child = GetWindow( hwnd, GW_CHILD );
                while (child)
                {
                    WCHAR cls[64];
                    if (GetClassNameW( child, cls, ARRAY_SIZE(cls) ) && !wcscmp( cls, L"Chrome_WidgetWin_0" ))
                    {
                        SetWindowPos( child, NULL, 0, 0, (int)width, (int)height,
                                SWP_NOZORDER | SWP_NOACTIVATE );
                        break;
                    }
                    child = GetWindow( child, GW_HWNDNEXT );
                }
            }
            for (i = 0; i < ARRAY_SIZE( impl->size_changed_handlers ); i++)
            {
                __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *h =
                    impl->size_changed_handlers[i];
                if (!h) continue;
                if (!args) args = window_size_changed_args_create( width, height );
                if (args)
                    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_Invoke(
                        h, &impl->ICoreWindow_iface, args );
            }
            if (args)
                __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs_Release( args );
        }
        if (impl) core_window_sync_csd_overlay( impl );
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }

    case WM_SHOWWINDOW:
    {
        if (impl && impl->visibility_changed_handler)
        {
            __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *args =
                visibility_changed_args_create( !!wparam );
            if (args)
            {
                __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_Invoke(
                    impl->visibility_changed_handler, &impl->ICoreWindow_iface, args );
                __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs_Release( args );
            }
        }
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }

    case WM_ACTIVATE:
    {
        TRACE( "WM_ACTIVATE on tid %lu, handler %p, wparam %#lx.\n", GetCurrentThreadId(), impl ? impl->activated_handler : NULL, (long)wparam );
        if (LOWORD( wparam ) != WA_INACTIVE && impl && impl->activated_handler)
        {
            CoreWindowActivationState state = (LOWORD( wparam ) == WA_CLICKACTIVE) ?
                CoreWindowActivationState_PointerActivated : CoreWindowActivationState_CodeActivated;
            core_window_deliver_activation( impl, state );
        }
        if (impl && impl->csd_overlay_hwnd && IsWindow( impl->csd_overlay_hwnd ))
        {
            struct csd_overlay_state *st =
                (struct csd_overlay_state *)GetWindowLongPtrW( impl->csd_overlay_hwnd, GWLP_USERDATA );
            if (st) csd_try_sample_parent_bg( impl->csd_overlay_hwnd, st, st->has_sampled_bg );
            InvalidateRect( impl->csd_overlay_hwnd, NULL, FALSE );
        }
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }

    case WM_CLOSE:
        DestroyWindow( hwnd );
        PostQuitMessage( 0 );
        return 0;

    case WM_CORE_DISPATCHER_POST:
    {
        /* A foreign-thread RunAsync/RunIdleAsync rides the window queue; this runs on the owner
         * thread, drained by its ProcessEvents pump. */
        struct dispatcher_post *post = (struct dispatcher_post *)lparam;
        TRACE( "dispatcher post drained on tid %lu (post %p).\n", GetCurrentThreadId(), post );
        if (post)
        {
            void *handler = post->handler;
            BOOL idle = post->idle;
            free( post );
            if (handler)
            {
                dispatcher_invoke( handler, idle );
                IUnknown_Release( (IUnknown *)handler );
            }
        }
        return 0;
    }

    case WM_NCDESTROY:
        if (impl) impl->csd_overlay_hwnd = NULL;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, 0 );
        return DefWindowProcW( hwnd, msg, wparam, lparam );

    default:
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }
}

/* ---- the shell's window properties: navigation client, window client adapter ---- */

static inline struct core_window *impl_from_ICoreWindowNavigation( ICoreWindowNavigation *iface )
{
    return CONTAINING_RECORD( iface, struct core_window, ICoreWindowNavigation_iface );
}

static inline struct core_window *impl_from_ICoreWindowAdapterInterop( ICoreWindowAdapterInterop *iface )
{
    return CONTAINING_RECORD( iface, struct core_window, ICoreWindowAdapterInterop_iface );
}

static HRESULT WINAPI core_window_navigation_QueryInterface( ICoreWindowNavigation *iface, REFIID iid, void **out )
{
    struct core_window *impl = impl_from_ICoreWindowNavigation( iface );
    return ICoreWindow_QueryInterface( &impl->ICoreWindow_iface, iid, out );
}
static ULONG WINAPI core_window_navigation_AddRef( ICoreWindowNavigation *iface )
{
    struct core_window *impl = impl_from_ICoreWindowNavigation( iface );
    return ICoreWindow_AddRef( &impl->ICoreWindow_iface );
}
static ULONG WINAPI core_window_navigation_Release( ICoreWindowNavigation *iface )
{
    struct core_window *impl = impl_from_ICoreWindowNavigation( iface );
    return ICoreWindow_Release( &impl->ICoreWindow_iface );
}
static HRESULT WINAPI core_window_navigation_GetIids( ICoreWindowNavigation *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI core_window_navigation_GetRuntimeClassName( ICoreWindowNavigation *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CoreWindow", 26, name );
}
static HRESULT WINAPI core_window_navigation_GetTrustLevel( ICoreWindowNavigation *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI core_window_navigation_get_NavigationClient( ICoreWindowNavigation *iface, IUnknown **client )
{
    struct core_window *impl = impl_from_ICoreWindowNavigation( iface );

    TRACE( "iface %p, client %p.\n", iface, client );
    if (!client) return E_POINTER;
    *client = impl->navigation_client;
    if (*client) IUnknown_AddRef( *client );
    return S_OK;
}
static HRESULT WINAPI core_window_navigation_put_NavigationClient( ICoreWindowNavigation *iface, IUnknown *client )
{
    struct core_window *impl = impl_from_ICoreWindowNavigation( iface );

    TRACE( "iface %p, client %p.\n", iface, client );
    if (impl->navigation_client) IUnknown_Release( impl->navigation_client );
    impl->navigation_client = client;
    if (client) IUnknown_AddRef( client );
    return S_OK;
}

static const ICoreWindowNavigationVtbl core_window_navigation_vtbl =
{
    core_window_navigation_QueryInterface,
    core_window_navigation_AddRef,
    core_window_navigation_Release,
    core_window_navigation_GetIids,
    core_window_navigation_GetRuntimeClassName,
    core_window_navigation_GetTrustLevel,
    core_window_navigation_get_NavigationClient,
    core_window_navigation_put_NavigationClient,
};

static HRESULT WINAPI core_window_adapter_QueryInterface( ICoreWindowAdapterInterop *iface, REFIID iid, void **out )
{
    struct core_window *impl = impl_from_ICoreWindowAdapterInterop( iface );
    return ICoreWindow_QueryInterface( &impl->ICoreWindow_iface, iid, out );
}
static ULONG WINAPI core_window_adapter_AddRef( ICoreWindowAdapterInterop *iface )
{
    struct core_window *impl = impl_from_ICoreWindowAdapterInterop( iface );
    return ICoreWindow_AddRef( &impl->ICoreWindow_iface );
}
static ULONG WINAPI core_window_adapter_Release( ICoreWindowAdapterInterop *iface )
{
    struct core_window *impl = impl_from_ICoreWindowAdapterInterop( iface );
    return ICoreWindow_Release( &impl->ICoreWindow_iface );
}
static HRESULT WINAPI core_window_adapter_GetIids( ICoreWindowAdapterInterop *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI core_window_adapter_GetRuntimeClassName( ICoreWindowAdapterInterop *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.CoreWindow", 26, name );
}
static HRESULT WINAPI core_window_adapter_GetTrustLevel( ICoreWindowAdapterInterop *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
/* The sub-adapter getters: this module has no consumer for any of them, and NULL is what a window
 * with no adapter reports, which is what every caller of these saw before an adapter existed. */
static HRESULT WINAPI core_window_adapter_no_adapter( ICoreWindowAdapterInterop *iface, IUnknown **out )
{
    TRACE( "iface %p.\n", iface );
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI core_window_adapter_SetWindowClientAdapter( ICoreWindowAdapterInterop *iface, IUnknown *adapter )
{
    struct core_window *impl = impl_from_ICoreWindowAdapterInterop( iface );

    TRACE( "iface %p, adapter %p.\n", iface, adapter );
    if (impl->window_client_adapter) IUnknown_Release( impl->window_client_adapter );
    impl->window_client_adapter = adapter;
    if (adapter) IUnknown_AddRef( adapter );
    return S_OK;
}

static const ICoreWindowAdapterInteropVtbl core_window_adapter_vtbl =
{
    core_window_adapter_QueryInterface,
    core_window_adapter_AddRef,
    core_window_adapter_Release,
    core_window_adapter_GetIids,
    core_window_adapter_GetRuntimeClassName,
    core_window_adapter_GetTrustLevel,
    {
        (void *)core_window_adapter_no_adapter, (void *)core_window_adapter_no_adapter,
        (void *)core_window_adapter_no_adapter, (void *)core_window_adapter_no_adapter,
        (void *)core_window_adapter_no_adapter, (void *)core_window_adapter_no_adapter,
        (void *)core_window_adapter_no_adapter,
    },
    core_window_adapter_SetWindowClientAdapter,
};

/* Window class name for this process's CoreWindow, chosen so winex11 publishes it
 * as WM_CLASS: the package's short name (DisplayName identity), not the exe name.
 * Resolution order: seeded AppModel registry identity (ApplicationShortName, then
 * DisplayName on the package key and its VisualElements subkey, HKCU before HKLM),
 * then the GetCurrentApplicationUserModelId app id (the part after '!'), then the
 * classic name. windows.ui.xaml seeds ApplicationShortName from the manifest-resolved
 * DisplayName at package init; a XAML app therefore lands on its package display name. */
static const WCHAR *core_window_class_name( void )
{
    static const WCHAR default_class[] = L"Windows.UI.Core.CoreWindow";
    static const WCHAR appmodel_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\";
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    static WCHAR cached[128];
    static BOOL done;
    WCHAR full[330], sub[640], val[128];
    UINT32 len;
    SIZE_T i;

    if (done) return cached[0] ? cached : default_class;
    done = TRUE;

    len = ARRAY_SIZE( full );
    if (GetCurrentPackageFullName( &len, full ) == ERROR_SUCCESS && full[0])
    {
        static const WCHAR *const subkeys[] = { L"", L"\\VisualElements" };
        static const WCHAR *const values[] = { L"ApplicationShortName", L"DisplayName" };
        SIZE_T j;
        for (i = 0; i < ARRAY_SIZE( roots ); i++)
        {
            for (j = 0; j < ARRAY_SIZE( subkeys ); j++)
            {
                SIZE_T k;
                HKEY hkey;
                lstrcpyW( sub, full );
                lstrcatW( sub, subkeys[j] );
                {
                    WCHAR path[640];
                    lstrcpyW( path, appmodel_key );
                    lstrcatW( path, sub );
                    if (RegOpenKeyExW( roots[i], path, 0, KEY_READ, &hkey ) != ERROR_SUCCESS)
                        continue;
                }
                for (k = 0; k < ARRAY_SIZE( values ); k++)
                {
                    DWORD type = 0, size = sizeof(val);
                    LONG ret = RegQueryValueExW( hkey, values[k], NULL, &type, (BYTE *)val, &size );
                    if (ret == ERROR_SUCCESS && type == REG_SZ && val[0])
                    {
                        RegCloseKey( hkey );
                        lstrcpynW( cached, val, ARRAY_SIZE(cached) );
                        return cached;
                    }
                }
                RegCloseKey( hkey );
            }
        }
    }

    len = ARRAY_SIZE(val);
    if (GetCurrentApplicationUserModelId( &len, val ) == ERROR_SUCCESS && val[0])
    {
        WCHAR *p = wcschr( val, L'!' );
        const WCHAR *app = p ? p + 1 : val;
        if (*app)
        {
            lstrcpynW( cached, app, ARRAY_SIZE(cached) );
            return cached;
        }
    }
    return default_class;
}

/* Create a CoreWindow with its own dispatcher. The shell's window factory hands the window its
 * title, position and size (PrivateCreateCoreWindow below); the plain WinRT path has no arguments
 * and keeps the size this module has always used, which is why the geometry is applied only when
 * the caller asked for it. */
static struct core_window *create_core_window( const WCHAR *title, INT x, INT y, UINT width, UINT height,
                                               BOOL set_geometry )
{
    const WCHAR *class_name = core_window_class_name();
    WNDCLASSEXW wc = { sizeof(wc) };
    struct core_window *impl;
    HINSTANCE inst = GetModuleHandleW( NULL );
    SIZE work_area = get_work_area_size();

    /* Clamp initial window size to monitor work area so UWP windows never overflow small displays
     * (e.g. 1024x768 Xvfb -> 1020x740) while still allowing larger sizes on larger displays. */
    if (work_area.cx <= 1280 && work_area.cy <= 1024)
    {
        if (width > 1020) width = 1020;
        if (height > 740) height = 740;
    }
    else
    {
        if (work_area.cx > 4 && width > (UINT)(work_area.cx - 4))
            width = (UINT)(work_area.cx - 4);
        if (work_area.cy > 28 && height > (UINT)(work_area.cy - 28))
            height = (UINT)(work_area.cy - 28);
    }

    if (thread_core_window)
    {
        impl = thread_core_window;
        InterlockedIncrement( &impl->ref );
        if (set_geometry)
        {
            /* A factory can be asked for a window of a different size than the one this thread
             * already has; the view follows the window, so move it rather than make a second. */
            if (title) SetWindowTextW( impl->hwnd, title );
            SetWindowPos( impl->hwnd, NULL, x, y, width, height,
                          SWP_NOZORDER | SWP_NOACTIVATE | (x == CW_USEDEFAULT ? SWP_NOMOVE : 0) );
        }
        TRACE( "reusing thread CoreWindow %p, hwnd %p\n", impl, impl->hwnd );
        return impl;
    }

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICoreWindow_iface.lpVtbl = &core_window_vtbl;
    impl->ICoreWindowInterop_iface.lpVtbl = &core_window_interop_vtbl;
    impl->ICoreWindowNavigation_iface.lpVtbl = &core_window_navigation_vtbl;
    impl->ICoreWindowAdapterInterop_iface.lpVtbl = &core_window_adapter_vtbl;
    impl->IWeakReferenceSource_iface = cw_wrs_vtbl;
    impl->dummy_iface.lpVtbl = (void *)dummy_vtbl;
    impl->dispatcher = core_dispatcher_create( impl );
    impl->flow_direction = CoreWindowFlowDirection_LeftToRight;
    impl->input_enabled = TRUE;
    impl->ref = 1;

    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = core_window_proc;
    wc.hInstance = inst;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW( &wc );

    thread_core_window = impl;
    InterlockedIncrement( &process_core_window_count );

    impl->hwnd = CreateWindowExW( 0, class_name, title,
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  x, y, width, height,
                                  NULL, NULL, inst, impl );
    if (impl->hwnd)
    {
        SetWindowLongPtrW( impl->hwnd, GWLP_USERDATA, (LONG_PTR)impl );
        ShowWindow( impl->hwnd, SW_SHOW );
        UpdateWindow( impl->hwnd );
    }
    TRACE( "created CoreWindow %p, hwnd %p\n", impl, impl->hwnd );
    return impl;
}

ICoreWindow *core_window_create( void )
{
    SIZE work_area = get_work_area_size();
    struct core_window *impl;
    UINT w = (UINT)(work_area.cx * 0.8f + 0.5f);
    UINT h = (UINT)(work_area.cy * 0.8f + 0.5f);
    if (w < 800) w = 800;
    if (h < 600) h = 600;
    if (work_area.cx <= 1280 && work_area.cy <= 1024)
    {
        if (w > 1020) w = 1020;
        if (h > 740) h = 740;
    }
    else
    {
        if (w > 1920) w = 1920;
        if (h > 1440) h = 1440;
    }
    impl = create_core_window( NULL, 0, 0, w, h, FALSE );

    return impl ? &impl->ICoreWindow_iface : NULL;
}

/* Fire the KeyDown/KeyUp/CharacterReceived listeners of the CoreWindow that owns hwnd.
 * windows.ui.xaml's host window proc sits on the window slot ahead of core_window_proc
 * (it swallows pointer messages), so it drives this for keyboard messages it has
 * already routed to its own listener registries. */
HRESULT WINAPI core_window_notify_key( HWND hwnd, UINT msg, WPARAM wparam )
{
    struct core_window *impl = (struct core_window *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    if (!impl) impl = thread_core_window; /* the XAML host window is not always the CoreWindow's own HWND */
    if (!impl) return E_FAIL;
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        return core_window_deliver_keydown( impl, (__x_ABI_CWindows_CSystem_CVirtualKey)wparam ) ? S_OK : S_FALSE;
    case WM_KEYUP:
        return core_window_deliver_keyup( impl, (__x_ABI_CWindows_CSystem_CVirtualKey)wparam ) ? S_OK : S_FALSE;
    case WM_CHAR:
        return core_window_deliver_character_received( impl, (UINT32)wparam ) ? S_OK : S_FALSE;
    default:
        return E_INVALIDARG;
    }
}

/* Windows' shell makes a view's CoreWindow through its UAP window factory, which ends in this
 * private windows.ui export; wineuwp's in-process broker stand-in calls it the same way, and it is
 * what lets a view get a CoreWindow the shell would otherwise have to hand it.
 *
 * window_type is the factory's switch: 1 is the main window of a view. The flags and the parent are
 * the shell's UAP window options, which this module has no use for: the window it makes is the one
 * the thread has, and a thread has exactly one. */
HRESULT PrivateCreateCoreWindow( DWORD window_type, const WCHAR *title, INT x, INT y,
                                 UINT width, UINT height, DWORD flags, HWND parent,
                                 const GUID *iid, void **out )
{
    struct core_window *impl;
    HRESULT hr;

    TRACE( "window_type %lu, title %s, x %d, y %d, size %ux%u, flags %#lx, parent %p.\n",
           window_type, debugstr_w( title ), x, y, width, height, flags, parent );

    if (!out) return E_POINTER;
    *out = NULL;
    if (window_type != 1)
    {
        FIXME( "window type %lu not supported.\n", window_type );
        return E_NOTIMPL;
    }
    if (!width || !height) return E_INVALIDARG;

    if (!(impl = create_core_window( title, x, y, width, height, TRUE ))) return E_OUTOFMEMORY;

    if (!iid)
    {
        *out = &impl->ICoreWindow_iface;
        return S_OK;
    }
    hr = ICoreWindow_QueryInterface( &impl->ICoreWindow_iface, iid, (void **)out );
    ICoreWindow_Release( &impl->ICoreWindow_iface );
    return hr;
}

/* ---- Windows.UI.Core.CoreCursor ---- */

struct core_cursor
{
    ICoreCursor ICoreCursor_iface;
    LONG ref;
    CoreCursorType type;
    UINT32 id;
};

static inline struct core_cursor *impl_from_ICoreCursor( ICoreCursor *iface )
{
    return CONTAINING_RECORD( iface, struct core_cursor, ICoreCursor_iface );
}

static HRESULT WINAPI core_cursor_QueryInterface( ICoreCursor *iface, REFIID iid, void **out )
{
    struct core_cursor *impl = impl_from_ICoreCursor( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreCursor ))
    {
        *out = &impl->ICoreCursor_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "iid %s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI core_cursor_AddRef( ICoreCursor *iface )
{
    struct core_cursor *impl = impl_from_ICoreCursor( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI core_cursor_Release( ICoreCursor *iface )
{
    struct core_cursor *impl = impl_from_ICoreCursor( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI core_cursor_GetIids( ICoreCursor *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI core_cursor_GetRuntimeClassName( ICoreCursor *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreCursor", 26, name ); }
static HRESULT WINAPI core_cursor_GetTrustLevel( ICoreCursor *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI core_cursor_get_Id( ICoreCursor *iface, UINT32 *value )
{
    struct core_cursor *impl = impl_from_ICoreCursor( iface );
    if (!value) return E_POINTER;
    *value = impl->id;
    return S_OK;
}
static HRESULT WINAPI core_cursor_get_Type( ICoreCursor *iface, CoreCursorType *value )
{
    struct core_cursor *impl = impl_from_ICoreCursor( iface );
    if (!value) return E_POINTER;
    *value = impl->type;
    return S_OK;
}

static const struct ICoreCursorVtbl core_cursor_vtbl =
{
    core_cursor_QueryInterface,
    core_cursor_AddRef,
    core_cursor_Release,
    core_cursor_GetIids,
    core_cursor_GetRuntimeClassName,
    core_cursor_GetTrustLevel,
    core_cursor_get_Id,
    core_cursor_get_Type,
};

struct core_cursor_factory
{
    IActivationFactory IActivationFactory_iface;
    ICoreCursorFactory ICoreCursorFactory_iface;
    LONG ref;
};

static inline struct core_cursor_factory *impl_from_IActivationFactory_cursor( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct core_cursor_factory, IActivationFactory_iface );
}
static inline struct core_cursor_factory *impl_from_ICoreCursorFactory( ICoreCursorFactory *iface )
{
    return CONTAINING_RECORD( iface, struct core_cursor_factory, ICoreCursorFactory_iface );
}

static HRESULT WINAPI cursor_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct core_cursor_factory *impl = impl_from_IActivationFactory_cursor( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreCursorFactory ))
    {
        *out = &impl->ICoreCursorFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "iid %s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cursor_factory_AddRef( IActivationFactory *iface )
{
    struct core_cursor_factory *impl = impl_from_IActivationFactory_cursor( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cursor_factory_Release( IActivationFactory *iface )
{
    struct core_cursor_factory *impl = impl_from_IActivationFactory_cursor( iface );
    return InterlockedDecrement( &impl->ref );
}
static HRESULT WINAPI cursor_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI cursor_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreCursor", 26, name ); }
static HRESULT WINAPI cursor_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI cursor_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{ FIXME( "stub\n" ); return E_NOTIMPL; }

static const struct IActivationFactoryVtbl cursor_activation_factory_vtbl =
{
    cursor_factory_QueryInterface,
    cursor_factory_AddRef,
    cursor_factory_Release,
    cursor_factory_GetIids,
    cursor_factory_GetRuntimeClassName,
    cursor_factory_GetTrustLevel,
    cursor_factory_ActivateInstance,
};

static HRESULT WINAPI cursor_factory_CreateCursor( ICoreCursorFactory *iface, CoreCursorType type, UINT32 id, ICoreCursor **cursor )
{
    struct core_cursor *impl;
    TRACE( "iface %p, type %d, id %u, cursor %p\n", iface, type, id, cursor );
    if (!cursor) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICoreCursor_iface.lpVtbl = &core_cursor_vtbl;
    impl->ref = 1;
    impl->type = type;
    impl->id = id;
    *cursor = &impl->ICoreCursor_iface;
    return S_OK;
}

static HRESULT WINAPI cursor_icf_QueryInterface( ICoreCursorFactory *iface, REFIID iid, void **out )
{
    struct core_cursor_factory *impl = impl_from_ICoreCursorFactory( iface );
    return cursor_factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI cursor_icf_AddRef( ICoreCursorFactory *iface )
{
    struct core_cursor_factory *impl = impl_from_ICoreCursorFactory( iface );
    return cursor_factory_AddRef( &impl->IActivationFactory_iface );
}
static ULONG WINAPI cursor_icf_Release( ICoreCursorFactory *iface )
{
    struct core_cursor_factory *impl = impl_from_ICoreCursorFactory( iface );
    return cursor_factory_Release( &impl->IActivationFactory_iface );
}
static HRESULT WINAPI cursor_icf_GetIids( ICoreCursorFactory *iface, ULONG *count, IID **iids )
{
    struct core_cursor_factory *impl = impl_from_ICoreCursorFactory( iface );
    return cursor_factory_GetIids( &impl->IActivationFactory_iface, count, iids );
}
static HRESULT WINAPI cursor_icf_GetRuntimeClassName( ICoreCursorFactory *iface, HSTRING *name )
{
    struct core_cursor_factory *impl = impl_from_ICoreCursorFactory( iface );
    return cursor_factory_GetRuntimeClassName( &impl->IActivationFactory_iface, name );
}
static HRESULT WINAPI cursor_icf_GetTrustLevel( ICoreCursorFactory *iface, TrustLevel *level )
{
    struct core_cursor_factory *impl = impl_from_ICoreCursorFactory( iface );
    return cursor_factory_GetTrustLevel( &impl->IActivationFactory_iface, level );
}

static const struct ICoreCursorFactoryVtbl cursor_factory_vtbl =
{
    cursor_icf_QueryInterface,
    cursor_icf_AddRef,
    cursor_icf_Release,
    cursor_icf_GetIids,
    cursor_icf_GetRuntimeClassName,
    cursor_icf_GetTrustLevel,
    cursor_factory_CreateCursor,
};

static struct core_cursor_factory cursor_factory_obj =
{
    {&cursor_activation_factory_vtbl},
    {&cursor_factory_vtbl},
    1,
};

IActivationFactory *core_cursor_factory = &cursor_factory_obj.IActivationFactory_iface;
