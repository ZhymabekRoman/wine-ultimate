/*
 * Net isolation stubs (firewallapi.dll / api-ms-win-net-isolation-l1-1-1)
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

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(firewallapi);

/*
 * Network isolation (the AppContainer per-app firewall) does not exist for win32 processes, so the
 * whole netisolation surface reports "no isolation in effect": every query succeeds, every rule set
 * is empty, and every status flag comes back cleared. Callers (UWP network brokers, XAML hosting)
 * only use these to decide whether to apply isolation rules; the cleared answers keep them on the
 * plain path. In-box Windows has the same answers for a process that is not inside a container.
 */

DWORD WINAPI NetworkIsolationDiagnoseConnectFailure( const WCHAR *server )
{
    TRACE( "%s\n", debugstr_w(server) );
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationDiagnoseConnectFailureGetMessage( const WCHAR *server, WCHAR *message )
{
    TRACE( "%s %p\n", debugstr_w(server), message );
    if (message) message[0] = 0; /* no diagnosis, so no message to format */
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationFreeMessage( void *message )
{
    TRACE( "%p\n", message );
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationGetAppContainerConfig( void *rules )
{
    TRACE( "%p\n", rules );
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationGetAppContainerPrivateNetworkStatus( const WCHAR *sid, DWORD *status )
{
    TRACE( "%s %p\n", debugstr_w(sid), status );
    if (status) *status = 0;
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationIsAppLocallyBridged( const WCHAR *sid, BOOL *is_bridged )
{
    TRACE( "%s %p\n", debugstr_w(sid), is_bridged );
    if (is_bridged) *is_bridged = FALSE;
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationIsConnected( BOOL *connected )
{
    TRACE( "%p\n", connected );
    if (connected) *connected = FALSE;
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationIsDomainJoined( BOOL *domain_joined )
{
    TRACE( "%p\n", domain_joined );
    if (domain_joined) *domain_joined = FALSE;
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationIsEnterpriseReady( const WCHAR *arg, BOOL *enterprise_ready )
{
    TRACE( "%s %p\n", debugstr_w(arg), enterprise_ready );
    if (enterprise_ready) *enterprise_ready = FALSE;
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationIsPrivateNetworkPresent( const WCHAR *sid, BOOL *is_present )
{
    TRACE( "%s %p\n", debugstr_w(sid), is_present );
    if (is_present) *is_present = FALSE;
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationIsPublisherRestricted( const WCHAR *sid, BOOL *is_restricted )
{
    TRACE( "%s %p\n", debugstr_w(sid), is_restricted );
    if (is_restricted) *is_restricted = FALSE;
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationRegisterAppContainerNotification( const WCHAR *sid, DWORD type, void *callback, void *context )
{
    TRACE( "%s %u %p %p\n", debugstr_w(sid), type, callback, context );
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationSetAppContainerConfig( const void *rules )
{
    TRACE( "%p\n", rules );
    return ERROR_SUCCESS;
}

DWORD WINAPI NetworkIsolationUnregisterAppContainerNotification( const WCHAR *sid )
{
    TRACE( "%s\n", debugstr_w(sid) );
    return ERROR_SUCCESS;
}
