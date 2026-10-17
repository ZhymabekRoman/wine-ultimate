/* WinRT Windows.Storage.ApplicationData ApplicationData Implementation
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

WINE_DEFAULT_DEBUG_CHANNEL(data);

struct application_data_statics
{
    IActivationFactory IActivationFactory_iface;
    IApplicationDataStatics IApplicationDataStatics_iface;
    LONG ref;
};

static inline struct application_data_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IApplicationDataStatics ))
    {
        *out = &impl->IApplicationDataStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );
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
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
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

struct application_data
{
    IApplicationData IApplicationData_iface;
    IApplicationData2 IApplicationData2_iface;
    LONG ref;
};

static inline struct application_data *impl_from_IApplicationData( IApplicationData *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData_iface );
}

static inline struct application_data *impl_from_IApplicationData2( IApplicationData2 *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData2_iface );
}

static HRESULT WINAPI application_data_QueryInterface( IApplicationData *iface, REFIID iid, void **out )
{
    struct application_data *impl = impl_from_IApplicationData( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplicationData ))
    {
        *out = &impl->IApplicationData_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IApplicationData2 ))
    {
        *out = &impl->IApplicationData2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI application_data_AddRef( IApplicationData *iface )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI application_data_Release( IApplicationData *iface )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI application_data_GetIids( IApplicationData *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_GetRuntimeClassName( IApplicationData *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_GetTrustLevel( IApplicationData *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_get_Version( IApplicationData *iface, UINT32 *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_SetVersionAsync( IApplicationData *iface, UINT32 version, IApplicationDataSetVersionHandler *handler,
                                                        IAsyncAction **operation )
{
    FIXME( "iface %p, version %d, handler %p, operation %p stub!\n", iface, version, handler, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_ClearAllAsync( IApplicationData *iface, IAsyncAction **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_ClearAsync( IApplicationData *iface, ApplicationDataLocality locality, IAsyncAction **operation )
{
    FIXME( "iface %p, %d locality, operation %p stub!\n", iface, locality, operation );
    return E_NOTIMPL;
}

struct application_data_container
{
    IApplicationDataContainer IApplicationDataContainer_iface;
    LONG ref;
    ApplicationDataLocality locality;
    IPropertySet *values;
};

static inline struct application_data_container *impl_from_IApplicationDataContainer( IApplicationDataContainer *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_container, IApplicationDataContainer_iface );
}

static HRESULT WINAPI container_QueryInterface( IApplicationDataContainer *iface, REFIID iid, void **out )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplicationDataContainer ))
    {
        *out = &impl->IApplicationDataContainer_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "iid %s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI container_AddRef( IApplicationDataContainer *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI container_Release( IApplicationDataContainer *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->values) IPropertySet_Release( impl->values );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI container_GetIids( IApplicationDataContainer *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI container_GetRuntimeClassName( IApplicationDataContainer *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.ApplicationDataContainer", 39, name ); }
static HRESULT WINAPI container_GetTrustLevel( IApplicationDataContainer *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI container_get_Name( IApplicationDataContainer *iface, HSTRING *value )
{ return WindowsCreateString( L"LocalSettings", 13, value ); }

static HRESULT WINAPI container_get_Locality( IApplicationDataContainer *iface, ApplicationDataLocality *value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    if (!value) return E_POINTER;
    *value = impl->locality;
    return S_OK;
}

static HRESULT WINAPI container_get_Values( IApplicationDataContainer *iface, IPropertySet **value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    if (!value) return E_POINTER;
    if (!impl->values)
    {
        static const WCHAR name[] = L"Windows.Foundation.Collections.PropertySet";
        HSTRING class_id;
        WindowsCreateString( name, wcslen(name), &class_id );
        RoActivateInstance( class_id, (IInspectable **)&impl->values );
        WindowsDeleteString( class_id );
    }
    *value = impl->values;
    if (impl->values) IPropertySet_AddRef( impl->values );
    return *value ? S_OK : E_FAIL;
}

static HRESULT WINAPI container_get_Containers( IApplicationDataContainer *iface, IMapView_HSTRING_ApplicationDataContainer **value )
{
    FIXME( "stub\n" );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT container_create( ApplicationDataLocality locality, IApplicationDataContainer **out );

static HRESULT WINAPI container_CreateContainer( IApplicationDataContainer *iface, HSTRING name, ApplicationDataCreateDisposition disposition, IApplicationDataContainer **container )
{
    struct application_data_container *parent = impl_from_IApplicationDataContainer( iface );
    TRACE( "iface %p, name %s, disposition %d, container %p\n", iface, debugstr_hstring(name), disposition, container );
    if (!container) return E_POINTER;
    return container_create( parent->locality, container );
}

static HRESULT WINAPI container_DeleteContainer( IApplicationDataContainer *iface, HSTRING name )
{
    FIXME( "stub\n" );
    return S_OK;
}

static const struct IApplicationDataContainerVtbl container_vtbl =
{
    container_QueryInterface,
    container_AddRef,
    container_Release,
    container_GetIids,
    container_GetRuntimeClassName,
    container_GetTrustLevel,
    container_get_Name,
    container_get_Locality,
    container_get_Values,
    container_get_Containers,
    container_CreateContainer,
    container_DeleteContainer,
};

static HRESULT container_create( ApplicationDataLocality locality, IApplicationDataContainer **out )
{
    struct application_data_container *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IApplicationDataContainer_iface.lpVtbl = &container_vtbl;
    impl->ref = 1;
    impl->locality = locality;
    *out = &impl->IApplicationDataContainer_iface;
    return S_OK;
}

static HRESULT WINAPI application_data_get_LocalSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    static IApplicationDataContainer *local_settings;
    if (!value) return E_POINTER;
    if (!local_settings) container_create( ApplicationDataLocality_Local, &local_settings );
    *value = local_settings;
    if (local_settings) IApplicationDataContainer_AddRef( local_settings );
    return *value ? S_OK : E_FAIL;
}

static HRESULT WINAPI application_data_get_RoamingSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    static IApplicationDataContainer *roaming_settings;
    if (!value) return E_POINTER;
    if (!roaming_settings) container_create( ApplicationDataLocality_Roaming, &roaming_settings );
    *value = roaming_settings;
    if (roaming_settings) IApplicationDataContainer_AddRef( roaming_settings );
    return *value ? S_OK : E_FAIL;
}

struct storage_folder
{
    IStorageFolder IStorageFolder_iface;
    IStorageItem IStorageItem_iface;
    LONG ref;
    WCHAR path[MAX_PATH];
};

static inline struct storage_folder *impl_from_IStorageFolder( IStorageFolder *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageFolder_iface );
}

static inline struct storage_folder *impl_from_IStorageItem( IStorageItem *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageItem_iface );
}

static HRESULT WINAPI storage_folder_QueryInterface( IStorageFolder *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStorageFolder ))
    {
        *out = &impl->IStorageFolder_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStorageItem ))
    {
        *out = &impl->IStorageItem_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "iid %s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI storage_folder_AddRef( IStorageFolder *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI storage_folder_Release( IStorageFolder *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI storage_folder_GetIids( IStorageFolder *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetRuntimeClassName( IStorageFolder *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.StorageFolder", 29, name ); }
static HRESULT WINAPI storage_folder_GetTrustLevel( IStorageFolder *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI storage_folder_CreateFileAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_CreateFileAsync( IStorageFolder *iface, HSTRING name, CreationCollisionOption collision, IAsyncOperation_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_CreateFolderAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_CreateFolderAsync( IStorageFolder *iface, HSTRING name, CreationCollisionOption collision, IAsyncOperation_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFileAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFolderAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetItemAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_IStorageItem **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface, IAsyncOperation_IVectorView_StorageFile **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface, IAsyncOperation_IVectorView_StorageFolder **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_folder_GetItemsAsyncOverloadDefaultStartAndCount( IStorageFolder *iface, IAsyncOperation_IVectorView_IStorageItem **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }

static const struct IStorageFolderVtbl storage_folder_vtbl =
{
    storage_folder_QueryInterface,
    storage_folder_AddRef,
    storage_folder_Release,
    storage_folder_GetIids,
    storage_folder_GetRuntimeClassName,
    storage_folder_GetTrustLevel,
    storage_folder_CreateFileAsyncOverloadDefaultOptions,
    storage_folder_CreateFileAsync,
    storage_folder_CreateFolderAsyncOverloadDefaultOptions,
    storage_folder_CreateFolderAsync,
    storage_folder_GetFileAsync,
    storage_folder_GetFolderAsync,
    storage_folder_GetItemAsync,
    storage_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_GetItemsAsyncOverloadDefaultStartAndCount,
};

static HRESULT WINAPI storage_item_QueryInterface( IStorageItem *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return IStorageFolder_QueryInterface( &impl->IStorageFolder_iface, iid, out );
}
static ULONG WINAPI storage_item_AddRef( IStorageItem *iface )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return IStorageFolder_AddRef( &impl->IStorageFolder_iface );
}
static ULONG WINAPI storage_item_Release( IStorageItem *iface )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return IStorageFolder_Release( &impl->IStorageFolder_iface );
}
static HRESULT WINAPI storage_item_GetIids( IStorageItem *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_GetRuntimeClassName( IStorageItem *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Storage.StorageFolder", 29, name ); }
static HRESULT WINAPI storage_item_GetTrustLevel( IStorageItem *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI storage_item_RenameAsyncOverloadDefaultOptions( IStorageItem *iface, HSTRING name, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_RenameAsync( IStorageItem *iface, HSTRING name, NameCollisionOption collision, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_DeleteAsyncOverloadDefaultOptions( IStorageItem *iface, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_DeleteAsync( IStorageItem *iface, StorageDeleteOption option, IAsyncAction **action )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_GetBasicPropertiesAsync( IStorageItem *iface, IAsyncOperation_BasicProperties **operation )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_get_Name( IStorageItem *iface, HSTRING *value )
{ return WindowsCreateString( L"LocalState", 10, value ); }
static HRESULT WINAPI storage_item_get_Path( IStorageItem *iface, HSTRING *value )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    return WindowsCreateString( impl->path, wcslen(impl->path), value );
}
static HRESULT WINAPI storage_item_get_Attributes( IStorageItem *iface, FileAttributes *value )
{ if (value) *value = FileAttributes_Directory; return S_OK; }
static HRESULT WINAPI storage_item_get_DateCreated( IStorageItem *iface, DateTime *value )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI storage_item_IsOfType( IStorageItem *iface, StorageItemTypes type, boolean *value )
{ if (value) *value = (type == StorageItemTypes_Folder); return S_OK; }

static const struct IStorageItemVtbl storage_item_vtbl =
{
    storage_item_QueryInterface,
    storage_item_AddRef,
    storage_item_Release,
    storage_item_GetIids,
    storage_item_GetRuntimeClassName,
    storage_item_GetTrustLevel,
    storage_item_RenameAsyncOverloadDefaultOptions,
    storage_item_RenameAsync,
    storage_item_DeleteAsyncOverloadDefaultOptions,
    storage_item_DeleteAsync,
    storage_item_GetBasicPropertiesAsync,
    storage_item_get_Name,
    storage_item_get_Path,
    storage_item_get_Attributes,
    storage_item_get_DateCreated,
    storage_item_IsOfType,
};

static HRESULT storage_folder_create( const WCHAR *path, IStorageFolder **out )
{
    struct storage_folder *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IStorageFolder_iface.lpVtbl = &storage_folder_vtbl;
    impl->IStorageItem_iface.lpVtbl = &storage_item_vtbl;
    impl->ref = 1;
    lstrcpynW( impl->path, path, ARRAY_SIZE(impl->path) );
    CreateDirectoryW( impl->path, NULL );
    *out = &impl->IStorageFolder_iface;
    return S_OK;
}

static HRESULT WINAPI application_data_get_LocalFolder( IApplicationData *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    if (!value) return E_POINTER;
    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", path, ARRAY_SIZE(path) ))
    {
        if (!GetEnvironmentVariableW( L"USERPROFILE", path, ARRAY_SIZE(path) ))
            return E_FAIL;
        lstrcatW( path, L"\\AppData\\Local" );
    }
    return storage_folder_create( path, value );
}

static HRESULT WINAPI application_data_get_RoamingFolder( IApplicationData *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    if (!value) return E_POINTER;
    if (!GetEnvironmentVariableW( L"APPDATA", path, ARRAY_SIZE(path) ))
    {
        if (!GetEnvironmentVariableW( L"USERPROFILE", path, ARRAY_SIZE(path) ))
            return E_FAIL;
        lstrcatW( path, L"\\AppData\\Roaming" );
    }
    return storage_folder_create( path, value );
}

static HRESULT WINAPI application_data_get_TemporaryFolder( IApplicationData *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    if (!value) return E_POINTER;
    GetTempPathW( ARRAY_SIZE(path), path );
    return storage_folder_create( path, value );
}

static HRESULT WINAPI application_data_add_DataChanged( IApplicationData *iface, ITypedEventHandler_ApplicationData_IInspectable *handler,
                                                        EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_remove_DataChanged( IApplicationData *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_SignalDataChanged( IApplicationData *iface )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_get_RoamingStorageQuota( IApplicationData *iface, UINT64 *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IApplicationDataVtbl application_data_vtbl =
{
    application_data_QueryInterface,
    application_data_AddRef,
    application_data_Release,
    /* IInspectable methods */
    application_data_GetIids,
    application_data_GetRuntimeClassName,
    application_data_GetTrustLevel,
    /* IApplicationData methods */
    application_data_get_Version,
    application_data_SetVersionAsync,
    application_data_ClearAllAsync,
    application_data_ClearAsync,
    application_data_get_LocalSettings,
    application_data_get_RoamingSettings,
    application_data_get_LocalFolder,
    application_data_get_RoamingFolder,
    application_data_get_TemporaryFolder,
    application_data_add_DataChanged,
    application_data_remove_DataChanged,
    application_data_SignalDataChanged,
    application_data_get_RoamingStorageQuota,
};

