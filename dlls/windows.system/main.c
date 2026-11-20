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

static HRESULT WINAPI telemetry_QueryInterface( void *t, REFIID riid, void **obj )
{
    if (!obj) return E_POINTER;
    telemetry_AddRef( t );
    *obj = t;
    return S_OK;
}

static HRESULT WINAPI telemetry_ActivateInstance( void *t, IInspectable **instance )
{
    struct telemetry_client *c = t;
    if (!instance) return E_POINTER;
    telemetry_AddRef( c );
    *instance = (IInspectable *)c;
    return S_OK;
}

static HRESULT WINAPI telemetry_stub( void *t )
{
    return E_NOTIMPL;
}

/* IPlatformTelemetryRegistrationResult face:
 *   slot 6: get_Status([out, retval] PlatformTelemetryRegistrationStatus *value) -> 4-byte UINT32 (0 = Success)
 */
static HRESULT WINAPI telemetry_result_get_Status( void *t, UINT32 *status )
{
    if (status) *status = 0;
    return S_OK;
}

static const void **telemetry_map_vtbl;

static HRESULT WINAPI telemetry_map_QI( void *t, REFIID riid, void **obj )
{
    if (!obj) return E_POINTER;
    telemetry_AddRef( t );
    *obj = t;
    return S_OK;
}

static struct telemetry_client *create_telemetry_map( void )
{
    static struct telemetry_client map_obj = { NULL, 1 };
    const void **vt;
    unsigned int i;

    if (telemetry_map_vtbl)
    {
        telemetry_AddRef( &map_obj );
        return &map_obj;
    }
    if (!(vt = HeapAlloc( GetProcessHeap(), 0, 32 * sizeof(*vt) ))) return NULL;
    for (i = 0; i < 32; i++) vt[i] = telemetry_stub;
    vt[0] = telemetry_map_QI;
    vt[1] = telemetry_AddRef;
    vt[2] = telemetry_Release;
    vt[6] = telemetry_result_get_Status;
    telemetry_map_vtbl = vt;
    map_obj.vtbl = vt;
    telemetry_AddRef( &map_obj );
    return &map_obj;
}

/* IPlatformTelemetryClientStatics:
 *   slot 6: Register(HSTRING id, IPlatformTelemetryRegistrationResult **result)
 *   slot 7: RegisterWithSettings(HSTRING id, IPlatformTelemetryRegistrationSettings *settings,
 *                                IPlatformTelemetryRegistrationResult **result)
 */
static HRESULT WINAPI telemetry_register( void *t, HSTRING id, void **result )
{
    struct telemetry_client *m;
    if (!result) return E_POINTER;
    *result = NULL;
    if ((m = create_telemetry_map())) *result = m;
    return S_OK;
}

