/* WinRT collection objects for windows.applicationmodel.dataexchange.dll
 *
 * Copyright 2026 Wine Developers
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

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

/* The Lookup miss code of the collection contracts (winerror.h does not ship it). */
#ifndef ELEMENT_NOT_FOUND
#define ELEMENT_NOT_FOUND 0x80070490
#endif

/*
 * Array-backed IVector`1<T>/IVectorView`1<T>/IIterable`1<T> and
 * IMap`2<HSTRING,V>/IMapView`2/IIterable`1 objects over the element kinds
 * this module stores: HSTRING (STRING) and COM pointer (COM).  The IIDs the
 * object answers are per-object fields, so a single vtable set serves every
 * instantiation, as the born-completed operations in async.c do.  All objects
 * are agile and free of the FTM; GetIids and GetTrustLevel follow the tree
 * convention of E_NOTIMPL.
 *
 * A map can be embedded in an owning object (the data package property set);
 * map_set_outer() then forwards its IUnknown and refcount to the owner, the
 * same shape the sibling modules use for embedded interface groups.
 */

static void *ref_elem( enum elem_kind kind, void *item )
{
    if (kind == ELEM_STRING)
    {
        HSTRING str = (HSTRING)item, dup = NULL;
        WindowsDuplicateString( str, &dup );
        return dup;
    }
    if (item) IUnknown_AddRef( (IUnknown *)item );
    return item;
}

static void unref_elem( enum elem_kind kind, void *item )
{
    if (kind == ELEM_STRING) WindowsDeleteString( (HSTRING)item );
    else if (item) IUnknown_Release( (IUnknown *)item );
}

static HRESULT create_class_name( const WCHAR *name, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, lstrlenW( name ), class_name );
}

/* ------------------------------------------------------------ vector ---- */

struct winrt_vector
{
    const void *vector_vtbl;     /* IVector`1<T> at offset 0 */
    const void *view_vtbl;       /* IVectorView`1<T> at offset 1 pointer */
    const void *iterable_vtbl;   /* IIterable`1<T> at offset 2 pointers */
    LONG ref;
    enum elem_kind kind;
    void **items;
    ULONG count, alloc;
    IID iid_vector, iid_view, iid_iterable, iid_iterator;
    const WCHAR *class_name;
};

struct vector_iterator
{
    const void *iterator_vtbl;
    LONG ref;
    struct winrt_vector *vec;
    ULONG pos;
    IID iid;
};

static inline struct vector_iterator *impl_from_vector_iterator( const void *iface )
{
    return (struct vector_iterator *)iface;
}

extern const void *winrt_vector_vtbl[];
extern const void *winrt_vector_view_vtbl[];
extern const void *winrt_vector_iterable_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct winrt_vector *impl_from_vector_any( const void *iface )
{
    static const void *groups[] = { winrt_vector_vtbl, winrt_vector_view_vtbl, winrt_vector_iterable_vtbl };
    static const int offsets[] = { 0, 1, 2 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct winrt_vector *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct winrt_vector *)iface;
}

static HRESULT WINAPI vector_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        (!IsEqualGUID( &impl->iid_vector, &GUID_NULL ) && IsEqualGUID( iid, &impl->iid_vector )))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (!IsEqualGUID( &impl->iid_view, &GUID_NULL ) && IsEqualGUID( iid, &impl->iid_view ))
    {
        *out = (void *)&impl->view_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (!IsEqualGUID( &impl->iid_iterable, &GUID_NULL ) && IsEqualGUID( iid, &impl->iid_iterable ))
    {
        *out = (void *)&impl->iterable_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI vector_AddRef( const void *iface )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI vector_Release( const void *iface )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        ULONG i;
        for (i = 0; i < impl->count; i++) unref_elem( impl->kind, impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI vector_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    return create_class_name( impl->class_name, class_name );
}

static HRESULT WINAPI vector_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI vector_GetAt( const void *iface, UINT32 index, void *value )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    if (!value) return E_POINTER;
    if (index >= impl->count) return E_INVALIDARG;
    *(void **)value = ref_elem( impl->kind, impl->items[index] );
    return S_OK;
}

static HRESULT WINAPI vector_get_Size( const void *iface, UINT32 *value )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->count;
    return S_OK;
}

