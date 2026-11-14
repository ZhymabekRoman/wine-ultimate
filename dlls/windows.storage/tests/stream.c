/* WinRT Windows.Storage.Streams Tests
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

#define COBJMACROS
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "psapi.h"

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

struct wct_abi_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IRandomAccessStreamWithContentType *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IRandomAccessStreamWithContentType *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IRandomAccessStreamWithContentType *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IRandomAccessStreamWithContentType *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IRandomAccessStreamWithContentType *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IRandomAccessStreamWithContentType *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *get_Size)( IRandomAccessStreamWithContentType *iface, UINT64 *value );
    HRESULT (STDMETHODCALLTYPE *put_Size)( IRandomAccessStreamWithContentType *iface, UINT64 value );
    HRESULT (STDMETHODCALLTYPE *GetInputStreamAt)( IRandomAccessStreamWithContentType *iface, UINT64 position, IInputStream **stream );
    HRESULT (STDMETHODCALLTYPE *GetOutputStreamAt)( IRandomAccessStreamWithContentType *iface, UINT64 position, IOutputStream **stream );
    HRESULT (STDMETHODCALLTYPE *get_Position)( IRandomAccessStreamWithContentType *iface, UINT64 *value );
    HRESULT (STDMETHODCALLTYPE *Seek)( IRandomAccessStreamWithContentType *iface, UINT64 position );
    HRESULT (STDMETHODCALLTYPE *CloneStream)( IRandomAccessStreamWithContentType *iface, IRandomAccessStream **stream );
    HRESULT (STDMETHODCALLTYPE *get_CanRead)( IRandomAccessStreamWithContentType *iface, boolean *value );
    HRESULT (STDMETHODCALLTYPE *get_CanWrite)( IRandomAccessStreamWithContentType *iface, boolean *value );
    HRESULT (STDMETHODCALLTYPE *get_ContentType)( IRandomAccessStreamWithContentType *iface, HSTRING *value );
};

struct test_completed_handler
{
    const void *lpVtbl;
    LONG ref;
    LONG called;
    AsyncStatus status;
};

static HRESULT WINAPI test_handler_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( &((struct test_completed_handler *)iface)->ref );
    return S_OK;
}

static ULONG WINAPI test_handler_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct test_completed_handler *)iface)->ref );
}

static ULONG WINAPI test_handler_Release( void *iface )
{
    return InterlockedDecrement( &((struct test_completed_handler *)iface)->ref );
}

static HRESULT WINAPI test_handler_Invoke( void *iface, void *sender, AsyncStatus status )
{
    struct test_completed_handler *impl = iface;
    impl->status = status;
    InterlockedIncrement( &impl->called );
    return S_OK;
}

static const void *test_handler_vtbl[] =
{
    test_handler_QueryInterface,
    test_handler_AddRef,
    test_handler_Release,
    test_handler_Invoke,
};

static void init_handler( struct test_completed_handler *h )
{
    h->lpVtbl = test_handler_vtbl;
    h->ref = 1;
    h->called = 0;
    h->status = Started;
}

static HRESULT create_in_memory_stream( IRandomAccessStream **out )
{
    IActivationFactory *factory = NULL;
    IInspectable *insp = NULL;
    HSTRING str;
    HRESULT hr;

    *out = NULL;
    hr = WindowsCreateString( L"Windows.Storage.Streams.InMemoryRandomAccessStream", 50, &str );
    ok( hr == S_OK, "WindowsCreateString failed, hr %#lx\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx\n", hr );
    if (FAILED(hr)) return hr;

    hr = IActivationFactory_ActivateInstance( factory, &insp );
    IActivationFactory_Release( factory );
    ok( hr == S_OK, "ActivateInstance failed, hr %#lx\n", hr );
    if (FAILED(hr)) return hr;

    hr = IInspectable_QueryInterface( insp, &IID_IRandomAccessStream, (void **)out );
    IInspectable_Release( insp );
    ok( hr == S_OK, "QI IRandomAccessStream failed, hr %#lx\n", hr );
    return hr;
}

static HRESULT create_buffer( UINT32 capacity, IBuffer **out )
{
    IBufferFactory *factory = NULL;
    HSTRING str;
    HRESULT hr;

    *out = NULL;
    WindowsCreateString( L"Windows.Storage.Streams.Buffer", 30, &str );
    hr = RoGetActivationFactory( str, &IID_IBufferFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory Buffer failed, hr %#lx\n", hr );
    if (FAILED(hr)) return hr;

    hr = IBufferFactory_Create( factory, capacity, out );
    IBufferFactory_Release( factory );
    return hr;
}

static HRESULT create_data_writer( IOutputStream *out_stream, IDataWriter **out )
{
    IDataWriterFactory *factory = NULL;
    HSTRING str;
    HRESULT hr;

    *out = NULL;
    WindowsCreateString( L"Windows.Storage.Streams.DataWriter", 34, &str );
    hr = RoGetActivationFactory( str, &IID_IDataWriterFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory DataWriter failed, hr %#lx\n", hr );
    if (FAILED(hr)) return hr;

    hr = IDataWriterFactory_CreateDataWriter( factory, out_stream, out );
    IDataWriterFactory_Release( factory );
    return hr;
}

static HRESULT create_data_reader( IInputStream *in_stream, IDataReader **out )
{
    IDataReaderFactory *factory = NULL;
    HSTRING str;
    HRESULT hr;

    *out = NULL;
    WindowsCreateString( L"Windows.Storage.Streams.DataReader", 34, &str );
    hr = RoGetActivationFactory( str, &IID_IDataReaderFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "RoGetActivationFactory DataReader failed, hr %#lx\n", hr );
    if (FAILED(hr)) return hr;

    hr = IDataReaderFactory_CreateDataReader( factory, in_stream, out );
    IDataReaderFactory_Release( factory );
    return hr;
}

static void check_async_status( IUnknown *op, AsyncStatus expected )
{
    IAsyncInfo *info = NULL;
    AsyncStatus status = (AsyncStatus)-1;
    HRESULT hr;

    hr = IUnknown_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    ok( hr == S_OK, "QI IAsyncInfo failed, hr %#lx\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IAsyncInfo_get_Status( info, &status );
        ok( hr == S_OK && status == expected, "expected status %d, got %d (hr %#lx)\n", expected, status, hr );
        IAsyncInfo_Release( info );
    }
}

static void test_clone_and_detach(void)
{
    IRandomAccessStream *stream = NULL, *clone = NULL, *detached_ras = NULL;
    IOutputStream *output = NULL, *detached_out = NULL;
    IInputStream *input = NULL, *input_at = NULL, *detached_in = NULL;
    IDataWriter *writer = NULL;
    IDataReader *reader = NULL;
    IClosable *closable = NULL;
    IAsyncOperation_UINT32 *u32_op = NULL;
    UINT64 size = 0, pos = 0;
    UINT32 res = 0;
    boolean can_read = FALSE, can_write = FALSE;
    BYTE payload[16] = { 0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                         0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f };
    BYTE read_buf[16];
    BYTE b = 0;
    HRESULT hr;

    hr = create_in_memory_stream( &stream );
    ok( hr == S_OK && stream != NULL, "create_in_memory_stream failed, hr %#lx\n", hr );
    if (FAILED(hr)) return;

    hr = IRandomAccessStream_QueryInterface( stream, &IID_IOutputStream, (void **)&output );
    ok( hr == S_OK, "QI IOutputStream failed, hr %#lx\n", hr );
    hr = create_data_writer( output, &writer );
    ok( hr == S_OK, "create_data_writer failed, hr %#lx\n", hr );
    IOutputStream_Release( output );

    hr = IDataWriter_WriteBytes( writer, 8, payload );
    ok( hr == S_OK, "WriteBytes failed, hr %#lx\n", hr );
    hr = IDataWriter_StoreAsync( writer, &u32_op );
    ok( hr == S_OK && u32_op != NULL, "StoreAsync failed, hr %#lx\n", hr );
    check_async_status( (IUnknown *)u32_op, Completed );
    hr = IAsyncOperation_UINT32_GetResults( u32_op, &res );
    ok( hr == S_OK && res == 8, "StoreAsync GetResults got %u, hr %#lx\n", res, hr );
    IAsyncOperation_UINT32_Release( u32_op );

    /* DetachStream + Close(DataWriter): original stream must retain size, CanRead, CanWrite */
    hr = IDataWriter_DetachStream( writer, &detached_out );
    ok( hr == S_OK && detached_out != NULL, "DetachStream failed, hr %#lx\n", hr );
    hr = IDataWriter_QueryInterface( writer, &IID_IClosable, (void **)&closable );
    ok( hr == S_OK, "QI IClosable on DataWriter failed, hr %#lx\n", hr );
    hr = IClosable_Close( closable );
    ok( hr == S_OK, "DataWriter Close failed, hr %#lx\n", hr );
    IClosable_Release( closable );
    IDataWriter_Release( writer );

    hr = IOutputStream_QueryInterface( detached_out, &IID_IRandomAccessStream, (void **)&detached_ras );
    ok( hr == S_OK, "QI IRandomAccessStream on detached failed, hr %#lx\n", hr );
    IOutputStream_Release( detached_out );

    hr = IRandomAccessStream_get_Size( detached_ras, &size );
    ok( hr == S_OK && size == 8, "expected size 8 after DetachStream, got %I64u (hr %#lx)\n", size, hr );
    hr = IRandomAccessStream_get_CanRead( detached_ras, &can_read );
    ok( hr == S_OK && can_read == TRUE, "expected CanRead TRUE after DetachStream, got %d (hr %#lx)\n", can_read, hr );
    hr = IRandomAccessStream_get_CanWrite( detached_ras, &can_write );
    ok( hr == S_OK && can_write == TRUE, "expected CanWrite TRUE after DetachStream, got %d (hr %#lx)\n", can_write, hr );
    hr = IRandomAccessStream_get_Position( detached_ras, &pos );
    ok( hr == S_OK && pos == 8, "expected Position 8 after StoreAsync, got %I64u (hr %#lx)\n", pos, hr );
    IRandomAccessStream_Release( detached_ras );

    /* CloneStream must share underlying buffer and have independent position */
    hr = IRandomAccessStream_CloneStream( stream, &clone );
    ok( hr == S_OK && clone != NULL, "CloneStream failed, hr %#lx\n", hr );

    hr = IRandomAccessStream_get_Position( clone, &pos );
    ok( hr == S_OK && pos == 0, "expected clone Position 0, got %I64u\n", pos );
    hr = IRandomAccessStream_get_Size( clone, &size );
    ok( hr == S_OK && size == 8, "expected clone Size 8, got %I64u\n", size );

    /* Write more via original; clone must immediately see Size == 16 and new bytes */
    hr = IRandomAccessStream_QueryInterface( stream, &IID_IOutputStream, (void **)&output );
    ok( hr == S_OK, "QI IOutputStream failed, hr %#lx\n", hr );
    hr = create_data_writer( output, &writer );
    ok( hr == S_OK, "create_data_writer failed, hr %#lx\n", hr );
    IOutputStream_Release( output );
    hr = IDataWriter_WriteBytes( writer, 8, payload + 8 );
    ok( hr == S_OK, "WriteBytes #2 failed, hr %#lx\n", hr );
    hr = IDataWriter_StoreAsync( writer, &u32_op );
    ok( hr == S_OK, "StoreAsync #2 failed, hr %#lx\n", hr );
    IAsyncOperation_UINT32_Release( u32_op );
    hr = IDataWriter_DetachStream( writer, &detached_out );
    ok( hr == S_OK, "DetachStream #2 failed, hr %#lx\n", hr );
    IOutputStream_Release( detached_out );
    IDataWriter_Release( writer );

    hr = IRandomAccessStream_get_Size( clone, &size );
    ok( hr == S_OK && size == 16, "clone did not share buffer growth: size %I64u (expected 16)\n", size );

    /* Write via clone at offset 0; original must observe the change */
    {
        IBuffer *wbuf = NULL;
        IBufferByteAccess *acc = NULL;
        BYTE *ptr = NULL;
        IAsyncOperationWithProgress_UINT32_UINT32 *wop = NULL;

        hr = create_buffer( 1, &wbuf );
        ok( hr == S_OK, "create_buffer failed, hr %#lx\n", hr );
        hr = IBuffer_QueryInterface( wbuf, &IID_IBufferByteAccess, (void **)&acc );
        ok( hr == S_OK, "QI IBufferByteAccess failed, hr %#lx\n", hr );
        IBufferByteAccess_Buffer( acc, &ptr );
        ptr[0] = 0xAA;
        IBufferByteAccess_Release( acc );
        IBuffer_put_Length( wbuf, 1 );

        hr = IRandomAccessStream_QueryInterface( clone, &IID_IOutputStream, (void **)&output );
        ok( hr == S_OK, "QI IOutputStream on clone failed, hr %#lx\n", hr );
        hr = IOutputStream_WriteAsync( output, wbuf, &wop );
        ok( hr == S_OK, "WriteAsync on clone failed, hr %#lx\n", hr );
        IAsyncOperationWithProgress_UINT32_UINT32_Release( wop );
        IOutputStream_Release( output );
        IBuffer_Release( wbuf );

        /* Original position must still be 16 (clone write did not touch orig position) */
        hr = IRandomAccessStream_get_Position( stream, &pos );
        ok( hr == S_OK && pos == 16, "orig position mutated by clone write: %I64u\n", pos );
    }

    /* GetInputStreamAt(4) must NOT clobber stream position */
    hr = IRandomAccessStream_GetInputStreamAt( stream, 4, &input_at );
    ok( hr == S_OK && input_at != NULL, "GetInputStreamAt failed, hr %#lx\n", hr );
    hr = IRandomAccessStream_get_Position( stream, &pos );
    ok( hr == S_OK && pos == 16, "GetInputStreamAt clobbered orig position: %I64u\n", pos );

    hr = create_data_reader( input_at, &reader );
    ok( hr == S_OK, "create_data_reader(input_at) failed, hr %#lx\n", hr );
    hr = IDataReader_LoadAsync( reader, 12, &u32_op );
    ok( hr == S_OK, "LoadAsync on input_at failed, hr %#lx\n", hr );
    IAsyncOperation_UINT32_GetResults( u32_op, &res );
    ok( res == 12, "expected 12 bytes from offset 4, got %u\n", res );
    IAsyncOperation_UINT32_Release( u32_op );
    memset( read_buf, 0, sizeof(read_buf) );
    hr = IDataReader_ReadBytes( reader, 12, read_buf );
    ok( hr == S_OK && !memcmp( read_buf, payload + 4, 12 ), "input_at data mismatch\n" );
    IDataReader_Release( reader );
    IInputStream_Release( input_at );

    /* Seek original to 0 and read via DataReader + DetachStream */
    hr = IRandomAccessStream_Seek( stream, 0 );
    ok( hr == S_OK, "Seek(0) failed, hr %#lx\n", hr );
    hr = IRandomAccessStream_QueryInterface( stream, &IID_IInputStream, (void **)&input );
    ok( hr == S_OK, "QI IInputStream failed, hr %#lx\n", hr );
    hr = create_data_reader( input, &reader );
    ok( hr == S_OK, "create_data_reader failed, hr %#lx\n", hr );
    IInputStream_Release( input );

    hr = IDataReader_LoadAsync( reader, 16, &u32_op );
    ok( hr == S_OK, "LoadAsync failed, hr %#lx\n", hr );
    IAsyncOperation_UINT32_GetResults( u32_op, &res );
    ok( res == 16, "expected 16 bytes, got %u\n", res );
    IAsyncOperation_UINT32_Release( u32_op );

    hr = IDataReader_ReadByte( reader, &b );
    ok( hr == S_OK && b == 0xAA, "expected clone-written 0xAA at offset 0, got %#x (hr %#lx)\n", b, hr );

    hr = IDataReader_DetachStream( reader, &detached_in );
    ok( hr == S_OK && detached_in != NULL, "DataReader DetachStream failed, hr %#lx\n", hr );
    hr = IDataReader_QueryInterface( reader, &IID_IClosable, (void **)&closable );
    ok( hr == S_OK, "QI IClosable on DataReader failed, hr %#lx\n", hr );
    hr = IClosable_Close( closable );
    ok( hr == S_OK, "DataReader Close failed, hr %#lx\n", hr );
    IClosable_Release( closable );
    IDataReader_Release( reader );

    hr = IInputStream_QueryInterface( detached_in, &IID_IRandomAccessStream, (void **)&detached_ras );
    ok( hr == S_OK, "QI IRandomAccessStream failed, hr %#lx\n", hr );
    IInputStream_Release( detached_in );
    hr = IRandomAccessStream_get_Size( detached_ras, &size );
    ok( hr == S_OK && size == 16, "expected size 16 after DataReader DetachStream+Close, got %I64u\n", size );
    hr = IRandomAccessStream_get_CanRead( detached_ras, &can_read );
    ok( hr == S_OK && can_read == TRUE, "expected CanRead TRUE after DataReader DetachStream+Close\n" );
    hr = IRandomAccessStream_get_CanWrite( detached_ras, &can_write );
    ok( hr == S_OK && can_write == TRUE, "expected CanWrite TRUE after DataReader DetachStream+Close\n" );
    IRandomAccessStream_Release( detached_ras );

    /* Close DataReader WITHOUT DetachStream: closes orig stream, leaves clone alive */
    hr = IRandomAccessStream_QueryInterface( stream, &IID_IInputStream, (void **)&input );
    ok( hr == S_OK, "QI IInputStream failed, hr %#lx\n", hr );
    hr = create_data_reader( input, &reader );
    ok( hr == S_OK, "create_data_reader failed, hr %#lx\n", hr );
    IInputStream_Release( input );
    hr = IDataReader_QueryInterface( reader, &IID_IClosable, (void **)&closable );
    ok( hr == S_OK, "QI IClosable failed, hr %#lx\n", hr );
    IClosable_Close( closable );
    IClosable_Release( closable );
    IDataReader_Release( reader );

    hr = IRandomAccessStream_get_CanRead( stream, &can_read );
    ok( hr == S_OK && can_read == FALSE, "expected closed orig CanRead FALSE, got %d\n", can_read );
    hr = IRandomAccessStream_get_CanWrite( stream, &can_write );
    ok( hr == S_OK && can_write == FALSE, "expected closed orig CanWrite FALSE, got %d\n", can_write );
    hr = IRandomAccessStream_get_Size( stream, &size );
    ok( hr == RO_E_CLOSED, "expected closed orig get_Size RO_E_CLOSED, got %#lx\n", hr );

    /* Clone must still be open and readable with size 16 */
    hr = IRandomAccessStream_get_CanRead( clone, &can_read );
    ok( hr == S_OK && can_read == TRUE, "expected clone CanRead TRUE after orig Close, got %d\n", can_read );
    hr = IRandomAccessStream_get_CanWrite( clone, &can_write );
    ok( hr == S_OK && can_write == TRUE, "expected clone CanWrite TRUE after orig Close, got %d\n", can_write );
    hr = IRandomAccessStream_get_Size( clone, &size );
    ok( hr == S_OK && size == 16, "expected clone Size 16 after orig Close, got %I64u (hr %#lx)\n", size, hr );

    IRandomAccessStream_Release( clone );
    IRandomAccessStream_Release( stream );
}

