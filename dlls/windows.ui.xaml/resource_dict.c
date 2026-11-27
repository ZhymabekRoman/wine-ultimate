/* WinRT Windows.UI.Xaml.ResourceDictionary Implementation
 *
 * Copyright (C) 2026 Wine project
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

#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "app_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static const GUID IID_IResourceDictionary_Mcg =
    { 0xc1ea4f24, 0xd6de, 0x4191, { 0x8e, 0x3a, 0xf4, 0x86, 0x01, 0xf7, 0x48, 0x9c } };
static const GUID IID_IResourceDictionaryFactory_UWP =
    { 0xea3639b5, 0x31b7, 0x4271, { 0x92, 0xc9, 0x7c, 0x95, 0x84, 0xa9, 0x1c, 0x22 } };
static const GUID IID_IResourceDictionaryFactory_WinUI =
    { 0xea22a48f, 0xab71, 0x56f6, { 0xa3, 0x92, 0xd8, 0x23, 0x10, 0xc8, 0xaa, 0x7b } };
static const GUID IID_IMap_Object_Object_WinUI =
    { 0x1c200999, 0x1a6d, 0x5f79, { 0xb6, 0x10, 0xf8, 0x5a, 0x2c, 0xd7, 0x77, 0xd8 } };
static const GUID IID_IMapView_Object_Object_WinUI =
    { 0x8e411a90, 0x5410, 0x5dd2, { 0x9b, 0x64, 0x25, 0xb5, 0xc5, 0xf0, 0xfc, 0x9f } };
static const GUID IID_IDictionary_Object_Object =
    { 0xf5f69427, 0x55ed, 0x5512, { 0x84, 0x29, 0xd4, 0xf6, 0x62, 0x6d, 0xfc, 0xdd } };
static const GUID IID_IMap_Object_Object_Old =
    { 0x9f03235a, 0xf3e2, 0x5cbe, { 0x81, 0x22, 0x21, 0xf7, 0xf4, 0xda, 0xd6, 0xf6 } };
static const GUID IID_IMapView_Object_Object_Old =
    { 0x28cb687b, 0xca64, 0x5c58, { 0xa0, 0xc5, 0xee, 0x73, 0x50, 0x15, 0x10, 0xc2 } };

struct xaml_resource_dictionary
{
    IInspectable IInspectable_iface; /* IResourceDictionary face */
    IInspectable IMap_iface;          /* IMap / IDictionary face */
    LONG ref;
    struct { WCHAR *key_str; void *key_ptr; IInspectable *val; } entries[32];
    int entry_count;
    IInspectable *merged_dicts;
    IInspectable *theme_dicts;
    IInspectable *source_uri;
};

static WCHAR *rd_key_to_wstr( void *key )
{
    IPropertyValue *pv = NULL;
    HSTRING hs = NULL;
    WCHAR *dup = NULL;
    if (!key) return NULL;
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)key, &IID_IPropertyValue, (void **)&pv )) && pv)
    {
        if (SUCCEEDED(IPropertyValue_GetString( pv, &hs )) && hs)
        {
            const WCHAR *buf = WindowsGetStringRawBuffer( hs, NULL );
            if (buf) dup = wcsdup( buf );
            WindowsDeleteString( hs );
        }
        IPropertyValue_Release( pv );
    }
    return dup;
}

static int rd_find_entry( struct xaml_resource_dictionary *impl, void *key )
{
    WCHAR *ks = rd_key_to_wstr( key );
    int i, found = -1;
    for (i = 0; i < impl->entry_count; i++)
    {
        if (ks && impl->entries[i].key_str && !wcscmp( ks, impl->entries[i].key_str ))
        {
            found = i;
            break;
        }
        if (!ks && impl->entries[i].key_ptr == key)
        {
            found = i;
            break;
        }
    }
    free( ks );
    return found;
}

static inline struct xaml_resource_dictionary *impl_from_rd( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_resource_dictionary, IInspectable_iface );
}
static inline struct xaml_resource_dictionary *impl_from_rd_map( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_resource_dictionary, IMap_iface );
}

static HRESULT WINAPI rd_generic_slot( void *iface, ... )
{
    return S_OK;
}

