/*
 * WebP decoder implementation for Wine
 *
 * Copyright 2026 Wine Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "ntstatus.h"

#include "wincodecs_private.h"
#include "unixlib.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wincodecs);

struct webp_decoder
{
    struct decoder decoder;
    IStream *stream;
    struct decoder_frame decoder_frame;
    UINT stride;
    BYTE *image_bits;
};

static inline struct webp_decoder *impl_from_decoder( struct decoder *iface )
{
    return CONTAINING_RECORD( iface, struct webp_decoder, decoder );
}

static HRESULT CDECL webp_decoder_initialize( struct decoder *iface, IStream *stream, struct decoder_stat *st )
{
    struct webp_decoder *This = impl_from_decoder( iface );
    ULONGLONG size64;
    ULONG bytes_read;
    BYTE *raw_data;
    UINT data_size;
    HRESULT hr;
    struct webp_get_info_params info_params;
    struct webp_decode_params decode_params;
    NTSTATUS status;
    UINT width = 0, height = 0, frame_count = 1;
    BOOL has_alpha = FALSE, has_animation = FALSE;

    hr = stream_getsize( stream, &size64 );
    if (FAILED(hr)) return hr;
    if (size64 < 12 || size64 > 0x7fffffff) return WINCODEC_ERR_BADIMAGE;
    data_size = (UINT)size64;

    raw_data = malloc( data_size );
    if (!raw_data) return E_OUTOFMEMORY;

    hr = stream_seek( stream, 0, STREAM_SEEK_SET, NULL );
    if (FAILED(hr)) { free( raw_data ); return hr; }

    hr = stream_read( stream, raw_data, data_size, &bytes_read );
    if (FAILED(hr) || bytes_read != data_size) { free( raw_data ); return hr; }

    memset( &info_params, 0, sizeof(info_params) );
    info_params.data = raw_data;
    info_params.data_size = data_size;
    info_params.width = &width;
    info_params.height = &height;
    info_params.has_alpha = &has_alpha;
    info_params.has_animation = &has_animation;
    info_params.frame_count = &frame_count;

    status = WINE_UNIX_CALL( unix_webp_get_info, &info_params );
    if (status != STATUS_SUCCESS || !width || !height)
    {
        free( raw_data );
        return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
    }

    This->decoder_frame.width = width;
    This->decoder_frame.height = height;
    This->decoder_frame.bpp = 32;
    This->decoder_frame.pixel_format = GUID_WICPixelFormat32bppBGRA;
    This->decoder_frame.dpix = 96.0;
    This->decoder_frame.dpiy = 96.0;
    This->decoder_frame.num_color_contexts = 0;
    This->decoder_frame.num_colors = 0;

    This->stride = width * 4;
    This->image_bits = malloc( This->stride * height );
    if (!This->image_bits)
    {
        free( raw_data );
        return E_OUTOFMEMORY;
    }

    memset( &decode_params, 0, sizeof(decode_params) );
    decode_params.data = raw_data;
    decode_params.data_size = data_size;
    decode_params.output = This->image_bits;
    decode_params.output_size = This->stride * height;
    decode_params.stride = This->stride;

    status = WINE_UNIX_CALL( unix_webp_decode, &decode_params );
    free( raw_data );
    if (status != STATUS_SUCCESS)
    {
        free( This->image_bits );
        This->image_bits = NULL;
        return E_FAIL;
    }

    This->stream = stream;
    IStream_AddRef( stream );

    st->flags = 0;
    st->frame_count = frame_count;
    return S_OK;
}

static HRESULT CDECL webp_decoder_get_frame_info( struct decoder *iface, UINT frame, struct decoder_frame *info )
{
    struct webp_decoder *This = impl_from_decoder( iface );
    *info = This->decoder_frame;
    return S_OK;
}

static HRESULT CDECL webp_decoder_get_decoder_palette( struct decoder *iface, UINT frame, WICColor *colors, UINT *num_colors )
{
    return WINCODEC_ERR_PALETTEUNAVAILABLE;
}

static HRESULT CDECL webp_decoder_copy_pixels( struct decoder *iface, UINT frame,
    const WICRect *prc, UINT stride, UINT buffersize, BYTE *buffer )
{
    struct webp_decoder *This = impl_from_decoder( iface );
    return copy_pixels( This->decoder_frame.bpp, This->image_bits,
                        This->decoder_frame.width, This->decoder_frame.height, This->stride,
                        prc, stride, buffersize, buffer );
}

static HRESULT CDECL webp_decoder_get_metadata_blocks( struct decoder *iface, UINT frame,
    UINT *count, struct decoder_block **blocks )
{
    *count = 0;
    *blocks = NULL;
    return S_OK;
}

static HRESULT CDECL webp_decoder_get_color_context( struct decoder *iface, UINT frame, UINT num,
    BYTE **data, DWORD *datasize )
{
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static void CDECL webp_decoder_destroy( struct decoder *iface )
{
    struct webp_decoder *This = impl_from_decoder( iface );
    if (This->stream) IStream_Release( This->stream );
    free( This->image_bits );
    free( This );
}

static const struct decoder_funcs webp_decoder_vtable =
{
    webp_decoder_initialize,
    webp_decoder_get_frame_info,
    webp_decoder_get_decoder_palette,
    webp_decoder_copy_pixels,
    webp_decoder_get_metadata_blocks,
    webp_decoder_get_color_context,
    webp_decoder_destroy
};

HRESULT CDECL webp_decoder_create( struct decoder_info *info, struct decoder **result )
{
    struct webp_decoder *This;

    This = malloc( sizeof(*This) );
    if (!This) return E_OUTOFMEMORY;

    This->decoder.vtable = &webp_decoder_vtable;
    This->stream = NULL;
    This->image_bits = NULL;
    *result = &This->decoder;

    info->container_format = GUID_ContainerFormatWebp;
    info->block_format = GUID_ContainerFormatWebp;
    info->clsid = CLSID_WICWebpDecoder;

    return S_OK;
}

HRESULT WebpDecoder_CreateInstance( REFIID iid, void **ppv )
{
    HRESULT hr;
    struct decoder *decoder;
    struct decoder_info decoder_info;

    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (__wine_init_unix_call()) return E_FAIL;

    hr = webp_decoder_create( &decoder_info, &decoder );
    if (SUCCEEDED(hr))
        hr = CommonDecoder_CreateInstance( decoder, &decoder_info, iid, ppv );

    return hr;
}
