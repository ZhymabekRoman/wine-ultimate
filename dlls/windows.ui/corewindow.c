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

DEFINE_IINSPECTABLE( corewindow_static, ICoreWindowStatic, struct corewindow_statics, IActivationFactory_iface )

static HRESULT STDMETHODCALLTYPE corewindow_static_GetForCurrentThread( ICoreWindowStatic *iface, ICoreWindow **windows )
{
    FIXME( "iface %p, windows %p semi-stub.\n", iface, windows );
    if (!windows) return E_POINTER;
    *windows = NULL;
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
/* The typed-event-handler parameter types differ per event but are ABI-equivalent
 * (all pointer-sized). Suppress the pointer-type mismatch for these stubs. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wincompatible-function-pointer-types"
#endif

struct core_dispatcher
{
    ICoreDispatcher ICoreDispatcher_iface;
    ICoreAcceleratorKeys ICoreAcceleratorKeys_iface;
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
static HRESULT WINAPI core_dispatcher_keys_add( ICoreAcceleratorKeys *iface, void *handler, EventRegistrationToken *cookie )
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

static HRESULT WINAPI core_dispatcher_RunAsync( ICoreDispatcher *iface, CoreDispatcherPriority priority,
        void *agileCallback, IAsyncAction **asyncAction )
{
    TRACE( "iface %p, priority %d, agileCallback %p, asyncAction %p\n", iface, priority, agileCallback, asyncAction );
    if (agileCallback)
    {
        HRESULT (STDMETHODCALLTYPE *invoke)(void *) = (*(void ***)agileCallback)[3];
        invoke( agileCallback );
    }
    if (asyncAction)
    {
        *asyncAction = dispatcher_async_action_create();
    }
    return S_OK;
}

static HRESULT WINAPI core_dispatcher_RunIdleAsync( ICoreDispatcher *iface, void *agileCallback, IAsyncAction **asyncAction )
{
    TRACE( "iface %p, agileCallback %p, asyncAction %p\n", iface, agileCallback, asyncAction );
    if (agileCallback)
    {
        HRESULT (STDMETHODCALLTYPE *invoke)(void *) = (*(void ***)agileCallback)[3];
        invoke( agileCallback );
    }
    if (asyncAction)
    {
        *asyncAction = dispatcher_async_action_create();
    }
    return S_OK;
}
static HRESULT WINAPI core_dispatcher_HasThreadAccess( ICoreDispatcher *iface, boolean *value )
{ FIXME( "stub\n" ); if (value) *value = TRUE; return S_OK; }
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

static ICoreDispatcher *core_dispatcher_create(void)
{
    struct core_dispatcher *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICoreDispatcher_iface.lpVtbl = &core_dispatcher_vtbl;
    impl->ICoreAcceleratorKeys_iface.lpVtbl = &core_dispatcher_keys_vtbl;
    impl->ref = 1;
    return &impl->ICoreDispatcher_iface;
}

/* ---- minimal CoreWindow ---- */

static const GUID IID_ICoreWindowInterop = { 0x45d64a29, 0xa63e, 0x4cb6, { 0xb4, 0x98, 0x57, 0x81, 0xd2, 0x97, 0xcb, 0x98 } };

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

struct core_window
{
    ICoreWindow ICoreWindow_iface;
    ICoreWindowInterop ICoreWindowInterop_iface;
    IInspectable dummy_iface;
    ICoreDispatcher *dispatcher;
    HWND hwnd;
    LONG ref;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs *visibility_changed_handler;
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *size_changed_handler;
};

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
static __thread struct core_window *thread_core_window;

static ULONG WINAPI core_window_Release( ICoreWindow *iface )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (thread_core_window == impl) thread_core_window = NULL;
        if (impl->hwnd) DestroyWindow( impl->hwnd );
        if (impl->dispatcher) ICoreDispatcher_Release( impl->dispatcher );
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
static HRESULT WINAPI core_window_get_CustomProperties( ICoreWindow *iface, void **value )
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

static HRESULT WINAPI core_window_Activate( ICoreWindow *iface )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, hwnd %p\n", iface, impl->hwnd );
    if (impl->hwnd)
    {
        ShowWindow( impl->hwnd, SW_SHOW );
        UpdateWindow( impl->hwnd );
    }
    if (impl->visibility_changed_handler)
    {
        __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *args = visibility_changed_args_create( TRUE );
        if (args)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_Invoke(
                impl->visibility_changed_handler, &impl->ICoreWindow_iface, args );
            __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs_Release( args );
        }
    }
    return S_OK;
}
static HRESULT WINAPI core_window_Close( ICoreWindow *iface )
{ FIXME( "iface %p semi-stub.\n", iface ); return S_OK; }
static HRESULT WINAPI core_window_GetAsyncKeyState( ICoreWindow *iface, int key, CoreVirtualKeyStates *state )
{ FIXME( "stub\n" ); if (state) *state = 0; return S_OK; }
static HRESULT WINAPI core_window_GetKeyState( ICoreWindow *iface, int key, CoreVirtualKeyStates *state )
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

