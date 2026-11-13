/* WinRT Windows.ApplicationModel Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#include "initguid.h"
#include <string.h>

#include "private.h"
#include "winreg.h"
#include "objbase.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(model);

/* StartupTask implementation */
static const void *startup_task_vtbl[];
static const void *startup_task_factory_vtbl[];
static const void *startup_task_async_vtbl[];

static const void *startup_task_obj = startup_task_vtbl;
static const void *startup_task_factory = startup_task_factory_vtbl;

struct startup_task_async
{
    const void *vtbl;
    LONG ref;
};

static HRESULT WINAPI startup_task_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI startup_task_async_AddRef( void *iface ) { return 2; }
static ULONG WINAPI startup_task_async_Release( void *iface ) { return 1; }
static HRESULT WINAPI startup_task_async_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI startup_task_async_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation", 32, n );
}
static HRESULT WINAPI startup_task_async_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI startup_task_async_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI startup_task_async_get_Completed( void *iface, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}
static HRESULT WINAPI startup_task_async_GetResults( void *iface, void **results )
{
    if (results) *results = (void *)&startup_task_obj;
    return S_OK;
}
static HRESULT WINAPI startup_task_async_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI startup_task_async_get_Status( void *iface, UINT32 *status ) { if (status) *status = 1 /* Completed */; return S_OK; }
static HRESULT WINAPI startup_task_async_get_ErrorCode( void *iface, HRESULT *code ) { if (code) *code = S_OK; return S_OK; }
static HRESULT WINAPI startup_task_async_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI startup_task_async_Close( void *iface ) { return S_OK; }

static const void *startup_task_async_vtbl[] =
{
    startup_task_async_QueryInterface,
    startup_task_async_AddRef,
    startup_task_async_Release,
    startup_task_async_GetIids,
    startup_task_async_GetRuntimeClassName,
    startup_task_async_GetTrustLevel,
    startup_task_async_put_Completed,
    startup_task_async_get_Completed,
    startup_task_async_GetResults,
    startup_task_async_get_Id,
    startup_task_async_get_Status,
    startup_task_async_get_ErrorCode,
    startup_task_async_Cancel,
    startup_task_async_Close,
};

static struct startup_task_async startup_task_async_inst = { startup_task_async_vtbl, 1 };

static HRESULT WINAPI startup_task_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI startup_task_AddRef( void *iface ) { return 2; }
static ULONG WINAPI startup_task_Release( void *iface ) { return 1; }
static HRESULT WINAPI startup_task_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI startup_task_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.StartupTask", 35, n );
}
static HRESULT WINAPI startup_task_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI startup_task_RequestEnableAsync( void *iface, void **op )
{
    if (op) *op = &startup_task_async_inst;
    return S_OK;
}
static HRESULT WINAPI startup_task_Disable( void *iface ) { return S_OK; }
static HRESULT WINAPI startup_task_get_State( void *iface, DWORD *state )
{
    if (state) *state = 2 /* StartupTaskState_Enabled */;
    return S_OK;
}
static HRESULT WINAPI startup_task_get_TaskId( void *iface, HSTRING *id )
{
    if (id) return WindowsCreateString( L"WhatsApp", 8, id );
    return S_OK;
}

static const void *startup_task_vtbl[] =
{
    startup_task_QueryInterface,
    startup_task_AddRef,
    startup_task_Release,
    startup_task_GetIids,
    startup_task_GetRuntimeClassName,
    startup_task_GetTrustLevel,
    startup_task_RequestEnableAsync,
    startup_task_Disable,
    startup_task_get_State,
    startup_task_get_TaskId,
};

static HRESULT WINAPI startup_task_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    return S_OK;
}
static ULONG WINAPI startup_task_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI startup_task_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI startup_task_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI startup_task_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.StartupTask", 35, n );
}
static HRESULT WINAPI startup_task_factory_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI startup_task_factory_GetForCurrentPackageAsync( void *iface, void **op )
{
    if (op) *op = &startup_task_async_inst;
    return S_OK;
}
static HRESULT WINAPI startup_task_factory_GetAsync( void *iface, HSTRING taskId, void **op )
{
    TRACE( "iface %p, taskId %s, op %p\n", iface, debugstr_hstring(taskId), op );
    if (op) *op = &startup_task_async_inst;
    return S_OK;
}

static const void *startup_task_factory_vtbl[] =
{
    startup_task_factory_QueryInterface,
    startup_task_factory_AddRef,
    startup_task_factory_Release,
    startup_task_factory_GetIids,
    startup_task_factory_GetRuntimeClassName,
    startup_task_factory_GetTrustLevel,
    startup_task_factory_GetForCurrentPackageAsync,
    startup_task_factory_GetAsync,
};

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* WindowsAppSDK static-class surface ------------------------------------------
 * Microsoft.Windows.ApplicationModel.WindowsAppRuntime.* (DeploymentManager,
 * DeploymentInitializeOptions, DeploymentResult) and Microsoft.Windows.ApplicationModel.
 * DynamicDependency.* (PackageDependency, its context and options classes) are the
 * Windows App SDK bootstrap and framework-package classes; Windows hosts them in
 * Microsoft.WindowsAppRuntime.dll, which has no builtin.  combase routes both
 * namespaces here (roapi.c) and the packaged prefix's ActivatableClassId seeds carry
 * this module as their DllPath, so the apprepository answer and the code route agree.
 * Each class is a static: its factory hands out one QI-everything singleton that
 * reports its own class name; every method slot past IInspectable answers S_OK with a
 * zeroed out-param, the same tolerated shape as the StartupTask object above (the
 * .NET projection reads the defaults and continues; E_NOTIMPL slots are what turn
 * into RaiseFailFastException pairs). */
struct winappsdk_class
{
    const void *vtbl;
    const WCHAR *name;
    LONG ref;
};

static HRESULT WINAPI winappsdk_class_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}

static HRESULT WINAPI winappsdk_QueryInterface( void *iface, REFIID iid, void **out )
{
    return winappsdk_class_QueryInterface( iface, iid, out );
}

static ULONG WINAPI winappsdk_AddRef( void *iface ) { return 2; }
static ULONG WINAPI winappsdk_Release( void *iface ) { return 1; }
static HRESULT WINAPI winappsdk_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI winappsdk_GetRuntimeClassName( void *iface, HSTRING *n )
{
    struct winappsdk_class *cls = iface;
    return WindowsCreateString( cls->name, lstrlenW( cls->name ), n );
}
static HRESULT WINAPI winappsdk_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI winappsdk_zero_out( void *iface, void *out )
{
    if (out) *(void **)out = NULL;
    return S_OK;
}

/* WinRT generic collection implementation conforming to ABI:
 * IVectorView<T>, IMapView<K, V>, IIterable<T>, IIterator<T>, IKeyValuePair<K, V> */

static const GUID IID_IReferenceTracker =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
static const GUID IID_IReferenceTrackerManager =
    { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
static const GUID IID_IReferenceTrackerTarget =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };

/* Generic IIDs */
static const GUID IID_IVectorView_Object =
    { 0xa6487363, 0xb074, 0x5c60, { 0xab, 0x16, 0x86, 0x6d, 0xce, 0x4e, 0xe5, 0x4d } };
static const GUID IID_IIterable_Object =
    { 0x092b849b, 0x60b1, 0x52be, { 0xa4, 0x4a, 0x6f, 0xe8, 0xe9, 0x33, 0xcb, 0xe4 } };
static const GUID IID_IIterator_Object =
    { 0x44a94f2d, 0x04f8, 0x5091, { 0xb3, 0x36, 0xbe, 0x78, 0x92, 0xdd, 0x10, 0xbe } };
static const GUID IID_IMapView_Object =
    { 0x236aac9d, 0xfb12, 0x5c4d, { 0xa4, 0x1c, 0x9e, 0x44, 0x5f, 0xb4, 0xd7, 0xec } };
static const GUID IID_IKeyValuePair_Object =
    { 0x09335560, 0x6c6b, 0x5a26, { 0x93, 0x48, 0x97, 0xb7, 0x81, 0x13, 0x2b, 0x20 } };
static const GUID IID_IIterable_KeyValuePair_Object =
    { 0xcf2c8067, 0xa319, 0x570b, { 0xa2, 0x40, 0xd9, 0x30, 0x22, 0x05, 0xec, 0x92 } };
static const GUID IID_IIterator_KeyValuePair_Object =
    { 0xf3c2dc13, 0x6484, 0x5da6, { 0x9e, 0x3c, 0xdc, 0x03, 0x37, 0x4b, 0x0b, 0x36 } };

/* Instantiated IIDs for Store types */
static const GUID IID_IVectorView_StoreImage =
    { 0x7e1ceace, 0x82bd, 0x5db3, { 0x8f, 0x35, 0x9b, 0xf0, 0xc8, 0x8e, 0xf8, 0x39 } };
static const GUID IID_IIterable_StoreImage =
    { 0xb2da6de8, 0xad55, 0x56ce, { 0x87, 0x54, 0x2c, 0x96, 0xf4, 0xfe, 0x1c, 0x2e } };
static const GUID IID_IIterator_StoreImage =
    { 0xfbe076f6, 0xc3d2, 0x5df7, { 0x83, 0x9f, 0x01, 0x2a, 0xc0, 0xf9, 0x51, 0xc5 } };

static const GUID IID_IMapView_StoreProduct =
    { 0x3074efe6, 0x0cd8, 0x5fb7, { 0xae, 0xe3, 0x60, 0xbd, 0xa3, 0xc2, 0xf6, 0x7e } };
static const GUID IID_IIterable_StoreProductKeyValuePair =
    { 0xb9663fb7, 0x1c5f, 0x59a4, { 0x90, 0xc7, 0x36, 0x34, 0xfd, 0xd3, 0xf2, 0xf4 } };
static const GUID IID_IKeyValuePair_StoreProduct =
    { 0x0e89a311, 0x437a, 0x5957, { 0x95, 0x93, 0x8e, 0xd6, 0x45, 0x11, 0x54, 0x5b } };
static const GUID IID_IIterator_StoreProductKeyValuePair =
    { 0xaf049501, 0x6d88, 0x5887, { 0x81, 0xa1, 0xed, 0xd4, 0xad, 0xc4, 0xc0, 0x63 } };

struct winrt_vector_view
{
    const void *vtbl_vector;
    const void *vtbl_iterable;
    LONG ref;
    GUID vector_iid;
    GUID iterable_iid;
    GUID iterator_iid;
    UINT32 count;
    IInspectable **items;
};

struct winrt_vector_iterator
{
    const void *vtbl_iterator;
    LONG ref;
    struct winrt_vector_view *vector;
    UINT32 index;
};

struct winrt_map_view
{
    const void *vtbl_map;
    const void *vtbl_iterable;
    LONG ref;
    GUID map_iid;
    GUID iterable_iid;
    GUID kv_iid;
    GUID iterator_iid;
    UINT32 count;
    HSTRING *keys;
    IInspectable **values;
};

struct winrt_map_iterator
{
    const void *vtbl_iterator;
    LONG ref;
    struct winrt_map_view *map;
    UINT32 index;
};

struct winrt_key_value_pair
{
    const void *vtbl_kv;
    LONG ref;
    HSTRING key;
    IInspectable *value;
    GUID kv_iid;
};

static inline struct winrt_key_value_pair *impl_from_kv( void *iface )
{
    return (struct winrt_key_value_pair *)iface;
}
static HRESULT WINAPI kv_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->kv_iid ) ||
        IsEqualGUID( iid, &IID_IKeyValuePair_Object ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI kv_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_kv( iface )->ref );
}
static ULONG WINAPI kv_Release( void *iface )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->key );
        if (impl->value) IInspectable_Release( impl->value );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI kv_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI kv_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IKeyValuePair`2";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI kv_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI kv_get_Key( void *iface, HSTRING *key )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    if (!key) return E_POINTER;
    return WindowsDuplicateString( impl->key, key );
}
static HRESULT WINAPI kv_get_Value( void *iface, IInspectable **value )
{
    struct winrt_key_value_pair *impl = impl_from_kv( iface );
    if (!value) return E_POINTER;
    *value = impl->value;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}
static const void *winrt_kv_vtbl[] =
{
    kv_QueryInterface,
    kv_AddRef,
    kv_Release,
    kv_GetIids,
    kv_GetRuntimeClassName,
    kv_GetTrustLevel,
    kv_get_Key,
    kv_get_Value,
};

static IInspectable *create_key_value_pair( HSTRING key, IInspectable *value, const GUID *kv_iid )
{
    struct winrt_key_value_pair *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl_kv = winrt_kv_vtbl;
    impl->ref = 1;
    WindowsDuplicateString( key, &impl->key );
    impl->value = value;
    if (impl->value) IInspectable_AddRef( impl->value );
    if (kv_iid) impl->kv_iid = *kv_iid;
    return (IInspectable *)&impl->vtbl_kv;
}

static inline struct winrt_vector_view *impl_from_vector( void *iface )
{
    return (struct winrt_vector_view *)iface;
}
static inline struct winrt_vector_view *impl_from_vector_iterable( void *iface )
{
    return (struct winrt_vector_view *)((char *)iface - sizeof(void *));
}
static inline struct winrt_vector_iterator *impl_from_vector_iter( void *iface )
{
    return (struct winrt_vector_iterator *)iface;
}

static HRESULT WINAPI vector_view_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->vector_iid ) ||
        IsEqualGUID( iid, &IID_IVectorView_Object ))
    {
        *out = &impl->vtbl_vector;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &impl->iterable_iid ) ||
        IsEqualGUID( iid, &IID_IIterable_Object ))
    {
        *out = &impl->vtbl_iterable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vector_view_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_vector( iface )->ref );
}
static ULONG WINAPI vector_view_Release( void *iface )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        for (UINT32 i = 0; i < impl->count; i++)
        {
            if (impl->items[i]) IInspectable_Release( impl->items[i] );
        }
        free( impl->items );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI vector_view_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI vector_view_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IVectorView`1";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI vector_view_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI vector_view_GetAt( void *iface, UINT32 index, IInspectable **item )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!item) return E_POINTER;
    if (index >= impl->count) { *item = NULL; return E_BOUNDS; }
    *item = impl->items[index];
    if (*item) IInspectable_AddRef( *item );
    return S_OK;
}
static HRESULT WINAPI vector_view_get_Size( void *iface, UINT32 *size )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!size) return E_POINTER;
    *size = impl->count;
    return S_OK;
}
static HRESULT WINAPI vector_view_IndexOf( void *iface, IInspectable *item, UINT32 *index, boolean *found )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    if (!index || !found) return E_POINTER;
    for (UINT32 i = 0; i < impl->count; i++)
    {
        if (impl->items[i] == item)
        {
            *index = i;
            *found = TRUE;
            return S_OK;
        }
    }
    *index = 0;
    *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI vector_view_GetMany( void *iface, UINT32 start_index, UINT32 count, IInspectable **items, UINT32 *actual )
{
    struct winrt_vector_view *impl = impl_from_vector( iface );
    UINT32 i, num;
    if (!actual) return E_POINTER;
    if (start_index >= impl->count) { *actual = 0; return S_OK; }
    num = min( count, impl->count - start_index );
    for (i = 0; i < num; i++)
    {
        items[i] = impl->items[start_index + i];
        if (items[i]) IInspectable_AddRef( items[i] );
    }
    *actual = num;
    return S_OK;
}

static HRESULT WINAPI vector_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->vector->iterator_iid ) ||
        IsEqualGUID( iid, &IID_IIterator_Object ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vector_iter_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_vector_iter( iface )->ref );
}
static ULONG WINAPI vector_iter_Release( void *iface )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        vector_view_Release( &impl->vector->vtbl_vector );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI vector_iter_get_Current( void *iface, IInspectable **item )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!item) return E_POINTER;
    if (impl->index >= impl->vector->count) { *item = NULL; return E_BOUNDS; }
    *item = impl->vector->items[impl->index];
    if (*item) IInspectable_AddRef( *item );
    return S_OK;
}
static HRESULT WINAPI vector_iter_get_HasCurrent( void *iface, boolean *has )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!has) return E_POINTER;
    *has = (impl->index < impl->vector->count);
    return S_OK;
}
static HRESULT WINAPI vector_iter_MoveNext( void *iface, boolean *has )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    if (!has) return E_POINTER;
    if (impl->index < impl->vector->count) impl->index++;
    *has = (impl->index < impl->vector->count);
    return S_OK;
}
static HRESULT WINAPI vector_iter_GetMany( void *iface, UINT32 count, IInspectable **items, UINT32 *actual )
{
    struct winrt_vector_iterator *impl = impl_from_vector_iter( iface );
    HRESULT hr = vector_view_GetMany( &impl->vector->vtbl_vector, impl->index, count, items, actual );
    if (SUCCEEDED(hr)) impl->index += *actual;
    return hr;
}
static const void *winrt_vector_iter_vtbl[] =
{
    vector_iter_QueryInterface,
    vector_iter_AddRef,
    vector_iter_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    vector_iter_get_Current,
    vector_iter_get_HasCurrent,
    vector_iter_MoveNext,
    vector_iter_GetMany,
};

