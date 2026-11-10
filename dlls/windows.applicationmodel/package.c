/* WinRT Windows.ApplicationModel Package Implementation
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

#include <string.h>

#include "private.h"
#include "objbase.h"
#include "wine/debug.h"

#include "pathcch.h"

WINE_DEFAULT_DEBUG_CHANNEL(model);

struct package_statics
{
    IActivationFactory IActivationFactory_iface;
    IPackageStatics IPackageStatics_iface;
    IPackage IPackage_iface;
    LONG ref;
};

static inline struct package_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct package_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct package_statics *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IPackageStatics ))
    {
        *out = &impl->IPackageStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* Windows aggregates the class default interface (IPackage) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IPackage ))
    {
        *out = &impl->IPackage_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct package_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct package_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IPackageStatics,
        IID_IPackage,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Package, lstrlenW( RuntimeClass_Windows_ApplicationModel_Package ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
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

static const WCHAR appmodel_reg_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel";

static BOOL package_get_reg_str( const WCHAR *prop_name, WCHAR *buf, DWORD cch )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR subkey[576], name[MAX_PATH], exe[MAX_PATH];
    SIZE_T i;

    if (GetModuleFileNameW( NULL, exe, ARRAY_SIZE(exe) ))
    {
        WCHAR *p = wcsrchr( exe, '\\' );
        const WCHAR *exe_name = p ? p + 1 : exe;
        lstrcpyW( subkey, appmodel_reg_key );
        lstrcatW( subkey, L"\\Exes\\" );
        lstrcatW( subkey, exe_name );
        for (i = 0; i < ARRAY_SIZE(roots); i++)
        {
            HKEY key;
            DWORD type, size = cch * sizeof(WCHAR);
            if (!RegOpenKeyExW( roots[i], subkey, 0, KEY_READ, &key ))
            {
                if (!RegQueryValueExW( key, prop_name, 0, &type, (BYTE *)buf, &size ) && type == REG_SZ && size)
                {
                    RegCloseKey( key );
                    return TRUE;
                }
                size = sizeof(name);
                if (!RegQueryValueExW( key, L"PackageFullName", 0, &type, (BYTE *)name, &size ) && type == REG_SZ && size)
                {
                    HKEY pkg_key;
                    WCHAR pkg_subkey[576];
                    lstrcpyW( pkg_subkey, appmodel_reg_key );
                    lstrcatW( pkg_subkey, L"\\Packages\\" );
                    lstrcatW( pkg_subkey, name );
                    if (!RegOpenKeyExW( roots[i], pkg_subkey, 0, KEY_READ, &pkg_key ))
                    {
                        DWORD psize = cch * sizeof(WCHAR);
                        if (!RegQueryValueExW( pkg_key, prop_name, 0, &type, (BYTE *)buf, &psize ) && type == REG_SZ && psize)
                        {
                            RegCloseKey( pkg_key );
                            RegCloseKey( key );
                            return TRUE;
                        }
                        RegCloseKey( pkg_key );
                    }
                }
                RegCloseKey( key );
            }
        }
    }

    if (GetEnvironmentVariableW( L"WINE_PACKAGE_FULL_NAME", name, ARRAY_SIZE(name) ))
    {
        lstrcpyW( subkey, appmodel_reg_key );
        lstrcatW( subkey, L"\\Packages\\" );
        lstrcatW( subkey, name );
        for (i = 0; i < ARRAY_SIZE(roots); i++)
        {
            HKEY key;
            DWORD type, size = cch * sizeof(WCHAR);
            if (!RegOpenKeyExW( roots[i], subkey, 0, KEY_READ, &key ))
            {
                if (!RegQueryValueExW( key, prop_name, 0, &type, (BYTE *)buf, &size ) && type == REG_SZ && size)
                {
                    RegCloseKey( key );
                    return TRUE;
                }
                RegCloseKey( key );
            }
        }
    }
    return FALSE;
}

/* Install directory of the running package: env override first (per-run),
 * then the AppModel registration (PackagePath, then the installer-recorded
 * PackageRootPath), then the executable directory as the last resort. Same
 * resolution order as the kernel32 package identity helpers. */
static void package_root_path( WCHAR *buf, DWORD cch )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR subkey[576], name[MAX_PATH];
    SIZE_T i;

    if (GetEnvironmentVariableW( L"WINE_PACKAGE_PATH", buf, cch )) return;

    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_FULL_NAME", name, ARRAY_SIZE(name) ) &&
        !package_get_reg_str( L"PackageFullName", name, ARRAY_SIZE(name) ))
        name[0] = 0;

    if (name[0])
    {
        wcscpy( subkey, appmodel_reg_key );
        lstrcatW( subkey, L"\\Packages\\" );
        lstrcatW( subkey, name );

        for (i = 0; i < ARRAY_SIZE(roots); i++)
        {
            static const WCHAR *const values[] = { L"PackagePath", L"PackageRootPath" };
            HKEY key;
            SIZE_T j;

            if (RegOpenKeyExW( roots[i], subkey, 0, KEY_READ, &key )) continue;
            for (j = 0; j < ARRAY_SIZE(values); j++)
            {
                DWORD type, size = cch * sizeof(WCHAR);
                if (!RegQueryValueExW( key, values[j], 0, &type, (BYTE *)buf, &size ) && type == REG_SZ && size)
                {
                    RegCloseKey( key );
                    return;
                }
            }
            RegCloseKey( key );
        }
    }

    if (!GetModuleFileNameW( NULL, buf, cch )) buf[0] = 0;
    else PathCchRemoveFileSpec( buf, cch );
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