static HRESULT WINAPI vector_GetView( const void *iface, void **value )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    /* The object answers the read-only view interface as well; hand it out. */
    *value = (void *)&impl->view_vtbl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI vector_IndexOf( const void *iface, void *element, UINT32 *index, BOOLEAN *found )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    ULONG i;

    TRACE( "iface %p, element %p, index %p, found %p.\n", iface, element, index, found );

    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;

    for (i = 0; i < impl->count; i++)
    {
        BOOL match;

        if (impl->kind == ELEM_STRING)
        {
            INT cmp = 0;
            WindowsCompareStringOrdinal( (HSTRING)impl->items[i], (HSTRING)element, &cmp );
            match = !cmp;
        }
        else match = impl->items[i] == element;

        if (match)
        {
            *index = i;
            *found = TRUE;
            break;
        }
    }
    return S_OK;
}

static HRESULT vector_reserve( struct winrt_vector *impl, ULONG total )
{
    if (total <= impl->alloc) return S_OK;
    {
        ULONG alloc = max( total, impl->alloc * 2 );
        void **items = realloc( impl->items, alloc * sizeof(*items) );
        if (!items) return E_OUTOFMEMORY;
        impl->items = items;
        impl->alloc = alloc;
    }
    return S_OK;
}

static HRESULT WINAPI vector_SetAt( const void *iface, UINT32 index, void *value )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    if (index >= impl->count) return E_INVALIDARG;
    unref_elem( impl->kind, impl->items[index] );
    impl->items[index] = ref_elem( impl->kind, value );
    return S_OK;
}

static HRESULT WINAPI vector_InsertAt( const void *iface, UINT32 index, void *value )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    HRESULT hr;
    ULONG i;

    TRACE( "iface %p, index %u, value %p.\n", iface, index, value );

    if (index > impl->count) return E_INVALIDARG;
    if (FAILED( (hr = vector_reserve( impl, impl->count + 1 )) )) return hr;

    for (i = impl->count; i > index; i--) impl->items[i] = impl->items[i - 1];
    impl->items[index] = ref_elem( impl->kind, value );
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI vector_RemoveAt( const void *iface, UINT32 index )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    ULONG i;

    TRACE( "iface %p, index %u.\n", iface, index );

    if (index >= impl->count) return E_INVALIDARG;
    unref_elem( impl->kind, impl->items[index] );
    for (i = index; i + 1 < impl->count; i++) impl->items[i] = impl->items[i + 1];
    impl->count--;
    return S_OK;
}

static HRESULT WINAPI vector_Append( const void *iface, void *value )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (FAILED( (hr = vector_reserve( impl, impl->count + 1 )) )) return hr;
    impl->items[impl->count++] = ref_elem( impl->kind, value );
    return S_OK;
}

static HRESULT WINAPI vector_RemoveAtEnd( const void *iface )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );

    TRACE( "iface %p.\n", iface );

    if (!impl->count) return E_INVALIDARG;
    unref_elem( impl->kind, impl->items[--impl->count] );
    return S_OK;
}

static HRESULT WINAPI vector_Clear( const void *iface )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    ULONG i;

    TRACE( "iface %p.\n", iface );

    for (i = 0; i < impl->count; i++) unref_elem( impl->kind, impl->items[i] );
    impl->count = 0;
    return S_OK;
}

static HRESULT WINAPI vector_GetMany( const void *iface, UINT32 start, UINT32 capacity, void *items, UINT32 *count )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    ULONG i, n;

    TRACE( "iface %p, start %u, capacity %u, items %p, count %p.\n", iface, start, capacity, items, count );

    if (!count) return E_POINTER;
    if (start > impl->count) return E_INVALIDARG;

    n = impl->count - start;
    if (capacity && n > capacity) n = capacity;
    for (i = 0; i < n; i++) *(void **)((const BYTE *)items + i * sizeof(void *)) = ref_elem( impl->kind, impl->items[start + i] );
    *count = n;
    return S_OK;
}

