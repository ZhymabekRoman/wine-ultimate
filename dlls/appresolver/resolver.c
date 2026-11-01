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
    if (hwnd)
    {
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

/* slot 5; the native onecore caller passes only (hwnd, &appid), so the extra
 * out-params are never written: a 2-arg caller leaves garbage registers in
 * r8/r9 and touching them would corrupt memory.  A 5-arg caller (the .NET
 * Toolkit projection) receives its BOOL out-params from the marshaller's
 * zero-initialised locals, i.e. FALSE, which is the correct answer. */
static HRESULT WINAPI AppResolver_GetAppIDForWindow(IApplicationResolver *iface, HWND hwnd,
        PWSTR *appid, BOOL *relaunch_required, DWORD *host_type, DWORD *reliability)
{
    TRACE("(%p %p %p %p %p %p)\n", iface, hwnd, appid, relaunch_required, host_type, reliability);

    if (!appid) return E_INVALIDARG;
    *appid = NULL;

    /* documented: a NULL window queries the current application's app id */
    return get_window_appid(hwnd, appid);
}

/* slot 6, full 5-arg shape (Toolkit: pid + 4 out-params) */
static HRESULT WINAPI AppResolver_GetAppIDForProcess(IApplicationResolver *iface,
        DWORD pid, PWSTR *appid, BOOL *pinning_prevented, BOOL *explicit_appid,
        BOOL *embedded_shortcut_valid)
{
    TRACE("(%p %lx %p %p %p %p)\n", iface, pid, appid, pinning_prevented,
          explicit_appid, embedded_shortcut_valid);

    if (!appid) return E_INVALIDARG;
    *appid = NULL;

    if (pinning_prevented) *pinning_prevented = FALSE;
    if (explicit_appid) *explicit_appid = FALSE;
    if (embedded_shortcut_valid) *embedded_shortcut_valid = FALSE;

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

/* unimplemented slots (never fabricate success on these) */
#define NOTIMPL(name) static HRESULT WINAPI name(void) { FIXME("stub\n"); return E_NOTIMPL; }
NOTIMPL(AppResolver_GetAppIDForShortcut)
NOTIMPL(AppResolver_GetAppIDForShortcutObject)
NOTIMPL(AppResolver_GetShortcutForProcess)
NOTIMPL(AppResolver_GetBestShortcutForAppID)
NOTIMPL(AppResolver_GetBestShortcutAndAppIDForAppPath)
NOTIMPL(AppResolver_CanPinApp)
NOTIMPL(AppResolver_CanPinAppShortcut)
NOTIMPL(AppResolver_GetRelaunchProperties)
NOTIMPL(AppResolver_GenerateShortcutFromWindowProperties)
NOTIMPL(AppResolver_GenerateShortcutFromItemProperties)
NOTIMPL(AppResolver_reserved_15)

static const IApplicationResolverVtbl AppResolver_vtbl =
{
    AppResolver_QueryInterface,
    AppResolver_AddRef,
    AppResolver_Release,
    AppResolver_GetAppIDForShortcut,             /*  3 */
    AppResolver_GetAppIDForShortcutObject,       /*  4 */
    AppResolver_GetAppIDForWindow,               /*  5 */
    AppResolver_GetAppIDForProcess,              /*  6 */
    AppResolver_GetShortcutForProcess,           /*  7 */
    AppResolver_GetBestShortcutForAppID,         /*  8 */
    AppResolver_GetBestShortcutAndAppIDForAppPath, /* 9 */
    AppResolver_CanPinApp,                       /* 10 */
    AppResolver_CanPinAppShortcut,               /* 11 */
    AppResolver_GetRelaunchProperties,           /* 12 */
    AppResolver_GenerateShortcutFromWindowProperties, /* 13 */
    AppResolver_GenerateShortcutFromItemProperties,   /* 14 */
    AppResolver_reserved_15,                     /* 15 */
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
