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
#define WIDL_using_Windows_ApplicationModel_Core
#include "windows.applicationmodel.core.h"
#define WIDL_using_Windows_ApplicationModel_DataTransfer
#include "windows.applicationmodel.datatransfer.h"

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
    static const WCHAR *class_name = RuntimeClass_Windows_UI_ViewManagement_ApplicationView;
    IApplicationViewStatics2 *app_view_statics2;
    IActivationFactory *factory;
    IApplicationView *app_view;
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
    check_interface( factory, &IID_IApplicationViewStatics, TRUE );
    check_interface( factory, &IID_IApplicationViewStatics2, TRUE );

    /* Test IApplicationViewStatics2 */
    hr = IActivationFactory_QueryInterface( factory, &IID_IApplicationViewStatics2,
                                            (void **)&app_view_statics2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IApplicationViewStatics2_GetForCurrentView( app_view_statics2, &app_view );
    ok( hr == 0x80070490, "got hr %#lx.\n", hr );

    IApplicationViewStatics2_Release( app_view_statics2 );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_CoreApplication(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_Core_CoreApplication;
    IActivationFactory *factory;
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

    RoUninitialize();
}