static HRESULT WINAPI vector_ReplaceAll( const void *iface, UINT32 size, void *items )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    ULONG i;
    HRESULT hr;

    (void)impl;

    TRACE( "iface %p, size %u, items %p.\n", iface, size, items );

    if (FAILED( (hr = vector_Clear( iface )) )) return hr;
    for (i = 0; i < size; i++)
    {
        void *item = *(void **)((const BYTE *)items + i * sizeof(void *));
        if (FAILED( (hr = vector_Append( iface, item )) )) return hr;
    }
    return S_OK;
}

/* Group-side IUnknown entry points: the QI/AddRef/Release slots of every
 * interface group resolve the object from their own vtable offset. */

static HRESULT WINAPI vecview_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return vector_QueryInterface( (void *)impl_from_vector_any( iface ), iid, out );
}

static ULONG WINAPI vecview_AddRef( const void *iface )
{
    return vector_AddRef( (void *)impl_from_vector_any( iface ) );
}

static ULONG WINAPI vecview_Release( const void *iface )
{
    return vector_Release( (void *)impl_from_vector_any( iface ) );
}

static HRESULT WINAPI veciter_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return vector_QueryInterface( (void *)impl_from_vector_any( iface ), iid, out );
}

static ULONG WINAPI veciter_AddRef( const void *iface )
{
    return vector_AddRef( (void *)impl_from_vector_any( iface ) );
}

static ULONG WINAPI veciter_Release( const void *iface )
{
    return vector_Release( (void *)impl_from_vector_any( iface ) );
}

static HRESULT WINAPI vector_First( const void *iface, void **value );

const void *winrt_vector_vtbl[] =
{
    vector_QueryInterface,
    vector_AddRef,
    vector_Release,
    vector_GetIids,
    vector_GetRuntimeClassName,
    vector_GetTrustLevel,
    vector_GetAt,
    vector_get_Size,
    vector_GetView,
    vector_IndexOf,
    vector_SetAt,
    vector_InsertAt,
    vector_RemoveAt,
    vector_Append,
    vector_RemoveAtEnd,
    vector_Clear,
    vector_GetMany,
    vector_ReplaceAll,
};

const void *winrt_vector_view_vtbl[] =
{
    vecview_QueryInterface,
    vecview_AddRef,
    vecview_Release,
    vector_GetIids,
    vector_GetRuntimeClassName,
    vector_GetTrustLevel,
    vector_GetAt,
    vector_get_Size,
    vector_IndexOf,
    vector_GetMany,
};

/* ------------------------------------------------------- vector iterator */

