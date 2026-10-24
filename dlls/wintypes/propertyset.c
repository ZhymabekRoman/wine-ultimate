/*
 * Copyright 2024 Vibhav Pant
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

#define COBJMACROS
#include <stdarg.h>
#include <string.h>

#include <winstring.h>
#include <objbase.h>
#include <wine/debug.h>
#include <activation.h>

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL( wintypes );

struct propertyset
{
    IPropertySet IPropertySet_iface;
    IObservableMap_HSTRING_IInspectable IObservableMap_HSTRING_IInspectable_iface;
    LONG ref;

    IInspectable *map_inner;
};

static inline struct propertyset *impl_from_IPropertySet( IPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct propertyset, IPropertySet_iface );
}

static HRESULT STDMETHODCALLTYPE propertyset_QueryInterface( IPropertySet *iface, REFIID iid, void **out )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertySet ))
    {
        *out = iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IObservableMap_HSTRING_IInspectable ))
    {
        *out = &impl->IObservableMap_HSTRING_IInspectable_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    if (SUCCEEDED(IInspectable_QueryInterface( impl->map_inner, iid, out ))) return S_OK;

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE propertyset_AddRef( IPropertySet *iface )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG STDMETHODCALLTYPE propertyset_Release( IPropertySet *iface )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        /* guard against re-entry if inner releases an outer iface */
        InterlockedIncrement( &impl->ref );
        IInspectable_Release( impl->map_inner );
        free( impl );
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE propertyset_GetIids( IPropertySet *iface, ULONG *iid_count, IID **iids )
{
    static const IID instance_iids[] =
    {
        IID_IPropertySet,
        IID_IMap_HSTRING_IInspectable,
        IID_IMapView_HSTRING_IInspectable,
        IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
        IID_IIterator_IKeyValuePair_HSTRING_IInspectable,
        IID_IObservableMap_HSTRING_IInspectable,
    };
    IID *array = CoTaskMemAlloc( sizeof(instance_iids) );

    TRACE( "(%p, %p, %p)\n", iface, iid_count, iids );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, instance_iids, sizeof(instance_iids) );
    *iid_count = ARRAY_SIZE(instance_iids);
    *iids = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE propertyset_GetRuntimeClassName( IPropertySet *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";

    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT STDMETHODCALLTYPE propertyset_GetTrustLevel( IPropertySet *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static const IPropertySetVtbl propertyset_vtbl =
{
    /* IUnknown */
    propertyset_QueryInterface,
    propertyset_AddRef,
    propertyset_Release,
    /* IInspectable */
    propertyset_GetIids,
    propertyset_GetRuntimeClassName,
    propertyset_GetTrustLevel,
};

DEFINE_IINSPECTABLE( propertyset_IObservableMap, IObservableMap_HSTRING_IInspectable, struct propertyset, IPropertySet_iface );

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_add_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                                            IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                                            EventRegistrationToken *token )
{
    FIXME( "(%p, %p, %p) stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_remove_MapChanged( IObservableMap_HSTRING_IInspectable *iface, EventRegistrationToken token )
{
    FIXME( "(%p, %I64d) stub!\n", iface, token.value );
    return E_NOTIMPL;
}

const static IObservableMap_HSTRING_IInspectableVtbl propertyset_IObservableMap_vtbl =
{
    /* IUnknown */
    propertyset_IObservableMap_QueryInterface,
    propertyset_IObservableMap_AddRef,
    propertyset_IObservableMap_Release,
    /* IInspectable */
    propertyset_IObservableMap_GetIids,
    propertyset_IObservableMap_GetRuntimeClassName,
    propertyset_IObservableMap_GetTrustLevel,
    /* IObservableMap<HSTRING, IInspectable*> */
    propertyset_IObservableMap_add_MapChanged,
    propertyset_IObservableMap_remove_MapChanged,
};

struct propertyset_factory
{
    IActivationFactory IActivationFactory_iface;
    IPropertySet IPropertySet_iface;
    IObservableMap_HSTRING_IInspectable IObservableMap_HSTRING_IInspectable_iface;
    LONG ref;
};

static inline struct propertyset_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct propertyset_factory, IActivationFactory_iface );
}

static HRESULT STDMETHODCALLTYPE factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );
    *out = NULL;
    if (IsEqualGUID( &IID_IUnknown, iid ) ||
        IsEqualGUID( &IID_IInspectable, iid ) ||
        IsEqualGUID( &IID_IAgileObject, iid ) ||
        IsEqualGUID( &IID_IActivationFactory, iid ))
    {
        *out = iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    /* Windows aggregates the class default interface and implemented interfaces
     * on the factory object (CsWinRT probes the factory for the class interfaces). */
    if (IsEqualGUID( &IID_IPropertySet, iid ))
    {
        struct propertyset_factory *impl = impl_from_IActivationFactory( iface );
        *out = &impl->IPropertySet_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    if (IsEqualGUID( &IID_IObservableMap_HSTRING_IInspectable, iid ))
    {
        struct propertyset_factory *impl = impl_from_IActivationFactory( iface );
        *out = &impl->IObservableMap_HSTRING_IInspectable_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, return E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_AddRef( IActivationFactory *iface )
{
    struct propertyset_factory *impl = impl_from_IActivationFactory( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG STDMETHODCALLTYPE factory_Release( IActivationFactory *iface )
{
    struct propertyset_factory *impl = impl_from_IActivationFactory( iface );
    TRACE( "(%p)\n", iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT STDMETHODCALLTYPE factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IPropertySet,
        IID_IObservableMap_HSTRING_IInspectable,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    TRACE( "(%p, %p, %p)\n", iface, iid_count, iids );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";

    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT STDMETHODCALLTYPE factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    static const struct map_iids iids =
    {
        .map = &IID_IMap_HSTRING_IInspectable,
        .view = &IID_IMapView_HSTRING_IInspectable,
        .iterable = &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
        .iterator = &IID_IIterator_IKeyValuePair_HSTRING_IInspectable,
        .pair = &IID_IKeyValuePair_HSTRING_IInspectable,
    };

    struct propertyset *impl;
    HRESULT hr;

    TRACE( "(%p, %p)\n", iface, instance );

    impl = calloc( 1, sizeof( *impl ) );
    if (!impl)
        return E_OUTOFMEMORY;

    impl->IPropertySet_iface.lpVtbl = &propertyset_vtbl;
    impl->IObservableMap_HSTRING_IInspectable_iface.lpVtbl = &propertyset_IObservableMap_vtbl;
    impl->ref = 1;

    if (FAILED(hr = multi_threaded_map_create( &iids, (IInspectable *)&impl->IPropertySet_iface, &impl->map_inner )))
    {
        free( impl );
        return hr;
    }

    *instance = (IInspectable *)&impl->IPropertySet_iface;
    return S_OK;
}

static const IActivationFactoryVtbl propertyset_factory_vtbl =
{
    /* IUnknown */
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory */
    factory_ActivateInstance,
};

/*
 * Windows aggregates the class default interface (the IPropertySet marker) and
 * its implemented interfaces on the factory object: QI for those IIDs on the
 * factory succeeds (CsWinRT probes the factory for the class interfaces). The
 * factory is not an instance; instance method calls fail.
 */
DEFINE_IINSPECTABLE_( propertyset_factory_def, IPropertySet, struct propertyset_factory,
                      impl_from_IPropertySet_propertyset_factory, IPropertySet_iface,
                      &impl->IActivationFactory_iface )

static const IPropertySetVtbl propertyset_factory_def_vtbl =
{
    propertyset_factory_def_QueryInterface,
    propertyset_factory_def_AddRef,
    propertyset_factory_def_Release,
    propertyset_factory_def_GetIids,
    propertyset_factory_def_GetRuntimeClassName,
    propertyset_factory_def_GetTrustLevel,
};

DEFINE_IINSPECTABLE_( propertyset_factory_obsm, IObservableMap_HSTRING_IInspectable, struct propertyset_factory,
                      impl_from_IObservableMap_HSTRING_IInspectable_propertyset_factory,
                      IObservableMap_HSTRING_IInspectable_iface, &impl->IActivationFactory_iface )

static HRESULT STDMETHODCALLTYPE propertyset_factory_obsm_add_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
        IMapChangedEventHandler_HSTRING_IInspectable *handler, EventRegistrationToken *token )
{
    return E_FAIL;
}

static HRESULT STDMETHODCALLTYPE propertyset_factory_obsm_remove_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
        EventRegistrationToken token )
{
    return E_FAIL;
}

static const IObservableMap_HSTRING_IInspectableVtbl propertyset_factory_obsm_vtbl =
{
    propertyset_factory_obsm_QueryInterface,
    propertyset_factory_obsm_AddRef,
    propertyset_factory_obsm_Release,
    propertyset_factory_obsm_GetIids,
    propertyset_factory_obsm_GetRuntimeClassName,
    propertyset_factory_obsm_GetTrustLevel,
    propertyset_factory_obsm_add_MapChanged,
    propertyset_factory_obsm_remove_MapChanged,
};

static struct propertyset_factory propertyset_factory =
{
    {&propertyset_factory_vtbl},
    {&propertyset_factory_def_vtbl},
    {&propertyset_factory_obsm_vtbl},
    1
};

IActivationFactory *property_set_factory = &propertyset_factory.IActivationFactory_iface;
