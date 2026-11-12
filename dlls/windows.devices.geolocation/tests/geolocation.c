/*
 * WinRT Windows.Devices.Geolocation tests
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
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"

#include "initguid.h"
#include "roapi.h"

#include "weakreference.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Devices_Geolocation
#include "windows.devices.geolocation.h"

#include "wine/test.h"

static const WCHAR geolocator_name[] = L"Windows.Devices.Geolocation.Geolocator";

static void check_interface_(unsigned int line, void *obj, const IID *iid)
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface(iface, iid, (void **)&unk);
    ok_(__FILE__, line)(hr == S_OK, "got hr %#lx.\n", hr);
    if (hr == S_OK)
        IUnknown_Release(unk);
}
#define check_interface(obj, iid) check_interface_(__LINE__, obj, iid)

static void check_runtime_class_(unsigned int line, IInspectable *obj, const WCHAR *name)
{
    HSTRING str;
    HRESULT hr;

    hr = IInspectable_GetRuntimeClassName(obj, &str);
    ok_(__FILE__, line)(hr == S_OK, "got hr %#lx.\n", hr);
    if (hr == S_OK)
    {
        ok_(__FILE__, line)(!wcscmp(WindowsGetStringRawBuffer(str, NULL), name), "got wrong class name.\n");
        WindowsDeleteString(str);
    }
}
#define check_runtime_class(obj, name) check_runtime_class_(__LINE__, obj, name)

/* IAsyncOperationCompletedHandler<Geoposition*> sink, counts invocations. */
struct completed_handler
{
    IAsyncOperationCompletedHandler_Geoposition IAsyncOperationCompletedHandler_Geoposition_iface;
    LONG ref;
    LONG invoked;
    AsyncStatus status;
};

static inline struct completed_handler *impl_from_handler(
        IAsyncOperationCompletedHandler_Geoposition *iface)
{
    return CONTAINING_RECORD(iface, struct completed_handler,
                             IAsyncOperationCompletedHandler_Geoposition_iface);
}

