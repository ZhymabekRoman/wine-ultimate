/*
 * SysMain offerable heap apiset (wineuwp.dll)
 *
 * Copyright 2026 Zealot
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(uwp);

/* ext-ms-win-sysmain-pfsapi-l1-1-0: SysMain's "offerable heap", which Windows.UI.Xaml's OfferableHeap
 * delay-loads (a failed delay load aborted the process). Signatures from XAML's wrappers: Initialize
 * and Start take the heap and return a Win32 status; Alloc(heap, size) returns the block or NULL;
 * Free/Offer/Reclaim take (heap, block); Reclaim returns 0 when the block's contents survived. Nothing
 * is ever offered to the system here, so every block survives. */
DWORD WINAPI PfsOfferableHeapInitialize( void *heap, void *a, void *b, void *c )
{
    TRACE( "%p %p %p %p\n", heap, a, b, c );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapStart( void *heap, void *a, void *b, void *c )
{
    TRACE( "%p\n", heap );
    return ERROR_SUCCESS;
}
void * WINAPI PfsOfferableHeapAlloc( void *heap, SIZE_T size, void *a, void *b )
{
    void *block = HeapAlloc( GetProcessHeap(), 0, size );
    TRACE( "%p size %Iu -> %p\n", heap, size, block );
    return block;
}
DWORD WINAPI PfsOfferableHeapFree( void *heap, void *block, void *a, void *b )
{
    TRACE( "%p %p\n", heap, block );
    if (block) HeapFree( GetProcessHeap(), 0, block );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapOffer( void *heap, void *block, void *a, void *b )
{
    TRACE( "%p %p\n", heap, block );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapReclaim( void *heap, void *block, void *a, void *b )
{
    TRACE( "%p %p\n", heap, block );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapCleanup( void *heap, void *a, void *b, void *c )
{
    TRACE( "%p\n", heap );
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
    }
    return TRUE;
}
