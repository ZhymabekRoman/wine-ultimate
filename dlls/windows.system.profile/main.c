/*
 * Windows.System.Profile.EducationSettings activation factory
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

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_System_Profile_EducationSettings ))
        IActivationFactory_QueryInterface( &education_settings_statics.IActivationFactory_iface,
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
