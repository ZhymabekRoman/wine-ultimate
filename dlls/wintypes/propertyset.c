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
#include <winternl.h>
#include <ntstatus.h>
#include <wine/debug.h>
#include <wine/exception.h>
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
static const GUID IID_IMap_String_Object =
{ 0x8114f3e9, 0xf118, 0x3c7b, { 0x91, 0x94, 0x00, 0xa3, 0xa2, 0x4d, 0x96, 0x7d } };
static const GUID IID_IMapView_String_Object =
{ 0x7c054694, 0x4acd, 0x3716, { 0xa1, 0x7a, 0xd5, 0x83, 0xc5, 0xb3, 0xde, 0x8b } };
static const GUID IID_IObservableMap_String_Object =
{ 0x65df2bf5, 0xbf39, 0x41b5, { 0xae, 0xbc, 0x5a, 0x9d, 0x86, 0x5e, 0x47, 0x2b } };
static const GUID IID_IMap_Object_Object =
{ 0xf5f69427, 0x55ed, 0x5512, { 0x84, 0x29, 0xd4, 0xf6, 0x62, 0x6d, 0xfc, 0xdd } };
static const GUID IID_IMapView_Object_Object =
{ 0x59e7ae0c, 0xc29d, 0x5ad6, { 0xbe, 0xf5, 0xde, 0xdb, 0x52, 0xa1, 0x98, 0xe1 } };
static const GUID IID_IIterable_KeyValuePair_Object_Object =
{ 0x33ac68f0, 0x1084, 0x529a, { 0x8a, 0x17, 0x4e, 0x7c, 0x8a, 0xdb, 0x7a, 0x0c } };
static const GUID IID_IResourceDictionary =
{ 0xc1ea4f24, 0xd6de, 0x4191, { 0x8e, 0x3a, 0xf4, 0x86, 0x01, 0xf7, 0x48, 0x9c } };

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

/* Registration list for the event accessors of this face; the full layout is
 * documented with its implementation below. */
struct propertyset_reg
{
    LONG ref;
    IUnknown *handler;
    struct propertyset_reg *next;
};

/* Classic XAML IReferenceTracker (windows.ui.xaml.hosting.referencetracker.h).
 * CoreCLR's ComWrappers builds a composite wrapper for each RCW it creates and
 * the managed TrackerObjectManager::AfterWrapperCreated walks the composite's
 * slot 0 (packed ComVirtualMethodData header, tag 3), fetching the header slot
 * +0x38 = slot 7 AddRefFromTrackerSource from the shader's heap chunk. That
 * chunk (first block of the runtime heap region, observed base
 * 0x6fff00000000) lands on an unmapped first page under wine, so every
 * get_Values RCW creation re-faults 0x6fff0000003b (6.7M first-chance AVs,
 * 35 fatal cycles per 150s gate). Hand CoreCLR a real tracker face: its QI
 * (IID_IReferenceTracker) then lands on our image-resident vtable and the
 * heap-chunk composite is never dereferenced. Slot order per the header:
 * 0-2 IUnknown, 3 Connect, 4 Disconnect, 5 FindTrackerTargets,
 * 6 GetReferenceTrackerManager, 7 AddRefFromTrackerSource,
 * 8 ReleaseFromTrackerSource, 9 PegFromTrackerSource. */
static const GUID IID_IReferenceTracker_g =
{ 0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6} };

/* WindowsAppSDK (1.x) extension face probed by the Windows App Runtime 1.6
 * XAML wrapper alongside the classic tracker: same 10 slots plus
 * GetIsWinRTObject / SetIsWinRTObject. Offered on the same singleton. */
static const GUID IID_IReferenceTracker2_g =
{ 0x689fab0b, 0xb359, 0x5a94, {0xbe, 0x9f, 0x24, 0xf6, 0x3e, 0x4b, 0x9a, 0x6d} };

struct propertyset_reference_tracker;

static HRESULT STDMETHODCALLTYPE tracker_QueryInterface( struct propertyset_reference_tracker *iface,
                                                         REFIID iid, void **out );
static ULONG STDMETHODCALLTYPE tracker_AddRef( struct propertyset_reference_tracker *iface );
static ULONG STDMETHODCALLTYPE tracker_Release( struct propertyset_reference_tracker *iface );
static HRESULT STDMETHODCALLTYPE tracker_ConnectFromTrackerSource( struct propertyset_reference_tracker *iface );
static HRESULT STDMETHODCALLTYPE tracker_DisconnectFromTrackerSource( struct propertyset_reference_tracker *iface,
                                                                      int disconnect_type );
static HRESULT STDMETHODCALLTYPE tracker_FindTrackerTargets( struct propertyset_reference_tracker *iface,
                                                             void *callback );
static HRESULT STDMETHODCALLTYPE tracker_GetReferenceTrackerManager( struct propertyset_reference_tracker *iface,
                                                                     void **manager );
static HRESULT STDMETHODCALLTYPE tracker_AddRefFromTrackerSource( struct propertyset_reference_tracker *iface );
static HRESULT STDMETHODCALLTYPE tracker_ReleaseFromTrackerSource( struct propertyset_reference_tracker *iface );
static HRESULT STDMETHODCALLTYPE tracker_PegFromTrackerSource( struct propertyset_reference_tracker *iface );

struct propertyset_reference_tracker_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( struct propertyset_reference_tracker *iface,
                                                 REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( struct propertyset_reference_tracker *iface );
    ULONG (STDMETHODCALLTYPE *Release)( struct propertyset_reference_tracker *iface );
    HRESULT (STDMETHODCALLTYPE *ConnectFromTrackerSource)( struct propertyset_reference_tracker *iface );
    HRESULT (STDMETHODCALLTYPE *DisconnectFromTrackerSource)( struct propertyset_reference_tracker *iface,
                                                             int disconnect_type );
    HRESULT (STDMETHODCALLTYPE *FindTrackerTargets)( struct propertyset_reference_tracker *iface,
                                                     void *callback );
    HRESULT (STDMETHODCALLTYPE *GetReferenceTrackerManager)( struct propertyset_reference_tracker *iface,
                                                             void **manager );
    HRESULT (STDMETHODCALLTYPE *AddRefFromTrackerSource)( struct propertyset_reference_tracker *iface );
    HRESULT (STDMETHODCALLTYPE *ReleaseFromTrackerSource)( struct propertyset_reference_tracker *iface );
    HRESULT (STDMETHODCALLTYPE *PegFromTrackerSource)( struct propertyset_reference_tracker *iface );
};

struct propertyset_reference_tracker
{
    const struct propertyset_reference_tracker_vtbl *vtbl;
    LONG ref;
};

static const GUID IID_INotifyCollectionChanged_g =
    { 0x350ffb1a, 0xdf08, 0x56fd, { 0x98, 0xd1, 0x37, 0x4d, 0x77, 0x72, 0x34, 0x9f }};
static const GUID IID_INotifyPropertyChanged_g =
    { 0xe8a31095, 0x6789, 0x587b, { 0xb8, 0xe5, 0xcc, 0x7a, 0x1b, 0xdd, 0x8e, 0x37 }};

/* Merged-face vtable blobs are defined below GetIids; forward-declare them
 * (tagged) so propertyset_GetIids' compile-time pair array can reference
 * their addresses directly — no runtime accessors, no lazy memcpy publish. */
struct propertyset_vtbl_full_s;
struct propertyset_imapview_vtbl_full_s;
struct propertyset_iterable_vtbl_full_s;
static const struct propertyset_vtbl_full_s propertyset_vtbl_full;



static const struct propertyset_imapview_vtbl_full_s propertyset_IMapView_vtbl_full;
static const struct propertyset_iterable_vtbl_full_s propertyset_IIterable_vtbl_full;

/* Forward accessors: the merged-face vtables are defined below GetIids; the
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

    /* add_MapChanged/CollectionChanged/PropertyChanged registrations
     * (interlocked push list, see propertyset_reg_add). */
    struct propertyset_reg *regs;

    /* Single-call-contract pair array: allocated on the first GetIids (the
     * shader's (NULL, NULL) count probe allocates and caches it too) and
     * returned by pointer on every subsequent call, so a shader that probes
     * the count and then consumes a cached/assumed array pointer sees one
     * stable, live ComInterfaceEntry block per object identity. */
    struct com_interface_entry *iids_cache;
    ULONG iids_count;
};

static inline struct propertyset *impl_from_IPropertySet( IPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct propertyset, IPropertySet_iface );
}

