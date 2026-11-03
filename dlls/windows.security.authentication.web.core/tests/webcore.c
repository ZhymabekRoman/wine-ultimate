/*
 * Windows.Security.Authentication.Web.Core tests
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
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Security_Credentials
#define WIDL_using_Windows_Security_Authentication_Web_Core
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.security.credentials.h"
#include "windows.security.authentication.web.core.h"

#include "wine/test.h"

static void check_hresult( ULONG line, HRESULT hr, HRESULT expected )
{
    ok_(__FILE__, line)( hr == expected, "got hr %#lx, expected %#lx.\n", hr, expected );
}
#define check_hr( hr, expected ) check_hresult( __LINE__, hr, expected )

static ULONG invoked_count;

static HRESULT WINAPI handler_QueryInterface( IAsyncOperationCompletedHandler_WebTokenRequestResult *iface,
                                              REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_WebTokenRequestResult ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef( IAsyncOperationCompletedHandler_WebTokenRequestResult *iface )
{
    return 2;
}

static ULONG WINAPI handler_Release( IAsyncOperationCompletedHandler_WebTokenRequestResult *iface )
{
    return 1;
}

static HRESULT WINAPI handler_Invoke( IAsyncOperationCompletedHandler_WebTokenRequestResult *iface,
                                      IAsyncOperation_WebTokenRequestResult *async, enum AsyncStatus status )
{
    invoked_count++;
    ok( status == Completed, "got status %u.\n", status );
    return S_OK;
}

static const struct IAsyncOperationCompletedHandler_WebTokenRequestResultVtbl handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static IAsyncOperationCompletedHandler_WebTokenRequestResult completed_handler = { &handler_vtbl };

static void test_activation_factory( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Security_Authentication_Web_Core_WebAuthenticationCoreManager;
    IWebAuthenticationCoreManagerStatics2 *statics2 = (void *)0xdeadbeef;
    IWebAuthenticationCoreManagerStatics *statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
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

    hr = IActivationFactory_QueryInterface( factory, &IID_IInspectable, (void **)&statics );
    IInspectable_Release( (IInspectable *)statics );
    check_hr( hr, S_OK );

    hr = IActivationFactory_QueryInterface( factory, &IID_IWebAuthenticationCoreManagerStatics, (void **)&statics );
    check_hr( hr, S_OK );
    hr = IActivationFactory_QueryInterface( factory, &IID_IWebAuthenticationCoreManagerStatics2, (void **)&statics2 );
    check_hr( hr, S_OK );
    IWebAuthenticationCoreManagerStatics2_Release( statics2 );

    /* Statics-only class: no default constructor. */
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_FAIL || hr == E_POINTER, "got hr %#lx.\n", hr );

    IWebAuthenticationCoreManagerStatics_Release( statics );
    IActivationFactory_Release( factory );
}