static HRESULT WINAPI handler_QueryInterface(IAsyncOperationCompletedHandler_Geoposition *iface,
                                             REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IAsyncOperationCompletedHandler_Geoposition))
    {
        *out = iface;
        IAsyncOperationCompletedHandler_Geoposition_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef(IAsyncOperationCompletedHandler_Geoposition *iface)
{
    struct completed_handler *impl = impl_from_handler(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI handler_Release(IAsyncOperationCompletedHandler_Geoposition *iface)
{
    struct completed_handler *impl = impl_from_handler(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref) free(impl);
    return ref;
}

static HRESULT WINAPI handler_Invoke(IAsyncOperationCompletedHandler_Geoposition *iface,
                                     IAsyncOperation_Geoposition *sender, AsyncStatus status)
{
    struct completed_handler *impl = impl_from_handler(iface);

    ok(sender != NULL, "sender is NULL\n");
    impl->status = status;
    InterlockedIncrement(&impl->invoked);
    return S_OK;
}

static const struct IAsyncOperationCompletedHandler_GeopositionVtbl handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static struct completed_handler *create_completed_handler(void)
{
    struct completed_handler *handler = calloc(1, sizeof(*handler));

    handler->IAsyncOperationCompletedHandler_Geoposition_iface.lpVtbl = &handler_vtbl;
    handler->ref = 1;
    return handler;
}

/* TypedEventHandler<Geolocator*, PositionChangedEventArgs*> sink for the event tests. */
struct event_handler
{
    ITypedEventHandler_Geolocator_PositionChangedEventArgs
        ITypedEventHandler_Geolocator_PositionChangedEventArgs_iface;
    LONG ref;
};

static inline struct event_handler *impl_from_event_handler(
        ITypedEventHandler_Geolocator_PositionChangedEventArgs *iface)
{
    return CONTAINING_RECORD(iface, struct event_handler,
                             ITypedEventHandler_Geolocator_PositionChangedEventArgs_iface);
}

static HRESULT WINAPI event_handler_QueryInterface(
        ITypedEventHandler_Geolocator_PositionChangedEventArgs *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_Geolocator_PositionChangedEventArgs))
    {
        *out = iface;
        ITypedEventHandler_Geolocator_PositionChangedEventArgs_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI event_handler_AddRef(ITypedEventHandler_Geolocator_PositionChangedEventArgs *iface)
{
    struct event_handler *impl = impl_from_event_handler(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI event_handler_Release(ITypedEventHandler_Geolocator_PositionChangedEventArgs *iface)
{
    struct event_handler *impl = impl_from_event_handler(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref) free(impl);
    return ref;
}

static HRESULT WINAPI event_handler_Invoke(ITypedEventHandler_Geolocator_PositionChangedEventArgs *iface,
                                           IGeolocator *sender, IPositionChangedEventArgs *args)
{
    return S_OK;
}

static const struct ITypedEventHandler_Geolocator_PositionChangedEventArgsVtbl event_handler_vtbl =
{
    event_handler_QueryInterface,
    event_handler_AddRef,
    event_handler_Release,
    event_handler_Invoke,
};

static HRESULT activate_geolocator(IGeolocator **geolocator)
{
    IActivationFactory *factory;
    IInspectable *inspectable;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString(geolocator_name, ARRAYSIZE(geolocator_name) - 1, &str);
    if (FAILED(hr)) return hr;

    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w(geolocator_name));
        return S_FALSE;
    }
    if (FAILED(hr)) return hr;

    hr = IActivationFactory_ActivateInstance(factory, &inspectable);
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface(inspectable, &IID_IGeolocator, (void **)geolocator);
        IInspectable_Release(inspectable);
    }
    IActivationFactory_Release(factory);
    return hr;
}

static void test_basic(void)
{
    IInspectable *inspectable;
    IActivationFactory *factory;
    IGeolocator *geolocator;
    IGeolocator *geolocator2;
    IWeakReferenceSource *weak_reference_source;
    IWeakReference *weak_reference;
    IUnknown *unknown;
    void *dummy;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString(geolocator_name, ARRAYSIZE(geolocator_name) - 1, &str);
    ok(hr == S_OK, "got hr %#lx.\n", hr);

    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w(geolocator_name));
        return;
    }

    check_interface(factory, &IID_IUnknown);
    check_interface(factory, &IID_IInspectable);
    check_interface(factory, &IID_IActivationFactory);

    hr = IActivationFactory_ActivateInstance(factory, &inspectable);
    ok(hr == S_OK && inspectable, "got hr %#lx.\n", hr);

    check_interface(inspectable, &IID_IUnknown);
    check_interface(inspectable, &IID_IInspectable);
    check_interface(inspectable, &IID_IAgileObject);
    check_runtime_class(inspectable, L"Windows.Devices.Geolocation.Geolocator");

    hr = IInspectable_QueryInterface(inspectable, &IID_IGeolocator, (void **)&geolocator);
    ok(hr == S_OK && geolocator, "got hr %#lx.\n", hr);
    ok((void *)inspectable == (void *)geolocator, "Interfaces are not the same\n");

    IInspectable_Release(inspectable);

    hr = IGeolocator_QueryInterface(geolocator, &IID_IWeakReferenceSource, (void **)&weak_reference_source);
    ok(hr == S_OK && weak_reference_source, "got hr %#lx.\n", hr);

    hr = IWeakReferenceSource_GetWeakReference(weak_reference_source, &weak_reference);
    ok(hr == S_OK && weak_reference, "got hr %#lx.\n", hr);
    IWeakReferenceSource_Release(weak_reference_source);

    hr = IWeakReference_Resolve(weak_reference, &IID_IUnknown, (IInspectable **)&unknown);
    ok(hr == S_OK && unknown, "got hr %#lx.\n", hr);
    hr = IWeakReference_Resolve(weak_reference, &IID_IGeolocator, (IInspectable **)&geolocator2);
    ok(hr == S_OK && geolocator2, "got hr %#lx.\n", hr);
    hr = IWeakReference_Resolve(weak_reference, &IID_IInspectable, &inspectable);
    ok(hr == S_OK && inspectable, "got hr %#lx.\n", hr);
    ok((void *)inspectable == (void *)geolocator, "Interfaces are not the same\n");
    ok((void *)unknown == (void *)geolocator, "Interfaces are not the same\n");
    IUnknown_Release(unknown);
    IGeolocator_Release(geolocator2);
    IInspectable_Release(inspectable);

    dummy = (void *)0xdeadbeef;
    hr = IWeakReference_Resolve(weak_reference, &IID_IWeakReference, (IInspectable **)&dummy);
    ok(hr == E_NOINTERFACE && !dummy, "got hr %#lx.\n", hr);

    check_interface(weak_reference, &IID_IUnknown);
    check_interface(weak_reference, &IID_IWeakReference);
    hr = IWeakReference_QueryInterface(weak_reference, &IID_IGeolocator, &dummy);
    ok(hr == E_NOINTERFACE && !dummy, "got hr %#lx.\n", hr);

    IGeolocator_Release(geolocator);

    hr = IWeakReference_Resolve(weak_reference, &IID_IGeolocator, (IInspectable **)&geolocator2);
    ok(hr == S_OK && !geolocator2, "got hr %#lx.\n", hr);
    IWeakReference_Release(weak_reference);
    IActivationFactory_Release(factory);
}

