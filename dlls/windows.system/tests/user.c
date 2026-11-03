/*
 * Windows.System.User tests
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
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_System
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.storage.streams.h"
#include "windows.system.h"

#include "wine/test.h"

static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK || (!supported && hr == E_NOINTERFACE), "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
        IUnknown_Release( unk );
}
#define check_interface( obj, iid, supported ) check_interface_( __LINE__, obj, iid, supported )

static ULONG invoked_count;
static enum AsyncStatus invoked_status;

static HRESULT WINAPI handler_QueryInterface( __FIAsyncOperationCompletedHandler_1_IInspectable *iface,
                                              REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef( __FIAsyncOperationCompletedHandler_1_IInspectable *iface )
{
    return 2;
}

static ULONG WINAPI handler_Release( __FIAsyncOperationCompletedHandler_1_IInspectable *iface )
{
    return 1;
}

static HRESULT WINAPI handler_Invoke( __FIAsyncOperationCompletedHandler_1_IInspectable *iface,
                                      __FIAsyncOperation_1_IInspectable *async, enum AsyncStatus status )
{
    invoked_count++;
    invoked_status = status;
    return S_OK;
}

static const __FIAsyncOperationCompletedHandler_1_IInspectableVtbl handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static __FIAsyncOperationCompletedHandler_1_IInspectable completed_handler = { &handler_vtbl };

/*
 * The operation the User property lookups hand out must be born completed: IAsyncInfo reports
 * Completed/S_OK, attaching a completion handler invokes it synchronously on the calling thread
 * (which is what makes the app's await path finish instead of parking), and GetResults returns
 * the documented "no such property" answer, a null result with S_OK.
 */
static void check_born_completed_op_( ULONG line, IInspectable *op )
{
    __FIAsyncOperation_1_IInspectable *async = (void *)op;
    IAsyncInfo *info = (void *)0xdeadbeef;
    IInspectable *results = (void *)0xdeadbeef;
    enum AsyncStatus status;
    HRESULT error_code;
    HRESULT hr;

    hr = IInspectable_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IAsyncInfo_get_Status( info, NULL );
    ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
    status = (enum AsyncStatus)0xdeadbeef;
    hr = IAsyncInfo_get_Status( info, &status );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( status == Completed, "got status %#x.\n", status );

    hr = IAsyncInfo_get_ErrorCode( info, &error_code );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( error_code == S_OK, "got error code %#lx.\n", error_code );

    hr = IAsyncInfo_Cancel( info );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncInfo_Release( info );

    invoked_count = 0;
    invoked_status = (enum AsyncStatus)0xdeadbeef;
    hr = __FIAsyncOperation_1_IInspectable_put_Completed( async, &completed_handler );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( invoked_count == 1, "got %lu invocations.\n", invoked_count );
    ok_(__FILE__, line)( invoked_status == Completed, "got status %#x.\n", invoked_status );

    /* get_Completed must hand back the handler that was attached. */
    {
        void *handler = NULL;
        hr = __FIAsyncOperation_1_IInspectable_get_Completed( async, &handler );
        ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
        ok_(__FILE__, line)( handler == (void *)&completed_handler, "got handler %p.\n", handler );
        if (handler) IUnknown_Release( (IUnknown *)handler );
    }

    hr = __FIAsyncOperation_1_IInspectable_GetResults( async, NULL );
    ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
    results = (void *)0xdeadbeef;
    hr = __FIAsyncOperation_1_IInspectable_GetResults( async, &results );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( !results, "got results %p, expected NULL.\n", results );
}

