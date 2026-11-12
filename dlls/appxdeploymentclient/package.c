/* WinRT Windows.Management.Deployment.PackageManager Implementation
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

#include "private.h"
#include "wine/debug.h"

#include <stdio.h>

#include "winreg.h"
#include "objbase.h"
#include "appmodel.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

#ifndef E_NOT_FOUND
#define E_NOT_FOUND MAKE_HRESULT( SEVERITY_ERROR, 0x87, 0x490 ) /* 0x80070490 */
#endif

LONG WINAPI PackageFamilyNameFromFullName( const WCHAR *full_name, UINT32 *length, WCHAR *family_name );

/*
 * RegisterPackageAsync's deployment, minimal ladder.
 *
 * Windows copies a staged package into the per-user package root and records
 * the deployment in the StateRepository; here the staged directory is the
 * install directory (no copy), and the record is kernelbase's registration:
 * Packages\<FullName> under the AppModel key, HKCU (the per-user deployment,
 * see dlls/kernelbase/version.c). The FullName of a staged package is the
 * staged directory's name; the family name is that name without the publisher
 * token, which is how the registration is indexed by the identity APIs.
 *
 * The operation is born completed: the deployment happened synchronously, and
 * a completed IAsyncOperationWithProgress answers GetResults from birth.
 */

struct deployment_result
{
    IDeploymentResult IDeploymentResult_iface;
    LONG ref;
};

static inline struct deployment_result *impl_from_IDeploymentResult( IDeploymentResult *iface )
{
    return CONTAINING_RECORD( iface, struct deployment_result, IDeploymentResult_iface );
}

static HRESULT WINAPI deployment_result_QueryInterface( IDeploymentResult *iface, REFIID iid, void **out )
{
    struct deployment_result *impl = impl_from_IDeploymentResult( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IDeploymentResult ))
    {
        *out = &impl->IDeploymentResult_iface;
        IDeploymentResult_AddRef( *out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI deployment_result_AddRef( IDeploymentResult *iface )
{
    struct deployment_result *impl = impl_from_IDeploymentResult( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI deployment_result_Release( IDeploymentResult *iface )
{
    struct deployment_result *impl = impl_from_IDeploymentResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI deployment_result_GetIids( IDeploymentResult *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI deployment_result_GetRuntimeClassName( IDeploymentResult *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Management.Deployment.DeploymentResult";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI deployment_result_GetTrustLevel( IDeploymentResult *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI deployment_result_get_ErrorText( IDeploymentResult *iface, HSTRING *text )
{
    *text = NULL;
    return S_OK;
}

static HRESULT WINAPI deployment_result_get_ActivityId( IDeploymentResult *iface, GUID *activity )
{
    memset( activity, 0, sizeof(*activity) );
    return S_OK;
}

static HRESULT WINAPI deployment_result_get_ExtendedErrorCode( IDeploymentResult *iface, HRESULT *hr )
{
    *hr = S_OK;
    return S_OK;
}

static const struct IDeploymentResultVtbl deployment_result_vtbl =
{
    deployment_result_QueryInterface,
    deployment_result_AddRef,
    deployment_result_Release,
    deployment_result_GetIids,
    deployment_result_GetRuntimeClassName,
    deployment_result_GetTrustLevel,
    deployment_result_get_ErrorText,
    deployment_result_get_ActivityId,
    deployment_result_get_ExtendedErrorCode,
};

struct register_async_op
{
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress iface;
    IAsyncInfo IAsyncInfo_iface;
    struct deployment_result *result;
    IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *completed;
    IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress *progress;
    LONG ref;
    ULONG id;
};

static inline struct register_async_op *impl_from_IAsyncOperationWithProgress(
        IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    return CONTAINING_RECORD( iface, struct register_async_op, iface );
}

static inline struct register_async_op *impl_from_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct register_async_op, IAsyncInfo_iface );
}

static HRESULT WINAPI async_op_QueryInterface( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        REFIID iid, void **out )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );

    TRACE( "iid %s, out %p.\n", debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress ))
    {
        *out = &impl->iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_op_AddRef( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_op_Release( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->completed)
            IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Release( impl->completed );
        if (impl->progress)
            IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_Release( impl->progress );
        IDeploymentResult_Release( &impl->result->IDeploymentResult_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI async_op_GetIids( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI async_op_GetRuntimeClassName( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Management.Deployment.DeploymentResult, Windows.Management.Deployment.DeploymentProgress>";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI async_op_GetTrustLevel( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_op_put_Progress( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress *handler )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );

    if (impl->progress)
        IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_Release( impl->progress );
    impl->progress = handler;
    if (handler) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_AddRef( handler );
    return S_OK;
}

static HRESULT WINAPI async_op_get_Progress( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress **handler )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );

    if ((*handler = impl->progress))
        IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI async_op_put_Completed( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *handler )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );

    if (impl->completed)
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Release( impl->completed );
    impl->completed = handler;
    if (handler)
    {
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_AddRef( handler );
        /* born completed: the handler of a completed operation fires at once */
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Invoke( handler, iface,
                Completed );
    }
    return S_OK;
}

static HRESULT WINAPI async_op_get_Completed( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress **handler )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );

    if ((*handler = impl->completed))
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI async_op_GetResults( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IDeploymentResult **results )
{
    struct register_async_op *impl = impl_from_IAsyncOperationWithProgress( iface );

    if (!results) return E_INVALIDARG;
    *results = &impl->result->IDeploymentResult_iface;
    IDeploymentResult_AddRef( *results );
    return S_OK;
}

static const struct IAsyncOperationWithProgress_DeploymentResult_DeploymentProgressVtbl async_op_vtbl =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Progress,
    async_op_get_Progress,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults,
};

static HRESULT WINAPI asyncinfo_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    struct register_async_op *impl = impl_from_IAsyncInfo( iface );

    return IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_QueryInterface( &impl->iface, iid, out );
}

static ULONG WINAPI asyncinfo_AddRef( IAsyncInfo *iface )
{
    struct register_async_op *impl = impl_from_IAsyncInfo( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI asyncinfo_Release( IAsyncInfo *iface )
{
    struct register_async_op *impl = impl_from_IAsyncInfo( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->completed)
            IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Release( impl->completed );
        if (impl->progress)
            IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_Release( impl->progress );
        IDeploymentResult_Release( &impl->result->IDeploymentResult_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI asyncinfo_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI asyncinfo_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return async_op_GetRuntimeClassName( (IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *)iface, name );
}

static HRESULT WINAPI asyncinfo_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI asyncinfo_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    struct register_async_op *impl = impl_from_IAsyncInfo( iface );

    *id = impl->id;
    return S_OK;
}

static HRESULT WINAPI asyncinfo_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI asyncinfo_get_ErrorCode( IAsyncInfo *iface, HRESULT *hr )
{
    *hr = S_OK;
    return S_OK;
}

static HRESULT WINAPI asyncinfo_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI asyncinfo_Close( IAsyncInfo *iface )
{
    return S_OK;
}

static const struct IAsyncInfoVtbl asyncinfo_vtbl =
{
    asyncinfo_QueryInterface,
    asyncinfo_AddRef,
    asyncinfo_Release,
    /* IInspectable methods */
    asyncinfo_GetIids,
    asyncinfo_GetRuntimeClassName,
    asyncinfo_GetTrustLevel,
    /* IAsyncInfo methods */
    asyncinfo_get_Id,
    asyncinfo_get_Status,
    asyncinfo_get_ErrorCode,
    asyncinfo_Cancel,
    asyncinfo_Close,
};

static HRESULT register_async_op_create( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **out )
{
    static LONG next_id;
    struct register_async_op *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->iface.lpVtbl = &async_op_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &asyncinfo_vtbl;
    impl->ref = 1;
    impl->id = InterlockedIncrement( &next_id );
    if (!(impl->result = calloc( 1, sizeof(*impl->result) )))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }
    impl->result->IDeploymentResult_iface.lpVtbl = &deployment_result_vtbl;
    impl->result->ref = 1;
    *out = &impl->iface;
    return S_OK;
}

/* Register package by parsing manifest and populating AppModel and WinRT registries */
static HRESULT register_staged_package( const WCHAR *path )
{
    return AppxRegisterPackageManifest( path );
}

static HRESULT WINAPI package_manager_RegisterPackageAsync( IPackageManager *iface, IUriRuntimeClass *uri,
    IIterable_Uri *dependencies, DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    WCHAR path[512];
    HSTRING str = NULL;
    const WCHAR *buffer;
    SIZE_T len;
    HRESULT hr;

    TRACE( "iface %p, uri %p, dependencies %p, options %d, operation %p.\n", iface, uri, dependencies, options, operation );

    if (!uri || !operation) return E_INVALIDARG;
    *operation = NULL;

    /* the staged directory arrives as a file: URI; Path is its local path
     * (this tree's IUriRuntimeClass; see wintypes/uri.c) */
    if (FAILED( (hr = IUriRuntimeClass_get_Path( uri, &str ) ) )) return hr;
    if (!(buffer = WindowsGetStringRawBuffer( str, NULL )))
    {
        WindowsDeleteString( str );
        return E_INVALIDARG;
    }
    lstrcpynW( path, buffer, ARRAY_SIZE(path) );
    WindowsDeleteString( str );
    len = lstrlenW( path );
    while (len && (path[len - 1] == L'\\' || path[len - 1] == L'/')) path[--len] = 0;  /* tolerate a trailing separator */
    if (!path[0]) return E_INVALIDARG;

    if (FAILED( (hr = register_staged_package( path ) ) )) return hr;
    TRACE( "registered package from %s\n", debugstr_w(path) );
    return register_async_op_create( operation );
}

static HRESULT find_packages( const WCHAR *family, const WCHAR *name, const WCHAR *publisher,
                              PackageTypes types, IIterable_Package **packages );
static HRESULT find_package_by_full_name( const WCHAR *full_name, IPackage **package );

struct package_manager
{
    IPackageManager IPackageManager_iface;
    IPackageManager2 IPackageManager2_iface;
    LONG ref;
};

static inline struct package_manager *impl_from_IPackageManager( IPackageManager *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager, IPackageManager_iface );
}

static HRESULT WINAPI package_manager_QueryInterface( IPackageManager *iface, REFIID iid, void **out )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPackageManager ))
    {
        *out = &impl->IPackageManager_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPackageManager2 ))
    {
        *out = &impl->IPackageManager2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_manager_AddRef( IPackageManager *iface )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI package_manager_Release( IPackageManager *iface )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI package_manager_GetIids( IPackageManager *iface, ULONG *iid_count, IID **iids )
{
    static const IID *const iids_pm[] = { &IID_IPackageManager, &IID_IPackageManager2 };

    FIXME( "iface %p, iid_count %p, iids %p semi-stub!\n", iface, iid_count, iids );
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = ARRAY_SIZE(iids_pm);
    if (!(*iids = CoTaskMemAlloc( *iid_count * sizeof(IID) ))) return E_OUTOFMEMORY;
    for (ULONG i = 0; i < *iid_count; i++) *(*iids + i) = *iids_pm[i];
    return S_OK;
}

static HRESULT WINAPI package_manager_GetRuntimeClassName( IPackageManager *iface, HSTRING *class_name )
{
    static const WCHAR class_w[] = L"Windows.Management.Deployment.PackageManager";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, class_name );
}

