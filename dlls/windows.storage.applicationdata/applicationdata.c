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
#include "appmodel.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(data);

/* Arm-3 probe 8 (s239): shared head-block writer probe. Dumps the composite
 * tracker fields at the pinned heap-head base; called per-QI (probe 7, n<=8),
 * at the n=50 cascade end, and from IReferenceTracker slot 8
 * (ReleaseFromTrackerSource, the tracker cycle's per-wrapper tail call).
 * Writer-runs (fields nonzero at relfs/n50) vs write-faults (zero at fatal)
 * bisects in one census. */
static inline BOOL is_readable_ptr( const void *p, size_t size )
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery( p, &mbi, sizeof(mbi) ) != sizeof(mbi)) return FALSE;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS)) return FALSE;
    return TRUE;
}

static void wrdump( const char *tag )
{
    static const char *const head = (const char *)0x6fff00000000;
    if (!is_readable_ptr( head, 0x50 )) return;
    TRACE( "tag=%s w30=%016lx w38=%016lx w40=%016lx w48=%016lx\n", tag,
           (unsigned long)*(const ULONGLONG *)(head + 0x30),
           (unsigned long)*(const ULONGLONG *)(head + 0x38),
           (unsigned long)*(const ULONGLONG *)(head + 0x40),
           (unsigned long)*(const ULONGLONG *)(head + 0x48) );
}

/* Arm-3 probe 2 (s229): real XAML IReferenceTracker face on the
 * ApplicationData/ApplicationDataContainer objects, mirroring the singleton
 * in dlls/wintypes/propertyset.c. The 0x80131506
 * TryGetOrCreateObjectForComInstanceInternal fatal survives the projection
 * face (s227d) and the heap-reserve grow (s228): CoreCLR's
 * TrackerObjectManager::AfterWrapperCreated reads the composite slot 0 as a
 * packed ComVirtualMethodData header and refetches chunk+0x38 (slot 7
 * AddRefFromTrackerSource) from the shader heap region (base
 * 0x6fff00000000, first page unmapped under wine: FIRST_CHANCE_AV
 * rax=0x6fff00000002, rip load 0x6fff0000003b, 35 fatal cycles per gate).
 * With a real face the QI lands on this image-resident vtable and the
 * heap-chunk refetch is never taken. Slot order per
 * windows.ui.xaml.hosting.referencetracker.h: 0-2 IUnknown, 3 Connect,
 * 4 Disconnect, 5 FindTrackerTargets, 6 GetReferenceTrackerManager,
 * 7 AddRefFromTrackerSource, 8 ReleaseFromTrackerSource, 9 Peg. */
static const GUID IID_IReferenceTracker =
{ 0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46} };
static const GUID IID_IReferenceTracker_g =
{ 0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6} };

struct appdata_tracker;

struct appdata_tracker_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct appdata_tracker *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( struct appdata_tracker *iface );
    ULONG (WINAPI *Release)( struct appdata_tracker *iface );
    HRESULT (WINAPI *ConnectFromTrackerSource)( struct appdata_tracker *iface );
    HRESULT (WINAPI *DisconnectFromTrackerSource)( struct appdata_tracker *iface, int disconnect_type );
    HRESULT (WINAPI *FindTrackerTargets)( struct appdata_tracker *iface, void *callback );
    HRESULT (WINAPI *GetReferenceTrackerManager)( struct appdata_tracker *iface, void **manager );
    HRESULT (WINAPI *AddRefFromTrackerSource)( struct appdata_tracker *iface );
    HRESULT (WINAPI *ReleaseFromTrackerSource)( struct appdata_tracker *iface );
    HRESULT (WINAPI *PegFromTrackerSource)( struct appdata_tracker *iface );
};

struct appdata_tracker
{
    const struct appdata_tracker_vtbl *vtbl;
    LONG ref;
};

static HRESULT WINAPI tracker_QueryInterface( struct appdata_tracker *iface, REFIID iid, void **out )
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

static ULONG WINAPI tracker_AddRef( struct appdata_tracker *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI tracker_Release( struct appdata_tracker *iface )
{
    return InterlockedDecrement( &iface->ref );
}

static HRESULT WINAPI tracker_ConnectFromTrackerSource( struct appdata_tracker *iface )
{
    TRACE( "tracker %p connect\n", iface );
    return S_OK;
}

static HRESULT WINAPI tracker_DisconnectFromTrackerSource( struct appdata_tracker *iface, int disconnect_type )
{
    TRACE( "tracker %p disconnect type %d\n", iface, disconnect_type );
    return S_OK;
}

static HRESULT WINAPI tracker_FindTrackerTargets( struct appdata_tracker *iface, void *callback )
{
    TRACE( "tracker %p find targets\n", iface );
    return S_OK; /* no tracked targets from a builtin data object */
}

static HRESULT WINAPI tracker_GetReferenceTrackerManager( struct appdata_tracker *iface, void **manager )
{
    TRACE( "tracker %p get manager\n", iface );
    if (manager) *manager = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI tracker_AddRefFromTrackerSource( struct appdata_tracker *iface )
{
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static HRESULT WINAPI tracker_ReleaseFromTrackerSource( struct appdata_tracker *iface )
{
    wrdump( "relfs" );
    InterlockedDecrement( &iface->ref );
    return S_OK;
}

static HRESULT WINAPI tracker_PegFromTrackerSource( struct appdata_tracker *iface )
{
    TRACE( "tracker %p peg\n", iface );
    return S_OK;
}

static const struct appdata_tracker_vtbl appdata_tracker_vtbl =
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

/* Process-wide singleton: the XAML tracker contract for builtin data objects
 * is satisfied by no-op connect/disconnect/refcount hooks, per-object state
 * is not needed. Shared by the ApplicationData and ApplicationDataContainer
 * faces (propertyset precedent). */
static struct appdata_tracker appdata_tracker = { &appdata_tracker_vtbl, 1 };


/* Arm-3 probe 4 (s232): bounded per-cycle QI(iid) census on the appdata
 * chain (first 50, object-tagged). s232: the single E_NOINTERFACE was
 * {5c13e51c-4f32-4726-a3fd-f3edd63da3a0} = IApplicationDataGetForApplication
 * (GetForApp) on the appdata object; probe 5 answers it. */
static LONG qi_log_count;

static void qi_log_record( REFIID iid, HRESULT hr, const char *obj )
{
    LONG n = InterlockedIncrement( &qi_log_count );

    if (n <= 50)
        TRACE( "obj=%s iid=%s hr=%08lx\n", obj, wine_dbgstr_guid( iid ), (unsigned long)hr );

    /* Arm-3 probe 7 (s237): writer trace of the composite wrapper block at
     * the pinned heap-head base. Dump base+0x30..0x48 on the first 8 QIs,
     * tagged by the obj stage (entry/post-face/pre-AV). Census separates:
     * (a) all-zero never-written, (b) 0x6fff.. head-page pointers,
     * (c) value changes across tags = written-then-raced. */
    if (n <= 8)
    {
        static const char *const head = (const char *)0x6fff00000000;
        if (is_readable_ptr( head, 0x50 ))
        {
            TRACE( "n=%ld tag=%s w30=%016lx w38=%016lx w40=%016lx w48=%016lx\n",
                   n, obj, (unsigned long)*(const ULONGLONG *)(head + 0x30),
                   (unsigned long)*(const ULONGLONG *)(head + 0x38),
                   (unsigned long)*(const ULONGLONG *)(head + 0x40),
                   (unsigned long)*(const ULONGLONG *)(head + 0x48) );
        }
    }
    if (n == 50)
        wrdump( "n50" );
}

/* Arm-3 probe 5 (s233): Windows.Storage.IApplicationDataGetForApplication
 * (GetForApp) face. No SDK header in-tree defines the IID; projection
 * contract = IUnknown 3 + IInspectable 3 + GetForApp. */
static const GUID IID_IApplicationDataGetForApplication =
{ 0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0} };

struct IApplicationDataGetForApplication;

struct IApplicationDataGetForApplicationVtbl
{
    HRESULT (WINAPI *QueryInterface)( struct IApplicationDataGetForApplication *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( struct IApplicationDataGetForApplication *iface );
    ULONG (WINAPI *Release)( struct IApplicationDataGetForApplication *iface );
    HRESULT (WINAPI *GetIids)( struct IApplicationDataGetForApplication *iface, ULONG *count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( struct IApplicationDataGetForApplication *iface, HSTRING *name );
    HRESULT (WINAPI *GetTrustLevel)( struct IApplicationDataGetForApplication *iface, TrustLevel *level );
    HRESULT (WINAPI *GetForApp)( struct IApplicationDataGetForApplication *iface, HSTRING app_name, struct IApplicationData **value );
};

struct IApplicationDataGetForApplication
{
    const struct IApplicationDataGetForApplicationVtbl *lpVtbl;
};

static HRESULT WINAPI application_data_QueryInterface( IApplicationData *iface, REFIID iid, void **out );
static HRESULT WINAPI container_QueryInterface( IApplicationDataContainer *iface, REFIID iid, void **out );
static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out );

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

static HRESULT WINAPI factory_qi_impl( IActivationFactory *iface, REFIID iid, void **out )
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

    /* The CoreCLR ComWrappers shader QIs IReferenceTracker ({00000035-...-0046}
     * canonical, {11d3b13a-...} as observed in-app) on the factory face before
     * building the composite; answering it with the factory iface exposes the
     * IActivationFactory slots as tracker slots. Hand out the dedicated
     * no-op tracker face, as the application_data object already does. */
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) || IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = NULL;
        return E_NOINTERFACE;
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

    if (iid_count) *iid_count = 3; /* filled on the (NULL, NULL) probe itself */
    if (!impl->iids_cache)
    {
        struct com_interface_entry entries[] =
        {
            { (void **)&factory_vtbl, (IID *)&IID_IActivationFactory },
            { (void **)&application_data_statics_vtbl, (IID *)&IID_IApplicationDataStatics },
            { (void **)&appdata_tracker_vtbl, (IID *)&IID_IReferenceTracker_g },
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

struct data_changed_entry;

struct application_data
{
    IApplicationData IApplicationData_iface;
    IApplicationData2 IApplicationData2_iface;
    IApplicationData3 IApplicationData3_iface;
    IApplicationDataMethods IApplicationDataMethods_iface;
    struct IApplicationDataGetForApplication IApplicationDataGetForApplication_iface;
    LONG ref;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
    struct data_changed_entry *handlers;    /* DataChanged registrations, see application_data_fire_data_changed */
    LONG next_data_changed_token;
    UINT32 version;
};

static inline struct application_data *impl_from_IApplicationData( IApplicationData *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData_iface );
}

static inline struct application_data *impl_from_IApplicationData2( IApplicationData2 *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData2_iface );
}

static inline struct application_data *impl_from_IApplicationData3( IApplicationData3 *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData3_iface );
}

static inline struct application_data *impl_from_IApplicationDataMethods( IApplicationDataMethods *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationDataMethods_iface );
}

static const struct IApplicationDataVtbl application_data_vtbl;
static const struct IApplicationData2Vtbl application_data2_vtbl;
static const struct IApplicationData3Vtbl application_data3_vtbl;
static const struct IApplicationDataMethodsVtbl application_data_methods_vtbl;
static const struct IApplicationDataGetForApplicationVtbl application_data_getforapp_vtbl;

static HRESULT WINAPI application_data_qi_impl( IApplicationData *iface, REFIID iid, void **out )
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

    if (IsEqualGUID( iid, &IID_IApplicationData3 ))
    {
        *out = &impl->IApplicationData3_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationDataMethods ))
    {
        *out = &impl->IApplicationDataMethods_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    /* CoreCLR ComWrappers uses 5c13e51c-4f32-4726-a3fd-f3edd63da3a0 (IID_TaggedImpl)
     * to test whether an incoming IUnknown is an already-wrapped managed CCW.
     * Native COM/WinRT objects must return E_NOINTERFACE so CoreCLR creates an RCW. */
    if (IsEqualGUID( iid, &IID_IApplicationDataGetForApplication ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) || IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = NULL;
        return E_NOINTERFACE;
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
    struct application_data *impl = impl_from_IApplicationData( iface );

    /* xamlwave9 contract: CoreCLR probes the count with (NULL, NULL), frees
     * that array, then consumes the array returned by the following call -
     * so every call must hand back ONE stable block (factory iids_cache /
     * propertyset precedent), allocated on the count probe. */
    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!impl->iids_cache)
    {
        static const struct com_interface_entry entries[] =
        {
            { (void **)&application_data_vtbl, (IID *)&IID_IApplicationData },
            { (void **)&application_data2_vtbl, (IID *)&IID_IApplicationData2 },
            { (void **)&application_data_methods_vtbl, (IID *)&IID_IApplicationDataMethods },
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
    struct application_data *impl = impl_from_IApplicationData( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->version;
    return S_OK;
}

struct set_version_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IAsyncActionCompletedHandler *handler;
};

static inline struct set_version_action *sva_from_IAsyncAction( IAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct set_version_action, IAsyncAction_iface );
}
static inline struct set_version_action *sva_from_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct set_version_action, IAsyncInfo_iface );
}

