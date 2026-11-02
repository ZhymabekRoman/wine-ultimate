/*
 * SysMain offerable heap (ext-ms-win-sysmain-pfsapi-l1-1-0 / wineuwp.dll) conformance tests
 *
 * Copyright 2026 Zealot
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <windows.h>

#include "wine/test.h"

static const char apiset_name[] = "ext-ms-win-sysmain-pfsapi-l1-1-0.dll";

static const char *const pfsapi_exports[] =
{
    "PfsOfferableHeapInitialize",
    "PfsOfferableHeapStart",
    "PfsOfferableHeapAlloc",
    "PfsOfferableHeapFree",
    "PfsOfferableHeapOffer",
    "PfsOfferableHeapReclaim",
    "PfsOfferableHeapCleanup",
};

typedef void *(WINAPI *pfs_heap_alloc_t)( void *heap, SIZE_T size, void *arg3, void *arg4 );
typedef DWORD (WINAPI *pfs_heap_free_t)( void *heap, void *block, void *arg3, void *arg4 );

static void test_pfsapi_apiset( void )
{
    HMODULE module;
    SIZE_T i;

    winetest_push_context( "apiset" );

    module = LoadLibraryA( apiset_name );
    ok( module != NULL, "Failed to load %s, err %lu.\n", apiset_name, GetLastError() );
    if (!module)
    {
        winetest_pop_context();
        return;
    }

    for (i = 0; i < ARRAY_SIZE( pfsapi_exports ); i++)
        ok( GetProcAddress( module, pfsapi_exports[i] ) != NULL, "Failed to get %s, err %lu.\n",
            pfsapi_exports[i], GetLastError() );

    FreeLibrary( module );
    winetest_pop_context();
}

static void test_pfsapi_heap( void )
{
    /* XAML's OfferableHeap passes its own heap object; our implementation ignores it and uses
     * the process heap, so the value must not matter as long as alloc and free agree. */
    static void *const fake_heap = (void *) 0xdeadbeef;
    static const SIZE_T size = 0x100;
    pfs_heap_alloc_t p_alloc;
    pfs_heap_free_t p_free;
    HMODULE module;
    char *block;
    DWORD ret;

    winetest_push_context( "heap" );

    module = LoadLibraryA( apiset_name );
    ok( module != NULL, "Failed to load %s, err %lu.\n", apiset_name, GetLastError() );
    if (!module)
    {
        winetest_pop_context();
        return;
    }

    p_alloc = (pfs_heap_alloc_t) GetProcAddress( module, "PfsOfferableHeapAlloc" );
    p_free = (pfs_heap_free_t) GetProcAddress( module, "PfsOfferableHeapFree" );
    ok( p_alloc != NULL, "PfsOfferableHeapAlloc is missing.\n" );
    ok( p_free != NULL, "PfsOfferableHeapFree is missing.\n" );

    if (p_alloc && p_free)
    {
        block = p_alloc( fake_heap, size, NULL, NULL );
        ok( block != NULL, "PfsOfferableHeapAlloc(%Iu) returned NULL.\n", size );
        if (block)
        {
            memset( block, 0x5a, size ); /* the block must be real writable memory */
            ok( block[0] == 0x5a && block[size - 1] == 0x5a, "Block is not writable.\n" );
        }
        ret = p_free( fake_heap, block, NULL, NULL );
        ok( ret == ERROR_SUCCESS, "PfsOfferableHeapFree returned %lu.\n", ret );
    }

    FreeLibrary( module );
    winetest_pop_context();
}

START_TEST( wineuwp )
{
    test_pfsapi_apiset();
    test_pfsapi_heap();
}
