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
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

const GUID CLSID_WICImagingFactory_local =
    { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
const GUID IID_IWICImagingFactory_local =
    { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
const GUID GUID_WICPixelFormat32bppBGRA_local =
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
struct xaml_metadata_provider;



struct notify_property_changed;

struct property_changed_args;

struct reference_int32;

struct command_iface;

/* Generic event delegate: { QueryInterface, AddRef, Release, Invoke(sender, args) }. */
struct xaml_event_handler;

/* UWP Windows.UI.Xaml interface GUIDs */
static const GUID IID_IDependencyObject_UWP =
    { 0x5c526665, 0xf60e, 0x4912, { 0xaf, 0x59, 0x5f, 0xe0, 0x68, 0x0f, 0x08, 0x9d } };
static const GUID IID_IDependencyObject2_UWP =
    { 0x29fed85d, 0x3d22, 0x43a1, { 0xad, 0xd0, 0x17, 0x02, 0x7c, 0x08, 0xb2, 0x12 } };
const GUID IID_IDependencyObjectFactory_UWP =
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
const GUID IID_IUIElementFactory_UWP =
    { 0xb61c4d87, 0x70e5, 0x412e, { 0xb5, 0x20, 0x1a, 0x41, 0xee, 0x76, 0xbb, 0xf4 } };
const GUID IID_IUIElementStatics_UWP =
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
static const GUID IID_IDataTemplateSelector_UWP =
    { 0xa907d496, 0x46a0, 0x4cd7, { 0x8d, 0xbe, 0xf9, 0xa5, 0x81, 0xdf, 0x60, 0xb1 } };
static const GUID IID_IDataTemplateSelector2_UWP =
    { 0x3792bc47, 0x8c4b, 0x4257, { 0xa5, 0xae, 0xcc, 0x3f, 0x8e, 0xd7, 0x86, 0xeb } };
static const GUID IID_IDataTemplateSelectorOverrides_UWP =
    { 0x2d162925, 0x5966, 0x4bfa, { 0x86, 0x38, 0x4d, 0x34, 0x56, 0x89, 0xf6, 0xcf } };
static const GUID IID_IDataTemplateSelectorOverrides2_UWP =
    { 0x6c3fb5fc, 0x8e84, 0x4c91, { 0xac, 0x15, 0x04, 0xa1, 0xd8, 0x85, 0x50, 0x0f } };
static const GUID IID_IFrameworkElement7_UWP =
    { 0x792a5d91, 0x62a1, 0x40bf, { 0xa0, 0xce, 0xf9, 0xc1, 0x31, 0xfc, 0xb7, 0xa7 } };
static const GUID IID_IFrameworkElement7_Proj =
    { 0x2263886c, 0xc069, 0x570f, { 0xb9, 0xcc, 0x9e, 0x21, 0xdd, 0x02, 0x8d, 0x8e } };
static const GUID IID_IEffectiveViewportChangedEventArgs_UWP =
    { 0xbdff696d, 0x3f22, 0x41f9, { 0x97, 0xa6, 0x88, 0x31, 0x34, 0xa7, 0x61, 0x13 } };
static const GUID IID_IFrameworkElementOverrides_UWP =
    { 0xda007e54, 0xb3c2, 0x4b9a, { 0xaa, 0x8e, 0xd3, 0xf0, 0x71, 0x26, 0x2b, 0x97 } };
static const GUID IID_IFrameworkElementOverrides2_UWP =
    { 0xcb5cd2b9, 0xe3b4, 0x458c, { 0xb6, 0x4e, 0x14, 0x34, 0xfd, 0x1b, 0xd8, 0x8a } };
const GUID IID_IFrameworkElementFactory_UWP =
    { 0xdeaee126, 0x03ca, 0x4966, { 0xb5, 0x76, 0x60, 0x4c, 0xce, 0x93, 0xb5, 0xe8 } };
const GUID IID_IFrameworkElementStatics_UWP =
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
const GUID IID_IControlFactory_UWP =
    { 0xb12b1d5a, 0x6419, 0x4e16, { 0xb2, 0x12, 0xa4, 0x53, 0x36, 0xb7, 0x57, 0x78 } };
static const GUID IID_IContentControl_UWP =
    { 0xa26dd1dc, 0xcd44, 0x435c, { 0xbe, 0x94, 0x01, 0xd6, 0x24, 0x1c, 0x23, 0x1c } };
static const GUID IID_IContentControlOverrides_UWP =
    { 0x3874208b, 0x57d6, 0x4697, { 0x9f, 0x01, 0xf6, 0xf6, 0x64, 0x23, 0xa2, 0x2b } };
const GUID IID_IContentControlFactory_UWP =
    { 0x6584ebdf, 0xcbc7, 0x4e19, { 0x84, 0x3a, 0xa1, 0x4f, 0x24, 0xa7, 0x93, 0xea } };
const GUID IID_IContentControlFactory_UWP2 =
    { 0xab017b19, 0x196e, 0x4ed5, { 0xa2, 0x7f, 0x55, 0xc5, 0x8f, 0xb1, 0x67, 0x3f } };
static const GUID IID_IUserControl_UWP =
    { 0xa7a69ec9, 0xea35, 0x4679, { 0xbf, 0x29, 0xf4, 0xf0, 0x92, 0x86, 0xd3, 0x14 } };
const GUID IID_IUserControlFactory_UWP =
    { 0x38b1ed92, 0xa28a, 0x4972, { 0x93, 0xdf, 0xf4, 0xf7, 0x59, 0xb8, 0xaf, 0xd2 } };
static const GUID IID_IPage_UWP =
    { 0xc4b27075, 0xe641, 0x453e, { 0x82, 0x4d, 0x01, 0x2f, 0xc7, 0xcf, 0x95, 0xcf } };
static const GUID IID_IPageOverrides_UWP =
    { 0x230b9cb8, 0x3419, 0x4306, { 0xaa, 0xbe, 0x4a, 0x8e, 0x68, 0x0d, 0x36, 0x27 } };
const GUID IID_IPageFactory_UWP =
    { 0xdfa149ac, 0x1849, 0x445e, { 0x93, 0x7c, 0x40, 0xa9, 0x59, 0x0c, 0xc0, 0x76 } };
static const GUID IID_IFrame_UWP =
    { 0x68ea500b, 0x3fe9, 0x4735, { 0xb6, 0xa0, 0xe7, 0x60, 0x19, 0x33, 0xb0, 0x89 } };
static const GUID IID_INavigate_UWP =
    { 0xe7337f7a, 0x5e13, 0x4d32, { 0xb5, 0x72, 0x89, 0x35, 0x6e, 0x98, 0x1d, 0x96 } };
static const GUID IID_IButtonBase_UWP =
    { 0xfa002c1a, 0x494e, 0x46cf, { 0x91, 0xd4, 0xe1, 0x4a, 0x8d, 0x79, 0x86, 0x74 } };
const GUID IID_IButtonBaseFactory_UWP =
    { 0x389b7c71, 0x5220, 0x42b2, { 0x99, 0x92, 0x26, 0x90, 0xc1, 0xa6, 0x70, 0x2f } };
const GUID IID_IButtonBaseStatics_UWP =
    { 0x541c8d6c, 0x0283, 0x4581, { 0xb9, 0x45, 0x2a, 0x64, 0xc2, 0x8a, 0x06, 0x46 } };
static const GUID IID_IButton_UWP =
    { 0x280335ae, 0x5570, 0x46c7, { 0x8e, 0x0b, 0x60, 0x2b, 0xe7, 0x12, 0x29, 0xa2 } };
static const GUID IID_IButtonWithFlyout_UWP =
    { 0x06e32cce, 0xdfdd, 0x3639, { 0x94, 0xa5, 0xc7, 0xff, 0x91, 0x19, 0x93, 0xf8 } };
const GUID IID_IButtonFactory_UWP =
    { 0xe2d795e3, 0x85c0, 0x4de2, { 0xba, 0xc1, 0x52, 0x94, 0xca, 0x01, 0x1a, 0x78 } };
static const GUID IID_IToggleButton_UWP =
    { 0x589877fb, 0x0fc7, 0x4036, { 0x9d, 0x8b, 0x12, 0x7d, 0xfa, 0x75, 0xc1, 0x6d } };
static const GUID IID_IToggleButtonOverrides_UWP =
    { 0x84e5b964, 0x4d3d, 0x49c1, { 0xab, 0xe7, 0xcb, 0xd3, 0xc5, 0x77, 0x49, 0x0d } };
const GUID IID_IToggleButtonFactory_UWP =
    { 0x80a13c19, 0x843a, 0x451c, { 0x8c, 0xf5, 0x44, 0xc7, 0x01, 0xb0, 0xe2, 0x16 } };
const GUID IID_IToggleButtonFactory2_UWP =
    { 0x13300b06, 0xbd10, 0x4e09, { 0xbf, 0xf7, 0x71, 0xef, 0xb8, 0xbb, 0xb4, 0x2b } };
const GUID IID_IToggleButtonStatics_UWP =
    { 0xaf1eab12, 0x0128, 0x4f67, { 0x9c, 0x5a, 0x82, 0x32, 0x0c, 0x44, 0x5d, 0x19 } };
static const GUID IID_IHyperlinkButton_UWP =
    { 0x325c44e0, 0x9a03, 0x4bf3, { 0xab, 0xd6, 0x6f, 0xbb, 0x46, 0xc9, 0xa4, 0x86 } };
static const GUID IID_IPanel_UWP =
    { 0xa50a4bbd, 0x8361, 0x469c, { 0x90, 0xda, 0xe9, 0xa4, 0x0c, 0x74, 0x74, 0xdf } };
const GUID IID_IPanelFactory_UWP =
    { 0xbfd07f53, 0xf8ca, 0x4e47, { 0x94, 0x9e, 0x9e, 0x80, 0x42, 0x9b, 0x3d, 0x16 } };
const GUID IID_IPanelFactory_UWP2 =
    { 0xeee68111, 0xc7cc, 0x433f, { 0x95, 0xcd, 0xd6, 0x30, 0xc3, 0x43, 0x02, 0xdd } };
const GUID IID_IGridFactory_UWP =
    { 0xae814041, 0xc531, 0x43b4, { 0xbf, 0x99, 0x12, 0xf5, 0x06, 0xf7, 0xb0, 0x1c } };
const GUID IID_IListViewFactory_UWP =
    { 0xbdff696d, 0x3f22, 0x41f9, { 0x97, 0xa6, 0x88, 0x31, 0x34, 0xa7, 0x61, 0x13 } };
const GUID IID_IXamlLightFactory_UWP =
    { 0x87ded768, 0x3055, 0x43b8, { 0x8e, 0xf6, 0x79, 0x8d, 0xc4, 0xc2, 0x32, 0x9a } };
const GUID IID_IContentPresenterFactory_UWP =
    { 0x3339d4f7, 0x4815, 0x44c4, { 0x9f, 0x76, 0x68, 0xc8, 0x38, 0x80, 0xef, 0x10 } };
const GUID IID_IXamlCompositionBrushBaseFactory_UWP =
    { 0x394f0823, 0x2451, 0x4ed8, { 0xbd, 0x24, 0x48, 0x81, 0x49, 0xb3, 0x42, 0x8d } };
static const GUID IID_IGrid_UWP =
    { 0x709133c7, 0x38f3, 0x5083, { 0xa4, 0xbb, 0x06, 0xa5, 0x48, 0xa1, 0xd2, 0x31 } };
static const GUID IID_IGrid_UWP2 =
    { 0xfd104460, 0x2e15, 0x4ba3, { 0x8b, 0x8f, 0xfa, 0x69, 0x3a, 0x41, 0x61, 0xe9 } };
const GUID IID_IFlyoutBaseFactory_UWP =
    { 0xf82b435e, 0x65b3, 0x41c6, { 0xa9, 0xe2, 0x77, 0xb6, 0x7b, 0xc4, 0xc0, 0x0c } };
const GUID IID_IStateTriggerBaseFactory_UWP =
    { 0x0fa0f896, 0x64c0, 0x45fb, { 0x8d, 0x24, 0xfb, 0x83, 0x29, 0x8c, 0x0d, 0x93 } };
const GUID IID_IMenuFlyoutItemBaseFactory_UWP =
    { 0x9727b2e3, 0xb2b6, 0x49dc, { 0x8c, 0x22, 0xe0, 0x13, 0x8b, 0x94, 0x15, 0x59 } };
const GUID IID_IMenuFlyoutItemFactory_UWP =
    { 0xc5aa39e2, 0x44f3, 0x455e, { 0x9c, 0x5b, 0x62, 0xbb, 0x6a, 0xfc, 0xe3, 0xad } };
const GUID IID_IGridStatics_UWP =
    { 0xed02d274, 0x81f1, 0x5ac2, { 0xa0, 0xc3, 0xbb, 0xda, 0xf5, 0xa3, 0xff, 0xee } };
const GUID IID_IGridStatics_UWP2 =
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
static const GUID IID_INavigationViewItem_UWP =
    { 0xeb24c20b, 0x9816, 0x4ac7, { 0x8c, 0xff, 0x36, 0xf6, 0x7a, 0x11, 0x8f, 0x4e } };
static const GUID IID_IFrameProj_UWP =
    { 0x3f367f87, 0x9f5a, 0x4b04, { 0xb8, 0x18, 0xb5, 0x54, 0xc0, 0x69, 0x59, 0x7a } };
static const GUID IID_IRoutedEventArgs_QI_UWP =
    { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };
static const GUID IID_IRoutedEventArgs_Clone_UWP =
    { 0x443ecc75, 0x7e5d, 0x48a5, { 0x93, 0x1a, 0x30, 0x0e, 0x46, 0xa4, 0x91, 0x6a } };
static const GUID IID_IRoutedEventArgs2_UWP =
    { 0xfd1d67ae, 0xeafb, 0x4079, { 0xbe, 0x80, 0x6d, 0xc9, 0x2a, 0x03, 0xae, 0xdf } };
static const GUID IID_IRoutedEventArgs_WinUI3 =
    { 0x0908c407, 0x1c7d, 0x5de3, { 0x9c, 0x50, 0xd9, 0x71, 0xc6, 0x2e, 0xc8, 0xec } };
static const GUID IID_ISplitView_UWP =
    { 0x97222f31, 0x3844, 0x429e, { 0x93, 0x9c, 0x16, 0x73, 0x15, 0x53, 0x22, 0xa1 } };
static const GUID IID_ISplitView2_UWP =
    { 0x0ea12d8d, 0x1171, 0x407b, { 0x96, 0xd3, 0x5e, 0x1b, 0x47, 0x0c, 0x3e, 0xc5 } };
static const GUID IID_IItemsRepeater_UWP =
    { 0x0bd894f2, 0xedfc, 0x5ddf, { 0xa1, 0x66, 0x2d, 0xb1, 0x4b, 0xbf, 0xdf, 0x35 } };
static const GUID IID_IItemsRepeaterSizingTarget_UWP =
    { 0xa686ff0f, 0xb61a, 0x4ea8, { 0xbd, 0x4b, 0x5f, 0xf4, 0xd8, 0x5a, 0x53, 0xf6 } };
static const GUID IID_IContentPresenter_UWP =
    { 0xa5f6b7f5, 0x8c34, 0x4cd0, { 0x8a, 0xbc, 0xf3, 0x03, 0x83, 0x11, 0xa4, 0x6b } };
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
static const GUID IID_IFrame3_UWP =
    { 0xbf2195a9, 0xf4ea, 0x4336, { 0x97, 0x7c, 0xf8, 0xfc, 0xf7, 0x8b, 0x0d, 0x9e } };
static const GUID IID_IFrame5_UWP =
    { 0x453fb92e, 0x82b5, 0x5f93, { 0x92, 0xab, 0x15, 0x34, 0x18, 0x06, 0xc9, 0xda } };
static const GUID IID_IMediaPlayerElement_UWP =
    { 0x3b4ca526, 0x2e25, 0x4598, { 0xbc, 0x71, 0xd4, 0x91, 0xf8, 0xe8, 0xde, 0x39 } };
static const GUID IID_IVisualStateGroup_UWP =
    { 0xe4f9d9a4, 0xe028, 0x44de, { 0x9b, 0x15, 0x49, 0x29, 0xae, 0x0a, 0x26, 0xc2 } };
static const GUID IID_IVisualState_UWP =
    { 0x6320affc, 0xc31a, 0x4450, { 0xaf, 0xde, 0xf6, 0xea, 0x7b, 0xd1, 0xf5, 0x86 } };
static const GUID IID_IStateTriggerBase_UWP =
    { 0x48b20698, 0xaf06, 0x466c, { 0x80, 0x52, 0x93, 0x66, 0x6d, 0xde, 0x0e, 0x49 } };
static const GUID IID_IStateTriggerBaseProtected_UWP =
    { 0x3c41e253, 0x8d14, 0x4216, { 0x99, 0x4c, 0xf9, 0x93, 0x04, 0x29, 0xf6, 0xe5 } };
static const GUID IID_IMarkupExtension_UWP =
    { 0x1bc32a2c, 0x2236, 0x45f0, { 0x88, 0x54, 0x5b, 0x3b, 0x19, 0x26, 0xd1, 0x20 } };
static const GUID IID_IMarkupExtensionOverrides_UWP =
    { 0x393d791f, 0x5389, 0x452c, { 0x88, 0x59, 0x8c, 0x54, 0x92, 0x04, 0x26, 0x19 } };
const GUID IID_IMarkupExtensionFactory_UWP =
    { 0x65329c05, 0xfb5a, 0x4567, { 0x9d, 0x55, 0x5c, 0xdf, 0xba, 0xda, 0x27, 0x39 } };
static const GUID IID_IXamlBindingHelperStatics_UWP =
    { 0xf65cfb71, 0xc80c, 0x4ffa, { 0x86, 0xee, 0x55, 0x87, 0x54, 0xee, 0x33, 0x6d } };
const GUID IID_IComponentConnector_UWP_g =
    { 0xf6790987, 0xe6e5, 0x47f2, { 0x92, 0xc6, 0xec, 0xcc, 0xe4, 0xba, 0x15, 0x9a } };
const GUID IID_IComponentConnector2_UWP_g =
    { 0xdc8f368b, 0xeccc, 0x498e, { 0xb1, 0x39, 0x91, 0x14, 0x22, 0x54, 0xd7, 0xae } };
const GUID IID_IXamlMetadataProvider_UWP_g =
    { 0xb3765d69, 0x68a5, 0x4b32, { 0x88, 0x61, 0xfd, 0xb9, 0x0c, 0x1f, 0x58, 0x36 } };
const GUID IID_IXamlType_UWP_g =
    { 0x7920eab1, 0xb2e5, 0x41ea, { 0x8d, 0x86, 0xe1, 0x0f, 0x7b, 0x37, 0x73, 0x37 } };
const GUID IID_ICommand_UWP =
    { 0xe5af3542, 0xca67, 0x4080, { 0x99, 0x5b, 0x70, 0x9d, 0xd1, 0x37, 0x92, 0xdf } };
const GUID IID_ICommand_UWP2 =
    { 0xe5af3542, 0xca67, 0x4081, { 0x99, 0x5b, 0x70, 0x9d, 0xd1, 0x37, 0x92, 0xdf } };
const GUID IID_IPropertyValue_g =
    { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };
const GUID IID_IDependencyPropertyChangedEventArgs_UWP =
    { 0x81212c2b, 0x24d0, 0x4957, { 0xab, 0xc3, 0x22, 0x44, 0x70, 0xa9, 0x3a, 0x4e } };
static const GUID IID_IDataContextChangedEventArgs_UWP =
    { 0x7da68e21, 0x0b8f, 0x4f9f, { 0xa1, 0x43, 0xf8, 0xe7, 0x78, 0x01, 0x36, 0xa2 } };
static const GUID IID_IRadioButton_UWP =
    { 0xd20e4c28, 0xf18b, 0x491a, { 0xa0, 0xf1, 0x45, 0x9a, 0x93, 0x4a, 0xa4, 0x69 } };
static const GUID IID_IRadioButtons_WinUI =
    { 0xdd372d64, 0xb740, 0x5739, { 0x8d, 0x24, 0xb6, 0xde, 0xcc, 0x91, 0xf4, 0x08 } };
const GUID IID_IUriRuntimeClass_g =
    { 0x9e365e57, 0x48b2, 0x4160, { 0x95, 0x6f, 0xc7, 0x38, 0x51, 0x20, 0xbb, 0xfc } };

/* CalculatorApp / MCG additional discovered interface IIDs */
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
static const GUID IID_IGrid3_UWP =
    { 0x12dfc5fc, 0x2342, 0x4dd2, { 0x9e, 0x7d, 0x20, 0x90, 0xa1, 0x71, 0xd1, 0xef } };
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
    /* IHyperlink, IHyperlink2, IHyperlink3, IHyperlink4 */
    { 0x0fe2363b, 0x14e9, 0x4152, { 0x9e, 0x58, 0x5a, 0xea, 0x5b, 0x21, 0xf0, 0x8d } },
    { 0xca138509, 0xf203, 0x4eb2, { 0x9b, 0x19, 0x1d, 0x65, 0x74, 0xc5, 0x45, 0xb0 } },
    { 0x1763d12c, 0x80a8, 0x468d, { 0xa2, 0x47, 0x05, 0xf7, 0xd9, 0x96, 0x8c, 0x97 } },
    { 0xdf622217, 0x29b6, 0x46a2, { 0x99, 0x1a, 0x90, 0x02, 0xa7, 0x4c, 0x67, 0x7f } },
    /* MenuFlyout, MenuFlyoutItem, Flyout, Hyperlink, RichTextBlock, Storyboard, ValueConverter */
    { 0xad3210f1, 0x985b, 0x4171, { 0xbb, 0xba, 0x67, 0xbc, 0xc7, 0x29, 0x64, 0x9b } },
    { 0x507c7bad, 0xd9d1, 0x4c05, { 0x9d, 0x4b, 0xcd, 0xa8, 0xde, 0x9a, 0xb2, 0x42 } },
    { 0x0ae266eb, 0x028c, 0x4423, { 0xa8, 0xe3, 0x98, 0x9f, 0xd9, 0xdd, 0x71, 0x26 } },
    { 0x6584ebdf, 0xcbc7, 0x4e19, { 0x84, 0x3a, 0xa1, 0x4f, 0x24, 0xa7, 0x93, 0xea } },
    { 0xccebaca3, 0x3b5c, 0x4f4c, { 0x9b, 0xfd, 0x86, 0x88, 0x7b, 0xc7, 0x97, 0x72 } },
    { 0xe5fff9e2, 0xb968, 0x49e7, { 0x97, 0xd4, 0x8c, 0xca, 0x2a, 0xc3, 0xae, 0x7c } },
    { 0x9c7dc0c6, 0x7a9f, 0x45b6, { 0x88, 0x18, 0x82, 0x2c, 0x54, 0x9b, 0xa0, 0x94 } },
    { 0xd45c1e6e, 0x3594, 0x460e, { 0x98, 0x1a, 0x32, 0x27, 0x1b, 0xd3, 0xaa, 0x06 } },
    /* IRichTextBlock2..5 */
    { 0x3f209013, 0x03e7, 0x4508, { 0x96, 0x4a, 0x91, 0xae, 0xda, 0xb3, 0xd1, 0x1e } },
    { 0x63c1a3aa, 0x521a, 0x43f4, { 0xb8, 0xa5, 0xd6, 0xe3, 0xc3, 0xc9, 0xe2, 0xc4 } },
    { 0x50f2b6dc, 0x4b94, 0x418f, { 0x9e, 0x2e, 0xb0, 0x6e, 0x33, 0xf2, 0xa8, 0xd5 } },
    { 0x4172441c, 0x915a, 0x4361, { 0x99, 0x27, 0x44, 0x69, 0x27, 0x46, 0x23, 0xc1 } },
    { 0x394b0030, 0x9f4e, 0x41e6, { 0x85, 0x2c, 0x22, 0x7a, 0x82, 0x02, 0x6f, 0x94 } },
    /* IBlock, ITextElement, IParagraph, ISpan */
    { 0x4bce0016, 0xdd47, 0x4350, { 0x8c, 0xb0, 0xe1, 0x71, 0x60, 0x0a, 0xc8, 0x96 } },
    { 0xf83ef59a, 0xfa61, 0x4bef, { 0xae, 0x33, 0x0b, 0x0a, 0xd7, 0x56, 0xa8, 0x4d } },
    { 0xc5829095, 0x4919, 0x461e, { 0x92, 0x62, 0xf1, 0x05, 0xf7, 0x8b, 0x31, 0x43 } },
    { 0x54f03373, 0xc273, 0x4f3b, { 0xb3, 0x71, 0x6e, 0x2b, 0x4b, 0x2a, 0x96, 0x91 } },
    { 0x7b01c14b, 0xe5e6, 0x3c6e, { 0x9d, 0x1b, 0x93, 0xa4, 0x7c, 0xbf, 0x7d, 0x96 } },
    /* DataTemplateSelector & StyleSelector interfaces */
    { 0xa26dd1dc, 0xcd44, 0x435c, { 0xbe, 0x94, 0x01, 0xd6, 0x24, 0x1c, 0x23, 0x1c } },
    { 0x51ed9d7e, 0x2b53, 0x475b, { 0x9c, 0x88, 0x0c, 0x18, 0x32, 0xc8, 0x35, 0x1a } },
    { 0x8af77d73, 0xaa01, 0x471e, { 0xbe, 0xdd, 0x8b, 0xad, 0x86, 0x21, 0x9b, 0x77 } },
    { 0x595e9547, 0xcdff, 0x4b92, { 0xb7, 0x73, 0xab, 0x39, 0x68, 0x78, 0xf3, 0x53 } },
    { 0xa907d496, 0x46a0, 0x4cd7, { 0x8d, 0xbe, 0xf9, 0xa5, 0x81, 0xdf, 0x60, 0xb1 } },
    { 0x2d162925, 0x5966, 0x4bfa, { 0x86, 0x38, 0x4d, 0x34, 0x56, 0x89, 0xf6, 0xcf } },
    /* WinUI 2.x / UWP Controls (TabViewItem, Expander, AnimatedIcon, ProgressRing, etc.) */
    { 0x189826ad, 0xf6f2, 0x533e, { 0x9d, 0xdb, 0xb6, 0x60, 0x0e, 0x88, 0x67, 0x5b } }, /* Microsoft.UI.Xaml.Controls.IProgressRing */
    /* IImage3, IShape2, ITextBlock5 (GetAlphaMask) */
    { 0x06ebe792, 0x47c2, 0x4d92, { 0xa4, 0x88, 0x8c, 0xd6, 0x85, 0xd0, 0x6a, 0xca } },
    { 0x97248dba, 0x49f2, 0x49a4, { 0xa5, 0xdd, 0x16, 0x4d, 0xf8, 0x24, 0xdb, 0x14 } },
    { 0xb014f270, 0x9808, 0x4adc, { 0xb4, 0x52, 0x60, 0xcd, 0xa5, 0x41, 0x00, 0xc1 } },
    /* ILinearGradientBrush, IGradientStop, IFrameNavigationOptions, IRoutedEvent */
    { 0x8e96d16b, 0xbb84, 0x4c6f, { 0x9d, 0xbf, 0x9d, 0x6c, 0x5c, 0x6d, 0x9c, 0x39 } },
    { 0x665f44fe, 0x2e59, 0x4c4a, { 0xab, 0x53, 0x07, 0x6a, 0x10, 0x0c, 0xcd, 0x81 } },
    { 0xb539ad2a, 0x9fb7, 0x4536, { 0x8d, 0xe0, 0xf7, 0x82, 0x06, 0x1c, 0x62, 0x62 } },
    { 0xa6b25818, 0x43c1, 0x4c70, { 0x86, 0x5c, 0x7b, 0xdd, 0x5a, 0x32, 0xe3, 0x27 } },
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
    { 0x688ea9b9, 0x1e4e, 0x4596, { 0x86, 0xe3, 0x42, 0x8b, 0x27, 0x33, 0x4f, 0xaf } },
    { 0xc975905c, 0x3c36, 0x4229, { 0x81, 0x7b, 0x17, 0x8f, 0x64, 0xc0, 0xe1, 0x13 } },
    { 0x7e9f3d59, 0x0f07, 0x4bc9, { 0x97, 0x7d, 0x03, 0x76, 0x3f, 0xf8, 0x15, 0x4f } },
    { 0xf57ae6ca, 0xd1a6, 0x4b90, { 0xa4, 0xe9, 0x54, 0xdf, 0x1b, 0xa8, 0xd2, 0xec } },
    { 0x67918e1b, 0xf2d5, 0x44d1, { 0x8b, 0x84, 0x92, 0xb8, 0x7f, 0x80, 0xa3, 0x50 } },
    { 0x3d0813ba, 0x6890, 0x4537, { 0xbf, 0xe5, 0x79, 0x6d, 0x94, 0x58, 0xed, 0xd6 } },
    { 0x53c9f23a, 0xf25d, 0x446a, { 0x99, 0xb8, 0xc7, 0xae, 0xb8, 0x39, 0x05, 0x0f } },
    { 0x754c3266, 0x0529, 0x45e3, { 0x87, 0x48, 0xbf, 0x74, 0x7d, 0x15, 0x83, 0x57 } },
    { 0xf82b435e, 0x65b3, 0x41c6, { 0xa9, 0xe2, 0x77, 0xb6, 0x7b, 0xc4, 0xc0, 0x0c } },
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
const GUID IID_IMenuBar_WinUI =
    { 0xba97f337, 0x8f1e, 0x5141, { 0xb5, 0x3f, 0xe7, 0x7a, 0x8b, 0xa3, 0xeb, 0xbd } };
const GUID IID_IMenuBarItem_WinUI =
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
static const GUID IID_IContentDialog_UWP_Alt =
    { 0x38dc4404, 0xd24e, 0x40d8, { 0x94, 0x15, 0x34, 0x94, 0x64, 0xc1, 0xaf, 0xdc } };
static const GUID IID_ISwapChainPanel_UWP =
    { 0xc589644f, 0xeba8, 0x427a, { 0xb7, 0x5a, 0x9f, 0x1f, 0x93, 0xa1, 0x1a, 0xe9 } };
static const GUID IID_ISwapChainPanelNative_UWP =
    { 0xf92f19d2, 0x3ade, 0x45a6, { 0xa2, 0x0c, 0xf6, 0xf1, 0xea, 0x90, 0x55, 0x4b } };
static const GUID IID_ISwapChainPanelNative2_UWP =
    { 0xd5a2f60c, 0x37b2, 0x44a2, { 0x93, 0x7b, 0x8d, 0x8e, 0xb9, 0x72, 0x68, 0x21 } };
static const GUID IID_IContentDialog2_UWP =
    { 0x2f93eb45, 0xee43, 0x4303, { 0x9b, 0x38, 0x3f, 0xe1, 0xa1, 0x11, 0xec, 0xbf } };
const GUID IID_IContentDialogFactory_UWP =
    { 0x05557178, 0x9d8e, 0x4315, { 0xb3, 0x7d, 0x68, 0x0c, 0x14, 0x01, 0x2c, 0x35 } };
const GUID IID_IContentDialogClosedEventArgs_UWP =
    { 0x4d6f20f2, 0x75d5, 0x4269, { 0x9f, 0x5c, 0xd6, 0x5b, 0x73, 0xb5, 0x33, 0x2d } };
static const GUID IID_IUIElement4_Real_UWP =
    { 0x69145cd4, 0x199a, 0x4657, { 0x9e, 0x57, 0xe9, 0x9e, 0x8f, 0x13, 0x67, 0x12 } };
static const GUID IID_IUIElement5_Real_UWP =
    { 0x8eed9bc2, 0xa58c, 0x4453, { 0xaf, 0x0f, 0xa9, 0x2e, 0xe0, 0x6d, 0x03, 0x17 } };
static const GUID IID_IButtonWithFlyout_Real_UWP =
    { 0x09108f87, 0xdf6c, 0x4180, { 0x9b, 0x3a, 0xe6, 0x08, 0x45, 0x82, 0x58, 0x11 } };
static const GUID IID_IUIElement7_UWP =
    { 0xcafc4968, 0x6369, 0x4249, { 0x80, 0xf9, 0x3d, 0x65, 0x63, 0x19, 0xe8, 0x11 } };
const GUID IID_IDesktopWindowXamlSourceFactory_UWP =
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
static const GUID IID_IDataTemplate_UWP =
    { 0x9910aec7, 0x8ab5, 0x4118, { 0x9b, 0xc6, 0x09, 0xf4, 0x5a, 0x35, 0x07, 0x3d } };
static const GUID IID_IAutoSuggestBox_Real_UWP =
    { 0xf4a91dd8, 0xd979, 0x4381, { 0x86, 0x52, 0xbd, 0xa0, 0x34, 0x2a, 0x76, 0x5e } };
static const GUID IID_IAutoSuggestBox2_Real_UWP =
    { 0xaa87ddde, 0xe679, 0x45b2, { 0xa7, 0xc9, 0x9a, 0xed, 0xc3, 0x9d, 0xb8, 0x86 } };
static const GUID IID_INavigationTransitionInfo_UWP =
    { 0xa9b05091, 0xae4a, 0x4372, { 0x86, 0x25, 0x21, 0xb7, 0xa8, 0xb9, 0x8c, 0xa4 } };
static const GUID IID_IEntranceNavigationTransitionInfo_UWP =
    { 0x720a256b, 0x1c8a, 0x41ee, { 0x82, 0xec, 0x8a, 0x87, 0xc0, 0xcf, 0x47, 0xda } };
static const GUID IID_IFlyoutBase5_UWP =
    { 0xad3ec0c7, 0x12bb, 0x5a73, { 0xb7, 0x8e, 0x10, 0x51, 0x92, 0xca, 0x73, 0xd6 } };
static const GUID IID_IImage2_UWP =
    { 0x495b7402, 0x9af3, 0x4e50, { 0xaa, 0x90, 0x03, 0x38, 0x8f, 0x30, 0x86, 0xd2 } };
static const GUID IID_IEasingFunctionBase_UWP =
    { 0xc108383f, 0x2c02, 0x4151, { 0x8e, 0xcd, 0x68, 0xdd, 0xaa, 0x3f, 0x0d, 0x9b } };

const GUID IID_IDataTemplateSelectorFactory_UWP =
    { 0x511baad0, 0x210a, 0x457d, { 0x95, 0x21, 0x43, 0x3d, 0x2b, 0x11, 0xb7, 0xd2 } };
const GUID IID_IStyleSelectorFactory_UWP =
    { 0x6c3fb5fc, 0x8e84, 0x4c91, { 0xac, 0x15, 0x04, 0xa1, 0xd8, 0x85, 0x50, 0x0f } };
const GUID IID_IStyleSelectorFactory_UWP2 =
    { 0x9e942ae7, 0x3761, 0x447f, { 0x8f, 0x97, 0x29, 0xe3, 0x9d, 0x5e, 0xb3, 0x10 } };
const GUID IID_IStateTriggerBaseFactory2_UWP =
    { 0x970e2c4b, 0xbfaf, 0x47b0, { 0xbe, 0x42, 0xc1, 0xd7, 0x11, 0xbb, 0x2e, 0x9f } };
const GUID IID_IBindingFactory_UWP =
    { 0xff42bb08, 0xc39e, 0x4f7e, { 0x84, 0x34, 0xa1, 0x56, 0x90, 0x83, 0x88, 0x3c } };
const GUID IID_IRelativeSourceFactory_UWP =
    { 0xef8392cd, 0x446e, 0x4f93, { 0xaa, 0xcb, 0x9b, 0x12, 0x55, 0x57, 0x74, 0x60 } };
const GUID IID_IRadioButtonFactory_UWP =
    { 0xf1d04933, 0x34e1, 0x4a5c, { 0xb2, 0xae, 0xca, 0x3b, 0x1c, 0x0b, 0x20, 0xde } };
const GUID IID_IPropertyPathFactory_UWP =
    { 0x4e4cdf99, 0x9826, 0x4e56, { 0x84, 0x7c, 0xca, 0x05, 0x5f, 0x16, 0x29, 0x05 } };
const GUID IID_IDependencyObjectCollectionFactory_UWP =
    { 0x051e79ff, 0xb3a8, 0x49ee, { 0xb5, 0xaf, 0xac, 0x8f, 0x68, 0xb6, 0x49, 0xe4 } };
const GUID IID_IMenuFlyoutSeparatorFactory_UWP =
    { 0xc159dcc8, 0xb437, 0x44e0, { 0xb2, 0x4b, 0x57, 0x20, 0xae, 0xa1, 0xdb, 0xac } };
static const GUID IID_IKeyboardAccelerator_UWP =
    { 0x92e6181e, 0x19ae, 0x465a, { 0x9b, 0x3c, 0xa7, 0x1e, 0xe9, 0xea, 0x74, 0x20 } };
const GUID IID_IKeyboardAcceleratorInvokedEventArgs_UWP =
    { 0xd300a9f6, 0x2f7e, 0x4873, { 0xa5, 0x55, 0x16, 0x6e, 0x59, 0x6e, 0x1c, 0x5a } };
const GUID IID_IKeyboardAcceleratorFactory_UWP =
    { 0x44d88a99, 0x4bfd, 0x4a47, { 0xa8, 0x93, 0x51, 0x5f, 0x38, 0x86, 0x23, 0xf6 } };
static const GUID IID_IXamlUICommand_UWP =
    { 0x8494f8d4, 0xead1, 0x5f01, { 0xad, 0x2e, 0xa8, 0xca, 0xd4, 0xf9, 0xdc, 0x0e } };
const GUID IID_IExecuteRequestedEventArgs_UWP =
    { 0xe07fa734, 0xa0b6, 0x5755, { 0x9e, 0x87, 0x24, 0xf5, 0x4c, 0xca, 0x93, 0x72 } };
static const GUID IID_ITeachingTip_WinUI =
    { 0x5b200440, 0x4fcc, 0x5fdb, { 0xb4, 0x18, 0xb5, 0x79, 0x7b, 0x08, 0x74, 0xad } };
const GUID IID_IKeyRoutedEventArgs_UWP =
    { 0xd4cd3dfe, 0x4079, 0x42e9, { 0xa3, 0x9a, 0x30, 0x95, 0xd3, 0xf0, 0x49, 0xc6 } };
const GUID IID_IDispatcherTimerFactory_UWP =
    { 0xe9961e6e, 0x3626, 0x403a, { 0xaf, 0xe0, 0x04, 0x0d, 0x58, 0x16, 0x56, 0x32 } };

/* ============================================================================
 * 1. PRI v2 (mrm_pri2) Container Reader
 * ============================================================================ */

const BYTE *g_pri_data = NULL;
DWORD g_pri_size = 0;
INIT_ONCE g_pri_once = INIT_ONCE_STATIC_INIT;

const BYTE *g_winui_pri_data = NULL;
DWORD g_winui_pri_size = 0;
BOOL g_winui_pri_probed = FALSE;

BOOL WINAPI init_pri_once( INIT_ONCE *once, void *param, void **ctx )
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

BOOL WINAPI init_pri_xbf_cat_once( INIT_ONCE *once, void *param, void **ctx )
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

void ensure_winui_pri_cat( void )
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

BOOL WINAPI init_xaml_meta_cache( INIT_ONCE *once, void *param, void **ctx )
{
    size_t i;
    AcquireSRWLockExclusive( &xaml_meta_cache_lock );
    for (i = 0; i < ARRAY_SIZE(xbf_known_types); i++)
    {
        if (xbf_known_types[i])
            xaml_meta_insert_locked( xbf_known_types[i], (WORD)(0x8000 | i) );
    }
    ReleaseSRWLockExclusive( &xaml_meta_cache_lock );
    return TRUE;
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

BOOL uwp_is_display_result_element( const struct uwp_element *el )
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
    if (out_type_name && max_chars && !out_type_name[0] && xaml_get_app_caps()->vm_type[0])
        lstrcpynW( out_type_name, xaml_get_app_caps()->vm_type, (int)max_chars );
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

    if (g_calc_vm.cmd)
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

struct uwp_element *uwp_elem_from_any( void *ptr )
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
    if (ptr != uwp_get_app_outer())
    {
        static const GUID IID_IResourceDictionary_UWP_g =
            { 0x44a9796f, 0x723e, 0x4fdf, { 0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84 } };
        static const GUID IID_IResourceDictionary_WinUI_g =
            { 0x1b690975, 0xa710, 0x5783, { 0xa6, 0xe1, 0x15, 0x83, 0x6f, 0x61, 0x86, 0xc2 } };
        void *pv_iface = NULL;
        void *ui_iface = NULL;
        void *cand = NULL;
        HSTRING hcls = NULL;
        HRESULT cls_hr;
        /* Boxed primitive PropertyValues and ResourceDictionaries are never UIElements; do not probe vtbl[6]/[7] on them */
        if ((SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IPropertyValue_g, &pv_iface )) && pv_iface) ||
            (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IResourceDictionary_UWP_g, &pv_iface )) && pv_iface) ||
            (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IResourceDictionary_WinUI_g, &pv_iface )) && pv_iface))
        {
            IUnknown_Release( (IUnknown *)pv_iface );
            return NULL;
        }
        /* If ptr answers IUIElement, resolve directly via IID_UWP_ELEM_SELF or table without probing vtbl[6] */
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IUIElement_UWP, &ui_iface )) && ui_iface)
        {
            if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ui_iface, &IID_UWP_ELEM_SELF, &self_iface )) && self_iface)
            {
                AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
                for (el = g_all_uwp_elems_head; el; el = el->next_reg)
                {
                    if (self_iface == (void *)&el->nondelegating_iface ||
                        ((char *)self_iface >= (char *)el && (char *)self_iface < (char *)(el + 1)))
                    {
                        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                        IUnknown_Release( (IUnknown *)self_iface );
                        IUnknown_Release( (IUnknown *)ui_iface );
                        return el;
                    }
                }
                ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                IUnknown_Release( (IUnknown *)self_iface );
            }
            IUnknown_Release( (IUnknown *)ui_iface );
        }
        /* Only probe MCG __ComObject get_Inner (vtbl[6]/[7]) if GetRuntimeClassName is not implemented */
        cls_hr = E_FAIL;
        __TRY
        {
            cls_hr = IInspectable_GetRuntimeClassName( (IInspectable *)ptr, &hcls );
        }
        __EXCEPT_ALL
        {
            cls_hr = E_FAIL;
        }
        __ENDTRY;
        if (SUCCEEDED(cls_hr))
        {
            if (hcls) WindowsDeleteString( hcls );
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

HWND g_island_hwnds[16];
int g_island_hwnd_cnt = 0;

void uwp_invalidate_window( void )
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

HRESULT WINAPI uwp_sub_QueryInterface( IInspectable *iface, REFIID iid, void **out )
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
    {
        HRESULT hr = E_NOINTERFACE;
        BOOL faulted = FALSE;
        __TRY
        {
            hr = IInspectable_QueryInterface( impl->outer, iid, out );
        }
        __EXCEPT_ALL
        {
            faulted = TRUE;
        }
        __ENDTRY;
        if (!faulted) return hr;
        ERR( "uwp_sub_QI: outer=%p FAULTED on %s (%s), falling back to inner\n",
             impl->outer, debugstr_w(impl->class_name), debugstr_w(impl->name) );
        impl->outer = NULL;
        impl->delegating = FALSE;
    }
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}

