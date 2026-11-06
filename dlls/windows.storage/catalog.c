/* WinRT Windows.Storage.Provider.WindowsStorageCatalog implementation
 *
 * Copyright (C) 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "objidl.h"
#include "hstring.h"
#include "inspectable.h"
#include "eventtoken.h"
#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.h"
#include "windows.storage.streams.h"

#ifndef __cplusplus
typedef __FITypedEventHandler_2_IInspectable_IInspectable ITypedEventHandler;
#define ITypedEventHandler_AddRef(this)  __FITypedEventHandler_2_IInspectable_IInspectable_AddRef(this)
#define ITypedEventHandler_Release(this) __FITypedEventHandler_2_IInspectable_IInspectable_Release(this)
#endif

#include "wine/debug.h"
#include "wine/list.h"

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

/* Windows.Storage.Provider.WindowsStorageCatalog; on Windows this is the
 * cross-process "hub" directory the WebView2 runtime probes at environment
 * init: EmbeddedBrowserWebView.dll CoCreateInstances it for
 * IID_IStorageLibraryChangeListener (an IUnknown-based vtable in the 136
 * ABI: add/remove/RequestServerDeregistration), registers a listener named
 * "WebView2_Event" and treats a missing class as a hung shared browser
 * (WEBVIEW2_E_WEBVIEW_EXTENSION_HUNG 0x802A000C from CreateCoreWebView2
 * Controller). Answer it in-proc so activation falls through to the
 * standalone msedgewebview2.exe spawn plane. */

static const IID IID_IStorageLibraryChangeListener =
    {0x00000146,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

typedef struct IStorageLibraryChangeListener IStorageLibraryChangeListener;

typedef struct IStorageLibraryChangeListenerVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IStorageLibraryChangeListener *This, REFIID riid, void **obj);
    ULONG (STDMETHODCALLTYPE *AddRef)(IStorageLibraryChangeListener *This);
    ULONG (STDMETHODCALLTYPE *Release)(IStorageLibraryChangeListener *This);
    HRESULT (STDMETHODCALLTYPE *add_StorageLibraryChanged)(IStorageLibraryChangeListener *This,
            HSTRING name, ITypedEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_StorageLibraryChanged)(IStorageLibraryChangeListener *This,
            EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *RequestServerDeregistration)(IStorageLibraryChangeListener *This,
            uint32_t reserved);
} IStorageLibraryChangeListenerVtbl;

struct IStorageLibraryChangeListener
{
    const IStorageLibraryChangeListenerVtbl *lpVtbl;
};

#define IStorageLibraryChangeListener_QueryInterface(this,riid,obj)  (this)->lpVtbl->QueryInterface(this,riid,obj)
#define IStorageLibraryChangeListener_AddRef(this)                   (this)->lpVtbl->AddRef(this)
#define IStorageLibraryChangeListener_Release(this)                  (this)->lpVtbl->Release(this)

struct catalog_listener
{
    struct list entry;
    HSTRING name;
    ITypedEventHandler *handler;
    EventRegistrationToken token;
};

struct catalog
{
    IStorageLibraryChangeListener IStorageLibraryChangeListener_iface;
    LONG ref;
    struct list listeners;
    INT64 next_token;
};

static inline struct catalog *impl_from_IStorageLibraryChangeListener(IStorageLibraryChangeListener *iface)
{
    return CONTAINING_RECORD(iface, struct catalog, IStorageLibraryChangeListener_iface);
}

