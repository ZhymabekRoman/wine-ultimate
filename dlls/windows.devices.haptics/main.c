/*
 * Windows.Devices.Haptics runtime classes
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
#include <stddef.h>

#include "initguid.h"

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "eventtoken.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#include "windows.devices.haptics.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(haptics);

static const GUID IID_IWaveform =
    { 0x88383641, 0x711c, 0x4c5a, { 0x86, 0x74, 0x01, 0x9d, 0x56, 0x3e, 0x29, 0xb5 } };
static const GUID IID_IWaveformStatics =
    { 0x78a42b97, 0x8cd0, 0x44ea, { 0x93, 0x74, 0x98, 0xd8, 0x86, 0xec, 0xaa, 0xdb } };
static const GUID IID_IWaveformStatics2 =
    { 0xa7d24c27, 0xb79d, 0x510a, { 0xbf, 0x79, 0xff, 0x6d, 0x49, 0x17, 0x3e, 0x1d } };
static const GUID IID_IReference_Waveform =
    { 0x401d0000, 0x0001, 0x0000, { 0x60, 0x8f, 0xb9, 0xc5, 0xcc, 0x3b, 0x00, 0x00 } };

static inline int is_iid_ireference( REFIID iid )
{
    return iid->Data1 == 0x401d0000 && iid->Data2 == 0x0001 && iid->Data3 == 0x0000;
}

struct waveform
{
    const void **vtbl;
    const void **vtbl_reference;
    LONG ref;
    HSTRING name;
};

static HRESULT WINAPI waveform_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl );

    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWaveform ))
        *out = &impl->vtbl;
    else if (is_iid_ireference( iid ))
        *out = &impl->vtbl_reference;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI waveform_AddRef( void *iface )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI waveform_Release( void *iface )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI waveform_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI waveform_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Devices.Haptics.Waveform";
    return WindowsCreateString( class_name, ARRAY_SIZE( class_name ) - 1, name );
}

static HRESULT WINAPI waveform_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI waveform_get_Name( void *iface, HSTRING *value )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->name, value );
}

static HRESULT WINAPI waveform_get_Duration( void *iface, TimeSpan *value )
{
    if (!value) return E_POINTER;
    value->Duration = 50000; /* 5 ms in 100 ns units; no device behind it */
    return S_OK;
}

static HRESULT WINAPI reference_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl_reference );
    void *vtbl = &impl->vtbl;

    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWaveform ))
        *out = vtbl;
    else if (is_iid_ireference( iid ))
        *out = &impl->vtbl_reference;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI reference_AddRef( void *iface )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl_reference );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI reference_Release( void *iface )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl_reference );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI reference_Get( void *iface, void **value )
{
    struct waveform *impl = CONTAINING_RECORD( iface, struct waveform, vtbl_reference );
    if (!value) return E_POINTER;
    *value = &impl->vtbl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static const void *waveform_reference_vtbl[] =
{
    reference_QueryInterface,
    reference_AddRef,
    reference_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    reference_Get, /* 6 — IReference<Waveform>::Get */
};

static const void *waveform_vtbl[] =
{
    waveform_QueryInterface,
    waveform_AddRef,
    waveform_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    waveform_get_Name,      /* 6 */
    waveform_get_Duration,  /* 7 */
};

static HRESULT create_waveform( const WCHAR *name, void **out )
{
    struct waveform *impl = calloc( 1, sizeof(*impl) );

    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl = waveform_vtbl;
    impl->vtbl_reference = waveform_reference_vtbl;
    impl->ref = 1;
    WindowsCreateString( name, wcslen( name ), &impl->name );
    *out = &impl->vtbl;
    return S_OK;
}

/* statics + factory face */
struct waveform_statics
{
    const void **vtbl_factory;
    const void **vtbl_statics;
    const void **vtbl_statics2;
    const void **vtbl_reference;
};
static struct waveform_statics waveform_statics_obj;

static HRESULT WINAPI statics_reference_Get( void *iface, void **value )
{
    if (!value) return E_POINTER;
    return create_waveform( L"Simple", value );
}

static HRESULT WINAPI statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &waveform_statics_obj.vtbl_factory;
    else if (IsEqualGUID( iid, &IID_IWaveformStatics ))
        *out = &waveform_statics_obj.vtbl_statics;
    else if (IsEqualGUID( iid, &IID_IWaveformStatics2 ))
        *out = &waveform_statics_obj.vtbl_statics2;
    else if (is_iid_ireference( iid ))
        *out = &waveform_statics_obj.vtbl_reference;
    else
        return E_NOINTERFACE;
    return S_OK;
}

static ULONG WINAPI statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI statics_Release( void *iface ) { return 1; }

static HRESULT WINAPI factory_ActivateInstance( void *iface, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    return create_waveform( L"Simple", (void **)instance );
}

static HRESULT WINAPI statics_get_Basic( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}

static HRESULT WINAPI statics_get_HalfBeat( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 2;
    return S_OK;
}

static HRESULT WINAPI statics_get_ImpactLight( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 3;
    return S_OK;
}

static HRESULT WINAPI statics_get_ImpactMedium( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 4;
    return S_OK;
}

static HRESULT WINAPI statics_get_ImpactHeavy( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 5;
    return S_OK;
}

static HRESULT WINAPI statics_get_Simple( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 6;
    return S_OK;
}

static HRESULT WINAPI statics2_get_Cello( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 7;
    return S_OK;
}

static HRESULT WINAPI statics2_get_Triple( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 8;
    return S_OK;
}

static HRESULT WINAPI statics2_get_VibrationContinuous( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 9;
    return S_OK;
}

static HRESULT WINAPI statics2_get_VibrationShort( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 10;
    return S_OK;
}

