/* WinRT Windows.Storage Implementation
 *
 * Copyright (C) 2026 Wine Ultimate
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

WINE_DEFAULT_DEBUG_CHANNEL(storage);

/* Helper to read entire file content into allocated buffer */
HRESULT read_file_bytes( const WCHAR *path, BYTE **out_bytes, DWORD *out_size )
{
    HANDLE hFile;
    LARGE_INTEGER size;
    BYTE *buf;
    DWORD read = 0;

    *out_bytes = NULL;
    *out_size = 0;

    hFile = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL );
    if (hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32( GetLastError() );

    if (!GetFileSizeEx( hFile, &size ) || size.QuadPart > 0x7fffffff)
    {
        CloseHandle( hFile );
        return E_FAIL;
    }

    if (!size.LowPart)
    {
        CloseHandle( hFile );
        *out_bytes = calloc( 1, 1 );
        *out_size = 0;
        return S_OK;
    }

    if (!(buf = malloc( size.LowPart )))
    {
        CloseHandle( hFile );
        return E_OUTOFMEMORY;
    }

    if (!ReadFile( hFile, buf, size.LowPart, &read, NULL ))
    {
        free( buf );
        CloseHandle( hFile );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    CloseHandle( hFile );
    *out_bytes = buf;
    *out_size = read;
    return S_OK;
}

/* Helper to write entire buffer to file */
static HRESULT write_file_bytes( const WCHAR *path, const BYTE *bytes, DWORD size, BOOL append )
{
    HANDLE hFile;
    DWORD written = 0;

    hFile = CreateFileW( path, append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32( GetLastError() );

    if (append)
        SetFilePointer( hFile, 0, NULL, FILE_END );

    if (size && !WriteFile( hFile, bytes, size, &written, NULL ))
    {
        CloseHandle( hFile );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    CloseHandle( hFile );
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* StorageFile implementation                                                 */
/* -------------------------------------------------------------------------- */

struct storage_file
{
    IStorageFile IStorageFile_iface;
    IStorageFile2 IStorageFile2_iface;
    IStorageItem IStorageItem_iface;
    IRandomAccessStreamReference IRandomAccessStreamReference_iface;
    IInputStreamReference IInputStreamReference_iface;
    LONG ref;

    WCHAR path[MAX_PATH];
    WCHAR name[MAX_PATH];
    WCHAR file_type[32];
};

static inline struct storage_file *impl_from_IStorageFile( IStorageFile *iface )
{
    return CONTAINING_RECORD( iface, struct storage_file, IStorageFile_iface );
}

static HRESULT WINAPI storage_file_QueryInterface( IStorageFile *iface, REFIID iid, void **out )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStorageFile ))
    {
        *out = &impl->IStorageFile_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStorageFile2 ))
    {
        *out = &impl->IStorageFile2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStorageItem ))
    {
        *out = &impl->IStorageItem_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IRandomAccessStreamReference ))
    {
        *out = &impl->IRandomAccessStreamReference_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IInputStreamReference ))
    {
        *out = &impl->IInputStreamReference_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI storage_file_AddRef( IStorageFile *iface )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI storage_file_Release( IStorageFile *iface )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
        free( impl );

    return ref;
}

static HRESULT WINAPI storage_file_GetIids( IStorageFile *iface, ULONG *iid_count, IID **iids )
{
    IID instance_iids[] =
    {
        IID_IStorageFile,
        IID_IStorageFile2,
        IID_IStorageItem,
        IID_IRandomAccessStreamReference,
        IID_IInputStreamReference,
    };
    IID *array;

    if (!iid_count || !iids) return E_INVALIDARG;
    if (!(array = CoTaskMemAlloc( sizeof(instance_iids) ))) return E_OUTOFMEMORY;
    memcpy( array, instance_iids, sizeof(instance_iids) );
    *iid_count = ARRAY_SIZE(instance_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI storage_file_GetRuntimeClassName( IStorageFile *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.StorageFile", 26, class_name );
}

static HRESULT WINAPI storage_file_GetTrustLevel( IStorageFile *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI storage_file_get_FileType( IStorageFile *iface, HSTRING *value )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->file_type, wcslen( impl->file_type ), value );
}

static HRESULT WINAPI storage_file_get_ContentType( IStorageFile *iface, HSTRING *value )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    const WCHAR *mime = L"application/octet-stream";

    if (!value) return E_POINTER;

    if (!wcsicmp( impl->file_type, L".txt" )) mime = L"text/plain";
    else if (!wcsicmp( impl->file_type, L".json" )) mime = L"application/json";
    else if (!wcsicmp( impl->file_type, L".xml" )) mime = L"application/xml";
    else if (!wcsicmp( impl->file_type, L".png" )) mime = L"image/png";
    else if (!wcsicmp( impl->file_type, L".jpg" ) || !wcsicmp( impl->file_type, L".jpeg" )) mime = L"image/jpeg";
    else if (!wcsicmp( impl->file_type, L".html" ) || !wcsicmp( impl->file_type, L".htm" )) mime = L"text/html";

    return WindowsCreateString( mime, wcslen( mime ), value );
}

static HRESULT WINAPI storage_file_OpenAsync( IStorageFile *iface, FileAccessMode mode,
                                              IAsyncOperation_IRandomAccessStream **operation )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    IRandomAccessStream *stream = NULL;
    HRESULT hr;

    TRACE( "iface %p, mode %d, operation %p.\n", iface, mode, operation );

    if (!operation) return E_POINTER;

    if (FAILED(hr = in_memory_stream_create_from_file( impl->path, &stream )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IRandomAccessStream,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IRandomAccessStream>",
                                            (IInspectable *)stream, (void **)operation );
    IRandomAccessStream_Release( stream );
    return hr;
}

static HRESULT WINAPI storage_file_OpenTransactedWriteAsync( IStorageFile *iface,
                                                            IAsyncOperation_StorageStreamTransaction **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_file_CopyOverloadDefaultNameAndOptions( IStorageFile *iface, IStorageFolder *folder,
                                                                      IAsyncOperation_StorageFile **operation )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    HSTRING name;
    HRESULT hr;

    WindowsCreateString( impl->name, wcslen( impl->name ), &name );
    hr = IStorageFile_CopyOverload( iface, folder, name, NameCollisionOption_FailIfExists, operation );
    WindowsDeleteString( name );
    return hr;
}

static HRESULT WINAPI storage_file_CopyOverloadDefaultOptions( IStorageFile *iface, IStorageFolder *folder,
                                                               HSTRING name, IAsyncOperation_StorageFile **operation )
{
    return IStorageFile_CopyOverload( iface, folder, name, NameCollisionOption_FailIfExists, operation );
}

static HRESULT WINAPI storage_file_CopyOverload( IStorageFile *iface, IStorageFolder *folder, HSTRING name,
                                                NameCollisionOption option, IAsyncOperation_StorageFile **operation )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    IStorageItem *item = NULL;
    HSTRING folder_path_str = NULL;
    const WCHAR *dest_dir;
    const WCHAR *dest_name;
    WCHAR dest_path[MAX_PATH];
    IStorageFile *new_file = NULL;
    BOOL fail_if_exists = (option == NameCollisionOption_FailIfExists);
    HRESULT hr;

    TRACE( "iface %p, folder %p, name %p, option %d, operation %p.\n", iface, folder, name, option, operation );

    if (!folder || !operation) return E_POINTER;

    if (FAILED(hr = IStorageFolder_QueryInterface( folder, &IID_IStorageItem, (void **)&item )))
        return hr;

    IStorageItem_get_Path( item, &folder_path_str );
    IStorageItem_Release( item );

    dest_dir = WindowsGetStringRawBuffer( folder_path_str, NULL );
    dest_name = name ? WindowsGetStringRawBuffer( name, NULL ) : impl->name;

    lstrcpyW( dest_path, dest_dir );
    if (dest_path[wcslen( dest_path ) - 1] != '\\')
        lstrcatW( dest_path, L"\\" );
    lstrcatW( dest_path, dest_name );
    WindowsDeleteString( folder_path_str );

    if (!CopyFileW( impl->path, dest_path, fail_if_exists ))
        return HRESULT_FROM_WIN32( GetLastError() );

    if (FAILED(hr = storage_file_create( dest_path, &new_file )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFile,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
                                            (IInspectable *)new_file, (void **)operation );
    IStorageFile_Release( new_file );
    return hr;
}