static HRESULT WINAPI application_data2_QueryInterface( IApplicationData2 *iface, REFIID iid, void **out )
{
    struct application_data *impl = impl_from_IApplicationData2( iface );
    return IApplicationData_QueryInterface( &impl->IApplicationData_iface, iid, out );
}
static ULONG WINAPI application_data2_AddRef( IApplicationData2 *iface )
{
    struct application_data *impl = impl_from_IApplicationData2( iface );
    return IApplicationData_AddRef( &impl->IApplicationData_iface );
}
static ULONG WINAPI application_data2_Release( IApplicationData2 *iface )
{
    struct application_data *impl = impl_from_IApplicationData2( iface );
    return IApplicationData_Release( &impl->IApplicationData_iface );
}
static HRESULT WINAPI application_data2_GetIids( IApplicationData2 *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "stub\n" ); return E_NOTIMPL;
}
static HRESULT WINAPI application_data2_GetRuntimeClassName( IApplicationData2 *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.ApplicationData", 30, class_name );
}
static HRESULT WINAPI application_data2_GetTrustLevel( IApplicationData2 *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust; return S_OK;
}
static HRESULT WINAPI application_data2_get_LocalCacheFolder( IApplicationData2 *iface, IStorageFolder **value )
{
    WCHAR path[MAX_PATH];
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", path, ARRAY_SIZE(path) ))
    {
        if (!GetEnvironmentVariableW( L"USERPROFILE", path, ARRAY_SIZE(path) ))
            return E_FAIL;
        lstrcatW( path, L"\\AppData\\Local" );
    }
    lstrcatW( path, L"\\WhatsApp\\LocalCache" );
    return storage_folder_create( path, value );
}

