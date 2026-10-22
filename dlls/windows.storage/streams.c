/* WinRT Windows.Storage.Streams Implementation
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

/* -------------------------------------------------------------------------- */
/* Buffer Implementation                                                      */
/* -------------------------------------------------------------------------- */

struct buffer
{
    IBuffer IBuffer_iface;
    IBufferByteAccess IBufferByteAccess_iface;
    LONG ref;

    UINT32 capacity;
    UINT32 length;
    BYTE *data;
};

static inline struct buffer *impl_from_IBuffer( IBuffer *iface )
{
    return CONTAINING_RECORD( iface, struct buffer, IBuffer_iface );
}

static inline struct buffer *impl_from_IBufferByteAccess( IBufferByteAccess *iface )
{
    return CONTAINING_RECORD( iface, struct buffer, IBufferByteAccess_iface );
}

static HRESULT WINAPI buffer_QueryInterface( IBuffer *iface, REFIID iid, void **out )
{
    struct buffer *impl = impl_from_IBuffer( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IBuffer ))
    {
        *out = &impl->IBuffer_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IBufferByteAccess ))
    {
        *out = &impl->IBufferByteAccess_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI buffer_AddRef( IBuffer *iface )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI buffer_Release( IBuffer *iface )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        free( impl->data );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI buffer_GetIids( IBuffer *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI buffer_GetRuntimeClassName( IBuffer *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.Buffer", 29, class_name );
}

static HRESULT WINAPI buffer_GetTrustLevel( IBuffer *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI buffer_get_Capacity( IBuffer *iface, UINT32 *value )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    if (!value) return E_POINTER;
    *value = impl->capacity;
    return S_OK;
}

static HRESULT WINAPI buffer_get_Length( IBuffer *iface, UINT32 *value )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    if (!value) return E_POINTER;
    *value = impl->length;
    return S_OK;
}

static HRESULT WINAPI buffer_put_Length( IBuffer *iface, UINT32 value )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    if (value > impl->capacity) return E_INVALIDARG;
    impl->length = value;
    return S_OK;
}

static const struct IBufferVtbl buffer_vtbl =
{
    buffer_QueryInterface,
    buffer_AddRef,
    buffer_Release,
    buffer_GetIids,
    buffer_GetRuntimeClassName,
    buffer_GetTrustLevel,
    buffer_get_Capacity,
    buffer_get_Length,
    buffer_put_Length,
};

static HRESULT WINAPI buffer_byte_access_QueryInterface( IBufferByteAccess *iface, REFIID iid, void **out )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_QueryInterface( &impl->IBuffer_iface, iid, out );
}

static ULONG WINAPI buffer_byte_access_AddRef( IBufferByteAccess *iface )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_AddRef( &impl->IBuffer_iface );
}

static ULONG WINAPI buffer_byte_access_Release( IBufferByteAccess *iface )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );
    return IBuffer_Release( &impl->IBuffer_iface );
}

static HRESULT WINAPI buffer_byte_access_Buffer( IBufferByteAccess *iface, byte **value )
{
    struct buffer *impl = impl_from_IBufferByteAccess( iface );
    if (!value) return E_POINTER;
    *value = impl->data;
    return S_OK;
}

static const struct IBufferByteAccessVtbl buffer_byte_access_vtbl =
{
    buffer_byte_access_QueryInterface,
    buffer_byte_access_AddRef,
    buffer_byte_access_Release,
    buffer_byte_access_Buffer,
};

HRESULT buffer_create( UINT32 capacity, IBuffer **out )
{
    struct buffer *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IBuffer_iface.lpVtbl = &buffer_vtbl;
    impl->IBufferByteAccess_iface.lpVtbl = &buffer_byte_access_vtbl;
    impl->ref = 1;
    impl->capacity = capacity;
    impl->length = 0;

    if (capacity)
    {
        if (!(impl->data = calloc( 1, capacity )))
        {
            free( impl );
            return E_OUTOFMEMORY;
        }
    }

    *out = &impl->IBuffer_iface;
    return S_OK;
}

/* Buffer Factory & Statics */
struct buffer_statics
{
    IActivationFactory IActivationFactory_iface;
    IBufferFactory IBufferFactory_iface;
    IBufferStatics IBufferStatics_iface;
    LONG ref;
};

static inline struct buffer_statics *impl_from_IActivationFactory_Buffer( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct buffer_statics, IActivationFactory_iface );
}

