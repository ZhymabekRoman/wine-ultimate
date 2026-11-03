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
#include "windows.foundation.h"
#define WIDL_using_Windows_UI
#include "windows.ui.h"
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"
#define WIDL_using_Windows_ApplicationModel_Core
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

struct fake_window
{
    ICoreWindow ICoreWindow_iface;
    LONG ref;
};

static inline struct fake_window *impl_from_fake( ICoreWindow *iface )
{
    return CONTAINING_RECORD( iface, struct fake_window, ICoreWindow_iface );
}

static HRESULT WINAPI fake_QueryInterface( ICoreWindow *iface, REFIID iid, void **out )
{
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

static void test_View_lifecycle( void )
{
    IFrameworkViewSource *source = get_view_source();

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

START_TEST(xaml)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_ColorHelper();
    test_Application();
    test_SwapChainPanel();
    test_View_lifecycle();

    RoUninitialize();
}
