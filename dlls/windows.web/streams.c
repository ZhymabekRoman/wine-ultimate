/* Minimal Windows.Storage.Streams support for the windows.web builtin.
 *
 * WindowsPackageManager.dll's Downloader reads the HTTP response body through
 * IHttpContent::GetBufferAsync() (IAsyncOperation<IBuffer*>) and then copies
 * the bytes out of a Windows.Storage.Streams.DataReader (IDataReaderStatics::
 * FromBuffer + ReadBytes). Wine has no windows.storage.streams builtin, so
 * host the two classes (IBuffer memory implementation and DataReader) inside
 * windows.web and expose the DataReader factory via DllGetActivationFactory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdlib.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "wine/debug.h"

#include "private.h"

#include <stdio.h>

#define SDBG(...) TRACE( __VA_ARGS__ )

WINE_DEFAULT_DEBUG_CHANNEL(web);

static HRESULT WINAPI ras_ReadAsync( void *iface, void *buffer, void **op );

/* -------------------------------------------------------------------------- */
/* IBuffer: in-memory byte buffer (Windows.Storage.Streams.IBuffer)
 * ABI: 0-5 IInspectable, 6 Length, 7 Capacity, 8 Start (BYTE*). */

struct mem_buffer
{
    const void *vtbl;
    const void *vtbl_byte_access;
    LONG ref;
    BYTE *data;
    ULONG len;
};

static const void *mem_buffer_vtbl[];
static const void *mem_buffer_ba_vtbl[];

struct buffer_iter
{
    const void *vtbl;
    LONG ref;
    struct mem_buffer *buf;
    ULONG pos;
};

static const GUID IID_IBuffer_s =
    { 0x905a0fe0, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
static const GUID IID_IRandomAccessStream_s =
    { 0x905a0fe1, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
static const GUID IID_IInputStream_s =
    { 0x905a0fe2, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
static const GUID IID_IBufferByteAccess_s =
    { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
static const GUID IID_IBufferFactory_s =
    { 0x71af914d, 0xc10f, 0x484b, { 0xbc, 0x50, 0x14, 0xbc, 0x62, 0x3b, 0x3a, 0x27 } };
static const GUID IID_IDataReader_s =
    { 0xe2b50029, 0xb4c1, 0x4314, { 0xa4, 0xb8, 0xfb, 0x81, 0x3a, 0x2f, 0x27, 0x5e } };
static const GUID IID_IDataReaderFactory_s =
    { 0xd7527847, 0x57da, 0x4e15, { 0x91, 0x4c, 0x06, 0x80, 0x66, 0x99, 0xa0, 0x98 } };
static const GUID IID_IDataReaderStatics_s =
    { 0x11fcbefc, 0xfd05, 0x4181, { 0x9f, 0x31, 0x65, 0x6e, 0x75, 0x04, 0x92, 0x1e } };
static const GUID IID_IIterator_Byte_s =
    { 0x40556131, 0xa2a1, 0x5fab, { 0xaa, 0xee, 0x5f, 0x35, 0x26, 0x8c, 0xa2, 0x6b } };

static HRESULT WINAPI buffer_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct mem_buffer *impl = iface;
    SDBG( "buffer_QI %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBuffer_s ))
    {
        *out = &impl->vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IBufferByteAccess_s ))
    {
        *out = &impl->vtbl_byte_access;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI buffer_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct mem_buffer *)iface)->ref );
}
static ULONG WINAPI buffer_Release( void *iface )
{
    struct mem_buffer *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->data );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI buffer_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI buffer_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Storage.Streams.IBuffer";
    if (!name) return E_POINTER;
    return WindowsCreateString( nm, ARRAY_SIZE( nm ) - 1, name );
}
static HRESULT WINAPI buffer_GetTrustLevel( void *iface, int *level )
{
    if (level) *level = 0;
    return S_OK;
}
static HRESULT WINAPI buffer_get_Length( void *iface, ULONG *value )
{
    struct mem_buffer *impl = iface;
    SDBG( "buffer_get_Length %lu\n", impl->len );
    if (!value) return E_POINTER;
    *value = impl->len;
    return S_OK;
}
static HRESULT WINAPI buffer_get_Capacity( void *iface, ULONG *value )
{
    struct mem_buffer *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->len;
    return S_OK;
}

static const void *buffer_iter_vtbl[];

