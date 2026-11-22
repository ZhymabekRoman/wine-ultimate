/* WinRT Windows.ApplicationModel Implementation - WinAppSDK Dynamic Dependencies
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

#include <string.h>

#include "private.h"
#include "winreg.h"
#include "objbase.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(model);


/* WinRT generic collection implementation conforming to ABI:
 * IVectorView<T>, IMapView<K, V>, IIterable<T>, IIterator<T>, IKeyValuePair<K, V> */

static const GUID IID_IReferenceTracker =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
static const GUID IID_IReferenceTrackerManager =
    { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
static const GUID IID_IReferenceTrackerTarget =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };

/* Generic IIDs */
static const GUID IID_IVectorView_Object =
    { 0xa6487363, 0xb074, 0x5c60, { 0xab, 0x16, 0x86, 0x6d, 0xce, 0x4e, 0xe5, 0x4d } };
static const GUID IID_IIterable_Object =
    { 0x092b849b, 0x60b1, 0x52be, { 0xa4, 0x4a, 0x6f, 0xe8, 0xe9, 0x33, 0xcb, 0xe4 } };
static const GUID IID_IIterator_Object =
    { 0x44a94f2d, 0x04f8, 0x5091, { 0xb3, 0x36, 0xbe, 0x78, 0x92, 0xdd, 0x10, 0xbe } };
static const GUID IID_IMapView_Object =
    { 0x236aac9d, 0xfb12, 0x5c4d, { 0xa4, 0x1c, 0x9e, 0x44, 0x5f, 0xb4, 0xd7, 0xec } };
static const GUID IID_IKeyValuePair_Object =
    { 0x09335560, 0x6c6b, 0x5a26, { 0x93, 0x48, 0x97, 0xb7, 0x81, 0x13, 0x2b, 0x20 } };
static const GUID IID_IIterable_KeyValuePair_Object =
    { 0xcf2c8067, 0xa319, 0x570b, { 0xa2, 0x40, 0xd9, 0x30, 0x22, 0x05, 0xec, 0x92 } };
static const GUID IID_IIterator_KeyValuePair_Object =
    { 0xf3c2dc13, 0x6484, 0x5da6, { 0x9e, 0x3c, 0xdc, 0x03, 0x37, 0x4b, 0x0b, 0x36 } };

/* Instantiated IIDs for Store types */
static const GUID IID_IVectorView_StoreImage =
    { 0x7e1ceace, 0x82bd, 0x5db3, { 0x8f, 0x35, 0x9b, 0xf0, 0xc8, 0x8e, 0xf8, 0x39 } };
static const GUID IID_IIterable_StoreImage =
    { 0xb2da6de8, 0xad55, 0x56ce, { 0x87, 0x54, 0x2c, 0x96, 0xf4, 0xfe, 0x1c, 0x2e } };

static const GUID IID_IMapView_StoreProduct =
    { 0x3074efe6, 0x0cd8, 0x5fb7, { 0xae, 0xe3, 0x60, 0xbd, 0xa3, 0xc2, 0xf6, 0x7e } };
static const GUID IID_IIterable_StoreProductKeyValuePair =
    { 0xb9663fb7, 0x1c5f, 0x59a4, { 0x90, 0xc7, 0x36, 0x34, 0xfd, 0xd3, 0xf2, 0xf4 } };
static const GUID IID_IKeyValuePair_StoreProduct =
    { 0x0e89a311, 0x437a, 0x5957, { 0x95, 0x93, 0x8e, 0xd6, 0x45, 0x11, 0x54, 0x5b } };

struct winrt_vector_view
{
    const void *vtbl_vector;
    const void *vtbl_iterable;
    LONG ref;
    GUID vector_iid;
    GUID iterable_iid;
    GUID iterator_iid;
    UINT32 count;
    IInspectable **items;
};

struct winrt_vector_iterator
{
    const void *vtbl_iterator;
    LONG ref;
    struct winrt_vector_view *vector;
    UINT32 index;
};

struct winrt_map_view
{
    const void *vtbl_map;
    const void *vtbl_iterable;
    LONG ref;
    GUID map_iid;
    GUID iterable_iid;
    GUID kv_iid;
    GUID iterator_iid;
    UINT32 count;
    HSTRING *keys;
    IInspectable **values;
};

struct winrt_map_iterator
{
    const void *vtbl_iterator;
    LONG ref;
    struct winrt_map_view *map;
    UINT32 index;
};

struct winrt_key_value_pair
{
    const void *vtbl_kv;
    LONG ref;
    HSTRING key;
    IInspectable *value;
    GUID kv_iid;
};

static inline struct winrt_key_value_pair *impl_from_kv( void *iface )
{
    return (struct winrt_key_value_pair *)iface;
}
static HRESULT WINAPI kv_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->kv_iid ) ||
        IsEqualGUID( iid, &IID_IKeyValuePair_Object ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI kv_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_kv( iface )->ref );
}
static ULONG WINAPI kv_Release( void *iface )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->key );
        if (impl->value) IInspectable_Release( impl->value );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI kv_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI kv_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IKeyValuePair`2";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI kv_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI kv_get_Key( void *iface, HSTRING *key )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    if (!key) return E_POINTER;
    return WindowsDuplicateString( impl->key, key );
}
static HRESULT WINAPI kv_get_Value( void *iface, IInspectable **value )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    if (!value) return E_POINTER;
    *value = impl->value;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}
static const void *winrt_kv_vtbl[] =
{
    kv_QueryInterface,
    kv_AddRef,
    kv_Release,
    kv_GetIids,
    kv_GetRuntimeClassName,
    kv_GetTrustLevel,
    kv_get_Key,
    kv_get_Value,
};

static IInspectable *create_key_value_pair( HSTRING key, IInspectable *value, const GUID *kv_iid )
{
    struct winrt_key_value_pair *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl_kv = winrt_kv_vtbl;
    impl->ref = 1;
    WindowsDuplicateString( key, &impl->key );
    impl->value = value;
    if (impl->value) IInspectable_AddRef( impl->value );
    if (kv_iid) impl->kv_iid = *kv_iid;
    return (IInspectable *)&impl->vtbl_kv;
}

static inline struct winrt_vector_view *impl_from_vector( void *iface )
{
    return (struct winrt_vector_view *)iface;
}
static inline struct winrt_vector_view *impl_from_vector_iterable( void *iface )
{
    return (struct winrt_vector_view *)((char *)iface - sizeof(void *));
}
static inline struct winrt_vector_iterator *impl_from_vector_iter( void *iface )
{
    return (struct winrt_vector_iterator *)iface;
}

static HRESULT WINAPI vector_view_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->vector_iid ) ||
        IsEqualGUID( iid, &IID_IVectorView_Object ))
    {
        *out = &impl->vtbl_vector;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &impl->iterable_iid ) ||
        IsEqualGUID( iid, &IID_IIterable_Object ))
    {
        *out = &impl->vtbl_iterable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    ERR( "vector_view_QueryInterface: rejecting iid=%s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vector_view_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_vector( iface )->ref );
}
static ULONG WINAPI vector_view_Release( void *iface )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        for (UINT32 i = 0; i < impl->count; i++)
        {
            if (impl->items[i]) IInspectable_Release( impl->items[i] );
        }
        free( impl->items );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI vector_view_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI vector_view_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IVectorView`1";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI vector_view_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI vector_view_GetAt( void *iface, UINT32 index, IInspectable **item )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!item) return E_POINTER;
    if (index >= impl->count) { *item = NULL; return E_BOUNDS; }
    *item = impl->items[index];
    if (*item) IInspectable_AddRef( *item );
    return S_OK;
}
static HRESULT WINAPI vector_view_get_Size( void *iface, UINT32 *size )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!size) return E_POINTER;
    *size = impl->count;
    return S_OK;
}
static HRESULT WINAPI vector_view_IndexOf( void *iface, IInspectable *item, UINT32 *index, boolean *found )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!index || !found) return E_POINTER;
    for (UINT32 i = 0; i < impl->count; i++)
    {
        if (impl->items[i] == item)
        {
            *index = i;
            *found = TRUE;
            return S_OK;
        }
    }
    *index = 0;
    *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI vector_view_GetMany( void *iface, UINT32 start_index, UINT32 count, IInspectable **items, UINT32 *actual )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    UINT32 i, num;
    if (!actual) return E_POINTER;
    if (start_index >= impl->count) { *actual = 0; return S_OK; }
    num = min( count, impl->count - start_index );
    for (i = 0; i < num; i++)
    {
        items[i] = impl->items[start_index + i];
        if (items[i]) IInspectable_AddRef( items[i] );
    }
    *actual = num;
    return S_OK;
}

static HRESULT WINAPI vector_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->vector->iterator_iid ) ||
        IsEqualGUID( iid, &IID_IIterator_Object ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    ERR( "vector_iter_QueryInterface: rejecting iid=%s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vector_iter_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_vector_iter( iface )->ref );
}
static ULONG WINAPI vector_iter_Release( void *iface )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        vector_view_Release( &impl->vector->vtbl_vector );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI vector_iter_get_Current( void *iface, IInspectable **item )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!item) return E_POINTER;
    if (impl->index >= impl->vector->count) { *item = NULL; return E_BOUNDS; }
    *item = impl->vector->items[impl->index];
    if (*item) IInspectable_AddRef( *item );
    return S_OK;
}
static HRESULT WINAPI vector_iter_get_HasCurrent( void *iface, boolean *has )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!has) return E_POINTER;
    *has = (impl->index < impl->vector->count);
    return S_OK;
}
static HRESULT WINAPI vector_iter_MoveNext( void *iface, boolean *has )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!has) return E_POINTER;
    if (impl->index < impl->vector->count) impl->index++;
    *has = (impl->index < impl->vector->count);
    return S_OK;
}
static HRESULT WINAPI vector_iter_GetMany( void *iface, UINT32 count, IInspectable **items, UINT32 *actual )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    HRESULT hr = vector_view_GetMany( &impl->vector->vtbl_vector, impl->index, count, items, actual );
    if (SUCCEEDED(hr)) impl->index += *actual;
    return hr;
}
static const void *winrt_vector_iter_vtbl[] =
{
    vector_iter_QueryInterface,
    vector_iter_AddRef,
    vector_iter_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    vector_iter_get_Current,
    vector_iter_get_HasCurrent,
    vector_iter_MoveNext,
    vector_iter_GetMany,
};

static HRESULT WINAPI vector_iterable_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_vector_view *impl = impl_from_vector_iterable( iface );
    return vector_view_QueryInterface( &impl->vtbl_vector, iid, out );
}
static ULONG WINAPI vector_iterable_AddRef( void *iface )
{
    return vector_view_AddRef( &impl_from_vector_iterable( iface )->vtbl_vector );
}
static ULONG WINAPI vector_iterable_Release( void *iface )
{
    return vector_view_Release( &impl_from_vector_iterable( iface )->vtbl_vector );
}
static HRESULT WINAPI vector_iterable_First( void *iface, void **iter )
{
    struct winrt_vector_view *impl = impl_from_vector_iterable( iface );
    struct winrt_vector_iterator *it;
    if (!iter) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->vtbl_iterator = winrt_vector_iter_vtbl;
    it->ref = 1;
    it->vector = impl;
    vector_view_AddRef( &impl->vtbl_vector );
    it->index = 0;
    *iter = &it->vtbl_iterator;
    return S_OK;
}
static const void *winrt_vector_iterable_vtbl[] =
{
    vector_iterable_QueryInterface,
    vector_iterable_AddRef,
    vector_iterable_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    vector_iterable_First,
};

static const void *winrt_vector_view_vtbl[] =
{
    vector_view_QueryInterface,
    vector_view_AddRef,
    vector_view_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    vector_view_GetAt,
    vector_view_get_Size,
    vector_view_IndexOf,
    vector_view_GetMany,
};

static struct winrt_vector_view *store_create_vector_view( IInspectable **items, UINT32 count,
                                                          const GUID *vec_iid, const GUID *iter_iid )
{
    struct winrt_vector_view *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl_vector = winrt_vector_view_vtbl;
    impl->vtbl_iterable = winrt_vector_iterable_vtbl;
    impl->ref = 1;
    if (vec_iid) impl->vector_iid = *vec_iid;
    if (iter_iid) impl->iterable_iid = *iter_iid;
    impl->count = count;
    if (count)
    {
        impl->items = calloc( count, sizeof(IInspectable *) );
        for (UINT32 i = 0; i < count; i++)
        {
            impl->items[i] = items[i];
            if (impl->items[i]) IInspectable_AddRef( impl->items[i] );
        }
    }
    return impl;
}

static inline struct winrt_map_view *impl_from_map( void *iface )
{
    return (struct winrt_map_view *)iface;
}
static inline struct winrt_map_view *impl_from_map_iterable( void *iface )
{
    return (struct winrt_map_view *)((char *)iface - sizeof(void *));
}
static inline struct winrt_map_iterator *impl_from_map_iter( void *iface )
{
    return (struct winrt_map_iterator *)iface;
}

