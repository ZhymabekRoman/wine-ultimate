/*
 * Windows.Devices.Geolocation implementation
 *
 * Copyright 2026 Wine Project
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


#include "initguid.h"
#include "private.h"
#include "weakref.h"

#include "wine/debug.h"
#include "wine/unixlib.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(geolocator);

static void get_default_fix(struct geo_fix *fix)
{
    fix->latitude = default_latitude;
    fix->longitude = default_longitude;
    fix->accuracy = default_accuracy;
    fix->altitude = default_altitude;
}

static HRESULT get_iids(const IID *const *iids, ULONG count, ULONG *iid_count, IID **out)
{
    IID *arr;
    ULONG i;

    if (!(arr = malloc(count * sizeof(*arr)))) return E_OUTOFMEMORY;
    for (i = 0; i < count; ++i) arr[i] = *iids[i];
    *iid_count = count;
    *out = arr;
    return S_OK;
}


static void get_current_fix(struct geo_fix *fix)
{
    /* unix_get_fix layers the GeoClue2 fix over the built-in defaults. */
    if (!__wine_init_unix_call() && WINE_UNIX_CALL(unix_get_fix, fix) >= 0) return;
    get_default_fix(fix);
}

static DateTime get_now(void)
{
    FILETIME ft;
    ULARGE_INTEGER ul;
    DateTime ret;

    GetSystemTimePreciseAsFileTime(&ft);
    ul.u.LowPart = ft.dwLowDateTime;
    ul.u.HighPart = ft.dwHighDateTime;
    ret.UniversalTime = ul.QuadPart;
    return ret;
}

/* ---------------------------------------------------------------------------
 * IReference<Double>, the boxed Altitude/AltitudeAccuracy/Heading/Speed.
 * ------------------------------------------------------------------------- */

struct reference_double
{
    IReference_DOUBLE IReference_DOUBLE_iface;
    LONG ref;
    double value;
};

static inline struct reference_double *impl_from_IReference_DOUBLE(IReference_DOUBLE *iface)
{
    return CONTAINING_RECORD(iface, struct reference_double, IReference_DOUBLE_iface);
}

