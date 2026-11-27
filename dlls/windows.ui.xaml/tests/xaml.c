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

struct test_thickness;

struct test_ptr_args_vtbl
{
    void *pad0_8[9];
    HRESULT (WINAPI *put_Handled)(void *, boolean);
    HRESULT (WINAPI *GetCurrentPoint)(void *, void *, IPointerPoint **);
};
struct test_ptr_args_iface { const struct test_ptr_args_vtbl *lpVtbl; };

struct test_window_statics_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_Current)(void *, void **);
};
struct test_window_statics_iface { const struct test_window_statics_vtbl *lpVtbl; };

struct test_window_vtbl
{
    void *pad0_8[9];
    HRESULT (WINAPI *put_Content)(void *, void *);
    HRESULT (WINAPI *get_CoreWindow)(void *, ICoreWindow **);
    void *pad11_19[9];
    HRESULT (WINAPI *Activate)(void *);
};
struct test_window_iface { const struct test_window_vtbl *lpVtbl; };

struct test_fe_vtbl
{
    void *pad0_15[16];
    HRESULT (WINAPI *put_Width)(void *, double);
    void *pad17;
    HRESULT (WINAPI *put_Height)(void *, double);
    void *pad19_27[9];
    HRESULT (WINAPI *put_HorizontalAlignment)(void *, int);
    void *pad29;
    HRESULT (WINAPI *put_VerticalAlignment)(void *, int);
    void *pad31;
    HRESULT (WINAPI *put_Margin)(void *, struct test_thickness *);
    void *pad33;
    HRESULT (WINAPI *put_Name)(void *, HSTRING);
    void *pad35;
    HRESULT (WINAPI *get_DataContext)(void *, void **);
    HRESULT (WINAPI *put_DataContext)(void *, void *);
    void *pad38_51[14];
    HRESULT (WINAPI *SetBinding)(void *, void *, void *);
};
struct test_fe_iface { const struct test_fe_vtbl *lpVtbl; };

struct test_panel_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_Children)(void *, void **);
};
struct test_panel_iface { const struct test_panel_vtbl *lpVtbl; };

struct test_vec_vtbl
{
    void *pad0_12[13];
    HRESULT (WINAPI *Append)(void *, void *);
};
struct test_vec_iface { const struct test_vec_vtbl *lpVtbl; };

struct test_ui_vtbl
{
    void *pad0_56[57];
    HRESULT (WINAPI *add_PointerPressed)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_PointerPressed)(void *, EventRegistrationToken);
};
struct test_ui_iface { const struct test_ui_vtbl *lpVtbl; };

struct test_tb_vtbl
{
    void *pad0_25[26];
    HRESULT (WINAPI *get_Text)(void *, HSTRING *);
};
struct test_tb_iface { const struct test_tb_vtbl *lpVtbl; };

struct test_dp_statics_vtbl
{
    void *pad0_6[7];
    HRESULT (WINAPI *Register)(void *, HSTRING, void *, void *, void *, IUnknown **);
};
struct test_dp_statics_iface { const struct test_dp_statics_vtbl *lpVtbl; };

struct test_do_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *GetValue)(void *, void *, IInspectable **);
    HRESULT (WINAPI *SetValue)(void *, void *, IInspectable *);
};
struct test_do_iface { const struct test_do_vtbl *lpVtbl; };

struct test_xr_statics_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *Load)(void *, HSTRING, IInspectable **);
};
struct test_xr_statics_iface { const struct test_xr_statics_vtbl *lpVtbl; };

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
        const struct test_ptr_args_iface *pa = (const struct test_ptr_args_iface *)pargs;
        IPointerPoint *pt = NULL;
        if (pa->lpVtbl->GetCurrentPoint && SUCCEEDED( pa->lpVtbl->GetCurrentPoint( pargs, NULL, &pt ) ) && pt)
        {
            Point pos = {0};
            IPointerPoint_get_Position( pt, &pos );
            h->last_abs_x = pos.X;
            h->last_abs_y = pos.Y;
            IPointerPoint_Release( pt );
        }
        if (h->rel_elem && pa->lpVtbl->GetCurrentPoint && SUCCEEDED( pa->lpVtbl->GetCurrentPoint( pargs, h->rel_elem, &pt ) ) && pt)
        {
            Point pos = {0};
            IPointerPoint_get_Position( pt, &pos );
            h->last_rel_x = pos.X;
            h->last_rel_y = pos.Y;
            IPointerPoint_Release( pt );
        }
        if (h->set_handled && pa->lpVtbl->put_Handled)
            pa->lpVtbl->put_Handled( pargs, TRUE );
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

    hr = ((const struct test_window_statics_iface *)win_statics)->lpVtbl->get_Current( win_statics, (void **)&win );
    IInspectable_Release( win_statics );
    ok( hr == S_OK && win != NULL, "get_Current hr %#lx\n", hr );
    if (FAILED(hr) || !win) return;
    check_interface( win, &IID_IWindow_test, FALSE );

    /* Activate creates/attaches the CoreWindow HWND and subclasses it */
    hr = ((const struct test_window_iface *)win)->lpVtbl->Activate( win );
    ok( hr == S_OK, "Window.Activate hr %#lx\n", hr );

    hr = ((const struct test_window_iface *)win)->lpVtbl->get_CoreWindow( win, &cw );
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
    ((const struct test_fe_iface *)parent_fe)->lpVtbl->put_Width( parent_fe, 200.0 );
    ((const struct test_fe_iface *)parent_fe)->lpVtbl->put_Height( parent_fe, 200.0 );
    ((const struct test_fe_iface *)parent_fe)->lpVtbl->put_HorizontalAlignment( parent_fe, 0 );
    ((const struct test_fe_iface *)parent_fe)->lpVtbl->put_VerticalAlignment( parent_fe, 0 );
    ((const struct test_fe_iface *)child_fe)->lpVtbl->put_Width( child_fe, 80.0 );
    ((const struct test_fe_iface *)child_fe)->lpVtbl->put_Height( child_fe, 80.0 );
    ((const struct test_fe_iface *)child_fe)->lpVtbl->put_HorizontalAlignment( child_fe, 0 );
    ((const struct test_fe_iface *)child_fe)->lpVtbl->put_VerticalAlignment( child_fe, 0 );
    ((const struct test_fe_iface *)child_fe)->lpVtbl->put_Margin( child_fe, &margin );

    IInspectable_QueryInterface( parent_obj, &IID_IPanel_test, &parent_panel );
    ((const struct test_panel_iface *)parent_panel)->lpVtbl->get_Children( parent_panel, &children_vec );
    ((const struct test_vec_iface *)children_vec)->lpVtbl->Append( children_vec, child_obj );

    IInspectable_QueryInterface( parent_obj, &IID_IUIElement_test, &parent_ui );
    IInspectable_QueryInterface( child_obj, &IID_IUIElement_test, &child_ui );
    h_child.rel_elem = child_obj;
    ((const struct test_ui_iface *)child_ui)->lpVtbl->add_PointerPressed( child_ui, &h_child, &tok_child );
    ((const struct test_ui_iface *)parent_ui)->lpVtbl->add_PointerPressed( parent_ui, &h_parent, &tok_parent );

    ((const struct test_window_iface *)win)->lpVtbl->put_Content( win, parent_obj );
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

    ((const struct test_ui_iface *)child_ui)->lpVtbl->remove_PointerPressed( child_ui, tok_child );
    ((const struct test_ui_iface *)parent_ui)->lpVtbl->remove_PointerPressed( parent_ui, tok_parent );
    ((const struct test_window_iface *)win)->lpVtbl->put_Content( win, NULL );
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

static const GUID IID_ITextBlock_test =
    { 0xae2d9271, 0x3b4a, 0x45fc, { 0x84, 0x68, 0xf7, 0x94, 0x95, 0x48, 0xf4, 0xd5 } };
static const GUID IID_IPropertyValue_test =
    { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };

struct fake_string_pv
{
    const void **vtbl;
    LONG ref;
    const WCHAR *val;
};

