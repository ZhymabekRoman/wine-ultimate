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

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "private.h"
#include "objbase.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

#define PTRACE(...) fprintf( stderr, "ST8 " __VA_ARGS__ )

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
    IID iid_array[] =
    {
        IID_IBuffer,
        IID_IBufferByteAccess,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI buffer_GetRuntimeClassName( IBuffer *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.Buffer", 30, class_name );
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
    TRACE( "iface %p, capacity %u\n", iface, impl->capacity );
    *value = impl->capacity;
    return S_OK;
}

static HRESULT WINAPI buffer_get_Length( IBuffer *iface, UINT32 *value )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    if (!value) return E_POINTER;
    TRACE( "iface %p, length %u\n", iface, impl->length );
    *value = impl->length;
    return S_OK;
}

static HRESULT WINAPI buffer_put_Length( IBuffer *iface, UINT32 value )
{
    struct buffer *impl = impl_from_IBuffer( iface );
    if (value > impl->capacity) return E_INVALIDARG;

    TRACE( "iface %p, value %u (capacity %u)\n", iface, value, impl->capacity );
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
    TRACE( "iface %p, data %p (length %u, capacity %u)\n", iface, impl->data, impl->length, impl->capacity );
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

static HRESULT buffer_create_raw( UINT32 capacity, BOOL zero_init, struct buffer **out )
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
        impl->data = zero_init ? calloc( 1, capacity ) : malloc( capacity );
        if (!impl->data)
        {
            free( impl );
            return E_OUTOFMEMORY;
        }
    }

    *out = impl;
    return S_OK;
}

static HRESULT buffer_ensure_capacity( struct buffer *impl, UINT32 needed )
{
    BYTE *new_data;

    if (needed <= impl->capacity) return S_OK;
    if (!(new_data = realloc( impl->data, needed ))) return E_OUTOFMEMORY;
    impl->data = new_data;
    impl->capacity = needed;
    return S_OK;
}

HRESULT buffer_create( UINT32 capacity, IBuffer **out )
{
    struct buffer *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;

    if (FAILED(hr = buffer_create_raw( capacity, TRUE, &impl )))
        return hr;

    *out = &impl->IBuffer_iface;
    TRACE( "created IBuffer %p (impl %p, data %p, capacity %u).\n", *out, impl, impl->data, capacity );
    return S_OK;
}

/* Buffer Factory & Statics */
struct buffer_statics
{
    IActivationFactory IActivationFactory_iface;
    IBufferFactory IBufferFactory_iface;
    IBufferStatics IBufferStatics_iface;
    IBuffer IBuffer_iface;
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

    /* Windows aggregates the class default interface (IBuffer) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IBuffer ))
    {
        *out = &impl->IBuffer_iface;
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
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IBufferFactory,
        IID_IBufferStatics,
        IID_IBuffer,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI buffer_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.Buffer", 30, class_name );
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


/* Windows aggregates the class default interface (IBuffer) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( buffer_def, IBuffer, struct buffer_statics, impl_from_IBuffer_buffer_def,
                      IBuffer_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI buffer_def_get_Capacity( IBuffer *iface, UINT32 *value ) { return E_FAIL; }
static HRESULT WINAPI buffer_def_get_Length( IBuffer *iface, UINT32 *value ) { return E_FAIL; }
static HRESULT WINAPI buffer_def_put_Length( IBuffer *iface, UINT32 value ) { return E_FAIL; }

static const struct IBufferVtbl buffer_def_v =
{
    buffer_def_QueryInterface,
    buffer_def_AddRef,
    buffer_def_Release,
    buffer_def_GetIids,
    buffer_def_GetRuntimeClassName,
    buffer_def_GetTrustLevel,
    buffer_def_get_Capacity,
    buffer_def_get_Length,
    buffer_def_put_Length,
};

static struct buffer_statics buffer_statics_obj =
{
    {&buffer_factory_vtbl},
    {&buffer_factory_methods_vtbl},
    {&buffer_statics_methods_vtbl},
    {&buffer_def_v}, 1,
};

IActivationFactory *buffer_factory = &buffer_statics_obj.IActivationFactory_iface;

/* -------------------------------------------------------------------------- */
/* InMemoryRandomAccessStream Implementation                                  */
/* -------------------------------------------------------------------------- */

struct in_memory_content
{
    LONG ref;
    CRITICAL_SECTION cs;
    BYTE *buffer;
    UINT64 size;
    UINT64 capacity;

    /* Backing file for streams opened from a StorageFile. Writes are buffered in
     * memory and flushed back to the file on FlushAsync()/Close() (Windows semantics). */
    WCHAR backing_path[STORAGE_MAX_PATH];
    WCHAR content_type[64];
    BOOL dirty;
};

static struct in_memory_content *content_create( void )
{
    struct in_memory_content *content = calloc( 1, sizeof(*content) );

    if (!content) return NULL;
    content->ref = 1;
    lstrcpyW( content->content_type, L"application/octet-stream" );
    InitializeCriticalSection( &content->cs );
    return content;
}

/* Write the buffered content back to the backing file. Caller holds content->cs. */
static void content_flush_to_file( struct in_memory_content *content )
{
    HANDLE hFile;
    DWORD written = 0, size;

    if (!content->dirty || !content->backing_path[0]) return;

    if (content->size > 0xffffffff) size = 0xffffffff;
    else size = (DWORD)content->size;

    hFile = CreateFileW( content->backing_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (hFile == INVALID_HANDLE_VALUE) return;

    if (size) WriteFile( hFile, content->buffer, size, &written, NULL );
    CloseHandle( hFile );
    content->dirty = FALSE;
}

static struct in_memory_content *content_addref( struct in_memory_content *content )
{
    InterlockedIncrement( &content->ref );
    return content;
}

static void content_release( struct in_memory_content *content )
{
    EnterCriticalSection( &content->cs );
    content_flush_to_file( content );
    LeaveCriticalSection( &content->cs );

    if (!InterlockedDecrement( &content->ref ))
    {
        DeleteCriticalSection( &content->cs );
        free( content->buffer );
        free( content );
    }
}

/* Real WinRT layout: IRandomAccessStreamWithContentType extends IRandomAccessStream
 * and appends get_ContentType() as slot 15. Wine's IDL models it as an empty interface,
 * so use an extended vtable with the content type accessor appended; the per-slot
 * thunks below translate the interface pointer to the shared impl. */
struct in_memory_stream_wct_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IRandomAccessStreamWithContentType *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IRandomAccessStreamWithContentType *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IRandomAccessStreamWithContentType *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IRandomAccessStreamWithContentType *iface, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IRandomAccessStreamWithContentType *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IRandomAccessStreamWithContentType *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *get_Size)( IRandomAccessStreamWithContentType *iface, UINT64 *value );
    HRESULT (STDMETHODCALLTYPE *put_Size)( IRandomAccessStreamWithContentType *iface, UINT64 value );
    HRESULT (STDMETHODCALLTYPE *GetInputStreamAt)( IRandomAccessStreamWithContentType *iface, UINT64 position,
                                                   IInputStream **stream );
    HRESULT (STDMETHODCALLTYPE *GetOutputStreamAt)( IRandomAccessStreamWithContentType *iface, UINT64 position,
                                                    IOutputStream **stream );
    HRESULT (STDMETHODCALLTYPE *get_Position)( IRandomAccessStreamWithContentType *iface, UINT64 *value );
    HRESULT (STDMETHODCALLTYPE *Seek)( IRandomAccessStreamWithContentType *iface, UINT64 position );
    HRESULT (STDMETHODCALLTYPE *CloneStream)( IRandomAccessStreamWithContentType *iface, IRandomAccessStream **stream );
    HRESULT (STDMETHODCALLTYPE *get_CanRead)( IRandomAccessStreamWithContentType *iface, boolean *value );
    HRESULT (STDMETHODCALLTYPE *get_CanWrite)( IRandomAccessStreamWithContentType *iface, boolean *value );
    HRESULT (STDMETHODCALLTYPE *get_ContentType)( IRandomAccessStreamWithContentType *iface, HSTRING *value );
};