CORE_WINDOW_EVENT_ADD( Activated, Windows__CUI__CCore__CWindowActivatedEventArgs )
CORE_WINDOW_EVENT_REMOVE( Activated )
CORE_WINDOW_EVENT_ADD( AutomationProviderRequested, Windows__CUI__CCore__CAutomationProviderRequestedEventArgs )
CORE_WINDOW_EVENT_REMOVE( AutomationProviderRequested )
CORE_WINDOW_EVENT_ADD( CharacterReceived, Windows__CUI__CCore__CCharacterReceivedEventArgs )
CORE_WINDOW_EVENT_REMOVE( CharacterReceived )
CORE_WINDOW_EVENT_ADD( Closed, Windows__CUI__CCore__CCoreWindowEventArgs )
CORE_WINDOW_EVENT_REMOVE( Closed )
CORE_WINDOW_EVENT_ADD( InputEnabled, Windows__CUI__CCore__CInputEnabledEventArgs )
CORE_WINDOW_EVENT_REMOVE( InputEnabled )
CORE_WINDOW_EVENT_ADD( KeyDown, Windows__CUI__CCore__CKeyEventArgs )
CORE_WINDOW_EVENT_REMOVE( KeyDown )
CORE_WINDOW_EVENT_ADD( KeyUp, Windows__CUI__CCore__CKeyEventArgs )
CORE_WINDOW_EVENT_REMOVE( KeyUp )
CORE_WINDOW_EVENT_ADD( PointerCaptureLost, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerCaptureLost )
CORE_WINDOW_EVENT_ADD( PointerEntered, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerEntered )
CORE_WINDOW_EVENT_ADD( PointerExited, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerExited )
CORE_WINDOW_EVENT_ADD( PointerMoved, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerMoved )
CORE_WINDOW_EVENT_ADD( PointerPressed, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerPressed )
CORE_WINDOW_EVENT_ADD( PointerReleased, Windows__CUI__CCore__CPointerEventArgs )
CORE_WINDOW_EVENT_REMOVE( PointerReleased )
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

static HRESULT WINAPI core_window_add_SizeChanged( ICoreWindow *iface,
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs *handler, EventRegistrationToken *cookie )
{
    struct core_window *impl = impl_from_ICoreWindow( iface );
    TRACE( "iface %p, handler %p\n", iface, handler );
    if (cookie) cookie->value = 1;
    if (handler)
    {
        __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs *args;
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_AddRef( handler );
        impl->size_changed_handler = handler;
        args = window_size_changed_args_create( 1016.0f, 734.0f );
        if (args)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs_Invoke(
                handler, &impl->ICoreWindow_iface, args );
            __x_ABI_CWindows_CUI_CCore_CIWindowSizeChangedEventArgs_Release( args );
        }
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
        __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs *args;
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_AddRef( handler );
        impl->visibility_changed_handler = handler;
        args = visibility_changed_args_create( TRUE );
        if (args)
        {
            __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs_Invoke(
                handler, &impl->ICoreWindow_iface, args );
            __x_ABI_CWindows_CUI_CCore_CIVisibilityChangedEventArgs_Release( args );
        }
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

static LRESULT CALLBACK core_window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

/* Create a CoreWindow with its own dispatcher. */
ICoreWindow *core_window_create(void)
{
    static const WCHAR class_name[] = L"Windows.UI.Core.CoreWindow";
    WNDCLASSEXW wc = { sizeof(wc) };
    struct core_window *impl;
    HINSTANCE inst = GetModuleHandleW( NULL );

    if (thread_core_window)
    {
        InterlockedIncrement( &thread_core_window->ref );
        TRACE( "reusing thread CoreWindow %p, hwnd %p\n", thread_core_window, thread_core_window->hwnd );
        return &thread_core_window->ICoreWindow_iface;
    }

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ICoreWindow_iface.lpVtbl = &core_window_vtbl;
    impl->ICoreWindowInterop_iface.lpVtbl = &core_window_interop_vtbl;
    impl->dummy_iface.lpVtbl = (void *)dummy_vtbl;
    impl->dispatcher = core_dispatcher_create();
    impl->ref = 1;

    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = core_window_proc;
    wc.hInstance = inst;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW( &wc );

    impl->hwnd = CreateWindowExW( 0, class_name, NULL,
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
                                  NULL, NULL, inst, NULL );
    ShowWindow( impl->hwnd, SW_SHOW );
    UpdateWindow( impl->hwnd );
    thread_core_window = impl;
    TRACE( "created CoreWindow %p, hwnd %p\n", impl, impl->hwnd );
    return &impl->ICoreWindow_iface;
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