static HRESULT WINAPI telemetry_register_with_settings( void *t, HSTRING id, void *settings, void **result )
{
    struct telemetry_client *m;
    if (!result) return E_POINTER;
    *result = NULL;
    if ((m = create_telemetry_map())) *result = m;
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
    vt[6] = telemetry_register;
    vt[7] = telemetry_register_with_settings;
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

/* Windows.System.UserPicker */
static const GUID IID_IUserPicker =
    { 0x7d548008, 0xf1e3, 0x4a6c, { 0x8d, 0xdc, 0xa9, 0xbb, 0x0f, 0x48, 0x8a, 0xed } };
static const GUID IID_IUserPickerStatics =
    { 0xde3290dc, 0x7e73, 0x4df6, { 0xa1, 0xae, 0x4d, 0x7e, 0xca, 0x82, 0xb4, 0x0d } };
static const GUID IID_IAsyncOperation_User_picker =
    { 0x7f131288, 0x5c96, 0x58e4, { 0x93, 0x3b, 0x62, 0x20, 0x1e, 0x6f, 0x49, 0x3f } };

struct user_picker
{
    const void **vtbl;
    LONG ref;
    boolean allow_guest;
    IUser *suggested_user;
};

static HRESULT WINAPI user_picker_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct user_picker *impl = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUserPicker ))
    {
        InterlockedIncrement( &impl->ref );
        *out = impl;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI user_picker_AddRef( void *iface )
{
    struct user_picker *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI user_picker_Release( void *iface )
{
    struct user_picker *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->suggested_user) user_release( impl->suggested_user );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI user_picker_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI user_picker_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.System.UserPicker";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}

static HRESULT WINAPI user_picker_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI user_picker_get_AllowGuestAccounts( void *iface, boolean *value )
{
    struct user_picker *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->allow_guest;
    return S_OK;
}

static HRESULT WINAPI user_picker_put_AllowGuestAccounts( void *iface, boolean value )
{
    struct user_picker *impl = iface;
    impl->allow_guest = value;
    return S_OK;
}

static HRESULT WINAPI user_picker_get_SuggestedSelectedUser( void *iface, IUser **value )
{
    struct user_picker *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->suggested_user;
    if (*value) user_add_ref( *value );
    return S_OK;
}

static HRESULT WINAPI user_picker_put_SuggestedSelectedUser( void *iface, IUser *value )
{
    struct user_picker *impl = iface;
    if (value) user_add_ref( value );
    if (impl->suggested_user) user_release( impl->suggested_user );
    impl->suggested_user = value;
    return S_OK;
}

static HRESULT WINAPI user_picker_PickSingleUserAsync( void *iface, void **operation )
{
    struct async_op *op;
    void *user = NULL;

    if (!operation) return E_POINTER;
    *operation = NULL;
    create_user( &user );
    op = create_async_op( &IID_IAsyncOperation_User_picker,
                          L"Windows.Foundation.IAsyncOperation`1<Windows.System.User>",
                          (IInspectable *)user );
    if (user) IInspectable_Release( (IInspectable *)user );
    if (!op) return E_OUTOFMEMORY;
    *operation = async_op_iface( op );
    return S_OK;
}

static const void *user_picker_vtbl[] =
{
    user_picker_QueryInterface,
    user_picker_AddRef,
    user_picker_Release,
    user_picker_GetIids,
    user_picker_GetRuntimeClassName,
    user_picker_GetTrustLevel,
    user_picker_get_AllowGuestAccounts,
    user_picker_put_AllowGuestAccounts,
    user_picker_get_SuggestedSelectedUser,
    user_picker_put_SuggestedSelectedUser,
    user_picker_PickSingleUserAsync,
};

struct user_picker_statics
{
    const void **vtbl_factory;
    const void **vtbl_statics;
};

static struct user_picker_statics user_picker_statics_obj;

static HRESULT WINAPI user_picker_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &user_picker_statics_obj.vtbl_factory;
    else if (IsEqualGUID( iid, &IID_IUserPickerStatics ))
        *out = &user_picker_statics_obj.vtbl_statics;
    else
        return E_NOINTERFACE;
    return S_OK;
}

static ULONG WINAPI user_picker_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI user_picker_statics_Release( void *iface ) { return 1; }

static HRESULT WINAPI user_picker_statics_ActivateInstance( void *iface, IInspectable **instance )
{
    struct user_picker *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl = user_picker_vtbl;
    impl->ref = 1;
    *instance = (IInspectable *)impl;
    return S_OK;
}

static HRESULT WINAPI user_picker_statics_IsSupported( void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const void *user_picker_factory_vtbl[] =
{
    user_picker_statics_QueryInterface,
    user_picker_statics_AddRef,
    user_picker_statics_Release,
    user_picker_GetIids,
    user_picker_GetRuntimeClassName,
    user_picker_GetTrustLevel,
    user_picker_statics_ActivateInstance,
};

static const void *user_picker_statics_vtbl[] =
{
    user_picker_statics_QueryInterface,
    user_picker_statics_AddRef,
    user_picker_statics_Release,
    user_picker_GetIids,
    user_picker_GetRuntimeClassName,
    user_picker_GetTrustLevel,
    user_picker_statics_IsSupported,
};

static struct user_picker_statics user_picker_statics_obj =
{
    user_picker_factory_vtbl,
    user_picker_statics_vtbl,
};

/* ============================================================================
 * Windows.System.Diagnostics.SystemDiagnosticInfo
 * ============================================================================ */

static const GUID IID_ISystemDiagnosticInfoStatics =
    { 0xd404ac21, 0xfc7d, 0x45f0, {0x9a, 0x3f, 0x39, 0x20, 0x3a, 0xed, 0x9f, 0x7e} };
static const GUID IID_ISystemDiagnosticInfoStatics2 =
    { 0x79ded189, 0x6af9, 0x4da9, {0xa4, 0x22, 0x15, 0xf7, 0x32, 0x55, 0xb3, 0xeb} };
static const GUID IID_ISystemDiagnosticInfo =
    { 0xa290fe05, 0xdff3, 0x407f, {0x9a, 0x1b, 0x0b, 0x2b, 0x31, 0x7c, 0xa8, 0x00} };
static const GUID IID_ISystemMemoryUsage =
    { 0x17ffc595, 0x1702, 0x49cf, {0xaa, 0x27, 0x2f, 0x0a, 0x32, 0x59, 0x14, 0x04} };
static const GUID IID_ISystemMemoryUsageReport =
    { 0x38663c87, 0x2a9f, 0x403a, {0xbd, 0x19, 0x2c, 0xf3, 0xe8, 0x16, 0x95, 0x00} };
static const GUID IID_ISystemCpuUsage =
    { 0x6037b3ac, 0x02d6, 0x4234, {0x83, 0x62, 0x7f, 0xe3, 0xad, 0xc8, 0x1f, 0x5f} };
static const GUID IID_ISystemCpuUsageReport =
    { 0x2c26d0b2, 0x9483, 0x4f62, {0xab, 0x57, 0x82, 0xb2, 0x9d, 0x97, 0x19, 0xb8} };

struct sys_mem_report
{
    const void *lpVtbl;
    LONG ref;
    UINT64 total_phys;
    UINT64 avail_phys;
    UINT64 committed;
};

static HRESULT WINAPI smr_QI( void *iface, REFIID iid, void **out )
{
    struct sys_mem_report *impl = (struct sys_mem_report *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISystemMemoryUsageReport ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI smr_AddRef( void *iface ) { return InterlockedIncrement( &((struct sys_mem_report *)iface)->ref ); }
static ULONG WINAPI smr_Release( void *iface )
{
    struct sys_mem_report *impl = (struct sys_mem_report *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI smr_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_ISystemMemoryUsageReport;
    *c = 1;
    return S_OK;
}
static HRESULT WINAPI smr_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.System.Diagnostics.SystemMemoryUsageReport";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI smr_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI smr_get_TotalPhysical( void *iface, UINT64 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct sys_mem_report *)iface)->total_phys;
    return S_OK;
}
static HRESULT WINAPI smr_get_Available( void *iface, UINT64 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct sys_mem_report *)iface)->avail_phys;
    return S_OK;
}
static HRESULT WINAPI smr_get_Committed( void *iface, UINT64 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct sys_mem_report *)iface)->committed;
    return S_OK;
}

