/* WinRT Windows.Storage.ApplicationData Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(data);

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

static HRESULT WINAPI composite_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI composite_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI composite_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI composite_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI composite_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Storage.ApplicationDataCompositeValue", 45, name );
}
static HRESULT WINAPI composite_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI composite_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    HSTRING cls;
    HRESULT hr;
    static const WCHAR propset[] = L"Windows.Foundation.Collections.PropertySet";

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (FAILED(hr = WindowsCreateString( propset, ARRAY_SIZE(propset) - 1, &cls )))
        return hr;
    hr = RoActivateInstance( cls, instance );
    WindowsDeleteString( cls );
    return hr;
}

static const struct IActivationFactoryVtbl composite_factory_vtbl =
{
    composite_factory_QueryInterface,
    composite_factory_AddRef,
    composite_factory_Release,
    composite_factory_GetIids,
    composite_factory_GetRuntimeClassName,
    composite_factory_GetTrustLevel,
    composite_factory_ActivateInstance,
};
static IActivationFactory composite_factory = { &composite_factory_vtbl };

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    /* Probe 11 (s249): seed all provider head blocks ahead of the first QI
     * cascade (first-touch entry point of this provider). */
    application_data_preseed();

    *factory = NULL;

    /* Microsoft.Windows.Storage.ApplicationData* is the Windows App SDK mirror of
     * this class (the same Current/LocalSettings surface, hosted by
     * Microsoft.WindowsAppRuntime.dll on Windows); combase routes the namespace here
     * and the packaged prefix's ActivatableClassId seed names this module for it.
     * The mirror's interface layouts extend the UWP ones, so the provider answers. */
    if (!wcscmp( buffer, RuntimeClass_Windows_Storage_ApplicationData ) ||
        !wcscmp( buffer, L"Microsoft.Windows.Storage.ApplicationData" ))
        IActivationFactory_QueryInterface( application_data_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.Storage.ApplicationDataCompositeValue" ) ||
             !wcscmp( buffer, L"Microsoft.Windows.Storage.ApplicationDataCompositeValue" ))
        *factory = &composite_factory;

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