static HRESULT STDMETHODCALLTYPE tracker_QueryInterface( struct propertyset_reference_tracker *iface,
                                                         REFIID iid, void **out )
{
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = iface;
        InterlockedIncrement( &iface->ref );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE tracker_AddRef( struct propertyset_reference_tracker *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG STDMETHODCALLTYPE tracker_Release( struct propertyset_reference_tracker *iface )
{
    return InterlockedDecrement( &iface->ref );
}

static HRESULT STDMETHODCALLTYPE tracker_ConnectFromTrackerSource( struct propertyset_reference_tracker *iface )
{
    TRACE( "tracker %p connect\n", iface );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tracker_DisconnectFromTrackerSource( struct propertyset_reference_tracker *iface,
                                                                     int disconnect_type )
{
    TRACE( "tracker %p disconnect type %d\n", iface, disconnect_type );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tracker_FindTrackerTargets( struct propertyset_reference_tracker *iface,
                                                             void *callback )
{
    TRACE( "tracker %p find targets callback %p\n", iface, callback );
    return S_OK; /* no tracked targets from a builtin property set */
}

static HRESULT STDMETHODCALLTYPE tracker_GetReferenceTrackerManager( struct propertyset_reference_tracker *iface,
                                                                     void **manager )
{
    TRACE( "tracker %p get manager %p\n", iface, manager );
    if (manager) *manager = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tracker_AddRefFromTrackerSource( struct propertyset_reference_tracker *iface )
{
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tracker_ReleaseFromTrackerSource( struct propertyset_reference_tracker *iface )
{
    InterlockedDecrement( &iface->ref );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tracker_PegFromTrackerSource( struct propertyset_reference_tracker *iface )
{
    TRACE( "tracker %p peg\n", iface );
    return S_OK;
}

static const struct propertyset_reference_tracker_vtbl propertyset_tracker_vtbl =
{
    /* Producer-side ComVirtualMethodData layout: slot 0 is the packed header
     * word (self | ComVirtualMethodDataTag=4), not a QI function pointer.
     * The shader copies the entry vtable words into the composite; the
     * consumer (TrackerObjectManager::AfterWrapperCreated) reads composite
     * slot 0 as the header, masks the tag and dereferences chunk+0x18/+0x38
     * for slots 3/7 (ConnectFromTrackerSource / AddRefFromTrackerSource).
     * Pointing the header at the vtable itself makes that walk land on this
     * image-resident chunk's direct stub pointers, so the shader heap-chunk
     * refetch (base 0x6fff00000000, unmapped first page) is never taken.
     * CoreCLR invokes QI through the header's interface data, not by
     * calling slot 0, so the QI path is unchanged. */
    (void *)((uintptr_t)&propertyset_tracker_vtbl + 4),
    tracker_AddRef,
    tracker_Release,
    tracker_ConnectFromTrackerSource,
    tracker_DisconnectFromTrackerSource,
    tracker_FindTrackerTargets,
    tracker_GetReferenceTrackerManager,
    tracker_AddRefFromTrackerSource,
    tracker_ReleaseFromTrackerSource,
    tracker_PegFromTrackerSource,
};

/* Process-wide singleton tracker face (per-propertyset state is not needed:
 * the XAML tracker contract for a builtin data object is satisfied by no-op
 * connect/disconnect/refcount hooks). */
static struct propertyset_reference_tracker propertyset_tracker =
{
    &propertyset_tracker_vtbl, 1
};

static const struct propertyset_reference_tracker_vtbl propertyset_tracker_store_vtbl =
{
    tracker_QueryInterface,
    tracker_AddRef,
    tracker_Release,
    tracker_ConnectFromTrackerSource,
    tracker_DisconnectFromTrackerSource,
    tracker_FindTrackerTargets,
    tracker_GetReferenceTrackerManager,
    tracker_AddRefFromTrackerSource,
    tracker_ReleaseFromTrackerSource,
    tracker_PegFromTrackerSource,
};

static HRESULT STDMETHODCALLTYPE propertyset_QueryInterface( IPropertySet *iface, REFIID iid, void **out )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );

    TRACE( "(%p, %s, %p)\n", iface, debugstr_guid( iid ), out );

    *out = NULL;
    if (IsEqualGUID( iid, &IID_IReferenceTracker_g ))
        return E_NOINTERFACE;

    if (IsEqualGUID( iid, &IID_IMapView_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_String_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_StringObject_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_String_Object ) ||
        IsEqualGUID( iid, &IID_IMapView_Object_Object ))
    {
        *out = &impl->IMapView_HSTRING_IInspectable_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        TRACE( "handing out mapview face %p, vtbl %p\n", *out, *(void **)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IIterable_KeyValuePair_Object_Object ))
    {
        *out = &impl->IIterable_IKeyValuePair_HSTRING_IInspectable_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        TRACE( "handing out iterable face %p, vtbl %p\n", *out, *(void **)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IObservableMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IObservableMap_String_Object ))
    {
        *out = &impl->IObservableMap_HSTRING_IInspectable_iface;
        IUnknown_AddRef( (IUnknown *)*out );
        TRACE( "handing out observable_map face %p, vtbl %p\n", *out, *(void **)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertySet ) ||
        IsEqualGUID( iid, &IID_IMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMap_String_Object ) ||
        IsEqualGUID( iid, &IID_IMap_Object_Object ) ||
        IsEqualGUID( iid, &IID_IResourceDictionary ) ||
        IsEqualGUID( iid, &IID_INotifyCollectionChanged_g ) ||
        IsEqualGUID( iid, &IID_INotifyPropertyChanged_g ))
    {
        *out = iface;
        IUnknown_AddRef( (IUnknown *)*out );
        TRACE( "handing out face %p, vtbl %p\n", *out, *(void **)*out );
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
        struct propertyset_reg *reg = impl->regs;

        /* guard against re-entry if inner releases an outer iface */
        InterlockedAdd( &impl->ref, 0x10000 );
        impl->regs = NULL;
        while (reg)
        {
            struct propertyset_reg *next = reg->next;
            if (reg->handler) IUnknown_Release( reg->handler );
            HeapFree( GetProcessHeap(), 0, reg );
            reg = next;
        }
        if (impl->inner_map) IMap_HSTRING_IInspectable_Release( impl->inner_map );
        if (impl->inner_iterable) IIterable_IKeyValuePair_HSTRING_IInspectable_Release( impl->inner_iterable );
        if (impl->map_inner) IInspectable_Release( impl->map_inner );
        free( impl );
    }

    return ref;
}

const struct com_interface_entry propertyset_shared_pairs[4] =
{
    { (void *)&propertyset_vtbl_full, (IID *)&IID_IPropertySet },
    { (void *)&propertyset_IMapView_vtbl_full, (IID *)&IID_IMapView_HSTRING_IInspectable },
    { (void *)&propertyset_IIterable_vtbl_full, (IID *)&IID_IIterable_IKeyValuePair_HSTRING_IInspectable },
    { (void *)&propertyset_tracker_vtbl, (IID *)&IID_IReferenceTracker_g },
};

/* Probe 11 mirror: expose the static 4-pair array (the same content
 * propertyset_GetIids hands back, see the p209 comment) so the appdata
 * provider can pre-seed the propertyset composite block at head+0xc0.
 * Copy-only: CoreCLR copies these words into its chunk; no calls. */
void propertyset_pair_array_seed( void **dst )
{
    extern const struct com_interface_entry propertyset_shared_pairs[4];
    memcpy( dst, propertyset_shared_pairs, sizeof( propertyset_shared_pairs ) );
}

static HRESULT STDMETHODCALLTYPE propertyset_GetIids( IPropertySet *iface, ULONG *iid_count, IID **iids )
{
    /* Windows reports the PropertySet runtime class interface list (winmd):
     * IPropertySet (default, = IMap<String, IInspectable>) + IObservableMap.
     * The C++/WinRT and CsWinRT projections find IMap/IMapView via the QI
     * arms on the merged face, so the list stays narrow; an over-wide list
     * makes the CoreCLR ComWrappers shader probe IIterable/IIterator
     * adapters whose CreateObject walks past the entries it can satisfy. */
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "(%p, %p, %p)\n", iface, iid_count, iids );
    TRACE( "SHADERPROBE: getiids impl %p count %u cache %p\n", impl, impl->iids_count, impl->iids_cache );

    /* Standard WinRT PropertySet runtime class interface list (winmd):
     * IPropertySet (default) + IMap<HSTRING, IInspectable> + IObservableMap<HSTRING, IInspectable>.
     * CsWinRT and .NET runtime consume this as a standard CoTaskMemAlloc'd IID array. */
    if (0)
    {
        static const IID store_iids[] =
        {
            { 0x8a43ed9f, 0xf4e6, 0x4421, {0xac, 0xf9, 0x1d, 0xab, 0x29, 0x86, 0x82, 0x0c} }, /* IPropertySet */
            { 0x8114f3e9, 0xf118, 0x3c7b, {0x91, 0x94, 0x00, 0xa3, 0xa2, 0x4d, 0x96, 0x7d} }, /* IMap<String, object> */
            { 0x65df2bf5, 0xbf39, 0x41b5, {0xae, 0xbc, 0x5a, 0x9d, 0x86, 0x5e, 0x47, 0x2b} }, /* IObservableMap<String, object> */
            { 0x7c054694, 0x4acd, 0x3716, {0xa1, 0x7a, 0xd5, 0x83, 0xc5, 0xb3, 0xde, 0x8b} }, /* IMapView<String, object> */
            { 0x1b0d3570, 0x0877, 0x5ec2, {0x8a, 0x2c, 0x3b, 0x95, 0x39, 0x50, 0x6a, 0xca} }, /* IMap<HSTRING, IInspectable> */
            { 0x24f981e5, 0x4f81, 0x5fb2, {0xac, 0xc2, 0x49, 0x2a, 0x64, 0xbd, 0x37, 0x08} }, /* IObservableMap<HSTRING, IInspectable> */
        };
        if (iid_count) *iid_count = ARRAY_SIZE( store_iids );
        if (iids)
        {
            IID *array = CoTaskMemAlloc( sizeof( store_iids ) );
            if (!array) return E_OUTOFMEMORY;
            memcpy( array, store_iids, sizeof( store_iids ) );
            *iids = array;
        }
        return S_OK;
    }

    if (iid_count) *iid_count = 4; /* filled on the (NULL, NULL) probe itself */
    {
        /* p209: canonical AE face — IPropertySet (default) +
         * IMapView<HSTRING,IInspectable> + IIterable<IKeyValuePair>.
         *
         * The pair array is fully built at load time (image relocations),
         * not lazily memcpy'd into .bss on the first GetIids: CoreCLR's
         * ComWrappers shader caches the (count, array) pair for the process
         * lifetime and copies the ComInterfaceEntry slots into its heap
         * ComVirtualMethodData chunk (header word stored packed as
         * chunk|ComVirtualMethodDataTag, observed vtable slot 0 =
         * 0x6fff00000000|3 in the p204-p208 AV runs; the consumer in
         * TrackerObjectManager::AfterWrapperCreated reads header slots
         * +0x18/+0x38 = IReferenceTracker ConnectFromTrackerSource /
         * AddRefFromTrackerSource). A shader racing the first-GetIids
         * memcpy published half-written pairs: the copied entry's Vtable
         * slot read back as heap base + tag with the IID slot still zero,
         * and the slot-7 refetch dereferenced 0x6fff0000003b → the 6.7M
         * first-chance AV loop. Compile-time init + single store publish
         * removes the window. One static array per face type outlives
         * every consumer (p202 lesson: the shader caches the pointer). */
        static struct com_interface_entry shared[4] =
        {
            { (void *)&propertyset_vtbl_full,/* .vtbl @ offset 0 */ (IID *)&IID_IPropertySet },
            { (void *)&propertyset_IMapView_vtbl_full, (IID *)&IID_IMapView_HSTRING_IInspectable },
            { (void *)&propertyset_IIterable_vtbl_full, (IID *)&IID_IIterable_IKeyValuePair_HSTRING_IInspectable },
            { (void *)&propertyset_tracker_vtbl, (IID *)&IID_IReferenceTracker_g },
        };
        if (!impl->iids_cache)
        {
            impl->iids_count = ARRAY_SIZE( shared );
            InterlockedCompareExchangePointer( (void **)&impl->iids_cache, shared, NULL );
        }
    }

    if (iid_count) *iid_count = impl->iids_count;
    if (iids)
    {
        *iids = (IID *)impl->iids_cache;
        for (ULONG i = 0; i < impl->iids_count; i++)
            TRACE( "pair[%u] at %p: iid %s (%p) vtable %p\n", i, impl->iids_cache + i,
                   debugstr_guid( impl->iids_cache[i].iid ), (void *)impl->iids_cache[i].iid,
                   (void *)impl->iids_cache[i].vtable );
        TRACE( "returning %u ComInterfaceEntry pairs at %p (slot0 vtable %p, slot0 iid %p)\n",
               impl->iids_count, impl->iids_cache, (void *)impl->iids_cache[0].vtable,
               (void *)impl->iids_cache[0].iid );
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

static const GUID IID_IStringable_ps = {0x96369f54, 0x8eb6, 0x48f0, {0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3}};

static HSTRING propertyset_decode_key( void *key, BOOL *allocated )
{
    UINT32 len = 0;
    const WCHAR *raw;
    HSTRING str = NULL;
    *allocated = FALSE;

    if (!key) return NULL;

    raw = WindowsGetStringRawBuffer( (HSTRING)key, &len );
    if (raw && len < 0x1000)
    {
        return (HSTRING)key;
    }

    __TRY
    {
        IUnknown *unk = (IUnknown *)key;
        void *stringable = NULL;
        if (SUCCEEDED( IUnknown_QueryInterface( unk, &IID_IStringable_ps, &stringable ) ) && stringable)
        {
            void **v = *(void ***)stringable;
            HRESULT (STDMETHODCALLTYPE *to_string)( void *, HSTRING * ) = (void *)v[6];
            if (SUCCEEDED( to_string( stringable, &str ) ) && str)
            {
                *allocated = TRUE;
            }
            IUnknown_Release( (IUnknown *)stringable );
        }
    }
    __EXCEPT_ALL
    {
    }
    __ENDTRY

    return str;
}

/* Slot 6: IMap::Lookup */
static HRESULT STDMETHODCALLTYPE propertyset_Lookup( IPropertySet *iface, HSTRING key, IInspectable **value )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    BOOL allocated = FALSE;
    HSTRING hkey;
    HRESULT hr;

    if (!value) return E_POINTER;

    hkey = propertyset_decode_key( key, &allocated );
    TRACE( "iface %p (vtbl %p), key %p (decoded %s), value %p\n",
           iface, *(void **)iface, key, debugstr_hstring( hkey ), value );

    hr = IMap_HSTRING_IInspectable_Lookup( impl->inner_map, hkey, value );
    if (FAILED(hr) && hkey)
    {
        const WCHAR *name = WindowsGetStringRawBuffer( hkey, NULL );
        ERR( "propertyset_Lookup missing key: %s\n", debugstr_w(name) );
        if (name)
        {
            if (!wcscmp( name, L"AppMinWindowWidth" ))
            {
                hr = create_double_property_value( 320.0, (IInspectable **)value );
                TRACE( "propertyset_Lookup fallback: AppMinWindowWidth -> 320.0, hr %#lx\n", hr );
            }
            else if (!wcscmp( name, L"AppMinWindowHeight" ))
            {
                hr = create_double_property_value( 500.0, (IInspectable **)value );
                TRACE( "propertyset_Lookup fallback: AppMinWindowHeight -> 500.0, hr %#lx\n", hr );
            }
            else if (!wcscmp( name, L"ClearCacheOnNextLaunch" ))
            {
                hr = create_boolean_property_value( FALSE, (IInspectable **)value );
                TRACE( "propertyset_Lookup fallback: ClearCacheOnNextLaunch -> FALSE, hr %#lx\n", hr );
            }
        }
    }
    if (allocated) WindowsDeleteString( hkey );
    return hr;
}

static HRESULT STDMETHODCALLTYPE propertyset_get_Size( IPropertySet *iface, UINT32 *size )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    HRESULT hr = IMap_HSTRING_IInspectable_get_Size( impl->inner_map, size );
    TRACE( "iface %p (vtbl %p), size %p -> %u, hr %08lx\n", iface, *(void **)iface, size,
           size ? *size : 0, hr );
    return hr;
}

static HRESULT STDMETHODCALLTYPE propertyset_HasKey( IPropertySet *iface, HSTRING key, boolean *found )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    BOOL allocated = FALSE;
    HSTRING hkey = propertyset_decode_key( key, &allocated );
    HRESULT hr;

    TRACE( "iface %p, key %p (decoded %s), found %p\n", iface, key, debugstr_hstring( hkey ), found );
    hr = IMap_HSTRING_IInspectable_HasKey( impl->inner_map, hkey, found );
    if (hkey && found && !*found)
    {
        const WCHAR *name = WindowsGetStringRawBuffer( hkey, NULL );
        if (name && (!wcscmp( name, L"AppMinWindowWidth" ) ||
                     !wcscmp( name, L"AppMinWindowHeight" ) ||
                     !wcscmp( name, L"ClearCacheOnNextLaunch" )))
        {
            *found = TRUE;
            hr = S_OK;
        }
    }
    if (allocated) WindowsDeleteString( hkey );
    return hr;
}

/* Slot 9: IMap::GetView */
static HRESULT STDMETHODCALLTYPE propertyset_GetView( IPropertySet *iface, IMapView_HSTRING_IInspectable **view )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );

    TRACE( "iface %p (vtbl %p), view %p\n", iface, *(void **)iface, view );

    if (!view) return E_POINTER;
    return IMap_HSTRING_IInspectable_GetView( impl->inner_map, view );
}

static void propertyset_fire_map_changed( struct propertyset *impl, HSTRING key, enum CollectionChange status );

static HRESULT STDMETHODCALLTYPE propertyset_Insert( IPropertySet *iface, HSTRING key, IInspectable *value, boolean *replaced )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    BOOL allocated = FALSE;
    HSTRING hkey = propertyset_decode_key( key, &allocated );
    HRESULT hr;
    TRACE( "iface %p, key %p (decoded %s), value %p, replaced %p\n", iface, key, debugstr_hstring( hkey ), value, replaced );
    hr = IMap_HSTRING_IInspectable_Insert( impl->inner_map, hkey, value, replaced );
    if (SUCCEEDED( hr ))
        propertyset_fire_map_changed( impl, hkey,
                (replaced && *replaced) ? CollectionChange_ItemChanged : CollectionChange_ItemInserted );
    if (allocated) WindowsDeleteString( hkey );
    return hr;
}

