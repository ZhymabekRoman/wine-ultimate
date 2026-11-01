/*
 * appresolver.dll conformance tests
 *
 * Copyright 2026 Zealot
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include <windows.h>
#include <shobjidl.h>
#include <appmodel.h>
#include "shellapi.h"
#include <propsys.h>
#include <propkey.h>
#include <initguid.h>

#include "wine/test.h"

typedef struct IApplicationResolver IApplicationResolver;

typedef struct IApplicationResolverVtbl
{
    /*** IUnknown methods ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IApplicationResolver *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IApplicationResolver *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IApplicationResolver *This);
    /*** IApplicationResolver methods ***/
    /*  3 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForWindow)(IApplicationResolver *This, HWND hwnd,
            PWSTR *appid, BOOL *relaunch_required, DWORD *host_type, DWORD *reliability);
    /*  4 */ HRESULT (STDMETHODCALLTYPE *GetWindowFromAppID)(IApplicationResolver *This,
            const WCHAR *appid, HWND *hwnd);
    /*  5 */ HRESULT (STDMETHODCALLTYPE *GetFirstWindowFromAppID)(void);
    /*  6 */ HRESULT (STDMETHODCALLTYPE *GetWindowFromWindow)(void);
    /*  7 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForProcess)(IApplicationResolver *This,
            DWORD pid, PWSTR *appid);
    /*  8 */ HRESULT (STDMETHODCALLTYPE *RelaunchApplication)(void);
    /*  9 */ HRESULT (STDMETHODCALLTYPE *SetWindowFromAppID)(void);
    /* 10 */ HRESULT (STDMETHODCALLTYPE *GetIconPath)(void);
    /* 11 */ HRESULT (STDMETHODCALLTYPE *GetShortNamePath)(void);
    /* 12 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForCurrentProcess)(void);
    /* 13 */ HRESULT (STDMETHODCALLTYPE *reserved_13)(void);
    /* 14 */ HRESULT (STDMETHODCALLTYPE *reserved_14)(void);
    /* 15 */ HRESULT (STDMETHODCALLTYPE *reserved_15)(void);
} IApplicationResolverVtbl;

interface IApplicationResolver
{
    CONST_VTBL struct IApplicationResolverVtbl *lpVtbl;
};

#define IApplicationResolver_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IApplicationResolver_AddRef(p) (p)->lpVtbl->AddRef(p)
#define IApplicationResolver_Release(p) (p)->lpVtbl->Release(p)
#define IApplicationResolver_GetAppIDForWindow(p,a,b,c,d,e) (p)->lpVtbl->GetAppIDForWindow(p,a,b,c,d,e)
#define IApplicationResolver_GetWindowFromAppID(p,a,b) (p)->lpVtbl->GetWindowFromAppID(p,a,b)
#define IApplicationResolver_GetAppIDForProcess(p,a,b) (p)->lpVtbl->GetAppIDForProcess(p,a,b)

/* {660b90c8-73a9-4b58-8cae-355b7f55341b} */
DEFINE_GUID(CLSID_StartMenuCacheAndAppResolver,
    0x660b90c8, 0x73a9, 0x4b58, 0x8c, 0xae, 0x35, 0x5b, 0x7f, 0x55, 0x34, 0x1b);
/* {de25675a-72de-44b4-9373-05170450c140} */
DEFINE_GUID(IID_IApplicationResolver,
    0xde25675a, 0x72de, 0x44b4, 0x93, 0x73, 0x05, 0x17, 0x04, 0x50, 0xc1, 0x40);
/* {46a6eff-aa6b-4b44-8fb3-b30d626d17a3}: NOT the interface used by the caller, must E_NOINTERFACE */
DEFINE_GUID(IID_MysteryResolver,
    0x46a6eff, 0xaa6b, 0x4b44, 0x8f, 0xb3, 0xb3, 0x0d, 0x62, 0x6d, 0x17, 0xa3);

HRESULT WINAPI SetCurrentProcessExplicitAppUserModelID(const WCHAR *appid);

static HMODULE hAppresolver;

static const WCHAR process_aumid[] = L"Wine.AppResolver.Test.Process";
static const WCHAR window_aumid[]  = L"Wine.AppResolver.Test.Window";