static HRESULT WINAPI statics2_get_GalaxyPenContinuous( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 11;
    return S_OK;
}

static HRESULT WINAPI statics2_get_Hover( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 12;
    return S_OK;
}

static HRESULT WINAPI statics2_get_InkContinuous( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 13;
    return S_OK;
}

static HRESULT WINAPI statics2_get_MarkerContinuous( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 14;
    return S_OK;
}

static HRESULT WINAPI statics2_get_PencilContinuous( void *iface, UINT16 *value )
{
    if (!value) return E_POINTER;
    *value = 15;
    return S_OK;
}

static const WCHAR * const waveform_names[] =
{
    L"Basic", L"HalfBeat", L"ImpactLight", L"ImpactMedium", L"ImpactHeavy", L"Simple",
    L"Cello", L"Triple", L"VibrationContinuous", L"VibrationShort",
};
#define WAVEFORM_COUNT (sizeof(waveform_names) / sizeof(*waveform_names))

struct waveform_list
{
    const void **vtbl_iterable;
    const void **vtbl_iterator;
    LONG ref;
    ULONG pos;
};

static HRESULT WINAPI list_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterable );

    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        is_iid_ireference( iid ))
        *out = &impl->vtbl_iterable;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI list_AddRef( void *iface )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterable );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI list_Release( void *iface )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterable );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI iterator_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterator );

    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        is_iid_ireference( iid ))
        *out = &impl->vtbl_iterator;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI iterator_AddRef( void *iface )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterator );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI iterator_Release( void *iface )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterator );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI list_get_Current( void *iface, void **value )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterator );
    if (!value) return E_POINTER;
    if (impl->pos >= WAVEFORM_COUNT) return E_BOUNDS;
    return create_waveform( waveform_names[impl->pos], value );
}

static HRESULT WINAPI list_get_HasCurrent( void *iface, boolean *value )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterator );
    if (!value) return E_POINTER;
    *value = impl->pos < WAVEFORM_COUNT;
    return S_OK;
}

static HRESULT WINAPI list_MoveNext( void *iface, boolean *value )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterator );
    if (!value) return E_POINTER;
    if (impl->pos >= WAVEFORM_COUNT) { *value = false; return S_OK; }
    impl->pos++;
    *value = impl->pos < WAVEFORM_COUNT;
    return S_OK;
}

static HRESULT WINAPI list_GetMany( void *iface, ULONG capacity, ULONG *count, void **items )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterator );
    ULONG i;

    if (!count) return E_POINTER;
    *count = 0;
    if (!items) return capacity ? E_POINTER : S_OK;
    for (i = 0; i < capacity && impl->pos < WAVEFORM_COUNT; i++, impl->pos++)
    {
        HRESULT hr = create_waveform( waveform_names[impl->pos], items + i );
        if (FAILED( hr )) return hr;
        (*count)++;
    }
    return S_OK;
}

static HRESULT WINAPI list_First( void *iface, void **value )
{
    struct waveform_list *impl = CONTAINING_RECORD( iface, struct waveform_list, vtbl_iterable );
    if (!value) return E_POINTER;
    impl->pos = 0;
    *value = &impl->vtbl_iterator;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static const void *waveform_list_iterable_vtbl[] =
{
    list_QueryInterface,
    list_AddRef,
    list_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    list_First, /* 6 — IIterable<Waveform>::First */
};

static const void *waveform_list_iterator_vtbl[] =
{
    iterator_QueryInterface,
    iterator_AddRef,
    iterator_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    list_get_Current,  /* 6 — IIterator<Waveform>::get_Current */
    list_get_HasCurrent,
    list_MoveNext,
    list_GetMany,
};

static HRESULT statics_get_All( void *iface, void **value )
{
    struct waveform_list *impl = calloc( 1, sizeof(*impl) );
    if (!value) return E_POINTER;
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_iterable = waveform_list_iterable_vtbl;
    impl->vtbl_iterator = waveform_list_iterator_vtbl;
    impl->ref = 1;
    *value = &impl->vtbl_iterable;
    return S_OK;
}

static const void *waveform_factory_vtbl[] =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    factory_ActivateInstance,
};

static const void *waveform_statics_vtbl[] =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    statics_get_Basic,          /* 6 */
    statics_get_HalfBeat,
    statics_get_ImpactLight,
    statics_get_ImpactMedium,
    statics_get_ImpactHeavy,
    statics_get_Simple,
    statics_get_All,            /* 12 — IWaveformStatics::get_All */
};

static const void *waveform_statics2_vtbl[] =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    statics2_get_Cello,                 /* 6: BrushContinuous */
    statics2_get_Triple,                /* 7: ChiselMarkerContinuous */
    statics2_get_VibrationContinuous,   /* 8: EraserContinuous */
    statics2_get_VibrationShort,        /* 9: Error */
    statics2_get_GalaxyPenContinuous,   /* 10: GalaxyPenContinuous */
    statics2_get_Hover,                 /* 11: Hover */
    statics2_get_InkContinuous,         /* 12: InkContinuous */
    statics2_get_MarkerContinuous,      /* 13: MarkerContinuous */
    statics2_get_PencilContinuous,      /* 14: PencilContinuous */
};

static const void *waveform_statics_reference_vtbl[] =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    waveform_GetIids,
    waveform_GetRuntimeClassName,
    waveform_GetTrustLevel,
    statics_reference_Get, /* 6 — IReference<T>::Get */
};

static struct waveform_statics waveform_statics_obj =
{
    waveform_factory_vtbl,
    waveform_statics_vtbl,
    waveform_statics2_vtbl,
    waveform_statics_reference_vtbl,
};

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, L"Windows.Devices.Haptics.KnownSimpleHapticsControllerWaveforms" ))
        statics_QueryInterface( &waveform_statics_obj.vtbl_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}