static void test_large_load_async(void)
{
    const UINT32 size_16mb = 16 * 1024 * 1024;
    const UINT32 chunk_1mb = 1024 * 1024;
    IRandomAccessStream *stream = NULL;
    IOutputStream *output = NULL;
    IInputStream *input = NULL;
    IBuffer *wbuf = NULL, *rbuf = NULL;
    IBufferByteAccess *acc = NULL;
    IDataReader *reader = NULL;
    IAsyncOperationWithProgress_UINT32_UINT32 *write_op = NULL;
    IAsyncOperation_UINT32 *load_op = NULL;
    struct test_completed_handler handler;
    BYTE *ptr = NULL;
    BYTE *chunk = NULL;
    UINT32 res = 0, unconsumed = 0, i;
    HRESULT hr;

    hr = create_in_memory_stream( &stream );
    ok( hr == S_OK, "create_in_memory_stream failed, hr %#lx\n", hr );
    if (FAILED(hr)) return;

    hr = create_buffer( size_16mb, &wbuf );
    ok( hr == S_OK, "create_buffer(16MB) failed, hr %#lx\n", hr );
    hr = IBuffer_QueryInterface( wbuf, &IID_IBufferByteAccess, (void **)&acc );
    ok( hr == S_OK, "QI IBufferByteAccess failed, hr %#lx\n", hr );
    hr = IBufferByteAccess_Buffer( acc, &ptr );
    ok( hr == S_OK && ptr != NULL, "Buffer() failed, hr %#lx\n", hr );
    for (i = 0; i < size_16mb; i += 4096)
    {
        ptr[i] = (BYTE)(i >> 12);
        ptr[i + 4095] = (BYTE)((i >> 12) ^ 0x5A);
    }
    IBufferByteAccess_Release( acc );
    IBuffer_put_Length( wbuf, size_16mb );

    hr = IRandomAccessStream_QueryInterface( stream, &IID_IOutputStream, (void **)&output );
    ok( hr == S_OK, "QI IOutputStream failed, hr %#lx\n", hr );
    hr = IOutputStream_WriteAsync( output, wbuf, &write_op );
    ok( hr == S_OK && write_op != NULL, "WriteAsync(16MB) failed, hr %#lx\n", hr );
    check_async_status( (IUnknown *)write_op, Completed );
    init_handler( &handler );
    hr = IAsyncOperationWithProgress_UINT32_UINT32_put_Completed( write_op, (void *)&handler );
    ok( hr == S_OK && handler.called == 1 && handler.status == Completed,
        "WriteAsync(16MB) put_Completed synchronous callback failed: called=%ld status=%d hr=%#lx\n",
        handler.called, handler.status, hr );
    hr = IAsyncOperationWithProgress_UINT32_UINT32_GetResults( write_op, &res );
    ok( hr == S_OK && res == size_16mb, "WriteAsync(16MB) GetResults=%u (hr %#lx)\n", res, hr );
    IAsyncOperationWithProgress_UINT32_UINT32_Release( write_op );
    IOutputStream_Release( output );
    IBuffer_Release( wbuf );

    /* Full 16MB DataReader.LoadAsync must complete synchronously (born-completed) */
    hr = IRandomAccessStream_Seek( stream, 0 );
    ok( hr == S_OK, "Seek(0) failed, hr %#lx\n", hr );
    hr = IRandomAccessStream_QueryInterface( stream, &IID_IInputStream, (void **)&input );
    ok( hr == S_OK, "QI IInputStream failed, hr %#lx\n", hr );
    hr = create_data_reader( input, &reader );
    ok( hr == S_OK, "create_data_reader failed, hr %#lx\n", hr );

    hr = IDataReader_LoadAsync( reader, size_16mb, &load_op );
    ok( hr == S_OK && load_op != NULL, "LoadAsync(16MB) failed, hr %#lx\n", hr );
    check_async_status( (IUnknown *)load_op, Completed );
    init_handler( &handler );
    hr = IAsyncOperation_UINT32_put_Completed( load_op, (void *)&handler );
    ok( hr == S_OK && handler.called == 1 && handler.status == Completed,
        "LoadAsync(16MB) put_Completed synchronous callback failed: called=%ld status=%d hr=%#lx\n",
        handler.called, handler.status, hr );
    hr = IAsyncOperation_UINT32_GetResults( load_op, &res );
    ok( hr == S_OK && res == size_16mb, "LoadAsync(16MB) GetResults=%u (hr %#lx)\n", res, hr );
    IAsyncOperation_UINT32_Release( load_op );

    hr = IDataReader_get_UnconsumedBufferLength( reader, &unconsumed );
    ok( hr == S_OK && unconsumed == size_16mb, "UnconsumedBufferLength=%u (expected 16MB)\n", unconsumed );

    hr = IDataReader_ReadBuffer( reader, size_16mb, &rbuf );
    ok( hr == S_OK && rbuf != NULL, "ReadBuffer(16MB) failed, hr %#lx\n", hr );
    hr = IBuffer_QueryInterface( rbuf, &IID_IBufferByteAccess, (void **)&acc );
    ok( hr == S_OK, "QI IBufferByteAccess on rbuf failed, hr %#lx\n", hr );
    hr = IBufferByteAccess_Buffer( acc, &ptr );
    ok( hr == S_OK && ptr != NULL, "Buffer() on rbuf failed, hr %#lx\n", hr );
    for (i = 0; i < size_16mb; i += 4096)
    {
        if (ptr[i] != (BYTE)(i >> 12) || ptr[i + 4095] != (BYTE)((i >> 12) ^ 0x5A))
        {
            ok( 0, "16MB pattern mismatch at offset %u\n", i );
            break;
        }
    }
    IBufferByteAccess_Release( acc );
    IBuffer_Release( rbuf );
    IDataReader_Release( reader );

    /* Second pass: 16 x 1MB LoadAsync + ReadBytes loop tests compaction / no churn */
    hr = IRandomAccessStream_Seek( stream, 0 );
    ok( hr == S_OK, "Seek(0) #2 failed, hr %#lx\n", hr );
    hr = create_data_reader( input, &reader );
    ok( hr == S_OK, "create_data_reader #2 failed, hr %#lx\n", hr );
    chunk = malloc( chunk_1mb );
    ok( chunk != NULL, "malloc(1MB) failed\n" );
    for (i = 0; i < 16; i++)
    {
        UINT32 page_idx = (i * chunk_1mb) >> 12;
        hr = IDataReader_LoadAsync( reader, chunk_1mb, &load_op );
        ok( hr == S_OK, "LoadAsync(1MB) iter %u failed, hr %#lx\n", i, hr );
        IAsyncOperation_UINT32_GetResults( load_op, &res );
        ok( res == chunk_1mb, "LoadAsync(1MB) iter %u got %u\n", i, res );
        IAsyncOperation_UINT32_Release( load_op );

        hr = IDataReader_ReadBytes( reader, chunk_1mb, chunk );
        ok( hr == S_OK, "ReadBytes(1MB) iter %u failed, hr %#lx\n", i, hr );
        ok( chunk[0] == (BYTE)page_idx && chunk[4095] == (BYTE)(page_idx ^ 0x5A),
            "1MB chunk %u data mismatch\n", i );
    }
    free( chunk );
    IDataReader_Release( reader );
    IInputStream_Release( input );
    IRandomAccessStream_Release( stream );
}