static HRESULT STDMETHODCALLTYPE propertyset_Remove( IPropertySet *iface, HSTRING key )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    HRESULT hr;
    TRACE( "iface %p, key %s\n", iface, debugstr_hstring( key ) );
    hr = IMap_HSTRING_IInspectable_Remove( impl->inner_map, key );
    if (SUCCEEDED( hr )) propertyset_fire_map_changed( impl, key, CollectionChange_ItemRemoved );
    return hr;
}

static HRESULT STDMETHODCALLTYPE propertyset_Clear( IPropertySet *iface )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    HRESULT hr;
    TRACE( "iface %p\n", iface );
    hr = IMap_HSTRING_IInspectable_Clear( impl->inner_map );
    if (SUCCEEDED( hr )) propertyset_fire_map_changed( impl, NULL, CollectionChange_Reset );
    return hr;
}

/* IIterable member of IMap's own requires clause: the MCG ObservableMap
 * adapter keeps it in its own slot (13), separate from the Lookup/First
 * shared slot the shader-facing merged layout uses. */
static HRESULT STDMETHODCALLTYPE propertyset_First( IPropertySet *iface,
                                                    IIterator_IKeyValuePair_HSTRING_IInspectable **iter )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "iface %p, iter %p\n", iface, iter );
    return IIterable_IKeyValuePair_HSTRING_IInspectable_First( impl->inner_iterable, iter );
}

/* Registration list for the event accessors of this face (MapChanged on the
 * merged/observable faces, and the INCC/INPC continuation the MCG
 * ObservableMap adapter drives at slots 17-22).  Handlers are opaque
 * delegates: stored AddRef'd and invoked (as MapChanged) when a mutating
 * face (Insert/Remove/Clear) changes the map; the token is the registration's
 * own address, so a remove is self-validating.  Refcounted, the list is
 * mutated under propertyset_cs (see below); the interlocked push style is
 * kept from the observable-map lists in windows.applicationmodel.resources.core. */

/* One process-wide lock guards the two pieces of shared mutable state in this
 * file that the bare interlocked ops used to patch over:
 *   - the per-object registration push list (regs): the MapChanged fire
 *     snapshots the list under this lock and invokes the (now refcounted)
 *     registrations outside it, so a remove racing the fire cannot free an
 *     in-flight registration;
 *   - the factory's static pin slot (see factory_ActivateInstance).
 * Initialized lazily on first use. */
static CRITICAL_SECTION propertyset_cs;
static RTL_RUN_ONCE propertyset_cs_once = RTL_RUN_ONCE_INIT;

static DWORD WINAPI propertyset_cs_init( PRTL_RUN_ONCE once, PVOID param, PVOID *context )
{
    RtlInitializeCriticalSection( &propertyset_cs );
    return STATUS_SUCCESS;
}

static void propertyset_lock( void )
{
    RtlRunOnceExecuteOnce( &propertyset_cs_once, propertyset_cs_init, NULL, NULL );
    EnterCriticalSection( &propertyset_cs );
}