static HRESULT WINAPI catalog_QueryInterface(IStorageLibraryChangeListener *iface, REFIID riid, void **obj)
{
    TRACE("iface %p, riid %s, obj %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IStorageLibraryChangeListener))
    {
        IStorageLibraryChangeListener_AddRef(iface);
        *obj = iface;
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI catalog_AddRef(IStorageLibraryChangeListener *iface)
{
    struct catalog *catalog = impl_from_IStorageLibraryChangeListener(iface);
    return InterlockedIncrement(&catalog->ref);
}

static ULONG WINAPI catalog_Release(IStorageLibraryChangeListener *iface)
{
    struct catalog *catalog = impl_from_IStorageLibraryChangeListener(iface);
    ULONG ref = InterlockedDecrement(&catalog->ref);

    if (!ref)
    {
        struct catalog_listener *listener;
        struct list *cursor, *cur2;

        LIST_FOR_EACH_SAFE(cursor, cur2, &catalog->listeners)
        {
            listener = LIST_ENTRY(cursor, struct catalog_listener, entry);
            list_remove(cursor);
            WindowsDeleteString(listener->name);
            ITypedEventHandler_Release(listener->handler);
            HeapFree(GetProcessHeap(), 0, listener);
        }
        HeapFree(GetProcessHeap(), 0, catalog);
    }
    return ref;
}

static HRESULT WINAPI catalog_add_StorageLibraryChanged(IStorageLibraryChangeListener *iface,
        HSTRING name, ITypedEventHandler *handler, EventRegistrationToken *token)
{
    struct catalog *catalog = impl_from_IStorageLibraryChangeListener(iface);
    struct catalog_listener *listener;

    TRACE("iface %p, name %s, handler %p, token %p.\n", iface,
          debugstr_w(WindowsGetStringRawBuffer(name, NULL)), handler, token);

    if (!token) return E_POINTER;
    if (!(listener = HeapAlloc(GetProcessHeap(), 0, sizeof(*listener)))) return E_OUTOFMEMORY;

    listener->token.value = ++catalog->next_token;
    listener->handler = handler;
    ITypedEventHandler_AddRef(handler);
    if (FAILED(WindowsDuplicateString(name, &listener->name)))
    {
        ITypedEventHandler_Release(handler);
        HeapFree(GetProcessHeap(), 0, listener);
        return E_OUTOFMEMORY;
    }
    list_add_tail(&catalog->listeners, &listener->entry);
    *token = listener->token;
    return S_OK;
}

static HRESULT WINAPI catalog_remove_StorageLibraryChanged(IStorageLibraryChangeListener *iface,
        EventRegistrationToken token)
{
    struct catalog *catalog = impl_from_IStorageLibraryChangeListener(iface);
    struct catalog_listener *listener;

    TRACE("iface %p, token %s.\n", iface, wine_dbgstr_longlong(token.value));

    LIST_FOR_EACH_ENTRY(listener, &catalog->listeners, struct catalog_listener, entry)
    {
        if (listener->token.value == token.value)
        {
            list_remove(&listener->entry);
            WindowsDeleteString(listener->name);
            ITypedEventHandler_Release(listener->handler);
            HeapFree(GetProcessHeap(), 0, listener);
            break;
        }
    }
    return S_OK;
}

static HRESULT WINAPI catalog_RequestServerDeregistration(IStorageLibraryChangeListener *iface,
        uint32_t reserved)
{
    TRACE("iface %p, reserved %#lx.\n", iface, reserved);
    return S_OK;
}

static const IStorageLibraryChangeListenerVtbl catalog_vtbl =
{
    catalog_QueryInterface,
    catalog_AddRef,
    catalog_Release,
    catalog_add_StorageLibraryChanged,
    catalog_remove_StorageLibraryChanged,
    catalog_RequestServerDeregistration,
};

static struct catalog *create_catalog(void)
{
    struct catalog *catalog = HeapAlloc(GetProcessHeap(), 0, sizeof(*catalog));

    if (!catalog) return NULL;
    catalog->IStorageLibraryChangeListener_iface.lpVtbl = &catalog_vtbl;
    catalog->ref = 1;
    catalog->next_token = 0;
    list_init(&catalog->listeners);
    return catalog;
}

struct catalog_factory
{
    IClassFactory IClassFactory_iface;
    LONG ref;
};

static inline struct catalog_factory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct catalog_factory, IClassFactory_iface);
}

static HRESULT WINAPI catalogfactory_QueryInterface(IClassFactory *iface, REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory))
    {
        IClassFactory_AddRef(iface);
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI catalogfactory_AddRef(IClassFactory *iface)
{
    struct catalog_factory *factory = impl_from_IClassFactory(iface);
    return InterlockedIncrement(&factory->ref);
}

static ULONG WINAPI catalogfactory_Release(IClassFactory *iface)
{
    struct catalog_factory *factory = impl_from_IClassFactory(iface);
    ULONG ref = InterlockedDecrement(&factory->ref);

    if (!ref) HeapFree(GetProcessHeap(), 0, factory);
    return ref;
}

static HRESULT WINAPI catalogfactory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **obj)
{
    struct catalog *catalog;
    HRESULT hr;

    TRACE("iface %p, outer %p, riid %s, obj %p.\n", iface, outer, debugstr_guid(riid), obj);

    *obj = NULL;
    if (!(catalog = create_catalog())) return E_OUTOFMEMORY;

    hr = IStorageLibraryChangeListener_QueryInterface(&catalog->IStorageLibraryChangeListener_iface, riid, obj);
    IStorageLibraryChangeListener_Release(&catalog->IStorageLibraryChangeListener_iface);
    return hr;
}

static HRESULT WINAPI catalogfactory_LockServer(IClassFactory *iface, BOOL lock)
{
    TRACE("iface %p, lock %d.\n", iface, lock);
    return S_OK;
}

static const IClassFactoryVtbl catalogfactory_vtbl =
{
    catalogfactory_QueryInterface,
    catalogfactory_AddRef,
    catalogfactory_Release,
    catalogfactory_CreateInstance,
    catalogfactory_LockServer,
};

static struct catalog_factory catalog_factory = {{&catalogfactory_vtbl}, 1};

BOOL is_windows_storage_catalog_clsid(REFCLSID clsid)
{
    static const IID clsid_windows_storage_catalog =
        {0x00000323,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
    return IsEqualGUID(clsid, &clsid_windows_storage_catalog);
}

HRESULT windows_storage_catalog_get_class_object(REFCLSID clsid, REFIID riid, void **out)
{
    return catalogfactory_QueryInterface(&catalog_factory.IClassFactory_iface, riid, out);
}