ULONG WINAPI uwp_sub_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->delegating && impl->outer)
        return IInspectable_AddRef( impl->outer );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}

ULONG WINAPI uwp_sub_Release( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->delegating && impl->outer)
        return IInspectable_Release( impl->outer );
    return uwp_nondelegating_Release( &impl->nondelegating_iface );
}

HRESULT WINAPI uwp_sub_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_nondelegating_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, nondelegating_iface );
    const WCHAR *cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}

HRESULT WINAPI uwp_sub_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *cls;
    if (impl && impl->outer && iface != &impl->nondelegating_iface)
        return IInspectable_GetRuntimeClassName( impl->outer, name );
    cls = (impl && impl->class_name[0]) ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    return WindowsCreateString( cls, wcslen(cls), name );
}

HRESULT WINAPI uwp_sub_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

HRESULT WINAPI uwp_generic_noop( void *iface, ... )
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
    if (IsEqualGUID( iid, &IID_IDispatch ))
    {
        *out = &impl->IDispatch_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAgileObject ))
    {
        if (impl->delegating && impl->outer)
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
    else if (IsEqualGUID( iid, &IID_IFrameworkElement2_UWP ))
        *out = &impl->IFrameworkElement2_iface;
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
    else if (IsEqualGUID( iid, &IID_IDataTemplate_UWP ))
        *out = &impl->IDataTemplate_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplateSelector_UWP ))
        *out = &impl->IDataTemplateSelector_iface;
    else if (IsEqualGUID( iid, &IID_IDataTemplateSelector2_UWP ))
        *out = &impl->IDataTemplateSelector2_iface;
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
    else if (IsEqualGUID( iid, &IID_IUIElement5_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ))
        *out = &impl->IUIElement5_iface;
    else if (IsEqualGUID( iid, &IID_IButtonWithFlyout_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IButtonWithFlyout_UWP ))
        *out = &impl->IButtonWithFlyout_iface;
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
    else if (IsEqualGUID( iid, &IID_IAutoSuggestBox_Real_UWP ) ||
             IsEqualGUID( iid, &IID_IAutoSuggestBox2_Real_UWP ))
    {
        if (wcsstr( impl->class_name, L"AutoSuggestBox" ))
            *out = &impl->IAutoSuggestBox2_iface;
        else
            *out = &impl->ITextBlock_iface;
    }
    else if (IsEqualGUID( iid, &IID_IContentDialog_UWP ) ||
             IsEqualGUID( iid, &IID_IContentDialog_UWP_Alt ))
    {
        if (wcsstr( impl->class_name, L"AutoSuggestBox" ))
            *out = &impl->IAutoSuggestBox_iface;
        else
            *out = &impl->IContentDialog_iface;
    }
    else if (IsEqualGUID( iid, &IID_IContentDialog2_UWP ))
        *out = &impl->IContentDialog2_iface;
    else if (IsEqualGUID( iid, &IID_ISwapChainPanel_UWP ))
        *out = &impl->ISwapChainPanel_iface;
    else if (IsEqualGUID( iid, &IID_ISwapChainPanelNative_UWP ) ||
             IsEqualGUID( iid, &IID_ISwapChainPanelNative2_UWP ))
        *out = &impl->ISwapChainPanelNative2_iface;
    else if (IsEqualGUID( iid, &IID_ITeachingTip_WinUI ))
        *out = &impl->ITeachingTip_iface;
    else if (IsEqualGUID( iid, &IID_IGrid_UWP ) ||
             IsEqualGUID( iid, &IID_IGrid_UWP2 ))
        *out = &impl->IGrid_iface;
    else if (IsEqualGUID( iid, &IID_IGrid3_Calc ) ||
             IsEqualGUID( iid, &IID_IGrid3_UWP ))
        *out = &impl->IGrid3_iface;
    else if (IsEqualGUID( iid, &IID_IBorder_UWP ) ||
             IsEqualGUID( iid, &IID_IBorder2_Calc ))
        *out = &impl->IBorder_iface;
    else if (IsEqualGUID( iid, &IID_ITextBlock_UWP ))
        *out = &impl->ITextBlock_iface;
    else if ((iid->Data1 == 0xe5fff9e2 && iid->Data2 == 0xb968 && iid->Data3 == 0x49e7) || /* IRichTextBlock */
             (iid->Data1 == 0xf83ef59a && iid->Data2 == 0xfa61 && iid->Data3 == 0x4bef) || /* IParagraph */
             (iid->Data1 == 0x54f03373 && iid->Data2 == 0xc273 && iid->Data3 == 0x4f3b))   /* ISpan */
        *out = &impl->IParagraph_iface;
    else if (iid->Data1 == 0x59553c83 && iid->Data2 == 0x0e14 && iid->Data3 == 0x49bd)     /* IRun */
        *out = &impl->IRun_iface;
    else if (IsEqualGUID( iid, &IID_IRowDefinition_UWP ))
        *out = &impl->IRowDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IColumnDefinition_UWP ))
        *out = &impl->IColumnDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IScrollViewer_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer2_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer3_UWP ))
        *out = &impl->IScrollViewer_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement7_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement7_Proj ))
        *out = &impl->IFrameworkElement7_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement2_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement3_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement_Calc2 ) ||
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
    else
    {
        static const GUID IID_IXamlCompositionBrushBaseProtected_UWP =
            { 0x1513f3d8, 0x0457, 0x4e1c, { 0xad, 0x77, 0x11, 0xc1, 0xd9, 0x87, 0x97, 0x43 } };
        static const GUID IID_IXamlCompositionBrushBase_UWP =
            { 0x410b99e7, 0x9d63, 0x4449, { 0xa3, 0x58, 0x49, 0x62, 0xfd, 0x93, 0x5f, 0x39 } };
        if (IsEqualGUID( iid, &IID_IXamlCompositionBrushBaseProtected_UWP ) ||
            IsEqualGUID( iid, &IID_IXamlCompositionBrushBase_UWP ))
            *out = &impl->IStyle_iface;
    }
    if (!*out && (IsEqualGUID( iid, &IID_IRoutedEventArgs_QI_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs_Clone_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs2_UWP ) ||
              IsEqualGUID( iid, &IID_IRoutedEventArgs_WinUI3 )))
        *out = &impl->IInspectable_iface;
    else if (IsEqualGUID( iid, &IID_ISplitView_UWP ))
        *out = &impl->ISplitView_iface;
    else if (IsEqualGUID( iid, &IID_ISplitView2_UWP ))
        *out = &impl->ISplitView2_iface;
    else if (IsEqualGUID( iid, &IID_IItemsRepeater_UWP ) ||
             IsEqualGUID( iid, &IID_IItemsRepeaterSizingTarget_UWP ))
        *out = &impl->IPanel_iface;
    else if (IsEqualGUID( iid, &IID_IContentPresenter_UWP ))
        *out = &impl->IContentControl_iface;
    else if ((IsEqualGUID( iid, &IID_IFrameProj_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" ))))
        *out = &impl->IFrame2_iface; /* IFrame2: Navigate(TypeName, param, transitionInfo) */
    else if (IsEqualGUID( iid, &IID_IFrame3_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" )))
        *out = &impl->IFrame3_iface;
    else if (IsEqualGUID( iid, &IID_IFrame5_UWP ) &&
             (wcsstr( impl->class_name, L".Controls.Frame" ) ||
              wcsstr( impl->class_name, L".Controls.ContentFrame" )))
        *out = &impl->IFrame5_iface;
    else if (IsEqualGUID( iid, &IID_IMediaPlayerElement_UWP ) &&
             wcsstr( impl->class_name, L"MediaPlayerElement" ))
        *out = &impl->IMediaPlayerElement_iface;
    else if ((IsEqualGUID( iid, &IID_INavigationTransitionInfo_UWP ) ||
              IsEqualGUID( iid, &IID_IEntranceNavigationTransitionInfo_UWP )) &&
             wcsstr( impl->class_name, L"NavigationTransitionInfo" ))
        *out = &impl->IStyle_iface; /* INavigationTransitionInfo / IEntranceNavigationTransitionInfo */
    else if (IsEqualGUID( iid, &IID_IFlyoutBase5_UWP ) &&
             wcsstr( impl->class_name, L"Flyout" ))
        *out = &impl->IStyle_iface; /* IFlyoutBase5 */
    else if (IsEqualGUID( iid, &IID_IImage2_UWP ) &&
             wcsstr( impl->class_name, L"Image" ))
        *out = &impl->IImage_iface; /* Windows.UI.Xaml.Controls.IImage */
    else if (IsEqualGUID( iid, &IID_IEasingFunctionBase_UWP ) &&
             (wcsstr( impl->class_name, L"Ease" ) || wcsstr( impl->class_name, L"EasingFunction" )))
        *out = &impl->IEasingFunctionBase_iface;
    else if (IsEqualGUID( iid, &IID_INavigationView_UWP ) &&
             !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_INavigationViewItem_UWP ) &&
             !wcscmp( impl->class_name, L"Microsoft.UI.Xaml.Controls.NavigationViewItem" ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IFlyoutBase_UWP ) ||
             IsEqualGUID( iid, &IID_IFlyoutBase3_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP2 ) ||
             IsEqualGUID( iid, &IID_IMenuBarItem_WinUI ) ||
             IsEqualGUID( iid, &IID_ITabView_WinUI ))
        *out = &impl->IFlyout_iface;
    if (!*out)
    {
        static const GUID IID_IListViewBase_UWP =
            { 0xf6ce8c6d, 0xfe96, 0x41ad, { 0xa6, 0x4a, 0xc2, 0xb8, 0x1c, 0x4a, 0xf7, 0xf8 } };
        static const GUID IID_ISelector_UWP =
            { 0xe30eb3a5, 0xb36b, 0x42dc, { 0x85, 0x27, 0xcd, 0x25, 0x13, 0x6c, 0x08, 0x3c } };
        static const GUID IID_IItemsControl_UWP =
            { 0x3d0813ba, 0x6890, 0x4537, { 0xbf, 0xe5, 0x79, 0x6d, 0x94, 0x58, 0xed, 0xd6 } };
        static const GUID IID_IItemsControl_UWP2 =
            { 0xf4a91dd8, 0xd979, 0x4381, { 0x86, 0x52, 0xbd, 0xa0, 0x34, 0x2a, 0x76, 0x5e } };
        static const GUID IID_IListViewBase2_UWP =
            { 0xd1c2a4f7, 0x78a3, 0x4491, { 0x86, 0xe0, 0x2d, 0xde, 0xbc, 0x00, 0x7a, 0xc5 } };
        static const GUID IID_IListViewBase4_UWP =
            { 0x18b7792b, 0x11dc, 0x4eb6, { 0xa7, 0xbc, 0x74, 0x1a, 0xdb, 0x3e, 0xeb, 0x79 } };
        static const GUID IID_IListView_UWP =
            { 0x8eed9bc2, 0xa58c, 0x4453, { 0xaf, 0x0f, 0xa9, 0x2e, 0xe0, 0x6d, 0x03, 0x17 } };
        static const GUID IID_IShape_UWP =
            { 0x786f2b75, 0x9aa0, 0x454d, { 0xae, 0x06, 0xa2, 0x46, 0x6e, 0x37, 0xc8, 0x32 } };
        static const GUID IID_IScrollViewer3_Real_UWP =
            { 0x74e9be21, 0x5de2, 0x595e, { 0xbc, 0x08, 0xdb, 0xd3, 0xc5, 0x77, 0x49, 0x2c } };
        static const GUID IID_IStateTrigger_UWP =
            { 0x67adef2e, 0xd8d9, 0x49f7, { 0xa1, 0xfd, 0x2e, 0x35, 0xee, 0xdd, 0x23, 0xcd } };
        static const GUID IID_IBitmapImage_UWP =
            { 0x31af3271, 0xe3b4, 0x442d, { 0xa3, 0x41, 0x4c, 0x02, 0x26, 0xb2, 0x72, 0x5b } };
        static const GUID IID_IBitmapImage2_UWP =
            { 0x1abaee23, 0x74ee, 0x4cc7, { 0x99, 0xba, 0xb1, 0x71, 0xe3, 0xcd, 0xa6, 0x1e } };
        static const GUID IID_IBitmapImage2_Real_UWP =
            { 0x1069c1b6, 0x8c9b, 0x4762, { 0xbe, 0x3d, 0x75, 0x9f, 0x56, 0x98, 0xf2, 0xb3 } };
        static const GUID IID_IBitmapImage3_UWP =
            { 0x7311e0d4, 0xfe94, 0x4e70, { 0x9b, 0x90, 0xcd, 0xd4, 0x7a, 0xc2, 0x3a, 0xfb } };
        static const GUID IID_IBitmapSource_UWP =
            { 0x23d86411, 0x202f, 0x41b2, { 0x8c, 0x5b, 0xa8, 0xa3, 0xb3, 0x33, 0x80, 0x0b } };
        if (IsEqualGUID( iid, &IID_IListViewBase_UWP ))
            *out = &impl->IFlyout_iface;
        else if (IsEqualGUID( iid, &IID_IBitmapImage_UWP ))
            *out = &impl->IBitmapImage_iface;
        else if (IsEqualGUID( iid, &IID_IBitmapSource_UWP ))
            *out = &impl->IBitmapSource_iface;
        else if (IsEqualGUID( iid, &IID_IListViewBase2_UWP ) ||
                 IsEqualGUID( iid, &IID_IListViewBase4_UWP ) ||
                 IsEqualGUID( iid, &IID_IListView_UWP ) ||
                 IsEqualGUID( iid, &IID_IShape_UWP ) ||
                 IsEqualGUID( iid, &IID_IScrollViewer3_Real_UWP ) ||
                 IsEqualGUID( iid, &IID_IStateTrigger_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage2_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage2_Real_UWP ) ||
                 IsEqualGUID( iid, &IID_IBitmapImage3_UWP ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_ISelector_UWP ))
            *out = &impl->ISelector_iface;
        else if (IsEqualGUID( iid, &IID_IItemsControl_UWP ) ||
                 IsEqualGUID( iid, &IID_IItemsControl_UWP2 ))
            *out = &impl->IItemsControl_iface;
    }
    if (!*out)
    {
        if (IsEqualGUID( iid, &IID_IDispatcherTimer_UWP ))
            *out = &impl->IInspectable_iface;
        else if (IsEqualGUID( iid, &IID_IStateTriggerBase_UWP ) ||
                 IsEqualGUID( iid, &IID_IStateTriggerBaseProtected_UWP ) ||
                 IsEqualGUID( iid, &IID_IMarkupExtension_UWP ) ||
                 IsEqualGUID( iid, &IID_IMarkupExtensionOverrides_UWP ))
            *out = &impl->IStyle_iface;
        else if (IsEqualGUID( iid, &IID_IUIElement10_UWP ) ||
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

    if (iid->Data1 != 0x11d3b13a && wcsstr( impl->class_name, L"Grid" ))
        WARN( "uwp_nondelegating_QI: %s not matched on %s\n", debugstr_guid(iid), debugstr_w(impl->class_name) );
    else
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

HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token )
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

HRESULT WINAPI uwp_ui_event_add( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok && (ULONG_PTR)tok > 0x10000 && !IsBadWritePtr( tok, sizeof(*tok) ))
        tok->value = 1;
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

static const WCHAR *uwp_elem_name_w( struct uwp_element *el ) { return el ? el->name : L"?"; }

#define UWP_UI_PTR_EVENT( name, field ) \
HRESULT WINAPI uwp_ui_add_##name( void *iface, void *handler, EventRegistrationToken *tok ) \
{ \
    struct uwp_element *impl = uwp_elem_from_any( iface ); \
    if (!impl) { if (tok) tok->value = 0; return E_INVALIDARG; } \
    if (wcsstr( impl->class_name, L"Card" )) \
        WARN( "add_" #name " on %s (%s) impl=%p handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( uwp_elem_name_w( impl ) ), impl, handler ); \
    TRACE( "add_" #name " on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( uwp_elem_name_w( impl ) ), handler ); \
    return xaml_event_registry_add( &impl->field, handler, tok ); \
} \
HRESULT WINAPI uwp_ui_remove_##name( void *iface, EventRegistrationToken tok ) \
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
UWP_UI_PTR_EVENT( Tapped, tapped_reg )
UWP_UI_PTR_EVENT( PointerCanceled, ptr_canceled_reg )
UWP_UI_PTR_EVENT( PointerWheelChanged, ptr_wheel_reg )
UWP_UI_PTR_EVENT( SelectionChanged, nav_sel_changed_reg )
UWP_UI_PTR_EVENT( ItemInvoked, nav_item_invoked_reg )

/* INavigationViewSelectionChangedEventArgs / INavigationViewItemInvokedEventArgs
 * projected face (singleton, like g_sel_changed_args).  ABI after the 6
 * IInspectable slots: 6 = get_SelectedItem / get_InvokedItem, 7 = get_IsSelected
 * / get_IsSettingsInvoked, 8 = get_IsSettingsSelected, 9 = get_SelectedItemOverride.
 * The item slots serve the value in g_nav_args_item set by uwp_nav_fire(). */
IInspectable *g_nav_args_item;
HRESULT WINAPI uwp_nav_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
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
void uwp_realize_deferred_entry( struct uwp_defer_entry *de );
void uwp_fire_loaded_tree( struct uwp_element *el );

HRESULT WINAPI uwp_fe_get_Resources( void *iface, void **out )
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

HRESULT WINAPI uwp_fe_put_Resources( void *iface, void *val )
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

HRESULT WINAPI uwp_fe_get_Tag( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if ((ULONG_PTR)out < 0x10000 || IsBadWritePtr( out, sizeof(void *) )) return S_OK;
    *out = impl ? impl->tag : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_fe_put_Tag( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->tag) IInspectable_Release( impl->tag );
    impl->tag = (IInspectable *)val;
    if (impl->tag) IInspectable_AddRef( impl->tag );
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Style( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->style_obj : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static void uwp_apply_runtime_style_setters( struct uwp_element *target_el, const struct uwp_style_setter *setters, int cnt )
{
    void *app_outer = uwp_get_app_outer();
    IInspectable *target = target_el->outer ? target_el->outer : &target_el->IInspectable_iface;
    int i;
    for (i = 0; i < cnt; i++)
    {
        const struct uwp_style_setter *st = &setters[i];
        WARN( "  setter[%d] pid=%#x prop=%s owner=%s res=%d vt=%d str=%s\n",
              i, st->pid, debugstr_w(st->prop_name), debugstr_w(st->owner_cls), st->is_res_ref, st->vt, debugstr_w(st->str) );
        if (!st->prop_name[0] || !st->owner_cls[0]) continue;
        if (!wcsncmp( st->owner_cls, L"Windows.UI.Xaml.", 16 )) continue;
        if (!app_outer) continue;
        if (st->is_res_ref && st->str[0])
        {
            struct uwp_rd_entry *re = NULL;
            struct uwp_rd_bucket *found_b = NULL;
            if (uwp_rd_find_entry( NULL, target_el, st->str, 0, &re, &found_b ) && re)
            {
                IInspectable *res_val = uwp_rd_materialize_entry( found_b, re );
                WARN( "apply_style_setter %s.%s <- res %s (val=%p)\n",
                      debugstr_w(st->owner_cls), debugstr_w(st->prop_name), debugstr_w(st->str), res_val );
                if (res_val)
                    uwp_set_custom_xaml_member_obj( app_outer, st->owner_cls, st->prop_name, target, res_val );
            }
        }
        else if (st->vt == 0x01 || st->vt == 0x02)
        {
            uwp_set_custom_xaml_member( app_outer, st->owner_cls, st->prop_name, target,
                                        (st->vt == 0x02) ? L"True" : L"False" );
        }
        else if (st->str[0])
        {
            uwp_set_custom_xaml_member( app_outer, st->owner_cls, st->prop_name, target, st->str );
        }
    }
}

static void uwp_apply_runtime_style_by_name( struct uwp_element *target_el, const WCHAR *key, int depth )
{
    struct uwp_rd_entry *se = NULL;
    struct uwp_rd_bucket *sb = NULL;
    if (!target_el || !key || !key[0] || depth > 8) return;
    if (uwp_rd_find_entry( NULL, target_el, key, 0, &se, &sb ) && se && se->kind == UWP_RD_STYLE)
    {
        if (se->str_val[0])
            uwp_apply_runtime_style_by_name( target_el, se->str_val, depth + 1 );
        if (se->setter_cnt > 0 && se->setters)
            uwp_apply_runtime_style_setters( target_el, se->setters, se->setter_cnt );
    }
}

HRESULT WINAPI uwp_fe_put_Style( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *st_el;
    if (!impl) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (impl->style_obj) IInspectable_Release( impl->style_obj );
    impl->style_obj = val;
    st_el = uwp_elem_from_any( val );
    if (wcsstr( impl->class_name, L"ResponsiveItemsRepeaterList" ))
        WARN( "uwp_fe_put_Style on %s (%s) val=%p st_el=%p based_on=%s cnt=%d\n",
              debugstr_w(impl->class_name), debugstr_w(impl->name), val, st_el,
              st_el ? debugstr_w(st_el->style_based_on) : "\"-\"",
              st_el ? st_el->style_setter_cnt : -1 );
    if (st_el)
    {
        if (st_el->style_based_on[0])
            uwp_apply_runtime_style_by_name( impl, st_el->style_based_on, 0 );
        if (st_el->style_setter_cnt > 0 && st_el->style_setters)
            uwp_apply_runtime_style_setters( impl, st_el->style_setters, st_el->style_setter_cnt );
    }
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_ActualWidth( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->actual_w > 0) ? impl->actual_w : 320.0;
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_ActualHeight( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->actual_h > 0) ? impl->actual_h : 500.0;
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Width( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->width : 0.0;
    return S_OK;
}
HRESULT WINAPI uwp_fe_put_Width( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->width = val; g_layout_dirty = TRUE; }
    return S_OK;
}
HRESULT WINAPI uwp_fe_get_Height( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->height : 0.0;
    return S_OK;
}
HRESULT WINAPI uwp_fe_put_Height( void *iface, double val )
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


HRESULT WINAPI uwp_fe_get_Margin( void *iface, struct uwp_thickness *t )
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

HRESULT WINAPI uwp_fe_put_Margin( void *iface, struct uwp_thickness *t )
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

HRESULT WINAPI uwp_fe_get_Name( void *iface, HSTRING *name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = impl ? impl->name : L"";
    return WindowsCreateString( s, wcslen(s), name );
}

HRESULT WINAPI uwp_fe_put_Name( void *iface, HSTRING name )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = WindowsGetStringRawBuffer( name, NULL );
    if (impl && s) lstrcpynW( impl->name, s, ARRAY_SIZE(impl->name) );
    return S_OK;
}

HRESULT WINAPI uwp_fe_get_Parent( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    WARN( "uwp_fe_get_Parent: iface=%p impl=%p (%s) parent=%p (%s)\n", iface, impl,
          impl ? debugstr_w(impl->class_name) : "?", impl ? impl->parent : NULL,
          (impl && impl->parent) ? debugstr_w(impl->parent->class_name) : "?" );
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

void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc );

HRESULT WINAPI uwp_fe_get_DataContext( void *iface, void **out )
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

HRESULT WINAPI uwp_fe_put_DataContext( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    TRACE( "uwp_fe_put_DataContext on %s (%s) -> %p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), val );
    uwp_propagate_data_context( impl, (IInspectable *)val );
    return S_OK;
}

HRESULT WINAPI uwp_fe_FindName( void *iface, HSTRING name, void **out )
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
    if (root->defer_entries)
    {
        for (i = 0; i < root->defer_cnt; i++)
        {
            if (!wcscmp( root->defer_entries[i].name, want ) && !root->defer_entries[i].realized)
                uwp_realize_deferred_entry( &root->defer_entries[i] );
        }
    }
    if (impl != root && impl->defer_entries)
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

HRESULT WINAPI uwp_fe_add_Loaded( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    TRACE( "add_Loaded on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    if (xaml_vec_grow( (void **)&impl->loaded_handlers, &impl->loaded_cap, impl->loaded_cnt + 1, sizeof(*impl->loaded_handlers), 8 ))
    {
        impl->loaded_handlers[impl->loaded_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_add_SizeChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    TRACE( "add_SizeChanged on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    if (xaml_vec_grow( (void **)&impl->size_changed_handlers, &impl->size_changed_cap, impl->size_changed_cnt + 1, sizeof(*impl->size_changed_handlers), 8 ))
    {
        impl->size_changed_handlers[impl->size_changed_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_add_LayoutUpdated( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    TRACE( "add_LayoutUpdated on %s (%s) handler=%p\n", debugstr_w( impl->class_name ), debugstr_w( impl->name ), handler );
    if (xaml_vec_grow( (void **)&impl->layout_updated_handlers, &impl->layout_updated_cap, impl->layout_updated_cnt + 1, sizeof(*impl->layout_updated_handlers), 8 ))
    {
        impl->layout_updated_handlers[impl->layout_updated_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
        if (tok) tok->value = impl->layout_updated_cnt;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_remove_LayoutUpdated( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    TRACE( "remove_LayoutUpdated on %s tok=%I64d\n", impl ? debugstr_w( impl->class_name ) : "?", tok.value );
    if (impl && tok.value >= 1 && tok.value <= impl->layout_updated_cnt)
    {
        int idx = (int)tok.value - 1;
        if (impl->layout_updated_handlers[idx])
        {
            IUnknown_Release( (IUnknown *)impl->layout_updated_handlers[idx] );
            impl->layout_updated_handlers[idx] = NULL;
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_remove_Unloaded( void *iface, EventRegistrationToken tok )
{
    return S_OK;
}

HRESULT WINAPI uwp_fe3_add_DataContextChanged( void *iface, void *handler, EventRegistrationToken *tok )
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

HRESULT WINAPI uwp_fe3_add_Loading( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (xaml_vec_grow( (void **)&impl->loading_handlers, &impl->loading_cap, impl->loading_cnt + 1, sizeof(*impl->loading_handlers), 8 ))
    {
        impl->loading_handlers[impl->loading_cnt++] = (IInspectable *)handler;
        IUnknown_AddRef( (IUnknown *)handler );
    }
    return S_OK;
}

/* --- IControl & IControlProtected --- */
HRESULT WINAPI uwp_ctrl_get_FontSize( void *iface, double *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->font_size > 0) ? impl->font_size : 14.0;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_put_FontSize( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && val > 0) impl->font_size = val;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_get_IsEnabled( void *iface, boolean *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = impl ? impl->is_enabled : TRUE;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_put_IsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && impl->is_enabled != val)
    {
        impl->is_enabled = val;
        uwp_invalidate_window();
    }
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_ApplyTemplate( void *iface, boolean *ret )
{
    if (ret) *ret = TRUE;
    return S_OK;
}
HRESULT WINAPI uwp_ctrl_Focus( void *iface, int state, boolean *ret )
{
    if (ret) *ret = TRUE;
    return S_OK;
}

/* Named template parts synthesised for WinUI templated controls that ship a
 * rich ControlTemplate we do not fully model.  Keyed by part name so the
 * mapping is generic (no per-app shims).  Collapsed parts keep their slots
 * without stealing layout space from the visible pane/content. */
static const WCHAR *uwp_tpl_part_class( const WCHAR *nm )
{
    static const struct { const WCHAR *name, *cls; } map[] =
    {
        { L"RootSplitView", L"Microsoft.UI.Xaml.Controls.SplitView" },
        { L"MenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopNavMenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopNavMenuItemsOverflowHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"FooterMenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopFooterMenuItemsHost", L"Microsoft.UI.Xaml.Controls.ItemsRepeater" },
        { L"TopNavOverflowButton", L"Microsoft.UI.Xaml.Controls.Button" },
        { L"TogglePaneButton", L"Microsoft.UI.Xaml.Controls.Button" },
        { L"PaneAutoSuggestButton", L"Microsoft.UI.Xaml.Controls.Button" },
        { L"NavigationViewBackButton", L"Windows.UI.Xaml.Controls.Button" },
        { L"NavigationViewCloseButton", L"Windows.UI.Xaml.Controls.Button" },
        { L"Popup", L"Windows.UI.Xaml.Controls.Primitives.Popup" },
        { L"PopupContent", L"Windows.UI.Xaml.Controls.Grid" },
        { L"ExpandCollapseButton", L"Windows.UI.Xaml.Controls.Primitives.ToggleButton" },
        { L"PaneAutoSuggestBoxPresenter", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"TopPaneAutoSuggestBoxPresenter", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneHeaderOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneTitleOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneCustomContentOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneFooterOnTopPane", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneTitlePresenter", L"Microsoft.UI.Xaml.Controls.ContentPresenter" },
        { L"PaneTitleTextBlock", L"Windows.UI.Xaml.Controls.TextBlock" },
        { L"PaneContentGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"ContentGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"TopNavGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"TopNavContentOverlayAreaGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"ItemsContainerGrid", L"Windows.UI.Xaml.Controls.Grid" },
        { L"MenuItemsScrollViewer", L"Windows.UI.Xaml.Controls.ScrollViewer" },
        { L"FooterItemsScrollViewer", L"Windows.UI.Xaml.Controls.ScrollViewer" },
        { L"Shape", L"Windows.UI.Xaml.Controls.Border" },
    };
    size_t i;
    for (i = 0; i < ARRAY_SIZE(map); i++)
        if (!wcscmp( nm, map[i].name )) return map[i].cls;
    return NULL;
}

static BOOL uwp_tpl_part_collapsed( const WCHAR *nm )
{
    static const WCHAR *const cols[] =
    {
        L"TopNavMenuItemsHost", L"TopNavMenuItemsOverflowHost", L"TopFooterMenuItemsHost",
        L"TopNavOverflowButton", L"TopNavGrid", L"TopNavContentOverlayAreaGrid",
        L"PaneAutoSuggestBoxPresenter", L"TopPaneAutoSuggestBoxPresenter",
        L"PaneAutoSuggestButton", L"NavigationViewBackButton", L"NavigationViewCloseButton",
        L"PaneHeaderOnTopPane", L"PaneTitleOnTopPane", L"PaneCustomContentOnTopPane",
        L"PaneFooterOnTopPane",
    };
    size_t i;
    for (i = 0; i < ARRAY_SIZE(cols); i++)
        if (!wcscmp( nm, cols[i] )) return TRUE;
    return FALSE;
}

static void uwp_element_add_named( struct uwp_element *owner, struct uwp_element *el )
{
    if (!owner || !el) return;
    if (xaml_vec_grow( (void **)&owner->named_elems, &owner->named_cap,
                      owner->named_cnt + 1, sizeof(*owner->named_elems), 8 ))
        owner->named_elems[owner->named_cnt++] = el;
}

HRESULT WINAPI uwp_ctrl_prot_GetTemplateChild( void *iface, HSTRING child_name, void **out )
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
    else if (!wcscmp( nm, L"TabListView" ))
    {
        IInspectable *raw = NULL;
        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Controls.ListView", NULL, NULL, (void **)&raw )) && raw)
        {
            child = uwp_elem_from_any( raw );
            IInspectable_Release( raw );
            if (child)
            {
                child->parent = impl;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
            }
        }
    }
    else if (uwp_tpl_part_class( nm ))
    {
        /* WinUI templated controls (NavigationView, ...) call GetTemplateChild for
         * their named template parts.  The generic builtin template is minimal, so
         * synthesise the part with the class the control casts it to and cache it on
         * impl->named_elems so repeated lookups return the same element. */
        int i;
        for (i = 0; i < impl->named_cnt; i++)
        {
            if (impl->named_elems[i] && !wcscmp( impl->named_elems[i]->name, nm ))
            {
                child = impl->named_elems[i];
                break;
            }
        }
        if (!child)
        {
            const WCHAR *part_cls = uwp_tpl_part_class( nm );
            IInspectable *raw = NULL;
            if (!wcscmp( part_cls, L"Microsoft.UI.Xaml.Controls.ItemsRepeater" ) &&
                (!wcscmp( nm, L"MenuItemsHost" ) || !wcscmp( nm, L"FooterMenuItemsHost" )))
            {
                if (SUCCEEDED(uwp_create_instance_from_provider( part_cls, (void **)&raw )) && raw)
                {
                    child = uwp_elem_from_any( raw );
                    if (child) lstrcpynW( child->class_name, part_cls, ARRAY_SIZE(child->class_name) );
                    IInspectable_Release( raw );
                }
            }
            if (!child && SUCCEEDED(uwp_element_create( part_cls, NULL, NULL, (void **)&raw )) && raw)
            {
                child = uwp_elem_from_any( raw );
                IInspectable_Release( raw );
            }
            if (child)
            {
                child->parent = impl;
                child->visibility = uwp_tpl_part_collapsed( nm ) ? 1 : 0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                if (!wcscmp( nm, L"MenuItemsHost" ) || !wcscmp( nm, L"FooterMenuItemsHost" ) ||
                    !wcscmp( nm, L"PaneContentGrid" ))
                    child->width = 68.0;
                if (wcsstr( impl->class_name, L"NavigationView" ) &&
                    (!wcscmp( nm, L"TogglePaneButton" ) || !wcscmp( nm, L"MenuItemsHost" ) ||
                     !wcscmp( nm, L"FooterMenuItemsHost" )))
                {
                    if (!wcscmp( nm, L"TogglePaneButton" ))
                    {
                        child->width = 48.0;
                        child->height = 40.0;
                        if (!child->text[0])
                            lstrcpynW( child->text, L"\x2630", ARRAY_SIZE(child->text) );
                    }
                    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
                    if (impl->children_coll)
                        uwp_vec_Append( impl->children_coll, child->outer ? child->outer : &child->IInspectable_iface );
                }
                uwp_element_add_named( impl, child );
            }
        }
    }
    else
    {
        HRESULT hr_fn = uwp_fe_FindName( iface, child_name, out );
        if (!*out)
            WARN( "GetTemplateChild(%s) on %s (%s) -> NULL\n",
                  debugstr_w(nm), debugstr_w(impl->class_name), debugstr_w(impl->name) );
        return hr_fn;
    }

    if (child)
    {
        *out = &child->IDependencyObject_iface;
        uwp_sub_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}

/* --- IContentControl / IUserControl / IBorder --- */
BOOL g_in_tap_diag = FALSE;

static HRESULT WINAPI uwp_cc_get_Content( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && impl->content)
    {
        if (iface == &impl->IContentControl_iface)
        {
            struct uwp_element *ch = uwp_elem_from_any( impl->content );
            if (ch && ch->outer)
            {
                *out = ch->outer;
                IInspectable_AddRef( (IInspectable *)*out );
                if (g_in_tap_diag) WARN( "TAP uwp_cc_get_Content(%s) -> outer %p\n", debugstr_w(impl->text), *out );
                return S_OK;
            }
        }
        if (impl->text[0] && !uwp_elem_from_any( impl->content ))
        {
            WCHAR ub[8] = {0};
            if (!uwp_unbox_string( impl->content, ub, ARRAY_SIZE(ub) ))
            {
                *out = box_string_value( impl->text );
                return S_OK;
            }
        }
        *out = impl->content;
        IInspectable_AddRef( (IInspectable *)*out );
    }
    else if (impl && impl->text[0])
    {
        *out = box_string_value( impl->text );
        if (g_in_tap_diag) WARN( "TAP uwp_cc_get_Content(%s) -> boxed %p\n", debugstr_w(impl->text), *out );
    }
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
        if (child_el->outer && !child_el->delegating && !child_el->outer_pinned)
        {
            child_el->outer_pinned = TRUE;
            IInspectable_AddRef( child_el->outer );
        }
        g_layout_dirty = TRUE;
        TRACE( "uwp_cc_put_Content element on %s (%s) -> %s (%s)\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name),
               debugstr_w(child_el->class_name), debugstr_w(child_el->name) );
        if (impl->data_context && !child_el->data_context)
            uwp_propagate_data_context( child_el, impl->data_context );
        else if (child_el->binding_connector && !child_el->data_context && !impl->data_context)
        {
            struct uwp_element *dp_anc;
            for (dp_anc = impl->parent; dp_anc; dp_anc = dp_anc->parent)
                if (dp_anc->data_context) { uwp_propagate_data_context( child_el, dp_anc->data_context ); break; }
        }
        {
            BOOL tree_loaded = FALSE;
            struct uwp_element *p;
            for (p = impl; p; p = p->parent)
                if (p->loaded_fired) { tree_loaded = TRUE; break; }
            if (tree_loaded)
                uwp_fire_loaded_tree( child_el );
        }
    }
    if (impl->island_hwnd) InvalidateRect( impl->island_hwnd, NULL, FALSE );
    uwp_invalidate_window();
    return S_OK;
}

/* --- IButtonBase & IToggleButton --- */
HRESULT WINAPI uwp_btn_get_Command( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->command : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
HRESULT WINAPI uwp_btn_put_Command( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (wcsstr( impl->class_name, L"Card" ))
        WARN( "uwp_btn_put_Command on %s impl=%p val=%p\n", debugstr_w(impl->class_name), impl, val );
    if (impl->command) IInspectable_Release( impl->command );
    impl->command = (IInspectable *)val;
    if (impl->command) IInspectable_AddRef( impl->command );
    return S_OK;
}
HRESULT WINAPI uwp_btn_get_CommandParameter( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->command_param : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
HRESULT WINAPI uwp_btn_put_CommandParameter( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (wcsstr( impl->class_name, L"Card" ))
        WARN( "uwp_btn_put_CommandParameter on %s impl=%p val=%p\n", debugstr_w(impl->class_name), impl, val );
    if (impl->command_param) IInspectable_Release( impl->command_param );
    impl->command_param = (IInspectable *)val;
    if (impl->command_param) IInspectable_AddRef( impl->command_param );
    return S_OK;
}
HRESULT WINAPI uwp_btn_add_Click( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (wcsstr( impl->class_name, L"Card" ))
        WARN( "uwp_btn_add_Click on %s impl=%p handler=%p\n", debugstr_w(impl->class_name), impl, handler );
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
HRESULT WINAPI uwp_tb_get_Inlines( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->inlines_coll)
    {
        uwp_vec_create( impl, &impl->inlines_coll );
        if (impl->inlines_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( impl->inlines_coll, struct uwp_vec_view, IInspectable_iface );
            v->runtime_class = wcsstr( impl->class_name, L"RichTextBlock" )
                ? L"Windows.UI.Xaml.Documents.BlockCollection"
                : L"Windows.UI.Xaml.Documents.InlineCollection";
        }
    }
    *out = impl->inlines_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

HRESULT WINAPI uwp_tb_get_Text( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *s = impl ? impl->text : L"";
    return WindowsCreateString( s, wcslen(s), out );
}
HRESULT WINAPI uwp_tb_put_Text( void *iface, HSTRING val )
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

HRESULT uwp_vec_create( struct uwp_element *owner, IInspectable **out );

HRESULT WINAPI uwp_panel_get_Children( void *iface, void **out )
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

HRESULT WINAPI uwp_grid_get_RowDefinitions( void *iface, void **out )
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

HRESULT WINAPI uwp_grid_get_ColumnDefinitions( void *iface, void **out )
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


HRESULT WINAPI uwp_rowcol_get_Length( void *iface, struct uwp_grid_length *gl )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (gl)
    {
        gl->Value = impl ? impl->grid_len_val : 1.0;
        gl->GridUnitType = impl ? impl->grid_len_type : 2;
    }
    return S_OK;
}

HRESULT WINAPI uwp_rowcol_put_Length( void *iface, struct uwp_grid_length *gl )
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
static const GUID IID_IIterable_PageStackEntry_UWP =
    { 0xdd2278a5, 0x4c14, 0x5967, { 0x8a, 0x41, 0x9a, 0x47, 0xc3, 0xc5, 0xc4, 0xa7 } };
static const GUID IID_IIterator_PageStackEntry_UWP =
    { 0xb89ca96c, 0xded1, 0x53bd, { 0x87, 0x44, 0xca, 0xa8, 0x4e, 0x87, 0xba, 0x87 } };
static const GUID IID_IIterable_IInspectable_UWP =
    { 0x092b849b, 0x60b1, 0x52be, { 0xa4, 0x4a, 0x6f, 0xe8, 0xe9, 0x33, 0xcb, 0xe4 } };
static const GUID IID_IIterator_IInspectable_UWP =
    { 0x44a94f2d, 0x04f8, 0x5091, { 0xb3, 0x36, 0xbe, 0x78, 0x92, 0xdd, 0x10, 0xbe } };
static const GUID IID_IIterable_UIElement_UWP =
    { 0x1d1f9d60, 0xd53b, 0x57f7, { 0xb1, 0x44, 0x8f, 0x7c, 0x48, 0x78, 0x46, 0xe8 } };
static const GUID IID_IIterator_UIElement_UWP =
    { 0x42614e61, 0xb0aa, 0x5e72, { 0x93, 0x54, 0x27, 0x71, 0xf4, 0x32, 0x90, 0x78 } };
static const GUID IID_IIterable_Block_UWP =
    { 0xf7023b9a, 0xe6d1, 0x5e2d, { 0x8f, 0x41, 0xb2, 0x8c, 0x33, 0x32, 0x3e, 0x04 } };
static const GUID IID_IIterator_Block_UWP =
    { 0x798d518e, 0xa9f8, 0x5fc7, { 0x8c, 0xcc, 0x2a, 0x49, 0x06, 0x9a, 0xba, 0x05 } };
static const GUID IID_IIterable_Inline_UWP =
    { 0x3017abfa, 0xa321, 0x5045, { 0x97, 0x44, 0x60, 0xaf, 0x42, 0xed, 0x71, 0x59 } };
static const GUID IID_IIterator_Inline_UWP =
    { 0x18bb3ba7, 0x67d6, 0x5d0d, { 0x90, 0x97, 0x24, 0xfc, 0x66, 0xbf, 0xb9, 0x01 } };
static const GUID IID_IBindableIterable_UWP =
    { 0x036d2c08, 0xdf29, 0x41af, { 0x8a, 0xa2, 0xd7, 0x74, 0xbe, 0x62, 0xba, 0x6f } };
static const GUID IID_IIterable_RowDef_UWP =
    { 0x11fdab6b, 0x6769, 0x5c78, { 0x93, 0x21, 0x1c, 0x29, 0x30, 0x25, 0x38, 0x7d } };
static const GUID IID_IIterator_RowDef_UWP =
    { 0xd3d9ead4, 0x5e46, 0x54f7, { 0xa5, 0x27, 0x60, 0xde, 0xdd, 0x9f, 0x25, 0xc8 } };
static const GUID IID_IIterable_ColDef_UWP =
    { 0xb629d3fc, 0x513c, 0x5857, { 0x9b, 0xd5, 0xf9, 0xf7, 0x8f, 0xdc, 0xb1, 0xb9 } };
static const GUID IID_IIterator_ColDef_UWP =
    { 0x4159246b, 0x41be, 0x5bc2, { 0xa0, 0x7e, 0x9f, 0x26, 0xf1, 0xb9, 0x28, 0x71 } };
static const GUID IID_IIterable_DO_UWP =
    { 0x42e26ae1, 0xd357, 0x57e8, { 0xbb, 0x48, 0xf7, 0x5c, 0x9f, 0xf6, 0x9d, 0x91 } };

static HRESULT WINAPI uwp_vec_QI( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
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
    if (IsEqualGUID( iid, &IID_IIterable_PageStackEntry_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_IInspectable_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_UIElement_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_Block_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_Inline_UWP ) ||
        IsEqualGUID( iid, &IID_IBindableIterable_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_RowDef_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_ColDef_UWP ) ||
        IsEqualGUID( iid, &IID_IIterable_DO_UWP ))
    {
        *out = &v->IIterable_iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
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
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    const WCHAR *cls = (v && v->runtime_class) ? v->runtime_class : L"Windows.UI.Xaml.Controls.UIElementCollection";
    return WindowsCreateString( cls, lstrlenW( cls ), name );
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
HRESULT WINAPI uwp_vec_GetAt( void *iface, unsigned int idx, void **item )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (!item) return E_POINTER;
    if (idx >= v->count) { *item = NULL; return 0x8000000b; }
    *item = v->items[idx];
    if (*item) IInspectable_AddRef( (IInspectable *)*item );
    return S_OK;
}
HRESULT WINAPI uwp_vec_get_Size( void *iface, unsigned int *sz )
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
            if (child_el->outer && !child_el->delegating && !child_el->outer_pinned)
            {
                child_el->outer_pinned = TRUE;
                IInspectable_AddRef( child_el->outer );
            }
            if (v->owner->data_context && !child_el->data_context)
                uwp_propagate_data_context( child_el, v->owner->data_context );
        }
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_vec_RemoveAt( void *iface, unsigned int idx )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (idx >= v->count) return S_OK;
    if (v->items[idx])
    {
        struct uwp_element *ch = uwp_elem_from_any( v->items[idx] );
        if (ch && v->owner && ch->parent == v->owner) ch->parent = NULL;
        IInspectable_Release( v->items[idx] );
    }
    memmove( &v->items[idx], &v->items[idx + 1], (v->count - idx - 1) * sizeof(void *) );
    v->count--;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
    return S_OK;
}
HRESULT WINAPI uwp_vec_Append( void *iface, void *item )
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
    {
        if (v->items[i])
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && v->owner && ch->parent == v->owner) ch->parent = NULL;
            IInspectable_Release( v->items[i] );
        }
    }
    v->count = 0;
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
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

struct uwp_vec_iter
{
    IInspectable IIterator_iface;
    LONG ref;
    struct uwp_vec_view *vec;
    unsigned int idx;
};

static HRESULT WINAPI uwp_viter_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IIterator_PageStackEntry_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_IInspectable_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_UIElement_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_Block_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_Inline_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_RowDef_UWP ) ||
        IsEqualGUID( iid, &IID_IIterator_ColDef_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_viter_AddRef( IInspectable *iface )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    return InterlockedIncrement( &it->ref );
}
static ULONG WINAPI uwp_viter_Release( IInspectable *iface )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    ULONG r = InterlockedDecrement( &it->ref );
    if (!r)
    {
        if (it->vec) IInspectable_Release( &it->vec->IInspectable_iface );
        free( it );
    }
    return r;
}
static HRESULT WINAPI uwp_viter_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IIterator";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI uwp_viter_get_Current( IInspectable *iface, IInspectable **out )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    if (!out) return E_POINTER;
    if (!it->vec || it->idx >= it->vec->count) { *out = NULL; return E_BOUNDS; }
    *out = it->vec->items[it->idx];
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_viter_get_HasCurrent( IInspectable *iface, boolean *out )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    if (!out) return E_POINTER;
    *out = (it->vec && it->idx < it->vec->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_viter_MoveNext( IInspectable *iface, boolean *out )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    if (it->vec && it->idx < it->vec->count) it->idx++;
    if (out) *out = (it->vec && it->idx < it->vec->count) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_viter_GetMany( IInspectable *iface, UINT32 cap, IInspectable **items, UINT32 *actual )
{
    struct uwp_vec_iter *it = CONTAINING_RECORD( iface, struct uwp_vec_iter, IIterator_iface );
    UINT32 n = 0;
    if (actual) *actual = 0;
    if (!it->vec || !items) return S_OK;
    while (n < cap && it->idx < it->vec->count)
    {
        items[n] = it->vec->items[it->idx++];
        if (items[n]) IInspectable_AddRef( items[n] );
        n++;
    }
    if (actual) *actual = n;
    return S_OK;
}
static const void *uwp_viter_vtbl[] =
{
    uwp_viter_QI, uwp_viter_AddRef, uwp_viter_Release,
    uwp_sub_GetIids, uwp_viter_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_viter_get_Current, uwp_viter_get_HasCurrent, uwp_viter_MoveNext, uwp_viter_GetMany,
};

static HRESULT WINAPI uwp_vec_iter_QI( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    return uwp_vec_QI( &v->IInspectable_iface, iid, out );
}
static ULONG WINAPI uwp_vec_iter_AddRef( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    return uwp_vec_AddRef( &v->IInspectable_iface );
}
static ULONG WINAPI uwp_vec_iter_Release( IInspectable *iface )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    return uwp_vec_Release( &v->IInspectable_iface );
}
static HRESULT WINAPI uwp_vec_iter_First( IInspectable *iface, IInspectable **out )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IIterable_iface );
    struct uwp_vec_iter *it;
    if (!out) return E_POINTER;
    it = calloc( 1, sizeof(*it) );
    if (!it) return E_OUTOFMEMORY;
    it->IIterator_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_viter_vtbl;
    it->ref = 1;
    it->vec = v;
    IInspectable_AddRef( &v->IInspectable_iface );
    *out = &it->IIterator_iface;
    return S_OK;
}
static const void *uwp_vec_iterable_vtbl[] =
{
    uwp_vec_iter_QI, uwp_vec_iter_AddRef, uwp_vec_iter_Release,
    uwp_sub_GetIids, uwp_vec_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_vec_iter_First,
};

HRESULT uwp_vec_create( struct uwp_element *owner, IInspectable **out )
{
    struct uwp_vec_view *v = calloc( 1, sizeof(*v) );
    if (!v) return E_OUTOFMEMORY;
    v->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_vec_vtbl;
    v->IIterable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_vec_iterable_vtbl;
    v->ref = 1;
    v->owner = owner;
    *out = &v->IInspectable_iface;
    return S_OK;
}

struct uwp_vec_changed_args
{
    IInspectable IVectorChangedEventArgs_iface;
    LONG ref;
    int change;
    unsigned int index;
};
static const GUID IID_IVectorChangedEventArgs_UWP =
    { 0x575933df, 0x34fe, 0x4480, { 0xaf, 0x15, 0x07, 0x69, 0x1f, 0x3d, 0x5d, 0x9b } };
static HRESULT WINAPI uwp_vca_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IVectorChangedEventArgs_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_vca_AddRef( IInspectable *iface )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    return InterlockedIncrement( &a->ref );
}
static ULONG WINAPI uwp_vca_Release( IInspectable *iface )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI uwp_vca_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_vca_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.VectorChangedEventArgs", 53, name );
}
static HRESULT WINAPI uwp_vca_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI uwp_vca_get_CollectionChange( void *iface, int *out )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    if (out) *out = a->change;
    return S_OK;
}
static HRESULT WINAPI uwp_vca_get_Index( void *iface, unsigned int *out )
{
    struct uwp_vec_changed_args *a = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_vec_changed_args, IVectorChangedEventArgs_iface );
    if (out) *out = a->index;
    return S_OK;
}
static const void *uwp_vca_vtbl[] =
{
    uwp_vca_QI,
    uwp_vca_AddRef,
    uwp_vca_Release,
    uwp_vca_GetIids,
    uwp_vca_GetRuntimeClassName,
    uwp_vca_GetTrustLevel,
    uwp_vca_get_CollectionChange,
    uwp_vca_get_Index,
};

static void uwp_el_fire_vector_changed( struct uwp_element *impl, int change, unsigned int idx )
{
    void *handlers[32];
    unsigned int cnt, i;
    struct uwp_vec_changed_args *args;
    IInspectable *sender;
    if (!impl) return;
    cnt = xaml_event_registry_snapshot( &impl->obsvec_changed_reg, handlers, ARRAY_SIZE(handlers) );
    if (!cnt) return;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < cnt; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        return;
    }
    args->IVectorChangedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_vca_vtbl;
    args->ref = 1;
    args->change = change;
    args->index = idx;
    sender = impl->outer ? impl->outer : &impl->IObservableVector_iface;
    TRACE( "uwp_el_fire_vector_changed on %s change=%d idx=%u handlers=%u\n",
           debugstr_w(impl->class_name), change, idx, cnt );
    for (i = 0; i < cnt; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, &args->IVectorChangedEventArgs_iface );
        }
        __EXCEPT_ALL
        {
            WARN( "uwp_el_fire_vector_changed handler[%u] threw exception\n", i );
        }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)h );
    }
    IInspectable_Release( &args->IVectorChangedEventArgs_iface );
}

static HRESULT WINAPI uwp_el_obsvec_add( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_el_obsvec_add on %s handler=%p\n", debugstr_w(impl->class_name), handler );
    return xaml_event_registry_add( &impl->obsvec_changed_reg, handler, tok );
}
static HRESULT WINAPI uwp_el_obsvec_rem( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->obsvec_changed_reg, tok );
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
    HRESULT hr = v ? uwp_vec_SetAt( &v->IInspectable_iface, idx, item ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 3 /* ItemChanged */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_InsertAt( void *iface, unsigned int idx, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    HRESULT hr = v ? uwp_vec_InsertAt( &v->IInspectable_iface, idx, item ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 1 /* ItemInserted */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_RemoveAt( void *iface, unsigned int idx )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    HRESULT hr = v ? uwp_vec_RemoveAt( &v->IInspectable_iface, idx ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 2 /* ItemRemoved */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_Append( void *iface, void *item )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int idx = v ? v->count : 0;
    HRESULT hr = v ? uwp_vec_Append( &v->IInspectable_iface, item ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 1 /* ItemInserted */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_RemoveAtEnd( void *iface )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    unsigned int idx = (v && v->count > 0) ? v->count - 1 : 0;
    HRESULT hr = v ? uwp_vec_RemoveAtEnd( &v->IInspectable_iface ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 2 /* ItemRemoved */, idx );
    return hr;
}
static HRESULT WINAPI uwp_el_vec_Clear( void *iface )
{
    struct uwp_vec_view *v = uwp_el_ensure_vec( iface );
    HRESULT hr = v ? uwp_vec_Clear( &v->IInspectable_iface ) : S_OK;
    if (SUCCEEDED(hr) && v && v->owner) uwp_el_fire_vector_changed( v->owner, 0 /* Reset */, 0 );
    return hr;
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
    uwp_nondelegating_GetRuntimeClassName,
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

IDependencyProperty *g_routed_event_ptr_pressed = NULL;

static HRESULT WINAPI uwp_ui_AddHandler( void *iface, void *routed_event, void *handler, boolean handled_too )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    (void)handled_too;
    if (!impl || !handler) return S_OK;
    WARN( "uwp_ui_AddHandler on %s (%s) ev=%p ptr_ev=%p handler=%p handled_too=%d\n",
          debugstr_w(impl->class_name), debugstr_w(impl->name), routed_event, g_routed_event_ptr_pressed, handler, handled_too );
    if (g_routed_event_ptr_pressed && routed_event == (void *)g_routed_event_ptr_pressed)
    {
        static const GUID IID_IPointerEventHandler_UWP =
            { 0xe4385929, 0xc004, 0x4bcf, { 0x89, 0x70, 0x35, 0x94, 0x86, 0xe3, 0x9f, 0x88 } };
        static const GUID IID_IReference_PointerEventHandler =
            { 0x34de1275, 0x0aa0, 0x54ef, { 0xb5, 0x5a, 0xa2, 0xb5, 0x98, 0x68, 0x5f, 0xcb } };
        void *peh = NULL;
        void *iref = NULL;
        void *del = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)handler, &IID_IPointerEventHandler_UWP, &peh )) && peh)
        {
            del = peh;
            xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
            IUnknown_Release( (IUnknown *)peh );
        }
        else if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)handler, &IID_IReference_PointerEventHandler, &iref )) && iref)
        {
            void *unwrapped = NULL;
            HRESULT (WINAPI *get_val)(void *, void **) = (void *)(*(void ***)iref)[6];
            if (get_val && SUCCEEDED(get_val( iref, &unwrapped )) && unwrapped)
            {
                del = unwrapped;
                xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
                IUnknown_Release( (IUnknown *)unwrapped );
            }
            IUnknown_Release( (IUnknown *)iref );
        }
        else
        {
            del = handler;
            xaml_event_registry_add( &impl->ptr_pressed_reg, del, NULL );
        }
        TRACE( "IUIElement::AddHandler(PointerPressed) on %s (%s) -> del %p\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), del );
    }
    return S_OK;
}

struct uwp_general_transform
{
    IInspectable IGeneralTransform_iface;
    LONG ref;
    float dx, dy;
};
static const GUID IID_IGeneralTransform_UWP =
    { 0xa06798b7, 0xa2ec, 0x415f, { 0xad, 0xe2, 0xad, 0xe9, 0x37, 0xd8, 0xb8, 0x06 } };
static HRESULT WINAPI uwp_gt_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IGeneralTransform_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_gt_AddRef( IInspectable *iface )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( iface, struct uwp_general_transform, IGeneralTransform_iface );
    return InterlockedIncrement( &gt->ref );
}
static ULONG WINAPI uwp_gt_Release( IInspectable *iface )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( iface, struct uwp_general_transform, IGeneralTransform_iface );
    ULONG r = InterlockedDecrement( &gt->ref );
    if (!r) free( gt );
    return r;
}
static HRESULT WINAPI uwp_gt_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Media.GeneralTransform", 38, name );
}
static HRESULT WINAPI uwp_gt_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static const void *uwp_gt_vtbl[];
static HRESULT WINAPI uwp_gt_get_Inverse( void *iface, void **out )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_general_transform, IGeneralTransform_iface );
    struct uwp_general_transform *inv;
    if (!out) return E_POINTER;
    inv = calloc( 1, sizeof(*inv) );
    if (!inv) return E_OUTOFMEMORY;
    inv->IGeneralTransform_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_gt_vtbl;
    inv->ref = 1;
    inv->dx = -gt->dx;
    inv->dy = -gt->dy;
    *out = &inv->IGeneralTransform_iface;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_TransformPoint( void *iface, UINT64 pt_bits, float *out_pt )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_general_transform, IGeneralTransform_iface );
    float px, py;
    if (!out_pt) return E_POINTER;
    memcpy( &px, (const char *)&pt_bits, 4 );
    memcpy( &py, (const char *)&pt_bits + 4, 4 );
    out_pt[0] = px + gt->dx;
    out_pt[1] = py + gt->dy;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_TryTransform( void *iface, UINT64 pt_bits, float *out_pt, boolean *res )
{
    uwp_gt_TransformPoint( iface, pt_bits, out_pt );
    if (res) *res = TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_gt_TransformBounds( void *iface, const float *in_rc, float *out_rc )
{
    struct uwp_general_transform *gt = CONTAINING_RECORD( (IInspectable *)iface, struct uwp_general_transform, IGeneralTransform_iface );
    if (!out_rc) return E_POINTER;
    if (in_rc)
    {
        out_rc[0] = in_rc[0] + gt->dx;
        out_rc[1] = in_rc[1] + gt->dy;
        out_rc[2] = in_rc[2];
        out_rc[3] = in_rc[3];
    }
    return S_OK;
}
static const void *uwp_gt_vtbl[] =
{
    uwp_gt_QI,
    uwp_gt_AddRef,
    uwp_gt_Release,
    uwp_gt_GetIids,
    uwp_gt_GetRuntimeClassName,
    uwp_gt_GetTrustLevel,
    uwp_gt_get_Inverse,
    uwp_gt_TransformPoint,
    uwp_gt_TryTransform,
    uwp_gt_TransformBounds,
};
static HRESULT WINAPI uwp_ui_TransformToVisual( void *iface, void *visual, void **out )
{
    struct uwp_element *src = uwp_elem_from_any( iface );
    struct uwp_element *dst = visual ? uwp_elem_from_any( visual ) : NULL;
    struct uwp_general_transform *gt;
    if (!out) return E_POINTER;
    gt = calloc( 1, sizeof(*gt) );
    if (!gt) return E_OUTOFMEMORY;
    gt->IGeneralTransform_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_gt_vtbl;
    gt->ref = 1;
    if (src && dst)
    {
        gt->dx = (float)(src->bounds.left - dst->bounds.left);
        gt->dy = (float)(src->bounds.top - dst->bounds.top);
    }
    else if (src)
    {
        gt->dx = (float)src->bounds.left;
        gt->dy = (float)src->bounds.top;
    }
    *out = &gt->IGeneralTransform_iface;
    return S_OK;
}

static HRESULT WINAPI uwp_ui_InvalidateLayout( void *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->last_measured_w = 0.0f;
        impl->last_measured_h = 0.0f;
    }
    g_layout_dirty = TRUE;
    uwp_invalidate_window();
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
    [6] = uwp_ui_get_DesiredSize,
    [7 ... 18] = uwp_generic_noop,
    [19] = uwp_ui_get_IsHitTestVisible,
    [20] = uwp_ui_put_IsHitTestVisible,
    [21] = uwp_ui_get_Visibility,
    [22] = uwp_ui_put_Visibility,
    [23] = uwp_ui_get_RenderSize,
    [24 ... 40] = uwp_generic_noop,
    [41 ... 56] = uwp_ui_event_add,
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
    [73] = uwp_ui_add_Tapped,
    [74] = uwp_ui_remove_Tapped,
    [75 ... 85] = uwp_ui_event_add,
    [86 ... 92] = uwp_generic_noop,
    [93] = uwp_ui_CapturePointer,
    [94] = uwp_ui_ReleasePointerCapture,
    [95] = uwp_ui_ReleasePointerCaptures,
    [96] = uwp_ui_AddHandler,
    [97] = uwp_generic_noop,
    [98] = uwp_ui_TransformToVisual,
    [99] = uwp_ui_InvalidateLayout,  /* InvalidateMeasure */
    [100] = uwp_ui_InvalidateLayout, /* InvalidateArrange */
    [101] = uwp_ui_InvalidateLayout, /* UpdateLayout */
    [102 ... 127] = uwp_generic_noop,
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

/* INavigationView own-method getters, ABI slots 56/62 on the FE face
 * (INavigationView : IFrameworkElement, 3 + 53 IFE = base 56):
 * own[0] = get_IsPaneOpen, own[6] = get_FooterMenuItems. */
HRESULT WINAPI uwp_fe_probe6( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 6 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe11( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 11 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe12( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 12 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe27( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 27 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe28( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 28 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe29( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 29 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe30( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 30 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe38( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 38 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe39( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 39 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe44( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 44 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe48( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 48 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe52( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 52 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe53( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 53 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe54( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 54 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe55( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 55 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe57( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 57 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe58( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 58 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe59( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 59 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe60( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 60 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe61( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 61 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe63( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 63 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe64( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 64 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe65( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 65 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe66( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 66 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe67( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 67 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe68( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 68 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe69( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 69 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe70( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 70 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe71( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 71 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe72( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 72 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe73( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 73 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe74( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 74 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe75( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 75 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe76( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 76 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe77( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 77 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe78( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 78 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe79( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 79 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe80( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 80 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe81( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 81 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe82( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 82 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe83( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 83 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe84( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 84 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe85( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 85 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe86( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 86 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe87( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 87 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe88( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 88 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe89( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 89 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe90( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 90 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe91( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 91 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe92( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 92 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe93( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 93 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe94( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 94 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe95( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 95 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe96( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 96 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe97( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 97 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe98( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 98 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe99( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 99 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe100( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 100 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe101( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 101 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe106( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 106 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe107( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 107 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe108( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 108 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe109( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 109 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe110( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 110 hit on NavView\n" ); return S_OK; }
HRESULT WINAPI uwp_fe_probe111( void *iface, ... ) { struct uwp_element *impl = uwp_elem_from_any( iface ); if (impl && impl->class_name[0] && wcsstr( impl->class_name, L"NavigationView" )) TRACE( "fe-vtbl slot 111 hit on NavView\n" ); return S_OK; }

static const void *uwp_fe_vtbl[112] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe_probe6,
    [11] = uwp_fe_probe11,
    [12] = uwp_fe_probe12,
    [27] = uwp_fe_probe27,
    [28] = uwp_fe_probe28,
    [29] = uwp_fe_probe29,
    [30] = uwp_fe_probe30,
    [38] = uwp_fe_get_Style,
    [39] = uwp_fe_put_Style,
    [44] = uwp_fe_probe44,
    [48] = uwp_fe_probe48,
    [52] = uwp_fe_probe52,
    [53] = uwp_fe_probe53,
    [54] = uwp_fe_probe54,
    [55] = uwp_fe_probe55,
    [57] = uwp_fe_probe57,
    [58] = uwp_fe_probe58,
    [59] = uwp_fe_probe59,
    [60] = uwp_fe_probe60,
    [61] = uwp_fe_probe61,
    [63] = uwp_fe_probe63,
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
    [46] = uwp_fe_remove_Unloaded,
    [47] = uwp_fe_add_SizeChanged,
    [49] = uwp_fe_add_LayoutUpdated,
    [50] = uwp_fe_remove_LayoutUpdated,
    [51] = uwp_fe_FindName,
    /* INavigationView ABI slots when this face is cast to NavigationView.
     * Chain: IInspectable(3) + IFrameworkElement(53) = base 56, own-method
     * order from Microsoft.UI.Xaml.winmd 2.8: own[0]=IsPaneOpen(56),
     * own[6]=FooterMenuItems(62), own[46]=SelectionChanged(102),
     * own[48]=ItemInvoked(104) — Time.dll calls *0x1c0,*0x1f0,*0x340,*0x348. */
    [56] = uwp_nav_get_IsPaneOpen,
    [62] = uwp_nav_get_FooterMenuItems,
    [102] = uwp_ui_add_SelectionChanged,
    [103] = uwp_ui_remove_SelectionChanged,
    [104] = uwp_ui_add_ItemInvoked,
    [105] = uwp_ui_remove_ItemInvoked,
    [64] = uwp_fe_probe64, [65] = uwp_fe_probe65, [66] = uwp_fe_probe66, [67] = uwp_fe_probe67, [68] = uwp_fe_probe68, [69] = uwp_fe_probe69, [70] = uwp_fe_probe70, [71] = uwp_fe_probe71, [72] = uwp_fe_probe72, [73] = uwp_fe_probe73, [74] = uwp_fe_probe74, [75] = uwp_fe_probe75, [76] = uwp_fe_probe76, [77] = uwp_fe_probe77, [78] = uwp_fe_probe78, [79] = uwp_fe_probe79, [80] = uwp_fe_probe80, [81] = uwp_fe_probe81, [82] = uwp_fe_probe82, [83] = uwp_fe_probe83, [84] = uwp_fe_probe84, [85] = uwp_fe_probe85, [86] = uwp_fe_probe86, [87] = uwp_fe_probe87, [88] = uwp_fe_probe88, [89] = uwp_fe_probe89, [90] = uwp_fe_probe90, [91] = uwp_fe_probe91, [92] = uwp_fe_probe92, [93] = uwp_fe_probe93, [94] = uwp_fe_probe94, [95] = uwp_fe_probe95, [96] = uwp_fe_probe96, [97] = uwp_fe_probe97, [98] = uwp_fe_probe98, [99] = uwp_fe_probe99, [100] = uwp_fe_probe100, [101] = uwp_fe_probe101, [106] = uwp_fe_probe106, [107] = uwp_fe_probe107, [108] = uwp_fe_probe108, [109] = uwp_fe_probe109, [110] = uwp_fe_probe110, [111] = uwp_fe_probe111
};

/* Default face for Microsoft.UI.Xaml.Controls.NavigationView: CoreRT
 * projections call INavigationView members straight through the instance
 * vtable, so the INavigationView ABI slots (41/42 ItemInvoked, 53/54
 * SelectionChanged per alphabetical winmd metadata over Control's 24 slots)
 * must live in the face the element hands out at creation.  All other slots
 * mirror the FrameworkElement face. */
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
    [6] = uwp_xamlroot_get_Content,
    [7] = uwp_generic_noop,
    [8] = uwp_xamlroot_get_XamlRoot,
    [9 ... 127] = uwp_generic_noop,
};

static const void *uwp_noop_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    /* INavigationView ABI events (disasm-confirmed slots over Control's 24):
     * 56/57 = ItemInvoked, 62/63 = SelectionChanged. */
    [6 ... 55] = uwp_generic_noop,
    [56] = uwp_ui_add_ItemInvoked,
    [57] = uwp_ui_remove_ItemInvoked,
    [58 ... 61] = uwp_generic_noop,
    [62] = uwp_ui_add_SelectionChanged,
    [63] = uwp_ui_remove_SelectionChanged,
    [64 ... 127] = uwp_generic_noop,
};

static const void *uwp_cc_vtbl[128] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_cc_get_Content,
    [7] = uwp_cc_put_Content,
    [8 ... 127] = uwp_generic_noop,
};

static const void *uwp_border_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 15] = uwp_generic_noop,
    [16] = uwp_cc_get_Content,
    [17] = uwp_cc_put_Content,
    [18 ... 31] = uwp_generic_noop,
};

static const void *uwp_btn_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 9] = uwp_generic_noop,
    [10] = uwp_btn_get_Command,
    [11] = uwp_btn_put_Command,
    [12] = uwp_btn_get_CommandParameter,
    [13] = uwp_btn_put_CommandParameter,
    [14] = uwp_btn_add_Click,
    [15 ... 31] = uwp_generic_noop,
};

static const void *uwp_tbtn_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tbtn_get_IsChecked,
    [7] = uwp_tbtn_put_IsChecked,
    [8 ... 9] = uwp_generic_noop,
    [10] = uwp_ui_event_add,
    [11] = uwp_generic_noop,
    [12] = uwp_ui_event_add,
    [13] = uwp_generic_noop,
    [14] = uwp_ui_event_add,
    [15 ... 31] = uwp_generic_noop,
};

static const void *uwp_panel_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_panel_get_Children,
    [7 ... 31] = uwp_generic_noop,
};

/* --- IUIElement7, IKeyboardAccelerator, IXamlUICommand, ICommand, IMenuFlyoutItem, ITeachingTip --- */
HRESULT WINAPI uwp_ui7_get_KeyboardAccelerators( void *iface, void **out )
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
    [6] = uwp_ui7_get_KeyboardAccelerators,
    [7 ... 10] = uwp_generic_noop,
    [11] = uwp_ui7_add_PreviewKeyDown,
    [12] = uwp_ui7_remove_PreviewKeyDown,
    [13 ... 31] = uwp_generic_noop,
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
static HRESULT WINAPI uwp_ui4_add_event( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
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
    [8 ... 23] = uwp_generic_noop,
    [24] = uwp_ui4_add_event,
    [25 ... 31] = uwp_generic_noop,
};

static const void *uwp_ui5_vtbl[36] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 26] = uwp_generic_noop,
    [27] = uwp_ui4_add_event, /* add_GettingFocus */
    [28] = uwp_generic_noop,  /* remove_GettingFocus */
    [29] = uwp_ui4_add_event, /* add_LosingFocus */
    [30] = uwp_generic_noop,  /* remove_LosingFocus */
    [31] = uwp_ui4_add_event, /* add_NoFocusCandidateFound */
    [32 ... 35] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_btn_get_Flyout( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->flyout : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_btn_put_Flyout( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->flyout) IInspectable_Release( impl->flyout );
    impl->flyout = (IInspectable *)val;
    if (impl->flyout) IInspectable_AddRef( impl->flyout );
    return S_OK;
}
static const void *uwp_button_flyout_vtbl[8] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_btn_get_Flyout,
    [7] = uwp_btn_put_Flyout,
};

HRESULT WINAPI uwp_cd2_get_CloseButtonText( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    return WindowsCreateString( impl ? impl->dialog_close_text : L"",
                                impl ? wcslen(impl->dialog_close_text) : 0, out );
}
HRESULT WINAPI uwp_cd2_put_CloseButtonText( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *raw = ((ULONG_PTR)val >= 0x10000) ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) lstrcpynW( impl->dialog_close_text, raw ? raw : L"", ARRAY_SIZE(impl->dialog_close_text) );
    return S_OK;
}
HRESULT WINAPI uwp_cd2_get_DefaultButton( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->dialog_default_btn : 0;
    return S_OK;
}
HRESULT WINAPI uwp_cd2_put_DefaultButton( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->dialog_default_btn = val;
    return S_OK;
}
static HRESULT uwp_frame_navigate_impl( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result );
static HRESULT WINAPI uwp_frame_Navigate( void *iface, void *type_name_ptr, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, NULL, NULL, result );
}
static HRESULT WINAPI uwp_frame2_Navigate( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, transition_info, result );
}

static HRESULT WINAPI uwp_frame_add_Navigated( void *iface, void *handler, EventRegistrationToken *token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    TRACE( "uwp_frame_add_Navigated on %s (%s) handler=%p\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), handler );
    return xaml_event_registry_add( &impl->frame_navigated_reg, handler, token );
}
static HRESULT WINAPI uwp_frame_remove_Navigated( void *iface, EventRegistrationToken token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    return xaml_event_registry_remove( &impl->frame_navigated_reg, token );
}

static HRESULT WINAPI uwp_frame_get_CacheSize( void *iface, INT32 *out )
{
    if (out) *out = 10;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_CanGoBack( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->back_stack) uwp_vec_get_Size( impl->back_stack, &sz );
    if (out) *out = sz > 0 ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_CanGoForward( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->forward_stack) uwp_vec_get_Size( impl->forward_stack, &sz );
    if (out) *out = sz > 0 ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_SourcePageType( void *iface, TypeName *out )
{
    if (out) { out->Name = NULL; out->Kind = 0; }
    return S_OK;
}
static HRESULT WINAPI uwp_frame_get_BackStackDepth( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    unsigned int sz = 0;
    if (impl && impl->back_stack) uwp_vec_get_Size( impl->back_stack, &sz );
    if (out) *out = (INT32)sz;
    return S_OK;
}

static const void *uwp_frame_vtbl[48] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame_get_CacheSize,
    [7] = uwp_generic_noop,        /* put_CacheSize */
    [8] = uwp_frame_get_CanGoBack,
    [9] = uwp_frame_get_CanGoForward,
    [10] = uwp_frame_get_SourcePageType, /* get_CurrentSourcePageType */
    [11] = uwp_frame_get_SourcePageType, /* get_SourcePageType */
    [12] = uwp_generic_noop,       /* put_SourcePageType */
    [13] = uwp_frame_get_BackStackDepth,
    [14] = uwp_frame_add_Navigated,
    [15] = uwp_frame_remove_Navigated,
    [16] = uwp_ui_event_add,       /* add_Navigating */
    [17] = uwp_generic_noop,       /* remove_Navigating */
    [18] = uwp_ui_event_add,       /* add_NavigationFailed */
    [19] = uwp_generic_noop,       /* remove_NavigationFailed */
    [20] = uwp_ui_event_add,       /* add_NavigationStopped */
    [21] = uwp_generic_noop,       /* remove_NavigationStopped */
    [22] = uwp_generic_noop,       /* GoBack */
    [23] = uwp_frame_Navigate,
    [24] = uwp_frame_NavigateWithParam,
    [25 ... 47] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_frame2_get_BackStack( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return E_NOINTERFACE;
    if (!impl->back_stack)
        uwp_vec_create( NULL, &impl->back_stack );
    if (impl->back_stack)
    {
        *out = impl->back_stack;
        IInspectable_AddRef( *out );
    }
    return S_OK;
}
static HRESULT WINAPI uwp_frame2_get_ForwardStack( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return E_NOINTERFACE;
    if (!impl->forward_stack)
        uwp_vec_create( NULL, &impl->forward_stack );
    if (impl->forward_stack)
    {
        *out = impl->forward_stack;
        IInspectable_AddRef( *out );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_frame3_GoBack( void *iface, void *transitionInfo )
{
    return S_OK;
}
static const void *uwp_frame3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame3_GoBack,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_frame5_get_IsNavigationStackEnabled( void *iface, boolean *value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (value) *value = impl ? impl->nav_stack_enabled : TRUE;
    return S_OK;
}
static HRESULT WINAPI uwp_frame5_put_IsNavigationStackEnabled( void *iface, boolean value )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->nav_stack_enabled = value;
    return S_OK;
}
static HRESULT WINAPI uwp_frame5_NavigateToType( void *iface, void *type_name_ptr, IInspectable *param,
                                                  void *nav_options, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, nav_options, result );
}
static const void *uwp_frame5_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame5_get_IsNavigationStackEnabled,
    [7] = uwp_frame5_put_IsNavigationStackEnabled,
    [8] = uwp_frame5_NavigateToType,
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_mpe_get_Source( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->media_source : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_put_Source( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (impl->media_source) IInspectable_Release( impl->media_source );
    impl->media_source = val;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_get_TransportControls( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->media_transport_controls : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_put_TransportControls( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (impl->media_transport_controls) IInspectable_Release( impl->media_transport_controls );
    impl->media_transport_controls = val;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_get_AreTransportControlsEnabled( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->media_transport_enabled : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_put_AreTransportControlsEnabled( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->media_transport_enabled = val;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_get_PosterSource( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->media_poster_source : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_put_PosterSource( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (impl->media_poster_source) IInspectable_Release( impl->media_poster_source );
    impl->media_poster_source = val;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_get_Stretch( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->media_stretch : 2; /* Uniform */
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_put_Stretch( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->media_stretch = val;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_get_AutoPlay( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->media_auto_play : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_put_AutoPlay( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->media_auto_play = val;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_get_IsFullWindow( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->media_is_full_window : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_put_IsFullWindow( void *iface, boolean val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->media_is_full_window = val;
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_get_MediaPlayer( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if (impl && !impl->media_player)
    {
        HSTRING hcls;
        if (SUCCEEDED(WindowsCreateString( L"Windows.Media.Playback.MediaPlayer", 34, &hcls )))
        {
            RoActivateInstance( hcls, &impl->media_player );
            WindowsDeleteString( hcls );
        }
    }
    *out = impl ? impl->media_player : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_mpe_SetMediaPlayer( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (val) IInspectable_AddRef( val );
    if (impl->media_player) IInspectable_Release( impl->media_player );
    impl->media_player = val;
    return S_OK;
}
static const void *uwp_mpe_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_mpe_get_Source,
    [7] = uwp_mpe_put_Source,
    [8] = uwp_mpe_get_TransportControls,
    [9] = uwp_mpe_put_TransportControls,
    [10] = uwp_mpe_get_AreTransportControlsEnabled,
    [11] = uwp_mpe_put_AreTransportControlsEnabled,
    [12] = uwp_mpe_get_PosterSource,
    [13] = uwp_mpe_put_PosterSource,
    [14] = uwp_mpe_get_Stretch,
    [15] = uwp_mpe_put_Stretch,
    [16] = uwp_mpe_get_AutoPlay,
    [17] = uwp_mpe_put_AutoPlay,
    [18] = uwp_mpe_get_IsFullWindow,
    [19] = uwp_mpe_put_IsFullWindow,
    [20] = uwp_mpe_get_MediaPlayer,
    [21] = uwp_mpe_SetMediaPlayer,
    [22 ... 23] = uwp_generic_noop,
};

void uwp_extract_uri_string( IInspectable *uri_obj, WCHAR *out, size_t cap )
{
    IUriRuntimeClass *uri_rt = NULL;
    HSTRING hstr = NULL;
    if (!out || !cap) return;
    out[0] = 0;
    if (!uri_obj || (ULONG_PTR)uri_obj < 0x10000) return;
    if (SUCCEEDED(IInspectable_QueryInterface( uri_obj, &IID_IUriRuntimeClass_g, (void **)&uri_rt )) && uri_rt)
    {
        if (FAILED(IUriRuntimeClass_get_AbsoluteUri( uri_rt, &hstr )) || !hstr)
            IUriRuntimeClass_get_RawUri( uri_rt, &hstr );
        if (hstr)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( hstr, NULL );
            if (raw) lstrcpynW( out, raw, cap );
            WindowsDeleteString( hstr );
        }
        IUriRuntimeClass_Release( uri_rt );
    }
}

static HRESULT WINAPI uwp_bitmap_image_factory_CreateInstanceWithUriSource( void *iface, IInspectable *uri_source, void **value )
{
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.Imaging.BitmapImage", NULL, NULL, value );
    if (SUCCEEDED(hr) && *value && uri_source)
    {
        struct uwp_element *bi_el = uwp_elem_from_any( *value );
        if (bi_el)
        {
            uwp_extract_uri_string( uri_source, bi_el->image_src, ARRAY_SIZE(bi_el->image_src) );
            TRACE( "BitmapImage::CreateInstanceWithUriSource uri=%s -> %p\n",
                   debugstr_w(bi_el->image_src), *value );
        }
    }
    return hr;
}

const void *uwp_bitmap_image_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_bitmap_image_factory_CreateInstanceWithUriSource,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_bi_get_CreateOptions( void *iface, int *out )
{
    if (out) *out = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_bi_put_CreateOptions( void *iface, int val )
{
    return S_OK;
}
static HRESULT WINAPI uwp_bi_get_UriSource( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl && impl->image_src[0])
    {
        HSTRING h_cls = NULL;
        IActivationFactory *ufac = NULL;
        if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.Uri", 22, &h_cls )) && h_cls)
        {
            if (SUCCEEDED(RoGetActivationFactory( h_cls, &IID_IUriRuntimeClassFactory, (void **)&ufac )) && ufac)
            {
                HSTRING h_uri = NULL;
                if (SUCCEEDED(WindowsCreateString( impl->image_src, wcslen(impl->image_src), &h_uri )) && h_uri)
                {
                    IUriRuntimeClassFactory_CreateUri( (IUriRuntimeClassFactory *)ufac, h_uri, (IUriRuntimeClass **)out );
                    WindowsDeleteString( h_uri );
                }
                IActivationFactory_Release( ufac );
            }
            WindowsDeleteString( h_cls );
        }
    }
    return S_OK;
}
static HRESULT WINAPI uwp_bi_put_UriSource( void *iface, IInspectable *uri )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (uri)
        uwp_extract_uri_string( uri, impl->image_src, ARRAY_SIZE(impl->image_src) );
    else
        impl->image_src[0] = 0;
    impl->img_loaded = FALSE;
    InterlockedExchange( &impl->img_loading, 0 );
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_bi_get_int_zero( void *iface, INT32 *out )
{
    if (out) *out = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_bi_put_int_noop( void *iface, INT32 val )
{
    return S_OK;
}
static const void *uwp_bi_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_bi_get_CreateOptions,
    [7] = uwp_bi_put_CreateOptions,
    [8] = uwp_bi_get_UriSource,
    [9] = uwp_bi_put_UriSource,
    [10] = uwp_bi_get_int_zero,  /* get_DecodePixelWidth */
    [11] = uwp_bi_put_int_noop,  /* put_DecodePixelWidth */
    [12] = uwp_bi_get_int_zero,  /* get_DecodePixelHeight */
    [13] = uwp_bi_put_int_noop,  /* put_DecodePixelHeight */
    [14] = uwp_ui_event_add,     /* add_DownloadProgress */
    [15] = uwp_generic_noop,     /* remove_DownloadProgress */
    [16] = uwp_ui_event_add,     /* add_ImageOpened */
    [17] = uwp_generic_noop,     /* remove_ImageOpened */
    [18] = uwp_ui_event_add,     /* add_ImageFailed */
    [19] = uwp_generic_noop,     /* remove_ImageFailed */
    [20 ... 23] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_bs_get_PixelWidth( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? (INT32)impl->img_w : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_bs_get_PixelHeight( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? (INT32)impl->img_h : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_bs_SetSourceAsync( void *iface, void *stream, void **op )
{
    if (op) *op = NULL;
    return S_OK;
}
static const void *uwp_bs_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_bs_get_PixelWidth,
    [7] = uwp_bs_get_PixelHeight,
    [8] = uwp_generic_noop,      /* SetSource */
    [9] = uwp_bs_SetSourceAsync, /* SetSourceAsync */
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_img_get_Source( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->img_source : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_img_put_Source( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->img_source != val)
    {
        if (impl->img_source) IInspectable_Release( impl->img_source );
        impl->img_source = val;
        if (impl->img_source) IInspectable_AddRef( impl->img_source );
    }
    impl->image_src[0] = 0;
    if (val)
    {
        struct uwp_element *src_el = uwp_elem_from_any( val );
        if (src_el && src_el->image_src[0])
            lstrcpynW( impl->image_src, src_el->image_src, ARRAY_SIZE(impl->image_src) );
        else
            uwp_extract_uri_string( val, impl->image_src, ARRAY_SIZE(impl->image_src) );
    }
    impl->img_loaded = FALSE;
    InterlockedExchange( &impl->img_loading, 0 );
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_img_get_Stretch( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->media_stretch : 2;
    return S_OK;
}
static HRESULT WINAPI uwp_img_put_Stretch( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->media_stretch = val;
    return S_OK;
}
static const void *uwp_img_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_img_get_Source,
    [7] = uwp_img_put_Source,
    [8] = uwp_img_get_Stretch,
    [9] = uwp_img_put_Stretch,
    [10] = uwp_ui_event_add,     /* add_ImageFailed */
    [11] = uwp_generic_noop,     /* remove_ImageFailed */
    [12] = uwp_ui_event_add,     /* add_ImageOpened */
    [13] = uwp_generic_noop,     /* remove_ImageOpened */
    [14 ... 23] = uwp_generic_noop,
};

static const void *uwp_frame2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_frame2_get_BackStack,
    [7] = uwp_frame2_get_ForwardStack,
    [8] = uwp_frame2_Navigate, /* Navigate(TypeName, param, transitionInfo, out bool) */
    [9 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_page_get_Frame( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *p;
    if (!out) return E_POINTER;
    *out = NULL;
    for (p = impl ? impl->parent : NULL; p; p = p->parent)
    {
        if (p->kind == UWP_KIND_FRAME)
        {
            *out = &p->IFrame_iface;
            uwp_sub_AddRef( &p->IFrame_iface );
            TRACE( "uwp_page_get_Frame(%s) -> %p (%s)\n",
                   debugstr_w(impl->class_name), *out, debugstr_w(p->name) );
            return S_OK;
        }
    }
    TRACE( "uwp_page_get_Frame(%s) -> NULL\n", impl ? debugstr_w(impl->class_name) : "(null)" );
    return S_OK;
}
static HRESULT WINAPI uwp_page_get_NavigationCacheMode( void *iface, INT32 *val )
{
    if (val) *val = 0;
    return S_OK;
}
static HRESULT WINAPI uwp_page_put_NavigationCacheMode( void *iface, INT32 val )
{
    return S_OK;
}
static HRESULT WINAPI uwp_page_get_NullOut( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static const void *uwp_page_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_page_get_Frame,
    [7] = uwp_page_get_NavigationCacheMode,
    [8] = uwp_page_put_NavigationCacheMode,
    [9] = uwp_page_get_NullOut,  /* get_TopAppBar */
    [10] = uwp_generic_noop,     /* put_TopAppBar */
    [11] = uwp_page_get_NullOut, /* get_BottomAppBar */
    [12 ... 15] = uwp_generic_noop,
};

static const void *uwp_sv_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 49] = uwp_generic_noop,
    [50] = uwp_ui_event_add, /* add_ViewChanged */
    [51] = uwp_generic_noop, /* remove_ViewChanged */
    [52 ... 63] = uwp_generic_noop,
};

enum uwp_elem_kind classify_uwp_class( const WCHAR *cls )
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


ID3D11Device *g_island_d3d11_dev = NULL;
ID3D11DeviceContext *g_island_d3d11_ctx = NULL;
BOOL g_island_d3d11_failed = FALSE;

static HRESULT WINAPI uwp_rtt_QI( IInspectable *iface, REFIID iid, void **out )
{
    return uwp_sub_QueryInterface( iface, iid, out );
}
static ULONG WINAPI uwp_rtt_AddRef( IInspectable *iface )
{
    return uwp_sub_AddRef( iface );
}
static ULONG WINAPI uwp_rtt_Release( IInspectable *iface )
{
    return uwp_sub_Release( iface );
}
static HRESULT WINAPI uwp_rtt_GetIids_( IInspectable *iface, ULONG *count, GUID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_GetRuntimeClassName_( IInspectable *iface, HSTRING *name )
{
    return uwp_sub_GetRuntimeClassName( iface, name );
}
static HRESULT WINAPI uwp_rtt_GetTrustLevel_( IInspectable *iface, UINT *level )
{
    if (level) *level = 0 /* BaseTrust */;
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_AddRefToTracker( IInspectable *iface )
{
    uwp_sub_AddRef( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_ReleaseToTracker( IInspectable *iface )
{
    uwp_sub_Release( iface );
    return S_OK;
}
static HRESULT WINAPI uwp_rtt_GetStrongValue( IInspectable *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = &impl->nondelegating_iface;
    uwp_nondelegating_AddRef( &impl->nondelegating_iface );
    return S_OK;
}
const struct uwp_rtt_target_vtbl uwp_rtt_vtbl =
{
    { uwp_rtt_QI, uwp_rtt_AddRef, uwp_rtt_Release,
      uwp_rtt_GetIids_, uwp_rtt_GetRuntimeClassName_, uwp_rtt_GetTrustLevel_ },
    uwp_rtt_AddRefToTracker,
    uwp_rtt_ReleaseToTracker,
    uwp_rtt_GetStrongValue,
};
static const struct IDispatchVtbl uwp_disp_vtbl =
{
    uwp_disp_QueryInterface,
    uwp_disp_AddRef,
    uwp_disp_Release,
    uwp_disp_GetTypeInfoCount,
    uwp_disp_GetTypeInfo,
    uwp_disp_GetIDsOfNames,
    uwp_disp_Invoke,
};

static HRESULT WINAPI uwp_dt_LoadContent( IInspectable *iface, IInspectable **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IDataTemplate_iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (impl->template_xbf_data && impl->template_xbf_size)
    {
        *out = uwp_instantiate_xbf_substream( impl->template_xbf_data, impl->template_xbf_size,
                                              impl->template_sub_idx, impl->template_owner_comp );
        if (*out)
        {
            struct uwp_element *ce = uwp_elem_from_any( *out );
            WARN( "uwp_dt_LoadContent(%p) sub=%u -> %p (%s bc=%p clicks=%d)\n",
                  impl, (unsigned)impl->template_sub_idx, *out,
                  ce ? debugstr_w(ce->class_name) : "?",
                  ce ? ce->binding_connector : NULL, ce ? ce->click_cnt : -1 );
            return S_OK;
        }
    }
    TRACE( "uwp_dt_LoadContent(%p) -> fallback Grid\n", impl );
    return uwp_element_create( L"Windows.UI.Xaml.Controls.Grid", NULL, NULL, (void **)out );
}

static const void *uwp_data_template_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_dt_LoadContent, /* slot 6: LoadContent */
};

static HRESULT WINAPI uwp_dts_SelectTemplate( IInspectable *iface, IInspectable *item, IInspectable *container, IInspectable **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IDataTemplateSelector_iface );
    void *ovr = NULL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl->outer) return S_OK;
    if (SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, container, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out && SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides2_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out)
        element_create_typed( L"Windows.UI.Xaml.DataTemplate", (void **)out );
    return S_OK;
}

static HRESULT WINAPI uwp_dts2_SelectTemplateForItem( IInspectable *iface, IInspectable *item, IInspectable **out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IDataTemplateSelector2_iface );
    void *ovr = NULL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl->outer) return S_OK;
    if (SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides2_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out && SUCCEEDED(IInspectable_QueryInterface( impl->outer, &IID_IDataTemplateSelectorOverrides_UWP, &ovr )) && ovr)
    {
        HRESULT (WINAPI *fn)(void *, void *, void *, void **) = (void *)(*(void ***)ovr)[6];
        if (fn) fn( ovr, item, NULL, (void **)out );
        IUnknown_Release( (IUnknown *)ovr );
    }
    if (!*out)
        element_create_typed( L"Windows.UI.Xaml.DataTemplate", (void **)out );
    return S_OK;
}

static const void *uwp_dts_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_dts_SelectTemplate, /* slot 6 */
};

static const void *uwp_dts2_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_dts2_SelectTemplateForItem, /* slot 6 */
};

static double uwp_ease_in_core( const WCHAR *cls, double t )
{
    if (wcsstr( cls, L"SineEase" ))
        return 1.0 - sin( (1.0 - t) * 1.5707963267948966 );
    if (wcsstr( cls, L"QuadraticEase" ))
        return t * t;
    if (wcsstr( cls, L"CubicEase" ))
        return t * t * t;
    if (wcsstr( cls, L"QuarticEase" ))
        return t * t * t * t;
    if (wcsstr( cls, L"QuinticEase" ))
        return t * t * t * t * t;
    if (wcsstr( cls, L"CircleEase" ))
        return 1.0 - sqrt( fmax( 0.0, 1.0 - t * t ) );
    if (wcsstr( cls, L"ExponentialEase" ))
        return (t <= 0.0) ? 0.0 : (exp( 2.0 * t ) - 1.0) / (exp( 2.0 ) - 1.0);
    if (wcsstr( cls, L"PowerEase" ))
        return t * t;
    return t;
}

static HRESULT WINAPI uwp_ease_get_EasingMode( IInspectable *iface, INT32 *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    if (!val) return E_POINTER;
    *val = impl->easing_mode;
    return S_OK;
}

static HRESULT WINAPI uwp_ease_put_EasingMode( IInspectable *iface, INT32 val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    impl->easing_mode = val;
    return S_OK;
}

static HRESULT WINAPI uwp_ease_Ease( IInspectable *iface, double t, double *out )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IEasingFunctionBase_iface );
    if (!out) return E_POINTER;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    /* EasingMode: 0=EaseOut, 1=EaseIn, 2=EaseInOut */
    if (impl->easing_mode == 1)
        *out = uwp_ease_in_core( impl->class_name, t );
    else if (impl->easing_mode == 2)
        *out = (t < 0.5) ? (uwp_ease_in_core( impl->class_name, t * 2.0 ) * 0.5)
                         : ((1.0 - uwp_ease_in_core( impl->class_name, (1.0 - t) * 2.0 )) * 0.5 + 0.5);
    else
        *out = 1.0 - uwp_ease_in_core( impl->class_name, 1.0 - t );
    return S_OK;
}

static const void *uwp_ease_vtbl[] =
{
    uwp_sub_QueryInterface, uwp_sub_AddRef, uwp_sub_Release,
    uwp_sub_GetIids, uwp_sub_GetRuntimeClassName, uwp_sub_GetTrustLevel,
    uwp_ease_get_EasingMode, /* slot 6: get_EasingMode */
    uwp_ease_put_EasingMode, /* slot 7: put_EasingMode */
    uwp_ease_Ease,           /* slot 8: Ease(double) */
};

struct uwp_evc_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    float x, y, w, h;
};

static HRESULT WINAPI uwp_evc_args_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IEffectiveViewportChangedEventArgs_UWP ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    WARN( "uwp_evc_args_QI: unmatched %s\n", debugstr_guid(iid) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_evc_args_AddRef( IInspectable *iface )
{
    struct uwp_evc_args *a = CONTAINING_RECORD( iface, struct uwp_evc_args, IInspectable_iface );
    return InterlockedIncrement( &a->ref );
}
static ULONG WINAPI uwp_evc_args_Release( IInspectable *iface )
{
    struct uwp_evc_args *a = CONTAINING_RECORD( iface, struct uwp_evc_args, IInspectable_iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r) free( a );
    return r;
}
static HRESULT WINAPI uwp_evc_args_GetIids( IInspectable *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.EffectiveViewportChangedEventArgs", 49, name );
}
static HRESULT WINAPI uwp_evc_args_GetTrustLevel( IInspectable *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_get_EffectiveViewport( IInspectable *iface, float *rect )
{
    struct uwp_evc_args *a = CONTAINING_RECORD( iface, struct uwp_evc_args, IInspectable_iface );
    if (!rect) return E_POINTER;
    WARN( "uwp_evc_args_get_EffectiveViewport -> (%.0f,%.0f,%.0f,%.0f)\n", a->x, a->y, a->w, a->h );
    rect[0] = a->x; rect[1] = a->y; rect[2] = a->w; rect[3] = a->h;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_get_MaxViewport( IInspectable *iface, float *rect )
{
    if (!rect) return E_POINTER;
    rect[0] = 0.0f; rect[1] = 0.0f; rect[2] = 1020.0f; rect[3] = 740.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_evc_args_get_BringIntoViewDist( IInspectable *iface, double *val )
{
    if (val) *val = 0.0;
    return S_OK;
}
static const void *uwp_evc_args_vtbl[] =
{
    uwp_evc_args_QI, uwp_evc_args_AddRef, uwp_evc_args_Release,
    uwp_evc_args_GetIids, uwp_evc_args_GetRuntimeClassName, uwp_evc_args_GetTrustLevel,
    uwp_evc_args_get_EffectiveViewport,
    uwp_evc_args_get_MaxViewport,
    uwp_evc_args_get_BringIntoViewDist,
    uwp_evc_args_get_BringIntoViewDist,
};

static void uwp_fire_evc_one( struct uwp_element *impl, IInspectable *handler )
{
    struct uwp_evc_args *args;
    IInspectable *sender;
    struct xaml_event_handler *h = (struct xaml_event_handler *)handler;
    if (!impl || !handler) return;
    if (!(args = calloc( 1, sizeof(*args) ))) return;
    args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_evc_args_vtbl;
    args->ref = 1;
    args->x = 0.0f;
    args->y = 0.0f;
    args->w = impl->actual_w > 10.0 ? (float)impl->actual_w : 960.0f;
    args->h = impl->actual_h > 10.0 ? (float)impl->actual_h : 650.0f;
    sender = impl->outer ? impl->outer : &impl->IFrameworkElement_iface;
    WARN( "EVC_BEFORE %s (%s) h=%p sender=%p rect=(%.0f,%.0f,%.0f,%.0f)\n",
          debugstr_w(impl->class_name), debugstr_w(impl->name), h, sender, args->x, args->y, args->w, args->h );
    __TRY
    {
        h->lpVtbl->Invoke( h, sender, &args->IInspectable_iface );
    }
    __EXCEPT_ALL
    {
        WARN( "EVC_EXCEPT %s (%s)\n", debugstr_w(impl->class_name), debugstr_w(impl->name) );
    }
    __ENDTRY;
    WARN( "EVC_AFTER %s (%s)\n", debugstr_w(impl->class_name), debugstr_w(impl->name) );
    IInspectable_Release( &args->IInspectable_iface );
}

void uwp_fire_evc_if_needed( struct uwp_element *impl )
{
    int i;
    if (!impl || impl->evc_cnt <= 0 || impl->evc_fired) return;
    impl->evc_fired = TRUE;
    for (i = 0; i < impl->evc_cnt; i++)
        uwp_fire_evc_one( impl, impl->evc_handlers[i] );
}

static HRESULT WINAPI uwp_fe7_get_IsLoaded( IInspectable *iface, boolean *val )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    BOOL loaded = impl->loaded_fired;
    if (!loaded && impl->parent && impl->parent->loaded_fired) loaded = TRUE;
    TRACE( "uwp_fe7_get_IsLoaded(%s, %s) -> %d (w=%.0f h=%.0f)\n",
           debugstr_w(impl->class_name), debugstr_w(impl->name), loaded, impl->actual_w, impl->actual_h );
    if (val) *val = loaded ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_fe7_add_EffectiveViewportChanged( IInspectable *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    if (tok && (ULONG_PTR)tok > 0x10000 && !IsBadWritePtr( tok, sizeof(*tok) ))
        tok->value = impl->evc_cnt + 1;
    if (handler && impl->evc_cnt < (int)ARRAY_SIZE(impl->evc_handlers))
    {
        WARN( "EVC_ADD %s (%s) h=%p tok=%d\n", debugstr_w(impl->class_name), debugstr_w(impl->name), handler, impl->evc_cnt + 1 );
        impl->evc_handlers[impl->evc_cnt++] = (IInspectable *)handler;
        IInspectable_AddRef( (IInspectable *)handler );
        impl->evc_fired = FALSE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_fe7_remove_EffectiveViewportChanged( IInspectable *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    if (wcsstr( impl->class_name, L"ItemsRepeater" ))
        WARN( "EVC_REM %s (%s) tok=%lld\n", debugstr_w(impl->class_name), debugstr_w(impl->name), (long long)tok.value );
    if (tok.value >= 1 && tok.value <= (INT64)impl->evc_cnt)
    {
        int idx = (int)(tok.value - 1);
        if (impl->evc_handlers[idx])
        {
            IInspectable_Release( impl->evc_handlers[idx] );
            impl->evc_handlers[idx] = NULL;
        }
    }
    return S_OK;
}
static HRESULT WINAPI uwp_fe7_InvalidateViewport( IInspectable *iface )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, IFrameworkElement7_iface );
    int i;
    for (i = 0; i < impl->evc_cnt; i++)
        uwp_fire_evc_one( impl, impl->evc_handlers[i] );
    return S_OK;
}
static const void *uwp_fe7_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_fe7_get_IsLoaded,                    /* slot 6: get_IsLoaded */
    [7] = uwp_fe7_add_EffectiveViewportChanged,    /* slot 7: add_EffectiveViewportChanged */
    [8] = uwp_fe7_remove_EffectiveViewportChanged, /* slot 8: remove_EffectiveViewportChanged */
    [9] = uwp_fe7_InvalidateViewport,              /* slot 9: InvalidateViewport */
    [10 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_scp_get_CompositionScaleX( void *iface, float *val )
{
    if (val) *val = 1.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_scp_get_CompositionScaleY( void *iface, float *val )
{
    if (val) *val = 1.0f;
    return S_OK;
}
static const GUID IID_ICoreInputSourceBase_UWP =
    { 0x9f488807, 0x4580, 0x4be8, { 0xbe, 0x68, 0x92, 0xa9, 0x31, 0x17, 0x13, 0xbb } };
static const GUID IID_ICorePointerInputSource_UWP =
    { 0xbbf1bb18, 0xe47a, 0x48eb, { 0x88, 0x07, 0xf8, 0xf8, 0xd3, 0xea, 0x45, 0x51 } };
static const GUID IID_ICorePointerInputSource2_UWP =
    { 0xd70370ba, 0x1953, 0x4271, { 0xb8, 0x65, 0x72, 0x77, 0x0c, 0x92, 0x7d, 0x03 } };

#define CIIS_MAX_HANDLERS 16
#define CIIS_MAX_SOURCES  16

struct core_ind_input_source
{
    const void **lpVtbl_base;
    const void **lpVtbl_ptr;
    const void **lpVtbl_ptr2;
    LONG ref;
    LONG next_token;
    boolean input_enabled;
    boolean has_capture;
    float last_x;
    float last_y;
    void *handlers[7][CIIS_MAX_HANDLERS];
    INT64 tokens[7][CIIS_MAX_HANDLERS];
};

static CRITICAL_SECTION g_ciis_cs = { NULL, -1, 0, 0, 0, 0 };
static struct core_ind_input_source *g_ciis_sources[CIIS_MAX_SOURCES];

static inline struct core_ind_input_source *ciis_from_base( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_base ); }
static inline struct core_ind_input_source *ciis_from_ptr( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_ptr ); }
static inline struct core_ind_input_source *ciis_from_ptr2( void *iface )
{ return CONTAINING_RECORD( iface, struct core_ind_input_source, lpVtbl_ptr2 ); }

static HRESULT WINAPI ciis_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct core_ind_input_source *impl = ciis_from_base( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICoreInputSourceBase_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_base;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICorePointerInputSource_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_ptr;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICorePointerInputSource2_UWP ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->lpVtbl_ptr2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ciis_AddRef( void *iface )
{ return InterlockedIncrement( &ciis_from_base( iface )->ref ); }
static ULONG WINAPI ciis_Release( void *iface )
{
    struct core_ind_input_source *impl = ciis_from_base( iface );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        int i, k;
        EnterCriticalSection( &g_ciis_cs );
        for (i = 0; i < CIIS_MAX_SOURCES; i++)
            if (g_ciis_sources[i] == impl) g_ciis_sources[i] = NULL;
        LeaveCriticalSection( &g_ciis_cs );
        for (k = 0; k < 7; k++)
            for (i = 0; i < CIIS_MAX_HANDLERS; i++)
                if (impl->handlers[k][i]) IUnknown_Release( (IUnknown *)impl->handlers[k][i] );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI ciis_GetIids( void *iface, ULONG *cnt, IID **iids )
{ if (cnt) *cnt = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI ciis_GetRuntimeClassName( void *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.UI.Core.CoreIndependentInputSource", 42, name ); }
static HRESULT WINAPI ciis_GetTrustLevel( void *iface, TrustLevel *lvl )
{ if (lvl) *lvl = BaseTrust; return S_OK; }

static HRESULT WINAPI ciis_get_Dispatcher( void *iface, ICoreDispatcher **out )
{
    ICoreWindow *cw = get_current_core_window();
    if (!out) return E_POINTER;
    *out = NULL;
    if (cw) return ICoreWindow_get_Dispatcher( cw, out );
    return S_OK;
}
static HRESULT WINAPI ciis_get_IsInputEnabled( void *iface, boolean *val )
{ if (val) *val = ciis_from_base( iface )->input_enabled; return S_OK; }
static HRESULT WINAPI ciis_put_IsInputEnabled( void *iface, boolean val )
{ ciis_from_base( iface )->input_enabled = val; return S_OK; }
static HRESULT WINAPI ciis_event_add( void *iface, void *handler, EventRegistrationToken *token )
{ if (token) token->value = 1; return S_OK; }
static HRESULT WINAPI ciis_event_remove( void *iface, EventRegistrationToken token )
{ return S_OK; }

static HRESULT ciis_ptr_add_kind( void *iface, int kind, void *handler, EventRegistrationToken *token )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    int i;
    if (!handler) return E_POINTER;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_HANDLERS; i++)
    {
        if (!impl->handlers[kind][i])
        {
            INT64 tok = InterlockedIncrement( &impl->next_token );
            IUnknown_AddRef( (IUnknown *)handler );
            impl->handlers[kind][i] = handler;
            impl->tokens[kind][i] = tok;
            if (token) token->value = tok;
            LeaveCriticalSection( &g_ciis_cs );
            ERR( "ciis_ptr_add_kind kind=%d handler=%p tok=%lld\n", kind, handler, (long long)tok );
            return S_OK;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    return E_OUTOFMEMORY;
}

static HRESULT ciis_ptr_remove_kind( void *iface, int kind, EventRegistrationToken token )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    int i;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_HANDLERS; i++)
    {
        if (impl->handlers[kind][i] && impl->tokens[kind][i] == token.value)
        {
            void *h = impl->handlers[kind][i];
            impl->handlers[kind][i] = NULL;
            impl->tokens[kind][i] = 0;
            LeaveCriticalSection( &g_ciis_cs );
            IUnknown_Release( (IUnknown *)h );
            return S_OK;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    return S_OK;
}

#define CIIS_PTR_EV( name, kind ) \
static HRESULT WINAPI ciis_add_##name( void *iface, void *h, EventRegistrationToken *t ) \
{ return ciis_ptr_add_kind( iface, kind, h, t ); } \
static HRESULT WINAPI ciis_remove_##name( void *iface, EventRegistrationToken t ) \
{ return ciis_ptr_remove_kind( iface, kind, t ); }

CIIS_PTR_EV( PointerPressed,      XAML_PTR_PRESSED )
CIIS_PTR_EV( PointerMoved,        XAML_PTR_MOVED )
CIIS_PTR_EV( PointerReleased,     XAML_PTR_RELEASED )
CIIS_PTR_EV( PointerWheelChanged, XAML_PTR_WHEEL )
CIIS_PTR_EV( PointerEntered,      XAML_PTR_ENTERED )
CIIS_PTR_EV( PointerExited,       XAML_PTR_EXITED )
CIIS_PTR_EV( PointerCaptureLost,  XAML_PTR_CAPTURE_LOST )

void uwp_ciis_dispatch_pointer_event( enum xaml_pointer_event_kind kind, const struct xaml_pointer_info *info )
{
    struct core_ind_input_source *srcs[CIIS_MAX_SOURCES];
    void *handlers[CIIS_MAX_HANDLERS];
    int n_srcs = 0, i, k;
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs *args = NULL;

    if ((unsigned int)kind >= 7 || !info) return;

    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_SOURCES; i++)
    {
        if (g_ciis_sources[i] && g_ciis_sources[i]->input_enabled)
        {
            InterlockedIncrement( &g_ciis_sources[i]->ref );
            srcs[n_srcs++] = g_ciis_sources[i];
        }
    }
    LeaveCriticalSection( &g_ciis_cs );

    if (!n_srcs) return;
    if (kind == XAML_PTR_PRESSED || kind == XAML_PTR_WHEEL)
        ERR( "uwp_ciis_dispatch_pointer_event kind=%d pos=(%.1f,%.1f) R=%d L=%d wheel=%d srcs=%d\n",
             kind, info->x, info->y, info->is_right, info->is_left, info->wheel_delta, n_srcs );
    args = xaml_core_pointer_args_create( info );
    if (!args)
    {
        for (i = 0; i < n_srcs; i++) ciis_Release( &srcs[i]->lpVtbl_base );
        return;
    }

    for (i = 0; i < n_srcs; i++)
    {
        struct core_ind_input_source *src = srcs[i];
        int n_h = 0;
        src->last_x = info->x;
        src->last_y = info->y;
        EnterCriticalSection( &g_ciis_cs );
        for (k = 0; k < CIIS_MAX_HANDLERS; k++)
        {
            if (src->handlers[kind][k])
            {
                IUnknown_AddRef( (IUnknown *)src->handlers[kind][k] );
                handlers[n_h++] = src->handlers[kind][k];
            }
        }
        LeaveCriticalSection( &g_ciis_cs );
        for (k = 0; k < n_h; k++)
        {
            typedef HRESULT (WINAPI *invoke_fn)( void *iface, void *sender, void *args );
            void ***vtbl = (void ***)handlers[k];
            invoke_fn fn = (invoke_fn)((*vtbl)[3]);
            __TRY
            {
                fn( handlers[k], &src->lpVtbl_ptr, args );
            }
            __EXCEPT_ALL
            {
            }
            __ENDTRY;
            IUnknown_Release( (IUnknown *)handlers[k] );
        }
        ciis_Release( &src->lpVtbl_base );
    }
    __x_ABI_CWindows_CUI_CCore_CIPointerEventArgs_Release( args );
}

static const void *ciis_base_vtbl[] =
{
    ciis_QueryInterface,
    ciis_AddRef,
    ciis_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_get_Dispatcher,
    ciis_get_IsInputEnabled,
    ciis_put_IsInputEnabled,
    ciis_event_add,
    ciis_event_remove,
};

static HRESULT WINAPI ciis_ptr_QI( void *iface, REFIID iid, void **out )
{ return ciis_QueryInterface( &ciis_from_ptr( iface )->lpVtbl_base, iid, out ); }
static ULONG WINAPI ciis_ptr_AddRef( void *iface )
{ return ciis_AddRef( &ciis_from_ptr( iface )->lpVtbl_base ); }
static ULONG WINAPI ciis_ptr_Release( void *iface )
{ return ciis_Release( &ciis_from_ptr( iface )->lpVtbl_base ); }
static HRESULT WINAPI ciis_ptr_ReleaseCapture( void *iface )
{ ciis_from_ptr( iface )->has_capture = FALSE; return S_OK; }
static HRESULT WINAPI ciis_ptr_SetCapture( void *iface )
{ ciis_from_ptr( iface )->has_capture = TRUE; return S_OK; }
static HRESULT WINAPI ciis_ptr_get_HasCapture( void *iface, boolean *val )
{ if (val) *val = ciis_from_ptr( iface )->has_capture; return S_OK; }
static HRESULT WINAPI ciis_ptr_get_PointerPosition( void *iface, Point *pt )
{
    struct core_ind_input_source *impl = ciis_from_ptr( iface );
    if (pt) { pt->X = impl->last_x; pt->Y = impl->last_y; }
    return S_OK;
}
static HRESULT WINAPI ciis_ptr_get_PointerCursor( void *iface, void **out )
{ if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ciis_ptr_put_PointerCursor( void *iface, void *cursor )
{ return S_OK; }

static const void *ciis_ptr_vtbl[] =
{
    ciis_ptr_QI,
    ciis_ptr_AddRef,
    ciis_ptr_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_ptr_ReleaseCapture,      /* 6: ReleasePointerCapture */
    ciis_ptr_SetCapture,          /* 7: SetPointerCapture */
    ciis_ptr_get_HasCapture,      /* 8: get_HasCapture */
    ciis_ptr_get_PointerPosition, /* 9: get_PointerPosition */
    ciis_ptr_get_PointerCursor,   /* 10: get_PointerCursor */
    ciis_ptr_put_PointerCursor,   /* 11: put_PointerCursor */
    ciis_add_PointerCaptureLost,      /* 12: add_PointerCaptureLost */
    ciis_remove_PointerCaptureLost,   /* 13: remove_PointerCaptureLost */
    ciis_add_PointerEntered,          /* 14: add_PointerEntered */
    ciis_remove_PointerEntered,       /* 15: remove_PointerEntered */
    ciis_add_PointerExited,           /* 16: add_PointerExited */
    ciis_remove_PointerExited,        /* 17: remove_PointerExited */
    ciis_add_PointerMoved,            /* 18: add_PointerMoved */
    ciis_remove_PointerMoved,         /* 19: remove_PointerMoved */
    ciis_add_PointerPressed,          /* 20: add_PointerPressed */
    ciis_remove_PointerPressed,       /* 21: remove_PointerPressed */
    ciis_add_PointerReleased,         /* 22: add_PointerReleased */
    ciis_remove_PointerReleased,      /* 23: remove_PointerReleased */
    ciis_add_PointerWheelChanged,     /* 24: add_PointerWheelChanged */
    ciis_remove_PointerWheelChanged,  /* 25: remove_PointerWheelChanged */
};

static HRESULT WINAPI ciis_ptr2_QI( void *iface, REFIID iid, void **out )
{ return ciis_QueryInterface( &ciis_from_ptr2( iface )->lpVtbl_base, iid, out ); }
static ULONG WINAPI ciis_ptr2_AddRef( void *iface )
{ return ciis_AddRef( &ciis_from_ptr2( iface )->lpVtbl_base ); }
static ULONG WINAPI ciis_ptr2_Release( void *iface )
{ return ciis_Release( &ciis_from_ptr2( iface )->lpVtbl_base ); }
static HRESULT WINAPI ciis_ptr2_get_DispatcherQueue( void *iface, void **out )
{ return uwp_do_get_DispatcherQueue( NULL, out ); }

static const void *ciis_ptr2_vtbl[] =
{
    ciis_ptr2_QI,
    ciis_ptr2_AddRef,
    ciis_ptr2_Release,
    ciis_GetIids,
    ciis_GetRuntimeClassName,
    ciis_GetTrustLevel,
    ciis_ptr2_get_DispatcherQueue,
};

static HRESULT WINAPI uwp_scp_CreateCoreIndependentInputSource( void *iface, UINT32 device_types, void **out )
{
    struct core_ind_input_source *src;
    int i;
    ERR( "uwp_scp_CreateCoreIndependentInputSource dev=%#x\n", device_types );
    if (!out) return E_POINTER;
    if (!(src = calloc( 1, sizeof(*src) ))) return E_OUTOFMEMORY;
    src->lpVtbl_base = ciis_base_vtbl;
    src->lpVtbl_ptr = ciis_ptr_vtbl;
    src->lpVtbl_ptr2 = ciis_ptr2_vtbl;
    src->ref = 1;
    src->input_enabled = TRUE;
    EnterCriticalSection( &g_ciis_cs );
    for (i = 0; i < CIIS_MAX_SOURCES; i++)
    {
        if (!g_ciis_sources[i])
        {
            g_ciis_sources[i] = src;
            break;
        }
    }
    LeaveCriticalSection( &g_ciis_cs );
    *out = &src->lpVtbl_base;
    return S_OK;
}
static const void *uwp_scp_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_scp_get_CompositionScaleX,
    [7] = uwp_scp_get_CompositionScaleY,
    [8] = uwp_ui_event_add,
    [9] = uwp_generic_noop,
    [10] = uwp_scp_CreateCoreIndependentInputSource,
    [11 ... 15] = uwp_generic_noop,
};
void uwp_notify_swapchain_panel_resize( void *el_ptr, UINT w, UINT h )
{
    struct uwp_element *impl = (struct uwp_element *)el_ptr;
    if (!impl || w <= 1 || h <= 1) return;
    impl->actual_w = (double)w;
    impl->actual_h = (double)h;
    uwp_fire_size_and_layout_changed( impl );
}

static HRESULT WINAPI uwp_scp_native_SetSwapChain( void *iface, void *sc )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ISwapChainPanelNative2_iface );
    HRESULT hr;
    ERR( "uwp_scp_native_SetSwapChain iface=%p sc=%p\n", iface, sc );
    xaml_set_active_swapchain_panel( impl, sc );
    hr = xaml_attach_swapchain_panel_content( (IUnknown *)sc, &impl->scp_visual );
    if (sc)
    {
        UINT w = 0, h = 0;
        xaml_get_host_window_size( &w, &h );
        if (w > 1 && h > 1)
        {
            impl->actual_w = (double)w;
            impl->actual_h = (double)h;
            ERR( "uwp_scp_native_SetSwapChain firing SizeChanged %ux%u (handlers=%d)\n",
                 w, h, impl->size_changed_cnt );
            uwp_fire_size_and_layout_changed( impl );
        }
    }
    return hr;
}
static HRESULT WINAPI uwp_scp_native_SetSwapChainHandle( void *iface, HANDLE h )
{
    struct uwp_element *impl = CONTAINING_RECORD( iface, struct uwp_element, ISwapChainPanelNative2_iface );
    ERR( "uwp_scp_native_SetSwapChainHandle iface=%p h=%p\n", iface, h );
    return xaml_attach_swapchain_panel_handle( h, &impl->scp_visual );
}
static const void *uwp_scp_native_vtbl[8] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_scp_native_SetSwapChain,
    [4] = uwp_scp_native_SetSwapChainHandle,
    [5 ... 7] = uwp_generic_noop,
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
        : (class_name && !wcscmp( class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ))
        ? (const struct IInspectableVtbl *)uwp_navview_vtbl
        : (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IDependencyObject_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do_vtbl;
    impl->IDependencyObject2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_do2_vtbl;
    impl->IUIElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui_vtbl;
    impl->IFrameworkElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IFrameworkElement2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe2_vtbl;
    impl->IFrameworkElement3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe3_vtbl;
    impl->IFrameworkElement7_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe7_vtbl;
    impl->IControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_vtbl;
    impl->IControlProtected_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ctrl_prot_vtbl;
    impl->IContentControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IUserControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cc_vtbl;
    impl->IPage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_page_vtbl;
    impl->IFrame_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame_vtbl;
    impl->IFrame2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame2_vtbl;
    impl->IFrame3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame3_vtbl;
    impl->IFrame5_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_frame5_vtbl;
    impl->nav_stack_enabled = TRUE;
    impl->IButtonBase_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_btn_vtbl;
    impl->IToggleButton_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tbtn_vtbl;
    impl->IPanel_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_panel_vtbl;
    impl->IGrid_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid_vtbl;
    impl->IGrid3_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_grid3_vtbl;
    impl->IBorder_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_border_vtbl;
    impl->ITextBlock_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tb_vtbl;
    impl->IParagraph_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_paragraph_vtbl;
    impl->IRun_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_run_vtbl;
    impl->IRowDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IColumnDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_rowcol_vtbl;
    impl->IStyle_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_noop_vtbl;
    impl->IDataTemplate_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_data_template_vtbl;
    impl->IDataTemplateSelector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dts_vtbl;
    impl->IDataTemplateSelector2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_dts2_vtbl;
    impl->IBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_brush_vtbl;
    impl->ISolidColorBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scb_vtbl;
    impl->IFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_flyout_vtbl;
    impl->ISelector_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_selector_vtbl;
    impl->IItemsControl_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_items_ctrl_vtbl;
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
    impl->IUIElement5_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ui5_vtbl;
    impl->IButtonWithFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_button_flyout_vtbl;
    impl->IContentDialog_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd_vtbl;
    impl->IContentDialog2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cd2_vtbl;
    impl->ISwapChainPanel_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scp_vtbl;
    impl->ISwapChainPanelNative2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_scp_native_vtbl;
    impl->ITeachingTip_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_teachingtip_vtbl;
    impl->IAutoSuggestBox_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_asb_vtbl;
    impl->IAutoSuggestBox2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_asb2_vtbl;
    impl->ISplitView_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_splitview_vtbl;
    impl->ISplitView2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_splitview2_vtbl;
    impl->IEasingFunctionBase_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_ease_vtbl;
    impl->IMediaPlayerElement_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_mpe_vtbl;
    impl->IBitmapImage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bi_vtbl;
    impl->IBitmapSource_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bs_vtbl;
    impl->IImage_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_img_vtbl;
    impl->media_stretch = 2; /* Uniform */
    impl->IWeakReferenceSource_iface.lpVtbl = &uwp_wrs_vtbl;
    impl->IDispatch_iface.lpVtbl = &uwp_disp_vtbl;
    impl->IReferenceTrackerTarget_iface.lpVtbl = (const IInspectableVtbl *)&uwp_rtt_vtbl;
    impl->ITypeInfo_iface.lpVtbl = &uwp_disp_ti_vtbl;

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
        void *ui = NULL;
        HRESULT qi_hr = E_FAIL;
        *inner = &impl->nondelegating_iface;
        /* Delegation mode is a property of the outer object, not the app: an outer
         * that answers IUIElement is a real app-side element, so the element's
         * lifetime rides on it; otherwise the element owns its own refcount. */
        if (outer &&
            !IsBadReadPtr( outer, sizeof(void *) ) &&
            !IsBadReadPtr( *(void **)outer, 2 * sizeof(void *) ))
        {
            __TRY
            {
                qi_hr = IInspectable_QueryInterface( outer, &IID_IUIElement_UWP, &ui );
            }
            __EXCEPT_ALL
            {
                qi_hr = E_FAIL;
            }
            __ENDTRY;
        }
        if (SUCCEEDED( qi_hr ) && ui)
        {
            IInspectable_Release( (IInspectable *)ui );
            impl->delegating = TRUE;
            IInspectable_AddRef( outer );
        }
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
    else if (!wcscmp( impl->class_name, L"Windows.UI.Xaml.Data.Binding" ) ||
             !wcscmp( impl->class_name, L"Windows.UI.Xaml.Data.RelativeSource" ) ||
             !wcscmp( impl->class_name, L"Windows.UI.Xaml.Controls.SymbolIcon" ))
        *instance = &impl->IStyle_iface;
    else
        *instance = &impl->IFrameworkElement_iface;

    return S_OK;
}

/* ============================================================================
 * 5. DataContext Propagation, Loaded & OnApplyTemplate Dispatch, Frame Navigate
 * ============================================================================ */

struct uwp_ccc_args
{
    const void *vtbl;
    LONG ref;
    IInspectable *item;
    UINT32 phase;
    boolean in_recycle;
    boolean handled;
};

static HRESULT WINAPI uwp_ccc_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IContainerContentChangingEventArgs_g =
        { 0x13922481, 0x41f3, 0x4e20, { 0xb6, 0x3a, 0x3f, 0x53, 0x63, 0x2e, 0x10, 0x85 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IContainerContentChangingEventArgs_g ))
    {
        *out = iface;
        InterlockedIncrement( &((struct uwp_ccc_args *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI uwp_ccc_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_ccc_args *)iface)->ref );
}

static ULONG WINAPI uwp_ccc_Release( void *iface )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    LONG r = InterlockedDecrement( &a->ref );
    if (r == 0)
    {
        if (a->item) IInspectable_Release( a->item );
        free( a );
    }
    return r;
}

static HRESULT WINAPI uwp_ccc_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_ccc_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.ContainerContentChangingEventArgs", 58, name );
}

static HRESULT WINAPI uwp_ccc_GetTrustLevel( void *iface, TrustLevel *tl )
{
    if (tl) *tl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uwp_ccc_get_ItemContainer( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_InRecycleQueue( void *iface, boolean *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->in_recycle; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_ItemIndex( void *iface, INT32 *out ) { if (out) *out = 0; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_Item( void *iface, IInspectable **out )
{
    struct uwp_ccc_args *a = (struct uwp_ccc_args *)iface;
    if (!out) return E_POINTER;
    *out = a->item;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_ccc_get_Phase( void *iface, UINT32 *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->phase; return S_OK; }
static HRESULT WINAPI uwp_ccc_put_Handled( void *iface, boolean v ) { ((struct uwp_ccc_args *)iface)->handled = v; return S_OK; }
static HRESULT WINAPI uwp_ccc_get_Handled( void *iface, boolean *out ) { if (out) *out = ((struct uwp_ccc_args *)iface)->handled; return S_OK; }
static HRESULT WINAPI uwp_ccc_RegisterUpdateCallback( void *iface, void *cb ) { return S_OK; }
static HRESULT WINAPI uwp_ccc_RegisterUpdateCallbackWithPhase( void *iface, UINT32 p, void *cb ) { return S_OK; }

static const void *uwp_ccc_vtbl[] =
{
    uwp_ccc_QueryInterface,
    uwp_ccc_AddRef,
    uwp_ccc_Release,
    uwp_ccc_GetIids,
    uwp_ccc_GetRuntimeClassName,
    uwp_ccc_GetTrustLevel,
    uwp_ccc_get_ItemContainer,
    uwp_ccc_get_InRecycleQueue,
    uwp_ccc_get_ItemIndex,
    uwp_ccc_get_Item,
    uwp_ccc_get_Phase,
    uwp_ccc_put_Handled,
    uwp_ccc_get_Handled,
    uwp_ccc_RegisterUpdateCallback,
    uwp_ccc_RegisterUpdateCallbackWithPhase,
};

void uwp_propagate_data_context( struct uwp_element *impl, IInspectable *dc )
{
    int i;
    IInspectable *old_dc;
    if (!impl) return;
    old_dc = impl->data_context;
    if (old_dc) IInspectable_AddRef( old_dc );
    if (impl->data_context != dc)
    {
        if (impl->data_context) IInspectable_Release( impl->data_context );
        impl->data_context = dc;
        if (dc) IInspectable_AddRef( dc );
    }
    if (wcsstr( impl->class_name, L"Card" ))
    {
        static const GUID IID_IDTE = { 0x595e9547, 0xcdff, 0x4b92, { 0xb7, 0x73, 0xab, 0x39, 0x68, 0x78, 0xf3, 0x53 } };
        void *dte = NULL;
        if (impl->binding_connector &&
            SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)impl->binding_connector, &IID_IDTE, &dte )) && dte)
            IUnknown_Release( (IUnknown *)dte );
        WARN( "Card DC(%p) dc=%p dc_cnt=%d bc=%p dte=%p clicks=%d cmd=%p\n",
              impl, dc, impl->dc_cnt, impl->binding_connector, dte, impl->click_cnt, impl->command );
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
    else if (!xaml_get_app_caps()->teardown_vm && g_calc_vm.vm)
    {
        uwp_calc_vm_teardown( FALSE );
    }

    if (dc && dc != old_dc && impl->binding_connector && impl->dc_cnt == 0)
    {
        static const GUID IID_IDTE_g = { 0x595e9547, 0xcdff, 0x4b92, { 0xb7, 0x73, 0xab, 0x39, 0x68, 0x78, 0xf3, 0x53 } };
        void *dte = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)impl->binding_connector, &IID_IDTE_g, &dte )) && dte)
        {
            struct uwp_ccc_args *ccc = calloc( 1, sizeof(*ccc) );
            if (ccc)
            {
                INT32 next_phase = -1;
                HRESULT (WINAPI *proc_bindings)(void *, void *, INT32 *) = (void *)(*(void ***)dte)[8];
                ccc->vtbl = uwp_ccc_vtbl;
                ccc->ref = 1;
                ccc->item = dc;
                IInspectable_AddRef( dc );
                ccc->phase = 0;
                ccc->in_recycle = FALSE;
                if (proc_bindings)
                {
                    __TRY
                    {
                        proc_bindings( dte, ccc, &next_phase );
                    }
                    __EXCEPT_ALL {}
                    __ENDTRY;
                }
                uwp_ccc_Release( ccc );
            }
            IUnknown_Release( (IUnknown *)dte );
        }
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
        if (ch && (!ch->data_context || (old_dc && ch->data_context == old_dc)) && ch->data_context != dc)
            uwp_propagate_data_context( ch, dc );
    }
    if (impl->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( impl->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && (!ch->data_context || (old_dc && ch->data_context == old_dc)) && ch->data_context != dc)
                uwp_propagate_data_context( ch, dc );
        }
    }
    if (old_dc) IInspectable_Release( old_dc );
}

static struct { const void *vtbl; LONG ref; } g_dp_routed_args = { (const void *)dp_changed_args_vtbl, 1 };
/* Loaded/Click/Navigation callbacks receive this: McgMarshal ComInterfaceToObject
 * calls GetRuntimeClassName on it, so it must be an element face naming
 * RoutedEventArgs. */
static struct uwp_element *g_routed_args_el = NULL;
/* McgMarshal caches the RCW (__ComObject) per IUnknown pointer and pins its
 * interop type at the first crossing.  Handlers registered on one element
 * often use distinct delegate args types (RoutedEventArgs vs RoutedEventArgs2),
 * so each fire gets its own args object to keep cached RCW types matching. */
static struct uwp_element *g_args_ring[8];
static unsigned int g_args_idx;
static void *g_routed_args(void)
{
    struct uwp_element *el;
    unsigned int idx = g_args_idx;
    g_args_idx = (g_args_idx + 1) % ARRAY_SIZE( g_args_ring );
    el = g_args_ring[idx];
    if (!el)
    {
        void *inst = NULL;
        uwp_element_create( L"Windows.UI.Xaml.RoutedEventArgs", NULL, NULL, &inst );
        el = g_args_ring[idx] = uwp_elem_from_any( inst );
        if (!g_routed_args_el) g_routed_args_el = el;
    }
    if (el && el->outer) return el->outer;
    if (el) return &el->IInspectable_iface;
    return &g_dp_routed_args;
}
#define g_dummy_routed_args g_routed_args()

static void uwp_apply_template_if_needed( struct uwp_element *el )
{
    void *fe_ovr = NULL;
    BOOL has_children = FALSE;
    if (!el || el->template_applied || !el->outer) return;
    el->template_applied = TRUE;
    if (el->parent && !wcsncmp( el->parent->class_name, L"Microsoft.UI.Xaml.", 18 ))
        return;
    if (el->named_cnt > 0)
        has_children = TRUE;
    /* On Windows XAML, OnApplyTemplate() is only called after a ControlTemplate
     * has actually been inflated into the control's visual tree (except WinUI
     * builtin controls like NavigationView/ItemsRepeater that synthesize parts). */
    if (!has_children && wcsncmp( el->class_name, L"Microsoft.UI.Xaml.", 18 ) != 0 &&
        !uwp_is_display_result_element( el ))
        return;
    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IFrameworkElementOverrides_UWP, &fe_ovr )) && fe_ovr)
    {
        /* Slot 8 on IFrameworkElementOverrides is OnApplyTemplate() */
        HRESULT (WINAPI *on_apply)(void *) = (void *)(*(void ***)fe_ovr)[8];
        if (on_apply)
        {
            WARN( "Calling IFrameworkElementOverrides::OnApplyTemplate on %s (%s) named_cnt=%d\n",
                   debugstr_w(el->class_name), debugstr_w(el->name), el->named_cnt );
            __TRY
            {
                on_apply( fe_ovr );
            }
            __EXCEPT_ALL
            {
                WARN( "OnApplyTemplate on %s (%s) threw exception\n",
                      debugstr_w(el->class_name), debugstr_w(el->name) );
            }
            __ENDTRY;
        }
        IUnknown_Release( (IUnknown *)fe_ovr );
    }
    uwp_sync_all_template_presenters( el );
    if (xaml_get_app_caps()->display_sync && uwp_is_display_result_element( el ))
    {
        uwp_calc_vm_ensure();
        uwp_sync_calc_display_from_vm();
    }
}

static const GUID IID_ISizeChangedEventArgs_UWP =
    { 0xd5312e60, 0x5cc1, 0x42a1, { 0x92, 0x0c, 0x1a, 0xf4, 0x6b, 0xe2, 0xf9, 0x86 } };
static const GUID IID_IRoutedEventArgs_sc =
    { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };

static HRESULT WINAPI uwp_sc_args_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_sc ) ||
        IsEqualGUID( iid, &IID_ISizeChangedEventArgs_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_sc_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI uwp_sc_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI uwp_sc_args_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI uwp_sc_args_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.SizeChangedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI uwp_sc_args_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_sc_args_get_PreviousSize( void *iface, Size *sz )
{
    if (sz) { sz->Width = 0.0f; sz->Height = 0.0f; }
    return S_OK;
}
static float g_sc_new_w = 1272.0f, g_sc_new_h = 990.0f;
static HRESULT WINAPI uwp_sc_args_get_NewSize( void *iface, Size *sz )
{
    if (sz) { sz->Width = g_sc_new_w > 0.0f ? g_sc_new_w : 1272.0f; sz->Height = g_sc_new_h > 0.0f ? g_sc_new_h : 990.0f; }
    return S_OK;
}
static const void *const uwp_sc_args_vtbl[] =
{
    uwp_sc_args_QI, uwp_sc_args_AddRef, uwp_sc_args_Release,
    uwp_sc_args_GetIids, uwp_sc_args_GetRuntimeClassName, uwp_sc_args_GetTrustLevel,
    uwp_sc_args_get_PreviousSize,
    uwp_sc_args_get_NewSize,
};
static struct { const void *const *vtbl; } uwp_sc_args_obj = { uwp_sc_args_vtbl };

void uwp_fire_size_and_layout_changed( struct uwp_element *el )
{
    int i;
    float prev_w = 0.0f, prev_h = 0.0f;
    if (!el) return;
    if (el->size_changed_cnt <= 0 && el->layout_updated_cnt <= 0) return;
    if (el->actual_w <= 0.0 || el->actual_h <= 0.0) return;
    if (el->size_changed_cnt > 0 && (el->actual_w != el->last_sc_w || el->actual_h != el->last_sc_h))
    {
        IInspectable * volatile sender = el->outer ? el->outer : &el->IInspectable_iface;
        volatile int cnt = el->size_changed_cnt;
        prev_w = g_sc_new_w;
        prev_h = g_sc_new_h;
        g_sc_new_w = (float)el->actual_w;
        g_sc_new_h = (float)el->actual_h;
        el->last_sc_w = el->actual_w;
        el->last_sc_h = el->actual_h;
        for (i = 0; i < cnt && i < el->size_changed_cnt; i++)
        {
            struct xaml_event_handler * volatile h = (struct xaml_event_handler *)el->size_changed_handlers[i];
            if (h)
            {
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, (IInspectable *)&uwp_sc_args_obj );
                }
                __EXCEPT_ALL
                {
                }
                __ENDTRY;
            }
        }
        g_sc_new_w = prev_w;
        g_sc_new_h = prev_h;
    }
    if (el->layout_updated_cnt > 0)
    {
        int cnt = el->layout_updated_cnt;
        for (i = 0; i < cnt && i < el->layout_updated_cnt; i++)
        {
            if (el->layout_updated_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->layout_updated_handlers[i];
                __TRY
                {
                    h->lpVtbl->Invoke( h, NULL, NULL );
                }
                __EXCEPT_ALL
                {
                }
                __ENDTRY;
            }
        }
    }
    g_sc_new_w = prev_w;
    g_sc_new_h = prev_h;
}

void uwp_fire_loaded_tree( struct uwp_element *el )
{
    int i;
    if (!el) return;

    uwp_apply_template_if_needed( el );

    if (!el->loaded_fired && el->loading_cnt > 0)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        for (i = 0; i < el->loading_cnt; i++)
        {
            if (el->loading_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->loading_handlers[i];
                TRACE( "Firing Loading handler[%d] on %s (%s)\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name) );
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, NULL );
                }
                __EXCEPT_ALL
                {
                    WARN( "Loading handler[%d] on %s (%s) threw\n",
                          i, debugstr_w(el->class_name), debugstr_w(el->name) );
                }
                __ENDTRY;
            }
        }
    }

    el->loaded_fired = TRUE;
    if (el->loaded_fired_cnt < el->loaded_cnt)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        int start = el->loaded_fired_cnt;
        el->loaded_fired_cnt = el->loaded_cnt;
        for (i = start; i < el->loaded_cnt; i++)
        {
            if (el->loaded_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->loaded_handlers[i];
                TRACE( "Firing Loaded handler[%d] on %s (%s)\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name) );
                __TRY
                {
                    HRESULT lhr = h->lpVtbl->Invoke( h, sender, g_routed_args() );
                    if (FAILED(lhr))
                        WARN( "Loaded handler[%d] on %s (%s) returned hr %#lx\n",
                              i, debugstr_w(el->class_name), debugstr_w(el->name), lhr );
                }
                __EXCEPT_ALL
                {
                    struct uwp_element *se = uwp_elem_from_any( sender );
                    WARN( "Loaded handler[%d] on %s (%s) threw exception (sender=%s args=%s)\n",
                          i, debugstr_w(el->class_name), debugstr_w(el->name),
                          se ? debugstr_w( se->class_name ) : "\"?\"",
                          debugstr_w( g_routed_args_el ? g_routed_args_el->class_name : L"?" ) );
                }
                __ENDTRY;
            }
        }
        /* EVC is fired during Arrange (uwp_fire_evc_if_needed) once Measure has run. */
    }
    if (el->size_changed_cnt > 0 && el->actual_w > 0.0 && el->actual_h > 0.0 &&
        (el->actual_w != el->last_sc_w || el->actual_h != el->last_sc_h))
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        float prev_w = g_sc_new_w, prev_h = g_sc_new_h;
        g_sc_new_w = (float)el->actual_w;
        g_sc_new_h = (float)el->actual_h;
        el->last_sc_w = el->actual_w;
        el->last_sc_h = el->actual_h;
        for (i = 0; i < el->size_changed_cnt; i++)
        {
            if (el->size_changed_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->size_changed_handlers[i];
                TRACE( "Firing SizeChanged handler[%d] on %s (%s) w=%.0f h=%.0f\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name), el->actual_w, el->actual_h );
                __TRY
                {
                    h->lpVtbl->Invoke( h, sender, (IInspectable *)&uwp_sc_args_obj );
                }
                __EXCEPT_ALL
                {
                    WARN( "SizeChanged handler[%d] on %s threw exception\n", i, debugstr_w(el->class_name) );
                }
                __ENDTRY;
            }
        }
        g_sc_new_w = prev_w;
        g_sc_new_h = prev_h;
    }
    if (el->layout_updated_cnt > 0)
    {
        int cnt = el->layout_updated_cnt;
        for (i = 0; i < cnt && i < el->layout_updated_cnt; i++)
        {
            if (el->layout_updated_handlers[i])
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)el->layout_updated_handlers[i];
                TRACE( "Firing LayoutUpdated handler[%d] on %s (%s) w=%.0f h=%.0f\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name), el->actual_w, el->actual_h );
                IUnknown_AddRef( (IUnknown *)h );
                __TRY
                {
                    h->lpVtbl->Invoke( h, NULL, NULL );
                }
                __EXCEPT_ALL
                {
                    WARN( "LayoutUpdated handler[%d] on %s threw exception\n", i, debugstr_w(el->class_name) );
                }
                __ENDTRY;
                IUnknown_Release( (IUnknown *)h );
            }
        }
    }

    /* NavigationView raises SelectionChanged/ItemInvoked once after its own
     * Loaded handlers so the app's navigation handler populates the content
     * Frame (ContentFrame1.Navigate in Clock's MainPage) before we recurse. */
    if (!el->nav_fired && el->class_name[0] && wcsstr( el->class_name, L".Controls.NavigationView" ))
    {
        struct uwp_element *item = uwp_nav_find_item( el, 0 );
        IInspectable *item_obj = item ? (item->outer ? item->outer : &item->IInspectable_iface) : NULL;
        el->nav_fired = TRUE;
        uwp_nav_fire( el, &el->nav_sel_changed_reg, item_obj );
        uwp_nav_fire( el, &el->nav_item_invoked_reg, item_obj );
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

void uwp_fire_loaded_content( IInspectable *content )
{
    struct uwp_element *el;
    if (!content) return;
    el = uwp_elem_from_any( content );
    if (el) uwp_fire_loaded_tree( el );
}

static void uwp_seed_synthetic_calculator( struct uwp_element *holder );
void uwp_realize_deferred_entry( struct uwp_defer_entry *de );
void uwp_vsm_invalidate_eval_chain( struct uwp_element *el );
void uwp_realize_empty_panel_defers( struct uwp_element *root )
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

void uwp_record_xbf_ref( struct uwp_element *root, const WCHAR *full_name,
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

void uwp_mark_xbf_ref_instantiated( struct uwp_element *root, const WCHAR *cls_name )
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

BOOL uwp_populate_element_from_pri_xbf( struct uwp_element *el, const WCHAR *short_name )
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
        lstrcpynW( uc->name, xaml_get_app_caps()->seed_page[0] ? xaml_get_app_caps()->seed_page : L"Page", ARRAY_SIZE(uc->name) );
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

struct uwp_element *g_focused_textbox = NULL;

static const GUID IID_INavigationEventArgs_UWP =
    { 0xb6aa9834, 0x6691, 0x44d1, { 0xbd, 0xf7, 0x58, 0x82, 0x0c, 0x27, 0xb0, 0xd0 } };
static const GUID IID_INavigationEventArgs2_UWP =
    { 0xdbff71d9, 0x979a, 0x4b2e, { 0xa4, 0x9b, 0x3b, 0xb1, 0x7f, 0xde, 0xf5, 0x74 } };

struct uwp_nav_event_args
{
    IInspectable INavigationEventArgs_iface;
    IInspectable INavigationEventArgs2_iface;
    LONG ref;
    IInspectable *content;
    IInspectable *parameter;
    IInspectable *transition_info;
    IInspectable *uri;
    TypeName source_page_type;
    INT32 nav_mode;
};

static inline struct uwp_nav_event_args *impl_from_nav_args( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_nav_event_args, INavigationEventArgs_iface );
}
static inline struct uwp_nav_event_args *impl_from_nav_args2( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_nav_event_args, INavigationEventArgs2_iface );
}

static HRESULT WINAPI uwp_nea_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_INavigationEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->INavigationEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_INavigationEventArgs2_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->INavigationEventArgs2_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_nea_AddRef( void *iface ) { return InterlockedIncrement( &impl_from_nav_args( iface )->ref ); }
static ULONG WINAPI uwp_nea_Release( void *iface )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->content) IInspectable_Release( a->content );
        if (a->parameter) IInspectable_Release( a->parameter );
        if (a->transition_info) IInspectable_Release( a->transition_info );
        if (a->uri) IInspectable_Release( a->uri );
        if (a->source_page_type.Name) WindowsDeleteString( a->source_page_type.Name );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_nea_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    IID *out = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!out) return E_OUTOFMEMORY;
    out[0] = IID_INavigationEventArgs_UWP;
    out[1] = IID_INavigationEventArgs2_UWP;
    *cnt = 2;
    *iids = out;
    return S_OK;
}
static HRESULT WINAPI uwp_nea_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Navigation.NavigationEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI uwp_nea_GetTrustLevel( void *iface, TrustLevel *lvl ) { if (lvl) *lvl = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_nea_get_Content( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    *val = a->content;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_Parameter( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    *val = a->parameter;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_SourcePageType( void *iface, TypeName *val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    val->Name = NULL;
    val->Kind = a->source_page_type.Kind;
    if (a->source_page_type.Name)
        WindowsDuplicateString( a->source_page_type.Name, &val->Name );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_NavigationMode( void *iface, INT32 *val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (val) *val = a->nav_mode;
    return S_OK;
}
static HRESULT WINAPI uwp_nea_get_Uri( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (!val) return E_POINTER;
    *val = a->uri;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}
static HRESULT WINAPI uwp_nea_put_Uri( void *iface, IInspectable *val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args( iface );
    if (a->uri) IInspectable_Release( a->uri );
    a->uri = val;
    if (val) IInspectable_AddRef( val );
    return S_OK;
}

static HRESULT WINAPI uwp_nea2_QI( void *iface, REFIID iid, void **out ) { return uwp_nea_QI( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, iid, out ); }
static ULONG WINAPI uwp_nea2_AddRef( void *iface ) { return uwp_nea_AddRef( &impl_from_nav_args2(iface)->INavigationEventArgs_iface ); }
static ULONG WINAPI uwp_nea2_Release( void *iface ) { return uwp_nea_Release( &impl_from_nav_args2(iface)->INavigationEventArgs_iface ); }
static HRESULT WINAPI uwp_nea2_GetIids( void *iface, ULONG *cnt, IID **iids ) { return uwp_nea_GetIids( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, cnt, iids ); }
static HRESULT WINAPI uwp_nea2_GetRuntimeClassName( void *iface, HSTRING *name ) { return uwp_nea_GetRuntimeClassName( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, name ); }
static HRESULT WINAPI uwp_nea2_GetTrustLevel( void *iface, TrustLevel *lvl ) { return uwp_nea_GetTrustLevel( &impl_from_nav_args2(iface)->INavigationEventArgs_iface, lvl ); }
static HRESULT WINAPI uwp_nea2_get_NavigationTransitionInfo( void *iface, IInspectable **val )
{
    struct uwp_nav_event_args *a = impl_from_nav_args2( iface );
    if (!val) return E_POINTER;
    *val = a->transition_info;
    if (*val) IInspectable_AddRef( *val );
    return S_OK;
}

static const void *uwp_nea_vtbl[] =
{
    uwp_nea_QI, uwp_nea_AddRef, uwp_nea_Release,
    uwp_nea_GetIids, uwp_nea_GetRuntimeClassName, uwp_nea_GetTrustLevel,
    uwp_nea_get_Content,
    uwp_nea_get_Parameter,
    uwp_nea_get_SourcePageType,
    uwp_nea_get_NavigationMode,
    uwp_nea_get_Uri,
    uwp_nea_put_Uri,
};
static const void *uwp_nea2_vtbl[] =
{
    uwp_nea2_QI, uwp_nea2_AddRef, uwp_nea2_Release,
    uwp_nea2_GetIids, uwp_nea2_GetRuntimeClassName, uwp_nea2_GetTrustLevel,
    uwp_nea2_get_NavigationTransitionInfo,
};

static struct uwp_nav_event_args *uwp_nav_event_args_create( IInspectable *content, IInspectable *param,
                                                             const TypeName *type, IInspectable *transition_info )
{
    struct uwp_nav_event_args *a = calloc( 1, sizeof(*a) );
    if (!a) return NULL;
    a->INavigationEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nea_vtbl;
    a->INavigationEventArgs2_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nea2_vtbl;
    a->ref = 1;
    a->content = content;
    if (content) IInspectable_AddRef( content );
    a->parameter = param;
    if (param) IInspectable_AddRef( param );
    a->transition_info = transition_info;
    if (transition_info) IInspectable_AddRef( transition_info );
    if (type)
    {
        a->source_page_type.Kind = type->Kind;
        if (type->Name) WindowsDuplicateString( type->Name, &a->source_page_type.Name );
    }
    a->nav_mode = 0; /* NavigationMode_New */
    return a;
}

static HRESULT uwp_frame_navigate_impl( void *iface, void *type_name_ptr, void *param, void *transition_info, boolean *result )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    TypeName *type = (TypeName *)type_name_ptr;
    const WCHAR *name = (type && type->Name) ? WindowsGetStringRawBuffer( type->Name, NULL ) : NULL;

    WARN( "uwp_frame_navigate_impl iface=%p (%s) type=%s param=%p trans=%p\n",
          iface, impl ? debugstr_w(impl->name) : "\"?\"", debugstr_w(name), param, transition_info );
    if (result) *result = TRUE;
    if (!impl || !name) return S_OK;

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
                page_el = g_last_created_page;
                TRACE( "uwp_frame_navigate_impl: RCW page unresolvable, using last PAGE element %p\n", page_el );
            }
            TRACE( "uwp_frame_navigate_impl page_el=%p outer=%p\n", page_el, page_el ? page_el->outer : NULL );
            if (page_el)
            {
                const WCHAR *comma = wcschr( name, ',' );
                size_t len = comma ? (size_t)(comma - name) : wcslen( name );
                IInspectable *new_content;
                if (len && len < ARRAY_SIZE( page_el->class_name ))
                {
                    lstrcpynW( page_el->class_name, name, (int)len + 1 );
                    page_el->class_name[len] = 0;
                }
                new_content = page_el->outer ? page_el->outer : &page_el->IInspectable_iface;
                IInspectable_AddRef( new_content );
                if (impl->content) IInspectable_Release( impl->content );
                impl->content = new_content;
                if (!impl->parent || !g_navigated_page_el)
                    g_navigated_page_el = page_el;
                page_el->parent = impl;

                {
                    struct uwp_nav_event_args *nea = uwp_nav_event_args_create(
                        new_content, (IInspectable *)param, type, (IInspectable *)transition_info );

                    /* Invoke IPageOverrides::OnNavigatedTo (slot 7) if implemented */
                    if (page_el->outer)
                    {
                        void *page_ovr = NULL;
                        HRESULT qi_hr = IInspectable_QueryInterface( page_el->outer, &IID_IPageOverrides_UWP, &page_ovr );
                        TRACE( "uwp_frame_navigate_impl QI IPageOverrides -> hr %#lx ptr %p\n", qi_hr, page_ovr );
                        if (SUCCEEDED(qi_hr) && page_ovr)
                        {
                            HRESULT (WINAPI *on_nav)(void *, void *) = (void *)(*(void ***)page_ovr)[7];
                            HRESULT nav_hr = S_OK;
                            __TRY
                            {
                                nav_hr = on_nav ? on_nav( page_ovr, nea ? (void *)&nea->INavigationEventArgs_iface : g_routed_args() ) : S_OK;
                            }
                            __EXCEPT_ALL { nav_hr = E_FAIL; }
                            __ENDTRY;
                            TRACE( "uwp_frame_navigate_impl OnNavigatedTo -> hr %#lx\n", nav_hr );
                            IUnknown_Release( (IUnknown *)page_ovr );
                        }
                    }

                    /* Fire Frame::Navigated handlers */
                    if (impl->frame_navigated_reg.next_token > 0)
                    {
                        void *handlers[32];
                        unsigned int cnt = xaml_event_registry_snapshot( &impl->frame_navigated_reg, handlers, ARRAY_SIZE(handlers) );
                        IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                        unsigned int hi;
                        for (hi = 0; hi < cnt; hi++)
                        {
                            struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[hi];
                            TRACE( "uwp_frame_navigate_impl firing Navigated[%u] on %s (%s)\n",
                                   hi, debugstr_w(impl->class_name), debugstr_w(impl->name) );
                            __TRY
                            {
                                h->lpVtbl->Invoke( h, sender, nea ? &nea->INavigationEventArgs_iface : g_routed_args() );
                            }
                            __EXCEPT_ALL { WARN( "Frame::Navigated[%u] threw\n", hi ); }
                            __ENDTRY;
                            IUnknown_Release( (IUnknown *)handlers[hi] );
                        }
                    }
                    if (nea) uwp_nea_Release( &nea->INavigationEventArgs_iface );
                }
                uwp_fire_loaded_tree( page_el );

                /* Populate any empty placeholder containers from the page XBF's referenced PRI resources */
                if (!uwp_populate_page_from_xbf_refs( page_el ) && xaml_get_app_caps()->seed_page[0])
                {
                    if (!wcscmp( xaml_get_app_caps()->seed_page, L"Calculator" ))
                    {
                        struct uwp_element *holder = uwp_find_empty_holder_border( page_el, NULL, FALSE );
                        TRACE( "xbf: falling back to seed tree\n" );
                        uwp_seed_synthetic_calculator( holder ? holder : page_el );
                    }
                }
            }
            else
            {
                if (impl->content) IInspectable_Release( impl->content );
                impl->content = page;
            }
            xaml_window_present_current();
        }
    }
    return S_OK;
}

HRESULT WINAPI uwp_frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result )
{
    return uwp_frame_navigate_impl( iface, type_name_ptr, param, NULL, result );
}

/* ============================================================================
 * 6. XBF v2 Stack Parser & Visual Tree Builder
 * ============================================================================ */

const WCHAR *uwp_btype_to_class_name( WORD tid )
{
    WORD idx = tid & 0x7fff;

    if ((tid & 0x8000) && idx < ARRAY_SIZE( xbf_known_types ) && xbf_known_types[idx])
        return xbf_known_types[idx];

    FIXME( "Unknown XBF builtin type %#04x\n", tid );
    return L"Windows.UI.Xaml.Controls.ContentControl";
}

/* Set a custom XAML property (e.g. CalculatorButton.ButtonId = "Zero") via IXamlMetadataProvider */
static struct uwp_element *uwp_hit_test_button( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0) return NULL;

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

    if (el->has_bounds && el->is_enabled &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
         el->click_cnt > 0 || el->command != NULL ||
         wcsstr( el->class_name, L"MenuBarItem" )))
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
    if ((el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON) && el->text[0])
    {
        WCHAR tmp[64];
        const WCHAR *disp = uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) );
        if (!lstrcmpiW( el->text, text ) || (disp && disp[0] && !lstrcmpiW( disp, text )))
            return el;
    }
    return NULL;
}

static BOOL uwp_is_textbox( const struct uwp_element *el )
{
    if (!el || el->visibility != 0 || !el->is_enabled) return FALSE;
    return wcsstr( el->class_name, L"TextBox" ) != NULL ||
           wcsstr( el->class_name, L"AutoSuggestBox" ) != NULL ||
           wcsstr( el->name, L"SearchBox" ) != NULL;
}

static struct uwp_element *uwp_hit_test_textbox( struct uwp_element *el, int x, int y )
{
    if (!el || el->visibility != 0) return NULL;

    if (el->has_bounds &&
        x >= el->bounds.left && x < el->bounds.right &&
        y >= el->bounds.top && y < el->bounds.bottom &&
        uwp_is_textbox( el ))
        return el;

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = uwp_hit_test_textbox( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = uwp_hit_test_textbox( ch, x, y );
        if (hit) return hit;
    }
    return NULL;
}

static void uwp_fire_asb_recursive( struct uwp_element *el, const WCHAR *text, BOOL is_submit )
{
    if (!el) return;
    if (text && el->text != text && (wcsstr( el->class_name, L"AutoSuggestBox" ) || wcsstr( el->class_name, L"TextBox" )))
        lstrcpynW( el->text, text, ARRAY_SIZE(el->text) );
    if (is_submit)
        uwp_asb_fire_query_submitted( el );
    else
        uwp_asb_fire_text_changed( el );
    if (el->content)
        uwp_fire_asb_recursive( uwp_elem_from_any( el->content ), text, is_submit );
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
            uwp_fire_asb_recursive( uwp_elem_from_any( children->items[k] ), text, is_submit );
    }
}

void uwp_keyboard_press_char( WCHAR ch )
{
    WCHAR buf[2] = { ch, 0 };
    struct uwp_element *root = g_navigated_page_el, *hit;

    if (g_focused_textbox && g_focused_textbox->visibility == 0 && g_focused_textbox->is_enabled)
    {
        size_t len = wcslen( g_focused_textbox->text );
        BOOL changed = FALSE;
        TRACE( "uwp_keyboard_press_char: ch=%#x focused=%s cur=%s\n",
               (unsigned)ch, debugstr_w(g_focused_textbox->name), debugstr_w(g_focused_textbox->text) );
        if (ch == L'\b')
        {
            if (len > 0) { g_focused_textbox->text[len - 1] = 0; changed = TRUE; }
        }
        else if (ch == 27)
        {
            if (len > 0) { g_focused_textbox->text[0] = 0; changed = TRUE; }
            else g_focused_textbox = NULL;
        }
        else if (ch == L'\r')
        {
            uwp_fire_asb_recursive( g_focused_textbox, g_focused_textbox->text, TRUE );
        }
        else if (ch >= 0x20 && len + 1 < ARRAY_SIZE(g_focused_textbox->text))
        {
            g_focused_textbox->text[len] = ch;
            g_focused_textbox->text[len + 1] = 0;
            changed = TRUE;
        }
        if (changed && g_focused_textbox)
            uwp_fire_asb_recursive( g_focused_textbox, g_focused_textbox->text, FALSE );
        uwp_invalidate_window();
        xaml_window_present_current();
        return;
    }

    if (!root) return;
    if (ch == L'\r') buf[0] = L'=';
    if ((hit = uwp_find_button_by_text( root, buf ))) uwp_invoke_button( hit );
}

BOOL uwp_elem_is_descendant( struct uwp_element *ancestor, struct uwp_element *node )
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

BOOL uwp_fire_preview_keydown_tree( struct uwp_element *el, struct uwp_key_routed_args *args )
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

BOOL uwp_invoke_accel_entry( const struct uwp_accel_entry *e )
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

static BOOL uwp_fire_tapped_bubble( struct uwp_element *target, int x, int y );

void uwp_invoke_button( struct uwp_element *btn )
{
    int i;
    if (!btn || !btn->is_enabled) return;

    WARN( "uwp_invoke_button: %s (%s, text=%s, cmd=%p, param=%p, dc=%p, clicks=%d, tapped=%d, ptrP=%d, ptrR=%d, dp=%d)\n",
          debugstr_w(btn->class_name), debugstr_w(btn->name), debugstr_w(btn->text),
          btn->command, btn->command_param, btn->data_context, btn->click_cnt,
          (int)btn->tapped_reg.next_token, (int)btn->ptr_pressed_reg.next_token,
          (int)btn->ptr_released_reg.next_token, btn->dp_cnt );
    for (i = 0; i < btn->dp_cnt; i++)
    {
        struct { IDependencyProperty iface; LONG ref; void *meta; WCHAR name[64]; } *dpl = (void *)btn->dp_store[i].dp;
        WARN( "  dp[%d] name=%s val=%p\n", i,
              (dpl && (ULONG_PTR)dpl >= 0x10000) ? debugstr_w(dpl->name) : "?", btn->dp_store[i].value );
    }

    /* 1. Invoke ICommand::Execute(CommandParameter) if Command is bound */
    if (btn->command)
    {
        __TRY
        {
            void *cmd_iface = NULL;
            HRESULT qhr = IInspectable_QueryInterface( btn->command, &IID_ICommand_UWP, &cmd_iface );
            if (FAILED(qhr))
                qhr = IInspectable_QueryInterface( btn->command, &IID_ICommand_UWP2, &cmd_iface );
            if (SUCCEEDED(qhr) && cmd_iface)
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
                HRESULT ehr = E_NOTIMPL;
                if (cmd->lpVtbl->Execute) ehr = cmd->lpVtbl->Execute( cmd, btn->command_param );
                WARN( "uwp_invoke_button: direct Execute qhr=%#lx cmd=%p fn=%p -> hr=%#lx\n",
                      (unsigned long)qhr, cmd, cmd->lpVtbl->Execute, (unsigned long)ehr );
            }
        }
        __EXCEPT_ALL
        {
            WARN( "uwp_invoke_button: ICommand::Execute faulted\n" );
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
                HMODULE mod = NULL;
                void *inv = h->lpVtbl->Invoke;
                HRESULT chr;
                GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)inv, &mod );
                chr = h->lpVtbl->Invoke( h, sender, g_routed_args() );
                WARN( "uwp_invoke_button: Click[%d] h=%p inv=%p (mod=%p +0x%lx) sender=%p -> hr=%#lx\n",
                      i, h, inv, mod, mod ? (unsigned long)((const char *)inv - (const char *)mod) : 0ul,
                      sender, (unsigned long)chr );
            }
            __EXCEPT_ALL
            {
                WARN( "uwp_invoke_button: Click handler %d faulted\n", i );
            }
            __ENDTRY;
        }
    }
    if (btn->tapped_reg.next_token > 0)
        uwp_fire_tapped_bubble( btn, btn->bounds.left + 8, btn->bounds.top + 8 );

    /* 3. Resolve CalculatorButton.ButtonId / CommandParameter dynamically via IXamlMetadataProvider */
    if (xaml_get_app_caps()->vm_type[0])
    {
        int code = uwp_resolve_button_calc_code( btn );
        if (code >= 0)
            uwp_calc_vm_send( code, btn->command_param );
    }

    uwp_invalidate_window();
}

static struct uwp_element *uwp_hit_test_tabview( struct uwp_element *el, int x, int y )
{
    if (!el || !el->has_bounds || el->visibility != 0) return NULL;
    if (wcsstr( el->class_name, L"TabView" ) && !wcsstr( el->class_name, L"TabViewItem" ))
    {
        if (x >= el->bounds.left && x < el->bounds.right &&
            y >= el->bounds.top && y < el->bounds.bottom)
            return el;
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
        {
            struct uwp_element *hit = uwp_hit_test_tabview( ch, x, y );
            if (hit) return hit;
        }
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el)
            {
                struct uwp_element *hit = uwp_hit_test_tabview( ch, x, y );
                if (hit) return hit;
            }
        }
    }
    return NULL;
}

static const GUID IID_ITappedRoutedEventArgs_UWP =
    { 0xafa4bd0f, 0xa05b, 0x4665, { 0x8d, 0x2c, 0xa6, 0xf6, 0x73, 0x67, 0x23, 0x8a } };
static const GUID IID_IRoutedEventArgs_tap_UWP =
    { 0x5c985ac6, 0xd802, 0x4b38, { 0xa2, 0x23, 0xbf, 0x07, 0x0c, 0x43, 0xfe, 0xdf } };

struct uwp_tapped_args
{
    IInspectable ITappedRoutedEventArgs_iface;
    IInspectable IRoutedEventArgs_iface;
    LONG ref;
    IInspectable *orig_source;
    boolean handled;
    float x, y;
};

static inline struct uwp_tapped_args *impl_from_tap( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_tapped_args, ITappedRoutedEventArgs_iface );
}
static inline struct uwp_tapped_args *impl_from_tap_routed( void *iface )
{
    return CONTAINING_RECORD( iface, struct uwp_tapped_args, IRoutedEventArgs_iface );
}
static HRESULT WINAPI uwp_tap_QI( void *iface, REFIID iid, void **out )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ITappedRoutedEventArgs_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->ITappedRoutedEventArgs_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRoutedEventArgs_tap_UWP ))
    {
        InterlockedIncrement( &a->ref );
        *out = &a->IRoutedEventArgs_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_tap_AddRef( void *iface ) { return InterlockedIncrement( &impl_from_tap( iface )->ref ); }
static ULONG WINAPI uwp_tap_Release( void *iface )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    ULONG r = InterlockedDecrement( &a->ref );
    if (!r)
    {
        if (a->orig_source) IInspectable_Release( a->orig_source );
        free( a );
    }
    return r;
}
static HRESULT WINAPI uwp_tap_GetIids( void *iface, ULONG *cnt, IID **iids ) { *cnt = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI uwp_tap_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.UI.Xaml.Input.TappedRoutedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI uwp_tap_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI uwp_tap_get_PointerDeviceType( void *iface, INT32 *v ) { if (v) *v = 2; return S_OK; } /* Mouse */
static HRESULT WINAPI uwp_tap_get_Handled( void *iface, boolean *v ) { if (v) *v = impl_from_tap( iface )->handled; return S_OK; }
static HRESULT WINAPI uwp_tap_put_Handled( void *iface, boolean v ) { impl_from_tap( iface )->handled = v; return S_OK; }
static HRESULT WINAPI uwp_tap_GetPosition( void *iface, void *rel, Point *pt )
{
    struct uwp_tapped_args *a = impl_from_tap( iface );
    if (pt) { pt->X = a->x; pt->Y = a->y; }
    return S_OK;
}
static HRESULT WINAPI uwp_tap_routed_QI( void *iface, REFIID iid, void **out ) { return uwp_tap_QI( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, iid, out ); }
static ULONG WINAPI uwp_tap_routed_AddRef( void *iface ) { return uwp_tap_AddRef( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface ); }
static ULONG WINAPI uwp_tap_routed_Release( void *iface ) { return uwp_tap_Release( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface ); }
static HRESULT WINAPI uwp_tap_routed_GetIids( void *iface, ULONG *c, IID **i ) { return uwp_tap_GetIids( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, c, i ); }
static HRESULT WINAPI uwp_tap_routed_GetRuntimeClassName( void *iface, HSTRING *n ) { return uwp_tap_GetRuntimeClassName( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, n ); }
static HRESULT WINAPI uwp_tap_routed_GetTrustLevel( void *iface, TrustLevel *l ) { return uwp_tap_GetTrustLevel( &impl_from_tap_routed(iface)->ITappedRoutedEventArgs_iface, l ); }
static HRESULT WINAPI uwp_tap_routed_get_OriginalSource( void *iface, IInspectable **out )
{
    struct uwp_tapped_args *a = impl_from_tap_routed( iface );
    if (!out) return E_POINTER;
    *out = a->orig_source;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static const void *uwp_tap_vtbl[] =
{
    uwp_tap_QI, uwp_tap_AddRef, uwp_tap_Release,
    uwp_tap_GetIids, uwp_tap_GetRuntimeClassName, uwp_tap_GetTrustLevel,
    uwp_tap_get_PointerDeviceType, uwp_tap_get_Handled, uwp_tap_put_Handled, uwp_tap_GetPosition,
};
static const void *uwp_tap_routed_vtbl[] =
{
    uwp_tap_routed_QI, uwp_tap_routed_AddRef, uwp_tap_routed_Release,
    uwp_tap_routed_GetIids, uwp_tap_routed_GetRuntimeClassName, uwp_tap_routed_GetTrustLevel,
    uwp_tap_routed_get_OriginalSource,
};

static struct uwp_element *uwp_hit_test_tappable( struct uwp_element *el, int x, int y )
{
    if (!el || !el->has_bounds || el->visibility != 0) return NULL;
    if (x < el->bounds.left || x >= el->bounds.right || y < el->bounds.top || y >= el->bounds.bottom)
        return NULL;
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int k;
        for (k = (int)children->count - 1; k >= 0; k--)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            struct uwp_element *hit = ch && ch != el ? uwp_hit_test_tappable( ch, x, y ) : NULL;
            if (hit) return hit;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        struct uwp_element *hit = ch && ch != el ? uwp_hit_test_tappable( ch, x, y ) : NULL;
        if (hit) return hit;
    }
    if (el->tapped_reg.next_token > 0) return el;
    return NULL;
}

static BOOL uwp_fire_tapped( struct uwp_element *el, int x, int y )
{
    void *handlers[16];
    int count, i;
    struct uwp_tapped_args *args;
    IInspectable *sender;

    if (!el || el->tapped_reg.next_token == 0) return FALSE;
    count = xaml_event_registry_snapshot( &el->tapped_reg, handlers, ARRAY_SIZE(handlers) );
    if (count <= 0) return FALSE;

    sender = el->outer ? el->outer : &el->IInspectable_iface;
    args = calloc( 1, sizeof(*args) );
    if (!args)
    {
        for (i = 0; i < count; i++) IUnknown_Release( (IUnknown *)handlers[i] );
        return FALSE;
    }
    args->ITappedRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_vtbl;
    args->IRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_routed_vtbl;
    args->ref = 1;
    args->orig_source = sender;
    if (sender) IInspectable_AddRef( sender );
    args->x = (float)x;
    args->y = (float)y;

    TRACE( "uwp_fire_tapped on %s (%s / %s) handlers=%d\n",
           debugstr_w(el->class_name), debugstr_w(el->name), debugstr_w(el->text), count );
    for (i = 0; i < count; i++)
    {
        struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
        __TRY
        {
            h->lpVtbl->Invoke( h, sender, &args->ITappedRoutedEventArgs_iface );
        }
        __EXCEPT_ALL {}
        __ENDTRY;
        IUnknown_Release( (IUnknown *)handlers[i] );
    }
    uwp_tap_Release( &args->ITappedRoutedEventArgs_iface );
    return TRUE;
}

static struct uwp_element *g_pressed_btn = NULL;
static struct uwp_element *g_pressed_tabview = NULL;
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
        struct uwp_element *tv = uwp_hit_test_tabview( root_el, x, y );
        struct uwp_element *tb = tv ? NULL : uwp_hit_test_textbox( root_el, x, y );
        struct uwp_element *btn = (tv || tb) ? NULL : uwp_hit_test_button( root_el, x, y );
        if (tv)
        {
            g_pressed_tabview = tv;
            return TRUE;
        }
        if (tb)
        {
            g_focused_textbox = tb;
            TRACE( "uwp_handle_mouse_input: FOCUS TextBox (%d,%d) -> %s\n", x, y, debugstr_w(tb->name) );
            if (hwnd)
            {
                SetFocus( hwnd );
                InvalidateRect( hwnd, NULL, FALSE );
            }
            uwp_invalidate_window();
            return TRUE;
        }
        if (g_focused_textbox)
        {
            g_focused_textbox = NULL;
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
        }
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
            struct xaml_pointer_info info = {0};
            g_pressed_btn = btn;
            btn->is_pressed = TRUE;
            info.x = (float)x;
            info.y = (float)y;
            info.pointer_id = 1;
            info.is_left = TRUE;
            info.is_in_contact = TRUE;
            info.is_in_window = TRUE;
            uwp_fire_element_pointer( btn, XAML_PTR_PRESSED, &info, TRUE );
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
            uwp_invalidate_window();
            return TRUE;
        }
        return FALSE;
    }
    else if (msg == WM_LBUTTONUP)
    {
        if (g_pressed_tabview)
        {
            struct uwp_element *tv = g_pressed_tabview;
            struct uwp_element *hit = uwp_hit_test_tabview( root_el, x, y );
            g_pressed_tabview = NULL;
            if (hit == tv)
            {
                IInspectable *items[64];
                int count = uwp_tabview_get_items( tv, items, ARRAY_SIZE(items) );
                int n = max( 1, count ), i;
                POINT pt = { x, y };
                RECT pill, close_rc, add_rc;
                uwp_tabview_compute_rects( &tv->bounds, n, 0, &pill, &close_rc, &add_rc );
                if (PtInRect( &add_rc, pt ))
                {
                    uwp_tabview_fire_add_tab( tv );
                }
                else
                {
                    for (i = 0; i < count; i++)
                    {
                        uwp_tabview_compute_rects( &tv->bounds, n, i, &pill, &close_rc, &add_rc );
                        if (PtInRect( &close_rc, pt ))
                        {
                            uwp_tabview_fire_close_tab( tv, items[i] );
                            break;
                        }
                        if (PtInRect( &pill, pt ))
                        {
                            uwp_tabview_select_item( tv, items[i] );
                            break;
                        }
                    }
                }
                for (i = 0; i < count; i++)
                    IInspectable_Release( items[i] );
            }
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        if (g_pressed_btn)
        {
            struct uwp_element *btn = g_pressed_btn;
            struct uwp_element *hit = uwp_hit_test_button( root_el, x, y );
            btn->is_pressed = FALSE;
            g_pressed_btn = NULL;
            WARN( "uwp_handle_mouse_input: UP (%d,%d) pressed=%p(%s) hit=%p(%s)\n",
                  x, y, btn, debugstr_w(btn->class_name), hit, hit ? debugstr_w(hit->class_name) : "(null)" );
            if (hit == btn)
            {
                struct xaml_pointer_info info = {0};
                info.x = (float)x;
                info.y = (float)y;
                info.pointer_id = 1;
                info.is_left = TRUE;
                info.is_in_contact = FALSE;
                info.is_in_window = TRUE;
                uwp_fire_element_pointer( btn, XAML_PTR_RELEASED, &info, TRUE );
                uwp_invoke_button( btn );
            }
            else
                uwp_fire_tapped_bubble( btn, x, y );
            if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
            uwp_invalidate_window();
            return TRUE;
        }
        {
            struct uwp_element *tap = uwp_hit_test_tappable( root_el, x, y );
            if (tap && uwp_fire_tapped( tap, x, y ))
            {
                if (hwnd) InvalidateRect( hwnd, NULL, FALSE );
                uwp_invalidate_window();
                return TRUE;
            }
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

static BOOL uwp_fire_tapped_bubble( struct uwp_element *target, int x, int y )
{
    struct uwp_tapped_args *args;
    struct uwp_element *cur;
    IInspectable *orig_sender;
    BOOL fired = FALSE;

    if (!target) return FALSE;
    WARN( "uwp_fire_tapped_bubble: target=%s (%s) txt=%s (%d,%d)\n",
          debugstr_w(target->class_name), debugstr_w(target->name), debugstr_w(target->text), x, y );
    orig_sender = target->outer ? target->outer : &target->IInspectable_iface;
    args = calloc( 1, sizeof(*args) );
    if (!args) return FALSE;
    args->ITappedRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_vtbl;
    args->IRoutedEventArgs_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_tap_routed_vtbl;
    args->ref = 1;
    args->orig_source = orig_sender;
    if (orig_sender) IInspectable_AddRef( orig_sender );
    args->x = (float)x;
    args->y = (float)y;

    for (cur = target; cur && !args->handled; cur = cur->parent)
    {
        void *handlers[16];
        int count, i;
        IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;

        if (cur->outer)
        {
            void *ctrl_ovr = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( cur->outer, &IID_IControlOverrides_UWP, &ctrl_ovr )) && ctrl_ovr)
            {
                HRESULT (WINAPI *on_tapped)(void *, void *) = (void *)(*(void ***)ctrl_ovr)[14];
                if (on_tapped)
                {
                    WARN( "uwp_fire_tapped_bubble: IControlOverrides::OnTapped on %s (%s) txt=%s\n",
                          debugstr_w(cur->class_name), debugstr_w(cur->name), debugstr_w(cur->text) );
                    __TRY { on_tapped( ctrl_ovr, &args->ITappedRoutedEventArgs_iface ); fired = TRUE; }
                    __EXCEPT_ALL {}
                    __ENDTRY;
                }
                IUnknown_Release( (IUnknown *)ctrl_ovr );
            }
        }
        if (cur->tapped_reg.next_token > 0)
        {
            count = xaml_event_registry_snapshot( &cur->tapped_reg, handlers, ARRAY_SIZE(handlers) );
            for (i = 0; i < count; i++)
            {
                struct xaml_event_handler *h = (struct xaml_event_handler *)handlers[i];
                HRESULT thr = S_OK;
                WARN( "uwp_fire_tapped_bubble: invoking tapped_reg[%d/%d] on %s (%s) txt=%s handled=%d\n",
                      i, count, debugstr_w(cur->class_name), debugstr_w(cur->name), debugstr_w(cur->text), args->handled );
                g_in_tap_diag = TRUE;
                __TRY { thr = h->lpVtbl->Invoke( h, sender, &args->ITappedRoutedEventArgs_iface ); fired = TRUE; }
                __EXCEPT_ALL { WARN( "uwp_fire_tapped_bubble: tapped_reg[%d] threw!\n", i ); }
                __ENDTRY;
                g_in_tap_diag = FALSE;
                WARN( "uwp_fire_tapped_bubble: after tapped_reg[%d] hr=%#lx handled=%d\n", i, thr, args->handled );
                IUnknown_Release( (IUnknown *)handlers[i] );
            }
        }
    }
    uwp_tap_Release( &args->ITappedRoutedEventArgs_iface );
    return fired;
}

static BOOL uwp_fire_element_pointer( struct uwp_element *target, enum xaml_pointer_event_kind kind,
                                      const struct xaml_pointer_info *info, BOOL bubble )
{
    struct uwp_pointer_routed_args *args;
    struct uwp_element *cur;
    BOOL fired = FALSE;
    int ovr_slot = -1;

    if (!target) return FALSE;
    switch (kind)
    {
    case XAML_PTR_ENTERED:      ovr_slot = 6;  break;
    case XAML_PTR_PRESSED:      ovr_slot = 7;  break;
    case XAML_PTR_MOVED:        ovr_slot = 8;  break;
    case XAML_PTR_RELEASED:     ovr_slot = 9;  break;
    case XAML_PTR_EXITED:       ovr_slot = 10; break;
    case XAML_PTR_CAPTURE_LOST: ovr_slot = 11; break;
    case XAML_PTR_WHEEL:        ovr_slot = 13; break;
    default:                    ovr_slot = -1; break;
    }

    args = uwp_pointer_routed_args_create( info, target->outer ? target->outer : &target->IInspectable_iface );
    if (!args) return FALSE;

    for (cur = target; cur && !args->handled; cur = bubble ? cur->parent : NULL)
    {
        struct xaml_event_registry *reg = uwp_elem_ptr_registry( cur, kind );
        void *handlers[64];
        unsigned int i, count = reg ? xaml_event_registry_snapshot( reg, handlers, ARRAY_SIZE( handlers ) ) : 0;
        IInspectable *sender = cur->outer ? cur->outer : &cur->IInspectable_iface;

        if (ovr_slot >= 0 && cur->outer)
        {
            void *ctrl_ovr = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( cur->outer, &IID_IControlOverrides_UWP, &ctrl_ovr )) && ctrl_ovr)
            {
                HRESULT (WINAPI *on_ptr)(void *, void *) = (void *)(*(void ***)ctrl_ovr)[ovr_slot];
                if (on_ptr)
                {
                    __TRY
                    {
                        on_ptr( ctrl_ovr, &args->IPointerRoutedEventArgs_vtbl );
                        fired = TRUE;
                    }
                    __EXCEPT_ALL
                    {
                        TRACE( "uwp_fire_element_pointer: IControlOverrides[%d] faulted on %s\n",
                               ovr_slot, debugstr_w(cur->class_name) );
                    }
                    __ENDTRY;
                }
                IUnknown_Release( (IUnknown *)ctrl_ovr );
            }
        }

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
        if (uwp_fire_tapped_bubble( target, (int)info->x, (int)info->y ))
            fired = TRUE;
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

    if (memcmp( raw, "XBF\0", 4 ) && wcsstr( raw, L"SolidColorBrush" ))
    {
        if (SUCCEEDED(element_create_typed( L"Microsoft.UI.Xaml.Media.SolidColorBrush", (void **)&root )) && root)
            *out = root;
        return S_OK;
    }

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
    WCHAR str_buf[512] = {0};
    BOOL has_str = FALSE;
    const WCHAR *tname = (type && type->Name) ? WindowsGetStringRawBuffer( type->Name, NULL ) : NULL;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!val) return S_OK;

    if (tname && tname[0])
    {
        has_str = uwp_unbox_string( val, str_buf, ARRAY_SIZE(str_buf) );
        if (!wcscmp( tname, L"Windows.Foundation.Uri" ) && has_str && str_buf[0])
        {
            static const GUID IID_IUriFactory_local =
                { 0x44a9796f, 0x723e, 0x4fdf, { 0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84 } };
            HSTRING h_cls = NULL, h_uri = NULL;
            void *ufac = NULL;
            if (SUCCEEDED(WindowsCreateString( L"Windows.Foundation.Uri", 22, &h_cls )))
            {
                if (SUCCEEDED(RoGetActivationFactory( h_cls, &IID_IUriFactory_local, &ufac )) && ufac)
                {
                    HRESULT (WINAPI *create_uri)(void *, HSTRING, void **) = (void *)(*(void ***)ufac)[6];
                    if (create_uri && SUCCEEDED(WindowsCreateString( str_buf, wcslen(str_buf), &h_uri )))
                    {
                        create_uri( ufac, h_uri, (void **)out );
                        WindowsDeleteString( h_uri );
                    }
                    IUnknown_Release( (IUnknown *)ufac );
                }
                WindowsDeleteString( h_cls );
            }
            if (*out) return S_OK;
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.Media.ImageSource" ) ||
             !wcscmp( tname, L"Windows.UI.Xaml.Media.Imaging.BitmapImage" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.ImageSource" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.Imaging.BitmapImage" )) && has_str)
        {
            void *bi = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Media.Imaging.BitmapImage", NULL, NULL, &bi )) && bi)
            {
                struct uwp_element *bi_el = uwp_elem_from_any( bi );
                if (bi_el && str_buf[0])
                    lstrcpynW( bi_el->image_src, str_buf, ARRAY_SIZE(bi_el->image_src) );
                *out = (IInspectable *)bi;
                return S_OK;
            }
        }
        if ((!wcscmp( tname, L"Windows.UI.Xaml.Media.Brush" ) ||
             !wcscmp( tname, L"Windows.UI.Xaml.Media.SolidColorBrush" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.Brush" ) ||
             !wcscmp( tname, L"Microsoft.UI.Xaml.Media.SolidColorBrush" )) && has_str)
        {
            void *br = NULL;
            if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Media.SolidColorBrush", NULL, NULL, &br )) && br)
            {
                struct uwp_element *br_el = uwp_elem_from_any( br );
                UINT32 argb = 0;
                if (br_el && str_buf[0] && uwp_parse_color_string( str_buf, &argb ))
                {
                    br_el->brush_argb = argb;
                    br_el->has_brush_argb = TRUE;
                    br_el->brush_opacity = (double)((argb >> 24) & 0xff) / 255.0;
                }
                *out = (IInspectable *)br;
                return S_OK;
            }
        }
        /* For non-primitive reference types (not String/Object/IInspectable), never return
         * an unconverted IPropertyValue — CoreRT casts it immediately and failfasts. */
        if (type->Kind != 0 &&
            wcscmp( tname, L"String" ) && wcscmp( tname, L"System.String" ) &&
            wcscmp( tname, L"Object" ) && wcscmp( tname, L"System.Object" ) &&
            wcscmp( tname, L"IInspectable" ))
        {
            IPropertyValue *pv = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( val, &IID_IPropertyValue_g, (void **)&pv )) && pv)
            {
                IPropertyValue_Release( pv );
                *out = NULL;
                return S_OK;
            }
        }
    }

    *out = val;
    IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI xbh_get_DataTemplateComponentProperty( void *iface, void **out )
{
    static IDependencyProperty *dp = NULL;
    if (!out) return E_POINTER;
    if (!dp) dependency_property_create( NULL, &dp );
    *out = dp;
    if (dp) IDependencyProperty_AddRef( dp );
    return S_OK;
}
static HRESULT WINAPI xbh_GetDataTemplateComponent( void *iface, void *el, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI xbh_QI( void *iface, REFIID iid, void **out );
static ULONG WINAPI xbh_AddRef( void *iface ) { return 2; }
static ULONG WINAPI xbh_Release( void *iface ) { return 1; }

static const void *xbh_statics_vtbl[32] =
{
    [0] = xbh_QI,
    [1] = xbh_AddRef,
    [2] = xbh_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = xbh_get_DataTemplateComponentProperty,
    [7] = xbh_GetDataTemplateComponent,
    [8 ... 10] = uwp_generic_noop,
    [11] = xbh_ConvertValue, /* slot 11: ConvertValue(TypeName, object, object**) */
    [12 ... 31] = uwp_generic_noop,
};
static struct xbh_statics_obj_s { const void *vtbl; } xbh_statics_obj = { xbh_statics_vtbl };

static const void *xbh_vtbl[32] =
{
    [0] = xbh_QI,
    [1] = xbh_AddRef,
    [2] = xbh_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = xaml_reader_Load,  /* IXamlReaderStatics::Load(HSTRING, object**) */
    [7] = xaml_reader_Load,  /* IXamlReaderStatics::LoadWithInitialTemplateValidation(HSTRING, object**) */
    [8 ... 10] = uwp_generic_noop,
    [11] = xbh_ConvertValue, /* slot 11: ConvertValue(TypeName, object, object**) */
    [12 ... 31] = uwp_generic_noop,
};
struct xbh_factory_obj_s { const void *vtbl; } xbh_factory_obj = { xbh_vtbl };

static HRESULT WINAPI xbh_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IXamlReaderStatics_WinUI3 =
        { 0x82a4cd9e, 0x435e, 0x5aeb, { 0x8c, 0x4f, 0x30, 0x0c, 0xec, 0xe4, 0x5c, 0xae } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IXamlBindingHelperStatics_UWP ))
    {
        *out = &xbh_statics_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IXamlReaderStatics_UWP ) ||
        IsEqualGUID( iid, &IID_IXamlReaderStatics_WinUI3 ))
    {
        *out = &xbh_factory_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

/* Universal UWP Composable / Statics Factory for stock XAML classes */
const GUID IID_IFrameFactory_UWP =
    { 0x02ee93d4, 0x448e, 0x469e, { 0x97, 0x99, 0x0a, 0x8a, 0x1f, 0x70, 0xf1, 0x71 } };
const GUID IID_IRoutedEventArgsFactory_UWP =
    { 0x7c820996, 0x475c, 0x4d3c, { 0x98, 0x12, 0x0b, 0x4a, 0x1d, 0xf2, 0x3b, 0x52 } };
const GUID IID_IReferenceTracker_UWP =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
const GUID IID_IReferenceTrackerTarget_UWP =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
const GUID IID_IReferenceTrackerTarget_UWP2 =
    { 0x64e71ddf, 0x2424, 0x4ec5, { 0x86, 0x27, 0x15, 0x05, 0x08, 0x96, 0xd9, 0xb6 } };

const GUID IID_IVisualStateManagerFactory_UWP =
    { 0x3976547f, 0x7089, 0x4801, { 0x8f, 0x1d, 0xaa, 0xb7, 0x80, 0x90, 0xd1, 0xa0 } };
const GUID IID_IVisualTreeHelperStatics_UWP =
    { 0xe75758c4, 0xd25d, 0x4b1d, { 0x97, 0x1f, 0x59, 0x6f, 0x17, 0xf1, 0x2b, 0xaa } };


struct uwp_class_factory g_dyn_factories[256];
static WCHAR g_dyn_factory_names[256][128];
int g_dyn_factory_cnt = 0;
SRWLOCK g_dyn_factories_lock = SRWLOCK_INIT;

static UINT32 xaml_factory_hash_fnv( const WCHAR *name )
{
    UINT32 h = 2166136261u;   /* FNV-1a 32-bit offset basis */
    for (; *name; name++)
    {
        h ^= (UINT32)*name;
        h *= 16777619u;       /* FNV-1a prime */
    }
    return h;
}

static BOOL WINAPI init_xaml_factory_hash( INIT_ONCE *once, void *param, void **ctx )
{
    size_t i;
    for (i = 0; i < xaml_static_factories_count; i++)
    {
        const struct xaml_static_factory *e = &xaml_static_factories[i];
        UINT32 h = xaml_factory_hash_fnv( e->name ) & (XAML_FACTORY_HASH_SIZE - 1);
        while (xaml_factory_hash[h])   /* linear probe; first insert wins */
            h = (h + 1) & (XAML_FACTORY_HASH_SIZE - 1);
        xaml_factory_hash[h] = e;
    }
    return TRUE;
}

/* O(1) probe of the built index.  Returns the singleton factory for a known static
 * class name, NULL when the class is not in the table (the caller then falls through to
 * the dynamic per-class-name factory). */
static IActivationFactory *xaml_lookup_static_factory( const WCHAR *name )
{
    const struct xaml_static_factory *e;
    UINT32 h;

    InitOnceExecuteOnce( &xaml_factory_hash_once, init_xaml_factory_hash, NULL, NULL );
    h = xaml_factory_hash_fnv( name ) & (XAML_FACTORY_HASH_SIZE - 1);
    while ((e = xaml_factory_hash[h]))
    {
        if (!wcscmp( e->name, name )) return e->factory;
        h = (h + 1) & (XAML_FACTORY_HASH_SIZE - 1);
    }
    return NULL;
}

HRESULT uwp_get_activation_factory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buf = WindowsGetStringRawBuffer( classid, NULL );
    int i;

    if (!buf || !factory) return E_NOINTERFACE;

    /* Fixed singleton factories resolved through the FNV-1a index (one hash probe) in
     * place of the old linear wcscmp chain.  A miss falls through to the dynamic
     * per-class-name factory below. */
    {
        IActivationFactory *sf = xaml_lookup_static_factory( buf );
        if (sf)
        {
            *factory = sf;
            return S_OK;
        }
    }

    /* For UWP apps (Calculator, Store), serve all Windows.UI.Xaml.* and Microsoft.UI.Xaml.* runtime classes with our UWP multi-iface element factory;
     * also serve core Windows.UI.Xaml.Controls panels/buttons outside WhatsApp so UWP controls use the layout/pointer engine. */
    if ((!xaml_get_app_caps()->winui3 ||
         (!wcscmp( buf, L"Windows.UI.Xaml.Controls.Grid" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.StackPanel" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Panel" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Border" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Canvas" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.Button" ) ||
           !wcscmp( buf, L"Windows.UI.Xaml.Controls.TextBlock" ) ||
           !wcscmp( buf, L"CalculatorApp.Controls.CalculationResult" ))) &&
        (!wcsncmp( buf, L"Windows.UI.Xaml.", 16 ) || !wcsncmp( buf, L"Microsoft.UI.Xaml.", 18 ) ||
         !wcscmp( buf, L"CalculatorApp.Controls.CalculationResult" )) &&
        wcscmp( buf, L"Windows.UI.Xaml.Application" ) != 0 &&
        wcscmp( buf, L"Microsoft.UI.Xaml.Application" ) != 0 &&
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
        static int g_dyn_hash_next[256];
        static int g_dyn_hash_head[512];
        static BOOL g_dyn_hash_init = FALSE;
        UINT32 bucket = xaml_factory_hash_fnv( buf ) & 511;

        AcquireSRWLockShared( &g_dyn_factories_lock );
        if (g_dyn_hash_init)
        {
            for (i = g_dyn_hash_head[bucket]; i >= 0; i = g_dyn_hash_next[i])
            {
                if (!wcscmp( g_dyn_factory_names[i], buf ))
                {
                    *factory = &g_dyn_factories[i].IActivationFactory_iface;
                    ReleaseSRWLockShared( &g_dyn_factories_lock );
                    return S_OK;
                }
            }
        }
        ReleaseSRWLockShared( &g_dyn_factories_lock );

        AcquireSRWLockExclusive( &g_dyn_factories_lock );
        if (!g_dyn_hash_init)
        {
            memset( g_dyn_hash_head, -1, sizeof(g_dyn_hash_head) );
            for (i = 0; i < g_dyn_factory_cnt; i++)
            {
                UINT32 b = xaml_factory_hash_fnv( g_dyn_factory_names[i] ) & 511;
                g_dyn_hash_next[i] = g_dyn_hash_head[b];
                g_dyn_hash_head[b] = i;
            }
            g_dyn_hash_init = TRUE;
        }
        for (i = g_dyn_hash_head[bucket]; i >= 0; i = g_dyn_hash_next[i])
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
            g_dyn_factories[idx].IBitmapImageFactory_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_bitmap_image_factory_vtbl;
            g_dyn_factories[idx].class_name = g_dyn_factory_names[idx];
            g_dyn_hash_next[idx] = g_dyn_hash_head[bucket];
            g_dyn_hash_head[bucket] = idx;
            *factory = &g_dyn_factories[idx].IActivationFactory_iface;
            ReleaseSRWLockExclusive( &g_dyn_factories_lock );
            return S_OK;
        }
        ReleaseSRWLockExclusive( &g_dyn_factories_lock );
    }

    return E_NOINTERFACE;
}





const GUID IID_IXamlType_WinUI3_g =
    { 0xd24219df, 0x7ec9, 0x57f1, { 0xa2, 0x7b, 0x6a, 0xf2, 0x51, 0xd9, 0xc5, 0xbc } };
const void *dp_changed_args_vtbl[] =
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
HRESULT WINAPI dp_args_QI( void *iface, REFIID iid, void **out )
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
ULONG WINAPI dp_args_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct uwp_dp_changed_args *)iface)->ref );
}
ULONG WINAPI dp_args_Release( void *iface )
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
HRESULT WINAPI dp_args_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
HRESULT WINAPI dp_args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    const WCHAR *cls = L"Windows.UI.Xaml.DependencyPropertyChangedEventArgs";
    return WindowsCreateString( cls, wcslen(cls), name );
}
HRESULT WINAPI dp_args_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
HRESULT WINAPI dp_args_get_Property( void *iface, void **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->dp;
    if (*out) IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}
HRESULT WINAPI dp_args_get_OldValue( void *iface, IInspectable **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->old_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
HRESULT WINAPI dp_args_get_NewValue( void *iface, IInspectable **out )
{
    struct uwp_dp_changed_args *a = iface;
    if (!out) return E_POINTER;
    *out = a->new_val;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
SRWLOCK xaml_meta_cache_lock = SRWLOCK_INIT;
INIT_ONCE xaml_meta_cache_once = INIT_ONCE_STATIC_INIT;
struct xaml_meta_cache_entry *xaml_meta_cache[XAML_META_CACHE_SIZE];
struct uwp_calc_vm g_calc_vm;
BOOL uwp_get_custom_xaml_member( void *app_outer, const WCHAR *owner_type_name,
                                        const WCHAR *prop_name, void *target_obj, IInspectable **out_val )
{
    void *provider = NULL, *xaml_type = NULL, *member = NULL;
    HSTRING h_prop = NULL;
    BOOL ok = FALSE;
    if (!out_val) return FALSE;
    *out_val = NULL;
    if (!app_outer || !owner_type_name || !owner_type_name[0] || !prop_name || !target_obj) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)app_outer, &IID_IXamlMetadataProvider_UWP_g, &provider )) || !provider)
        return FALSE;
    xaml_type = uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, owner_type_name );
    if (xaml_type)
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
    IInspectable_Release( (IInspectable *)provider );
    return ok;
}