static const struct IApplicationData2Vtbl application_data2_vtbl =
{
    application_data2_QueryInterface,
    application_data2_AddRef,
    application_data2_Release,
    /* IInspectable methods */
    application_data2_GetIids,
    application_data2_GetRuntimeClassName,
    application_data2_GetTrustLevel,
    /* IApplicationData2 methods */
    application_data2_get_LocalCacheFolder,
};

DEFINE_IINSPECTABLE( application_data_statics, IApplicationDataStatics, struct application_data_statics, IActivationFactory_iface )

static HRESULT WINAPI application_data_statics_get_Current( IApplicationDataStatics *iface, IApplicationData **value )
{
    struct application_data *impl;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IApplicationData_iface.lpVtbl = &application_data_vtbl;
    impl->IApplicationData2_iface.lpVtbl = &application_data2_vtbl;
    impl->ref = 1;

    *value = &impl->IApplicationData_iface;
    TRACE( "created IApplicationData %p.\n", *value );
    return S_OK;
}

static const struct IApplicationDataStaticsVtbl application_data_statics_vtbl =
{
    application_data_statics_QueryInterface,
    application_data_statics_AddRef,
    application_data_statics_Release,
    /* IInspectable methods */
    application_data_statics_GetIids,
    application_data_statics_GetRuntimeClassName,
    application_data_statics_GetTrustLevel,
    /* IApplicationDataStatics methods */
    application_data_statics_get_Current,
};

static struct application_data_statics application_data_statics =
{
    {&factory_vtbl},
    {&application_data_statics_vtbl},
    1,
};

IActivationFactory *application_data_factory = &application_data_statics.IActivationFactory_iface;