static IApplicationResolver *create_resolver(DWORD ctx)
{
    IApplicationResolver *resolver = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_StartMenuCacheAndAppResolver, NULL, ctx,
                                  &IID_IApplicationResolver, (void **)&resolver);
    ok(hr == S_OK, "CoCreateInstance failed, hr %#lx.\n", hr);
    return resolver;
}

static void test_create(void)
{
    IUnknown *unk, *unk2;
    IApplicationResolver *resolver;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_StartMenuCacheAndAppResolver, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (void **)&unk);
    ok(hr == S_OK, "CoCreateInstance IUnknown failed, hr %#lx.\n", hr);
    if (FAILED(hr)) return;

    hr = IUnknown_QueryInterface(unk, &IID_IApplicationResolver, (void **)&resolver);
    ok(hr == S_OK, "QI IApplicationResolver failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IUnknown_QueryInterface(unk, &IID_IUnknown, (void **)&unk2);
        ok(hr == S_OK, "QI IUnknown failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr) && unk2)
        {
            ok(unk2 == (IUnknown *)resolver, "expected identical interface pointers, got %p and %p.\n", unk2, resolver);
            IUnknown_Release(unk2);
        }

        /* unimplemented interfaces must not lie with S_OK */
        hr = IUnknown_QueryInterface(unk, &IID_IPersist, (void **)&unk2);
        ok(hr == E_NOINTERFACE, "QI IPersist returned %#lx, expected E_NOINTERFACE.\n", hr);
        if (SUCCEEDED(hr) && unk2) IUnknown_Release(unk2);

        hr = IUnknown_QueryInterface(unk, &IID_IPropertyStore, (void **)&unk2);
        ok(hr == E_NOINTERFACE, "QI IPropertyStore returned %#lx, expected E_NOINTERFACE.\n", hr);
        if (SUCCEEDED(hr) && unk2) IUnknown_Release(unk2);

        hr = IUnknown_QueryInterface(unk, &IID_MysteryResolver, (void **)&unk2);
        ok(hr == E_NOINTERFACE, "QI {46a6eff-...} returned %#lx, expected E_NOINTERFACE.\n", hr);
        if (SUCCEEDED(hr) && unk2) IUnknown_Release(unk2);

        IApplicationResolver_Release(resolver);
    }
    IUnknown_Release(unk);
}

/* the WhatsApp/WebView2 caller goes through CoCreateInstanceEx with ctx 0x15 */
static void test_create_instance_ex(void)
{
    MULTI_QI mqi = { &IID_IApplicationResolver, NULL, S_OK };
    HRESULT hr;

    hr = CoCreateInstanceEx(&CLSID_StartMenuCacheAndAppResolver, NULL,
                            CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
                            NULL, 1, &mqi);
    ok(hr == S_OK, "CoCreateInstanceEx ctx 0x15 failed, hr %#lx.\n", hr);
    ok(mqi.hr == S_OK, "MULTI_QI.hr %#lx, expected S_OK.\n", mqi.hr);
    if (mqi.pItf) IUnknown_Release(mqi.pItf);

    /* an unregistered clsid fails at the registry lookup, before any factory runs */
    hr = CoCreateInstance(&IID_MysteryResolver, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (void **)&mqi.pItf);
    ok(hr == REGDB_E_CLASSNOTREG, "CoCreateInstance bogus clsid returned %#lx, expected REGDB_E_CLASSNOTREG.\n", hr);
    if (SUCCEEDED(hr) && mqi.pItf) IUnknown_Release(mqi.pItf);
}