static HRESULT WINAPI fake_pv_QI( void *iface, REFIID iid, void **out )
{
    struct fake_string_pv *pv = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertyValue_test ))
    {
        InterlockedIncrement( &pv->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI fake_pv_AddRef( void *iface )
{
    struct fake_string_pv *pv = iface;
    return InterlockedIncrement( &pv->ref );
}
static ULONG WINAPI fake_pv_Release( void *iface )
{
    struct fake_string_pv *pv = iface;
    return InterlockedDecrement( &pv->ref );
}
static HRESULT WINAPI fake_pv_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI fake_pv_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.IPropertyValue";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI fake_pv_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI fake_pv_get_Type( void *iface, int *type )
{
    if (type) *type = 12; /* PropertyType_String */
    return S_OK;
}
static HRESULT WINAPI fake_pv_get_IsNumericScalar( void *iface, boolean *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI fake_pv_noop( void *iface ) { return E_NOTIMPL; }
static HRESULT WINAPI fake_pv_GetString( void *iface, HSTRING *out )
{
    struct fake_string_pv *pv = iface;
    const WCHAR *s = pv->val ? pv->val : L"";
    return WindowsCreateString( s, wcslen( s ), out );
}

static const void *fake_pv_vtbl[] =
{
    fake_pv_QI,
    fake_pv_AddRef,
    fake_pv_Release,
    fake_pv_GetIids,
    fake_pv_GetRuntimeClassName,
    fake_pv_GetTrustLevel,
    fake_pv_get_Type,          /* 6 */
    fake_pv_get_IsNumericScalar, /* 7 */
    fake_pv_noop, fake_pv_noop, fake_pv_noop, fake_pv_noop, fake_pv_noop, /* 8..12 */
    fake_pv_noop, fake_pv_noop, fake_pv_noop, fake_pv_noop, fake_pv_noop, /* 13..17 */
    fake_pv_noop,              /* 18: GetBoolean */
    fake_pv_GetString,         /* 19: GetString */
};

static void test_VmDataContextBinding( void )
{
    static const GUID IID_IDependencyPropertyStatics_test =
        { 0x49e5f28f, 0x8259, 0x4d5c, { 0xaa, 0xe0, 0x83, 0xd5, 0x6d, 0xbb, 0x68, 0xd9 } };
    IActivationFactory *tb_af = NULL, *dp_af = NULL, *bind_af = NULL;
    IInspectable *tb_obj = NULL, *bind_obj = NULL;
    void *tb_fe = NULL, *tb_iface = NULL, *dp_statics = NULL, *dc_out = NULL;
    IUnknown *text_dp = NULL;
    HSTRING str = NULL, htxt = NULL;
    struct { HSTRING Name; int Kind; } tname = { NULL, 0 };
    const WCHAR *raw = NULL;
    HRESULT hr;
    struct fake_string_pv pv1 = { fake_pv_vtbl, 1, L"42" };
    struct fake_string_pv pv2 = { fake_pv_vtbl, 1, L"2718" };

    WindowsCreateString( L"Windows.UI.Xaml.Controls.TextBlock", 34, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&tb_af );
    WindowsDeleteString( str );
    ok( hr == S_OK && tb_af != NULL, "TextBlock factory hr %#lx\n", hr );
    if (FAILED(hr) || !tb_af) return;

    hr = IActivationFactory_ActivateInstance( tb_af, &tb_obj );
    IActivationFactory_Release( tb_af );
    ok( hr == S_OK && tb_obj != NULL, "TextBlock ActivateInstance hr %#lx\n", hr );
    if (FAILED(hr) || !tb_obj) return;

    hr = IInspectable_QueryInterface( tb_obj, &IID_IFrameworkElement_test, &tb_fe );
    ok( hr == S_OK && tb_fe != NULL, "QI IFrameworkElement hr %#lx\n", hr );
    hr = IInspectable_QueryInterface( tb_obj, &IID_ITextBlock_test, &tb_iface );
    ok( hr == S_OK && tb_iface != NULL, "QI ITextBlock hr %#lx\n", hr );

    WindowsCreateString( L"Windows.UI.Xaml.DependencyProperty", 34, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&dp_af );
    WindowsDeleteString( str );
    ok( hr == S_OK && dp_af != NULL, "DependencyProperty factory hr %#lx\n", hr );
    hr = IActivationFactory_QueryInterface( dp_af, &IID_IDependencyPropertyStatics_test, &dp_statics );
    IActivationFactory_Release( dp_af );
    ok( hr == S_OK && dp_statics != NULL, "QI IDependencyPropertyStatics hr %#lx\n", hr );

    WindowsCreateString( L"String", 6, &tname.Name );
    WindowsCreateString( L"Text", 4, &str );
    hr = ((const struct test_dp_statics_iface *)dp_statics)->lpVtbl->Register(
        dp_statics, str, &tname, &tname, NULL, &text_dp );
    WindowsDeleteString( str );
    WindowsDeleteString( tname.Name );
    ok( hr == S_OK && text_dp != NULL, "Register Text DP hr %#lx\n", hr );

    WindowsCreateString( L"Windows.UI.Xaml.Data.Binding", 28, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&bind_af );
    WindowsDeleteString( str );
    ok( hr == S_OK && bind_af != NULL, "Binding factory hr %#lx\n", hr );
    hr = IActivationFactory_ActivateInstance( bind_af, &bind_obj );
    IActivationFactory_Release( bind_af );
    ok( hr == S_OK && bind_obj != NULL, "Binding ActivateInstance hr %#lx\n", hr );

    hr = ((const struct test_fe_iface *)tb_fe)->lpVtbl->SetBinding( tb_fe, text_dp, bind_obj );
    ok( hr == S_OK, "SetBinding hr %#lx\n", hr );

    /* 1. Setting DataContext to pv1 applies binding to TextBlock ("42") */
    hr = ((const struct test_fe_iface *)tb_fe)->lpVtbl->put_DataContext( tb_fe, &pv1 );
    ok( hr == S_OK, "put_DataContext(pv1) hr %#lx\n", hr );
    hr = ((const struct test_fe_iface *)tb_fe)->lpVtbl->get_DataContext( tb_fe, &dc_out );
    ok( hr == S_OK && dc_out == &pv1, "expected DataContext == pv1, got %p\n", dc_out );
    if (dc_out) IUnknown_Release( (IUnknown *)dc_out );
    dc_out = NULL;

    hr = ((const struct test_tb_iface *)tb_iface)->lpVtbl->get_Text( tb_iface, &htxt );
    raw = htxt ? WindowsGetStringRawBuffer( htxt, NULL ) : NULL;
    ok( hr == S_OK && raw && !wcscmp( raw, L"42" ), "expected bound TextBlock text '42', got %s\n", wine_dbgstr_w( raw ) );
    if (htxt) { WindowsDeleteString( htxt ); htxt = NULL; }

    /* 2. Switching DataContext to pv2 updates bound text ("2718") and releases pv1 */
    hr = ((const struct test_fe_iface *)tb_fe)->lpVtbl->put_DataContext( tb_fe, &pv2 );
    ok( hr == S_OK, "put_DataContext(pv2) hr %#lx\n", hr );
    ok( pv1.ref == 1, "expected pv1 refcount restored to 1 after switch, got %ld\n", pv1.ref );

    hr = ((const struct test_tb_iface *)tb_iface)->lpVtbl->get_Text( tb_iface, &htxt );
    raw = htxt ? WindowsGetStringRawBuffer( htxt, NULL ) : NULL;
    ok( hr == S_OK && raw && !wcscmp( raw, L"2718" ), "expected bound TextBlock text '2718', got %s\n", wine_dbgstr_w( raw ) );
    if (htxt) { WindowsDeleteString( htxt ); htxt = NULL; }

    /* 3. Clearing DataContext to NULL returns NULL and releases pv2 once element is freed */
    hr = ((const struct test_fe_iface *)tb_fe)->lpVtbl->put_DataContext( tb_fe, NULL );
    ok( hr == S_OK, "put_DataContext(NULL) hr %#lx\n", hr );
    hr = ((const struct test_fe_iface *)tb_fe)->lpVtbl->get_DataContext( tb_fe, &dc_out );
    ok( hr == S_OK && dc_out == NULL, "expected NULL DataContext, got %p\n", dc_out );

    IInspectable_Release( bind_obj );
    IUnknown_Release( text_dp );
    IUnknown_Release( (IUnknown *)dp_statics );
    IUnknown_Release( (IUnknown *)tb_iface );
    IUnknown_Release( (IUnknown *)tb_fe );
    IInspectable_Release( tb_obj );
    ok( pv2.ref == 1, "expected pv2 refcount restored to 1 after teardown, got %ld\n", pv2.ref );
}

static void test_VectorGrowthBeyondOldCaps( void )
{
    static const GUID IID_IDependencyObject_test =
        { 0x5c526665, 0xf60e, 0x4912, { 0xaf, 0x59, 0x5f, 0xe0, 0x68, 0x0f, 0x08, 0x9d } };
    static const GUID IID_IDependencyPropertyStatics_test =
        { 0x49e5f28f, 0x8259, 0x4d5c, { 0xaa, 0xe0, 0x83, 0xd5, 0x6d, 0xbb, 0x68, 0xd9 } };
    IActivationFactory *tb_af = NULL, *dp_af = NULL;
    IInspectable *elem_obj = NULL;
    void *do_iface = NULL, *dp_statics = NULL;
    IUnknown *dps[30] = {0};
    IInspectable *vals[30] = {0};
    HSTRING str = NULL, hname = NULL;
    struct { HSTRING Name; int Kind; } tname = { NULL, 0 };
    HRESULT hr;
    int i;

    WindowsCreateString( L"Windows.UI.Xaml.Controls.TextBlock", 34, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&tb_af );
    WindowsDeleteString( str );
    ok( hr == S_OK && tb_af != NULL, "TextBlock factory hr %#lx\n", hr );
    if (FAILED(hr) || !tb_af) return;

    hr = IActivationFactory_ActivateInstance( tb_af, &elem_obj );
    IActivationFactory_Release( tb_af );
    ok( hr == S_OK && elem_obj != NULL, "TextBlock ActivateInstance hr %#lx\n", hr );
    if (FAILED(hr) || !elem_obj) return;

    hr = IInspectable_QueryInterface( elem_obj, &IID_IDependencyObject_test, &do_iface );
    ok( hr == S_OK && do_iface != NULL, "QI IDependencyObject hr %#lx\n", hr );

    WindowsCreateString( L"Windows.UI.Xaml.DependencyProperty", 34, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&dp_af );
    WindowsDeleteString( str );
    ok( hr == S_OK && dp_af != NULL, "DependencyProperty factory hr %#lx\n", hr );
    hr = IActivationFactory_QueryInterface( dp_af, &IID_IDependencyPropertyStatics_test, &dp_statics );
    IActivationFactory_Release( dp_af );
    ok( hr == S_OK && dp_statics != NULL, "QI IDependencyPropertyStatics hr %#lx\n", hr );

    WindowsCreateString( L"Int32", 5, &tname.Name );

    /* Register and SetValue 30 distinct DependencyProperties (exceeding old dp_store[24] cap) */
    for (i = 0; i < 30; i++)
    {
        WCHAR prop_name[32];
        swprintf( prop_name, ARRAY_SIZE(prop_name), L"TestDP_%d", i );
        WindowsCreateString( prop_name, wcslen( prop_name ), &hname );
        /* Slot 7 on IDependencyPropertyStatics: Register(HSTRING, TypeName, TypeName, IPropertyMetadata*, IDependencyProperty**) */
        hr = ((const struct test_dp_statics_iface *)dp_statics)->lpVtbl->Register(
            dp_statics, hname, &tname, &tname, NULL, &dps[i] );
        WindowsDeleteString( hname );
        ok( hr == S_OK && dps[i] != NULL, "Register DP[%d] hr %#lx\n", i, hr );

        /* Use a distinct TextBlock instance as the boxed value for each DP */
        WindowsCreateString( L"Windows.UI.Xaml.Controls.TextBlock", 34, &str );
        hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&tb_af );
        WindowsDeleteString( str );
        if (SUCCEEDED(hr) && tb_af)
        {
            IActivationFactory_ActivateInstance( tb_af, &vals[i] );
            IActivationFactory_Release( tb_af );
        }
        /* Slot 7 on IDependencyObject: SetValue(IDependencyProperty*, IInspectable*) */
        hr = ((const struct test_do_iface *)do_iface)->lpVtbl->SetValue( do_iface, dps[i], vals[i] );
        ok( hr == S_OK, "SetValue DP[%d] hr %#lx\n", i, hr );
    }

    /* Round-trip GetValue for all 30 DependencyProperties */
    for (i = 0; i < 30; i++)
    {
        IInspectable *got = NULL;
        /* Slot 6 on IDependencyObject: GetValue(IDependencyProperty*, IInspectable**) */
        hr = ((const struct test_do_iface *)do_iface)->lpVtbl->GetValue( do_iface, dps[i], &got );
        ok( hr == S_OK && got == vals[i], "GetValue DP[%d] expected %p, got %p (hr %#lx)\n", i, vals[i], got, hr );
        if (got) IInspectable_Release( got );
        if (vals[i]) IInspectable_Release( vals[i] );
        if (dps[i]) IUnknown_Release( dps[i] );
    }

    WindowsDeleteString( tname.Name );
    IUnknown_Release( (IUnknown *)dp_statics );
    IUnknown_Release( (IUnknown *)do_iface );
    IInspectable_Release( elem_obj );
}

static void test_TruncatedXbfBounds( void )
{
    static const GUID IID_IXamlReaderStatics_test =
        { 0x9891c6bd, 0x534f, 0x4955, { 0xb8, 0x5a, 0x8a, 0x8d, 0xc0, 0xdc, 0xa6, 0x02 } };
    IActivationFactory *xr_af = NULL;
    void *xr_statics = NULL;
    HSTRING str = NULL;
    HRESULT hr;
    BOOL all_ok = TRUE;
    BYTE blob[160];
    unsigned int tc;

    WindowsCreateString( L"Windows.UI.Xaml.Markup.XamlReader", 33, &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&xr_af );
    WindowsDeleteString( str );
    if (FAILED(hr) || !xr_af)
    {
        ok( FALSE, "XamlReader factory hr %#lx\n", hr );
        return;
    }
    hr = IActivationFactory_QueryInterface( xr_af, &IID_IXamlReaderStatics_test, &xr_statics );
    IActivationFactory_Release( xr_af );
    if (FAILED(hr) || !xr_statics)
    {
        ok( FALSE, "QI IXamlReaderStatics hr %#lx\n", hr );
        return;
    }

    for (tc = 0; tc < 4; tc++)
    {
        IInspectable *root = NULL;
        DWORD meta_sz = 0x44, node_sz = 0x38;
        BYTE *nodes = blob + 12 + meta_sz;

        memset( blob, 0, sizeof(blob) );
        memcpy( blob, "XBF\0", 4 );
        *(DWORD *)(blob + 4) = meta_sz;
        *(DWORD *)(blob + 8) = node_sz;
        /* Minimal valid metadata: offs[0..5] = 0x38, 0 entries at offset 0x38 */
        *(UINT64 *)(blob + 20 + 0 * 8) = 0x38;
        *(UINT64 *)(blob + 20 + 3 * 8) = 0x38;
        *(UINT64 *)(blob + 20 + 4 * 8) = 0x38;
        *(UINT64 *)(blob + 20 + 5 * 8) = 0x38;

        /* 2 substreams: sub 0 = [0, 0x18), sub 1 = [0x18, 0x1c) (size 4) */
        *(DWORD *)(nodes + 0) = 2;
        *(DWORD *)(nodes + 4) = 0;
        *(DWORD *)(nodes + 8) = 0x18;
        *(DWORD *)(nodes + 12) = 0x18;
        *(DWORD *)(nodes + 16) = 0x1c;

        if (tc == 0)
        {
            /* Truncated def_kind=0x0a ResourceDictionary c1..c6 ULEB counts exceeding substream end */
            BYTE *s0 = nodes + 20;
            s0[0] = 0x17; s0[1] = 0x73; s0[2] = 0x81; /* Push ResourceDictionary */
            s0[3] = 0x0f; s0[4] = 0x01; s0[5] = 0x00; s0[6] = 0x00; s0[7] = 0x0a; /* DeferSubstream sub=1 kind=0x0a */
            s0[8] = 0x01; /* c1 = 1 */
            s0[9] = 0x00; s0[10] = 0x00; s0[11] = 0x7f; /* key_sid=0, sub_off=127 (> def_sub_sz 4!) */
            s0[12] = 0xff; s0[13] = 0x7f; /* c2 = 16383 (> remaining substream!) */
        }
        else if (tc == 1)
        {
            /* Truncated def_kind=0x05 VisualStateGroupCollection with state offset exceeding def_sub end */
            BYTE *s0 = nodes + 20;
            s0[0] = 0x17; s0[1] = 0xba; s0[2] = 0x81; /* Push Grid */
            s0[3] = 0x0f; s0[4] = 0x01; s0[5] = 0x00; s0[6] = 0x00; s0[7] = 0x05; /* DeferSubstream sub=1 kind=0x05 */
            s0[8] = 0x02; /* n_states = 2 */
            s0[9] = 0x40; /* st_off = 64 (> def_sub_sz 4!) */
            s0[10] = 0x80; /* truncated ULEB at substream end */
        }
        else if (tc == 2)
        {
            /* Out-of-range def_sub index in DeferSubstream */
            BYTE *s0 = nodes + 20;
            s0[0] = 0x0f; s0[1] = 0x55; s0[2] = 0x00; s0[3] = 0x00; s0[4] = 0x0a;
        }
        else
        {
            /* Substream directory end offset exceeding node_sz */
            *(DWORD *)(nodes + 16) = 0x9999;
        }

        hr = WindowsCreateString( (const WCHAR *)blob, sizeof(blob) / sizeof(WCHAR), &str );
        if (FAILED(hr)) { all_ok = FALSE; break; }
        /* Slot 6 on IXamlReaderStatics: Load(HSTRING, IInspectable**) */
        hr = ((const struct test_xr_statics_iface *)xr_statics)->lpVtbl->Load( xr_statics, str, &root );
        WindowsDeleteString( str );
        if (FAILED(hr) || !root) all_ok = FALSE;
        if (root) IInspectable_Release( root );
    }

    ok( all_ok, "truncated/malformed XBF v2 blobs should be rejected gracefully without crash\n" );
    IUnknown_Release( (IUnknown *)xr_statics );
}

struct test_dummy_handler
{
    const void **lpVtbl;
    LONG ref;
    LONG invoke_cnt;
};

static HRESULT WINAPI test_dh_QI( void *iface, REFIID iid, void **out )
{
    struct test_dummy_handler *h = iface;
    if (!out) return E_POINTER;
    *out = iface;
    InterlockedIncrement( &h->ref );
    return S_OK;
}
static ULONG WINAPI test_dh_AddRef( void *iface )
{
    struct test_dummy_handler *h = iface;
    return InterlockedIncrement( &h->ref );
}
static ULONG WINAPI test_dh_Release( void *iface )
{
    struct test_dummy_handler *h = iface;
    return InterlockedDecrement( &h->ref );
}
static HRESULT WINAPI test_dh_Invoke( void *iface, void *sender, void *args )
{
    struct test_dummy_handler *h = iface;
    InterlockedIncrement( &h->invoke_cnt );
    return S_OK;
}
static const void *test_dh_vtbl[] = { test_dh_QI, test_dh_AddRef, test_dh_Release, test_dh_Invoke };

struct test_wrs_vtbl
{
    HRESULT (WINAPI *QueryInterface)(void *, REFIID, void **);
    ULONG (WINAPI *AddRef)(void *);
    ULONG (WINAPI *Release)(void *);
    HRESULT (WINAPI *GetWeakReference)(void *, void **);
};
struct test_wrs_iface { const struct test_wrs_vtbl *lpVtbl; };

struct test_wr_vtbl
{
    HRESULT (WINAPI *QueryInterface)(void *, REFIID, void **);
    ULONG (WINAPI *AddRef)(void *);
    ULONG (WINAPI *Release)(void *);
    HRESULT (WINAPI *Resolve)(void *, REFIID, IInspectable **);
};
struct test_wr_iface { const struct test_wr_vtbl *lpVtbl; };

struct test_fe_ev_vtbl
{
    void *pad0_42[43];
    HRESULT (WINAPI *add_Loaded)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_Loaded)(void *, EventRegistrationToken);
    void *pad45_46[2];
    HRESULT (WINAPI *add_SizeChanged)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_SizeChanged)(void *, EventRegistrationToken);
    HRESULT (WINAPI *add_LayoutUpdated)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_LayoutUpdated)(void *, EventRegistrationToken);
};
struct test_fe_ev_iface { const struct test_fe_ev_vtbl *lpVtbl; };

