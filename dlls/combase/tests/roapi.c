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

#include "winreg.h"
#include "objbase.h"

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
    ok(hr == S_OK, "Unexpected hr %#lx.\n", hr);
    if(factory)
        IActivationFactory_Release(factory);

    hr = RoActivateInstance(str2, &inspect);
    ok(hr == REGDB_E_CLASSNOTREG, "Unexpected hr %#lx.\n", hr);

    hr = RoActivateInstance(str, &inspect);
    ok(hr == S_OK, "UNexpected hr %#lx.\n", hr);
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
 * Activation kind: in-process registered activation factories.
 *
 * RoRegisterActivationFactories is the registration kind of WinRT activation: a
 * server module (vccorlib140.dll, statically imported by msedgewebview2.exe and every
 * C++/CX component, imports RoRegisterActivationFactories + RoRevokeActivationFactories
 * from api-ms-win-core-winrt-l1-1-0) registers its classes' factory callbacks in-process,
 * after which RoGetActivationFactory must resolve them.  combase's FIXME stub returned
 * S_OK while discarding the registration, so the registrations never resolve: the
 * gate traces show the WebView2 core-server classes answering REGDB_E_CLASSNOTREG
 * right after their registration succeeded ("{daa52b27} never registers").
 */
static const GUID guid_registered_marker = {0xf2b7a531, 0x33c4, 0x4d6e, {0x9a, 0x2f, 0x1c, 0x0d, 0x7e, 0x5a, 0xb3, 0x69}};
static const WCHAR registered_class_name[] = L"Wine.Test.Registered";

struct test_registered_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static struct test_registered_factory test_registered_factory_obj = {{NULL}, 1};

static HRESULT WINAPI test_reg_factory_QueryInterface(IActivationFactory *iface, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IInspectable)
        || IsEqualGUID(riid, &IID_IActivationFactory))
    {
        *ppv = &test_registered_factory_obj;
        IActivationFactory_AddRef(iface);
        return S_OK;
    }
    if (IsEqualGUID(riid, &guid_registered_marker))
    {
        *ppv = &test_registered_factory_obj;
        IActivationFactory_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI test_reg_factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&test_registered_factory_obj.ref);
}

static ULONG WINAPI test_reg_factory_Release(IActivationFactory *iface)
{
    return InterlockedDecrement(&test_registered_factory_obj.ref);
}

static HRESULT WINAPI test_reg_factory_GetIids(IActivationFactory *iface, ULONG *count, IID **ids)
{
    *count = 0;
    *ids = NULL;
    return S_OK;
}

static HRESULT WINAPI test_reg_factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    return WindowsCreateString(registered_class_name, ARRAY_SIZE(registered_class_name) - 1, name);
}

static HRESULT WINAPI test_reg_factory_GetTrustLevel(IActivationFactory *iface, enum TrustLevel *level)
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI test_reg_factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    /* Hand back the factory as the instance: the test identifies it through
     * guid_registered_marker, no activation of its own is needed. */
    IActivationFactory_AddRef(iface);
    *instance = (IInspectable *)&test_registered_factory_obj.IActivationFactory_iface;
    return S_OK;
}

static const IActivationFactoryVtbl test_reg_factory_vtbl =
{
    test_reg_factory_QueryInterface,
    test_reg_factory_AddRef,
    test_reg_factory_Release,
    test_reg_factory_GetIids,
    test_reg_factory_GetRuntimeClassName,
    test_reg_factory_GetTrustLevel,
    test_reg_factory_ActivateInstance,
};

static HRESULT WINAPI test_registered_pfn(HSTRING classid, IActivationFactory **factory)
{
    return test_reg_factory_QueryInterface(&test_registered_factory_obj.IActivationFactory_iface,
            &IID_IActivationFactory, (void **)factory) == S_OK ? S_OK : E_FAIL;
}

/* RoRevokeActivationFactories is an @stub line in combase.spec: the export exists at
 * runtime (E_NOTIMPL) but the stub generates no import symbol, so resolve it
 * dynamically like the two absent exports below. */
typedef HRESULT (WINAPI *pRoRevokeActivationFactories)(RO_REGISTRATION_COOKIE);

/*
 * The Windows App SDK namespaces a packaged WA/WinUI3 app activates at startup
 * (AppLifecycle redirection, MRT Core Resources, the WinAppSDK ApplicationData mirror,
 * the DynamicDependency/WindowsAppRuntime bootstrap) answer from builtin providers on
 * the combase route and from the same modules through the packaged prefix's
 * ActivatableClassId\DllPath seeds.  Pin the factory shape for one class per namespace;
 * the windows.applicationmodel-hosted statics also round-trip their class name through
 * ActivateInstance + GetRuntimeClassName, the check the .NET projection performs.
 */
static void test_winappsdk_activation_route(void)
{
    static const struct
    {
        const WCHAR *class;
        BOOL roundtrip;   /* ActivateInstance answers GetRuntimeClassName = class */
    }
    classes[] =
    {
        { L"Microsoft.Windows.AppLifecycle.AppInstance", FALSE },
        { L"Microsoft.Windows.ApplicationModel.Resources.ResourceContext", FALSE },
        { L"Microsoft.Windows.ApplicationModel.Resources.ResourceLoader", FALSE },
        { L"Microsoft.Windows.Storage.ApplicationData", FALSE },
        { L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentManager", TRUE },
        { L"Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependency", TRUE },
        { L"Microsoft.Windows.ApplicationModel.DynamicDependency.AddPackageDependencyOptions", TRUE },
    };
    DWORD i;
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr );

    for (i = 0; i < ARRAYSIZE( classes ); i++)
    {
        IActivationFactory *factory = NULL;
        IInspectable *instance = NULL;
        HSTRING str, name = NULL;
        const WCHAR *raw;

        hr = WindowsCreateString( classes[i].class, lstrlenW( classes[i].class ), &str );
        ok( hr == S_OK, "WindowsCreateString(%s) returned %#lx.\n", debugstr_w( classes[i].class ), hr );

        factory = NULL;
        hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
        ok( hr == S_OK && factory, "RoGetActivationFactory(%s) returned %#lx, factory %p.\n",
            debugstr_w( classes[i].class ), hr, factory );
        if (!factory)
        {
            WindowsDeleteString( str );
            continue;
        }

        if (classes[i].roundtrip)
        {
            instance = NULL;
            hr = IActivationFactory_ActivateInstance( factory, &instance );
            ok( hr == S_OK && instance, "%s ActivateInstance returned %#lx, instance %p.\n",
                debugstr_w( classes[i].class ), hr, instance );
            if (instance)
            {
                name = NULL;
                hr = IInspectable_GetRuntimeClassName( instance, &name );
                ok( hr == S_OK, "%s GetRuntimeClassName returned %#lx.\n", debugstr_w( classes[i].class ), hr );
                raw = WindowsGetStringRawBuffer( name, NULL );
                ok( raw && !wcscmp( raw, classes[i].class ), "got class name %s.\n", debugstr_w( raw ) );
                WindowsDeleteString( name );
                IInspectable_Release( instance );
            }
        }

        IActivationFactory_Release( factory );
        WindowsDeleteString( str );
    }

    RoUninitialize();
}

