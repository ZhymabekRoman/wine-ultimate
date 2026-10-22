/* WinRT Windows.Storage Tests
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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

#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.h"
#include "windows.storage.streams.h"
#include "robuffer.h"

#include "wine/test.h"

#define check_interface( obj, iid, is_broken ) check_interface_( __LINE__, obj, iid, is_broken )
static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL is_broken )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK || broken( is_broken && hr == E_NOINTERFACE ), "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
        IUnknown_Release( unk );
}

static void test_RandomAccessStreamReference(void)
{
    static const WCHAR *random_access_stream_reference_statics_name = L"Windows.Storage.Streams.RandomAccessStreamReference";
    IRandomAccessStreamReferenceStatics *random_access_stream_reference_statics = (void *)0xdeadbeef;
    IRandomAccessStreamReference *random_access_stream_reference = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( random_access_stream_reference_statics_name, wcslen( random_access_stream_reference_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( random_access_stream_reference_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, FALSE );
    check_interface( factory, &IID_IInspectable, FALSE );
    check_interface( factory, &IID_IAgileObject, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IRandomAccessStreamReferenceStatics, (void **)&random_access_stream_reference_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IRandomAccessStreamReferenceStatics_CreateFromStream( random_access_stream_reference_statics, NULL, &random_access_stream_reference );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    ok( random_access_stream_reference == NULL, "IRandomAccessStreamReferenceStatics_CreateFromStream returned %p.\n", random_access_stream_reference );

    ref = IRandomAccessStreamReferenceStatics_Release( random_access_stream_reference_statics );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 0, "got ref %ld.\n", ref );
}

static void test_Buffer(void)
{
    static const WCHAR *class_name = L"Windows.Storage.Streams.Buffer";
    IBufferByteAccess *byte_access = NULL;
    IBufferFactory *factory = NULL;
    IBuffer *buffer = NULL;
    BYTE *data = NULL;
    UINT32 val;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoGetActivationFactory( str, &IID_IBufferFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr );

    hr = IBufferFactory_Create( factory, 128, &buffer );
    ok( hr == S_OK, "IBufferFactory_Create failed, hr %#lx\n", hr );

    val = 0xdeadbeef;
    hr = IBuffer_get_Capacity( buffer, &val );
    ok( hr == S_OK && val == 128, "get_Capacity failed, hr %#lx, val %u\n", hr, val );

    val = 0xdeadbeef;
    hr = IBuffer_get_Length( buffer, &val );
    ok( hr == S_OK && val == 0, "get_Length failed, hr %#lx, val %u\n", hr, val );

    hr = IBuffer_put_Length( buffer, 64 );
    ok( hr == S_OK, "put_Length failed, hr %#lx\n", hr );

    hr = IBuffer_get_Length( buffer, &val );
    ok( hr == S_OK && val == 64, "get_Length failed, hr %#lx, val %u\n", hr, val );

    hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&byte_access );
    ok( hr == S_OK, "QI IBufferByteAccess failed, hr %#lx\n", hr );

    hr = IBufferByteAccess_Buffer( byte_access, &data );
    ok( hr == S_OK && data != NULL, "Buffer failed, hr %#lx, data %p\n", hr, data );

    IBufferByteAccess_Release( byte_access );
    IBuffer_Release( buffer );
    IBufferFactory_Release( factory );
}

static void test_InMemoryRandomAccessStream(void)
{
    static const WCHAR *class_name = L"Windows.Storage.Streams.InMemoryRandomAccessStream";
    IActivationFactory *factory = NULL;
    IInspectable *inspectable = NULL;
    IRandomAccessStream *stream = NULL;
    IInputStream *input_stream = NULL;
    IOutputStream *output_stream = NULL;
    IClosable *closable = NULL;
    UINT64 size = 0, pos = 0;
    boolean can_read = FALSE, can_write = FALSE;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr );

    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    ok( hr == S_OK, "ActivateInstance failed, hr %#lx\n", hr );

    hr = IInspectable_QueryInterface( inspectable, &IID_IRandomAccessStream, (void **)&stream );
    ok( hr == S_OK, "QI IRandomAccessStream failed, hr %#lx\n", hr );

    hr = IRandomAccessStream_QueryInterface( stream, &IID_IInputStream, (void **)&input_stream );
    ok( hr == S_OK, "QI IInputStream failed, hr %#lx\n", hr );

    hr = IRandomAccessStream_QueryInterface( stream, &IID_IOutputStream, (void **)&output_stream );
    ok( hr == S_OK, "QI IOutputStream failed, hr %#lx\n", hr );

    hr = IRandomAccessStream_QueryInterface( stream, &IID_IClosable, (void **)&closable );
    ok( hr == S_OK, "QI IClosable failed, hr %#lx\n", hr );

    hr = IRandomAccessStream_get_Size( stream, &size );
    ok( hr == S_OK && size == 0, "get_Size failed, hr %#lx, size %llu\n", hr, size );

    hr = IRandomAccessStream_get_Position( stream, &pos );
    ok( hr == S_OK && pos == 0, "get_Position failed, hr %#lx, pos %llu\n", hr, pos );

    hr = IRandomAccessStream_get_CanRead( stream, &can_read );
    ok( hr == S_OK && can_read == TRUE, "get_CanRead failed, hr %#lx\n", hr );

    hr = IRandomAccessStream_get_CanWrite( stream, &can_write );
    ok( hr == S_OK && can_write == TRUE, "get_CanWrite failed, hr %#lx\n", hr );

    IClosable_Release( closable );
    IOutputStream_Release( output_stream );
    IInputStream_Release( input_stream );
    IRandomAccessStream_Release( stream );
    IInspectable_Release( inspectable );
    IActivationFactory_Release( factory );
}

static void test_DataReader_DataWriter(void)
{
    static const WCHAR *writer_name = L"Windows.Storage.Streams.DataWriter";
    static const WCHAR *reader_name = L"Windows.Storage.Streams.DataReader";
    IActivationFactory *writer_factory = NULL;
    IDataReaderStatics *reader_statics = NULL;
    IInspectable *inspectable = NULL;
    IDataWriter *writer = NULL;
    IDataReader *reader = NULL;
    IBuffer *buffer = NULL;
    HSTRING str, read_str = NULL;
    UINT32 unstored = 0, unconsumed = 0, code_units = 0;
    BYTE b_val = 0;
    INT32 i32_val = 0;
    const WCHAR *raw_read;
    HRESULT hr;

    hr = WindowsCreateString( writer_name, wcslen( writer_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&writer_factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory DataWriter failed, hr %#lx\n", hr );

    hr = IActivationFactory_ActivateInstance( writer_factory, &inspectable );
    ok( hr == S_OK, "ActivateInstance DataWriter failed, hr %#lx\n", hr );

    hr = IInspectable_QueryInterface( inspectable, &IID_IDataWriter, (void **)&writer );
    ok( hr == S_OK, "QI IDataWriter failed, hr %#lx\n", hr );

    hr = IDataWriter_WriteByte( writer, 0x42 );
    ok( hr == S_OK, "WriteByte failed, hr %#lx\n", hr );

    hr = IDataWriter_WriteInt32( writer, 123456 );
    ok( hr == S_OK, "WriteInt32 failed, hr %#lx\n", hr );

    hr = WindowsCreateString( L"Wine Storage", 12, &str );
    hr = IDataWriter_WriteString( writer, str, &code_units );
    WindowsDeleteString( str );
    ok( hr == S_OK, "WriteString failed, hr %#lx\n", hr );

    hr = IDataWriter_get_UnstoredBufferLength( writer, &unstored );
    ok( hr == S_OK && unstored > 0, "get_UnstoredBufferLength failed, len %u\n", unstored );

    hr = IDataWriter_DetachBuffer( writer, &buffer );
    ok( hr == S_OK && buffer != NULL, "DetachBuffer failed, hr %#lx\n", hr );

    /* Read it back using DataReader */
    hr = WindowsCreateString( reader_name, wcslen( reader_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IDataReaderStatics, (void **)&reader_statics );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory DataReaderStatics failed, hr %#lx\n", hr );

    hr = IDataReaderStatics_FromBuffer( reader_statics, buffer, &reader );
    ok( hr == S_OK && reader != NULL, "FromBuffer failed, hr %#lx\n", hr );

    hr = IDataReader_get_UnconsumedBufferLength( reader, &unconsumed );
    ok( hr == S_OK && unconsumed == unstored, "get_UnconsumedBufferLength failed, unconsumed %u != unstored %u\n", unconsumed, unstored );

    hr = IDataReader_ReadByte( reader, &b_val );
    ok( hr == S_OK && b_val == 0x42, "ReadByte failed, hr %#lx, val %#x\n", hr, b_val );

    hr = IDataReader_ReadInt32( reader, &i32_val );
    ok( hr == S_OK && i32_val == 123456, "ReadInt32 failed, hr %#lx, val %ld\n", hr, i32_val );

    hr = IDataReader_ReadString( reader, 12, &read_str );
    ok( hr == S_OK, "ReadString failed, hr %#lx\n", hr );
    raw_read = WindowsGetStringRawBuffer( read_str, NULL );
    ok( !wcscmp( raw_read, L"Wine Storage" ), "read text mismatch: %s\n", wine_dbgstr_w( raw_read ) );
    WindowsDeleteString( read_str );

    IDataReader_Release( reader );
    IDataReaderStatics_Release( reader_statics );
    IBuffer_Release( buffer );
    IDataWriter_Release( writer );
    IInspectable_Release( inspectable );
    IActivationFactory_Release( writer_factory );
}

