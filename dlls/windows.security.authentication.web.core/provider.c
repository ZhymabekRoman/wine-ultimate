/* WinRT Windows.Security.Authentication.Web.Core provider and request objects
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

WINE_DEFAULT_DEBUG_CHANNEL(webauthcore);

static HRESULT create_class_name( const WCHAR *name, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, lstrlenW( name ), class_name );
}

static HRESULT dup_empty_string( HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"", 0, value );
}

/*
 * WebAccountProvider stand-in for the system providers an app asks for by id
 * (FindAccountProviderAsync and friends).  There is no Web Account Manager
 * service in a prefix, so the provider carries no identity: an empty id, no
 * display name and no icon.  The object exists so the async result is a real,
 * QI-able provider instead of a null pointer the projection would throw on.
 */

struct web_account_provider
{
    const void *provider_vtbl;
    LONG ref;
};

static inline struct web_account_provider *impl_from_provider( const void *iface )
{
    return (struct web_account_provider *)iface;
}

static HRESULT WINAPI provider_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct web_account_provider *impl = impl_from_provider( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWebAccountProvider ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI provider_AddRef( const void *iface )
{
    struct web_account_provider *impl = impl_from_provider( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI provider_Release( const void *iface )
{
    struct web_account_provider *impl = impl_from_provider( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI provider_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI provider_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Security.Credentials.WebAccountProvider", class_name );
}

static HRESULT WINAPI provider_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI provider_get_Id( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return dup_empty_string( value );
}

static HRESULT WINAPI provider_get_DisplayName( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return dup_empty_string( value );
}

static HRESULT WINAPI provider_get_IconUri( const void *iface, __x_ABI_CWindows_CFoundation_CUri **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *web_account_provider_vtbl[] =
{
    provider_QueryInterface,
    provider_AddRef,
    provider_Release,
    provider_GetIids,
    provider_GetRuntimeClassName,
    provider_GetTrustLevel,
    provider_get_Id,
    provider_get_DisplayName,
    provider_get_IconUri,
};

IInspectable *create_web_account_provider( void )
{
    struct web_account_provider *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->provider_vtbl = web_account_provider_vtbl;
    impl->ref = 1;

    TRACE( "created WebAccountProvider %p.\n", impl );
    return (IInspectable *)impl;
}

/* WebAccount stand-in: no provider, no user name, state None. */

struct web_account
{
    const void *account_vtbl;
    LONG ref;
};

static inline struct web_account *impl_from_account( const void *iface )
{
    return (struct web_account *)iface;
}

static HRESULT WINAPI account_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct web_account *impl = impl_from_account( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWebAccount ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI account_AddRef( const void *iface )
{
    struct web_account *impl = impl_from_account( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI account_Release( const void *iface )
{
    struct web_account *impl = impl_from_account( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI account_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI account_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Security.Credentials.WebAccount", class_name );
}

static HRESULT WINAPI account_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI account_get_WebAccountProvider( const void *iface,
                                                      __x_ABI_CWindows_CSecurity_CCredentials_CWebAccountProvider **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI account_get_UserName( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return dup_empty_string( value );
}

static HRESULT WINAPI account_get_State( const void *iface, WebAccountState *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = WebAccountState_None;
    return S_OK;
}

static const void *web_account_vtbl[] =
{
    account_QueryInterface,
    account_AddRef,
    account_Release,
    account_GetIids,
    account_GetRuntimeClassName,
    account_GetTrustLevel,
    account_get_WebAccountProvider,
    account_get_UserName,
    account_get_State,
};

IInspectable *create_web_account( void )
{
    struct web_account *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->account_vtbl = web_account_vtbl;
    impl->ref = 1;

    TRACE( "created WebAccount %p.\n", impl );
    return (IInspectable *)impl;
}

/*
 * WebTokenRequest stand-in, created when an app activates the class.  The
 * request properties are read-only in the projection (the values are set
 * through the creation factory), so they read back empty here; the request
 * still round-trips through the token request APIs.
 */

struct web_token_request
{
    const void *request_vtbl;
    LONG ref;
};

static inline struct web_token_request *impl_from_request( const void *iface )
{
    return (struct web_token_request *)iface;
}

static HRESULT WINAPI request_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct web_token_request *impl = impl_from_request( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWebTokenRequest ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI request_AddRef( const void *iface )
{
    struct web_token_request *impl = impl_from_request( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI request_Release( const void *iface )
{
    struct web_token_request *impl = impl_from_request( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI request_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI request_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Security.Authentication.Web.Core.WebTokenRequest", class_name );
}

static HRESULT WINAPI request_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI request_get_WebAccountProvider( const void *iface,
                                                      __x_ABI_CWindows_CSecurity_CCredentials_CWebAccountProvider **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI request_get_Scope( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return dup_empty_string( value );
}

static HRESULT WINAPI request_get_ClientId( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return dup_empty_string( value );
}

static HRESULT WINAPI request_get_PromptType( const void *iface, WebTokenRequestPromptType *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = WebTokenRequestPromptType_Default;
    return S_OK;
}

static HRESULT WINAPI request_get_Properties( const void *iface, IMap_HSTRING_HSTRING **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *web_token_request_vtbl[] =
{
    request_QueryInterface,
    request_AddRef,
    request_Release,
    request_GetIids,
    request_GetRuntimeClassName,
    request_GetTrustLevel,
    request_get_WebAccountProvider,
    request_get_Scope,
    request_get_ClientId,
    request_get_PromptType,
    request_get_Properties,
};

IInspectable *create_web_token_request( void )
{
    struct web_token_request *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->request_vtbl = web_token_request_vtbl;
    impl->ref = 1;

    TRACE( "created WebTokenRequest %p.\n", impl );
    return (IInspectable *)impl;
}