/*
 * DeploymentManager.Current (1.4) and the instance's CurrentPackages (1.7) are
 * the WindowsAppSDK deployment enumeration; the rows are the framework packages
 * registered on the AppModel plane (the same AppModel\Packages keys kernelbase
 * answers package identity from), so a framework seeded into the registry must
 * enumerate as a PackageDependencyPackageInfo whose Package.Id.FullName — and
 * DependencyPackageInfo.PackageId — name exactly that package.
 */
static HRESULT call_out( void *iface, unsigned int slot, void **out )
{
    HRESULT (STDMETHODCALLTYPE *fn)( void *, void ** ) = (void *)(*(void ***)iface)[slot];
    return fn( iface, out );
}

static HRESULT call_u32_out( void *iface, unsigned int slot, UINT32 *out )
{
    HRESULT (STDMETHODCALLTYPE *fn)( void *, UINT32 * ) = (void *)(*(void ***)iface)[slot];
    return fn( iface, out );
}

static HRESULT call_in_u32_out( void *iface, unsigned int slot, UINT32 index, void **out )
{
    HRESULT (STDMETHODCALLTYPE *fn)( void *, UINT32, void ** ) = (void *)(*(void ***)iface)[slot];
    return fn( iface, index, out );
}

static void unref( void *iface )
{
    ULONG (STDMETHODCALLTYPE *fn)( void * ) = (void *)(*(void ***)iface)[2];
    fn( iface );
}

static void test_deployment_manager_current_packages(void)
{
    static const WCHAR manager_class[] = L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentManager";
    static const WCHAR fw_name[] = L"WineWpDeploy_7.7.7.7_x64__8wekyb3d8bbwe";
    static const WCHAR fw_path[] = L"C:\\deployfw";
    static const WCHAR fw_key[] =
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\WineWpDeploy_7.7.7.7_x64__8wekyb3d8bbwe";
    struct test_version { WORD Major, Minor, Build, Revision; } version;
    HRESULT (STDMETHODCALLTYPE *get_version)( void *, struct test_version * );
    HRESULT hr;
    HSTRING str = NULL, name = NULL;
    IActivationFactory *factory = NULL;
    HKEY hkey;
    DWORD framework = 1;
    UINT32 count = 0, i;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr );

    if (RegCreateKeyExW( HKEY_CURRENT_USER, fw_key, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_WRITE, NULL, &hkey, NULL ))
    {
        win_skip( "cannot create the AppModel Packages key\n" );
        RoUninitialize();
        return;
    }
    RegSetValueExW( hkey, L"PackagePath", 0, REG_SZ, (const BYTE *)fw_path, (lstrlenW( fw_path ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hkey, L"Framework", 0, REG_DWORD, (const BYTE *)&framework, sizeof(framework) );
    RegCloseKey( hkey );

    hr = WindowsCreateString( manager_class, lstrlenW( manager_class ), &str );
    ok( hr == S_OK, "WindowsCreateString returned %#lx.\n", hr );

    factory = NULL;
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK && factory, "RoGetActivationFactory returned %#lx, factory %p.\n", hr, factory );
    if (FAILED( hr ) || !factory) goto done;

    /* DeploymentManager.Current: the statics get_Current (merged factory slot 8)
     * answers the DeploymentManager instance face. */
    {
        void *manager = NULL, *async = NULL, *view = NULL;
        BOOL found = FALSE;

        hr = call_out( factory, 8, &manager );
        ok( hr == S_OK && manager, "get_Current returned %#lx, manager %p.\n", hr, manager );
        if (FAILED( hr ) || !manager) goto done;

        name = NULL;
        hr = IInspectable_GetRuntimeClassName( (IInspectable *)manager, &name );
        ok( hr == S_OK && name && !wcscmp( WindowsGetStringRawBuffer( name, NULL ), manager_class ),
            "Current GetRuntimeClassName returned %#lx, name %s.\n", hr,
            debugstr_w( WindowsGetStringRawBuffer( name, NULL ) ) );
        WindowsDeleteString( name );

        /* CurrentPackages: born-completed async (instance slot 8) -> GetResults (slot 8)
         * -> IVectorView over the registered frameworks. */
        hr = call_out( manager, 8, &async );
        ok( hr == S_OK && async, "get_CurrentPackages returned %#lx, async %p.\n", hr, async );
        if (FAILED( hr ) || !async) goto done;

        hr = call_out( async, 8, &view );
        ok( hr == S_OK && view, "GetResults returned %#lx, view %p.\n", hr, view );
        if (FAILED( hr ) || !view) goto done;

        hr = call_u32_out( view, 7, &count );  /* IVectorView::get_Size */
        ok( hr == S_OK && count >= 1, "get_Size returned %#lx, size %u.\n", hr, count );

        for (i = 0; i < count && !found; i++)
        {
            void *row = NULL, *info = NULL, *package = NULL, *id = NULL;
            const WCHAR *raw;

            row = NULL;
            hr = call_in_u32_out( view, 6, i, &row );  /* IVectorView::GetAt */
            if (FAILED( hr ) || !row) break;

            hr = call_out( row, 7, &package );  /* IPackageDependencyPackageInfo::get_Package */
            if (SUCCEEDED( hr ) && package)
            {
                hr = call_out( package, 6, &id );  /* IPackage::get_Id */
                if (SUCCEEDED( hr ) && id)
                {
                    name = NULL;
                    hr = call_out( id, 6, (void **)&name );  /* IPackageId::get_FullName */
                    raw = WindowsGetStringRawBuffer( name, NULL );
                    found = SUCCEEDED( hr ) && raw && !wcscmp( raw, fw_name );
                    WindowsDeleteString( name );
                    name = NULL;
                    hr = call_out( id, 7, (void **)&name );  /* IPackageId::get_FamilyName */
                    raw = WindowsGetStringRawBuffer( name, NULL );
                    ok( !found || (SUCCEEDED( hr ) && raw && !wcscmp( raw, L"WineWpDeploy_8wekyb3d8bbwe" )),
                        "PackageId.FamilyName is %s.\n", debugstr_w( raw ) );
                    WindowsDeleteString( name );

                    if (found)
                    {
                        /* The same row's graph face names the package identically,
                         * down to the path and the version parsed out of the name. */
                        hr = call_out( row, 6, &info );  /* get_DependencyPackageInfo */
                        if (SUCCEEDED( hr ) && info)
                        {
                            name = NULL;
                            hr = call_out( info, 7, (void **)&name );  /* get_PackageId */
                            raw = WindowsGetStringRawBuffer( name, NULL );
                            ok( SUCCEEDED( hr ) && raw && !wcscmp( raw, fw_name ),
                                "DependencyPackageInfo.PackageId is %s.\n", debugstr_w( raw ) );
                            WindowsDeleteString( name );
                            name = NULL;
                            hr = call_out( info, 8, (void **)&name );  /* get_PackagePath */
                            raw = WindowsGetStringRawBuffer( name, NULL );
                            ok( SUCCEEDED( hr ) && raw && !wcscmp( raw, fw_path ),
                                "DependencyPackageInfo.PackagePath is %s.\n", debugstr_w( raw ) );
                            WindowsDeleteString( name );

                            get_version = (void *)(*(void ***)info)[9];  /* get_Version */
                            version.Major = version.Minor = version.Build = version.Revision = 0;
                            hr = get_version( info, &version );
                            ok( hr == S_OK && version.Major == 7 && version.Minor == 7
                                && version.Build == 7 && version.Revision == 7,
                                "get_Version returned %#lx, %u.%u.%u.%u.\n", hr,
                                version.Major, version.Minor, version.Build, version.Revision );

                            name = NULL;
                            hr = call_out( info, 6, (void **)&name );  /* get_Dependency (shape check only) */
                            ok( SUCCEEDED( hr ) && name, "DependencyPackageInfo.Dependency returned %#lx, %p.\n", hr, name );
                            if (name)
                            {
                                void *dep_name = NULL;
                                hr = call_out( name, 7, (void **)&dep_name );  /* PackageDependency::get_Name */
                                raw = WindowsGetStringRawBuffer( dep_name, NULL );
                                ok( SUCCEEDED( hr ) && raw && !wcscmp( raw, L"WineWpDeploy_8wekyb3d8bbwe" ),
                                    "Dependency.Name is %s.\n", debugstr_w( raw ) );
                                WindowsDeleteString( dep_name );
                                unref( name );
                            }
                            unref( info );
                        }

                        name = NULL;
                        hr = call_out( package, 7, (void **)&name );  /* IPackage::get_InstalledLocation */
                        ok( SUCCEEDED( hr ) && name, "InstalledLocation returned %#lx, %p.\n", hr, name );
                        if (name)
                        {
                            void *uri = NULL;
                            hr = call_out( name, 6, (void **)&uri );  /* Uri::get_AbsoluteUri */
                            raw = WindowsGetStringRawBuffer( uri, NULL );
                            ok( SUCCEEDED( hr ) && raw && !wcscmp( raw, L"file:///C:\\deployfw" ),
                                "InstalledLocation.AbsoluteUri is %s.\n", debugstr_w( raw ) );
                            WindowsDeleteString( uri );
                            unref( name );
                        }
                    }
                    unref( id );
                }
                unref( package );
            }
            unref( row );
        }
        ok( found, "the seeded framework %s did not enumerate in CurrentPackages.\n", debugstr_w( fw_name ) );
        unref( view );
        unref( async );
        unref( manager );
    }

done:
    RegDeleteTreeW( HKEY_CURRENT_USER, fw_key );
    WindowsDeleteString( str );
    IActivationFactory_Release( factory );
    RoUninitialize();
}

