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

#include <stdlib.h>

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

/***********************************************************************
 * Windows.Management.Policies.NamedPolicy
 *
 * Policy store face mspaint probes at startup.  The class is routed here by
 * dlls/combase/roapi.c static_exact_routes; INamedPolicyStatics
 * {7f793be7-76c4-4058-8cad-67662cd05f0d} (GetPolicyFromPath slot 6,
 * GetPolicyFromPathForUser slot 7) hands out INamedPolicy
 * {38dcb198-95ac-4077-a643-8078cae26400} objects reporting an unmanaged,
 * non-user policy with Kind=Unknown(1) and empty values, which is the
 * documented answer on a machine with no policy service.  The object also
 * answers IWeakReferenceSource with a strong-reference weak ref.
 */
static const GUID IID_INamedPolicyStatics =
    { 0x7f793be7, 0x76c4, 0x4058, { 0x8c, 0xad, 0x67, 0x66, 0x2c, 0xd0, 0x5f, 0x0d } };
static const GUID IID_INamedPolicy =
    { 0x38dcb198, 0x95ac, 0x4077, { 0xa6, 0x43, 0x80, 0x78, 0xca, 0xe2, 0x64, 0x00 } };
static const GUID IID_INamedPolicy_IWeakReferenceSource =
    { 0x0000003c, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_INamedPolicy_IWeakReference =
    { 0x00000037, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

struct named_policy
{
    const void **vtbl;              /* INamedPolicy / IInspectable face */
    const void **vtbl_weak_source;  /* IWeakReferenceSource face */
    LONG ref;
    HSTRING area;
    HSTRING name;
};

struct named_policy_statics
{
    const void **vtbl_factory;      /* IActivationFactory face */
    const void **vtbl_statics;      /* INamedPolicyStatics face */
};
static struct named_policy_statics named_policy_statics_obj;

static HRESULT named_policy_qi( struct named_policy *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INamedPolicy ))
        *out = &impl->vtbl;
    else if (IsEqualGUID( iid, &IID_INamedPolicy_IWeakReferenceSource ))
        *out = &impl->vtbl_weak_source;
    else
        return E_NOINTERFACE;

    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI named_policy_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct named_policy *impl = CONTAINING_RECORD( iface, struct named_policy, vtbl );
    return named_policy_qi( impl, iid, out );
}

static ULONG WINAPI named_policy_AddRef( void *iface )
{
    struct named_policy *impl = CONTAINING_RECORD( iface, struct named_policy, vtbl );
    return InterlockedIncrement( &impl->ref );
}

static ULONG named_policy_release( struct named_policy *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->area );
        WindowsDeleteString( impl->name );
        free( impl );
    }
    return ref;
}

static ULONG WINAPI named_policy_Release( void *iface )
{
    return named_policy_release( CONTAINING_RECORD( iface, struct named_policy, vtbl ) );
}

static HRESULT WINAPI named_policy_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI named_policy_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Management.Policies.NamedPolicy";
    return WindowsCreateString( class_name, ARRAY_SIZE( class_name ) - 1, name );
}

static HRESULT WINAPI named_policy_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI named_policy_get_Area( void *iface, HSTRING *value )
{
    struct named_policy *impl = CONTAINING_RECORD( iface, struct named_policy, vtbl );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->area, value );
}

static HRESULT WINAPI named_policy_get_Name( void *iface, HSTRING *value )
{
    struct named_policy *impl = CONTAINING_RECORD( iface, struct named_policy, vtbl );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->name, value );
}

static HRESULT WINAPI named_policy_get_Kind( void *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = 1; /* NamedPolicyKind_Unknown: no policy service in a prefix */
    return S_OK;
}

