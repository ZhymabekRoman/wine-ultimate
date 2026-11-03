/*
 * Windows.Networking.PushNotifications tests
 *
 * Copyright 2026 Wine Developers
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

#include "initguid.h"
#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "asyncinfo.h"
#include "eventtoken.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Networking_PushNotifications
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.networking.pushnotifications.h"

#include "wine/test.h"

static const WCHAR expected_uri[] = L"https://db5.notify.windows.com/?token=animvwZ...fake";

static void check_hresult( ULONG line, HRESULT hr, HRESULT expected )
{
    ok_(__FILE__, line)( hr == expected, "got hr %#lx, expected %#lx.\n", hr, expected );
}
#define check_hr( hr, expected ) check_hresult( __LINE__, hr, expected )

static void get_now( ULONGLONG *now )
{
    FILETIME ft;

    GetSystemTimeAsFileTime( &ft );
    *now = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static ULONG invoked_count;

static HRESULT WINAPI handler_QueryInterface( IAsyncOperationCompletedHandler_PushNotificationChannel *iface,
                                              REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_PushNotificationChannel ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef( IAsyncOperationCompletedHandler_PushNotificationChannel *iface )
{
    return 2;
}

static ULONG WINAPI handler_Release( IAsyncOperationCompletedHandler_PushNotificationChannel *iface )
{
    return 1;
}

static HRESULT WINAPI handler_Invoke( IAsyncOperationCompletedHandler_PushNotificationChannel *iface,
                                      IAsyncOperation_PushNotificationChannel *async, enum AsyncStatus status )
{
    invoked_count++;
    ok( status == Completed, "got status %u.\n", status );
    return S_OK;
}

static const struct IAsyncOperationCompletedHandler_PushNotificationChannelVtbl handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static IAsyncOperationCompletedHandler_PushNotificationChannel completed_handler = { &handler_vtbl };

static void check_channel( ULONG line, IPushNotificationChannel *channel )
{
    __x_ABI_CWindows_CFoundation_CDateTime expiration = { 0xdeadbeefdeadbeefull };
    IPushNotificationReceivedEventArgs *args = (void *)0xdeadbeef;
    __FIEventHandler_1_Windows__CNetworking__CPushNotifications__CPushNotificationReceivedEventArgs *handler = NULL;
    EventRegistrationToken token = { 0 };
    const WCHAR *raw;
    UINT32 len;
    ULONGLONG now;
    HSTRING uri = NULL;
    HRESULT hr;

    hr = IPushNotificationChannel_get_Uri( channel, NULL );
    ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );

    hr = IPushNotificationChannel_get_Uri( channel, &uri );
    check_hr( hr, S_OK );
    raw = WindowsGetStringRawBuffer( uri, &len );
    ok( raw && !lstrcmpW( raw, expected_uri ), "got uri %s.\n", debugstr_w( raw ) );
    ok( len == lstrlenW( expected_uri ), "got %u chars.\n", len );
    WindowsDeleteString( uri );

    hr = IPushNotificationChannel_get_ExpirationTime( channel, NULL );
    ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IPushNotificationChannel_get_ExpirationTime( channel, &expiration );
    check_hr( hr, S_OK );
    get_now( &now );
    ok_(__FILE__, line)( expiration.UniversalTime > now + 863900000000ull && expiration.UniversalTime <= now + 864000000000ull,
                        "got expiry %s, now %s.\n", wine_dbgstr_longlong( expiration.UniversalTime ),
                        wine_dbgstr_longlong( now ) );

    hr = IPushNotificationChannel_add_PushNotificationReceived( channel, handler, &token );
    check_hr( hr, S_OK );
    ok_(__FILE__, line)( token.value != 0, "got token %s.\n", wine_dbgstr_longlong( token.value ) );
    hr = IPushNotificationChannel_remove_PushNotificationReceived( channel, token );
    check_hr( hr, S_OK );

    hr = IPushNotificationChannel_QueryInterface( channel, &IID_IPushNotificationReceivedEventArgs, (void **)&args );
    ok_(__FILE__, line)( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );

    hr = IPushNotificationChannel_Close( channel );
    check_hr( hr, S_OK );
}

static void test_activation_factory( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Networking_PushNotifications_PushNotificationChannelManager;
    IPushNotificationChannelManagerStatics4 *statics4 = (void *)0xdeadbeef;
    IPushNotificationChannelManagerStatics3 *statics3 = (void *)0xdeadbeef;
    IPushNotificationChannelManagerStatics2 *statics2 = (void *)0xdeadbeef;
    IPushNotificationChannelManagerStatics *statics = (void *)0xdeadbeef;
    __FIEventHandler_1_Windows__CNetworking__CPushNotifications__CPushNotificationChannelsRevokedEventArgs *handler = NULL;
    IActivationFactory *factory = (void *)0xdeadbeef;
    EventRegistrationToken token = { 0 };
    HSTRING str = NULL;
    HRESULT hr;

    hr = WindowsCreateString( class_name, lstrlenW( class_name ), &str );
    check_hr( hr, S_OK );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IPushNotificationChannelManagerStatics, (void **)&statics );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IPushNotificationChannelManagerStatics2, (void **)&statics2 );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IPushNotificationChannelManagerStatics3, (void **)&statics3 );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IPushNotificationChannelManagerStatics4, (void **)&statics4 );
    check_hr( hr, S_OK );

    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_FAIL || hr == E_POINTER, "got hr %#lx.\n", hr );

    /* Statics4 is the ChannelsRevoked event registration point. */
    hr = IPushNotificationChannelManagerStatics4_add_ChannelsRevoked( statics4, handler, &token );
    check_hr( hr, S_OK );
    ok( token.value != 0, "got token %s.\n", wine_dbgstr_longlong( token.value ) );
    hr = IPushNotificationChannelManagerStatics4_remove_ChannelsRevoked( statics4, token );
    check_hr( hr, S_OK );

    IPushNotificationChannelManagerStatics4_Release( statics4 );
    IPushNotificationChannelManagerStatics3_Release( statics3 );
    IPushNotificationChannelManagerStatics2_Release( statics2 );
    IPushNotificationChannelManagerStatics_Release( statics );
    IActivationFactory_Release( factory );
}