static void test_registered_activation_factories(void)
{
    static const WCHAR compositor_name[] = L"Windows.UI.Composition.Compositor";
    pRoRevokeActivationFactories revoke;
    RO_REGISTRATION_COOKIE cookie = NULL, cookie2 = NULL;
    IActivationFactory *factory = NULL;
    IInspectable *instance = NULL;
    PFNGETACTIVATIONFACTORY callbacks[1];
    HSTRING classes[1], str;
    void *marker = NULL;
    HRESULT hr;

    revoke = (void *)GetProcAddress(GetModuleHandleW(L"combase.dll"), "RoRevokeActivationFactories");
    ok(!!revoke, "combase has no RoRevokeActivationFactories export.\n");

    test_registered_factory_obj.IActivationFactory_iface.lpVtbl = &test_reg_factory_vtbl;
    callbacks[0] = test_registered_pfn;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr);

    hr = WindowsCreateString(registered_class_name, ARRAY_SIZE(registered_class_name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    classes[0] = str;

    /* Pre-state: the class resolves to nothing before registration. */
    marker = NULL;
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, &marker);
    ok(hr == REGDB_E_CLASSNOTREG, "pre-registration RoGetActivationFactory returned %#lx.\n", hr);
    ok(!marker, "got factory %p.\n", marker);

    /* Argument conformance. */
    hr = RoRegisterActivationFactories(NULL, NULL, 1, &cookie);
    ok(hr == E_INVALIDARG, "NULL arrays returned %#lx.\n", hr);
    hr = RoRegisterActivationFactories(classes, callbacks, 0, &cookie);
    ok(hr == E_INVALIDARG, "zero count returned %#lx.\n", hr);
    hr = RoRegisterActivationFactories(classes, callbacks, 1, NULL);
    ok(hr == E_INVALIDARG, "NULL cookie returned %#lx.\n", hr);

    hr = RoRegisterActivationFactories(classes, callbacks, 1, &cookie);
    ok(hr == S_OK, "RoRegisterActivationFactories returned %#lx.\n", hr);
    ok(!!cookie, "got cookie %p.\n", cookie);

    /* The registration has to resolve through RoGetActivationFactory. */
    factory = NULL;
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "registered RoGetActivationFactory returned %#lx.\n", hr);
    ok(factory == &test_registered_factory_obj.IActivationFactory_iface, "got factory %p.\n", factory);
    if (factory) IActivationFactory_Release(factory);

    /* RoActivateInstance rides the same path. */
    instance = NULL;
    hr = RoActivateInstance(str, &instance);
    ok(hr == S_OK, "RoActivateInstance returned %#lx.\n", hr);
    ok(instance == (IInspectable *)&test_registered_factory_obj.IActivationFactory_iface,
       "got instance %p.\n", instance);
    if (instance) IInspectable_Release(instance);

    /* The registration wins over the routes/registry: re-register the name of a
     * routed class and the registered factory has to answer, not the builtin. */
    hr = WindowsCreateString(compositor_name, ARRAY_SIZE(compositor_name) - 1, &classes[0]);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = RoRegisterActivationFactories(classes, callbacks, 1, &cookie2);
    ok(hr == S_OK, "compositor registration returned %#lx.\n", hr);
    marker = NULL;
    hr = RoGetActivationFactory(classes[0], &guid_registered_marker, &marker);
    ok(hr == S_OK, "registered compositor marker QI returned %#lx.\n", hr);
    ok(marker == &test_registered_factory_obj.IActivationFactory_iface, "got marker %p.\n", marker);
    if (marker) IUnknown_Release((IUnknown *)marker);

    hr = revoke ? revoke(NULL) : E_NOTIMPL;
    ok(hr == E_INVALIDARG, "NULL cookie revoke returned %#lx.\n", hr);

    hr = revoke ? revoke(cookie2) : E_NOTIMPL;
    ok(hr == S_OK, "revoke returned %#lx.\n", hr);

    /* After revocation the builtin route answers again and the marker is gone. */
    marker = NULL;
    hr = RoGetActivationFactory(classes[0], &guid_registered_marker, &marker);
    ok(hr == E_NOINTERFACE, "post-revoke marker QI returned %#lx.\n", hr);
    ok(!marker, "got marker %p.\n", marker);
    factory = NULL;
    hr = RoGetActivationFactory(classes[0], &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "post-revoke routed activation returned %#lx.\n", hr);
    if (factory) IActivationFactory_Release(factory);
    WindowsDeleteString(classes[0]);

    hr = revoke ? revoke(cookie) : E_NOTIMPL;
    ok(hr == S_OK, "revoke returned %#lx.\n", hr);

    /* After revocation the class resolves to nothing again. */
    marker = NULL;
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, &marker);
    ok(hr == REGDB_E_CLASSNOTREG, "post-revoke RoGetActivationFactory returned %#lx.\n", hr);
    ok(!marker, "got factory %p.\n", marker);

    WindowsDeleteString(str);
    RoUninitialize();
}

