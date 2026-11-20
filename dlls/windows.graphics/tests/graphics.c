/*
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
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
#include <math.h>
#define COBJMACROS
#include "initguid.h"
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winstring.h"
#include "roapi.h"
#include "wine/test.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Graphics_Display
#include "windows.graphics.display.h"

DEFINE_GUID( IID_IDisplayInformationInterop, 0x7449121c, 0x382b, 0x4705, 0x8d, 0xa7, 0xa7, 0x95, 0xba, 0x48, 0x20, 0x13 );

typedef struct IDisplayInformationInterop IDisplayInformationInterop;
typedef struct IDisplayInformationInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)( IDisplayInformationInterop *iface, REFIID riid, void **out );
    ULONG (WINAPI *AddRef)( IDisplayInformationInterop *iface );
    ULONG (WINAPI *Release)( IDisplayInformationInterop *iface );
    HRESULT (WINAPI *GetIids)( IDisplayInformationInterop *iface, ULONG *iid_count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( IDisplayInformationInterop *iface, HSTRING *class_name );
    HRESULT (WINAPI *GetTrustLevel)( IDisplayInformationInterop *iface, TrustLevel *trust_level );
    HRESULT (WINAPI *GetForWindow)( IDisplayInformationInterop *iface, HWND window, REFIID riid, void **display_info );
    HRESULT (WINAPI *GetForMonitor)( IDisplayInformationInterop *iface, HMONITOR monitor, REFIID riid, void **display_info );
} IDisplayInformationInteropVtbl;
struct IDisplayInformationInterop { const IDisplayInformationInteropVtbl *lpVtbl; };

struct dummy_handler
{
    ITypedEventHandler_DisplayInformation_IInspectable ITypedEventHandler_DisplayInformation_IInspectable_iface;
    LONG ref;
};

static inline struct dummy_handler *impl_from_handler( ITypedEventHandler_DisplayInformation_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct dummy_handler, ITypedEventHandler_DisplayInformation_IInspectable_iface );
}

static HRESULT WINAPI handler_QueryInterface( ITypedEventHandler_DisplayInformation_IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ITypedEventHandler_DisplayInformation_IInspectable ))
    {
        *out = iface;
        ITypedEventHandler_DisplayInformation_IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef( ITypedEventHandler_DisplayInformation_IInspectable *iface )
{
    struct dummy_handler *impl = impl_from_handler( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI handler_Release( ITypedEventHandler_DisplayInformation_IInspectable *iface )
{
    struct dummy_handler *impl = impl_from_handler( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI handler_Invoke( ITypedEventHandler_DisplayInformation_IInspectable *iface,
        IDisplayInformation *sender, IInspectable *args )
{
    return S_OK;
}

static const ITypedEventHandler_DisplayInformation_IInspectableVtbl dummy_handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

#define check_interface( obj, iid, supported ) _check_interface( __LINE__, obj, iid, supported )
static void _check_interface( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *iface = obj, *unknown;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unknown );
    ok_(__FILE__, line)( hr == S_OK || (!supported && hr == E_NOINTERFACE), "Got unexpected hr %#lx.\n", hr );
    if (SUCCEEDED( hr ))
        IUnknown_Release( unknown );
}

static void test_DisplayInformationStatics(void)
{
    IDisplayInformationStatics *statics = NULL;
    IDisplayInformation *info = NULL;
    IDisplayInformation2 *info2 = NULL;
    IDisplayInformation4 *info4 = NULL;
    IDisplayInformation5 *info5 = NULL;
    IAdvancedColorInfo *aci = NULL;
    IActivationFactory *factory;
    struct dummy_handler handler = { {&dummy_handler_vtbl}, 1 };
    EventRegistrationToken tok1 = {0}, tok2 = {0};
    DisplayOrientations orient = DisplayOrientations_None;
    ResolutionScale scale = ResolutionScale_Invalid;
    FLOAT dpi = 0.0f, raw_x = 0.0f, raw_y = 0.0f;
    DOUBLE raw_px = 0.0;
    UINT32 sw = 0, sh = 0;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( RuntimeClass_Windows_Graphics_Display_DisplayInformation,
                              wcslen( RuntimeClass_Windows_Graphics_Display_DisplayInformation ), &str );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n",
                  wine_dbgstr_w(RuntimeClass_Windows_Graphics_Display_DisplayInformation) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IDisplayInformationStatics, TRUE );
    /* WinAppSDK C++/WinRT resolves an agile reference on every activation factory before using it. */
    check_interface( factory, &IID_IAgileObject, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IDisplayInformationStatics, (void **)&statics );
    ok( hr == S_OK, "QI IDisplayInformationStatics failed, hr %#lx\n", hr );

    hr = IDisplayInformationStatics_GetForCurrentView( statics, &info );
    ok( hr == S_OK && info != NULL, "GetForCurrentView failed, hr %#lx\n", hr );

    hr = IDisplayInformation_get_LogicalDpi( info, &dpi );
    ok( hr == S_OK && dpi > 0.0f, "get_LogicalDpi failed, hr %#lx, dpi %f\n", hr, dpi );
    hr = IDisplayInformation_get_RawDpiX( info, &raw_x );
    ok( hr == S_OK && raw_x > 0.0f, "get_RawDpiX failed, hr %#lx, raw_x %f\n", hr, raw_x );
    hr = IDisplayInformation_get_RawDpiY( info, &raw_y );
    ok( hr == S_OK && raw_y > 0.0f, "get_RawDpiY failed, hr %#lx, raw_y %f\n", hr, raw_y );
    hr = IDisplayInformation_get_ResolutionScale( info, &scale );
    ok( hr == S_OK && scale >= ResolutionScale_Scale100Percent, "get_ResolutionScale failed, hr %#lx, scale %d\n", hr, scale );
    hr = IDisplayInformation_get_CurrentOrientation( info, &orient );
    ok( hr == S_OK && orient != DisplayOrientations_None, "get_CurrentOrientation failed, hr %#lx\n", hr );
    hr = IDisplayInformation_get_NativeOrientation( info, &orient );
    ok( hr == S_OK && orient != DisplayOrientations_None, "get_NativeOrientation failed, hr %#lx\n", hr );

    hr = IDisplayInformation_QueryInterface( info, &IID_IDisplayInformation2, (void **)&info2 );
    ok( hr == S_OK, "QI IDisplayInformation2 failed, hr %#lx\n", hr );
    hr = IDisplayInformation2_get_RawPixelsPerViewPixel( info2, &raw_px );
    ok( hr == S_OK && raw_px > 0.0, "get_RawPixelsPerViewPixel failed, hr %#lx, val %f\n", hr, raw_px );
    ok( fabs( raw_px - (DOUBLE)dpi / 96.0 ) < 1e-6, "RawPixelsPerViewPixel %f != dpi/96 (%f)\n", raw_px, (DOUBLE)dpi / 96.0 );
    IDisplayInformation2_Release( info2 );

    hr = IDisplayInformation_QueryInterface( info, &IID_IDisplayInformation4, (void **)&info4 );
    ok( hr == S_OK, "QI IDisplayInformation4 failed, hr %#lx\n", hr );
    hr = IDisplayInformation4_get_ScreenWidthInRawPixels( info4, &sw );
    ok( hr == S_OK && sw > 0, "get_ScreenWidthInRawPixels failed, hr %#lx, sw %u\n", hr, sw );
    hr = IDisplayInformation4_get_ScreenHeightInRawPixels( info4, &sh );
    ok( hr == S_OK && sh > 0, "get_ScreenHeightInRawPixels failed, hr %#lx, sh %u\n", hr, sh );
    IDisplayInformation4_Release( info4 );

    hr = IDisplayInformation_QueryInterface( info, &IID_IDisplayInformation5, (void **)&info5 );
    ok( hr == S_OK, "QI IDisplayInformation5 failed, hr %#lx\n", hr );
    hr = IDisplayInformation5_GetAdvancedColorInfo( info5, &aci );
    ok( hr == S_OK && aci != NULL, "GetAdvancedColorInfo failed, hr %#lx\n", hr );
    if (aci) IAdvancedColorInfo_Release( aci );
    IDisplayInformation5_Release( info5 );

    hr = IDisplayInformation_add_DpiChanged( info, &handler.ITypedEventHandler_DisplayInformation_IInspectable_iface, &tok1 );
    ok( hr == S_OK && tok1.value != 0, "add_DpiChanged failed, hr %#lx, tok %I64x\n", hr, tok1.value );
    hr = IDisplayInformation_add_OrientationChanged( info, &handler.ITypedEventHandler_DisplayInformation_IInspectable_iface, &tok2 );
    ok( hr == S_OK && tok2.value != 0 && tok2.value != tok1.value, "add_OrientationChanged failed, hr %#lx\n", hr );
    ok( handler.ref == 3, "unexpected handler ref %ld\n", handler.ref );

    hr = IDisplayInformation_remove_DpiChanged( info, tok1 );
    ok( hr == S_OK, "remove_DpiChanged failed, hr %#lx\n", hr );
    hr = IDisplayInformation_remove_OrientationChanged( info, tok2 );
    ok( hr == S_OK, "remove_OrientationChanged failed, hr %#lx\n", hr );
    ok( handler.ref == 1, "unexpected handler ref %ld after remove\n", handler.ref );

    hr = IDisplayInformationStatics_put_AutoRotationPreferences( statics, DisplayOrientations_Landscape );
    ok( hr == S_OK, "put_AutoRotationPreferences failed, hr %#lx\n", hr );
    hr = IDisplayInformationStatics_get_AutoRotationPreferences( statics, &orient );
    ok( hr == S_OK && orient == DisplayOrientations_Landscape, "get_AutoRotationPreferences failed, hr %#lx\n", hr );

    IDisplayInformation_Release( info );
    IDisplayInformationStatics_Release( statics );

    /* Test IDisplayInformationInterop (GetForWindow & GetForMonitor) */
    {
        IDisplayInformationInterop *interop = NULL;
        IDisplayInformation *win_info = NULL, *mon_info = NULL;
        IDisplayInformation2 *win_info2 = NULL;
        IDisplayInformation4 *win_info4 = NULL;
        HWND hwnd;
        HMONITOR hmon;
        FLOAT win_dpi = 0.0f, mon_dpi = 0.0f;
        DOUBLE win_scale = 0.0;
        UINT32 win_w = 0, win_h = 0;
        ResolutionScale win_res_scale = ResolutionScale_Invalid;
        EventRegistrationToken win_tok = {0};

        HMODULE user32 = LoadLibraryW( L"user32.dll" );
        HWND (WINAPI *pCreateWindowExW)( DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID ) =
            user32 ? (void *)GetProcAddress( user32, "CreateWindowExW" ) : NULL;
        BOOL (WINAPI *pDestroyWindow)( HWND ) = user32 ? (void *)GetProcAddress( user32, "DestroyWindow" ) : NULL;
        HMONITOR (WINAPI *pMonitorFromWindow)( HWND, DWORD ) = user32 ? (void *)GetProcAddress( user32, "MonitorFromWindow" ) : NULL;

        hr = IActivationFactory_QueryInterface( factory, &IID_IDisplayInformationInterop, (void **)&interop );
        ok( hr == S_OK && interop != NULL, "QI IDisplayInformationInterop failed, hr %#lx\n", hr );
        if (SUCCEEDED( hr ) && pCreateWindowExW && pDestroyWindow && pMonitorFromWindow)
        {
            hr = interop->lpVtbl->GetForWindow( interop, NULL, &IID_IDisplayInformation, (void **)&win_info );
            ok( hr == E_INVALIDARG, "GetForWindow(NULL) got hr %#lx\n", hr );

            hwnd = pCreateWindowExW( 0, L"static", L"dpi_test", WS_POPUP, 0, 0, 100, 100, NULL, NULL, NULL, NULL );
            ok( hwnd != NULL, "CreateWindowExW failed\n" );
            if (hwnd)
            {
                hr = interop->lpVtbl->GetForWindow( interop, hwnd, &IID_IDisplayInformation, (void **)&win_info );
                ok( hr == S_OK && win_info != NULL, "GetForWindow failed, hr %#lx\n", hr );
                if (SUCCEEDED( hr ))
                {
                    hr = IDisplayInformation_get_LogicalDpi( win_info, &win_dpi );
                    ok( hr == S_OK && win_dpi >= 48.0f && win_dpi <= 480.0f, "win LogicalDpi hr %#lx dpi %f\n", hr, win_dpi );
                    hr = IDisplayInformation_get_ResolutionScale( win_info, &win_res_scale );
                    ok( hr == S_OK && win_res_scale >= ResolutionScale_Scale100Percent, "win ResolutionScale hr %#lx scale %d\n", hr, win_res_scale );

                    hr = IDisplayInformation_QueryInterface( win_info, &IID_IDisplayInformation2, (void **)&win_info2 );
                    ok( hr == S_OK, "QI win IDisplayInformation2 hr %#lx\n", hr );
                    hr = IDisplayInformation2_get_RawPixelsPerViewPixel( win_info2, &win_scale );
                    ok( hr == S_OK && fabs( win_scale - (DOUBLE)win_dpi / 96.0 ) < 1e-6,
                        "win RawPixelsPerViewPixel %f != %f\n", win_scale, (DOUBLE)win_dpi / 96.0 );
                    IDisplayInformation2_Release( win_info2 );

                    hr = IDisplayInformation_QueryInterface( win_info, &IID_IDisplayInformation4, (void **)&win_info4 );
                    ok( hr == S_OK, "QI win IDisplayInformation4 hr %#lx\n", hr );
                    hr = IDisplayInformation4_get_ScreenWidthInRawPixels( win_info4, &win_w );
                    ok( hr == S_OK && win_w > 0, "win ScreenWidthInRawPixels hr %#lx w %u\n", hr, win_w );
                    hr = IDisplayInformation4_get_ScreenHeightInRawPixels( win_info4, &win_h );
                    ok( hr == S_OK && win_h > 0, "win ScreenHeightInRawPixels hr %#lx h %u\n", hr, win_h );
                    IDisplayInformation4_Release( win_info4 );

                    hr = IDisplayInformation_add_DpiChanged( win_info, &handler.ITypedEventHandler_DisplayInformation_IInspectable_iface, &win_tok );
                    ok( hr == S_OK && win_tok.value != 0, "win add_DpiChanged hr %#lx\n", hr );
                    hr = IDisplayInformation_remove_DpiChanged( win_info, win_tok );
                    ok( hr == S_OK, "win remove_DpiChanged hr %#lx\n", hr );
                    ok( handler.ref == 1, "unexpected handler ref %ld after win remove\n", handler.ref );

                    IDisplayInformation_Release( win_info );
                }
                hmon = pMonitorFromWindow( hwnd, MONITOR_DEFAULTTOPRIMARY );
                ok( hmon != NULL, "MonitorFromWindow failed\n" );
                if (hmon)
                {
                    hr = interop->lpVtbl->GetForMonitor( interop, hmon, &IID_IDisplayInformation, (void **)&mon_info );
                    ok( hr == S_OK && mon_info != NULL, "GetForMonitor failed, hr %#lx\n", hr );
                    if (SUCCEEDED( hr ))
                    {
                        hr = IDisplayInformation_get_LogicalDpi( mon_info, &mon_dpi );
                        ok( hr == S_OK && mon_dpi >= 48.0f && mon_dpi <= 480.0f, "mon LogicalDpi hr %#lx dpi %f\n", hr, mon_dpi );
                        IDisplayInformation_Release( mon_info );
                    }
                }
                pDestroyWindow( hwnd );
            }
            interop->lpVtbl->Release( interop );
        }
    }

    ref = IActivationFactory_Release( factory );
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);
}

START_TEST(graphics)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_DisplayInformationStatics();

    RoUninitialize();
}