static HRESULT WINAPI buffer_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct buffer_statics *impl = impl_from_IActivationFactory_Buffer( iface );

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

    if (IsEqualGUID( iid, &IID_IBufferFactory ))
    {
        *out = &impl->IBufferFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IBufferStatics ))
    {
        *out = &impl->IBufferStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI buffer_factory_AddRef( IActivationFactory *iface )
{
    struct buffer_statics *impl = impl_from_IActivationFactory_Buffer( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI buffer_factory_Release( IActivationFactory *iface )
{
    struct buffer_statics *impl = impl_from_IActivationFactory_Buffer( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI buffer_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI buffer_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.Buffer", 29, class_name );
}

static HRESULT WINAPI buffer_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI buffer_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl buffer_factory_vtbl =
{
    buffer_factory_QueryInterface,
    buffer_factory_AddRef,
    buffer_factory_Release,
    buffer_factory_GetIids,
    buffer_factory_GetRuntimeClassName,
    buffer_factory_GetTrustLevel,
    buffer_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( buffer_factory_methods, IBufferFactory, struct buffer_statics, IActivationFactory_iface )

static HRESULT WINAPI buffer_factory_methods_Create( IBufferFactory *iface, UINT32 capacity, IBuffer **value )
{
    return buffer_create( capacity, value );
}

static const struct IBufferFactoryVtbl buffer_factory_methods_vtbl =
{
    buffer_factory_methods_QueryInterface,
    buffer_factory_methods_AddRef,
    buffer_factory_methods_Release,
    buffer_factory_methods_GetIids,
    buffer_factory_methods_GetRuntimeClassName,
    buffer_factory_methods_GetTrustLevel,
    buffer_factory_methods_Create,
};

DEFINE_IINSPECTABLE( buffer_statics_methods, IBufferStatics, struct buffer_statics, IActivationFactory_iface )

static HRESULT WINAPI buffer_statics_methods_CreateCopyFromMemoryBuffer( IBufferStatics *iface, IMemoryBuffer *input,
                                                                        IBuffer **value )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static HRESULT WINAPI buffer_statics_methods_CreateMemoryBufferOverIBuffer( IBufferStatics *iface, IBuffer *input,
                                                                          IMemoryBuffer **value )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IBufferStaticsVtbl buffer_statics_methods_vtbl =
{
    buffer_statics_methods_QueryInterface,
    buffer_statics_methods_AddRef,
    buffer_statics_methods_Release,
    buffer_statics_methods_GetIids,
    buffer_statics_methods_GetRuntimeClassName,
    buffer_statics_methods_GetTrustLevel,
    buffer_statics_methods_CreateCopyFromMemoryBuffer,
    buffer_statics_methods_CreateMemoryBufferOverIBuffer,
};

static struct buffer_statics buffer_statics_obj =
{
    {&buffer_factory_vtbl},
    {&buffer_factory_methods_vtbl},
    {&buffer_statics_methods_vtbl},
    1,
};

IActivationFactory *buffer_factory = &buffer_statics_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* InMemoryRandomAccessStream Implementation                                  */
/* -------------------------------------------------------------------------- */

struct in_memory_stream
{
    IRandomAccessStream IRandomAccessStream_iface;
    IInputStream IInputStream_iface;
    IOutputStream IOutputStream_iface;
    IContentTypeProvider IContentTypeProvider_iface;
    IClosable IClosable_iface;
    LONG ref;

    CRITICAL_SECTION cs;
    BYTE *buffer;
    UINT64 size;
    UINT64 capacity;
    UINT64 position;
    BOOL closed;
};

static inline struct in_memory_stream *impl_from_IRandomAccessStream( IRandomAccessStream *iface )
{
    return CONTAINING_RECORD( iface, struct in_memory_stream, IRandomAccessStream_iface );
}

static HRESULT WINAPI in_memory_stream_QueryInterface( IRandomAccessStream *iface, REFIID iid, void **out )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStream ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStreamWithContentType ))
    {
        *out = &impl->IRandomAccessStream_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IInputStream ))
    {
        *out = &impl->IInputStream_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IOutputStream ))
    {
        *out = &impl->IOutputStream_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IContentTypeProvider ))
    {
        *out = &impl->IContentTypeProvider_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI in_memory_stream_AddRef( IRandomAccessStream *iface )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI in_memory_stream_Release( IRandomAccessStream *iface )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        DeleteCriticalSection( &impl->cs );
        free( impl->buffer );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI in_memory_stream_GetIids( IRandomAccessStream *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_GetRuntimeClassName( IRandomAccessStream *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.InMemoryRandomAccessStream", 49, class_name );
}

static HRESULT WINAPI in_memory_stream_GetTrustLevel( IRandomAccessStream *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_get_Size( IRandomAccessStream *iface, UINT64 *value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *value = impl->size;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_put_Size( IRandomAccessStream *iface, UINT64 value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );

    EnterCriticalSection( &impl->cs );
    if (value > impl->capacity)
    {
        BYTE *new_buf = realloc( impl->buffer, value );
        if (!new_buf)
        {
            LeaveCriticalSection( &impl->cs );
            return E_OUTOFMEMORY;
        }
        memset( new_buf + impl->capacity, 0, value - impl->capacity );
        impl->buffer = new_buf;
        impl->capacity = value;
    }
    impl->size = value;
    if (impl->position > impl->size)
        impl->position = impl->size;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_GetInputStreamAt( IRandomAccessStream *iface, UINT64 position,
                                                         IInputStream **stream )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );

    if (!stream) return E_POINTER;
    *stream = &impl->IInputStream_iface;
    IInputStream_AddRef( *stream );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_GetOutputStreamAt( IRandomAccessStream *iface, UINT64 position,
                                                          IOutputStream **stream )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );

    if (!stream) return E_POINTER;
    *stream = &impl->IOutputStream_iface;
    IOutputStream_AddRef( *stream );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_get_Position( IRandomAccessStream *iface, UINT64 *value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *value = impl->position;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_Seek( IRandomAccessStream *iface, UINT64 position )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    EnterCriticalSection( &impl->cs );
    impl->position = (position <= impl->size) ? position : impl->size;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_CloneStream( IRandomAccessStream *iface, IRandomAccessStream **stream )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    struct in_memory_stream *clone;

    if (!stream) return E_POINTER;
    if (FAILED(in_memory_stream_create( stream )))
        return E_OUTOFMEMORY;

    clone = impl_from_IRandomAccessStream( *stream );
    EnterCriticalSection( &impl->cs );
    if (impl->size)
    {
        clone->buffer = malloc( impl->size );
        if (clone->buffer)
        {
            memcpy( clone->buffer, impl->buffer, impl->size );
            clone->size = clone->capacity = impl->size;
        }
    }
    clone->position = impl->position;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_get_CanRead( IRandomAccessStream *iface, boolean *value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    if (!value) return E_POINTER;
    *value = !impl->closed;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_get_CanWrite( IRandomAccessStream *iface, boolean *value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    if (!value) return E_POINTER;
    *value = !impl->closed;
    return S_OK;
}

static const struct IRandomAccessStreamVtbl in_memory_stream_vtbl =
{
    in_memory_stream_QueryInterface,
    in_memory_stream_AddRef,
    in_memory_stream_Release,
    in_memory_stream_GetIids,
    in_memory_stream_GetRuntimeClassName,
    in_memory_stream_GetTrustLevel,
    in_memory_stream_get_Size,
    in_memory_stream_put_Size,
    in_memory_stream_GetInputStreamAt,
    in_memory_stream_GetOutputStreamAt,
    in_memory_stream_get_Position,
    in_memory_stream_Seek,
    in_memory_stream_CloneStream,
    in_memory_stream_get_CanRead,
    in_memory_stream_get_CanWrite,
};

/* IInputStream for InMemoryRandomAccessStream */
DEFINE_IINSPECTABLE( in_memory_stream_input, IInputStream, struct in_memory_stream, IRandomAccessStream_iface )

static HRESULT WINAPI in_memory_stream_input_ReadAsync( IInputStream *iface, IBuffer *buffer, UINT32 count,
                                                        InputStreamOptions options,
                                                        IAsyncOperationWithProgress_IBuffer_UINT32 **operation )
{
    struct in_memory_stream *impl = impl_from_IInputStream( iface );
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    UINT32 to_read = 0;
    HRESULT hr;

    if (!buffer || !operation) return E_POINTER;

    EnterCriticalSection( &impl->cs );
    if (impl->position < impl->size)
    {
        to_read = min( count, (UINT32)(impl->size - impl->position) );
        if (SUCCEEDED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        {
            if (SUCCEEDED(IBufferByteAccess_Buffer( access, &dest )) && dest && to_read)
                memcpy( dest, impl->buffer + impl->position, to_read );
            IBufferByteAccess_Release( access );
        }
        impl->position += to_read;
    }
    IBuffer_put_Length( buffer, to_read );
    LeaveCriticalSection( &impl->cs );

    return async_operation_progress_buffer_create( buffer, operation );
}

static const struct IInputStreamVtbl in_memory_stream_input_vtbl =
{
    in_memory_stream_input_QueryInterface,
    in_memory_stream_input_AddRef,
    in_memory_stream_input_Release,
    in_memory_stream_input_GetIids,
    in_memory_stream_input_GetRuntimeClassName,
    in_memory_stream_input_GetTrustLevel,
    in_memory_stream_input_ReadAsync,
};

/* IOutputStream for InMemoryRandomAccessStream */
DEFINE_IINSPECTABLE( in_memory_stream_output, IOutputStream, struct in_memory_stream, IRandomAccessStream_iface )

static HRESULT WINAPI in_memory_stream_output_WriteAsync( IOutputStream *iface, IBuffer *buffer,
                                                          IAsyncOperationWithProgress_UINT32_UINT32 **operation )
{
    struct in_memory_stream *impl = impl_from_IOutputStream( iface );
    IBufferByteAccess *access = NULL;
    BYTE *src = NULL;
    UINT32 len = 0;
    HRESULT hr;

    if (!buffer || !operation) return E_POINTER;

    IBuffer_get_Length( buffer, &len );
    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        return hr;

    hr = IBufferByteAccess_Buffer( access, &src );
    IBufferByteAccess_Release( access );
    if (FAILED(hr)) return hr;

    EnterCriticalSection( &impl->cs );
    if (len)
    {
        UINT64 needed = impl->position + len;
        if (needed > impl->capacity)
        {
            UINT64 new_cap = max( impl->capacity * 2, needed + 1024 );
            BYTE *new_buf = realloc( impl->buffer, new_cap );
            if (!new_buf)
            {
                LeaveCriticalSection( &impl->cs );
                return E_OUTOFMEMORY;
            }
            impl->buffer = new_buf;
            impl->capacity = new_cap;
        }
        memcpy( impl->buffer + impl->position, src, len );
        impl->position += len;
        if (impl->position > impl->size)
            impl->size = impl->position;
    }
    LeaveCriticalSection( &impl->cs );

    return async_operation_progress_uint32_create( len, operation );
}

static HRESULT WINAPI in_memory_stream_output_FlushAsync( IOutputStream *iface, IAsyncOperation_boolean **operation )
{
    return async_operation_boolean_create( L"Windows.Foundation.IAsyncOperation`1<Boolean>", TRUE, operation );
}

static const struct IOutputStreamVtbl in_memory_stream_output_vtbl =
{
    in_memory_stream_output_QueryInterface,
    in_memory_stream_output_AddRef,
    in_memory_stream_output_Release,
    in_memory_stream_output_GetIids,
    in_memory_stream_output_GetRuntimeClassName,
    in_memory_stream_output_GetTrustLevel,
    in_memory_stream_output_WriteAsync,
    in_memory_stream_output_FlushAsync,
};

/* IContentTypeProvider for InMemoryRandomAccessStream */
DEFINE_IINSPECTABLE( in_memory_stream_content_type, IContentTypeProvider, struct in_memory_stream, IRandomAccessStream_iface )

static HRESULT WINAPI in_memory_stream_content_type_get_ContentType( IContentTypeProvider *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"application/octet-stream", 24, value );
}

static const struct IContentTypeProviderVtbl in_memory_stream_content_type_vtbl =
{
    in_memory_stream_content_type_QueryInterface,
    in_memory_stream_content_type_AddRef,
    in_memory_stream_content_type_Release,
    in_memory_stream_content_type_GetIids,
    in_memory_stream_content_type_GetRuntimeClassName,
    in_memory_stream_content_type_GetTrustLevel,
    in_memory_stream_content_type_get_ContentType,
};

/* IClosable for InMemoryRandomAccessStream */
DEFINE_IINSPECTABLE_( in_memory_stream_closable, IClosable, struct in_memory_stream, impl_from_IClosable_Stream, IClosable_iface, &impl->IRandomAccessStream_iface )

static HRESULT WINAPI in_memory_stream_closable_Close( IClosable *iface )
{
    struct in_memory_stream *impl = impl_from_IClosable_Stream( iface );

    EnterCriticalSection( &impl->cs );
    impl->closed = TRUE;
    free( impl->buffer );
    impl->buffer = NULL;
    impl->size = impl->capacity = impl->position = 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IClosableVtbl in_memory_stream_closable_vtbl =
{
    in_memory_stream_closable_QueryInterface,
    in_memory_stream_closable_AddRef,
    in_memory_stream_closable_Release,
    in_memory_stream_closable_GetIids,
    in_memory_stream_closable_GetRuntimeClassName,
    in_memory_stream_closable_GetTrustLevel,
    in_memory_stream_closable_Close,
};

HRESULT in_memory_stream_create( IRandomAccessStream **out )
{
    struct in_memory_stream *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IRandomAccessStream_iface.lpVtbl = &in_memory_stream_vtbl;
    impl->IInputStream_iface.lpVtbl = &in_memory_stream_input_vtbl;
    impl->IOutputStream_iface.lpVtbl = &in_memory_stream_output_vtbl;
    impl->IContentTypeProvider_iface.lpVtbl = &in_memory_stream_content_type_vtbl;
    impl->IClosable_iface.lpVtbl = &in_memory_stream_closable_vtbl;
    impl->ref = 1;
    InitializeCriticalSection( &impl->cs );

    *out = &impl->IRandomAccessStream_iface;
    return S_OK;
}

HRESULT in_memory_stream_create_from_file( const WCHAR *path, IRandomAccessStream **out )
{
    struct in_memory_stream *impl;
    BYTE *bytes = NULL;
    DWORD size = 0;
    HRESULT hr;

    if (FAILED(hr = in_memory_stream_create( out )))
        return hr;

    impl = impl_from_IRandomAccessStream( *out );
    if (SUCCEEDED(read_file_bytes( path, &bytes, &size )))
    {
        impl->buffer = bytes;
        impl->size = size;
        impl->capacity = size;
        impl->position = 0;
    }

    return S_OK;
}

/* InMemoryRandomAccessStream Factory */
struct in_memory_stream_factory_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct in_memory_stream_factory_statics *impl_from_IActivationFactory_InMemoryStream( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct in_memory_stream_factory_statics, IActivationFactory_iface );
}

static HRESULT WINAPI in_memory_stream_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct in_memory_stream_factory_statics *impl = impl_from_IActivationFactory_InMemoryStream( iface );

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

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI in_memory_stream_factory_AddRef( IActivationFactory *iface )
{
    struct in_memory_stream_factory_statics *impl = impl_from_IActivationFactory_InMemoryStream( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI in_memory_stream_factory_Release( IActivationFactory *iface )
{
    struct in_memory_stream_factory_statics *impl = impl_from_IActivationFactory_InMemoryStream( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI in_memory_stream_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.InMemoryRandomAccessStream", 49, class_name );
}

static HRESULT WINAPI in_memory_stream_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IRandomAccessStream *stream = NULL;
    HRESULT hr;

    if (!instance) return E_POINTER;
    if (FAILED(hr = in_memory_stream_create( &stream )))
        return hr;

    *instance = (IInspectable *)stream;
    return S_OK;
}

static const struct IActivationFactoryVtbl in_memory_stream_factory_vtbl =
{
    in_memory_stream_factory_QueryInterface,
    in_memory_stream_factory_AddRef,
    in_memory_stream_factory_Release,
    in_memory_stream_factory_GetIids,
    in_memory_stream_factory_GetRuntimeClassName,
    in_memory_stream_factory_GetTrustLevel,
    in_memory_stream_factory_ActivateInstance,
};

static struct in_memory_stream_factory_statics in_memory_stream_factory_obj =
{
    {&in_memory_stream_factory_vtbl},
    1,
};

IActivationFactory *in_memory_random_access_stream_factory = &in_memory_stream_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* DataReader Implementation                                                  */
/* -------------------------------------------------------------------------- */

struct data_reader
{
    IDataReader IDataReader_iface;
    IClosable IClosable_iface;
    LONG ref;

    CRITICAL_SECTION cs;
    IInputStream *input_stream;
    BYTE *buffer;
    UINT32 capacity;
    UINT32 length;
    UINT32 position;
    UnicodeEncoding encoding;
    ByteOrder byte_order;
    InputStreamOptions options;
    BOOL closed;
};

static inline struct data_reader *impl_from_IDataReader( IDataReader *iface )
{
    return CONTAINING_RECORD( iface, struct data_reader, IDataReader_iface );
}

static HRESULT WINAPI data_reader_QueryInterface( IDataReader *iface, REFIID iid, void **out )
{
    struct data_reader *impl = impl_from_IDataReader( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataReader ))
    {
        *out = &impl->IDataReader_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI data_reader_AddRef( IDataReader *iface )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI data_reader_Release( IDataReader *iface )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->input_stream)
            IInputStream_Release( impl->input_stream );
        DeleteCriticalSection( &impl->cs );
        free( impl->buffer );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI data_reader_GetIids( IDataReader *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI data_reader_GetRuntimeClassName( IDataReader *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataReader", 33, class_name );
}

static HRESULT WINAPI data_reader_GetTrustLevel( IDataReader *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI data_reader_get_UnconsumedBufferLength( IDataReader *iface, UINT32 *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *value = (impl->position < impl->length) ? (impl->length - impl->position) : 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI data_reader_get_UnicodeEncoding( IDataReader *iface, UnicodeEncoding *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    *value = impl->encoding;
    return S_OK;
}

static HRESULT WINAPI data_reader_put_UnicodeEncoding( IDataReader *iface, UnicodeEncoding value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    impl->encoding = value;
    return S_OK;
}

static HRESULT WINAPI data_reader_get_ByteOrder( IDataReader *iface, ByteOrder *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    *value = impl->byte_order;
    return S_OK;
}

static HRESULT WINAPI data_reader_put_ByteOrder( IDataReader *iface, ByteOrder value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    impl->byte_order = value;
    return S_OK;
}

static HRESULT WINAPI data_reader_get_InputStreamOptions( IDataReader *iface, InputStreamOptions *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    *value = impl->options;
    return S_OK;
}

static HRESULT WINAPI data_reader_put_InputStreamOptions( IDataReader *iface, InputStreamOptions value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    impl->options = value;
    return S_OK;
}

static HRESULT data_reader_read_raw( struct data_reader *impl, void *dest, UINT32 count )
{
    HRESULT hr = S_OK;
    EnterCriticalSection( &impl->cs );
    if (impl->position + count > impl->length)
    {
        hr = E_BOUNDS;
    }
    else
    {
        memcpy( dest, impl->buffer + impl->position, count );
        impl->position += count;
    }
    LeaveCriticalSection( &impl->cs );
    return hr;
}

static HRESULT WINAPI data_reader_ReadByte( IDataReader *iface, BYTE *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    return data_reader_read_raw( impl, value, 1 );
}

static HRESULT WINAPI data_reader_ReadBytes( IDataReader *iface, UINT32 value_size, BYTE *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value && value_size) return E_POINTER;
    return data_reader_read_raw( impl, value, value_size );
}

static HRESULT WINAPI data_reader_ReadBuffer( IDataReader *iface, UINT32 length, IBuffer **buffer )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    IBuffer *buf = NULL;
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    HRESULT hr;

    if (!buffer) return E_POINTER;

    if (FAILED(hr = buffer_create( length, &buf )))
        return hr;

    if (length)
    {
        if (FAILED(hr = IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
        {
            IBuffer_Release( buf );
            return hr;
        }
        IBufferByteAccess_Buffer( access, &dest );
        IBufferByteAccess_Release( access );

        if (FAILED(hr = data_reader_read_raw( impl, dest, length )))
        {
            IBuffer_Release( buf );
            return hr;
        }
    }
    IBuffer_put_Length( buf, length );
    *buffer = buf;
    return S_OK;
}

static HRESULT WINAPI data_reader_ReadBoolean( IDataReader *iface, boolean *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    BYTE b = 0;
    HRESULT hr;

    if (!value) return E_POINTER;
    if (SUCCEEDED(hr = data_reader_read_raw( impl, &b, 1 )))
        *value = (b != 0);
    return hr;
}

static HRESULT WINAPI data_reader_ReadGuid( IDataReader *iface, GUID *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    return data_reader_read_raw( impl, value, sizeof(GUID) );
}

static HRESULT WINAPI data_reader_ReadInt16( IDataReader *iface, INT16 *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = data_reader_read_raw( impl, value, sizeof(INT16) );
    if (SUCCEEDED(hr) && impl->byte_order == ByteOrder_BigEndian)
        *value = __builtin_bswap16( *value );
    return hr;
}

static HRESULT WINAPI data_reader_ReadInt32( IDataReader *iface, INT32 *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = data_reader_read_raw( impl, value, sizeof(INT32) );
    if (SUCCEEDED(hr) && impl->byte_order == ByteOrder_BigEndian)
        *value = __builtin_bswap32( *value );
    return hr;
}

static HRESULT WINAPI data_reader_ReadInt64( IDataReader *iface, INT64 *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = data_reader_read_raw( impl, value, sizeof(INT64) );
    if (SUCCEEDED(hr) && impl->byte_order == ByteOrder_BigEndian)
        *value = __builtin_bswap64( *value );
    return hr;
}

static HRESULT WINAPI data_reader_ReadUInt16( IDataReader *iface, UINT16 *value )
{
    return data_reader_ReadInt16( iface, (INT16 *)value );
}

static HRESULT WINAPI data_reader_ReadUInt32( IDataReader *iface, UINT32 *value )
{
    return data_reader_ReadInt32( iface, (INT32 *)value );
}

static HRESULT WINAPI data_reader_ReadUInt64( IDataReader *iface, UINT64 *value )
{
    return data_reader_ReadInt64( iface, (INT64 *)value );
}

static HRESULT WINAPI data_reader_ReadSingle( IDataReader *iface, FLOAT *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    return data_reader_read_raw( impl, value, sizeof(FLOAT) );
}

static HRESULT WINAPI data_reader_ReadDouble( IDataReader *iface, DOUBLE *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!value) return E_POINTER;
    return data_reader_read_raw( impl, value, sizeof(DOUBLE) );
}

static HRESULT WINAPI data_reader_ReadString( IDataReader *iface, UINT32 code_unit_count, HSTRING *value )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    HRESULT hr;

    if (!value) return E_POINTER;

    if (impl->encoding == UnicodeEncoding_Utf8)
    {
        char *utf8 = malloc( code_unit_count + 1 );
        int wlen;
        WCHAR *wbuf;

        if (!utf8) return E_OUTOFMEMORY;
        if (FAILED(hr = data_reader_read_raw( impl, utf8, code_unit_count )))
        {
            free( utf8 );
            return hr;
        }
        utf8[code_unit_count] = 0;
        wlen = MultiByteToWideChar( CP_UTF8, 0, utf8, code_unit_count, NULL, 0 );
        wbuf = malloc( (wlen + 1) * sizeof(WCHAR) );
        if (!wbuf)
        {
            free( utf8 );
            return E_OUTOFMEMORY;
        }
        MultiByteToWideChar( CP_UTF8, 0, utf8, code_unit_count, wbuf, wlen );
        wbuf[wlen] = 0;
        hr = WindowsCreateString( wbuf, wlen, value );
        free( wbuf );
        free( utf8 );
        return hr;
    }
    else
    {
        UINT32 bytes = code_unit_count * sizeof(WCHAR);
        WCHAR *wbuf = malloc( bytes + sizeof(WCHAR) );
        if (!wbuf) return E_OUTOFMEMORY;
        if (FAILED(hr = data_reader_read_raw( impl, wbuf, bytes )))
        {
            free( wbuf );
            return hr;
        }
        wbuf[code_unit_count] = 0;
        hr = WindowsCreateString( wbuf, code_unit_count, value );
        free( wbuf );
        return hr;
    }
}

static HRESULT WINAPI data_reader_ReadDateTime( IDataReader *iface, DateTime *value )
{
    if (!value) return E_POINTER;
    return data_reader_ReadInt64( iface, &value->UniversalTime );
}

static HRESULT WINAPI data_reader_ReadTimeSpan( IDataReader *iface, TimeSpan *value )
{
    if (!value) return E_POINTER;
    return data_reader_ReadInt64( iface, &value->Duration );
}

static HRESULT WINAPI data_reader_LoadAsync( IDataReader *iface, UINT32 count,
                                            IAsyncOperation_UINT32 **operation )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    UINT32 loaded = 0;

    TRACE( "iface %p, count %u, operation %p.\n", iface, count, operation );

    if (!operation) return E_POINTER;

    EnterCriticalSection( &impl->cs );
    if (impl->input_stream && count)
    {
        IBuffer *buf = NULL;
        IAsyncOperationWithProgress_IBuffer_UINT32 *read_op = NULL;
        if (SUCCEEDED(buffer_create( count, &buf )))
        {
            if (SUCCEEDED(IInputStream_ReadAsync( impl->input_stream, buf, count, impl->options, &read_op )))
            {
                IBufferByteAccess *access = NULL;
                BYTE *data = NULL;
                UINT32 rlen = 0;

                IBuffer_get_Length( buf, &rlen );
                if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
                {
                    if (SUCCEEDED(IBufferByteAccess_Buffer( access, &data )) && data && rlen)
                    {
                        if (impl->length + rlen > impl->capacity)
                        {
                            UINT32 new_cap = max( impl->capacity * 2, impl->length + rlen + 64 );
                            impl->buffer = realloc( impl->buffer, new_cap );
                            impl->capacity = new_cap;
                        }
                        memcpy( impl->buffer + impl->length, data, rlen );
                        impl->length += rlen;
                        loaded = rlen;
                    }
                    IBufferByteAccess_Release( access );
                }
                IAsyncOperationWithProgress_IBuffer_UINT32_Release( read_op );
            }
            IBuffer_Release( buf );
        }
    }
    LeaveCriticalSection( &impl->cs );

    return async_operation_uint32_create( L"Windows.Storage.Streams.DataReaderLoadOperation", loaded,
                                          (IAsyncOperation_UINT32 **)operation );
}

static HRESULT WINAPI data_reader_DetachBuffer( IDataReader *iface, IBuffer **buffer )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    IBuffer *buf = NULL;
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    UINT32 unconsumed;
    HRESULT hr;

    if (!buffer) return E_POINTER;

    EnterCriticalSection( &impl->cs );
    unconsumed = (impl->position < impl->length) ? (impl->length - impl->position) : 0;
    if (FAILED(hr = buffer_create( unconsumed, &buf )))
    {
        LeaveCriticalSection( &impl->cs );
        return hr;
    }

    if (unconsumed)
    {
        if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
        {
            if (SUCCEEDED(IBufferByteAccess_Buffer( access, &dest )) && dest)
                memcpy( dest, impl->buffer + impl->position, unconsumed );
            IBufferByteAccess_Release( access );
        }
        IBuffer_put_Length( buf, unconsumed );
    }
    impl->position = impl->length;
    LeaveCriticalSection( &impl->cs );

    *buffer = buf;
    return S_OK;
}

static HRESULT WINAPI data_reader_DetachStream( IDataReader *iface, IInputStream **stream )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!stream) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *stream = impl->input_stream;
    impl->input_stream = NULL;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IDataReaderVtbl data_reader_vtbl =
{
    data_reader_QueryInterface,
    data_reader_AddRef,
    data_reader_Release,
    data_reader_GetIids,
    data_reader_GetRuntimeClassName,
    data_reader_GetTrustLevel,
    data_reader_get_UnconsumedBufferLength,
    data_reader_get_UnicodeEncoding,
    data_reader_put_UnicodeEncoding,
    data_reader_get_ByteOrder,
    data_reader_put_ByteOrder,
    data_reader_get_InputStreamOptions,
    data_reader_put_InputStreamOptions,
    data_reader_ReadByte,
    data_reader_ReadBytes,
    data_reader_ReadBuffer,
    data_reader_ReadBoolean,
    data_reader_ReadGuid,
    data_reader_ReadInt16,
    data_reader_ReadInt32,
    data_reader_ReadInt64,
    data_reader_ReadUInt16,
    data_reader_ReadUInt32,
    data_reader_ReadUInt64,
    data_reader_ReadSingle,
    data_reader_ReadDouble,
    data_reader_ReadString,
    data_reader_ReadDateTime,
    data_reader_ReadTimeSpan,
    data_reader_LoadAsync,
    data_reader_DetachBuffer,
    data_reader_DetachStream,
};

/* IClosable for DataReader */
DEFINE_IINSPECTABLE_( data_reader_closable, IClosable, struct data_reader, impl_from_IClosable_DataReader, IClosable_iface, &impl->IDataReader_iface )

static HRESULT WINAPI data_reader_closable_Close( IClosable *iface )
{
    struct data_reader *impl = impl_from_IClosable_DataReader( iface );

    EnterCriticalSection( &impl->cs );
    impl->closed = TRUE;
    if (impl->input_stream)
    {
        IInputStream_Release( impl->input_stream );
        impl->input_stream = NULL;
    }
    free( impl->buffer );
    impl->buffer = NULL;
    impl->length = impl->capacity = impl->position = 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IClosableVtbl data_reader_closable_vtbl =
{
    data_reader_closable_QueryInterface,
    data_reader_closable_AddRef,
    data_reader_closable_Release,
    data_reader_closable_GetIids,
    data_reader_closable_GetRuntimeClassName,
    data_reader_closable_GetTrustLevel,
    data_reader_closable_Close,
};

HRESULT data_reader_create( IInputStream *stream, IDataReader **out )
{
    struct data_reader *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IDataReader_iface.lpVtbl = &data_reader_vtbl;
    impl->IClosable_iface.lpVtbl = &data_reader_closable_vtbl;
    impl->ref = 1;
    impl->encoding = UnicodeEncoding_Utf8;
    impl->byte_order = ByteOrder_LittleEndian;
    impl->options = InputStreamOptions_None;
    InitializeCriticalSection( &impl->cs );

    if (stream)
    {
        impl->input_stream = stream;
        IInputStream_AddRef( stream );
    }

    *out = &impl->IDataReader_iface;
    return S_OK;
}

/* DataReader Factory & Statics */
struct data_reader_statics
{
    IActivationFactory IActivationFactory_iface;
    IDataReaderFactory IDataReaderFactory_iface;
    IDataReaderStatics IDataReaderStatics_iface;
    LONG ref;
};

static inline struct data_reader_statics *impl_from_IActivationFactory_DataReader( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct data_reader_statics, IActivationFactory_iface );
}

static HRESULT WINAPI data_reader_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct data_reader_statics *impl = impl_from_IActivationFactory_DataReader( iface );

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

    if (IsEqualGUID( iid, &IID_IDataReaderFactory ))
    {
        *out = &impl->IDataReaderFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataReaderStatics ))
    {
        *out = &impl->IDataReaderStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI data_reader_factory_AddRef( IActivationFactory *iface )
{
    struct data_reader_statics *impl = impl_from_IActivationFactory_DataReader( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI data_reader_factory_Release( IActivationFactory *iface )
{
    struct data_reader_statics *impl = impl_from_IActivationFactory_DataReader( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI data_reader_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI data_reader_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataReader", 33, class_name );
}

static HRESULT WINAPI data_reader_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI data_reader_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl data_reader_factory_vtbl =
{
    data_reader_factory_QueryInterface,
    data_reader_factory_AddRef,
    data_reader_factory_Release,
    data_reader_factory_GetIids,
    data_reader_factory_GetRuntimeClassName,
    data_reader_factory_GetTrustLevel,
    data_reader_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( data_reader_factory_methods, IDataReaderFactory, struct data_reader_statics, IActivationFactory_iface )

static HRESULT WINAPI data_reader_factory_methods_CreateDataReader( IDataReaderFactory *iface,
                                                                   IInputStream *input_stream,
                                                                   IDataReader **data_reader )
{
    return data_reader_create( input_stream, data_reader );
}

static const struct IDataReaderFactoryVtbl data_reader_factory_methods_vtbl =
{
    data_reader_factory_methods_QueryInterface,
    data_reader_factory_methods_AddRef,
    data_reader_factory_methods_Release,
    data_reader_factory_methods_GetIids,
    data_reader_factory_methods_GetRuntimeClassName,
    data_reader_factory_methods_GetTrustLevel,
    data_reader_factory_methods_CreateDataReader,
};

DEFINE_IINSPECTABLE( data_reader_statics_methods, IDataReaderStatics, struct data_reader_statics, IActivationFactory_iface )

static HRESULT WINAPI data_reader_statics_methods_FromBuffer( IDataReaderStatics *iface, IBuffer *buffer,
                                                             IDataReader **data_reader )
{
    struct data_reader *reader_impl;
    IBufferByteAccess *access = NULL;
    BYTE *data = NULL;
    UINT32 len = 0;
    HRESULT hr;

    if (!buffer || !data_reader) return E_POINTER;

    if (FAILED(hr = data_reader_create( NULL, data_reader )))
        return hr;

    reader_impl = impl_from_IDataReader( *data_reader );
    IBuffer_get_Length( buffer, &len );

    if (len && SUCCEEDED(IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
    {
        if (SUCCEEDED(IBufferByteAccess_Buffer( access, &data )) && data)
        {
            reader_impl->buffer = malloc( len );
            if (reader_impl->buffer)
            {
                memcpy( reader_impl->buffer, data, len );
                reader_impl->length = reader_impl->capacity = len;
            }
        }
        IBufferByteAccess_Release( access );
    }

    return S_OK;
}

static const struct IDataReaderStaticsVtbl data_reader_statics_methods_vtbl =
{
    data_reader_statics_methods_QueryInterface,
    data_reader_statics_methods_AddRef,
    data_reader_statics_methods_Release,
    data_reader_statics_methods_GetIids,
    data_reader_statics_methods_GetRuntimeClassName,
    data_reader_statics_methods_GetTrustLevel,
    data_reader_statics_methods_FromBuffer,
};

static struct data_reader_statics data_reader_statics_obj =
{
    {&data_reader_factory_vtbl},
    {&data_reader_factory_methods_vtbl},
    {&data_reader_statics_methods_vtbl},
    1,
};

IActivationFactory *data_reader_factory = &data_reader_statics_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* DataWriter Implementation                                                  */
/* -------------------------------------------------------------------------- */

struct data_writer
{
    IDataWriter IDataWriter_iface;
    IClosable IClosable_iface;
    LONG ref;

    CRITICAL_SECTION cs;
    IOutputStream *output_stream;
    BYTE *buffer;
    UINT32 capacity;
    UINT32 length;
    UnicodeEncoding encoding;
    ByteOrder byte_order;
    BOOL closed;
};

static inline struct data_writer *impl_from_IDataWriter( IDataWriter *iface )
{
    return CONTAINING_RECORD( iface, struct data_writer, IDataWriter_iface );
}

static HRESULT WINAPI data_writer_QueryInterface( IDataWriter *iface, REFIID iid, void **out )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataWriter ))
    {
        *out = &impl->IDataWriter_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->IClosable_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI data_writer_AddRef( IDataWriter *iface )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI data_writer_Release( IDataWriter *iface )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->output_stream)
            IOutputStream_Release( impl->output_stream );
        DeleteCriticalSection( &impl->cs );
        free( impl->buffer );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI data_writer_GetIids( IDataWriter *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI data_writer_GetRuntimeClassName( IDataWriter *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataWriter", 33, class_name );
}

static HRESULT WINAPI data_writer_GetTrustLevel( IDataWriter *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI data_writer_get_UnstoredBufferLength( IDataWriter *iface, UINT32 *value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *value = impl->length;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI data_writer_get_UnicodeEncoding( IDataWriter *iface, UnicodeEncoding *value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    if (!value) return E_POINTER;
    *value = impl->encoding;
    return S_OK;
}

static HRESULT WINAPI data_writer_put_UnicodeEncoding( IDataWriter *iface, UnicodeEncoding value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    impl->encoding = value;
    return S_OK;
}

static HRESULT WINAPI data_writer_get_ByteOrder( IDataWriter *iface, ByteOrder *value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    if (!value) return E_POINTER;
    *value = impl->byte_order;
    return S_OK;
}

static HRESULT WINAPI data_writer_put_ByteOrder( IDataWriter *iface, ByteOrder value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    impl->byte_order = value;
    return S_OK;
}

static HRESULT data_writer_write_raw( struct data_writer *impl, const void *src, UINT32 count )
{
    EnterCriticalSection( &impl->cs );
    if (impl->length + count > impl->capacity)
    {
        UINT32 new_cap = max( impl->capacity * 2, impl->length + count + 64 );
        BYTE *new_buf = realloc( impl->buffer, new_cap );
        if (!new_buf)
        {
            LeaveCriticalSection( &impl->cs );
            return E_OUTOFMEMORY;
        }
        impl->buffer = new_buf;
        impl->capacity = new_cap;
    }
    memcpy( impl->buffer + impl->length, src, count );
    impl->length += count;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI data_writer_WriteByte( IDataWriter *iface, BYTE value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    return data_writer_write_raw( impl, &value, 1 );
}

static HRESULT WINAPI data_writer_WriteBytes( IDataWriter *iface, UINT32 value_size, BYTE *value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    return data_writer_write_raw( impl, value, value_size );
}

static HRESULT WINAPI data_writer_WriteBuffer( IDataWriter *iface, IBuffer *buffer )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    IBufferByteAccess *access = NULL;
    BYTE *data = NULL;
    UINT32 len = 0;
    HRESULT hr;

    if (!buffer) return E_POINTER;

    IBuffer_get_Length( buffer, &len );
    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        return hr;

    hr = IBufferByteAccess_Buffer( access, &data );
    IBufferByteAccess_Release( access );
    if (FAILED(hr)) return hr;

    return data_writer_write_raw( impl, data, len );
}

static HRESULT WINAPI data_writer_WriteBufferRange( IDataWriter *iface, IBuffer *buffer, UINT32 start, UINT32 count )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    IBufferByteAccess *access = NULL;
    BYTE *data = NULL;
    UINT32 len = 0;
    HRESULT hr;

    if (!buffer) return E_POINTER;

    IBuffer_get_Length( buffer, &len );
    if (start + count > len) return E_INVALIDARG;

    if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        return hr;

    hr = IBufferByteAccess_Buffer( access, &data );
    IBufferByteAccess_Release( access );
    if (FAILED(hr)) return hr;

    return data_writer_write_raw( impl, data + start, count );
}

static HRESULT WINAPI data_writer_WriteBoolean( IDataWriter *iface, boolean value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    BYTE b = value ? 1 : 0;
    return data_writer_write_raw( impl, &b, 1 );
}

static HRESULT WINAPI data_writer_WriteGuid( IDataWriter *iface, GUID value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    return data_writer_write_raw( impl, &value, sizeof(GUID) );
}

static HRESULT WINAPI data_writer_WriteInt16( IDataWriter *iface, INT16 value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    if (impl->byte_order == ByteOrder_BigEndian)
        value = __builtin_bswap16( value );
    return data_writer_write_raw( impl, &value, sizeof(INT16) );
}

static HRESULT WINAPI data_writer_WriteInt32( IDataWriter *iface, INT32 value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    if (impl->byte_order == ByteOrder_BigEndian)
        value = __builtin_bswap32( value );
    return data_writer_write_raw( impl, &value, sizeof(INT32) );
}

static HRESULT WINAPI data_writer_WriteInt64( IDataWriter *iface, INT64 value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    if (impl->byte_order == ByteOrder_BigEndian)
        value = __builtin_bswap64( value );
    return data_writer_write_raw( impl, &value, sizeof(INT64) );
}

static HRESULT WINAPI data_writer_WriteUInt16( IDataWriter *iface, UINT16 value )
{
    return data_writer_WriteInt16( iface, (INT16)value );
}

static HRESULT WINAPI data_writer_WriteUInt32( IDataWriter *iface, UINT32 value )
{
    return data_writer_WriteInt32( iface, (INT32)value );
}

static HRESULT WINAPI data_writer_WriteUInt64( IDataWriter *iface, UINT64 value )
{
    return data_writer_WriteInt64( iface, (INT64)value );
}

static HRESULT WINAPI data_writer_WriteSingle( IDataWriter *iface, FLOAT value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    return data_writer_write_raw( impl, &value, sizeof(FLOAT) );
}

static HRESULT WINAPI data_writer_WriteDouble( IDataWriter *iface, DOUBLE value )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    return data_writer_write_raw( impl, &value, sizeof(DOUBLE) );
}

static HRESULT WINAPI data_writer_WriteDateTime( IDataWriter *iface, DateTime value )
{
    return data_writer_WriteInt64( iface, value.UniversalTime );
}

static HRESULT WINAPI data_writer_WriteTimeSpan( IDataWriter *iface, TimeSpan value )
{
    return data_writer_WriteInt64( iface, value.Duration );
}

static HRESULT WINAPI data_writer_WriteString( IDataWriter *iface, HSTRING value, UINT32 *code_unit_count )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    const WCHAR *raw;
    UINT32 len;
    HRESULT hr;

    raw = WindowsGetStringRawBuffer( value, &len );

    if (impl->encoding == UnicodeEncoding_Utf8)
    {
        int utf8_len = WideCharToMultiByte( CP_UTF8, 0, raw, len, NULL, 0, NULL, NULL );
        char *utf8 = malloc( utf8_len );
        if (!utf8) return E_OUTOFMEMORY;
        WideCharToMultiByte( CP_UTF8, 0, raw, len, utf8, utf8_len, NULL, NULL );
        hr = data_writer_write_raw( impl, utf8, utf8_len );
        free( utf8 );
        if (code_unit_count) *code_unit_count = utf8_len;
        return hr;
    }
    else
    {
        hr = data_writer_write_raw( impl, raw, len * sizeof(WCHAR) );
        if (code_unit_count) *code_unit_count = len;
        return hr;
    }
}

static HRESULT WINAPI data_writer_MeasureString( IDataWriter *iface, HSTRING value, UINT32 *code_unit_count )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    const WCHAR *raw;
    UINT32 len;

    if (!code_unit_count) return E_POINTER;
    raw = WindowsGetStringRawBuffer( value, &len );

    if (impl->encoding == UnicodeEncoding_Utf8)
        *code_unit_count = WideCharToMultiByte( CP_UTF8, 0, raw, len, NULL, 0, NULL, NULL );
    else
        *code_unit_count = len;

    return S_OK;
}

static HRESULT WINAPI data_writer_StoreAsync( IDataWriter *iface, IAsyncOperation_UINT32 **operation )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    UINT32 stored = 0;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    EnterCriticalSection( &impl->cs );
    stored = impl->length;
    if (impl->output_stream && impl->length)
    {
        IBuffer *buf = NULL;
        IBufferByteAccess *access = NULL;
        BYTE *dest = NULL;
        IAsyncOperationWithProgress_UINT32_UINT32 *write_op = NULL;

        if (SUCCEEDED(buffer_create( impl->length, &buf )))
        {
            if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
            {
                if (SUCCEEDED(IBufferByteAccess_Buffer( access, &dest )) && dest)
                    memcpy( dest, impl->buffer, impl->length );
                IBufferByteAccess_Release( access );
            }
            IBuffer_put_Length( buf, impl->length );

            if (SUCCEEDED(IOutputStream_WriteAsync( impl->output_stream, buf, &write_op )))
                IAsyncOperationWithProgress_UINT32_UINT32_Release( write_op );

            IBuffer_Release( buf );
        }
        impl->length = 0;
    }
    LeaveCriticalSection( &impl->cs );

    return async_operation_uint32_create( L"Windows.Storage.Streams.DataWriterStoreOperation", stored,
                                          (IAsyncOperation_UINT32 **)operation );
}

static HRESULT WINAPI data_writer_FlushAsync( IDataWriter *iface, IAsyncOperation_boolean **operation )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );

    if (!operation) return E_POINTER;

    if (impl->output_stream)
        return IOutputStream_FlushAsync( impl->output_stream, operation );

    return async_operation_boolean_create( L"Windows.Foundation.IAsyncOperation`1<Boolean>", TRUE, operation );
}

static HRESULT WINAPI data_writer_DetachBuffer( IDataWriter *iface, IBuffer **buffer )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    IBuffer *buf = NULL;
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    HRESULT hr;

    if (!buffer) return E_POINTER;

    EnterCriticalSection( &impl->cs );
    if (FAILED(hr = buffer_create( impl->length, &buf )))
    {
        LeaveCriticalSection( &impl->cs );
        return hr;
    }

    if (impl->length)
    {
        if (SUCCEEDED(IBuffer_QueryInterface( buf, &IID_IBufferByteAccess, (void **)&access )))
        {
            if (SUCCEEDED(IBufferByteAccess_Buffer( access, &dest )) && dest)
                memcpy( dest, impl->buffer, impl->length );
            IBufferByteAccess_Release( access );
        }
        IBuffer_put_Length( buf, impl->length );
    }
    impl->length = 0;
    LeaveCriticalSection( &impl->cs );

    *buffer = buf;
    return S_OK;
}

static HRESULT WINAPI data_writer_DetachStream( IDataWriter *iface, IOutputStream **output_stream )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );
    if (!output_stream) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *output_stream = impl->output_stream;
    impl->output_stream = NULL;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IDataWriterVtbl data_writer_vtbl =
{
    data_writer_QueryInterface,
    data_writer_AddRef,
    data_writer_Release,
    data_writer_GetIids,
    data_writer_GetRuntimeClassName,
    data_writer_GetTrustLevel,
    data_writer_get_UnstoredBufferLength,
    data_writer_get_UnicodeEncoding,
    data_writer_put_UnicodeEncoding,
    data_writer_get_ByteOrder,
    data_writer_put_ByteOrder,
    data_writer_WriteByte,
    data_writer_WriteBytes,
    data_writer_WriteBuffer,
    data_writer_WriteBufferRange,
    data_writer_WriteBoolean,
    data_writer_WriteGuid,
    data_writer_WriteInt16,
    data_writer_WriteInt32,
    data_writer_WriteInt64,
    data_writer_WriteUInt16,
    data_writer_WriteUInt32,
    data_writer_WriteUInt64,
    data_writer_WriteSingle,
    data_writer_WriteDouble,
    data_writer_WriteDateTime,
    data_writer_WriteTimeSpan,
    data_writer_WriteString,
    data_writer_MeasureString,
    data_writer_StoreAsync,
    data_writer_FlushAsync,
    data_writer_DetachBuffer,
    data_writer_DetachStream,
};

/* IClosable for DataWriter */
DEFINE_IINSPECTABLE_( data_writer_closable, IClosable, struct data_writer, impl_from_IClosable_DataWriter, IClosable_iface, &impl->IDataWriter_iface )

static HRESULT WINAPI data_writer_closable_Close( IClosable *iface )
{
    struct data_writer *impl = impl_from_IClosable_DataWriter( iface );

    EnterCriticalSection( &impl->cs );
    impl->closed = TRUE;
    if (impl->output_stream)
    {
        IOutputStream_Release( impl->output_stream );
        impl->output_stream = NULL;
    }
    free( impl->buffer );
    impl->buffer = NULL;
    impl->length = impl->capacity = 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct IClosableVtbl data_writer_closable_vtbl =
{
    data_writer_closable_QueryInterface,
    data_writer_closable_AddRef,
    data_writer_closable_Release,
    data_writer_closable_GetIids,
    data_writer_closable_GetRuntimeClassName,
    data_writer_closable_GetTrustLevel,
    data_writer_closable_Close,
};

HRESULT data_writer_create( IOutputStream *stream, IDataWriter **out )
{
    struct data_writer *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IDataWriter_iface.lpVtbl = &data_writer_vtbl;
    impl->IClosable_iface.lpVtbl = &data_writer_closable_vtbl;
    impl->ref = 1;
    impl->encoding = UnicodeEncoding_Utf8;
    impl->byte_order = ByteOrder_LittleEndian;
    InitializeCriticalSection( &impl->cs );

    if (stream)
    {
        impl->output_stream = stream;
        IOutputStream_AddRef( stream );
    }

    *out = &impl->IDataWriter_iface;
    return S_OK;
}

/* DataWriter Factory */
struct data_writer_factory_statics
{
    IActivationFactory IActivationFactory_iface;
    IDataWriterFactory IDataWriterFactory_iface;
    LONG ref;
};

static inline struct data_writer_factory_statics *impl_from_IActivationFactory_DataWriter( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct data_writer_factory_statics, IActivationFactory_iface );
}

static HRESULT WINAPI data_writer_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct data_writer_factory_statics *impl = impl_from_IActivationFactory_DataWriter( iface );

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

    if (IsEqualGUID( iid, &IID_IDataWriterFactory ))
    {
        *out = &impl->IDataWriterFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI data_writer_factory_AddRef( IActivationFactory *iface )
{
    struct data_writer_factory_statics *impl = impl_from_IActivationFactory_DataWriter( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI data_writer_factory_Release( IActivationFactory *iface )
{
    struct data_writer_factory_statics *impl = impl_from_IActivationFactory_DataWriter( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI data_writer_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI data_writer_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataWriter", 33, class_name );
}

static HRESULT WINAPI data_writer_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI data_writer_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IDataWriter *writer = NULL;
    HRESULT hr;

    if (!instance) return E_POINTER;
    if (FAILED(hr = data_writer_create( NULL, &writer )))
        return hr;

    *instance = (IInspectable *)writer;
    return S_OK;
}

static const struct IActivationFactoryVtbl data_writer_factory_vtbl =
{
    data_writer_factory_QueryInterface,
    data_writer_factory_AddRef,
    data_writer_factory_Release,
    data_writer_factory_GetIids,
    data_writer_factory_GetRuntimeClassName,
    data_writer_factory_GetTrustLevel,
    data_writer_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( data_writer_factory_methods, IDataWriterFactory, struct data_writer_factory_statics, IActivationFactory_iface )

static HRESULT WINAPI data_writer_factory_methods_CreateDataWriter( IDataWriterFactory *iface,
                                                                   IOutputStream *output_stream,
                                                                   IDataWriter **data_writer )
{
    return data_writer_create( output_stream, data_writer );
}

static const struct IDataWriterFactoryVtbl data_writer_factory_methods_vtbl =
{
    data_writer_factory_methods_QueryInterface,
    data_writer_factory_methods_AddRef,
    data_writer_factory_methods_Release,
    data_writer_factory_methods_GetIids,
    data_writer_factory_methods_GetRuntimeClassName,
    data_writer_factory_methods_GetTrustLevel,
    data_writer_factory_methods_CreateDataWriter,
};

static struct data_writer_factory_statics data_writer_factory_obj =
{
    {&data_writer_factory_vtbl},
    {&data_writer_factory_methods_vtbl},
    1,
};

IActivationFactory *data_writer_factory = &data_writer_factory_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* RandomAccessStreamReference Implementation                                 */
/* -------------------------------------------------------------------------- */

struct random_access_stream_reference
{
    IRandomAccessStreamReference IRandomAccessStreamReference_iface;
    LONG ref;
    IRandomAccessStream *stream;
};

static inline struct random_access_stream_reference *impl_from_IRandomAccessStreamReference( IRandomAccessStreamReference *iface )
{
    return CONTAINING_RECORD( iface, struct random_access_stream_reference, IRandomAccessStreamReference_iface );
}

static HRESULT WINAPI random_access_stream_reference_QueryInterface( IRandomAccessStreamReference *iface, REFIID iid, void **out )
{
    struct random_access_stream_reference *impl = impl_from_IRandomAccessStreamReference( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStreamReference ))
    {
        *out = &impl->IRandomAccessStreamReference_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI random_access_stream_reference_AddRef( IRandomAccessStreamReference *iface )
{
    struct random_access_stream_reference *impl = impl_from_IRandomAccessStreamReference( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI random_access_stream_reference_Release( IRandomAccessStreamReference *iface )
{
    struct random_access_stream_reference *impl = impl_from_IRandomAccessStreamReference( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->stream)
            IRandomAccessStream_Release( impl->stream );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI random_access_stream_reference_GetIids( IRandomAccessStreamReference *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI random_access_stream_reference_GetRuntimeClassName( IRandomAccessStreamReference *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.RandomAccessStreamReference", 48, class_name );
}

static HRESULT WINAPI random_access_stream_reference_GetTrustLevel( IRandomAccessStreamReference *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI random_access_stream_reference_OpenReadAsync( IRandomAccessStreamReference *iface,
                                                                   IAsyncOperation_IRandomAccessStreamWithContentType **operation )
{
    struct random_access_stream_reference *impl = impl_from_IRandomAccessStreamReference( iface );
    IRandomAccessStream *cloned = NULL;
    HRESULT hr;

    if (!operation) return E_POINTER;

    if (impl->stream)
    {
        if (FAILED(hr = IRandomAccessStream_CloneStream( impl->stream, &cloned )))
            return hr;
    }
    else
    {
        if (FAILED(hr = in_memory_stream_create( &cloned )))
            return hr;
    }

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IRandomAccessStreamWithContentType,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IRandomAccessStreamWithContentType>",
                                            (IInspectable *)cloned, (void **)operation );
    IRandomAccessStream_Release( cloned );
    return hr;
}

static const struct IRandomAccessStreamReferenceVtbl random_access_stream_reference_vtbl =
{
    random_access_stream_reference_QueryInterface,
    random_access_stream_reference_AddRef,
    random_access_stream_reference_Release,
    random_access_stream_reference_GetIids,
    random_access_stream_reference_GetRuntimeClassName,
    random_access_stream_reference_GetTrustLevel,
    random_access_stream_reference_OpenReadAsync,
};

HRESULT random_access_stream_reference_create( IRandomAccessStream *stream, IRandomAccessStreamReference **out )
{
    struct random_access_stream_reference *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IRandomAccessStreamReference_iface.lpVtbl = &random_access_stream_reference_vtbl;
    impl->ref = 1;
    if (stream)
    {
        impl->stream = stream;
        IRandomAccessStream_AddRef( stream );
    }

    *out = &impl->IRandomAccessStreamReference_iface;
    return S_OK;
}

/* RandomAccessStreamReference Factory */
struct random_access_stream_reference_statics
{
    IActivationFactory IActivationFactory_iface;
    IRandomAccessStreamReferenceStatics IRandomAccessStreamReferenceStatics_iface;
    LONG ref;
};

static inline struct random_access_stream_reference_statics *impl_from_IActivationFactory_StreamRef( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct random_access_stream_reference_statics, IActivationFactory_iface );
}

static HRESULT WINAPI stream_ref_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct random_access_stream_reference_statics *impl = impl_from_IActivationFactory_StreamRef( iface );

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

    if (IsEqualGUID( iid, &IID_IRandomAccessStreamReferenceStatics ))
    {
        *out = &impl->IRandomAccessStreamReferenceStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    WARN( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI stream_ref_factory_AddRef( IActivationFactory *iface )
{
    struct random_access_stream_reference_statics *impl = impl_from_IActivationFactory_StreamRef( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI stream_ref_factory_Release( IActivationFactory *iface )
{
    struct random_access_stream_reference_statics *impl = impl_from_IActivationFactory_StreamRef( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI stream_ref_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI stream_ref_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.RandomAccessStreamReference", 48, class_name );
}

static HRESULT WINAPI stream_ref_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI stream_ref_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "stub!\n" );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl stream_ref_factory_vtbl =
{
    stream_ref_factory_QueryInterface,
    stream_ref_factory_AddRef,
    stream_ref_factory_Release,
    stream_ref_factory_GetIids,
    stream_ref_factory_GetRuntimeClassName,
    stream_ref_factory_GetTrustLevel,
    stream_ref_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( stream_ref_statics, IRandomAccessStreamReferenceStatics, struct random_access_stream_reference_statics, IActivationFactory_iface )

static HRESULT WINAPI stream_ref_statics_CreateFromFile( IRandomAccessStreamReferenceStatics *iface, IStorageFile *file,
                                                         IRandomAccessStreamReference **stream_reference )
{
    IStorageItem *item = NULL;
    HSTRING path_str = NULL;
    const WCHAR *raw_path;
    IRandomAccessStream *stream = NULL;
    HRESULT hr;

    if (!file || !stream_reference) return E_POINTER;

    if (FAILED(hr = IStorageFile_QueryInterface( file, &IID_IStorageItem, (void **)&item )))
        return hr;

    IStorageItem_get_Path( item, &path_str );
    IStorageItem_Release( item );

    raw_path = WindowsGetStringRawBuffer( path_str, NULL );
    hr = in_memory_stream_create_from_file( raw_path, &stream );
    WindowsDeleteString( path_str );
    if (FAILED(hr)) return hr;

    hr = random_access_stream_reference_create( stream, stream_reference );
    IRandomAccessStream_Release( stream );
    return hr;
}

static HRESULT WINAPI stream_ref_statics_CreateFromUri( IRandomAccessStreamReferenceStatics *iface, IUriRuntimeClass *uri,
                                                        IRandomAccessStreamReference **stream_reference )
{
    FIXME( "stub!\n" );
    return random_access_stream_reference_create( NULL, stream_reference );
}

static HRESULT WINAPI stream_ref_statics_CreateFromStream( IRandomAccessStreamReferenceStatics *iface, IRandomAccessStream *stream,
                                                           IRandomAccessStreamReference **stream_reference )
{
    if (!stream_reference) return E_POINTER;
    *stream_reference = NULL;
    if (!stream) return E_POINTER;

    return random_access_stream_reference_create( stream, stream_reference );
}

static const struct IRandomAccessStreamReferenceStaticsVtbl stream_ref_statics_vtbl =
{
    stream_ref_statics_QueryInterface,
    stream_ref_statics_AddRef,
    stream_ref_statics_Release,
    stream_ref_statics_GetIids,
    stream_ref_statics_GetRuntimeClassName,
    stream_ref_statics_GetTrustLevel,
    stream_ref_statics_CreateFromFile,
    stream_ref_statics_CreateFromUri,
    stream_ref_statics_CreateFromStream,
};

static struct random_access_stream_reference_statics stream_ref_statics_obj =
{
    {&stream_ref_factory_vtbl},
    {&stream_ref_statics_vtbl},
    1,
};

IActivationFactory *random_access_stream_reference_factory = &stream_ref_statics_obj.IActivationFactory_iface;