static void test_properties(void)
{
    PositionAccuracy accuracy;
    PositionStatus status;
    ITypedEventHandler_Geolocator_PositionChangedEventArgs *event_handler;
    struct event_handler *handler;
    EventRegistrationToken token, token2;
    IGeolocator *geolocator;
    DOUBLE threshold;
    UINT32 interval;
    HRESULT hr;

    hr = activate_geolocator(&geolocator);
    if (hr == S_FALSE) return;
    ok(hr == S_OK, "got hr %#lx.\n", hr);

    accuracy = PositionAccuracy_High;
    hr = IGeolocator_get_DesiredAccuracy(geolocator, &accuracy);
    ok(hr == S_OK && accuracy == PositionAccuracy_Default, "got hr %#lx, accuracy %d.\n", hr, accuracy);

    hr = IGeolocator_put_DesiredAccuracy(geolocator, PositionAccuracy_High);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = IGeolocator_get_DesiredAccuracy(geolocator, &accuracy);
    ok(hr == S_OK && accuracy == PositionAccuracy_High, "got hr %#lx, accuracy %d.\n", hr, accuracy);

    threshold = 12.0;
    hr = IGeolocator_get_MovementThreshold(geolocator, &threshold);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = IGeolocator_put_MovementThreshold(geolocator, 25.0);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = IGeolocator_get_MovementThreshold(geolocator, &threshold);
    ok(hr == S_OK && threshold == 25.0, "got hr %#lx, threshold %f.\n", hr, threshold);

    interval = 1;
    hr = IGeolocator_get_ReportInterval(geolocator, &interval);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = IGeolocator_put_ReportInterval(geolocator, 5000);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = IGeolocator_get_ReportInterval(geolocator, &interval);
    ok(hr == S_OK && interval == 5000, "got hr %#lx, interval %u.\n", hr, interval);

    status = PositionStatus_NotAvailable;
    hr = IGeolocator_get_LocationStatus(geolocator, &status);
    ok(hr == S_OK && status == PositionStatus_Ready, "got hr %#lx, status %d.\n", hr, status);

    handler = calloc(1, sizeof(*handler));
    handler->ITypedEventHandler_Geolocator_PositionChangedEventArgs_iface.lpVtbl = &event_handler_vtbl;
    handler->ref = 1;
    event_handler = &handler->ITypedEventHandler_Geolocator_PositionChangedEventArgs_iface;

    token.value = 0;
    hr = IGeolocator_add_PositionChanged(geolocator, event_handler, &token);
    ok(hr == S_OK && token.value, "got hr %#lx, token %#I64x.\n", hr, token.value);

    token2.value = 0;
    hr = IGeolocator_add_PositionChanged(geolocator, event_handler, &token2);
    ok(hr == S_OK && token2.value && token2.value != token.value, "got hr %#lx.\n", hr);

    hr = IGeolocator_remove_PositionChanged(geolocator, token);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = IGeolocator_remove_PositionChanged(geolocator, token2);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    /* Removing an unknown token is not an error. */
    hr = IGeolocator_remove_PositionChanged(geolocator, token);
    ok(hr == S_OK, "got hr %#lx.\n", hr);

    ITypedEventHandler_Geolocator_PositionChangedEventArgs_Release(event_handler);
    IGeolocator_Release(geolocator);
}