static HRESULT WINAPI viter_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct vector_iterator *impl = impl_from_vector_iterator( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->iid ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI viter_AddRef( const void *iface )
{
    struct vector_iterator *impl = impl_from_vector_iterator( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI viter_Release( const void *iface )
{
    struct vector_iterator *impl = impl_from_vector_iterator( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        vector_release( impl->vec );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI viter_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI viter_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Foundation.Collections.IIterator`1", class_name );
}

static HRESULT WINAPI viter_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI viter_get_Current( const void *iface, void *value )
{
    struct vector_iterator *impl = impl_from_vector_iterator( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->pos >= impl->vec->count) return E_FAIL;
    *(void **)value = ref_elem( impl->vec->kind, impl->vec->items[impl->pos] );
    return S_OK;
}

static HRESULT WINAPI viter_get_HasCurrent( const void *iface, BOOLEAN *value )
{
    struct vector_iterator *impl = impl_from_vector_iterator( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->pos < impl->vec->count;
    return S_OK;
}

static HRESULT WINAPI viter_MoveNext( const void *iface, BOOLEAN *value )
{
    struct vector_iterator *impl = impl_from_vector_iterator( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->pos < impl->vec->count) impl->pos++;
    *value = impl->pos < impl->vec->count;
    return S_OK;
}

static HRESULT WINAPI viter_GetMany( const void *iface, UINT32 capacity, void *items, UINT32 *count )
{
    struct vector_iterator *impl = impl_from_vector_iterator( iface );
    ULONG i, n;

    TRACE( "iface %p, capacity %u, items %p, count %p.\n", iface, capacity, items, count );

    if (!count) return E_POINTER;

    n = impl->vec->count - impl->pos;
    if (capacity && n > capacity) n = capacity;
    for (i = 0; i < n; i++)
    {
        *(void **)((const BYTE *)items + i * sizeof(void *)) = ref_elem( impl->vec->kind, impl->vec->items[impl->pos + i] );
        impl->pos++;
    }
    *count = n;
    return S_OK;
}

const void *winrt_vector_iterator_vtbl[] =
{
    viter_QueryInterface,
    viter_AddRef,
    viter_Release,
    viter_GetIids,
    viter_GetRuntimeClassName,
    viter_GetTrustLevel,
    viter_get_Current,
    viter_get_HasCurrent,
    viter_MoveNext,
    viter_GetMany,
};

static HRESULT WINAPI vector_First( const void *iface, void **value )
{
    struct winrt_vector *impl = impl_from_vector_any( iface );
    struct vector_iterator *iter;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (IsEqualGUID( &impl->iid_iterator, &GUID_NULL )) return E_NOINTERFACE;

    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->iterator_vtbl = winrt_vector_iterator_vtbl;
    iter->ref = 1;
    iter->vec = impl;
    vector_add_ref( impl );
    iter->iid = impl->iid_iterator;

    *value = (void *)iter;
    return S_OK;
}

const void *winrt_vector_iterable_vtbl[] =
{
    veciter_QueryInterface,
    veciter_AddRef,
    veciter_Release,
    vector_GetIids,
    vector_GetRuntimeClassName,
    vector_GetTrustLevel,
    vector_First,
};

struct winrt_vector *create_vector( const IID *iid_vector, const IID *iid_view, const IID *iid_iterable,
                                    const IID *iid_iterator, enum elem_kind kind, const WCHAR *class_name )
{
    struct winrt_vector *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->vector_vtbl = winrt_vector_vtbl;
    impl->view_vtbl = winrt_vector_view_vtbl;
    impl->iterable_vtbl = winrt_vector_iterable_vtbl;
    impl->ref = 1;
    impl->kind = kind;
    if (iid_vector) impl->iid_vector = *iid_vector;
    if (iid_view) impl->iid_view = *iid_view;
    if (iid_iterable) impl->iid_iterable = *iid_iterable;
    if (iid_iterator) impl->iid_iterator = *iid_iterator;
    impl->class_name = class_name;

    TRACE( "created vector %p.\n", impl );
    return impl;
}

void vector_add_ref( struct winrt_vector *impl ) { InterlockedIncrement( &impl->ref ); }

void vector_release( struct winrt_vector *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        ULONG i;
        for (i = 0; i < impl->count; i++) unref_elem( impl->kind, impl->items[i] );
        free( impl->items );
        free( impl );
    }
}

ULONG vector_size( struct winrt_vector *impl ) { return impl->count; }

HRESULT vector_append( struct winrt_vector *impl, void *item )
{
    HRESULT hr;

    if (FAILED( (hr = vector_reserve( impl, impl->count + 1 )) )) return hr;
    impl->items[impl->count++] = ref_elem( impl->kind, item );
    return S_OK;
}

void *vector_get_at( struct winrt_vector *impl, ULONG index )
{
    return index < impl->count ? impl->items[index] : NULL;
}

void *vector_iface( struct winrt_vector *impl ) { return (void *)impl; }
void *vector_view_iface( struct winrt_vector *impl ) { return (void *)&impl->view_vtbl; }
void *vector_iterable_iface( struct winrt_vector *impl ) { return (void *)&impl->iterable_vtbl; }

/* --------------------------------------------------------------- map ---- */

struct map_entry
{
    HSTRING key;
    void *value;
};

struct winrt_map
{
    const void *map_vtbl;        /* IMap`2<K,V> at offset 0 */
    const void *view_vtbl;       /* IMapView`2<K,V> at offset 1 pointer */
    const void *iterable_vtbl;   /* IIterable`1<KVP> at offset 2 pointers */
    LONG ref;
    enum elem_kind kind;
    struct map_entry *entries;
    ULONG count, alloc;
    IID iid_map, iid_view, iid_iterable, iid_iterator, iid_kvp;
    IInspectable *outer;
    const WCHAR *class_name;
};

struct keyvalue_pair
{
    const void *kvp_vtbl;
    LONG ref;
    HSTRING key;
    void *value;
    enum elem_kind kind;
    IID iid;
};

struct map_iterator
{
    const void *iterator_vtbl;
    LONG ref;
    struct winrt_map *map;
    ULONG pos;
    IID iid;
};

static inline struct keyvalue_pair *impl_from_kvp( const void *iface )
{
    return (struct keyvalue_pair *)iface;
}

static inline struct map_iterator *impl_from_map_iterator( const void *iface )
{
    return (struct map_iterator *)iface;
}

extern const void *winrt_map_vtbl[];
extern const void *winrt_map_view_vtbl[];
extern const void *winrt_map_iterable_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct winrt_map *impl_from_map_any( const void *iface )
{
    static const void *groups[] = { winrt_map_vtbl, winrt_map_view_vtbl, winrt_map_iterable_vtbl };
    static const int offsets[] = { 0, 1, 2 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct winrt_map *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct winrt_map *)iface;
}

static HRESULT WINAPI map_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct winrt_map *impl = impl_from_map_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        if (impl->outer) return IInspectable_QueryInterface( impl->outer, iid, out );
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (!IsEqualGUID( &impl->iid_map, &GUID_NULL ) && IsEqualGUID( iid, &impl->iid_map ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (!IsEqualGUID( &impl->iid_view, &GUID_NULL ) && IsEqualGUID( iid, &impl->iid_view ))
    {
        *out = (void *)&impl->view_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (!IsEqualGUID( &impl->iid_iterable, &GUID_NULL ) && IsEqualGUID( iid, &impl->iid_iterable ))
    {
        *out = (void *)&impl->iterable_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI map_AddRef( const void *iface )
{
    struct winrt_map *impl = impl_from_map_any( iface );

    if (impl->outer) return IInspectable_AddRef( impl->outer );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI map_Release( const void *iface )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    ULONG ref;

    if (impl->outer) return IInspectable_Release( impl->outer );
    ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        ULONG i;
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->entries[i].key );
            unref_elem( impl->kind, impl->entries[i].value );
        }
        free( impl->entries );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI map_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI map_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    return create_class_name( impl->class_name, class_name );
}

static HRESULT WINAPI map_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static LONG map_find( struct winrt_map *impl, HSTRING key )
{
    ULONG i;

    for (i = 0; i < impl->count; i++)
    {
        INT cmp = 1;
        WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp );
        if (!cmp) return i;
    }
    return -1;
}

static HRESULT WINAPI map_Lookup( const void *iface, HSTRING key, void *value )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    LONG idx;

    TRACE( "iface %p, key %s, value %p.\n", iface, debugstr_hstring( key ), value );

    if (!value) return E_POINTER;
    idx = map_find( impl, key );
    if (idx < 0) return ELEMENT_NOT_FOUND;
    *(void **)value = ref_elem( impl->kind, impl->entries[idx].value );
    return S_OK;
}

static HRESULT WINAPI map_get_Size( const void *iface, UINT32 *value )
{
    struct winrt_map *impl = impl_from_map_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->count;
    return S_OK;
}

static HRESULT WINAPI map_HasKey( const void *iface, HSTRING key, BOOLEAN *found )
{
    struct winrt_map *impl = impl_from_map_any( iface );

    TRACE( "iface %p, key %s, found %p.\n", iface, debugstr_hstring( key ), found );

    if (!found) return E_POINTER;
    *found = map_find( impl, key ) >= 0;
    return S_OK;
}

static HRESULT WINAPI map_GetView( const void *iface, void **value )
{
    struct winrt_map *impl = impl_from_map_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = (void *)&impl->view_vtbl;
    if (impl->outer) IInspectable_AddRef( impl->outer );
    else InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI map_Insert( const void *iface, HSTRING key, void *value, BOOLEAN *replaced )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    LONG idx;
    HRESULT hr;

    TRACE( "iface %p, key %s, value %p, replaced %p.\n", iface, debugstr_hstring( key ), value, replaced );

    if (!replaced) return E_POINTER;
    *replaced = FALSE;

    idx = map_find( impl, key );
    if (idx >= 0)
    {
        unref_elem( impl->kind, impl->entries[idx].value );
        impl->entries[idx].value = ref_elem( impl->kind, value );
        *replaced = TRUE;
        return S_OK;
    }

    if (impl->count == impl->alloc)
    {
        ULONG alloc = max( impl->alloc * 2, 8 );
        struct map_entry *entries = realloc( impl->entries, alloc * sizeof(*entries) );
        if (!entries) return E_OUTOFMEMORY;
        impl->entries = entries;
        impl->alloc = alloc;
    }

    if (FAILED( (hr = WindowsDuplicateString( key, &impl->entries[impl->count].key )) )) return hr;
    impl->entries[impl->count].value = ref_elem( impl->kind, value );
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI map_Remove( const void *iface, HSTRING key )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    LONG idx;
    ULONG i;

    TRACE( "iface %p, key %s.\n", iface, debugstr_hstring( key ) );

    idx = map_find( impl, key );
    if (idx < 0) return E_INVALIDARG;

    WindowsDeleteString( impl->entries[idx].key );
    unref_elem( impl->kind, impl->entries[idx].value );
    for (i = idx; i + 1 < impl->count; i++) impl->entries[i] = impl->entries[i + 1];
    impl->count--;
    return S_OK;
}

static HRESULT WINAPI map_Clear( const void *iface )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    ULONG i;

    TRACE( "iface %p.\n", iface );

    for (i = 0; i < impl->count; i++)
    {
        WindowsDeleteString( impl->entries[i].key );
        unref_elem( impl->kind, impl->entries[i].value );
    }
    impl->count = 0;
    return S_OK;
}

const void *winrt_map_vtbl[] =
{
    map_QueryInterface,
    map_AddRef,
    map_Release,
    map_GetIids,
    map_GetRuntimeClassName,
    map_GetTrustLevel,
    map_Lookup,
    map_get_Size,
    map_HasKey,
    map_GetView,
    map_Insert,
    map_Remove,
    map_Clear,
};

static HRESULT WINAPI mapview_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return map_QueryInterface( (void *)impl_from_map_any( iface ), iid, out );
}

static ULONG WINAPI mapview_AddRef( const void *iface )
{
    return map_AddRef( (void *)impl_from_map_any( iface ) );
}

static ULONG WINAPI mapview_Release( const void *iface )
{
    return map_Release( (void *)impl_from_map_any( iface ) );
}

static HRESULT WINAPI mapiter_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return map_QueryInterface( (void *)impl_from_map_any( iface ), iid, out );
}

static ULONG WINAPI mapiter_AddRef( const void *iface )
{
    return map_AddRef( (void *)impl_from_map_any( iface ) );
}

static ULONG WINAPI mapiter_Release( const void *iface )
{
    return map_Release( (void *)impl_from_map_any( iface ) );
}

static HRESULT WINAPI map_First( const void *iface, void **value );

/* IMapView view-side: same storage, no mutation entry points. */
static HRESULT WINAPI mapview_Lookup( const void *iface, HSTRING key, void *value )
{
    return map_Lookup( iface, key, value );
}

static HRESULT WINAPI mapview_get_Size( const void *iface, UINT32 *value )
{
    return map_get_Size( iface, value );
}

static HRESULT WINAPI mapview_HasKey( const void *iface, HSTRING key, BOOLEAN *found )
{
    return map_HasKey( iface, key, found );
}

static HRESULT WINAPI mapview_Split( const void *iface, void **first, void **second )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    struct winrt_map *empty;

    TRACE( "iface %p, first %p, second %p.\n", iface, first, second );

    if (!first || !second) return E_POINTER;

    *first = (void *)&impl->view_vtbl;
    if (impl->outer) IInspectable_AddRef( impl->outer );
    else InterlockedIncrement( &impl->ref );

    if (!(empty = create_map( NULL, &impl->iid_view, NULL, NULL, NULL, impl->kind, impl->class_name )))
    {
        if (impl->outer) IInspectable_Release( impl->outer );
        else InterlockedDecrement( &impl->ref );
        *first = NULL;
        return E_OUTOFMEMORY;
    }
    *second = (void *)empty;
    return S_OK;
}

