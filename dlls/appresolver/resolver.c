/*
 * Start Menu Cache and App Resolver: IApplicationResolver implementation
 *
 * Copyright 2026 Zealot
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "winnls.h"
#include "winternl.h"
#include "objbase.h"
#include "propsys.h"
#include "propkey.h"
#include "appmodel.h"
#include "shellapi.h"
#include "wine/debug.h"

#include "appresolver.h"

WINE_DEFAULT_DEBUG_CHANNEL(appresolver);

extern LONG appresolver_object_count;

typedef struct
{
    IApplicationResolver IApplicationResolver_iface;
    LONG ref;
} AppResolver;

static inline AppResolver *impl_from_IApplicationResolver(IApplicationResolver *iface)
{
    return CONTAINING_RECORD(iface, AppResolver, IApplicationResolver_iface);
}

static HRESULT dup_appid(const WCHAR *src, PWSTR *dst)
{
    size_t bytes = (lstrlenW(src) + 1) * sizeof(WCHAR);

    *dst = CoTaskMemAlloc(bytes);
    if (!*dst) return E_OUTOFMEMORY;
    memcpy(*dst, src, bytes);
    return S_OK;
}

/*
 * Wine-tracked process AUMID: the value set through SetCurrentProcessExplicitAppUserModelID
 * (stored in the PEB as STARTF_TITLEISAPPID WindowTitle, same as shcore). Windows
 * falls back to a path-derived app id (the executable path) when none was set.
 */
static HRESULT get_process_appid(PWSTR *out)
{
    RTL_USER_PROCESS_PARAMETERS *params;
    WCHAR path[APPLICATION_USER_MODEL_ID_MAX_LENGTH * 8];
    DWORD len;
    BOOL copied = FALSE;

    *out = NULL;

    RtlAcquirePebLock();
    params = RtlGetCurrentPeb()->ProcessParameters;
    if ((params->dwFlags & STARTF_TITLEISAPPID) && params->WindowTitle.Buffer &&
            params->WindowTitle.Length)
    {
        size_t chars = params->WindowTitle.Length / sizeof(WCHAR);
        *out = CoTaskMemAlloc((chars + 1) * sizeof(WCHAR));
        if (*out)
        {
            memcpy(*out, params->WindowTitle.Buffer, params->WindowTitle.Length);
            (*out)[chars] = 0;
            copied = TRUE;
        }
    }
    RtlReleasePebLock();

    if (copied) return S_OK;
    if (*out) return E_OUTOFMEMORY;

    len = GetModuleFileNameW(NULL, path, ARRAY_SIZE(path));
    if (!len) return HRESULT_FROM_WIN32(GetLastError());
    return dup_appid(path, out);
}

static HRESULT get_window_appid(HWND hwnd, PWSTR *out)
{
    IPropertyStore *store = NULL;
    PROPVARIANT pv;
    HRESULT hr;

    *out = NULL;

    /* documented precedence: window property store value wins over process default */
    hr = SHGetPropertyStoreForWindow(hwnd, &IID_IPropertyStore, (void **)&store);
    if (SUCCEEDED(hr) && store)
    {
        PropVariantInit(&pv);
        hr = IPropertyStore_GetValue(store, &PKEY_AppUserModel_ID, &pv);
        IPropertyStore_Release(store);
        if (SUCCEEDED(hr) && pv.vt == VT_LPWSTR && pv.pwszVal && pv.pwszVal[0])
        {
            hr = dup_appid(pv.pwszVal, out);
            PropVariantClear(&pv);
            return hr;
        }
        PropVariantClear(&pv);
    }

    return get_process_appid(out);
}

/***********************************************************************
 * IUnknown
 */

