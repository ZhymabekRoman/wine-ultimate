/* WinRT Windows.Media.Editing Implementation
 *
 * Copyright (C) 2026 Wine Developers
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
#include "objbase.h"
#include "robuffer.h"
#include "wine/debug.h"

#ifndef AsyncStatus_Completed
#define AsyncStatus_Completed Completed
#endif

WINE_DEFAULT_DEBUG_CHANNEL(media);

static const GUID guid_IMediaCompositionStatics = {0x87a08f04, 0xe32a, 0x45ce, {0x8f, 0x66, 0xa3, 0x0d, 0xf0, 0x76, 0x62, 0x24}};
static const GUID guid_IMediaComposition        = {0x2e06e605, 0xdc71, 0x41d6, {0xb8, 0x37, 0x2d, 0xb1, 0x4a, 0x29, 0x47}};
static const GUID guid_IMediaComposition2       = {0xa59e5372, 0x2366, 0x492c, {0xbe, 0xc8, 0xe6, 0xdf, 0xba, 0x6d, 0x02, 0x81}};
static const GUID guid_IMediaClipStatics        = {0xfa402b68, 0x928f, 0x43c4, {0xbc, 0x6e, 0x78, 0x3a, 0x1a, 0x35, 0x96, 0x56}};
static const GUID guid_IMediaClip               = {0x53f25366, 0x5fba, 0x3ea4, {0x86, 0x93, 0x24, 0x76, 0x18, 0x11, 0x14, 0x0a}};
static const GUID guid_IVector_MediaClip        = {0x3197feb8, 0x95e9, 0x3649, {0x8e, 0xcd, 0x87, 0x81, 0x7e, 0x62, 0x88, 0x87}};
static const GUID guid_IIterable_MediaClip      = {0xd82d8d58, 0xe60d, 0x3dd3, {0x83, 0xb1, 0x4f, 0x93, 0x06, 0x75, 0x08, 0x0d}};
static const GUID guid_IAsyncOperation_MediaClip= {0xef90faea, 0x21a9, 0x36fa, {0xaf, 0x79, 0xd5, 0xfe, 0x59, 0x15, 0xfe, 0xb7}};
static const GUID guid_IAsyncOperation_ImageStream = {0xde31c9e8, 0xa5ae, 0x39fb, {0x97, 0x50, 0x05, 0xe5, 0x4d, 0x9d, 0x26, 0x66}};
static const GUID guid_IBufferByteAccess        = {0x905a0fef, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};

#pragma pack(push, 1)
struct bmp_image
{
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    BYTE pixels[16 * 16 * 4];
};
#pragma pack(pop)

static void init_bmp_image( struct bmp_image *bmp )
{
    memset( bmp, 0, sizeof(*bmp) );
    bmp->bfh.bfType = 0x4D42; /* 'BM' */
    bmp->bfh.bfSize = sizeof(*bmp);
    bmp->bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmp->bih.biSize = sizeof(BITMAPINFOHEADER);
    bmp->bih.biWidth = 16;
    bmp->bih.biHeight = 16;
    bmp->bih.biPlanes = 1;
    bmp->bih.biBitCount = 32;
    bmp->bih.biCompression = BI_RGB;
    bmp->bih.biSizeImage = sizeof(bmp->pixels);
    /* Fill with opaque neutral gray so decoding doesn't produce an empty transparent buffer */
    memset( bmp->pixels, 0x80, sizeof(bmp->pixels) );
}

/* --- image_stream --- */

struct image_stream
{
    const void *vtbl_ras;
    const void *vtbl_stream;
    const void *vtbl_async_info;
    LONG ref;
    struct bmp_image bmp;
    UINT64 pos;
};

/* --- read_buffer_async_op --- */

struct read_buffer_async_op
{
    const void *vtbl_op;
    const void *vtbl_info;
    LONG ref;
    void *buffer;
    void *completed_handler;
};

static HRESULT WINAPI read_buffer_op_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationWithProgress_IBuffer_UINT32 ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (void *)((BYTE *)iface + sizeof(void *));
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI read_buffer_op_AddRef( void *iface ) { return 2; }
static ULONG WINAPI read_buffer_op_Release( void *iface ) { return 1; }
static HRESULT WINAPI read_buffer_op_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI read_buffer_op_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Storage.Streams.IBuffer, UInt32>", 87, n );
}
static HRESULT WINAPI read_buffer_op_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI read_buffer_op_put_Progress( void *iface, void *h ) { return S_OK; }
static HRESULT WINAPI read_buffer_op_get_Progress( void *iface, void **h ) { if (h) *h = NULL; return S_OK; }
static HRESULT WINAPI read_buffer_op_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, AsyncStatus_Completed );
    }
    return S_OK;
}
static HRESULT WINAPI read_buffer_op_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI read_buffer_op_GetResults( void *iface, void **results )
{
    struct read_buffer_async_op *impl = iface;
    if (results) *results = impl->buffer;
    return S_OK;
}