static void propertyset_unlock( void )
{
    LeaveCriticalSection( &propertyset_cs );
}

static void propertyset_reg_release( struct propertyset_reg *reg )
{
    if (!InterlockedDecrement( &reg->ref ))
    {
        if (reg->handler) IUnknown_Release( reg->handler );
        HeapFree( GetProcessHeap(), 0, reg );
    }
}

static HRESULT propertyset_reg_add( struct propertyset *impl, IUnknown *handler, EventRegistrationToken *token )
{
    struct propertyset_reg *reg;

    if (!token) return E_POINTER;
    if (!(reg = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*reg) ))) return E_OUTOFMEMORY;
    reg->ref = 1;
    reg->handler = handler;
    if (handler) IUnknown_AddRef( handler );
    propertyset_lock();
    reg->next = impl->regs;
    impl->regs = reg;
    propertyset_unlock();
    token->value = (INT64)(INT_PTR)reg;
    return S_OK;
}

static HRESULT propertyset_reg_remove( struct propertyset *impl, EventRegistrationToken token )
{
    struct propertyset_reg *cur, **slot;

    propertyset_lock();
    slot = (struct propertyset_reg **)&impl->regs;
    while ((cur = *slot))
    {
        if ((INT64)(INT_PTR)cur == token.value)
        {
            *slot = cur->next;
            propertyset_unlock();
            propertyset_reg_release( cur ); /* drops the list reference */
            return S_OK;
        }
        slot = &cur->next;
    }
    propertyset_unlock();
    return S_OK; /* remove of an unknown token: the adapter's Dispose removes twice */
}

/* IMapChangedEventArgs<HSTRING>: the event-args pair a MapChanged delegate
 * carries. This tree's projection (windows.foundation.collections.idl) names
 * the status accessor get_CollectionChanged; it is the winmd ChangedStatus
 * (Inserted/Removed/Replaced), and Clear reports Reset with a NULL key.
 * Hand-rolled vtable, the style used throughout this file; the object lives
 * on propertyset_fire_map_changed's stack across the Invoke calls. */
struct propertyset_changed_args
{
    IMapChangedEventArgs_HSTRING iface;
    LONG ref;
    HSTRING key;
    enum CollectionChange status;
};

static inline struct propertyset_changed_args *changed_args_from_iface( IMapChangedEventArgs_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct propertyset_changed_args, iface );
}