static HRESULT WINAPI storage_file_CopyAndReplaceAsync( IStorageFile *iface, IStorageFile *file,
                                                       IAsyncAction **operation )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    IStorageItem *item = NULL;
    HSTRING dest_path_str = NULL;
    const WCHAR *dest_path;
    HRESULT hr;

    if (!file || !operation) return E_POINTER;

    if (FAILED(hr = IStorageFile_QueryInterface( file, &IID_IStorageItem, (void **)&item )))
        return hr;

    IStorageItem_get_Path( item, &dest_path_str );
    IStorageItem_Release( item );

    dest_path = WindowsGetStringRawBuffer( dest_path_str, NULL );
    if (!CopyFileW( impl->path, dest_path, FALSE ))
    {
        WindowsDeleteString( dest_path_str );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    WindowsDeleteString( dest_path_str );
    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI storage_file_MoveOverloadDefaultNameAndOptions( IStorageFile *iface, IStorageFolder *folder,
                                                                      IAsyncAction **operation )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    HSTRING name;
    HRESULT hr;

    WindowsCreateString( impl->name, wcslen( impl->name ), &name );
    hr = IStorageFile_MoveOverload( iface, folder, name, NameCollisionOption_FailIfExists, operation );
    WindowsDeleteString( name );
    return hr;
}

static HRESULT WINAPI storage_file_MoveOverloadDefaultOptions( IStorageFile *iface, IStorageFolder *folder,
                                                               HSTRING name, IAsyncAction **operation )
{
    return IStorageFile_MoveOverload( iface, folder, name, NameCollisionOption_FailIfExists, operation );
}