struct test_btn_ev_vtbl
{
    void *pad0_13[14];
    HRESULT (WINAPI *add_Click)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_Click)(void *, EventRegistrationToken);
};
struct test_btn_ev_iface { const struct test_btn_ev_vtbl *lpVtbl; };

struct test_fe7_ev_vtbl
{
    void *pad0_6[7];
    HRESULT (WINAPI *add_EffectiveViewportChanged)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_EffectiveViewportChanged)(void *, EventRegistrationToken);
};
struct test_fe7_ev_iface { const struct test_fe7_ev_vtbl *lpVtbl; };

static void test_EventRegistryAndTransientLifecycle(void)
{
    static const GUID IID_IFrameworkElement_t = { 0xa391d09b, 0x4a99, 0x4b7c, { 0x9d, 0x8d, 0x6f, 0xa5, 0xd0, 0x1f, 0x6f, 0xbf } };
    static const GUID IID_IFrameworkElement7_t = { 0xc62080a9, 0x475e, 0x4b78, { 0x8a, 0xe8, 0x7c, 0x99, 0x46, 0x43, 0x1c, 0x45 } };
    static const GUID IID_IButtonBase_t = { 0xfa002c1a, 0x494e, 0x46cf, { 0x91, 0xd4, 0xe1, 0x4a, 0x8d, 0x79, 0x86, 0x74 } };
    static const GUID IID_IWeakReferenceSource_t = { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    HSTRING hcls;
    IInspectable *btn = NULL, *resolved = NULL;
    void *fe = NULL, *fe7 = NULL, *bb = NULL, *wrs = NULL, *wr = NULL;
    struct test_dummy_handler h1 = { test_dh_vtbl, 1, 0 };
    struct test_dummy_handler h2 = { test_dh_vtbl, 1, 0 };
    EventRegistrationToken tok1 = {0}, tok2 = {0};
    HRESULT hr;

    hr = WindowsCreateString( L"Windows.UI.Xaml.Controls.Button", 31, &hcls );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoActivateInstance( hcls, &btn );
    WindowsDeleteString( hcls );
    if (FAILED(hr) || !btn)
    {
        win_skip( "Button activation unavailable (hr %#lx)\n", hr );
        return;
    }

    /* 1. Test WeakReference before and after transient element destroy */
    hr = IInspectable_QueryInterface( btn, &IID_IWeakReferenceSource_t, &wrs );
    ok( hr == S_OK && wrs != NULL, "QI IWeakReferenceSource got hr %#lx wrs %p\n", hr, wrs );
    if (wrs)
    {
        hr = ((const struct test_wrs_iface *)wrs)->lpVtbl->GetWeakReference( wrs, &wr );
        ok( hr == S_OK && wr != NULL, "GetWeakReference got hr %#lx wr %p\n", hr, wr );
        IUnknown_Release( (IUnknown *)wrs );
    }
    if (wr)
    {
        hr = ((const struct test_wr_iface *)wr)->lpVtbl->Resolve( wr, &IID_IInspectable, &resolved );
        ok( hr == S_OK && resolved != NULL, "Resolve while alive got hr %#lx resolved %p\n", hr, resolved );
        if (resolved) IInspectable_Release( resolved );
    }

    /* 2. Test add/remove on migrated event registries (Loaded, SizeChanged, LayoutUpdated, Click, EVC) */
    if (SUCCEEDED(IInspectable_QueryInterface( btn, &IID_IFrameworkElement_t, &fe )) && fe)
    {
        const struct test_fe_ev_iface *f = (const struct test_fe_ev_iface *)fe;
        hr = f->lpVtbl->add_Loaded( fe, &h1, &tok1 );
        ok( hr == S_OK && tok1.value != 0, "add_Loaded hr %#lx tok %I64d\n", hr, tok1.value );
        ok( h1.ref == 2, "add_Loaded should AddRef handler (ref=%ld)\n", h1.ref );
        hr = f->lpVtbl->remove_Loaded( fe, tok1 );
        ok( hr == S_OK && h1.ref == 1, "remove_Loaded should Release handler (hr=%#lx ref=%ld)\n", hr, h1.ref );

        hr = f->lpVtbl->add_SizeChanged( fe, &h1, &tok1 );
        ok( hr == S_OK && tok1.value != 0 && h1.ref == 2, "add_SizeChanged hr %#lx ref %ld\n", hr, h1.ref );
        hr = f->lpVtbl->remove_SizeChanged( fe, tok1 );
        ok( hr == S_OK && h1.ref == 1, "remove_SizeChanged hr %#lx ref %ld\n", hr, h1.ref );

        hr = f->lpVtbl->add_LayoutUpdated( fe, &h1, &tok1 );
        ok( hr == S_OK && tok1.value != 0 && h1.ref == 2, "add_LayoutUpdated hr %#lx ref %ld\n", hr, h1.ref );
        hr = f->lpVtbl->remove_LayoutUpdated( fe, tok1 );
        ok( hr == S_OK && h1.ref == 1, "remove_LayoutUpdated hr %#lx ref %ld\n", hr, h1.ref );
        IUnknown_Release( (IUnknown *)fe );
    }

    if (SUCCEEDED(IInspectable_QueryInterface( btn, &IID_IButtonBase_t, &bb )) && bb)
    {
        const struct test_btn_ev_iface *b = (const struct test_btn_ev_iface *)bb;
        hr = b->lpVtbl->add_Click( bb, &h1, &tok1 );
        ok( hr == S_OK && tok1.value != 0 && h1.ref == 2, "add_Click hr %#lx ref %ld\n", hr, h1.ref );
        hr = b->lpVtbl->remove_Click( bb, tok1 );
        ok( hr == S_OK && h1.ref == 1, "remove_Click hr %#lx ref %ld\n", hr, h1.ref );
        /* Also leave h2 registered so we verify uwp_element_destroy releases it */
        hr = b->lpVtbl->add_Click( bb, &h2, &tok2 );
        ok( hr == S_OK && h2.ref == 2, "add_Click(h2) hr %#lx ref %ld\n", hr, h2.ref );
        IUnknown_Release( (IUnknown *)bb );
    }

    if (SUCCEEDED(IInspectable_QueryInterface( btn, &IID_IFrameworkElement7_t, &fe7 )) && fe7)
    {
        const struct test_fe7_ev_iface *f7 = (const struct test_fe7_ev_iface *)fe7;
        hr = f7->lpVtbl->add_EffectiveViewportChanged( fe7, &h1, &tok1 );
        ok( hr == S_OK && tok1.value != 0 && h1.ref == 2, "add_EVC hr %#lx ref %ld\n", hr, h1.ref );
        hr = f7->lpVtbl->remove_EffectiveViewportChanged( fe7, tok1 );
        ok( hr == S_OK && h1.ref == 1, "remove_EVC hr %#lx ref %ld\n", hr, h1.ref );
        IUnknown_Release( (IUnknown *)fe7 );
    }

    /* 3. Final Release destroys transient Button, releases h2, and invalidates WeakReference */
    IInspectable_Release( btn );
    ok( h2.ref == 1, "destroying transient element should release remaining event handlers (h2.ref=%ld)\n", h2.ref );
    if (wr)
    {
        resolved = (IInspectable *)0xdeadbeef;
        hr = ((const struct test_wr_iface *)wr)->lpVtbl->Resolve( wr, &IID_IInspectable, &resolved );
        ok( hr == S_OK && resolved == NULL, "Resolve after destroy should return NULL (hr=%#lx resolved=%p)\n", hr, resolved );
        IUnknown_Release( (IUnknown *)wr );
    }
}

struct test_fe2_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_RequestedTheme)(void *, int *);
    HRESULT (WINAPI *put_RequestedTheme)(void *, int);
};
struct test_fe2_iface { const struct test_fe2_vtbl *lpVtbl; };

