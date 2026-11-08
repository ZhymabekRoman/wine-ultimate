/* WinRT Windows.Storage.ApplicationData ApplicationData Implementation
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

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(data);

/* CoreCLR's ComWrappers shader reads IInspectable::GetIids output as an array of
 * ComInterfaceEntry pairs (vtable-first layout, matching the runtime's
 * sequential struct { void** Vtable; Guid* IID; }). */
struct com_interface_entry
{
    void **vtable;
    IID   *iid;
};

struct application_data_statics
{
    IActivationFactory IActivationFactory_iface;
    IApplicationDataStatics IApplicationDataStatics_iface;
    LONG ref;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
};

static inline struct application_data_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationDataStatics ))
    {
        *out = &impl->IApplicationDataStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static const struct IActivationFactoryVtbl factory_vtbl;
static const struct IApplicationDataStaticsVtbl application_data_statics_vtbl;

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    /* CoreCLR's ComWrappers shader reads the output as ComInterfaceEntry pairs
     * (vtable-first); with E_NOTIMPL it falls back and the first RCW built in
     * the process (this activation factory face) seeds the shader's per-IID-set
     * cache with a bare layout, flowing into get_Values slot decode. Hand back
     * one stable pair array per singleton: {factory vtbl, IActivationFactory} +
     * {statics vtbl, IApplicationDataStatics}. */
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );
    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (iid_count) *iid_count = 2; /* filled on the (NULL, NULL) probe itself */
    if (!impl->iids_cache)
    {
        struct com_interface_entry entries[] =
        {
            { (void **)&factory_vtbl, (IID *)&IID_IActivationFactory },
            { (void **)&application_data_statics_vtbl, (IID *)&IID_IApplicationDataStatics },
        };
        struct com_interface_entry *array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        impl->iids_cache = array;
        impl->iids_count = ARRAY_SIZE( entries );
    }

    if (iid_count) *iid_count = impl->iids_count;
    if (iids) *iids = (IID *)impl->iids_cache;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

struct application_data
{
    IApplicationData IApplicationData_iface;
    IApplicationData2 IApplicationData2_iface;
    IApplicationDataMethods IApplicationDataMethods_iface;
    LONG ref;
};

static inline struct application_data *impl_from_IApplicationData( IApplicationData *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData_iface );
}

static inline struct application_data *impl_from_IApplicationData2( IApplicationData2 *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData2_iface );
}

static inline struct application_data *impl_from_IApplicationDataMethods( IApplicationDataMethods *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationDataMethods_iface );
}

static const struct IApplicationDataVtbl application_data_vtbl;
static const struct IApplicationDataMethodsVtbl application_data_methods_vtbl;