static HRESULT WINAPI map_view_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->map_iid ) ||
        IsEqualGUID( iid, &IID_IMapView_Object ))
    {
        *out = &impl->vtbl_map;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &impl->iterable_iid ) ||
        IsEqualGUID( iid, &IID_IIterable_KeyValuePair_Object ))
    {
        *out = &impl->vtbl_iterable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI map_view_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_map( iface )->ref );
}
static ULONG WINAPI map_view_Release( void *iface )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        for (UINT32 i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->keys[i] );
            if (impl->values[i]) IInspectable_Release( impl->values[i] );
        }
        free( impl->keys );
        free( impl->values );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI map_view_Lookup( void *iface, HSTRING key, IInspectable **value )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    const WCHAR *raw_key = WindowsGetStringRawBuffer( key, NULL );
    if (!value) return E_POINTER;
    if (!raw_key) { *value = NULL; return E_BOUNDS; }
    for (UINT32 i = 0; i < impl->count; i++)
    {
        const WCHAR *k = WindowsGetStringRawBuffer( impl->keys[i], NULL );
        if (k && !wcscmp( k, raw_key ))
        {
            *value = impl->values[i];
            if (*value) IInspectable_AddRef( *value );
            return S_OK;
        }
    }
    *value = NULL;
    return E_BOUNDS;
}
static HRESULT WINAPI map_view_get_Size( void *iface, UINT32 *size )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    if (!size) return E_POINTER;
    *size = impl->count;
    return S_OK;
}
static HRESULT WINAPI map_view_HasKey( void *iface, HSTRING key, boolean *found )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    const WCHAR *raw_key = WindowsGetStringRawBuffer( key, NULL );
    if (!found) return E_POINTER;
    if (!raw_key) { *found = FALSE; return S_OK; }
    for (UINT32 i = 0; i < impl->count; i++)
    {
        const WCHAR *k = WindowsGetStringRawBuffer( impl->keys[i], NULL );
        if (k && !wcscmp( k, raw_key ))
        {
            *found = TRUE;
            return S_OK;
        }
    }
    *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI map_view_Split( void *iface, void **first, void **second )
{
    if (first) *first = NULL;
    if (second) *second = NULL;
    return S_OK;
}
static const void *winrt_map_view_vtbl[] =
{
    map_view_QueryInterface,
    map_view_AddRef,
    map_view_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    map_view_Lookup,
    map_view_get_Size,
    map_view_HasKey,
    map_view_Split,
};

static HRESULT WINAPI map_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->map->iterator_iid ) ||
        IsEqualGUID( iid, &IID_IIterator_KeyValuePair_Object ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI map_iter_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_map_iter( iface )->ref );
}
static ULONG WINAPI map_iter_Release( void *iface )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        map_view_Release( &impl->map->vtbl_map );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI map_iter_get_Current( void *iface, IInspectable **item )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!item) return E_POINTER;
    if (impl->index >= impl->map->count) { *item = NULL; return E_BOUNDS; }
    *item = create_key_value_pair( impl->map->keys[impl->index], impl->map->values[impl->index], &impl->map->kv_iid );
    return *item ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI map_iter_get_HasCurrent( void *iface, boolean *has )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!has) return E_POINTER;
    *has = (impl->index < impl->map->count);
    return S_OK;
}
static HRESULT WINAPI map_iter_MoveNext( void *iface, boolean *has )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!has) return E_POINTER;
    if (impl->index < impl->map->count) impl->index++;
    *has = (impl->index < impl->map->count);
    return S_OK;
}
static HRESULT WINAPI map_iter_GetMany( void *iface, UINT32 count, IInspectable **items, UINT32 *actual )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    UINT32 i, num;
    if (!actual) return E_POINTER;
    if (impl->index >= impl->map->count) { *actual = 0; return S_OK; }
    num = min( count, impl->map->count - impl->index );
    for (i = 0; i < num; i++)
    {
        items[i] = create_key_value_pair( impl->map->keys[impl->index + i], impl->map->values[impl->index + i], &impl->map->kv_iid );
    }
    impl->index += num;
    *actual = num;
    return S_OK;
}
static const void *winrt_map_iter_vtbl[] =
{
    map_iter_QueryInterface,
    map_iter_AddRef,
    map_iter_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    map_iter_get_Current,
    map_iter_get_HasCurrent,
    map_iter_MoveNext,
    map_iter_GetMany,
};

static HRESULT WINAPI map_iterable_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_map_view *impl = impl_from_map_iterable( iface );
    return map_view_QueryInterface( &impl->vtbl_map, iid, out );
}
static ULONG WINAPI map_iterable_AddRef( void *iface )
{
    return map_view_AddRef( &impl_from_map_iterable( iface )->vtbl_map );
}
static ULONG WINAPI map_iterable_Release( void *iface )
{
    return map_view_Release( &impl_from_map_iterable( iface )->vtbl_map );
}
static HRESULT WINAPI map_iterable_First( void *iface, void **iter )
{
    struct winrt_map_view *impl = impl_from_map_iterable( iface );
    struct winrt_map_iterator *it;
    if (!iter) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->vtbl_iterator = winrt_map_iter_vtbl;
    it->ref = 1;
    it->map = impl;
    map_view_AddRef( &impl->vtbl_map );
    it->index = 0;
    *iter = &it->vtbl_iterator;
    return S_OK;
}
static const void *winrt_map_iterable_vtbl[] =
{
    map_iterable_QueryInterface,
    map_iterable_AddRef,
    map_iterable_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    map_iterable_First,
};

static struct winrt_map_view *store_create_map_view( HSTRING *keys, IInspectable **values, UINT32 count,
                                                      const GUID *map_iid, const GUID *iter_iid, const GUID *kv_iid )
{
    struct winrt_map_view *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl_map = winrt_map_view_vtbl;
    impl->vtbl_iterable = winrt_map_iterable_vtbl;
    impl->ref = 1;
    if (map_iid) impl->map_iid = *map_iid;
    if (iter_iid) impl->iterable_iid = *iter_iid;
    if (kv_iid) impl->kv_iid = *kv_iid;
    impl->count = count;
    if (count)
    {
        impl->keys = calloc( count, sizeof(HSTRING) );
        impl->values = calloc( count, sizeof(IInspectable *) );
        for (UINT32 i = 0; i < count; i++)
        {
            WindowsDuplicateString( keys[i], &impl->keys[i] );
            impl->values[i] = values[i];
            if (impl->values[i]) IInspectable_AddRef( impl->values[i] );
        }
    }
    return impl;
}

static const GUID IID_IVectorView_AppInstallItem =
    { 0x48d7f874, 0xa83c, 0x55db, { 0xb2, 0xe6, 0x94, 0x0b, 0xe9, 0x56, 0x98, 0x69 } };
static const GUID IID_IIterable_AppInstallItem =
    { 0x93e1cdc8, 0x503f, 0x55b1, { 0x91, 0x5b, 0xc0, 0xdc, 0x78, 0x88, 0xce, 0x31 } };
static const GUID IID_IIterator_AppInstallItem =
    { 0xccaca81b, 0x6cf7, 0x56f0, { 0xb7, 0xff, 0x8a, 0xc5, 0x19, 0x1e, 0x79, 0xbf } };

static void *store_create_empty_vector_view( void )
{
    return store_create_vector_view( NULL, 0, &IID_IVectorView_Object, &IID_IIterable_Object );
}

static void *store_create_empty_app_install_item_vector_view( void )
{
    struct winrt_vector_view *vv = store_create_vector_view( NULL, 0,
        &IID_IVectorView_AppInstallItem, &IID_IIterable_AppInstallItem );
    if (vv) vv->iterator_iid = IID_IIterator_AppInstallItem;
    return vv;
}

static void *store_create_empty_map_view( void )
{
    return store_create_map_view( NULL, NULL, 0, &IID_IMapView_Object, &IID_IIterable_KeyValuePair_Object, &IID_IKeyValuePair_Object );
}

static const GUID IID_IAsyncInfo_s =
    { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

struct store_async_op
{
    const void *vtbl_async_op;
    const void *vtbl_async_info;
    LONG ref;
    void *result;
    BOOL is_bool;
    BOOL bool_result;
    HRESULT error_code;
};

static inline struct store_async_op *impl_from_async_info( void *iface )
{
    return (struct store_async_op *)((char *)iface - offsetof(struct store_async_op, vtbl_async_info));
}

static HRESULT WINAPI store_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
    static const GUID IID_IReferenceTrackerTarget =
        { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
    static const GUID IID_IMarshal =
        { 0x00000003, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    static const GUID IID_IWeakReferenceSource =
        { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    struct store_async_op *op = iface;

    if (!out) return E_INVALIDARG;
    ERR( "store_async_QueryInterface iid=%s\n", debugstr_guid(iid) );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_s ))
    {
        *out = &op->vtbl_async_info;
        InterlockedIncrement( &op->ref );
        return S_OK;
    }
    *out = &op->vtbl_async_op;
    InterlockedIncrement( &op->ref );
    return S_OK;
}

static ULONG WINAPI store_async_AddRef( void *iface )
{
    struct store_async_op *op = iface;
    return InterlockedIncrement( &op->ref );
}
static ULONG WINAPI store_async_Release( void *iface )
{
    struct store_async_op *op = iface;
    LONG ref = InterlockedDecrement( &op->ref );
    if (!ref)
    {
        if (op->result && !op->is_bool) IUnknown_Release( (IUnknown *)op->result );
        HeapFree( GetProcessHeap(), 0, op );
    }
    return ref;
}

static HRESULT WINAPI store_async_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_async_op *op = impl_from_async_info( iface );
    return store_async_QueryInterface( &op->vtbl_async_op, iid, out );
}
static ULONG WINAPI store_async_info_AddRef( void *iface )
{
    struct store_async_op *op = impl_from_async_info( iface );
    return InterlockedIncrement( &op->ref );
}
static ULONG WINAPI store_async_info_Release( void *iface )
{
    struct store_async_op *op = impl_from_async_info( iface );
    return store_async_Release( &op->vtbl_async_op );
}
static HRESULT WINAPI store_async_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, n );
}
static HRESULT WINAPI store_async_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI store_async_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1", 34, n );
}
static HRESULT WINAPI store_async_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI store_async_put_Completed( void *iface, void *handler )
{
    struct store_async_op *op = iface;
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, &op->vtbl_async_op, 1 /* AsyncStatus_Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI store_async_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI store_async_GetResults( void *iface, void *results )
{
    struct store_async_op *op = iface;
    if (!results) return E_INVALIDARG;
    if (op->is_bool)
        *(boolean *)results = op->bool_result ? 1 : 0;
    else
    {
        *(void **)results = op->result;
        if (op->result) IUnknown_AddRef( (IUnknown *)op->result );
    }
    return op->error_code;
}
static HRESULT WINAPI store_async_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI store_async_get_Status( void *iface, UINT32 *status ) { if (status) *status = 1; return S_OK; }
static HRESULT WINAPI store_async_get_ErrorCode( void *iface, HRESULT *code )
{
    struct store_async_op *op = impl_from_async_info( iface );
    if (code) *code = op->error_code;
    return S_OK;
}
static HRESULT WINAPI store_async_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI store_async_Close( void *iface ) { return S_OK; }

static const void *store_async_vtbl[] =
{
    store_async_QueryInterface,
    store_async_AddRef,
    store_async_Release,
    store_async_GetIids,
    store_async_GetRuntimeClassName,
    store_async_GetTrustLevel,
    store_async_put_Completed,
    store_async_get_Completed,
    store_async_GetResults,
};

static const void *store_async_info_vtbl[] =
{
    store_async_info_QueryInterface,
    store_async_info_AddRef,
    store_async_info_Release,
    store_async_GetIids,
    store_async_info_GetRuntimeClassName,
    store_async_GetTrustLevel,
    store_async_get_Id,
    store_async_get_Status,
    store_async_get_ErrorCode,
    store_async_Cancel,
    store_async_Close,
};

static HRESULT store_async_create( void *result, BOOL is_bool, BOOL bool_result, HRESULT error_code, void **out )
{
    struct store_async_op *op;
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!(op = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*op) ))) return E_OUTOFMEMORY;
    op->vtbl_async_op = store_async_vtbl;
    op->vtbl_async_info = store_async_info_vtbl;
    op->ref = 1;
    op->result = result;
    if (result && !is_bool) IUnknown_AddRef( (IUnknown *)result );
    op->is_bool = is_bool;
    op->bool_result = bool_result;
    op->error_code = error_code;
    *out = &op->vtbl_async_op;
    return S_OK;
}
WINAPPSDK_CLASS( app_install_options, "Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallOptions" );
WINAPPSDK_CLASS( app_update_options, "Windows.ApplicationModel.Store.Preview.InstallControl.AppUpdateOptions" );
WINAPPSDK_CLASS( store_system_feature, "Windows.ApplicationModel.Store.Preview.StoreSystemFeature" );
WINAPPSDK_CLASS( web_auth_core_helper, "Windows.ApplicationModel.Store.Preview.WebAuthenticationCoreManagerHelper" );
WINAPPSDK_CLASS( license_manager, "Windows.ApplicationModel.Store.LicenseManagement.LicenseManager" );
/* =========================================================================
 * Store Support: StoreProduct, StorePrice, StoreAppLicense, StoreContext,
 * StoreConfiguration, AppInstallManager
 * ========================================================================= */

static const GUID IID_IAppInstallManager =
    { 0x9353e170, 0x8441, 0x4b45, { 0xbd, 0x72, 0x7c, 0x2f, 0xa9, 0x25, 0xbe, 0xee } };
static const GUID IID_IAppInstallManager2 =
    { 0x16937851, 0xed37, 0x480d, { 0x83, 0x14, 0x52, 0xe2, 0x7c, 0x03, 0xf0, 0x4a } };
static const GUID IID_IAppInstallManager3 =
    { 0x95b24b17, 0xe96a, 0x4d0e, { 0x84, 0xe1, 0xc8, 0xcb, 0x41, 0x7a, 0x01, 0x78 } };
static const GUID IID_IAppInstallManager4 =
    { 0x260a2a16, 0x5a9e, 0x4ebd, { 0xb9, 0x44, 0xf2, 0xba, 0x75, 0xc3, 0x11, 0x59 } };
static const GUID IID_IAppInstallManager5 =
    { 0x3cd7be4c, 0x1be9, 0x4f7f, { 0xb6, 0x75, 0xaa, 0x1d, 0x64, 0xa5, 0x29, 0xb2 } };

static const GUID IID_IStoreContextStatics =
    { 0x9c06ee5f, 0x15c0, 0x4e72, { 0x93, 0x30, 0xd6, 0x19, 0x1c, 0xeb, 0xd1, 0x9c } };
static const GUID IID_IStoreContext =
    { 0xac98b6be, 0xf4fd, 0x4912, { 0xba, 0xbd, 0x50, 0x35, 0xe5, 0xe8, 0xbc, 0xab } };
static const GUID IID_IStoreContext2 =
    { 0x18bc54da, 0x7bd9, 0x452c, { 0x91, 0x16, 0x3b, 0xbd, 0x06, 0xff, 0xc6, 0x3a } };

static const GUID IID_IStoreConfigurationStatics =
    { 0x728f7fc0, 0x8628, 0x42ec, { 0x84, 0xa2, 0x07, 0x78, 0x0e, 0xb4, 0x4d, 0x8b } };
static const GUID IID_IStoreConfigurationStatics2 =
    { 0x657c4595, 0xc8b7, 0x4fe9, { 0x9f, 0x4c, 0x4d, 0x71, 0x02, 0x7d, 0x34, 0x7e } };
static const GUID IID_IStoreConfigurationStatics3 =
    { 0x6d45f57c, 0xf144, 0x4cb5, { 0x9d, 0x3f, 0x4e, 0xb0, 0x5e, 0x30, 0xb6, 0xd3 } };
static const GUID IID_IStoreConfigurationStatics4 =
    { 0x20ff56d2, 0x4ee3, 0x4cf0, { 0x9b, 0x12, 0x55, 0x2c, 0x03, 0x31, 0x0f, 0x75 } };

/* --- Genuine WinRT Store Data Models --- */
static const GUID IID_IStorePrice =
    { 0x55ba94c4, 0x15f1, 0x407c, { 0x8f, 0x06, 0x00, 0x63, 0x80, 0xf4, 0xdf, 0x0b } };
static const GUID IID_IStoreImage =
    { 0x081fd248, 0xadb4, 0x4b64, { 0xa9, 0x93, 0x78, 0x47, 0x89, 0x92, 0x6e, 0xd5 } };
static const GUID IID_IStoreProduct =
    { 0x320e2c52, 0xd760, 0x450a, { 0xa4, 0x2b, 0x67, 0xd1, 0xe9, 0x01, 0xac, 0x90 } };
static const GUID IID_IStoreProductQueryResult =
    { 0xd805e6c5, 0xd456, 0x4ff6, { 0x80, 0x49, 0x90, 0x76, 0xd5, 0x16, 0x5f, 0x73 } };
static const GUID IID_IStoreProductPagedQueryResult =
    { 0xc92718c5, 0x4dd5, 0x4869, { 0xa4, 0x62, 0xec, 0xc6, 0x87, 0x2e, 0x43, 0xc5 } };
static const GUID IID_IStoreProductResult =
    { 0xb7674f73, 0x3c87, 0x4ee1, { 0x82, 0x01, 0xf4, 0x28, 0x35, 0x9b, 0xd3, 0xaf } };

static IInspectable *create_uri( const WCHAR *uri_str )
{
    IActivationFactory *af = NULL;
    IUriRuntimeClassFactory *factory = NULL;
    IUriRuntimeClass *uri = NULL;
    HSTRING class_name = NULL, uri_hstr = NULL;
    HRESULT hr;

    if (!uri_str) return NULL;
    if (FAILED( WindowsCreateString( L"Windows.Foundation.Uri", 22, &class_name ) )) return NULL;
    if (FAILED( WindowsCreateString( uri_str, wcslen(uri_str), &uri_hstr ) ))
    {
        WindowsDeleteString( class_name );
        return NULL;
    }

    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&af );
    if (SUCCEEDED(hr) && af)
    {
        hr = IActivationFactory_QueryInterface( af, &IID_IUriRuntimeClassFactory, (void **)&factory );
        if (SUCCEEDED(hr) && factory)
        {
            IUriRuntimeClassFactory_CreateUri( factory, uri_hstr, &uri );
            IUriRuntimeClassFactory_Release( factory );
        }
        IActivationFactory_Release( af );
    }
    WindowsDeleteString( uri_hstr );
    WindowsDeleteString( class_name );
    return (IInspectable *)uri;
}