static void test_create_channel_async( void )
{
    IAsyncOperationCompletedHandler_PushNotificationChannel *handler = (void *)0xdeadbeef;
    IPushNotificationChannelManagerStatics *statics = (void *)0xdeadbeef;
    IPushNotificationChannelManagerStatics3 *statics3 = (void *)0xdeadbeef;
    IAsyncOperation_PushNotificationChannel *op = (void *)0xdeadbeef;
    IPushNotificationChannelManagerForUser *for_user = (void *)0xdeadbeef;
    __x_ABI_CWindows_CSystem_CIUser *user = (void *)0xdeadbeef;
    IPushNotificationChannel *channel = (void *)0xdeadbeef;
    IAsyncOperation_PushNotificationChannel *op2 = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    void *data = (void *)0xdeadbeef;
    enum AsyncStatus status = 0xdeadbeef;
    IAsyncInfo *info = (void *)0xdeadbeef;
    HSTRING str = NULL;
    HRESULT hr;

    hr = WindowsCreateString( RuntimeClass_Windows_Networking_PushNotifications_PushNotificationChannelManager,
                              lstrlenW( RuntimeClass_Windows_Networking_PushNotifications_PushNotificationChannelManager ),
                              &str );
    check_hr( hr, S_OK );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (hr != S_OK)
    {
        win_skip( "PushNotificationChannelManager not registered, skipping tests.\n" );
        return;
    }
    hr = IActivationFactory_QueryInterface( factory, &IID_IPushNotificationChannelManagerStatics, (void **)&statics );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IPushNotificationChannelManagerStatics3, (void **)&statics3 );
    check_hr( hr, S_OK );

    hr = IPushNotificationChannelManagerStatics_CreatePushNotificationChannelForApplicationAsync( statics, NULL );
    check_hr( hr, E_POINTER );

    invoked_count = 0;
    hr = IPushNotificationChannelManagerStatics_CreatePushNotificationChannelForApplicationAsync( statics, &op );
    check_hr( hr, S_OK );

    hr = IAsyncOperation_PushNotificationChannel_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    check_hr( hr, S_OK );
    hr = IAsyncInfo_get_Status( info, &status );
    check_hr( hr, S_OK );
    ok( status == Completed, "got status %u.\n", status );
    hr = IAsyncInfo_Close( info );
    check_hr( hr, S_OK );
    IAsyncInfo_Release( info );

    hr = IAsyncOperation_PushNotificationChannel_put_Completed( op, &completed_handler );
    check_hr( hr, S_OK );
    ok( invoked_count == 1, "got %lu handler invocations.\n", invoked_count );

    hr = IAsyncOperation_PushNotificationChannel_get_Completed( op, &handler );
    check_hr( hr, S_OK );
    ok( handler == &completed_handler, "got handler %p.\n", handler );
    IAsyncOperationCompletedHandler_PushNotificationChannel_Release( handler );

    hr = IAsyncOperation_PushNotificationChannel_GetResults( op, &channel );
    check_hr( hr, S_OK );
    check_channel( __LINE__, channel );
    IPushNotificationChannel_Release( channel );

    hr = IAsyncOperation_PushNotificationChannel_QueryInterface( op, &IID_IAsyncOperation_PushNotificationChannel,
                                                                (void **)&data );
    IUnknown_Release( (IUnknown *)data );
    check_hr( hr, S_OK );
    IAsyncOperation_PushNotificationChannel_Release( op );

    /* The tile and library-scoped creation points complete the same way. */
    hr = WindowsCreateString( L"tile", 4, &str );
    check_hr( hr, S_OK );
    hr = IPushNotificationChannelManagerStatics_CreatePushNotificationChannelForSecondaryTileAsync( statics, str, &op2 );
    check_hr( hr, S_OK );
    WindowsDeleteString( str );
    hr = IAsyncOperation_PushNotificationChannel_GetResults( op2, &channel );
    check_hr( hr, S_OK );
    check_channel( __LINE__, channel );
    IPushNotificationChannel_Release( channel );
    IAsyncOperation_PushNotificationChannel_Release( op2 );

    hr = IPushNotificationChannelManagerStatics_CreatePushNotificationChannelForApplicationAsyncWithId( statics, str, &op2 );
    check_hr( hr, S_OK );
    hr = IAsyncOperation_PushNotificationChannel_GetResults( op2, &channel );
    check_hr( hr, S_OK );
    check_channel( __LINE__, channel );
    IPushNotificationChannel_Release( channel );
    IAsyncOperation_PushNotificationChannel_Release( op2 );

    /* GetDefault hands out the per-user manager stand-in. */
    hr = IPushNotificationChannelManagerStatics3_GetDefault( statics3, NULL );
    check_hr( hr, E_POINTER );
    hr = IPushNotificationChannelManagerStatics3_GetDefault( statics3,
            (__x_ABI_CWindows_CNetworking_CPushNotifications_CIPushNotificationChannelManagerForUser **)&for_user );
    check_hr( hr, S_OK );
    hr = IPushNotificationChannelManagerForUser_QueryInterface( for_user, &IID_IAgileObject, &data );
    check_hr( hr, S_OK );
    IUnknown_Release( (IUnknown *)data );
    hr = IPushNotificationChannelManagerForUser_get_User( for_user, NULL );
    check_hr( hr, E_POINTER );
    hr = IPushNotificationChannelManagerForUser_get_User( for_user, &user );
    check_hr( hr, S_OK );
    ok( user == NULL, "got user %p.\n", user );
    hr = IPushNotificationChannelManagerForUser_CreatePushNotificationChannelForApplicationAsync( for_user, &op2 );
    check_hr( hr, S_OK );
    hr = IAsyncOperation_PushNotificationChannel_GetResults( op2, &channel );
    check_hr( hr, S_OK );
    check_channel( __LINE__, channel );
    IPushNotificationChannel_Release( channel );
    IAsyncOperation_PushNotificationChannel_Release( op2 );
    IPushNotificationChannelManagerForUser_Release( for_user );

    IPushNotificationChannelManagerStatics3_Release( statics3 );
    IPushNotificationChannelManagerStatics_Release( statics );
    IActivationFactory_Release( factory );
}

static void test_channel_class( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Networking_PushNotifications_PushNotificationChannel;
    IPushNotificationChannel *channel = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    HSTRING str = NULL;
    HRESULT hr;

    hr = WindowsCreateString( class_name, lstrlenW( class_name ), &str );
    check_hr( hr, S_OK );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    check_hr( hr, S_OK );
    hr = IInspectable_QueryInterface( inspectable, &IID_IPushNotificationChannel, (void **)&channel );
    check_hr( hr, S_OK );
    check_channel( __LINE__, channel );
    IPushNotificationChannel_Release( channel );

    IInspectable_Release( inspectable );
    IActivationFactory_Release( factory );
}

START_TEST( pushnotifications )
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    test_activation_factory();
    test_create_channel_async();
    test_channel_class();

    RoUninitialize();
}
