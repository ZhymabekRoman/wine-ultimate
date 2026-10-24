/* WinRT Windows.UI Implementation
 *
 * Copyright (C) 2024 Vijay Kiran Kamuju
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

#include <string.h>

#include "private.h"
#include "objbase.h"
#include "initguid.h"
#include "uiviewsettingsinterop.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

struct uiviewsettings
{
    IUIViewSettings IUIViewSettings_iface;
    LONG ref;
};

static inline struct uiviewsettings *impl_from_IUIViewSettings( IUIViewSettings *iface )
{
    return CONTAINING_RECORD( iface, struct uiviewsettings, IUIViewSettings_iface );
}

static HRESULT WINAPI uiviewsettings_QueryInterface( IUIViewSettings *iface, REFIID iid, void **out )
{
    struct uiviewsettings *impl = impl_from_IUIViewSettings( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUIViewSettings ))
    {
        *out = &impl->IUIViewSettings_iface;
    }

    if (!*out)
    {
        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static ULONG WINAPI uiviewsettings_AddRef( IUIViewSettings *iface )
{
    struct uiviewsettings *impl = impl_from_IUIViewSettings( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI uiviewsettings_Release( IUIViewSettings *iface )
{
    struct uiviewsettings *impl = impl_from_IUIViewSettings( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI uiviewsettings_GetIids( IUIViewSettings *iface, ULONG *iid_count, IID **iids )
{
    static const IID iid_array[] =
    {
        IID_IUIViewSettings,
    };
    IID *array = CoTaskMemAlloc( sizeof(iid_array) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, iid_array, sizeof(iid_array) );
    *iid_count = ARRAY_SIZE(iid_array);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI uiviewsettings_GetRuntimeClassName( IUIViewSettings *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI uiviewsettings_GetTrustLevel( IUIViewSettings *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI uiviewsettings_get_UserInteractionMode( IUIViewSettings *iface, enum UserInteractionMode *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IUIViewSettingsVtbl uiviewsettings_vtbl =
{
    uiviewsettings_QueryInterface,
    uiviewsettings_AddRef,
    uiviewsettings_Release,

    /* IInspectable methods */
    uiviewsettings_GetIids,
    uiviewsettings_GetRuntimeClassName,
    uiviewsettings_GetTrustLevel,

    /* IUIViewSettings methods */
    uiviewsettings_get_UserInteractionMode,
};

struct uiviewsettings_statics
{
    IActivationFactory IActivationFactory_iface;
    IUIViewSettingsInterop IUIViewSettingsInterop_iface;
    IUIViewSettingsStatics IUIViewSettingsStatics_iface;
    LONG ref;
};