struct test_fe6_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_ActualTheme)(void *, int *);
    HRESULT (WINAPI *add_ActualThemeChanged)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_ActualThemeChanged)(void *, EventRegistrationToken);
};
struct test_fe6_iface { const struct test_fe6_vtbl *lpVtbl; };

struct test_fe7_full_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_IsLoaded)(void *, boolean *);
};
struct test_fe7_full_iface { const struct test_fe7_full_vtbl *lpVtbl; };

struct test_icm_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *ItemFromContainer)(void *, void *, void **);
    HRESULT (WINAPI *ContainerFromItem)(void *, void *, void **);
    HRESULT (WINAPI *IndexFromContainer)(void *, void *, INT32 *);
    HRESULT (WINAPI *ContainerFromIndex)(void *, INT32, void **);
};
struct test_icm_iface { const struct test_icm_vtbl *lpVtbl; };

struct test_ic_vtbl
{
    void *pad0_6[7];
    HRESULT (WINAPI *put_ItemsSource)(void *, void *);
};
struct test_ic_iface { const struct test_ic_vtbl *lpVtbl; };

struct test_vec_vtbl
{
    void *pad0_12[13];
    HRESULT (WINAPI *Append)(void *, void *);
};
struct test_vec_iface { const struct test_vec_vtbl *lpVtbl; };

struct test_fm_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *GetFocusedElement)(void *, void **);
};
struct test_fm_iface { const struct test_fm_vtbl *lpVtbl; };

struct test_scale_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_CenterX)(void *, double *);
    HRESULT (WINAPI *put_CenterX)(void *, double);
    HRESULT (WINAPI *get_CenterY)(void *, double *);
    HRESULT (WINAPI *put_CenterY)(void *, double);
    HRESULT (WINAPI *get_ScaleX)(void *, double *);
    HRESULT (WINAPI *put_ScaleX)(void *, double);
    HRESULT (WINAPI *get_ScaleY)(void *, double *);
    HRESULT (WINAPI *put_ScaleY)(void *, double);
};
struct test_scale_iface { const struct test_scale_vtbl *lpVtbl; };

struct test_ts_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_IsOn)(void *, boolean *);
    HRESULT (WINAPI *put_IsOn)(void *, boolean);
    HRESULT (WINAPI *add_Toggled)(void *, void *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_Toggled)(void *, EventRegistrationToken);
};
struct test_ts_iface { const struct test_ts_vtbl *lpVtbl; };

struct test_pv_statics_vtbl
{
    void *pad0_17[18];
    HRESULT (WINAPI *CreateString)(void *, HSTRING, IInspectable **);
};
struct test_pv_statics_iface { const struct test_pv_statics_vtbl *lpVtbl; };