static HRESULT WINAPI application_data_QueryInterface( IApplicationData *iface, REFIID iid, void **out )
{
    struct application_data *impl = impl_from_IApplicationData( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplicationData ))
    {
        *out = &impl->IApplicationData_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationData2 ))
    {
        *out = &impl->IApplicationData2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationDataMethods ))
    {
        *out = &impl->IApplicationDataMethods_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI application_data_AddRef( IApplicationData *iface )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI application_data_Release( IApplicationData *iface )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI application_data_GetIids( IApplicationData *iface, ULONG *iid_count, IID **iids )
{
    /* Shader pair-array contract, see container_GetIids: with E_NOTIMPL the
     * CoreCLR ComWrappers shader decodes get_LocalSettings off the property
     * face ComVirtualMethodData and the ApplicationDataContainer RCW chain
     * from LocalSettings faults (0x80131506 failfast). */
    struct com_interface_entry entries[] =
    {
        { (void **)&application_data_vtbl, (IID *)&IID_IApplicationData },
        { (void **)&application_data_methods_vtbl, (IID *)&IID_IApplicationDataMethods },
    };

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (iid_count) *iid_count = ARRAY_SIZE( entries );
    if (iids)
    {
        struct com_interface_entry *array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        *iids = (IID *)array;
    }
    return S_OK;
}

static HRESULT WINAPI application_data_GetRuntimeClassName( IApplicationData *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.ApplicationData", 31, class_name );
}

static HRESULT WINAPI application_data_GetTrustLevel( IApplicationData *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI application_data_get_Version( IApplicationData *iface, UINT32 *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_SetVersionAsync( IApplicationData *iface, UINT32 version, IApplicationDataSetVersionHandler *handler,
                                                        IAsyncAction **operation )
{
    FIXME( "iface %p, version %d, handler %p, operation %p stub!\n", iface, version, handler, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_ClearAllAsync( IApplicationData *iface, IAsyncAction **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_ClearAsync( IApplicationData *iface, ApplicationDataLocality locality, IAsyncAction **operation )
{
    FIXME( "iface %p, %d locality, operation %p stub!\n", iface, locality, operation );
    return E_NOTIMPL;
}

struct application_data_container
{
    IApplicationDataContainer IApplicationDataContainer_iface;
    IApplicationDataContainerMethods IApplicationDataContainerMethods_iface;
    LONG ref;
    ApplicationDataLocality locality;
    IPropertySet *values;
};

static inline struct application_data_container *impl_from_IApplicationDataContainer( IApplicationDataContainer *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_container, IApplicationDataContainer_iface );
}

static inline struct application_data_container *impl_from_IApplicationDataContainerMethods( IApplicationDataContainerMethods *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_container, IApplicationDataContainerMethods_iface );
}

static HRESULT WINAPI container_QueryInterface( IApplicationDataContainer *iface, REFIID iid, void **out )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplicationDataContainer ))
    {
        *out = &impl->IApplicationDataContainer_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IApplicationDataContainerMethods ))
    {
        *out = &impl->IApplicationDataContainerMethods_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "iid %s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI container_AddRef( IApplicationDataContainer *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI container_Release( IApplicationDataContainer *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->values) IPropertySet_Release( impl->values );
        free( impl );
    }
    return ref;
}

/* Shader pair-array contract: see struct com_interface_entry at the top.
 * With E_NOTIMPL the shader falls back to a single-entry IInspectable vtable
 * and the IApplicationDataContainer projection reads its method slots
 * (get_Values = 8) off the wrong ComVirtualMethodData, AVing the Values RCW
 * chain. */
static const struct IApplicationDataContainerVtbl container_vtbl;
static const struct IApplicationDataContainerMethodsVtbl container_methods_vtbl;
static const struct __FIMapView_2_HSTRING_Windows__CStorage__CApplicationDataContainerVtbl empty_map_view_vtbl;

