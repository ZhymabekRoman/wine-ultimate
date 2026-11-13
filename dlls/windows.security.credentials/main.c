/* WinRT Windows.Security.Credentials Implementation
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

#include "initguid.h" /* sole IID definition site in the DLL */
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(creds);

/*
 * Activation factory for the instantiable form of PasswordVault: combase
 * hands the class name to DllGetActivationFactory and the app creates the
 * vault through IActivationFactory::ActivateInstance.
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
    if (!trust_level) return E_POINTER;
    *trust_level = FullTrust;
    return S_OK;
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

/*
 * The PasswordVault activation factory is also the class's default instance:
 * RoGetActivationFactory(PasswordVault, IID_IPasswordVault) reaches a vault
 * object through this factory's QueryInterface, matching the Windows behavior
 * for default-constructor classes (the vault is a stateless view over the
 * credential manager, so a fresh vault object per query is safe).
 */
static HRESULT WINAPI vault_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
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

    if (IsEqualGUID( iid, &IID_IPasswordVault ))
    {
        IInspectable *obj = impl->create();
        HRESULT hr;

        if (!obj) return E_OUTOFMEMORY;
        hr = IInspectable_QueryInterface( obj, iid, out );
        IInspectable_Release( obj );
        return hr;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static const struct IActivationFactoryVtbl vault_factory_vtbl =
{
    vault_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_GetIids,
    class_factory_GetRuntimeClassName,
    class_factory_GetTrustLevel,
    class_factory_ActivateInstance,
};

#define DEFINE_CLASS_FACTORY( name, create_func )                                                  \
    static struct class_factory name = { { &class_factory_vtbl }, 1, create_func }

/*
 * PasswordCredential is a composable class: its activation factory doubles
 * as IPasswordCredentialFactory, the constructor surface the C++/C#
 * projections call for new PasswordCredential(resource, user[, password]).
 * A single static object answers both interfaces (shared identity), like
 * windows.networking.hostname.dll does for HostName/IHostNameFactory.
 */
struct password_credential_statics
{
    IActivationFactory IActivationFactory_iface;
    IPasswordCredentialFactory IPasswordCredentialFactory_iface;
    LONG ref;
};

static inline struct password_credential_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct password_credential_statics, IActivationFactory_iface );
}

static HRESULT WINAPI credential_statics_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct password_credential_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IPasswordCredentialFactory ))
    {
        *out = &impl->IPasswordCredentialFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI credential_statics_AddRef( IActivationFactory *iface )
{
    struct password_credential_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI credential_statics_Release( IActivationFactory *iface )
{
    struct password_credential_statics *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI credential_statics_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
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

static HRESULT WINAPI credential_statics_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Security_Credentials_PasswordCredential,
                                sizeof(RuntimeClass_Windows_Security_Credentials_PasswordCredential) / sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI credential_statics_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI credential_statics_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IPasswordCredential *credential;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (FAILED(hr = create_password_credential( L"", L"", L"", &credential ))) return hr;

    *instance = (IInspectable *)credential;
    return S_OK;
}

static const struct IActivationFactoryVtbl credential_statics_activation_vtbl =
{
    credential_statics_QueryInterface,
    credential_statics_AddRef,
    credential_statics_Release,
    credential_statics_GetIids,
    credential_statics_GetRuntimeClassName,
    credential_statics_GetTrustLevel,
    credential_statics_ActivateInstance,
};

DEFINE_IINSPECTABLE( credential_factory, IPasswordCredentialFactory, struct password_credential_statics,
                     IActivationFactory_iface )

static HRESULT WINAPI credential_factory_CreatePasswordCredentialWithDefaultPassword(
        IPasswordCredentialFactory *iface, HSTRING resource, HSTRING userName, IPasswordCredential **value )
{
    const WCHAR *resource_buf, *user_buf;

    TRACE( "iface %p, resource %s, userName %s, value %p.\n", iface, debugstr_hstring( resource ),
           debugstr_hstring( userName ), value );

    if (!value) return E_POINTER;
    resource_buf = WindowsGetStringRawBuffer( resource, NULL );
    user_buf = WindowsGetStringRawBuffer( userName, NULL );
    return create_password_credential( resource_buf, user_buf, L"", value );
}

static HRESULT WINAPI credential_factory_CreatePasswordCredential(
        IPasswordCredentialFactory *iface, HSTRING resource, HSTRING userName, HSTRING password,
        IPasswordCredential **value )
{
    const WCHAR *resource_buf, *user_buf, *password_buf;

    TRACE( "iface %p, resource %s, userName %s, value %p.\n", iface, debugstr_hstring( resource ),
           debugstr_hstring( userName ), value );

    if (!value) return E_POINTER;
    resource_buf = WindowsGetStringRawBuffer( resource, NULL );
    user_buf = WindowsGetStringRawBuffer( userName, NULL );
    password_buf = WindowsGetStringRawBuffer( password, NULL );
    return create_password_credential( resource_buf, user_buf, password_buf, value );
}

static const struct IPasswordCredentialFactoryVtbl credential_factory_vtbl =
{
    credential_factory_QueryInterface,
    credential_factory_AddRef,
    credential_factory_Release,
    credential_factory_GetIids,
    credential_factory_GetRuntimeClassName,
    credential_factory_GetTrustLevel,
    credential_factory_CreatePasswordCredentialWithDefaultPassword,
    credential_factory_CreatePasswordCredential,
};

static struct password_credential_statics password_credential_statics =
{
    { &credential_statics_activation_vtbl },
    { &credential_factory_vtbl },
    1,
};

IActivationFactory *password_credential_factory = &password_credential_statics.IActivationFactory_iface;

static struct class_factory vault_factory_impl = { { &vault_factory_vtbl }, 1, create_password_vault };

IActivationFactory *password_vault_factory = &vault_factory_impl.IActivationFactory_iface;

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_Security_Credentials_PasswordVault ))
        IActivationFactory_QueryInterface( password_vault_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_Security_Credentials_PasswordCredential ))
        IActivationFactory_QueryInterface( password_credential_factory, &IID_IActivationFactory, (void **)factory );

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
