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

/* Parameterized interface ids used by the C++/WinRT and .NET/CsWinRT
 * projections of IPropertySet; both land on the single merged object. */
static const GUID IID_IMap_String_IInspectable =
{ 0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6} };
static const GUID IID_IMapView_String_IInspectable =
{ 0xe8cc1dcf, 0xaa1b, 0x51e7, {0x90, 0xa7, 0x6b, 0x7d, 0x99, 0xbd, 0x99, 0xa0} };
static const GUID IID_IMapView_StringObject_IInspectable =
{ 0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0} };

/* CoreCLR's ComWrappers shader reads the IInspectable::GetIids output of a
 * WinRT object as an array of ComInterfaceEntry pairs, not a bare IID array:
 *
 *   struct ComInterfaceEntry { void **Vtable; GUID *IID; };
 *
 * The shader (InvokeableVtable generation) walks count entries, and for each
 * pair stores entry->Vtable as the source vtable it thunks into slot N, using
 * entry->IID to key the interface. With a bare IID array the shader read entry
 * i's "Vtable" half as the (i+1)-th IID's raw bytes, so a generated thunk's
 * call target became IID data (observed rax=0x47264f325c13e51c =
 * IID_IMapView<String,Object> executed as a call target after
 * mov 0x38(%rax),%rax). Hand back a real pair array: a stable IID pointer
 * (the static IID globals) plus the real per-interface vtable, allocated
 * per-call with CoTaskMemAlloc (CoreCLR frees it with CoTaskMemFree). */
struct com_interface_entry;

/* Forward accessors: the merged-face vtables are defined below GetIids; the
 * shader hands these out as the entry Vtable pointers. */
static void **propertyset_vtbl_ptr( void );
static void **propertyset_imapview_vtbl_ptr( void );
static void **propertyset_iterable_vtbl_ptr( void );
static void **propertyset_iobservablemap_vtbl_ptr( void );

/* Windows hands out a single object for the whole PropertySet interface set:
 * QI for IPropertySet, IMap, IMapView, IIterable and IObservableMap all return
 * the same pointer at offset 0, whose vtable is the prefix-deduplicated union
 * of the interface layouts (windows.foundation.collections.idl):
 *
 *   0-2   IUnknown QI / AddRef / Release
 *   3-5   IInspectable GetIids / GetRuntimeClassName / GetTrustLevel
 *   6     Lookup (IIterable consumers call this slot as First)
 *   7     get_Size
 *   8     HasKey
 *   9     GetView (IMapView consumers call this slot as Split)
 *   10-12 Insert / Remove / Clear
 *   13    IObservableMap add_MapChanged
 *   14    IObservableMap remove_MapChanged
 *
 * Keeping one face lets the CoreCLR ComWrappers shader walk each QI'd
 * interface's vtable as a clean, contiguous, NULL-terminated entry array
 * instead of strolling into the vtable padding of separate faces. */
struct propertyset
{
    IPropertySet IPropertySet_iface;
    IMapView_HSTRING_IInspectable IMapView_HSTRING_IInspectable_iface;
    IIterable_IKeyValuePair_HSTRING_IInspectable IIterable_IKeyValuePair_HSTRING_IInspectable_iface;
    IObservableMap_HSTRING_IInspectable IObservableMap_HSTRING_IInspectable_iface;
    LONG ref;

