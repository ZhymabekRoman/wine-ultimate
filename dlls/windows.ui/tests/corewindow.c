/*
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
#include <stdarg.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#include "wine/test.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"

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

#define ICoreWindowInterop_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define ICoreWindowInterop_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ICoreWindowInterop_Release(This) (This)->lpVtbl->Release(This)
#define ICoreWindowInterop_GetWindowHandle(This,hwnd) (This)->lpVtbl->GetWindowHandle(This,hwnd)
#define ICoreWindowInterop_SetMessageHandled(This,value) (This)->lpVtbl->SetMessageHandled(This,value)

#define check_interface( obj, iid, exp ) check_interface_( __LINE__, obj, iid, exp )
static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *unk, *iface = obj;
    HRESULT hr, expected_hr;

    expected_hr = supported ? S_OK : E_NOINTERFACE;
    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == expected_hr, "Got hr %#lx, expected %#lx.\n", hr, expected_hr );
    if (SUCCEEDED( hr ))
        IUnknown_Release( unk );
}

static void test_CoreWindow(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_UI_Core_CoreWindow;
    ICoreWindowStatic *core_window_static;
    IActivationFactory *factory;
    ICoreWindow *core_window;
    ICoreWindowInterop *interop;
    ICoreDispatcher *dispatcher;
    HWND hwnd;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_ICoreWindowStatic, TRUE );

    /* Test ICoreWindowStatic */
    hr = IActivationFactory_QueryInterface( factory, &IID_ICoreWindowStatic, (void **)&core_window_static );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    /* Crash on Windows */
    if (0)
    {
    hr = ICoreWindowStatic_GetForCurrentThread( core_window_static, 0 );
    ok( hr == E_INVALIDARG, "Got unexpected hr %#lx.\n", hr );
    }

    /* Test GetForCurrentThread, Dispatcher, and ICoreWindowInterop */
    core_window = NULL;
    hr = ICoreWindowStatic_GetForCurrentThread( core_window_static, &core_window );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    ok( core_window != NULL, "Expected non-NULL core_window.\n" );

    if (core_window)
    {
        dispatcher = NULL;
        hr = ICoreWindow_get_Dispatcher( core_window, &dispatcher );
        ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
        ok( dispatcher != NULL, "Expected non-NULL dispatcher.\n" );
        if (dispatcher)
            ICoreDispatcher_Release( dispatcher );

        interop = NULL;
        hr = ICoreWindow_QueryInterface( core_window, &IID_ICoreWindowInterop, (void **)&interop );
        ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
        ok( interop != NULL, "Expected non-NULL interop.\n" );

        if (interop)
        {
            hwnd = NULL;
            hr = ICoreWindowInterop_GetWindowHandle( interop, &hwnd );
            ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
            ok( hwnd != NULL, "Expected non-NULL hwnd.\n" );
            ICoreWindowInterop_Release( interop );
        }

        ICoreWindow_Release( core_window );
    }

    ICoreWindowStatic_Release( core_window_static );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "Got unexpected ref %ld.\n", ref );
}

struct size_changed_handler
{
    ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs_iface;
    LONG ref;
    LONG invoked;
    Size last_size;
};

static inline struct size_changed_handler *impl_from_size_changed_handler( ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct size_changed_handler, ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs_iface );
}

