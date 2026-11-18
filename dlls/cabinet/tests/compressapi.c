/*
 * Unit tests for the Compression API
 *
 * Copyright 2026 Wine Project Contributors
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
#include <stdlib.h>
#include <windows.h>
#include "compressapi.h"
#include "wine/test.h"

#ifndef COMPRESS_RAW
#define COMPRESS_RAW 0x20000000
#endif

static LONG alloc_count;
static LONG free_count;

static void *CALLBACK test_alloc( void *ctx, SIZE_T size )
{
    InterlockedIncrement( &alloc_count );
    return HeapAlloc( GetProcessHeap(), 0, size );
}

static void CALLBACK test_free( void *ctx, void *ptr )
{
    InterlockedIncrement( &free_count );
    HeapFree( GetProcessHeap(), 0, ptr );
}

static void test_decompressor( void )
{
    static const BYTE mszip_blob[] = {
        0x0a, 0x51, 0xe5, 0xc0, 0x18, 0x00, 0x96, 0x02,
        0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x36, 0x00, 0x00, 0x00, 0x43, 0x4b, 0xf3, 0x48,
        0xcd, 0xc9, 0xc9, 0x57, 0x08, 0xcf, 0xcc, 0x4b,
        0x55, 0x70, 0xce, 0xcf, 0x2d, 0x28, 0x4a, 0x2d,
        0x2e, 0xce, 0xcc, 0xcf, 0x53, 0x70, 0x0c, 0xf0,
        0x54, 0xf0, 0x0d, 0x8e, 0xf2, 0x0c, 0x50, 0x28,
        0x49, 0x2d, 0x2e, 0x51, 0x28, 0x48, 0xac, 0xcc,
        0xc9, 0x4f, 0x4c, 0x51, 0x54, 0xf0, 0x18, 0x4c,
        0xca, 0x01
    };
    static const char expected[] =
        "Hello Wine Compression API MSZIP test payload! "
        "Hello Wine Compression API MSZIP test payload! "
        "Hello Wine Compression API MSZIP test payload! "
        "Hello Wine Compression API MSZIP test payload! ";

    COMPRESS_ALLOCATION_ROUTINES routines = { test_alloc, test_free, NULL };
    DECOMPRESSOR_HANDLE handle = NULL;
    BYTE out[256], bad_blob[sizeof(mszip_blob)];
    SIZE_T out_size = 0;
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = CreateDecompressor( COMPRESS_ALGORITHM_MSZIP, NULL, NULL );
    ok( !ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "expected ERROR_INVALID_PARAMETER, got ret=%d err=%lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = CreateDecompressor( COMPRESS_ALGORITHM_INVALID, NULL, &handle );
    ok( !ret && GetLastError() == ERROR_NOT_SUPPORTED,
        "expected ERROR_NOT_SUPPORTED, got ret=%d err=%lu\n", ret, GetLastError() );

    alloc_count = free_count = 0;
    ret = CreateDecompressor( COMPRESS_ALGORITHM_MSZIP, &routines, &handle );
    ok( ret && handle != NULL, "CreateDecompressor failed, err=%lu\n", GetLastError() );
    ok( alloc_count == 1, "expected alloc_count 1, got %ld\n", alloc_count );

    /* Parameter validation */
    SetLastError( 0xdeadbeef );
    ret = Decompress( NULL, mszip_blob, sizeof(mszip_blob), out, sizeof(out), &out_size );
    ok( !ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "expected ERROR_INVALID_PARAMETER, got ret=%d err=%lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = Decompress( handle, mszip_blob, sizeof(mszip_blob), out, sizeof(out), NULL );
    ok( !ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "expected ERROR_INVALID_PARAMETER, got ret=%d err=%lu\n", ret, GetLastError() );

    /* Size query with NULL buffer */
    out_size = 0;
    SetLastError( 0xdeadbeef );
    ret = Decompress( handle, mszip_blob, sizeof(mszip_blob), NULL, 0, &out_size );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "expected ERROR_INSUFFICIENT_BUFFER, got ret=%d err=%lu\n", ret, GetLastError() );
    ok( out_size == strlen( expected ), "expected out_size %Iu, got %Iu\n", strlen( expected ), out_size );

    /* Undersized buffer */
    out_size = 0;
    SetLastError( 0xdeadbeef );
    ret = Decompress( handle, mszip_blob, sizeof(mszip_blob), out, 16, &out_size );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "expected ERROR_INSUFFICIENT_BUFFER, got ret=%d err=%lu\n", ret, GetLastError() );
    ok( out_size == strlen( expected ), "expected out_size %Iu, got %Iu\n", strlen( expected ), out_size );

    /* Full decompression */
    memset( out, 0, sizeof(out) );
    out_size = 0;
    ret = Decompress( handle, mszip_blob, sizeof(mszip_blob), out, sizeof(out), &out_size );
    ok( ret, "Decompress failed, err=%lu\n", GetLastError() );
    ok( out_size == strlen( expected ), "expected out_size %Iu, got %Iu\n", strlen( expected ), out_size );
    ok( !memcmp( out, expected, strlen( expected ) ), "decompressed payload mismatch\n" );

    /* Bad magic / truncated */
    memcpy( bad_blob, mszip_blob, sizeof(bad_blob) );
    bad_blob[0] ^= 0xff;
    SetLastError( 0xdeadbeef );
    ret = Decompress( handle, bad_blob, sizeof(bad_blob), out, sizeof(out), &out_size );
    ok( !ret && GetLastError() == ERROR_BAD_FORMAT,
        "expected ERROR_BAD_FORMAT, got ret=%d err=%lu\n", ret, GetLastError() );

    /* Corrupted deflate payload (set reserved btype=3 in first deflate block header) */
    memcpy( bad_blob, mszip_blob, sizeof(bad_blob) );
    bad_blob[30] = (bad_blob[30] & ~0x06) | 0x06;
    SetLastError( 0xdeadbeef );
    ret = Decompress( handle, bad_blob, sizeof(bad_blob), out, sizeof(out), &out_size );
    ok( !ret && GetLastError() == ERROR_BAD_FORMAT,
        "expected ERROR_BAD_FORMAT, got ret=%d err=%lu\n", ret, GetLastError() );

    ret = CloseDecompressor( handle );
    ok( ret, "CloseDecompressor failed, err=%lu\n", GetLastError() );
    ok( alloc_count == free_count, "expected alloc_count %ld == free_count %ld\n", alloc_count, free_count );
    ok( free_count >= 1, "expected at least one free, got %ld\n", free_count );
}