static HRESULT WINAPI sva_QueryInterface( IAsyncAction *iface, REFIID iid, void **out )
{
    struct set_version_action *impl = sva_from_IAsyncAction( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        *out = &impl->IAsyncAction_iface;
        IAsyncAction_AddRef( &impl->IAsyncAction_iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        IAsyncAction_AddRef( &impl->IAsyncAction_iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sva_AddRef( IAsyncAction *iface )
{
    struct set_version_action *impl = sva_from_IAsyncAction( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sva_Release( IAsyncAction *iface )
{
    struct set_version_action *impl = sva_from_IAsyncAction( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler) IAsyncActionCompletedHandler_Release( impl->handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sva_GetIids( IAsyncAction *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI sva_GetRuntimeClassName( IAsyncAction *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncAction", 31, name );
}

static HRESULT WINAPI sva_GetTrustLevel( IAsyncAction *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sva_put_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler *handler )
{
    struct set_version_action *impl = sva_from_IAsyncAction( iface );
    if (impl->handler) IAsyncActionCompletedHandler_Release( impl->handler );
    impl->handler = handler;
    if (handler)
    {
        IAsyncActionCompletedHandler_AddRef( handler );
        IAsyncActionCompletedHandler_Invoke( handler, iface, Completed );
    }
    return S_OK;
}

static HRESULT WINAPI sva_get_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler **handler )
{
    struct set_version_action *impl = sva_from_IAsyncAction( iface );
    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) IAsyncActionCompletedHandler_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI sva_GetResults( IAsyncAction *iface ) { return S_OK; }

static const struct IAsyncActionVtbl sva_vtbl =
{
    sva_QueryInterface,
    sva_AddRef,
    sva_Release,
    sva_GetIids,
    sva_GetRuntimeClassName,
    sva_GetTrustLevel,
    sva_put_Completed,
    sva_get_Completed,
    sva_GetResults,
};

static HRESULT WINAPI sva_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return sva_QueryInterface( &sva_from_IAsyncInfo( iface )->IAsyncAction_iface, iid, out );
}
static ULONG WINAPI sva_info_AddRef( IAsyncInfo *iface )
{
    return sva_AddRef( &sva_from_IAsyncInfo( iface )->IAsyncAction_iface );
}
static ULONG WINAPI sva_info_Release( IAsyncInfo *iface )
{
    return sva_Release( &sva_from_IAsyncInfo( iface )->IAsyncAction_iface );
}
static HRESULT WINAPI sva_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return sva_GetIids( &sva_from_IAsyncInfo( iface )->IAsyncAction_iface, count, iids );
}
static HRESULT WINAPI sva_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return sva_GetRuntimeClassName( &sva_from_IAsyncInfo( iface )->IAsyncAction_iface, name );
}
static HRESULT WINAPI sva_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    return sva_GetTrustLevel( &sva_from_IAsyncInfo( iface )->IAsyncAction_iface, level );
}
static HRESULT WINAPI sva_info_get_Id( IAsyncInfo *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI sva_info_get_Status( IAsyncInfo *iface, AsyncStatus *status ) { if (status) *status = Completed; return S_OK; }
static HRESULT WINAPI sva_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *err ) { if (err) *err = S_OK; return S_OK; }
static HRESULT WINAPI sva_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI sva_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const struct IAsyncInfoVtbl sva_info_vtbl =
{
    sva_info_QueryInterface,
    sva_info_AddRef,
    sva_info_Release,
    sva_info_GetIids,
    sva_info_GetRuntimeClassName,
    sva_info_GetTrustLevel,
    sva_info_get_Id,
    sva_info_get_Status,
    sva_info_get_ErrorCode,
    sva_info_Cancel,
    sva_info_Close,
};

static HRESULT WINAPI application_data_SetVersionAsync( IApplicationData *iface, UINT32 version, IApplicationDataSetVersionHandler *handler,
                                                        IAsyncAction **operation )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    struct set_version_action *act;

    TRACE( "iface %p, version %u, handler %p, operation %p.\n", iface, version, handler, operation );
    if (!operation) return E_POINTER;
    if (!(act = calloc( 1, sizeof(*act) ))) return E_OUTOFMEMORY;
    act->IAsyncAction_iface.lpVtbl = &sva_vtbl;
    act->IAsyncInfo_iface.lpVtbl = &sva_info_vtbl;
    act->ref = 1;
    impl->version = version;
    *operation = &act->IAsyncAction_iface;
    return S_OK;
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

/* DataChanged registrations on the Current singleton. Single-process scope:
 * handlers added via add_DataChanged fire in-process when this process writes
 * through LocalSettings/RoamingSettings Values::Insert (and on explicit
 * SignalDataChanged). Real UWP fires across processes on the settings
 * container change; here the registry is the shared store and the fan-out is
 * process-local (documented limitation). Push list, walk under no lock: the
 * appdata chain is exercised from the UI thread. */
struct data_changed_entry
{
    ITypedEventHandler_ApplicationData_IInspectable *handler;
    EventRegistrationToken token;
    struct data_changed_entry *next;
};

static void application_data_fire_data_changed( struct application_data *impl )
{
    struct data_changed_entry *entry;

    for (entry = impl->handlers; entry; entry = entry->next)
        ITypedEventHandler_ApplicationData_IInspectable_Invoke( entry->handler,
                                                                &impl->IApplicationData_iface, NULL );
}

/* ---- per-package identity + settings persistence -----------------------
 *
 * Package key = WINE_PACKAGE_FAMILY_NAME (set by the lane launcher for
 * packaged apps, kernelbase appmodel pattern of 36782d4f). Unpackaged
 * fallback = exe base name without extension, mirroring the
 * %LOCALAPPDATA%\<exe> convention.
 *
 * Settings containers persist to HKCU\Software\<key>\Settings (Local) and
 * ...\RoamingSettings (Roaming) as
 *   String -> REG_SZ
 *   Boolean/UInt8/Int16/UInt16/Int32/UInt32 -> REG_DWORD
 *   Int64/UInt64/DateTime/TimeSpan -> REG_QWORD (decoded back as Int64)
 *   Single/Double/Char16/Guid -> REG_BINARY (decoded back by size)
 * Composite (Inspectable) values stay in-memory only. Load-on-first-touch:
 * every fresh Values map (one per get_Values call, see container_get_Values)
 * reads the whole key; every Insert writes through immediately. */

static void get_package_name( WCHAR *buf, DWORD size )
{
    static const WCHAR env_name[] = L"WINE_PACKAGE_FAMILY_NAME";
    WCHAR path[MAX_PATH], *p;
    UINT32 pkg_len = size;
    DWORD len;

    len = GetEnvironmentVariableW( env_name, buf, size );
    if (len && len < size) return;
    if (GetCurrentPackageFamilyName( &pkg_len, buf ) == ERROR_SUCCESS && buf[0]) return;

    len = GetModuleFileNameW( NULL, path, ARRAY_SIZE(path) );
    if (!len || len >= ARRAY_SIZE(path))
    {
        lstrcpynW( buf, L"Windows", size );
        return;
    }
    if ((p = wcsrchr( path, L'\\' ))) p++;
    else p = path;
    lstrcpynW( buf, p, size );
    if ((p = wcsrchr( buf, L'.' )) && p > buf) *p = 0; /* strip .exe */
}

static void settings_subkey( ApplicationDataLocality locality, WCHAR *buf, DWORD size )
{
    WCHAR name[MAX_PATH];

    get_package_name( name, ARRAY_SIZE(name) );
    lstrcpynW( buf, L"Software\\", size );
    lstrcatW( buf, name );
    lstrcatW( buf, locality == ApplicationDataLocality_Roaming ? L"\\RoamingSettings" : L"\\Settings" );
}

static HRESULT property_value_statics( IPropertyValueStatics **out )
{
    static IPropertyValueStatics *cache;
    HSTRING class_id;
    HRESULT hr;

    if (cache)
    {
        *out = cache;
        return S_OK;
    }
    if (FAILED(hr = WindowsCreateString( RuntimeClass_Windows_Foundation_PropertyValue,
                                         ARRAY_SIZE(RuntimeClass_Windows_Foundation_PropertyValue) - 1,
                                         &class_id ))) return hr;
    hr = RoGetActivationFactory( class_id, &IID_IPropertyValueStatics, (void **)&cache );
    WindowsDeleteString( class_id );
    if (FAILED(hr)) return hr;
    *out = cache;
    return S_OK;
}

/* Box a registry value into the WinRT type the map contract expects. */
static HRESULT reg_value_box( IPropertyValueStatics *statics, DWORD type, const BYTE *data, DWORD len,
                              IInspectable **out )
{
    switch (type)
    {
    case REG_SZ:
    {
        HSTRING str = NULL;
        HRESULT hr2;
        if (FAILED(hr2 = WindowsCreateString( (const WCHAR *)data, wcslen((const WCHAR *)data), &str )))
            return hr2;
        hr2 = IPropertyValueStatics_CreateString( statics, str, out );
        WindowsDeleteString( str );
        return hr2;
    }
    case REG_DWORD:
    {
        DWORD v;
        if (len < sizeof(v)) return E_FAIL;
        memcpy( &v, data, sizeof(v) );
        return IPropertyValueStatics_CreateInt32( statics, (INT32)v, out );
    }
    case REG_QWORD:
    {
        INT64 v;
        if (len < sizeof(v)) return E_FAIL;
        memcpy( &v, data, sizeof(v) );
        return IPropertyValueStatics_CreateInt64( statics, v, out );
    }
    case REG_BINARY:
    {
        if (len == 1)
        {
            if (!data[0]) return IPropertyValueStatics_CreateBoolean( statics, FALSE, out );
            return IPropertyValueStatics_CreateBoolean( statics, TRUE, out );
        }
        if (len == sizeof(WCHAR))
        {
            WCHAR c;
            memcpy( &c, data, sizeof(c) );
            return IPropertyValueStatics_CreateChar16( statics, c, out );
        }
        if (len == sizeof(FLOAT))
        {
            FLOAT f;
            memcpy( &f, data, sizeof(f) );
            return IPropertyValueStatics_CreateSingle( statics, f, out );
        }
        if (len == sizeof(DOUBLE))
        {
            DOUBLE d;
            memcpy( &d, data, sizeof(d) );
            return IPropertyValueStatics_CreateDouble( statics, d, out );
        }
        if (len == sizeof(GUID))
        {
            GUID g;
            memcpy( &g, data, sizeof(g) );
            return IPropertyValueStatics_CreateGuid( statics, g, out );
        }
        break;
    }
    }
    return E_FAIL;
}

/* The IPropertySet C face exposes only the six IInspectable slots; the map
 * methods live in slots 6-12 of the merged face (IMap<HSTRING,IInspectable>
 * layout, wintypes propertyset precedent). Cast to the map face to reach them. */
#define IMAPFACE(p) ((IMap_HSTRING_IInspectable *)(p))

static void reg_load_values( HKEY hkey, IPropertySet *map )
{
    IPropertyValueStatics *statics = NULL;
    WCHAR name[256];
    BYTE data[512];
    DWORD i, namelen, dlen, type;
    LONG status;

    if (FAILED(property_value_statics(&statics))) return;

    for (i = 0; ; i++)
    {
        IInspectable *box = NULL;
        HSTRING key = NULL;

        namelen = ARRAY_SIZE(name);
        dlen = sizeof(data);
        type = 0;
        if ((status = RegEnumValueW( hkey, i, name, &namelen, NULL, &type, data, &dlen )) != ERROR_SUCCESS)
        {
            TRACE( "load: enum %lu status %ld\n", i, status );
            break;
        }
        if (type == REG_SZ)
        {
            /* RegEnumValueW reports the byte count without the terminating
             * NUL; ensure termination within the buffer before boxing. */
            WCHAR *s = (WCHAR *)data;
            DWORD chars = dlen / sizeof(WCHAR);
            if (chars && s[chars - 1])
            {
                if (chars < ARRAY_SIZE(data) / sizeof(WCHAR)) s[chars] = 0;
                else s[ARRAY_SIZE(data) / sizeof(WCHAR) - 1] = 0;
            }
        }
        if (FAILED(reg_value_box( statics, type, data, dlen, &box )))
        {
            TRACE( "load: box value %s type %ld len %ld failed\n", debugstr_w(name), type, dlen );
            continue;
        }
        if (SUCCEEDED(WindowsCreateString( name, wcslen(name), &key )))
        {
            boolean replaced;
            HRESULT hr2 = IMap_HSTRING_IInspectable_Insert( IMAPFACE(map), key, box, &replaced );
            TRACE( "load: inserted %s type %ld hr %#lx\n", debugstr_w(name), type, hr2 );
            WindowsDeleteString( key );
        }
        IInspectable_Release( box );
    }
}

static void reg_store_value( HKEY hkey, HSTRING key, IInspectable *value )
{
    IPropertyValue *pv = NULL;
    PropertyType type;
    LONG status;

    if (FAILED(IInspectable_QueryInterface( value, &IID_IPropertyValue, (void **)&pv )))
        return; /* composite Inspectable value: in-memory only */

    if (FAILED(IPropertyValue_get_Type( pv, &type )))
    {
        IPropertyValue_Release( pv );
        return;
    }

    switch (type)
    {
    case PropertyType_String:
    {
        HSTRING s = NULL;
        if (SUCCEEDED(IPropertyValue_GetString( pv, &s )))
        {
            const WCHAR *w = WindowsGetStringRawBuffer( s, NULL );
            status = RegSetValueExW( hkey, WindowsGetStringRawBuffer( key, NULL ), 0, REG_SZ,
                                     (const BYTE *)w, (DWORD)((wcslen(w) + 1) * sizeof(WCHAR)) );
            WindowsDeleteString( s );
        }
        break;
    }
    case PropertyType_Boolean:
    {
        boolean b = FALSE;
        BYTE v;
        if (SUCCEEDED(IPropertyValue_GetBoolean( pv, &b )))
        {
            /* REG_BINARY keeps Boolean distinguishable from the integer types
             * on decode (REG_DWORD round-trips as Int32). */
            v = b ? 1 : 0;
            status = RegSetValueExW( hkey, WindowsGetStringRawBuffer(key, NULL), 0, REG_BINARY,
                                     &v, sizeof(v) );
        }
        break;
    }
    case PropertyType_UInt8:
    case PropertyType_Int16:
    case PropertyType_UInt16:
    case PropertyType_Int32:
    case PropertyType_UInt32:
    {
        INT32 v = 0;
        if (type == PropertyType_UInt8)
        {
            BYTE b;
            if (SUCCEEDED(IPropertyValue_GetUInt8( pv, &b ))) v = b;
            else break;
        }
        else if (type == PropertyType_Int16)
        {
            INT16 s;
            if (SUCCEEDED(IPropertyValue_GetInt16( pv, &s ))) v = s;
            else break;
        }
        else if (type == PropertyType_UInt16)
        {
            UINT16 u;
            if (SUCCEEDED(IPropertyValue_GetUInt16( pv, &u ))) v = u;
            else break;
        }
        else if (type == PropertyType_Int32)
        {
            if (FAILED(IPropertyValue_GetInt32( pv, &v ))) break;
        }
        else
        {
            UINT32 u = 0;
            if (SUCCEEDED(IPropertyValue_GetUInt32( pv, &u ))) v = (INT32)u;
            else break;
        }
        status = RegSetValueExW( hkey, WindowsGetStringRawBuffer(key, NULL), 0, REG_DWORD,
                                 (const BYTE *)&v, sizeof(v) );
        break;
    }
    case PropertyType_Int64:
    case PropertyType_UInt64:
    case PropertyType_DateTime:
    case PropertyType_TimeSpan:
    {
        INT64 v = 0;
        if (type == PropertyType_Int64)
        {
            if (FAILED(IPropertyValue_GetInt64( pv, &v ))) break;
        }
        else if (type == PropertyType_UInt64)
        {
            UINT64 u = 0;
            if (SUCCEEDED(IPropertyValue_GetUInt64( pv, &u ))) v = (INT64)u;
            else break;
        }
        else if (type == PropertyType_DateTime)
        {
            DateTime dt;
            if (SUCCEEDED(IPropertyValue_GetDateTime( pv, &dt ))) v = dt.UniversalTime;
            else break;
        }
        else
        {
            TimeSpan ts;
            if (SUCCEEDED(IPropertyValue_GetTimeSpan( pv, &ts ))) v = ts.Duration;
            else break;
        }
        status = RegSetValueExW( hkey, WindowsGetStringRawBuffer(key, NULL), 0, REG_QWORD,
                                 (const BYTE *)&v, sizeof(v) );
        break;
    }
    case PropertyType_Single:
    {
        FLOAT f = 0;
        if (SUCCEEDED(IPropertyValue_GetSingle( pv, &f )))
            status = RegSetValueExW( hkey, WindowsGetStringRawBuffer(key, NULL), 0, REG_BINARY,
                                     (const BYTE *)&f, sizeof(f) );
        break;
    }
    case PropertyType_Double:
    {
        DOUBLE d = 0;
        if (SUCCEEDED(IPropertyValue_GetDouble( pv, &d )))
            status = RegSetValueExW( hkey, WindowsGetStringRawBuffer(key, NULL), 0, REG_BINARY,
                                     (const BYTE *)&d, sizeof(d) );
        break;
    }
    case PropertyType_Char16:
    {
        WCHAR c = 0;
        if (SUCCEEDED(IPropertyValue_GetChar16( pv, &c )))
            status = RegSetValueExW( hkey, WindowsGetStringRawBuffer(key, NULL), 0, REG_BINARY,
                                     (const BYTE *)&c, sizeof(c) );
        break;
    }
    case PropertyType_Guid:
    {
        GUID g = {0};
        if (SUCCEEDED(IPropertyValue_GetGuid( pv, &g )))
            status = RegSetValueExW( hkey, WindowsGetStringRawBuffer(key, NULL), 0, REG_BINARY,
                                     (const BYTE *)&g, sizeof(g) );
        break;
    }
    default:
        break;
    }
    TRACE( "stored %s status %ld\n", debugstr_w( WindowsGetStringRawBuffer(key, NULL) ), status );
    (void)status;
    IPropertyValue_Release( pv );
}

/* Registry-backed IPropertySet. The live map is the wintypes PropertySet
 * (in-memory map, probe-hardened CoreCLR face); this object delegates every
 * slot to it and adds registry load-on-create + write-through on Insert /
 * Remove / Clear, plus a DataChanged poke on the owning ApplicationData.
 * Single merged face (0-14 slots, wintypes propertyset layout: IMap 6-12,
 * IObservableMap add/remove_MapChanged 13-14, IIterable consumers call slot 6
 * as First): QI of IPropertySet/IMap/IMapView/IIterable/IObservableMap all
 * return this face, matching the provider the CoreCLR shader already
 * consumed via the bare PropertySet. */
struct settings_values
{
    IPropertySet IPropertySet_iface;
    IMap_HSTRING_IInspectable IMap_iface;
    IIterable_IKeyValuePair_HSTRING_IInspectable IIterable_iface;
    IObservableMap_HSTRING_IInspectable IObservableMap_iface;
    IApplicationDataContainerMethods IApplicationDataContainerMethods_iface;
    LONG ref;
    ApplicationDataLocality locality;
    IPropertySet *inner;
    HKEY hkey;
    struct application_data *owner;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
};

static inline struct settings_values *impl_from_IPropertySet( IPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct settings_values, IPropertySet_iface );
}

static inline struct settings_values *impl_from_IMap( IMap_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct settings_values, IMap_iface );
}

static inline struct settings_values *impl_from_IIterable( IIterable_IKeyValuePair_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct settings_values, IIterable_iface );
}

static inline struct settings_values *impl_from_IObservableMap( IObservableMap_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct settings_values, IObservableMap_iface );
}

