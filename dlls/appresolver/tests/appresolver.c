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

/*
 * Vtable layout pinned from Microsoft.Toolkit.Uwp.Notifications.dll ComImport
 * metadata and Microsoft.Internal.FrameworkUdk.dll native call sites.
 */
typedef struct IApplicationResolver IApplicationResolver;

typedef struct IApplicationResolverVtbl
{
    /*** IUnknown methods ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IApplicationResolver *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IApplicationResolver *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IApplicationResolver *This);
    /*** IApplicationResolver methods ***/
    /*  3 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForShortcut)(void);
    /*  4 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForShortcutObject)(void);
    /*  5 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForWindow)(IApplicationResolver *This, HWND hwnd,
            PWSTR *appid, BOOL *relaunch_required, DWORD *host_type, DWORD *reliability);
    /*  6 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForProcess)(IApplicationResolver *This, DWORD pid,
            PWSTR *appid, BOOL *pinning_prevented, BOOL *explicit_appid, BOOL *embedded_shortcut_valid);
    /*  7 */ HRESULT (STDMETHODCALLTYPE *GetShortcutForProcess)(void);
    /*  8 */ HRESULT (STDMETHODCALLTYPE *GetBestShortcutForAppID)(void);
    /*  9 */ HRESULT (STDMETHODCALLTYPE *GetBestShortcutAndAppIDForAppPath)(void);
    /* 10 */ HRESULT (STDMETHODCALLTYPE *CanPinApp)(void);
    /* 11 */ HRESULT (STDMETHODCALLTYPE *CanPinAppShortcut)(void);
    /* 12 */ HRESULT (STDMETHODCALLTYPE *GetRelaunchProperties)(void);
    /* 13 */ HRESULT (STDMETHODCALLTYPE *GenerateShortcutFromWindowProperties)(void);
    /* 14 */ HRESULT (STDMETHODCALLTYPE *GenerateShortcutFromItemProperties)(void);
    /* 15 */ HRESULT (STDMETHODCALLTYPE *reserved_15)(void);
} IApplicationResolverVtbl;

interface IApplicationResolver
{
    CONST_VTBL struct IApplicationResolverVtbl *lpVtbl;
};

#define IApplicationResolver_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IApplicationResolver_AddRef(p) (p)->lpVtbl->AddRef(p)
#define IApplicationResolver_Release(p) (p)->lpVtbl->Release(p)
#define IApplicationResolver_GetAppIDForWindow(p,a,b) (p)->lpVtbl->GetAppIDForWindow(p,a,b,0,0,0)
#define IApplicationResolver_GetAppIDForWindow5(p,a,b,c,d,e) (p)->lpVtbl->GetAppIDForWindow(p,a,b,c,d,e)
#define IApplicationResolver_GetAppIDForProcess(p,a,b,c,d,e) (p)->lpVtbl->GetAppIDForProcess(p,a,b,c,d,e)

/* {660b90c8-73a9-4b58-8cae-355b7f55341b} */
DEFINE_GUID(CLSID_StartMenuCacheAndAppResolver,
    0x660b90c8, 0x73a9, 0x4b58, 0x8c, 0xae, 0x35, 0x5b, 0x7f, 0x55, 0x34, 0x1b);
/* {de25675a-72de-44b4-9373-05170450c140} */
DEFINE_GUID(IID_IApplicationResolver,
    0xde25675a, 0x72de, 0x44b4, 0x93, 0x73, 0x05, 0x17, 0x04, 0x50, 0xc1, 0x40);