static HRESULT WINAPI biter_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_Byte_s ))
    {
        *out = iface;
        InterlockedIncrement( &((struct buffer_iter *)iface)->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI biter_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct buffer_iter *)iface)->ref );
}
static ULONG WINAPI biter_Release( void *iface )
{
    struct buffer_iter *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        buffer_Release( impl->buf );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI biter_MoveNext( void *iface, boolean *has )
{
    struct buffer_iter *impl = iface;
    if (!has) return E_POINTER;
    *has = (impl->pos < impl->buf->len);
    if (*has) impl->pos++;
    return S_OK;
}
static HRESULT WINAPI biter_get_Current( void *iface, BYTE *value )
{
    struct buffer_iter *impl = iface;
    if (!value) return E_POINTER;
    *value = (impl->pos && impl->pos - 1 < impl->buf->len) ? impl->buf->data[impl->pos - 1] : 0;
    return S_OK;
}
static HRESULT WINAPI biter_HasCurrent( void *iface, boolean *has )
{
    struct buffer_iter *impl = iface;
    if (!has) return E_POINTER;
    *has = (impl->pos > 0 && impl->pos - 1 < impl->buf->len);
    return S_OK;
}
static HRESULT WINAPI buffer_wrpc( void *iface, const BYTE *data, ULONG len )
{
    struct mem_buffer *impl = iface;
    BYTE *nd;
    if (len && !data) return E_POINTER;
    if (!(nd = realloc( impl->data, len ? len : 1 ))) return E_OUTOFMEMORY;
    impl->data = nd;
    if (len) memcpy( impl->data, data, len );
    impl->len = len;
    return S_OK;
}
static HRESULT WINAPI buffer_rdc( void *iface, BYTE *dst, ULONG off, ULONG len )
{
    struct mem_buffer *impl = iface;
    if (!dst) return E_POINTER;
    if (off > impl->len) off = impl->len;
    if (len > impl->len - off) len = impl->len - off;
    memcpy( dst, impl->data + off, len );
    return S_OK;
}
static HRESULT WINAPI buffer_First( void *iface, void **out )
{
    struct mem_buffer *impl = iface;
    SDBG( "buffer_Start len %lu\n", impl->len );
    if (!out) return E_POINTER;
    /* Windows 8.1 IBuffer`1<Byte> slot 8 is the Start property getter
     * (raw BYTE*), not Win10's First(); WPM dereferences it as a pointer. */
    *out = (void *)impl->data;
    return S_OK;
}
static const void *mem_buffer_vtbl[] =
{
    buffer_QueryInterface, buffer_AddRef, buffer_Release,
    buffer_GetIids, buffer_GetRuntimeClassName, buffer_GetTrustLevel,
    buffer_get_Length,  /* 6 */
    buffer_get_Capacity,/* 7 */
    buffer_First,       /* 8 Start (BYTE*) — Win8.1 IBuffer`1<Byte> */
    buffer_wrpc,        /* 9 Wine-internal: write bytes into the buffer */
    buffer_rdc,         /* 10 Wine-internal: read bytes out (dst, offset, len) */
};
static const void *buffer_iter_vtbl[] __attribute__((unused)) =
{
    biter_QueryInterface, biter_AddRef, biter_Release,
    biter_QueryInterface, biter_AddRef, biter_Release, /* IInspectable slots */
    biter_MoveNext,   /* 6 */
    biter_get_Current,/* 7 */
    biter_HasCurrent, /* 8 */
};

static HRESULT WINAPI buf_ba_QI( void *iface, REFIID iid, void **out )
{
    struct mem_buffer *impl = CONTAINING_RECORD( iface, struct mem_buffer, vtbl_byte_access );
    return buffer_QueryInterface( impl, iid, out );
}
static ULONG WINAPI buf_ba_AddRef( void *iface )
{
    struct mem_buffer *impl = CONTAINING_RECORD( iface, struct mem_buffer, vtbl_byte_access );
    return buffer_AddRef( impl );
}
static ULONG WINAPI buf_ba_Release( void *iface )
{
    struct mem_buffer *impl = CONTAINING_RECORD( iface, struct mem_buffer, vtbl_byte_access );
    return buffer_Release( impl );
}
static HRESULT WINAPI buf_ba_Buffer( void *iface, BYTE **value )
{
    struct mem_buffer *impl = CONTAINING_RECORD( iface, struct mem_buffer, vtbl_byte_access );
    if (!value) return E_POINTER;
    *value = impl->data;
    return S_OK;
}
static const void *mem_buffer_ba_vtbl[] =
{
    buf_ba_QI,
    buf_ba_AddRef,
    buf_ba_Release,
    buf_ba_Buffer,
};

static HRESULT create_mem_buffer( const void *data, ULONG len, void **out )
{
    struct mem_buffer *impl = calloc( 1, sizeof(*impl) );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl = mem_buffer_vtbl;
    impl->vtbl_byte_access = mem_buffer_ba_vtbl;
    impl->ref = 1;
    impl->len = len;
    if (len)
    {
        if (!(impl->data = malloc( len )))
        {
            free( impl );
            return E_OUTOFMEMORY;
        }
        if (data) memcpy( impl->data, data, len );
        else memset( impl->data, 0, len );
    }
    *out = impl;
    return S_OK;
}

HRESULT create_mem_buffer_from_hstring( HSTRING str, void **out )
{
    const WCHAR *raw = WindowsGetStringRawBuffer( str, NULL );
    UINT len = WindowsGetStringLen( str );
    int bytes;
    char *utf8;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    bytes = WideCharToMultiByte( CP_UTF8, 0, raw, len, NULL, 0, NULL, NULL );
    if (bytes < 0) bytes = 0;
    if (!(utf8 = malloc( bytes ? bytes : 1 ))) return E_OUTOFMEMORY;
    WideCharToMultiByte( CP_UTF8, 0, raw, len, utf8, bytes, NULL, NULL );
    hr = create_mem_buffer( utf8, bytes, out );
    free( utf8 );
    return hr;
}

HRESULT create_empty_mem_buffer( unsigned long cap, void **out )
{
    return create_mem_buffer( NULL, cap, out );
}

HRESULT create_mem_buffer_from_bytes( const void *data, unsigned long len, void **out )
{
    return create_mem_buffer( data, len, out );
}

/* -------------------------------------------------------------------------- */
/* DataReader: Windows.Storage.Streams.DataReader
 * IDataReaderStatics: 0-5 IInspectable, 6 FromBuffer/ActivateInstance.
 * IDataReader: 6 Buffer, 7 UnconsumedBufferLength, 8 InputStreamOptions,
 * 9 ReadByte, 10 ReadBytes(count, *out), 11 ReadGuid, 12 ReadUInt16,
 * 13 ReadUInt32, 14 ReadUInt64, 15 ReadSingle, 16 ReadDouble, 17 ReadDecimal,
 * 18 ReadUnicodeString(len, HSTRING*), 19 ReadDateTime, 20 ReadTimeSpan. */

struct data_reader
{
    const void *vtbl;
    LONG ref;
    struct mem_buffer *buf;
    ULONG pos;
};

static HRESULT WINAPI reader_QI( void *iface, REFIID iid, void **out )
{
    SDBG( "reader_QI %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataReader_s ))
    {
        *out = iface;
        InterlockedIncrement( &((struct data_reader *)iface)->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI reader_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct data_reader *)iface)->ref );
}
static ULONG WINAPI reader_Release( void *iface )
{
    struct data_reader *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->buf) buffer_Release( impl->buf );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI reader_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI reader_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Storage.Streams.DataReader";
    if (!name) return E_POINTER;
    return WindowsCreateString( nm, ARRAY_SIZE( nm ) - 1, name );
}
static HRESULT WINAPI reader_GetTrustLevel( void *iface, int *level )
{
    if (level) *level = 0;
    return S_OK;
}
static HRESULT WINAPI reader_get_Buffer( void *iface, void **out )
{
    struct data_reader *impl = iface;
    if (!out) return E_POINTER;
    *out = impl->buf;
    if (impl->buf) buffer_AddRef( impl->buf );
    return S_OK;
}
static HRESULT WINAPI reader_get_UnconsumedBufferLength( void *iface, ULONG *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->buf && impl->pos < impl->buf->len ? impl->buf->len - impl->pos : 0;
    return S_OK;
}
static HRESULT WINAPI reader_get_InputStreamOptions( void *iface, int *value )
{
    if (!value) return E_POINTER;
    *value = 0; /* All */
    return S_OK;
}
static HRESULT WINAPI reader_ReadByte( void *iface, BYTE *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos >= impl->buf->len) return 0x8000000c /* E_BOUNDS */;
    *value = impl->buf->data[impl->pos++];
    return S_OK;
}
static HRESULT WINAPI reader_ReadBytes( void *iface, ULONG count, BYTE *value )
{
    struct data_reader *impl = iface;
    SDBG( "reader_ReadBytes count %lu pos %lu len %lu\n", count, impl->pos,
           impl->buf ? impl->buf->len : 0 );
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + count > impl->buf->len) return 0x8000000c /* E_BOUNDS */;
    memcpy( value, impl->buf->data + impl->pos, count );
    impl->pos += count;
    return S_OK;
}
static HRESULT WINAPI reader_ReadGuid( void *iface, GUID *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 16 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 16 );
    impl->pos += 16;
    return S_OK;
}
static HRESULT WINAPI reader_ReadUInt16( void *iface, WORD *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 2 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 2 );
    impl->pos += 2;
    return S_OK;
}
static HRESULT WINAPI reader_ReadUInt32( void *iface, ULONG *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 4 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 4 );
    impl->pos += 4;
    return S_OK;
}
static HRESULT WINAPI reader_ReadUInt64( void *iface, UINT64 *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 8 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 8 );
    impl->pos += 8;
    return S_OK;
}
static HRESULT WINAPI reader_ReadSingle( void *iface, float *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 4 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 4 );
    impl->pos += 4;
    return S_OK;
}
static HRESULT WINAPI reader_ReadDouble( void *iface, double *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 8 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 8 );
    impl->pos += 8;
    return S_OK;
}
static HRESULT WINAPI reader_ReadDecimal( void *iface, void *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 16 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 16 );
    impl->pos += 16;
    return S_OK;
}
static HRESULT WINAPI reader_ReadUnicodeString( void *iface, ULONG length, HSTRING *value )
{
    struct data_reader *impl = iface;
    const WCHAR *w;
    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->buf || impl->pos + length * 2 > impl->buf->len) return 0x8000000c;
    w = (const WCHAR *)(impl->buf->data + impl->pos);
    impl->pos += length * 2;
    return WindowsCreateString( w, length, value );
}
static HRESULT WINAPI reader_ReadDateTime( void *iface, INT64 *value )
{
    struct data_reader *impl = iface;
    if (!value) return E_POINTER;
    if (!impl->buf || impl->pos + 8 > impl->buf->len) return 0x8000000c;
    memcpy( value, impl->buf->data + impl->pos, 8 );
    impl->pos += 8;
    return S_OK;
}
static HRESULT WINAPI reader_ReadTimeSpan( void *iface, INT64 *value )
{
    return reader_ReadDateTime( iface, value );
}
static const void *data_reader_vtbl[] =
{
    reader_QI, reader_AddRef, reader_Release,
    reader_GetIids, reader_GetRuntimeClassName, reader_GetTrustLevel,
    reader_get_Buffer,               /* 6  */
    reader_get_UnconsumedBufferLength,/* 7  */
    reader_get_InputStreamOptions,   /* 8  */
    reader_ReadByte,                 /* 9  */
    reader_ReadBytes,                /* 10 */
    reader_ReadGuid,                 /* 11 */
    reader_ReadUInt16,               /* 12 */
    reader_ReadUInt32,               /* 13 */
    reader_ReadUInt64,               /* 14 */
    reader_ReadSingle,               /* 15 */
    reader_ReadDouble,               /* 16 */
    reader_ReadDecimal,              /* 17 */
    reader_ReadUnicodeString,        /* 18 */
    reader_ReadDateTime,             /* 19 */
    reader_ReadTimeSpan,             /* 20 */
};

