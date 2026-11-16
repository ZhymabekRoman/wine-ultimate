/*
 * windows.system.dll: activation factories for the Windows.System namespace root
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

#include "initguid.h"
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(winsystem);

/* Windows.System.Diagnostics.Telemetry.PlatformTelemetryClient (OfficeHub
 * telemetry probe): a single heap object serving both the server-side
 * IActivationFactory face (ActivateInstance at slot 3) and the
 * IPlatformTelemetryClient WinRT face (prologue 0-5, ~40 methods after),
 * answered by a generic S_OK stub that zeroes its first argument so
 * single-out callers (GetTelemetryGuid, get_AppId, ReportCriticalIssue,
 * event-registration tokens) see empty values instead of stack garbage.
 * The heap vtbl with per-slot stores matches the webview2loader pattern
 * (relocated shared .rdata pages go stale across app/child image bases). */
struct telemetry_client
{
    const void **vtbl;
    LONG ref;
};

static HRESULT WINAPI telemetry_QueryInterface( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}

static ULONG WINAPI telemetry_AddRef( void *t )
{
    struct telemetry_client *c = t;
    return InterlockedIncrement( &c->ref );
}

static ULONG WINAPI telemetry_Release( void *t )
{
    struct telemetry_client *c = t;
    return InterlockedDecrement( &c->ref );
}

static HRESULT WINAPI telemetry_ActivateInstance( void *t, IInspectable **instance )
{
    struct telemetry_client *c = t;
    telemetry_AddRef( c );
    *instance = (IInspectable *)c;
    return S_OK;
}

static HRESULT WINAPI telemetry_stub( void *t, void *arg, void *arg2 )
{
    if (arg) *(void **)arg = NULL;
    if (arg2) *(void **)arg2 = NULL;
    return S_OK;
}

/* IMapView-like face handed out by client slot 6 (GetContextProperties /
 * Get shape): the app chains client->slot6(out=map), map->slot6(out=count)
 * (GetMany), so the map's vtbl is all S_OK stubs that zero their outs. */
static const void **telemetry_map_vtbl;

static HRESULT WINAPI telemetry_map_QI( void *t, REFIID riid, void **obj )
{
    *obj = t;
    return S_OK;
}

static struct telemetry_client *create_telemetry_map( void )
{
    static struct telemetry_client map_obj = { NULL, 1 };
    const void **vt;
    unsigned int i;

    if (telemetry_map_vtbl) return &map_obj;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 32 * sizeof(*vt) ))) return NULL;
    for (i = 0; i < 32; i++) vt[i] = telemetry_stub;
    vt[0] = telemetry_map_QI;
    vt[1] = telemetry_AddRef;
    vt[2] = telemetry_Release;
    telemetry_map_vtbl = vt;
    map_obj.vtbl = vt;
    return &map_obj;
}

static HRESULT WINAPI telemetry_get( void *t, void *key, void *value )
{
    struct telemetry_client *m;
    if (key) *(void **)key = NULL;
    if (!value) return S_OK;
    if ((m = create_telemetry_map())) *(void **)value = m;
    return S_OK;
}

static struct telemetry_client *create_telemetry_client( void )
{
    static struct telemetry_client *client;
    struct telemetry_client *c;
    const void **vt;
    unsigned int i;

    if (client) return client;
    if (!(c = HeapAlloc( GetProcessHeap(), 0, sizeof(*c) ))) return NULL;
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 48 * sizeof(*vt) )))
    {
        HeapFree( GetProcessHeap(), 0, c );
        return NULL;
    }
    for (i = 0; i < 48; i++) vt[i] = telemetry_stub;
    vt[0] = telemetry_QueryInterface;
    vt[1] = telemetry_AddRef;
    vt[2] = telemetry_Release;
    vt[3] = telemetry_ActivateInstance;
    vt[6] = telemetry_get;
    c->vtbl = vt;
    c->ref = 1;
    client = c;
    return client;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_System_User ))
        IActivationFactory_QueryInterface( user_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_System_Diagnostics_Telemetry_PlatformTelemetryClient ))
    {
        struct telemetry_client *c = create_telemetry_client();
        if (c) IActivationFactory_QueryInterface( (IActivationFactory *)c, &IID_IActivationFactory,
                                                  (void **)factory );
    }

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}
