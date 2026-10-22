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
#include "private.h"

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
    else if (!wcsncmp( buffer, L"Windows.ApplicationModel.Calls.", 31 ))
    {
        HMODULE mod = LoadLibraryW( L"windows.applicationmodel.calls.dll" );
        if (mod)
        {
            HRESULT (WINAPI *pfn)(HSTRING, void **) = (void *)GetProcAddress( mod, "DllGetActivationFactory" );
            if (pfn) return pfn( classid, (void **)factory );
        }
    }

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