/* Factory face doubles as IDataReaderStatics: slot 6 is ActivateInstance on
 * IActivationFactory (in: IInspectable**) and FromBuffer on IDataReaderStatics
 * (in: IBuffer*, out: IDataReader**). Dispatch on argument count by inspecting
 * the pointer: C++/WinRT calls FromBuffer(statics, buffer, &reader),
 * ActivateInstance(factory, &instance). */
struct reader_factory
{
    const void *vtbl;
    LONG ref;
};

static HRESULT WINAPI factory_QI( void *iface, REFIID iid, void **out )
{
    SDBG( "reader_factory_QI %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IDataReaderFactory_s ) ||
        IsEqualGUID( iid, &IID_IDataReaderStatics_s ))
    {
        *out = iface;
        InterlockedIncrement( &((struct reader_factory *)iface)->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI factory_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct reader_factory *)iface)->ref );
}
static ULONG WINAPI factory_Release( void *iface )
{
    struct reader_factory *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI factory_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Storage.Streams.DataReader";
    if (!name) return E_POINTER;
    return WindowsCreateString( nm, ARRAY_SIZE( nm ) - 1, name );
}
static HRESULT WINAPI factory_GetTrustLevel( void *iface, int *level )
{
    if (level) *level = 0;
    return S_OK;
}
static HRESULT WINAPI factory_ActivateInstance( void *iface, void *arg1, void *arg2 )
{
    struct data_reader *impl = calloc( 1, sizeof(*impl) );
    void **out;
    struct mem_buffer *buf = arg1;
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl = data_reader_vtbl;
    impl->ref = 1;
    if (arg2 && arg1 && buf->vtbl == mem_buffer_vtbl)
    {
        /* FromBuffer(buffer, &reader) */
        impl->buf = buf;
        buffer_AddRef( buf );
        out = arg2;
        SDBG( "reader FromBuffer len %lu\n", buf->len );
    }
    else
    {
        /* ActivateInstance(&instance) */
        out = arg1;
    }
    *out = impl;
    return S_OK;
}
static const void *reader_factory_vtbl[] =
{
    factory_QI, factory_AddRef, factory_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel,
    factory_ActivateInstance, /* 6 ActivateInstance / IDataReaderStatics::FromBuffer */
};

