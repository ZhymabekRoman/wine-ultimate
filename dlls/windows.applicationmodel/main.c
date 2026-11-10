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

static HRESULT WINAPI winappsdk_QueryInterface( void *iface, REFIID iid, void **out )
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

static const void *winappsdk_class_vtbl[] =
{
    winappsdk_QueryInterface,
    winappsdk_AddRef,
    winappsdk_Release,
    winappsdk_GetIids,
    winappsdk_GetRuntimeClassName,
    winappsdk_GetTrustLevel,
    winappsdk_zero_out,      /* ActivateInstance / the class's own first method slot */
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
    winappsdk_QueryInterface,
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
WINAPPSDK_CLASS( app_install_manager, "Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager" );
WINAPPSDK_CLASS( app_install_options, "Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallOptions" );
WINAPPSDK_CLASS( app_update_options, "Windows.ApplicationModel.Store.Preview.InstallControl.AppUpdateOptions" );
WINAPPSDK_CLASS( store_configuration, "Windows.ApplicationModel.Store.Preview.StoreConfiguration" );
WINAPPSDK_CLASS( store_system_feature, "Windows.ApplicationModel.Store.Preview.StoreSystemFeature" );
WINAPPSDK_CLASS( web_auth_core_helper, "Windows.ApplicationModel.Store.Preview.WebAuthenticationCoreManagerHelper" );
WINAPPSDK_CLASS( license_manager, "Windows.ApplicationModel.Store.LicenseManagement.LicenseManager" );
WINAPPSDK_CLASS( store_context, "Windows.Services.Store.StoreContext" );

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
      (IActivationFactory *)&app_install_manager_factory },
    { L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallOptions",
      (IActivationFactory *)&app_install_options_factory },
    { L"Windows.ApplicationModel.Store.Preview.InstallControl.AppUpdateOptions",
      (IActivationFactory *)&app_update_options_factory },
    { L"Windows.ApplicationModel.Store.Preview.StoreConfiguration",
      (IActivationFactory *)&store_configuration_factory },
    { L"Windows.ApplicationModel.Store.Preview.StoreSystemFeature",
      (IActivationFactory *)&store_system_feature_factory },
    { L"Windows.ApplicationModel.Store.Preview.WebAuthenticationCoreManagerHelper",
      (IActivationFactory *)&web_auth_core_helper_factory },
    { L"Windows.ApplicationModel.Store.LicenseManagement.LicenseManager",
      (IActivationFactory *)&license_manager_factory },
    { L"Windows.Services.Store.StoreContext",
      (IActivationFactory *)&store_context_factory },
};

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
            IActivationFactory_QueryInterface( (IActivationFactory *)&app_install_manager_factory,
                                               &IID_IActivationFactory, (void **)factory );
        }
    }

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