static void roundtrip( DWORD algorithm, const BYTE *payload, SIZE_T size )
{
    COMPRESSOR_HANDLE chandle = NULL;
    DECOMPRESSOR_HANDLE dhandle = NULL;
    BYTE *comp, *out;
    SIZE_T csize = 0, osize = 0;
    BOOL ret;

    comp = malloc( size + 4096 );
    out = malloc( size );

    ret = CreateCompressor( algorithm, NULL, &chandle );
    ok( ret && chandle != NULL, "CreateCompressor(%#lx) failed, err=%lu\n", algorithm, GetLastError() );

    /* Size query first, then compress with the reported size. */
    ret = Compress( chandle, payload, size, NULL, 0, &csize );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "expected ERROR_INSUFFICIENT_BUFFER, got ret=%d err=%lu\n", ret, GetLastError() );
    ok( csize > 0, "expected nonzero compressed size, got %Iu\n", csize );

    ret = Compress( chandle, payload, size, comp, csize - 1, &csize );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "expected ERROR_INSUFFICIENT_BUFFER, got ret=%d err=%lu\n", ret, GetLastError() );

    csize = 0;
    ret = Compress( chandle, payload, size, comp, size + 4096, &csize );
    ok( ret, "Compress(%Iu) failed, err=%lu\n", size, GetLastError() );
    ok( csize > 0 && csize <= size + 4096, "bogus compressed size %Iu\n", csize );

    ret = CloseCompressor( chandle );
    ok( ret, "CloseCompressor failed, err=%lu\n", GetLastError() );

    ret = CreateDecompressor( algorithm, NULL, &dhandle );
    ok( ret && dhandle != NULL, "CreateDecompressor(%#lx) failed, err=%lu\n", algorithm, GetLastError() );

    osize = 0;
    ret = Decompress( dhandle, comp, csize, NULL, 0, &osize );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "expected ERROR_INSUFFICIENT_BUFFER, got ret=%d err=%lu\n", ret, GetLastError() );
    ok( osize == size, "expected decompressed size %Iu, got %Iu\n", size, osize );

    osize = 0;
    ret = Decompress( dhandle, comp, csize, out, size, &osize );
    ok( ret, "Decompress(%Iu) failed, err=%lu\n", size, GetLastError() );
    ok( osize == size, "expected decompressed size %Iu, got %Iu\n", size, osize );
    ok( !memcmp( out, payload, size ), "round-trip payload mismatch (size %Iu, algo %#lx)\n", size, algorithm );

    ret = CloseDecompressor( dhandle );
    ok( ret, "CloseDecompressor failed, err=%lu\n", GetLastError() );

    free( comp );
    free( out );
}