void *create_data_reader_factory( void )
{
    struct reader_factory *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = reader_factory_vtbl;
    impl->ref = 1;
    return impl;
}

/* -------------------------------------------------------------------------- */
/* IRandomAccessStream: in-memory random access stream over a mem_buffer.
 * Consumed by shcore!CreateStreamOverRandomAccessStream (WindowsPackageManager
 * reads the msix payload through the resulting IStream). Wine-internal contract
 * between windows.web (implementation) and shcore (caller), Win8 IRandomAccessStream
 * slot layout plus IInputStream extras at 15+.
 *   0-5  IInspectable
 *   6    get_Size(UINT64*)        7 put_Size(UINT64)
 *   8    GetInputStreamAt(UINT64, IInputStream**)  9 GetOutputStreamAt(...)
 *   10   get_Position(UINT64*)    11 Seek(UINT64)
 *   12   CloneStream(IRandomAccessStream**)        13 get_CanRead(boolean*)
 *   14   get_CanWrite(boolean*)
 *   15   ReadAsync(IBuffer*, UINT32, InputStreamOptions, IAsyncOperationWithProgress**)
 *   16   DrainAsync(UINT32, op**) 17 Close() */

struct ras_stream
{
    const void *vtbl;
    const void *vtbl_input;
    const void *vtbl_closable;
    LONG ref;
    struct mem_buffer *buf;
    ULONG pos;
};

