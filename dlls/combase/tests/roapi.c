/*
 * Copyright (c) 2018 Alistair Leslie-Hughes
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
#include "winternl.h"

#include "initguid.h"
#include "roapi.h"
#include "roparameterizediid.h"
#include "roerrorapi.h"

#include "wine/test.h"

#define EXPECT_REF(obj,ref) _expect_ref((IUnknown*)obj, ref, __LINE__)
static void _expect_ref(IUnknown* obj, ULONG ref, int line)
{
    ULONG rc;
    IUnknown_AddRef(obj);
    rc = IUnknown_Release(obj);
    ok_(__FILE__,line)(rc == ref, "expected refcount %ld, got %ld\n", ref, rc);
}

static void flush_events(void)
{
    int diff = 200;
    DWORD time;
    MSG msg;

    time = GetTickCount() + diff;
    while (diff > 0)
    {
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT) == WAIT_TIMEOUT)
            break;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
            DispatchMessageA(&msg);
        diff = time - GetTickCount();
    }
}

static void load_resource(const WCHAR *filename)
{
    DWORD written;
    HANDLE file;
    HRSRC res;
    void *ptr;

    file = CreateFileW(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "failed to create %s, error %lu\n", debugstr_w(filename), GetLastError());

    res = FindResourceW(NULL, filename, L"TESTDLL");
    ok(res != 0, "couldn't find resource\n");
    ptr = LockResource(LoadResource(GetModuleHandleW(NULL), res));
    WriteFile(file, ptr, SizeofResource(GetModuleHandleW(NULL), res), &written, NULL);
    ok(written == SizeofResource(GetModuleHandleW(NULL), res), "couldn't write resource\n");
    CloseHandle(file);
}

static void test_ActivationFactories(void)
{
    HRESULT hr;
    HSTRING str, str2;
    IActivationFactory *factory = NULL;
    IInspectable *inspect = NULL;
    ULONG ref;

    hr = WindowsCreateString(L"Windows.Data.Xml.Dom.XmlDocument",
                              ARRAY_SIZE(L"Windows.Data.Xml.Dom.XmlDocument") - 1, &str);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = WindowsCreateString(L"Does.Not.Exist", ARRAY_SIZE(L"Does.Not.Exist") - 1, &str2);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);

    hr = RoGetActivationFactory(str2, &IID_IActivationFactory, (void **)&factory);
    ok(hr == REGDB_E_CLASSNOTREG, "Unexpected hr %#lx.\n", hr);

    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    todo_wine ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    if(factory)
        IActivationFactory_Release(factory);

    hr = RoActivateInstance(str2, &inspect);
    ok(hr == REGDB_E_CLASSNOTREG, "Unexpected hr %#lx.\n", hr);

    hr = RoActivateInstance(str, &inspect);
    todo_wine ok(hr == S_OK, "UNexpected hr %#lx.\n", hr);
    if(inspect)
        IInspectable_Release(inspect);

    WindowsDeleteString(str2);
    WindowsDeleteString(str);

    hr = WindowsCreateString(L"Wine.Test.Missing", ARRAY_SIZE(L"Wine.Test.Missing") - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == REGDB_E_CLASSNOTREG, "RoGetActivationFactory returned %#lx.\n", hr);
    ok(factory == NULL, "got factory %p.\n", factory);
    WindowsDeleteString(str);
    hr = WindowsCreateString(L"Wine.Test.Class", ARRAY_SIZE(L"Wine.Test.Class") - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    todo_wine
    ok(hr == E_NOTIMPL || broken(hr == REGDB_E_CLASSNOTREG) /* <= w1064v1809 */,
            "RoGetActivationFactory returned %#lx.\n", hr);
    todo_wine
    ok(factory == NULL, "got factory %p.\n", factory);
    if (factory) IActivationFactory_Release(factory);
    WindowsDeleteString(str);
    hr = WindowsCreateString(L"Wine.Test.Trusted", ARRAY_SIZE(L"Wine.Test.Trusted") - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG) /* <= w1064v1809 */,
            "RoGetActivationFactory returned %#lx.\n", hr);
    if (hr == REGDB_E_CLASSNOTREG)
        ok(!factory, "got factory %p.\n", factory);
    else
        ok(!!factory, "got factory %p.\n", factory);
    if (!factory) ref = 0;
    else ref = IActivationFactory_Release(factory);
    ok(ref == 0, "Release returned %lu\n", ref);
    WindowsDeleteString(str);

    RoUninitialize();
}

static APTTYPE check_thread_apttype;
static APTTYPEQUALIFIER check_thread_aptqualifier;
static HRESULT check_thread_hr;

static DWORD WINAPI check_apartment_thread(void *dummy)
{
    check_thread_apttype = 0xdeadbeef;
    check_thread_aptqualifier = 0xdeadbeef;
    check_thread_hr = CoGetApartmentType(&check_thread_apttype, &check_thread_aptqualifier);
    return 0;
}