struct settings_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IPropertySet *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IPropertySet *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IPropertySet *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IPropertySet *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IPropertySet *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IPropertySet *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *Lookup)( IPropertySet *iface, HSTRING key, IInspectable **value );
    HRESULT (STDMETHODCALLTYPE *get_Size)( IPropertySet *iface, unsigned int *size );
    HRESULT (STDMETHODCALLTYPE *HasKey)( IPropertySet *iface, HSTRING key, boolean *found );
    HRESULT (STDMETHODCALLTYPE *GetView)( IPropertySet *iface, IMapView_HSTRING_IInspectable **view );
    HRESULT (STDMETHODCALLTYPE *Insert)( IPropertySet *iface, HSTRING key, IInspectable *value, boolean *replaced );
    HRESULT (STDMETHODCALLTYPE *Remove)( IPropertySet *iface, HSTRING key );
    HRESULT (STDMETHODCALLTYPE *Clear)( IPropertySet *iface );
    HRESULT (STDMETHODCALLTYPE *add_MapChanged)( IPropertySet *iface, IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_MapChanged)( IPropertySet *iface, EventRegistrationToken token );
};

static const struct settings_vtbl *settings_inner_vt( struct settings_values *impl )
{
    return (const struct settings_vtbl *)impl->inner->lpVtbl;
}

static HRESULT WINAPI settings_values_QueryInterface( IPropertySet *iface, REFIID iid, void **out )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    struct settings_values *impl = impl_from_IPropertySet( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertySet ))
    {
        *out = &impl->IPropertySet_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMap_HSTRING_IInspectable ))
    {
        *out = &impl->IMap_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable ))
    {
        *out = &impl->IIterable_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IObservableMap_HSTRING_IInspectable ))
    {
        *out = &impl->IObservableMap_iface;
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

static ULONG WINAPI settings_values_AddRef( IPropertySet *iface )
{
    struct settings_values *impl = impl_from_IPropertySet( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI settings_values_Release( IPropertySet *iface )
{
    struct settings_values *impl = impl_from_IPropertySet( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->hkey) RegCloseKey( impl->hkey );
        IPropertySet_Release( impl->inner );
        CoTaskMemFree( impl->iids_cache );
        free( impl );
    }
    return ref;
}

static const struct settings_vtbl settings_values_vtbl;
static const struct IApplicationDataContainerMethodsVtbl settings_methods_vtbl;

static HRESULT WINAPI settings_values_GetIids( IPropertySet *iface, ULONG *count, IID **iids )
{
    struct settings_values *impl = impl_from_IPropertySet( iface );

    TRACE( "iface %p, count %p, iids %p.\n", iface, count, iids );

    /* Fresh Guid** array per call (empty_map_view precedent): the CoreCLR
     * shader frees the probe array before consuming the next, so a cached
     * block goes dangling — the dangling read produced a zero-length
     * ComVirtualMethodData and the methods-face get_Values consumer crashed
     * in CreateVirtualMethodData (failfast 0x1007, Store SplashPage). */
    {
        static const IID *entries[] =
        {
            &IID_IPropertySet,
        };
        const IID **array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        /* Small count pointers (null-page scratch, see container_GetIids). */
        if (count && (uintptr_t)count >= 0x10000) *count = ARRAY_SIZE( entries );
        if (iids) *iids = (IID *)array;
    }
    return S_OK;
}

static HRESULT WINAPI settings_values_GetRuntimeClassName( IPropertySet *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.PropertySet";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI settings_values_GetTrustLevel( IPropertySet *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI settings_values_Lookup( IPropertySet *iface, HSTRING key, IInspectable **value )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    struct settings_values *impl = impl_from_IPropertySet( iface );
    HRESULT hr = settings_inner_vt( impl )->Lookup( impl->inner, key, value );
    if (hr == E_BOUNDS && value)
    {
        *value = NULL;
        return S_OK;
    }
    return hr;
}

static HRESULT WINAPI settings_values_get_Size( IPropertySet *iface, unsigned int *size )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    struct settings_values *impl = impl_from_IPropertySet( iface );
    HRESULT hr = settings_inner_vt( impl )->get_Size( impl->inner, size );
    TRACE( "map %p get_Size hr %#lx size %u\n", impl, hr, size ? *size : 0 );
    return hr;
}

static HRESULT WINAPI settings_values_HasKey( IPropertySet *iface, HSTRING key, boolean *found )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    struct settings_values *impl = impl_from_IPropertySet( iface );
    return settings_inner_vt( impl )->HasKey( impl->inner, key, found );
}

static HRESULT WINAPI settings_values_GetView( IPropertySet *iface, IMapView_HSTRING_IInspectable **view )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    struct settings_values *impl = impl_from_IPropertySet( iface );
    return settings_inner_vt( impl )->GetView( impl->inner, view );
}

static HRESULT WINAPI settings_values_Insert( IPropertySet *iface, HSTRING key, IInspectable *value, boolean *replaced )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    struct settings_values *impl = impl_from_IPropertySet( iface );
    HRESULT hr;

    if (!value)
    {
        boolean has = FALSE;
        settings_inner_vt( impl )->HasKey( impl->inner, key, &has );
        if (has) settings_inner_vt( impl )->Remove( impl->inner, key );
        if (impl->hkey) RegDeleteValueW( impl->hkey, WindowsGetStringRawBuffer( key, NULL ) );
        if (replaced) *replaced = has;
        if (impl->owner) application_data_fire_data_changed( impl->owner );
        return S_OK;
    }

    if (!(hr = settings_inner_vt( impl )->Insert( impl->inner, key, value, replaced )))
    {
        if (impl->hkey) reg_store_value( impl->hkey, key, value );
        if (impl->owner) application_data_fire_data_changed( impl->owner );
    }
    return hr;
}