struct store_price
{
    const void *vtbl;
    LONG ref;
    HSTRING base_price;
    HSTRING price;
    boolean is_on_sale;
    INT64 sale_end_date;
    HSTRING currency;
    HSTRING recurrence;
};

static inline struct store_price *impl_from_store_price( void *iface )
{
    return (struct store_price *)iface;
}
static HRESULT WINAPI store_price_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStorePrice ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI store_price_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_store_price( iface )->ref );
}
static ULONG WINAPI store_price_Release( void *iface )
{
    struct store_price *impl = impl_from_store_price( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->base_price );
        WindowsDeleteString( impl->price );
        WindowsDeleteString( impl->currency );
        WindowsDeleteString( impl->recurrence );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI store_price_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI store_price_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StorePrice";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI store_price_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI store_price_get_FormattedBasePrice( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->base_price, value );
}
static HRESULT WINAPI store_price_get_FormattedPrice( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->price, value );
}
static HRESULT WINAPI store_price_get_IsOnSale( void *iface, boolean *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    *value = impl->is_on_sale;
    return S_OK;
}
static HRESULT WINAPI store_price_get_SaleEndDate( void *iface, void *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    memcpy( value, &impl->sale_end_date, sizeof(INT64) );
    return S_OK;
}
static HRESULT WINAPI store_price_get_CurrencyCode( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->currency, value );
}
static HRESULT WINAPI store_price_get_FormattedRecurrencePrice( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->recurrence, value );
}
static const void *store_price_vtbl[] =
{
    store_price_QueryInterface,
    store_price_AddRef,
    store_price_Release,
    store_price_GetIids,
    store_price_GetRuntimeClassName,
    store_price_GetTrustLevel,
    store_price_get_FormattedBasePrice,
    store_price_get_FormattedPrice,
    store_price_get_IsOnSale,
    store_price_get_SaleEndDate,
    store_price_get_CurrencyCode,
    store_price_get_FormattedRecurrencePrice,
};

static struct store_price *store_price_create( const WCHAR *base, const WCHAR *price, const WCHAR *currency )
{
    struct store_price *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_price_vtbl;
    impl->ref = 1;
    if (base) WindowsCreateString( base, wcslen(base), &impl->base_price );
    if (price) WindowsCreateString( price, wcslen(price), &impl->price );
    if (currency) WindowsCreateString( currency, wcslen(currency), &impl->currency );
    WindowsCreateString( L"", 0, &impl->recurrence );
    return impl;
}

struct store_image
{
    const void *vtbl;
    LONG ref;
    IInspectable *uri;
    HSTRING tag;
    UINT32 width;
    UINT32 height;
    HSTRING caption;
};

static inline struct store_image *impl_from_store_image( void *iface )
{
    return (struct store_image *)iface;
}
static HRESULT WINAPI store_image_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreImage ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI store_image_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_store_image( iface )->ref );
}
static ULONG WINAPI store_image_Release( void *iface )
{
    struct store_image *impl = impl_from_store_image( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->uri) IInspectable_Release( impl->uri );
        WindowsDeleteString( impl->tag );
        WindowsDeleteString( impl->caption );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI store_image_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI store_image_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreImage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI store_image_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI store_image_get_Uri( void *iface, void **value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    *value = impl->uri;
    if (*value) IInspectable_AddRef( impl->uri );
    return S_OK;
}
static HRESULT WINAPI store_image_get_ImagePurposeTag( void *iface, HSTRING *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->tag, value );
}
static HRESULT WINAPI store_image_get_Width( void *iface, UINT32 *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    *value = impl->width;
    return S_OK;
}
static HRESULT WINAPI store_image_get_Height( void *iface, UINT32 *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    *value = impl->height;
    return S_OK;
}
static HRESULT WINAPI store_image_get_Caption( void *iface, HSTRING *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->caption, value );
}
static const void *store_image_vtbl[] =
{
    store_image_QueryInterface,
    store_image_AddRef,
    store_image_Release,
    store_image_GetIids,
    store_image_GetRuntimeClassName,
    store_image_GetTrustLevel,
    store_image_get_Uri,
    store_image_get_ImagePurposeTag,
    store_image_get_Width,
    store_image_get_Height,
    store_image_get_Caption,
};

static struct store_image *store_image_create( const WCHAR *uri_str, const WCHAR *tag, UINT32 w, UINT32 h, const WCHAR *caption )
{
    struct store_image *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_image_vtbl;
    impl->ref = 1;
    if (uri_str) impl->uri = create_uri( uri_str );
    if (tag) WindowsCreateString( tag, wcslen(tag), &impl->tag );
    impl->width = w;
    impl->height = h;
    if (caption) WindowsCreateString( caption, wcslen(caption), &impl->caption );
    return impl;
}

struct store_product
{
    const void *vtbl;
    LONG ref;
    HSTRING store_id;
    HSTRING language;
    HSTRING title;
    HSTRING description;
    HSTRING product_kind;
    boolean has_digital_download;
    boolean is_in_user_collection;
    struct store_price *price;
    HSTRING extended_json_data;
    HSTRING in_app_offer_token;
    IInspectable *images;
    IInspectable *keywords;
    IInspectable *skus;
    IInspectable *videos;
    IInspectable *link_uri;
};