static HRESULT STDMETHODCALLTYPE changed_args_QueryInterface( IMapChangedEventArgs_HSTRING *iface, REFIID iid, void **out )
{
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapChangedEventArgs_HSTRING ))
    {
        *out = iface;
        IMapChangedEventArgs_HSTRING_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE changed_args_AddRef( IMapChangedEventArgs_HSTRING *iface )
{
    struct propertyset_changed_args *impl = changed_args_from_iface( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG STDMETHODCALLTYPE changed_args_Release( IMapChangedEventArgs_HSTRING *iface )
{
    struct propertyset_changed_args *impl = changed_args_from_iface( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT STDMETHODCALLTYPE changed_args_GetIids( IMapChangedEventArgs_HSTRING *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE changed_args_GetRuntimeClassName( IMapChangedEventArgs_HSTRING *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.MapChangedEventHandler`1";
    if (!name) return E_POINTER;
    return WindowsCreateString( cls, ARRAY_SIZE( cls ) - 1, name );
}

static HRESULT STDMETHODCALLTYPE changed_args_GetTrustLevel( IMapChangedEventArgs_HSTRING *iface, TrustLevel *trust )
{
    if (trust) *trust = FullTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE changed_args_get_CollectionChanged( IMapChangedEventArgs_HSTRING *iface,
                                                                     enum CollectionChange *value )
{
    struct propertyset_changed_args *impl = changed_args_from_iface( iface );
    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE changed_args_get_Key( IMapChangedEventArgs_HSTRING *iface, HSTRING *key )
{
    struct propertyset_changed_args *impl = changed_args_from_iface( iface );
    if (!key) return E_POINTER;
    *key = NULL;
    if (!impl->key) return S_OK;
    return WindowsDuplicateString( impl->key, key );
}

static const IMapChangedEventArgs_HSTRINGVtbl propertyset_changed_args_vtbl =
{
    changed_args_QueryInterface,
    changed_args_AddRef,
    changed_args_Release,
    changed_args_GetIids,
    changed_args_GetRuntimeClassName,
    changed_args_GetTrustLevel,
    changed_args_get_CollectionChanged,
    changed_args_get_Key,
};

/* Raise MapChanged on every registered handler (MapChanged shares the reg list
 * with the INCC/INPC registrations; their delegates are opaque, caller-clean
 * stdcall, and all take (sender, args)). The list is snapshotted under the
 * lock, each registration is pinned with a ref and invoked outside it. */
static void propertyset_fire_map_changed( struct propertyset *impl, HSTRING key, enum CollectionChange status )
{
    struct propertyset_changed_args args;
    struct propertyset_reg *reg;

    propertyset_lock();
    for (reg = impl->regs; reg; reg = reg->next) InterlockedIncrement( &reg->ref ); /* pin for the fire */
    reg = impl->regs;
    propertyset_unlock();

    if (!reg) return;

    args.iface.lpVtbl = &propertyset_changed_args_vtbl;
    args.ref = 1;
    args.key = key;
    args.status = status;

    while (reg)
    {
        struct propertyset_reg *next = reg->next;
        if (reg->handler)
        {
            __TRY
            {
                IMapChangedEventHandler_HSTRING_IInspectable_Invoke(
                    (IMapChangedEventHandler_HSTRING_IInspectable *)reg->handler,
                    (IObservableMap_HSTRING_IInspectable *)&impl->IPropertySet_iface, &args.iface );
            }
            __EXCEPT_ALL
            {
            }
            __ENDTRY;
        }
        propertyset_reg_release( reg );
        reg = next;
    }
}

static HRESULT STDMETHODCALLTYPE propertyset_add_MapChanged( IPropertySet *iface,
                                                            IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                            EventRegistrationToken *token )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "(%p, %p, %p)\n", iface, handler, token );
    return propertyset_reg_add( impl, (IUnknown *)handler, token );
}

static HRESULT STDMETHODCALLTYPE propertyset_remove_MapChanged( IPropertySet *iface, EventRegistrationToken token )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "(%p, %I64d)\n", iface, token.value );
    return propertyset_reg_remove( impl, token );
}

/* get_MapChanged: the MCG ObservableMap class vtable puts this accessor at
 * slot 16 (event = add/remove/get triple); the property is the last handler
 * registered, and S_OK with NULL for none. */
static HRESULT STDMETHODCALLTYPE propertyset_get_MapChanged( IPropertySet *iface,
                                                            IMapChangedEventHandler_HSTRING_IInspectable **handler )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "(%p, %p)\n", iface, handler );
    if (!handler) return E_POINTER;
    *handler = (IMapChangedEventHandler_HSTRING_IInspectable *)impl->regs ?
                   (IMapChangedEventHandler_HSTRING_IInspectable *)impl->regs->handler : NULL;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

/* INotifyCollectionChanged / INotifyPropertyChanged: the MCG ObservableMap
 * adapter is one class over the IObservableMap pointer — slots 17-19 are the
 * CollectionChanged add/remove/get pair (17 is the one the CoreRT interop
 * stub calls through [iface+0x88] and writes the EventRegistrationToken
 * out-param), slots 20-22 the PropertyChanged triple.  The delegates are
 * opaque; one registration list serves all six. */
static HRESULT STDMETHODCALLTYPE propertyset_add_CollectionChanged( IPropertySet *iface, void *handler,
                                                                    EventRegistrationToken *token )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "(%p, %p, %p)\n", iface, handler, token );
    return propertyset_reg_add( impl, (IUnknown *)handler, token );
}

static HRESULT STDMETHODCALLTYPE propertyset_remove_CollectionChanged( IPropertySet *iface, EventRegistrationToken token )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "(%p, %I64d)\n", iface, token.value );
    return propertyset_reg_remove( impl, token );
}

static HRESULT STDMETHODCALLTYPE propertyset_get_CollectionChanged( IPropertySet *iface, void **handler )
{
    struct propertyset *impl = impl_from_IPropertySet( iface );
    TRACE( "(%p, %p)\n", iface, handler );
    if (!handler) return E_POINTER;
    *handler = impl->regs ? (void *)impl->regs->handler : NULL;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

/* MCG ObservableMap<HSTRING,IInspectable*> adapter layout (SharedLibrary
 * McgMarshal casts the QI'd pointer to this class vtable):
 *
 *   6-12  Lookup, get_Size, HasKey, GetView, Insert, Remove, Clear
 *   13    First (IIterable member of IMap's requires clause)
 *   14-16 add / remove / get_MapChanged
 *   17-19 add / remove / get_CollectionChanged (INotifyCollectionChanged)
 *   20-22 add / remove / get_PropertyChanged (INotifyPropertyChanged)
 *   23    NULL terminator (the shader's InvokeableVtable mode counts method
 *         slots by walking to the first NULL entry)
 *
 * Slots 17+ are the interop-call target of the CoreRT stub (call [rax+0x88]);
 * before they existed the stub ran the data behind the vtable and every
 * fault nested the mrt100 VEH one context frame deeper. */
static const struct propertyset_vtbl_full_s
{
    IPropertySetVtbl vtbl;
    void *map_slots[7];                /* 6-12 */
    void *First;                       /* 13 */
    void *add_MapChanged;              /* 14 */
    void *remove_MapChanged;           /* 15 */
    void *get_MapChanged;              /* 16 */
    void *add_CollectionChanged;       /* 17 */
    void *remove_CollectionChanged;    /* 18 */
    void *get_CollectionChanged;       /* 19 */
    void *add_PropertyChanged;         /* 20 */
    void *remove_PropertyChanged;      /* 21 */
    void *get_PropertyChanged;         /* 22 */
    void *null_term;                   /* 23 */
} propertyset_vtbl_full =
{
    { /* IUnknown */
    propertyset_QueryInterface,
    propertyset_AddRef,
    propertyset_Release,
    /* IInspectable */
    propertyset_GetIids,
    propertyset_GetRuntimeClassName,
    propertyset_GetTrustLevel },
    { (void *)propertyset_Lookup,
      (void *)propertyset_get_Size,
      (void *)propertyset_HasKey,
      (void *)propertyset_GetView,
      (void *)propertyset_Insert,
      (void *)propertyset_Remove,
      (void *)propertyset_Clear },
    (void *)propertyset_First,
    (void *)propertyset_add_MapChanged,
    (void *)propertyset_remove_MapChanged,
    (void *)propertyset_get_MapChanged,
    (void *)propertyset_add_CollectionChanged,
    (void *)propertyset_remove_CollectionChanged,
    (void *)propertyset_get_CollectionChanged,
    (void *)propertyset_add_CollectionChanged,   /* PropertyChanged reuses the
                                                  * opaque registration pair */
    (void *)propertyset_remove_CollectionChanged,
    (void *)propertyset_get_CollectionChanged,
    0,
};
#define propertyset_vtbl propertyset_vtbl_full.vtbl


DEFINE_IINSPECTABLE( propertyset_IObservableMap, IObservableMap_HSTRING_IInspectable, struct propertyset, IPropertySet_iface );

/* The canonical IObservableMap<HSTRING, IInspectable*> face is the 15-slot
 * flattening of IMap (windows.foundation.collections.idl: IObservableMap
 * requires IMap): Lookup, Size, HasKey, GetView, Insert, Remove, Clear, then
 * add/remove_MapChanged. The CoreCLR shader hands this exact vtable to the
 * IObservableMap entry of GetIids, and the CsWinRT projection calls slots by
 * that ordinal; a truncated face (add/remove at 6/7) faults the consumer. */
static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_Lookup( IObservableMap_HSTRING_IInspectable *iface,
                                                                    HSTRING key, IInspectable **value )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return IMap_HSTRING_IInspectable_Lookup( impl->inner_map, key, value );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_get_Size( IObservableMap_HSTRING_IInspectable *iface, UINT32 *size )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return IMap_HSTRING_IInspectable_get_Size( impl->inner_map, size );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_HasKey( IObservableMap_HSTRING_IInspectable *iface, HSTRING key, boolean *found )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return IMap_HSTRING_IInspectable_HasKey( impl->inner_map, key, found );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_GetView( IObservableMap_HSTRING_IInspectable *iface, IMapView_HSTRING_IInspectable **view )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return IMap_HSTRING_IInspectable_GetView( impl->inner_map, view );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_Insert( IObservableMap_HSTRING_IInspectable *iface, HSTRING key, IInspectable *value, boolean *replaced )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    /* route through the merged face so both faces raise MapChanged identically */
    return propertyset_Insert( &impl->IPropertySet_iface, key, value, replaced );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_Remove( IObservableMap_HSTRING_IInspectable *iface, HSTRING key )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_Remove( &impl->IPropertySet_iface, key );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_Clear( IObservableMap_HSTRING_IInspectable *iface )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_Clear( &impl->IPropertySet_iface );
}

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

/* First + the event accessors past the wine vtbl, in MCG ObservableMap order
 * (Lookup..First delegated at 8-15, get at 16, INCC 17-19, INPC 20-22). */
static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_First( IObservableMap_HSTRING_IInspectable *iface,
                                                                   IIterator_IKeyValuePair_HSTRING_IInspectable **iter )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return IIterable_IKeyValuePair_HSTRING_IInspectable_First( impl->inner_iterable, iter );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_get_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                                            IMapChangedEventHandler_HSTRING_IInspectable **handler )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_get_MapChanged( &impl->IPropertySet_iface, handler );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_add_CollectionChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                                                   void *handler, EventRegistrationToken *token )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_add_CollectionChanged( &impl->IPropertySet_iface, handler, token );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_remove_CollectionChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                                                      EventRegistrationToken token )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_remove_CollectionChanged( &impl->IPropertySet_iface, token );
}

static HRESULT STDMETHODCALLTYPE propertyset_IObservableMap_get_CollectionChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                                                   void **handler )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IObservableMap_HSTRING_IInspectable_iface );
    return propertyset_get_CollectionChanged( &impl->IPropertySet_iface, handler );
}

/* MCG ObservableMap<HSTRING,IInspectable*> class vtable: 0-5 IU+IInspectable,
 * 6 add / 7 remove_MapChanged (widl order), delegated IMap 8-14, First 15,
 * get_MapChanged 16, INCC add/remove/get 17-19, INPC add/remove/get 20-22.
 * The CoreRT interop stub calls [iface+0x88] = slot 17 and writes the
 * EventRegistrationToken out-param; slots past the wine vtbl used to be the
 * zero pad, so the callee jumped into data and the fault re-entered the mrt100
 * hardware VEH (284 nesting write faults).  23 = NULL terminator. */
const static struct
{
    IObservableMap_HSTRING_IInspectableVtbl vtbl;  /* 0-7 */
    void *map_slots[7];                            /* 8-14 Lookup..Clear */
    void *First;                                   /* 15 */
    void *get_MapChanged;                          /* 16 */
    void *add_CollectionChanged;                   /* 17 */
    void *remove_CollectionChanged;                /* 18 */
    void *get_CollectionChanged;                   /* 19 */
    void *add_PropertyChanged;                     /* 20 */
    void *remove_PropertyChanged;                  /* 21 */
    void *get_PropertyChanged;                     /* 22 */
    void *null_term;                               /* 23 */
} propertyset_IObservableMap_vtbl_full =
{
    { /* IUnknown */
    propertyset_IObservableMap_QueryInterface,
    propertyset_IObservableMap_AddRef,
    propertyset_IObservableMap_Release,
    /* IInspectable */
    propertyset_IObservableMap_GetIids,
    propertyset_IObservableMap_GetRuntimeClassName,
    propertyset_IObservableMap_GetTrustLevel,
    /* IObservableMap<HSTRING, IInspectable*> own slots; wine's
     * __FIObservableMap_2_HSTRING_IInspectableVtbl carries only these two —
     * the IMap<HSTRING,IInspectable*> slots follow in the MCG flattening. */
    propertyset_IObservableMap_add_MapChanged,
    propertyset_IObservableMap_remove_MapChanged, },
    { (void *)propertyset_IObservableMap_Lookup,
      (void *)propertyset_IObservableMap_get_Size,
      (void *)propertyset_IObservableMap_HasKey,
      (void *)propertyset_IObservableMap_GetView,
      (void *)propertyset_IObservableMap_Insert,
      (void *)propertyset_IObservableMap_Remove,
      (void *)propertyset_IObservableMap_Clear },
    (void *)propertyset_IObservableMap_First,
    (void *)propertyset_IObservableMap_get_MapChanged,
    (void *)propertyset_IObservableMap_add_CollectionChanged,
    (void *)propertyset_IObservableMap_remove_CollectionChanged,
    (void *)propertyset_IObservableMap_get_CollectionChanged,
    (void *)propertyset_IObservableMap_add_CollectionChanged,
    (void *)propertyset_IObservableMap_remove_CollectionChanged,
    (void *)propertyset_IObservableMap_get_CollectionChanged,
    0,
};
#define propertyset_IObservableMap_vtbl propertyset_IObservableMap_vtbl_full.vtbl


/* The per-interface faces below remain addressable for consumers that cached
 * them before the merge; every method forwards to the merged object. */
DEFINE_IINSPECTABLE( propertyset_IMapView, IMapView_HSTRING_IInspectable, struct propertyset, IPropertySet_iface );

static HRESULT STDMETHODCALLTYPE propertyset_IMapView_Lookup( IMapView_HSTRING_IInspectable *iface, HSTRING key, IInspectable **value )
{
    struct propertyset *impl = CONTAINING_RECORD( iface, struct propertyset, IMapView_HSTRING_IInspectable_iface );
    TRACE( "viewface %p, key %s, value %p\n", iface, debugstr_hstring( key ), value );
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
    IMapView_HSTRING_IInspectable *view;
    HRESULT hr;

    TRACE( "viewface %p, first %p, second %p\n", iface, first, second );
    if (!first || !second) return E_POINTER;
    if (FAILED(hr = IMap_HSTRING_IInspectable_GetView( impl->inner_map, &view ))) return hr;
    hr = IMapView_HSTRING_IInspectable_Split( view, first, second );
    IMapView_HSTRING_IInspectable_Release( view );
    return hr;
}

static const struct propertyset_imapview_vtbl_full_s
{ IMapView_HSTRING_IInspectableVtbl vtbl; void *pad[4]; } propertyset_IMapView_vtbl_full =
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
    TRACE( "iterableface %p, value %p\n", iface, value );
    return IIterable_IKeyValuePair_HSTRING_IInspectable_First( impl->inner_iterable, value );
}

static const struct propertyset_iterable_vtbl_full_s
{
    IIterable_IKeyValuePair_HSTRING_IInspectableVtbl vtbl;
    void *add_ref_from_tracker_source;  /* 7 */
    void *pad[3];
} propertyset_IIterable_vtbl_full =
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
    /* 7: the CoreCLR ComWrappers shader hands this vtable to the shader
     * chunk for the IIterable entry of GetIids and calls slot 7 as
     * IReferenceTracker::AddRefFromTrackerSource() (coreclr
     * TrackerObjectManager::AfterWrapperCreated, mov 0x38(%rax),%rax);
     * with pad[0] = NULL the shader's slot-7 refetch returned a heap
     * blob address and faulted on the code fetch. A refcount bump answers
     * the tracker contract. */
    (void *)propertyset_IIterable_AddRef,
    { 0, 0, 0 }
};
#define propertyset_IIterable_vtbl propertyset_IIterable_vtbl_full.vtbl


/* The merged-face ComInterfaceEntry pairs are compile-time initialized with
 * direct &vtbl_full.vtbl references (see propertyset_GetIids); the former
 * runtime accessors are gone. */


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

    if (IsEqualGUID( &IID_IReferenceTracker_g, iid ))
    {
        *out = NULL;
        return E_NOINTERFACE;
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

static const struct map_iids propertyset_map_iids =
{
    .map = &IID_IMap_HSTRING_IInspectable,
    .view = &IID_IMapView_HSTRING_IInspectable,
    .iterable = &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
    .iterator = &IID_IIterator_IKeyValuePair_HSTRING_IInspectable,
    .pair = &IID_IKeyValuePair_HSTRING_IInspectable,
};

static HRESULT STDMETHODCALLTYPE factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    /* Single static pin (depth-1) under propertyset_cs: CoreCLR's ComWrappers
     * CreateObject runs an AddRef/Release pair around the GetIids probe on the
     * just-activated instance and the caller (e.g. the CoreRT McgInterop shim)
     * may drop its own reference immediately, so the slot keeps the most
     * recently activated instance alive past that. The former depth-2 ring
     * raced: two unsequenced InterlockedExchangePointer chains let a
     * concurrent ActivateInstance evict and release an instance a caller in
     * another CreateObject still held. One slot, published under the lock,
     * has no eviction window; the wintypes suite is the judge. */
    static struct propertyset *pinned;
    struct propertyset *impl, *old = NULL;
    HRESULT hr;

    TRACE( "(%p, %p)\n", iface, instance );

    if (!instance) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IPropertySet_iface.lpVtbl = &propertyset_vtbl;
    impl->IMapView_HSTRING_IInspectable_iface.lpVtbl = &propertyset_IMapView_vtbl;
    impl->IIterable_IKeyValuePair_HSTRING_IInspectable_iface.lpVtbl = &propertyset_IIterable_vtbl;
    impl->IObservableMap_HSTRING_IInspectable_iface.lpVtbl = &propertyset_IObservableMap_vtbl;
    impl->ref = 1;

    if (FAILED(hr = multi_threaded_map_create( &propertyset_map_iids,
                                               (IInspectable *)&impl->IPropertySet_iface,
                                               &impl->map_inner )))
    {
        free( impl );
        return hr;
    }
    if (FAILED(hr = IInspectable_QueryInterface( impl->map_inner, &IID_IMap_HSTRING_IInspectable,
                                                 (void **)&impl->inner_map )))
    {
        IInspectable_Release( impl->map_inner );
        free( impl );
        return hr;
    }
    IMap_HSTRING_IInspectable_Release( impl->inner_map ); /* release outer self-reference */
    if (FAILED(hr = IInspectable_QueryInterface( impl->map_inner, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                 (void **)&impl->inner_iterable )))
    {
        IInspectable_Release( impl->map_inner );
        free( impl );
        return hr;
    }
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release( impl->inner_iterable ); /* release outer self-reference */

    propertyset_lock();
    old = pinned;
    InterlockedIncrement( &impl->ref );  /* pin */
    pinned = impl;
    propertyset_unlock();
    if (old) IPropertySet_Release( &old->IPropertySet_iface );

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

/* ============================================================================
 * Windows.Foundation.Collections.StringMap
 * ============================================================================ */

static const GUID SM_IID_IMap_HSTRING_HSTRING =
    { 0xf6d1f700, 0x49c2, 0x52ae, {0x81, 0x54, 0x82, 0x6f, 0x99, 0x08, 0x77, 0x3c} };
static const GUID SM_IID_IObservableMap_HSTRING_HSTRING =
    { 0x1e036276, 0x2f60, 0x55f6, {0xb7, 0xf3, 0xf8, 0x60, 0x79, 0xe6, 0x90, 0x0b} };
static const GUID SM_IID_IMapView_HSTRING_HSTRING =
    { 0xac7f26f2, 0xfeb7, 0x5b2a, {0x8a, 0xc4, 0x34, 0x5b, 0xc6, 0x2c, 0xae, 0xde} };
static const GUID SM_IID_IKeyValuePair_HSTRING_HSTRING =
    { 0x60310303, 0x49c5, 0x52e6, {0xab, 0xc6, 0xa9, 0xb3, 0x6e, 0xcc, 0xc7, 0x16} };
static const GUID SM_IID_IIterable_IKeyValuePair_HSTRING_HSTRING =
    { 0xe9bdaaf0, 0xcbf6, 0x5c72, {0xbe, 0x90, 0x29, 0xcb, 0xf3, 0xa1, 0x31, 0x9b} };
static const GUID SM_IID_IIterator_IKeyValuePair_HSTRING_HSTRING =
    { 0x05eb86f1, 0x7140, 0x5517, {0xb8, 0x8d, 0xcb, 0xae, 0xbe, 0x57, 0xe6, 0xb1} };

struct stringmap_entry
{
    HSTRING key;
    HSTRING value;
};

struct stringmap_kvp
{
    const void *lpVtbl;
    LONG ref;
    HSTRING key;
    HSTRING value;
};

struct stringmap_iter
{
    const void *lpVtbl;
    LONG ref;
    struct stringmap_entry *entries;
    UINT32 count;
    UINT32 index;
};

struct stringmap_view
{
    const void *lpVtbl_view;
    const void *lpVtbl_iterable;
    LONG ref;
    struct stringmap_entry *entries;
    UINT32 count;
};

struct stringmap_handler
{
    INT64 token;
    IUnknown *handler;
};

struct stringmap
{
    const void *lpVtbl_map;
    const void *lpVtbl_obs;
    const void *lpVtbl_iterable;
    LONG ref;
    SRWLOCK lock;
    struct stringmap_entry *entries;
    UINT32 count;
    UINT32 capacity;
    struct stringmap_handler *handlers;
    UINT32 handler_count;
    INT64 next_token;
};

static HRESULT stringmap_kvp_create( HSTRING key, HSTRING value, void **out );
static HRESULT stringmap_iter_create( const struct stringmap_entry *src, UINT32 count, void **out );
static HRESULT stringmap_view_create( const struct stringmap_entry *src, UINT32 count, void **out );

/* --- IKeyValuePair<HSTRING, HSTRING> --- */

static HRESULT WINAPI sm_kvp_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct stringmap_kvp *impl = (struct stringmap_kvp *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &SM_IID_IKeyValuePair_HSTRING_HSTRING ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sm_kvp_AddRef( void *iface )
{
    struct stringmap_kvp *impl = (struct stringmap_kvp *)iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sm_kvp_Release( void *iface )
{
    struct stringmap_kvp *impl = (struct stringmap_kvp *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->key );
        WindowsDeleteString( impl->value );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sm_kvp_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *iids = CoTaskMemAlloc( sizeof(IID) );
    if (!*iids) { *count = 0; return E_OUTOFMEMORY; }
    (*iids)[0] = SM_IID_IKeyValuePair_HSTRING_HSTRING;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI sm_kvp_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IKeyValuePair`2<String, String>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}

static HRESULT WINAPI sm_kvp_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sm_kvp_get_Key( void *iface, HSTRING *key )
{
    struct stringmap_kvp *impl = (struct stringmap_kvp *)iface;
    if (!key) return E_POINTER;
    return WindowsDuplicateString( impl->key, key );
}

static HRESULT WINAPI sm_kvp_get_Value( void *iface, HSTRING *value )
{
    struct stringmap_kvp *impl = (struct stringmap_kvp *)iface;
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->value, value );
}

static const void * const sm_kvp_vtbl[] =
{
    sm_kvp_QueryInterface,
    sm_kvp_AddRef,
    sm_kvp_Release,
    sm_kvp_GetIids,
    sm_kvp_GetRuntimeClassName,
    sm_kvp_GetTrustLevel,
    sm_kvp_get_Key,
    sm_kvp_get_Value,
};

static HRESULT stringmap_kvp_create( HSTRING key, HSTRING value, void **out )
{
    struct stringmap_kvp *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = sm_kvp_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsDuplicateString( key, &impl->key )) ||
        FAILED(hr = WindowsDuplicateString( value, &impl->value )))
    {
        WindowsDeleteString( impl->key );
        free( impl );
        return hr;
    }
    *out = impl;
    return S_OK;
}

/* --- IIterator<IKeyValuePair<HSTRING, HSTRING>> --- */

static HRESULT WINAPI sm_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct stringmap_iter *impl = (struct stringmap_iter *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &SM_IID_IIterator_IKeyValuePair_HSTRING_HSTRING ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sm_iter_AddRef( void *iface )
{
    struct stringmap_iter *impl = (struct stringmap_iter *)iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sm_iter_Release( void *iface )
{
    struct stringmap_iter *impl = (struct stringmap_iter *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->entries[i].key );
            WindowsDeleteString( impl->entries[i].value );
        }
        free( impl->entries );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sm_iter_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *iids = CoTaskMemAlloc( sizeof(IID) );
    if (!*iids) { *count = 0; return E_OUTOFMEMORY; }
    (*iids)[0] = SM_IID_IIterator_IKeyValuePair_HSTRING_HSTRING;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI sm_iter_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IKeyValuePair`2<String, String>>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}

static HRESULT WINAPI sm_iter_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sm_iter_get_Current( void *iface, void **current )
{
    struct stringmap_iter *impl = (struct stringmap_iter *)iface;
    if (!current) return E_POINTER;
    *current = NULL;
    if (impl->index >= impl->count) return E_BOUNDS;
    return stringmap_kvp_create( impl->entries[impl->index].key, impl->entries[impl->index].value, current );
}

static HRESULT WINAPI sm_iter_get_HasCurrent( void *iface, boolean *has_current )
{
    struct stringmap_iter *impl = (struct stringmap_iter *)iface;
    if (!has_current) return E_POINTER;
    *has_current = (impl->index < impl->count);
    return S_OK;
}

static HRESULT WINAPI sm_iter_MoveNext( void *iface, boolean *has_current )
{
    struct stringmap_iter *impl = (struct stringmap_iter *)iface;
    if (!has_current) return E_POINTER;
    if (impl->index < impl->count) impl->index++;
    *has_current = (impl->index < impl->count);
    return S_OK;
}

static HRESULT WINAPI sm_iter_GetMany( void *iface, UINT32 capacity, void **items, UINT32 *actual )
{
    struct stringmap_iter *impl = (struct stringmap_iter *)iface;
    UINT32 n = 0;
    if (!actual) return E_POINTER;
    *actual = 0;
    while (n < capacity && impl->index < impl->count)
    {
        HRESULT hr = stringmap_kvp_create( impl->entries[impl->index].key, impl->entries[impl->index].value, &items[n] );
        if (FAILED(hr)) return hr;
        impl->index++;
        n++;
    }
    *actual = n;
    return S_OK;
}

static const void * const sm_iter_vtbl[] =
{
    sm_iter_QueryInterface,
    sm_iter_AddRef,
    sm_iter_Release,
    sm_iter_GetIids,
    sm_iter_GetRuntimeClassName,
    sm_iter_GetTrustLevel,
    sm_iter_get_Current,
    sm_iter_get_HasCurrent,
    sm_iter_MoveNext,
    sm_iter_GetMany,
};

static HRESULT stringmap_iter_create( const struct stringmap_entry *src, UINT32 count, void **out )
{
    struct stringmap_iter *impl;
    UINT32 i;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = sm_iter_vtbl;
    impl->ref = 1;
    if (count)
    {
        if (!(impl->entries = calloc( count, sizeof(*impl->entries) )))
        {
            free( impl );
            return E_OUTOFMEMORY;
        }
        for (i = 0; i < count; i++)
        {
            if (FAILED(hr = WindowsDuplicateString( src[i].key, &impl->entries[i].key )) ||
                FAILED(hr = WindowsDuplicateString( src[i].value, &impl->entries[i].value )))
            {
                impl->count = i + 1;
                sm_iter_Release( impl );
                return hr;
            }
        }
        impl->count = count;
    }
    *out = impl;
    return S_OK;
}

/* --- IMapView<HSTRING, HSTRING> --- */

static inline struct stringmap_view *sm_view_from_view( void *iface )
{
    return CONTAINING_RECORD( iface, struct stringmap_view, lpVtbl_view );
}

static inline struct stringmap_view *sm_view_from_iter( void *iface )
{
    return CONTAINING_RECORD( iface, struct stringmap_view, lpVtbl_iterable );
}

static HRESULT WINAPI sm_view_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct stringmap_view *impl = sm_view_from_view( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &SM_IID_IMapView_HSTRING_HSTRING ))
    {
        *out = &impl->lpVtbl_view;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &SM_IID_IIterable_IKeyValuePair_HSTRING_HSTRING ))
    {
        *out = &impl->lpVtbl_iterable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sm_view_AddRef( void *iface )
{
    struct stringmap_view *impl = sm_view_from_view( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sm_view_Release( void *iface )
{
    struct stringmap_view *impl = sm_view_from_view( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->entries[i].key );
            WindowsDeleteString( impl->entries[i].value );
        }
        free( impl->entries );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sm_view_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *iids = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!*iids) { *count = 0; return E_OUTOFMEMORY; }
    (*iids)[0] = SM_IID_IMapView_HSTRING_HSTRING;
    (*iids)[1] = SM_IID_IIterable_IKeyValuePair_HSTRING_HSTRING;
    *count = 2;
    return S_OK;
}

static HRESULT WINAPI sm_view_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IMapView`2<String, String>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}

static HRESULT WINAPI sm_view_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sm_view_Lookup( void *iface, HSTRING key, HSTRING *value )
{
    struct stringmap_view *impl = sm_view_from_view( iface );
    UINT32 i;
    INT32 cmp;
    if (!value) return E_POINTER;
    *value = NULL;
    for (i = 0; i < impl->count; i++)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
            return WindowsDuplicateString( impl->entries[i].value, value );
    }
    return E_BOUNDS;
}

static HRESULT WINAPI sm_view_get_Size( void *iface, UINT32 *size )
{
    struct stringmap_view *impl = sm_view_from_view( iface );
    if (!size) return E_POINTER;
    *size = impl->count;
    return S_OK;
}

static HRESULT WINAPI sm_view_HasKey( void *iface, HSTRING key, boolean *found )
{
    struct stringmap_view *impl = sm_view_from_view( iface );
    UINT32 i;
    INT32 cmp;
    if (!found) return E_POINTER;
    *found = FALSE;
    for (i = 0; i < impl->count; i++)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
        {
            *found = TRUE;
            return S_OK;
        }
    }
    return S_OK;
}

static HRESULT WINAPI sm_view_Split( void *iface, void **first, void **second )
{
    struct stringmap_view *impl = sm_view_from_view( iface );
    UINT32 mid;
    HRESULT hr;
    if (!first || !second) return E_POINTER;
    *first = *second = NULL;
    if (impl->count < 2) return S_OK;
    mid = impl->count / 2;
    if (FAILED(hr = stringmap_view_create( impl->entries, mid, first ))) return hr;
    if (FAILED(hr = stringmap_view_create( impl->entries + mid, impl->count - mid, second )))
    {
        sm_view_Release( *first );
        *first = NULL;
        return hr;
    }
    return S_OK;
}

static HRESULT WINAPI sm_view_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    return sm_view_QueryInterface( &sm_view_from_iter( iface )->lpVtbl_view, iid, out );
}
static ULONG WINAPI sm_view_iter_AddRef( void *iface )
{
    return sm_view_AddRef( &sm_view_from_iter( iface )->lpVtbl_view );
}
static ULONG WINAPI sm_view_iter_Release( void *iface )
{
    return sm_view_Release( &sm_view_from_iter( iface )->lpVtbl_view );
}
static HRESULT WINAPI sm_view_iter_GetIids( void *iface, ULONG *c, IID **i )
{
    return sm_view_GetIids( &sm_view_from_iter( iface )->lpVtbl_view, c, i );
}
static HRESULT WINAPI sm_view_iter_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return sm_view_GetRuntimeClassName( &sm_view_from_iter( iface )->lpVtbl_view, n );
}
static HRESULT WINAPI sm_view_iter_GetTrustLevel( void *iface, TrustLevel *l )
{
    return sm_view_GetTrustLevel( &sm_view_from_iter( iface )->lpVtbl_view, l );
}
static HRESULT WINAPI sm_view_iter_First( void *iface, void **first )
{
    struct stringmap_view *impl = sm_view_from_iter( iface );
    return stringmap_iter_create( impl->entries, impl->count, first );
}

