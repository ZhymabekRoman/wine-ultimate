/* WinRT Windows.UI Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* Windows.UI.Colors */
struct colors_obj { const void **vtbl; };
static struct colors_obj colors_statics_obj;
static struct colors_obj colors_factory_obj;
static HRESULT WINAPI colors_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IColorsStatics ))
    {
        *out = &colors_statics_obj;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI colors_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI colors_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI colors_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI colors_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Colors", 17, name );
}
static HRESULT WINAPI colors_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI colors_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = (IInspectable *)iface;
    return S_OK;
}
static HRESULT WINAPI colors_get_color( void *iface, struct Color *value )
{
    if (!value) return E_POINTER;
    value->A = 255;
    value->R = 0;
    value->G = 0;
    value->B = 0;
    return S_OK;
}
static HRESULT WINAPI colors_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    return colors_factory_QueryInterface( (IActivationFactory *)&colors_factory_obj, iid, out );
}
static const void *colors_statics_vtbl[] =
{
    colors_statics_QueryInterface,
    colors_factory_AddRef,
    colors_factory_Release,
    colors_factory_GetIids,
    colors_factory_GetRuntimeClassName,
    colors_factory_GetTrustLevel,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
    colors_get_color, colors_get_color, colors_get_color, colors_get_color,
};
static struct colors_obj colors_statics_obj = { colors_statics_vtbl };
static const void *colors_factory_vtbl[] =
{
    colors_factory_QueryInterface,
    colors_factory_AddRef,
    colors_factory_Release,
    colors_factory_GetIids,
    colors_factory_GetRuntimeClassName,
    colors_factory_GetTrustLevel,
    colors_factory_ActivateInstance,
};
static struct colors_obj colors_factory_obj = { colors_factory_vtbl };
static IActivationFactory *colors_factory = (IActivationFactory *)&colors_factory_obj;

/* Windows.UI.Core.Preview.SystemNavigationManagerPreview */
static const GUID IID_ISystemNavigationManagerPreviewStatics = {0x0e971360, 0xdf74, 0x4bce, {0x84, 0xcb, 0xbd, 0x11, 0x81, 0xac, 0x0a, 0x71}};
static const GUID IID_ISystemNavigationManagerPreview = {0xec5f0488, 0x6425, 0x4777, {0xa5, 0x36, 0xcb, 0x56, 0x34, 0x42, 0x7f, 0x0d}};

