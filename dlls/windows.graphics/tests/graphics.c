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
#define COBJMACROS
#include "initguid.h"
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#include "wine/test.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Graphics_Display
#include "windows.graphics.display.h"

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