static HRESULT WINAPI package_manager_GetTrustLevel( IPackageManager *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_manager_AddPackageAsync( IPackageManager *iface, IUriRuntimeClass *uri,
    IIterable_Uri *dependencies, DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    TRACE( "iface %p, uri %p, dependencies %p, options %d, operation %p.\n", iface, uri, dependencies, options, operation );

    /* the staged package deploys like RegisterPackageAsync: the registration
     * is Packages\<directory name> in the AppModel key, the operation is born
     * completed with a DeploymentResult. */
    return package_manager_RegisterPackageAsync( iface, uri, dependencies, options, operation );
}

static HRESULT WINAPI package_manager_UpdatePackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_RemovePackageAsync( IPackageManager *iface, HSTRING name,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    const WCHAR *full;
    HRESULT hr;

    TRACE( "iface %p, name %s, operation %p.\n", iface, debugstr_hstring(name), operation );

    /* undeploy = drop the registration written by Register/AddPackageAsync;
     * a completed DeploymentResult answers from birth. */
    if (!name || !operation) return E_INVALIDARG;
    *operation = NULL;
    if (!(full = WindowsGetStringRawBuffer( name, NULL ))) return E_INVALIDARG;
    if (FAILED( (hr = AppxUnregisterPackageByName( full ) ) )) return hr;
    return register_async_op_create( operation );
}

static HRESULT WINAPI package_manager_StagePackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, operation %p stub!\n", iface, uri, dependencies, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackages( IPackageManager *iface, IIterable_Package **packages )
{
    TRACE( "iface %p, packages %p.\n", iface, packages );
    return find_packages( NULL, NULL, NULL, 0, packages );
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityId( IPackageManager *iface, HSTRING sid, IIterable_Package **packages )
{
    TRACE( "iface %p, sid %s, packages %p.\n", iface, debugstr_hstring(sid), packages );
    return find_packages( NULL, NULL, NULL, 0, packages );
}

static HRESULT WINAPI package_manager_FindPackagesByNamePublisher( IPackageManager *iface, HSTRING name, HSTRING publisher, IIterable_Package **packages )
{
    TRACE( "iface %p, name %s, publisher %s, packages %p.\n", iface, debugstr_hstring(name), debugstr_hstring(publisher), packages );

    if (!name || !publisher) return E_INVALIDARG;
    return find_packages( NULL, WindowsGetStringRawBuffer( name, NULL ),
                          WindowsGetStringRawBuffer( publisher, NULL ), 0, packages );
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityIdNamePublisher( IPackageManager *iface, HSTRING sid,
    HSTRING name, HSTRING publisher, IIterable_Package **packages )
{
    TRACE( "iface %p, sid %s, name %s, publisher %s, packages %p.\n", iface, debugstr_hstring(sid), debugstr_hstring(name), debugstr_hstring(publisher), packages );

    if (!name || !publisher) return E_INVALIDARG;
    return find_packages( NULL, WindowsGetStringRawBuffer( name, NULL ),
                          WindowsGetStringRawBuffer( publisher, NULL ), 0, packages );
}


/*
 * FindUsers: the prefix registers packages per user under the AppModel key;
 * every registered package's UserSecurityId value names a user that has it.
 * Enumerate that hive, dedupe by family filter, answer with
 * PackageUserInformation faces (security id + Installed) behind an
 * IIterable<PackageUserInformation> with a single-pass iterator.
 */

struct pui_entry
{
    struct package_user_info *info;
    WCHAR sid[128];
};

struct pui_list
{
    IIterable_PackageUserInformation IIterable_PackageUserInformation_iface;
    LONG ref;
    struct pui_entry entries[8];
    UINT32 count;
};

struct package_user_info
{
    IPackageUserInformation IPackageUserInformation_iface;
    struct pui_list *list;
    UINT32 index;
    LONG ref;
};

struct pui_iterator
{
    IIterator_PackageUserInformation IIterator_PackageUserInformation_iface;
    struct pui_list *list;
    LONG pos;
    LONG ref;
};

static inline struct package_user_info *pui_from_iface( IPackageUserInformation *iface )
{
    return CONTAINING_RECORD( iface, struct package_user_info, IPackageUserInformation_iface );
}

static void pui_list_release( struct pui_list *list );

static HRESULT WINAPI package_user_info_QueryInterface( IPackageUserInformation *iface, REFIID iid, void **out )
{
    struct package_user_info *This = pui_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPackageUserInformation ))
        *out = &This->IPackageUserInformation_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI package_user_info_AddRef( IPackageUserInformation *iface )
{
    struct package_user_info *This = pui_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI package_user_info_Release( IPackageUserInformation *iface )
{
    struct package_user_info *This = pui_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        pui_list_release( This->list );
        free( This );
    }
    return ref;
}

static HRESULT WINAPI package_user_info_GetIids( IPackageUserInformation *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI package_user_info_GetRuntimeClassName( IPackageUserInformation *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.Management.Deployment.PackageUserInformation";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI package_user_info_GetTrustLevel( IPackageUserInformation *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_user_info_get_UserSecurityId( IPackageUserInformation *iface, HSTRING *value )
{
    struct package_user_info *This = pui_from_iface( iface );
    const WCHAR *sid = This->list->entries[This->index].sid[0] ? This->list->entries[This->index].sid : L"S-1-5-21-0";
    return WindowsCreateString( sid, lstrlenW( sid ), value );
}

static HRESULT WINAPI package_user_info_get_InstallState( IPackageUserInformation *iface, PackageInstallState *state )
{
    *state = PackageInstallState_Installed;
    return S_OK;
}

static const struct IPackageUserInformationVtbl package_user_info_vtbl =
{
    package_user_info_QueryInterface,
    package_user_info_AddRef,
    package_user_info_Release,
    package_user_info_GetIids,
    package_user_info_GetRuntimeClassName,
    package_user_info_GetTrustLevel,
    package_user_info_get_UserSecurityId,
    package_user_info_get_InstallState,
};

static inline struct pui_iterator *pui_iter_from_iface( IIterator_PackageUserInformation *iface )
{
    return CONTAINING_RECORD( iface, struct pui_iterator, IIterator_PackageUserInformation_iface );
}

static HRESULT WINAPI pui_iterator_QueryInterface( IIterator_PackageUserInformation *iface, REFIID iid, void **out )
{
    struct pui_iterator *This = pui_iter_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterator_PackageUserInformation ))
        *out = &This->IIterator_PackageUserInformation_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI pui_iterator_AddRef( IIterator_PackageUserInformation *iface )
{
    struct pui_iterator *This = pui_iter_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI pui_iterator_Release( IIterator_PackageUserInformation *iface )
{
    struct pui_iterator *This = pui_iter_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        pui_list_release( This->list );
        free( This );
    }
    return ref;
}

static HRESULT WINAPI pui_iterator_GetIids( IIterator_PackageUserInformation *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI pui_iterator_GetRuntimeClassName( IIterator_PackageUserInformation *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.Foundation.Collections.IIterator";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI pui_iterator_GetTrustLevel( IIterator_PackageUserInformation *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI pui_iterator_get_Current( IIterator_PackageUserInformation *iface, IPackageUserInformation **item )
{
    struct pui_iterator *This = pui_iter_from_iface( iface );

    if (This->pos < 0 || This->pos >= (LONG)This->list->count) return E_FAIL;
    *item = &This->list->entries[This->pos].info->IPackageUserInformation_iface;
    IPackageUserInformation_AddRef( *item );
    return S_OK;
}

static HRESULT WINAPI pui_iterator_get_HasCurrent( IIterator_PackageUserInformation *iface, boolean *value )
{
    struct pui_iterator *This = pui_iter_from_iface( iface );
    *value = (This->pos >= 0 && This->pos < (LONG)This->list->count);
    return S_OK;
}

static HRESULT WINAPI pui_iterator_MoveNext( IIterator_PackageUserInformation *iface, boolean *value )
{
    struct pui_iterator *This = pui_iter_from_iface( iface );

    This->pos++;
    *value = (This->pos < (LONG)This->list->count);
    return S_OK;
}

static HRESULT WINAPI pui_iterator_GetMany( IIterator_PackageUserInformation *iface, UINT32 count,
        IPackageUserInformation **items, UINT32 *actual )
{
    FIXME( "iface %p, count %u, items %p, actual %p stub!\n", iface, count, items, actual );
    return E_NOTIMPL;
}

static const struct IIterator_PackageUserInformationVtbl pui_iterator_vtbl =
{
    pui_iterator_QueryInterface,
    pui_iterator_AddRef,
    pui_iterator_Release,
    pui_iterator_GetIids,
    pui_iterator_GetRuntimeClassName,
    pui_iterator_GetTrustLevel,
    pui_iterator_get_Current,
    pui_iterator_get_HasCurrent,
    pui_iterator_MoveNext,
    pui_iterator_GetMany,
};

static inline struct pui_list *pui_list_from_iface( IIterable_PackageUserInformation *iface )
{
    return CONTAINING_RECORD( iface, struct pui_list, IIterable_PackageUserInformation_iface );
}

static ULONG WINAPI pui_list_Release( IIterable_PackageUserInformation *iface )
{
    struct pui_list *This = pui_list_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < This->count; i++) free( This->entries[i].info );
        free( This );
    }
    return ref;
}

static HRESULT WINAPI pui_list_GetIids( IIterable_PackageUserInformation *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI pui_list_GetRuntimeClassName( IIterable_PackageUserInformation *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.Foundation.Collections.IIterable";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI pui_list_GetTrustLevel( IIterable_PackageUserInformation *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI pui_list_QueryInterface( IIterable_PackageUserInformation *iface, REFIID iid, void **out )
{
    struct pui_list *This = pui_list_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterable_PackageUserInformation ))
        *out = &This->IIterable_PackageUserInformation_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI pui_list_AddRef( IIterable_PackageUserInformation *iface )
{
    struct pui_list *This = pui_list_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static void pui_list_release( struct pui_list *This )
{
    ULONG ref = InterlockedDecrement( &This->ref );
    UINT32 i;

    if (!ref)
    {
        for (i = 0; i < This->count; i++) free( This->entries[i].info );
        free( This );
    }
}

static HRESULT WINAPI pui_list_First( IIterable_PackageUserInformation *iface,
        IIterator_PackageUserInformation **iterator )
{
    struct pui_list *This = pui_list_from_iface( iface );
    struct pui_iterator *iter;

    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->IIterator_PackageUserInformation_iface.lpVtbl = &pui_iterator_vtbl;
    iter->list = This;
    iter->pos = 0;
    iter->ref = 1;
    InterlockedIncrement( &This->ref );
    *iterator = &iter->IIterator_PackageUserInformation_iface;
    return S_OK;
}

static const struct IIterable_PackageUserInformationVtbl pui_list_vtbl =
{
    pui_list_QueryInterface,
    pui_list_AddRef,
    pui_list_Release,
    pui_list_GetIids,
    pui_list_GetRuntimeClassName,
    pui_list_GetTrustLevel,
    pui_list_First,
};

static HRESULT WINAPI package_manager_FindUsers( IPackageManager *iface, HSTRING name, IIterable_PackageUserInformation **users )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages";
    const WCHAR *family = name ? WindowsGetStringRawBuffer( name, NULL ) : NULL;
    WCHAR keyname[512], value[128];
    DWORD type, size;
    HKEY hkey, hsub;
    struct pui_list *list;
    DWORD i = 0;

    TRACE( "iface %p, name %s, users %p.\n", iface, debugstr_hstring(name), users );

    if (!users) return E_INVALIDARG;
    if (!(list = calloc( 1, sizeof(*list) ))) return E_OUTOFMEMORY;
    list->IIterable_PackageUserInformation_iface.lpVtbl = &pui_list_vtbl;
    list->ref = 1;

    if (RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_path, 0, KEY_READ, &hkey ))
    {
        free( list );
        return E_NOT_FOUND;
    }
    while (list->count < ARRAY_SIZE(list->entries))
    {
        struct package_user_info *info;

        if (RegEnumKeyW( hkey, i++, keyname, ARRAY_SIZE(keyname) )) break;
        if (family && wcsncmp( keyname, family, lstrlenW(family) )) continue;
        lstrcpyW( value, L"S-1-5-21-0" );
        if (!RegOpenKeyExW( hkey, keyname, 0, KEY_READ, &hsub ))
        {
            type = REG_SZ;
            size = sizeof(value);
            RegQueryValueExW( hsub, L"UserSecurityId", NULL, &type, (BYTE *)value, &size );
            RegCloseKey( hsub );
        }
        if (!(info = calloc( 1, sizeof(*info) ))) continue;
        info->IPackageUserInformation_iface.lpVtbl = &package_user_info_vtbl;
        info->list = list;
        info->index = list->count;
        info->ref = 1;
        lstrcpyW( list->entries[list->count].sid, value );
        list->entries[list->count].info = info;
        list->count++;
    }
    RegCloseKey( hkey );

    *users = &list->IIterable_PackageUserInformation_iface;
    return S_OK;
}

static HRESULT WINAPI package_manager_SetPackageState( IPackageManager *iface, HSTRING name, PackageState state )
{
    FIXME("iface %p, name %s, state %d stub!\n", iface, debugstr_hstring(name), state);
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackageByPackageFullName( IPackageManager *iface, HSTRING name, IPackage **package )
{
    TRACE( "iface %p, name %s, package %p.\n", iface, debugstr_hstring(name), package );

    if (!name || !package) return E_INVALIDARG;
    return find_package_by_full_name( WindowsGetStringRawBuffer( name, NULL ), package );
}

static HRESULT WINAPI package_manager_CleanupPackageForUserAsync( IPackageManager *iface, HSTRING name, HSTRING sid,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, sid %s, operation %p stub!\n", iface, debugstr_hstring(name), debugstr_hstring(sid), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackagesByPackageFamilyName( IPackageManager *iface, HSTRING family_name,
    IIterable_Package **packages )
{
    TRACE( "iface %p, family_name %s, packages %p.\n", iface, debugstr_hstring(family_name), packages );

    if (!family_name) return E_INVALIDARG;
    return find_packages( WindowsGetStringRawBuffer( family_name, NULL ), NULL, NULL, 0, packages );
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityIdPackageFamilyName( IPackageManager *iface, HSTRING sid,
    HSTRING family_name, IIterable_Package **packages )
{
    TRACE( "iface %p, sid %s, family_name %s, packages %p.\n", iface, debugstr_hstring(sid), debugstr_hstring(family_name), packages );

    if (!family_name) return E_INVALIDARG;
    return find_packages( WindowsGetStringRawBuffer( family_name, NULL ), NULL, NULL, 0, packages );
}

static HRESULT WINAPI package_manager_FindPackageByUserSecurityIdPackageFullName( IPackageManager *iface, HSTRING sid, HSTRING name, IPackage **package )
{
    TRACE( "iface %p, sid %s, name %s, package %p.\n", iface, debugstr_hstring(sid), debugstr_hstring(name), package );

    if (!name || !package) return E_INVALIDARG;
    return find_package_by_full_name( WindowsGetStringRawBuffer( name, NULL ), package );
}

static const struct IPackageManagerVtbl package_manager_vtbl =
{
    package_manager_QueryInterface,
    package_manager_AddRef,
    package_manager_Release,
    /* IInspectable methods */
    package_manager_GetIids,
    package_manager_GetRuntimeClassName,
    package_manager_GetTrustLevel,
    /* IPackageManager methods */
    package_manager_AddPackageAsync,
    package_manager_UpdatePackageAsync,
    package_manager_RemovePackageAsync,
    package_manager_StagePackageAsync,
    package_manager_RegisterPackageAsync,
    package_manager_FindPackages,
    package_manager_FindPackagesByUserSecurityId,
    package_manager_FindPackagesByNamePublisher,
    package_manager_FindPackagesByUserSecurityIdNamePublisher,
    package_manager_FindUsers,
    package_manager_SetPackageState,
    package_manager_FindPackageByPackageFullName,
    package_manager_CleanupPackageForUserAsync,
    package_manager_FindPackagesByPackageFamilyName,
    package_manager_FindPackagesByUserSecurityIdPackageFamilyName,
    package_manager_FindPackageByUserSecurityIdPackageFullName
};

DEFINE_IINSPECTABLE( package_manager2, IPackageManager2, struct package_manager, IPackageManager_iface );

static HRESULT WINAPI package_manager2_RemovePackageWithOptionsAsync( IPackageManager2 *iface, HSTRING name, RemovalOptions options,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, options %d, operation %p stub!\n", iface, debugstr_hstring(name), options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_StagePackageWithOptionsAsync( IPackageManager2 *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_RegisterPackageByFullNameAsync( IPackageManager2 *iface, HSTRING name, IIterable_HSTRING *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, dependencies %p, options %d, operation %p stub!\n", iface, debugstr_hstring(name), dependencies, options, operation );
    return E_NOTIMPL;
}

/* The WithPackageTypes family answers the plain one with the Framework flag
 * of the registration deciding which side of the filter each record is on. */

static HRESULT WINAPI package_manager2_FindPackagesWithPackageTypes( IPackageManager2 *iface, PackageTypes types, IIterable_Package **packages )
{
    TRACE( "iface %p, types %d, packages %p.\n", iface, types, packages );
    return find_packages( NULL, NULL, NULL, types, packages );
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdWithPackageTypes( IPackageManager2 *iface, HSTRING sid,
    PackageTypes types, IIterable_Package **packages )
{
    TRACE( "iface %p, sid %s, types %d, packages %p.\n", iface, debugstr_hstring(sid), types, packages );
    return find_packages( NULL, NULL, NULL, types, packages );
}

static HRESULT WINAPI package_manager2_FindPackagesByNamePublisherWithPackageTypes( IPackageManager2 *iface, HSTRING name, HSTRING publisher,
    PackageTypes types, IIterable_Package **packages )
{
    TRACE( "iface %p, name %s, publisher %s, types %d, packages %p.\n", iface, debugstr_hstring(name), debugstr_hstring(publisher), types, packages );

    if (!name || !publisher) return E_INVALIDARG;
    return find_packages( NULL, WindowsGetStringRawBuffer( name, NULL ),
                          WindowsGetStringRawBuffer( publisher, NULL ), types, packages );
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdNamePublisherWithPackageTypes( IPackageManager2 *iface, HSTRING sid, HSTRING name,
    HSTRING publisher, PackageTypes types, IIterable_Package **packages )
{
    TRACE( "iface %p, sid %s, name %s, publisher %s, types %d, packages %p.\n", iface, debugstr_hstring(sid), debugstr_hstring(name), debugstr_hstring(publisher), types, packages );

    if (!name || !publisher) return E_INVALIDARG;
    return find_packages( NULL, WindowsGetStringRawBuffer( name, NULL ),
                          WindowsGetStringRawBuffer( publisher, NULL ), types, packages );
}

static HRESULT WINAPI package_manager2_FindPackagesByPackageFamilyNameWithPackageTypes( IPackageManager2 *iface, HSTRING family_name, PackageTypes types,
   IIterable_Package **packages )
{
    TRACE( "iface %p, family_name %s, types %d, packages %p.\n", iface, debugstr_hstring(family_name), types, packages );

    if (!family_name) return E_INVALIDARG;
    return find_packages( WindowsGetStringRawBuffer( family_name, NULL ), NULL, NULL, types, packages );
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes( IPackageManager2 *iface, HSTRING sid, HSTRING family_name,
    PackageTypes types, IIterable_Package **packages )
{
    TRACE( "iface %p, sid %s, family_name %s, types %d, packages %p.\n", iface, debugstr_hstring(sid), debugstr_hstring(family_name), types, packages );

    if (!family_name) return E_INVALIDARG;
    return find_packages( WindowsGetStringRawBuffer( family_name, NULL ), NULL, NULL, types, packages );
}

static HRESULT find_users_async( const WCHAR *family, IAsyncOperation_IVectorView_User **operation );

static HRESULT WINAPI __attribute__((unused)) package_manager2_FindUsersAsync( IPackageManager2 *iface,
        IIterable_Package *packages, IAsyncOperation_IVectorView_User **operation )
{
    TRACE( "iface %p, packages %p, operation %p.\n", iface, packages, operation );

    return find_users_async( NULL, operation );
}

static HRESULT WINAPI __attribute__((unused)) package_manager2_FindUsersForPackageFamilyAsync( IPackageManager2 *iface,
        HSTRING family_name, IAsyncOperation_IVectorView_User **operation )
{
    const WCHAR *family = family_name ? WindowsGetStringRawBuffer( family_name, NULL ) : NULL;

    TRACE( "iface %p, family_name %s, operation %p.\n", iface, debugstr_hstring( family_name ), operation );

    return find_users_async( family, operation );
}


static HRESULT WINAPI package_manager2_StageUserDataAsync( IPackageManager2 *iface, HSTRING name,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static const struct IPackageManager2Vtbl package_manager2_vtbl =
{
    package_manager2_QueryInterface,
    package_manager2_AddRef,
    package_manager2_Release,
    /* IInspectable methods */
    package_manager2_GetIids,
    package_manager2_GetRuntimeClassName,
    package_manager2_GetTrustLevel,
    /* IPackageManager2 methods */
    package_manager2_RemovePackageWithOptionsAsync,
    package_manager2_StagePackageWithOptionsAsync,
    package_manager2_RegisterPackageByFullNameAsync,
    package_manager2_FindPackagesWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdWithPackageTypes,
    package_manager2_FindPackagesByNamePublisherWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdNamePublisherWithPackageTypes,
    package_manager2_FindPackagesByPackageFamilyNameWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes,
    package_manager2_StageUserDataAsync,
};

/*
 * The registered package, read from the deployment registration (kernelbase's
 * plane: Packages\<FullName> under the AppModel key, HKCU over HKLM, plus the
 * env-pinned running package and framework the identity APIs answer from). A
 * FindPackages call hands back these records as IPackage rows: the identity
 * fields parse out of the full name (Name_Version_Arch_Publisher grammar),
 * the path is the registration's, and the framework flag its Framework value.
 */

struct package_record
{
    WCHAR full_name[PACKAGE_FULL_NAME_MAX_LENGTH + 1];
    WCHAR family_name[256];
    WCHAR path[512];
    BOOL framework;
};

/* Parse the full name's identity parts; missing parts answer neutral/zero. */
static void split_full_name( const WCHAR *full, WCHAR *name, DWORD cch_name,
                             PackageVersion *version, ProcessorArchitecture *arch,
                             WCHAR *publisher, DWORD cch_publisher )
{
    WCHAR ver[32];
    const WCHAR *p = full, *end;
    SIZE_T i = 0;

    if (name) name[0] = 0;
    if (publisher) publisher[0] = 0;
    if (version) version->Major = version->Minor = version->Build = version->Revision = 0;
    if (arch) *arch = ProcessorArchitecture_Neutral;
    if (!full || !full[0]) return;

    end = wcschr( p, L'_' );
    if (!end) end = p + lstrlenW( p );
    if (name)
    {
        for (i = 0; p + i < end && i + 1 < cch_name; i++) name[i] = p[i];
        name[i] = 0;
    }
    if (!*end) return;
    p = end + 1;

    /* version: four decimal fields; a short one is a short version, not an error */
    end = wcschr( p, L'_' );
    i = end ? (SIZE_T)(end - p) : lstrlenW( p );
    if (i < ARRAY_SIZE(ver))
    {
        memcpy( ver, p, i * sizeof(WCHAR) );
        ver[i] = 0;
        if (version) swscanf( ver, L"%hu.%hu.%hu.%hu",
                &version->Major, &version->Minor, &version->Build, &version->Revision );
    }
    if (!end) return;
    p = end + 1;

    end = wcschr( p, L'_' );
    if (end)
    {
        if (!wcsncmp( p, L"x86", 3 ) && (end - p == 3 )) *arch = ProcessorArchitecture_X86;
        else if (!wcsncmp( p, L"x64", 3 ) && (end - p == 3 )) *arch = ProcessorArchitecture_X64;
        else if (!wcsncmp( p, L"arm64", 5 ) && (end - p == 5 )) *arch = ProcessorArchitecture_Arm64;
        else if (!wcsncmp( p, L"arm", 3 ) && (end - p == 3 )) *arch = ProcessorArchitecture_Arm;
        else if (!wcsncmp( p, L"neutral", 7 ) && (end - p == 7 )) *arch = ProcessorArchitecture_Neutral;
        if (!*end) return;
        p = end + 1;  /* the neutral arch token is empty: 'Name_Version__Publisher' */
        if (!*p)
        {
            end = wcschr( p, L'_' );
            if (!end) return;
            p = end + 1;
        }
    }
    if (publisher) lstrcpynW( publisher, p, cch_publisher );
}

struct package
{
    IPackage IPackage_iface;
    IPackageId IPackageId_iface;
    IStorageFolder IStorageFolder_iface;
    IStorageItem IStorageItem_iface;
    LONG ref;
    struct package_record rec;
};

static inline struct package *impl_from_IPackage( IPackage *iface )
{
    return CONTAINING_RECORD( iface, struct package, IPackage_iface );
}

static inline struct package *impl_from_IPackageId( IPackageId *iface )
{
    return CONTAINING_RECORD( iface, struct package, IPackageId_iface );
}

static HRESULT package_create( const struct package_record *rec, struct package **ret );

static HRESULT WINAPI package_QueryInterface( IPackage *iface, REFIID iid, void **out )
{
    struct package *impl = impl_from_IPackage( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPackage ))
    {
        *out = &impl->IPackage_iface;
    }
    else if (IsEqualGUID( iid, &IID_IPackageId ))
    {
        *out = &impl->IPackageId_iface;
    }
    else if (IsEqualGUID( iid, &IID_IStorageFolder ))
    {
        *out = &impl->IStorageFolder_iface;
    }
    else if (IsEqualGUID( iid, &IID_IStorageItem ))
    {
        *out = &impl->IStorageItem_iface;
    }
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI package_AddRef( IPackage *iface )
{
    struct package *impl = impl_from_IPackage( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI package_Release( IPackage *iface )
{
    struct package *impl = impl_from_IPackage( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI package_GetIids( IPackage *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI package_GetRuntimeClassName( IPackage *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.ApplicationModel.Package";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI package_GetTrustLevel( IPackage *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_get_Id( IPackage *iface, IPackageId **value )
{
    struct package *impl = impl_from_IPackage( iface );

    *value = &impl->IPackageId_iface;
    IPackageId_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI package_get_InstalledLocation( IPackage *iface, IStorageFolder **value )
{
    struct package *impl = impl_from_IPackage( iface );

    *value = &impl->IStorageFolder_iface;
    IStorageFolder_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI package_get_IsFramework( IPackage *iface, boolean *value )
{
    struct package *impl = impl_from_IPackage( iface );

    *value = impl->rec.framework;
    return S_OK;
}

static HRESULT create_empty_vectorview( IVectorView_Package **value );

static HRESULT WINAPI package_get_Dependencies( IPackage *iface, IVectorView_Package **value )
{
    /* the registration records no dependency graph: the empty view is the truth */
    return create_empty_vectorview( value );
}

static const struct IPackageVtbl package_vtbl =
{
    package_QueryInterface,
    package_AddRef,
    package_Release,
    /* IInspectable methods */
    package_GetIids,
    package_GetRuntimeClassName,
    package_GetTrustLevel,
    /* IPackage methods */
    package_get_Id,
    package_get_InstalledLocation,
    package_get_IsFramework,
    package_get_Dependencies,
};

/* IPackageId: every field is a read of the record the row was created from. */

static HRESULT WINAPI packageid_QueryInterface( IPackageId *iface, REFIID iid, void **out )
{
    struct package *impl = impl_from_IPackageId( iface );

    return IPackage_QueryInterface( &impl->IPackage_iface, iid, out );
}

static ULONG WINAPI packageid_AddRef( IPackageId *iface )
{
    struct package *impl = impl_from_IPackageId( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI packageid_Release( IPackageId *iface )
{
    struct package *impl = impl_from_IPackageId( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI packageid_GetIids( IPackageId *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI packageid_GetRuntimeClassName( IPackageId *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.ApplicationModel.PackageId";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI packageid_GetTrustLevel( IPackageId *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT packageid_str( struct package *impl, const WCHAR *value, HSTRING *str )
{
    if (!value || !value[0])
    {
        *str = NULL;
        return S_OK;
    }
    return WindowsCreateString( value, lstrlenW( value ), str );
}

static HRESULT WINAPI packageid_get_Name( IPackageId *iface, HSTRING *value )
{
    struct package *impl = impl_from_IPackageId( iface );
    WCHAR name[256];

    split_full_name( impl->rec.full_name, name, ARRAY_SIZE(name), NULL, NULL, NULL, 0 );
    return packageid_str( impl, name, value );
}

static HRESULT WINAPI packageid_get_Version( IPackageId *iface, PackageVersion *value )
{
    struct package *impl = impl_from_IPackageId( iface );

    split_full_name( impl->rec.full_name, NULL, 0, value, NULL, NULL, 0 );
    return S_OK;
}

static HRESULT WINAPI packageid_get_Architecture( IPackageId *iface, ProcessorArchitecture *value )
{
    struct package *impl = impl_from_IPackageId( iface );

    split_full_name( impl->rec.full_name, NULL, 0, NULL, value, NULL, 0 );
    return S_OK;
}

static HRESULT WINAPI packageid_get_ResourceId( IPackageId *iface, HSTRING *value )
{
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI packageid_get_Publisher( IPackageId *iface, HSTRING *value )
{
    struct package *impl = impl_from_IPackageId( iface );
    WCHAR publisher[256];

    split_full_name( impl->rec.full_name, NULL, 0, NULL, NULL, publisher, ARRAY_SIZE(publisher) );
    return packageid_str( impl, publisher, value );
}

static HRESULT WINAPI packageid_get_PublisherId( IPackageId *iface, HSTRING *value )
{
    /* the registration plane knows the full name's publisher token, which is
     * what PublisherId reports for these packages */
    return packageid_get_Publisher( iface, value );
}

static HRESULT WINAPI packageid_get_FullName( IPackageId *iface, HSTRING *value )
{
    struct package *impl = impl_from_IPackageId( iface );

    return packageid_str( impl, impl->rec.full_name, value );
}

static HRESULT WINAPI packageid_get_FamilyName( IPackageId *iface, HSTRING *value )
{
    struct package *impl = impl_from_IPackageId( iface );

    return packageid_str( impl, impl->rec.family_name, value );
}

static const struct IPackageIdVtbl packageid_vtbl =
{
    packageid_QueryInterface,
    packageid_AddRef,
    packageid_Release,
    /* IInspectable methods */
    packageid_GetIids,
    packageid_GetRuntimeClassName,
    packageid_GetTrustLevel,
    /* IPackageId methods */
    packageid_get_Name,
    packageid_get_Version,
    packageid_get_Architecture,
    packageid_get_ResourceId,
    packageid_get_Publisher,
    packageid_get_PublisherId,
    packageid_get_FullName,
    packageid_get_FamilyName,
};

/* InstalledLocation's folder: the item face carries the path, the folder face
 * is the handle a caller digs into with the async methods (unimplemented). */

#define DEFINE_PACKAGE_QI( name, iface_type, member )                                                 \
    static HRESULT WINAPI name##_QueryInterface( iface_type *iface, REFIID iid, void **out )          \
    {                                                                                                 \
        struct package *impl = CONTAINING_RECORD( iface, struct package, member );                    \
        return package_QueryInterface( &impl->IPackage_iface, iid, out );                             \
    }                                                                                                 \
    static ULONG WINAPI name##_AddRef( iface_type *iface )                                            \
    {                                                                                                 \
        struct package *impl = CONTAINING_RECORD( iface, struct package, member );                    \
        return InterlockedIncrement( &impl->ref );                                                    \
    }                                                                                                 \
    static ULONG WINAPI name##_Release( iface_type *iface )                                           \
    {                                                                                                 \
        struct package *impl = CONTAINING_RECORD( iface, struct package, member );                    \
        ULONG ref = InterlockedDecrement( &impl->ref );                                               \
        if (!ref) free( impl );                                                                       \
        return ref;                                                                                   \
    }

DEFINE_PACKAGE_QI( package_folder, IStorageFolder, IStorageFolder_iface )
DEFINE_PACKAGE_QI( package_item, IStorageItem, IStorageItem_iface )

static HRESULT WINAPI package_folder_GetIids( IStorageFolder *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI package_folder_GetRuntimeClassName( IStorageFolder *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Storage.StorageFolder";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI package_folder_GetTrustLevel( IStorageFolder *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

#define FOLDER_METHODS( pfx ) \
    static HRESULT WINAPI pfx##_CreateFileAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name, \
            IAsyncOperation_StorageFile **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_CreateFileAsync( IStorageFolder *iface, HSTRING name, \
            CreationCollisionOption option, IAsyncOperation_StorageFile **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_CreateFolderAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name, \
            IAsyncOperation_StorageFolder **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_CreateFolderAsync( IStorageFolder *iface, HSTRING name, \
            CreationCollisionOption option, IAsyncOperation_StorageFolder **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_GetFileAsync( IStorageFolder *iface, HSTRING name, \
            IAsyncOperation_StorageFile **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_GetFolderAsync( IStorageFolder *iface, HSTRING name, \
            IAsyncOperation_StorageFolder **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_GetItemAsync( IStorageFolder *iface, HSTRING name, \
            IAsyncOperation_IStorageItem **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_GetFilesAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface, \
            IAsyncOperation_IVectorView_StorageFile **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_GetFoldersAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface, \
            IAsyncOperation_IVectorView_StorageFolder **operation ) { return E_NOTIMPL; } \
    static HRESULT WINAPI pfx##_GetItemsAsyncOverloadDefaultStartAndCount( IStorageFolder *iface, \
            IAsyncOperation_IVectorView_IStorageItem **operation ) { return E_NOTIMPL; }

FOLDER_METHODS( package_folder )

static const struct IStorageFolderVtbl package_folder_vtbl =
{
    package_folder_QueryInterface,
    package_folder_AddRef,
    package_folder_Release,
    package_folder_GetIids,
    package_folder_GetRuntimeClassName,
    package_folder_GetTrustLevel,
    package_folder_CreateFileAsyncOverloadDefaultOptions,
    package_folder_CreateFileAsync,
    package_folder_CreateFolderAsyncOverloadDefaultOptions,
    package_folder_CreateFolderAsync,
    package_folder_GetFileAsync,
    package_folder_GetFolderAsync,
    package_folder_GetItemAsync,
    package_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount,
    package_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount,
    package_folder_GetItemsAsyncOverloadDefaultStartAndCount,
};

static HRESULT WINAPI package_item_GetIids( IStorageItem *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI package_item_GetRuntimeClassName( IStorageItem *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Storage.StorageFolder";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI package_item_GetTrustLevel( IStorageItem *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_item_RenameAsyncOverloadDefaultOptions( IStorageItem *iface, HSTRING name,
        IAsyncAction **operation ) { return E_NOTIMPL; }
static HRESULT WINAPI package_item_RenameAsync( IStorageItem *iface, HSTRING name, NameCollisionOption option,
        IAsyncAction **operation ) { return E_NOTIMPL; }
static HRESULT WINAPI package_item_DeleteAsyncOverloadDefaultOptions( IStorageItem *iface,
        IAsyncAction **operation ) { return E_NOTIMPL; }
static HRESULT WINAPI package_item_DeleteAsync( IStorageItem *iface, StorageDeleteOption option,
        IAsyncAction **operation ) { return E_NOTIMPL; }
static HRESULT WINAPI package_item_GetBasicPropertiesAsync( IStorageItem *iface,
        IAsyncOperation_BasicProperties **operation ) { return E_NOTIMPL; }

static HRESULT WINAPI package_item_get_Name( IStorageItem *iface, HSTRING *value )
{
    struct package *impl = CONTAINING_RECORD( iface, struct package, IStorageItem_iface );
    WCHAR name[256];

    split_full_name( impl->rec.full_name, name, ARRAY_SIZE(name), NULL, NULL, NULL, 0 );
    return WindowsCreateString( name, lstrlenW( name ), value );
}

static HRESULT WINAPI package_item_get_Path( IStorageItem *iface, HSTRING *value )
{
    struct package *impl = CONTAINING_RECORD( iface, struct package, IStorageItem_iface );

    if (!impl->rec.path[0])
    {
        *value = NULL;
        return S_OK;
    }
    return WindowsCreateString( impl->rec.path, lstrlenW( impl->rec.path ), value );
}

static HRESULT WINAPI package_item_get_Attributes( IStorageItem *iface, FileAttributes *value )
{
    *value = FileAttributes_Normal;
    return S_OK;
}

static HRESULT WINAPI package_item_get_DateCreated( IStorageItem *iface, DateTime *value )
{
    memset( value, 0, sizeof(*value) );
    return S_OK;
}

static HRESULT WINAPI package_item_IsOfType( IStorageItem *iface, StorageItemTypes type, boolean *value )
{
    *value = type == StorageItemTypes_Folder;
    return S_OK;
}

static const struct IStorageItemVtbl package_item_vtbl =
{
    package_item_QueryInterface,
    package_item_AddRef,
    package_item_Release,
    package_item_GetIids,
    package_item_GetRuntimeClassName,
    package_item_GetTrustLevel,
    package_item_RenameAsyncOverloadDefaultOptions,
    package_item_RenameAsync,
    package_item_DeleteAsyncOverloadDefaultOptions,
    package_item_DeleteAsync,
    package_item_GetBasicPropertiesAsync,
    package_item_get_Name,
    package_item_get_Path,
    package_item_get_Attributes,
    package_item_get_DateCreated,
    package_item_IsOfType,
};

static HRESULT package_create( const struct package_record *rec, struct package **ret )
{
    struct package *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IPackage_iface.lpVtbl = &package_vtbl;
    impl->IPackageId_iface.lpVtbl = &packageid_vtbl;
    impl->IStorageFolder_iface.lpVtbl = &package_folder_vtbl;
    impl->IStorageItem_iface.lpVtbl = &package_item_vtbl;
    impl->ref = 1;
    impl->rec = *rec;
    *ret = impl;
    return S_OK;
}

/*
 * The package collection: an IIterable built once per Find call, its rows the
 * records the registration held at that moment. The iterator walks the rows;
 * a caller holding the iterable after its iterator dies sees a stable set.
 */

struct package_collection
{
    IIterable_Package IIterable_Package_iface;
    LONG ref;
    ULONG count;
    struct package *items[1];
};

static inline struct package_collection *impl_from_IIterable_Package( IIterable_Package *iface )
{
    return CONTAINING_RECORD( iface, struct package_collection, IIterable_Package_iface );
}

struct package_iterator
{
    IIterator_Package IIterator_Package_iface;
    LONG ref;
    struct package_collection *coll;
    ULONG pos;
};

static inline struct package_iterator *impl_from_IIterator_Package( IIterator_Package *iface )
{
    return CONTAINING_RECORD( iface, struct package_iterator, IIterator_Package_iface );
}

static HRESULT WINAPI package_iterator_QueryInterface( IIterator_Package *iface, REFIID iid, void **out )
{
    struct package_iterator *impl = impl_from_IIterator_Package( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterator_Package ))
    {
        *out = &impl->IIterator_Package_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_iterator_AddRef( IIterator_Package *iface )
{
    struct package_iterator *impl = impl_from_IIterator_Package( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI package_iterator_Release( IIterator_Package *iface )
{
    struct package_iterator *impl = impl_from_IIterator_Package( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        IIterable_Package_Release( &impl->coll->IIterable_Package_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI package_iterator_GetIids( IIterator_Package *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI package_iterator_GetRuntimeClassName( IIterator_Package *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.ApplicationModel.Package>";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI package_iterator_GetTrustLevel( IIterator_Package *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_iterator_get_HasCurrent( IIterator_Package *iface, boolean *value )
{
    struct package_iterator *impl = impl_from_IIterator_Package( iface );

    *value = impl->pos < impl->coll->count;
    return S_OK;
}

static HRESULT WINAPI package_iterator_get_Current( IIterator_Package *iface, IPackage **item )
{
    struct package_iterator *impl = impl_from_IIterator_Package( iface );

    if (impl->pos >= impl->coll->count) return E_BOUNDS;
    *item = &impl->coll->items[impl->pos]->IPackage_iface;
    IPackage_AddRef( *item );
    return S_OK;
}

static HRESULT WINAPI package_iterator_MoveNext( IIterator_Package *iface, boolean *value )
{
    struct package_iterator *impl = impl_from_IIterator_Package( iface );

    if (impl->pos < impl->coll->count) impl->pos++;
    *value = impl->pos < impl->coll->count;
    return S_OK;
}

static HRESULT WINAPI package_iterator_GetMany( IIterator_Package *iface, UINT32 capacity,
        IPackage **items, UINT32 *written )
{
    struct package_iterator *impl = impl_from_IIterator_Package( iface );
    ULONG n;

    if (!items || !written) return E_INVALIDARG;
    n = min( capacity, impl->coll->count - impl->pos );
    for (ULONG i = 0; i < n; i++)
    {
        items[i] = &impl->coll->items[impl->pos + i]->IPackage_iface;
        IPackage_AddRef( items[i] );
    }
    *written = n;
    return S_OK;
}

static const struct IIterator_PackageVtbl package_iterator_vtbl =
{
    package_iterator_QueryInterface,
    package_iterator_AddRef,
    package_iterator_Release,
    package_iterator_GetIids,
    package_iterator_GetRuntimeClassName,
    package_iterator_GetTrustLevel,
    package_iterator_get_Current,
    package_iterator_get_HasCurrent,
    package_iterator_MoveNext,
    package_iterator_GetMany,
};

static HRESULT WINAPI package_collection_QueryInterface( IIterable_Package *iface, REFIID iid, void **out )
{
    struct package_collection *impl = impl_from_IIterable_Package( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterable_Package ))
    {
        *out = &impl->IIterable_Package_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_collection_AddRef( IIterable_Package *iface )
{
    struct package_collection *impl = impl_from_IIterable_Package( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI package_collection_Release( IIterable_Package *iface )
{
    struct package_collection *impl = impl_from_IIterable_Package( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        for (ULONG i = 0; i < impl->count; i++) IPackage_Release( &impl->items[i]->IPackage_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI package_collection_GetIids( IIterable_Package *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI package_collection_GetRuntimeClassName( IIterable_Package *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IIterable`1<Windows.ApplicationModel.Package>";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI package_collection_GetTrustLevel( IIterable_Package *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_collection_First( IIterable_Package *iface, IIterator_Package **iterator )
{
    struct package_collection *impl = impl_from_IIterable_Package( iface );
    struct package_iterator *it;

    if (!iterator) return E_INVALIDARG;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->IIterator_Package_iface.lpVtbl = &package_iterator_vtbl;
    it->ref = 1;
    it->coll = impl;
    IIterable_Package_AddRef( iface );
    *iterator = &it->IIterator_Package_iface;
    return S_OK;
}

static const struct IIterable_PackageVtbl package_collection_vtbl =
{
    package_collection_QueryInterface,
    package_collection_AddRef,
    package_collection_Release,
    package_collection_GetIids,
    package_collection_GetRuntimeClassName,
    package_collection_GetTrustLevel,
    package_collection_First,
};

static HRESULT package_collection_create( struct package **items, ULONG count, IIterable_Package **ret )
{
    struct package_collection *impl;
    SIZE_T size = offsetof( struct package_collection, items[count ? count : 1] );

    if (!(impl = calloc( 1, size ))) return E_OUTOFMEMORY;
    impl->IIterable_Package_iface.lpVtbl = &package_collection_vtbl;
    impl->ref = 1;
    impl->count = count;
    for (ULONG i = 0; i < count; i++)
    {
        impl->items[i] = items[i];
        IPackage_AddRef( &items[i]->IPackage_iface );
    }
    *ret = &impl->IIterable_Package_iface;
    return S_OK;
}

/* IPackage's Dependencies: the registration records no graph; the empty view. */

struct package_vectorview
{
    IVectorView_Package IVectorView_Package_iface;
    LONG ref;
};

static inline struct package_vectorview *impl_from_IVectorView_Package( IVectorView_Package *iface )
{
    return CONTAINING_RECORD( iface, struct package_vectorview, IVectorView_Package_iface );
}

static HRESULT WINAPI package_vectorview_QueryInterface( IVectorView_Package *iface, REFIID iid, void **out )
{
    struct package_vectorview *impl = impl_from_IVectorView_Package( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IVectorView_Package ))
    {
        *out = &impl->IVectorView_Package_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_vectorview_AddRef( IVectorView_Package *iface )
{
    struct package_vectorview *impl = impl_from_IVectorView_Package( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI package_vectorview_Release( IVectorView_Package *iface )
{
    struct package_vectorview *impl = impl_from_IVectorView_Package( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI package_vectorview_GetIids( IVectorView_Package *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI package_vectorview_GetRuntimeClassName( IVectorView_Package *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Package>";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI package_vectorview_GetTrustLevel( IVectorView_Package *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_vectorview_GetAt( IVectorView_Package *iface, UINT32 index, IPackage **item )
{
    return E_BOUNDS;
}

static HRESULT WINAPI package_vectorview_get_Size( IVectorView_Package *iface, UINT32 *size )
{
    *size = 0;
    return S_OK;
}

static HRESULT WINAPI package_vectorview_IndexOf( IVectorView_Package *iface, IPackage *item, UINT32 *index,
        boolean *found )
{
    *index = 0;
    *found = FALSE;
    return S_OK;
}

static HRESULT WINAPI package_vectorview_GetMany( IVectorView_Package *iface, UINT32 start, UINT32 capacity,
        IPackage **items, UINT32 *written )
{
    *written = 0;
    return S_OK;
}

static const struct IVectorView_PackageVtbl package_vectorview_vtbl =
{
    package_vectorview_QueryInterface,
    package_vectorview_AddRef,
    package_vectorview_Release,
    package_vectorview_GetIids,
    package_vectorview_GetRuntimeClassName,
    package_vectorview_GetTrustLevel,
    package_vectorview_GetAt,
    package_vectorview_get_Size,
    package_vectorview_IndexOf,
    package_vectorview_GetMany,
};

static HRESULT create_empty_vectorview( IVectorView_Package **value )
{
    struct package_vectorview *impl;

    if (!value) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IVectorView_Package_iface.lpVtbl = &package_vectorview_vtbl;
    impl->ref = 1;
    *value = &impl->IVectorView_Package_iface;
    return S_OK;
}

/*
 * The registration read: the running package, the env framework, then every
 * Packages\<FullName> key HKCU over HKLM (a user deployment shadows the
 * machine one, kernelbase's merge order). Family/name/publisher filters narrow
 * the set; the package APIs answer the family and path for each record. This
 * is the same graph kernelbase's GetCurrentPackageInfo builds.
 */
#define MAX_RECORDS 64

struct registration
{
    struct package_record records[MAX_RECORDS];
    ULONG count;
};

static BOOL registration_add( struct registration *reg, const WCHAR *full_name, BOOL framework )
{
    struct package_record *rec;
    UINT32 len;
    SIZE_T i;

    for (i = 0; i < reg->count; i++)
        if (!wcsicmp( reg->records[i].full_name, full_name )) return FALSE;
    if (reg->count == MAX_RECORDS) return FALSE;

    rec = &reg->records[reg->count];
    lstrcpynW( rec->full_name, full_name, ARRAY_SIZE(rec->full_name) );
    rec->framework = framework;
    rec->family_name[0] = 0;
    len = ARRAY_SIZE(rec->family_name);
    PackageFamilyNameFromFullName( full_name, &len, rec->family_name );
    rec->path[0] = 0;
    len = ARRAY_SIZE(rec->path);
    GetPackagePathByFullName( full_name, &len, rec->path );
    reg->count++;
    return TRUE;
}

static void registration_read( struct registration *reg, const WCHAR *family, const WCHAR *name,
                               const WCHAR *publisher )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages";
    WCHAR full[PACKAGE_FULL_NAME_MAX_LENGTH + 1], key[600], subkey[620], fw[512];
    SIZE_T r, i;

    reg->count = 0;

    /* the running package and the env framework: the identity APIs' graph */
    {
        UINT32 len = ARRAY_SIZE(full);
        if (!GetCurrentPackageFullName( &len, full )) registration_add( reg, full, FALSE );
    }
    if (GetEnvironmentVariableW( L"WINE_FRAMEWORK_PACKAGE_FULL_NAME", fw, ARRAY_SIZE(fw) ))
        registration_add( reg, fw, TRUE );

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY hkey;
        DWORD i = 0;

        if (RegOpenKeyExW( roots[r], appmodel_path, 0, KEY_READ, &hkey )) continue;
        for (;; i++)
        {
            DWORD len = ARRAY_SIZE(key), type;
            HKEY pkg;

            if (RegEnumKeyExW( hkey, i, key, &len, NULL, NULL, NULL, NULL )) break;
            lstrcpyW( subkey, appmodel_path );
            lstrcatW( subkey, L"\\" );
            lstrcatW( subkey, key );
            type = 0;
            len = sizeof(type);
            if (RegOpenKeyExW( roots[r], subkey, 0, KEY_READ, &pkg ) == ERROR_SUCCESS)
            {
                RegQueryValueExW( pkg, L"Framework", NULL, &type, (BYTE *)&type, &len );
                RegCloseKey( pkg );
            }
            else type = 0;
            registration_add( reg, key, type == 1 );
        }
        RegCloseKey( hkey );
    }

    /* the filters narrow the set; every record keeps its own family/path reads */
    for (i = 0; i < reg->count; i++)
    {
        struct package_record *rec = &reg->records[i];
        BOOL keep = TRUE;

        if (family && wcsicmp( rec->family_name, family )) keep = FALSE;
        if (keep && (name || publisher))
        {
            WCHAR part[256];
            if (name)
            {
                split_full_name( rec->full_name, part, ARRAY_SIZE(part), NULL, NULL, NULL, 0 );
                if (wcsicmp( part, name )) keep = FALSE;
            }
            if (keep && publisher)
            {
                split_full_name( rec->full_name, NULL, 0, NULL, NULL, part, ARRAY_SIZE(part) );
                if (wcsicmp( part, publisher )) keep = FALSE;
            }
        }
        if (!keep) reg->records[i--] = reg->records[--reg->count];
    }
}

/* Hand the registration (filtered) back as the collection the caller asked
 * for. The SID is the single user a prefix has, so the ByUserSecurityId
 * methods answer the same set the plain ones do; types selects frameworks,
 * apps, or both (0 answers both). */

/* ---- born-completed IAsyncOperation<IVectorView<User>> over the FindUsers
 * face: Windows.System.User over the registration's UserSecurityId ---- */

struct user_list;

struct user_item
{
    IUser IUser_iface;
    struct user_list *list;
    UINT32 index;
    LONG ref;
};

struct user_list
{
    IAsyncOperation_IVectorView_User op;
    IVectorView_User view;
    IAsyncInfo info;
    LONG ref;
    IUnknown *handler;
    WCHAR sid[8][128];
    UINT32 count;
};

static inline struct user_list *user_from_op( IAsyncOperation_IVectorView_User *iface )
{
    return CONTAINING_RECORD( iface, struct user_list, op );
}

static inline struct user_list *user_from_view( IVectorView_User *iface )
{
    return CONTAINING_RECORD( iface, struct user_list, view );
}

static inline struct user_list *user_from_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct user_list, info );
}

static inline struct user_item *user_from_iface( IUser *iface )
{
    return CONTAINING_RECORD( iface, struct user_item, IUser_iface );
}

static void user_list_release( struct user_list *This );

static HRESULT WINAPI user_item_QueryInterface( IUser *iface, REFIID iid, void **out )
{
    struct user_item *This = user_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IUser ))
        *out = &This->IUser_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI user_item_AddRef( IUser *iface )
{
    struct user_item *This = user_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI user_item_Release( IUser *iface )
{
    struct user_item *This = user_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        user_list_release( This->list );
        free( This );
    }
    return ref;
}

static HRESULT WINAPI user_item_GetIids( IUser *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}

static HRESULT WINAPI user_item_GetRuntimeClassName( IUser *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.System.User";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI user_item_GetTrustLevel( IUser *iface, TrustLevel *trust )
{
    *trust = BaseTrust; return S_OK;
}

static HRESULT WINAPI user_item_get_DomainAccountName( IUser *iface, HSTRING *value )
{
    struct user_item *This = user_from_iface( iface );
    WCHAR name[192], user[64];
    DWORD chars = ARRAY_SIZE(user);

    GetUserNameW( user, &chars );
    lstrcpyW( name, user );
    lstrcatW( name, L"\\" );
    lstrcatW( name, This->list->sid[This->index] );
    return WindowsCreateString( name, lstrlenW( name ), value );
}

static const struct IUserVtbl user_item_vtbl =
{
    user_item_QueryInterface, user_item_AddRef, user_item_Release,
    user_item_GetIids, user_item_GetRuntimeClassName, user_item_GetTrustLevel,
    user_item_get_DomainAccountName,
};

static HRESULT user_list_query( struct user_list *This, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) || IsEqualGUID( iid, &IID_IAgileObject ))
        *out = &This->op;
    else if (IsEqualGUID( iid, &IID_IAsyncOperation_IVectorView_User ))
        *out = &This->op;
    else if (IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_IVectorView_User ))
        *out = &This->op;
    else if (IsEqualGUID( iid, &IID_IVectorView_User ))
        *out = &This->view;
    else if (IsEqualGUID( iid, &IID_IAsyncInfo ))
        *out = &This->info;
    else
    {
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

#define USER_FWD(name, type, from) \
static HRESULT WINAPI name##_QueryInterface( type *iface, REFIID iid, void **out ) { return user_list_query( from( iface ), iid, out ); } \
static ULONG WINAPI name##_addref( type *iface ) { struct user_list *This = from( iface ); return InterlockedIncrement( &This->ref ); } \
static ULONG WINAPI name##_release( type *iface ) { struct user_list *This = from( iface ); ULONG ref = InterlockedDecrement( &This->ref ); \
    if (!ref) user_list_release( This ); return ref; } \
static HRESULT WINAPI name##_GetIids( type *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; } \
static HRESULT WINAPI name##_GetRuntimeClassName( type *iface, HSTRING *name ) { return WindowsCreateString( L"Windows.Foundation.IAsyncOperation", 34, name ); } \
static HRESULT WINAPI name##_GetTrustLevel( type *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }
USER_FWD(user_op, IAsyncOperation_IVectorView_User, user_from_op)
USER_FWD(user_view, IVectorView_User, user_from_view)
USER_FWD(user_info, IAsyncInfo, user_from_info)
static HRESULT WINAPI user_op_put_Completed( IAsyncOperation_IVectorView_User *iface,
        IAsyncOperationCompletedHandler_IVectorView_User *handler )
{
    struct user_list *This = user_from_op( iface );

    if (This->handler) IUnknown_Release( This->handler );
    This->handler = (IUnknown *)handler;
    if (handler)
    {
        IUnknown_AddRef( This->handler );
        IAsyncOperationCompletedHandler_IVectorView_User_Invoke( handler, iface, 1 /* AsyncStatus_Completed */ );
    }
    return S_OK;
}

static HRESULT WINAPI user_op_get_Completed( IAsyncOperation_IVectorView_User *iface,
        IAsyncOperationCompletedHandler_IVectorView_User **handler )
{
    struct user_list *This = user_from_op( iface );

    *handler = (IAsyncOperationCompletedHandler_IVectorView_User *)This->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI user_op_GetResults( IAsyncOperation_IVectorView_User *iface, IVectorView_User **results )
{
    struct user_list *This = user_from_op( iface );

    *results = &This->view;
    IInspectable_AddRef( (IInspectable *)*results );
    return S_OK;
}

static const struct __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUserVtbl user_op_vtbl =
{
    user_op_QueryInterface, user_op_addref, user_op_release,
    user_op_GetIids, user_op_GetRuntimeClassName, user_op_GetTrustLevel,
    user_op_put_Completed, user_op_get_Completed, user_op_GetResults,
};

static HRESULT WINAPI user_view_GetAt( IVectorView_User *iface, UINT32 index, IUser **item )
{
    struct user_list *This = user_from_view( iface );
    struct user_item *item_impl;

    if (index >= This->count) return E_INVALIDARG;
    if (!(item_impl = calloc( 1, sizeof(*item_impl) ))) return E_OUTOFMEMORY;
    item_impl->IUser_iface.lpVtbl = &user_item_vtbl;
    item_impl->list = This;
    item_impl->index = index;
    item_impl->ref = 1;
    InterlockedIncrement( &This->ref );
    *item = &item_impl->IUser_iface;
    return S_OK;
}

static HRESULT WINAPI user_view_get_Size( IVectorView_User *iface, UINT32 *size )
{
    struct user_list *This = user_from_view( iface );
    *size = This->count;
    return S_OK;
}

static HRESULT WINAPI user_view_IndexOf( IVectorView_User *iface, IUser *item, UINT32 *index, boolean *found )
{
    *index = 0;
    *found = false;
    return S_OK;
}

static const struct __FIVectorView_1_Windows__CSystem__CUserVtbl user_view_vtbl =
{
    user_view_QueryInterface, user_view_addref, user_view_release,
    user_view_GetIids, user_view_GetRuntimeClassName, user_view_GetTrustLevel,
    user_view_GetAt, user_view_get_Size, user_view_IndexOf,
};

static HRESULT WINAPI user_info_get_Id( IAsyncInfo *iface, UINT32 *id ) { *id = 1; return S_OK; }
static HRESULT WINAPI user_info_get_Status( IAsyncInfo *iface, enum AsyncStatus *status ) { *status = (enum AsyncStatus)1; /* Completed */ return S_OK; }
static HRESULT WINAPI user_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *code ) { *code = S_OK; return S_OK; }
static HRESULT WINAPI user_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI user_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const struct IAsyncInfoVtbl user_info_vtbl =
{
    user_info_QueryInterface, user_info_addref, user_info_release,
    user_info_GetIids, user_info_GetRuntimeClassName, user_info_GetTrustLevel,
    user_info_get_Id, user_info_get_Status, user_info_get_ErrorCode,
    user_info_Cancel, user_info_Close,
};

static void user_list_release( struct user_list *This )
{
    if (This->handler) IUnknown_Release( This->handler );
    free( This );
}

/* enumerate the AppModel registration, filter by family, answer with a
 * born-completed async operation over a single Windows.System.User */
static HRESULT find_users_async( const WCHAR *family, IAsyncOperation_IVectorView_User **operation )
{
    static const WCHAR appmodel_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages";
    WCHAR keyname[512], value[128], fam[512];
    struct user_list *list;
    HKEY hkey, hsub;
    DWORD i = 0;

    if (!operation) return E_INVALIDARG;
    if (!(list = calloc( 1, sizeof(*list) ))) return E_OUTOFMEMORY;
    list->op.lpVtbl = &user_op_vtbl;
    list->view.lpVtbl = &user_view_vtbl;
    list->info.lpVtbl = &user_info_vtbl;
    list->ref = 1;

    if (!RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_path, 0, KEY_READ, &hkey ))
    {
        while (list->count < ARRAY_SIZE(list->sid))
        {
            if (RegEnumKeyW( hkey, i++, keyname, ARRAY_SIZE(keyname) )) break;
            if (family)
            {
                UINT32 chars2 = ARRAY_SIZE(fam);
                lstrcpyW( fam, keyname );
                if (!PackageFamilyNameFromFullName( fam, &chars2, fam ) && lstrcmpiW( fam, family ))
                    continue;
            }
            lstrcpyW( value, L"S-1-5-21-0" );
            if (!RegOpenKeyExW( hkey, keyname, 0, KEY_READ, &hsub ))
            {
                DWORD type = REG_SZ, size = sizeof(value);
                RegQueryValueExW( hsub, L"UserSecurityId", NULL, &type, (BYTE *)value, &size );
                RegCloseKey( hsub );
            }
            lstrcpyW( list->sid[list->count], value );
            list->count++;
        }
        RegCloseKey( hkey );
    }
    if (!list->count)
    {
        lstrcpyW( list->sid[0], L"S-1-5-21-0" );
        list->count = 1;
    }

    *operation = &list->op;
    IInspectable_AddRef( (IInspectable *)*operation );
    return S_OK;
}

static HRESULT find_packages( const WCHAR *family, const WCHAR *name, const WCHAR *publisher,
                              PackageTypes types, IIterable_Package **packages )
{
    struct registration reg;
    struct package **items = NULL;
    IIterable_Package *iterable = NULL;
    HRESULT hr;
    ULONG i, n = 0;

    if (!packages) return E_INVALIDARG;
    registration_read( &reg, family, name, publisher );

    if (!(items = calloc( max( reg.count, 1 ), sizeof(*items) ))) return E_OUTOFMEMORY;
    for (i = 0; i < reg.count; i++)
    {
        BOOL keep = !types || (types == PackageTypes_All)
                    || (reg.records[i].framework ? (types & PackageTypes_Framework)
                                                 : (types & PackageTypes_Main));
        if (!keep) continue;
        if (FAILED( (hr = package_create( &reg.records[i], &items[n] ) ) )) goto done;
        n++;
    }
    if (FAILED( (hr = package_collection_create( items, n, &iterable ) ) )) goto done;
    *packages = iterable;
    hr = S_OK;
done:
    for (i = 0; i < n; i++) IPackage_Release( &items[i]->IPackage_iface );
    free( items );
    return hr;
}

/* One registered package by full name: the registration's path is the proof
 * the name is deployed; an unregistered name is E_NOT_FOUND, what Windows
 * hands the caller of FindPackageByPackageFullName. */
static HRESULT find_package_by_full_name( const WCHAR *full_name, IPackage **package )
{
    struct package_record rec;
    struct package *impl;
    UINT32 len;
    HRESULT hr;

    if (!full_name || !full_name[0]) return E_INVALIDARG;
    memset( &rec, 0, sizeof(rec) );
    len = ARRAY_SIZE(rec.path);
    if (GetPackagePathByFullName( full_name, &len, rec.path )) return E_NOT_FOUND;

    lstrcpynW( rec.full_name, full_name, ARRAY_SIZE(rec.full_name) );
    len = ARRAY_SIZE(rec.family_name);
    PackageFamilyNameFromFullName( full_name, &len, rec.family_name );

    if (FAILED( (hr = package_create( &rec, &impl ) ) )) return hr;
    *package = &impl->IPackage_iface;
    return S_OK;
}

struct package_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct package_manager_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IPackageManager *pm;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (FAILED( (hr = package_manager_create( &pm ) ) ))
    {
        *instance = NULL;
        return hr;
    }
    *instance = (IInspectable *)pm;
    return S_OK;
}

/* One PackageManager instance: stateless, every call reads the registration. */
HRESULT package_manager_create( IPackageManager **out )
{
    struct package_manager *impl;

    if (!out) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IPackageManager_iface.lpVtbl = &package_manager_vtbl;
    impl->IPackageManager2_iface.lpVtbl = &package_manager2_vtbl;
    impl->ref = 1;
    *out = &impl->IPackageManager_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

static struct package_manager_statics package_manager_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *package_manager_factory = &package_manager_statics.IActivationFactory_iface;

struct package_catalog
{
    IPackageCatalog IPackageCatalog_iface;
    IPackageCatalog2 IPackageCatalog2_iface;
    IPackageCatalog3 IPackageCatalog3_iface;
    LONG ref;
    IPackage *package;
};

static inline struct package_catalog *impl_from_IPackageCatalog( IPackageCatalog *iface )
{
    return CONTAINING_RECORD( iface, struct package_catalog, IPackageCatalog_iface );
}

static HRESULT WINAPI package_catalog_QueryInterface( IPackageCatalog *iface, REFIID iid, void **out )
{
    struct package_catalog *impl = impl_from_IPackageCatalog( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPackageCatalog ))
    {
        *out = &impl->IPackageCatalog_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPackageCatalog2 ))
    {
        *out = &impl->IPackageCatalog2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPackageCatalog3 ))
    {
        *out = &impl->IPackageCatalog3_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_catalog_AddRef( IPackageCatalog *iface )
{
    struct package_catalog *impl = impl_from_IPackageCatalog( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI package_catalog_Release( IPackageCatalog *iface )
{
    struct package_catalog *impl = impl_from_IPackageCatalog( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref)
    {
        if (impl->package) IPackage_Release( impl->package );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI package_catalog_GetIids( IPackageCatalog *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_catalog_GetRuntimeClassName( IPackageCatalog *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_PackageCatalog,
                                wcslen( RuntimeClass_Windows_ApplicationModel_PackageCatalog ),
                                class_name );
}

static HRESULT WINAPI package_catalog_GetTrustLevel( IPackageCatalog *iface, TrustLevel *trust_level )
{
    *trust_level = BaseTrust;
    return S_OK;
}

static LONG64 package_catalog_event_token = 1;

static inline HRESULT package_catalog_add_event( void *handler, EventRegistrationToken *token )
{
    if (!token) return E_INVALIDARG;
    token->value = InterlockedIncrement64( &package_catalog_event_token );
    return S_OK;
}

static HRESULT WINAPI package_catalog_add_PackageStaging( IPackageCatalog *iface, void *handler,
                                                          EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_catalog_add_event( handler, token );
}

static HRESULT WINAPI package_catalog_remove_PackageStaging( IPackageCatalog *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI package_catalog_add_PackageInstalling( IPackageCatalog *iface, void *handler,
                                                             EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_catalog_add_event( handler, token );
}

static HRESULT WINAPI package_catalog_remove_PackageInstalling( IPackageCatalog *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI package_catalog_add_PackageUpdating( IPackageCatalog *iface, void *handler,
                                                           EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_catalog_add_event( handler, token );
}

static HRESULT WINAPI package_catalog_remove_PackageUpdating( IPackageCatalog *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI package_catalog_add_PackageUninstalling( IPackageCatalog *iface, void *handler,
                                                               EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_catalog_add_event( handler, token );
}

static HRESULT WINAPI package_catalog_remove_PackageUninstalling( IPackageCatalog *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI package_catalog_add_PackageStatusChanged( IPackageCatalog *iface, void *handler,
                                                                EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_catalog_add_event( handler, token );
}

static HRESULT WINAPI package_catalog_remove_PackageStatusChanged( IPackageCatalog *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return S_OK;
}

static const struct IPackageCatalogVtbl package_catalog_vtbl =
{
    package_catalog_QueryInterface,
    package_catalog_AddRef,
    package_catalog_Release,
    /* IInspectable methods */
    package_catalog_GetIids,
    package_catalog_GetRuntimeClassName,
    package_catalog_GetTrustLevel,
    /* IPackageCatalog methods */
    package_catalog_add_PackageStaging,
    package_catalog_remove_PackageStaging,
    package_catalog_add_PackageInstalling,
    package_catalog_remove_PackageInstalling,
    package_catalog_add_PackageUpdating,
    package_catalog_remove_PackageUpdating,
    package_catalog_add_PackageUninstalling,
    package_catalog_remove_PackageUninstalling,
    package_catalog_add_PackageStatusChanged,
    package_catalog_remove_PackageStatusChanged,
};

DEFINE_IINSPECTABLE( package_catalog2, IPackageCatalog2, struct package_catalog, IPackageCatalog_iface );

static HRESULT WINAPI package_catalog2_add_PackageContentGroupStaging( IPackageCatalog2 *iface, void *handler,
                                                                       EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return package_catalog_add_event( handler, token );
}

static HRESULT WINAPI package_catalog2_remove_PackageContentGroupStaging( IPackageCatalog2 *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI package_catalog2_AddOptionalPackageAsync( IPackageCatalog2 *iface, HSTRING name, void **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    if (operation) *operation = NULL;
    return E_NOTIMPL;
}

static const struct IPackageCatalog2Vtbl package_catalog2_vtbl =
{
    package_catalog2_QueryInterface,
    package_catalog2_AddRef,
    package_catalog2_Release,
    /* IInspectable methods */
    package_catalog2_GetIids,
    package_catalog2_GetRuntimeClassName,
    package_catalog2_GetTrustLevel,
    /* IPackageCatalog2 methods */
    package_catalog2_add_PackageContentGroupStaging,
    package_catalog2_remove_PackageContentGroupStaging,
    package_catalog2_AddOptionalPackageAsync,
};

DEFINE_IINSPECTABLE( package_catalog3, IPackageCatalog3, struct package_catalog, IPackageCatalog_iface );

static HRESULT WINAPI package_catalog3_RemoveOptionalPackagesAsync( IPackageCatalog3 *iface, void *names, void **operation )
{
    FIXME( "iface %p, names %p, operation %p stub!\n", iface, names, operation );
    if (operation) *operation = NULL;
    return E_NOTIMPL;
}

static const struct IPackageCatalog3Vtbl package_catalog3_vtbl =
{
    package_catalog3_QueryInterface,
    package_catalog3_AddRef,
    package_catalog3_Release,
    /* IInspectable methods */
    package_catalog3_GetIids,
    package_catalog3_GetRuntimeClassName,
    package_catalog3_GetTrustLevel,
    /* IPackageCatalog3 methods */
    package_catalog3_RemoveOptionalPackagesAsync,
};

static HRESULT package_catalog_create( IPackage *package, IPackageCatalog **out )
{
    struct package_catalog *impl;

    if (!out) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IPackageCatalog_iface.lpVtbl = &package_catalog_vtbl;
    impl->IPackageCatalog2_iface.lpVtbl = &package_catalog2_vtbl;
    impl->IPackageCatalog3_iface.lpVtbl = &package_catalog3_vtbl;
    impl->ref = 1;
    if (package)
    {
        impl->package = package;
        IPackage_AddRef( package );
    }

    *out = &impl->IPackageCatalog_iface;
    return S_OK;
}

struct package_catalog_statics
{
    IActivationFactory IActivationFactory_iface;
    IPackageCatalogStatics IPackageCatalogStatics_iface;
    IPackageCatalogStatics2 IPackageCatalogStatics2_iface;
    LONG ref;
};

static inline struct package_catalog_statics *impl_catalog_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct package_catalog_statics, IActivationFactory_iface );
}

static HRESULT WINAPI package_catalog_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct package_catalog_statics *impl = impl_catalog_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPackageCatalogStatics ))
    {
        *out = &impl->IPackageCatalogStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPackageCatalogStatics2 ))
    {
        *out = &impl->IPackageCatalogStatics2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_catalog_factory_AddRef( IActivationFactory *iface )
{
    struct package_catalog_statics *impl = impl_catalog_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI package_catalog_factory_Release( IActivationFactory *iface )
{
    struct package_catalog_statics *impl = impl_catalog_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI package_catalog_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_catalog_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_catalog_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_catalog_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IPackageCatalog *catalog;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (FAILED( (hr = package_catalog_create( NULL, &catalog )) ))
    {
        *instance = NULL;
        return hr;
    }
    *instance = (IInspectable *)catalog;
    return S_OK;
}

static const struct IActivationFactoryVtbl package_catalog_factory_vtbl =
{
    package_catalog_factory_QueryInterface,
    package_catalog_factory_AddRef,
    package_catalog_factory_Release,
    /* IInspectable methods */
    package_catalog_factory_GetIids,
    package_catalog_factory_GetRuntimeClassName,
    package_catalog_factory_GetTrustLevel,
    /* IActivationFactory methods */
    package_catalog_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( package_catalog_statics, IPackageCatalogStatics, struct package_catalog_statics, IActivationFactory_iface );

static HRESULT WINAPI package_catalog_statics_OpenForCurrentPackage( IPackageCatalogStatics *iface, IPackageCatalog **catalog )
{
    TRACE( "iface %p, catalog %p.\n", iface, catalog );
    return package_catalog_create( NULL, catalog );
}

static HRESULT WINAPI package_catalog_statics_OpenForCurrentUser( IPackageCatalogStatics *iface, IPackageCatalog **catalog )
{
    TRACE( "iface %p, catalog %p.\n", iface, catalog );
    return package_catalog_create( NULL, catalog );
}

static HRESULT WINAPI package_catalog_statics_OpenForPackage( IPackageCatalogStatics *iface, IPackage *package, IPackageCatalog **catalog )
{
    TRACE( "iface %p, package %p, catalog %p.\n", iface, package, catalog );
    return package_catalog_create( package, catalog );
}

static const struct IPackageCatalogStaticsVtbl package_catalog_statics_vtbl =
{
    package_catalog_statics_QueryInterface,
    package_catalog_statics_AddRef,
    package_catalog_statics_Release,
    /* IInspectable methods */
    package_catalog_statics_GetIids,
    package_catalog_statics_GetRuntimeClassName,
    package_catalog_statics_GetTrustLevel,
    /* IPackageCatalogStatics methods */
    package_catalog_statics_OpenForCurrentPackage,
    package_catalog_statics_OpenForCurrentUser,
    package_catalog_statics_OpenForPackage,
};

DEFINE_IINSPECTABLE( package_catalog_statics2, IPackageCatalogStatics2, struct package_catalog_statics, IActivationFactory_iface );

static HRESULT WINAPI package_catalog_statics2_OpenForPackage( IPackageCatalogStatics2 *iface, IPackage *package, IPackageCatalog **catalog )
{
    TRACE( "iface %p, package %p, catalog %p.\n", iface, package, catalog );
    return package_catalog_create( package, catalog );
}

static const struct IPackageCatalogStatics2Vtbl package_catalog_statics2_vtbl =
{
    package_catalog_statics2_QueryInterface,
    package_catalog_statics2_AddRef,
    package_catalog_statics2_Release,
    /* IInspectable methods */
    package_catalog_statics2_GetIids,
    package_catalog_statics2_GetRuntimeClassName,
    package_catalog_statics2_GetTrustLevel,
    /* IPackageCatalogStatics2 methods */
    package_catalog_statics2_OpenForPackage,
};

static struct package_catalog_statics package_catalog_statics =
{
    {&package_catalog_factory_vtbl},
    {&package_catalog_statics_vtbl},
    {&package_catalog_statics2_vtbl},
    1,
};

IActivationFactory *package_catalog_factory = &package_catalog_statics.IActivationFactory_iface;