static void test_file_open_10k_reads(void)
{
    static const UINT32 file_size = 256 * 1024;
    static const UINT32 read_size = 64 * 1024;
    typedef BOOL (WINAPI *pfnGetProcessHandleCount)( HANDLE, DWORD * );
    pfnGetProcessHandleCount pGetProcessHandleCount;
    WCHAR temp_dir[MAX_PATH], temp_file[MAX_PATH];
    HANDLE hFile;
    BYTE *file_data;
    DWORD written = 0, handles_before = 0, handles_after = 0;
    PROCESS_MEMORY_COUNTERS pmc_before, pmc_after;
    IStorageFileStatics *file_statics = NULL;
    IAsyncOperation_StorageFile *file_op = NULL;
    IStorageFile *file = NULL;
    IAsyncOperation_IRandomAccessStream *open_op = NULL;
    IAsyncOperation_IRandomAccessStreamWithContentType *open_wct_op = NULL;
    IRandomAccessStream *stream = NULL;
    IRandomAccessStreamWithContentType *wct = NULL, *wct2 = NULL;
    IRandomAccessStreamReference *stream_ref = NULL;
    IInputStream *input = NULL;
    IBuffer *buf = NULL;
    HSTRING str, ct = NULL;
    const WCHAR *raw_ct;
    boolean can_read = FALSE, can_write = TRUE;
    UINT32 i, len = 0;
    HRESULT hr;

    GetTempPathW( MAX_PATH, temp_dir );
    GetTempFileNameW( temp_dir, L"wst", 0, temp_file );
    /* Rename to .txt so ContentType is text/plain */
    DeleteFileW( temp_file );
    lstrcatW( temp_file, L".txt" );

    file_data = malloc( file_size );
    ok( file_data != NULL, "malloc(256KB) failed\n" );
    for (i = 0; i < file_size; i++)
        file_data[i] = (BYTE)((i / read_size) + (i & 0xFF));

    hFile = CreateFileW( temp_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    ok( hFile != INVALID_HANDLE_VALUE, "CreateFileW failed: %lu\n", GetLastError() );
    WriteFile( hFile, file_data, file_size, &written, NULL );
    ok( written == file_size, "WriteFile wrote %lu of %u\n", written, file_size );
    CloseHandle( hFile );
    free( file_data );

    WindowsCreateString( L"Windows.Storage.StorageFile", 27, &str );
    hr = RoGetActivationFactory( str, &IID_IStorageFileStatics, (void **)&file_statics );
    WindowsDeleteString( str );
    ok( hr == S_OK && file_statics != NULL, "RoGetActivationFactory StorageFile failed, hr %#lx\n", hr );

    WindowsCreateString( temp_file, wcslen( temp_file ), &str );
    hr = IStorageFileStatics_GetFileFromPathAsync( file_statics, str, &file_op );
    WindowsDeleteString( str );
    ok( hr == S_OK, "GetFileFromPathAsync failed, hr %#lx\n", hr );
    hr = IAsyncOperation_StorageFile_GetResults( file_op, &file );
    ok( hr == S_OK && file != NULL, "GetResults StorageFile failed, hr %#lx\n", hr );
    IAsyncOperation_StorageFile_Release( file_op );
    IStorageFileStatics_Release( file_statics );

    /* Verify IRandomAccessStreamReference.OpenReadAsync returns a real 16-slot IRandomAccessStreamWithContentType */
    hr = IStorageFile_QueryInterface( file, &IID_IRandomAccessStreamReference, (void **)&stream_ref );
    ok( hr == S_OK, "QI IRandomAccessStreamReference failed, hr %#lx\n", hr );
    hr = IRandomAccessStreamReference_OpenReadAsync( stream_ref, &open_wct_op );
    ok( hr == S_OK && open_wct_op != NULL, "OpenReadAsync failed, hr %#lx\n", hr );
    hr = IAsyncOperation_IRandomAccessStreamWithContentType_GetResults( open_wct_op, &wct2 );
    ok( hr == S_OK && wct2 != NULL, "OpenReadAsync GetResults failed, hr %#lx\n", hr );
    IAsyncOperation_IRandomAccessStreamWithContentType_Release( open_wct_op );
    hr = ((const struct wct_abi_vtbl *)wct2->lpVtbl)->get_ContentType( wct2, &ct );
    ok( hr == S_OK, "wct2 slot-15 get_ContentType failed, hr %#lx\n", hr );
    raw_ct = WindowsGetStringRawBuffer( ct, NULL );
    ok( !wcscmp( raw_ct, L"text/plain" ), "expected text/plain, got %s\n", wine_dbgstr_w( raw_ct ) );
    WindowsDeleteString( ct );
    IRandomAccessStreamWithContentType_Release( wct2 );
    IRandomAccessStreamReference_Release( stream_ref );

    /* OpenAsync(FileAccessMode_Read) -> IRandomAccessStreamWithContentType */
    hr = IStorageFile_OpenAsync( file, FileAccessMode_Read, &open_op );
    ok( hr == S_OK, "OpenAsync(Read) failed, hr %#lx\n", hr );
    check_async_status( (IUnknown *)open_op, Completed );
    hr = IAsyncOperation_IRandomAccessStream_GetResults( open_op, &stream );
    ok( hr == S_OK && stream != NULL, "OpenAsync GetResults failed, hr %#lx\n", hr );
    IAsyncOperation_IRandomAccessStream_Release( open_op );

    hr = IRandomAccessStream_QueryInterface( stream, &IID_IRandomAccessStreamWithContentType, (void **)&wct );
    ok( hr == S_OK && wct != NULL, "QI IRandomAccessStreamWithContentType failed, hr %#lx\n", hr );
    hr = IRandomAccessStream_get_CanRead( stream, &can_read );
    ok( hr == S_OK && can_read == TRUE, "expected CanRead TRUE, got %d\n", can_read );
    hr = IRandomAccessStream_get_CanWrite( stream, &can_write );
    ok( hr == S_OK && can_write == FALSE, "expected CanWrite FALSE on Read stream, got %d\n", can_write );

    hr = IRandomAccessStreamWithContentType_QueryInterface( wct, &IID_IInputStream, (void **)&input );
    ok( hr == S_OK && input != NULL, "QI IInputStream failed, hr %#lx\n", hr );
    hr = create_buffer( read_size, &buf );
    ok( hr == S_OK && buf != NULL, "create_buffer(64KB) failed, hr %#lx\n", hr );

    pGetProcessHandleCount = (pfnGetProcessHandleCount)GetProcAddress( GetModuleHandleA( "kernel32.dll" ),
                                                                       "GetProcessHandleCount" );

    /* Warmup 50 reads before baseline handle/memory snapshot */
    for (i = 0; i < 50; i++)
    {
        IAsyncOperationWithProgress_IBuffer_UINT32 *rop = NULL;
        IBuffer *res_buf = NULL;
        IRandomAccessStream_Seek( stream, (UINT64)(i % 4) * read_size );
        IInputStream_ReadAsync( input, buf, read_size, InputStreamOptions_None, &rop );
        IAsyncOperationWithProgress_IBuffer_UINT32_GetResults( rop, &res_buf );
        if (res_buf) IBuffer_Release( res_buf );
        IAsyncOperationWithProgress_IBuffer_UINT32_Release( rop );
    }

    if (pGetProcessHandleCount) pGetProcessHandleCount( GetCurrentProcess(), &handles_before );
    memset( &pmc_before, 0, sizeof(pmc_before) );
    pmc_before.cb = sizeof(pmc_before);
    GetProcessMemoryInfo( GetCurrentProcess(), &pmc_before, sizeof(pmc_before) );

    /* 10,000 x 64KB ReadAsync loop */
    for (i = 0; i < 10000; i++)
    {
        IAsyncOperationWithProgress_IBuffer_UINT32 *rop = NULL;
        IBuffer *res_buf = NULL;
        IBufferByteAccess *acc = NULL;
        BYTE *ptr = NULL;
        UINT32 chunk_idx = i % 4;

        hr = IRandomAccessStream_Seek( stream, (UINT64)chunk_idx * read_size );
        if (FAILED(hr)) { ok( 0, "Seek failed at iter %u, hr %#lx\n", i, hr ); break; }

        hr = IInputStream_ReadAsync( input, buf, read_size, InputStreamOptions_None, &rop );
        if (FAILED(hr) || !rop) { ok( 0, "ReadAsync failed at iter %u, hr %#lx\n", i, hr ); break; }

        hr = IAsyncOperationWithProgress_IBuffer_UINT32_GetResults( rop, &res_buf );
        if (FAILED(hr) || !res_buf) { ok( 0, "GetResults failed at iter %u, hr %#lx\n", i, hr ); IAsyncOperationWithProgress_IBuffer_UINT32_Release( rop ); break; }

        IBuffer_get_Length( res_buf, &len );
        if (len != read_size) { ok( 0, "iter %u read length %u != 65536\n", i, len ); }
        if (i % 1000 == 0 && SUCCEEDED(IBuffer_QueryInterface( res_buf, &IID_IBufferByteAccess, (void **)&acc )))
        {
            IBufferByteAccess_Buffer( acc, &ptr );
            ok( ptr[0] == (BYTE)chunk_idx && ptr[read_size - 1] == (BYTE)(chunk_idx + 0xFF),
                "iter %u data mismatch\n", i );
            IBufferByteAccess_Release( acc );
        }

        IBuffer_Release( res_buf );
        IAsyncOperationWithProgress_IBuffer_UINT32_Release( rop );
    }
    ok( i == 10000, "completed %u of 10000 ReadAsync(64KB) iterations\n", i );

    if (pGetProcessHandleCount)
    {
        pGetProcessHandleCount( GetCurrentProcess(), &handles_after );
        ok( handles_after <= handles_before + 2, "handle leak across 10k reads: before=%lu after=%lu\n",
            handles_before, handles_after );
    }
    memset( &pmc_after, 0, sizeof(pmc_after) );
    pmc_after.cb = sizeof(pmc_after);
    GetProcessMemoryInfo( GetCurrentProcess(), &pmc_after, sizeof(pmc_after) );
    ok( pmc_after.PagefileUsage <= pmc_before.PagefileUsage + 8 * 1024 * 1024,
        "memory leak across 10k reads: before=%Iu after=%Iu\n",
        pmc_before.PagefileUsage, pmc_after.PagefileUsage );

    IBuffer_Release( buf );
    IInputStream_Release( input );
    IRandomAccessStreamWithContentType_Release( wct );
    IRandomAccessStream_Release( stream );
    IStorageFile_Release( file );
    DeleteFileW( temp_file );
}

START_TEST(stream)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_clone_and_detach();
    test_large_load_async();
    test_file_open_10k_reads();

    RoUninitialize();
}
