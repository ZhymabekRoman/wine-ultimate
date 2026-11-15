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
#include "winuser.h"

#define COBJMACROS
#include "initguid.h"
#include "winstring.h"
#include "winternl.h"
#include "roapi.h"
#include "weakreference.h"
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
#define WIDL_using_Windows_Data_Xml_Dom
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

struct test_lifecycle_handler
{
    void *lpVtbl;
    LONG ref;
    LONG count;
    BOOL try_deferral;
    BOOL deferral_ok;
    HANDLE event;
};

static HRESULT WINAPI test_lifecycle_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        InterlockedIncrement( &((struct test_lifecycle_handler *)iface)->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI test_lifecycle_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct test_lifecycle_handler *)iface)->ref );
}

static ULONG WINAPI test_lifecycle_Release( void *iface )
{
    return InterlockedDecrement( &((struct test_lifecycle_handler *)iface)->ref );
}

static HRESULT WINAPI test_leaving_Invoke(
        __FIEventHandler_1_Windows__CApplicationModel__CLeavingBackgroundEventArgs *iface,
        IInspectable *sender, __x_ABI_CWindows_CApplicationModel_CILeavingBackgroundEventArgs *args )
{
    struct test_lifecycle_handler *impl = (struct test_lifecycle_handler *)iface;
    __x_ABI_CWindows_CFoundation_CIDeferral *def = NULL;
    HRESULT hr;

    if (impl->try_deferral && args)
    {
        hr = __x_ABI_CWindows_CApplicationModel_CILeavingBackgroundEventArgs_GetDeferral( args, &def );
        if (SUCCEEDED( hr ) && def)
        {
            hr = __x_ABI_CWindows_CFoundation_CIDeferral_Complete( def );
            impl->deferral_ok = SUCCEEDED( hr );
            __x_ABI_CWindows_CFoundation_CIDeferral_Release( def );
        }
    }
    InterlockedIncrement( &impl->count );
    SetEvent( impl->event );
    return S_OK;
}

static HRESULT WINAPI test_entered_Invoke(
        __FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs *iface,
        IInspectable *sender, __x_ABI_CWindows_CApplicationModel_CIEnteredBackgroundEventArgs *args )
{
    struct test_lifecycle_handler *impl = (struct test_lifecycle_handler *)iface;
    __x_ABI_CWindows_CFoundation_CIDeferral *def = NULL;
    HRESULT hr;

    if (impl->try_deferral && args)
    {
        hr = __x_ABI_CWindows_CApplicationModel_CIEnteredBackgroundEventArgs_GetDeferral( args, &def );
        if (SUCCEEDED( hr ) && def)
        {
            __x_ABI_CWindows_CFoundation_CIClosable *closable = NULL;
            hr = __x_ABI_CWindows_CFoundation_CIDeferral_Complete( def );
            impl->deferral_ok = SUCCEEDED( hr );
            if (SUCCEEDED( __x_ABI_CWindows_CFoundation_CIDeferral_QueryInterface( def,
                    &IID___x_ABI_CWindows_CFoundation_CIClosable, (void **)&closable ) ))
            {
                hr = __x_ABI_CWindows_CFoundation_CIClosable_Close( closable );
                impl->deferral_ok = impl->deferral_ok && SUCCEEDED( hr );
                __x_ABI_CWindows_CFoundation_CIClosable_Release( closable );
            }
            __x_ABI_CWindows_CFoundation_CIDeferral_Release( def );
        }
    }
    InterlockedIncrement( &impl->count );
    SetEvent( impl->event );
    return S_OK;
}

static HRESULT WINAPI test_resuming_Invoke( __FIEventHandler_1_IInspectable *iface,
        IInspectable *sender, IInspectable *args )
{
    struct test_lifecycle_handler *impl = (struct test_lifecycle_handler *)iface;
    InterlockedIncrement( &impl->count );
    SetEvent( impl->event );
    return S_OK;
}

static const void *test_leaving_vtbl[] =
{
    test_lifecycle_QueryInterface, test_lifecycle_AddRef, test_lifecycle_Release, test_leaving_Invoke,
};
static const void *test_entered_vtbl[] =
{
    test_lifecycle_QueryInterface, test_lifecycle_AddRef, test_lifecycle_Release, test_entered_Invoke,
};
static const void *test_resuming_vtbl[] =
{
    test_lifecycle_QueryInterface, test_lifecycle_AddRef, test_lifecycle_Release, test_resuming_Invoke,
};