static HRESULT WINAPI container_GetIids( IApplicationDataContainer *iface, ULONG *count, IID **iids )
{
    struct com_interface_entry entries[] =
    {
        { (void **)&container_vtbl, (IID *)&IID_IApplicationDataContainer },
        { (void **)&container_methods_vtbl, (IID *)&IID_IApplicationDataContainerMethods },
    };
    TRACE( "iface %p, count %p, iids %p.\n", iface, count, iids );

    if (count) *count = ARRAY_SIZE( entries );
    if (iids)
    {
        struct com_interface_entry *array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        *iids = (IID *)array;
    }
    return S_OK;
}
static HRESULT WINAPI container_GetRuntimeClassName( IApplicationDataContainer *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.ApplicationDataContainer", 39, name ); }
static HRESULT WINAPI container_GetTrustLevel( IApplicationDataContainer *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI container_get_Name( IApplicationDataContainer *iface, HSTRING *value )
{ return WindowsCreateString( L"LocalSettings", 13, value ); }

static HRESULT WINAPI container_get_Locality( IApplicationDataContainer *iface, ApplicationDataLocality *value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    if (!value) return E_POINTER;
    *value = impl->locality;
    return S_OK;
}

static HRESULT WINAPI container_get_Values( IApplicationDataContainer *iface, IPropertySet **value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";
    HSTRING class_id;

    if (!value) return E_POINTER;

    /* Values is a live view; hand out a fresh PropertySet per call so that
     * the CLR RCW cache (keyed by ABI pointer) cannot alias a collected
     * wrapper onto a new marshal.  The container keeps one ref on the last
     * instance it handed out, mirroring the cached-map shape callers expect. */
    WindowsCreateString( name, wcslen(name), &class_id );
    RoActivateInstance( class_id, (IInspectable **)&impl->values );
    WindowsDeleteString( class_id );

    *value = impl->values;
    if (impl->values) IPropertySet_AddRef( impl->values );
    /* Tag hypothesis probe: log the ring slot handed to the shader and the
     * first qword (expected: the merged propertyset vtable, not a tagged
     * base+count value like 0x6fff00000006). */
    TRACE( "get_Values: ring iface %p first_qword %p\n", impl->values,
           impl->values ? *(void **)impl->values : NULL );
    return *value ? S_OK : E_FAIL;
}

static HRESULT WINAPI empty_map_view_QueryInterface( IMapView_HSTRING_ApplicationDataContainer *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_HSTRING_ApplicationDataContainer ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI empty_map_view_AddRef( IMapView_HSTRING_ApplicationDataContainer *iface ) { return 2; }
static ULONG WINAPI empty_map_view_Release( IMapView_HSTRING_ApplicationDataContainer *iface ) { return 1; }
static HRESULT WINAPI empty_map_view_GetIids( IMapView_HSTRING_ApplicationDataContainer *iface, ULONG *c, IID **i )
{
    struct com_interface_entry entries[] =
    {
        { (void **)&empty_map_view_vtbl, (IID *)&IID_IMapView_HSTRING_ApplicationDataContainer },
    };

    if (c) *c = ARRAY_SIZE( entries );
    if (i)
    {
        struct com_interface_entry *array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        *i = (IID *)array;
    }
    return S_OK;
}
static HRESULT WINAPI empty_map_view_GetRuntimeClassName( IMapView_HSTRING_ApplicationDataContainer *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IMapView`2", 40, n );
}
static HRESULT WINAPI empty_map_view_GetTrustLevel( IMapView_HSTRING_ApplicationDataContainer *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}
static HRESULT WINAPI empty_map_view_Lookup( IMapView_HSTRING_ApplicationDataContainer *iface, HSTRING key, IApplicationDataContainer **value )
{
    if (value) *value = NULL;
    return 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI empty_map_view_get_Size( IMapView_HSTRING_ApplicationDataContainer *iface, unsigned int *size )
{
    if (size) *size = 0;
    return S_OK;
}
static HRESULT WINAPI empty_map_view_HasKey( IMapView_HSTRING_ApplicationDataContainer *iface, HSTRING key, boolean *found )
{
    if (found) *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI empty_map_view_Split( IMapView_HSTRING_ApplicationDataContainer *iface, IMapView_HSTRING_ApplicationDataContainer **first, IMapView_HSTRING_ApplicationDataContainer **second )
{
    if (first) *first = NULL;
    if (second) *second = NULL;
    return S_OK;
}

static const struct __FIMapView_2_HSTRING_Windows__CStorage__CApplicationDataContainerVtbl empty_map_view_vtbl =
{
    empty_map_view_QueryInterface,
    empty_map_view_AddRef,
    empty_map_view_Release,
    empty_map_view_GetIids,
    empty_map_view_GetRuntimeClassName,
    empty_map_view_GetTrustLevel,
    empty_map_view_Lookup,
    empty_map_view_get_Size,
    empty_map_view_HasKey,
    empty_map_view_Split,
};

static IMapView_HSTRING_ApplicationDataContainer empty_map_view = { (void *)&empty_map_view_vtbl };

static HRESULT WINAPI container_get_Containers( IApplicationDataContainer *iface, IMapView_HSTRING_ApplicationDataContainer **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = &empty_map_view;
    return S_OK;
}

static HRESULT container_create( ApplicationDataLocality locality, IApplicationDataContainer **out );

static HRESULT WINAPI container_CreateContainer( IApplicationDataContainer *iface, HSTRING name, ApplicationDataCreateDisposition disposition, IApplicationDataContainer **container )
{
    struct application_data_container *parent = impl_from_IApplicationDataContainer( iface );
    TRACE( "iface %p, name %s, disposition %d, container %p\n", iface, debugstr_hstring(name), disposition, container );
    if (!container) return E_POINTER;
    return container_create( parent->locality, container );
}

static HRESULT WINAPI container_DeleteContainer( IApplicationDataContainer *iface, HSTRING name )
{
    FIXME( "stub\n" );
    return S_OK;
}

static const struct IApplicationDataContainerVtbl container_vtbl =
{
    container_QueryInterface,
    container_AddRef,
    container_Release,
    container_GetIids,
    container_GetRuntimeClassName,
    container_GetTrustLevel,
    container_get_Name,
    container_get_Locality,
    container_get_Values,
    container_get_Containers,
    container_CreateContainer,
    container_DeleteContainer,
};

/* CsWinRT projection face: the .NET ApplicationDataContainer wrapper dispatches
 * Values/Containers through IApplicationDataContainerMethods (11d3b13a-...);
 * without it the shader decodes get_Values off the property-face ComVirtualMethodData
 * and the Values RCW chain AV-storms (s220-s223, 33M first-chance lines). */
static HRESULT WINAPI container_methods_QueryInterface( IApplicationDataContainerMethods *iface, REFIID iid, void **out )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainerMethods( iface );
    return IApplicationDataContainer_QueryInterface( &impl->IApplicationDataContainer_iface, iid, out );
}

static ULONG WINAPI container_methods_AddRef( IApplicationDataContainerMethods *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainerMethods( iface );
    return IApplicationDataContainer_AddRef( &impl->IApplicationDataContainer_iface );
}

static ULONG WINAPI container_methods_Release( IApplicationDataContainerMethods *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainerMethods( iface );
    return IApplicationDataContainer_Release( &impl->IApplicationDataContainer_iface );
}

static HRESULT WINAPI container_methods_GetIids( IApplicationDataContainerMethods *iface, ULONG *count, IID **iids )
{
    struct com_interface_entry entries[] =
    {
        { (void **)&container_vtbl, (IID *)&IID_IApplicationDataContainer },
        { (void **)&container_methods_vtbl, (IID *)&IID_IApplicationDataContainerMethods },
    };

    if (count) *count = ARRAY_SIZE( entries );
    if (iids)
    {
        struct com_interface_entry *array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        *iids = (IID *)array;
    }
    return S_OK;
}

static HRESULT WINAPI container_methods_GetRuntimeClassName( IApplicationDataContainerMethods *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Storage.ApplicationDataContainer", 39, name );
}

static HRESULT WINAPI container_methods_GetTrustLevel( IApplicationDataContainerMethods *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI container_methods_get_Values( IApplicationDataContainerMethods *iface, IPropertySet **value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainerMethods( iface );
    return container_get_Values( &impl->IApplicationDataContainer_iface, value );
}

static HRESULT WINAPI container_methods_get_Containers( IApplicationDataContainerMethods *iface, IMapView_HSTRING_ApplicationDataContainer **value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainerMethods( iface );
    return container_get_Containers( &impl->IApplicationDataContainer_iface, value );
}

static HRESULT WINAPI container_methods_CreateContainer( IApplicationDataContainerMethods *iface, HSTRING name,
                                                        ApplicationDataCreateDisposition disposition, IApplicationDataContainer **container )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainerMethods( iface );
    return container_CreateContainer( &impl->IApplicationDataContainer_iface, name, disposition, container );
}

static HRESULT WINAPI container_methods_DeleteContainer( IApplicationDataContainerMethods *iface, HSTRING name )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainerMethods( iface );
    return container_DeleteContainer( &impl->IApplicationDataContainer_iface, name );
}

static const struct IApplicationDataContainerMethodsVtbl container_methods_vtbl =
{
    container_methods_QueryInterface,
    container_methods_AddRef,
    container_methods_Release,
    container_methods_GetIids,
    container_methods_GetRuntimeClassName,
    container_methods_GetTrustLevel,
    container_methods_get_Values,
    container_methods_get_Containers,
    container_methods_CreateContainer,
    container_methods_DeleteContainer,
};

static HRESULT container_create( ApplicationDataLocality locality, IApplicationDataContainer **out )
{
    struct application_data_container *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IApplicationDataContainer_iface.lpVtbl = &container_vtbl;
    impl->IApplicationDataContainerMethods_iface.lpVtbl = &container_methods_vtbl;
    impl->ref = 1;
    impl->locality = locality;
    *out = &impl->IApplicationDataContainer_iface;
    return S_OK;
}

static HRESULT WINAPI application_data_get_LocalSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    static IApplicationDataContainer *local_settings;
    if (!value) return E_POINTER;
    if (!local_settings) container_create( ApplicationDataLocality_Local, &local_settings );
    *value = local_settings;
    if (local_settings) IApplicationDataContainer_AddRef( local_settings );
    return *value ? S_OK : E_FAIL;
}

static HRESULT WINAPI application_data_get_RoamingSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    static IApplicationDataContainer *roaming_settings;
    if (!value) return E_POINTER;
    if (!roaming_settings) container_create( ApplicationDataLocality_Roaming, &roaming_settings );
    *value = roaming_settings;
    if (roaming_settings) IApplicationDataContainer_AddRef( roaming_settings );
    return *value ? S_OK : E_FAIL;
}

static HRESULT WINAPI application_data_methods_QueryInterface( IApplicationDataMethods *iface, REFIID iid, void **out )
{
    struct application_data *impl = impl_from_IApplicationDataMethods( iface );
    return IApplicationData_QueryInterface( &impl->IApplicationData_iface, iid, out );
}

static ULONG WINAPI application_data_methods_AddRef( IApplicationDataMethods *iface )
{
    struct application_data *impl = impl_from_IApplicationDataMethods( iface );
    return IApplicationData_AddRef( &impl->IApplicationData_iface );
}

static ULONG WINAPI application_data_methods_Release( IApplicationDataMethods *iface )
{
    struct application_data *impl = impl_from_IApplicationDataMethods( iface );
    return IApplicationData_Release( &impl->IApplicationData_iface );
}

static HRESULT WINAPI application_data_methods_GetIids( IApplicationDataMethods *iface, ULONG *count, IID **iids )
{
    struct application_data *impl = impl_from_IApplicationDataMethods( iface );
    return application_data_GetIids( &impl->IApplicationData_iface, count, iids );
}

static HRESULT WINAPI application_data_methods_GetRuntimeClassName( IApplicationDataMethods *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Storage.ApplicationData", 31, name );
}

static HRESULT WINAPI application_data_methods_GetTrustLevel( IApplicationDataMethods *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI application_data_methods_get_LocalSettings( IApplicationDataMethods *iface, IApplicationDataContainer **value )
{
    struct application_data *impl = impl_from_IApplicationDataMethods( iface );
    return application_data_get_LocalSettings( &impl->IApplicationData_iface, value );
}

static HRESULT WINAPI application_data_methods_get_RoamingSettings( IApplicationDataMethods *iface, IApplicationDataContainer **value )
{
    struct application_data *impl = impl_from_IApplicationDataMethods( iface );
    return application_data_get_RoamingSettings( &impl->IApplicationData_iface, value );
}

static HRESULT WINAPI application_data_methods_CreateCompositeStream( IApplicationDataMethods *iface,
                                                                      IInspectable *stream,
                                                                      struct __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream **composite_stream )
{
    return E_NOTIMPL;
}

static const struct IApplicationDataMethodsVtbl application_data_methods_vtbl =
{
    application_data_methods_QueryInterface,
    application_data_methods_AddRef,
    application_data_methods_Release,
    application_data_methods_GetIids,
    application_data_methods_GetRuntimeClassName,
    application_data_methods_GetTrustLevel,
    application_data_methods_get_LocalSettings,
    application_data_methods_get_RoamingSettings,
    application_data_methods_CreateCompositeStream,
};

struct storage_folder
{
    IStorageFolder IStorageFolder_iface;
    IStorageItem IStorageItem_iface;
    LONG ref;
    WCHAR path[MAX_PATH];
};

static inline struct storage_folder *impl_from_IStorageFolder( IStorageFolder *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageFolder_iface );
}

static inline struct storage_folder *impl_from_IStorageItem( IStorageItem *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageItem_iface );
}

static HRESULT WINAPI storage_folder_QueryInterface( IStorageFolder *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStorageFolder ))
    {
        *out = &impl->IStorageFolder_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStorageItem ))
    {
        *out = &impl->IStorageItem_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "iid %s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI storage_folder_AddRef( IStorageFolder *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI storage_folder_Release( IStorageFolder *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI storage_folder_GetIids( IStorageFolder *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetRuntimeClassName( IStorageFolder *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.StorageFolder", 29, name ); }
static HRESULT WINAPI storage_folder_GetTrustLevel( IStorageFolder *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI storage_folder_CreateFileAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_CreateFileAsync( IStorageFolder *iface, HSTRING name, CreationCollisionOption collision, IAsyncOperation_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_CreateFolderAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_CreateFolderAsync( IStorageFolder *iface, HSTRING name, CreationCollisionOption collision, IAsyncOperation_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFileAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFolderAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetItemAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_IStorageItem **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface, IAsyncOperation_IVectorView_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface, IAsyncOperation_IVectorView_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetItemsAsyncOverloadDefaultStartAndCount( IStorageFolder *iface, IAsyncOperation_IVectorView_IStorageItem **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }

static const struct IStorageFolderVtbl storage_folder_vtbl =
{
    storage_folder_QueryInterface,
    storage_folder_AddRef,
    storage_folder_Release,
    storage_folder_GetIids,
    storage_folder_GetRuntimeClassName,
    storage_folder_GetTrustLevel,
    storage_folder_CreateFileAsyncOverloadDefaultOptions,
    storage_folder_CreateFileAsync,
    storage_folder_CreateFolderAsyncOverloadDefaultOptions,
    storage_folder_CreateFolderAsync,
    storage_folder_GetFileAsync,
    storage_folder_GetFolderAsync,
    storage_folder_GetItemAsync,
    storage_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_GetItemsAsyncOverloadDefaultStartAndCount,
};

static HRESULT WINAPI storage_item_QueryInterface( IStorageItem *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return IStorageFolder_QueryInterface( &impl->IStorageFolder_iface, iid, out );
}
static ULONG WINAPI storage_item_AddRef( IStorageItem *iface )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return IStorageFolder_AddRef( &impl->IStorageFolder_iface );
}
static ULONG WINAPI storage_item_Release( IStorageItem *iface )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return IStorageFolder_Release( &impl->IStorageFolder_iface );
}
static HRESULT WINAPI storage_item_GetIids( IStorageItem *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_GetRuntimeClassName( IStorageItem *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.StorageFolder", 29, name ); }
static HRESULT WINAPI storage_item_GetTrustLevel( IStorageItem *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI storage_item_RenameAsyncOverloadDefaultOptions( IStorageItem *iface, HSTRING name, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_RenameAsync( IStorageItem *iface, HSTRING name, NameCollisionOption collision, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_DeleteAsyncOverloadDefaultOptions( IStorageItem *iface, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_DeleteAsync( IStorageItem *iface, StorageDeleteOption option, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_GetBasicPropertiesAsync( IStorageItem *iface, IAsyncOperation_BasicProperties **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_get_Name( IStorageItem *iface, HSTRING *value )
{ return WindowsCreateString( L"LocalState", 10, value ); }
static HRESULT WINAPI storage_item_get_Path( IStorageItem *iface, HSTRING *value )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return WindowsCreateString( impl->path, wcslen(impl->path), value );
}
static HRESULT WINAPI storage_item_get_Attributes( IStorageItem *iface, FileAttributes *value )
{ if (value) *value = FileAttributes_Directory; return S_OK; }
static HRESULT WINAPI storage_item_get_DateCreated( IStorageItem *iface, DateTime *value )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_IsOfType( IStorageItem *iface, StorageItemTypes type, boolean *value )
{ if (value) *value = (type == StorageItemTypes_Folder); return S_OK; }

static const struct IStorageItemVtbl storage_item_vtbl =
{
    storage_item_QueryInterface,
    storage_item_AddRef,
    storage_item_Release,
    storage_item_GetIids,
    storage_item_GetRuntimeClassName,
    storage_item_GetTrustLevel,
    storage_item_RenameAsyncOverloadDefaultOptions,
    storage_item_RenameAsync,
    storage_item_DeleteAsyncOverloadDefaultOptions,
    storage_item_DeleteAsync,
    storage_item_GetBasicPropertiesAsync,
    storage_item_get_Name,
    storage_item_get_Path,
    storage_item_get_Attributes,
    storage_item_get_DateCreated,
    storage_item_IsOfType,
};

static HRESULT storage_folder_create( const WCHAR *path, IStorageFolder **out )
{
    struct storage_folder *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IStorageFolder_iface.lpVtbl = &storage_folder_vtbl;
    impl->IStorageItem_iface.lpVtbl = &storage_item_vtbl;
    impl->ref = 1;
    lstrcpynW( impl->path, path, ARRAY_SIZE(impl->path) );
    CreateDirectoryW( impl->path, NULL );
    *out = &impl->IStorageFolder_iface;
    return S_OK;
}

static HRESULT WINAPI application_data_get_LocalFolder( IApplicationData *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    if (!value) return E_POINTER;
    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", path, ARRAY_SIZE(path) ))
    {
        if (!GetEnvironmentVariableW( L"USERPROFILE", path, ARRAY_SIZE(path) ))
            return E_FAIL;
        lstrcatW( path, L"\\AppData\\Local" );
    }
    return storage_folder_create( path, value );
}

static HRESULT WINAPI application_data_get_RoamingFolder( IApplicationData *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    if (!value) return E_POINTER;
    if (!GetEnvironmentVariableW( L"APPDATA", path, ARRAY_SIZE(path) ))
    {
        if (!GetEnvironmentVariableW( L"USERPROFILE", path, ARRAY_SIZE(path) ))
            return E_FAIL;
        lstrcatW( path, L"\\AppData\\Roaming" );
    }
    return storage_folder_create( path, value );
}

static HRESULT WINAPI application_data_get_TemporaryFolder( IApplicationData *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    if (!value) return E_POINTER;
    GetTempPathW( ARRAY_SIZE(path), path );
    return storage_folder_create( path, value );
}

static HRESULT WINAPI application_data_add_DataChanged( IApplicationData *iface, ITypedEventHandler_ApplicationData_IInspectable *handler,
                                                        EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_remove_DataChanged( IApplicationData *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_SignalDataChanged( IApplicationData *iface )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_get_RoamingStorageQuota( IApplicationData *iface, UINT64 *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IApplicationDataVtbl application_data_vtbl =
{
    application_data_QueryInterface,
    application_data_AddRef,
    application_data_Release,
    /* IInspectable methods */
    application_data_GetIids,
    application_data_GetRuntimeClassName,
    application_data_GetTrustLevel,
    /* IApplicationData methods */
    application_data_get_Version,
    application_data_SetVersionAsync,
    application_data_ClearAllAsync,
    application_data_ClearAsync,
    application_data_get_LocalSettings,
    application_data_get_RoamingSettings,
    application_data_get_LocalFolder,
    application_data_get_RoamingFolder,
    application_data_get_TemporaryFolder,
    application_data_add_DataChanged,
    application_data_remove_DataChanged,
    application_data_SignalDataChanged,
    application_data_get_RoamingStorageQuota,
};

static HRESULT WINAPI application_data2_QueryInterface( IApplicationData2 *iface, REFIID iid, void **out )
{
    struct application_data *impl = impl_from_IApplicationData2( iface );
    return IApplicationData_QueryInterface( &impl->IApplicationData_iface, iid, out );
}
static ULONG WINAPI application_data2_AddRef( IApplicationData2 *iface )
{
    struct application_data *impl = impl_from_IApplicationData2( iface );
    return IApplicationData_AddRef( &impl->IApplicationData_iface );
}
static ULONG WINAPI application_data2_Release( IApplicationData2 *iface )
{
    struct application_data *impl = impl_from_IApplicationData2( iface );
    return IApplicationData_Release( &impl->IApplicationData_iface );
}
static HRESULT WINAPI application_data2_GetIids( IApplicationData2 *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "stub\n" ); return E_NOTIMPL;
}
static HRESULT WINAPI application_data2_GetRuntimeClassName( IApplicationData2 *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.ApplicationData", 30, class_name );
}
static HRESULT WINAPI application_data2_GetTrustLevel( IApplicationData2 *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust; return S_OK;
}
static HRESULT WINAPI application_data2_get_LocalCacheFolder( IApplicationData2 *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", path, ARRAY_SIZE(path) ))
    {
        if (!GetEnvironmentVariableW( L"USERPROFILE", path, ARRAY_SIZE(path) ))
            return E_FAIL;
        lstrcatW( path, L"\\AppData\\Local" );
    }
    lstrcatW( path, L"\\WhatsApp\\LocalCache" );
    return storage_folder_create( path, value );
}

static const struct IApplicationData2Vtbl application_data2_vtbl =
{
    application_data2_QueryInterface,
    application_data2_AddRef,
    application_data2_Release,
    /* IInspectable methods */
    application_data2_GetIids,
    application_data2_GetRuntimeClassName,
    application_data2_GetTrustLevel,
    /* IApplicationData2 methods */
    application_data2_get_LocalCacheFolder,
};

DEFINE_IINSPECTABLE( application_data_statics, IApplicationDataStatics, struct application_data_statics, IActivationFactory_iface )

static HRESULT WINAPI application_data_statics_get_Current( IApplicationDataStatics *iface, IApplicationData **value )
{
    struct application_data *impl;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IApplicationData_iface.lpVtbl = &application_data_vtbl;
    impl->IApplicationData2_iface.lpVtbl = &application_data2_vtbl;
    impl->IApplicationDataMethods_iface.lpVtbl = &application_data_methods_vtbl;
    impl->ref = 1;

    *value = &impl->IApplicationData_iface;
    TRACE( "created IApplicationData %p.\n", *value );
    return S_OK;
}

static const struct IApplicationDataStaticsVtbl application_data_statics_vtbl =
{
    application_data_statics_QueryInterface,
    application_data_statics_AddRef,
    application_data_statics_Release,
    /* IInspectable methods */
    application_data_statics_GetIids,
    application_data_statics_GetRuntimeClassName,
    application_data_statics_GetTrustLevel,
    /* IApplicationDataStatics methods */
    application_data_statics_get_Current,
};

static struct application_data_statics application_data_statics =
{
    {&factory_vtbl},
    {&application_data_statics_vtbl},
    1,
};

IActivationFactory *application_data_factory = &application_data_statics.IActivationFactory_iface;