const void *winrt_map_view_vtbl[] =
{
    mapview_QueryInterface,
    mapview_AddRef,
    mapview_Release,
    map_GetIids,
    map_GetRuntimeClassName,
    map_GetTrustLevel,
    mapview_Lookup,
    mapview_get_Size,
    mapview_HasKey,
    mapview_Split,
};

/* ----------------------------------------------------- key value pair ---- */

static HRESULT WINAPI kvp_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct keyvalue_pair *impl = impl_from_kvp( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->iid ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI kvp_AddRef( const void *iface )
{
    struct keyvalue_pair *impl = impl_from_kvp( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI kvp_Release( const void *iface )
{
    struct keyvalue_pair *impl = impl_from_kvp( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->key );
        unref_elem( impl->kind, impl->value );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI kvp_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI kvp_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Foundation.Collections.IKeyValuePair`2", class_name );
}

static HRESULT WINAPI kvp_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI kvp_get_Key( const void *iface, HSTRING *value )
{
    struct keyvalue_pair *impl = impl_from_kvp( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->key, value );
}

static HRESULT WINAPI kvp_get_Value( const void *iface, void *value )
{
    struct keyvalue_pair *impl = impl_from_kvp( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *(void **)value = ref_elem( impl->kind, impl->value );
    return S_OK;
}

const void *winrt_kvp_vtbl[] =
{
    kvp_QueryInterface,
    kvp_AddRef,
    kvp_Release,
    kvp_GetIids,
    kvp_GetRuntimeClassName,
    kvp_GetTrustLevel,
    kvp_get_Key,
    kvp_get_Value,
};

static HRESULT create_kvp( struct winrt_map *impl, ULONG index, struct keyvalue_pair **out )
{
    struct keyvalue_pair *kvp;

    if (!(kvp = calloc( 1, sizeof(*kvp) ))) return E_OUTOFMEMORY;
    kvp->kvp_vtbl = winrt_kvp_vtbl;
    kvp->ref = 1;
    WindowsDuplicateString( impl->entries[index].key, &kvp->key );
    kvp->value = ref_elem( impl->kind, impl->entries[index].value );
    kvp->kind = impl->kind;
    kvp->iid = impl->iid_kvp;
    *out = kvp;
    return S_OK;
}

/* ------------------------------------------------------- map iterator --- */

static HRESULT WINAPI miter_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct map_iterator *impl = impl_from_map_iterator( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->iid ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI miter_AddRef( const void *iface )
{
    struct map_iterator *impl = impl_from_map_iterator( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI miter_Release( const void *iface )
{
    struct map_iterator *impl = impl_from_map_iterator( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        map_release( impl->map );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI miter_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI miter_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    return create_class_name( L"Windows.Foundation.Collections.IIterator`1", class_name );
}

static HRESULT WINAPI miter_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI miter_get_Current( const void *iface, void *value )
{
    struct map_iterator *impl = impl_from_map_iterator( iface );
    struct keyvalue_pair *kvp;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->pos >= impl->map->count) return E_FAIL;
    if (FAILED( (hr = create_kvp( impl->map, impl->pos, &kvp )) )) return hr;
    *(void **)value = kvp;
    return S_OK;
}

static HRESULT WINAPI miter_get_HasCurrent( const void *iface, BOOLEAN *value )
{
    struct map_iterator *impl = impl_from_map_iterator( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->pos < impl->map->count;
    return S_OK;
}

static HRESULT WINAPI miter_MoveNext( const void *iface, BOOLEAN *value )
{
    struct map_iterator *impl = impl_from_map_iterator( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->pos < impl->map->count) impl->pos++;
    *value = impl->pos < impl->map->count;
    return S_OK;
}

static HRESULT WINAPI miter_GetMany( const void *iface, UINT32 capacity, void *items, UINT32 *count )
{
    struct map_iterator *impl = impl_from_map_iterator( iface );
    ULONG i, n;

    TRACE( "iface %p, capacity %u, items %p, count %p.\n", iface, capacity, items, count );

    if (!count) return E_POINTER;

    n = impl->map->count - impl->pos;
    if (capacity && n > capacity) n = capacity;
    for (i = 0; i < n; i++)
    {
        struct keyvalue_pair *kvp;
        HRESULT hr = create_kvp( impl->map, impl->pos + i, &kvp );
        if (FAILED( hr ))
        {
            *count = i;
            return hr;
        }
        *(void **)((const BYTE *)items + i * sizeof(void *)) = kvp;
        impl->pos++;
    }
    *count = n;
    return S_OK;
}

const void *winrt_map_iterator_vtbl[] =
{
    miter_QueryInterface,
    miter_AddRef,
    miter_Release,
    miter_GetIids,
    miter_GetRuntimeClassName,
    miter_GetTrustLevel,
    miter_get_Current,
    miter_get_HasCurrent,
    miter_MoveNext,
    miter_GetMany,
};

static HRESULT WINAPI map_First( const void *iface, void **value )
{
    struct winrt_map *impl = impl_from_map_any( iface );
    struct map_iterator *iter;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (IsEqualGUID( &impl->iid_iterator, &GUID_NULL )) return E_NOINTERFACE;

    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->iterator_vtbl = winrt_map_iterator_vtbl;
    iter->ref = 1;
    iter->map = impl;
    map_add_ref( impl );
    iter->pos = 0;
    iter->iid = impl->iid_iterator;

    *value = (void *)iter;
    return S_OK;
}

const void *winrt_map_iterable_vtbl[] =
{
    mapiter_QueryInterface,
    mapiter_AddRef,
    mapiter_Release,
    map_GetIids,
    map_GetRuntimeClassName,
    map_GetTrustLevel,
    map_First,
};

struct winrt_map *create_map( const IID *iid_map, const IID *iid_view, const IID *iid_iterable,
                              const IID *iid_iterator, const IID *iid_kvp, enum elem_kind kind,
                              const WCHAR *class_name )
{
    struct winrt_map *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->map_vtbl = winrt_map_vtbl;
    impl->view_vtbl = winrt_map_view_vtbl;
    impl->iterable_vtbl = winrt_map_iterable_vtbl;
    impl->ref = 1;
    impl->kind = kind;
    if (iid_map) impl->iid_map = *iid_map;
    if (iid_view) impl->iid_view = *iid_view;
    if (iid_iterable) impl->iid_iterable = *iid_iterable;
    if (iid_iterator) impl->iid_iterator = *iid_iterator;
    if (iid_kvp) impl->iid_kvp = *iid_kvp;
    impl->class_name = class_name;

    TRACE( "created map %p.\n", impl );
    return impl;
}

void map_add_ref( struct winrt_map *impl )
{
    if (impl->outer) IInspectable_AddRef( impl->outer );
    else InterlockedIncrement( &impl->ref );
}

void map_release( struct winrt_map *impl )
{
    ULONG ref;

    if (impl->outer)
    {
        IInspectable_Release( impl->outer );
        return;
    }
    ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        ULONG i;
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->entries[i].key );
            unref_elem( impl->kind, impl->entries[i].value );
        }
        free( impl->entries );
        free( impl );
    }
}

void map_set_outer( struct winrt_map *impl, IInspectable *outer ) { impl->outer = outer; }

HRESULT map_insert( struct winrt_map *impl, HSTRING key, void *value )
{
    BOOLEAN replaced;
    return map_Insert( (const void *)&impl->map_vtbl, key, value, &replaced );
}

HRESULT map_lookup( struct winrt_map *impl, HSTRING key, void **value )
{
    return map_Lookup( (const void *)&impl->map_vtbl, key, value );
}

ULONG map_size( struct winrt_map *impl ) { return impl->count; }

void *map_iface( struct winrt_map *impl ) { return (void *)impl; }
void *map_view_iface( struct winrt_map *impl ) { return (void *)&impl->view_vtbl; }
void *map_iterable_iface( struct winrt_map *impl ) { return (void *)&impl->iterable_vtbl; }
