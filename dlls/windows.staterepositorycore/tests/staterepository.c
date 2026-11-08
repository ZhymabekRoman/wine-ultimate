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

/*
 * The QueryState plane answers from the package registration (the AppModel key
 * kernelbase reads): seed a deployment record and the context fields report the
 * package's state repository and DataPackage paths; an unregistered name is the
 * empty cache. The record is seeded for the test process only, HKCU.
 */
static void test_cache_registry( void )
{
    static const WCHAR full_name[] = L"WineTest.Package_1.2.3.4_x64__abcd1234abcd";
    static const WCHAR foreign_name[] = L"WineTest.Absent_9.9.9.9_x64__zzzzzzzzzzzz";
    static const WCHAR pkg_path[] = L"C:\\WineTestPkg";
    static const WCHAR sr_path[] = L"C:\\WineTestSR";
    static const WCHAR appmodel_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\WineTest.Package_1.2.3.4_x64__abcd1234abcd";
    HRESULT (WINAPI *p_ManagerOpen)( const void *, void ** );
    HRESULT (WINAPI *p_ContextOpen)( void *, const WCHAR *, void ** );
    HRESULT (WINAPI *p_IsEmpty)( void *, BOOL * );
    HRESULT (WINAPI *p_GetField_UInt32)( void *, ULONG, ULONG * );
    HRESULT (WINAPI *p_GetField_String)( void *, ULONG, WCHAR ** );
    HRESULT (WINAPI *p_GetDefaultAccountSid)( WCHAR ** );
    HRESULT (WINAPI *p_Free)( void * );
    HMODULE mod;
    HRESULT hr;
    void *manager, *context;
    BOOL empty;
    ULONG roaming;
    WCHAR *str;
    HKEY hkey;

    mod = LoadLibraryA( "ext-ms-onecore-appmodel-staterepository-cache-l1-1-0.dll" );
    if (!mod)
    {
        skip( "cache apiset does not load (err %lu)\n", GetLastError() );
        return;
    }

    p_ManagerOpen = (void *)GetProcAddress( mod, "SRCacheManager_Open" );
    p_ContextOpen = (void *)GetProcAddress( mod, "SRCacheContext_Open" );
    p_IsEmpty = (void *)GetProcAddress( mod, "SRCacheContext_IsEmpty" );
    p_GetField_UInt32 = (void *)GetProcAddress( mod, "SRCacheContext_GetField_UInt32" );
    p_GetField_String = (void *)GetProcAddress( mod, "SRCacheContext_GetField_String" );
    p_GetDefaultAccountSid = (void *)GetProcAddress( mod, "SRCache_GetDefaultAccountSid" );
    p_Free = (void *)GetProcAddress( mod, "SRCache_Free" );
    if (!p_ManagerOpen || !p_ContextOpen || !p_IsEmpty || !p_GetField_UInt32 || !p_GetField_String ||
        !p_GetDefaultAccountSid || !p_Free)
    {
        skip( "cache exports missing\n" );
        FreeLibrary( mod );
        return;
    }

    /* the manager is an open handle to the repository, not to the cache;
     * contexts open on the handle the manager hands out (here: none) */
    manager = (void *)0xdead;
    hr = p_ManagerOpen( NULL, &manager );
    ok( hr == SR_E_UNAVAILABLE && !manager, "manager open hr %#lx manager %p\n", hr, manager );
    manager = NULL;

    if (RegCreateKeyExW( HKEY_CURRENT_USER, appmodel_key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL ))
    {
        skip( "cannot seed the package registration\n" );
        FreeLibrary( mod );
        return;
    }
    RegSetValueExW( hkey, L"PackagePath", 0, REG_SZ, (const BYTE *)pkg_path, sizeof(pkg_path) );
    RegCloseKey( hkey );

    hr = p_ContextOpen( manager, full_name, &context );
    ok( hr == S_OK && context, "context open hr %#lx context %p\n", hr, context );
    if (SUCCEEDED( hr ) && context)
    {
        empty = TRUE;
        hr = p_IsEmpty( context, &empty );
        ok( hr == S_OK && !empty, "registered package: empty %d hr %#lx\n", empty, hr );

        roaming = 0;
        hr = p_GetField_UInt32( context, 2, &roaming );
        ok( hr == S_OK && roaming == 1, "Roaming %lu hr %#lx\n", roaming, hr );

        str = (void *)0xdead;
        hr = p_GetField_String( context, 7, &str ); /* DataPackagePath */
        ok( hr == S_OK && str && !wcscmp( str, L"C:\\WineTestPkg\\AppData\\Local" ),
            "DataPackagePath hr %#lx str %s\n", hr, debugstr_w( str ) );
        p_Free( str );

        str = (void *)0xdead;
        hr = p_GetField_String( context, 1, &str ); /* Path: convention AppData root */
        ok( hr == S_OK && str && !wcscmp( str, L"C:\\WineTestPkg\\AppData" ),
            "Path hr %#lx str %s\n", hr, debugstr_w( str ) );
        p_Free( str );

        /* the recorded StateRepositoryPath wins over the convention */
        if (!RegOpenKeyExW( HKEY_CURRENT_USER, appmodel_key, 0, KEY_WRITE, &hkey ))
        {
            RegSetValueExW( hkey, L"StateRepositoryPath", 0, REG_SZ, (const BYTE *)sr_path, sizeof(sr_path) );
            RegCloseKey( hkey );
        }
        str = (void *)0xdead;
        hr = p_GetField_String( context, 1, &str );
        ok( hr == S_OK && str && !wcscmp( str, sr_path ),
            "recorded StateRepositoryPath hr %#lx str %s\n", hr, debugstr_w( str ) );
        p_Free( str );
    }

    /* the running package answers under its own identity too: the empty name
     * is the root context (a no-op outside a package) */
    {
        void *root = NULL;
        hr = p_ContextOpen( manager, NULL, &root );
        ok( hr == S_OK, "root context open hr %#lx\n", hr );
        if (SUCCEEDED( hr ) && root)
        {
            str = NULL;
            hr = p_GetField_String( root, 3, &str ); /* InstanceId is always there */
            ok( hr == S_OK && str && str[0], "InstanceId hr %#lx str %s\n", hr, debugstr_w( str ) );
            p_Free( str );
        }
    }

    /* an unregistered name is the empty cache: S_OK, no string, empty */
    hr = p_ContextOpen( manager, foreign_name, &context );
    if (SUCCEEDED( hr ) && context)
    {
        empty = FALSE;
        hr = p_IsEmpty( context, &empty );
        ok( hr == S_OK && empty, "unregistered package: empty %d hr %#lx\n", empty, hr );
        str = (void *)0xdead;
        hr = p_GetField_String( context, 1, &str );
        ok( hr == S_OK && !str, "unregistered Path hr %#lx str %s\n", hr, debugstr_w( str ) );
        p_Free( str );
    }

    str = (void *)0xdead;
    hr = p_GetDefaultAccountSid( &str );
    ok( hr == S_OK && str && str[0] == L'S', "default account sid hr %#lx sid %s\n", hr, debugstr_w( str ) );
    p_Free( str );

    RegDeleteTreeW( HKEY_CURRENT_USER, appmodel_key );
    FreeLibrary( mod );
}

