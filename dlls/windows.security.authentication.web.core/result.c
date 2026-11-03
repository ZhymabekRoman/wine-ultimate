/* WinRT Windows.Security.Authentication.Web.Core result objects
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

/*
 * WebTokenRequestResult stand-in.  There is no Web Account Manager provider in
 * a prefix, so a token request cannot succeed: the result reports
 * WebTokenRequestStatus_UserCancel, which is the status Windows returns for a
 * request the user dismissed, and every value the result carries stays empty -
 * no response data (no token), no provider error and no account.  The object is
 * QI-faithful for its default interface so projections can cast to it.
 */

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

struct web_token_request_result
{
    const void *result_vtbl;
    LONG ref;
};

static inline struct web_token_request_result *impl_from_result( const void *iface )
{
    return (struct web_token_request_result *)iface;
}

static HRESULT WINAPI result_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct web_token_request_result *impl = impl_from_result( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWebTokenRequestResult ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI result_AddRef( const void *iface )
{
    struct web_token_request_result *impl = impl_from_result( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI result_Release( const void *iface )
{
    struct web_token_request_result *impl = impl_from_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI result_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI result_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Security.Authentication.Web.Core.WebTokenRequestResult", class_name );
}

static HRESULT WINAPI result_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI result_get_ResponseData( const void *iface, IVectorView_WebTokenResponse **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    /* No token was issued, so the response data collection stays empty. */
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI result_get_ResponseStatus( const void *iface, WebTokenRequestStatus *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = WebTokenRequestStatus_UserCancel;
    return S_OK;
}

static HRESULT WINAPI result_get_ResponseError( const void *iface,
                                                __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CWebProviderError **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI result_InvalidateCacheAsync( const void *iface, IAsyncAction **value )
{
    struct async_op *op;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!(op = create_async_action( NULL ))) return E_OUTOFMEMORY;
    *value = (IAsyncAction *)async_action_iface( op );
    return S_OK;
}

static const void *web_token_request_result_vtbl[] =
{
    result_QueryInterface,
    result_AddRef,
    result_Release,
    result_GetIids,
    result_GetRuntimeClassName,
    result_GetTrustLevel,
    result_get_ResponseData,
    result_get_ResponseStatus,
    result_get_ResponseError,
    result_InvalidateCacheAsync,
};

IInspectable *create_web_token_request_result( void )
{
    struct web_token_request_result *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->result_vtbl = web_token_request_result_vtbl;
    impl->ref = 1;

    TRACE( "created WebTokenRequestResult %p.\n", impl );
    return (IInspectable *)impl;
}

/* WebTokenResponse stand-in: no token, no account, no provider error. */

struct web_token_response
{
    const void *response_vtbl;
    LONG ref;
};

static inline struct web_token_response *impl_from_response( const void *iface )
{
    return (struct web_token_response *)iface;
}

static HRESULT WINAPI response_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct web_token_response *impl = impl_from_response( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWebTokenResponse ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI response_AddRef( const void *iface )
{
    struct web_token_response *impl = impl_from_response( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI response_Release( const void *iface )
{
    struct web_token_response *impl = impl_from_response( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI response_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI response_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Security.Authentication.Web.Core.WebTokenResponse", class_name );
}

static HRESULT WINAPI response_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI response_get_Token( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return dup_empty_string( value );
}

static HRESULT WINAPI response_get_ProviderError( const void *iface,
                                                  __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CWebProviderError **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI response_get_WebAccount( const void *iface,
                                               __x_ABI_CWindows_CSecurity_CCredentials_CWebAccount **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI response_get_Properties( const void *iface, IMap_HSTRING_HSTRING **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *web_token_response_vtbl[] =
{
    response_QueryInterface,
    response_AddRef,
    response_Release,
    response_GetIids,
    response_GetRuntimeClassName,
    response_GetTrustLevel,
    response_get_Token,
    response_get_ProviderError,
    response_get_WebAccount,
    response_get_Properties,
};

IInspectable *create_web_token_response( void )
{
    struct web_token_response *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->response_vtbl = web_token_response_vtbl;
    impl->ref = 1;

    TRACE( "created WebTokenResponse %p.\n", impl );
    return (IInspectable *)impl;
}

/* WebProviderError stand-in: no error code, no message, no properties. */

struct web_provider_error
{
    const void *error_vtbl;
    LONG ref;
};

static inline struct web_provider_error *impl_from_error( const void *iface )
{
    return (struct web_provider_error *)iface;
}

static HRESULT WINAPI error_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct web_provider_error *impl = impl_from_error( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWebProviderError ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI error_AddRef( const void *iface )
{
    struct web_provider_error *impl = impl_from_error( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI error_Release( const void *iface )
{
    struct web_provider_error *impl = impl_from_error( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI error_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI error_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Security.Authentication.Web.Core.WebProviderError", class_name );
}

static HRESULT WINAPI error_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI error_get_ErrorCode( const void *iface, INT32 *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI error_get_ErrorMessage( const void *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return dup_empty_string( value );
}

static HRESULT WINAPI error_get_Properties( const void *iface, IMapView_HSTRING_HSTRING **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *web_provider_error_vtbl[] =
{
    error_QueryInterface,
    error_AddRef,
    error_Release,
    error_GetIids,
    error_GetRuntimeClassName,
    error_GetTrustLevel,
    error_get_ErrorCode,
    error_get_ErrorMessage,
    error_get_Properties,
};

IInspectable *create_web_provider_error( void )
{
    struct web_provider_error *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->error_vtbl = web_provider_error_vtbl;
    impl->ref = 1;

    TRACE( "created WebProviderError %p.\n", impl );
    return (IInspectable *)impl;
}