static HRESULT WINAPI named_policy_get_IsManaged( void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI named_policy_get_IsUserPolicy( void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI named_policy_get_User( void *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI named_policy_GetBoolean( void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI named_policy_GetBinary( void *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI named_policy_GetInt32( void *iface, INT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI named_policy_GetInt64( void *iface, INT64 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI named_policy_GetString( void *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsCreateString( L"", 0, value );
}

static HRESULT WINAPI named_policy_add_Changed( void *iface, IInspectable *handler,
                                                EventRegistrationToken *token )
{
    (void)handler;
    if (!token) return E_POINTER;
    token->value = 0;
    return S_OK;
}

static HRESULT WINAPI named_policy_remove_Changed( void *iface, EventRegistrationToken token,
                                                   IInspectable **handler )
{
    (void)token;
    if (handler) *handler = NULL;
    return S_OK;
}

static const void *named_policy_vtbl[] =
{
    named_policy_QueryInterface,
    named_policy_AddRef,
    named_policy_Release,
    named_policy_GetIids,
    named_policy_GetRuntimeClassName,
    named_policy_GetTrustLevel,
    named_policy_get_Area,        /* 6 */
    named_policy_get_Name,
    named_policy_get_Kind,
    named_policy_get_IsManaged,
    named_policy_get_IsUserPolicy,
    named_policy_get_User,
    named_policy_GetBoolean,
    named_policy_GetBinary,
    named_policy_GetInt32,
    named_policy_GetInt64,
    named_policy_GetString,
    named_policy_add_Changed,     /* 17 */
    named_policy_remove_Changed,  /* 18 */
};

/* IWeakReferenceSource face */
static HRESULT WINAPI named_policy_weak_source_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct named_policy *impl = CONTAINING_RECORD( iface, struct named_policy, vtbl_weak_source );
    return named_policy_qi( impl, iid, out );
}

static ULONG WINAPI named_policy_weak_source_AddRef( void *iface )
{
    struct named_policy *impl = CONTAINING_RECORD( iface, struct named_policy, vtbl_weak_source );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI named_policy_weak_source_Release( void *iface )
{
    return named_policy_release( CONTAINING_RECORD( iface, struct named_policy, vtbl_weak_source ) );
}

struct named_policy_weak_reference
{
    const void **vtbl;
    LONG ref;
    struct named_policy *owner;
};

static HRESULT WINAPI named_policy_weakref_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct named_policy_weak_reference *impl =
        CONTAINING_RECORD( iface, struct named_policy_weak_reference, vtbl );

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_INamedPolicy_IWeakReference ))
        *out = &impl->vtbl;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI named_policy_weakref_AddRef( void *iface )
{
    struct named_policy_weak_reference *impl =
        CONTAINING_RECORD( iface, struct named_policy_weak_reference, vtbl );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI named_policy_weakref_Release( void *iface )
{
    struct named_policy_weak_reference *impl =
        CONTAINING_RECORD( iface, struct named_policy_weak_reference, vtbl );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        struct named_policy *owner = impl->owner;
        free( impl );
        named_policy_release( owner );
    }
    return ref;
}

static HRESULT WINAPI named_policy_weakref_Resolve( void *iface, REFIID iid, void **obj )
{
    struct named_policy_weak_reference *impl =
        CONTAINING_RECORD( iface, struct named_policy_weak_reference, vtbl );
    return named_policy_qi( impl->owner, iid, obj );
}

static const void *named_policy_weakref_vtbl[] =
{
    named_policy_weakref_QueryInterface,
    named_policy_weakref_AddRef,
    named_policy_weakref_Release,
    named_policy_weakref_Resolve,
};

static HRESULT WINAPI named_policy_weak_source_GetWeakReference( void *iface, void **out )
{
    struct named_policy *impl = CONTAINING_RECORD( iface, struct named_policy, vtbl_weak_source );
    struct named_policy_weak_reference *weak;

    if (!out) return E_POINTER;
    if (!(weak = calloc( 1, sizeof(*weak) ))) return E_OUTOFMEMORY;
    weak->vtbl = named_policy_weakref_vtbl;
    weak->ref = 1;
    weak->owner = impl;
    InterlockedIncrement( &impl->ref );
    *out = &weak->vtbl;
    return S_OK;
}

static const void *named_policy_weak_source_vtbl[] =
{
    named_policy_weak_source_QueryInterface,
    named_policy_weak_source_AddRef,
    named_policy_weak_source_Release,
    named_policy_GetIids,
    named_policy_GetRuntimeClassName,
    named_policy_GetTrustLevel,
    named_policy_weak_source_GetWeakReference, /* 6 */
};

static HRESULT create_named_policy( HSTRING area, HSTRING name, void **out )
{
    struct named_policy *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl = named_policy_vtbl;
    impl->vtbl_weak_source = named_policy_weak_source_vtbl;
    impl->ref = 1;
    WindowsDuplicateString( area, &impl->area );
    WindowsDuplicateString( name, &impl->name );
    *out = &impl->vtbl;
    return S_OK;
}

/* IActivationFactory face */
static HRESULT WINAPI named_policy_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &named_policy_statics_obj.vtbl_factory;
    else if (IsEqualGUID( iid, &IID_INamedPolicyStatics ))
        *out = &named_policy_statics_obj.vtbl_statics;
    else
        return E_NOINTERFACE;
    return S_OK;
}

static ULONG WINAPI named_policy_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI named_policy_statics_Release( void *iface ) { return 1; }

static HRESULT WINAPI named_policy_statics_ActivateInstance( void *iface, IInspectable **instance )
{
    HRESULT hr;

    if (!instance) return E_POINTER;
    hr = create_named_policy( NULL, NULL, (void **)instance );
    return hr;
}

static HRESULT WINAPI named_policy_statics_GetPolicyFromPath( void *iface, HSTRING area,
                                                              HSTRING name, void **result )
{
    if (!result) return E_POINTER;
    return create_named_policy( area, name, result );
}

static HRESULT WINAPI named_policy_statics_GetPolicyFromPathForUser( void *iface,
                                                                     IInspectable *user,
                                                                     HSTRING area, HSTRING name,
                                                                     void **result )
{
    (void)user;
    if (!result) return E_POINTER;
    return create_named_policy( area, name, result );
}

static const void *named_policy_factory_vtbl[] =
{
    named_policy_statics_QueryInterface,
    named_policy_statics_AddRef,
    named_policy_statics_Release,
    named_policy_GetIids,
    named_policy_GetRuntimeClassName,
    named_policy_GetTrustLevel,
    named_policy_statics_ActivateInstance,
};

static const void *named_policy_statics_vtbl[] =
{
    named_policy_statics_QueryInterface,
    named_policy_statics_AddRef,
    named_policy_statics_Release,
    named_policy_GetIids,
    named_policy_GetRuntimeClassName,
    named_policy_GetTrustLevel,
    named_policy_statics_GetPolicyFromPath,
    named_policy_statics_GetPolicyFromPathForUser,
};

static struct named_policy_statics named_policy_statics_obj =
{
    named_policy_factory_vtbl,
    named_policy_statics_vtbl,
};

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
    else if (!wcscmp( name, L"Windows.Management.Policies.NamedPolicy" ))
        IActivationFactory_QueryInterface( (IActivationFactory *)&named_policy_statics_obj.vtbl_factory,
                                           &IID_IActivationFactory, (void **)factory );

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