/*
 * Cache.StateCacheManager: the WinRT face of the C plane. The classes are
 * private (no metadata header), so the test drives the vtables directly: the
 * statics' slot 6 QueryState opens a context against the registration, and the
 * context projects the C plane's GetField_String as an HSTRING.
 */

struct cache_statics_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *, const void *, void ** );
    ULONG (WINAPI *AddRef)( void * );
    ULONG (WINAPI *Release)( void * );
    HRESULT (WINAPI *GetIids)( void *, ULONG *, void ** );
    HRESULT (WINAPI *GetRuntimeClassName)( void *, HSTRING * );
    HRESULT (WINAPI *GetTrustLevel)( void *, int * );
    HRESULT (WINAPI *QueryState)( void *, HSTRING, void ** );
    HRESULT (WINAPI *CreateStateCacheManager)( void *, void ** );
};

struct cache_context_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *, const void *, void ** );
    ULONG (WINAPI *AddRef)( void * );
    ULONG (WINAPI *Release)( void * );
    HRESULT (WINAPI *GetIids)( void *, ULONG *, void ** );
    HRESULT (WINAPI *GetRuntimeClassName)( void *, HSTRING * );
    HRESULT (WINAPI *GetTrustLevel)( void *, int * );
    HRESULT (WINAPI *OpenSubContext)( void *, HSTRING, void ** );
    HRESULT (WINAPI *Close)( void * );
    HRESULT (WINAPI *IsEmpty)( void *, int * );
    HRESULT (WINAPI *EnumerateData)( void *, void * );
    HRESULT (WINAPI *AddToCache)( void *, void * );
    HRESULT (WINAPI *GetField_UInt32)( void *, ULONG, ULONG * );
    HRESULT (WINAPI *GetField_UInt64)( void *, ULONG, ULONG64 * );
    HRESULT (WINAPI *GetField_String)( void *, ULONG, HSTRING * );
    HRESULT (WINAPI *GetField_MultiString)( void *, ULONG, ULONG *, HSTRING ** );
    HRESULT (WINAPI *GetField_Binary)( void *, ULONG, ULONG *, void ** );
    HRESULT (WINAPI *GetField_Guid)( void *, ULONG, GUID * );
};

