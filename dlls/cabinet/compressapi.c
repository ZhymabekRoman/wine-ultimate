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

#ifndef COMPRESS_RAW
#define COMPRESS_RAW 0x20000000
#endif

#define MSZIP_BLOCK_SIZE 32768

struct compression_ctx
{
    DWORD algorithm;
    COMPRESS_ALLOCATION_ROUTINES routines;
    BOOL has_routines;
};

static void *ctx_alloc( struct compression_ctx *ctx, SIZE_T size )
{
    if (ctx && ctx->has_routines && ctx->routines.Allocate)
        return ctx->routines.Allocate( ctx->routines.UserContext, size );
    return malloc( size );
}

static void ctx_free( struct compression_ctx *ctx, void *ptr )
{
    if (!ptr) return;
    if (ctx && ctx->has_routines && ctx->routines.Free)
        ctx->routines.Free( ctx->routines.UserContext, ptr );
    else
        free( ptr );
}

static void *ctx_zalloc( void *opaque, uInt items, uInt size )
{
    struct compression_ctx *ctx = (struct compression_ctx *)opaque;

    if (items && size > SIZE_MAX / items) return NULL;

    return ctx_alloc( ctx, (SIZE_T)items * size );
}

static void ctx_zfree( void *opaque, void *address )
{
    struct compression_ctx *ctx = (struct compression_ctx *)opaque;

    ctx_free( ctx, address );
}

/***********************************************************************
 *		CreateCompressor (CABINET.30)
 */
BOOL WINAPI CreateCompressor( DWORD algorithm, COMPRESS_ALLOCATION_ROUTINES *routines, COMPRESSOR_HANDLE *handle )
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
    if (routines) { ctx->routines = *routines; ctx->has_routines = TRUE; }
    *handle = (COMPRESSOR_HANDLE)ctx;
    return TRUE;
}

static BOOL mszip_compress( struct compression_ctx *ctx, const BYTE *src, SIZE_T src_size,
                            BYTE **dst, SIZE_T *dst_size )
{
    SIZE_T nchunks = src_size / MSZIP_BLOCK_SIZE + 1;
    SIZE_T capacity = 16 + src_size + (src_size >> 3) + nchunks * 512 + 4;
    BYTE *out;
    SIZE_T pos = 0;
    int ret;

    out = malloc( capacity );
    if (!out) { WARN("no mem for %Iu\n", capacity ); return FALSE; }

    if (ctx && (ctx->algorithm & COMPRESS_RAW))
    {
        /* Raw stream: DWORD uncompressed size, then 'CK' deflate blocks. */
        *(DWORD *)(out + pos) = (DWORD)src_size;
        pos += 4;
    }
    else
    {
        /* Container header: magic, header size, uncompressed size, reserved. */
        out[pos + 0] = 0x0a; out[pos + 1] = 0x51; out[pos + 2] = 0xe5; out[pos + 3] = 0xc0;
        *(WORD *)(out + 4) = 16;
        *(ULONGLONG *)(out + 8) = src_size;
        memset( out + 14, 0, 2 );
        pos = 16;
    }

    while (src_size)
    {
        SIZE_T chunk_in = src_size > MSZIP_BLOCK_SIZE ? MSZIP_BLOCK_SIZE : src_size;
        DWORD *chunk_len = (DWORD *)(out + pos);
        z_stream zs;
        uLong bound;

        memset( &zs, 0, sizeof(zs) );
        zs.zalloc = ctx_zalloc;
        zs.zfree = ctx_zfree;
        zs.opaque = ctx;
        if ((ret = deflateInit2( &zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY )) != Z_OK)
        {
            WARN("deflateInit2 failed: ret %d, msg %s\n", ret, zs.msg ? zs.msg : "none" );
            free( out );
            return FALSE;
        }
        bound = deflateBound( &zs, (uInt)chunk_in );

        out[pos + 4] = 'C';
        out[pos + 5] = 'K';
        zs.next_in = (Bytef *)src;
        zs.avail_in = (uInt)chunk_in;
        zs.next_out = out + pos + 6;
        zs.avail_out = bound;

        if (deflate( &zs, Z_FINISH ) != Z_STREAM_END)
        {
            WARN("deflate failed: ret %d (%s) avail_in=%u avail_out=%u bound=%lu\n",
                 deflate( &zs, Z_NO_FLUSH ), zs.msg ? zs.msg : "", zs.avail_in, zs.avail_out, bound );
            deflateEnd( &zs );
            free( out );
            return FALSE;
        }
        TRACE("chunk in %Iu out %lu len %lu\n", chunk_in, zs.total_out, (SIZE_T)(2 + zs.total_out));
        *chunk_len = (DWORD)(2 + zs.total_out);
        pos += 6 + zs.total_out;

        deflateEnd( &zs );
        src += chunk_in;
        src_size -= chunk_in;
    }

    *dst = out;
    *dst_size = pos;
    return TRUE;
}

/***********************************************************************
 *		Compress (CABINET.33)
 */