static inline struct uiviewsettings_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct uiviewsettings_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct uiviewsettings_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IUIViewSettingsInterop ))
    {
        *out = &impl->IUIViewSettingsInterop_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IUIViewSettingsStatics ))
    {
        *out = &impl->IUIViewSettingsStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* Windows aggregates the class default interface (IUIViewSettings) on the factory object
     * (CsWinRT probes the factory for the class interfaces). The factory is not an
     * instance; instance method calls fail. */
    if (IsEqualGUID( iid, &IID_IUIViewSettings ))
    {
        *out = &impl->IUIViewSettings_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct uiviewsettings_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct uiviewsettings_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID factory_iids[] =
    {
        IID_IActivationFactory,
        IID_IUIViewSettingsStatics,
        IID_IUIViewSettingsInterop,
        IID_IUIViewSettings,
    };
    IID *array = CoTaskMemAlloc( sizeof(factory_iids) );

    if (!array) return E_OUTOFMEMORY;
    memcpy( array, factory_iids, sizeof(factory_iids) );
    *iid_count = ARRAY_SIZE(factory_iids);
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_ViewManagement_UIViewSettings, lstrlenW( RuntimeClass_Windows_UI_ViewManagement_UIViewSettings ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    **trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct uiviewsettings *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }

    impl->IUIViewSettings_iface.lpVtbl = &uiviewsettings_vtbl;
    impl->ref = 1;

    *instance = (IInspectable *)&impl->IUIViewSettings_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( ui_viewsettings_interop, IUIViewSettingsInterop, struct uiviewsettings_statics, IActivationFactory_iface );

static HRESULT WINAPI ui_viewsettings_interop_GetForWindow( IUIViewSettingsInterop *iface, HWND window, REFIID riid, void **uiviewsettings )
{
    struct uiviewsettings_statics *impl = impl_from_IUIViewSettingsInterop( iface );

    TRACE( "(window %p, riid %s, uiviewsettings %p)\n", window, debugstr_guid( riid ), uiviewsettings );

    factory_ActivateInstance( &impl->IActivationFactory_iface, (IInspectable **)uiviewsettings );
    return S_OK;
}

static const struct IUIViewSettingsInteropVtbl ui_viewsettings_interop_vtbl =
{
    ui_viewsettings_interop_QueryInterface,
    ui_viewsettings_interop_AddRef,
    ui_viewsettings_interop_Release,

    /* IInspectable methods */
    ui_viewsettings_interop_GetIids,
    ui_viewsettings_interop_GetRuntimeClassName,
    ui_viewsettings_interop_GetTrustLevel,

    /* IUIViewSettingsInterop methods */
    ui_viewsettings_interop_GetForWindow,
};

DEFINE_IINSPECTABLE( ui_viewsettings_statics, IUIViewSettingsStatics, struct uiviewsettings_statics, IActivationFactory_iface );

static HRESULT WINAPI ui_viewsettings_statics_GetForCurrentView( IUIViewSettingsStatics *iface, IUIViewSettings **uiviewsettings )
{
    struct uiviewsettings_statics *impl = impl_from_IUIViewSettingsStatics( iface );

    TRACE( "(uiviewsettings %p)\n", uiviewsettings );

    factory_ActivateInstance( &impl->IActivationFactory_iface, (IInspectable **)uiviewsettings );
    return S_OK;
}

static const struct IUIViewSettingsStaticsVtbl ui_viewsettings_statics_vtbl =
{
    ui_viewsettings_statics_QueryInterface,
    ui_viewsettings_statics_AddRef,
    ui_viewsettings_statics_Release,

    /* IInspectable methods */
    ui_viewsettings_statics_GetIids,
    ui_viewsettings_statics_GetRuntimeClassName,
    ui_viewsettings_statics_GetTrustLevel,

    /* IUIViewSettingsStatics methods */
    ui_viewsettings_statics_GetForCurrentView,
};


/* Windows aggregates the class default interface (IUIViewSettings) on the factory object;
 * the factory is not an instance: method calls fail. */
DEFINE_IINSPECTABLE_( factory_def, IUIViewSettings, struct uiviewsettings_statics, impl_from_IUIViewSettings_factory_def,
                      IUIViewSettings_iface, &impl->IActivationFactory_iface )

static HRESULT WINAPI factory_def_get_UserInteractionMode( IUIViewSettings *iface, UserInteractionMode *value ) { return E_FAIL; }

static const struct IUIViewSettingsVtbl factory_def_vtbl =
{
    factory_def_QueryInterface,
    factory_def_AddRef,
    factory_def_Release,
    factory_def_GetIids,
    factory_def_GetRuntimeClassName,
    factory_def_GetTrustLevel,
    factory_def_get_UserInteractionMode,
};

static struct uiviewsettings_statics uiviewsettings_statics =
{
    {&factory_vtbl},
    {&ui_viewsettings_interop_vtbl},
    {&ui_viewsettings_statics_vtbl},
    {&factory_def_vtbl}, 1,
};

IActivationFactory *uiviewsettings_factory = &uiviewsettings_statics.IActivationFactory_iface;
