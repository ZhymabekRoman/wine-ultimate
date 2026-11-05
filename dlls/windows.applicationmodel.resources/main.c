/*
 * Copyright (C) 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/*
 * MRT Core resource provider (Microsoft.Windows.ApplicationModel.Resources).
 *
 * The native MRT Core layer (MRM.dll) boots its ModernResourceProvider by
 * CoCreateInstance()-ing a class id from the {fe28e430-7fff-*} family; the
 * class tail is generated at runtime and varies between runs, so it cannot be
 * pinned in the registry.  combase routes the whole family here (see
 * CoCreateInstanceEx).  The class object creates a provider that answers
 * IServiceProvider::QueryService(SID_MRT_CoreIndependent) with the
 * IRuntimeProvider surface, whose GetActivationFactory/CreateInstance are
 * delegated to the Windows Runtime activation of this prefix - the builtin
 * windows.applicationmodel.resources.core.dll serves the Resources.Core and
 * (through the Windows App SDK seeds) the Microsoft...Resources class faces.
 */

#include <stdarg.h>
#include <string.h>
#include "initguid.h"

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"
#include "hstring.h"
#include "wine/debug.h"
#include "activation.h"
#include "inspectable.h"
#include "roapi.h"
#include "unknwn.h"

WINE_DEFAULT_DEBUG_CHANNEL(mwr);


/* {6d5140c1-110b-11ce-9c8b-00006a4bb51e} */
/* {331b2358-bebb-5116-805b-e07956e24806} - Microsoft.Windows.ApplicationModel.Resources.IRuntimeProvider */
DEFINE_GUID(IID_IRuntimeProvider, 0x331b2358, 0xbebb, 0x5116, 0x80, 0x5b, 0xe0, 0x79, 0x56, 0xe2, 0x48, 0x06);
/* {2a224ab7-1a2a-4bcc-ba00-3f8f68ea3409} - SID_MRT_CoreIndependent (combase on Windows) */
DEFINE_GUID(SID_MRT_CoreIndependent, 0x2a224ab7, 0x1a2a, 0x4bcc, 0xba, 0x00, 0x3f, 0x8f, 0x68, 0xea, 0x34, 0x09);

static LONG object_count;

struct provider
{
    const void **vtbl;
    LONG ref;
};

static const void *provider_vtbl[];

static inline struct provider *provider_from(void *iface)
{
    return (struct provider *)iface;
}

static const GUID provider_service_provider_sid = { 0x6d5140c1, 0x110b, 0x11ce, { 0x9c, 0x8b, 0x00, 0x00, 0x6a, 0x4b, 0xb5, 0x1e } };

