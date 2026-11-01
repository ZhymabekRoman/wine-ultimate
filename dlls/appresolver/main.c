/*
 * Start Menu Cache and App Resolver (appresolver.dll)
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
#include "objbase.h"
#include "olectl.h"
#include "wine/debug.h"
#include "initguid.h"

#include "appresolver.h"

WINE_DEFAULT_DEBUG_CHANNEL(appresolver);

LONG appresolver_object_count;

static LONG server_lock;

typedef struct
{
    IClassFactory IClassFactory_iface;
} ClassFactory;

static inline ClassFactory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, ClassFactory, IClassFactory_iface);
}

HRESULT appresolver_create(REFIID riid, void **ppv);

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    TRACE("(%p %s %p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv) return E_INVALIDARG;
    *ppv = NULL;

    if (IsEqualIID(&IID_IUnknown, riid) || IsEqualIID(&IID_IClassFactory, riid))
    {
        *ppv = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2; /* nonrefcounted singleton */
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 2;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface, IUnknown *outer,
                                                   REFIID riid, void **ppv)
{
    TRACE("(%p %s %p)\n", outer, debugstr_guid(riid), ppv);

    if (outer) return CLASS_E_NOAGGREGATION;
    return appresolver_create(riid, ppv);
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    TRACE("(%d)\n", lock);

    if (lock) InterlockedIncrement(&server_lock);
    else InterlockedDecrement(&server_lock);
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer,
};

static ClassFactory factory = { { &class_factory_vtbl } };

/***********************************************************************
 *           DllGetClassObject   [appresolver.@]
 */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    TRACE("(%s %s %p)\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv);

    if (!ppv) return E_INVALIDARG;
    *ppv = NULL;

    if (!IsEqualCLSID(rclsid, &CLSID_StartMenuCacheAndAppResolver))
        return CLASS_E_CLASSNOTAVAILABLE;

    return IClassFactory_QueryInterface(&factory.IClassFactory_iface, riid, ppv);
}

/***********************************************************************
 *           DllCanUnloadNow   [appresolver.@]
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
    TRACE("()\n");

    return (appresolver_object_count || server_lock) ? S_FALSE : S_OK;
}
