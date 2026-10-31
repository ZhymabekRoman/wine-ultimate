/*
 * Copyright 2023 Rémi Bernon for CodeWeavers
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
#include <stddef.h>

#include "windef.h"
#include "winbase.h"

#define COBJMACROS
#include "initguid.h"
#include "winstring.h"
#include "winternl.h"
#include "roapi.h"
#include "shobjidl_core.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Security_ExchangeActiveSyncProvisioning
#include "windows.security.exchangeactivesyncprovisioning.h"
#define WIDL_using_Windows_System_Power
#include "windows.system.power.h"
#define WIDL_using_Windows_System_Profile
#include "windows.system.profile.h"
#define WIDL_using_Windows_System_UserProfile
#include "windows.system.userprofile.h"
#define WIDL_using_Windows_UI_ViewManagement
#include "windows.ui.viewmanagement.h"
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"
#define WIDL_using_Windows_ApplicationModel_Core
#include "windows.applicationmodel.core.h"
#define WIDL_using_Windows_ApplicationModel_DataTransfer
#include "windows.applicationmodel.datatransfer.h"
#define WIDL_using_Windows_UI_Notifications
#include "windows.ui.notifications.h"

#include "wine/test.h"

#define check_interface( a, b, c ) check_interface_( __LINE__, a, b, c, FALSE )
static void check_interface_( unsigned int line, void *iface_ptr, REFIID iid, BOOL supported, BOOL is_broken )
{
    HRESULT hr, expected_hr, broken_hr;
    IUnknown *iface = iface_ptr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;
    broken_hr = supported ? E_NOINTERFACE : S_OK;
    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == expected_hr || broken( is_broken && hr == broken_hr ),
                         "got hr %#lx, expected %#lx.\n", hr, expected_hr );
    if (SUCCEEDED(hr)) IUnknown_Release( unk );
}

static void test_EasClientDeviceInformation(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_Security_ExchangeActiveSyncProvisioning_EasClientDeviceInformation;
    IEasClientDeviceInformation *client_device_information;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    /* WinAppSDK C++/WinRT resolves an agile reference on every activation factory before using it. */
    check_interface( factory, &IID_IAgileObject, TRUE );

    hr = IActivationFactory_ActivateInstance( factory, (IInspectable **)&client_device_information );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( client_device_information, &IID_IUnknown, TRUE );
    check_interface( client_device_information, &IID_IInspectable, TRUE );
    check_interface( client_device_information, &IID_IAgileObject, FALSE );

    hr = IEasClientDeviceInformation_get_OperatingSystem( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_FriendlyName( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_SystemManufacturer( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_SystemProductName( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_SystemSku( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    ref = IEasClientDeviceInformation_Release( client_device_information );
    ok( ref == 0, "got ref %ld.\n", ref );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_AnalyticsVersionInfo(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_System_Profile_AnalyticsInfo;
    IAnalyticsInfoStatics *analytics_info_statics;
    IAnalyticsVersionInfo *analytics_version_info;
    IActivationFactory *factory;
    HSTRING str, expect_str;
    DWORD revision, size;
    WCHAR buffer[32];
    UINT64 version;
    HRESULT hr;
    INT32 res;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IAnalyticsInfoStatics, (void **)&analytics_info_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IAnalyticsInfoStatics_get_VersionInfo( analytics_info_statics, &analytics_version_info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( analytics_version_info, &IID_IUnknown, TRUE );
    check_interface( analytics_version_info, &IID_IInspectable, TRUE );
    check_interface( analytics_version_info, &IID_IAgileObject, TRUE );

    hr = WindowsCreateString( L"Windows.Desktop", wcslen( L"Windows.Desktop" ), &expect_str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAnalyticsVersionInfo_get_DeviceFamily( analytics_version_info, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCompareStringOrdinal( str, expect_str, &res );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !res, "got unexpected string %s.\n", debugstr_hstring(str) );
    WindowsDeleteString( str );
    WindowsDeleteString( expect_str );

    size = sizeof(revision);
    if (RegGetValueW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                      RRF_RT_REG_DWORD, NULL, &revision, &size ))
        revision = 0;
    version = NtCurrentTeb()->Peb->OSMajorVersion & 0xffff;
    version = (version << 16) | (NtCurrentTeb()->Peb->OSMinorVersion & 0xffff);
    version = (version << 16) | (NtCurrentTeb()->Peb->OSBuildNumber & 0xffff);
    version = (version << 16) | revision;

    res = swprintf( buffer, ARRAY_SIZE(buffer), L"%I64u", version );
    hr = WindowsCreateString( buffer, res, &expect_str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAnalyticsVersionInfo_get_DeviceFamilyVersion( analytics_version_info, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCompareStringOrdinal( str, expect_str, &res );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !res || broken(revision == 0) /* Win11 */, "got unexpected string %s.\n", debugstr_hstring(str) );
    WindowsDeleteString( str );
    WindowsDeleteString( expect_str );

    ref = IAnalyticsVersionInfo_Release( analytics_version_info );
    ok( ref == 0, "got ref %ld.\n", ref );

    ref = IAnalyticsInfoStatics_Release( analytics_info_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_AdvertisingManager(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_System_UserProfile_AdvertisingManager;
    IAdvertisingManagerStatics *advertising_manager_statics;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface_( __LINE__, factory, &IID_IAgileObject, TRUE, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IAdvertisingManagerStatics, (void **)&advertising_manager_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IAdvertisingManagerStatics_get_AdvertisingId( advertising_manager_statics, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    ref = IAdvertisingManagerStatics_Release( advertising_manager_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_ApplicationView(void)
{
    static const GUID IID_IApplicationView_alt =
        {0xd222d3c7, 0x71a8, 0x495d, {0x95, 0x46, 0x04, 0x33, 0x0f, 0xb3, 0x3d, 0x76}};
    static const GUID IID_IApplicationView3_alt =
        {0x907f6222, 0xdf3d, 0x4794, {0x98, 0x76, 0x1f, 0x42, 0xe5, 0x87, 0xb2, 0xe1}};
    static const GUID IID_IApplicationViewTitleBar_alt =
        {0x00924509, 0x5480, 0x4cf8, {0xac, 0x71, 0x71, 0x22, 0xdd, 0x51, 0x8c, 0x77}};
    static const WCHAR *class_name = RuntimeClass_Windows_UI_ViewManagement_ApplicationView;
    IApplicationViewStatics2 *app_view_statics2;
    IApplicationViewTitleBar *title_bar, *title_bar2;
    IApplicationView *app_view, *app_view_check;
    IApplicationView2 *app_view2;
    IApplicationView3 *app_view3;
    IActivationFactory *factory;
    __FIReference_1_Color *color;
    boolean overlays = TRUE;
    boolean fullscreen = TRUE;
    HSTRING title = NULL;
    HSTRING str;
    INT32 id = 0;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IApplicationViewStatics, TRUE );
    check_interface( factory, &IID_IApplicationViewStatics2, TRUE );

    /* Test IApplicationViewStatics2 */
    hr = IActivationFactory_QueryInterface( factory, &IID_IApplicationViewStatics2,
                                            (void **)&app_view_statics2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IApplicationViewStatics2_GetForCurrentView( app_view_statics2, &app_view );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( app_view != NULL, "got NULL app_view\n" );

    if (SUCCEEDED( hr ) && app_view)
    {
        check_interface( app_view, &IID_IUnknown, TRUE );
        check_interface( app_view, &IID_IInspectable, TRUE );
        check_interface( app_view, &IID_IAgileObject, TRUE );
        check_interface( app_view, &IID_IApplicationView, TRUE );
        check_interface( app_view, &IID_IApplicationView_alt, TRUE );
        check_interface( app_view, &IID_IApplicationView2, TRUE );
        check_interface( app_view, &IID_IApplicationView3, TRUE );
        check_interface( app_view, &IID_IApplicationView3_alt, TRUE );

        /* Test singleton behavior */
        hr = IApplicationViewStatics2_GetForCurrentView( app_view_statics2, &app_view_check );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( app_view_check == app_view, "expected same singleton instance %p != %p\n", app_view_check, app_view );
        IApplicationView_Release( app_view_check );

        /* Test IApplicationView properties */
        hr = IApplicationView_get_Id( app_view, &id );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( id == 1, "got id %d\n", (int)id );

        hr = IApplicationView_get_IsFullScreen( app_view, &fullscreen );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( fullscreen == FALSE, "got fullscreen %d\n", fullscreen );

        hr = IApplicationView_get_Title( app_view, &title );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (title) WindowsDeleteString( title );

        /* Test IApplicationView2 */
        hr = IApplicationView_QueryInterface( app_view, &IID_IApplicationView2, (void **)&app_view2 );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (SUCCEEDED( hr ))
        {
            hr = IApplicationView2_get_SuppressSystemOverlays( app_view2, &overlays );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            ok( overlays == FALSE, "got overlays %d\n", overlays );

            hr = IApplicationView2_put_SuppressSystemOverlays( app_view2, TRUE );
            ok( hr == S_OK, "got hr %#lx.\n", hr );

            hr = IApplicationView2_get_SuppressSystemOverlays( app_view2, &overlays );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            ok( overlays == TRUE, "got overlays %d\n", overlays );

            IApplicationView2_Release( app_view2 );
        }

        /* Test IApplicationView3 */
        hr = IApplicationView_QueryInterface( app_view, &IID_IApplicationView3, (void **)&app_view3 );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (SUCCEEDED( hr ))
        {
            title_bar = NULL;
            hr = IApplicationView3_get_TitleBar( app_view3, &title_bar );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            ok( title_bar != NULL, "got NULL title_bar\n" );

            if (SUCCEEDED( hr ) && title_bar)
            {
                check_interface( title_bar, &IID_IUnknown, TRUE );
                check_interface( title_bar, &IID_IInspectable, TRUE );
                check_interface( title_bar, &IID_IAgileObject, TRUE );
                check_interface( title_bar, &IID_IApplicationViewTitleBar, TRUE );
                check_interface( title_bar, &IID_IApplicationViewTitleBar_alt, TRUE );

                color = (void *)0xdeadbeef;
                hr = IApplicationViewTitleBar_get_ForegroundColor( title_bar, &color );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                ok( color == NULL, "got non-NULL color %p\n", color );

                color = (void *)0xdeadbeef;
                hr = IApplicationViewTitleBar_get_BackgroundColor( title_bar, &color );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                ok( color == NULL, "got non-NULL color %p\n", color );

                color = (void *)0xdeadbeef;
                hr = IApplicationViewTitleBar_get_ButtonForegroundColor( title_bar, &color );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                ok( color == NULL, "got non-NULL color %p\n", color );

                color = (void *)0xdeadbeef;
                hr = IApplicationViewTitleBar_get_ButtonBackgroundColor( title_bar, &color );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                ok( color == NULL, "got non-NULL color %p\n", color );

                /* Test cached title bar */
                title_bar2 = NULL;
                hr = IApplicationView3_get_TitleBar( app_view3, &title_bar2 );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                ok( title_bar2 == title_bar, "expected same title bar instance %p != %p\n", title_bar2, title_bar );
                IApplicationViewTitleBar_Release( title_bar2 );

                IApplicationViewTitleBar_Release( title_bar );
            }

            IApplicationView3_Release( app_view3 );
        }

        IApplicationView_Release( app_view );
    }

    IApplicationViewStatics2_Release( app_view_statics2 );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_CoreApplication(void)
{
    static const GUID IID_ICoreApplicationView2_custom =
        {0x68eb7adf, 0x917f, 0x4885, {0x9e, 0x78, 0x81, 0xb3, 0x7b, 0x37, 0xf4, 0x60}};
    static const GUID IID_ICoreApplicationView3_custom =
        {0x07ebe1b3, 0xa4cf, 0x4550, {0xab, 0x70, 0xb0, 0x7e, 0x85, 0x33, 0x0b, 0xc8}};
    static const GUID IID_ICoreApplicationViewTitleBar_custom =
        {0x08d17364, 0x4e43, 0x40e2, {0xa4, 0x70, 0x36, 0x64, 0xf0, 0xe0, 0xb9, 0x6c}};
    static const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_Core_CoreApplication;
    IActivationFactory *factory;
    ICoreApplication *core_app;
    ICoreApplicationView *view;
    ICoreApplicationView2 *view2;
    ICoreApplicationView3 *view3;
    ICoreApplicationViewTitleBar *title_bar, *title_bar2;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_ICoreApplication, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_ICoreApplication, (void **)&core_app );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED( hr ))
    {
        view = NULL;
        hr = ICoreApplication_GetCurrentView( core_app, &view );
        ok( hr == S_OK, "GetCurrentView failed, hr %#lx\n", hr );
        ok( view != NULL, "got NULL view\n" );

        if (SUCCEEDED( hr ) && view)
        {
            check_interface( view, &IID_IUnknown, TRUE );
            check_interface( view, &IID_IInspectable, TRUE );
            check_interface( view, &IID_IAgileObject, TRUE );
            check_interface( view, &IID_ICoreApplicationView, TRUE );
            check_interface( view, &IID_ICoreApplicationView2, TRUE );
            check_interface( view, &IID_ICoreApplicationView2_custom, TRUE );
            check_interface( view, &IID_ICoreApplicationView3, TRUE );
            check_interface( view, &IID_ICoreApplicationView3_custom, TRUE );

            view2 = NULL;
            hr = ICoreApplicationView_QueryInterface( view, &IID_ICoreApplicationView2, (void **)&view2 );
            ok( hr == S_OK, "QI ICoreApplicationView2 failed, hr %#lx\n", hr );
            if (SUCCEEDED( hr ) && view2)
            {
                __x_ABI_CWindows_CUI_CCore_CICoreDispatcher *disp = NULL;
                hr = ICoreApplicationView2_get_Dispatcher( view2, &disp );
                ok( hr == S_OK || hr == E_FAIL, "get_Dispatcher returned hr %#lx\n", hr );
                if (SUCCEEDED( hr ) && disp)
                    IUnknown_Release( (IUnknown *)disp );
                ICoreApplicationView2_Release( view2 );
            }

            view3 = NULL;
            hr = ICoreApplicationView_QueryInterface( view, &IID_ICoreApplicationView3, (void **)&view3 );
            ok( hr == S_OK, "QI ICoreApplicationView3 failed, hr %#lx\n", hr );
            if (SUCCEEDED( hr ) && view3)
            {
                boolean is_comp = TRUE;
                hr = ICoreApplicationView3_get_IsComponent( view3, &is_comp );
                ok( hr == S_OK, "get_IsComponent failed, hr %#lx\n", hr );
                ok( is_comp == FALSE, "got is_comp %d\n", is_comp );

                title_bar = NULL;
                hr = ICoreApplicationView3_get_TitleBar( view3, &title_bar );
                ok( hr == S_OK, "get_TitleBar failed, hr %#lx\n", hr );
                ok( title_bar != NULL, "got NULL title_bar\n" );

                if (SUCCEEDED( hr ) && title_bar)
                {
                    boolean extend = TRUE;
                    DOUBLE left = -1.0, right = -1.0, height = 0.0;
                    boolean is_visible = FALSE;
                    EventRegistrationToken token = {0};

                    check_interface( title_bar, &IID_IUnknown, TRUE );
                    check_interface( title_bar, &IID_IInspectable, TRUE );
                    check_interface( title_bar, &IID_IAgileObject, TRUE );
                    check_interface( title_bar, &IID_ICoreApplicationViewTitleBar, TRUE );
                    check_interface( title_bar, &IID_ICoreApplicationViewTitleBar_custom, TRUE );

                    hr = ICoreApplicationViewTitleBar_get_ExtendViewIntoTitleBar( title_bar, &extend );
                    ok( hr == S_OK, "get_ExtendViewIntoTitleBar failed, hr %#lx\n", hr );
                    ok( extend == FALSE, "got extend %d\n", extend );

                    hr = ICoreApplicationViewTitleBar_put_ExtendViewIntoTitleBar( title_bar, TRUE );
                    ok( hr == S_OK, "put_ExtendViewIntoTitleBar failed, hr %#lx\n", hr );

                    hr = ICoreApplicationViewTitleBar_get_ExtendViewIntoTitleBar( title_bar, &extend );
                    ok( hr == S_OK, "get_ExtendViewIntoTitleBar failed, hr %#lx\n", hr );
                    ok( extend == TRUE, "got extend %d\n", extend );

                    hr = ICoreApplicationViewTitleBar_get_SystemOverlayLeftInset( title_bar, &left );
                    ok( hr == S_OK, "get_SystemOverlayLeftInset failed, hr %#lx\n", hr );
                    ok( left == 0.0, "got left inset %f\n", left );

                    hr = ICoreApplicationViewTitleBar_get_SystemOverlayRightInset( title_bar, &right );
                    ok( hr == S_OK, "get_SystemOverlayRightInset failed, hr %#lx\n", hr );
                    ok( right == 0.0, "got right inset %f\n", right );

                    hr = ICoreApplicationViewTitleBar_get_Height( title_bar, &height );
                    ok( hr == S_OK, "get_Height failed, hr %#lx\n", hr );
                    ok( height == 32.0, "got height %f\n", height );

                    hr = ICoreApplicationViewTitleBar_get_IsVisible( title_bar, &is_visible );
                    ok( hr == S_OK, "get_IsVisible failed, hr %#lx\n", hr );
                    ok( is_visible == TRUE, "got is_visible %d\n", is_visible );

                    hr = ICoreApplicationViewTitleBar_add_IsVisibleChanged( title_bar, NULL, &token );
                    ok( hr == S_OK, "add_IsVisibleChanged failed, hr %#lx\n", hr );
                    hr = ICoreApplicationViewTitleBar_remove_IsVisibleChanged( title_bar, token );
                    ok( hr == S_OK, "remove_IsVisibleChanged failed, hr %#lx\n", hr );

                    /* Verify cached title bar */
                    title_bar2 = NULL;
                    hr = ICoreApplicationView3_get_TitleBar( view3, &title_bar2 );
                    ok( hr == S_OK, "get_TitleBar (2) failed, hr %#lx\n", hr );
                    ok( title_bar2 == title_bar, "expected same title bar instance %p != %p\n", title_bar2, title_bar );
                    if (title_bar2) ICoreApplicationViewTitleBar_Release( title_bar2 );

                    ICoreApplicationViewTitleBar_Release( title_bar );
                }

                ICoreApplicationView3_Release( view3 );
            }

            ICoreApplicationView_Release( view );
        }

        ICoreApplication_Release( core_app );
    }

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_DataTransferManager(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IDataTransferManagerInterop, TRUE );
    check_interface( factory, &IID_IDataTransferManagerStatics, TRUE );

    IActivationFactory_Release( factory );
}

static LONG event_handler_invoke_count;

struct event_handler
{
    __FIEventHandler_1_IInspectable IEventHandler_iface;
    LONG ref;
};

static HRESULT WINAPI event_handler_QueryInterface( __FIEventHandler_1_IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIEventHandler_1_IInspectable ))
    {
        *out = iface;
        __FIEventHandler_1_IInspectable_AddRef( iface );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI event_handler_AddRef( __FIEventHandler_1_IInspectable *iface )
{
    struct event_handler *impl = CONTAINING_RECORD( iface, struct event_handler, IEventHandler_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI event_handler_Release( __FIEventHandler_1_IInspectable *iface )
{
    struct event_handler *impl = CONTAINING_RECORD( iface, struct event_handler, IEventHandler_iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI event_handler_Invoke( __FIEventHandler_1_IInspectable *iface, IInspectable *sender, IInspectable *args )
{
    InterlockedIncrement( &event_handler_invoke_count );
    return S_OK;
}

static const struct __FIEventHandler_1_IInspectableVtbl event_handler_vtbl =
{
    event_handler_QueryInterface,
    event_handler_AddRef,
    event_handler_Release,
    event_handler_Invoke,
};

static struct event_handler event_handler = { { &event_handler_vtbl }, 1 };

static void test_PowerManager(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_System_Power_PowerManager;
    IPowerManagerStatics *power_manager_statics;
    IInspectable *instance;
    IActivationFactory *factory;
    EventRegistrationToken tokens[5], token;
    EnergySaverStatus saver_status;
    PowerSupplyStatus supply_status;
    BatteryStatus battery_status;
    INT32 remaining_percent;
    HSTRING str;
    TimeSpan timespan;
    HRESULT hr;
    LONG ref;
    unsigned int i, j;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IPowerManagerStatics, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IPowerManagerStatics, (void **)&power_manager_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( power_manager_statics, &IID_IUnknown, TRUE );
    check_interface( power_manager_statics, &IID_IInspectable, TRUE );
    check_interface( power_manager_statics, &IID_IAgileObject, TRUE );
    check_interface( power_manager_statics, &IID_IPowerManagerStatics, TRUE );
    check_interface( power_manager_statics, &IID_IActivationFactory, TRUE );

    instance = (IInspectable *)0xdeadbeef;
    hr = IActivationFactory_ActivateInstance( factory, &instance );
    ok( hr == E_NOTIMPL || broken( hr == E_FAIL ), "got hr %#lx.\n", hr );
    ok( !instance, "got instance %p.\n", instance );

    battery_status = (BatteryStatus) -1;
    hr = IPowerManagerStatics_get_BatteryStatus( power_manager_statics, &battery_status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( battery_status == BatteryStatus_NotPresent || broken( battery_status != BatteryStatus_NotPresent ),
        "got battery status %d.\n", battery_status );

    supply_status = (PowerSupplyStatus) -1;
    hr = IPowerManagerStatics_get_PowerSupplyStatus( power_manager_statics, &supply_status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( supply_status == PowerSupplyStatus_Adequate || broken( supply_status != PowerSupplyStatus_Adequate ),
        "got power supply status %d.\n", supply_status );

    remaining_percent = -1;
    hr = IPowerManagerStatics_get_RemainingChargePercent( power_manager_statics, &remaining_percent );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( remaining_percent == 100 || broken( remaining_percent != 100 ), "got remaining percent %d.\n", remaining_percent );

    saver_status = (EnergySaverStatus) -1;
    hr = IPowerManagerStatics_get_EnergySaverStatus( power_manager_statics, &saver_status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( saver_status == EnergySaverStatus_Off || broken( saver_status != EnergySaverStatus_Off ),
        "got energy saver status %d.\n", saver_status );

    timespan.Duration = 0;
    hr = IPowerManagerStatics_get_RemainingDischargeTime( power_manager_statics, &timespan );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( timespan.Duration == -1 || broken( timespan.Duration != -1 ), "got remaining discharge time %s.\n",
        wine_dbgstr_longlong( timespan.Duration ) );

    hr = IPowerManagerStatics_get_BatteryStatus( power_manager_statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_get_PowerSupplyStatus( power_manager_statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_get_RemainingChargePercent( power_manager_statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_get_EnergySaverStatus( power_manager_statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_get_RemainingDischargeTime( power_manager_statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    hr = IPowerManagerStatics_add_BatteryStatusChanged( power_manager_statics, &event_handler.IEventHandler_iface, &tokens[0] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_add_EnergySaverStatusChanged( power_manager_statics, &event_handler.IEventHandler_iface, &tokens[1] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_add_PowerSupplyStatusChanged( power_manager_statics, &event_handler.IEventHandler_iface, &tokens[2] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_add_RemainingChargePercentChanged( power_manager_statics, &event_handler.IEventHandler_iface, &tokens[3] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_add_RemainingDischargeTimeChanged( power_manager_statics, &event_handler.IEventHandler_iface, &tokens[4] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    for (i = 0; i < ARRAY_SIZE( tokens ); ++i)
        ok( tokens[i].value != 0, "got token value %s.\n", wine_dbgstr_longlong( tokens[i].value ) );
    for (i = 0; i < ARRAY_SIZE( tokens ); ++i)
        for (j = i + 1; j < ARRAY_SIZE( tokens ); ++j)
            ok( tokens[i].value != tokens[j].value, "got duplicated token value %s.\n",
                wine_dbgstr_longlong( tokens[i].value ) );

    ok( event_handler.ref == 6, "got handler ref %ld.\n", event_handler.ref );

    token.value = 0;
    hr = IPowerManagerStatics_remove_BatteryStatusChanged( power_manager_statics, token );
    ok( hr == E_INVALIDARG || broken( hr != E_INVALIDARG ), "got hr %#lx.\n", hr );

    hr = IPowerManagerStatics_add_BatteryStatusChanged( power_manager_statics, NULL, &token );
    ok( hr == E_POINTER || broken( hr != E_POINTER ), "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_add_BatteryStatusChanged( power_manager_statics, &event_handler.IEventHandler_iface, NULL );
    ok( hr == E_POINTER || broken( hr != E_POINTER ), "got hr %#lx.\n", hr );

    hr = IPowerManagerStatics_remove_BatteryStatusChanged( power_manager_statics, tokens[0] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_remove_EnergySaverStatusChanged( power_manager_statics, tokens[1] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_remove_PowerSupplyStatusChanged( power_manager_statics, tokens[2] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_remove_RemainingChargePercentChanged( power_manager_statics, tokens[3] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IPowerManagerStatics_remove_RemainingDischargeTimeChanged( power_manager_statics, tokens[4] );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IPowerManagerStatics_remove_BatteryStatusChanged( power_manager_statics, tokens[0] );
    ok( hr == S_OK || broken( hr != S_OK ), "got hr %#lx.\n", hr );

    ok( event_handler.ref == 1, "got handler ref %ld.\n", event_handler.ref );
    ok( !event_handler_invoke_count, "got invoke count %ld.\n", event_handler_invoke_count );

    ref = IPowerManagerStatics_Release( power_manager_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_ToastNotificationManager(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_UI_Notifications_ToastNotificationManager;
    static const WCHAR *id = L"wine.twinapi.test";
    __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *scheduled;
    __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iterable;
    __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iterator;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *notifier;
    IToastNotificationManagerStatics *toast_notification_manager_statics;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IToastNotificationManagerStatics,
                                            (void **)&toast_notification_manager_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( id, wcslen( id ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IToastNotificationManagerStatics_CreateToastNotifierWithId( toast_notification_manager_statics, str, &notifier );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_GetScheduledToastNotifications( notifier, &scheduled );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* the scheduled set is enumerable: the closed pinterface instantiations are answered */
    check_interface( scheduled, &IID___FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification, TRUE );
    check_interface( scheduled, &IID___FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification, TRUE );

    hr = __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_QueryInterface(
            scheduled, &IID___FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification, (void **)&iterable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_First( iterable, &iterator );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* the set is empty: an exhausted iterator, an empty view */
    {
        BOOLEAN has_current = TRUE, moved = TRUE;
        UINT32 size = 0xc0ffee, count = 0xc0ffee;

        hr = __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_get_HasCurrent( iterator,
                                                                                                      &has_current );
        ok( hr == S_OK && !has_current, "got hr %#lx, has_current %d.\n", hr, has_current );
        hr = __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_MoveNext( iterator, &moved );
        ok( hr == S_OK && !moved, "got hr %#lx, moved %d.\n", hr, moved );
        hr = __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_GetMany( iterator, 0, NULL, &count );
        ok( hr == S_OK && !count, "got hr %#lx, count %u.\n", hr, count );
        hr = __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_get_Current( iterator, NULL );
        ok( hr == E_POINTER, "got hr %#lx.\n", hr );

        hr = __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_get_Size( scheduled, &size );
        ok( hr == S_OK && !size, "got hr %#lx, size %u.\n", hr, size );
        hr = __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_GetAt( scheduled, 0, NULL );
        ok( hr == E_BOUNDS || hr == E_POINTER, "got hr %#lx.\n", hr );
    }

    ref = __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_Release( iterator );
    ok( !ref, "got ref %ld.\n", ref );
    ref = __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_Release( iterable );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_Release( scheduled );
    ok( !ref, "got ref %ld.\n", ref );
    ref = __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_Release( notifier );
    ok( !ref, "got ref %ld.\n", ref );

    ref = IToastNotificationManagerStatics_Release( toast_notification_manager_statics );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref >= 1, "got ref %ld.\n", ref );
}

START_TEST(twinapi)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_EasClientDeviceInformation();
    test_AnalyticsVersionInfo();
    test_AdvertisingManager();
    test_ApplicationView();
    test_CoreApplication();
    test_DataTransferManager();
    test_PowerManager();
    test_ToastNotificationManager();

    RoUninitialize();
}