static void test_compressor( void )
{
    COMPRESS_ALLOCATION_ROUTINES routines = { test_alloc, test_free, NULL };
    static BYTE payload[100000];
    COMPRESSOR_HANDLE chandle = NULL;
    DECOMPRESSOR_HANDLE dhandle = NULL;
    SIZE_T csize = 0, i;
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = CreateCompressor( COMPRESS_ALGORITHM_MSZIP, NULL, NULL );
    ok( !ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "expected ERROR_INVALID_PARAMETER, got ret=%d err=%lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = CreateCompressor( COMPRESS_ALGORITHM_INVALID, NULL, &chandle );
    ok( !ret && GetLastError() == ERROR_NOT_SUPPORTED,
        "expected ERROR_NOT_SUPPORTED, got ret=%d err=%lu\n", ret, GetLastError() );

    for (i = 0; i < sizeof(payload); i++) payload[i] = (BYTE)(i * 7 + (i >> 8));

    /* Single chunk, multi chunk (> 32 KiB, cross-block dictionary), raw stream. */
    roundtrip( COMPRESS_ALGORITHM_MSZIP, payload, 1461 );
    roundtrip( COMPRESS_ALGORITHM_MSZIP, payload, sizeof(payload) );
    roundtrip( COMPRESS_ALGORITHM_MSZIP | COMPRESS_RAW, payload, sizeof(payload) );

    /* Compressor handle routed through the custom allocator, zero leaks. */
    alloc_count = free_count = 0;
    ret = CreateCompressor( COMPRESS_ALGORITHM_MSZIP, &routines, &chandle );
    ok( ret && chandle != NULL, "CreateCompressor with routines failed, err=%lu\n", GetLastError() );
    ok( alloc_count == 1, "expected alloc_count 1, got %ld\n", alloc_count );

    ret = Compress( chandle, payload, 512, NULL, 0, &csize );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "expected ERROR_INSUFFICIENT_BUFFER, got ret=%d err=%lu\n", ret, GetLastError() );
    ok( alloc_count > 1, "expected routed zlib allocs, got %ld\n", alloc_count );
    ok( free_count == alloc_count - 1,
        "Compress should balance all allocs except the live handle, got alloc %ld free %ld\n",
        alloc_count, free_count );

    ret = CloseCompressor( chandle );
    ok( ret, "CloseCompressor failed, err=%lu\n", GetLastError() );
    ok( alloc_count == free_count, "expected alloc_count %ld == free_count %ld\n",
        alloc_count, free_count );

    /* Full pipeline through the custom allocator: decompress alloc == free. */
    alloc_count = free_count = 0;
    ret = CreateCompressor( COMPRESS_ALGORITHM_MSZIP, &routines, &chandle );
    ok( ret, "CreateCompressor with routines failed, err=%lu\n", GetLastError() );

    {
        BYTE *comp = malloc( sizeof( payload ) + 4096 ), *out = malloc( sizeof( payload ) );
        csize = 0;
        ret = Compress( chandle, payload, sizeof(payload), comp, sizeof(payload) + 4096, &csize );
        ok( ret, "Compress failed, err=%lu\n", GetLastError() );
        CloseCompressor( chandle );

        ret = CreateDecompressor( COMPRESS_ALGORITHM_MSZIP, &routines, &dhandle );
        ok( ret, "CreateDecompressor with routines failed, err=%lu\n", GetLastError() );
        ret = Decompress( dhandle, comp, csize, out, sizeof(payload), &csize );
        ok( ret, "Decompress failed, err=%lu\n", GetLastError() );
        CloseDecompressor( dhandle );
        ok( alloc_count == free_count, "expected alloc_count %ld == free_count %ld\n",
            alloc_count, free_count );
        ok( alloc_count >= 2, "expected routed zlib allocs, got %ld\n", alloc_count );
        free( comp );
        free( out );
    }
}

START_TEST(compressapi)
{
    test_decompressor();
    test_compressor();
}