static HRESULT WINAPI sys_nav_preview_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemNavigationManagerPreview ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sys_nav_preview_AddRef( void *iface ) { return 2; }
static ULONG WINAPI sys_nav_preview_Release( void *iface ) { return 1; }
static HRESULT WINAPI sys_nav_preview_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *array;
    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_ISystemNavigationManagerPreview;
    *count = 1;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI sys_nav_preview_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.Preview.SystemNavigationManagerPreview", 54, name );
}
static HRESULT WINAPI sys_nav_preview_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI sys_nav_preview_add_CloseRequested( void *iface, void *handler, UINT64 *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI sys_nav_preview_remove_CloseRequested( void *iface, UINT64 token )
{
    TRACE( "iface %p, token %s\n", iface, wine_dbgstr_longlong(token) );
    return S_OK;
}
static const void *sys_nav_preview_vtbl[] =
{
    sys_nav_preview_QueryInterface,
    sys_nav_preview_AddRef,
    sys_nav_preview_Release,
    sys_nav_preview_GetIids,
    sys_nav_preview_GetRuntimeClassName,
    sys_nav_preview_GetTrustLevel,
    sys_nav_preview_add_CloseRequested,
    sys_nav_preview_remove_CloseRequested,
};
static struct { const void **vtbl; } sys_nav_preview_obj = { sys_nav_preview_vtbl };

static HRESULT WINAPI sys_nav_preview_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_ISystemNavigationManagerPreviewStatics ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISystemNavigationManagerPreview ))
    {
        *out = &sys_nav_preview_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sys_nav_preview_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI sys_nav_preview_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI sys_nav_preview_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    IID *array;
    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_ISystemNavigationManagerPreviewStatics;
    *count = 1;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI sys_nav_preview_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.Preview.SystemNavigationManagerPreview", 54, name );
}
static HRESULT WINAPI sys_nav_preview_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI sys_nav_preview_factory_GetForCurrentView( void *iface, void **out )
{
    TRACE( "iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    *out = &sys_nav_preview_obj;
    return S_OK;
}
static const void *sys_nav_preview_factory_vtbl[] =
{
    sys_nav_preview_factory_QueryInterface,
    sys_nav_preview_factory_AddRef,
    sys_nav_preview_factory_Release,
    sys_nav_preview_factory_GetIids,
    sys_nav_preview_factory_GetRuntimeClassName,
    sys_nav_preview_factory_GetTrustLevel,
    sys_nav_preview_factory_GetForCurrentView, /* slot 6: ActivateInstance / GetForCurrentView */
    sys_nav_preview_factory_GetForCurrentView, /* slot 7 */
};
static struct { const void **vtbl; } sys_nav_preview_factory_obj = { sys_nav_preview_factory_vtbl };
static IActivationFactory *sys_nav_preview_factory = (IActivationFactory *)&sys_nav_preview_factory_obj;

/* Windows.UI.Core.SystemNavigationManager */
static const GUID IID_ISystemNavigationManagerStatics = {0xdc52b5ce, 0xbee0, 0x4305, {0x8c, 0x54, 0x68, 0x22, 0x8e, 0xd6, 0x83, 0xb5}};
static const GUID IID_ISystemNavigationManager = {0x93023118, 0xcf50, 0x42a6, {0x97, 0x06, 0x69, 0x10, 0x7f, 0xa1, 0x22, 0xe1}};
static const GUID IID_ISystemNavigationManager2 = {0x8c510401, 0x67be, 0x49ae, {0x95, 0x09, 0x67, 0x1c, 0x1e, 0x54, 0xa3, 0x89}};

struct sys_nav
{
    const void **vtbl1;
    const void **vtbl2;
};

static struct sys_nav sys_nav_obj;

static HRESULT WINAPI sys_nav_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ISystemNavigationManager ))
    {
        *out = &sys_nav_obj.vtbl1;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISystemNavigationManager2 ))
    {
        *out = &sys_nav_obj.vtbl2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sys_nav_AddRef( void *iface ) { return 2; }
static ULONG WINAPI sys_nav_Release( void *iface ) { return 1; }
static HRESULT WINAPI sys_nav_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *array;
    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) * 2 );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_ISystemNavigationManager;
    array[1] = IID_ISystemNavigationManager2;
    *count = 2;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI sys_nav_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.SystemNavigationManager", 39, name );
}
static HRESULT WINAPI sys_nav_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI sys_nav_add_BackRequested( void *iface, void *handler, UINT64 *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI sys_nav_remove_BackRequested( void *iface, UINT64 token )
{
    TRACE( "iface %p, token %s\n", iface, wine_dbgstr_longlong(token) );
    return S_OK;
}
static HRESULT WINAPI sys_nav2_get_AppViewBackButtonVisibility( void *iface, int *value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    if (value) *value = 1; /* Collapsed */
    return S_OK;
}
static HRESULT WINAPI sys_nav2_put_AppViewBackButtonVisibility( void *iface, int value )
{
    TRACE( "iface %p, value %d\n", iface, value );
    return S_OK;
}
static const void *sys_nav1_vtbl[] =
{
    sys_nav_QueryInterface,
    sys_nav_AddRef,
    sys_nav_Release,
    sys_nav_GetIids,
    sys_nav_GetRuntimeClassName,
    sys_nav_GetTrustLevel,
    sys_nav_add_BackRequested,
    sys_nav_remove_BackRequested,
};
static const void *sys_nav2_vtbl[] =
{
    sys_nav_QueryInterface,
    sys_nav_AddRef,
    sys_nav_Release,
    sys_nav_GetIids,
    sys_nav_GetRuntimeClassName,
    sys_nav_GetTrustLevel,
    sys_nav2_get_AppViewBackButtonVisibility,
    sys_nav2_put_AppViewBackButtonVisibility,
};
static struct sys_nav sys_nav_obj = { sys_nav1_vtbl, sys_nav2_vtbl };

static HRESULT WINAPI sys_nav_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_ISystemNavigationManagerStatics ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISystemNavigationManager ))
    {
        *out = &sys_nav_obj.vtbl1;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISystemNavigationManager2 ))
    {
        *out = &sys_nav_obj.vtbl2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sys_nav_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI sys_nav_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI sys_nav_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    IID *array;
    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_ISystemNavigationManagerStatics;
    *count = 1;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI sys_nav_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.SystemNavigationManager", 39, name );
}
static HRESULT WINAPI sys_nav_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI sys_nav_factory_GetForCurrentView( void *iface, void **out )
{
    TRACE( "iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    *out = &sys_nav_obj.vtbl1;
    return S_OK;
}
static const void *sys_nav_factory_vtbl[] =
{
    sys_nav_factory_QueryInterface,
    sys_nav_factory_AddRef,
    sys_nav_factory_Release,
    sys_nav_factory_GetIids,
    sys_nav_factory_GetRuntimeClassName,
    sys_nav_factory_GetTrustLevel,
    sys_nav_factory_GetForCurrentView, /* slot 6 */
    sys_nav_factory_GetForCurrentView, /* slot 7 */
};
static struct { const void **vtbl; } sys_nav_factory_obj = { sys_nav_factory_vtbl };
static IActivationFactory *sys_nav_factory = (IActivationFactory *)&sys_nav_factory_obj;

/* Windows.UI.ApplicationSettings.SettingsCommand */
static const GUID IID_ISettingsCommandFactory_local =
    { 0x68e15b33, 0x1c83, 0x433a, { 0xaa, 0x5a, 0xce, 0xee, 0xa5, 0xbd, 0x47, 0x64 } };
static const GUID IID_ISettingsCommandStatics_local =
    { 0x749ae954, 0x2f69, 0x4b17, { 0x8a, 0xba, 0xd0, 0x5c, 0xe5, 0x77, 0x8e, 0x46 } };
static const GUID IID_IUICommand_local =
    { 0x4ff93a75, 0x4145, 0x47ff, { 0xac, 0x7f, 0xdf, 0xf1, 0xc1, 0xfa, 0x5b, 0x0f } };

struct settings_cmd_obj
{
    const void **vtbl;
    LONG ref;
    HSTRING label;
    IInspectable *id;
    IUnknown *handler;
};

static HRESULT WINAPI settings_cmd_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct settings_cmd_obj *impl = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUICommand_local ))
    {
        InterlockedIncrement( &impl->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI settings_cmd_AddRef( void *iface )
{
    struct settings_cmd_obj *impl = iface;
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI settings_cmd_Release( void *iface )
{
    struct settings_cmd_obj *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->label) WindowsDeleteString( impl->label );
        if (impl->id) IInspectable_Release( impl->id );
        if (impl->handler) IUnknown_Release( impl->handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI settings_cmd_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI settings_cmd_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.ApplicationSettings.SettingsCommand", 46, name );
}

static HRESULT WINAPI settings_cmd_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI settings_cmd_get_Label( void *iface, HSTRING *value )
{
    struct settings_cmd_obj *impl = iface;
    return WindowsDuplicateString( impl->label, value );
}

static HRESULT WINAPI settings_cmd_put_Label( void *iface, HSTRING value )
{
    struct settings_cmd_obj *impl = iface;
    if (impl->label) WindowsDeleteString( impl->label );
    impl->label = NULL;
    return value ? WindowsDuplicateString( value, &impl->label ) : S_OK;
}

static HRESULT WINAPI settings_cmd_get_Invoked( void *iface, IUnknown **value )
{
    struct settings_cmd_obj *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->handler;
    if (*value) IUnknown_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI settings_cmd_put_Invoked( void *iface, IUnknown *value )
{
    struct settings_cmd_obj *impl = iface;
    if (impl->handler) IUnknown_Release( impl->handler );
    impl->handler = value;
    if (value) IUnknown_AddRef( value );
    return S_OK;
}

static HRESULT WINAPI settings_cmd_get_Id( void *iface, IInspectable **value )
{
    struct settings_cmd_obj *impl = iface;
    if (!value) return E_POINTER;
    *value = impl->id;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI settings_cmd_put_Id( void *iface, IInspectable *value )
{
    struct settings_cmd_obj *impl = iface;
    if (impl->id) IInspectable_Release( impl->id );
    impl->id = value;
    if (value) IInspectable_AddRef( value );
    return S_OK;
}

static const void *settings_cmd_vtbl[] =
{
    settings_cmd_QueryInterface,
    settings_cmd_AddRef,
    settings_cmd_Release,
    settings_cmd_GetIids,
    settings_cmd_GetRuntimeClassName,
    settings_cmd_GetTrustLevel,
    settings_cmd_get_Label,
    settings_cmd_put_Label,
    settings_cmd_get_Invoked,
    settings_cmd_put_Invoked,
    settings_cmd_get_Id,
    settings_cmd_put_Id,
};

static HRESULT settings_cmd_create( IInspectable *id, HSTRING label, IUnknown *handler, void **out )
{
    struct settings_cmd_obj *obj;
    if (!out) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = settings_cmd_vtbl;
    obj->ref = 1;
    if (label) WindowsDuplicateString( label, &obj->label );
    if (id) { obj->id = id; IInspectable_AddRef( id ); }
    if (handler) { obj->handler = handler; IUnknown_AddRef( handler ); }
    *out = obj;
    return S_OK;
}

/* Windows.UI.Shell.TaskbarManager */
static const GUID IID_ITaskbarManagerStatics_local =
    { 0xdb32ab74, 0xde52, 0x4fe6, { 0xb7, 0xb6, 0x95, 0xff, 0x9f, 0x83, 0x95, 0xdf } };
static const GUID IID_ITaskbarManager_local =
    { 0x87490a19, 0x1ad9, 0x49f4, { 0xb2, 0xe8, 0x86, 0x73, 0x8d, 0xc5, 0xac, 0x40 } };

struct taskbar_mgr_obj { const void **vtbl; LONG ref; };
static const void *taskbar_mgr_vtbl[];

static HRESULT WINAPI taskbar_mgr_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ITaskbarManager_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct taskbar_mgr_obj *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI taskbar_mgr_AddRef( void *iface ) { return InterlockedIncrement( &((struct taskbar_mgr_obj *)iface)->ref ); }
static ULONG WINAPI taskbar_mgr_Release( void *iface )
{
    struct taskbar_mgr_obj *obj = iface;
    ULONG r = InterlockedDecrement( &obj->ref );
    if (!r) free( obj );
    return r;
}
static HRESULT WINAPI taskbar_mgr_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI taskbar_mgr_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.Shell.TaskbarManager", 31, n );
}
static HRESULT WINAPI taskbar_mgr_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI taskbar_mgr_get_IsSupported( void *iface, boolean *val ) { if (val) *val = FALSE; return S_OK; }
static HRESULT WINAPI taskbar_mgr_get_IsPinningAllowed( void *iface, boolean *val ) { if (val) *val = FALSE; return S_OK; }
static HRESULT WINAPI taskbar_mgr_stub_async( void *iface, void **op ) { if (op) *op = NULL; return E_NOTIMPL; }
static HRESULT WINAPI taskbar_mgr_stub_entry_async( void *iface, void *entry, void **op ) { if (op) *op = NULL; return E_NOTIMPL; }

static const void *taskbar_mgr_vtbl[] =
{
    taskbar_mgr_QI,
    taskbar_mgr_AddRef,
    taskbar_mgr_Release,
    taskbar_mgr_GetIids,
    taskbar_mgr_GetRuntimeClassName,
    taskbar_mgr_GetTrustLevel,
    taskbar_mgr_get_IsSupported,
    taskbar_mgr_get_IsPinningAllowed,
    taskbar_mgr_stub_async,
    taskbar_mgr_stub_entry_async,
    taskbar_mgr_stub_async,
    taskbar_mgr_stub_entry_async,
};

static HRESULT taskbar_mgr_create( void **out )
{
    struct taskbar_mgr_obj *obj;
    if (!out) return E_POINTER;
    if (!(obj = calloc( 1, sizeof(*obj) ))) return E_OUTOFMEMORY;
    obj->vtbl = taskbar_mgr_vtbl;
    obj->ref = 1;
    *out = obj;
    return S_OK;
}

static HRESULT WINAPI taskbar_mgr_factory_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_ITaskbarManagerStatics_local ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI taskbar_mgr_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI taskbar_mgr_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI taskbar_mgr_factory_GetIids( IActivationFactory *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI taskbar_mgr_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.Shell.TaskbarManager", 31, n );
}
static HRESULT WINAPI taskbar_mgr_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI taskbar_mgr_factory_ActivateInstance( IActivationFactory *iface, IInspectable **out )
{
    return taskbar_mgr_create( (void **)out );
}
static HRESULT WINAPI taskbar_mgr_factory_GetDefault( IActivationFactory *iface, void **mgr )
{
    return taskbar_mgr_create( mgr );
}

static const void *taskbar_mgr_factory_vtbl[] =
{
    taskbar_mgr_factory_QI,
    taskbar_mgr_factory_AddRef,
    taskbar_mgr_factory_Release,
    taskbar_mgr_factory_GetIids,
    taskbar_mgr_factory_GetRuntimeClassName,
    taskbar_mgr_factory_GetTrustLevel,
    taskbar_mgr_factory_ActivateInstance,
    taskbar_mgr_factory_GetDefault,
};
static struct { const void **vtbl; } taskbar_mgr_factory_obj = { taskbar_mgr_factory_vtbl };
static IActivationFactory *taskbar_mgr_factory = (IActivationFactory *)&taskbar_mgr_factory_obj;

/* Windows.UI.StartScreen.JumpList */
static const GUID IID_IJumpListStatics_local =
    { 0xa7e0c681, 0xe67e, 0x4b74, { 0x82, 0x50, 0x3f, 0x32, 0x2c, 0x4d, 0x92, 0xc3 } };

static const void *jumplist_act_vtbl[];
static const void *jumplist_stat_vtbl[];
static struct { const void **vtbl_act; const void **vtbl_stat; } jumplist_factory_obj =
    { jumplist_act_vtbl, jumplist_stat_vtbl };

static HRESULT WINAPI jumplist_factory_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &jumplist_factory_obj.vtbl_act;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IJumpListStatics_local ))
    {
        *out = &jumplist_factory_obj.vtbl_stat;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI jumplist_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.StartScreen.JumpList", 31, n );
}
static HRESULT WINAPI jumplist_ActivateInstance( void *iface, IInspectable **out )
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI jumplist_LoadCurrentAsync( void *iface, void **op )
{
    if (op) *op = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI jumplist_IsSupported( void *iface, boolean *result )
{
    if (result) *result = FALSE;
    return S_OK;
}
static const void *jumplist_act_vtbl[] =
{
    jumplist_factory_QI,
    taskbar_mgr_factory_AddRef,
    taskbar_mgr_factory_Release,
    taskbar_mgr_factory_GetIids,
    jumplist_GetRuntimeClassName,
    taskbar_mgr_factory_GetTrustLevel,
    jumplist_ActivateInstance,
};
static const void *jumplist_stat_vtbl[] =
{
    jumplist_factory_QI,
    taskbar_mgr_factory_AddRef,
    taskbar_mgr_factory_Release,
    taskbar_mgr_factory_GetIids,
    jumplist_GetRuntimeClassName,
    taskbar_mgr_factory_GetTrustLevel,
    jumplist_LoadCurrentAsync,
    jumplist_IsSupported,
};
static IActivationFactory *jumplist_factory = (IActivationFactory *)&jumplist_factory_obj.vtbl_act;

static const void *sc_act_vtbl[];
static const void *sc_fac_vtbl[];
static const void *sc_stat_vtbl[];
static struct {
    const void **vtbl_act;
    const void **vtbl_fac;
    const void **vtbl_stat;
} settings_cmd_factory_obj = { sc_act_vtbl, sc_fac_vtbl, sc_stat_vtbl };

static HRESULT WINAPI sc_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &settings_cmd_factory_obj.vtbl_act;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISettingsCommandFactory_local ))
    {
        *out = &settings_cmd_factory_obj.vtbl_fac;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISettingsCommandStatics_local ))
    {
        *out = &settings_cmd_factory_obj.vtbl_stat;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sc_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI sc_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI sc_factory_ActivateInstance( void *iface, IInspectable **out )
{
    return settings_cmd_create( NULL, NULL, NULL, (void **)out );
}
static HRESULT WINAPI sc_factory_CreateSettingsCommand( void *iface, IInspectable *id, HSTRING label, IUnknown *handler, void **out )
{
    return settings_cmd_create( id, label, handler, out );
}
static HRESULT WINAPI sc_statics_get_AccountsCommand( void *iface, void **out )
{
    HSTRING lbl = NULL;
    HRESULT hr;
    WindowsCreateString( L"Accounts", 8, &lbl );
    hr = settings_cmd_create( NULL, lbl, NULL, out );
    if (lbl) WindowsDeleteString( lbl );
    return hr;
}

static const void *sc_act_vtbl[] =
{
    sc_factory_QueryInterface,
    sc_factory_AddRef,
    sc_factory_Release,
    settings_cmd_GetIids,
    settings_cmd_GetRuntimeClassName,
    settings_cmd_GetTrustLevel,
    sc_factory_ActivateInstance,
};
static const void *sc_fac_vtbl[] =
{
    sc_factory_QueryInterface,
    sc_factory_AddRef,
    sc_factory_Release,
    settings_cmd_GetIids,
    settings_cmd_GetRuntimeClassName,
    settings_cmd_GetTrustLevel,
    sc_factory_CreateSettingsCommand,
};
static const void *sc_stat_vtbl[] =
{
    sc_factory_QueryInterface,
    sc_factory_AddRef,
    sc_factory_Release,
    settings_cmd_GetIids,
    settings_cmd_GetRuntimeClassName,
    settings_cmd_GetTrustLevel,
    sc_statics_get_AccountsCommand,
};
static IActivationFactory *settings_cmd_factory = (IActivationFactory *)&settings_cmd_factory_obj.vtbl_act;

/* Windows.UI.ApplicationSettings.AccountsSettingsPane */
static const GUID IID_IAccountsSettingsPane_local =
    { 0x81ea942c, 0x4f09, 0x4406, { 0xa5, 0x38, 0x83, 0x8d, 0x9b, 0x14, 0xb7, 0xe6 } };
static const GUID IID_IAccountsSettingsPaneStatics_local =
    { 0x561f8b60, 0xb0ec, 0x4150, { 0xa8, 0xdc, 0x20, 0x8e, 0xe4, 0x4b, 0x06, 0x8a } };
static const GUID IID_IAccountsSettingsPaneStatics2_local =
    { 0xd21df7c2, 0xce0d, 0x484f, { 0xb8, 0xe8, 0xe8, 0x23, 0xc2, 0x15, 0x76, 0x5e } };
static const GUID IID_IAccountsSettingsPaneStatics3_local =
    { 0x08410458, 0xa2ba, 0x4c6f, { 0xb4, 0xac, 0x48, 0xf5, 0x14, 0x33, 0x12, 0x16 } };
static const GUID IID_IAccountsSettingsPaneCommandsRequestedEventArgs_local =
    { 0x3b68c099, 0xdb19, 0x45d0, { 0x9a, 0xbf, 0x95, 0xd3, 0x77, 0x3c, 0x93, 0x30 } };
static const GUID IID_IAccountsSettingsPaneEventDeferral_local =
    { 0xcbf25d3f, 0xe5ba, 0x40ef, { 0x93, 0xda, 0x65, 0xe0, 0x96, 0xe5, 0xfb, 0x04 } };
static const GUID IID_IVector_WebAccountProviderCommand_local =
    { 0xd376abf3, 0xf0c1, 0x5233, { 0x9f, 0x42, 0xde, 0x53, 0x18, 0x84, 0x96, 0x3e } };
static const GUID IID_IVector_WebAccountCommand_local =
    { 0x64e864c8, 0x7fef, 0x5df5, { 0xa6, 0x24, 0x50, 0xb5, 0x77, 0xf4, 0x85, 0x54 } };
static const GUID IID_IVector_CredentialCommand_local =
    { 0xb6af1cb5, 0xf60e, 0x5b08, { 0xb3, 0x12, 0x2e, 0xb5, 0x11, 0x35, 0xcf, 0xc6 } };
static const GUID IID_IVector_SettingsCommand_local =
    { 0x10bd9cdd, 0x3767, 0x5e96, { 0x90, 0x22, 0xf0, 0x0f, 0x9c, 0xbd, 0x62, 0x41 } };

struct asp_async_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    AsyncStatus status;
    IAsyncActionCompletedHandler *handler;
};