static HRESULT WINAPI size_changed_handler_QueryInterface( ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs *iface, REFIID iid, void **out )
{
    struct size_changed_handler *impl = impl_from_size_changed_handler( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs ))
    {
        *out = &impl->ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI size_changed_handler_AddRef( ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs *iface )
{
    struct size_changed_handler *impl = impl_from_size_changed_handler( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI size_changed_handler_Release( ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs *iface )
{
    struct size_changed_handler *impl = impl_from_size_changed_handler( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI size_changed_handler_Invoke( ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs *iface,
                                                   ICoreWindow *sender, IWindowSizeChangedEventArgs *args )
{
    struct size_changed_handler *impl = impl_from_size_changed_handler( iface );
    HRESULT hr;

    impl->invoked++;
    if (args)
    {
        hr = IWindowSizeChangedEventArgs_get_Size( args, &impl->last_size );
        ok( hr == S_OK, "get_Size failed, hr %#lx\n", hr );
    }
    return S_OK;
}

static const ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgsVtbl size_changed_handler_vtbl =
{
    size_changed_handler_QueryInterface,
    size_changed_handler_AddRef,
    size_changed_handler_Release,
    size_changed_handler_Invoke,
};

struct visibility_changed_handler
{
    ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs_iface;
    LONG ref;
    LONG invoked;
    boolean last_visible;
};

static inline struct visibility_changed_handler *impl_from_visibility_changed_handler( ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct visibility_changed_handler, ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs_iface );
}

static HRESULT WINAPI visibility_changed_handler_QueryInterface( ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs *iface, REFIID iid, void **out )
{
    struct visibility_changed_handler *impl = impl_from_visibility_changed_handler( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs ))
    {
        *out = &impl->ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI visibility_changed_handler_AddRef( ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs *iface )
{
    struct visibility_changed_handler *impl = impl_from_visibility_changed_handler( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI visibility_changed_handler_Release( ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs *iface )
{
    struct visibility_changed_handler *impl = impl_from_visibility_changed_handler( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI visibility_changed_handler_Invoke( ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs *iface,
                                                         ICoreWindow *sender, IVisibilityChangedEventArgs *args )
{
    struct visibility_changed_handler *impl = impl_from_visibility_changed_handler( iface );
    HRESULT hr;

    impl->invoked++;
    if (args)
    {
        hr = IVisibilityChangedEventArgs_get_Visible( args, &impl->last_visible );
        ok( hr == S_OK, "get_Visible failed, hr %#lx\n", hr );
    }
    return S_OK;
}

static const ITypedEventHandler_CoreWindow_VisibilityChangedEventArgsVtbl visibility_changed_handler_vtbl =
{
    visibility_changed_handler_QueryInterface,
    visibility_changed_handler_AddRef,
    visibility_changed_handler_Release,
    visibility_changed_handler_Invoke,
};

struct window_activated_handler
{
    ITypedEventHandler_CoreWindow_WindowActivatedEventArgs ITypedEventHandler_CoreWindow_WindowActivatedEventArgs_iface;
    LONG ref;
    LONG invoked;
    CoreWindowActivationState last_state;
};

static inline struct window_activated_handler *impl_from_window_activated_handler( ITypedEventHandler_CoreWindow_WindowActivatedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct window_activated_handler, ITypedEventHandler_CoreWindow_WindowActivatedEventArgs_iface );
}

static HRESULT WINAPI window_activated_handler_QueryInterface( ITypedEventHandler_CoreWindow_WindowActivatedEventArgs *iface, REFIID iid, void **out )
{
    struct window_activated_handler *impl = impl_from_window_activated_handler( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ITypedEventHandler_CoreWindow_WindowActivatedEventArgs ))
    {
        *out = &impl->ITypedEventHandler_CoreWindow_WindowActivatedEventArgs_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI window_activated_handler_AddRef( ITypedEventHandler_CoreWindow_WindowActivatedEventArgs *iface )
{
    struct window_activated_handler *impl = impl_from_window_activated_handler( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI window_activated_handler_Release( ITypedEventHandler_CoreWindow_WindowActivatedEventArgs *iface )
{
    struct window_activated_handler *impl = impl_from_window_activated_handler( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI window_activated_handler_Invoke( ITypedEventHandler_CoreWindow_WindowActivatedEventArgs *iface,
                                                       ICoreWindow *sender, IWindowActivatedEventArgs *args )
{
    struct window_activated_handler *impl = impl_from_window_activated_handler( iface );
    HRESULT hr;

    impl->invoked++;
    if (args)
    {
        hr = IWindowActivatedEventArgs_get_WindowActivationState( args, &impl->last_state );
        ok( hr == S_OK, "get_WindowActivationState failed, hr %#lx\n", hr );
    }
    return S_OK;
}

static const ITypedEventHandler_CoreWindow_WindowActivatedEventArgsVtbl window_activated_handler_vtbl =
{
    window_activated_handler_QueryInterface,
    window_activated_handler_AddRef,
    window_activated_handler_Release,
    window_activated_handler_Invoke,
};

static void test_CoreWindow_events(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_UI_Core_CoreWindow;
    ICoreWindowStatic *core_window_static;
    IActivationFactory *factory;
    ICoreWindow *core_window;
    ICoreWindowInterop *interop;
    HWND hwnd = NULL;
    HSTRING str;
    HRESULT hr;
    EventRegistrationToken token_size, token_vis, token_act;
    struct size_changed_handler size_handler = { { &size_changed_handler_vtbl }, 1, 0, { 0.0f, 0.0f } };
    struct visibility_changed_handler vis_handler = { { &visibility_changed_handler_vtbl }, 1, 0, FALSE };
    struct window_activated_handler act_handler = { { &window_activated_handler_vtbl }, 1, 0, 0 };

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    if (FAILED( hr )) return;

    hr = IActivationFactory_QueryInterface( factory, &IID_ICoreWindowStatic, (void **)&core_window_static );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );

    core_window = NULL;
    hr = ICoreWindowStatic_GetForCurrentThread( core_window_static, &core_window );
    ok( hr == S_OK && core_window != NULL, "GetForCurrentThread failed\n" );

    hr = ICoreWindow_QueryInterface( core_window, &IID_ICoreWindowInterop, (void **)&interop );
    ok( hr == S_OK && interop != NULL, "QI ICoreWindowInterop failed\n" );
    hr = ICoreWindowInterop_GetWindowHandle( interop, &hwnd );
    ok( hr == S_OK && hwnd != NULL, "GetWindowHandle failed\n" );
    ICoreWindowInterop_Release( interop );

    /* Test SizeChanged event via WM_SIZE */
    hr = ICoreWindow_add_SizeChanged( core_window, &size_handler.ITypedEventHandler_CoreWindow_WindowSizeChangedEventArgs_iface, &token_size );
    ok( hr == S_OK, "add_SizeChanged failed, hr %#lx\n", hr );
    size_handler.invoked = 0;

    SendMessageW( hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM( 800, 600 ) );
    ok( size_handler.invoked == 1, "Expected SizeChanged invoked once, got %ld\n", size_handler.invoked );
    ok( size_handler.last_size.Width == 800.0f && size_handler.last_size.Height == 600.0f,
        "Expected size 800x600, got %fx%f\n", size_handler.last_size.Width, size_handler.last_size.Height );

    /* Sending same size again should not re-invoke */
    SendMessageW( hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM( 800, 600 ) );
    ok( size_handler.invoked == 1, "Expected SizeChanged not re-invoked for identical size\n" );

    /* Sending different size invokes again */
    SendMessageW( hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM( 640, 480 ) );
    ok( size_handler.invoked == 2, "Expected SizeChanged invoked twice, got %ld\n", size_handler.invoked );
    ok( size_handler.last_size.Width == 640.0f && size_handler.last_size.Height == 480.0f,
        "Expected size 640x480, got %fx%f\n", size_handler.last_size.Width, size_handler.last_size.Height );

    hr = ICoreWindow_remove_SizeChanged( core_window, token_size );
    ok( hr == S_OK, "remove_SizeChanged failed, hr %#lx\n", hr );

    /* Test VisibilityChanged event via WM_SHOWWINDOW */
    hr = ICoreWindow_add_VisibilityChanged( core_window, &vis_handler.ITypedEventHandler_CoreWindow_VisibilityChangedEventArgs_iface, &token_vis );
    ok( hr == S_OK, "add_VisibilityChanged failed, hr %#lx\n", hr );
    vis_handler.invoked = 0;

    SendMessageW( hwnd, WM_SHOWWINDOW, FALSE, 0 );
    ok( vis_handler.invoked == 1, "Expected VisibilityChanged invoked once, got %ld\n", vis_handler.invoked );
    ok( vis_handler.last_visible == FALSE, "Expected visible FALSE, got %d\n", vis_handler.last_visible );

    SendMessageW( hwnd, WM_SHOWWINDOW, TRUE, 0 );
    ok( vis_handler.invoked == 2, "Expected VisibilityChanged invoked twice, got %ld\n", vis_handler.invoked );
    ok( vis_handler.last_visible == TRUE, "Expected visible TRUE, got %d\n", vis_handler.last_visible );

    hr = ICoreWindow_remove_VisibilityChanged( core_window, token_vis );
    ok( hr == S_OK, "remove_VisibilityChanged failed, hr %#lx\n", hr );

    /* Test Activated event via WM_ACTIVATE */
    hr = ICoreWindow_add_Activated( core_window, &act_handler.ITypedEventHandler_CoreWindow_WindowActivatedEventArgs_iface, &token_act );
    ok( hr == S_OK, "add_Activated failed, hr %#lx\n", hr );
    act_handler.invoked = 0;

    SendMessageW( hwnd, WM_ACTIVATE, MAKEWPARAM( WA_ACTIVE, 0 ), 0 );
    ok( act_handler.invoked == 1, "Expected Activated invoked once, got %ld\n", act_handler.invoked );
    ok( act_handler.last_state == CoreWindowActivationState_CodeActivated, "Expected CodeActivated, got %d\n", act_handler.last_state );

    SendMessageW( hwnd, WM_ACTIVATE, MAKEWPARAM( WA_CLICKACTIVE, 0 ), 0 );
    ok( act_handler.invoked == 2, "Expected Activated invoked twice, got %ld\n", act_handler.invoked );
    ok( act_handler.last_state == CoreWindowActivationState_PointerActivated, "Expected PointerActivated, got %d\n", act_handler.last_state );

    SendMessageW( hwnd, WM_ACTIVATE, MAKEWPARAM( WA_INACTIVE, 0 ), 0 );
    ok( act_handler.invoked == 2, "Expected Activated not invoked on WA_INACTIVE\n" );

    hr = ICoreWindow_remove_Activated( core_window, token_act );
    ok( hr == S_OK, "remove_Activated failed, hr %#lx\n", hr );

    /* Test WM_CLOSE posts quit message to exit ProcessUntilQuit */
    {
        ICoreDispatcher *dispatcher = NULL;
        hr = ICoreWindow_get_Dispatcher( core_window, &dispatcher );
        ok( hr == S_OK && dispatcher != NULL, "get_Dispatcher failed\n" );
        PostMessageW( hwnd, WM_CLOSE, 0, 0 );
        hr = ICoreDispatcher_ProcessEvents( dispatcher, CoreProcessEventsOption_ProcessUntilQuit );
        ok( hr == S_OK, "ProcessEvents failed, hr %#lx\n", hr );
        ICoreDispatcher_Release( dispatcher );
    }

    ICoreWindow_Release( core_window );
    ICoreWindowStatic_Release( core_window_static );
    IActivationFactory_Release( factory );
}

START_TEST(corewindow)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_CoreWindow();
    test_CoreWindow_events();

    RoUninitialize();
}