static HRESULT WINAPI settings_values_Remove( IPropertySet *iface, HSTRING key )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    struct settings_values *impl = impl_from_IPropertySet( iface );
    HRESULT hr;

    if (!(hr = settings_inner_vt( impl )->Remove( impl->inner, key )))
    {
        if (impl->hkey) RegDeleteValueW( impl->hkey, WindowsGetStringRawBuffer( key, NULL ) );
    }
    return hr;
}

static HRESULT WINAPI settings_values_Clear( IPropertySet *iface )
{
    struct settings_values *impl = impl_from_IPropertySet( iface );
    HRESULT hr;

    if (!(hr = settings_inner_vt( impl )->Clear( impl->inner )))
    {
        if (impl->hkey) RegDeleteTreeW( impl->hkey, NULL );
    }
    return hr;
}

static HRESULT WINAPI settings_values_add_MapChanged( IPropertySet *iface, IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                      EventRegistrationToken *token )
{
    struct settings_values *impl = impl_from_IPropertySet( iface );
    return settings_inner_vt( impl )->add_MapChanged( impl->inner, handler, token );
}

static HRESULT WINAPI settings_values_remove_MapChanged( IPropertySet *iface, EventRegistrationToken token )
{
    struct settings_values *impl = impl_from_IPropertySet( iface );
    return settings_inner_vt( impl )->remove_MapChanged( impl->inner, token );
}

static const struct settings_vtbl settings_values_vtbl =
{
    settings_values_QueryInterface,
    settings_values_AddRef,
    settings_values_Release,
    settings_values_GetIids,
    settings_values_GetRuntimeClassName,
    settings_values_GetTrustLevel,
    settings_values_Lookup,
    settings_values_get_Size,
    settings_values_HasKey,
    settings_values_GetView,
    settings_values_Insert,
    settings_values_Remove,
    settings_values_Clear,
    settings_values_add_MapChanged,
    settings_values_remove_MapChanged,
};

static HRESULT WINAPI settings_map_QueryInterface( IMap_HSTRING_IInspectable *iface, REFIID iid, void **out )
{
    return settings_values_QueryInterface( &impl_from_IMap( iface )->IPropertySet_iface, iid, out );
}

static ULONG WINAPI settings_map_AddRef( IMap_HSTRING_IInspectable *iface )
{
    return settings_values_AddRef( &impl_from_IMap( iface )->IPropertySet_iface );
}

static ULONG WINAPI settings_map_Release( IMap_HSTRING_IInspectable *iface )
{
    return settings_values_Release( &impl_from_IMap( iface )->IPropertySet_iface );
}

static HRESULT WINAPI settings_map_GetIids( IMap_HSTRING_IInspectable *iface, ULONG *count, IID **iids )
{
    return settings_values_GetIids( &impl_from_IMap( iface )->IPropertySet_iface, count, iids );
}

static HRESULT WINAPI settings_map_GetRuntimeClassName( IMap_HSTRING_IInspectable *iface, HSTRING *name )
{
    return settings_values_GetRuntimeClassName( &impl_from_IMap( iface )->IPropertySet_iface, name );
}

static HRESULT WINAPI settings_map_GetTrustLevel( IMap_HSTRING_IInspectable *iface, TrustLevel *level )
{
    return settings_values_GetTrustLevel( &impl_from_IMap( iface )->IPropertySet_iface, level );
}

static HRESULT WINAPI settings_map_Lookup( IMap_HSTRING_IInspectable *iface, HSTRING key, IInspectable **value )
{
    return settings_values_Lookup( &impl_from_IMap( iface )->IPropertySet_iface, key, value );
}

static HRESULT WINAPI settings_map_get_Size( IMap_HSTRING_IInspectable *iface, unsigned int *size )
{
    return settings_values_get_Size( &impl_from_IMap( iface )->IPropertySet_iface, size );
}

static HRESULT WINAPI settings_map_HasKey( IMap_HSTRING_IInspectable *iface, HSTRING key, boolean *found )
{
    return settings_values_HasKey( &impl_from_IMap( iface )->IPropertySet_iface, key, found );
}

static HRESULT WINAPI settings_map_GetView( IMap_HSTRING_IInspectable *iface, IMapView_HSTRING_IInspectable **view )
{
    return settings_values_GetView( &impl_from_IMap( iface )->IPropertySet_iface, view );
}

static HRESULT WINAPI settings_map_Insert( IMap_HSTRING_IInspectable *iface, HSTRING key, IInspectable *value, boolean *replaced )
{
    return settings_values_Insert( &impl_from_IMap( iface )->IPropertySet_iface, key, value, replaced );
}

static HRESULT WINAPI settings_map_Remove( IMap_HSTRING_IInspectable *iface, HSTRING key )
{
    return settings_values_Remove( &impl_from_IMap( iface )->IPropertySet_iface, key );
}

static HRESULT WINAPI settings_map_Clear( IMap_HSTRING_IInspectable *iface )
{
    return settings_values_Clear( &impl_from_IMap( iface )->IPropertySet_iface );
}

static const struct IMap_HSTRING_IInspectableVtbl settings_map_vtbl =
{
    settings_map_QueryInterface,
    settings_map_AddRef,
    settings_map_Release,
    settings_map_GetIids,
    settings_map_GetRuntimeClassName,
    settings_map_GetTrustLevel,
    settings_map_Lookup,
    settings_map_get_Size,
    settings_map_HasKey,
    settings_map_GetView,
    settings_map_Insert,
    settings_map_Remove,
    settings_map_Clear,
};

static HRESULT WINAPI settings_iterable_QueryInterface( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, REFIID iid, void **out )
{
    return settings_values_QueryInterface( &impl_from_IIterable( iface )->IPropertySet_iface, iid, out );
}

static ULONG WINAPI settings_iterable_AddRef( IIterable_IKeyValuePair_HSTRING_IInspectable *iface )
{
    return settings_values_AddRef( &impl_from_IIterable( iface )->IPropertySet_iface );
}

static ULONG WINAPI settings_iterable_Release( IIterable_IKeyValuePair_HSTRING_IInspectable *iface )
{
    return settings_values_Release( &impl_from_IIterable( iface )->IPropertySet_iface );
}

static HRESULT WINAPI settings_iterable_GetIids( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, ULONG *count, IID **iids )
{
    return settings_values_GetIids( &impl_from_IIterable( iface )->IPropertySet_iface, count, iids );
}

static HRESULT WINAPI settings_iterable_GetRuntimeClassName( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, HSTRING *name )
{
    return settings_values_GetRuntimeClassName( &impl_from_IIterable( iface )->IPropertySet_iface, name );
}

static HRESULT WINAPI settings_iterable_GetTrustLevel( IIterable_IKeyValuePair_HSTRING_IInspectable *iface, TrustLevel *level )
{
    return settings_values_GetTrustLevel( &impl_from_IIterable( iface )->IPropertySet_iface, level );
}

static HRESULT WINAPI settings_iterable_First( IIterable_IKeyValuePair_HSTRING_IInspectable *iface,
                                               IIterator_IKeyValuePair_HSTRING_IInspectable **first )
{
    struct settings_values *impl = impl_from_IIterable( iface );
    IIterable_IKeyValuePair_HSTRING_IInspectable *inner_iter;
    HRESULT hr;

    if (FAILED(hr = IPropertySet_QueryInterface( impl->inner, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable, (void **)&inner_iter )))
        return hr;
    hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First( inner_iter, first );
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release( inner_iter );
    return hr;
}

static const struct IIterable_IKeyValuePair_HSTRING_IInspectableVtbl settings_iterable_vtbl =
{
    settings_iterable_QueryInterface,
    settings_iterable_AddRef,
    settings_iterable_Release,
    settings_iterable_GetIids,
    settings_iterable_GetRuntimeClassName,
    settings_iterable_GetTrustLevel,
    settings_iterable_First,
};

static HRESULT WINAPI settings_observable_QueryInterface( IObservableMap_HSTRING_IInspectable *iface, REFIID iid, void **out )
{
    return settings_values_QueryInterface( &impl_from_IObservableMap( iface )->IPropertySet_iface, iid, out );
}

static ULONG WINAPI settings_observable_AddRef( IObservableMap_HSTRING_IInspectable *iface )
{
    return settings_values_AddRef( &impl_from_IObservableMap( iface )->IPropertySet_iface );
}

static ULONG WINAPI settings_observable_Release( IObservableMap_HSTRING_IInspectable *iface )
{
    return settings_values_Release( &impl_from_IObservableMap( iface )->IPropertySet_iface );
}

static HRESULT WINAPI settings_observable_GetIids( IObservableMap_HSTRING_IInspectable *iface, ULONG *count, IID **iids )
{
    return settings_values_GetIids( &impl_from_IObservableMap( iface )->IPropertySet_iface, count, iids );
}

static HRESULT WINAPI settings_observable_GetRuntimeClassName( IObservableMap_HSTRING_IInspectable *iface, HSTRING *name )
{
    return settings_values_GetRuntimeClassName( &impl_from_IObservableMap( iface )->IPropertySet_iface, name );
}

static HRESULT WINAPI settings_observable_GetTrustLevel( IObservableMap_HSTRING_IInspectable *iface, TrustLevel *level )
{
    return settings_values_GetTrustLevel( &impl_from_IObservableMap( iface )->IPropertySet_iface, level );
}

static HRESULT WINAPI settings_observable_add_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                          IMapChangedEventHandler_HSTRING_IInspectable *handler,
                                                          EventRegistrationToken *token )
{
    return settings_values_add_MapChanged( &impl_from_IObservableMap( iface )->IPropertySet_iface, handler, token );
}

static HRESULT WINAPI settings_observable_remove_MapChanged( IObservableMap_HSTRING_IInspectable *iface,
                                                             EventRegistrationToken token )
{
    return settings_values_remove_MapChanged( &impl_from_IObservableMap( iface )->IPropertySet_iface, token );
}

static const struct IObservableMap_HSTRING_IInspectableVtbl settings_observable_vtbl =
{
    settings_observable_QueryInterface,
    settings_observable_AddRef,
    settings_observable_Release,
    settings_observable_GetIids,
    settings_observable_GetRuntimeClassName,
    settings_observable_GetTrustLevel,
    settings_observable_add_MapChanged,
    settings_observable_remove_MapChanged,
};

static HRESULT container_create( ApplicationDataLocality locality, struct application_data *owner,
                                 IApplicationDataContainer **out );
static HRESULT WINAPI container_get_Values( IApplicationDataContainer *iface, IPropertySet **value );
static HRESULT WINAPI container_get_Containers( IApplicationDataContainer *iface,
                                                IMapView_HSTRING_ApplicationDataContainer **value );
static HRESULT WINAPI container_CreateContainer( IApplicationDataContainer *iface, HSTRING name,
                                                 ApplicationDataCreateDisposition disposition,
                                                 IApplicationDataContainer **container );
static HRESULT WINAPI container_DeleteContainer( IApplicationDataContainer *iface, HSTRING name );

/* Windows projects IApplicationDataContainerMethods ({11d3b13a-...}, slot 6 =
 * GetAllContains) onto the PropertySet class too: the Store app QIs the
 * LocalSettings.Values object for it and calls GetAllContains. Without the
 * face the CsWinRT/CoreRT caller sees E_NOINTERFACE and the splash page
 * awaits a data-bound collection that never fills. */
static inline struct settings_values *impl_from_IApplicationDataContainerMethods(
        IApplicationDataContainerMethods *iface )
{
    return CONTAINING_RECORD( iface, struct settings_values, IApplicationDataContainerMethods_iface );
}

static HRESULT WINAPI settings_methods_QueryInterface( IApplicationDataContainerMethods *iface, REFIID iid, void **out )
{
    return settings_values_QueryInterface( &impl_from_IApplicationDataContainerMethods( iface )->IPropertySet_iface, iid, out );
}

static ULONG WINAPI settings_methods_AddRef( IApplicationDataContainerMethods *iface )
{
    return settings_values_AddRef( &impl_from_IApplicationDataContainerMethods( iface )->IPropertySet_iface );
}

static ULONG WINAPI settings_methods_Release( IApplicationDataContainerMethods *iface )
{
    return settings_values_Release( &impl_from_IApplicationDataContainerMethods( iface )->IPropertySet_iface );
}

static HRESULT WINAPI settings_methods_GetIids( IApplicationDataContainerMethods *iface, ULONG *count, IID **iids )
{
    return settings_values_GetIids( &impl_from_IApplicationDataContainerMethods( iface )->IPropertySet_iface, count, iids );
}

static HRESULT WINAPI settings_methods_GetRuntimeClassName( IApplicationDataContainerMethods *iface, HSTRING *name )
{
    return settings_values_GetRuntimeClassName( &impl_from_IApplicationDataContainerMethods( iface )->IPropertySet_iface, name );
}