static HRESULT WINAPI AppResolver_QueryInterface(IApplicationResolver *iface, REFIID riid, void **ppv)
{
    AppResolver *This = impl_from_IApplicationResolver(iface);

    TRACE("(%p %s %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv) return E_INVALIDARG;
    *ppv = NULL;

    if (IsEqualIID(&IID_IUnknown, riid) || IsEqualIID(&IID_IApplicationResolver, riid))
    {
        *ppv = &This->IApplicationResolver_iface;
        IApplicationResolver_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI AppResolver_AddRef(IApplicationResolver *iface)
{
    AppResolver *This = impl_from_IApplicationResolver(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);
    return ref;
}

static ULONG WINAPI AppResolver_Release(IApplicationResolver *iface)
{
    AppResolver *This = impl_from_IApplicationResolver(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);
    if (!ref)
    {
        InterlockedDecrement(&appresolver_object_count);
        CoTaskMemFree(This);
    }
    return ref;
}

/***********************************************************************
 * IApplicationResolver
 */

/* slot 3 */
static HRESULT WINAPI AppResolver_GetAppIDForWindow(IApplicationResolver *iface, HWND hwnd,
        PWSTR *appid, BOOL *relaunch_required, DWORD *host_type, DWORD *reliability)
{
    TRACE("(%p %p %p %p %p %p)\n", iface, hwnd, appid, relaunch_required, host_type, reliability);

    if (!appid) return E_INVALIDARG;
    *appid = NULL;

    if (!hwnd || !IsWindow(hwnd)) return E_INVALIDARG;

    if (relaunch_required) *relaunch_required = FALSE;
    if (host_type) *host_type = 0;
    if (reliability) *reliability = 0;

    return get_window_appid(hwnd, appid);
}

/* slot 4 */
struct find_appid_ctx
{
    const WCHAR *appid;
    HWND hwnd;
};

static BOOL CALLBACK find_appid_enum(HWND hwnd, LPARAM lparam)
{
    struct find_appid_ctx *ctx = (struct find_appid_ctx *)lparam;
    PWSTR appid = NULL;

    if (GetWindowThreadProcessId(hwnd, NULL) != GetCurrentProcessId()) return TRUE;
    if (get_window_appid(hwnd, &appid) != S_OK) return TRUE;

    if (appid && !lstrcmpiW(appid, ctx->appid))
    {
        CoTaskMemFree(appid);
        ctx->hwnd = hwnd;
        return FALSE;
    }
    CoTaskMemFree(appid);
    return TRUE;
}

static HRESULT WINAPI AppResolver_GetWindowFromAppID(IApplicationResolver *iface,
        const WCHAR *appid, HWND *hwnd)
{
    struct find_appid_ctx ctx = { appid, NULL };

    TRACE("(%p %s %p)\n", iface, debugstr_w(appid), hwnd);

    if (!appid || !hwnd) return E_INVALIDARG;
    *hwnd = NULL;

    EnumWindows(find_appid_enum, (LPARAM)&ctx);
    if (!ctx.hwnd) return E_FAIL;
    *hwnd = ctx.hwnd;
    return S_OK;
}

/* slot 7 */
static HRESULT WINAPI AppResolver_GetAppIDForProcess(IApplicationResolver *iface,
        DWORD pid, PWSTR *appid)
{
    TRACE("(%p %lx %p)\n", iface, pid, appid);

    if (!appid) return E_INVALIDARG;
    *appid = NULL;

    if (pid == GetCurrentProcessId())
        return get_process_appid(appid);

    /* path-derived app id for other processes, Windows behavior for non-packaged apps */
    {
        HANDLE process;
        WCHAR path[APPLICATION_USER_MODEL_ID_MAX_LENGTH * 8];
        DWORD size = ARRAY_SIZE(path);
        HRESULT hr = S_OK;

        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return HRESULT_FROM_WIN32(GetLastError());

        if (!QueryFullProcessImageNameW(process, 0, path, &size))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else
            hr = dup_appid(path, appid);

        CloseHandle(process);
        return hr;
    }
}

/* unimplemented slots */
#define NOTIMPL(name) static HRESULT WINAPI name(void) { FIXME("stub\n"); return E_NOTIMPL; }
NOTIMPL(AppResolver_GetFirstWindowFromAppID)
NOTIMPL(AppResolver_GetWindowFromWindow)
NOTIMPL(AppResolver_RelaunchApplication)
NOTIMPL(AppResolver_SetWindowFromAppID)
NOTIMPL(AppResolver_GetIconPath)
NOTIMPL(AppResolver_GetShortNamePath)
NOTIMPL(AppResolver_GetAppIDForCurrentProcess)
NOTIMPL(AppResolver_reserved_13)
NOTIMPL(AppResolver_reserved_14)
NOTIMPL(AppResolver_reserved_15)

static const IApplicationResolverVtbl AppResolver_vtbl =
{
    AppResolver_QueryInterface,
    AppResolver_AddRef,
    AppResolver_Release,
    AppResolver_GetAppIDForWindow,
    AppResolver_GetWindowFromAppID,
    AppResolver_GetFirstWindowFromAppID,
    AppResolver_GetWindowFromWindow,
    AppResolver_GetAppIDForProcess,
    AppResolver_RelaunchApplication,
    AppResolver_SetWindowFromAppID,
    AppResolver_GetIconPath,
    AppResolver_GetShortNamePath,
    AppResolver_GetAppIDForCurrentProcess,
    AppResolver_reserved_13,
    AppResolver_reserved_14,
    AppResolver_reserved_15,
};

HRESULT appresolver_create(REFIID riid, void **ppv)
{
    AppResolver *object;
    HRESULT hr;

    object = CoTaskMemAlloc(sizeof(*object));
    if (!object) return E_OUTOFMEMORY;

    object->IApplicationResolver_iface.lpVtbl = &AppResolver_vtbl;
    object->ref = 1;
    InterlockedIncrement(&appresolver_object_count);

    hr = IApplicationResolver_QueryInterface(&object->IApplicationResolver_iface, riid, ppv);
    IApplicationResolver_Release(&object->IApplicationResolver_iface);
    return hr;
}