    IInspectable *map_inner;
    IMap_HSTRING_IInspectable *inner_map;
    IIterable_IKeyValuePair_HSTRING_IInspectable *inner_iterable;
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
        IsEqualGUID( iid, &IID_IPropertySet ) ||
        IsEqualGUID( iid, &IID_IMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMap_String_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_String_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_StringObject_IInspectable ) ||
        IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IObservableMap_HSTRING_IInspectable ))
    {
        *out = iface;
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
        if (impl->inner_map) IMap_HSTRING_IInspectable_Release( impl->inner_map );
        if (impl->inner_iterable) IIterable_IKeyValuePair_HSTRING_IInspectable_Release( impl->inner_iterable );
        IInspectable_Release( impl->map_inner );
        free( impl );
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE propertyset_GetIids( IPropertySet *iface, ULONG *iid_count, IID **iids )
{
    /* Windows reports the PropertySet runtime class interface list (winmd):
     * IPropertySet (default, = IMap<String, IInspectable>) + IObservableMap.
     * The C++/WinRT and CsWinRT projections find IMap/IMapView via the QI
     * arms on the merged face, so the list stays narrow; an over-wide list
     * makes the CoreCLR ComWrappers shader probe IIterable/IIterator
     * adapters whose CreateObject walks past the entries it can satisfy. */
    struct com_interface_entry entries[] =
    {
        { (IID *)&IID_IMap_String_IInspectable, propertyset_vtbl_ptr() },
        { (IID *)&IID_IMapView_StringObject_IInspectable, propertyset_imapview_vtbl_ptr() },
        { (IID *)&IID_IIterable_IKeyValuePair_HSTRING_IInspectable, propertyset_iterable_vtbl_ptr() },
        { (IID *)&IID_IObservableMap_HSTRING_IInspectable, propertyset_iobservablemap_vtbl_ptr() },
    };
    TRACE( "(%p, %p, %p)\n", iface, iid_count, iids );

    if (iid_count) *iid_count = ARRAY_SIZE(entries);
    if (iids)
    {
        struct com_interface_entry *array = CoTaskMemAlloc( sizeof(entries) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof(entries) );
        *iids = (IID *)array;
        TRACE( "returning %u ComInterfaceEntry pairs: v0=%p iid0=%p\n",
               ARRAY_SIZE(entries), (void *)array[0].vtable, (void *)array[0].iid );
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE propertyset_GetRuntimeClassName( IPropertySet *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";

    TRACE( "(%p, %p)\n", iface, class_name );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT STDMETHODCALLTYPE propertyset_GetTrustLevel( IPropertySet *iface, TrustLevel *trust_level )
{
    TRACE( "(%p, %p)\n", iface, trust_level );

    if (trust_level) *trust_level = FullTrust;
    return S_OK;
}

/* Slot 6: IMap::Lookup / IIterable::First (First passes the iterator pointer
 * in the key slot and nothing in the value slot). */
static HRESULT STDMETHODCALLTYPE propertyset_Lookup( IPropertySet *iface, void *key, void *value )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );

    if (!value)
        return IIterable_IKeyValuePair_HSTRING_IInspectable_First( impl->inner_iterable,
                                                                   (IIterator_IKeyValuePair_HSTRING_IInspectable **)key );
    return IMap_HSTRING_IInspectable_Lookup( impl->inner_map, (HSTRING)key, (IInspectable **)value );
}

static HRESULT STDMETHODCALLTYPE propertyset_get_Size( IPropertySet *iface, UINT32 *size )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    return IMap_HSTRING_IInspectable_get_Size( impl->inner_map, size );
}

static HRESULT STDMETHODCALLTYPE propertyset_HasKey( IPropertySet *iface, HSTRING key, boolean *found )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    return IMap_HSTRING_IInspectable_HasKey( impl->inner_map, key, found );
}

/* Slot 9: IMap::GetView / IMapView::Split (Split passes both view outs). */
static HRESULT STDMETHODCALLTYPE propertyset_GetView( IPropertySet *iface, void *first, void *second )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    IMapView_HSTRING_IInspectable *view;
    HRESULT hr;

    if (FAILED(hr = IMap_HSTRING_IInspectable_GetView( impl->inner_map, &view ))) return hr;
    if (second)
    {
        hr = IMapView_HSTRING_IInspectable_Split( view, (IMapView_HSTRING_IInspectable **)first,
                                                  (IMapView_HSTRING_IInspectable **)second );
        IMapView_HSTRING_IInspectable_Release( view );
    }
    else *(IMapView_HSTRING_IInspectable **)first = view;
    return hr;
}

