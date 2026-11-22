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
    WebTokenRequestStatus status;
    IVectorView_WebTokenResponse *response_data;
    WCHAR *cache_authority;
    WCHAR *cache_client_id;
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

    if (!ref)
    {
        if (impl->response_data) IVectorView_WebTokenResponse_Release( impl->response_data );
        free( impl->cache_authority );
        free( impl->cache_client_id );
        free( impl );
    }
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
    struct web_token_request_result *impl = impl_from_result( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->response_data;
    if (*value) IVectorView_WebTokenResponse_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI result_get_ResponseStatus( const void *iface, WebTokenRequestStatus *value )
{
    struct web_token_request_result *impl = impl_from_result( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->status;
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
    struct web_token_request_result *impl = impl_from_result( iface );
    struct async_op *op;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->cache_authority && impl->cache_client_id)
        wam_cache_invalidate( impl->cache_authority, impl->cache_client_id );
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

IInspectable *create_web_token_request_result_ex( WebTokenRequestStatus status,
                                                  IVectorView_WebTokenResponse *response_data,
                                                  const WCHAR *authority, const WCHAR *client_id )
{
    struct web_token_request_result *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->result_vtbl = web_token_request_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    if (response_data)
    {
        impl->response_data = response_data;
        IVectorView_WebTokenResponse_AddRef( response_data );
    }
    if (authority && *authority) impl->cache_authority = wcsdup( authority );
    if (client_id && *client_id) impl->cache_client_id = wcsdup( client_id );

    TRACE( "created WebTokenRequestResult %p status=%u response_data=%p.\n", impl, status, response_data );
    return (IInspectable *)impl;
}

IInspectable *create_web_token_request_result( void )
{
    return create_web_token_request_result_ex( WebTokenRequestStatus_UserCancel, NULL, NULL, NULL );
}

/* WebTokenResponse with optional token and WebAccount. */

struct web_token_response
{
    const void *response_vtbl;
    LONG ref;
    HSTRING token;
    IInspectable *account;
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

    if (!ref)
    {
        if (impl->token) WindowsDeleteString( impl->token );
        if (impl->account) IInspectable_Release( impl->account );
        free( impl );
    }
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
    struct web_token_response *impl = impl_from_response( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if (impl->token) return WindowsDuplicateString( impl->token, value );
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
    struct web_token_response *impl = impl_from_response( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->account)
    {
        IInspectable_AddRef( impl->account );
        *value = (__x_ABI_CWindows_CSecurity_CCredentials_CWebAccount *)impl->account;
    }
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

IInspectable *create_web_token_response_ex( const WCHAR *token, IInspectable *account )
{
    struct web_token_response *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->response_vtbl = web_token_response_vtbl;
    impl->ref = 1;
    if (token && *token) WindowsCreateString( token, lstrlenW( token ), &impl->token );
    if (account)
    {
        impl->account = account;
        IInspectable_AddRef( account );
    }

    TRACE( "created WebTokenResponse %p account=%p.\n", impl, account );
    return (IInspectable *)impl;
}

IInspectable *create_web_token_response( void )
{
    return create_web_token_response_ex( NULL, NULL );
}

/* Single-element IVectorView<WebTokenResponse>. */

struct response_vector_view
{
    IVectorView_WebTokenResponse IVectorView_WebTokenResponse_iface;
    LONG ref;
    IInspectable *item;
};

static inline struct response_vector_view *impl_from_vector_view( IVectorView_WebTokenResponse *iface )
{
    return CONTAINING_RECORD( iface, struct response_vector_view, IVectorView_WebTokenResponse_iface );
}

static HRESULT WINAPI vec_QueryInterface( IVectorView_WebTokenResponse *iface, REFIID iid, void **out )
{
    struct response_vector_view *impl = impl_from_vector_view( iface );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IVectorView_WebTokenResponse ))
    {
        *out = &impl->IVectorView_WebTokenResponse_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI vec_AddRef( IVectorView_WebTokenResponse *iface )
{
    struct response_vector_view *impl = impl_from_vector_view( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI vec_Release( IVectorView_WebTokenResponse *iface )
{
    struct response_vector_view *impl = impl_from_vector_view( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->item) IInspectable_Release( impl->item );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI vec_GetIids( IVectorView_WebTokenResponse *iface, ULONG *iid_count, IID **iids )
{
    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI vec_GetRuntimeClassName( IVectorView_WebTokenResponse *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Foundation.Collections.IVectorView`1<Windows.Security.Authentication.Web.Core.WebTokenResponse>",
                              class_name );
}

static HRESULT WINAPI vec_GetTrustLevel( IVectorView_WebTokenResponse *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI vec_GetAt( IVectorView_WebTokenResponse *iface, UINT32 index,
                                 __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenResponse **value )
{
    struct response_vector_view *impl = impl_from_vector_view( iface );

    if (!value) return E_POINTER;
    *value = NULL;
    if (index != 0 || !impl->item) return E_BOUNDS;
    return IInspectable_QueryInterface( impl->item, &IID_IWebTokenResponse, (void **)value );
}

static HRESULT WINAPI vec_get_Size( IVectorView_WebTokenResponse *iface, UINT32 *value )
{
    struct response_vector_view *impl = impl_from_vector_view( iface );

    if (!value) return E_POINTER;
    *value = impl->item ? 1 : 0;
    return S_OK;
}

static HRESULT WINAPI vec_IndexOf( IVectorView_WebTokenResponse *iface,
                                   __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenResponse *element,
                                   UINT32 *index, boolean *found )
{
    struct response_vector_view *impl = impl_from_vector_view( iface );

    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = (element && impl->item && (void *)element == (void *)impl->item);
    return S_OK;
}

static HRESULT WINAPI vec_GetMany( IVectorView_WebTokenResponse *iface, UINT32 start_index, UINT32 capacity,
                                   __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenResponse **value,
                                   UINT32 *actual )
{
    struct response_vector_view *impl = impl_from_vector_view( iface );

    if (!actual) return E_POINTER;
    *actual = 0;
    if (start_index == 0 && capacity >= 1 && impl->item && value)
    {
        HRESULT hr = IInspectable_QueryInterface( impl->item, &IID_IWebTokenResponse, (void **)&value[0] );
        if (SUCCEEDED( hr )) *actual = 1;
        return hr;
    }
    return S_OK;
}

static const struct IVectorView_WebTokenResponseVtbl response_vector_view_vtbl =
{
    vec_QueryInterface,
    vec_AddRef,
    vec_Release,
    vec_GetIids,
    vec_GetRuntimeClassName,
    vec_GetTrustLevel,
    vec_GetAt,
    vec_get_Size,
    vec_IndexOf,
    vec_GetMany,
};

IVectorView_WebTokenResponse *create_web_token_response_vector_view( IInspectable *response )
{
    struct response_vector_view *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IVectorView_WebTokenResponse_iface.lpVtbl = &response_vector_view_vtbl;
    impl->ref = 1;
    if (response)
    {
        impl->item = response;
        IInspectable_AddRef( response );
    }
    return &impl->IVectorView_WebTokenResponse_iface;
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