struct in_memory_stream
{
    IRandomAccessStream IRandomAccessStream_iface;
    IRandomAccessStreamWithContentType IRandomAccessStreamWithContentType_iface;
    IInputStream IInputStream_iface;
    IOutputStream IOutputStream_iface;
    IContentTypeProvider IContentTypeProvider_iface;
    IClosable IClosable_iface;
    LONG ref;

    /* Windows InMemoryRandomAccessStream.Clone()/GetInputStreamAt() semantics: the
     * clone shares the underlying content (writes are visible to both) and carries its
     * own independent position. The content block is reference counted, the position and
     * closed flags are per-stream. */
    struct in_memory_content *content;
    UINT64 position;
    BOOL closed;
    BOOL read_only;
};

static inline struct in_memory_stream *impl_from_IRandomAccessStream( IRandomAccessStream *iface )
{
    return CONTAINING_RECORD( iface, struct in_memory_stream, IRandomAccessStream_iface );
}

/* Win11 (26100) C++/WinRT projection streams the IRandomAccessStream face itself:
 * slot 21 ReadAsync(buffer, position, count, &op), slot 30 ReadAsync(buffer, &op),
 * slot 32 GetResults(&result). WPM's HTTP body pump calls these directly. */
static HRESULT in_memory_stream_read_at( struct in_memory_stream *impl, IBuffer *buffer,
                                         UINT64 pos, UINT32 count,
                                         IAsyncOperationWithProgress_IBuffer_UINT32 **operation )
{
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    UINT32 cap = 0, to_read = 0;
    HRESULT hr;

    if (!buffer || !operation) return E_POINTER;
    *operation = NULL;
    if (impl->closed) return RO_E_CLOSED;
    IBuffer_get_Capacity( buffer, &cap );

    EnterCriticalSection( &impl->content->cs );
    if (pos < impl->content->size)
    {
        UINT64 avail = impl->content->size - pos;
        to_read = (avail < count) ? (UINT32)avail : count;
        if (to_read > cap) to_read = cap;
        if (to_read && SUCCEEDED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        {
            if (SUCCEEDED( IBufferByteAccess_Buffer( access, &dest )) && dest)
                memcpy( dest, impl->content->buffer + pos, to_read );
            IBufferByteAccess_Release( access );
        }
        impl->position = pos + to_read;
    }
    IBuffer_put_Length( buffer, to_read );
    LeaveCriticalSection( &impl->content->cs );

    return async_operation_progress_buffer_create( buffer, operation );
}

static HRESULT WINAPI in_memory_stream_ReadAsync( IRandomAccessStream *iface, IBuffer *buffer,
                                                  IAsyncOperationWithProgress_IBuffer_UINT32 **operation )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    UINT32 cap = 0;

    if (buffer) IBuffer_get_Capacity( buffer, &cap );
    PTRACE( "stream_ReadAsync impl %p cap %u pos %llu size %llu\n", impl, cap,
            impl->position, impl->content->size );
    return in_memory_stream_read_at( impl, buffer, impl->position, cap, operation );
}

/* Win11 projection slot 21: ReadAsync(op**, IBuffer* dest, UINT32 count) —
 * sequential continuation of slot 30 from the stream position. */
static HRESULT WINAPI in_memory_stream_ReadAsyncAt( IRandomAccessStream *iface,
                                                    IAsyncOperationWithProgress_IBuffer_UINT32 **operation,
                                                    IBuffer *buffer, UINT32 count )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );

    PTRACE( "stream_ReadAsyncAt impl %p count %u pos %llu size %llu\n", impl, count,
            impl->position, impl->content->size );
    return in_memory_stream_read_at( impl, buffer, impl->position, count, operation );
}

