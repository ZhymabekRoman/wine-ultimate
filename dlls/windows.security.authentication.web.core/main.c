/* WinRT Windows.Security.Authentication.Web.Core Implementation
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
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(webauthcore);

/*
 * Activation factory of the classes of this namespace that have an
 * instantiable form (WebTokenRequest, WebTokenRequestResult, WebTokenResponse,
 * WebProviderError, WebAccountProvider, WebAccount).  combase hands the class
 * name to DllGetActivationFactory and the app then activates an instance
 * through IActivationFactory::ActivateInstance.
 */

struct class_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
    IInspectable *(*create)( void );
};

static inline struct class_factory *impl_from_class_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct class_factory, IActivationFactory_iface );
}

static HRESULT WINAPI class_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct class_factory *impl = impl_from_class_factory( iface );

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

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef( IActivationFactory *iface )
{
    struct class_factory *impl = impl_from_class_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI class_factory_Release( IActivationFactory *iface )
{
    struct class_factory *impl = impl_from_class_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI class_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI class_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI class_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI class_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct class_factory *impl = impl_from_class_factory( iface );
    IInspectable *obj;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (!(obj = impl->create())) return E_OUTOFMEMORY;

    *instance = obj;
    return S_OK;
}

static const struct IActivationFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_GetIids,
    class_factory_GetRuntimeClassName,
    class_factory_GetTrustLevel,
    class_factory_ActivateInstance,
};

#define DEFINE_CLASS_FACTORY( name, create_func )                                                  \
    static struct class_factory name = { { &class_factory_vtbl }, 1, create_func }

DEFINE_CLASS_FACTORY( web_token_request_factory, create_web_token_request );
DEFINE_CLASS_FACTORY( web_token_request_result_factory, create_web_token_request_result );
DEFINE_CLASS_FACTORY( web_token_response_factory, create_web_token_response );
DEFINE_CLASS_FACTORY( web_provider_error_factory, create_web_provider_error );
DEFINE_CLASS_FACTORY( web_account_provider_factory, create_web_account_provider );
DEFINE_CLASS_FACTORY( web_account_factory, create_web_account );

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_Security_Authentication_Web_Core_WebAuthenticationCoreManager ))
        IActivationFactory_QueryInterface( webauthcore_manager_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_Security_Authentication_Web_Core_WebTokenRequest ))
        IActivationFactory_QueryInterface( &web_token_request_factory.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_Security_Authentication_Web_Core_WebTokenRequestResult ))
        IActivationFactory_QueryInterface( &web_token_request_result_factory.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_Security_Authentication_Web_Core_WebTokenResponse ))
        IActivationFactory_QueryInterface( &web_token_response_factory.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_Security_Authentication_Web_Core_WebProviderError ))
        IActivationFactory_QueryInterface( &web_provider_error_factory.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_Security_Credentials_WebAccountProvider ))
        IActivationFactory_QueryInterface( &web_account_provider_factory.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_Security_Credentials_WebAccount ))
        IActivationFactory_QueryInterface( &web_account_factory.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}
