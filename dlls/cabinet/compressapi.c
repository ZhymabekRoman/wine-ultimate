/*
 * Compression API
 *
 * Copyright 2026 Chris Kadar
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

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "compressapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(cabinet);

struct compression_ctx
{
    DWORD algorithm;
    COMPRESS_ALLOCATION_ROUTINES routines;
    BOOL has_routines;
};

static void ctx_free( struct compression_ctx *ctx, void *ptr )
{
    if (!ptr) return;
    if (ctx && ctx->has_routines && ctx->routines.Free)
        ctx->routines.Free( ctx->routines.UserContext, ptr );
    else
        free( ptr );
}

/***********************************************************************
 *		CreateCompressor (CABINET.30)
 */
BOOL WINAPI CreateCompressor( DWORD algorithm, COMPRESS_ALLOCATION_ROUTINES *routines, COMPRESSOR_HANDLE *handle )
{
    struct compression_ctx *ctx;

    FIXME("algo (%lu) stub\n", algorithm);
    if (!handle) { SetLastError( ERROR_INVALID_PARAMETER ); return FALSE; }
    ctx = calloc( 1, sizeof(*ctx) );
    if (!ctx) { SetLastError( ERROR_OUTOFMEMORY ); return FALSE; }
    ctx->algorithm = algorithm;
    if (routines) { ctx->routines = *routines; ctx->has_routines = TRUE; }
    *handle = (COMPRESSOR_HANDLE)ctx;
    return TRUE;
}

/***********************************************************************
 *		Compress (CABINET.33)
 */
BOOL WINAPI Compress( COMPRESSOR_HANDLE handle, const VOID *data, SIZE_T data_size,
                      VOID *buffer, SIZE_T buffer_size, SIZE_T *compressed_data_size )
{
    FIXME("stub\n");

    *compressed_data_size = data_size;

    if (buffer_size < data_size)
    {
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        return FALSE;
    }
    memcpy( buffer, data, data_size );
    return TRUE;
}

/***********************************************************************
 *		CloseCompressor  (CABINET.35)
 */
BOOL WINAPI CloseCompressor( COMPRESSOR_HANDLE handle )
{
    free( handle );
    return TRUE;
}

/***********************************************************************
 *		CreateDecompressor (CABINET.40)
 */
BOOL WINAPI CreateDecompressor( DWORD algorithm, COMPRESS_ALLOCATION_ROUTINES *routines, DECOMPRESSOR_HANDLE *handle )
{
    struct compression_ctx *ctx;
    DWORD base_algo = algorithm & 0x0fffffff;

    TRACE("algo %#lx, routines %p, handle %p.\n", algorithm, routines, handle);

    if (!handle)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    if (base_algo < COMPRESS_ALGORITHM_MSZIP || base_algo > COMPRESS_ALGORITHM_LZMS)
    {
        SetLastError( ERROR_NOT_SUPPORTED );
        return FALSE;
    }

    if (routines && routines->Allocate)
        ctx = routines->Allocate( routines->UserContext, sizeof(*ctx) );
    else
        ctx = malloc( sizeof(*ctx) );
    if (!ctx)
    {
        SetLastError( ERROR_OUTOFMEMORY );
        return FALSE;
    }
    memset( ctx, 0, sizeof(*ctx) );
    ctx->algorithm = algorithm;
    if (routines)
    {
        ctx->routines = *routines;
        ctx->has_routines = TRUE;
    }
    *handle = (DECOMPRESSOR_HANDLE)ctx;
    return TRUE;
}

static void *mszip_zalloc( void *opaque, uInt items, uInt size )
{
    return calloc( items, size );
}

static void mszip_zfree( void *opaque, void *address )
{
    free( address );
}