static const void *ras_input_vtbl[];
static const void *ras_closable_vtbl[];

static HRESULT WINAPI ras_QI( void *iface, REFIID iid, void **out )
{
    struct ras_stream *impl = iface;
    SDBG( "ras_QI %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStream_s ))
    {
        *out = &impl->vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IInputStream_s ))
    {
        *out = &impl->vtbl_input;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ras_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct ras_stream *)iface)->ref );
}
static ULONG WINAPI ras_Release( void *iface )
{
    struct ras_stream *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        buffer_Release( impl->buf );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI ras_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0; *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI ras_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Storage.Streams.InMemoryRandomAccessStream";
    if (!name) return E_POINTER;
    return WindowsCreateString( nm, ARRAY_SIZE( nm ) - 1, name );
}
static HRESULT WINAPI ras_GetTrustLevel( void *iface, int *level )
{
    if (level) *level = 0;
    return S_OK;
}
static HRESULT WINAPI ras_get_Size( void *iface, UINT64 *value )
{
    struct ras_stream *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->buf->len;
    return S_OK;
}
static HRESULT WINAPI ras_put_Size( void *iface, UINT64 value )
{
    (void)value;
    return S_OK;
}
static HRESULT WINAPI ras_GetInputStreamAt( void *iface, UINT64 pos, void **out )
{
    struct ras_stream *impl = iface;
    struct ras_stream *clone;

    if (!out) return E_POINTER;
    /* Windows semantics: each GetInputStreamAt returns an independent stream
     * over the shared content, positioned at `pos`; reads on it advance its
     * own position (WPM pumps the body in chunks via GetInputStreamAt+ReadAsync). */
    if (!(clone = calloc( 1, sizeof(*clone) ))) return E_OUTOFMEMORY;
    clone->vtbl = impl->vtbl;
    clone->vtbl_input = ras_input_vtbl;
    clone->vtbl_closable = ras_closable_vtbl;
    clone->ref = 1;
    clone->buf = impl->buf;
    clone->pos = (ULONG)pos;
    buffer_AddRef( impl->buf );
    *out = clone;
    return S_OK;
}
static HRESULT WINAPI ras_GetOutputStreamAt( void *iface, UINT64 pos, void **out )
{
    return ras_GetInputStreamAt( iface, pos, out );
}
static HRESULT WINAPI ras_get_Position( void *iface, UINT64 *value )
{
    struct ras_stream *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->pos;
    return S_OK;
}
static HRESULT WINAPI ras_Seek( void *iface, UINT64 pos )
{
    struct ras_stream *impl = iface;
    impl->pos = (ULONG)pos;
    return S_OK;
}
static HRESULT WINAPI ras_CloneStream( void *iface, void **out )
{
    struct ras_stream *impl = iface;
    struct ras_stream *clone = calloc( 1, sizeof(*clone) );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!clone) return E_OUTOFMEMORY;
    clone->vtbl = impl->vtbl;
    clone->ref = 1;
    clone->buf = impl->buf;
    buffer_AddRef( impl->buf );
    *out = clone;
    return S_OK;
}
static HRESULT WINAPI ras_get_CanRead( void *iface, boolean *value )
{
    (void)iface;
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}
static HRESULT WINAPI ras_get_CanWrite( void *iface, boolean *value )
{
    (void)iface;
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}
/* Win11 projection slot 30: ReadAsync(IBuffer*, IAsyncOperationWithProgress**)
 * — count comes from the destination buffer's Capacity. */
