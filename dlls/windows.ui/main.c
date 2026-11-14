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
    if (!wcscmp( buffer, RuntimeClass_Microsoft_UI_Text_FontWeights ))
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

    if (*factory) return S_OK;
    /* This module is reached through the ext-ms-win-windowing-internal-l1-1-0 apiset as
     * Windowing_DllGetActivationFactory, and the Windows App SDK chains that probe to its own
     * activation factories only on E_NOINTERFACE - the value the generated C++/WinRT factories
     * of the system modules report for classes they do not implement. */
    return E_NOINTERFACE;
}