static inline struct asp_async_action *asp_action_from_IAsyncAction( IAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct asp_async_action, IAsyncAction_iface );
}

static inline struct asp_async_action *asp_action_from_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct asp_async_action, IAsyncInfo_iface );
}

static HRESULT WINAPI asp_action_QueryInterface( IAsyncAction *iface, REFIID iid, void **out )
{
    struct asp_async_action *This = asp_action_from_IAsyncAction( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        *out = &This->IAsyncAction_iface;
        IAsyncAction_AddRef( &This->IAsyncAction_iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &This->IAsyncInfo_iface;
        IAsyncAction_AddRef( &This->IAsyncAction_iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI asp_action_AddRef( IAsyncAction *iface )
{
    struct asp_async_action *This = asp_action_from_IAsyncAction( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI asp_action_Release( IAsyncAction *iface )
{
    struct asp_async_action *This = asp_action_from_IAsyncAction( iface );
    ULONG ref = InterlockedDecrement( &This->ref );
    if (!ref)
    {
        if (This->handler) IAsyncActionCompletedHandler_Release( This->handler );
        free( This );
    }
    return ref;
}

static HRESULT WINAPI asp_action_GetIids( IAsyncAction *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI asp_action_GetRuntimeClassName( IAsyncAction *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncAction", 31, name );
}

static HRESULT WINAPI asp_action_GetTrustLevel( IAsyncAction *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI asp_action_put_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler *handler )
{
    struct asp_async_action *This = asp_action_from_IAsyncAction( iface );
    if (This->handler) IAsyncActionCompletedHandler_Release( This->handler );
    This->handler = handler;
    if (handler)
    {
        IAsyncActionCompletedHandler_AddRef( handler );
        IAsyncActionCompletedHandler_Invoke( handler, iface, This->status );
    }
    return S_OK;
}

static HRESULT WINAPI asp_action_get_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler **handler )
{
    struct asp_async_action *This = asp_action_from_IAsyncAction( iface );
    if (!handler) return E_POINTER;
    *handler = This->handler;
    if (*handler) IAsyncActionCompletedHandler_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI asp_action_GetResults( IAsyncAction *iface )
{
    return S_OK;
}

static const IAsyncActionVtbl asp_action_vtbl =
{
    asp_action_QueryInterface,
    asp_action_AddRef,
    asp_action_Release,
    asp_action_GetIids,
    asp_action_GetRuntimeClassName,
    asp_action_GetTrustLevel,
    asp_action_put_Completed,
    asp_action_get_Completed,
    asp_action_GetResults,
};

static HRESULT WINAPI asp_action_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return asp_action_QueryInterface( &asp_action_from_IAsyncInfo( iface )->IAsyncAction_iface, iid, out );
}
static ULONG WINAPI asp_action_info_AddRef( IAsyncInfo *iface )
{
    return asp_action_AddRef( &asp_action_from_IAsyncInfo( iface )->IAsyncAction_iface );
}
static ULONG WINAPI asp_action_info_Release( IAsyncInfo *iface )
{
    return asp_action_Release( &asp_action_from_IAsyncInfo( iface )->IAsyncAction_iface );
}
static HRESULT WINAPI asp_action_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI asp_action_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncAction", 31, name );
}
static HRESULT WINAPI asp_action_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *trust )
{
    *trust = BaseTrust; return S_OK;
}
static HRESULT WINAPI asp_action_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (id) *id = 1;
    return S_OK;
}
static HRESULT WINAPI asp_action_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct asp_async_action *This = asp_action_from_IAsyncInfo( iface );
    if (status) *status = This->status;
    return S_OK;
}
static HRESULT WINAPI asp_action_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (error) *error = S_OK;
    return S_OK;
}
static HRESULT WINAPI asp_action_info_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}
static HRESULT WINAPI asp_action_info_Close( IAsyncInfo *iface )
{
    struct asp_async_action *This = asp_action_from_IAsyncInfo( iface );
    This->status = AsyncStatus_Closed;
    return S_OK;
}