static HRESULT STDMETHODCALLTYPE propertyset_Insert( IPropertySet *iface, HSTRING key, IInspectable *value, boolean *replaced )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    return IMap_HSTRING_IInspectable_Insert( impl->inner_map, key, value, replaced );
}

static HRESULT STDMETHODCALLTYPE propertyset_Remove( IPropertySet *iface, HSTRING key )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    return IMap_HSTRING_IInspectable_Remove( impl->inner_map, key );
}

static HRESULT STDMETHODCALLTYPE propertyset_Clear( IPropertySet *iface )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    return IMap_HSTRING_IInspectable_Clear( impl->inner_map );
}

static HRESULT STDMETHODCALLTYPE propertyset_add_MapChanged( IPropertySet *iface,
                                                            IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                            EventRegistrationToken *token )
{
    FIXME( "(%p, %p, %p) stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE propertyset_remove_MapChanged( IPropertySet *iface, EventRegistrationToken token )
{
    FIXME( "(%p, %I64d) stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static const struct
{
    IPropertySetVtbl vtbl;
    HRESULT (STDMETHODCALLTYPE *add_MapChanged)( IObservableMap_HSTRING_IInspectable *iface,
                                                 IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                 EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_MapChanged)( IObservableMap_HSTRING_IInspectable *iface,
                                                    EventRegistrationToken token );
} propertyset_vtbl_full =
{
    { /* IUnknown */
    propertyset_QueryInterface,
    propertyset_AddRef,
    propertyset_Release,
    /* IInspectable */
    propertyset_GetIids,
    propertyset_GetRuntimeClassName,
    propertyset_GetTrustLevel,
    /* IMap<HSTRING, IInspectable *> / IMapView / IIterable merged slots */
    (HRESULT (STDMETHODCALLTYPE *)( IPropertySet *, HSTRING, IInspectable ** ))propertyset_Lookup,
    propertyset_get_Size,
    propertyset_HasKey,
    (HRESULT (STDMETHODCALLTYPE *)( IPropertySet *, IMapView_HSTRING_IInspectable ** ))propertyset_GetView,
    propertyset_Insert,
    propertyset_Remove,
    propertyset_Clear, },
    (HRESULT (STDMETHODCALLTYPE *)( IObservableMap_HSTRING_IInspectable *, IMapChangedEventHandler_HSTRING_IInspectable *, EventRegistrationToken * ))propertyset_add_MapChanged,
    (HRESULT (STDMETHODCALLTYPE *)( IObservableMap_HSTRING_IInspectable *, EventRegistrationToken ))propertyset_remove_MapChanged,
};
#define propertyset_vtbl propertyset_vtbl_full.vtbl


DEFINE_IINSPECTABLE( propertyset_IObservableMap, IObservableMap_HSTRING_IInspectable, struct propertyset, IPropertySet_iface );

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_add_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                                            IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                                            EventRegistrationToken *token )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_add_MapChanged( &impl->IPropertySet_iface, handler, token );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_remove_MapChanged( IObservableMap_HSTRING_IInspectable *iface, EventRegistrationToken token )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_remove_MapChanged( &impl->IPropertySet_iface, token );
}

const static struct { IObservableMap_HSTRING_IInspectableVtbl vtbl; void *pad[4]; } propertyset_IObservableMap_vtbl_full =
{
    { /* IUnknown */
    propertyset_IObservableMap_QueryInterface,
    propertyset_IObservableMap_AddRef,
    propertyset_IObservableMap_Release,
    /* IInspectable */
    propertyset_IObservableMap_GetIids,
    propertyset_IObservableMap_GetRuntimeClassName,
    propertyset_IObservableMap_GetTrustLevel,
    /* IObservableMap<HSTRING, IInspectable*> */
    propertyset_IObservableMap_add_MapChanged,
    propertyset_IObservableMap_remove_MapChanged, },
    { 0, 0, 0, 0 }
};
#define propertyset_IObservableMap_vtbl propertyset_IObservableMap_vtbl_full.vtbl


/* The per-interface faces below remain addressable for consumers that cached
 * them before the merge; every method forwards to the merged object. */
DEFINE_IINSPECTABLE( propertyset_IMapView, IMapView_HSTRING_IInspectable, struct propertyset, IPropertySet_iface );

static HRESULT STDMETHODCALLTYPE propertyset_IMapView_Lookup( IMapView_HSTRING_IInspectable *iface, HSTRING key, IInspectable **value )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IMapView_HSTRING_IInspectable_iface );
    return IMap_HSTRING_IInspectable_Lookup( (IMap_HSTRING_IInspectable *)&impl->IPropertySet_iface, key, value );
}

