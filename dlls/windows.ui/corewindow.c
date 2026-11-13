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

WINE_DEFAULT_DEBUG_CHANNEL(ui);

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

struct core_window
{
    ICoreWindow ICoreWindow_iface;
    ICoreWindowInterop ICoreWindowInterop_iface;
    ICoreWindowNavigation ICoreWindowNavigation_iface;
    ICoreWindowAdapterInterop ICoreWindowAdapterInterop_iface;
    IInspectable dummy_iface;
    ICoreDispatcher *dispatcher;
    IUnknown *navigation_client;
    IUnknown *window_client_adapter;
    HWND hwnd;
    LONG ref;
    float width;
    float height;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs *visibility_changed_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *size_changed_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs *activated_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs *character_received_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *keydown_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *keyup_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *pointer_pressed_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *pointer_released_handler;
};

static __thread struct core_window *thread_core_window;

static struct core_window *create_core_window( const WCHAR *title, INT x, INT y, UINT width, UINT height,
                                               BOOL set_geometry );

DEFINE_IINSPECTABLE( corewindow_static, ICoreWindowStatic, struct corewindow_statics, IActivationFactory_iface )

static BOOL ondemand_corewindow_enabled( void )
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *v = getenv( "WINE_COREWINDOW_ONDEMAND" );
        cached = !v || strcmp( v, "0" ) != 0;
    }
    return cached;
}