static inline struct store_product *impl_from_store_product( void *iface )
{
    return (struct store_product *)iface;
}
static HRESULT WINAPI store_product_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProduct ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI store_product_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_store_product( iface )->ref );
}
static ULONG WINAPI store_product_Release( void *iface )
{
    struct store_product *impl = impl_from_store_product( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->store_id );
        WindowsDeleteString( impl->language );
        WindowsDeleteString( impl->title );
        WindowsDeleteString( impl->description );
        WindowsDeleteString( impl->product_kind );
        WindowsDeleteString( impl->extended_json_data );
        WindowsDeleteString( impl->in_app_offer_token );
        if (impl->price) store_price_Release( &impl->price->vtbl );
        if (impl->images) IInspectable_Release( impl->images );
        if (impl->keywords) IInspectable_Release( impl->keywords );
        if (impl->skus) IInspectable_Release( impl->skus );
        if (impl->videos) IInspectable_Release( impl->videos );
        if (impl->link_uri) IInspectable_Release( impl->link_uri );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI store_product_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI store_product_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreProduct";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI store_product_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI store_product_get_StoreId( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->store_id, value );
}
static HRESULT WINAPI store_product_get_Language( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->language, value );
}
static HRESULT WINAPI store_product_get_Title( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->title, value );
}
static HRESULT WINAPI store_product_get_Description( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->description, value );
}
static HRESULT WINAPI store_product_get_ProductKind( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->product_kind, value );
}
static HRESULT WINAPI store_product_get_HasDigitalDownload( void *iface, boolean *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->has_digital_download;
    return S_OK;
}
static HRESULT WINAPI store_product_get_Keywords( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->keywords;
    if (*value) IInspectable_AddRef( impl->keywords );
    return S_OK;
}
static HRESULT WINAPI store_product_get_Images( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->images;
    if (*value) IInspectable_AddRef( impl->images );
    return S_OK;
}
static HRESULT WINAPI store_product_get_Videos( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->videos;
    if (*value) IInspectable_AddRef( impl->videos );
    return S_OK;
}
static HRESULT WINAPI store_product_get_Skus( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->skus;
    if (*value) IInspectable_AddRef( impl->skus );
    return S_OK;
}
static HRESULT WINAPI store_product_get_IsInUserCollection( void *iface, boolean *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->is_in_user_collection;
    return S_OK;
}
static HRESULT WINAPI store_product_get_Price( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->price;
    if (*value) store_price_AddRef( &impl->price->vtbl );
    return S_OK;
}
static HRESULT WINAPI store_product_get_ExtendedJsonData( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->extended_json_data, value );
}
static HRESULT WINAPI store_product_get_LinkUri( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->link_uri;
    if (*value) IInspectable_AddRef( impl->link_uri );
    return S_OK;
}
static HRESULT WINAPI store_product_GetIsAnySkuInstalledAsync( void *iface, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI store_product_RequestPurchaseAsync( void *iface, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_product_RequestPurchaseWithPropertiesAsync( void *iface, void *props, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_product_get_InAppOfferToken( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->in_app_offer_token, value );
}
static const void *store_product_vtbl[] =
{
    store_product_QueryInterface,
    store_product_AddRef,
    store_product_Release,
    store_product_GetIids,
    store_product_GetRuntimeClassName,
    store_product_GetTrustLevel,
    store_product_get_StoreId,
    store_product_get_Language,
    store_product_get_Title,
    store_product_get_Description,
    store_product_get_ProductKind,
    store_product_get_HasDigitalDownload,
    store_product_get_Keywords,
    store_product_get_Images,
    store_product_get_Videos,
    store_product_get_Skus,
    store_product_get_IsInUserCollection,
    store_product_get_Price,
    store_product_get_ExtendedJsonData,
    store_product_get_LinkUri,
    store_product_GetIsAnySkuInstalledAsync,
    store_product_RequestPurchaseAsync,
    store_product_RequestPurchaseWithPropertiesAsync,
    store_product_get_InAppOfferToken,
};

static struct store_product *store_product_create( const WCHAR *id, const WCHAR *title, const WCHAR *desc, const WCHAR *kind )
{
    struct store_product *impl = calloc( 1, sizeof(*impl) );
    struct store_image *img;
    IInspectable *images_arr[3];
    struct winrt_vector_view *vv;
    UINT32 img_count = 0;

    if (!impl) return NULL;
    impl->vtbl = store_product_vtbl;
    impl->ref = 1;

    if (id) WindowsCreateString( id, wcslen(id), &impl->store_id );
    WindowsCreateString( L"en-us", 5, &impl->language );
    if (title) WindowsCreateString( title, wcslen(title), &impl->title );
    if (desc) WindowsCreateString( desc, wcslen(desc), &impl->description );
    if (kind) WindowsCreateString( kind, wcslen(kind), &impl->product_kind );
    WindowsCreateString( L"{}", 2, &impl->extended_json_data );
    WindowsCreateString( L"", 0, &impl->in_app_offer_token );

    impl->has_digital_download = TRUE;
    impl->is_in_user_collection = TRUE;
    impl->price = store_price_create( L"Free", L"Free", L"USD" );
    impl->link_uri = create_uri( L"https://apps.microsoft.com" );

    img = store_image_create( L"ms-appx:///Assets/StoreLogo.png", L"Logo", 150, 150, L"Logo" );
    if (img) images_arr[img_count++] = (IInspectable *)&img->vtbl;
    img = store_image_create( L"ms-appx:///Assets/Square150x150Logo.png", L"Poster", 300, 300, L"Poster" );
    if (img) images_arr[img_count++] = (IInspectable *)&img->vtbl;
    img = store_image_create( L"ms-appx:///Assets/Square44x44Logo.png", L"Icon", 44, 44, L"Icon" );
    if (img) images_arr[img_count++] = (IInspectable *)&img->vtbl;

    vv = store_create_vector_view( images_arr, img_count, &IID_IVectorView_StoreImage, &IID_IIterable_StoreImage );
    impl->images = vv ? (IInspectable *)&vv->vtbl_vector : NULL;
    for (UINT32 i = 0; i < img_count; i++) store_image_Release( images_arr[i] );

    vv = store_create_vector_view( NULL, 0, &IID_IVectorView_HSTRING, &IID_IIterable_HSTRING );
    impl->keywords = vv ? (IInspectable *)&vv->vtbl_vector : NULL;
    vv = store_create_vector_view( NULL, 0, &IID_IVectorView_Object, &IID_IIterable_Object );
    impl->skus = vv ? (IInspectable *)&vv->vtbl_vector : NULL;
    vv = store_create_vector_view( NULL, 0, &IID_IVectorView_Object, &IID_IIterable_Object );
    impl->videos = vv ? (IInspectable *)&vv->vtbl_vector : NULL;

    return impl;
}

struct store_product_query_result
{
    const void *vtbl;
    LONG ref;
    IInspectable *products;
    HRESULT error_code;
};
static inline struct store_product_query_result *impl_from_product_query_result( void *iface )
{
    return (struct store_product_query_result *)iface;
}
static HRESULT WINAPI product_query_result_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProductQueryResult ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI product_query_result_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_product_query_result( iface )->ref );
}
static ULONG WINAPI product_query_result_Release( void *iface )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->products) IInspectable_Release( impl->products );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI product_query_result_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI product_query_result_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreProductQueryResult";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI product_query_result_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI product_query_result_get_Products( void *iface, void **value )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    if (!value) return E_POINTER;
    *value = impl->products;
    if (*value) IInspectable_AddRef( impl->products );
    return S_OK;
}
static HRESULT WINAPI product_query_result_get_ExtendedError( void *iface, HRESULT *value )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    if (!value) return E_POINTER;
    *value = impl->error_code;
    return S_OK;
}
static const void *store_product_query_result_vtbl[] =
{
    product_query_result_QueryInterface,
    product_query_result_AddRef,
    product_query_result_Release,
    product_query_result_GetIids,
    product_query_result_GetRuntimeClassName,
    product_query_result_GetTrustLevel,
    product_query_result_get_Products,
    product_query_result_get_ExtendedError,
};

static struct store_product_query_result *store_product_query_result_create( void *map )
{
    struct store_product_query_result *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_product_query_result_vtbl;
    impl->ref = 1;
    impl->products = map;
    if (impl->products) IInspectable_AddRef( impl->products );
    impl->error_code = S_OK;
    return impl;
}

struct store_product_paged_query_result
{
    const void *vtbl;
    LONG ref;
    IInspectable *products;
    boolean has_more_results;
    HRESULT error_code;
};
static inline struct store_product_paged_query_result *impl_from_product_paged_query_result( void *iface )
{
    return (struct store_product_paged_query_result *)iface;
}
static HRESULT WINAPI product_paged_query_result_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProductPagedQueryResult ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI product_paged_query_result_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_product_paged_query_result( iface )->ref );
}
static ULONG WINAPI product_paged_query_result_Release( void *iface )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->products) IInspectable_Release( impl->products );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI product_paged_query_result_GetNextAsync( void *iface, void **op )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    return store_async_create( impl, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI product_paged_query_result_get_HasMoreResults( void *iface, boolean *val )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    if (!val) return E_POINTER;
    *val = impl->has_more_results;
    return S_OK;
}
static const void *store_product_paged_query_result_vtbl[] =
{
    product_paged_query_result_QueryInterface,
    product_paged_query_result_AddRef,
    product_paged_query_result_Release,
    product_query_result_GetIids,
    product_query_result_GetRuntimeClassName,
    product_query_result_GetTrustLevel,
    product_query_result_get_Products,
    product_paged_query_result_get_HasMoreResults,
    product_query_result_get_ExtendedError,
    product_paged_query_result_GetNextAsync,
};

static struct store_product_paged_query_result *store_product_paged_query_result_create( void *map )
{
    struct store_product_paged_query_result *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_product_paged_query_result_vtbl;
    impl->ref = 1;
    impl->products = map;
    if (impl->products) IInspectable_AddRef( impl->products );
    impl->has_more_results = FALSE;
    impl->error_code = S_OK;
    return impl;
}

struct store_product_result
{
    const void *vtbl;
    LONG ref;
    struct store_product *product;
    HRESULT error_code;
};
static inline struct store_product_result *impl_from_product_result( void *iface )
{
    return (struct store_product_result *)iface;
}
static HRESULT WINAPI product_result_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProductResult ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI product_result_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_product_result( iface )->ref );
}
static ULONG WINAPI product_result_Release( void *iface )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->product) store_product_Release( &impl->product->vtbl );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI product_result_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI product_result_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreProductResult";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI product_result_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI product_result_get_Product( void *iface, void **value )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    if (!value) return E_POINTER;
    *value = impl->product;
    if (*value) store_product_AddRef( &impl->product->vtbl );
    return S_OK;
}
static HRESULT WINAPI product_result_get_ExtendedError( void *iface, HRESULT *value )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    if (!value) return E_POINTER;
    *value = impl->error_code;
    return S_OK;
}
static const void *store_product_result_vtbl[] =
{
    product_result_QueryInterface,
    product_result_AddRef,
    product_result_Release,
    product_result_GetIids,
    product_result_GetRuntimeClassName,
    product_result_GetTrustLevel,
    product_result_get_Product,
    product_result_get_ExtendedError,
};

static struct store_product_result *store_product_result_create( struct store_product *prod )
{
    struct store_product_result *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_product_result_vtbl;
    impl->ref = 1;
    impl->product = prod;
    if (impl->product) store_product_AddRef( &impl->product->vtbl );
    impl->error_code = S_OK;
    return impl;
}

/* --- StoreAppLicense --- */
static HRESULT WINAPI store_app_license_get_IsActive( void *iface, BOOL *value )
{
    if (value) *value = TRUE;
    return S_OK;
}
static HRESULT WINAPI store_app_license_get_IsTrial( void *iface, BOOL *value )
{
    if (value) *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_app_license_get_AddOnLicenses( void *iface, void **value )
{
    if (value) *value = store_create_empty_map_view();
    return S_OK;
}
static const void *store_app_license_vtbl[] =
{
    winappsdk_class_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_zero_out,              /* SkuStoreId */
    store_app_license_get_IsActive,  /* IsActive */
    store_app_license_get_IsTrial,   /* IsTrial */
    winappsdk_zero_out,              /* ExpirationDate */
    winappsdk_zero_out,              /* ExtendedJsonData */
    store_app_license_get_AddOnLicenses, /* AddOnLicenses */
    winappsdk_zero_out,              /* TrialTimeRemaining */
    winappsdk_zero_out,              /* IsTrialOwnedByThisUser */
    winappsdk_zero_out,              /* TrialUniqueId */
};
static struct winappsdk_class store_app_license_obj =
{
    store_app_license_vtbl, L"Windows.Services.Store.StoreAppLicense", 1
};

/* --- GetEntitlementResult --- */
static HRESULT WINAPI get_entitlement_result_get_Status( void *iface, UINT32 *status )
{
    if (status) *status = 0; /* Succeeded */
    return S_OK;
}
static const void *get_entitlement_result_vtbl[] =
{
    winappsdk_class_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    get_entitlement_result_get_Status,
};
static struct winappsdk_class get_entitlement_result_obj =
{
    get_entitlement_result_vtbl, L"Windows.ApplicationModel.Store.Preview.InstallControl.GetEntitlementResult", 1
};

/* --- AppInstallManager --- */
struct app_install_manager_impl
{
    const void *vtbl1; /* IAppInstallManager */
    const void *vtbl2; /* IAppInstallManager2 */
    const void *vtbl3; /* IAppInstallManager3 */
    const void *vtbl4; /* IAppInstallManager4 */
    const void *vtbl5; /* IAppInstallManager5 */
    const WCHAR *name;
    LONG ref;
};
static struct app_install_manager_impl app_install_mgr_instance;

static HRESULT WINAPI app_install_mgr_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
    struct app_install_manager_impl *mgr = &app_install_mgr_instance;

    TRACE( "AppInstallManager::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IAppInstallManager ))
        *out = (void *)&mgr->vtbl1;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager2 ))
        *out = (void *)&mgr->vtbl2;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager3 ))
        *out = (void *)&mgr->vtbl3;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager4 ))
        *out = (void *)&mgr->vtbl4;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager5 ))
        *out = (void *)&mgr->vtbl5;
    else
        *out = (void *)&mgr->vtbl1;

    InterlockedIncrement( &mgr->ref );
    return S_OK;
}
static ULONG WINAPI app_install_mgr_AddRef( void *iface )
{
    return InterlockedIncrement( &app_install_mgr_instance.ref );
}
static ULONG WINAPI app_install_mgr_Release( void *iface )
{
    return InterlockedDecrement( &app_install_mgr_instance.ref );
}
static HRESULT WINAPI app_install_mgr_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0; if (i) *i = NULL; return S_OK;
}
static HRESULT WINAPI app_install_mgr_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager", 69, n );
}
static HRESULT WINAPI app_install_mgr_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}

static const GUID IID_IAppInstallItem =
    { 0x49d3dfab, 0x168a, 0x4cbf, { 0xa9, 0x3a, 0x9e, 0x44, 0x8c, 0x82, 0x73, 0x7d } };
static const GUID IID_IAppInstallItem2 =
    { 0xd3972af8, 0x40c0, 0x4fd7, { 0xaa, 0x6c, 0x0a, 0xa1, 0x3c, 0xa6, 0x18, 0x8c } };
static const GUID IID_IAppInstallItem3 =
    { 0x6f3dc998, 0xdd47, 0x433c, { 0x92, 0x34, 0x56, 0x01, 0x72, 0xd6, 0x7a, 0x45 } };
static const GUID IID_IAppInstallStatus =
    { 0x936dccfa, 0x2450, 0x4126, { 0x88, 0xb1, 0x61, 0x27, 0xa6, 0x44, 0xdd, 0x5c } };
static const GUID IID_IAppInstallStatus2 =
    { 0x96e7818a, 0x5e92, 0x4aa9, { 0x8e, 0xdc, 0x58, 0xfe, 0xd4, 0xb8, 0x7e, 0x00 } };
static const GUID IID_IAppInstallManagerItemEventArgs =
    { 0xbc505743, 0x4674, 0x4dd1, { 0x95, 0x7e, 0xc2, 0x56, 0x82, 0x08, 0x6a, 0x14 } };