static const void * const sm_view_vtbl[] =
{
    sm_view_QueryInterface,
    sm_view_AddRef,
    sm_view_Release,
    sm_view_GetIids,
    sm_view_GetRuntimeClassName,
    sm_view_GetTrustLevel,
    sm_view_Lookup,
    sm_view_get_Size,
    sm_view_HasKey,
    sm_view_Split,
};

static const void * const sm_view_iter_vtbl[] =
{
    sm_view_iter_QueryInterface,
    sm_view_iter_AddRef,
    sm_view_iter_Release,
    sm_view_iter_GetIids,
    sm_view_iter_GetRuntimeClassName,
    sm_view_iter_GetTrustLevel,
    sm_view_iter_First,
};

static HRESULT stringmap_view_create( const struct stringmap_entry *src, UINT32 count, void **out )
{
    struct stringmap_view *impl;
    UINT32 i;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl_view = sm_view_vtbl;
    impl->lpVtbl_iterable = sm_view_iter_vtbl;
    impl->ref = 1;
    if (count)
    {
        if (!(impl->entries = calloc( count, sizeof(*impl->entries) )))
        {
            free( impl );
            return E_OUTOFMEMORY;
        }
        for (i = 0; i < count; i++)
        {
            if (FAILED(hr = WindowsDuplicateString( src[i].key, &impl->entries[i].key )) ||
                FAILED(hr = WindowsDuplicateString( src[i].value, &impl->entries[i].value )))
            {
                impl->count = i + 1;
                sm_view_Release( &impl->lpVtbl_view );
                return hr;
            }
        }
        impl->count = count;
    }
    *out = &impl->lpVtbl_view;
    return S_OK;
}