static HRESULT WINAPI rd_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static ULONG WINAPI rd_AddRef( IInspectable *iface );
static ULONG WINAPI rd_Release( IInspectable *iface );
static HRESULT WINAPI rd_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI rd_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    if (!xaml_get_app_caps()->winui3)
        return WindowsCreateString( L"Windows.UI.Xaml.ResourceDictionary", 34, name );
    return WindowsCreateString( L"Microsoft.UI.Xaml.ResourceDictionary", 36, name );
}
static HRESULT WINAPI rd_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI rd_map_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    return rd_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI rd_map_AddRef( void *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    return rd_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI rd_map_Release( void *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    return rd_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI rd_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    TRACE( "rd_QueryInterface %p, iid %s\n", iface, debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertySet ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_WinUI ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_UWP ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_Mcg ))
    {
        *out = &impl->IInspectable_iface;
        rd_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IDictionary_Object_Object ) ||
        IsEqualGUID( iid, &IID_IMap_Object_Object_WinUI ) ||
        IsEqualGUID( iid, &IID_IMapView_Object_Object_WinUI ) ||
        IsEqualGUID( iid, &IID_IMap_Object_Object_Old ) ||
        IsEqualGUID( iid, &IID_IMapView_Object_Object_Old ))
    {
        *out = &impl->IMap_iface;
        rd_AddRef( iface );
        return S_OK;
    }
    TRACE( "rd_QueryInterface iid %s not implemented, returning E_NOINTERFACE\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rd_AddRef( IInspectable *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI rd_Release( IInspectable *iface )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->merged_dicts) IInspectable_Release( impl->merged_dicts );
        if (impl->theme_dicts) IInspectable_Release( impl->theme_dicts );
        if (impl->source_uri) IInspectable_Release( impl->source_uri );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI rd_get_MergedDictionaries( void *iface, void **value )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    TRACE( "rd_get_MergedDictionaries iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    if (!impl->merged_dicts)
        uwp_vec_create( NULL, &impl->merged_dicts );
    *value = impl->merged_dicts;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}
static HRESULT WINAPI rd_get_Source( void *iface, void **value )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    if (!value) return E_POINTER;
    *value = impl->source_uri;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}
static HRESULT WINAPI rd_set_Source( void *iface, void *value )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    if (value) IInspectable_AddRef( (IInspectable *)value );
    if (impl->source_uri) IInspectable_Release( impl->source_uri );
    impl->source_uri = (IInspectable *)value;
    return S_OK;
}
static HRESULT WINAPI rd_get_ThemeDictionaries( void *iface, void **value )
{
    struct xaml_resource_dictionary *impl = impl_from_rd( iface );
    if (!value) return E_POINTER;
    if (!impl->theme_dicts)
    {
        void *td = NULL;
        if (SUCCEEDED( xaml_resource_dictionary_create( &td ) ) && td)
        {
            struct xaml_resource_dictionary *td_impl = impl_from_rd( (IInspectable *)td );
            static const WCHAR *const themes[] = { L"Light", L"Dark", L"HighContrast" };
            unsigned int ti;
            for (ti = 0; ti < ARRAY_SIZE(themes); ti++)
            {
                void *sub = NULL;
                if (SUCCEEDED( xaml_resource_dictionary_create( &sub ) ) && sub)
                {
                    int n = td_impl->entry_count++;
                    td_impl->entries[n].key_str = wcsdup( themes[ti] );
                    td_impl->entries[n].val = (IInspectable *)sub;
                }
            }
            impl->theme_dicts = (IInspectable *)td;
        }
    }
    *value = impl->theme_dicts;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI rd_map_Lookup( void *iface, void *key, void **value )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    int idx;
    TRACE( "rd_map_Lookup iface %p, key %p, value %p\n", iface, key, value );
    idx = rd_find_entry( impl, key );
    if (idx >= 0)
    {
        if (value)
        {
            *value = impl->entries[idx].val;
            if (impl->entries[idx].val) IInspectable_AddRef( impl->entries[idx].val );
        }
        return S_OK;
    }
    if (!xaml_get_app_caps()->winui3)
        return uwp_rd_lookup( &impl->IInspectable_iface, key, value );
    if (value) *value = NULL;
    return 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI rd_map_get_Size( void *iface, UINT32 *size )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    if (size) *size = (UINT32)impl->entry_count;
    return S_OK;
}
static HRESULT WINAPI rd_map_HasKey( void *iface, void *key, boolean *found )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    if (rd_find_entry( impl, key ) >= 0)
    {
        if (found) *found = TRUE;
        return S_OK;
    }
    if (!xaml_get_app_caps()->winui3)
    {
        if (found) *found = uwp_rd_haskey( &impl->IInspectable_iface, key );
        return S_OK;
    }
    if (found) *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI rd_map_GetView( void *iface, void **view )
{
    if (view) *view = NULL;
    return S_OK;
}
static HRESULT WINAPI rd_map_Insert( void *iface, void *key, IInspectable *val, boolean *replaced )
{
    struct xaml_resource_dictionary *impl = impl_from_rd_map( iface );
    int idx = rd_find_entry( impl, key );
    if (idx >= 0)
    {
        if (impl->entries[idx].val) IInspectable_Release( impl->entries[idx].val );
        impl->entries[idx].val = val;
        if (val) IInspectable_AddRef( val );
        if (replaced) *replaced = TRUE;
        return S_OK;
    }
    if (impl->entry_count < 32)
    {
        int n = impl->entry_count++;
        impl->entries[n].key_str = rd_key_to_wstr( key );
        impl->entries[n].key_ptr = key;
        impl->entries[n].val = val;
        if (val) IInspectable_AddRef( val );
    }
    if (replaced) *replaced = FALSE;
    return S_OK;
}