static HRESULT WINAPI storage_file_MoveOverload( IStorageFile *iface, IStorageFolder *folder, HSTRING name,
                                                NameCollisionOption option, IAsyncAction **operation )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    IStorageItem *item = NULL;
    HSTRING folder_path_str = NULL;
    const WCHAR *dest_dir;
    const WCHAR *dest_name;
    WCHAR dest_path[MAX_PATH];
    DWORD flags = (option == NameCollisionOption_ReplaceExisting) ? MOVEFILE_REPLACE_EXISTING : 0;
    HRESULT hr;

    if (!folder || !operation) return E_POINTER;

    if (FAILED(hr = IStorageFolder_QueryInterface( folder, &IID_IStorageItem, (void **)&item )))
        return hr;

    IStorageItem_get_Path( item, &folder_path_str );
    IStorageItem_Release( item );

    dest_dir = WindowsGetStringRawBuffer( folder_path_str, NULL );
    dest_name = name ? WindowsGetStringRawBuffer( name, NULL ) : impl->name;

    lstrcpyW( dest_path, dest_dir );
    if (dest_path[wcslen( dest_path ) - 1] != '\\')
        lstrcatW( dest_path, L"\\" );
    lstrcatW( dest_path, dest_name );
    WindowsDeleteString( folder_path_str );

    if (!MoveFileExW( impl->path, dest_path, flags ))
        return HRESULT_FROM_WIN32( GetLastError() );

    lstrcpyW( impl->path, dest_path );
    lstrcpyW( impl->name, dest_name );
    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI storage_file_MoveAndReplaceAsync( IStorageFile *iface, IStorageFile *file,
                                                       IAsyncAction **operation )
{
    struct storage_file *impl = impl_from_IStorageFile( iface );
    IStorageItem *item = NULL;
    HSTRING dest_path_str = NULL;
    const WCHAR *dest_path;
    HRESULT hr;

    if (!file || !operation) return E_POINTER;

    if (FAILED(hr = IStorageFile_QueryInterface( file, &IID_IStorageItem, (void **)&item )))
        return hr;

    IStorageItem_get_Path( item, &dest_path_str );
    IStorageItem_Release( item );

    dest_path = WindowsGetStringRawBuffer( dest_path_str, NULL );
    if (!MoveFileExW( impl->path, dest_path, MOVEFILE_REPLACE_EXISTING ))
    {
        WindowsDeleteString( dest_path_str );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    lstrcpyW( impl->path, dest_path );
    WindowsDeleteString( dest_path_str );
    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static const struct IStorageFileVtbl storage_file_vtbl =
{
    storage_file_QueryInterface,
    storage_file_AddRef,
    storage_file_Release,
    storage_file_GetIids,
    storage_file_GetRuntimeClassName,
    storage_file_GetTrustLevel,
    storage_file_get_FileType,
    storage_file_get_ContentType,
    storage_file_OpenAsync,
    storage_file_OpenTransactedWriteAsync,
    storage_file_CopyOverloadDefaultNameAndOptions,
    storage_file_CopyOverloadDefaultOptions,
    storage_file_CopyOverload,
    storage_file_CopyAndReplaceAsync,
    storage_file_MoveOverloadDefaultNameAndOptions,
    storage_file_MoveOverloadDefaultOptions,
    storage_file_MoveOverload,
    storage_file_MoveAndReplaceAsync,
};

/* IStorageFile2 */
DEFINE_IINSPECTABLE( storage_file2, IStorageFile2, struct storage_file, IStorageFile_iface )

static HRESULT WINAPI storage_file2_OpenWithOptionsAsync( IStorageFile2 *iface, FileAccessMode access_mode,
                                                         StorageOpenOptions options,
                                                         IAsyncOperation_IRandomAccessStream **operation )
{
    struct storage_file *impl = impl_from_IStorageFile2( iface );
    return IStorageFile_OpenAsync( &impl->IStorageFile_iface, access_mode, operation );
}

static HRESULT WINAPI storage_file2_OpenTransactedWriteWithOptionsAsync( IStorageFile2 *iface, StorageOpenOptions options,
                                                                        IAsyncOperation_StorageStreamTransaction **operation )
{
    FIXME( "iface %p, options %d, operation %p stub!\n", iface, options, operation );
    return E_NOTIMPL;
}

static const struct IStorageFile2Vtbl storage_file2_vtbl =
{
    storage_file2_QueryInterface,
    storage_file2_AddRef,
    storage_file2_Release,
    storage_file2_GetIids,
    storage_file2_GetRuntimeClassName,
    storage_file2_GetTrustLevel,
    storage_file2_OpenWithOptionsAsync,
    storage_file2_OpenTransactedWriteWithOptionsAsync,
};

/* IStorageItem for StorageFile */
DEFINE_IINSPECTABLE_( storage_item_file, IStorageItem, struct storage_file, impl_from_IStorageItem_File, IStorageItem_iface, &impl->IStorageFile_iface )

static HRESULT WINAPI storage_item_file_RenameAsyncOverloadDefaultOptions( IStorageItem *iface, HSTRING name,
                                                                           IAsyncAction **operation )
{
    return IStorageItem_RenameAsync( iface, name, NameCollisionOption_FailIfExists, operation );
}

static HRESULT WINAPI storage_item_file_RenameAsync( IStorageItem *iface, HSTRING name,
                                                    NameCollisionOption option, IAsyncAction **operation )
{
    struct storage_file *impl = impl_from_IStorageItem_File( iface );
    const WCHAR *new_name = WindowsGetStringRawBuffer( name, NULL );
    WCHAR new_path[MAX_PATH];
    WCHAR *last_slash;

    lstrcpyW( new_path, impl->path );
    if ((last_slash = wcsrchr( new_path, '\\' )))
        lstrcpyW( last_slash + 1, new_name );
    else
        lstrcpyW( new_path, new_name );

    if (!MoveFileExW( impl->path, new_path, (option == NameCollisionOption_ReplaceExisting) ? MOVEFILE_REPLACE_EXISTING : 0 ))
        return HRESULT_FROM_WIN32( GetLastError() );

    lstrcpyW( impl->path, new_path );
    lstrcpyW( impl->name, new_name );
    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI storage_item_file_DeleteAsyncOverloadDefaultOptions( IStorageItem *iface, IAsyncAction **operation )
{
    return IStorageItem_DeleteAsync( iface, StorageDeleteOption_Default, operation );
}

static HRESULT WINAPI storage_item_file_DeleteAsync( IStorageItem *iface, StorageDeleteOption option,
                                                    IAsyncAction **operation )
{
    struct storage_file *impl = impl_from_IStorageItem_File( iface );

    if (!DeleteFileW( impl->path ))
        return HRESULT_FROM_WIN32( GetLastError() );

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI storage_item_file_GetBasicPropertiesAsync( IStorageItem *iface,
                                                                IAsyncOperation_BasicProperties **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_file_get_Name( IStorageItem *iface, HSTRING *value )
{
    struct storage_file *impl = impl_from_IStorageItem_File( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->name, wcslen( impl->name ), value );
}

static HRESULT WINAPI storage_item_file_get_Path( IStorageItem *iface, HSTRING *value )
{
    struct storage_file *impl = impl_from_IStorageItem_File( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->path, wcslen( impl->path ), value );
}

static HRESULT WINAPI storage_item_file_get_Attributes( IStorageItem *iface, FileAttributes *value )
{
    struct storage_file *impl = impl_from_IStorageItem_File( iface );
    DWORD attr;

    if (!value) return E_POINTER;
    attr = GetFileAttributesW( impl->path );
    *value = (attr == INVALID_FILE_ATTRIBUTES) ? FileAttributes_Normal : (FileAttributes)attr;
    return S_OK;
}

static HRESULT WINAPI storage_item_file_get_DateCreated( IStorageItem *iface, DateTime *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_file_IsOfType( IStorageItem *iface, StorageItemTypes type, boolean *value )
{
    if (!value) return E_POINTER;
    *value = (type == StorageItemTypes_File);
    return S_OK;
}

static const struct IStorageItemVtbl storage_item_file_vtbl =
{
    storage_item_file_QueryInterface,
    storage_item_file_AddRef,
    storage_item_file_Release,
    storage_item_file_GetIids,
    storage_item_file_GetRuntimeClassName,
    storage_item_file_GetTrustLevel,
    storage_item_file_RenameAsyncOverloadDefaultOptions,
    storage_item_file_RenameAsync,
    storage_item_file_DeleteAsyncOverloadDefaultOptions,
    storage_item_file_DeleteAsync,
    storage_item_file_GetBasicPropertiesAsync,
    storage_item_file_get_Name,
    storage_item_file_get_Path,
    storage_item_file_get_Attributes,
    storage_item_file_get_DateCreated,
    storage_item_file_IsOfType,
};

/* IRandomAccessStreamReference for StorageFile */
DEFINE_IINSPECTABLE_( storage_file_stream_ref, IRandomAccessStreamReference, struct storage_file, impl_from_IRandomAccessStreamReference_File, IRandomAccessStreamReference_iface, &impl->IStorageFile_iface )

static HRESULT WINAPI storage_file_stream_ref_OpenReadAsync( IRandomAccessStreamReference *iface,
                                                            IAsyncOperation_IRandomAccessStreamWithContentType **operation )
{
    struct storage_file *impl = impl_from_IRandomAccessStreamReference_File( iface );
    IRandomAccessStream *stream = NULL;
    HRESULT hr;

    if (!operation) return E_POINTER;

    if (FAILED(hr = in_memory_stream_create_from_file( impl->path, &stream )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IRandomAccessStreamWithContentType,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IRandomAccessStreamWithContentType>",
                                            (IInspectable *)stream, (void **)operation );
    IRandomAccessStream_Release( stream );
    return hr;
}

static const struct IRandomAccessStreamReferenceVtbl storage_file_stream_ref_vtbl =
{
    storage_file_stream_ref_QueryInterface,
    storage_file_stream_ref_AddRef,
    storage_file_stream_ref_Release,
    storage_file_stream_ref_GetIids,
    storage_file_stream_ref_GetRuntimeClassName,
    storage_file_stream_ref_GetTrustLevel,
    storage_file_stream_ref_OpenReadAsync,
};

/* IInputStreamReference for StorageFile */
DEFINE_IINSPECTABLE_( storage_file_input_ref, IInputStreamReference, struct storage_file, impl_from_IInputStreamReference_File, IInputStreamReference_iface, &impl->IStorageFile_iface )

static HRESULT WINAPI storage_file_input_ref_OpenSequentialReadAsync( IInputStreamReference *iface,
                                                                      IAsyncOperation_IInputStream **operation )
{
    struct storage_file *impl = impl_from_IInputStreamReference_File( iface );
    IRandomAccessStream *stream = NULL;
    IInputStream *input_stream = NULL;
    HRESULT hr;

    if (!operation) return E_POINTER;

    if (FAILED(hr = in_memory_stream_create_from_file( impl->path, &stream )))
        return hr;

    if (FAILED(hr = IRandomAccessStream_QueryInterface( stream, &IID_IInputStream, (void **)&input_stream )))
    {
        IRandomAccessStream_Release( stream );
        return hr;
    }
    IRandomAccessStream_Release( stream );

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IInputStream,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IInputStream>",
                                            (IInspectable *)input_stream, (void **)operation );
    IInputStream_Release( input_stream );
    return hr;
}

static const struct IInputStreamReferenceVtbl storage_file_input_ref_vtbl =
{
    storage_file_input_ref_QueryInterface,
    storage_file_input_ref_AddRef,
    storage_file_input_ref_Release,
    storage_file_input_ref_GetIids,
    storage_file_input_ref_GetRuntimeClassName,
    storage_file_input_ref_GetTrustLevel,
    storage_file_input_ref_OpenSequentialReadAsync,
};

HRESULT storage_file_create( const WCHAR *path, IStorageFile **out )
{
    struct storage_file *impl;
    const WCHAR *last_slash;
    const WCHAR *last_dot;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IStorageFile_iface.lpVtbl = &storage_file_vtbl;
    impl->IStorageFile2_iface.lpVtbl = &storage_file2_vtbl;
    impl->IStorageItem_iface.lpVtbl = &storage_item_file_vtbl;
    impl->IRandomAccessStreamReference_iface.lpVtbl = &storage_file_stream_ref_vtbl;
    impl->IInputStreamReference_iface.lpVtbl = &storage_file_input_ref_vtbl;
    impl->ref = 1;

    lstrcpynW( impl->path, path, ARRAY_SIZE(impl->path) );

    last_slash = wcsrchr( impl->path, '\\' );
    if (!last_slash) last_slash = wcsrchr( impl->path, '/' );
    lstrcpyW( impl->name, last_slash ? last_slash + 1 : impl->path );

    last_dot = wcsrchr( impl->name, '.' );
    if (last_dot)
        lstrcpynW( impl->file_type, last_dot, ARRAY_SIZE(impl->file_type) );
    else
        impl->file_type[0] = 0;

    *out = &impl->IStorageFile_iface;
    TRACE( "created StorageFile %p for path %s\n", *out, debugstr_w( impl->path ) );
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* StorageFile Factory & Statics                                              */
/* -------------------------------------------------------------------------- */

struct storage_file_statics
{
    IActivationFactory IActivationFactory_iface;
    IStorageFileStatics IStorageFileStatics_iface;
    IStorageFile IStorageFile_iface;
    LONG ref;
};

static inline struct storage_file_statics *impl_from_IActivationFactory_StorageFile( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct storage_file_statics, IActivationFactory_iface );
}

static HRESULT WINAPI storage_file_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct storage_file_statics *impl = impl_from_IActivationFactory_StorageFile( iface );

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

    if (IsEqualGUID( iid, &IID_IStorageFileStatics ))
    {
        *out = &impl->IStorageFileStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* Windows aggregates the class default interface (IStorageFile) on the factory
     * object: QI for IID_IStorageFile on the factory succeeds (CsWinRT probes the
     * factory for the class interfaces). The factory is not an instance; instance
     * method calls fail. */
    if (IsEqualGUID( iid, &IID_IStorageFile ))
    {
        *out = &impl->IStorageFile_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI storage_file_factory_AddRef( IActivationFactory *iface )
{
    struct storage_file_statics *impl = impl_from_IActivationFactory_StorageFile( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI storage_file_factory_Release( IActivationFactory *iface )
{
    struct storage_file_statics *impl = impl_from_IActivationFactory_StorageFile( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI storage_file_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IStorageFileStatics,
        IID_IStorageFile,
    };
    IID *array;

    if (!iid_count || !iids) return E_INVALIDARG;
    if (!(array = CoTaskMemAlloc( sizeof(factory_iids) ))) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI storage_file_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.StorageFile", 26, class_name );
}

static HRESULT WINAPI storage_file_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI storage_file_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl storage_file_factory_vtbl =
{
    storage_file_factory_QueryInterface,
    storage_file_factory_AddRef,
    storage_file_factory_Release,
    storage_file_factory_GetIids,
    storage_file_factory_GetRuntimeClassName,
    storage_file_factory_GetTrustLevel,
    storage_file_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( storage_file_statics, IStorageFileStatics, struct storage_file_statics, IActivationFactory_iface )

static HRESULT WINAPI storage_file_statics_GetFileFromPathAsync( IStorageFileStatics *iface, HSTRING path,
                                                                IAsyncOperation_StorageFile **operation )
{
    const WCHAR *raw_path;
    IStorageFile *file;
    HRESULT hr;

    TRACE( "iface %p, path %s, operation %p.\n", iface, debugstr_hstring( path ), operation );

    if (!path || !operation) return E_POINTER;

    raw_path = WindowsGetStringRawBuffer( path, NULL );
    if (FAILED(hr = storage_file_create( raw_path, &file )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFile,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
                                            (IInspectable *)file, (void **)operation );
    IStorageFile_Release( file );
    return hr;
}

static HRESULT WINAPI storage_file_statics_GetFileFromApplicationUriAsync( IStorageFileStatics *iface, IUriRuntimeClass *uri,
                                                                          IAsyncOperation_StorageFile **operation )
{
    HSTRING raw_uri_str;
    const WCHAR *raw_uri;
    WCHAR path[MAX_PATH];
    IStorageFile *file;
    HRESULT hr;

    if (!uri || !operation) return E_POINTER;

    if (FAILED(hr = IUriRuntimeClass_get_RawUri( uri, &raw_uri_str )))
        return hr;

    raw_uri = WindowsGetStringRawBuffer( raw_uri_str, NULL );
    if (!wcsncmp( raw_uri, L"ms-appx:///", 11 ))
    {
        WCHAR *slash;
        GetModuleFileNameW( NULL, path, MAX_PATH );
        if ((slash = wcsrchr( path, '\\' ))) *slash = 0;
        lstrcatW( path, L"\\" );
        lstrcatW( path, raw_uri + 11 );
    }
    else
    {
        lstrcpynW( path, raw_uri, MAX_PATH );
    }
    WindowsDeleteString( raw_uri_str );

    if (FAILED(hr = storage_file_create( path, &file )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFile,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
                                            (IInspectable *)file, (void **)operation );
    IStorageFile_Release( file );
    return hr;
}

static HRESULT WINAPI storage_file_statics_CreateStreamedFileAsync( IStorageFileStatics *iface, HSTRING name,
                                                                   IStreamedFileDataRequestedHandler *handler,
                                                                   IRandomAccessStreamReference *thumb,
                                                                   IAsyncOperation_StorageFile **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_file_statics_ReplaceWithStreamedFileAsync( IStorageFileStatics *iface, IStorageFile *replace,
                                                                        IStreamedFileDataRequestedHandler *handler,
                                                                        IRandomAccessStreamReference *thumb,
                                                                        IAsyncOperation_StorageFile **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_file_statics_CreateStreamedFileFromUriAsync( IStorageFileStatics *iface, HSTRING name,
                                                                          IUriRuntimeClass *uri,
                                                                          IRandomAccessStreamReference *thumb,
                                                                          IAsyncOperation_StorageFile **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_file_statics_ReplaceWithStreamedFileFromUriAsync( IStorageFileStatics *iface, IStorageFile *replace,
                                                                              IUriRuntimeClass *uri,
                                                                              IRandomAccessStreamReference *thumb,
                                                                              IAsyncOperation_StorageFile **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IStorageFileStaticsVtbl storage_file_statics_vtbl =
{
    storage_file_statics_QueryInterface,
    storage_file_statics_AddRef,
    storage_file_statics_Release,
    storage_file_statics_GetIids,
    storage_file_statics_GetRuntimeClassName,
    storage_file_statics_GetTrustLevel,
    storage_file_statics_GetFileFromPathAsync,
    storage_file_statics_GetFileFromApplicationUriAsync,
    storage_file_statics_CreateStreamedFileAsync,
    storage_file_statics_ReplaceWithStreamedFileAsync,
    storage_file_statics_CreateStreamedFileFromUriAsync,
    storage_file_statics_ReplaceWithStreamedFileFromUriAsync,
};

DEFINE_IINSPECTABLE_( storage_file_def, IStorageFile, struct storage_file_statics, impl_from_IStorageFile_factory,
                      IStorageFile_iface, &impl->IActivationFactory_iface )

#define STORAGE_FACTORY_STUB( name, proto ) \
    static HRESULT WINAPI name proto { return E_FAIL; }

STORAGE_FACTORY_STUB( storage_file_def_get_FileType, ( IStorageFile *iface, HSTRING *value ) )
STORAGE_FACTORY_STUB( storage_file_def_get_ContentType, ( IStorageFile *iface, HSTRING *value ) )
STORAGE_FACTORY_STUB( storage_file_def_OpenAsync, ( IStorageFile *iface, FileAccessMode mode,
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStream **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_OpenTransactedWriteAsync, ( IStorageFile *iface,
    __FIAsyncOperation_1_Windows__CStorage__CStorageStreamTransaction **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_CopyOverloadDefaultNameAndOptions, ( IStorageFile *iface, IStorageFolder *folder,
    __FIAsyncOperation_1_Windows__CStorage__CStorageFile **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_CopyOverloadDefaultOptions, ( IStorageFile *iface, IStorageFolder *folder,
    HSTRING name, __FIAsyncOperation_1_Windows__CStorage__CStorageFile **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_CopyOverload, ( IStorageFile *iface, IStorageFolder *folder, HSTRING name,
    NameCollisionOption option, __FIAsyncOperation_1_Windows__CStorage__CStorageFile **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_CopyAndReplaceAsync, ( IStorageFile *iface, IStorageFile *file,
    IAsyncAction **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_MoveOverloadDefaultNameAndOptions, ( IStorageFile *iface, IStorageFolder *folder,
    IAsyncAction **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_MoveOverloadDefaultOptions, ( IStorageFile *iface, IStorageFolder *folder,
    HSTRING name, IAsyncAction **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_MoveOverload, ( IStorageFile *iface, IStorageFolder *folder, HSTRING name,
    NameCollisionOption option, IAsyncAction **operation ) )
STORAGE_FACTORY_STUB( storage_file_def_MoveAndReplaceAsync, ( IStorageFile *iface, IStorageFile *file,
    IAsyncAction **operation ) )

static const struct IStorageFileVtbl storage_file_def_vtbl =
{
    storage_file_def_QueryInterface,
    storage_file_def_AddRef,
    storage_file_def_Release,
    storage_file_def_GetIids,
    storage_file_def_GetRuntimeClassName,
    storage_file_def_GetTrustLevel,
    storage_file_def_get_FileType,
    storage_file_def_get_ContentType,
    storage_file_def_OpenAsync,
    storage_file_def_OpenTransactedWriteAsync,
    storage_file_def_CopyOverloadDefaultNameAndOptions,
    storage_file_def_CopyOverloadDefaultOptions,
    storage_file_def_CopyOverload,
    storage_file_def_CopyAndReplaceAsync,
    storage_file_def_MoveOverloadDefaultNameAndOptions,
    storage_file_def_MoveOverloadDefaultOptions,
    storage_file_def_MoveOverload,
    storage_file_def_MoveAndReplaceAsync,
};

static struct storage_file_statics storage_file_statics_obj =
{
    {&storage_file_factory_vtbl},
    {&storage_file_statics_vtbl},
    {&storage_file_def_vtbl},
    1,
};

IActivationFactory *storage_file_factory = &storage_file_statics_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* StorageFolder implementation                                               */
/* -------------------------------------------------------------------------- */

struct storage_folder
{
    IStorageFolder IStorageFolder_iface;
    IStorageItem IStorageItem_iface;
    LONG ref;

    WCHAR path[MAX_PATH];
    WCHAR name[MAX_PATH];
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
        IsEqualGUID( iid, &IID_IAgileObject ) ||
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

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
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

static HRESULT WINAPI storage_folder_GetIids( IStorageFolder *iface, ULONG *iid_count, IID **iids )
{
    IID instance_iids[] =
    {
        IID_IStorageFolder,
        IID_IStorageItem,
    };
    IID *array;

    if (!iid_count || !iids) return E_INVALIDARG;
    if (!(array = CoTaskMemAlloc( sizeof(instance_iids) ))) return E_OUTOFMEMORY;
    memcpy( array, instance_iids, sizeof(instance_iids) );
    *iid_count = ARRAY_SIZE(instance_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI storage_folder_GetRuntimeClassName( IStorageFolder *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.StorageFolder", 28, class_name );
}

static HRESULT WINAPI storage_folder_GetTrustLevel( IStorageFolder *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI storage_folder_CreateFileAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name,
                                                                           IAsyncOperation_StorageFile **operation )
{
    return IStorageFolder_CreateFileAsync( iface, name, CreationCollisionOption_FailIfExists, operation );
}

static HRESULT WINAPI storage_folder_CreateFileAsync( IStorageFolder *iface, HSTRING name,
                                                     CreationCollisionOption options,
                                                     IAsyncOperation_StorageFile **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    const WCHAR *file_name;
    WCHAR file_path[MAX_PATH];
    HANDLE hFile;
    DWORD disposition;
    IStorageFile *file;
    HRESULT hr;

    if (!name || !operation) return E_POINTER;

    file_name = WindowsGetStringRawBuffer( name, NULL );
    lstrcpyW( file_path, impl->path );
    if (file_path[wcslen( file_path ) - 1] != '\\')
        lstrcatW( file_path, L"\\" );
    lstrcatW( file_path, file_name );

    disposition = (options == CreationCollisionOption_ReplaceExisting) ? CREATE_ALWAYS :
                  (options == CreationCollisionOption_OpenIfExists) ? OPEN_ALWAYS : CREATE_NEW;

    hFile = CreateFileW( file_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL );
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle( hFile );

    if (FAILED(hr = storage_file_create( file_path, &file )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFile,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
                                            (IInspectable *)file, (void **)operation );
    IStorageFile_Release( file );
    return hr;
}

static HRESULT WINAPI storage_folder_CreateFolderAsyncOverloadDefaultOptions( IStorageFolder *iface, HSTRING name,
                                                                             IAsyncOperation_StorageFolder **operation )
{
    return IStorageFolder_CreateFolderAsync( iface, name, CreationCollisionOption_FailIfExists, operation );
}

static HRESULT WINAPI storage_folder_CreateFolderAsync( IStorageFolder *iface, HSTRING name,
                                                       CreationCollisionOption options,
                                                       IAsyncOperation_StorageFolder **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    const WCHAR *folder_name;
    WCHAR folder_path[MAX_PATH];
    IStorageFolder *folder;
    HRESULT hr;

    if (!name || !operation) return E_POINTER;

    folder_name = WindowsGetStringRawBuffer( name, NULL );
    lstrcpyW( folder_path, impl->path );
    if (folder_path[wcslen( folder_path ) - 1] != '\\')
        lstrcatW( folder_path, L"\\" );
    lstrcatW( folder_path, folder_name );

    CreateDirectoryW( folder_path, NULL );

    if (FAILED(hr = storage_folder_create( folder_path, &folder )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFolder,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
                                            (IInspectable *)folder, (void **)operation );
    IStorageFolder_Release( folder );
    return hr;
}

static HRESULT WINAPI storage_folder_GetFileAsync( IStorageFolder *iface, HSTRING name,
                                                  IAsyncOperation_StorageFile **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    const WCHAR *file_name;
    WCHAR file_path[MAX_PATH];
    IStorageFile *file;
    HRESULT hr;

    if (!name || !operation) return E_POINTER;

    file_name = WindowsGetStringRawBuffer( name, NULL );
    lstrcpyW( file_path, impl->path );
    if (file_path[wcslen( file_path ) - 1] != '\\')
        lstrcatW( file_path, L"\\" );
    lstrcatW( file_path, file_name );

    if (FAILED(hr = storage_file_create( file_path, &file )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFile,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
                                            (IInspectable *)file, (void **)operation );
    IStorageFile_Release( file );
    return hr;
}

static HRESULT WINAPI storage_folder_GetFolderAsync( IStorageFolder *iface, HSTRING name,
                                                    IAsyncOperation_StorageFolder **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    const WCHAR *folder_name;
    WCHAR folder_path[MAX_PATH];
    IStorageFolder *folder;
    HRESULT hr;

    if (!name || !operation) return E_POINTER;

    folder_name = WindowsGetStringRawBuffer( name, NULL );
    lstrcpyW( folder_path, impl->path );
    if (folder_path[wcslen( folder_path ) - 1] != '\\')
        lstrcatW( folder_path, L"\\" );
    lstrcatW( folder_path, folder_name );

    if (FAILED(hr = storage_folder_create( folder_path, &folder )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFolder,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
                                            (IInspectable *)folder, (void **)operation );
    IStorageFolder_Release( folder );
    return hr;
}

static HRESULT WINAPI storage_folder_GetItemAsync( IStorageFolder *iface, HSTRING name,
                                                  IAsyncOperation_IStorageItem **operation )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    const WCHAR *item_name;
    WCHAR item_path[MAX_PATH];
    DWORD attr;
    HRESULT hr;

    if (!name || !operation) return E_POINTER;

    item_name = WindowsGetStringRawBuffer( name, NULL );
    lstrcpyW( item_path, impl->path );
    if (item_path[wcslen( item_path ) - 1] != '\\')
        lstrcatW( item_path, L"\\" );
    lstrcatW( item_path, item_name );

    attr = GetFileAttributesW( item_path );
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        IStorageFolder *folder;
        if (FAILED(hr = storage_folder_create( item_path, &folder ))) return hr;
        hr = async_operation_inspectable_create( &IID_IAsyncOperation_IStorageItem,
                                                L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.IStorageItem>",
                                                (IInspectable *)folder, (void **)operation );
        IStorageFolder_Release( folder );
        return hr;
    }
    else
    {
        IStorageFile *file;
        if (FAILED(hr = storage_file_create( item_path, &file ))) return hr;
        hr = async_operation_inspectable_create( &IID_IAsyncOperation_IStorageItem,
                                                L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.IStorageItem>",
                                                (IInspectable *)file, (void **)operation );
        IStorageFile_Release( file );
        return hr;
    }
}

static HRESULT WINAPI storage_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface,
                                                                                      IAsyncOperation_IVectorView_StorageFile **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount( IStorageFolder *iface,
                                                                                        IAsyncOperation_IVectorView_StorageFolder **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_folder_GetItemsAsyncOverloadDefaultStartAndCount( IStorageFolder *iface,
                                                                               IAsyncOperation_IVectorView_IStorageItem **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

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

/* IStorageItem for StorageFolder */
DEFINE_IINSPECTABLE_( storage_item_folder, IStorageItem, struct storage_folder, impl_from_IStorageItem_Folder, IStorageItem_iface, &impl->IStorageFolder_iface )

static HRESULT WINAPI storage_item_folder_RenameAsyncOverloadDefaultOptions( IStorageItem *iface, HSTRING name,
                                                                             IAsyncAction **operation )
{
    return IStorageItem_RenameAsync( iface, name, NameCollisionOption_FailIfExists, operation );
}

static HRESULT WINAPI storage_item_folder_RenameAsync( IStorageItem *iface, HSTRING name,
                                                      NameCollisionOption option, IAsyncAction **operation )
{
    struct storage_folder *impl = impl_from_IStorageItem_Folder( iface );
    const WCHAR *new_name = WindowsGetStringRawBuffer( name, NULL );
    WCHAR new_path[MAX_PATH];
    WCHAR *last_slash;

    lstrcpyW( new_path, impl->path );
    if ((last_slash = wcsrchr( new_path, '\\' )))
        lstrcpyW( last_slash + 1, new_name );
    else
        lstrcpyW( new_path, new_name );

    if (!MoveFileExW( impl->path, new_path, (option == NameCollisionOption_ReplaceExisting) ? MOVEFILE_REPLACE_EXISTING : 0 ))
        return HRESULT_FROM_WIN32( GetLastError() );

    lstrcpyW( impl->path, new_path );
    lstrcpyW( impl->name, new_name );
    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI storage_item_folder_DeleteAsyncOverloadDefaultOptions( IStorageItem *iface, IAsyncAction **operation )
{
    return IStorageItem_DeleteAsync( iface, StorageDeleteOption_Default, operation );
}

static HRESULT WINAPI storage_item_folder_DeleteAsync( IStorageItem *iface, StorageDeleteOption option,
                                                      IAsyncAction **operation )
{
    struct storage_folder *impl = impl_from_IStorageItem_Folder( iface );

    if (!RemoveDirectoryW( impl->path ))
        return HRESULT_FROM_WIN32( GetLastError() );

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI storage_item_folder_GetBasicPropertiesAsync( IStorageItem *iface,
                                                                  IAsyncOperation_BasicProperties **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_folder_get_Name( IStorageItem *iface, HSTRING *value )
{
    struct storage_folder *impl = impl_from_IStorageItem_Folder( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->name, wcslen( impl->name ), value );
}

static HRESULT WINAPI storage_item_folder_get_Path( IStorageItem *iface, HSTRING *value )
{
    struct storage_folder *impl = impl_from_IStorageItem_Folder( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->path, wcslen( impl->path ), value );
}

static HRESULT WINAPI storage_item_folder_get_Attributes( IStorageItem *iface, FileAttributes *value )
{
    if (!value) return E_POINTER;
    *value = FileAttributes_Directory;
    return S_OK;
}

static HRESULT WINAPI storage_item_folder_get_DateCreated( IStorageItem *iface, DateTime *value )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_folder_IsOfType( IStorageItem *iface, StorageItemTypes type, boolean *value )
{
    if (!value) return E_POINTER;
    *value = (type == StorageItemTypes_Folder);
    return S_OK;
}

static const struct IStorageItemVtbl storage_item_folder_vtbl =
{
    storage_item_folder_QueryInterface,
    storage_item_folder_AddRef,
    storage_item_folder_Release,
    storage_item_folder_GetIids,
    storage_item_folder_GetRuntimeClassName,
    storage_item_folder_GetTrustLevel,
    storage_item_folder_RenameAsyncOverloadDefaultOptions,
    storage_item_folder_RenameAsync,
    storage_item_folder_DeleteAsyncOverloadDefaultOptions,
    storage_item_folder_DeleteAsync,
    storage_item_folder_GetBasicPropertiesAsync,
    storage_item_folder_get_Name,
    storage_item_folder_get_Path,
    storage_item_folder_get_Attributes,
    storage_item_folder_get_DateCreated,
    storage_item_folder_IsOfType,
};

HRESULT storage_folder_create( const WCHAR *path, IStorageFolder **out )
{
    struct storage_folder *impl;
    const WCHAR *last_slash;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IStorageFolder_iface.lpVtbl = &storage_folder_vtbl;
    impl->IStorageItem_iface.lpVtbl = &storage_item_folder_vtbl;
    impl->ref = 1;

    lstrcpynW( impl->path, path, ARRAY_SIZE(impl->path) );
    CreateDirectoryW( impl->path, NULL );

    last_slash = wcsrchr( impl->path, '\\' );
    if (!last_slash) last_slash = wcsrchr( impl->path, '/' );
    lstrcpyW( impl->name, last_slash ? last_slash + 1 : impl->path );

    *out = &impl->IStorageFolder_iface;
    TRACE( "created StorageFolder %p for path %s\n", *out, debugstr_w( impl->path ) );
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* StorageFolder Factory & Statics                                            */
/* -------------------------------------------------------------------------- */

struct storage_folder_statics
{
    IActivationFactory IActivationFactory_iface;
    IStorageFolderStatics IStorageFolderStatics_iface;
    IStorageFolder IStorageFolder_iface;
    LONG ref;
};

static inline struct storage_folder_statics *impl_from_IActivationFactory_StorageFolder( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder_statics, IActivationFactory_iface );
}

static HRESULT WINAPI storage_folder_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct storage_folder_statics *impl = impl_from_IActivationFactory_StorageFolder( iface );

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

    if (IsEqualGUID( iid, &IID_IStorageFolderStatics ))
    {
        *out = &impl->IStorageFolderStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* Windows aggregates the class default interface (IStorageFolder) on the factory object. */
    if (IsEqualGUID( iid, &IID_IStorageFolder ))
    {
        *out = &impl->IStorageFolder_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI storage_folder_factory_AddRef( IActivationFactory *iface )
{
    struct storage_folder_statics *impl = impl_from_IActivationFactory_StorageFolder( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI storage_folder_factory_Release( IActivationFactory *iface )
{
    struct storage_folder_statics *impl = impl_from_IActivationFactory_StorageFolder( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI storage_folder_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IStorageFolderStatics,
        IID_IStorageFolder,
    };
    IID *array;

    if (!iid_count || !iids) return E_INVALIDARG;
    if (!(array = CoTaskMemAlloc( sizeof(factory_iids) ))) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI storage_folder_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.StorageFolder", 28, class_name );
}

static HRESULT WINAPI storage_folder_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI storage_folder_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl storage_folder_factory_vtbl =
{
    storage_folder_factory_QueryInterface,
    storage_folder_factory_AddRef,
    storage_folder_factory_Release,
    storage_folder_factory_GetIids,
    storage_folder_factory_GetRuntimeClassName,
    storage_folder_factory_GetTrustLevel,
    storage_folder_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( storage_folder_statics, IStorageFolderStatics, struct storage_folder_statics, IActivationFactory_iface )

static HRESULT WINAPI storage_folder_statics_GetFolderFromPathAsync( IStorageFolderStatics *iface, HSTRING path,
                                                                    IAsyncOperation_StorageFolder **operation )
{
    const WCHAR *raw_path;
    IStorageFolder *folder;
    HRESULT hr;

    TRACE( "iface %p, path %s, operation %p.\n", iface, debugstr_hstring( path ), operation );

    if (!path || !operation) return E_POINTER;

    raw_path = WindowsGetStringRawBuffer( path, NULL );
    if (FAILED(hr = storage_folder_create( raw_path, &folder )))
        return hr;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_StorageFolder,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
                                            (IInspectable *)folder, (void **)operation );
    IStorageFolder_Release( folder );
    return hr;
}

static const struct IStorageFolderStaticsVtbl storage_folder_statics_vtbl =
{
    storage_folder_statics_QueryInterface,
    storage_folder_statics_AddRef,
    storage_folder_statics_Release,
    storage_folder_statics_GetIids,
    storage_folder_statics_GetRuntimeClassName,
    storage_folder_statics_GetTrustLevel,
    storage_folder_statics_GetFolderFromPathAsync,
};

DEFINE_IINSPECTABLE_( storage_folder_def, IStorageFolder, struct storage_folder_statics,
                      impl_from_IStorageFolder_factory, IStorageFolder_iface, &impl->IActivationFactory_iface )

STORAGE_FACTORY_STUB( storage_folder_def_CreateFileAsyncOverloadDefaultOptions, ( IStorageFolder *iface, HSTRING name,
    __FIAsyncOperation_1_Windows__CStorage__CStorageFile **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_CreateFileAsync, ( IStorageFolder *iface, HSTRING name,
    CreationCollisionOption options, __FIAsyncOperation_1_Windows__CStorage__CStorageFile **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_CreateFolderAsyncOverloadDefaultOptions, ( IStorageFolder *iface, HSTRING name,
    __FIAsyncOperation_1_Windows__CStorage__CStorageFolder **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_CreateFolderAsync, ( IStorageFolder *iface, HSTRING name,
    CreationCollisionOption options, __FIAsyncOperation_1_Windows__CStorage__CStorageFolder **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_GetFileAsync, ( IStorageFolder *iface, HSTRING name,
    __FIAsyncOperation_1_Windows__CStorage__CStorageFile **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_GetFolderAsync, ( IStorageFolder *iface, HSTRING name,
    __FIAsyncOperation_1_Windows__CStorage__CStorageFolder **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_GetItemAsync, ( IStorageFolder *iface, HSTRING name,
    __FIAsyncOperation_1_Windows__CStorage__CIStorageItem **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_GetFilesAsyncOverloadDefaultOptionsStartAndCount, ( IStorageFolder *iface,
    __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CStorageFile **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_GetFoldersAsyncOverloadDefaultOptionsStartAndCount, ( IStorageFolder *iface,
    __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CStorageFolder **operation ) )
STORAGE_FACTORY_STUB( storage_folder_def_GetItemsAsyncOverloadDefaultStartAndCount, ( IStorageFolder *iface,
    __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CIStorageItem **operation ) )

static const struct IStorageFolderVtbl storage_folder_def_vtbl =
{
    storage_folder_def_QueryInterface,
    storage_folder_def_AddRef,
    storage_folder_def_Release,
    storage_folder_def_GetIids,
    storage_folder_def_GetRuntimeClassName,
    storage_folder_def_GetTrustLevel,
    storage_folder_def_CreateFileAsyncOverloadDefaultOptions,
    storage_folder_def_CreateFileAsync,
    storage_folder_def_CreateFolderAsyncOverloadDefaultOptions,
    storage_folder_def_CreateFolderAsync,
    storage_folder_def_GetFileAsync,
    storage_folder_def_GetFolderAsync,
    storage_folder_def_GetItemAsync,
    storage_folder_def_GetFilesAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_def_GetFoldersAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_def_GetItemsAsyncOverloadDefaultStartAndCount,
};

static struct storage_folder_statics storage_folder_statics_obj =
{
    {&storage_folder_factory_vtbl},
    {&storage_folder_statics_vtbl},
    {&storage_folder_def_vtbl},
    1,
};

IActivationFactory *storage_folder_factory = &storage_folder_statics_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* FileIO & PathIO implementation                                             */
/* -------------------------------------------------------------------------- */

struct file_io_statics
{
    IActivationFactory IActivationFactory_iface;
    IFileIOStatics IFileIOStatics_iface;
    LONG ref;
};

static inline struct file_io_statics *impl_from_IActivationFactory_FileIO( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct file_io_statics, IActivationFactory_iface );
}

static HRESULT WINAPI file_io_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct file_io_statics *impl = impl_from_IActivationFactory_FileIO( iface );

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

    if (IsEqualGUID( iid, &IID_IFileIOStatics ))
    {
        *out = &impl->IFileIOStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI file_io_factory_AddRef( IActivationFactory *iface )
{
    struct file_io_statics *impl = impl_from_IActivationFactory_FileIO( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI file_io_factory_Release( IActivationFactory *iface )
{
    struct file_io_statics *impl = impl_from_IActivationFactory_FileIO( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI file_io_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IFileIOStatics,
    };
    IID *array;

    if (!iid_count || !iids) return E_INVALIDARG;
    if (!(array = CoTaskMemAlloc( sizeof(factory_iids) ))) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI file_io_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.FileIO", 21, class_name );
}

static HRESULT WINAPI file_io_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI file_io_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl file_io_factory_vtbl =
{
    file_io_factory_QueryInterface,
    file_io_factory_AddRef,
    file_io_factory_Release,
    file_io_factory_GetIids,
    file_io_factory_GetRuntimeClassName,
    file_io_factory_GetTrustLevel,
    file_io_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( file_io_statics, IFileIOStatics, struct file_io_statics, IActivationFactory_iface )

static HRESULT file_get_path( IStorageFile *file, WCHAR *path, DWORD max_len )
{
    IStorageItem *item = NULL;
    HSTRING path_str = NULL;
    const WCHAR *raw;
    HRESULT hr;

    if (FAILED(hr = IStorageFile_QueryInterface( file, &IID_IStorageItem, (void **)&item )))
        return hr;

    hr = IStorageItem_get_Path( item, &path_str );
    IStorageItem_Release( item );
    if (FAILED(hr)) return hr;

    raw = WindowsGetStringRawBuffer( path_str, NULL );
    lstrcpynW( path, raw, max_len );
    WindowsDeleteString( path_str );
    return S_OK;
}

static HRESULT file_io_read_text( const WCHAR *path, HSTRING *out_str )
{
    BYTE *bytes = NULL;
    DWORD size = 0;
    int len;
    WCHAR *wbuf;
    HRESULT hr;

    if (FAILED(hr = read_file_bytes( path, &bytes, &size )))
        return hr;

    if (!size)
    {
        free( bytes );
        return WindowsCreateString( L"", 0, out_str );
    }

    /* Convert UTF-8 to WCHAR */
    len = MultiByteToWideChar( CP_UTF8, 0, (char *)bytes, size, NULL, 0 );
    wbuf = malloc( (len + 1) * sizeof(WCHAR) );
    if (!wbuf)
    {
        free( bytes );
        return E_OUTOFMEMORY;
    }
    MultiByteToWideChar( CP_UTF8, 0, (char *)bytes, size, wbuf, len );
    wbuf[len] = 0;
    free( bytes );

    hr = WindowsCreateString( wbuf, len, out_str );
    free( wbuf );
    return hr;
}

static HRESULT file_io_write_text( const WCHAR *path, HSTRING contents, BOOL append )
{
    const WCHAR *raw = WindowsGetStringRawBuffer( contents, NULL );
    int len;
    char *utf8;
    HRESULT hr;

    len = WideCharToMultiByte( CP_UTF8, 0, raw, -1, NULL, 0, NULL, NULL );
    utf8 = malloc( len );
    if (!utf8) return E_OUTOFMEMORY;
    WideCharToMultiByte( CP_UTF8, 0, raw, -1, utf8, len, NULL, NULL );

    hr = write_file_bytes( path, (const BYTE *)utf8, len > 0 ? len - 1 : 0, append );
    free( utf8 );
    return hr;
}

static HRESULT WINAPI file_io_statics_ReadTextAsync( IFileIOStatics *iface, IStorageFile *file,
                                                    IAsyncOperation_HSTRING **operation )
{
    WCHAR path[MAX_PATH];
    HSTRING str;
    HRESULT hr;

    if (!file || !operation) return E_POINTER;
    if (FAILED(hr = file_get_path( file, path, MAX_PATH ))) return hr;
    if (FAILED(hr = file_io_read_text( path, &str ))) return hr;

    return async_operation_hstring_create( L"Windows.Foundation.IAsyncOperation`1<String>", str, operation );
}

static HRESULT WINAPI file_io_statics_ReadTextWithEncodingAsync( IFileIOStatics *iface, IStorageFile *file,
                                                                UnicodeEncoding encoding,
                                                                IAsyncOperation_HSTRING **operation )
{
    return IFileIOStatics_ReadTextAsync( iface, file, operation );
}

static HRESULT WINAPI file_io_statics_WriteTextAsync( IFileIOStatics *iface, IStorageFile *file,
                                                     HSTRING contents, IAsyncAction **operation )
{
    WCHAR path[MAX_PATH];
    HRESULT hr;

    if (!file || !operation) return E_POINTER;
    if (FAILED(hr = file_get_path( file, path, MAX_PATH ))) return hr;
    if (FAILED(hr = file_io_write_text( path, contents, FALSE ))) return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI file_io_statics_WriteTextWithEncodingAsync( IFileIOStatics *iface, IStorageFile *file,
                                                                 HSTRING contents, UnicodeEncoding encoding,
                                                                 IAsyncAction **operation )
{
    return IFileIOStatics_WriteTextAsync( iface, file, contents, operation );
}

static HRESULT WINAPI file_io_statics_AppendTextAsync( IFileIOStatics *iface, IStorageFile *file,
                                                      HSTRING contents, IAsyncAction **operation )
{
    WCHAR path[MAX_PATH];
    HRESULT hr;

    if (!file || !operation) return E_POINTER;
    if (FAILED(hr = file_get_path( file, path, MAX_PATH ))) return hr;
    if (FAILED(hr = file_io_write_text( path, contents, TRUE ))) return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI file_io_statics_AppendTextWithEncodingAsync( IFileIOStatics *iface, IStorageFile *file,
                                                                  HSTRING contents, UnicodeEncoding encoding,
                                                                  IAsyncAction **operation )
{
    return IFileIOStatics_AppendTextAsync( iface, file, contents, operation );
}

static HRESULT WINAPI file_io_statics_ReadLinesAsync( IFileIOStatics *iface, IStorageFile *file,
                                                     IAsyncOperation_IVector_HSTRING **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI file_io_statics_ReadLinesWithEncodingAsync( IFileIOStatics *iface, IStorageFile *file,
                                                                 UnicodeEncoding encoding,
                                                                 IAsyncOperation_IVector_HSTRING **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI file_io_statics_WriteLinesAsync( IFileIOStatics *iface, IStorageFile *file,
                                                      IIterable_HSTRING *lines, IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI file_io_statics_WriteLinesWithEncodingAsync( IFileIOStatics *iface, IStorageFile *file,
                                                                  IIterable_HSTRING *lines, UnicodeEncoding encoding,
                                                                  IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI file_io_statics_AppendLinesAsync( IFileIOStatics *iface, IStorageFile *file,
                                                       IIterable_HSTRING *lines, IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI file_io_statics_AppendLinesWithEncodingAsync( IFileIOStatics *iface, IStorageFile *file,
                                                                   IIterable_HSTRING *lines, UnicodeEncoding encoding,
                                                                   IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI file_io_statics_ReadBufferAsync( IFileIOStatics *iface, IStorageFile *file,
                                                      IAsyncOperation_IBuffer **operation )
{
    WCHAR path[MAX_PATH];
    BYTE *bytes = NULL;
    DWORD size = 0;
    IBuffer *buf = NULL;
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    HRESULT hr;

    if (!file || !operation) return E_POINTER;
    if (FAILED(hr = file_get_path( file, path, MAX_PATH ))) return hr;
    if (FAILED(hr = read_file_bytes( path, &bytes, &size ))) return hr;

    if (FAILED(hr = buffer_create( size, &buf )))
    {
        free( bytes );
        return hr;
    }

    if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
    {
        if (SUCCEEDED(IBufferByteAccess_Buffer( access, &dest )) && dest && size)
            memcpy( dest, bytes, size );
        IBufferByteAccess_Release( access );
    }
    IBuffer_put_Length( buf, size );
    free( bytes );

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IBuffer,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IBuffer>",
                                            (IInspectable *)buf, (void **)operation );
    IBuffer_Release( buf );
    return hr;
}

static HRESULT WINAPI file_io_statics_WriteBufferAsync( IFileIOStatics *iface, IStorageFile *file,
                                                       IBuffer *buffer, IAsyncAction **operation )
{
    WCHAR path[MAX_PATH];
    IBufferByteAccess *access = NULL;
    BYTE *data = NULL;
    UINT32 len = 0;
    HRESULT hr;

    if (!file || !buffer || !operation) return E_POINTER;
    if (FAILED(hr = file_get_path( file, path, MAX_PATH ))) return hr;

    IBuffer_get_Length( buffer, &len );
    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        return hr;

    hr = IBufferByteAccess_Buffer( access, &data );
    IBufferByteAccess_Release( access );
    if (FAILED(hr)) return hr;

    if (FAILED(hr = write_file_bytes( path, data, len, FALSE )))
        return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI file_io_statics_WriteBytesAsync( IFileIOStatics *iface, IStorageFile *file,
                                                      UINT32 count, BYTE *values, IAsyncAction **operation )
{
    WCHAR path[MAX_PATH];
    HRESULT hr;

    if (!file || !operation) return E_POINTER;
    if (FAILED(hr = file_get_path( file, path, MAX_PATH ))) return hr;
    if (FAILED(hr = write_file_bytes( path, values, count, FALSE ))) return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static const struct IFileIOStaticsVtbl file_io_statics_vtbl =
{
    file_io_statics_QueryInterface,
    file_io_statics_AddRef,
    file_io_statics_Release,
    file_io_statics_GetIids,
    file_io_statics_GetRuntimeClassName,
    file_io_statics_GetTrustLevel,
    file_io_statics_ReadTextAsync,
    file_io_statics_ReadTextWithEncodingAsync,
    file_io_statics_WriteTextAsync,
    file_io_statics_WriteTextWithEncodingAsync,
    file_io_statics_AppendTextAsync,
    file_io_statics_AppendTextWithEncodingAsync,
    file_io_statics_ReadLinesAsync,
    file_io_statics_ReadLinesWithEncodingAsync,
    file_io_statics_WriteLinesAsync,
    file_io_statics_WriteLinesWithEncodingAsync,
    file_io_statics_AppendLinesAsync,
    file_io_statics_AppendLinesWithEncodingAsync,
    file_io_statics_ReadBufferAsync,
    file_io_statics_WriteBufferAsync,
    file_io_statics_WriteBytesAsync,
};

static struct file_io_statics file_io_statics_obj =
{
    {&file_io_factory_vtbl},
    {&file_io_statics_vtbl},
    1,
};

IActivationFactory *file_io_factory = &file_io_statics_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* PathIO Statics                                                             */
/* -------------------------------------------------------------------------- */

struct path_io_statics
{
    IActivationFactory IActivationFactory_iface;
    IPathIOStatics IPathIOStatics_iface;
    LONG ref;
};

static inline struct path_io_statics *impl_from_IActivationFactory_PathIO( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct path_io_statics, IActivationFactory_iface );
}

static HRESULT WINAPI path_io_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct path_io_statics *impl = impl_from_IActivationFactory_PathIO( iface );

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

    if (IsEqualGUID( iid, &IID_IPathIOStatics ))
    {
        *out = &impl->IPathIOStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI path_io_factory_AddRef( IActivationFactory *iface )
{
    struct path_io_statics *impl = impl_from_IActivationFactory_PathIO( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI path_io_factory_Release( IActivationFactory *iface )
{
    struct path_io_statics *impl = impl_from_IActivationFactory_PathIO( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI path_io_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI path_io_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.PathIO", 21, class_name );
}

static HRESULT WINAPI path_io_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI path_io_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl path_io_factory_vtbl =
{
    path_io_factory_QueryInterface,
    path_io_factory_AddRef,
    path_io_factory_Release,
    path_io_factory_GetIids,
    path_io_factory_GetRuntimeClassName,
    path_io_factory_GetTrustLevel,
    path_io_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( path_io_statics, IPathIOStatics, struct path_io_statics, IActivationFactory_iface )

static HRESULT WINAPI path_io_statics_ReadTextAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                    IAsyncOperation_HSTRING **operation )
{
    const WCHAR *path;
    HSTRING str;
    HRESULT hr;

    if (!absolute_path || !operation) return E_POINTER;
    path = WindowsGetStringRawBuffer( absolute_path, NULL );
    if (FAILED(hr = file_io_read_text( path, &str ))) return hr;

    return async_operation_hstring_create( L"Windows.Foundation.IAsyncOperation`1<String>", str, operation );
}

static HRESULT WINAPI path_io_statics_ReadTextWithEncodingAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                                UnicodeEncoding encoding,
                                                                IAsyncOperation_HSTRING **operation )
{
    return IPathIOStatics_ReadTextAsync( iface, absolute_path, operation );
}

static HRESULT WINAPI path_io_statics_WriteTextAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                     HSTRING contents, IAsyncAction **operation )
{
    const WCHAR *path;
    HRESULT hr;

    if (!absolute_path || !operation) return E_POINTER;
    path = WindowsGetStringRawBuffer( absolute_path, NULL );
    if (FAILED(hr = file_io_write_text( path, contents, FALSE ))) return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI path_io_statics_WriteTextWithEncodingAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                                 HSTRING contents, UnicodeEncoding encoding,
                                                                 IAsyncAction **operation )
{
    return IPathIOStatics_WriteTextAsync( iface, absolute_path, contents, operation );
}

static HRESULT WINAPI path_io_statics_AppendTextAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                      HSTRING contents, IAsyncAction **operation )
{
    const WCHAR *path;
    HRESULT hr;

    if (!absolute_path || !operation) return E_POINTER;
    path = WindowsGetStringRawBuffer( absolute_path, NULL );
    if (FAILED(hr = file_io_write_text( path, contents, TRUE ))) return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI path_io_statics_AppendTextWithEncodingAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                                  HSTRING contents, UnicodeEncoding encoding,
                                                                  IAsyncAction **operation )
{
    return IPathIOStatics_AppendTextAsync( iface, absolute_path, contents, operation );
}

static HRESULT WINAPI path_io_statics_ReadLinesAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                     IAsyncOperation_IVector_HSTRING **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI path_io_statics_ReadLinesWithEncodingAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                                 UnicodeEncoding encoding,
                                                                 IAsyncOperation_IVector_HSTRING **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI path_io_statics_WriteLinesAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                      IIterable_HSTRING *lines, IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI path_io_statics_WriteLinesWithEncodingAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                                  IIterable_HSTRING *lines, UnicodeEncoding encoding,
                                                                  IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI path_io_statics_AppendLinesAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                       IIterable_HSTRING *lines, IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI path_io_statics_AppendLinesWithEncodingAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                                   IIterable_HSTRING *lines, UnicodeEncoding encoding,
                                                                   IAsyncAction **operation )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI path_io_statics_ReadBufferAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                      IAsyncOperation_IBuffer **operation )
{
    const WCHAR *path;
    BYTE *bytes = NULL;
    DWORD size = 0;
    IBuffer *buf = NULL;
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    HRESULT hr;

    if (!absolute_path || !operation) return E_POINTER;
    path = WindowsGetStringRawBuffer( absolute_path, NULL );
    if (FAILED(hr = read_file_bytes( path, &bytes, &size ))) return hr;

    if (FAILED(hr = buffer_create( size, &buf )))
    {
        free( bytes );
        return hr;
    }

    if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
    {
        if (SUCCEEDED(IBufferByteAccess_Buffer( access, &dest )) && dest && size)
            memcpy( dest, bytes, size );
        IBufferByteAccess_Release( access );
    }
    IBuffer_put_Length( buf, size );
    free( bytes );

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IBuffer,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IBuffer>",
                                            (IInspectable *)buf, (void **)operation );
    IBuffer_Release( buf );
    return hr;
}

static HRESULT WINAPI path_io_statics_WriteBufferAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                       IBuffer *buffer, IAsyncAction **operation )
{
    const WCHAR *path;
    IBufferByteAccess *access = NULL;
    BYTE *data = NULL;
    UINT32 len = 0;
    HRESULT hr;

    if (!absolute_path || !buffer || !operation) return E_POINTER;
    path = WindowsGetStringRawBuffer( absolute_path, NULL );

    IBuffer_get_Length( buffer, &len );
    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        return hr;

    hr = IBufferByteAccess_Buffer( access, &data );
    IBufferByteAccess_Release( access );
    if (FAILED(hr)) return hr;

    if (FAILED(hr = write_file_bytes( path, data, len, FALSE )))
        return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static HRESULT WINAPI path_io_statics_WriteBytesAsync( IPathIOStatics *iface, HSTRING absolute_path,
                                                      UINT32 count, BYTE *values, IAsyncAction **operation )
{
    const WCHAR *path;
    HRESULT hr;

    if (!absolute_path || !operation) return E_POINTER;
    path = WindowsGetStringRawBuffer( absolute_path, NULL );
    if (FAILED(hr = write_file_bytes( path, values, count, FALSE ))) return hr;

    return async_action_create( L"Windows.Foundation.IAsyncAction", S_OK, operation );
}

static const struct IPathIOStaticsVtbl path_io_statics_vtbl =
{
    path_io_statics_QueryInterface,
    path_io_statics_AddRef,
    path_io_statics_Release,
    path_io_statics_GetIids,
    path_io_statics_GetRuntimeClassName,
    path_io_statics_GetTrustLevel,
    path_io_statics_ReadTextAsync,
    path_io_statics_ReadTextWithEncodingAsync,
    path_io_statics_WriteTextAsync,
    path_io_statics_WriteTextWithEncodingAsync,
    path_io_statics_AppendTextAsync,
    path_io_statics_AppendTextWithEncodingAsync,
    path_io_statics_ReadLinesAsync,
    path_io_statics_ReadLinesWithEncodingAsync,
    path_io_statics_WriteLinesAsync,
    path_io_statics_WriteLinesWithEncodingAsync,
    path_io_statics_AppendLinesAsync,
    path_io_statics_AppendLinesWithEncodingAsync,
    path_io_statics_ReadBufferAsync,
    path_io_statics_WriteBufferAsync,
    path_io_statics_WriteBytesAsync,
};

static struct path_io_statics path_io_statics_obj =
{
    {&path_io_factory_vtbl},
    {&path_io_statics_vtbl},
    1,
};

IActivationFactory *path_io_factory = &path_io_statics_obj.IActivationFactory_iface;
