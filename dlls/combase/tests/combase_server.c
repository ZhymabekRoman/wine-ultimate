/*
 * Copyright 2026 Zhymabek Roman
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

#if 0
#pragma makedep baseexe
#endif

/*
 * Out-of-proc activation-factory server for the combase roapi conformance test.
 *
 * RoGetActivationFactory resolves a class that carries an ActivatableClassId\<class>\Server
 * subkey through CoCreateInstance(CLSCTX_LOCAL_SERVER) on the CLSID recorded in that
 * subkey's default value.  CoCreateInstance starts this server (the CLSID's LocalServer32,
 * with the -Embedding /PROCESSID:{GUID} --clsid={GUID} switches combase's create_server
 * appends) and polls rpcss for the class object this process registers.  The class object's
 * CreateInstance hands back an object that answers IActivationFactory, which is what
 * RoGetActivationFactory returns to the caller.  This is the shape of the {daa52b27-...}
 * Microsoft.Web.WebView2.Core.CoreWebView2CompositionController server activation, and the
 * Store/packaged (apprepository) activation path it generalizes to.
 *
 * This unit is the server half; the client (test_server_class_activation in roapi.c)
 * registers LocalServer32 = "<combase_test.exe> combase_server -server".
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"

#include "objidl.h"
#include "inspectable.h"
#include "roapi.h"
#include "winstring.h"

#include "wine/debug.h"
#include "wine/test.h"

WINE_DEFAULT_DEBUG_CHANNEL(combase);

/* The CLSID the roapi test seeds in the class's Server subkey and in LocalServer32.
 * A file-scope const (not initguid) so this unit's standard IIDs stay extern (libuuid). */
static const GUID CLSID_WineTestServerClass =
    {0x6c1f3a2d, 0x9b4e, 0x4f70, {0xa2, 0x1c, 0x7e, 0x5d, 0x3b, 0x0a, 0x9f, 0x64}};

/* The instance the class factory's CreateInstance returns: it answers the activation
 * factory interfaces the caller QIs, standing in for the server-hosted factory. */
struct activation_instance
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct activation_instance *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct activation_instance, IActivationFactory_iface );
}

static HRESULT WINAPI instance_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct activation_instance *impl = impl_from_IActivationFactory( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( iface );
        *out = impl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMarshal ))
    {
        /* The COM proxy the client builds for this cross-apartment object asks it
         * for the standard marshaler; answering it is what lets the proxy's
         * IActivationFactory stubs reach this object (Windows does the same for
         * the server-hosted activation factories). */
        return CoGetStandardMarshal( &IID_IActivationFactory, (IUnknown *)iface, NULL, NULL, 0, (IMarshal **)out );
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI instance_AddRef( IActivationFactory *iface )
{
    struct activation_instance *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI instance_Release( IActivationFactory *iface )
{
    struct activation_instance *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI instance_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI instance_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Wine.Test.ServerClass", 20, name );
}

static HRESULT WINAPI instance_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI instance_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IActivationFactory_AddRef( iface );
    *instance = (IInspectable *)iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl instance_vtbl =
{
    instance_QueryInterface,
    instance_AddRef,
    instance_Release,
    instance_GetIids,
    instance_GetRuntimeClassName,
    instance_GetTrustLevel,
    instance_ActivateInstance,
};

static struct activation_instance activation_instance = { { &instance_vtbl }, 1 };

/* The class object CoRegisterClassObject registers; its CreateInstance returns the
 * activation instance above. */
struct class_factory
{
    IClassFactory IClassFactory_iface;
    LONG ref;
};

static inline struct class_factory *impl_from_IClassFactory( IClassFactory *iface )
{
    return CONTAINING_RECORD( iface, struct class_factory, IClassFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IClassFactory *iface, REFIID iid, void **out )
{
    struct class_factory *impl = impl_from_IClassFactory( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IClassFactory ))
    {
        IClassFactory_AddRef( iface );
        *out = impl;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IClassFactory *iface )
{
    struct class_factory *impl = impl_from_IClassFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( IClassFactory *iface )
{
    struct class_factory *impl = impl_from_IClassFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI factory_CreateInstance( IClassFactory *iface, IUnknown *outer, REFIID iid, void **out )
{
    return IActivationFactory_QueryInterface( &activation_instance.IActivationFactory_iface, iid, out );
}

static HRESULT WINAPI factory_LockServer( IClassFactory *iface, BOOL lock )
{
    return S_OK;
}

static const struct IClassFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_CreateInstance,
    factory_LockServer,
};

static struct class_factory class_factory = { { &factory_vtbl }, 1 };

START_TEST( combase_server )
{
    HANDLE event;
    char **argv;
    int argc;
    DWORD key;
    HRESULT hr;

    argc = winetest_get_mainargs( &argv );
    if (argc < 3 || strcmp( argv[2], "-server" )) return;

    CoInitializeEx( NULL, COINIT_MULTITHREADED );

    hr = CoRegisterClassObject( &CLSID_WineTestServerClass, (IUnknown *)&class_factory.IClassFactory_iface,
                                CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &key );
    if (FAILED( hr ))
    {
        WINE_FIXME( "CoRegisterClassObject returned %#lx.\n", hr );
        return;
    }

    /* Keep the class object registered until the client signals this event, capped so a
     * hung client cannot pin the server; REGCLS_MULTIPLEUSE spans activations. */
    event = CreateEventA( NULL, TRUE, FALSE, "wine_combase_test_server_done" );
    if (event) WaitForSingleObject( event, 30000 );

    CoRevokeClassObject( key );
    CoUninitialize();
}