BOOL WINAPI Compress( COMPRESSOR_HANDLE handle, const VOID *data, SIZE_T data_size,
                      VOID *buffer, SIZE_T buffer_size, SIZE_T *compressed_data_size )
{
    struct compression_ctx *ctx = (struct compression_ctx *)handle;
    BYTE *out = NULL;
    SIZE_T out_size = 0;

    TRACE("handle %p, data %p, data_size %Iu, buffer %p, buffer_size %Iu, out_size %p.\n",
          handle, data, data_size, buffer, buffer_size, compressed_data_size);

    if (!ctx || !compressed_data_size)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    if ((ctx->algorithm & 0x0fffffff) != COMPRESS_ALGORITHM_MSZIP)
    {
        FIXME("unsupported algorithm %lu\n", ctx->algorithm);
        SetLastError( ERROR_NOT_SUPPORTED );
        return FALSE;
    }

    if (!mszip_compress( ctx, data, data_size, &out, &out_size ))
    {
        SetLastError( ERROR_OUTOFMEMORY );
        return FALSE;
    }

    *compressed_data_size = out_size;
    if (!buffer || buffer_size < out_size)
    {
        free( out );
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        return FALSE;
    }

    memcpy( buffer, out, out_size );
    free( out );
    return TRUE;
}

/***********************************************************************
 *		CloseCompressor  (CABINET.35)
 */
BOOL WINAPI CloseCompressor( COMPRESSOR_HANDLE handle )
{
    struct compression_ctx *ctx = (struct compression_ctx *)handle;
    if (!ctx) return TRUE;
    ctx_free( ctx, ctx );
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

static BOOL decompress_mszip( struct compression_ctx *ctx, const BYTE *payload, SIZE_T payload_len,
                              BYTE *out, SIZE_T out_size )
{
    z_stream zs;
    SIZE_T in_off = 0, out_off = 0;
    const BYTE *prev_block = NULL;
    SIZE_T prev_len = 0;
    BYTE *scratch = NULL;

    /* The optimized zlib in wine lets inflate_fast write a match's tail a
       few bytes past the nominal block end, so decompose into a padded
       scratch buffer and copy the exact block size out. */
    if (!(scratch = malloc( MSZIP_BLOCK_SIZE + 64 ))) return FALSE;

    while (out_off < out_size && in_off < payload_len)
    {
        SIZE_T chunk_out = out_size - out_off;
        DWORD chunk_len;
        const BYTE *chunk_ptr;
        SIZE_T avail;
        int ret;

        if (chunk_out > MSZIP_BLOCK_SIZE) chunk_out = MSZIP_BLOCK_SIZE;

        if (payload_len - in_off < 4) { WARN("short chunk hdr\n"); free( scratch ); return FALSE; }
        chunk_len = *(const DWORD *)(payload + in_off);
        in_off += 4;
        if (chunk_len > payload_len - in_off)
        {
            WARN("chunk_len %lu > rem %Iu\n", chunk_len, payload_len - in_off);
            free( scratch );
            return FALSE;
        }

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
        zs.zalloc = ctx_zalloc;
        zs.zfree = ctx_zfree;
        zs.opaque = ctx;
        if ((ret = inflateInit2( &zs, -MAX_WBITS )) != Z_OK)
        {
            WARN("inflateInit2 failed: %d\n", ret);
            free( scratch );
            return FALSE;
        }

        if (prev_block && prev_len)
        {
            uInt dlen = (uInt)prev_len;
            if (dlen > MSZIP_BLOCK_SIZE - 3) dlen = MSZIP_BLOCK_SIZE - 3;
            inflateSetDictionary( &zs, prev_block + (prev_len - dlen), dlen );
        }

        zs.next_in = (Bytef *)chunk_ptr;
        zs.avail_in = (uInt)avail;
        zs.next_out = scratch;
        zs.avail_out = (uInt)(chunk_out + 64);

        for (;;)
        {
            ret = inflate( &zs, Z_FINISH );
            if (ret == Z_STREAM_END) break;
            if (zs.avail_out <= 64) break; /* block output complete */
            if (ret != Z_OK) break;
        }
        if (ret != Z_STREAM_END && !(zs.avail_out <= 64 && (ret == Z_OK || ret == Z_BUF_ERROR)))
        {
            WARN("inflate failed: ret %d (%s) avail_in=%u avail_out=%u\n", ret, zs.msg ? zs.msg : "", zs.avail_in, zs.avail_out);
            inflateEnd( &zs );
            free( scratch );
            return FALSE;
        }

        /* Full MSZIP blocks always decode to the complete 32 KiB window. */
        memcpy( out + out_off, scratch, chunk_out );
        prev_block = scratch;
        prev_len = chunk_out;
        out_off += chunk_out;
        inflateEnd( &zs );
    }

    free( scratch );
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
    const BYTE *payload;
    SIZE_T payload_len;
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

    if (ctx->algorithm & COMPRESS_RAW)
    {
        /* Raw stream: DWORD uncompressed size, then 'CK' deflate blocks. */
        if (data_size < 4)
        {
            SetLastError( ERROR_BAD_FORMAT );
            return FALSE;
        }
        uncomp_size = *(const DWORD *)b;
        payload = b + 4;
        payload_len = data_size - 4;
    }
    else
    {
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

        payload = b + hdr_size;
        payload_len = data_size - hdr_size;
    }

    *decompressed_data_size = (SIZE_T)uncomp_size;
    if (!buffer || buffer_size < uncomp_size)
    {
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        return FALSE;
    }

    if (!decompress_mszip( ctx, payload, payload_len, buffer, (SIZE_T)uncomp_size ))
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