static const void * const smr_vtbl[] =
{
    smr_QI, smr_AddRef, smr_Release, smr_GetIids, smr_GetRuntimeClassName, smr_GetTrustLevel,
    smr_get_TotalPhysical, smr_get_Available, smr_get_Committed,
};

struct sys_mem_usage { const void *lpVtbl; LONG ref; };
static HRESULT WINAPI smu_QI( void *iface, REFIID iid, void **out )
{
    struct sys_mem_usage *impl = (struct sys_mem_usage *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISystemMemoryUsage ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI smu_AddRef( void *iface ) { return InterlockedIncrement( &((struct sys_mem_usage *)iface)->ref ); }
static ULONG WINAPI smu_Release( void *iface )
{
    struct sys_mem_usage *impl = (struct sys_mem_usage *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI smu_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_ISystemMemoryUsage;
    *c = 1;
    return S_OK;
}
static HRESULT WINAPI smu_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.System.Diagnostics.SystemMemoryUsage";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI smu_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI smu_GetReport( void *iface, void **out )
{
    struct sys_mem_report *r;
    MEMORYSTATUSEX ms;
    if (!out) return E_POINTER;
    if (!(r = calloc( 1, sizeof(*r) ))) return E_OUTOFMEMORY;
    r->lpVtbl = smr_vtbl;
    r->ref = 1;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx( &ms ))
    {
        r->total_phys = ms.ullTotalPhys;
        r->avail_phys = ms.ullAvailPhys;
        r->committed = ms.ullTotalPageFile > ms.ullAvailPageFile ? ms.ullTotalPageFile - ms.ullAvailPageFile : 0;
    }
    *out = r;
    return S_OK;
}
static const void * const smu_vtbl[] =
{
    smu_QI, smu_AddRef, smu_Release, smu_GetIids, smu_GetRuntimeClassName, smu_GetTrustLevel,
    smu_GetReport,
};

struct sys_cpu_report
{
    const void *lpVtbl;
    LONG ref;
    INT64 kernel_time;
    INT64 user_time;
    INT64 idle_time;
};

static HRESULT WINAPI scr_QI( void *iface, REFIID iid, void **out )
{
    struct sys_cpu_report *impl = (struct sys_cpu_report *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISystemCpuUsageReport ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI scr_AddRef( void *iface ) { return InterlockedIncrement( &((struct sys_cpu_report *)iface)->ref ); }
static ULONG WINAPI scr_Release( void *iface )
{
    struct sys_cpu_report *impl = (struct sys_cpu_report *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI scr_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_ISystemCpuUsageReport;
    *c = 1;
    return S_OK;
}
static HRESULT WINAPI scr_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.System.Diagnostics.SystemCpuUsageReport";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI scr_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI scr_get_KernelTime( void *iface, INT64 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct sys_cpu_report *)iface)->kernel_time;
    return S_OK;
}
static HRESULT WINAPI scr_get_UserTime( void *iface, INT64 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct sys_cpu_report *)iface)->user_time;
    return S_OK;
}
static HRESULT WINAPI scr_get_IdleTime( void *iface, INT64 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct sys_cpu_report *)iface)->idle_time;
    return S_OK;
}
static const void * const scr_vtbl[] =
{
    scr_QI, scr_AddRef, scr_Release, scr_GetIids, scr_GetRuntimeClassName, scr_GetTrustLevel,
    scr_get_KernelTime, scr_get_UserTime, scr_get_IdleTime,
};

