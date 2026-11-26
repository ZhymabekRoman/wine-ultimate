/* WinUI3 generic collection + IIterable/IVectorView views (RowDefinitionCollection).
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

#include <stdarg.h>

#include "private.h"
#include "weakreference.h"

#include "wine/exception.h"
#include "uwp_private.h"
#include "winui_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* Generic collection for RowDefinitions, ColumnDefinitions, Children */
struct xaml_collection *impl_from_collection( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_collection, IInspectable_iface );
}

/* WinUI3 RowDefinitionCollection enumerable IV (IIterable<RowDefinition> dephased view) */
static const GUID IID_IIterable_RowDef =
    { 0x5ddd9577, 0x3f94, 0x567f, { 0xbe, 0xef, 0x54, 0x05, 0x68, 0x52, 0x22, 0x89 } };

/* WinUI3 RowDefinitionCollection IV3 = IVectorView<RowDefinition> (GetMany at slot 10) */
static const GUID IID_IVectorView_RowDef =
    { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

static const void *collection_view_vtbl[256];
void **vtbl_dup( const void *src );
static HRESULT collection_iterable_view( struct xaml_collection *coll, void **out );

static HRESULT WINAPI collection_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_collection *impl = impl_from_collection( iface );

    TRACE( "collection_QueryInterface %p %s\n", iface, debugstr_guid(iid) );
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IIterable_RowDef ))
        return collection_iterable_view( impl, out );

    if (IsEqualGUID( iid, &IID_IVectorView_RowDef ))
    {
        struct xaml_collection *v = impl->vector_view;
        if (!v)
        {
            if (!(v = calloc( 1, sizeof(*v) ))) return E_OUTOFMEMORY;
            *v = *impl;                                   /* shallow: items/count shared */
            v->ref = 1;
            v->capacity = ~0u;                            /* sentinel: view owns no array */
            v->vector_view = v;
            v->iterable_view = NULL;
            v->parent = impl;
            v->IInspectable_iface.lpVtbl =
                (const struct IInspectableVtbl *)vtbl_dup( collection_view_vtbl );
            impl->vector_view = v;
            TRACE( "collection QI %p IV3 vectorview created %p\n", iface, v );
        }
        else InterlockedIncrement( &v->ref );
        *out = (IInspectable *)v;
        return S_OK;
    }

    *out = &impl->IInspectable_iface;
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI collection_AddRef( IInspectable *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "collection_AddRef iface %p ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI collection_Release( IInspectable *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        unsigned int i;
        if (impl->capacity == ~0u)                        /* IV3 view shard: array shared */
        {
            TRACE( "collection_Release view %p freed\n", iface );
            if (impl->vector_view == impl) impl->vector_view = NULL;
            if (impl->parent)                             /* drop parent's stale cache slot */
            {
                struct xaml_collection *p = impl->parent;
                if (p->vector_view == (void *)impl) p->vector_view = NULL;
            }
            free( impl );
            return 0;
        }
        InterlockedIncrement( &impl->ref );               /* leak-keep: GC-strand guard */
        return 1;
        for (i = 0; i < impl->count; i++)
            if (impl->items[i]) IInspectable_Release( impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI collection_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI collection_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Foundation.Collections.IVector`1<Windows.UI.Xaml.Controls.RowDefinition>";
    TRACE( "collection_GetRuntimeClassName iface %p\n", iface );
    return WindowsCreateString( nm, ARRAYSIZE(nm) - 1, name );
}

static HRESULT WINAPI collection_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    TRACE( "collection_GetTrustLevel iface %p\n", iface );
    if (level) *level = BaseTrust;
    return S_OK;
}

struct xaml_iterator
{
    IInspectable IInspectable_iface;
    LONG ref;
    struct xaml_collection *coll;
    unsigned int index;
    unsigned int end;    /* snapshot of coll->count at First(): appends during enumeration stop growing */
};

static HRESULT WINAPI iterator_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    TRACE( "iterator_QueryInterface %p %s\n", iface, debugstr_guid(iid) );
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    IInspectable_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI iterator_AddRef( IInspectable *iface )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iterator_AddRef iface %p ref %lu\n", iface, ref );
    return ref;
}
static ULONG WINAPI iterator_Release( IInspectable *iface )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    ULONG ref;
    TRACE( "iterator_Release iface %p\n", iface );
    ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        /* leak-keep: app caches raw ptrs in CoreRT RCWs; GC compaction
         * strands freed objects -> stale-vtbl AV in WindowEx ctor. */
        InterlockedIncrement( &impl->ref );
        return 1;
    }
    return ref;
}
static HRESULT WINAPI iterator_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    TRACE( "iterator_GetIids iface %p\n", iface );
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI iterator_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Xaml.Controls.RowDefinition>";
    TRACE( "iterator_GetRuntimeClassName iface %p\n", iface );
    return WindowsCreateString( nm, ARRAYSIZE(nm) - 1, name );
}
static HRESULT WINAPI iterator_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    TRACE( "iterator_GetTrustLevel iface %p\n", iface );
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_get_Current( void *iface, void **current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    TRACE( "iterator_get_Current iface %p current %p\n", iface, current );
    if (!current) return E_POINTER;
    if (impl->coll && impl->index < impl->end)
    {
        *current = impl->coll->items[impl->index];
        if (*current) IInspectable_AddRef( (IInspectable *)*current );
        return S_OK;
    }
    *current = NULL;
    return 0x8000000b; /* E_BOUNDS */
}

static HRESULT WINAPI iterator_get_HasCurrent( void *iface, boolean *has_current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    if (has_current) *has_current = (impl->coll && impl->index < impl->end);
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext( void *iface, boolean *has_current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    TRACE( "iterator_MoveNext iface %p has %p\n", iface, has_current );
    if (impl->coll && impl->index < impl->end) impl->index++;
    if (has_current) *has_current = (impl->coll && impl->index < impl->end);
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany( void *iface, unsigned int count, void **items, unsigned int *actual )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    unsigned int n = 0;
    TRACE( "iterator_GetMany iface %p count %u\n", iface, count );
    if (impl->coll && impl->index < impl->coll->count)
    {
        n = min( count, impl->coll->count - impl->index );
        memcpy( items, &impl->coll->items[impl->index], n * sizeof(*impl->coll->items) );
        impl->index += n;
    }
    if (actual) *actual = n;
    return S_OK;
}


/* Per-instance vtbl: .NET ComWrappers writes the RCW managed pointer into
 * slot 6 (SetManagedReference), so a shared static vtbl array gets clobbered
 * for every object using it. Give each object its own copy. */
void **vtbl_dup_n( const void *src, size_t count )
{
    void **d = calloc( 256, sizeof(*d) );
    if (d)
    {
        size_t i;
        memcpy( d, src, count * sizeof(*d) );
        for (i = count; i < 256; i++) d[i] = element_generic_slot;
    }
    return d;
}

void **vtbl_dup( const void *src )
{
    return vtbl_dup_n( src, 256 );
}

static const void *iterator_vtbl[256] =
{
    [0] = iterator_QueryInterface,         /* 0: QI */
    [1] = iterator_AddRef,                 /* 1: AddRef */
    [2] = iterator_Release,                /* 2: Release */
    [3] = iterator_GetIids,                /* 3: GetIids */
    [4] = iterator_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = iterator_GetTrustLevel,          /* 5: GetTrustLevel */
    [6] = iterator_MoveNext,               /* 6: MoveNext (IIterator untracked layout) */
    [7] = iterator_get_Current,            /* 7: get_Current */
    [8] = iterator_get_HasCurrent,         /* 8: get_HasCurrent */
    [9] = iterator_GetMany,                /* 9: GetMany */
    [10 ... 255] = frame_generic_slot,
};

/* IIterable<RowDefinition> view: slot 6 = First() -> fresh IIterator enumerator */
static HRESULT WINAPI iterable_First( void *iface, void **current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    struct xaml_iterator *it;
    TRACE( "iterable_First iface %p out %p\n", iface, current );
    if (!current) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( iterator_vtbl );
    it->ref = 1;
    it->coll = impl->coll;
    it->index = 0;
    it->end = impl->end;
    *current = &it->IInspectable_iface;
    return S_OK;
}

static const void *iterable_vtbl[256] =
{
    [0] = iterator_QueryInterface,
    [1] = iterator_AddRef,
    [2] = iterator_Release,
    [3] = iterator_GetIids,
    [4] = iterator_GetRuntimeClassName,
    [5] = iterator_GetTrustLevel,
    [6] = iterable_First,                  /* IIterable: First */
    [7 ... 255] = frame_generic_slot,
};

static HRESULT collection_iterable_view( struct xaml_collection *coll, void **out )
{
    struct xaml_iterator *v;
    if (coll->iterable_view)
    {
        *out = coll->iterable_view;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (!(v = calloc( 1, sizeof(*v) ))) return E_OUTOFMEMORY;
    v->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( iterable_vtbl );
    v->ref = 1;
    v->coll = coll;
    v->end = coll->count;
    coll->iterable_view = &v->IInspectable_iface;  /* leak-kept for collection lifetime */
    *out = &v->IInspectable_iface;
    return S_OK;
}

static HRESULT WINAPI collection_GetAt( void *iface, ULONG_PTR index, void **item );
static HRESULT WINAPI collection_get_Size( void *iface, unsigned int *size );
static HRESULT WINAPI collection_GetView( void *iface, void **view );
static HRESULT WINAPI collection_IndexOf( void *iface, void *item, unsigned int *index, boolean *found );
static HRESULT WINAPI collection_SetAt( void *iface, unsigned int index, void *item );
static HRESULT WINAPI collection_InsertAt( void *iface, unsigned int index, void *item );
static HRESULT WINAPI collection_RemoveAt( void *iface, unsigned int index );
HRESULT WINAPI collection_Append( void *iface, void *item );
static HRESULT WINAPI collection_Clear( void *iface );
static HRESULT WINAPI collection_GetMany( void *iface, unsigned int start, unsigned int count, void **items, unsigned int *actual );

static HRESULT WINAPI collection_GetAt( void *iface, ULONG_PTR index, void **item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    void **cv = (void **)iface;
    void **cv0 = *(void ***)cv;
    TRACE( "collection_GetAt iface %p, index %lu (0x%lx), item %p, count %u\n", iface, (unsigned long)index, (unsigned long)index, item, impl->count );
    TRACE( "COLDUMP self %p lpVtbl %p v0 %p v1 %p v6 %p v7 %p\n", cv, cv0, cv0[0], cv0[1], cv0[6], cv0[7] );
    if (!item || index > 0x10000)
    {
        void **first = (void **)index;
        struct xaml_iterator *it;
        if (!first) first = item;
        if (!first) return E_POINTER;
        if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
        it->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( iterator_vtbl );
        it->ref = 1;
        it->coll = impl;
        it->index = 0;
        it->end = impl->count;
        IInspectable_AddRef( &impl->IInspectable_iface );
        *first = &it->IInspectable_iface;
        TRACE( "IIterable::First created iterator %p for coll %p at %p\n", *first, impl, first );
        {
            void **v = (void **)*first;
            void **v0 = *(void ***)v;
            TRACE( "ITERDUMP self %p lpVtbl %p v0 %p v1 %p v6 %p v7 %p v8 %p v9 %p\n",
                   v, v0, v0[0], v0[1], v0[6], v0[7], v0[8], v0[9] );
        }
        return S_OK;
    }
    if (index >= impl->count)
    {
        *item = NULL;
        return 0x8000000b; /* E_BOUNDS */
    }
    *item = impl->items[index];
    if (*item) IInspectable_AddRef( (IInspectable *)*item );
    return S_OK;
}

static HRESULT WINAPI collection_get_Size( void *iface, unsigned int *size )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    TRACE( "collection_get_Size iface %p size %p count %u\n", iface, size, impl->count );
    if (size) *size = impl->count;
    return S_OK;
}

static HRESULT WINAPI collection_GetView( void *iface, void **view )
{
    if (view)
    {
        *view = iface;
        IInspectable_AddRef( (IInspectable *)iface );
    }
    return S_OK;
}

static HRESULT WINAPI collection_IndexOf( void *iface, void *item, unsigned int *index, boolean *found )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int i;
    for (i = 0; i < impl->count; i++)
    {
        if (impl->items[i] == item)
        {
            if (index) *index = i;
            if (found) *found = TRUE;
            return S_OK;
        }
    }
    if (index) *index = 0;
    if (found) *found = FALSE;
    return S_OK;
}

static HRESULT WINAPI collection_SetAt( void *iface, unsigned int index, void *item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    if (index >= impl->count) return 0x8000000b; /* E_BOUNDS */
    if (impl->items[index]) IInspectable_Release( impl->items[index] );
    impl->items[index] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    return S_OK;
}

static HRESULT WINAPI collection_InsertAt( void *iface, unsigned int index, void *item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    if (index > impl->count) return 0x8000000b; /* E_BOUNDS */
    if (impl->count >= impl->capacity)
    {
        unsigned int new_cap = impl->capacity ? impl->capacity * 2 : 8;
        void *new_items = realloc( impl->items, new_cap * sizeof(*impl->items) );
        if (!new_items) return E_OUTOFMEMORY;
        impl->items = new_items;
        impl->capacity = new_cap;
    }
    memmove( &impl->items[index + 1], &impl->items[index], (impl->count - index) * sizeof(*impl->items) );
    impl->items[index] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI collection_RemoveAt( void *iface, unsigned int index )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    if (index >= impl->count) return 0x8000000b; /* E_BOUNDS */
    if (impl->items[index]) IInspectable_Release( impl->items[index] );
    memmove( &impl->items[index], &impl->items[index + 1], (impl->count - index - 1) * sizeof(*impl->items) );
    impl->count--;
    return S_OK;
}

HRESULT WINAPI collection_Append( void *iface, void *item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    TRACE( "collection_Append iface %p, item %p, count was %u\n", iface, item, impl->count );
    if (impl->count >= impl->capacity)
    {
        unsigned int new_cap = impl->capacity ? impl->capacity * 2 : 8;
        void *new_items = realloc( impl->items, new_cap * sizeof(*impl->items) );
        if (!new_items) return E_OUTOFMEMORY;
        impl->items = new_items;
        impl->capacity = new_cap;
    }
    impl->items[impl->count] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI collection_Clear( void *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int i;
    for (i = 0; i < impl->count; i++)
        if (impl->items[i]) IInspectable_Release( impl->items[i] );
    impl->count = 0;
    return S_OK;
}

static HRESULT WINAPI collection_GetMany( void *iface, unsigned int start, unsigned int count, void **items, unsigned int *actual )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int n = 0;
    if (start < impl->count)
    {
        n = min(count, impl->count - start);
        memcpy(items, &impl->items[start], n * sizeof(*impl->items));
    }
    if (actual) *actual = n;
    return S_OK;
}

/* IVectorView<T>::GetMany is (count, items, actual) — 3 args, no start index.
 * IObservableVector<T>::GetMany is (start, count, items, actual). Feeding the
 * 4-arg form through the 3-arg slot shifted every returned item pointer by
 * one qword, handing the app a stack-junk ComObject (AV in set_MinHeight). */
static HRESULT WINAPI collection_GetMany_view( void *iface, unsigned int count, void **items, unsigned int *actual )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int n = min(count, impl->count);
    if (items && n) memcpy( items, impl->items, n * sizeof(*impl->items) );
    TRACE( "collection_GetMany_view iface %p count %u -> %u\n", iface, count, n );
    if (actual) *actual = n;
    return S_OK;
}

/* WinUI3 RowDefinitionCollection IV3 {94ea2b94} = IVectorView<RowDefinition>:
 * GetAt6, Size7, GetView8, IndexOf9, GetMany10 (IObservableVector has SetAt10). */
static const void *collection_view_vtbl[256] =
{
    [0] = collection_QueryInterface,
    [1] = collection_AddRef,
    [2] = collection_Release,
    [3] = collection_GetIids,
    [4] = collection_GetRuntimeClassName,
    [5] = collection_GetTrustLevel,
    [6] = collection_GetAt,
    [7] = collection_get_Size,
    [8] = collection_GetView,
    [9] = collection_IndexOf,
    [10] = collection_GetMany_view,
    [11 ... 12] = frame_generic_slot,
    [13] = collection_Append,
    [14] = frame_generic_slot,
    [15] = collection_Clear,
    [16] = collection_GetMany,
    [17 ... 255] = frame_generic_slot,
};

static const void *collection_vtbl[256] =
{
    [0] = collection_QueryInterface,         /* 0: QI */
    [1] = collection_AddRef,                 /* 1: AddRef */
    [2] = collection_Release,                /* 2: Release */
    [3] = collection_GetIids,                /* 3: GetIids */
    [4] = collection_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = collection_GetTrustLevel,          /* 5: GetTrustLevel */
    [6] = collection_GetAt,                  /* 6: First */
    [7] = collection_get_Size,               /* 7: get_Size */
    [8] = collection_GetView,                /* 8: GetView */
    [9] = collection_IndexOf,                /* 9: IndexOf */
    [10] = collection_SetAt,                 /* 10: SetAt */
    [11] = collection_InsertAt,              /* 11: InsertAt */
    [12] = collection_RemoveAt,              /* 12: RemoveAt */
    [13] = collection_Append,                /* 13: Append */
    [14] = frame_generic_slot,               /* 14: RemoveAtEnd */
    [15] = collection_Clear,                 /* 15: Clear */
    [16] = collection_GetMany,               /* 16: GetMany */
    [17 ... 255] = frame_generic_slot,
};

HRESULT collection_create( void **instance )
{
    struct xaml_collection *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( collection_vtbl );
    impl->ref = 1;
    *instance = &impl->IInspectable_iface;
    return S_OK;
}
