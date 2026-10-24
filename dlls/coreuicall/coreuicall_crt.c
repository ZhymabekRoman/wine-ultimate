/*
 * CRT utility apiset host functions
 *
 * The api-ms-win-crt-utility-l1-1-0 API set is the host module that
 * CoreMessagingXP.dll (and the WindowsAppRuntime family) resolves its
 * CoreMsg and CoreUICall conversation entry points from (the app package
 * ships a CoreMessagingXP.dll whose import table names that set).  This
 * module is that host: it implements the CoreUICall protocol (coreuicall.c)
 * and forwards the handful of CRT utility functions the same set carries
 * (qsort, qsort_s, rand, rand_s, srand - the names the app binaries of
 * this family import) to ucrtbase, so repointing
 * api-ms-win-crt-utility-l1-1-0 onto this module loses nothing.
 *
 * Copyright 2026 Zhymabek Roman
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
 */

#include <stdarg.h>
#include <stddef.h>

#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"



/* The ucrtbase import library provides these plain names; a Wine PE module
 * does not link the host libc, so the calls bind to the ucrtbase imports.
 * Declared here (not via stdlib.h) to avoid host header clashes. */
extern void qsort( void *base, size_t count, size_t size, int (*compar)(const void *, const void *) );
extern int  qsort_s( void *base, size_t count, size_t size,
                     int (*compar)(void *, const void *, const void *), void *context );
extern int  rand( void );
extern void srand( unsigned int seed );
extern int  rand_s( unsigned int *val );

NTSTATUS WINAPI wine_rand_s( unsigned int *val )
{
    return rand_s( val );
}

int WINAPI wine_rand( void )
{
    return rand();
}

void WINAPI wine_srand( unsigned int seed )
{
    srand( seed );
}

void WINAPI wine_qsort( void *base, size_t count, size_t size,
                   int (CDECL *compar)(const void *, const void *) )
{
    qsort( base, count, size, compar );
}

int WINAPI wine_qsort_s( void *base, size_t count, size_t size,
                    int (CDECL *compar)(void *, const void *, const void *), void *context )
{
    return qsort_s( base, count, size, compar, context );
}