static HRESULT WINAPI vector_iterable_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_vector_view *impl = impl_from_vector_iterable( iface );
    return vector_view_QueryInterface( &impl->vtbl_vector, iid, out );
}
static ULONG WINAPI vector_iterable_AddRef( void *iface )
{
    return vector_view_AddRef( &impl_from_vector_iterable( iface )->vtbl_vector );
}
static ULONG WINAPI vector_iterable_Release( void *iface )
{
    return vector_view_Release( &impl_from_vector_iterable( iface )->vtbl_vector );
}
static HRESULT WINAPI vector_iterable_First( void *iface, void **iter )
{
    struct winrt_vector_view *impl = impl_from_vector_iterable( iface );
    struct winrt_vector_iterator *it;
    if (!iter) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->vtbl_iterator = winrt_vector_iter_vtbl;
    it->ref = 1;
    it->vector = impl;
    vector_view_AddRef( &impl->vtbl_vector );
    it->index = 0;
    *iter = &it->vtbl_iterator;
    return S_OK;
}
static const void *winrt_vector_iterable_vtbl[] =
{
    vector_iterable_QueryInterface,
    vector_iterable_AddRef,
    vector_iterable_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    vector_iterable_First,
};

static const void *winrt_vector_view_vtbl[] =
{
    vector_view_QueryInterface,
    vector_view_AddRef,
    vector_view_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    vector_view_GetAt,
    vector_view_get_Size,
    vector_view_IndexOf,
    vector_view_GetMany,
};

static struct winrt_vector_view *store_create_vector_view( IInspectable **items, UINT32 count,
                                                          const GUID *vec_iid, const GUID *iter_iid )
{
    struct winrt_vector_view *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl_vector = winrt_vector_view_vtbl;
    impl->vtbl_iterable = winrt_vector_iterable_vtbl;
    impl->ref = 1;
    if (vec_iid) impl->vector_iid = *vec_iid;
    if (iter_iid) impl->iterable_iid = *iter_iid;
    impl->count = count;
    if (count)
    {
        impl->items = calloc( count, sizeof(IInspectable *) );
        for (UINT32 i = 0; i < count; i++)
        {
            impl->items[i] = items[i];
            if (impl->items[i]) IInspectable_AddRef( impl->items[i] );
        }
    }
    return impl;
}

static inline struct winrt_map_view *impl_from_map( void *iface )
{
    return (struct winrt_map_view *)iface;
}
static inline struct winrt_map_view *impl_from_map_iterable( void *iface )
{
    return (struct winrt_map_view *)((char *)iface - sizeof(void *));
}
static inline struct winrt_map_iterator *impl_from_map_iter( void *iface )
{
    return (struct winrt_map_iterator *)iface;
}

static HRESULT WINAPI map_view_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->map_iid ) ||
        IsEqualGUID( iid, &IID_IMapView_Object ))
    {
        *out = &impl->vtbl_map;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &impl->iterable_iid ) ||
        IsEqualGUID( iid, &IID_IIterable_KeyValuePair_Object ))
    {
        *out = &impl->vtbl_iterable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI map_view_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_map( iface )->ref );
}
static ULONG WINAPI map_view_Release( void *iface )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        for (UINT32 i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->keys[i] );
            if (impl->values[i]) IInspectable_Release( impl->values[i] );
        }
        free( impl->keys );
        free( impl->values );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI map_view_Lookup( void *iface, HSTRING key, IInspectable **value )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    const WCHAR *raw_key = WindowsGetStringRawBuffer( key, NULL );
    if (!value) return E_POINTER;
    if (!raw_key) { *value = NULL; return E_BOUNDS; }
    for (UINT32 i = 0; i < impl->count; i++)
    {
        const WCHAR *k = WindowsGetStringRawBuffer( impl->keys[i], NULL );
        if (k && !wcscmp( k, raw_key ))
        {
            *value = impl->values[i];
            if (*value) IInspectable_AddRef( *value );
            return S_OK;
        }
    }
    *value = NULL;
    return E_BOUNDS;
}
static HRESULT WINAPI map_view_get_Size( void *iface, UINT32 *size )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    if (!size) return E_POINTER;
    *size = impl->count;
    return S_OK;
}
static HRESULT WINAPI map_view_HasKey( void *iface, HSTRING key, boolean *found )
{
    struct winrt_map_view *impl = impl_from_map( iface );
    const WCHAR *raw_key = WindowsGetStringRawBuffer( key, NULL );
    if (!found) return E_POINTER;
    if (!raw_key) { *found = FALSE; return S_OK; }
    for (UINT32 i = 0; i < impl->count; i++)
    {
        const WCHAR *k = WindowsGetStringRawBuffer( impl->keys[i], NULL );
        if (k && !wcscmp( k, raw_key ))
        {
            *found = TRUE;
            return S_OK;
        }
    }
    *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI map_view_Split( void *iface, void **first, void **second )
{
    if (first) *first = NULL;
    if (second) *second = NULL;
    return S_OK;
}
static const void *winrt_map_view_vtbl[] =
{
    map_view_QueryInterface,
    map_view_AddRef,
    map_view_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    map_view_Lookup,
    map_view_get_Size,
    map_view_HasKey,
    map_view_Split,
};

static HRESULT WINAPI map_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->map->iterator_iid ) ||
        IsEqualGUID( iid, &IID_IIterator_KeyValuePair_Object ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI map_iter_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_map_iter( iface )->ref );
}
static ULONG WINAPI map_iter_Release( void *iface )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        map_view_Release( &impl->map->vtbl_map );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI map_iter_get_Current( void *iface, IInspectable **item )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!item) return E_POINTER;
    if (impl->index >= impl->map->count) { *item = NULL; return E_BOUNDS; }
    *item = create_key_value_pair( impl->map->keys[impl->index], impl->map->values[impl->index], &impl->map->kv_iid );
    return *item ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI map_iter_get_HasCurrent( void *iface, boolean *has )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!has) return E_POINTER;
    *has = (impl->index < impl->map->count);
    return S_OK;
}
static HRESULT WINAPI map_iter_MoveNext( void *iface, boolean *has )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    if (!has) return E_POINTER;
    if (impl->index < impl->map->count) impl->index++;
    *has = (impl->index < impl->map->count);
    return S_OK;
}
static HRESULT WINAPI map_iter_GetMany( void *iface, UINT32 count, IInspectable **items, UINT32 *actual )
{
    struct winrt_map_iterator *impl = impl_from_map_iter( iface );
    UINT32 i, num;
    if (!actual) return E_POINTER;
    if (impl->index >= impl->map->count) { *actual = 0; return S_OK; }
    num = min( count, impl->map->count - impl->index );
    for (i = 0; i < num; i++)
    {
        items[i] = create_key_value_pair( impl->map->keys[impl->index + i], impl->map->values[impl->index + i], &impl->map->kv_iid );
    }
    impl->index += num;
    *actual = num;
    return S_OK;
}
static const void *winrt_map_iter_vtbl[] =
{
    map_iter_QueryInterface,
    map_iter_AddRef,
    map_iter_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    map_iter_get_Current,
    map_iter_get_HasCurrent,
    map_iter_MoveNext,
    map_iter_GetMany,
};

static HRESULT WINAPI map_iterable_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct winrt_map_view *impl = impl_from_map_iterable( iface );
    return map_view_QueryInterface( &impl->vtbl_map, iid, out );
}
static ULONG WINAPI map_iterable_AddRef( void *iface )
{
    return map_view_AddRef( &impl_from_map_iterable( iface )->vtbl_map );
}
static ULONG WINAPI map_iterable_Release( void *iface )
{
    return map_view_Release( &impl_from_map_iterable( iface )->vtbl_map );
}
static HRESULT WINAPI map_iterable_First( void *iface, void **iter )
{
    struct winrt_map_view *impl = impl_from_map_iterable( iface );
    struct winrt_map_iterator *it;
    if (!iter) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->vtbl_iterator = winrt_map_iter_vtbl;
    it->ref = 1;
    it->map = impl;
    map_view_AddRef( &impl->vtbl_map );
    it->index = 0;
    *iter = &it->vtbl_iterator;
    return S_OK;
}
static const void *winrt_map_iterable_vtbl[] =
{
    map_iterable_QueryInterface,
    map_iterable_AddRef,
    map_iterable_Release,
    vector_view_GetIids,
    vector_view_GetRuntimeClassName,
    vector_view_GetTrustLevel,
    map_iterable_First,
};

static struct winrt_map_view *store_create_map_view( HSTRING *keys, IInspectable **values, UINT32 count,
                                                      const GUID *map_iid, const GUID *iter_iid, const GUID *kv_iid )
{
    struct winrt_map_view *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl_map = winrt_map_view_vtbl;
    impl->vtbl_iterable = winrt_map_iterable_vtbl;
    impl->ref = 1;
    if (map_iid) impl->map_iid = *map_iid;
    if (iter_iid) impl->iterable_iid = *iter_iid;
    if (kv_iid) impl->kv_iid = *kv_iid;
    impl->count = count;
    if (count)
    {
        impl->keys = calloc( count, sizeof(HSTRING) );
        impl->values = calloc( count, sizeof(IInspectable *) );
        for (UINT32 i = 0; i < count; i++)
        {
            WindowsDuplicateString( keys[i], &impl->keys[i] );
            impl->values[i] = values[i];
            if (impl->values[i]) IInspectable_AddRef( impl->values[i] );
        }
    }
    return impl;
}

static void *store_create_empty_vector_view( void )
{
    return store_create_vector_view( NULL, 0, &IID_IVectorView_Object, &IID_IIterable_Object );
}

static void *store_create_empty_map_view( void )
{
    return store_create_map_view( NULL, NULL, 0, &IID_IMapView_Object, &IID_IIterable_KeyValuePair_Object, &IID_IKeyValuePair_Object );
}

static const GUID IID_IAsyncInfo_s =
    { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

struct store_async_op
{
    const void *vtbl_async_op;
    const void *vtbl_async_info;
    LONG ref;
    void *result;
    BOOL is_bool;
    BOOL bool_result;
    HRESULT error_code;
};

static inline struct store_async_op *impl_from_async_info( void *iface )
{
    return (struct store_async_op *)((char *)iface - offsetof(struct store_async_op, vtbl_async_info));
}

static HRESULT WINAPI store_async_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
    static const GUID IID_IReferenceTrackerTarget =
        { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
    static const GUID IID_IMarshal =
        { 0x00000003, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    static const GUID IID_IWeakReferenceSource =
        { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    struct store_async_op *op = iface;

    if (!out) return E_INVALIDARG;
    ERR( "store_async_QueryInterface iid=%s\n", debugstr_guid(iid) );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_s ))
    {
        *out = &op->vtbl_async_info;
        InterlockedIncrement( &op->ref );
        return S_OK;
    }
    *out = &op->vtbl_async_op;
    InterlockedIncrement( &op->ref );
    return S_OK;
}

static ULONG WINAPI store_async_AddRef( void *iface )
{
    struct store_async_op *op = iface;
    return InterlockedIncrement( &op->ref );
}
static ULONG WINAPI store_async_Release( void *iface )
{
    struct store_async_op *op = iface;
    LONG ref = InterlockedDecrement( &op->ref );
    if (!ref)
    {
        if (op->result && !op->is_bool) IUnknown_Release( (IUnknown *)op->result );
        HeapFree( GetProcessHeap(), 0, op );
    }
    return ref;
}

static HRESULT WINAPI store_async_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_async_op *op = impl_from_async_info( iface );
    return store_async_QueryInterface( &op->vtbl_async_op, iid, out );
}
static ULONG WINAPI store_async_info_AddRef( void *iface )
{
    struct store_async_op *op = impl_from_async_info( iface );
    return InterlockedIncrement( &op->ref );
}
static ULONG WINAPI store_async_info_Release( void *iface )
{
    struct store_async_op *op = impl_from_async_info( iface );
    return store_async_Release( &op->vtbl_async_op );
}
static HRESULT WINAPI store_async_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, n );
}
static HRESULT WINAPI store_async_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI store_async_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1", 34, n );
}
static HRESULT WINAPI store_async_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI store_async_put_Completed( void *iface, void *handler )
{
    struct store_async_op *op = iface;
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, &op->vtbl_async_op, 1 /* AsyncStatus_Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI store_async_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI store_async_GetResults( void *iface, void *results )
{
    struct store_async_op *op = iface;
    if (!results) return E_INVALIDARG;
    if (op->is_bool)
        *(BOOL *)results = op->bool_result;
    else
    {
        *(void **)results = op->result;
        if (op->result) IUnknown_AddRef( (IUnknown *)op->result );
    }
    return op->error_code;
}
static HRESULT WINAPI store_async_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI store_async_get_Status( void *iface, UINT32 *status ) { if (status) *status = 1; return S_OK; }
static HRESULT WINAPI store_async_get_ErrorCode( void *iface, HRESULT *code )
{
    struct store_async_op *op = impl_from_async_info( iface );
    if (code) *code = op->error_code;
    return S_OK;
}
static HRESULT WINAPI store_async_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI store_async_Close( void *iface ) { return S_OK; }

static const void *store_async_vtbl[] =
{
    store_async_QueryInterface,
    store_async_AddRef,
    store_async_Release,
    store_async_GetIids,
    store_async_GetRuntimeClassName,
    store_async_GetTrustLevel,
    store_async_put_Completed,
    store_async_get_Completed,
    store_async_GetResults,
};

static const void *store_async_info_vtbl[] =
{
    store_async_info_QueryInterface,
    store_async_info_AddRef,
    store_async_info_Release,
    store_async_GetIids,
    store_async_info_GetRuntimeClassName,
    store_async_GetTrustLevel,
    store_async_get_Id,
    store_async_get_Status,
    store_async_get_ErrorCode,
    store_async_Cancel,
    store_async_Close,
};

static HRESULT store_async_create( void *result, BOOL is_bool, BOOL bool_result, HRESULT error_code, void **out )
{
    struct store_async_op *op;
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!(op = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*op) ))) return E_OUTOFMEMORY;
    op->vtbl_async_op = store_async_vtbl;
    op->vtbl_async_info = store_async_info_vtbl;
    op->ref = 1;
    op->result = result;
    if (result && !is_bool) IUnknown_AddRef( (IUnknown *)result );
    op->is_bool = is_bool;
    op->bool_result = bool_result;
    op->error_code = error_code;
    *out = &op->vtbl_async_op;
    return S_OK;
}

static const void *winappsdk_class_vtbl[] =
{
    winappsdk_class_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
};

struct winappsdk_factory
{
    const void *vtbl;
    struct winappsdk_class *obj;
    LONG ref;
};

static HRESULT WINAPI winappsdk_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}

static HRESULT WINAPI winappsdk_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    struct winappsdk_factory *f = iface;
    *instance = (IInspectable *)f->obj;
    return S_OK;
}
static HRESULT WINAPI winappsdk_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    struct winappsdk_factory *f = iface;
    return WindowsCreateString( f->obj->name, lstrlenW( f->obj->name ), n );
}

static const void *winappsdk_factory_vtbl[] =
{
    winappsdk_factory_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_factory_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_factory_ActivateInstance,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
    winappsdk_zero_out,
};

#define WINAPPSDK_CLASS( ident, classname ) \
    static struct winappsdk_class ident##_obj = { winappsdk_class_vtbl, L##classname, 1 }; \
    static struct winappsdk_factory ident##_factory = { winappsdk_factory_vtbl, &ident##_obj, 1 }

#define WINAPPSDK_CLASS_V( ident, vtbl, classname ) \
    static struct winappsdk_class ident##_obj = { vtbl, L##classname, 1 }; \
    static struct winappsdk_factory ident##_factory = { winappsdk_factory_vtbl, &ident##_obj, 1 }
/* Info faces for the classes the combase roapi suite and the WA bootstrap walk.
 *
 * The roapi suite row asserts RoGetActivationFactory + ActivateInstance +
 * GetRuntimeClassName round-trip; beyond that row the WindowsAppSDK bootstrap
 * (Microsoft.WindowsAppRuntime.dll DeploymentManager::GetCurrent) and the
 * DynamicDependency framework-package surface walk one level deeper: the
 * DeploymentResult with its DeploymentPackage/PackageVersion, the
 * PackageDependencyContext with its Name, ContextToken and (mirroring the UWP
 * package's DependencyContext.PackageGraph) an empty IIterable<PackageDependency>.
 * These vtbls give the three classes exactly that surface, methods in the
 * alphabetical per-interface order the projection lays out, with stub-store
 * defaults; slots past these keep the shared winappsdk_zero_out answer and a
 * QI of any IID answers the same singleton, as above. */
static struct winappsdk_class deployment_result_obj;
static struct winappsdk_class package_dependency_context_obj;

#define PACKAGE_DEPENDENCY_CLASS L"Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependency"

/* A PackageDependency is a winappsdk class plus the framework family it names;
 * the graph hands out one per registered framework package, the class singletons
 * (GetDefault, GetAdjacent, the context's own dependency) are statics of it. */
struct package_dependency
{
    struct winappsdk_class cls;
    WCHAR family[128];
};

static const void *package_dependency_vtbl[];
static struct package_dependency package_dependency_obj;

/* The AppModel registration plane kernelbase answers package identity from
 * (dlls/kernelbase/version.c): HKCU over HKLM, Packages\<FullName> with a
 * PackagePath and Framework=1 for a framework package. The WindowsAppSDK
 * DependencyManager face below reads the same plane, so the DeploymentPackage
 * and the PackageDependencyContext.PackageGraph name packages the identity APIs
 * also know -- one deployment view, not two. */
#define APPMODEL_REG_PATH L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages"
#define MAX_FRAMEWORKS 8

struct framework_entry
{
    WCHAR full_name[256];
    WCHAR path[MAX_PATH];
};

static UINT32 enum_framework_packages( struct framework_entry *entries, UINT32 max )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR key[600], name[256];
    SIZE_T r, j;
    UINT32 n = 0, i;

    for (r = 0; r < ARRAY_SIZE( roots ); r++)
    {
        HKEY hkey;

        if (RegOpenKeyExW( roots[r], APPMODEL_REG_PATH, 0, KEY_READ, &hkey )) continue;
        for (i = 0; n < max; i++)
        {
            DWORD len = ARRAY_SIZE( name ), framework = 0, size = sizeof(framework), type = 0;
            HKEY pkg;

            if (RegEnumKeyExW( hkey, i, name, &len, NULL, NULL, NULL, NULL )) break;
            lstrcpyW( key, APPMODEL_REG_PATH );
            lstrcatW( key, L"\\" );
            lstrcatW( key, name );
            if (RegOpenKeyExW( roots[r], key, 0, KEY_READ, &pkg ) != ERROR_SUCCESS) continue;
            RegQueryValueExW( pkg, L"Framework", NULL, &type, (BYTE *)&framework, &size );
            size = sizeof( entries[n].path );
            type = 0;
            entries[n].path[0] = 0;
            RegQueryValueExW( pkg, L"PackagePath", NULL, &type, (BYTE *)entries[n].path, &size );
            RegCloseKey( pkg );
            if (framework != 1) continue;
            for (j = 0; j < n; j++)
                if (!wcsicmp( entries[j].full_name, name )) break;
            if (j < n) continue;  /* HKCU wins over HKLM for the same package */
            lstrcpyW( entries[n].full_name, name );
            n++;
        }
        RegCloseKey( hkey );
    }
    return n;
}