static HRESULT WINAPI provider_QueryInterface(void *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &provider_service_provider_sid) || IsEqualGUID(iid, &IID_IRuntimeProvider))
    {
        *out = iface;
        InterlockedIncrement(&provider_from(iface)->ref);
        return S_OK;
    }
    WARN("provider QI miss, iid %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI provider_AddRef(void *iface)
{
    return InterlockedIncrement(&provider_from(iface)->ref);
}

static ULONG WINAPI provider_Release(void *iface)
{
    ULONG ref = InterlockedDecrement(&provider_from(iface)->ref);

    if (!ref)
    {
        InterlockedDecrement(&object_count);
        free(iface);
    }
    return ref;
}

static HRESULT WINAPI provider_GetIids(void *iface, ULONG *count, GUID **iids)
{
    static GUID iids_arr[] =
    {
        {0x6d5140c1, 0x110b, 0x11ce, {0x9c, 0x8b, 0x00, 0x00, 0x6a, 0x4b, 0xb5, 0x1e}},
        {0x331b2358, 0xbebb, 0x5116, {0x80, 0x5b, 0xe0, 0x79, 0x56, 0xe2, 0x48, 0x06}},
    };

    *count = ARRAY_SIZE(iids_arr);
    if (!(*iids = malloc(sizeof(iids_arr)))) return E_OUTOFMEMORY;
    memcpy(*iids, iids_arr, sizeof(iids_arr));
    return S_OK;
}

static HRESULT WINAPI provider_GetRuntimeClassName(void *iface, HSTRING *name)
{
    static const WCHAR cls[] = L"Microsoft.Windows.ApplicationModel.Resources.RuntimeProvider";
    return WindowsCreateString(cls, ARRAYSIZE(cls) - 1, name);
}

static HRESULT WINAPI provider_GetTrustLevel(void *iface, INT32 *level)
{
    *level = FullTrust;
    return S_OK;
}

/* IServiceProvider::QueryService - the MRT Core handoff: MRM asks for
 * SID_MRT_CoreIndependent and receives the IRuntimeProvider surface. */
static HRESULT WINAPI provider_QueryService(void *iface, REFGUID sid, REFIID iid, void **service)
{
    TRACE("sid %s, iid %s.\n", debugstr_guid(sid), debugstr_guid(iid));

    *service = NULL;
    if (IsEqualGUID(sid, &SID_MRT_CoreIndependent))
        return provider_QueryInterface(iface, iid, service);

    return E_NOINTERFACE;
}

/* MRM's liveness probe class name test (see provider_CreateInstance). */
static BOOL is_probe_class(HSTRING classid)
{
    static const WCHAR probeW[] = L"FakeClass.ClassName";
    UINT32 len = 0;
    LPCWSTR buf = WindowsGetStringRawBuffer(classid, &len);

    if (buf && len == ARRAYSIZE(probeW) - 1 && !wcscmp(buf, probeW)) return TRUE;
    WARN("probe miss, class %s.\n", debugstr_hstring(classid));
    return FALSE;
}

/* The answer for the probe class: a merged IInspectable + IActivationFactory
 * object, so it serves MRM's direct face probes (factory/instance), and the
 * combase RoActivateInstance route (factory -> ActivateInstance).  MRT Core
 * answers the probe the same way, with an empty activation. */
struct probe_stub
{
    const void **vtbl;
    LONG ref;
};

static HRESULT WINAPI probe_QueryInterface(void *iface, REFIID iid, void **out);
static ULONG WINAPI probe_AddRef(void *iface);
static ULONG WINAPI probe_Release(void *iface);
static HRESULT WINAPI probe_GetIids(void *iface, ULONG *count, GUID **iids);
static HRESULT WINAPI probe_GetRuntimeClassName(void *iface, HSTRING *name);
static HRESULT WINAPI probe_GetTrustLevel(void *iface, INT32 *level);
static HRESULT WINAPI probe_ActivateInstance(void *iface, void **instance);

static const void *probe_vtbl[] =
{
    probe_QueryInterface, probe_AddRef, probe_Release,
    probe_GetIids, probe_GetRuntimeClassName, probe_GetTrustLevel,
    probe_ActivateInstance,
};

static HRESULT WINAPI probe_QueryInterface(void *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
    {
        *out = iface;
        InterlockedIncrement(&((struct probe_stub *)iface)->ref);
        return S_OK;
    }
    WARN("stub QI miss, iid %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI probe_AddRef(void *iface)
{
    return InterlockedIncrement(&((struct probe_stub *)iface)->ref);
}

static ULONG WINAPI probe_Release(void *iface)
{
    ULONG ref = InterlockedDecrement(&((struct probe_stub *)iface)->ref);

    if (!ref)
    {
        InterlockedDecrement(&object_count);
        free(iface);
    }
    return ref;
}

static HRESULT WINAPI probe_GetIids(void *iface, ULONG *count, GUID **iids)
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI probe_GetRuntimeClassName(void *iface, HSTRING *name)
{
    static const WCHAR cls[] = L"FakeClass.ClassName";
    return WindowsCreateString(cls, ARRAYSIZE(cls) - 1, name);
}

static HRESULT WINAPI probe_GetTrustLevel(void *iface, INT32 *level)
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI probe_ActivateInstance(void *iface, void **instance)
{
    struct probe_stub *stub;

    if (!(stub = malloc(sizeof(*stub)))) return E_OUTOFMEMORY;
    stub->vtbl = probe_vtbl;
    stub->ref = 1;
    InterlockedIncrement(&object_count);
    *instance = (void *)stub;
    return S_OK;
}

static HRESULT probe_stub_create(void **out)
{
    struct probe_stub *stub;

    if (!(stub = malloc(sizeof(*stub)))) return E_OUTOFMEMORY;
    stub->vtbl = probe_vtbl;
    stub->ref = 1;
    InterlockedIncrement(&object_count);
    *out = (void *)stub;
    return S_OK;
}

/* IRuntimeProvider::GetActivationFactory - delegate to the Windows Runtime
 * activation route of this prefix (combase: ActivatableClassId seeds and the
 * builtin namespace routes), so the classes land on the builtin
 * windows.applicationmodel.resources.core faces. */
static HRESULT WINAPI provider_GetActivationFactory(void *iface, HSTRING classid, void **factory)
{
    TRACE("class %s, factory %p.\n", debugstr_hstring(classid), factory);

    if (!classid || !factory) return E_POINTER;
    *factory = NULL;

    /* MRM probes both faces of the runtime provider with the dummy class. */
    if (is_probe_class(classid)) return probe_stub_create(factory);

    return RoGetActivationFactory(classid, &IID_IInspectable, factory);
}

/* IRuntimeProvider::CreateInstance - same, activating the class.  MRM's
 * ModernResourceProvider::Create probes the provider by activating
 * L"FakeClass.ClassName" and inspects only the return code (the name is a
 * deliberate non-class); MRT Core answers with a bare stub instance, so the
 * probe must not go through the WinRT activation table. */
static HRESULT WINAPI provider_CreateInstance(void *iface, HSTRING classid, void **instance)
{
    TRACE("class %s, instance %p.\n", debugstr_hstring(classid), instance);

    if (!classid || !instance) return E_POINTER;
    *instance = NULL;

    if (is_probe_class(classid)) return probe_stub_create(instance);

    return RoActivateInstance(classid, (IInspectable **)instance);
}

/* IRuntimeProvider::ResourceMap[propget] - the app-wide PRI resource map; the
 * MWR ResourceMap class is served by the builtin resources.core provider. */
static HRESULT WINAPI provider_get_ResourceMap(void *iface, IInspectable **map)
{
    static const WCHAR cls[] = L"Microsoft.Windows.ApplicationModel.Resources.ResourceMap";
    HSTRING hstr;
    HRESULT hr;

    TRACE("iface %p, map %p.\n", iface, map);

    if ((hr = WindowsCreateString(cls, ARRAYSIZE(cls) - 1, &hstr))) return hr;
    hr = RoActivateInstance(hstr, map);
    WindowsDeleteString(hstr);
    return hr;
}

static const void *provider_vtbl[] =
{
    provider_QueryInterface, provider_AddRef, provider_Release,
    provider_GetIids, provider_GetRuntimeClassName, provider_GetTrustLevel,
    provider_QueryService,
    provider_GetActivationFactory, provider_CreateInstance, provider_get_ResourceMap,
};

/* ------------------------------------------------------------------ */
/* class factory                                                       */
/* ------------------------------------------------------------------ */

struct provider_factory
{
    const void **vtbl;
    LONG ref;
};

static inline struct provider_factory *factory_from(void *iface)
{
    return (struct provider_factory *)iface;
}

static HRESULT WINAPI factory_QueryInterface(void *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory))
    {
        *out = iface;
        InterlockedIncrement(&factory_from(iface)->ref);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(void *iface)
{
    return InterlockedIncrement(&factory_from(iface)->ref);
}

static ULONG WINAPI factory_Release(void *iface)
{
    return InterlockedDecrement(&factory_from(iface)->ref);
}

static HRESULT WINAPI factory_CreateInstance(void *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct provider *provider;

    TRACE("outer %p, iid %s, out %p.\n", outer, debugstr_guid(iid), out);

    if (outer && !IsEqualGUID(iid, &IID_IUnknown)) return CLASS_E_NOAGGREGATION;

    if (!(provider = malloc(sizeof(*provider)))) return E_OUTOFMEMORY;
    provider->vtbl = provider_vtbl;
    provider->ref = 1;
    InterlockedIncrement(&object_count);

    if (outer)
    {
        *(void **)provider = (void *)provider_vtbl;
        *out = (void *)provider;
        InterlockedIncrement(&provider->ref);
        return IUnknown_QueryInterface(outer, iid, out);
    }

    *out = (void *)provider;
    return provider_QueryInterface(provider, iid, out);
}

static HRESULT WINAPI factory_LockServer(void *iface, BOOL lock)
{
    return S_OK;
}

static const void *factory_vtbl[] =
{
    factory_QueryInterface, factory_AddRef, factory_Release,
    factory_CreateInstance, factory_LockServer,
};

static struct provider_factory provider_factory_inst = { factory_vtbl, 1 };

/* ------------------------------------------------------------------ */
/* exports                                                             */
/* ------------------------------------------------------------------ */

HRESULT WINAPI DllCanUnloadNow(void)
{
    return object_count ? S_FALSE : S_OK;
}

/* One provider class object answers the whole {fe28e430-7fff-*} family; the
 * tail of the id is runtime-generated, so it must not gate the answer. */
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    TRACE("clsid %s, riid %s, out %p.\n", debugstr_guid(clsid), debugstr_guid(riid), out);

    return factory_QueryInterface(&provider_factory_inst, riid, out);
}

/* Accept the MWR-namespaced activatable classes directly, so a packaged
 * prefix naming this module in ActivatableClassId\DllPath seeds activates
 * them without going through the COM provider. */
HRESULT WINAPI DllGetActivationFactory(HSTRING classid, void **factory)
{
    if (!factory) return E_POINTER;
    *factory = NULL;

    /* combase's namespace route sends the MRM probe class here (roapi.c);
     * answer directly, before the activation table (no self-recursion). */
    if (is_probe_class(classid)) return probe_stub_create(factory);

    return RoGetActivationFactory(classid, &IID_IInspectable, factory);
}