typedef HRESULT (WINAPI *typed_event_invoke_fn)( void *handler, void *sender, void *args );

#define APP_INSTALL_MAX_ITEMS    32

struct app_install_status_impl
{
    const void *vtbl1;
    const void *vtbl2;
    LONG ref;
    INT32 state;
    UINT64 download_size;
    UINT64 bytes_downloaded;
    double percent_complete;
    HRESULT error_code;
    boolean ready_for_launch;
};

struct app_install_mgr_item_event_args
{
    const void *vtbl;
    LONG ref;
    struct app_install_item_impl *item;
};

CRITICAL_SECTION app_install_cs;
static CRITICAL_SECTION_DEBUG app_install_cs_debug =
{
    0, 0, &app_install_cs,
    { &app_install_cs_debug.ProcessLocksList, &app_install_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": app_install_cs") }
};
CRITICAL_SECTION app_install_cs = { &app_install_cs_debug, -1, 0, 0, 0, 0 };

static LONG64 app_install_next_token = 1;
static struct app_install_item_impl *app_install_active_items[APP_INSTALL_MAX_ITEMS];
static UINT32 app_install_active_count = 0;
static struct app_install_handler_slot app_install_mgr_completed_handlers[APP_INSTALL_MAX_HANDLERS];
static struct app_install_handler_slot app_install_mgr_status_handlers[APP_INSTALL_MAX_HANDLERS];

static HRESULT WINAPI app_install_status_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct app_install_status_impl *impl = iface;
    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAppInstallStatus ))
    {
        *out = &impl->vtbl1;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppInstallStatus2 ))
    {
        *out = &impl->vtbl2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI app_install_status2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct app_install_status_impl *impl = (struct app_install_status_impl *)((char *)iface - offsetof(struct app_install_status_impl, vtbl2));
    return app_install_status_QueryInterface( &impl->vtbl1, iid, out );
}
static ULONG WINAPI app_install_status_AddRef( void *iface )
{
    struct app_install_status_impl *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_install_status2_AddRef( void *iface )
{
    struct app_install_status_impl *impl = (struct app_install_status_impl *)((char *)iface - offsetof(struct app_install_status_impl, vtbl2));
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_install_status_Release( void *iface )
{
    struct app_install_status_impl *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static ULONG WINAPI app_install_status2_Release( void *iface )
{
    struct app_install_status_impl *impl = (struct app_install_status_impl *)((char *)iface - offsetof(struct app_install_status_impl, vtbl2));
    return app_install_status_Release( &impl->vtbl1 );
}
static HRESULT WINAPI app_install_status_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI app_install_status_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR name[] = L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallStatus";
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, n );
}
static HRESULT WINAPI app_install_status_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI app_install_status_get_InstallState( void *iface, INT32 *value )
{
    struct app_install_status_impl *impl = iface;
    if (value) *value = impl->state;
    return S_OK;
}
static HRESULT WINAPI app_install_status_get_DownloadSizeInBytes( void *iface, UINT64 *value )
{
    struct app_install_status_impl *impl = iface;
    if (value) *value = impl->download_size;
    return S_OK;
}
static HRESULT WINAPI app_install_status_get_BytesDownloaded( void *iface, UINT64 *value )
{
    struct app_install_status_impl *impl = iface;
    if (value) *value = impl->bytes_downloaded;
    return S_OK;
}
static HRESULT WINAPI app_install_status_get_PercentComplete( void *iface, double *value )
{
    struct app_install_status_impl *impl = iface;
    if (value) *value = impl->percent_complete;
    return S_OK;
}
static HRESULT WINAPI app_install_status_get_ErrorCode( void *iface, HRESULT *value )
{
    struct app_install_status_impl *impl = iface;
    if (value) *value = impl->error_code;
    return S_OK;
}
static HRESULT WINAPI app_install_status2_get_User( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI app_install_status2_get_ReadyForLaunch( void *iface, boolean *value )
{
    struct app_install_status_impl *impl = (struct app_install_status_impl *)((char *)iface - offsetof(struct app_install_status_impl, vtbl2));
    if (value) *value = impl->ready_for_launch;
    return S_OK;
}

static const void *app_install_status_vtbl1[] =
{
    app_install_status_QueryInterface,
    app_install_status_AddRef,
    app_install_status_Release,
    app_install_status_GetIids,
    app_install_status_GetRuntimeClassName,
    app_install_status_GetTrustLevel,
    app_install_status_get_InstallState,
    app_install_status_get_DownloadSizeInBytes,
    app_install_status_get_BytesDownloaded,
    app_install_status_get_PercentComplete,
    app_install_status_get_ErrorCode,
};

static const void *app_install_status_vtbl2[] =
{
    app_install_status2_QueryInterface,
    app_install_status2_AddRef,
    app_install_status2_Release,
    app_install_status_GetIids,
    app_install_status_GetRuntimeClassName,
    app_install_status_GetTrustLevel,
    app_install_status2_get_User,
    app_install_status2_get_ReadyForLaunch,
};

static HRESULT WINAPI app_install_event_args_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct app_install_mgr_item_event_args *impl = iface;
    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAppInstallManagerItemEventArgs ))
    {
        *out = &impl->vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_install_event_args_AddRef( void *iface )
{
    struct app_install_mgr_item_event_args *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_install_event_args_Release( void *iface )
{
    struct app_install_mgr_item_event_args *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->item) IUnknown_Release( (IUnknown *)&impl->item->vtbl1 );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI app_install_event_args_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI app_install_event_args_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR name[] = L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManagerItemEventArgs";
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, n );
}
static HRESULT WINAPI app_install_event_args_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI app_install_event_args_get_Item( void *iface, void **value )
{
    struct app_install_mgr_item_event_args *impl = iface;
    if (!value) return E_INVALIDARG;
    *value = &impl->item->vtbl1;
    if (impl->item) IUnknown_AddRef( (IUnknown *)&impl->item->vtbl1 );
    return S_OK;
}

static const void *app_install_event_args_vtbl[] =
{
    app_install_event_args_QueryInterface,
    app_install_event_args_AddRef,
    app_install_event_args_Release,
    app_install_event_args_GetIids,
    app_install_event_args_GetRuntimeClassName,
    app_install_event_args_GetTrustLevel,
    app_install_event_args_get_Item,
};

static struct app_install_mgr_item_event_args *app_install_event_args_create( struct app_install_item_impl *item )
{
    struct app_install_mgr_item_event_args *args = calloc( 1, sizeof(*args) );
    if (!args) return NULL;
    args->vtbl = app_install_event_args_vtbl;
    args->ref = 1;
    args->item = item;
    if (item) IUnknown_AddRef( (IUnknown *)&item->vtbl1 );
    return args;
}

static void app_install_add_handler( struct app_install_handler_slot *slots, void *handler, INT64 *token )
{
    int i;
    INT64 tok = InterlockedIncrement64( &app_install_next_token );
    if (token) *token = tok;
    if (!handler) return;
    EnterCriticalSection( &app_install_cs );
    for (i = 0; i < APP_INSTALL_MAX_HANDLERS; i++)
    {
        if (!slots[i].handler)
        {
            slots[i].token = tok;
            slots[i].handler = (IUnknown *)handler;
            IUnknown_AddRef( slots[i].handler );
            break;
        }
    }
    LeaveCriticalSection( &app_install_cs );
}

static void app_install_remove_handler( struct app_install_handler_slot *slots, INT64 token )
{
    int i;
    IUnknown *to_release = NULL;
    EnterCriticalSection( &app_install_cs );
    for (i = 0; i < APP_INSTALL_MAX_HANDLERS; i++)
    {
        if (slots[i].handler && slots[i].token == token)
        {
            to_release = slots[i].handler;
            slots[i].handler = NULL;
            slots[i].token = 0;
            break;
        }
    }
    LeaveCriticalSection( &app_install_cs );
    if (to_release) IUnknown_Release( to_release );
}

static void app_install_fire_handlers( struct app_install_handler_slot *slots, void *sender, void *args )
{
    IUnknown *snapshot[APP_INSTALL_MAX_HANDLERS];
    int count = 0, i;

    EnterCriticalSection( &app_install_cs );
    for (i = 0; i < APP_INSTALL_MAX_HANDLERS; i++)
    {
        if (slots[i].handler)
        {
            snapshot[count] = slots[i].handler;
            IUnknown_AddRef( snapshot[count] );
            count++;
        }
    }
    LeaveCriticalSection( &app_install_cs );

    for (i = 0; i < count; i++)
    {
        void **vtbl = *(void ***)snapshot[i];
        typed_event_invoke_fn invoke = (typed_event_invoke_fn)vtbl[3];
        if (invoke) invoke( snapshot[i], sender, args );
        IUnknown_Release( snapshot[i] );
    }
}

void app_install_notify_item_change( struct app_install_item_impl *item, BOOL completed )
{
    struct app_install_mgr_item_event_args *mgr_args;

    app_install_fire_handlers( item->status_handlers, &item->vtbl1, NULL );
    mgr_args = app_install_event_args_create( item );
    if (mgr_args)
    {
        app_install_fire_handlers( app_install_mgr_status_handlers, &app_install_mgr_instance.vtbl1, &mgr_args->vtbl );
        if (completed)
        {
            app_install_fire_handlers( item->completed_handlers, &item->vtbl1, NULL );
            app_install_fire_handlers( app_install_mgr_completed_handlers, &app_install_mgr_instance.vtbl1, &mgr_args->vtbl );
        }
        IUnknown_Release( (IUnknown *)&mgr_args->vtbl );
    }
    else if (completed)
    {
        app_install_fire_handlers( item->completed_handlers, &item->vtbl1, NULL );
    }
}

static HRESULT WINAPI app_install_item_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct app_install_item_impl *impl = iface;
    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAppInstallItem ))
    {
        *out = &impl->vtbl1;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppInstallItem2 ))
    {
        *out = &impl->vtbl2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppInstallItem3 ))
    {
        *out = &impl->vtbl3;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI app_install_item2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl2));
    return app_install_item_QueryInterface( &impl->vtbl1, iid, out );
}
static HRESULT WINAPI app_install_item3_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl3));
    return app_install_item_QueryInterface( &impl->vtbl1, iid, out );
}
static ULONG WINAPI app_install_item_AddRef( void *iface )
{
    struct app_install_item_impl *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_install_item2_AddRef( void *iface )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl2));
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_install_item3_AddRef( void *iface )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl3));
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_install_item_Release( void *iface )
{
    struct app_install_item_impl *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        int i;
        for (i = 0; i < APP_INSTALL_MAX_HANDLERS; i++)
        {
            if (impl->completed_handlers[i].handler) IUnknown_Release( impl->completed_handlers[i].handler );
            if (impl->status_handlers[i].handler) IUnknown_Release( impl->status_handlers[i].handler );
        }
        if (impl->product_id) WindowsDeleteString( impl->product_id );
        if (impl->package_family_name) WindowsDeleteString( impl->package_family_name );
        free( impl );
    }
    return ref;
}
static ULONG WINAPI app_install_item2_Release( void *iface )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl2));
    return app_install_item_Release( &impl->vtbl1 );
}
static ULONG WINAPI app_install_item3_Release( void *iface )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl3));
    return app_install_item_Release( &impl->vtbl1 );
}
static HRESULT WINAPI app_install_item_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI app_install_item_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR name[] = L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallItem";
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, n );
}
static HRESULT WINAPI app_install_item_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI app_install_item_get_ProductId( void *iface, HSTRING *value )
{
    struct app_install_item_impl *impl = iface;
    if (!value) return E_INVALIDARG;
    return WindowsDuplicateString( impl->product_id, value );
}
static HRESULT WINAPI app_install_item_get_PackageFamilyName( void *iface, HSTRING *value )
{
    struct app_install_item_impl *impl = iface;
    if (!value) return E_INVALIDARG;
    return WindowsDuplicateString( impl->package_family_name, value );
}
static HRESULT WINAPI app_install_item_get_InstallType( void *iface, INT32 *value )
{
    struct app_install_item_impl *impl = iface;
    if (value) *value = impl->install_type;
    return S_OK;
}
static HRESULT WINAPI app_install_item_get_IsUserInitiated( void *iface, boolean *value )
{
    struct app_install_item_impl *impl = iface;
    if (value) *value = impl->is_user_initiated;
    return S_OK;
}
static HRESULT WINAPI app_install_item_GetCurrentStatus( void *iface, void **result )
{
    struct app_install_item_impl *impl = iface;
    struct app_install_status_impl *st;
    if (!result) return E_INVALIDARG;
    st = calloc( 1, sizeof(*st) );
    if (!st) return E_OUTOFMEMORY;
    st->vtbl1 = app_install_status_vtbl1;
    st->vtbl2 = app_install_status_vtbl2;
    st->ref = 1;
    EnterCriticalSection( &app_install_cs );
    st->state = impl->state;
    st->download_size = impl->download_size;
    st->bytes_downloaded = impl->bytes_downloaded;
    st->percent_complete = impl->percent_complete;
    st->error_code = impl->error_code;
    st->ready_for_launch = impl->ready_for_launch;
    LeaveCriticalSection( &app_install_cs );
    *result = &st->vtbl1;
    return S_OK;
}
static HRESULT WINAPI app_install_item_Cancel( void *iface )
{
    struct app_install_item_impl *impl = iface;
    EnterCriticalSection( &app_install_cs );
    impl->canceled = TRUE;
    impl->state = APP_INSTALL_STATE_CANCELED;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( impl, TRUE );
    return S_OK;
}
static HRESULT WINAPI app_install_item_Pause( void *iface )
{
    struct app_install_item_impl *impl = iface;
    EnterCriticalSection( &app_install_cs );
    impl->paused = TRUE;
    impl->state = APP_INSTALL_STATE_PAUSED;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( impl, FALSE );
    return S_OK;
}
static HRESULT WINAPI app_install_item_Restart( void *iface )
{
    struct app_install_item_impl *impl = iface;
    EnterCriticalSection( &app_install_cs );
    impl->paused = FALSE;
    if (impl->state == APP_INSTALL_STATE_PAUSED)
        impl->state = APP_INSTALL_STATE_DOWNLOADING;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( impl, FALSE );
    return S_OK;
}
static HRESULT WINAPI app_install_item_add_Completed( void *iface, void *handler, INT64 *token )
{
    struct app_install_item_impl *impl = iface;
    app_install_add_handler( impl->completed_handlers, handler, token );
    return S_OK;
}
static HRESULT WINAPI app_install_item_remove_Completed( void *iface, INT64 token )
{
    struct app_install_item_impl *impl = iface;
    app_install_remove_handler( impl->completed_handlers, token );
    return S_OK;
}
static HRESULT WINAPI app_install_item_add_StatusChanged( void *iface, void *handler, INT64 *token )
{
    struct app_install_item_impl *impl = iface;
    app_install_add_handler( impl->status_handlers, handler, token );
    return S_OK;
}
static HRESULT WINAPI app_install_item_remove_StatusChanged( void *iface, INT64 token )
{
    struct app_install_item_impl *impl = iface;
    app_install_remove_handler( impl->status_handlers, token );
    return S_OK;
}
static HRESULT WINAPI app_install_item2_CancelWithTelemetry( void *iface, HSTRING cv )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl2));
    return app_install_item_Cancel( &impl->vtbl1 );
}
static HRESULT WINAPI app_install_item2_PauseWithTelemetry( void *iface, HSTRING cv )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl2));
    return app_install_item_Pause( &impl->vtbl1 );
}
static HRESULT WINAPI app_install_item2_RestartWithTelemetry( void *iface, HSTRING cv )
{
    struct app_install_item_impl *impl = (struct app_install_item_impl *)((char *)iface - offsetof(struct app_install_item_impl, vtbl2));
    return app_install_item_Restart( &impl->vtbl1 );
}
static HRESULT WINAPI app_install_item3_get_Children( void *iface, void **value )
{
    if (value) *value = store_create_empty_app_install_item_vector_view();
    return S_OK;
}
static HRESULT WINAPI app_install_item3_get_ItemOperationsMightAffectOtherItems( void *iface, boolean *value )
{
    if (value) *value = FALSE;
    return S_OK;
}