/* The family name (Name_PublisherId) of a package full name. */
static void family_from_full_name( const WCHAR *full, WCHAR *family, DWORD cch )
{
    const WCHAR *version, *publisher;

    if (!(version = wcschr( full, L'_' )) || !(publisher = wcsrchr( version, L'_' )) || publisher == version)
    {
        lstrcpynW( family, full, cch );
        return;
    }
    lstrcpynW( family, full, (version - full) + 1 );
    lstrcatW( family, L"_" );
    lstrcatW( family, publisher + 1 );
}

/* Windows.ApplicationModel.PackageVersion is a four-WORD value struct; the
 * projection's property getter takes it by pointer. */
static HRESULT WINAPI package_version_get_Major( void *iface, UINT16 *major ) { if (major) *major = 1; return S_OK; }
static HRESULT WINAPI package_version_get_Minor( void *iface, UINT16 *minor ) { if (minor) *minor = 0; return S_OK; }
static HRESULT WINAPI package_version_get_Build( void *iface, UINT16 *build ) { if (build) *build = 0; return S_OK; }
static HRESULT WINAPI package_version_get_Revision( void *iface, UINT16 *revision ) { if (revision) *revision = 0; return S_OK; }

static const void *package_version_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    package_version_get_Major,
    package_version_get_Minor,
    package_version_get_Build,
    package_version_get_Revision,
};

WINAPPSDK_CLASS( package_version, "Windows.ApplicationModel.PackageVersion" );

/* The registered framework the app pins: the WindowsAppRuntime framework of the
 * AppModel registration, the first framework registered if there is no
 * WindowsAppRuntime package, and nothing when the plane is empty. The
 * DeploymentPackage and the PackageGraph enumerate the same entry, so
 * DeploymentPackage.Id is a name the graph also reports. */
static const WCHAR winappsdk_family_prefix[] = L"Microsoft.WindowsAppRuntime.";

static BOOL winappsdk_framework( struct framework_entry *out )
{
    struct framework_entry entries[MAX_FRAMEWORKS];
    UINT32 n = enum_framework_packages( entries, MAX_FRAMEWORKS ), i;

    if (!n) return FALSE;
    for (i = 0; i < n; i++)
        if (!wcsncmp( entries[i].full_name, winappsdk_family_prefix, lstrlenW( winappsdk_family_prefix ) )) break;
    *out = entries[i < n ? i : 0];
    return TRUE;
}

/* DeploymentPackage.Id is the framework's package full name as registered. */
static HRESULT WINAPI deployment_package_get_Id( void *iface, HSTRING *id )
{
    struct framework_entry fw;

    if (!id) return S_OK;
    if (winappsdk_framework( &fw ))
        return WindowsCreateString( fw.full_name, lstrlenW( fw.full_name ), id );
    return WindowsCreateString( L"Microsoft.WindowsAppRuntime.1.6", 31, id );
}
static HRESULT WINAPI deployment_package_get_InstalledPath( void *iface, HSTRING *path )
{
    struct framework_entry fw;

    if (!path) return S_OK;
    if (winappsdk_framework( &fw ) && fw.path[0])
        return WindowsCreateString( fw.path, lstrlenW( fw.path ), path );
    return WindowsCreateString( L"C:\\Program Files\\WindowsApps\\Stub_1.0.0.0_x64__8wekyb3d8bbwe", 57, path );
}
static HRESULT WINAPI deployment_package_get_IsFramework( void *iface, BOOL *ret ) { if (ret) *ret = TRUE; return S_OK; }
/* The version field of a registered package full name (Name_V.V.V.V_arch_pub). */
static void fill_package_version( const WCHAR *full_name, struct PackageVersion *version )
{
    const UINT16 *fields[4] = { &version->Major, &version->Minor, &version->Build, &version->Revision };
    SIZE_T i;
    WCHAR *end;
    const WCHAR *v;

    version->Major = 1, version->Minor = 0, version->Build = 0, version->Revision = 0;
    if (!(v = wcschr( full_name, L'_' ))) return;
    v++;
    for (i = 0; i < 4; i++)
    {
        *(UINT16 *)fields[i] = (UINT16)wcstol( v, &end, 10 );
        if (*end != L'.') break;
        v = end + 1;
    }
}

static HRESULT WINAPI deployment_package_get_Version( void *iface, struct PackageVersion *version )
{
    struct framework_entry fw;

    if (!version) return S_OK;
    version->Major = 1, version->Minor = 0, version->Build = 0, version->Revision = 0;
    if (winappsdk_framework( &fw )) fill_package_version( fw.full_name, version );
    return S_OK;
}

static const void *deployment_package_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    deployment_package_get_Id,
    deployment_package_get_InstalledPath,
    deployment_package_get_IsFramework,
    deployment_package_get_Version,
};

WINAPPSDK_CLASS_V( deployment_package, deployment_package_vtbl, "Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentPackage" );

/* DeploymentResult: Status (stub answers IdenticalPackageAvailable) + package. */
static HRESULT WINAPI deployment_result_get_DeploymentPackage( void *iface, void **package )
{
    if (package) *package = &deployment_package_obj;
    return S_OK;
}
static HRESULT WINAPI deployment_result_get_Status( void *iface, UINT32 *status ) { if (status) *status = 1 /* IdenticalPackageAvailable */; return S_OK; }

static const void *deployment_result_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    deployment_result_get_DeploymentPackage,
    deployment_result_get_Status,
};

/* Completed IAsyncOperationWithProgress<DeploymentResult, DeploymentProgress>
 * the DeploymentManager Ensure* methods answer; fires the handler on
 * put_Completed like the StartupTask async above. */
struct winappsdk_async
{
    const void *vtbl;
    LONG ref;
};

static HRESULT WINAPI winappsdk_async_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, 1 /* AsyncStatus_Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI winappsdk_async_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI winappsdk_async_GetResults( void *iface, void **results ) { if (results) *results = &deployment_result_obj; return S_OK; }
static HRESULT WINAPI winappsdk_async_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI winappsdk_async_get_Status( void *iface, UINT32 *status ) { if (status) *status = 1 /* Completed */; return S_OK; }
static HRESULT WINAPI winappsdk_async_get_ErrorCode( void *iface, HRESULT *code ) { if (code) *code = S_OK; return S_OK; }
static HRESULT WINAPI winappsdk_async_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI winappsdk_async_Close( void *iface ) { return S_OK; }

static const void *winappsdk_async_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_async_put_Completed,
    winappsdk_async_get_Completed,
    winappsdk_async_GetResults,
    winappsdk_async_get_Id,
    winappsdk_async_get_Status,
    winappsdk_async_get_ErrorCode,
    winappsdk_async_Cancel,
    winappsdk_async_Close,
};

static struct winappsdk_async deployment_async_inst = { winappsdk_async_vtbl, 1 };

static HRESULT WINAPI deployment_manager_Ensure( void *iface, void **op ) { if (op) *op = &deployment_async_inst; return S_OK; }
static HRESULT WINAPI deployment_manager_get_CurrentPackages( void *iface, void **op );
static HRESULT WINAPI deployment_manager_factory_get_Current( void *iface, void **manager );

static const void *deployment_manager_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    deployment_manager_Ensure,     /* EnsureCurrentApplicationDeployedAsync */
    deployment_manager_Ensure,     /* EnsureLatestAsync */
    deployment_manager_get_CurrentPackages, /* CurrentPackages (1.7) */
    winappsdk_zero_out,
};

static HRESULT WINAPI deployment_manager_factory_GetCurrent( void *iface, void **result )
{
    if (result) *result = &deployment_result_obj;
    return S_OK;
}

static const void *deployment_manager_factory_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_factory_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_factory_ActivateInstance,
    deployment_manager_factory_GetCurrent,
    deployment_manager_factory_get_Current,  /* IDeploymentManagerStatics2::get_Current (1.4) */
};

static struct winappsdk_class deployment_manager_obj =
{
    deployment_manager_vtbl, L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentManager", 1
};
static struct winappsdk_factory deployment_manager_factory =
{
    deployment_manager_factory_vtbl, &deployment_manager_obj, 1
};

/* DeploymentManager.Current / CurrentPackages --------------------------------
 *
 * Current is the 1.4 statics property the projection reaches first; the instance's
 * CurrentPackages (1.7) is a born-completed IAsyncOperation over an
 * IVectorView<PackageDependencyPackageInfo>, one row per registered framework
 * package on the AppModel plane above -- the same enumeration the PackageGraph
 * iterable and DeploymentPackage.Id answer, so Package.Current dependents read one
 * deployment view, not two. A row is a PackageDependencyPackageInfo: its
 * DependencyPackageInfo names the family (Dependency), the package full name
 * (PackageId) and path straight from the registry; its Package is a
 * Windows.ApplicationModel.Package face carrying the same identity -- Id.FullName,
 * Id.Version parsed from it, InstalledLocation as a file:/// URI, IsFramework.
 *
 * The rows are per-snapshot heap objects (unlike the class singletons above): the
 * enumeration is a picture of the registry taken at the call, and the last
 * reference to a row frees it. Slots follow the WinAppSDK metadata order:
 * IPackageDependencyPackageInfo { DependencyPackageInfo, Package },
 * IDependencyPackageInfo { Dependency, PackageId, PackagePath, Version },
 * IPackage { Id, InstalledLocation, IsFramework, IsResourcePackage, ... },
 * IPackageId { FullName, FamilyName, PublisherId, Version, Architecture, ... },
 * IVectorView { GetAt, Size, IndexOf, GetMany }. */

struct fw_face
{
    const void *vtbl;
    const WCHAR *name;
    LONG ref;
    struct framework_entry fw;
};

static const void *package_dependency_package_info_vtbl[];
static const void *dependency_package_info_vtbl[];
static const void *fw_package_vtbl[];
static const void *fw_package_id_vtbl[];
static const void *fw_uri_vtbl[];
static const void *packages_async_vtbl[];

static const WCHAR package_dependency_package_info_class[] =
    L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.PackageDependencyPackageInfo";
static const WCHAR dependency_package_info_class[] =
    L"Microsoft.Windows.ApplicationModel.PackageGraph.DependencyPackageInfo";
static const WCHAR fw_package_class[] = L"Windows.ApplicationModel.Package";
static const WCHAR fw_package_id_class[] = L"Windows.ApplicationModel.PackageId";
static const WCHAR fw_uri_class[] = L"Windows.Foundation.Uri";
static const WCHAR packages_view_class[] =
    L"Windows.Foundation.Collections.IVectorView`1<Microsoft.Windows.ApplicationModel.WindowsAppRuntime.PackageDependencyPackageInfo>";
static const WCHAR packages_async_class[] =
    L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<"
    L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.PackageDependencyPackageInfo>>";

static HRESULT WINAPI fw_face_QueryInterface( struct fw_face *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( &iface->ref );
    return S_OK;
}
static ULONG WINAPI fw_face_AddRef( struct fw_face *iface ) { return InterlockedIncrement( &iface->ref ); }
static ULONG WINAPI fw_face_Release( struct fw_face *iface )
{
    LONG ref = InterlockedDecrement( &iface->ref );
    if (!ref) HeapFree( GetProcessHeap(), 0, iface );
    return ref;
}
static HRESULT WINAPI fw_face_GetRuntimeClassName( struct fw_face *iface, HSTRING *name )
{
    return WindowsCreateString( iface->name, lstrlenW( iface->name ), name );
}

static struct fw_face *fw_face_create( const void *vtbl, const WCHAR *name, const struct framework_entry *fw )
{
    struct fw_face *face = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*face) );

    if (!face) return NULL;
    face->vtbl = vtbl;
    face->name = name;
    face->ref = 1;
    if (fw) face->fw = *fw;
    return face;
}

static HRESULT WINAPI fw_hstring( const WCHAR *value, HSTRING *out )
{
    if (!out) return S_OK;
    *out = NULL;
    if (!value[0]) return S_OK;
    return WindowsCreateString( value, lstrlenW( value ), out );
}

/* IDependencyPackageInfo (Microsoft.Windows.ApplicationModel.PackageGraph). */
static HRESULT WINAPI dep_package_info_get_Dependency( struct fw_face *iface, void **dep )
{
    struct package_dependency *d;

    if (!dep) return S_OK;
    *dep = NULL;
    if (!(d = HeapAlloc( GetProcessHeap(), 0, sizeof(*d) ))) return E_OUTOFMEMORY;
    d->cls.vtbl = package_dependency_vtbl;
    d->cls.name = PACKAGE_DEPENDENCY_CLASS;
    d->cls.ref = 1;  /* the caller's reference; the row owns none */
    family_from_full_name( iface->fw.full_name, d->family, ARRAY_SIZE(d->family) );
    *dep = d;
    return S_OK;
}
static HRESULT WINAPI dep_package_info_get_PackageId( struct fw_face *iface, HSTRING *id )
{
    return fw_hstring( iface->fw.full_name, id );
}
static HRESULT WINAPI dep_package_info_get_PackagePath( struct fw_face *iface, HSTRING *path )
{
    return fw_hstring( iface->fw.path, path );
}
static HRESULT WINAPI dep_package_info_get_Version( struct fw_face *iface, struct PackageVersion *version )
{
    if (version) fill_package_version( iface->fw.full_name, version );
    return S_OK;
}