/*
 * Activation kind: RoGetActivationFactoryForHost, the host-scoped factory of the
 * same resolution: SDK roapi.h declares
 *   RoGetActivationFactoryForHost(HSTRING hostId, HSTRING activatableClassId,
 *                                 REFIID interfaceId, void **factory)
 * and Windows resolves the class in the process hosting it; the empty hostId is
 * the in-proc host.  The export is absent from combase entirely (not even a stub
 * line in the spec): a PE that binds it by name fails to load, and the
 * Windows.Data.Json/Microsoft.Windows.AppLifecycle projections probe for it at
 * runtime.  Resolved dynamically so a missing export reports here and not as a
 * load-time failure of the whole test exe.
 */
typedef HRESULT (WINAPI *pRoGetActivationFactoryForHost)(HSTRING, HSTRING, REFIID, void **);

static void test_activation_for_host(void)
{
    static const WCHAR compositor_name[] = L"Windows.UI.Composition.Compositor";
    pRoGetActivationFactoryForHost pfn;
    IActivationFactory *factory = NULL;
    HSTRING host, class_str;
    HRESULT hr;

    pfn = (void *)GetProcAddress(GetModuleHandleW(L"combase.dll"), "RoGetActivationFactoryForHost");
    ok(!!pfn, "combase has no RoGetActivationFactoryForHost export.\n");
    if (!pfn) return;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr);

    hr = WindowsCreateString(L"", 0, &host);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = WindowsCreateString(compositor_name, ARRAY_SIZE(compositor_name) - 1, &class_str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);

    /* The in-proc host resolves the built-in classes. */
    factory = NULL;
    hr = pfn(host, class_str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "RoGetActivationFactoryForHost returned %#lx.\n", hr);
    ok(!!factory, "got factory %p.\n", factory);
    if (factory) IActivationFactory_Release(factory);

    /* The empty hostId is the NULL HSTRING and names the current process. */
    factory = NULL;
    hr = pfn(NULL, class_str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == S_OK, "NULL (empty) hostId returned %#lx.\n", hr);
    ok(!!factory, "got factory %p.\n", factory);
    if (factory) IActivationFactory_Release(factory);

    /* Argument conformance. */
    hr = pfn(host, NULL, &IID_IActivationFactory, (void **)&factory);
    ok(hr == E_INVALIDARG, "NULL classId returned %#lx.\n", hr);
    hr = pfn(host, class_str, NULL, (void **)&factory);
    ok(hr == E_INVALIDARG, "NULL iid returned %#lx.\n", hr);
    hr = pfn(host, class_str, &IID_IActivationFactory, NULL);
    ok(hr == E_INVALIDARG, "NULL factory returned %#lx.\n", hr);

    /* An unknown class keeps the not-registered answer. */
    hr = WindowsCreateString(L"Does.Not.Exist", ARRAY_SIZE(L"Does.Not.Exist") - 1, &class_str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    factory = NULL;
    hr = pfn(host, class_str, &IID_IActivationFactory, (void **)&factory);
    ok(hr == REGDB_E_CLASSNOTREG, "unknown class returned %#lx.\n", hr);
    ok(!factory, "got factory %p.\n", factory);

    WindowsDeleteString(class_str);
    WindowsDeleteString(host);
    RoUninitialize();
}

/*
 * Activation kind: class versioning.  The SDK declares
 *   RoGetClassNameAndVersion(HSTRING activatableClassId, HSTRING *className,
 *                            HSTRING_HEADER *classNameHeader, UINT32 *version)
 * which splits a versioned type reference ("Ns.Class, Version=MAJOR.MINOR") into
 * the bare class name (returned through the caller's HSTRING_HEADER fast-alloc
 * buffer) and the class version packed major<<16|minor; unversioned names answer
 * the name itself with version 0.  AppLifecycle/Redirection and the XAML host
 * projections call it.  The export is absent from combase entirely; resolved
 * dynamically as above.
 */
typedef HRESULT (WINAPI *pRoGetClassNameAndVersion)(HSTRING, HSTRING *, HSTRING_HEADER *, UINT32 *);