static HRESULT STDMETHODCALLTYPE propertyset_IMapView_get_Size( IMapView_HSTRING_IInspectable *iface, UINT32 *size )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IMapView_HSTRING_IInspectable_iface );
    return IMap_HSTRING_IInspectable_get_Size( (IMap_HSTRING_IInspectable *)&impl->IPropertySet_iface, size );
}

static HRESULT STDMETHODCALLTYPE propertyset_IMapView_HasKey( IMapView_HSTRING_IInspectable *iface, HSTRING key, boolean *found )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IMapView_HSTRING_IInspectable_iface );
    return IMap_HSTRING_IInspectable_HasKey( (IMap_HSTRING_IInspectable *)&impl->IPropertySet_iface, key, found );
}

static HRESULT STDMETHODCALLTYPE propertyset_IMapView_Split( IMapView_HSTRING_IInspectable *iface,
                                                            IMapView_HSTRING_IInspectable **first,
                                                            IMapView_HSTRING_IInspectable **second )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IMapView_HSTRING_IInspectable_iface );
    return propertyset_GetView( &impl->IPropertySet_iface, first, second );
}

static const struct { IMapView_HSTRING_IInspectableVtbl vtbl; void *pad[4]; } propertyset_IMapView_vtbl_full =
{
    { propertyset_IMapView_QueryInterface,
    propertyset_IMapView_AddRef,
    propertyset_IMapView_Release,
    /* IInspectable */
    propertyset_IMapView_GetIids,
    propertyset_IMapView_GetRuntimeClassName,
    propertyset_IMapView_GetTrustLevel,
    /* IMapView<HSTRING, IInspectable*> */
    propertyset_IMapView_Lookup,
    propertyset_IMapView_get_Size,
    propertyset_IMapView_HasKey,
    propertyset_IMapView_Split, },
    { 0, 0, 0, 0 }
};
#define propertyset_IMapView_vtbl propertyset_IMapView_vtbl_full.vtbl


DEFINE_IINSPECTABLE( propertyset_IIterable, IIterable_IKeyValuePair_HSTRING_IInspectable, struct propertyset, IPropertySet_iface );

static HRESULT STDMETHODCALLTYPE propertyset_IIterable_First( IIterable_IKeyValuePair_HSTRING_IInspectable *iface,
                                                             IIterator_IKeyValuePair_HSTRING_IInspectable **value )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IIterable_IKeyValuePair_HSTRING_IInspectable_iface );
    return IIterable_IKeyValuePair_HSTRING_IInspectable_First( impl->inner_iterable, value );
}

static const struct { IIterable_IKeyValuePair_HSTRING_IInspectableVtbl vtbl; void *pad[4]; } propertyset_IIterable_vtbl_full =
{
    { propertyset_IIterable_QueryInterface,
    propertyset_IIterable_AddRef,
    propertyset_IIterable_Release,
    /* IInspectable */
    propertyset_IIterable_GetIids,
    propertyset_IIterable_GetRuntimeClassName,
    propertyset_IIterable_GetTrustLevel,
    /* IIterable<IKeyValuePair<HSTRING, IInspectable*>> */
    propertyset_IIterable_First, },
    { 0, 0, 0, 0 }
};
#define propertyset_IIterable_vtbl propertyset_IIterable_vtbl_full.vtbl