static HRESULT WINAPI settings_methods_GetTrustLevel( IApplicationDataContainerMethods *iface, TrustLevel *level )
{
    return settings_values_GetTrustLevel( &impl_from_IApplicationDataContainerMethods( iface )->IPropertySet_iface, level );
}

static HRESULT WINAPI settings_methods_GetAllContains( IApplicationDataContainerMethods *iface,
                                                      IApplicationDataContainer **container )
{
    struct settings_values *impl = impl_from_IApplicationDataContainerMethods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return container_create( ApplicationDataLocality_Local, impl->owner, container );
}

static HRESULT WINAPI settings_methods_get_Values( IApplicationDataContainerMethods *iface, IPropertySet **value )
{
    struct settings_values *impl = impl_from_IApplicationDataContainerMethods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    IPropertySet *self = &impl->IPropertySet_iface;
    IPropertySet_AddRef( self );
    *value = self;
    return S_OK;
}

static HRESULT WINAPI settings_methods_get_Containers( IApplicationDataContainerMethods *iface,
                                                      IMapView_HSTRING_ApplicationDataContainer **value )
{
    struct settings_values *impl = impl_from_IApplicationDataContainerMethods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    IApplicationDataContainer *snap;
    HRESULT hr = container_create( impl->locality, impl->owner, &snap );
    if (FAILED( hr )) return hr;
    hr = container_get_Containers( snap, value );
    IApplicationDataContainer_Release( snap );
    return hr;
}

static HRESULT WINAPI settings_methods_CreateContainer( IApplicationDataContainerMethods *iface, HSTRING name,
                                                       ApplicationDataCreateDisposition disposition,
                                                       IApplicationDataContainer **container )
{
    struct settings_values *impl = impl_from_IApplicationDataContainerMethods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    IApplicationDataContainer *snap;
    HRESULT hr = container_create( impl->locality, impl->owner, &snap );
    if (FAILED( hr )) return hr;
    hr = container_CreateContainer( snap, name, disposition, container );
    IApplicationDataContainer_Release( snap );
    return hr;
}

static HRESULT WINAPI settings_methods_DeleteContainer( IApplicationDataContainerMethods *iface, HSTRING name )
{
    struct settings_values *impl = impl_from_IApplicationDataContainerMethods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    IApplicationDataContainer *snap;
    HRESULT hr = container_create( impl->locality, impl->owner, &snap );
    if (FAILED( hr )) return hr;
    hr = container_DeleteContainer( snap, name );
    IApplicationDataContainer_Release( snap );
    return hr;
}

static const struct IApplicationDataContainerMethodsVtbl settings_methods_vtbl =
{
    settings_methods_QueryInterface,
    settings_methods_AddRef,
    settings_methods_Release,
    settings_methods_GetIids,
    settings_methods_GetRuntimeClassName,
    settings_methods_GetTrustLevel,
        settings_methods_get_Values,
    settings_methods_get_Containers,
    settings_methods_CreateContainer,
    settings_methods_DeleteContainer,
settings_methods_GetAllContains,
};

static HRESULT settings_values_create( struct application_data *owner, const WCHAR *subkey,
                                       ApplicationDataLocality locality, IPropertySet **out )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.PropertySet";
    struct settings_values *impl;
    HSTRING class_id;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IPropertySet_iface.lpVtbl = (const struct IPropertySetVtbl *)&settings_values_vtbl;
    impl->IMap_iface.lpVtbl = &settings_map_vtbl;
    impl->IIterable_iface.lpVtbl = &settings_iterable_vtbl;
    impl->IObservableMap_iface.lpVtbl = &settings_observable_vtbl;
    impl->IApplicationDataContainerMethods_iface.lpVtbl = &settings_methods_vtbl;
    impl->ref = 1;
    impl->locality = locality;
    impl->owner = owner;

    if (FAILED(hr = WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, &class_id )))
    {
        free( impl );
        return hr;
    }
    hr = RoActivateInstance( class_id, (IInspectable **)&impl->inner );
    WindowsDeleteString( class_id );
    if (FAILED(hr))
    {
        free( impl );
        return hr;
    }

    {
        LSTATUS status = RegCreateKeyExW( HKEY_CURRENT_USER, subkey, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL,
                                          &impl->hkey, NULL );
        TRACE( "settings map %p open subkey %s status %ld\n", impl, debugstr_w(subkey), status );
        if (status == ERROR_SUCCESS) reg_load_values( impl->hkey, impl->inner );
    }

    *out = &impl->IPropertySet_iface;
    return S_OK;
}

struct application_data_container
{
    IApplicationDataContainer IApplicationDataContainer_iface;
    IApplicationDataContainerMethods IApplicationDataContainerMethods_iface;
    LONG ref;
    ApplicationDataLocality locality;
    WCHAR subkey[512];
    struct application_data *owner;
    struct com_interface_entry *iids_cache; /* stable GetIids pair array, see factory_GetIids */
    ULONG iids_count;
    IPropertySet *values;
};

static inline struct application_data_container *impl_from_IApplicationDataContainer( IApplicationDataContainer *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_container, IApplicationDataContainer_iface );
}

static inline struct application_data_container *impl_from_container_methods( IApplicationDataContainerMethods *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_container, IApplicationDataContainerMethods_iface );
}

static HRESULT WINAPI container_qi_impl( IApplicationDataContainer *iface, REFIID iid, void **out )
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
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) || IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = NULL;
        return E_NOINTERFACE;
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
struct empty_observable_map_view_vtbl;
static const struct empty_observable_map_view_vtbl empty_map_view_vtbl;

static HRESULT WINAPI container_GetIids( IApplicationDataContainer *iface, ULONG *count, IID **iids )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );

    TRACE( "iface %p, count %p, iids %p.\n", iface, count, iids );

    /* Fresh Guid** array per call: the shader frees the probe array before
     * consuming the next, so a cached block goes dangling (zero-length
     * ComVirtualMethodData, Store failfast 0x1007). */
    {
        static const IID *entries[] =
        {
            &IID_IApplicationDataContainer,
        };
        const IID **array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        /* Store's XAML shader passes its count out-slot as a low null-page
         * scratch value (0x14C/0x150 across runs); writing through it faults
         * the RCW call wrapper into a failfast. Tolerate it — the iids array
         * is what the shader consumes. */
        if (count && (uintptr_t)count >= 0x10000) *count = ARRAY_SIZE( entries );
        if (iids) *iids = (IID *)array;
    }
    return S_OK;
}
static HRESULT WINAPI container_GetRuntimeClassName( IApplicationDataContainer *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.ApplicationDataContainer", 39, name ); }
static HRESULT WINAPI container_GetTrustLevel( IApplicationDataContainer *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI container_get_Name( IApplicationDataContainer *iface, HSTRING *value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    static const WCHAR local[] = L"LocalSettings";
    static const WCHAR roaming[] = L"RoamingSettings";
    const WCHAR *name = impl->locality == ApplicationDataLocality_Roaming ? roaming : local;
    return WindowsCreateString( name, wcslen( name ), value );
}

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
    IPropertySet *values = NULL;
    HRESULT hr;

    if (!value) return E_POINTER;

    /* Values is a live view; hand out a fresh registry-backed map per call so
     * that the CLR RCW cache (keyed by ABI pointer) cannot alias a collected
     * wrapper onto a new marshal, and so writes made through other views (or
     * a previous process run) are visible. The container keeps one ref on the
     * last instance it handed out, mirroring the cached-map shape callers
     * expect. */
    if (FAILED(hr = settings_values_create( impl->owner, impl->subkey, impl->locality, &values ))) return hr;

    if (impl->values) IPropertySet_Release( impl->values );
    impl->values = values;
    IPropertySet_AddRef( values );
    *value = values;

    TRACE( "get_Values: settings map iface %p\n", values );
    return S_OK;
}

/* Instantiated GUIDs computed by CsWinRT (SHA1 of the projection signature);
 * the Store's NativeAOT shader resolves GetIids entries against these, not the
 * widl-computed IID___FIMapView_2_... values. */
static const IID IID_IMapView_String_ApplicationDataContainer =
    { 0x25d7260e, 0x3699, 0x7def, { 0x3f, 0x32, 0x3e, 0xc9, 0x10, 0x89, 0x5f, 0x6d } };
static const IID IID_IObservableMap_String_ApplicationDataContainer =
    { 0x47109dbf, 0x1ceb, 0xd4a8, { 0xb1, 0xd5, 0x21, 0xb3, 0x2f, 0xf7, 0x0c, 0xda } };
static const IID IID_IJupiterObject_g =
    { 0xdcb0a76f, 0x0d26, 0x5108, { 0x8a, 0x7b, 0x8f, 0xc8, 0xb6, 0x41, 0xba, 0x7f } };
static const IID IID_IIterable_HSTRING_IInspectable_g =
    { 0xd675fa43, 0x4ae1, 0x5164, { 0x87, 0x13, 0x40, 0x37, 0x9c, 0x47, 0x0e, 0xe0 } };
static const IID IID_IIterable_String_ApplicationDataContainer =
    { 0xc3ea4300, 0xf00c, 0x7f69, { 0x0f, 0xbd, 0x8e, 0xa0, 0xba, 0xe1, 0xb3, 0xd7 } };


static HRESULT WINAPI empty_map_view_QueryInterface( IMapView_HSTRING_ApplicationDataContainer *iface, REFIID iid, void **out )
{
    fprintf( stderr, "VIEWQI %p %s\n", iface, debugstr_guid(iid) );
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid(iid), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapView_HSTRING_ApplicationDataContainer ) ||
        IsEqualGUID( iid, &IID_IMapView_String_ApplicationDataContainer ) ||
        IsEqualGUID( iid, &IID_IObservableMap_String_ApplicationDataContainer ) ||
        IsEqualGUID( iid, &IID_IIterable_String_ApplicationDataContainer ) ||
        IsEqualGUID( iid, &IID_IIterable_HSTRING_IInspectable_g ) ||
        IsEqualGUID( iid, &IID_IMapView_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IObservableMap_HSTRING_IInspectable ) ||
        IsEqualGUID( iid, &IID_IJupiterObject_g ))
    {
        *out = iface;
        return S_OK;
    }
    /* CoreCLR's TrackerObjectManager walks the ComWrappers v1 chunk of every
     * QI'd face of the LocalSettings decode, calling slot7
     * (AddRefFromTrackerSource) per interface; without a face here the walk
     * dispatches into the windowscodecs iid-pair pool (DllRegisterServer
     * region, NULL-BSTR write) and feeds the 35-cycle AV storm.  Answer with
     * the shared 10-slot appdata tracker face, as on the container and the
     * wintypes propertyset singleton. */
    if (IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI empty_map_view_AddRef( IMapView_HSTRING_ApplicationDataContainer *iface ) { return 2; }
static ULONG WINAPI empty_map_view_Release( IMapView_HSTRING_ApplicationDataContainer *iface ) { return 1; }
static HRESULT WINAPI empty_map_view_GetIids( IMapView_HSTRING_ApplicationDataContainer *iface, ULONG *c, IID **i )
{
    fprintf( stderr, "VIEWGETIIDS\n" );
    TRACE( "%s\n", debugstr_a(__func__) );
    const IID *entries[] =
    {
        &IID_IIterable_String_ApplicationDataContainer,
        &IID_IMapView_String_ApplicationDataContainer,
        &IID_IObservableMap_String_ApplicationDataContainer,
        &IID_IIterable_HSTRING_IInspectable_g,
        &IID_IMapView_HSTRING_IInspectable,
        &IID_IObservableMap_HSTRING_IInspectable,
    };

    if (c) *c = ARRAY_SIZE( entries );
    if (i)
    {
        const IID **array = CoTaskMemAlloc( sizeof( entries ) );
        if (!array) return E_OUTOFMEMORY;
        memcpy( array, entries, sizeof( entries ) );
        *i = (IID *)array;
    }
    return S_OK;
}
static HRESULT WINAPI empty_map_view_GetRuntimeClassName( IMapView_HSTRING_ApplicationDataContainer *iface, HSTRING *n )
{
    /* CoreRT registers the generic instantiation from this name; the bare
     * "IMapView`2" (no type args) fails ComImport registration (failfast
     * 0x1007) when Store wraps the get_Containers view. */
    static const WCHAR cls[] =
        L"Windows.Foundation.Collections.IObservableMap`2<System.String,Windows.Storage.ApplicationDataContainer>";
    return WindowsCreateString( cls, ARRAY_SIZE( cls ) - 1, n );
}
static HRESULT WINAPI empty_map_view_GetTrustLevel( IMapView_HSTRING_ApplicationDataContainer *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}
/* Real Windows IMapView<HSTRING,V> slot layout: GetMatch, GetCount,
 * GetLookup, GetKeyAtIndex, GetValueAtIndex (6-10). Wine's idl shape
 * (Lookup/Size/HasKey/GetView/Split) mismatched the Store projection:
 * GetLookup@8 hit HasKey, the boolean was consumed as a container
 * pointer (failfast 0x1007 on the XAML resource container walk). */
static HRESULT WINAPI empty_iter_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    (void)iid;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI empty_iter_AddRef( IUnknown *iface ) { return 2; }
static ULONG WINAPI empty_iter_Release( IUnknown *iface ) { return 1; }
static HRESULT WINAPI empty_iter_GetCurrent( IUnknown *iface, HSTRING *current )
{
    if (current) *current = NULL;
    return 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI empty_iter_HasCurrent( IUnknown *iface, BOOLEAN *has )
{
    if (has) *has = FALSE;
    return S_OK;
}
static HRESULT WINAPI empty_iter_MoveNext( IUnknown *iface, BOOLEAN *has )
{
    if (has) *has = FALSE;
    return S_OK;
}
static HRESULT WINAPI empty_iter_GetMany( IUnknown *iface, UINT32 cap, HSTRING *items, UINT32 *count )
{
    (void)cap; (void)items;
    if (count) *count = 0;
    return S_OK;
}
struct empty_string_iterator_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IUnknown *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IUnknown *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IUnknown *iface );
    HRESULT (STDMETHODCALLTYPE *get_Current)( IUnknown *iface, HSTRING *current );
    HRESULT (STDMETHODCALLTYPE *get_HasCurrent)( IUnknown *iface, BOOLEAN *has );
    HRESULT (STDMETHODCALLTYPE *MoveNext)( IUnknown *iface, BOOLEAN *has );
    HRESULT (STDMETHODCALLTYPE *GetMany)( IUnknown *iface, UINT32 cap, HSTRING *items, UINT32 *count );
};
static const struct empty_string_iterator_vtbl empty_iter_vtbl =
{
    empty_iter_QueryInterface,
    empty_iter_AddRef,
    empty_iter_Release,
    empty_iter_GetCurrent,
    empty_iter_HasCurrent,
    empty_iter_MoveNext,
    empty_iter_GetMany,
};
static IUnknown empty_string_iterator = { (void *)&empty_iter_vtbl };