static HRESULT WINAPI in_memory_stream_StreamGetResults( IRandomAccessStream *iface, void **result )
{
    if (result) *result = NULL;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_QueryInterface( IRandomAccessStream *iface, REFIID iid, void **out )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStream ))
    {
        *out = &impl->IRandomAccessStream_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IRandomAccessStreamWithContentType ))
    {
        *out = &impl->IRandomAccessStreamWithContentType_iface;
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
        content_release( impl->content );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI in_memory_stream_GetIids( IRandomAccessStream *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IRandomAccessStream,
        IID_IRandomAccessStreamWithContentType,
        IID_IInputStream,
        IID_IOutputStream,
        IID_IContentTypeProvider,
        IID_IClosable,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_GetRuntimeClassName( IRandomAccessStream *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.InMemoryRandomAccessStream", 50, class_name );
}

static HRESULT WINAPI in_memory_stream_GetTrustLevel( IRandomAccessStream *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT in_memory_stream_clone_at( struct in_memory_stream *impl, UINT64 position,
                                          struct in_memory_stream **out )
{
    IRandomAccessStream *stream;
    struct in_memory_stream *clone;
    HRESULT hr;

    if (FAILED(hr = in_memory_stream_create( &stream )))
        return hr;

    clone = impl_from_IRandomAccessStream( stream );
    EnterCriticalSection( &impl->content->cs );
    content_release( clone->content );
    clone->content = content_addref( impl->content );
    clone->position = position;
    clone->read_only = impl->read_only;
    LeaveCriticalSection( &impl->content->cs );
    *out = clone;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_get_Size( IRandomAccessStream *iface, UINT64 *value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    if (!value) return E_POINTER;
    if (impl->closed) return RO_E_CLOSED;
    EnterCriticalSection( &impl->content->cs );
    *value = impl->content->size;
    LeaveCriticalSection( &impl->content->cs );
    PTRACE( "get_Size impl %p size %llu\n", impl, *value );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_put_Size( IRandomAccessStream *iface, UINT64 value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );

    if (impl->closed) return RO_E_CLOSED;
    if (impl->read_only) return E_ACCESSDENIED;

    EnterCriticalSection( &impl->content->cs );
    if (value > impl->content->capacity)
    {
        BYTE *new_buf = realloc( impl->content->buffer, value );
        if (!new_buf)
        {
            LeaveCriticalSection( &impl->content->cs );
            return E_OUTOFMEMORY;
        }
        memset( new_buf + impl->content->capacity, 0, value - impl->content->capacity );
        impl->content->buffer = new_buf;
        impl->content->capacity = value;
    }
    if (value > impl->content->size)
        memset( impl->content->buffer + impl->content->size, 0, value - impl->content->size );
    impl->content->size = value;
    impl->content->dirty = TRUE;
    if (impl->position > impl->content->size)
        impl->position = impl->content->size;
    LeaveCriticalSection( &impl->content->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_GetInputStreamAt( IRandomAccessStream *iface, UINT64 position,
                                                         IInputStream **stream )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    struct in_memory_stream *clone;
    HRESULT hr;

    if (!stream) return E_POINTER;
    *stream = NULL;
    if (impl->closed) return RO_E_CLOSED;

    PTRACE( "GetInputStreamAt impl %p pos %llu size %llu\n", impl, position, impl->content->size );
    if (FAILED(hr = in_memory_stream_clone_at( impl, position, &clone )))
        return hr;

    *stream = &clone->IInputStream_iface;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_GetOutputStreamAt( IRandomAccessStream *iface, UINT64 position,
                                                          IOutputStream **stream )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    struct in_memory_stream *clone;
    HRESULT hr;

    if (!stream) return E_POINTER;
    *stream = NULL;
    if (impl->closed) return RO_E_CLOSED;
    if (impl->read_only) return E_ACCESSDENIED;

    if (FAILED(hr = in_memory_stream_clone_at( impl, position, &clone )))
        return hr;

    *stream = &clone->IOutputStream_iface;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_get_Position( IRandomAccessStream *iface, UINT64 *value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    if (!value) return E_POINTER;
    if (impl->closed) return RO_E_CLOSED;
    EnterCriticalSection( &impl->content->cs );
    *value = impl->position;
    LeaveCriticalSection( &impl->content->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_Seek( IRandomAccessStream *iface, UINT64 position )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    if (impl->closed) return RO_E_CLOSED;
    PTRACE( "Seek impl %p pos %llu size %llu\n", impl, position, impl->content->size );
    EnterCriticalSection( &impl->content->cs );
    impl->position = position;
    LeaveCriticalSection( &impl->content->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_CloneStream( IRandomAccessStream *iface, IRandomAccessStream **stream )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStream( iface );
    struct in_memory_stream *clone;
    HRESULT hr;

    if (!stream) return E_POINTER;
    *stream = NULL;
    if (impl->closed) return RO_E_CLOSED;

    /* Windows semantics: the clone shares the source content (writes to one are visible
     * to the other) and starts with its own independent position at the start. */
    if (FAILED(hr = in_memory_stream_clone_at( impl, 0, &clone )))
        return hr;

    *stream = &clone->IRandomAccessStream_iface;
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
    *value = !impl->closed && !impl->read_only;
    return S_OK;
}

static const void *in_memory_stream_vtbl[] =
{
    in_memory_stream_QueryInterface,      /* 0 */
    in_memory_stream_AddRef,
    in_memory_stream_Release,
    in_memory_stream_GetIids,
    in_memory_stream_GetRuntimeClassName,
    in_memory_stream_GetTrustLevel,
    in_memory_stream_get_Size,            /* 6 */
    in_memory_stream_put_Size,
    in_memory_stream_GetInputStreamAt,
    in_memory_stream_GetOutputStreamAt,
    in_memory_stream_get_Position,        /* 10 */
    in_memory_stream_Seek,
    in_memory_stream_CloneStream,
    in_memory_stream_get_CanRead,
    in_memory_stream_get_CanWrite,        /* 14 */
    in_memory_stream_QueryInterface,      /* 15-20 projection fillers */
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,      /* 21-29 projection fillers */
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
    in_memory_stream_ReadAsync,           /* 30 ReadAsync(buffer) */
    in_memory_stream_ReadAsyncAt,         /* 31 ReadAsyncAt(op**, buffer, count) */
    in_memory_stream_StreamGetResults,    /* 32 GetResults */
    in_memory_stream_StreamGetResults,    /* 33 */
    in_memory_stream_StreamGetResults,    /* 34 */
    in_memory_stream_StreamGetResults,    /* 35 */
    in_memory_stream_StreamGetResults,    /* 36 */
    in_memory_stream_QueryInterface,      /* 37-39 */
    in_memory_stream_QueryInterface,
    in_memory_stream_QueryInterface,
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
    UINT32 cap = 0, to_read = 0;
    HRESULT hr;

    if (!buffer || !operation) return E_POINTER;
    *operation = NULL;
    if (impl->closed) return RO_E_CLOSED;

    PTRACE( "input_ReadAsync impl %p count %u pos %llu size %llu\n", impl, count,
            impl->position, impl->content->size );

    IBuffer_get_Capacity( buffer, &cap );

    EnterCriticalSection( &impl->content->cs );
    if (impl->position < impl->content->size)
    {
        UINT64 avail = impl->content->size - impl->position;
        to_read = (avail < count) ? (UINT32)avail : count;
        if (to_read > cap) to_read = cap;
        if (to_read && SUCCEEDED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
        {
            if (SUCCEEDED(IBufferByteAccess_Buffer( access, &dest )) && dest)
                memcpy( dest, impl->content->buffer + impl->position, to_read );
            IBufferByteAccess_Release( access );
        }
        impl->position += to_read;
    }
    IBuffer_put_Length( buffer, to_read );
    LeaveCriticalSection( &impl->content->cs );

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

static HRESULT in_memory_stream_write_raw( struct in_memory_stream *impl, const BYTE *src, UINT32 len )
{
    if (impl->closed) return RO_E_CLOSED;
    if (impl->read_only) return E_ACCESSDENIED;

    EnterCriticalSection( &impl->content->cs );
    if (len)
    {
        UINT64 needed;

        if (len > UINT64_MAX - impl->position)
        {
            LeaveCriticalSection( &impl->content->cs );
            return E_OUTOFMEMORY;
        }
        needed = impl->position + len;
        if (needed > impl->content->capacity)
        {
            UINT64 new_cap = max( impl->content->capacity * 2, needed + 1024 );
            BYTE *new_buf;

            if (new_cap < needed) new_cap = needed;
            if (!(new_buf = realloc( impl->content->buffer, new_cap )))
            {
                LeaveCriticalSection( &impl->content->cs );
                return E_OUTOFMEMORY;
            }
            impl->content->buffer = new_buf;
            impl->content->capacity = new_cap;
        }
        if (impl->position > impl->content->size)
            memset( impl->content->buffer + impl->content->size, 0, impl->position - impl->content->size );
        memcpy( impl->content->buffer + impl->position, src, len );
        impl->position += len;
        if (impl->position > impl->content->size)
            impl->content->size = impl->position;
        impl->content->dirty = TRUE;
    }
    LeaveCriticalSection( &impl->content->cs );
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_output_WriteAsync( IOutputStream *iface, IBuffer *buffer,
                                                          IAsyncOperationWithProgress_UINT32_UINT32 **operation )
{
    struct in_memory_stream *impl = impl_from_IOutputStream( iface );
    IBufferByteAccess *access = NULL;
    BYTE *src = NULL;
    UINT32 len = 0;
    HRESULT hr;

    if (!buffer || !operation) return E_POINTER;
    *operation = NULL;
    if (impl->closed) return RO_E_CLOSED;
    if (impl->read_only) return E_ACCESSDENIED;

    IBuffer_get_Length( buffer, &len );
    PTRACE( "output_WriteAsync impl %p len %u pos %llu\n", impl, len, impl->position );

    if (len)
    {
        if (FAILED(hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access )))
            return hr;
        hr = IBufferByteAccess_Buffer( access, &src );
        IBufferByteAccess_Release( access );
        if (FAILED(hr)) return hr;
        if (FAILED(hr = in_memory_stream_write_raw( impl, src, len )))
            return hr;
    }

    return async_operation_progress_uint32_create( len, operation );
}

static HRESULT WINAPI in_memory_stream_output_FlushAsync( IOutputStream *iface, IAsyncOperation_boolean **operation )
{
    struct in_memory_stream *impl = impl_from_IOutputStream( iface );

    if (!operation) return E_POINTER;
    *operation = NULL;
    if (impl->closed) return RO_E_CLOSED;

    EnterCriticalSection( &impl->content->cs );
    content_flush_to_file( impl->content );
    LeaveCriticalSection( &impl->content->cs );

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
    struct in_memory_stream *impl = impl_from_IContentTypeProvider( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->content->content_type, wcslen( impl->content->content_type ), value );
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

    EnterCriticalSection( &impl->content->cs );
    content_flush_to_file( impl->content );
    impl->closed = TRUE;
    impl->position = 0;
    LeaveCriticalSection( &impl->content->cs );
    return S_OK;
}

/* IRandomAccessStreamWithContentType for InMemoryRandomAccessStream. Shares the
 * IRandomAccessStream vtable slots 0..14 (see struct in_memory_stream_wct_vtbl);
 * only get_ContentType is interface specific. */
static inline struct in_memory_stream *impl_from_IRandomAccessStreamWithContentType(
        IRandomAccessStreamWithContentType *iface )
{
    return CONTAINING_RECORD( iface, struct in_memory_stream, IRandomAccessStreamWithContentType_iface );
}

static HRESULT WINAPI in_memory_stream_wct_get_ContentType( IRandomAccessStreamWithContentType *iface, HSTRING *value )
{
    struct in_memory_stream *impl = impl_from_IRandomAccessStreamWithContentType( iface );
    if (!value) return E_POINTER;
    return WindowsCreateString( impl->content->content_type, wcslen( impl->content->content_type ), value );
}

#define WCT_IMPL struct in_memory_stream *impl = impl_from_IRandomAccessStreamWithContentType( iface )
#define WCT_RAS( fn, ... ) (fn)( &impl->IRandomAccessStream_iface, ##__VA_ARGS__ )

static HRESULT WINAPI in_memory_stream_wct_QueryInterface( IRandomAccessStreamWithContentType *iface, REFIID iid, void **out )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_QueryInterface, iid, out );
}
static ULONG WINAPI in_memory_stream_wct_AddRef( IRandomAccessStreamWithContentType *iface )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_AddRef );
}
static ULONG WINAPI in_memory_stream_wct_Release( IRandomAccessStreamWithContentType *iface )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_Release );
}
static HRESULT WINAPI in_memory_stream_wct_GetIids( IRandomAccessStreamWithContentType *iface, ULONG *count, IID **iids )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_GetIids, count, iids );
}
static HRESULT WINAPI in_memory_stream_wct_GetRuntimeClassName( IRandomAccessStreamWithContentType *iface, HSTRING *name )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_GetRuntimeClassName, name );
}
static HRESULT WINAPI in_memory_stream_wct_GetTrustLevel( IRandomAccessStreamWithContentType *iface, TrustLevel *level )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_GetTrustLevel, level );
}
static HRESULT WINAPI in_memory_stream_wct_get_Size( IRandomAccessStreamWithContentType *iface, UINT64 *value )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_get_Size, value );
}
static HRESULT WINAPI in_memory_stream_wct_put_Size( IRandomAccessStreamWithContentType *iface, UINT64 value )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_put_Size, value );
}
static HRESULT WINAPI in_memory_stream_wct_GetInputStreamAt( IRandomAccessStreamWithContentType *iface,
                                                             UINT64 position, IInputStream **stream )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_GetInputStreamAt, position, stream );
}
static HRESULT WINAPI in_memory_stream_wct_GetOutputStreamAt( IRandomAccessStreamWithContentType *iface,
                                                              UINT64 position, IOutputStream **stream )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_GetOutputStreamAt, position, stream );
}
static HRESULT WINAPI in_memory_stream_wct_get_Position( IRandomAccessStreamWithContentType *iface, UINT64 *value )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_get_Position, value );
}
static HRESULT WINAPI in_memory_stream_wct_Seek( IRandomAccessStreamWithContentType *iface, UINT64 position )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_Seek, position );
}
static HRESULT WINAPI in_memory_stream_wct_CloneStream( IRandomAccessStreamWithContentType *iface,
                                                        IRandomAccessStream **stream )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_CloneStream, stream );
}
static HRESULT WINAPI in_memory_stream_wct_get_CanRead( IRandomAccessStreamWithContentType *iface, boolean *value )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_get_CanRead, value );
}
static HRESULT WINAPI in_memory_stream_wct_get_CanWrite( IRandomAccessStreamWithContentType *iface, boolean *value )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_get_CanWrite, value );
}
static HRESULT WINAPI in_memory_stream_wct_ReadAsync( IRandomAccessStreamWithContentType *iface, IBuffer *buffer,
                                                      IAsyncOperationWithProgress_IBuffer_UINT32 **operation )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_ReadAsync, buffer, operation );
}
static HRESULT WINAPI in_memory_stream_wct_ReadAsyncAt( IRandomAccessStreamWithContentType *iface,
                                                        IAsyncOperationWithProgress_IBuffer_UINT32 **operation,
                                                        IBuffer *buffer, UINT32 count )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_ReadAsyncAt, operation, buffer, count );
}
static HRESULT WINAPI in_memory_stream_wct_GetResults( IRandomAccessStreamWithContentType *iface, void **result )
{
    WCT_IMPL; return WCT_RAS( in_memory_stream_StreamGetResults, result );
}