static void test_request_token_async( void )
{
    static const WCHAR *request_class = RuntimeClass_Windows_Security_Authentication_Web_Core_WebTokenRequest;
    IActivationFactory *request_factory = (void *)0xdeadbeef;
    IWebAuthenticationCoreManagerStatics *statics = (void *)0xdeadbeef;
    IWebTokenRequestResult *result = (void *)0xdeadbeef;
    IAsyncOperation_WebTokenRequestResult *op = (void *)0xdeadbeef;
    __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenRequest *request = (void *)0xdeadbeef;
    IAsyncOperationCompletedHandler_WebTokenRequestResult *handler = (void *)0xdeadbeef;
    IAsyncInfo *info = (void *)0xdeadbeef;
    HRESULT error_code = 0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    WebTokenRequestStatus status = 0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IAsyncAction *action = (void *)0xdeadbeef;
    void *data = (void *)0xdeadbeef;
    HSTRING str = NULL;
    enum AsyncStatus async_status = 0xdeadbeef;
    HRESULT hr;

    hr = WindowsCreateString( RuntimeClass_Windows_Security_Authentication_Web_Core_WebAuthenticationCoreManager,
                              lstrlenW( RuntimeClass_Windows_Security_Authentication_Web_Core_WebAuthenticationCoreManager ),
                              &str );
    check_hr( hr, S_OK );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (hr != S_OK)
    {
        win_skip( "WebAuthenticationCoreManager not registered, skipping tests.\n" );
        return;
    }
    hr = IActivationFactory_QueryInterface( factory, &IID_IWebAuthenticationCoreManagerStatics, (void **)&statics );
    check_hr( hr, S_OK );

    /* Build the request the app would build through the WebTokenRequest class. */
    hr = WindowsCreateString( request_class, lstrlenW( request_class ), &str );
    check_hr( hr, S_OK );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&request_factory );
    WindowsDeleteString( str );
    check_hr( hr, S_OK );
    hr = IActivationFactory_ActivateInstance( request_factory, &inspectable );
    check_hr( hr, S_OK );
    hr = IInspectable_QueryInterface( inspectable, &IID_IWebTokenRequest, (void **)&request );
    check_hr( hr, S_OK );

    hr = statics->lpVtbl->RequestTokenAsync( statics, NULL, &op );
    check_hr( hr, E_INVALIDARG );
    hr = statics->lpVtbl->RequestTokenAsync( statics, request, NULL );
    check_hr( hr, E_POINTER );

    invoked_count = 0;
    hr = statics->lpVtbl->RequestTokenAsync( statics, request, &op );
    check_hr( hr, S_OK );

    hr = IAsyncOperation_WebTokenRequestResult_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    check_hr( hr, S_OK );
    hr = IAsyncInfo_get_Status( info, &async_status );
    check_hr( hr, S_OK );
    ok( async_status == Completed, "got status %u.\n", async_status );
    hr = IAsyncInfo_get_ErrorCode( info, &error_code );
    check_hr( hr, S_OK );
    ok( error_code == S_OK, "got error code %#lx.\n", error_code );
    hr = IAsyncInfo_Close( info );
    check_hr( hr, S_OK );
    IAsyncInfo_Release( info );

    /* The operation is born completed, so the handler fires when it is set. */
    hr = IAsyncOperation_WebTokenRequestResult_put_Completed( op, &completed_handler );
    check_hr( hr, S_OK );
    ok( invoked_count == 1, "got %lu handler invocations.\n", invoked_count );

    hr = IAsyncOperation_WebTokenRequestResult_get_Completed( op, &handler );
    check_hr( hr, S_OK );
    ok( handler == &completed_handler, "got handler %p.\n", handler );
    IAsyncOperationCompletedHandler_WebTokenRequestResult_Release( handler );

    hr = IAsyncOperation_WebTokenRequestResult_GetResults( op, &result );
    check_hr( hr, S_OK );
    hr = IWebTokenRequestResult_QueryInterface( result, &IID_IInspectable, (void **)&inspectable );
    IInspectable_Release( inspectable );
    check_hr( hr, S_OK );

    hr = IWebTokenRequestResult_get_ResponseStatus( result, NULL );
    check_hr( hr, E_POINTER );
    status = 0xdeadbeef;
    hr = IWebTokenRequestResult_get_ResponseStatus( result, &status );
    check_hr( hr, S_OK );
    ok( status == WebTokenRequestStatus_UserCancel, "got status %u.\n", status );

    data = (void *)0xdeadbeef;
    hr = IWebTokenRequestResult_get_ResponseData( result, (IVectorView_WebTokenResponse **)&data );
    check_hr( hr, S_OK );
    ok( data == NULL, "got response data %p.\n", data );
    data = (void *)0xdeadbeef;
    hr = IWebTokenRequestResult_get_ResponseError( result, (IWebProviderError **)&data );
    check_hr( hr, S_OK );
    ok( data == NULL, "got response error %p.\n", data );

    hr = IWebTokenRequestResult_InvalidateCacheAsync( result, &action );
    check_hr( hr, S_OK );
    IAsyncAction_Release( action );

    IWebTokenRequestResult_Release( result );
    IAsyncOperation_WebTokenRequestResult_Release( op );

    hr = statics->lpVtbl->GetTokenSilentlyAsync( statics, request, &op );
    check_hr( hr, S_OK );
    hr = IAsyncOperation_WebTokenRequestResult_GetResults( op, &result );
    check_hr( hr, S_OK );
    status = 0xdeadbeef;
    hr = IWebTokenRequestResult_get_ResponseStatus( result, &status );
    check_hr( hr, S_OK );
    ok( status == WebTokenRequestStatus_UserCancel, "got status %u.\n", status );
    IWebTokenRequestResult_Release( result );
    IAsyncOperation_WebTokenRequestResult_Release( op );

    IInspectable_Release( (IInspectable *)request );
    IInspectable_Release( inspectable );
    IActivationFactory_Release( request_factory );
    IWebAuthenticationCoreManagerStatics_Release( statics );
    IActivationFactory_Release( factory );
}

static void test_result_object( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Security_Authentication_Web_Core_WebTokenRequestResult;
    IWebTokenRequestResult *result = (void *)0xdeadbeef;
    IWebTokenResponse *other = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    WebTokenRequestStatus status = 0xdeadbeef;
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
    hr = IInspectable_QueryInterface( inspectable, &IID_IWebTokenRequestResult, (void **)&result );
    check_hr( hr, S_OK );
    hr = IInspectable_QueryInterface( inspectable, &IID_IWebTokenResponse, (void **)&other );
    ok( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );
    ok( other == NULL, "got other %p.\n", other );

    hr = IWebTokenRequestResult_get_ResponseStatus( result, &status );
    check_hr( hr, S_OK );
    ok( status == WebTokenRequestStatus_UserCancel, "got status %u.\n", status );

    IWebTokenRequestResult_Release( result );
    IInspectable_Release( inspectable );
    IActivationFactory_Release( factory );
}

START_TEST( webcore )
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    test_activation_factory();
    test_request_token_async();
    test_result_object();

    RoUninitialize();
}