static void test_cache_statecache( void )
{
    static const WCHAR manager_class[] = L"Windows.Internal.StateRepository.Cache.StateCacheManager";
    static const WCHAR full_name[] = L"WineTest.CacheState_7.7.7.7_x64__qq11qq11qq11";
    static const WCHAR pkg_path[] = L"C:\\WineTestCacheState";
    static const WCHAR appmodel_key[] =
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\WineTest.CacheState_7.7.7.7_x64__qq11qq11qq11";
    HRESULT (WINAPI *p_DllGetActivationFactory)( HSTRING, void ** );
    const struct cache_statics_vtbl *statics;
    const struct cache_context_vtbl *vtbl;
    HMODULE mod = LoadLibraryA( "windows.staterepositorycore.dll" );
    void *factory, *manager, *context, *sub;
    HSTRING str, hstr;
    HRESULT hr;
    int empty;
    ULONG roaming;
    HKEY hkey;

    ok( mod != NULL, "failed to load windows.staterepositorycore.dll, err %lu\n", GetLastError() );
    if (!mod) return;
    p_DllGetActivationFactory = (void *)GetProcAddress( mod, "DllGetActivationFactory" );
    if (!p_DllGetActivationFactory)
    {
        skip( "DllGetActivationFactory not exported\n" );
        FreeLibrary( mod );
        return;
    }

    WindowsCreateString( manager_class, ARRAY_SIZE(manager_class) - 1, &str );
    factory = NULL;
    hr = p_DllGetActivationFactory( str, &factory );
    WindowsDeleteString( str );
    ok( hr == S_OK && factory, "StateCacheManager factory hr %#lx factory %p\n", hr, factory );
    if (hr != S_OK || !factory)
    {
        FreeLibrary( mod );
        return;
    }
    statics = *(const struct cache_statics_vtbl **)factory;

    /* CreateStateCacheManager hands out the manager row */
    manager = NULL;
    hr = statics->CreateStateCacheManager( factory, &manager );
    ok( hr == S_OK && manager, "CreateStateCacheManager hr %#lx manager %p\n", hr, manager );

    /* seed a deployment record; the context answers its fields from it */
    if (RegCreateKeyExW( HKEY_CURRENT_USER, appmodel_key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL ))
    {
        skip( "cannot seed the package registration\n" );
        FreeLibrary( mod );
        return;
    }
    RegSetValueExW( hkey, L"PackagePath", 0, REG_SZ, (const BYTE *)pkg_path, sizeof(pkg_path) );
    RegCloseKey( hkey );

    WindowsCreateString( full_name, ARRAY_SIZE(full_name) - 1, &str );
    context = NULL;
    hr = statics->QueryState( factory, str, &context );
    WindowsDeleteString( str );
    ok( hr == S_OK && context, "QueryState hr %#lx context %p\n", hr, context );
    if (hr == S_OK && context)
    {
        vtbl = *(const struct cache_context_vtbl **)context;

        empty = TRUE;
        hr = vtbl->IsEmpty( context, &empty );
        ok( hr == S_OK && !empty, "registered package: empty %d hr %#lx\n", empty, hr );

        roaming = 0;
        hr = vtbl->GetField_UInt32( context, 2, &roaming ); /* Roaming */
        ok( hr == S_OK && roaming == 1, "Roaming %lu hr %#lx\n", roaming, hr );

        hstr = NULL;
        hr = vtbl->GetField_String( context, 7, &hstr ); /* DataPackagePath */
        ok( hr == S_OK && hstr && !wcscmp( WindowsGetStringRawBuffer( hstr, NULL ), L"C:\\WineTestCacheState\\AppData\\Local" ),
                "DataPackagePath hr %#lx str %s\n", hr, debugstr_hstring( hstr ) );
        WindowsDeleteString( hstr );

        hstr = NULL;
        hr = vtbl->GetField_String( context, 3, &hstr ); /* InstanceId */
        ok( hr == S_OK && hstr && WindowsGetStringLen(hstr), "InstanceId hr %#lx str %s\n", hr, debugstr_hstring( hstr ) );
        WindowsDeleteString( hstr );

        /* the subcontext projects OpenSubContext: the name grows, the answers stay */
        {
            static const WCHAR sub_name[] = L"Sub";
            WindowsCreateString( sub_name, ARRAY_SIZE(sub_name) - 1, &str );
            sub = NULL;
            hr = vtbl->OpenSubContext( context, str, &sub );
            WindowsDeleteString( str );
            ok( hr == S_OK && sub, "OpenSubContext hr %#lx sub %p\n", hr, sub );
            if (hr == S_OK && sub)
            {
                const struct cache_context_vtbl *svtbl = *(const struct cache_context_vtbl **)sub;
                hstr = NULL;
                hr = svtbl->GetField_String( sub, 3, &hstr );
                ok( hr == S_OK && hstr && WindowsGetStringLen(hstr), "sub InstanceId hr %#lx str %s\n", hr, debugstr_hstring( hstr ) );
                WindowsDeleteString( hstr );
                svtbl->Release( sub );
            }
        }
        vtbl->Release( context );
    }

    /* the row answers QueryState at its own slot 6 */
    if (manager)
    {
        const struct cache_statics_vtbl *mvtbl = *(const struct cache_statics_vtbl **)manager;
        context = NULL;
        hr = mvtbl->QueryState( manager, NULL, &context );
        ok( hr == S_OK && context, "manager row QueryState hr %#lx context %p\n", hr, context );
        if (hr == S_OK && context)
        {
            vtbl = *(const struct cache_context_vtbl **)context;
            vtbl->Release( context );
        }
        mvtbl->Release( manager );
    }

    (*(const struct cache_statics_vtbl **)factory)->Release( factory );
    RegDeleteTreeW( HKEY_CURRENT_USER, appmodel_key );
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
    test_cache_registry();
    test_cache_statecache();
    test_activation();
}