static const void *in_memory_stream_wct_vtbl[] =
{
    in_memory_stream_wct_QueryInterface,  /* 0 */
    in_memory_stream_wct_AddRef,
    in_memory_stream_wct_Release,
    in_memory_stream_wct_GetIids,
    in_memory_stream_wct_GetRuntimeClassName,
    in_memory_stream_wct_GetTrustLevel,
    in_memory_stream_wct_get_Size,        /* 6 */
    in_memory_stream_wct_put_Size,
    in_memory_stream_wct_GetInputStreamAt,
    in_memory_stream_wct_GetOutputStreamAt,
    in_memory_stream_wct_get_Position,    /* 10 */
    in_memory_stream_wct_Seek,
    in_memory_stream_wct_CloneStream,
    in_memory_stream_wct_get_CanRead,
    in_memory_stream_wct_get_CanWrite,
    in_memory_stream_wct_get_ContentType, /* 15 */
    in_memory_stream_wct_QueryInterface,  /* 16-20 projection fillers */
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,  /* 21-29 projection fillers */
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_ReadAsync,       /* 30 ReadAsync(buffer) */
    in_memory_stream_wct_ReadAsyncAt,     /* 31 ReadAsyncAt(op**, buffer, count) */
    in_memory_stream_wct_GetResults,      /* 32 GetResults */
    in_memory_stream_wct_GetResults,      /* 33 */
    in_memory_stream_wct_GetResults,      /* 34 */
    in_memory_stream_wct_GetResults,      /* 35 */
    in_memory_stream_wct_GetResults,      /* 36 */
    in_memory_stream_wct_QueryInterface,  /* 37-39 */
    in_memory_stream_wct_QueryInterface,
    in_memory_stream_wct_QueryInterface,
};

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

    impl->IRandomAccessStream_iface.lpVtbl =
            (CONST_VTBL struct IRandomAccessStreamVtbl *)in_memory_stream_vtbl;
    impl->IRandomAccessStreamWithContentType_iface.lpVtbl =
            (CONST_VTBL struct IRandomAccessStreamWithContentTypeVtbl *)in_memory_stream_wct_vtbl;
    impl->IInputStream_iface.lpVtbl = &in_memory_stream_input_vtbl;
    impl->IOutputStream_iface.lpVtbl = &in_memory_stream_output_vtbl;
    impl->IContentTypeProvider_iface.lpVtbl = &in_memory_stream_content_type_vtbl;
    impl->IClosable_iface.lpVtbl = &in_memory_stream_closable_vtbl;
    impl->ref = 1;
    if (!(impl->content = content_create()))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }

    *out = &impl->IRandomAccessStream_iface;
    return S_OK;
}