static void test_WinUINotepadRegressions(void)
{
    static const GUID IID_IFrameworkElement2_t = { 0xf19104be, 0x422a, 0x4904, { 0xa5, 0x2f, 0xee, 0x72, 0x01, 0x04, 0x29, 0xe5 } };
    static const GUID IID_IFrameworkElement6_t = { 0x792a5d91, 0x62a1, 0x40bf, { 0xa0, 0xce, 0xf9, 0xc1, 0x31, 0xfc, 0xb7, 0xa7 } };
    static const GUID IID_IFrameworkElement7_t = { 0xeb06716a, 0x4214, 0x499c, { 0xa4, 0x8a, 0x2d, 0x5e, 0x5b, 0x13, 0x5b, 0x30 } };
    static const GUID IID_IItemsControl_t = { 0x3d0813ba, 0x6890, 0x4537, { 0xbf, 0xe5, 0x79, 0x6d, 0x94, 0x58, 0xed, 0xd6 } };
    static const GUID IID_IItemContainerMapping_t = { 0x53c9f23a, 0xf25d, 0x446a, { 0x99, 0xb8, 0xc7, 0xae, 0xb8, 0x39, 0x05, 0x0f } };
    static const GUID IID_ITabView_t = { 0x6aa787ab, 0x5a30, 0x5ea2, { 0xbe, 0x5b, 0xae, 0xd8, 0x68, 0x38, 0x17, 0x56 } };
    static const GUID IID_IFocusManagerStatics_t = { 0x1eccd326, 0x8182, 0x4482, { 0x82, 0x6a, 0x09, 0x18, 0xe9, 0xed, 0x9a, 0xf7 } };
    static const GUID IID_IScaleTransform_t = { 0xed67f18d, 0x936e, 0x43ab, { 0x92, 0x9a, 0xe9, 0xcd, 0x0a, 0x51, 0x1e, 0x52 } };
    static const GUID IID_IFrame2_t = { 0x65d505d9, 0xf6c5, 0x489a, { 0x93, 0xeb, 0x77, 0xa8, 0xc0, 0xdd, 0x46, 0x5b } };
    static const GUID IID_IVector_obj_t = { 0x771b857e, 0xab5c, 0x5db8, { 0xa0, 0x21, 0x39, 0x7c, 0x92, 0xcd, 0xc4, 0x4c } };
    static const GUID IID_IPropertyValueStatics_t = { 0x629bdbc8, 0xd932, 0x4ff4, { 0x96, 0xb9, 0x8d, 0x96, 0xc5, 0xc1, 0xe8, 0x58 } };
    HSTRING hcls, hs0 = NULL, hs1 = NULL;
    IInspectable *grid = NULL, *lv = NULL, *tv = NULL, *item0 = NULL, *item1 = NULL, *vec_obj = NULL, *st = NULL;
    void *fe2 = NULL, *fe6 = NULL, *fe7 = NULL, *ic = NULL, *icm = NULL, *tabview_if = NULL, *fm = NULL, *sc = NULL, *frame2 = NULL;
    void *vec = NULL, *c0 = NULL, *it0 = NULL, *pvs = NULL;
    struct test_dummy_handler theme_h = { test_dh_vtbl, 1, 0 };
    EventRegistrationToken tok = {0};
    int theme = -1;
    boolean is_loaded = TRUE;
    INT32 idx = -99;
    void *focused = (void *)0xdeadbeef;
    double sx = 0.0, sy = 0.0, cx = 0.0, cy = 0.0;
    HRESULT hr;

    /* 1. IFrameworkElement6 (ActualTheme + ActualThemeChanged) vs IFrameworkElement7 (IsLoaded) */
    hr = WindowsCreateString( L"Windows.UI.Xaml.Controls.Grid", 29, &hcls );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoActivateInstance( hcls, &grid );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && grid != NULL, "Grid activate hr %#lx\n", hr );
    if (grid)
    {
        hr = IInspectable_QueryInterface( grid, &IID_IFrameworkElement6_t, &fe6 );
        ok( hr == S_OK && fe6 != NULL, "QI IFrameworkElement6 got hr %#lx\n", hr );
        hr = IInspectable_QueryInterface( grid, &IID_IFrameworkElement2_t, &fe2 );
        ok( hr == S_OK && fe2 != NULL, "QI IFrameworkElement2 got hr %#lx\n", hr );
        if (fe6 && fe2)
        {
            const struct test_fe6_iface *f6 = (const struct test_fe6_iface *)fe6;
            const struct test_fe2_iface *f2 = (const struct test_fe2_iface *)fe2;
            hr = f6->lpVtbl->get_ActualTheme( fe6, &theme );
            ok( hr == S_OK && (theme == 1 || theme == 2), "ActualTheme should be Light(1) or Dark(2), got %d (hr=%#lx)\n", theme, hr );
            hr = f6->lpVtbl->add_ActualThemeChanged( fe6, &theme_h, &tok );
            ok( hr == S_OK && tok.value != 0 && theme_h.ref == 2, "add_ActualThemeChanged hr %#lx ref %ld\n", hr, theme_h.ref );

            hr = f2->lpVtbl->put_RequestedTheme( fe2, 1 );
            ok( hr == S_OK, "put_RequestedTheme(1) hr %#lx\n", hr );
            hr = f6->lpVtbl->get_ActualTheme( fe6, &theme );
            ok( hr == S_OK && theme == 1, "ActualTheme after Light should be 1, got %d\n", theme );
            ok( theme_h.invoke_cnt >= 1, "ActualThemeChanged should fire on Light switch (invoked=%ld)\n", theme_h.invoke_cnt );

            hr = f2->lpVtbl->put_RequestedTheme( fe2, 2 );
            ok( hr == S_OK, "put_RequestedTheme(2) hr %#lx\n", hr );
            hr = f6->lpVtbl->get_ActualTheme( fe6, &theme );
            ok( hr == S_OK && theme == 2, "ActualTheme after Dark should be 2, got %d\n", theme );
            ok( theme_h.invoke_cnt >= 2, "ActualThemeChanged should fire on Dark switch (invoked=%ld)\n", theme_h.invoke_cnt );

            hr = f6->lpVtbl->remove_ActualThemeChanged( fe6, tok );
            ok( hr == S_OK && theme_h.ref == 1, "remove_ActualThemeChanged hr %#lx ref %ld\n", hr, theme_h.ref );
        }
        if (fe2) IUnknown_Release( (IUnknown *)fe2 );
        if (fe6) IUnknown_Release( (IUnknown *)fe6 );

        hr = IInspectable_QueryInterface( grid, &IID_IFrameworkElement7_t, &fe7 );
        ok( hr == S_OK && fe7 != NULL, "QI IFrameworkElement7 got hr %#lx\n", hr );
        if (fe7)
        {
            const struct test_fe7_full_iface *f7 = (const struct test_fe7_full_iface *)fe7;
            hr = f7->lpVtbl->get_IsLoaded( fe7, &is_loaded );
            ok( hr == S_OK && is_loaded == FALSE, "standalone Grid IsLoaded should be FALSE, got %d\n", is_loaded );
            IUnknown_Release( (IUnknown *)fe7 );
        }
        IInspectable_Release( grid );
    }

    /* 2. ListView rejects ITabView and implements IItemContainerMapping; TabView accepts ITabView */
    hr = WindowsCreateString( L"Windows.UI.Xaml.Controls.ListView", 33, &hcls );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoActivateInstance( hcls, &lv );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && lv != NULL, "ListView activate hr %#lx\n", hr );
    if (lv)
    {
        hr = IInspectable_QueryInterface( lv, &IID_ITabView_t, &tabview_if );
        ok( hr == E_NOINTERFACE && tabview_if == NULL, "ListView must NOT answer ITabView (hr=%#lx)\n", hr );
        if (tabview_if) { IUnknown_Release( (IUnknown *)tabview_if ); tabview_if = NULL; }

        hr = IInspectable_QueryInterface( lv, &IID_IItemContainerMapping_t, &icm );
        ok( hr == S_OK && icm != NULL, "ListView should answer IItemContainerMapping (hr=%#lx)\n", hr );
        hr = IInspectable_QueryInterface( lv, &IID_IItemsControl_t, &ic );
        ok( hr == S_OK && ic != NULL, "ListView should answer IItemsControl (hr=%#lx)\n", hr );

        hr = WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls );
        if (SUCCEEDED(hr) && SUCCEEDED(RoGetActivationFactory( hcls, &IID_IPropertyValueStatics_t, &pvs )) && pvs)
        {
            WindowsCreateString( L"Tab0", 4, &hs0 );
            WindowsCreateString( L"Tab1", 4, &hs1 );
            ((const struct test_pv_statics_iface *)pvs)->lpVtbl->CreateString( pvs, hs0, &item0 );
            ((const struct test_pv_statics_iface *)pvs)->lpVtbl->CreateString( pvs, hs1, &item1 );
            if (hs0) WindowsDeleteString( hs0 );
            if (hs1) WindowsDeleteString( hs1 );
            IUnknown_Release( (IUnknown *)pvs );
        }
        WindowsDeleteString( hcls );
        hr = WindowsCreateString( L"Windows.UI.Xaml.DependencyObjectCollection", 44, &hcls );
        if (SUCCEEDED(hr)) { RoActivateInstance( hcls, &vec_obj ); WindowsDeleteString( hcls ); }

        if (ic && icm && item0 && item1 && vec_obj &&
            SUCCEEDED(IInspectable_QueryInterface( vec_obj, &IID_IVector_obj_t, &vec )) && vec)
        {
            const struct test_vec_iface *v = (const struct test_vec_iface *)vec;
            const struct test_ic_iface *ici = (const struct test_ic_iface *)ic;
            const struct test_icm_iface *icmi = (const struct test_icm_iface *)icm;
            v->lpVtbl->Append( vec, item0 );
            v->lpVtbl->Append( vec, item1 );
            hr = ici->lpVtbl->put_ItemsSource( ic, vec_obj );
            ok( hr == S_OK, "put_ItemsSource hr %#lx\n", hr );

            hr = icmi->lpVtbl->ContainerFromIndex( icm, 1, &c0 );
            ok( hr == S_OK && c0 != NULL, "ContainerFromIndex(1) hr %#lx c0=%p\n", hr, c0 );
            if (c0)
            {
                hr = icmi->lpVtbl->IndexFromContainer( icm, c0, &idx );
                ok( hr == S_OK && idx == 1, "IndexFromContainer(c0) should be 1, got %d (hr=%#lx)\n", idx, hr );
                hr = icmi->lpVtbl->ItemFromContainer( icm, c0, &it0 );
                ok( hr == S_OK && it0 == item1, "ItemFromContainer(c0) should return item1=%p, got %p\n", item1, it0 );
                if (it0) IUnknown_Release( (IUnknown *)it0 );
                IUnknown_Release( (IUnknown *)c0 );
            }
        }
        if (vec) IUnknown_Release( (IUnknown *)vec );
        if (vec_obj) IInspectable_Release( vec_obj );
        if (item0) IInspectable_Release( item0 );
        if (item1) IInspectable_Release( item1 );
        if (ic) IUnknown_Release( (IUnknown *)ic );
        if (icm) IUnknown_Release( (IUnknown *)icm );
        IInspectable_Release( lv );
    }

    {
        HMODULE hmod = GetModuleHandleW( L"windows.ui.xaml.dll" );
        typedef HRESULT (WINAPI *pDllGetActivationFactory)(HSTRING, IActivationFactory **);
        pDllGetActivationFactory pGetFactory = hmod ? (pDllGetActivationFactory)GetProcAddress( hmod, "DllGetActivationFactory" ) : NULL;
        IActivationFactory *fac = NULL;
        hr = WindowsCreateString( L"Microsoft.UI.Xaml.Controls.TabView", 34, &hcls );
        ok( hr == S_OK, "got hr %#lx\n", hr );
        if (pGetFactory && SUCCEEDED(pGetFactory( hcls, &fac )) && fac)
        {
            hr = IActivationFactory_ActivateInstance( fac, &tv );
            IActivationFactory_Release( fac );
        }
        else
            hr = E_FAIL;
        WindowsDeleteString( hcls );
        ok( hr == S_OK && tv != NULL, "TabView activate hr %#lx\n", hr );
        if (tv)
        {
            hr = IInspectable_QueryInterface( tv, &IID_ITabView_t, &tabview_if );
            ok( hr == S_OK && tabview_if != NULL, "TabView should answer ITabView (hr=%#lx)\n", hr );
            if (tabview_if) IUnknown_Release( (IUnknown *)tabview_if );
            IInspectable_Release( tv );
        }
    }

    /* 3. FocusManager::GetFocusedElement returns NULL instead of a DependencyProperty */
    hr = WindowsCreateString( L"Windows.UI.Xaml.Input.FocusManager", 34, &hcls );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoGetActivationFactory( hcls, &IID_IFocusManagerStatics_t, &fm );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && fm != NULL, "RoGetActivationFactory FocusManager hr %#lx\n", hr );
    if (fm)
    {
        hr = ((const struct test_fm_iface *)fm)->lpVtbl->GetFocusedElement( fm, &focused );
        ok( hr == S_OK && focused == NULL, "GetFocusedElement should return S_OK + NULL, got hr=%#lx focused=%p\n", hr, focused );
        if (focused && (ULONG_PTR)focused > 0x10000) IUnknown_Release( (IUnknown *)focused );
        IUnknown_Release( (IUnknown *)fm );
    }

    /* 4. ScaleTransform getters/setters and QI_GUARD_NOT_FRAME */
    hr = WindowsCreateString( L"Windows.UI.Xaml.Media.ScaleTransform", 36, &hcls );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoActivateInstance( hcls, &st );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && st != NULL, "ScaleTransform activate hr %#lx\n", hr );
    if (st)
    {
        hr = IInspectable_QueryInterface( st, &IID_IFrame2_t, &frame2 );
        ok( hr == E_NOINTERFACE && frame2 == NULL, "ScaleTransform must not answer IFrame2 (hr=%#lx)\n", hr );
        if (frame2) IUnknown_Release( (IUnknown *)frame2 );

        hr = IInspectable_QueryInterface( st, &IID_IScaleTransform_t, &sc );
        ok( hr == S_OK && sc != NULL, "QI IScaleTransform got hr %#lx\n", hr );
        if (sc)
        {
            const struct test_scale_iface *si = (const struct test_scale_iface *)sc;
            si->lpVtbl->get_ScaleX( sc, &sx );
            si->lpVtbl->get_ScaleY( sc, &sy );
            ok( sx == 1.0 && sy == 1.0, "ScaleTransform defaults should be 1.0/1.0, got %.2f/%.2f\n", sx, sy );
            si->lpVtbl->put_CenterX( sc, 10.0 );
            si->lpVtbl->put_CenterY( sc, 12.0 );
            si->lpVtbl->put_ScaleX( sc, 1.5 );
            si->lpVtbl->put_ScaleY( sc, 0.75 );
            si->lpVtbl->get_CenterX( sc, &cx );
            si->lpVtbl->get_CenterY( sc, &cy );
            si->lpVtbl->get_ScaleX( sc, &sx );
            si->lpVtbl->get_ScaleY( sc, &sy );
            ok( cx == 10.0 && cy == 12.0 && sx == 1.5 && sy == 0.75,
                "ScaleTransform roundtrip got (%.2f, %.2f, %.2f, %.2f)\n", cx, cy, sx, sy );
            IUnknown_Release( (IUnknown *)sc );
        }
        IInspectable_Release( st );
    }

    /* 5. ToggleSwitch (IToggleSwitch IsOn + Toggled) & HyperlinkButton */
    {
        static const GUID IID_IToggleSwitch_t = { 0x331d8f00, 0xc5f9, 0x46a5, { 0xb6, 0xc8, 0xed, 0xe5, 0x39, 0x30, 0x45, 0x67 } };
        static const GUID IID_IHyperlinkButton_t = { 0x325c44e0, 0x9a03, 0x4bf3, { 0xab, 0xd6, 0x6f, 0xbb, 0x46, 0xc9, 0xa4, 0x86 } };
        IInspectable *ts = NULL, *hb = NULL;
        void *its = NULL, *ihb = NULL;
        struct test_dummy_handler ts_h = { test_dh_vtbl, 1, 0 };
        EventRegistrationToken ts_tok = {0};
        boolean is_on = TRUE;

        hr = WindowsCreateString( L"Windows.UI.Xaml.Controls.ToggleSwitch", 37, &hcls );
        ok( hr == S_OK, "got hr %#lx\n", hr );
        hr = RoActivateInstance( hcls, &ts );
        WindowsDeleteString( hcls );
        ok( hr == S_OK && ts != NULL, "ToggleSwitch activate hr %#lx\n", hr );
        if (ts)
        {
            hr = IInspectable_QueryInterface( ts, &IID_IToggleSwitch_t, &its );
            ok( hr == S_OK && its != NULL, "QI IToggleSwitch hr %#lx\n", hr );
            if (its)
            {
                const struct test_ts_iface *tsi = (const struct test_ts_iface *)its;
                hr = tsi->lpVtbl->get_IsOn( its, &is_on );
                ok( hr == S_OK && is_on == FALSE, "ToggleSwitch IsOn default should be FALSE, got %d\n", is_on );
                hr = tsi->lpVtbl->add_Toggled( its, &ts_h, &ts_tok );
                ok( hr == S_OK && ts_tok.value != 0 && ts_h.ref == 2, "add_Toggled hr %#lx ref %ld\n", hr, ts_h.ref );
                hr = tsi->lpVtbl->put_IsOn( its, TRUE );
                ok( hr == S_OK, "put_IsOn(TRUE) hr %#lx\n", hr );
                hr = tsi->lpVtbl->get_IsOn( its, &is_on );
                ok( hr == S_OK && is_on == TRUE && ts_h.invoke_cnt >= 1,
                    "ToggleSwitch IsOn should be TRUE and Toggled fired (on=%d cnt=%ld)\n", is_on, ts_h.invoke_cnt );
                hr = tsi->lpVtbl->remove_Toggled( its, ts_tok );
                ok( hr == S_OK && ts_h.ref == 1, "remove_Toggled hr %#lx ref %ld\n", hr, ts_h.ref );
                IUnknown_Release( (IUnknown *)its );
            }
            IInspectable_Release( ts );
        }

        hr = WindowsCreateString( L"Windows.UI.Xaml.Controls.HyperlinkButton", 40, &hcls );
        ok( hr == S_OK, "got hr %#lx\n", hr );
        hr = RoActivateInstance( hcls, &hb );
        WindowsDeleteString( hcls );
        ok( hr == S_OK && hb != NULL, "HyperlinkButton activate hr %#lx\n", hr );
        if (hb)
        {
            hr = IInspectable_QueryInterface( hb, &IID_IHyperlinkButton_t, &ihb );
            ok( hr == S_OK && ihb != NULL, "QI IHyperlinkButton hr %#lx\n", hr );
            if (ihb) IUnknown_Release( (IUnknown *)ihb );
            IInspectable_Release( hb );
        }
    }
}