static HRESULT STDMETHODCALLTYPE corewindow_static_GetForCurrentThread( ICoreWindowStatic *iface, ICoreWindow **windows )
{
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
        if (!ondemand_corewindow_enabled())
        {
            *windows = NULL;
            return S_OK;
        }
        if (!create_core_window( NULL, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768, FALSE ))
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

struct core_dispatcher
{
    ICoreDispatcher ICoreDispatcher_iface;
    ICoreAcceleratorKeys ICoreAcceleratorKeys_iface;
    struct core_window *window;   /* the owning window: its hwnd is the queue's door, its thread the owner */
    DWORD owner_tid;
    LONG ref;
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
    FIXME( "%s E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI core_dispatcher_AddRef( ICoreDispatcher *iface )
{ return InterlockedIncrement( &impl_from_ICoreDispatcher( iface )->ref ); }
static ULONG WINAPI core_dispatcher_Release( ICoreDispatcher *iface )
{
    struct core_dispatcher *impl = impl_from_ICoreDispatcher( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI core_dispatcher_GetIids( ICoreDispatcher *iface, ULONG *count, IID **iids ) { FIXME("stub\n"); return E_NOTIMPL; }
static HRESULT WINAPI core_dispatcher_GetRuntimeClassName( ICoreDispatcher *iface, HSTRING *name ) { FIXME("stub\n"); return E_NOTIMPL; }
static HRESULT WINAPI core_dispatcher_GetTrustLevel( ICoreDispatcher *iface, TrustLevel *level ) { FIXME("stub\n"); return E_NOTIMPL; }static HRESULT WINAPI core_dispatcher_get_AutomationProviderDelegate( ICoreDispatcher *iface, IInspectable **value )
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
    {
        void (STDMETHODCALLTYPE *invoke)(void *, void *, int) = (*(void ***)handler)[3];
        invoke( handler, iface, 1 /* Completed */ );
    }
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
        cached = v && strcmp( v, "0" ) != 0;
    }
    return cached;
}

static void dispatcher_run_or_post( struct core_dispatcher *impl, void *agileCallback, BOOL idle )
{
    HRESULT (STDMETHODCALLTYPE *invoke)(void *) = (*(void ***)agileCallback)[3];

    /* Live bisect (WhatsApp Desktop p53 wave, gate57/58): queue-routed foreign RunAsync was the
     * 1.10-vs-1.11 behavior delta that blocked the unpackaged WinUI3 client's managed init; the
     * 1.10 baseline runs every callback inline on the caller.  The window-queue route is opt-in
     * via WINE_COREWINDOW_STRICT_DISPATCH; the owner_tid recording stays live in both modes. */
    if (!strict_dispatch_enabled() || GetCurrentThreadId() == impl->owner_tid)
    {
        invoke( agileCallback );
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
    invoke( agileCallback );
}

static HRESULT WINAPI core_dispatcher_RunAsync( ICoreDispatcher *iface, CoreDispatcherPriority priority,
        __x_ABI_CWindows_CUI_CCore_CIDispatchedHandler *agileCallback, IAsyncAction **asyncAction )
{
    struct core_dispatcher *impl = impl_from_ICoreDispatcher( iface );

    TRACE( "iface %p, priority %d, agileCallback %p, asyncAction %p\n", iface, priority, agileCallback, asyncAction );
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
        if (thread_core_window == impl) thread_core_window = NULL;
        if (impl->hwnd) DestroyWindow( impl->hwnd );
        if (impl->dispatcher) ICoreDispatcher_Release( impl->dispatcher );
        if (impl->navigation_client) IUnknown_Release( impl->navigation_client );
        if (impl->window_client_adapter) IUnknown_Release( impl->window_client_adapter );
        if (impl->visibility_changed_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_Release( impl->visibility_changed_handler );
        if (impl->size_changed_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_Release( impl->size_changed_handler );
        if (impl->activated_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs_Release( impl->activated_handler );
        if (impl->character_received_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Release( impl->character_received_handler );
        if (impl->keydown_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keydown_handler );
        if (impl->keyup_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keyup_handler );
        if (impl->pointer_pressed_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Release( impl->pointer_pressed_handler );
        if (impl->pointer_released_handler)
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_Release( impl->pointer_released_handler );
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
        value->X = 0; value->Y = 0; value->Width = 1024; value->Height = 768;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_get_CustomProperties( ICoreWindow *iface, __x_ABI_CWindows_CFoundation_CCollections_CIPropertySet **value )
{ FIXME( "stub\n" ); if (value) *value = NULL; return S_OK; }
static HRESULT WINAPI core_window_get_Dispatcher( ICoreWindow *iface, ICoreDispatcher **value )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    FIXME( "iface %p semi-stub.\n", iface );
    if (value)
    {
        if (impl->dispatcher) ICoreDispatcher_AddRef( impl->dispatcher );
        *value = impl->dispatcher;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_get_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection *value )
{ FIXME( "stub\n" ); if (value) *value = 0; return S_OK; }
static HRESULT WINAPI core_window_put_FlowDirection( ICoreWindow *iface, CoreWindowFlowDirection value )
{ FIXME( "stub\n" ); return S_OK; }
static HRESULT WINAPI core_window_get_IsInputEnabled( ICoreWindow *iface, boolean *value )
{ FIXME( "stub\n" ); if (value) *value = TRUE; return S_OK; }
static HRESULT WINAPI core_window_put_IsInputEnabled( ICoreWindow *iface, boolean value )
{ FIXME( "stub\n" ); return S_OK; }
static HRESULT WINAPI core_window_get_PointerCursor( ICoreWindow *iface, ICoreCursor **value )
{ FIXME( "stub\n" ); if (value) *value = NULL; return S_OK; }
static HRESULT WINAPI core_window_put_PointerCursor( ICoreWindow *iface, ICoreCursor *value )
{ FIXME( "stub\n" ); return S_OK; }
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
{ FIXME( "stub\n" ); if (state) *state = 0; return S_OK; }
static HRESULT WINAPI core_window_GetKeyState( ICoreWindow *iface, __x_ABI_CWindows_CSystem_CVirtualKey key, CoreVirtualKeyStates *state )
{ FIXME( "stub\n" ); if (state) *state = 0; return S_OK; }
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
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_AddRef( handler );
        impl->character_received_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_CharacterReceived( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (impl->character_received_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Release( impl->character_received_handler );
        impl->character_received_handler = NULL;
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
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_AddRef( handler );
        impl->keydown_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_KeyDown( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (impl->keydown_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keydown_handler );
        impl->keydown_handler = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI core_window_add_KeyUp( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_AddRef( handler );
        impl->keyup_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_KeyUp( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (impl->keyup_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Release( impl->keyup_handler );
        impl->keyup_handler = NULL;
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
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_AddRef( handler );
        impl->size_changed_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI core_window_remove_SizeChanged( ICoreWindow *iface, EventRegistrationToken cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    if (impl->size_changed_handler)
    {
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_Release( impl->size_changed_handler );
        impl->size_changed_handler = NULL;
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

static const struct ICoreWindowVtbl core_window_vtbl =
{
    core_window_QueryInterface,
    core_window_AddRef,
    core_window_Release,
    core_window_GetIids,
    core_window_GetRuntimeClassName,
    core_window_GetTrustLevel,
    /* ICoreWindow properties */
    core_window_get_AutomationHostProvider,
    core_window_get_Bounds,
    core_window_get_CustomProperties,
    core_window_get_Dispatcher,
    core_window_get_FlowDirection,
    core_window_put_FlowDirection,
    core_window_get_IsInputEnabled,
    core_window_put_IsInputEnabled,
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
    core_window_remove_PointerReleased,
    core_window_add_TouchHitTesting,
    core_window_remove_TouchHitTesting,
    core_window_add_PointerWheelChanged,
    core_window_remove_PointerWheelChanged,
    core_window_add_SizeChanged,
    core_window_remove_SizeChanged,
    core_window_add_VisibilityChanged,
    core_window_remove_VisibilityChanged,
};

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

static void core_window_deliver_keydown( struct core_window *impl, __x_ABI_CWindows_CSystem_CVirtualKey key )
{
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *args;
    if (!impl || !impl->keydown_handler) return;
    if (!(args = key_event_args_create( key ))) return;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Invoke(
        impl->keydown_handler, &impl->ICoreWindow_iface, args );
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( args );
}

static void core_window_deliver_keyup( struct core_window *impl, __x_ABI_CWindows_CSystem_CVirtualKey key )
{
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs *args;
    if (!impl || !impl->keyup_handler) return;
    if (!(args = key_event_args_create( key ))) return;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_Invoke(
        impl->keyup_handler, &impl->ICoreWindow_iface, args );
    __x_ABI_CWindows_CUI_CCore_CIKeyEventArgs_Release( args );
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

static void core_window_deliver_character_received( struct core_window *impl, UINT32 key_code )
{
    __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs *args;
    if (!impl || !impl->character_received_handler) return;
    if (!(args = character_received_args_create( key_code ))) return;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_Invoke(
        impl->character_received_handler, &impl->ICoreWindow_iface, args );
    __x_ABI_CWindows_CUI_CCore_CICharacterReceivedEventArgs_Release( args );
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
        if (cwp_depth < 10) ERR( "CWP d=%d hwnd %p msg %x\n", cwp_depth, hwnd, msg );
        cwp_depth++;
    }
    switch (msg)
    {
    case WM_WINDOWPOSCHANGING:
    {
        /* The WinUI3 Window defaults Width/Height to NaN; when the app activates before the
         * XAML-set size is applied, the window lands at 0x0, never maps, and WM_PAINT (hence
         * layout, Loaded and the whole WebView2 chain) never fires. Clamp to a sane default. */
        WINDOWPOS *pos = (WINDOWPOS *)lparam;
        if (!pos->cx || !pos->cy)
        {
            if (!pos->cx) pos->cx = 960;
            if (!pos->cy) pos->cy = 768;
        }
        return 0;
    }

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
        if (impl && impl->character_received_handler)
            core_window_deliver_character_received( impl, (UINT32)wparam );
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (impl && impl->keydown_handler)
            core_window_deliver_keydown( impl, (__x_ABI_CWindows_CSystem_CVirtualKey)wparam );
        return 0;
    }

    case WM_KEYUP:
    {
        if (impl && impl->keyup_handler)
            core_window_deliver_keyup( impl, (__x_ABI_CWindows_CSystem_CVirtualKey)wparam );
        return 0;
    }

    case WM_SIZE:
    {
        float width = (float)LOWORD( lparam );
        float height = (float)HIWORD( lparam );
        if (impl && (width != impl->width || height != impl->height))
        {
            impl->width = width;
            impl->height = height;
            if (impl->size_changed_handler)
            {
                __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *args =
                    window_size_changed_args_create( width, height );
                if (args)
                {
                    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_Invoke(
                        impl->size_changed_handler, &impl->ICoreWindow_iface, args );
                    __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs_Release( args );
                }
            }
        }
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
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }

    case WM_CLOSE:
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
            free( post );
            if (handler)
            {
                HRESULT (STDMETHODCALLTYPE *invoke)(void *) = (*(void ***)handler)[3];
                invoke( handler );
                IUnknown_Release( (IUnknown *)handler );
            }
        }
        return 0;
    }

    case WM_NCDESTROY:
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

/* Create a CoreWindow with its own dispatcher. The shell's window factory hands the window its
 * title, position and size (PrivateCreateCoreWindow below); the plain WinRT path has no arguments
 * and keeps the size this module has always used, which is why the geometry is applied only when
 * the caller asked for it. */
static struct core_window *create_core_window( const WCHAR *title, INT x, INT y, UINT width, UINT height,
                                               BOOL set_geometry )
{
    static const WCHAR class_name[] = L"Windows.UI.Core.CoreWindow";
    WNDCLASSEXW wc = { sizeof(wc) };
    struct core_window *impl;
    HINSTANCE inst = GetModuleHandleW( NULL );

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
    impl->dummy_iface.lpVtbl = (void *)dummy_vtbl;
    impl->dispatcher = core_dispatcher_create( impl );
    impl->ref = 1;

    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = core_window_proc;
    wc.hInstance = inst;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW( &wc );

    thread_core_window = impl;

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
    struct core_window *impl = create_core_window( NULL, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768, FALSE );

    return impl ? &impl->ICoreWindow_iface : NULL;
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

static const struct ICoreCursorFactoryVtbl cursor_factory_vtbl =
{
    (void *)cursor_factory_QueryInterface,
    (void *)cursor_factory_AddRef,
    (void *)cursor_factory_Release,
    (void *)cursor_factory_GetIids,
    (void *)cursor_factory_GetRuntimeClassName,
    (void *)cursor_factory_GetTrustLevel,
    cursor_factory_CreateCursor,
};

static struct core_cursor_factory cursor_factory_obj =
{
    {&cursor_activation_factory_vtbl},
    {&cursor_factory_vtbl},
    1,
};

IActivationFactory *core_cursor_factory = &cursor_factory_obj.IActivationFactory_iface;