static const void *read_buffer_op_vtbl[] =
{
    read_buffer_op_QueryInterface,
    read_buffer_op_AddRef,
    read_buffer_op_Release,
    read_buffer_op_GetIids,
    read_buffer_op_GetRuntimeClassName,
    read_buffer_op_GetTrustLevel,
    read_buffer_op_put_Progress,
    read_buffer_op_get_Progress,
    read_buffer_op_put_Completed,
    read_buffer_op_get_Completed,
    read_buffer_op_GetResults,
};

static HRESULT WINAPI read_buffer_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *op = (void *)((BYTE *)iface - sizeof(void *));
    return read_buffer_op_QueryInterface( op, iid, out );
}
static ULONG WINAPI read_buffer_info_AddRef( void *iface ) { return 2; }
static ULONG WINAPI read_buffer_info_Release( void *iface ) { return 1; }
static HRESULT WINAPI read_buffer_info_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI read_buffer_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, n );
}
static HRESULT WINAPI read_buffer_info_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI read_buffer_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI read_buffer_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = AsyncStatus_Completed; return S_OK; }
static HRESULT WINAPI read_buffer_info_get_ErrorCode( void *iface, HRESULT *ec ) { if (ec) *ec = S_OK; return S_OK; }
static HRESULT WINAPI read_buffer_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI read_buffer_info_Close( void *iface ) { return S_OK; }

static const void *read_buffer_info_vtbl[] =
{
    read_buffer_info_QueryInterface,
    read_buffer_info_AddRef,
    read_buffer_info_Release,
    read_buffer_info_GetIids,
    read_buffer_info_GetRuntimeClassName,
    read_buffer_info_GetTrustLevel,
    read_buffer_info_get_Id,
    read_buffer_info_get_Status,
    read_buffer_info_get_ErrorCode,
    read_buffer_info_Cancel,
    read_buffer_info_Close,
};

static HRESULT create_read_buffer_async_op( void *buffer, void **out )
{
    struct read_buffer_async_op *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_op = read_buffer_op_vtbl;
    impl->vtbl_info = read_buffer_info_vtbl;
    impl->ref = 1;
    impl->buffer = buffer;
    *out = impl;
    return S_OK;
}