static const IAsyncInfoVtbl asp_action_info_vtbl =
{
    asp_action_info_QueryInterface,
    asp_action_info_AddRef,
    asp_action_info_Release,
    asp_action_info_GetIids,
    asp_action_info_GetRuntimeClassName,
    asp_action_info_GetTrustLevel,
    asp_action_info_get_Id,
    asp_action_info_get_Status,
    asp_action_info_get_ErrorCode,
    asp_action_info_Cancel,
    asp_action_info_Close,
};

static HRESULT asp_create_completed_action( IAsyncAction **out )
{
    struct asp_async_action *act;
    if (!out) return E_POINTER;
    act = calloc( 1, sizeof(*act) );
    if (!act) return E_OUTOFMEMORY;
    act->IAsyncAction_iface.lpVtbl = &asp_action_vtbl;
    act->IAsyncInfo_iface.lpVtbl = &asp_action_info_vtbl;
    act->ref = 1;
    act->status = Completed;
    *out = &act->IAsyncAction_iface;
    return S_OK;
}

struct asp_vector
{
    const void **vtbl;
    LONG ref;
    GUID iid;
    IUnknown **items;
    UINT32 count;
    UINT32 cap;
};

static HRESULT WINAPI asp_vector_QI( void *iface, REFIID iid, void **out )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &This->iid ))
    {
        *out = This;
        InterlockedIncrement( &This->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI asp_vector_AddRef( void *iface )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI asp_vector_Release( void *iface )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    ULONG ref = InterlockedDecrement( &This->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < This->count; i++)
            if (This->items[i]) IUnknown_Release( This->items[i] );
        free( This->items );
        free( This );
    }
    return ref;
}

