/* WinRT Windows.Management.Deployment Implementation
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

#include "objbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

/* The class id Windows.Management.Deployment.PackageManager is activated by
 * the classic CoCreateInstance(CLSID) path (the WinRT path reaches the same
 * instance through DllGetActivationFactory). */
DEFINE_GUID(CLSID_PackageManager, 0x4df1538a, 0x1819, 0x4822, 0x81, 0xa4, 0x4d, 0xf6, 0x79, 0x5e, 0xa7, 0x89);

struct package_manager_classfactory
{
    IClassFactory IClassFactory_iface;
    LONG ref;
};

static struct package_manager_classfactory classfactory = { { NULL }, 1 };

static HRESULT WINAPI classfactory_QueryInterface( IClassFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IClassFactory ))
    {
        *out = &classfactory.IClassFactory_iface;
        InterlockedIncrement( &classfactory.ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI classfactory_AddRef( IClassFactory *iface )
{
    return InterlockedIncrement( &classfactory.ref );
}

static ULONG WINAPI classfactory_Release( IClassFactory *iface )
{
    return InterlockedDecrement( &classfactory.ref );
}

static HRESULT WINAPI classfactory_CreateInstance( IClassFactory *iface, IUnknown *outer, REFIID riid, void **out )
{
    IPackageManager *pm;
    HRESULT hr;

    TRACE( "iface %p, outer %p, riid %s, out %p.\n", iface, outer, debugstr_guid(riid), out );

    if (outer && outer != (IUnknown *)&classfactory) return CLASS_E_NOAGGREGATION;
    if (!out) return E_INVALIDARG;
    if (FAILED( (hr = package_manager_create( &pm ) ) )) return hr;

    if (IsEqualGUID( riid, &IID_IPackageManager ))
    {
        *out = pm;
        return S_OK;
    }
    hr = IPackageManager_QueryInterface( pm, riid, out );
    IPackageManager_Release( pm );
    return hr;
}

static HRESULT WINAPI classfactory_LockServer( IClassFactory *iface, BOOL lock )
{
    return S_OK;
}

static const struct IClassFactoryVtbl classfactory_vtbl =
{
    classfactory_QueryInterface,
    classfactory_AddRef,
    classfactory_Release,
    classfactory_CreateInstance,
    classfactory_LockServer,
};

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    TRACE( "clsid %s, riid %s, out %p.\n", debugstr_guid(clsid), debugstr_guid(riid), out );

    *out = NULL;
    if (!IsEqualGUID( clsid, &CLSID_PackageManager )) return CLASS_E_CLASSNOTAVAILABLE;
    classfactory.IClassFactory_iface.lpVtbl = &classfactory_vtbl;
    return IClassFactory_QueryInterface( &classfactory.IClassFactory_iface, riid, out );
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Management_Deployment_PackageManager ))
        IActivationFactory_QueryInterface( package_manager_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