static HRESULT WINAPI image_stream_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct image_stream *impl = iface;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStreamWithContentType ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStream ) ||
        IsEqualGUID( iid, &IID_IInputStream ) ||
        IsEqualGUID( iid, &IID_IOutputStream ) ||
        IsEqualGUID( iid, &IID_IContentTypeProvider ) ||
        IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_ras;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IStream ) || IsEqualGUID( iid, &IID_ISequentialStream ))
    {
        *out = &impl->vtbl_stream;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "image_stream %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI image_stream_AddRef( void *iface )
{
    struct image_stream *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI image_stream_Release( void *iface )
{
    struct image_stream *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI image_stream_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI image_stream_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Graphics.Imaging.ImageStream", 35, n );
}
static HRESULT WINAPI image_stream_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI image_stream_get_Size( void *iface, UINT64 *value )
{
    if (value) *value = sizeof(struct bmp_image);
    return S_OK;
}
static HRESULT WINAPI image_stream_put_Size( void *iface, UINT64 value ) { return S_OK; }

static HRESULT WINAPI image_stream_GetInputStreamAt( void *iface, UINT64 position, void **stream )
{
    if (stream)
    {
        *stream = iface;
        image_stream_AddRef( iface );
    }
    return S_OK;
}

static HRESULT WINAPI image_stream_GetOutputStreamAt( void *iface, UINT64 position, void **stream )
{
    if (stream)
    {
        *stream = iface;
        image_stream_AddRef( iface );
    }
    return S_OK;
}

static HRESULT WINAPI image_stream_get_Position( void *iface, UINT64 *value )
{
    struct image_stream *impl = iface;
    if (value) *value = impl->pos;
    return S_OK;
}

static HRESULT WINAPI image_stream_Seek( void *iface, UINT64 position )
{
    struct image_stream *impl = iface;
    impl->pos = min( position, sizeof(struct bmp_image) );
    return S_OK;
}

static HRESULT create_image_stream( void **out );

static HRESULT WINAPI image_stream_CloneStream( void *iface, void **stream )
{
    return create_image_stream( stream );
}

static HRESULT WINAPI image_stream_get_CanRead( void *iface, boolean *value )
{
    if (value) *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI image_stream_get_CanWrite( void *iface, boolean *value )
{
    if (value) *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI image_stream_Close( void *iface ) { return S_OK; }

static HRESULT WINAPI image_stream_ReadAsync( void *iface, void *buffer, UINT32 count, UINT32 options, void **op )
{
    struct image_stream *impl = iface;
    IBufferByteAccess *byte_access;
    BYTE *dst = NULL;
    UINT32 to_read;

    TRACE( "iface %p, buffer %p, count %u, options %u, op %p\n", iface, buffer, count, options, op );

    to_read = min( count, (UINT32)(sizeof(struct bmp_image) - impl->pos) );
    if (buffer && SUCCEEDED( IUnknown_QueryInterface( (IUnknown *)buffer, &guid_IBufferByteAccess, (void **)&byte_access ) ))
    {
        if (SUCCEEDED( IBufferByteAccess_Buffer( byte_access, &dst ) ) && dst && to_read)
        {
            memcpy( dst, (BYTE *)&impl->bmp + impl->pos, to_read );
            impl->pos += to_read;
        }
        IBufferByteAccess_Release( byte_access );
        IBuffer_put_Length( (IBuffer *)buffer, to_read );
    }

    return create_read_buffer_async_op( buffer, op );
}

static HRESULT WINAPI image_stream_WriteAsync( void *iface, void *buffer, void **op ) { return E_NOTIMPL; }
static HRESULT WINAPI image_stream_FlushAsync( void *iface, void **op ) { return S_OK; }

static HRESULT WINAPI image_stream_get_ContentType( void *iface, HSTRING *value )
{
    return WindowsCreateString( L"image/bmp", 9, value );
}

static const void *image_stream_ras_vtbl[] =
{
    image_stream_QueryInterface,
    image_stream_AddRef,
    image_stream_Release,
    image_stream_GetIids,
    image_stream_GetRuntimeClassName,
    image_stream_GetTrustLevel,
    image_stream_Close,
    image_stream_ReadAsync,
    image_stream_WriteAsync,
    image_stream_FlushAsync,
    image_stream_get_ContentType,
    image_stream_get_Size,
    image_stream_put_Size,
    image_stream_GetInputStreamAt,
    image_stream_GetOutputStreamAt,
    image_stream_get_Position,
    image_stream_Seek,
    image_stream_CloneStream,
    image_stream_get_CanRead,
    image_stream_get_CanWrite,
};

static inline struct image_stream *impl_from_IStream( void *iface )
{
    return (struct image_stream *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI stream_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct image_stream *impl = impl_from_IStream( iface );
    return image_stream_QueryInterface( impl, iid, out );
}
static ULONG WINAPI stream_AddRef( void *iface )
{
    struct image_stream *impl = impl_from_IStream( iface );
    return image_stream_AddRef( impl );
}
static ULONG WINAPI stream_Release( void *iface )
{
    struct image_stream *impl = impl_from_IStream( iface );
    return image_stream_Release( impl );
}

static HRESULT WINAPI stream_Read( void *iface, void *pv, ULONG cb, ULONG *read_bytes )
{
    struct image_stream *impl = impl_from_IStream( iface );
    ULONG to_read = min( cb, (ULONG)(sizeof(struct bmp_image) - impl->pos) );
    if (to_read && pv)
    {
        memcpy( pv, (BYTE *)&impl->bmp + impl->pos, to_read );
        impl->pos += to_read;
    }
    if (read_bytes) *read_bytes = to_read;
    return S_OK;
}

static HRESULT WINAPI stream_Write( void *iface, const void *pv, ULONG cb, ULONG *written )
{
    if (written) *written = 0;
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Seek( void *iface, LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition )
{
    struct image_stream *impl = impl_from_IStream( iface );
    LONGLONG new_pos = 0;

    switch (dwOrigin)
    {
    case STREAM_SEEK_SET: new_pos = dlibMove.QuadPart; break;
    case STREAM_SEEK_CUR: new_pos = impl->pos + dlibMove.QuadPart; break;
    case STREAM_SEEK_END: new_pos = sizeof(struct bmp_image) + dlibMove.QuadPart; break;
    }
    if (new_pos < 0) new_pos = 0;
    impl->pos = min( (UINT64)new_pos, sizeof(struct bmp_image) );
    if (plibNewPosition) plibNewPosition->QuadPart = impl->pos;
    return S_OK;
}

static HRESULT WINAPI stream_SetSize( void *iface, ULARGE_INTEGER libNewSize ) { return S_OK; }
static HRESULT WINAPI stream_CopyTo( void *iface, IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_Commit( void *iface, DWORD grfCommitFlags ) { return S_OK; }
static HRESULT WINAPI stream_Revert( void *iface ) { return S_OK; }
static HRESULT WINAPI stream_LockRegion( void *iface, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType ) { return S_OK; }
static HRESULT WINAPI stream_UnlockRegion( void *iface, ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType ) { return S_OK; }
static HRESULT WINAPI stream_Stat( void *iface, STATSTG *pstatstg, DWORD grfStatFlag )
{
    if (!pstatstg) return E_POINTER;
    memset( pstatstg, 0, sizeof(*pstatstg) );
    pstatstg->type = STGTY_STREAM;
    pstatstg->cbSize.QuadPart = sizeof(struct bmp_image);
    return S_OK;
}
static HRESULT WINAPI stream_Clone( void *iface, IStream **ppstm )
{
    return create_image_stream( (void **)ppstm );
}

static const void *image_stream_stream_vtbl[] =
{
    stream_QueryInterface,
    stream_AddRef,
    stream_Release,
    stream_Read,
    stream_Write,
    stream_Seek,
    stream_SetSize,
    stream_CopyTo,
    stream_Commit,
    stream_Revert,
    stream_LockRegion,
    stream_UnlockRegion,
    stream_Stat,
    stream_Clone,
};

static HRESULT create_image_stream( void **out )
{
    struct image_stream *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_ras = image_stream_ras_vtbl;
    impl->vtbl_stream = image_stream_stream_vtbl;
    impl->ref = 1;
    init_bmp_image( &impl->bmp );
    *out = impl;
    return S_OK;
}

/* --- thumbnail_async_op (IAsyncOperation<ImageStream*>) --- */

struct thumbnail_async_op
{
    const void *vtbl_op;
    const void *vtbl_info;
    LONG ref;
    void *stream;
};

static HRESULT WINAPI thumb_op_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IAsyncOperation_ImageStream ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (void *)((BYTE *)iface + sizeof(void *));
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI thumb_op_AddRef( void *iface ) { return 2; }
static ULONG WINAPI thumb_op_Release( void *iface ) { return 1; }
static HRESULT WINAPI thumb_op_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI thumb_op_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Windows.Graphics.Imaging.ImageStream>", 69, n );
}
static HRESULT WINAPI thumb_op_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI thumb_op_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, AsyncStatus_Completed );
    }
    return S_OK;
}
static HRESULT WINAPI thumb_op_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI thumb_op_GetResults( void *iface, void **results )
{
    struct thumbnail_async_op *impl = iface;
    if (results) *results = impl->stream;
    return S_OK;
}

static const void *thumb_op_vtbl[] =
{
    thumb_op_QueryInterface,
    thumb_op_AddRef,
    thumb_op_Release,
    thumb_op_GetIids,
    thumb_op_GetRuntimeClassName,
    thumb_op_GetTrustLevel,
    thumb_op_put_Completed,
    thumb_op_get_Completed,
    thumb_op_GetResults,
};

static HRESULT WINAPI thumb_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *op = (void *)((BYTE *)iface - sizeof(void *));
    return thumb_op_QueryInterface( op, iid, out );
}
static ULONG WINAPI thumb_info_AddRef( void *iface ) { return 2; }
static ULONG WINAPI thumb_info_Release( void *iface ) { return 1; }
static HRESULT WINAPI thumb_info_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI thumb_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, n );
}
static HRESULT WINAPI thumb_info_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI thumb_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI thumb_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = AsyncStatus_Completed; return S_OK; }
static HRESULT WINAPI thumb_info_get_ErrorCode( void *iface, HRESULT *ec ) { if (ec) *ec = S_OK; return S_OK; }
static HRESULT WINAPI thumb_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI thumb_info_Close( void *iface ) { return S_OK; }

