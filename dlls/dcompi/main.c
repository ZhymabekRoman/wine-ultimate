/*
 * dcompi.dll: the Microsoft.WindowsAppRuntime composition provider.
 *
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

/*
 * dcompi.dll is the provider Microsoft.WindowsAppRuntime ships for the
 * Microsoft.UI.Composition namespace (192 activatable classes in its WRL creator map).
 * This module implements the one Microsoft.ui.xaml.dll needs to bring up its composition
 * tree, Microsoft.UI.Composition.Compositor, whose activation Microsoft.ui.xaml.dll
 * FailFasts on (ActivationFactoryCache::GetInteropCompositorFactoryPartner 0x18023586c,
 * DCompTreeHost::CreateWinRTInteropCompositionDevice 0x1802357b8).
 *
 * Class dispatch mirrors the real DllGetActivationFactory (dcompi 0x180046970): look the
 * class id up, hand out the cached factory instance (WRL Details::GetCacheEntry), so the
 * FtmBase factory keeps one identity per class for the life of the process.  Classes in the
 * namespace that have no Wine implementation get a factory that resolves the activation -
 * XAML's requests stop being process-fatal - and answers E_NOTIMPL with a fixme naming the
 * class, so the gate log says exactly what is still missing.
 */

#include "initguid.h"
#include "dcompi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcompi);

const WCHAR dcompi_compositor_class[] = L"Microsoft.UI.Composition.Compositor";
const WCHAR dcompi_namespace[] = L"Microsoft.UI.Composition.";

/*
 * A resolving stub factory for the remaining Microsoft.UI.Composition classes.  One
 * instance per class name, cached for the life of the process, so the activation identity
 * is stable the way GetCacheEntry makes the real one.
 */
struct stub_factory
{
    struct IActivationFactory IActivationFactory_iface;
    const WCHAR *class_name;
    LONG ref;
};

#define STUB_CACHE_SIZE 32
static struct stub_factory *stub_cache[STUB_CACHE_SIZE];
static CRITICAL_SECTION stub_cache_cs;
static INIT_ONCE stub_cache_once = INIT_ONCE_STATIC_INIT;

static const struct IActivationFactoryVtbl stub_factory_vtbl;

static inline struct stub_factory *stub_from_IActivationFactory( struct IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct stub_factory, IActivationFactory_iface );
}

static BOOL CALLBACK stub_cache_init( PINIT_ONCE init_once, PVOID parameter, PVOID *context )
{
    InitializeCriticalSection( &stub_cache_cs );
    return TRUE;
}

static struct stub_factory *stub_factory_get( const WCHAR *class_name )
{
    struct stub_factory *stub;
    size_t len;
    unsigned int i;

    InitOnceExecuteOnce( &stub_cache_once, stub_cache_init, NULL, NULL );
    EnterCriticalSection( &stub_cache_cs );

    for (i = 0; i < STUB_CACHE_SIZE; i++)
    {
        if (stub_cache[i] && !wcscmp( stub_cache[i]->class_name, class_name ))
        {
            LeaveCriticalSection( &stub_cache_cs );
            return stub_cache[i];
        }
    }

    len = (lstrlenW( class_name ) + 1) * sizeof(WCHAR);
    if ((stub = calloc( 1, sizeof(*stub) + len )))
    {
        WCHAR *name = (WCHAR *)((char *)stub + sizeof(*stub));
        memcpy( name, class_name, len );
        stub->class_name = name;
        stub->IActivationFactory_iface.lpVtbl = &stub_factory_vtbl;
        stub->ref = 1;
        for (i = 0; i < STUB_CACHE_SIZE; i++)
        {
            if (!stub_cache[i])
            {
                stub_cache[i] = stub;
                break;
            }
        }
        if (i == STUB_CACHE_SIZE)
        {
            free( stub );
            stub = NULL;
        }
    }

    LeaveCriticalSection( &stub_cache_cs );
    return stub;
}

static HRESULT STDMETHODCALLTYPE stub_QueryInterface( struct IActivationFactory *iface, REFIID iid, void **out )
{
    struct stub_factory *impl = stub_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented for %s, returning E_NOINTERFACE.\n", debugstr_guid( iid ),
            debugstr_w( impl->class_name ) );
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE stub_AddRef( struct IActivationFactory *iface )
{
    return InterlockedIncrement( &stub_from_IActivationFactory( iface )->ref );
}

static ULONG STDMETHODCALLTYPE stub_Release( struct IActivationFactory *iface )
{
    /* Cached for the life of the process, like the WRL module cache. */
    return InterlockedDecrement( &stub_from_IActivationFactory( iface )->ref );
}