/* IPackageId: the registry identity, Name_PublisherId split out of the full name. */
static HRESULT WINAPI fw_package_id_get_FullName( struct fw_face *iface, HSTRING *value )
{
    return fw_hstring( iface->fw.full_name, value );
}
static HRESULT WINAPI fw_package_id_get_FamilyName( struct fw_face *iface, HSTRING *value )
{
    WCHAR family[128];

    family_from_full_name( iface->fw.full_name, family, ARRAY_SIZE(family) );
    return fw_hstring( family, value );
}
static HRESULT WINAPI fw_package_id_get_PublisherId( struct fw_face *iface, HSTRING *value )
{
    const WCHAR *publisher = wcsrchr( iface->fw.full_name, L'_' );

    return fw_hstring( publisher ? publisher + 1 : L"", value );
}
static HRESULT WINAPI fw_package_id_get_Version( struct fw_face *iface, struct PackageVersion *version )
{
    if (version) fill_package_version( iface->fw.full_name, version );
    return S_OK;
}
static HRESULT WINAPI fw_package_id_get_Architecture( struct fw_face *iface, UINT32 *value )
{
    if (value) *value = 5; /* PackageArchitecture_X64: the prefix is x64 */
    return S_OK;
}

/* Windows.Foundation.Uri face for InstalledLocation: file:///<PackagePath>. */
static HRESULT WINAPI fw_uri_get_AbsoluteUri( struct fw_face *iface, HSTRING *value )
{
    WCHAR uri[MAX_PATH + 16];

    if (!value) return S_OK;
    *value = NULL;
    if (!iface->fw.path[0]) return S_OK;
    lstrcpyW( uri, L"file:///" );
    lstrcatW( uri, iface->fw.path );
    return WindowsCreateString( uri, lstrlenW( uri ), value );
}

/* IPackage: the framework package as Package.Current dependents see it. */
static HRESULT WINAPI fw_package_get_Id( struct fw_face *iface, void **id )
{
    struct fw_face *face;

    if (!id) return S_OK;
    *id = NULL;
    if (!(face = fw_face_create( fw_package_id_vtbl, fw_package_id_class, &iface->fw ))) return E_OUTOFMEMORY;
    *id = face;
    return S_OK;
}
static HRESULT WINAPI fw_package_get_InstalledLocation( struct fw_face *iface, void **uri )
{
    struct fw_face *face;

    if (!uri) return S_OK;
    *uri = NULL;
    if (!(face = fw_face_create( fw_uri_vtbl, fw_uri_class, &iface->fw ))) return E_OUTOFMEMORY;
    *uri = face;
    return S_OK;
}
static HRESULT WINAPI fw_package_get_IsFramework( struct fw_face *iface, BOOL *ret ) { if (ret) *ret = TRUE; return S_OK; }

/* IPackageDependencyPackageInfo: the graph row + the deployment row. */
static HRESULT WINAPI package_info_get_DependencyPackageInfo( struct fw_face *iface, void **info )
{
    struct fw_face *face;

    if (!info) return S_OK;
    *info = NULL;
    if (!(face = fw_face_create( dependency_package_info_vtbl, dependency_package_info_class, &iface->fw )))
        return E_OUTOFMEMORY;
    *info = face;
    return S_OK;
}
static HRESULT WINAPI package_info_get_Package( struct fw_face *iface, void **package )
{
    struct fw_face *face;

    if (!package) return S_OK;
    *package = NULL;
    if (!(face = fw_face_create( fw_package_vtbl, fw_package_class, &iface->fw ))) return E_OUTOFMEMORY;
    *package = face;
    return S_OK;
}

/* IVectorView<PackageDependencyPackageInfo>: the snapshot. */
struct packages_view
{
    const void *vtbl;
    LONG ref;
    struct fw_face **items;
    UINT32 count;
};

static HRESULT packages_view_create( void **out );

/* The async face is born completed; the view is the snapshot taken at GetResults. */
static HRESULT WINAPI packages_async_put_Completed( struct fw_face *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, 1 /* AsyncStatus_Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI packages_async_get_Completed( struct fw_face *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI packages_async_GetResults( struct fw_face *iface, void **results )
{
    if (!results) return E_INVALIDARG;
    *results = NULL;
    return packages_view_create( results );
}
static HRESULT WINAPI packages_async_get_Id( struct fw_face *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI packages_async_get_Status( struct fw_face *iface, UINT32 *status ) { if (status) *status = 1; return S_OK; }
static HRESULT WINAPI packages_async_get_ErrorCode( struct fw_face *iface, HRESULT *code ) { if (code) *code = S_OK; return S_OK; }

static HRESULT WINAPI packages_view_GetAt( struct packages_view *iface, UINT32 index, void **item )
{
    if (!item) return E_INVALIDARG;
    if (index >= iface->count) return E_BOUNDS;
    *item = iface->items[index];
    InterlockedIncrement( &((struct fw_face *)*item)->ref );
    return S_OK;
}
static HRESULT WINAPI packages_view_get_Size( struct packages_view *iface, UINT32 *size )
{
    if (size) *size = iface->count;
    return S_OK;
}
static HRESULT WINAPI packages_view_IndexOf( struct packages_view *iface, void *item, UINT32 *index, BOOL *found )
{
    UINT32 i;

    if (index) *index = 0;
    if (!found) return S_OK;
    *found = FALSE;
    for (i = 0; i < iface->count; i++)
        if (iface->items[i] == item) { *index = i; *found = TRUE; break; }
    return S_OK;
}
static HRESULT WINAPI packages_view_GetMany( struct packages_view *iface, UINT32 start, UINT32 capacity, void **items,
                                             UINT32 *written )
{
    UINT32 n = 0;

    if (!written) return E_INVALIDARG;
    *written = 0;
    while (start + n < iface->count && n < capacity)
    {
        if (items)
        {
            items[n] = iface->items[start + n];
            InterlockedIncrement( &((struct fw_face *)items[n])->ref );
        }
        n++;
    }
    *written = n;
    return S_OK;
}
static ULONG WINAPI packages_view_AddRef( struct packages_view *iface ) { return InterlockedIncrement( &iface->ref ); }
static ULONG WINAPI packages_view_Release( struct packages_view *iface )
{
    LONG ref = InterlockedDecrement( &iface->ref );

    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < iface->count; i++) fw_face_Release( iface->items[i] );
        HeapFree( GetProcessHeap(), 0, iface->items );
        HeapFree( GetProcessHeap(), 0, iface );
    }
    return ref;
}
static HRESULT WINAPI packages_view_QueryInterface( struct packages_view *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( &iface->ref );
    return S_OK;
}
static HRESULT WINAPI packages_view_GetRuntimeClassName( struct packages_view *iface, HSTRING *name )
{
    return WindowsCreateString( packages_view_class, lstrlenW( packages_view_class ), name );
}

static const void *packages_view_vtbl[] =
{
    packages_view_QueryInterface,
    packages_view_AddRef,
    packages_view_Release,
    winappsdk_GetIids,
    packages_view_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    packages_view_GetAt,
    packages_view_get_Size,
    packages_view_IndexOf,
    packages_view_GetMany,
};

static HRESULT packages_view_create( void **out )
{
    struct framework_entry entries[MAX_FRAMEWORKS];
    struct packages_view *view;
    UINT32 n, i;

    if (!(view = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*view) ))) return E_OUTOFMEMORY;
    view->vtbl = packages_view_vtbl;
    view->ref = 1;

    n = enum_framework_packages( entries, MAX_FRAMEWORKS );
    if (n && !(view->items = HeapAlloc( GetProcessHeap(), 0, n * sizeof(*view->items) )))
    {
        HeapFree( GetProcessHeap(), 0, view );
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < n; i++)
    {
        if (!(view->items[i] = fw_face_create( package_dependency_package_info_vtbl,
                                               package_dependency_package_info_class, &entries[i] )))
        {
            packages_view_Release( view );
            return E_OUTOFMEMORY;
        }
        view->count++;
    }
    *out = view;
    return S_OK;
}

static HRESULT deployment_manager_get_CurrentPackages_impl( void **op );

static const void *dependency_package_info_vtbl[] =
{
    fw_face_QueryInterface,
    fw_face_AddRef,
    fw_face_Release,
    winappsdk_GetIids,
    fw_face_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    dep_package_info_get_Dependency,
    dep_package_info_get_PackageId,
    dep_package_info_get_PackagePath,
    dep_package_info_get_Version,
};

static const void *fw_package_vtbl[] =
{
    fw_face_QueryInterface,
    fw_face_AddRef,
    fw_face_Release,
    winappsdk_GetIids,
    fw_face_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    fw_package_get_Id,
    fw_package_get_InstalledLocation,
    fw_package_get_IsFramework,
    winappsdk_zero_out,          /* IsResourcePackage */
};

static const void *fw_package_id_vtbl[] =
{
    fw_face_QueryInterface,
    fw_face_AddRef,
    fw_face_Release,
    winappsdk_GetIids,
    fw_face_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    fw_package_id_get_FullName,
    fw_package_id_get_FamilyName,
    fw_package_id_get_PublisherId,
    fw_package_id_get_Version,
    fw_package_id_get_Architecture,
    winappsdk_zero_out,          /* ResourceId */
    winappsdk_zero_out,          /* RelativeId */
};

static const void *fw_uri_vtbl[] =
{
    fw_face_QueryInterface,
    fw_face_AddRef,
    fw_face_Release,
    winappsdk_GetIids,
    fw_face_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    fw_uri_get_AbsoluteUri,
    winappsdk_zero_out,          /* RelativeUri */
    winappsdk_zero_out,          /* AbsoluteCanonicalUri */
    winappsdk_zero_out,          /* DisplayUri */
    winappsdk_zero_out,          /* DisplaySpecifiedUri */
};

static const void *package_dependency_package_info_vtbl[] =
{
    fw_face_QueryInterface,
    fw_face_AddRef,
    fw_face_Release,
    winappsdk_GetIids,
    fw_face_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    package_info_get_DependencyPackageInfo,
    package_info_get_Package,
};

/* IAsyncOperation<IVectorView<PackageDependencyPackageInfo>>: born completed;
 * one per CurrentPackages call, GetResults takes a fresh snapshot. */
static const void *packages_async_vtbl[] =
{
    fw_face_QueryInterface,
    fw_face_AddRef,
    fw_face_Release,
    winappsdk_GetIids,
    fw_face_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    packages_async_put_Completed,
    packages_async_get_Completed,
    packages_async_GetResults,
    packages_async_get_Id,
    packages_async_get_Status,
    packages_async_get_ErrorCode,
    winappsdk_zero_out,          /* Cancel (one-arg shape answers fine) */
    winappsdk_zero_out,          /* Close */
};

static HRESULT deployment_manager_get_CurrentPackages_impl( void **op )
{
    struct fw_face *face;

    if (!(face = fw_face_create( packages_async_vtbl, packages_async_class, NULL ))) return E_OUTOFMEMORY;
    *op = face;
    return S_OK;
}

static HRESULT WINAPI deployment_manager_get_CurrentPackages( void *iface, void **op )
{
    if (!op) return E_INVALIDARG;
    *op = NULL;
    return deployment_manager_get_CurrentPackages_impl( op );
}

static HRESULT WINAPI deployment_manager_factory_get_Current( void *iface, void **manager )
{
    if (manager) *manager = &deployment_manager_obj;
    return S_OK;
}

/* The context's PackageGraph face: IIterable<PackageDependency> over the
 * registered framework packages (the AppModel plane above), so the dependency
 * graph enumerates the same packages the identity APIs answer -- the
 * DeploymentPackage.Id is one of the names it reports. First() takes a fresh
 * snapshot; the iterator owns one PackageDependency element per framework. */
struct package_graph_iterator
{
    struct winappsdk_class cls;
    struct package_dependency **items;
    UINT32 count, index;
};

static const WCHAR package_graph_iterator_class[] =
    L"Windows.Foundation.Collections.IIterator`1<Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependency>";

static HRESULT WINAPI package_graph_iterator_get_Current( void *iface, void **item )
{
    struct package_graph_iterator *iter = iface;

    struct package_dependency *dep;

    if (!item) return E_INVALIDARG;
    dep = iter->index < iter->count ? iter->items[iter->index] : NULL;
    if (dep) InterlockedIncrement( &dep->cls.ref );
    *item = dep;
    return S_OK;
}
static HRESULT WINAPI package_graph_iterator_get_HasCurrent( void *iface, BOOL *ret )
{
    struct package_graph_iterator *iter = iface;

    if (ret) *ret = iter->index < iter->count;
    return S_OK;
}
static HRESULT WINAPI package_graph_iterator_MoveNext( void *iface, BOOL *ret )
{
    struct package_graph_iterator *iter = iface;

    if (iter->index < iter->count) iter->index++;
    if (ret) *ret = iter->index < iter->count;
    return S_OK;
}
static HRESULT WINAPI package_graph_iterator_GetMany( void *iface, UINT32 size, void *items, UINT32 *count )
{
    struct package_graph_iterator *iter = iface;
    UINT32 n = 0;

    if (!count) return E_INVALIDARG;
    while (n < size && iter->index < iter->count)
    {
        if (items)
        {
            struct package_dependency **out = items;
            out[n] = iter->items[iter->index];
            InterlockedIncrement( &out[n]->cls.ref );
        }
        n++, iter->index++;
    }
    *count = n;
    return S_OK;
}

static ULONG WINAPI package_graph_iterator_AddRef( void *iface )
{
    struct package_graph_iterator *iter = iface;

    return InterlockedIncrement( &iter->cls.ref );
}
static ULONG WINAPI package_graph_iterator_Release( void *iface )
{
    struct package_graph_iterator *iter = iface;
    LONG ref = InterlockedDecrement( &iter->cls.ref );

    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < iter->count; i++)
        {
            if (!InterlockedDecrement( &iter->items[i]->cls.ref )) HeapFree( GetProcessHeap(), 0, iter->items[i] );
        }
        HeapFree( GetProcessHeap(), 0, iter->items );
        HeapFree( GetProcessHeap(), 0, iter );
    }
    return ref;
}

static const void *package_graph_iterator_vtbl[] =
{
    winappsdk_QueryInterface,
    package_graph_iterator_AddRef,
    package_graph_iterator_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    package_graph_iterator_get_Current,
    package_graph_iterator_get_HasCurrent,
    package_graph_iterator_MoveNext,
    package_graph_iterator_GetMany,
};

