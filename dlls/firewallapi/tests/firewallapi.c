/*
 * Net isolation (api-ms-win-net-isolation-l1-1-1 / firewallapi.dll) conformance tests
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
 * In-box Windows has no network isolation for win32 processes, and every netisolation entry point
 * reports "no isolation in effect". The surface is reached through the apiset only; firewallapi.dll
 * is the host we bind the apiset to.
 */
static const char apiset_name[] = "api-ms-win-net-isolation-l1-1-1.dll";

static const char *const netisolation_exports[] =
{
    "NetworkIsolationDiagnoseConnectFailure",
    "NetworkIsolationDiagnoseConnectFailureGetMessage",
    "NetworkIsolationFreeMessage",
    "NetworkIsolationGetAppContainerConfig",
    "NetworkIsolationGetAppContainerPrivateNetworkStatus",
    "NetworkIsolationIsAppLocallyBridged",
    "NetworkIsolationIsConnected",
    "NetworkIsolationIsDomainJoined",
    "NetworkIsolationIsEnterpriseReady",
    "NetworkIsolationIsPrivateNetworkPresent",
    "NetworkIsolationIsPublisherRestricted",
    "NetworkIsolationRegisterAppContainerNotification",
    "NetworkIsolationSetAppContainerConfig",
    "NetworkIsolationUnregisterAppContainerNotification",
};

typedef DWORD (WINAPI *diagnose_connect_failure_t)( const WCHAR *server );

static void test_netisolation_apiset( void )
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

    for (i = 0; i < ARRAY_SIZE( netisolation_exports ); i++)
        ok( GetProcAddress( module, netisolation_exports[i] ) != NULL, "Failed to get %s, err %lu.\n",
            netisolation_exports[i], GetLastError() );

    FreeLibrary( module );
    winetest_pop_context();
}

static void test_diagnose_connect_failure( void )
{
    diagnose_connect_failure_t p_diagnose;
    HMODULE module;
    DWORD ret;

    winetest_push_context( "diagnose" );

    module = LoadLibraryA( apiset_name );
    ok( module != NULL, "Failed to load %s, err %lu.\n", apiset_name, GetLastError() );
    if (!module)
    {
        winetest_pop_context();
        return;
    }

    /* A connect failure diagnosis with no target must succeed: nothing is isolated, nothing failed. */
    p_diagnose = (diagnose_connect_failure_t) GetProcAddress( module, "NetworkIsolationDiagnoseConnectFailure" );
    ok( p_diagnose != NULL, "NetworkIsolationDiagnoseConnectFailure is missing.\n" );
    if (p_diagnose)
    {
        ret = p_diagnose( NULL );
        ok( ret == ERROR_SUCCESS, "NetworkIsolationDiagnoseConnectFailure(NULL) returned %lu.\n", ret );
    }

    FreeLibrary( module );
    winetest_pop_context();
}

START_TEST( firewallapi )
{
    test_netisolation_apiset();
    test_diagnose_connect_failure();
}