static const GUID IID_IBufferByteAccess_local =
    { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };

static HRESULT WINAPI ras_ReadAsync_impl( struct ras_stream *impl, void *buffer, UINT32 count, void **op )
{
    HRESULT (WINAPI *cap)( void *, UINT32 * );
    HRESULT (WINAPI *put_len)( void *, UINT32 );
    void *ba = NULL;
    HRESULT hr;
    UINT32 n = 0;

    if (!buffer || !op) return E_POINTER;
    *op = NULL;
    /* IBuffer: slot 6 = get_Capacity, slot 8 = put_Length */
    cap = (HRESULT (WINAPI *)( void *, UINT32 * ))
          ((void *const *)(*(void *const **)buffer))[6];
    cap( buffer, &n );
    if (count && count < n) n = count;
    if (impl->pos > impl->buf->len) impl->pos = impl->buf->len;
    if (n > impl->buf->len - impl->pos) n = impl->buf->len - impl->pos;
    if (SUCCEEDED( IUnknown_QueryInterface( (IUnknown *)buffer, &IID_IBufferByteAccess_local, &ba ) ) && ba)
    {
        BYTE *ptr = NULL;
        HRESULT (WINAPI *get_buf)( void *, BYTE ** ) = (void *)((void *const *)(*(void *const **)ba))[3];
        hr = get_buf( ba, &ptr );
        IUnknown_Release( (IUnknown *)ba );
        if (FAILED( hr ) || (n && !ptr)) return FAILED( hr ) ? hr : E_FAIL;
        if (n) memcpy( ptr, impl->buf->data + impl->pos, n );
        put_len = (HRESULT (WINAPI *)( void *, UINT32 ))
                  ((void *const *)(*(void *const **)buffer))[8];
        hr = put_len( buffer, n );
        if (FAILED( hr )) return hr;
    }
    else if (n)
    {
        HRESULT (WINAPI *wrpc)( void *, const BYTE *, ULONG );
        BYTE *tmp = malloc( n );
        if (!tmp) return E_OUTOFMEMORY;
        memcpy( tmp, impl->buf->data + impl->pos, n );
        wrpc = (HRESULT (WINAPI *)( void *, const BYTE *, ULONG ))
               ((void *const *)(*(void *const **)buffer))[9];
        hr = wrpc( buffer, tmp, n );
        free( tmp );
        if (FAILED( hr )) return hr;
    }
    impl->pos += n;
    SDBG( "ras_ReadAsync n %u pos %lu\n", n, impl->pos );
    return create_async_http_op_obj( buffer, op );
}

