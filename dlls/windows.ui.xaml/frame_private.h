/* Internal declarations shared between frame.c and winui_elements.c
 *
 * Copyright (C) 2026 Wine project
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

#ifndef __WINE_FRAME_PRIVATE_H
#define __WINE_FRAME_PRIVATE_H

/* Shared WinUI 3 / UWP interface GUIDs defined in frame.c */
extern const GUID IID_IControl_g;
extern const GUID IID_IGrid;
extern const GUID IID_IPanel;
extern const GUID IID_IRowDefinition;
extern const GUID IID_IColumnDefinition;
extern const GUID IID_IUIElement_WinUI;
extern const GUID IID_IFrameworkElement_WinUI;
extern const GUID IID_IControl_WinUI;
extern const GUID IID_IContentControl_WinUI;
extern const GUID IID_IUserControl;
extern const GUID IID_IBorder;
extern const GUID IID_IStackPanel;
extern const GUID IID_IButton;
extern const GUID IID_IButtonBase;
extern const GUID IID_IStyle;
extern const GUID IID_ICanvas;
extern const GUID IID_IProgressRing;
extern const GUID IID_IImage_WinUI;

/* Frame / generic WinUI3 element factory layout and shared base methods */
struct frame_factory
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IComposableFactory_iface;
    LONG ref;
    IInspectable IStatics_iface;
};

HRESULT WINAPI frame_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out );
ULONG WINAPI frame_factory_AddRef( IActivationFactory *iface );
ULONG WINAPI frame_factory_Release( IActivationFactory *iface );
HRESULT WINAPI frame_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids );
HRESULT WINAPI frame_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name );
HRESULT WINAPI frame_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level );

HRESULT WINAPI composable_factory_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI composable_factory_AddRef( IInspectable *iface );
ULONG WINAPI composable_factory_Release( IInspectable *iface );

HRESULT WINAPI statics_factory_QueryInterface( IInspectable *iface, REFIID iid, void **out );
ULONG WINAPI statics_factory_AddRef( IInspectable *iface );
ULONG WINAPI statics_factory_Release( IInspectable *iface );

#endif /* __WINE_FRAME_PRIVATE_H */
