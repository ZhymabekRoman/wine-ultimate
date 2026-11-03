/* WinRT Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager statics
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

#define OP_CLASS_RESULT  L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Authentication.Web.Core.WebTokenRequestResult>"
#define OP_CLASS_ACCOUNT L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Credentials.WebAccount>"
#define OP_CLASS_PROVIDER L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Credentials.WebAccountProvider>"

/*
 * WebAuthenticationCoreManager is the entry point MSAL-based apps use to get a
 * token from a Web Account Manager provider.  A prefix has no Web Account
 * Manager service and no provider to show a UI against, so every request this
 * statics object starts completes immediately with a WebTokenRequestResult
 * reporting WebTokenRequestStatus_UserCancel: the same shape Windows produces
 * when the user dismisses the prompt, so the caller's error handling path runs
 * on a real result object instead of an exception escaping through the
 * projection's noexcept boundary.
 */

struct webauthcore_statics
{
    IActivationFactory IActivationFactory_iface;
    IWebAuthenticationCoreManagerStatics IWebAuthenticationCoreManagerStatics_iface;
    IWebAuthenticationCoreManagerStatics2 IWebAuthenticationCoreManagerStatics2_iface;
    LONG ref;
};

static inline struct webauthcore_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct webauthcore_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct webauthcore_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWebAuthenticationCoreManagerStatics ))
    {
        *out = &impl->IWebAuthenticationCoreManagerStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWebAuthenticationCoreManagerStatics2 ))
    {
        *out = &impl->IWebAuthenticationCoreManagerStatics2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct webauthcore_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct webauthcore_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager",
                                lstrlenW( L"Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager" ),
                                class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    /* The class is statics-only: Windows has no default constructor for it. */
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_FAIL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static HRESULT create_token_request_result_op( IAsyncOperation_WebTokenRequestResult **operation )
{
    struct async_op *op;
    IInspectable *result;

    if (!(result = create_web_token_request_result())) return E_OUTOFMEMORY;
    if (!(op = create_async_op( &IID_IAsyncOperation_WebTokenRequestResult, OP_CLASS_RESULT, result )))
    {
        IInspectable_Release( result );
        return E_OUTOFMEMORY;
    }
    IInspectable_Release( result );

    *operation = (IAsyncOperation_WebTokenRequestResult *)async_op_iface( op );
    TRACE( "created completed token request op %p.\n", *operation );
    return S_OK;
}

static HRESULT create_account_provider_op( IAsyncOperation_WebAccountProvider **operation )
{
    struct async_op *op;
    IInspectable *provider;

    if (!(provider = create_web_account_provider())) return E_OUTOFMEMORY;
    if (!(op = create_async_op( &IID_IAsyncOperation_WebAccountProvider, OP_CLASS_PROVIDER, provider )))
    {
        IInspectable_Release( provider );
        return E_OUTOFMEMORY;
    }
    IInspectable_Release( provider );

    *operation = (IAsyncOperation_WebAccountProvider *)async_op_iface( op );
    TRACE( "created completed account provider op %p.\n", *operation );
    return S_OK;
}

static HRESULT WINAPI statics_GetTokenSilentlyAsync( IWebAuthenticationCoreManagerStatics *iface,
                                                     __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenRequest *request,
                                                     IAsyncOperation_WebTokenRequestResult **operation )
{
    TRACE( "iface %p, request %p, operation %p.\n", iface, request, operation );

    if (!request) return E_INVALIDARG;
    if (!operation) return E_POINTER;
    return create_token_request_result_op( operation );
}

static HRESULT WINAPI statics_GetTokenSilentlyWithWebAccountAsync( IWebAuthenticationCoreManagerStatics *iface,
                                                                  __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenRequest *request,
                                                                  __x_ABI_CWindows_CSecurity_CCredentials_CIWebAccount *web_account,
                                                                  IAsyncOperation_WebTokenRequestResult **operation )
{
    TRACE( "iface %p, request %p, web_account %p, operation %p.\n", iface, request, web_account, operation );

    if (!request) return E_INVALIDARG;
    if (!operation) return E_POINTER;
    return create_token_request_result_op( operation );
}

static HRESULT WINAPI statics_RequestTokenAsync( IWebAuthenticationCoreManagerStatics *iface,
                                                 __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenRequest *request,
                                                 IAsyncOperation_WebTokenRequestResult **operation )
{
    TRACE( "iface %p, request %p, operation %p.\n", iface, request, operation );

    if (!request) return E_INVALIDARG;
    if (!operation) return E_POINTER;
    return create_token_request_result_op( operation );
}

static HRESULT WINAPI statics_RequestTokenWithWebAccountAsync( IWebAuthenticationCoreManagerStatics *iface,
                                                              __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenRequest *request,
                                                              __x_ABI_CWindows_CSecurity_CCredentials_CIWebAccount *web_account,
                                                              IAsyncOperation_WebTokenRequestResult **operation )
{
    TRACE( "iface %p, request %p, web_account %p, operation %p.\n", iface, request, web_account, operation );

    if (!request) return E_INVALIDARG;
    if (!operation) return E_POINTER;
    return create_token_request_result_op( operation );
}

static HRESULT WINAPI statics_FindAccountAsync( IWebAuthenticationCoreManagerStatics *iface,
                                                __x_ABI_CWindows_CSecurity_CCredentials_CIWebAccountProvider *provider,
                                                HSTRING web_account_id,
                                                IAsyncOperation_WebAccount **operation )
{
    struct async_op *op;
    IInspectable *account;

    TRACE( "iface %p, provider %p, web_account_id %s, operation %p.\n", iface, provider,
           debugstr_hstring( web_account_id ), operation );

    if (!operation) return E_POINTER;
    if (!(account = create_web_account())) return E_OUTOFMEMORY;
    if (!(op = create_async_op( &IID_IAsyncOperation_WebAccount, OP_CLASS_ACCOUNT, account )))
    {
        IInspectable_Release( account );
        return E_OUTOFMEMORY;
    }
    IInspectable_Release( account );

    *operation = (IAsyncOperation_WebAccount *)async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI statics_FindAccountProviderAsync( IWebAuthenticationCoreManagerStatics *iface,
                                                        HSTRING web_account_provider_id,
                                                        IAsyncOperation_WebAccountProvider **operation )
{
    TRACE( "iface %p, web_account_provider_id %s, operation %p.\n", iface,
           debugstr_hstring( web_account_provider_id ), operation );

    if (!operation) return E_POINTER;
    return create_account_provider_op( operation );
}

static HRESULT WINAPI statics_FindAccountProviderWithAuthorityAsync( IWebAuthenticationCoreManagerStatics *iface,
                                                                     HSTRING web_account_provider_id,
                                                                     HSTRING authority,
                                                                     IAsyncOperation_WebAccountProvider **operation )
{
    TRACE( "iface %p, web_account_provider_id %s, authority %s, operation %p.\n", iface,
           debugstr_hstring( web_account_provider_id ), debugstr_hstring( authority ), operation );

    if (!operation) return E_POINTER;
    return create_account_provider_op( operation );
}

DEFINE_IINSPECTABLE( statics, IWebAuthenticationCoreManagerStatics, struct webauthcore_statics,
                     IActivationFactory_iface )

static const struct IWebAuthenticationCoreManagerStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_GetTokenSilentlyAsync,
    statics_GetTokenSilentlyWithWebAccountAsync,
    statics_RequestTokenAsync,
    statics_RequestTokenWithWebAccountAsync,
    statics_FindAccountAsync,
    statics_FindAccountProviderAsync,
    statics_FindAccountProviderWithAuthorityAsync,
};

static HRESULT WINAPI statics2_FindAccountProviderWithAuthorityForUserAsync(
        IWebAuthenticationCoreManagerStatics2 *iface, HSTRING web_account_provider_id, HSTRING authority,
        __x_ABI_CWindows_CSystem_CIUser *user, IAsyncOperation_WebAccountProvider **operation )
{
    TRACE( "iface %p, web_account_provider_id %s, authority %s, user %p, operation %p.\n", iface,
           debugstr_hstring( web_account_provider_id ), debugstr_hstring( authority ), user, operation );

    if (!operation) return E_POINTER;
    return create_account_provider_op( operation );
}

DEFINE_IINSPECTABLE( statics2, IWebAuthenticationCoreManagerStatics2, struct webauthcore_statics,
                     IActivationFactory_iface )

static const struct IWebAuthenticationCoreManagerStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface,
    statics2_AddRef,
    statics2_Release,
    statics2_GetIids,
    statics2_GetRuntimeClassName,
    statics2_GetTrustLevel,
    statics2_FindAccountProviderWithAuthorityForUserAsync,
};

static struct webauthcore_statics webauthcore_statics =
{
    { &factory_vtbl },
    { &statics_vtbl },
    { &statics2_vtbl },
    1,
};

IActivationFactory *webauthcore_manager_factory = &webauthcore_statics.IActivationFactory_iface;