static void pump_until_event( HANDLE event, DWORD timeout_ms,
        BOOL (WINAPI *pPeekMessageW)(MSG *, HWND, UINT, UINT, UINT),
        BOOL (WINAPI *pTranslateMessage)(const MSG *),
        LRESULT (WINAPI *pDispatchMessageW)(const MSG *),
        DWORD (WINAPI *pMsgWaitForMultipleObjects)(DWORD, const HANDLE *, BOOL, DWORD, DWORD) )
{
    DWORD deadline = GetTickCount() + timeout_ms;
    for (;;)
    {
        MSG msg;
        DWORD rem;
        while (pPeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            pTranslateMessage( &msg );
            pDispatchMessageW( &msg );
        }
        if (WaitForSingleObject( event, 0 ) == WAIT_OBJECT_0) break;
        rem = deadline > GetTickCount() ? deadline - GetTickCount() : 0;
        if (!rem) break;
        pMsgWaitForMultipleObjects( 1, &event, FALSE, rem, QS_ALLINPUT );
    }
}

static void test_CoreApplication_lifecycle(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_Core_CoreApplication;
    HMODULE user32 = LoadLibraryA( "user32.dll" );
    ATOM (WINAPI *pRegisterClassW)(const WNDCLASSW *);
    HWND (WINAPI *pCreateWindowExW)(DWORD, const WCHAR *, const WCHAR *, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void *);
    BOOL (WINAPI *pDestroyWindow)(HWND);
    BOOL (WINAPI *pShowWindow)(HWND, int);
    BOOL (WINAPI *pSetForegroundWindow)(HWND);
    BOOL (WINAPI *pPeekMessageW)(MSG *, HWND, UINT, UINT, UINT);
    BOOL (WINAPI *pTranslateMessage)(const MSG *);
    LRESULT (WINAPI *pDispatchMessageW)(const MSG *);
    LRESULT (WINAPI *pDefWindowProcW)(HWND, UINT, WPARAM, LPARAM);
    DWORD (WINAPI *pMsgWaitForMultipleObjects)(DWORD, const HANDLE *, BOOL, DWORD, DWORD);
    IActivationFactory *factory = NULL;
    ICoreApplication *core_app = NULL;
    ICoreApplication2 *core_app2 = NULL;
    EventRegistrationToken tok_leaving = {0}, tok_entered = {0}, tok_resuming = {0};
    struct test_lifecycle_handler leaving_h = { test_leaving_vtbl, 1, 0, TRUE, FALSE, NULL };
    struct test_lifecycle_handler entered_h = { test_entered_vtbl, 1, 0, TRUE, FALSE, NULL };
    struct test_lifecycle_handler resuming_h = { test_resuming_vtbl, 1, 0, FALSE, FALSE, NULL };
    WNDCLASSW wc = {0};
    HWND hwnd;
    HSTRING str;
    HRESULT hr;

    ok( user32 != NULL, "LoadLibrary user32 failed\n" );
    pRegisterClassW = (void *)GetProcAddress( user32, "RegisterClassW" );
    pCreateWindowExW = (void *)GetProcAddress( user32, "CreateWindowExW" );
    pDestroyWindow = (void *)GetProcAddress( user32, "DestroyWindow" );
    pShowWindow = (void *)GetProcAddress( user32, "ShowWindow" );
    pSetForegroundWindow = (void *)GetProcAddress( user32, "SetForegroundWindow" );
    pPeekMessageW = (void *)GetProcAddress( user32, "PeekMessageW" );
    pTranslateMessage = (void *)GetProcAddress( user32, "TranslateMessage" );
    pDispatchMessageW = (void *)GetProcAddress( user32, "DispatchMessageW" );
    pDefWindowProcW = (void *)GetProcAddress( user32, "DefWindowProcW" );
    pMsgWaitForMultipleObjects = (void *)GetProcAddress( user32, "MsgWaitForMultipleObjects" );

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (FAILED( hr ))
    {
        FreeLibrary( user32 );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_ICoreApplication, (void **)&core_app );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = IActivationFactory_QueryInterface( factory, &IID_ICoreApplication2, (void **)&core_app2 );
    ok( hr == S_OK, "got hr %#lx\n", hr );

    leaving_h.event = CreateEventW( NULL, TRUE, FALSE, NULL );
    entered_h.event = CreateEventW( NULL, TRUE, FALSE, NULL );
    resuming_h.event = CreateEventW( NULL, TRUE, FALSE, NULL );

    hr = ICoreApplication2_add_LeavingBackground( core_app2,
            (__FIEventHandler_1_Windows__CApplicationModel__CLeavingBackgroundEventArgs *)&leaving_h, &tok_leaving );
    ok( hr == S_OK && tok_leaving.value != 0, "add_LeavingBackground hr %#lx, tok %I64d\n", hr, tok_leaving.value );

    hr = ICoreApplication2_add_EnteredBackground( core_app2,
            (__FIEventHandler_1_Windows__CApplicationModel__CEnteredBackgroundEventArgs *)&entered_h, &tok_entered );
    ok( hr == S_OK && tok_entered.value != 0, "add_EnteredBackground hr %#lx, tok %I64d\n", hr, tok_entered.value );

    hr = ICoreApplication_add_Resuming( core_app,
            (__FIEventHandler_1_IInspectable *)&resuming_h, &tok_resuming );
    ok( hr == S_OK && tok_resuming.value != 0, "add_Resuming hr %#lx, tok %I64d\n", hr, tok_resuming.value );

    wc.lpfnWndProc = pDefWindowProcW;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"WineTwinapiLifecycleTest";
    pRegisterClassW( &wc );

    hwnd = pCreateWindowExW( 0, wc.lpszClassName, L"LifecycleTest", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 240, 200, NULL, NULL, wc.hInstance, NULL );
    ok( hwnd != NULL, "CreateWindowExW failed\n" );
    pShowWindow( hwnd, SW_SHOWNORMAL );
    pSetForegroundWindow( hwnd );

    pump_until_event( leaving_h.event, 3000, pPeekMessageW, pTranslateMessage, pDispatchMessageW, pMsgWaitForMultipleObjects );
    pump_until_event( resuming_h.event, 1000, pPeekMessageW, pTranslateMessage, pDispatchMessageW, pMsgWaitForMultipleObjects );
    ok( leaving_h.count >= 1, "LeavingBackground did not fire\n" );
    ok( leaving_h.deferral_ok, "LeavingBackground deferral failed\n" );
    ok( resuming_h.count >= 1, "Resuming did not fire\n" );

    pShowWindow( hwnd, SW_HIDE );
    pump_until_event( entered_h.event, 3000, pPeekMessageW, pTranslateMessage, pDispatchMessageW, pMsgWaitForMultipleObjects );
    ok( entered_h.count >= 1, "EnteredBackground did not fire\n" );
    ok( entered_h.deferral_ok, "EnteredBackground deferral failed\n" );

    hr = ICoreApplication2_remove_LeavingBackground( core_app2, tok_leaving );
    ok( hr == S_OK, "remove_LeavingBackground hr %#lx\n", hr );
    hr = ICoreApplication2_remove_EnteredBackground( core_app2, tok_entered );
    ok( hr == S_OK, "remove_EnteredBackground hr %#lx\n", hr );
    hr = ICoreApplication_remove_Resuming( core_app, tok_resuming );
    ok( hr == S_OK, "remove_Resuming hr %#lx\n", hr );

    ok( leaving_h.ref == 1, "leaving_h ref %ld\n", leaving_h.ref );
    ok( entered_h.ref == 1, "entered_h ref %ld\n", entered_h.ref );
    ok( resuming_h.ref == 1, "resuming_h ref %ld\n", resuming_h.ref );

    pDestroyWindow( hwnd );
    CloseHandle( leaving_h.event );
    CloseHandle( entered_h.event );
    CloseHandle( resuming_h.event );
    ICoreApplication2_Release( core_app2 );
    ICoreApplication_Release( core_app );
    IActivationFactory_Release( factory );
    FreeLibrary( user32 );
}