static HRESULT WINAPI reference_double_QueryInterface(IReference_DOUBLE *iface, REFIID iid, void **out)
{
    struct reference_double *impl = impl_from_IReference_DOUBLE(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IReference_DOUBLE))
    {
        *out = &impl->IReference_DOUBLE_iface;
        IInspectable_AddRef(*out);
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI reference_double_AddRef(IReference_DOUBLE *iface)
{
    struct reference_double *impl = impl_from_IReference_DOUBLE(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI reference_double_Release(IReference_DOUBLE *iface)
{
    struct reference_double *impl = impl_from_IReference_DOUBLE(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref) free(impl);
    return ref;
}

static HRESULT WINAPI reference_double_GetIids(IReference_DOUBLE *iface, ULONG *iid_count, IID **iids)
{
    static const IID * const vtbl_iids[] = { &IID_IReference_DOUBLE };

    FIXME("iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids);
    return get_iids(vtbl_iids, ARRAY_SIZE(vtbl_iids), iid_count, iids);
}

static HRESULT WINAPI reference_double_GetRuntimeClassName(IReference_DOUBLE *iface, HSTRING *class_name)
{
    static const WCHAR name[] = {'W','i','n','d','o','w','s','.','F','o','u','n','d','a','t','i','o','n','.',
                                 'I','R','e','f','e','r','e','n','c','e','`','D','o','u','b','l','e',0};

    TRACE("iface %p, class_name %p.\n", iface, class_name);
    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, class_name);
}

static HRESULT WINAPI reference_double_GetTrustLevel(IReference_DOUBLE *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI reference_double_get_Value(IReference_DOUBLE *iface, DOUBLE *value)
{
    struct reference_double *impl = impl_from_IReference_DOUBLE(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->value;
    return S_OK;
}

static const struct IReference_DOUBLEVtbl reference_double_vtbl =
{
    reference_double_QueryInterface,
    reference_double_AddRef,
    reference_double_Release,
    reference_double_GetIids,
    reference_double_GetRuntimeClassName,
    reference_double_GetTrustLevel,
    reference_double_get_Value,
};

static HRESULT reference_double_create(double value, struct reference_double **ret)
{
    struct reference_double *impl;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IReference_DOUBLE_iface.lpVtbl = &reference_double_vtbl;
    impl->ref = 1;
    impl->value = value;
    *ret = impl;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * Geocoordinate.
 * ------------------------------------------------------------------------- */

struct geocoordinate
{
    IGeocoordinate IGeocoordinate_iface;
    struct weak_reference_source weak_reference_source;
    double latitude;
    double longitude;
    double accuracy;
    double altitude;
    DateTime timestamp;
    struct reference_double *altitude_ref;
    struct reference_double *altitude_accuracy_ref;
    struct reference_double *heading_ref;
    struct reference_double *speed_ref;
};

static inline struct geocoordinate *impl_from_IGeocoordinate(IGeocoordinate *iface)
{
    return CONTAINING_RECORD(iface, struct geocoordinate, IGeocoordinate_iface);
}

static HRESULT geocoordinate_get_box(struct geocoordinate *impl, struct reference_double **box,
                                     double value, IReference_DOUBLE **out)
{
    HRESULT hr;

    if (!*box)
    {
        if (FAILED(hr = reference_double_create(value, box))) return hr;
    }
    *out = &(*box)->IReference_DOUBLE_iface;
    IReference_DOUBLE_AddRef(*out);
    return S_OK;
}

static HRESULT WINAPI geocoordinate_QueryInterface(IGeocoordinate *iface, REFIID iid, void **out)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IGeocoordinate))
    {
        *out = &impl->IGeocoordinate_iface;
        IInspectable_AddRef(*out);
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
    {
        *out = &impl->weak_reference_source.IWeakReferenceSource_iface;
        IWeakReferenceSource_AddRef(*out);
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI geocoordinate_AddRef(IGeocoordinate *iface)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);
    return weak_reference_strong_add_ref(&impl->weak_reference_source);
}

static ULONG WINAPI geocoordinate_Release(IGeocoordinate *iface)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);
    ULONG ref = weak_reference_strong_release(&impl->weak_reference_source);

    if (!ref)
    {
        if (impl->altitude_ref) IReference_DOUBLE_Release(&impl->altitude_ref->IReference_DOUBLE_iface);
        if (impl->altitude_accuracy_ref)
            IReference_DOUBLE_Release(&impl->altitude_accuracy_ref->IReference_DOUBLE_iface);
        if (impl->heading_ref) IReference_DOUBLE_Release(&impl->heading_ref->IReference_DOUBLE_iface);
        if (impl->speed_ref) IReference_DOUBLE_Release(&impl->speed_ref->IReference_DOUBLE_iface);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI geocoordinate_GetIids(IGeocoordinate *iface, ULONG *iid_count, IID **iids)
{
    static const IID * const vtbl_iids[] = { &IID_IGeocoordinate };

    FIXME("iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids);
    return get_iids(vtbl_iids, ARRAY_SIZE(vtbl_iids), iid_count, iids);
}

static HRESULT WINAPI geocoordinate_GetRuntimeClassName(IGeocoordinate *iface, HSTRING *class_name)
{
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    return WindowsCreateString(RuntimeClass_Windows_Devices_Geolocation_Geocoordinate,
                               ARRAY_SIZE(RuntimeClass_Windows_Devices_Geolocation_Geocoordinate) - 1,
                               class_name);
}

static HRESULT WINAPI geocoordinate_GetTrustLevel(IGeocoordinate *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI geocoordinate_get_Latitude(IGeocoordinate *iface, DOUBLE *value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->latitude;
    return S_OK;
}

static HRESULT WINAPI geocoordinate_get_Longitude(IGeocoordinate *iface, DOUBLE *value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->longitude;
    return S_OK;
}

static HRESULT WINAPI geocoordinate_get_Altitude(IGeocoordinate *iface, IReference_DOUBLE **value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    return geocoordinate_get_box(impl, &impl->altitude_ref, impl->altitude, value);
}

static HRESULT WINAPI geocoordinate_get_Accuracy(IGeocoordinate *iface, DOUBLE *value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->accuracy;
    return S_OK;
}

static HRESULT WINAPI geocoordinate_get_AltitudeAccuracy(IGeocoordinate *iface, IReference_DOUBLE **value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    return geocoordinate_get_box(impl, &impl->altitude_accuracy_ref, default_altitude, value);
}

static HRESULT WINAPI geocoordinate_get_Heading(IGeocoordinate *iface, IReference_DOUBLE **value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    return geocoordinate_get_box(impl, &impl->heading_ref, default_altitude, value);
}

static HRESULT WINAPI geocoordinate_get_Speed(IGeocoordinate *iface, IReference_DOUBLE **value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    return geocoordinate_get_box(impl, &impl->speed_ref, default_altitude, value);
}

static HRESULT WINAPI geocoordinate_get_Timestamp(IGeocoordinate *iface, DateTime *value)
{
    struct geocoordinate *impl = impl_from_IGeocoordinate(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->timestamp;
    return S_OK;
}

static const struct IGeocoordinateVtbl geocoordinate_vtbl =
{
    geocoordinate_QueryInterface,
    geocoordinate_AddRef,
    geocoordinate_Release,
    geocoordinate_GetIids,
    geocoordinate_GetRuntimeClassName,
    geocoordinate_GetTrustLevel,
    geocoordinate_get_Latitude,
    geocoordinate_get_Longitude,
    geocoordinate_get_Altitude,
    geocoordinate_get_Accuracy,
    geocoordinate_get_AltitudeAccuracy,
    geocoordinate_get_Heading,
    geocoordinate_get_Speed,
    geocoordinate_get_Timestamp,
};

static HRESULT geocoordinate_create(const struct geo_fix *fix, DateTime timestamp,
                                    struct geocoordinate **ret)
{
    struct geocoordinate *impl;
    HRESULT hr;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IGeocoordinate_iface.lpVtbl = &geocoordinate_vtbl;
    impl->latitude = fix->latitude;
    impl->longitude = fix->longitude;
    impl->accuracy = fix->accuracy;
    impl->altitude = fix->altitude;
    impl->timestamp = timestamp;
    if (FAILED(hr = weak_reference_source_init(&impl->weak_reference_source,
                                              (IUnknown *)&impl->IGeocoordinate_iface)))
    {
        free(impl);
        return hr;
    }
    *ret = impl;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * Geoposition.
 * ------------------------------------------------------------------------- */

struct geoposition
{
    IGeoposition IGeoposition_iface;
    struct weak_reference_source weak_reference_source;
    struct geocoordinate *coordinate;
};

static inline struct geoposition *impl_from_IGeoposition(IGeoposition *iface)
{
    return CONTAINING_RECORD(iface, struct geoposition, IGeoposition_iface);
}

static HRESULT WINAPI geoposition_QueryInterface(IGeoposition *iface, REFIID iid, void **out)
{
    struct geoposition *impl = impl_from_IGeoposition(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IGeoposition))
    {
        *out = &impl->IGeoposition_iface;
        IInspectable_AddRef(*out);
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
    {
        *out = &impl->weak_reference_source.IWeakReferenceSource_iface;
        IWeakReferenceSource_AddRef(*out);
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI geoposition_AddRef(IGeoposition *iface)
{
    struct geoposition *impl = impl_from_IGeoposition(iface);
    return weak_reference_strong_add_ref(&impl->weak_reference_source);
}

static ULONG WINAPI geoposition_Release(IGeoposition *iface)
{
    struct geoposition *impl = impl_from_IGeoposition(iface);
    ULONG ref = weak_reference_strong_release(&impl->weak_reference_source);

    if (!ref)
    {
        IGeocoordinate_Release(&impl->coordinate->IGeocoordinate_iface);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI geoposition_GetIids(IGeoposition *iface, ULONG *iid_count, IID **iids)
{
    static const IID * const vtbl_iids[] = { &IID_IGeoposition };

    FIXME("iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids);
    return get_iids(vtbl_iids, ARRAY_SIZE(vtbl_iids), iid_count, iids);
}

static HRESULT WINAPI geoposition_GetRuntimeClassName(IGeoposition *iface, HSTRING *class_name)
{
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    return WindowsCreateString(RuntimeClass_Windows_Devices_Geolocation_Geoposition,
                               ARRAY_SIZE(RuntimeClass_Windows_Devices_Geolocation_Geoposition) - 1,
                               class_name);
}

static HRESULT WINAPI geoposition_GetTrustLevel(IGeoposition *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI geoposition_get_Coordinate(IGeoposition *iface, IGeocoordinate **value)
{
    struct geoposition *impl = impl_from_IGeoposition(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = (IGeocoordinate *)&impl->coordinate->IGeocoordinate_iface;
    IGeocoordinate_AddRef(&impl->coordinate->IGeocoordinate_iface);
    return S_OK;
}

/* CivicAddress is 'Unsupported API' on every Windows that has it: the
 * position->address lookup returns the empty address, and NULL is the
 * documented 'no civic address for this fix' answer. */
static HRESULT WINAPI geoposition_get_CivicAddress(IGeoposition *iface, ICivicAddress **value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    *value = NULL;
    return S_OK;
}

static const struct IGeopositionVtbl geoposition_vtbl =
{
    geoposition_QueryInterface,
    geoposition_AddRef,
    geoposition_Release,
    geoposition_GetIids,
    geoposition_GetRuntimeClassName,
    geoposition_GetTrustLevel,
    geoposition_get_Coordinate,
    geoposition_get_CivicAddress,
};

static HRESULT geoposition_create(struct geocoordinate *coordinate, struct geoposition **ret)
{
    struct geoposition *impl;
    HRESULT hr;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IGeoposition_iface.lpVtbl = &geoposition_vtbl;
    impl->coordinate = coordinate;
    if (FAILED(hr = weak_reference_source_init(&impl->weak_reference_source,
                                              (IUnknown *)&impl->IGeoposition_iface)))
    {
        free(impl);
        return hr;
    }
    *ret = impl;
    return S_OK;
}

static HRESULT create_geoposition(struct geoposition **ret)
{
    struct geocoordinate *coordinate;
    struct geoposition *position;
    struct geo_fix fix;
    HRESULT hr;

    get_current_fix(&fix);
    if (FAILED(hr = geocoordinate_create(&fix, get_now(), &coordinate))) return hr;
    if (FAILED(hr = geoposition_create(coordinate, &position)))
    {
        IGeocoordinate_Release(&coordinate->IGeocoordinate_iface);
        return hr;
    }
    *ret = position;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * IAsyncOperation<Geoposition*>.
 *
 * The fix is already in hand when GetGeopositionAsync is called, so the
 * operation is created in the Completed state: put_Completed invokes the
 * handler synchronously, which is what the .NET / C++/WinRT awaiters drive.
 * ------------------------------------------------------------------------- */

struct geoposition_async
{
    IAsyncOperation_Geoposition IAsyncOperation_Geoposition_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    struct geoposition *result;
    IAsyncOperationCompletedHandler_Geoposition *handler;
    AsyncStatus status;
    BOOL closed;
};

static inline struct geoposition_async *impl_from_IAsyncOperation_Geoposition(
        IAsyncOperation_Geoposition *iface)
{
    return CONTAINING_RECORD(iface, struct geoposition_async, IAsyncOperation_Geoposition_iface);
}

static inline struct geoposition_async *impl_from_IAsyncInfo(IAsyncInfo *iface)
{
    return CONTAINING_RECORD(iface, struct geoposition_async, IAsyncInfo_iface);
}

static void geoposition_async_release_contents(struct geoposition_async *impl)
{
    if (impl->handler)
    {
        IAsyncOperationCompletedHandler_Geoposition_Release(impl->handler);
        impl->handler = NULL;
    }
    if (impl->result)
    {
        IGeoposition_Release(&impl->result->IGeoposition_iface);
        impl->result = NULL;
    }
}

static HRESULT WINAPI async_QueryInterface(IAsyncOperation_Geoposition *iface, REFIID iid, void **out)
{
    struct geoposition_async *impl = impl_from_IAsyncOperation_Geoposition(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IAsyncOperation_Geoposition))
    {
        *out = &impl->IAsyncOperation_Geoposition_iface;
        IInspectable_AddRef(*out);
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IAsyncInfo))
    {
        *out = &impl->IAsyncInfo_iface;
        IAsyncInfo_AddRef(*out);
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_AddRef(IAsyncOperation_Geoposition *iface)
{
    struct geoposition_async *impl = impl_from_IAsyncOperation_Geoposition(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI async_Release(IAsyncOperation_Geoposition *iface)
{
    struct geoposition_async *impl = impl_from_IAsyncOperation_Geoposition(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        geoposition_async_release_contents(impl);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI async_GetIids(IAsyncOperation_Geoposition *iface, ULONG *iid_count, IID **iids)
{
    static const IID * const vtbl_iids[] = { &IID_IAsyncOperation_Geoposition, &IID_IAsyncInfo };

    FIXME("iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids);
    return get_iids(vtbl_iids, ARRAY_SIZE(vtbl_iids), iid_count, iids);
}

static HRESULT WINAPI async_GetRuntimeClassName(IAsyncOperation_Geoposition *iface, HSTRING *class_name)
{
    static const WCHAR name[] = {'W','i','n','d','o','w','s','.','F','o','u','n','d','a','t','i','o','n','.',
                                 'I','A','s','y','n','c','O','p','e','r','a','t','i','o','n','<','G','e','o','p',
                                 'o','s','i','t','i','o','n','>','>'};
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    return WindowsCreateString(name, ARRAY_SIZE(name), class_name);
}

static HRESULT WINAPI async_GetTrustLevel(IAsyncOperation_Geoposition *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI async_put_Completed(IAsyncOperation_Geoposition *iface,
                                          IAsyncOperationCompletedHandler_Geoposition *handler)
{
    struct geoposition_async *impl = impl_from_IAsyncOperation_Geoposition(iface);

    TRACE("iface %p, handler %p.\n", iface, handler);

    if (impl->handler) IAsyncOperationCompletedHandler_Geoposition_Release(impl->handler);
    impl->handler = handler;
    if (handler)
    {
        IAsyncOperationCompletedHandler_Geoposition_AddRef(handler);
        if (impl->status != Started)
            IAsyncOperationCompletedHandler_Geoposition_Invoke(handler, iface, impl->status);
    }
    return S_OK;
}

static HRESULT WINAPI async_get_Completed(IAsyncOperation_Geoposition *iface,
                                          IAsyncOperationCompletedHandler_Geoposition **handler)
{
    struct geoposition_async *impl = impl_from_IAsyncOperation_Geoposition(iface);

    TRACE("iface %p, handler %p.\n", iface, handler);

    *handler = impl->handler;
    if (*handler) IAsyncOperationCompletedHandler_Geoposition_AddRef(*handler);
    return S_OK;
}

static HRESULT WINAPI async_GetResults(IAsyncOperation_Geoposition *iface, IGeoposition **result)
{
    struct geoposition_async *impl = impl_from_IAsyncOperation_Geoposition(iface);

    TRACE("iface %p, result %p.\n", iface, result);

    if (impl->closed || !impl->result) return E_ILLEGAL_METHOD_CALL;
    *result = (IGeoposition *)&impl->result->IGeoposition_iface;
    IGeoposition_AddRef(&impl->result->IGeoposition_iface);
    return S_OK;
}

static const struct IAsyncOperation_GeopositionVtbl async_vtbl =
{
    async_QueryInterface,
    async_AddRef,
    async_Release,
    async_GetIids,
    async_GetRuntimeClassName,
    async_GetTrustLevel,
    async_put_Completed,
    async_get_Completed,
    async_GetResults,
};

static HRESULT WINAPI async_info_QueryInterface(IAsyncInfo *iface, REFIID iid, void **out)
{
    IInspectable *inspectable = (IInspectable *)iface;

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return IInspectable_QueryInterface(inspectable, iid, out);
}

static ULONG WINAPI async_info_AddRef(IAsyncInfo *iface)
{
    struct geoposition_async *impl = impl_from_IAsyncInfo(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI async_info_Release(IAsyncInfo *iface)
{
    struct geoposition_async *impl = impl_from_IAsyncInfo(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        geoposition_async_release_contents(impl);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI async_info_GetIids(IAsyncInfo *iface, ULONG *iid_count, IID **iids)
{
    static const IID * const vtbl_iids[] = { &IID_IAsyncInfo };

    FIXME("iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids);
    return get_iids(vtbl_iids, ARRAY_SIZE(vtbl_iids), iid_count, iids);
}

static HRESULT WINAPI async_info_GetRuntimeClassName(IAsyncInfo *iface, HSTRING *class_name)
{
    static const WCHAR name[] = {'W','i','n','d','o','w','s','.','F','o','u','n','d','a','t','i','o','n','.',
                                 'I','A','s','y','n','c','I','n','f','o',0};
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, class_name);
}

static HRESULT WINAPI async_info_GetTrustLevel(IAsyncInfo *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI async_info_get_Id(IAsyncInfo *iface, UINT32 *id)
{
    TRACE("iface %p, id %p.\n", iface, id);
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI async_info_get_Status(IAsyncInfo *iface, AsyncStatus *status)
{
    struct geoposition_async *impl = impl_from_IAsyncInfo(iface);

    TRACE("iface %p, status %p.\n", iface, status);
    *status = impl->status;
    return S_OK;
}

static HRESULT WINAPI async_info_get_ErrorCode(IAsyncInfo *iface, HRESULT *error_code)
{
    TRACE("iface %p, error_code %p.\n", iface, error_code);
    *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_info_Cancel(IAsyncInfo *iface)
{
    struct geoposition_async *impl = impl_from_IAsyncInfo(iface);

    TRACE("iface %p.\n", iface);
    if (impl->status == Started) impl->status = Canceled;
    return S_OK;
}

static HRESULT WINAPI async_info_Close(IAsyncInfo *iface)
{
    struct geoposition_async *impl = impl_from_IAsyncInfo(iface);

    TRACE("iface %p.\n", iface);
    geoposition_async_release_contents(impl);
    impl->closed = TRUE;
    impl->status = Canceled;
    return S_OK;
}

static const struct IAsyncInfoVtbl async_info_vtbl =
{
    async_info_QueryInterface,
    async_info_AddRef,
    async_info_Release,
    async_info_GetIids,
    async_info_GetRuntimeClassName,
    async_info_GetTrustLevel,
    async_info_get_Id,
    async_info_get_Status,
    async_info_get_ErrorCode,
    async_info_Cancel,
    async_info_Close,
};

static HRESULT geoposition_async_create(struct geoposition *result, struct geoposition_async **ret)
{
    struct geoposition_async *impl;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IAsyncOperation_Geoposition_iface.lpVtbl = &async_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &async_info_vtbl;
    impl->ref = 1;
    impl->result = result;
    impl->status = Completed;
    *ret = impl;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * Geolocator.
 * ------------------------------------------------------------------------- */

struct event_node
{
    struct list entry;
    EventRegistrationToken token;
    IUnknown *handler;
};

struct geolocator
{
    IGeolocator IGeolocator_iface;
    struct weak_reference_source weak_reference_source;
    PositionAccuracy accuracy;
    DOUBLE movement_threshold;
    UINT32 report_interval;
    LONG token_counter;
    struct list position_changed;
    struct list status_changed;
};

static inline struct geolocator *impl_from_IGeolocator(IGeolocator *iface)
{
    return CONTAINING_RECORD(iface, struct geolocator, IGeolocator_iface);
}

static void event_list_clear(struct list *list)
{
    struct event_node *node, *tmp;

    LIST_FOR_EACH_ENTRY_SAFE(node, tmp, list, struct event_node, entry)
    {
        IUnknown_Release(node->handler);
        list_remove(&node->entry);
        free(node);
    }
}

static HRESULT event_list_add(struct list *list, LONG *counter, IUnknown *handler,
                              EventRegistrationToken *token)
{
    struct event_node *node;

    if (!(node = calloc(1, sizeof(*node)))) return E_OUTOFMEMORY;
    node->token.value = InterlockedIncrement(counter);
    node->handler = handler;
    IUnknown_AddRef(handler);
    list_add_tail(list, &node->entry);
    *token = node->token;
    return S_OK;
}

static HRESULT event_list_remove(struct list *list, EventRegistrationToken token)
{
    struct event_node *node;

    LIST_FOR_EACH_ENTRY(node, list, struct event_node, entry)
    {
        if (node->token.value != token.value) continue;
        IUnknown_Release(node->handler);
        list_remove(&node->entry);
        free(node);
        return S_OK;
    }
    return S_OK;
}

static HRESULT WINAPI geolocator_QueryInterface(IGeolocator *iface, REFIID iid, void **out)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IGeolocator))
    {
        *out = &impl->IGeolocator_iface;
        IInspectable_AddRef(*out);
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
    {
        *out = &impl->weak_reference_source.IWeakReferenceSource_iface;
        IWeakReferenceSource_AddRef(*out);
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI geolocator_AddRef(IGeolocator *iface)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);
    ULONG ref = weak_reference_strong_add_ref(&impl->weak_reference_source);

    TRACE("iface %p increasing refcount to %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI geolocator_Release(IGeolocator *iface)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);
    ULONG ref = weak_reference_strong_release(&impl->weak_reference_source);

    TRACE("iface %p decreasing refcount to %lu.\n", iface, ref);
    if (!ref)
    {
        event_list_clear(&impl->position_changed);
        event_list_clear(&impl->status_changed);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI geolocator_GetIids(IGeolocator *iface, ULONG *iid_count, IID **iids)
{
    static const IID * const vtbl_iids[] = { &IID_IGeolocator };

    FIXME("iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids);
    return get_iids(vtbl_iids, ARRAY_SIZE(vtbl_iids), iid_count, iids);
}

static HRESULT WINAPI geolocator_GetRuntimeClassName(IGeolocator *iface, HSTRING *class_name)
{
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    return WindowsCreateString(RuntimeClass_Windows_Devices_Geolocation_Geolocator,
                               ARRAY_SIZE(RuntimeClass_Windows_Devices_Geolocation_Geolocator) - 1,
                               class_name);
}

static HRESULT WINAPI geolocator_GetTrustLevel(IGeolocator *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    *trust_level = FullTrust;
    return S_OK;
}

HRESULT WINAPI geolocator_get_DesiredAccuracy(IGeolocator *iface, PositionAccuracy *value)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->accuracy;
    return S_OK;
}

HRESULT WINAPI geolocator_set_DesiredAccuracy(IGeolocator *iface, PositionAccuracy value)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, value %d.\n", iface, value);
    impl->accuracy = value;
    return S_OK;
}

HRESULT WINAPI geolocator_get_MovementThreshold(IGeolocator *iface, DOUBLE *value)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->movement_threshold;
    return S_OK;
}

HRESULT WINAPI geolocator_set_MovementThreshold(IGeolocator *iface, DOUBLE value)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, value %f.\n", iface, value);
    impl->movement_threshold = value;
    return S_OK;
}

HRESULT WINAPI geolocator_get_ReportInterval(IGeolocator *iface, UINT32 *value)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    *value = impl->report_interval;
    return S_OK;
}

HRESULT WINAPI geolocator_set_ReportInterval(IGeolocator *iface, UINT32 value)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, value %u.\n", iface, value);
    impl->report_interval = value;
    return S_OK;
}

/* The fix is always in hand (built-in default, GeoClue on top of it), so the
 * service answers Ready from the first call: apps that gate their UI on
 * LocationStatus get a usable position immediately. */
HRESULT WINAPI geolocator_get_LocationStatus(IGeolocator *iface, PositionStatus *value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    *value = PositionStatus_Ready;
    return S_OK;
}

static HRESULT geolocator_get_async(IGeolocator *iface, IAsyncOperation_Geoposition **value)
{
    struct geoposition_async *async;
    struct geoposition *position;
    HRESULT hr;

    if (FAILED(hr = create_geoposition(&position))) return hr;
    if (FAILED(hr = geoposition_async_create(position, &async)))
    {
        IGeoposition_Release(&position->IGeoposition_iface);
        return hr;
    }
    *value = &async->IAsyncOperation_Geoposition_iface;
    return S_OK;
}

HRESULT WINAPI geolocator_GetGeopositionAsync(IGeolocator *iface, IAsyncOperation_Geoposition **value)
{
    TRACE("iface %p, value %p.\n", iface, value);
    return geolocator_get_async(iface, value);
}

HRESULT WINAPI geolocator_GetGeopositionAsyncWithAgeAndTimeout(IGeolocator *iface, TimeSpan maximum_age,
                                                               TimeSpan timeout,
                                                               IAsyncOperation_Geoposition **value)
{
    TRACE("iface %p, maximum_age %s, timeout %s, value %p.\n", iface,
          wine_dbgstr_longlong(maximum_age.Duration), wine_dbgstr_longlong(timeout.Duration), value);
    return geolocator_get_async(iface, value);
}

HRESULT WINAPI geolocator_add_PositionChanged(IGeolocator *iface,
                                              ITypedEventHandler_Geolocator_PositionChangedEventArgs *handler,
                                              EventRegistrationToken *token)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, handler %p, token %p.\n", iface, handler, token);
    return event_list_add(&impl->position_changed, &impl->token_counter,
                          (IUnknown *)handler, token);
}

HRESULT WINAPI geolocator_remove_PositionChanged(IGeolocator *iface, EventRegistrationToken token)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, token %#I64x.\n", iface, token.value);
    return event_list_remove(&impl->position_changed, token);
}

HRESULT WINAPI geolocator_add_StatusChanged(IGeolocator *iface,
                                            ITypedEventHandler_Geolocator_StatusChangedEventArgs *handler,
                                            EventRegistrationToken *token)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, handler %p, token %p.\n", iface, handler, token);
    return event_list_add(&impl->status_changed, &impl->token_counter, (IUnknown *)handler, token);
}

HRESULT WINAPI geolocator_remove_StatusChanged(IGeolocator *iface, EventRegistrationToken token)
{
    struct geolocator *impl = impl_from_IGeolocator(iface);

    TRACE("iface %p, token %#I64x.\n", iface, token.value);
    return event_list_remove(&impl->status_changed, token);
}

static const struct IGeolocatorVtbl geolocator_vtbl =
{
    geolocator_QueryInterface,
    geolocator_AddRef,
    geolocator_Release,
    geolocator_GetIids,
    geolocator_GetRuntimeClassName,
    geolocator_GetTrustLevel,
    geolocator_get_DesiredAccuracy,
    geolocator_set_DesiredAccuracy,
    geolocator_get_MovementThreshold,
    geolocator_set_MovementThreshold,
    geolocator_get_ReportInterval,
    geolocator_set_ReportInterval,
    geolocator_get_LocationStatus,
    geolocator_GetGeopositionAsync,
    geolocator_GetGeopositionAsyncWithAgeAndTimeout,
    geolocator_add_PositionChanged,
    geolocator_remove_PositionChanged,
    geolocator_add_StatusChanged,
    geolocator_remove_StatusChanged,
};

static HRESULT geolocator_create(struct geolocator **ret)
{
    struct geolocator *impl;
    HRESULT hr;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IGeolocator_iface.lpVtbl = &geolocator_vtbl;
    impl->accuracy = PositionAccuracy_Default;
    impl->report_interval = 3000;
    list_init(&impl->position_changed);
    list_init(&impl->status_changed);
    if (FAILED(hr = weak_reference_source_init(&impl->weak_reference_source,
                                              (IUnknown *)&impl->IGeolocator_iface)))
    {
        free(impl);
        return hr;
    }
    *ret = impl;
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * Activation factory / class factory.
 * ------------------------------------------------------------------------- */

struct geolocator_statics
{
    IActivationFactory IActivationFactory_iface;
    IClassFactory IClassFactory_iface;
    LONG ref;
};

static inline struct geolocator_statics *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct geolocator_statics, IActivationFactory_iface);
}

static inline struct geolocator_statics *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct geolocator_statics, IClassFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct geolocator_statics *impl = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef(*out);
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_IClassFactory))
    {
        *out = &impl->IClassFactory_iface;
        IClassFactory_AddRef(*out);
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    struct geolocator_statics *impl = impl_from_IActivationFactory(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    struct geolocator_statics *impl = impl_from_IActivationFactory(iface);
    return InterlockedDecrement(&impl->ref);
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *iid_count, IID **iids)
{
    static const IID * const vtbl_iids[] = { &IID_IActivationFactory };

    FIXME("iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids);
    return get_iids(vtbl_iids, ARRAY_SIZE(vtbl_iids), iid_count, iids);
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    return WindowsCreateString(RuntimeClass_Windows_Devices_Geolocation_Geolocator,
                               ARRAY_SIZE(RuntimeClass_Windows_Devices_Geolocation_Geolocator) - 1,
                               class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    struct geolocator *impl;
    HRESULT hr;

    TRACE("iface %p, instance %p.\n", iface, instance);

    if (FAILED(hr = geolocator_create(&impl)))
    {
        *instance = NULL;
        return hr;
    }
    *instance = (IInspectable *)&impl->IGeolocator_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl;

static HRESULT WINAPI classfactory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    IUnknown *inspectable = (IUnknown *)iface;

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return IUnknown_QueryInterface(inspectable, iid, out);
}

static ULONG WINAPI classfactory_AddRef(IClassFactory *iface)
{
    struct geolocator_statics *impl = impl_from_IClassFactory(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI classfactory_Release(IClassFactory *iface)
{
    struct geolocator_statics *impl = impl_from_IClassFactory(iface);
    return InterlockedDecrement(&impl->ref);
}

static HRESULT WINAPI classfactory_CreateInstance(IClassFactory *iface, IUnknown *outer,
                                                  REFIID iid, void **out)
{
    struct geolocator *impl;
    HRESULT hr;

    TRACE("iface %p, outer %p, iid %s, out %p.\n", iface, outer, debugstr_guid(iid), out);

    if (outer && !(IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory)))
        return CLASS_E_NOAGGREGATION;

    if (FAILED(hr = geolocator_create(&impl)))
    {
        *out = NULL;
        return hr;
    }
    hr = IGeolocator_QueryInterface(&impl->IGeolocator_iface, iid, out);
    IGeolocator_Release(&impl->IGeolocator_iface);
    return hr;
}

static HRESULT WINAPI classfactory_LockServer(IClassFactory *iface, BOOL lock)
{
    struct geolocator_statics *impl = impl_from_IClassFactory(iface);

    TRACE("iface %p, lock %d.\n", iface, lock);
    if (lock) InterlockedIncrement(&impl->ref);
    else InterlockedDecrement(&impl->ref);
    return S_OK;
}

static const struct IClassFactoryVtbl classfactory_vtbl =
{
    classfactory_QueryInterface,
    classfactory_AddRef,
    classfactory_Release,
    classfactory_CreateInstance,
    classfactory_LockServer,
};

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static struct geolocator_statics geolocator_statics =
{
    {&factory_vtbl},
    {&classfactory_vtbl},
    1,
};

static IActivationFactory *geolocator_factory = &geolocator_statics.IActivationFactory_iface;

/* The WinRT class GUID of Windows.Devices.Geolocation.Geolocator; a packaged
 * app's manifest (or a prefix seed) can address the class through
 * CoCreateInstance instead of the activation factory. */
static const IID IID_GeolocatorClass =
    {0x946de4ee,0xab40,0xf0cc, {0x1e,0xbc,0x75,0xdc,0x0d,0x91,0xd0,0x2a}};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    TRACE("clsid %s, riid %s, out %p.\n", debugstr_guid(clsid), debugstr_guid(riid), out);

    if (!IsEqualGUID(clsid, &IID_GeolocatorClass)) return CLASS_E_CLASSNOTAVAILABLE;
    return IActivationFactory_QueryInterface(geolocator_factory, riid, out);
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    TRACE("classid %s, factory %p.\n", debugstr_hstring(classid), factory);

    *factory = NULL;

    if (!wcscmp(name, RuntimeClass_Windows_Devices_Geolocation_Geolocator) ||
        !wcscmp(name, RuntimeClass_Windows_Devices_Geolocation_Geoposition) ||
        !wcscmp(name, RuntimeClass_Windows_Devices_Geolocation_Geocoordinate))
        IActivationFactory_QueryInterface(geolocator_factory, &IID_IActivationFactory, (void **)factory);

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return geolocator_statics.ref > 1 ? S_FALSE : S_OK;
}
