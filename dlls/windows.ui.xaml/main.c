/* WinRT Windows.UI.Xaml Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* Windows.UI.Xaml.PropertyMetadata */
static const GUID IID_IPropertyMetadata_WinUI =
    { 0xb3644425, 0x9464, 0x5434, { 0xb0, 0xae, 0xaf, 0xf8, 0xd3, 0x15, 0x9f, 0xe1 } };

struct property_metadata
{
    IPropertyMetadata IPropertyMetadata_iface;
    LONG ref;
    IInspectable *default_value;
    IPropertyChangedCallback *callback;
};

static inline struct property_metadata *impl_from_IPropertyMetadata( IPropertyMetadata *iface )
{
    return CONTAINING_RECORD( iface, struct property_metadata, IPropertyMetadata_iface );
}

static HRESULT WINAPI property_metadata_QueryInterface( IPropertyMetadata *iface, REFIID iid, void **out )
{
    struct property_metadata *impl = impl_from_IPropertyMetadata( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertyMetadata ) ||
        IsEqualGUID( iid, &IID_IPropertyMetadata_WinUI ))
    {
        *out = &impl->IPropertyMetadata_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI property_metadata_AddRef( IPropertyMetadata *iface )
{
    struct property_metadata *impl = impl_from_IPropertyMetadata( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI property_metadata_Release( IPropertyMetadata *iface )
{
    struct property_metadata *impl = impl_from_IPropertyMetadata( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p ref %lu.\n", iface, ref );
    if (!ref)
    {
        if (impl->default_value) IInspectable_Release( impl->default_value );
        if (impl->callback) IPropertyChangedCallback_Release( impl->callback );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI property_metadata_GetIids( IPropertyMetadata *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI property_metadata_GetRuntimeClassName( IPropertyMetadata *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.PropertyMetadata", 32, name );
}

static HRESULT WINAPI property_metadata_GetTrustLevel( IPropertyMetadata *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI property_metadata_get_DefaultValue( IPropertyMetadata *iface, IInspectable **value )
{
    struct property_metadata *impl = impl_from_IPropertyMetadata( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (value)
    {
        *value = impl->default_value;
        if (*value) IInspectable_AddRef( *value );
    }
    return S_OK;
}

static HRESULT WINAPI property_metadata_get_CreateDefaultValueCallback( IPropertyMetadata *iface, ICreateDefaultValueCallback **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CXaml_CIPropertyMetadataVtbl property_metadata_vtbl =
{
    property_metadata_QueryInterface,
    property_metadata_AddRef,
    property_metadata_Release,
    property_metadata_GetIids,
    property_metadata_GetRuntimeClassName,
    property_metadata_GetTrustLevel,
    property_metadata_get_DefaultValue,
    property_metadata_get_CreateDefaultValueCallback,
};

static HRESULT property_metadata_create( IInspectable *default_value, IPropertyChangedCallback *callback, IPropertyMetadata **out )
{
    struct property_metadata *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IPropertyMetadata_iface.lpVtbl = &property_metadata_vtbl;
    impl->ref = 1;
    impl->default_value = default_value;
    if (default_value) IInspectable_AddRef( default_value );
    impl->callback = callback;
    if (callback) IPropertyChangedCallback_AddRef( callback );
    *out = &impl->IPropertyMetadata_iface;
    return S_OK;
}

static const GUID IID_IPropertyMetadataFactory_WinUI =
    { 0x9f420906, 0x111a, 0x5465, { 0x91, 0xee, 0xbe, 0xd1, 0x4b, 0x3e, 0x7f, 0xec } };
static const GUID IID_IPropertyMetadataStatics_WinUI =
    { 0x37b8add4, 0x7a4a, 0x5cf7, { 0xa1, 0x74, 0x23, 0x51, 0x82, 0xcd, 0x08, 0x2e } };
static const GUID IID_IDependencyPropertyStatics_WinUI =
    { 0x61ddc651, 0x0383, 0x5d6f, { 0x98, 0xce, 0x5c, 0x04, 0x6a, 0xaa, 0xaa, 0x8f } };

struct property_metadata_factory
{
    IActivationFactory IActivationFactory_iface;
    IPropertyMetadataFactory IPropertyMetadataFactory_iface;
    IPropertyMetadataStatics IPropertyMetadataStatics_iface;
    LONG ref;
};

static inline struct property_metadata_factory *impl_from_pm_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct property_metadata_factory, IActivationFactory_iface );
}

static HRESULT WINAPI pm_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct property_metadata_factory *impl = impl_from_pm_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPropertyMetadataFactory ) ||
        IsEqualGUID( iid, &IID_IPropertyMetadataFactory_WinUI ))
    {
        *out = &impl->IPropertyMetadataFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPropertyMetadataStatics ) ||
        IsEqualGUID( iid, &IID_IPropertyMetadataStatics_WinUI ))
    {
        *out = &impl->IPropertyMetadataStatics_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI pm_factory_AddRef( IActivationFactory *iface )
{
    struct property_metadata_factory *impl = impl_from_pm_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI pm_factory_Release( IActivationFactory *iface )
{
    struct property_metadata_factory *impl = impl_from_pm_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI pm_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI pm_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.PropertyMetadata", 32, name );
}

static HRESULT WINAPI pm_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI pm_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IPropertyMetadata *pm = NULL;
    HRESULT hr = property_metadata_create( NULL, NULL, &pm );
    if (SUCCEEDED(hr)) *instance = (IInspectable *)pm;
    return hr;
}

static const struct IActivationFactoryVtbl pm_factory_vtbl =
{
    pm_factory_QueryInterface,
    pm_factory_AddRef,
    pm_factory_Release,
    pm_factory_GetIids,
    pm_factory_GetRuntimeClassName,
    pm_factory_GetTrustLevel,
    pm_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( pm_metadata_factory, IPropertyMetadataFactory, struct property_metadata_factory, IActivationFactory_iface )

static HRESULT WINAPI pm_metadata_factory_CreateInstanceWithDefaultValue(
    IPropertyMetadataFactory *iface,
    IInspectable *default_value,
    IInspectable *base_interface,
    IInspectable **inner_interface,
    IPropertyMetadata **value )
{
    HRESULT hr = property_metadata_create( default_value, NULL, value );
    if (SUCCEEDED(hr) && inner_interface && value && *value)
    {
        *inner_interface = (IInspectable *)*value;
        IInspectable_AddRef( *inner_interface );
    }
    return hr;
}

static HRESULT WINAPI pm_metadata_factory_CreateInstanceWithDefaultValueAndCallback(
    IPropertyMetadataFactory *iface,
    IInspectable *default_value,
    IPropertyChangedCallback *property_changed_callback,
    IInspectable *base_interface,
    IInspectable **inner_interface,
    IPropertyMetadata **value )
{
    HRESULT hr = property_metadata_create( default_value, property_changed_callback, value );
    if (SUCCEEDED(hr) && inner_interface && value && *value)
    {
        *inner_interface = (IInspectable *)*value;
        IInspectable_AddRef( *inner_interface );
    }
    return hr;
}

static const struct __x_ABI_CWindows_CUI_CXaml_CIPropertyMetadataFactoryVtbl pm_metadata_factory_vtbl =
{
    pm_metadata_factory_QueryInterface,
    pm_metadata_factory_AddRef,
    pm_metadata_factory_Release,
    pm_metadata_factory_GetIids,
    pm_metadata_factory_GetRuntimeClassName,
    pm_metadata_factory_GetTrustLevel,
    pm_metadata_factory_CreateInstanceWithDefaultValue,
    pm_metadata_factory_CreateInstanceWithDefaultValueAndCallback,
};

DEFINE_IINSPECTABLE( pm_metadata_statics, IPropertyMetadataStatics, struct property_metadata_factory, IActivationFactory_iface )

static HRESULT WINAPI pm_metadata_statics_CreateWithDefaultValue(
    IPropertyMetadataStatics *iface,
    IInspectable *default_value,
    IPropertyMetadata **result )
{
    return property_metadata_create( default_value, NULL, result );
}

static HRESULT WINAPI pm_metadata_statics_CreateWithDefaultValueAndCallback(
    IPropertyMetadataStatics *iface,
    IInspectable *default_value,
    IPropertyChangedCallback *property_changed_callback,
    IPropertyMetadata **result )
{
    return property_metadata_create( default_value, property_changed_callback, result );
}

static HRESULT WINAPI pm_metadata_statics_CreateWithFactory(
    IPropertyMetadataStatics *iface,
    ICreateDefaultValueCallback *create_default_value_callback,
    IPropertyMetadata **result )
{
    return property_metadata_create( NULL, NULL, result );
}

static HRESULT WINAPI pm_metadata_statics_CreateWithFactoryAndCallback(
    IPropertyMetadataStatics *iface,
    ICreateDefaultValueCallback *create_default_value_callback,
    IPropertyChangedCallback *property_changed_callback,
    IPropertyMetadata **result )
{
    return property_metadata_create( NULL, property_changed_callback, result );
}

static const struct __x_ABI_CWindows_CUI_CXaml_CIPropertyMetadataStaticsVtbl pm_metadata_statics_vtbl =
{
    pm_metadata_statics_QueryInterface,
    pm_metadata_statics_AddRef,
    pm_metadata_statics_Release,
    pm_metadata_statics_GetIids,
    pm_metadata_statics_GetRuntimeClassName,
    pm_metadata_statics_GetTrustLevel,
    pm_metadata_statics_CreateWithDefaultValue,
    pm_metadata_statics_CreateWithDefaultValueAndCallback,
    pm_metadata_statics_CreateWithFactory,
    pm_metadata_statics_CreateWithFactoryAndCallback,
};

static struct property_metadata_factory property_metadata_factory_obj =
{
    {&pm_factory_vtbl},
    {&pm_metadata_factory_vtbl},
    {&pm_metadata_statics_vtbl},
    1,
};
IActivationFactory *property_metadata_factory = &property_metadata_factory_obj.IActivationFactory_iface;

/* Windows.UI.Xaml.DependencyProperty */
struct dependency_property
{
    IDependencyProperty IDependencyProperty_iface;
    LONG ref;
    IPropertyMetadata *metadata;
};

static const GUID IID_IDependencyProperty_WinUI =
    { 0x960eab49, 0x9672, 0x58a0, { 0x99, 0x5b, 0x3a, 0x42, 0xe5, 0xea, 0x62, 0x78 } };

static inline struct dependency_property *impl_from_IDependencyProperty( IDependencyProperty *iface )
{
    return CONTAINING_RECORD( iface, struct dependency_property, IDependencyProperty_iface );
}

static HRESULT WINAPI dep_prop_QueryInterface( IDependencyProperty *iface, REFIID iid, void **out )
{
    struct dependency_property *impl = impl_from_IDependencyProperty( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDependencyProperty ) ||
        IsEqualGUID( iid, &IID_IDependencyProperty_WinUI ))
    {
        *out = &impl->IDependencyProperty_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dep_prop_AddRef( IDependencyProperty *iface )
{
    struct dependency_property *impl = impl_from_IDependencyProperty( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI dep_prop_Release( IDependencyProperty *iface )
{
    struct dependency_property *impl = impl_from_IDependencyProperty( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p ref %lu.\n", iface, ref );
    if (!ref)
    {
        if (impl->metadata) IPropertyMetadata_Release( impl->metadata );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI dep_prop_GetIids( IDependencyProperty *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI dep_prop_GetRuntimeClassName( IDependencyProperty *iface, HSTRING *name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.DependencyProperty", 36, name );
}

static HRESULT WINAPI dep_prop_GetTrustLevel( IDependencyProperty *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dep_prop_GetMetadata( IDependencyProperty *iface, TypeName type, IPropertyMetadata **result )
{
    struct dependency_property *impl = impl_from_IDependencyProperty( iface );
    TRACE( "iface %p, result %p\n", iface, result );
    if (result)
    {
        *result = impl->metadata;
        if (*result) IPropertyMetadata_AddRef( *result );
    }
    return S_OK;
}

static const struct __x_ABI_CWindows_CUI_CXaml_CIDependencyPropertyVtbl dep_prop_vtbl =
{
    dep_prop_QueryInterface,
    dep_prop_AddRef,
    dep_prop_Release,
    dep_prop_GetIids,
    dep_prop_GetRuntimeClassName,
    dep_prop_GetTrustLevel,
    dep_prop_GetMetadata,
};

HRESULT dependency_property_create( IPropertyMetadata *metadata, IDependencyProperty **out )
{
    struct dependency_property *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IDependencyProperty_iface.lpVtbl = &dep_prop_vtbl;
    impl->ref = 1;
    impl->metadata = metadata;
    if (metadata) IPropertyMetadata_AddRef( metadata );
    *out = &impl->IDependencyProperty_iface;
    return S_OK;
}

struct dependency_property_factory
{
    IActivationFactory IActivationFactory_iface;
    IDependencyPropertyStatics IDependencyPropertyStatics_iface;
    LONG ref;
};

static inline struct dependency_property_factory *impl_from_dp_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct dependency_property_factory, IActivationFactory_iface );
}

static HRESULT WINAPI dp_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct dependency_property_factory *impl = impl_from_dp_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDependencyPropertyStatics ) ||
        IsEqualGUID( iid, &IID_IDependencyPropertyStatics_WinUI ))
    {
        *out = &impl->IDependencyPropertyStatics_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dp_factory_AddRef( IActivationFactory *iface )
{
    struct dependency_property_factory *impl = impl_from_dp_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI dp_factory_Release( IActivationFactory *iface )
{
    struct dependency_property_factory *impl = impl_from_dp_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI dp_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI dp_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.DependencyProperty", 36, name );
}

static HRESULT WINAPI dp_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dp_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl dp_factory_vtbl =
{
    dp_factory_QueryInterface,
    dp_factory_AddRef,
    dp_factory_Release,
    dp_factory_GetIids,
    dp_factory_GetRuntimeClassName,
    dp_factory_GetTrustLevel,
    dp_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( dp_statics, IDependencyPropertyStatics, struct dependency_property_factory, IActivationFactory_iface )

static HRESULT WINAPI dp_statics_get_UnsetValue( IDependencyPropertyStatics *iface, IInspectable **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI dp_statics_Register(
    IDependencyPropertyStatics *iface,
    HSTRING name,
    TypeName property_type,
    TypeName owner_type,
    IPropertyMetadata *type_metadata,
    IDependencyProperty **result )
{
    TRACE( "iface %p, name %s, metadata %p, result %p\n",
           iface, debugstr_hstring( name ), type_metadata, result );
    return dependency_property_create( type_metadata, result );
}

static HRESULT WINAPI dp_statics_RegisterAttached(
    IDependencyPropertyStatics *iface,
    HSTRING name,
    TypeName property_type,
    TypeName owner_type,
    IPropertyMetadata *default_metadata,
    IDependencyProperty **result )
{
    TRACE( "iface %p, name %s, metadata %p, result %p\n",
           iface, debugstr_hstring( name ), default_metadata, result );
    return dependency_property_create( default_metadata, result );
}

static const struct __x_ABI_CWindows_CUI_CXaml_CIDependencyPropertyStaticsVtbl dp_statics_vtbl =
{
    dp_statics_QueryInterface,
    dp_statics_AddRef,
    dp_statics_Release,
    dp_statics_GetIids,
    dp_statics_GetRuntimeClassName,
    dp_statics_GetTrustLevel,
    dp_statics_get_UnsetValue,
    dp_statics_Register,
    dp_statics_RegisterAttached,
};

static struct dependency_property_factory dependency_property_factory_obj =
{
    {&dp_factory_vtbl},
    {&dp_statics_vtbl},
    1,
};
IActivationFactory *dependency_property_factory = &dependency_property_factory_obj.IActivationFactory_iface;

/* Microsoft.UI.Xaml.Data.CollectionViewSource / Windows.UI.Xaml.Data.CollectionViewSource */
static const GUID IID_ICollectionViewSource =
    { 0xa45e3b3a, 0xf31e, 0x5bbb, { 0x8a, 0x7c, 0x70, 0xcf, 0x5c, 0x64, 0xbc, 0x3f } };
static const GUID IID_ICollectionViewSourceStatics =
    { 0xe282f10f, 0xd4b1, 0x5769, { 0x8a, 0x11, 0x30, 0xf7, 0x39, 0xe6, 0x11, 0x3b } };
static const GUID IID_IDependencyObject_WinUI =
    { 0xe7beaee7, 0x160e, 0x50f7, { 0x87, 0x89, 0xd6, 0x34, 0x63, 0xf9, 0x79, 0xfa } };

struct collection_view_source
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *source;
};

static HRESULT WINAPI cvs_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct collection_view_source *impl = (struct collection_view_source *)iface;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICollectionViewSource ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cvs_AddRef( IInspectable *iface )
{
    struct collection_view_source *impl = (struct collection_view_source *)iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cvs_Release( IInspectable *iface )
{
    struct collection_view_source *impl = (struct collection_view_source *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->source) IInspectable_Release( impl->source );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI cvs_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI cvs_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.Data.CollectionViewSource", 42, name );
}
static HRESULT WINAPI cvs_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cvs_get_Source( void *iface, void **value )
{
    struct collection_view_source *impl = (struct collection_view_source *)iface;
    if (!value) return E_POINTER;
    *value = impl->source;
    if (impl->source) IInspectable_AddRef( impl->source );
    return S_OK;
}
static HRESULT WINAPI cvs_set_Source( void *iface, void *value )
{
    struct collection_view_source *impl = (struct collection_view_source *)iface;
    if (impl->source) IInspectable_Release( impl->source );
    impl->source = (IInspectable *)value;
    if (impl->source) IInspectable_AddRef( impl->source );
    return S_OK;
}
static HRESULT WINAPI cvs_stub_zero( void *iface, void *a2, void *a3 )
{
    if (a3) *(void **)a3 = NULL;
    else if (a2) *(void **)a2 = NULL;
    return S_OK;
}
static HRESULT WINAPI cvs_stub_ok( void *iface )
{
    return S_OK;
}

static const void *cvs_vtbl[] =
{
    cvs_QueryInterface,           /* 0 */
    cvs_AddRef,                   /* 1 */
    cvs_Release,                  /* 2 */
    cvs_GetIids,                  /* 3 */
    cvs_GetRuntimeClassName,      /* 4 */
    cvs_GetTrustLevel,            /* 5 */
    cvs_get_Source,               /* 6: get_Source */
    cvs_set_Source,               /* 7: set_Source */
    cvs_stub_zero,                /* 8: get_View */
    cvs_stub_zero,                /* 9: get_IsSourceGrouped */
    cvs_stub_ok,                  /* 10: set_IsSourceGrouped */
    cvs_stub_zero,                /* 11: get_ItemsPath */
    cvs_stub_ok,                  /* 12: set_ItemsPath */
    cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, /* 13..16 */
    cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, /* 17..20 */
};

/* CollectionViewSource factory */
struct cvs_factory_s
{
    IActivationFactory factory_iface;
    IActivationFactory statics_iface;
};
static struct cvs_factory_s cvs_factory_obj;

static HRESULT WINAPI cvs_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &cvs_factory_obj.factory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICollectionViewSourceStatics ))
    {
        *out = &cvs_factory_obj.statics_iface;
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cvs_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI cvs_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI cvs_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct collection_view_source *obj;
    if (!instance) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)cvs_vtbl;
    obj->ref = 1;
    *instance = &obj->IInspectable_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl cvs_factory_vtbl =
{
    cvs_factory_QueryInterface,
    cvs_factory_AddRef,
    cvs_factory_Release,
    (void *)cvs_GetIids,
    (void *)cvs_GetRuntimeClassName,
    (void *)cvs_GetTrustLevel,
    cvs_factory_ActivateInstance,
};

static const void *cvs_statics_vtbl[] =
{
    cvs_factory_QueryInterface,
    cvs_factory_AddRef,
    cvs_factory_Release,
    cvs_GetIids,
    cvs_GetRuntimeClassName,
    cvs_GetTrustLevel,
    cvs_stub_zero, /* 6: get_SourceProperty */
    cvs_stub_zero, /* 7: get_ViewProperty */
    cvs_stub_zero, /* 8: get_IsSourceGroupedProperty */
    cvs_stub_zero, /* 9: get_ItemsPathProperty */
};

static struct cvs_factory_s cvs_factory_obj =
{
    {&cvs_factory_vtbl},
    {(const struct IActivationFactoryVtbl *)cvs_statics_vtbl},
};
IActivationFactory *collection_view_source_factory = &cvs_factory_obj.factory_iface;

/* Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider */
static const GUID IID_IXamlMetadataProvider_WinUI2 =
    { 0xa96251f0, 0x2214, 0x5d53, { 0x87, 0x46, 0xce, 0x99, 0xa2, 0x59, 0x3c, 0xd7 } };
static const GUID IID_IXamlMetadataProvider_UWP =
    { 0xb3765d69, 0x68a5, 0x4b32, { 0x88, 0x61, 0xfd, 0xb9, 0x0c, 0x1f, 0x58, 0x36 } };

static HRESULT WINAPI meta_provider_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_WinUI2 ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI meta_provider_AddRef( void *iface ) { return 2; }
static ULONG WINAPI meta_provider_Release( void *iface ) { return 1; }
static HRESULT WINAPI meta_provider_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI meta_provider_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider", 61, name ); }
static HRESULT WINAPI meta_provider_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI meta_provider_GetXamlType( void *iface, void *type_name, void **out )
{ if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI meta_provider_GetXamlTypeByFullName( void *iface, HSTRING name, void **out )
{ if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI meta_provider_GetXmlnsDefinitions( void *iface, ULONG *count, void ***out )
{ if (count) *count = 0; if (out) *out = NULL; return S_OK; }

static const void *meta_provider_vtbl[] =
{
    meta_provider_QueryInterface,
    meta_provider_AddRef,
    meta_provider_Release,
    meta_provider_GetIids,
    meta_provider_GetRuntimeClassName,
    meta_provider_GetTrustLevel,
    meta_provider_GetXamlType,           /* 6: GetXamlType */
    meta_provider_GetXamlTypeByFullName, /* 7: GetXamlTypeByFullName */
    meta_provider_GetXmlnsDefinitions,   /* 8: GetXmlnsDefinitions */
};

static struct { const void *vtbl; } meta_provider_obj = { meta_provider_vtbl };

static HRESULT WINAPI meta_provider_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IXamlMetadataProvider_WinUI2 ) ||
        IsEqualGUID( iid, &IID_IXamlMetadataProvider_UWP ))
    {
        *out = &meta_provider_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI meta_provider_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    *instance = (IInspectable *)&meta_provider_obj;
    return S_OK;
}

static const struct IActivationFactoryVtbl meta_provider_factory_vtbl =
{
    meta_provider_factory_QueryInterface,
    cvs_factory_AddRef,
    cvs_factory_Release,
    (void *)cvs_GetIids,
    (void *)cvs_GetRuntimeClassName,
    (void *)cvs_GetTrustLevel,
    meta_provider_factory_ActivateInstance,
};

static struct { const struct IActivationFactoryVtbl *vtbl; } meta_provider_factory_obj = { &meta_provider_factory_vtbl };
static IActivationFactory *meta_provider_factory = (IActivationFactory *)&meta_provider_factory_obj;

/* Microsoft.UI.Xaml.PropertyPath / Windows.UI.Xaml.PropertyPath */
static const GUID IID_IPropertyPath =
    { 0x8b0712f6, 0x9e57, 0x53b0, { 0x80, 0xb1, 0x96, 0x6a, 0x79, 0xf6, 0x0b, 0x96 } };
static const GUID IID_IPropertyPathFactory =
    { 0x08a8ccab, 0x7ff8, 0x5cec, { 0xbd, 0x3c, 0x72, 0xc9, 0x88, 0x04, 0xd9, 0x89 } };

struct property_path
{
    IInspectable IInspectable_iface;
    LONG ref;
    HSTRING path;
};

static HRESULT WINAPI pp_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct property_path *impl = (struct property_path *)iface;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertyPath ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pp_AddRef( IInspectable *iface )
{
    struct property_path *impl = (struct property_path *)iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI pp_Release( IInspectable *iface )
{
    struct property_path *impl = (struct property_path *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->path) WindowsDeleteString( impl->path );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI pp_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI pp_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.PropertyPath", 30, name );
}
static HRESULT WINAPI pp_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI pp_get_Path( void *iface, HSTRING *value )
{
    struct property_path *impl = (struct property_path *)iface;
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->path, value );
}

static const void *pp_vtbl[] =
{
    pp_QueryInterface,           /* 0 */
    pp_AddRef,                   /* 1 */
    pp_Release,                  /* 2 */
    pp_GetIids,                  /* 3 */
    pp_GetRuntimeClassName,      /* 4 */
    pp_GetTrustLevel,            /* 5 */
    pp_get_Path,                 /* 6: get_Path */
    cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, /* 7..10 */
    cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, cvs_stub_zero, /* 11..14 */
};

/* PropertyPath factory */
static HRESULT WINAPI pp_factory_CreateInstance( void *iface, HSTRING path, void **instance )
{
    struct property_path *obj;
    if (!instance) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)pp_vtbl;
    obj->ref = 1;
    if (path) WindowsDuplicateString( path, &obj->path );
    *instance = &obj->IInspectable_iface;
    return S_OK;
}

static HRESULT WINAPI pp_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IPropertyPathFactory ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pp_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI pp_factory_Release( IActivationFactory *iface ) { return 1; }

static const void *pp_factory_vtbl[] =
{
    pp_factory_QueryInterface,
    pp_factory_AddRef,
    pp_factory_Release,
    pp_GetIids,
    pp_GetRuntimeClassName,
    pp_GetTrustLevel,
    pp_factory_CreateInstance, /* slot 6: CreateInstance */
};

static struct { IActivationFactory iface; } pp_factory_obj = { {(const struct IActivationFactoryVtbl *)pp_factory_vtbl} };
IActivationFactory *property_path_factory = &pp_factory_obj.iface;

/* Windows.UI.Xaml.Data.PropertyChangedEventArgs */
static const GUID IID_IPropertyChangedEventArgsFactory =
    { 0x6dcc9c03, 0xe0c7, 0x4eee, { 0x8e, 0xa9, 0x37, 0xe3, 0x40, 0x6e, 0xeb, 0x1c } };
static const GUID IID_IPropertyChangedEventArgs =
    { 0x4f33a9a0, 0x5cf4, 0x47a4, { 0xb1, 0x6f, 0xd7, 0xfa, 0xaf, 0x17, 0x45, 0x7e } };
static const GUID IID_IReferenceTracker_d =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
static const GUID IID_IReferenceTrackerTarget_d =
    { 0x64e71ddf, 0x2424, 0x4ec5, { 0x86, 0x27, 0x15, 0x05, 0x08, 0x96, 0xd9, 0xb6 } };

struct property_changed_event_args
{
    const void *vtbl;
    LONG ref;
    HSTRING property_name;
};

static inline struct property_changed_event_args *impl_from_property_changed_args( void *iface )
{
    return (struct property_changed_event_args *)iface;
}
static HRESULT WINAPI pcea_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct property_changed_event_args *impl = impl_from_property_changed_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker_d ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_d ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertyChangedEventArgs ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pcea_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_property_changed_args( iface )->ref );
}
static ULONG WINAPI pcea_Release( void *iface )
{
    struct property_changed_event_args *impl = impl_from_property_changed_args( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->property_name );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI pcea_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI pcea_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Xaml.Data.PropertyChangedEventArgs", 44, name ); }
static HRESULT WINAPI pcea_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI pcea_get_PropertyName( void *iface, HSTRING *value )
{
    struct property_changed_event_args *impl = impl_from_property_changed_args( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->property_name, value );
}

static const void *property_changed_event_args_vtbl[] =
{
    pcea_QueryInterface,
    pcea_AddRef,
    pcea_Release,
    pcea_GetIids,
    pcea_GetRuntimeClassName,
    pcea_GetTrustLevel,
    pcea_get_PropertyName,
};

static HRESULT property_changed_args_create( HSTRING name, void **out )
{
    struct property_changed_event_args *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl = property_changed_event_args_vtbl;
    impl->ref = 1;
    if (name) WindowsDuplicateString( name, &impl->property_name );
    *out = impl;
    return S_OK;
}

/* PropertyChangedEventArgs Factory */
struct pcea_factory_impl
{
    const void *vtbl_activation;
    const void *vtbl_factory;
};

static inline struct pcea_factory_impl *impl_from_pcea_act( void *iface )
{
    return (struct pcea_factory_impl *)iface;
}
static inline struct pcea_factory_impl *impl_from_pcea_fac( void *iface )
{
    return (struct pcea_factory_impl *)((char *)iface - sizeof(void *));
}

static HRESULT WINAPI pcea_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct pcea_factory_impl *impl = impl_from_pcea_act( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)&impl->vtbl_activation;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPropertyChangedEventArgsFactory ))
    {
        *out = (void *)&impl->vtbl_factory;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pcea_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI pcea_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI pcea_factory_GetIids( void *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI pcea_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Xaml.Data.PropertyChangedEventArgs", 44, name ); }
static HRESULT WINAPI pcea_factory_GetTrustLevel( void *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI pcea_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return property_changed_args_create( NULL, (void **)instance );
}

static const void *pcea_activation_vtbl[] =
{
    pcea_factory_QueryInterface,
    pcea_factory_AddRef,
    pcea_factory_Release,
    pcea_factory_GetIids,
    pcea_factory_GetRuntimeClassName,
    pcea_factory_GetTrustLevel,
    pcea_factory_ActivateInstance,
};

static HRESULT WINAPI pcea_factory2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct pcea_factory_impl *impl = impl_from_pcea_fac( iface );
    return pcea_factory_QueryInterface( (void *)&impl->vtbl_activation, iid, out );
}
static ULONG WINAPI pcea_factory2_AddRef( void *iface )
{
    return pcea_factory_AddRef( (void *)&impl_from_pcea_fac( iface )->vtbl_activation );
}
static ULONG WINAPI pcea_factory2_Release( void *iface )
{
    return pcea_factory_Release( (void *)&impl_from_pcea_fac( iface )->vtbl_activation );
}
static HRESULT WINAPI pcea_factory_CreateInstance( void *iface, HSTRING name, void *outer, void **inner, void **instance )
{
    HRESULT hr = property_changed_args_create( name, instance );
    if (SUCCEEDED(hr) && inner)
    {
        *inner = *instance;
        IUnknown_AddRef( (IUnknown *)*inner );
    }
    return hr;
}

static const void *pcea_factory_vtbl[] =
{
    pcea_factory2_QueryInterface,
    pcea_factory2_AddRef,
    pcea_factory2_Release,
    pcea_factory_GetIids,
    pcea_factory_GetRuntimeClassName,
    pcea_factory_GetTrustLevel,
    pcea_factory_CreateInstance, /* slot 6: CreateInstance on IPropertyChangedEventArgsFactory */
};

static struct pcea_factory_impl property_changed_factory_inst =
{
    pcea_activation_vtbl,
    pcea_factory_vtbl,
};
static IActivationFactory *property_changed_factory = (IActivationFactory *)&property_changed_factory_inst.vtbl_activation;

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (SUCCEEDED(uwp_get_activation_factory( classid, factory )) && *factory)
        return S_OK;

    if (!wcscmp( buffer, RuntimeClass_Windows_UI_ColorHelper ))
        IActivationFactory_QueryInterface( color_helper_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.SwapChainPanel" ))
        IActivationFactory_QueryInterface( swapchain_panel_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Application" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Application" ))
        IActivationFactory_QueryInterface( application_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Window" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Window" ))
        IActivationFactory_QueryInterface( window_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.PropertyMetadata" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.PropertyMetadata" ))
        IActivationFactory_QueryInterface( property_metadata_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.DependencyProperty" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.DependencyProperty" ))
        IActivationFactory_QueryInterface( dependency_property_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Data.CollectionViewSource" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Data.CollectionViewSource" ))
        IActivationFactory_QueryInterface( collection_view_source_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.ResourceDictionary" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.ResourceDictionary" ))
        IActivationFactory_QueryInterface( resource_dictionary_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Data.PropertyChangedEventArgs" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Data.PropertyChangedEventArgs" ))
        IActivationFactory_QueryInterface( property_changed_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.PropertyPath" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.PropertyPath" ))
        IActivationFactory_QueryInterface( property_path_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.SizeHelper" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.SizeHelper" ))
        IActivationFactory_QueryInterface( size_helper_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.Frame" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Frame" ))
        IActivationFactory_QueryInterface( frame_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.Page" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Page" ))
        IActivationFactory_QueryInterface( page_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.RowDefinitionCollection" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.RowDefinitionCollection" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.ColumnDefinitionCollection" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.ColumnDefinitionCollection" ))
        IActivationFactory_QueryInterface( coll_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.RowDefinition" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.RowDefinition" ))
        IActivationFactory_QueryInterface( row_def_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.ColumnDefinition" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.ColumnDefinition" ))
        IActivationFactory_QueryInterface( col_def_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.WebView2" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.WebView2" ))
        IActivationFactory_QueryInterface( webview2_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.UserControl" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.UserControl" ))
        IActivationFactory_QueryInterface( user_control_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider" ))
        IActivationFactory_QueryInterface( meta_provider_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.Canvas" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Canvas" ))
        IActivationFactory_QueryInterface( canvas_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.Grid" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Grid" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.StackPanel" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.StackPanel" ))
        IActivationFactory_QueryInterface( grid_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.ContentControl" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.ContentControl" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.Button" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Button" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.Border" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Border" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.UserControl" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.UserControl" ))
        IActivationFactory_QueryInterface( content_control_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.UI.Xaml.Controls.TextBlock" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.TextBlock" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Shapes.Rectangle" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Shapes.Rectangle" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.Image" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Image" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.Viewbox" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.Viewbox" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.SwapChainPanel" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.SwapChainPanel" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Style" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Style" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.DataTemplate" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.DataTemplate" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.FrameworkTemplate" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.FrameworkTemplate" ) ||
             !wcscmp( buffer, L"Windows.UI.Xaml.Controls.ProgressRing" ) ||
             !wcscmp( buffer, L"Microsoft.UI.Xaml.Controls.ProgressRing" ))
        IActivationFactory_QueryInterface( element_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
