/* WinRT Windows.UI.Xaml UWP Visual Tree, PRI/XBF Loader, Layout & Input Host
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wctype.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "appmodel.h"
#include "weakreference.h"
#include "winreg.h"
#include "wincodec.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"

static const GUID CLSID_WICImagingFactory_local =
    { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID IID_IWICImagingFactory_local =
    { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID GUID_WICPixelFormat32bppBGRA_local =
    { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f } };

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static const GUID IID_IWeakReference_local =
    { 0x00000037, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IWeakReferenceSource_local =
    { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

/* Faces we QI and call raw-indexed: the metadata provider, XAML type/member, property-change
 * notification, boxed IReference<T>, ICommand and generic event delegates. The objects behind
 * them are served by our own combase-side code and Calculator's .NET Native RCW projection,
 * whose vtables follow the WinRT ABI. Naming each slot we call lets callers use typed calls;
 * earlier, uncalled slots stay as void * placeholders. */
struct xaml_type;
struct xaml_member;

struct xaml_metadata_provider;
struct xaml_metadata_provider_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_metadata_provider *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_metadata_provider *This );
    ULONG (WINAPI *Release)( struct xaml_metadata_provider *This );
    void *pad3, *pad4, *pad5, *pad6;
    HRESULT (WINAPI *GetXamlTypeByFullName)( struct xaml_metadata_provider *This, HSTRING name,
                                             struct xaml_type **type );   /* slot 7 */
};
struct xaml_metadata_provider
{
    const struct xaml_metadata_provider_vtbl *lpVtbl;
};

struct xaml_type_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_type *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_type *This );
    ULONG (WINAPI *Release)( struct xaml_type *This );
    void *pad3, *pad4, *pad5, *pad6, *pad7;
    HRESULT (WINAPI *get_FullName)( struct xaml_type *This, HSTRING *name );                /* slot 8 */
    void *pad9, *pad10, *pad11, *pad12, *pad13, *pad14, *pad15, *pad16, *pad17;
    HRESULT (WINAPI *ActivateInstance)( struct xaml_type *This, IInspectable **instance );  /* slot 18 */
    HRESULT (WINAPI *CreateFromString)( struct xaml_type *This, HSTRING input, void **value ); /* slot 19 */
    HRESULT (WINAPI *GetMember)( struct xaml_type *This, HSTRING name, struct xaml_member **member ); /* slot 20 */
};
struct xaml_type
{
    const struct xaml_type_vtbl *lpVtbl;
};

struct xaml_member_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_member *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_member *This );
    ULONG (WINAPI *Release)( struct xaml_member *This );
    void *pad3, *pad4, *pad5, *pad6, *pad7, *pad8, *pad9, *pad10;
    HRESULT (WINAPI *get_Type)( struct xaml_member *This, struct xaml_type **type );        /* slot 11 */
    HRESULT (WINAPI *GetValue)( struct xaml_member *This, void *instance, IInspectable **value ); /* slot 12 */
    HRESULT (WINAPI *SetValue)( struct xaml_member *This, void *instance, void *value );    /* slot 13 */
};
struct xaml_member
{
    const struct xaml_member_vtbl *lpVtbl;
};

struct notify_property_changed;
struct notify_property_changed_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct notify_property_changed *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct notify_property_changed *This );
    ULONG (WINAPI *Release)( struct notify_property_changed *This );
    void *pad3, *pad4, *pad5;
    HRESULT (WINAPI *add_PropertyChanged)( struct notify_property_changed *This, void *handler,
                                           EventRegistrationToken *token );                 /* slot 6 */
    HRESULT (WINAPI *remove_PropertyChanged)( struct notify_property_changed *This,
                                              EventRegistrationToken token );               /* slot 7 */
};
struct notify_property_changed
{
    const struct notify_property_changed_vtbl *lpVtbl;
};

struct property_changed_args;
struct property_changed_args_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct property_changed_args *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct property_changed_args *This );
    ULONG (WINAPI *Release)( struct property_changed_args *This );
    void *pad3, *pad4, *pad5;
    HRESULT (WINAPI *get_PropertyName)( struct property_changed_args *This, HSTRING *name ); /* slot 6 */
};
struct property_changed_args
{
    const struct property_changed_args_vtbl *lpVtbl;
};

struct reference_int32;
struct reference_int32_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct reference_int32 *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct reference_int32 *This );
    ULONG (WINAPI *Release)( struct reference_int32 *This );
    void *pad3, *pad4, *pad5;
    HRESULT (WINAPI *get_Value)( struct reference_int32 *This, INT32 *value );               /* slot 6 */
};
struct reference_int32
{
    const struct reference_int32_vtbl *lpVtbl;
};

struct command_iface;
struct command_iface_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct command_iface *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct command_iface *This );
    ULONG (WINAPI *Release)( struct command_iface *This );
    void *pad3, *pad4, *pad5, *pad6, *pad7, *pad8;
    HRESULT (WINAPI *Execute)( struct command_iface *This, void *parameter );                /* slot 9 */
};
struct command_iface
{
    const struct command_iface_vtbl *lpVtbl;
};

/* Generic event delegate: { QueryInterface, AddRef, Release, Invoke(sender, args) }. */
struct xaml_event_handler;
struct xaml_event_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct xaml_event_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct xaml_event_handler *This );
    ULONG (WINAPI *Release)( struct xaml_event_handler *This );
    HRESULT (WINAPI *Invoke)( struct xaml_event_handler *This, void *sender, void *args );   /* slot 3 */
};
struct xaml_event_handler
{
    const struct xaml_event_handler_vtbl *lpVtbl;
};

/* UWP Windows.UI.Xaml interface GUIDs */
static const GUID IID_IDependencyObject_UWP =
    { 0x5c526665, 0xf60e, 0x4912, { 0xaf, 0x59, 0x5f, 0xe0, 0x68, 0x0f, 0x08, 0x9d } };
static const GUID IID_IDependencyObject2_UWP =
    { 0x29fed85d, 0x3d22, 0x43a1, { 0xad, 0xd0, 0x17, 0x02, 0x7c, 0x08, 0xb2, 0x12 } };
static const GUID IID_IDependencyObjectFactory_UWP =
    { 0x9a03af92, 0x7d8a, 0x4937, { 0x88, 0x4f, 0xec, 0xf3, 0x4f, 0xe0, 0x2a, 0xcb } };
static const GUID IID_IUIElement_UWP =
    { 0x676d0be9, 0xb65c, 0x41c6, { 0xba, 0x40, 0x58, 0xcf, 0x87, 0xf2, 0x01, 0xc1 } };
static const GUID IID_IUIElement2_UWP =
    { 0x676d0bf9, 0xb66c, 0x41d6, { 0xba, 0x50, 0x58, 0xcf, 0x87, 0xf2, 0x01, 0xd1 } };
static const GUID IID_IUIElement3_UWP =
    { 0x676d0c09, 0xb67c, 0x41e6, { 0xba, 0x60, 0x58, 0xcf, 0x87, 0xf2, 0x01, 0xe1 } };
static const GUID IID_IUIElement4_UWP =
    { 0x676d0c19, 0xb68c, 0x41f6, { 0xba, 0x70, 0x58, 0xcf, 0x87, 0xf2, 0x01, 0xf1 } };
static const GUID IID_IUIElement5_UWP =
    { 0x676d0c29, 0xb69c, 0x4206, { 0xba, 0x80, 0x58, 0xcf, 0x87, 0xf2, 0x02, 0x01 } };
static const GUID IID_IUIElementOverrides_UWP =
    { 0x608d2f1d, 0x7858, 0x4aeb, { 0x89, 0xe4, 0xb5, 0x4e, 0x2c, 0x7e, 0xd3, 0xd3 } };
static const GUID IID_IUIElementFactory_UWP =
    { 0xb61c4d87, 0x70e5, 0x412e, { 0xb5, 0x20, 0x1a, 0x41, 0xee, 0x76, 0xbb, 0xf4 } };
static const GUID IID_IUIElementStatics_UWP =
    { 0x58d3573b, 0xf52c, 0x45be, { 0x98, 0x8b, 0xa5, 0x86, 0x95, 0x64, 0x87, 0x3c } };
static const GUID IID_IFrameworkElement_UWP =
    { 0xa391d09b, 0x4a99, 0x4b7c, { 0x9d, 0x8d, 0x6f, 0xa5, 0xd0, 0x1f, 0x6f, 0xbf } };
static const GUID IID_IFrameworkElement2_UWP =
    { 0xf19104be, 0x422a, 0x4904, { 0xa5, 0x2f, 0xee, 0x72, 0x01, 0x04, 0x29, 0xe5 } };
static const GUID IID_IFrameworkElement3_UWP =
    { 0xc81c2720, 0x5c52, 0x4bbe, { 0xa1, 0x99, 0x2b, 0x1e, 0x34, 0xf0, 0x0f, 0x70 } };
static const GUID IID_IFrameworkElement4_UWP =
    { 0x6b73fe13, 0x2cf2, 0x4d48, { 0x8b, 0x5e, 0x38, 0xf0, 0x52, 0xc4, 0xe3, 0x62 } };
static const GUID IID_IFrameworkElement6_UWP =
    { 0xeb06716a, 0x4214, 0x499c, { 0xa4, 0x8a, 0x2d, 0x5e, 0x5b, 0x13, 0x5b, 0x30 } };
static const GUID IID_IFrameworkElementOverrides_UWP =
    { 0xda007e54, 0xb3c2, 0x4b9a, { 0xaa, 0x8e, 0xd3, 0xf0, 0x71, 0x26, 0x2b, 0x97 } };
static const GUID IID_IFrameworkElementOverrides2_UWP =
    { 0xcb5cd2b9, 0xe3b4, 0x458c, { 0xb6, 0x4e, 0x14, 0x34, 0xfd, 0x1b, 0xd8, 0x8a } };
static const GUID IID_IFrameworkElementFactory_UWP =
    { 0xdeaee126, 0x03ca, 0x4966, { 0xb5, 0x76, 0x60, 0x4c, 0xce, 0x93, 0xb5, 0xe8 } };
static const GUID IID_IFrameworkElementStatics_UWP =
    { 0x48383032, 0xfbeb, 0x4f8a, { 0xae, 0xd2, 0xee, 0x21, 0xfb, 0x27, 0xa5, 0x7b } };
static const GUID IID_IControl_UWP =
    { 0xa8912263, 0x2951, 0x4f58, { 0xa9, 0xc5, 0x5a, 0x13, 0x4e, 0xaa, 0x7f, 0x07 } };
static const GUID IID_IControl2_UWP =
    { 0x0b9689ef, 0x1711, 0x41c8, { 0xa6, 0x27, 0x91, 0x3f, 0x0b, 0x97, 0x21, 0xf2 } };
static const GUID IID_IControl3_UWP =
    { 0x1a03e127, 0x8615, 0x4c67, { 0x8f, 0x78, 0x37, 0x39, 0x67, 0x8d, 0x20, 0x9d } };
static const GUID IID_IControl4_UWP =
    { 0x4122761c, 0x0b84, 0x49b8, { 0xa5, 0x14, 0x30, 0x19, 0xd5, 0x77, 0x5b, 0x24 } };
static const GUID IID_IControl5_UWP =
    { 0x21b87d65, 0x2e45, 0x4996, { 0x89, 0x56, 0x1b, 0xc2, 0x1b, 0x30, 0x8f, 0x10 } };
static const GUID IID_IControlOverrides_UWP =
    { 0xa09691df, 0x9824, 0x41fe, { 0xb5, 0x30, 0xb0, 0xd8, 0x99, 0x0e, 0x64, 0xc1 } };
static const GUID IID_IControlProtected_UWP =
    { 0x1791587a, 0x2a7c, 0x4981, { 0xa2, 0xd8, 0xab, 0x99, 0xff, 0xfc, 0x65, 0xb9 } };
static const GUID IID_IControlFactory_UWP =
    { 0xb12b1d5a, 0x6419, 0x4e16, { 0xb2, 0x12, 0xa4, 0x53, 0x36, 0xb7, 0x57, 0x78 } };
static const GUID IID_IContentControl_UWP =
    { 0xa26dd1dc, 0xcd44, 0x435c, { 0xbe, 0x94, 0x01, 0xd6, 0x24, 0x1c, 0x23, 0x1c } };
static const GUID IID_IContentControlOverrides_UWP =
    { 0x3874208b, 0x57d6, 0x4697, { 0x9f, 0x01, 0xf6, 0xf6, 0x64, 0x23, 0xa2, 0x2b } };
static const GUID IID_IContentControlFactory_UWP =
    { 0x6584ebdf, 0xcbc7, 0x4e19, { 0x84, 0x3a, 0xa1, 0x4f, 0x24, 0xa7, 0x93, 0xea } };
static const GUID IID_IContentControlFactory_UWP2 =
    { 0xab017b19, 0x196e, 0x4ed5, { 0xa2, 0x7f, 0x55, 0xc5, 0x8f, 0xb1, 0x67, 0x3f } };
static const GUID IID_IUserControl_UWP =
    { 0xa7a69ec9, 0xea35, 0x4679, { 0xbf, 0x29, 0xf4, 0xf0, 0x92, 0x86, 0xd3, 0x14 } };
static const GUID IID_IUserControlFactory_UWP =
    { 0x38b1ed92, 0xa28a, 0x4972, { 0x93, 0xdf, 0xf4, 0xf7, 0x59, 0xb8, 0xaf, 0xd2 } };
static const GUID IID_IPage_UWP =
    { 0xc4b27075, 0xe641, 0x453e, { 0x82, 0x4d, 0x01, 0x2f, 0xc7, 0xcf, 0x95, 0xcf } };
static const GUID IID_IPageOverrides_UWP =
    { 0x230b9cb8, 0x3419, 0x4306, { 0xaa, 0xbe, 0x4a, 0x8e, 0x68, 0x0d, 0x36, 0x27 } };
static const GUID IID_IPageFactory_UWP =
    { 0xdfa149ac, 0x1849, 0x445e, { 0x93, 0x7c, 0x40, 0xa9, 0x59, 0x0c, 0xc0, 0x76 } };
static const GUID IID_IFrame_UWP =
    { 0x68ea500b, 0x3fe9, 0x4735, { 0xb6, 0xa0, 0xe7, 0x60, 0x19, 0x33, 0xb0, 0x89 } };
static const GUID IID_INavigate_UWP =
    { 0xe7337f7a, 0x5e13, 0x4d32, { 0xb5, 0x72, 0x89, 0x35, 0x6e, 0x98, 0x1d, 0x96 } };
static const GUID IID_IButtonBase_UWP =
    { 0xfa002c1a, 0x494e, 0x46cf, { 0x91, 0xd4, 0xe1, 0x4a, 0x8d, 0x79, 0x86, 0x74 } };
static const GUID IID_IButtonBaseFactory_UWP =
    { 0x389b7c71, 0x5220, 0x42b2, { 0x99, 0x92, 0x26, 0x90, 0xc1, 0xa6, 0x70, 0x2f } };
static const GUID IID_IButtonBaseStatics_UWP =
    { 0x541c8d6c, 0x0283, 0x4581, { 0xb9, 0x45, 0x2a, 0x64, 0xc2, 0x8a, 0x06, 0x46 } };
static const GUID IID_IButton_UWP =
    { 0x280335ae, 0x5570, 0x46c7, { 0x8e, 0x0b, 0x60, 0x2b, 0xe7, 0x12, 0x29, 0xa2 } };
static const GUID IID_IButtonWithFlyout_UWP =
    { 0x06e32cce, 0xdfdd, 0x3639, { 0x94, 0xa5, 0xc7, 0xff, 0x91, 0x19, 0x93, 0xf8 } };
static const GUID IID_IButtonFactory_UWP =
    { 0xe2d795e3, 0x85c0, 0x4de2, { 0xba, 0xc1, 0x52, 0x94, 0xca, 0x01, 0x1a, 0x78 } };
static const GUID IID_IToggleButton_UWP =
    { 0x589877fb, 0x0fc7, 0x4036, { 0x9d, 0x8b, 0x12, 0x7d, 0xfa, 0x75, 0xc1, 0x6d } };
static const GUID IID_IToggleButtonOverrides_UWP =
    { 0x84e5b964, 0x4d3d, 0x49c1, { 0xab, 0xe7, 0xcb, 0xd3, 0xc5, 0x77, 0x49, 0x0d } };
static const GUID IID_IToggleButtonFactory_UWP =
    { 0x80a13c19, 0x843a, 0x451c, { 0x8c, 0xf5, 0x44, 0xc7, 0x01, 0xb0, 0xe2, 0x16 } };
static const GUID IID_IToggleButtonFactory2_UWP =
    { 0x13300b06, 0xbd10, 0x4e09, { 0xbf, 0xf7, 0x71, 0xef, 0xb8, 0xbb, 0xb4, 0x2b } };
static const GUID IID_IToggleButtonStatics_UWP =
    { 0xaf1eab12, 0x0128, 0x4f67, { 0x9c, 0x5a, 0x82, 0x32, 0x0c, 0x44, 0x5d, 0x19 } };
static const GUID IID_IHyperlinkButton_UWP =
    { 0x325c44e0, 0x9a03, 0x4bf3, { 0xab, 0xd6, 0x6f, 0xbb, 0x46, 0xc9, 0xa4, 0x86 } };
static const GUID IID_IPanel_UWP =
    { 0xa50a4bbd, 0x8361, 0x469c, { 0x90, 0xda, 0xe9, 0xa4, 0x0c, 0x74, 0x74, 0xdf } };
static const GUID IID_IPanelFactory_UWP =
    { 0xbfd07f53, 0xf8ca, 0x4e47, { 0x94, 0x9e, 0x9e, 0x80, 0x42, 0x9b, 0x3d, 0x16 } };
static const GUID IID_IPanelFactory_UWP2 =
    { 0xeee68111, 0xc7cc, 0x433f, { 0x95, 0xcd, 0xd6, 0x30, 0xc3, 0x43, 0x02, 0xdd } };
static const GUID IID_IGridFactory_UWP =
    { 0xae814041, 0xc531, 0x43b4, { 0xbf, 0x99, 0x12, 0xf5, 0x06, 0xf7, 0xb0, 0x1c } };
static const GUID IID_IListViewFactory_UWP =
    { 0xbdff696d, 0x3f22, 0x41f9, { 0x97, 0xa6, 0x88, 0x31, 0x34, 0xa7, 0x61, 0x13 } };
static const GUID IID_IXamlLightFactory_UWP =
    { 0x87ded768, 0x3055, 0x43b8, { 0x8e, 0xf6, 0x79, 0x8d, 0xc4, 0xc2, 0x32, 0x9a } };
static const GUID IID_IContentPresenterFactory_UWP =
    { 0x3339d4f7, 0x4815, 0x44c4, { 0x9f, 0x76, 0x68, 0xc8, 0x38, 0x80, 0xef, 0x10 } };
static const GUID IID_IXamlCompositionBrushBaseFactory_UWP =
    { 0x394f0823, 0x2451, 0x4ed8, { 0xbd, 0x24, 0x48, 0x81, 0x49, 0xb3, 0x42, 0x8d } };
static const GUID IID_IGrid_UWP =
    { 0x709133c7, 0x38f3, 0x5083, { 0xa4, 0xbb, 0x06, 0xa5, 0x48, 0xa1, 0xd2, 0x31 } };
static const GUID IID_IGrid_UWP2 =
    { 0xfd104460, 0x2e15, 0x4ba3, { 0x8b, 0x8f, 0xfa, 0x69, 0x3a, 0x41, 0x61, 0xe9 } };
static const GUID IID_IFlyoutBaseFactory_UWP =
    { 0xf82b435e, 0x65b3, 0x41c6, { 0xa9, 0xe2, 0x77, 0xb6, 0x7b, 0xc4, 0xc0, 0x0c } };
static const GUID IID_IStateTriggerBaseFactory_UWP =
    { 0x0fa0f896, 0x64c0, 0x45fb, { 0x8d, 0x24, 0xfb, 0x83, 0x29, 0x8c, 0x0d, 0x93 } };
static const GUID IID_IMenuFlyoutItemBaseFactory_UWP =
    { 0x9727b2e3, 0xb2b6, 0x49dc, { 0x8c, 0x22, 0xe0, 0x13, 0x8b, 0x94, 0x15, 0x59 } };
static const GUID IID_IMenuFlyoutItemFactory_UWP =
    { 0xc5aa39e2, 0x44f3, 0x455e, { 0x9c, 0x5b, 0x62, 0xbb, 0x6a, 0xfc, 0xe3, 0xad } };
static const GUID IID_IGridStatics_UWP =
    { 0xed02d274, 0x81f1, 0x5ac2, { 0xa0, 0xc3, 0xbb, 0xda, 0xf5, 0xa3, 0xff, 0xee } };
static const GUID IID_IGridStatics_UWP2 =
    { 0x1366e46e, 0x1e3c, 0x4765, { 0x85, 0x75, 0x98, 0xc3, 0x8b, 0x6b, 0x9e, 0x52 } };
static const GUID IID_IStackPanel_UWP =
    { 0x2a7993c3, 0x4230, 0x4132, { 0x97, 0x92, 0x19, 0x28, 0xd1, 0x41, 0x6b, 0x0f } };
static const GUID IID_IStackPanel_UWP2 =
    { 0xb8ae8fe2, 0xd641, 0x4fd7, { 0x80, 0xb4, 0x74, 0x39, 0x20, 0x7d, 0x27, 0x98 } };
static const GUID IID_IBorder_UWP =
    { 0x797c4539, 0x45bd, 0x4633, { 0xa0, 0x44, 0xbf, 0xb0, 0x2e, 0xf5, 0x17, 0x0f } };
static const GUID IID_ITextBlock_UWP =
    { 0xae2d9271, 0x3b4a, 0x45fc, { 0x84, 0x68, 0xf7, 0x94, 0x95, 0x48, 0xf4, 0xd5 } };
static const GUID IID_ITextBlock2_UWP =
    { 0x76b1a3d0, 0x8b05, 0x4362, { 0x8c, 0x0e, 0x61, 0x91, 0x10, 0x21, 0x97, 0x12 } };
static const GUID IID_ITextBlock3_UWP =
    { 0x062b92a1, 0x18c4, 0x49b2, { 0x9c, 0x00, 0x54, 0x0e, 0x20, 0x10, 0x01, 0x02 } };
static const GUID IID_ITextBlock6_UWP =
    { 0x902b92a1, 0x18c4, 0x49b2, { 0x9c, 0x00, 0x54, 0x0e, 0x20, 0x10, 0x01, 0x06 } };
static const GUID IID_IRowDefinition_UWP =
    { 0x4abae829, 0xd80c, 0x4a5e, { 0xa4, 0x8c, 0xf8, 0xb3, 0xd3, 0xb6, 0x53, 0x3d } };
static const GUID IID_IColumnDefinition_UWP =
    { 0xf7f1b229, 0xf024, 0x467f, { 0x97, 0x0a, 0x7e, 0x70, 0x56, 0x15, 0xdb, 0x7b } };
static const GUID IID_IScrollViewer_UWP =
    { 0x64e9be00, 0x4dc1, 0x493d, { 0xab, 0xe7, 0xcb, 0xd3, 0xc5, 0x77, 0x49, 0x0d } };
static const GUID IID_IScrollViewer2_UWP =
    { 0x64e9be10, 0x4dd1, 0x494d, { 0xab, 0xf7, 0xcb, 0xd3, 0xc5, 0x77, 0x49, 0x1d } };
static const GUID IID_IScrollViewer3_UWP =
    { 0x6b12000e, 0x2856, 0x475e, { 0x9e, 0x00, 0x52, 0x10, 0x1b, 0x92, 0x10, 0x03 } };
static const GUID IID_IStyle_UWP =
    { 0xc4a9f225, 0x9db7, 0x4a7d, { 0xb6, 0xd1, 0xf7, 0x4e, 0xdb, 0x92, 0x93, 0xc2 } };
static const GUID IID_ISetter_UWP =
    { 0xa73ded29, 0xb4ae, 0x4a87, { 0xbe, 0x85, 0xe6, 0x90, 0xba, 0x0d, 0x3b, 0x6e } };
static const GUID IID_ISetterBase_UWP =
    { 0x418be27c, 0x2ac4, 0x4f22, { 0x80, 0x97, 0xde, 0xa3, 0xae, 0xeb, 0x2f, 0xb3 } };
static const GUID IID_IBrush_UWP =
    { 0x8806a321, 0x1e06, 0x422c, { 0xa1, 0xcc, 0x01, 0x69, 0x65, 0x59, 0xe0, 0x21 } };
static const GUID IID_ISolidColorBrush_UWP =
    { 0x9d850850, 0x66f3, 0x48df, { 0x9a, 0x8f, 0x82, 0x4b, 0xd5, 0xe0, 0x70, 0xaf } };
static const GUID IID_IFlyoutBase_UWP =
    { 0x723eea0b, 0xd12e, 0x430d, { 0xa9, 0xf0, 0x9b, 0xb3, 0x2b, 0xbf, 0x99, 0x13 } };
static const GUID IID_IFlyoutBase3_UWP =
    { 0xa89c9712, 0x48e0, 0x4240, { 0x95, 0xb9, 0x0d, 0xfd, 0x08, 0x26, 0xa8, 0xd3 } };
static const GUID IID_IDispatcherTimer_UWP =
    { 0xd160ce46, 0xcd22, 0x4f5f, { 0x8c, 0x97, 0x40, 0xe6, 0x1d, 0xa3, 0xe2, 0xdc } };
static const GUID IID_IMenuFlyout_UWP =
    { 0x62081062, 0xb44a, 0x4210, { 0x91, 0xa8, 0x16, 0x75, 0x15, 0x25, 0x1a, 0x12 } };
static const GUID IID_IMenuFlyoutItem_UWP =
    { 0x160775cf, 0xb598, 0x44af, { 0xa4, 0x88, 0xc3, 0xce, 0x2a, 0xf6, 0xd3, 0xf0 } };
static const GUID IID_IMenuFlyoutItemBase_UWP =
    { 0x5c36c019, 0x3058, 0x427b, { 0x9e, 0x82, 0x82, 0x98, 0x1b, 0x3b, 0x3f, 0x19 } };
static const GUID IID_IMenuFlyout_UWP2 =
    { 0xad3210f1, 0x985b, 0x4171, { 0xbb, 0xba, 0x67, 0xbc, 0xc7, 0x29, 0x64, 0x9b } };
static const GUID IID_IMenuFlyoutItemBase_UWP2 =
    { 0xf9b56047, 0x8da6, 0x4730, { 0x92, 0x08, 0x14, 0x41, 0x25, 0xc6, 0xb3, 0x96 } };
static const GUID IID_IVisualStateGroup_UWP =
    { 0xe4f9d9a4, 0xe028, 0x44de, { 0x9b, 0x15, 0x49, 0x29, 0xae, 0x0a, 0x26, 0xc2 } };
static const GUID IID_IVisualState_UWP =
    { 0x6320affc, 0xc31a, 0x4450, { 0xaf, 0xde, 0xf6, 0xea, 0x7b, 0xd1, 0xf5, 0x86 } };
static const GUID IID_IVisualStateManagerStatics_UWP =
    { 0x0194e4c5, 0x4f70, 0x34c3, { 0xae, 0xb9, 0x60, 0x3f, 0x4b, 0xf5, 0x32, 0x12 } };
static const GUID IID_IStateTriggerBase_UWP =
    { 0x48b20698, 0xaf06, 0x466c, { 0x80, 0x52, 0x93, 0x66, 0x6d, 0xde, 0x0e, 0x49 } };
static const GUID IID_IStateTriggerBaseProtected_UWP =
    { 0x3c41e253, 0x8d14, 0x4216, { 0x99, 0x4c, 0xf9, 0x93, 0x04, 0x29, 0xf6, 0xe5 } };
static const GUID IID_IMarkupExtension_UWP =
    { 0x1bc32a2c, 0x2236, 0x45f0, { 0x88, 0x54, 0x5b, 0x3b, 0x19, 0x26, 0xd1, 0x20 } };
static const GUID IID_IMarkupExtensionOverrides_UWP =
    { 0x393d791f, 0x5389, 0x452c, { 0x88, 0x59, 0x8c, 0x54, 0x92, 0x04, 0x26, 0x19 } };
static const GUID IID_IMarkupExtensionFactory_UWP =
    { 0x65329c05, 0xfb5a, 0x4567, { 0x9d, 0x55, 0x5c, 0xdf, 0xba, 0xda, 0x27, 0x39 } };
static const GUID IID_IXamlBindingHelperStatics_UWP =
    { 0xf65cfb71, 0xc80c, 0x4faf, { 0x86, 0xee, 0x55, 0x87, 0x54, 0xee, 0x33, 0x6d } };
static const GUID IID_IComponentConnector_UWP_g =
    { 0xf6790987, 0xe6e5, 0x47f2, { 0x92, 0xc6, 0xec, 0xcc, 0xe4, 0xba, 0x15, 0x9a } };
static const GUID IID_IComponentConnector2_UWP_g =
    { 0xdc8f368b, 0xeccc, 0x498e, { 0xb1, 0x39, 0x91, 0x14, 0x22, 0x54, 0xd7, 0xae } };
static const GUID IID_IXamlMetadataProvider_UWP_g =
    { 0xb3765d69, 0x68a5, 0x4b32, { 0x88, 0x61, 0xfd, 0xb9, 0x0c, 0x1f, 0x58, 0x36 } };
static const GUID IID_IXamlType_UWP_g =
    { 0x7920eab1, 0xb2e5, 0x41ea, { 0x8d, 0x86, 0xe1, 0x0f, 0x7b, 0x37, 0x73, 0x37 } };
static const GUID IID_ICommand_UWP =
    { 0xe5af3542, 0xca67, 0x4080, { 0x99, 0x5b, 0x70, 0x9d, 0xd1, 0x37, 0x92, 0xdf } };
static const GUID IID_ICommand_UWP2 =
    { 0xe5af3542, 0xca67, 0x4081, { 0x99, 0x5b, 0x70, 0x9d, 0xd1, 0x37, 0x92, 0xdf } };
static const GUID IID_IPropertyValue_g =
    { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };
static const GUID IID_IPropertyValueStatics_g =
    { 0x629bdbc8, 0xd932, 0x4ff4, { 0x96, 0xb9, 0x8d, 0x96, 0xc5, 0xc1, 0xe8, 0x58 } };
static const GUID IID_IDependencyPropertyChangedEventArgs_UWP =
    { 0x81212c2b, 0x24d0, 0x4957, { 0xab, 0xc3, 0x22, 0x44, 0x70, 0xa9, 0x3a, 0x4e } };
static const GUID IID_IDataContextChangedEventArgs_UWP =
    { 0x7da68e21, 0x0b8f, 0x4f9f, { 0xa1, 0x43, 0xf8, 0xe7, 0x78, 0x01, 0x36, 0xa2 } };
static const GUID IID_IRadioButton_UWP =
    { 0xd20e4c28, 0xf18b, 0x491a, { 0xa0, 0xf1, 0x45, 0x9a, 0x93, 0x4a, 0xa4, 0x69 } };
static const GUID IID_IRadioButtons_WinUI =
    { 0xdd372d64, 0xb740, 0x5739, { 0x8d, 0x24, 0xb6, 0xde, 0xcc, 0x91, 0xf4, 0x08 } };
static const GUID IID_IUriRuntimeClass_g =
    { 0x9e365e57, 0x48b2, 0x4160, { 0x95, 0x6f, 0xc7, 0x38, 0x51, 0x20, 0xbb, 0xfc } };

/* CalculatorApp / MCG additional discovered interface IIDs */
static const GUID IID_IGrid_Calc =
    { 0x280335ae, 0x5570, 0x46c7, { 0x8e, 0x0b, 0x60, 0x2b, 0xe7, 0x12, 0x29, 0xa2 } };
static const GUID IID_IUIElement_Calc2 =
    { 0x33d1299d, 0x8d6e, 0x4290, { 0xbf, 0x22, 0x90, 0x5c, 0xcc, 0xd0, 0x4d, 0x31 } };
static const GUID IID_INavigationView_UWP =
    { 0xb00eb54c, 0x9174, 0x5d84, { 0x96, 0x78, 0x56, 0xc9, 0x80, 0x16, 0xe6, 0x89 } };
static const GUID IID_INavigationView2_UWP =
    { 0x5ba2eefc, 0x3736, 0x5e42, { 0xac, 0x56, 0x9d, 0x0b, 0xe5, 0x52, 0x3d, 0x40 } };
static const GUID IID_IPopup_UWP =
    { 0x62418240, 0xe6d3, 0x4705, { 0xa1, 0xdc, 0x39, 0x15, 0x64, 0x56, 0xee, 0x29 } };
static const GUID IID_IPopup2_UWP =
    { 0x376a8c4c, 0xaac0, 0x4b20, { 0x96, 0x6a, 0x0b, 0x93, 0x64, 0xfe, 0xb4, 0xb5 } };
static const GUID IID_IPopup3_UWP =
    { 0x1870b836, 0xdf2f, 0x5fc6, { 0xa5, 0xf2, 0x74, 0x8e, 0xd6, 0xce, 0x73, 0x21 } };
static const GUID IID_IContentControl2_UWP =
    { 0x652c208c, 0x8ccf, 0x4399, { 0xbd, 0x3e, 0x5a, 0x01, 0x5a, 0xa1, 0xbc, 0x03 } };
static const GUID IID_IContentControlOvr_Calc =
    { 0x5f4c0b10, 0xe38e, 0x4b5d, { 0xbe, 0x1a, 0x5e, 0xd0, 0x42, 0x46, 0xa6, 0x35 } };
static const GUID IID_IBorder2_Calc =
    { 0xa1efbd9d, 0xcb8b, 0x56b5, { 0x87, 0x8d, 0x3a, 0x7f, 0x43, 0xbc, 0x97, 0x30 } };
static const GUID IID_IGrid3_Calc =
    { 0xf76efa41, 0x380e, 0x45db, { 0xbe, 0x87, 0x9e, 0x13, 0x26, 0xba, 0x4b, 0x57 } };
static const GUID IID_ITextBlock_Calc2 =
    { 0x45206a4c, 0x0a4e, 0x4fbf, { 0xae, 0xe9, 0x33, 0x5d, 0x5a, 0x20, 0x5f, 0x6e } };
static const GUID IID_ITextBlock_Calc3 =
    { 0xb651dd5a, 0x424b, 0x48c3, { 0x9b, 0x67, 0x48, 0x6f, 0x4e, 0x13, 0x98, 0x99 } };
static const GUID IID_ITextBlock_Calc4 =
    { 0x31816cd9, 0x0404, 0x410f, { 0x91, 0xec, 0x33, 0xb0, 0xa0, 0xbb, 0x92, 0xb2 } };
static const GUID IID_IUIE_Extra[] =
{
    { 0xbc2b28f1, 0x26f2, 0x4aab, { 0xb2, 0x56, 0x3b, 0x53, 0x50, 0x88, 0x1e, 0x37 } },
    { 0x8eed9bc2, 0xa58c, 0x4453, { 0xaf, 0x0f, 0xa9, 0x2e, 0xe0, 0x6d, 0x03, 0x17 } },
    { 0xcafc4968, 0x6369, 0x4249, { 0x80, 0xf9, 0x3d, 0x65, 0x63, 0x19, 0xe8, 0x11 } },
    { 0x3ab70e85, 0xd508, 0x4477, { 0xb6, 0xf8, 0x0e, 0x43, 0x57, 0x01, 0xc8, 0x36 } },
    { 0xb4a04776, 0x4e88, 0x50ca, { 0x8f, 0x2b, 0x08, 0x94, 0x0d, 0x6c, 0x5f, 0x94 } },
    { 0xd531c629, 0xad2c, 0x5f6b, { 0xad, 0xcf, 0xfb, 0x87, 0x28, 0x7d, 0x18, 0xd7 } },
    { 0xb97f7f68, 0xc29b, 0x4c99, { 0xa1, 0xc3, 0x95, 0x26, 0x19, 0xd6, 0xe7, 0x20 } },
    { 0x4a5a645c, 0x548d, 0x48cf, { 0xb9, 0x98, 0x78, 0x44, 0xd6, 0xe2, 0x35, 0xa1 } },
    { 0x9a6e5973, 0x6d63, 0x54f2, { 0x90, 0xfa, 0x62, 0x81, 0x3b, 0x20, 0xb7, 0xb9 } },
    { 0xe7141e0a, 0x04b8, 0x4fc5, { 0xa4, 0xdc, 0x19, 0x53, 0x92, 0xe5, 0x78, 0x07 } },
    { 0x01e64612, 0x1d82, 0x42f4, { 0x8e, 0x3f, 0xa7, 0x22, 0xde, 0xd3, 0x3f, 0xc7 } },
    { 0x6b765bb3, 0xfba3, 0x4404, { 0xbd, 0xee, 0x1a, 0x45, 0xd1, 0xca, 0x5f, 0x21 } },
    { 0x792a5d91, 0x62a1, 0x40bf, { 0xa0, 0xce, 0xf9, 0xc1, 0x31, 0xfc, 0xb7, 0xa7 } },
    { 0x2263886c, 0xc069, 0x570f, { 0xb9, 0xcc, 0x9e, 0x21, 0xdd, 0x02, 0x8d, 0x8e } },
    { 0x65aa0480, 0x22e3, 0x5103, { 0xad, 0x2a, 0xb6, 0x26, 0xf8, 0x8c, 0xa5, 0xae } },
    { 0x43e0fe10, 0x14ec, 0x427e, { 0x8c, 0x57, 0xde, 0xe6, 0x0d, 0xf6, 0x0a, 0xa8 } },
    { 0x077b29e4, 0xdade, 0x4f55, { 0x9b, 0x96, 0x09, 0xe2, 0x1b, 0x28, 0xeb, 0x1c } },
    { 0x0e18aeee, 0x5f2e, 0x44ea, { 0x85, 0x13, 0xd3, 0x87, 0x5c, 0x0a, 0x75, 0x13 } },
    { 0x5ebb51d5, 0x9e58, 0x49b7, { 0xbc, 0x2d, 0x01, 0x55, 0xff, 0x11, 0x86, 0x64 } },
    { 0x3550dbe1, 0xe561, 0x5934, { 0xa3, 0xef, 0x7d, 0xb8, 0xbf, 0x6e, 0x3b, 0x45 } },
    { 0xe194c96a, 0xd8cc, 0x4339, { 0xa0, 0xe1, 0x2e, 0x9c, 0x42, 0x08, 0x79, 0xec } },
    /* IBinding, IBinding2, IBindingBase, IPropertyPath, IRelativeSource, IStoryboard, IRun */
    { 0x3f7a0c6b, 0xd00f, 0x4730, { 0x8c, 0x1d, 0x48, 0xe1, 0x6c, 0x46, 0xf9, 0xca } },
    { 0x34f96fcb, 0x0406, 0x48b3, { 0x9e, 0x82, 0xf3, 0x33, 0xec, 0x4c, 0x69, 0x10 } },
    { 0x1589a2ab, 0x3d15, 0x49bc, { 0xa4, 0x47, 0x8a, 0x54, 0x48, 0xe5, 0x88, 0x70 } },
    { 0x300e5d8a, 0x1ff3, 0x4d2c, { 0x95, 0xec, 0x27, 0xf8, 0x1d, 0xeb, 0xac, 0xb8 } },
    { 0x2397ce84, 0x2822, 0x483a, { 0xb4, 0x99, 0xd0, 0xf0, 0x31, 0xe0, 0x6c, 0x6b } },
    { 0x0bc465dc, 0xbe4d, 0x4d0d, { 0x95, 0x49, 0x22, 0x08, 0xb7, 0x15, 0xf4, 0x0d } },
    { 0x59553c83, 0x0e14, 0x49bd, { 0xb8, 0x4b, 0xc5, 0x26, 0xf3, 0x03, 0x43, 0x49 } },
    /* MenuFlyout, MenuFlyoutItem, Flyout, Hyperlink, RichTextBlock, Storyboard, ValueConverter */
    { 0xad3210f1, 0x985b, 0x4171, { 0xbb, 0xba, 0x67, 0xbc, 0xc7, 0x29, 0x64, 0x9b } },
    { 0x507c7bad, 0xd9d1, 0x4c05, { 0x9d, 0x4b, 0xcd, 0xa8, 0xde, 0x9a, 0xb2, 0x42 } },
    { 0x0ae266eb, 0x028c, 0x4423, { 0xa8, 0xe3, 0x98, 0x9f, 0xd9, 0xdd, 0x71, 0x26 } },
    { 0x6584ebdf, 0xcbc7, 0x4e19, { 0x84, 0x3a, 0xa1, 0x4f, 0x24, 0xa7, 0x93, 0xea } },
    { 0xccebaca3, 0x3b5c, 0x4f4c, { 0x9b, 0xfd, 0x86, 0x88, 0x7b, 0xc7, 0x97, 0x72 } },
    { 0xe5fff9e2, 0xb968, 0x49e7, { 0x97, 0xd4, 0x8c, 0xca, 0x2a, 0xc3, 0xae, 0x7c } },
    { 0x9c7dc0c6, 0x7a9f, 0x45b6, { 0x88, 0x18, 0x82, 0x2c, 0x54, 0x9b, 0xa0, 0x94 } },
    { 0xd45c1e6e, 0x3594, 0x460e, { 0x98, 0x1a, 0x32, 0x27, 0x1b, 0xd3, 0xaa, 0x06 } },
    { 0x7b01c14b, 0xe5e6, 0x3c6e, { 0x9d, 0x1b, 0x93, 0xa4, 0x7c, 0xbf, 0x7d, 0x96 } },
    /* DataTemplateSelector & StyleSelector interfaces */
    { 0xa26dd1dc, 0xcd44, 0x435c, { 0xbe, 0x94, 0x01, 0xd6, 0x24, 0x1c, 0x23, 0x1c } },
    { 0x51ed9d7e, 0x2b53, 0x475b, { 0x9c, 0x88, 0x0c, 0x18, 0x32, 0xc8, 0x35, 0x1a } },
    { 0x8af77d73, 0xaa01, 0x471e, { 0xbe, 0xdd, 0x8b, 0xad, 0x86, 0x21, 0x9b, 0x77 } },
    { 0x595e9547, 0xcdff, 0x4b92, { 0xb7, 0x73, 0xab, 0x39, 0x68, 0x78, 0xf3, 0x53 } },
    { 0xa907d496, 0x46a0, 0x4cd7, { 0x8d, 0xbe, 0xf9, 0xa5, 0x81, 0xdf, 0x60, 0xb1 } },
    { 0x2d162925, 0x5966, 0x4bfa, { 0x86, 0x38, 0x4d, 0x34, 0x56, 0x89, 0xf6, 0xcf } },
    /* WinUI 2.x / UWP Controls (TabViewItem, Expander, AnimatedIcon, ProgressRing, etc.) */
    { 0x291f3e98, 0x4f17, 0x5021, { 0x94, 0xf0, 0x6a, 0x5b, 0x30, 0x43, 0x12, 0xb6 } },
    { 0xcd1d4ede, 0x8d39, 0x5bef, { 0xa7, 0x35, 0x9f, 0xdf, 0x60, 0x38, 0xe8, 0x6b } },
    { 0xf705dfda, 0x8196, 0x56d0, { 0x8d, 0xcf, 0x2b, 0x66, 0xc2, 0xae, 0xd7, 0x91 } },
    { 0x2670d03f, 0xe28c, 0x5652, { 0xbe, 0xe2, 0xb5, 0x21, 0x2e, 0xbd, 0xf7, 0xff } },
    { 0xc1e9fa91, 0x4f95, 0x5796, { 0x8a, 0x7b, 0x3b, 0x75, 0x94, 0xa1, 0x2c, 0x69 } },
    { 0xf8f5b8bc, 0x8d67, 0x5fa9, { 0x8f, 0xb0, 0xc2, 0xc3, 0x31, 0x1e, 0x1b, 0x7c } },
    { 0x6e3501ed, 0xdd31, 0x51e9, { 0x8f, 0x14, 0x25, 0x61, 0xf9, 0x9c, 0x8a, 0x8f } },
    { 0x00378a74, 0x790b, 0x5328, { 0x8a, 0xfa, 0x7d, 0x65, 0xe2, 0x2d, 0xa4, 0x26 } },
    { 0x4459a451, 0x69e8, 0x440c, { 0x98, 0x96, 0x4b, 0xb4, 0xf5, 0xf6, 0x42, 0xd1 } },
    { 0x331d8f00, 0xc5f9, 0x46a5, { 0xb6, 0xc8, 0xed, 0xe5, 0x39, 0x30, 0x45, 0x67 } },
    { 0xe48f5a8b, 0x1dff, 0x4352, { 0xa1, 0xf4, 0xe5, 0x16, 0x51, 0x4e, 0xc8, 0x82 } },
    { 0x7df18c3c, 0x2826, 0x425e, { 0x9a, 0x94, 0x00, 0xb7, 0xf7, 0x3f, 0x37, 0x56 } },
    { 0x41ff2d07, 0x5085, 0x4e52, { 0xa5, 0x1c, 0xf9, 0x4b, 0x06, 0xcd, 0x55, 0x8b } },
    { 0xb9a8d05c, 0xac97, 0x47f1, { 0xa5, 0xf4, 0x3f, 0x9f, 0x4d, 0x4b, 0x11, 0x6c } },
    { 0x79190e19, 0xcd38, 0x4823, { 0xae, 0xae, 0x64, 0xa7, 0x71, 0x32, 0xf5, 0x19 } },
    { 0x79fde5b4, 0xcd37, 0x491c, { 0x88, 0x45, 0xda, 0xf4, 0x72, 0xde, 0xff, 0xf6 } },
    { 0x7de5d75f, 0x4415, 0x429f, { 0xb5, 0x23, 0x69, 0x9b, 0xdb, 0x8c, 0x30, 0xdb } },
    { 0x855bc230, 0x8a11, 0x4e18, { 0xa1, 0x36, 0x4b, 0xc2, 0x1c, 0x78, 0x27, 0xb0 } },
    { 0x9910aec7, 0x8ab5, 0x4118, { 0x9b, 0xc6, 0x09, 0xf4, 0x5a, 0x35, 0x07, 0x3d } },
    { 0x688ea9b9, 0x1e4e, 0x4596, { 0x86, 0xe3, 0x42, 0x8b, 0x27, 0x33, 0x4f, 0xaf } },
    { 0xc975905c, 0x3c36, 0x4229, { 0x81, 0x7b, 0x17, 0x8f, 0x64, 0xc0, 0xe1, 0x13 } },
    { 0x7e9f3d59, 0x0f07, 0x4bc9, { 0x97, 0x7d, 0x03, 0x76, 0x3f, 0xf8, 0x15, 0x4f } },
    { 0xf57ae6ca, 0xd1a6, 0x4b90, { 0xa4, 0xe9, 0x54, 0xdf, 0x1b, 0xa8, 0xd2, 0xec } },
    { 0x67918e1b, 0xf2d5, 0x44d1, { 0x8b, 0x84, 0x92, 0xb8, 0x7f, 0x80, 0xa3, 0x50 } },
    { 0x3d0813ba, 0x6890, 0x4537, { 0xbf, 0xe5, 0x79, 0x6d, 0x94, 0x58, 0xed, 0xd6 } },
    { 0x53c9f23a, 0xf25d, 0x446a, { 0x99, 0xb8, 0xc7, 0xae, 0xb8, 0x39, 0x05, 0x0f } },
    { 0x754c3266, 0x0529, 0x45e3, { 0x87, 0x48, 0xbf, 0x74, 0x7d, 0x15, 0x83, 0x57 } },
    { 0xf82b435e, 0x65b3, 0x41c6, { 0xa9, 0xe2, 0x77, 0xb6, 0x7b, 0xc4, 0xc0, 0x0c } },
    { 0xaa87ddde, 0xe679, 0x45b2, { 0xa7, 0xc9, 0x9a, 0xed, 0xc3, 0x9d, 0xb8, 0x86 } },
    { 0x16c724fb, 0x6c1a, 0x4852, { 0x92, 0x58, 0x08, 0xc0, 0x38, 0x36, 0x79, 0x07 } },
    { 0xf3709e8b, 0x091a, 0x5db5, { 0xb8, 0x96, 0xff, 0x78, 0xf0, 0x19, 0x90, 0xc9 } },
    { 0x0e1ebbd4, 0xa003, 0x4936, { 0x81, 0x75, 0xf5, 0x45, 0x7c, 0x07, 0xf0, 0xc6 } },
    { 0xe98babe7, 0xf6ff, 0x444c, { 0x9c, 0x0d, 0x27, 0x7e, 0xaf, 0x0a, 0xd9, 0xc0 } },
    { 0xb90ad781, 0xbfeb, 0x4451, { 0xbd, 0x47, 0x9f, 0x3a, 0x63, 0xeb, 0xd2, 0x4a } },
    { 0xb9c0b997, 0x2820, 0x44a1, { 0xa5, 0xa8, 0x9b, 0x80, 0x1e, 0xdc, 0x26, 0x9e } },
    { 0xb618fd7b, 0x32d0, 0x4970, { 0x9c, 0x42, 0x7c, 0x03, 0x9a, 0xc7, 0xbe, 0x78 } },
    { 0x89839403, 0xb538, 0x49e3, { 0xa4, 0x30, 0x3a, 0xc0, 0x37, 0xdc, 0x6f, 0xe0 } },
    { 0x35aac87a, 0x62ee, 0x4d3e, { 0xa2, 0x4c, 0x2b, 0xc8, 0x43, 0x2d, 0x68, 0xb7 } },
    { 0x2cf95efb, 0xa7a2, 0x5d85, { 0x8e, 0xad, 0xea, 0x22, 0x2b, 0xaa, 0x3c, 0x55 } },
    { 0xb64c2423, 0x7e56, 0x5d41, { 0x8a, 0x84, 0x1e, 0xe2, 0x8f, 0x98, 0x26, 0xa4 } },
    { 0x4459a451, 0x69e8, 0x440c, { 0x98, 0x96, 0x4b, 0xb4, 0xf5, 0xf6, 0x42, 0xd1 } },
    { 0x688ea9b9, 0x1e4e, 0x4596, { 0x86, 0xe3, 0x42, 0x8b, 0x27, 0x33, 0x4f, 0xaf } },
    { 0xd4d61391, 0x39ac, 0x4950, { 0x91, 0x66, 0x36, 0x06, 0xc2, 0x64, 0x41, 0x8b } },
    { 0x9af0803b, 0xd04c, 0x467a, { 0xbb, 0xd5, 0x9b, 0x81, 0xf0, 0x2d, 0x9a, 0x56 } }, /* IIconElement */
    { 0xac935626, 0x190a, 0x4de2, { 0x8d, 0x71, 0x7c, 0xc4, 0x74, 0x7d, 0xa5, 0x80 } }, /* IMenuFlyoutSubItem2 */
};
static const GUID IID_ITabView_WinUI =
    { 0x6aa787ab, 0x5a30, 0x5ea2, { 0xbe, 0x5b, 0xae, 0xd8, 0x68, 0x38, 0x17, 0x56 } };
static const GUID IID_IMenuBar_WinUI =
    { 0xba97f337, 0x8f1e, 0x5141, { 0xb5, 0x3f, 0xe7, 0x7a, 0x8b, 0xa3, 0xeb, 0xbd } };
static const GUID IID_IMenuBarItem_WinUI =
    { 0x9caa4261, 0x3a0c, 0x5a59, { 0x8c, 0xfe, 0x39, 0x17, 0x44, 0xc3, 0xf8, 0x15 } };
static const GUID IID_IMenuFlyoutSubItem_UWP =
    { 0xe5386550, 0xfe2c, 0x43e2, { 0xa7, 0x3f, 0x62, 0x16, 0x8f, 0x61, 0x99, 0x73 } };
static const GUID IID_IMenuFlyout2_Real_UWP =
    { 0x507c7bad, 0xd9d1, 0x4c05, { 0x9d, 0x4b, 0xcd, 0xa8, 0xde, 0x9a, 0xb2, 0x42 } };
static const GUID IID_IMenuFlyoutItem3_UWP =
    { 0x9727b2e3, 0xb2b6, 0x49dc, { 0x8c, 0x22, 0xe0, 0x13, 0x8b, 0x94, 0x15, 0x59 } };
static const GUID IID_IToggleMenuFlyoutItem_UWP =
    { 0x49841edd, 0x88e9, 0x4663, { 0xa7, 0x01, 0xcd, 0x4f, 0xd2, 0x5e, 0x39, 0x8f } };
static const GUID IID_IContentDialog_UWP =
    { 0x103e9b13, 0x3400, 0x4a16, { 0x90, 0xb9, 0x69, 0x12, 0xbf, 0x06, 0x97, 0x4f } };
static const GUID IID_IContentDialog2_UWP =
    { 0x2f93eb45, 0xee43, 0x4303, { 0x9b, 0x38, 0x3f, 0xe1, 0xa1, 0x11, 0xec, 0xbf } };
static const GUID IID_IContentDialogFactory_UWP =
    { 0x05557178, 0x9d8e, 0x4315, { 0xb3, 0x7d, 0x68, 0x0c, 0x14, 0x01, 0x2c, 0x35 } };
static const GUID IID_IContentDialogClosedEventArgs_UWP =
    { 0x4d6f20f2, 0x75d5, 0x4269, { 0x9f, 0x5c, 0xd6, 0x5b, 0x73, 0xb5, 0x33, 0x2d } };
static const GUID IID_IUIElement4_Real_UWP =
    { 0x69145cd4, 0x199a, 0x4657, { 0x9e, 0x57, 0xe9, 0x9e, 0x8f, 0x13, 0x67, 0x12 } };
static const GUID IID_IUIElement7_UWP =
    { 0xcafc4968, 0x6369, 0x4249, { 0x80, 0xf9, 0x3d, 0x65, 0x63, 0x19, 0xe8, 0x11 } };
static const GUID IID_IDesktopWindowXamlSourceFactory_UWP =
    { 0x5cd61dc0, 0x2561, 0x56e1, { 0x8e, 0x75, 0x6e, 0x44, 0x17, 0x38, 0x05, 0xe3 } };
static const GUID IID_IDesktopWindowXamlSource_UWP =
    { 0xd585bfe1, 0x00ff, 0x51be, { 0xba, 0x1d, 0xa1, 0x32, 0x99, 0x56, 0xea, 0x0a } };
static const GUID IID_IDesktopWindowXamlSourceNative_UWP =
    { 0x3cbcf1bf, 0x2f76, 0x4e9c, { 0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54 } };
static const GUID IID_IDesktopWindowXamlSourceNative2_UWP =
    { 0xe3dcd8c7, 0x3057, 0x4692, { 0x99, 0xc3, 0x7b, 0x77, 0x20, 0xaf, 0xda, 0x31 } };
static const GUID IID_IUIElement10_UWP =
    { 0xd531c629, 0xad2c, 0x5f6b, { 0xad, 0xcf, 0xfb, 0x87, 0x28, 0x7d, 0x18, 0xd7 } };
static const GUID IID_IXamlRoot_UWP =
    { 0x34b50756, 0x1696, 0x5b6d, { 0x8e, 0x9b, 0xc7, 0x14, 0x64, 0xcc, 0xad, 0x5a } };
static const GUID IID_IDataTemplateSelectorFactory_UWP =
    { 0x511baad0, 0x210a, 0x457d, { 0x95, 0x21, 0x43, 0x3d, 0x2b, 0x11, 0xb7, 0xd2 } };
static const GUID IID_IStyleSelectorFactory_UWP =
    { 0x6c3fb5fc, 0x8e84, 0x4c91, { 0xac, 0x15, 0x04, 0xa1, 0xd8, 0x85, 0x50, 0x0f } };
static const GUID IID_IStyleSelectorFactory_UWP2 =
    { 0x9e942ae7, 0x3761, 0x447f, { 0x8f, 0x97, 0x29, 0xe3, 0x9d, 0x5e, 0xb3, 0x10 } };
static const GUID IID_IStateTriggerBaseFactory2_UWP =
    { 0x970e2c4b, 0xbfaf, 0x47b0, { 0xbe, 0x42, 0xc1, 0xd7, 0x11, 0xbb, 0x2e, 0x9f } };
static const GUID IID_IBindingFactory_UWP =
    { 0xff42bb08, 0xc39e, 0x4f7e, { 0x84, 0x34, 0xa1, 0x56, 0x90, 0x83, 0x88, 0x3c } };
static const GUID IID_IRelativeSourceFactory_UWP =
    { 0xef8392cd, 0x446e, 0x4f93, { 0xaa, 0xcb, 0x9b, 0x12, 0x55, 0x57, 0x74, 0x60 } };
static const GUID IID_IRadioButtonFactory_UWP =
    { 0xf1d04933, 0x34e1, 0x4a5c, { 0xb2, 0xae, 0xca, 0x3b, 0x1c, 0x0b, 0x20, 0xde } };
static const GUID IID_IPropertyPathFactory_UWP =
    { 0x4e4cdf99, 0x9826, 0x4e56, { 0x84, 0x7c, 0xca, 0x05, 0x5f, 0x16, 0x29, 0x05 } };
static const GUID IID_IDependencyObjectCollectionFactory_UWP =
    { 0x051e79ff, 0xb3a8, 0x49ee, { 0xb5, 0xaf, 0xac, 0x8f, 0x68, 0xb6, 0x49, 0xe4 } };
static const GUID IID_IMenuFlyoutSeparatorFactory_UWP =
    { 0xc159dcc8, 0xb437, 0x44e0, { 0xb2, 0x4b, 0x57, 0x20, 0xae, 0xa1, 0xdb, 0xac } };
static const GUID IID_IKeyboardAccelerator_UWP =
    { 0x92e6181e, 0x19ae, 0x465a, { 0x9b, 0x3c, 0xa7, 0x1e, 0xe9, 0xea, 0x74, 0x20 } };
static const GUID IID_IKeyboardAcceleratorInvokedEventArgs_UWP =
    { 0xd300a9f6, 0x2f7e, 0x4873, { 0xa5, 0x55, 0x16, 0x6e, 0x59, 0x6e, 0x1c, 0x5a } };
static const GUID IID_IKeyboardAcceleratorFactory_UWP =
    { 0x44d88a99, 0x4bfd, 0x4a47, { 0xa8, 0x93, 0x51, 0x5f, 0x38, 0x86, 0x23, 0xf6 } };
static const GUID IID_IXamlUICommand_UWP =
    { 0x8494f8d4, 0xead1, 0x5f01, { 0xad, 0x2e, 0xa8, 0xca, 0xd4, 0xf9, 0xdc, 0x0e } };
static const GUID IID_IExecuteRequestedEventArgs_UWP =
    { 0xe07fa734, 0xa0b6, 0x5755, { 0x9e, 0x87, 0x24, 0xf5, 0x4c, 0xca, 0x93, 0x72 } };
static const GUID IID_ITeachingTip_WinUI =
    { 0x5b200440, 0x4fcc, 0x5fdb, { 0xb4, 0x18, 0xb5, 0x79, 0x7b, 0x08, 0x74, 0xad } };
static const GUID IID_IKeyRoutedEventArgs_UWP =
    { 0xd4cd3dfe, 0x4079, 0x42e9, { 0xa3, 0x9a, 0x30, 0x95, 0xd3, 0xf0, 0x49, 0xc6 } };
static const GUID IID_IDispatcherTimerFactory_UWP =
    { 0xe9961e6e, 0x3626, 0x403a, { 0xaf, 0xe0, 0x04, 0x0d, 0x58, 0x16, 0x56, 0x32 } };

/* ============================================================================
 * 1. PRI v2 (mrm_pri2) Container Reader
 * ============================================================================ */

static const BYTE *g_pri_data = NULL;
static DWORD g_pri_size = 0;
static INIT_ONCE g_pri_once = INIT_ONCE_STATIC_INIT;

static const BYTE *g_winui_pri_data = NULL;
static DWORD g_winui_pri_size = 0;
static BOOL g_winui_pri_probed = FALSE;

static void pri_map_file_to( const WCHAR *pri_path, const BYTE **out_data, DWORD *out_size )
{
    HANDLE hFile, hMap;
    DWORD sz;

    hFile = CreateFileW( pri_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (hFile != INVALID_HANDLE_VALUE)
    {
        sz = GetFileSize( hFile, NULL );
        if (sz > 0x40 && sz < 64 * 1024 * 1024)
        {
            hMap = CreateFileMappingW( hFile, NULL, PAGE_READONLY, 0, 0, NULL );
            if (hMap)
            {
                const BYTE *mapped = MapViewOfFile( hMap, FILE_MAP_READ, 0, 0, 0 );
                if (mapped && !memcmp( mapped, "mrm_pri2", 8 ))
                {
                    *out_data = mapped;
                    *out_size = sz;
                    TRACE( "Mapped %s (%lu bytes) at %p\n", debugstr_w(pri_path), (unsigned long)sz, mapped );
                }
                else if (mapped)
                {
                    UnmapViewOfFile( mapped );
                }
                CloseHandle( hMap );
            }
        }
        CloseHandle( hFile );
    }
}

static BOOL WINAPI init_pri_once( INIT_ONCE *once, void *param, void **ctx )
{
    WCHAR pri_path[MAX_PATH];
    WCHAR *slash;

    GetModuleFileNameW( NULL, pri_path, MAX_PATH );
    slash = wcsrchr( pri_path, '\\' );
    if (slash) *(slash + 1) = 0;
    lstrcatW( pri_path, L"resources.pri" );
    pri_map_file_to( pri_path, &g_pri_data, &g_pri_size );
    return TRUE;
}

static const BYTE *pri_get_data( DWORD *out_size )
{
    InitOnceExecuteOnce( &g_pri_once, init_pri_once, NULL, NULL );
    if (out_size) *out_size = g_pri_size;
    return g_pri_data;
}

static const BYTE *winui_pri_get_data( DWORD *out_size )
{
    if (!g_winui_pri_probed)
    {
        HMODULE hmod = GetModuleHandleW( L"Microsoft.UI.Xaml.dll" );
        if (hmod)
        {
            WCHAR pri_path[MAX_PATH];
            WCHAR *slash;
            g_winui_pri_probed = TRUE;
            if (GetModuleFileNameW( hmod, pri_path, MAX_PATH ))
            {
                slash = wcsrchr( pri_path, '\\' );
                if (slash) *(slash + 1) = 0;
                lstrcatW( pri_path, L"resources.pri" );
                pri_map_file_to( pri_path, &g_winui_pri_data, &g_winui_pri_size );
            }
        }
    }
    if (out_size) *out_size = g_winui_pri_size;
    return g_winui_pri_data;
}

static BOOL pri_get_section( const BYTE *pri, DWORD pri_sz, int sec_idx, const BYTE **out_sec, DWORD *out_sz )
{
    DWORD toc_off, sec_base, off, sz;
    WORD num_sec;
    if (!pri || pri_sz < 0x20) return FALSE;
    toc_off = *(const DWORD *)(pri + 0x10);
    sec_base = *(const DWORD *)(pri + 0x14);
    num_sec = *(const WORD *)(pri + 0x18);
    if (sec_idx < 0 || sec_idx >= num_sec) return FALSE;
    if (toc_off + (DWORD)(sec_idx + 1) * 32 > pri_sz) return FALSE;
    off = *(const DWORD *)(pri + toc_off + sec_idx * 32 + 24);
    sz = *(const DWORD *)(pri + toc_off + sec_idx * 32 + 28);
    if (sec_base + off + sz > pri_sz) return FALSE;
    *out_sec = pri + sec_base + off;
    *out_sz = sz;
    return TRUE;
}

static BOOL pri_get_data_item( const BYTE *pri, DWORD pri_sz, int sec_idx, int data_idx, const BYTE **out_ptr, DWORD *out_sz )
{
    const BYTE *di = NULL;
    DWORD di_sz = 0, payload_off, item_off, item_sz;
    WORD n_small, n_large;

    if (!pri_get_section( pri, pri_sz, sec_idx, &di, &di_sz ) || di_sz < 0x2c)
        return FALSE;
    if (memcmp( di, "[mrm_dataitem]", 14 ) != 0)
        return FALSE;

    n_small = *(const WORD *)(di + 0x24);
    n_large = *(const WORD *)(di + 0x26);
    payload_off = 0x2c + (DWORD)n_small * 4 + (DWORD)n_large * 8;
    if (payload_off > di_sz) return FALSE;

    if (data_idx < n_small)
    {
        item_off = *(const WORD *)(di + 0x2c + data_idx * 4);
        item_sz = *(const WORD *)(di + 0x2c + data_idx * 4 + 2);
    }
    else if (data_idx - n_small < n_large)
    {
        int lidx = data_idx - n_small;
        item_off = *(const DWORD *)(di + 0x2c + n_small * 4 + lidx * 8);
        item_sz = *(const DWORD *)(di + 0x2c + n_small * 4 + lidx * 8 + 4);
    }
    else return FALSE;

    if (payload_off + item_off + item_sz > di_sz) return FALSE;
    *out_ptr = di + payload_off + item_off;
    *out_sz = item_sz;
    return TRUE;
}

/* Reconstruct full path of node_id in [mrm_hschemaex] into ASCII buffer */
static void pri_build_node_path( const BYTE *hs, DWORD hs_sz, DWORD node_off, DWORD num_nodes,
                                 DWORD pool_off, WORD nid, char *buf, size_t buf_sz )
{
    WORD stack[24];
    int depth = 0, i;
    size_t pos = 0;
    WORD cur = nid;

    while (cur < num_nodes && depth < 24)
    {
        WORD parent = *(const WORD *)(hs + node_off + (DWORD)cur * 12);
        stack[depth++] = cur;
        if (cur == 0 || parent == cur) break;
        cur = parent;
    }

    buf[0] = 0;
    for (i = depth - 1; i >= 0; i--)
    {
        WORD n = stack[i];
        WORD name_off = *(const WORD *)(hs + node_off + (DWORD)n * 12 + 8);
        if (pool_off + name_off < hs_sz)
        {
            const char *s = (const char *)(hs + pool_off + name_off);
            size_t max_rem = hs_sz - (pool_off + name_off);
            size_t slen = strnlen( s, max_rem );
            if (slen > 0)
            {
                if (pos > 0 && pos + 1 < buf_sz) buf[pos++] = '/';
                if (pos + slen < buf_sz)
                {
                    memcpy( buf + pos, s, slen );
                    pos += slen;
                }
            }
        }
    }
    buf[pos] = 0;
}

static BOOL pri_resolve_item_data( const BYTE *pri, DWORD pri_sz, const BYTE *rm, DWORD rm_sz,
                                   DWORD count1, DWORD extra_sz, DWORD map_off, DWORD cand_off,
                                   DWORD item_id, const BYTE **out_ptr, DWORD *out_sz )
{
    DWORD cand_base = 0;
    WORD data_idx, sec_idx;
    if (item_id < count1)
        cand_base = *(const WORD *)(rm + map_off + item_id * 4 + 2);
    else if (extra_sz >= 28)
    {
        DWORD r_off = map_off + count1 * 4 + 28 + (item_id - count1) * 8;
        if (r_off + 8 > cand_off) return FALSE;
        cand_base = *(const DWORD *)(rm + r_off + 4);
    }
    else return FALSE;
    if (cand_off + (cand_base + 1) * 8 > rm_sz) return FALSE;
    data_idx = *(const WORD *)(rm + cand_off + cand_base * 8 + 4);
    sec_idx = *(const WORD *)(rm + cand_off + cand_base * 8 + 6);
    return pri_get_data_item( pri, pri_sz, sec_idx, data_idx, out_ptr, out_sz );
}

static BOOL pri_find_item_by_path( const char *want_path, const char *want_suffix,
                                   const BYTE **out_ptr, DWORD *out_sz )
{
    DWORD pri_sz = 0, hs_sz = 0, rm_sz = 0;
    const BYTE *pri = pri_get_data( &pri_sz );
    const BYTE *hs = NULL, *rm = NULL;
    DWORD num_nodes, num_scopes, num_items;
    DWORD node_off, scope_off, item_off, pool_off, map_off, cand_off;
    DWORD count1, extra_sz;
    WORD l1, l2, num_env;
    DWORD hs_base;
    int matched_item = -1, fallback_item = -1;
    DWORD iid;

    if (!pri) return FALSE;
    if (!pri_get_section( pri, pri_sz, 2, &hs, &hs_sz ) || hs_sz < 0xd8) return FALSE;
    if (!pri_get_section( pri, pri_sz, 3, &rm, &rm_sz ) || rm_sz < 0x80) return FALSE;

    l1 = *(const WORD *)(hs + 0x22);
    l2 = *(const WORD *)(hs + 0x24);
    hs_base = ((0x48 + ((DWORD)l1 + (DWORD)l2) * 2 + 3) & ~3u) + 8;
    if (hs_base + 0x18 > hs_sz) return FALSE;

    num_nodes = *(const DWORD *)(hs + hs_base);
    num_scopes = *(const DWORD *)(hs + hs_base + 4);
    num_items = *(const DWORD *)(hs + hs_base + 8);
    node_off = hs_base + 0x18;
    scope_off = node_off + num_nodes * 12;
    item_off = scope_off + num_scopes * 8;
    pool_off = item_off + num_items * 2 + *(const DWORD *)(hs + hs_base + 12) * 2;
    if (pool_off >= hs_sz) return FALSE;

    num_env = *(const WORD *)(rm + 0x2a);
    count1 = *(const DWORD *)(rm + 0x30);
    extra_sz = *(const DWORD *)(rm + 0x3c);
    map_off = 0x40 + (DWORD)num_env * 8 + 8;
    cand_off = map_off + count1 * 4 + extra_sz;
    if (cand_off >= rm_sz) return FALSE;

    for (iid = 0; iid < num_items; iid++)
    {
        WORD nid = *(const WORD *)(hs + item_off + iid * 2);
        char path[256];
        pri_build_node_path( hs, hs_sz, node_off, num_nodes, pool_off, nid, path, sizeof(path) );
        if (want_path && !strcasecmp( path, want_path ))
        {
            matched_item = (int)iid;
            break;
        }
        if (want_suffix && fallback_item < 0)
        {
            size_t plen = strlen( path ), slen = strlen( want_suffix );
            if (plen >= slen && !strcasecmp( path + plen - slen, want_suffix ) &&
                (plen == slen || path[plen - slen - 1] == '/'))
            {
                fallback_item = (int)iid;
            }
        }
    }

    if (matched_item < 0) matched_item = fallback_item;
    if (matched_item < 0) return FALSE;

    return pri_resolve_item_data( pri, pri_sz, rm, rm_sz, count1, extra_sz, map_off, cand_off,
                                  (DWORD)matched_item, out_ptr, out_sz );
}

static void uwp_pri_enum_xbfs_in_blob( const BYTE *pri, DWORD pri_sz,
                                       BOOL (*cb)(const char *path, const BYTE *xbf, DWORD sz, void *ctx), void *ctx )
{
    DWORD hs_sz = 0, rm_sz = 0;
    const BYTE *hs = NULL, *rm = NULL;
    DWORD num_nodes, num_scopes, num_items;
    DWORD node_off, scope_off, item_off, pool_off, map_off, cand_off;
    DWORD count1, extra_sz;
    WORD l1, l2, num_env;
    DWORD hs_base, iid;

    if (!pri || !cb) return;
    if (!pri_get_section( pri, pri_sz, 2, &hs, &hs_sz ) || hs_sz < 0xd8) return;
    if (!pri_get_section( pri, pri_sz, 3, &rm, &rm_sz ) || rm_sz < 0x80) return;

    l1 = *(const WORD *)(hs + 0x22);
    l2 = *(const WORD *)(hs + 0x24);
    hs_base = ((0x48 + ((DWORD)l1 + (DWORD)l2) * 2 + 3) & ~3u) + 8;
    if (hs_base + 0x18 > hs_sz) return;

    num_nodes = *(const DWORD *)(hs + hs_base);
    num_scopes = *(const DWORD *)(hs + hs_base + 4);
    num_items = *(const DWORD *)(hs + hs_base + 8);
    node_off = hs_base + 0x18;
    scope_off = node_off + num_nodes * 12;
    item_off = scope_off + num_scopes * 8;
    pool_off = item_off + num_items * 2 + *(const DWORD *)(hs + hs_base + 12) * 2;
    if (pool_off >= hs_sz) return;

    num_env = *(const WORD *)(rm + 0x2a);
    count1 = *(const DWORD *)(rm + 0x30);
    extra_sz = *(const DWORD *)(rm + 0x3c);
    map_off = 0x40 + (DWORD)num_env * 8 + 8;
    cand_off = map_off + count1 * 4 + extra_sz;
    if (cand_off >= rm_sz) return;

    for (iid = 0; iid < num_items; iid++)
    {
        WORD nid = *(const WORD *)(hs + item_off + iid * 2);
        char path[256];
        size_t plen;
        const BYTE *ptr = NULL;
        DWORD sz = 0;

        pri_build_node_path( hs, hs_sz, node_off, num_nodes, pool_off, nid, path, sizeof(path) );
        plen = strlen( path );
        if (plen < 4 || strcasecmp( path + plen - 4, ".xbf" )) continue;
        if (pri_resolve_item_data( pri, pri_sz, rm, rm_sz, count1, extra_sz, map_off, cand_off,
                                   iid, &ptr, &sz ) && ptr && sz > 0x50 && !memcmp( ptr, "XBF", 4 ))
        {
            if (!cb( path, ptr, sz, ctx )) break;
        }
    }
}

struct pri_xbf_entry
{
    char path[256];
    const char *base;
    const BYTE *data;
    DWORD size;
};

struct pri_xbf_catalog
{
    struct pri_xbf_entry *entries;
    DWORD cnt;
    DWORD cap;
};

static struct pri_xbf_catalog g_pri_cat = {0};
static INIT_ONCE g_pri_xbf_cat_once = INIT_ONCE_STATIC_INIT;

static struct pri_xbf_catalog g_winui_pri_cat = {0};
static BOOL g_winui_pri_cat_done = FALSE;

static BOOL pri_xbf_collect_cb( const char *path, const BYTE *xbf, DWORD sz, void *ctx )
{
    struct pri_xbf_catalog *cat = ctx;
    const char *slash;
    if (cat->cnt >= cat->cap)
    {
        DWORD ncap = cat->cap ? cat->cap * 2 : 64;
        struct pri_xbf_entry *ne = realloc( cat->entries, ncap * sizeof(*ne) );
        if (!ne) return FALSE;
        cat->entries = ne;
        cat->cap = ncap;
    }
    snprintf( cat->entries[cat->cnt].path, sizeof(cat->entries[0].path), "%s", path );
    slash = strrchr( cat->entries[cat->cnt].path, '/' );
    cat->entries[cat->cnt].base = slash ? slash + 1 : cat->entries[cat->cnt].path;
    cat->entries[cat->cnt].data = xbf;
    cat->entries[cat->cnt].size = sz;
    cat->cnt++;
    return TRUE;
}

static void scan_disk_xbfs_in_dir( const WCHAR *dir_path, const char *rel_prefix, struct pri_xbf_catalog *cat )
{
    WCHAR pattern[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hfind;

    if (wcslen( dir_path ) + 7 >= ARRAY_SIZE(pattern)) return;
    lstrcpyW( pattern, dir_path );
    lstrcatW( pattern, L"\\*.xbf" );
    hfind = FindFirstFileW( pattern, &fd );
    if (hfind == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcslen( dir_path ) + 1 + wcslen( fd.cFileName ) < ARRAY_SIZE(full))
        {
            HANDLE hfile, hmap;
            DWORD sz;
            lstrcpyW( full, dir_path );
            lstrcatW( full, L"\\" );
            lstrcatW( full, fd.cFileName );
            hfile = CreateFileW( full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
            if (hfile != INVALID_HANDLE_VALUE)
            {
                sz = GetFileSize( hfile, NULL );
                if (sz > 0x50 && sz < 16 * 1024 * 1024 &&
                    (hmap = CreateFileMappingW( hfile, NULL, PAGE_READONLY, 0, 0, NULL )))
                {
                    const BYTE *mapped = MapViewOfFile( hmap, FILE_MAP_READ, 0, 0, 0 );
                    if (mapped && !memcmp( mapped, "XBF", 4 ))
                    {
                        char name_a[128] = {0}, vpath[256];
                        WideCharToMultiByte( CP_UTF8, 0, fd.cFileName, -1, name_a, sizeof(name_a) - 1, NULL, NULL );
                        if (rel_prefix && rel_prefix[0])
                            snprintf( vpath, sizeof(vpath), "Files/%s/%s", rel_prefix, name_a );
                        else
                            snprintf( vpath, sizeof(vpath), "Files/%s", name_a );
                        pri_xbf_collect_cb( vpath, mapped, sz, cat );
                    }
                    else if (mapped) UnmapViewOfFile( mapped );
                    CloseHandle( hmap );
                }
                CloseHandle( hfile );
            }
        }
    } while (FindNextFileW( hfind, &fd ));
    FindClose( hfind );
}

static BOOL WINAPI init_pri_xbf_cat_once( INIT_ONCE *once, void *param, void **ctx )
{
    DWORD pri_sz = 0;
    const BYTE *pri = pri_get_data( &pri_sz );
    WCHAR exe_dir[MAX_PATH], pattern[MAX_PATH];
    WCHAR *slash;

    uwp_pri_enum_xbfs_in_blob( pri, pri_sz, pri_xbf_collect_cb, &g_pri_cat );

    if (GetModuleFileNameW( NULL, exe_dir, ARRAY_SIZE(exe_dir) ) && (slash = wcsrchr( exe_dir, '\\' )))
    {
        WIN32_FIND_DATAW fd;
        HANDLE hfind;
        *slash = 0;
        scan_disk_xbfs_in_dir( exe_dir, "", &g_pri_cat );
        if (wcslen( exe_dir ) + 3 < ARRAY_SIZE(pattern))
        {
            lstrcpyW( pattern, exe_dir );
            lstrcatW( pattern, L"\\*" );
            hfind = FindFirstFileW( pattern, &fd );
            if (hfind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        wcscmp( fd.cFileName, L"." ) && wcscmp( fd.cFileName, L".." ) &&
                        wcsicmp( fd.cFileName, L"dependencies" ))
                    {
                        WCHAR sub_dir[MAX_PATH];
                        char sub_a[128] = {0};
                        if (wcslen( exe_dir ) + 1 + wcslen( fd.cFileName ) < ARRAY_SIZE(sub_dir))
                        {
                            lstrcpyW( sub_dir, exe_dir );
                            lstrcatW( sub_dir, L"\\" );
                            lstrcatW( sub_dir, fd.cFileName );
                            WideCharToMultiByte( CP_UTF8, 0, fd.cFileName, -1, sub_a, sizeof(sub_a) - 1, NULL, NULL );
                            scan_disk_xbfs_in_dir( sub_dir, sub_a, &g_pri_cat );
                        }
                    }
                } while (FindNextFileW( hfind, &fd ));
                FindClose( hfind );
            }
        }
    }
    TRACE( "PRI XBF catalog indexed %lu .xbf entries\n", (unsigned long)g_pri_cat.cnt );
    return TRUE;
}

static void ensure_winui_pri_cat( void )
{
    if (!g_winui_pri_cat_done)
    {
        DWORD sz = 0;
        const BYTE *pri = winui_pri_get_data( &sz );
        if (pri)
        {
            g_winui_pri_cat_done = TRUE;
            uwp_pri_enum_xbfs_in_blob( pri, sz, pri_xbf_collect_cb, &g_winui_pri_cat );
            TRACE( "WinUI PRI XBF catalog indexed %lu .xbf entries\n", (unsigned long)g_winui_pri_cat.cnt );
        }
    }
}

static BOOL pri_cat_find_xbf( struct pri_xbf_catalog *cat, const char *base_xbf_dir,
                              const WCHAR *uri_or_name, const BYTE **out_ptr, DWORD *out_sz,
                              char *out_resolved, size_t out_resolved_sz )
{
    char full_path[256] = "Files/";
    char rel_path[256] = {0};
    char suffix[256] = {0};
    const WCHAR *p = uri_or_name;
    size_t i, len;
    DWORD k;
    int fallback = -1;
    BOOL is_appx = FALSE;

    if (!cat || !uri_or_name || !out_ptr || !out_sz) return FALSE;
    *out_ptr = NULL;
    *out_sz = 0;
    if (out_resolved && out_resolved_sz) out_resolved[0] = 0;

    if (!wcsncmp( p, L"ms-appx:///", 11 )) { p += 11; is_appx = TRUE; }
    else if (!wcsncmp( p, L"ms-appx://", 10 )) { p += 10; is_appx = TRUE; }
    while (*p == '/' || *p == '\\') { p++; is_appx = TRUE; }

    len = wcslen( p );
    for (i = 0; i < len && i + 7 < sizeof(full_path) - 5; i++)
        full_path[6 + i] = (p[i] == '\\') ? '/' : (char)p[i];
    full_path[6 + i] = 0;

    /* Replace .xaml with .xbf, or append .xbf */
    len = strlen( full_path );
    if (len >= 5 && !strcasecmp( full_path + len - 5, ".xaml" ))
        strcpy( full_path + len - 5, ".xbf" );
    else if (len < 4 || strcasecmp( full_path + len - 4, ".xbf" ))
        strcat( full_path, ".xbf" );

    if (!is_appx && base_xbf_dir && base_xbf_dir[0])
    {
        snprintf( rel_path, sizeof(rel_path), "%s%s", base_xbf_dir, full_path + 6 );
        for (k = 0; k < cat->cnt; k++)
        {
            if (!strcasecmp( cat->entries[k].path, rel_path ))
            {
                *out_ptr = cat->entries[k].data;
                *out_sz = cat->entries[k].size;
                if (out_resolved && out_resolved_sz)
                    snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[k].path );
                TRACE( "Resolved PRI XBF rel '%s' -> %p (%lu bytes)\n",
                       cat->entries[k].path, *out_ptr, (unsigned long)*out_sz );
                return TRUE;
            }
        }
    }

    {
        const char *slash = strrchr( full_path, '/' );
        snprintf( suffix, sizeof(suffix), "%s", slash ? slash + 1 : full_path );
    }

    for (k = 0; k < cat->cnt; k++)
    {
        if (!strcasecmp( cat->entries[k].path, full_path ))
        {
            *out_ptr = cat->entries[k].data;
            *out_sz = cat->entries[k].size;
            if (out_resolved && out_resolved_sz)
                snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[k].path );
            TRACE( "Resolved PRI XBF '%s' (suffix '%s') -> %p (%lu bytes)\n",
                   full_path, suffix, *out_ptr, (unsigned long)*out_sz );
            return TRUE;
        }
        if (fallback < 0 && !strcasecmp( cat->entries[k].base, suffix ))
            fallback = (int)k;
    }
    if (fallback >= 0)
    {
        *out_ptr = cat->entries[fallback].data;
        *out_sz = cat->entries[fallback].size;
        if (out_resolved && out_resolved_sz)
            snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[fallback].path );
        TRACE( "Resolved PRI XBF '%s' (suffix '%s') -> %p (%lu bytes)\n",
               cat->entries[fallback].path, suffix, *out_ptr, (unsigned long)*out_sz );
        return TRUE;
    }
    return FALSE;
}

static BOOL uwp_pri_find_xbf_rel( const char *base_xbf_dir, const WCHAR *uri_or_name,
                                  const BYTE **out_ptr, DWORD *out_sz,
                                  char *out_resolved, size_t out_resolved_sz )
{
    InitOnceExecuteOnce( &g_pri_xbf_cat_once, init_pri_xbf_cat_once, NULL, NULL );
    if (pri_cat_find_xbf( &g_pri_cat, base_xbf_dir, uri_or_name, out_ptr, out_sz, out_resolved, out_resolved_sz ))
        return TRUE;
    ensure_winui_pri_cat();
    if (g_winui_pri_cat.cnt > 0 &&
        pri_cat_find_xbf( &g_winui_pri_cat, base_xbf_dir, uri_or_name, out_ptr, out_sz, out_resolved, out_resolved_sz ))
        return TRUE;
    return FALSE;
}

BOOL uwp_pri_find_xbf( const WCHAR *uri_or_name, const BYTE **out_ptr, DWORD *out_sz )
{
    return uwp_pri_find_xbf_rel( NULL, uri_or_name, out_ptr, out_sz, NULL, 0 );
}

BOOL uwp_pri_lookup_string( const WCHAR *res_name, WCHAR *out_buf, int out_chars )
{
    char full_path[256] = "Resources/";
    char suffix[256] = {0};
    const BYTE *ptr = NULL;
    DWORD sz = 0;
    size_t i, len;

    if (!res_name || !out_buf || out_chars <= 0) return FALSE;
    out_buf[0] = 0;

    len = wcslen( res_name );
    for (i = 0; i < len && i + 11 < sizeof(full_path) - 1; i++)
        full_path[10 + i] = (res_name[i] == '\\') ? '/' : (char)res_name[i];
    full_path[10 + i] = 0;
    snprintf( suffix, sizeof(suffix), "%s", full_path + 10 );

    if (!pri_find_item_by_path( full_path, suffix, &ptr, &sz ) || !ptr || !sz)
        return FALSE;

    if (sz >= 2 && (sz % 2) == 0 && ptr[1] == 0 && ptr[sz - 1] == 0)
    {
        int wlen = (int)(sz / 2);
        if (wlen >= out_chars) wlen = out_chars - 1;
        memcpy( out_buf, ptr, wlen * sizeof(WCHAR) );
        out_buf[wlen] = 0;
    }
    else
    {
        int n = MultiByteToWideChar( CP_UTF8, 0, (const char *)ptr, (int)sz, out_buf, out_chars - 1 );
        if (n < 0) n = 0;
        out_buf[n] = 0;
    }
    TRACE( "PRI string '%s' -> %s\n", full_path, debugstr_w(out_buf) );
    return out_buf[0] != 0;
}

/* ============================================================================
 * 2. PropertyValue Boxing & Unboxing Helpers
 * ============================================================================ */

static IInspectable *box_double_value( double d )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                IPropertyValueStatics_CreateDouble( pvs, d, &out );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

struct uwp_boxed_thickness
{
    IInspectable IInspectable_iface;
    LONG ref;
    double val[4];
};

static const GUID IID_IReference_Thickness_UWP =
    { 0xa19f7ba8, 0xd8cd, 0x5df2, { 0xab, 0x44, 0xfe, 0xfd, 0x26, 0x64, 0x44, 0x84 } };

static HRESULT WINAPI uwp_bth_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertyValue ) ||
        IsEqualGUID( iid, &IID_IReference_Thickness_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI uwp_bth_AddRef( IInspectable *iface )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_bth_Release( IInspectable *iface )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI uwp_bth_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_bth_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IReference`1<Windows.UI.Xaml.Thickness>", 58, name );
}

static HRESULT WINAPI uwp_bth_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_bth_get_Value( void *iface, double *out )
{
    struct uwp_boxed_thickness *impl = CONTAINING_RECORD( iface, struct uwp_boxed_thickness, IInspectable_iface );
    if (!out) return E_POINTER;
    memcpy( out, impl->val, sizeof(impl->val) );
    return S_OK;
}

static const void *uwp_bth_vtbl[16] =
{
    uwp_bth_QueryInterface,
    uwp_bth_AddRef,
    uwp_bth_Release,
    uwp_bth_GetIids,
    uwp_bth_GetRuntimeClassName,
    uwp_bth_GetTrustLevel,
    uwp_bth_get_Value,
};

static IInspectable *box_thickness_value( const double vec4[4] )
{
    struct uwp_boxed_thickness *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return NULL;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bth_vtbl;
    impl->ref = 1;
    if (vec4) memcpy( impl->val, vec4, sizeof(impl->val) );
    return &impl->IInspectable_iface;
}

static IInspectable *box_string_value( const WCHAR *str )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL, hval = NULL;

    if (!str) str = L"";
    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                WindowsCreateString( str, wcslen(str), &hval );
                IPropertyValueStatics_CreateString( pvs, hval, &out );
                if (hval) WindowsDeleteString( hval );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

static IInspectable *box_int32_value( INT32 v )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                IPropertyValueStatics_CreateInt32( pvs, v, &out );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

static IInspectable *box_bool_value( boolean b )
{
    IActivationFactory *fac = NULL;
    IPropertyValueStatics *pvs = NULL;
    IInspectable *out = NULL;
    HSTRING hcls = NULL;

    if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.PropertyValue", 32, &hcls )))
    {
        if (SUCCEEDED(RoGetActivationFactory( hcls, &IID_IActivationFactory, (void **)&fac )) && fac)
        {
            if (SUCCEEDED(IActivationFactory_QueryInterface( fac, &IID_IPropertyValueStatics, (void **)&pvs )) && pvs)
            {
                IPropertyValueStatics_CreateBoolean( pvs, b, &out );
                IPropertyValueStatics_Release( pvs );
            }
            IActivationFactory_Release( fac );
        }
        WindowsDeleteString( hcls );
    }
    return out;
}

BOOL uwp_unbox_string( void *obj, WCHAR *buf, int buf_chars )
{
    IPropertyValue *pv = NULL;
    HSTRING hs = NULL;
    if (!obj || !buf || buf_chars <= 0) return FALSE;
    buf[0] = 0;

    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)obj, &IID_IPropertyValue, (void **)&pv )) && pv)
    {
        PropertyType pt = PropertyType_Empty;
        if (SUCCEEDED(IPropertyValue_get_Type( pv, &pt )) && pt == PropertyType_String &&
            SUCCEEDED(IPropertyValue_GetString( pv, &hs )) && hs)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( hs, NULL );
            if (raw) lstrcpynW( buf, raw, buf_chars );
            WindowsDeleteString( hs );
        }
        IPropertyValue_Release( pv );
        if (buf[0]) return TRUE;
    }
    return FALSE;
}

static BOOL uwp_unbox_int32( void *obj, INT32 *out_val )
{
    IPropertyValue *pv = NULL;
    if (!obj || !out_val) return FALSE;
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)obj, &IID_IPropertyValue, (void **)&pv )) && pv)
    {
        HRESULT hr = IPropertyValue_GetInt32( pv, out_val );
        IPropertyValue_Release( pv );
        return SUCCEEDED(hr);
    }
    return FALSE;
}

/* ============================================================================
 * 3. ResourceDictionary Key-Value Store (Scoped Chain & XBF Resource Parsing)
 * ============================================================================ */

struct uwp_element;
struct uwp_rd_bucket;

static struct uwp_element *uwp_elem_from_any( void *ptr );
static void uwp_set_brush_element_argb( IInspectable *br, UINT32 argb );
static void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                                   struct uwp_rd_bucket *target_rd );

enum uwp_rd_kind
{
    UWP_RD_OBJ = 0,
    UWP_RD_BRUSH,
    UWP_RD_COLOR,
    UWP_RD_DOUBLE,
    UWP_RD_THICKNESS,
    UWP_RD_STRING,
    UWP_RD_ALIAS,
    UWP_RD_STYLE,
};

struct uwp_style_setter
{
    WORD pid;
    int vt;
    BOOL is_res_ref;
    double num;
    double vec4[4];
    WCHAR str[128];
};

struct uwp_rd_entry
{
    WCHAR key[96];
    enum uwp_rd_kind kind;
    IInspectable *value;
    UINT32 argb;
    BOOL has_argb;
    double opacity;
    double dval;
    double vec4[4];
    WCHAR str_val[128];
    struct uwp_style_setter *setters;
    int setter_cnt;
    int setter_cap;
};

struct uwp_rd_bucket
{
    void *rd_key;
    struct uwp_element *owner_el;
    struct uwp_rd_entry *entries;
    int count;
    int cap;
    struct uwp_rd_bucket **merged;
    int merged_cnt;
    int merged_cap;
    struct uwp_rd_bucket *theme_dark;
    struct uwp_rd_bucket *theme_default;
    struct uwp_rd_bucket *theme_light;
    struct uwp_rd_bucket *theme_hc;
    BOOL app_seeded;
    struct uwp_rd_bucket *next;
};

static struct uwp_rd_bucket *g_rd_buckets_head = NULL;
static int g_app_rd_sentinel = 0;
static int g_theme_rd_sentinel = 0;
static BOOL g_app_res_loaded = FALSE;
static BOOL g_app_res_loading = FALSE;
static char g_loaded_res_xbfs[128][256];
static int g_loaded_res_xbf_cnt = 0;

static COLORREF argb_to_colorref( UINT32 argb )
{
    return RGB( (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff );
}

static UINT32 apply_opacity_to_argb( UINT32 argb, double opacity )
{
    BYTE a;
    if (opacity >= 0.999) return argb;
    if (opacity <= 0.0) return argb & 0x00ffffffu;
    a = (BYTE)((double)((argb >> 24) & 0xff) * opacity + 0.5);
    return (argb & 0x00ffffffu) | ((UINT32)a << 24);
}

static COLORREF blend_argb_over( UINT32 fg_argb, COLORREF bg_cr )
{
    BYTE a = (BYTE)((fg_argb >> 24) & 0xff);
    BYTE r = (BYTE)((fg_argb >> 16) & 0xff);
    BYTE g = (BYTE)((fg_argb >> 8) & 0xff);
    BYTE b = (BYTE)(fg_argb & 0xff);
    BYTE br, bg, bb;
    if (a == 255) return RGB( r, g, b );
    if (a == 0) return bg_cr;
    br = GetRValue( bg_cr );
    bg = GetGValue( bg_cr );
    bb = GetBValue( bg_cr );
    return RGB( (BYTE)((r * a + br * (255 - a)) / 255),
                (BYTE)((g * a + bg * (255 - a)) / 255),
                (BYTE)((b * a + bb * (255 - a)) / 255) );
}

static int hex_nib( WCHAR c )
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
    return -1;
}

static BOOL uwp_parse_color_string( const WCHAR *s, UINT32 *out_argb )
{
    size_t len;
    if (!s || !s[0] || !out_argb) return FALSE;
    while (*s == L' ' || *s == L'\t') s++;
    if (s[0] == L'#')
    {
        UINT32 v = 0;
        size_t i;
        s++;
        len = wcslen( s );
        while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t')) len--;
        if (len != 3 && len != 4 && len != 6 && len != 8) return FALSE;
        for (i = 0; i < len; i++)
        {
            int n = hex_nib( s[i] );
            if (n < 0) return FALSE;
            v = (v << 4) | (UINT32)n;
        }
        if (len == 3)
        {
            UINT32 r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            *out_argb = 0xff000000u | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
        }
        else if (len == 4)
        {
            UINT32 a = (v >> 12) & 0xf, r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            *out_argb = (a * 0x11 << 24) | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
        }
        else if (len == 6)
            *out_argb = 0xff000000u | v;
        else
            *out_argb = v;
        return TRUE;
    }
    if (!wcsicmp( s, L"Transparent" )) { *out_argb = 0x00ffffffu; return TRUE; }
    if (!wcsicmp( s, L"Black" ))       { *out_argb = 0xff000000u; return TRUE; }
    if (!wcsicmp( s, L"White" ))       { *out_argb = 0xffffffffu; return TRUE; }
    if (!wcsicmp( s, L"Red" ))         { *out_argb = 0xffff0000u; return TRUE; }
    if (!wcsicmp( s, L"Green" ))       { *out_argb = 0xff008000u; return TRUE; }
    if (!wcsicmp( s, L"Blue" ))        { *out_argb = 0xff0000ffu; return TRUE; }
    if (!wcsicmp( s, L"Gray" ))        { *out_argb = 0xff808080u; return TRUE; }
    if (!wcsicmp( s, L"DarkGray" ))    { *out_argb = 0xffa9a9a9u; return TRUE; }
    if (!wcsicmp( s, L"LightGray" ))   { *out_argb = 0xffd3d3d3u; return TRUE; }
    if (!wcsicmp( s, L"Yellow" ))      { *out_argb = 0xffffff00u; return TRUE; }
    if (!wcsicmp( s, L"Orange" ))      { *out_argb = 0xffffa500u; return TRUE; }
    return FALSE;
}

static BOOL uwp_extract_resource_key( const WCHAR *in, WCHAR *out, size_t out_chars )
{
    const WCHAR *p = in;
    size_t len;
    BOOL is_markup = FALSE;
    if (!in || !out || !out_chars) return FALSE;
    out[0] = 0;
    while (*p == L' ' || *p == L'\t') p++;
    if (*p == L'{')
    {
        p++;
        while (*p == L' ' || *p == L'\t') p++;
        if (!wcsnicmp( p, L"StaticResource", 14 ) && (p[14] == L' ' || p[14] == L'\t'))
        {
            p += 14;
            is_markup = TRUE;
        }
        else if (!wcsnicmp( p, L"ThemeResource", 13 ) && (p[13] == L' ' || p[13] == L'\t'))
        {
            p += 13;
            is_markup = TRUE;
        }
        while (*p == L' ' || *p == L'\t') p++;
        if (!wcsnicmp( p, L"ResourceKey=", 12 )) p += 12;
        lstrcpynW( out, p, out_chars );
        len = wcslen( out );
        while (len > 0 && (out[len - 1] == L'}' || out[len - 1] == L' ' || out[len - 1] == L'\t'))
            out[--len] = 0;
        return is_markup;
    }
    lstrcpynW( out, p, out_chars );
    len = wcslen( out );
    while (len > 0 && (out[len - 1] == L' ' || out[len - 1] == L'\t'))
        out[--len] = 0;
    return FALSE;
}

static struct uwp_rd_bucket *get_rd_bucket( void *rd_key, BOOL create )
{
    struct uwp_rd_bucket *b;
    for (b = g_rd_buckets_head; b; b = b->next)
        if (b->rd_key == rd_key) return b;
    if (!create || !rd_key) return NULL;
    b = calloc( 1, sizeof(*b) );
    if (!b) return NULL;
    b->rd_key = rd_key;
    b->next = g_rd_buckets_head;
    g_rd_buckets_head = b;
    return b;
}

static struct uwp_rd_entry *rd_bucket_upsert( struct uwp_rd_bucket *b, const WCHAR *key )
{
    int i;
    if (!b || !key || !key[0]) return NULL;
    for (i = 0; i < b->count; i++)
    {
        if (!wcscmp( b->entries[i].key, key ))
            return &b->entries[i];
    }
    if (b->count >= b->cap)
    {
        int ncap = b->cap ? b->cap * 2 : 32;
        struct uwp_rd_entry *ne = realloc( b->entries, ncap * sizeof(*ne) );
        if (!ne) return NULL;
        b->entries = ne;
        b->cap = ncap;
    }
    memset( &b->entries[b->count], 0, sizeof(b->entries[0]) );
    lstrcpynW( b->entries[b->count].key, key, 96 );
    b->entries[b->count].opacity = 1.0;
    return &b->entries[b->count++];
}

static void uwp_rd_link_merged( struct uwp_rd_bucket *parent, struct uwp_rd_bucket *child )
{
    int i;
    if (!parent || !child || parent == child) return;
    for (i = 0; i < parent->merged_cnt; i++)
        if (parent->merged[i] == child) return;
    if (parent->merged_cnt >= parent->merged_cap)
    {
        int ncap = parent->merged_cap ? parent->merged_cap * 2 : 8;
        struct uwp_rd_bucket **nm = realloc( parent->merged, ncap * sizeof(*nm) );
        if (!nm) return;
        parent->merged = nm;
        parent->merged_cap = ncap;
    }
    parent->merged[parent->merged_cnt++] = child;
}

static void uwp_rd_link_theme( struct uwp_rd_bucket *parent, const WCHAR *theme_name, struct uwp_rd_bucket *child )
{
    if (!parent || !theme_name || !child || parent == child) return;
    if (!wcsicmp( theme_name, L"Dark" )) parent->theme_dark = child;
    else if (!wcsicmp( theme_name, L"Default" )) parent->theme_default = child;
    else if (!wcsicmp( theme_name, L"Light" )) parent->theme_light = child;
    else if (!wcsicmp( theme_name, L"HighContrast" )) parent->theme_hc = child;
}

void uwp_rd_insert( void *rd_key, const WCHAR *key, IInspectable *val )
{
    struct uwp_rd_bucket *b;
    struct uwp_rd_entry *e;
    if (!rd_key || !key || !key[0] || !val) return;
    b = get_rd_bucket( rd_key, TRUE );
    e = rd_bucket_upsert( b, key );
    if (!e) return;
    if (e->value) IInspectable_Release( e->value );
    e->value = val;
    IInspectable_AddRef( val );
    e->kind = UWP_RD_OBJ;
}

static BOOL uwp_mark_res_xbf_loaded( const char *resolved_path )
{
    int i;
    if (!resolved_path || !resolved_path[0]) return FALSE;
    for (i = 0; i < g_loaded_res_xbf_cnt; i++)
    {
        if (!strcasecmp( g_loaded_res_xbfs[i], resolved_path ))
            return FALSE;
    }
    if (g_loaded_res_xbf_cnt < (int)ARRAY_SIZE(g_loaded_res_xbfs))
    {
        snprintf( g_loaded_res_xbfs[g_loaded_res_xbf_cnt++], 256, "%s", resolved_path );
    }
    return TRUE;
}

static void uwp_load_res_xbf_into_bucket( const char *base_dir, const WCHAR *uri_or_path, struct uwp_rd_bucket *target )
{
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;
    char resolved[256] = {0};

    if (!uri_or_path || !uri_or_path[0] || !target) return;
    if (!uwp_pri_find_xbf_rel( base_dir, uri_or_path, &xbf, &xbf_sz, resolved, sizeof(resolved) ))
        return;
    if (!uwp_mark_res_xbf_loaded( resolved[0] ? resolved : "xbf" ))
        return;
    TRACE( "xbf-res: loading %s (%lu bytes) into rd %p\n", resolved, (unsigned long)xbf_sz, target );
    uwp_parse_xbf_into_rd( xbf, xbf_sz, resolved, target );
}

/* Registry-driven per-app Resources_xbf + App.xbf + WinUI theme defaults (no hardcoded app families) */
static void uwp_ensure_app_resources_loaded( void )
{
    struct uwp_rd_bucket *app_b, *theme_b;
    WCHAR pkg_full[256] = {0};
    UINT32 len = ARRAY_SIZE(pkg_full);
    HMODULE reg_mod;

    if (g_app_res_loaded || g_app_res_loading) return;
    g_app_res_loading = TRUE;

    app_b = get_rd_bucket( &g_app_rd_sentinel, TRUE );
    theme_b = get_rd_bucket( &g_theme_rd_sentinel, TRUE );

    /* 1. Check Software\Classes\AppModel\Repository\Packages\<FullName>\Resources_xbf */
    if (GetCurrentPackageFullName( &len, pkg_full ) == ERROR_SUCCESS && pkg_full[0])
    {
        reg_mod = GetModuleHandleW( L"kernelbase.dll" );
        if (!reg_mod) reg_mod = LoadLibraryW( L"advapi32.dll" );
        if (reg_mod)
        {
            typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
            typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
            typedef LONG (WINAPI *p_RegDeleteValueW_t)( HKEY, LPCWSTR );
            typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );
            p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
            p_RegQueryValueExW_t p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
            p_RegDeleteValueW_t p_del = (p_RegDeleteValueW_t)GetProcAddress( reg_mod, "RegDeleteValueW" );
            p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
            if (p_open && p_del && p_close)
            {
                WCHAR fam[128] = {0};
                UINT32 flen = ARRAY_SIZE(fam);
                if (GetCurrentPackageFamilyName( &flen, fam ) == ERROR_SUCCESS && fam[0])
                {
                    WCHAR ssub[256];
                    HKEY shk = NULL;
                    swprintf( ssub, ARRAY_SIZE(ssub), L"Software\\%s\\Settings", fam );
                    if (p_open( HKEY_CURRENT_USER, ssub, 0, KEY_SET_VALUE, &shk ) == ERROR_SUCCESS)
                    {
                        p_del( shk, L"VeryFirstLaunch" );
                        p_close( shk );
                    }
                }
            }
            if (p_open && p_query && p_close)
            {
                static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
                WCHAR sub[512];
                size_t r;
                swprintf( sub, ARRAY_SIZE(sub), L"Software\\Classes\\AppModel\\Repository\\Packages\\%s", pkg_full );
                for (r = 0; r < ARRAY_SIZE(roots); r++)
                {
                    HKEY hk = NULL;
                    WCHAR valbuf[1024] = {0};
                    DWORD type = 0, cb = sizeof(valbuf) - sizeof(WCHAR);
                    if (p_open( roots[r], sub, 0, KEY_READ, &hk ) != ERROR_SUCCESS) continue;
                    if (p_query( hk, L"Resources_xbf", NULL, &type, (BYTE *)valbuf, &cb ) == ERROR_SUCCESS)
                    {
                        if (type == REG_MULTI_SZ)
                        {
                            const WCHAR *it = valbuf;
                            while (*it)
                            {
                                TRACE( "xbf-res: registry Resources_xbf[%s] -> %s\n", debugstr_w(pkg_full), debugstr_w(it) );
                                uwp_load_res_xbf_into_bucket( NULL, it, app_b );
                                it += wcslen( it ) + 1;
                            }
                        }
                        else if ((type == REG_SZ || type == REG_EXPAND_SZ) && valbuf[0])
                        {
                            WCHAR *tok = valbuf;
                            while (*tok)
                            {
                                WCHAR *sep = wcspbrk( tok, L";," );
                                if (sep) *sep = 0;
                                while (*tok == L' ') tok++;
                                if (*tok)
                                {
                                    TRACE( "xbf-res: registry Resources_xbf[%s] -> %s\n", debugstr_w(pkg_full), debugstr_w(tok) );
                                    uwp_load_res_xbf_into_bucket( NULL, tok, app_b );
                                }
                                if (!sep) break;
                                tok = sep + 1;
                            }
                        }
                    }
                    p_close( hk );
                }
            }
        }
    }

    /* 2. Parse App.xbf (and any MergedDictionaries / ThemeDictionaries it references) */
    if (app_b)
        uwp_load_res_xbf_into_bucket( NULL, L"App.xbf", app_b );

    /* 3. Load WinUI / system theme XBF into theme defaults bucket */
    if (theme_b)
    {
        uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/21h1_themeresources.xbf", theme_b );
        uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/21h1_generic.xbf", theme_b );
        if (theme_b->count == 0 && !theme_b->theme_default && !theme_b->theme_dark)
            uwp_load_res_xbf_into_bucket( NULL, L"Microsoft.UI.Xaml/Themes/rs5_themeresources.xbf", theme_b );
    }

    g_app_res_loaded = TRUE;
    g_app_res_loading = FALSE;
}

static BOOL uwp_lookup_in_bucket( struct uwp_rd_bucket *b, const WCHAR *key, int depth,
                                  struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket )
{
    int i;
    if (!b || !key || !key[0] || depth > 12) return FALSE;
    TRACE( "uwp_lookup_in_bucket: b=%p count=%d merged_cnt=%d key=%s\n",
           b, b->count, b->merged_cnt, debugstr_w(key) );

    for (i = 0; i < b->count; i++)
    {
        if (!wcscmp( b->entries[i].key, key ))
        {
            *out_entry = &b->entries[i];
            if (out_bucket) *out_bucket = b;
            return TRUE;
        }
    }
    if (b->theme_dark && uwp_lookup_in_bucket( b->theme_dark, key, depth + 1, out_entry, out_bucket ))
        return TRUE;
    if (b->theme_default && uwp_lookup_in_bucket( b->theme_default, key, depth + 1, out_entry, out_bucket ))
        return TRUE;
    for (i = b->merged_cnt - 1; i >= 0; i--)
    {
        if (uwp_lookup_in_bucket( b->merged[i], key, depth + 1, out_entry, out_bucket ))
            return TRUE;
    }
    if (b->theme_light && uwp_lookup_in_bucket( b->theme_light, key, depth + 1, out_entry, out_bucket ))
        return TRUE;
    return FALSE;
}

static UINT32 uwp_get_system_accent_argb( void )
{
    static UINT32 cached_accent = 0;
    static BOOL accent_init = FALSE;
    if (!accent_init)
    {
        HMODULE reg_mod = GetModuleHandleW( L"kernelbase.dll" );
        accent_init = TRUE;
        cached_accent = 0xff0078d4u; /* Windows Fluent default blue */
        if (!reg_mod) reg_mod = LoadLibraryW( L"advapi32.dll" );
        if (reg_mod)
        {
            typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
            typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
            typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );
            p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
            p_RegQueryValueExW_t p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
            p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
            if (p_open && p_query && p_close)
            {
                HKEY hk = NULL;
                DWORD abgr = 0, type = 0, cb = sizeof(abgr);
                if (p_open( HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &hk ) == ERROR_SUCCESS)
                {
                    if (p_query( hk, L"AccentColor", NULL, &type, (BYTE *)&abgr, &cb ) == ERROR_SUCCESS &&
                        type == REG_DWORD && (abgr & 0x00ffffffu))
                    {
                        BYTE r = (BYTE)(abgr & 0xff);
                        BYTE g = (BYTE)((abgr >> 8) & 0xff);
                        BYTE b = (BYTE)((abgr >> 16) & 0xff);
                        cached_accent = 0xff000000u | ((UINT32)r << 16) | ((UINT32)g << 8) | b;
                    }
                    p_close( hk );
                }
            }
        }
    }
    return cached_accent;
}

/* Documented system/Fluent dark theme fallback ONLY with TRACE when not found in element/page/app/WinUI dicts */
static BOOL uwp_theme_default_fallback( const WCHAR *key, UINT32 *out_argb )
{
    static const struct { const WCHAR *name; UINT32 argb; } defaults[] =
    {
        { L"ApplicationPageBackgroundThemeBrush",       0xff1f1f1fu },
        { L"SystemControlPageBackgroundChromeLowBrush", 0xff1f1f1fu },
        { L"SolidBackgroundFillColorBaseBrush",         0xff202020u },
        { L"SystemAltHighColor",                        0xff000000u },
        { L"SystemAltMediumHighColor",                  0xcc000000u },
        { L"SystemAltMediumColor",                      0x99000000u },
        { L"SystemAltMediumLowColor",                   0x66000000u },
        { L"SystemAltLowColor",                         0x33000000u },
        { L"SystemBaseHighColor",                       0xffffffffu },
        { L"SystemBaseMediumHighColor",                 0xccffffffu },
        { L"SystemBaseMediumColor",                     0x99ffffffu },
        { L"SystemBaseMediumLowColor",                  0x66ffffffu },
        { L"SystemBaseLowColor",                        0x33ffffffu },
        { L"SystemChromeWhiteColor",                    0xffffffffu },
        { L"SystemChromeBlackLowColor",                 0xff171717u },
        { L"SystemChromeMediumLowColor",                0xff2b2b2bu },
        { L"SystemListLowColor",                        0x19ffffffu },
        { L"SystemColorWindowColor",                    0xff202020u },
        { L"SystemColorWindowTextColor",                0xffffffffu },
        { L"SystemColorButtonFaceColor",                0xff323232u },
        { L"DefaultTextForegroundThemeBrush",           0xffffffffu },
        { L"TextFillColorPrimaryBrush",                 0xffffffffu },
        { L"TextFillColorSecondaryBrush",               0xffc5c5c5u },
        { L"TextFillColorDisabledBrush",                0x5dffffffu },
        { L"TextOnAccentFillColorPrimaryBrush",         0xffffffffu },
        { L"ControlFillColorDefaultBrush",              0x18ffffffu },
        { L"ControlFillColorSecondaryBrush",            0x22ffffffu },
        { L"ControlStrokeColorDefaultBrush",            0x18ffffffu },
        { L"CardBackgroundFillColorDefaultBrush",       0x0dffffffu },
        { L"SystemAccentColorLight1",                   0xff2b88d8u },
        { L"SystemAccentColorLight2",                   0xff4cc2ffu },
        { L"SystemAccentColorLight3",                   0xff99ebffu },
        { L"SystemAccentColorDark1",                    0xff005a9eu },
        { L"SystemAccentColorDark2",                    0xff004578u },
        { L"SystemAccentColorDark3",                    0xff002642u },
        { L"AccentTextThemeResource",                   0xff4cc2ffu },
        { L"SystemControlForegroundAccentBrush",        0xff4cc2ffu },
        { L"AccentTextFillColorPrimaryBrush",           0xff4cc2ffu },
    };
    size_t i;
    if (!key || !key[0] || !out_argb) return FALSE;
    if (!wcscmp( key, L"SystemAccentColor" ) ||
        !wcscmp( key, L"AccentFillColorDefaultBrush" ) ||
        !wcscmp( key, L"SystemControlHighlightAccentBrush" ) ||
        !wcscmp( key, L"AccentBrush" ))
    {
        *out_argb = uwp_get_system_accent_argb();
        TRACE( "xbf-res: theme default fallback key=%s -> #%08x\n", debugstr_w(key), *out_argb );
        return TRUE;
    }
    for (i = 0; i < ARRAY_SIZE(defaults); i++)
    {
        if (!wcscmp( key, defaults[i].name ))
        {
            *out_argb = defaults[i].argb;
            TRACE( "xbf-res: theme default fallback key=%s -> #%08x\n", debugstr_w(key), *out_argb );
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL uwp_rd_find_entry( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                               const WCHAR *raw_key, int depth,
                               struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket );

static BOOL uwp_resolve_brush_or_color( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                                        const WCHAR *key_or_expr, int depth, UINT32 *out_argb )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *e = NULL;
    struct uwp_rd_bucket *found_b = NULL;

    if (!key_or_expr || !key_or_expr[0] || !out_argb || depth > 12) return FALSE;
    if (uwp_parse_color_string( key_or_expr, out_argb )) return TRUE;

    uwp_extract_resource_key( key_or_expr, key, ARRAY_SIZE(key) );
    if (!key[0]) return FALSE;
    if (uwp_parse_color_string( key, out_argb )) return TRUE;

    uwp_ensure_app_resources_loaded();
    if (uwp_rd_find_entry( start_bucket, scope_el, key, depth, &e, &found_b ) && e)
    {
        if (e->kind == UWP_RD_COLOR)
        {
            *out_argb = e->argb;
            return TRUE;
        }
        if (e->kind == UWP_RD_BRUSH)
        {
            if (e->has_argb)
            {
                *out_argb = apply_opacity_to_argb( e->argb, e->opacity );
                return TRUE;
            }
            if (e->str_val[0])
            {
                UINT32 inner = 0;
                if (uwp_resolve_brush_or_color( found_b ? found_b : start_bucket, scope_el, e->str_val, depth + 1, &inner ))
                {
                    *out_argb = apply_opacity_to_argb( inner, e->opacity );
                    return TRUE;
                }
            }
        }
        if (e->kind == UWP_RD_STRING && e->str_val[0])
            return uwp_resolve_brush_or_color( found_b ? found_b : start_bucket, scope_el, e->str_val, depth + 1, out_argb );
    }

    return uwp_theme_default_fallback( key, out_argb );
}

static IInspectable *uwp_rd_materialize_entry( struct uwp_rd_bucket *b, struct uwp_rd_entry *e )
{
    if (!e) return NULL;
    if (e->value) return e->value;
    if (e->kind == UWP_RD_DOUBLE)
        e->value = box_double_value( e->dval );
    else if (e->kind == UWP_RD_THICKNESS)
        e->value = box_thickness_value( e->vec4 );
    else if (e->kind == UWP_RD_STRING)
        e->value = box_string_value( e->str_val );
    else if (e->kind == UWP_RD_BRUSH)
    {
        IInspectable *br = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Media.SolidColorBrush", (void **)&br )) && br)
        {
            UINT32 argb = 0;
            if (uwp_resolve_brush_or_color( b, b ? b->owner_el : NULL, e->key, 0, &argb ))
            {
                e->argb = argb;
                e->has_argb = TRUE;
                uwp_set_brush_element_argb( br, argb );
            }
            e->value = br;
        }
    }
    else if (e->kind == UWP_RD_STYLE)
    {
        IInspectable *st = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Style", (void **)&st )) && st)
            e->value = st;
    }
    return e->value;
}

HRESULT uwp_rd_lookup( void *rd_key, void *key_obj, void **out_val )
{
    WCHAR raw_str[128] = {0}, key_str[128] = {0};
    struct uwp_rd_bucket *b, *found_b = NULL;
    struct uwp_rd_entry *e = NULL;

    if (!out_val) return E_POINTER;
    *out_val = NULL;
    if (!key_obj) return 0x8000000b; /* E_BOUNDS */

    if (!uwp_unbox_string( key_obj, raw_str, ARRAY_SIZE(raw_str) ))
    {
        const WCHAR *raw = WindowsGetStringRawBuffer( (HSTRING)key_obj, NULL );
        if (raw) lstrcpynW( raw_str, raw, ARRAY_SIZE(raw_str) );
    }
    uwp_extract_resource_key( raw_str, key_str, ARRAY_SIZE(key_str) );

    TRACE( "uwp_rd_lookup rd=%p key=%s\n", rd_key, debugstr_w(key_str) );
    if (!key_str[0]) return 0x8000000b;

    uwp_ensure_app_resources_loaded();
    b = get_rd_bucket( rd_key, FALSE );
    TRACE( "uwp_rd_lookup: b=%p owner=%p\n", b, b ? b->owner_el : NULL );
    if (uwp_rd_find_entry( b, b ? b->owner_el : NULL, key_str, 0, &e, &found_b ) && e)
    {
        IInspectable *val = uwp_rd_materialize_entry( found_b ? found_b : b, e );
        TRACE( "uwp_rd_lookup: e=%p found_b=%p val=%p\n", e, found_b, val );
        if (val)
        {
            *out_val = val;
            IInspectable_AddRef( val );
            return S_OK;
        }
    }

    /* Documented fallback for system theme brush keys queried via Application.Resources */
    {
        UINT32 argb = 0;
        if (uwp_theme_default_fallback( key_str, &argb ))
        {
            IInspectable *br = NULL;
            if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Media.SolidColorBrush", (void **)&br )) && br)
            {
                struct uwp_rd_bucket *target = b ? b : get_rd_bucket( rd_key, TRUE );
                struct uwp_rd_entry *ne = rd_bucket_upsert( target, key_str );
                if (ne)
                {
                    ne->kind = UWP_RD_BRUSH;
                    ne->argb = argb;
                    ne->has_argb = TRUE;
                    ne->value = br;
                    IInspectable_AddRef( br );
                }
                uwp_set_brush_element_argb( br, argb );
                *out_val = br;
                return S_OK;
            }
        }
    }

    /* Synthesize MenuFlyout / Style / Brush if requested by name from Resources */
    if (wcsstr( key_str, L"ContextMenu" ) || wcsstr( key_str, L"Flyout" ))
    {
        IInspectable *flyout = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Controls.MenuFlyout", (void **)&flyout )) && flyout)
        {
            uwp_rd_insert( rd_key, key_str, flyout );
            *out_val = flyout;
            return S_OK;
        }
    }
    if (wcsstr( key_str, L"Style" ))
    {
        IInspectable *st = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Style", (void **)&st )) && st)
        {
            uwp_rd_insert( rd_key, key_str, st );
            *out_val = st;
            return S_OK;
        }
    }
    if (wcsstr( key_str, L"Brush" ))
    {
        IInspectable *br = NULL;
        if (SUCCEEDED(element_create_typed( L"Windows.UI.Xaml.Media.SolidColorBrush", (void **)&br )) && br)
        {
            uwp_rd_insert( rd_key, key_str, br );
            *out_val = br;
            return S_OK;
        }
    }

    return 0x8000000b; /* E_BOUNDS */
}

BOOL uwp_rd_haskey( void *rd_key, void *key_obj )
{
    void *val = NULL;
    if (SUCCEEDED(uwp_rd_lookup( rd_key, key_obj, &val )) && val)
    {
        IInspectable_Release( (IInspectable *)val );
        return TRUE;
    }
    return FALSE;
}

/* ============================================================================
 * 4. UWP Visual Element & Distinct Per-Interface Sub-Objects
 * ============================================================================ */

enum uwp_elem_kind
{
    UWP_KIND_GENERIC = 0,
    UWP_KIND_FRAME,
    UWP_KIND_PAGE,
    UWP_KIND_USER_CONTROL,
    UWP_KIND_GRID,
    UWP_KIND_STACK_PANEL,
    UWP_KIND_BORDER,
    UWP_KIND_CONTENT_CONTROL,
    UWP_KIND_BUTTON,
    UWP_KIND_TOGGLE_BUTTON,
    UWP_KIND_TEXT_BLOCK,
    UWP_KIND_ROW_DEF,
    UWP_KIND_COL_DEF,
    UWP_KIND_SCROLL_VIEWER,
    UWP_KIND_STYLE,
    UWP_KIND_BRUSH,
    UWP_KIND_FLYOUT,
    UWP_KIND_POPUP,
    UWP_KIND_DEFERRED,
    UWP_KIND_IMAGE,
    UWP_KIND_PROGRESS_RING,
    UWP_KIND_RESOURCE_DICT,
    UWP_KIND_COLOR,
    UWP_KIND_ACCELERATOR,
    UWP_KIND_XAML_COMMAND,
};

struct uwp_dp_entry
{
    void *dp;
    IInspectable *value;
};

struct uwp_dp_cb_entry
{
    void *dp;
    IInspectable *callback;
    INT64 token;
};

struct uwp_defer_entry
{
    WCHAR name[64];
    int sub_idx;
    struct uwp_element *parent_panel;
    int child_idx;
    const BYTE *xbf_data;
    DWORD xbf_size;
    IInspectable *root_component;
    BOOL realized;
};

enum uwp_vsm_prop_kind
{
    UWP_VSM_PROP_NONE = 0,
    UWP_VSM_PROP_GRID_LEN,
    UWP_VSM_PROP_WIDTH,
    UWP_VSM_PROP_HEIGHT,
    UWP_VSM_PROP_MIN_WIDTH,
    UWP_VSM_PROP_MAX_WIDTH,
    UWP_VSM_PROP_MIN_HEIGHT,
    UWP_VSM_PROP_MAX_HEIGHT,
    UWP_VSM_PROP_VISIBILITY,
    UWP_VSM_PROP_IS_ENABLED,
    UWP_VSM_PROP_GRID_ROW,
    UWP_VSM_PROP_GRID_ROW_SPAN,
    UWP_VSM_PROP_GRID_COL,
    UWP_VSM_PROP_GRID_COL_SPAN,
    UWP_VSM_PROP_FONT_SIZE,
};

struct uwp_vsm_setter
{
    WCHAR target[64];
    WCHAR property[64];
    WCHAR val_str[128];
    int vt;
    double num;
    BOOL is_res_ref;
};

struct uwp_vsm_snapshot_entry
{
    struct uwp_element *target;
    enum uwp_vsm_prop_kind prop_kind;
    int old_int;
    double old_num;
};

struct uwp_vsm_state
{
    WCHAR name[64];
    BOOL has_trigger;
    double min_window_width;
    double min_window_height;
    struct uwp_vsm_setter *setters;
    int setter_cnt;
    int setter_cap;
};

struct uwp_vsm_group
{
    WCHAR name[64];
    WCHAR current_state[64];
    int active_state_idx;
    struct uwp_vsm_state *states;
    int state_cnt;
    int state_cap;
    struct uwp_vsm_snapshot_entry *snapshots;
    int snapshot_cnt;
    int snapshot_cap;
};

struct uwp_element
{
    IInspectable nondelegating_iface;
    IInspectable IInspectable_iface;
    IInspectable IDependencyObject_iface;
    IInspectable IDependencyObject2_iface;
    IInspectable IUIElement_iface;
    IInspectable IFrameworkElement_iface;
    IInspectable IFrameworkElement3_iface;
    IInspectable IControl_iface;
    IInspectable IControlProtected_iface;
    IInspectable IContentControl_iface;
    IInspectable IUserControl_iface;
    IInspectable IPage_iface;
    IInspectable IFrame_iface;
    IInspectable IButtonBase_iface;
    IInspectable IToggleButton_iface;
    IInspectable IPanel_iface;
    IInspectable IGrid_iface;
    IInspectable IBorder_iface;
    IInspectable ITextBlock_iface;
    IInspectable IRowDefinition_iface;
    IInspectable IColumnDefinition_iface;
    IInspectable IStyle_iface;
    IInspectable IBrush_iface;
    IInspectable ISolidColorBrush_iface;
    IInspectable IFlyout_iface;
    IInspectable IStateTrigger_iface;
    IInspectable IObservableVector_iface;
    IInspectable IVector_iface;
    IInspectable IIterable_iface;
    IInspectable IVectorView_iface;
    IInspectable IScrollViewer_iface;
    IInspectable IDesktopWindowXamlSourceNative_iface;
    IInspectable IUIElement7_iface;
    IInspectable IKeyboardAccelerator_iface;
    IInspectable IXamlUICommand_iface;
    IInspectable ICommand_iface;
    IInspectable IMenuFlyoutItem_iface;
    IInspectable IMenuFlyoutItem3_iface;
    IInspectable IToggleMenuFlyoutItem_iface;
    IInspectable IMenuFlyoutSubItem_iface;
    IInspectable IMenuFlyout2_iface;
    IInspectable IUIElement4_iface;
    IInspectable IContentDialog_iface;
    IInspectable IContentDialog2_iface;
    IInspectable ITeachingTip_iface;
    IWeakReferenceSource IWeakReferenceSource_iface;

    LONG ref;
    HWND island_hwnd;
    HWND dialog_hwnd;
    IInspectable *outer;
    enum uwp_elem_kind kind;
    WCHAR class_name[128];
    WCHAR x_class[128];
    WCHAR name[64];
    WCHAR uid[64];
    WCHAR text[256];
    WCHAR accel_text[64];
    IInspectable *dialog_title_obj;
    WCHAR dialog_title[128];
    WCHAR dialog_primary_text[64];
    WCHAR dialog_secondary_text[64];
    WCHAR dialog_close_text[64];
    int dialog_default_btn;
    int conn_id;

    struct uwp_element *parent;
    struct uwp_element *owner_component;
    IInspectable *content;       /* ContentControl/UserControl/Page/Border/ScrollViewer child */
    IInspectable *context_flyout; /* UIElement.ContextFlyout */
    IInspectable *children_coll; /* Panel.Children */
    IInspectable *accelerators_coll; /* UIElement.KeyboardAccelerators */
    IInspectable *row_defs_coll; /* Grid.RowDefinitions */
    IInspectable *col_defs_coll; /* Grid.ColumnDefinitions */
    IInspectable *resources_rd;  /* FrameworkElement.Resources */
    IInspectable *data_context;
    IInspectable *command;
    IInspectable *command_param;
    IInspectable *tag;
    IDCompositionVisual *visual;

    /* Layout properties */
    int grid_row;
    int grid_col;
    int grid_row_span;
    int grid_col_span;
    int grid_len_type; /* 0=Auto, 1=Pixel, 2=Star */
    double grid_len_val;
    int orientation;   /* 0=Vertical, 1=Horizontal */
    int visibility;    /* 0=Visible, 1=Collapsed */
    boolean is_enabled;
    int is_checked;    /* 0=Unchecked, 1=Checked, 2=Indeterminate */
    BOOL is_pressed;
    BOOL is_hovered;
    BOOL is_hit_test_visible;
    double opacity;
    double width, height;
    double min_width, max_width;
    double min_height, max_height;
    double actual_w, actual_h;
    double margin_l, margin_t, margin_r, margin_b;
    double padding_l, padding_t, padding_r, padding_b;
    int h_align;       /* 0=Left, 1=Center, 2=Right, 3=Stretch */
    int v_align;       /* 0=Top, 1=Center, 2=Bottom, 3=Stretch */
    double font_size;
    COLORREF bg_color;
    UINT32 bg_argb;
    BYTE bg_alpha;
    BOOL has_bg;
    COLORREF fg_color;
    UINT32 fg_argb;
    BYTE fg_alpha;
    BOOL has_fg;
    COLORREF border_color;
    UINT32 border_argb;
    BYTE border_alpha;
    BOOL has_border;
    double border_thick_l, border_thick_t, border_thick_r, border_thick_b;
    double corner_radius;

    /* SolidColorBrush / Color fields */
    UINT32 brush_argb;
    BOOL has_brush_argb;
    double brush_opacity;
    WCHAR brush_color_ref[96];

    /* Style / Setter fields */
    WCHAR style_based_on[96];
    struct uwp_style_setter *style_setters;
    int style_setter_cnt;
    int style_setter_cap;
    struct uwp_rd_bucket *rd_bucket;

    /* Computed screen rectangle */
    RECT bounds;
    BOOL has_bounds;

    /* Events & DependencyProperty store */
    IInspectable **loaded_handlers;
    int loaded_cnt;
    int loaded_cap;
    BOOL loaded_fired;
    BOOL template_applied;

    IInspectable **click_handlers;
    int click_cnt;
    int click_cap;

    IInspectable **dc_handlers;
    int dc_cnt;
    int dc_cap;
    void *binding_connector;
    IInspectable *selected_item;
    IInspectable *sel_changed_handler;

    struct xaml_event_registry ptr_pressed_reg;
    struct xaml_event_registry ptr_moved_reg;
    struct xaml_event_registry ptr_released_reg;
    struct xaml_event_registry ptr_entered_reg;
    struct xaml_event_registry ptr_exited_reg;
    struct xaml_event_registry ptr_capture_lost_reg;
    struct xaml_event_registry ptr_canceled_reg;
    struct xaml_event_registry ptr_wheel_reg;

    struct uwp_dp_entry *dp_store;
    int dp_cnt;
    int dp_cap;

    struct uwp_dp_cb_entry *dp_cbs;
    int dp_cb_cnt;
    int dp_cb_cap;
    INT64 dp_cb_next_token;

    /* Template children cache (for CalculationResult etc.) */
    struct uwp_element *tpl_text_container;
    struct uwp_element *tpl_normal_output;
    struct uwp_element *tpl_scroll_left;
    struct uwp_element *tpl_scroll_right;

    /* Named children & deferred substreams for FindName */
    struct uwp_element **named_elems;
    int named_cnt;
    int named_cap;
    struct uwp_defer_entry *defer_entries;
    int defer_cnt;
    int defer_cap;
    struct uwp_xbf_ref
    {
        WCHAR full_name[128];
        WCHAR short_name[128];
        BOOL is_resource_path;
        BOOL instantiated;
    } *xbf_refs;
    int xbf_ref_cnt;
    int xbf_ref_cap;
    BOOL xbf_parsed;
    struct uwp_vsm_group *vsm_groups;
    int vsm_group_cnt;
    int vsm_group_cap;
    double vsm_eval_width;
    double vsm_eval_height;
    BOOL vsm_eval_valid;
    WCHAR image_src[256];
    BYTE *img_pixels;
    UINT img_w, img_h;
    BOOL img_loaded;
    struct uwp_element *next_reg;
    int adopted_faulted_ctor;
    IInspectable *provider_inst;
    void *outer_unk;

    /* KeyboardAccelerator, XamlUICommand, PreviewKeyDown, TeachingTip state */
    struct xaml_event_registry preview_keydown_reg;
    UINT accel_key;
    UINT accel_mods;
    BOOL accel_enabled;
    IInspectable *accel_scope_owner;
    struct uwp_element *accel_target;
    IInspectable **accel_invoked_handlers;
    int accel_invoked_cnt;
    int accel_invoked_cap;
    IInspectable **exec_req_handlers;
    int exec_req_cnt;
    int exec_req_cap;
    IInspectable **tip_closing_handlers;
    int tip_closing_cnt;
    int tip_closing_cap;
    IInspectable **tip_closed_handlers;
    int tip_closed_cnt;
    int tip_closed_cap;
    IInspectable **flyout_opening_handlers;
    int flyout_opening_cnt;
    int flyout_opening_cap;
    IInspectable **flyout_closed_handlers;
    int flyout_closed_cnt;
    int flyout_closed_cap;
    BOOL is_open;
};

/* Global registry of live uwp_elements so we can resolve from any interface or outer.
 * Linked list (newest first): static-array registry overflowed at 2048 entries with
 * App.xbf style/setter/visualstate churn, silently dropping the Page root. */
static struct uwp_element *g_all_uwp_elems_head = NULL;
static int g_all_uwp_elem_cnt = 0;
static SRWLOCK g_all_uwp_elems_lock = SRWLOCK_INIT;
static BOOL g_layout_dirty = TRUE;
static struct uwp_element *g_captured_elem = NULL;
static struct uwp_element *g_hovered_elem = NULL;
/* Last PAGE-kind element created (set by uwp_element_create). Navigate uses it
 * as fallback when the RCW page pointer cannot be resolved to an element. */
static struct uwp_element *g_last_created_page = NULL;
/* Page element adopted by the last Frame.Navigate; rendered directly because
 * Frame.content often holds an unresolvable MCG RCW pointer. */
static struct uwp_element *g_navigated_page_el = NULL;

static void uwp_invalidate_window( void );
static struct uwp_element *uwp_find_named( struct uwp_element *el, const WCHAR *want );
static struct uwp_element *uwp_elem_from_any( void *ptr );
static BOOL uwp_elem_is_descendant( struct uwp_element *ancestor, struct uwp_element *node );
void uwp_invoke_button( struct uwp_element *btn );
BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr );
static void uwp_show_flyout_popup( struct uwp_element *flyout, HWND owner_hwnd, int screen_x, int screen_y );
static void uwp_close_flyout_popup( BOOL fire_closed );
static BOOL uwp_popup_is_open_for( struct uwp_element *flyout );
static BOOL uwp_popup_handle_escape( void );
static HWND g_popup_hwnd = NULL;
static HWND g_sub_popup_hwnd = NULL;
static struct uwp_element *g_popup_owner_el = NULL;
static struct uwp_element *g_last_closed_flyout = NULL;
static DWORD g_last_closed_tick = 0;

struct uwp_accel_entry
{
    struct uwp_element *accel_el;
    struct uwp_element *target_el;
    struct uwp_element *page_el;
    UINT vk;
    UINT mods;
    BOOL enabled;
};
static struct uwp_accel_entry *g_accel_entries = NULL;
static int g_accel_cnt = 0;
static int g_accel_cap = 0;

static struct uwp_element *uwp_find_owning_page( struct uwp_element *el )
{
    struct uwp_element *cur = el;
    int depth = 0;
    while (cur && depth < 64)
    {
        if (cur->kind == UWP_KIND_PAGE || cur->kind == UWP_KIND_USER_CONTROL)
            return cur;
        if (cur->parent)
            cur = cur->parent;
        else if (cur->owner_component && cur->owner_component != cur)
            cur = cur->owner_component;
        else
            break;
        depth++;
    }
    if (el && el->owner_component) return el->owner_component;
    return g_navigated_page_el ? g_navigated_page_el : g_last_created_page;
}

static void uwp_accel_register( struct uwp_element *accel_el, struct uwp_element *target_el,
                                UINT vk, UINT mods, BOOL enabled )
{
    struct uwp_element *page_el;
    int i;

    if (!vk) return;
    if (!target_el && accel_el) target_el = accel_el->accel_target;
    page_el = target_el ? uwp_find_owning_page( target_el ) : (accel_el ? uwp_find_owning_page( accel_el ) : g_navigated_page_el);

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (accel_el)
    {
        for (i = 0; i < g_accel_cnt; i++)
        {
            if (g_accel_entries[i].accel_el == accel_el)
            {
                if (target_el) g_accel_entries[i].target_el = target_el;
                if (page_el) g_accel_entries[i].page_el = page_el;
                g_accel_entries[i].vk = vk;
                g_accel_entries[i].mods = mods;
                g_accel_entries[i].enabled = enabled;
                TRACE( "accel_reg(update): vk=%#x mods=%#x enabled=%d target=%s (%s) page=%s (%s)\n",
                       vk, mods, enabled,
                       g_accel_entries[i].target_el ? debugstr_w(g_accel_entries[i].target_el->class_name) : "?",
                       g_accel_entries[i].target_el ? debugstr_w(g_accel_entries[i].target_el->name) : "",
                       g_accel_entries[i].page_el ? debugstr_w(g_accel_entries[i].page_el->class_name) : "?",
                       g_accel_entries[i].page_el ? debugstr_w(g_accel_entries[i].page_el->name) : "" );
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                return;
            }
        }
    }
    else if (target_el)
    {
        for (i = 0; i < g_accel_cnt; i++)
        {
            if (!g_accel_entries[i].accel_el && g_accel_entries[i].target_el == target_el &&
                g_accel_entries[i].vk == vk && g_accel_entries[i].mods == mods)
            {
                g_accel_entries[i].enabled = enabled;
                if (page_el) g_accel_entries[i].page_el = page_el;
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                return;
            }
        }
    }
    if (xaml_vec_grow( (void **)&g_accel_entries, &g_accel_cap, g_accel_cnt + 1, sizeof(*g_accel_entries), 32 ))
    {
        struct uwp_accel_entry *e = &g_accel_entries[g_accel_cnt++];
        e->accel_el = accel_el;
        e->target_el = target_el;
        e->page_el = page_el;
        e->vk = vk;
        e->mods = mods;
        e->enabled = enabled;
        TRACE( "accel_reg: vk=%#x mods=%#x enabled=%d target=%s (%s) page=%s (%s) total=%d\n",
               vk, mods, enabled,
               target_el ? debugstr_w(target_el->class_name) : "?",
               target_el ? debugstr_w(target_el->name) : "",
               page_el ? debugstr_w(page_el->class_name) : "?",
               page_el ? debugstr_w(page_el->name) : "",
               g_accel_cnt );
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
}

static UINT uwp_parse_vk_string( const WCHAR *s )
{
    if (!s || !s[0]) return 0;
    if (!s[1])
    {
        WCHAR ch = towupper( s[0] );
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return (UINT)ch;
        if (ch == '+') return VK_OEM_PLUS;
        if (ch == '-') return VK_OEM_MINUS;
        if (ch == '.') return VK_OEM_PERIOD;
        if (ch == ',') return VK_OEM_COMMA;
    }
    if (!wcsicmp( s, L"Insert" )) return VK_INSERT;
    if (!wcsicmp( s, L"Delete" )) return VK_DELETE;
    if (!wcsicmp( s, L"Escape" ) || !wcsicmp( s, L"Esc" )) return VK_ESCAPE;
    if (!wcsicmp( s, L"Back" ) || !wcsicmp( s, L"Backspace" )) return VK_BACK;
    if (!wcsicmp( s, L"Enter" ) || !wcsicmp( s, L"Return" )) return VK_RETURN;
    if (!wcsicmp( s, L"Tab" )) return VK_TAB;
    if (!wcsicmp( s, L"Space" )) return VK_SPACE;
    if (!wcsicmp( s, L"Up" )) return VK_UP;
    if (!wcsicmp( s, L"Down" )) return VK_DOWN;
    if (!wcsicmp( s, L"Left" )) return VK_LEFT;
    if (!wcsicmp( s, L"Right" )) return VK_RIGHT;
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9')
    {
        int fn = _wtoi( s + 1 );
        if (fn >= 1 && fn <= 24) return (UINT)(VK_F1 + fn - 1);
    }
    return 0;
}

static void uwp_set_brush_element_argb( IInspectable *br, UINT32 argb )
{
    struct uwp_element *el = uwp_elem_from_any( br );
    if (el)
    {
        el->brush_argb = argb;
        el->has_brush_argb = TRUE;
        el->brush_opacity = (double)((argb >> 24) & 0xff) / 255.0;
    }
}

static struct uwp_rd_bucket *uwp_rd_bucket_for_el( struct uwp_element *el, BOOL create )
{
    struct uwp_rd_bucket *b;
    if (!el) return NULL;
    if (el->rd_bucket) return el->rd_bucket;
    if (el->resources_rd)
    {
        b = get_rd_bucket( el->resources_rd, create );
        if (b) { b->owner_el = el; el->rd_bucket = b; }
        return b;
    }
    if (el->kind == UWP_KIND_RESOURCE_DICT)
    {
        b = get_rd_bucket( &el->IInspectable_iface, create );
        if (b) { b->owner_el = el; el->rd_bucket = b; }
        return b;
    }
    if (!create) return NULL;
    xaml_resource_dictionary_create_for_uwp( (void **)&el->resources_rd );
    if (!el->resources_rd) return NULL;
    b = get_rd_bucket( el->resources_rd, TRUE );
    if (b) { b->owner_el = el; el->rd_bucket = b; }
    return b;
}

static BOOL uwp_rd_find_entry( struct uwp_rd_bucket *start_bucket, struct uwp_element *scope_el,
                               const WCHAR *raw_key, int depth,
                               struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *e = NULL;
    struct uwp_rd_bucket *found_b = NULL;
    struct uwp_rd_bucket *visited[32];
    int vis_cnt = 0;
    struct uwp_element *cur;

    if (!raw_key || !raw_key[0] || !out_entry || depth > 14) return FALSE;
    *out_entry = NULL;
    if (out_bucket) *out_bucket = NULL;

    uwp_extract_resource_key( raw_key, key, ARRAY_SIZE(key) );
    if (!key[0]) return FALSE;

    if (start_bucket)
    {
        if (vis_cnt < 32) visited[vis_cnt++] = start_bucket;
        if (uwp_lookup_in_bucket( start_bucket, key, 0, &e, &found_b ))
            goto found;
    }

    /* Walk element -> parent -> ... -> page/component scope */
    cur = scope_el ? scope_el : (start_bucket ? start_bucket->owner_el : NULL);
    while (cur)
    {
        struct uwp_rd_bucket *cb = uwp_rd_bucket_for_el( cur, FALSE );
        if (cb)
        {
            int v;
            BOOL seen = FALSE;
            for (v = 0; v < vis_cnt; v++) if (visited[v] == cb) { seen = TRUE; break; }
            if (!seen)
            {
                if (vis_cnt < 32) visited[vis_cnt++] = cb;
                if (uwp_lookup_in_bucket( cb, key, 0, &e, &found_b ))
                    goto found;
            }
        }
        if (cur->parent) cur = cur->parent;
        else if (cur->owner_component && cur->owner_component != cur) cur = cur->owner_component;
        else break;
    }
    if (scope_el && scope_el->owner_component)
    {
        struct uwp_rd_bucket *ob = uwp_rd_bucket_for_el( scope_el->owner_component, FALSE );
        if (ob)
        {
            int v;
            BOOL seen = FALSE;
            for (v = 0; v < vis_cnt; v++) if (visited[v] == ob) { seen = TRUE; break; }
            if (!seen && uwp_lookup_in_bucket( ob, key, 0, &e, &found_b ))
                goto found;
        }
    }

    /* App-level Resources (App.xbf + registry Resources_xbf + merged/theme) wins over theme defaults */
    {
        struct uwp_rd_bucket *app_b = get_rd_bucket( &g_app_rd_sentinel, FALSE );
        BOOL ok = (app_b && uwp_lookup_in_bucket( app_b, key, 0, &e, &found_b ));
        TRACE( "uwp_rd_find_entry: app_b=%p ok=%d key=%s\n", app_b, ok, debugstr_w(key) );
        if (ok) goto found;
    }

    /* Theme defaults (WinUI themeresources.xbf Dark/Default) */
    {
        struct uwp_rd_bucket *theme_b = get_rd_bucket( &g_theme_rd_sentinel, FALSE );
        if (theme_b && uwp_lookup_in_bucket( theme_b, key, 0, &e, &found_b ))
            goto found;
    }

    return FALSE;

found:
    if (e && e->kind == UWP_RD_ALIAS && e->str_val[0])
    {
        if (uwp_rd_find_entry( start_bucket, scope_el, e->str_val, depth + 1, out_entry, out_bucket ))
            return TRUE;
        if (found_b && found_b != start_bucket &&
            uwp_rd_find_entry( found_b, found_b->owner_el, e->str_val, depth + 1, out_entry, out_bucket ))
            return TRUE;
    }
    *out_entry = e;
    if (out_bucket) *out_bucket = found_b;
    return TRUE;
}
static HRESULT WINAPI uwp_do_GetValue( void *iface, void *dp, IInspectable **out );
static HRESULT WINAPI uwp_do_SetValue( void *iface, void *dp, IInspectable *val );
static HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token );
static HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val );
static void uwp_set_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, const WCHAR *str_val );

/* --- Live engine: StandardCalculatorViewModel (CalcViewModel.dll) ---
 * Slot 7 = get_DisplayValue(HSTRING*), slot 79 = SendCommandToCalcManager(int).
 * Init is async (Concurrency task): m_calculatorManager lands at this+392
 * ~100ms after activation, so pump messages before the first SendCommand. */
extern HRESULT WINAPI RoActivateInstance( HSTRING activatableClassId, IInspectable **instance );

static const GUID IID_INotifyPropertyChanged_UWP =
    { 0xcf75d69c, 0xf2f4, 0x4fb6, { 0xa7, 0xc3, 0x5f, 0x20, 0x42, 0x0f, 0x5d, 0x42 } };
static const GUID IID_IPropertyChangedEventHandler_UWP =
    { 0x50f19c16, 0x0a22, 0x4d8e, { 0xa0, 0x89, 0x1e, 0xa9, 0x95, 0x16, 0x57, 0xd2 } };
static const GUID IID_IPropertyChangedEventArgs_UWP =
    { 0x4f33a9a0, 0x5cf4, 0x47a4, { 0xb1, 0x6f, 0xd7, 0xfa, 0xaf, 0x17, 0x45, 0x7e } };
static const GUID IID_IReference_NumbersAndOperatorsEnum =
    { 0x487a747c, 0x8529, 0x548a, { 0x92, 0x6f, 0x3b, 0xe3, 0x9f, 0x3e, 0xcb, 0xbf } };
static const GUID IID_IDependencyPropertyChangedCallback_UWP =
    { 0x49c85e2a, 0xf407, 0x48e8, { 0x84, 0x54, 0x3e, 0x3f, 0x98, 0x31, 0xe9, 0xb9 } };

static struct
{
    IInspectable *vm;             /* Active ViewModel RCW/instance */
    IInspectable *npc;            /* Held INotifyPropertyChanged face for remove_PropertyChanged on teardown */
    EventRegistrationToken npc_token;
    BOOL npc_hooked;
    IInspectable *cmd;            /* Optional ICommand face resolved via IXamlMember / QI */
    IDependencyProperty *display_dp;
    WCHAR vm_class_name[128];
    BOOL pumped;
    int last_code;                /* dedupe repeated clicks on init-pending VM */
} g_calc_vm;

static BOOL uwp_unbox_enum_int32( IInspectable *obj, INT32 *out_val, BOOL allow_plain_int )
{
    IPropertyValue *pv = NULL;
    PropertyType pt = PropertyType_Empty;
    void *iref = NULL;

    if (!obj || !out_val) return FALSE;
    if (allow_plain_int && uwp_unbox_int32( obj, out_val )) return TRUE;

    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IPropertyValue_g, (void **)&pv )) && pv)
    {
        HRESULT hr = IPropertyValue_get_Type( pv, &pt );
        IPropertyValue_Release( pv );
        if (FAILED(hr) || pt != 20 /* PropertyType_OtherType */)
            return FALSE;
    }
    else return FALSE;

    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IReference_NumbersAndOperatorsEnum, &iref )) && iref)
    {
        struct reference_int32 *ref = (struct reference_int32 *)iref;
        INT32 v = 0;
        HRESULT hr = ref->lpVtbl->get_Value ? ref->lpVtbl->get_Value( ref, &v ) : E_FAIL;
        IUnknown_Release( (IUnknown *)iref );
        if (SUCCEEDED(hr))
        {
            *out_val = v;
            return TRUE;
        }
    }

    /* Generic IReference<Enum> fallback via runtime class name + GetIids */
    {
        HSTRING hcls = NULL;
        const WCHAR *cls = NULL;
        ULONG cnt = 0, k;
        IID *iids = NULL;
        if (SUCCEEDED(IInspectable_GetRuntimeClassName( obj, &hcls )) && hcls)
        {
            cls = WindowsGetStringRawBuffer( hcls, NULL );
            if (cls && wcsstr( cls, L"IReference`1<" ) &&
                SUCCEEDED(IInspectable_GetIids( obj, &cnt, &iids )) && iids)
            {
                for (k = 0; k < cnt; k++)
                {
                    if (IsEqualGUID( &iids[k], &IID_IUnknown ) ||
                        IsEqualGUID( &iids[k], &IID_IInspectable ) ||
                        IsEqualGUID( &iids[k], &IID_IAgileObject ) ||
                        IsEqualGUID( &iids[k], &IID_IPropertyValue_g ))
                        continue;
                    iref = NULL;
                    if (SUCCEEDED(IInspectable_QueryInterface( obj, &iids[k], &iref )) && iref)
                    {
                        INT32 v = 0;
                        HRESULT hr = E_FAIL;
                        __TRY
                        {
                            struct reference_int32 *ref = (struct reference_int32 *)iref;
                            if (ref->lpVtbl->get_Value) hr = ref->lpVtbl->get_Value( ref, &v );
                        }
                        __EXCEPT_ALL { hr = E_FAIL; }
                        __ENDTRY;
                        IUnknown_Release( (IUnknown *)iref );
                        if (SUCCEEDED(hr))
                        {
                            *out_val = v;
                            CoTaskMemFree( iids );
                            WindowsDeleteString( hcls );
                            return TRUE;
                        }
                    }
                }
                CoTaskMemFree( iids );
            }
            WindowsDeleteString( hcls );
        }
    }
    return FALSE;
}

static BOOL uwp_get_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, IInspectable **out_val )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL;
    HSTRING h_owner = NULL, h_prop = NULL;
    BOOL ok = FALSE;

    if (!out_val) return FALSE;
    *out_val = NULL;
    if (!app_outer || !owner_type_name || !owner_type_name[0] || !prop_name || !target_obj) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) || !provider)
        return FALSE;

    if (SUCCEEDED(WindowsCreateString( owner_type_name, wcslen(owner_type_name), &h_owner )))
    {
        struct xaml_metadata_provider *mp = (struct xaml_metadata_provider *)provider;
        if (mp->lpVtbl->GetXamlTypeByFullName &&
            SUCCEEDED(mp->lpVtbl->GetXamlTypeByFullName( mp, h_owner, (struct xaml_type **)&xaml_type )) && xaml_type)
        {
            struct xaml_type *xt = (struct xaml_type *)xaml_type;
            if (xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
            {
                if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, (struct xaml_member **)&member )) && member)
                {
                    struct xaml_member *xm = (struct xaml_member *)member;
                    if (xm->lpVtbl->GetValue)
                    {
                        HRESULT hr = E_FAIL;
                        __TRY
                        {
                            hr = xm->lpVtbl->GetValue( xm, target_obj, out_val );
                        }
                        __EXCEPT_ALL { hr = E_FAIL; *out_val = NULL; }
                        __ENDTRY;
                        if (SUCCEEDED(hr) && *out_val) ok = TRUE;
                    }
                    IInspectable_Release( (IInspectable *)member );
                }
                WindowsDeleteString( h_prop );
            }
            IInspectable_Release( (IInspectable *)xaml_type );
        }
        WindowsDeleteString( h_owner );
    }
    IInspectable_Release( (IInspectable *)provider );
    return ok;
}

static int uwp_resolve_button_calc_code( struct uwp_element *btn )
{
    IInspectable *val = NULL;
    INT32 code = -1;
    int i;

    if (!btn) return -1;

    /* 1. Read ButtonId dynamically from the app's IXamlMetadataProvider */
    if (btn->class_name[0])
    {
        IInspectable *target = btn->outer ? btn->outer : &btn->IInspectable_iface;
        if (uwp_get_custom_xaml_member( uwp_get_app_outer(), btn->class_name, L"ButtonId", target, &val ) && val)
        {
            BOOL ok = uwp_unbox_enum_int32( val, &code, TRUE );
            IInspectable_Release( val );
            if (ok && code >= 0)
            {
                TRACE( "uwp_resolve_button_calc_code: provider ButtonId on %s -> %d\n",
                       debugstr_w(btn->name), (int)code );
                return (int)code;
            }
        }
    }

    /* 2. Read CommandParameter set by CalculatorButton::OnButtonIdPropertyChanged */
    if (btn->command_param && uwp_unbox_enum_int32( btn->command_param, &code, TRUE ) && code >= 0)
    {
        TRACE( "uwp_resolve_button_calc_code: CommandParameter on %s -> %d\n",
               debugstr_w(btn->name), (int)code );
        return (int)code;
    }

    /* 3. Read boxed ButtonId enum from element's DependencyProperty bag */
    for (i = 0; i < btn->dp_cnt; i++)
    {
        if (btn->dp_store[i].value &&
            uwp_unbox_enum_int32( btn->dp_store[i].value, &code, FALSE ) && code >= 0)
        {
            TRACE( "uwp_resolve_button_calc_code: dp_store[%d] on %s -> %d\n",
                   i, debugstr_w(btn->name), (int)code );
            return (int)code;
        }
    }

    return -1;
}

/* DependencyPropertyChangedCallback attached to CalculationResult controls */
static HRESULT WINAPI calc_result_dp_cb_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDependencyPropertyChangedCallback_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI calc_result_dp_cb_AddRef( void *iface ) { return 2; }
static ULONG WINAPI calc_result_dp_cb_Release( void *iface ) { return 1; }
static HRESULT WINAPI calc_result_dp_cb_Invoke( void *iface, void *sender, void *dp )
{
    struct uwp_element *res = uwp_elem_from_any( sender );
    IInspectable *val = NULL;
    WCHAR buf[256] = {0};

    if (!res || !dp) return S_OK;
    if (SUCCEEDED(uwp_do_GetValue( &res->IDependencyObject_iface, dp, &val )) && val)
    {
        if (uwp_unbox_string( val, buf, ARRAY_SIZE(buf) ) && buf[0])
        {
            lstrcpynW( res->text, buf, ARRAY_SIZE(res->text) );
            if (res->tpl_normal_output)
            {
                HSTRING htxt = NULL;
                if (SUCCEEDED(WindowsCreateString( buf, wcslen(buf), &htxt )) && htxt)
                {
                    uwp_tb_put_Text( &res->tpl_normal_output->ITextBlock_iface, htxt );
                    WindowsDeleteString( htxt );
                }
                else
                {
                    lstrcpynW( res->tpl_normal_output->text, buf, ARRAY_SIZE(res->tpl_normal_output->text) );
                    uwp_invalidate_window();
                }
            }
            else
            {
                uwp_invalidate_window();
            }
        }
        IInspectable_Release( val );
    }
    return S_OK;
}
static const void *g_calc_result_dp_cb_vtbl[] =
{
    calc_result_dp_cb_QI,
    calc_result_dp_cb_AddRef,
    calc_result_dp_cb_Release,
    calc_result_dp_cb_Invoke,
};
static struct { const void *vtbl; } g_calc_result_dp_cb = { g_calc_result_dp_cb_vtbl };

static BOOL uwp_is_display_result_element( const struct uwp_element *el )
{
    if (!el) return FALSE;
    if (!wcscmp( el->name, L"Results" ) || !wcscmp( el->name, L"NormalOutput" ))
        return TRUE;
    if (wcsstr( el->class_name, L"CalculationResult" ) != NULL)
        return TRUE;
    return FALSE;
}

static void uwp_bind_calc_result_elements( void )
{
    struct uwp_element *scan, *matches[32];
    int mcnt = 0, m;
    if (!g_calc_vm.display_dp)
        dependency_property_create( NULL, &g_calc_vm.display_dp );
    if (!g_calc_vm.display_dp) return;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (scan = g_all_uwp_elems_head; scan && mcnt < (int)ARRAY_SIZE(matches); scan = scan->next_reg)
    {
        if (uwp_is_display_result_element( scan ))
            matches[mcnt++] = scan;
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    for (m = 0; m < mcnt; m++)
    {
        BOOL already = FALSE;
        int i;
        scan = matches[m];
        for (i = 0; i < scan->dp_cb_cnt; i++)
        {
            if (scan->dp_cbs[i].dp == g_calc_vm.display_dp) { already = TRUE; break; }
        }
        if (!already)
        {
            INT64 tok = 0;
            uwp_do2_RegisterPropertyChangedCallback( &scan->IDependencyObject2_iface,
                                                     g_calc_vm.display_dp, &g_calc_result_dp_cb, &tok );
            TRACE( "uwp_bind_calc_result_elements: bound DP changed callback on %p (%s)\n",
                   scan, debugstr_w(scan->name) );
        }
    }
}

static void uwp_push_display_value_to_results( const WCHAR *s )
{
    IInspectable *boxed = NULL;
    struct uwp_element *scan, *matches[32];
    int mcnt = 0, m;

    if (!s || !s[0]) return;
    uwp_bind_calc_result_elements();
    if (!g_calc_vm.display_dp) return;

    boxed = box_string_value( s );
    if (!boxed) return;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (scan = g_all_uwp_elems_head; scan && mcnt < (int)ARRAY_SIZE(matches); scan = scan->next_reg)
    {
        if (uwp_is_display_result_element( scan ))
            matches[mcnt++] = scan;
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    for (m = 0; m < mcnt; m++)
    {
        IInspectable *target;
        scan = matches[m];
        target = scan->outer ? scan->outer : &scan->IInspectable_iface;
        if (scan->class_name[0] && uwp_get_app_outer())
            uwp_set_custom_xaml_member( uwp_get_app_outer(), scan->class_name, L"DisplayValue", target, s );
        uwp_do_SetValue( &scan->IDependencyObject_iface, g_calc_vm.display_dp, boxed );
    }
    IInspectable_Release( boxed );
}

static BOOL uwp_vm_read_display_value( IInspectable *vm, const WCHAR *vm_class_name, WCHAR *out_buf, size_t max_chars )
{
    IInspectable *val = NULL;
    HSTRING hval = NULL;
    HRESULT hr = E_FAIL;

    if (!vm || !out_buf || !max_chars) return FALSE;
    out_buf[0] = 0;

    /* 1. Try IXamlMetadataProvider member "DisplayValue" when provider is available */
    if (vm_class_name && vm_class_name[0] && uwp_get_app_outer() &&
        uwp_get_custom_xaml_member( uwp_get_app_outer(), vm_class_name, L"DisplayValue", vm, &val ) && val)
    {
        BOOL ok = uwp_unbox_string( val, out_buf, (int)max_chars );
        IInspectable_Release( val );
        if (ok && out_buf[0]) return TRUE;
    }

    /* 2. Try unboxing directly if VM is a PropertyValue string */
    if (uwp_unbox_string( vm, out_buf, (int)max_chars ) && out_buf[0])
        return TRUE;

    /* 3. Fallback to default interface slot 7 get_DisplayValue(HSTRING*) */
    __TRY
    {
        HRESULT (WINAPI *get_disp)(void *, HSTRING *) = (void *)(*(void ***)vm)[7];
        if (get_disp) hr = get_disp( vm, &hval );
    }
    __EXCEPT_ALL
    {
        hr = E_FAIL;
        hval = NULL;
    }
    __ENDTRY;

    if (SUCCEEDED(hr) && hval)
    {
        const WCHAR *s = WindowsGetStringRawBuffer( hval, NULL );
        if (s && s[0]) lstrcpynW( out_buf, s, (int)max_chars );
        WindowsDeleteString( hval );
        return out_buf[0] != 0;
    }
    return FALSE;
}

void uwp_sync_calc_display_from_vm( void )
{
    WCHAR buf[256] = {0};

    if (!g_calc_vm.vm) return;
    if (uwp_vm_read_display_value( g_calc_vm.vm, g_calc_vm.vm_class_name, buf, ARRAY_SIZE(buf) ))
    {
        TRACE( "uwp_sync_calc_display_from_vm: DisplayValue = %s\n", debugstr_w(buf) );
        uwp_push_display_value_to_results( buf );
    }
}

/* INotifyPropertyChanged listener for ViewModel DisplayValue */
static HRESULT WINAPI calc_vm_npc_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPropertyChangedEventHandler_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI calc_vm_npc_AddRef( void *iface ) { return 2; }
static ULONG WINAPI calc_vm_npc_Release( void *iface ) { return 1; }
static HRESULT WINAPI calc_vm_npc_Invoke( void *iface, void *sender, void *args )
{
    HSTRING hprop = NULL;
    const WCHAR *pname = NULL;
    BOOL match = FALSE;

    if (args)
    {
        void *pcea = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)args, &IID_IPropertyChangedEventArgs_UWP, &pcea )) && pcea)
        {
            struct property_changed_args *pca = (struct property_changed_args *)pcea;
            if (pca->lpVtbl->get_PropertyName && SUCCEEDED(pca->lpVtbl->get_PropertyName( pca, &hprop )) && hprop)
                pname = WindowsGetStringRawBuffer( hprop, NULL );
            IUnknown_Release( (IUnknown *)pcea );
        }
    }
    if (!pname || !pname[0] || !wcscmp( pname, L"DisplayValue" ))
        match = TRUE;
    if (hprop) WindowsDeleteString( hprop );

    if (match)
        uwp_sync_calc_display_from_vm();
    return S_OK;
}
static const void *g_calc_vm_npc_vtbl[] =
{
    calc_vm_npc_QI,
    calc_vm_npc_AddRef,
    calc_vm_npc_Release,
    calc_vm_npc_Invoke,
};
static struct { const void *vtbl; } g_calc_vm_npc_handler = { g_calc_vm_npc_vtbl };

static void uwp_calc_vm_teardown( BOOL final_cleanup )
{
    if (g_calc_vm.npc)
    {
        if (g_calc_vm.npc_hooked)
        {
            struct notify_property_changed *npc = (struct notify_property_changed *)g_calc_vm.npc;
            if (npc->lpVtbl->remove_PropertyChanged)
            {
                __TRY { npc->lpVtbl->remove_PropertyChanged( npc, g_calc_vm.npc_token ); }
                __EXCEPT_ALL { }
                __ENDTRY;
            }
        }
        IInspectable_Release( g_calc_vm.npc );
        g_calc_vm.npc = NULL;
        g_calc_vm.npc_hooked = FALSE;
        g_calc_vm.npc_token.value = 0;
    }
    if (g_calc_vm.cmd)
    {
        IInspectable_Release( g_calc_vm.cmd );
        g_calc_vm.cmd = NULL;
    }
    if (g_calc_vm.vm)
    {
        IInspectable_Release( g_calc_vm.vm );
        g_calc_vm.vm = NULL;
    }
    g_calc_vm.vm_class_name[0] = 0;
    g_calc_vm.pumped = FALSE;
    if (final_cleanup && g_calc_vm.display_dp)
    {
        IDependencyProperty_Release( g_calc_vm.display_dp );
        g_calc_vm.display_dp = NULL;
    }
}

/* Resolve ViewModel type name (and optionally activate via IXamlType) from the page/view XBF
 * metadata via IXamlMetadataProvider::GetXamlTypeByFullName(view_type)->GetMember("Model")->get_Type(). */
static void uwp_resolve_vm_type_from_metadata( WCHAR *out_type_name, size_t max_chars, IInspectable **out_instance )
{
    static const WCHAR * const member_names[] = { L"Model", L"ViewModel", L"DataContext" };
    void *app_outer = uwp_get_app_outer();
    void *provider = NULL;
    WCHAR candidates[8][128];
    int ncand = 0, c;

    if (out_type_name && max_chars) out_type_name[0] = 0;
    if (out_instance) *out_instance = NULL;

    /* Collect view/page candidate types from parsed XBF tree */
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (g_navigated_page_el)
    {
        int r;
        for (r = 0; r < g_navigated_page_el->xbf_ref_cnt && ncand < (int)ARRAY_SIZE(candidates); r++)
        {
            if (!g_navigated_page_el->xbf_refs[r].is_resource_path &&
                g_navigated_page_el->xbf_refs[r].full_name[0])
                lstrcpynW( candidates[ncand++], g_navigated_page_el->xbf_refs[r].full_name, 128 );
        }
        if (g_navigated_page_el->x_class[0] && ncand < (int)ARRAY_SIZE(candidates))
            lstrcpynW( candidates[ncand++], g_navigated_page_el->x_class, 128 );
    }
    {
        struct uwp_element *scan;
        for (scan = g_all_uwp_elems_head; scan && ncand < (int)ARRAY_SIZE(candidates); scan = scan->next_reg)
        {
            const WCHAR *cls = scan->x_class[0] ? scan->x_class : scan->class_name;
            if ((scan->kind == UWP_KIND_USER_CONTROL || scan->kind == UWP_KIND_PAGE) &&
                cls[0] && wcsncmp( cls, L"Windows.UI.Xaml.", 16 ) != 0 &&
                wcsncmp( cls, L"Microsoft.UI.Xaml.", 18 ) != 0)
            {
                int k;
                BOOL dup = FALSE;
                for (k = 0; k < ncand; k++) if (!wcscmp( candidates[k], cls )) { dup = TRUE; break; }
                if (!dup) lstrcpynW( candidates[ncand++], cls, 128 );
            }
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    if (app_outer && ncand > 0 &&
        SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) && provider)
    {
        struct xaml_metadata_provider *mp = (struct xaml_metadata_provider *)provider;
        for (c = 0; c < ncand && (!out_type_name || !out_type_name[0]); c++)
        {
            HSTRING h_cls = NULL;
            void *view_xt = NULL;
            if (mp->lpVtbl->GetXamlTypeByFullName && SUCCEEDED(WindowsCreateString( candidates[c], wcslen(candidates[c]), &h_cls )))
            {
                if (SUCCEEDED(mp->lpVtbl->GetXamlTypeByFullName( mp, h_cls, (struct xaml_type **)&view_xt )) && view_xt)
                {
                    struct xaml_type *vxt = (struct xaml_type *)view_xt;
                    size_t m;
                    for (m = 0; vxt->lpVtbl->GetMember && m < ARRAY_SIZE(member_names); m++)
                    {
                        HSTRING h_mem = NULL;
                        void *xm = NULL;
                        if (SUCCEEDED(WindowsCreateString( member_names[m], wcslen(member_names[m]), &h_mem )))
                        {
                            if (SUCCEEDED(vxt->lpVtbl->GetMember( vxt, h_mem, (struct xaml_member **)&xm )) && xm)
                            {
                                struct xaml_member *xm2 = (struct xaml_member *)xm;
                                void *vm_xt = NULL;
                                if (xm2->lpVtbl->get_Type && SUCCEEDED(xm2->lpVtbl->get_Type( xm2, (struct xaml_type **)&vm_xt )) && vm_xt)
                                {
                                    /* Follow nested CalculatorViewModel if Model is ApplicationViewModel */
                                    struct xaml_type *vmxt = (struct xaml_type *)vm_xt;
                                    HSTRING h_sub = NULL;
                                    void *sub_xm = NULL;
                                    if (vmxt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( L"CalculatorViewModel", 19, &h_sub )))
                                    {
                                        if (SUCCEEDED(vmxt->lpVtbl->GetMember( vmxt, h_sub, (struct xaml_member **)&sub_xm )) && sub_xm)
                                        {
                                            struct xaml_member *sxm = (struct xaml_member *)sub_xm;
                                            void *sub_xt = NULL;
                                            if (sxm->lpVtbl->get_Type && SUCCEEDED(sxm->lpVtbl->get_Type( sxm, (struct xaml_type **)&sub_xt )) && sub_xt)
                                            {
                                                IInspectable_Release( (IInspectable *)vm_xt );
                                                vm_xt = sub_xt;
                                            }
                                            IInspectable_Release( (IInspectable *)sub_xm );
                                        }
                                        WindowsDeleteString( h_sub );
                                    }
                                    {
                                        HSTRING h_fn = NULL;
                                        if (vmxt->lpVtbl->get_FullName && SUCCEEDED(vmxt->lpVtbl->get_FullName( vmxt, &h_fn )) && h_fn)
                                        {
                                            const WCHAR *fn = WindowsGetStringRawBuffer( h_fn, NULL );
                                            if (fn && fn[0] && out_type_name && max_chars)
                                            {
                                                lstrcpynW( out_type_name, fn, (int)max_chars );
                                                TRACE( "uwp_resolve_vm_type_from_metadata: %s.%s -> %s\n",
                                                       debugstr_w(candidates[c]), debugstr_w(member_names[m]),
                                                       debugstr_w(out_type_name) );
                                            }
                                            WindowsDeleteString( h_fn );
                                        }
                                    }
                                    if (out_instance && !*out_instance)
                                    {
                                        if (vmxt->lpVtbl->ActivateInstance)
                                        {
                                            __TRY { vmxt->lpVtbl->ActivateInstance( vmxt, out_instance ); }
                                            __EXCEPT_ALL { *out_instance = NULL; }
                                            __ENDTRY;
                                        }
                                    }
                                    IInspectable_Release( (IInspectable *)vm_xt );
                                }
                                IInspectable_Release( (IInspectable *)xm );
                            }
                            WindowsDeleteString( h_mem );
                        }
                        if (out_type_name && out_type_name[0]) break;
                    }
                    IInspectable_Release( (IInspectable *)view_xt );
                }
                WindowsDeleteString( h_cls );
            }
        }
        IInspectable_Release( (IInspectable *)provider );
    }

    /* Single fallback call site when IXamlMetadataProvider is unavailable */
    if (out_type_name && max_chars && !out_type_name[0] && xaml_get_app_profile()->vm_type_name)
        lstrcpynW( out_type_name, xaml_get_app_profile()->vm_type_name, (int)max_chars );
}

static IInspectable *uwp_make_button_pressed_args( int code );

static void uwp_calc_vm_set_instance( IInspectable *vm, const WCHAR *class_name )
{
    void *npc = NULL;
    IInspectable *cmd_prop = NULL;
    void *cmd_iface = NULL;

    if (g_calc_vm.vm == vm && vm != NULL)
    {
        uwp_bind_calc_result_elements();
        uwp_sync_calc_display_from_vm();
        return;
    }

    uwp_calc_vm_teardown( FALSE );
    if (!vm) return;

    g_calc_vm.vm = vm;
    IInspectable_AddRef( vm );

    if (class_name && class_name[0])
        lstrcpynW( g_calc_vm.vm_class_name, class_name, ARRAY_SIZE(g_calc_vm.vm_class_name) );
    else
    {
        HSTRING hcls = NULL;
        if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( hcls, NULL );
            if (raw) lstrcpynW( g_calc_vm.vm_class_name, raw, ARRAY_SIZE(g_calc_vm.vm_class_name) );
            WindowsDeleteString( hcls );
        }
    }

    /* Subscribe to INotifyPropertyChanged and retain token + interface for teardown */
    if (SUCCEEDED(IInspectable_QueryInterface( vm, &IID_INotifyPropertyChanged_UWP, &npc )) && npc)
    {
        struct notify_property_changed *npcp = (struct notify_property_changed *)npc;
        EventRegistrationToken tok = {0};
        if (npcp->lpVtbl->add_PropertyChanged &&
            SUCCEEDED(npcp->lpVtbl->add_PropertyChanged( npcp, &g_calc_vm_npc_handler, &tok )))
        {
            g_calc_vm.npc = (IInspectable *)npc;
            g_calc_vm.npc_token = tok;
            g_calc_vm.npc_hooked = TRUE;
        }
        else
        {
            IUnknown_Release( (IUnknown *)npc );
        }
    }

    /* Resolve ICommand face via IXamlMember ("ButtonPressed") or direct QI on VM */
    if (g_calc_vm.vm_class_name[0] && uwp_get_app_outer() &&
        uwp_get_custom_xaml_member( uwp_get_app_outer(), g_calc_vm.vm_class_name, L"ButtonPressed", vm, &cmd_prop ) && cmd_prop)
    {
        HRESULT qhr = IInspectable_QueryInterface( cmd_prop, &IID_ICommand_UWP2, &cmd_iface );
        if (FAILED(qhr))
            qhr = IInspectable_QueryInterface( cmd_prop, &IID_ICommand_UWP, &cmd_iface );
        TRACE( "uwp_calc_vm_set_instance: ButtonPressed=%p QI(ICommand) -> %#lx (%p)\n",
               (void *)cmd_prop, qhr, cmd_iface );
        if (SUCCEEDED(qhr) && cmd_iface)
            g_calc_vm.cmd = (IInspectable *)cmd_iface;
        else
        {
            g_calc_vm.cmd = cmd_prop;
            IInspectable_AddRef( cmd_prop );
        }
        IInspectable_Release( cmd_prop );
    }
    if (!g_calc_vm.cmd &&
        (SUCCEEDED(IInspectable_QueryInterface( vm, &IID_ICommand_UWP2, &cmd_iface )) ||
         SUCCEEDED(IInspectable_QueryInterface( vm, &IID_ICommand_UWP, &cmd_iface ))) && cmd_iface)
        g_calc_vm.cmd = (IInspectable *)cmd_iface;

    if (g_calc_vm.cmd && is_calculator_app())
    {
        IInspectable *mode_args = uwp_make_button_pressed_args( 200 );
        if (mode_args)
        {
            HRESULT mhr = E_FAIL;
            __TRY
            {
                struct command_iface *cmd = (struct command_iface *)g_calc_vm.cmd;
                if (cmd->lpVtbl->Execute) mhr = cmd->lpVtbl->Execute( cmd, mode_args );
            }
            __EXCEPT_ALL { mhr = E_FAIL; }
            __ENDTRY;
            TRACE( "uwp_calc_vm_set_instance: ModeBasic(200) via ICommand::Execute -> %#lx\n", mhr );
            IInspectable_Release( mode_args );
        }
    }

    g_calc_vm.pumped = TRUE;
    uwp_sync_calc_display_from_vm();
}

static void uwp_calc_vm_ensure( void )
{
    WCHAR vm_type_name[128] = {0};
    IInspectable *vm_inst = NULL;
    HSTRING h = NULL;

    if (g_calc_vm.vm)
    {
        uwp_bind_calc_result_elements();
        return;
    }

    uwp_resolve_vm_type_from_metadata( vm_type_name, ARRAY_SIZE(vm_type_name), &vm_inst );
    if (!vm_inst && vm_type_name[0] &&
        SUCCEEDED(WindowsCreateString( vm_type_name, wcslen(vm_type_name), &h )) && h)
    {
        HRESULT hr = RoActivateInstance( h, &vm_inst );
        TRACE( "uwp_calc_vm_ensure: RoActivateInstance(%s) -> %#lx vm %p\n",
               debugstr_w(vm_type_name), hr, (void *)vm_inst );
        WindowsDeleteString( h );
        if (FAILED(hr)) vm_inst = NULL;
    }

    if (vm_inst)
    {
        uwp_calc_vm_set_instance( vm_inst, vm_type_name );
        IInspectable_Release( vm_inst );
    }
}

static IInspectable *uwp_make_button_pressed_args( int code )
{
    static const WCHAR args_cls[] = L"CalculatorApp.ViewModel.Common.CalculatorButtonPressedEventArgs";
    HSTRING h_cls = NULL;
    IInspectable *args = NULL;
    HRESULT hr;

    if (SUCCEEDED(WindowsCreateString( args_cls, ARRAY_SIZE(args_cls) - 1, &h_cls )) && h_cls)
    {
        IActivationFactory *af = NULL;
        hr = RoGetActivationFactory( h_cls, &IID_IActivationFactory, (void **)&af );
        TRACE( "uwp_make_button_pressed_args: RoGetActivationFactory -> %#lx af=%p\n", hr, (void *)af );
        if (SUCCEEDED(hr) && af)
        {
            ULONG iid_cnt = 0, i;
            IID *iids = NULL;
            if (SUCCEEDED(af->lpVtbl->GetIids( af, &iid_cnt, &iids )) && iids)
            {
                for (i = 0; i < iid_cnt; i++)
                {
                    void *fac = NULL;
                    if (IsEqualGUID( &iids[i], &IID_IActivationFactory )) continue;
                    if (SUCCEEDED(IActivationFactory_QueryInterface( af, &iids[i], &fac )) && fac)
                    {
                        HRESULT chr = E_FAIL;
                        __TRY
                        {
                            HRESULT (WINAPI *create_inst)(void *, HSTRING, INT32, IInspectable **) =
                                (void *)(*(void ***)fac)[6];
                            if (create_inst) chr = create_inst( fac, NULL, code, &args );
                        }
                        __EXCEPT_ALL { chr = E_FAIL; args = NULL; }
                        __ENDTRY;
                        TRACE( "uwp_make_button_pressed_args: iid[%lu] %s CreateInstance(%d) -> %#lx args=%p\n",
                               i, debugstr_guid(&iids[i]), code, chr, (void *)args );
                        IUnknown_Release( (IUnknown *)fac );
                        if (SUCCEEDED(chr) && args) break;
                        args = NULL;
                    }
                }
                CoTaskMemFree( iids );
            }
            IActivationFactory_Release( af );
        }
        WindowsDeleteString( h_cls );
    }
    return args;
}

static void uwp_calc_vm_send( int code, IInspectable *cmd_param )
{
    WCHAR pre_disp[256] = {0}, post_disp[256] = {0};

    uwp_calc_vm_ensure();
    if (!g_calc_vm.vm) return;

    uwp_vm_read_display_value( g_calc_vm.vm, g_calc_vm.vm_class_name, pre_disp, ARRAY_SIZE(pre_disp) );
    TRACE( "uwp_calc_vm_send: code %d pre DisplayValue = %s\n", code, debugstr_w(pre_disp) );

    if (g_calc_vm.cmd)
    {
        IInspectable *args = uwp_make_button_pressed_args( code );
        IInspectable *param = args ? args : (cmd_param ? cmd_param : box_int32_value( code ));
        HRESULT chr = E_FAIL;
        if (!args && cmd_param) IInspectable_AddRef( cmd_param );
        __TRY
        {
            struct command_iface *cmd = (struct command_iface *)g_calc_vm.cmd;
            if (cmd->lpVtbl->Execute) chr = cmd->lpVtbl->Execute( cmd, param );
        }
        __EXCEPT_ALL
        {
            chr = E_FAIL;
        }
        __ENDTRY;
        if (param) IInspectable_Release( param );
        uwp_vm_read_display_value( g_calc_vm.vm, g_calc_vm.vm_class_name, post_disp, ARRAY_SIZE(post_disp) );
        TRACE( "uwp_calc_vm_send: code %d (args=%p) Execute -> %#lx post DisplayValue = %s\n",
               code, (void *)args, chr, debugstr_w(post_disp) );
    }

    uwp_sync_calc_display_from_vm();
}

static void register_uwp_elem( struct uwp_element *el )
{
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    el->next_reg = g_all_uwp_elems_head;
    g_all_uwp_elems_head = el;
    g_all_uwp_elem_cnt++;
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
}

static const GUID IID_UWP_ELEM_SELF =
    { 0x7a1b2c3d, 0x4e5f, 0x4678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x79 } };

static struct uwp_element *uwp_elem_from_any( void *ptr )
{
    struct uwp_element *el;
    void *self_iface = NULL;
    void *inner = NULL;
    if (!ptr || (ULONG_PTR)ptr < 0x10000) return NULL;
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (el = g_all_uwp_elems_head; el; el = el->next_reg)
    {
        if ((void *)ptr == (void *)el->outer ||
            (void *)ptr == (void *)el->provider_inst ||
            ((char *)ptr >= (char *)el && (char *)ptr < (char *)(el + 1)))
        {
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            return el;
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    /* .NET Native hands us __ComObject RCWs that answer QI from their own
     * projection table (only IUnknown): unwrap get_Inner (slot 6) /
     * get_InnerIUnknown (slot 7) to reach our CCW face, then resolve that.
     * Only when the SELF identity QI failed (i.e. ptr is a foreign RCW). */
    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_UWP_ELEM_SELF, &self_iface )) && self_iface)
    {
        void *probe_ptr = self_iface;
        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
        for (el = g_all_uwp_elems_head; el; el = el->next_reg)
        {
            if (probe_ptr == (void *)&el->nondelegating_iface ||
                ((char *)probe_ptr >= (char *)el && (char *)probe_ptr < (char *)(el + 1)))
            {
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                IUnknown_Release( (IUnknown *)self_iface );
                return el;
            }
        }
        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
        IUnknown_Release( (IUnknown *)self_iface );
    }
    if (xaml_get_app_profile()->seed_xbf != NULL && ptr != uwp_get_app_outer())
    {
        void *pv_iface = NULL;
        void *cand = NULL;
        /* Boxed primitive PropertyValues are never UIElements; do not probe vtbl[6]/[7] on them */
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IPropertyValue_g, &pv_iface )) && pv_iface)
        {
            IUnknown_Release( (IUnknown *)pv_iface );
            return NULL;
        }
        __TRY
        {
            void **vtbl = *(void ***)ptr;
            HRESULT (WINAPI *get_inner)(void *, void **) = (void *)vtbl[6];
            HRESULT (WINAPI *get_inner_iu)(void *, void **) = (void *)vtbl[7];
            if (get_inner && SUCCEEDED(get_inner( ptr, &cand )) && cand)
                inner = cand;
            else if (get_inner_iu && SUCCEEDED(get_inner_iu( ptr, &cand )) && cand)
                inner = cand;
            else inner = NULL;
        }
        __EXCEPT_ALL
        {
            inner = NULL;
        }
        __ENDTRY;
        if (inner)
        {
            /* Validate: trust inner only when it answers our identity IID
             * (slot 6 on native objects is an arbitrary property getter). */
            void *id = NULL;
            if (FAILED(IUnknown_QueryInterface( (IUnknown *)inner, &IID_UWP_ELEM_SELF, &id )) || !id)
            {
                IUnknown_Release( (IUnknown *)inner );
                return NULL;
            }
            IUnknown_Release( (IUnknown *)id );
            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
            for (el = g_all_uwp_elems_head; el; el = el->next_reg)
            {
                if ((void *)inner == (void *)el->outer ||
                    (void *)inner == (void *)&el->nondelegating_iface ||
                    ((char *)inner >= (char *)el && (char *)inner < (char *)(el + 1)))
                {
                    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                    IUnknown_Release( (IUnknown *)inner );
                    return el;
                }
            }
            TRACE( "uwp_elem_from_any: INNER-MISS ptr=%p inner=%p cnt=%d\n", ptr, inner, g_all_uwp_elem_cnt );
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            IUnknown_Release( (IUnknown *)inner );
            return NULL;
        }
    }
    /* Fallback: Try QI for IID_IDependencyObject_UWP to unwrap MCG RCW/CCW */
    {
        void *do_iface = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IDependencyObject_UWP, &do_iface )) && do_iface)
        {
            struct uwp_element *e2;
            int k;
            el = NULL;
            /* RCW may return its own projected face; probe all our faces on it */
            for (k = 0; k < 8; k++)
            {
                void *cand = NULL;
                static const GUID *iids[8] = { &IID_IUIElement_UWP, &IID_IFrameworkElement_UWP,
                    &IID_IControl_UWP, &IID_IColumnDefinition_UWP };
                if (k >= 4) break;
                if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)do_iface, iids[k], &cand )) && cand)
                {
                    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                    for (e2 = g_all_uwp_elems_head; e2; e2 = e2->next_reg)
                    {
                        if ((void *)cand == (void *)e2->outer ||
                            ((char *)cand >= (char *)e2 && (char *)cand < (char *)(e2 + 1)))
                        { el = e2; break; }
                    }
                    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                    IUnknown_Release( (IUnknown *)cand );
                    if (el) break;
                }
            }
            if (el)
            {
                IUnknown_Release( (IUnknown *)do_iface );
                return el;
            }
            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
            TRACE( "uwp_elem_from_any: MISS ptr=%p do_iface=%p cnt=%d\n", ptr, do_iface, g_all_uwp_elem_cnt );
            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
            IUnknown_Release( (IUnknown *)do_iface );
        }
    }
    return NULL;
}

static HWND g_island_hwnds[16];
static int g_island_hwnd_cnt = 0;

static void uwp_invalidate_window( void )
{
    if (g_island_hwnd_cnt > 0)
    {
        int i;
        g_layout_dirty = TRUE;
        for (i = 0; i < g_island_hwnd_cnt; i++)
        {
            if (g_island_hwnds[i] && IsWindow( g_island_hwnds[i] ) && IsWindowVisible( g_island_hwnds[i] ))
                InvalidateRect( g_island_hwnds[i], NULL, FALSE );
        }
        return;
    }
    {
        HWND top = xaml_find_core_window();
        xaml_mark_window_dirty();
        if (top)
        {
            HWND root = GetAncestor( top, GA_ROOT );
            if (root && root != top) InvalidateRect( root, NULL, FALSE );
            InvalidateRect( top, NULL, FALSE );
        }
    }
}

static HRESULT WINAPI uwp_nondelegating_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static ULONG WINAPI uwp_nondelegating_AddRef( IInspectable *iface );
static ULONG WINAPI uwp_nondelegating_Release( IInspectable *iface );

struct uwp_weak_ref
{
    IWeakReference IWeakReference_iface;
    LONG ref;
    struct uwp_element *elem;
};

static HRESULT WINAPI uwp_wr_QI( IWeakReference *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IWeakReference_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_weak_ref *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_wr_AddRef( IWeakReference *iface )
{
    return InterlockedIncrement( &((struct uwp_weak_ref *)iface)->ref );
}
static ULONG WINAPI uwp_wr_Release( IWeakReference *iface )
{
    struct uwp_weak_ref *wr = (struct uwp_weak_ref *)iface;
    ULONG r = InterlockedDecrement( &wr->ref );
    if (!r) free( wr );
    return r;
}
static HRESULT WINAPI uwp_wr_Resolve( IWeakReference *iface, REFIID iid, IInspectable **out )
{
    struct uwp_weak_ref *wr = (struct uwp_weak_ref *)iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!wr->elem) return S_OK;
    if (wr->elem->outer)
        return IInspectable_QueryInterface( wr->elem->outer, iid, (void **)out );
    return IInspectable_QueryInterface( &wr->elem->IInspectable_iface, iid, (void **)out );
}
static const struct IWeakReferenceVtbl uwp_weak_ref_vtbl =
{
    uwp_wr_QI,
    uwp_wr_AddRef,
    uwp_wr_Release,
    uwp_wr_Resolve,
};

static HRESULT WINAPI uwp_wrs_QI( IWeakReferenceSource *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}
static ULONG WINAPI uwp_wrs_AddRef( IWeakReferenceSource *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}
static ULONG WINAPI uwp_wrs_Release( IWeakReferenceSource *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    return uwp_nondelegating_Release( &impl->nondelegating_iface );
}
static HRESULT WINAPI uwp_wrs_GetWeakReference( IWeakReferenceSource *iface, IWeakReference **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IWeakReferenceSource_iface );
    struct uwp_weak_ref *wr;
    if (!out) return E_POINTER;
    wr = calloc( 1, sizeof(*wr) );
    if (!wr) return E_OUTOFMEMORY;
    wr->IWeakReference_iface.lpVtbl = &uwp_weak_ref_vtbl;
    wr->ref = 1;
    wr->elem = impl;
    *out = &wr->IWeakReference_iface;
    return S_OK;
}
static const struct IWeakReferenceSourceVtbl uwp_wrs_vtbl =
{
    uwp_wrs_QI,
    uwp_wrs_AddRef,
    uwp_wrs_Release,
    uwp_wrs_GetWeakReference,
};

static HRESULT WINAPI uwp_sub_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    /* Private identity IID: answer locally so app-side RCW QIs can be
     * resolved back to our element (delegation to outer loses this). */
    if (IsEqualGUID( iid, &IID_UWP_ELEM_SELF ))
    {
        if (!out) return E_POINTER;
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        if (!out) return E_POINTER;
        *out = &impl->IWeakReferenceSource_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (impl->outer && *(void **)impl->outer)
        return IInspectable_QueryInterface( impl->outer, iid, out );
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}

static ULONG WINAPI uwp_sub_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->outer && *(void **)impl->outer && !xaml_get_app_profile()->seed_xbf)
        return IInspectable_AddRef( impl->outer );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}

static ULONG WINAPI uwp_sub_Release( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->outer && *(void **)impl->outer && !xaml_get_app_profile()->seed_xbf)
        return IInspectable_Release( impl->outer );
    return uwp_nondelegating_Release( &impl->nondelegating_iface );
}

static HRESULT WINAPI uwp_sub_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_sub_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}

static HRESULT WINAPI uwp_sub_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_generic_noop( void *iface, ... )
{
    return S_OK;
}

static HRESULT WINAPI uwp_nondelegating_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_UWP_ELEM_SELF ))
    {
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ))
    {
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAgileObject ))
    {
        if (impl->outer && !xaml_get_app_profile()->seed_xbf)
        {
            *out = &impl->IInspectable_iface;
            IInspectable_AddRef( impl->outer );
            return S_OK;
        }
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        *out = &impl->IWeakReferenceSource_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDependencyObject_UWP ))
        *out = &impl->IDependencyObject_iface;
    else if (IsEqualGUID( iid, &IID_IDependencyObject2_UWP ))
        *out = &impl->IDependencyObject2_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement_UWP ))
        *out = &impl->IUIElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement_UWP ))
        *out = &impl->IFrameworkElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement3_UWP ))
        *out = &impl->IFrameworkElement3_iface;
    else if (IsEqualGUID( iid, &IID_IControl_UWP ))
        *out = &impl->IControl_iface;
    else if (IsEqualGUID( iid, &IID_IControlProtected_UWP ))
        *out = &impl->IControlProtected_iface;
    else if (IsEqualGUID( iid, &IID_IContentControl_UWP ) ||
             IsEqualGUID( iid, &IID_INavigationView_UWP ) ||
             IsEqualGUID( iid, &IID_INavigationView2_UWP ) ||
             IsEqualGUID( iid, &IID_IDesktopWindowXamlSource_UWP ))
        *out = &impl->IContentControl_iface;
    else if (IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceNative_UWP ) ||
             IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceNative2_UWP ))
        *out = &impl->IDesktopWindowXamlSourceNative_iface;
    else if (IsEqualGUID( iid, &IID_IUserControl_UWP ))
        *out = &impl->IUserControl_iface;
    else if (IsEqualGUID( iid, &IID_IPage_UWP ))
        *out = &impl->IPage_iface;
    else if (IsEqualGUID( iid, &IID_IFrame_UWP ) ||
             IsEqualGUID( iid, &IID_INavigate_UWP ))
        *out = &impl->IFrame_iface;
    else if (IsEqualGUID( iid, &IID_IButtonBase_UWP ) ||
             IsEqualGUID( iid, &IID_IHyperlinkButton_UWP ))
        *out = &impl->IButtonBase_iface;
    else if (IsEqualGUID( iid, &IID_IToggleButton_UWP ) ||
             IsEqualGUID( iid, &IID_IRadioButton_UWP ))
        *out = &impl->IToggleButton_iface;
    else if (IsEqualGUID( iid, &IID_IToggleMenuFlyoutItem_UWP ))
    {
        if (wcsstr( impl->class_name, L"ToggleMenuFlyoutItem" ))
            *out = &impl->IToggleMenuFlyoutItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IRadioButtons_WinUI ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IPanel_UWP ) ||
             IsEqualGUID( iid, &IID_IStackPanel_UWP ) ||
             IsEqualGUID( iid, &IID_IStackPanel_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuBar_WinUI ))
        *out = &impl->IPanel_iface;
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutSubItem_UWP ))
    {
        if (wcsstr( impl->class_name, L"MenuFlyoutSubItem" ))
            *out = &impl->IMenuFlyoutSubItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IUIElement4_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement4_UWP ))
        *out = &impl->IUIElement4_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement7_UWP ))
        *out = &impl->IUIElement7_iface;
    else if (IsEqualGUID( iid, &IID_IKeyboardAccelerator_UWP ))
        *out = &impl->IKeyboardAccelerator_iface;
    else if (IsEqualGUID( iid, &IID_IXamlUICommand_UWP ))
        *out = &impl->IXamlUICommand_iface;
    else if (IsEqualGUID( iid, &IID_ICommand_UWP ) ||
             IsEqualGUID( iid, &IID_ICommand_UWP2 ))
        *out = &impl->ICommand_iface;
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutItem_UWP ))
    {
        if (wcsstr( impl->class_name, L"MenuFlyoutItem" ))
            *out = &impl->IMenuFlyoutItem_iface;
    }
    else if (IsEqualGUID( iid, &IID_IMenuFlyoutItem3_UWP ))
    {
        if (wcsstr( impl->class_name, L"MenuFlyoutItem" ))
            *out = &impl->IMenuFlyoutItem3_iface;
    }
    else if (IsEqualGUID( iid, &IID_IMenuFlyout2_Real_UWP ))
        *out = &impl->IMenuFlyout2_iface;
    else if (IsEqualGUID( iid, &IID_IContentDialog_UWP ))
        *out = &impl->IContentDialog_iface;
    else if (IsEqualGUID( iid, &IID_IContentDialog2_UWP ))
        *out = &impl->IContentDialog2_iface;
    else if (IsEqualGUID( iid, &IID_ITeachingTip_WinUI ))
        *out = &impl->ITeachingTip_iface;
    else if (IsEqualGUID( iid, &IID_IGrid_UWP ) ||
             IsEqualGUID( iid, &IID_IGrid_UWP2 ) ||
             IsEqualGUID( iid, &IID_IGrid3_Calc ))
        *out = &impl->IGrid_iface;
    else if (IsEqualGUID( iid, &IID_IBorder_UWP ) ||
             IsEqualGUID( iid, &IID_IBorder2_Calc ))
        *out = &impl->IBorder_iface;
    else if (IsEqualGUID( iid, &IID_ITextBlock_UWP ))
        *out = &impl->ITextBlock_iface;
    else if (IsEqualGUID( iid, &IID_IRowDefinition_UWP ))
        *out = &impl->IRowDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IColumnDefinition_UWP ))
        *out = &impl->IColumnDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IScrollViewer_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer2_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer3_UWP ))
        *out = &impl->IScrollViewer_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement2_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement3_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement_Calc2 ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement2_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement4_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement6_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides2_UWP ) ||
             IsEqualGUID( iid, &IID_IControl2_UWP ) ||
             IsEqualGUID( iid, &IID_IControl3_UWP ) ||
             IsEqualGUID( iid, &IID_IControl4_UWP ) ||
             IsEqualGUID( iid, &IID_IControl5_UWP ) ||
             IsEqualGUID( iid, &IID_IControlOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControl2_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControlOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControlOvr_Calc ) ||
             IsEqualGUID( iid, &IID_IPageOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IButton_UWP ) ||
             IsEqualGUID( iid, &IID_IButtonWithFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IToggleButtonOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock2_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock3_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock6_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc2 ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc3 ) ||
             IsEqualGUID( iid, &IID_ITextBlock_Calc4 ) ||
             IsEqualGUID( iid, &IID_IPopup_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup2_UWP ) ||
             IsEqualGUID( iid, &IID_IPopup3_UWP ) ||
             IsEqualGUID( iid, &IID_IStyle_UWP ) ||
             IsEqualGUID( iid, &IID_ISetter_UWP ) ||
             IsEqualGUID( iid, &IID_ISetterBase_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualStateGroup_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualState_UWP ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IBrush_UWP ))
        *out = &impl->IBrush_iface;
    else if (IsEqualGUID( iid, &IID_ISolidColorBrush_UWP ))
        *out = &impl->ISolidColorBrush_iface;
    else if (IsEqualGUID( iid, &IID_IFlyoutBase_UWP ) ||
             IsEqualGUID( iid, &IID_IFlyoutBase3_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuBarItem_WinUI ) ||
             IsEqualGUID( iid, &IID_ITabView_WinUI ))
        *out = &impl->IFlyout_iface;
    else if (IsEqualGUID( iid, &IID_IDispatcherTimer_UWP ))
        *out = &impl->IInspectable_iface;
    else if (IsEqualGUID( iid, &IID_IStateTriggerBase_UWP ) ||
             IsEqualGUID( iid, &IID_IStateTriggerBaseProtected_UWP ) ||
             IsEqualGUID( iid, &IID_IMarkupExtension_UWP ) ||
             IsEqualGUID( iid, &IID_IMarkupExtensionOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement10_UWP ) ||
             IsEqualGUID( iid, &IID_IXamlRoot_UWP ))
        *out = &impl->IStateTrigger_iface;
    else
    {
        static const GUID IID_IObservableVector_DO =
            { 0xfac6f4d2, 0x96fd, 0x5ceb, { 0x80, 0xbf, 0x46, 0xac, 0x65, 0x29, 0xfc, 0x65 } };
        static const GUID IID_IObservableVector_ICBE =
            { 0xd5644006, 0x717d, 0x512d, { 0x87, 0x85, 0xb4, 0x8c, 0x17, 0xe7, 0x34, 0x68 } };
        static const GUID IID_IVector_DO =
            { 0x771b857e, 0xab5c, 0x5db8, { 0xa0, 0x21, 0x39, 0x7c, 0x92, 0xcd, 0xc4, 0x4c } };
        static const GUID IID_IVector_Insp =
            { 0xb32bdca4, 0x5e52, 0x5b27, { 0xbc, 0x5d, 0xd6, 0x6a, 0x1a, 0x26, 0x8c, 0x2a } };
        static const GUID IID_IVector_MBI =
            { 0xef83f4ba, 0x0b73, 0x51d5, { 0xb1, 0x3b, 0x89, 0xa2, 0xb1, 0x56, 0xad, 0x9a } };
        static const GUID IID_IVector_MFIB =
            { 0xf7f1c37b, 0xb4df, 0x5128, { 0xa4, 0x76, 0xfc, 0x83, 0x99, 0xbc, 0x10, 0xaf } };
        static const GUID IID_IIterable_DO =
            { 0xf66c6bd3, 0x55b4, 0x5bbb, { 0xb8, 0x2a, 0x6d, 0x9c, 0xe3, 0x83, 0x09, 0x1a } };
        static const GUID IID_IVectorView_DO =
            { 0xfe750d77, 0x1307, 0x5df2, { 0xa0, 0x21, 0x1c, 0x7a, 0x8d, 0x6b, 0x80, 0xad } };
        if (IsEqualGUID( iid, &IID_IObservableVector_DO ) ||
            IsEqualGUID( iid, &IID_IObservableVector_ICBE ))
            *out = &impl->IObservableVector_iface;
        else if (IsEqualGUID( iid, &IID_IVector_DO ) ||
                 IsEqualGUID( iid, &IID_IVector_Insp ) ||
                 IsEqualGUID( iid, &IID_IVector_MBI ) ||
                 IsEqualGUID( iid, &IID_IVector_MFIB ))
            *out = &impl->IVector_iface;
        else if (IsEqualGUID( iid, &IID_IIterable_DO ))
            *out = &impl->IIterable_iface;
        else if (IsEqualGUID( iid, &IID_IVectorView_DO ))
            *out = &impl->IVectorView_iface;
        else
        {
            size_t k;
            for (k = 0; k < ARRAY_SIZE(IID_IUIE_Extra); k++)
            {
                if (IsEqualGUID( iid, &IID_IUIE_Extra[k] ))
                {
                    *out = &impl->IStyle_iface;
                    break;
                }
            }
        }
    }
    if (!*out && (IsEqualGUID( iid, &IID_IDCompositionVisual ) ||
                  IsEqualGUID( iid, &IID_IDCompositionVisual2 )))
    {
        if (impl->visual)
        {
            *out = impl->visual;
            IDCompositionVisual_AddRef( impl->visual );
            return S_OK;
        }
    }

    if (*out)
    {
        TRACE( "uwp_nondelegating_QI: matched %s -> %p on %s\n", debugstr_guid(iid), *out, debugstr_w(impl->class_name) );
        uwp_sub_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    TRACE( "uwp_nondelegating_QI: %s not matched on %s\n", debugstr_guid(iid), debugstr_w(impl->class_name) );
    return E_NOINTERFACE;
}

static ULONG WINAPI uwp_nondelegating_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI uwp_nondelegating_Release( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        /* Keep registered elements alive for visual tree lifetime */
        impl->outer = NULL;
        impl->ref = 1;
    }
    return ref;
}

/* IDependencyPropertyChangedEventArgs implementation (slots 6=NewValue, 7=OldValue, 8=Property) */
struct uwp_dp_changed_args
{
    const void *vtbl;
    LONG ref;
    IInspectable *old_val;
    IInspectable *new_val;
    void *dp;
};

static HRESULT WINAPI dp_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IRoutedEventArgs_UWP =
        { 0x443ecc75, 0x7e5d, 0x48a5, { 0x93, 0x1a, 0x30, 0x0e, 0x46, 0xa4, 0x91, 0x6a } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDependencyPropertyChangedEventArgs_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_dp_changed_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dp_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_dp_changed_args *)iface)->ref );
}
static ULONG WINAPI dp_args_Release( void *iface )
{
    struct uwp_dp_changed_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->old_val) IInspectable_Release( a->old_val );
        if (a->new_val) IInspectable_Release( a->new_val );
        free( a );
    }
    return r;
}
static HRESULT WINAPI dp_args_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI dp_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR *cls = L"Windows.UI.Xaml.DependencyPropertyChangedEventArgs";
    return WindowsCreateString( cls, wcslen(cls), name );
}
static HRESULT WINAPI dp_args_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI dp_args_get_Property( void *iface, void **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->dp;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}
static HRESULT WINAPI dp_args_get_OldValue( void *iface, IInspectable **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->old_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI dp_args_get_NewValue( void *iface, IInspectable **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->new_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static const void *dp_changed_args_vtbl[] =
{
    dp_args_QI,
    dp_args_AddRef,
    dp_args_Release,
    dp_args_GetIids,
    dp_args_GetRuntimeClassName,
    dp_args_GetTrustLevel,
    dp_args_get_Property, /* 6: get_Property */
    dp_args_get_OldValue, /* 7: get_OldValue */
    dp_args_get_NewValue, /* 8: get_NewValue */
};

/* Access metadata callback on our IDependencyProperty (from main.c) */
struct dp_internal_layout
{
    IDependencyProperty IDependencyProperty_iface;
    LONG ref;
    IPropertyMetadata *metadata;
};
struct pm_internal_layout
{
    IPropertyMetadata IPropertyMetadata_iface;
    LONG ref;
    IInspectable *default_value;
    IPropertyChangedCallback *callback;
};

/* --- IDependencyObject (slots 6=GetValue, 7=SetValue, 8=ClearValue, 9=ReadLocalValue, 10=GetAnimationBaseValue, 11=get_Dispatcher) --- */
static HRESULT WINAPI uwp_do_GetValue( void *iface, void *dp, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int i;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !dp) return S_OK;

    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            *out = impl->dp_store[i].value;
            if (*out) IInspectable_AddRef( *out );
            return S_OK;
        }
    }

    /* Fall back to IPropertyMetadata::get_DefaultValue if DP has metadata */
    {
        struct dp_internal_layout *dpl = (struct dp_internal_layout *)dp;
        if (dpl && dpl->metadata)
        {
            IPropertyMetadata_get_DefaultValue( dpl->metadata, out );
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_do_SetValue( void *iface, void *dp, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    IInspectable *old_val = NULL;
    WCHAR str_val[256] = {0};
    int i;

    if (!impl || !dp) return S_OK;

    /* Check if val is a string (e.g. CalculationResult.DisplayValue or Content) */
    if (val && uwp_unbox_string( val, str_val, ARRAY_SIZE(str_val) ) && str_val[0])
    {
        if (uwp_is_display_result_element( impl ) ||
            wcsstr( impl->class_name, L"OverflowTextBlock" ) ||
            wcsstr( impl->class_name, L"TabsBarItem" ) ||
            wcsstr( impl->class_name, L"TabViewItem" ) ||
            wcsstr( impl->class_name, L"MenuBarItem" ) ||
            !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.Control" ) ||
            !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.ListViewItem" ) ||
            impl->kind == UWP_KIND_TEXT_BLOCK)
        {
            lstrcpynW( impl->text, str_val, ARRAY_SIZE(impl->text) );
            if (impl->tpl_normal_output)
                lstrcpynW( impl->tpl_normal_output->text, str_val, ARRAY_SIZE(impl->tpl_normal_output->text) );
            TRACE( "SetValue updated display text on %s (%s) -> %s\n",
                   debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_val) );
            uwp_invalidate_window();
        }
    }

    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            old_val = impl->dp_store[i].value;
            impl->dp_store[i].value = val;
            if (val) IInspectable_AddRef( val );
            break;
        }
    }
    if (i == impl->dp_cnt)
    {
        if (!xaml_vec_grow( (void **)&impl->dp_store, &impl->dp_cap, impl->dp_cnt + 1, sizeof(*impl->dp_store), 24 ))
            return E_OUTOFMEMORY;
        impl->dp_store[impl->dp_cnt].dp = dp;
        impl->dp_store[impl->dp_cnt].value = val;
        if (val) IInspectable_AddRef( val );
        impl->dp_cnt++;
    }

    /* Fire PropertyMetadata PropertyChangedCallback if present */
    {
        struct dp_internal_layout *dpl = (struct dp_internal_layout *)dp;
        if (dpl && dpl->metadata)
        {
            struct pm_internal_layout *pml = (struct pm_internal_layout *)dpl->metadata;
            if (!old_val && pml->default_value)
            {
                old_val = pml->default_value;
                IInspectable_AddRef( old_val );
            }
            if (pml->callback)
            {
                struct uwp_dp_changed_args *args = calloc( 1, sizeof(*args) );
                if (args)
                {
                    IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                    args->vtbl = dp_changed_args_vtbl;
                    args->ref = 1;
                    args->dp = dp;
                    args->old_val = old_val;
                    if (old_val) IInspectable_AddRef( old_val );
                    args->new_val = val;
                    if (val) IInspectable_AddRef( val );
                    TRACE( "uwp_do_SetValue invoking PropertyChangedCallback=%p sender=%p (%s) dp=%p old=%p new=%p\n",
                           pml->callback, sender, debugstr_w(impl->class_name), dp, old_val, val );
                    IPropertyChangedCallback_Invoke( pml->callback, (IDependencyObject *)sender,
                                                     (IDependencyPropertyChangedEventArgs *)args );
                    {
                        IInspectable *nv = NULL;
                        IDependencyPropertyChangedEventArgs_get_NewValue( (IDependencyPropertyChangedEventArgs *)args, &nv );
                        TRACE( "uwp_do_SetValue callback read NewValue -> %p (val %p)\n", (void *)nv, (void *)val );
                        if (nv) IInspectable_Release( nv );
                    }
                    dp_args_Release( args );
                }
            }
        }
    }

    /* Fire registered DependencyPropertyChangedCallbacks */
    for (i = 0; i < impl->dp_cb_cnt; i++)
    {
        if (impl->dp_cbs[i].dp == dp && impl->dp_cbs[i].callback)
        {
            IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->dp_cbs[i].callback;
            h->lpVtbl->Invoke( h, sender, dp );
        }
    }

    if (old_val) IInspectable_Release( old_val );
    return S_OK;
}

static HRESULT WINAPI uwp_do_ClearValue( void *iface, void *dp )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int i;
    if (!impl || !dp) return S_OK;
    for (i = 0; i < impl->dp_cnt; i++)
    {
        if (impl->dp_store[i].dp == dp)
        {
            if (impl->dp_store[i].value) IInspectable_Release( impl->dp_store[i].value );
            impl->dp_store[i] = impl->dp_store[--impl->dp_cnt];
            break;
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_do_get_Dispatcher( void *iface, void **value )
{
    ICoreWindow *cw;
    if (!value) return E_POINTER;
    *value = NULL;
    cw = get_current_core_window();
    if (cw) return ICoreWindow_get_Dispatcher( cw, (ICoreDispatcher **)value );
    return S_OK;
}

static HRESULT WINAPI uwp_do_get_DispatcherQueue( void *iface, void **value )
{
    static const WCHAR name[] = L"Windows.System.DispatcherQueue";
    HSTRING hstr;
    IActivationFactory *af = NULL;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &hstr );
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory( hstr, &IID_IActivationFactory, (void **)&af );
    WindowsDeleteString( hstr );
    if (SUCCEEDED(hr) && af)
    {
        void **vtbl = *(void ***)af;
        HRESULT (WINAPI *pGetForCurrentThread)(void *, void **) = (void *)vtbl[6];
        if (pGetForCurrentThread) hr = pGetForCurrentThread( af, value );
        IActivationFactory_Release( af );
    }
    return hr;
}

/* --- IDependencyObject2 (slot 6=RegisterPropertyChangedCallback, slot 7=UnregisterPropertyChangedCallback) --- */
static HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (token) *token = 1;
    if (!impl || !cb) return S_OK;
    if (xaml_vec_grow( (void **)&impl->dp_cbs, &impl->dp_cb_cap, impl->dp_cb_cnt + 1, sizeof(*impl->dp_cbs), 16 ))
    {
        int idx = impl->dp_cb_cnt++;
        INT64 tok = ++impl->dp_cb_next_token;
        impl->dp_cbs[idx].dp = dp;
        impl->dp_cbs[idx].callback = (IInspectable *)cb;
        impl->dp_cbs[idx].token = tok;
        IUnknown_AddRef( (IUnknown *)cb );
        if (token) *token = tok;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_do2_UnregisterPropertyChangedCallback( void *iface, void *dp, INT64 token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    int i;
    if (!impl) return S_OK;
    for (i = 0; i < impl->dp_cb_cnt; i++)
    {
        if (impl->dp_cbs[i].dp == dp && impl->dp_cbs[i].token == token)
        {
            if (impl->dp_cbs[i].callback) IUnknown_Release( (IUnknown *)impl->dp_cbs[i].callback );
            impl->dp_cbs[i] = impl->dp_cbs[--impl->dp_cb_cnt];
            break;
        }
    }
    return S_OK;
}

/* --- IUIElement (slots 6=get_DesiredSize, 21=get_Visibility, 22=put_Visibility, 23=get_RenderSize, ...) --- */
static HRESULT WINAPI uwp_ui_get_DesiredSize( void *iface, Size *sz )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (sz)
    {
        sz->Width = (impl && impl->actual_w > 0) ? (FLOAT)impl->actual_w : 100.0f;
        sz->Height = (impl && impl->actual_h > 0) ? (FLOAT)impl->actual_h : 40.0f;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_ui_get_Visibility( void *iface, int *vis )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (vis) *vis = impl ? impl->visibility : 0;
    return S_OK;
}

static HRESULT WINAPI uwp_ui_put_Visibility( void *iface, int vis )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && impl->visibility != vis)
    {
        impl->visibility = vis;
        uwp_invalidate_window();
    }
    return S_OK;
}

static HRESULT WINAPI uwp_ui_get_RenderSize( void *iface, Size *sz )
{
    return uwp_ui_get_DesiredSize( iface, sz );
}

static HRESULT WINAPI uwp_ui_event_add( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}

static HRESULT WINAPI uwp_ui_get_IsHitTestVisible( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->is_hit_test_visible) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_put_IsHitTestVisible( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_hit_test_visible = val ? TRUE : FALSE;
    return S_OK;
}

#define UWP_UI_PTR_EVENT( name, field ) \
static HRESULT WINAPI uwp_ui_add_##name( void *iface, void *handler, EventRegistrationToken *tok ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    if (!impl) { if (tok) tok->value = 0; return E_INVALIDARG; } \
    return xaml_event_registry_add( &impl->field, handler, tok ); \
} \
static HRESULT WINAPI uwp_ui_remove_##name( void *iface, EventRegistrationToken tok ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    if (!impl) return E_INVALIDARG; \
    return xaml_event_registry_remove( &impl->field, tok ); \
}

UWP_UI_PTR_EVENT( PointerPressed, ptr_pressed_reg )
UWP_UI_PTR_EVENT( PointerMoved, ptr_moved_reg )
UWP_UI_PTR_EVENT( PointerReleased, ptr_released_reg )
UWP_UI_PTR_EVENT( PointerEntered, ptr_entered_reg )
UWP_UI_PTR_EVENT( PointerExited, ptr_exited_reg )
UWP_UI_PTR_EVENT( PointerCaptureLost, ptr_capture_lost_reg )
UWP_UI_PTR_EVENT( PointerCanceled, ptr_canceled_reg )
UWP_UI_PTR_EVENT( PointerWheelChanged, ptr_wheel_reg )

static HRESULT WINAPI uwp_ui_CapturePointer( void *iface, void *pointer, boolean *ret )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) g_captured_elem = impl;
    if (ret) *ret = impl != NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_ReleasePointerCapture( void *iface, void *pointer )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && g_captured_elem == impl) g_captured_elem = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_ui_ReleasePointerCaptures( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && g_captured_elem == impl) g_captured_elem = NULL;
    return S_OK;
}

/* --- IFrameworkElement --- */
static void uwp_realize_deferred_entry( struct uwp_defer_entry *de );
static void uwp_fire_loaded_tree( struct uwp_element *el );

static HRESULT WINAPI uwp_fe_get_Resources( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->resources_rd)
        xaml_resource_dictionary_create_for_uwp( (void **)&impl->resources_rd );
    if (impl->resources_rd && !impl->rd_bucket)
    {
        impl->rd_bucket = get_rd_bucket( impl->resources_rd, TRUE );
        if (impl->rd_bucket) impl->rd_bucket->owner_el = impl;
    }
    *out = impl->resources_rd;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_fe_put_Resources( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->resources_rd) IInspectable_Release( impl->resources_rd );
    impl->resources_rd = (IInspectable *)val;
    impl->rd_bucket = NULL;
    if (impl->resources_rd)
    {
        IInspectable_AddRef( impl->resources_rd );
        impl->rd_bucket = get_rd_bucket( impl->resources_rd, TRUE );
        if (impl->rd_bucket) impl->rd_bucket->owner_el = impl;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_get_Tag( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if ((ULONG_PTR)out < 0x10000 || IsBadWritePtr( out, sizeof(void *) )) return S_OK;
    *out = impl ? impl->tag : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_fe_put_Tag( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->tag) IInspectable_Release( impl->tag );
    impl->tag = (IInspectable *)val;
    if (impl->tag) IInspectable_AddRef( impl->tag );
    return S_OK;
}

static HRESULT WINAPI uwp_fe_get_ActualWidth( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->actual_w > 0) ? impl->actual_w : 320.0;
    return S_OK;
}

static HRESULT WINAPI uwp_fe_get_ActualHeight( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->actual_h > 0) ? impl->actual_h : 500.0;
    return S_OK;
}

static HRESULT WINAPI uwp_fe_get_Width( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->width : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_Width( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->width = val; g_layout_dirty = TRUE; }
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_Height( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->height : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_Height( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->height = val; g_layout_dirty = TRUE; }
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MinWidth( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->min_width : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MinWidth( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->min_width = val;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MaxWidth( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->max_width : 1e9;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MaxWidth( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->max_width = val;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MinHeight( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->min_height : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MinHeight( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->min_height = val;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_MaxHeight( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->max_height : 1e9;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_MaxHeight( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->max_height = val;
    return S_OK;
}

struct uwp_thickness { double Left, Top, Right, Bottom; };

static HRESULT WINAPI uwp_fe_get_Margin( void *iface, struct uwp_thickness *t )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (t)
    {
        t->Left = impl ? impl->margin_l : 0;
        t->Top = impl ? impl->margin_t : 0;
        t->Right = impl ? impl->margin_r : 0;
        t->Bottom = impl ? impl->margin_b : 0;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_put_Margin( void *iface, struct uwp_thickness *t )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && t)
    {
        impl->margin_l = t->Left;
        impl->margin_t = t->Top;
        impl->margin_r = t->Right;
        impl->margin_b = t->Bottom;
        g_layout_dirty = TRUE;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_get_Name( void *iface, HSTRING *name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = impl ? impl->name : L"";
    return WindowsCreateString( s, wcslen(s), name );
}

static HRESULT WINAPI uwp_fe_put_Name( void *iface, HSTRING name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( name, NULL );
    if (impl && s) lstrcpynW( impl->name, s, ARRAY_SIZE(impl->name) );
    return S_OK;
}

static HRESULT WINAPI uwp_fe_get_Parent( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && impl->parent)
    {
        *out = impl->parent->outer ? impl->parent->outer : &impl->parent->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}

/* DataContextChangedEventArgs (slot 6 = get_NewValue, slot 7 = get_Handled, slot 8 = put_Handled) */
struct uwp_dc_args
{
    const void *vtbl;
    LONG ref;
    IInspectable *new_val;
    boolean handled;
};
static HRESULT WINAPI dc_args_get_NewValue( void *iface, IInspectable **out )
{
    struct uwp_dc_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->new_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI dc_args_get_Handled( void *iface, boolean *h )
{
    struct uwp_dc_args *a = iface;
    if (h) *h = a->handled;
    return S_OK;
}
static HRESULT WINAPI dc_args_put_Handled( void *iface, boolean h )
{
    struct uwp_dc_args *a = iface;
    a->handled = h;
    return S_OK;
}
static HRESULT WINAPI dc_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataContextChangedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_dc_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI dc_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR *cls = L"Windows.UI.Xaml.DataContextChangedEventArgs";
    return WindowsCreateString( cls, wcslen(cls), name );
}
static const void *dc_args_vtbl[] =
{
    dc_args_QI,
    dp_args_AddRef,
    dp_args_Release,
    dp_args_GetIids,
    dc_args_GetRuntimeClassName,
    dp_args_GetTrustLevel,
    dc_args_get_NewValue,
    dc_args_get_Handled,
    dc_args_put_Handled,
};

static void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc );

static HRESULT WINAPI uwp_fe_get_DataContext( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *cur = impl;
    if (!out) return E_POINTER;
    *out = NULL;
    while (cur)
    {
        if (cur->data_context)
        {
            *out = cur->data_context;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
        cur = cur->parent;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_put_DataContext( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    TRACE( "uwp_fe_put_DataContext on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), val );
    uwp_propagate_data_context( impl, (IInspectable *)val );
    return S_OK;
}

static HRESULT WINAPI uwp_fe_FindName( void *iface, HSTRING name, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *root;
    struct uwp_element *el;
    const WCHAR *want = WindowsGetStringRawBuffer( name, NULL );
    int i;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !want || !want[0]) return S_OK;

    root = impl->owner_component ? impl->owner_component : impl;
    TRACE( "uwp_fe_FindName(%s) on %s (root=%s)\n",
           debugstr_w(want), debugstr_w(impl->class_name), debugstr_w(root->class_name) );

    /* 1. Check deferred entries on root and impl */
    for (i = 0; i < root->defer_cnt; i++)
    {
        if (!wcscmp( root->defer_entries[i].name, want ) && !root->defer_entries[i].realized)
            uwp_realize_deferred_entry( &root->defer_entries[i] );
    }
    if (impl != root)
    {
        for (i = 0; i < impl->defer_cnt; i++)
        {
            if (!wcscmp( impl->defer_entries[i].name, want ) && !impl->defer_entries[i].realized)
                uwp_realize_deferred_entry( &impl->defer_entries[i] );
        }
    }

    /* 2. Search named elements in root, impl, and global list */
    for (i = 0; i < root->named_cnt; i++)
    {
        struct uwp_element *el = root->named_elems[i];
        if (el && !wcscmp( el->name, want ))
        {
            *out = el->outer ? el->outer : &el->IInspectable_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }
    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    for (el = g_all_uwp_elems_head; el; el = el->next_reg)
    {
        if (!wcscmp( el->name, want ))
        {
            *out = el->outer ? el->outer : &el->IInspectable_iface;
            break;
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_fe_add_Loaded( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->loaded_handlers, &impl->loaded_cap, impl->loaded_cnt + 1, sizeof(*impl->loaded_handlers), 8 ))
    {
        impl->loaded_handlers[impl->loaded_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe3_add_DataContextChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->dc_handlers, &impl->dc_cap, impl->dc_cnt + 1, sizeof(*impl->dc_handlers), 8 ))
    {
        impl->dc_handlers[impl->dc_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        /* If DataContext is already available, fire immediately */
        {
            void *cur_dc = NULL;
            uwp_fe_get_DataContext( iface, &cur_dc );
            if (cur_dc)
            {
                struct uwp_dc_args *args = calloc( 1, sizeof(*args) );
                if (args)
                {
                    IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                    struct xaml_event_handler *h = (struct xaml_event_handler *)handler;
                    args->vtbl = dc_args_vtbl;
                    args->ref = 1;
                    args->new_val = (IInspectable *)cur_dc;
                    h->lpVtbl->Invoke( h, sender, args );
                    dp_args_Release( args );
                }
                else IInspectable_Release( (IInspectable *)cur_dc );
            }
        }
    }
    return S_OK;
}

/* --- IControl & IControlProtected --- */
static HRESULT WINAPI uwp_ctrl_get_FontSize( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->font_size > 0) ? impl->font_size : 14.0;
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_put_FontSize( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && val > 0) impl->font_size = val;
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_get_IsEnabled( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->is_enabled : TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_put_IsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && impl->is_enabled != val)
    {
        impl->is_enabled = val;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_ApplyTemplate( void *iface, boolean *ret )
{
    if (ret) *ret = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_ctrl_Focus( void *iface, int state, boolean *ret )
{
    if (ret) *ret = TRUE;
    return S_OK;
}

static HRESULT WINAPI uwp_ctrl_prot_GetTemplateChild( void *iface, HSTRING child_name, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *nm = WindowsGetStringRawBuffer( child_name, NULL );
    struct uwp_element *child = NULL;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl || !nm) return S_OK;

    TRACE( "GetTemplateChild(%s) on %s (%s)\n",
           debugstr_w(nm), debugstr_w(impl->class_name), debugstr_w(impl->name) );

    if (!wcscmp( nm, L"TextContainer" ))
    {
        if (!impl->tpl_text_container)
        {
            IInspectable *raw = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.ScrollViewer", NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                IInspectable_Release( raw );
            }
            if (child)
            {
                child->parent = impl;
                child->actual_w = 320.0;
                child->actual_h = 80.0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                impl->tpl_text_container = child;
            }
        }
        child = impl->tpl_text_container;
    }
    else if (!wcscmp( nm, L"NormalOutput" ))
    {
        if (!impl->tpl_normal_output)
        {
            IInspectable *raw = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                IInspectable_Release( raw );
            }
            if (child)
            {
                child->parent = impl->tpl_text_container ? impl->tpl_text_container : impl;
                child->actual_w = 300.0;
                child->actual_h = 64.0;
                child->font_size = 44.0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                if (impl->text[0]) lstrcpynW( child->text, impl->text, ARRAY_SIZE(child->text) );
                impl->tpl_normal_output = child;
                if (impl->tpl_text_container)
                    impl->tpl_text_container->content = &child->IInspectable_iface;
                impl->content = &child->IInspectable_iface;
            }
        }
        child = impl->tpl_normal_output;
    }
    else if (!wcscmp( nm, L"ScrollLeft" ) || !wcscmp( nm, L"ScrollRight" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.HyperlinkButton", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
        if (child)
        {
            child->parent = impl;
            child->visibility = 1; /* Collapsed by default */
            lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
        }
    }
    else if (!wcscmp( nm, L"ContentRootGrid" ) || !wcscmp( nm, L"RootGrid" ) ||
             !wcscmp( nm, L"DialogSpace" ) || !wcscmp( nm, L"CommandSpace" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
        if (child)
        {
            child->parent = impl;
            child->actual_w = 360.0;
            child->actual_h = 180.0;
            lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
        }
    }
    else if (!wcscmp( nm, L"PrimaryButton" ) || !wcscmp( nm, L"SecondaryButton" ) ||
             !wcscmp( nm, L"CloseButton" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Button", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
        if (child)
        {
            child->parent = impl;
            lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
        }
    }
    else
    {
        return uwp_fe_FindName( iface, child_name, out );
    }

    if (child)
    {
        *out = &child->IDependencyObject_iface;
        uwp_sub_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}

/* --- IContentControl / IUserControl / IBorder --- */
static HRESULT WINAPI uwp_cc_get_Content( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->content : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_cc_put_Content( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    WCHAR str_buf[256] = {0};
    struct uwp_element *child_el;

    if (!impl) return S_OK;
    if (impl->content) IInspectable_Release( impl->content );
    impl->content = (IInspectable *)val;
    if (impl->content) IInspectable_AddRef( impl->content );

    if (val && uwp_unbox_string( val, str_buf, ARRAY_SIZE(str_buf) ))
    {
        if (str_buf[0])
        {
            lstrcpynW( impl->text, str_buf, ARRAY_SIZE(impl->text) );
            TRACE( "uwp_cc_put_Content string on %s (%s) -> %s\n",
                   debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_buf) );
        }
    }
    else if ((child_el = uwp_elem_from_any( val )) != NULL)
    {
        child_el->parent = impl;
        g_layout_dirty = TRUE;
        TRACE( "uwp_cc_put_Content element on %s (%s) -> %s (%s)\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name),
               debugstr_w(child_el->class_name), debugstr_w(child_el->name) );
        if (impl->data_context && !child_el->data_context)
            uwp_propagate_data_context( child_el, impl->data_context );
        uwp_fire_loaded_tree( child_el );
    }
    if (impl->island_hwnd) InvalidateRect( impl->island_hwnd, NULL, FALSE );
    uwp_invalidate_window();
    return S_OK;
}

/* --- IButtonBase & IToggleButton --- */
static HRESULT WINAPI uwp_btn_get_Command( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->command : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_btn_put_Command( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->command) IInspectable_Release( impl->command );
    impl->command = (IInspectable *)val;
    if (impl->command) IInspectable_AddRef( impl->command );
    return S_OK;
}
static HRESULT WINAPI uwp_btn_get_CommandParameter( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->command_param : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_btn_put_CommandParameter( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->command_param) IInspectable_Release( impl->command_param );
    impl->command_param = (IInspectable *)val;
    if (impl->command_param) IInspectable_AddRef( impl->command_param );
    return S_OK;
}
static HRESULT WINAPI uwp_btn_add_Click( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->click_handlers, &impl->click_cap, impl->click_cnt + 1, sizeof(*impl->click_handlers), 8 ))
    {
        impl->click_handlers[impl->click_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_tbtn_get_IsChecked( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = box_bool_value( (impl && impl->is_checked == 1) ? TRUE : FALSE );
    return S_OK;
}
static HRESULT WINAPI uwp_tbtn_put_IsChecked( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_checked = val ? 1 : 0;
    return S_OK;
}

/* --- ITextBlock (slot 26=get_Text, slot 27=put_Text) --- */
static HRESULT WINAPI uwp_tb_get_Text( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = impl ? impl->text : L"";
    return WindowsCreateString( s, wcslen(s), out );
}
static HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( val, NULL );
    if (impl)
    {
        if (s && wcsstr( s, L"Ln -" ))
            lstrcpynW( impl->text, L"  Ln 1, Col 1", ARRAY_SIZE(impl->text) );
        else
            lstrcpynW( impl->text, s ? s : L"", ARRAY_SIZE(impl->text) );
        TRACE( "ITextBlock::put_Text on %s (%s) -> %s\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(impl->text) );
        uwp_invalidate_window();
    }
    return S_OK;
}

/* --- IPanel / IGrid / IRowDefinition / IColumnDefinition --- */
struct uwp_vec_view
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable **items;
    unsigned int count;
    int cap;
    struct uwp_element *owner;
};

static HRESULT uwp_vec_create( struct uwp_element *owner, IInspectable **out );

static HRESULT WINAPI uwp_panel_get_Children( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
    *out = impl->children_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_grid_get_RowDefinitions( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->row_defs_coll) uwp_vec_create( impl, &impl->row_defs_coll );
    *out = impl->row_defs_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_grid_get_ColumnDefinitions( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->col_defs_coll) uwp_vec_create( impl, &impl->col_defs_coll );
    *out = impl->col_defs_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

struct uwp_grid_length { double Value; int GridUnitType; };

static HRESULT WINAPI uwp_rowcol_get_Length( void *iface, struct uwp_grid_length *gl )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (gl)
    {
        gl->Value = impl ? impl->grid_len_val : 1.0;
        gl->GridUnitType = impl ? impl->grid_len_type : 2;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_rowcol_put_Length( void *iface, struct uwp_grid_length *gl )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && gl)
    {
        impl->grid_len_val = gl->Value;
        impl->grid_len_type = gl->GridUnitType;
    }
    return S_OK;
}

/* --- Simple UWP Vector (UIElementCollection / RowDefinitionCollection / ColumnDefinitionCollection) --- */
static HRESULT WINAPI uwp_vec_QI( IInspectable *iface, REFIID iid, void **out )
{
    static const GUID IID_IPropertyValue_vec =
        { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };
    static const GUID IID_IReferenceTracker_vec =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IReferenceTrackerTarget_vec =
        { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
    static const GUID IID_IReferenceTrackerTarget_vec2 =
        { 0x64e71ddf, 0x2424, 0x4ec5, { 0x86, 0x27, 0x15, 0x05, 0x08, 0x96, 0xd9, 0xb6 } };
    static const GUID IID_IWeakReferenceSource_vec =
        { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IPropertyValue_vec ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_vec ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_vec ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_vec2 ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource_vec ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    IInspectable_AddRef( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_vec_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Controls.UIElementCollection";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static ULONG WINAPI uwp_vec_AddRef( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    return InterlockedIncrement( &v->ref );
}
static ULONG WINAPI uwp_vec_Release( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    ULONG r = InterlockedDecrement( &v->ref );
    if (!r) v->ref = 1;
    return r;
}
static HRESULT WINAPI uwp_vec_GetAt( void *iface, unsigned int idx, void **item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (!item) return E_POINTER;
    if (idx >= v->count) { *item = NULL; return 0x8000000b; }
    *item = v->items[idx];
    if (*item) IInspectable_AddRef( (IInspectable *)*item );
    return S_OK;
}
static HRESULT WINAPI uwp_vec_get_Size( void *iface, unsigned int *sz )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (sz) *sz = v->count;
    return S_OK;
}
static HRESULT WINAPI uwp_vec_GetView( void *iface, void **view )
{
    if (view) { *view = iface; IInspectable_AddRef( (IInspectable *)iface ); }
    return S_OK;
}
static HRESULT WINAPI uwp_vec_IndexOf( void *iface, void *item, unsigned int *idx, boolean *found )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    unsigned int i;
    struct uwp_element *want = uwp_elem_from_any( item );
    for (i = 0; i < v->count; i++)
    {
        if (v->items[i] == item || (want && uwp_elem_from_any( v->items[i] ) == want))
        {
            if (idx) *idx = i;
            if (found) *found = TRUE;
            return S_OK;
        }
    }
    if (idx) *idx = 0;
    if (found) *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_vec_SetAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (idx >= v->count) return 0x8000000b;
    if (v->items[idx]) IInspectable_Release( v->items[idx] );
    v->items[idx] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    return S_OK;
}
static HRESULT WINAPI uwp_vec_InsertAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    struct uwp_element *child_el;
    if (idx > v->count) return S_OK;
    if (!xaml_vec_grow( (void **)&v->items, &v->cap, (int)v->count + 1, sizeof(*v->items), 16 ))
        return E_OUTOFMEMORY;
    memmove( &v->items[idx + 1], &v->items[idx], (v->count - idx) * sizeof(void *) );
    v->items[idx] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    v->count++;
    if ((child_el = uwp_elem_from_any( item )) != NULL && v->owner)
    {
        if (child_el->kind == UWP_KIND_ACCELERATOR || wcsstr( child_el->class_name, L"Accelerator" ))
        {
            child_el->accel_target = v->owner;
            uwp_accel_register( child_el, v->owner, child_el->accel_key, child_el->accel_mods, child_el->accel_enabled );
        }
        else
        {
            child_el->parent = v->owner;
        }
    }
    g_layout_dirty = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_vec_RemoveAt( void *iface, unsigned int idx )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (idx >= v->count) return S_OK;
    if (v->items[idx]) IInspectable_Release( v->items[idx] );
    memmove( &v->items[idx], &v->items[idx + 1], (v->count - idx - 1) * sizeof(void *) );
    v->count--;
    g_layout_dirty = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_vec_Append( void *iface, void *item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    return uwp_vec_InsertAt( iface, v->count, item );
}
static HRESULT WINAPI uwp_vec_RemoveAtEnd( void *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (v->count > 0) return uwp_vec_RemoveAt( iface, v->count - 1 );
    return S_OK;
}
static HRESULT WINAPI uwp_vec_Clear( void *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    unsigned int i;
    for (i = 0; i < v->count; i++)
        if (v->items[i]) IInspectable_Release( v->items[i] );
    v->count = 0;
    return S_OK;
}

static const void *uwp_vec_vtbl[32] =
{
    uwp_vec_QI,
    uwp_vec_AddRef,
    uwp_vec_Release,
    uwp_sub_GetIids,
    uwp_vec_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_vec_GetAt,
    uwp_vec_get_Size,
    uwp_vec_GetView,
    uwp_vec_IndexOf,
    uwp_vec_SetAt,
    uwp_vec_InsertAt,
    uwp_vec_RemoveAt,
    uwp_vec_Append,
    uwp_vec_RemoveAtEnd,
    uwp_vec_Clear,
    uwp_generic_noop,
    uwp_generic_noop,
};

static HRESULT uwp_vec_create( struct uwp_element *owner, IInspectable **out )
{
    struct uwp_vec_view *v = calloc( 1, sizeof(*v) );
    if (!v) return E_OUTOFMEMORY;
    v->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_vec_vtbl;
    v->ref = 1;
    v->owner = owner;
    *out = &v->IInspectable_iface;
    return S_OK;
}

static HRESULT WINAPI uwp_el_obsvec_add( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}
static HRESULT WINAPI uwp_el_obsvec_rem( void *iface, EventRegistrationToken tok )
{
    return S_OK;
}
static const void *uwp_el_obsvec_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_obsvec_add,
    uwp_el_obsvec_rem,
    [8 ... 15] = uwp_generic_noop,
};

static struct uwp_vec_view *uwp_el_ensure_vec( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return NULL;
    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
    return impl->children_coll ? CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
}
static HRESULT WINAPI uwp_el_vec_GetAt( void *iface, unsigned int idx, void **item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    if (!v) { if (item) *item = NULL; return E_FAIL; }
    return uwp_vec_GetAt( &v->IInspectable_iface, idx, item );
}
static HRESULT WINAPI uwp_el_vec_get_Size( void *iface, unsigned int *sz )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    if (sz) *sz = v ? v->count : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_el_vec_GetView( void *iface, void **view )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!view) return E_POINTER;
    if (!impl) { *view = NULL; return E_FAIL; }
    *view = &impl->IVectorView_iface;
    IInspectable_AddRef( (IInspectable *)*view );
    return S_OK;
}
static HRESULT WINAPI uwp_el_vec_IndexOf( void *iface, void *item, unsigned int *idx, boolean *found )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    if (!v) { if (idx) *idx = 0; if (found) *found = FALSE; return S_OK; }
    return uwp_vec_IndexOf( &v->IInspectable_iface, item, idx, found );
}
static HRESULT WINAPI uwp_el_vec_SetAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    return v ? uwp_vec_SetAt( &v->IInspectable_iface, idx, item ) : S_OK;
}
static HRESULT WINAPI uwp_el_vec_InsertAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    return v ? uwp_vec_InsertAt( &v->IInspectable_iface, idx, item ) : S_OK;
}
static HRESULT WINAPI uwp_el_vec_RemoveAt( void *iface, unsigned int idx )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    return v ? uwp_vec_RemoveAt( &v->IInspectable_iface, idx ) : S_OK;
}
static HRESULT WINAPI uwp_el_vec_Append( void *iface, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    return v ? uwp_vec_Append( &v->IInspectable_iface, item ) : S_OK;
}
static HRESULT WINAPI uwp_el_vec_RemoveAtEnd( void *iface )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    return v ? uwp_vec_RemoveAtEnd( &v->IInspectable_iface ) : S_OK;
}
static HRESULT WINAPI uwp_el_vec_Clear( void *iface )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    return v ? uwp_vec_Clear( &v->IInspectable_iface ) : S_OK;
}
static HRESULT WINAPI uwp_el_vec_GetMany( void *iface, unsigned int start, unsigned int cap, void **items, unsigned int *actual )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int i = 0;
    if (v && start < v->count)
    {
        for (i = 0; i < cap && start + i < v->count; i++)
        {
            items[i] = v->items[start + i];
            if (items[i]) IInspectable_AddRef( (IInspectable *)items[i] );
        }
    }
    if (actual) *actual = i;
    return S_OK;
}
static HRESULT WINAPI uwp_el_vec_ReplaceAll( void *iface, unsigned int cnt, void **items )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int i;
    if (!v) return S_OK;
    uwp_vec_Clear( &v->IInspectable_iface );
    for (i = 0; i < cnt; i++)
        uwp_vec_Append( &v->IInspectable_iface, items[i] );
    return S_OK;
}
static const void *uwp_el_vec_vtbl[24] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_vec_GetAt,
    uwp_el_vec_get_Size,
    uwp_el_vec_GetView,
    uwp_el_vec_IndexOf,
    uwp_el_vec_SetAt,
    uwp_el_vec_InsertAt,
    uwp_el_vec_RemoveAt,
    uwp_el_vec_Append,
    uwp_el_vec_RemoveAtEnd,
    uwp_el_vec_Clear,
    uwp_el_vec_GetMany,
    uwp_el_vec_ReplaceAll,
    [18 ... 23] = uwp_generic_noop,
};
static const void *uwp_el_vecview_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_vec_GetAt,
    uwp_el_vec_get_Size,
    uwp_el_vec_IndexOf,
    uwp_el_vec_GetMany,
    [10 ... 15] = uwp_generic_noop,
};
struct uwp_el_iter
{
    const void *vtbl;
    LONG ref;
    struct uwp_vec_view *v;
    unsigned int idx;
};
static HRESULT WINAPI uwp_el_iter_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IIterator_DO =
        { 0x9247b89e, 0xd40f, 0x54c3, { 0xa7, 0xfe, 0x14, 0xfe, 0xe9, 0x37, 0x61, 0xc5 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_DO ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_el_iter *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI uwp_el_iter_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Xaml.DependencyObject>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static ULONG WINAPI uwp_el_iter_AddRef( void *iface ) { return InterlockedIncrement( &((struct uwp_el_iter *)iface)->ref ); }
static ULONG WINAPI uwp_el_iter_Release( void *iface )
{
    struct uwp_el_iter *it = iface;
    ULONG r = InterlockedDecrement( &it->ref );
    if (!r) { if (it->v) IInspectable_Release( &it->v->IInspectable_iface ); free( it ); }
    return r;
}
static HRESULT WINAPI uwp_el_iter_get_Current( void *iface, void **out )
{
    struct uwp_el_iter *it = iface;
    if (!out) return E_POINTER;
    if (!it->v || it->idx >= it->v->count) { *out = NULL; return 0x8000000b; }
    *out = it->v->items[it->idx];
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_el_iter_get_HasCurrent( void *iface, boolean *has )
{
    struct uwp_el_iter *it = iface;
    if (has) *has = (it->v && it->idx < it->v->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_el_iter_MoveNext( void *iface, boolean *has )
{
    struct uwp_el_iter *it = iface;
    if (it->v && it->idx < it->v->count) it->idx++;
    if (has) *has = (it->v && it->idx < it->v->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_el_iter_GetMany( void *iface, unsigned int cap, void **items, unsigned int *actual )
{
    struct uwp_el_iter *it = iface;
    unsigned int i = 0;
    if (it->v)
    {
        while (i < cap && it->idx < it->v->count)
        {
            items[i] = it->v->items[it->idx++];
            if (items[i]) IInspectable_AddRef( (IInspectable *)items[i] );
            i++;
        }
    }
    if (actual) *actual = i;
    return S_OK;
}
static const void *uwp_el_iter_vtbl[16] =
{
    uwp_el_iter_QI,
    uwp_el_iter_AddRef,
    uwp_el_iter_Release,
    uwp_sub_GetIids,
    uwp_el_iter_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_iter_get_Current,
    uwp_el_iter_get_HasCurrent,
    uwp_el_iter_MoveNext,
    uwp_el_iter_GetMany,
    [10 ... 15] = uwp_generic_noop,
};
static HRESULT WINAPI uwp_el_iterable_First( void *iface, void **out )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    struct uwp_el_iter *it;
    if (!out) return E_POINTER;
    it = calloc( 1, sizeof(*it) );
    if (!it) return E_OUTOFMEMORY;
    it->vtbl = uwp_el_iter_vtbl;
    it->ref = 1;
    it->v = v;
    if (v) IInspectable_AddRef( &v->IInspectable_iface );
    *out = it;
    return S_OK;
}
static const void *uwp_el_iterable_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_el_iterable_First,
    [7 ... 15] = uwp_generic_noop,
};

/* --- Sub-interface Vtables --- */
static const void *uwp_nondelegating_vtbl[16] =
{
    uwp_nondelegating_QueryInterface,
    uwp_nondelegating_AddRef,
    uwp_nondelegating_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
};

static const void *uwp_do_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_do_GetValue,              /* 6: GetValue */
    uwp_do_SetValue,              /* 7: SetValue */
    uwp_do_ClearValue,            /* 8: ClearValue */
    uwp_do_GetValue,              /* 9: ReadLocalValue */
    uwp_do_GetValue,              /* 10: GetAnimationBaseValue */
    uwp_do_get_Dispatcher,       /* 11: get_Dispatcher */
    uwp_do_get_DispatcherQueue,  /* 12: get_DispatcherQueue */
    [13 ... 15] = uwp_generic_noop,
};

static const void *uwp_do2_vtbl[16] =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_do2_RegisterPropertyChangedCallback,   /* 6 */
    uwp_do2_UnregisterPropertyChangedCallback, /* 7 */
    [8 ... 15] = uwp_generic_noop,
};

static IDependencyProperty *g_routed_event_ptr_pressed = NULL;

static HRESULT WINAPI uwp_ui_AddHandler( void *iface, void *routed_event, void *handler, boolean handled_too )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    (void)handled_too;
    if (!impl || !handler) return S_OK;
    if (g_routed_event_ptr_pressed && routed_event == (void *)g_routed_event_ptr_pressed)
    {
        void *pv = NULL;
        void *del = handler;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)handler, &IID_IPropertyValue_g, &pv )) && pv)
        {
            void *unwrapped = NULL;
            HRESULT (WINAPI *get_val)(void *, void **) = (void *)(*(void ***)handler)[6];
            if (get_val && SUCCEEDED(get_val( handler, &unwrapped )) && unwrapped)
                del = unwrapped;
            xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
            if (unwrapped && unwrapped != handler) IUnknown_Release( (IUnknown *)unwrapped );
            IUnknown_Release( (IUnknown *)pv );
        }
        else
        {
            xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
        }
        TRACE( "IUIElement::AddHandler(PointerPressed) on %s (%s) -> del %p\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), del );
    }
    return S_OK;
}

static const void *uwp_ui_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 127] = uwp_generic_noop,
    [6] = uwp_ui_get_DesiredSize,
    [19] = uwp_ui_get_IsHitTestVisible,
    [20] = uwp_ui_put_IsHitTestVisible,
    [21] = uwp_ui_get_Visibility,
    [22] = uwp_ui_put_Visibility,
    [23] = uwp_ui_get_RenderSize,
    [41 ... 85] = uwp_ui_event_add,
    [57] = uwp_ui_add_PointerPressed,
    [58] = uwp_ui_remove_PointerPressed,
    [59] = uwp_ui_add_PointerMoved,
    [60] = uwp_ui_remove_PointerMoved,
    [61] = uwp_ui_add_PointerReleased,
    [62] = uwp_ui_remove_PointerReleased,
    [63] = uwp_ui_add_PointerEntered,
    [64] = uwp_ui_remove_PointerEntered,
    [65] = uwp_ui_add_PointerExited,
    [66] = uwp_ui_remove_PointerExited,
    [67] = uwp_ui_add_PointerCaptureLost,
    [68] = uwp_ui_remove_PointerCaptureLost,
    [69] = uwp_ui_add_PointerCanceled,
    [70] = uwp_ui_remove_PointerCanceled,
    [71] = uwp_ui_add_PointerWheelChanged,
    [72] = uwp_ui_remove_PointerWheelChanged,
    [93] = uwp_ui_CapturePointer,
    [94] = uwp_ui_ReleasePointerCapture,
    [95] = uwp_ui_ReleasePointerCaptures,
    [96] = uwp_ui_AddHandler,
};

static HRESULT WINAPI uwp_fe_get_FlowDirection( void *iface, int *dir )
{
    if (dir) *dir = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_fe_put_FlowDirection( void *iface, int dir )
{
    return S_OK;
}
static HRESULT WINAPI uwp_fe_get_BaseUri( void *iface, void **uri )
{
    if (uri) *uri = NULL;
    return S_OK;
}

static const void *uwp_fe_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 63] = uwp_generic_noop,
    [7] = uwp_fe_get_Resources,
    [8] = uwp_fe_put_Resources,
    [9] = uwp_fe_get_Tag,
    [10] = uwp_fe_put_Tag,
    [13] = uwp_fe_get_ActualWidth,
    [14] = uwp_fe_get_ActualHeight,
    [15] = uwp_fe_get_Width,
    [16] = uwp_fe_put_Width,
    [17] = uwp_fe_get_Height,
    [18] = uwp_fe_put_Height,
    [19] = uwp_fe_get_MinWidth,
    [20] = uwp_fe_put_MinWidth,
    [21] = uwp_fe_get_MaxWidth,
    [22] = uwp_fe_put_MaxWidth,
    [23] = uwp_fe_get_MinHeight,
    [24] = uwp_fe_put_MinHeight,
    [25] = uwp_fe_get_MaxHeight,
    [26] = uwp_fe_put_MaxHeight,
    [31] = uwp_fe_get_Margin,
    [32] = uwp_fe_put_Margin,
    [33] = uwp_fe_get_Name,
    [34] = uwp_fe_put_Name,
    [35] = uwp_fe_get_BaseUri,
    [36] = uwp_fe_get_DataContext,
    [37] = uwp_fe_put_DataContext,
    [40] = uwp_fe_get_Parent,
    [41] = uwp_fe_get_FlowDirection,
    [42] = uwp_fe_put_FlowDirection,
    [43] = uwp_fe_add_Loaded,
    [45] = uwp_ui_event_add,
    [47] = uwp_ui_event_add,
    [49] = uwp_ui_event_add,
    [51] = uwp_fe_FindName,
};

static const void *uwp_fe3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe3_add_DataContextChanged,
    [7 ... 15] = uwp_generic_noop,
};

static const void *uwp_ctrl_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 63] = uwp_generic_noop,
    [6] = uwp_ctrl_get_FontSize,
    [7] = uwp_ctrl_put_FontSize,
    [22] = uwp_ctrl_get_IsEnabled,
    [23] = uwp_ctrl_put_IsEnabled,
    [44] = uwp_ctrl_ApplyTemplate,
    [46] = uwp_ctrl_Focus,
};

static const void *uwp_ctrl_prot_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
    [8] = uwp_ctrl_prot_GetTemplateChild,
};

static HRESULT WINAPI uwp_flyout_get_Items( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    if (!impl) { *out = NULL; return E_NOINTERFACE; }
    *out = &impl->IVector_iface;
    uwp_sub_AddRef( &impl->IVector_iface );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw)
        lstrcpynW( impl->text, raw, ARRAY_SIZE(impl->text) );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_slot6( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return E_NOINTERFACE;
    /* Only MenuFlyout / MenuBar / TabView use slot 6 for get_Items; IFlyoutBase::ShowAt passes a UIElement* */
    if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.Flyout" ) ||
        !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.CommandBarFlyout" ))
        return S_OK;
    return uwp_flyout_get_Items( iface, out );
}

static HRESULT WINAPI uwp_flyout_slot7( void *iface, HSTRING val )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    /* IFlyoutBase::Hide() takes no args; only MenuBarItem / MenuFlyoutSubItem use slot 7 for put_Title/put_Text */
    if (wcsstr( impl->class_name, L"MenuBarItem" ) == NULL &&
        wcsstr( impl->class_name, L"MenuFlyoutSubItem" ) == NULL &&
        wcsstr( impl->class_name, L"MenuFlyoutItem" ) == NULL)
        return S_OK;
    return uwp_flyout_put_Text( iface, val );
}

static HRESULT WINAPI uwp_flyout_slot8( void *iface, void *arg1, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    /* IFlyoutBase::add_Opened(handler, token*) on Flyout / MenuFlyout / CommandBarFlyout */
    if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.Flyout" ) ||
        !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.MenuFlyout" ) ||
        !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.CommandBarFlyout" ))
    {
        if (tok) tok->value = 1;
        return S_OK;
    }
    if (wcsstr( impl->class_name, L"MenuBarItem" ) != NULL ||
        wcsstr( impl->class_name, L"MenuFlyoutSubItem" ) != NULL)
        return uwp_flyout_get_Items( iface, (void **)arg1 );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_get_SelectedIndex( void *iface, int *out )
{
    if (out) *out = 0;
    return S_OK;
}

static HRESULT WINAPI uwp_sel_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI uwp_sel_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI uwp_sel_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI uwp_sel_args_get_Items( void *iface, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static const void *uwp_sel_args_vtbl[16] =
{
    [0] = uwp_sel_args_QI,
    [1] = uwp_sel_args_AddRef,
    [2] = uwp_sel_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_sel_args_get_Items,
    [7] = uwp_sel_args_get_Items,
    [8 ... 15] = uwp_generic_noop,
};
static struct { const void *vtbl; } g_sel_changed_args = { uwp_sel_args_vtbl };

static HRESULT WINAPI uwp_tabview_get_SelectedItem( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->selected_item : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_put_SelectedItem( void *iface, void *item )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (!impl) return S_OK;
    if (impl->selected_item) IInspectable_Release( impl->selected_item );
    impl->selected_item = (IInspectable *)item;
    if (impl->selected_item) IInspectable_AddRef( impl->selected_item );
    TRACE( "ITabView::put_SelectedItem on %s (%s) -> %p (handler=%p)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), item, impl->sel_changed_handler );
    if (impl->sel_changed_handler)
    {
        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
        struct xaml_event_handler *h = (struct xaml_event_handler *)impl->sel_changed_handler;
        h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
    }
    uwp_invalidate_window();
    return S_OK;
}

static HRESULT WINAPI uwp_tabview_add_SelectionChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IFlyout_iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (impl->sel_changed_handler) IInspectable_Release( impl->sel_changed_handler );
    impl->sel_changed_handler = (IInspectable *)handler;
    IInspectable_AddRef( impl->sel_changed_handler );
    TRACE( "ITabView::add_SelectionChanged on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->flyout_closed_handlers, &impl->flyout_closed_cap,
                       impl->flyout_closed_cnt + 1, sizeof(*impl->flyout_closed_handlers), 4 ))
    {
        impl->flyout_closed_handlers[impl->flyout_closed_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->flyout_closed_cnt;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_flyout_add_Opening( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->flyout_opening_handlers, &impl->flyout_opening_cap,
                       impl->flyout_opening_cnt + 1, sizeof(*impl->flyout_opening_handlers), 4 ))
    {
        impl->flyout_opening_handlers[impl->flyout_opening_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->flyout_opening_cnt;
    }
    return S_OK;
}

static const void *uwp_flyout_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 127] = uwp_generic_noop,
    [6] = uwp_flyout_slot6,
    [7] = uwp_flyout_slot7,
    [8] = uwp_flyout_slot8,
    [10] = uwp_flyout_add_Closed,
    [12] = uwp_flyout_add_Opening,
    [34] = uwp_flyout_get_Items,
    [45] = uwp_flyout_get_SelectedIndex,
    [47] = uwp_tabview_get_SelectedItem,
    [48] = uwp_tabview_put_SelectedItem,
    [51] = uwp_tabview_add_SelectionChanged,
};

static HRESULT WINAPI uwp_xamlroot_get_Content( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IStateTrigger_iface );
    if (!out) return E_POINTER;
    *out = &impl->IUIElement_iface;
    uwp_sub_AddRef( &impl->IUIElement_iface );
    return S_OK;
}

static HRESULT WINAPI uwp_xamlroot_get_XamlRoot( void *iface, void **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_element, IStateTrigger_iface );
    if (!out) return E_POINTER;
    *out = &impl->IStateTrigger_iface;
    uwp_sub_AddRef( &impl->IStateTrigger_iface );
    return S_OK;
}

static const void *uwp_xamlroot_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 127] = uwp_generic_noop,
    [6] = uwp_xamlroot_get_Content,
    [8] = uwp_xamlroot_get_XamlRoot,
};

static const void *uwp_noop_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 127] = uwp_generic_noop,
};

static const void *uwp_cc_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 127] = uwp_generic_noop,
    [6] = uwp_cc_get_Content,
    [7] = uwp_cc_put_Content,
};

static const void *uwp_border_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [16] = uwp_cc_get_Content,
    [17] = uwp_cc_put_Content,
};

static const void *uwp_btn_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [10] = uwp_btn_get_Command,
    [11] = uwp_btn_put_Command,
    [12] = uwp_btn_get_CommandParameter,
    [13] = uwp_btn_put_CommandParameter,
    [14] = uwp_btn_add_Click,
};

static const void *uwp_tbtn_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = uwp_tbtn_get_IsChecked,
    [7] = uwp_tbtn_put_IsChecked,
    [10] = uwp_ui_event_add,
    [12] = uwp_ui_event_add,
    [14] = uwp_ui_event_add,
};

static const void *uwp_panel_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = uwp_panel_get_Children,
};

/* --- IUIElement7, IKeyboardAccelerator, IXamlUICommand, ICommand, IMenuFlyoutItem, ITeachingTip --- */
static HRESULT WINAPI uwp_ui7_get_KeyboardAccelerators( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->accelerators_coll) uwp_vec_create( impl, &impl->accelerators_coll );
    *out = impl->accelerators_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ui7_add_PreviewKeyDown( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) { if (tok) tok->value = 0; return E_INVALIDARG; }
    return xaml_event_registry_add( &impl->preview_keydown_reg, handler, tok );
}
static HRESULT WINAPI uwp_ui7_remove_PreviewKeyDown( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_INVALIDARG;
    return xaml_event_registry_remove( &impl->preview_keydown_reg, tok );
}
static const void *uwp_ui7_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = uwp_ui7_get_KeyboardAccelerators,
    [11] = uwp_ui7_add_PreviewKeyDown,
    [12] = uwp_ui7_remove_PreviewKeyDown,
};

struct uwp_key_routed_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    UINT vk;
    boolean handled;
    struct { UINT32 RepeatCount; UINT32 ScanCode; boolean IsExtendedKey; boolean IsMenuKeyDown; boolean WasKeyDown; boolean IsKeyReleased; } status;
};
static HRESULT WINAPI uwp_key_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IKeyRoutedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_key_routed_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_key_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_key_routed_args *)iface)->ref );
}
static ULONG WINAPI uwp_key_args_Release( void *iface )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI uwp_key_args_get_Key( void *iface, UINT *out )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    if (out) *out = a->vk;
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_get_KeyStatus( void *iface, void *out )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    if (out) memcpy( out, &a->status, sizeof(a->status) );
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_get_Handled( void *iface, boolean *out )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    if (out) *out = a->handled;
    return S_OK;
}
static HRESULT WINAPI uwp_key_args_put_Handled( void *iface, boolean val )
{
    struct uwp_key_routed_args *a = (struct uwp_key_routed_args *)iface;
    a->handled = val ? TRUE : FALSE;
    return S_OK;
}
static const void *uwp_key_args_vtbl[16] =
{
    [0] = uwp_key_args_QI,
    [1] = uwp_key_args_AddRef,
    [2] = uwp_key_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_key_args_get_Key,
    [7] = uwp_key_args_get_KeyStatus,
    [8] = uwp_key_args_get_Handled,
    [9] = uwp_key_args_put_Handled,
    [10 ... 15] = uwp_generic_noop,
};

struct uwp_ka_invoked_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    boolean handled;
    IInspectable *element;
};
static HRESULT WINAPI uwp_ka_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IKeyboardAcceleratorInvokedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_ka_invoked_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_ka_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_ka_invoked_args *)iface)->ref );
}
static ULONG WINAPI uwp_ka_args_Release( void *iface )
{
    struct uwp_ka_invoked_args *a = (struct uwp_ka_invoked_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->element) IInspectable_Release( a->element );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_ka_args_get_Handled( void *iface, boolean *out )
{
    if (out) *out = ((struct uwp_ka_invoked_args *)iface)->handled;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_args_put_Handled( void *iface, boolean val )
{
    ((struct uwp_ka_invoked_args *)iface)->handled = val ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_args_get_Element( void *iface, void **out )
{
    struct uwp_ka_invoked_args *a = (struct uwp_ka_invoked_args *)iface;
    if (!out) return E_POINTER;
    *out = a->element;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static const void *uwp_ka_args_vtbl[16] =
{
    [0] = uwp_ka_args_QI,
    [1] = uwp_ka_args_AddRef,
    [2] = uwp_ka_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ka_args_get_Handled,
    [7] = uwp_ka_args_put_Handled,
    [8] = uwp_ka_args_get_Element,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ka_get_Key( void *iface, UINT *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->accel_key : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_Key( void *iface, UINT val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_key = val;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_Modifiers( void *iface, UINT *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->accel_mods : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_Modifiers( void *iface, UINT val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_mods = val;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_IsEnabled( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? (impl->accel_enabled ? TRUE : FALSE) : TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_IsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->accel_enabled = val ? TRUE : FALSE;
        uwp_accel_register( impl, impl->accel_target, impl->accel_key, impl->accel_mods, impl->accel_enabled );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_ka_get_ScopeOwner( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->accel_scope_owner : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ka_put_ScopeOwner( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->accel_scope_owner) IInspectable_Release( impl->accel_scope_owner );
    impl->accel_scope_owner = (IInspectable *)val;
    if (impl->accel_scope_owner) IInspectable_AddRef( impl->accel_scope_owner );
    return S_OK;
}
static HRESULT WINAPI uwp_ka_add_Invoked( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->accel_invoked_handlers, &impl->accel_invoked_cap,
                       impl->accel_invoked_cnt + 1, sizeof(*impl->accel_invoked_handlers), 4 ))
    {
        impl->accel_invoked_handlers[impl->accel_invoked_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->accel_invoked_cnt;
    }
    return S_OK;
}
static const void *uwp_ka_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = uwp_ka_get_Key,
    [7] = uwp_ka_put_Key,
    [8] = uwp_ka_get_Modifiers,
    [9] = uwp_ka_put_Modifiers,
    [10] = uwp_ka_get_IsEnabled,
    [11] = uwp_ka_put_IsEnabled,
    [12] = uwp_ka_get_ScopeOwner,
    [13] = uwp_ka_put_ScopeOwner,
    [14] = uwp_ka_add_Invoked,
};

struct uwp_exec_req_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *param;
};
static HRESULT WINAPI uwp_exec_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IExecuteRequestedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_exec_req_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_exec_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_exec_req_args *)iface)->ref );
}
static ULONG WINAPI uwp_exec_args_Release( void *iface )
{
    struct uwp_exec_req_args *a = (struct uwp_exec_req_args *)iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->param) IInspectable_Release( a->param );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_exec_args_get_Parameter( void *iface, void **out )
{
    struct uwp_exec_req_args *a = (struct uwp_exec_req_args *)iface;
    if (!out) return E_POINTER;
    *out = a->param;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static const void *uwp_exec_args_vtbl[16] =
{
    [0] = uwp_exec_args_QI,
    [1] = uwp_exec_args_AddRef,
    [2] = uwp_exec_args_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_exec_args_get_Parameter,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_xaml_cmd_add_ExecuteRequested( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->exec_req_handlers, &impl->exec_req_cap,
                       impl->exec_req_cnt + 1, sizeof(*impl->exec_req_handlers), 4 ))
    {
        impl->exec_req_handlers[impl->exec_req_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->exec_req_cnt;
    }
    return S_OK;
}
static const void *uwp_xaml_cmd_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [10] = uwp_ui7_get_KeyboardAccelerators,
    [15] = uwp_btn_get_Command,
    [16] = uwp_btn_put_Command,
    [17] = uwp_xaml_cmd_add_ExecuteRequested,
};

static HRESULT WINAPI uwp_icmd_CanExecute( void *iface, void *param, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_icmd_Execute( void *iface, void *param )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_exec_req_args *args;
    IInspectable *sender;
    int i;
    if (!impl) return S_OK;
    if (impl->command)
    {
        void *sub_cmd = NULL;
        if ((SUCCEEDED(IInspectable_QueryInterface( impl->command, &IID_ICommand_UWP, &sub_cmd )) ||
             SUCCEEDED(IInspectable_QueryInterface( impl->command, &IID_ICommand_UWP2, &sub_cmd ))) && sub_cmd)
        {
            struct command_iface *cmd = (struct command_iface *)sub_cmd;
            if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, param );
            IUnknown_Release( (IUnknown *)sub_cmd );
        }
    }
    args = calloc( 1, sizeof(*args) );
    if (!args) return E_OUTOFMEMORY;
    args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_exec_args_vtbl;
    args->ref = 1;
    args->param = (IInspectable *)param;
    if (args->param) IInspectable_AddRef( args->param );
    sender = impl->outer ? impl->outer : &impl->IXamlUICommand_iface;
    for (i = 0; i < impl->exec_req_cnt; i++)
    {
        if (impl->exec_req_handlers[i])
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->exec_req_handlers[i];
            h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
        }
    }
    uwp_exec_args_Release( &args->IInspectable_iface );
    return S_OK;
}
static const void *uwp_icmd_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
    [8] = uwp_icmd_CanExecute,
    [9] = uwp_icmd_Execute,
};

static HRESULT WINAPI uwp_mfi_get_Text( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->text : L"", impl ? wcslen(impl->text) : 0, out );
}
static HRESULT WINAPI uwp_mfi_put_Text( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw) lstrcpynW( impl->text, raw, ARRAY_SIZE(impl->text) );
    return S_OK;
}
static const void *uwp_mfi_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = uwp_mfi_get_Text,
    [7] = uwp_mfi_put_Text,
    [8] = uwp_btn_get_Command,
    [9] = uwp_btn_put_Command,
    [10] = uwp_btn_get_CommandParameter,
    [11] = uwp_btn_put_CommandParameter,
    [12] = uwp_btn_add_Click,
};

static HRESULT WINAPI uwp_mfi3_get_AccelText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->accel_text : L"", impl ? wcslen(impl->accel_text) : 0, out );
}
static HRESULT WINAPI uwp_mfi3_put_AccelText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw;
    if ((ULONG_PTR)val < 0x10000) return S_OK;
    raw = WindowsGetStringRawBuffer( val, NULL );
    if (impl && raw) lstrcpynW( impl->accel_text, raw, ARRAY_SIZE(impl->accel_text) );
    return S_OK;
}
static const void *uwp_mfi3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mfi3_get_AccelText,
    [7] = uwp_mfi3_put_AccelText,
    [8 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tmfi_get_IsChecked( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->is_checked == 1) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_tmfi_put_IsChecked( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->is_checked = val ? 1 : 0;
    return S_OK;
}
static const void *uwp_tmfi_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tmfi_get_IsChecked,
    [7] = uwp_tmfi_put_IsChecked,
    [8 ... 15] = uwp_generic_noop,
};

static const void *uwp_mfsi_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_panel_get_Children,
    [7] = uwp_mfi_get_Text,
    [8] = uwp_mfi_put_Text,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ui4_get_ContextFlyout( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->context_flyout : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_ui4_put_ContextFlyout( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->context_flyout) IInspectable_Release( impl->context_flyout );
    impl->context_flyout = (IInspectable *)val;
    if (impl->context_flyout) IInspectable_AddRef( impl->context_flyout );
    return S_OK;
}
static const void *uwp_ui4_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ui4_get_ContextFlyout,
    [7] = uwp_ui4_put_ContextFlyout,
    [8 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mf2_ShowAt( void *iface, void *target, UINT64 pt_raw )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *target_el = uwp_elem_from_any( target );
    float fx = 0.0f, fy = 0.0f;
    HWND owner = NULL;
    POINT spt;
    UINT dpi;
    int i;

    if (!impl) return S_OK;
    memcpy( &fx, &pt_raw, sizeof(float) );
    memcpy( &fy, (const char *)&pt_raw + sizeof(float), sizeof(float) );

    if (target_el && target_el->island_hwnd)
        owner = GetAncestor( target_el->island_hwnd, GA_ROOT );
    if (!owner) owner = GetActiveWindow();
    if (!owner) owner = GetForegroundWindow();
    if (!owner && g_island_hwnd_cnt > 0 && g_island_hwnds[0])
        owner = GetAncestor( g_island_hwnds[0], GA_ROOT );

    dpi = owner ? GetDpiForWindow( owner ) : 96;
    if (!dpi) dpi = 96;
    spt.x = MulDiv( (int)(fx + 0.5f), dpi, 96 );
    spt.y = MulDiv( (int)(fy + 0.5f), dpi, 96 );
    if (owner && IsWindow( owner ))
        ClientToScreen( owner, &spt );
    else
        GetCursorPos( &spt );

    TRACE( "IMenuFlyout2::ShowAt(%s, pt=%.1f,%.1f -> screen=%ld,%ld, owner=%p)\n",
           debugstr_w(impl->class_name), fx, fy, spt.x, spt.y, owner );

    for (i = 0; i < impl->flyout_opening_cnt; i++)
    {
        if (impl->flyout_opening_handlers[i])
        {
            IInspectable *sender = impl->outer ? impl->outer : &impl->IFlyout_iface;
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->flyout_opening_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, NULL ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }

    uwp_show_flyout_popup( impl, owner, spt.x, spt.y );
    return S_OK;
}

static HRESULT WINAPI uwp_mf2_ShowAtWithOptions( void *iface, void *target, void *opts )
{
    POINT pt;
    UINT64 raw = 0;
    float fx, fy;
    HWND owner = GetActiveWindow();
    (void)opts;
    GetCursorPos( &pt );
    if (owner && IsWindow( owner )) ScreenToClient( owner, &pt );
    fx = (float)pt.x;
    fy = (float)pt.y;
    memcpy( &raw, &fx, sizeof(float) );
    memcpy( (char *)&raw + sizeof(float), &fy, sizeof(float) );
    return uwp_mf2_ShowAt( iface, target, raw );
}

static const void *uwp_mf2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mf2_ShowAt,
    [7] = uwp_mf2_ShowAtWithOptions,
    [8 ... 15] = uwp_generic_noop,
};

/* --- IContentDialog, IContentDialog2, IContentDialogClosedEventArgs --- */
struct uwp_cd_closed_args
{
    const void *vtbl;
    LONG ref;
    int result;
};

static HRESULT WINAPI cd_closed_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IContentDialogClosedEventArgs_UWP ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_cd_closed_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cd_closed_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_cd_closed_args *)iface)->ref );
}
static ULONG WINAPI cd_closed_args_Release( void *iface )
{
    struct uwp_cd_closed_args *a = iface;
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI cd_closed_args_get_Result( void *iface, int *out )
{
    struct uwp_cd_closed_args *a = iface;
    if (out) *out = a ? a->result : 0;
    return S_OK;
}
static const void *uwp_cd_closed_args_vtbl[8] =
{
    cd_closed_args_QI,
    cd_closed_args_AddRef,
    cd_closed_args_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    cd_closed_args_get_Result,
    uwp_generic_noop,
};

static void uwp_cd_fire_closed( struct uwp_element *impl, int result )
{
    struct uwp_cd_closed_args *args;
    IInspectable *sender;
    int i;
    if (!impl) return;
    if (impl->dialog_hwnd && IsWindow( impl->dialog_hwnd ))
    {
        HWND dh = impl->dialog_hwnd;
        impl->dialog_hwnd = NULL;
        DestroyWindow( dh );
    }
    args = calloc( 1, sizeof(*args) );
    if (!args) return;
    args->vtbl = uwp_cd_closed_args_vtbl;
    args->ref = 1;
    args->result = result;
    sender = impl->outer ? impl->outer : &impl->IContentDialog_iface;
    for (i = 0; i < impl->flyout_closed_cnt; i++)
    {
        if (impl->flyout_closed_handlers[i])
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)impl->flyout_closed_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, args ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }
    cd_closed_args_Release( args );
    if (g_island_hwnd_cnt > 0 && g_island_hwnds[0])
    {
        HWND root = GetAncestor( g_island_hwnds[0], GA_ROOT );
        if (root && IsWindow( root ))
        {
            RedrawWindow( root, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
            PostMessageW( root, WM_NULL, 0, 0 );
        }
    }
    uwp_invalidate_window();
}

static LRESULT CALLBACK uwp_cd_popup_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct uwp_element *impl = (struct uwp_element *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_APP + 1)
    {
        if (IsWindow( hwnd ) && IsWindowVisible( hwnd ))
            SetFocus( hwnd );
        return 0;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && impl)
    {
        if (wp == VK_ESCAPE)
        {
            uwp_cd_fire_closed( impl, 0 );
            return 0;
        }
        if (wp == VK_RETURN)
        {
            int res = (impl->dialog_default_btn == 1) ? 1 : ((impl->dialog_default_btn == 2) ? 2 : 0);
            uwp_cd_fire_closed( impl, res );
            return 0;
        }
    }
    if (msg == WM_LBUTTONUP && impl)
    {
        RECT rc;
        int mx = (short)LOWORD( lp ), my = (short)HIWORD( lp );
        GetClientRect( hwnd, &rc );
        if (my >= rc.bottom - 52 && my <= rc.bottom - 14)
        {
            int btn_w = (rc.right - rc.left - 36) / 3;
            if (mx >= rc.left + 12 && mx < rc.left + 12 + btn_w && impl->dialog_primary_text[0])
                uwp_cd_fire_closed( impl, 1 );
            else if (mx >= rc.left + 18 + btn_w && mx < rc.left + 18 + 2 * btn_w && impl->dialog_secondary_text[0])
                uwp_cd_fire_closed( impl, 2 );
            else if (mx >= rc.left + 24 + 2 * btn_w)
                uwp_cd_fire_closed( impl, 0 );
        }
        return 0;
    }
    if (msg == WM_PAINT && impl)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        if (hdc)
        {
            RECT rc, trc, brc;
            HBRUSH bg_br = CreateSolidBrush( RGB( 43, 43, 43 ) );
            HBRUSH bd_br = CreateSolidBrush( RGB( 80, 80, 80 ) );
            HBRUSH p_br  = CreateSolidBrush( RGB( 0, 120, 212 ) );
            HBRUSH s_br  = CreateSolidBrush( RGB( 60, 60, 60 ) );
            HFONT f_title = CreateFontW( -18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HFONT f_body  = CreateFontW( -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_f = SelectObject( hdc, f_title );
            int btn_w;

            GetClientRect( hwnd, &rc );
            FillRect( hdc, &rc, bg_br );
            FrameRect( hdc, &rc, bd_br );
            SetBkMode( hdc, TRANSPARENT );
            SetTextColor( hdc, RGB( 255, 255, 255 ) );

            SetRect( &trc, 20, 16, rc.right - 20, 46 );
            DrawTextW( hdc, impl->dialog_title[0] ? impl->dialog_title : L"Notepad", -1, &trc,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE );

            SelectObject( hdc, f_body );
            SetRect( &brc, 20, 52, rc.right - 20, rc.bottom - 62 );
            DrawTextW( hdc, impl->text, -1, &brc, DT_LEFT | DT_WORDBREAK );

            btn_w = (rc.right - rc.left - 36) / 3;
            if (impl->dialog_primary_text[0])
            {
                RECT r1 = { 12, rc.bottom - 46, 12 + btn_w, rc.bottom - 14 };
                FillRect( hdc, &r1, p_br );
                DrawTextW( hdc, impl->dialog_primary_text, -1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }
            if (impl->dialog_secondary_text[0])
            {
                RECT r2 = { 18 + btn_w, rc.bottom - 46, 18 + 2 * btn_w, rc.bottom - 14 };
                FillRect( hdc, &r2, s_br );
                DrawTextW( hdc, impl->dialog_secondary_text, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }
            {
                RECT r3 = { 24 + 2 * btn_w, rc.bottom - 46, rc.right - 12, rc.bottom - 14 };
                FillRect( hdc, &r3, s_br );
                DrawTextW( hdc, impl->dialog_close_text[0] ? impl->dialog_close_text : L"Cancel", -1, &r3,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE );
            }

            SelectObject( hdc, old_f );
            DeleteObject( f_body );
            DeleteObject( f_title );
            DeleteObject( s_br );
            DeleteObject( p_br );
            DeleteObject( bd_br );
            DeleteObject( bg_br );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HRESULT WINAPI uwp_cd_get_Title( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->dialog_title_obj : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_put_Title( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->dialog_title_obj) IInspectable_Release( impl->dialog_title_obj );
    impl->dialog_title_obj = val;
    if (val)
    {
        IInspectable_AddRef( val );
        uwp_unbox_string( val, impl->dialog_title, ARRAY_SIZE(impl->dialog_title) );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_NullOut( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_BoolFalse( void *iface, boolean *out )
{
    if (out) *out = FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_BoolTrue( void *iface, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_PrimaryButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_primary_text : L"",
                                impl ? wcslen(impl->dialog_primary_text) : 0, out );
}
static HRESULT WINAPI uwp_cd_put_PrimaryButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_primary_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_primary_text) );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_get_SecondaryButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_secondary_text : L"",
                                impl ? wcslen(impl->dialog_secondary_text) : 0, out );
}
static HRESULT WINAPI uwp_cd_put_SecondaryButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_secondary_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_secondary_text) );
    return S_OK;
}
static HRESULT WINAPI uwp_cd2_get_CloseButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_close_text : L"",
                                impl ? wcslen(impl->dialog_close_text) : 0, out );
}
static HRESULT WINAPI uwp_cd2_put_CloseButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_close_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_close_text) );
    return S_OK;
}
static HRESULT WINAPI uwp_cd2_get_DefaultButton( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->dialog_default_btn : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_cd2_put_DefaultButton( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->dialog_default_btn = val;
    return S_OK;
}
static HRESULT WINAPI uwp_cd_Hide( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) uwp_cd_fire_closed( impl, 0 );
    return S_OK;
}
static HRESULT WINAPI uwp_cd_ShowAsync( void *iface, void **out )
{
    static BOOL cls_reg = FALSE;
    struct uwp_element *impl = uwp_elem_from_any( iface );
    HWND owner = NULL;
    RECT orc = { 100, 100, 900, 700 };
    int dw = 380, dh = 180, dx, dy;

    if (out) *out = NULL;
    if (!impl) return S_OK;

    uwp_fire_loaded_tree( impl );

    if (g_island_hwnd_cnt > 0 && g_island_hwnds[0])
        owner = GetAncestor( g_island_hwnds[0], GA_ROOT );
    if (!owner) owner = GetActiveWindow();
    if (owner && IsWindow( owner ))
        GetWindowRect( owner, &orc );
    dx = (orc.left + orc.right - dw) / 2;
    dy = (orc.top + orc.bottom - dh) / 2;

    if (!cls_reg)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = uwp_cd_popup_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        wc.lpszClassName = L"WineUwpContentDialogPopup";
        RegisterClassExW( &wc );
        cls_reg = TRUE;
    }
    if (impl->dialog_hwnd && IsWindow( impl->dialog_hwnd ))
        DestroyWindow( impl->dialog_hwnd );
    impl->dialog_hwnd = CreateWindowExW( WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                         L"WineUwpContentDialogPopup", L"",
                                         WS_POPUP | WS_VISIBLE,
                                         dx, dy, dw, dh,
                                         owner, NULL, GetModuleHandleW( NULL ), impl );
    if (impl->dialog_hwnd)
    {
        InvalidateRect( impl->dialog_hwnd, NULL, FALSE );
        UpdateWindow( impl->dialog_hwnd );
        PostMessageW( impl->dialog_hwnd, WM_APP + 1, 0, 0 );
    }
    return S_OK;
}

static const void *uwp_cd_vtbl[48] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 47] = uwp_generic_noop,
    [6] = uwp_cd_get_Title,
    [7] = uwp_cd_put_Title,
    [8] = uwp_cd_get_NullOut,
    [10] = uwp_cd_get_BoolFalse,
    [12] = uwp_cd_get_PrimaryButtonText,
    [13] = uwp_cd_put_PrimaryButtonText,
    [14] = uwp_cd_get_SecondaryButtonText,
    [15] = uwp_cd_put_SecondaryButtonText,
    [16] = uwp_cd_get_NullOut,
    [18] = uwp_cd_get_NullOut,
    [20] = uwp_cd_get_NullOut,
    [22] = uwp_cd_get_NullOut,
    [24] = uwp_cd_get_BoolTrue,
    [26] = uwp_cd_get_BoolTrue,
    [28] = uwp_ui_event_add,
    [30] = uwp_flyout_add_Closed,
    [32] = uwp_ui_event_add,
    [34] = uwp_ui_event_add,
    [36] = uwp_ui_event_add,
    [38] = uwp_cd_Hide,
    [39] = uwp_cd_ShowAsync,
};

static const void *uwp_cd2_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 23] = uwp_generic_noop,
    [6] = uwp_cd2_get_CloseButtonText,
    [7] = uwp_cd2_put_CloseButtonText,
    [8] = uwp_cd_get_NullOut,
    [10] = uwp_cd_get_NullOut,
    [12] = uwp_cd_get_BoolTrue,
    [14] = uwp_ui_event_add,
    [16] = uwp_cd_get_NullOut,
    [18] = uwp_cd2_get_DefaultButton,
    [19] = uwp_cd2_put_DefaultButton,
};

static HRESULT WINAPI uwp_tt_get_IsOpen( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->is_open) ? TRUE : FALSE;
    return S_OK;
}
static BOOL uwp_has_open_teachingtip( struct uwp_element *el )
{
    if (!el) return FALSE;
    if (wcsstr( el->class_name, L"TeachingTip" ) && el->is_open) return TRUE;
    if (el->content && uwp_has_open_teachingtip( uwp_elem_from_any( el->content ) )) return TRUE;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
            if (uwp_has_open_teachingtip( uwp_elem_from_any( children->items[k] ) )) return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI uwp_tt_put_IsOpen( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    BOOL prev;
    if (!impl) return S_OK;
    prev = impl->is_open;
    impl->is_open = val ? TRUE : FALSE;
    g_layout_dirty = TRUE;
    TRACE( "ITeachingTip::put_IsOpen on %s (%s) -> %d (prev %d)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), impl->is_open, prev );
    if (impl->is_open)
    {
        int ii;
        for (ii = 0; ii < g_island_hwnd_cnt; ii++)
        {
            HWND ih = g_island_hwnds[ii];
            struct uwp_element *isl = ih ? (struct uwp_element *)GetWindowLongPtrW( ih, GWLP_USERDATA ) : NULL;
            struct uwp_element *root_el = (isl && isl->content) ? uwp_elem_from_any( isl->content ) : NULL;
            if (root_el && (root_el == impl || uwp_elem_is_descendant( root_el, impl )))
                SetWindowPos( ih, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW );
        }
    }
    if (prev && !impl->is_open)
    {
        IInspectable *sender = impl->outer ? impl->outer : &impl->ITeachingTip_iface;
        int i;
        for (i = 0; i < impl->tip_closing_cnt; i++)
        {
            if (impl->tip_closing_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closing_handlers[i];
                h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
            }
        }
        for (i = 0; i < impl->tip_closed_cnt; i++)
        {
            if (impl->tip_closed_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)impl->tip_closed_handlers[i];
                h->lpVtbl->Invoke( h, sender, &g_sel_changed_args );
            }
        }
    }
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_tt_add_Closing( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tip_closing_handlers, &impl->tip_closing_cap,
                       impl->tip_closing_cnt + 1, sizeof(*impl->tip_closing_handlers), 4 ))
    {
        impl->tip_closing_handlers[impl->tip_closing_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tip_closing_cnt;
    }
    return S_OK;
}
static HRESULT WINAPI uwp_tt_add_Closed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->tip_closed_handlers, &impl->tip_closed_cap,
                       impl->tip_closed_cnt + 1, sizeof(*impl->tip_closed_handlers), 4 ))
    {
        impl->tip_closed_handlers[impl->tip_closed_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->tip_closed_cnt;
    }
    return S_OK;
}
static const void *uwp_teachingtip_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 63] = uwp_generic_noop,
    [10] = uwp_tt_get_IsOpen,
    [11] = uwp_tt_put_IsOpen,
    [51] = uwp_tt_add_Closing,
    [53] = uwp_tt_add_Closed,
};

static const void *uwp_grid_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = uwp_grid_get_RowDefinitions,
    [7] = uwp_grid_get_ColumnDefinitions,
};

static const void *uwp_tb_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 63] = uwp_generic_noop,
    [6] = uwp_ctrl_get_FontSize,
    [7] = uwp_ctrl_put_FontSize,
    [26] = uwp_tb_get_Text,
    [27] = uwp_tb_put_Text,
};

static const void *uwp_rowcol_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = uwp_rowcol_get_Length,
    [7] = uwp_rowcol_put_Length,
};

static HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result );
static HRESULT WINAPI uwp_frame_Navigate( void *iface, void *type_name_ptr, boolean *result )
{
    return uwp_frame_NavigateWithParam( iface, type_name_ptr, NULL, result );
}

static const void *uwp_frame_vtbl[48] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 47] = uwp_generic_noop,
    [6] = uwp_cc_get_Content,
    [7] = uwp_cc_put_Content,
    [12] = uwp_ui_event_add,
    [14] = uwp_ui_event_add,
    [16] = uwp_ui_event_add,
    [18] = uwp_ui_event_add,
    [23] = uwp_frame_Navigate,
    [24] = uwp_frame_NavigateWithParam,
};

static const void *uwp_sv_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 63] = uwp_generic_noop,
    [50] = uwp_ui_event_add, /* add_ViewChanged */
    [51] = uwp_generic_noop, /* remove_ViewChanged */
};

static enum uwp_elem_kind classify_uwp_class( const WCHAR *cls )
{
    if (!cls) return UWP_KIND_GENERIC;
    if (wcsstr( cls, L"Frame" ) && !wcsstr( cls, L"Framework" )) return UWP_KIND_FRAME;
    if (wcsstr( cls, L"Page" )) return UWP_KIND_PAGE;
    if (wcsstr( cls, L"RowDefinition" )) return UWP_KIND_ROW_DEF;
    if (wcsstr( cls, L"ColumnDefinition" )) return UWP_KIND_COL_DEF;
    if (wcsstr( cls, L"StackPanel" )) return UWP_KIND_STACK_PANEL;
    if (!wcscmp( cls, L"Windows.UI.Xaml.Controls.Image" ) || !wcscmp( cls, L"Image" )) return UWP_KIND_IMAGE;
    if (wcsstr( cls, L"ProgressRing" )) return UWP_KIND_PROGRESS_RING;
    if (wcsstr( cls, L"Grid" ) || wcsstr( cls, L"Panel" ) || wcsstr( cls, L"Canvas" )) return UWP_KIND_GRID;
    if (wcsstr( cls, L"Border" )) return UWP_KIND_BORDER;
    if (wcsstr( cls, L"RadioButtons" )) return UWP_KIND_CONTENT_CONTROL;
    if (wcsstr( cls, L"RadioButton" )) return UWP_KIND_TOGGLE_BUTTON;
    if (wcsstr( cls, L"ToggleButton" )) return UWP_KIND_TOGGLE_BUTTON;
    if (wcsstr( cls, L"Button" )) return UWP_KIND_BUTTON;
    if (wcsstr( cls, L"TextBlock" ) || wcsstr( cls, L"CalculationResult" )) return UWP_KIND_TEXT_BLOCK;
    if (wcsstr( cls, L"ScrollViewer" )) return UWP_KIND_SCROLL_VIEWER;
    if (wcsstr( cls, L"UserControl" ) || wcsstr( cls, L"Calculator" ) ||
        wcsstr( cls, L"NumberPad" ) || wcsstr( cls, L"TitleBar" ) ||
        wcsstr( cls, L"Operators" )) return UWP_KIND_USER_CONTROL;
    if (wcsstr( cls, L"ContentControl" ) || wcsstr( cls, L"NavigationView" ) ||
        wcsstr( cls, L"Pivot" ) || wcsstr( cls, L"Viewbox" ) ||
        wcsstr( cls, L"DesktopWindowXamlSource" )) return UWP_KIND_CONTENT_CONTROL;
    if (wcsstr( cls, L"ResourceDictionary" ) || !wcscmp( cls, L"Microsoft.UI.Xaml.Controls.XamlControlsResources" ))
        return UWP_KIND_RESOURCE_DICT;
    if (!wcscmp( cls, L"Windows.UI.Color" )) return UWP_KIND_COLOR;
    if (wcsstr( cls, L"Style" ) || wcsstr( cls, L"Setter" ) || wcsstr( cls, L"VisualState" )) return UWP_KIND_STYLE;
    if (wcsstr( cls, L"Brush" )) return UWP_KIND_BRUSH;
    if (wcsstr( cls, L"Flyout" )) return UWP_KIND_FLYOUT;
    if (wcsstr( cls, L"Popup" )) return UWP_KIND_POPUP;
    if (wcsstr( cls, L"Accelerator" )) return UWP_KIND_ACCELERATOR;
    if (wcsstr( cls, L"XamlUICommand" )) return UWP_KIND_XAML_COMMAND;
    return UWP_KIND_GENERIC;
}

static HRESULT WINAPI uwp_brush_get_Opacity( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->brush_opacity : 1.0;
    return S_OK;
}
static HRESULT WINAPI uwp_brush_put_Opacity( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->brush_opacity = val;
    return S_OK;
}
static const void *uwp_brush_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_brush_get_Opacity,
    [7] = uwp_brush_put_Opacity,
    [8 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_scb_get_Color( void *iface, Color *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    UINT32 argb = (impl && impl->has_brush_argb) ? impl->brush_argb : 0;
    if (!out) return E_POINTER;
    out->A = (BYTE)((argb >> 24) & 0xff);
    out->R = (BYTE)((argb >> 16) & 0xff);
    out->G = (BYTE)((argb >> 8) & 0xff);
    out->B = (BYTE)(argb & 0xff);
    return S_OK;
}
static HRESULT WINAPI uwp_scb_put_Color( void *iface, Color val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->brush_argb = ((UINT32)val.A << 24) | ((UINT32)val.R << 16) | ((UINT32)val.G << 8) | val.B;
        impl->has_brush_argb = TRUE;
    }
    return S_OK;
}
static const void *uwp_scb_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_scb_get_Color,
    [7] = uwp_scb_put_Color,
    [8 ... 15] = uwp_generic_noop,
};

void xaml_layout_render( HWND hwnd, HDC hdc, int width, int height, IInspectable *content );
BOOL uwp_handle_mouse_input( HWND hwnd, UINT msg, int x, int y, int width, int height, IInspectable *root_content );

static LRESULT CALLBACK uwp_island_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct uwp_element *impl = (struct uwp_element *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_WINDOWPOSCHANGING && lp && impl && impl->content)
    {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        if (!(pos->flags & SWP_NOZORDER) && pos->hwndInsertAfter == HWND_BOTTOM &&
            uwp_has_open_teachingtip( uwp_elem_from_any( impl->content ) ))
            pos->hwndInsertAfter = HWND_TOP;
    }
    if (msg == WM_SIZE)
    {
        InvalidateRect( hwnd, NULL, FALSE );
        return 0;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        if (hdc && impl && impl->content)
        {
            RECT rc;
            GetClientRect( hwnd, &rc );
            xaml_layout_render( hwnd, hdc, rc.right - rc.left, rc.bottom - rc.top, impl->content );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    if (msg == WM_MOUSELEAVE)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            g_hovered_elem = NULL;
            InvalidateRect( hwnd, NULL, FALSE );
        }
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_MOUSEMOVE) && impl && impl->content)
    {
        RECT rc;
        GetClientRect( hwnd, &rc );
        if (msg == WM_MOUSEMOVE)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent( &tme );
        }
        if (msg == WM_LBUTTONDOWN)
            TRACE( "uwp_island_wndproc: LBUTTONDOWN hwnd=%p (%d,%d) size=%dx%d\n",
                   hwnd, (short)LOWORD(lp), (short)HIWORD(lp),
                   (int)(rc.right - rc.left), (int)(rc.bottom - rc.top) );
        uwp_handle_mouse_input( hwnd, msg, (short)LOWORD(lp), (short)HIWORD(lp),
                                rc.right - rc.left, rc.bottom - rc.top, impl->content );
        return 0;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_ESCAPE && uwp_popup_handle_escape())
        return 0;
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && impl && impl->content)
    {
        struct uwp_element *root_el = uwp_elem_from_any( impl->content );
        if (uwp_dispatch_keydown( hwnd, (UINT)wp, lp, root_el ))
        {
            InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return 0;
        }
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HRESULT WINAPI uwp_dwxs_AttachToWindow( void *iface, HWND parent_wnd )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    static BOOL cls_reg = FALSE;
    if (!impl) return E_NOINTERFACE;
    if (!cls_reg)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = uwp_island_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        wc.lpszClassName = L"DesktopWindowXamlSourceWindow";
        RegisterClassExW( &wc );
        cls_reg = TRUE;
    }
    if (!impl->island_hwnd && parent_wnd)
    {
        impl->island_hwnd = CreateWindowExW( 0, L"DesktopWindowXamlSourceWindow", L"",
                                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                             0, 0, 800, 40, parent_wnd, NULL, GetModuleHandleW( NULL ), impl );
        if (impl->island_hwnd && g_island_hwnd_cnt < (int)ARRAY_SIZE(g_island_hwnds))
            g_island_hwnds[g_island_hwnd_cnt++] = impl->island_hwnd;
        TRACE( "uwp_dwxs_AttachToWindow: parent=%p -> island_hwnd=%p\n", parent_wnd, impl->island_hwnd );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_dwxs_get_WindowHandle( void *iface, HWND *out_hwnd )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out_hwnd) return E_POINTER;
    *out_hwnd = impl ? impl->island_hwnd : NULL;
    return S_OK;
}

static BOOL uwp_is_any_island_hwnd( HWND hwnd )
{
    int i;
    if (!hwnd) return FALSE;
    for (i = 0; i < g_island_hwnd_cnt; i++)
    {
        if (g_island_hwnds[i] == hwnd || (g_island_hwnds[i] && IsChild( g_island_hwnds[i], hwnd )))
            return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI uwp_dwxs_PreTranslateMessage( void *iface, const MSG *msg, BOOL *result )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    BOOL handled = FALSE;

    if (msg && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) &&
        msg->wParam == VK_ESCAPE && uwp_popup_handle_escape())
    {
        if (result) *result = TRUE;
        return S_OK;
    }

    if (msg && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) && impl && impl->content)
    {
        if (!msg->hwnd || msg->hwnd == impl->island_hwnd ||
            (impl->island_hwnd && IsChild( impl->island_hwnd, msg->hwnd )) ||
            !uwp_is_any_island_hwnd( msg->hwnd ))
        {
            struct uwp_element *root_el = uwp_elem_from_any( impl->content );
            handled = uwp_dispatch_keydown( msg->hwnd ? msg->hwnd : impl->island_hwnd,
                                            (UINT)msg->wParam, msg->lParam, root_el );
            if (handled)
            {
                if (impl->island_hwnd) InvalidateRect( impl->island_hwnd, NULL, FALSE );
                uwp_invalidate_window();
            }
        }
    }
    if (result) *result = handled;
    return S_OK;
}

static const void *uwp_dwxs_native_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_dwxs_AttachToWindow,
    [4] = uwp_dwxs_get_WindowHandle,
    [5] = uwp_dwxs_PreTranslateMessage,
    [6 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_timer_add_Tick( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}

static const void *uwp_timer_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
    [9] = uwp_timer_add_Tick,
};

HRESULT uwp_element_create( const WCHAR *class_name, IInspectable *outer, IInspectable **inner, void **instance )
{
    struct uwp_element *impl;
    if (!instance) return E_POINTER;
    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->nondelegating_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nondelegating_vtbl;
    impl->IInspectable_iface.lpVtbl = (class_name && !wcscmp( class_name, L"Windows.UI.Xaml.DispatcherTimer" ))
        ? (const struct IInspectableVtbl *)uwp_timer_vtbl
        : (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IDependencyObject_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do_vtbl;
    impl->IDependencyObject2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do2_vtbl;
    impl->IUIElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui_vtbl;
    impl->IFrameworkElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IFrameworkElement3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe3_vtbl;
    impl->IControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_vtbl;
    impl->IControlProtected_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_prot_vtbl;
    impl->IContentControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IUserControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IPage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IFrame_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame_vtbl;
    impl->IButtonBase_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_btn_vtbl;
    impl->IToggleButton_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tbtn_vtbl;
    impl->IPanel_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_panel_vtbl;
    impl->IGrid_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid_vtbl;
    impl->IBorder_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_border_vtbl;
    impl->ITextBlock_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tb_vtbl;
    impl->IRowDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IColumnDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IStyle_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_noop_vtbl;
    impl->IBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_brush_vtbl;
    impl->ISolidColorBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scb_vtbl;
    impl->IFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_flyout_vtbl;
    impl->IStateTrigger_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_xamlroot_vtbl;
    impl->IObservableVector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_obsvec_vtbl;
    impl->IVector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_vec_vtbl;
    impl->IIterable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_iterable_vtbl;
    impl->IVectorView_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_el_vecview_vtbl;
    impl->IScrollViewer_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_sv_vtbl;
    impl->IDesktopWindowXamlSourceNative_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dwxs_native_vtbl;
    impl->IUIElement7_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui7_vtbl;
    impl->IKeyboardAccelerator_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ka_vtbl;
    impl->IXamlUICommand_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_xaml_cmd_vtbl;
    impl->ICommand_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_icmd_vtbl;
    impl->IMenuFlyoutItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfi_vtbl;
    impl->IMenuFlyoutItem3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfi3_vtbl;
    impl->IToggleMenuFlyoutItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tmfi_vtbl;
    impl->IMenuFlyoutSubItem_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mfsi_vtbl;
    impl->IMenuFlyout2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mf2_vtbl;
    impl->IUIElement4_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui4_vtbl;
    impl->IContentDialog_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd_vtbl;
    impl->IContentDialog2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd2_vtbl;
    impl->ITeachingTip_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_teachingtip_vtbl;
    impl->IWeakReferenceSource_iface.lpVtbl = &uwp_wrs_vtbl;

    impl->ref = 1;
    impl->outer = outer;
    impl->accel_enabled = TRUE;
    lstrcpynW( impl->class_name, class_name ? class_name : L"Windows.UI.Xaml.FrameworkElement", ARRAY_SIZE(impl->class_name) );
    impl->kind = classify_uwp_class( impl->class_name );
    if (impl->kind == UWP_KIND_PAGE) g_last_created_page = impl;
    impl->grid_row_span = 1;
    impl->grid_col_span = 1;
    impl->grid_len_type = 2; /* Star */
    impl->grid_len_val = 1.0;
    impl->is_enabled = TRUE;
    impl->is_hit_test_visible = TRUE;
    impl->opacity = 1.0;
    impl->brush_opacity = 1.0;
    impl->h_align = 3; /* Stretch */
    impl->v_align = 3; /* Stretch */
    impl->font_size = 16.0;
    g_layout_dirty = TRUE;
    if (impl->kind != UWP_KIND_BRUSH && impl->kind != UWP_KIND_STYLE &&
        impl->kind != UWP_KIND_RESOURCE_DICT && impl->kind != UWP_KIND_COLOR &&
        impl->kind != UWP_KIND_ROW_DEF && impl->kind != UWP_KIND_COL_DEF &&
        impl->kind != UWP_KIND_DEFERRED && impl->kind != UWP_KIND_ACCELERATOR &&
        impl->kind != UWP_KIND_XAML_COMMAND)
        xaml_create_dcomp_visual( &impl->visual );

    register_uwp_elem( impl );

    if (inner)
    {
        *inner = &impl->nondelegating_iface;
        if (outer && !xaml_get_app_profile()->seed_xbf)
            IInspectable_AddRef( outer );
        else
            InterlockedIncrement( &impl->ref );
    }
    if (impl->kind == UWP_KIND_FRAME)
        *instance = &impl->IFrame_iface;
    else if (impl->kind == UWP_KIND_GRID || impl->kind == UWP_KIND_STACK_PANEL)
        *instance = &impl->IGrid_iface;
    else if (impl->kind == UWP_KIND_BORDER)
        *instance = &impl->IBorder_iface;
    else if (impl->kind == UWP_KIND_BUTTON)
        *instance = &impl->IButtonBase_iface;
    else if (impl->kind == UWP_KIND_TOGGLE_BUTTON)
        *instance = &impl->IToggleButton_iface;
    else if (impl->kind == UWP_KIND_TEXT_BLOCK)
        *instance = &impl->ITextBlock_iface;
    else if (impl->kind == UWP_KIND_ROW_DEF)
        *instance = &impl->IRowDefinition_iface;
    else if (impl->kind == UWP_KIND_COL_DEF)
        *instance = &impl->IColumnDefinition_iface;
    else if (impl->kind == UWP_KIND_CONTENT_CONTROL || impl->kind == UWP_KIND_USER_CONTROL || impl->kind == UWP_KIND_PAGE)
        *instance = &impl->IContentControl_iface;
    else if (impl->kind == UWP_KIND_ACCELERATOR || wcsstr( impl->class_name, L"Accelerator" ))
        *instance = &impl->IKeyboardAccelerator_iface;
    else if (impl->kind == UWP_KIND_XAML_COMMAND || wcsstr( impl->class_name, L"XamlUICommand" ))
        *instance = &impl->IXamlUICommand_iface;
    else if (wcsstr( impl->class_name, L"FlyoutItem" ))
        *instance = &impl->IMenuFlyoutItem_iface;
    else if (wcsstr( impl->class_name, L"ContentDialog" ))
        *instance = &impl->IContentDialog_iface;
    else if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.DispatcherTimer" ))
        *instance = &impl->IInspectable_iface;
    else
        *instance = &impl->IFrameworkElement_iface;

    return S_OK;
}

/* ============================================================================
 * 5. DataContext Propagation, Loaded & OnApplyTemplate Dispatch, Frame Navigate
 * ============================================================================ */

static void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc )
{
    int i;
    if (!impl) return;
    if (impl->data_context != dc)
    {
        if (impl->data_context) IInspectable_Release( impl->data_context );
        impl->data_context = dc;
        if (dc) IInspectable_AddRef( dc );
    }

    if (dc)
    {
        void *npc = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( dc, &IID_INotifyPropertyChanged_UWP, &npc )) && npc)
        {
            IUnknown_Release( (IUnknown *)npc );
            uwp_calc_vm_set_instance( dc, NULL );
        }
        else if (uwp_is_display_result_element( impl ))
        {
            uwp_calc_vm_set_instance( dc, NULL );
        }
    }
    else if (!xaml_get_app_profile()->teardown_vm && g_calc_vm.vm)
    {
        uwp_calc_vm_teardown( FALSE );
    }

    for (i = 0; i < impl->dc_cnt; i++)
    {
        if (impl->dc_handlers[i])
        {
            struct uwp_dc_args *args = calloc( 1, sizeof(*args) );
            if (args)
            {
                IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                struct xaml_event_handler *h = (struct xaml_event_handler *)impl->dc_handlers[i];
                args->vtbl = dc_args_vtbl;
                args->ref = 1;
                args->new_val = dc;
                if (dc) IInspectable_AddRef( dc );
                h->lpVtbl->Invoke( h, sender, args );
                dp_args_Release( args );
            }
        }
    }

    if (impl->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( impl->content );
        if (ch && !ch->data_context) uwp_propagate_data_context( ch, dc );
    }
    if (impl->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && !ch->data_context) uwp_propagate_data_context( ch, dc );
        }
    }
}

static struct { const void *vtbl; LONG ref; } g_dp_routed_args = { (const void *)dp_changed_args_vtbl, 1 };
/* Loaded/Click/Navigation callbacks receive this: McgMarshal ComInterfaceToObject
 * calls GetRuntimeClassName on it, so it must be an element face naming
 * RoutedEventArgs. */
static struct uwp_element *g_routed_args_el = NULL;
static void *g_routed_args(void)
{
    if (!g_routed_args_el)
    {
        void *inst = NULL;
        uwp_element_create( L"Windows.UI.Xaml.RoutedEventArgs", NULL, NULL, &inst );
        g_routed_args_el = uwp_elem_from_any( inst );
    }
    if (g_routed_args_el && g_routed_args_el->outer) return g_routed_args_el->outer;
    if (g_routed_args_el) return &g_routed_args_el->IInspectable_iface;
    return &g_dp_routed_args;
}
#define g_dummy_routed_args g_routed_args()

static void uwp_apply_template_if_needed( struct uwp_element *el )
{
    void *fe_ovr = NULL;
    if (!el || el->template_applied || !el->outer) return;
    el->template_applied = TRUE;
    if (el->parent && !wcsncmp( el->parent->class_name, L"Microsoft.UI.Xaml.", 18 ))
        return;
    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IFrameworkElementOverrides_UWP, &fe_ovr )) && fe_ovr)
    {
        /* Slot 8 on IFrameworkElementOverrides is OnApplyTemplate() */
        HRESULT (WINAPI *on_apply)(void *) = (void *)(*(void ***)fe_ovr)[8];
        if (on_apply)
        {
            TRACE( "Calling IFrameworkElementOverrides::OnApplyTemplate on %s (%s)\n",
                   debugstr_w(el->class_name), debugstr_w(el->name) );
            on_apply( fe_ovr );
        }
        IUnknown_Release( (IUnknown *)fe_ovr );
    }
    if (xaml_get_app_profile()->display_sync && uwp_is_display_result_element( el ))
    {
        uwp_calc_vm_ensure();
        uwp_sync_calc_display_from_vm();
    }
}

static void uwp_fire_loaded_tree( struct uwp_element *el )
{
    int i;
    if (!el) return;

    uwp_apply_template_if_needed( el );

    if (!el->loaded_fired && el->dc_cnt > 0 && !el->data_context)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        for (i = 0; i < el->dc_cnt; i++)
        {
            if (el->dc_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->dc_handlers[i];
                TRACE( "Firing Loading handler[%d] on %s (%s)\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name) );
                h->lpVtbl->Invoke( h, sender, NULL );
            }
        }
    }

    if (!el->loaded_fired && el->loaded_cnt > 0)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        el->loaded_fired = TRUE;
        for (i = 0; i < el->loaded_cnt; i++)
        {
            if (el->loaded_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->loaded_handlers[i];
                TRACE( "Firing Loaded handler[%d] on %s (%s)\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name) );
                h->lpVtbl->Invoke( h, sender, g_routed_args() );
            }
        }
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_fire_loaded_tree( ch );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el) uwp_fire_loaded_tree( ch );
        }
    }
}

static struct uwp_element *uwp_find_named( struct uwp_element *el, const WCHAR *want )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!el || !want) return NULL;
    if (el->name[0] && !wcscmp( el->name, want )) return el;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *r;
        if (ch && ch != el && (r = uwp_find_named( ch, want ))) return r;
    }
    if (el->children_coll)
    {
        v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            struct uwp_element *r;
            if (ch && ch != el && (r = uwp_find_named( ch, want ))) return r;
        }
    }
    return NULL;
}

static void uwp_seed_synthetic_calculator( struct uwp_element *holder );
static void uwp_realize_deferred_entry( struct uwp_defer_entry *de );
static void uwp_vsm_invalidate_eval_chain( struct uwp_element *el );
static void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created );

static BOOL uwp_panel_has_concrete_child( struct uwp_element *panel )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!panel) return FALSE;
    if (panel->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( panel->content );
        if (ch && ch->kind != UWP_KIND_DEFERRED && ch->visibility == 0) return TRUE;
    }
    if (!panel->children_coll) return FALSE;
    v = CONTAINING_RECORD( panel->children_coll, struct uwp_vec_view, IInspectable_iface );
    for (k = 0; k < v->count; k++)
    {
        struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
        if (ch && ch->kind != UWP_KIND_DEFERRED && ch->visibility == 0) return TRUE;
    }
    return FALSE;
}

static void uwp_realize_empty_panel_defers( struct uwp_element *root )
{
    int d;
    if (!root) return;
    for (d = 0; d < root->defer_cnt; d++)
    {
        struct uwp_defer_entry *de = &root->defer_entries[d];
        if (!de->realized && de->parent_panel &&
            (de->parent_panel->kind == UWP_KIND_GRID ||
             de->parent_panel->kind == UWP_KIND_STACK_PANEL ||
             de->parent_panel->kind == UWP_KIND_BORDER) &&
            !uwp_panel_has_concrete_child( de->parent_panel ))
        {
            uwp_realize_deferred_entry( de );
        }
    }
}

static void uwp_record_xbf_ref( struct uwp_element *root, const WCHAR *full_name,
                                const WCHAR *short_name, BOOL is_resource_path )
{
    int i;
    if (!root || !full_name || !full_name[0] || !short_name || !short_name[0]) return;
    for (i = 0; i < root->xbf_ref_cnt; i++)
    {
        if (!wcscmp( root->xbf_refs[i].full_name, full_name )) return;
    }
    if (!xaml_vec_grow( (void **)&root->xbf_refs, &root->xbf_ref_cap, root->xbf_ref_cnt + 1, sizeof(*root->xbf_refs), 32 ))
        return;
    lstrcpynW( root->xbf_refs[root->xbf_ref_cnt].full_name, full_name,
               ARRAY_SIZE(root->xbf_refs[0].full_name) );
    lstrcpynW( root->xbf_refs[root->xbf_ref_cnt].short_name, short_name,
               ARRAY_SIZE(root->xbf_refs[0].short_name) );
    root->xbf_refs[root->xbf_ref_cnt].is_resource_path = is_resource_path;
    root->xbf_refs[root->xbf_ref_cnt].instantiated = FALSE;
    root->xbf_ref_cnt++;
}

static void uwp_mark_xbf_ref_instantiated( struct uwp_element *root, const WCHAR *cls_name )
{
    int i;
    if (!root || !cls_name || !cls_name[0]) return;
    for (i = 0; i < root->xbf_ref_cnt; i++)
    {
        if (!wcscmp( root->xbf_refs[i].full_name, cls_name ))
            root->xbf_refs[i].instantiated = TRUE;
    }
}

static BOOL uwp_tree_has_class( struct uwp_element *el, const WCHAR *full_name )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!el || !full_name) return FALSE;
    if (el->class_name[0] && !wcscmp( el->class_name, full_name )) return TRUE;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_tree_has_class( ch, full_name )) return TRUE;
    }
    if (el->children_coll)
    {
        v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el && uwp_tree_has_class( ch, full_name )) return TRUE;
        }
    }
    return FALSE;
}

static struct uwp_element *uwp_find_empty_holder_border( struct uwp_element *el, const WCHAR *match_prefix, BOOL prefix_only )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!el) return NULL;
    if (el->kind == UWP_KIND_BORDER && el->visibility == 0 && !el->children_coll && !el->content)
    {
        if (!prefix_only) return el;
        if (match_prefix && el->name[0])
        {
            WCHAR pfx[8];
            int plen = min( 4, (int)wcslen( match_prefix ) );
            if (plen >= 3)
            {
                lstrcpynW( pfx, match_prefix, plen + 1 );
                if (wcsstr( el->name, pfx )) return el;
            }
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *r;
        if (ch && ch != el && (r = uwp_find_empty_holder_border( ch, match_prefix, prefix_only ))) return r;
    }
    if (el->children_coll)
    {
        v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            struct uwp_element *r;
            if (ch && ch != el && (r = uwp_find_empty_holder_border( ch, match_prefix, prefix_only ))) return r;
        }
    }
    return NULL;
}

static BOOL uwp_populate_element_from_pri_xbf( struct uwp_element *el, const WCHAR *short_name )
{
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;
    const WCHAR *lookup = short_name;

    if (!el) return FALSE;
    if (!lookup || !lookup[0])
    {
        const WCHAR *dot = wcsrchr( el->class_name, '.' );
        lookup = dot ? dot + 1 : el->class_name;
    }
    if (!lookup || !lookup[0]) return FALSE;
    if (!uwp_pri_find_xbf( lookup, &xbf, &xbf_sz ) || !xbf || xbf_sz <= 0x50)
        return FALSE;

    if (!el->children_coll && !el->content)
    {
        void *conn = NULL;
        IInspectable *comp = el->outer ? el->outer : &el->IInspectable_iface;
        if (el->outer)
            IInspectable_QueryInterface( el->outer, &IID_IComponentConnector_UWP_g, &conn );
        TRACE( "uwp_populate_element_from_pri_xbf: parsing %s.xbf (%lu bytes) into %p\n",
               debugstr_w(lookup), (unsigned long)xbf_sz, el );
        uwp_parse_and_build_xbf( xbf, xbf_sz, comp, conn, TRUE, uwp_get_app_outer(), -1, el, NULL );
        if (conn) IUnknown_Release( (IUnknown *)conn );
    }
    uwp_realize_empty_panel_defers( el );
    return el->children_coll != NULL || el->content != NULL;
}

static BOOL uwp_instantiate_xbf_control_into( struct uwp_element *holder, const WCHAR *full_name,
                                              const WCHAR *short_name )
{
    void *inst = NULL;
    struct uwp_element *el = NULL;
    struct uwp_element *prev_head;
    int gen;
    HRESULT hr;

    if (!holder || !full_name || !full_name[0]) return FALSE;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    prev_head = g_all_uwp_elems_head;
    gen = g_all_uwp_elem_cnt;
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    __TRY
    {
        hr = uwp_create_instance_from_provider( full_name, &inst );
    }
    __EXCEPT_ALL
    {
        hr = E_FAIL;
        TRACE( "uwp_instantiate_xbf_control_into: provider activation(%s) faulted\n", debugstr_w(full_name) );
    }
    __ENDTRY;

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    if (g_all_uwp_elem_cnt > gen)
    {
        struct uwp_element *e;
        for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
        {
            if (e->kind == UWP_KIND_USER_CONTROL || e->kind == UWP_KIND_PAGE)
                el = e;
        }
    }
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    if (!el && inst && SUCCEEDED(hr))
        el = uwp_elem_from_any( inst );

    if (!el)
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( full_name, NULL, NULL, (void **)&raw )) && raw)
        {
            el = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
        }
    }
    if (!el) return FALSE;

    if (inst) el->provider_inst = (IInspectable *)inst;
    lstrcpynW( el->class_name, full_name, ARRAY_SIZE(el->class_name) );
    if (!el->children_coll && !el->content)
        uwp_populate_element_from_pri_xbf( el, short_name );

    if (!el->children_coll && !el->content)
    {
        TRACE( "uwp_instantiate_xbf_control_into: %s yielded empty tree\n", debugstr_w(full_name) );
        return FALSE;
    }

    uwp_vec_create( holder, &holder->children_coll );
    uwp_vec_Append( holder->children_coll, el->outer ? (void *)el->outer : (void *)&el->IInspectable_iface );
    el->parent = holder;
    uwp_vsm_invalidate_eval_chain( el );
    uwp_realize_empty_panel_defers( el );
    uwp_fire_loaded_tree( el );
    TRACE( "uwp_instantiate_xbf_control_into: hosted %s in %s\n",
           debugstr_w(el->class_name), debugstr_w(holder->name) );
    return TRUE;
}

static BOOL uwp_populate_page_from_xbf_refs( struct uwp_element *page_el )
{
    int i;
    struct uwp_element *any_holder;

    if (!page_el || !page_el->xbf_parsed) return FALSE;

    uwp_realize_empty_panel_defers( page_el );

    /* Check if page already has populated content and no empty placeholder holders needing XBF controls */
    any_holder = uwp_find_empty_holder_border( page_el, NULL, FALSE );
    if (!any_holder)
        return page_el->children_coll != NULL || page_el->content != NULL;

    for (i = 0; i < page_el->xbf_ref_cnt; i++)
    {
        struct uwp_xbf_ref *ref = &page_el->xbf_refs[i];
        struct uwp_element *holder;

        if (ref->is_resource_path || ref->instantiated) continue;
        if (uwp_tree_has_class( page_el, ref->full_name ))
        {
            ref->instantiated = TRUE;
            continue;
        }
        holder = uwp_find_empty_holder_border( page_el, ref->short_name, TRUE );
        if (!holder) holder = any_holder;
        if (holder && uwp_instantiate_xbf_control_into( holder, ref->full_name, ref->short_name ))
        {
            ref->instantiated = TRUE;
            return TRUE;
        }
    }

    return page_el->children_coll != NULL || page_el->content != NULL;
}

/* Error fallback strictly when Calculator.xbf / NumberPad.xbf / CalculatorStandardOperators.xbf fail to load */
static void uwp_seed_synthetic_calculator( struct uwp_element *holder )
{
    static const WCHAR *keys[20] =
    {
        L"%", L"CE", L"C", L"\x232b",
        L"1", L"2", L"3", L"\x00f7",
        L"4", L"5", L"6", L"\x00d7",
        L"7", L"8", L"9", L"-",
        L"\x221a", L"0", L"+", L"=",
    };
    static const WCHAR *names[20] =
    {
        L"PercentButton", L"ClearEntryButton", L"ClearButton", L"BackSpaceButton",
        L"num1Button", L"num2Button", L"num3Button", L"divButton",
        L"num4Button", L"num5Button", L"num6Button", L"mulButton",
        L"num7Button", L"num8Button", L"num9Button", L"minusButton",
        L"SqrtButton", L"num0Button", L"plusButton", L"EqualButton",
    };
    struct uwp_element *result_el, *grid_el;
    IInspectable *result_raw = NULL, *grid_raw = NULL;
    int i;

    if (!holder || holder->children_coll) return;

    if (FAILED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&result_raw )))
        return;
    result_el = uwp_elem_from_any( result_raw );
    IInspectable_Release( result_raw );
    if (!result_el) return;
    lstrcpynW( result_el->name, L"NormalOutput", ARRAY_SIZE(result_el->name) );
    lstrcpynW( result_el->text, L"0", ARRAY_SIZE(result_el->text) );
    result_el->grid_row = 0;
    result_el->grid_row_span = 1;
    result_el->font_size = 46.0;
    uwp_calc_vm_ensure();   /* early activation: async calc manager init lands while UI idles */

    if (FAILED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&grid_raw )))
        return;
    grid_el = uwp_elem_from_any( grid_raw );
    IInspectable_Release( grid_raw );
    if (!grid_el) return;
    grid_el->grid_row = 1;
    grid_el->grid_row_span = 1;

    uwp_vec_create( grid_el, &grid_el->children_coll );
    for (i = 0; i < 20; i++)
    {
        IInspectable *btn_raw = NULL;
        struct uwp_element *btn;
        if (FAILED(uwp_element_create( L"Windows.UI.Xaml.Controls.Button", NULL, NULL, (void **)&btn_raw )))
            break;
        btn = uwp_elem_from_any( btn_raw );
        IInspectable_Release( btn_raw );
        if (!btn) break;
        lstrcpynW( btn->name, names[i], ARRAY_SIZE(btn->name) );
        lstrcpynW( btn->text, keys[i], ARRAY_SIZE(btn->text) );
        btn->grid_row = i / 4;
        btn->grid_col = i % 4;
        uwp_vec_Append( grid_el->children_coll, &btn->IInspectable_iface );
    }

    /* Wrap in a UserControl mimicking CalculatorApp.Controls.Calculator:
     * the app's Control template host fills CalcHolder with it. */
    {
        IInspectable *uc_raw = NULL;
        struct uwp_element *uc;
        if (FAILED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&uc_raw )) || !uc_raw)
            return;
        uc = uwp_elem_from_any( uc_raw );
        IInspectable_Release( uc_raw );
        if (!uc) return;
        lstrcpynW( uc->name, xaml_get_app_profile()->seed_xbf ? xaml_get_app_profile()->seed_xbf : L"Calculator", ARRAY_SIZE(uc->name) );
        uwp_vec_create( uc, &uc->children_coll );
        uwp_vec_Append( uc->children_coll, &result_el->IInspectable_iface );
        uwp_vec_Append( uc->children_coll, &grid_el->IInspectable_iface );
        result_el->parent = uc;
        grid_el->parent = uc;
        uc->parent = holder;
        uwp_vec_create( holder, &holder->children_coll );
        uwp_vec_Append( holder->children_coll, &uc->IInspectable_iface );
    }
    TRACE( "uwp_seed_synthetic_calculator: seeded display + keypad(20) into %s\n",
           debugstr_w(holder->name) );
}

static void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created );

struct store_catalog_item
{
    WCHAR full_name[256];
    WCHAR name[128];
    WCHAR cat[64];
    WCHAR icon[8];
    WCHAR rating[32];
    WCHAR btn[16];
    BOOL is_framework;
};

typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
typedef LONG (WINAPI *p_RegEnumKeyExW_t)( HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME );
typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );

static void store_make_icon_monogram( const WCHAR *title, WCHAR *icon, size_t icon_len )
{
    size_t n = 0;
    const WCHAR *p = title;
    BOOL take_next = TRUE;

    while (*p && n + 1 < icon_len && n < 3)
    {
        WCHAR ch = *p++;
        if (ch == L'.' || ch == L' ' || ch == L'-' || ch == L'_')
        {
            take_next = TRUE;
            continue;
        }
        if (take_next)
        {
            if (ch >= L'a' && ch <= L'z') ch -= (L'a' - L'A');
            icon[n++] = ch;
            take_next = FALSE;
        }
    }
    if (!n && title[0])
    {
        WCHAR ch = title[0];
        if (ch >= L'a' && ch <= L'z') ch -= (L'a' - L'A');
        icon[n++] = ch;
    }
    icon[n] = 0;
}

static void store_split_identity( const WCHAR *full, WCHAR *name, size_t name_len, WCHAR *ver, size_t ver_len )
{
    const WCHAR *u1 = wcschr( full, L'_' );
    if (!u1)
    {
        lstrcpynW( name, full, name_len );
        if (ver_len) ver[0] = 0;
        return;
    }
    {
        size_t len = min( (size_t)(u1 - full), name_len - 1 );
        memcpy( name, full, len * sizeof(WCHAR) );
        name[len] = 0;
    }
    if (ver_len)
    {
        const WCHAR *u2 = wcschr( u1 + 1, L'_' );
        size_t vlen = u2 ? (size_t)(u2 - (u1 + 1)) : wcslen( u1 + 1 );
        vlen = min( vlen, ver_len - 1 );
        memcpy( ver, u1 + 1, vlen * sizeof(WCHAR) );
        ver[vlen] = 0;
    }
}

static void store_query_reg_display_info( HMODULE reg_mod, const WCHAR *full_name, WCHAR *disp_name, size_t disp_len, BOOL *is_fw )
{
    p_RegOpenKeyExW_t p_open;
    p_RegQueryValueExW_t p_query;
    p_RegCloseKey_t p_close;
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    static const WCHAR *prefixes[] = {
        L"Software\\Classes\\AppModel\\Repository\\Packages\\",
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\",
    };
    size_t r, p;

    if (!reg_mod) return;
    p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
    p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
    p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
    if (!p_open || !p_query || !p_close) return;

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        for (p = 0; p < ARRAY_SIZE(prefixes); p++)
        {
            WCHAR sub[512];
            HKEY hk = NULL;
            DWORD type = 0, cb;
            lstrcpynW( sub, prefixes[p], ARRAY_SIZE(sub) - 256 );
            lstrcatW( sub, full_name );
            if (p_open( roots[r], sub, 0, KEY_READ, &hk ) != ERROR_SUCCESS) continue;

            if (disp_name && !disp_name[0])
            {
                WCHAR buf[128] = {0};
                cb = sizeof(buf);
                if (p_query( hk, L"DisplayName", NULL, &type, (BYTE *)buf, &cb ) == ERROR_SUCCESS &&
                    type == REG_SZ && buf[0] && wcsncmp( buf, L"ms-resource:", 12 ) != 0)
                {
                    lstrcpynW( disp_name, buf, disp_len );
                }
            }
            if (is_fw)
            {
                DWORD fw = 0;
                cb = sizeof(fw);
                if (p_query( hk, L"Framework", NULL, &type, (BYTE *)&fw, &cb ) == ERROR_SUCCESS && type == REG_DWORD)
                    *is_fw = (fw != 0);
            }
            p_close( hk );
        }
    }
}

static void store_add_catalog_item( struct store_catalog_item **items, int *count, int *cap,
                                    const WCHAR *full_name, const WCHAR *disp_name,
                                    const WCHAR *pkg_name, const WCHAR *ver_str, BOOL is_fw )
{
    int i;
    struct store_catalog_item *arr, *it;
    const WCHAR *title;

    if (!full_name || !full_name[0] || !wcschr( full_name, L'_' )) return;

    for (i = 0; i < *count; i++)
    {
        if (!wcsicmp( (*items)[i].full_name, full_name )) return;
    }
    if (*count >= *cap)
    {
        int new_cap = *cap ? (*cap * 2) : 16;
        arr = realloc( *items, new_cap * sizeof(*arr) );
        if (!arr) return;
        *items = arr;
        *cap = new_cap;
    }

    it = &(*items)[(*count)++];
    memset( it, 0, sizeof(*it) );
    lstrcpynW( it->full_name, full_name, ARRAY_SIZE(it->full_name) );
    it->is_framework = is_fw;

    title = (disp_name && disp_name[0]) ? disp_name : (pkg_name && pkg_name[0] ? pkg_name : full_name);
    lstrcpynW( it->name, title, ARRAY_SIZE(it->name) );
    lstrcpynW( it->cat, is_fw ? L"Framework" : L"Installed App", ARRAY_SIZE(it->cat) );
    store_make_icon_monogram( it->name, it->icon, ARRAY_SIZE(it->icon) );
    if (ver_str && ver_str[0])
        swprintf( it->rating, ARRAY_SIZE(it->rating), L"v%s", ver_str );
    else
        lstrcpynW( it->rating, L"Installed", ARRAY_SIZE(it->rating) );
    lstrcpynW( it->btn, is_fw ? L"Installed" : L"Open", ARRAY_SIZE(it->btn) );
}

static void store_query_installed_packages( struct store_catalog_item **out_items, int *out_count )
{
    static const GUID IID_IPackageManager_local =
        { 0x9a7d4b65, 0x5e8f, 0x4fc7, { 0xa2, 0xe5, 0x7f, 0x69, 0x25, 0xcb, 0x8b, 0x53 } };
    struct store_catalog_item *items = NULL;
    int count = 0, cap = 0;
    HMODULE reg_mod;
    HSTRING pm_cls = NULL;
    IInspectable *pm_insp = NULL;
    IPackageManager *pm = NULL;
    IIterable_Package *iterable = NULL;
    IIterator_Package *iter = NULL;
    HRESULT hr;

    *out_items = NULL;
    *out_count = 0;

    reg_mod = GetModuleHandleW( L"kernelbase.dll" );
    if (!reg_mod) reg_mod = LoadLibraryW( L"advapi32.dll" );

    /* 1. Query via Windows.Management.Deployment.IPackageManager::FindPackagesForUser */
    if (SUCCEEDED(WindowsCreateString( L"Windows.Management.Deployment.PackageManager", 44, &pm_cls )) && pm_cls)
    {
        if (SUCCEEDED(RoActivateInstance( pm_cls, &pm_insp )) && pm_insp)
        {
            if (SUCCEEDED(IInspectable_QueryInterface( pm_insp, &IID_IPackageManager_local, (void **)&pm )) && pm)
            {
                hr = IPackageManager_FindPackagesByUserSecurityId( pm, NULL, &iterable );
                if (FAILED(hr) || !iterable)
                    hr = IPackageManager_FindPackages( pm, &iterable );
                if (SUCCEEDED(hr) && iterable)
                {
                    if (SUCCEEDED(IIterable_Package_First( iterable, &iter )) && iter)
                    {
                        boolean has_cur = FALSE;
                        while (SUCCEEDED(IIterator_Package_get_HasCurrent( iter, &has_cur )) && has_cur)
                        {
                            __x_ABI_CWindows_CApplicationModel_CIPackage *pkg = NULL;
                            if (SUCCEEDED(IIterator_Package_get_Current( iter, &pkg )) && pkg)
                            {
                                __x_ABI_CWindows_CApplicationModel_CIPackageId *pkg_id = NULL;
                                boolean is_fw = FALSE;
                                __x_ABI_CWindows_CApplicationModel_CIPackage_get_IsFramework( pkg, &is_fw );
                                if (SUCCEEDED(__x_ABI_CWindows_CApplicationModel_CIPackage_get_Id( pkg, &pkg_id )) && pkg_id)
                                {
                                    HSTRING hfull = NULL;
                                    WCHAR disp[128] = {0}, name_buf[128] = {0}, ver_str[32] = {0};
                                    const WCHAR *full_s = NULL;
                                    BOOL fw_reg = is_fw;

                                    __x_ABI_CWindows_CApplicationModel_CIPackageId_get_FullName( pkg_id, &hfull );
                                    full_s = hfull ? WindowsGetStringRawBuffer( hfull, NULL ) : NULL;
                                    if (full_s)
                                    {
                                        store_split_identity( full_s, name_buf, ARRAY_SIZE(name_buf), ver_str, ARRAY_SIZE(ver_str) );
                                        store_query_reg_display_info( reg_mod, full_s, disp, ARRAY_SIZE(disp), &fw_reg );
                                        store_add_catalog_item( &items, &count, &cap, full_s, disp, name_buf, ver_str, fw_reg );
                                    }
                                    if (hfull) WindowsDeleteString( hfull );
                                    __x_ABI_CWindows_CApplicationModel_CIPackageId_Release( pkg_id );
                                }
                                __x_ABI_CWindows_CApplicationModel_CIPackage_Release( pkg );
                            }
                            if (FAILED(IIterator_Package_MoveNext( iter, &has_cur )) || !has_cur)
                                break;
                        }
                        IIterator_Package_Release( iter );
                    }
                    IIterable_Package_Release( iterable );
                }
                IPackageManager_Release( pm );
            }
            IInspectable_Release( pm_insp );
        }
        WindowsDeleteString( pm_cls );
    }

    /* 2. Supplement / fallback from AppModel package registry keys */
    if (reg_mod)
    {
        p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
        p_RegEnumKeyExW_t p_enum = (p_RegEnumKeyExW_t)GetProcAddress( reg_mod, "RegEnumKeyExW" );
        p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
        if (p_open && p_enum && p_close)
        {
            static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
            static const WCHAR *paths[] = {
                L"Software\\Classes\\AppModel\\Repository\\Packages",
                L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages",
            };
            size_t r, p;
            for (r = 0; r < ARRAY_SIZE(roots); r++)
            {
                for (p = 0; p < ARRAY_SIZE(paths); p++)
                {
                    HKEY hk = NULL;
                    DWORD idx = 0;
                    if (p_open( roots[r], paths[p], 0, KEY_READ, &hk ) != ERROR_SUCCESS) continue;
                    for (;; idx++)
                    {
                        WCHAR full[256], name[128], ver[32], disp[128] = {0};
                        DWORD cch = ARRAY_SIZE(full);
                        BOOL is_fw = FALSE;
                        if (p_enum( hk, idx, full, &cch, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS) break;
                        store_split_identity( full, name, ARRAY_SIZE(name), ver, ARRAY_SIZE(ver) );
                        store_query_reg_display_info( reg_mod, full, disp, ARRAY_SIZE(disp), &is_fw );
                        store_add_catalog_item( &items, &count, &cap, full, disp, name, ver, is_fw );
                    }
                    p_close( hk );
                }
            }
        }
    }

    /* Order non-framework apps before framework packages */
    if (count > 1)
    {
        int i, j;
        for (i = 0; i < count; i++)
        {
            if (items[i].is_framework)
            {
                for (j = i + 1; j < count; j++)
                {
                    if (!items[j].is_framework)
                    {
                        struct store_catalog_item tmp = items[i];
                        items[i] = items[j];
                        items[j] = tmp;
                        break;
                    }
                }
            }
        }
    }

    TRACE( "store_query_installed_packages: discovered %d packages\n", count );
    *out_items = items;
    *out_count = count;
}

/* Documented fallback only: invoked strictly when XBF parsing fails to build a visual tree. */
static void uwp_seed_store_catalog( struct uwp_element *page_el )
{
    static const WCHAR *nav_names[5] = { L"Home", L"Apps", L"Games", L"Movies", L"Library" };
    static const WCHAR *nav_icons[5] = { L"\x2302", L"\x25a6", L"\x25c8", L"\x25b6", L"\x25a4" };

    struct store_catalog_item *apps = NULL;
    int app_count = 0;
    IInspectable *root_grid_raw = NULL, *nav_rail_raw = NULL, *main_grid_raw = NULL;
    IInspectable *search_raw = NULL, *banner_raw = NULL;
    IInspectable *sec_title_raw = NULL, *tiles_grid_raw = NULL;
    struct uwp_element *root_grid, *nav_rail, *main_grid, *tiles_grid;
    int i;

    if (!page_el || page_el->children_coll) return;
    TRACE( "xbf: falling back to seed tree\n" );
    store_query_installed_packages( &apps, &app_count );

    /* 2. Build Root Layout: Page -> RootGrid (Col 0: NavRail 68px, Col 1: MainContent 1*) */
    if (FAILED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&root_grid_raw )) || !root_grid_raw)
    {
        free( apps );
        return;
    }
    root_grid = uwp_elem_from_any( root_grid_raw );
    IInspectable_Release( root_grid_raw );
    if (!root_grid) { free( apps ); return; }
    lstrcpynW( root_grid->name, L"RootStoreGrid", ARRAY_SIZE(root_grid->name) );
    uwp_vec_create( root_grid, &root_grid->children_coll );

    /* Left Navigation Rail */
    if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&nav_rail_raw )) && nav_rail_raw)
    {
        nav_rail = uwp_elem_from_any( nav_rail_raw );
        IInspectable_Release( nav_rail_raw );
        if (nav_rail)
        {
            IInspectable *logo_raw = NULL;
            struct uwp_element *logo;

            lstrcpynW( nav_rail->name, L"NavRail", ARRAY_SIZE(nav_rail->name) );
            nav_rail->grid_col = 0;
            nav_rail->grid_row = 0;
            nav_rail->parent = root_grid;
            uwp_vec_create( nav_rail, &nav_rail->children_coll );

            /* Logo at top */
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&logo_raw )) && logo_raw)
            {
                logo = uwp_elem_from_any( logo_raw );
                IInspectable_Release( logo_raw );
                if (logo)
                {
                    lstrcpynW( logo->name, L"StoreBrandLogo", ARRAY_SIZE(logo->name) );
                    lstrcpynW( logo->text, L"\x229e Store", ARRAY_SIZE(logo->text) );
                    logo->grid_row = 0;
                    logo->font_size = 14.0;
                    logo->parent = nav_rail;
                    uwp_vec_Append( nav_rail->children_coll, &logo->IInspectable_iface );
                }
            }

            /* Nav items */
            for (i = 0; i < 5; i++)
            {
                IInspectable *item_raw = NULL;
                struct uwp_element *item;
                if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Button", NULL, NULL, (void **)&item_raw )) && item_raw)
                {
                    item = uwp_elem_from_any( item_raw );
                    IInspectable_Release( item_raw );
                    if (item)
                    {
                        swprintf( item->name, ARRAY_SIZE(item->name), L"NavItem_%s", nav_names[i] );
                        swprintf( item->text, ARRAY_SIZE(item->text), L"%s %s", nav_icons[i], nav_names[i] );
                        item->grid_row = i + 1;
                        item->parent = nav_rail;
                        uwp_vec_Append( nav_rail->children_coll, &item->IInspectable_iface );
                    }
                }
            }
            uwp_vec_Append( root_grid->children_coll, &nav_rail->IInspectable_iface );
        }
    }

    /* Main Content Area (Col 1) */
    if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&main_grid_raw )) && main_grid_raw)
    {
        main_grid = uwp_elem_from_any( main_grid_raw );
        IInspectable_Release( main_grid_raw );
        if (main_grid)
        {
            lstrcpynW( main_grid->name, L"MainContentGrid", ARRAY_SIZE(main_grid->name) );
            main_grid->grid_col = 1;
            main_grid->grid_row = 0;
            main_grid->parent = root_grid;
            uwp_vec_create( main_grid, &main_grid->children_coll );

            /* Top Search Bar (Row 0) */
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&search_raw )) && search_raw)
            {
                struct uwp_element *search = uwp_elem_from_any( search_raw );
                IInspectable_Release( search_raw );
                if (search)
                {
                    lstrcpynW( search->name, L"SearchBoxHolderControl", ARRAY_SIZE(search->name) );
                    lstrcpynW( search->text, L"\x26b2 Search apps, games, movies and more", ARRAY_SIZE(search->text) );
                    search->grid_row = 0;
                    search->font_size = 14.0;
                    search->parent = main_grid;
                    uwp_vec_Append( main_grid->children_coll, &search->IInspectable_iface );
                }
            }

            /* Featured Hero Banner (Row 1) */
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Border", NULL, NULL, (void **)&banner_raw )) && banner_raw)
            {
                struct uwp_element *banner = uwp_elem_from_any( banner_raw );
                IInspectable_Release( banner_raw );
                if (banner)
                {
                    lstrcpynW( banner->name, L"HeroBanner", ARRAY_SIZE(banner->name) );
                    banner->grid_row = 1;
                    banner->parent = main_grid;
                    uwp_vec_Append( main_grid->children_coll, &banner->IInspectable_iface );
                }
            }

            /* Section Header (Row 2) */
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&sec_title_raw )) && sec_title_raw)
            {
                struct uwp_element *st = uwp_elem_from_any( sec_title_raw );
                IInspectable_Release( sec_title_raw );
                if (st)
                {
                    lstrcpynW( st->name, L"TopAppsHeader", ARRAY_SIZE(st->name) );
                    swprintf( st->text, ARRAY_SIZE(st->text), L"Installed Packages (%d)", app_count );
                    st->grid_row = 2;
                    st->font_size = 20.0;
                    st->parent = main_grid;
                    uwp_vec_Append( main_grid->children_coll, &st->IInspectable_iface );
                }
            }

            /* Catalog Tiles Grid (Row 3): dynamic rows x 4 columns of cards */
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&tiles_grid_raw )) && tiles_grid_raw)
            {
                tiles_grid = uwp_elem_from_any( tiles_grid_raw );
                IInspectable_Release( tiles_grid_raw );
                if (tiles_grid)
                {
                    lstrcpynW( tiles_grid->name, L"HomeIslandTilesGrid", ARRAY_SIZE(tiles_grid->name) );
                    tiles_grid->grid_row = 3;
                    tiles_grid->parent = main_grid;
                    uwp_vec_create( tiles_grid, &tiles_grid->children_coll );

                    for (i = 0; i < app_count && i < 8; i++)
                    {
                        IInspectable *card_raw = NULL;
                        struct uwp_element *card;
                        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Border", NULL, NULL, (void **)&card_raw )) && card_raw)
                        {
                            card = uwp_elem_from_any( card_raw );
                            IInspectable_Release( card_raw );
                            if (card)
                            {
                                IInspectable *icon_raw = NULL, *t_raw = NULL, *c_raw = NULL, *r_raw = NULL, *b_raw = NULL;
                                struct uwp_element *icon, *t, *c, *r, *b;

                                swprintf( card->name, ARRAY_SIZE(card->name), L"TileCard_%s", apps[i].name );
                                card->grid_row = i / 4;
                                card->grid_col = i % 4;
                                card->parent = tiles_grid;
                                uwp_vec_create( card, &card->children_coll );

                                /* App Icon */
                                if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Border", NULL, NULL, (void **)&icon_raw )) && icon_raw)
                                {
                                    icon = uwp_elem_from_any( icon_raw );
                                    IInspectable_Release( icon_raw );
                                    if (icon)
                                    {
                                        swprintf( icon->name, ARRAY_SIZE(icon->name), L"AppIcon_%s", apps[i].name );
                                        lstrcpynW( icon->text, apps[i].icon, ARRAY_SIZE(icon->text) );
                                        icon->parent = card;
                                        uwp_vec_Append( card->children_coll, &icon->IInspectable_iface );
                                    }
                                }
                                /* App Title */
                                if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&t_raw )) && t_raw)
                                {
                                    t = uwp_elem_from_any( t_raw );
                                    IInspectable_Release( t_raw );
                                    if (t)
                                    {
                                        swprintf( t->name, ARRAY_SIZE(t->name), L"TitleText_%s", apps[i].name );
                                        lstrcpynW( t->text, apps[i].name, ARRAY_SIZE(t->text) );
                                        t->font_size = 14.0;
                                        t->parent = card;
                                        uwp_vec_Append( card->children_coll, &t->IInspectable_iface );
                                    }
                                }
                                /* Category */
                                if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&c_raw )) && c_raw)
                                {
                                    c = uwp_elem_from_any( c_raw );
                                    IInspectable_Release( c_raw );
                                    if (c)
                                    {
                                        swprintf( c->name, ARRAY_SIZE(c->name), L"CategoryText_%s", apps[i].name );
                                        lstrcpynW( c->text, apps[i].cat, ARRAY_SIZE(c->text) );
                                        c->font_size = 11.0;
                                        c->parent = card;
                                        uwp_vec_Append( card->children_coll, &c->IInspectable_iface );
                                    }
                                }
                                /* Rating */
                                if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&r_raw )) && r_raw)
                                {
                                    r = uwp_elem_from_any( r_raw );
                                    IInspectable_Release( r_raw );
                                    if (r)
                                    {
                                        swprintf( r->name, ARRAY_SIZE(r->name), L"RatingText_%s", apps[i].name );
                                        lstrcpynW( r->text, apps[i].rating, ARRAY_SIZE(r->text) );
                                        r->font_size = 11.0;
                                        r->parent = card;
                                        uwp_vec_Append( card->children_coll, &r->IInspectable_iface );
                                    }
                                }
                                /* Get Button */
                                if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Button", NULL, NULL, (void **)&b_raw )) && b_raw)
                                {
                                    b = uwp_elem_from_any( b_raw );
                                    IInspectable_Release( b_raw );
                                    if (b)
                                    {
                                        swprintf( b->name, ARRAY_SIZE(b->name), L"GetButton_%s", apps[i].name );
                                        lstrcpynW( b->text, apps[i].btn, ARRAY_SIZE(b->text) );
                                        b->parent = card;
                                        uwp_vec_Append( card->children_coll, &b->IInspectable_iface );
                                    }
                                }
                                uwp_vec_Append( tiles_grid->children_coll, &card->IInspectable_iface );
                            }
                        }
                    }
                    uwp_vec_Append( main_grid->children_coll, &tiles_grid->IInspectable_iface );
                }
            }
            uwp_vec_Append( root_grid->children_coll, &main_grid->IInspectable_iface );
        }
    }

    uwp_vec_create( page_el, &page_el->children_coll );
    uwp_vec_Append( page_el->children_coll, &root_grid->IInspectable_iface );
    root_grid->parent = page_el;
    page_el->content = &root_grid->IInspectable_iface;
    IInspectable_AddRef( page_el->content );
    free( apps );
    TRACE( "uwp_seed_store_catalog: successfully populated %d dynamic Store catalog items into page %p\n",
           app_count, page_el );
}

HRESULT uwp_store_create_initial_page( IInspectable **out )
{
    IInspectable *p_raw = NULL;
    struct uwp_element *page_el;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;

    hr = uwp_element_create( L"Windows.UI.Xaml.Controls.Page", NULL, NULL, (void **)&p_raw );
    if (FAILED(hr) || !p_raw) return hr;

    page_el = uwp_elem_from_any( p_raw );
    if (page_el)
    {
        lstrcpynW( page_el->name, xaml_get_app_profile()->seed_xbf ? xaml_get_app_profile()->seed_xbf : L"StoreManifestPage", ARRAY_SIZE(page_el->name) );
        uwp_seed_store_catalog( page_el );
        g_navigated_page_el = page_el;
    }
    *out = p_raw;
    TRACE( "uwp_store_create_initial_page created page %p (raw %p)\n", page_el, p_raw );
    return S_OK;
}

static HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    TypeName *type = (TypeName *)type_name_ptr;
    const WCHAR *name = (type && type->Name) ? WindowsGetStringRawBuffer( type->Name, NULL ) : NULL;

    TRACE( "uwp_frame_NavigateWithParam iface=%p type=%s param=%p\n", iface, debugstr_w(name), param );
    if (result) *result = TRUE;
    if (!impl || !name) return S_OK;

    if (!impl->content)
    {
        IInspectable *page = NULL;
        HRESULT hr = xaml_create_page( type_name_ptr, &page );
        TRACE( "xaml_create_page(%s) -> hr %#lx, page %p\n", debugstr_w(name), hr, page );
        if (SUCCEEDED(hr) && page)
        {
            struct uwp_element *page_el = uwp_elem_from_any( page );
            TRACE( "NAVIGATE probe: page=%p g_last_created_page=%p kind=%d\n",
                   page, g_last_created_page, g_last_created_page ? g_last_created_page->kind : -1 );
            if (!page_el && g_last_created_page)
            {
                /* Provider returns the page as an MCG RCW we cannot unwrap
                 * (answers QI only for IUnknown). The composable Page element
                 * built for this activation is g_last_created_page. */
                page_el = g_last_created_page;
                TRACE( "uwp_frame_NavigateWithParam: RCW page unresolvable, using last PAGE element %p\n", page_el );
            }
            TRACE( "uwp_frame_NavigateWithParam page_el=%p outer=%p\n", page_el, page_el ? page_el->outer : NULL );
            if (page_el)
            {
                /* Store a resolvable face: RCW pointer is unresolvable by uwp_elem_from_any. */
                impl->content = page_el->outer ? page_el->outer : &page_el->IInspectable_iface;
                IInspectable_AddRef( (IInspectable *)impl->content );
                g_navigated_page_el = page_el;
                page_el->parent = impl;

                /* Invoke IPageOverrides::OnNavigatedTo (slot 8) if implemented */
                if (page_el->outer)
                {
                    void *page_ovr = NULL;
                    HRESULT qi_hr = IInspectable_QueryInterface( page_el->outer, &IID_IPageOverrides_UWP, &page_ovr );
                    TRACE( "uwp_frame_NavigateWithParam QI IPageOverrides -> hr %#lx ptr %p\n", qi_hr, page_ovr );
                    if (SUCCEEDED(qi_hr) && page_ovr)
                    {
                        HRESULT (WINAPI *on_nav)(void *, void *) = (void *)(*(void ***)page_ovr)[8];
                        HRESULT nav_hr = on_nav ? on_nav( page_ovr, g_routed_args() ) : S_OK;
                        TRACE( "uwp_frame_NavigateWithParam OnNavigatedTo -> hr %#lx\n", nav_hr );
                        IUnknown_Release( (IUnknown *)page_ovr );
                    }
                }
                uwp_fire_loaded_tree( page_el );

                /* Populate any empty placeholder containers from the page XBF's referenced PRI resources */
                if (!uwp_populate_page_from_xbf_refs( page_el ) && xaml_get_app_profile()->seed_xbf)
                {
                    if (!wcscmp( xaml_get_app_profile()->seed_xbf, L"Calculator" ))
                    {
                        struct uwp_element *holder = uwp_find_empty_holder_border( page_el, NULL, FALSE );
                        TRACE( "xbf: falling back to seed tree\n" );
                        uwp_seed_synthetic_calculator( holder ? holder : page_el );
                    }
                    else if (!wcscmp( xaml_get_app_profile()->seed_xbf, L"StoreManifestPage" ))
                    {
                        uwp_seed_store_catalog( page_el );
                    }
                }
            }
            else
            {
                impl->content = page;
            }
            xaml_window_present_current();
        }
        if ((FAILED(hr) || !impl->content) && is_store_app())
        {
            IInspectable *store_page = NULL;
            TRACE( "uwp_frame_NavigateWithParam: store fallback to uwp_store_create_initial_page\n" );
            if (SUCCEEDED(uwp_store_create_initial_page( &store_page )) && store_page)
            {
                struct uwp_element *page_el = uwp_elem_from_any( store_page );
                impl->content = store_page;
                g_navigated_page_el = page_el;
                if (page_el)
                {
                    page_el->parent = impl;
                    uwp_fire_loaded_tree( page_el );
                }
                xaml_window_present_current();
            }
        }
    }
    return S_OK;
}

/* ============================================================================
 * 6. XBF v2 Stack Parser & Visual Tree Builder
 * ============================================================================ */

static const WCHAR *uwp_btype_to_class_name( WORD tid )
{
    WORD idx = tid & 0x7fff;

    if ((tid & 0x8000) && idx < ARRAY_SIZE( xbf_known_types ) && xbf_known_types[idx])
        return xbf_known_types[idx];

    FIXME( "Unknown XBF builtin type %#04x\n", tid );
    return L"Windows.UI.Xaml.Controls.ContentControl";
}

/* Set a custom XAML property (e.g. CalculatorButton.ButtonId = "Zero") via IXamlMetadataProvider */
static void uwp_set_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, const WCHAR *str_val )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL, *member_type = NULL, *boxed_val = NULL;
    HSTRING h_owner = NULL, h_prop = NULL, h_val = NULL;
    HRESULT hr;

    if (!app_outer || !owner_type_name || !prop_name || !target_obj || !str_val) return;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) || !provider)
        return;

    if (SUCCEEDED(WindowsCreateString( owner_type_name, wcslen(owner_type_name), &h_owner )))
    {
        struct xaml_metadata_provider *mp = (struct xaml_metadata_provider *)provider;
        if (mp->lpVtbl->GetXamlTypeByFullName &&
            SUCCEEDED(mp->lpVtbl->GetXamlTypeByFullName( mp, h_owner, (struct xaml_type **)&xaml_type )) && xaml_type)
        {
            struct xaml_type *xt = (struct xaml_type *)xaml_type;
            if (xt->lpVtbl->GetMember && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
            {
                if (SUCCEEDED(xt->lpVtbl->GetMember( xt, h_prop, (struct xaml_member **)&member )) && member)
                {
                    struct xaml_member *xm = (struct xaml_member *)member;
                    if (xm->lpVtbl->get_Type && SUCCEEDED(xm->lpVtbl->get_Type( xm, (struct xaml_type **)&member_type )) && member_type)
                    {
                        struct xaml_type *mt = (struct xaml_type *)member_type;
                        if (mt->lpVtbl->CreateFromString && SUCCEEDED(WindowsCreateString( str_val, wcslen(str_val), &h_val )))
                        {
                            hr = mt->lpVtbl->CreateFromString( mt, h_val, &boxed_val );
                            if (SUCCEEDED(hr) && boxed_val && xm->lpVtbl->SetValue)
                            {
                                __TRY
                                {
                                    hr = xm->lpVtbl->SetValue( xm, target_obj, boxed_val );
                                }
                                __EXCEPT_ALL { hr = E_FAIL; }
                                __ENDTRY;
                                TRACE( "IXamlMember::SetValue(%s.%s = %s) -> hr %#lx\n",
                                       debugstr_w(owner_type_name), debugstr_w(prop_name),
                                       debugstr_w(str_val), hr );
                            }
                            if (boxed_val) IInspectable_Release( (IInspectable *)boxed_val );
                            WindowsDeleteString( h_val );
                        }
                        IInspectable_Release( (IInspectable *)member_type );
                    }
                    IInspectable_Release( (IInspectable *)member );
                }
                WindowsDeleteString( h_prop );
            }
            IInspectable_Release( (IInspectable *)xaml_type );
        }
        WindowsDeleteString( h_owner );
    }
    IInspectable_Release( (IInspectable *)provider );
}

struct xbf_type_info
{
    WCHAR xmlns[128];
    WCHAR full_name[128];
    WCHAR short_name[64];
};

struct xbf_prop_info
{
    WORD owner_tid;
    WCHAR name[64];
};

struct xbf_ctx
{
    const BYTE *xbf;
    DWORD xbf_sz;
    DWORD meta_sz;
    DWORD node_sz;
    WCHAR **strs;
    DWORD str_cnt;
    WCHAR **xmlns;
    DWORD xmlns_cnt;
    struct xbf_type_info *types;
    DWORD type_cnt;
    struct xbf_prop_info *props;
    DWORD prop_cnt;
    const BYTE *nodes;
    DWORD sub_cnt;
    DWORD sub_base;
    BOOL *sub_done;
    void *app_outer;
    void *connector;
    BOOL is_uwp_conn;
    struct uwp_element *root_el;
    IInspectable *root_comp;
    struct uwp_rd_bucket *target_rd;
    char base_xbf_dir[256];
    BOOL rd_only;
    BOOL vsm_collect;
};

struct xbf_rd_map_item
{
    WORD key_sid;
    DWORD sub_off;
};

struct xbf_rd_table
{
    struct xbf_rd_map_item *items;
    int count;
    int cap;
};

struct xbf_stack_val
{
    enum uwp_rd_kind kind;
    WORD tid;
    struct uwp_element *el;
    IInspectable *raw;
    UINT32 argb;
    BOOL has_argb;
    double opacity;
    double dval;
    double vec4[4];
    WCHAR str_val[128];
    WCHAR based_on[96];
    struct uwp_style_setter *setters;
    int setter_cnt;
    int setter_cap;
    WCHAR vsm_target[64];
    WCHAR vsm_prop[64];
    WCHAR vsm_val_str[128];
    int vsm_vt;
    double vsm_num;
    BOOL vsm_is_res_ref;
    BOOL vsm_has_val;
};

static void uwp_vsm_invalidate_eval_chain( struct uwp_element *el )
{
    int depth = 0;
    while (el && depth++ < 32)
    {
        el->vsm_eval_valid = FALSE;
        if (el->owner_component && el->owner_component != el)
            el->owner_component->vsm_eval_valid = FALSE;
        if (el->parent == el) break;
        el = el->parent;
    }
}

static struct uwp_vsm_group *uwp_vsm_add_group( struct uwp_element *el, const WCHAR *name )
{
    struct uwp_vsm_group *g;
    if (!el) return NULL;
    if (el->vsm_group_cnt >= el->vsm_group_cap)
    {
        int ncap = el->vsm_group_cap ? el->vsm_group_cap * 2 : 4;
        struct uwp_vsm_group *ng = realloc( el->vsm_groups, (size_t)ncap * sizeof(*ng) );
        if (!ng) return NULL;
        el->vsm_groups = ng;
        el->vsm_group_cap = ncap;
    }
    g = &el->vsm_groups[el->vsm_group_cnt++];
    memset( g, 0, sizeof(*g) );
    g->active_state_idx = -1;
    if (name && name[0]) lstrcpynW( g->name, name, ARRAY_SIZE(g->name) );
    uwp_vsm_invalidate_eval_chain( el );
    return g;
}

static struct uwp_vsm_state *uwp_vsm_add_state( struct uwp_vsm_group *g, const WCHAR *name )
{
    struct uwp_vsm_state *st;
    if (!g) return NULL;
    if (g->state_cnt >= g->state_cap)
    {
        int ncap = g->state_cap ? g->state_cap * 2 : 8;
        struct uwp_vsm_state *ns = realloc( g->states, (size_t)ncap * sizeof(*ns) );
        if (!ns) return NULL;
        g->states = ns;
        g->state_cap = ncap;
    }
    st = &g->states[g->state_cnt++];
    memset( st, 0, sizeof(*st) );
    if (name && name[0]) lstrcpynW( st->name, name, ARRAY_SIZE(st->name) );
    return st;
}

static void uwp_vsm_state_add_setter( struct uwp_vsm_state *st, const WCHAR *target, const WCHAR *prop,
                                      const WCHAR *val_str, int vt, double num, BOOL is_res_ref )
{
    struct uwp_vsm_setter *s;
    if (!st || !target || !target[0] || !prop || !prop[0]) return;
    if (st->setter_cnt >= st->setter_cap)
    {
        int ncap = st->setter_cap ? st->setter_cap * 2 : 8;
        struct uwp_vsm_setter *ns = realloc( st->setters, (size_t)ncap * sizeof(*ns) );
        if (!ns) return;
        st->setters = ns;
        st->setter_cap = ncap;
    }
    s = &st->setters[st->setter_cnt++];
    memset( s, 0, sizeof(*s) );
    lstrcpynW( s->target, target, ARRAY_SIZE(s->target) );
    lstrcpynW( s->property, prop, ARRAY_SIZE(s->property) );
    if (val_str && val_str[0]) lstrcpynW( s->val_str, val_str, ARRAY_SIZE(s->val_str) );
    s->vt = vt;
    s->num = num;
    s->is_res_ref = is_res_ref;
}

static void vsm_clean_prop_name( const WCHAR *raw, WCHAR *out_prop, size_t max_prop )
{
    const WCHAR *p = raw;
    size_t len;
    if (!out_prop || !max_prop) return;
    out_prop[0] = 0;
    if (!p) return;
    while (*p == L' ' || *p == L'(') p++;
    len = wcslen( p );
    while (len > 0 && (p[len - 1] == L')' || p[len - 1] == L' ')) len--;
    if (len >= max_prop) len = max_prop - 1;
    memcpy( out_prop, p, len * sizeof(WCHAR) );
    out_prop[len] = 0;
}

static void vsm_split_setter_target( const WCHAR *spec, WCHAR *out_target, size_t max_target,
                                     WCHAR *out_prop, size_t max_prop )
{
    const WCHAR *dot;
    size_t tlen;
    if (out_target && max_target) out_target[0] = 0;
    if (out_prop && max_prop) out_prop[0] = 0;
    if (!spec || !spec[0]) return;
    dot = wcschr( spec, L'.' );
    if (!dot) return;
    tlen = (size_t)(dot - spec);
    if (out_target && max_target)
    {
        if (tlen >= max_target) tlen = max_target - 1;
        memcpy( out_target, spec, tlen * sizeof(WCHAR) );
        out_target[tlen] = 0;
    }
    vsm_clean_prop_name( dot + 1, out_prop, max_prop );
}

static BOOL xbf_read_uleb( const BYTE *buf, DWORD *io_p, DWORD end_p, DWORD *out_val )
{
    DWORD p, val = 0;
    int sh = 0;
    if (!buf || !io_p || !out_val) return FALSE;
    p = *io_p;
    while (p < end_p)
    {
        BYTE b = buf[p++];
        if (sh >= 35 || (sh == 28 && (b & 0x70))) return FALSE;
        val |= (DWORD)(b & 0x7f) << sh;
        if (!(b & 0x80))
        {
            *io_p = p;
            *out_val = val;
            return TRUE;
        }
        sh += 7;
    }
    return FALSE;
}

static BOOL xbf_get_substream_bounds( const struct xbf_ctx *ctx, DWORD sub_idx,
                                      DWORD *out_start, DWORD *out_end, DWORD *out_size )
{
    DWORD ss, ee;
    if (!ctx || !ctx->nodes || sub_idx >= ctx->sub_cnt) return FALSE;
    if (ctx->sub_base < 4 || ctx->sub_base > ctx->node_sz || sub_idx >= (ctx->sub_base - 4) / 8) return FALSE;
    ss = *(const DWORD *)(ctx->nodes + 4 + sub_idx * 8);
    ee = *(const DWORD *)(ctx->nodes + 4 + sub_idx * 8 + 4);
    if (ss > ee || ee > ctx->node_sz - ctx->sub_base) return FALSE;
    if (out_start) *out_start = ctx->sub_base + ss;
    if (out_end) *out_end = ctx->sub_base + ee;
    if (out_size) *out_size = ee - ss;
    return TRUE;
}

static void xbf_stack_val_add_setter( struct xbf_stack_val *sv, WORD pid, int vt, BOOL is_res_ref,
                                      double num, const WCHAR *sval, const double vec4[4] )
{
    struct uwp_style_setter *st;
    if (!sv) return;
    if (sv->setter_cnt >= sv->setter_cap)
    {
        int ncap = sv->setter_cap ? sv->setter_cap * 2 : 16;
        struct uwp_style_setter *ns = realloc( sv->setters, ncap * sizeof(*ns) );
        if (!ns) return;
        sv->setters = ns;
        sv->setter_cap = ncap;
    }
    st = &sv->setters[sv->setter_cnt++];
    memset( st, 0, sizeof(*st) );
    st->pid = pid;
    st->vt = vt;
    st->is_res_ref = is_res_ref;
    st->num = num;
    if (vec4) memcpy( st->vec4, vec4, sizeof(st->vec4) );
    if (sval && sval[0]) lstrcpynW( st->str, sval, ARRAY_SIZE(st->str) );
}

static void uwp_rd_store_stack_val( struct uwp_rd_bucket *b, const WCHAR *key, struct xbf_stack_val *sv )
{
    struct uwp_rd_entry *e;
    if (!b || !key || !key[0] || !sv) return;
    TRACE( "uwp_rd_store_stack_val b=%p key=%s kind=%d dval=%f\n", b, debugstr_w(key), sv->kind, sv->dval );
    e = rd_bucket_upsert( b, key );
    if (!e) return;

    if (e->value) { IInspectable_Release( e->value ); e->value = NULL; }
    free( e->setters );
    e->setters = NULL;
    e->setter_cnt = e->setter_cap = 0;

    e->kind = sv->kind;
    e->argb = sv->argb;
    e->has_argb = sv->has_argb;
    e->opacity = sv->opacity > 0.0 ? sv->opacity : (sv->kind == UWP_RD_BRUSH ? sv->opacity : 1.0);
    if (sv->kind != UWP_RD_BRUSH && e->opacity == 0.0) e->opacity = 1.0;
    e->dval = sv->dval;
    memcpy( e->vec4, sv->vec4, sizeof(e->vec4) );
    lstrcpynW( e->str_val, (sv->kind == UWP_RD_STYLE) ? sv->based_on : sv->str_val, ARRAY_SIZE(e->str_val) );

    if (sv->kind == UWP_RD_STYLE && sv->setter_cnt > 0)
    {
        e->setters = malloc( (size_t)sv->setter_cnt * sizeof(*e->setters) );
        if (e->setters)
        {
            memcpy( e->setters, sv->setters, (size_t)sv->setter_cnt * sizeof(*e->setters) );
            e->setter_cnt = sv->setter_cnt;
            e->setter_cap = sv->setter_cnt;
        }
    }
    if (sv->el && sv->kind == UWP_RD_BRUSH)
    {
        if (sv->el->has_brush_argb)
        {
            e->argb = sv->el->brush_argb;
            e->has_argb = TRUE;
        }
        e->opacity = sv->el->brush_opacity;
        if (!e->str_val[0] && sv->el->brush_color_ref[0])
            lstrcpynW( e->str_val, sv->el->brush_color_ref, ARRAY_SIZE(e->str_val) );
    }
    if (sv->kind == UWP_RD_OBJ && sv->raw)
    {
        e->value = sv->raw;
        IInspectable_AddRef( sv->raw );
    }
    /* If it's a brush referencing a color key, try eager resolve if already in scope */
    if (e->kind == UWP_RD_BRUSH && !e->has_argb && e->str_val[0])
    {
        UINT32 resolved = 0;
        if (uwp_resolve_brush_or_color( b, b->owner_el, e->str_val, 0, &resolved ))
        {
            e->argb = resolved;
            e->has_argb = TRUE;
        }
    }
}

static BOOL xbf_read_value( struct xbf_ctx *ctx, const BYTE *nd, DWORD *io_p, DWORD end_p,
                            int *out_vt, double *out_num, WCHAR *out_str, int max_str,
                            double out_vec4[4] )
{
    DWORD p = *io_p;
    BYTE vt;
    if (p >= end_p) return FALSE;
    vt = nd[p++];
    *out_vt = vt;
    *out_num = 0.0;
    if (out_str && max_str > 0) out_str[0] = 0;
    if (out_vec4) { out_vec4[0] = out_vec4[1] = out_vec4[2] = out_vec4[3] = 0.0; }

    switch (vt)
    {
    case 0x00: /* bool false */
    case 0x01: /* bool false / zero */
        *out_num = 0.0;
        break;
    case 0x02: /* bool true */
        *out_num = 1.0;
        break;
    case 0x03: /* float32 */
        if (p + 4 > end_p) return FALSE;
        *out_num = (double)*(const float *)(nd + p);
        p += 4;
        break;
    case 0x04: /* int32 */
        if (p + 4 > end_p) return FALSE;
        *out_num = (double)*(const INT32 *)(nd + p);
        p += 4;
        break;
    case 0x05: /* string table ref (u16) */
        if (p + 2 > end_p) return FALSE;
        {
            WORD sid = *(const WORD *)(nd + p);
            p += 2;
            if (sid < ctx->str_cnt && ctx->strs[sid] && out_str)
                lstrcpynW( out_str, ctx->strs[sid], max_str );
        }
        break;
    case 0x06: /* Thickness (4 floats) */
        if (p + 16 > end_p) return FALSE;
        if (out_vec4)
        {
            out_vec4[0] = *(const float *)(nd + p);
            out_vec4[1] = *(const float *)(nd + p + 4);
            out_vec4[2] = *(const float *)(nd + p + 8);
            out_vec4[3] = *(const float *)(nd + p + 12);
        }
        p += 16;
        break;
    case 0x07: /* GridLength (u32 type, f32 val) */
        if (p + 8 > end_p) return FALSE;
        if (out_vec4)
        {
            out_vec4[0] = (double)*(const DWORD *)(nd + p);
            out_vec4[1] = (double)*(const float *)(nd + p + 4);
        }
        *out_num = (double)*(const float *)(nd + p + 4);
        p += 8;
        break;
    case 0x08: /* Color (u32 ARGB) */
        if (p + 4 > end_p) return FALSE;
        *out_num = (double)*(const DWORD *)(nd + p);
        p += 4;
        break;
    case 0x09: /* Inline UTF-16 string (u32 len + chars) */
        if (p + 4 > end_p) return FALSE;
        {
            DWORD ccnt = *(const DWORD *)(nd + p);
            p += 4;
            if (ccnt > (end_p - p) / 2) return FALSE;
            if (out_str && max_str > 0)
            {
                DWORD cp = min( ccnt, (DWORD)(max_str - 1) );
                memcpy( out_str, nd + p, cp * sizeof(WCHAR) );
                out_str[cp] = 0;
            }
            p += ccnt * 2;
        }
        break;
    case 0x0b: /* Enum (u16 type_id, u32 val) */
        if (p + 6 > end_p) return FALSE;
        *out_num = (double)*(const INT32 *)(nd + p + 2);
        p += 6;
        break;
    default:
        return FALSE;
    }
    *io_p = p;
    return TRUE;
}

static void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                                WORD pid, int vt, double num, const WCHAR *sval,
                                const double vec4[4], BOOL is_res_ref );

static void uwp_apply_style_entry( struct xbf_ctx *ctx, struct uwp_element *el,
                                   struct uwp_rd_bucket *b, struct uwp_rd_entry *se, int depth )
{
    int i;
    if (!el || !se || se->kind != UWP_RD_STYLE || depth > 8) return;
    if (se->str_val[0])
    {
        struct uwp_rd_entry *base_se = NULL;
        struct uwp_rd_bucket *base_b = NULL;
        if (uwp_rd_find_entry( b, el, se->str_val, 0, &base_se, &base_b ) && base_se)
            uwp_apply_style_entry( ctx, el, base_b, base_se, depth + 1 );
    }
    for (i = 0; i < se->setter_cnt; i++)
    {
        const struct uwp_style_setter *st = &se->setters[i];
        xbf_apply_property( ctx, el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
    }
}

static void uwp_apply_style_by_name( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *style_key )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *se = NULL;
    struct uwp_rd_bucket *sb = NULL;
    if (!el || !style_key || !style_key[0]) return;
    uwp_extract_resource_key( style_key, key, ARRAY_SIZE(key) );
    if (!key[0]) return;
    uwp_ensure_app_resources_loaded();
    if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, key, 0, &se, &sb ) && se && se->kind == UWP_RD_STYLE)
        uwp_apply_style_entry( ctx, el, sb, se, 0 );
}

static void uwp_el_set_bg_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->bg_argb = argb;
    el->bg_color = argb_to_colorref( argb );
    el->bg_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_bg = TRUE;
}

static void uwp_el_set_fg_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->fg_argb = argb;
    el->fg_color = argb_to_colorref( argb );
    el->fg_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_fg = TRUE;
}

static void uwp_el_set_border_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->border_argb = argb;
    el->border_color = argb_to_colorref( argb );
    el->border_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_border = TRUE;
}

static BOOL uwp_is_bg_pid( WORD pid )
{
    return pid == XBF_PROP_CONTROL_BACKGROUND || pid == XBF_PROP_PANEL_BACKGROUND || pid == XBF_PROP_BORDER_BACKGROUND || pid == XBF_PROP_SHAPE_FILL;
}

static BOOL uwp_is_fg_pid( WORD pid )
{
    return pid == XBF_PROP_CONTROL_FOREGROUND || pid == XBF_PROP_TEXTBLOCK_FOREGROUND || pid == XBF_PROP_ICONELEMENT_FOREGROUND || pid == XBF_PROP_TEXTELEMENT_FOREGROUND;
}

static BOOL uwp_is_border_pid( WORD pid )
{
    return pid == XBF_PROP_CONTROL_BORDER_BRUSH || pid == XBF_PROP_BORDER_BRUSH || pid == XBF_PROP_GRID_BORDER_BRUSH || pid == XBF_PROP_SHAPE_STROKE;
}

static void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                                WORD pid, int vt, double num, const WCHAR *sval,
                                const double vec4[4], BOOL is_res_ref )
{
    UINT32 argb = 0;

    if (sv)
    {
        if (sv->kind == UWP_RD_BRUSH)
        {
            if (pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR || pid == XBF_PROP_FALLBACK_COLOR) /* SolidColorBrush.Color / RevealBrush.Color */
            {
                if (vt == 0x08)
                {
                    sv->argb = (UINT32)num;
                    sv->has_argb = TRUE;
                }
                else if (sval && sval[0])
                {
                    if (uwp_parse_color_string( sval, &argb ))
                    {
                        sv->argb = argb;
                        sv->has_argb = TRUE;
                    }
                    else
                    {
                        lstrcpynW( sv->str_val, sval, ARRAY_SIZE(sv->str_val) );
                    }
                }
            }
            else if (pid == XBF_PROP_BRUSH_OPACITY) /* Brush.Opacity */
            {
                if (vt == 0x03 || vt == 0x04)
                    sv->opacity = num;
                else if (sval && sval[0])
                {
                    struct uwp_rd_entry *de = NULL;
                    if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                        de && de->kind == UWP_RD_DOUBLE)
                        sv->opacity = de->dval;
                }
            }
        }
        else if (sv->kind == UWP_RD_STYLE && pid == XBF_PROP_BASED_ON && sval && sval[0]) /* Style.BasedOn */
        {
            uwp_extract_resource_key( sval, sv->based_on, ARRAY_SIZE(sv->based_on) );
        }
        else if (sv->kind == UWP_RD_COLOR && pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR && vt == 0x08)
        {
            sv->argb = (UINT32)num;
            sv->has_argb = TRUE;
        }
    }

    if (!el) return;

    if (el->kind == UWP_KIND_BRUSH)
    {
        if (pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR || pid == XBF_PROP_FALLBACK_COLOR)
        {
            if (vt == 0x08)
            {
                el->brush_argb = (UINT32)num;
                el->has_brush_argb = TRUE;
            }
            else if (sval && sval[0])
            {
                if (uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                {
                    el->brush_argb = argb;
                    el->has_brush_argb = TRUE;
                }
                else
                {
                    lstrcpynW( el->brush_color_ref, sval, ARRAY_SIZE(el->brush_color_ref) );
                }
            }
            return;
        }
        if (pid == XBF_PROP_BRUSH_OPACITY)
        {
            if (vt == 0x03 || vt == 0x04) el->brush_opacity = num;
            return;
        }
    }

    if (el->kind == UWP_KIND_RESOURCE_DICT && pid == XBF_PROP_RESOURCE_DICTIONARY_SOURCE && sval && sval[0]) /* Source */
    {
        uwp_load_res_xbf_into_bucket( ctx ? ctx->base_xbf_dir : NULL, sval, uwp_rd_bucket_for_el( el, TRUE ) );
        return;
    }

    if (pid & 0x8000)
    {
        if (uwp_is_bg_pid( pid ))
        {
            if (vt == 0x08)
                uwp_el_set_bg_argb( el, (UINT32)num );
            else if (sval && sval[0] &&
                     uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                uwp_el_set_bg_argb( el, argb );
            return;
        }
        if (uwp_is_fg_pid( pid ))
        {
            if (vt == 0x08)
                uwp_el_set_fg_argb( el, (UINT32)num );
            else if (sval && sval[0] &&
                     uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                uwp_el_set_fg_argb( el, argb );
            return;
        }
        if (uwp_is_border_pid( pid ))
        {
            if (vt == 0x08)
                uwp_el_set_border_argb( el, (UINT32)num );
            else if (sval && sval[0] &&
                     uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                uwp_el_set_border_argb( el, argb );
            return;
        }

        switch (pid)
        {
        case XBF_PROP_STYLE: /* FrameworkElement.Style */
            if (sval && sval[0])
                uwp_apply_style_by_name( ctx, el, sval );
            break;
        case XBF_PROP_GRID_ROW: el->grid_row = (int)num; break;
        case XBF_PROP_GRID_COLUMN: el->grid_col = (int)num; break;
        case XBF_PROP_GRID_ROW_SPAN: el->grid_row_span = max( 1, (int)num ); break;
        case XBF_PROP_GRID_COLUMN_SPAN: el->grid_col_span = max( 1, (int)num ); break;
        case XBF_PROP_ROWDEF_HEIGHT: /* RowDefinition.Height */
        case XBF_PROP_COLUMNDEF_WIDTH: /* ColumnDefinition.Width */
            if (vt == 0x07) { el->grid_len_type = (int)vec4[0]; el->grid_len_val = vec4[1]; }
            else if (vt == 0x03 || vt == 0x04) { el->grid_len_type = 1; el->grid_len_val = num; }
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                {
                    el->grid_len_type = 1;
                    el->grid_len_val = de->dval;
                }
            }
            break;
        case XBF_PROP_COLUMNDEF_MAXWIDTH: /* ColumnDefinition.MaxWidth */
        case XBF_PROP_FRAMEWORKELEMENT_MAXHEIGHT: /* FrameworkElement.MaxWidth */
            if (vt == 0x03 || vt == 0x04) el->max_width = num;
            break;
        case XBF_PROP_COLUMNDEF_MINWIDTH: /* ColumnDefinition.MinWidth */
            if (vt == 0x03 || vt == 0x04) el->min_width = num;
            break;
        case XBF_PROP_ROWDEF_MAXHEIGHT: /* RowDefinition.MaxHeight */
        case XBF_PROP_FRAMEWORKELEMENT_MAXWIDTH: /* FrameworkElement.MaxHeight */
            if (vt == 0x03 || vt == 0x04) el->max_height = num;
            break;
        case XBF_PROP_ROWDEF_MINHEIGHT: /* RowDefinition.MinHeight */
            if (vt == 0x03 || vt == 0x04) el->min_height = num;
            break;
        case XBF_PROP_MARGIN: /* Margin */
            if (vt == 0x06)
            {
                el->margin_l = vec4[0]; el->margin_t = vec4[1];
                el->margin_r = vec4[2]; el->margin_b = vec4[3];
            }
            break;
        case XBF_PROP_CONTROL_PADDING: /* Control.Padding */
        case XBF_PROP_PANEL_IS_IGNORING_TRANSITIONS: /* Border.Padding */
            if (vt == 0x06)
            {
                el->padding_l = vec4[0]; el->padding_t = vec4[1];
                el->padding_r = vec4[2]; el->padding_b = vec4[3];
            }
            break;
        case XBF_PROP_CONTROL_BORDER_THICKNESS: /* Control.BorderThickness */
        case XBF_PROP_PANEL_CHILDREN_TRANSITIONS: /* Border.BorderThickness */
            if (vt == 0x06)
            {
                el->border_thick_l = vec4[0]; el->border_thick_t = vec4[1];
                el->border_thick_r = vec4[2]; el->border_thick_b = vec4[3];
            }
            break;
        case XBF_PROP_CONTENT: /* ContentControl.Content */
        case XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV: /* TextBlock.Text */
        case XBF_PROP_UIELEMENT_EXIT_DISPLAY_MODE: /* FontIcon.Glyph */
            if (sval && sval[0])
                lstrcpynW( el->text, sval, ARRAY_SIZE(el->text) );
            break;
        case XBF_PROP_IMAGE_SOURCE: /* Image.Source */
            if (sval && sval[0])
                lstrcpynW( el->image_src, sval, ARRAY_SIZE(el->image_src) );
            break;
        case XBF_PROP_AUTOMATION_ID: /* x:Uid or Name */
            if (sval && sval[0])
            {
                lstrcpynW( el->uid, sval, ARRAY_SIZE(el->uid) );
                if (!el->text[0])
                {
                    WCHAR res_key[256], res_val[128];
                    static const WCHAR *std_props[] = { L"Content", L"Text" };
                    unsigned int sp;
                    DWORD k;

                    for (sp = 0; sp < ARRAY_SIZE(std_props) && !el->text[0]; sp++)
                    {
                        swprintf( res_key, ARRAY_SIZE(res_key), L"%s/%s", sval, std_props[sp] );
                        if (uwp_pri_lookup_string( res_key, res_val, ARRAY_SIZE(res_val) ) && res_val[0])
                            lstrcpynW( el->text, res_val, ARRAY_SIZE(el->text) );
                    }
                    for (k = 0; k < ctx->prop_cnt && !el->text[0]; k++)
                    {
                        const struct xbf_prop_info *kpi = &ctx->props[k];
                        if (kpi->owner_tid < ctx->type_cnt &&
                            !wcsncmp( ctx->types[kpi->owner_tid].xmlns, L"using:", 6 ) &&
                            (!wcscmp( kpi->name, L"Character" ) ||
                             !wcscmp( kpi->name, L"Content" ) ||
                             !wcscmp( kpi->name, L"Text" )))
                        {
                            swprintf( res_key, ARRAY_SIZE(res_key), L"%s/[%s]%s/%s",
                                      sval, ctx->types[kpi->owner_tid].xmlns,
                                      ctx->types[kpi->owner_tid].short_name, kpi->name );
                            if (uwp_pri_lookup_string( res_key, res_val, ARRAY_SIZE(res_val) ) && res_val[0])
                                lstrcpynW( el->text, res_val, ARRAY_SIZE(el->text) );
                        }
                    }
                }
            }
            break;
        case XBF_PROP_POPUP_HORIZONTAL_OFFSET: /* FontSize */
        case XBF_PROP_CONTROL_FONTSIZE: /* Control.FontSize */
        case XBF_PROP_TEXTBLOCK_FONTSIZE: /* TextBlock.FontSize */
            if (vt == 0x03 || vt == 0x04)
            {
                if (num > 0) el->font_size = num;
            }
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE && de->dval > 0)
                    el->font_size = de->dval;
            }
            break;
        case XBF_PROP_WIDTH: /* Width */
            if (vt == 0x03 || vt == 0x04) el->width = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->width = de->dval;
            }
            break;
        case XBF_PROP_FLOW_DIRECTION: /* Height */
        case XBF_PROP_FRAMEWORKELEMENT_HEIGHT: /* FrameworkElement.Height */
            if (vt == 0x03 || vt == 0x04) el->height = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->height = de->dval;
            }
            break;
        case XBF_PROP_MIN_WIDTH: /* MinWidth */
            if (vt == 0x03 || vt == 0x04) el->min_width = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->min_width = de->dval;
            }
            break;
        case XBF_PROP_MIN_HEIGHT: /* MinHeight */
            if (vt == 0x03 || vt == 0x04) el->min_height = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->min_height = de->dval;
            }
            break;
        case XBF_PROP_HORIZONTAL_ALIGNMENT: /* HorizontalAlignment */
            el->h_align = (int)num;
            break;
        case XBF_PROP_VERTICAL_ALIGNMENT: /* VerticalAlignment */
            el->v_align = (int)num;
            break;
        case XBF_PROP_ORIENTATION: /* StackPanel.Orientation */
            el->orientation = (int)num;
            break;
        case XBF_PROP_UIELEMENT_VISIBILITY_V2: /* UIElement.Visibility (XBF v2) */
        case XBF_PROP_UIELEMENT_VISIBILITY: /* UIElement.Visibility */
            el->visibility = (int)num;
            break;
        case XBF_PROP_ACCELERATOR_KEY: /* KeyboardAccelerator.Key */
            el->accel_key = (UINT)num;
            uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
            break;
        case XBF_PROP_ACCELERATOR_MODIFIERS: /* KeyboardAccelerator.Modifiers */
            el->accel_mods = (UINT)num;
            uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
            break;
        case XBF_PROP_ACCELERATOR_SCOPE_OWNER: /* KeyboardAccelerator.IsEnabled */
            el->accel_enabled = (num != 0.0);
            uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
            break;
        }
    }
    else if (ctx && pid < ctx->prop_cnt)
    {
        const struct xbf_prop_info *pi = &ctx->props[pid];
        if (!wcscmp( pi->name, L"Name" ) && sval && sval[0])
        {
            lstrcpynW( el->text, sval, ARRAY_SIZE(el->text) );
        }
        else if ((!wcscmp( pi->name, L"VirtualKeyControlChord" ) ||
                  !wcscmp( pi->name, L"VirtualKeyAltChord" ) ||
                  !wcscmp( pi->name, L"VirtualKeyShiftChord" ) ||
                  !wcscmp( pi->name, L"VirtualKeyControlShiftChord" )) && sval && sval[0])
        {
            UINT vk = uwp_parse_vk_string( sval );
            UINT mods = !wcscmp( pi->name, L"VirtualKeyControlChord" ) ? 1 :
                        (!wcscmp( pi->name, L"VirtualKeyAltChord" ) ? 2 :
                         (!wcscmp( pi->name, L"VirtualKeyShiftChord" ) ? 4 : 5));
            if (vk)
                uwp_accel_register( NULL, el, vk, mods, TRUE );
        }
        else if (!wcscmp( pi->name, L"ButtonId" ) && sval && sval[0])
        {
            IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;
            const WCHAR *owner_cls = (pi->owner_tid < ctx->type_cnt && ctx->types[pi->owner_tid].full_name[0])
                ? ctx->types[pi->owner_tid].full_name
                : (el->class_name[0] ? el->class_name : NULL);
            if (owner_cls)
                uwp_set_custom_xaml_member( ctx->app_outer, owner_cls, L"ButtonId", target, sval );
            /* Fallback label from ButtonId if button still has no text */
            if (!el->text[0])
            {
                if (!wcscmp( sval, L"Zero" )) lstrcpyW( el->text, L"0" );
                else if (!wcscmp( sval, L"One" )) lstrcpyW( el->text, L"1" );
                else if (!wcscmp( sval, L"Two" )) lstrcpyW( el->text, L"2" );
                else if (!wcscmp( sval, L"Three" )) lstrcpyW( el->text, L"3" );
                else if (!wcscmp( sval, L"Four" )) lstrcpyW( el->text, L"4" );
                else if (!wcscmp( sval, L"Five" )) lstrcpyW( el->text, L"5" );
                else if (!wcscmp( sval, L"Six" )) lstrcpyW( el->text, L"6" );
                else if (!wcscmp( sval, L"Seven" )) lstrcpyW( el->text, L"7" );
                else if (!wcscmp( sval, L"Eight" )) lstrcpyW( el->text, L"8" );
                else if (!wcscmp( sval, L"Nine" )) lstrcpyW( el->text, L"9" );
                else if (!wcscmp( sval, L"Decimal" )) lstrcpyW( el->text, L"." );
                else if (!wcscmp( sval, L"Add" )) lstrcpyW( el->text, L"+" );
                else if (!wcscmp( sval, L"Subtract" )) lstrcpyW( el->text, L"-" );
                else if (!wcscmp( sval, L"Multiply" )) lstrcpyW( el->text, L"\x00d7" );
                else if (!wcscmp( sval, L"Divide" )) lstrcpyW( el->text, L"\x00f7" );
                else if (!wcscmp( sval, L"Equals" )) lstrcpyW( el->text, L"=" );
                else if (!wcscmp( sval, L"Clear" )) lstrcpyW( el->text, L"C" );
                else if (!wcscmp( sval, L"ClearEntry" )) lstrcpyW( el->text, L"CE" );
                else if (!wcscmp( sval, L"Backspace" )) lstrcpyW( el->text, L"\x232b" );
                else if (!wcscmp( sval, L"Negate" )) lstrcpyW( el->text, L"+/-" );
                else if (!wcscmp( sval, L"Percent" )) lstrcpyW( el->text, L"%" );
                else if (!wcscmp( sval, L"Invert" )) lstrcpyW( el->text, L"1/x" );
                else if (!wcscmp( sval, L"Power" )) lstrcpyW( el->text, L"x\x00b2" );
                else if (!wcscmp( sval, L"Sqrt" )) lstrcpyW( el->text, L"\x221a" );
            }
        }
    }
}

static void xbf_parse_style_setter_sub( struct xbf_ctx *ctx, int sub_idx, DWORD off, WORD target_pid,
                                        struct xbf_stack_val *style_sv )
{
    DWORD sub_start = 0, end_p = 0, sub_sz = 0, p;
    if (!ctx || !style_sv || sub_idx < 0) return;
    if (!xbf_get_substream_bounds( ctx, (DWORD)sub_idx, &sub_start, &end_p, &sub_sz )) return;
    if (off >= sub_sz) return;
    p = sub_start + off;
    if (p < end_p)
    {
        BYTE op = ctx->nodes[p++];
        if ((op == 0x24 || op == 0x1e || op == 0x1a) && p + 2 <= end_p)
        {
            WORD vpid = *(const WORD *)(ctx->nodes + p);
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            (void)vpid;
            p += 2;
            if (xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
                xbf_stack_val_add_setter( style_sv, target_pid, vt, (op == 0x24 || op == 0x1e), num, sbuf, vec4 );
        }
        else if (op == 0x14 && p + 2 <= end_p)
        {
            WORD tid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (tid == XBF_TYPE_BRUSH_C) /* inline SolidColorBrush */
            {
                UINT32 argb = 0;
                BOOL has_c = FALSE;
                WCHAR cref[128] = {0};
                while (p < end_p)
                {
                    BYTE sop = ctx->nodes[p++];
                    if (sop == 0x21 || sop == 0x02) break;
                    if ((sop == 0x1a || sop == 0x1e || sop == 0x24) && p + 2 <= end_p)
                    {
                        WORD spid = *(const WORD *)(ctx->nodes + p);
                        int vt = 0;
                        double num = 0;
                        WCHAR sbuf[128] = {0};
                        p += 2;
                        if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
                        if (spid == XBF_PROP_SOLIDCOLORBRUSH_COLOR)
                        {
                            if (vt == 0x08) { argb = (UINT32)num; has_c = TRUE; }
                            else if (sbuf[0]) lstrcpynW( cref, sbuf, ARRAY_SIZE(cref) );
                        }
                    }
                    else break;
                }
                if (has_c)
                    xbf_stack_val_add_setter( style_sv, target_pid, 0x08, FALSE, (double)argb, NULL, NULL );
                else if (cref[0])
                    xbf_stack_val_add_setter( style_sv, target_pid, 0x05, TRUE, 0, cref, NULL );
            }
        }
    }
}

static BOOL xbf_read_vsm_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                                       DWORD def_sub_sz, int *io_prop_sp )
{
    DWORD p = *io_p, n_states = 0, s_idx;
    if (!xbf_read_uleb( nodes, &p, end_p, &n_states ) || n_states > end_p - p || n_states > def_sub_sz)
        return FALSE;
    for (s_idx = 0; s_idx < n_states; s_idx++)
    {
        DWORD st_off = 0;
        if (!xbf_read_uleb( nodes, &p, end_p, &st_off ) || st_off > def_sub_sz)
            return FALSE;
    }
    /* Find VisualStateGroups footer: 00 02 00 00 01 <cnt_uleb> <cnt*u16> 02 */
    while (p < end_p)
    {
        if (end_p - p >= 7 &&
            nodes[p] == 0x00 && nodes[p + 1] == 0x02 &&
            nodes[p + 2] == 0x00 && nodes[p + 3] == 0x00 &&
            nodes[p + 4] == 0x01)
        {
            DWORD q = p + 5, cnt = 0;
            if (xbf_read_uleb( nodes, &q, end_p, &cnt ) &&
                cnt >= n_states && cnt <= (end_p - q) / 2 &&
                q + cnt * 2 < end_p && nodes[q + cnt * 2] == 0x02)
            {
                p = q + cnt * 2 + 1;
                if (io_prop_sp && *io_prop_sp > 0) (*io_prop_sp)--;
                *io_p = p;
                return TRUE;
            }
        }
        p++;
    }
    *io_p = p;
    return TRUE;
}

static BOOL xbf_read_style_defer_table( struct xbf_ctx *ctx, DWORD *io_p, DWORD end_p,
                                        DWORD def_sub, DWORD def_sub_sz,
                                        struct xbf_stack_val *style_sv, int *io_prop_sp )
{
    DWORD p = *io_p, scnt = 0, si;
    if (!xbf_read_uleb( ctx->nodes, &p, end_p, &scnt ) || scnt > (end_p - p) / 3)
        return FALSE;
    for (si = 0; si < scnt; si++)
    {
        BYTE sop;
        if (p >= end_p) return FALSE;
        sop = ctx->nodes[p];
        if (sop == 0x30)
        {
            WORD spid;
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            if (end_p - p < 3) return FALSE;
            spid = *(const WORD *)(ctx->nodes + p + 1);
            p += 3;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
                return FALSE;
            xbf_stack_val_add_setter( style_sv, spid, vt, FALSE, num, sbuf, vec4 );
        }
        else if (sop == 0x11 || sop == 0x12 || sop == 0x18)
        {
            WORD spid;
            DWORD soff = 0;
            if (end_p - p < 3) return FALSE;
            spid = *(const WORD *)(ctx->nodes + p + 1);
            p += 3;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &soff ) || soff > def_sub_sz)
                return FALSE;
            if ((sop == 0x11 || sop == 0x12) && soff < def_sub_sz)
                xbf_parse_style_setter_sub( ctx, (int)def_sub, soff, spid, style_sv );
        }
        else if (sop == 0x01 || sop == 0x02 || sop == 0x20)
        {
            DWORD soff = 0;
            if (end_p - p < 5) return FALSE;
            p += 5;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &soff ) || soff > def_sub_sz)
                return FALSE;
        }
        else
        {
            break;
        }
    }
    while (p < end_p)
    {
        if (ctx->nodes[p] == 0x21 ||
            (ctx->nodes[p] == 0x02 && (p + 1 == end_p || ctx->nodes[p + 1] == 0x13 || ctx->nodes[p + 1] == 0x14)))
        {
            if (ctx->nodes[p] == 0x02)
            {
                p++;
                if (io_prop_sp && *io_prop_sp > 0) (*io_prop_sp)--;
            }
            break;
        }
        p++;
    }
    *io_p = p;
    return TRUE;
}

static BOOL xbf_read_rd_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                                      DWORD def_sub_sz, struct xbf_rd_table *tbl, int *io_prop_sp )
{
    DWORD p = *io_p, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0, c6 = 0, ri, ki;

    if (!xbf_read_uleb( nodes, &p, end_p, &c1 ) || c1 >= 10000 || c1 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c1; ri++)
    {
        WORD sid;
        DWORD off = 0;
        if (end_p - p < 2) return FALSE;
        sid = *(const WORD *)(nodes + p);
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &off ) || off > def_sub_sz)
            return FALSE;
        if (!xaml_vec_grow( (void **)&tbl->items, &tbl->cap, tbl->count + 1, sizeof(*tbl->items), 32 ))
            return FALSE;
        tbl->items[tbl->count].key_sid = sid;
        tbl->items[tbl->count].sub_off = off;
        tbl->count++;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c2 ) || c2 > (end_p - p) / 2)
        return FALSE;
    p += c2 * 2;

    if (!xbf_read_uleb( nodes, &p, end_p, &c3 ) || c3 >= 10000 || c3 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c3; ri++)
    {
        WORD sid;
        DWORD off = 0;
        if (end_p - p < 2) return FALSE;
        sid = *(const WORD *)(nodes + p);
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &off ) || off > def_sub_sz)
            return FALSE;
        if (!xaml_vec_grow( (void **)&tbl->items, &tbl->cap, tbl->count + 1, sizeof(*tbl->items), 32 ))
            return FALSE;
        tbl->items[tbl->count].key_sid = sid;
        tbl->items[tbl->count].sub_off = off;
        tbl->count++;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c4 ) || c4 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c4; ri++)
    {
        DWORD kc = 0, dummy = 0;
        if (end_p - p < 2) return FALSE;
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &kc ) || kc > end_p - p)
            return FALSE;
        for (ki = 0; ki < kc; ki++)
            if (!xbf_read_uleb( nodes, &p, end_p, &dummy )) return FALSE;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c5 ) || c5 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c5; ri++)
    {
        DWORD kc = 0, dummy = 0;
        if (end_p - p < 2) return FALSE;
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &kc ) || kc > end_p - p)
            return FALSE;
        for (ki = 0; ki < kc; ki++)
            if (!xbf_read_uleb( nodes, &p, end_p, &dummy )) return FALSE;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c6 ) || c6 > (end_p - p) / 6)
        return FALSE;
    for (ri = 0; ri < c6; ri++)
    {
        DWORD dummy = 0;
        if (!xbf_read_uleb( nodes, &p, end_p, &dummy ) || end_p - p < 5)
            return FALSE;
        p += 5;
    }

    if (p < end_p && nodes[p] == 0x02)
    {
        p++;
        if (io_prop_sp && *io_prop_sp > 0) (*io_prop_sp)--;
    }
    *io_p = p;
    return TRUE;
}

static void xbf_exec_substream_rd( struct xbf_ctx *ctx, int sub_idx, struct uwp_element *initial_root,
                                   struct uwp_element **out_created, const struct xbf_rd_table *rd_tbl,
                                   struct uwp_rd_bucket *active_rd )
{
    DWORD sub_start = 0, end_p = 0, sub_sz = 0, p;
    struct uwp_element *obj_stack[64];
    IInspectable *raw_stack[64];
    struct xbf_stack_val val_stack[64];
    DWORD start_off_stack[64];
    int obj_sp = 0, base_sp = 0;
    WORD prop_stack[64];
    int prop_sp = 0;
    struct xbf_stack_val last_popped;
    DWORD last_popped_off = 0;
    BOOL has_last_popped = FALSE;

    memset( val_stack, 0, sizeof(val_stack) );
    memset( &last_popped, 0, sizeof(last_popped) );
    memset( start_off_stack, 0, sizeof(start_off_stack) );

    if (out_created) *out_created = NULL;
    if (sub_idx < 0 || !xbf_get_substream_bounds( ctx, (DWORD)sub_idx, &sub_start, &end_p, &sub_sz ))
        return;
    if (ctx->sub_done)
    {
        if (ctx->sub_done[sub_idx]) return;
        ctx->sub_done[sub_idx] = TRUE;
    }

    p = sub_start;

    if (initial_root)
    {
        obj_stack[0] = initial_root;
        raw_stack[0] = initial_root->outer ? initial_root->outer : &initial_root->IInspectable_iface;
        obj_sp = 1;
        base_sp = 1;
    }

    while (p < end_p)
    {
        DWORD cur_rel_off = p - sub_start;
        BYTE op = ctx->nodes[p++];
        if (op == 0x12) /* LineInfo (6 bytes) */
        {
            if (end_p - p < 6) break;
            p += 6;
        }
        else if (op == 0x03) /* Xmlns prefix */
        {
            DWORD plen;
            if (end_p - p < 6) break;
            plen = *(const DWORD *)(ctx->nodes + p + 2);
            if (plen > (end_p - (p + 6)) / 2) break;
            p += 6 + plen * 2;
        }
        else if (op == 0x17 || op == 0x14 || op == 0x15 || op == 0x16)
        {
            WORD tid;
            const WCHAR *cls_name = L"Windows.UI.Xaml.Controls.ContentControl";
            IInspectable *inst = NULL;
            struct uwp_element *el = NULL;
            struct xbf_stack_val sv = {0};
            int vt_init = 0;
            double num_init = 0, vec4_init[4] = {0};
            WCHAR sbuf_init[256] = {0};
            BOOL is_rd_type = FALSE;

            sv.opacity = 1.0;
            if (p + 2 > end_p) break;
            tid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (op == 0x15 || op == 0x16)
            {
                if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt_init, &num_init, sbuf_init, ARRAY_SIZE(sbuf_init), vec4_init ))
                    break;
            }
            TRACE( "XBF push op=%#x tid=%#x cls=%s at %lu/%lu (sub %d)\n", op, tid,
                   (tid < ctx->type_cnt) ? debugstr_w(ctx->types[tid].full_name) : "(btype)",
                   (unsigned long)cur_rel_off, (unsigned long)end_p, sub_idx );

            if (tid == XBF_TYPE_RESOURCE_DICTIONARY) is_rd_type = TRUE;
            else if (!(tid & 0x8000) && tid < ctx->type_cnt &&
                     (wcsstr( ctx->types[tid].short_name, L"ResourceDictionary" ) ||
                      !wcscmp( ctx->types[tid].short_name, L"XamlControlsResources" )))
                is_rd_type = TRUE;

            if (tid == XBF_TYPE_COLOR)
            {
                sv.kind = UWP_RD_COLOR;
                if (vt_init == 0x08) { sv.argb = (UINT32)num_init; sv.has_argb = TRUE; }
                else if (sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv.argb )) sv.has_argb = TRUE;
            }
            else if (tid == XBF_TYPE_DOUBLE)
            {
                sv.kind = UWP_RD_DOUBLE;
                sv.dval = num_init;
            }
            else if (tid == XBF_TYPE_GRID_LENGTH)
            {
                sv.kind = UWP_RD_DOUBLE;
                sv.dval = (vt_init == 0x07) ? vec4_init[1] : num_init;
            }
            else if (tid == XBF_TYPE_THICKNESS)
            {
                sv.kind = UWP_RD_THICKNESS;
                memcpy( sv.vec4, vec4_init, sizeof(sv.vec4) );
            }
            else if (tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_BRUSH_A || tid == XBF_TYPE_BRUSH_B ||
                     (!(tid & 0x8000) && tid < ctx->type_cnt && wcsstr( ctx->types[tid].short_name, L"Brush" )))
            {
                sv.kind = UWP_RD_BRUSH;
                if (op == 0x16 && sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv.argb ))
                    sv.has_argb = TRUE;
            }
            else if (tid == XBF_TYPE_STYLE)
            {
                sv.kind = UWP_RD_STYLE;
            }
            else if ((op == 0x15 || op == 0x16) && sbuf_init[0])
            {
                sv.kind = UWP_RD_STRING;
                lstrcpynW( sv.str_val, sbuf_init, ARRAY_SIZE(sv.str_val) );
            }

            if (op == 0x17 && ctx->root_el)
            {
                el = ctx->root_el;
                inst = ctx->root_comp;
                if (is_rd_type && active_rd)
                {
                    el->rd_bucket = active_rd;
                    active_rd->owner_el = el;
                }
            }
            else if ((ctx->rd_only || rd_tbl) && !is_rd_type &&
                     (!ctx->connector ||
                      tid == XBF_TYPE_STYLE || tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_SETTER_A || tid == XBF_TYPE_SETTER_B ||
                      tid == XBF_TYPE_BRUSH_A || tid == XBF_TYPE_BRUSH_B || tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_STRING ||
                      tid == XBF_TYPE_THICKNESS || tid == XBF_TYPE_COLUMN_DEFINITION || tid == XBF_TYPE_RESOURCE_DICTIONARY))
            {
                /* Lightweight mode inside ResourceDictionary XBFs: no COM/visual element needed */
                el = NULL;
                inst = NULL;
            }
            else
            {
                if (tid & 0x8000)
                {
                    cls_name = uwp_btype_to_class_name( tid );
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                    if (el && tid == XBF_TYPE_DEFERRED_ELEMENT) el->kind = UWP_KIND_DEFERRED;
                }
                else if (tid < ctx->type_cnt)
                {
                    cls_name = ctx->types[tid].full_name;
                    if (ctx->root_el) uwp_mark_xbf_ref_instantiated( ctx->root_el, cls_name );
                    /* Do not activate ResourceString, ResourceVirtualKey, or Microsoft.UI.Xaml controls via provider */
                    if (is_rd_type || wcsstr( cls_name, L"ResourceString" ) ||
                        wcsstr( cls_name, L"ResourceVirtualKey" ) ||
                        !wcsncmp( cls_name, L"Microsoft.UI.Xaml.", 18 ))
                    {
                        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                        el = uwp_elem_from_any( inst );
                    }
                    else
                    {
                        struct uwp_element *prev_head;
                        HRESULT phr;
                        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                        prev_head = g_all_uwp_elems_head;
                        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                        phr = uwp_create_instance_from_provider( cls_name, (void **)&inst );
                        if (SUCCEEDED(phr) && inst && g_all_uwp_elems_head != prev_head)
                            el = uwp_elem_from_any( inst );
                        if (!el)
                        {
                            AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                            if (g_all_uwp_elems_head != prev_head)
                            {
                                struct uwp_element *e;
                                for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
                                {
                                    if (e->kind == UWP_KIND_USER_CONTROL || e->kind == UWP_KIND_PAGE)
                                        el = e;
                                }
                                if (!el)
                                {
                                    for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
                                        el = e;
                                }
                            }
                            ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                        }
                        if (el)
                        {
                            if (inst && !el->provider_inst) el->provider_inst = inst;
                            else if (!inst) inst = el->outer ? el->outer : &el->IInspectable_iface;
                            lstrcpynW( el->class_name, cls_name, ARRAY_SIZE(el->class_name) );
                            el->kind = classify_uwp_class( el->class_name );
                        }
                        if (FAILED(phr) || !inst)
                        {
                            if (!inst) uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                            if (!el) el = uwp_elem_from_any( inst );
                            if (el && !el->xbf_parsed && !el->children_coll && !el->content &&
                                !wcsncmp( ctx->types[tid].xmlns, L"using:", 6 ))
                            {
                                uwp_populate_element_from_pri_xbf( el, ctx->types[tid].short_name );
                            }
                        }
                    }
                }
                else
                {
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                }
            }

            if (el)
            {
                if (ctx->root_el) el->owner_component = ctx->root_el;
                if (sv.kind == UWP_RD_BRUSH && sv.has_argb)
                {
                    el->brush_argb = sv.argb;
                    el->has_brush_argb = TRUE;
                }
                if (is_rd_type)
                {
                    if (op == 0x17 && active_rd)
                    {
                        el->rd_bucket = active_rd;
                        active_rd->owner_el = el;
                    }
                    else
                    {
                        uwp_rd_bucket_for_el( el, TRUE );
                    }
                }
            }
            sv.tid = tid;
            sv.el = el;
            sv.raw = inst;
            if (out_created && !*out_created && el) *out_created = el;

            if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el)
            {
                if (tid == XBF_TYPE_VISUAL_STATE_GROUP)
                    uwp_vsm_add_group( ctx->root_el, NULL );
                else if (tid == XBF_TYPE_VISUAL_STATE)
                {
                    struct uwp_vsm_group *grp = (ctx->root_el->vsm_group_cnt > 0)
                        ? &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1]
                        : uwp_vsm_add_group( ctx->root_el, L"DefaultGroup" );
                    uwp_vsm_add_state( grp, NULL );
                }
            }

            if (obj_sp < (int)ARRAY_SIZE(obj_stack))
            {
                obj_stack[obj_sp] = el;
                raw_stack[obj_sp] = inst;
                val_stack[obj_sp] = sv;
                start_off_stack[obj_sp] = cur_rel_off;
                obj_sp++;
            }
            else
            {
                free( sv.setters );
            }
        }
        else if (op == 0x0b) /* x:Class */
        {
            DWORD slen;
            if (end_p - p < 4) break;
            slen = *(const DWORD *)(ctx->nodes + p);
            p += 4;
            if (slen > (end_p - p) / 2) break;
            if (obj_sp > 0 && obj_stack[obj_sp - 1] && slen > 0)
            {
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                DWORD cp = min( slen, (DWORD)(ARRAY_SIZE(top_el->x_class) - 1) );
                memcpy( top_el->x_class, ctx->nodes + p, cp * sizeof(WCHAR) );
                top_el->x_class[cp] = 0;
            }
            p += slen * 2;
        }
        else if (op == 0x0c) /* ConnectionId */
        {
            int vt = 0;
            double num = 0;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, NULL, 0, NULL )) break;
            if (obj_sp > 0)
            {
                int cid = (int)num;
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                IInspectable *top_raw = raw_stack[obj_sp - 1];
                if (top_el) top_el->conn_id = cid;
                if (ctx->connector && top_raw && cid != 1)
                {
                    HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)ctx->connector)[6];
                    TRACE( "XBF Connect(cid=%d, target=%p [%s %s])\n",
                           cid, top_raw, top_el ? debugstr_w(top_el->class_name) : "?",
                           top_el ? debugstr_w(top_el->name) : "" );
                    conn_fn( ctx->connector, cid, top_raw );
                    if (ctx->root_el && ctx->root_el->binding_connector)
                    {
                        HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)ctx->root_el->binding_connector)[6];
                        if (b_conn) b_conn( ctx->root_el->binding_connector, cid, top_raw );
                    }
                }
            }
        }
        else if (op == 0x0d) /* x:Name */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            if (obj_sp > 0 && sbuf[0])
            {
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                WORD top_tid = val_stack[obj_sp - 1].tid;
                if (top_el)
                {
                    lstrcpynW( top_el->name, sbuf, ARRAY_SIZE(top_el->name) );
                    if (ctx->root_el &&
                        xaml_vec_grow( (void **)&ctx->root_el->named_elems, &ctx->root_el->named_cap,
                                       ctx->root_el->named_cnt + 1, sizeof(*ctx->root_el->named_elems), 32 ))
                    {
                        ctx->root_el->named_elems[ctx->root_el->named_cnt++] = top_el;
                    }
                }
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    if (top_tid == XBF_TYPE_VISUAL_STATE_GROUP)
                        lstrcpynW( grp->name, sbuf, ARRAY_SIZE(grp->name) );
                    else if (top_tid == XBF_TYPE_VISUAL_STATE && grp->state_cnt > 0)
                        lstrcpynW( grp->states[grp->state_cnt - 1].name, sbuf, ARRAY_SIZE(grp->states[grp->state_cnt - 1].name) );
                }
            }
        }
        else if (op == 0x0e) /* x:Key */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
        }
        else if (op == 0x13) /* PushProperty */
        {
            WORD pid;
            if (p + 2 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (prop_sp < (int)ARRAY_SIZE(prop_stack))
                prop_stack[prop_sp++] = pid;
        }
        else if (op == 0x01 || op == 0x27) /* EndOfAttributes / EndConditionalScope */
        {
        }
        else if (op == 0x26) /* BeginConditionalScope <pred_tid:u16> <arg_sid:u16> */
        {
            if (p + 4 > end_p) break;
            p += 4;
        }
        else if (op == 0x11) /* SetDeferredProperty <pid:u16> <sub_idx:uleb> <c1:uleb> <c2:uleb> <(c1+c2)*u16> */
        {
            DWORD sub_i = 0, def_sz = 0, c1 = 0, c2 = 0;
            if (end_p - p < 2) break;
            p += 2;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &sub_i )) break;
            if (!xbf_get_substream_bounds( ctx, sub_i, NULL, NULL, &def_sz )) break;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c1 )) break;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c2 )) break;
            (void)def_sz;
            if (c1 > (end_p - p) / 2 || c2 > (end_p - p) / 2 - c1) break;
            p += (c1 + c2) * 2;
        }
        else if (op == 0x02) /* PopProperty */
        {
            if (prop_sp > 0) prop_sp--;
        }
        else if (op == 0x1d) /* SetValueWithType: <pid:u16> <tid:u16> */
        {
            WORD pid, tid;
            if (p + 4 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            tid = *(const WORD *)(ctx->nodes + p + 2);
            (void)pid; (void)tid;
            p += 4;
        }
        else if ((op >= 0x1a && op <= 0x1f) || op == 0x24) /* SetValue / SetStaticResource / SetThemeResource */
        {
            WORD pid;
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            BOOL is_res_ref = (op == 0x1e || op == 0x24);
            if (p + 2 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 )) break;
            if (obj_sp > 0)
            {
                struct xbf_stack_val *top_sv = &val_stack[obj_sp - 1];
                xbf_apply_property( ctx, obj_stack[obj_sp - 1], top_sv,
                                    pid, vt, num, sbuf, vec4, is_res_ref );
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    struct uwp_vsm_state *st = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
                    if (top_sv->tid == XBF_TYPE_ADAPTIVE_TRIGGER && st && (pid == XBF_PROP_MIN_WINDOW_HEIGHT || pid == XBF_PROP_MIN_WINDOW_WIDTH))
                    {
                        double trig_val = num;
                        if ((is_res_ref || vt == 0x05 || vt == 0x09) && sbuf[0])
                        {
                            struct uwp_rd_entry *de = NULL;
                            if (uwp_rd_find_entry( active_rd ? active_rd : ctx->target_rd, ctx->root_el, sbuf, 0, &de, NULL ) &&
                                de && de->kind == UWP_RD_DOUBLE)
                                trig_val = de->dval;
                            else
                                trig_val = wcstod( sbuf, NULL );
                        }
                        if (pid == XBF_PROP_MIN_WINDOW_HEIGHT) st->min_window_height = trig_val;
                        else st->min_window_width = trig_val;
                        st->has_trigger = TRUE;
                    }
                    else if ((top_sv->tid == XBF_TYPE_SETTER_A || top_sv->tid == XBF_TYPE_SETTER_B) && st)
                    {
                        if (pid == XBF_PROP_SETTER_TARGET && sbuf[0])
                            vsm_split_setter_target( sbuf, top_sv->vsm_target, ARRAY_SIZE(top_sv->vsm_target),
                                                     top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
                        else if (pid == XBF_PROP_SETTER_VALUE)
                        {
                            lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                            top_sv->vsm_vt = vt;
                            top_sv->vsm_num = num;
                            top_sv->vsm_is_res_ref = is_res_ref;
                            top_sv->vsm_has_val = TRUE;
                        }
                    }
                    else if (top_sv->tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES && st)
                    {
                        if (pid == XBF_PROP_STORYBOARD_TARGET_NAME && sbuf[0])
                            lstrcpynW( top_sv->vsm_target, sbuf, ARRAY_SIZE(top_sv->vsm_target) );
                        else if (pid == XBF_PROP_STORYBOARD_TARGET_PROPERTY && sbuf[0])
                            vsm_clean_prop_name( sbuf, top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
                    }
                    else if (top_sv->tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && st && pid == XBF_PROP_KEYFRAME_VALUE)
                    {
                        lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                        top_sv->vsm_vt = vt;
                        top_sv->vsm_num = num;
                        top_sv->vsm_is_res_ref = is_res_ref;
                        top_sv->vsm_has_val = TRUE;
                    }
                }
            }
        }
        else if (op == 0x22) /* AddStaticResourceValue <xbf_value> */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped.kind = UWP_RD_ALIAS;
            last_popped.opacity = 1.0;
            lstrcpynW( last_popped.str_val, sbuf, ARRAY_SIZE(last_popped.str_val) );
            last_popped_off = cur_rel_off;
            has_last_popped = TRUE;
        }
        else if (op == 0x21) /* EndOfObject / PopObject */
        {
            if (obj_sp > base_sp)
            {
                free( last_popped.setters );
                obj_sp--;
                last_popped = val_stack[obj_sp];
                last_popped_off = start_off_stack[obj_sp];
                memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) );
                has_last_popped = TRUE;

                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    struct uwp_vsm_state *st = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
                    if ((last_popped.tid == XBF_TYPE_SETTER_A || last_popped.tid == XBF_TYPE_SETTER_B) && st &&
                        last_popped.vsm_target[0] && last_popped.vsm_prop[0] && last_popped.vsm_has_val)
                    {
                        uwp_vsm_state_add_setter( st, last_popped.vsm_target, last_popped.vsm_prop,
                                                  last_popped.vsm_val_str, last_popped.vsm_vt,
                                                  last_popped.vsm_num, last_popped.vsm_is_res_ref );
                    }
                    else if (last_popped.tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && st && last_popped.vsm_has_val &&
                             obj_sp > 0 && val_stack[obj_sp - 1].tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES &&
                             val_stack[obj_sp - 1].vsm_target[0] && val_stack[obj_sp - 1].vsm_prop[0])
                    {
                        uwp_vsm_state_add_setter( st, val_stack[obj_sp - 1].vsm_target,
                                                  val_stack[obj_sp - 1].vsm_prop,
                                                  last_popped.vsm_val_str, last_popped.vsm_vt,
                                                  last_popped.vsm_num, last_popped.vsm_is_res_ref );
                    }
                }

                /* Indexed ResourceDictionary entry without trailing 0x0a AddToDictionary (e.g. Dark.xbf) */
                if (active_rd && rd_tbl && obj_sp == base_sp)
                {
                    BYTE next_op = (p < end_p) ? ctx->nodes[p] : 0xff;
                    if (next_op != 0x0a && next_op != 0x07 && next_op != 0x08 && next_op != 0x09 && next_op != 0x20)
                    {
                        int k;
                        for (k = 0; k < rd_tbl->count; k++)
                        {
                            if (rd_tbl->items[k].sub_off == last_popped_off &&
                                rd_tbl->items[k].key_sid < ctx->str_cnt && ctx->strs[rd_tbl->items[k].key_sid])
                            {
                                uwp_rd_store_stack_val( active_rd, ctx->strs[rd_tbl->items[k].key_sid], &last_popped );
                            }
                        }
                        free( last_popped.setters );
                        memset( &last_popped, 0, sizeof(last_popped) );
                        has_last_popped = FALSE;
                    }
                }
            }
            else
            {
                free( last_popped.setters );
                memset( &last_popped, 0, sizeof(last_popped) );
                has_last_popped = FALSE;
            }
        }
        else if (op == 0x0a) /* AddToDictionary <xbf_value> */
        {
            int vt = 0;
            double num = 0;
            WCHAR kbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, kbuf, ARRAY_SIZE(kbuf), NULL )) break;
            if (has_last_popped && kbuf[0])
            {
                struct uwp_element *parent_el = (obj_sp > 0) ? obj_stack[obj_sp - 1] : NULL;
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : 0;
                if (pid == XBF_PROP_THEME_DICTIONARIES && last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT)
                {
                    struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                    struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    uwp_rd_link_theme( parent_rd, kbuf, child_rd );
                }
                else
                {
                    struct uwp_rd_bucket *dest_rd = NULL;
                    if (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        dest_rd = uwp_rd_bucket_for_el( parent_el, TRUE );
                    else
                        dest_rd = active_rd ? active_rd : (parent_el ? uwp_rd_bucket_for_el( parent_el, TRUE ) : NULL);
                    if (dest_rd)
                    {
                        uwp_rd_store_stack_val( dest_rd, kbuf, &last_popped );
                        if (active_rd && active_rd != dest_rd)
                            uwp_rd_store_stack_val( active_rd, kbuf, &last_popped );
                    }
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            has_last_popped = FALSE;
        }
        else if (op == 0x08) /* AddToCollection */
        {
            if (has_last_popped && obj_sp > 0)
            {
                struct uwp_element *parent_el = obj_stack[obj_sp - 1];
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : XBF_PROP_PANEL_CHILDREN;
                if (pid == XBF_PROP_MERGED_DICTIONARIES && last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT)
                {
                    struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                    struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    uwp_rd_link_merged( parent_rd, child_rd );
                }
                else if (parent_el && last_popped.raw)
                {
                    if (pid == XBF_PROP_KEYBOARD_ACCELERATORS || (last_popped.el && (last_popped.el->kind == UWP_KIND_ACCELERATOR ||
                                                             wcsstr( last_popped.el->class_name, L"Accelerator" ))))
                    {
                        if (!parent_el->accelerators_coll) uwp_vec_create( parent_el, &parent_el->accelerators_coll );
                        uwp_vec_Append( parent_el->accelerators_coll, last_popped.raw );
                        if (last_popped.el)
                        {
                            last_popped.el->accel_target = parent_el;
                            uwp_accel_register( last_popped.el, parent_el,
                                                last_popped.el->accel_key, last_popped.el->accel_mods,
                                                last_popped.el->accel_enabled );
                        }
                    }
                    else if (pid == XBF_PROP_GRID_ROW_DEFINITIONS) /* RowDefinitions */
                    {
                        if (!parent_el->row_defs_coll) uwp_vec_create( parent_el, &parent_el->row_defs_coll );
                        uwp_vec_Append( parent_el->row_defs_coll, last_popped.raw );
                    }
                    else if (pid == XBF_PROP_GRID_COLUMN_DEFINITIONS) /* ColumnDefinitions */
                    {
                        if (!parent_el->col_defs_coll) uwp_vec_create( parent_el, &parent_el->col_defs_coll );
                        uwp_vec_Append( parent_el->col_defs_coll, last_popped.raw );
                    }
                    else if (pid == XBF_PROP_PANEL_CHILDREN || pid == XBF_PROP_ITEMS || parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL)
                    {
                        if (!parent_el->children_coll) uwp_vec_create( parent_el, &parent_el->children_coll );
                        uwp_vec_Append( parent_el->children_coll, last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            has_last_popped = FALSE;
        }
        else if (op == 0x07) /* SetObjectProperty <pid:u16> */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            TRACE( "xbf-trace: op=0x07 pid=%#x has_last=%d last_el=%p\n",
                   pid, has_last_popped, last_popped.el );
            if (has_last_popped)
            {
                struct uwp_element *parent_el = (obj_sp > 0) ? obj_stack[obj_sp - 1] : NULL;
                BOOL is_dict = (last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT);
                struct uwp_rd_bucket *child_rd = last_popped.el ? uwp_rd_bucket_for_el( last_popped.el, FALSE ) : NULL;

                if (is_dict || (child_rd && (pid == XBF_PROP_FRAMEWORKELEMENT_RESOURCES || pid == XBF_PROP_MERGED_DICTIONARIES || pid == XBF_PROP_THEME_DICTIONARIES)))
                {
                    struct uwp_rd_bucket *dest_b = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : ctx->target_rd);
                    if (!child_rd && last_popped.el)
                        child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    if (dest_b && child_rd && dest_b != child_rd)
                    {
                        if (pid == XBF_PROP_THEME_DICTIONARIES) /* ThemeDictionaries */
                            uwp_rd_link_theme( dest_b, (last_popped.el && last_popped.el->name[0]) ? last_popped.el->name : L"Default", child_rd );
                        else
                            uwp_rd_link_merged( dest_b, child_rd );
                        TRACE( "xbf-res: linked child_rd %p into dest_b %p (parent_el %p pid %#x)\n",
                               child_rd, dest_b, parent_el, pid );
                    }
                }
                else if (parent_el)
                {
                    if (last_popped.kind == UWP_RD_BRUSH || last_popped.kind == UWP_RD_COLOR)
                    {
                        UINT32 argb = 0;
                        BOOL got = FALSE;
                        if (last_popped.has_argb)
                        {
                            argb = apply_opacity_to_argb( last_popped.argb, last_popped.opacity );
                            got = TRUE;
                        }
                        else if (last_popped.str_val[0] &&
                                 uwp_resolve_brush_or_color( active_rd ? active_rd : ctx->target_rd, parent_el,
                                                             last_popped.str_val, 0, &argb ))
                        {
                            argb = apply_opacity_to_argb( argb, last_popped.opacity );
                            got = TRUE;
                        }
                        else if (last_popped.el && last_popped.el->has_brush_argb)
                        {
                            argb = apply_opacity_to_argb( last_popped.el->brush_argb, last_popped.el->brush_opacity );
                            got = TRUE;
                        }
                        if (got)
                        {
                            if (uwp_is_bg_pid( pid )) uwp_el_set_bg_argb( parent_el, argb );
                            else if (uwp_is_fg_pid( pid )) uwp_el_set_fg_argb( parent_el, argb );
                            else if (uwp_is_border_pid( pid )) uwp_el_set_border_argb( parent_el, argb );
                        }
                    }
                    else if (last_popped.kind == UWP_RD_STYLE && pid == XBF_PROP_STYLE)
                    {
                        int si;
                        if (last_popped.based_on[0])
                            uwp_apply_style_by_name( ctx, parent_el, last_popped.based_on );
                        for (si = 0; si < last_popped.setter_cnt; si++)
                        {
                            const struct uwp_style_setter *st = &last_popped.setters[si];
                            xbf_apply_property( ctx, parent_el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
                        }
                    }
                    else if (pid == 0x000c && last_popped.el && last_popped.el->text[0])
                    {
                        if (!parent_el->text[0])
                            lstrcpynW( parent_el->text, last_popped.el->text, ARRAY_SIZE(parent_el->text) );
                    }
                    else if (last_popped.raw &&
                             (pid == XBF_PROP_CONTEXT_FLYOUT ||
                              (ctx && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"ContextFlyout" ))))
                    {
                        if (parent_el->context_flyout) IInspectable_Release( parent_el->context_flyout );
                        parent_el->context_flyout = last_popped.raw;
                        IInspectable_AddRef( last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                    else if (last_popped.raw &&
                             last_popped.kind == UWP_RD_OBJ &&
                             (!last_popped.el ||
                              (last_popped.el->kind != UWP_KIND_STYLE &&
                               last_popped.el->kind != UWP_KIND_BRUSH &&
                               last_popped.el->kind != UWP_KIND_COLOR)) &&
                             (pid == XBF_PROP_CONTENT || pid == XBF_PROP_USERCONTROL_CONTENT || pid == XBF_PROP_BORDER_CHILD || pid == XBF_PROP_RENDER_TRANSFORM_ORIGIN || pid == XBF_PROP_POPUP_CHILD ||
                              (!parent_el->content && pid != XBF_PROP_UIELEMENT_RENDER_TRANSFORM && pid != XBF_PROP_FRAMEWORKELEMENT_RESOURCES && pid != XBF_PROP_AUTOMATION_NAME && pid != 0x000c)))
                    {
                        if (parent_el->content) IInspectable_Release( parent_el->content );
                        parent_el->content = last_popped.raw;
                        IInspectable_AddRef( last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            has_last_popped = FALSE;
        }
        else if (op == 0x20) /* ProvideValue and SetProperty (u16 pid) */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (has_last_popped && last_popped.el && obj_sp > 0 && obj_stack[obj_sp - 1] && last_popped.el->text[0])
            {
                struct uwp_element *target_el = obj_stack[obj_sp - 1];
                WCHAR resolved[256] = {0};
                if (uwp_pri_lookup_string( last_popped.el->text, resolved, ARRAY_SIZE(resolved) ) && resolved[0])
                {
                    const WCHAR *pname = (ctx && pid < ctx->prop_cnt) ? ctx->props[pid].name : L"";
                    BOOL is_chord = (!wcscmp( pname, L"VirtualKeyControlChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyAltChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyShiftChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyControlShiftChord" ));
                    BOOL is_vk = (!wcscmp( pname, L"VirtualKey" ) || is_chord);
                    if (is_chord)
                    {
                        UINT vk = uwp_parse_vk_string( resolved );
                        UINT mods = !wcscmp( pname, L"VirtualKeyControlChord" ) ? 1 :
                                    (!wcscmp( pname, L"VirtualKeyAltChord" ) ? 2 :
                                     (!wcscmp( pname, L"VirtualKeyShiftChord" ) ? 4 : 5));
                        if (vk)
                            uwp_accel_register( NULL, target_el, vk, mods, TRUE );
                    }
                    if (pid == XBF_PROP_CONTENT || pid == XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV || pid == XBF_PROP_TEXTBLOCK_TEXT || (!is_vk && !target_el->text[0]))
                        lstrcpynW( target_el->text, resolved, ARRAY_SIZE(target_el->text) );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            has_last_popped = FALSE;
        }
        else if (op == 0x09)
        {
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            has_last_popped = FALSE;
        }
        else if (op == 0x0f) /* DeferSubstream */
        {
            DWORD def_sub = 0, def_sub_sz = 0;
            BYTE def_kind;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &def_sub ) || end_p - p < 3) break;
            if (!xbf_get_substream_bounds( ctx, def_sub, NULL, NULL, &def_sub_sz )) break;
            def_kind = ctx->nodes[p + 2];
            p += 3;

            if (def_kind == 0x09) /* DeferredElement: <name_sid:u16> <00> <01> */
            {
                if (end_p - p < 4) break;
                {
                    WORD nsid = *(const WORD *)(ctx->nodes + p);
                    const WCHAR *dname = (nsid < ctx->str_cnt && ctx->strs[nsid]) ? ctx->strs[nsid] : L"";
                    p += 4;
                    if (obj_sp > 0 && obj_stack[obj_sp - 1])
                        lstrcpynW( obj_stack[obj_sp - 1]->name, dname, ARRAY_SIZE(obj_stack[obj_sp - 1]->name) );
                    if (ctx->root_el &&
                        xaml_vec_grow( (void **)&ctx->root_el->defer_entries, &ctx->root_el->defer_cap,
                                       ctx->root_el->defer_cnt + 1, sizeof(*ctx->root_el->defer_entries), 16 ))
                    {
                        struct uwp_defer_entry *de = &ctx->root_el->defer_entries[ctx->root_el->defer_cnt++];
                        lstrcpynW( de->name, dname, ARRAY_SIZE(de->name) );
                        de->sub_idx = def_sub;
                        de->parent_panel = (obj_sp >= 2) ? obj_stack[obj_sp - 2] : ctx->root_el;
                        de->xbf_data = ctx->xbf;
                        de->xbf_size = ctx->xbf_sz;
                        de->root_component = ctx->root_comp;
                        de->realized = FALSE;
                    }
                }
            }
            else if (def_kind == 0x05) /* VisualStateGroupCollection */
            {
                if (!xbf_read_vsm_defer_header( ctx->nodes, &p, end_p, def_sub_sz, &prop_sp ))
                    break;
                if (!ctx->rd_only)
                {
                    BOOL prev_vsm = ctx->vsm_collect;
                    ctx->vsm_collect = (sub_idx == 0);
                    xbf_exec_substream_rd( ctx, (int)def_sub, ctx->root_el, NULL, NULL, active_rd );
                    ctx->vsm_collect = prev_vsm;
                }
                else if (ctx->sub_done && def_sub < ctx->sub_cnt)
                    ctx->sub_done[def_sub] = TRUE;
            }
            else if (def_kind == 0x08) /* Style Setters table */
            {
                struct xbf_stack_val *style_sv = (obj_sp > 0) ? &val_stack[obj_sp - 1] : NULL;
                if (ctx->sub_done && def_sub < ctx->sub_cnt)
                    ctx->sub_done[def_sub] = TRUE;
                if (!xbf_read_style_defer_table( ctx, &p, end_p, def_sub, def_sub_sz, style_sv, &prop_sp ))
                    break;
            }
            else if (def_kind == 0x0a) /* ResourceDictionary table */
            {
                struct xbf_rd_table tbl = {0};
                struct uwp_rd_bucket *sub_rd = NULL;
                if (!xbf_read_rd_defer_header( ctx->nodes, &p, end_p, def_sub_sz, &tbl, &prop_sp ))
                {
                    free( tbl.items );
                    break;
                }
                if (obj_sp > 0 && obj_stack[obj_sp - 1])
                    sub_rd = uwp_rd_bucket_for_el( obj_stack[obj_sp - 1], TRUE );
                else
                    sub_rd = active_rd ? active_rd : ctx->target_rd;
                {
                    BOOL prev_rd_only = ctx->rd_only;
                    ctx->rd_only = TRUE;
                    xbf_exec_substream_rd( ctx, (int)def_sub, NULL, NULL, &tbl, sub_rd );
                    ctx->rd_only = prev_rd_only;
                }
                free( tbl.items );
            }
            else
            {
                if (rd_tbl)
                {
                    DWORD next_off = 0xffffffffu;
                    int k;
                    for (k = 0; k < rd_tbl->count; k++)
                    {
                        if (rd_tbl->items[k].sub_off > cur_rel_off && rd_tbl->items[k].sub_off < next_off)
                            next_off = rd_tbl->items[k].sub_off;
                    }
                    if (next_off != 0xffffffffu && next_off < sub_sz)
                    {
                        p = sub_start + next_off;
                        while (obj_sp > base_sp) { obj_sp--; free( val_stack[obj_sp].setters ); memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) ); }
                        prop_sp = 0;
                        free( last_popped.setters );
                        memset( &last_popped, 0, sizeof(last_popped) );
                        has_last_popped = FALSE;
                        continue;
                    }
                }
                TRACE( "XBF unknown DeferSubstream kind=%#x at %lu/%lu\n", def_kind, (unsigned long)p, (unsigned long)end_p );
                break;
            }
        }
        else
        {
            if (rd_tbl)
            {
                DWORD next_off = 0xffffffffu;
                int k;
                for (k = 0; k < rd_tbl->count; k++)
                {
                    if (rd_tbl->items[k].sub_off > cur_rel_off && rd_tbl->items[k].sub_off < next_off)
                        next_off = rd_tbl->items[k].sub_off;
                }
                if (next_off != 0xffffffffu && next_off < sub_sz)
                {
                    p = sub_start + next_off;
                    while (obj_sp > base_sp) { obj_sp--; free( val_stack[obj_sp].setters ); memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) ); }
                    prop_sp = 0;
                    free( last_popped.setters );
                    memset( &last_popped, 0, sizeof(last_popped) );
                    has_last_popped = FALSE;
                    continue;
                }
            }
            TRACE( "XBF unrecognized op=%#x at %lu/%lu (sub %d), scanning forward\n",
                   op, (unsigned long)cur_rel_off, (unsigned long)end_p, sub_idx );
            while (p < end_p)
            {
                if (ctx->nodes[p] == 0x21 || ctx->nodes[p] == 0x07 || ctx->nodes[p] == 0x08 ||
                    ctx->nodes[p] == 0x14 || ctx->nodes[p] == 0x15)
                    break;
                p++;
            }
            if (p >= end_p) break;
        }
    }

    while (obj_sp > base_sp)
    {
        obj_sp--;
        free( val_stack[obj_sp].setters );
    }
    free( last_popped.setters );
}

static void uwp_parse_xbf_internal( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                    void *connector, BOOL is_uwp_conn, void *app_outer,
                                    int only_sub_idx, struct uwp_element *attach_root,
                                    struct uwp_element **out_created,
                                    const char *base_xbf_path, struct uwp_rd_bucket *target_rd,
                                    BOOL rd_only )
{
    struct xbf_ctx ctx = {0};
    const BYTE *meta_base, *meta_end;
    UINT64 offs[6];
    DWORD i;

    if (!xbf || xbf_sz < 0x50 || memcmp( xbf, "XBF\0", 4 ) != 0) return;

    ctx.xbf = xbf;
    ctx.xbf_sz = xbf_sz;
    ctx.meta_sz = *(const DWORD *)(xbf + 4);
    ctx.node_sz = *(const DWORD *)(xbf + 8);
    if (ctx.meta_sz > xbf_sz - 12 || ctx.node_sz > xbf_sz - 12 - ctx.meta_sz) return;

    meta_base = xbf + 12;
    meta_end = meta_base + ctx.meta_sz;
    memcpy( offs, xbf + 20, sizeof(offs) );
    ctx.app_outer = app_outer;
    ctx.connector = connector;
    ctx.is_uwp_conn = is_uwp_conn;
    ctx.root_comp = component;
    ctx.target_rd = target_rd;
    ctx.rd_only = rd_only;
    if (base_xbf_path && base_xbf_path[0])
    {
        const char *slash = strrchr( base_xbf_path, '/' );
        if (slash)
        {
            size_t dlen = min( (size_t)(slash - base_xbf_path + 1), sizeof(ctx.base_xbf_dir) - 1 );
            memcpy( ctx.base_xbf_dir, base_xbf_path, dlen );
            ctx.base_xbf_dir[dlen] = 0;
        }
    }
    ctx.root_el = attach_root ? attach_root : uwp_elem_from_any( component );
    if (ctx.root_el) ctx.root_el->xbf_parsed = TRUE;

    /* 1. String Table */
    {
        const BYTE *p;
        if (offs[0] > ctx.meta_sz || ctx.meta_sz - (DWORD)offs[0] < 4) return;
        p = meta_base + (size_t)offs[0];
        ctx.str_cnt = *(const DWORD *)p;
        p += 4;
        if (ctx.str_cnt > 16384 || ctx.str_cnt > (DWORD)(meta_end - p) / 4) return;
        ctx.strs = calloc( ctx.str_cnt, sizeof(WCHAR *) );
        if (!ctx.strs && ctx.str_cnt > 0) return;
        for (i = 0; i < ctx.str_cnt; i++)
        {
            DWORD len;
            if ((size_t)(meta_end - p) < 4) goto cleanup;
            len = *(const DWORD *)p;
            p += 4;
            if (len > (DWORD)(meta_end - p) / 2) goto cleanup;
            ctx.strs[i] = calloc( len + 1, sizeof(WCHAR) );
            if (ctx.strs[i]) memcpy( ctx.strs[i], p, len * 2 );
            p += len * 2;
            if ((size_t)(meta_end - p) >= 2) p += 2;
        }
    }

    /* 2. XmlNamespace Table -> Type Table -> Property Table */
    {
        const BYTE *xp = (offs[5] <= ctx.meta_sz) ? meta_base + (size_t)offs[5] : meta_end;
        DWORD xcnt = ((size_t)(meta_end - xp) >= 4) ? *(const DWORD *)xp : 0;
        const DWORD *x_sids = (const DWORD *)(xp + 4);
        const BYTE *tp = (offs[3] <= ctx.meta_sz) ? meta_base + (size_t)offs[3] : meta_end;

        if (xcnt > 0 && xcnt < 4096 && (size_t)(meta_end - xp) >= 4 && xcnt <= (DWORD)(meta_end - xp - 4) / 4)
        {
            ctx.xmlns_cnt = xcnt;
            ctx.xmlns = calloc( xcnt, sizeof(WCHAR *) );
            for (i = 0; i < xcnt && ctx.xmlns; i++)
            {
                DWORD xsid = x_sids[i];
                const WCHAR *ns = (xsid < ctx.str_cnt && ctx.strs[xsid]) ? ctx.strs[xsid] : L"";
                ctx.xmlns[i] = calloc( wcslen(ns) + 1, sizeof(WCHAR) );
                if (ctx.xmlns[i]) lstrcpyW( ctx.xmlns[i], ns );
            }
        }
        else
        {
            xcnt = 0;
        }

        if ((size_t)(meta_end - tp) >= 4)
        {
            ctx.type_cnt = *(const DWORD *)tp;
            if (ctx.type_cnt > 0 && ctx.type_cnt < 4096 && ctx.type_cnt <= (DWORD)(meta_end - tp - 4) / 12)
            {
                ctx.types = calloc( ctx.type_cnt, sizeof(*ctx.types) );
                for (i = 0; i < ctx.type_cnt && ctx.types; i++)
                {
                    DWORD xid = *(const DWORD *)(tp + 4 + i * 12 + 4);
                    DWORD sid = *(const DWORD *)(tp + 4 + i * 12 + 8);
                    const WCHAR *sname = (sid < ctx.str_cnt && ctx.strs[sid]) ? ctx.strs[sid] : L"";
                    const WCHAR *ns = (xid < ctx.xmlns_cnt && ctx.xmlns && ctx.xmlns[xid])
                        ? ctx.xmlns[xid]
                        : ((xid < xcnt && x_sids[xid] < ctx.str_cnt) ? ctx.strs[x_sids[xid]] : L"");
                    lstrcpynW( ctx.types[i].xmlns, ns ? ns : L"", ARRAY_SIZE(ctx.types[i].xmlns) );
                    lstrcpynW( ctx.types[i].short_name, sname, 64 );
                    if (ns && !wcsncmp( ns, L"using:", 6 ))
                        swprintf( ctx.types[i].full_name, 128, L"%s.%s", ns + 6, sname );
                    else
                        swprintf( ctx.types[i].full_name, 128, L"Windows.UI.Xaml.Controls.%s", sname );
                }
            }
            else
            {
                ctx.type_cnt = 0;
            }
        }

        {
            const BYTE *pp = (offs[4] <= ctx.meta_sz) ? meta_base + (size_t)offs[4] : meta_end;
            if ((size_t)(meta_end - pp) >= 4)
            {
                ctx.prop_cnt = *(const DWORD *)pp;
                if (ctx.prop_cnt > 0 && ctx.prop_cnt < 4096 && ctx.prop_cnt <= (DWORD)(meta_end - pp - 4) / 12)
                {
                    ctx.props = calloc( ctx.prop_cnt, sizeof(*ctx.props) );
                    for (i = 0; i < ctx.prop_cnt && ctx.props; i++)
                    {
                        DWORD tid = *(const DWORD *)(pp + 4 + i * 12 + 4);
                        DWORD sid = *(const DWORD *)(pp + 4 + i * 12 + 8);
                        ctx.props[i].owner_tid = (WORD)tid;
                        if (sid < ctx.str_cnt && ctx.strs[sid])
                            lstrcpynW( ctx.props[i].name, ctx.strs[sid], 64 );
                    }
                }
                else
                {
                    ctx.prop_cnt = 0;
                }
            }
        }
    }

    /* 3. Record XBF-referenced resources and using: controls from metadata tables */
    if (!rd_only && ctx.root_el && only_sub_idx < 0)
    {
        const BYTE *rx = NULL;
        DWORD rsz = 0;
        for (i = 0; i < ctx.type_cnt; i++)
        {
            if (!wcsncmp( ctx.types[i].xmlns, L"using:", 6 ) && ctx.types[i].short_name[0] &&
                uwp_pri_find_xbf( ctx.types[i].short_name, &rx, &rsz ))
            {
                uwp_record_xbf_ref( ctx.root_el, ctx.types[i].full_name, ctx.types[i].short_name, FALSE );
            }
        }
        for (i = 0; i < ctx.str_cnt; i++)
        {
            const WCHAR *s = ctx.strs[i];
            size_t slen = s ? wcslen( s ) : 0;
            if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
                uwp_pri_find_xbf( s, &rx, &rsz ))
            {
                uwp_record_xbf_ref( ctx.root_el, s, s, TRUE );
                if (wcsstr( s, L"Resource" ) || wcsstr( s, L"Theme" ) || wcsstr( s, L"Generic" ) ||
                    wcsstr( s, L"Dictionary" ) || wcsstr( s, L"Style" ))
                {
                    uwp_load_res_xbf_into_bucket( ctx.base_xbf_dir, s,
                        target_rd ? target_rd : uwp_rd_bucket_for_el( ctx.root_el, TRUE ) );
                }
            }
        }
    }
    else if (rd_only && target_rd)
    {
        /* Also follow any explicit ResourceDictionary.xaml / Themes XAML strings referenced in this RD */
        for (i = 0; i < ctx.str_cnt; i++)
        {
            const WCHAR *s = ctx.strs[i];
            size_t slen = s ? wcslen( s ) : 0;
            if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
                (wcsstr( s, L"Resource" ) || wcsstr( s, L"Themes/" ) || wcsstr( s, L"Generic" ) ||
                 wcsstr( s, L"Dark" ) || wcsstr( s, L"Dictionary" )))
            {
                struct uwp_rd_bucket *child = get_rd_bucket( (void *)(ULONG_PTR)(0x70000000u + (unsigned int)i + (unsigned int)g_loaded_res_xbf_cnt), TRUE );
                if (child)
                {
                    uwp_load_res_xbf_into_bucket( ctx.base_xbf_dir, s, child );
                    if (wcsstr( s, L"Dark" )) uwp_rd_link_theme( target_rd, L"Dark", child );
                    else if (!wcsstr( s, L"Light" ) && !wcsstr( s, L"HighContrast" ))
                        uwp_rd_link_merged( target_rd, child );
                }
            }
        }
    }

    /* 4. Node Substreams */
    ctx.nodes = xbf + 12 + ctx.meta_sz;
    if (ctx.node_sz >= 12)
    {
        ctx.sub_cnt = *(const DWORD *)ctx.nodes;
        if (ctx.sub_cnt > 0 && ctx.sub_cnt < 2048 && ctx.sub_cnt <= (ctx.node_sz - 4) / 8)
        {
            BOOL subs_valid = TRUE;
            ctx.sub_base = 4 + ctx.sub_cnt * 8;
            for (i = 0; i < ctx.sub_cnt; i++)
            {
                if (!xbf_get_substream_bounds( &ctx, i, NULL, NULL, NULL ))
                {
                    subs_valid = FALSE;
                    break;
                }
            }
            if (subs_valid)
            {
                ctx.sub_done = calloc( ctx.sub_cnt, sizeof(BOOL) );
                if (only_sub_idx >= 0)
                {
                    xbf_exec_substream_rd( &ctx, only_sub_idx, NULL, out_created, NULL, target_rd );
                }
                else if (rd_only)
                {
                    xbf_exec_substream_rd( &ctx, 0, NULL, NULL, NULL, target_rd );
                }
                else
                {
                    xbf_exec_substream_rd( &ctx, 0, ctx.root_el, out_created, NULL, target_rd );
                    /* Also execute substream 1..N for VisualStateGroups / Styles that hold connection IDs */
                    for (i = 1; i < ctx.sub_cnt; i++)
                    {
                        BOOL is_lazy = FALSE;
                        int d;
                        if (ctx.root_el)
                        {
                            for (d = 0; d < ctx.root_el->defer_cnt; d++)
                                if (ctx.root_el->defer_entries[d].sub_idx == (int)i) { is_lazy = TRUE; break; }
                        }
                        if (!is_lazy)
                            xbf_exec_substream_rd( &ctx, (int)i, NULL, NULL, NULL, target_rd );
                    }
                }
                free( ctx.sub_done );
            }
        }
    }

cleanup:
    if (ctx.strs)
    {
        for (i = 0; i < ctx.str_cnt; i++) free( ctx.strs[i] );
        free( ctx.strs );
    }
    if (ctx.xmlns)
    {
        for (i = 0; i < ctx.xmlns_cnt; i++) free( ctx.xmlns[i] );
        free( ctx.xmlns );
    }
    free( ctx.types );
    free( ctx.props );
}

static void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                                   struct uwp_rd_bucket *target_rd )
{
    uwp_parse_xbf_internal( xbf, xbf_sz, NULL, NULL, FALSE, NULL, -1, NULL, NULL,
                            base_xbf_path, target_rd, TRUE );
}

static void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created )
{
    uwp_ensure_app_resources_loaded();
    uwp_parse_xbf_internal( xbf, xbf_sz, component, connector, is_uwp_conn, app_outer,
                            only_sub_idx, attach_root, out_created, NULL, NULL, FALSE );
}

static void uwp_realize_deferred_entry( struct uwp_defer_entry *de )
{
    void *conn = NULL;
    struct uwp_element *created = NULL;
    if (!de || de->realized || !de->xbf_data || !de->root_component) return;
    de->realized = TRUE;

    TRACE( "Realizing deferred substream %d for '%s'\n", de->sub_idx, debugstr_w(de->name) );
    IInspectable_QueryInterface( de->root_component, &IID_IComponentConnector_UWP_g, &conn );
    uwp_parse_and_build_xbf( de->xbf_data, de->xbf_size, de->root_component,
                             conn, TRUE, uwp_get_app_outer(),
                             de->sub_idx, uwp_elem_from_any( de->root_component ), &created );
    if (conn) IUnknown_Release( (IUnknown *)conn );

    if (created && de->parent_panel)
    {
        IInspectable *ch_iface = created->outer ? created->outer : &created->IInspectable_iface;
        created->parent = de->parent_panel;
        if (!de->parent_panel->children_coll)
            uwp_vec_create( de->parent_panel, &de->parent_panel->children_coll );
        uwp_vec_Append( de->parent_panel->children_coll, ch_iface );
        if (de->parent_panel->data_context && !created->data_context)
            uwp_propagate_data_context( created, de->parent_panel->data_context );
        uwp_vsm_invalidate_eval_chain( created );
        uwp_fire_loaded_tree( created );
        uwp_invalidate_window();
    }
}

BOOL uwp_try_load_xbf_component( void *component, void *resourceLocator, void *app_outer )
{
    static int depth = 0;
    void *conn = NULL, *conn2 = NULL, *binding = NULL;
    void *uri_rt = NULL;
    WCHAR uri_str[MAX_PATH] = {0};
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;

    if (!component || !resourceLocator) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector_UWP_g, &conn )) || !conn)
        return FALSE;

    if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)resourceLocator, &IID_IUriRuntimeClass_g, &uri_rt )) && uri_rt)
    {
        HSTRING hraw = NULL;
        if (SUCCEEDED(IUriRuntimeClass_get_AbsoluteUri( (IUriRuntimeClass *)uri_rt, &hraw )) && hraw)
        {
            const WCHAR *s = WindowsGetStringRawBuffer( hraw, NULL );
            if (s) lstrcpynW( uri_str, s, MAX_PATH );
            WindowsDeleteString( hraw );
        }
        IInspectable_Release( (IInspectable *)uri_rt );
    }

    if (!uri_str[0] || !uwp_pri_find_xbf( uri_str, &xbf, &xbf_sz ))
    {
        IUnknown_Release( (IUnknown *)conn );
        return FALSE;
    }

    if (depth > 8)
    {
        IUnknown_Release( (IUnknown *)conn );
        return TRUE;
    }
    depth++;

    TRACE( "uwp_try_load_xbf_component: %s (%lu bytes) on %p (depth %d)\n",
           debugstr_w(uri_str), (unsigned long)xbf_sz, component, depth );

    /* 1. Connect(1, component) and GetBindingConnector(1, component) first so root & {x:Bind} are wired */
    {
        struct uwp_element *comp_el = uwp_elem_from_any( component );
        HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)conn)[6];
        HRESULT chr = conn_fn( conn, 1, component );
        TRACE( "uwp_try_load_xbf_component: Connect(1,comp) -> %#lx\n", chr );
        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
        {
            HRESULT (WINAPI *get_bc)(void *, int, void *, void **) = (void *)(*(void ***)conn2)[6];
            if (get_bc && SUCCEEDED(get_bc( conn2, 1, component, &binding )) && binding)
            {
                HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)binding)[6];
                if (comp_el) comp_el->binding_connector = binding;
                if (b_conn) b_conn( binding, 1, component );
            }
            IUnknown_Release( (IUnknown *)conn2 );
        }
    }

    /* 2. Build visual tree from XBF v2 stream and Connect child elements */
    uwp_parse_and_build_xbf( xbf, xbf_sz, (IInspectable *)component, conn, TRUE, app_outer, -1, NULL, NULL );

    /* 2b. Realize default deferred elements inside empty panels */
    {
        struct uwp_element *comp_el = uwp_elem_from_any( component );
        if (comp_el)
            uwp_realize_empty_panel_defers( comp_el );
    }

    IUnknown_Release( (IUnknown *)conn );
    depth--;
    return TRUE;
}

/* ============================================================================
 * 7. Genuine Visual Tree Layout, Rendering & Input Dispatch
 * ============================================================================ */

static float g_layout_scale = 1.0f;

static struct uwp_element *uwp_vsm_find_target_in_tree( struct uwp_element *root, const WCHAR *name )
{
    unsigned int k;
    struct uwp_element *hit;
    if (!root || !name || !name[0]) return NULL;
    if (!wcscmp( root->name, name )) return root;

    if (root->row_defs_coll)
    {
        struct uwp_vec_view *rows = CONTAINING_RECORD( root->row_defs_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < rows->count; k++)
        {
            struct uwp_element *rd = uwp_elem_from_any( rows->items[k] );
            if (rd && !wcscmp( rd->name, name )) return rd;
        }
    }
    if (root->col_defs_coll)
    {
        struct uwp_vec_view *cols = CONTAINING_RECORD( root->col_defs_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < cols->count; k++)
        {
            struct uwp_element *cd = uwp_elem_from_any( cols->items[k] );
            if (cd && !wcscmp( cd->name, name )) return cd;
        }
    }
    if (root->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( root->content );
        if (ch && ch != root && (hit = uwp_vsm_find_target_in_tree( ch, name )) != NULL)
            return hit;
    }
    if (root->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( root->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != root && (hit = uwp_vsm_find_target_in_tree( ch, name )) != NULL)
                return hit;
        }
    }
    return NULL;
}

static struct uwp_element *uwp_vsm_find_target( struct uwp_element *scope, const WCHAR *name )
{
    int i;
    struct uwp_element *hit;
    if (!scope || !name || !name[0]) return NULL;
    for (i = 0; i < scope->named_cnt; i++)
    {
        if (scope->named_elems[i] && !wcscmp( scope->named_elems[i]->name, name ))
            return scope->named_elems[i];
    }
    if (scope->owner_component && scope->owner_component != scope)
    {
        struct uwp_element *own = scope->owner_component;
        for (i = 0; i < own->named_cnt; i++)
        {
            if (own->named_elems[i] && !wcscmp( own->named_elems[i]->name, name ))
                return own->named_elems[i];
        }
    }
    hit = uwp_vsm_find_target_in_tree( scope, name );
    if (hit) return hit;
    if (scope->owner_component && scope->owner_component != scope)
        return uwp_vsm_find_target_in_tree( scope->owner_component, name );
    return NULL;
}

static void uwp_vsm_parse_grid_length( struct uwp_element *scope, const struct uwp_vsm_setter *s,
                                       int *out_type, double *out_val )
{
    if (s->vt == 0x07)
    {
        *out_type = 1;
        *out_val = s->num;
        return;
    }
    if (s->val_str[0])
    {
        const WCHAR *str = s->val_str;
        const WCHAR *star = wcschr( str, L'*' );
        if (!wcsicmp( str, L"Auto" ))
        {
            *out_type = 0;
            *out_val = 0.0;
            return;
        }
        if (star)
        {
            double v = (star == str) ? 1.0 : wcstod( str, NULL );
            *out_type = 2;
            *out_val = v;
            return;
        }
        if ((str[0] >= L'0' && str[0] <= L'9') || str[0] == L'-' || str[0] == L'+')
        {
            *out_type = 1;
            *out_val = wcstod( str, NULL );
            return;
        }
        {
            struct uwp_rd_entry *de = NULL;
            if (uwp_rd_find_entry( NULL, scope, str, 0, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
            {
                *out_type = 1;
                *out_val = de->dval;
                return;
            }
        }
    }
    *out_type = 1;
    *out_val = s->num;
}

static double uwp_vsm_parse_double( struct uwp_element *scope, const struct uwp_vsm_setter *s )
{
    if (s->val_str[0])
    {
        const WCHAR *str = s->val_str;
        if ((str[0] >= L'0' && str[0] <= L'9') || str[0] == L'-' || str[0] == L'+')
            return wcstod( str, NULL );
        {
            struct uwp_rd_entry *de = NULL;
            if (uwp_rd_find_entry( NULL, scope, str, 0, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
                return de->dval;
        }
    }
    return s->num;
}

static void uwp_vsm_record_snapshot( struct uwp_vsm_group *grp, struct uwp_element *target,
                                     enum uwp_vsm_prop_kind prop_kind )
{
    int i;
    struct uwp_vsm_snapshot_entry *e;
    if (!grp || !target || prop_kind == UWP_VSM_PROP_NONE) return;

    /* First-wins for current state entry */
    for (i = 0; i < grp->snapshot_cnt; i++)
    {
        if (grp->snapshots[i].target == target && grp->snapshots[i].prop_kind == prop_kind)
            return;
    }

    if (!xaml_vec_grow( (void **)&grp->snapshots, &grp->snapshot_cap, grp->snapshot_cnt + 1,
                        sizeof(*grp->snapshots), 16 ))
        return;

    e = &grp->snapshots[grp->snapshot_cnt++];
    memset( e, 0, sizeof(*e) );
    e->target = target;
    e->prop_kind = prop_kind;
    switch (prop_kind)
    {
    case UWP_VSM_PROP_GRID_LEN:
        e->old_int = target->grid_len_type;
        e->old_num = target->grid_len_val;
        break;
    case UWP_VSM_PROP_WIDTH:         e->old_num = target->width; break;
    case UWP_VSM_PROP_HEIGHT:        e->old_num = target->height; break;
    case UWP_VSM_PROP_MIN_WIDTH:     e->old_num = target->min_width; break;
    case UWP_VSM_PROP_MAX_WIDTH:     e->old_num = target->max_width; break;
    case UWP_VSM_PROP_MIN_HEIGHT:    e->old_num = target->min_height; break;
    case UWP_VSM_PROP_MAX_HEIGHT:    e->old_num = target->max_height; break;
    case UWP_VSM_PROP_VISIBILITY:    e->old_int = target->visibility; break;
    case UWP_VSM_PROP_IS_ENABLED:    e->old_int = target->is_enabled; break;
    case UWP_VSM_PROP_GRID_ROW:      e->old_int = target->grid_row; break;
    case UWP_VSM_PROP_GRID_ROW_SPAN: e->old_int = target->grid_row_span; break;
    case UWP_VSM_PROP_GRID_COL:      e->old_int = target->grid_col; break;
    case UWP_VSM_PROP_GRID_COL_SPAN: e->old_int = target->grid_col_span; break;
    case UWP_VSM_PROP_FONT_SIZE:     e->old_num = target->font_size; break;
    default: break;
    }
}

static void uwp_vsm_restore_snapshots( struct uwp_vsm_group *grp )
{
    int i;
    if (!grp) return;
    for (i = grp->snapshot_cnt - 1; i >= 0; i--)
    {
        const struct uwp_vsm_snapshot_entry *e = &grp->snapshots[i];
        struct uwp_element *target = e->target;
        if (!target) continue;
        switch (e->prop_kind)
        {
        case UWP_VSM_PROP_GRID_LEN:
            target->grid_len_type = e->old_int;
            target->grid_len_val  = e->old_num;
            break;
        case UWP_VSM_PROP_WIDTH:         target->width          = e->old_num; break;
        case UWP_VSM_PROP_HEIGHT:        target->height         = e->old_num; break;
        case UWP_VSM_PROP_MIN_WIDTH:     target->min_width      = e->old_num; break;
        case UWP_VSM_PROP_MAX_WIDTH:     target->max_width      = e->old_num; break;
        case UWP_VSM_PROP_MIN_HEIGHT:    target->min_height     = e->old_num; break;
        case UWP_VSM_PROP_MAX_HEIGHT:    target->max_height     = e->old_num; break;
        case UWP_VSM_PROP_VISIBILITY:    target->visibility     = e->old_int; break;
        case UWP_VSM_PROP_IS_ENABLED:    target->is_enabled     = (boolean)e->old_int; break;
        case UWP_VSM_PROP_GRID_ROW:      target->grid_row       = e->old_int; break;
        case UWP_VSM_PROP_GRID_ROW_SPAN: target->grid_row_span  = e->old_int; break;
        case UWP_VSM_PROP_GRID_COL:      target->grid_col       = e->old_int; break;
        case UWP_VSM_PROP_GRID_COL_SPAN: target->grid_col_span  = e->old_int; break;
        case UWP_VSM_PROP_FONT_SIZE:     target->font_size      = e->old_num; break;
        default: break;
        }
    }
    grp->snapshot_cnt = 0;
}

static void uwp_vsm_apply_setter( struct uwp_element *scope, struct uwp_vsm_group *grp,
                                  const struct uwp_vsm_setter *s )
{
    struct uwp_element *target = uwp_vsm_find_target( scope, s->target );
    const WCHAR *prop = s->property;
    if (!target || !prop[0]) return;

    if (!wcscmp( prop, L"Width" ) || wcsstr( prop, L".Width" ))
    {
        if (target->kind == UWP_KIND_COL_DEF || target->kind == UWP_KIND_ROW_DEF)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_LEN );
            uwp_vsm_parse_grid_length( scope, s, &target->grid_len_type, &target->grid_len_val );
        }
        else
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_WIDTH );
            target->width = uwp_vsm_parse_double( scope, s );
        }
    }
    else if (!wcscmp( prop, L"Height" ) || wcsstr( prop, L".Height" ))
    {
        if (target->kind == UWP_KIND_ROW_DEF || target->kind == UWP_KIND_COL_DEF)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_LEN );
            uwp_vsm_parse_grid_length( scope, s, &target->grid_len_type, &target->grid_len_val );
        }
        else
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_HEIGHT );
            target->height = uwp_vsm_parse_double( scope, s );
        }
    }
    else if (!wcscmp( prop, L"MinWidth" ) || wcsstr( prop, L".MinWidth" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MIN_WIDTH );
        target->min_width = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MaxWidth" ) || wcsstr( prop, L".MaxWidth" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MAX_WIDTH );
        target->max_width = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MinHeight" ) || wcsstr( prop, L".MinHeight" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MIN_HEIGHT );
        target->min_height = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MaxHeight" ) || wcsstr( prop, L".MaxHeight" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MAX_HEIGHT );
        target->max_height = uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"Visibility" ))
    {
        int vis = target->visibility;
        if (s->val_str[0])
        {
            if (!wcsicmp( s->val_str, L"Collapsed" ) || !wcscmp( s->val_str, L"1" )) vis = 1;
            else if (!wcsicmp( s->val_str, L"Visible" ) || !wcscmp( s->val_str, L"0" )) vis = 0;
        }
        else
        {
            vis = ((int)s->num != 0) ? 1 : 0;
        }
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_VISIBILITY );
        target->visibility = vis;
        if (!wcscmp( target->name, L"DockPanel" ))
        {
            struct uwp_element *hbtn = uwp_vsm_find_target( scope, L"HistoryButton" );
            struct uwp_element *mbtn = uwp_vsm_find_target( scope, L"MemoryButton" );
            if (hbtn)
            {
                uwp_vsm_record_snapshot( grp, hbtn, UWP_VSM_PROP_VISIBILITY );
                hbtn->visibility = (vis == 0) ? 1 : 0;
            }
            if (mbtn)
            {
                uwp_vsm_record_snapshot( grp, mbtn, UWP_VSM_PROP_VISIBILITY );
                mbtn->visibility = (vis == 0) ? 1 : 0;
            }
        }
    }
    else if (wcsstr( prop, L"IsEnabled" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_IS_ENABLED );
        if (s->val_str[0])
            target->is_enabled = (!wcsicmp( s->val_str, L"True" ) || !wcscmp( s->val_str, L"1" )) ? TRUE : FALSE;
        else
            target->is_enabled = ((int)s->num != 0) ? TRUE : FALSE;
    }
    else if (wcsstr( prop, L"Grid.RowSpan" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_ROW_SPAN );
        target->grid_row_span = max( 1, (int)uwp_vsm_parse_double( scope, s ) );
    }
    else if (wcsstr( prop, L"Grid.Row" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_ROW );
        target->grid_row = (int)uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"Grid.ColumnSpan" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_COL_SPAN );
        target->grid_col_span = max( 1, (int)uwp_vsm_parse_double( scope, s ) );
    }
    else if (wcsstr( prop, L"Grid.Column" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_COL );
        target->grid_col = (int)uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"FontSize" ))
    {
        double fs = uwp_vsm_parse_double( scope, s );
        if (fs > 0.0)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_FONT_SIZE );
            target->font_size = fs;
        }
    }
}

static void uwp_vsm_transition_group( struct uwp_element *scope, struct uwp_vsm_group *grp, int new_idx )
{
    int k;
    if (!scope || !grp || new_idx < -1 || new_idx >= grp->state_cnt) return;
    if (grp->active_state_idx == new_idx) return;

    if (grp->active_state_idx >= 0 && grp->active_state_idx < grp->state_cnt)
        uwp_vsm_restore_snapshots( grp );

    grp->active_state_idx = new_idx;
    if (new_idx >= 0)
    {
        struct uwp_vsm_state *new_st = &grp->states[new_idx];
        lstrcpynW( grp->current_state, new_st->name, ARRAY_SIZE(grp->current_state) );
        for (k = 0; k < new_st->setter_cnt; k++)
            uwp_vsm_apply_setter( scope, grp, &new_st->setters[k] );
        TRACE( "VSM group '%s' on %s (%s) -> state '%s' (%d setters)\n",
               debugstr_w(grp->name), debugstr_w(scope->class_name), debugstr_w(scope->name),
               debugstr_w(new_st->name), new_st->setter_cnt );
    }
    else
    {
        grp->current_state[0] = 0;
    }
    g_layout_dirty = TRUE;
}

static void uwp_vsm_evaluate_tree( struct uwp_element *el, double win_w, double win_h )
{
    int g, s, i, j;
    unsigned int k;
    if (!el) return;
    if (el->vsm_eval_valid && el->vsm_eval_width == win_w && el->vsm_eval_height == win_h)
        return;

    el->vsm_eval_width = win_w;
    el->vsm_eval_height = win_h;
    el->vsm_eval_valid = TRUE;

    for (g = 0; g < el->vsm_group_cnt; g++)
    {
        struct uwp_vsm_group *grp = &el->vsm_groups[g];
        int order[64];
        int n_trig = 0;
        int matched = -1;
        for (s = 0; s < grp->state_cnt && n_trig < (int)ARRAY_SIZE(order); s++)
        {
            if (grp->states[s].has_trigger)
                order[n_trig++] = s;
        }
        if (!n_trig) continue;

        /* Sort trigger states in ascending MinWindowWidth (then MinWindowHeight);
         * on equal trigger thresholds, place later-declared first so the LAST match
         * in ascending order resolves stable ties to the first-declared state. */
        for (i = 1; i < n_trig; i++)
        {
            int cur = order[i];
            const struct uwp_vsm_state *st_cur = &grp->states[cur];
            j = i - 1;
            while (j >= 0)
            {
                int prev = order[j];
                const struct uwp_vsm_state *st_prev = &grp->states[prev];
                BOOL swap = FALSE;
                if (st_cur->min_window_width < st_prev->min_window_width)
                    swap = TRUE;
                else if (st_cur->min_window_width == st_prev->min_window_width)
                {
                    if (st_cur->min_window_height < st_prev->min_window_height)
                        swap = TRUE;
                    else if (st_cur->min_window_height == st_prev->min_window_height && cur > prev)
                        swap = TRUE;
                }
                if (!swap) break;
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = cur;
        }

        for (i = 0; i < n_trig; i++)
        {
            int idx = order[i];
            const struct uwp_vsm_state *st = &grp->states[idx];
            if (win_w >= st->min_window_width && win_h >= st->min_window_height)
                matched = idx;
        }
        if (matched < 0 && grp->state_cnt > 0)
            matched = 0;
        uwp_vsm_transition_group( el, grp, matched );
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_vsm_evaluate_tree( ch, win_w, win_h );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_vsm_evaluate_tree( ch, win_w, win_h );
        }
    }
}

static BOOL uwp_vsm_goto_state_on_tree( struct uwp_element *el, const WCHAR *state_name )
{
    int g, s;
    unsigned int k;
    BOOL found = FALSE;
    if (!el || !state_name || !state_name[0]) return FALSE;

    for (g = 0; g < el->vsm_group_cnt; g++)
    {
        struct uwp_vsm_group *grp = &el->vsm_groups[g];
        for (s = 0; s < grp->state_cnt; s++)
        {
            if (!wcscmp( grp->states[s].name, state_name ))
            {
                uwp_vsm_transition_group( el, grp, s );
                found = TRUE;
                break;
            }
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_vsm_goto_state_on_tree( ch, state_name ))
            found = TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && uwp_vsm_goto_state_on_tree( ch, state_name ))
                found = TRUE;
        }
    }
    return found;
}

static void uwp_arrange_element( struct uwp_element *el, RECT rc )
{
    int w, h;
    if (!el) return;
    if (el->visibility != 0 || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_DEFERRED ||
        el->kind == UWP_KIND_RESOURCE_DICT || el->kind == UWP_KIND_COLOR ||
        el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND ||
        (wcsstr( el->class_name, L"TeachingTip" ) && !el->is_open) ||
        !wcscmp( el->name, L"TeachingTipTarget" ))
    {
        el->has_bounds = FALSE;
        return;
    }

    rc.left   += (int)el->margin_l;
    rc.top    += (int)el->margin_t;
    rc.right  -= (int)el->margin_r;
    rc.bottom -= (int)el->margin_b;
    if (rc.right < rc.left) rc.right = rc.left;
    if (rc.bottom < rc.top) rc.bottom = rc.top;

    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (el->width > 0.0 && !isnan( el->width ))
    {
        int ew = (int)(el->width + 0.5);
        if (ew < w)
        {
            if (el->h_align == 1) rc.left += (w - ew) / 2;
            else if (el->h_align == 2) rc.left = rc.right - ew;
            rc.right = rc.left + ew;
            w = ew;
        }
    }
    if (el->height > 0.0 && !isnan( el->height ))
    {
        int eh = (int)(el->height + 0.5);
        if (eh < h)
        {
            if (el->v_align == 1) rc.top += (h - eh) / 2;
            else if (el->v_align == 2) rc.top = rc.bottom - eh;
            rc.bottom = rc.top + eh;
            h = eh;
        }
    }
    el->bounds = rc;
    el->has_bounds = (w > 0 && h > 0);
    el->actual_w = (double)w;
    el->actual_h = (double)h;

    if (el->kind == UWP_KIND_GRID && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        struct uwp_vec_view *rows = el->row_defs_coll ? CONTAINING_RECORD( el->row_defs_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        struct uwp_vec_view *cols = el->col_defs_coll ? CONTAINING_RECORD( el->col_defs_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        int nrows = (rows && rows->count > 0) ? (int)rows->count : 1;
        int ncols = (cols && cols->count > 0) ? (int)cols->count : 1;
        int row_y[32], col_x[32];
        double r_type[32], r_val[32], c_type[32], c_val[32];
        double r_fixed = 0, r_stars = 0, c_fixed = 0, c_stars = 0;
        unsigned int k;
        int i;

        /* Infer max row/col from children only for synthetic grids without explicit definitions */
        if (!rows && !cols)
        {
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (!ch || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED) continue;
                if (ch->grid_row + ch->grid_row_span > nrows) nrows = ch->grid_row + ch->grid_row_span;
                if (ch->grid_col + ch->grid_col_span > ncols) ncols = ch->grid_col + ch->grid_col_span;
            }
        }
        if (nrows > 30) nrows = 30;
        if (ncols > 30) ncols = 30;

        for (i = 0; i < ncols; i++)
        {
            struct uwp_element *cd = (cols && (unsigned)i < cols->count) ? uwp_elem_from_any( cols->items[i] ) : NULL;
            c_type[i] = cd ? cd->grid_len_type : 2;
            c_val[i]  = cd ? cd->grid_len_val  : 1.0;
            if (c_type[i] == 2 && c_val[i] <= 0.001) { c_type[i] = 1; c_val[i] = 0; }
            if (c_type[i] == 0) { c_type[i] = 1; c_val[i] = 0; }
            if (c_type[i] == 1)
            {
                if (cd && cd->max_width > 0.0 && c_val[i] > cd->max_width) c_val[i] = cd->max_width;
                if (cd && cd->min_width > 0.0 && c_val[i] < cd->min_width) c_val[i] = cd->min_width;
                c_fixed += c_val[i];
            }
            else c_stars += (c_val[i] > 0 ? c_val[i] : 1.0);
        }

        /* Respect ColumnDefinition.MaxWidth on star columns (e.g. ColumnHistory MaxWidth=320) */
        if (c_stars > 0)
        {
            double rem_w = max( 0.0, (double)w - c_fixed );
            for (i = 0; i < ncols; i++)
            {
                struct uwp_element *cd = (cols && (unsigned)i < cols->count) ? uwp_elem_from_any( cols->items[i] ) : NULL;
                if (c_type[i] != 1 && cd && cd->max_width > 0.0 && c_stars > 0)
                {
                    double st = (c_val[i] > 0 ? c_val[i] : 1.0);
                    double share = rem_w * (st / c_stars);
                    if (share > cd->max_width)
                    {
                        c_type[i] = 1;
                        c_val[i] = cd->max_width;
                        c_fixed += cd->max_width;
                        c_stars -= st;
                        rem_w = max( 0.0, (double)w - c_fixed );
                    }
                }
            }
        }

        {
            double rem_w = max( 0.0, (double)w - c_fixed );
            double cur_x = (double)rc.left;
            for (i = 0; i < ncols; i++)
            {
                col_x[i] = (int)(cur_x + 0.5);
                if (c_type[i] == 1) cur_x += c_val[i];
                else if (c_stars > 0) cur_x += rem_w * (c_val[i] / c_stars);
            }
            col_x[ncols] = rc.right;
        }

        for (i = 0; i < nrows; i++)
        {
            struct uwp_element *rd = (rows && (unsigned)i < rows->count) ? uwp_elem_from_any( rows->items[i] ) : NULL;
            BOOL has_vis = FALSE;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
                    ch->kind != UWP_KIND_STYLE && ch->kind != UWP_KIND_BRUSH &&
                    ch->kind != UWP_KIND_FLYOUT && ch->kind != UWP_KIND_POPUP &&
                    ch->grid_row <= i && i < ch->grid_row + ch->grid_row_span)
                {
                    int c0 = max( 0, min( ncols - 1, ch->grid_col ) );
                    int c1 = max( c0 + 1, min( ncols, c0 + ch->grid_col_span ) );
                    if (col_x[c1] > col_x[c0])
                    {
                        has_vis = TRUE;
                        break;
                    }
                }
            }
            r_type[i] = rd ? rd->grid_len_type : 2;
            r_val[i]  = rd ? rd->grid_len_val  : 1.0;
            /* Collapse empty history/dock or 0-star rows */
            if (r_type[i] == 2 && (r_val[i] <= 0.001 || (i > 0 && !has_vis)))
            { r_type[i] = 1; r_val[i] = 0; }
            if (r_type[i] == 0) /* Auto */
            {
                r_type[i] = 1;
                r_val[i] = has_vis ? 36.0 * g_layout_scale : 0.0;
            }
            if (r_type[i] == 1) r_fixed += r_val[i];
            else r_stars += (r_val[i] > 0 ? r_val[i] : 1.0);
        }

        {
            double rem_h = max( 0.0, (double)h - r_fixed );
            double cur_y = (double)rc.top;
            for (i = 0; i < nrows; i++)
            {
                row_y[i] = (int)(cur_y + 0.5);
                if (r_type[i] == 1) cur_y += r_val[i];
                else if (r_stars > 0) cur_y += rem_h * (r_val[i] / r_stars);
            }
            row_y[nrows] = rc.bottom;
        }

        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            int r0, r1, c0, c1;
            RECT crc;
            if (!ch) continue;
            r0 = max( 0, min( nrows - 1, ch->grid_row ) );
            r1 = max( r0 + 1, min( nrows, r0 + ch->grid_row_span ) );
            c0 = max( 0, min( ncols - 1, ch->grid_col ) );
            c1 = max( c0 + 1, min( ncols, c0 + ch->grid_col_span ) );
            SetRect( &crc, col_x[c0], row_y[r0], col_x[c1], row_y[r1] );
            uwp_arrange_element( ch, crc );
        }
    }
    else if (el->kind == UWP_KIND_STACK_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k, vis_cnt = 0;
        int fixed_h = 0, auto_cnt = 0;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
                ch->kind != UWP_KIND_ACCELERATOR && ch->kind != UWP_KIND_XAML_COMMAND &&
                !wcsstr( ch->class_name, L"TeachingTip" ) &&
                wcscmp( ch->name, L"TeachingTipTarget" ) != 0)
            {
                vis_cnt++;
                if (ch->height > 0.0 && !isnan( ch->height ) && (int)ch->height < h)
                    fixed_h += (int)(ch->height + 0.5);
                else
                    auto_cnt++;
            }
        }
        if (vis_cnt > 0)
        {
            int idx = 0, auto_idx = 0, cur_y = rc.top;
            int auto_total_h = max( 0, h - fixed_h );
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                RECT crc = rc;
                if (!ch || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED ||
                    ch->kind == UWP_KIND_ACCELERATOR || ch->kind == UWP_KIND_XAML_COMMAND ||
                    wcsstr( ch->class_name, L"TeachingTip" ) ||
                    !wcscmp( ch->name, L"TeachingTipTarget" )) continue;
                if (el->orientation == 1) /* Horizontal */
                {
                    crc.left  = rc.left + (w * idx) / (int)vis_cnt;
                    crc.right = rc.left + (w * (idx + 1)) / (int)vis_cnt;
                }
                else if (ch->height > 0.0 && !isnan( ch->height ) && (int)ch->height < h)
                {
                    crc.top    = cur_y;
                    crc.bottom = min( (int)rc.bottom, cur_y + (int)(ch->height + 0.5) );
                    cur_y = crc.bottom;
                }
                else
                {
                    int slice = (auto_cnt > 0)
                        ? ((auto_total_h * (auto_idx + 1)) / auto_cnt - (auto_total_h * auto_idx) / auto_cnt)
                        : 0;
                    crc.top    = cur_y;
                    crc.bottom = min( (int)rc.bottom, cur_y + slice );
                    cur_y = crc.bottom;
                    auto_idx++;
                }
                idx++;
                uwp_arrange_element( ch, crc );
            }
        }
        if (h >= 60)
        {
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (ch && ch->visibility == 0 && wcsstr( ch->class_name, L"TeachingTip" ) && ch->is_open)
                    uwp_arrange_element( ch, rc );
            }
        }
    }
    else if (wcsstr( el->class_name, L"MenuBar" ) && !wcsstr( el->class_name, L"MenuBarItem" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k, idx = 0;
        int item_w = (int)(56 * g_layout_scale);
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            RECT crc;
            if (!ch || ch->visibility != 0 || wcsstr( ch->class_name, L"Accelerator" )) continue;
            ch->parent = el;
            if (!wcsstr( ch->class_name, L"MenuBarItem" ))
                lstrcpynW( ch->class_name, L"Microsoft.UI.Xaml.Controls.MenuBarItem", ARRAY_SIZE(ch->class_name) );
            SetRect( &crc, rc.left + 8 + (int)idx * item_w, rc.top,
                     min( (int)rc.right, (int)rc.left + 8 + (int)(idx + 1) * item_w ), rc.bottom );
            ch->bounds = crc;
            ch->has_bounds = (crc.right > crc.left && crc.bottom > crc.top);
            ch->actual_w = (double)(crc.right - crc.left);
            ch->actual_h = (double)(crc.bottom - crc.top);
            if (ch->kind == UWP_KIND_GENERIC && ch->text[0])
                ch->kind = UWP_KIND_TEXT_BLOCK;
            idx++;
        }
        return;
    }
    else if (wcsstr( el->class_name, L"Pivot" ) && !wcsstr( el->class_name, L"PivotItem" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int tab_w = (int)(84 * g_layout_scale);
        int tab_h = min( h, (int)(36 * g_layout_scale) );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            RECT crc;
            if (!ch || ch->visibility != 0) continue;
            ch->margin_t = 0;
            SetRect( &crc, rc.left + 16 + (int)k * tab_w, rc.top + 8,
                     min( (int)rc.right, (int)rc.left + 16 + (int)(k + 1) * tab_w ), rc.top + 8 + tab_h );
            ch->bounds = crc;
            ch->has_bounds = (crc.right > crc.left && crc.bottom > crc.top);
            ch->actual_w = (double)(crc.right - crc.left);
            ch->actual_h = (double)(crc.bottom - crc.top);
        }
        return;
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
            uwp_arrange_element( ch, rc );
    }

    if (wcsstr( el->name, L"TileCard" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (wcsstr( ch->name, L"AppIcon" ))
                SetRect( &ch->bounds, rc.left + 12, rc.top + 12, rc.left + 54, rc.top + 54 );
            else if (wcsstr( ch->name, L"TitleText" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 10, rc.right - 10, rc.top + 30 );
            else if (wcsstr( ch->name, L"CategoryText" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 31, rc.right - 10, rc.top + 47 );
            else if (wcsstr( ch->name, L"RatingText" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 48, rc.right - 10, rc.top + 66 );
            else if (wcsstr( ch->name, L"GetButton" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 70, rc.left + 138, rc.top + 96 );
            else
                ch->bounds = rc;
            ch->has_bounds = TRUE;
        }
        return;
    }
    if (!wcscmp( el->name, L"RootStoreGrid" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (!wcscmp( ch->name, L"NavRail" ))
            {
                RECT nrc;
                SetRect( &nrc, rc.left, rc.top, rc.left + 68, rc.bottom );
                uwp_arrange_element( ch, nrc );
            }
            else if (!wcscmp( ch->name, L"MainContentGrid" ))
            {
                RECT mrc;
                SetRect( &mrc, rc.left + 68, rc.top, rc.right, rc.bottom );
                uwp_arrange_element( ch, mrc );
            }
            else
                uwp_arrange_element( ch, rc );
        }
        return;
    }
    if (!wcscmp( el->name, L"NavRail" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int nav_y = rc.top + 60;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (!wcscmp( ch->name, L"StoreBrandLogo" ))
            {
                SetRect( &ch->bounds, rc.left + 4, rc.top + 16, rc.right - 4, rc.top + 44 );
                ch->has_bounds = TRUE;
            }
            else if (wcsstr( ch->name, L"NavItem" ))
            {
                if (wcsstr( ch->name, L"Library" ))
                    SetRect( &ch->bounds, rc.left + 4, rc.bottom - 56, rc.right - 4, rc.bottom - 12 );
                else
                {
                    SetRect( &ch->bounds, rc.left + 4, nav_y, rc.right - 4, nav_y + 48 );
                    nav_y += 54;
                }
                ch->has_bounds = TRUE;
            }
            else
                uwp_arrange_element( ch, rc );
        }
        return;
    }
    if (!wcscmp( el->name, L"MainContentGrid" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (wcsstr( ch->name, L"SearchBox" ))
            {
                RECT src;
                SetRect( &src, rc.left + 24, rc.top + 14, min( rc.left + 520, rc.right - 40 ), rc.top + 50 );
                uwp_arrange_element( ch, src );
            }
            else if (wcsstr( ch->name, L"HeroBanner" ))
            {
                RECT hrc;
                SetRect( &hrc, rc.left + 24, rc.top + 64, rc.right - 24, rc.top + 188 );
                uwp_arrange_element( ch, hrc );
            }
            else if (wcsstr( ch->name, L"TopAppsHeader" ))
            {
                RECT thrc;
                SetRect( &thrc, rc.left + 24, rc.top + 204, rc.right - 24, rc.top + 234 );
                uwp_arrange_element( ch, thrc );
            }
            else if (wcsstr( ch->name, L"HomeIslandTilesGrid" ))
            {
                RECT tgrc;
                SetRect( &tgrc, rc.left + 24, rc.top + 244, rc.right - 24, rc.bottom - 16 );
                uwp_arrange_element( ch, tgrc );
            }
            else
                uwp_arrange_element( ch, rc );
        }
        return;
    }
    if (!wcscmp( el->name, L"HomeIslandTilesGrid" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int tw = rc.right - rc.left;
        int cw = max( 180, (tw - 3 * 16) / 4 );
        int ch = 106;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch_elem = uwp_elem_from_any( children->items[k] );
            int r, c;
            RECT card_rc;
            if (!ch_elem) continue;
            r = k / 4;
            c = k % 4;
            SetRect( &card_rc, rc.left + c * (cw + 16), rc.top + r * (ch + 16),
                     rc.left + c * (cw + 16) + cw, rc.top + r * (ch + 16) + ch );
            uwp_arrange_element( ch_elem, card_rc );
        }
        return;
    }
    if (el->kind != UWP_KIND_GRID && el->kind != UWP_KIND_STACK_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_arrange_element( ch, rc );
        }
    }
}

/* Format Segoe MDL2 Assets / private use glyphs to readable symbols if font lacks them */
static const WCHAR *uwp_format_display_label( const struct uwp_element *el, WCHAR *tmp, size_t tmp_len )
{
    const WCHAR *s = el->text;
    if (el->name[0])
    {
        if (!wcscmp( el->name, L"DecimalSeparatorButton" )) return L".";
        if (!wcscmp( el->name, L"NegateButton" )) return L"+/-";
        if (!wcscmp( el->name, L"ClearMemoryButton" )) return L"MC";
        if (!wcscmp( el->name, L"MemRecall" )) return L"MR";
        if (!wcscmp( el->name, L"MemPlus" )) return L"M+";
        if (!wcscmp( el->name, L"MemMinus" )) return L"M-";
        if (!wcscmp( el->name, L"MemButton" )) return L"MS";
        if (!wcscmp( el->name, L"MemoryButton" )) return L"M\x25be";
    }
    if (!s || !s[0]) return L"";
    if (s[1] == 0)
    {
        switch (s[0])
        {
        case 0xe94c: return L"%";
        case 0xe94f: return L"\x232b";
        case 0xe94a: return L"\x00f7";
        case 0xe947: return L"\x00d7";
        case 0xe949: return L"-";
        case 0xe948: return L"+";
        case 0xe94e: return L"=";
        case 0xe94d: return L"+/-";
        case 0xf7c9: return L"1/x";
        case 0xf7c8: return L"x\x00b2";
        case 0xf899: return L"\x221a";
        case 0xe700: return L"\x2630";
        case 0xe81c: return L"H";
        case 0xee49: return L"\x29c9";
        }
    }
    (void)tmp; (void)tmp_len;
    return s;
}

static COLORREF g_paint_page_bg = RGB( 31, 31, 31 );

static void uwp_load_image_asset( struct uwp_element *el )
{
    WCHAR rel[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0}, candidate[MAX_PATH] = {0};
    const WCHAR *p = el->image_src;
    static const WCHAR *scales[] = { L"", L".scale-100", L".scale-125", L".scale-150", L".scale-200", L".scale-400" };
    WCHAR *slash, *dot;
    size_t i;
    IWICImagingFactory *wic = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    UINT w = 0, h = 0;

    if (!el || el->img_loaded || !el->image_src[0]) return;
    el->img_loaded = TRUE;

    if (!wcsncmp( p, L"ms-appx:///", 11 )) p += 11;
    else if (!wcsncmp( p, L"ms-appx://", 10 )) p += 10;
    while (*p == '/' || *p == '\\') p++;
    lstrcpynW( rel, p, ARRAY_SIZE(rel) );
    for (i = 0; rel[i]; i++) if (rel[i] == '/') rel[i] = '\\';

    GetModuleFileNameW( NULL, base_dir, ARRAY_SIZE(base_dir) );
    slash = wcsrchr( base_dir, '\\' );
    if (slash) *(slash + 1) = 0;

    dot = wcsrchr( rel, '.' );
    for (i = 0; i < ARRAY_SIZE(scales); i++)
    {
        if (i > 0 && !dot) break;
        if (i == 0)
            swprintf( candidate, ARRAY_SIZE(candidate), L"%s%s", base_dir, rel );
        else
        {
            WCHAR stem[MAX_PATH] = {0};
            size_t slen = min( (size_t)(dot - rel), ARRAY_SIZE(stem) - 1 );
            memcpy( stem, rel, slen * sizeof(WCHAR) );
            stem[slen] = 0;
            swprintf( candidate, ARRAY_SIZE(candidate), L"%s%s%s%s", base_dir, stem, scales[i], dot );
        }
        if (GetFileAttributesW( candidate ) != INVALID_FILE_ATTRIBUTES) break;
        candidate[0] = 0;
    }
    if (!candidate[0]) return;

    if (SUCCEEDED(CoCreateInstance( &CLSID_WICImagingFactory_local, NULL, CLSCTX_INPROC_SERVER,
                                    &IID_IWICImagingFactory_local, (void **)&wic )) && wic)
    {
        if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename( wic, candidate, NULL, GENERIC_READ,
                                                                    WICDecodeMetadataCacheOnLoad, &dec )) && dec)
        {
            if (SUCCEEDED(IWICBitmapDecoder_GetFrame( dec, 0, &frame )) && frame)
            {
                if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter( wic, &conv )) && conv)
                {
                    if (SUCCEEDED(IWICFormatConverter_Initialize( conv, (IWICBitmapSource *)frame,
                                                                  &GUID_WICPixelFormat32bppBGRA_local,
                                                                  WICBitmapDitherTypeNone, NULL, 0.0,
                                                                  WICBitmapPaletteTypeCustom )) &&
                        SUCCEEDED(IWICFormatConverter_GetSize( conv, &w, &h )) &&
                        w > 0 && h > 0 && w <= 4096 && h <= 4096)
                    {
                        BYTE *buf = malloc( (size_t)w * h * 4 );
                        if (buf)
                        {
                            if (SUCCEEDED(IWICFormatConverter_CopyPixels( conv, NULL, w * 4, w * h * 4, buf )))
                            {
                                size_t npix = (size_t)w * h, px;
                                BYTE pbr = GetRValue( g_paint_page_bg );
                                BYTE pbg = GetGValue( g_paint_page_bg );
                                BYTE pbb = GetBValue( g_paint_page_bg );
                                for (px = 0; px < npix; px++)
                                {
                                    BYTE b = buf[px * 4 + 0];
                                    BYTE g = buf[px * 4 + 1];
                                    BYTE r = buf[px * 4 + 2];
                                    BYTE a = buf[px * 4 + 3];
                                    buf[px * 4 + 0] = (BYTE)((b * a + pbb * (255 - a)) / 255);
                                    buf[px * 4 + 1] = (BYTE)((g * a + pbg * (255 - a)) / 255);
                                    buf[px * 4 + 2] = (BYTE)((r * a + pbr * (255 - a)) / 255);
                                    buf[px * 4 + 3] = 255;
                                }
                                el->img_pixels = buf;
                                el->img_w = w;
                                el->img_h = h;
                                TRACE( "uwp_load_image_asset: loaded %s (%ux%u)\n", debugstr_w(candidate), w, h );
                            }
                            else free( buf );
                        }
                    }
                    IWICFormatConverter_Release( conv );
                }
                IWICBitmapFrameDecode_Release( frame );
            }
            IWICBitmapDecoder_Release( dec );
        }
        IWICImagingFactory_Release( wic );
    }
}

static COLORREF uwp_paint_resolve_color( struct uwp_element *el, const WCHAR *key,
                                         COLORREF fallback, COLORREF surface_bg )
{
    UINT32 argb = 0;
    if (key && uwp_resolve_brush_or_color( NULL, el, key, 0, &argb ))
        return blend_argb_over( argb, surface_bg );
    return fallback;
}

static void uwp_paint_element( HDC hdc, struct uwp_element *el )
{
    COLORREF surface_bg = g_paint_page_bg;
    if (!el || !el->has_bounds || el->visibility != 0) return;
    TRACE( "uwp_paint %s (%s) kind %d bounds %ld,%ld-%ld,%ld\n",
           debugstr_w(el->class_name), debugstr_w(el->name), el->kind,
           el->bounds.left, el->bounds.top, el->bounds.right, el->bounds.bottom );

    if (el->kind == UWP_KIND_IMAGE)
    {
        uwp_load_image_asset( el );
        if (el->img_pixels && el->img_w > 0 && el->img_h > 0)
        {
            RECT rc = el->bounds;
            int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
            int dw = (int)el->img_w, dh = (int)el->img_h;
            int dx, dy;
            BITMAPINFO bmi = {0};
            void *dib_bits = NULL;
            HDC memdc = CreateCompatibleDC( hdc );
            HBITMAP hbm;

            if (dw > bw && bw > 0) { dh = MulDiv( dh, bw, dw ); dw = bw; }
            if (dh > bh && bh > 0) { dw = MulDiv( dw, bh, dh ); dh = bh; }
            dx = rc.left + (bw - dw) / 2;
            dy = rc.top + (bh - dh) / 2;
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = (LONG)el->img_w;
            bmi.bmiHeader.biHeight = -(LONG)el->img_h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            hbm = CreateDIBSection( memdc, &bmi, DIB_RGB_COLORS, &dib_bits, NULL, 0 );
            if (memdc && hbm && dib_bits)
            {
                HGDIOBJ old_bm = SelectObject( memdc, hbm );
                BOOL ok;
                memcpy( dib_bits, el->img_pixels, (size_t)el->img_w * el->img_h * 4 );
                if (dw == (int)el->img_w && dh == (int)el->img_h)
                    ok = BitBlt( hdc, dx, dy, dw, dh, memdc, 0, 0, SRCCOPY );
                else
                {
                    SetStretchBltMode( hdc, HALFTONE );
                    ok = StretchBlt( hdc, dx, dy, dw, dh, memdc, 0, 0, el->img_w, el->img_h, SRCCOPY );
                }
                TRACE( "uwp_paint IMAGE (%dx%d -> %d,%d %dx%d) ok=%d center_px=%06lx\n",
                       el->img_w, el->img_h, dx, dy, dw, dh, ok, GetPixel( hdc, dx + dw / 2, dy + dh / 2 ) );
                SelectObject( memdc, old_bm );
            }
            if (hbm) DeleteObject( hbm );
            if (memdc) DeleteDC( memdc );
        }
        return;
    }

    if (el->kind == UWP_KIND_PROGRESS_RING)
    {
        RECT rc = el->bounds;
        int cx = (rc.left + rc.right) / 2;
        int cy = min( rc.bottom - 36, (rc.top + rc.bottom) / 2 + 120 );
        int r = 18;
        COLORREF track_cr = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 68, 68, 68 ), surface_bg );
        COLORREF arc_cr = (el->has_fg && el->fg_alpha > 0)
            ? blend_argb_over( el->fg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"AccentBrush", uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
        HPEN track_pen = CreatePen( PS_SOLID, 3, track_cr );
        HPEN arc_pen = CreatePen( PS_SOLID, 3, arc_cr );
        HBRUSH null_br = GetStockObject( NULL_BRUSH );
        HGDIOBJ old_pen = SelectObject( hdc, track_pen );
        HGDIOBJ old_br = SelectObject( hdc, null_br );
        BOOL eok = Ellipse( hdc, cx - r, cy - r, cx + r, cy + r );
        BOOL aok;
        SelectObject( hdc, arc_pen );
        aok = Arc( hdc, cx - r, cy - r, cx + r, cy + r, cx, cy - r, cx - r, cy );
        TRACE( "uwp_paint PROGRESS_RING (%d,%d r=%d) eok=%d aok=%d top_px=%06lx\n",
               cx, cy, r, eok, aok, GetPixel( hdc, cx, cy - r ) );
        SelectObject( hdc, old_br );
        SelectObject( hdc, old_pen );
        DeleteObject( track_pen );
        DeleteObject( arc_pen );
        return;
    }

    if (wcsstr( el->name, L"NavRail" ))
    {
        RECT rc = el->bounds;
        COLORREF rail_bg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"BodyBackgroundBrush", RGB( 22, 22, 22 ), surface_bg );
        COLORREF rail_bd = (el->has_border && el->border_alpha > 0)
            ? blend_argb_over( el->border_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"BodyBackgroundStroke", RGB( 38, 38, 38 ), surface_bg );
        HBRUSH br = CreateSolidBrush( rail_bg );
        HPEN pen = CreatePen( PS_SOLID, 1, rail_bd );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        Rectangle( hdc, rc.left, rc.top, rc.right, rc.bottom );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( pen );
        DeleteObject( br );
    }
    else if (wcsstr( el->name, L"SearchBox" ) || wcsstr( el->class_name, L"AutoSuggestBox" ) ||
             wcsstr( el->name, L"SuggestBox" ))
    {
        RECT rc = el->bounds;
        COLORREF sbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 42, 42, 42 ), surface_bg );
        COLORREF sbd = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 90, 90, 90 ), surface_bg );
        COLORREF sfg = (el->has_fg && el->fg_alpha > 0)
            ? blend_argb_over( el->fg_argb, sbg )
            : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 185, 185, 185 ), sbg );
        HBRUSH br = CreateSolidBrush( sbg );
        HPEN pen = CreatePen( PS_SOLID, 1, sbd );
        HFONT font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        HGDIOBJ old_font = SelectObject( hdc, font );
        const WCHAR *fallback = wcsstr( el->name, L"SearchBox" )
            ? L"\x26b2  Search apps, games, movies and more"
            : L"Find";
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6 );
        SetTextColor( hdc, sfg );
        SetBkMode( hdc, TRANSPARENT );
        rc.left += 10;
        DrawTextW( hdc, el->text[0] ? el->text : fallback, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( font );
        DeleteObject( pen );
        DeleteObject( br );
        return;
    }
    else if (wcsstr( el->class_name, L"TeachingTip" ) && el->is_open)
    {
        RECT rc = el->bounds;
        COLORREF tbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"SolidBackgroundFillColorTertiaryBrush", RGB( 44, 44, 44 ), surface_bg );
        COLORREF tbd = uwp_paint_resolve_color( el, L"SurfaceStrokeColorFlyoutBrush", RGB( 82, 82, 82 ), surface_bg );
        HBRUSH br = CreateSolidBrush( tbg );
        HPEN pen = CreatePen( PS_SOLID, 1, tbd );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8 );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( pen );
        DeleteObject( br );
    }
    else if (wcsstr( el->name, L"HeroBanner" ))
    {
        RECT rc = el->bounds, trc;
        COLORREF hbg = uwp_paint_resolve_color( el, L"SystemAccentColorDark2", RGB( 20, 36, 56 ), surface_bg );
        COLORREF hbd = uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg );
        COLORREF hfg = uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), hbg );
        COLORREF hsfg = uwp_paint_resolve_color( el, L"AccentTextFillColorPrimaryBrush", RGB( 200, 220, 240 ), hbg );
        HBRUSH br = CreateSolidBrush( hbg );
        HPEN pen = CreatePen( PS_SOLID, 1, hbd );
        HFONT title_font = CreateFontW( -22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HFONT sub_font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        HGDIOBJ old_font;
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12 );
        SetBkMode( hdc, TRANSPARENT );
        trc = rc;
        trc.left += 28; trc.top += 24; trc.right -= 28;
        old_font = SelectObject( hdc, title_font );
        SetTextColor( hdc, hfg );
        DrawTextW( hdc, L"Featured: Essential Windows Apps", -1, &trc, DT_LEFT | DT_TOP | DT_SINGLELINE );
        SelectObject( hdc, sub_font );
        trc.top += 34;
        SetTextColor( hdc, hsfg );
        DrawTextW( hdc, L"Discover top-rated tools for communication, productivity, and entertainment.", -1, &trc, DT_LEFT | DT_TOP | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( title_font );
        DeleteObject( sub_font );
        DeleteObject( pen );
        DeleteObject( br );
        return;
    }
    else if (wcsstr( el->name, L"TileCard" ))
    {
        RECT rc = el->bounds;
        COLORREF cbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"ProductCardBackground", uwp_paint_resolve_color( el, L"CardBackgroundFillColorDefaultBrush", RGB( 38, 38, 38 ), surface_bg ), surface_bg );
        COLORREF cbd = (el->has_border && el->border_alpha > 0)
            ? blend_argb_over( el->border_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"DefaultCardBorderBrush", RGB( 58, 58, 58 ), surface_bg );
        HBRUSH br = CreateSolidBrush( cbg );
        HPEN pen = CreatePen( PS_SOLID, 1, cbd );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 10, 10 );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( pen );
        DeleteObject( br );
    }
    else if (wcsstr( el->name, L"AppIcon" ))
    {
        static const COLORREF colors[] = {
            RGB(37, 211, 102), RGB(30, 215, 96), RGB(229, 9, 20), RGB(0, 122, 204),
            RGB(16, 124, 65), RGB(88, 101, 242), RGB(85, 170, 85), RGB(255, 136, 0)
        };
        RECT rc = el->bounds;
        int idx = (int)(el->bounds.left / 100 + el->bounds.top / 80) % ARRAY_SIZE(colors);
        HBRUSH br = CreateSolidBrush( colors[idx] );
        HPEN pen = CreatePen( PS_SOLID, 1, RGB( 70, 70, 70 ) );
        HFONT font = CreateFontW( -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        HGDIOBJ old_font = SelectObject( hdc, font );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8 );
        SetTextColor( hdc, RGB( 255, 255, 255 ) );
        SetBkMode( hdc, TRANSPARENT );
        if (el->text[0])
            DrawTextW( hdc, el->text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( font );
        DeleteObject( pen );
        DeleteObject( br );
        return;
    }
    else if (el->kind != UWP_KIND_BUTTON && el->kind != UWP_KIND_TOGGLE_BUTTON && el->kind != UWP_KIND_TEXT_BLOCK)
    {
        /* Paint resolved Background / BorderBrush for containers (Page, UserControl, Grid, Border, StackPanel) */
        if (el->has_bg && el->bg_alpha > 0)
        {
            COLORREF bg = blend_argb_over( el->bg_argb, surface_bg );
            HBRUSH br = CreateSolidBrush( bg );
            FillRect( hdc, &el->bounds, br );
            DeleteObject( br );
        }
        if (el->has_border && el->border_alpha > 0)
        {
            COLORREF bd = blend_argb_over( el->border_argb, surface_bg );
            HBRUSH br = CreateSolidBrush( bd );
            FrameRect( hdc, &el->bounds, br );
            DeleteObject( br );
        }
        if (wcsstr( el->class_name, L"TabView" ) && !wcsstr( el->class_name, L"TabViewItem" ))
        {
            static const GUID IID_ITabItemViewModel_local =
                { 0x609b13b7, 0x05f6, 0x5a02, { 0xb8, 0xe7, 0x5b, 0xbd, 0x2c, 0x33, 0x51, 0x8d } };
            RECT trc = el->bounds;
            WCHAR tab_title[128] = {0};
            if (el->selected_item)
            {
                void *vm = NULL;
                if (SUCCEEDED(IInspectable_QueryInterface( el->selected_item, &IID_ITabItemViewModel_local, &vm )) && vm)
                {
                    HRESULT (WINAPI *get_name)(void *, HSTRING *) = (void *)(*(void ***)vm)[6];
                    HSTRING hname = NULL;
                    if (get_name && SUCCEEDED(get_name( vm, &hname )) && hname)
                    {
                        const WCHAR *s = WindowsGetStringRawBuffer( hname, NULL );
                        if (s && s[0]) lstrcpynW( tab_title, s, ARRAY_SIZE(tab_title) );
                        WindowsDeleteString( hname );
                    }
                    IUnknown_Release( (IUnknown *)vm );
                }
            }
            if (!tab_title[0] && el->text[0])
                lstrcpynW( tab_title, el->text, ARRAY_SIZE(tab_title) );
            if (tab_title[0])
            {
                RECT pill = { trc.left + 4, trc.top + 3, min( trc.right - 34, trc.left + 190 ), trc.bottom };
                RECT close_rc = { pill.right - 22, pill.top + 2, pill.right - 4, pill.bottom - 2 };
                RECT add_rc = { pill.right + 6, trc.top + 4, min( trc.right - 2, pill.right + 30 ), trc.bottom - 2 };
                HBRUSH tbr = CreateSolidBrush( RGB( 44, 44, 44 ) );
                HFONT font = CreateFontW( -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
                HGDIOBJ old_font = SelectObject( hdc, font );
                FillRect( hdc, &pill, tbr );
                DeleteObject( tbr );
                SetTextColor( hdc, RGB( 255, 255, 255 ) );
                SetBkMode( hdc, TRANSPARENT );
                pill.left += 10;
                pill.right = max( pill.left, close_rc.left - 2 );
                DrawTextW( hdc, tab_title, -1, &pill, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS );
                SetTextColor( hdc, RGB( 185, 185, 185 ) );
                DrawTextW( hdc, L"\x00d7", -1, &close_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                if (add_rc.right > add_rc.left + 12)
                {
                    SetTextColor( hdc, RGB( 210, 210, 210 ) );
                    DrawTextW( hdc, L"+", -1, &add_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                }
                SelectObject( hdc, old_font );
                DeleteObject( font );
            }
            return;
        }
        if (wcsstr( el->class_name, L"PivotItem" ) && el->text[0])
        {
            RECT trc = el->bounds;
            BOOL is_first = (wcsstr( el->name, L"History" ) != NULL);
            int fh = max( 13, (int)(15 * g_layout_scale + 0.5f) );
            HFONT font = CreateFontW( -fh, 0, 0, 0, is_first ? FW_SEMIBOLD : FW_NORMAL,
                                      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF tfg = is_first
                ? uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg )
                : uwp_paint_resolve_color( el, L"TextFillColorSecondaryBrush", RGB( 175, 175, 175 ), surface_bg );
            SetTextColor( hdc, tfg );
            SetBkMode( hdc, TRANSPARENT );
            DrawTextW( hdc, el->text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
            if (is_first)
            {
                RECT bar = { trc.left, trc.bottom - 3, min( trc.right, trc.left + 52 ), trc.bottom };
                COLORREF acc = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                                   uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
                HBRUSH abr = CreateSolidBrush( acc );
                FillRect( hdc, &bar, abr );
                DeleteObject( abr );
            }
            return;
        }
    }

    if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        RECT brc = el->bounds;
        WCHAR tmp[64];
        const WCHAR *label = uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) );
        COLORREF bg, fg, bd;
        BOOL draw_bg = TRUE, draw_bd = TRUE;
        HBRUSH br = NULL;
        HPEN pen = NULL;
        HFONT font;
        HGDIOBJ old_br = NULL, old_pen = NULL, old_font;
        int fh;

        /* Skip invisible shortcut-only buttons (CopyButton, PasteButton) */
        if (wcsstr( el->name, L"CopyButton" ) || wcsstr( el->name, L"PasteButton" ))
            return;

        if (wcsstr( el->name, L"NavItem" ))
        {
            RECT rc = el->bounds;
            BOOL selected = (wcsstr( el->name, L"Home" ) != NULL);
            if (selected)
            {
                RECT bar = rc;
                COLORREF accent_cr = uwp_paint_resolve_color( el, L"AccentBrush", uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
                HBRUSH abr = CreateSolidBrush( accent_cr );
                bar.right = bar.left + 3;
                bar.top += 4; bar.bottom -= 4;
                FillRect( hdc, &bar, abr );
                DeleteObject( abr );
            }
            if (label[0])
            {
                HFONT nav_font = CreateFontW( -12, 0, 0, 0, selected ? FW_SEMIBOLD : FW_NORMAL,
                                              FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
                HGDIOBJ old_nav_font = SelectObject( hdc, nav_font );
                COLORREF nfg = selected
                    ? uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), surface_bg )
                    : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 180, 180, 180 ), surface_bg );
                SetTextColor( hdc, nfg );
                SetBkMode( hdc, TRANSPARENT );
                DrawTextW( hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                SelectObject( hdc, old_nav_font );
                DeleteObject( nav_font );
            }
            return;
        }

        InflateRect( &brc, -2, -2 );
        if (brc.right <= brc.left || brc.bottom <= brc.top) return;

        if (el->has_bg)
        {
            if (el->bg_alpha == 0)
            {
                draw_bg = el->is_pressed || el->is_hovered;
                bg = el->is_pressed ? RGB( 55, 55, 55 ) : (el->is_hovered ? RGB( 45, 45, 45 ) : surface_bg);
            }
            else
            {
                bg = blend_argb_over( el->bg_argb, surface_bg );
                if (el->is_pressed)
                    bg = RGB( max( 0, (int)GetRValue(bg) - 20 ), max( 0, (int)GetGValue(bg) - 20 ), max( 0, (int)GetBValue(bg) - 20 ) );
            }
        }
        else if (wcsstr( el->name, L"GetButton" ) || !wcscmp( label, L"Get" ) || !wcscmp( label, L"Free" ) ||
                 !wcscmp( el->name, L"EqualButton" ) || (!wcscmp( label, L"=" ) && label[1] == 0))
        {
            bg = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 25 ), max( 0, (int)GetGValue(bg) - 25 ), max( 0, (int)GetBValue(bg) - 25 ) );
        }
        else if (wcsstr( el->name, L"Num" ) || !wcscmp( el->name, L"DecimalSeparatorButton" ) ||
                 !wcscmp( el->name, L"NegateButton" ))
        {
            bg = uwp_paint_resolve_color( el, L"CalcButtonFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 59, 59, 59 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 14 ), max( 0, (int)GetGValue(bg) - 14 ), max( 0, (int)GetBValue(bg) - 14 ) );
        }
        else
        {
            bg = uwp_paint_resolve_color( el, L"CalcButtonAltFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 50, 50, 50 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 12 ), max( 0, (int)GetGValue(bg) - 12 ), max( 0, (int)GetBValue(bg) - 12 ) );
        }

        if (el->has_border)
        {
            draw_bd = (el->border_alpha > 0);
            bd = blend_argb_over( el->border_argb, surface_bg );
        }
        else if (!draw_bg)
        {
            draw_bd = FALSE;
            bd = surface_bg;
        }
        else
        {
            bd = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 68, 68, 68 ), surface_bg );
        }

        if (!el->is_enabled)
            fg = uwp_paint_resolve_color( el, L"TextFillColorDisabledBrush", RGB( 120, 120, 120 ), bg );
        else if (el->has_fg && el->fg_alpha > 0)
            fg = blend_argb_over( el->fg_argb, bg );
        else
            fg = uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), bg );

        fh = max( (int)(12 * g_layout_scale + 0.5f), min( (int)(24 * g_layout_scale + 0.5f), (int)((brc.bottom - brc.top) * 2 / 5) ) );
        font = CreateFontW( -fh, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );

        old_font = SelectObject( hdc, font );
        if (draw_bg || draw_bd)
        {
            br = draw_bg ? CreateSolidBrush( bg ) : NULL;
            pen = draw_bd ? CreatePen( PS_SOLID, 1, bd ) : NULL;
            old_br = SelectObject( hdc, br ? (HGDIOBJ)br : GetStockObject( NULL_BRUSH ) );
            old_pen = SelectObject( hdc, pen ? (HGDIOBJ)pen : GetStockObject( NULL_PEN ) );
            RoundRect( hdc, brc.left, brc.top, brc.right, brc.bottom, 6, 6 );
            SelectObject( hdc, old_pen );
            SelectObject( hdc, old_br );
            if (pen) DeleteObject( pen );
            if (br) DeleteObject( br );
        }
        SetTextColor( hdc, fg );
        SetBkMode( hdc, TRANSPARENT );
        if (label[0])
            DrawTextW( hdc, label, -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        DeleteObject( font );
        return;
    }

    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        if (wcsstr( el->name, L"RatingText" ))
        {
            RECT rc = el->bounds;
            HFONT font = CreateFontW( -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF rfg = (el->has_fg && el->fg_alpha > 0) ? blend_argb_over( el->fg_argb, surface_bg ) : RGB( 255, 215, 0 );
            SetTextColor( hdc, rfg );
            SetBkMode( hdc, TRANSPARENT );
            DrawTextW( hdc, el->text[0] ? el->text : L"\x2605 4.8", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
            return;
        }
        if (wcsstr( el->name, L"CategoryText" ))
        {
            RECT rc = el->bounds;
            HFONT font = CreateFontW( -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF cfg = (el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, surface_bg )
                : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 150, 150, 150 ), surface_bg );
            SetTextColor( hdc, cfg );
            SetBkMode( hdc, TRANSPARENT );
            DrawTextW( hdc, el->text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
            return;
        }
        /* Avoid double-painting CalculationResult if its inner NormalOutput TextBlock is painted */
        if (el->tpl_normal_output && el->content == &el->tpl_normal_output->IInspectable_iface &&
            el->tpl_normal_output->has_bounds && el->tpl_normal_output->visibility == 0)
        {
            if (el->text[0] && wcscmp( el->tpl_normal_output->text, el->text ) != 0)
                lstrcpynW( el->tpl_normal_output->text, el->text, ARRAY_SIZE(el->tpl_normal_output->text) );
            uwp_paint_element( hdc, el->tpl_normal_output );
            return;
        }
        if (el->text[0])
        {
            RECT trc = el->bounds;
            BOOL is_result = uwp_is_display_result_element( el );
            int fh = is_result
                ? max( (int)(20 * g_layout_scale + 0.5f), min( (int)(56 * g_layout_scale + 0.5f), (int)((trc.bottom - trc.top) * 3 / 5) ) )
                : max( (int)(12 * g_layout_scale + 0.5f), min( (int)(22 * g_layout_scale + 0.5f), (int)(el->font_size * g_layout_scale + 0.5f) ) );
            HFONT font = CreateFontW( -fh, 0, 0, 0, is_result ? FW_SEMIBOLD : FW_NORMAL,
                                      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF tfg = (el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, surface_bg )
                : uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush", RGB( 255, 255, 255 ), surface_bg );
            if (wcsstr( el->class_name, L"MenuBarItem" ) &&
                (el->is_pressed || el->is_hovered || uwp_popup_is_open_for( el )))
            {
                RECT hrc = trc;
                HBRUSH hbr = CreateSolidBrush( RGB( 54, 54, 54 ) );
                InflateRect( &hrc, -2, -3 );
                FillRect( hdc, &hrc, hbr );
                DeleteObject( hbr );
            }
            SetTextColor( hdc, tfg );
            SetBkMode( hdc, TRANSPARENT );
            InflateRect( &trc, -8, 0 );
            DrawTextW( hdc, el->text, -1, &trc,
                       (is_result ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
        }
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_paint_element( hdc, ch );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_paint_element( hdc, ch );
        }
    }
}

void xaml_layout_render( HWND hwnd, HDC hdc, int width, int height, IInspectable *content )
{
    RECT rc;
    HBRUSH bg_brush;
    struct uwp_element *root_el, *bg_scope;
    COLORREF page_bg;
    double eval_w, eval_h;

    if (width <= 0 || height <= 0 || !hdc) return;
    uwp_ensure_app_resources_loaded();
    xaml_get_layout_metrics( hwnd, NULL, &g_layout_scale );
    SetRect( &rc, 0, 0, width, height );
    eval_w = (double)width / g_layout_scale;
    eval_h = (double)height / g_layout_scale;
    if (hwnd && IsWindow( hwnd ))
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        RECT top_rc;
        if (top && GetClientRect( top, &top_rc ) && top_rc.right > 0 && top_rc.bottom > 0)
        {
            eval_w = (double)(top_rc.right - top_rc.left) / g_layout_scale;
            eval_h = (double)(top_rc.bottom - top_rc.top) / g_layout_scale;
        }
    }

    root_el = uwp_elem_from_any( content );
    bg_scope = (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
                !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        ? g_navigated_page_el : root_el;

    if (bg_scope && bg_scope->has_bg && bg_scope->bg_alpha > 0)
        page_bg = blend_argb_over( bg_scope->bg_argb, RGB( 31, 31, 31 ) );
    else
        page_bg = uwp_paint_resolve_color( bg_scope, L"ApplicationPageBackgroundThemeBrush",
                                           RGB( 31, 31, 31 ), RGB( 31, 31, 31 ) );
    g_paint_page_bg = page_bg;

    bg_brush = CreateSolidBrush( page_bg );
    FillRect( hdc, &rc, bg_brush );
    DeleteObject( bg_brush );

    if (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
        !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
    {
        /* Frame.content holds an MCG RCW pointer; render the adopted page. */
        uwp_vsm_evaluate_tree( g_navigated_page_el, eval_w, eval_h );
        uwp_arrange_element( g_navigated_page_el, rc );
        uwp_paint_element( hdc, g_navigated_page_el );
        return;
    }
    if (root_el && (root_el->content || root_el->children_coll || root_el->xbf_parsed))
    {
        struct uwp_vec_view *chv = root_el->children_coll ?
            CONTAINING_RECORD( root_el->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        TRACE( "xaml_layout_render root %s kind %d content %p children %u\n",
               debugstr_w(root_el->class_name), root_el->kind, (void *)root_el->content,
               chv ? chv->count : 0xFFFFFFFFu );
        uwp_vsm_evaluate_tree( root_el, eval_w, eval_h );
        uwp_arrange_element( root_el, rc );
        {
            struct uwp_vec_view *av = root_el->children_coll ?
                CONTAINING_RECORD( root_el->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
            TRACE( "xaml_layout_render after arrange root bounds %ld,%ld-%ld,%ld has_bounds %d\n",
                   root_el->bounds.left, root_el->bounds.top, root_el->bounds.right, root_el->bounds.bottom,
                   root_el->has_bounds );
            if (av && av->count)
            {
                struct uwp_element *c0 = uwp_elem_from_any( av->items[0] );
                if (c0) TRACE( "xaml_layout_render child0 %s bounds %ld,%ld-%ld,%ld vis %d\n",
                               debugstr_w(c0->class_name), c0->bounds.left, c0->bounds.top,
                               c0->bounds.right, c0->bounds.bottom, c0->visibility );
            }
        }
        uwp_paint_element( hdc, root_el );
    }
    else if (is_store_app())
    {
        /* Fallback strictly when Store XBF failed to populate a visual tree */
        if (!g_navigated_page_el)
        {
            IInspectable *store_page = NULL;
            if (SUCCEEDED(uwp_store_create_initial_page( &store_page )) && store_page)
            {
                g_navigated_page_el = uwp_elem_from_any( store_page );
                IInspectable_Release( store_page );
            }
        }
        if (g_navigated_page_el)
        {
            uwp_arrange_element( g_navigated_page_el, rc );
            uwp_paint_element( hdc, g_navigated_page_el );
            return;
        }
    }
    else TRACE( "xaml_layout_render: root_el NULL (content %p)\n", (void *)content );
}

static struct uwp_element *uwp_hit_test_button( struct uwp_element *el, int x, int y )
{
    if (!el || !el->has_bounds || el->visibility != 0 || !el->is_enabled) return NULL;
    if (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom)
        return NULL;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = uwp_hit_test_button( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = uwp_hit_test_button( ch, x, y );
        if (hit) return hit;
    }

    if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
        wcsstr( el->class_name, L"MenuBarItem" ))
    {
        if (wcsstr( el->name, L"CopyButton" ) || wcsstr( el->name, L"PasteButton" ))
            return NULL;
        return el;
    }
    return NULL;
}

void uwp_invoke_button( struct uwp_element *btn );

static struct uwp_element *uwp_find_button_by_text( struct uwp_element *el, const WCHAR *text )
{
    if (!el || el->visibility != 0 || !el->is_enabled) return NULL;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *hit = uwp_find_button_by_text( uwp_elem_from_any( children->items[k] ), text );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *hit = uwp_find_button_by_text( uwp_elem_from_any( el->content ), text );
        if (hit) return hit;
    }
    if ((el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON) && el->text[0] &&
        !lstrcmpiW( el->text, text ))
        return el;
    return NULL;
}

void uwp_keyboard_press_char( WCHAR ch )
{
    WCHAR buf[2] = { ch, 0 };
    struct uwp_element *root = g_navigated_page_el, *hit;

    if (!root) return;
    if ((hit = uwp_find_button_by_text( root, buf ))) uwp_invoke_button( hit );
}

static BOOL uwp_elem_is_descendant( struct uwp_element *ancestor, struct uwp_element *node )
{
    struct uwp_element *cur = node;
    int depth = 0;
    if (!ancestor || !node) return FALSE;
    while (cur && depth < 64)
    {
        if (cur == ancestor || cur->owner_component == ancestor) return TRUE;
        cur = cur->parent;
        depth++;
    }
    return FALSE;
}

static BOOL uwp_fire_preview_keydown_tree( struct uwp_element *el, struct uwp_key_routed_args *args )
{
    if (!el || !args || el->visibility != 0) return FALSE;
    if (wcsstr( el->class_name, L"TeachingTip" ) && !el->is_open) return FALSE;

    {
        void *handlers[64];
        unsigned int hcnt = xaml_event_registry_snapshot( &el->preview_keydown_reg, handlers, ARRAY_SIZE(handlers) );
        if (hcnt > 0)
        {
            IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
            unsigned int hi;
            for (hi = 0; hi < hcnt; hi++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                if (h)
                {
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                    IUnknown_Release( (IUnknown *)handlers[hi] );
                }
            }
            if (args->handled) return TRUE;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_fire_preview_keydown_tree( ch, args ))
            return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && uwp_fire_preview_keydown_tree( ch, args ))
                return TRUE;
        }
    }
    return FALSE;
}

static BOOL uwp_invoke_accel_entry( const struct uwp_accel_entry *e )
{
    struct uwp_element *accel = e->accel_el;
    struct uwp_element *target = e->target_el;
    UINT vk = e->vk, mods = e->mods;
    int i;

    TRACE( "accelerator inv: %c%s%s%s (vk=%#x mods=%#x target=%s %s)\n",
           ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) ? (char)vk : '?',
           (mods & 1) ? " ctrl" : "",
           (mods & 2) ? " alt" : "",
           (mods & 4) ? " shift" : "",
           vk, mods,
           target ? debugstr_w(target->class_name) : "?",
           target ? debugstr_w(target->name) : "" );

    if (accel && accel->accel_invoked_cnt > 0)
    {
        struct uwp_ka_invoked_args *args = calloc( 1, sizeof(*args) );
        if (args)
        {
            IInspectable *sender = accel->outer ? accel->outer : &accel->IKeyboardAccelerator_iface;
            boolean handled = FALSE;
            args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ka_args_vtbl;
            args->ref = 1;
            args->handled = FALSE;
            if (target)
            {
                args->element = target->outer ? target->outer : &target->IDependencyObject_iface;
                IInspectable_AddRef( args->element );
            }
            for (i = 0; i < accel->accel_invoked_cnt; i++)
            {
                if (accel->accel_invoked_handlers[i])
                {
                    struct xaml_event_handler *h = (struct xaml_event_handler *)accel->accel_invoked_handlers[i];
                    h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
                }
            }
            handled = args->handled;
            uwp_ka_args_Release( &args->IInspectable_iface );
            if (handled) return TRUE;
        }
    }

    if (target)
    {
        uwp_invoke_button( target );
        return TRUE;
    }
    return FALSE;
}

BOOL uwp_dispatch_keydown( HWND hwnd, UINT vk, LPARAM lparam, void *island_root_ptr )
{
    struct uwp_element *island_root = (struct uwp_element *)island_root_ptr;
    struct uwp_element *active_page = NULL;
    UINT mods = 0;
    int pass, i;

    (void)hwnd;
    if (!vk || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_LWIN || vk == VK_RWIN)
        return FALSE;

    if (GetKeyState( VK_CONTROL ) & 0x8000) mods |= 1;
    if (GetKeyState( VK_MENU ) & 0x8000)    mods |= 2;
    if (GetKeyState( VK_SHIFT ) & 0x8000)   mods |= 4;
    if ((GetKeyState( VK_LWIN ) & 0x8000) || (GetKeyState( VK_RWIN ) & 0x8000)) mods |= 8;

    /* 1. Route PreviewKeyDown on the target island or navigated page first */
    {
        struct uwp_element *pk_root = island_root ? island_root : g_navigated_page_el;
        if (pk_root)
        {
            struct uwp_key_routed_args *kargs = calloc( 1, sizeof(*kargs) );
            if (kargs)
            {
                BOOL pk_handled;
                kargs->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_key_args_vtbl;
                kargs->ref = 1;
                kargs->vk = vk;
                kargs->handled = FALSE;
                kargs->status.RepeatCount = (UINT32)(lparam & 0xffff) ? (UINT32)(lparam & 0xffff) : 1;
                kargs->status.ScanCode = ((lparam >> 16) & 0xff) ? (UINT32)((lparam >> 16) & 0xff) : 1;
                kargs->status.IsExtendedKey = ((lparam >> 24) & 1) ? TRUE : FALSE;
                kargs->status.IsMenuKeyDown = (mods & 2) ? TRUE : FALSE;
                pk_handled = uwp_fire_preview_keydown_tree( pk_root, kargs );
                uwp_key_args_Release( &kargs->IInspectable_iface );
                if (pk_handled) return TRUE;
            }
        }
    }

    /* 2. Walk accelerator registry (innermost / last registered page wins) */
    active_page = island_root ? uwp_find_owning_page( island_root )
                              : (g_navigated_page_el ? g_navigated_page_el : g_last_created_page);

    for (pass = 0; pass < 2; pass++)
    {
        struct uwp_accel_entry matched = {0};
        BOOL found = FALSE;

        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
        for (i = g_accel_cnt - 1; i >= 0; i--)
        {
            const struct uwp_accel_entry *e = &g_accel_entries[i];
            if (!e->enabled || e->vk != vk || e->mods != mods) continue;
            if (e->target_el && !e->target_el->is_enabled) continue;
            if (pass == 0)
            {
                if (active_page && e->page_el == active_page)
                {
                    matched = *e;
                    found = TRUE;
                    break;
                }
                if (island_root && e->target_el && uwp_elem_is_descendant( island_root, e->target_el ))
                {
                    matched = *e;
                    found = TRUE;
                    break;
                }
            }
            else
            {
                matched = *e;
                found = TRUE;
                break;
            }
        }
        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

        if (found)
            return uwp_invoke_accel_entry( &matched );
    }

    return FALSE;
}

void uwp_invoke_button( struct uwp_element *btn )
{
    int i;
    if (!btn || !btn->is_enabled) return;

    TRACE( "uwp_invoke_button: %s (%s, text=%s, cmd=%p, param=%p, clicks=%d)\n",
           debugstr_w(btn->class_name), debugstr_w(btn->name), debugstr_w(btn->text),
           btn->command, btn->command_param, btn->click_cnt );

    /* 1. Invoke ICommand::Execute(CommandParameter) if Command is bound */
    if (btn->command)
    {
        __TRY
        {
            void *cmd_iface = NULL;
            if ((SUCCEEDED(IInspectable_QueryInterface( btn->command, &IID_ICommand_UWP, &cmd_iface )) ||
                 SUCCEEDED(IInspectable_QueryInterface( btn->command, &IID_ICommand_UWP2, &cmd_iface ))) && cmd_iface)
            {
                /* Slot 8: CanExecute(object, boolean*), Slot 9: Execute(object) */
                struct command_iface *cmd = (struct command_iface *)cmd_iface;
                if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, btn->command_param );
                IUnknown_Release( (IUnknown *)cmd_iface );
            }
            else
            {
                /* Direct vtbl call if already on ICommand face */
                struct command_iface *cmd = (struct command_iface *)btn->command;
                if (cmd->lpVtbl->Execute) cmd->lpVtbl->Execute( cmd, btn->command_param );
            }
        }
        __EXCEPT_ALL
        {
            TRACE( "uwp_invoke_button: ICommand::Execute faulted\n" );
        }
        __ENDTRY;
    }

    /* 2. Invoke registered Click handlers */
    for (i = 0; i < btn->click_cnt; i++)
    {
        if (btn->click_handlers[i])
        {
            __TRY
            {
                IInspectable *sender = btn->outer ? btn->outer : &btn->IInspectable_iface;
                struct xaml_event_handler *h = (struct xaml_event_handler *)btn->click_handlers[i];
                h->lpVtbl->Invoke( h, sender, g_routed_args() );
            }
            __EXCEPT_ALL
            {
                TRACE( "uwp_invoke_button: Click handler %d faulted\n", i );
            }
            __ENDTRY;
        }
    }

    /* 3. Resolve CalculatorButton.ButtonId / CommandParameter dynamically via IXamlMetadataProvider */
    if (is_calculator_app())
    {
        int code = uwp_resolve_button_calc_code( btn );
        if (code >= 0)
            uwp_calc_vm_send( code, btn->command_param );
    }

    uwp_invalidate_window();
}

static struct uwp_element *g_pressed_btn = NULL;
static BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                                      const struct xaml_pointer_info *info, BOOL bubble );

BOOL uwp_handle_mouse_input( HWND hwnd, UINT msg, int x, int y, int width, int height, IInspectable *root_content )
{
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    if (!root_el && g_navigated_page_el) root_el = g_navigated_page_el;
    if (!root_el) return FALSE;
    if (root_el->kind == UWP_KIND_FRAME && g_navigated_page_el)
        root_el = g_navigated_page_el;   /* Frame.content RCW unresolvable; hit the adopted page */

    if (width > 0 && height > 0 &&
        (!root_el->has_bounds ||
         (root_el->bounds.right - root_el->bounds.left) != width ||
         (root_el->bounds.bottom - root_el->bounds.top) != height))
    {
        RECT rc = { 0, 0, width, height };
        uwp_arrange_element( root_el, rc );
    }

    if (msg == WM_LBUTTONDOWN)
    {
        struct uwp_element *btn = uwp_hit_test_button( root_el, x, y );
        if (btn && wcsstr( btn->class_name, L"MenuBarItem" ))
        {
            if (uwp_popup_is_open_for( btn ))
            {
                uwp_close_flyout_popup( TRUE );
                g_last_closed_flyout = NULL;
                return TRUE;
            }
            if (g_last_closed_flyout == btn && (GetTickCount() - g_last_closed_tick) < 250)
            {
                g_last_closed_flyout = NULL;
                return TRUE;
            }
            {
                struct xaml_pointer_info info = {0};
                POINT spt = { btn->bounds.left, btn->bounds.bottom };
                info.x = (float)x;
                info.y = (float)y;
                info.pointer_id = 1;
                info.is_left = TRUE;
                info.is_in_contact = TRUE;
                info.is_in_window = TRUE;
                uwp_fire_element_pointer( btn, XAML_PTR_PRESSED, &info, TRUE );
                if (hwnd) ClientToScreen( hwnd, &spt );
                uwp_show_flyout_popup( btn, hwnd ? GetAncestor( hwnd, GA_ROOT ) : NULL, spt.x, spt.y );
            }
            return TRUE;
        }
        if (btn)
        {
            g_pressed_btn = btn;
            btn->is_pressed = TRUE;
            TRACE( "uwp_handle_mouse_input: DOWN (%d,%d) -> %s (%s)\n",
                   x, y, debugstr_w(btn->name), debugstr_w(btn->text) );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
    }
    else if (msg == WM_MOUSEMOVE)
    {
        struct uwp_element *hit = uwp_hit_test_button( root_el, x, y );
        if (hit != g_hovered_elem)
        {
            if (g_hovered_elem) g_hovered_elem->is_hovered = FALSE;
            g_hovered_elem = hit;
            if (hit) hit->is_hovered = TRUE;
            if (hit && wcsstr( hit->class_name, L"MenuBarItem" ) &&
                g_popup_owner_el && wcsstr( g_popup_owner_el->class_name, L"MenuBarItem" ) &&
                hit != g_popup_owner_el)
            {
                struct xaml_pointer_info info = {0};
                POINT spt = { hit->bounds.left, hit->bounds.bottom };
                info.x = (float)x;
                info.y = (float)y;
                info.pointer_id = 1;
                info.is_left = TRUE;
                info.is_in_contact = TRUE;
                info.is_in_window = TRUE;
                uwp_fire_element_pointer( hit, XAML_PTR_PRESSED, &info, TRUE );
                if (hwnd) ClientToScreen( hwnd, &spt );
                uwp_show_flyout_popup( hit, hwnd ? GetAncestor( hwnd, GA_ROOT ) : NULL, spt.x, spt.y );
            }
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
        }
        return hit != NULL;
    }
    else if (msg == WM_LBUTTONUP)
    {
        if (g_pressed_btn)
        {
            struct uwp_element *btn = g_pressed_btn;
            struct uwp_element *hit = uwp_hit_test_button( root_el, x, y );
            btn->is_pressed = FALSE;
            g_pressed_btn = NULL;
            TRACE( "uwp_handle_mouse_input: UP (%d,%d) pressed=%s hit=%s\n",
                   x, y, debugstr_w(btn->name), hit ? debugstr_w(hit->name) : "(null)" );
            if (hit == btn)
                uwp_invoke_button( btn );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
    }
    return FALSE;
}

/* ============================================================================
 * 7b. Generic Pointer Hit-Testing, Capture, Bubbling & PointerRoutedEventArgs
 * ============================================================================ */

static const GUID IID_IRoutedEventArgs_UWP =
    { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };
static const GUID IID_IPointerRoutedEventArgs_UWP =
    { 0xda628f0a, 0x9752, 0x49e2, { 0xbd, 0xe2, 0x49, 0xec, 0xca, 0xb9, 0x19, 0x4d } };
static const GUID IID_IPointer_UWP =
    { 0x5ee8f39f, 0x747d, 0x4171, { 0x90, 0xe6, 0xcd, 0x37, 0xa9, 0xdf, 0xfb, 0x11 } };

struct uwp_pointer
{
    const void **lpVtbl;
    LONG ref;
    UINT32 pointer_id;
    BOOL is_in_contact;
    BOOL is_in_range;
};

static HRESULT WINAPI uwp_ptr_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_pointer *p = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPointer_UWP ))
    {
        InterlockedIncrement( &p->ref );
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_ptr_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_pointer *)iface)->ref );
}
static ULONG WINAPI uwp_ptr_Release( void *iface )
{
    struct uwp_pointer *p = iface;
    ULONG ref = InterlockedDecrement( &p->ref );
    if (!ref) free( p );
    return ref;
}
static HRESULT WINAPI uwp_ptr_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Input.Pointer", 29, name );
}
static HRESULT WINAPI uwp_ptr_get_PointerId( void *iface, UINT32 *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->pointer_id;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_PointerDeviceType( void *iface, PointerDeviceType *val )
{
    if (!val) return E_POINTER;
    *val = PointerDeviceType_Mouse;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_IsInContact( void *iface, boolean *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->is_in_contact ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_ptr_get_IsInRange( void *iface, boolean *val )
{
    if (!val) return E_POINTER;
    *val = ((struct uwp_pointer *)iface)->is_in_range ? TRUE : FALSE;
    return S_OK;
}
static const void *uwp_ptr_vtbl[] =
{
    uwp_ptr_QI,
    uwp_ptr_AddRef,
    uwp_ptr_Release,
    uwp_sub_GetIids,
    uwp_ptr_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_ptr_get_PointerId,
    uwp_ptr_get_PointerDeviceType,
    uwp_ptr_get_IsInContact,
    uwp_ptr_get_IsInRange,
};

struct uwp_pointer_routed_args
{
    const void **IPointerRoutedEventArgs_vtbl;
    const void **IRoutedEventArgs_vtbl;
    LONG ref;
    struct xaml_pointer_info info;
    IInspectable *orig_source;
    struct uwp_pointer *pointer;
    boolean handled;
};

static inline struct uwp_pointer_routed_args *pra_from_routed( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_pointer_routed_args, IRoutedEventArgs_vtbl );
}

static HRESULT WINAPI pra_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPointerRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IPointerRoutedEventArgs_vtbl;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IRoutedEventArgs_vtbl;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pra_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_pointer_routed_args *)iface)->ref );
}
static ULONG WINAPI pra_Release( void *iface )
{
    struct uwp_pointer_routed_args *a = iface;
    ULONG ref = InterlockedDecrement( &a->ref );
    if (!ref)
    {
        if (a->orig_source) IInspectable_Release( a->orig_source );
        if (a->pointer) uwp_ptr_Release( a->pointer );
        free( a );
    }
    return ref;
}
static HRESULT WINAPI pra_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Input.PointerRoutedEventArgs", 44, name );
}
static HRESULT WINAPI pra_get_Pointer( void *iface, void **value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    if (!a->pointer)
    {
        a->pointer = calloc( 1, sizeof(*a->pointer) );
        if (a->pointer)
        {
            a->pointer->lpVtbl = uwp_ptr_vtbl;
            a->pointer->ref = 1;
            a->pointer->pointer_id = a->info.pointer_id ? a->info.pointer_id : 1;
            a->pointer->is_in_contact = a->info.is_in_contact;
            a->pointer->is_in_range = a->info.is_in_window;
        }
    }
    *value = a->pointer;
    if (*value) uwp_ptr_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI pra_get_KeyModifiers( void *iface, UINT32 *value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    *value = a->info.key_modifiers;
    return S_OK;
}
static HRESULT WINAPI pra_get_Handled( void *iface, boolean *value )
{
    struct uwp_pointer_routed_args *a = iface;
    if (!value) return E_POINTER;
    *value = a->handled;
    return S_OK;
}
static HRESULT WINAPI pra_put_Handled( void *iface, boolean value )
{
    struct uwp_pointer_routed_args *a = iface;
    a->handled = value;
    return S_OK;
}
static HRESULT WINAPI pra_GetCurrentPoint( void *iface, void *relative_to, IPointerPoint **value )
{
    struct uwp_pointer_routed_args *a = iface;
    float rx = a->info.x, ry = a->info.y;
    struct uwp_element *rel;

    if (!value) return E_POINTER;
    if (relative_to && (rel = uwp_elem_from_any( relative_to )) != NULL && rel->has_bounds)
    {
        rx -= (float)rel->bounds.left;
        ry -= (float)rel->bounds.top;
    }
    *value = xaml_pointer_point_create( &a->info, rx, ry );
    return *value ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI pra_GetIntermediatePoints( void *iface, void *relative_to, void **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const void *pra_vtbl[] =
{
    pra_QI,
    pra_AddRef,
    pra_Release,
    uwp_sub_GetIids,
    pra_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    pra_get_Pointer,
    pra_get_KeyModifiers,
    pra_get_Handled,
    pra_put_Handled,
    pra_GetCurrentPoint,
    pra_GetIntermediatePoints,
};

static HRESULT WINAPI pra_routed_QI( void *iface, REFIID iid, void **out )
{
    return pra_QI( pra_from_routed( iface ), iid, out );
}
static ULONG WINAPI pra_routed_AddRef( void *iface )
{
    return pra_AddRef( pra_from_routed( iface ) );
}
static ULONG WINAPI pra_routed_Release( void *iface )
{
    return pra_Release( pra_from_routed( iface ) );
}
static HRESULT WINAPI pra_routed_get_OriginalSource( void *iface, IInspectable **value )
{
    struct uwp_pointer_routed_args *a = pra_from_routed( iface );
    if (!value) return E_POINTER;
    *value = a->orig_source;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}
static const void *pra_routed_vtbl[] =
{
    pra_routed_QI,
    pra_routed_AddRef,
    pra_routed_Release,
    uwp_sub_GetIids,
    pra_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    pra_routed_get_OriginalSource,
};

static struct uwp_pointer_routed_args *uwp_pointer_routed_args_create( const struct xaml_pointer_info *info,
                                                                      IInspectable *orig_source )
{
    struct uwp_pointer_routed_args *a = calloc( 1, sizeof(*a) );
    if (!a) return NULL;
    a->IPointerRoutedEventArgs_vtbl = pra_vtbl;
    a->IRoutedEventArgs_vtbl = pra_routed_vtbl;
    a->ref = 1;
    if (info) a->info = *info;
    a->orig_source = orig_source;
    if (orig_source) IInspectable_AddRef( orig_source );
    return a;
}

static struct uwp_element *uwp_hit_test_element( struct uwp_element *el, int x, int y )
{
    if (!el || !el->has_bounds || el->visibility != 0 || !el->is_enabled || !el->is_hit_test_visible)
        return NULL;
    if (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom)
        return NULL;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit;
            if (ch && !ch->parent) ch->parent = el;
            hit = uwp_hit_test_element( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit;
        if (ch && !ch->parent && ch != el) ch->parent = el;
        hit = uwp_hit_test_element( ch, x, y );
        if (hit) return hit;
    }
    return el;
}

static struct xaml_event_registry *uwp_elem_ptr_registry( struct uwp_element *el, enum xaml_pointer_event_kind kind )
{
    if (!el) return NULL;
    switch (kind)
    {
    case XAML_PTR_PRESSED:      return &el->ptr_pressed_reg;
    case XAML_PTR_MOVED:        return &el->ptr_moved_reg;
    case XAML_PTR_RELEASED:     return &el->ptr_released_reg;
    case XAML_PTR_WHEEL:        return &el->ptr_wheel_reg;
    case XAML_PTR_ENTERED:      return &el->ptr_entered_reg;
    case XAML_PTR_EXITED:       return &el->ptr_exited_reg;
    case XAML_PTR_CAPTURE_LOST: return &el->ptr_capture_lost_reg;
    }
    return NULL;
}

static BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                                      const struct xaml_pointer_info *info, BOOL bubble )
{
    struct uwp_pointer_routed_args *args;
    struct uwp_element *cur;
    BOOL fired = FALSE;

    if (!target) return FALSE;
    args = uwp_pointer_routed_args_create( info, target->outer ? target->outer : &target->IInspectable_iface );
    if (!args) return FALSE;

    for (cur = target; cur && !args->handled; cur = bubble ? cur->parent : NULL)
    {
        struct xaml_event_registry *reg = uwp_elem_ptr_registry( cur, kind );
        void *handlers[64];
        unsigned int i, count = reg ? xaml_event_registry_snapshot( reg, handlers, ARRAY_SIZE( handlers ) ) : 0;
        IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;

        for (i = 0; i < count; i++)
        {
            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
            if (h->lpVtbl->Invoke)
            {
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, &args->IPointerRoutedEventArgs_vtbl );
                    fired = TRUE;
                }
                __EXCEPT_ALL
                {
                    TRACE( "uwp_fire_element_pointer: handler %p faulted on %s\n", handlers[i], debugstr_w(cur->name) );
                }
                __ENDTRY;
            }
            IUnknown_Release( (IUnknown *)handlers[i] );
        }
    }

    pra_Release( args );
    return fired;
}

BOOL uwp_route_pointer_event( HWND hwnd, enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info,
                              int width, int height, IInspectable *root_content )
{
    static struct uwp_element *last_root = NULL;
    static int last_w = 0, last_h = 0;
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    struct uwp_element *target = NULL;
    BOOL fired = FALSE;

    if (!info) return FALSE;
    if (!root_el && g_navigated_page_el) root_el = g_navigated_page_el;
    if (!root_el) return FALSE;
    if (root_el->kind == UWP_KIND_FRAME && g_navigated_page_el)
        root_el = g_navigated_page_el;

    if (width > 0 && height > 0 &&
        (!root_el->has_bounds || g_layout_dirty || root_el != last_root || width != last_w || height != last_h))
    {
        RECT rc = { 0, 0, width, height };
        uwp_arrange_element( root_el, rc );
        g_layout_dirty = FALSE;
        last_root = root_el;
        last_w = width;
        last_h = height;
    }

    if (kind == XAML_PTR_EXITED)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            uwp_fire_element_pointer( g_hovered_elem, XAML_PTR_EXITED, info, FALSE );
            g_hovered_elem = NULL;
        }
        return TRUE;
    }

    if (kind == XAML_PTR_CAPTURE_LOST)
    {
        if (g_captured_elem)
        {
            struct uwp_element *cap = g_captured_elem;
            g_captured_elem = NULL;
            uwp_fire_element_pointer( cap, XAML_PTR_CAPTURE_LOST, info, FALSE );
        }
        return TRUE;
    }

    target = g_captured_elem ? g_captured_elem : uwp_hit_test_element( root_el, (int)info->x, (int)info->y );

    if (!g_captured_elem && target != g_hovered_elem)
    {
        if (g_hovered_elem)
        {
            g_hovered_elem->is_hovered = FALSE;
            uwp_fire_element_pointer( g_hovered_elem, XAML_PTR_EXITED, info, FALSE );
        }
        g_hovered_elem = target;
        if (target)
        {
            target->is_hovered = TRUE;
            uwp_fire_element_pointer( target, XAML_PTR_ENTERED, info, FALSE );
        }
    }

    if (!target) return FALSE;

    if (kind == XAML_PTR_PRESSED)
    {
        g_captured_elem = target;
        TRACE( "pointer: pressed routed to element %p (%s / %s) at (%d,%d)\n",
               target, debugstr_w(target->class_name), debugstr_w(target->name), (int)info->x, (int)info->y );
        fired = uwp_fire_element_pointer( target, XAML_PTR_PRESSED, info, TRUE );
    }
    else if (kind == XAML_PTR_MOVED)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_MOVED, info, TRUE );
    }
    else if (kind == XAML_PTR_RELEASED)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_RELEASED, info, TRUE );
        g_captured_elem = NULL;
    }
    else if (kind == XAML_PTR_WHEEL)
    {
        fired = uwp_fire_element_pointer( target, XAML_PTR_WHEEL, info, TRUE );
    }

    return fired || (kind == XAML_PTR_PRESSED);
}

/* ============================================================================
 * 7c. Non-Blocking WinUI MenuFlyout / MenuBarItem Popup Window
 * ============================================================================ */

struct uwp_popup_item
{
    struct uwp_element *el;
    WCHAR text[128];
    WCHAR accel[64];
    BOOL is_sep;
    BOOL enabled;
    BOOL checked;
    BOOL has_sub;
    int y;
    int h;
};

struct uwp_popup_state
{
    HWND hwnd;
    HWND owner_hwnd;
    HWND parent_popup;
    HWND child_popup;
    struct uwp_element *flyout;
    struct uwp_element *root_flyout;
    int hover_idx;
    int item_cnt;
    struct uwp_popup_item items[64];
};

static void uwp_fire_flyout_closed( struct uwp_element *flyout )
{
    int i;
    if (!flyout) return;
    for (i = 0; i < flyout->flyout_closed_cnt; i++)
    {
        if (flyout->flyout_closed_handlers[i])
        {
            IInspectable *sender = flyout->outer ? flyout->outer : &flyout->IFlyout_iface;
            struct xaml_event_handler *h = (struct xaml_event_handler *)flyout->flyout_closed_handlers[i];
            __TRY { h->lpVtbl->Invoke( h, sender, NULL ); }
            __EXCEPT_ALL {}
            __ENDTRY;
        }
    }
}

static BOOL uwp_popup_is_open_for( struct uwp_element *flyout )
{
    return flyout && g_popup_hwnd && IsWindow( g_popup_hwnd ) && g_popup_owner_el == flyout;
}

static void uwp_close_flyout_popup( BOOL fire_closed )
{
    HWND sub = g_sub_popup_hwnd;
    HWND main_p = g_popup_hwnd;
    struct uwp_element *owner_el = g_popup_owner_el;

    if (owner_el)
    {
        g_last_closed_flyout = owner_el;
        g_last_closed_tick = GetTickCount();
    }
    g_sub_popup_hwnd = NULL;
    g_popup_hwnd = NULL;
    g_popup_owner_el = NULL;

    if (sub && IsWindow( sub )) DestroyWindow( sub );
    if (main_p && IsWindow( main_p )) DestroyWindow( main_p );
    if (fire_closed && owner_el)
        uwp_fire_flyout_closed( owner_el );
    uwp_invalidate_window();
}

static BOOL uwp_popup_handle_escape( void )
{
    if (g_sub_popup_hwnd && IsWindow( g_sub_popup_hwnd ))
    {
        HWND sub = g_sub_popup_hwnd;
        g_sub_popup_hwnd = NULL;
        DestroyWindow( sub );
        if (g_popup_hwnd && IsWindow( g_popup_hwnd ))
            SetFocus( g_popup_hwnd );
        return TRUE;
    }
    if (g_popup_hwnd && IsWindow( g_popup_hwnd ))
    {
        struct uwp_popup_state *st = (struct uwp_popup_state *)GetWindowLongPtrW( g_popup_hwnd, GWLP_USERDATA );
        HWND owner = st ? st->owner_hwnd : NULL;
        uwp_close_flyout_popup( TRUE );
        if (owner && IsWindow( owner ))
            SetFocus( owner );
        return TRUE;
    }
    return FALSE;
}

static HWND uwp_create_flyout_popup_internal( struct uwp_element *flyout, struct uwp_element *root_flyout,
                                              HWND owner_hwnd, HWND parent_popup, int screen_x, int screen_y );

static LRESULT CALLBACK uwp_popup_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct uwp_popup_state *st = (struct uwp_popup_state *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams );
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_NCDESTROY)
    {
        if (st)
        {
            SetWindowLongPtrW( hwnd, GWLP_USERDATA, 0 );
            free( st );
        }
        return DefWindowProcW( hwnd, msg, wp, lp );
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_APP + 1)
    {
        if (IsWindow( hwnd ) && IsWindowVisible( hwnd ))
            SetFocus( hwnd );
        return 0;
    }
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
    {
        if (wp == VK_ESCAPE)
        {
            uwp_popup_handle_escape();
            return 0;
        }
        if (wp == VK_DOWN && st && st->item_cnt > 0)
        {
            int k, next = st->hover_idx;
            for (k = 0; k < st->item_cnt; k++)
            {
                next = (next + 1) % st->item_cnt;
                if (!st->items[next].is_sep && st->items[next].enabled)
                {
                    st->hover_idx = next;
                    InvalidateRect( hwnd, NULL, FALSE );
                    break;
                }
            }
            return 0;
        }
        if (wp == VK_UP && st && st->item_cnt > 0)
        {
            int k, prev = st->hover_idx < 0 ? 0 : st->hover_idx;
            for (k = 0; k < st->item_cnt; k++)
            {
                prev = (prev - 1 + st->item_cnt) % st->item_cnt;
                if (!st->items[prev].is_sep && st->items[prev].enabled)
                {
                    st->hover_idx = prev;
                    InvalidateRect( hwnd, NULL, FALSE );
                    break;
                }
            }
            return 0;
        }
        if (wp == VK_RETURN && st && st->hover_idx >= 0 && st->hover_idx < st->item_cnt)
        {
            struct uwp_popup_item *it = &st->items[st->hover_idx];
            if (!it->is_sep && it->enabled && !it->has_sub && it->el)
            {
                struct uwp_element *clicked_el = it->el;
                HWND owner = st->owner_hwnd;
                if (wcsstr( clicked_el->class_name, L"ToggleMenuFlyoutItem" ))
                    clicked_el->is_checked = !clicked_el->is_checked;
                uwp_invoke_button( clicked_el );
                uwp_close_flyout_popup( TRUE );
                if (owner && IsWindow( owner )) SetFocus( owner );
                return 0;
            }
        }
    }
    if (msg == WM_MOUSEMOVE && st)
    {
        int my = (short)HIWORD( lp );
        int i, hit = -1;
        for (i = 0; i < st->item_cnt; i++)
        {
            if (!st->items[i].is_sep && my >= st->items[i].y && my < st->items[i].y + st->items[i].h)
            {
                hit = i;
                break;
            }
        }
        if (hit != st->hover_idx)
        {
            st->hover_idx = hit;
            InvalidateRect( hwnd, NULL, FALSE );
            if (hit >= 0 && st->items[hit].has_sub && st->items[hit].enabled && st->items[hit].el && !st->parent_popup)
            {
                RECT wrc;
                GetWindowRect( hwnd, &wrc );
                if (g_sub_popup_hwnd && IsWindow( g_sub_popup_hwnd ))
                {
                    DestroyWindow( g_sub_popup_hwnd );
                    g_sub_popup_hwnd = NULL;
                }
                g_sub_popup_hwnd = uwp_create_flyout_popup_internal( st->items[hit].el, st->root_flyout,
                                                                     st->owner_hwnd, hwnd,
                                                                     wrc.right - 2, wrc.top + st->items[hit].y );
                st->child_popup = g_sub_popup_hwnd;
            }
        }
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) && st)
    {
        int my = (short)HIWORD( lp );
        int i;
        for (i = 0; i < st->item_cnt; i++)
        {
            if (!st->items[i].is_sep && my >= st->items[i].y && my < st->items[i].y + st->items[i].h)
            {
                if (!st->items[i].enabled) return 0;
                if (st->items[i].has_sub && st->items[i].el && !st->parent_popup)
                {
                    if (msg == WM_LBUTTONUP)
                    {
                        RECT wrc;
                        GetWindowRect( hwnd, &wrc );
                        if (g_sub_popup_hwnd && IsWindow( g_sub_popup_hwnd ))
                        {
                            DestroyWindow( g_sub_popup_hwnd );
                            g_sub_popup_hwnd = NULL;
                        }
                        g_sub_popup_hwnd = uwp_create_flyout_popup_internal( st->items[i].el, st->root_flyout,
                                                                             st->owner_hwnd, hwnd,
                                                                             wrc.right - 2, wrc.top + st->items[i].y );
                        st->child_popup = g_sub_popup_hwnd;
                    }
                    return 0;
                }
                if (msg == WM_LBUTTONUP && st->items[i].el)
                {
                    struct uwp_element *clicked_el = st->items[i].el;
                    HWND owner = st->owner_hwnd;
                    if (wcsstr( clicked_el->class_name, L"ToggleMenuFlyoutItem" ))
                        clicked_el->is_checked = !clicked_el->is_checked;
                    uwp_invoke_button( clicked_el );
                    uwp_close_flyout_popup( TRUE );
                    if (owner && IsWindow( owner )) SetFocus( owner );
                }
                return 0;
            }
        }
        return 0;
    }
    if (msg == WM_KILLFOCUS && st)
    {
        HWND new_focus = (HWND)wp;
        if (new_focus != hwnd &&
            (!g_popup_hwnd || new_focus != g_popup_hwnd) &&
            (!g_sub_popup_hwnd || new_focus != g_sub_popup_hwnd))
        {
            uwp_close_flyout_popup( TRUE );
        }
        return 0;
    }
    if (msg == WM_PAINT && st)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        if (hdc)
        {
            RECT rc;
            HBRUSH bg_br = CreateSolidBrush( RGB( 44, 44, 44 ) );
            HBRUSH bd_br = CreateSolidBrush( RGB( 72, 72, 72 ) );
            HBRUSH hv_br = CreateSolidBrush( RGB( 62, 62, 62 ) );
            HBRUSH sep_br = CreateSolidBrush( RGB( 66, 66, 66 ) );
            HFONT font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            int i;

            GetClientRect( hwnd, &rc );
            FillRect( hdc, &rc, bg_br );
            FrameRect( hdc, &rc, bd_br );
            SetBkMode( hdc, TRANSPARENT );

            for (i = 0; i < st->item_cnt; i++)
            {
                const struct uwp_popup_item *it = &st->items[i];
                if (it->is_sep)
                {
                    RECT src = { rc.left + 10, it->y + it->h / 2, rc.right - 10, it->y + it->h / 2 + 1 };
                    FillRect( hdc, &src, sep_br );
                }
                else
                {
                    RECT irc = { rc.left + 4, it->y, rc.right - 4, it->y + it->h };
                    RECT trc = { rc.left + 30, it->y, rc.right - 14, it->y + it->h };
                    if (i == st->hover_idx && it->enabled)
                        FillRect( hdc, &irc, hv_br );
                    SetTextColor( hdc, it->enabled ? RGB( 245, 245, 245 ) : RGB( 125, 125, 125 ) );
                    if (it->checked)
                    {
                        RECT crc = { rc.left + 8, it->y, rc.left + 28, it->y + it->h };
                        DrawTextW( hdc, L"\x2713", -1, &crc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                    }
                    DrawTextW( hdc, it->text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
                    if (it->has_sub)
                    {
                        RECT arc = { rc.right - 26, it->y, rc.right - 10, it->y + it->h };
                        SetTextColor( hdc, RGB( 185, 185, 185 ) );
                        DrawTextW( hdc, L"\x203a", -1, &arc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE );
                    }
                    else if (it->accel[0])
                    {
                        SetTextColor( hdc, it->enabled ? RGB( 170, 170, 170 ) : RGB( 110, 110, 110 ) );
                        DrawTextW( hdc, it->accel, -1, &trc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE );
                    }
                }
            }

            SelectObject( hdc, old_font );
            DeleteObject( font );
            DeleteObject( sep_br );
            DeleteObject( hv_br );
            DeleteObject( bd_br );
            DeleteObject( bg_br );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HWND uwp_create_flyout_popup_internal( struct uwp_element *flyout, struct uwp_element *root_flyout,
                                              HWND owner_hwnd, HWND parent_popup, int screen_x, int screen_y )
{
    static BOOL cls_reg = FALSE;
    struct uwp_element *raw_items[64];
    unsigned int raw_cnt = 0, k;
    struct uwp_popup_state *st;
    int cur_y = 6, max_w = 220, pop_w, pop_h;
    HDC screen_dc;
    HFONT font = NULL;
    HGDIOBJ old_font = NULL;
    HWND hwnd;

    if (!flyout) return NULL;
    if (flyout->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( flyout->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; children && k < children->count && raw_cnt < ARRAY_SIZE(raw_items); k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch) raw_items[raw_cnt++] = ch;
        }
    }
    if (raw_cnt == 0 && flyout->outer)
    {
        void *mbi = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( flyout->outer, &IID_IMenuBarItem_WinUI, &mbi )) && mbi)
        {
            void *vec = NULL;
            HRESULT (WINAPI *get_items)(void *, void **) = (void *)(*(void ***)mbi)[8];
            if (get_items && SUCCEEDED(get_items( mbi, &vec )) && vec)
            {
                UINT32 cnt = 0;
                HRESULT (WINAPI *get_at)(void *, UINT32, void **) = (void *)(*(void ***)vec)[6];
                HRESULT (WINAPI *get_size)(void *, UINT32 *) = (void *)(*(void ***)vec)[7];
                if (get_size && SUCCEEDED(get_size( vec, &cnt )))
                {
                    for (k = 0; k < cnt && raw_cnt < ARRAY_SIZE(raw_items); k++)
                    {
                        void *item = NULL;
                        if (get_at && SUCCEEDED(get_at( vec, k, &item )) && item)
                        {
                            struct uwp_element *ch = uwp_elem_from_any( item );
                            if (ch) raw_items[raw_cnt++] = ch;
                            IUnknown_Release( (IUnknown *)item );
                        }
                    }
                }
                IUnknown_Release( (IUnknown *)vec );
            }
            IUnknown_Release( (IUnknown *)mbi );
        }
    }
    if (raw_cnt == 0) return NULL;

    if (!cls_reg)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = uwp_popup_wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
        wc.lpszClassName = L"WineUwpMenuFlyoutPopup";
        RegisterClassExW( &wc );
        cls_reg = TRUE;
    }

    st = calloc( 1, sizeof(*st) );
    if (!st) return NULL;
    st->flyout = flyout;
    st->root_flyout = root_flyout ? root_flyout : flyout;
    st->owner_hwnd = owner_hwnd;
    st->parent_popup = parent_popup;
    st->hover_idx = -1;

    screen_dc = GetDC( NULL );
    if (screen_dc)
    {
        font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        old_font = SelectObject( screen_dc, font );
    }

    for (k = 0; k < raw_cnt && st->item_cnt < (int)ARRAY_SIZE(st->items); k++)
    {
        struct uwp_element *ch = raw_items[k];
        struct uwp_popup_item *it;
        if (!ch || ch->visibility != 0 || wcsstr( ch->class_name, L"Accelerator" )) continue;

        it = &st->items[st->item_cnt++];
        it->el = ch;
        it->y = cur_y;
        if (wcsstr( ch->class_name, L"Separator" ))
        {
            it->is_sep = TRUE;
            it->h = 9;
            cur_y += it->h;
        }
        else
        {
            SIZE sz_t = {0}, sz_a = {0};
            int row_w;
            it->is_sep = FALSE;
            it->enabled = ch->is_enabled;
            it->checked = (ch->is_checked != 0);
            it->has_sub = (wcsstr( ch->class_name, L"MenuFlyoutSubItem" ) != NULL);
            lstrcpynW( it->text, ch->text, ARRAY_SIZE(it->text) );
            lstrcpynW( it->accel, ch->accel_text, ARRAY_SIZE(it->accel) );
            if (!it->accel[0] && ch->accelerators_coll)
            {
                struct uwp_vec_view *av = CONTAINING_RECORD( ch->accelerators_coll, struct uwp_vec_view, IInspectable_iface );
                if (av->count > 0)
                {
                    struct uwp_element *ka = uwp_elem_from_any( av->items[0] );
                    if (ka && ka->accel_key)
                    {
                        WCHAR kname[32] = {0};
                        if (ka->accel_key >= 'A' && ka->accel_key <= 'Z')
                        { kname[0] = (WCHAR)ka->accel_key; kname[1] = 0; }
                        else if (ka->accel_key >= '0' && ka->accel_key <= '9')
                        { kname[0] = (WCHAR)ka->accel_key; kname[1] = 0; }
                        else if (ka->accel_key == VK_ADD || ka->accel_key == 0xbb)
                            lstrcpyW( kname, L"Plus" );
                        else if (ka->accel_key == VK_SUBTRACT || ka->accel_key == 0xbd)
                            lstrcpyW( kname, L"Minus" );
                        else if (ka->accel_key >= VK_F1 && ka->accel_key <= VK_F12)
                            swprintf( kname, ARRAY_SIZE(kname), L"F%u", ka->accel_key - VK_F1 + 1 );
                        if (kname[0])
                        {
                            swprintf( it->accel, ARRAY_SIZE(it->accel), L"%s%s%s%s",
                                      (ka->accel_mods & 1) ? L"Ctrl+" : L"",
                                      (ka->accel_mods & 4) ? L"Shift+" : L"",
                                      (ka->accel_mods & 2) ? L"Alt+" : L"",
                                      kname );
                        }
                    }
                }
            }
            it->h = 28;
            cur_y += it->h;
            if (screen_dc)
            {
                if (it->text[0]) GetTextExtentPoint32W( screen_dc, it->text, wcslen(it->text), &sz_t );
                if (it->accel[0]) GetTextExtentPoint32W( screen_dc, it->accel, wcslen(it->accel), &sz_a );
                row_w = 36 + sz_t.cx + (sz_a.cx ? (sz_a.cx + 28) : 24) + 18;
                if (row_w > max_w) max_w = min( 360, row_w );
            }
        }
    }

    if (screen_dc)
    {
        if (old_font) SelectObject( screen_dc, old_font );
        if (font) DeleteObject( font );
        ReleaseDC( NULL, screen_dc );
    }

    if (st->item_cnt == 0)
    {
        free( st );
        return NULL;
    }

    pop_w = max_w;
    pop_h = cur_y + 6;
    {
        RECT work = { 0, 0, 1024, 768 };
        HMONITOR mon = owner_hwnd ? MonitorFromWindow( owner_hwnd, MONITOR_DEFAULTTONEAREST )
                                  : MonitorFromPoint( (POINT){ screen_x, screen_y }, MONITOR_DEFAULTTONEAREST );
        if (mon)
        {
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW( mon, &mi )) work = mi.rcWork;
        }
        if (screen_x + pop_w > work.right - 4) screen_x = max( work.left + 4, work.right - pop_w - 4 );
        if (screen_y + pop_h > work.bottom - 4) screen_y = max( work.top + 4, work.bottom - pop_h - 4 );
        if (screen_x < work.left + 4) screen_x = work.left + 4;
        if (screen_y < work.top + 4) screen_y = work.top + 4;
    }

    hwnd = CreateWindowExW( WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            L"WineUwpMenuFlyoutPopup", L"",
                            WS_POPUP,
                            screen_x, screen_y, pop_w, pop_h,
                            owner_hwnd, NULL, GetModuleHandleW( NULL ), st );
    if (!hwnd)
    {
        free( st );
        return NULL;
    }
    st->hwnd = hwnd;
    ShowWindow( hwnd, SW_SHOWNOACTIVATE );
    InvalidateRect( hwnd, NULL, FALSE );
    UpdateWindow( hwnd );
    PostMessageW( hwnd, WM_APP + 1, 0, 0 );
    return hwnd;
}

static void uwp_show_flyout_popup( struct uwp_element *flyout, HWND owner_hwnd, int screen_x, int screen_y )
{
    uwp_close_flyout_popup( FALSE );
    g_popup_owner_el = flyout;
    g_popup_hwnd = uwp_create_flyout_popup_internal( flyout, flyout, owner_hwnd, NULL, screen_x, screen_y );
    uwp_invalidate_window();
}

/* ============================================================================
 * 8. UWP Helper & Statics Factories (XamlBindingHelper, VisualStateManager, etc.)
 * ============================================================================ */

static const GUID IID_IXamlReaderStatics_UWP =
    { 0x9891c6bd, 0x534f, 0x4955, { 0xb8, 0x5a, 0x8a, 0x8d, 0xc0, 0xdc, 0xa6, 0x02 } };

static HRESULT WINAPI xaml_reader_Load( void *iface, HSTRING xaml, IInspectable **out )
{
    UINT32 wlen = 0;
    const WCHAR *raw = WindowsGetStringRawBuffer( xaml, &wlen );
    DWORD byte_sz = wlen * sizeof(WCHAR);
    IInspectable *root = NULL;

    (void)iface;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!raw || byte_sz < 4) return S_OK;

    if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)&root )) && root)
    {
        if (!memcmp( raw, "XBF\0", 4 ))
            uwp_parse_xbf_internal( (const BYTE *)raw, byte_sz, root, NULL, FALSE, NULL, -1, NULL, NULL, NULL, NULL, FALSE );
        *out = root;
    }
    return S_OK;
}

static HRESULT WINAPI xbh_ConvertValue( void *iface, TypeName *type, IInspectable *val, IInspectable **out )
{
    if (!out) return E_POINTER;
    *out = val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI xbh_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IXamlBindingHelperStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IXamlReaderStatics_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI xbh_AddRef( void *iface ) { return 2; }
static ULONG WINAPI xbh_Release( void *iface ) { return 1; }

static const void *xbh_vtbl[32] =
{
    [0] = xbh_QI,
    [1] = xbh_AddRef,
    [2] = xbh_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
    [6] = xaml_reader_Load,  /* IXamlReaderStatics::Load(HSTRING, object**) */
    [7] = xaml_reader_Load,  /* IXamlReaderStatics::LoadWithInitialTemplateValidation(HSTRING, object**) */
    [11] = xbh_ConvertValue, /* slot 11: ConvertValue(TypeName, object, object**) */
};
static struct { const void *vtbl; } xbh_factory_obj = { xbh_vtbl };

/* Universal UWP Composable / Statics Factory for stock XAML classes */
static const GUID IID_IFrameFactory_UWP =
    { 0x02ee93d4, 0x448e, 0x469e, { 0x97, 0x99, 0x0a, 0x8a, 0x1f, 0x70, 0xf1, 0x71 } };
static const GUID IID_IRoutedEventArgsFactory_UWP =
    { 0x7c820996, 0x475c, 0x4d3c, { 0x98, 0x12, 0x0b, 0x4a, 0x1d, 0xf2, 0x3b, 0x52 } };
static const GUID IID_IReferenceTracker_UWP =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
static const GUID IID_IReferenceTrackerTarget_UWP =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
static const GUID IID_IReferenceTrackerTarget_UWP2 =
    { 0x64e71ddf, 0x2424, 0x4ec5, { 0x86, 0x27, 0x15, 0x05, 0x08, 0x96, 0xd9, 0xb6 } };

static const GUID IID_IVisualStateManagerFactory_UWP =
    { 0x3976547f, 0x7089, 0x4801, { 0x8f, 0x1d, 0xaa, 0xb7, 0x80, 0x90, 0xd1, 0xa0 } };
static const GUID IID_IVisualTreeHelperStatics_UWP =
    { 0xe75758c4, 0xd25d, 0x4b1d, { 0x97, 0x1f, 0x59, 0x6f, 0x17, 0xf1, 0x2b, 0xaa } };

struct uwp_class_factory
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IComposableFactory_iface;
    IInspectable IStatics_iface;
    const WCHAR *class_name;
    IInspectable IDpStatics_iface;
};

static struct uwp_class_factory vsm_factory_obj;
static struct uwp_class_factory auto_props_factory_obj;
static struct uwp_class_factory vth_factory_obj;
static struct uwp_class_factory feap_factory_obj;
static struct uwp_class_factory scb_factory_obj;
struct uwp_class_factory g_dyn_factories[256];
static WCHAR g_dyn_factory_names[256][128];
static int g_dyn_factory_cnt = 0;
static SRWLOCK g_dyn_factories_lock = SRWLOCK_INIT;

static inline struct uwp_class_factory *uwp_cf_from_any( void *iface )
{
    int i;
    if ((char *)iface >= (char *)&vsm_factory_obj && (char *)iface < (char *)(&vsm_factory_obj + 1))
        return &vsm_factory_obj;
    if ((char *)iface >= (char *)&auto_props_factory_obj && (char *)iface < (char *)(&auto_props_factory_obj + 1))
        return &auto_props_factory_obj;
    if ((char *)iface >= (char *)&vth_factory_obj && (char *)iface < (char *)(&vth_factory_obj + 1))
        return &vth_factory_obj;
    if ((char *)iface >= (char *)&feap_factory_obj && (char *)iface < (char *)(&feap_factory_obj + 1))
        return &feap_factory_obj;
    if ((char *)iface >= (char *)&scb_factory_obj && (char *)iface < (char *)(&scb_factory_obj + 1))
        return &scb_factory_obj;
    AcquireSRWLockExclusive( &g_dyn_factories_lock );
    for (i = 0; i < g_dyn_factory_cnt; i++)
    {
        if ((char *)iface >= (char *)&g_dyn_factories[i] &&
            (char *)iface < (char *)&g_dyn_factories[i + 1])
        {
            ReleaseSRWLockExclusive( &g_dyn_factories_lock );
            return &g_dyn_factories[i];
        }
    }
    ReleaseSRWLockExclusive( &g_dyn_factories_lock );
    return NULL;
}

static const GUID IID_ICanvasStatics_local =
    { 0x40ce5c46, 0x2962, 0x446f, { 0xaa, 0xfb, 0x4c, 0xdc, 0x48, 0x69, 0x39, 0xc9 } };
static const GUID IID_IScrollViewerStatics_local =
    { 0x33d1299d, 0x8d6e, 0x4290, { 0xbf, 0x22, 0x90, 0x5c, 0xcc, 0xd0, 0x4d, 0x31 } };
static const GUID IID_IComboBoxFactory_local =
    { 0xac0d0444, 0xa65b, 0x4abd, { 0x86, 0xdf, 0x30, 0x16, 0x04, 0x9e, 0xfe, 0xdc } };
static const GUID IID_ISolidColorBrushFactory_local =
    { 0xd935ce0c, 0x86f5, 0x4da6, { 0x8a, 0x27, 0xb1, 0x61, 0x9e, 0xf7, 0xf9, 0x2b } };
static const GUID IID_IButtonBaseStatics_Calc =
    { 0x67ef17e1, 0xfe37, 0x474f, { 0x9e, 0x97, 0x3b, 0x5e, 0x0b, 0x30, 0xf2, 0xdf } };

static HRESULT WINAPI uwp_cf_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    TRACE( "uwp_cf_QI enter: cls=%s, iid=%s\n", f ? debugstr_w(f->class_name) : "?", debugstr_guid(iid) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = f ? (void *)&f->IActivationFactory_iface : (void *)iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IButtonBaseStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonBaseStatics_Calc ) ||
        IsEqualGUID( iid, &IID_IToggleButtonStatics_UWP ))
    {
        *out = f && f->IDpStatics_iface.lpVtbl ? (void *)&f->IDpStatics_iface : (void *)&f->IStatics_iface;
        return S_OK;
    }

    if (f == &auto_props_factory_obj ||
        f == &feap_factory_obj ||
        IsEqualGUID( iid, &IID_ICanvasStatics_local ) ||
        IsEqualGUID( iid, &IID_IScrollViewerStatics_local ) ||
        IsEqualGUID( iid, &IID_IGridStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IGridStatics_UWP2 ) ||
        IsEqualGUID( iid, &IID_IVisualTreeHelperStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementStatics_UWP ))
    {
        *out = (void *)&f->IStatics_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IFrameFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPageFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUserControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObjectFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IToggleButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IToggleButtonFactory2_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IGridFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IListViewFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IComboBoxFactory_local ) ||
        IsEqualGUID( iid, &IID_ISolidColorBrushFactory_local ) ||
        IsEqualGUID( iid, &IID_IXamlLightFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentPresenterFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IXamlCompositionBrushBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IFlyoutBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStateTriggerBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutItemBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutItemFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMenuFlyoutSeparatorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMarkupExtensionFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgsFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IBindingFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRelativeSourceFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRadioButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPropertyPathFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObjectCollectionFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IVisualStateManagerFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDataTemplateSelectorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStyleSelectorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IStyleSelectorFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IStateTriggerBaseFactory2_UWP ) ||
        IsEqualGUID( iid, &IID_IDesktopWindowXamlSourceFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDispatcherTimerFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IKeyboardAcceleratorFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentDialogFactory_UWP ))
    {
        *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
        return S_OK;
    }
    if (f && f->class_name &&
        (wcsstr( f->class_name, L"Control" ) ||
         wcsstr( f->class_name, L"Source" ) ||
         wcsstr( f->class_name, L"Box" ) ||
         wcsstr( f->class_name, L"Item" ) ||
         wcsstr( f->class_name, L"View" ) ||
         wcsstr( f->class_name, L"Panel" ) ||
         wcsstr( f->class_name, L"Button" ) ||
         wcsstr( f->class_name, L"Selector" ) ||
         wcsstr( f->class_name, L"Flyout" ) ||
         wcsstr( f->class_name, L"Separator" ) ||
         wcsstr( f->class_name, L"Trigger" ) ||
         wcsstr( f->class_name, L"Transition" ) ||
         wcsstr( f->class_name, L"Extension" ) ||
         wcsstr( f->class_name, L"BitmapSource" ) ||
         wcsstr( f->class_name, L"Accelerator" ) ||
         wcsstr( f->class_name, L"Command" ) ||
         wcsstr( f->class_name, L"Timer" ) ||
         wcsstr( f->class_name, L"Collection" )))
    {
        *out = (void *)&f->IComposableFactory_iface;
        return S_OK;
    }
    /* Unknown IID on factory: log so we can see exact Statics/Factory IID, return Statics/Composable */
    TRACE( "uwp_cf_QI(%s) -> %s\n", f ? debugstr_w(f->class_name) : "?", debugstr_guid(iid) );
    *out = f ? (void *)&f->IStatics_iface : (void *)iface;
    return S_OK;
}
static ULONG WINAPI uwp_cf_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI uwp_cf_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI uwp_cf_GetIids( IActivationFactory *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_cf_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}
static HRESULT WINAPI uwp_cf_GetTrustLevel( IActivationFactory *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_cf_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    TRACE( "uwp_cf_ActivateInstance(%s, %p)\n", debugstr_w(cls), instance );
    return uwp_element_create( cls, NULL, NULL, (void **)instance );
}

static HRESULT WINAPI uwp_cf_CreateInstance( void *iface, IInspectable *outer, IInspectable **inner, void **instance )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    const WCHAR *cls = f ? f->class_name : L"Windows.UI.Xaml.FrameworkElement";
    /* IPropertyPathFactory::CreateInstance(HSTRING path, IPropertyPath **out)
     * takes 2 args after iface, so the output pointer arrives in `inner`. */
    if (!wcscmp( cls, L"Windows.UI.Xaml.PropertyPath" ))
    {
        TRACE( "uwp_cf_CreateInstance(PropertyPath, path=%p, out=%p)\n", outer, inner );
        return uwp_element_create( cls, NULL, NULL, (void **)inner );
    }
    /* If called as I*Statics::get_*Property(iface, IDependencyProperty **out),
     * `outer` is the out-pointer (with *(void **)outer == NULL or on the stack). */
    if (outer && (*(void **)outer == NULL ||
                  (uintptr_t)outer - (uintptr_t)&f < 0x100000 ||
                  (uintptr_t)outer - (uintptr_t)&f > ~(uintptr_t)0x100000))
    {
        return dependency_property_create( NULL, (IDependencyProperty **)outer );
    }
    TRACE( "uwp_cf_CreateInstance(%s, outer=%p, inner=%p, inst=%p)\n", debugstr_w(cls), outer, inner, instance );
    return uwp_element_create( cls, outer, inner, instance );
}

static HRESULT WINAPI uwp_cf_dp_prop( void *iface, void **out )
{
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

static HRESULT WINAPI uwp_cf_dp_prop_slot9( void *iface, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
    if (f && f->class_name && !wcscmp( f->class_name, L"Windows.UI.Xaml.UIElement" ))
    {
        if (!out) return E_POINTER;
        if (!g_routed_event_ptr_pressed)
            dependency_property_create( NULL, &g_routed_event_ptr_pressed );
        *out = g_routed_event_ptr_pressed;
        if (*out) IUnknown_AddRef( (IUnknown *)*out );
        return S_OK;
    }
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}

static const void *uwp_cf_act_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_ActivateInstance,
    [7 ... 15] = uwp_cf_dp_prop,
};

static const void *uwp_cf_comp_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_cf_CreateInstance,
    [7] = uwp_cf_CreateInstance,
    [8] = uwp_cf_CreateInstance,
    [9] = uwp_cf_CreateInstance,
    [10 ... 63] = uwp_generic_noop,
};

static const void *uwp_cf_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 63] = uwp_generic_noop,
    [6] = uwp_cf_dp_prop,
    [9] = uwp_cf_dp_prop_slot9,
    [12] = uwp_cf_dp_prop,
    [15] = uwp_cf_dp_prop,
    [18] = uwp_cf_dp_prop,
    [21] = uwp_cf_dp_prop,
    [24] = uwp_cf_dp_prop,
    [27] = uwp_cf_dp_prop,
    [30] = uwp_cf_dp_prop,
    [33] = uwp_cf_dp_prop,
    [36] = uwp_cf_dp_prop,
    [39] = uwp_cf_dp_prop,
    [42] = uwp_cf_dp_prop,
    [45] = uwp_cf_dp_prop,
};

static const void *uwp_dp_statics_vtbl[32] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 31] = uwp_cf_dp_prop,
};

/* VisualStateManagerStatics (slot 6 = GoToState(Control, HSTRING, bool, bool*), slot 7 = GetVisualStateGroups) */
static HRESULT WINAPI vsm_GoToState( void *iface, void *ctrl, HSTRING state, boolean use_trans, boolean *ret )
{
    struct uwp_element *el = uwp_elem_from_any( ctrl );
    const WCHAR *state_str = WindowsGetStringRawBuffer( state, NULL );
    (void)iface; (void)use_trans;
    if (el && state_str && state_str[0])
    {
        if (!uwp_vsm_goto_state_on_tree( el, state_str ) && el->owner_component && el->owner_component != el)
            uwp_vsm_goto_state_on_tree( el->owner_component, state_str );
        uwp_invalidate_window();
    }
    if (ret) *ret = TRUE;
    return S_OK;
}
static HRESULT WINAPI vsm_GetVisualStateGroups( void *iface, void *obj, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static const void *vsm_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vsm_GoToState,
    [7] = vsm_GetVisualStateGroups,
    [8 ... 15] = uwp_generic_noop,
};
static struct uwp_class_factory vsm_factory_obj =
    { {(const struct IActivationFactoryVtbl *)vsm_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)vsm_vtbl}, L"Windows.UI.Xaml.VisualStateManager" };

/* VisualTreeHelperStatics */
static HRESULT WINAPI vth_FindPoint( void *iface, double x, double y, void *subtree, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindRect( void *iface, double x, double y, double w, double h, void *subtree, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindPointSub( void *iface, double x, double y, void *subtree, boolean all, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindRectSub( void *iface, double x, double y, double w, double h, void *subtree, boolean all, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_GetChild( void *iface, void *ref, INT32 idx, void **out )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!el || idx < 0) return S_OK;
    if (el->children_coll)
        return uwp_vec_GetAt( el->children_coll, (unsigned int)idx, out );
    if (el->content && idx == 0)
    {
        *out = el->content;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}
static HRESULT WINAPI vth_GetChildrenCount( void *iface, void *ref, INT32 *count )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    if (!count) return E_POINTER;
    *count = 0;
    if (!el) return S_OK;
    if (el->children_coll)
    {
        unsigned int sz = 0;
        uwp_vec_get_Size( el->children_coll, &sz );
        *count = (INT32)sz;
    }
    else if (el->content)
    {
        *count = 1;
    }
    return S_OK;
}
static HRESULT WINAPI vth_GetParent( void *iface, void *ref, void **out )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    if (!out) return E_POINTER;
    *out = NULL;
    if (el && el->parent)
    {
        *out = el->parent->outer ? el->parent->outer : &el->parent->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}
static HRESULT WINAPI vth_DisconnectChildren( void *iface, void *el )
{
    return S_OK;
}
static HRESULT WINAPI vth_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}
static const void *vth_vtbl[16] =
{
    [0] = vth_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vth_FindPoint,
    [7] = vth_FindRect,
    [8] = vth_FindPointSub,
    [9] = vth_FindRectSub,
    [10] = vth_GetChild,
    [11] = vth_GetChildrenCount,
    [12] = vth_GetParent,
    [13] = vth_DisconnectChildren,
    [14 ... 15] = uwp_generic_noop,
};
static struct uwp_class_factory vth_factory_obj =
    { {(const struct IActivationFactoryVtbl *)vth_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)vth_vtbl}, L"Windows.UI.Xaml.Media.VisualTreeHelper" };

/* AutomationProperties / ToolTipService / GridStatics attached property helpers */
static HRESULT WINAPI attached_prop_get_dp( void *iface, void **out )
{
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}
static const void *attached_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 63] = uwp_generic_noop,
    [6] = attached_prop_get_dp,
    [9] = attached_prop_get_dp,
    [12] = attached_prop_get_dp,
    [15] = attached_prop_get_dp,
    [18] = attached_prop_get_dp,
    [21] = attached_prop_get_dp,
    [24] = attached_prop_get_dp,
    [27] = attached_prop_get_dp,
    [30] = attached_prop_get_dp,
};
static struct uwp_class_factory auto_props_factory_obj =
    { {(const struct IActivationFactoryVtbl *)attached_statics_vtbl}, {(const struct IInspectableVtbl *)attached_statics_vtbl}, {(const struct IInspectableVtbl *)attached_statics_vtbl}, L"Windows.UI.Xaml.Automation.AutomationProperties" };

static HRESULT WINAPI feap_CreatePeerForElement( void *iface, void *el, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static const void *feap_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = feap_CreatePeerForElement,
    [7] = feap_CreatePeerForElement,
    [8 ... 15] = uwp_generic_noop,
};
static struct uwp_class_factory feap_factory_obj =
    { {(const struct IActivationFactoryVtbl *)feap_statics_vtbl}, {(const struct IInspectableVtbl *)feap_statics_vtbl}, {(const struct IInspectableVtbl *)feap_statics_vtbl}, L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer" };

static HRESULT WINAPI scb_factory_CreateInstanceWithColor( void *iface, Color color, void **out )
{
    HRESULT hr;
    if (!out) return E_POINTER;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.SolidColorBrush", NULL, NULL, out );
    if (SUCCEEDED(hr) && *out)
    {
        struct uwp_element *el = uwp_elem_from_any( *out );
        if (el)
        {
            el->brush_argb = ((UINT32)color.A << 24) | ((UINT32)color.R << 16) | ((UINT32)color.G << 8) | color.B;
            el->has_brush_argb = TRUE;
            el->brush_opacity = 1.0;
        }
    }
    return hr;
}
static const void *scb_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = scb_factory_CreateInstanceWithColor,
    [7 ... 15] = uwp_generic_noop,
};
static struct uwp_class_factory scb_factory_obj =
    { {(const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl}, {(const struct IInspectableVtbl *)scb_factory_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_statics_vtbl}, L"Windows.UI.Xaml.Media.SolidColorBrush" };

static HRESULT WINAPI th_FromLengths( void *iface, double left, double top, double right, double bottom, double *out )
{
    if (!out) return E_POINTER;
    out[0] = left;
    out[1] = top;
    out[2] = right;
    out[3] = bottom;
    return S_OK;
}
static HRESULT WINAPI th_FromUniformLength( void *iface, double uniform, double *out )
{
    if (!out) return E_POINTER;
    out[0] = out[1] = out[2] = out[3] = uniform;
    return S_OK;
}
static const void *th_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = th_FromLengths,
    [7] = th_FromUniformLength,
    [8 ... 15] = uwp_generic_noop,
};
static struct uwp_class_factory th_factory_obj =
    { {(const struct IActivationFactoryVtbl *)th_statics_vtbl}, {(const struct IInspectableVtbl *)th_statics_vtbl}, {(const struct IInspectableVtbl *)th_statics_vtbl}, L"Windows.UI.Xaml.ThicknessHelper" };

HRESULT uwp_get_activation_factory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buf = WindowsGetStringRawBuffer( classid, NULL );
    int i;

    if (!buf || !factory) return E_NOINTERFACE;

    if (!wcscmp( buf, L"Windows.UI.Xaml.ThicknessHelper" ) ||
        !wcscmp( buf, L"Microsoft.UI.Xaml.ThicknessHelper" ))
    {
        *factory = &th_factory_obj.IActivationFactory_iface;
        return S_OK;
    }

    if (!wcscmp( buf, L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer" ))
    {
        *factory = &feap_factory_obj.IActivationFactory_iface;
        return S_OK;
    }
    if (!wcscmp( buf, L"Windows.UI.Xaml.Media.SolidColorBrush" ))
    {
        *factory = &scb_factory_obj.IActivationFactory_iface;
        return S_OK;
    }

    if (!wcscmp( buf, L"Windows.UI.Xaml.Markup.XamlBindingHelper" ) ||
        !wcscmp( buf, L"Microsoft.UI.Xaml.Markup.XamlBindingHelper" ) ||
        !wcscmp( buf, L"Windows.UI.Xaml.Markup.XamlReader" ) ||
        !wcscmp( buf, L"Microsoft.UI.Xaml.Markup.XamlReader" ))
    {
        *factory = (IActivationFactory *)&xbh_factory_obj;
        return S_OK;
    }
    if (!wcscmp( buf, L"Windows.UI.Xaml.VisualStateManager" ) ||
        !wcscmp( buf, L"Microsoft.UI.Xaml.VisualStateManager" ))
    {
        *factory = &vsm_factory_obj.IActivationFactory_iface;
        return S_OK;
    }
    if (!wcscmp( buf, L"Windows.UI.Xaml.Media.VisualTreeHelper" ) ||
        !wcscmp( buf, L"Microsoft.UI.Xaml.Media.VisualTreeHelper" ))
    {
        *factory = &vth_factory_obj.IActivationFactory_iface;
        return S_OK;
    }
    if (!wcscmp( buf, L"Windows.UI.Xaml.Automation.AutomationProperties" ) ||
        !wcscmp( buf, L"Windows.UI.Xaml.Controls.ToolTipService" ))
    {
        *factory = &auto_props_factory_obj.IActivationFactory_iface;
        return S_OK;
    }

    /* For UWP apps (Calculator, Store), serve all Windows.UI.Xaml.* and Microsoft.UI.Xaml.* runtime classes with our UWP multi-iface element factory;
     * also serve core Windows.UI.Xaml.Controls panels/buttons outside WhatsApp so UWP controls use the layout/pointer engine. */
    if ((is_uwp_xaml_app() ||
         (xaml_get_app_profile()->serve_core_controls &&
          (!wcscmp( buf, L"Windows.UI.Xaml.Controls.Grid" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.StackPanel" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Panel" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Border" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Canvas" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Button" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.TextBlock" ) ||
           !wcscmp( buf, L"CalculatorApp.Controls.CalculationResult" )))) &&
        (!wcsncmp( buf, L"Windows.UI.Xaml.", 16 ) || !wcsncmp( buf, L"Microsoft.UI.Xaml.", 18 ) ||
         !wcscmp( buf, L"CalculatorApp.Controls.CalculationResult" )) &&
        wcscmp( buf, L"Windows.UI.Xaml.Application" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Window" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.PropertyMetadata" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.DependencyProperty" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.ResourceDictionary" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.SizeHelper" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Data.PropertyChangedEventArgs" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Data.CollectionViewSource" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.PropertyPath" ) != 0 &&
        wcscmp( buf, L"Microsoft.UI.Xaml.PropertyPath" ) != 0 &&
        wcscmp( buf, L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider" ) != 0)
    {
        AcquireSRWLockExclusive( &g_dyn_factories_lock );
        for (i = 0; i < g_dyn_factory_cnt; i++)
        {
            if (!wcscmp( g_dyn_factory_names[i], buf ))
            {
                *factory = &g_dyn_factories[i].IActivationFactory_iface;
                ReleaseSRWLockExclusive( &g_dyn_factories_lock );
                return S_OK;
            }
        }
        if (g_dyn_factory_cnt < 256)
        {
            int idx = g_dyn_factory_cnt++;
            lstrcpynW( g_dyn_factory_names[idx], buf, 128 );
            g_dyn_factories[idx].IActivationFactory_iface.lpVtbl = (const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl;
            g_dyn_factories[idx].IComposableFactory_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cf_comp_vtbl;
            g_dyn_factories[idx].IStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cf_statics_vtbl;
            g_dyn_factories[idx].IDpStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dp_statics_vtbl;
            g_dyn_factories[idx].class_name = g_dyn_factory_names[idx];
            *factory = &g_dyn_factories[idx].IActivationFactory_iface;
            ReleaseSRWLockExclusive( &g_dyn_factories_lock );
            return S_OK;
        }
        ReleaseSRWLockExclusive( &g_dyn_factories_lock );
    }

    return E_NOINTERFACE;
}