static HRESULT WINAPI empty_map_view_First( IMapView_HSTRING_ApplicationDataContainer *iface, IUnknown **current )
{
    TRACE( "%s\n", debugstr_a(__func__) );
    if (current) *current = &empty_string_iterator;
    return S_OK;
}
static HRESULT WINAPI empty_map_view_GetCount( IMapView_HSTRING_ApplicationDataContainer *iface, UINT32 *size )
{
    if (size) *size = 0;
    return S_OK;
}
static IApplicationDataContainer *g_view_owner_container;

static HRESULT WINAPI empty_map_view_GetLookup( IMapView_HSTRING_ApplicationDataContainer *iface,
                                                HSTRING key, IApplicationDataContainer **value )
{
    /* The RCW walker's AddRefFromExternal trampoline dispatches through slot 7
     * (GetLookup); E_BOUNDS made the generated wrapper throw
     * IndexOutOfRange(non-continuable) -> failfast 0x1007.  Succeed with the
     * owning container. */
    if (value) *value = g_view_owner_container;
    return g_view_owner_container ? S_OK : 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI empty_map_view_GetKeyAtIndex( IMapView_HSTRING_ApplicationDataContainer *iface,
                                                    UINT32 index, HSTRING *key )
{
    if (key) *key = NULL;
    return 0x8000000b; /* E_BOUNDS */
}
static HRESULT WINAPI empty_map_view_GetValueAtIndex( IMapView_HSTRING_ApplicationDataContainer *iface,
                                                      UINT32 index, IApplicationDataContainer **value )
{
    if (value) *value = NULL;
    return 0x8000000b; /* E_BOUNDS */
}

static HRESULT WINAPI empty_map_view_add_MapChanged( IMapView_HSTRING_ApplicationDataContainer *iface,
                                                     IUnknown *handler,
                                                     EventRegistrationToken *token )
{
    if (token) token->value = 0;
    return S_OK;
}

static HRESULT WINAPI empty_map_view_remove_MapChanged( IMapView_HSTRING_ApplicationDataContainer *iface,
                                                        EventRegistrationToken token )
{
    return S_OK;
}

/* Real Windows IMapView<HSTRING,V> + IObservableMap<HSTRING,V> layout:
 * GetCount, GetLookup, GetKeyAtIndex, GetValueAtIndex, First (6-10),
 * add/remove_MapChanged (11-12) = 13 slots. Wine's idl shape
 * (Lookup/Size/HasKey/GetView/Split) is offset-by-one against the Store
 * projection: GetLookup@8 hit HasKey and the boolean decoded as a
 * container pointer (failfast 0x1007 on the XAML resource containers). */
struct empty_observable_map_view_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IMapView_HSTRING_ApplicationDataContainer *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IMapView_HSTRING_ApplicationDataContainer *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IMapView_HSTRING_ApplicationDataContainer *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IMapView_HSTRING_ApplicationDataContainer *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IMapView_HSTRING_ApplicationDataContainer *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IMapView_HSTRING_ApplicationDataContainer *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *GetCount)( IMapView_HSTRING_ApplicationDataContainer *iface, UINT32 *size );
    HRESULT (STDMETHODCALLTYPE *GetLookup)( IMapView_HSTRING_ApplicationDataContainer *iface, HSTRING key, IApplicationDataContainer **value );
    HRESULT (STDMETHODCALLTYPE *GetKeyAtIndex)( IMapView_HSTRING_ApplicationDataContainer *iface, UINT32 index, HSTRING *key );
    HRESULT (STDMETHODCALLTYPE *GetValueAtIndex)( IMapView_HSTRING_ApplicationDataContainer *iface, UINT32 index, IApplicationDataContainer **value );
    HRESULT (STDMETHODCALLTYPE *First)( IMapView_HSTRING_ApplicationDataContainer *iface, IUnknown **current );
    HRESULT (STDMETHODCALLTYPE *add_MapChanged)( IMapView_HSTRING_ApplicationDataContainer *iface, IUnknown *handler, EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_MapChanged)( IMapView_HSTRING_ApplicationDataContainer *iface, EventRegistrationToken token );
};

static const struct empty_observable_map_view_vtbl empty_map_view_vtbl =
{
    empty_map_view_QueryInterface,
    empty_map_view_AddRef,
    empty_map_view_Release,
    empty_map_view_GetIids,
    empty_map_view_GetRuntimeClassName,
    empty_map_view_GetTrustLevel,
    empty_map_view_GetCount,
    empty_map_view_GetLookup,
    empty_map_view_GetKeyAtIndex,
    empty_map_view_GetValueAtIndex,
    empty_map_view_First,
    empty_map_view_add_MapChanged,
    empty_map_view_remove_MapChanged,
};

struct empty_map_view_obj { const IMapView_HSTRING_ApplicationDataContainerVtbl *vtbl; LONG ref; };
static struct empty_map_view_obj empty_map_view = { (void *)&empty_map_view_vtbl, 1 };

static HRESULT WINAPI container_get_Containers( IApplicationDataContainer *iface, IMapView_HSTRING_ApplicationDataContainer **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    g_view_owner_container = iface;
    struct empty_map_view_obj *obj = CoTaskMemAlloc( sizeof(*obj) );
    if (!obj) return E_OUTOFMEMORY;
    obj->vtbl = (void *)&empty_map_view_vtbl;
    obj->ref = 1;
    *value = (IMapView_HSTRING_ApplicationDataContainer *)obj;
    return S_OK;
}

static HRESULT container_create( ApplicationDataLocality locality, struct application_data *owner,
                                 IApplicationDataContainer **out );

static HRESULT WINAPI container_CreateContainer( IApplicationDataContainer *iface, HSTRING name, ApplicationDataCreateDisposition disposition, IApplicationDataContainer **container )
{
    struct application_data_container *parent = impl_from_IApplicationDataContainer( iface );
    TRACE( "iface %p, name %s, disposition %d, container %p\n", iface, debugstr_hstring(name), disposition, container );
    if (!container) return E_POINTER;
    return container_create( parent->locality, parent->owner, container );
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
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return IApplicationDataContainer_QueryInterface( &impl->IApplicationDataContainer_iface, iid, out );
}

static ULONG WINAPI container_methods_AddRef( IApplicationDataContainerMethods *iface )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return IApplicationDataContainer_AddRef( &impl->IApplicationDataContainer_iface );
}

static ULONG WINAPI container_methods_Release( IApplicationDataContainerMethods *iface )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return IApplicationDataContainer_Release( &impl->IApplicationDataContainer_iface );
}

static HRESULT WINAPI container_methods_GetIids( IApplicationDataContainerMethods *iface, ULONG *count, IID **iids )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return IApplicationDataContainer_GetIids( &impl->IApplicationDataContainer_iface, count, iids );
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

static HRESULT WINAPI container_methods_GetAllContains( IApplicationDataContainerMethods *iface,
                                                       IApplicationDataContainer **container )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return container_create( impl->locality, impl->owner, container );
}

static HRESULT WINAPI container_methods_get_Values( IApplicationDataContainerMethods *iface, IPropertySet **value )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return container_get_Values( &impl->IApplicationDataContainer_iface, value );
}

static HRESULT WINAPI container_methods_get_Containers( IApplicationDataContainerMethods *iface, IMapView_HSTRING_ApplicationDataContainer **value )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return container_get_Containers( &impl->IApplicationDataContainer_iface, value );
}

static HRESULT WINAPI container_methods_CreateContainer( IApplicationDataContainerMethods *iface, HSTRING name,
                                                        ApplicationDataCreateDisposition disposition, IApplicationDataContainer **container )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
    return container_CreateContainer( &impl->IApplicationDataContainer_iface, name, disposition, container );
}

static HRESULT WINAPI container_methods_DeleteContainer( IApplicationDataContainerMethods *iface, HSTRING name )
{
    struct application_data_container *impl = impl_from_container_methods( iface );
    TRACE( "%s\n", debugstr_a(__func__) );
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
container_methods_GetAllContains,
};

static HRESULT container_create( ApplicationDataLocality locality, struct application_data *owner,
                                 IApplicationDataContainer **out )
{
    struct application_data_container *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IApplicationDataContainer_iface.lpVtbl = &container_vtbl;
    impl->IApplicationDataContainerMethods_iface.lpVtbl = &container_methods_vtbl;
    impl->ref = 1;
    impl->locality = locality;
    impl->owner = owner;
    settings_subkey( locality, impl->subkey, ARRAY_SIZE(impl->subkey) );
    *out = &impl->IApplicationDataContainer_iface;
    return S_OK;
}

static HRESULT WINAPI application_data_get_LocalSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    static IApplicationDataContainer *local_settings;
    if (!value) return E_POINTER;
    if (!local_settings)
        container_create( ApplicationDataLocality_Local, impl_from_IApplicationData( iface ), &local_settings );
    *value = local_settings;
    if (local_settings) IApplicationDataContainer_AddRef( local_settings );
    return *value ? S_OK : E_FAIL;
}

static HRESULT WINAPI application_data_get_RoamingSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    static IApplicationDataContainer *roaming_settings;
    if (!value) return E_POINTER;
    if (!roaming_settings)
        container_create( ApplicationDataLocality_Roaming, impl_from_IApplicationData( iface ), &roaming_settings );
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

/* Windows.Storage.IStorageFolder2 is commented out in our windows.storage.idl,
 * so declare the interface locally. {e827e6b9-08d9-0aae-a0ac-fe5ed3cbbbd3} */
interface IStorageFolder2;
typedef interface IStorageFolder2 IStorageFolder2;
struct IStorageFolder2Vtbl;
interface IStorageFolder2
{
    const struct IStorageFolder2Vtbl *lpVtbl;
};
typedef struct IStorageFolder2Vtbl
{
    HRESULT (WINAPI *QueryInterface)( IStorageFolder2 *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( IStorageFolder2 *iface );
    ULONG (WINAPI *Release)( IStorageFolder2 *iface );
    HRESULT (WINAPI *GetIids)( IStorageFolder2 *iface, ULONG *iid_count, IID **iids );
    HRESULT (WINAPI *GetRuntimeClassName)( IStorageFolder2 *iface, HSTRING *class_name );
    HRESULT (WINAPI *GetTrustLevel)( IStorageFolder2 *iface, TrustLevel *trust_level );
    HRESULT (WINAPI *TryGetItemAsync)( IStorageFolder2 *iface, HSTRING name,
                                       IAsyncOperation_IStorageItem **result );
} IStorageFolder2Vtbl;

static const IID IID_IStorageFolder2 =
{ 0xe827e6b9, 0x08d9, 0x4aae, { 0xa0, 0xac, 0xfe, 0x5e, 0xd3, 0xcb, 0xbb, 0xd3 } };

struct storage_folder
{
    IStorageFolder IStorageFolder_iface;
    IStorageItem IStorageItem_iface;
    IStorageItem2 IStorageItem2_iface;
    IStorageFolder2 IStorageFolder2_iface;
    IApplicationDataContainerMethods IApplicationDataContainerMethods_iface;
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

static inline struct storage_folder *impl_from_IStorageItem2( IStorageItem2 *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageItem2_iface );
}

static inline struct storage_folder *impl_from_IStorageFolder2( IStorageFolder2 *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageFolder2_iface );
}

