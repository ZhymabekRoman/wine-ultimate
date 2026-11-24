/*
 * Copyright 2022 Zhiyi Zhang for CodeWeavers
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

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"

#include "initguid.h"
#include "roapi.h"
#include "rometadataresolution.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Foundation_Metadata
#include "windows.foundation.h"
#include "windows.foundation.metadata.h"
#include "wintypes_test.h"

#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.streams.h"

#include "robuffer.h"
#include "weakreference.h"

#include "wine/test.h"

static BOOL is_wow64;

#define check_interface(obj, iid, supported) check_interface_(__LINE__, obj, iid, supported)
static void check_interface_(unsigned int line, void *obj, const IID *iid, BOOL supported)
{
    HRESULT hr, expected_hr;
    IUnknown *iface = obj;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;
    hr = IUnknown_QueryInterface(iface, iid, (void **)&unk);
    ok_(__FILE__, line)(hr == expected_hr, "Got hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
}

static void test_interfaces(void)
{
    static WCHAR class_name[1024];
    IActivationFactory *factory;
    IUnknown *unk;
    HSTRING str;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    wcscpy(class_name, L"Windows.Foundation.Metadata.ApiInformation");
    hr = WindowsCreateString(class_name, wcslen(class_name), &str);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG) /* pre-win8 */, "Got hr %#lx.\n", hr);
    if (hr == S_OK)
    {
        check_interface(factory, &IID_IUnknown, TRUE);
        check_interface(factory, &IID_IInspectable, TRUE);
        check_interface(factory, &IID_IAgileObject, TRUE);
        check_interface(factory, &IID_IActivationFactory, TRUE);
        check_interface(factory, &IID_IApiInformationStatics, TRUE);
        check_interface(factory, &IID_IPropertyValueStatics, FALSE);
        IActivationFactory_Release(factory);
    }
    else
        win_skip("%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w(class_name));
    WindowsDeleteString(str);

    wcscpy(class_name, L"Windows.Foundation.PropertyValue");
    hr = WindowsCreateString(class_name, wcslen(class_name), &str);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    check_interface(factory, &IID_IUnknown, TRUE);
    check_interface(factory, &IID_IInspectable, TRUE);
    check_interface(factory, &IID_IAgileObject, TRUE);
    check_interface(factory, &IID_IActivationFactory, TRUE);
    check_interface(factory, &IID_IApiInformationStatics, FALSE);
    check_interface(factory, &IID_IPropertyValueStatics, TRUE);
    IActivationFactory_Release(factory);
    WindowsDeleteString(str);

    wcscpy(class_name, L"Windows.Storage.Streams.DataWriter");
    hr = WindowsCreateString(class_name, wcslen(class_name), &str);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    check_interface(factory, &IID_IUnknown, TRUE);
    check_interface(factory, &IID_IInspectable, TRUE);
    check_interface(factory, &IID_IAgileObject, TRUE);
    check_interface(factory, &IID_IActivationFactory, TRUE);
    todo_wine check_interface(factory, &IID_IDataWriterFactory, TRUE);
    check_interface(factory, &IID_IRandomAccessStreamReferenceStatics, FALSE);
    check_interface(factory, &IID_IApiInformationStatics, FALSE);
    check_interface(factory, &IID_IPropertyValueStatics, FALSE);
    IActivationFactory_Release(factory);
    WindowsDeleteString(str);

    wcscpy(class_name, L"Windows.Storage.Streams.RandomAccessStreamReference");
    hr = WindowsCreateString(class_name, wcslen(class_name), &str);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    check_interface(factory, &IID_IUnknown, TRUE);
    check_interface(factory, &IID_IInspectable, TRUE);
    hr = IActivationFactory_QueryInterface(factory, &IID_IAgileObject, (void **)&unk);
    ok(hr == S_OK || broken(hr == E_NOINTERFACE) /* pre win10 v1809 */, "Got hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    check_interface(factory, &IID_IActivationFactory, TRUE);
    check_interface(factory, &IID_IDataWriterFactory, FALSE);
    check_interface(factory, &IID_IRandomAccessStreamReferenceStatics, TRUE);
    check_interface(factory, &IID_IApiInformationStatics, FALSE);
    check_interface(factory, &IID_IPropertyValueStatics, FALSE);
    IActivationFactory_Release(factory);
    WindowsDeleteString(str);

    RoUninitialize();
}

static void test_IBufferStatics(void)
{
    static const WCHAR *class_name = L"Windows.Storage.Streams.Buffer";
    IBufferByteAccess *buffer_byte_access = NULL;
    IBufferFactory *buffer_factory = NULL;
    IActivationFactory *factory = NULL;
    UINT32 capacity, length;
    IBuffer *buffer = NULL;
    HSTRING str;
    HRESULT hr;
    BYTE *data;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "RoInitialize failed, hr %#lx.\n", hr);

    hr = WindowsCreateString(class_name, wcslen(class_name), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "RoGetActivationFactory failed, hr %#lx.\n", hr);
    WindowsDeleteString(str);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w(class_name));
        RoUninitialize();
        return;
    }

    check_interface(factory, &IID_IUnknown, TRUE);
    check_interface(factory, &IID_IInspectable, TRUE);
    check_interface(factory, &IID_IAgileObject, TRUE);
    check_interface(factory, &IID_IBufferByteAccess, FALSE);

    hr = IActivationFactory_QueryInterface(factory, &IID_IBufferFactory, (void **)&buffer_factory);
    ok(hr == S_OK, "QueryInterface IID_IBufferFactory failed, hr %#lx.\n", hr);

    if (0) /* Crash on Windows */
    {
    hr = IBufferFactory_Create(buffer_factory, 0, NULL);
    ok(hr == E_INVALIDARG, "IBufferFactory_Create failed, hr %#lx.\n", hr);
    }

    hr = IBufferFactory_Create(buffer_factory, 0, &buffer);
    ok(hr == S_OK, "IBufferFactory_Create failed, hr %#lx.\n", hr);

    check_interface(buffer, &IID_IAgileObject, TRUE);

    if (0) /* Crash on Windows */
    {
    hr = IBuffer_get_Capacity(buffer, NULL);
    ok(hr == E_INVALIDARG, "IBuffer_get_Capacity failed, hr %#lx.\n", hr);
    }

    capacity = 0xdeadbeef;
    hr = IBuffer_get_Capacity(buffer, &capacity);
    ok(hr == S_OK, "IBuffer_get_Capacity failed, hr %#lx.\n", hr);
    ok(capacity == 0, "IBuffer_get_Capacity returned capacity %u.\n", capacity);

    if (0) /* Crash on Windows */
    {
    hr = IBuffer_get_Length(buffer, NULL);
    ok(hr == E_INVALIDARG, "IBuffer_get_Length failed, hr %#lx.\n", hr);
    }

    length = 0xdeadbeef;
    hr = IBuffer_get_Length(buffer, &length);
    ok(hr == S_OK, "IBuffer_get_Length failed, hr %#lx.\n", hr);
    ok(length == 0, "IBuffer_get_Length returned length %u.\n", length);

    hr = IBuffer_put_Length(buffer, 1);
    ok(hr == E_INVALIDARG, "IBuffer_put_Length failed, hr %#lx.\n", hr);

    hr = IBuffer_QueryInterface(buffer, &IID_IBufferByteAccess, (void **)&buffer_byte_access);
    ok(hr == S_OK, "QueryInterface IID_IBufferByteAccess failed, hr %#lx.\n", hr);

    check_interface(buffer_byte_access, &IID_IInspectable, TRUE);
    check_interface(buffer_byte_access, &IID_IAgileObject, TRUE);
    check_interface(buffer_byte_access, &IID_IBuffer, TRUE);

    if (0) /* Crash on Windows */
    {
    hr = IBufferByteAccess_Buffer(buffer_byte_access, NULL);
    ok(hr == E_INVALIDARG, "IBufferByteAccess_Buffer failed, hr %#lx.\n", hr);
    }

    data = NULL;
    hr = IBufferByteAccess_Buffer(buffer_byte_access, &data);
    ok(hr == S_OK, "IBufferByteAccess_Buffer failed, hr %#lx.\n", hr);
    ok(data != NULL, "IBufferByteAccess_Buffer returned NULL data.\n");

    IBufferByteAccess_Release(buffer_byte_access);
    IBuffer_Release(buffer);

    hr = IBufferFactory_Create(buffer_factory, 100, &buffer);
    ok(hr == S_OK, "IBufferFactory_Create failed, hr %#lx.\n", hr);

    capacity = 0;
    hr = IBuffer_get_Capacity(buffer, &capacity);
    ok(hr == S_OK, "IBuffer_get_Capacity failed, hr %#lx.\n", hr);
    ok(capacity == 100, "IBuffer_get_Capacity returned capacity %u.\n", capacity);

    length = 0xdeadbeef;
    hr = IBuffer_get_Length(buffer, &length);
    ok(hr == S_OK, "IBuffer_get_Length failed, hr %#lx.\n", hr);
    ok(length == 0, "IBuffer_get_Length returned length %u.\n", length);

    hr = IBuffer_put_Length(buffer, 1);
    ok(hr == S_OK, "IBuffer_put_Length failed, hr %#lx.\n", hr);
    length = 0xdeadbeef;
    hr = IBuffer_get_Length(buffer, &length);
    ok(hr == S_OK, "IBuffer_get_Length failed, hr %#lx.\n", hr);
    ok(length == 1, "IBuffer_get_Length returned length %u.\n", length);

    hr = IBuffer_put_Length(buffer, 100 + 1);
    ok(hr == E_INVALIDARG, "IBuffer_put_Length failed, hr %#lx.\n", hr);

    hr = IBuffer_put_Length(buffer, 100);
    ok(hr == S_OK, "IBuffer_put_Length failed, hr %#lx.\n", hr);
    length = 0;
    hr = IBuffer_get_Length(buffer, &length);
    ok(hr == S_OK, "IBuffer_get_Length failed, hr %#lx.\n", hr);
    ok(length == 100, "IBuffer_get_Length returned length %u.\n", length);

    hr = IBuffer_QueryInterface(buffer, &IID_IBufferByteAccess, (void **)&buffer_byte_access);
    ok(hr == S_OK, "QueryInterface IID_IBufferByteAccess failed, hr %#lx.\n", hr);

    hr = IBufferByteAccess_Buffer(buffer_byte_access, &data);
    ok(hr == S_OK, "IBufferByteAccess_Buffer failed, hr %#lx.\n", hr);

    /* Windows does not zero out data when changing Length */

    hr = IBuffer_put_Length(buffer, 0);
    ok(hr == S_OK, "IBuffer_put_Length failed, hr %#lx.\n", hr);
    data[0] = 1;
    data[10] = 10;
    length = 0xdeadbeef;
    hr = IBuffer_get_Length(buffer, &length);
    ok(hr == S_OK, "IBuffer_get_Length failed, hr %#lx.\n", hr);
    ok(length == 0, "IBuffer_get_Length returned length %u.\n", length);
    hr = IBuffer_put_Length(buffer, 1);
    ok(hr == S_OK, "IBuffer_put_Length failed, hr %#lx.\n", hr);
    ok(data[0] == 1, "Buffer returned %#x.\n", data[0]);
    ok(data[10] == 10, "Buffer returned %#x.\n", data[10]);

    IBufferByteAccess_Release(buffer_byte_access);
    IBuffer_Release(buffer);
    IBufferFactory_Release(buffer_factory);
    IActivationFactory_Release(factory);
    RoUninitialize();
}

