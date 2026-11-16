/*
 * Windows.System.Profile.EducationSettings and PlatformDiagnosticsAndUsageDataSettings
 * activation factories
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

#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(profile);

/*
 * Windows.System.Profile.EducationSettings hosts one statics interface, IEducationSettingsStatics
 * {fc53f0ef-4d3e-4e13-9b23-505f4d091e92}, whose vtable is the IInspectable prologue plus a single
 * read-only IsEducationEnvironment property.  Interface IID, single-slot layout and class name are
 * the windows-rs metadata projection (crates/libs/windows/src/Windows/System/Profile/mod.rs:226,
 * :235, :108); the projection has no other statics interface for this class, so the two boolean
 * getters some callers expect (IsEnabled/IsAvailable) and a GetEducationActivity* entry point do
 * not exist on the class and are not implemented here - inventing slots past an advertised IID
 * would corrupt the caller's vtable, which is the failure this surface must avoid.
 *
 * Documented contract of the one member (learn.microsoft.com "Education Settings. Is Education
 * Environment Property", Windows.System.Profile): "Gets a Boolean value indicating if the app is
 * running in an education environment." / "A Boolean value indicating if the app is running in an
 * education environment."  A Wine prefix has no education-mode provisioning (the real answer comes
 * from the Windows EducationManager/EDU policy store), so the honest builtin value is FALSE: the
 * app is not running in an education environment.  Returning TRUE would push callers down the
 * managed-device code path that this prefix cannot serve.
 *
 * The class carries no instance interface and no activatable constructor in the metadata, so
 * ActivateInstance answers the standard "class not activatable" failure like the sibling modules.
 */

struct education_settings_statics
{
    IActivationFactory IActivationFactory_iface;
    IEducationSettingsStatics IEducationSettingsStatics_iface;
    LONG ref;
};

static inline struct education_settings_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct education_settings_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct education_settings_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IEducationSettingsStatics ))
    {
        *out = &impl->IEducationSettingsStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct education_settings_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct education_settings_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_Profile_EducationSettings,
                                lstrlenW( RuntimeClass_Windows_System_Profile_EducationSettings ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;

    /* EducationSettings has no activatable constructor in the metadata (statics only). */
    return CLASS_E_CLASSNOTAVAILABLE;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( statics, IEducationSettingsStatics, struct education_settings_statics, IActivationFactory_iface )

static HRESULT WINAPI statics_get_IsEducationEnvironment( IEducationSettingsStatics *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;

    /* No education-mode provisioning exists in a Wine prefix; see the header comment. */
    *value = FALSE;
    return S_OK;
}

static const struct IEducationSettingsStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_get_IsEducationEnvironment,
};

static struct education_settings_statics education_settings_statics =
{
    { &factory_vtbl },
    { &statics_vtbl },
    1,
};

/*
 * Windows.System.Profile.PlatformDiagnosticsAndUsageDataSettings: the diagnostic
 * data settings of the device. Statics-only class. The CollectionLevelChanged
 * event registrations are kept in a list; nothing publishes level changes, the
 * list exists so add/remove round-trip on a token like on Windows.
 */
static SRWLOCK pd_event_lock = SRWLOCK_INIT;
static struct list pd_event_list = LIST_INIT( pd_event_list );
static INT64 pd_next_token;

struct pd_event_registration
{
    struct list entry;
    EventRegistrationToken token;
    __FIEventHandler_1_IInspectable *handler;
};

struct platform_diagnostics_statics
{
    IActivationFactory IActivationFactory_iface;
    IPlatformDiagnosticsAndUsageDataSettingsStatics IPlatformDiagnosticsAndUsageDataSettingsStatics_iface;
    LONG ref;
};

static inline struct platform_diagnostics_statics *pd_impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct platform_diagnostics_statics, IActivationFactory_iface );
}

static HRESULT WINAPI pd_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct platform_diagnostics_statics *impl = pd_impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPlatformDiagnosticsAndUsageDataSettingsStatics ))
    {
        *out = &impl->IPlatformDiagnosticsAndUsageDataSettingsStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI pd_factory_AddRef( IActivationFactory *iface )
{
    struct platform_diagnostics_statics *impl = pd_impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI pd_factory_Release( IActivationFactory *iface )
{
    struct platform_diagnostics_statics *impl = pd_impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI pd_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI pd_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_Profile_PlatformDiagnosticsAndUsageDataSettings,
                                lstrlenW( RuntimeClass_Windows_System_Profile_PlatformDiagnosticsAndUsageDataSettings ),
                                class_name );
}

static HRESULT WINAPI pd_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI pd_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;

    /* Statics-only class, no constructor in the metadata. */
    return CLASS_E_CLASSNOTAVAILABLE;
}

static const struct IActivationFactoryVtbl pd_factory_vtbl =
{
    pd_factory_QueryInterface,
    pd_factory_AddRef,
    pd_factory_Release,
    pd_factory_GetIids,
    pd_factory_GetRuntimeClassName,
    pd_factory_GetTrustLevel,
    pd_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( pd_statics, IPlatformDiagnosticsAndUsageDataSettingsStatics,
                     struct platform_diagnostics_statics, IActivationFactory_iface )

static HRESULT WINAPI pd_statics_get_CollectionLevel( IPlatformDiagnosticsAndUsageDataSettingsStatics *iface,
        PlatformDataCollectionLevel *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = PlatformDataCollectionLevel_Full;
    return S_OK;
}

static HRESULT WINAPI pd_statics_add_CollectionLevelChanged( IPlatformDiagnosticsAndUsageDataSettingsStatics *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    struct pd_event_registration *reg;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!handler || !token) return E_POINTER;
    if (!(reg = calloc( 1, sizeof(*reg) ))) return E_OUTOFMEMORY;

    reg->handler = handler;
    __FIEventHandler_1_IInspectable_AddRef( handler );
    reg->token.value = InterlockedIncrement64( &pd_next_token );

    AcquireSRWLockExclusive( &pd_event_lock );
    list_add_tail( &pd_event_list, &reg->entry );
    ReleaseSRWLockExclusive( &pd_event_lock );

    *token = reg->token;
    return S_OK;
}