static HRESULT WINAPI package_graph_First( void *iface, void **iterator )
{
    struct framework_entry entries[MAX_FRAMEWORKS];
    struct package_graph_iterator *iter;
    UINT32 n, i;

    if (!iterator) return E_INVALIDARG;
    *iterator = NULL;
    if (!(iter = HeapAlloc( GetProcessHeap(), 0, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->cls.vtbl = package_graph_iterator_vtbl;
    iter->cls.name = package_graph_iterator_class;
    iter->cls.ref = 1;
    iter->items = NULL;
    iter->count = iter->index = 0;

    n = enum_framework_packages( entries, MAX_FRAMEWORKS );
    if (n && !(iter->items = HeapAlloc( GetProcessHeap(), 0, n * sizeof(*iter->items) )))
    {
        HeapFree( GetProcessHeap(), 0, iter );
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < n; i++)
    {
        struct package_dependency *dep = HeapAlloc( GetProcessHeap(), 0, sizeof(*dep) );

        if (!dep)
        {
            package_graph_iterator_Release( iter );
            return E_OUTOFMEMORY;
        }
        dep->cls.vtbl = package_dependency_vtbl;
        dep->cls.name = PACKAGE_DEPENDENCY_CLASS;
        dep->cls.ref = 1;  /* the iterator's reference; the face hands out its own */
        family_from_full_name( entries[i].full_name, dep->family, ARRAY_SIZE(dep->family) );
        iter->items[iter->count++] = dep;
    }
    *iterator = iter;
    return S_OK;
}

static const void *package_graph_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    package_graph_First,
};

WINAPPSDK_CLASS_V( package_graph, package_graph_vtbl, "Windows.Foundation.Collections.IIterable`1<Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependency>" );

/* PackageDependencyContext: ContextToken (one installed dependency = token 1),
 * the PackageDependency it points at, and the PackageGraph iterable. */
static HRESULT WINAPI package_dependency_context_get_ContextToken( void *iface, INT32 *token ) { if (token) *token = 1; return S_OK; }
static HRESULT WINAPI package_dependency_context_get_PackageDependency( void *iface, void **dep )
{
    if (dep) *dep = &package_dependency_obj;
    return S_OK;
}
static HRESULT WINAPI package_dependency_context_get_PackageGraph( void *iface, void **graph )
{
    if (graph) *graph = &package_graph_obj;
    return S_OK;
}

static const void *package_dependency_context_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    package_dependency_context_get_ContextToken,
    package_dependency_context_get_PackageDependency,
    package_dependency_context_get_PackageGraph,
    winappsdk_zero_out,
};

/* PackageDependency: the framework family it names. GetDefault/GetAdjacent
 * answer the WinAppSDK EmojiData framework the projection pins (the static
 * singleton below); the PackageGraph hands out one of these per registered
 * framework package, its Name that package's family. The singleton is static,
 * the graph's elements are heap blocks the last reference releases. */
static HRESULT WINAPI package_dependency_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct package_dependency *dep = iface;

    *out = iface;
    InterlockedIncrement( &dep->cls.ref );
    return S_OK;
}
static ULONG WINAPI package_dependency_AddRef( void *iface )
{
    struct package_dependency *dep = iface;

    return InterlockedIncrement( &dep->cls.ref );
}
static ULONG WINAPI package_dependency_Release( void *iface )
{
    struct package_dependency *dep = iface;
    LONG ref = InterlockedDecrement( &dep->cls.ref );

    if (!ref && dep != &package_dependency_obj) HeapFree( GetProcessHeap(), 0, dep );
    return ref;
}
static HRESULT WINAPI package_dependency_get_Context( void *iface, void **context )
{
    if (context) *context = &package_dependency_context_obj;
    return S_OK;
}
static HRESULT WINAPI package_dependency_get_Name( void *iface, HSTRING *name )
{
    const struct package_dependency *dep = iface;

    if (name) return WindowsCreateString( dep->family, lstrlenW( dep->family ), name );
    return S_OK;
}

static const void *package_dependency_vtbl[] =
{
    package_dependency_QueryInterface,
    package_dependency_AddRef,
    package_dependency_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    package_dependency_get_Context,
    package_dependency_get_Name,
};

static HRESULT WINAPI package_dependency_factory_AddPackageDependency( void *iface, HSTRING family_name, void **context )
{
    if (context) *context = &package_dependency_context_obj;
    return S_OK;
}
static HRESULT WINAPI package_dependency_factory_AddPackageDependencyWithApplicabilityFilters( void *iface, void *args, void **context )
{
    if (context) *context = &package_dependency_context_obj;
    return S_OK;
}
static HRESULT WINAPI package_dependency_factory_CreatePackageDependency( void *iface, void *args, void **context )
{
    if (context) *context = &package_dependency_context_obj;
    return S_OK;
}
static HRESULT WINAPI package_dependency_factory_GetAdjacent( void *iface, HSTRING family_name, void **dep )
{
    if (dep) *dep = &package_dependency_obj;
    return S_OK;
}
static HRESULT WINAPI package_dependency_factory_GetDefault( void *iface, void **dep )
{
    if (dep) *dep = &package_dependency_obj;
    return S_OK;
}

static const void *package_dependency_factory_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_factory_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_factory_ActivateInstance,
    package_dependency_factory_AddPackageDependency,
    package_dependency_factory_AddPackageDependencyWithApplicabilityFilters,
    package_dependency_factory_CreatePackageDependency,
    package_dependency_factory_GetAdjacent,
    package_dependency_factory_GetDefault,
};

/* AddPackageDependencyOptions: DependencyKinds, PackageDependencyRank, Rank. */
static HRESULT WINAPI add_options_get_DependencyKinds( void *iface, UINT32 *kinds ) { if (kinds) *kinds = 0; return S_OK; }
static HRESULT WINAPI add_options_put_DependencyKinds( void *iface, UINT32 kinds ) { return S_OK; }
static HRESULT WINAPI add_options_get_PackageDependencyRank( void *iface, void *rank ) { if (rank) *(UINT64 *)rank = 0; return S_OK; }
static HRESULT WINAPI add_options_put_PackageDependencyRank( void *iface, void *rank ) { return S_OK; }
static HRESULT WINAPI add_options_get_Rank( void *iface, INT32 *rank ) { if (rank) *rank = 0; return S_OK; }
static HRESULT WINAPI add_options_put_Rank( void *iface, INT32 rank ) { return S_OK; }

static const void *add_package_dependency_options_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    add_options_get_DependencyKinds,
    add_options_put_DependencyKinds,
    add_options_get_PackageDependencyRank,
    add_options_put_PackageDependencyRank,
    add_options_get_Rank,
    add_options_put_Rank,
};

WINAPPSDK_CLASS( deployment_init_options, "Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentInitializeOptions" );
WINAPPSDK_CLASS( create_package_dependency_options, "Microsoft.Windows.ApplicationModel.DynamicDependency.CreatePackageDependencyOptions" );
WINAPPSDK_CLASS( package_dependency_rank, "Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependencyRank" );
WINAPPSDK_CLASS( environment_manager, "Microsoft.Windows.System.EnvironmentManager" );
WINAPPSDK_CLASS( app_install_options, "Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallOptions" );
WINAPPSDK_CLASS( app_update_options, "Windows.ApplicationModel.Store.Preview.InstallControl.AppUpdateOptions" );
WINAPPSDK_CLASS( store_system_feature, "Windows.ApplicationModel.Store.Preview.StoreSystemFeature" );
WINAPPSDK_CLASS( web_auth_core_helper, "Windows.ApplicationModel.Store.Preview.WebAuthenticationCoreManagerHelper" );
WINAPPSDK_CLASS( license_manager, "Windows.ApplicationModel.Store.LicenseManagement.LicenseManager" );

/* =========================================================================
 * Store Support: StoreProduct, StorePrice, StoreAppLicense, StoreContext,
 * StoreConfiguration, AppInstallManager
 * ========================================================================= */

static const GUID IID_IAppInstallManager =
    { 0x9353e170, 0x8441, 0x4b45, { 0xbd, 0x72, 0x7c, 0x2f, 0xa9, 0x25, 0xbe, 0xee } };
static const GUID IID_IAppInstallManager2 =
    { 0x16937851, 0xed37, 0x480d, { 0x83, 0x14, 0x52, 0xe2, 0x7c, 0x03, 0xf0, 0x4a } };
static const GUID IID_IAppInstallManager3 =
    { 0x95b24b17, 0xe96a, 0x4d0e, { 0x84, 0xe1, 0xc8, 0xcb, 0x41, 0x7a, 0x01, 0x78 } };
static const GUID IID_IAppInstallManager4 =
    { 0x260a2a16, 0x5a9e, 0x4ebd, { 0xb9, 0x44, 0xf2, 0xba, 0x75, 0xc3, 0x11, 0x59 } };
static const GUID IID_IAppInstallManager5 =
    { 0x3cd7be4c, 0x1be9, 0x4f7f, { 0xb6, 0x75, 0xaa, 0x1d, 0x64, 0xa5, 0x29, 0xb2 } };

static const GUID IID_IStoreContextStatics =
    { 0x9c06ee5f, 0x15c0, 0x4e72, { 0x93, 0x30, 0xd6, 0x19, 0x1c, 0xeb, 0xd1, 0x9c } };
static const GUID IID_IStoreContext =
    { 0xac98b6be, 0xf4fd, 0x4912, { 0xba, 0xbd, 0x50, 0x35, 0xe5, 0xe8, 0xbc, 0xab } };
static const GUID IID_IStoreContext2 =
    { 0x18bc54da, 0x7bd9, 0x452c, { 0x91, 0x16, 0x3b, 0xbd, 0x06, 0xff, 0xc6, 0x3a } };

static const GUID IID_IStoreConfigurationStatics =
    { 0x728f7fc0, 0x8628, 0x42ec, { 0x84, 0xa2, 0x07, 0x78, 0x0e, 0xb4, 0x4d, 0x8b } };
static const GUID IID_IStoreConfigurationStatics2 =
    { 0x657c4595, 0xc8b7, 0x4fe9, { 0x9f, 0x4c, 0x4d, 0x71, 0x02, 0x7d, 0x34, 0x7e } };
static const GUID IID_IStoreConfigurationStatics3 =
    { 0x6d45f57c, 0xf144, 0x4cb5, { 0x9d, 0x3f, 0x4e, 0xb0, 0x5e, 0x30, 0xb6, 0xd3 } };
static const GUID IID_IStoreConfigurationStatics4 =
    { 0x20ff56d2, 0x4ee3, 0x4cf0, { 0x9b, 0x12, 0x55, 0x2c, 0x03, 0x31, 0x0f, 0x75 } };

/* --- Genuine WinRT Store Data Models --- */
static const GUID IID_IStorePrice =
    { 0x55ba94c4, 0x15f1, 0x407c, { 0x8f, 0x06, 0x00, 0x63, 0x80, 0xf4, 0xdf, 0x0b } };
static const GUID IID_IStoreImage =
    { 0x081fd248, 0xadb4, 0x4b64, { 0xa9, 0x93, 0x78, 0x47, 0x89, 0x92, 0x6e, 0xd5 } };
static const GUID IID_IStoreProduct =
    { 0x320e2c52, 0xd760, 0x450a, { 0xa4, 0x2b, 0x67, 0xd1, 0xe9, 0x01, 0xac, 0x90 } };
static const GUID IID_IStoreProductQueryResult =
    { 0xd805e6c5, 0xd456, 0x4ff6, { 0x80, 0x49, 0x90, 0x76, 0xd5, 0x16, 0x5f, 0x73 } };
static const GUID IID_IStoreProductPagedQueryResult =
    { 0xc92718c5, 0x4dd5, 0x4869, { 0xa4, 0x62, 0xec, 0xc6, 0x87, 0x2e, 0x43, 0xc5 } };
static const GUID IID_IStoreProductResult =
    { 0xb7674f73, 0x3c87, 0x4ee1, { 0x82, 0x01, 0xf4, 0x28, 0x35, 0x9b, 0xd3, 0xaf } };

static IInspectable *create_uri( const WCHAR *uri_str )
{
    IActivationFactory *af = NULL;
    IUriRuntimeClassFactory *factory = NULL;
    IUriRuntimeClass *uri = NULL;
    HSTRING class_name = NULL, uri_hstr = NULL;
    HRESULT hr;

    if (!uri_str) return NULL;
    if (FAILED( WindowsCreateString( L"Windows.Foundation.Uri", 22, &class_name ) )) return NULL;
    if (FAILED( WindowsCreateString( uri_str, wcslen(uri_str), &uri_hstr ) ))
    {
        WindowsDeleteString( class_name );
        return NULL;
    }

    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&af );
    if (SUCCEEDED(hr) && af)
    {
        hr = IActivationFactory_QueryInterface( af, &IID_IUriRuntimeClassFactory, (void **)&factory );
        if (SUCCEEDED(hr) && factory)
        {
            IUriRuntimeClassFactory_CreateUri( factory, uri_hstr, &uri );
            IUriRuntimeClassFactory_Release( factory );
        }
        IActivationFactory_Release( af );
    }
    WindowsDeleteString( uri_hstr );
    WindowsDeleteString( class_name );
    return (IInspectable *)uri;
}

struct store_price
{
    const void *vtbl;
    LONG ref;
    HSTRING base_price;
    HSTRING price;
    boolean is_on_sale;
    INT64 sale_end_date;
    HSTRING currency;
    HSTRING recurrence;
};

static inline struct store_price *impl_from_store_price( void *iface )
{
    return (struct store_price *)iface;
}
static HRESULT WINAPI store_price_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStorePrice ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI store_price_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_store_price( iface )->ref );
}
static ULONG WINAPI store_price_Release( void *iface )
{
    struct store_price *impl = impl_from_store_price( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->base_price );
        WindowsDeleteString( impl->price );
        WindowsDeleteString( impl->currency );
        WindowsDeleteString( impl->recurrence );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI store_price_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI store_price_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StorePrice";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI store_price_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI store_price_get_FormattedBasePrice( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->base_price, value );
}
static HRESULT WINAPI store_price_get_FormattedPrice( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->price, value );
}
static HRESULT WINAPI store_price_get_IsOnSale( void *iface, boolean *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    *value = impl->is_on_sale;
    return S_OK;
}
static HRESULT WINAPI store_price_get_SaleEndDate( void *iface, void *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    memcpy( value, &impl->sale_end_date, sizeof(INT64) );
    return S_OK;
}
static HRESULT WINAPI store_price_get_CurrencyCode( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->currency, value );
}
static HRESULT WINAPI store_price_get_FormattedRecurrencePrice( void *iface, HSTRING *value )
{
    struct store_price *impl = impl_from_store_price( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->recurrence, value );
}
static const void *store_price_vtbl[] =
{
    store_price_QueryInterface,
    store_price_AddRef,
    store_price_Release,
    store_price_GetIids,
    store_price_GetRuntimeClassName,
    store_price_GetTrustLevel,
    store_price_get_FormattedBasePrice,
    store_price_get_FormattedPrice,
    store_price_get_IsOnSale,
    store_price_get_SaleEndDate,
    store_price_get_CurrencyCode,
    store_price_get_FormattedRecurrencePrice,
};

static struct store_price *store_price_create( const WCHAR *base, const WCHAR *price, const WCHAR *currency )
{
    struct store_price *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_price_vtbl;
    impl->ref = 1;
    if (base) WindowsCreateString( base, wcslen(base), &impl->base_price );
    if (price) WindowsCreateString( price, wcslen(price), &impl->price );
    if (currency) WindowsCreateString( currency, wcslen(currency), &impl->currency );
    WindowsCreateString( L"", 0, &impl->recurrence );
    return impl;
}

struct store_image
{
    const void *vtbl;
    LONG ref;
    IInspectable *uri;
    HSTRING tag;
    UINT32 width;
    UINT32 height;
    HSTRING caption;
};

static inline struct store_image *impl_from_store_image( void *iface )
{
    return (struct store_image *)iface;
}
static HRESULT WINAPI store_image_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreImage ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI store_image_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_store_image( iface )->ref );
}
static ULONG WINAPI store_image_Release( void *iface )
{
    struct store_image *impl = impl_from_store_image( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->uri) IInspectable_Release( impl->uri );
        WindowsDeleteString( impl->tag );
        WindowsDeleteString( impl->caption );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI store_image_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI store_image_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreImage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI store_image_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI store_image_get_Uri( void *iface, void **value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    *value = impl->uri;
    if (*value) IInspectable_AddRef( impl->uri );
    return S_OK;
}
static HRESULT WINAPI store_image_get_ImagePurposeTag( void *iface, HSTRING *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->tag, value );
}
static HRESULT WINAPI store_image_get_Width( void *iface, UINT32 *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    *value = impl->width;
    return S_OK;
}
static HRESULT WINAPI store_image_get_Height( void *iface, UINT32 *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    *value = impl->height;
    return S_OK;
}
static HRESULT WINAPI store_image_get_Caption( void *iface, HSTRING *value )
{
    struct store_image *impl = impl_from_store_image( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->caption, value );
}
static const void *store_image_vtbl[] =
{
    store_image_QueryInterface,
    store_image_AddRef,
    store_image_Release,
    store_image_GetIids,
    store_image_GetRuntimeClassName,
    store_image_GetTrustLevel,
    store_image_get_Uri,
    store_image_get_ImagePurposeTag,
    store_image_get_Width,
    store_image_get_Height,
    store_image_get_Caption,
};

static struct store_image *store_image_create( const WCHAR *uri_str, const WCHAR *tag, UINT32 w, UINT32 h, const WCHAR *caption )
{
    struct store_image *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_image_vtbl;
    impl->ref = 1;
    if (uri_str) impl->uri = create_uri( uri_str );
    if (tag) WindowsCreateString( tag, wcslen(tag), &impl->tag );
    impl->width = w;
    impl->height = h;
    if (caption) WindowsCreateString( caption, wcslen(caption), &impl->caption );
    return impl;
}