static HRESULT WINAPI storage_folder_QueryInterface( IStorageFolder *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IStorageFolder ))
    {
        *out = &impl->IStorageFolder_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStorageItem ))
    {
        *out = &impl->IStorageItem_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI storage_folder_AddRef( IStorageFolder *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI storage_folder_Release( IStorageFolder *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI storage_folder_GetIids( IStorageFolder *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IStorageFolder,
        IID_IStorageItem,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI storage_folder_GetRuntimeClassName( IStorageFolder *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_GetTrustLevel( IStorageFolder *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_CreateFileAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name,
                                                                            IAsyncOperation_StorageFile **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_CreateFileAsync( IStorageFolder *iface, HSTRING name, CreationCollisionOption options,
                                                      IAsyncOperation_StorageFile **operation )
{
    FIXME( "iface %p, name %s, options %d, operation %p stub!\n", iface, debugstr_hstring(name), options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_CreateFolderAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name,
                                                                              IAsyncOperation_StorageFolder **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_CreateFolderAsync( IStorageFolder *iface, HSTRING name, CreationCollisionOption options,
                                                        IAsyncOperation_StorageFolder **operation )
{
    FIXME( "iface %p, name %s, options %d, operation %p stub!\n", iface, debugstr_hstring(name), options, operation );
    return E_NOTIMPL;
}

static HRESULT create_async_result( struct storage_folder *folder, int kind, HSTRING name, IInspectable **ret );

static HRESULT WINAPI storage_folder_GetFileAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFile **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );

    return create_async_result( impl, 0, name, (IInspectable **)operation );
}

static HRESULT WINAPI storage_folder_GetFolderAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFolder **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_GetItemAsync( IStorageFolder *iface, HSTRING name, IAsyncOperation_IStorageItem **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );

    return create_async_result( impl, 1, name, (IInspectable **)operation );
}

static HRESULT WINAPI storage_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface,
                                                                                       IAsyncOperation_IVectorView_StorageFile **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );

    return create_async_result( impl, 2, NULL, (IInspectable **)operation );
}

static HRESULT WINAPI storage_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface,
                                                                                       IAsyncOperation_IVectorView_StorageFolder **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );

    return create_async_result( impl, 3, NULL, (IInspectable **)operation );
}

static HRESULT WINAPI storage_folder_GetItemsAsyncOverloadDefaultStartAndCount( IStorageFolder *iface,
                                                                                IAsyncOperation_IVectorView_IStorageItem **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static const struct IStorageFolderVtbl storage_folder_vtbl =
{
    storage_folder_QueryInterface,
    storage_folder_AddRef,
    storage_folder_Release,
    /* IInspectable methods */
    storage_folder_GetIids,
    storage_folder_GetRuntimeClassName,
    storage_folder_GetTrustLevel,
    /* IStorageFolder methods */
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

DEFINE_IINSPECTABLE( storage_item, IStorageItem, struct storage_folder, IStorageFolder_iface )

static HRESULT WINAPI storage_item_RenameAsyncOverloadDefaultOptions( IStorageItem *iface, HSTRING name, IAsyncAction **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_RenameAsync( IStorageItem *iface, HSTRING name, NameCollisionOption option, IAsyncAction **operation )
{
    FIXME( "iface %p, name %s, option %d, operation %p stub!\n", iface, debugstr_hstring(name), option, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_DeleteAsyncOverloadDefaultOptions( IStorageItem *iface, IAsyncAction **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_DeleteAsync( IStorageItem *iface, StorageDeleteOption option, IAsyncAction **operation )
{
    FIXME( "iface %p, option %d, operation %p stub!\n", iface, option, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_GetBasicPropertiesAsync( IStorageItem *iface, IAsyncOperation_BasicProperties **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_get_Name( IStorageItem *iface, HSTRING *value )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );
    WCHAR name[MAX_PATH];
    const WCHAR *base;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!impl->path[0]) package_root_path( impl->path, ARRAY_SIZE(impl->path) );
    if (!(base = wcsrchr( impl->path, L'\\' ))) base = impl->path;
    else base = base[1] ? base + 1 : base;
    lstrcpyW( name, base );
    return WindowsCreateString( name, wcslen(name), value );
}

static HRESULT WINAPI storage_item_get_Path( IStorageItem *iface, HSTRING *value )
{
    struct storage_folder *impl = impl_from_IStorageItem( iface );

    TRACE( "iface %p, value %p\n", iface, value );

    if (!impl->path[0]) package_root_path( impl->path, ARRAY_SIZE(impl->path) );
    return WindowsCreateString( impl->path, wcslen(impl->path), value );
}

static HRESULT WINAPI storage_item_get_Attributes( IStorageItem *iface, FileAttributes *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_get_DateCreated( IStorageItem *iface, DateTime *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_IsOfType( IStorageItem *iface, StorageItemTypes type, boolean *value )
{
    FIXME( "iface %p, type %d, value %p stub!\n", iface, type, value );
    return E_NOTIMPL;
}


static const struct IStorageItemVtbl storage_item_vtbl =
{
    storage_item_QueryInterface,
    storage_item_AddRef,
    storage_item_Release,
    /* IInspectable methods */
    storage_item_GetIids,
    storage_item_GetRuntimeClassName,
    storage_item_GetTrustLevel,
    /* IStorageItem methods */
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

struct storageFile
{
    IStorageFile IStorageFile_iface;
    IStorageItem IStorageItem_iface;
    LONG ref;
    WCHAR path[MAX_PATH];
};

static inline struct storageFile *impl_from_IStorageFile( IStorageFile *iface )
{
    return CONTAINING_RECORD( iface, struct storageFile, IStorageFile_iface );
}

static HRESULT WINAPI storageFile_QueryInterface( IStorageFile *iface, REFIID iid, void **out )
{
    struct storageFile *This = impl_from_IStorageFile( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IStringable ) || IsEqualGUID( iid, &IID_IStorageFile ))
        *out = &This->IStorageFile_iface;
    else if (IsEqualGUID( iid, &IID_IStorageItem ))
        *out = &This->IStorageItem_iface;
    else
    {
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI storageFile_AddRef( IStorageFile *iface )
{
    struct storageFile *This = impl_from_IStorageFile( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI storageFile_Release( IStorageFile *iface )
{
    struct storageFile *This = impl_from_IStorageFile( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref) free( This );
    return ref;
}

static HRESULT WINAPI storageFile_GetIids( IStorageFile *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] = { IID_IStringable, IID_IStorageFile, IID_IStorageItem };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI storageFile_GetRuntimeClassName( IStorageFile *iface, HSTRING *name )
{
    static const WCHAR class[] = L"Windows.Storage.StorageFile";
    return WindowsCreateString( class, ARRAY_SIZE(class) - 1, name );
}

static HRESULT WINAPI storageFile_GetTrustLevel( IStorageFile *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI storageFile_get_FileType( IStorageFile *iface, HSTRING *value )
{
    struct storageFile *This = impl_from_IStorageFile( iface );
    const WCHAR *dot = wcsrchr( This->path, '.' );
    WCHAR ext[16] = L"";

    if (dot) lstrcpyW( ext, dot );
    return WindowsCreateString( ext, lstrlenW( ext ), value );
}

static HRESULT WINAPI storageFile_get_ContentType( IStorageFile *iface, HSTRING *value )
{
    return WindowsCreateString( L"*/*", 3, value );
}

static HRESULT WINAPI storageFile_OpenAsync( IStorageFile *iface, enum FileAccessMode mode,
        IAsyncOperation_IRandomAccessStream **operation )
{
    FIXME( "iface %p, mode %d, operation %p stub!\n", iface, mode, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_OpenTransactedWriteAsync( IStorageFile *iface,
        IAsyncOperation_StorageStreamTransaction **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_CopyOverloadDefaultNameAndOptions( IStorageFile *iface, IStorageFolder *folder,
        IAsyncOperation_StorageFile **operation )
{
    FIXME( "iface %p, folder %p, operation %p stub!\n", iface, folder, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_CopyOverloadDefaultOptions( IStorageFile *iface, IStorageFolder *folder,
        HSTRING name, IAsyncOperation_StorageFile **operation )
{
    FIXME( "iface %p, folder %p, name %s, operation %p stub!\n", iface, folder, debugstr_hstring( name ), operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_CopyOverload( IStorageFile *iface, IStorageFolder *folder, HSTRING name,
        enum NameCollisionOption option, IAsyncOperation_StorageFile **operation )
{
    FIXME( "iface %p, folder %p, name %s, option %d, operation %p stub!\n",
            iface, folder, debugstr_hstring( name ), option, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_CopyAndReplaceAsync( IStorageFile *iface, IStorageFile *file,
        IAsyncAction **operation )
{
    FIXME( "iface %p, file %p, operation %p stub!\n", iface, file, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_MoveOverloadDefaultNameAndOptions( IStorageFile *iface, IStorageFolder *folder,
        IAsyncAction **operation )
{
    FIXME( "iface %p, folder %p, operation %p stub!\n", iface, folder, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_MoveOverloadDefaultOptions( IStorageFile *iface, IStorageFolder *folder,
        HSTRING name, IAsyncAction **operation )
{
    FIXME( "iface %p, folder %p, name %s, operation %p stub!\n", iface, folder, debugstr_hstring( name ), operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_MoveOverload( IStorageFile *iface, IStorageFolder *folder, HSTRING name,
        enum NameCollisionOption option, IAsyncAction **operation )
{
    FIXME( "iface %p, folder %p, name %s, option %d, operation %p stub!\n",
            iface, folder, debugstr_hstring( name ), option, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI storageFile_MoveAndReplaceAsync( IStorageFile *iface, IStorageFile *file,
        IAsyncAction **operation )
{
    FIXME( "iface %p, file %p, operation %p stub!\n", iface, file, operation );
    *operation = NULL;
    return E_NOTIMPL;
}

static const struct IStorageFileVtbl storage_file_vtbl =
{
    storageFile_QueryInterface,
    storageFile_AddRef,
    storageFile_Release,
    storageFile_GetIids,
    storageFile_GetRuntimeClassName,
    storageFile_GetTrustLevel,
    storageFile_get_FileType,
    storageFile_get_ContentType,
    storageFile_OpenAsync,
    storageFile_OpenTransactedWriteAsync,
    storageFile_CopyOverloadDefaultNameAndOptions,
    storageFile_CopyOverloadDefaultOptions,
    storageFile_CopyOverload,
    storageFile_CopyAndReplaceAsync,
    storageFile_MoveOverloadDefaultNameAndOptions,
    storageFile_MoveOverloadDefaultOptions,
    storageFile_MoveOverload,
    storageFile_MoveAndReplaceAsync,
};

static struct storageFile *create_storage_file( const WCHAR *path )
{
    struct storageFile *file = calloc( 1, sizeof(*file) );

    if (!file) return NULL;
    file->IStorageFile_iface.lpVtbl = &storage_file_vtbl;
    file->IStorageItem_iface.lpVtbl = &storage_item_vtbl;
    file->ref = 1;
    lstrcpyW( file->path, path );
    return file;
}

/* ---- completed eager async operations (GetFileAsync/GetItemAsync/GetFilesAsync) ---- */

struct async_result
{
    IAsyncOperation_StorageFile op_file;
    IAsyncOperation_IStorageItem op_item;
    IAsyncOperation_IVectorView_StorageFile op_vec;
    IAsyncOperation_IVectorView_StorageFolder op_vecf;
    IAsyncInfo info;
    IVectorView_StorageFile view;
    IVectorView_StorageFolder view_folder;
    LONG ref;
    int kind;
    struct storageFile *file;
    struct storageFile *files[16];
    UINT32 file_count;
    struct storage_folder *folders[16];
    UINT32 folder_count;
    IUnknown *handler;
};

static inline struct async_result *async_from_op( IAsyncOperation_StorageFile *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, op_file );
}

static inline struct async_result *async_from_op_item( IAsyncOperation_IStorageItem *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, op_item );
}

static inline struct async_result *async_from_op_vec( IAsyncOperation_IVectorView_StorageFile *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, op_vec );
}

static inline struct async_result *async_from_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, info );
}

static inline struct async_result *async_from_view( IVectorView_StorageFile *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, view );
}

static inline struct async_result *async_from_op_vecf( IAsyncOperation_IVectorView_StorageFolder *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, op_vecf );
}

static inline struct async_result *async_from_viewf( IVectorView_StorageFolder *iface )
{
    return CONTAINING_RECORD( iface, struct async_result, view_folder );
}

static void async_invoke_handler( struct async_result *This )
{
    if (!This->handler) return;
    switch (This->kind)
    {
    case 0:
        {
            __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStorageFile *h = (void *)This->handler;
            h->lpVtbl->Invoke( h, (IAsyncOperation_StorageFile *)&This->op_file, 1 /* AsyncStatus_Completed */ );
            break;
        }
    case 1:
        {
            __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CIStorageItem *h = (void *)This->handler;
            h->lpVtbl->Invoke( h, (IAsyncOperation_IStorageItem *)&This->op_item, 1 /* AsyncStatus_Completed */ );
            break;
        }
    case 2:
        {
            __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CStorage__CStorageFile *h = (void *)This->handler;
            h->lpVtbl->Invoke( h, (IAsyncOperation_IVectorView_StorageFile *)&This->op_vec, 1 /* AsyncStatus_Completed */ );
            break;
        }
    default:
        {
            __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CStorage__CStorageFolder *h = (void *)This->handler;
            h->lpVtbl->Invoke( h, (IAsyncOperation_IVectorView_StorageFolder *)&This->op_vecf, 1 /* AsyncStatus_Completed */ );
            break;
        }
    }
}

static HRESULT async_query( struct async_result *This, REFIID iid, void **out )
{
    TRACE( "iid %s out %p.\n", debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ))
        *out = &This->op_file;
    else if (IsEqualGUID( iid, &IID_IAsyncOperation_StorageFile ))
        *out = &This->op_file;
    else if (IsEqualGUID( iid, &IID_IAsyncOperation_IStorageItem ))
        *out = &This->op_item;
    else if (IsEqualGUID( iid, &IID_IAsyncOperation_IVectorView_StorageFile ))
        *out = &This->op_vec;
    else if (IsEqualGUID( iid, &IID_IAsyncInfo ))
        *out = &This->info;
    else if (IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_StorageFile ))
        *out = &This->op_file;
    else if (IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_IStorageItem ))
        *out = &This->op_item;
    else if (IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_IVectorView_StorageFile ))
        *out = &This->op_vec;
    else if (IsEqualGUID( iid, &IID_IVectorView_StorageFile ))
        *out = &This->view;
    else if (IsEqualGUID( iid, &IID_IAsyncOperation_IVectorView_StorageFolder ))
        *out = &This->op_vecf;
    else if (IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_IVectorView_StorageFolder ))
        *out = &This->op_vecf;
    else if (IsEqualGUID( iid, &IID_IVectorView_StorageFolder ))
        *out = &This->view_folder;
    else
    {
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG async_addref( struct async_result *This )
{
    return InterlockedIncrement( &This->ref );
}

static ULONG async_release( struct async_result *This )
{
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        UINT32 i;
        if (This->handler) IUnknown_Release( This->handler );
        if (This->file) IStorageFile_Release( &This->file->IStorageFile_iface );
        for (i = 0; i < This->file_count; i++) IStorageFile_Release( &This->files[i]->IStorageFile_iface );
        for (i = 0; i < This->folder_count; i++) IStorageFolder_Release( &This->folders[i]->IStorageFolder_iface );
        free( This );
    }
    return ref;
}

#define ASYNC_FWD(name, type, from) \
static HRESULT WINAPI name##_QueryInterface( type *iface, REFIID iid, void **out ) { return async_query( from( iface ), iid, out ); } \
static ULONG WINAPI name##_addref( type *iface ) { return async_addref( from( iface ) ); } \
static ULONG WINAPI name##_release( type *iface ) { return async_release( from( iface ) ); } \
static HRESULT WINAPI name##_GetIids( type *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; } \
static HRESULT WINAPI name##_GetRuntimeClassName( type *iface, HSTRING *name ) { return WindowsCreateString( L"Windows.Foundation.IAsyncOperation", 34, name ); } \
static HRESULT WINAPI name##_GetTrustLevel( type *iface, TrustLevel *level ) { *level = FullTrust; return S_OK; } \

ASYNC_FWD(async_file, IAsyncOperation_StorageFile, async_from_op)
ASYNC_FWD(async_item, IAsyncOperation_IStorageItem, async_from_op_item)
ASYNC_FWD(async_vec, IAsyncOperation_IVectorView_StorageFile, async_from_op_vec)
ASYNC_FWD(async_info, IAsyncInfo, async_from_info)
ASYNC_FWD(async_view, IVectorView_StorageFile, async_from_view)
ASYNC_FWD(async_vecf, IAsyncOperation_IVectorView_StorageFolder, async_from_op_vecf)
ASYNC_FWD(async_viewf, IVectorView_StorageFolder, async_from_viewf)
static HRESULT WINAPI async_file_put_Completed( IAsyncOperation_StorageFile *iface,
        __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStorageFile *handler )
{
    struct async_result *This = async_from_op( iface );

    if (This->handler) IUnknown_Release( This->handler );
    This->handler = (IUnknown *)handler;
    if (handler) IUnknown_AddRef( This->handler );
    async_invoke_handler( This );
    return S_OK;
}

static HRESULT WINAPI async_file_get_Completed( IAsyncOperation_StorageFile *iface,
        IAsyncOperationCompletedHandler_StorageFile **handler )
{
    struct async_result *This = async_from_op( iface );

    *handler = (__FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStorageFile *)This->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI async_file_GetResults( IAsyncOperation_StorageFile *iface, IStorageFile **results )
{
    struct async_result *This = async_from_op( iface );

    *results = &This->file->IStorageFile_iface;
    IStorageFile_AddRef( *results );
    return S_OK;
}

static const struct __FIAsyncOperation_1_Windows__CStorage__CStorageFileVtbl async_file_vtbl =
{
    async_file_QueryInterface, async_file_addref, async_file_release,
    async_file_GetIids, async_file_GetRuntimeClassName, async_file_GetTrustLevel,
    async_file_put_Completed, async_file_get_Completed, async_file_GetResults,
};

static HRESULT WINAPI async_item_put_Completed( IAsyncOperation_IStorageItem *iface,
        __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CIStorageItem *handler )
{
    struct async_result *This = async_from_op_item( iface );

    if (This->handler) IUnknown_Release( This->handler );
    This->handler = (IUnknown *)handler;
    if (handler) IUnknown_AddRef( This->handler );
    async_invoke_handler( This );
    return S_OK;
}

static HRESULT WINAPI async_item_get_Completed( IAsyncOperation_IStorageItem *iface,
        IAsyncOperationCompletedHandler_IStorageItem **handler )
{
    struct async_result *This = async_from_op_item( iface );

    *handler = (__FIAsyncOperationCompletedHandler_1_Windows__CStorage__CIStorageItem *)This->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI async_item_GetResults( IAsyncOperation_IStorageItem *iface, IStorageItem **results )
{
    struct async_result *This = async_from_op_item( iface );

    *results = &This->file->IStorageItem_iface;
    IStorageFile_AddRef( &This->file->IStorageFile_iface );
    return S_OK;
}

static const struct __FIAsyncOperation_1_Windows__CStorage__CIStorageItemVtbl async_item_vtbl =
{
    async_item_QueryInterface, async_item_addref, async_item_release,
    async_item_GetIids, async_item_GetRuntimeClassName, async_item_GetTrustLevel,
    async_item_put_Completed, async_item_get_Completed, async_item_GetResults,
};

static HRESULT WINAPI async_vec_put_Completed( IAsyncOperation_IVectorView_StorageFile *iface,
        __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CStorage__CStorageFile *handler )
{
    struct async_result *This = async_from_op_vec( iface );

    if (This->handler) IUnknown_Release( This->handler );
    This->handler = (IUnknown *)handler;
    if (handler) IUnknown_AddRef( This->handler );
    async_invoke_handler( This );
    return S_OK;
}

static HRESULT WINAPI async_vec_get_Completed( IAsyncOperation_IVectorView_StorageFile *iface,
        IAsyncOperationCompletedHandler_IVectorView_StorageFile **handler )
{
    struct async_result *This = async_from_op_vec( iface );

    *handler = (__FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CStorage__CStorageFile *)This->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI async_vec_GetResults( IAsyncOperation_IVectorView_StorageFile *iface,
        IVectorView_StorageFile **results )
{
    struct async_result *This = async_from_op_vec( iface );

    *results = &This->view;
    IInspectable_AddRef( (IInspectable *)*results );
    return S_OK;
}

static const struct __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CStorageFileVtbl async_vec_vtbl =
{
    async_vec_QueryInterface, async_vec_addref, async_vec_release,
    async_vec_GetIids, async_vec_GetRuntimeClassName, async_vec_GetTrustLevel,
    async_vec_put_Completed, async_vec_get_Completed, async_vec_GetResults,
};

static HRESULT WINAPI async_vecf_put_Completed( IAsyncOperation_IVectorView_StorageFolder *iface,
        __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CStorage__CStorageFolder *handler )
{
    struct async_result *This = async_from_op_vecf( iface );

    if (This->handler) IUnknown_Release( This->handler );
    This->handler = (IUnknown *)handler;
    if (handler) IUnknown_AddRef( This->handler );
    async_invoke_handler( This );
    return S_OK;
}

static HRESULT WINAPI async_vecf_get_Completed( IAsyncOperation_IVectorView_StorageFolder *iface,
        __FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CStorage__CStorageFolder **handler )
{
    struct async_result *This = async_from_op_vecf( iface );

    *handler = (__FIAsyncOperationCompletedHandler_1___FIVectorView_1_Windows__CStorage__CStorageFolder *)This->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI async_vecf_GetResults( IAsyncOperation_IVectorView_StorageFolder *iface,
        IVectorView_StorageFolder **results )
{
    struct async_result *This = async_from_op_vecf( iface );

    *results = &This->view_folder;
    IInspectable_AddRef( (IInspectable *)*results );
    return S_OK;
}

static const struct __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CStorageFolderVtbl async_vecf_vtbl =
{
    async_vecf_QueryInterface, async_vecf_addref, async_vecf_release,
    async_vecf_GetIids, async_vecf_GetRuntimeClassName, async_vecf_GetTrustLevel,
    async_vecf_put_Completed, async_vecf_get_Completed, async_vecf_GetResults,
};

static HRESULT WINAPI async_viewf_GetAt( IVectorView_StorageFolder *iface, UINT32 index,
        IStorageFolder **item )
{
    struct async_result *This = async_from_viewf( iface );

    if (index >= This->folder_count) return E_INVALIDARG;
    *item = &This->folders[index]->IStorageFolder_iface;
    IStorageFolder_AddRef( *item );
    return S_OK;
}

static HRESULT WINAPI async_viewf_get_Size( IVectorView_StorageFolder *iface, UINT32 *size )
{
    struct async_result *This = async_from_viewf( iface );

    *size = This->folder_count;
    return S_OK;
}

static HRESULT WINAPI async_viewf_IndexOf( IVectorView_StorageFolder *iface, IStorageFolder *item,
        UINT32 *index, boolean *found )
{
    struct async_result *This = async_from_viewf( iface );
    UINT32 i;

    for (i = 0; i < This->folder_count; i++)
    {
        if (&This->folders[i]->IStorageFolder_iface == item)
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

static const struct __FIVectorView_1_Windows__CStorage__CStorageFolderVtbl async_viewf_vtbl =
{
    async_viewf_QueryInterface, async_viewf_addref, async_viewf_release,
    async_viewf_GetIids, async_viewf_GetRuntimeClassName, async_viewf_GetTrustLevel,
    async_viewf_GetAt, async_viewf_get_Size, async_viewf_IndexOf,
};

static HRESULT WINAPI async_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI async_info_get_Status( IAsyncInfo *iface, enum AsyncStatus *status )
{
    *status = 1 /* AsyncStatus_Completed */;
    return S_OK;
}

static HRESULT WINAPI async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *code )
{
    *code = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_info_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI async_info_Close( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI async_info_GetResults( IAsyncInfo *iface, HRESULT *code )
{
    *code = S_OK;
    return S_OK;
}

static const struct IAsyncInfoVtbl async_info_vtbl =
{
    async_info_QueryInterface, async_info_addref, async_info_release,
    async_info_GetIids, async_info_GetRuntimeClassName, async_info_GetTrustLevel,
    async_info_get_Id, async_info_get_Status, async_info_get_ErrorCode,
    async_info_Cancel, async_info_Close, async_info_GetResults,
};

static HRESULT WINAPI async_view_GetAt( IVectorView_StorageFile *iface, UINT32 index,
        IStorageFile **item )
{
    struct async_result *This = async_from_view( iface );

    if (index >= This->file_count) return E_INVALIDARG;
    *item = &This->files[index]->IStorageFile_iface;
    IStorageFile_AddRef( *item );
    return S_OK;
}

static HRESULT WINAPI async_view_get_Size( IVectorView_StorageFile *iface, UINT32 *size )
{
    struct async_result *This = async_from_view( iface );

    *size = This->file_count;
    return S_OK;
}

static HRESULT WINAPI async_view_IndexOf( IVectorView_StorageFile *iface, IStorageFile *item,
        UINT32 *index, boolean *found )
{
    struct async_result *This = async_from_view( iface );
    UINT32 i;

    for (i = 0; i < This->file_count; i++)
    {
        if (&This->files[i]->IStorageFile_iface == item)
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

static const struct __FIVectorView_1_Windows__CStorage__CStorageFileVtbl async_view_vtbl =
{
    async_view_QueryInterface, async_view_addref, async_view_release,
    async_view_GetIids, async_view_GetRuntimeClassName, async_view_GetTrustLevel,
    async_view_GetAt, async_view_get_Size, async_view_IndexOf,
};

static struct storage_folder *make_storage_folder( const WCHAR *path )
{
    struct storage_folder *folder = calloc( 1, sizeof(*folder) );

    if (!folder) return NULL;
    folder->IStorageFolder_iface.lpVtbl = &storage_folder_vtbl;
    folder->IStorageItem_iface.lpVtbl = &storage_item_vtbl;
    folder->ref = 1;
    lstrcpyW( folder->path, path );
    return folder;
}

static void fill_named_file( WCHAR *path, const WCHAR *root, HSTRING name )
{
    const WCHAR *n;

    lstrcpyW( path, root );
    if ((n = WindowsGetStringRawBuffer( name, NULL )) && *n)
    {
        lstrcatW( path, L"\\" );
        lstrcatW( path, n );
    }
    else lstrcatW( path, L"\\AppXManifest.xml" );
}

static void enumerate_dir( struct async_result *async, const WCHAR *root, int folders_only )
{
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE handle;

    lstrcpyW( pattern, root );
    lstrcatW( pattern, L"\\*" );
    if ((handle = FindFirstFileW( pattern, &data )) == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!lstrcmpW( data.cFileName, L"." ) || !lstrcmpW( data.cFileName, L".." )) continue;
        if (folders_only ? !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                         : !!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        lstrcpyW( pattern, root );
        lstrcatW( pattern, L"\\" );
        lstrcatW( pattern, data.cFileName );
        if (folders_only)
        {
            if (async->folder_count < ARRAY_SIZE(async->folders) &&
                    (async->folders[async->folder_count] = make_storage_folder( pattern )))
                async->folder_count++;
        }
        else if (async->file_count < ARRAY_SIZE(async->files) &&
                (async->files[async->file_count] = create_storage_file( pattern )))
            async->file_count++;
    } while (async->file_count + async->folder_count < 16 && FindNextFileW( handle, &data ));
    FindClose( handle );
}

static HRESULT create_async_result( struct storage_folder *folder, int kind,
        HSTRING name, IInspectable **ret )
{
    struct async_result *async = calloc( 1, sizeof(*async) );
    WCHAR path[MAX_PATH];

    if (!async) return E_OUTOFMEMORY;
    if (!folder->path[0]) package_root_path( folder->path, ARRAY_SIZE(folder->path) );
    async->ref = 1;
    async->kind = kind;
    async->op_file.lpVtbl = &async_file_vtbl;
    async->op_item.lpVtbl = &async_item_vtbl;
    async->op_vec.lpVtbl = &async_vec_vtbl;
    async->op_vecf.lpVtbl = &async_vecf_vtbl;
    async->info.lpVtbl = &async_info_vtbl;
    async->view.lpVtbl = &async_view_vtbl;
    async->view_folder.lpVtbl = &async_viewf_vtbl;

    if (kind <= 1)
    {
        fill_named_file( path, folder->path, name );
        if (!(async->file = create_storage_file( path )))
        {
            free( async );
            return E_OUTOFMEMORY;
        }
        async->files[0] = async->file;
        async->file_count = 1;
    }
    else
    {
        enumerate_dir( async, folder->path, kind == 3 );
        if (kind == 2 && !async->file_count)
        {
            fill_named_file( path, folder->path, NULL );
            if (!(async->file = create_storage_file( path )))
            {
                free( async );
                return E_OUTOFMEMORY;
            }
            async->files[0] = async->file;
            async->file_count = 1;
        }
    }
    switch (kind)
    {
    case 0: *ret = (IInspectable *)&async->op_file; break;
    case 1: *ret = (IInspectable *)&async->op_item; break;
    case 3: *ret = (IInspectable *)&async->op_vecf; break;
    default: *ret = (IInspectable *)&async->op_vec; break;
    }
    IInspectable_AddRef( *ret );
    return S_OK;
}


struct package
{
    IPackage IPackage_iface;
    LONG ref;
};

static inline struct package *impl_from_IPackage( IPackage *iface )
{
    return CONTAINING_RECORD( iface, struct package, IPackage_iface );
}

static HRESULT WINAPI package_QueryInterface( IPackage *iface, REFIID iid, void **out )
{
    struct package *impl = impl_from_IPackage( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPackage ))
    {
        *out = &impl->IPackage_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_AddRef( IPackage *iface )
{
    struct package *impl = impl_from_IPackage( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI package_Release( IPackage *iface )
{
    struct package *impl = impl_from_IPackage( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI package_GetIids( IPackage *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IPackage,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI package_GetRuntimeClassName( IPackage *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_GetTrustLevel( IPackage *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

struct package_id
{
    IPackageId IPackageId_iface;
    LONG ref;
};

static inline struct package_id *impl_from_IPackageId( IPackageId *iface )
{
    return CONTAINING_RECORD( iface, struct package_id, IPackageId_iface );
}

static HRESULT WINAPI package_id_QueryInterface( IPackageId *iface, REFIID iid, void **out )
{
    struct package_id *impl = impl_from_IPackageId( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IPackageId ))
    {
        *out = &impl->IPackageId_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_id_AddRef( IPackageId *iface )
{
    struct package_id *impl = impl_from_IPackageId( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI package_id_Release( IPackageId *iface )
{
    struct package_id *impl = impl_from_IPackageId( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI package_id_GetIids( IPackageId *iface, ULONG *count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IPackageId,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI package_id_GetTrustLevel( IPackageId *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI package_id_get_Name( IPackageId *iface, HSTRING *value )
{
    WCHAR buf[MAX_PATH];
    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_FAMILY_NAME", buf, ARRAY_SIZE(buf) ))
        wcscpy( buf, L"5319275A.WhatsAppDesktop" );
    return WindowsCreateString( buf, wcslen(buf), value );
}

static HRESULT WINAPI package_id_get_Version( IPackageId *iface, struct PackageVersion *value )
{
    if (!value) return E_POINTER;
    value->Major = 2;
    value->Minor = 2620;
    value->Build = 102;
    value->Revision = 0;
    return S_OK;
}

static HRESULT WINAPI package_id_get_Architecture( IPackageId *iface, enum __x_ABI_CWindows_CSystem_CProcessorArchitecture *value )
{
    if (!value) return E_POINTER;
    *value = 9; /* X64 */
    return S_OK;
}

static HRESULT WINAPI package_id_get_ResourceId( IPackageId *iface, HSTRING *value )
{
    return WindowsCreateString( L"", 0, value );
}

static HRESULT WINAPI package_id_get_Publisher( IPackageId *iface, HSTRING *value )
{
    WCHAR buf[MAX_PATH];
    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_PUBLISHER", buf, ARRAY_SIZE(buf) ) &&
        !package_get_reg_str( L"PackagePublisher", buf, ARRAY_SIZE(buf) ))
        wcscpy( buf, L"CN=Unknown" );
    return WindowsCreateString( buf, wcslen(buf), value );
}

static HRESULT WINAPI package_id_get_PublisherId( IPackageId *iface, HSTRING *value )
{
    WCHAR buf[MAX_PATH];
    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_PUBLISHER_ID", buf, ARRAY_SIZE(buf) ) &&
        !package_get_reg_str( L"PackagePublisherId", buf, ARRAY_SIZE(buf) ))
        wcscpy( buf, L"0000000000000" );
    return WindowsCreateString( buf, wcslen(buf), value );
}

static HRESULT WINAPI package_id_get_FullName( IPackageId *iface, HSTRING *value )
{
    WCHAR buf[MAX_PATH];
    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_FULL_NAME", buf, ARRAY_SIZE(buf) ) &&
        !package_get_reg_str( L"PackageFullName", buf, ARRAY_SIZE(buf) ))
        wcscpy( buf, L"UnknownPackage_1.0.0.0_neutral__0000000000000" );
    return WindowsCreateString( buf, wcslen(buf), value );
}

static HRESULT WINAPI package_id_get_FamilyName( IPackageId *iface, HSTRING *value )
{
    WCHAR buf[MAX_PATH];
    if (!GetEnvironmentVariableW( L"WINE_PACKAGE_FAMILY_NAME", buf, ARRAY_SIZE(buf) ) &&
        !package_get_reg_str( L"PackageFamilyName", buf, ARRAY_SIZE(buf) ))
        wcscpy( buf, L"UnknownPackage_0000000000000" );
    return WindowsCreateString( buf, wcslen(buf), value );
}

static HRESULT WINAPI package_id_GetRuntimeClassName( IPackageId *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.PackageId", 34, name );
}

static const struct IPackageIdVtbl package_id_vtbl =
{
    package_id_QueryInterface,
    package_id_AddRef,
    package_id_Release,
    package_id_GetIids,
    package_id_GetRuntimeClassName,
    package_id_GetTrustLevel,
    package_id_get_Name,
    package_id_get_Version,
    package_id_get_Architecture,
    package_id_get_ResourceId,
    package_id_get_Publisher,
    package_id_get_PublisherId,
    package_id_get_FullName,
    package_id_get_FamilyName,
};

static HRESULT WINAPI package_get_Id( IPackage *iface, IPackageId **value )
{
    struct package_id *impl;
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IPackageId_iface.lpVtbl = &package_id_vtbl;
    impl->ref = 1;
    *value = &impl->IPackageId_iface;
    return S_OK;
}

static HRESULT WINAPI package_get_InstalledLocation( IPackage *iface, IStorageFolder **value )
{
    struct storage_folder *impl;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IStorageFolder_iface.lpVtbl = &storage_folder_vtbl;
    impl->IStorageItem_iface.lpVtbl = &storage_item_vtbl;
    impl->ref = 1;

    *value = &impl->IStorageFolder_iface;
    TRACE( "created IStorageFolder %p.\n", *value );
    return S_OK;
}

static HRESULT WINAPI package_get_IsFramework( IPackage *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_get_Dependencies( IPackage *iface, IVectorView_Package **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
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

DEFINE_IINSPECTABLE( package_statics, IPackageStatics, struct package_statics, IActivationFactory_iface )

/* The running process is the package Wine reports, both IPackageStatics.Current
 * and AppInfo.Package hand out an instance of it. */
HRESULT create_package_instance( IPackage **out )
{
    struct package *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IPackage_iface.lpVtbl = &package_vtbl;
    impl->ref = 1;
    *out = &impl->IPackage_iface;
    return S_OK;
}

static HRESULT WINAPI package_statics_get_Current( IPackageStatics *iface, IPackage **value )
{
    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_INVALIDARG;
    *value = NULL;
    return create_package_instance( value );
}

static const struct IPackageStaticsVtbl package_statics_vtbl =
{
    package_statics_QueryInterface,
    package_statics_AddRef,
    package_statics_Release,
    /* IInspectable methods */
    package_statics_GetIids,
    package_statics_GetRuntimeClassName,
    package_statics_GetTrustLevel,
    /* IPackageStatics methods */
    package_statics_get_Current,
};


/* Windows aggregates the class default interface (IPackage) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( factory_def, IPackage, struct package_statics, impl_from_IPackage_factory_def,
                      IPackage_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI factory_def_get_Id( IPackage *iface, IPackageId **value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_get_InstalledLocation( IPackage *iface, IStorageFolder **value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_get_IsFramework( IPackage *iface, boolean *value ) { return E_FAIL; }
static HRESULT WINAPI factory_def_get_Dependencies( IPackage *iface, __FIVectorView_1_Windows__CApplicationModel__CPackage **value ) { return E_FAIL; }

static const struct IPackageVtbl factory_def_vtbl =
{
    factory_def_QueryInterface,
    factory_def_AddRef,
    factory_def_Release,
    factory_def_GetIids,
    factory_def_GetRuntimeClassName,
    factory_def_GetTrustLevel,
    factory_def_get_Id,
    factory_def_get_InstalledLocation,
    factory_def_get_IsFramework,
    factory_def_get_Dependencies,
};

static struct package_statics package_statics =
{
    {&factory_vtbl},
    {&package_statics_vtbl},
    {&factory_def_vtbl}, 1,
};

IActivationFactory *package_factory = &package_statics.IActivationFactory_iface;