static const void *app_install_item_vtbl1[] =
{
    app_install_item_QueryInterface,
    app_install_item_AddRef,
    app_install_item_Release,
    app_install_item_GetIids,
    app_install_item_GetRuntimeClassName,
    app_install_item_GetTrustLevel,
    app_install_item_get_ProductId,
    app_install_item_get_PackageFamilyName,
    app_install_item_get_InstallType,
    app_install_item_get_IsUserInitiated,
    app_install_item_GetCurrentStatus,
    app_install_item_Cancel,
    app_install_item_Pause,
    app_install_item_Restart,
    app_install_item_add_Completed,
    app_install_item_remove_Completed,
    app_install_item_add_StatusChanged,
    app_install_item_remove_StatusChanged,
};

static const void *app_install_item_vtbl2[] =
{
    app_install_item2_QueryInterface,
    app_install_item2_AddRef,
    app_install_item2_Release,
    app_install_item_GetIids,
    app_install_item_GetRuntimeClassName,
    app_install_item_GetTrustLevel,
    app_install_item2_CancelWithTelemetry,
    app_install_item2_PauseWithTelemetry,
    app_install_item2_RestartWithTelemetry,
};

static const void *app_install_item_vtbl3[] =
{
    app_install_item3_QueryInterface,
    app_install_item3_AddRef,
    app_install_item3_Release,
    app_install_item_GetIids,
    app_install_item_GetRuntimeClassName,
    app_install_item_GetTrustLevel,
    app_install_item3_get_Children,
    app_install_item3_get_ItemOperationsMightAffectOtherItems,
};

static struct app_install_item_impl *app_install_start_item( HSTRING product_id, BOOL repair )
{
    struct app_install_item_impl *item = calloc( 1, sizeof(*item) );
    HANDLE thread;
    if (!item) return NULL;

    item->vtbl1 = app_install_item_vtbl1;
    item->vtbl2 = app_install_item_vtbl2;
    item->vtbl3 = app_install_item_vtbl3;
    item->ref = 1;
    if (product_id) WindowsDuplicateString( product_id, &item->product_id );
    else WindowsCreateString( L"", 0, &item->product_id );
    WindowsCreateString( L"", 0, &item->package_family_name );
    item->install_type = repair ? 2 : 0;
    item->is_user_initiated = TRUE;
    item->state = APP_INSTALL_STATE_PENDING;
    item->download_size = 0;
    item->bytes_downloaded = 0;
    item->percent_complete = 0.0;
    item->error_code = S_OK;
    item->ready_for_launch = FALSE;

    EnterCriticalSection( &app_install_cs );
    if (app_install_active_count < APP_INSTALL_MAX_ITEMS)
    {
        app_install_active_items[app_install_active_count++] = item;
        InterlockedIncrement( &item->ref );
    }
    LeaveCriticalSection( &app_install_cs );

    InterlockedIncrement( &item->ref );
    thread = CreateThread( NULL, 0, app_install_worker_proc, item, 0, NULL );
    if (thread) CloseHandle( thread );
    else InterlockedDecrement( &item->ref );

    return item;
}

static void *app_install_create_active_vector_view( void )
{
    IInspectable *items[APP_INSTALL_MAX_ITEMS];
    UINT32 count, i;
    struct winrt_vector_view *vv;

    EnterCriticalSection( &app_install_cs );
    count = app_install_active_count;
    for (i = 0; i < count; i++)
        items[i] = (IInspectable *)&app_install_active_items[i]->vtbl1;
    vv = store_create_vector_view( count ? items : NULL, count,
        &IID_IVectorView_AppInstallItem, &IID_IIterable_AppInstallItem );
    LeaveCriticalSection( &app_install_cs );
    if (vv) vv->iterator_iid = IID_IIterator_AppInstallItem;
    return vv;
}

static struct app_install_item_impl *app_install_find_by_product( HSTRING prod )
{
    const WCHAR *target = WindowsGetStringRawBuffer( prod, NULL );
    struct app_install_item_impl *found = NULL;
    UINT32 i;
    if (!target) return NULL;
    EnterCriticalSection( &app_install_cs );
    for (i = 0; i < app_install_active_count; i++)
    {
        const WCHAR *cur = WindowsGetStringRawBuffer( app_install_active_items[i]->product_id, NULL );
        if (cur && !wcscmp( cur, target ))
        {
            found = app_install_active_items[i];
            InterlockedIncrement( &found->ref );
            break;
        }
    }
    LeaveCriticalSection( &app_install_cs );
    return found;
}