static BOOL decompress_mszip( const BYTE *payload, SIZE_T payload_len, BYTE *out, SIZE_T out_size )
{
    z_stream zs;
    SIZE_T in_off = 0, out_off = 0;
    const BYTE *prev_block = NULL;
    SIZE_T prev_len = 0;

    while (out_off < out_size && in_off < payload_len)
    {
        SIZE_T chunk_out = out_size - out_off;
        DWORD chunk_len;
        const BYTE *chunk_ptr;
        SIZE_T avail;
        int ret;

        if (chunk_out > 32768) chunk_out = 32768;

        if (payload_len - in_off < 4) { WARN("short chunk hdr\n"); return FALSE; }
        chunk_len = *(const DWORD *)(payload + in_off);
        in_off += 4;
        if (chunk_len > payload_len - in_off) { WARN("chunk_len %lu > rem %Iu\n", chunk_len, payload_len - in_off); return FALSE; }

        chunk_ptr = payload + in_off;
        avail = chunk_len;
        in_off += chunk_len;

        /* Skip MSZIP 'CK' signature at block start. */
        if (avail >= 2 && chunk_ptr[0] == 'C' && chunk_ptr[1] == 'K')
        {
            chunk_ptr += 2;
            avail -= 2;
        }

        memset( &zs, 0, sizeof(zs) );
        zs.zalloc = mszip_zalloc;
        zs.zfree = mszip_zfree;
        if ((ret = inflateInit2( &zs, -MAX_WBITS )) != Z_OK)
        {
            WARN("inflateInit2 failed: %d\n", ret);
            return FALSE;
        }

        if (prev_block && prev_len)
            inflateSetDictionary( &zs, prev_block, (uInt)prev_len );

        zs.next_in = (Bytef *)chunk_ptr;
        zs.avail_in = (uInt)avail;
        zs.next_out = out + out_off;
        zs.avail_out = (uInt)chunk_out;

        ret = inflate( &zs, Z_FINISH );
        if (ret != Z_STREAM_END && !(ret == Z_OK && zs.avail_out == 0))
        {
            WARN("inflate failed: ret %d (%s) avail_in=%u avail_out=%u\n", ret, zs.msg ? zs.msg : "", zs.avail_in, zs.avail_out);
            inflateEnd( &zs );
            return FALSE;
        }

        prev_block = out + out_off;
        prev_len = chunk_out - zs.avail_out;
        out_off += prev_len;
        inflateEnd( &zs );
    }

    if (out_off != out_size)
        WARN("out_off %Iu != out_size %Iu\n", out_off, out_size);
    return out_off == out_size;
}

/***********************************************************************
 *		Decompress (CABINET.43)
 */
BOOL WINAPI Decompress( DECOMPRESSOR_HANDLE handle, const VOID *data, SIZE_T data_size,
                        VOID *buffer, SIZE_T buffer_size, SIZE_T *decompressed_data_size )
{
    static const BYTE magic[4] = { 0x0a, 0x51, 0xe5, 0xc0 };
    struct compression_ctx *ctx = (struct compression_ctx *)handle;
    const BYTE *b = data;
    WORD hdr_size;
    ULONGLONG uncomp_size;
    DWORD base_algo;

    TRACE("handle %p, data %p, data_size %Iu, buffer %p, buffer_size %Iu, out_size %p.\n",
          handle, data, data_size, buffer, buffer_size, decompressed_data_size);

    if (!ctx || !data || !decompressed_data_size)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }

    base_algo = ctx->algorithm & 0x0fffffff;
    if (base_algo != COMPRESS_ALGORITHM_MSZIP)
    {
        FIXME("unsupported algorithm %lu\n", ctx->algorithm);
        SetLastError( ERROR_NOT_SUPPORTED );
        return FALSE;
    }

    if (data_size < 24 || memcmp( b, magic, sizeof(magic) ))
    {
        SetLastError( ERROR_BAD_FORMAT );
        return FALSE;
    }

    hdr_size = *(const WORD *)(b + 4);
    uncomp_size = *(const ULONGLONG *)(b + 8);
    if (hdr_size < 16 || hdr_size > data_size)
    {
        SetLastError( ERROR_BAD_FORMAT );
        return FALSE;
    }

    *decompressed_data_size = (SIZE_T)uncomp_size;
    if (!buffer || buffer_size < uncomp_size)
    {
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        return FALSE;
    }

    if (!decompress_mszip( b + hdr_size, data_size - hdr_size, buffer, (SIZE_T)uncomp_size ))
    {
        SetLastError( ERROR_BAD_FORMAT );
        return FALSE;
    }

    return TRUE;
}

/***********************************************************************
 *		CloseDecompressor  (CABINET.45)
 */
BOOL WINAPI CloseDecompressor( DECOMPRESSOR_HANDLE handle )
{
    struct compression_ctx *ctx = (struct compression_ctx *)handle;
    if (!ctx) return TRUE;
    ctx_free( ctx, ctx );
    return TRUE;
}