struct store_product
{
    const void *vtbl;
    LONG ref;
    HSTRING store_id;
    HSTRING language;
    HSTRING title;
    HSTRING description;
    HSTRING product_kind;
    boolean has_digital_download;
    boolean is_in_user_collection;
    struct store_price *price;
    HSTRING extended_json_data;
    HSTRING in_app_offer_token;
    IInspectable *images;
    IInspectable *keywords;
    IInspectable *skus;
    IInspectable *videos;
    IInspectable *link_uri;
};

static inline struct store_product *impl_from_store_product( void *iface )
{
    return (struct store_product *)iface;
}
static HRESULT WINAPI store_product_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProduct ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI store_product_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_store_product( iface )->ref );
}
static ULONG WINAPI store_product_Release( void *iface )
{
    struct store_product *impl = impl_from_store_product( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->store_id );
        WindowsDeleteString( impl->language );
        WindowsDeleteString( impl->title );
        WindowsDeleteString( impl->description );
        WindowsDeleteString( impl->product_kind );
        WindowsDeleteString( impl->extended_json_data );
        WindowsDeleteString( impl->in_app_offer_token );
        if (impl->price) store_price_Release( &impl->price->vtbl );
        if (impl->images) IInspectable_Release( impl->images );
        if (impl->keywords) IInspectable_Release( impl->keywords );
        if (impl->skus) IInspectable_Release( impl->skus );
        if (impl->videos) IInspectable_Release( impl->videos );
        if (impl->link_uri) IInspectable_Release( impl->link_uri );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI store_product_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI store_product_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreProduct";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI store_product_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI store_product_get_StoreId( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->store_id, value );
}
static HRESULT WINAPI store_product_get_Language( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->language, value );
}
static HRESULT WINAPI store_product_get_Title( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->title, value );
}
static HRESULT WINAPI store_product_get_Description( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->description, value );
}
static HRESULT WINAPI store_product_get_ProductKind( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->product_kind, value );
}
static HRESULT WINAPI store_product_get_HasDigitalDownload( void *iface, boolean *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->has_digital_download;
    return S_OK;
}
static HRESULT WINAPI store_product_get_Keywords( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->keywords;
    if (*value) IInspectable_AddRef( impl->keywords );
    return S_OK;
}
static HRESULT WINAPI store_product_get_Images( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->images;
    if (*value) IInspectable_AddRef( impl->images );
    return S_OK;
}
static HRESULT WINAPI store_product_get_Videos( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->videos;
    if (*value) IInspectable_AddRef( impl->videos );
    return S_OK;
}
static HRESULT WINAPI store_product_get_Skus( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->skus;
    if (*value) IInspectable_AddRef( impl->skus );
    return S_OK;
}
static HRESULT WINAPI store_product_get_IsInUserCollection( void *iface, boolean *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->is_in_user_collection;
    return S_OK;
}
static HRESULT WINAPI store_product_get_Price( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->price;
    if (*value) store_price_AddRef( &impl->price->vtbl );
    return S_OK;
}
static HRESULT WINAPI store_product_get_ExtendedJsonData( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->extended_json_data, value );
}
static HRESULT WINAPI store_product_get_LinkUri( void *iface, void **value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    *value = impl->link_uri;
    if (*value) IInspectable_AddRef( impl->link_uri );
    return S_OK;
}
static HRESULT WINAPI store_product_GetIsAnySkuInstalledAsync( void *iface, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI store_product_RequestPurchaseAsync( void *iface, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_product_RequestPurchaseWithPropertiesAsync( void *iface, void *props, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_product_get_InAppOfferToken( void *iface, HSTRING *value )
{
    struct store_product *impl = impl_from_store_product( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->in_app_offer_token, value );
}
static const void *store_product_vtbl[] =
{
    store_product_QueryInterface,
    store_product_AddRef,
    store_product_Release,
    store_product_GetIids,
    store_product_GetRuntimeClassName,
    store_product_GetTrustLevel,
    store_product_get_StoreId,
    store_product_get_Language,
    store_product_get_Title,
    store_product_get_Description,
    store_product_get_ProductKind,
    store_product_get_HasDigitalDownload,
    store_product_get_Keywords,
    store_product_get_Images,
    store_product_get_Videos,
    store_product_get_Skus,
    store_product_get_IsInUserCollection,
    store_product_get_Price,
    store_product_get_ExtendedJsonData,
    store_product_get_LinkUri,
    store_product_GetIsAnySkuInstalledAsync,
    store_product_RequestPurchaseAsync,
    store_product_RequestPurchaseWithPropertiesAsync,
    store_product_get_InAppOfferToken,
};

static struct store_product *store_product_create( const WCHAR *id, const WCHAR *title, const WCHAR *desc, const WCHAR *kind )
{
    struct store_product *impl = calloc( 1, sizeof(*impl) );
    struct store_image *img;
    IInspectable *images_arr[3];
    struct winrt_vector_view *vv;
    UINT32 img_count = 0;

    if (!impl) return NULL;
    impl->vtbl = store_product_vtbl;
    impl->ref = 1;

    if (id) WindowsCreateString( id, wcslen(id), &impl->store_id );
    WindowsCreateString( L"en-us", 5, &impl->language );
    if (title) WindowsCreateString( title, wcslen(title), &impl->title );
    if (desc) WindowsCreateString( desc, wcslen(desc), &impl->description );
    if (kind) WindowsCreateString( kind, wcslen(kind), &impl->product_kind );
    WindowsCreateString( L"{}", 2, &impl->extended_json_data );
    WindowsCreateString( L"", 0, &impl->in_app_offer_token );

    impl->has_digital_download = TRUE;
    impl->is_in_user_collection = TRUE;
    impl->price = store_price_create( L"Free", L"Free", L"USD" );
    impl->link_uri = create_uri( L"https://apps.microsoft.com" );

    img = store_image_create( L"ms-appx:///Assets/StoreLogo.png", L"Logo", 150, 150, L"Logo" );
    if (img) images_arr[img_count++] = (IInspectable *)&img->vtbl;
    img = store_image_create( L"ms-appx:///Assets/Square150x150Logo.png", L"Poster", 300, 300, L"Poster" );
    if (img) images_arr[img_count++] = (IInspectable *)&img->vtbl;
    img = store_image_create( L"ms-appx:///Assets/Square44x44Logo.png", L"Icon", 44, 44, L"Icon" );
    if (img) images_arr[img_count++] = (IInspectable *)&img->vtbl;

    vv = store_create_vector_view( images_arr, img_count, &IID_IVectorView_StoreImage, &IID_IIterable_StoreImage );
    impl->images = vv ? (IInspectable *)&vv->vtbl_vector : NULL;
    for (UINT32 i = 0; i < img_count; i++) store_image_Release( images_arr[i] );

    vv = store_create_vector_view( NULL, 0, &IID_IVectorView_HSTRING, &IID_IIterable_HSTRING );
    impl->keywords = vv ? (IInspectable *)&vv->vtbl_vector : NULL;
    vv = store_create_vector_view( NULL, 0, &IID_IVectorView_Object, &IID_IIterable_Object );
    impl->skus = vv ? (IInspectable *)&vv->vtbl_vector : NULL;
    vv = store_create_vector_view( NULL, 0, &IID_IVectorView_Object, &IID_IIterable_Object );
    impl->videos = vv ? (IInspectable *)&vv->vtbl_vector : NULL;

    return impl;
}

struct store_product_query_result
{
    const void *vtbl;
    LONG ref;
    IInspectable *products;
    HRESULT error_code;
};
static inline struct store_product_query_result *impl_from_product_query_result( void *iface )
{
    return (struct store_product_query_result *)iface;
}
static HRESULT WINAPI product_query_result_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProductQueryResult ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI product_query_result_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_product_query_result( iface )->ref );
}
static ULONG WINAPI product_query_result_Release( void *iface )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->products) IInspectable_Release( impl->products );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI product_query_result_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI product_query_result_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreProductQueryResult";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI product_query_result_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI product_query_result_get_Products( void *iface, void **value )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    if (!value) return E_POINTER;
    *value = impl->products;
    if (*value) IInspectable_AddRef( impl->products );
    return S_OK;
}
static HRESULT WINAPI product_query_result_get_ExtendedError( void *iface, HRESULT *value )
{
    struct store_product_query_result *impl = impl_from_product_query_result( iface );
    if (!value) return E_POINTER;
    *value = impl->error_code;
    return S_OK;
}
static const void *store_product_query_result_vtbl[] =
{
    product_query_result_QueryInterface,
    product_query_result_AddRef,
    product_query_result_Release,
    product_query_result_GetIids,
    product_query_result_GetRuntimeClassName,
    product_query_result_GetTrustLevel,
    product_query_result_get_Products,
    product_query_result_get_ExtendedError,
};

static struct store_product_query_result *store_product_query_result_create( void *map )
{
    struct store_product_query_result *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_product_query_result_vtbl;
    impl->ref = 1;
    impl->products = map;
    if (impl->products) IInspectable_AddRef( impl->products );
    impl->error_code = S_OK;
    return impl;
}

struct store_product_paged_query_result
{
    const void *vtbl;
    LONG ref;
    IInspectable *products;
    boolean has_more_results;
    HRESULT error_code;
};
static inline struct store_product_paged_query_result *impl_from_product_paged_query_result( void *iface )
{
    return (struct store_product_paged_query_result *)iface;
}
static HRESULT WINAPI product_paged_query_result_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProductPagedQueryResult ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI product_paged_query_result_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_product_paged_query_result( iface )->ref );
}
static ULONG WINAPI product_paged_query_result_Release( void *iface )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->products) IInspectable_Release( impl->products );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI product_paged_query_result_GetNextAsync( void *iface, void **op )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    return store_async_create( impl, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI product_paged_query_result_get_HasMoreResults( void *iface, boolean *val )
{
    struct store_product_paged_query_result *impl = impl_from_product_paged_query_result( iface );
    if (!val) return E_POINTER;
    *val = impl->has_more_results;
    return S_OK;
}
static const void *store_product_paged_query_result_vtbl[] =
{
    product_paged_query_result_QueryInterface,
    product_paged_query_result_AddRef,
    product_paged_query_result_Release,
    product_query_result_GetIids,
    product_query_result_GetRuntimeClassName,
    product_query_result_GetTrustLevel,
    product_query_result_get_Products,
    product_paged_query_result_get_HasMoreResults,
    product_query_result_get_ExtendedError,
    product_paged_query_result_GetNextAsync,
};

static struct store_product_paged_query_result *store_product_paged_query_result_create( void *map )
{
    struct store_product_paged_query_result *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_product_paged_query_result_vtbl;
    impl->ref = 1;
    impl->products = map;
    if (impl->products) IInspectable_AddRef( impl->products );
    impl->has_more_results = FALSE;
    impl->error_code = S_OK;
    return impl;
}

struct store_product_result
{
    const void *vtbl;
    LONG ref;
    struct store_product *product;
    HRESULT error_code;
};
static inline struct store_product_result *impl_from_product_result( void *iface )
{
    return (struct store_product_result *)iface;
}
static HRESULT WINAPI product_result_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreProductResult ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI product_result_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_product_result( iface )->ref );
}
static ULONG WINAPI product_result_Release( void *iface )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->product) store_product_Release( &impl->product->vtbl );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI product_result_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI product_result_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Services.Store.StoreProductResult";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI product_result_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI product_result_get_Product( void *iface, void **value )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    if (!value) return E_POINTER;
    *value = impl->product;
    if (*value) store_product_AddRef( &impl->product->vtbl );
    return S_OK;
}
static HRESULT WINAPI product_result_get_ExtendedError( void *iface, HRESULT *value )
{
    struct store_product_result *impl = impl_from_product_result( iface );
    if (!value) return E_POINTER;
    *value = impl->error_code;
    return S_OK;
}
static const void *store_product_result_vtbl[] =
{
    product_result_QueryInterface,
    product_result_AddRef,
    product_result_Release,
    product_result_GetIids,
    product_result_GetRuntimeClassName,
    product_result_GetTrustLevel,
    product_result_get_Product,
    product_result_get_ExtendedError,
};

static struct store_product_result *store_product_result_create( struct store_product *prod )
{
    struct store_product_result *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = store_product_result_vtbl;
    impl->ref = 1;
    impl->product = prod;
    if (impl->product) store_product_AddRef( &impl->product->vtbl );
    impl->error_code = S_OK;
    return impl;
}

/* --- StoreAppLicense --- */
static HRESULT WINAPI store_app_license_get_IsActive( void *iface, BOOL *value )
{
    if (value) *value = TRUE;
    return S_OK;
}
static HRESULT WINAPI store_app_license_get_IsTrial( void *iface, BOOL *value )
{
    if (value) *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_app_license_get_AddOnLicenses( void *iface, void **value )
{
    if (value) *value = store_create_empty_map_view();
    return S_OK;
}
static const void *store_app_license_vtbl[] =
{
    winappsdk_class_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_zero_out,              /* SkuStoreId */
    store_app_license_get_IsActive,  /* IsActive */
    store_app_license_get_IsTrial,   /* IsTrial */
    winappsdk_zero_out,              /* ExpirationDate */
    winappsdk_zero_out,              /* ExtendedJsonData */
    store_app_license_get_AddOnLicenses, /* AddOnLicenses */
    winappsdk_zero_out,              /* TrialTimeRemaining */
    winappsdk_zero_out,              /* IsTrialOwnedByThisUser */
    winappsdk_zero_out,              /* TrialUniqueId */
};
static struct winappsdk_class store_app_license_obj =
{
    store_app_license_vtbl, L"Windows.Services.Store.StoreAppLicense", 1
};

/* --- GetEntitlementResult --- */
static HRESULT WINAPI get_entitlement_result_get_Status( void *iface, UINT32 *status )
{
    if (status) *status = 0; /* Succeeded */
    return S_OK;
}
static const void *get_entitlement_result_vtbl[] =
{
    winappsdk_class_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    get_entitlement_result_get_Status,
};
static struct winappsdk_class get_entitlement_result_obj =
{
    get_entitlement_result_vtbl, L"Windows.ApplicationModel.Store.Preview.InstallControl.GetEntitlementResult", 1
};

/* --- AppInstallManager --- */
struct app_install_manager_impl
{
    const void *vtbl1; /* IAppInstallManager */
    const void *vtbl2; /* IAppInstallManager2 */
    const void *vtbl3; /* IAppInstallManager3 */
    const void *vtbl4; /* IAppInstallManager4 */
    const void *vtbl5; /* IAppInstallManager5 */
    const WCHAR *name;
    LONG ref;
};
static struct app_install_manager_impl app_install_mgr_instance;

static HRESULT WINAPI app_install_mgr_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
    struct app_install_manager_impl *mgr = &app_install_mgr_instance;

    TRACE( "AppInstallManager::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IAppInstallManager ))
        *out = (void *)&mgr->vtbl1;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager2 ))
        *out = (void *)&mgr->vtbl2;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager3 ))
        *out = (void *)&mgr->vtbl3;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager4 ))
        *out = (void *)&mgr->vtbl4;
    else if (IsEqualGUID( iid, &IID_IAppInstallManager5 ))
        *out = (void *)&mgr->vtbl5;
    else
        *out = (void *)&mgr->vtbl1;

    InterlockedIncrement( &mgr->ref );
    return S_OK;
}
static ULONG WINAPI app_install_mgr_AddRef( void *iface )
{
    return InterlockedIncrement( &app_install_mgr_instance.ref );
}
static ULONG WINAPI app_install_mgr_Release( void *iface )
{
    return InterlockedDecrement( &app_install_mgr_instance.ref );
}
static HRESULT WINAPI app_install_mgr_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0; if (i) *i = NULL; return S_OK;
}
static HRESULT WINAPI app_install_mgr_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager", 69, n );
}
static HRESULT WINAPI app_install_mgr_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}

