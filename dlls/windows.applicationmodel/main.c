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
#include "winhttp.h"

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
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI startup_task_async_AddRef( void *iface ) { return 2; }
static ULONG WINAPI startup_task_async_Release( void *iface ) { return 1; }
static HRESULT WINAPI startup_task_async_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI startup_task_async_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation";
    return WindowsCreateString( cls, ARRAYSIZE(cls) - 1, n );
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
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI startup_task_AddRef( void *iface ) { return 2; }
static ULONG WINAPI startup_task_Release( void *iface ) { return 1; }
static HRESULT WINAPI startup_task_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI startup_task_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.ApplicationModel.StartupTask";
    return WindowsCreateString( cls, ARRAYSIZE(cls) - 1, n );
}
static HRESULT WINAPI startup_task_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI startup_task_RequestEnableAsync( void *iface, void **op )
{
    if (!op) return E_POINTER;
    *op = &startup_task_async_inst;
    startup_task_async_AddRef( *op );
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
    WCHAR exe_buf[MAX_PATH], *dot;
    const WCHAR *base;
    DWORD len;

    if (!id) return E_POINTER;
    len = GetModuleFileNameW( NULL, exe_buf, ARRAY_SIZE(exe_buf) );
    if (len && len < ARRAY_SIZE(exe_buf))
    {
        base = wcsrchr( exe_buf, '\\' );
        base = base ? base + 1 : exe_buf;
        if ((dot = wcsrchr( (WCHAR *)base, '.' ))) *dot = 0;
        if (base[0]) return WindowsCreateString( base, wcslen( base ), id );
    }
    return WindowsCreateString( L"StartupTask", 11, id );
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
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI startup_task_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI startup_task_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI startup_task_factory_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI startup_task_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.ApplicationModel.StartupTask";
    return WindowsCreateString( cls, ARRAYSIZE(cls) - 1, n );
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

HRESULT WINAPI winappsdk_class_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}

HRESULT WINAPI winappsdk_QueryInterface( void *iface, REFIID iid, void **out )
{
    return winappsdk_class_QueryInterface( iface, iid, out );
}

ULONG WINAPI winappsdk_AddRef( void *iface ) { return 2; }
ULONG WINAPI winappsdk_Release( void *iface ) { return 1; }
HRESULT WINAPI winappsdk_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
HRESULT WINAPI winappsdk_GetRuntimeClassName( void *iface, HSTRING *n )
{
    struct winappsdk_class *cls = iface;
    return WindowsCreateString( cls->name, lstrlenW( cls->name ), n );
}
HRESULT WINAPI winappsdk_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
HRESULT WINAPI winappsdk_zero_out( void *iface, void *out )
{
    if (out) *(void **)out = NULL;
    return S_OK;
}
const void *winappsdk_class_vtbl[] =
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

HRESULT WINAPI winappsdk_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerManager =
        { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x39, 0x35, 0xe2, 0x37, 0xf3 } };

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerManager ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}

HRESULT WINAPI winappsdk_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    struct winappsdk_factory *f = iface;
    if (!instance) return E_POINTER;
    *instance = (IInspectable *)f->obj;
    return S_OK;
}
HRESULT WINAPI winappsdk_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    struct winappsdk_factory *f = iface;
    return WindowsCreateString( f->obj->name, lstrlenW( f->obj->name ), n );
}

const void *winappsdk_factory_vtbl[] =
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
    if (!out) return E_POINTER;
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
static HRESULT WINAPI user_activity_manager_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI user_activity_manager_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager";
    return WindowsCreateString( cls, ARRAYSIZE(cls) - 1, n );
}
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
static HRESULT WINAPI user_activity_statics_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI user_activity_statics_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager";
    return WindowsCreateString( cls, ARRAYSIZE(cls) - 1, n );
}
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

/* Windows.ApplicationModel.LimitedAccessFeatures */
struct limited_access_result
{
    const void *vtbl;
    LONG ref;
    HSTRING feature_id;
};