/* Face 1 methods */
static HRESULT WINAPI app_install_mgr_get_AppInstallItems( void *iface, void **value )
{
    if (value) *value = app_install_create_active_vector_view();
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_Cancel( void *iface, HSTRING prod )
{
    struct app_install_item_impl *item = app_install_find_by_product( prod );
    if (item)
    {
        app_install_item_Cancel( &item->vtbl1 );
        IUnknown_Release( (IUnknown *)&item->vtbl1 );
    }
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_Pause( void *iface, HSTRING prod )
{
    struct app_install_item_impl *item = app_install_find_by_product( prod );
    if (item)
    {
        app_install_item_Pause( &item->vtbl1 );
        IUnknown_Release( (IUnknown *)&item->vtbl1 );
    }
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_Restart( void *iface, HSTRING prod )
{
    struct app_install_item_impl *item = app_install_find_by_product( prod );
    if (item)
    {
        app_install_item_Restart( &item->vtbl1 );
        IUnknown_Release( (IUnknown *)&item->vtbl1 );
    }
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_ItemCompleted( void *iface, void *handler, INT64 *token )
{
    app_install_add_handler( app_install_mgr_completed_handlers, handler, token );
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_remove_ItemCompleted( void *iface, INT64 token )
{
    app_install_remove_handler( app_install_mgr_completed_handlers, token );
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_ItemStatusChanged( void *iface, void *handler, INT64 *token )
{
    app_install_add_handler( app_install_mgr_status_handlers, handler, token );
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_remove_ItemStatusChanged( void *iface, INT64 token )
{
    app_install_remove_handler( app_install_mgr_status_handlers, token );
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_get_AutoUpdateSetting( void *iface, INT32 *setting )
{
    if (setting) *setting = 1; /* Enabled */
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_put_AutoUpdateSetting( void *iface, INT32 setting ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_get_AcquisitionIdentity( void *iface, HSTRING *value )
{
    if (value) WindowsCreateString( L"", 0, value );
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_put_AcquisitionIdentity( void *iface, HSTRING value ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_GetIsApplicableAsync( void *iface, HSTRING p, HSTRING s, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_StartAppInstallAsync( void *iface, HSTRING p, HSTRING s, BOOL r, BOOL f, void **op )
{
    struct app_install_item_impl *item = app_install_start_item( p, r );
    WARN( "StartAppInstallAsync product=%s sku=%s repair=%d\n", debugstr_hstring(p), debugstr_hstring(s), r );
    return store_async_create( item ? &item->vtbl1 : NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_UpdateAppByPackageFamilyNameAsync( void *iface, HSTRING p, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_SearchForUpdatesAsync( void *iface, HSTRING p, HSTRING s, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_SearchForAllUpdatesAsync( void *iface, void **op )
{
    return store_async_create( store_create_empty_app_install_item_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_IsStoreBlockedByPolicyAsync( void *iface, HSTRING n, HSTRING p, void **op )
{
    return store_async_create( NULL, TRUE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_GetIsAppAllowedToInstallAsync( void *iface, HSTRING p, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}

static const void *app_install_manager_vtbl1[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr_get_AppInstallItems,
    app_install_mgr_Cancel,
    app_install_mgr_Pause,
    app_install_mgr_Restart,
    app_install_mgr_ItemCompleted,
    app_install_mgr_remove_ItemCompleted,
    app_install_mgr_ItemStatusChanged,
    app_install_mgr_remove_ItemStatusChanged,
    app_install_mgr_get_AutoUpdateSetting,
    app_install_mgr_put_AutoUpdateSetting,
    app_install_mgr_get_AcquisitionIdentity,
    app_install_mgr_put_AcquisitionIdentity,
    app_install_mgr_GetIsApplicableAsync,
    app_install_mgr_StartAppInstallAsync,
    app_install_mgr_UpdateAppByPackageFamilyNameAsync,
    app_install_mgr_SearchForUpdatesAsync,
    app_install_mgr_SearchForAllUpdatesAsync,
    app_install_mgr_IsStoreBlockedByPolicyAsync,
    app_install_mgr_GetIsAppAllowedToInstallAsync,
};

/* Face 2 methods */
static HRESULT WINAPI app_install_mgr2_StartAppInstallWithTelemetryAsync( void *iface, HSTRING p, HSTRING s, BOOL r, BOOL f, HSTRING c, HSTRING b, HSTRING cv, void **op )
{
    struct app_install_item_impl *item = app_install_start_item( p, r );
    WARN( "StartAppInstallWithTelemetryAsync product=%s sku=%s repair=%d\n", debugstr_hstring(p), debugstr_hstring(s), r );
    return store_async_create( item ? &item->vtbl1 : NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_UpdateAppByPackageFamilyNameWithTelemetryAsync( void *iface, HSTRING p, HSTRING cv, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_SearchForUpdatesWithTelemetryAsync( void *iface, HSTRING p, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_SearchForAllUpdatesWithTelemetryAsync( void *iface, HSTRING cv, void **op )
{
    return store_async_create( store_create_empty_app_install_item_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_GetIsAppAllowedToInstallWithTelemetryAsync( void *iface, HSTRING p, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_CancelWithTelemetry( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }
static HRESULT WINAPI app_install_mgr2_PauseWithTelemetry( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }
static HRESULT WINAPI app_install_mgr2_RestartWithTelemetry( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }

static const void *app_install_manager_vtbl2[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr2_StartAppInstallWithTelemetryAsync,
    app_install_mgr2_UpdateAppByPackageFamilyNameWithTelemetryAsync,
    app_install_mgr2_SearchForUpdatesWithTelemetryAsync,
    app_install_mgr2_SearchForAllUpdatesWithTelemetryAsync,
    app_install_mgr2_GetIsAppAllowedToInstallWithTelemetryAsync,
    app_install_mgr2_CancelWithTelemetry,
    app_install_mgr2_PauseWithTelemetry,
    app_install_mgr2_RestartWithTelemetry,
};

/* Face 3 methods */
static HRESULT WINAPI app_install_mgr3_StartProductInstallAsync( void *iface, HSTRING p, HSTRING cat, HSTRING fl, HSTRING cl, BOOL r, BOOL f, HSTRING cv, void *vol, void **op )
{
    struct app_install_item_impl *item = app_install_start_item( p, r );
    IInspectable *arr[1];
    struct winrt_vector_view *vv;

    WARN( "StartProductInstallAsync product=%s catalog=%s flight=%s client=%s\n",
          debugstr_hstring(p), debugstr_hstring(cat), debugstr_hstring(fl), debugstr_hstring(cl) );
    if (item)
    {
        arr[0] = (IInspectable *)&item->vtbl1;
        vv = store_create_vector_view( arr, 1, &IID_IVectorView_AppInstallItem, &IID_IIterable_AppInstallItem );
        if (vv) vv->iterator_iid = IID_IIterator_AppInstallItem;
        IUnknown_Release( (IUnknown *)&item->vtbl1 );
        return store_async_create( vv, FALSE, FALSE, S_OK, op );
    }
    return store_async_create( store_create_empty_app_install_item_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_StartProductInstallForUserAsync( void *iface, void *u, HSTRING p, HSTRING cat, HSTRING fl, HSTRING cl, BOOL r, BOOL f, HSTRING cv, void *vol, void **op )
{
    struct app_install_item_impl *item = app_install_start_item( p, r );
    IInspectable *arr[1];
    struct winrt_vector_view *vv;

    WARN( "StartProductInstallForUserAsync user=%p product=%s catalog=%s flight=%s client=%s\n",
          u, debugstr_hstring(p), debugstr_hstring(cat), debugstr_hstring(fl), debugstr_hstring(cl) );
    if (item)
    {
        arr[0] = (IInspectable *)&item->vtbl1;
        vv = store_create_vector_view( arr, 1, &IID_IVectorView_AppInstallItem, &IID_IIterable_AppInstallItem );
        if (vv) vv->iterator_iid = IID_IIterator_AppInstallItem;
        IUnknown_Release( (IUnknown *)&item->vtbl1 );
        return store_async_create( vv, FALSE, FALSE, S_OK, op );
    }
    return store_async_create( store_create_empty_app_install_item_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_UpdateAppByPackageFamilyNameForUserAsync( void *iface, void *u, HSTRING p, HSTRING cv, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_SearchForUpdatesForUserAsync( void *iface, void *u, HSTRING p, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_SearchForAllUpdatesForUserAsync( void *iface, void *u, HSTRING cv, void **op )
{
    return store_async_create( store_create_empty_app_install_item_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_GetIsAppAllowedToInstallForUserAsync( void *iface, void *u, HSTRING p, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_GetIsApplicableForUserAsync( void *iface, void *u, HSTRING p, HSTRING s, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_MoveToFrontOfDownloadQueue( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }

static const void *app_install_manager_vtbl3[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr3_StartProductInstallAsync,
    app_install_mgr3_StartProductInstallForUserAsync,
    app_install_mgr3_UpdateAppByPackageFamilyNameForUserAsync,
    app_install_mgr3_SearchForUpdatesForUserAsync,
    app_install_mgr3_SearchForAllUpdatesForUserAsync,
    app_install_mgr3_GetIsAppAllowedToInstallForUserAsync,
    app_install_mgr3_GetIsApplicableForUserAsync,
    app_install_mgr3_MoveToFrontOfDownloadQueue,
};

/* Face 4 methods */
static HRESULT WINAPI app_install_mgr4_GetFreeUserEntitlementAsync( void *iface, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    WARN( "GetFreeUserEntitlementAsync storeId=%s campaign=%s cv=%s\n", debugstr_hstring(s), debugstr_hstring(c), debugstr_hstring(cv) );
    return store_async_create( &get_entitlement_result_obj, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr4_GetFreeUserEntitlementForUserAsync( void *iface, void *u, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    WARN( "GetFreeUserEntitlementForUserAsync user=%p storeId=%s campaign=%s cv=%s\n", u, debugstr_hstring(s), debugstr_hstring(c), debugstr_hstring(cv) );
    return store_async_create( &get_entitlement_result_obj, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr4_GetFreeDeviceEntitlementAsync( void *iface, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    WARN( "GetFreeDeviceEntitlementAsync storeId=%s campaign=%s cv=%s\n", debugstr_hstring(s), debugstr_hstring(c), debugstr_hstring(cv) );
    return store_async_create( &get_entitlement_result_obj, FALSE, FALSE, S_OK, op );
}

static const void *app_install_manager_vtbl4[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr4_GetFreeUserEntitlementAsync,
    app_install_mgr4_GetFreeUserEntitlementForUserAsync,
    app_install_mgr4_GetFreeDeviceEntitlementAsync,
};

/* Face 5 methods */
static HRESULT WINAPI app_install_mgr5_get_AppInstallItemsWithGroupSupport( void *iface, void **value )
{
    if (value) *value = app_install_create_active_vector_view();
    return S_OK;
}

static const void *app_install_manager_vtbl5[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr5_get_AppInstallItemsWithGroupSupport,
};

static struct app_install_manager_impl app_install_mgr_instance =
{
    app_install_manager_vtbl1,
    app_install_manager_vtbl2,
    app_install_manager_vtbl3,
    app_install_manager_vtbl4,
    app_install_manager_vtbl5,
    L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager",
    1
};

static HRESULT WINAPI app_install_manager_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    if (instance) *instance = (IInspectable *)&app_install_mgr_instance.vtbl1;
    return S_OK;
}
static const void *app_install_manager_custom_factory_vtbl[] =
{
    winappsdk_factory_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_factory_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    app_install_manager_factory_ActivateInstance,
};
struct winappsdk_factory app_install_manager_custom_factory =
{
    app_install_manager_custom_factory_vtbl, (struct winappsdk_class *)&app_install_mgr_instance, 1
};

/* --- StoreContext --- */
static HRESULT WINAPI store_context_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    TRACE( "StoreContext::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreContext ) ||
        IsEqualGUID( iid, &IID_IStoreContext2 ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI store_ctx_get_User( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI store_ctx_add_OfflineLicensesChanged( void *iface, void *h, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI store_ctx_remove_OfflineLicensesChanged( void *iface, INT64 token ) { return S_OK; }
static HRESULT WINAPI store_ctx_GetCustomerPurchaseIdAsync( void *iface, HSTRING st, HSTRING pu, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetCustomerCollectionsIdAsync( void *iface, HSTRING st, HSTRING pu, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetAppLicenseAsync( void *iface, void **op )
{
    return store_async_create( &store_app_license_obj, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetStoreProductForCurrentAppAsync( void *iface, void **op )
{
    WCHAR buf[MAX_PATH];
    const WCHAR *pkg_id = L"9WZDNCRFJBMP";
    struct store_product *prod;
    struct store_product_result *res;

    if (GetEnvironmentVariableW( L"WINE_PACKAGE_NAME", buf, ARRAY_SIZE(buf) ) ||
        package_get_reg_str( L"PackageName", buf, ARRAY_SIZE(buf) ))
        pkg_id = buf;

    prod = store_product_create( pkg_id, L"Microsoft Store", L"Microsoft Store", L"Application" );
    res = store_product_result_create( prod );
    if (prod) store_product_Release( &prod->vtbl );
    return store_async_create( res, FALSE, FALSE, S_OK, op );
}
static void get_app_store_products( const WCHAR *req_id, HSTRING *keys, IInspectable **vals, UINT32 max_count, UINT32 *actual_count )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR subkey[512], name[MAX_PATH], family[MAX_PATH], title[MAX_PATH];
    HKEY pkgs_key, key;
    DWORD index = 0, name_len, type, size, framework;
    UINT32 count = 0;
    SIZE_T r;

    if (req_id)
    {
        struct store_product *prod = store_product_create( req_id, req_id, req_id, L"Application" );
        if (prod && count < max_count)
        {
            WindowsCreateString( req_id, wcslen(req_id), &keys[count] );
            vals[count] = (IInspectable *)&prod->vtbl;
            count++;
        }
        *actual_count = count;
        return;
    }

    for (r = 0; r < ARRAY_SIZE(roots) && count < max_count; r++)
    {
        lstrcpyW( subkey, appmodel_reg_key );
        lstrcatW( subkey, L"\\Packages" );
        if (RegOpenKeyExW( roots[r], subkey, 0, KEY_READ, &pkgs_key )) continue;

        index = 0;
        name_len = ARRAY_SIZE(name);
        while (!RegEnumKeyExW( pkgs_key, index++, name, &name_len, NULL, NULL, NULL, NULL ) && count < max_count)
        {
            if (!RegOpenKeyExW( pkgs_key, name, 0, KEY_READ, &key ))
            {
                framework = 0;
                size = sizeof(framework);
                RegQueryValueExW( key, L"Framework", 0, &type, (BYTE *)&framework, &size );

                if (!framework)
                {
                    WCHAR *underscore;
                    const WCHAR *p = name;
                    family[0] = 0;
                    size = sizeof(family);
                    RegQueryValueExW( key, L"PackageFamilyName", 0, &type, (BYTE *)family, &size );

                    if (!wcsncmp( p, L"Microsoft.", 10 )) p += 10;
                    lstrcpynW( title, p, ARRAY_SIZE(title) );
                    underscore = wcschr( title, '_' );
                    if (underscore) *underscore = 0;

                    if (wcslen(title) > 0)
                    {
                        const WCHAR *id_str = family[0] ? family : name;
                        struct store_product *prod = store_product_create( id_str, title, title, L"Application" );
                        if (prod)
                        {
                            WindowsCreateString( id_str, wcslen(id_str), &keys[count] );
                            vals[count] = (IInspectable *)&prod->vtbl;
                            count++;
                        }
                    }
                }
                RegCloseKey( key );
            }
            name_len = ARRAY_SIZE(name);
        }
        RegCloseKey( pkgs_key );
    }

    if (!count)
    {
        WCHAR buf[MAX_PATH];
        const WCHAR *pkg_id = L"Microsoft.WindowsStore_8wekyb3d8bbwe";
        struct store_product *prod;
        if (GetEnvironmentVariableW( L"WINE_PACKAGE_NAME", buf, ARRAY_SIZE(buf) ) ||
            package_get_reg_str( L"PackageName", buf, ARRAY_SIZE(buf) ))
            pkg_id = buf;
        prod = store_product_create( pkg_id, L"Microsoft Store", L"Microsoft Store", L"Application" );
        if (prod)
        {
            WindowsCreateString( pkg_id, wcslen(pkg_id), &keys[count] );
            vals[count] = (IInspectable *)&prod->vtbl;
            count++;
        }
    }

    *actual_count = count;
}

static HRESULT WINAPI store_ctx_GetStoreProductsAsync( void *iface, void *pk, void *si, void **op )
{
    struct winrt_map_view *map;
    struct store_product_query_result *res;
    HSTRING keys[16];
    IInspectable *vals[16];
    UINT32 count = 0;

    if (si)
    {
        void *iter_unk = NULL;
        void **vtbl = *(void ***)si;
        HRESULT (WINAPI *pFirst)(void *, void **) = (void *)vtbl[6];
        if (SUCCEEDED(pFirst( si, &iter_unk )) && iter_unk)
        {
            void **iter_vtbl = *(void ***)iter_unk;
            HRESULT (WINAPI *pHasCurrent)(void *, boolean *) = (void *)iter_vtbl[7];
            HRESULT (WINAPI *pCurrent)(void *, HSTRING *) = (void *)iter_vtbl[6];
            HRESULT (WINAPI *pMoveNext)(void *, boolean *) = (void *)iter_vtbl[8];
            ULONG (WINAPI *pRelease)(void *) = (void *)iter_vtbl[2];
            boolean has = FALSE;

            if (SUCCEEDED(pHasCurrent( iter_unk, &has )))
            {
                while (has && count < ARRAY_SIZE(keys))
                {
                    HSTRING cur_id = NULL;
                    if (SUCCEEDED(pCurrent( iter_unk, &cur_id )) && cur_id)
                    {
                        const WCHAR *raw_id = WindowsGetStringRawBuffer( cur_id, NULL );
                        UINT32 added = 0;
                        get_app_store_products( raw_id, &keys[count], &vals[count], 1, &added );
                        count += added;
                    }
                    if (FAILED(pMoveNext( iter_unk, &has ))) break;
                }
            }
            pRelease( iter_unk );
        }
    }

    if (!count)
    {
        get_app_store_products( NULL, keys, vals, ARRAY_SIZE(keys), &count );
    }

    map = store_create_map_view( keys, vals, count, &IID_IMapView_StoreProduct, &IID_IIterable_StoreProductKeyValuePair, &IID_IKeyValuePair_StoreProduct );
    for (UINT32 i = 0; i < count; i++)
    {
        WindowsDeleteString( keys[i] );
        IInspectable_Release( vals[i] );
    }

    res = store_product_query_result_create( (void *)map );
    if (map) map_view_Release( &map->vtbl_map );
    return store_async_create( res, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetAssociatedStoreProductsAsync( void *iface, void *pk, void **op )
{
    return store_ctx_GetStoreProductsAsync( iface, pk, NULL, op );
}
static HRESULT WINAPI store_ctx_GetAssociatedStoreProductsWithPagingAsync( void *iface, void *pk, UINT32 m, void **op )
{
    struct winrt_map_view *map;
    struct store_product_paged_query_result *res;
    HSTRING keys[16];
    IInspectable *vals[16];
    UINT32 count = 0;

    get_app_store_products( NULL, keys, vals, ARRAY_SIZE(keys), &count );

    map = store_create_map_view( keys, vals, count, &IID_IMapView_StoreProduct, &IID_IIterable_StoreProductKeyValuePair, &IID_IKeyValuePair_StoreProduct );
    for (UINT32 i = 0; i < count; i++)
    {
        WindowsDeleteString( keys[i] );
        IInspectable_Release( vals[i] );
    }

    res = store_product_paged_query_result_create( (void *)map );
    if (map) map_view_Release( &map->vtbl_map );
    return store_async_create( res, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetUserCollectionAsync( void *iface, void *pk, void **op )
{
    return store_ctx_GetStoreProductsAsync( iface, pk, NULL, op );
}
static HRESULT WINAPI store_ctx_GetUserCollectionWithPagingAsync( void *iface, void *pk, UINT32 m, void **op )
{
    return store_ctx_GetAssociatedStoreProductsWithPagingAsync( iface, pk, m, op );
}
static HRESULT WINAPI store_ctx_ReportConsumableFulfillmentAsync( void *iface, HSTRING id, UINT32 q, GUID t, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetConsumableBalanceRemainingAsync( void *iface, HSTRING id, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_AcquireStoreLicenseForOptionalPackageAsync( void *iface, void *pkg, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestPurchaseAsync( void *iface, HSTRING id, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestPurchaseWithPropertiesAsync( void *iface, HSTRING id, void *props, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetAppAndOptionalStorePackageUpdatesAsync( void *iface, void **op )
{
    return store_async_create( store_create_empty_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestDownloadStorePackageUpdatesAsync( void *iface, void *upd, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestDownloadAndInstallStorePackageUpdatesAsync( void *iface, void *upd, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestDownloadAndInstallStorePackagesAsync( void *iface, void *ids, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_FindStoreProductForPackageAsync( void *iface, void *pk, void *pkg, void **op )
{
    return store_ctx_GetStoreProductForCurrentAppAsync( iface, op );
}

static const void *store_context_custom_vtbl[] =
{
    store_context_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    store_ctx_get_User,
    store_ctx_add_OfflineLicensesChanged,
    store_ctx_remove_OfflineLicensesChanged,
    store_ctx_GetCustomerPurchaseIdAsync,
    store_ctx_GetCustomerCollectionsIdAsync,
    store_ctx_GetAppLicenseAsync,
    store_ctx_GetStoreProductForCurrentAppAsync,
    store_ctx_GetStoreProductsAsync,
    store_ctx_GetAssociatedStoreProductsAsync,
    store_ctx_GetAssociatedStoreProductsWithPagingAsync,
    store_ctx_GetUserCollectionAsync,
    store_ctx_GetUserCollectionWithPagingAsync,
    store_ctx_ReportConsumableFulfillmentAsync,
    store_ctx_GetConsumableBalanceRemainingAsync,
    store_ctx_AcquireStoreLicenseForOptionalPackageAsync,
    store_ctx_RequestPurchaseAsync,
    store_ctx_RequestPurchaseWithPropertiesAsync,
    store_ctx_GetAppAndOptionalStorePackageUpdatesAsync,
    store_ctx_RequestDownloadStorePackageUpdatesAsync,
    store_ctx_RequestDownloadAndInstallStorePackageUpdatesAsync,
    store_ctx_RequestDownloadAndInstallStorePackagesAsync,
    store_ctx_FindStoreProductForPackageAsync,
};
static struct winappsdk_class store_context_custom_obj =
{
    store_context_custom_vtbl, L"Windows.Services.Store.StoreContext", 1
};

static HRESULT WINAPI store_context_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    TRACE( "StoreContextFactory::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IStoreContextStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = iface;
    return S_OK;
}
static HRESULT WINAPI store_context_factory_GetDefault( void *iface, void **context )
{
    if (context) *context = &store_context_custom_obj;
    return S_OK;
}
static HRESULT WINAPI store_context_factory_GetForUser( void *iface, void *user, void **context )
{
    if (context) *context = &store_context_custom_obj;
    return S_OK;
}
static const void *store_context_custom_factory_vtbl[] =
{
    store_context_factory_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_factory_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    store_context_factory_GetDefault,
    store_context_factory_GetForUser,
};
struct winappsdk_factory store_context_custom_factory =
{
    store_context_custom_factory_vtbl, &store_context_custom_obj, 1
};

/* --- StoreConfiguration --- */
struct store_configuration_factory_impl store_cfg_factory_instance;

static HRESULT WINAPI store_cfg_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
    struct store_configuration_factory_impl *cfg = &store_cfg_factory_instance;

    TRACE( "StoreConfigurationFactory::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics ))
        *out = (void *)&cfg->vtbl1;
    else if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics2 ))
        *out = (void *)&cfg->vtbl2;
    else if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics3 ))
        *out = (void *)&cfg->vtbl3;
    else if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics4 ))
        *out = (void *)&cfg->vtbl4;
    else
        *out = (void *)&cfg->vtbl1;

    InterlockedIncrement( &cfg->ref );
    return S_OK;
}
static ULONG WINAPI store_cfg_AddRef( void *iface ) { return InterlockedIncrement( &store_cfg_factory_instance.ref ); }
static ULONG WINAPI store_cfg_Release( void *iface ) { return InterlockedDecrement( &store_cfg_factory_instance.ref ); }
static HRESULT WINAPI store_cfg_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI store_cfg_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Store.Preview.StoreConfiguration", 55, n );
}
static HRESULT WINAPI store_cfg_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

/* StoreHardwareManufacturerInfo ({F292DC08-C654-43AC-A21F-34801C9D3388}) */
static const GUID IID_IStoreHardwareManufacturerInfo =
    { 0xf292dc08, 0xc654, 0x43ac, { 0xa2, 0x1f, 0x34, 0x80, 0x1c, 0x9d, 0x33, 0x88 } };

struct store_hw_info
{
    const void **vtbl;
    LONG ref;
};

static HRESULT WINAPI store_hw_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker_hw =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    struct store_hw_info *impl = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreHardwareManufacturerInfo ))
    {
        InterlockedIncrement( &impl->ref );
        *out = impl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IReferenceTracker_hw ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI store_hw_info_AddRef( void *iface ) { return InterlockedIncrement( &((struct store_hw_info *)iface)->ref ); }
static ULONG WINAPI store_hw_info_Release( void *iface )
{
    struct store_hw_info *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI store_hw_info_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI store_hw_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.ApplicationModel.Store.Preview.StoreHardwareManufacturerInfo";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI store_hw_info_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI store_hw_info_get_HardwareManufacturerId( void *iface, HSTRING *val )
{
    if (!val) return E_POINTER;
    return WindowsCreateString( L"WinePC", 6, val );
}
static HRESULT WINAPI store_hw_info_get_StoreContentModifierId( void *iface, HSTRING *val )
{
    if (!val) return E_POINTER;
    return WindowsCreateString( L"", 0, val );
}
static HRESULT WINAPI store_hw_info_get_ModelName( void *iface, HSTRING *val )
{
    if (!val) return E_POINTER;
    return WindowsCreateString( L"Desktop", 7, val );
}
static HRESULT WINAPI store_hw_info_get_ManufacturerName( void *iface, HSTRING *val )
{
    if (!val) return E_POINTER;
    return WindowsCreateString( L"Wine", 4, val );
}
static const void *store_hw_info_vtbl[] =
{
    store_hw_info_QueryInterface,
    store_hw_info_AddRef,
    store_hw_info_Release,
    store_hw_info_GetIids,
    store_hw_info_GetRuntimeClassName,
    store_hw_info_GetTrustLevel,
    store_hw_info_get_HardwareManufacturerId,
    store_hw_info_get_StoreContentModifierId,
    store_hw_info_get_ModelName,
    store_hw_info_get_ManufacturerName,
};

/* Statics 1 ({728F7FC0-8628-42EC-84A2-07780EB44D8B}):
 *   slot 6:  SetSystemConfiguration
 *   slot 7:  SetMobileOperatorConfiguration
 *   slot 8:  SetStoreWebAccountId
 *   slot 9:  IsStoreWebAccountId
 *   slot 10: get_HardwareManufacturerInfo
 *   slot 11: FilterUnsupportedSystemFeaturesAsync */
static HRESULT WINAPI store_cfg1_SetSystemConfiguration( void *iface, HSTRING m, HSTRING mod, UINT64 exp, HSTRING d ) { return S_OK; }
static HRESULT WINAPI store_cfg1_SetMobileOperatorConfiguration( void *iface, HSTRING m, UINT32 a, UINT32 u ) { return S_OK; }
static HRESULT WINAPI store_cfg1_SetStoreWebAccountId( void *iface, HSTRING id ) { return S_OK; }
static HRESULT WINAPI store_cfg1_IsStoreWebAccountId( void *iface, HSTRING id, boolean *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg1_get_HardwareManufacturerInfo( void *iface, void **val )
{
    struct store_hw_info *impl;
    if (!val) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl = store_hw_info_vtbl;
    impl->ref = 1;
    *val = impl;
    return S_OK;
}
static const GUID IID_IVectorView_StoreSystemFeature =
    { 0xc8f90757, 0xebcf, 0x5f2c, { 0xb9, 0x18, 0x64, 0x87, 0x10, 0x5b, 0x0c, 0xa1 } };
static const GUID IID_IIterable_StoreSystemFeature =
    { 0x98a8577a, 0xb128, 0x5400, { 0x8d, 0x3d, 0x58, 0x65, 0x4e, 0xaa, 0xf9, 0x57 } };
static const GUID IID_IIterator_StoreSystemFeature =
    { 0xd0455b2d, 0xd8aa, 0x557e, { 0x89, 0xa3, 0x63, 0xc3, 0x3e, 0x8c, 0xee, 0x99 } };

static HRESULT WINAPI store_cfg1_FilterUnsupportedSystemFeaturesAsync( void *iface, void *features, void **op )
{
    struct winrt_vector_view *vec = store_create_vector_view( NULL, 0,
        &IID_IVectorView_StoreSystemFeature, &IID_IIterable_StoreSystemFeature );
    if (vec) vec->iterator_iid = IID_IIterator_StoreSystemFeature;
    return store_async_create( vec, FALSE, FALSE, S_OK, op );
}

static const void *store_cfg_vtbl1[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg1_SetSystemConfiguration,
    store_cfg1_SetMobileOperatorConfiguration,
    store_cfg1_SetStoreWebAccountId,
    store_cfg1_IsStoreWebAccountId,
    store_cfg1_get_HardwareManufacturerInfo,
    store_cfg1_FilterUnsupportedSystemFeaturesAsync,
};

/* Statics 2 */
static HRESULT WINAPI store_cfg2_get_PurchasePromptingPolicy( void *iface, void **val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg2_put_PurchasePromptingPolicy( void *iface, void *val ) { return S_OK; }

static const void *store_cfg_vtbl2[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg2_get_PurchasePromptingPolicy,
    store_cfg2_put_PurchasePromptingPolicy,
};

/* Statics 3 */
static HRESULT WINAPI store_cfg3_HasStoreWebAccount( void *iface, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_HasStoreWebAccountForUser( void *iface, void *u, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_GetStoreLogDataAsync( void *iface, UINT32 opt, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_cfg3_SetStoreWebAccountIdForUser( void *iface, void *u, HSTRING id ) { return S_OK; }
static HRESULT WINAPI store_cfg3_IsStoreWebAccountIdForUser( void *iface, void *u, HSTRING id, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_GetPurchasePromptingPolicyForUser( void *iface, void *u, void **val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_SetPurchasePromptingPolicyForUser( void *iface, void *u, void *val ) { return S_OK; }

static const void *store_cfg_vtbl3[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg3_HasStoreWebAccount,
    store_cfg3_HasStoreWebAccountForUser,
    store_cfg3_GetStoreLogDataAsync,
    store_cfg3_SetStoreWebAccountIdForUser,
    store_cfg3_IsStoreWebAccountIdForUser,
    store_cfg3_GetPurchasePromptingPolicyForUser,
    store_cfg3_SetPurchasePromptingPolicyForUser,
};

/* Statics 4 */
static HRESULT WINAPI store_cfg4_GetStoreWebAccountId( void *iface, HSTRING *val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg4_GetStoreWebAccountIdForUser( void *iface, void *u, HSTRING *val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg4_SetEnterpriseStoreWebAccountId( void *iface, HSTRING id ) { return S_OK; }

static const void *store_cfg_vtbl4[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg4_GetStoreWebAccountId,
    store_cfg4_GetStoreWebAccountIdForUser,
    store_cfg4_SetEnterpriseStoreWebAccountId,
};

struct store_configuration_factory_impl store_cfg_factory_instance =
{
    store_cfg_vtbl1,
    store_cfg_vtbl2,
    store_cfg_vtbl3,
    store_cfg_vtbl4,
    L"Windows.ApplicationModel.Store.Preview.StoreConfiguration",
    1
};