static const void *rd_vtbl[256] =
{
    [0] = rd_QueryInterface,
    [1] = rd_AddRef,
    [2] = rd_Release,
    [3] = rd_GetIids,
    [4] = rd_GetRuntimeClassName,
    [5] = rd_GetTrustLevel,
    [6] = rd_get_Source,
    [7] = rd_set_Source,
    [8] = rd_get_MergedDictionaries,
    [9] = rd_get_ThemeDictionaries,
    [10 ... 255] = rd_generic_slot,
};

static const void *rd_map_vtbl[256] =
{
    [0] = rd_map_QueryInterface,
    [1] = rd_map_AddRef,
    [2] = rd_map_Release,
    [3] = rd_GetIids,
    [4] = rd_GetRuntimeClassName,
    [5] = rd_GetTrustLevel,
    [11 ... 255] = rd_generic_slot,
    [6] = rd_map_Lookup,
    [7] = rd_map_get_Size,
    [8] = rd_map_HasKey,
    [9] = rd_map_GetView,
    [10] = rd_map_Insert,
};

HRESULT xaml_resource_dictionary_create( void **out )
{
    struct xaml_resource_dictionary *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)rd_vtbl;
    impl->IMap_iface.lpVtbl = (const struct IInspectableVtbl *)rd_map_vtbl;
    impl->ref = 1;
    *out = &impl->IInspectable_iface;
    return S_OK;
}

HRESULT xaml_resource_dictionary_create_for_uwp( void **out )
{
    return xaml_resource_dictionary_create( out );
}

/* ResourceDictionary Factory */
struct rd_factory
{
    const void **vtbl_act;
    const void **vtbl_fac;
};

static struct rd_factory rd_factory_obj;

static HRESULT WINAPI rd_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &rd_factory_obj.vtbl_act;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceDictionaryFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IResourceDictionaryFactory_WinUI ))
    {
        *out = &rd_factory_obj.vtbl_fac;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceDictionary_UWP ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_WinUI ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary_Mcg ))
    {
        return xaml_resource_dictionary_create( out );
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rd_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rd_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI rd_factory_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *array;
    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) * 2 );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_IResourceDictionaryFactory_UWP;
    array[1] = IID_IResourceDictionaryFactory_WinUI;
    *count = 2;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI rd_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.ResourceDictionary", 34, name );
}
static HRESULT WINAPI rd_factory_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI rd_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return xaml_resource_dictionary_create( (void **)instance );
}
static HRESULT WINAPI rd_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    HRESULT hr = xaml_resource_dictionary_create( instance );
    if (SUCCEEDED(hr) && inner)
    {
        *inner = *instance;
        IInspectable_AddRef( (IInspectable *)*inner );
    }
    return hr;
}

static const void *rd_act_vtbl[] =
{
    rd_factory_QueryInterface,
    rd_factory_AddRef,
    rd_factory_Release,
    rd_factory_GetIids,
    rd_factory_GetRuntimeClassName,
    rd_factory_GetTrustLevel,
    rd_factory_ActivateInstance,
};

static const void *rd_fac_vtbl[] =
{
    rd_factory_QueryInterface,
    rd_factory_AddRef,
    rd_factory_Release,
    rd_factory_GetIids,
    rd_factory_GetRuntimeClassName,
    rd_factory_GetTrustLevel,
    rd_factory_CreateInstance,
};

static struct rd_factory rd_factory_obj = { rd_act_vtbl, rd_fac_vtbl };
IActivationFactory *resource_dictionary_factory = (IActivationFactory *)&rd_factory_obj.vtbl_act;