static void test_StorageFile_StorageFolder(void)
{
    static const WCHAR *file_statics_name = L"Windows.Storage.StorageFile";
    static const WCHAR *folder_statics_name = L"Windows.Storage.StorageFolder";
    IStorageFileStatics *file_statics = NULL;
    IStorageFolderStatics *folder_statics = NULL;
    IAsyncOperation_StorageFile *file_op = NULL;
    IAsyncOperation_StorageFolder *folder_op = NULL;
    IStorageFile *file = NULL;
    IStorageFolder *folder = NULL;
    IStorageItem *item = NULL;
    HSTRING str, name_str = NULL, path_str = NULL, type_str = NULL;
    const WCHAR *raw;
    boolean is_file = FALSE, is_folder = FALSE;
    WCHAR temp_dir[MAX_PATH];
    WCHAR temp_file[MAX_PATH];
    HRESULT hr;

    GetTempPathW( MAX_PATH, temp_dir );
    GetTempFileNameW( temp_dir, L"wns", 0, temp_file );

    /* Test StorageFile */
    hr = WindowsCreateString( file_statics_name, wcslen( file_statics_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IStorageFileStatics, (void **)&file_statics );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory StorageFile failed, hr %#lx\n", hr );

    hr = WindowsCreateString( temp_file, wcslen( temp_file ), &str );
    hr = IStorageFileStatics_GetFileFromPathAsync( file_statics, str, &file_op );
    WindowsDeleteString( str );
    ok( hr == S_OK, "GetFileFromPathAsync failed, hr %#lx\n", hr );

    hr = IAsyncOperation_StorageFile_GetResults( file_op, &file );
    ok( hr == S_OK && file != NULL, "GetResults StorageFile failed, hr %#lx\n", hr );
    IAsyncOperation_StorageFile_Release( file_op );

    hr = IStorageFile_get_FileType( file, &type_str );
    ok( hr == S_OK, "get_FileType failed, hr %#lx\n", hr );
    WindowsDeleteString( type_str );

    hr = IStorageFile_QueryInterface( file, &IID_IStorageItem, (void **)&item );
    ok( hr == S_OK, "QI IStorageItem failed, hr %#lx\n", hr );

    hr = IStorageItem_get_Name( item, &name_str );
    ok( hr == S_OK, "get_Name failed, hr %#lx\n", hr );
    WindowsDeleteString( name_str );

    hr = IStorageItem_get_Path( item, &path_str );
    ok( hr == S_OK, "get_Path failed, hr %#lx\n", hr );
    raw = WindowsGetStringRawBuffer( path_str, NULL );
    ok( !wcsicmp( raw, temp_file ), "path mismatch: %s vs %s\n", wine_dbgstr_w( raw ), wine_dbgstr_w( temp_file ) );
    WindowsDeleteString( path_str );

    hr = IStorageItem_IsOfType( item, StorageItemTypes_File, &is_file );
    ok( hr == S_OK && is_file == TRUE, "IsOfType File failed\n" );

    IStorageItem_Release( item );
    IStorageFile_Release( file );
    IStorageFileStatics_Release( file_statics );

    /* Test StorageFolder */
    hr = WindowsCreateString( folder_statics_name, wcslen( folder_statics_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IStorageFolderStatics, (void **)&folder_statics );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory StorageFolder failed, hr %#lx\n", hr );

    hr = WindowsCreateString( temp_dir, wcslen( temp_dir ), &str );
    hr = IStorageFolderStatics_GetFolderFromPathAsync( folder_statics, str, &folder_op );
    WindowsDeleteString( str );
    ok( hr == S_OK, "GetFolderFromPathAsync failed, hr %#lx\n", hr );

    hr = IAsyncOperation_StorageFolder_GetResults( folder_op, &folder );
    ok( hr == S_OK && folder != NULL, "GetResults StorageFolder failed, hr %#lx\n", hr );
    IAsyncOperation_StorageFolder_Release( folder_op );

    hr = IStorageFolder_QueryInterface( folder, &IID_IStorageItem, (void **)&item );
    ok( hr == S_OK, "QI IStorageItem on folder failed, hr %#lx\n", hr );

    hr = IStorageItem_IsOfType( item, StorageItemTypes_Folder, &is_folder );
    ok( hr == S_OK && is_folder == TRUE, "IsOfType Folder failed\n" );

    IStorageItem_Release( item );
    IStorageFolder_Release( folder );
    IStorageFolderStatics_Release( folder_statics );

    DeleteFileW( temp_file );
}

static void test_FileIO_PathIO(void)
{
    static const WCHAR *file_io_name = L"Windows.Storage.FileIO";
    static const WCHAR *path_io_name = L"Windows.Storage.PathIO";
    IFileIOStatics *file_io = NULL;
    IPathIOStatics *path_io = NULL;
    IStorageFileStatics *file_statics = NULL;
    IStorageFile *file = NULL;
    IAsyncOperation_StorageFile *file_op = NULL;
    IAsyncAction *action = NULL;
    IAsyncOperation_HSTRING *read_op = NULL;
    HSTRING str, text_in, text_out = NULL;
    const WCHAR *raw_out;
    WCHAR temp_dir[MAX_PATH];
    WCHAR temp_file[MAX_PATH];
    HRESULT hr;

    GetTempPathW( MAX_PATH, temp_dir );
    GetTempFileNameW( temp_dir, L"wnio", 0, temp_file );

    /* PathIO tests */
    hr = WindowsCreateString( path_io_name, wcslen( path_io_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IPathIOStatics, (void **)&path_io );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory PathIO failed, hr %#lx\n", hr );

    hr = WindowsCreateString( temp_file, wcslen( temp_file ), &str );
    hr = WindowsCreateString( L"PathIO test content", 19, &text_in );
    hr = IPathIOStatics_WriteTextAsync( path_io, str, text_in, &action );
    ok( hr == S_OK, "WriteTextAsync failed, hr %#lx\n", hr );
    IAsyncAction_Release( action );
    WindowsDeleteString( text_in );

    hr = IPathIOStatics_ReadTextAsync( path_io, str, &read_op );
    ok( hr == S_OK, "ReadTextAsync failed, hr %#lx\n", hr );
    hr = IAsyncOperation_HSTRING_GetResults( read_op, &text_out );
    ok( hr == S_OK, "GetResults ReadTextAsync failed, hr %#lx\n", hr );
    IAsyncOperation_HSTRING_Release( read_op );

    raw_out = WindowsGetStringRawBuffer( text_out, NULL );
    ok( !wcscmp( raw_out, L"PathIO test content" ), "PathIO content mismatch: %s\n", wine_dbgstr_w( raw_out ) );
    WindowsDeleteString( text_out );
    WindowsDeleteString( str );
    IPathIOStatics_Release( path_io );

    /* FileIO tests */
    hr = WindowsCreateString( L"Windows.Storage.StorageFile", 26, &str );
    hr = RoGetActivationFactory( str, &IID_IStorageFileStatics, (void **)&file_statics );
    WindowsDeleteString( str );

    hr = WindowsCreateString( temp_file, wcslen( temp_file ), &str );
    hr = IStorageFileStatics_GetFileFromPathAsync( file_statics, str, &file_op );
    WindowsDeleteString( str );
    hr = IAsyncOperation_StorageFile_GetResults( file_op, &file );
    IAsyncOperation_StorageFile_Release( file_op );
    IStorageFileStatics_Release( file_statics );

    hr = WindowsCreateString( file_io_name, wcslen( file_io_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IFileIOStatics, (void **)&file_io );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory FileIO failed, hr %#lx\n", hr );

    hr = WindowsCreateString( L"FileIO updated content", 22, &text_in );
    hr = IFileIOStatics_WriteTextAsync( file_io, file, text_in, &action );
    ok( hr == S_OK, "WriteTextAsync FileIO failed, hr %#lx\n", hr );
    IAsyncAction_Release( action );
    WindowsDeleteString( text_in );

    hr = IFileIOStatics_ReadTextAsync( file_io, file, &read_op );
    ok( hr == S_OK, "ReadTextAsync FileIO failed, hr %#lx\n", hr );
    hr = IAsyncOperation_HSTRING_GetResults( read_op, &text_out );
    ok( hr == S_OK, "GetResults FileIO failed, hr %#lx\n", hr );
    IAsyncOperation_HSTRING_Release( read_op );

    raw_out = WindowsGetStringRawBuffer( text_out, NULL );
    ok( !wcscmp( raw_out, L"FileIO updated content" ), "FileIO content mismatch: %s\n", wine_dbgstr_w( raw_out ) );
    WindowsDeleteString( text_out );

    IFileIOStatics_Release( file_io );
    IStorageFile_Release( file );

    DeleteFileW( temp_file );
}

START_TEST(storage)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_RandomAccessStreamReference();
    test_Buffer();
    test_InMemoryRandomAccessStream();
    test_DataReader_DataWriter();
    test_StorageFile_StorageFolder();
    test_FileIO_PathIO();

    RoUninitialize();
}