static void test_IApiInformationStatics(void)
{
    static const struct
    {
        const WCHAR *name;
        unsigned int max_major;
    }
    present_contracts[] =
    {
        { L"Windows.Foundation.UniversalApiContract", 10, },
    };

    static const WCHAR *class_name = L"Windows.Foundation.Metadata.ApiInformation";
    IAgileObject *agile_object = NULL, *tmp_agile_object = NULL;
    IInspectable *inspectable = NULL, *tmp_inspectable = NULL;
    IApiInformationStatics *statics = NULL;
    IActivationFactory *factory = NULL;
    HSTRING str, str2;
    unsigned int i, j;
    BOOLEAN ret;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "RoInitialize failed, hr %#lx.\n", hr);

    hr = WindowsCreateString(class_name, wcslen(class_name), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "RoGetActivationFactory failed, hr %#lx.\n", hr);
    WindowsDeleteString(str);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w(class_name));
        RoUninitialize();
        return;
    }

    hr = IActivationFactory_QueryInterface(factory, &IID_IInspectable, (void **)&inspectable);
    ok(hr == S_OK, "QueryInterface IID_IInspectable failed, hr %#lx.\n", hr);

    hr = IActivationFactory_QueryInterface(factory, &IID_IAgileObject, (void **)&agile_object);
    ok(hr == S_OK, "QueryInterface IID_IAgileObject failed, hr %#lx.\n", hr);

    hr = IActivationFactory_QueryInterface(factory, &IID_IApiInformationStatics, (void **)&statics);
    ok(hr == S_OK, "QueryInterface IID_IApiInformationStatics failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_QueryInterface(statics, &IID_IInspectable, (void **)&tmp_inspectable);
    ok(hr == S_OK, "QueryInterface IID_IInspectable failed, hr %#lx.\n", hr);
    ok(tmp_inspectable == inspectable, "QueryInterface IID_IInspectable returned %p, expected %p.\n",
            tmp_inspectable, inspectable);
    IInspectable_Release(tmp_inspectable);

    hr = IApiInformationStatics_QueryInterface(statics, &IID_IAgileObject, (void **)&tmp_agile_object);
    ok(hr == S_OK, "QueryInterface IID_IAgileObject failed, hr %#lx.\n", hr);
    ok(tmp_agile_object == agile_object, "QueryInterface IID_IAgileObject returned %p, expected %p.\n",
            tmp_agile_object, agile_object);
    IAgileObject_Release(tmp_agile_object);

    /* IsTypePresent() */
    hr = WindowsCreateString(L"Windows.Foundation.FoundationContract",
            wcslen(L"Windows.Foundation.FoundationContract"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsTypePresent(statics, NULL, &ret);
    ok(hr == E_INVALIDARG, "IsTypePresent failed, hr %#lx.\n", hr);

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsTypePresent(statics, str, NULL);
    ok(hr == E_INVALIDARG, "IsTypePresent failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsTypePresent(statics, str, &ret);
    todo_wine
    ok(hr == S_OK, "IsTypePresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsTypePresent returned FALSE.\n");

    WindowsDeleteString(str);

    /* IsMethodPresent() */
    hr = WindowsCreateString(L"Windows.Foundation.Metadata.IApiInformationStatics",
            wcslen(L"Windows.Foundation.Metadata.IApiInformationStatics"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);
    hr = WindowsCreateString(L"IsTypePresent", wcslen(L"IsTypePresent"), &str2);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsMethodPresent(statics, NULL, str2, &ret);
    ok(hr == E_INVALIDARG, "IsMethodPresent failed, hr %#lx.\n", hr);

    ret = TRUE;
    hr = IApiInformationStatics_IsMethodPresent(statics, str, NULL, &ret);
    ok(hr == S_OK, "IsMethodPresent failed, hr %#lx.\n", hr);
    ok(ret == FALSE, "IsMethodPresent returned TRUE.\n");

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsMethodPresent(statics, str, str2, NULL);
    ok(hr == E_INVALIDARG, "IsMethodPresent failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsMethodPresent(statics, str, str2, &ret);
    ok(hr == S_OK, "IsMethodPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsMethodPresent returned FALSE.\n");

    /* IsMethodPresentWithArity() */
    hr = IApiInformationStatics_IsMethodPresentWithArity(statics, NULL, str2, 1, &ret);
    ok(hr == E_INVALIDARG, "IsMethodPresentWithArity failed, hr %#lx.\n", hr);

    ret = TRUE;
    hr = IApiInformationStatics_IsMethodPresentWithArity(statics, str, NULL, 1, &ret);
    todo_wine
    ok(hr == S_OK, "IsMethodPresentWithArity failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsMethodPresentWithArity returned FALSE.\n");

    ret = TRUE;
    hr = IApiInformationStatics_IsMethodPresentWithArity(statics, str, str2, 0, &ret);
    todo_wine
    ok(hr == S_OK, "IsMethodPresentWithArity failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsMethodPresentWithArity returned FALSE.\n");

    ret = TRUE;
    hr = IApiInformationStatics_IsMethodPresentWithArity(statics, str, str2, 2, &ret);
    todo_wine
    ok(hr == S_OK, "IsMethodPresentWithArity failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsMethodPresentWithArity returned FALSE.\n");

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsMethodPresentWithArity(statics, str, str2, 1, NULL);
    ok(hr == E_INVALIDARG, "IsMethodPresentWithArity failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsMethodPresentWithArity(statics, str, str2, 1, &ret);
    todo_wine
    ok(hr == S_OK, "IsMethodPresentWithArity failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsMethodPresentWithArity returned FALSE.\n");

    WindowsDeleteString(str2);
    WindowsDeleteString(str);

    /* IsEventPresent() */
    hr = WindowsCreateString(L"Windows.Devices.Enumeration.IDeviceWatcher",
            wcslen(L"Windows.Devices.Enumeration.IDeviceWatcher"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);
    hr = WindowsCreateString(L"Added", wcslen(L"Added"), &str2);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsEventPresent(statics, NULL, str2, &ret);
    ok(hr == E_INVALIDARG, "IsEventPresent failed, hr %#lx.\n", hr);

    ret = TRUE;
    hr = IApiInformationStatics_IsEventPresent(statics, str, NULL, &ret);
    todo_wine
    ok(hr == S_OK, "IsEventPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsEventPresent returned FALSE.\n");

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsEventPresent(statics, str, str2, NULL);
    ok(hr == E_INVALIDARG, "IsEventPresent failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsEventPresent(statics, str, str2, &ret);
    todo_wine
    ok(hr == S_OK, "IsEventPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsEventPresent returned FALSE.\n");

    WindowsDeleteString(str2);
    WindowsDeleteString(str);

    /* IsPropertyPresent() */
    hr = WindowsCreateString(L"Windows.Devices.Enumeration.IDeviceWatcher",
            wcslen(L"Windows.Devices.Enumeration.IDeviceWatcher"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);
    hr = WindowsCreateString(L"Status", wcslen(L"Status"), &str2);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsPropertyPresent(statics, NULL, str2, &ret);
    ok(hr == E_INVALIDARG, "IsPropertyPresent failed, hr %#lx.\n", hr);

    ret = TRUE;
    hr = IApiInformationStatics_IsPropertyPresent(statics, str, NULL, &ret);
    todo_wine
    ok(hr == S_OK, "IsPropertyPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsPropertyPresent returned TRUE.\n");

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsPropertyPresent(statics, str, str2, NULL);
    ok(hr == E_INVALIDARG, "IsPropertyPresent failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsPropertyPresent(statics, str, str2, &ret);
    todo_wine
    ok(hr == S_OK, "IsPropertyPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsPropertyPresent returned FALSE.\n");

    WindowsDeleteString(str2);
    WindowsDeleteString(str);

    /* IsReadOnlyPropertyPresent() */
    hr = WindowsCreateString(L"Windows.Devices.Enumeration.IDeviceWatcher",
            wcslen(L"Windows.Devices.Enumeration.IDeviceWatcher"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);
    hr = WindowsCreateString(L"Id", wcslen(L"Id"), &str2);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsReadOnlyPropertyPresent(statics, NULL, str2, &ret);
    ok(hr == E_INVALIDARG, "IsReadOnlyPropertyPresent failed, hr %#lx.\n", hr);

    ret = TRUE;
    hr = IApiInformationStatics_IsReadOnlyPropertyPresent(statics, str, NULL, &ret);
    todo_wine
    ok(hr == S_OK, "IsReadOnlyPropertyPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsReadOnlyPropertyPresent returned TRUE.\n");

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsReadOnlyPropertyPresent(statics, str, str2, NULL);
    ok(hr == E_INVALIDARG, "IsReadOnlyPropertyPresent failed, hr %#lx.\n", hr);
    }

    ret = TRUE;
    hr = IApiInformationStatics_IsReadOnlyPropertyPresent(statics, str, str2, &ret);
    todo_wine
    ok(hr == S_OK, "IsReadOnlyPropertyPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsReadOnlyPropertyPresent returned TRUE.\n");

    WindowsDeleteString(str2);
    WindowsDeleteString(str);

    /* IsWriteablePropertyPresent() */
    hr = WindowsCreateString(L"Windows.Gaming.Input.ForceFeedback.IForceFeedbackEffect",
            wcslen(L"Windows.Gaming.Input.ForceFeedback.IForceFeedbackEffect"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);
    hr = WindowsCreateString(L"Gain", wcslen(L"Gain"), &str2);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsWriteablePropertyPresent(statics, NULL, str2, &ret);
    ok(hr == E_INVALIDARG, "IsWriteablePropertyPresent failed, hr %#lx.\n", hr);

    ret = TRUE;
    hr = IApiInformationStatics_IsWriteablePropertyPresent(statics, str, NULL, &ret);
    todo_wine
    ok(hr == S_OK, "IsWriteablePropertyPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsWriteablePropertyPresent returned TRUE.\n");

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsWriteablePropertyPresent(statics, str, str2, NULL);
    ok(hr == E_INVALIDARG, "IsWriteablePropertyPresent failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsWriteablePropertyPresent(statics, str, str2, &ret);
    todo_wine
    ok(hr == S_OK, "IsWriteablePropertyPresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE || broken(ret == FALSE) /* Win10 1507 */,
            "IsWriteablePropertyPresent returned FALSE.\n");

    WindowsDeleteString(str2);
    WindowsDeleteString(str);

    /* IsEnumNamedValuePresent */
    hr = WindowsCreateString(L"Windows.Foundation.Metadata.GCPressureAmount",
            wcslen(L"Windows.Foundation.Metadata.GCPressureAmount"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);
    hr = WindowsCreateString(L"Low", wcslen(L"Low"), &str2);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsEnumNamedValuePresent(statics, NULL, str2, &ret);
    ok(hr == E_INVALIDARG, "IsEnumNamedValuePresent failed, hr %#lx.\n", hr);

    ret = TRUE;
    hr = IApiInformationStatics_IsEnumNamedValuePresent(statics, str, NULL, &ret);
    todo_wine
    ok(hr == S_OK, "IsEnumNamedValuePresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsEnumNamedValuePresent returned TRUE.\n");

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsEnumNamedValuePresent(statics, str, str2, NULL);
    ok(hr == E_INVALIDARG, "IsEnumNamedValuePresent failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsEnumNamedValuePresent(statics, str, str2, &ret);
    todo_wine
    ok(hr == S_OK, "IsEnumNamedValuePresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsEnumNamedValuePresent returned FALSE.\n");

    ret = TRUE;
    hr = IApiInformationStatics_IsEnumNamedValuePresent(statics, str, str, &ret);
    todo_wine
    ok(hr == S_OK, "IsEnumNamedValuePresent failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsEnumNamedValuePresent returned TRUE.\n");

    WindowsDeleteString(str2);
    WindowsDeleteString(str);

    /* IsApiContractPresentByMajor */
    hr = WindowsCreateString(L"Windows.Foundation.FoundationContract",
            wcslen(L"Windows.Foundation.FoundationContract"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsApiContractPresentByMajor(statics, NULL, 1, &ret);
    ok(hr == E_INVALIDARG, "IsApiContractPresentByMajor failed, hr %#lx.\n", hr);

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsApiContractPresentByMajor(statics, str, 1, NULL);
    ok(hr == E_INVALIDARG, "IsApiContractPresentByMajor failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsApiContractPresentByMajor(statics, str, 1, &ret);
    ok(hr == S_OK, "IsApiContractPresentByMajor failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsApiContractPresentByMajor returned FALSE.\n");

    ret = FALSE;
    hr = IApiInformationStatics_IsApiContractPresentByMajor(statics, str, 0, &ret);
    ok(hr == S_OK, "IsApiContractPresentByMajor failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsApiContractPresentByMajor returned FALSE.\n");

    ret = TRUE;
    hr = IApiInformationStatics_IsApiContractPresentByMajor(statics, str, 999, &ret);
    ok(hr == S_OK, "IsApiContractPresentByMajor failed, hr %#lx.\n", hr);
    ok(ret == FALSE, "IsApiContractPresentByMajor returned TRUE.\n");

    WindowsDeleteString(str);

    /* IsApiContractPresentByMajorAndMinor */
    hr = WindowsCreateString(L"Windows.Foundation.FoundationContract",
            wcslen(L"Windows.Foundation.FoundationContract"), &str);
    ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);

    hr = IApiInformationStatics_IsApiContractPresentByMajorAndMinor(statics, NULL, 1, 0, &ret);
    ok(hr == E_INVALIDARG, "IsApiContractPresentByMajorAndMinor failed, hr %#lx.\n", hr);

    if (0) /* Crash on Windows */
    {
    hr = IApiInformationStatics_IsApiContractPresentByMajorAndMinor(statics, str, 1, 0, NULL);
    ok(hr == E_INVALIDARG, "IsApiContractPresentByMajorAndMinor failed, hr %#lx.\n", hr);
    }

    ret = FALSE;
    hr = IApiInformationStatics_IsApiContractPresentByMajorAndMinor(statics, str, 1, 0, &ret);
    todo_wine
    ok(hr == S_OK, "IsApiContractPresentByMajorAndMinor failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsApiContractPresentByMajorAndMinor returned FALSE.\n");

    ret = FALSE;
    hr = IApiInformationStatics_IsApiContractPresentByMajorAndMinor(statics, str, 0, 999, &ret);
    todo_wine
    ok(hr == S_OK, "IsApiContractPresentByMajorAndMinor failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE, "IsApiContractPresentByMajorAndMinor returned FALSE.\n");

    ret = FALSE;
    hr = IApiInformationStatics_IsApiContractPresentByMajorAndMinor(statics, str, 1, 999, &ret);
    todo_wine
    ok(hr == S_OK, "IsApiContractPresentByMajorAndMinor failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == TRUE || broken(ret == FALSE) /* Win10 1507 */,
            "IsApiContractPresentByMajorAndMinor returned FALSE.\n");

    ret = TRUE;
    hr = IApiInformationStatics_IsApiContractPresentByMajorAndMinor(statics, str, 999, 999, &ret);
    todo_wine
    ok(hr == S_OK, "IsApiContractPresentByMajorAndMinor failed, hr %#lx.\n", hr);
    todo_wine
    ok(ret == FALSE, "IsApiContractPresentByMajorAndMinor returned TRUE.\n");

    WindowsDeleteString(str);

    /* Test API contracts presence. */
    for (i = 0; i < ARRAY_SIZE(present_contracts); ++i)
    {
        hr = WindowsCreateString(present_contracts[i].name, wcslen(present_contracts[i].name), &str);
        ok(hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr);
        for (j = 0; j <= present_contracts[i].max_major; ++j)
        {
            ret = FALSE;
            hr = IApiInformationStatics_IsApiContractPresentByMajor(statics, str, i, &ret);
            ok(hr == S_OK, "IsApiContractPresentByMajor failed, hr %#lx, i %u, major %u.\n", hr, i, j);
            ok(ret == TRUE, "IsApiContractPresentByMajor returned FALSE, i %u, major %u.\n", i, j);
        }
        WindowsDeleteString(str);
    }

    IApiInformationStatics_Release(statics);
    IAgileObject_Release(agile_object);
    IInspectable_Release(inspectable);
    IActivationFactory_Release(factory);
    RoUninitialize();
}

static void test_IPropertyValueStatics(void)
{
    static const WCHAR *class_name = L"Windows.Foundation.PropertyValue";
    static const BYTE byte_value = 0x12;
    static const INT16 int16_value = 0x1234;
    static const UINT16 uint16_value = 0x1234;
    static const INT32 int32_value= 0x1234abcd;
    static const UINT32 uint32_value = 0x1234abcd;
    static const INT64 int64_value = 0x12345678abcdef;
    static const UINT64 uint64_value = 0x12345678abcdef;
    static const FLOAT float_value = 1.5;
    static const DOUBLE double_value = 1.5;
    static const WCHAR wchar_value = 0x1234;
    static const boolean boolean_value = TRUE;
    static const struct DateTime datetime_value = {0x12345678abcdef};
    static const struct TimeSpan timespan_value = {0x12345678abcdef};
    static const struct Point point_value = {1, 2};
    static const struct Size size_value = {1, 2};
    static const struct Rect rect_value = {1, 2, 3, 4};
    IAgileObject *agile_object = NULL, *tmp_agile_object = NULL;
    IInspectable *inspectable = NULL, *tmp_inspectable = NULL;
    IPropertyValueStatics *statics = NULL;
    IActivationFactory *factory = NULL;
    IReference_BYTE *iref_byte;
    IReference_INT16 *iref_int16;
    IReference_INT32 *iref_int32;
    IReference_UINT32 *iref_uint32;
    IReference_INT64 *iref_int64;
    IReference_UINT64 *iref_uint64;
    IReference_boolean *iref_boolean;
    IReference_HSTRING *iref_hstring;
    IReference_FLOAT *iref_float;
    IReference_DOUBLE *iref_double;
    IReference_DateTime *iref_datetime;
    IReference_TimeSpan *iref_timespan;
    IReference_GUID *iref_guid;
    IReference_Point *iref_point;
    IReference_Size *iref_size;
    IReference_Rect *iref_rect;
    IPropertyValue *value = NULL;
    enum PropertyType type;
    unsigned int i, count;
    BYTE byte, *ptr_byte;
    HSTRING str, ret_str;
    INT16 ret_int16;
    INT32 ret_int32;
    UINT32 ret_uint32;
    INT64 ret_int64;
    UINT64 ret_uint64;
    FLOAT ret_float;
    DOUBLE ret_double;
    struct DateTime ret_datetime;
    struct TimeSpan ret_timespan;
    GUID ret_guid;
    struct Point ret_point;
    struct Size ret_size;
    struct Rect ret_rect;
    boolean ret;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = WindowsCreateString(class_name, wcslen(class_name), &str);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "RoGetActivationFactory failed, hr %#lx.\n", hr);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w(class_name));
        WindowsDeleteString(str);
        RoUninitialize();
        return;
    }

    hr = IActivationFactory_QueryInterface(factory, &IID_IInspectable, (void **)&inspectable);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = IActivationFactory_QueryInterface(factory, &IID_IAgileObject, (void **)&agile_object);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = IActivationFactory_QueryInterface(factory, &IID_IPropertyValueStatics, (void **)&statics);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValueStatics_QueryInterface(statics, &IID_IInspectable, (void **)&tmp_inspectable);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(tmp_inspectable == inspectable, "QueryInterface IID_IInspectable returned %p, expected %p.\n",
            tmp_inspectable, inspectable);
    IInspectable_Release(tmp_inspectable);
    IInspectable_Release(inspectable);

    hr = IPropertyValueStatics_QueryInterface(statics, &IID_IAgileObject, (void **)&tmp_agile_object);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(tmp_agile_object == agile_object, "QueryInterface IID_IAgileObject returned %p, expected %p.\n",
            tmp_agile_object, agile_object);
    IAgileObject_Release(tmp_agile_object);
    IAgileObject_Release(agile_object);

    /* Parameter checks */
    hr = IPropertyValueStatics_CreateUInt8(statics, 0x12, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValueStatics_CreateUInt8(statics, 0x12, &inspectable);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = IInspectable_QueryInterface(inspectable, &IID_IPropertyValue, (void **)&value);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(value == (IPropertyValue *)inspectable, "Expected the same pointer.\n");
    IInspectable_Release(inspectable);

    hr = IPropertyValue_get_Type(value, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValue_GetBoolean(value, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValue_GetBoolean(value, &ret);
    ok(hr == TYPE_E_TYPEMISMATCH, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValueStatics_CreateString(statics, (HSTRING)-1, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    IPropertyValue_Release(value);

    /* Parameter checks for array types */
    hr = IPropertyValueStatics_CreateUInt8Array(statics, 1, NULL, &inspectable);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValueStatics_CreateUInt8Array(statics, 1, &byte, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValueStatics_CreateUInt8Array(statics, 1, &byte, &inspectable);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = IInspectable_QueryInterface(inspectable, &IID_IPropertyValue, (void **)&value);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(value == (IPropertyValue *)inspectable, "Expected the same pointer.\n");
    IInspectable_Release(inspectable);

    hr = IPropertyValue_get_Type(value, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValue_GetBoolean(value, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValue_GetBoolean(value, &ret);
    ok(hr == TYPE_E_TYPEMISMATCH, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValue_GetUInt8Array(value, NULL, &ptr_byte);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValue_GetUInt8Array(value, &count, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = IPropertyValue_GetUInt8Array(value, &count, &ptr_byte);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    IPropertyValue_Release(value);

    /* PropertyType_Empty */
    hr = IPropertyValueStatics_CreateEmpty(statics, NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    inspectable = (IInspectable *)0xdeadbeef;
    hr = IPropertyValueStatics_CreateEmpty(statics, &inspectable);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(inspectable == NULL, "Got unexpected inspectable.\n");

    /* Test a single property value */
#define TEST_PROPERTY_VALUE(PROPERTY_TYPE, TYPE, VALUE)                                      \
    do                                                                                       \
    {                                                                                        \
        TYPE expected_value;                                                                 \
                                                                                             \
        inspectable = NULL;                                                                  \
        hr = IPropertyValueStatics_Create##PROPERTY_TYPE(statics, VALUE, &inspectable);      \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
        ok(inspectable != NULL, "Got unexpected inspectable.\n");                            \
                                                                                             \
        hr = IInspectable_QueryInterface(inspectable, &IID_IPropertyValue, (void **)&value); \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
        IInspectable_Release(inspectable);                                                   \
                                                                                             \
        hr = IPropertyValue_get_Type(value, &type);                                          \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
        ok(type == PropertyType_##PROPERTY_TYPE, "Got unexpected type %d.\n",                \
           PropertyType_##PROPERTY_TYPE);                                                    \
                                                                                             \
        ret = TRUE;                                                                          \
        hr = IPropertyValue_get_IsNumericScalar(value, &ret);                                \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
        ok(ret == FALSE, "Expected not numeric scalar.\n");                                  \
                                                                                             \
        hr = IPropertyValue_Get##PROPERTY_TYPE(value, &expected_value);                      \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
        ok(!memcmp(&VALUE, &expected_value, sizeof(VALUE)), "Got unexpected value.\n");      \
                                                                                             \
        IPropertyValue_Release(value);                                                       \
    } while (0);

    TEST_PROPERTY_VALUE(UInt8, BYTE, byte_value)
    TEST_PROPERTY_VALUE(Int16, INT16, int16_value)
    TEST_PROPERTY_VALUE(UInt16, UINT16, uint16_value)
    TEST_PROPERTY_VALUE(Int32, INT32, int32_value)
    TEST_PROPERTY_VALUE(UInt32, UINT32, uint32_value)
    TEST_PROPERTY_VALUE(Int64, INT64, int64_value)
    TEST_PROPERTY_VALUE(UInt64, UINT64, uint64_value)
    TEST_PROPERTY_VALUE(Single, FLOAT, float_value)
    TEST_PROPERTY_VALUE(Double, DOUBLE, double_value)
    TEST_PROPERTY_VALUE(Char16, WCHAR, wchar_value)
    TEST_PROPERTY_VALUE(Boolean, boolean, boolean_value)
    TEST_PROPERTY_VALUE(String, HSTRING, str)
    TEST_PROPERTY_VALUE(DateTime, DateTime, datetime_value)
    TEST_PROPERTY_VALUE(TimeSpan, TimeSpan, timespan_value)
    TEST_PROPERTY_VALUE(Guid, GUID, IID_IPropertyValue)
    TEST_PROPERTY_VALUE(Point, Point, point_value)
    TEST_PROPERTY_VALUE(Size, Size, size_value)
    TEST_PROPERTY_VALUE(Rect, Rect, rect_value)

#undef TEST_PROPERTY_VALUE

    /* Test property value array */
#define TEST_PROPERTY_COUNT 2
#define TEST_PROPERTY_VALUE_ARRAY(PROPERTY_TYPE, TYPE, VALUE)                                   \
    do                                                                                          \
    {                                                                                           \
        TYPE values[TEST_PROPERTY_COUNT], *expected_values;                                     \
                                                                                                \
        for (i = 0; i < TEST_PROPERTY_COUNT; i++)                                               \
            memcpy(&values[i], &VALUE, sizeof(VALUE));                                          \
                                                                                                \
        hr = IPropertyValueStatics_Create##PROPERTY_TYPE(statics, TEST_PROPERTY_COUNT, values,  \
                                                         NULL);                                 \
        ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);                                   \
                                                                                                \
        inspectable = NULL;                                                                     \
        hr = IPropertyValueStatics_Create##PROPERTY_TYPE(statics, TEST_PROPERTY_COUNT, values,  \
                                                         &inspectable);                         \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                        \
        ok(inspectable != NULL, "Got unexpected inspectable.\n");                               \
                                                                                                \
        hr = IInspectable_QueryInterface(inspectable, &IID_IPropertyValue, (void **)&value);    \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                        \
        IInspectable_Release(inspectable);                                                      \
                                                                                                \
        hr = IPropertyValue_get_Type(value, &type);                                             \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                        \
        ok(type == PropertyType_##PROPERTY_TYPE, "Got unexpected type %d.\n",                   \
           PropertyType_##PROPERTY_TYPE);                                                       \
                                                                                                \
        ret = TRUE;                                                                             \
        hr = IPropertyValue_get_IsNumericScalar(value, &ret);                                   \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                        \
        ok(ret == FALSE, "Expected not numeric scalar.\n");                                     \
                                                                                                \
        count = 0;                                                                              \
        hr = IPropertyValue_Get##PROPERTY_TYPE(value, &count, &expected_values);                \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                        \
        ok(count == TEST_PROPERTY_COUNT, "Got unexpected count %u.\n", count);                  \
        ok(expected_values != values, "Got same pointer.\n");                                   \
        for (i = 0; i < TEST_PROPERTY_COUNT; i++)                                               \
            ok(!memcmp(&VALUE, &expected_values[i], sizeof(VALUE)), "Got unexpected value.\n"); \
                                                                                                \
        IPropertyValue_Release(value);                                                          \
    } while (0);

    TEST_PROPERTY_VALUE_ARRAY(UInt8Array, BYTE, byte_value)
    TEST_PROPERTY_VALUE_ARRAY(Int16Array, INT16, int16_value)
    TEST_PROPERTY_VALUE_ARRAY(UInt16Array, UINT16, uint16_value)
    TEST_PROPERTY_VALUE_ARRAY(Int32Array, INT32, int32_value)
    TEST_PROPERTY_VALUE_ARRAY(UInt32Array, UINT32, uint32_value)
    TEST_PROPERTY_VALUE_ARRAY(Int64Array, INT64, int64_value)
    TEST_PROPERTY_VALUE_ARRAY(UInt64Array, UINT64, uint64_value)
    TEST_PROPERTY_VALUE_ARRAY(SingleArray, FLOAT, float_value)
    TEST_PROPERTY_VALUE_ARRAY(DoubleArray, DOUBLE, double_value)
    TEST_PROPERTY_VALUE_ARRAY(Char16Array, WCHAR, wchar_value)
    TEST_PROPERTY_VALUE_ARRAY(BooleanArray, boolean, boolean_value)
    TEST_PROPERTY_VALUE_ARRAY(StringArray, HSTRING, str)
    TEST_PROPERTY_VALUE_ARRAY(DateTimeArray, DateTime, datetime_value)
    TEST_PROPERTY_VALUE_ARRAY(TimeSpanArray, TimeSpan, timespan_value)
    TEST_PROPERTY_VALUE_ARRAY(GuidArray, GUID, IID_IPropertyValue)
    TEST_PROPERTY_VALUE_ARRAY(PointArray, Point, point_value)
    TEST_PROPERTY_VALUE_ARRAY(SizeArray, Size, size_value)
    TEST_PROPERTY_VALUE_ARRAY(RectArray, Rect, rect_value)

#undef TEST_PROPERTY_VALUE_ARRAY
#undef TEST_PROPERTY_COUNT

    /* Test IReference<*> interface */
#define TEST_PROPERTY_VALUE_IREFERENCE(TYPE, IFACE_TYPE, VALUE, RET_OBJ, RET_VALUE)          \
    do                                                                                       \
    {                                                                                        \
        hr = IPropertyValueStatics_Create##TYPE(statics, VALUE, &inspectable);               \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
                                                                                             \
        hr = IInspectable_QueryInterface(inspectable, &IID_IPropertyValue, (void **)&value); \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
        ok(value == (IPropertyValue *)inspectable, "Expected the same pointer.\n");          \
                                                                                             \
        hr = IPropertyValue_QueryInterface(value, &IID_##IFACE_TYPE, (void **)&RET_OBJ);     \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
                                                                                             \
        hr = IFACE_TYPE##_get_Value(RET_OBJ, &RET_VALUE);                                    \
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);                                     \
        ok(!memcmp(&RET_VALUE, &VALUE, sizeof(VALUE)), "Got unexpected value.\n");           \
                                                                                             \
        IFACE_TYPE##_Release(RET_OBJ);                                                       \
        IPropertyValue_Release(value);                                                       \
        IInspectable_Release(inspectable);                                                   \
    } while (0);

    TEST_PROPERTY_VALUE_IREFERENCE(UInt8, IReference_BYTE, byte_value, iref_byte, byte)
    TEST_PROPERTY_VALUE_IREFERENCE(Int16, IReference_INT16, int16_value, iref_int16, ret_int16)
    TEST_PROPERTY_VALUE_IREFERENCE(Int32, IReference_INT32, int32_value, iref_int32, ret_int32)
    TEST_PROPERTY_VALUE_IREFERENCE(UInt32, IReference_UINT32, uint32_value, iref_uint32, ret_uint32)
    TEST_PROPERTY_VALUE_IREFERENCE(Int64, IReference_INT64, int64_value, iref_int64, ret_int64)
    TEST_PROPERTY_VALUE_IREFERENCE(UInt64, IReference_UINT64, uint64_value, iref_uint64, ret_uint64)
    TEST_PROPERTY_VALUE_IREFERENCE(Boolean, IReference_boolean, boolean_value, iref_boolean, ret)
    TEST_PROPERTY_VALUE_IREFERENCE(String, IReference_HSTRING, str, iref_hstring, ret_str)
    TEST_PROPERTY_VALUE_IREFERENCE(Single, IReference_FLOAT, float_value, iref_float, ret_float)
    TEST_PROPERTY_VALUE_IREFERENCE(Double, IReference_DOUBLE, double_value, iref_double, ret_double)
    TEST_PROPERTY_VALUE_IREFERENCE(DateTime, IReference_DateTime, datetime_value, iref_datetime, ret_datetime)
    TEST_PROPERTY_VALUE_IREFERENCE(TimeSpan, IReference_TimeSpan, timespan_value, iref_timespan, ret_timespan)
    TEST_PROPERTY_VALUE_IREFERENCE(Guid, IReference_GUID, IID_IPropertyValue, iref_guid, ret_guid)
    TEST_PROPERTY_VALUE_IREFERENCE(Point, IReference_Point, point_value, iref_point, ret_point)
    TEST_PROPERTY_VALUE_IREFERENCE(Size, IReference_Size, size_value, iref_size, ret_size)
    TEST_PROPERTY_VALUE_IREFERENCE(Rect, IReference_Rect, rect_value, iref_rect, ret_rect)

#undef TEST_PROPERTY_VALUE_IREFERENCE

    /* Test IReference<UInt16>, IReference<Char16>, CreateInspectable, CreateInspectableArray, IReferenceArray<T>, IWeakReferenceSource */
    {
        static const GUID iid_iref_uint16 = { 0x5ab7d2c3, 0x6b62, 0x5e71, { 0xa4, 0xb6, 0x2d, 0x49, 0xc4, 0xf2, 0x38, 0xfd } };
        static const GUID iid_iref_char16 = { 0xfb393ef3, 0xbbac, 0x5bd5, { 0x91, 0x44, 0x84, 0xf2, 0x35, 0x76, 0xf4, 0x15 } };
        static const GUID iid_irefa_int32 = { 0xa6d080a5, 0xb087, 0x5bc2, { 0x9a, 0x9f, 0x5c, 0xd6, 0x87, 0xb4, 0xd1, 0xf7 } };
        static const GUID iid_irefa_insp  = { 0x9cd7a84f, 0x0c80, 0x59c5, { 0xb4, 0x4e, 0x97, 0x78, 0x41, 0xbb, 0x43, 0xd9 } };
        IInspectable *pv = NULL, *item1 = NULL, *item2 = NULL, *arr_in[2], **arr_out = NULL;
        IWeakReferenceSource *wrs = NULL;
        IWeakReference *wr = NULL;
        IInspectable *resolved = NULL;
        HSTRING cls = NULL;
        UINT16 u16 = 0;
        WCHAR ch = 0;
        INT32 ints[2] = { 10, 20 }, *ints_out = NULL;
        UINT32 cnt = 0;
        void *iref = NULL;

        /* IReference<UInt16> */
        hr = IPropertyValueStatics_CreateUInt16(statics, 0x1234, &pv);
        ok(hr == S_OK, "got %#lx\n", hr);
        hr = IInspectable_QueryInterface(pv, &iid_iref_uint16, &iref);
        ok(hr == S_OK, "QI IReference<UInt16> got %#lx\n", hr);
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, UINT16 *))(*(void ***)iref)[6])(iref, &u16);
        ok(hr == S_OK && u16 == 0x1234, "get_Value got %#lx, %#x\n", hr, u16);
        IUnknown_Release((IUnknown *)iref);
        hr = IInspectable_GetRuntimeClassName(pv, &cls);
        ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(cls, NULL), L"Windows.Foundation.IReference`1<UInt16>"), "got %s\n", wine_dbgstr_w(WindowsGetStringRawBuffer(cls, NULL)));
        WindowsDeleteString(cls);
        IInspectable_Release(pv);

        /* IReference<Char16> */
        hr = IPropertyValueStatics_CreateChar16(statics, L'Z', &pv);
        ok(hr == S_OK, "got %#lx\n", hr);
        hr = IInspectable_QueryInterface(pv, &iid_iref_char16, &iref);
        ok(hr == S_OK, "QI IReference<Char16> got %#lx\n", hr);
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, WCHAR *))(*(void ***)iref)[6])(iref, &ch);
        ok(hr == S_OK && ch == L'Z', "get_Value got %#lx, %#x\n", hr, ch);
        IUnknown_Release((IUnknown *)iref);
        IInspectable_Release(pv);

        /* CreateInspectable */
        hr = IPropertyValueStatics_CreateInspectable(statics, NULL, &pv);
        ok(hr == S_OK && pv == NULL, "CreateInspectable(NULL) got %#lx, %p\n", hr, pv);
        hr = IPropertyValueStatics_CreateInt32(statics, 42, &item1);
        ok(hr == S_OK, "got %#lx\n", hr);
        hr = IPropertyValueStatics_CreateInspectable(statics, item1, &pv);
        ok(hr == S_OK && pv == item1, "CreateInspectable(item1) got %#lx\n", hr);
        IInspectable_Release(pv);

        /* CreateInspectableArray + IReferenceArray<Object> */
        hr = IPropertyValueStatics_CreateInt32(statics, 99, &item2);
        ok(hr == S_OK, "got %#lx\n", hr);
        arr_in[0] = item1;
        arr_in[1] = item2;
        hr = IPropertyValueStatics_CreateInspectableArray(statics, 2, arr_in, &pv);
        ok(hr == S_OK, "CreateInspectableArray got %#lx\n", hr);
        hr = IInspectable_QueryInterface(pv, &iid_irefa_insp, &iref);
        ok(hr == S_OK, "QI IReferenceArray<Object> got %#lx\n", hr);
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, UINT32 *, IInspectable ***))(*(void ***)iref)[6])(iref, &cnt, &arr_out);
        ok(hr == S_OK && cnt == 2 && arr_out[0] == item1 && arr_out[1] == item2, "get_Value got %#lx, cnt %u\n", hr, cnt);
        IInspectable_Release(arr_out[0]);
        IInspectable_Release(arr_out[1]);
        CoTaskMemFree(arr_out);
        IUnknown_Release((IUnknown *)iref);
        hr = IInspectable_GetRuntimeClassName(pv, &cls);
        ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(cls, NULL), L"Windows.Foundation.IReferenceArray`1<Object>"), "got %s\n", wine_dbgstr_w(WindowsGetStringRawBuffer(cls, NULL)));
        WindowsDeleteString(cls);
        IInspectable_Release(pv);
        IInspectable_Release(item1);
        IInspectable_Release(item2);

        /* IReferenceArray<Int32> + IWeakReferenceSource */
        hr = IPropertyValueStatics_CreateInt32Array(statics, 2, ints, &pv);
        ok(hr == S_OK, "got %#lx\n", hr);
        hr = IInspectable_QueryInterface(pv, &iid_irefa_int32, &iref);
        ok(hr == S_OK, "QI IReferenceArray<Int32> got %#lx\n", hr);
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, UINT32 *, INT32 **))(*(void ***)iref)[6])(iref, &cnt, &ints_out);
        ok(hr == S_OK && cnt == 2 && ints_out[0] == 10 && ints_out[1] == 20, "get_Value got %#lx, cnt %u\n", hr, cnt);
        IUnknown_Release((IUnknown *)iref);
        hr = IInspectable_GetRuntimeClassName(pv, &cls);
        ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(cls, NULL), L"Windows.Foundation.IReferenceArray`1<Int32>"), "got %s\n", wine_dbgstr_w(WindowsGetStringRawBuffer(cls, NULL)));
        WindowsDeleteString(cls);

        hr = IInspectable_QueryInterface(pv, &IID_IWeakReferenceSource, (void **)&wrs);
        ok(hr == S_OK, "QI IWeakReferenceSource got %#lx\n", hr);
        hr = IWeakReferenceSource_GetWeakReference(wrs, &wr);
        ok(hr == S_OK && wr != NULL, "GetWeakReference got %#lx\n", hr);
        IWeakReferenceSource_Release(wrs);
        hr = IWeakReference_Resolve(wr, &IID_IPropertyValue, &resolved);
        ok(hr == S_OK && resolved != NULL, "Resolve(alive) got %#lx, %p\n", hr, resolved);
        IInspectable_Release(resolved);
        IInspectable_Release(pv);
        resolved = (IInspectable *)0xdeadbeef;
        hr = IWeakReference_Resolve(wr, &IID_IPropertyValue, &resolved);
        ok(hr == S_OK && resolved == NULL, "Resolve(dead) got %#lx, %p\n", hr, resolved);
        IWeakReference_Release(wr);

        /* IMarshal (FTM) and negative interface probes on PropertyValue */
        {
            static const GUID neg_iids[] =
            {
                {0xe7beaee7, 0x160e, 0x50f7, {0x87, 0x89, 0xd6, 0x34, 0x63, 0xf9, 0x79, 0xfa}}, /* Microsoft.UI.Xaml.IDependencyObject */
                {0x02dd3ad0, 0xb9de, 0x4b55, {0xa0, 0xc3, 0x50, 0x72, 0x35, 0xea, 0xe8, 0xea}}, /* DirectUI::IReferenceTrackerInternal */
                {0x64bd43f8, 0xbfee, 0x4ec4, {0xb7, 0xeb, 0x29, 0x35, 0x15, 0x8d, 0xae, 0x21}}, /* IReferenceTrackerTarget */
                {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}}, /* IReferenceTracker */
                {0x0c881986, 0x86e1, 0x40b3, {0x96, 0x42, 0x9e, 0x50, 0x90, 0xa8, 0xa6, 0xb0}}, /* DirectUI::DependencyObject */
                {0xdc678962, 0x0653, 0x4c92, {0x92, 0xcd, 0x95, 0x94, 0x3d, 0x59, 0x39, 0x00}}, /* DirectUI::ExternalObjectReference */
                {0x1f8cc8ba, 0x0cc5, 0x4799, {0xa5, 0x05, 0xf3, 0x58, 0xc9, 0x88, 0x6d, 0x12}}, /* DirectUI::IExpressionBase */
                {0xfe08f13d, 0xdc6a, 0x5495, {0xad, 0x44, 0xc2, 0xd8, 0xd2, 0x18, 0x63, 0xb0}}, /* Microsoft.UI.Xaml.IFrameworkElement */
                {0x67e93d74, 0x5e93, 0x59a1, {0x91, 0xbf, 0x41, 0x3e, 0xfb, 0xeb, 0x90, 0x4c}}, /* Microsoft.UI.Xaml.Controls.IToolTip */
                {0x3830ad99, 0xd8da, 0x53f3, {0x98, 0x9b, 0xfc, 0x92, 0xad, 0x22, 0x27, 0x78}}, /* IReference<TypeName> */
                {0x6e704a95, 0x4b2f, 0x5ba8, {0xad, 0xa5, 0x12, 0x61, 0xc8, 0x32, 0xba, 0xed}}, /* Microsoft.UI.Xaml.IDataTemplateKey */
                {0x7c925755, 0x3e48, 0x42b4, {0x86, 0x77, 0x76, 0x37, 0x22, 0x67, 0x03, 0x3f}}, /* ICustomPropertyProvider */
                {0x7a1b2c3d, 0x4e5f, 0x4678, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x79}}, /* IID_UWP_ELEM_SELF */
            };
            IMarshal *marshal = NULL;
            CLSID unmarshal_clsid;
            DWORD msize = 0;
            unsigned int i;

            hr = IPropertyValueStatics_CreateInt32(statics, 123, &pv);
            ok(hr == S_OK, "CreateInt32 got %#lx\n", hr);

            hr = IInspectable_QueryInterface(pv, &IID_IMarshal, (void **)&marshal);
            ok(hr == S_OK && marshal != NULL, "QI IMarshal got %#lx\n", hr);
            if (SUCCEEDED(hr) && marshal)
            {
                memset(&unmarshal_clsid, 0, sizeof(unmarshal_clsid));
                hr = IMarshal_GetUnmarshalClass(marshal, &IID_IInspectable, pv, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL, &unmarshal_clsid);
                ok(hr == S_OK, "GetUnmarshalClass got %#lx\n", hr);
                hr = IMarshal_GetMarshalSizeMax(marshal, &IID_IInspectable, pv, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL, &msize);
                ok(hr == S_OK && msize > 0, "GetMarshalSizeMax got %#lx, size %lu\n", hr, msize);
                IMarshal_Release(marshal);
            }

            /* Mismatched IReference<Double>, IUriRuntimeClass, IStringable must fail with E_NOINTERFACE */
            check_interface(pv, &IID_IReference_DOUBLE, FALSE);
            check_interface(pv, &IID_IUriRuntimeClass, FALSE);
            check_interface(pv, &IID_IStringable, FALSE);
            for (i = 0; i < ARRAY_SIZE(neg_iids); i++)
            {
                void *out = (void *)0xdeadbeef;
                hr = IInspectable_QueryInterface(pv, &neg_iids[i], &out);
                ok(hr == E_NOINTERFACE && out == NULL, "neg_iids[%u]: got %#lx, %p\n", i, hr, out);
            }
            IInspectable_Release(pv);
        }
    }

    IPropertyValueStatics_Release(statics);
    IActivationFactory_Release(factory);
    WindowsDeleteString(str);
    RoUninitialize();
}

static void test_RoResolveNamespace(void)
{
    static const WCHAR foundation[] = L"c:\\windows\\system32\\winmetadata\\windows.foundation.winmd";
    static const WCHAR foundation_wow64[] = L"c:\\windows\\sysnative\\winmetadata\\windows.foundation.winmd";
    static const WCHAR networking[] = L"c:\\windows\\system32\\winmetadata\\windows.networking.winmd";
    static const WCHAR networking_wow64[] = L"c:\\windows\\sysnative\\winmetadata\\windows.networking.winmd";
    HSTRING name, *paths;
    DWORD count, i;
    HRESULT hr;
    static const struct
    {
        const WCHAR *namespace;
        DWORD        len_namespace;
        const WCHAR *path;
        const WCHAR *path_wow64;
    }
    tests[] =
    {
        { L"Windows.Networking.Connectivity", ARRAY_SIZE(L"Windows.Networking.Connectivity") - 1,
          networking, networking_wow64 },
        { L"Windows.Foundation", ARRAY_SIZE(L"Windows.Foundation") - 1,
          foundation, foundation_wow64 },
    };

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "got %#lx\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        winetest_push_context("%lu: ", i);
        hr = WindowsCreateString(tests[i].namespace, tests[i].len_namespace, &name);
        ok(hr == S_OK, "got %#lx\n", hr);

        count = 0;
        hr = RoResolveNamespace(name, NULL, 0, NULL, &count, &paths, 0, NULL);
        ok(hr == S_OK, "got %#lx\n", hr);
        if (hr == S_OK)
        {
            const WCHAR *str = WindowsGetStringRawBuffer(paths[0], NULL);

            ok(count == 1, "got %lu\n", count);
            ok((!is_wow64 && !wcsicmp( str, tests[i].path )) ||
               (is_wow64 && !wcsicmp( str, tests[i].path_wow64 )) ||
               broken(is_wow64 && !wcsicmp( str, tests[i].path )) /* win8, win10 1507 */,
               "got %s\n", wine_dbgstr_w(str) );

            WindowsDeleteString(paths[0]);
            CoTaskMemFree(paths);
        }

        WindowsDeleteString(name);
        winetest_pop_context();
    }

    RoUninitialize();
}

static void test_RoGetMetaDataFile(void)
{
    HSTRING name, path;
    UINT32 token;
    HRESULT hr;

    hr = RoGetMetaDataFile(NULL, NULL, &path, NULL, &token);
    ok(hr == E_INVALIDARG, "got %#lx\n", hr);

    hr = WindowsCreateString(L"Windows.UI.Xaml.Controls.Grid", 29, &name);
    ok(hr == S_OK, "got %#lx\n", hr);
    path = NULL;
    token = 0;
    hr = RoGetMetaDataFile(name, NULL, &path, NULL, &token);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(path != NULL, "expected non-NULL path\n");
    ok(token != 0, "expected non-zero token\n");
    if (path)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer(path, NULL);
        ok(wcsstr(raw, L"windows.ui.xaml.winmd") != NULL, "unexpected path %s\n", wine_dbgstr_w(raw));
        WindowsDeleteString(path);
    }
    WindowsDeleteString(name);

    hr = WindowsCreateString(L"NonExistent.Namespace.Type", 26, &name);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = RoGetMetaDataFile(name, NULL, &path, NULL, &token);
    ok(hr == RO_E_METADATA_NAME_NOT_FOUND, "got %#lx\n", hr);
    WindowsDeleteString(name);
}

static void test_RoParseTypeName(void)
{
    static const struct
    {
        const WCHAR *type_name;
        HRESULT hr;
        DWORD parts_count;
        const WCHAR *parts[16];
    }
    tests[] =
    {
        /* Invalid type names */
        {L"", E_INVALIDARG},
        {L" ", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"`", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"<", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L">", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L",", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"<>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"`<>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a b", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a,b", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"1<>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L" a", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L" a ", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a<", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a<>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`<>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`1<>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a<b>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`<b> ", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"`1<b>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L" a`1<b>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`1<b>c", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`1<b,>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`2<b, <c, d>>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`10<b1, b2, b3, b4, b5, b6, b7, b8, b9, b10>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`0xa<b1, b2, b3, b4, b5, b6, b7, b8, b9, b10>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        {L"a`a<b1, b2, b3, b4, b5, b6, b7, b8, b9, b10>", RO_E_METADATA_INVALID_TYPE_FORMAT},
        /* Valid type names */
        {L"1", S_OK, 1, {L"1"}},
        {L"a", S_OK, 1, {L"a"}},
        {L"-", S_OK, 1, {L"-"}},
        {L"a ", S_OK, 1, {L"a"}},
        {L"0`1<b>", S_OK, 2, {L"0`1", L"b"}},
        {L"a`1<b>", S_OK, 2, {L"a`1", L"b"}},
        {L"a`1<b> ", S_OK, 2, {L"a`1", L"b"}},
        {L"a`1<b >", S_OK, 2, {L"a`1", L"b"}},
        {L"a`1< b>", S_OK, 2, {L"a`1", L"b"}},
        {L"a`1< b >", S_OK, 2, {L"a`1", L"b"}},
        {L"a`2<b,c>", S_OK, 3, {L"a`2", L"b", L"c"}},
        {L"a`2<b, c>", S_OK, 3, {L"a`2", L"b", L"c"}},
        {L"a`2<b ,c>", S_OK, 3, {L"a`2", L"b", L"c"}},
        {L"a`2<b , c>", S_OK, 3, {L"a`2", L"b", L"c"}},
        {L"a`3<b, c, d>", S_OK, 4, {L"a`3", L"b", L"c", L"d"}},
        {L"a`1<b`1<c>>", S_OK, 3, {L"a`1", L"b`1", L"c"}},
        {L"a`1<b`2<c, d>>", S_OK, 4, {L"a`1", L"b`2", L"c", L"d"}},
        {L"a`2<b`2<c, d>, e>", S_OK, 5, {L"a`2", L"b`2", L"c", L"d", L"e"}},
        {L"a`2<b, c`2<d, e>>", S_OK, 5, {L"a`2", L"b", L"c`2", L"d", L"e"}},
        {L"a`9<b1, b2, b3, b4, b5, b6, b7, b8, b9>", S_OK, 10, {L"a`9", L"b1", L"b2", L"b3", L"b4", L"b5", L"b6", L"b7", L"b8", L"b9"}},
        {L"Windows.Foundation.IExtensionInformation", S_OK, 1, {L"Windows.Foundation.IExtensionInformation"}},
        {L"Windows.Foundation.IReference`1<Windows.UI.Color>", S_OK, 2, {L"Windows.Foundation.IReference`1", L"Windows.UI.Color"}},
        {L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IMapView`2<Windows.Foundation.Collections.IVector`1<String>, String>>",
         S_OK, 5, {L"Windows.Foundation.Collections.IIterator`1",
                   L"Windows.Foundation.Collections.IMapView`2",
                   L"Windows.Foundation.Collections.IVector`1",
                   L"String",
                   L"String"}},
    };
    HSTRING type_name, *parts;
    const WCHAR *buffer;
    DWORD parts_count;
    unsigned int i, j;
    HRESULT hr;

    /* Parameter checks */
    hr = WindowsCreateString(L"a", 1, &type_name);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = RoParseTypeName(NULL, &parts_count, &parts);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#lx.\n", hr);

    /* Crash on Windows */
    if (0)
    {
    hr = RoParseTypeName(type_name, NULL, &parts);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#lx.\n", hr);

    hr = RoParseTypeName(type_name, &parts_count, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#lx.\n", hr);
    }

    hr = RoParseTypeName(type_name, &parts_count, &parts);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(parts_count == 1, "Got unexpected %ld.\n", parts_count);
    hr = WindowsDeleteString(parts[0]);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    CoTaskMemFree(parts);
    hr = WindowsDeleteString(type_name);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    /* Parsing checks */
    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        winetest_push_context("%s", wine_dbgstr_w(tests[i].type_name));

        if (tests[i].type_name)
        {
            hr = WindowsCreateString(tests[i].type_name, wcslen(tests[i].type_name), &type_name);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
        }
        else
        {
            type_name = NULL;
        }

        parts_count = 0;
        hr = RoParseTypeName(type_name, &parts_count, &parts);
        ok(hr == tests[i].hr, "Got unexpected hr %#lx.\n", hr);
        if (FAILED(hr))
        {
            hr = WindowsDeleteString(type_name);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
            winetest_pop_context();
            continue;
        }
        ok(parts_count == tests[i].parts_count, "Got unexpected %lu.\n", parts_count);

        for (j = 0; j < parts_count; j++)
        {
            winetest_push_context("%s", wine_dbgstr_w(tests[i].parts[j]));

            buffer = WindowsGetStringRawBuffer(parts[j], NULL);
            ok(!lstrcmpW(tests[i].parts[j], buffer), "Got unexpected %s.\n", wine_dbgstr_w(buffer));
            hr = WindowsDeleteString(parts[j]);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

            winetest_pop_context();
        }
        CoTaskMemFree(parts);

        hr = WindowsDeleteString(type_name);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
        winetest_pop_context();
    }
}

static void test_IPropertySet(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_Foundation_Collections_PropertySet;
    IActivationFactory *propset_factory;
    IPropertyValueStatics *propval_statics;
    IInspectable *inspectable, *val, *val1, *val2, *val3;
    IPropertySet *propset;
    IMap_HSTRING_IInspectable *map;
    IMapView_HSTRING_IInspectable *map_view;
    IObservableMap_HSTRING_IInspectable *observable_map;
    IIterable_IKeyValuePair_HSTRING_IInspectable *iterable;
    IIterator_IKeyValuePair_HSTRING_IInspectable *iterator;
    IKeyValuePair_HSTRING_IInspectable *pair;
    BOOLEAN boolean;
    HRESULT hr;
    HSTRING name, key1, key2;
    IPropertyValue *propval;
    UINT32 uint32 = 0;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "got %#lx\n", hr );

    hr = WindowsCreateString( class_name, wcslen( class_name ), &name );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = RoGetActivationFactory( name, &IID_IActivationFactory, (void **)&propset_factory );
    WindowsDeleteString( name );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "RoGetActivationFactory failed, hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        goto done;
    }

    class_name = RuntimeClass_Windows_Foundation_PropertyValue;
    hr = WindowsCreateString( class_name, wcslen( class_name ), &name );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = RoGetActivationFactory( name, &IID_IPropertyValueStatics, (void **)&propval_statics);
    WindowsDeleteString( name );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "RoGetActivationFactory failed, hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        IActivationFactory_Release( propset_factory );
        goto done;
    }

    hr = IActivationFactory_ActivateInstance( propset_factory, &inspectable );
    IActivationFactory_Release( propset_factory );
    ok( hr == S_OK, "got %#lx\n", hr );

    hr = IInspectable_QueryInterface( inspectable, &IID_IPropertySet, (void **)&propset );
    IInspectable_Release( inspectable );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );

    hr = IPropertySet_QueryInterface( propset, &IID_IObservableMap_HSTRING_IInspectable, (void **)&observable_map );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );
    IObservableMap_HSTRING_IInspectable_Release( observable_map );

    hr = IPropertySet_QueryInterface( propset, &IID_IMap_HSTRING_IInspectable, (void **)&map );
    IPropertySet_Release( propset );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );

    boolean = TRUE;
    hr = IMap_HSTRING_IInspectable_HasKey( map, NULL, &boolean );
    ok( hr == S_OK, "HasKey failed, got %#lx\n", hr );
    ok( !boolean, "Got boolean %d.\n", boolean );
    hr = IMap_HSTRING_IInspectable_Lookup( map, NULL, &val );
    ok( hr == E_BOUNDS, "Got hr %#lx\n", hr );

    hr = IPropertyValueStatics_CreateUInt32( propval_statics, 0xdeadbeef, &val1 );
    ok( hr == S_OK, "CreateUInt32 failed, got %#lx\n", hr );
    boolean = TRUE;
    hr = IMap_HSTRING_IInspectable_Insert( map, NULL, val1, &boolean );
    ok( hr == S_OK, "Insert failed, got %#lx\n", hr );
    ok( !boolean, "Got boolean %d.\n", boolean );

    hr = IPropertyValueStatics_CreateUInt32( propval_statics, 0xc0decafe, &val2 );
    ok( hr == S_OK, "CreateUInt32 failed, got %#lx\n", hr );
    boolean = FALSE;
    hr = IMap_HSTRING_IInspectable_Insert( map, NULL, val2, &boolean );
    IInspectable_Release( val2 );
    ok( boolean, "Got boolean %d.\n", boolean );
    ok( hr == S_OK, "Insert failed, got %#lx\n", hr );
    boolean = FALSE;
    hr = IMap_HSTRING_IInspectable_HasKey( map, NULL, &boolean );
    ok( hr == S_OK, "HasKey failed, got %#lx\n", hr );
    ok( boolean, "Got boolean %d.\n", boolean );
    hr = IMap_HSTRING_IInspectable_Lookup( map, NULL, &val );
    ok( hr == S_OK, "Lookup failed, got %#lx\n", hr );
    hr = IInspectable_QueryInterface( val, &IID_IPropertyValue, (void **)&propval );
    IInspectable_Release( val );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );
    hr = IPropertyValue_GetUInt32( propval, &uint32 );
    IPropertyValue_Release( propval );
    ok( hr == S_OK, "GetUInt32, got %#lx\n", hr );
    ok( uint32 == 0xc0decafe, "Got uint32 %u\n", uint32 );

    hr = WindowsCreateString( L"foo", 3, &key1 );
    ok( hr == S_OK, "WindowsCreateString failed, got %#lx\n", hr );
    boolean = TRUE;
    hr = IMap_HSTRING_IInspectable_Lookup( map, key1, &val );
    ok( hr == E_BOUNDS, "Got hr %#lx\n", hr );
    hr = IMap_HSTRING_IInspectable_Insert( map, key1, val1, &boolean );
    IInspectable_Release( val1 );
    ok( hr == S_OK, "Insert failed, got %#lx\n", hr );
    ok( !boolean, "Got boolean %d.\n", boolean );
    boolean = FALSE;
    hr = IMap_HSTRING_IInspectable_HasKey( map, key1, &boolean );
    ok( hr == S_OK, "HasKey failed, got %#lx\n", hr );
    ok( boolean, "Got boolean %d.\n", boolean );
    hr = IMap_HSTRING_IInspectable_Lookup( map, key1, &val );
    ok( hr == S_OK, "Lookup failed, got %#lx\n", hr );
    hr = IInspectable_QueryInterface( val, &IID_IPropertyValue, (void **)&propval );
    IInspectable_Release( val );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );
    hr = IPropertyValue_GetUInt32( propval, &uint32 );
    IPropertyValue_Release( propval );
    ok( hr == S_OK, "GetUInt32, got %#lx\n", hr );
    ok( uint32 == 0xdeadbeef, "Got uint32 %u\n", uint32 );
    WindowsDeleteString( key1 );

    hr = WindowsCreateString( L"bar", 3, &key2 );
    ok( hr == S_OK, "WindowsCreateString failed, got %#lx\n", hr );
    boolean = TRUE;
    hr = IMap_HSTRING_IInspectable_HasKey( map, key2, &boolean );
    ok( hr == S_OK, "HasKey failed, got %#lx\n", hr );
    ok( !boolean, "Got boolean %d.\n", boolean );
    hr = IPropertyValueStatics_CreateUInt64( propval_statics, 0xdeadbeefdeadbeef, &val3 );
    ok( hr == S_OK, "CreateUInt32 failed, got %#lx\n", hr );
    boolean = TRUE;
    hr = IMap_HSTRING_IInspectable_Insert( map, key2, val3, &boolean );
    IInspectable_Release( val3 );
    ok( hr == S_OK, "Insert failed, got %#lx\n", hr );
    ok( !boolean, "Got boolean %d.\n", boolean );
    boolean = FALSE;
    hr = IMap_HSTRING_IInspectable_HasKey( map, key2, &boolean );
    ok( hr == S_OK, "HasKey failed, got %#lx\n", hr );
    ok( boolean, "Got boolean %d.\n", boolean );
    hr = IMap_HSTRING_IInspectable_Lookup( map, key2, &val );
    ok( hr == S_OK, "Lookup failed, got %#lx\n", hr );
    if (SUCCEEDED(hr))
    {
        IPropertyValue *propval;
        UINT64 uint64 = 0;

        hr = IInspectable_QueryInterface( val, &IID_IPropertyValue, (void **)&propval );
        IInspectable_Release( val );
        ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );
        hr = IPropertyValue_GetUInt64( propval, &uint64 );
        IPropertyValue_Release( propval );
        ok( hr == S_OK, "GetUInt32, got %#lx\n", hr );
        ok( uint64 == 0xdeadbeefdeadbeef, "Got uint64 %I64u\n", uint64 );
    }

    check_interface( map, &IID_IAgileObject, TRUE );
    hr = IMap_HSTRING_IInspectable_QueryInterface( map, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                   (void **)&iterable );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );
    hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First( iterable, &iterator );
    ok( hr == S_OK, "got %#lx\n", hr );
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release( iterable );

    check_interface( iterator, &IID_IAgileObject, TRUE );
    hr = IIterator_IKeyValuePair_HSTRING_IInspectable_get_HasCurrent( iterator, &boolean );
    ok( hr == S_OK, "Got hr %#lx\n", hr );
    ok( boolean == TRUE, "Got %u\n", boolean );

    hr = IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current( iterator, &pair );
    ok( hr == S_OK, "Got hr %#lx\n", hr );
    check_interface( pair, &IID_IAgileObject, TRUE );
    hr = IKeyValuePair_HSTRING_IInspectable_get_Key( pair, &key2 );
    ok( hr == S_OK, "Got %#lx\n", hr );
    WindowsDeleteString( key2 );
    hr = IKeyValuePair_HSTRING_IInspectable_get_Value( pair, &val );
    ok( hr == S_OK, "Got %#lx\n", hr );
    IInspectable_Release( val );

    hr = IMap_HSTRING_IInspectable_GetView( map, &map_view );
    ok( hr == S_OK, "GetView failed, got %#lx\n", hr );

    check_interface( map_view, &IID_IAgileObject, TRUE );
    hr = IMapView_HSTRING_IInspectable_QueryInterface( map_view, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                       (void **)&iterable );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );
    hr = IMapView_HSTRING_IInspectable_Lookup( map_view, key2, &val );
    ok( hr == S_OK, "Lookup failed, got %#lx\n", hr );
    IInspectable_Release( val );


    /* after map is modified, associated objects are invalidated */
    hr = IMap_HSTRING_IInspectable_Remove( map, key2 );
    ok( hr == S_OK, "Remove failed, got %#lx\n", hr );

    hr = IKeyValuePair_HSTRING_IInspectable_get_Key( pair, &key2 );
    ok( hr == S_OK, "Got %#lx\n", hr );
    WindowsDeleteString( key2 );
    hr = IKeyValuePair_HSTRING_IInspectable_get_Value( pair, &val );
    ok( hr == S_OK, "Got %#lx\n", hr );
    IInspectable_Release( val );
    IKeyValuePair_HSTRING_IInspectable_Release( pair );

    hr = IIterator_IKeyValuePair_HSTRING_IInspectable_get_HasCurrent( iterator, &boolean );
    ok( hr == S_OK, "Got hr %#lx\n", hr );
    ok( boolean == TRUE, "Got %u\n", boolean );
    hr = IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current( iterator, &pair );
    ok( hr == E_CHANGED_STATE, "Got hr %#lx\n", hr );
    IIterator_IKeyValuePair_HSTRING_IInspectable_Release( iterator );

    hr = IMapView_HSTRING_IInspectable_Lookup( map_view, key2, &val );
    ok( hr == E_CHANGED_STATE, "Got hr %#lx\n", hr );
    WindowsDeleteString( key2 );
    hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First( iterable, &iterator );
    ok( hr == E_CHANGED_STATE, "got %#lx\n", hr );
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release( iterable );
    IMapView_HSTRING_IInspectable_Release( map_view );

    hr = IMap_HSTRING_IInspectable_GetView( map, &map_view );
    ok( hr == S_OK, "GetView failed, got %#lx\n", hr );
    hr = IMapView_HSTRING_IInspectable_QueryInterface( map_view, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
                                                       (void **)&iterable );
    ok( hr == S_OK, "QueryInterface failed, got %#lx\n", hr );
    hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First( iterable, &iterator );
    ok( hr == S_OK, "got %#lx\n", hr );
    IIterator_IKeyValuePair_HSTRING_IInspectable_Release( iterator );
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release( iterable );
    IMapView_HSTRING_IInspectable_Release( map_view );

    IMap_HSTRING_IInspectable_Release( map );
    IPropertyValueStatics_Release( propval_statics );