static void test_appid_for_process(void)
{
    IApplicationResolver *resolver = create_resolver(CLSCTX_INPROC_SERVER);
    WCHAR path[2048];
    PWSTR appid = NULL;
    HRESULT hr;
    DWORD len;

    if (!resolver) return;

    /* with no explicit AUMID set, Windows falls back to the executable path */
    hr = IApplicationResolver_GetAppIDForProcess(resolver, GetCurrentProcessId(), &appid);
    ok(hr == S_OK, "GetAppIDForProcess(self) failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && appid)
    {
        len = GetModuleFileNameW(NULL, path, ARRAY_SIZE(path));
        ok(len && !lstrcmpiW(appid, path), "expected exe path appid %s, got %s.\n",
           debugstr_w(path), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
    }

    hr = SetCurrentProcessExplicitAppUserModelID(process_aumid);
    ok(hr == S_OK, "SetCurrentProcessExplicitAppUserModelID failed, hr %#lx.\n", hr);

    hr = IApplicationResolver_GetAppIDForProcess(resolver, GetCurrentProcessId(), &appid);
    ok(hr == S_OK, "GetAppIDForProcess(self, explicit) failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && appid)
    {
        ok(!lstrcmpW(appid, process_aumid), "expected %s, got %s.\n",
           debugstr_w(process_aumid), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
    }

    /* out param validation */
    hr = IApplicationResolver_GetAppIDForProcess(resolver, GetCurrentProcessId(), NULL);
    ok(hr == E_INVALIDARG, "GetAppIDForProcess(NULL out) returned %#lx, expected E_INVALIDARG.\n", hr);

    /* dead pid must fail, not fabricate a string */
    hr = IApplicationResolver_GetAppIDForProcess(resolver, 0xffffffff, &appid);
    ok(FAILED(hr), "GetAppIDForProcess(invalid pid) returned %#lx, expected failure.\n", hr);
    ok(!appid, "expected NULL appid on failure, got %s.\n", debugstr_w(appid));
    CoTaskMemFree(appid);

    IApplicationResolver_Release(resolver);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static HWND create_test_window(void)
{
    WNDCLASSEXW cls = { .cbSize = sizeof(cls), .lpfnWndProc = wnd_proc,
                        .lpszClassName = L"appresolver_test_class" };
    static HWND result;

    if (!RegisterClassExW(&cls)) return NULL;
    result = CreateWindowExW(0, cls.lpszClassName, L"appresolver test", 0,
                             0, 0, 100, 100, NULL, NULL, NULL, NULL);
    return result;
}

static void test_appid_for_window(void)
{
    IApplicationResolver *resolver = create_resolver(CLSCTX_INPROC_SERVER);
    BOOL relaunch = TRUE;
    DWORD host_type = 0xdead, reliability = 0xdead;
    PWSTR appid = NULL;
    IPropertyStore *store;
    PROPVARIANT pv;
    HWND hwnd;
    HRESULT hr;

    if (!resolver) return;

    /* process explicit AUMID (set by previous test) must be inherited by windows */
    hwnd = create_test_window();
    ok(hwnd != NULL, "CreateWindowExW failed, gle %lu.\n", GetLastError());

    hr = IApplicationResolver_GetAppIDForWindow(resolver, hwnd, &appid, &relaunch,
                                                &host_type, &reliability);
    ok(hr == S_OK, "GetAppIDForWindow failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && appid)
    {
        ok(!lstrcmpW(appid, process_aumid), "expected %s, got %s.\n",
           debugstr_w(process_aumid), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
    }
    ok(!relaunch, "expected relaunch_required FALSE, got %d.\n", relaunch);

    /* documented precedence: the window property store AppUserModelID wins over
     * the process explicit value (Wine's window property store is a stub, todo) */
    store = NULL;
    hr = SHGetPropertyStoreForWindow(hwnd, &IID_IPropertyStore, (void **)&store);
    ok(hr == S_OK, "SHGetPropertyStoreForWindow failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && store)
    {
        PropVariantInit(&pv);
        pv.vt = VT_LPWSTR;
        pv.pwszVal = (WCHAR *)window_aumid;
        hr = IPropertyStore_SetValue(store, &PKEY_AppUserModel_ID, &pv);
        PropVariantClear(&pv);
        ok(hr == S_OK, "IPropertyStore::SetValue failed, hr %#lx.\n", hr);
        hr = IPropertyStore_Commit(store);
        ok(hr == S_OK, "IPropertyStore::Save failed, hr %#lx.\n", hr);

        hr = IApplicationResolver_GetAppIDForWindow(resolver, hwnd, &appid, NULL, NULL, NULL);
        ok(hr == S_OK, "GetAppIDForWindow failed, hr %#lx.\n", hr);
        todo_wine
        ok(appid && !lstrcmpW(appid, window_aumid), "expected window value %s, got %s.\n",
           debugstr_w(window_aumid), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
        IPropertyStore_Release(store);
    }

    /* invalid window */
    hr = IApplicationResolver_GetAppIDForWindow(resolver, NULL, &appid, NULL, NULL, NULL);
    ok(hr == E_INVALIDARG, "GetAppIDForWindow(NULL) returned %#lx, expected E_INVALIDARG.\n", hr);
    hr = IApplicationResolver_GetAppIDForWindow(resolver, hwnd, NULL, NULL, NULL, NULL);
    ok(hr == E_INVALIDARG, "GetAppIDForWindow(NULL out) returned %#lx, expected E_INVALIDARG.\n", hr);

    /* GetWindowFromAppID matches the window whose effective AUMID is the window
     * store value; on Wine the window store is stubbed, so this is native-only */
    {
        HWND found = NULL;
        hr = IApplicationResolver_GetWindowFromAppID(resolver, window_aumid, &found);
        todo_wine
        ok(hr == S_OK, "GetWindowFromAppID returned %#lx, expected S_OK.\n", hr);
        todo_wine
        ok(found == hwnd, "expected hwnd %p, got %p.\n", hwnd, found);
    }

    DestroyWindow(hwnd);
    IApplicationResolver_Release(resolver);
}

static void test_notimpl_slots(void)
{
    IApplicationResolver *resolver = create_resolver(CLSCTX_INPROC_SERVER);

    if (!resolver) return;

    /* every slot the module does not implement must return E_NOTIMPL, never S_OK */
    ok(resolver->lpVtbl->GetFirstWindowFromAppID() == E_NOTIMPL, "slot 5 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetWindowFromWindow() == E_NOTIMPL, "slot 6 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->RelaunchApplication() == E_NOTIMPL, "slot 8 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->SetWindowFromAppID() == E_NOTIMPL, "slot 9 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetIconPath() == E_NOTIMPL, "slot 10 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetShortNamePath() == E_NOTIMPL, "slot 11 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetAppIDForCurrentProcess() == E_NOTIMPL, "slot 12 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->reserved_13() == E_NOTIMPL, "slot 13 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->reserved_15() == E_NOTIMPL, "slot 15 must be E_NOTIMPL.\n");

    IApplicationResolver_Release(resolver);
}

static void test_factory(void)
{
    static HRESULT (WINAPI *pDllGetClassObject)(REFCLSID, REFIID, void **);
    IClassFactory *factory = NULL;
    HRESULT hr;

    pDllGetClassObject = (void *)GetProcAddress(hAppresolver, "DllGetClassObject");
    ok(pDllGetClassObject != NULL, "DllGetClassObject not exported.\n");
    if (!pDllGetClassObject) return;

    hr = pDllGetClassObject(&CLSID_StartMenuCacheAndAppResolver, &IID_IClassFactory, (void **)&factory);
    ok(hr == S_OK, "DllGetClassObject(clsid, IClassFactory) returned %#lx.\n", hr);
    if (SUCCEEDED(hr) && factory) IClassFactory_Release(factory);

    factory = NULL;
    hr = pDllGetClassObject(&IID_MysteryResolver, &IID_IClassFactory, (void **)&factory);
    ok(hr == CLASS_E_CLASSNOTAVAILABLE, "DllGetClassObject(bogus clsid) returned %#lx, expected CLASS_E_CLASSNOTAVAILABLE.\n", hr);
    if (SUCCEEDED(hr) && factory) IClassFactory_Release(factory);
}

static void test_can_unload(void)
{
    static HRESULT (WINAPI *pDllCanUnloadNow)(void);
    IApplicationResolver *resolver;

    if (!pDllCanUnloadNow)
    {
        pDllCanUnloadNow = (void *)GetProcAddress(hAppresolver, "DllCanUnloadNow");
        ok(pDllCanUnloadNow != NULL, "DllCanUnloadNow not exported.\n");
        if (!pDllCanUnloadNow) return;
    }

    ok(pDllCanUnloadNow() == S_OK, "expected S_OK with no live objects.\n");
    resolver = create_resolver(CLSCTX_INPROC_SERVER);
    if (resolver)
    {
        ok(pDllCanUnloadNow() == S_FALSE, "expected S_FALSE with a live object.\n");
        IApplicationResolver_Release(resolver);
    }
    ok(pDllCanUnloadNow() == S_OK, "expected S_OK after release.\n");
}

START_TEST(appresolver)
{
    HRESULT hr;

    hAppresolver = LoadLibraryW(L"appresolver.dll");
    ok(hAppresolver != NULL, "LoadLibrary failed, gle %lu.\n", GetLastError());
    if (!hAppresolver) return;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx failed, hr %#lx.\n", hr);

    test_create();
    test_create_instance_ex();
    test_appid_for_process();
    test_appid_for_window();
    test_notimpl_slots();
    test_factory();
    test_can_unload();

    CoUninitialize();
    FreeLibrary(hAppresolver);
}