static const void *thumb_info_vtbl[] =
{
    thumb_info_QueryInterface,
    thumb_info_AddRef,
    thumb_info_Release,
    thumb_info_GetIids,
    thumb_info_GetRuntimeClassName,
    thumb_info_GetTrustLevel,
    thumb_info_get_Id,
    thumb_info_get_Status,
    thumb_info_get_ErrorCode,
    thumb_info_Cancel,
    thumb_info_Close,
};

static HRESULT create_thumbnail_async_op( void **out )
{
    struct thumbnail_async_op *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_op = thumb_op_vtbl;
    impl->vtbl_info = thumb_info_vtbl;
    impl->ref = 1;
    create_image_stream( &impl->stream );
    *out = impl;
    return S_OK;
}

/* --- MediaClip --- */

struct media_clip
{
    const void *vtbl;
    LONG ref;
    IStorageFile *file;
};

static HRESULT WINAPI clip_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct media_clip *impl = iface;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IMediaClip ))
    {
        *out = impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "clip %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI clip_AddRef( void *iface )
{
    struct media_clip *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI clip_Release( void *iface )
{
    struct media_clip *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->file) IStorageFile_Release( impl->file );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI clip_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI clip_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Media.Editing.MediaClip", 31, n );
}
static HRESULT WINAPI clip_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI clip_Clone( void *iface, void **result ) { if (result) *result = iface; clip_AddRef( iface ); return S_OK; }
static HRESULT WINAPI clip_GetVideoEncodingProperties( void *iface, void **result ) { if (result) *result = NULL; return S_OK; }
static HRESULT WINAPI clip_get_AudioEffectDefinitions( void *iface, void **result ) { if (result) *result = NULL; return S_OK; }
static HRESULT WINAPI clip_get_EmbeddedAudioTracks( void *iface, void **result ) { if (result) *result = NULL; return S_OK; }
static HRESULT WINAPI clip_get_EndTimeInComposition( void *iface, TimeSpan *result ) { if (result) result->Duration = 0; return S_OK; }
static HRESULT WINAPI clip_get_OriginalDuration( void *iface, TimeSpan *result ) { if (result) result->Duration = 0; return S_OK; }
static HRESULT WINAPI clip_get_SelectedEmbeddedAudioTrackIndex( void *iface, UINT32 *result ) { if (result) *result = 0; return S_OK; }
static HRESULT WINAPI clip_set_SelectedEmbeddedAudioTrackIndex( void *iface, UINT32 value ) { return S_OK; }
static HRESULT WINAPI clip_get_StartTimeInComposition( void *iface, TimeSpan *result ) { if (result) result->Duration = 0; return S_OK; }
static HRESULT WINAPI clip_get_TrimTimeFromEnd( void *iface, TimeSpan *result ) { if (result) result->Duration = 0; return S_OK; }
static HRESULT WINAPI clip_set_TrimTimeFromEnd( void *iface, TimeSpan value ) { return S_OK; }
static HRESULT WINAPI clip_get_TrimTimeFromStart( void *iface, TimeSpan *result ) { if (result) result->Duration = 0; return S_OK; }
static HRESULT WINAPI clip_set_TrimTimeFromStart( void *iface, TimeSpan value ) { return S_OK; }
static HRESULT WINAPI clip_get_TrimmedDuration( void *iface, TimeSpan *result ) { if (result) result->Duration = 0; return S_OK; }
static HRESULT WINAPI clip_get_UserData( void *iface, void **result ) { if (result) *result = NULL; return S_OK; }
static HRESULT WINAPI clip_get_VideoEffectDefinitions( void *iface, void **result ) { if (result) *result = NULL; return S_OK; }
static HRESULT WINAPI clip_get_Volume( void *iface, DOUBLE *result ) { if (result) *result = 1.0; return S_OK; }
static HRESULT WINAPI clip_set_Volume( void *iface, DOUBLE value ) { return S_OK; }

static const void *media_clip_vtbl[] =
{
    clip_QueryInterface,
    clip_AddRef,
    clip_Release,
    clip_GetIids,
    clip_GetRuntimeClassName,
    clip_GetTrustLevel,
    clip_Clone,
    clip_GetVideoEncodingProperties,
    clip_get_AudioEffectDefinitions,
    clip_get_EmbeddedAudioTracks,
    clip_get_EndTimeInComposition,
    clip_get_OriginalDuration,
    clip_get_SelectedEmbeddedAudioTrackIndex,
    clip_set_SelectedEmbeddedAudioTrackIndex,
    clip_get_StartTimeInComposition,
    clip_get_TrimTimeFromEnd,
    clip_set_TrimTimeFromEnd,
    clip_get_TrimTimeFromStart,
    clip_set_TrimTimeFromStart,
    clip_get_TrimmedDuration,
    clip_get_UserData,
    clip_get_VideoEffectDefinitions,
    clip_get_Volume,
    clip_set_Volume,
};

static HRESULT create_media_clip( IStorageFile *file, void **out )
{
    struct media_clip *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl = media_clip_vtbl;
    impl->ref = 1;
    impl->file = file;
    if (file) IStorageFile_AddRef( file );
    *out = impl;
    return S_OK;
}

/* --- clip_async_op (IAsyncOperation<MediaClip*>) --- */

struct clip_async_op
{
    const void *vtbl_op;
    const void *vtbl_info;
    LONG ref;
    void *clip;
};

static HRESULT WINAPI clip_op_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IAsyncOperation_MediaClip ))
    {
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (void *)((BYTE *)iface + sizeof(void *));
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI clip_op_AddRef( void *iface ) { return 2; }
static ULONG WINAPI clip_op_Release( void *iface ) { return 1; }
static HRESULT WINAPI clip_op_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI clip_op_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Windows.Media.Editing.MediaClip>", 66, n );
}
static HRESULT WINAPI clip_op_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI clip_op_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        void **vtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)(void *, void *, UINT32) = (void *)vtbl[3];
        if (invoke) invoke( handler, iface, AsyncStatus_Completed );
    }
    return S_OK;
}
static HRESULT WINAPI clip_op_get_Completed( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }
static HRESULT WINAPI clip_op_GetResults( void *iface, void **results )
{
    struct clip_async_op *impl = iface;
    if (results) *results = impl->clip;
    return S_OK;
}