done:
    RoUninitialize();
}

struct map_changed_handler
{
    IMapChangedEventHandler_HSTRING_IInspectable IMapChangedEventHandler_HSTRING_IInspectable_iface;
    LONG ref;
    ULONG called;
    enum CollectionChange status;
    WCHAR key[16];
    BOOL has_sender;
};

static inline struct map_changed_handler *mch_from_iface( IMapChangedEventHandler_HSTRING_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct map_changed_handler, IMapChangedEventHandler_HSTRING_IInspectable_iface );
}

static HRESULT STDMETHODCALLTYPE mch_QueryInterface( IMapChangedEventHandler_HSTRING_IInspectable *iface,
                                                     REFIID iid, void **out )
{
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IMapChangedEventHandler_HSTRING_IInspectable ))
    {
        *out = iface;
        IMapChangedEventHandler_HSTRING_IInspectable_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE mch_AddRef( IMapChangedEventHandler_HSTRING_IInspectable *iface )
{
    struct map_changed_handler *impl = mch_from_iface( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG STDMETHODCALLTYPE mch_Release( IMapChangedEventHandler_HSTRING_IInspectable *iface )
{
    struct map_changed_handler *impl = mch_from_iface( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT STDMETHODCALLTYPE mch_Invoke( IMapChangedEventHandler_HSTRING_IInspectable *iface,
                                             IObservableMap_HSTRING_IInspectable *sender,
                                             IMapChangedEventArgs_HSTRING *args )
{
    struct map_changed_handler *impl = mch_from_iface( iface );
    HSTRING key = NULL;
    const WCHAR *raw;

    impl->called++;
    impl->status = (enum CollectionChange)-1;
    impl->key[0] = 0;
    impl->has_sender = sender != NULL;
    if (args)
    {
        IMapChangedEventArgs_HSTRING_get_CollectionChanged( args, &impl->status );
        if (SUCCEEDED( IMapChangedEventArgs_HSTRING_get_Key( args, &key ) ) && key)
        {
            raw = WindowsGetStringRawBuffer( key, NULL );
            if (raw) lstrcpynW( impl->key, raw, ARRAY_SIZE( impl->key ) );
            WindowsDeleteString( key );
        }
    }
    return S_OK;
}

/* The delegate projection is IUnknown + Invoke (no IInspectable). */
static const IMapChangedEventHandler_HSTRING_IInspectableVtbl map_changed_handler_vtbl =
{
    mch_QueryInterface,
    mch_AddRef,
    mch_Release,
    mch_Invoke,
};

static void test_PropertySet_MapChanged(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_Foundation_Collections_PropertySet;
    struct map_changed_handler handler;
    IActivationFactory *factory;
    IPropertySet *propset;
    IMap_HSTRING_IInspectable *map;
    IObservableMap_HSTRING_IInspectable *observable_map;
    IInspectable *inspectable, *value;
    IPropertyValueStatics *propval_statics;
    EventRegistrationToken token;
    BOOLEAN replaced;
    HRESULT hr;
    HSTRING name, key;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "got %#lx\n", hr );

    hr = WindowsCreateString( class_name, wcslen( class_name ), &name );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = RoGetActivationFactory( name, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( name );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        goto done;
    }
    hr = WindowsCreateString( RuntimeClass_Windows_Foundation_PropertyValue,
                              lstrlenW( RuntimeClass_Windows_Foundation_PropertyValue ), &name );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = RoGetActivationFactory( name, &IID_IPropertyValueStatics, (void **)&propval_statics );
    WindowsDeleteString( name );
    if (FAILED( hr ))
    {
        IActivationFactory_Release( factory );
        win_skip( "IPropertyValueStatics not registered, skipping tests.\n" );
        goto done;
    }

    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    IActivationFactory_Release( factory );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IPropertySet, (void **)&propset );
    IInspectable_Release( inspectable );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = IPropertySet_QueryInterface( propset, &IID_IObservableMap_HSTRING_IInspectable, (void **)&observable_map );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = IPropertySet_QueryInterface( propset, &IID_IMap_HSTRING_IInspectable, (void **)&map );
    ok( hr == S_OK, "got %#lx\n", hr );

    handler.IMapChangedEventHandler_HSTRING_IInspectable_iface.lpVtbl = &map_changed_handler_vtbl;
    handler.ref = 1;
    handler.called = 0;
    handler.has_sender = FALSE;
    handler.key[0] = 0;

    hr = WindowsCreateString( L"key", 3, &key );
    ok( hr == S_OK, "got %#lx\n", hr );
    hr = IPropertyValueStatics_CreateUInt32( propval_statics, 1, &value );
    ok( hr == S_OK, "got %#lx\n", hr );

    token.value = 0;
    hr = IObservableMap_HSTRING_IInspectable_add_MapChanged( observable_map,
            &handler.IMapChangedEventHandler_HSTRING_IInspectable_iface, &token );
    ok( hr == S_OK, "got %#lx\n", hr );

    /* Insert of a fresh key fires ItemInserted with the key */
    replaced = TRUE;
    hr = IMap_HSTRING_IInspectable_Insert( map, key, value, &replaced );
    ok( hr == S_OK, "got %#lx\n", hr );
    ok( !replaced, "got replaced %d\n", replaced );
    ok( handler.called == 1, "got %u calls\n", handler.called );
    ok( handler.status == CollectionChange_ItemInserted, "got status %d\n", handler.status );
    ok( !lstrcmpW( handler.key, L"key" ), "got key %s\n", debugstr_w( handler.key ) );
    ok( handler.has_sender, "sender not passed\n" );

    /* Insert over an existing key fires Replaced (ItemChanged) */
    handler.status = (enum CollectionChange)-1;
    replaced = FALSE;
    hr = IMap_HSTRING_IInspectable_Insert( map, key, value, &replaced );
    ok( hr == S_OK, "got %#lx\n", hr );
    ok( replaced, "got replaced %d\n", replaced );
    ok( handler.called == 2, "got %u calls\n", handler.called );
    ok( handler.status == CollectionChange_ItemChanged, "got status %d\n", handler.status );
    ok( !lstrcmpW( handler.key, L"key" ), "got key %s\n", debugstr_w( handler.key ) );

    /* Insert through the IPropertySet / IMap face queried from IObservableMap fires the same way */
    {
        IMap_HSTRING_IInspectable *obs_as_map = NULL;
        hr = IObservableMap_HSTRING_IInspectable_QueryInterface( observable_map, &IID_IMap_HSTRING_IInspectable, (void **)&obs_as_map );
        ok( hr == S_OK, "got %#lx\n", hr );
        handler.status = (enum CollectionChange)-1;
        replaced = TRUE;
        hr = IMap_HSTRING_IInspectable_Insert( obs_as_map, key, value, &replaced );
        ok( hr == S_OK, "got %#lx\n", hr );
        ok( handler.called == 3, "got %u calls\n", handler.called );
        ok( handler.status == CollectionChange_ItemChanged, "got status %d\n", handler.status );
        IMap_HSTRING_IInspectable_Release( obs_as_map );
    }

    /* Remove fires ItemRemoved */
    handler.status = (enum CollectionChange)-1;
    hr = IMap_HSTRING_IInspectable_Remove( map, key );
    ok( hr == S_OK, "got %#lx\n", hr );
    ok( handler.called == 4, "got %u calls\n", handler.called );
    ok( handler.status == CollectionChange_ItemRemoved, "got status %d\n", handler.status );

    /* Clear fires a single Reset event with a NULL key */
    handler.status = (enum CollectionChange)-1;
    handler.key[0] = 'x';
    replaced = TRUE;
    hr = IMap_HSTRING_IInspectable_Insert( map, key, value, &replaced );
    ok( hr == S_OK, "got %#lx\n", hr );
    handler.called = 0;
    handler.status = (enum CollectionChange)-1;
    hr = IMap_HSTRING_IInspectable_Clear( map );
    ok( hr == S_OK, "got %#lx\n", hr );
    ok( handler.called == 1, "got %u calls\n", handler.called );
    ok( handler.status == CollectionChange_Reset, "got status %d\n", handler.status );
    ok( !handler.key[0], "got key %s\n", debugstr_w( handler.key ) );

    /* remove_MapChanged unregisters: further mutations are silent */
    hr = IObservableMap_HSTRING_IInspectable_remove_MapChanged( observable_map, token );
    ok( hr == S_OK, "got %#lx\n", hr );
    handler.called = 0;
    hr = IMap_HSTRING_IInspectable_Clear( map );
    ok( hr == S_OK, "got %#lx\n", hr );
    ok( handler.called == 0, "got %u calls\n", handler.called );

    WindowsDeleteString( key );
    IInspectable_Release( value );
    IMapChangedEventHandler_HSTRING_IInspectable_Release( &handler.IMapChangedEventHandler_HSTRING_IInspectable_iface );
    IObservableMap_HSTRING_IInspectable_Release( observable_map );
    IMap_HSTRING_IInspectable_Release( map );
    IPropertySet_Release( propset );
    IPropertyValueStatics_Release( propval_statics );
done:
    RoUninitialize();
}

static void test_Uri(void)
{
    static const GUID neg_iids[] =
    {
        {0x0c881986, 0x86e1, 0x40b3, {0x96, 0x42, 0x9e, 0x50, 0x90, 0xa8, 0xa6, 0xb0}}, /* DirectUI::DependencyObject */
        {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}}, /* IReferenceTracker */
        {0x7a1b2c3d, 0x4e5f, 0x4678, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x79}}, /* IID_UWP_ELEM_SELF */
        {0x1b690975, 0xa710, 0x5783, {0xa6, 0xe1, 0x15, 0x83, 0x6f, 0x61, 0x86, 0xc2}}, /* Microsoft.UI.Xaml.IResourceDictionary */
        {0x676d0be9, 0xb65c, 0x41c6, {0xba, 0x40, 0x58, 0xcf, 0x87, 0xf2, 0x01, 0xc1}}, /* Windows.UI.Xaml.IUIElement */
    };
    static const WCHAR raw_uri[] = L"https://example.com:8443/path?k=v#frag";
    IActivationFactory *factory = NULL;
    IUriRuntimeClassFactory *uri_factory = NULL;
    IUriRuntimeClass *uri = NULL;
    IStringable *stringable = NULL;
    IWeakReferenceSource *wrs = NULL;
    IWeakReference *wr = NULL;
    IInspectable *resolved = NULL;
    IMarshal *marshal = NULL;
    CLSID unmarshal_clsid;
    DWORD msize = 0;
    HSTRING name, raw, str = NULL;
    unsigned int i;
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx.\n", hr );

    hr = WindowsCreateString( RuntimeClass_Windows_Foundation_Uri,
                              lstrlenW( RuntimeClass_Windows_Foundation_Uri ), &name );
    ok( hr == S_OK, "WindowsCreateString got %#lx.\n", hr );

    hr = RoGetActivationFactory( name, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( name );
    ok( hr == S_OK, "RoGetActivationFactory(Uri) got %#lx.\n", hr );
    if (FAILED( hr ))
    {
        RoUninitialize();
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IUriRuntimeClassFactory, TRUE );
    check_interface( factory, &IID_IUriEscapeStatics, TRUE );
    check_interface( factory, &IID_IMarshal, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IUriRuntimeClassFactory, (void **)&uri_factory );
    ok( hr == S_OK, "QI IUriRuntimeClassFactory got %#lx.\n", hr );

    hr = WindowsCreateString( raw_uri, lstrlenW( raw_uri ), &raw );
    ok( hr == S_OK, "got %#lx.\n", hr );
    hr = IUriRuntimeClassFactory_CreateUri( uri_factory, raw, &uri );
    WindowsDeleteString( raw );
    ok( hr == S_OK && uri != NULL, "CreateUri got %#lx.\n", hr );

    check_interface( uri, &IID_IUnknown, TRUE );
    check_interface( uri, &IID_IInspectable, TRUE );
    check_interface( uri, &IID_IAgileObject, TRUE );
    check_interface( uri, &IID_IUriRuntimeClass, TRUE );
    check_interface( uri, &IID_IUriRuntimeClassWithAbsoluteCanonicalUri, TRUE );
    check_interface( uri, &IID_IStringable, TRUE );
    check_interface( uri, &IID_IWeakReferenceSource, TRUE );
    check_interface( uri, &IID_IMarshal, TRUE );

    /* IStringable */
    hr = IUriRuntimeClass_QueryInterface( uri, &IID_IStringable, (void **)&stringable );
    ok( hr == S_OK, "QI IStringable got %#lx.\n", hr );
    hr = IStringable_ToString( stringable, &str );
    ok( hr == S_OK && str != NULL, "ToString got %#lx.\n", hr );
    WindowsDeleteString( str );
    IStringable_Release( stringable );

    /* IMarshal (FTM) */
    hr = IUriRuntimeClass_QueryInterface( uri, &IID_IMarshal, (void **)&marshal );
    ok( hr == S_OK && marshal != NULL, "QI IMarshal got %#lx.\n", hr );
    if (SUCCEEDED( hr ) && marshal)
    {
        memset( &unmarshal_clsid, 0, sizeof(unmarshal_clsid) );
        hr = IMarshal_GetUnmarshalClass( marshal, &IID_IUriRuntimeClass, uri, MSHCTX_INPROC, NULL,
                                         MSHLFLAGS_NORMAL, &unmarshal_clsid );
        ok( hr == S_OK, "GetUnmarshalClass got %#lx.\n", hr );
        hr = IMarshal_GetMarshalSizeMax( marshal, &IID_IUriRuntimeClass, uri, MSHCTX_INPROC, NULL,
                                         MSHLFLAGS_NORMAL, &msize );
        ok( hr == S_OK && msize > 0, "GetMarshalSizeMax got %#lx, size %lu.\n", hr, msize );
        IMarshal_Release( marshal );
    }

    /* Negative interface probes on Uri instance */
    check_interface( uri, &IID_IPropertyValue, FALSE );
    check_interface( uri, &IID_IUriRuntimeClassFactory, FALSE );
    for (i = 0; i < ARRAY_SIZE(neg_iids); i++)
    {
        void *out = (void *)0xdeadbeef;
        hr = IUriRuntimeClass_QueryInterface( uri, &neg_iids[i], &out );
        ok( hr == E_NOINTERFACE && out == NULL, "uri neg_iids[%u]: got %#lx, %p.\n", i, hr, out );
    }

    /* IWeakReferenceSource lifecycle */
    hr = IUriRuntimeClass_QueryInterface( uri, &IID_IWeakReferenceSource, (void **)&wrs );
    ok( hr == S_OK, "QI IWeakReferenceSource got %#lx.\n", hr );
    hr = IWeakReferenceSource_GetWeakReference( wrs, &wr );
    ok( hr == S_OK && wr != NULL, "GetWeakReference got %#lx.\n", hr );
    IWeakReferenceSource_Release( wrs );

    hr = IWeakReference_Resolve( wr, &IID_IUriRuntimeClass, &resolved );
    ok( hr == S_OK && resolved != NULL, "Resolve(alive) got %#lx, %p.\n", hr, resolved );
    IInspectable_Release( resolved );

    IUriRuntimeClass_Release( uri );

    resolved = (IInspectable *)0xdeadbeef;
    hr = IWeakReference_Resolve( wr, &IID_IUriRuntimeClass, &resolved );
    ok( hr == S_OK && resolved == NULL, "Resolve(dead) got %#lx, %p.\n", hr, resolved );
    IWeakReference_Release( wr );

    IUriRuntimeClassFactory_Release( uri_factory );
    IActivationFactory_Release( factory );
    RoUninitialize();
}

START_TEST(wintypes)
{
    IsWow64Process(GetCurrentProcess(), &is_wow64);

    test_interfaces();
    test_IApiInformationStatics();
    test_IBufferStatics();
    test_IPropertyValueStatics();
    test_Uri();
    test_RoParseTypeName();
    test_RoResolveNamespace();
    test_RoGetMetaDataFile();
    test_IPropertySet();
    test_PropertySet_MapChanged();
}