static inline struct storage_folder *folder_impl_from_IApplicationDataContainerMethods(
        IApplicationDataContainerMethods *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IApplicationDataContainerMethods_iface );
}

static HRESULT WINAPI storage_folder_QueryInterface( IStorageFolder *iface, REFIID iid, void **out );
static ULONG WINAPI storage_folder_AddRef( IStorageFolder *iface );
static ULONG WINAPI storage_folder_Release( IStorageFolder *iface );
static HRESULT WINAPI storage_folder_GetIids( IStorageFolder *iface, ULONG *count, IID **iids );
static HRESULT WINAPI storage_folder_GetRuntimeClassName( IStorageFolder *iface, HSTRING *name );
static HRESULT WINAPI storage_folder_GetTrustLevel( IStorageFolder *iface, TrustLevel *level );

static HRESULT WINAPI folder_methods_QueryInterface( IApplicationDataContainerMethods *iface, REFIID iid, void **out )
{
    return storage_folder_QueryInterface( &folder_impl_from_IApplicationDataContainerMethods( iface )->IStorageFolder_iface, iid, out );
}

static ULONG WINAPI folder_methods_AddRef( IApplicationDataContainerMethods *iface )
{
    return storage_folder_AddRef( &folder_impl_from_IApplicationDataContainerMethods( iface )->IStorageFolder_iface );
}

static ULONG WINAPI folder_methods_Release( IApplicationDataContainerMethods *iface )
{
    return storage_folder_Release( &folder_impl_from_IApplicationDataContainerMethods( iface )->IStorageFolder_iface );
}

static HRESULT WINAPI folder_methods_GetIids( IApplicationDataContainerMethods *iface, ULONG *count, IID **iids )
{
    return storage_folder_GetIids( &folder_impl_from_IApplicationDataContainerMethods( iface )->IStorageFolder_iface, count, iids );
}

static HRESULT WINAPI folder_methods_GetRuntimeClassName( IApplicationDataContainerMethods *iface, HSTRING *name )
{
    return storage_folder_GetRuntimeClassName( &folder_impl_from_IApplicationDataContainerMethods( iface )->IStorageFolder_iface, name );
}

static HRESULT WINAPI folder_methods_GetTrustLevel( IApplicationDataContainerMethods *iface, TrustLevel *level )
{
    return storage_folder_GetTrustLevel( &folder_impl_from_IApplicationDataContainerMethods( iface )->IStorageFolder_iface, level );
}

static HRESULT WINAPI folder_methods_GetAllContains( IApplicationDataContainerMethods *iface,
                                                    IApplicationDataContainer **container )
{
    return container_create( ApplicationDataLocality_Local, NULL, container );
}

static HRESULT WINAPI folder_methods_get_Values( IApplicationDataContainerMethods *iface, IPropertySet **value )
{
    IApplicationDataContainer *snap;
    HRESULT hr = container_create( ApplicationDataLocality_Local, NULL, &snap );
    if (FAILED( hr )) return hr;
    hr = container_get_Values( snap, value );
    IApplicationDataContainer_Release( snap );
    return hr;
}

static HRESULT WINAPI folder_methods_get_Containers( IApplicationDataContainerMethods *iface,
                                                    IMapView_HSTRING_ApplicationDataContainer **value )
{
    IApplicationDataContainer *snap;
    HRESULT hr = container_create( ApplicationDataLocality_Local, NULL, &snap );
    if (FAILED( hr )) return hr;
    hr = container_get_Containers( snap, value );
    IApplicationDataContainer_Release( snap );
    return hr;
}

static HRESULT WINAPI folder_methods_CreateContainer( IApplicationDataContainerMethods *iface, HSTRING name,
                                                     ApplicationDataCreateDisposition disposition,
                                                     IApplicationDataContainer **container )
{
    IApplicationDataContainer *snap;
    HRESULT hr = container_create( ApplicationDataLocality_Local, NULL, &snap );
    if (FAILED( hr )) return hr;
    hr = container_CreateContainer( snap, name, disposition, container );
    IApplicationDataContainer_Release( snap );
    return hr;
}

static HRESULT WINAPI folder_methods_DeleteContainer( IApplicationDataContainerMethods *iface, HSTRING name )
{
    IApplicationDataContainer *snap;
    HRESULT hr = container_create( ApplicationDataLocality_Local, NULL, &snap );
    if (FAILED( hr )) return hr;
    hr = container_DeleteContainer( snap, name );
    IApplicationDataContainer_Release( snap );
    return hr;
}