static const void *clip_op_vtbl[] =
{
    clip_op_QueryInterface,
    clip_op_AddRef,
    clip_op_Release,
    clip_op_GetIids,
    clip_op_GetRuntimeClassName,
    clip_op_GetTrustLevel,
    clip_op_put_Completed,
    clip_op_get_Completed,
    clip_op_GetResults,
};

static HRESULT WINAPI clip_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    void *op = (void *)((BYTE *)iface - sizeof(void *));
    return clip_op_QueryInterface( op, iid, out );
}
static ULONG WINAPI clip_info_AddRef( void *iface ) { return 2; }
static ULONG WINAPI clip_info_Release( void *iface ) { return 1; }
static HRESULT WINAPI clip_info_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI clip_info_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, n );
}
static HRESULT WINAPI clip_info_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI clip_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI clip_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = AsyncStatus_Completed; return S_OK; }
static HRESULT WINAPI clip_info_get_ErrorCode( void *iface, HRESULT *ec ) { if (ec) *ec = S_OK; return S_OK; }
static HRESULT WINAPI clip_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI clip_info_Close( void *iface ) { return S_OK; }

static const void *clip_info_vtbl[] =
{
    clip_info_QueryInterface,
    clip_info_AddRef,
    clip_info_Release,
    clip_info_GetIids,
    clip_info_GetRuntimeClassName,
    clip_info_GetTrustLevel,
    clip_info_get_Id,
    clip_info_get_Status,
    clip_info_get_ErrorCode,
    clip_info_Cancel,
    clip_info_Close,
};