static HRESULT WINAPI laf_result_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_ILimitedAccessFeatureRequestResult =
        { 0xd45156a6, 0x1e24, 0x5ddd, { 0xab, 0xb4, 0x61, 0x88, 0xab, 0xa4, 0xd5, 0xbf } };
    static const GUID IID_IAgileObject_local =
        { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject_local ) ||
        IsEqualGUID( iid, &IID_ILimitedAccessFeatureRequestResult ))
    {
        *out = iface;
        InterlockedIncrement( &((struct limited_access_result *)iface)->ref );
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI laf_result_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct limited_access_result *)iface)->ref );
}
static ULONG WINAPI laf_result_Release( void *iface )
{
    struct limited_access_result *r = iface;
    ULONG ref = InterlockedDecrement( &r->ref );
    if (!ref)
    {
        if (r->feature_id) WindowsDeleteString( r->feature_id );
        free( r );
    }
    return ref;
}
static HRESULT WINAPI laf_result_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI laf_result_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.LimitedAccessFeatureRequestResult", 58, n );
}
static HRESULT WINAPI laf_result_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI laf_result_get_FeatureId( void *iface, HSTRING *value )
{
    struct limited_access_result *r = iface;
    if (!value) return E_POINTER;
    return WindowsDuplicateString( r->feature_id, value );
}
static HRESULT WINAPI laf_result_get_Status( void *iface, INT32 *value )
{
    if (!value) return E_POINTER;
    *value = 1; /* LimitedAccessFeatureStatus_Available */
    return S_OK;
}
static HRESULT WINAPI laf_result_get_EstimatedRemovalDate( void *iface, void **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}
static const void *laf_result_vtbl[] =
{
    laf_result_QueryInterface,
    laf_result_AddRef,
    laf_result_Release,
    laf_result_GetIids,
    laf_result_GetRuntimeClassName,
    laf_result_GetTrustLevel,
    laf_result_get_FeatureId,
    laf_result_get_Status,
    laf_result_get_EstimatedRemovalDate,
};

static HRESULT WINAPI laf_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_ILimitedAccessFeaturesStatics =
        { 0x8be612d4, 0x302b, 0x5fbf, { 0xa6, 0x32, 0x1a, 0x99, 0xe4, 0x3e, 0x89, 0x25 } };
    static const GUID IID_IAgileObject_local =
        { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IAgileObject_local ) ||
        IsEqualGUID( iid, &IID_ILimitedAccessFeaturesStatics ))
    {
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI laf_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI laf_statics_Release( void *iface ) { return 1; }
static HRESULT WINAPI laf_statics_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI laf_statics_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.ApplicationModel.LimitedAccessFeatures", 46, n );
}
static HRESULT WINAPI laf_statics_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI laf_statics_TryUnlockFeature( void *iface, HSTRING feature_id, HSTRING token,
                                                    HSTRING attestation, void **result )
{
    struct limited_access_result *r;
    TRACE( "feature %s\n", debugstr_hstring(feature_id) );
    if (!result) return E_POINTER;
    *result = NULL;
    r = calloc( 1, sizeof(*r) );
    if (!r) return E_OUTOFMEMORY;
    r->vtbl = laf_result_vtbl;
    r->ref = 1;
    if (feature_id) WindowsDuplicateString( feature_id, &r->feature_id );
    *result = r;
    return S_OK;
}
static const void *laf_statics_vtbl[] =
{
    laf_statics_QueryInterface,
    laf_statics_AddRef,
    laf_statics_Release,
    laf_statics_GetIids,
    laf_statics_GetRuntimeClassName,
    laf_statics_GetTrustLevel,
    laf_statics_TryUnlockFeature,
};
static struct { const void **vtbl; } laf_statics_obj = { laf_statics_vtbl };
static IActivationFactory *laf_factory = (IActivationFactory *)&laf_statics_obj;

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;
    if (!buffer) return E_INVALIDARG;

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
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.LimitedAccessFeatures" ))
        *factory = laf_factory;
    else if (!wcscmp( buffer, L"Windows.ApplicationModel.AppExtensions.AppExtensionCatalog" ))
        IActivationFactory_QueryInterface( app_extension_catalog_factory,
                                           &IID_IActivationFactory, (void **)factory );
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