#define check_thread_apartment(a) check_thread_apartment_(__LINE__, FALSE, a)
#define check_thread_apartment_broken(a) check_thread_apartment_(__LINE__, TRUE, a)
static void check_thread_apartment_(unsigned int line, BOOL broken_fail, HRESULT expected_hr_thread)
{
    HANDLE thread;

    check_thread_hr = 0xdeadbeef;
    thread = CreateThread(NULL, 0, check_apartment_thread, NULL, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    ok_(__FILE__, line)(check_thread_hr == expected_hr_thread
            || broken(broken_fail && expected_hr_thread == S_OK && check_thread_hr == CO_E_NOTINITIALIZED),
            "got %#lx, expected %#lx.\n", check_thread_hr, expected_hr_thread);
    if (SUCCEEDED(check_thread_hr))
    {
        ok_(__FILE__, line)(check_thread_apttype == APTTYPE_MTA, "got %d.\n", check_thread_apttype);
        ok_(__FILE__, line)(check_thread_aptqualifier == APTTYPEQUALIFIER_IMPLICIT_MTA, "got %d.\n", check_thread_aptqualifier);
    }
}

static HANDLE mta_init_thread_init_done_event, mta_init_thread_done_event;

static DWORD WINAPI mta_init_thread(void *dummy)
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "got %#lx.\n", hr);
    SetEvent(mta_init_thread_init_done_event);

    WaitForSingleObject(mta_init_thread_done_event, INFINITE);
    CoUninitialize();
    return 0;
}

static DWORD WINAPI mta_init_implicit_thread(void *dummy)
{
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(hr == S_OK, "got %#lx.\n", hr);

    hr = WindowsCreateString(L"Does.Not.Exist", ARRAY_SIZE(L"Does.Not.Exist") - 1, &str);
    ok(hr == S_OK, "got %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == REGDB_E_CLASSNOTREG, "got %#lx.\n", hr);
    WindowsDeleteString(str);

    SetEvent(mta_init_thread_init_done_event);
    WaitForSingleObject(mta_init_thread_done_event, INFINITE);

    /* No CoUninitialize(), testing cleanup on thread exit. */
    return 0;
}

enum oletlsflags
{
    OLETLS_UUIDINITIALIZED = 0x2,
    OLETLS_DISABLE_OLE1DDE = 0x40,
    OLETLS_APARTMENTTHREADED = 0x80,
    OLETLS_MULTITHREADED = 0x100,
};

struct oletlsdata
{
    void *threadbase;
    void *smallocator;
    DWORD id;
    DWORD flags;
};

static DWORD get_oletlsflags(void)
{
    struct oletlsdata *data = NtCurrentTeb()->ReservedForOle;
    return data ? data->flags : 0;
}

static void test_implicit_mta(void)
{
    static const struct
    {
        BOOL ro_init;
        BOOL mta;
    }
    tests[] =
    {
        { FALSE, FALSE },
        { FALSE, TRUE },
        { TRUE, FALSE },
        { TRUE, TRUE },
    };
    APTTYPEQUALIFIER aptqualifier;
    IActivationFactory *factory;
    APTTYPE apttype;
    unsigned int i;
    HANDLE thread;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString(L"Does.Not.Exist", ARRAY_SIZE(L"Does.Not.Exist") - 1, &str);
    ok(hr == S_OK, "got %#lx.\n", hr);
    /* RoGetActivationFactory doesn't implicitly initialize COM. */
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == CO_E_NOTINITIALIZED, "got %#lx.\n", hr);

    check_thread_apartment(CO_E_NOTINITIALIZED);

    /* RoGetActivationFactory initializes implicit MTA. */
    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        winetest_push_context("test %u", i);
        if (tests[i].ro_init)
        {
            DWORD flags = tests[i].mta ? OLETLS_MULTITHREADED : OLETLS_APARTMENTTHREADED;

            hr = RoInitialize(tests[i].mta ? RO_INIT_MULTITHREADED : RO_INIT_SINGLETHREADED);

            flags |= OLETLS_DISABLE_OLE1DDE;
            ok((get_oletlsflags() & flags) == flags, "get_oletlsflags() = %lx\n", get_oletlsflags());
        }
        else
            hr = CoInitializeEx(NULL, tests[i].mta ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
        ok(hr == S_OK, "got %#lx.\n", hr);
        check_thread_apartment(tests[i].mta ? S_OK : CO_E_NOTINITIALIZED);
        hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
        ok(hr == REGDB_E_CLASSNOTREG, "got %#lx.\n", hr);
        check_thread_apartment_broken(S_OK); /* Broken on Win8. */
        hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
        ok(hr == REGDB_E_CLASSNOTREG, "got %#lx.\n", hr);
        check_thread_apartment_broken(S_OK); /* Broken on Win8. */
        if (tests[i].ro_init)
            RoUninitialize();
        else
            CoUninitialize();
        check_thread_apartment(CO_E_NOTINITIALIZED);
        winetest_pop_context();
    }

    mta_init_thread_init_done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    mta_init_thread_done_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    /* RoGetActivationFactory references implicit MTA in a current thread
     * even if implicit MTA was already initialized: check with STA init
     * after RoGetActivationFactory(). */
    thread = CreateThread(NULL, 0, mta_init_thread, NULL, 0, NULL);
    ok(!!thread, "failed.\n");
    WaitForSingleObject(mta_init_thread_init_done_event, INFINITE);
    check_thread_apartment(S_OK);

    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == REGDB_E_CLASSNOTREG, "got %#lx.\n", hr);
    check_thread_apartment(S_OK);

    hr = CoGetApartmentType(&apttype, &aptqualifier);
    ok(hr == S_OK, "got %#lx.\n", hr);
    ok(apttype == APTTYPE_MTA, "got %d.\n", apttype);
    ok(aptqualifier == APTTYPEQUALIFIER_IMPLICIT_MTA, "got %d.\n", aptqualifier);

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(hr == S_OK, "got %#lx.\n", hr);

    hr = CoGetApartmentType(&apttype, &aptqualifier);
    ok(hr == S_OK, "got %#lx.\n", hr);
    ok(apttype == APTTYPE_MAINSTA, "got %d.\n", apttype);
    ok(aptqualifier == APTTYPEQUALIFIER_NONE, "got %d.\n", aptqualifier);

    SetEvent(mta_init_thread_done_event);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    check_thread_apartment_broken(S_OK); /* Broken on Win8. */
    CoUninitialize();
    check_thread_apartment(CO_E_NOTINITIALIZED);

    /* RoGetActivationFactory references implicit MTA in a current thread
     * even if implicit MTA was already initialized: check with STA init
     * before RoGetActivationFactory(). */
    thread = CreateThread(NULL, 0, mta_init_thread, NULL, 0, NULL);
    ok(!!thread, "failed.\n");
    WaitForSingleObject(mta_init_thread_init_done_event, INFINITE);
    check_thread_apartment(S_OK);

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(hr == S_OK, "got %#lx.\n", hr);

    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == REGDB_E_CLASSNOTREG, "got %#lx.\n", hr);
    check_thread_apartment(S_OK);

    SetEvent(mta_init_thread_done_event);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    check_thread_apartment_broken(S_OK); /* Broken on Win8. */
    CoUninitialize();
    check_thread_apartment(CO_E_NOTINITIALIZED);

    /* Test implicit MTA apartment thread exit. */
    thread = CreateThread(NULL, 0, mta_init_implicit_thread, NULL, 0, NULL);
    ok(!!thread, "failed.\n");
    WaitForSingleObject(mta_init_thread_init_done_event, INFINITE);
    check_thread_apartment_broken(S_OK); /* Broken on Win8. */
    SetEvent(mta_init_thread_done_event);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    check_thread_apartment(CO_E_NOTINITIALIZED);

    CloseHandle(mta_init_thread_init_done_event);
    CloseHandle(mta_init_thread_done_event);
    WindowsDeleteString(str);
}