/* Face 1 methods */
static HRESULT WINAPI app_install_mgr_get_AppInstallItems( void *iface, void **value )
{
    if (value) *value = store_create_empty_vector_view();
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_Cancel( void *iface, HSTRING prod ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_Pause( void *iface, HSTRING prod ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_Restart( void *iface, HSTRING prod ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_ItemCompleted( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_remove_ItemCompleted( void *iface, INT64 token ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_ItemStatusChanged( void *iface, void *handler, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_remove_ItemStatusChanged( void *iface, INT64 token ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_get_AutoUpdateSetting( void *iface, INT32 *setting )
{
    if (setting) *setting = 1; /* Enabled */
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_put_AutoUpdateSetting( void *iface, INT32 setting ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_get_AcquisitionIdentity( void *iface, HSTRING *value )
{
    if (value) WindowsCreateString( L"", 0, value );
    return S_OK;
}
static HRESULT WINAPI app_install_mgr_put_AcquisitionIdentity( void *iface, HSTRING value ) { return S_OK; }
static HRESULT WINAPI app_install_mgr_GetIsApplicableAsync( void *iface, HSTRING p, HSTRING s, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_StartAppInstallAsync( void *iface, HSTRING p, HSTRING s, BOOL r, BOOL f, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_UpdateAppByPackageFamilyNameAsync( void *iface, HSTRING p, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_SearchForUpdatesAsync( void *iface, HSTRING p, HSTRING s, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_SearchForAllUpdatesAsync( void *iface, void **op )
{
    return store_async_create( store_create_empty_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_IsStoreBlockedByPolicyAsync( void *iface, HSTRING n, HSTRING p, void **op )
{
    return store_async_create( NULL, TRUE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr_GetIsAppAllowedToInstallAsync( void *iface, HSTRING p, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}

static const void *app_install_manager_vtbl1[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr_get_AppInstallItems,
    app_install_mgr_Cancel,
    app_install_mgr_Pause,
    app_install_mgr_Restart,
    app_install_mgr_ItemCompleted,
    app_install_mgr_remove_ItemCompleted,
    app_install_mgr_ItemStatusChanged,
    app_install_mgr_remove_ItemStatusChanged,
    app_install_mgr_get_AutoUpdateSetting,
    app_install_mgr_put_AutoUpdateSetting,
    app_install_mgr_get_AcquisitionIdentity,
    app_install_mgr_put_AcquisitionIdentity,
    app_install_mgr_GetIsApplicableAsync,
    app_install_mgr_StartAppInstallAsync,
    app_install_mgr_UpdateAppByPackageFamilyNameAsync,
    app_install_mgr_SearchForUpdatesAsync,
    app_install_mgr_SearchForAllUpdatesAsync,
    app_install_mgr_IsStoreBlockedByPolicyAsync,
    app_install_mgr_GetIsAppAllowedToInstallAsync,
};

/* Face 2 methods */
static HRESULT WINAPI app_install_mgr2_StartAppInstallWithTelemetryAsync( void *iface, HSTRING p, HSTRING s, BOOL r, BOOL f, HSTRING c, HSTRING b, HSTRING cv, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_UpdateAppByPackageFamilyNameWithTelemetryAsync( void *iface, HSTRING p, HSTRING cv, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_SearchForUpdatesWithTelemetryAsync( void *iface, HSTRING p, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_SearchForAllUpdatesWithTelemetryAsync( void *iface, HSTRING cv, void **op )
{
    return store_async_create( store_create_empty_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_GetIsAppAllowedToInstallWithTelemetryAsync( void *iface, HSTRING p, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr2_CancelWithTelemetry( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }
static HRESULT WINAPI app_install_mgr2_PauseWithTelemetry( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }
static HRESULT WINAPI app_install_mgr2_RestartWithTelemetry( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }

static const void *app_install_manager_vtbl2[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr2_StartAppInstallWithTelemetryAsync,
    app_install_mgr2_UpdateAppByPackageFamilyNameWithTelemetryAsync,
    app_install_mgr2_SearchForUpdatesWithTelemetryAsync,
    app_install_mgr2_SearchForAllUpdatesWithTelemetryAsync,
    app_install_mgr2_GetIsAppAllowedToInstallWithTelemetryAsync,
    app_install_mgr2_CancelWithTelemetry,
    app_install_mgr2_PauseWithTelemetry,
    app_install_mgr2_RestartWithTelemetry,
};

/* Face 3 methods */
static HRESULT WINAPI app_install_mgr3_StartProductInstallAsync( void *iface, HSTRING p, HSTRING cat, HSTRING fl, HSTRING cl, BOOL r, BOOL f, HSTRING cv, void *vol, void **op )
{
    return store_async_create( store_create_empty_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_StartProductInstallForUserAsync( void *iface, void *u, HSTRING p, HSTRING cat, HSTRING fl, HSTRING cl, BOOL r, BOOL f, HSTRING cv, void *vol, void **op )
{
    return store_async_create( store_create_empty_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_SearchForAllUpdatesForUserAsync( void *iface, void *u, HSTRING cv, void **op )
{
    return store_async_create( store_create_empty_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_GetIsAppAllowedToInstallForUserAsync( void *iface, void *u, HSTRING p, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_GetIsApplicableForUserAsync( void *iface, void *u, HSTRING p, HSTRING s, void **op )
{
    return store_async_create( NULL, TRUE, TRUE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr3_MoveToFrontOfDownloadQueue( void *iface, HSTRING p, HSTRING cv ) { return S_OK; }

static const void *app_install_manager_vtbl3[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr3_StartProductInstallAsync,
    app_install_mgr3_StartProductInstallForUserAsync,
    app_install_mgr3_SearchForAllUpdatesForUserAsync,
    app_install_mgr3_GetIsAppAllowedToInstallForUserAsync,
    app_install_mgr3_GetIsApplicableForUserAsync,
    app_install_mgr3_MoveToFrontOfDownloadQueue,
};

/* Face 4 methods */
static HRESULT WINAPI app_install_mgr4_GetFreeUserEntitlementAsync( void *iface, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( &get_entitlement_result_obj, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr4_GetFreeUserEntitlementForUserAsync( void *iface, void *u, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( &get_entitlement_result_obj, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI app_install_mgr4_GetFreeDeviceEntitlementAsync( void *iface, HSTRING s, HSTRING c, HSTRING cv, void **op )
{
    return store_async_create( &get_entitlement_result_obj, FALSE, FALSE, S_OK, op );
}

static const void *app_install_manager_vtbl4[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr4_GetFreeUserEntitlementAsync,
    app_install_mgr4_GetFreeUserEntitlementForUserAsync,
    app_install_mgr4_GetFreeDeviceEntitlementAsync,
};

/* Face 5 methods */
static HRESULT WINAPI app_install_mgr5_get_AppInstallItemsWithGroupSupport( void *iface, void **value )
{
    if (value) *value = store_create_empty_vector_view();
    return S_OK;
}

static const void *app_install_manager_vtbl5[] =
{
    app_install_mgr_QueryInterface,
    app_install_mgr_AddRef,
    app_install_mgr_Release,
    app_install_mgr_GetIids,
    app_install_mgr_GetRuntimeClassName,
    app_install_mgr_GetTrustLevel,
    app_install_mgr5_get_AppInstallItemsWithGroupSupport,
};

static struct app_install_manager_impl app_install_mgr_instance =
{
    app_install_manager_vtbl1,
    app_install_manager_vtbl2,
    app_install_manager_vtbl3,
    app_install_manager_vtbl4,
    app_install_manager_vtbl5,
    L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager",
    1
};

static HRESULT WINAPI app_install_manager_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    if (instance) *instance = (IInspectable *)&app_install_mgr_instance.vtbl1;
    return S_OK;
}
static const void *app_install_manager_custom_factory_vtbl[] =
{
    winappsdk_factory_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_factory_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    app_install_manager_factory_ActivateInstance,
};
static struct winappsdk_factory app_install_manager_custom_factory =
{
    app_install_manager_custom_factory_vtbl, (struct winappsdk_class *)&app_install_mgr_instance, 1
};

/* --- StoreContext --- */
static HRESULT WINAPI store_context_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    TRACE( "StoreContext::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStoreContext ) ||
        IsEqualGUID( iid, &IID_IStoreContext2 ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI store_ctx_get_User( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI store_ctx_add_OfflineLicensesChanged( void *iface, void *h, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI store_ctx_remove_OfflineLicensesChanged( void *iface, INT64 token ) { return S_OK; }
static HRESULT WINAPI store_ctx_GetCustomerPurchaseIdAsync( void *iface, HSTRING st, HSTRING pu, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetCustomerCollectionsIdAsync( void *iface, HSTRING st, HSTRING pu, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetAppLicenseAsync( void *iface, void **op )
{
    return store_async_create( &store_app_license_obj, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetStoreProductForCurrentAppAsync( void *iface, void **op )
{
    WCHAR buf[MAX_PATH];
    const WCHAR *pkg_id = L"9WZDNCRFJBMP";
    struct store_product *prod;
    struct store_product_result *res;

    if (GetEnvironmentVariableW( L"WINE_PACKAGE_NAME", buf, ARRAY_SIZE(buf) ) ||
        package_get_reg_str( L"PackageName", buf, ARRAY_SIZE(buf) ))
        pkg_id = buf;

    prod = store_product_create( pkg_id, L"Microsoft Store", L"Microsoft Store", L"Application" );
    res = store_product_result_create( prod );
    if (prod) store_product_Release( &prod->vtbl );
    return store_async_create( res, FALSE, FALSE, S_OK, op );
}
static void get_app_store_products( const WCHAR *req_id, HSTRING *keys, IInspectable **vals, UINT32 max_count, UINT32 *actual_count )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR subkey[512], name[MAX_PATH], family[MAX_PATH], title[MAX_PATH];
    HKEY pkgs_key, key;
    DWORD index = 0, name_len, type, size, framework;
    UINT32 count = 0;
    SIZE_T r;

    if (req_id)
    {
        struct store_product *prod = store_product_create( req_id, req_id, req_id, L"Application" );
        if (prod && count < max_count)
        {
            WindowsCreateString( req_id, wcslen(req_id), &keys[count] );
            vals[count] = (IInspectable *)&prod->vtbl;
            count++;
        }
        *actual_count = count;
        return;
    }

    for (r = 0; r < ARRAY_SIZE(roots) && count < max_count; r++)
    {
        lstrcpyW( subkey, appmodel_reg_key );
        lstrcatW( subkey, L"\\Packages" );
        if (RegOpenKeyExW( roots[r], subkey, 0, KEY_READ, &pkgs_key )) continue;

        index = 0;
        name_len = ARRAY_SIZE(name);
        while (!RegEnumKeyExW( pkgs_key, index++, name, &name_len, NULL, NULL, NULL, NULL ) && count < max_count)
        {
            if (!RegOpenKeyExW( pkgs_key, name, 0, KEY_READ, &key ))
            {
                framework = 0;
                size = sizeof(framework);
                RegQueryValueExW( key, L"Framework", 0, &type, (BYTE *)&framework, &size );

                if (!framework)
                {
                    WCHAR *underscore;
                    const WCHAR *p = name;
                    family[0] = 0;
                    size = sizeof(family);
                    RegQueryValueExW( key, L"PackageFamilyName", 0, &type, (BYTE *)family, &size );

                    if (!wcsncmp( p, L"Microsoft.", 10 )) p += 10;
                    lstrcpynW( title, p, ARRAY_SIZE(title) );
                    underscore = wcschr( title, '_' );
                    if (underscore) *underscore = 0;

                    if (wcslen(title) > 0)
                    {
                        const WCHAR *id_str = family[0] ? family : name;
                        struct store_product *prod = store_product_create( id_str, title, title, L"Application" );
                        if (prod)
                        {
                            WindowsCreateString( id_str, wcslen(id_str), &keys[count] );
                            vals[count] = (IInspectable *)&prod->vtbl;
                            count++;
                        }
                    }
                }
                RegCloseKey( key );
            }
            name_len = ARRAY_SIZE(name);
        }
        RegCloseKey( pkgs_key );
    }

    if (!count)
    {
        WCHAR buf[MAX_PATH];
        const WCHAR *pkg_id = L"Microsoft.WindowsStore_8wekyb3d8bbwe";
        struct store_product *prod;
        if (GetEnvironmentVariableW( L"WINE_PACKAGE_NAME", buf, ARRAY_SIZE(buf) ) ||
            package_get_reg_str( L"PackageName", buf, ARRAY_SIZE(buf) ))
            pkg_id = buf;
        prod = store_product_create( pkg_id, L"Microsoft Store", L"Microsoft Store", L"Application" );
        if (prod)
        {
            WindowsCreateString( pkg_id, wcslen(pkg_id), &keys[count] );
            vals[count] = (IInspectable *)&prod->vtbl;
            count++;
        }
    }

    *actual_count = count;
}

static HRESULT WINAPI store_ctx_GetStoreProductsAsync( void *iface, void *pk, void *si, void **op )
{
    struct winrt_map_view *map;
    struct store_product_query_result *res;
    HSTRING keys[16];
    IInspectable *vals[16];
    UINT32 count = 0;

    if (si)
    {
        void *iter_unk = NULL;
        void **vtbl = *(void ***)si;
        HRESULT (WINAPI *pFirst)(void *, void **) = (void *)vtbl[6];
        if (SUCCEEDED(pFirst( si, &iter_unk )) && iter_unk)
        {
            void **iter_vtbl = *(void ***)iter_unk;
            HRESULT (WINAPI *pHasCurrent)(void *, boolean *) = (void *)iter_vtbl[7];
            HRESULT (WINAPI *pCurrent)(void *, HSTRING *) = (void *)iter_vtbl[6];
            HRESULT (WINAPI *pMoveNext)(void *, boolean *) = (void *)iter_vtbl[8];
            ULONG (WINAPI *pRelease)(void *) = (void *)iter_vtbl[2];
            boolean has = FALSE;

            if (SUCCEEDED(pHasCurrent( iter_unk, &has )))
            {
                while (has && count < ARRAY_SIZE(keys))
                {
                    HSTRING cur_id = NULL;
                    if (SUCCEEDED(pCurrent( iter_unk, &cur_id )) && cur_id)
                    {
                        const WCHAR *raw_id = WindowsGetStringRawBuffer( cur_id, NULL );
                        UINT32 added = 0;
                        get_app_store_products( raw_id, &keys[count], &vals[count], 1, &added );
                        count += added;
                    }
                    if (FAILED(pMoveNext( iter_unk, &has ))) break;
                }
            }
            pRelease( iter_unk );
        }
    }

    if (!count)
    {
        get_app_store_products( NULL, keys, vals, ARRAY_SIZE(keys), &count );
    }

    map = store_create_map_view( keys, vals, count, &IID_IMapView_StoreProduct, &IID_IIterable_StoreProductKeyValuePair, &IID_IKeyValuePair_StoreProduct );
    for (UINT32 i = 0; i < count; i++)
    {
        WindowsDeleteString( keys[i] );
        IInspectable_Release( vals[i] );
    }

    res = store_product_query_result_create( (void *)map );
    if (map) map_view_Release( &map->vtbl_map );
    return store_async_create( res, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetAssociatedStoreProductsAsync( void *iface, void *pk, void **op )
{
    return store_ctx_GetStoreProductsAsync( iface, pk, NULL, op );
}
static HRESULT WINAPI store_ctx_GetAssociatedStoreProductsWithPagingAsync( void *iface, void *pk, UINT32 m, void **op )
{
    struct winrt_map_view *map;
    struct store_product_paged_query_result *res;
    HSTRING keys[16];
    IInspectable *vals[16];
    UINT32 count = 0;

    get_app_store_products( NULL, keys, vals, ARRAY_SIZE(keys), &count );

    map = store_create_map_view( keys, vals, count, &IID_IMapView_StoreProduct, &IID_IIterable_StoreProductKeyValuePair, &IID_IKeyValuePair_StoreProduct );
    for (UINT32 i = 0; i < count; i++)
    {
        WindowsDeleteString( keys[i] );
        IInspectable_Release( vals[i] );
    }

    res = store_product_paged_query_result_create( (void *)map );
    if (map) map_view_Release( &map->vtbl_map );
    return store_async_create( res, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetUserCollectionAsync( void *iface, void *pk, void **op )
{
    return store_ctx_GetStoreProductsAsync( iface, pk, NULL, op );
}
static HRESULT WINAPI store_ctx_GetUserCollectionWithPagingAsync( void *iface, void *pk, UINT32 m, void **op )
{
    return store_ctx_GetAssociatedStoreProductsWithPagingAsync( iface, pk, m, op );
}
static HRESULT WINAPI store_ctx_ReportConsumableFulfillmentAsync( void *iface, HSTRING id, UINT32 q, GUID t, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetConsumableBalanceRemainingAsync( void *iface, HSTRING id, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_AcquireStoreLicenseForOptionalPackageAsync( void *iface, void *pkg, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestPurchaseAsync( void *iface, HSTRING id, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestPurchaseWithPropertiesAsync( void *iface, HSTRING id, void *props, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_GetAppAndOptionalStorePackageUpdatesAsync( void *iface, void **op )
{
    return store_async_create( store_create_empty_vector_view(), FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestDownloadStorePackageUpdatesAsync( void *iface, void *upd, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestDownloadAndInstallStorePackageUpdatesAsync( void *iface, void *upd, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_RequestDownloadAndInstallStorePackagesAsync( void *iface, void *ids, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_ctx_FindStoreProductForPackageAsync( void *iface, void *pk, void *pkg, void **op )
{
    return store_ctx_GetStoreProductForCurrentAppAsync( iface, op );
}

static const void *store_context_custom_vtbl[] =
{
    store_context_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    store_ctx_get_User,
    store_ctx_add_OfflineLicensesChanged,
    store_ctx_remove_OfflineLicensesChanged,
    store_ctx_GetCustomerPurchaseIdAsync,
    store_ctx_GetCustomerCollectionsIdAsync,
    store_ctx_GetAppLicenseAsync,
    store_ctx_GetStoreProductForCurrentAppAsync,
    store_ctx_GetStoreProductsAsync,
    store_ctx_GetAssociatedStoreProductsAsync,
    store_ctx_GetAssociatedStoreProductsWithPagingAsync,
    store_ctx_GetUserCollectionAsync,
    store_ctx_GetUserCollectionWithPagingAsync,
    store_ctx_ReportConsumableFulfillmentAsync,
    store_ctx_GetConsumableBalanceRemainingAsync,
    store_ctx_AcquireStoreLicenseForOptionalPackageAsync,
    store_ctx_RequestPurchaseAsync,
    store_ctx_RequestPurchaseWithPropertiesAsync,
    store_ctx_GetAppAndOptionalStorePackageUpdatesAsync,
    store_ctx_RequestDownloadStorePackageUpdatesAsync,
    store_ctx_RequestDownloadAndInstallStorePackageUpdatesAsync,
    store_ctx_RequestDownloadAndInstallStorePackagesAsync,
    store_ctx_FindStoreProductForPackageAsync,
};
static struct winappsdk_class store_context_custom_obj =
{
    store_context_custom_vtbl, L"Windows.Services.Store.StoreContext", 1
};

static HRESULT WINAPI store_context_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    TRACE( "StoreContextFactory::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IStoreContextStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = iface;
    return S_OK;
}
static HRESULT WINAPI store_context_factory_GetDefault( void *iface, void **context )
{
    if (context) *context = &store_context_custom_obj;
    return S_OK;
}
static HRESULT WINAPI store_context_factory_GetForUser( void *iface, void *user, void **context )
{
    if (context) *context = &store_context_custom_obj;
    return S_OK;
}
static const void *store_context_custom_factory_vtbl[] =
{
    store_context_factory_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_factory_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    store_context_factory_GetDefault,
    store_context_factory_GetForUser,
};
static struct winappsdk_factory store_context_custom_factory =
{
    store_context_custom_factory_vtbl, &store_context_custom_obj, 1
};

/* --- StoreConfiguration --- */
struct store_configuration_factory_impl
{
    const void *vtbl1; /* IStoreConfigurationStatics */
    const void *vtbl2; /* IStoreConfigurationStatics2 */
    const void *vtbl3; /* IStoreConfigurationStatics3 */
    const void *vtbl4; /* IStoreConfigurationStatics4 */
    const WCHAR *name;
    LONG ref;
};
static struct store_configuration_factory_impl store_cfg_factory_instance;

static HRESULT WINAPI store_cfg_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };
    struct store_configuration_factory_impl *cfg = &store_cfg_factory_instance;

    TRACE( "StoreConfigurationFactory::QI iid=%s\n", debugstr_guid(iid) );

    if (!out) return E_INVALIDARG;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics ))
        *out = (void *)&cfg->vtbl1;
    else if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics2 ))
        *out = (void *)&cfg->vtbl2;
    else if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics3 ))
        *out = (void *)&cfg->vtbl3;
    else if (IsEqualGUID( iid, &IID_IStoreConfigurationStatics4 ))
        *out = (void *)&cfg->vtbl4;
    else
        *out = (void *)&cfg->vtbl1;

    InterlockedIncrement( &cfg->ref );
    return S_OK;
}
static ULONG WINAPI store_cfg_AddRef( void *iface ) { return InterlockedIncrement( &store_cfg_factory_instance.ref ); }
static ULONG WINAPI store_cfg_Release( void *iface ) { return InterlockedDecrement( &store_cfg_factory_instance.ref ); }
static HRESULT WINAPI store_cfg_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI store_cfg_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Store.Preview.StoreConfiguration", 55, n );
}
static HRESULT WINAPI store_cfg_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

/* Statics 1 */
static HRESULT WINAPI store_cfg1_SetSystemConfiguration( void *iface, HSTRING m, HSTRING mod, void *exp, HSTRING d ) { return S_OK; }
static HRESULT WINAPI store_cfg1_SetMobileOperatorConfiguration( void *iface, HSTRING m, UINT32 a, UINT32 u ) { return S_OK; }
static HRESULT WINAPI store_cfg1_SetSubSystemConfiguration( void *iface, HSTRING m, HSTRING mod, void *exp, HSTRING d ) { return S_OK; }
static HRESULT WINAPI store_cfg1_IsAnyPackageInstalledWithId( void *iface, HSTRING id, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg1_IsPackageInstalledWithId( void *iface, HSTRING id, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg1_GetStoreWebSelection( void *iface, void **val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg1_SetStoreWebSelection( void *iface, UINT32 val ) { return S_OK; }

static const void *store_cfg_vtbl1[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg1_SetSystemConfiguration,
    store_cfg1_SetMobileOperatorConfiguration,
    store_cfg1_SetSubSystemConfiguration,
    store_cfg1_IsAnyPackageInstalledWithId,
    store_cfg1_IsPackageInstalledWithId,
    store_cfg1_GetStoreWebSelection,
    store_cfg1_SetStoreWebSelection,
};

/* Statics 2 */
static HRESULT WINAPI store_cfg2_get_PurchasePromptingPolicy( void *iface, void **val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg2_put_PurchasePromptingPolicy( void *iface, void *val ) { return S_OK; }

static const void *store_cfg_vtbl2[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg2_get_PurchasePromptingPolicy,
    store_cfg2_put_PurchasePromptingPolicy,
};

/* Statics 3 */
static HRESULT WINAPI store_cfg3_HasStoreWebAccount( void *iface, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_HasStoreWebAccountForUser( void *iface, void *u, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_GetStoreLogDataAsync( void *iface, UINT32 opt, void **op )
{
    return store_async_create( NULL, FALSE, FALSE, S_OK, op );
}
static HRESULT WINAPI store_cfg3_SetStoreWebAccountIdForUser( void *iface, void *u, HSTRING id ) { return S_OK; }
static HRESULT WINAPI store_cfg3_IsStoreWebAccountIdForUser( void *iface, void *u, HSTRING id, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_GetPurchasePromptingPolicyForUser( void *iface, void *u, void **val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg3_SetPurchasePromptingPolicyForUser( void *iface, void *u, void *val ) { return S_OK; }

static const void *store_cfg_vtbl3[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg3_HasStoreWebAccount,
    store_cfg3_HasStoreWebAccountForUser,
    store_cfg3_GetStoreLogDataAsync,
    store_cfg3_SetStoreWebAccountIdForUser,
    store_cfg3_IsStoreWebAccountIdForUser,
    store_cfg3_GetPurchasePromptingPolicyForUser,
    store_cfg3_SetPurchasePromptingPolicyForUser,
};

/* Statics 4 */
static HRESULT WINAPI store_cfg4_GetStoreWebAccountId( void *iface, HSTRING *val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg4_GetStoreWebAccountIdForUser( void *iface, void *u, HSTRING *val )
{
    if (val) *val = NULL;
    return S_OK;
}
static HRESULT WINAPI store_cfg4_SetEnterpriseStoreWebAccountId( void *iface, HSTRING id ) { return S_OK; }

static const void *store_cfg_vtbl4[] =
{
    store_cfg_QueryInterface,
    store_cfg_AddRef,
    store_cfg_Release,
    store_cfg_GetIids,
    store_cfg_GetRuntimeClassName,
    store_cfg_GetTrustLevel,
    store_cfg4_GetStoreWebAccountId,
    store_cfg4_GetStoreWebAccountIdForUser,
    store_cfg4_SetEnterpriseStoreWebAccountId,
};

static struct store_configuration_factory_impl store_cfg_factory_instance =
{
    store_cfg_vtbl1,
    store_cfg_vtbl2,
    store_cfg_vtbl3,
    store_cfg_vtbl4,
    L"Windows.ApplicationModel.Store.Preview.StoreConfiguration",
    1
};

WINAPPSDK_CLASS_V( deployment_result, deployment_result_vtbl, "Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentResult" );
WINAPPSDK_CLASS_V( package_dependency_context, package_dependency_context_vtbl, "Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependencyContext" );
WINAPPSDK_CLASS_V( add_package_dependency_options, add_package_dependency_options_vtbl, "Microsoft.Windows.ApplicationModel.DynamicDependency.AddPackageDependencyOptions" );

static struct package_dependency package_dependency_obj =
{
    { package_dependency_vtbl, PACKAGE_DEPENDENCY_CLASS, 1 }, L"Microsoft.WinAppSDK.EmojiData"
};
static struct winappsdk_factory package_dependency_factory =
{
    package_dependency_factory_vtbl, &package_dependency_obj.cls, 1
};

static const struct
{
    const WCHAR *name;
    IActivationFactory *factory;
}
winappsdk_classes[] =
{
    { L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentManager",
      (IActivationFactory *)&deployment_manager_factory },
    { L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentInitializeOptions",
      (IActivationFactory *)&deployment_init_options_factory },
    { L"Microsoft.Windows.ApplicationModel.WindowsAppRuntime.DeploymentResult",
      (IActivationFactory *)&deployment_result_factory },
    { L"Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependency",
      (IActivationFactory *)&package_dependency_factory },
    { L"Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependencyContext",
      (IActivationFactory *)&package_dependency_context_factory },
    { L"Microsoft.Windows.ApplicationModel.DynamicDependency.AddPackageDependencyOptions",
      (IActivationFactory *)&add_package_dependency_options_factory },
    { L"Microsoft.Windows.ApplicationModel.DynamicDependency.CreatePackageDependencyOptions",
      (IActivationFactory *)&create_package_dependency_options_factory },
    { L"Microsoft.Windows.ApplicationModel.DynamicDependency.PackageDependencyRank",
      (IActivationFactory *)&package_dependency_rank_factory },
    { L"Microsoft.Windows.System.EnvironmentManager",
      (IActivationFactory *)&environment_manager_factory },
    { L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager",
      (IActivationFactory *)&app_install_manager_custom_factory },
    { L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallOptions",
      (IActivationFactory *)&app_install_options_factory },
    { L"Windows.ApplicationModel.Store.Preview.InstallControl.AppUpdateOptions",
      (IActivationFactory *)&app_update_options_factory },
    { L"Windows.ApplicationModel.Store.Preview.StoreConfiguration",
      (IActivationFactory *)&store_cfg_factory_instance.vtbl1 },
    { L"Windows.ApplicationModel.Store.Preview.StoreSystemFeature",
      (IActivationFactory *)&store_system_feature_factory },
    { L"Windows.ApplicationModel.Store.Preview.WebAuthenticationCoreManagerHelper",
      (IActivationFactory *)&web_auth_core_helper_factory },
    { L"Windows.ApplicationModel.Store.LicenseManagement.LicenseManager",
      (IActivationFactory *)&license_manager_factory },
    { L"Windows.Services.Store.StoreContext",
      (IActivationFactory *)&store_context_custom_factory },
};

/* Windows.ApplicationModel.UserActivities.UserActivityRequestManager */
static HRESULT WINAPI user_activity_manager_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    if (IsEqualGUID( iid, &IID_IReferenceTracker ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}
static ULONG WINAPI user_activity_manager_AddRef( void *iface ) { return 2; }
static ULONG WINAPI user_activity_manager_Release( void *iface ) { return 1; }
static HRESULT WINAPI user_activity_manager_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; return S_OK; }
static HRESULT WINAPI user_activity_manager_GetRuntimeClassName( void *iface, HSTRING *n )
{ return WindowsCreateString( L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager", 65, n ); }
static HRESULT WINAPI user_activity_manager_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI user_activity_manager_add_Requested( void *iface, void *h, EventRegistrationToken *t )
{ if (t) t->value = 1; return S_OK; }
static HRESULT WINAPI user_activity_manager_remove_Requested( void *iface, EventRegistrationToken t ) { return S_OK; }

static const void *user_activity_manager_vtbl[] =
{
    user_activity_manager_QueryInterface,
    user_activity_manager_AddRef,
    user_activity_manager_Release,
    user_activity_manager_GetIids,
    user_activity_manager_GetRuntimeClassName,
    user_activity_manager_GetTrustLevel,
    user_activity_manager_add_Requested,    /* slot 6 */
    user_activity_manager_remove_Requested, /* slot 7 */
    user_activity_manager_add_Requested,
    user_activity_manager_remove_Requested,
};
static struct { const void **vtbl; } user_activity_manager_obj = { user_activity_manager_vtbl };

static HRESULT WINAPI user_activity_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    if (IsEqualGUID( iid, &IID_IReferenceTracker ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}
static ULONG WINAPI user_activity_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI user_activity_statics_Release( void *iface ) { return 1; }
static HRESULT WINAPI user_activity_statics_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; return S_OK; }
static HRESULT WINAPI user_activity_statics_GetRuntimeClassName( void *iface, HSTRING *n )
{ return WindowsCreateString( L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager", 65, n ); }
static HRESULT WINAPI user_activity_statics_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI user_activity_statics_GetForCurrentView( void *iface, void **out )
{
    TRACE( "iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    *out = &user_activity_manager_obj;
    return S_OK;
}

static const void *user_activity_statics_vtbl[] =
{
    user_activity_statics_QueryInterface,
    user_activity_statics_AddRef,
    user_activity_statics_Release,
    user_activity_statics_GetIids,
    user_activity_statics_GetRuntimeClassName,
    user_activity_statics_GetTrustLevel,
    user_activity_statics_GetForCurrentView, /* slot 6 */
};
static struct { const void **vtbl; } user_activity_statics_obj = { user_activity_statics_vtbl };
static IActivationFactory *user_activity_factory = (IActivationFactory *)&user_activity_statics_obj;

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_Package ))
        IActivationFactory_QueryInterface( package_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_AppInfo ))
        IActivationFactory_QueryInterface( appinfo_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_ApplicationModel_DesignMode ))
        IActivationFactory_QueryInterface( design_mode_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.StartupTask" ))
        *factory = (IActivationFactory *)&startup_task_factory;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager" ))
        *factory = user_activity_factory;
    else if (!wcsncmp( buffer, L"Windows.ApplicationModel.Calls.", 31 ))
    {
        HMODULE mod = LoadLibraryW( L"windows.applicationmodel.calls.dll" );
        if (mod)
        {
            HRESULT (WINAPI *pfn)(HSTRING, void **) = (void *)GetProcAddress( mod, "DllGetActivationFactory" );
            if (pfn) return pfn( classid, (void **)factory );
        }
    }
    else
    {
        SIZE_T i;
        for (i = 0; i < ARRAYSIZE( winappsdk_classes ); i++)
            if (!wcscmp( buffer, winappsdk_classes[i].name ))
            {
                IActivationFactory_QueryInterface( winappsdk_classes[i].factory,
                                                   &IID_IActivationFactory, (void **)factory );
                break;
            }
        if (!*factory && (!wcsncmp( buffer, L"Windows.ApplicationModel.Store.", 31 ) ||
                          !wcsncmp( buffer, L"Windows.Services.Store.", 23 )))
        {
            IActivationFactory_QueryInterface( (IActivationFactory *)&app_install_manager_custom_factory,
                                               &IID_IActivationFactory, (void **)factory );
        }
    }

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