static HRESULT create_clip_async_op( IStorageFile *file, void **out )
{
    struct clip_async_op *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_op = clip_op_vtbl;
    impl->vtbl_info = clip_info_vtbl;
    impl->ref = 1;
    create_media_clip( file, &impl->clip );
    *out = impl;
    return S_OK;
}

/* --- MediaClip Statics & Factory --- */

static HRESULT WINAPI clip_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &guid_IMediaClipStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI clip_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI clip_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI clip_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI clip_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Media.Editing.MediaClip", 31, n );
}
static HRESULT WINAPI clip_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI clip_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_media_clip( NULL, (void **)instance );
}

static HRESULT WINAPI clip_statics_CreateFromColor( void *iface, struct { BYTE A, R, G, B; } color, TimeSpan duration, void **result )
{
    return create_media_clip( NULL, result );
}

static HRESULT WINAPI clip_statics_CreateFromFileAsync( void *iface, IStorageFile *file, void **operation )
{
    TRACE( "iface %p, file %p, operation %p\n", iface, file, operation );
    return create_clip_async_op( file, operation );
}

static HRESULT WINAPI clip_statics_CreateFromImageFileAsync( void *iface, IStorageFile *file, TimeSpan duration, void **operation )
{
    TRACE( "iface %p, file %p, operation %p\n", iface, file, operation );
    return create_clip_async_op( file, operation );
}

static const void *media_clip_factory_vtbl[] =
{
    clip_factory_QueryInterface,
    clip_factory_AddRef,
    clip_factory_Release,
    clip_factory_GetIids,
    clip_factory_GetRuntimeClassName,
    clip_factory_GetTrustLevel,
    clip_factory_ActivateInstance,
    clip_statics_CreateFromColor,
    clip_statics_CreateFromFileAsync,
    clip_statics_CreateFromImageFileAsync,
};

static const void *media_clip_factory_inst = media_clip_factory_vtbl;
IActivationFactory *media_clip_factory = (IActivationFactory *)&media_clip_factory_inst;

/* --- media_clip_vector (IVector<MediaClip*>) --- */

struct media_clip_vector
{
    const void *vtbl;
    LONG ref;
    void **items;
    UINT32 count;
    UINT32 capacity;
};

