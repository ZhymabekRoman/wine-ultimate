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
        IsEqualGUID( iid, &IID_IPropertyMetadata ))
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
    if (IsEqualGUID( iid, &IID_IPropertyMetadataFactory ))
    {
        *out = &impl->IPropertyMetadataFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPropertyMetadataStatics ))
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
    TRACE( "iface %p, default_value %p, base %p, inner %p, value %p\n",
           iface, default_value, base_interface, inner_interface, value );
    if (inner_interface) *inner_interface = NULL;
    return property_metadata_create( default_value, NULL, value );
}

static HRESULT WINAPI pm_metadata_factory_CreateInstanceWithDefaultValueAndCallback(
    IPropertyMetadataFactory *iface,
    IInspectable *default_value,
    IPropertyChangedCallback *property_changed_callback,
    IInspectable *base_interface,
    IInspectable **inner_interface,
    IPropertyMetadata **value )
{
    TRACE( "iface %p, default_value %p, callback %p, base %p, inner %p, value %p\n",
           iface, default_value, property_changed_callback, base_interface, inner_interface, value );
    if (inner_interface) *inner_interface = NULL;
    return property_metadata_create( default_value, property_changed_callback, value );
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
        IsEqualGUID( iid, &IID_IDependencyProperty ))
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
    return WindowsCreateString( L"Windows.UI.Xaml.DependencyProperty", 34, name );
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

static HRESULT dependency_property_create( IPropertyMetadata *metadata, IDependencyProperty **out )
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
    if (IsEqualGUID( iid, &IID_IDependencyPropertyStatics ))
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
    return WindowsCreateString( L"Windows.UI.Xaml.DependencyProperty", 34, name );
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

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

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

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