/* Microsoft.Windows.AppLifecycle.IMicrosoftAppInstanceStatics */
static const GUID IID_IMicrosoftAppInstanceStatics =
    {0x4f414b25, 0x8330, 0x5a9b, {0xbb, 0xc1, 0x82, 0x29, 0xd4, 0x79, 0x64, 0x9d}};

static void test_AppInstance(void)
{
    static const WCHAR *class_name = L"Microsoft.Windows.AppLifecycle.AppInstance";
    HRESULT (STDMETHODCALLTYPE *get_RecommendedInstance)( IInspectable *obj, IInspectable **value );
    IWeakReferenceSource *weak_ref_source;
    IActivationFactory *factory;
    IWeakReference *weak_ref;
    IInspectable *statics, *instance, *resolved;
    void **vtable;
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

    hr = IActivationFactory_QueryInterface( factory, &IID_IMicrosoftAppInstanceStatics, (void **)&statics );
    IActivationFactory_Release( factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED( hr ))
        return;

    /* get_RecommendedInstance follows the six IInspectable slots. */
    vtable = *(void **)(void *)statics;
    get_RecommendedInstance = (HRESULT (STDMETHODCALLTYPE *)( IInspectable *, IInspectable ** ))vtable[6];
    instance = NULL;
    hr = get_RecommendedInstance( statics, &instance );
    IInspectable_Release( statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED( hr ) || !instance)
        return;

    check_interface( instance, &IID_IUnknown, TRUE );
    check_interface( instance, &IID_IInspectable, TRUE );
    check_interface( instance, &IID_IAgileObject, TRUE );
    check_interface( instance, &IID_IWeakReferenceSource, TRUE );

    /* Every WinRT server object answers IWeakReferenceSource: the .NET/WinRT
     * callable wrapper, C++/WinRT weak_ref and the event registration helpers
     * QI for it on the AppInstance object handed out by RecommendedInstance. */
    weak_ref_source = NULL;
    hr = IInspectable_QueryInterface( instance, &IID_IWeakReferenceSource, (void **)&weak_ref_source );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED( hr ) && weak_ref_source)
    {
        weak_ref = NULL;
        hr = IWeakReferenceSource_GetWeakReference( weak_ref_source, &weak_ref );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (SUCCEEDED( hr ) && weak_ref)
        {
            resolved = NULL;
            hr = IWeakReference_Resolve( weak_ref, &IID_IInspectable, (IInspectable **)&resolved );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            ok( resolved == instance, "weak reference resolved to %p, expected %p.\n", resolved, instance );
            if (resolved) IInspectable_Release( resolved );
            IWeakReference_Release( weak_ref );
        }
        IWeakReferenceSource_Release( weak_ref_source );
    }

    IInspectable_Release( instance );
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

    /* the projection of the class carries the later static interfaces too */
    check_interface( factory, &IID_IToastNotificationManagerStatics2, TRUE );
    check_interface( factory, &IID_IToastNotificationManagerStatics4, TRUE );
    check_interface( factory, &IID_IToastNotificationManagerStatics5, TRUE );

    {
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationManagerStatics2 *statics2;
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationManagerStatics4 *statics4;
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationManagerStatics5 *statics5;
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationManagerForUser *for_user;
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *notifier2;

        hr = IActivationFactory_QueryInterface( factory, &IID_IToastNotificationManagerStatics5,
                                                (void **)&statics5 );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        /* ToastNotificationManager.GetDefault() is the entry the .NET projection
         * of the notification path reaches its notifier through */
        hr = IToastNotificationManagerStatics5_GetDefault( statics5, &for_user );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (hr == S_OK)
        {
            hr = IToastNotificationManagerForUser_CreateToastNotifier( for_user, &notifier2 );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            if (hr == S_OK)
            {
                __x_ABI_CWindows_CUI_CNotifications_CNotificationSetting setting = 0xc0ffee;
                hr = __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_get_Setting( notifier2, &setting );
                ok( hr == S_OK && setting == NotificationSetting_Enabled, "got hr %#lx, setting %d.\n", hr, setting );
                __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_Release( notifier2 );
            }
            IToastNotificationManagerForUser_Release( for_user );
        }
        IToastNotificationManagerStatics5_Release( statics5 );

        hr = IActivationFactory_QueryInterface( factory, &IID_IToastNotificationManagerStatics4,
                                                (void **)&statics4 );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IToastNotificationManagerStatics4_ConfigureNotificationMirroring( statics4,
                                                                              NotificationMirroring_Allowed );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        IToastNotificationManagerStatics4_Release( statics4 );

        hr = IActivationFactory_QueryInterface( factory, &IID_IToastNotificationManagerStatics2,
                                                (void **)&statics2 );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        /* the toast history store: Clear, Show -> GetHistory, Remove */
        {
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationHistory *history = NULL;
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationHistory2 *history2;
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationFactory *toast_factory;
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotification2 *hist_toast2;
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *hist_toast;
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *hist_notifier;
            __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *view;
            __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content;
            IActivationFactory *content_factory, *toast_class_factory;
            IInspectable *instance;
            static const WCHAR *xml_class = RuntimeClass_Windows_Data_Xml_Dom_XmlDocument;
            static const WCHAR *toast_class = RuntimeClass_Windows_UI_Notifications_ToastNotification;
            static const WCHAR *hist_tag = L"wine.history.test";
            UINT32 size = 0xc0ffee;

            hr = IToastNotificationManagerStatics2_get_History( statics2, &history );
            ok( hr == S_OK && history, "got hr %#lx, history %p.\n", hr, history );
            if (hr != S_OK) goto history_done;

            hr = IToastNotificationHistory_Clear( history );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IToastNotificationHistory_QueryInterface( history, &IID_IToastNotificationHistory2,
                                                           (void **)&history2 );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IToastNotificationHistory2_GetHistory( history2, &view );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_get_Size( view, &size );
            ok( hr == S_OK && !size, "got hr %#lx, size %u.\n", hr, size );
            __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_Release( view );
            IToastNotificationHistory2_Release( history2 );

            /* show a tagged toast and find it back in the history */
            hr = WindowsCreateString( xml_class, wcslen( xml_class ), &str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&content_factory );
            WindowsDeleteString( str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IActivationFactory_ActivateInstance( content_factory, &instance );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IInspectable_QueryInterface( instance, &IID_IXmlDocument, (void **)&content );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            IInspectable_Release( instance );
            IActivationFactory_Release( content_factory );

            hr = WindowsCreateString( toast_class, wcslen( toast_class ), &str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&toast_class_factory );
            WindowsDeleteString( str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IActivationFactory_QueryInterface( toast_class_factory, &IID_IToastNotificationFactory,
                                                    (void **)&toast_factory );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IToastNotificationFactory_CreateToastNotification( toast_factory, content, &hist_toast );
            ok( hr == S_OK, "got hr %#lx.\n", hr );

            hr = WindowsCreateString( hist_tag, wcslen( hist_tag ), &str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IToastNotification_QueryInterface( hist_toast, &IID_IToastNotification2,
                                                    (void **)&hist_toast2 );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IToastNotification2_put_Tag( hist_toast2, str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );

            hr = IToastNotificationManagerStatics_CreateToastNotifier( toast_notification_manager_statics,
                                                                       &hist_notifier );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_Show( hist_notifier, hist_toast );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            /* showing the same tagged toast again replaces its history entry */
            hr = __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_Show( hist_notifier, hist_toast );
            ok( hr == S_OK, "got hr %#lx.\n", hr );

            /* the toast is in the history, and the snapshot is the toast shown */
            hr = IToastNotificationHistory2_GetHistory( history2, &view );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_get_Size( view, &size );
            ok( hr == S_OK && size == 1, "got hr %#lx, size %u.\n", hr, size );
            if (hr == S_OK && size == 1)
            {
                __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *shown;
                IUnknown *shown_unk, *toast_unk;

                hr = __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_GetAt( view, 0, &shown );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                hr = __x_ABI_CWindows_CUI_CNotifications_CIToastNotification_QueryInterface( shown, &IID_IUnknown,
                                                                                             (void **)&shown_unk );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                hr = IToastNotification_QueryInterface( hist_toast, &IID_IUnknown, (void **)&toast_unk );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                ok( shown_unk == toast_unk, "got shown %p, toast %p.\n", shown_unk, toast_unk );
                IUnknown_Release( toast_unk );
                IUnknown_Release( shown_unk );
                __x_ABI_CWindows_CUI_CNotifications_CIToastNotification_Release( shown );
            }

            /* the snapshot enumerates: the closed IIterable pinterface is answered */
            check_interface( view, &IID___FIVectorView_1_Windows__CUI__CNotifications__CToastNotification, TRUE );
            check_interface( view, &IID___FIIterable_1_Windows__CUI__CNotifications__CToastNotification, TRUE );
            {
                __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iterable;
                __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iterator;
                __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *current = NULL;
                BOOLEAN moved = FALSE;

                hr = __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_QueryInterface(
                        view, &IID___FIIterable_1_Windows__CUI__CNotifications__CToastNotification,
                        (void **)&iterable );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                hr = __FIIterable_1_Windows__CUI__CNotifications__CToastNotification_First( iterable, &iterator );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                hr = __FIIterator_1_Windows__CUI__CNotifications__CToastNotification_MoveNext( iterator, &moved );
                ok( hr == S_OK && moved, "got hr %#lx, moved %d.\n", hr, moved );
                hr = __FIIterator_1_Windows__CUI__CNotifications__CToastNotification_get_Current( iterator,
                                                                                                  &current );
                ok( hr == S_OK && current, "got hr %#lx, current %p.\n", hr, current );
                __x_ABI_CWindows_CUI_CNotifications_CIToastNotification_Release( current );
                moved = TRUE;
                hr = __FIIterator_1_Windows__CUI__CNotifications__CToastNotification_MoveNext( iterator, &moved );
                ok( hr == S_OK && !moved, "got hr %#lx, moved %d.\n", hr, moved );
                __FIIterator_1_Windows__CUI__CNotifications__CToastNotification_Release( iterator );
                __FIIterable_1_Windows__CUI__CNotifications__CToastNotification_Release( iterable );
            }
            __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_Release( view );

            /* Remove(tag) prunes the entry by tag, Clear empties the store */
            hr = IToastNotificationHistory_Remove( history, str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IToastNotificationHistory2_GetHistory( history2, &view );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_get_Size( view, &size );
            ok( hr == S_OK && !size, "got hr %#lx, size %u.\n", hr, size );
            __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_Release( view );
            hr = IToastNotificationHistory_Clear( history );
            ok( hr == S_OK, "got hr %#lx.\n", hr );

            IToastNotification2_Release( hist_toast2 );
            IToastNotification_Release( hist_toast );
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_Release( hist_notifier );
            IToastNotificationFactory_Release( toast_factory );
            IActivationFactory_Release( toast_class_factory );
            IXmlDocument_Release( content );
            WindowsDeleteString( str );
history_done:
            IToastNotificationHistory_Release( history );
        }
        IToastNotificationManagerStatics2_Release( statics2 );
    }

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

/* Mirror of the module's Wine extension interfaces (dlls/twinapi.appcore/
 * toast_notification_manager.c): NotificationSuppressed sink registry and
 * ToastContent/ToastTemplate readback, QI-able on the ToastNotification face. */
static const IID IID_ToastSuppressed_ext = {0x4b3e2a91, 0x77d5, 0x4f1a, {0x9d,0x8e, 0x6c,0x3b, 0x5a,0x2f, 0x18,0xc4}};
static const IID IID_ToastTemplate_ext = {0x0d2f6b83, 0x62e1, 0x4c9f, {0xb0,0xa7, 0x54,0x1e, 0x9d,0xc6, 0x38,0x72}};

#define ToastTemplate_ToastGeneric 8

typedef struct ToastSuppressedVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( void *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( void *iface );
    ULONG (STDMETHODCALLTYPE *Release)( void *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( void *iface, ULONG *iid_count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( void *iface, HSTRING *class_name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( void *iface, TrustLevel *trust_level );
    HRESULT (STDMETHODCALLTYPE *add_NotificationSuppressed)( void *iface,
            __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler,
            EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_NotificationSuppressed)( void *iface, EventRegistrationToken token );
} ToastSuppressedVtbl;

typedef struct ToastTemplateVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( void *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( void *iface );
    ULONG (STDMETHODCALLTYPE *Release)( void *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( void *iface, ULONG *iid_count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( void *iface, HSTRING *class_name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( void *iface, TrustLevel *trust_level );
    HRESULT (STDMETHODCALLTYPE *get_ToastTemplate)( void *iface, INT32 *value );
    HRESULT (STDMETHODCALLTYPE *get_ToastContent)( void *iface,
            __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **value );
} ToastTemplateVtbl;

typedef struct
{
    const ToastSuppressedVtbl *lpVtbl;
} ToastSuppressed;

typedef struct
{
    const ToastTemplateVtbl *lpVtbl;
} ToastTemplate;

static unsigned int suppressed_calls;

static HRESULT WINAPI suppressed_handler_QueryInterface(
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *iface,
        REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID___FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI suppressed_handler_Noop(
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *iface )
{
    return 1;
}

static HRESULT WINAPI suppressed_handler_Invoke(
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *sender, IInspectable *args )
{
    suppressed_calls++;
    return S_OK;
}

static const struct __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectableVtbl
        suppressed_handler_vtbl =
{
    suppressed_handler_QueryInterface,
    suppressed_handler_Noop,
    suppressed_handler_Noop,
    suppressed_handler_Invoke,
};

static __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable suppressed_handler =
    { &suppressed_handler_vtbl };

static void test_toast_payload(void)
{
    static const WCHAR *xml_class = RuntimeClass_Windows_Data_Xml_Dom_XmlDocument;
    static const WCHAR *toast_class = RuntimeClass_Windows_UI_Notifications_ToastNotification;
    static const WCHAR *manager_class = RuntimeClass_Windows_UI_Notifications_ToastNotificationManager;
    static const WCHAR *payload_xml = L"<toast><visual><binding template=\"ToastGeneric\">"
            L"<text>Payload title</text><text>Payload body</text></binding></visual></toast>";
    static const WCHAR *tag = L"wine.payload.tag";
    ToastSuppressed *suppressed = NULL;
    ToastTemplate *tmpl = NULL;
    LONG ref;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content = NULL;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationFactory *toast_factory;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotification2 *toast2;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *notifier;
    __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *view;
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler = &suppressed_handler;
    IToastNotificationManagerStatics *statics;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocumentIO *io;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *toast;
    IInspectable *instance;
    IActivationFactory *xml_factory, *toast_factory_obj, *manager_factory;
    EventRegistrationToken token = {0};
    HSTRING str = NULL;
    INT32 value = 0;
    UINT32 size = 0xc0ffee;
    HRESULT hr;

    /* the toast payload the notification path parses */
    hr = WindowsCreateString( xml_class, wcslen( xml_class ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&xml_factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( xml_class ) );
        return;
    }
    hr = IActivationFactory_ActivateInstance( xml_factory, &instance );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( instance, &IID_IXmlDocument, (void **)&content );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( instance );
    IActivationFactory_Release( xml_factory );

    hr = WindowsCreateString( payload_xml, wcslen( payload_xml ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IXmlDocument_QueryInterface( content, &IID_IXmlDocumentIO, (void **)&io );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IXmlDocumentIO_LoadXml( io, str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IXmlDocumentIO_Release( io );
    WindowsDeleteString( str );

    hr = WindowsCreateString( toast_class, wcslen( toast_class ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&toast_factory_obj );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( toast_factory_obj, &IID_IToastNotificationFactory,
                                            (void **)&toast_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IToastNotificationFactory_CreateToastNotification( toast_factory, content, &toast );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* the notification keeps the parsed payload document */
    {
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *got;
        IUnknown *got_unk, *doc_unk;

        hr = IToastNotification_get_Content( toast, &got );
        ok( hr == S_OK && got, "got hr %#lx, content %p.\n", hr, got );
        hr = IXmlDocument_QueryInterface( got, &IID_IUnknown, (void **)&got_unk );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IXmlDocument_QueryInterface( content, &IID_IUnknown, (void **)&doc_unk );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( got_unk == doc_unk, "got content %p, document %p.\n", got_unk, doc_unk );
        IUnknown_Release( doc_unk );
        IUnknown_Release( got_unk );
        IXmlDocument_Release( got );
    }

    /* the tag round-trips */
    hr = IToastNotification_QueryInterface( toast, &IID_IToastNotification2, (void **)&toast2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( tag, wcslen( tag ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IToastNotification2_put_Tag( toast2, str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    str = NULL;
    hr = IToastNotification2_get_Tag( toast2, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !lstrcmpW( WindowsGetStringRawBuffer( str, NULL ), tag ), "got %s.\n",
        wine_dbgstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
    WindowsDeleteString( str );
    str = NULL;

    /* the ToastGeneric binding reads back as the ToastGeneric template */
    hr = IToastNotification_QueryInterface( toast, &IID_ToastTemplate_ext, (void **)&tmpl );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr == S_OK)
    {
        hr = tmpl->lpVtbl->get_ToastTemplate( tmpl, &value );
        ok( hr == S_OK && value == ToastTemplate_ToastGeneric, "got hr %#lx, template %d.\n", hr, value );
        tmpl->lpVtbl->Release( tmpl );
    }

    /* the NotificationSuppressed sink registers, fires on a suppressed Show */
    hr = IToastNotification_QueryInterface( toast, &IID_ToastSuppressed_ext, (void **)&suppressed );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr == S_OK)
    {
        hr = suppressed->lpVtbl->add_NotificationSuppressed( suppressed, handler, &token );
        ok( hr == S_OK && token.value, "got hr %#lx, token %s.\n", hr, wine_dbgstr_longlong( token.value ) );
    }

    hr = WindowsCreateString( manager_class, wcslen( manager_class ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&manager_factory );
    WindowsDeleteString( str );
    str = NULL;
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( manager_factory, &IID_IToastNotificationManagerStatics,
                                            (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IToastNotificationManagerStatics_CreateToastNotifier( statics, &notifier );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IToastNotification2_put_SuppressPopup( toast2, TRUE );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_Show( notifier, toast );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( suppressed_calls == 1, "got %u suppressed calls.\n", suppressed_calls );

    if (suppressed)
    {
        hr = suppressed->lpVtbl->remove_NotificationSuppressed( suppressed, token );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        suppressed->lpVtbl->Release( suppressed );
    }
    IToastNotification2_Release( toast2 );

    /* the shown queue is the first entry of get_Notifications */
    hr = IToastNotifier_QueryInterface( notifier,
            &IID___FIVectorView_1_Windows__CUI__CNotifications__CToastNotification, (void **)&view );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr == S_OK)
    {
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *shown;
        IUnknown *shown_unk, *toast_unk;

        hr = __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_get_Size( view, &size );
        ok( hr == S_OK && size == 1, "got hr %#lx, size %u.\n", hr, size );
        hr = __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_GetAt( view, 0, &shown );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (hr == S_OK)
        {
            hr = __x_ABI_CWindows_CUI_CNotifications_CIToastNotification_QueryInterface( shown, &IID_IUnknown,
                                                                                         (void **)&shown_unk );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            hr = IToastNotification_QueryInterface( toast, &IID_IUnknown, (void **)&toast_unk );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            ok( shown_unk == toast_unk, "got shown %p, toast %p.\n", shown_unk, toast_unk );
            IUnknown_Release( toast_unk );
            IUnknown_Release( shown_unk );
            __x_ABI_CWindows_CUI_CNotifications_CIToastNotification_Release( shown );
        }
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_Release( view );
    }

    ref = __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier_Release( notifier );
    ok( !ref, "got ref %ld.\n", ref );
    ref = IToastNotificationManagerStatics_Release( statics );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( manager_factory );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IToastNotificationFactory_Release( toast_factory );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( toast_factory_obj );
    ok( ref >= 1, "got ref %ld.\n", ref );
    /* The shown-toast queue keeps the notification (and its payload document)
     * alive until it is evicted from the queue of the last 16; a notification
     * daemon that accepted the toast adds one more live reference. */
    ref = IToastNotification_Release( toast );
    ok( ref == 1 || ref == 2, "got ref %ld.\n", ref );
    ref = IXmlDocument_Release( content );
    ok( ref == 1 || ref == 2, "got ref %ld.\n", ref );
}

static void test_ToastNotification(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_UI_Notifications_ToastNotification;
    static const WCHAR *xml_class_name = RuntimeClass_Windows_Data_Xml_Dom_XmlDocument;
    static const WCHAR *tag = L"wine.tag";
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotificationFactory *toast_notification_factory;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *toast_notification;
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotification2 *toast_notification2;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *xml_document;
    IInspectable *instance;
    IActivationFactory *factory, *xml_factory;
    IUnknown *content_unk, *document_unk;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IToastNotificationFactory, TRUE );

    /* the payload DOM the notification path builds its content in */
    hr = WindowsCreateString( xml_class_name, wcslen( xml_class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&xml_factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        IActivationFactory_Release( factory );
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( xml_class_name ) );
        return;
    }
    hr = IActivationFactory_ActivateInstance( xml_factory, &instance );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( instance, &IID_IXmlDocument, (void **)&xml_document );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IActivationFactory_QueryInterface( factory, &IID_IToastNotificationFactory,
                                            (void **)&toast_notification_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IToastNotificationFactory_CreateToastNotification( toast_notification_factory, xml_document,
                                                            &toast_notification );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* the notification keeps the content document it was created with */
    {
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content;

        hr = IToastNotification_get_Content( toast_notification, &content );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IXmlDocument_QueryInterface( content, &IID_IUnknown, (void **)&content_unk );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IXmlDocument_QueryInterface( xml_document, &IID_IUnknown, (void **)&document_unk );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( content_unk == document_unk, "content %p, document %p.\n", content_unk, document_unk );
        IUnknown_Release( document_unk );
        IUnknown_Release( content_unk );
        IXmlDocument_Release( content );
    }

    /* the tag/group surface of the notification round-trips */
    hr = IToastNotification_QueryInterface( toast_notification, &IID_IToastNotification2,
                                            (void **)&toast_notification2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( tag, wcslen( tag ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IToastNotification2_put_Tag( toast_notification2, str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IToastNotification2_get_Tag( toast_notification2, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !lstrcmpW( WindowsGetStringRawBuffer( str, NULL ), tag ), "got %s.\n",
        wine_dbgstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
    WindowsDeleteString( str );
    ref = IToastNotification2_Release( toast_notification2 );
    ok( ref == 1, "got ref %ld.\n", ref );

    ref = __x_ABI_CWindows_CUI_CNotifications_CIToastNotification_Release( toast_notification );
    ok( !ref, "got ref %ld.\n", ref );
    ref = IToastNotificationFactory_Release( toast_notification_factory );
    ok( ref >= 1, "got ref %ld.\n", ref );

    /* the class is activatable with an interface: no parameterless activation */
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_NOINTERFACE || hr == E_FAIL || hr == E_POINTER, "got hr %#lx.\n", hr );

    /* the document is also referenced by the instance the activation returned */
    ref = IXmlDocument_Release( xml_document );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IInspectable_Release( instance );
    ok( !ref, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( xml_factory );
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
    test_CoreApplication_lifecycle();
    test_ApplicationView();
    test_CoreApplication();
    test_AppInstance();
    test_DataTransferManager();
    test_PowerManager();
    test_toast_payload();
    test_ToastNotificationManager();
    test_ToastNotification();

    RoUninitialize();
}