static HRESULT WINAPI vector_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct media_clip_vector *impl = iface;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IVector_MediaClip ) ||
        IsEqualGUID( iid, &guid_IIterable_MediaClip ))
    {
        *out = impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "vector %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vector_AddRef( void *iface )
{
    struct media_clip_vector *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI vector_Release( void *iface )
{
    struct media_clip_vector *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; i++)
            if (impl->items[i]) IUnknown_Release( (IUnknown *)impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI vector_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI vector_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVector`1<Windows.Media.Editing.MediaClip>", 71, n );
}
static HRESULT WINAPI vector_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI vector_GetAt( void *iface, UINT32 index, void **item )
{
    struct media_clip_vector *impl = iface;
    if (index >= impl->count) return E_BOUNDS;
    if (item)
    {
        *item = impl->items[index];
        if (*item) IUnknown_AddRef( (IUnknown *)*item );
    }
    return S_OK;
}

static HRESULT WINAPI vector_get_Size( void *iface, UINT32 *size )
{
    struct media_clip_vector *impl = iface;
    if (size) *size = impl->count;
    return S_OK;
}

static HRESULT WINAPI vector_GetView( void *iface, void **view )
{
    if (view)
    {
        *view = iface;
        vector_AddRef( iface );
    }
    return S_OK;
}

static HRESULT WINAPI vector_IndexOf( void *iface, void *item, UINT32 *index, boolean *found )
{
    struct media_clip_vector *impl = iface;
    UINT32 i;
    for (i = 0; i < impl->count; i++)
    {
        if (impl->items[i] == item)
        {
            if (index) *index = i;
            if (found) *found = TRUE;
            return S_OK;
        }
    }
    if (index) *index = 0;
    if (found) *found = FALSE;
    return S_OK;
}

static HRESULT WINAPI vector_SetAt( void *iface, UINT32 index, void *item )
{
    struct media_clip_vector *impl = iface;
    if (index >= impl->count) return E_BOUNDS;
    if (impl->items[index]) IUnknown_Release( (IUnknown *)impl->items[index] );
    impl->items[index] = item;
    if (item) IUnknown_AddRef( (IUnknown *)item );
    return S_OK;
}

static HRESULT WINAPI vector_InsertAt( void *iface, UINT32 index, void *item )
{
    struct media_clip_vector *impl = iface;
    if (index > impl->count) return E_BOUNDS;
    if (impl->count >= impl->capacity)
    {
        UINT32 cap = max( 8, impl->capacity * 2 );
        void **new_items = realloc( impl->items, cap * sizeof(void *) );
        if (!new_items) return E_OUTOFMEMORY;
        impl->items = new_items;
        impl->capacity = cap;
    }
    memmove( &impl->items[index + 1], &impl->items[index], (impl->count - index) * sizeof(void *) );
    impl->items[index] = item;
    if (item) IUnknown_AddRef( (IUnknown *)item );
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI vector_RemoveAt( void *iface, UINT32 index )
{
    struct media_clip_vector *impl = iface;
    if (index >= impl->count) return E_BOUNDS;
    if (impl->items[index]) IUnknown_Release( (IUnknown *)impl->items[index] );
    memmove( &impl->items[index], &impl->items[index + 1], (impl->count - index - 1) * sizeof(void *) );
    impl->count--;
    return S_OK;
}

static HRESULT WINAPI vector_Append( void *iface, void *item )
{
    struct media_clip_vector *impl = iface;
    return vector_InsertAt( iface, impl->count, item );
}

static HRESULT WINAPI vector_RemoveAtEnd( void *iface )
{
    struct media_clip_vector *impl = iface;
    if (!impl->count) return E_BOUNDS;
    return vector_RemoveAt( iface, impl->count - 1 );
}

static HRESULT WINAPI vector_Clear( void *iface )
{
    struct media_clip_vector *impl = iface;
    UINT32 i;
    for (i = 0; i < impl->count; i++)
        if (impl->items[i]) IUnknown_Release( (IUnknown *)impl->items[i] );
    impl->count = 0;
    return S_OK;
}

static HRESULT WINAPI vector_GetMany( void *iface, UINT32 start, UINT32 capacity, void **items, UINT32 *actual )
{
    struct media_clip_vector *impl = iface;
    UINT32 i, to_copy;
    if (start >= impl->count) { if (actual) *actual = 0; return S_OK; }
    to_copy = min( capacity, impl->count - start );
    for (i = 0; i < to_copy; i++)
    {
        items[i] = impl->items[start + i];
        if (items[i]) IUnknown_AddRef( (IUnknown *)items[i] );
    }
    if (actual) *actual = to_copy;
    return S_OK;
}

static HRESULT WINAPI vector_ReplaceAll( void *iface, UINT32 count, void **items )
{
    vector_Clear( iface );
    while (count--) vector_Append( iface, *items++ );
    return S_OK;
}

static HRESULT WINAPI vector_First( void *iface, void **iterator )
{
    FIXME( "iterator stub\n" );
    if (iterator) *iterator = NULL;
    return S_OK;
}

static const void *media_clip_vector_vtbl[] =
{
    vector_QueryInterface,
    vector_AddRef,
    vector_Release,
    vector_GetIids,
    vector_GetRuntimeClassName,
    vector_GetTrustLevel,
    vector_GetAt,
    vector_get_Size,
    vector_GetView,
    vector_IndexOf,
    vector_SetAt,
    vector_InsertAt,
    vector_RemoveAt,
    vector_Append,
    vector_RemoveAtEnd,
    vector_Clear,
    vector_GetMany,
    vector_ReplaceAll,
    vector_First,
};

static HRESULT create_media_clip_vector( void **out )
{
    struct media_clip_vector *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl = media_clip_vector_vtbl;
    impl->ref = 1;
    *out = impl;
    return S_OK;
}

/* --- MediaComposition --- */

struct media_composition
{
    const void *vtbl_comp;
    const void *vtbl_comp2;
    LONG ref;
    void *clips;
};

static inline struct media_composition *impl_from_IMediaComposition2( void *iface )
{
    return (struct media_composition *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI comp_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct media_composition *impl = iface;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &guid_IMediaComposition ))
    {
        *out = &impl->vtbl_comp;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &guid_IMediaComposition2 ))
    {
        *out = &impl->vtbl_comp2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "composition %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI comp_AddRef( void *iface )
{
    struct media_composition *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI comp_Release( void *iface )
{
    struct media_composition *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->clips) IUnknown_Release( (IUnknown *)impl->clips );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI comp_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI comp_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Media.Editing.MediaComposition", 38, n );
}
static HRESULT WINAPI comp_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI comp_Clone( void *iface, void **result ) { if (result) *result = iface; comp_AddRef( iface ); return S_OK; }
static HRESULT WINAPI comp_SaveAsync( void *iface, IStorageFile *file, void **op ) { return E_NOTIMPL; }
static HRESULT WINAPI comp_GetThumbnailAsync( void *iface, TimeSpan timeFromStart, INT32 scaledWidth, INT32 scaledHeight, UINT32 framePrecision, void **operation )
{
    TRACE( "iface %p, time %I64d, w %d, h %d, prec %u, op %p\n", iface, timeFromStart.Duration, scaledWidth, scaledHeight, framePrecision, operation );
    return create_thumbnail_async_op( operation );
}
static HRESULT WINAPI comp_GetThumbnailsAsync( void *iface, void *times, INT32 scaledWidth, INT32 scaledHeight, UINT32 framePrecision, void **operation )
{
    return create_thumbnail_async_op( operation );
}
static HRESULT WINAPI comp_RenderToFileAsync( void *iface, IStorageFile *destination, void **op ) { return S_OK; }
static HRESULT WINAPI comp_RenderToFileAsync2( void *iface, IStorageFile *destination, UINT32 trimmingPreference, void **op ) { return S_OK; }
static HRESULT WINAPI comp_RenderToFileAsync3( void *iface, IStorageFile *destination, UINT32 trimmingPreference, void *encodingProfile, void **op ) { return S_OK; }
static HRESULT WINAPI comp_CreateDefaultEncodingProfile( void *iface, void **profile ) { if (profile) *profile = NULL; return S_OK; }
static HRESULT WINAPI comp_GenerateMediaStreamSource( void *iface, void **source ) { if (source) *source = NULL; return S_OK; }
static HRESULT WINAPI comp_GenerateMediaStreamSource2( void *iface, void *encodingProfile, void **source ) { if (source) *source = NULL; return S_OK; }
static HRESULT WINAPI comp_GeneratePreviewMediaStreamSource( void *iface, INT32 scaledWidth, INT32 scaledHeight, void **source ) { if (source) *source = NULL; return S_OK; }
static HRESULT WINAPI comp_get_BackgroundAudioTracks( void *iface, void **value ) { if (value) *value = NULL; return S_OK; }

static HRESULT WINAPI comp_get_Clips( void *iface, void **value )
{
    struct media_composition *impl = iface;
    TRACE( "iface %p, value %p\n", iface, value );
    if (value)
    {
        *value = impl->clips;
        if (*value) IUnknown_AddRef( (IUnknown *)*value );
    }
    return S_OK;
}

static HRESULT WINAPI comp_get_Duration( void *iface, TimeSpan *value ) { if (value) value->Duration = 0; return S_OK; }
static HRESULT WINAPI comp_get_UserData( void *iface, void **value ) { if (value) *value = NULL; return S_OK; }

static const void *media_composition_vtbl[] =
{
    comp_QueryInterface,
    comp_AddRef,
    comp_Release,
    comp_GetIids,
    comp_GetRuntimeClassName,
    comp_GetTrustLevel,
    comp_Clone,
    comp_SaveAsync,
    comp_GetThumbnailAsync,
    comp_GetThumbnailsAsync,
    comp_RenderToFileAsync,
    comp_RenderToFileAsync2,
    comp_RenderToFileAsync3,
    comp_CreateDefaultEncodingProfile,
    comp_GenerateMediaStreamSource,
    comp_GenerateMediaStreamSource2,
    comp_GeneratePreviewMediaStreamSource,
    comp_get_BackgroundAudioTracks,
    comp_get_Clips,
    comp_get_Duration,
    comp_get_UserData,
};

static HRESULT WINAPI comp2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct media_composition *impl = impl_from_IMediaComposition2( iface );
    return comp_QueryInterface( impl, iid, out );
}
static ULONG WINAPI comp2_AddRef( void *iface )
{
    struct media_composition *impl = impl_from_IMediaComposition2( iface );
    return comp_AddRef( impl );
}
static ULONG WINAPI comp2_Release( void *iface )
{
    struct media_composition *impl = impl_from_IMediaComposition2( iface );
    return comp_Release( impl );
}
static HRESULT WINAPI comp2_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI comp2_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Media.Editing.MediaComposition", 38, n );
}
static HRESULT WINAPI comp2_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI comp2_get_OverlayLayers( void *iface, void **value ) { if (value) *value = NULL; return S_OK; }

static const void *media_composition2_vtbl[] =
{
    comp2_QueryInterface,
    comp2_AddRef,
    comp2_Release,
    comp2_GetIids,
    comp2_GetRuntimeClassName,
    comp2_GetTrustLevel,
    comp2_get_OverlayLayers,
};

static HRESULT create_media_composition( void **out )
{
    struct media_composition *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_comp = media_composition_vtbl;
    impl->vtbl_comp2 = media_composition2_vtbl;
    impl->ref = 1;
    create_media_clip_vector( &impl->clips );
    *out = impl;
    return S_OK;
}

/* --- MediaComposition Statics & Factory --- */

static HRESULT WINAPI comp_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &guid_IMediaCompositionStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI comp_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI comp_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI comp_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI comp_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.Media.Editing.MediaComposition", 38, n );
}
static HRESULT WINAPI comp_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI comp_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_media_composition( (void **)instance );
}

static HRESULT WINAPI comp_statics_LoadAsync( void *iface, IStorageFile *file, void **op )
{
    return E_NOTIMPL;
}

static const void *media_composition_factory_vtbl[] =
{
    comp_factory_QueryInterface,
    comp_factory_AddRef,
    comp_factory_Release,
    comp_factory_GetIids,
    comp_factory_GetRuntimeClassName,
    comp_factory_GetTrustLevel,
    comp_factory_ActivateInstance,
    comp_statics_LoadAsync,
};

static const void *media_composition_factory_inst = media_composition_factory_vtbl;
IActivationFactory *media_composition_factory = (IActivationFactory *)&media_composition_factory_inst;
