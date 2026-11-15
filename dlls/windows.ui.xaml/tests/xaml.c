/*
 * Copyright (C) 2025 Mohamad Al-Jaf
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
#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Devices_Input
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Input
#define WIDL_using_Windows_UI_Core
#define WIDL_using_Windows_ApplicationModel_Core
#include "windows.foundation.h"
#include "windows.devices.input.h"
#include "windows.ui.h"
#include "windows.ui.input.h"
#include "windows.ui.core.h"
#include "windows.applicationmodel.core.h"

#include "wine/test.h"

#define check_interface( obj, iid, is_broken ) check_interface_( __LINE__, obj, iid, is_broken )
static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL is_broken )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK || broken( is_broken && hr == E_NOINTERFACE ), "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
        IUnknown_Release( unk );
}

static void test_ColorHelper(void)
{
    static const WCHAR *color_helper_statics_name = L"Windows.UI.ColorHelper";
    IColorHelperStatics *color_helper_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    Color color;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( color_helper_statics_name, wcslen( color_helper_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( color_helper_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, FALSE );
    check_interface( factory, &IID_IInspectable, FALSE );
    check_interface( factory, &IID_IAgileObject, TRUE /* Missing on Windows older than 1809v2 */ );

    hr = IActivationFactory_QueryInterface( factory, &IID_IColorHelperStatics, (void **)&color_helper_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    memset( &color, 0, sizeof( color ) );
    hr = IColorHelperStatics_FromArgb( color_helper_statics, 255, 255, 255, 255, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IColorHelperStatics_FromArgb( color_helper_statics, 255, 255, 255, 255, &color );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( color.A == 255 && color.R == 255 && color.G == 255 && color.B == 255,
        "got color.A = %u, color.R = %u, color.G = %u, color.B = %u,\n", color.A, color.R, color.G, color.B );

    IColorHelperStatics_Release( color_helper_statics );
    IActivationFactory_Release( factory );
}

static const GUID IID_ISwapChainPanelNative = {0xf92f19d2, 0x3ade, 0x45a6, {0xa2, 0x0c, 0xf6, 0xf1, 0xea, 0x90, 0x55, 0x4b}};

static void test_Application(void)
{
    static const WCHAR *class_name = L"Windows.UI.Xaml.Application";
    IActivationFactory *factory = NULL;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "WindowsCreateString failed, hr %#lx\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr );

    if (SUCCEEDED(hr))
    {
        check_interface( factory, &IID_IUnknown, FALSE );
        check_interface( factory, &IID_IInspectable, FALSE );
        check_interface( factory, &IID_IActivationFactory, FALSE );
        IActivationFactory_Release( factory );
    }
}

/* ---- view lifecycle invariants (WP1.11) ----
 *
 * The Application factory answers IFrameworkViewSource with the view source whose CreateView makes
 * the framework view CoreApplication::Run drives (real XAML answers this QI E_NOINTERFACE, so the
 * checks are the broken-on-Windows case). A minimal CoreWindow stand-in is enough: the view stores
 * the pointer and keeps a reference, nothing else calls into it.
 *
 * The strict refusals are opt-in on the builtin (WINE_XAML_STRICT_LIFECYCLE, live bisect: the
 * unpackaged WinUI3 client activates and pumps across threads, and refusing that pattern
 * regressed it; the builtin defaults to the pre-WP1.11 permissive accept). The builtin latches the
 * environment on first use, so a single test process runs in exactly one mode and the assertions
 * below match the mode of the current environment: with WINE_XAML_STRICT_LIFECYCLE set they assert
 * the strict refusals, unset they assert the permissive accepts. Run the suite both ways to cover
 * both shapes; SetEnvironmentVariableA inside the test cannot flip a latched mode mid-process, so
 * the mode is chosen by the environment of the test run, not by the test.
 */
static BOOL strict_mode( void )
{
    static int cached = -1;

    if (cached == -1)
    {
        const char *v = getenv( "WINE_XAML_STRICT_LIFECYCLE" );
        cached = v && strcmp( v, "0" ) != 0;
    }
    return cached;
}

static const GUID IID_IWindowNative = {0xeecdbf0e, 0xbae9, 0x4cb6, {0xa6, 0x8e, 0x95, 0x98, 0xe1, 0xcb, 0x57, 0xbb}};
static const GUID IID_ICoreWindowInterop = {0x45d64a29, 0xa63e, 0x4cb6, {0xb4, 0x98, 0x57, 0x81, 0xd2, 0x98, 0xcb, 0x4f}};

typedef struct ICoreWindowInterop ICoreWindowInterop;
typedef struct ICoreWindowInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWindowInterop *This, REFIID riid, void **out );
    ULONG (WINAPI *AddRef)( ICoreWindowInterop *This );
    ULONG (WINAPI *Release)( ICoreWindowInterop *This );
    HRESULT (WINAPI *GetWindowHandle)( ICoreWindowInterop *This, HWND *hwnd );
    HRESULT (WINAPI *SetMessageHandled)( ICoreWindowInterop *This, boolean value );
} ICoreWindowInteropVtbl;
struct ICoreWindowInterop { const ICoreWindowInteropVtbl *lpVtbl; };

typedef struct IWindowNative IWindowNative;
typedef struct IWindowNativeVtbl
{
    HRESULT (WINAPI *QueryInterface)( IWindowNative *This, REFIID riid, void **out );
    ULONG (WINAPI *AddRef)( IWindowNative *This );
    ULONG (WINAPI *Release)( IWindowNative *This );
    HRESULT (WINAPI *GetWindowHandle)( IWindowNative *This, HWND *hwnd );
} IWindowNativeVtbl;
struct IWindowNative { const IWindowNativeVtbl *lpVtbl; };

struct fake_window
{
    ICoreWindow ICoreWindow_iface;
    LONG ref;
    HWND hwnd;
};

struct fake_window_interop
{
    ICoreWindowInterop ICoreWindowInterop_iface;
    struct fake_window *window;
};

static struct fake_window_interop fake_interop;

static inline struct fake_window *impl_from_fake( ICoreWindow *iface )
{
    return CONTAINING_RECORD( iface, struct fake_window, ICoreWindow_iface );
}