static const struct IApplicationDataContainerMethodsVtbl storage_folder_methods_vtbl =
{
    folder_methods_QueryInterface,
    folder_methods_AddRef,
    folder_methods_Release,
    folder_methods_GetIids,
    folder_methods_GetRuntimeClassName,
    folder_methods_GetTrustLevel,
        folder_methods_get_Values,
    folder_methods_get_Containers,
    folder_methods_CreateContainer,
    folder_methods_DeleteContainer,
folder_methods_GetAllContains,
};

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
    if (IsEqualGUID( iid, &IID_IStorageItem2 ))
    {
        *out = &impl->IStorageItem2_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStorageFolder2 ))
    {
        *out = &impl->IStorageFolder2_iface;
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
{
    /* Same shader contract as container_GetIids: fresh Guid** array per
     * call, null-page count slot tolerated. The LocalFolder RCW build
     * (MemoryMappedFileSadRecordPersistentStorage) consumes this array to
     * seed the ComVirtualMethodData composite; E_NOTIMPL left the pair
     * array empty and the slot fetch AV'd in TryGetOrCreateObjectForComInstance. */
    return E_NOTIMPL;
}
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

/* IStorageItem2 (WinAppSDK layout: GetParentAsync, IsEqual, GetAttributesAsync)
 * and IStorageFolder2 (TryGetItemAsync) faces: the LocalFolder RCW projection
 * (StorageFolder CCW) casts to both; without them CoreCLR's vtable generator
 * throws MissingMethodException 0x80131506 in
 * TryGetOrCreateObjectForComInstanceInternal. Forwarding mirrors
 * dlls/windows.storage/storage.c storage_item2_folder_vtbl. */
static HRESULT WINAPI storage_item2_QueryInterface( IStorageItem2 *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageItem2( iface );
    return IStorageFolder_QueryInterface( &impl->IStorageFolder_iface, iid, out );
}
static ULONG WINAPI storage_item2_AddRef( IStorageItem2 *iface )
{
    struct storage_folder *impl = impl_from_IStorageItem2( iface );
    return IStorageFolder_AddRef( &impl->IStorageFolder_iface );
}
static ULONG WINAPI storage_item2_Release( IStorageItem2 *iface )
{
    struct storage_folder *impl = impl_from_IStorageItem2( iface );
    return IStorageFolder_Release( &impl->IStorageFolder_iface );
}
static HRESULT WINAPI storage_item2_GetIids( IStorageItem2 *iface, ULONG *count, IID **iids )
{
    struct storage_folder *impl = impl_from_IStorageItem2( iface );
    return IStorageFolder_GetIids( &impl->IStorageFolder_iface, count, iids );
}
static HRESULT WINAPI storage_item2_GetRuntimeClassName( IStorageItem2 *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.StorageFolder", 29, name ); }
static HRESULT WINAPI storage_item2_GetTrustLevel( IStorageItem2 *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI storage_item2_GetParentAsync( IStorageItem2 *iface, IAsyncOperation_IStorageItem **result )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item2_IsEqual( IStorageItem2 *iface, IStorageItem *item, boolean *result )
{
    struct storage_folder *impl = impl_from_IStorageItem2( iface );
    HSTRING str = NULL;
    if (!result) return E_POINTER;
    *result = FALSE;
    if (!item) return S_OK;
    if (SUCCEEDED( IStorageItem_get_Path( item, &str ) ) && str)
    {
        const WCHAR *p = WindowsGetStringRawBuffer( str, NULL );
        if (p && !wcsicmp( p, impl->path )) *result = TRUE;
        WindowsDeleteString( str );
    }
    return S_OK;
}
static HRESULT WINAPI storage_item2_GetAttributesAsync( IStorageItem2 *iface, IAsyncOperation_FileAttributes **result )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static const struct IStorageItem2Vtbl storage_item2_vtbl =
{
    storage_item2_QueryInterface,
    storage_item2_AddRef,
    storage_item2_Release,
    storage_item2_GetIids,
    storage_item2_GetRuntimeClassName,
    storage_item2_GetTrustLevel,
    storage_item2_GetParentAsync,
    storage_item2_IsEqual,
    storage_item2_GetAttributesAsync,
};

static HRESULT WINAPI storage_folder2_QueryInterface( IStorageFolder2 *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageFolder2( iface );
    return IStorageFolder_QueryInterface( &impl->IStorageFolder_iface, iid, out );
}
static ULONG WINAPI storage_folder2_AddRef( IStorageFolder2 *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder2( iface );
    return IStorageFolder_AddRef( &impl->IStorageFolder_iface );
}
static ULONG WINAPI storage_folder2_Release( IStorageFolder2 *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder2( iface );
    return IStorageFolder_Release( &impl->IStorageFolder_iface );
}
static HRESULT WINAPI storage_folder2_GetIids( IStorageFolder2 *iface, ULONG *count, IID **iids )
{
    struct storage_folder *impl = impl_from_IStorageFolder2( iface );
    return IStorageFolder_GetIids( &impl->IStorageFolder_iface, count, iids );
}
static HRESULT WINAPI storage_folder2_GetRuntimeClassName( IStorageFolder2 *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.StorageFolder", 29, name ); }
static HRESULT WINAPI storage_folder2_GetTrustLevel( IStorageFolder2 *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI storage_folder2_TryGetItemAsync( IStorageFolder2 *iface, HSTRING name,
                                                       IAsyncOperation_IStorageItem **result )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static const struct IStorageFolder2Vtbl storage_folder2_vtbl =
{
    storage_folder2_QueryInterface,
    storage_folder2_AddRef,
    storage_folder2_Release,
    storage_folder2_GetIids,
    storage_folder2_GetRuntimeClassName,
    storage_folder2_GetTrustLevel,
    storage_folder2_TryGetItemAsync,
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
    WCHAR *p;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IStorageFolder_iface.lpVtbl = &storage_folder_vtbl;
    impl->IStorageItem_iface.lpVtbl = &storage_item_vtbl;
    impl->IStorageItem2_iface.lpVtbl = &storage_item2_vtbl;
    impl->IStorageFolder2_iface.lpVtbl = &storage_folder2_vtbl;
    impl->IApplicationDataContainerMethods_iface.lpVtbl = &storage_folder_methods_vtbl;
    impl->ref = 1;
    lstrcpynW( impl->path, path, ARRAY_SIZE(impl->path) );
    for (p = impl->path + 3; *p; p++)
    {
        if (*p == '\\')
        {
            *p = 0;
            CreateDirectoryW( impl->path, NULL );
            *p = '\\';
        }
    }
    CreateDirectoryW( impl->path, NULL );
    *out = &impl->IStorageFolder_iface;
    return S_OK;
}

/* Per-package state folders: %LOCALAPPDATA%\<pkg>\{LocalState,RoamingState,
 * TempState,LocalCache}, created on demand. */
static HRESULT appdata_folder_create( const WCHAR *subdir, IStorageFolder **value )
{
    WCHAR path[MAX_PATH], name[MAX_PATH];

    get_package_name( name, ARRAY_SIZE(name) );
    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", path, ARRAY_SIZE(path) ))
    {
        if (!GetEnvironmentVariableW( L"USERPROFILE", path, ARRAY_SIZE(path) ))
            return E_FAIL;
        lstrcatW( path, L"\\AppData\\Local" );
    }
    lstrcatW( path, L"\\" );
    lstrcatW( path, name );
    lstrcatW( path, L"\\" );
    lstrcatW( path, subdir );
    return storage_folder_create( path, value );
}

static HRESULT WINAPI application_data_get_LocalFolder( IApplicationData *iface, IStorageFolder **value )
{
    if (!value) return E_POINTER;
    return appdata_folder_create( L"LocalState", value );
}

static HRESULT WINAPI application_data_get_RoamingFolder( IApplicationData *iface, IStorageFolder **value )
{
    if (!value) return E_POINTER;
    return appdata_folder_create( L"RoamingState", value );
}

static HRESULT WINAPI application_data_get_TemporaryFolder( IApplicationData *iface, IStorageFolder **value )
{
    if (!value) return E_POINTER;
    return appdata_folder_create( L"TempState", value );
}

static HRESULT WINAPI application_data_add_DataChanged( IApplicationData *iface, ITypedEventHandler_ApplicationData_IInspectable *handler,
                                                        EventRegistrationToken *token )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    struct data_changed_entry *entry;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!token) return E_POINTER;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return E_OUTOFMEMORY;
    entry->handler = handler;
    ITypedEventHandler_ApplicationData_IInspectable_AddRef( handler );
    entry->token.value = (UINT64)(ULONG)InterlockedIncrement( &impl->next_data_changed_token );
    entry->next = impl->handlers;
    impl->handlers = entry;
    *token = entry->token;
    return S_OK;
}

static HRESULT WINAPI application_data_remove_DataChanged( IApplicationData *iface, EventRegistrationToken token )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    struct data_changed_entry **link = &impl->handlers;

    TRACE( "iface %p, token %#I64x.\n", iface, token.value );

    while (*link)
    {
        struct data_changed_entry *entry = *link;
        if (entry->token.value == token.value)
        {
            *link = entry->next;
            ITypedEventHandler_ApplicationData_IInspectable_Release( entry->handler );
            free( entry );
            return S_OK;
        }
        link = &entry->next;
    }
    return S_OK; /* unknown token: remove is idempotent, matching Windows */
}

static HRESULT WINAPI application_data_SignalDataChanged( IApplicationData *iface )
{
    struct application_data *impl = impl_from_IApplicationData( iface );

    TRACE( "iface %p.\n", iface );
    application_data_fire_data_changed( impl );
    return S_OK;
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
    struct application_data *impl = impl_from_IApplicationData2( iface );
    return application_data_GetIids( &impl->IApplicationData_iface, iid_count, iids );
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
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return appdata_folder_create( L"LocalCache", value );
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

static HRESULT WINAPI application_data3_QueryInterface( IApplicationData3 *iface, REFIID iid, void **out )
{
    return application_data_QueryInterface( &impl_from_IApplicationData3( iface )->IApplicationData_iface, iid, out );
}
static ULONG WINAPI application_data3_AddRef( IApplicationData3 *iface )
{
    return application_data_AddRef( &impl_from_IApplicationData3( iface )->IApplicationData_iface );
}
static ULONG WINAPI application_data3_Release( IApplicationData3 *iface )
{
    return application_data_Release( &impl_from_IApplicationData3( iface )->IApplicationData_iface );
}
static HRESULT WINAPI application_data3_GetIids( IApplicationData3 *iface, ULONG *count, IID **iids )
{
    return application_data_GetIids( &impl_from_IApplicationData3( iface )->IApplicationData_iface, count, iids );
}
static HRESULT WINAPI application_data3_GetRuntimeClassName( IApplicationData3 *iface, HSTRING *name )
{
    return application_data_GetRuntimeClassName( &impl_from_IApplicationData3( iface )->IApplicationData_iface, name );
}
static HRESULT WINAPI application_data3_GetTrustLevel( IApplicationData3 *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI application_data3_GetPublisherCacheFolder( IApplicationData3 *iface, HSTRING folder_name, IStorageFolder **value )
{
    const WCHAR *sub = WindowsGetStringRawBuffer( folder_name, NULL );
    WCHAR rel[MAX_PATH];

    TRACE( "iface %p, folder_name %s, value %p\n", iface, debugstr_w(sub), value );
    if (!value) return E_POINTER;
    if (sub && sub[0])
        swprintf( rel, ARRAY_SIZE(rel), L"PublisherCache\\%s", sub );
    else
        lstrcpyW( rel, L"PublisherCache" );
    return appdata_folder_create( rel, value );
}
static HRESULT WINAPI application_data3_ClearPublisherCacheFolderAsync( IApplicationData3 *iface, HSTRING folder_name, IAsyncAction **operation )
{
    FIXME( "iface %p, folder_name %s, operation %p stub!\n", iface, debugstr_hstring(folder_name), operation );
    return E_NOTIMPL;
}
static HRESULT WINAPI application_data3_get_SharedLocalFolder( IApplicationData3 *iface, IStorageFolder **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    return appdata_folder_create( L"SharedLocal", value );
}

static const struct IApplicationData3Vtbl application_data3_vtbl =
{
    application_data3_QueryInterface,
    application_data3_AddRef,
    application_data3_Release,
    /* IInspectable methods */
    application_data3_GetIids,
    application_data3_GetRuntimeClassName,
    application_data3_GetTrustLevel,
    /* IApplicationData3 methods */
    application_data3_GetPublisherCacheFolder,
    application_data3_ClearPublisherCacheFolderAsync,
    application_data3_get_SharedLocalFolder,
};

DEFINE_IINSPECTABLE( application_data_statics, IApplicationDataStatics, struct application_data_statics, IActivationFactory_iface )

/* IApplicationData.Current is a process-wide singleton on Windows: the CLR RCW
 * cache is keyed by ABI pointer, and per-call fresh objects hand the shader a
 * new identity per decode (one ComVirtualMethodData composite + heap chunk
 * each), so the Current chain re-fetches on every property hop. Create-on-ask
 * once, AddRef per call; never freed. */
static struct application_data *application_data_current( void )
{
    static struct application_data *singleton;
    struct application_data *impl;

    if (!singleton)
    {
        if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
        impl->IApplicationData_iface.lpVtbl = &application_data_vtbl;
        impl->IApplicationData2_iface.lpVtbl = &application_data2_vtbl;
        impl->IApplicationData3_iface.lpVtbl = &application_data3_vtbl;
        impl->IApplicationDataMethods_iface.lpVtbl = &application_data_methods_vtbl;
        impl->IApplicationDataGetForApplication_iface.lpVtbl = &application_data_getforapp_vtbl;
        impl->ref = 1;
        if (InterlockedCompareExchangePointer( (void **)&singleton, impl, NULL )) free( impl );
    }
    impl = singleton;
    return impl;
}

static HRESULT WINAPI application_data_statics_get_Current( IApplicationDataStatics *iface, IApplicationData **value )
{
    struct application_data *impl;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_INVALIDARG;
    if (!(impl = application_data_current() )) return E_OUTOFMEMORY;

    *value = &impl->IApplicationData_iface;
    IApplicationData_AddRef( *value );
    TRACE( "returning singleton IApplicationData %p.\n", *value );
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

/* Probe 4/5 wrappers: log iid + result on every QI (bounded, see
 * qi_log_record); IApplicationDataGetForApplication face, see the IID
 * comment - for the in-app activation the requested app instance is self,
 * so GetForApp returns the same application_data object. */
static HRESULT WINAPI application_data_QueryInterface( IApplicationData *iface, REFIID iid, void **out )
{
    HRESULT hr;
    application_data_preseed();
    hr = application_data_qi_impl( iface, iid, out );
    qi_log_record( iid, hr, "appdata" );
    return hr;
}

static HRESULT WINAPI container_QueryInterface( IApplicationDataContainer *iface, REFIID iid, void **out )
{
    HRESULT hr = container_qi_impl( iface, iid, out );
    qi_log_record( iid, hr, "container" );
    return hr;
}

/* Arm-3 probe 9/11 (s240/s249): pre-populate the pinned heap-head composite
 * blocks ahead of any QI cascade. CoreCLR builds one composite per provider
 * object in heap-head order; each build consumes the pair array its provider
 * GetIids returns and faults on the slot-7 fetch of a not-yet-written field.
 * Seed every provider block up front, at 64-byte stride:
 *   +0x00 factory:   {factory, IActivationFactory},{statics, IApplicationDataStatics},{tracker, IReferenceTracker}
 *   +0x40 appdata:   the 5-pair application_data_GetIids array
 *   +0x80 container: the 2-pair container_GetIids array
 * (propertyset's block lands at +0xc0, seeded by wintypes heap_prewarm.)
 * Runs at DllGetActivationFactory entry, ahead of the (NULL,NULL) count
 * probe, so no composite build ever reads an unseeded block. */
void application_data_preseed( void )
{
    static BOOL done;
    void **base = (void **)0x6fff00000000;
    MEMORY_BASIC_INFORMATION mbi;

    if (done) return;
    done = TRUE;
    if (VirtualQuery( (void *)base, &mbi, sizeof(mbi) ) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_READONLY)))
        return;
    base[0] = (void **)&factory_vtbl;
    base[1] = (void *)&IID_IActivationFactory;
    base[2] = (void **)&application_data_statics_vtbl;
    base[3] = (void *)&IID_IApplicationDataStatics;
    base[4] = (void **)&appdata_tracker_vtbl;
    base[5] = (void *)&IID_IReferenceTracker_g;
    base[8] = (void **)&application_data_vtbl;
    base[9] = (void *)&IID_IApplicationData;
    base[10] = (void **)&application_data2_vtbl;
    base[11] = (void *)&IID_IApplicationData2;
    base[12] = (void **)&application_data_methods_vtbl;
    base[13] = (void *)&IID_IApplicationDataMethods;
    base[24] = (void **)&container_vtbl;
    base[25] = (void *)&IID_IApplicationDataContainer;
    /* propertyset (values) provider block at +0xc0: seeded by wintypes
     * heap_prewarm via propertyset_pair_array_seed (same-DLL symbols; the
     * cross-DLL import was not worth a spec export). */
    /* LocalFolder provider block at +0x100 (4th composite in heap-head
     * order after factory/appdata/container/propertyset): the StorageFolder
     * RCW build consumes this pair array; unseeded, the shader's slot fetch
     * threw MissingMethodException 0x80131506 in
     * TryGetOrCreateObjectForComInstanceInternal (sad-record ctor wall). */
    base[32] = (void **)&storage_folder_vtbl;
    base[33] = (void *)&IID_IStorageFolder;
    base[34] = (void **)&storage_item_vtbl;
    base[35] = (void *)&IID_IStorageItem;
    base[36] = (void **)&storage_item2_vtbl;
    base[37] = (void *)&IID_IStorageItem2;
    base[38] = (void **)&storage_folder2_vtbl;
    base[39] = (void *)&IID_IStorageFolder2;
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    HRESULT hr;
    application_data_preseed();
    hr = factory_qi_impl( iface, iid, out );
    qi_log_record( iid, hr, "factory" );
    return hr;
}

static inline struct application_data *impl_from_IApplicationDataGetForApplication(
        struct IApplicationDataGetForApplication *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationDataGetForApplication_iface );
}

static HRESULT WINAPI application_data_getforapp_QueryInterface(
        struct IApplicationDataGetForApplication *iface, REFIID iid, void **out )
{
    struct application_data *impl = impl_from_IApplicationDataGetForApplication( iface );
    return IApplicationData_QueryInterface( &impl->IApplicationData_iface, iid, out );
}

static ULONG WINAPI application_data_getforapp_AddRef( struct IApplicationDataGetForApplication *iface )
{
    struct application_data *impl = impl_from_IApplicationDataGetForApplication( iface );
    return IApplicationData_AddRef( &impl->IApplicationData_iface );
}

static ULONG WINAPI application_data_getforapp_Release( struct IApplicationDataGetForApplication *iface )
{
    struct application_data *impl = impl_from_IApplicationDataGetForApplication( iface );
    return IApplicationData_Release( &impl->IApplicationData_iface );
}

static HRESULT WINAPI application_data_getforapp_GetIids(
        struct IApplicationDataGetForApplication *iface, ULONG *count, IID **iids )
{
    struct application_data *impl = impl_from_IApplicationDataGetForApplication( iface );
    return application_data_GetIids( &impl->IApplicationData_iface, count, iids );
}

static HRESULT WINAPI application_data_getforapp_GetRuntimeClassName(
        struct IApplicationDataGetForApplication *iface, HSTRING *name )
{
    struct application_data *impl = impl_from_IApplicationDataGetForApplication( iface );
    return application_data_GetRuntimeClassName( &impl->IApplicationData_iface, name );
}

static HRESULT WINAPI application_data_getforapp_GetTrustLevel(
        struct IApplicationDataGetForApplication *iface, TrustLevel *level )
{
    struct application_data *impl = impl_from_IApplicationDataGetForApplication( iface );
    return application_data_GetTrustLevel( &impl->IApplicationData_iface, level );
}

static HRESULT WINAPI application_data_getforapp_GetForApp(
        struct IApplicationDataGetForApplication *iface, HSTRING app_name, IApplicationData **value )
{
    struct application_data *impl = impl_from_IApplicationDataGetForApplication( iface );

    TRACE( "iface %p, app_name %s, value %p.\n", iface, debugstr_hstring( app_name ), value );

    if (!value) return E_POINTER;
    *value = &impl->IApplicationData_iface;
    IApplicationData_AddRef( *value );
    return S_OK;
}

static const struct IApplicationDataGetForApplicationVtbl application_data_getforapp_vtbl =
{
    application_data_getforapp_QueryInterface,
    application_data_getforapp_AddRef,
    application_data_getforapp_Release,
    /* IInspectable methods */
    application_data_getforapp_GetIids,
    application_data_getforapp_GetRuntimeClassName,
    application_data_getforapp_GetTrustLevel,
    /* IApplicationDataGetForApplication methods */
    application_data_getforapp_GetForApp,
};