static void test_class_name_version(void)
{
    static const WCHAR plain_name[] = L"Wine.Test.Class";
    static const WCHAR versioned_name[] = L"Wine.Test.Class, Version=2.5";
    pRoGetClassNameAndVersion pfn;
    HSTRING_HEADER header;
    HSTRING str, out;
    UINT32 version = 0xdeadbeef;
    HRESULT hr;

    pfn = (void *)GetProcAddress(GetModuleHandleW(L"combase.dll"), "RoGetClassNameAndVersion");
    ok(!!pfn, "combase has no RoGetClassNameAndVersion export.\n");
    if (!pfn) return;

    hr = WindowsCreateString(plain_name, ARRAY_SIZE(plain_name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    out = NULL;
    version = 0xdeadbeef;
    hr = pfn(str, &out, &header, &version);
    ok(hr == S_OK, "RoGetClassNameAndVersion returned %#lx.\n", hr);
    ok(out && !wcscmp(WindowsGetStringRawBuffer(out, NULL), plain_name),
       "unversioned name round-trip mismatch, out %s.\n", debugstr_w(out ? WindowsGetStringRawBuffer(out, NULL) : NULL));
    ok(version == 0, "unversioned class got %#x.\n", version);
    WindowsDeleteString(out);
    WindowsDeleteString(str);

    hr = WindowsCreateString(versioned_name, ARRAY_SIZE(versioned_name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    out = NULL;
    version = 0xdeadbeef;
    hr = pfn(str, &out, &header, &version);
    ok(hr == S_OK, "versioned RoGetClassNameAndVersion returned %#lx.\n", hr);
    ok(out && !wcscmp(WindowsGetStringRawBuffer(out, NULL), plain_name),
       "versioned name split mismatch, out %s.\n", debugstr_w(out ? WindowsGetStringRawBuffer(out, NULL) : NULL));
    ok(version == 0x00020005, "versioned class got %#x.\n", version);
    WindowsDeleteString(out);
    WindowsDeleteString(str);

    /* Argument conformance. */
    hr = WindowsCreateString(plain_name, ARRAY_SIZE(plain_name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = pfn(NULL, &out, &header, &version);
    ok(hr == E_INVALIDARG, "NULL name returned %#lx.\n", hr);
    hr = pfn(str, NULL, &header, &version);
    ok(hr == E_INVALIDARG, "NULL out returned %#lx.\n", hr);
    hr = pfn(str, &out, NULL, &version);
    ok(hr == E_INVALIDARG, "NULL header returned %#lx.\n", hr);
    hr = pfn(str, &out, &header, NULL);
    ok(hr == E_INVALIDARG, "NULL version returned %#lx.\n", hr);
    WindowsDeleteString(str);
}

/*
 * Store roadmap P0-7: the roregistrationapi.h registration entry points that combase
 * exports (API-MS-Win-Core-WinRT-registration-l1-1-0 forwards to combase.dll):
 * RoGetActivatableClassRegistration, the registration lookup of a single activatable
 * class, and RoGetServerActivatableClasses, the activatable classes a server
 * executable is registered to serve.
 *
 * RoGetActivatableClassRegistration is an @stub line in combase.spec: the export
 * exists so PE imports of it bind, and calling it raises the unimplemented-function
 * exception, i.e. aborts the calling process (the same live hazard class
 * RoRevokeActivationFactories carried before it was implemented).
 * RoGetServerActivatableClasses is a FIXME stub that answers S_OK with count 0 for
 * every input.  Both are resolved dynamically so that a missing export reports here
 * instead of failing the load of the whole test exe.
 */
typedef HRESULT (WINAPI *pRoGetActivatableClassRegistration)(HSTRING, IActivationFactory **);
typedef HRESULT (WINAPI *pRoGetServerActivatableClasses)(HSTRING, HSTRING **, DWORD *);

static const WCHAR server_class_hklm[] = L"Wine.Test.ServerClass";
static const WCHAR server_class_hkcu[] = L"Wine.Test.ServerClassUser";
static const WCHAR server_name[] = L"wine.combase.test.server.exe";
static const WCHAR server_name_absent[] = L"wine.combase.test.absent.exe";
static const WCHAR runtime_key[] = L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId";

/* Seed a server-hosted activatable class the way loader/wine.inf seeds the shipped
 * ones: an ActivatableClassId key with the server module in its DllPath value. */
static void seed_server_class(HKEY root, const WCHAR *name, const WCHAR *dllpath)
{
    WCHAR path[MAX_PATH];
    HKEY sub;
    LONG ret;

    lstrcpyW(path, runtime_key);
    lstrcatW(path, L"\\");
    lstrcatW(path, name);

    ret = RegCreateKeyExW(root, path, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &sub, NULL);
    ok(ret == ERROR_SUCCESS, "RegCreateKeyExW(%s) returned %ld.\n", debugstr_w(path), ret);
    if (ret) return;
    ret = RegSetValueExW(sub, L"DllPath", 0, REG_SZ, (const BYTE *)dllpath, (lstrlenW(dllpath) + 1) * sizeof(WCHAR));
    ok(ret == ERROR_SUCCESS, "DllPath seed of %s returned %ld.\n", debugstr_w(name), ret);
    RegSetValueExW(sub, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Both", 5 * sizeof(WCHAR));
    RegCloseKey(sub);
}

static void unseed_server_class(HKEY root, const WCHAR *name)
{
    WCHAR path[MAX_PATH];

    lstrcpyW(path, runtime_key);
    lstrcatW(path, L"\\");
    lstrcatW(path, name);
    RegDeleteKeyW(root, path);
}

static void free_class_ids(HSTRING *ids, DWORD count)
{
    DWORD i;

    for (i = 0; i < count; i++)
        WindowsDeleteString(ids[i]);
    CoTaskMemFree(ids);
}

/*
 * The Calculator (.NET Native UWP) boot's AE (activation-error) census: exactly the
 * RoGetActivationFactory calls the app issues, each requesting the *class interface*
 * IID directly (the .NET projection shape), each resolving through the
 * ActivatableClassId DllPath seed to a builtin provider that answers that IID.
 * Live-captured pairs (forensics_gate.log, thread 0024):
 *
 *   Windows.System.MemoryManager                {6eee351f-6d62-423f-9479-b01f9c9f7669}
 *     -> windows.applicationmodel.resources.core.dll (co-hosted no-op stub factory)
 *   Windows.UI.Xaml.Application                 {06499997-f7b4-45fe-b763-7577d1d3cb4a}
 *     -> windows.ui.xaml.dll
 *   Windows.Foundation.Collections.PropertySet  {af86e2e0-b12d-4c6a-9c5a-d7aa65101e90}
 *     -> wintypes.dll (merged IObservableMap/PropertySet family)
 *
 * The gate prefix ships all three seeds; the test prefix seeds them here, so the
 * registry-backed DllPath leg plus each provider's class-interface answer is pinned
 * on every suite run. MemoryManager is deliberately unseeded in wine.inf for the
 * mrt100_app DllMain fallback, so seed/unseed around the probe.
 */
static const GUID guid_imemorymanager_statics = {0x6eee351f, 0x6d62, 0x423f, {0x94, 0x79, 0xb0, 0x1f, 0x9c, 0x9f, 0x76, 0x69}};
static const GUID guid_iapplication = {0x06499997, 0xf7b4, 0x45fe, {0xb7, 0x63, 0x75, 0x77, 0xd1, 0xd3, 0xcb, 0x4a}};
static const GUID guid_ipropertyset = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};

struct ae_face
{
    const WCHAR *class_name;
    const WCHAR *dllpath;
    const GUID *iid;
};

static const struct ae_face ae_faces[] =
{
    { L"Windows.System.MemoryManager", L"windows.applicationmodel.resources.core.dll", &guid_imemorymanager_statics },
    { L"Windows.UI.Xaml.Application", L"windows.ui.xaml.dll", &guid_iapplication },
    { L"Windows.Foundation.Collections.PropertySet", L"wintypes.dll", &guid_ipropertyset },
};

static void test_dotnet_native_ae_faces(void)
{
    DWORD i;
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr);

    for (i = 0; i < ARRAY_SIZE(ae_faces); i++)
    {
        const struct ae_face *face = &ae_faces[i];
        IInspectable *inspectable = NULL;
        HSTRING str = NULL;
        void *obj = NULL;

        seed_server_class(HKEY_LOCAL_MACHINE, face->class_name, face->dllpath);

        hr = WindowsCreateString(face->class_name, lstrlenW(face->class_name), &str);
        ok(hr == S_OK, "WindowsCreateString(%s) returned %#lx.\n", debugstr_w(face->class_name), hr);

        obj = NULL;
        hr = RoGetActivationFactory(str, face->iid, &obj);
        ok(hr == S_OK, "RoGetActivationFactory(%s, %s) returned %#lx.\n",
           debugstr_w(face->class_name), debugstr_guid(face->iid), hr);
        ok(!!obj, "%s: got object %p.\n", debugstr_w(face->class_name), obj);
        if (obj)
        {
            /* Every class interface inherits IInspectable: the handed-out object
             * answers it, proving the provider face (not just a pointer). */
            inspectable = NULL;
            hr = IInspectable_QueryInterface((IInspectable *)obj, &IID_IInspectable, (void **)&inspectable);
            ok(hr == S_OK && inspectable, "%s: QI(IInspectable) returned %#lx, %p.\n",
               debugstr_w(face->class_name), hr, inspectable);
            if (inspectable) IInspectable_Release(inspectable);
        }
        release_activation_result(obj);

        WindowsDeleteString(str);
        unseed_server_class(HKEY_LOCAL_MACHINE, face->class_name);
    }

    RoUninitialize();
}

static LONG find_class_id(HSTRING *ids, DWORD count, const WCHAR *name)
{
    DWORD i;

    for (i = 0; i < count; i++)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer(ids[i], NULL);
        if (raw && !wcscmp(raw, name)) return i;
    }
    return -1;
}

static void test_server_activatable_classes(void)
{
    pRoGetServerActivatableClasses pfn;
    HSTRING server, absent;
    HSTRING *array = NULL;
    DWORD count = 0, i;
    HRESULT hr;

    pfn = (void *)GetProcAddress(GetModuleHandleW(L"combase.dll"), "RoGetServerActivatableClasses");
    ok(!!pfn, "combase has no RoGetServerActivatableClasses export.\n");

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr);

    seed_server_class(HKEY_LOCAL_MACHINE, server_class_hklm, server_name);
    seed_server_class(HKEY_CURRENT_USER, server_class_hkcu, server_name);

    hr = WindowsCreateString(server_name, ARRAY_SIZE(server_name) - 1, &server);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = WindowsCreateString(server_name_absent, ARRAY_SIZE(server_name_absent) - 1, &absent);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);

    /* NULL out params are pointer errors, not a success and not a crash. */
    count = 0;
    array = NULL;
    hr = pfn(server, NULL, &count);
    ok(hr == E_POINTER, "NULL class array returned %#lx.\n", hr);
    hr = pfn(server, &array, NULL);
    ok(hr == E_POINTER, "NULL count returned %#lx.\n", hr);

    /* Documented contract: an empty server name is REGDB_E_CLASSNOTREG, and so is a
     * server that has no registered classes. */
    count = 0xdead;
    array = (void *)0xdead;
    hr = pfn(NULL, &array, &count);
    ok(hr == REGDB_E_CLASSNOTREG, "empty server name returned %#lx.\n", hr);
    ok(!array, "got array %p.\n", array);
    ok(count == 0, "got count %lu.\n", count);

    count = 1;
    array = (void *)0xdead;
    hr = pfn(absent, &array, &count);
    ok(hr == REGDB_E_CLASSNOTREG, "unregistered server returned %#lx.\n", hr);
    ok(!array, "got array %p.\n", array);
    ok(count == 0, "got count %lu.\n", count);

    /* The registered server answers its class list.  Both the machine configuration
     * (loader/wine.inf's ActivatableClassId keys) and the per-user hive feed it. */
    array = NULL;
    count = 0;
    hr = pfn(server, &array, &count);
    ok(hr == S_OK, "registered server returned %#lx.\n", hr);
    ok(!!array, "got array %p.\n", array);
    ok(count >= 2, "got count %lu.\n", count);
    if (array)
    {
        ok(find_class_id(array, count, server_class_hklm) != -1, "HKLM class missing from the %lu entries.\n", count);
        ok(find_class_id(array, count, server_class_hkcu) != -1, "HKCU class missing from the %lu entries.\n", count);
        /* The documented caller contract: the elements are HSTRINGs (WindowsDeleteString
         * per element) and the buffer is CoTaskMem-allocated. */
        for (i = 0; i < count; i++)
            ok(!!array[i], "entry %lu is not an HSTRING.\n", i);
        free_class_ids(array, count);
    }

    unseed_server_class(HKEY_LOCAL_MACHINE, server_class_hklm);
    unseed_server_class(HKEY_CURRENT_USER, server_class_hkcu);

    /* With the registrations gone the server has no classes again. */
    array = (void *)0xdead;
    count = 0xdead;
    hr = pfn(server, &array, &count);
    ok(hr == REGDB_E_CLASSNOTREG, "post-cleanup server returned %#lx.\n", hr);
    ok(!array, "got array %p.\n", array);
    ok(count == 0, "got count %lu.\n", count);

    WindowsDeleteString(server);
    WindowsDeleteString(absent);
    RoUninitialize();
}

static ULONG test_reg_callback_calls;

static HRESULT WINAPI test_registered_pfn_counted(HSTRING classid, IActivationFactory **factory)
{
    test_reg_callback_calls++;
    return test_registered_pfn(classid, factory);
}

static void test_activatable_class_registration(void)
{
    static const WCHAR compositor_name[] = L"Windows.UI.Composition.Compositor";
    static const WCHAR unknown_name[] = L"Wine.Test.NotRegistered";
    pRoGetActivatableClassRegistration pfn;
    pRoRevokeActivationFactories revoke;
    PFNGETACTIVATIONFACTORY callbacks[1];
    RO_REGISTRATION_COOKIE cookie = NULL;
    IActivationFactory *factory = NULL;
    HSTRING classes[1], str;
    HRESULT hr;

    pfn = (void *)GetProcAddress(GetModuleHandleW(L"combase.dll"), "RoGetActivatableClassRegistration");
    ok(!!pfn, "combase has no RoGetActivatableClassRegistration export.\n");
    revoke = (void *)GetProcAddress(GetModuleHandleW(L"combase.dll"), "RoRevokeActivationFactories");
    ok(!!revoke, "combase has no RoRevokeActivationFactories export.\n");

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr);

    hr = WindowsCreateString(registered_class_name, ARRAY_SIZE(registered_class_name) - 1, &str);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);

    /* A NULL out param is a pointer error: the export must neither crash nor report
     * success, and the process must survive (an @stub line aborts it). */
    hr = pfn(str, NULL);
    ok(hr == E_POINTER, "NULL factory returned %#lx.\n", hr);

    /* An unregistered class answers the class-not-registered code with no factory. */
    factory = NULL;
    hr = WindowsCreateString(unknown_name, ARRAY_SIZE(unknown_name) - 1, &classes[0]);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = pfn(classes[0], &factory);
    ok(hr == REGDB_E_CLASSNOTREG, "unregistered class returned %#lx.\n", hr);
    ok(!factory, "got factory %p.\n", factory);
    WindowsDeleteString(classes[0]);

    /* A class the process registered resolves through the registration table, and the
     * registered factory callback runs exactly once per lookup: the registration API
     * shares RoGetActivationFactory's resolution path instead of stacking a second
     * factory lookup on top of it. */
    test_registered_factory_obj.IActivationFactory_iface.lpVtbl = &test_reg_factory_vtbl;
    callbacks[0] = test_registered_pfn_counted;
    classes[0] = str;
    hr = RoRegisterActivationFactories(classes, callbacks, 1, &cookie);
    ok(hr == S_OK, "RoRegisterActivationFactories returned %#lx.\n", hr);

    test_reg_callback_calls = 0;
    factory = NULL;
    hr = pfn(str, &factory);
    ok(hr == S_OK, "registered class returned %#lx.\n", hr);
    ok(factory == &test_registered_factory_obj.IActivationFactory_iface, "got factory %p.\n", factory);
    ok(test_reg_callback_calls == 1, "the factory callback ran %lu times.\n", test_reg_callback_calls);
    if (factory)
    {
        void *marker = NULL;
        hr = IActivationFactory_QueryInterface(factory, &guid_registered_marker, &marker);
        ok(hr == S_OK, "QI of the handed-back factory returned %#lx.\n", hr);
        if (marker) IUnknown_Release((IUnknown *)marker);
        IActivationFactory_Release(factory);
    }

    /* A registry-seeded class resolves through the same call: the ActivatableClassId
     * DllPath leg, not only the in-process registration table. */
    factory = NULL;
    hr = WindowsCreateString(compositor_name, ARRAY_SIZE(compositor_name) - 1, &classes[0]);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = pfn(classes[0], &factory);
    ok(hr == S_OK, "registry-seeded class returned %#lx.\n", hr);
    ok(!!factory, "got factory %p.\n", factory);
    if (factory) IActivationFactory_Release(factory);
    WindowsDeleteString(classes[0]);

    hr = revoke ? revoke(cookie) : E_NOTIMPL;
    ok(hr == S_OK, "revoke returned %#lx.\n", hr);

    /* After revocation the class is not registered again. */
    factory = NULL;
    hr = pfn(str, &factory);
    ok(hr == REGDB_E_CLASSNOTREG, "post-revoke registered class returned %#lx.\n", hr);
    ok(!factory, "got factory %p.\n", factory);

    WindowsDeleteString(str);
    RoUninitialize();
}

