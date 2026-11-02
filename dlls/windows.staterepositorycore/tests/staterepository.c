/*
 * State repository cache (ext-ms-onecore-appmodel-staterepository-cache-*) tests
 *
 * Copyright 2026 the Wine project authors
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

/*
 * The cache apiset must load (MrmCoreR triggers its documented fallback on
 * SR_E_UNAVAILABLE, so the mapping in apisetschema.spec is the contract), every
 * export must resolve, and the WinRT activation factory must answer the
 * Windows.Internal.StateRepository.* classes and refuse the rest.
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"

#include "wine/test.h"

#define SR_E_UNAVAILABLE HRESULT_FROM_WIN32( ERROR_SERVICE_NOT_ACTIVE )

static const char *const cache_exports[] =
{
    "SRCacheManager_Open",
    "SRCacheManager_Close",
    "SRCacheManager_GetProperty_UInt32",
    "SRCacheManager_SetProperty_UInt32",
    "SRCacheManager_SetProperty_UInt64",
    "SRCacheContext_Open",
    "SRCacheContext_OpenSubContext",
    "SRCacheContext_Close",
    "SRCacheContext_IsEmpty",
    "SRCacheContext_EnumerateData",
    "SRCacheContext_AddToCache",
    "SRCacheContext_GetField_UInt32",
    "SRCacheContext_GetField_UInt64",
    "SRCacheContext_GetField_String",
    "SRCacheContext_GetField_MultiString",
    "SRCacheContext_GetField_Binary",
    "SRCacheContext_GetField_Guid",
    "SRCache_AllocStringBuffer",
    "SRCache_DuplicateString",
    "SRCache_Free",
    "SRCache_ExpandMacros",
    "SRCache_GetDefaultAccountSid",
};

static void test_apiset_exports( void )
{
    unsigned int i;
    HMODULE mod = LoadLibraryA( "ext-ms-onecore-appmodel-staterepository-cache-l1-1-0.dll" );

    ok( mod != NULL, "failed to load the staterepository cache apiset, err %lu\n", GetLastError() );
    if (!mod) return;

    for (i = 0; i < ARRAY_SIZE( cache_exports ); i++)
        ok( GetProcAddress( mod, cache_exports[i] ) != NULL, "%s not exported\n", cache_exports[i] );

    FreeLibrary( mod );
}

static void test_cache_unavailable( void )
{
    HRESULT (WINAPI *p_Open)( const void *, void ** );
    HRESULT (WINAPI *p_AllocStringBuffer)( ULONG, WCHAR ** );
    HRESULT (WINAPI *p_DuplicateString)( const WCHAR *, WCHAR ** );
    HRESULT (WINAPI *p_Free)( void * );
    void *manager = (void *)0xdead;
    WCHAR *copy = NULL, *buffer = NULL;
    HMODULE mod;
    HRESULT hr;

    mod = LoadLibraryA( "ext-ms-onecore-appmodel-staterepository-cache-l1-1-0.dll" );
    if (!mod)
    {
        skip( "cache apiset does not load (err %lu)\n", GetLastError() );
        return;
    }

    p_Open = (void *)GetProcAddress( mod, "SRCacheManager_Open" );
    if (p_Open)
    {
        hr = p_Open( NULL, &manager );
        ok( hr == SR_E_UNAVAILABLE, "SRCacheManager_Open hr %#lx, expected %#lx\n", hr, SR_E_UNAVAILABLE );
        ok( manager == NULL, "SRCacheManager_Open handed out %p\n", manager );
    }
    else skip( "SRCacheManager_Open not exported\n" );

    /* The allocator pair works even while the repository is unavailable: callers
     * free strings on the failure paths. */
    p_AllocStringBuffer = (void *)GetProcAddress( mod, "SRCache_AllocStringBuffer" );
    p_DuplicateString = (void *)GetProcAddress( mod, "SRCache_DuplicateString" );
    p_Free = (void *)GetProcAddress( mod, "SRCache_Free" );
    if (p_AllocStringBuffer && p_Free)
    {
        hr = p_AllocStringBuffer( 4, &buffer );
        ok( hr == S_OK && buffer && !buffer[0], "AllocStringBuffer hr %#lx buffer %p\n", hr, buffer );
        p_Free( buffer );
    }
    else skip( "SRCache_AllocStringBuffer not exported\n" );

    if (p_DuplicateString && p_Free)
    {
        static const WCHAR str[] = L"abc";
        hr = p_DuplicateString( str, &copy );
        ok( hr == S_OK && copy && !wcscmp( copy, str ), "DuplicateString hr %#lx copy %s\n", hr, debugstr_w( copy ) );
        p_Free( copy );
    }
    else skip( "SRCache_DuplicateString not exported\n" );

    FreeLibrary( mod );
}

static void test_activation( void )
{
    static const WCHAR user_class[] = L"Windows.Internal.StateRepository.User";
    static const WCHAR package_class[] = L"Windows.Internal.StateRepository.Package";
    static const WCHAR foreign_class[] = L"Windows.ApplicationModel.Package";
    HRESULT (WINAPI *p_DllGetActivationFactory)( HSTRING, void ** );
    HMODULE mod = LoadLibraryA( "windows.staterepositorycore.dll" );
    HRESULT hr;
    HSTRING str;
    void *factory;

    ok( mod != NULL, "failed to load windows.staterepositorycore.dll, err %lu\n", GetLastError() );
    if (!mod) return;

    p_DllGetActivationFactory = (void *)GetProcAddress( mod, "DllGetActivationFactory" );
    ok( p_DllGetActivationFactory != NULL, "DllGetActivationFactory not exported\n" );
    if (!p_DllGetActivationFactory)
    {
        FreeLibrary( mod );
        return;
    }

    WindowsCreateString( user_class, ARRAY_SIZE( user_class ) - 1, &str );
    factory = NULL;
    hr = p_DllGetActivationFactory( str, &factory );
    ok( hr == S_OK && factory, "User factory hr %#lx factory %p\n", hr, factory );
    WindowsDeleteString( str );

    WindowsCreateString( package_class, ARRAY_SIZE( package_class ) - 1, &str );
    factory = NULL;
    hr = p_DllGetActivationFactory( str, &factory );
    ok( hr == S_OK && factory, "Package factory hr %#lx factory %p\n", hr, factory );
    WindowsDeleteString( str );

    WindowsCreateString( foreign_class, ARRAY_SIZE( foreign_class ) - 1, &str );
    factory = (void *)0xdead;
    hr = p_DllGetActivationFactory( str, &factory );
    ok( hr == CLASS_E_CLASSNOTAVAILABLE && !factory, "foreign class hr %#lx, expected CLASS_E_CLASSNOTAVAILABLE\n", hr );
    WindowsDeleteString( str );

    FreeLibrary( mod );
}

START_TEST( staterepository )
{
    test_apiset_exports();
    test_cache_unavailable();
    test_activation();
}