static void test_User_Statics(void)
{
    static const WCHAR user_name[] = L"Windows.System.User";
    static const WCHAR some_id[] = L"S-1-5-21-0-0-0-1001";
    IActivationFactory *factory = (void *)0xdeadbeef;
    IUserStatics *statics = (void *)0xdeadbeef;
    IUserStatics2 *statics2 = (void *)0xdeadbeef;
    IUser *user = (void *)0xdeadbeef;
    IUser *user2 = (void *)0xdeadbeef;
    HSTRING str = NULL, id = NULL;
    HRESULT hr;

    hr = WindowsCreateString( user_name, ARRAYSIZE( user_name ) - 1, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( user_name ) );
        return;
    }
    WindowsDeleteString( str );

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IUserStatics, TRUE );
    check_interface( factory, &IID_IUserStatics2, TRUE );

    /* The class has no activatable constructor; instances come from the statics. */
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    hr = IActivationFactory_QueryInterface( factory, &IID_IUserStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( factory, &IID_IUserStatics2, (void **)&statics2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* User.GetDefault: "Gets an object representing the default user account." */
    hr = IUserStatics2_GetDefault( statics2, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUserStatics2_GetDefault( statics2, &user );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( user, "got NULL user.\n" );

    /* User.GetFromId: a prefix has one identity, so the id resolves to that same user. */
    hr = WindowsCreateString( some_id, ARRAYSIZE( some_id ) - 1, &id );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IUserStatics_GetFromId( statics, id, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUserStatics_GetFromId( statics, id, &user2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( user2 == user, "got user %p, expected the default user %p.\n", user2, user );
    WindowsDeleteString( id );

    /* The enumeration surface has no user-information store behind it in a prefix. */
    hr = IUserStatics_CreateWatcher( statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUserStatics_FindAllAsync( statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUserStatics_FindAllAsyncByType( statics, UserType_LocalUser, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUserStatics_FindAllAsyncByTypeAndStatus( statics, UserType_LocalUser,
                                                   UserAuthenticationStatus_Unauthenticated, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    {
        IInspectable *watcher = (void *)0xdeadbeef;
        __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser *op = (void *)0xdeadbeef;

        hr = IUserStatics_CreateWatcher( statics, &watcher );
        ok( hr == E_NOTIMPL, "got hr %#lx.\n", hr );
        ok( !watcher, "got watcher %p.\n", watcher );
        hr = IUserStatics_FindAllAsync( statics, &op );
        ok( hr == E_NOTIMPL, "got hr %#lx.\n", hr );
        ok( !op, "got operation %p.\n", op );
    }

    IUser_Release( user2 );
    IUser_Release( user );
    IUserStatics_Release( statics );
    IUserStatics2_Release( statics2 );
    IActivationFactory_Release( factory );
}

static void test_User_Object(void)
{
    static const WCHAR user_name[] = L"Windows.System.User";
    __FIAsyncOperation_1_IInspectable *op = (void *)0xdeadbeef;
    __FIAsyncOperation_1_Windows__CFoundation__CCollections__CIPropertySet *op2 = (void *)0xdeadbeef;
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamReference *op3 = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IUserStatics2 *statics2 = (void *)0xdeadbeef;
    __x_ABI_CWindows_CSystem_CUserType type;
    __x_ABI_CWindows_CSystem_CUserAuthenticationStatus status;
    IUser *user = (void *)0xdeadbeef;
    UINT32 len = 0xdeadbeef;
    HSTRING str = NULL, id = NULL;
    HRESULT hr;

    hr = WindowsCreateString( user_name, ARRAYSIZE( user_name ) - 1, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (FAILED(hr))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( user_name ) );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IUserStatics2, (void **)&statics2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IUserStatics2_GetDefault( statics2, &user );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IUserStatics2_Release( statics2 );
    IActivationFactory_Release( factory );

    check_interface( user, &IID_IUnknown, TRUE );
    check_interface( user, &IID_IInspectable, TRUE );
    check_interface( user, &IID_IAgileObject, TRUE );
    check_interface( user, &IID_IUser, TRUE );

    /* NonRoamableId: "Gets the user's non-roamable id." - a stable, non-empty identity string. */
    hr = IUser_get_NonRoamableId( user, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUser_get_NonRoamableId( user, &id );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( id, "got NULL id.\n" );
    if (id)
    {
        HSTRING id2 = NULL;

        len = WindowsGetStringLen( id );
        ok( len > 0, "got empty non-roamable id.\n" );

        /* The identity is stable across reads. */
        hr = IUser_get_NonRoamableId( user, &id2 );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        {
            INT32 cmp = 0xdeadbeef;
            hr = WindowsCompareStringOrdinal( id, id2, &cmp );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            ok( cmp == 0, "id changed between reads.\n" );
        }
        WindowsDeleteString( id2 );
    }

    /* Type: "Gets the user type." - the single interactive account of the prefix is local. */
    hr = IUser_get_Type( user, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    type = (__x_ABI_CWindows_CSystem_CUserType)0xdeadbeef;
    hr = IUser_get_Type( user, &type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( type == UserType_LocalUser, "got type %u.\n", type );

    /* AuthenticationStatus: "Gets the authentication status of the user." */
    hr = IUser_get_AuthenticationStatus( user, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    status = (__x_ABI_CWindows_CSystem_CUserAuthenticationStatus)0xdeadbeef;
    hr = IUser_get_AuthenticationStatus( user, &status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( status == UserAuthenticationStatus_LocallyAuthenticated, "got status %u.\n", status );

    /* Property lookups: born-completed operations with no result (no property provider). */
    hr = IUser_GetPropertyAsync( user, id, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUser_GetPropertyAsync( user, id, &op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( op, "got NULL operation.\n" );
    if (op) check_born_completed_op_( __LINE__, (IInspectable *)op );
    if (op) __FIAsyncOperation_1_IInspectable_Release( op );

    hr = IUser_GetPropertiesAsync( user, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUser_GetPropertiesAsync( user, NULL, &op2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( op2, "got NULL operation.\n" );
    if (op2) check_born_completed_op_( __LINE__, (IInspectable *)op2 );
    if (op2) __FIAsyncOperation_1_Windows__CFoundation__CCollections__CIPropertySet_Release( op2 );

    hr = IUser_GetPictureAsync( user, UserPictureSize_Size64x64, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IUser_GetPictureAsync( user, UserPictureSize_Size64x64, &op3 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( op3, "got NULL operation.\n" );
    if (op3) check_born_completed_op_( __LINE__, (IInspectable *)op3 );
    if (op3) __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamReference_Release( op3 );

    WindowsDeleteString( id );
    IUser_Release( user );
}

START_TEST(user)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_User_Statics();
    test_User_Object();

    RoUninitialize();
}