struct sys_cpu_usage { const void *lpVtbl; LONG ref; };
static HRESULT WINAPI scu_QI( void *iface, REFIID iid, void **out )
{
    struct sys_cpu_usage *impl = (struct sys_cpu_usage *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISystemCpuUsage ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI scu_AddRef( void *iface ) { return InterlockedIncrement( &((struct sys_cpu_usage *)iface)->ref ); }
static ULONG WINAPI scu_Release( void *iface )
{
    struct sys_cpu_usage *impl = (struct sys_cpu_usage *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI scu_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_ISystemCpuUsage;
    *c = 1;
    return S_OK;
}
static HRESULT WINAPI scu_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.System.Diagnostics.SystemCpuUsage";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI scu_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI scu_GetReport( void *iface, void **out )
{
    struct sys_cpu_report *r;
    FILETIME create, exit_t, kernel, user;
    if (!out) return E_POINTER;
    if (!(r = calloc( 1, sizeof(*r) ))) return E_OUTOFMEMORY;
    r->lpVtbl = scr_vtbl;
    r->ref = 1;
    if (GetProcessTimes( GetCurrentProcess(), &create, &exit_t, &kernel, &user ))
    {
        r->kernel_time = ((INT64)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
        r->user_time = ((INT64)user.dwHighDateTime << 32) | user.dwLowDateTime;
    }
    r->idle_time = (INT64)GetTickCount64() * 10000;
    *out = r;
    return S_OK;
}
static const void * const scu_vtbl[] =
{
    scu_QI, scu_AddRef, scu_Release, scu_GetIids, scu_GetRuntimeClassName, scu_GetTrustLevel,
    scu_GetReport,
};

struct sys_diag_info { const void *lpVtbl; LONG ref; };
static HRESULT WINAPI sdi_QI( void *iface, REFIID iid, void **out )
{
    struct sys_diag_info *impl = (struct sys_diag_info *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISystemDiagnosticInfo ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sdi_AddRef( void *iface ) { return InterlockedIncrement( &((struct sys_diag_info *)iface)->ref ); }
static ULONG WINAPI sdi_Release( void *iface )
{
    struct sys_diag_info *impl = (struct sys_diag_info *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI sdi_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_ISystemDiagnosticInfo;
    *c = 1;
    return S_OK;
}
static HRESULT WINAPI sdi_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.System.Diagnostics.SystemDiagnosticInfo";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI sdi_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI sdi_get_MemoryUsage( void *iface, void **out )
{
    struct sys_mem_usage *u;
    if (!out) return E_POINTER;
    if (!(u = calloc( 1, sizeof(*u) ))) return E_OUTOFMEMORY;
    u->lpVtbl = smu_vtbl;
    u->ref = 1;
    *out = u;
    return S_OK;
}
static HRESULT WINAPI sdi_get_CpuUsage( void *iface, void **out )
{
    struct sys_cpu_usage *u;
    if (!out) return E_POINTER;
    if (!(u = calloc( 1, sizeof(*u) ))) return E_OUTOFMEMORY;
    u->lpVtbl = scu_vtbl;
    u->ref = 1;
    *out = u;
    return S_OK;
}
static const void * const sdi_vtbl[] =
{
    sdi_QI, sdi_AddRef, sdi_Release, sdi_GetIids, sdi_GetRuntimeClassName, sdi_GetTrustLevel,
    sdi_get_MemoryUsage, sdi_get_CpuUsage,
};

struct sys_diag_factory
{
    const void *lpVtbl_factory;
    const void *lpVtbl_statics;
    const void *lpVtbl_statics2;
};
static struct sys_diag_factory sys_diag_factory_obj;

static HRESULT WINAPI sdi_fac_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &sys_diag_factory_obj.lpVtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISystemDiagnosticInfoStatics ))
    {
        *out = &sys_diag_factory_obj.lpVtbl_statics;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISystemDiagnosticInfoStatics2 ))
    {
        *out = &sys_diag_factory_obj.lpVtbl_statics2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sdi_fac_AddRef( void *iface ) { return 2; }
static ULONG WINAPI sdi_fac_Release( void *iface ) { return 1; }
static HRESULT WINAPI sdi_fac_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *c = 0; *i = NULL;
    return S_OK;
}
static HRESULT WINAPI sdi_fac_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.System.Diagnostics.SystemDiagnosticInfo";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI sdi_fac_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI sdi_fac_ActivateInstance( void *iface, IInspectable **out )
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI sdi_statics_GetForCurrentSystem( void *iface, void **out )
{
    struct sys_diag_info *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = sdi_vtbl;
    impl->ref = 1;
    *out = impl;
    return S_OK;
}
static HRESULT WINAPI sdi_statics2_IsArchitectureSupported( void *iface, INT32 arch, boolean *out )
{
    if (!out) return E_POINTER;
    /* ProcessorArchitecture_X64 = 9, X86 = 0 */
    *out = (arch == 9 || arch == 0);
    return S_OK;
}
static HRESULT WINAPI sdi_statics2_get_PreferredArchitecture( void *iface, INT32 *out )
{
    if (!out) return E_POINTER;
    *out = 9; /* ProcessorArchitecture_X64 */
    return S_OK;
}

static const void * const sdi_factory_vtbl[] =
{
    sdi_fac_QI, sdi_fac_AddRef, sdi_fac_Release,
    sdi_fac_GetIids, sdi_fac_GetRuntimeClassName, sdi_fac_GetTrustLevel,
    sdi_fac_ActivateInstance,
};
static const void * const sdi_statics_vtbl[] =
{
    sdi_fac_QI, sdi_fac_AddRef, sdi_fac_Release,
    sdi_fac_GetIids, sdi_fac_GetRuntimeClassName, sdi_fac_GetTrustLevel,
    sdi_statics_GetForCurrentSystem,
};
static const void * const sdi_statics2_vtbl[] =
{
    sdi_fac_QI, sdi_fac_AddRef, sdi_fac_Release,
    sdi_fac_GetIids, sdi_fac_GetRuntimeClassName, sdi_fac_GetTrustLevel,
    sdi_statics2_IsArchitectureSupported, sdi_statics2_get_PreferredArchitecture,
};
static struct sys_diag_factory sys_diag_factory_obj =
{
    sdi_factory_vtbl,
    sdi_statics_vtbl,
    sdi_statics2_vtbl,
};

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_System_User ))
        IActivationFactory_QueryInterface( user_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, L"Windows.System.Launcher" ))
        IActivationFactory_QueryInterface( launcher_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, L"Windows.System.UserPicker" ))
        IActivationFactory_QueryInterface( (IActivationFactory *)&user_picker_statics_obj.vtbl_factory,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_System_Diagnostics_Telemetry_PlatformTelemetryClient ))
    {
        struct telemetry_client *c = create_telemetry_client();
        if (c) IActivationFactory_QueryInterface( (IActivationFactory *)c, &IID_IActivationFactory,
                                                  (void **)factory );
    }
    else if (!wcscmp( name, L"Windows.System.Diagnostics.SystemDiagnosticInfo" ))
        IActivationFactory_QueryInterface( (IActivationFactory *)&sys_diag_factory_obj.lpVtbl_factory,
                                           &IID_IActivationFactory, (void **)factory );
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