/*
 * Activation-factory apartment semantics: the class's ThreadingModel REG_SZ under
 * ActivatableClassId\<class> decides whether the factory RoGetActivationFactory hands
 * out is agile.  A "Both"/"MTA" class's factory answers IAgileObject (calls cross
 * apartments without marshaling); an "Apartment"/"STA" class's factory does not; a class
 * with no recorded model falls back to the agile activation default ("Both").  This is
 * the same ThreadingModel value ole32's CoCreateInstance apartment logic reads, and the
 * value .NET Native (Calculator) and WinAppSDK (WhatsApp) read to decide marshaling.
 * Wine.Test.Class is seeded by loader/wine.inf with DllPath = the test module, so it
 * resolves to the test factory (which itself answers only IUnknown/IActivationFactory).
 */
static const WCHAR agile_class_name[] = L"Wine.Test.Class";

static void seed_class_threading_model( const WCHAR *model )
{
    WCHAR path[MAX_PATH];
    HKEY sub;
    LONG ret;

    lstrcpyW( path, runtime_key );
    lstrcatW( path, L"\\" );
    lstrcatW( path, agile_class_name );
    ret = RegCreateKeyExW( HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &sub, NULL );
    ok( ret == ERROR_SUCCESS, "RegCreateKeyExW returned %ld.\n", ret );
    if (ret) return;
    if (model)
        RegSetValueExW( sub, L"ThreadingModel", 0, REG_SZ, (const BYTE *)model,
                        (lstrlenW( model ) + 1) * sizeof(WCHAR) );
    RegCloseKey( sub );
}