static const WCHAR *content_type_for_path( const WCHAR *path )
{
    static const struct
    {
        const WCHAR *ext;
        const WCHAR *mime;
    } map[] =
    {
        { L".txt",  L"text/plain" },
        { L".json", L"application/json" },
        { L".xml",  L"application/xml" },
        { L".html", L"text/html" },
        { L".htm",  L"text/html" },
        { L".js",   L"application/javascript" },
        { L".css",  L"text/css" },
        { L".png",  L"image/png" },
        { L".jpg",  L"image/jpeg" },
        { L".jpeg", L"image/jpeg" },
        { L".gif",  L"image/gif" },
        { L".webp", L"image/webp" },
        { L".svg",  L"image/svg+xml" },
        { L".pdf",  L"application/pdf" },
    };
    const WCHAR *dot;
    size_t i;

    if (!path || !(dot = wcsrchr( path, '.' ))) return L"application/octet-stream";

    for (i = 0; i < ARRAY_SIZE(map); i++)
        if (!wcsicmp( dot, map[i].ext )) return map[i].mime;

    return L"application/octet-stream";
}

HRESULT in_memory_stream_create_from_file( const WCHAR *path, BOOL read_only, IRandomAccessStream **out )
{
    struct in_memory_stream *impl;
    BYTE *bytes = NULL;
    DWORD size = 0;
    HRESULT hr;

    if (FAILED(hr = in_memory_stream_create( out )))
        return hr;

    impl = impl_from_IRandomAccessStream( *out );
    if (FAILED(hr = read_file_bytes( path, &bytes, &size )))
    {
        IRandomAccessStream_Release( *out );
        *out = NULL;
        return hr;
    }

    impl->content->buffer = bytes;
    impl->content->size = size;
    impl->content->capacity = size;
    impl->position = 0;
    impl->read_only = read_only;
    if (path)
    {
        lstrcpynW( impl->content->backing_path, path, ARRAY_SIZE(impl->content->backing_path) );
        lstrcpynW( impl->content->content_type, content_type_for_path( path ), ARRAY_SIZE(impl->content->content_type) );
    }

    return S_OK;
}

/* InMemoryRandomAccessStream Factory */
struct in_memory_stream_factory_statics
{
    IActivationFactory IActivationFactory_iface;
    IRandomAccessStream IRandomAccessStream_iface;
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