static HRESULT WINAPI ras_ReadAsync( void *iface, void *buffer, void **op )
{
    return ras_ReadAsync_impl( (struct ras_stream *)iface, buffer, 0, op );
}

static HRESULT WINAPI ras_InputStream_ReadAsync( void *iface, void *buffer, UINT32 count, UINT32 options, void **op )
{
    (void)options;
    return ras_ReadAsync_impl( (struct ras_stream *)iface, buffer, count, op );
}
/* Windows 11 positioned projection read: ReadAsync(IBuffer*, UINT64 position,
 * UINT32 count) — WindowsPackageManager's Win11-ABI body writer pumps chunks
 * through this slot while keeping its own reader cursor. */
/* Win11 projection slot 21: ReadAsync(IAsyncOperationWithProgress**&op,
 * IBuffer* dest, UINT32 count) — sequential continuation of slot 30. */
static HRESULT WINAPI ras_ReadAsyncAt( void *iface, void **op, void *buffer, ULONG count )
{
    struct ras_stream *impl = iface;
    (void)count; /* destination Capacity bounds the read; count is the WPM chunk size */
    SDBG( "ras_ReadAsyncAt count %lu pos %lu\n", count, impl->pos );
    return ras_ReadAsync( iface, buffer, op );
}
/* Win11 projection GetResults on the stream face shares the async-op object
 * layout (result_obj at +0x10): write the IBuffer back for buffer results,
 * the byte count for UINT32 results. */
static HRESULT WINAPI ras_op_GetResults( void *iface, void *results )
{
    void **obj = results;
    if (!results) return E_POINTER;
    if (*(void **)((BYTE *)iface + 0x10))
    {
        IInspectable_AddRef( (IInspectable *)*(void **)((BYTE *)iface + 0x10) );
        *obj = *(void **)((BYTE *)iface + 0x10);
    }
    else *obj = NULL;
    return S_OK;
}
static HRESULT WINAPI ras_op_GetResults2( void *iface, void *results )
{
    ULONG *out = results;
    void *obj;
    if (!results) return E_POINTER;
    obj = *(void **)((BYTE *)iface + 0x10);
    if (!obj) { *out = 0; return S_OK; }
    /* IBuffer result: report its Length as the byte count */
    return ((HRESULT (WINAPI *)( void *, ULONG * ))((void *const *)(*(void *const **)obj))[6])( obj, out );
}
static HRESULT WINAPI ras_DrainAsync( void *iface, ULONG count, void **op )
{
    struct ras_stream *impl = iface;
    (void)count;
    if (!op) return E_POINTER;
    impl->pos = impl->buf->len;
    return create_async_http_op_obj( NULL, op );
}
static HRESULT WINAPI ras_Close( void *iface )
{
    (void)iface;
    return S_OK;
}
static const void *ras_stream_vtbl[] =
{
    ras_QI, ras_AddRef, ras_Release,
    ras_GetIids, ras_GetRuntimeClassName, ras_GetTrustLevel,
    ras_get_Size,           /* 6 */
    ras_put_Size,           /* 7 */
    ras_GetInputStreamAt,   /* 8 */
    ras_GetOutputStreamAt,  /* 9 */
    ras_get_Position,       /* 10 */
    ras_Seek,               /* 11 */
    ras_CloneStream,        /* 12 */
    ras_get_CanRead,        /* 13 */
    ras_get_CanWrite,       /* 14 */
    ras_InputStream_ReadAsync, /* 15 IInputStream::ReadAsync(buffer, count, options, op) */
    ras_DrainAsync,         /* 16 Wine-internal */
    ras_Close,              /* 17 Wine-internal (IClosable::Close) */
    ras_QI,                 /* 18 */ ras_QI,
    ras_QI,                 /* 20 */ ras_QI,                 /* 21 */ ras_QI,
    ras_QI,                 /* 22 */ ras_QI,
    ras_QI,                 /* 24 */ ras_QI,
    ras_QI,                 /* 26 */ ras_QI,                 /* 28 */ ras_QI,
    ras_ReadAsync,          /* 30 Win11 IRandomAccessStream::ReadAsync projection slot */
    ras_ReadAsyncAt,        /* 31 Win11 positioned ReadAsync (op**, buffer, count) */
    ras_QI,                 /* 32 stream-face async twin below (op carries results) */
    ras_op_GetResults,      /* 33 Win11 projection GetResults (IBuffer*) */
    ras_op_GetResults2,     /* 34 Win11 projection GetResults (UINT32 count) */
    ras_op_GetResults,      /* 35 */
    ras_op_GetResults2,     /* 36 */
};