static void test_activation_factory_threading_model(void)
{
    static const WCHAR model_both[] = L"Both";
    static const WCHAR model_mta[] = L"MTA";
    static const WCHAR model_apartment[] = L"Apartment";
    static const WCHAR model_sta[] = L"STA";
    IActivationFactory *factory = NULL;
    IAgileObject *agile;
    HSTRING str;
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr );

    hr = WindowsCreateString( agile_class_name, ARRAY_SIZE( agile_class_name ) - 1, &str );
    ok( hr == S_OK, "WindowsCreateString returned %#lx.\n", hr );

    /* A "Both" class: the factory is agile, directly and via QI on the factory. */
    seed_class_threading_model( model_both );
    agile = NULL;
    hr = RoGetActivationFactory( str, &IID_IAgileObject, (void **)&agile );
    ok( hr == S_OK && agile, "RoGetActivationFactory(IAgileObject) for Both returned %#lx, %p.\n", hr, agile );
    if (agile) IAgileObject_Release( agile );
    factory = NULL;
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK && factory, "RoGetActivationFactory(IActivationFactory) returned %#lx.\n", hr );
    if (factory)
    {
        agile = NULL;
        hr = IActivationFactory_QueryInterface( factory, &IID_IAgileObject, (void **)&agile );
        ok( hr == S_OK && agile, "factory QI(IAgileObject) for Both returned %#lx, %p.\n", hr, agile );
        if (agile) IAgileObject_Release( agile );
        IActivationFactory_Release( factory );
    }

    /* "MTA" is agile too. */
    seed_class_threading_model( model_mta );
    agile = NULL;
    hr = RoGetActivationFactory( str, &IID_IAgileObject, (void **)&agile );
    ok( hr == S_OK && agile, "RoGetActivationFactory(IAgileObject) for MTA returned %#lx, %p.\n", hr, agile );
    if (agile) IAgileObject_Release( agile );

    /* "STA" and "Apartment" are not agile (a per-user ThreadingModel override). */
    seed_class_threading_model( model_sta );
    agile = (void *)0xdead;
    hr = RoGetActivationFactory( str, &IID_IAgileObject, (void **)&agile );
    todo_wine
    ok( hr == E_NOINTERFACE, "RoGetActivationFactory(IAgileObject) for STA returned %#lx.\n", hr );
    todo_wine
    ok( !agile, "got agile %p.\n", agile );

    seed_class_threading_model( model_apartment );
    agile = (void *)0xdead;
    hr = RoGetActivationFactory( str, &IID_IAgileObject, (void **)&agile );
    todo_wine
    ok( hr == E_NOINTERFACE, "RoGetActivationFactory(IAgileObject) for Apartment returned %#lx.\n", hr );
    todo_wine
    ok( !agile, "got agile %p.\n", agile );

    /* No recorded model: the agile activation default ("Both"). */
    seed_class_threading_model( NULL );
    agile = NULL;
    hr = RoGetActivationFactory( str, &IID_IAgileObject, (void **)&agile );
    ok( hr == S_OK && agile, "RoGetActivationFactory(IAgileObject) for the default returned %#lx, %p.\n", hr, agile );
    if (agile) IAgileObject_Release( agile );

    WindowsDeleteString( str );
    RoUninitialize();
}