static void test_geoposition(void)
{
    IAsyncOperationCompletedHandler_Geoposition *completed, *completed2;
    IAsyncInfo *info;
    UINT32 id;
    struct completed_handler *handler;
    IAsyncOperation_Geoposition *async;
    IReference_DOUBLE *ref;
    IGeoposition *position;
    IGeocoordinate *coordinate;
    ICivicAddress *civic;
    IGeolocator *geolocator;
    AsyncStatus status;
    DateTime timestamp;
    HRESULT hr;
    DOUBLE value;

    hr = activate_geolocator(&geolocator);
    if (hr == S_FALSE) return;
    ok(hr == S_OK, "got hr %#lx.\n", hr);

    async = NULL;
    hr = IGeolocator_GetGeopositionAsync(geolocator, &async);
    ok(hr == S_OK && async, "got hr %#lx.\n", hr);

    check_interface(async, &IID_IUnknown);
    check_interface(async, &IID_IInspectable);
    check_interface(async, &IID_IAsyncOperation_Geoposition);
    check_interface(async, &IID_IAsyncInfo);

    /* The implementation answers from an in-hand fix: the operation is
     * Completed at creation, so the awaiter's handler runs in put_Completed. */
    handler = create_completed_handler();
    completed = &handler->IAsyncOperationCompletedHandler_Geoposition_iface;
    hr = IAsyncOperation_Geoposition_put_Completed(async, completed);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    ok(handler->invoked == 1, "got invoked %ld.\n", handler->invoked);
    ok(handler->status == Completed, "got status %d.\n", handler->status);

    completed2 = NULL;
    hr = IAsyncOperation_Geoposition_get_Completed(async, &completed2);
    ok(hr == S_OK && completed2 == completed, "got hr %#lx.\n", hr);
    IAsyncOperationCompletedHandler_Geoposition_Release(completed2);

    status = Started;
    hr = IAsyncOperation_Geoposition_QueryInterface(async, &IID_IAsyncInfo, (void **)&info);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = IAsyncInfo_get_Status(info, &status);
    ok(hr == S_OK && status == Completed, "got hr %#lx, status %d.\n", hr, status);
    id = 0;
    hr = IAsyncInfo_get_Id(info, &id);
    ok(hr == S_OK && id, "got hr %#lx, id %u.\n", hr, id);
    hr = IAsyncInfo_get_ErrorCode(info, &hr);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    IAsyncInfo_Release(info);

    position = NULL;
    hr = IAsyncOperation_Geoposition_GetResults(async, &position);
    ok(hr == S_OK && position, "got hr %#lx.\n", hr);
    check_runtime_class((IInspectable *)position, L"Windows.Devices.Geolocation.Geoposition");

    civic = (void *)0xdeadbeef;
    hr = IGeoposition_get_CivicAddress(position, &civic);
    ok(hr == S_OK && !civic, "got hr %#lx, civic %p.\n", hr, civic);

    coordinate = NULL;
    hr = IGeoposition_get_Coordinate(position, &coordinate);
    ok(hr == S_OK && coordinate, "got hr %#lx.\n", hr);
    check_runtime_class((IInspectable *)coordinate, L"Windows.Devices.Geolocation.Geocoordinate");

    /* A second read must hand out the same coordinate object (agile identity). */
    {
        IGeocoordinate *coordinate2;
        hr = IGeoposition_get_Coordinate(position, &coordinate2);
        ok(hr == S_OK && (void *)coordinate2 == (void *)coordinate, "got hr %#lx.\n", hr);
        IGeocoordinate_Release(coordinate2);
    }

    value = 0;
    hr = IGeocoordinate_get_Latitude(coordinate, &value);
    ok(hr == S_OK && value >= -90.0 && value <= 90.0 && value, "got hr %#lx, latitude %f.\n", hr, value);

    value = 0;
    hr = IGeocoordinate_get_Longitude(coordinate, &value);
    ok(hr == S_OK && value >= -180.0 && value <= 180.0 && value, "got hr %#lx, longitude %f.\n", hr, value);

    value = 0;
    hr = IGeocoordinate_get_Accuracy(coordinate, &value);
    ok(hr == S_OK && value > 0.0, "got hr %#lx, accuracy %f.\n", hr, value);

    ref = NULL;
    hr = IGeocoordinate_get_Altitude(coordinate, &ref);
    ok(hr == S_OK && ref, "got hr %#lx.\n", hr);
    if (ref)
    {
        value = 1;
        hr = IReference_DOUBLE_get_Value(ref, &value);
        ok(hr == S_OK && value == 0.0, "got hr %#lx, altitude %f.\n", hr, value);
        IReference_DOUBLE_Release(ref);
    }

    ref = NULL;
    hr = IGeocoordinate_get_Heading(coordinate, &ref);
    ok(hr == S_OK && ref, "got hr %#lx.\n", hr);
    if (ref) IReference_DOUBLE_Release(ref);

    ref = NULL;
    hr = IGeocoordinate_get_Speed(coordinate, &ref);
    ok(hr == S_OK && ref, "got hr %#lx.\n", hr);
    if (ref) IReference_DOUBLE_Release(ref);

    ref = NULL;
    hr = IGeocoordinate_get_AltitudeAccuracy(coordinate, &ref);
    ok(hr == S_OK && ref, "got hr %#lx.\n", hr);
    if (ref) IReference_DOUBLE_Release(ref);

    timestamp.UniversalTime = 0;
    hr = IGeocoordinate_get_Timestamp(coordinate, &timestamp);
    ok(hr == S_OK && timestamp.UniversalTime, "got hr %#lx, timestamp %I64u.\n", hr,
       timestamp.UniversalTime);

    /* The coordinate outlives the position it was read from. */
    IGeoposition_Release(position);

    IGeocoordinate_Release(coordinate);
    IAsyncOperation_Geoposition_Release(async);
    IAsyncOperationCompletedHandler_Geoposition_Release(completed);
    IGeolocator_Release(geolocator);
}