    /* Windows aggregates the class default interface (IRandomAccessStream) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IRandomAccessStream ))
    {
        *out = &impl->IRandomAccessStream_iface;
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
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IRandomAccessStream,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI in_memory_stream_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.InMemoryRandomAccessStream", 50, class_name );
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


/* Windows aggregates the class default interface (IRandomAccessStream) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( ims_def, IRandomAccessStream, struct in_memory_stream_factory_statics, impl_from_IRandomAccessStream_ims_def,
                      IRandomAccessStream_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI ims_def_get_Size( IRandomAccessStream *iface, UINT64 *value ) { return E_FAIL; }
static HRESULT WINAPI ims_def_put_Size( IRandomAccessStream *iface, UINT64 value ) { return E_FAIL; }
static HRESULT WINAPI ims_def_GetInputStreamAt( IRandomAccessStream *iface, UINT64 position, IInputStream **stream ) { return E_FAIL; }
static HRESULT WINAPI ims_def_GetOutputStreamAt( IRandomAccessStream *iface, UINT64 position, IOutputStream **stream ) { return E_FAIL; }
static HRESULT WINAPI ims_def_get_Position( IRandomAccessStream *iface, UINT64 *value ) { return E_FAIL; }
static HRESULT WINAPI ims_def_Seek( IRandomAccessStream *iface, UINT64 position ) { return E_FAIL; }
static HRESULT WINAPI ims_def_CloneStream( IRandomAccessStream *iface, IRandomAccessStream **stream ) { return E_FAIL; }
static HRESULT WINAPI ims_def_get_CanRead( IRandomAccessStream *iface, boolean *value ) { return E_FAIL; }
static HRESULT WINAPI ims_def_get_CanWrite( IRandomAccessStream *iface, boolean *value ) { return E_FAIL; }

static const struct IRandomAccessStreamVtbl ims_def_v =
{
    ims_def_QueryInterface,
    ims_def_AddRef,
    ims_def_Release,
    ims_def_GetIids,
    ims_def_GetRuntimeClassName,
    ims_def_GetTrustLevel,
    ims_def_get_Size,
    ims_def_put_Size,
    ims_def_GetInputStreamAt,
    ims_def_GetOutputStreamAt,
    ims_def_get_Position,
    ims_def_Seek,
    ims_def_CloneStream,
    ims_def_get_CanRead,
    ims_def_get_CanWrite,
};

static struct in_memory_stream_factory_statics in_memory_stream_factory_obj =
{
    {&in_memory_stream_factory_vtbl},
    {&ims_def_v}, 1,
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
    struct buffer *scratch_buf;
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
        if (impl->scratch_buf)
            IBuffer_Release( &impl->scratch_buf->IBuffer_iface );
        DeleteCriticalSection( &impl->cs );
        free( impl->buffer );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI data_reader_GetIids( IDataReader *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IDataReader,
        IID_IClosable,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI data_reader_GetRuntimeClassName( IDataReader *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataReader", 34, class_name );
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
    if (impl->closed)
    {
        hr = RO_E_CLOSED;
    }
    else if (count > impl->length - impl->position)
    {
        hr = E_BOUNDS;
    }
    else
    {
        if (count) memcpy( dest, impl->buffer + impl->position, count );
        impl->position += count;
        if (impl->position == impl->length)
        {
            impl->position = 0;
            impl->length = 0;
        }
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
        char stack_utf8[256];
        WCHAR stack_wbuf[256];
        char *utf8 = (code_unit_count < sizeof(stack_utf8)) ? stack_utf8 : malloc( code_unit_count + 1 );
        int wlen;
        WCHAR *wbuf;

        if (!utf8) return E_OUTOFMEMORY;
        if (FAILED(hr = data_reader_read_raw( impl, utf8, code_unit_count )))
        {
            if (utf8 != stack_utf8) free( utf8 );
            return hr;
        }
        utf8[code_unit_count] = 0;
        wlen = MultiByteToWideChar( CP_UTF8, 0, utf8, code_unit_count, NULL, 0 );
        wbuf = (wlen < (int)ARRAY_SIZE(stack_wbuf)) ? stack_wbuf : malloc( (wlen + 1) * sizeof(WCHAR) );
        if (!wbuf)
        {
            if (utf8 != stack_utf8) free( utf8 );
            return E_OUTOFMEMORY;
        }
        MultiByteToWideChar( CP_UTF8, 0, utf8, code_unit_count, wbuf, wlen );
        wbuf[wlen] = 0;
        hr = WindowsCreateString( wbuf, wlen, value );
        if (wbuf != stack_wbuf) free( wbuf );
        if (utf8 != stack_utf8) free( utf8 );
        return hr;
    }
    else
    {
        WCHAR stack_wbuf[128];
        UINT32 bytes = code_unit_count * sizeof(WCHAR);
        WCHAR *wbuf = (code_unit_count < ARRAY_SIZE(stack_wbuf)) ? stack_wbuf : malloc( bytes + sizeof(WCHAR) );
        if (!wbuf) return E_OUTOFMEMORY;
        if (FAILED(hr = data_reader_read_raw( impl, wbuf, bytes )))
        {
            if (wbuf != stack_wbuf) free( wbuf );
            return hr;
        }
        wbuf[code_unit_count] = 0;
        hr = WindowsCreateString( wbuf, code_unit_count, value );
        if (wbuf != stack_wbuf) free( wbuf );
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

struct sync_wait_handler
{
    const void *lpVtbl;
    LONG ref;
    HANDLE event;
};

static HRESULT WINAPI sync_wait_handler_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        InterlockedIncrement( &((struct sync_wait_handler *)iface)->ref );
        return S_OK;
    }
    *out = iface;
    InterlockedIncrement( &((struct sync_wait_handler *)iface)->ref );
    return S_OK;
}

static ULONG WINAPI sync_wait_handler_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct sync_wait_handler *)iface)->ref );
}

static ULONG WINAPI sync_wait_handler_Release( void *iface )
{
    struct sync_wait_handler *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->event) CloseHandle( impl->event );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sync_wait_handler_Invoke( void *iface, void *sender, AsyncStatus status )
{
    struct sync_wait_handler *impl = iface;
    if (impl->event) SetEvent( impl->event );
    return S_OK;
}

static const void *sync_wait_handler_vtbl[] =
{
    sync_wait_handler_QueryInterface,
    sync_wait_handler_AddRef,
    sync_wait_handler_Release,
    sync_wait_handler_Invoke,
};

static BOOL async_op_is_started( IUnknown *op )
{
    IAsyncInfo *info = NULL;
    AsyncStatus status = Completed;

    if (SUCCEEDED(IUnknown_QueryInterface( op, &IID_IAsyncInfo, (void **)&info )))
    {
        IAsyncInfo_get_Status( info, &status );
        IAsyncInfo_Release( info );
    }
    return status == Started;
}

static HRESULT data_reader_ensure_space( struct data_reader *impl, UINT32 extra )
{
    if (impl->position == impl->length)
    {
        impl->position = 0;
        impl->length = 0;
    }
    else if (impl->position > 0 && extra > impl->capacity - impl->length)
    {
        UINT32 unconsumed = impl->length - impl->position;
        memmove( impl->buffer, impl->buffer + impl->position, unconsumed );
        impl->length = unconsumed;
        impl->position = 0;
    }

    if (extra > impl->capacity - impl->length)
    {
        UINT64 needed = (UINT64)impl->length + extra;
        UINT64 new_cap = max( (UINT64)impl->capacity * 2, needed );
        BYTE *new_buf;

        if (needed > UINT32_MAX) return E_OUTOFMEMORY;
        if (new_cap > UINT32_MAX) new_cap = UINT32_MAX;
        if (!(new_buf = realloc( impl->buffer, (SIZE_T)new_cap ))) return E_OUTOFMEMORY;
        impl->buffer = new_buf;
        impl->capacity = (UINT32)new_cap;
    }

    return S_OK;
}

static HRESULT WINAPI data_reader_LoadAsync( IDataReader *iface, UINT32 count,
                                            IAsyncOperation_UINT32 **operation )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    UINT32 loaded = 0;
    HRESULT hr = S_OK;

    TRACE( "iface %p, count %u, operation %p.\n", iface, count, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return RO_E_CLOSED;
    }

    if (impl->input_stream && count)
    {
        if (impl->input_stream->lpVtbl == &in_memory_stream_input_vtbl)
        {
            struct in_memory_stream *stream = impl_from_IInputStream( impl->input_stream );
            if (stream->closed)
            {
                LeaveCriticalSection( &impl->cs );
                return RO_E_CLOSED;
            }
            EnterCriticalSection( &stream->content->cs );
            if (stream->position < stream->content->size)
            {
                UINT64 avail = stream->content->size - stream->position;
                UINT32 to_read = (avail < count) ? (UINT32)avail : count;
                if (SUCCEEDED(hr = data_reader_ensure_space( impl, to_read )))
                {
                    memcpy( impl->buffer + impl->length, stream->content->buffer + stream->position, to_read );
                    stream->position += to_read;
                    impl->length += to_read;
                    loaded = to_read;
                }
            }
            LeaveCriticalSection( &stream->content->cs );
            if (FAILED(hr))
            {
                LeaveCriticalSection( &impl->cs );
                return hr;
            }
        }
        else
        {
            struct buffer *scratch = impl->scratch_buf;
            IAsyncOperationWithProgress_IBuffer_UINT32 *read_op = NULL;

            if (!scratch || scratch->ref > 1)
            {
                if (scratch) IBuffer_Release( &scratch->IBuffer_iface );
                impl->scratch_buf = NULL;
                if (FAILED(hr = buffer_create_raw( count, FALSE, &scratch )))
                {
                    LeaveCriticalSection( &impl->cs );
                    return hr;
                }
                impl->scratch_buf = scratch;
            }
            else if (FAILED(hr = buffer_ensure_capacity( scratch, count )))
            {
                LeaveCriticalSection( &impl->cs );
                return hr;
            }
            scratch->length = 0;

            if (SUCCEEDED(hr = IInputStream_ReadAsync( impl->input_stream, &scratch->IBuffer_iface, count,
                                                       impl->options, &read_op )) && read_op)
            {
                IBuffer *res_buf = NULL;
                IBufferByteAccess *access = NULL;
                BYTE *data = NULL;
                UINT32 rlen = 0;

                if (async_op_is_started( (IUnknown *)read_op ))
                {
                    struct sync_wait_handler *handler = calloc( 1, sizeof(*handler) );
                    if (handler)
                    {
                        handler->lpVtbl = sync_wait_handler_vtbl;
                        handler->ref = 1;
                        handler->event = CreateEventW( NULL, TRUE, FALSE, NULL );
                        if (handler->event &&
                            SUCCEEDED(IAsyncOperationWithProgress_IBuffer_UINT32_put_Completed(
                                    read_op, (__FIAsyncOperationWithProgressCompletedHandler_2_Windows__CStorage__CStreams__CIBuffer_UINT32 *)handler )))
                        {
                            WaitForSingleObject( handler->event, INFINITE );
                        }
                        sync_wait_handler_Release( handler );
                    }
                }
                if (FAILED(IAsyncOperationWithProgress_IBuffer_UINT32_GetResults( read_op, &res_buf )) || !res_buf)
                {
                    res_buf = &scratch->IBuffer_iface;
                    IBuffer_AddRef( res_buf );
                }

                IBuffer_get_Length( res_buf, &rlen );
                if (rlen && SUCCEEDED(IBuffer_QueryInterface( res_buf, &IID_IBufferByteAccess, (void **)&access )))
                {
                    if (SUCCEEDED(IBufferByteAccess_Buffer( access, &data )) && data)
                    {
                        if (SUCCEEDED(hr = data_reader_ensure_space( impl, rlen )))
                        {
                            memcpy( impl->buffer + impl->length, data, rlen );
                            impl->length += rlen;
                            loaded = rlen;
                        }
                    }
                    IBufferByteAccess_Release( access );
                }
                IBuffer_Release( res_buf );
                IAsyncOperationWithProgress_IBuffer_UINT32_Release( read_op );
                if (FAILED(hr))
                {
                    LeaveCriticalSection( &impl->cs );
                    return hr;
                }
            }
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
    *buffer = NULL;

    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return RO_E_CLOSED;
    }
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
    impl->position = 0;
    impl->length = 0;
    LeaveCriticalSection( &impl->cs );

    *buffer = buf;
    return S_OK;
}

static HRESULT WINAPI data_reader_DetachStream( IDataReader *iface, IInputStream **stream )
{
    struct data_reader *impl = impl_from_IDataReader( iface );
    if (!stream) return E_POINTER;
    *stream = NULL;
    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return RO_E_CLOSED;
    }
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
        IClosable *closable = NULL;
        if (SUCCEEDED(IInputStream_QueryInterface( impl->input_stream, &IID_IClosable, (void **)&closable )))
        {
            IClosable_Close( closable );
            IClosable_Release( closable );
        }
        IInputStream_Release( impl->input_stream );
        impl->input_stream = NULL;
    }
    if (impl->scratch_buf)
    {
        IBuffer_Release( &impl->scratch_buf->IBuffer_iface );
        impl->scratch_buf = NULL;
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
    IDataReader IDataReader_iface;
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

    /* Windows aggregates the class default interface (IDataReader) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IDataReader ))
    {
        *out = &impl->IDataReader_iface;
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
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IDataReaderFactory,
        IID_IDataReaderStatics,
        IID_IDataReader,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI data_reader_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataReader", 34, class_name );
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


/* Windows aggregates the class default interface (IDataReader) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( dr_def, IDataReader, struct data_reader_statics, impl_from_IDataReader_dr_def,
                      IDataReader_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI dr_def_get_UnconsumedBufferLength( IDataReader *iface, UINT32 *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_get_UnicodeEncoding( IDataReader *iface, UnicodeEncoding *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_put_UnicodeEncoding( IDataReader *iface, UnicodeEncoding value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_get_ByteOrder( IDataReader *iface, ByteOrder *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_put_ByteOrder( IDataReader *iface, ByteOrder value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_get_InputStreamOptions( IDataReader *iface, InputStreamOptions *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_put_InputStreamOptions( IDataReader *iface, InputStreamOptions value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadByte( IDataReader *iface, BYTE *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadBytes( IDataReader *iface, UINT32 value_size, BYTE *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadBuffer( IDataReader *iface, UINT32 length, IBuffer **buffer ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadBoolean( IDataReader *iface, boolean *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadGuid( IDataReader *iface, GUID *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadInt16( IDataReader *iface, INT16 *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadInt32( IDataReader *iface, INT32 *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadInt64( IDataReader *iface, INT64 *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadUInt16( IDataReader *iface, UINT16 *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadUInt32( IDataReader *iface, UINT32 *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadUInt64( IDataReader *iface, UINT64 *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadSingle( IDataReader *iface, FLOAT *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadDouble( IDataReader *iface, DOUBLE *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadString( IDataReader *iface, UINT32 code_unit_count, HSTRING *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadDateTime( IDataReader *iface, DateTime *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_ReadTimeSpan( IDataReader *iface, TimeSpan *value ) { return E_FAIL; }
static HRESULT WINAPI dr_def_LoadAsync( IDataReader *iface, UINT32 count, __FIAsyncOperation_1_UINT32 **operation ) { return E_FAIL; }
static HRESULT WINAPI dr_def_DetachBuffer( IDataReader *iface, IBuffer **buffer ) { return E_FAIL; }
static HRESULT WINAPI dr_def_DetachStream( IDataReader *iface, IInputStream **stream ) { return E_FAIL; }

static const struct IDataReaderVtbl dr_def_v =
{
    dr_def_QueryInterface,
    dr_def_AddRef,
    dr_def_Release,
    dr_def_GetIids,
    dr_def_GetRuntimeClassName,
    dr_def_GetTrustLevel,
    dr_def_get_UnconsumedBufferLength,
    dr_def_get_UnicodeEncoding,
    dr_def_put_UnicodeEncoding,
    dr_def_get_ByteOrder,
    dr_def_put_ByteOrder,
    dr_def_get_InputStreamOptions,
    dr_def_put_InputStreamOptions,
    dr_def_ReadByte,
    dr_def_ReadBytes,
    dr_def_ReadBuffer,
    dr_def_ReadBoolean,
    dr_def_ReadGuid,
    dr_def_ReadInt16,
    dr_def_ReadInt32,
    dr_def_ReadInt64,
    dr_def_ReadUInt16,
    dr_def_ReadUInt32,
    dr_def_ReadUInt64,
    dr_def_ReadSingle,
    dr_def_ReadDouble,
    dr_def_ReadString,
    dr_def_ReadDateTime,
    dr_def_ReadTimeSpan,
    dr_def_LoadAsync,
    dr_def_DetachBuffer,
    dr_def_DetachStream,
};

static struct data_reader_statics data_reader_statics_obj =
{
    {&data_reader_factory_vtbl},
    {&data_reader_factory_methods_vtbl},
    {&data_reader_statics_methods_vtbl},
    {&dr_def_v}, 1,
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
    struct buffer *scratch_buf;
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
        if (impl->scratch_buf)
            IBuffer_Release( &impl->scratch_buf->IBuffer_iface );
        DeleteCriticalSection( &impl->cs );
        free( impl->buffer );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI data_writer_GetIids( IDataWriter *iface, ULONG *iid_count, IID **iids )
{
    IID iid_array[] =
    {
        IID_IDataWriter,
        IID_IClosable,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI data_writer_GetRuntimeClassName( IDataWriter *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataWriter", 34, class_name );
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
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return RO_E_CLOSED;
    }
    if (count > UINT32_MAX - impl->length)
    {
        LeaveCriticalSection( &impl->cs );
        return E_OUTOFMEMORY;
    }
    if (impl->length + count > impl->capacity)
    {
        UINT64 needed = (UINT64)impl->length + count;
        UINT64 new_cap = max( (UINT64)impl->capacity * 2, needed + 64 );
        BYTE *new_buf;

        if (new_cap > UINT32_MAX) new_cap = UINT32_MAX;
        new_buf = realloc( impl->buffer, (SIZE_T)new_cap );
        if (!new_buf)
        {
            LeaveCriticalSection( &impl->cs );
            return E_OUTOFMEMORY;
        }
        impl->buffer = new_buf;
        impl->capacity = (UINT32)new_cap;
    }
    if (count) memcpy( impl->buffer + impl->length, src, count );
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
        char stack_utf8[256];
        int utf8_len = WideCharToMultiByte( CP_UTF8, 0, raw, len, NULL, 0, NULL, NULL );
        char *utf8 = (utf8_len <= (int)sizeof(stack_utf8)) ? stack_utf8 : malloc( utf8_len );
        if (!utf8) return E_OUTOFMEMORY;
        WideCharToMultiByte( CP_UTF8, 0, raw, len, utf8, utf8_len, NULL, NULL );
        hr = data_writer_write_raw( impl, utf8, utf8_len );
        if (utf8 != stack_utf8) free( utf8 );
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
    HRESULT hr = S_OK;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return RO_E_CLOSED;
    }
    stored = impl->length;
    if (impl->output_stream && impl->length)
    {
        if (impl->output_stream->lpVtbl == &in_memory_stream_output_vtbl)
        {
            struct in_memory_stream *stream = impl_from_IOutputStream( impl->output_stream );
            hr = in_memory_stream_write_raw( stream, impl->buffer, impl->length );
            if (FAILED(hr))
            {
                LeaveCriticalSection( &impl->cs );
                return hr;
            }
            impl->length = 0;
        }
        else
        {
            struct buffer *scratch = impl->scratch_buf;
            IAsyncOperationWithProgress_UINT32_UINT32 *write_op = NULL;

            if (!scratch || scratch->ref > 1)
            {
                if (scratch) IBuffer_Release( &scratch->IBuffer_iface );
                impl->scratch_buf = NULL;
                if (FAILED(hr = buffer_create_raw( impl->length, FALSE, &scratch )))
                {
                    LeaveCriticalSection( &impl->cs );
                    return hr;
                }
                impl->scratch_buf = scratch;
            }
            else if (FAILED(hr = buffer_ensure_capacity( scratch, impl->length )))
            {
                LeaveCriticalSection( &impl->cs );
                return hr;
            }

            memcpy( scratch->data, impl->buffer, impl->length );
            scratch->length = impl->length;

            if (SUCCEEDED(hr = IOutputStream_WriteAsync( impl->output_stream, &scratch->IBuffer_iface, &write_op )) && write_op)
                IAsyncOperationWithProgress_UINT32_UINT32_Release( write_op );
            if (FAILED(hr))
            {
                LeaveCriticalSection( &impl->cs );
                return hr;
            }
            impl->length = 0;
        }
    }
    LeaveCriticalSection( &impl->cs );

    return async_operation_uint32_create( L"Windows.Storage.Streams.DataWriterStoreOperation", stored,
                                          (IAsyncOperation_UINT32 **)operation );
}

static HRESULT WINAPI data_writer_FlushAsync( IDataWriter *iface, IAsyncOperation_boolean **operation )
{
    struct data_writer *impl = impl_from_IDataWriter( iface );

    if (!operation) return E_POINTER;
    *operation = NULL;
    if (impl->closed) return RO_E_CLOSED;

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
    *buffer = NULL;

    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return RO_E_CLOSED;
    }
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
    *output_stream = NULL;
    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return RO_E_CLOSED;
    }
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
        IClosable *closable = NULL;
        if (SUCCEEDED(IOutputStream_QueryInterface( impl->output_stream, &IID_IClosable, (void **)&closable )))
        {
            IClosable_Close( closable );
            IClosable_Release( closable );
        }
        IOutputStream_Release( impl->output_stream );
        impl->output_stream = NULL;
    }
    if (impl->scratch_buf)
    {
        IBuffer_Release( &impl->scratch_buf->IBuffer_iface );
        impl->scratch_buf = NULL;
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
    IDataWriter IDataWriter_iface;
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

    /* Windows aggregates the class default interface (IDataWriter) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IDataWriter ))
    {
        *out = &impl->IDataWriter_iface;
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
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IDataWriterFactory,
        IID_IDataWriter,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI data_writer_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.DataWriter", 34, class_name );
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


/* Windows aggregates the class default interface (IDataWriter) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( dw_def, IDataWriter, struct data_writer_factory_statics, impl_from_IDataWriter_dw_def,
                      IDataWriter_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI dw_def_get_UnstoredBufferLength( IDataWriter *iface, UINT32 *value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_get_UnicodeEncoding( IDataWriter *iface, UnicodeEncoding *value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_put_UnicodeEncoding( IDataWriter *iface, UnicodeEncoding value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_get_ByteOrder( IDataWriter *iface, ByteOrder *value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_put_ByteOrder( IDataWriter *iface, ByteOrder value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteByte( IDataWriter *iface, BYTE value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteBytes( IDataWriter *iface, UINT32 value_size, BYTE *value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteBuffer( IDataWriter *iface, IBuffer *buffer ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteBufferRange( IDataWriter *iface, IBuffer *buffer, UINT32 start, UINT32 count ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteBoolean( IDataWriter *iface, boolean value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteGuid( IDataWriter *iface, GUID value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteInt16( IDataWriter *iface, INT16 value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteInt32( IDataWriter *iface, INT32 value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteInt64( IDataWriter *iface, INT64 value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteUInt16( IDataWriter *iface, UINT16 value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteUInt32( IDataWriter *iface, UINT32 value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteUInt64( IDataWriter *iface, UINT64 value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteSingle( IDataWriter *iface, FLOAT value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteDouble( IDataWriter *iface, DOUBLE value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteDateTime( IDataWriter *iface, DateTime value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteTimeSpan( IDataWriter *iface, TimeSpan value ) { return E_FAIL; }
static HRESULT WINAPI dw_def_WriteString( IDataWriter *iface, HSTRING value, UINT32 *code_unit_count ) { return E_FAIL; }
static HRESULT WINAPI dw_def_MeasureString( IDataWriter *iface, HSTRING value, UINT32 *code_unit_count ) { return E_FAIL; }
static HRESULT WINAPI dw_def_StoreAsync( IDataWriter *iface, __FIAsyncOperation_1_UINT32 **operation ) { return E_FAIL; }
static HRESULT WINAPI dw_def_FlushAsync( IDataWriter *iface, __FIAsyncOperation_1_boolean **operation ) { return E_FAIL; }
static HRESULT WINAPI dw_def_DetachBuffer( IDataWriter *iface, IBuffer **buffer ) { return E_FAIL; }
static HRESULT WINAPI dw_def_DetachStream( IDataWriter *iface, IOutputStream **output_stream ) { return E_FAIL; }

static const struct IDataWriterVtbl dw_def_v =
{
    dw_def_QueryInterface,
    dw_def_AddRef,
    dw_def_Release,
    dw_def_GetIids,
    dw_def_GetRuntimeClassName,
    dw_def_GetTrustLevel,
    dw_def_get_UnstoredBufferLength,
    dw_def_get_UnicodeEncoding,
    dw_def_put_UnicodeEncoding,
    dw_def_get_ByteOrder,
    dw_def_put_ByteOrder,
    dw_def_WriteByte,
    dw_def_WriteBytes,
    dw_def_WriteBuffer,
    dw_def_WriteBufferRange,
    dw_def_WriteBoolean,
    dw_def_WriteGuid,
    dw_def_WriteInt16,
    dw_def_WriteInt32,
    dw_def_WriteInt64,
    dw_def_WriteUInt16,
    dw_def_WriteUInt32,
    dw_def_WriteUInt64,
    dw_def_WriteSingle,
    dw_def_WriteDouble,
    dw_def_WriteDateTime,
    dw_def_WriteTimeSpan,
    dw_def_WriteString,
    dw_def_MeasureString,
    dw_def_StoreAsync,
    dw_def_FlushAsync,
    dw_def_DetachBuffer,
    dw_def_DetachStream,
};

static struct data_writer_factory_statics data_writer_factory_obj =
{
    {&data_writer_factory_vtbl},
    {&data_writer_factory_methods_vtbl},
    {&dw_def_v}, 1,
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
    IID iid_array[] =
    {
        IID_IRandomAccessStreamReference,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI random_access_stream_reference_GetRuntimeClassName( IRandomAccessStreamReference *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.RandomAccessStreamReference", 51, class_name );
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
    IRandomAccessStreamWithContentType *wct = NULL;
    IInspectable *result;
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

    if (SUCCEEDED(IRandomAccessStream_QueryInterface( cloned, &IID_IRandomAccessStreamWithContentType, (void **)&wct )))
        result = (IInspectable *)wct;
    else
        result = (IInspectable *)cloned;

    hr = async_operation_inspectable_create( &IID_IAsyncOperation_IRandomAccessStreamWithContentType,
                                            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IRandomAccessStreamWithContentType>",
                                            result, (void **)operation );
    if (wct) IRandomAccessStreamWithContentType_Release( wct );
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
    IRandomAccessStreamReference IRandomAccessStreamReference_iface;
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

    /* Windows aggregates the class default interface (IRandomAccessStreamReference) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IRandomAccessStreamReference ))
    {
        *out = &impl->IRandomAccessStreamReference_iface;
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
    IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IRandomAccessStreamReferenceStatics,
        IID_IRandomAccessStreamReference,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI stream_ref_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Storage.Streams.RandomAccessStreamReference", 51, class_name );
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
    hr = in_memory_stream_create_from_file( raw_path, TRUE, &stream );
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


/* Windows aggregates the class default interface (IRandomAccessStreamReference) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( sr_def, IRandomAccessStreamReference, struct random_access_stream_reference_statics, impl_from_IRandomAccessStreamReference_sr_def,
                      IRandomAccessStreamReference_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI sr_def_OpenReadAsync( IRandomAccessStreamReference *iface, __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType **operation ) { return E_FAIL; }

static const struct IRandomAccessStreamReferenceVtbl sr_def_v =
{
    sr_def_QueryInterface,
    sr_def_AddRef,
    sr_def_Release,
    sr_def_GetIids,
    sr_def_GetRuntimeClassName,
    sr_def_GetTrustLevel,
    sr_def_OpenReadAsync,
};

static struct random_access_stream_reference_statics stream_ref_statics_obj =
{
    {&stream_ref_factory_vtbl},
    {&stream_ref_statics_vtbl},
    {&sr_def_v}, 1,
};

IActivationFactory *random_access_stream_reference_factory = &stream_ref_statics_obj.IActivationFactory_iface;