/*
 * Out-of-proc (Server) activation conformance.  An ActivatableClassId\<class> key that
 * carries a Server subkey is an out-of-proc class: its factory lives in a server process,
 * and RoGetActivationFactory resolves it through CoCreateInstance(CLSCTX_LOCAL_SERVER) on
 * the CLSID recorded in the Server subkey's default value.  CoCreateInstance starts the
 * server (the CLSID's LocalServer32, with the -Embedding /PROCESSID:{GUID} --clsid={GUID}
 * switches combase's create_server appends) and polls rpcss for the class object the
 * server registers.  This is the shape of {daa52b27-773a-44a3-9895-b138c3e0b2c0}
 * Microsoft.Web.WebView2.Core.CoreWebView2CompositionController server activation, and the
 * Store/packaged activation path apprepository reaches later.
 *
 * The class is seeded at run time (HKCU, over the machine hive) so the test is
 * self-contained; combase_server.exe (built beside this test) registers the class object.
 */
static const GUID test_server_clsid =
    {0x6c1f3a2d, 0x9b4e, 0x4f70, {0xa2, 0x1c, 0x7e, 0x5d, 0x3b, 0x0a, 0x9f, 0x64}};
static const WCHAR server_activation_class[] = L"Wine.Test.ServerClass";
static const WCHAR server_activation_clsidx[] = L"{6c1f3a2d-9b4e-4f70-a21c-7e5d3b0a9f64}";

static void seed_server_activation_class( const WCHAR *server_exe )
{
    WCHAR path[MAX_PATH];
    HKEY sub;

    lstrcpyW( path, runtime_key );
    lstrcatW( path, L"\\" );
    lstrcatW( path, server_activation_class );
    if (RegCreateKeyExW( HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &sub, NULL ))
        return;
    RegSetValueExW( sub, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Both", 5 * sizeof(WCHAR) );
    RegCloseKey( sub );

    lstrcatW( path, L"\\Server" );
    if (!RegCreateKeyExW( HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &sub, NULL ))
    {
        RegSetValueExW( sub, NULL, 0, REG_SZ, (const BYTE *)server_activation_clsidx,
                        (lstrlenW( server_activation_clsidx ) + 1) * sizeof(WCHAR) );
        RegCloseKey( sub );
    }

    lstrcpyW( path, L"Software\\Classes\\CLSID\\" );
    StringFromGUID2( &test_server_clsid, path + 23, 39 );
    if (RegCreateKeyExW( HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_WRITE | KEY_CREATE_SUB_KEY, NULL, &sub, NULL ))
        return;
    if (!RegCreateKeyExW( sub, L"LocalServer32", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &sub, NULL ))
    {
        RegSetValueExW( sub, NULL, 0, REG_SZ, (const BYTE *)server_exe, (lstrlenW( server_exe ) + 1) * sizeof(WCHAR) );
        RegCloseKey( sub );
    }
    RegCloseKey( sub );
}

static void unseed_server_activation_class( void )
{
    WCHAR path[MAX_PATH];

    lstrcpyW( path, runtime_key );
    lstrcatW( path, L"\\" );
    lstrcatW( path, server_activation_class );
    lstrcatW( path, L"\\Server" );
    RegDeleteKeyW( HKEY_CURRENT_USER, path );
    path[lstrlenW( path ) - 7] = 0;
    RegDeleteKeyW( HKEY_CURRENT_USER, path );

    lstrcpyW( path, L"Software\\Classes\\CLSID\\" );
    StringFromGUID2( &test_server_clsid, path + 23, 39 );
    lstrcatW( path, L"\\LocalServer32" );
    RegDeleteKeyW( HKEY_CURRENT_USER, path );
    path[lstrlenW( path ) - 14] = 0;
    RegDeleteKeyW( HKEY_CURRENT_USER, path );
}

static void test_server_class_activation( void )
{
    WCHAR exe[MAX_PATH + 32];
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr );

    /* The server helper exe sits beside this test exe (combase_test.exe -> combase_server.exe). */
    /* LocalServer32 runs this very exe as the combase_server unit (-server). */
    GetModuleFileNameW( NULL, exe, ARRAY_SIZE( exe ) );
    lstrcatW( exe, L" combase_server -server" );

    seed_server_activation_class( exe );

    hr = WindowsCreateString( server_activation_class, ARRAY_SIZE( server_activation_class ) - 1, &str );
    ok( hr == S_OK, "WindowsCreateString returned %#lx.\n", hr );

    /* CoCreateInstance(CLSCTX_LOCAL_SERVER) starts the server, which registers the class
     * object; the factory comes back as the server's IActivationFactory. */
    factory = NULL;
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK && factory, "RoGetActivationFactory(ServerClass) returned %#lx, factory %p.\n", hr, factory );
    if (factory)
    {
        IAgileObject *agile = NULL;
        hr = IActivationFactory_QueryInterface( factory, &IID_IAgileObject, (void **)&agile );
        ok( hr == S_OK && agile, "server factory QI(IAgileObject) returned %#lx, %p.\n", hr, agile );
        if (agile) IAgileObject_Release( agile );
        IActivationFactory_Release( factory );
    }

    WindowsDeleteString( str );
    unseed_server_activation_class();
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
    test_winappsdk_activation_route();
    test_deployment_manager_current_packages();
    test_registered_activation_factories();
    test_activation_for_host();
    test_class_name_version();
    test_server_activatable_classes();
    test_activatable_class_registration();
    test_activation_factory_threading_model();
    test_server_class_activation();
    test_dotnet_native_ae_faces();
    test_RoGetParameterizedTypeInstanceIID();
    test_RoGetAgileReference();
    test_RoGetErrorReportingFlags();

    SetLastError(0xdeadbeef);
    ret = DeleteFileW(L"wine.combase.test.dll");
    todo_wine_if(!ret && GetLastError() == ERROR_ACCESS_DENIED)
    ok(ret, "Failed to delete file, error %lu\n", GetLastError());
}