/* vtable accessors used by propertyset_GetIids' ComInterfaceEntry table. */
static void **propertyset_vtbl_ptr( void ) { return (void **)&propertyset_vtbl; }
static void **propertyset_imapview_vtbl_ptr( void ) { return (void **)&propertyset_IMapView_vtbl; }
static void **propertyset_iterable_vtbl_ptr( void ) { return (void **)&propertyset_IIterable_vtbl; }
static void **propertyset_iobservablemap_vtbl_ptr( void ) { return (void **)&propertyset_IObservableMap_vtbl; }


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
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IPropertySet,
        IID_IObservableMap_HSTRING_IInspectable,
    };
    TRACE( "(%p, %p, %p)\n", iface, iid_count, iids );

    if (iid_count) *iid_count = ARRAY_SIZE(factory_iids);
    if (iids)
    {
        IID *array = CoTaskMemAlloc( sizeof(factory_iids) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, factory_iids, sizeof(factory_iids) );
        *iids = array;
    }
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

/* Identity-stable allocation: CoreCLR's ComWrappers RCW cache is keyed by the
 * raw ABI pointer. Heap-allocated instances that the app releases let the RCW
 * entry alias a wine-heap block that gets reused (e.g. by a managed UTF-16
 * string); the vtable shader then executes mov 0x38(%rax),%rax against the
 * string header and faults (VTableIndirection chunk-handle mis-decode). Serve
 * the class from a static slab ring: RING_SIZE slots are allocated once, are
 * never freed (self-referencing ref count), keep their inner map/iterator
 * cache faces for the lifetime of the slot, and cycle round-robin so any
 * RCW cache depth sees a live, generation-stable identity. Windows itself
 * hands out Values as a live per-container view; a ring of 8 satisfies the
 * "fresh per call" shape while pinning the identities. */
#define PROPERTYSET_RING_SIZE 8

static struct propertyset propertyset_ring[PROPERTYSET_RING_SIZE];
static INIT_ONCE propertyset_ring_once = INIT_ONCE_STATIC_INIT;
static LONG propertyset_ring_next;

static const struct map_iids propertyset_map_iids =
{
    .map = &IID_IMap_HSTRING_IInspectable,
    .view = &IID_IMapView_HSTRING_IInspectable,
    .iterable = &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
    .iterator = &IID_IIterator_IKeyValuePair_HSTRING_IInspectable,
    .pair = &IID_IKeyValuePair_HSTRING_IInspectable,
};

static BOOL CALLBACK propertyset_ring_init( INIT_ONCE *once, void *param, void **context )
{
    ULONG i;

    for (i = 0; i < PROPERTYSET_RING_SIZE; i++)
    {
        struct propertyset *impl = &propertyset_ring[i];

        impl->IPropertySet_iface.lpVtbl = &propertyset_vtbl;
        impl->IMapView_HSTRING_IInspectable_iface.lpVtbl = &propertyset_IMapView_vtbl;
        impl->IIterable_IKeyValuePair_HSTRING_IInspectable_iface.lpVtbl = &propertyset_IIterable_vtbl;
        impl->IObservableMap_HSTRING_IInspectable_iface.lpVtbl = &propertyset_IObservableMap_vtbl;
        impl->ref = 1; /* self-reference: ring slots are never freed */

        if (FAILED(multi_threaded_map_create( &propertyset_map_iids,
                                              (IInspectable *)&impl->IPropertySet_iface,
                                              &impl->map_inner )))
            continue;
        IInspectable_QueryInterface( impl->map_inner, &IID_IMap_HSTRING_IInspectable,
                                     (void **)&impl->inner_map );
        IInspectable_QueryInterface( impl->map_inner, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                     (void **)&impl->inner_iterable );
    }
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct propertyset *impl;

    TRACE( "(%p, %p)\n", iface, instance );

    InitOnceExecuteOnce( &propertyset_ring_once, propertyset_ring_init, NULL, NULL );

    impl = &propertyset_ring[InterlockedIncrement( &propertyset_ring_next ) & (PROPERTYSET_RING_SIZE - 1)];
    if (!impl->map_inner)
        return E_OUTOFMEMORY;
    InterlockedIncrement( &impl->ref );

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
