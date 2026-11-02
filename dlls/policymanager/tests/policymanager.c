/*
 * Device management policy (ext-ms-win-devmgmt-policy-l1-1-1 / policymanager.dll) conformance tests
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>

#include "wine/test.h"

/*
 * We have no device-management policy store, so every query must report "policy not found"
 * through the apiset that in-box Windows binds to policymanager.dll.
 */
static const char apiset_name[] = "ext-ms-win-devmgmt-policy-l1-1-1.dll";

static const char *const policymanager_exports[] =
{
    "PolicyManager_GetPolicy",
    "PolicyManager_FreeGetPolicyData",
    "PolicyManager_GetPolicyInt",
};

typedef HRESULT (WINAPI *get_policy_t)( const WCHAR *area, const WCHAR *name, void *value );
typedef HRESULT (WINAPI *free_get_policy_data_t)( void *data );
typedef HRESULT (WINAPI *get_policy_int_t)( const WCHAR *area, const WCHAR *name, int *value );

static void test_policymanager_apiset( void )
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

    for (i = 0; i < ARRAY_SIZE( policymanager_exports ); i++)
        ok( GetProcAddress( module, policymanager_exports[i] ) != NULL, "Failed to get %s, err %lu.\n",
            policymanager_exports[i], GetLastError() );

    FreeLibrary( module );
    winetest_pop_context();
}

static void test_get_policy( void )
{
    static const WCHAR *area = L"DeviceInstall";
    static const WCHAR *name = L"RestrictDrivers";
    get_policy_t p_get_policy;
    free_get_policy_data_t p_free;
    get_policy_int_t p_get_policy_int;
    HMODULE module;
    int value = 0xdeadbeef;
    HRESULT hr;

    winetest_push_context( "policy" );

    module = LoadLibraryA( apiset_name );
    ok( module != NULL, "Failed to load %s, err %lu.\n", apiset_name, GetLastError() );
    if (!module)
    {
        winetest_pop_context();
        return;
    }

    p_get_policy = (get_policy_t) GetProcAddress( module, "PolicyManager_GetPolicy" );
    p_get_policy_int = (get_policy_int_t) GetProcAddress( module, "PolicyManager_GetPolicyInt" );
    p_free = (free_get_policy_data_t) GetProcAddress( module, "PolicyManager_FreeGetPolicyData" );
    ok( p_get_policy != NULL, "PolicyManager_GetPolicy is missing.\n" );
    ok( p_get_policy_int != NULL, "PolicyManager_GetPolicyInt is missing.\n" );
    ok( p_free != NULL, "PolicyManager_FreeGetPolicyData is missing.\n" );

    if (p_get_policy)
    {
        hr = p_get_policy( area, name, NULL );
        ok( hr == HRESULT_FROM_WIN32( ERROR_NOT_FOUND ), "PolicyManager_GetPolicy returned %#lx.\n", hr );
    }
    if (p_get_policy_int)
    {
        hr = p_get_policy_int( area, name, &value );
        ok( hr == HRESULT_FROM_WIN32( ERROR_NOT_FOUND ), "PolicyManager_GetPolicyInt returned %#lx.\n", hr );
    }
    /* The free path must tolerate a NULL buffer, callers free whatever the query handed back. */
    if (p_free) p_free( NULL );

    FreeLibrary( module );
    winetest_pop_context();
}

START_TEST( policymanager )
{
    test_policymanager_apiset();
    test_get_policy();
}