static void test_geoposition_overloads(void)
{
    IGeolocator *geolocator;
    IAsyncOperation_Geoposition *async, *async2;
    IGeoposition *position;
    TimeSpan age, timeout;
    HRESULT hr;

    hr = activate_geolocator(&geolocator);
    if (hr == S_FALSE) return;
    ok(hr == S_OK, "got hr %#lx.\n", hr);

    age.Duration = 10 * 10000000ll;   /* 1 s */
    timeout.Duration = 100 * 10000000ll; /* 10 s */
    async = NULL;
    hr = IGeolocator_GetGeopositionAsyncWithAgeAndTimeout(geolocator, age, timeout, &async);
    ok(hr == S_OK && async, "got hr %#lx.\n", hr);

    position = NULL;
    hr = IAsyncOperation_Geoposition_GetResults(async, &position);
    ok(hr == S_OK && position, "got hr %#lx.\n", hr);
    IGeoposition_Release(position);

    /* Two operations return distinct Geoposition objects. */
    async2 = NULL;
    hr = IGeolocator_GetGeopositionAsync(geolocator, &async2);
    ok(hr == S_OK && async2, "got hr %#lx.\n", hr);
    position = NULL;
    hr = IAsyncOperation_Geoposition_GetResults(async2, &position);
    ok(hr == S_OK && position, "got hr %#lx.\n", hr);
    IGeoposition_Release(position);

    /* Cancel/Close drive the operation out of the Completed state. */
    {
        IAsyncInfo *info;
        AsyncStatus status;

        hr = IAsyncOperation_Geoposition_QueryInterface(async2, &IID_IAsyncInfo, (void **)&info);
        ok(hr == S_OK, "got hr %#lx.\n", hr);
        hr = IAsyncInfo_Close(info);
        ok(hr == S_OK, "got hr %#lx.\n", hr);
        status = Completed;
        hr = IAsyncInfo_get_Status(info, &status);
        ok(hr == S_OK && status != Completed, "got hr %#lx, status %d.\n", hr, status);
        IAsyncInfo_Release(info);
    }

    IAsyncOperation_Geoposition_Release(async2);
    IAsyncOperation_Geoposition_Release(async);
    IGeolocator_Release(geolocator);
}

START_TEST(geolocation)
{
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "RoInitialize failed, hr %#lx\n", hr);

    test_basic();
    test_properties();
    test_geoposition();
    test_geoposition_overloads();

    RoUninitialize();
}