struct test_typename { HSTRING Name; int Kind; };

struct test_conv_obj
{
    const void **lpVtbl;
    LONG ref;
    LONG convert_cnt;
    IInspectable *last_param;
    WCHAR last_lang[32];
};

static HRESULT WINAPI test_conv_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IValueConverter_t = { 0x7f5ee992, 0x7694, 0x4e6c, { 0xa5, 0x18, 0x4e, 0x6f, 0x31, 0xb1, 0x79, 0x76 } };
    struct test_conv_obj *c = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IValueConverter_t ))
    {
        *out = iface;
        InterlockedIncrement( &c->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI test_conv_AddRef( void *iface ) { return InterlockedIncrement( &((struct test_conv_obj *)iface)->ref ); }
static ULONG WINAPI test_conv_Release( void *iface ) { return InterlockedDecrement( &((struct test_conv_obj *)iface)->ref ); }
static HRESULT WINAPI test_conv_GetIids( void *iface, ULONG *c, IID **iids ) { if (c) *c = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI test_conv_GetRCN( void *iface, HSTRING *n ) { return WindowsCreateString( L"Test.Converter", 14, n ); }
static HRESULT WINAPI test_conv_GetTL( void *iface, TrustLevel *tl ) { if (tl) *tl = BaseTrust; return S_OK; }
static HRESULT WINAPI test_conv_Convert( void *iface, IInspectable *val, struct test_typename tname,
                                          IInspectable *param, HSTRING lang, IInspectable **out )
{
    struct test_conv_obj *c = iface;
    const WCHAR *lraw = lang ? WindowsGetStringRawBuffer( lang, NULL ) : L"";
    (void)val;
    (void)tname;
    InterlockedIncrement( &c->convert_cnt );
    c->last_param = param;
    lstrcpynW( c->last_lang, lraw ? lraw : L"", ARRAY_SIZE(c->last_lang) );
    if (out)
    {
        static struct fake_string_pv converted_pv = { fake_pv_vtbl, 1, L"converted" };
        converted_pv.ref++;
        *out = (IInspectable *)&converted_pv;
    }
    return S_OK;
}
static HRESULT WINAPI test_conv_ConvertBack( void *iface, IInspectable *val, struct test_typename tname,
                                              IInspectable *param, HSTRING lang, IInspectable **out )
{
    (void)iface; (void)val; (void)tname; (void)param; (void)lang;
    if (out) *out = NULL;
    return E_NOTIMPL;
}
static const void *test_conv_vtbl[] = {
    test_conv_QI, test_conv_AddRef, test_conv_Release,
    test_conv_GetIids, test_conv_GetRCN, test_conv_GetTL,
    test_conv_Convert, test_conv_ConvertBack
};

struct test_binding_vtbl
{
    void *pad0_7[8];
    HRESULT (WINAPI *get_Mode)(void *, INT32 *);
    HRESULT (WINAPI *put_Mode)(void *, INT32);
    void *pad10_15[6];
    HRESULT (WINAPI *get_Converter)(void *, IInspectable **);
    HRESULT (WINAPI *put_Converter)(void *, IInspectable *);
    HRESULT (WINAPI *get_ConverterParameter)(void *, IInspectable **);
    HRESULT (WINAPI *put_ConverterParameter)(void *, IInspectable *);
    HRESULT (WINAPI *get_ConverterLanguage)(void *, HSTRING *);
    HRESULT (WINAPI *put_ConverterLanguage)(void *, HSTRING);
};
struct test_binding_iface { const struct test_binding_vtbl *lpVtbl; };

struct test_bops_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *SetBinding)(void *, void *, void *, void *);
};
struct test_bops_iface { const struct test_bops_vtbl *lpVtbl; };

struct test_pr_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_IsActive)(void *, boolean *);
    HRESULT (WINAPI *put_IsActive)(void *, boolean);
};
struct test_pr_iface { const struct test_pr_vtbl *lpVtbl; };

struct test_flyout_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_Content)(void *, IInspectable **);
    HRESULT (WINAPI *put_Content)(void *, IInspectable *);
};
struct test_flyout_iface { const struct test_flyout_vtbl *lpVtbl; };

struct test_flyout_base_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *ShowAt)(void *, void *);
    HRESULT (WINAPI *Hide)(void *);
};
struct test_flyout_base_iface { const struct test_flyout_base_vtbl *lpVtbl; };

struct test_feap_statics_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *CreatePeerForElement)(void *, void *, void **);
};
struct test_feap_statics_iface { const struct test_feap_statics_vtbl *lpVtbl; };

struct test_ap_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *GetPattern)(void *, INT32, void **);
};
struct test_ap_iface { const struct test_ap_vtbl *lpVtbl; };

struct test_invoke_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *Invoke)(void *);
};
struct test_invoke_iface { const struct test_invoke_vtbl *lpVtbl; };

struct test_dp_prop_slot_vtbl
{
    void *pad0_5[6];
    HRESULT (WINAPI *get_Prop)(void *, void **);
};
struct test_dp_prop_slot_iface { const struct test_dp_prop_slot_vtbl *lpVtbl; };