static HRESULT WINAPI fake_interop_QueryInterface( ICoreWindowInterop *iface, REFIID iid, void **out )
{
    struct fake_window_interop *interop = CONTAINING_RECORD( iface, struct fake_window_interop, ICoreWindowInterop_iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) || IsEqualGUID( iid, &IID_ICoreWindowInterop ))
    {
        *out = iface;
        InterlockedIncrement( &interop->window->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI fake_interop_AddRef( ICoreWindowInterop *iface )
{
    struct fake_window_interop *interop = CONTAINING_RECORD( iface, struct fake_window_interop, ICoreWindowInterop_iface );
    return InterlockedIncrement( &interop->window->ref );
}
static ULONG WINAPI fake_interop_Release( ICoreWindowInterop *iface )
{
    struct fake_window_interop *interop = CONTAINING_RECORD( iface, struct fake_window_interop, ICoreWindowInterop_iface );
    return InterlockedDecrement( &interop->window->ref );
}
static HRESULT WINAPI fake_interop_GetWindowHandle( ICoreWindowInterop *iface, HWND *hwnd )
{
    struct fake_window_interop *interop = CONTAINING_RECORD( iface, struct fake_window_interop, ICoreWindowInterop_iface );
    if (!hwnd) return E_POINTER;
    *hwnd = interop->window->hwnd;
    return S_OK;
}
static HRESULT WINAPI fake_interop_SetMessageHandled( ICoreWindowInterop *iface, boolean value ) { return S_OK; }
static const ICoreWindowInteropVtbl fake_interop_vtbl =
{
    fake_interop_QueryInterface, fake_interop_AddRef, fake_interop_Release,
    fake_interop_GetWindowHandle, fake_interop_SetMessageHandled,
};

static HRESULT WINAPI fake_QueryInterface( ICoreWindow *iface, REFIID iid, void **out )
{
    struct fake_window *window = impl_from_fake( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) || IsEqualGUID( iid, &IID_ICoreWindow ))
    {
        *out = iface;
        InterlockedIncrement( &window->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWindowInterop ))
    {
        *out = &fake_interop.ICoreWindowInterop_iface;
        InterlockedIncrement( &window->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI fake_AddRef( ICoreWindow *iface )
{
    return InterlockedIncrement( &impl_from_fake( iface )->ref );
}
static ULONG WINAPI fake_Release( ICoreWindow *iface )
{
    return InterlockedDecrement( &impl_from_fake( iface )->ref );
}

static const ICoreWindowVtbl fake_window_vtbl =
{
    fake_QueryInterface,
    fake_AddRef,
    fake_Release,
};

static void fake_window_init( struct fake_window *w )
{
    w->ICoreWindow_iface.lpVtbl = &fake_window_vtbl;
    w->ref = 1;
    w->hwnd = (HWND)0x1234;
    fake_interop.ICoreWindowInterop_iface.lpVtbl = &fake_interop_vtbl;
    fake_interop.window = w;
}

static IFrameworkViewSource *get_view_source( void )
{
    static const WCHAR *class_name = L"Windows.UI.Xaml.Application";
    IActivationFactory *factory = NULL;
    IFrameworkViewSource *source = NULL;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED( hr )) return NULL;

    hr = IActivationFactory_QueryInterface( factory, &IID_IFrameworkViewSource, (void **)&source );
    IActivationFactory_Release( factory );
    ok( hr == S_OK || broken( hr == E_NOINTERFACE ), "QI IFrameworkViewSource got hr %#lx.\n", hr );
    return source;
}

static IFrameworkView *create_view( IFrameworkViewSource *source )
{
    IFrameworkView *view = NULL;
    HRESULT hr = IFrameworkViewSource_CreateView( source, &view );
    ok( hr == S_OK, "CreateView got hr %#lx.\n", hr );
    return view;
}

static void test_View_SetWindow( IFrameworkViewSource *source )
{
    struct fake_window w1, w2;
    IFrameworkView *view;
    HRESULT hr;

    winetest_push_context( "SetWindow" );

    fake_window_init( &w1 );
    fake_window_init( &w2 );

    view = create_view( source );
    if (!view) goto done;

    hr = IFrameworkView_Initialize( view, NULL );
    ok( hr == S_OK, "Initialize got hr %#lx.\n", hr );

    hr = IFrameworkView_SetWindow( view, &w1.ICoreWindow_iface );
    ok( hr == S_OK, "SetWindow(w1) got hr %#lx, expected S_OK.\n", hr );

    hr = IFrameworkView_SetWindow( view, &w1.ICoreWindow_iface );
    ok( hr == S_OK, "SetWindow(w1) again got hr %#lx, expected S_OK.\n", hr );

    hr = IFrameworkView_SetWindow( view, &w2.ICoreWindow_iface );
    if (strict_mode())
        ok( hr == E_UNEXPECTED, "SetWindow(w2) got hr %#lx, expected E_UNEXPECTED.\n", hr );
    else
        ok( hr == S_OK, "SetWindow(w2) got hr %#lx, expected S_OK (strict=off, second window accepted).\n", hr );

    IFrameworkView_Release( view );
done:
    winetest_pop_context();
}

static void test_View_Load( IFrameworkViewSource *source )
{
    struct fake_window w1;
    IFrameworkView *view;
    HRESULT hr;

    winetest_push_context( "Load" );

    fake_window_init( &w1 );

    view = create_view( source );
    if (!view) goto done;

    hr = IFrameworkView_Initialize( view, NULL );
    ok( hr == S_OK, "Initialize got hr %#lx.\n", hr );

    hr = IFrameworkView_Load( view, NULL );
    if (strict_mode())
        ok( hr == E_UNEXPECTED, "Load before SetWindow got hr %#lx, expected E_UNEXPECTED.\n", hr );
    else
        ok( hr == S_OK, "Load before SetWindow got hr %#lx, expected S_OK (strict=off, accepted).\n", hr );

    hr = IFrameworkView_SetWindow( view, &w1.ICoreWindow_iface );
    ok( hr == S_OK, "SetWindow got hr %#lx.\n", hr );

    hr = IFrameworkView_Load( view, NULL );
    ok( hr == S_OK, "Load after SetWindow got hr %#lx, expected S_OK.\n", hr );

    hr = IFrameworkView_Load( view, NULL );
    ok( hr == S_OK, "Load twice got hr %#lx, expected S_OK.\n", hr );

    IFrameworkView_Release( view );
done:
    winetest_pop_context();
}

static HANDLE foreign_thread;
static HANDLE foreign_event;
static HRESULT foreign_hr;
static IFrameworkView *foreign_view;
static ICoreWindow *foreign_window;

static DWORD WINAPI set_window_thread( LPVOID param )
{
    foreign_hr = IFrameworkView_SetWindow( foreign_view, foreign_window );
    SetEvent( foreign_event );
    return 0;
}

static void test_View_SetWindow_foreign_thread( IFrameworkViewSource *source )
{
    struct fake_window w1;
    IFrameworkView *view;
    DWORD tid;
    HRESULT hr;

    winetest_push_context( "SetWindow foreign thread" );

    fake_window_init( &w1 );

    view = create_view( source );
    if (!view) goto done;

    hr = IFrameworkView_Initialize( view, NULL );
    ok( hr == S_OK, "Initialize got hr %#lx.\n", hr );

    foreign_view = view;
    foreign_window = &w1.ICoreWindow_iface;
    foreign_hr = 0xdeadbeef;
    foreign_event = CreateEventW( NULL, TRUE, FALSE, NULL );

    foreign_thread = CreateThread( NULL, 0, set_window_thread, NULL, 0, &tid );
    ok( foreign_thread != NULL, "CreateThread failed, error %lu.\n", GetLastError() );
    if (foreign_thread)
    {
        WaitForSingleObject( foreign_event, 5000 );
        if (strict_mode())
            ok( foreign_hr == RPC_E_WRONG_THREAD,
                "SetWindow from foreign thread got hr %#lx, expected RPC_E_WRONG_THREAD.\n", foreign_hr );
        else
            ok( foreign_hr == S_OK,
                "SetWindow from foreign thread got hr %#lx, expected S_OK (strict=off, accepted).\n", foreign_hr );
        CloseHandle( foreign_thread );
    }
    CloseHandle( foreign_event );

    /* the owner thread may still set its window: strict, the foreign call changed nothing;
     * permissive, the foreign call took the same window, so this is the idempotent path */
    hr = IFrameworkView_SetWindow( view, &w1.ICoreWindow_iface );
    ok( hr == S_OK, "owner SetWindow after foreign attempt got hr %#lx, expected S_OK.\n", hr );

    IFrameworkView_Release( view );
done:
    winetest_pop_context();
}

static void test_View_exiting( IFrameworkViewSource *source )
{
    struct fake_window w1;
    IFrameworkView *view;
    HRESULT hr;

    winetest_push_context( "exiting view" );

    fake_window_init( &w1 );

    view = create_view( source );
    if (!view) goto done;

    hr = IFrameworkView_Initialize( view, NULL );
    ok( hr == S_OK, "Initialize got hr %#lx.\n", hr );
    hr = IFrameworkView_SetWindow( view, &w1.ICoreWindow_iface );
    ok( hr == S_OK, "SetWindow got hr %#lx.\n", hr );
    hr = IFrameworkView_Uninitialize( view );
    ok( hr == S_OK, "Uninitialize got hr %#lx.\n", hr );

    hr = IFrameworkView_SetWindow( view, &w1.ICoreWindow_iface );
    if (strict_mode())
        ok( hr == E_APPLICATION_VIEW_EXITING,
            "SetWindow after Uninitialize got hr %#lx, expected E_APPLICATION_VIEW_EXITING.\n", hr );
    else
        ok( hr == S_OK,
            "SetWindow after Uninitialize got hr %#lx, expected S_OK (strict=off, accepted).\n", hr );

    hr = IFrameworkView_Load( view, NULL );
    if (strict_mode())
        ok( hr == E_APPLICATION_VIEW_EXITING,
            "Load after Uninitialize got hr %#lx, expected E_APPLICATION_VIEW_EXITING.\n", hr );
    else
        ok( hr == S_OK,
            "Load after Uninitialize got hr %#lx, expected S_OK (strict=off, accepted).\n", hr );

    IFrameworkView_Release( view );
done:
    winetest_pop_context();
}

static void test_WindowNative( void )
{
    static const WCHAR class_name[] = L"Microsoft.UI.Xaml.Window";
    IFrameworkViewSource *source = get_view_source();
    IFrameworkView *view;
    struct fake_window window;
    IInspectable *xaml_window = NULL;
    IWindowNative *window_native = NULL;
    HSTRING class_id;
    HWND hwnd = NULL;
    HRESULT hr;

    if (!source) return;
    fake_window_init( &window );
    view = create_view( source );
    if (!view) goto done;
    hr = IFrameworkView_Initialize( view, NULL );
    ok( hr == S_OK, "Initialize got hr %#lx.\n", hr );
    hr = IFrameworkView_SetWindow( view, &window.ICoreWindow_iface );
    ok( hr == S_OK, "SetWindow got hr %#lx.\n", hr );

    hr = WindowsCreateString( class_name, wcslen( class_name ), &class_id );
    ok( hr == S_OK, "WindowsCreateString got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = RoActivateInstance( class_id, &xaml_window );
        WindowsDeleteString( class_id );
        ok( hr == S_OK, "RoActivateInstance got hr %#lx.\n", hr );
    }
    if (SUCCEEDED(hr) && xaml_window)
    {
        hr = IInspectable_QueryInterface( xaml_window, &IID_IWindowNative, (void **)&window_native );
        ok( hr == S_OK, "QI IWindowNative got hr %#lx.\n", hr );
        if (SUCCEEDED(hr))
        {
            hr = window_native->lpVtbl->GetWindowHandle( window_native, &hwnd );
            ok( hr == S_OK, "GetWindowHandle got hr %#lx.\n", hr );
            ok( hwnd == window.hwnd, "got hwnd %p, expected %p.\n", hwnd, window.hwnd );
            window_native->lpVtbl->Release( window_native );
        }
        IInspectable_Release( xaml_window );
    }
    IFrameworkView_Release( view );
done:
    IFrameworkViewSource_Release( source );
}

static void test_View_lifecycle( void )
{
    IFrameworkViewSource *source = get_view_source();

    test_WindowNative();

    if (!source)
    {
        win_skip( "no IFrameworkViewSource seam on the Application factory.\n" );
        return;
    }

    test_View_SetWindow( source );
    test_View_Load( source );
    test_View_SetWindow_foreign_thread( source );
    test_View_exiting( source );

    IFrameworkViewSource_Release( source );
}

static void test_SwapChainPanel(void)
{
    static const WCHAR *class_name = L"Windows.UI.Xaml.Controls.SwapChainPanel";
    IActivationFactory *factory = NULL;
    IUnknown *native_panel = NULL;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "WindowsCreateString failed, hr %#lx\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr );

    if (SUCCEEDED(hr))
    {
        check_interface( factory, &IID_IUnknown, FALSE );
        check_interface( factory, &IID_IInspectable, FALSE );
        check_interface( factory, &IID_IActivationFactory, FALSE );

        hr = IActivationFactory_QueryInterface( factory, &IID_ISwapChainPanelNative, (void **)&native_panel );
        ok( hr == S_OK, "QueryInterface ISwapChainPanelNative failed, hr %#lx\n", hr );
        if (SUCCEEDED(hr))
            IUnknown_Release( native_panel );

        IActivationFactory_Release( factory );
    }
}

/* ---- Generic Pointer Bridge tests (CoreWindow + XAML element hit-test/bubbling) ---- */
static const GUID IID_IWindowStatics_test = {0x93bbe361, 0xbe5a, 0x4ee3, {0xb4, 0xa3, 0x95, 0x11, 0x8d, 0xc9, 0x7a, 0x89}};
static const GUID IID_IWindow_test        = {0x3276167d, 0xc9f6, 0x462d, {0x9d, 0xe2, 0xae, 0x4c, 0x1f, 0xd8, 0xc2, 0xe5}};
static const GUID IID_IUIElement_test     = {0x676d0be9, 0xb65c, 0x41c6, {0xba, 0x40, 0x58, 0xcf, 0x87, 0xf2, 0x01, 0xc1}};
static const GUID IID_IFrameworkElement_test = {0xa391d09b, 0x4a99, 0x4b7c, {0x9d, 0x8d, 0x6f, 0xa5, 0xd0, 0x1f, 0x6f, 0xbf}};
static const GUID IID_IPanel_test         = {0xa50a4bbd, 0x8361, 0x469c, {0x90, 0xda, 0xe9, 0xa4, 0x0c, 0x74, 0x74, 0xdf}};
static const GUID IID_IPointerRoutedEventArgs_test = {0xda628f0a, 0x9752, 0x49e2, {0xbd, 0xe2, 0x49, 0xec, 0xca, 0xb9, 0x19, 0x4d}};

struct test_thickness { double Left, Top, Right, Bottom; };

struct cw_ptr_test_state
{
    int pressed_cnt;
    int moved_cnt;
    int released_cnt;
    int wheel_cnt;
    float press_x, press_y;
    UINT32 press_ptr_id;
    boolean press_left;
    PointerUpdateKind press_kind;
    FLOAT press_pressure;
    Rect press_contact;
    BOOL saw_move_55_75;
    PointerUpdateKind rel_kind;
    INT32 last_wheel;
};

struct cw_ptr_handler
{
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs iface;
    LONG ref;
    int kind; /* 0=pressed, 1=moved, 2=released, 3=wheel */
    struct cw_ptr_test_state *st;
};

static HRESULT WINAPI cw_ptr_handler_QI(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *iface,
    REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs ))
    {
        *out = iface;
        ((struct cw_ptr_handler *)iface)->ref++;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cw_ptr_handler_AddRef(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *iface )
{
    return InterlockedIncrement( &((struct cw_ptr_handler *)iface)->ref );
}
static ULONG WINAPI cw_ptr_handler_Release(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *iface )
{
    return InterlockedDecrement( &((struct cw_ptr_handler *)iface)->ref );
}
static HRESULT WINAPI cw_ptr_handler_Invoke(
    __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs *iface,
    ICoreWindow *sender, __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args )
{
    struct cw_ptr_handler *h = (struct cw_ptr_handler *)iface;
    IPointerPoint *pt = NULL;
    IPointerPointProperties *props = NULL;
    Point pos = {0};
    HRESULT hr;

    if (h->kind == 0) h->st->pressed_cnt++;
    else if (h->kind == 1) h->st->moved_cnt++;
    else if (h->kind == 2) h->st->released_cnt++;
    else if (h->kind == 3) h->st->wheel_cnt++;

    if (args && SUCCEEDED( hr = __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_get_CurrentPoint( args, &pt )) && pt)
    {
        IPointerPoint_get_Position( pt, &pos );
        if (h->kind == 0)
        {
            h->st->press_x = pos.X;
            h->st->press_y = pos.Y;
            IPointerPoint_get_pointer_id( pt, &h->st->press_ptr_id );
        }
        else if (h->kind == 1 && pos.X == 55.0f && pos.Y == 75.0f)
        {
            h->st->saw_move_55_75 = TRUE;
        }
        if (SUCCEEDED( IPointerPoint_get_Properties( pt, &props ) ) && props)
        {
            if (h->kind == 0)
            {
                IPointerPointProperties_get_IsLeftButtonPressed( props, &h->st->press_left );
                IPointerPointProperties_get_PointerUpdateKind( props, &h->st->press_kind );
                IPointerPointProperties_get_Pressure( props, &h->st->press_pressure );
                IPointerPointProperties_get_ContactRect( props, &h->st->press_contact );
            }
            else if (h->kind == 2)
            {
                IPointerPointProperties_get_PointerUpdateKind( props, &h->st->rel_kind );
            }
            else if (h->kind == 3)
            {
                IPointerPointProperties_get_MouseWheelDelta( props, &h->st->last_wheel );
            }
            IPointerPointProperties_Release( props );
        }
        IPointerPoint_Release( pt );
    }
    return S_OK;
}
static const __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgsVtbl cw_ptr_handler_vtbl =
{
    cw_ptr_handler_QI,
    cw_ptr_handler_AddRef,
    cw_ptr_handler_Release,
    cw_ptr_handler_Invoke,
};

struct elem_ptr_handler
{
    const void **lpVtbl;
    LONG ref;
    int fired_cnt;
    float last_abs_x, last_abs_y;
    float last_rel_x, last_rel_y;
    void *rel_elem;
    boolean set_handled;
};

static HRESULT WINAPI elem_ptr_QI( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        InterlockedIncrement( &((struct elem_ptr_handler *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI elem_ptr_AddRef( void *iface ) { return InterlockedIncrement( &((struct elem_ptr_handler *)iface)->ref ); }
static ULONG WINAPI elem_ptr_Release( void *iface ) { return InterlockedDecrement( &((struct elem_ptr_handler *)iface)->ref ); }
static HRESULT WINAPI elem_ptr_Invoke( void *iface, IInspectable *sender, void *args )
{
    struct elem_ptr_handler *h = iface;
    void *pargs = NULL;
    h->fired_cnt++;
    if (args && SUCCEEDED( IUnknown_QueryInterface( (IUnknown *)args, &IID_IPointerRoutedEventArgs_test, &pargs ) ) && pargs)
    {
        HRESULT (WINAPI *get_cur)(void *, void *, IPointerPoint **) = (void *)(*(void ***)pargs)[10];
        HRESULT (WINAPI *put_handled)(void *, boolean) = (void *)(*(void ***)pargs)[9];
        IPointerPoint *pt = NULL;
        if (get_cur && SUCCEEDED( get_cur( pargs, NULL, &pt ) ) && pt)
        {
            Point pos = {0};
            IPointerPoint_get_Position( pt, &pos );
            h->last_abs_x = pos.X;
            h->last_abs_y = pos.Y;
            IPointerPoint_Release( pt );
        }
        if (h->rel_elem && get_cur && SUCCEEDED( get_cur( pargs, h->rel_elem, &pt ) ) && pt)
        {
            Point pos = {0};
            IPointerPoint_get_Position( pt, &pos );
            h->last_rel_x = pos.X;
            h->last_rel_y = pos.Y;
            IPointerPoint_Release( pt );
        }
        if (h->set_handled && put_handled)
            put_handled( pargs, TRUE );
        IUnknown_Release( (IUnknown *)pargs );
    }
    return S_OK;
}
static const void *elem_ptr_vtbl[] = { elem_ptr_QI, elem_ptr_AddRef, elem_ptr_Release, elem_ptr_Invoke };

static BOOL (WINAPI *pPeekMessageW)(MSG *, HWND, UINT, UINT, UINT);
static BOOL (WINAPI *pTranslateMessage)(const MSG *);
static LRESULT (WINAPI *pDispatchMessageW)(const MSG *);
static BOOL (WINAPI *pIsWindow)(HWND);
static BOOL (WINAPI *pPostMessageW)(HWND, UINT, WPARAM, LPARAM);
static BOOL (WINAPI *pClientToScreen)(HWND, POINT *);

static void init_user32_procs( void )
{
    HMODULE u32 = GetModuleHandleA( "user32.dll" );
    if (!u32) u32 = LoadLibraryA( "user32.dll" );
    pPeekMessageW = (void *)GetProcAddress( u32, "PeekMessageW" );
    pTranslateMessage = (void *)GetProcAddress( u32, "TranslateMessage" );
    pDispatchMessageW = (void *)GetProcAddress( u32, "DispatchMessageW" );
    pIsWindow = (void *)GetProcAddress( u32, "IsWindow" );
    pPostMessageW = (void *)GetProcAddress( u32, "PostMessageW" );
    pClientToScreen = (void *)GetProcAddress( u32, "ClientToScreen" );
}

static void pump_messages( HWND hwnd )
{
    MSG msg;
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 50)
    {
        while (pPeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            pTranslateMessage( &msg );
            pDispatchMessageW( &msg );
        }
        Sleep( 2 );
    }
}

static void test_PointerBridge( void )
{
    IActivationFactory *win_af = NULL, *grid_af = NULL, *sp_af = NULL;
    IInspectable *win_statics = NULL, *win = NULL;
    IInspectable *parent_obj = NULL, *child_obj = NULL;
    void *parent_ui = NULL, *child_ui = NULL, *parent_fe = NULL, *child_fe = NULL;
    void *parent_panel = NULL, *children_vec = NULL;
    ICoreWindow *cw = NULL;
    ICoreWindowInterop *interop = NULL;
    HWND hwnd = NULL;
    HSTRING str;
    HRESULT hr;
    struct cw_ptr_test_state cw_st = {0};
    struct cw_ptr_handler h_press = { { &cw_ptr_handler_vtbl }, 1, 0, &cw_st };
    struct cw_ptr_handler h_move  = { { &cw_ptr_handler_vtbl }, 1, 1, &cw_st };
    struct cw_ptr_handler h_rel   = { { &cw_ptr_handler_vtbl }, 1, 2, &cw_st };
    struct cw_ptr_handler h_wheel = { { &cw_ptr_handler_vtbl }, 1, 3, &cw_st };
    struct elem_ptr_handler h_child = { elem_ptr_vtbl, 1 };
    struct elem_ptr_handler h_parent = { elem_ptr_vtbl, 1 };
    EventRegistrationToken tok_press = {0}, tok_move = {0}, tok_rel = {0}, tok_wheel = {0};
    EventRegistrationToken tok_child = {0}, tok_parent = {0};
    struct test_thickness margin = { 20.0, 20.0, 0.0, 0.0 };

    init_user32_procs();

    WindowsCreateString( L"Windows.UI.Xaml.Window", 22, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&win_af );
    WindowsDeleteString( str );
    ok( hr == S_OK, "Window factory hr %#lx\n", hr );
    if (FAILED(hr)) return;

    hr = IActivationFactory_QueryInterface( win_af, &IID_IWindowStatics_test, (void **)&win_statics );
    IActivationFactory_Release( win_af );
    ok( hr == S_OK, "QI IWindowStatics hr %#lx\n", hr );
    if (FAILED(hr)) return;

    hr = ((HRESULT (WINAPI *)(void *, void **))(*(void ***)win_statics)[6])( win_statics, (void **)&win );
    IInspectable_Release( win_statics );
    ok( hr == S_OK && win != NULL, "get_Current hr %#lx\n", hr );
    if (FAILED(hr) || !win) return;
    check_interface( win, &IID_IWindow_test, FALSE );

    /* Activate creates/attaches the CoreWindow HWND and subclasses it */
    hr = ((HRESULT (WINAPI *)(void *))(*(void ***)win)[20])( win );
    ok( hr == S_OK, "Window.Activate hr %#lx\n", hr );

    hr = ((HRESULT (WINAPI *)(void *, ICoreWindow **))(*(void ***)win)[10])( win, &cw );
    ok( hr == S_OK && cw != NULL, "Window.get_CoreWindow hr %#lx\n", hr );
    if (!cw) { IInspectable_Release( win ); return; }

    hr = ICoreWindow_QueryInterface( cw, &IID_ICoreWindowInterop, (void **)&interop );
    ok( hr == S_OK && interop != NULL, "QI ICoreWindowInterop hr %#lx\n", hr );
    if (SUCCEEDED(hr) && interop)
    {
        hr = interop->lpVtbl->GetWindowHandle( interop, &hwnd );
        ok( hr == S_OK && pIsWindow( hwnd ), "GetWindowHandle hr %#lx hwnd %p\n", hr, hwnd );
        interop->lpVtbl->Release( interop );
    }
    if (!pIsWindow( hwnd )) { ICoreWindow_Release( cw ); IInspectable_Release( win ); return; }

    /* 1. CoreWindow pointer event registry + WM_* bridge */
    hr = ICoreWindow_add_PointerPressed( cw, &h_press.iface, &tok_press );
    ok( hr == S_OK && tok_press.value != 0, "add_PointerPressed hr %#lx tok %I64d\n", hr, tok_press.value );
    hr = ICoreWindow_add_PointerMoved( cw, &h_move.iface, &tok_move );
    ok( hr == S_OK && tok_move.value != 0, "add_PointerMoved hr %#lx\n", hr );
    hr = ICoreWindow_add_PointerReleased( cw, &h_rel.iface, &tok_rel );
    ok( hr == S_OK && tok_rel.value != 0, "add_PointerReleased hr %#lx\n", hr );
    hr = ICoreWindow_add_PointerWheelChanged( cw, &h_wheel.iface, &tok_wheel );
    ok( hr == S_OK && tok_wheel.value != 0, "add_PointerWheelChanged hr %#lx\n", hr );

    pPostMessageW( hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM( 45, 65 ) );
    pump_messages( hwnd );
    ok( cw_st.pressed_cnt == 1, "expected 1 PointerPressed, got %d\n", cw_st.pressed_cnt );
    ok( cw_st.press_x == 45.0f && cw_st.press_y == 65.0f, "got coords (%.1f, %.1f)\n", cw_st.press_x, cw_st.press_y );
    ok( cw_st.press_ptr_id == 1, "expected PointerId 1, got %u\n", cw_st.press_ptr_id );
    ok( cw_st.press_left == TRUE, "expected IsLeftButtonPressed TRUE\n" );
    ok( cw_st.press_kind == PointerUpdateKind_LeftButtonPressed, "got update kind %d\n", cw_st.press_kind );
    ok( cw_st.press_pressure == 0.5f, "expected pressure 0.5, got %.2f\n", cw_st.press_pressure );
    ok( cw_st.press_contact.Width == 1.0f && cw_st.press_contact.Height == 1.0f,
        "expected 1x1 contact rect, got %.1fx%.1f\n", cw_st.press_contact.Width, cw_st.press_contact.Height );

    pPostMessageW( hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM( 55, 75 ) );
    pump_messages( hwnd );
    ok( cw_st.moved_cnt >= 1 && cw_st.saw_move_55_75, "expected PointerMoved at (55,75), cnt=%d saw=%d\n",
        cw_st.moved_cnt, cw_st.saw_move_55_75 );

    pPostMessageW( hwnd, WM_LBUTTONUP, 0, MAKELPARAM( 55, 75 ) );
    pump_messages( hwnd );
    ok( cw_st.released_cnt == 1, "expected 1 PointerReleased, got %d\n", cw_st.released_cnt );
    ok( cw_st.rel_kind == PointerUpdateKind_LeftButtonReleased, "got release kind %d\n", cw_st.rel_kind );

    {
        POINT spt = { 55, 75 };
        pClientToScreen( hwnd, &spt );
        pPostMessageW( hwnd, WM_MOUSEWHEEL, MAKEWPARAM( 0, 120 ), MAKELPARAM( spt.x, spt.y ) );
        pump_messages( hwnd );
        ok( cw_st.wheel_cnt == 1, "expected 1 PointerWheelChanged, got %d\n", cw_st.wheel_cnt );
        ok( cw_st.last_wheel == 120, "expected wheel delta 120, got %d\n", cw_st.last_wheel );
    }

    ICoreWindow_remove_PointerPressed( cw, tok_press );
    ICoreWindow_remove_PointerMoved( cw, tok_move );
    ICoreWindow_remove_PointerReleased( cw, tok_rel );
    ICoreWindow_remove_PointerWheelChanged( cw, tok_wheel );
    pPostMessageW( hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM( 10, 10 ) );
    pPostMessageW( hwnd, WM_LBUTTONUP, 0, MAKELPARAM( 10, 10 ) );
    pump_messages( hwnd );
    ok( cw_st.pressed_cnt == 1, "removed PointerPressed handler still fired (%d)\n", cw_st.pressed_cnt );

    /* 2. Element tree hit-testing & bubbling */
    WindowsCreateString( L"Windows.UI.Xaml.Controls.Grid", 29, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&grid_af );
    WindowsDeleteString( str );
    ok( hr == S_OK, "Grid factory hr %#lx\n", hr );
    hr = IActivationFactory_ActivateInstance( grid_af, &parent_obj );
    IActivationFactory_Release( grid_af );
    ok( hr == S_OK && parent_obj != NULL, "Grid ActivateInstance hr %#lx\n", hr );

    WindowsCreateString( L"Windows.UI.Xaml.Controls.StackPanel", 35, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&sp_af );
    WindowsDeleteString( str );
    ok( hr == S_OK, "StackPanel factory hr %#lx\n", hr );
    hr = IActivationFactory_ActivateInstance( sp_af, &child_obj );
    IActivationFactory_Release( sp_af );
    ok( hr == S_OK && child_obj != NULL, "StackPanel ActivateInstance hr %#lx\n", hr );

    IInspectable_QueryInterface( parent_obj, &IID_IFrameworkElement_test, &parent_fe );
    IInspectable_QueryInterface( child_obj, &IID_IFrameworkElement_test, &child_fe );
    ((HRESULT (WINAPI *)(void *, double))(*(void ***)parent_fe)[16])( parent_fe, 200.0 );
    ((HRESULT (WINAPI *)(void *, double))(*(void ***)parent_fe)[18])( parent_fe, 200.0 );
    ((HRESULT (WINAPI *)(void *, double))(*(void ***)child_fe)[16])( child_fe, 80.0 );
    ((HRESULT (WINAPI *)(void *, double))(*(void ***)child_fe)[18])( child_fe, 80.0 );
    ((HRESULT (WINAPI *)(void *, struct test_thickness *))(*(void ***)child_fe)[32])( child_fe, &margin );

    IInspectable_QueryInterface( parent_obj, &IID_IPanel_test, &parent_panel );
    ((HRESULT (WINAPI *)(void *, void **))(*(void ***)parent_panel)[6])( parent_panel, &children_vec );
    ((HRESULT (WINAPI *)(void *, void *))(*(void ***)children_vec)[13])( children_vec, child_obj );

    IInspectable_QueryInterface( parent_obj, &IID_IUIElement_test, &parent_ui );
    IInspectable_QueryInterface( child_obj, &IID_IUIElement_test, &child_ui );
    h_child.rel_elem = child_obj;
    ((HRESULT (WINAPI *)(void *, void *, EventRegistrationToken *))(*(void ***)child_ui)[57])( child_ui, &h_child, &tok_child );
    ((HRESULT (WINAPI *)(void *, void *, EventRegistrationToken *))(*(void ***)parent_ui)[57])( parent_ui, &h_parent, &tok_parent );

    ((HRESULT (WINAPI *)(void *, void *))(*(void ***)win)[9])( win, parent_obj );
    pump_messages( hwnd );

    /* Inside child rect [20,20 .. 100,100]: child fires and bubbles to parent */
    pPostMessageW( hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM( 50, 50 ) );
    pPostMessageW( hwnd, WM_LBUTTONUP, 0, MAKELPARAM( 50, 50 ) );
    pump_messages( hwnd );
    ok( h_child.fired_cnt == 1, "expected child PointerPressed count 1, got %d\n", h_child.fired_cnt );
    ok( h_parent.fired_cnt == 1, "expected parent PointerPressed (bubbled) count 1, got %d\n", h_parent.fired_cnt );
    ok( h_child.last_abs_x == 50.0f && h_child.last_abs_y == 50.0f,
        "child abs coords (%.1f, %.1f)\n", h_child.last_abs_x, h_child.last_abs_y );
    ok( h_child.last_rel_x == 30.0f && h_child.last_rel_y == 30.0f,
        "child rel coords (%.1f, %.1f), expected (30, 30)\n", h_child.last_rel_x, h_child.last_rel_y );

    /* Outside parent rect [0,0 .. 200,200]: neither fires */
    pPostMessageW( hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM( 300, 300 ) );
    pPostMessageW( hwnd, WM_LBUTTONUP, 0, MAKELPARAM( 300, 300 ) );
    pump_messages( hwnd );
    ok( h_child.fired_cnt == 1, "outside click fired child (%d)\n", h_child.fired_cnt );
    ok( h_parent.fired_cnt == 1, "outside click fired parent (%d)\n", h_parent.fired_cnt );

    /* Handled=TRUE on child stops bubbling to parent */
    h_child.set_handled = TRUE;
    pPostMessageW( hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM( 50, 50 ) );
    pPostMessageW( hwnd, WM_LBUTTONUP, 0, MAKELPARAM( 50, 50 ) );
    pump_messages( hwnd );
    ok( h_child.fired_cnt == 2, "expected child count 2, got %d\n", h_child.fired_cnt );
    ok( h_parent.fired_cnt == 1, "expected parent count still 1 when Handled=TRUE, got %d\n", h_parent.fired_cnt );

    ((HRESULT (WINAPI *)(void *, EventRegistrationToken))(*(void ***)child_ui)[58])( child_ui, tok_child );
    ((HRESULT (WINAPI *)(void *, EventRegistrationToken))(*(void ***)parent_ui)[58])( parent_ui, tok_parent );
    ((HRESULT (WINAPI *)(void *, void *))(*(void ***)win)[9])( win, NULL );
    IUnknown_Release( (IUnknown *)child_ui );
    IUnknown_Release( (IUnknown *)parent_ui );
    IUnknown_Release( (IUnknown *)children_vec );
    IUnknown_Release( (IUnknown *)parent_panel );
    IUnknown_Release( (IUnknown *)child_fe );
    IUnknown_Release( (IUnknown *)parent_fe );
    IInspectable_Release( child_obj );
    IInspectable_Release( parent_obj );
    ICoreWindow_Release( cw );
    IInspectable_Release( win );
}

static const GUID IID_INotifyPropertyChanged_test =
    { 0xcf75d69c, 0xf2f4, 0x4fb6, { 0xa7, 0xc3, 0x5f, 0x20, 0x42, 0x0f, 0x5d, 0x42 } };
static const GUID IID_ITextBlock_test =
    { 0xae2d9271, 0x3b4a, 0x45fc, { 0x84, 0x68, 0xf7, 0x94, 0x95, 0x48, 0xf4, 0xd5 } };

struct fake_calc_vm
{
    const void **vtbl_insp;
    const void **vtbl_npc;
    LONG ref;
    const WCHAR *display_val;
    void *pc_handler;
    INT64 pc_token;
    int add_pc_cnt;
    int rem_pc_cnt;
};

static inline struct fake_calc_vm *vm_from_npc( void *iface )
{
    return CONTAINING_RECORD( iface, struct fake_calc_vm, vtbl_npc );
}

static HRESULT WINAPI fake_vm_QI( void *iface, REFIID iid, void **out )
{
    struct fake_calc_vm *vm = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        InterlockedIncrement( &vm->ref );
        *out = &vm->vtbl_insp;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_INotifyPropertyChanged_test ))
    {
        InterlockedIncrement( &vm->ref );
        *out = &vm->vtbl_npc;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI fake_vm_AddRef( void *iface )
{
    struct fake_calc_vm *vm = iface;
    return InterlockedIncrement( &vm->ref );
}
static ULONG WINAPI fake_vm_Release( void *iface )
{
    struct fake_calc_vm *vm = iface;
    return InterlockedDecrement( &vm->ref );
}
static HRESULT WINAPI fake_vm_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI fake_vm_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"CalculatorApp.ViewModel.StandardCalculatorViewModel";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI fake_vm_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI fake_vm_slot6_noop( void *iface ) { return S_OK; }
static HRESULT WINAPI fake_vm_get_DisplayValue( void *iface, HSTRING *out )
{
    struct fake_calc_vm *vm = iface;
    const WCHAR *s = vm->display_val ? vm->display_val : L"";
    return WindowsCreateString( s, wcslen( s ), out );
}

static HRESULT WINAPI fake_vm_npc_QI( void *iface, REFIID iid, void **out )
{
    return fake_vm_QI( vm_from_npc( iface ), iid, out );
}
static ULONG WINAPI fake_vm_npc_AddRef( void *iface )
{
    return fake_vm_AddRef( vm_from_npc( iface ) );
}
static ULONG WINAPI fake_vm_npc_Release( void *iface )
{
    return fake_vm_Release( vm_from_npc( iface ) );
}
static HRESULT WINAPI fake_vm_npc_add_PropertyChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct fake_calc_vm *vm = vm_from_npc( iface );
    vm->add_pc_cnt++;
    vm->pc_handler = handler;
    vm->pc_token = 0x7700 + vm->add_pc_cnt;
    if (tok) tok->value = vm->pc_token;
    return S_OK;
}
static HRESULT WINAPI fake_vm_npc_remove_PropertyChanged( void *iface, EventRegistrationToken tok )
{
    struct fake_calc_vm *vm = vm_from_npc( iface );
    if (tok.value == vm->pc_token)
    {
        vm->rem_pc_cnt++;
        vm->pc_handler = NULL;
    }
    return S_OK;
}

static const void *fake_vm_insp_vtbl[] =
{
    fake_vm_QI,
    fake_vm_AddRef,
    fake_vm_Release,
    fake_vm_GetIids,
    fake_vm_GetRuntimeClassName,
    fake_vm_GetTrustLevel,
    fake_vm_slot6_noop,
    fake_vm_get_DisplayValue, /* slot 7: get_DisplayValue */
};

static const void *fake_vm_npc_vtbl[] =
{
    fake_vm_npc_QI,
    fake_vm_npc_AddRef,
    fake_vm_npc_Release,
    fake_vm_GetIids,
    fake_vm_GetRuntimeClassName,
    fake_vm_GetTrustLevel,
    fake_vm_npc_add_PropertyChanged,    /* slot 6: add_PropertyChanged */
    fake_vm_npc_remove_PropertyChanged, /* slot 7: remove_PropertyChanged */
};

static void test_VmDataContextBinding( void )
{
    IActivationFactory *res_af = NULL;
    IInspectable *res_obj = NULL;
    void *res_fe = NULL, *res_tb = NULL;
    HSTRING str = NULL, htxt = NULL;
    const WCHAR *raw = NULL;
    HRESULT hr;
    struct fake_calc_vm vm1 = { fake_vm_insp_vtbl, fake_vm_npc_vtbl, 1, L"42" };
    struct fake_calc_vm vm2 = { fake_vm_insp_vtbl, fake_vm_npc_vtbl, 1, L"2718" };

    WindowsCreateString( L"Windows.UI.Xaml.Controls.TextBlock", 34, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&res_af );
    WindowsDeleteString( str );
    ok( hr == S_OK && res_af != NULL, "TextBlock factory hr %#lx\n", hr );
    if (FAILED(hr) || !res_af) return;

    hr = IActivationFactory_ActivateInstance( res_af, &res_obj );
    IActivationFactory_Release( res_af );
    ok( hr == S_OK && res_obj != NULL, "TextBlock ActivateInstance hr %#lx\n", hr );
    if (FAILED(hr) || !res_obj) return;

    hr = IInspectable_QueryInterface( res_obj, &IID_IFrameworkElement_test, &res_fe );
    ok( hr == S_OK && res_fe != NULL, "QI IFrameworkElement hr %#lx\n", hr );
    hr = IInspectable_QueryInterface( res_obj, &IID_ITextBlock_test, &res_tb );
    ok( hr == S_OK && res_tb != NULL, "QI ITextBlock hr %#lx\n", hr );

    /* Name the element "Results" so it acts as a CalculationResult display sink */
    WindowsCreateString( L"Results", 7, &str );
    hr = ((HRESULT (WINAPI *)(void *, HSTRING))(*(void ***)res_fe)[34])( res_fe, str );
    WindowsDeleteString( str );
    ok( hr == S_OK, "put_Name(Results) hr %#lx\n", hr );

    /* 1. Setting DataContext to vm1 subscribes to INotifyPropertyChanged and syncs DisplayValue ("42") */
    hr = ((HRESULT (WINAPI *)(void *, void *))(*(void ***)res_fe)[37])( res_fe, &vm1.vtbl_insp );
    ok( hr == S_OK, "put_DataContext(vm1) hr %#lx\n", hr );
    ok( vm1.add_pc_cnt == 1 && vm1.pc_handler != NULL, "expected add_PropertyChanged count 1, got %d\n", vm1.add_pc_cnt );

    hr = ((HRESULT (WINAPI *)(void *, HSTRING *))(*(void ***)res_tb)[26])( res_tb, &htxt );
    raw = htxt ? WindowsGetStringRawBuffer( htxt, NULL ) : NULL;
    ok( hr == S_OK && raw && !wcscmp( raw, L"42" ), "expected CalculationResult text '42', got %s\n", wine_dbgstr_w( raw ) );
    if (htxt) { WindowsDeleteString( htxt ); htxt = NULL; }

    /* 2. Firing PropertyChanged on vm1 updates CalculationResult display text ("314") */
    vm1.display_val = L"314";
    if (vm1.pc_handler)
    {
        HRESULT (WINAPI *invoke)(void *, void *, void *) = (void *)(*(void ***)vm1.pc_handler)[3];
        invoke( vm1.pc_handler, &vm1.vtbl_insp, NULL );
    }
    hr = ((HRESULT (WINAPI *)(void *, HSTRING *))(*(void ***)res_tb)[26])( res_tb, &htxt );
    raw = htxt ? WindowsGetStringRawBuffer( htxt, NULL ) : NULL;
    ok( hr == S_OK && raw && !wcscmp( raw, L"314" ), "expected updated CalculationResult text '314', got %s\n", wine_dbgstr_w( raw ) );
    if (htxt) { WindowsDeleteString( htxt ); htxt = NULL; }

    /* 3. Switching DataContext to vm2 unsubscribes/releases vm1 and syncs vm2 ("2718") */
    hr = ((HRESULT (WINAPI *)(void *, void *))(*(void ***)res_fe)[37])( res_fe, &vm2.vtbl_insp );
    ok( hr == S_OK, "put_DataContext(vm2) hr %#lx\n", hr );
    ok( vm1.rem_pc_cnt == 1, "expected vm1 remove_PropertyChanged count 1, got %d\n", vm1.rem_pc_cnt );
    ok( vm1.ref == 1, "expected vm1 refcount restored to 1 after teardown, got %ld\n", vm1.ref );

    hr = ((HRESULT (WINAPI *)(void *, HSTRING *))(*(void ***)res_tb)[26])( res_tb, &htxt );
    raw = htxt ? WindowsGetStringRawBuffer( htxt, NULL ) : NULL;
    ok( hr == S_OK && raw && !wcscmp( raw, L"2718" ), "expected CalculationResult text '2718', got %s\n", wine_dbgstr_w( raw ) );
    if (htxt) { WindowsDeleteString( htxt ); htxt = NULL; }

    /* 4. Clearing DataContext to NULL unsubscribes and releases vm2 */
    hr = ((HRESULT (WINAPI *)(void *, void *))(*(void ***)res_fe)[37])( res_fe, NULL );
    ok( hr == S_OK, "put_DataContext(NULL) hr %#lx\n", hr );
    ok( vm2.rem_pc_cnt == 1, "expected vm2 remove_PropertyChanged count 1, got %d\n", vm2.rem_pc_cnt );
    ok( vm2.ref == 1, "expected vm2 refcount restored to 1 after teardown, got %ld\n", vm2.ref );

    IUnknown_Release( (IUnknown *)res_tb );
    IUnknown_Release( (IUnknown *)res_fe );
    IInspectable_Release( res_obj );
}

START_TEST(xaml)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_ColorHelper();
    test_Application();
    test_SwapChainPanel();
    test_View_lifecycle();
    test_PointerBridge();
    test_VmDataContextBinding();

    RoUninitialize();
}