static HRESULT STDMETHODCALLTYPE stub_GetIids( struct IActivationFactory *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE stub_GetRuntimeClassName( struct IActivationFactory *iface, HSTRING *name )
{
    struct stub_factory *impl = stub_from_IActivationFactory( iface );
    return WindowsCreateString( impl->class_name, lstrlenW( impl->class_name ), name );
}

static HRESULT STDMETHODCALLTYPE stub_GetTrustLevel( struct IActivationFactory *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE stub_ActivateInstance( struct IActivationFactory *iface,
        IInspectable **instance )
{
    struct stub_factory *impl = stub_from_IActivationFactory( iface );

    FIXME( "class %s has no implementation in Wine, ActivateInstance returns E_NOTIMPL.\n",
            debugstr_w( impl->class_name ) );

    if (instance) *instance = NULL;
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl stub_factory_vtbl =
{
    stub_QueryInterface,
    stub_AddRef,
    stub_Release,
    stub_GetIids,
    stub_GetRuntimeClassName,
    stub_GetTrustLevel,
    stub_ActivateInstance,
};

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_OK;
}

/*
 * The real DllGetClassObject (dcompi 0x180045a30) resolves a CLSID through the WRL module's
 * creator map, whose CLSIDs come from the WinRT metadata of the framework package, and hands
 * out the cached factory for it.  This module activates through the WinRT path
 * (DllGetActivationFactory, driven by the ActivatableClassId registry key) and carries no
 * metadata, so the class objects it cannot name are reported instead of guessed.
 */
HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p: no class-object CLSID table for %s, "
            "activating through DllGetActivationFactory.\n", debugstr_guid( clsid ),
            debugstr_guid( riid ), out, debugstr_w( dcompi_compositor_class ) );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;
    if (!name) return E_INVALIDARG;

    if (!wcscmp( name, dcompi_compositor_class ))
    {
        IActivationFactory_QueryInterface( compositor_factory_get(), &IID_IActivationFactory,
                (void **)factory );
        return *factory ? S_OK : E_OUTOFMEMORY;
    }

    if (!wcsncmp( name, dcompi_namespace, lstrlenW( dcompi_namespace ) ))
    {
        struct stub_factory *stub = stub_factory_get( name );

        FIXME( "class %s is resolved to a stub factory: it activates, its methods are "
                "E_NOTIMPL.\n", debugstr_w( name ) );

        if (stub)
        {
            IActivationFactory_QueryInterface( &stub->IActivationFactory_iface,
                    &IID_IActivationFactory, (void **)factory );
            return *factory ? S_OK : E_OUTOFMEMORY;
        }
        return E_OUTOFMEMORY;
    }

    FIXME( "class %s is not in this module, returning E_NOINTERFACE.\n", debugstr_w( name ) );
    return E_NOINTERFACE;
}

static HRESULT register_class( const WCHAR *class_name, const WCHAR *dll_path )
{
    HKEY hkey;
    LONG ret;

    ret = RegCreateKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId",
            0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hkey, NULL );
    if (ret) return HRESULT_FROM_WIN32( ret );

    ret = RegCreateKeyExW( hkey, class_name, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hkey, NULL );
    if (ret)
    {
        RegCloseKey( hkey );
        return HRESULT_FROM_WIN32( ret );
    }
    ret = RegSetValueExW( hkey, L"DllPath", 0, REG_EXPAND_SZ, (const BYTE *)dll_path,
            (lstrlenW( dll_path ) + 1) * sizeof(WCHAR) );
    if (!ret)
    {
        static const DWORD threading_model_both = 2;
        ret = RegSetValueExW( hkey, L"ThreadingModel", 0, REG_DWORD, (const BYTE *)&threading_model_both,
                sizeof(threading_model_both) );
    }
    RegCloseKey( hkey );
    return HRESULT_FROM_WIN32( ret );
}

HRESULT WINAPI DllRegisterServer( void )
{
    static const WCHAR dll_path[] = L"C:\\windows\\system32\\dcompi.dll";

    TRACE( "\n" );
    return register_class( dcompi_compositor_class, dll_path );
}

HRESULT WINAPI DllUnregisterServer( void )
{
    static const WCHAR path[] =
        L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Microsoft.UI.Composition.Compositor";

    TRACE( "\n" );
    return HRESULT_FROM_WIN32( RegDeleteKeyW( HKEY_LOCAL_MACHINE, path ) );
}