static HRESULT WINAPI asp_vector_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI asp_vector_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVector`1", 40, name );
}

static HRESULT WINAPI asp_vector_GetTrustLevel( void *iface, TrustLevel *trust )
{
    if (trust) *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI asp_vector_GetAt( void *iface, UINT32 index, IUnknown **item )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    if (!item) return E_POINTER;
    if (index >= This->count) return E_BOUNDS;
    *item = This->items[index];
    if (*item) IUnknown_AddRef( *item );
    return S_OK;
}

static HRESULT WINAPI asp_vector_get_Size( void *iface, UINT32 *size )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    if (!size) return E_POINTER;
    *size = This->count;
    return S_OK;
}

static HRESULT WINAPI asp_vector_GetView( void *iface, void **view )
{
    if (!view) return E_POINTER;
    *view = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI asp_vector_IndexOf( void *iface, IUnknown *value, UINT32 *index, boolean *found )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    UINT32 i;
    if (index) *index = 0;
    if (found) *found = FALSE;
    for (i = 0; i < This->count; i++)
    {
        if (This->items[i] == value)
        {
            if (index) *index = i;
            if (found) *found = TRUE;
            return S_OK;
        }
    }
    return S_OK;
}

static HRESULT WINAPI asp_vector_SetAt( void *iface, UINT32 index, IUnknown *item )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    if (index >= This->count) return E_BOUNDS;
    if (item) IUnknown_AddRef( item );
    if (This->items[index]) IUnknown_Release( This->items[index] );
    This->items[index] = item;
    return S_OK;
}

static HRESULT WINAPI asp_vector_InsertAt( void *iface, UINT32 index, IUnknown *item )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    if (index > This->count) return E_BOUNDS;
    if (This->count == This->cap)
    {
        UINT32 new_cap = This->cap ? This->cap * 2 : 8;
        IUnknown **tmp = realloc( This->items, new_cap * sizeof(*tmp) );
        if (!tmp) return E_OUTOFMEMORY;
        This->items = tmp;
        This->cap = new_cap;
    }
    if (index < This->count)
        memmove( &This->items[index + 1], &This->items[index], (This->count - index) * sizeof(*This->items) );
    if (item) IUnknown_AddRef( item );
    This->items[index] = item;
    This->count++;
    return S_OK;
}

static HRESULT WINAPI asp_vector_RemoveAt( void *iface, UINT32 index )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    if (index >= This->count) return E_BOUNDS;
    if (This->items[index]) IUnknown_Release( This->items[index] );
    if (index + 1 < This->count)
        memmove( &This->items[index], &This->items[index + 1], (This->count - index - 1) * sizeof(*This->items) );
    This->count--;
    return S_OK;
}

static HRESULT WINAPI asp_vector_Append( void *iface, IUnknown *item )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    return asp_vector_InsertAt( iface, This->count, item );
}

static HRESULT WINAPI asp_vector_RemoveAtEnd( void *iface )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    if (!This->count) return E_BOUNDS;
    return asp_vector_RemoveAt( iface, This->count - 1 );
}

static HRESULT WINAPI asp_vector_Clear( void *iface )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    UINT32 i;
    for (i = 0; i < This->count; i++)
        if (This->items[i]) IUnknown_Release( This->items[i] );
    This->count = 0;
    return S_OK;
}

static HRESULT WINAPI asp_vector_GetMany( void *iface, UINT32 start, UINT32 cap, IUnknown **items, UINT32 *actual )
{
    struct asp_vector *This = (struct asp_vector *)iface;
    UINT32 i, n = 0;
    if (start < This->count && items)
    {
        n = This->count - start;
        if (n > cap) n = cap;
        for (i = 0; i < n; i++)
        {
            items[i] = This->items[start + i];
            if (items[i]) IUnknown_AddRef( items[i] );
        }
    }
    if (actual) *actual = n;
    return S_OK;
}

static HRESULT WINAPI asp_vector_ReplaceAll( void *iface, UINT32 count, IUnknown **items )
{
    UINT32 i;
    HRESULT hr;
    asp_vector_Clear( iface );
    for (i = 0; i < count; i++)
    {
        if (FAILED(hr = asp_vector_Append( iface, items ? items[i] : NULL )))
            return hr;
    }
    return S_OK;
}

static const void *asp_vector_vtbl[] =
{
    asp_vector_QI,
    asp_vector_AddRef,
    asp_vector_Release,
    asp_vector_GetIids,
    asp_vector_GetRuntimeClassName,
    asp_vector_GetTrustLevel,
    asp_vector_GetAt,
    asp_vector_get_Size,
    asp_vector_GetView,
    asp_vector_IndexOf,
    asp_vector_SetAt,
    asp_vector_InsertAt,
    asp_vector_RemoveAt,
    asp_vector_Append,
    asp_vector_RemoveAtEnd,
    asp_vector_Clear,
    asp_vector_GetMany,
    asp_vector_ReplaceAll,
};

static struct asp_vector *asp_create_vector( REFIID iid )
{
    struct asp_vector *vec = calloc( 1, sizeof(*vec) );
    if (!vec) return NULL;
    vec->vtbl = asp_vector_vtbl;
    vec->ref = 1;
    vec->iid = *iid;
    return vec;
}

struct asp_deferral
{
    const void **vtbl;
    LONG ref;
};

static HRESULT WINAPI asp_deferral_QI( void *iface, REFIID iid, void **out )
{
    struct asp_deferral *This = (struct asp_deferral *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAccountsSettingsPaneEventDeferral_local ))
    {
        *out = This;
        InterlockedIncrement( &This->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI asp_deferral_AddRef( void *iface )
{
    struct asp_deferral *This = (struct asp_deferral *)iface;
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI asp_deferral_Release( void *iface )
{
    struct asp_deferral *This = (struct asp_deferral *)iface;
    ULONG ref = InterlockedDecrement( &This->ref );
    if (!ref) free( This );
    return ref;
}

static HRESULT WINAPI asp_deferral_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI asp_deferral_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.ApplicationSettings.AccountsSettingsPaneEventDeferral", 64, name );
}

static HRESULT WINAPI asp_deferral_GetTrustLevel( void *iface, TrustLevel *trust )
{
    if (trust) *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI asp_deferral_Complete( void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static const void *asp_deferral_vtbl[] =
{
    asp_deferral_QI,
    asp_deferral_AddRef,
    asp_deferral_Release,
    asp_deferral_GetIids,
    asp_deferral_GetRuntimeClassName,
    asp_deferral_GetTrustLevel,
    asp_deferral_Complete,
};

struct asp_commands_args
{
    const void **vtbl;
    LONG ref;
    struct asp_vector *provider_commands;
    struct asp_vector *account_commands;
    struct asp_vector *credential_commands;
    struct asp_vector *commands;
    HSTRING header_text;
};

static HRESULT WINAPI asp_args_QI( void *iface, REFIID iid, void **out )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAccountsSettingsPaneCommandsRequestedEventArgs_local ))
    {
        *out = This;
        InterlockedIncrement( &This->ref );
        return S_OK;
    }
    WARN( "AccountsSettingsPaneCommandsRequestedEventArgs unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI asp_args_AddRef( void *iface )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI asp_args_Release( void *iface )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    ULONG ref = InterlockedDecrement( &This->ref );
    if (!ref)
    {
        if (This->provider_commands) asp_vector_Release( This->provider_commands );
        if (This->account_commands) asp_vector_Release( This->account_commands );
        if (This->credential_commands) asp_vector_Release( This->credential_commands );
        if (This->commands) asp_vector_Release( This->commands );
        if (This->header_text) WindowsDeleteString( This->header_text );
        free( This );
    }
    return ref;
}

static HRESULT WINAPI asp_args_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI asp_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.ApplicationSettings.AccountsSettingsPaneCommandsRequestedEventArgs", 77, name );
}

static HRESULT WINAPI asp_args_GetTrustLevel( void *iface, TrustLevel *trust )
{
    if (trust) *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI asp_args_get_WebAccountProviderCommands( void *iface, void **value )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    if (!value) return E_POINTER;
    *value = This->provider_commands;
    if (*value) asp_vector_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI asp_args_get_WebAccountCommands( void *iface, void **value )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    if (!value) return E_POINTER;
    *value = This->account_commands;
    if (*value) asp_vector_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI asp_args_get_CredentialCommands( void *iface, void **value )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    if (!value) return E_POINTER;
    *value = This->credential_commands;
    if (*value) asp_vector_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI asp_args_get_Commands( void *iface, void **value )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    if (!value) return E_POINTER;
    *value = This->commands;
    if (*value) asp_vector_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI asp_args_get_HeaderText( void *iface, HSTRING *value )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    if (!value) return E_POINTER;
    return WindowsDuplicateString( This->header_text, value );
}

static HRESULT WINAPI asp_args_put_HeaderText( void *iface, HSTRING value )
{
    struct asp_commands_args *This = (struct asp_commands_args *)iface;
    HSTRING dup = NULL;
    HRESULT hr = WindowsDuplicateString( value, &dup );
    if (FAILED(hr)) return hr;
    if (This->header_text) WindowsDeleteString( This->header_text );
    This->header_text = dup;
    return S_OK;
}

static HRESULT WINAPI asp_args_GetDeferral( void *iface, void **value )
{
    struct asp_deferral *def;
    if (!value) return E_POINTER;
    def = calloc( 1, sizeof(*def) );
    if (!def) return E_OUTOFMEMORY;
    def->vtbl = asp_deferral_vtbl;
    def->ref = 1;
    *value = def;
    return S_OK;
}

static const void *asp_args_vtbl[] =
{
    asp_args_QI,
    asp_args_AddRef,
    asp_args_Release,
    asp_args_GetIids,
    asp_args_GetRuntimeClassName,
    asp_args_GetTrustLevel,
    asp_args_get_WebAccountProviderCommands,
    asp_args_get_WebAccountCommands,
    asp_args_get_CredentialCommands,
    asp_args_get_Commands,
    asp_args_get_HeaderText,
    asp_args_put_HeaderText,
    asp_args_GetDeferral,
};

static struct asp_commands_args *asp_create_commands_args( void )
{
    struct asp_commands_args *args = calloc( 1, sizeof(*args) );
    if (!args) return NULL;
    args->vtbl = asp_args_vtbl;
    args->ref = 1;
    args->provider_commands = asp_create_vector( &IID_IVector_WebAccountProviderCommand_local );
    args->account_commands = asp_create_vector( &IID_IVector_WebAccountCommand_local );
    args->credential_commands = asp_create_vector( &IID_IVector_CredentialCommand_local );
    args->commands = asp_create_vector( &IID_IVector_SettingsCommand_local );
    if (!args->provider_commands || !args->account_commands ||
        !args->credential_commands || !args->commands)
    {
        asp_args_Release( args );
        return NULL;
    }
    return args;
}

struct asp_typed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( void *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( void *iface );
    ULONG (WINAPI *Release)( void *iface );
    HRESULT (WINAPI *Invoke)( void *iface, void *sender, void *args );
};

struct asp_handler_entry
{
    IUnknown *handler;
    INT64 cookie;
};

static SRWLOCK asp_handlers_lock = SRWLOCK_INIT;
static struct asp_handler_entry asp_handlers[32];
static UINT32 asp_handler_count = 0;

static const void *asp_pane_vtbl[];
static struct { const void **vtbl; } asp_pane_singleton = { asp_pane_vtbl };
static LONG asp_next_cookie = 1;

static void asp_fire_commands_requested( void )
{
    IUnknown *snapshot[32];
    UINT32 count = 0, i;
    struct asp_commands_args *args;

    AcquireSRWLockShared( &asp_handlers_lock );
    for (i = 0; i < asp_handler_count && i < ARRAY_SIZE(snapshot); i++)
    {
        snapshot[count] = asp_handlers[i].handler;
        if (snapshot[count])
        {
            IUnknown_AddRef( snapshot[count] );
            count++;
        }
    }
    ReleaseSRWLockShared( &asp_handlers_lock );

    if (!count) return;
    args = asp_create_commands_args();
    if (!args)
    {
        for (i = 0; i < count; i++) IUnknown_Release( snapshot[i] );
        return;
    }

    for (i = 0; i < count; i++)
    {
        const struct asp_typed_handler_vtbl *vtbl = *(const struct asp_typed_handler_vtbl **)snapshot[i];
        TRACE( "invoking AccountCommandsRequested handler %p with args %p.\n", snapshot[i], args );
        vtbl->Invoke( snapshot[i], &asp_pane_singleton, args );
        IUnknown_Release( snapshot[i] );
    }
    asp_args_Release( args );
}

static HRESULT WINAPI asp_pane_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAccountsSettingsPane_local ))
    {
        *out = &asp_pane_singleton;
        return S_OK;
    }
    WARN( "AccountsSettingsPane unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI asp_pane_AddRef( void *iface ) { return 2; }
static ULONG WINAPI asp_pane_Release( void *iface ) { return 1; }
static HRESULT WINAPI asp_pane_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI asp_pane_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.ApplicationSettings.AccountsSettingsPane", 51, name );
}
static HRESULT WINAPI asp_pane_GetTrustLevel( void *iface, TrustLevel *trust )
{
    *trust = BaseTrust; return S_OK;
}
static HRESULT WINAPI asp_pane_add_AccountCommandsRequested( void *iface, void *handler, EventRegistrationToken *cookie )
{
    INT64 token;
    TRACE( "iface %p, handler %p, cookie %p.\n", iface, handler, cookie );
    if (!handler || !cookie) return E_POINTER;
    token = InterlockedIncrement( &asp_next_cookie );
    AcquireSRWLockExclusive( &asp_handlers_lock );
    if (asp_handler_count < ARRAY_SIZE(asp_handlers))
    {
        IUnknown_AddRef( (IUnknown *)handler );
        asp_handlers[asp_handler_count].handler = (IUnknown *)handler;
        asp_handlers[asp_handler_count].cookie = token;
        asp_handler_count++;
    }
    ReleaseSRWLockExclusive( &asp_handlers_lock );
    cookie->value = token;
    return S_OK;
}
static HRESULT WINAPI asp_pane_remove_AccountCommandsRequested( void *iface, EventRegistrationToken cookie )
{
    IUnknown *removed = NULL;
    UINT32 i;
    TRACE( "iface %p, cookie %I64d.\n", iface, cookie.value );
    AcquireSRWLockExclusive( &asp_handlers_lock );
    for (i = 0; i < asp_handler_count; i++)
    {
        if (asp_handlers[i].cookie == cookie.value)
        {
            removed = asp_handlers[i].handler;
            asp_handler_count--;
            if (i < asp_handler_count)
                asp_handlers[i] = asp_handlers[asp_handler_count];
            break;
        }
    }
    ReleaseSRWLockExclusive( &asp_handlers_lock );
    if (removed) IUnknown_Release( removed );
    return S_OK;
}

static const void *asp_pane_vtbl[] =
{
    asp_pane_QI,
    asp_pane_AddRef,
    asp_pane_Release,
    asp_pane_GetIids,
    asp_pane_GetRuntimeClassName,
    asp_pane_GetTrustLevel,
    asp_pane_add_AccountCommandsRequested,
    asp_pane_remove_AccountCommandsRequested,
};

static const void *asp_act_vtbl[];
static const void *asp_stat1_vtbl[];
static const void *asp_stat2_vtbl[];
static const void *asp_stat3_vtbl[];
static struct {
    const void **vtbl_act;
    const void **vtbl_stat1;
    const void **vtbl_stat2;
    const void **vtbl_stat3;
} asp_factory_obj = { asp_act_vtbl, asp_stat1_vtbl, asp_stat2_vtbl, asp_stat3_vtbl };

static HRESULT WINAPI asp_factory_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &asp_factory_obj.vtbl_act;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAccountsSettingsPaneStatics_local ))
    {
        *out = &asp_factory_obj.vtbl_stat1;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAccountsSettingsPaneStatics2_local ))
    {
        *out = &asp_factory_obj.vtbl_stat2;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAccountsSettingsPaneStatics3_local ))
    {
        *out = &asp_factory_obj.vtbl_stat3;
        return S_OK;
    }
    WARN( "AccountsSettingsPane factory unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI asp_factory_ActivateInstance( void *iface, IInspectable **out )
{
    if (!out) return E_POINTER;
    *out = (IInspectable *)&asp_pane_singleton;
    return S_OK;
}
static HRESULT WINAPI asp_stat1_GetForCurrentView( void *iface, void **out )
{
    TRACE( "iface %p, out %p.\n", iface, out );
    if (!out) return E_POINTER;
    *out = &asp_pane_singleton;
    return S_OK;
}
static HRESULT WINAPI asp_stat1_Show( void *iface )
{
    TRACE( "iface %p.\n", iface );
    asp_fire_commands_requested();
    return S_OK;
}
static HRESULT WINAPI asp_stat2_ShowManageAccountsAsync( void *iface, IAsyncAction **out )
{
    TRACE( "iface %p, out %p.\n", iface, out );
    asp_fire_commands_requested();
    return asp_create_completed_action( out );
}
static HRESULT WINAPI asp_stat2_ShowAddAccountAsync( void *iface, IAsyncAction **out )
{
    TRACE( "iface %p, out %p.\n", iface, out );
    asp_fire_commands_requested();
    return asp_create_completed_action( out );
}
static HRESULT WINAPI asp_stat3_ShowManageAccountsForUserAsync( void *iface, void *user, IAsyncAction **out )
{
    TRACE( "iface %p, user %p, out %p.\n", iface, user, out );
    asp_fire_commands_requested();
    return asp_create_completed_action( out );
}
static HRESULT WINAPI asp_stat3_ShowAddAccountForUserAsync( void *iface, void *user, IAsyncAction **out )
{
    TRACE( "iface %p, user %p, out %p.\n", iface, user, out );
    asp_fire_commands_requested();
    return asp_create_completed_action( out );
}

static const void *asp_act_vtbl[] =
{
    asp_factory_QI,
    asp_pane_AddRef,
    asp_pane_Release,
    asp_pane_GetIids,
    asp_pane_GetRuntimeClassName,
    asp_pane_GetTrustLevel,
    asp_factory_ActivateInstance,
};
static const void *asp_stat1_vtbl[] =
{
    asp_factory_QI,
    asp_pane_AddRef,
    asp_pane_Release,
    asp_pane_GetIids,
    asp_pane_GetRuntimeClassName,
    asp_pane_GetTrustLevel,
    asp_stat1_GetForCurrentView,
    asp_stat1_Show,
};
static const void *asp_stat2_vtbl[] =
{
    asp_factory_QI,
    asp_pane_AddRef,
    asp_pane_Release,
    asp_pane_GetIids,
    asp_pane_GetRuntimeClassName,
    asp_pane_GetTrustLevel,
    asp_stat2_ShowManageAccountsAsync,
    asp_stat2_ShowAddAccountAsync,
};
static const void *asp_stat3_vtbl[] =
{
    asp_factory_QI,
    asp_pane_AddRef,
    asp_pane_Release,
    asp_pane_GetIids,
    asp_pane_GetRuntimeClassName,
    asp_pane_GetTrustLevel,
    asp_stat3_ShowManageAccountsForUserAsync,
    asp_stat3_ShowAddAccountForUserAsync,
};
static IActivationFactory *accounts_settings_pane_factory = (IActivationFactory *)&asp_factory_obj.vtbl_act;

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_UI_ViewManagement_AccessibilitySettings ))
        IActivationFactory_QueryInterface( accessibilitysettings_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, RuntimeClass_Windows_UI_ViewManagement_UISettings ))
        IActivationFactory_QueryInterface( uisettings_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, RuntimeClass_Windows_UI_ViewManagement_UIViewSettings ))
        IActivationFactory_QueryInterface( uiviewsettings_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, RuntimeClass_Windows_UI_ViewManagement_InputPane ))
        IActivationFactory_QueryInterface( inputpane_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, RuntimeClass_Windows_UI_Core_CoreWindow ))
        IActivationFactory_QueryInterface( corewindow_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Core.CoreCursor" ))
        IActivationFactory_QueryInterface( core_cursor_factory, &IID_IActivationFactory, (void **)factory );

    /*
     * Windows App SDK types the XAML parser activates by class id; no framework
     * module has them in its activation table (the WinAppSDK modules answer
     * CLASS_E_CLASSNOTAVAILABLE for FontWeights), so the registry routes them here.
     */
    if (!wcscmp( buffer, RuntimeClass_Microsoft_UI_Text_FontWeights ) ||
        !wcscmp( buffer, L"Windows.UI.Text.FontWeights" ))
        IActivationFactory_QueryInterface( fontweights_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.Foundation.IReference`1<Windows.UI.Text.FontWeight>" ))
        IActivationFactory_QueryInterface( fontweight_reference_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Colors" ))
        IActivationFactory_QueryInterface( colors_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, RuntimeClass_Windows_UI_Input_GestureRecognizer ))
        IActivationFactory_QueryInterface( gesturerecognizer_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Core.Preview.SystemNavigationManagerPreview" ))
        IActivationFactory_QueryInterface( sys_nav_preview_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Core.SystemNavigationManager" ))
        IActivationFactory_QueryInterface( sys_nav_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.ApplicationSettings.SettingsCommand" ))
        IActivationFactory_QueryInterface( settings_cmd_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.ApplicationSettings.AccountsSettingsPane" ))
        IActivationFactory_QueryInterface( accounts_settings_pane_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Shell.TaskbarManager" ))
        IActivationFactory_QueryInterface( taskbar_mgr_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.StartScreen.JumpList" ))
        IActivationFactory_QueryInterface( jumplist_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Popups.MessageDialog" ) ||
        !wcscmp( buffer, L"Windows.UI.Popups.UICommand" ))
        IActivationFactory_QueryInterface( popup_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Popups.UICommandSeparator" ))
        IActivationFactory_QueryInterface( uicmd_separator_factory, &IID_IActivationFactory, (void **)factory );

    if (!wcscmp( buffer, L"Windows.UI.Popups.PopupMenu" ))
        IActivationFactory_QueryInterface( popupmenu_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    /* This module is reached through the ext-ms-win-windowing-internal-l1-1-0 apiset as
     * Windowing_DllGetActivationFactory, and the Windows App SDK chains that probe to its own
     * activation factories only on E_NOINTERFACE - the value the generated C++/WinRT factories
     * of the system modules report for classes they do not implement. */
    return E_NOINTERFACE;
}
