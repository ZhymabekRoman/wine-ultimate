/* Minimal IMap<HSTRING, IInspectable> for CoreApplication's IStaticLifetime.
 *
 * Copyright 2026 for CodeWeavers
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

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

struct map_entry
{
    HSTRING key;
    IInspectable *value;
};

struct static_map
{
    __FIMap_2_HSTRING_IInspectable IMap_iface;
    LONG ref;
    struct map_entry *entries;
    SIZE_T count, capacity;
    CRITICAL_SECTION cs;
};

static inline struct static_map *impl_from_IMap( __FIMap_2_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct static_map, IMap_iface );
}

static int key_equal( HSTRING a, HSTRING b )
{
    INT32 r = 0;
    WindowsCompareStringOrdinal( a, b, &r );
    return r == 0;
}

static struct map_entry *find_entry( struct static_map *impl, HSTRING key )
{
    SIZE_T i;
    for (i = 0; i < impl->count; i++)
        if (key_equal( impl->entries[i].key, key )) return &impl->entries[i];
    return NULL;
}

static HRESULT WINAPI map_QueryInterface( __FIMap_2_HSTRING_IInspectable *iface, REFIID iid, void **out )
{
    struct static_map *impl = impl_from_IMap( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIMap_2_HSTRING_IInspectable ))
    {
        __FIMap_2_HSTRING_IInspectable_AddRef( &impl->IMap_iface );
        *out = &impl->IMap_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI map_AddRef( __FIMap_2_HSTRING_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_IMap( iface )->ref );
}

static ULONG WINAPI map_Release( __FIMap_2_HSTRING_IInspectable *iface )
{
    struct static_map *impl = impl_from_IMap( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        SIZE_T i;
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->entries[i].key );
            if (impl->entries[i].value) IInspectable_Release( impl->entries[i].value );
        }
        free( impl->entries );
        impl->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI map_GetIids( __FIMap_2_HSTRING_IInspectable *iface, ULONG *count, IID **iids )
{ *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI map_GetRuntimeClassName( __FIMap_2_HSTRING_IInspectable *iface, HSTRING *name )
{ return E_NOTIMPL; }
static HRESULT WINAPI map_GetTrustLevel( __FIMap_2_HSTRING_IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI map_Lookup( __FIMap_2_HSTRING_IInspectable *iface, HSTRING key, IInspectable **value )
{
    struct static_map *impl = impl_from_IMap( iface );
    struct map_entry *e;
    HRESULT hr = E_BOUNDS;

    TRACE( "iface %p, key %s, value %p.\n", iface, debugstr_hstring( key ), value );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    if ((e = find_entry( impl, key )))
    {
        if (e->value) IInspectable_AddRef( e->value );
        *value = e->value;
        hr = S_OK;
    }
    else *value = NULL;
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI map_get_Size( __FIMap_2_HSTRING_IInspectable *iface, UINT32 *size )
{
    struct static_map *impl = impl_from_IMap( iface );
    if (size) *size = impl->count;
    return S_OK;
}

static HRESULT WINAPI map_HasKey( __FIMap_2_HSTRING_IInspectable *iface, HSTRING key, boolean *found )
{
    struct static_map *impl = impl_from_IMap( iface );
    TRACE( "iface %p, key %s.\n", iface, debugstr_hstring( key ) );
    if (!found) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *found = find_entry( impl, key ) != NULL;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI map_GetView( __FIMap_2_HSTRING_IInspectable *iface,
        __FIMapView_2_HSTRING_IInspectable **view )
{
    FIXME( "iface %p, view %p stub!\n", iface, view );
    if (view) *view = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI map_Insert( __FIMap_2_HSTRING_IInspectable *iface, HSTRING key,
        IInspectable *value, boolean *replaced )
{
    struct static_map *impl = impl_from_IMap( iface );
    struct map_entry *e;
    HRESULT hr = S_OK;

    TRACE( "iface %p, key %s, value %p.\n", iface, debugstr_hstring( key ), value );
    EnterCriticalSection( &impl->cs );
    if ((e = find_entry( impl, key )))
    {
        if (e->value) IInspectable_Release( e->value );
        if ((e->value = value)) IInspectable_AddRef( value );
        if (replaced) *replaced = TRUE;
    }
    else
    {
        if (impl->count >= impl->capacity)
        {
            SIZE_T cap = impl->capacity ? impl->capacity * 2 : 8;
            struct map_entry *tmp = realloc( impl->entries, cap * sizeof(*tmp) );
            if (!tmp) { hr = E_OUTOFMEMORY; goto done; }
            impl->entries = tmp;
            impl->capacity = cap;
        }
        if (FAILED(hr = WindowsDuplicateString( key, &impl->entries[impl->count].key ))) goto done;
        if ((impl->entries[impl->count].value = value)) IInspectable_AddRef( value );
        impl->count++;
        if (replaced) *replaced = FALSE;
    }
done:
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI map_Remove( __FIMap_2_HSTRING_IInspectable *iface, HSTRING key )
{
    struct static_map *impl = impl_from_IMap( iface );
    struct map_entry *e;
    HRESULT hr = E_BOUNDS;

    EnterCriticalSection( &impl->cs );
    if ((e = find_entry( impl, key )))
    {
        SIZE_T idx = e - impl->entries;
        WindowsDeleteString( e->key );
        if (e->value) IInspectable_Release( e->value );
        memmove( &impl->entries[idx], &impl->entries[idx + 1],
                 (impl->count - idx - 1) * sizeof(*e) );
        impl->count--;
        hr = S_OK;
    }
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI map_Clear( __FIMap_2_HSTRING_IInspectable *iface )
{
    struct static_map *impl = impl_from_IMap( iface );
    SIZE_T i;
    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->count; i++)
    {
        WindowsDeleteString( impl->entries[i].key );
        if (impl->entries[i].value) IInspectable_Release( impl->entries[i].value );
    }
    impl->count = 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct __FIMap_2_HSTRING_IInspectableVtbl static_map_vtbl =
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

HRESULT static_lifetime_map_create( __FIMap_2_HSTRING_IInspectable **out )
{
    struct static_map *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IMap_iface.lpVtbl = &static_map_vtbl;
    impl->ref = 1;
    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)( __FILE__ ": static_map.cs" );
    *out = &impl->IMap_iface;
    return S_OK;
}