/* {46a6eff-aa6b-4b44-8fb3-b30d626d17a3}: NOT the interface used by the callers, must E_NOINTERFACE */
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
    BOOL pinning = TRUE, explicit_flag = TRUE, embedded = TRUE;
    WCHAR path[2048];
    PWSTR appid = NULL;
    HRESULT hr;
    DWORD len;

    if (!resolver) return;

    /* with no explicit AUMID set, Windows falls back to the executable path */
    hr = IApplicationResolver_GetAppIDForProcess(resolver, GetCurrentProcessId(), &appid,
                                                 &pinning, &explicit_flag, &embedded);
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

    /* the full Toolkit projection shape: pid + four out-params, all written */
    pinning = explicit_flag = embedded = TRUE;
    hr = IApplicationResolver_GetAppIDForProcess(resolver, GetCurrentProcessId(), &appid,
                                                 &pinning, &explicit_flag, &embedded);
    ok(hr == S_OK, "GetAppIDForProcess(self, explicit) failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && appid)
    {
        ok(!lstrcmpW(appid, process_aumid), "expected %s, got %s.\n",
           debugstr_w(process_aumid), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
    }
    ok(pinning == FALSE, "pinning_prevented written %d, expected FALSE.\n", pinning);
    ok(explicit_flag == FALSE, "explicit_appid written %d, expected FALSE.\n", explicit_flag);
    ok(embedded == FALSE, "embedded_shortcut_valid written %d, expected FALSE.\n", embedded);

    /* out param validation */
    hr = IApplicationResolver_GetAppIDForProcess(resolver, GetCurrentProcessId(), NULL,
                                                 NULL, NULL, NULL);
    ok(hr == E_INVALIDARG, "GetAppIDForProcess(NULL out) returned %#lx, expected E_INVALIDARG.\n", hr);

    /* dead pid must fail, not fabricate a string */
    hr = IApplicationResolver_GetAppIDForProcess(resolver, 0xffffffff, &appid,
                                                 &pinning, &explicit_flag, &embedded);
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

    if (!RegisterClassExW(&cls)) return NULL;
    return CreateWindowExW(0, cls.lpszClassName, L"appresolver test", 0,
                           0, 0, 100, 100, NULL, NULL, NULL, NULL);
}

static void test_appid_for_window(void)
{
    IApplicationResolver *resolver = create_resolver(CLSCTX_INPROC_SERVER);
    PWSTR appid = NULL;
    IPropertyStore *store;
    PROPVARIANT pv;
    HWND hwnd;
    HRESULT hr;

    if (!resolver) return;

    /* the native FrameworkUdk shape: (NULL, &appid) = current app id, no extra writes */
    hr = IApplicationResolver_GetAppIDForWindow(resolver, NULL, &appid);
    ok(hr == S_OK, "GetAppIDForWindow(NULL, &appid) failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && appid)
    {
        ok(!lstrcmpW(appid, process_aumid), "expected %s, got %s.\n",
           debugstr_w(process_aumid), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
    }

    hwnd = create_test_window();
    ok(hwnd != NULL, "CreateWindowExW failed, gle %lu.\n", GetLastError());

    hr = IApplicationResolver_GetAppIDForWindow5(resolver, hwnd, &appid, NULL, NULL, NULL);
    ok(hr == S_OK, "GetAppIDForWindow failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && appid)
    {
        ok(!lstrcmpW(appid, process_aumid), "expected %s, got %s.\n",
           debugstr_w(process_aumid), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
    }

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
        ok(hr == S_OK, "IPropertyStore::Commit failed, hr %#lx.\n", hr);

        hr = IApplicationResolver_GetAppIDForWindow5(resolver, hwnd, &appid, NULL, NULL, NULL);
        ok(hr == S_OK, "GetAppIDForWindow failed, hr %#lx.\n", hr);
        todo_wine
        ok(appid && !lstrcmpW(appid, window_aumid), "expected window value %s, got %s.\n",
           debugstr_w(window_aumid), debugstr_w(appid));
        CoTaskMemFree(appid);
        appid = NULL;
        IPropertyStore_Release(store);
    }

    hr = IApplicationResolver_GetAppIDForWindow5(resolver, hwnd, NULL, NULL, NULL, NULL);
    ok(hr == E_INVALIDARG, "GetAppIDForWindow(NULL out) returned %#lx, expected E_INVALIDARG.\n", hr);

    DestroyWindow(hwnd);
    IApplicationResolver_Release(resolver);
}

static void test_notimpl_slots(void)
{
    IApplicationResolver *resolver = create_resolver(CLSCTX_INPROC_SERVER);

    if (!resolver) return;

    /* every slot the module does not implement must return E_NOTIMPL, never S_OK */
    ok(resolver->lpVtbl->GetAppIDForShortcut() == E_NOTIMPL, "slot 3 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetAppIDForShortcutObject() == E_NOTIMPL, "slot 4 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetShortcutForProcess() == E_NOTIMPL, "slot 7 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetBestShortcutForAppID() == E_NOTIMPL, "slot 8 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetBestShortcutAndAppIDForAppPath() == E_NOTIMPL, "slot 9 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->CanPinApp() == E_NOTIMPL, "slot 10 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->CanPinAppShortcut() == E_NOTIMPL, "slot 11 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GetRelaunchProperties() == E_NOTIMPL, "slot 12 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GenerateShortcutFromWindowProperties() == E_NOTIMPL, "slot 13 must be E_NOTIMPL.\n");
    ok(resolver->lpVtbl->GenerateShortcutFromItemProperties() == E_NOTIMPL, "slot 14 must be E_NOTIMPL.\n");
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
    static HRESULT (WINAPI *pCanUnload)(void);
    IApplicationResolver *resolver;

    if (!pCanUnload)
    {
        pCanUnload = (void *)GetProcAddress(hAppresolver, "DllCanUnloadNow");
        ok(pCanUnload != NULL, "DllCanUnloadNow not exported.\n");
        if (!pCanUnload) return;
    }

    ok(pCanUnload() == S_OK, "expected S_OK with no live objects.\n");
    resolver = create_resolver(CLSCTX_INPROC_SERVER);
    if (resolver)
    {
        ok(pCanUnload() == S_FALSE, "expected S_FALSE with a live object.\n");
        IApplicationResolver_Release(resolver);
    }
    ok(pCanUnload() == S_OK, "expected S_OK after release.\n");
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