struct unk_impl
{
    IUnknown IUnknown_iface;
    LONG ref;
};

static inline struct unk_impl *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct unk_impl, IUnknown_iface);
}

static HRESULT WINAPI unk_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown))
    {
        *ppv = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static HRESULT WINAPI unk_no_marshal_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown)
        || IsEqualGUID(riid, &IID_INoMarshal))
    {
        *ppv = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static HRESULT WINAPI unk_agile_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown)
        || IsEqualGUID(riid, &IID_IAgileObject))
    {
        *ppv = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI unk_AddRef(IUnknown *iface)
{
    struct unk_impl *impl = impl_from_IUnknown(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI unk_Release(IUnknown *iface)
{
    struct unk_impl *impl = impl_from_IUnknown(iface);
    return InterlockedDecrement(&impl->ref);
}

static const IUnknownVtbl unk_vtbl =
{
    unk_QueryInterface,
    unk_AddRef,
    unk_Release
};

static const IUnknownVtbl unk_no_marshal_vtbl =
{
    unk_no_marshal_QueryInterface,
    unk_AddRef,
    unk_Release
};

static const IUnknownVtbl unk_agile_vtbl =
{
    unk_agile_QueryInterface,
    unk_AddRef,
    unk_Release
};

struct test_RoGetAgileReference_thread_param
{
    enum AgileReferenceOptions option;
    RO_INIT_TYPE from_type;
    RO_INIT_TYPE to_type;
    IAgileReference *agile_reference;
    IUnknown *unk_obj;
    BOOLEAN obj_is_agile;
};

static DWORD CALLBACK test_RoGetAgileReference_thread_proc(void *arg)
{
    struct test_RoGetAgileReference_thread_param *param = (struct test_RoGetAgileReference_thread_param *)arg;
    IUnknown *unknown;
    HRESULT hr;

    winetest_push_context("%d %d %d", param->option, param->from_type, param->to_type);

    RoInitialize(param->to_type);

    unknown = NULL;
    hr = IAgileReference_Resolve(param->agile_reference, &IID_IUnknown, (void **)&unknown);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(!!unknown, "Expected pointer not NULL.\n");
    if (param->obj_is_agile)
    {
        ok(unknown == param->unk_obj, "Expected the same object.\n");
        EXPECT_REF(param->unk_obj, 4);
    }
    else if (param->from_type == RO_INIT_MULTITHREADED && param->to_type == RO_INIT_MULTITHREADED)
    {
        ok(unknown == param->unk_obj, "Expected the same object.\n");
        todo_wine_if(param->option == AGILEREFERENCE_DEFAULT)
        EXPECT_REF(param->unk_obj, param->option == AGILEREFERENCE_DEFAULT ? 5 : 6);
    }
    else
    {
        ok(unknown != param->unk_obj, "Expected a different object pointer.\n");
        todo_wine_if(param->option == AGILEREFERENCE_DEFAULT)
        EXPECT_REF(param->unk_obj, param->option == AGILEREFERENCE_DEFAULT ? 4 : 5);
        EXPECT_REF(unknown, 1);
    }
    IUnknown_Release(unknown);

    RoUninitialize();
    winetest_pop_context();
    return 0;
}

static void test_RoGetAgileReference(void)
{
    struct test_RoGetAgileReference_thread_param param;
    struct unk_impl unk_no_marshal_obj = {{&unk_no_marshal_vtbl}, 1};
    struct unk_impl unk_obj = {{&unk_vtbl}, 1};
    struct unk_impl unk_agile_obj = {{&unk_agile_vtbl}, 1};
    enum AgileReferenceOptions option;
    IAgileReference *agile_reference;
    RO_INIT_TYPE from_type, to_type;
    IUnknown *unknown, *unknown2;
    IAgileObject *agile_object;
    HANDLE thread;
    HRESULT hr;
    DWORD ret;

    for (option = AGILEREFERENCE_DEFAULT; option <= AGILEREFERENCE_DELAYEDMARSHAL; option++)
    {
        for (from_type = RO_INIT_SINGLETHREADED; from_type <= RO_INIT_MULTITHREADED; from_type++)
        {
            winetest_push_context("%d %d", option, from_type);

            hr = RoGetAgileReference(option, &IID_IUnknown, &unk_obj.IUnknown_iface, &agile_reference);
            ok(hr == CO_E_NOTINITIALIZED, "Got unexpected hr %#lx.\n", hr);

            RoInitialize(from_type);

            agile_reference = NULL;
            EXPECT_REF(&unk_obj, 1);

            /* Invalid option */
            hr = RoGetAgileReference(AGILEREFERENCE_DELAYEDMARSHAL + 1, &IID_IUnknown, &unk_obj.IUnknown_iface, &agile_reference);
            ok(hr == E_INVALIDARG, "Got unexpected hr %#lx.\n", hr);

            /* Non-existent interface for the object */
            hr = RoGetAgileReference(option, &IID_IActivationFactory, &unk_obj.IUnknown_iface, &agile_reference);
            ok(hr == E_NOINTERFACE, "Got unexpected hr %#lx.\n", hr);

            /* Objects that implements INoMarshal */
            hr = RoGetAgileReference(option, &IID_IUnknown, &unk_no_marshal_obj.IUnknown_iface, &agile_reference);
            ok(hr == CO_E_NOT_SUPPORTED, "Got unexpected hr %#lx.\n", hr);

            /* Create agile reference object */
            hr = RoGetAgileReference(option, &IID_IUnknown, &unk_obj.IUnknown_iface, &agile_reference);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
            ok(!!agile_reference, "Got unexpected agile_reference.\n");
            todo_wine_if(option == AGILEREFERENCE_DEFAULT)
            EXPECT_REF(&unk_obj, 2);

            /* Check the created agile reference object has IAgileObject */
            hr = IAgileReference_QueryInterface(agile_reference, &IID_IAgileObject, (void **)&agile_object);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
            IAgileObject_Release(agile_object);

            /* Resolve once */
            unknown = NULL;
            hr = IAgileReference_Resolve(agile_reference, &IID_IUnknown, (void **)&unknown);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
            ok(!!unknown, "Expected pointer not NULL.\n");
            ok(unknown == &unk_obj.IUnknown_iface, "Expected the same object.\n");
            todo_wine
            EXPECT_REF(&unk_obj, 3);

            /* Resolve twice */
            unknown2 = NULL;
            hr = IAgileReference_Resolve(agile_reference, &IID_IUnknown, (void **)&unknown2);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
            ok(!!unknown2, "Expected pointer not NULL.\n");
            ok(unknown2 == &unk_obj.IUnknown_iface, "Expected the same object.\n");
            todo_wine
            EXPECT_REF(&unk_obj, 4);

            /* Resolve in another apartment */
            for (to_type = RO_INIT_SINGLETHREADED; to_type <= RO_INIT_MULTITHREADED; to_type++)
            {
                param.option = option;
                param.from_type = from_type;
                param.to_type = to_type;
                param.agile_reference = agile_reference;
                param.unk_obj = &unk_obj.IUnknown_iface;
                param.obj_is_agile = FALSE;
                thread = CreateThread(NULL, 0, test_RoGetAgileReference_thread_proc, &param, 0, NULL);
                flush_events();
                ret = WaitForSingleObject(thread, 100);
                ok(!ret, "WaitForSingleObject failed, error %ld.\n", GetLastError());
            }

            IUnknown_Release(unknown2);
            IUnknown_Release(unknown);
            IAgileReference_Release(agile_reference);
            EXPECT_REF(&unk_obj, 1);

            hr = RoGetAgileReference(option, &IID_IUnknown, &unk_agile_obj.IUnknown_iface, &agile_reference);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
            ok(!!agile_reference, "Got unexpected agile_reference.\n");
            EXPECT_REF(&unk_agile_obj, 2);

            unknown = NULL;
            hr = IAgileReference_Resolve(agile_reference, &IID_IUnknown, (void **)&unknown);
            ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
            ok(!!unknown, "Expected pointer not NULL.\n");
            ok(unknown == &unk_agile_obj.IUnknown_iface, "Expected the same object.\n");
            EXPECT_REF(&unk_agile_obj, 3);

            for (to_type = RO_INIT_SINGLETHREADED; to_type <= RO_INIT_MULTITHREADED; to_type++)
            {
                param.option = option;
                param.from_type = from_type;
                param.to_type = to_type;
                param.agile_reference = agile_reference;
                param.unk_obj = &unk_agile_obj.IUnknown_iface;
                param.obj_is_agile = TRUE;
                thread = CreateThread(NULL, 0, test_RoGetAgileReference_thread_proc, &param, 0, NULL);
                flush_events();
                ret = WaitForSingleObject(thread, 100);
                ok(!ret, "WaitForSingleObject failed, error %ld.\n", GetLastError());
            }

            IUnknown_Release(unknown);
            IAgileReference_Release(agile_reference);
            EXPECT_REF(&unk_obj, 1);

            RoUninitialize();
            winetest_pop_context();
        }
    }
}

static void test_RoGetErrorReportingFlags(void)
{
    UINT32 flags;
    HRESULT hr;

    hr = RoGetErrorReportingFlags(NULL);
    ok(hr == E_POINTER, "Got unexpected hr %#lx.\n", hr);

    hr = RoGetErrorReportingFlags(&flags);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(flags == RO_ERROR_REPORTING_USESETERRORINFO, "Got unexpected flag %#x.\n", flags);
}

/*
 * Windows.UI.Composition.Compositor is the class the XAML/Composition root of an
 * unpackaged WinUI 3 app (WhatsApp Desktop) activates during startup.  Such an app
 * registers its activatable classes through the framework package, so the prefix has no
 * Software\Microsoft\WindowsRuntime\ActivatableClassId key for it and the registry
 * lookup fails; combase has to resolve the class to the builtin windows.ui.composition
 * module the same way it resolves the Windows.UI.ViewManagement.* classes.  A miss is
 * logged as "Failed to find library for Windows.UI.Composition.Compositor" and stalls
 * the app UI thread.
 *
 * The .NET projection (WinRTModule.GetActivationFactory<I>(runtimeClassId, iid)) and the
 * C++/WinRT projection both pass the *class interface* IID (IID_ICompositor,
 * {b403ca50-7f8c-4e83-985f-cc45060036d8}) to RoGetActivationFactory, not
 * IID_IActivationFactory, so both request shapes are pinned below.
 */
static const GUID guid_icompositor = {0xb403ca50, 0x7f8c, 0x4e83, {0x98, 0x5f, 0xcc, 0x45, 0x06, 0x00, 0x36, 0xd8}};

/* Releases an activation result whose vtable is not declared in this test file: the
 * QueryInterface reference plus the reference handed out by RoGetActivationFactory. */
static void release_activation_result(void *obj)
{
    IUnknown *unknown = NULL;

    if (!obj) return;
    if (((IInspectable *)obj)->lpVtbl->QueryInterface((IInspectable *)obj, &IID_IUnknown, (void **)&unknown) == S_OK)
    {
        IUnknown_Release(unknown);
        IUnknown_Release(unknown);
    }
}

static void test_composition_activation_route(void)
{
    static const WCHAR compositor_name[] = L"Windows.UI.Composition.Compositor";
    IActivationFactory *factory = NULL;
    IInspectable *inspectable = NULL;
    void *compositor = NULL, *second = NULL;
    HSTRING str, class_name = NULL;
    const WCHAR *raw;
    HRESULT hr;

    /* The prefix registers this class for the built-in module (loader/wine.inf.in); remove
     * the key so the probe exercises combase's own routing, which is what an unpackaged
     * app needs on a prefix that has no ActivatableClassId entry. */
    RegDeleteKeyW(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Composition.Compositor");

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr);

    hr = WindowsCreateString(compositor_name, ARRAY_SIZE(compositor_name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);

    /* The projection request shape: class interface IID straight from the class name. */
    hr = RoGetActivationFactory(str, &guid_icompositor, &compositor);
    ok(hr == S_OK, "RoGetActivationFactory(IID_ICompositor) returned %#lx.\n", hr);
    ok(!!compositor, "got compositor %p.\n", compositor);

    /* The factory request shape. */
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "RoGetActivationFactory(IID_IActivationFactory) returned %#lx.\n", hr);
    ok(!!factory, "got factory %p.\n", factory);

    if (factory)
    {
        class_name = NULL;
        hr = IActivationFactory_GetRuntimeClassName(factory, &class_name);
        ok(hr == S_OK, "GetRuntimeClassName returned %#lx.\n", hr);
        raw = WindowsGetStringRawBuffer(class_name, NULL);
        ok(!wcscmp(raw, compositor_name), "got class name %s.\n", debugstr_w(raw));
        WindowsDeleteString(class_name);
    }

    if (compositor)
    {
        /* The routed module has to hand out the Compositor object itself, not the
         * factory, so the WinRT identity interfaces answer for it. */
        hr = IInspectable_QueryInterface((IInspectable *)compositor, &IID_IInspectable, (void **)&inspectable);
        ok(hr == S_OK && inspectable, "QI for IInspectable returned %#lx.\n", hr);
        if (inspectable)
        {
            class_name = NULL;
            hr = IInspectable_GetRuntimeClassName(inspectable, &class_name);
            ok(hr == S_OK, "IInspectable::GetRuntimeClassName returned %#lx.\n", hr);
            raw = WindowsGetStringRawBuffer(class_name, NULL);
            ok(!wcscmp(raw, compositor_name), "got instance class name %s.\n", debugstr_w(raw));
            WindowsDeleteString(class_name);
        }
        if (inspectable) IInspectable_Release(inspectable);

        /* Activating twice must create independent compositors, as on Windows. */
        second = NULL;
        hr = RoGetActivationFactory(str, &guid_icompositor, &second);
        ok(hr == S_OK && second, "second activation returned %#lx, %p.\n", hr, second);
        ok(!second || second != compositor, "activation returned the same object %p.\n", second);
        release_activation_result(second);
    }

    /* RoActivateInstance goes through the same route. */
    inspectable = NULL;
    hr = RoActivateInstance(str, &inspectable);
    ok(hr == S_OK, "RoActivateInstance returned %#lx.\n", hr);
    ok(!!inspectable, "got instance %p.\n", inspectable);
    if (inspectable) IInspectable_Release(inspectable);

    WindowsDeleteString(str);
    if (factory) IActivationFactory_Release(factory);
    release_activation_result(compositor);
    RoUninitialize();
}

/*
 * Windows.ApplicationModel.Resources.Core.ResourceContext is the class that .NET Native
 * MRT (mrt100_app/SharedLibrary System.SR) and Microsoft.UI.Xaml activate through
 * ResourceManager.Current at startup; a missing route makes RoGetActivationFactory
 * return REGDB_E_CLASSNOTREG and the app FailFasts with c0000602 (live-captured on
 * Calculator).  The route has to resolve the namespace to the builtin
 * windows.applicationmodel.resources.core module, as the Windows.ApplicationModel.Core.*
 * route does for twinapi.appcore.  The .NET projection passes the class interface IID
 * (IID_IResourceContext, {2fa22f4b-707e-4b27-ad0d-d0d8cd468fd2}) directly, the C++/WinRT
 * factory shape passes IID_IActivationFactory; pin both.
 */
/* The request shape .NET Native MRT (mrt100_app/SharedLibrary) sends on the
 * ResourceContext factory right after activation is IReferenceTracker
 * {11d3b13a-180e-4789-a8be-7712882893e6} (windows.ui.xaml.hosting.referencetracker.idl,
 * IUnknown-derived: [3]ConnectFromTrackerSource [4]DisconnectFromTrackerSource
 * [5]FindTrackerTargets [6]GetReferenceTrackerManager [7]AddRefFromTrackerSource
 * [8]ReleaseFromTrackerSource [9]PegFromTrackerSource).  The CLR reference
 * tracker walks those slots once the QI succeeds, so the routed module has to
 * answer the IID with the tracker contract (slots 3/4/7/8/9 take no arguments;
 * answering them in IInspectable shape writes through the caller's registers
 * and produced Calculator's 280x c0000005 loop).  The 19041 metadata IID
 * {2fa22f4b-...} is the instance interface, not a factory-requestable one. */
static const GUID guid_iresourcecontext_v81 = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};

static void test_resources_core_activation_route(void)
{
    static const WCHAR resourcecontext_name[] = L"Windows.ApplicationModel.Resources.Core.ResourceContext";
    IActivationFactory *factory = NULL;
    IInspectable *inspectable = NULL;
    void *context = NULL;
    HSTRING str;
    const WCHAR *raw;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr);

    hr = WindowsCreateString(resourcecontext_name, ARRAY_SIZE(resourcecontext_name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);

    /* The projection request shape: class interface IID straight from the class name. */
    hr = RoGetActivationFactory(str, &guid_iresourcecontext_v81, &context);
    ok(hr == S_OK, "RoGetActivationFactory(IID_IResourceContext_v81) returned %#lx.\n", hr);
    ok(!!context, "got resourcecontext %p.\n", context);

    /* The factory request shape. */
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "RoGetActivationFactory(IID_IActivationFactory) returned %#lx.\n", hr);
    ok(!!factory, "got factory %p.\n", factory);

    if (factory)
    {
        HSTRING class_name = NULL;
        hr = IActivationFactory_GetRuntimeClassName(factory, &class_name);
        ok(hr == S_OK, "GetRuntimeClassName returned %#lx.\n", hr);
        raw = WindowsGetStringRawBuffer(class_name, NULL);
        ok(!wcscmp(raw, resourcecontext_name), "got class name %s.\n", debugstr_w(raw));
        WindowsDeleteString(class_name);
    }

    if (context)
    {
        /* The routed module has to hand out the ResourceContext object itself, not the
         * factory, so the WinRT identity interfaces answer for it. */
        HSTRING class_name = NULL;
        hr = IInspectable_QueryInterface((IInspectable *)context, &IID_IInspectable, (void **)&inspectable);
        ok(hr == S_OK && inspectable, "QI for IInspectable returned %#lx.\n", hr);
        if (inspectable)
        {
            hr = IInspectable_GetRuntimeClassName(inspectable, &class_name);
            ok(hr == S_OK, "IInspectable::GetRuntimeClassName returned %#lx.\n", hr);
            raw = WindowsGetStringRawBuffer(class_name, NULL);
            ok(!wcscmp(raw, resourcecontext_name), "got instance class name %s.\n", debugstr_w(raw));
            WindowsDeleteString(class_name);
            IInspectable_Release(inspectable);
        }

        /* The QI'd pointer is the object, so the tracker slots the CLR walks
         * have to answer as IReferenceTracker.  Slots 3 and 7 are no-argument
         * calls; the sentinels below are the two measured AV write targets
         * (rdx = 0x24 at slot 3, r8 = stale heap at slot 7), so a vtable in
         * IInspectable shape clobbers them and the contract leaves them. */
        {
            void **vt = *(void **)context;
            ULONG probe = 0x24;
            IID *piids = NULL;
            void *sentinel = (void *)0x5b5b5b5b;
            void *mgr = (void *)0x1;

            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, ULONG *, IID **))vt[3])(context, &probe, &piids);
            ok(hr == S_OK, "ConnectFromTrackerSource returned %#lx.\n", hr);
            ok(probe == 0x24, "slot 3 wrote the caller's ULONG* (%u).\n", probe);
            ok(!piids, "slot 3 wrote the caller's IID** (%p).\n", piids);

            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))vt[7])(context, (void *)0x1, &sentinel);
            ok(hr == S_OK, "AddRefFromTrackerSource returned %#lx.\n", hr);
            ok(sentinel == (void *)0x5b5b5b5b, "slot 7 wrote the caller's out param (%p).\n", sentinel);

            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))vt[6])(context, &mgr);
            ok(hr == S_OK && mgr, "GetReferenceTrackerManager returned %#lx mgr=%p.\n", hr, mgr);
        }
        release_activation_result(context);
    }

    /* RoActivateInstance goes through the same route. */
    inspectable = NULL;
    hr = RoActivateInstance(str, &inspectable);
    ok(hr == S_OK, "RoActivateInstance returned %#lx.\n", hr);
    ok(!!inspectable, "got instance %p.\n", inspectable);
    if (inspectable) IInspectable_Release(inspectable);

    WindowsDeleteString(str);
    if (factory) IActivationFactory_Release(factory);
    RoUninitialize();
}