static HRESULT WINAPI pd_statics_remove_CollectionLevelChanged( IPlatformDiagnosticsAndUsageDataSettingsStatics *iface,
        EventRegistrationToken token )
{
    struct pd_event_registration *reg, *cursor;
    BOOL found = FALSE;

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );

    if (!token.value) return E_INVALIDARG;

    AcquireSRWLockExclusive( &pd_event_lock );
    LIST_FOR_EACH_ENTRY_SAFE( reg, cursor, &pd_event_list, struct pd_event_registration, entry )
    {
        if (reg->token.value == token.value)
        {
            list_remove( &reg->entry );
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive( &pd_event_lock );

    if (found)
    {
        __FIEventHandler_1_IInspectable_Release( reg->handler );
        free( reg );
    }
    return S_OK;
}

static HRESULT WINAPI pd_statics_CanCollectDiagnostics( IPlatformDiagnosticsAndUsageDataSettingsStatics *iface,
        PlatformDataCollectionLevel level, boolean *result )
{
    TRACE( "iface %p, level %u, result %p.\n", iface, level, result );

    if (!result) return E_POINTER;
    *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI pd_statics_GetCurrentData( IPlatformDiagnosticsAndUsageDataSettingsStatics *iface,
        IPropertySet **value )
{
    IInspectable *inspectable;
    HSTRING class_id;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;

    /* The current diagnostic data is a plain property set; the app reads its
     * flight-ring keys out of it, an empty map is a valid answer. */
    if (FAILED( hr = WindowsCreateString( RuntimeClass_Windows_Foundation_Collections_PropertySet,
                                          lstrlenW( RuntimeClass_Windows_Foundation_Collections_PropertySet ),
                                          &class_id ) ))
        return hr;

    hr = RoActivateInstance( class_id, &inspectable );
    WindowsDeleteString( class_id );
    if (FAILED( hr )) return hr;

    hr = IInspectable_QueryInterface( inspectable, &IID_IPropertySet, (void **)value );
    IInspectable_Release( inspectable );
    return hr;
}

static const struct IPlatformDiagnosticsAndUsageDataSettingsStaticsVtbl pd_statics_vtbl =
{
    pd_statics_QueryInterface,
    pd_statics_AddRef,
    pd_statics_Release,
    pd_statics_GetIids,
    pd_statics_GetRuntimeClassName,
    pd_statics_GetTrustLevel,
    pd_statics_get_CollectionLevel,
    pd_statics_add_CollectionLevelChanged,
    pd_statics_remove_CollectionLevelChanged,
    pd_statics_CanCollectDiagnostics,
    pd_statics_GetCurrentData,
};

static struct platform_diagnostics_statics platform_diagnostics_statics =
{
    { &pd_factory_vtbl },
    { &pd_statics_vtbl },
    1,
};

/* Windows.System.Profile.WindowsIntegrityPolicy */
static const GUID IID_IWindowsIntegrityPolicyStatics_local =
    { 0x7d1d81db, 0x8d63, 0x4789, { 0x9e, 0xa5, 0xdd, 0xcf, 0x65, 0xa9, 0x4f, 0x3c } };

static HRESULT WINAPI wip_factory_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IWindowsIntegrityPolicyStatics_local ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI wip_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI wip_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI wip_factory_GetIids( IActivationFactory *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI wip_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.System.Profile.WindowsIntegrityPolicy", 43, n );
}
static HRESULT WINAPI wip_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI wip_factory_ActivateInstance( IActivationFactory *iface, IInspectable **out )
{
    return CLASS_E_CLASSNOTAVAILABLE;
}
/* IWindowsIntegrityPolicyStatics methods:
 * Slot 6: get_IsEnabled(boolean *value)
 * Slot 7: get_IsDisableEmbeddedSignatures(boolean *value)
 * Slot 8: get_IsAppAllowed(boolean *value)
 * Slot 9: get_IsAppAllowedEx(...)
 */
static HRESULT WINAPI wip_statics_get_IsEnabled( void *iface, boolean *val ) { if (val) *val = FALSE; return S_OK; }
static HRESULT WINAPI wip_statics_get_bool( void *iface, boolean *val ) { if (val) *val = FALSE; return S_OK; }

static const void *wip_factory_vtbl[] =
{
    wip_factory_QI,
    wip_factory_AddRef,
    wip_factory_Release,
    wip_factory_GetIids,
    wip_factory_GetRuntimeClassName,
    wip_factory_GetTrustLevel,
    wip_factory_ActivateInstance,
    wip_statics_get_IsEnabled,
    wip_statics_get_bool,
    wip_statics_get_bool,
    wip_statics_get_bool,
    wip_statics_get_bool,
};
static struct { const void **vtbl; } wip_factory_obj = { wip_factory_vtbl };

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_System_Profile_EducationSettings ))
        IActivationFactory_QueryInterface( &education_settings_statics.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, RuntimeClass_Windows_System_Profile_PlatformDiagnosticsAndUsageDataSettings ))
        IActivationFactory_QueryInterface( &platform_diagnostics_statics.IActivationFactory_iface,
                                           &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, L"Windows.System.Profile.WindowsIntegrityPolicy" ))
        *factory = (IActivationFactory *)&wip_factory_obj;

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
