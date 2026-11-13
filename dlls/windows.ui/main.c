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
static HRESULT WINAPI colors_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IColorsStatics ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    *out = iface;
    IActivationFactory_AddRef( iface );
    return S_OK;
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
static const void *colors_factory_vtbl[] =
{
    colors_factory_QueryInterface,
    colors_factory_AddRef,
    colors_factory_Release,
    colors_factory_GetIids,
    colors_factory_GetRuntimeClassName,
    colors_factory_GetTrustLevel,
    colors_factory_ActivateInstance, /* 6: ActivateInstance */
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
static struct { const void **vtbl; } colors_factory_obj = { colors_factory_vtbl };
static IActivationFactory *colors_factory = (IActivationFactory *)&colors_factory_obj;

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

    if (*factory) return S_OK;
    /* This module is reached through the ext-ms-win-windowing-internal-l1-1-0 apiset as
     * Windowing_DllGetActivationFactory, and the Windows App SDK chains that probe to its own
     * activation factories only on E_NOINTERFACE - the value the generated C++/WinRT factories
     * of the system modules report for classes they do not implement. */
    return E_NOINTERFACE;
}
