/*
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
#include <stdio.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#define WIDL_using_Windows_Security_Credentials
#include "windows.security.credentials.h"

#ifndef PasswordCredential
#define PasswordCredential __x_ABI_CWindows_CSecurity_CCredentials_CPasswordCredential
#endif

#include "wine/test.h"

static const WCHAR resource[] = L"wine-credentials-test-resource";
static const WCHAR resource2[] = L"wine-credentials-test-resource2";
static const WCHAR user[] = L"wine-credentials-test-user";
static const WCHAR password[] = L"s3cret";
static const WCHAR password2[] = L"m0retokens";

static HSTRING hstr( const WCHAR *s )
{
    HSTRING ret;
    ok_( __FILE__, __LINE__ )( WindowsCreateString( s, lstrlenW( s ), &ret ) == S_OK, "failed to create string\n" );
    return ret;
}

static void check_string( HSTRING str, const WCHAR *expected )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( str, NULL );
    ok( !lstrcmpW( buffer ? buffer : L"", expected ), "got %s, expected %s\n",
        debugstr_w( buffer ), debugstr_w( expected ) );
}

static void expect_vector( __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *vec,
                           UINT32 expected )
{
    UINT32 size = 0;
    HRESULT hr = __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_get_Size( vec, &size );
    ok( hr == S_OK, "get_Size failed, hr %#lx\n", hr );
    ok( size == expected, "got size %u, expected %u\n", size, expected );
}

static void check_credential( IPasswordCredential *cred, const WCHAR *res, const WCHAR *usr, const WCHAR *pass )
{
    HSTRING str;
    HRESULT hr;

    hr = IPasswordCredential_get_Resource( cred, &str );
    ok( hr == S_OK, "get_Resource failed, hr %#lx\n", hr );
    check_string( str, res );
    WindowsDeleteString( str );

    hr = IPasswordCredential_get_UserName( cred, &str );
    ok( hr == S_OK, "get_UserName failed, hr %#lx\n", hr );
    check_string( str, usr );
    WindowsDeleteString( str );

    hr = IPasswordCredential_get_Password( cred, &str );
    ok( hr == S_OK, "get_Password failed, hr %#lx\n", hr );
    check_string( str, pass );
    WindowsDeleteString( str );

    hr = IPasswordCredential_RetrievePassword( cred, &str );
    ok( hr == S_OK, "RetrievePassword failed, hr %#lx\n", hr );
    check_string( str, pass );
    WindowsDeleteString( str );
}

static void test_vault( void )
{
    __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential *vec;
    IPasswordCredentialFactory *factory;
    IPasswordCredential *cred, *elem;
    IPasswordVault *vault;
    IInspectable *obj;
    IActivationFactory *activ;
    HSTRING hresource, huser, hpassword;
    HRESULT hr;
    boolean has;
    UINT32 size;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    hr = RoGetActivationFactory( hstr( RuntimeClass_Windows_Security_Credentials_PasswordVault ),
                                 &IID_IPasswordVault, (void **)&vault );
    ok( hr == S_OK, "RoGetActivationFactory(PasswordVault) failed, hr %#lx\n", hr );

    hr = RoGetActivationFactory( hstr( RuntimeClass_Windows_Security_Credentials_PasswordCredential ),
                                 &IID_IPasswordCredentialFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory(PasswordCredential) failed, hr %#lx\n", hr );

    /* composable constructor (resource, user, password) */
    hresource = hstr( resource );
    huser = hstr( user );
    hpassword = hstr( password );
    hr = IPasswordCredentialFactory_CreatePasswordCredential( factory, hresource, huser, hpassword,
                                                              &cred );
    ok( hr == S_OK, "CreatePasswordCredential failed, hr %#lx\n", hr );
    check_credential( cred, resource, user, password );

    /* composable constructor (resource, user): default empty password */
    hr = IPasswordCredentialFactory_CreatePasswordCredentialWithDefaultPassword( factory, hresource, huser,
                                                                                 &cred );
    ok( hr == S_OK, "CreatePasswordCredentialWithDefaultPassword failed, hr %#lx\n", hr );
    check_credential( cred, resource, user, L"" );

    hr = IPasswordCredential_put_Password( cred, hpassword );
    ok( hr == S_OK, "put_Password failed, hr %#lx\n", hr );
    check_credential( cred, resource, user, password );

    /* property setters round-trip */
    {
        HSTRING huser2 = hstr( L"other-user" );
        hr = IPasswordCredential_put_UserName( cred, huser2 );
        ok( hr == S_OK, "put_UserName failed, hr %#lx\n", hr );
        check_credential( cred, resource, L"other-user", password );
        hr = IPasswordCredential_put_UserName( cred, huser );
        ok( hr == S_OK, "put_UserName failed, hr %#lx\n", hr );
        WindowsDeleteString( huser2 );
    }

    /* IActivationFactory::ActivateInstance default-constructs the class */
    hr = RoGetActivationFactory( hstr( RuntimeClass_Windows_Security_Credentials_PasswordCredential ),
                                 &IID_IActivationFactory, (void **)&activ );
    ok( hr == S_OK, "RoGetActivationFactory(PasswordCredential, IActivationFactory) failed, hr %#lx\n", hr );
    hr = IActivationFactory_ActivateInstance( activ, &obj );
    ok( hr == S_OK, "ActivateInstance failed, hr %#lx\n", hr );
    hr = IInspectable_QueryInterface( obj, &IID_IPasswordCredential, (void **)&cred );
    ok( hr == S_OK, "QI IPasswordCredential failed, hr %#lx\n", hr );
    check_credential( cred, L"", L"", L"" );
    IInspectable_Release( obj );
    IActivationFactory_Release( activ );

    /* Add + Retrieve round-trip */
    hr = IPasswordCredentialFactory_CreatePasswordCredential( factory, hresource, huser, hpassword,
                                                              &cred );
    ok( hr == S_OK, "CreatePasswordCredential failed, hr %#lx\n", hr );
    hr = IPasswordVault_Add( vault, cred );
    ok( hr == S_OK, "Add failed, hr %#lx\n", hr );

    hr = IPasswordVault_Retrieve( vault, hresource, huser, &cred );
    ok( hr == S_OK, "Retrieve failed, hr %#lx\n", hr );
    check_credential( cred, resource, user, password );

    /* Retrieve is case-insensitive on the target key */
    {
        HSTRING hupper = hstr( L"Wine-Credentials-Test-Resource" );
        hr = IPasswordVault_Retrieve( vault, hupper, huser, &cred );
        ok( hr == S_OK, "Retrieve with upper-case resource failed, hr %#lx\n", hr );
        check_credential( cred, L"Wine-Credentials-Test-Resource", user, password );
        WindowsDeleteString( hupper );
    }

    /* second resource for the find-by filters */
    {
        HSTRING hresource2 = hstr( resource2 );
        IPasswordCredential *cred2;

        hr = IPasswordCredentialFactory_CreatePasswordCredential( factory, hresource2, huser, hpassword,
                                                                  &cred2 );
        ok( hr == S_OK, "CreatePasswordCredential failed, hr %#lx\n", hr );
        hr = IPasswordVault_Add( vault, cred2 );
        ok( hr == S_OK, "Add failed, hr %#lx\n", hr );

        hr = IPasswordVault_FindAllByResource( vault, hresource, &vec );
        ok( hr == S_OK, "FindAllByResource failed, hr %#lx\n", hr );
        expect_vector( vec, 1 );
        hr = __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_GetAt( vec, 0, &elem );
        ok( hr == S_OK, "GetAt failed, hr %#lx\n", hr );
        check_credential( elem, resource, user, password );
        IPasswordCredential_Release( elem );

        hr = __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_GetAt( vec, 1, &elem );
        ok( hr == E_BOUNDS, "got hr %#lx, expected E_BOUNDS\n", hr );

        /* GetMany over the whole view */
        elem = NULL;
        hr = __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_GetMany( vec, 0, 1, &elem, &size );
        ok( hr == S_OK && size == 1, "GetMany failed, hr %#lx size %u\n", hr, size );
        IPasswordCredential_Release( elem );

        /* enumeration through IIterable (the C# projection path) */
        {
            __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential *iterable;
            __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential *iter;

            hr = __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_QueryInterface(
                    vec, &IID___FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential,
                    (void **)&iterable );
            ok( hr == S_OK, "QI IIterable failed, hr %#lx\n", hr );
            hr = __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential_First( iterable, &iter );
            ok( hr == S_OK, "First failed, hr %#lx\n", hr );
            hr = __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_get_HasCurrent( iter, &has );
            ok( hr == S_OK && has, "HasCurrent failed, hr %#lx\n", hr );
            hr = __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_get_Current( iter, &elem );
            ok( hr == S_OK, "Current failed, hr %#lx\n", hr );
            IPasswordCredential_Release( elem );
            hr = __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_MoveNext( iter, &has );
            ok( hr == S_OK && !has, "MoveNext failed, hr %#lx\n", hr );
            hr = __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_get_Current( iter, &elem );
            ok( hr == E_BOUNDS, "got hr %#lx, expected E_BOUNDS\n", hr );
            __FIIterator_1_Windows__CSecurity__CCredentials__CPasswordCredential_Release( iter );
            __FIIterable_1_Windows__CSecurity__CCredentials__CPasswordCredential_Release( iterable );
        }
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_Release( vec );

        hr = IPasswordVault_FindAllByUserName( vault, huser, &vec );
        ok( hr == S_OK, "FindAllByUserName failed, hr %#lx\n", hr );
        expect_vector( vec, 2 );
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_Release( vec );

        hr = IPasswordVault_RetrieveAll( vault, &vec );
        ok( hr == S_OK, "RetrieveAll failed, hr %#lx\n", hr );
        {
            UINT32 size_all = 0;
            __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_get_Size( vec, &size_all );
            ok( size_all >= 2, "got size %u, expected >= 2\n", size_all );
        }
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_Release( vec );

        /* Add overwrites the entry for the same (resource, user) key */
        {
            HSTRING hpassword2 = hstr( password2 );
            hr = IPasswordCredentialFactory_CreatePasswordCredential( factory, hresource2, huser, hpassword2,
                                                                      &cred2 );
            ok( hr == S_OK, "CreatePasswordCredential failed, hr %#lx\n", hr );
            hr = IPasswordVault_Add( vault, cred2 );
            ok( hr == S_OK, "Add failed, hr %#lx\n", hr );
            hr = IPasswordVault_Retrieve( vault, hresource2, huser, &cred2 );
            ok( hr == S_OK, "Retrieve failed, hr %#lx\n", hr );
            check_credential( cred2, resource2, user, password2 );
            IPasswordCredential_Release( cred2 );
            WindowsDeleteString( hpassword2 );
        }

        hr = IPasswordVault_FindAllByResource( vault, hresource2, &vec );
        ok( hr == S_OK, "FindAllByResource failed, hr %#lx\n", hr );
        expect_vector( vec, 1 );
        __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_Release( vec );

        hr = IPasswordVault_Remove( vault, cred2 );
        ok( hr == S_OK, "Remove failed, hr %#lx\n", hr );
        IPasswordCredential_Release( cred2 );
        WindowsDeleteString( hresource2 );
    }

    /* Remove + Remove-again (the second one is a no-op on Windows) */
    hr = IPasswordVault_Remove( vault, cred );
    ok( hr == S_OK, "Remove failed, hr %#lx\n", hr );
    hr = IPasswordVault_Remove( vault, cred );
    ok( hr == S_OK, "Remove of a removed credential failed, hr %#lx\n", hr );

    hr = IPasswordVault_Retrieve( vault, hresource, huser, &cred );
    ok( hr == HRESULT_FROM_WIN32( ERROR_NOT_FOUND ), "got hr %#lx, expected ERROR_NOT_FOUND\n", hr );

    hr = IPasswordVault_FindAllByResource( vault, hresource, &vec );
    ok( hr == S_OK, "FindAllByResource failed, hr %#lx\n", hr );
    expect_vector( vec, 0 );
    __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential_Release( vec );

    WindowsDeleteString( hresource );
    WindowsDeleteString( huser );
    WindowsDeleteString( hpassword );
    IPasswordCredential_Release( cred );
    IPasswordVault_Release( vault );
    IPasswordCredentialFactory_Release( factory );
}

START_TEST( credentials )
{
    test_vault();
}
