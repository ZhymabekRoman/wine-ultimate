/* WinRT Windows.Security.Credentials.PasswordCredential Implementation
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

#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(creds);

struct password_credential
{
    IPasswordCredential IPasswordCredential_iface;
    LONG ref;

    HSTRING resource;
    HSTRING username;
    HSTRING password;
};

static inline struct password_credential *impl_from_IPasswordCredential( IPasswordCredential *iface )
{
    return CONTAINING_RECORD( iface, struct password_credential, IPasswordCredential_iface );
}

static HRESULT WINAPI credential_QueryInterface( IPasswordCredential *iface, REFIID iid, void **out )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPasswordCredential ))
    {
        *out = &impl->IPasswordCredential_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI credential_AddRef( IPasswordCredential *iface )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI credential_Release( IPasswordCredential *iface )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        WindowsDeleteString( impl->resource );
        WindowsDeleteString( impl->username );
        WindowsDeleteString( impl->password );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI credential_GetIids( IPasswordCredential *iface, ULONG *iid_count, IID **iids )
{
    static const IID iids_array[] =
    {
        { 0x6ab18989, 0x1326, 0x4969, { 0x9e, 0x61, 0xe6, 0xec, 0x19, 0xb2, 0xf5, 0xc6 } }, /* IID_IPasswordCredential */
    };
    IID *array;

    if (!iid_count || !iids) return E_POINTER;
    if (!(array = CoTaskMemAlloc( sizeof(iids_array) ))) return E_OUTOFMEMORY;
    memcpy( array, iids_array, sizeof(iids_array) );
    *iid_count = ARRAY_SIZE(iids_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI credential_GetRuntimeClassName( IPasswordCredential *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Security_Credentials_PasswordCredential,
                                sizeof(RuntimeClass_Windows_Security_Credentials_PasswordCredential) / sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI credential_GetTrustLevel( IPasswordCredential *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI credential_get_Resource( IPasswordCredential *iface, HSTRING *value )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->resource, value );
}

static HRESULT WINAPI credential_get_UserName( IPasswordCredential *iface, HSTRING *value )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->username, value );
}

static HRESULT WINAPI credential_put_UserName( IPasswordCredential *iface, HSTRING value )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );
    HSTRING dup;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if ((hr = WindowsDuplicateString( value, &dup )) != S_OK) return hr;
    WindowsDeleteString( impl->username );
    impl->username = dup;
    return S_OK;
}

static HRESULT WINAPI credential_get_Password( IPasswordCredential *iface, HSTRING *value )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->password, value );
}

static HRESULT WINAPI credential_put_Password( IPasswordCredential *iface, HSTRING value )
{
    struct password_credential *impl = impl_from_IPasswordCredential( iface );
    HSTRING dup;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if ((hr = WindowsDuplicateString( value, &dup )) != S_OK) return hr;
    WindowsDeleteString( impl->password );
    impl->password = dup;
    return S_OK;
}

/*
 * On Windows the property is lazily backed: RetrievePassword() pulls the
 * secret from the vault entry behind the (resource, user) key.  This
 * implementation keeps the secret in the object, so RetrievePassword()
 * answers with the same string the Password property does, which is the
 * observable contract after a vault Retrieve()/Add() round trip.
 */
static HRESULT WINAPI credential_RetrievePassword( IPasswordCredential *iface, HSTRING *value )
{
    return credential_get_Password( iface, value );
}

static const struct IPasswordCredentialVtbl credential_vtbl =
{
    credential_QueryInterface,
    credential_AddRef,
    credential_Release,
    credential_GetIids,
    credential_GetRuntimeClassName,
    credential_GetTrustLevel,
    credential_get_Resource,
    credential_get_UserName,
    credential_put_UserName,
    credential_get_Password,
    credential_put_Password,
    credential_RetrievePassword,
};

static HRESULT set_hstring( const WCHAR *buffer, HSTRING *dst )
{
    return WindowsCreateString( buffer, buffer ? lstrlenW( buffer ) : 0, dst );
}

HRESULT create_password_credential( const WCHAR *resource, const WCHAR *username,
                                    const WCHAR *password, IPasswordCredential **out )
{
    struct password_credential *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IPasswordCredential_iface.lpVtbl = &credential_vtbl;
    impl->ref = 1;

    if (FAILED(set_hstring( resource, &impl->resource )))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }
    if (FAILED(set_hstring( username, &impl->username )))
    {
        WindowsDeleteString( impl->resource );
        free( impl );
        return E_OUTOFMEMORY;
    }
    if (FAILED(set_hstring( password, &impl->password )))
    {
        WindowsDeleteString( impl->resource );
        WindowsDeleteString( impl->username );
        free( impl );
        return E_OUTOFMEMORY;
    }

    *out = (IPasswordCredential *)impl;
    TRACE( "created PasswordCredential %p.\n", *out );
    return S_OK;
}