static HRESULT WINAPI ras_in_QI( void *iface, REFIID iid, void **out )
{
    struct ras_stream *impl = (struct ras_stream *)((char *)iface - offsetof( struct ras_stream, vtbl_input ));
    return ras_QI( impl, iid, out );
}
static ULONG WINAPI ras_in_AddRef( void *iface )
{
    struct ras_stream *impl = (struct ras_stream *)((char *)iface - offsetof( struct ras_stream, vtbl_input ));
    return ras_AddRef( impl );
}
static ULONG WINAPI ras_in_Release( void *iface )
{
    struct ras_stream *impl = (struct ras_stream *)((char *)iface - offsetof( struct ras_stream, vtbl_input ));
    return ras_Release( impl );
}
static HRESULT WINAPI ras_in_ReadAsync( void *iface, void *buffer, UINT32 count, UINT32 options, void **op )
{
    struct ras_stream *impl = (struct ras_stream *)((char *)iface - offsetof( struct ras_stream, vtbl_input ));
    (void)options;
    return ras_ReadAsync_impl( impl, buffer, count, op );
}
static const void *ras_input_vtbl[] =
{
    ras_in_QI, ras_in_AddRef, ras_in_Release,
    ras_GetIids, ras_GetRuntimeClassName, ras_GetTrustLevel,
    ras_in_ReadAsync,
};

static HRESULT WINAPI ras_cl_QI( void *iface, REFIID iid, void **out )
{
    struct ras_stream *impl = (struct ras_stream *)((char *)iface - offsetof( struct ras_stream, vtbl_closable ));
    return ras_QI( impl, iid, out );
}
static ULONG WINAPI ras_cl_AddRef( void *iface )
{
    struct ras_stream *impl = (struct ras_stream *)((char *)iface - offsetof( struct ras_stream, vtbl_closable ));
    return ras_AddRef( impl );
}
static ULONG WINAPI ras_cl_Release( void *iface )
{
    struct ras_stream *impl = (struct ras_stream *)((char *)iface - offsetof( struct ras_stream, vtbl_closable ));
    return ras_Release( impl );
}
static const void *ras_closable_vtbl[] =
{
    ras_cl_QI, ras_cl_AddRef, ras_cl_Release,
    ras_GetIids, ras_GetRuntimeClassName, ras_GetTrustLevel,
    ras_Close,
};

HRESULT create_ras_stream_from_buffer( void *buffer, void **out )
{
    struct ras_stream *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl = ras_stream_vtbl;
    impl->vtbl_input = ras_input_vtbl;
    impl->vtbl_closable = ras_closable_vtbl;
    impl->ref = 1;
    impl->buf = (struct mem_buffer *)buffer;
    buffer_AddRef( impl->buf );
    *out = impl;
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* Windows.Storage.Streams.Buffer activation factory: slot 6 Create(capacity). */

struct buffer_factory
{
    const void *vtbl;
    LONG ref;
};

static HRESULT WINAPI bfac_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IBufferFactory_s ))
    {
        *out = iface;
        InterlockedIncrement( &((struct buffer_factory *)iface)->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI bfac_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct buffer_factory *)iface)->ref );
}
static ULONG WINAPI bfac_Release( void *iface )
{
    struct buffer_factory *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI bfac_Create( void *iface, ULONG capacity, void **out )
{
    struct mem_buffer *impl;
    (void)iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl = mem_buffer_vtbl;
    impl->ref = 1;
    if (capacity && !(impl->data = malloc( capacity )))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }
    impl->len = 0;
    *out = impl;
    return S_OK;
}
static const void *buffer_factory_vtbl[] =
{
    bfac_QI, bfac_AddRef, bfac_Release,
    bfac_QI, bfac_QI, bfac_QI, /* IInspectable stubs */
    bfac_Create, /* 6 IBufferFactory::Create */
};

void *create_buffer_factory( void )
{
    struct buffer_factory *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->vtbl = buffer_factory_vtbl;
    impl->ref = 1;
    return impl;
}