static void test_CalculatorQaRegressions(void)
{
    static const GUID IID_IReferenceTrackerTarget_t = { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
    static const GUID IID_IUIElementStatics_t = { 0x58d3573b, 0xf52c, 0x45be, { 0x98, 0x8b, 0xa5, 0x86, 0x95, 0x64, 0x87, 0x3c } };
    static const GUID IID_IControlStatics_t = { 0x473b305b, 0x877d, 0x4c35, { 0x8f, 0xca, 0x46, 0xa0, 0x5f, 0x96, 0x60, 0x7a } };
    static const GUID IID_IProgressRingStatics_t = { 0x6e820f24, 0x64d6, 0x4570, { 0x8d, 0xc2, 0xb1, 0xc8, 0x20, 0xec, 0x52, 0xd6 } };
    static const GUID IID_IBindingOperationsStatics_t = { 0xe155ef73, 0x95a0, 0x4aab, { 0x8c, 0x7d, 0x2a, 0x47, 0xda, 0x07, 0x3c, 0x79 } };
    static const GUID IID_IButtonBaseStatics_t = { 0x77b0fb6e, 0x2950, 0x4b42, { 0xbf, 0xae, 0xf1, 0x05, 0xaa, 0x5a, 0xa1, 0x16 } };
    static const GUID IID_ISelectorStatics_t = { 0x13300b06, 0xc2ef, 0x4b08, { 0xb5, 0x14, 0xdd, 0x8f, 0xe8, 0xfa, 0x37, 0xbe } };
    static const GUID IID_IGridStatics_t = { 0x1e661634, 0x7856, 0x44f4, { 0x89, 0x2e, 0x14, 0xd1, 0x15, 0x5c, 0xa2, 0x10 } };
    static const GUID IID_IFocusManagerStatics_t = { 0x1eccd326, 0x8182, 0x4482, { 0x82, 0x6a, 0x09, 0x18, 0xe9, 0xed, 0x9a, 0xf7 } };
    static const GUID IID_IThicknessHelperStatics_t = { 0xc0991a7c, 0x070c, 0x4da6, { 0x87, 0x84, 0x01, 0xca, 0x80, 0x0e, 0xb7, 0x3a } };
    static const GUID IID_IXamlMarkupHelperStatics_t = { 0xc9bc3725, 0xf34f, 0x445c, { 0x81, 0xa2, 0x6b, 0x72, 0xa5, 0xe8, 0xf0, 0x72 } };
    static const GUID IID_IBinding_t = { 0x3f7a0c6b, 0xd00f, 0x4730, { 0x8c, 0x1d, 0x48, 0xe1, 0x6c, 0x46, 0xf9, 0xca } };
    static const GUID IID_IProgressRing_t = { 0x7132f7bf, 0x88c5, 0x42a2, { 0x96, 0xd9, 0x24, 0xe4, 0xc7, 0x03, 0x6d, 0x0a } };
    static const GUID IID_IFlyout_t = { 0xd075852d, 0xb09a, 0x4fd1, { 0xb0, 0x05, 0xdb, 0x2b, 0xa0, 0x12, 0x06, 0xfb } };
    static const GUID IID_IFlyoutBase_t = { 0x723eea0b, 0xd12e, 0x430d, { 0xa9, 0xf0, 0x9b, 0xb3, 0x2b, 0xbf, 0x99, 0x13 } };
    static const GUID IID_IFeapStatics_t = { 0x6c218499, 0x6708, 0x454f, { 0xb1, 0xc6, 0x4f, 0x9b, 0x9d, 0x45, 0xef, 0x9e } };
    static const GUID IID_IButtonBase_t = { 0xfa002c1a, 0x494e, 0x46cf, { 0x91, 0xd4, 0xe1, 0x4a, 0x8d, 0x79, 0x86, 0x74 } };
    static const GUID IID_IInvokeProvider_t = { 0xf7d1a187, 0xb13c, 0x4540, { 0xb0, 0x9e, 0x67, 0x78, 0xe2, 0xdc, 0x9b, 0xa5 } };
    static const GUID IID_IDependencyPropertyStatics_t = { 0x49e5f28f, 0x8259, 0x4d5c, { 0xaa, 0xe0, 0x83, 0xd5, 0x6d, 0xbb, 0x68, 0xd9 } };
    static const struct { const WCHAR *cls; const GUID *statics_iid; } statics_cases[] = {
        { L"Windows.UI.Xaml.Controls.Primitives.ButtonBase", &IID_IButtonBaseStatics_t },
        { L"Windows.UI.Xaml.Controls.Primitives.Selector",   &IID_ISelectorStatics_t },
        { L"Windows.UI.Xaml.UIElement",                      &IID_IUIElementStatics_t },
        { L"Windows.UI.Xaml.Controls.Grid",                  &IID_IGridStatics_t },
        { L"Windows.UI.Xaml.Input.FocusManager",             &IID_IFocusManagerStatics_t },
        { L"Windows.UI.Xaml.Data.BindingOperations",         &IID_IBindingOperationsStatics_t },
        { L"Windows.UI.Xaml.ThicknessHelper",                &IID_IThicknessHelperStatics_t },
        { L"Windows.UI.Xaml.Markup.XamlMarkupHelper",        &IID_IXamlMarkupHelperStatics_t },
    };
    HSTRING hcls = NULL, hstr = NULL, htxt = NULL;
    IActivationFactory *af = NULL;
    void *dummy = NULL, *dp_statics = NULL;
    IInspectable *tb = NULL, *bind = NULL, *pr = NULL, *fl = NULL, *grd = NULL, *btn = NULL;
    void *bops = NULL, *ibind = NULL, *tb_fe = NULL, *tb_if = NULL, *ipr = NULL;
    void *ifl = NULL, *ifb = NULL, *feap_st = NULL, *peer = NULL, *invp = NULL, *bb = NULL;
    IUnknown *text_dp = NULL;
    struct { HSTRING Name; int Kind; } tname = { NULL, 0 };
    struct test_conv_obj conv = { test_conv_vtbl, 1, 0, NULL, {0} };
    struct fake_string_pv param_pv = { fake_pv_vtbl, 1, L"p1" };
    struct fake_string_pv src_pv = { fake_pv_vtbl, 1, L"raw" };
    struct test_dummy_handler clk_h = { test_dh_vtbl, 1, 0 };
    EventRegistrationToken clk_tok = {0};
    HRESULT hr;
    unsigned int k;

    /* 1. Embedded statics interfaces resolve back to factory via QI and reject tracker/marshal IIDs */
    {
        HMODULE hmod = GetModuleHandleW( L"windows.ui.xaml.dll" );
        typedef HRESULT (WINAPI *pDllGetActivationFactory)(HSTRING, IActivationFactory **);
        pDllGetActivationFactory pGetFactory = hmod ? (pDllGetActivationFactory)GetProcAddress( hmod, "DllGetActivationFactory" ) : NULL;
        for (k = 0; k < ARRAY_SIZE(statics_cases); k++)
        {
            void *st_if = NULL, *af_back = NULL;
            WindowsCreateString( statics_cases[k].cls, wcslen( statics_cases[k].cls ), &hcls );
            hr = RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&af );
            if (FAILED(hr) && pGetFactory)
                hr = pGetFactory( hcls, &af );
            WindowsDeleteString( hcls );
            ok( hr == S_OK && af != NULL, "factory[%u] hr %#lx\n", k, hr );
            if (af)
            {
                dummy = (void *)0xdeadbeef;
                hr = IActivationFactory_QueryInterface( af, &IID_IReferenceTrackerTarget_t, &dummy );
                ok( hr == E_NOINTERFACE && dummy == NULL, "factory[%u] tracker QI expected E_NOINTERFACE, got %#lx (%p)\n",
                    k, hr, dummy );
                hr = IActivationFactory_QueryInterface( af, statics_cases[k].statics_iid, &st_if );
                ok( hr == S_OK && st_if != NULL, "factory[%u] statics QI hr %#lx\n", k, hr );
                if (st_if)
                {
                    hr = IUnknown_QueryInterface( (IUnknown *)st_if, &IID_IActivationFactory, &af_back );
                    ok( hr == S_OK && af_back == (void *)af, "factory[%u] roundtrip QI got hr=%#lx back=%p af=%p\n",
                        k, hr, af_back, af );
                    if (af_back) IUnknown_Release( (IUnknown *)af_back );
                    IUnknown_Release( (IUnknown *)st_if );
                }
                IActivationFactory_Release( af );
                af = NULL;
            }
        }
    }

    /* 2. Named DPs on UIElementStatics / ControlStatics / ProgressRingStatics */
    {
        void *vis_dp = NULL, *en_dp = NULL, *act_dp = NULL;
        void *uie_st = NULL, *ctrl_st = NULL, *pr_st = NULL;
        WindowsCreateString( L"Windows.UI.Xaml.UIElement", 25, &hcls );
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IUIElementStatics_t, &uie_st )) && uie_st)
        {
            const struct test_dp_prop_slot_vtbl *v = *(const struct test_dp_prop_slot_vtbl **)uie_st;
            typedef HRESULT (WINAPI *get_dp_fn)(void *, void **);
            hr = ((get_dp_fn)((void *const *)v)[35])( uie_st, &vis_dp );
            ok( hr == S_OK && vis_dp != NULL, "UIElementStatics::VisibilityProperty hr %#lx dp=%p\n", hr, vis_dp );
            IUnknown_Release( (IUnknown *)uie_st );
        }
        WindowsDeleteString( hcls );

        WindowsCreateString( L"Windows.UI.Xaml.Controls.Control", 32, &hcls );
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IControlStatics_t, &ctrl_st )) && ctrl_st)
        {
            typedef HRESULT (WINAPI *get_dp_fn)(void *, void **);
            hr = ((get_dp_fn)(*(void *const **)ctrl_st)[14])( ctrl_st, &en_dp );
            ok( hr == S_OK && en_dp != NULL, "ControlStatics::IsEnabledProperty hr %#lx dp=%p\n", hr, en_dp );
            IUnknown_Release( (IUnknown *)ctrl_st );
        }
        WindowsDeleteString( hcls );

        WindowsCreateString( L"Windows.UI.Xaml.Controls.ProgressRing", 37, &hcls );
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IProgressRingStatics_t, &pr_st )) && pr_st)
        {
            typedef HRESULT (WINAPI *get_dp_fn)(void *, void **);
            hr = ((get_dp_fn)(*(void *const **)pr_st)[6])( pr_st, &act_dp );
            ok( hr == S_OK && act_dp != NULL, "ProgressRingStatics::IsActiveProperty hr %#lx dp=%p\n", hr, act_dp );
            IUnknown_Release( (IUnknown *)pr_st );
        }
        WindowsDeleteString( hcls );

        ok( vis_dp != en_dp && vis_dp != act_dp && en_dp != act_dp,
            "named DPs should be distinct (%p, %p, %p)\n", vis_dp, en_dp, act_dp );
        if (vis_dp) IUnknown_Release( (IUnknown *)vis_dp );
        if (en_dp) IUnknown_Release( (IUnknown *)en_dp );
        if (act_dp) IUnknown_Release( (IUnknown *)act_dp );
    }

    /* 3. IValueConverter + BindingOperations::SetBinding */
    WindowsCreateString( L"Windows.UI.Xaml.Controls.TextBlock", 34, &hcls );
    hr = RoActivateInstance( hcls, &tb );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && tb != NULL, "TextBlock activate hr %#lx\n", hr );
    WindowsCreateString( L"Windows.UI.Xaml.Data.Binding", 28, &hcls );
    hr = RoActivateInstance( hcls, &bind );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && bind != NULL, "Binding activate hr %#lx\n", hr );
    WindowsCreateString( L"Windows.UI.Xaml.DependencyProperty", 34, &hcls );
    RoGetActivationFactory( hcls, &IID_IDependencyPropertyStatics_t, &dp_statics );
    WindowsDeleteString( hcls );
    WindowsCreateString( L"Windows.UI.Xaml.Data.BindingOperations", 38, &hcls );
    RoGetActivationFactory( hcls, &IID_IBindingOperationsStatics_t, &bops );
    WindowsDeleteString( hcls );

    if (tb && bind && dp_statics && bops)
    {
        WindowsCreateString( L"String", 6, &tname.Name );
        WindowsCreateString( L"Text", 4, &hstr );
        ((const struct test_dp_statics_iface *)dp_statics)->lpVtbl->Register( dp_statics, hstr, &tname, &tname, NULL, &text_dp );
        WindowsDeleteString( hstr );
        WindowsDeleteString( tname.Name );

        if (SUCCEEDED(IInspectable_QueryInterface( bind, &IID_IBinding_t, &ibind )) && ibind)
        {
            const struct test_binding_iface *bi = (const struct test_binding_iface *)ibind;
            INT32 mode = 0;
            WindowsCreateString( L"en-US", 5, &hstr );
            bi->lpVtbl->put_Mode( ibind, 3 );
            bi->lpVtbl->get_Mode( ibind, &mode );
            ok( mode == 3, "Binding Mode expected 3, got %d\n", mode );
            bi->lpVtbl->put_Converter( ibind, (IInspectable *)&conv );
            bi->lpVtbl->put_ConverterParameter( ibind, (IInspectable *)&param_pv );
            bi->lpVtbl->put_ConverterLanguage( ibind, hstr );
            WindowsDeleteString( hstr );
            IUnknown_Release( (IUnknown *)ibind );
        }
        IInspectable_QueryInterface( tb, &IID_IFrameworkElement_test, &tb_fe );
        IInspectable_QueryInterface( tb, &IID_ITextBlock_test, &tb_if );
        if (tb_fe && tb_if && text_dp)
        {
            ((const struct test_bops_iface *)bops)->lpVtbl->SetBinding( bops, tb, text_dp, bind );
            ((const struct test_fe_iface *)tb_fe)->lpVtbl->put_DataContext( tb_fe, &src_pv );
            hr = ((const struct test_tb_iface *)tb_if)->lpVtbl->get_Text( tb_if, &htxt );
            ok( hr == S_OK && htxt && !wcscmp( WindowsGetStringRawBuffer( htxt, NULL ), L"converted" ),
                "expected converted TextBlock text, got %s\n", wine_dbgstr_w( htxt ? WindowsGetStringRawBuffer( htxt, NULL ) : NULL ) );
            ok( conv.convert_cnt >= 1 && conv.last_param == (IInspectable *)&param_pv && !wcscmp( conv.last_lang, L"en-US" ),
                "expected converter called with param & en-US (cnt=%ld lang=%s)\n",
                conv.convert_cnt, wine_dbgstr_w( conv.last_lang ) );
            if (htxt) WindowsDeleteString( htxt );
        }
        if (tb_fe) IUnknown_Release( (IUnknown *)tb_fe );
        if (tb_if) IUnknown_Release( (IUnknown *)tb_if );
        if (text_dp) IUnknown_Release( text_dp );
    }
    if (bops) IUnknown_Release( (IUnknown *)bops );
    if (dp_statics) IUnknown_Release( (IUnknown *)dp_statics );
    if (bind) IInspectable_Release( bind );
    if (tb) IInspectable_Release( tb );

    /* 4. IProgressRing IsActive */
    WindowsCreateString( L"Windows.UI.Xaml.Controls.ProgressRing", 37, &hcls );
    hr = RoActivateInstance( hcls, &pr );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && pr != NULL, "ProgressRing activate hr %#lx\n", hr );
    if (pr && SUCCEEDED(IInspectable_QueryInterface( pr, &IID_IProgressRing_t, &ipr )) && ipr)
    {
        const struct test_pr_iface *pri = (const struct test_pr_iface *)ipr;
        boolean act = TRUE;
        pri->lpVtbl->get_IsActive( ipr, &act );
        ok( act == FALSE, "ProgressRing IsActive default should be FALSE, got %d\n", act );
        pri->lpVtbl->put_IsActive( ipr, TRUE );
        pri->lpVtbl->get_IsActive( ipr, &act );
        ok( act == TRUE, "ProgressRing IsActive after put(TRUE) should be TRUE, got %d\n", act );
        IUnknown_Release( (IUnknown *)ipr );
    }
    if (pr) IInspectable_Release( pr );

    /* 5. Flyout.Content + ShowAt/Hide */
    WindowsCreateString( L"Windows.UI.Xaml.Controls.Flyout", 31, &hcls );
    hr = RoActivateInstance( hcls, &fl );
    WindowsDeleteString( hcls );
    WindowsCreateString( L"Windows.UI.Xaml.Controls.Grid", 29, &hcls );
    RoActivateInstance( hcls, &grd );
    WindowsDeleteString( hcls );
    ok( hr == S_OK && fl != NULL && grd != NULL, "Flyout/Grid activate hr %#lx\n", hr );
    if (fl && grd)
    {
        hr = IInspectable_QueryInterface( fl, &IID_IFlyout_t, &ifl );
        ok( hr == S_OK && ifl != NULL, "QI IFlyout hr %#lx\n", hr );
        hr = IInspectable_QueryInterface( fl, &IID_IFlyoutBase_t, &ifb );
        ok( hr == S_OK && ifb != NULL, "QI IFlyoutBase hr %#lx\n", hr );
        if (ifl && ifb)
        {
            IInspectable *got_c = NULL;
            ((const struct test_flyout_iface *)ifl)->lpVtbl->put_Content( ifl, grd );
            ((const struct test_flyout_iface *)ifl)->lpVtbl->get_Content( ifl, &got_c );
            ok( got_c != NULL, "Flyout.Content should survive ShowAt/Hide slot separation\n" );
            if (got_c) IInspectable_Release( got_c );
            hr = ((const struct test_flyout_base_iface *)ifb)->lpVtbl->ShowAt( ifb, grd );
            ok( hr == S_OK, "FlyoutBase::ShowAt hr %#lx\n", hr );
            hr = ((const struct test_flyout_base_iface *)ifb)->lpVtbl->Hide( ifb );
            ok( hr == S_OK, "FlyoutBase::Hide hr %#lx\n", hr );
        }
        if (ifl) IUnknown_Release( (IUnknown *)ifl );
        if (ifb) IUnknown_Release( (IUnknown *)ifb );
    }
    if (fl) IInspectable_Release( fl );
    if (grd) IInspectable_Release( grd );

    /* 6. FrameworkElementAutomationPeer::CreatePeerForElement -> IInvokeProvider::Invoke */
    WindowsCreateString( L"Windows.UI.Xaml.Controls.Button", 31, &hcls );
    hr = RoActivateInstance( hcls, &btn );
    WindowsDeleteString( hcls );
    WindowsCreateString( L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer", 63, &hcls );
    RoGetActivationFactory( hcls, &IID_IFeapStatics_t, &feap_st );
    WindowsDeleteString( hcls );
    if (btn && feap_st)
    {
        if (SUCCEEDED(IInspectable_QueryInterface( btn, &IID_IButtonBase_t, &bb )) && bb)
        {
            ((const struct test_btn_ev_iface *)bb)->lpVtbl->add_Click( bb, &clk_h, &clk_tok );
            hr = ((const struct test_feap_statics_iface *)feap_st)->lpVtbl->CreatePeerForElement( feap_st, btn, &peer );
            ok( hr == S_OK && peer != NULL, "CreatePeerForElement hr %#lx peer=%p\n", hr, peer );
            if (peer)
            {
                hr = ((const struct test_ap_iface *)peer)->lpVtbl->GetPattern( peer, 0 /* Invoke */, &invp );
                ok( hr == S_OK && invp != NULL, "GetPattern(Invoke) hr %#lx invp=%p\n", hr, invp );
                if (invp)
                {
                    void *inv_qi = NULL;
                    hr = IUnknown_QueryInterface( (IUnknown *)invp, &IID_IInvokeProvider_t, &inv_qi );
                    ok( hr == S_OK && inv_qi != NULL, "QI IInvokeProvider hr %#lx\n", hr );
                    if (inv_qi)
                    {
                        hr = ((const struct test_invoke_iface *)inv_qi)->lpVtbl->Invoke( inv_qi );
                        ok( hr == S_OK && clk_h.invoke_cnt >= 1,
                            "IInvokeProvider::Invoke should fire Button::Click (hr=%#lx cnt=%ld)\n", hr, clk_h.invoke_cnt );
                        IUnknown_Release( (IUnknown *)inv_qi );
                    }
                    IUnknown_Release( (IUnknown *)invp );
                }
                IUnknown_Release( (IUnknown *)peer );
            }
            ((const struct test_btn_ev_iface *)bb)->lpVtbl->remove_Click( bb, clk_tok );
            IUnknown_Release( (IUnknown *)bb );
        }
    }
    if (feap_st) IUnknown_Release( (IUnknown *)feap_st );
    if (btn) IInspectable_Release( btn );
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
    test_VectorGrowthBeyondOldCaps();
    test_TruncatedXbfBounds();
    test_EventRegistryAndTransientLifecycle();
    test_WinUINotepadRegressions();
    test_CalculatorQaRegressions();

    RoUninitialize();
}