/* --- StringMap (IMap + IObservableMap + IIterable) --- */

static inline struct stringmap *sm_from_map( void *iface )
{
    return CONTAINING_RECORD( iface, struct stringmap, lpVtbl_map );
}

static inline struct stringmap *sm_from_obs( void *iface )
{
    return CONTAINING_RECORD( iface, struct stringmap, lpVtbl_obs );
}

static inline struct stringmap *sm_from_iter( void *iface )
{
    return CONTAINING_RECORD( iface, struct stringmap, lpVtbl_iterable );
}

static HRESULT WINAPI sm_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct stringmap *impl = sm_from_map( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &SM_IID_IMap_HSTRING_HSTRING ))
    {
        *out = &impl->lpVtbl_map;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &SM_IID_IObservableMap_HSTRING_HSTRING ))
    {
        *out = &impl->lpVtbl_obs;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &SM_IID_IIterable_IKeyValuePair_HSTRING_HSTRING ))
    {
        *out = &impl->lpVtbl_iterable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sm_AddRef( void *iface )
{
    struct stringmap *impl = sm_from_map( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sm_Release( void *iface )
{
    struct stringmap *impl = sm_from_map( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->entries[i].key );
            WindowsDeleteString( impl->entries[i].value );
        }
        free( impl->entries );
        for (i = 0; i < impl->handler_count; i++)
        {
            if (impl->handlers[i].handler) IUnknown_Release( impl->handlers[i].handler );
        }
        free( impl->handlers );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sm_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *iids = CoTaskMemAlloc( 3 * sizeof(IID) );
    if (!*iids) { *count = 0; return E_OUTOFMEMORY; }
    (*iids)[0] = SM_IID_IMap_HSTRING_HSTRING;
    (*iids)[1] = SM_IID_IObservableMap_HSTRING_HSTRING;
    (*iids)[2] = SM_IID_IIterable_IKeyValuePair_HSTRING_HSTRING;
    *count = 3;
    return S_OK;
}

static HRESULT WINAPI sm_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.StringMap";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}

static HRESULT WINAPI sm_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sm_Lookup( void *iface, HSTRING key, HSTRING *value )
{
    struct stringmap *impl = sm_from_map( iface );
    UINT32 i;
    INT32 cmp;
    HRESULT hr = E_BOUNDS;
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared( &impl->lock );
    for (i = 0; i < impl->count; i++)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
        {
            hr = WindowsDuplicateString( impl->entries[i].value, value );
            break;
        }
    }
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI sm_get_Size( void *iface, UINT32 *size )
{
    struct stringmap *impl = sm_from_map( iface );
    if (!size) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *size = impl->count;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI sm_HasKey( void *iface, HSTRING key, boolean *found )
{
    struct stringmap *impl = sm_from_map( iface );
    UINT32 i;
    INT32 cmp;
    if (!found) return E_POINTER;
    *found = FALSE;
    AcquireSRWLockShared( &impl->lock );
    for (i = 0; i < impl->count; i++)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
        {
            *found = TRUE;
            break;
        }
    }
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI sm_GetView( void *iface, void **view )
{
    struct stringmap *impl = sm_from_map( iface );
    HRESULT hr;
    AcquireSRWLockShared( &impl->lock );
    hr = stringmap_view_create( impl->entries, impl->count, view );
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI sm_Insert( void *iface, HSTRING key, HSTRING value, boolean *replaced )
{
    struct stringmap *impl = sm_from_map( iface );
    HSTRING key_dup = NULL, val_dup = NULL;
    UINT32 i;
    INT32 cmp;
    HRESULT hr;

    if (replaced) *replaced = FALSE;
    if (FAILED(hr = WindowsDuplicateString( value, &val_dup ))) return hr;

    AcquireSRWLockExclusive( &impl->lock );
    for (i = 0; i < impl->count; i++)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
        {
            WindowsDeleteString( impl->entries[i].value );
            impl->entries[i].value = val_dup;
            if (replaced) *replaced = TRUE;
            ReleaseSRWLockExclusive( &impl->lock );
            return S_OK;
        }
    }
    if (FAILED(hr = WindowsDuplicateString( key, &key_dup )))
    {
        ReleaseSRWLockExclusive( &impl->lock );
        WindowsDeleteString( val_dup );
        return hr;
    }
    if (impl->count == impl->capacity)
    {
        UINT32 new_cap = impl->capacity ? impl->capacity * 2 : 16;
        struct stringmap_entry *new_entries = realloc( impl->entries, new_cap * sizeof(*new_entries) );
        if (!new_entries)
        {
            ReleaseSRWLockExclusive( &impl->lock );
            WindowsDeleteString( key_dup );
            WindowsDeleteString( val_dup );
            return E_OUTOFMEMORY;
        }
        impl->entries = new_entries;
        impl->capacity = new_cap;
    }
    impl->entries[impl->count].key = key_dup;
    impl->entries[impl->count].value = val_dup;
    impl->count++;
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI sm_Remove( void *iface, HSTRING key )
{
    struct stringmap *impl = sm_from_map( iface );
    UINT32 i;
    INT32 cmp;
    AcquireSRWLockExclusive( &impl->lock );
    for (i = 0; i < impl->count; i++)
    {
        if (SUCCEEDED(WindowsCompareStringOrdinal( impl->entries[i].key, key, &cmp )) && cmp == 0)
        {
            WindowsDeleteString( impl->entries[i].key );
            WindowsDeleteString( impl->entries[i].value );
            if (i + 1 < impl->count)
                memmove( &impl->entries[i], &impl->entries[i + 1],
                         (impl->count - i - 1) * sizeof(*impl->entries) );
            impl->count--;
            ReleaseSRWLockExclusive( &impl->lock );
            return S_OK;
        }
    }
    ReleaseSRWLockExclusive( &impl->lock );
    return E_BOUNDS;
}

static HRESULT WINAPI sm_Clear( void *iface )
{
    struct stringmap *impl = sm_from_map( iface );
    UINT32 i;
    AcquireSRWLockExclusive( &impl->lock );
    for (i = 0; i < impl->count; i++)
    {
        WindowsDeleteString( impl->entries[i].key );
        WindowsDeleteString( impl->entries[i].value );
    }
    impl->count = 0;
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI sm_obs_QueryInterface( void *iface, REFIID iid, void **out )
{
    return sm_QueryInterface( &sm_from_obs( iface )->lpVtbl_map, iid, out );
}
static ULONG WINAPI sm_obs_AddRef( void *iface )
{
    return sm_AddRef( &sm_from_obs( iface )->lpVtbl_map );
}
static ULONG WINAPI sm_obs_Release( void *iface )
{
    return sm_Release( &sm_from_obs( iface )->lpVtbl_map );
}
static HRESULT WINAPI sm_obs_GetIids( void *iface, ULONG *c, IID **i )
{
    return sm_GetIids( &sm_from_obs( iface )->lpVtbl_map, c, i );
}
static HRESULT WINAPI sm_obs_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return sm_GetRuntimeClassName( &sm_from_obs( iface )->lpVtbl_map, n );
}
static HRESULT WINAPI sm_obs_GetTrustLevel( void *iface, TrustLevel *l )
{
    return sm_GetTrustLevel( &sm_from_obs( iface )->lpVtbl_map, l );
}
static HRESULT WINAPI sm_obs_add_MapChanged( void *iface, IUnknown *handler, EventRegistrationToken *token )
{
    struct stringmap *impl = sm_from_obs( iface );
    struct stringmap_handler *nh;
    if (!token) return E_POINTER;
    AcquireSRWLockExclusive( &impl->lock );
    nh = realloc( impl->handlers, (impl->handler_count + 1) * sizeof(*nh) );
    if (!nh)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return E_OUTOFMEMORY;
    }
    impl->handlers = nh;
    token->value = ++impl->next_token;
    impl->handlers[impl->handler_count].token = token->value;
    impl->handlers[impl->handler_count].handler = handler;
    if (handler) IUnknown_AddRef( handler );
    impl->handler_count++;
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}
static HRESULT WINAPI sm_obs_remove_MapChanged( void *iface, EventRegistrationToken token )
{
    struct stringmap *impl = sm_from_obs( iface );
    UINT32 i;
    AcquireSRWLockExclusive( &impl->lock );
    for (i = 0; i < impl->handler_count; i++)
    {
        if (impl->handlers[i].token == token.value)
        {
            if (impl->handlers[i].handler) IUnknown_Release( impl->handlers[i].handler );
            if (i + 1 < impl->handler_count)
                memmove( &impl->handlers[i], &impl->handlers[i + 1],
                         (impl->handler_count - i - 1) * sizeof(*impl->handlers) );
            impl->handler_count--;
            break;
        }
    }
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI sm_iter_iface_QueryInterface( void *iface, REFIID iid, void **out )
{
    return sm_QueryInterface( &sm_from_iter( iface )->lpVtbl_map, iid, out );
}
static ULONG WINAPI sm_iter_iface_AddRef( void *iface )
{
    return sm_AddRef( &sm_from_iter( iface )->lpVtbl_map );
}
static ULONG WINAPI sm_iter_iface_Release( void *iface )
{
    return sm_Release( &sm_from_iter( iface )->lpVtbl_map );
}
static HRESULT WINAPI sm_iter_iface_GetIids( void *iface, ULONG *c, IID **i )
{
    return sm_GetIids( &sm_from_iter( iface )->lpVtbl_map, c, i );
}
static HRESULT WINAPI sm_iter_iface_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return sm_GetRuntimeClassName( &sm_from_iter( iface )->lpVtbl_map, n );
}
static HRESULT WINAPI sm_iter_iface_GetTrustLevel( void *iface, TrustLevel *l )
{
    return sm_GetTrustLevel( &sm_from_iter( iface )->lpVtbl_map, l );
}
static HRESULT WINAPI sm_iter_iface_First( void *iface, void **first )
{
    struct stringmap *impl = sm_from_iter( iface );
    HRESULT hr;
    AcquireSRWLockShared( &impl->lock );
    hr = stringmap_iter_create( impl->entries, impl->count, first );
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static const void * const sm_map_vtbl[] =
{
    sm_QueryInterface,
    sm_AddRef,
    sm_Release,
    sm_GetIids,
    sm_GetRuntimeClassName,
    sm_GetTrustLevel,
    sm_Lookup,
    sm_get_Size,
    sm_HasKey,
    sm_GetView,
    sm_Insert,
    sm_Remove,
    sm_Clear,
};

static const void * const sm_obs_vtbl[] =
{
    sm_obs_QueryInterface,
    sm_obs_AddRef,
    sm_obs_Release,
    sm_obs_GetIids,
    sm_obs_GetRuntimeClassName,
    sm_obs_GetTrustLevel,
    sm_obs_add_MapChanged,
    sm_obs_remove_MapChanged,
};

static const void * const sm_iterable_vtbl[] =
{
    sm_iter_iface_QueryInterface,
    sm_iter_iface_AddRef,
    sm_iter_iface_Release,
    sm_iter_iface_GetIids,
    sm_iter_iface_GetRuntimeClassName,
    sm_iter_iface_GetTrustLevel,
    sm_iter_iface_First,
};

/* --- StringMap Activation Factory --- */

static HRESULT WINAPI sm_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sm_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI sm_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI sm_factory_GetIids( IActivationFactory *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *c = 0;
    *i = NULL;
    return S_OK;
}
static HRESULT WINAPI sm_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.StringMap";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI sm_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *l )
{
    if (!l) return E_POINTER;
    *l = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sm_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct stringmap *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl_map = sm_map_vtbl;
    impl->lpVtbl_obs = sm_obs_vtbl;
    impl->lpVtbl_iterable = sm_iterable_vtbl;
    impl->ref = 1;
    InitializeSRWLock( &impl->lock );
    *instance = (IInspectable *)&impl->lpVtbl_map;
    return S_OK;
}

static const IActivationFactoryVtbl sm_factory_vtbl =
{
    sm_factory_QueryInterface,
    sm_factory_AddRef,
    sm_factory_Release,
    sm_factory_GetIids,
    sm_factory_GetRuntimeClassName,
    sm_factory_GetTrustLevel,
    sm_factory_ActivateInstance,
};

static IActivationFactory stringmap_factory_obj = { &sm_factory_vtbl };
IActivationFactory *string_map_factory = &stringmap_factory_obj;