/*
 * RoGetParameterizedTypeInstanceIID conformance: Windows derives the IID as a
 * UUIDv5-style SHA-1 hash (namespace 11f47ad5-7b73-42c0-abae-878b1e16adee) over the
 * UTF-8 "pinterface({piid};arg;...)" signature string of the instantiation
 * (MS-WDMDB signatures / "The Windows Runtime type system" docs; the same
 * algorithm CoreRT's ComputeIID uses: the "pinterface(" / ";i4)" /
 * "struct(Windows.UI.Xaml.Interop.TypeName;string;enum(Windows.UI.Xaml.Interop.TypeKind;i4))"
 * literals and the 11f47ad5 namespace string are present in Calculator's SharedLibrary.dll).
 * The expected IIDs below are the values produced by that algorithm with the
 * metadata PIIDs (windows-rs generated metadata / the app's own PIID table).
 */
HRESULT WINAPI RoGetParameterizedTypeInstanceIID(UINT32 name_element_count, const WCHAR **name_elements,
                                                 const void *meta_data_locator, GUID *iid,
                                                 ROPARAMIIDHANDLE *hiid);

static void test_RoGetParameterizedTypeInstanceIID(void)
{
    static const WCHAR imapview[]  = L"Windows.Foundation.Collections.IMapView`2";
    static const WCHAR imap[]      = L"Windows.Foundation.Collections.IMap`2";
    static const WCHAR ivectorview[] = L"Windows.Foundation.Collections.IVectorView`1";
    static const WCHAR ivector[]   = L"Windows.Foundation.Collections.IVector`1";
    static const WCHAR iiterable[] = L"Windows.Foundation.Collections.IIterable`1";
    static const WCHAR iiterator[] = L"Windows.Foundation.Collections.IIterator`1";
    static const WCHAR ikvp[]      = L"Windows.Foundation.Collections.IKeyValuePair`2";
    static const WCHAR ireference[] = L"Windows.Foundation.IReference`1";
    static const WCHAR iobservablemap[] = L"Windows.Foundation.Collections.IObservableMap`2";
    static const WCHAR string_name[] = L"Windows.Foundation.String";
    static const WCHAR int32_name[]  = L"Windows.Foundation.Int32";
    static const WCHAR string_kw[]   = L"string";
    static const WCHAR int32_kw[]    = L"i4";
    static const WCHAR resmap[] = L"Windows.ApplicationModel.Resources.Core.ResourceMap";

    static const struct { const WCHAR *elems[5]; UINT32 count; GUID iid; } vectors[] =
    {
        { { imapview, string_name, string_name }, 3, {0x1e036276, 0x2f60, 0x55f6, {0xb7, 0xf3, 0xf8, 0x60, 0x79, 0xe6, 0x90, 0x0b}} },
        { { imap, string_name, string_name },     3, {0xf6d1f700, 0x49c2, 0x52ae, {0x81, 0x54, 0x82, 0x6f, 0x99, 0x08, 0x77, 0x3c}} },
        { { ivectorview, string_name },           2, {0x2f13c006, 0xa03a, 0x5f69, {0xb0, 0x90, 0x75, 0xa4, 0x3e, 0x33, 0x42, 0x3e}} },
        { { ivector, string_name },               2, {0x98b9acc1, 0x4b56, 0x532e, {0xac, 0x73, 0x03, 0xd5, 0x29, 0x1c, 0xca, 0x90}} },
        { { iiterable, string_name },             2, {0xe2fcc7c1, 0x3bfc, 0x5a0b, {0xb2, 0xb0, 0x72, 0xe7, 0x69, 0xd1, 0xcb, 0x7e}} },
        { { iiterator, string_name },             2, {0x8c304ebb, 0x6615, 0x50a4, {0x88, 0x29, 0x87, 0x9e, 0xcd, 0x44, 0x32, 0x36}} },
        { { ikvp, string_name, string_name },     3, {0x60310303, 0x49c5, 0x52e6, {0xab, 0xc6, 0xa9, 0xb3, 0x6e, 0xcc, 0xc7, 0x16}} },
        { { iiterable, ikvp, string_name, string_name }, 4, {0xe9bdaaf0, 0xcbf6, 0x5c72, {0xbe, 0x90, 0x29, 0xcb, 0xf3, 0xa1, 0x31, 0x9b}} },
        { { iiterator, ikvp, string_name, string_name }, 4, {0x05eb86f1, 0x7140, 0x5517, {0xb8, 0x8d, 0xcb, 0xae, 0xbe, 0x57, 0xe6, 0xb1}} },
        { { imapview, string_name, ivectorview, string_name }, 4, {0xf232b158, 0xb565, 0x52ec, {0xab, 0x09, 0x03, 0xb7, 0x08, 0xef, 0x96, 0x86}} },
        { { imap, string_name, ivectorview, string_name },     4, {0xb63397c9, 0x5d89, 0x530b, {0xbe, 0xcf, 0x2b, 0x84, 0x3a, 0x15, 0xc4, 0xe4}} },
        { { ireference, string_name },            2, {0xfd416dfb, 0x2a07, 0x52eb, {0xaa, 0xe3, 0xdf, 0xce, 0x14, 0x11, 0x6c, 0x05}} },
        { { ireference, int32_name },             2, {0x548cefbd, 0xbc8a, 0x5fa0, {0x8d, 0xf2, 0x95, 0x74, 0x40, 0xfc, 0x8b, 0xf4}} },
        { { iobservablemap, string_name, string_name }, 3, {0xac7f26f2, 0xfeb7, 0x5b2a, {0x8a, 0xc4, 0x34, 0x5b, 0xc6, 0x2c, 0xae, 0xde}} },
        { { imapview, string_kw, string_kw },     3, {0x1e036276, 0x2f60, 0x55f6, {0xb7, 0xf3, 0xf8, 0x60, 0x79, 0xe6, 0x90, 0x0b}} },
        { { iiterable, int32_kw },                2, {0x81a643fb, 0xf51c, 0x5565, {0x83, 0xc4, 0xf9, 0x64, 0x25, 0x77, 0x7b, 0x66}} },
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(vectors); i++)
    {
        GUID iid;
        ROPARAMIIDHANDLE handle = 0;
        HRESULT hr;

        iid = GUID_NULL;
        hr = RoGetParameterizedTypeInstanceIID(vectors[i].count, (const WCHAR **)vectors[i].elems, NULL, &iid, &handle);
        ok(hr == S_OK, "vector %u returned %#lx.\n", i, hr);
        if (hr == S_OK)
            ok(IsEqualGUID(&iid, &vectors[i].iid), "vector %u got %s, expected %s.\n",
               i, debugstr_guid(&iid), debugstr_guid(&vectors[i].iid));
    }

    /* arg count / shape errors */
    {
        const WCHAR *bad_arity[] = { imapview, string_name };                 /* 2 of 3 */
        const WCHAR *too_many[]  = { imapview, string_name, string_name, string_name };
        const WCHAR *unknown[]   = { L"Not.A.Known.Interface`1", string_name };
        const WCHAR *not_generic[] = { string_name };
        GUID iid;
        ROPARAMIIDHANDLE handle = 0;
        HRESULT hr;

        hr = RoGetParameterizedTypeInstanceIID(2, bad_arity, NULL, &iid, &handle);
        ok(hr == RO_E_METADATA_INVALID_TYPE_FORMAT || broken(hr == E_INVALIDARG),
           "short args returned %#lx.\n", hr);

        hr = RoGetParameterizedTypeInstanceIID(4, too_many, NULL, &iid, &handle);
        ok(hr == E_INVALIDARG, "extra args returned %#lx.\n", hr);

        hr = RoGetParameterizedTypeInstanceIID(2, unknown, NULL, &iid, &handle);
        ok(hr == RO_E_METADATA_NAME_NOT_FOUND, "unknown name returned %#lx.\n", hr);

        hr = RoGetParameterizedTypeInstanceIID(1, not_generic, NULL, &iid, &handle);
        ok(hr != S_OK, "non-generic base type returned %#lx.\n", hr);

        hr = RoGetParameterizedTypeInstanceIID(1, (const WCHAR *[]){ L"Windows.Foundation.IStringable" }, NULL, &iid, &handle);
        ok(hr == S_OK, "non-generic interface returned %#lx.\n", hr);
        {
            static const GUID istringable = {0x96369f54, 0x8eb6, 0x48f0, {0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3}};
            ok(IsEqualGUID(&iid, &istringable), "IStringable got %s.\n", debugstr_guid(&iid));
        }

        hr = RoGetParameterizedTypeInstanceIID(0, NULL, NULL, &iid, &handle);
        ok(hr == E_INVALIDARG, "empty elements returned %#lx.\n", hr);

        hr = RoGetParameterizedTypeInstanceIID(2, (const WCHAR *[]){ ivectorview, resmap }, NULL, &iid, &handle);
        ok(hr == S_OK, "IVectorView<ResourceMap> returned %#lx.\n", hr);
        {
            static const GUID viv_resmap = {0xf2656ef5, 0xfc27, 0x5c24, {0xa8, 0xc2, 0x16, 0x97, 0xe0, 0xbe, 0x73, 0x6f}};
            ok(IsEqualGUID(&iid, &viv_resmap), "IVectorView<ResourceMap> got %s.\n", debugstr_guid(&iid));
        }
    }
}

START_TEST(roapi)
{
    BOOL ret;

    load_resource(L"wine.combase.test.dll");

    test_implicit_mta();
    test_ActivationFactories();
    test_composition_activation_route();
    test_resources_core_activation_route();
    test_RoGetParameterizedTypeInstanceIID();
    test_RoGetAgileReference();
    test_RoGetErrorReportingFlags();

    SetLastError(0xdeadbeef);
    ret = DeleteFileW(L"wine.combase.test.dll");
    todo_wine_if(!ret && GetLastError() == ERROR_ACCESS_DENIED)
    ok(ret, "Failed to delete file, error %lu\n", GetLastError());
}
