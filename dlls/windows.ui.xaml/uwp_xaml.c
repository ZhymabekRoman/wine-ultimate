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
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

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
    { 0xb9ee93fe, 0xa338, 0x419f, { 0xac, 0x32, 0x91, 0xdc, 0x12, 0x5d, 0x08, 0x41 } };
static const GUID IID_IUIElementStatics_UWP =
    { 0x58d3573b, 0xf52c, 0x45be, { 0x98, 0x8b, 0xa5, 0x86, 0x95, 0x64, 0x87, 0x3c } };
static const GUID IID_IFrameworkElement_UWP =
    { 0xa391d09b, 0x4a99, 0x4b7c, { 0x9d, 0x8d, 0x6f, 0xa5, 0xd0, 0x1f, 0x6f, 0xbf } };
static const GUID IID_IFrameworkElement2_UWP =
    { 0xf19104be, 0x422a, 0x4904, { 0xa5, 0x2f, 0xde, 0x72, 0x01, 0x04, 0x29, 0xe0 } };
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
    { 0xb01602b8, 0x3370, 0x422a, { 0x94, 0x9c, 0x59, 0x9d, 0x26, 0x17, 0x6f, 0x2e } };
static const GUID IID_IContentControl_UWP =
    { 0xa26dd1dc, 0xcd44, 0x435c, { 0xbe, 0x94, 0x01, 0xd6, 0x24, 0x1c, 0x23, 0x1c } };
static const GUID IID_IContentControlOverrides_UWP =
    { 0x3874208b, 0x57d6, 0x4697, { 0x9f, 0x01, 0xf6, 0xf6, 0x64, 0x23, 0xa2, 0x2b } };
static const GUID IID_IContentControlFactory_UWP =
    { 0xab51a1b5, 0x1e98, 0x4892, { 0x82, 0x2c, 0x25, 0x97, 0x58, 0x22, 0x0b, 0x35 } };
static const GUID IID_IUserControl_UWP =
    { 0xa7a69ec9, 0xea35, 0x4679, { 0xbf, 0x29, 0xf4, 0xf0, 0x92, 0x86, 0xd3, 0x14 } };
static const GUID IID_IUserControlFactory_UWP =
    { 0xb12b1d5a, 0x6419, 0x4e16, { 0xb2, 0x12, 0xa4, 0x53, 0x36, 0xb7, 0x57, 0x78 } };
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
    { 0x38534690, 0x802a, 0x46c3, { 0x8e, 0x02, 0x7e, 0xf2, 0x8c, 0x3a, 0x0e, 0x4c } };
static const GUID IID_IButton_UWP =
    { 0x258c3f1c, 0xf6c2, 0x40e7, { 0x89, 0xbe, 0x28, 0xd8, 0xdc, 0x42, 0x00, 0x84 } };
static const GUID IID_IButtonWithFlyout_UWP =
    { 0x06e32cce, 0xdfdd, 0x3639, { 0x94, 0xa5, 0xc7, 0xff, 0x91, 0x19, 0x93, 0xf8 } };
static const GUID IID_IButtonFactory_UWP =
    { 0x408669b7, 0x2d24, 0x4212, { 0xa9, 0x63, 0x2a, 0x16, 0x1e, 0x6f, 0x03, 0x37 } };
static const GUID IID_IToggleButton_UWP =
    { 0x589877fb, 0x0fc7, 0x4036, { 0x9d, 0x8b, 0x12, 0x7d, 0xfa, 0x75, 0xc1, 0x6d } };
static const GUID IID_IToggleButtonOverrides_UWP =
    { 0x84e5b964, 0x4d3d, 0x49c1, { 0xab, 0xe7, 0xcb, 0xd3, 0xc5, 0x77, 0x49, 0x0d } };
static const GUID IID_IToggleButtonFactory_UWP =
    { 0x80a13c19, 0x843a, 0x451c, { 0x8c, 0xf5, 0x44, 0xc7, 0x01, 0xb0, 0xe2, 0x16 } };
static const GUID IID_IToggleButtonStatics_UWP =
    { 0xaf1eab12, 0x0128, 0x4f67, { 0x9c, 0x5a, 0x82, 0x32, 0x0c, 0x44, 0x5d, 0x19 } };
static const GUID IID_IHyperlinkButton_UWP =
    { 0x325c44e0, 0x9a03, 0x4bf3, { 0xab, 0xd6, 0x6f, 0xbb, 0x46, 0xc9, 0xa4, 0x86 } };
static const GUID IID_IPanel_UWP =
    { 0xa50a4bbd, 0x8361, 0x469c, { 0x90, 0xda, 0xe9, 0xa4, 0x0c, 0x74, 0x74, 0xdf } };
static const GUID IID_IPanelFactory_UWP =
    { 0x26d06b5c, 0x0c5e, 0x4e42, { 0xb5, 0x34, 0x08, 0x35, 0x01, 0x59, 0x68, 0x6b } };
static const GUID IID_IGrid_UWP =
    { 0x709133c7, 0x38f3, 0x5083, { 0xa4, 0xbb, 0x06, 0xa5, 0x48, 0xa1, 0xd2, 0x31 } };
static const GUID IID_IGrid_UWP2 =
    { 0xdf104460, 0x2e15, 0x4ba3, { 0x8b, 0x8f, 0xfa, 0x69, 0x3a, 0x41, 0x61, 0xe9 } };
static const GUID IID_IGridStatics_UWP =
    { 0xed02d274, 0x81f1, 0x5ac2, { 0xa0, 0xc3, 0xbb, 0xda, 0xf5, 0xa3, 0xff, 0xee } };
static const GUID IID_IGridStatics_UWP2 =
    { 0x1366e46e, 0x1e3c, 0x4765, { 0x85, 0x75, 0x98, 0xc3, 0x8b, 0x6b, 0x9e, 0x52 } };
static const GUID IID_IStackPanel_UWP =
    { 0x2a7993c3, 0x4230, 0x4132, { 0x97, 0x92, 0x19, 0x28, 0xd1, 0x41, 0x6b, 0x0f } };
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
    { 0x6429200e, 0x2856, 0x475e, { 0x9e, 0x00, 0x52, 0x10, 0x1b, 0x92, 0x10, 0x0e } };
static const GUID IID_IScrollViewer2_UWP =
    { 0x4b12000e, 0x2856, 0x475e, { 0x9e, 0x00, 0x52, 0x10, 0x1b, 0x92, 0x10, 0x02 } };
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
    { 0x9d8c52c8, 0x853a, 0x41b3, { 0x96, 0x92, 0x72, 0xd6, 0xe5, 0x64, 0x1a, 0x64 } };
static const GUID IID_IFlyoutBase_UWP =
    { 0x723eea0b, 0xd12e, 0x430d, { 0xa9, 0xf0, 0x9b, 0xb3, 0x2b, 0xbf, 0x99, 0x13 } };
static const GUID IID_IMenuFlyout_UWP =
    { 0x62081062, 0xb44a, 0x4210, { 0x91, 0xa8, 0x16, 0x75, 0x15, 0x25, 0x1a, 0x12 } };
static const GUID IID_IMenuFlyoutItem_UWP =
    { 0x160775cf, 0xb598, 0x44af, { 0xa4, 0x88, 0xc3, 0xce, 0x2a, 0xf6, 0xd3, 0xf0 } };
static const GUID IID_IMenuFlyoutItemBase_UWP =
    { 0x5c36c019, 0x3058, 0x427b, { 0x9e, 0x82, 0x82, 0x98, 0x1b, 0x3b, 0x3f, 0x19 } };
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
    { 0x65329c08, 0x5254, 0x42b7, { 0x9c, 0x5e, 0x42, 0x20, 0x59, 0x12, 0x12, 0x24 } };
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
static const GUID IID_IPropertyValue_g =
    { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };
static const GUID IID_IPropertyValueStatics_g =
    { 0x629bdbc8, 0xd932, 0x4ff4, { 0x96, 0xb9, 0x8d, 0x96, 0xc5, 0xc1, 0xe8, 0x58 } };
static const GUID IID_IUriRuntimeClass_g =
    { 0x9e365e57, 0x48b2, 0x4160, { 0x95, 0x6f, 0xc7, 0x38, 0x51, 0x20, 0xbb, 0xfc } };

/* CalculatorApp / MCG additional discovered interface IIDs */
static const GUID IID_IGrid_Calc =
    { 0x280335ae, 0x5570, 0x46c7, { 0x8e, 0x0b, 0x60, 0x2b, 0xe7, 0x12, 0x29, 0xa2 } };
static const GUID IID_IUIElement_Calc2 =
    { 0x33d1299d, 0x8d6e, 0x4290, { 0xbf, 0x22, 0x90, 0x5c, 0xcc, 0xd0, 0x4d, 0x31 } };

/* ============================================================================
 * 1. PRI v2 (mrm_pri2) Container Reader
 * ============================================================================ */

static const BYTE *g_pri_data = NULL;
static DWORD g_pri_size = 0;
static INIT_ONCE g_pri_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI init_pri_once( INIT_ONCE *once, void *param, void **ctx )
{
    WCHAR pri_path[MAX_PATH];
    WCHAR *slash;
    HANDLE hFile, hMap;
    DWORD sz;

    GetModuleFileNameW( NULL, pri_path, MAX_PATH );
    slash = wcsrchr( pri_path, '\\' );
    if (slash) *(slash + 1) = 0;
    lstrcatW( pri_path, L"resources.pri" );

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
                    g_pri_data = mapped;
                    g_pri_size = sz;
                    TRACE( "Mapped resources.pri (%lu bytes) at %p\n", (unsigned long)sz, mapped );
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
    return TRUE;
}

static const BYTE *pri_get_data( DWORD *out_size )
{
    InitOnceExecuteOnce( &g_pri_once, init_pri_once, NULL, NULL );
    if (out_size) *out_size = g_pri_size;
    return g_pri_data;
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

static BOOL pri_find_item_by_path( const char *want_path, const char *want_suffix,
                                   const BYTE **out_ptr, DWORD *out_sz )
{
    DWORD pri_sz = 0, hs_sz = 0, rm_sz = 0;
    const BYTE *pri = pri_get_data( &pri_sz );
    const BYTE *hs = NULL, *rm = NULL;
    DWORD num_nodes, num_scopes, num_items;
    DWORD node_off, scope_off, item_off, pool_off, cand_off;
    int matched_item = -1, fallback_item = -1;
    DWORD iid;

    if (!pri) return FALSE;
    if (!pri_get_section( pri, pri_sz, 2, &hs, &hs_sz ) || hs_sz < 0xf0) return FALSE;
    if (!pri_get_section( pri, pri_sz, 3, &rm, &rm_sz ) || rm_sz < 0x80) return FALSE;

    num_nodes = *(const DWORD *)(hs + 0xd8);
    num_scopes = *(const DWORD *)(hs + 0xdc);
    num_items = *(const DWORD *)(hs + 0xe0);
    node_off = 0xf0;
    scope_off = node_off + num_nodes * 12;
    item_off = scope_off + num_scopes * 8;
    pool_off = item_off + num_items * 2;
    if (pool_off >= hs_sz) return FALSE;

    cand_off = 0x80 + num_items * 4;
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

    {
        WORD cand_base = *(const WORD *)(rm + 0x80 + (DWORD)matched_item * 4 + 2);
        WORD data_idx, sec_idx;
        if (cand_off + ((DWORD)cand_base + 1) * 8 > rm_sz) return FALSE;
        data_idx = *(const WORD *)(rm + cand_off + (DWORD)cand_base * 8 + 4);
        sec_idx = *(const WORD *)(rm + cand_off + (DWORD)cand_base * 8 + 6);
        return pri_get_data_item( pri, pri_sz, sec_idx, data_idx, out_ptr, out_sz );
    }
}

BOOL uwp_pri_find_xbf( const WCHAR *uri_or_name, const BYTE **out_ptr, DWORD *out_sz )
{
    char full_path[256] = "Files/";
    char suffix[256] = {0};
    const WCHAR *p = uri_or_name;
    size_t i, len;

    if (!uri_or_name || !out_ptr || !out_sz) return FALSE;
    *out_ptr = NULL;
    *out_sz = 0;

    if (!wcsncmp( p, L"ms-appx:///", 11 )) p += 11;
    else if (!wcsncmp( p, L"ms-appx://", 10 )) p += 10;
    while (*p == '/' || *p == '\\') p++;

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

    {
        const char *slash = strrchr( full_path, '/' );
        snprintf( suffix, sizeof(suffix), "%s", slash ? slash + 1 : full_path );
    }

    if (pri_find_item_by_path( full_path, suffix, out_ptr, out_sz ))
    {
        TRACE( "Resolved PRI XBF '%s' (suffix '%s') -> %p (%lu bytes)\n",
               full_path, suffix, *out_ptr, (unsigned long)*out_sz );
        return TRUE;
    }
    return FALSE;
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
        if (SUCCEEDED(IPropertyValue_GetString( pv, &hs )) && hs)
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
 * 3. ResourceDictionary Key-Value Store (App.xbf & Component Resources)
 * ============================================================================ */

struct uwp_rd_entry
{
    WCHAR key[96];
    IInspectable *value;
};

struct uwp_rd_bucket
{
    void *rd_key;
    struct uwp_rd_entry *entries;
    int count;
    int cap;
    BOOL app_seeded;
};

static struct uwp_rd_bucket g_rd_buckets[32];
static int g_rd_bucket_cnt = 0;

static struct uwp_rd_bucket *get_rd_bucket( void *rd_key, BOOL create )
{
    int i;
    for (i = 0; i < g_rd_bucket_cnt; i++)
        if (g_rd_buckets[i].rd_key == rd_key) return &g_rd_buckets[i];
    if (!create || g_rd_bucket_cnt >= 32) return NULL;
    g_rd_buckets[g_rd_bucket_cnt].rd_key = rd_key;
    return &g_rd_buckets[g_rd_bucket_cnt++];
}

void uwp_rd_insert( void *rd_key, const WCHAR *key, IInspectable *val )
{
    struct uwp_rd_bucket *b;
    int i;
    if (!rd_key || !key || !key[0] || !val) return;
    b = get_rd_bucket( rd_key, TRUE );
    if (!b) return;
    for (i = 0; i < b->count; i++)
    {
        if (!wcscmp( b->entries[i].key, key ))
        {
            if (b->entries[i].value) IInspectable_Release( b->entries[i].value );
            b->entries[i].value = val;
            IInspectable_AddRef( val );
            return;
        }
    }
    if (b->count >= b->cap)
    {
        int ncap = b->cap ? b->cap * 2 : 32;
        struct uwp_rd_entry *ne = realloc( b->entries, ncap * sizeof(*ne) );
        if (!ne) return;
        b->entries = ne;
        b->cap = ncap;
    }
    lstrcpynW( b->entries[b->count].key, key, 96 );
    b->entries[b->count].value = val;
    IInspectable_AddRef( val );
    b->count++;
}

static void uwp_seed_app_xbf_into_rd( void *rd_key )
{
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;
    struct uwp_rd_bucket *b = get_rd_bucket( rd_key, TRUE );
    if (!b || b->app_seeded) return;
    b->app_seeded = TRUE;

    /* Parse App.xbf for top-level double/int/string/object resources */
    if (uwp_pri_find_xbf( L"App.xbf", &xbf, &xbf_sz ) && xbf && xbf_sz > 0x50)
    {
        DWORD meta_sz = *(const DWORD *)(xbf + 4);
        UINT64 str_off = *(const UINT64 *)(xbf + 20);
        const BYTE *f_str = xbf + 12 + (size_t)str_off;
        if (12 + meta_sz < xbf_sz && f_str + 4 <= xbf + 12 + meta_sz)
        {
            DWORD str_cnt = *(const DWORD *)f_str;
            if (str_cnt > 0 && str_cnt < 4096)
            {
                WCHAR **strs = calloc( str_cnt, sizeof(WCHAR *) );
                const BYTE *cur = f_str + 4;
                DWORD i;
                for (i = 0; i < str_cnt && cur + 4 <= xbf + 12 + meta_sz; i++)
                {
                    DWORD slen = *(const DWORD *)cur;
                    cur += 4;
                    if (cur + slen * 2 <= xbf + 12 + meta_sz)
                    {
                        strs[i] = calloc( slen + 1, sizeof(WCHAR) );
                        if (strs[i]) memcpy( strs[i], cur, slen * 2 );
                        cur += slen * 2 + 2;
                    }
                }
                /* Scan node stream for 15 34 80 03 <f32> 21 0a 05 <sid:u16> (x:Double in ResourceDictionary) */
                {
                    const BYTE *nd = xbf + 12 + meta_sz;
                    const BYTE *nd_end = xbf + xbf_sz;
                    const BYTE *p = nd;
                    while (p + 12 <= nd_end)
                    {
                        if (p[0] == 0x15 && p[1] == 0x34 && p[2] == 0x80 && p[3] == 0x03 &&
                            p[8] == 0x21 && p[9] == 0x0a && p[10] == 0x05)
                        {
                            float fval = *(const float *)(p + 4);
                            WORD sid = *(const WORD *)(p + 11);
                            if (sid < str_cnt && strs[sid])
                            {
                                IInspectable *boxed = box_double_value( (double)fval );
                                if (boxed)
                                {
                                    TRACE( "App.xbf resource[%s] = %f\n", debugstr_w(strs[sid]), fval );
                                    uwp_rd_insert( rd_key, strs[sid], boxed );
                                    IInspectable_Release( boxed );
                                }
                            }
                            p += 13;
                            continue;
                        }
                        p++;
                    }
                }
                for (i = 0; i < str_cnt; i++) free( strs[i] );
                free( strs );
            }
        }
    }
}

HRESULT uwp_rd_lookup( void *rd_key, void *key_obj, void **out_val )
{
    WCHAR key_str[128] = {0};
    struct uwp_rd_bucket *b;
    int i;

    if (!out_val) return E_POINTER;
    *out_val = NULL;
    if (!key_obj) return 0x8000000b; /* E_BOUNDS */

    if (!uwp_unbox_string( key_obj, key_str, ARRAY_SIZE(key_str) ))
    {
        /* Also check if key_obj is a raw HSTRING */
        const WCHAR *raw = WindowsGetStringRawBuffer( (HSTRING)key_obj, NULL );
        if (raw) lstrcpynW( key_str, raw, ARRAY_SIZE(key_str) );
    }

    TRACE( "uwp_rd_lookup rd=%p key=%s\n", rd_key, debugstr_w(key_str) );
    if (!key_str[0]) return 0x8000000b;

    uwp_seed_app_xbf_into_rd( rd_key );
    b = get_rd_bucket( rd_key, FALSE );
    if (b)
    {
        for (i = 0; i < b->count; i++)
        {
            if (!wcscmp( b->entries[i].key, key_str ))
            {
                *out_val = b->entries[i].value;
                if (*out_val) IInspectable_AddRef( (IInspectable *)*out_val );
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
    IInspectable IFlyout_iface;
    IInspectable IStateTrigger_iface;

    LONG ref;
    IInspectable *outer;
    enum uwp_elem_kind kind;
    WCHAR class_name[128];
    WCHAR name[64];
    WCHAR uid[64];
    WCHAR text[256];
    int conn_id;

    struct uwp_element *parent;
    struct uwp_element *owner_component;
    IInspectable *content;       /* ContentControl/UserControl/Page/Border/ScrollViewer child */
    IInspectable *children_coll; /* Panel.Children */
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
    BOOL has_bg;
    COLORREF fg_color;
    BOOL has_fg;

    /* Computed screen rectangle */
    RECT bounds;
    BOOL has_bounds;

    /* Events & DependencyProperty store */
    IInspectable *loaded_handlers[8];
    int loaded_cnt;
    BOOL loaded_fired;
    BOOL template_applied;

    IInspectable *click_handlers[8];
    int click_cnt;

    IInspectable *dc_handlers[8];
    int dc_cnt;

    struct uwp_dp_entry dp_store[24];
    int dp_cnt;

    struct uwp_dp_cb_entry dp_cbs[16];
    int dp_cb_cnt;

    /* Template children cache (for CalculationResult etc.) */
    struct uwp_element *tpl_text_container;
    struct uwp_element *tpl_normal_output;
    struct uwp_element *tpl_scroll_left;
    struct uwp_element *tpl_scroll_right;

    /* Named children & deferred substreams for FindName */
    struct uwp_element *named_elems[96];
    int named_cnt;
    struct uwp_defer_entry defer_entries[16];
    int defer_cnt;
};

/* Global registry of live uwp_elements so we can resolve from any interface or outer */
static struct uwp_element *g_all_uwp_elems[2048];
static int g_all_uwp_elem_cnt = 0;

static void register_uwp_elem( struct uwp_element *el )
{
    if (g_all_uwp_elem_cnt < (int)ARRAY_SIZE(g_all_uwp_elems))
        g_all_uwp_elems[g_all_uwp_elem_cnt++] = el;
}

static struct uwp_element *uwp_elem_from_any( void *ptr )
{
    int i;
    if (!ptr) return NULL;
    for (i = g_all_uwp_elem_cnt - 1; i >= 0; i--)
    {
        struct uwp_element *el = g_all_uwp_elems[i];
        if (!el) continue;
        if ((void *)ptr == (void *)el->outer ||
            (char *)ptr >= (char *)el && (char *)ptr < (char *)(el + 1))
            return el;
    }
    /* Try QI for IID_IDependencyObject_UWP to unwrap MCG RCW/CCW */
    {
        void *do_iface = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)ptr, &IID_IDependencyObject_UWP, &do_iface )) && do_iface)
        {
            for (i = g_all_uwp_elem_cnt - 1; i >= 0; i--)
            {
                struct uwp_element *el = g_all_uwp_elems[i];
                if (el && &el->IDependencyObject_iface == do_iface)
                {
                    IUnknown_Release( (IUnknown *)do_iface );
                    return el;
                }
            }
            IUnknown_Release( (IUnknown *)do_iface );
        }
    }
    return NULL;
}

static void uwp_invalidate_window( void )
{
    HWND top = FindWindowW( L"Windows.UI.Core.CoreWindow", NULL );
    if (top)
    {
        HWND root = GetAncestor( top, GA_ROOT );
        if (root && root != top) InvalidateRect( root, NULL, FALSE );
        InvalidateRect( top, NULL, FALSE );
    }
}

static HRESULT WINAPI uwp_nondelegating_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static ULONG WINAPI uwp_nondelegating_AddRef( IInspectable *iface );
static ULONG WINAPI uwp_nondelegating_Release( IInspectable *iface );

static HRESULT WINAPI uwp_sub_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return E_NOINTERFACE;
    if (impl->outer)
        return IInspectable_QueryInterface( impl->outer, iid, out );
    return uwp_nondelegating_QueryInterface( &impl->nondelegating_iface, iid, out );
}

static ULONG WINAPI uwp_sub_AddRef( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->outer)
        return IInspectable_AddRef( impl->outer );
    return uwp_nondelegating_AddRef( &impl->nondelegating_iface );
}

static ULONG WINAPI uwp_sub_Release( IInspectable *iface )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return 1;
    if (impl->outer)
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

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->nondelegating_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDependencyObject_UWP ))
        *out = &impl->IDependencyObject_iface;
    else if (IsEqualGUID( iid, &IID_IDependencyObject2_UWP ))
        *out = &impl->IDependencyObject2_iface;
    else if (IsEqualGUID( iid, &IID_IUIElement_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement2_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement3_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement4_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement5_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IUIElement_Calc2 ))
        *out = &impl->IUIElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement2_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement4_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElement6_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides_UWP ) ||
             IsEqualGUID( iid, &IID_IFrameworkElementOverrides2_UWP ))
        *out = &impl->IFrameworkElement_iface;
    else if (IsEqualGUID( iid, &IID_IFrameworkElement3_UWP ))
        *out = &impl->IFrameworkElement3_iface;
    else if (IsEqualGUID( iid, &IID_IControl_UWP ) ||
             IsEqualGUID( iid, &IID_IControl2_UWP ) ||
             IsEqualGUID( iid, &IID_IControl3_UWP ) ||
             IsEqualGUID( iid, &IID_IControl4_UWP ) ||
             IsEqualGUID( iid, &IID_IControl5_UWP ) ||
             IsEqualGUID( iid, &IID_IControlOverrides_UWP ))
        *out = &impl->IControl_iface;
    else if (IsEqualGUID( iid, &IID_IControlProtected_UWP ))
        *out = &impl->IControlProtected_iface;
    else if (IsEqualGUID( iid, &IID_IContentControl_UWP ) ||
             IsEqualGUID( iid, &IID_IContentControlOverrides_UWP ))
        *out = &impl->IContentControl_iface;
    else if (IsEqualGUID( iid, &IID_IUserControl_UWP ))
        *out = &impl->IUserControl_iface;
    else if (IsEqualGUID( iid, &IID_IPage_UWP ) ||
             IsEqualGUID( iid, &IID_IPageOverrides_UWP ))
        *out = &impl->IPage_iface;
    else if (IsEqualGUID( iid, &IID_IFrame_UWP ) ||
             IsEqualGUID( iid, &IID_INavigate_UWP ))
        *out = &impl->IFrame_iface;
    else if (IsEqualGUID( iid, &IID_IButtonBase_UWP ) ||
             IsEqualGUID( iid, &IID_IButton_UWP ) ||
             IsEqualGUID( iid, &IID_IButtonWithFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IHyperlinkButton_UWP ))
        *out = &impl->IButtonBase_iface;
    else if (IsEqualGUID( iid, &IID_IToggleButton_UWP ) ||
             IsEqualGUID( iid, &IID_IToggleButtonOverrides_UWP ))
        *out = &impl->IToggleButton_iface;
    else if (IsEqualGUID( iid, &IID_IPanel_UWP ) ||
             IsEqualGUID( iid, &IID_IStackPanel_UWP ))
        *out = &impl->IPanel_iface;
    else if (IsEqualGUID( iid, &IID_IGrid_UWP ) ||
             IsEqualGUID( iid, &IID_IGrid_UWP2 ) ||
             IsEqualGUID( iid, &IID_IGrid_Calc ))
        *out = &impl->IGrid_iface;
    else if (IsEqualGUID( iid, &IID_IBorder_UWP ))
        *out = &impl->IBorder_iface;
    else if (IsEqualGUID( iid, &IID_ITextBlock_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock2_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock3_UWP ) ||
             IsEqualGUID( iid, &IID_ITextBlock6_UWP ))
        *out = &impl->ITextBlock_iface;
    else if (IsEqualGUID( iid, &IID_IRowDefinition_UWP ))
        *out = &impl->IRowDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IColumnDefinition_UWP ))
        *out = &impl->IColumnDefinition_iface;
    else if (IsEqualGUID( iid, &IID_IScrollViewer_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer2_UWP ) ||
             IsEqualGUID( iid, &IID_IScrollViewer3_UWP ))
        *out = &impl->IContentControl_iface;
    else if (IsEqualGUID( iid, &IID_IStyle_UWP ) ||
             IsEqualGUID( iid, &IID_ISetter_UWP ) ||
             IsEqualGUID( iid, &IID_ISetterBase_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualStateGroup_UWP ) ||
             IsEqualGUID( iid, &IID_IVisualState_UWP ))
        *out = &impl->IStyle_iface;
    else if (IsEqualGUID( iid, &IID_IBrush_UWP ) ||
             IsEqualGUID( iid, &IID_ISolidColorBrush_UWP ))
        *out = &impl->IBrush_iface;
    else if (IsEqualGUID( iid, &IID_IFlyoutBase_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyout_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItem_UWP ) ||
             IsEqualGUID( iid, &IID_IMenuFlyoutItemBase_UWP ))
        *out = &impl->IFlyout_iface;
    else if (IsEqualGUID( iid, &IID_IStateTriggerBase_UWP ) ||
             IsEqualGUID( iid, &IID_IStateTriggerBaseProtected_UWP ) ||
             IsEqualGUID( iid, &IID_IMarkupExtension_UWP ) ||
             IsEqualGUID( iid, &IID_IMarkupExtensionOverrides_UWP ))
        *out = &impl->IStateTrigger_iface;
    else if (IsEqualGUID( iid, &IID_IDCompositionVisual ) ||
             IsEqualGUID( iid, &IID_IDCompositionVisual2 ))
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
    if (!out) return E_POINTER;
    *out = iface;
    InterlockedIncrement( &((struct uwp_dp_changed_args *)iface)->ref );
    return S_OK;
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
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
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
        if (wcsstr( impl->class_name, L"CalculationResult" ) ||
            wcsstr( impl->class_name, L"OverflowTextBlock" ) ||
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
    if (i == impl->dp_cnt && impl->dp_cnt < (int)ARRAY_SIZE(impl->dp_store))
    {
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
                    IPropertyChangedCallback_Invoke( pml->callback, (IDependencyObject *)sender,
                                                     (IDependencyPropertyChangedEventArgs *)args );
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
            HRESULT (WINAPI *invoke)(void *, void *, void *) = (void *)(*(void ***)impl->dp_cbs[i].callback)[3];
            invoke( impl->dp_cbs[i].callback, sender, dp );
        }
    }

    if (old_val) IInspectable_Release( old_val );
    return S_OK;
}

/* --- IDependencyObject2 (slot 6=RegisterPropertyChangedCallback, slot 7=UnregisterPropertyChangedCallback) --- */
static HRESULT WINAPI uwp_do2_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (token) *token = 1;
    if (!impl || !cb) return S_OK;
    if (impl->dp_cb_cnt < (int)ARRAY_SIZE(impl->dp_cbs))
    {
        int idx = impl->dp_cb_cnt++;
        impl->dp_cbs[idx].dp = dp;
        impl->dp_cbs[idx].callback = (IInspectable *)cb;
        impl->dp_cbs[idx].token = idx + 1;
        IUnknown_AddRef( (IUnknown *)cb );
        if (token) *token = idx + 1;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_do2_UnregisterPropertyChangedCallback( void *iface, void *dp, INT64 token )
{
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
    if (impl->resources_rd) IInspectable_AddRef( impl->resources_rd );
    return S_OK;
}

static HRESULT WINAPI uwp_fe_get_Tag( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
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
    if (impl) impl->width = val;
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
    if (impl) impl->height = val;
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
static const void *dc_args_vtbl[] =
{
    dp_args_QI,
    dp_args_AddRef,
    dp_args_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
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
    for (i = g_all_uwp_elem_cnt - 1; i >= 0; i--)
    {
        struct uwp_element *el = g_all_uwp_elems[i];
        if (el && !wcscmp( el->name, want ))
        {
            *out = el->outer ? el->outer : &el->IInspectable_iface;
            IInspectable_AddRef( (IInspectable *)*out );
            return S_OK;
        }
    }
    return S_OK;
}

static HRESULT WINAPI uwp_fe_add_Loaded( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (tok) tok->value = 1;
    if (!impl || !handler) return S_OK;
    if (impl->loaded_cnt < (int)ARRAY_SIZE(impl->loaded_handlers))
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
    if (impl->dc_cnt < (int)ARRAY_SIZE(impl->dc_handlers))
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
                    HRESULT (WINAPI *invoke)(void *, void *, void *) = (void *)(*(void ***)handler)[3];
                    args->vtbl = dc_args_vtbl;
                    args->ref = 1;
                    args->new_val = (IInspectable *)cur_dc;
                    invoke( handler, sender, args );
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
            uwp_element_create( L"Windows.UI.Xaml.Controls.ScrollViewer", NULL, NULL, (void **)&child );
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
            uwp_element_create( L"Windows.UI.Xaml.Controls.TextBlock", NULL, NULL, (void **)&child );
            if (child)
            {
                child->parent = impl->tpl_text_container ? impl->tpl_text_container : impl;
                child->actual_w = 300.0;
                child->actual_h = 64.0;
                child->font_size = 44.0;
                lstrcpynW( child->name, nm, ARRAY_SIZE(child->name) );
                if (impl->text[0]) lstrcpynW( child->text, impl->text, ARRAY_SIZE(child->text) );
                else lstrcpyW( child->text, L"0" );
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
        uwp_element_create( L"Windows.UI.Xaml.Controls.HyperlinkButton", NULL, NULL, (void **)&child );
        if (child)
        {
            child->parent = impl;
            child->visibility = 1; /* Collapsed by default */
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

    if (val && uwp_unbox_string( val, str_buf, ARRAY_SIZE(str_buf) ) && str_buf[0])
    {
        lstrcpynW( impl->text, str_buf, ARRAY_SIZE(impl->text) );
        TRACE( "uwp_cc_put_Content string on %s (%s) -> %s\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(str_buf) );
    }
    else if ((child_el = uwp_elem_from_any( val )) != NULL)
    {
        child_el->parent = impl;
        TRACE( "uwp_cc_put_Content element on %s (%s) -> %s (%s)\n",
               debugstr_w(impl->class_name), debugstr_w(impl->name),
               debugstr_w(child_el->class_name), debugstr_w(child_el->name) );
        if (impl->data_context && !child_el->data_context)
            uwp_propagate_data_context( child_el, impl->data_context );
        uwp_fire_loaded_tree( child_el );
    }
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
    if (impl->click_cnt < (int)ARRAY_SIZE(impl->click_handlers))
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
    IInspectable *items[128];
    unsigned int count;
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
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal )) { *out = NULL; return E_NOINTERFACE; }
    *out = iface;
    IInspectable_AddRef( iface );
    return S_OK;
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
    if (idx > v->count || v->count >= ARRAY_SIZE(v->items)) return S_OK;
    memmove( &v->items[idx + 1], &v->items[idx], (v->count - idx) * sizeof(void *) );
    v->items[idx] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    v->count++;
    if ((child_el = uwp_elem_from_any( item )) != NULL && v->owner)
        child_el->parent = v->owner;
    return S_OK;
}
static HRESULT WINAPI uwp_vec_RemoveAt( void *iface, unsigned int idx )
{
    struct uwp_vec_view *v = CONTAINING_RECORD( iface, struct uwp_vec_view, IInspectable_iface );
    if (idx >= v->count) return S_OK;
    if (v->items[idx]) IInspectable_Release( v->items[idx] );
    memmove( &v->items[idx], &v->items[idx + 1], (v->count - idx - 1) * sizeof(void *) );
    v->count--;
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
    uwp_sub_GetRuntimeClassName,
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
    uwp_generic_noop,             /* 8: ClearValue */
    uwp_do_GetValue,              /* 9: ReadLocalValue */
    uwp_do_GetValue,              /* 10: GetAnimationBaseValue */
    uwp_generic_noop,             /* 11: get_Dispatcher */
    [12 ... 15] = uwp_generic_noop,
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
    [21] = uwp_ui_get_Visibility,
    [22] = uwp_ui_put_Visibility,
    [23] = uwp_ui_get_RenderSize,
    [41 ... 85] = uwp_ui_event_add,
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

static const void *uwp_cc_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6 ... 31] = uwp_generic_noop,
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

static enum uwp_elem_kind classify_uwp_class( const WCHAR *cls )
{
    if (!cls) return UWP_KIND_GENERIC;
    if (wcsstr( cls, L"Frame" ) && !wcsstr( cls, L"Framework" )) return UWP_KIND_FRAME;
    if (wcsstr( cls, L"Page" )) return UWP_KIND_PAGE;
    if (wcsstr( cls, L"RowDefinition" )) return UWP_KIND_ROW_DEF;
    if (wcsstr( cls, L"ColumnDefinition" )) return UWP_KIND_COL_DEF;
    if (wcsstr( cls, L"StackPanel" )) return UWP_KIND_STACK_PANEL;
    if (wcsstr( cls, L"Grid" ) || wcsstr( cls, L"Panel" ) || wcsstr( cls, L"Canvas" )) return UWP_KIND_GRID;
    if (wcsstr( cls, L"Border" )) return UWP_KIND_BORDER;
    if (wcsstr( cls, L"ToggleButton" )) return UWP_KIND_TOGGLE_BUTTON;
    if (wcsstr( cls, L"Button" )) return UWP_KIND_BUTTON;
    if (wcsstr( cls, L"TextBlock" ) || wcsstr( cls, L"CalculationResult" )) return UWP_KIND_TEXT_BLOCK;
    if (wcsstr( cls, L"ScrollViewer" )) return UWP_KIND_SCROLL_VIEWER;
    if (wcsstr( cls, L"UserControl" ) || wcsstr( cls, L"Calculator" ) ||
        wcsstr( cls, L"NumberPad" ) || wcsstr( cls, L"TitleBar" ) ||
        wcsstr( cls, L"Operators" )) return UWP_KIND_USER_CONTROL;
    if (wcsstr( cls, L"ContentControl" ) || wcsstr( cls, L"NavigationView" ) ||
        wcsstr( cls, L"Pivot" ) || wcsstr( cls, L"Viewbox" )) return UWP_KIND_CONTENT_CONTROL;
    if (wcsstr( cls, L"Style" ) || wcsstr( cls, L"Setter" ) || wcsstr( cls, L"VisualState" )) return UWP_KIND_STYLE;
    if (wcsstr( cls, L"Brush" )) return UWP_KIND_BRUSH;
    if (wcsstr( cls, L"Flyout" )) return UWP_KIND_FLYOUT;
    if (wcsstr( cls, L"Popup" )) return UWP_KIND_POPUP;
    return UWP_KIND_GENERIC;
}

HRESULT uwp_element_create( const WCHAR *class_name, IInspectable *outer, IInspectable **inner, void **instance )
{
    struct uwp_element *impl;
    if (!instance) return E_POINTER;
    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;

    impl->nondelegating_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_nondelegating_vtbl;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
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
    impl->IStyle_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IBrush_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IFlyout_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;
    impl->IStateTrigger_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_fe_vtbl;

    impl->ref = 1;
    impl->outer = outer;
    lstrcpynW( impl->class_name, class_name ? class_name : L"Windows.UI.Xaml.FrameworkElement", ARRAY_SIZE(impl->class_name) );
    impl->kind = classify_uwp_class( impl->class_name );
    impl->grid_row_span = 1;
    impl->grid_col_span = 1;
    impl->grid_len_type = 2; /* Star */
    impl->grid_len_val = 1.0;
    impl->is_enabled = TRUE;
    impl->opacity = 1.0;
    impl->h_align = 3; /* Stretch */
    impl->v_align = 3; /* Stretch */
    impl->font_size = 16.0;
    xaml_create_dcomp_visual( &impl->visual );

    register_uwp_elem( impl );

    if (inner)
    {
        *inner = &impl->nondelegating_iface;
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

    for (i = 0; i < impl->dc_cnt; i++)
    {
        if (impl->dc_handlers[i])
        {
            struct uwp_dc_args *args = calloc( 1, sizeof(*args) );
            if (args)
            {
                IInspectable *sender = impl->outer ? impl->outer : &impl->IInspectable_iface;
                HRESULT (WINAPI *invoke)(void *, void *, void *) = (void *)(*(void ***)impl->dc_handlers[i])[3];
                args->vtbl = dc_args_vtbl;
                args->ref = 1;
                args->new_val = dc;
                if (dc) IInspectable_AddRef( dc );
                invoke( impl->dc_handlers[i], sender, args );
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

static struct { const void *vtbl; LONG ref; } g_dummy_routed_args = { (const void *)dp_changed_args_vtbl, 1 };

static void uwp_apply_template_if_needed( struct uwp_element *el )
{
    void *fe_ovr = NULL;
    if (!el || el->template_applied || !el->outer) return;
    el->template_applied = TRUE;
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
}

static void uwp_fire_loaded_tree( struct uwp_element *el )
{
    int i;
    if (!el) return;

    uwp_apply_template_if_needed( el );

    if (!el->loaded_fired && el->loaded_cnt > 0)
    {
        IInspectable *sender = el->outer ? el->outer : &el->IInspectable_iface;
        el->loaded_fired = TRUE;
        for (i = 0; i < el->loaded_cnt; i++)
        {
            if (el->loaded_handlers[i])
            {
                HRESULT (WINAPI *invoke)(void *, void *, void *) = (void *)(*(void ***)el->loaded_handlers[i])[3];
                TRACE( "Firing Loaded handler[%d] on %s (%s)\n",
                       i, debugstr_w(el->class_name), debugstr_w(el->name) );
                invoke( el->loaded_handlers[i], sender, &g_dummy_routed_args );
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
            impl->content = page;
            if (page_el)
            {
                page_el->parent = impl;
                /* Invoke IPageOverrides::OnNavigatedTo (slot 8) if implemented */
                if (page_el->outer)
                {
                    void *page_ovr = NULL;
                    if (SUCCEEDED(IInspectable_QueryInterface( page_el->outer, &IID_IPageOverrides_UWP, &page_ovr )) && page_ovr)
                    {
                        HRESULT (WINAPI *on_nav)(void *, void *) = (void *)(*(void ***)page_ovr)[8];
                        if (on_nav) on_nav( page_ovr, &g_dummy_routed_args );
                        IUnknown_Release( (IUnknown *)page_ovr );
                    }
                }
                uwp_fire_loaded_tree( page_el );
            }
            xaml_window_present_current();
        }
    }
    return S_OK;
}

/* ============================================================================
 * 6. XBF v2 Stack Parser & Visual Tree Builder
 * ============================================================================ */

static const WCHAR *uwp_btype_to_class_name( WORD tid )
{
    switch (tid)
    {
    case 0x802c: return L"Windows.UI.Xaml.Controls.ColumnDefinition";
    case 0x806a: return L"Windows.UI.Xaml.Controls.RowDefinition";
    case 0x8072: return L"Windows.UI.Xaml.Style";
    case 0x8089: return L"Windows.UI.Xaml.VisualState";
    case 0x808a: return L"Windows.UI.Xaml.VisualStateGroup";
    case 0x80bf: return L"Windows.UI.Xaml.DataTemplate";
    case 0x80d2: return L"Windows.UI.Xaml.Media.SolidColorBrush";
    case 0x80d5: return L"Windows.UI.Xaml.Controls.MenuFlyout";
    case 0x8106: return L"Windows.UI.Xaml.Setter";
    case 0x811e: return L"Windows.UI.Xaml.Controls.Border";
    case 0x8146: return L"Windows.UI.Xaml.Controls.Image";
    case 0x816b: return L"Windows.UI.Xaml.Controls.Primitives.Popup";
    case 0x8189: return L"Windows.UI.Xaml.Media.Animation.Storyboard";
    case 0x818c: return L"Windows.UI.Xaml.Controls.TextBlock";
    case 0x819c: return L"Windows.UI.Xaml.Controls.FontIcon";
    case 0x81b8: return L"Windows.UI.Xaml.Controls.FontIcon";
    case 0x81ba: return L"Windows.UI.Xaml.Controls.Grid";
    case 0x81e1: return L"Windows.UI.Xaml.Controls.StackPanel";
    case 0x81eb: return L"Windows.UI.Xaml.Controls.UserControl";
    case 0x820d: return L"Windows.UI.Xaml.Controls.Page";
    case 0x821c: return L"Windows.UI.Xaml.Controls.Button";
    case 0x8234: return L"Windows.UI.Xaml.Controls.Primitives.ToggleButton";
    case 0x82ea: return L"Windows.UI.Xaml.DeferredElement";
    case 0x82f5: return L"Windows.UI.Xaml.AdaptiveTrigger";
    case 0x8381: return L"Windows.UI.Xaml.Input.KeyboardAccelerator";
    default:     return L"Windows.UI.Xaml.Controls.ContentControl";
    }
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
        /* Slot 7: GetXamlTypeByFullName */
        HRESULT (WINAPI *get_by_name)(void *, HSTRING, void **) = (void *)(*(void ***)provider)[7];
        if (get_by_name && SUCCEEDED(get_by_name( provider, h_owner, &xaml_type )) && xaml_type)
        {
            /* Slot 20 on IXamlType_UWP: GetMember(HSTRING, IXamlMember**) */
            HRESULT (WINAPI *get_member)(void *, HSTRING, void **) = (void *)(*(void ***)xaml_type)[20];
            if (get_member && SUCCEEDED(WindowsCreateString( prop_name, wcslen(prop_name), &h_prop )))
            {
                if (SUCCEEDED(get_member( xaml_type, h_prop, &member )) && member)
                {
                    /* Slot 11 on IXamlMember: get_Type(IXamlType**) */
                    HRESULT (WINAPI *get_mtype)(void *, void **) = (void *)(*(void ***)member)[11];
                    /* Slot 13 on IXamlMember: SetValue(IInspectable*, IInspectable*) */
                    HRESULT (WINAPI *set_val)(void *, void *, void *) = (void *)(*(void ***)member)[13];
                    if (get_mtype && SUCCEEDED(get_mtype( member, &member_type )) && member_type)
                    {
                        /* Slot 19 on IXamlType_UWP: CreateFromString(HSTRING, IInspectable**) */
                        HRESULT (WINAPI *from_str)(void *, HSTRING, void **) = (void *)(*(void ***)member_type)[19];
                        if (from_str && SUCCEEDED(WindowsCreateString( str_val, wcslen(str_val), &h_val )))
                        {
                            hr = from_str( member_type, h_val, &boxed_val );
                            if (SUCCEEDED(hr) && boxed_val && set_val)
                            {
                                hr = set_val( member, target_obj, boxed_val );
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
    struct xbf_type_info *types;
    DWORD type_cnt;
    struct xbf_prop_info *props;
    DWORD prop_cnt;
    const BYTE *nodes;
    DWORD sub_cnt;
    DWORD sub_base;
    BOOL sub_done[256];
    void *app_outer;
    void *connector;
    BOOL is_uwp_conn;
    struct uwp_element *root_el;
    IInspectable *root_comp;
};

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
            if (p + ccnt * 2 > end_p) return FALSE;
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

static void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, WORD pid,
                                int vt, double num, const WCHAR *sval, const double vec4[4] )
{
    if (!el) return;
    if (pid & 0x8000)
    {
        switch (pid)
        {
        case 0x835c: el->grid_row = (int)num; break;
        case 0x8359: el->grid_col = (int)num; break;
        case 0x835e: el->grid_row_span = max( 1, (int)num ); break;
        case 0x835b: el->grid_col_span = max( 1, (int)num ); break;
        case 0x80e3: /* RowDefinition.Height */
        case 0x805f: /* ColumnDefinition.Width */
            if (vt == 0x07) { el->grid_len_type = (int)vec4[0]; el->grid_len_val = vec4[1]; }
            else if (vt == 0x03 || vt == 0x04) { el->grid_len_type = 1; el->grid_len_val = num; }
            break;
        case 0x818d: /* Margin */
            if (vt == 0x06)
            {
                el->margin_l = vec4[0]; el->margin_t = vec4[1];
                el->margin_r = vec4[2]; el->margin_b = vec4[3];
            }
            break;
        case 0x8340: /* ContentControl.Content */
        case 0x8769: /* TextBlock.Text */
        case 0x873a: /* FontIcon.Glyph */
            if (sval && sval[0])
                lstrcpynW( el->text, sval, ARRAY_SIZE(el->text) );
            break;
        case 0x8008: /* x:Uid or Name */
            if (sval && sval[0])
            {
                lstrcpynW( el->uid, sval, ARRAY_SIZE(el->uid) );
                if (!el->text[0])
                {
                    WCHAR res_key[160], res_val[128];
                    swprintf( res_key, ARRAY_SIZE(res_key),
                              L"%s/[using:CalculatorApp.Common]KeyboardShortcutManager/Character", sval );
                    if (uwp_pri_lookup_string( res_key, res_val, ARRAY_SIZE(res_val) ) && res_val[0])
                        lstrcpynW( el->text, res_val, ARRAY_SIZE(el->text) );
                }
            }
            break;
        case 0x829e: /* FontSize */
            if (num > 0) el->font_size = num;
            break;
        case 0x8199: /* Width */
            el->width = num;
            break;
        case 0x8188: /* Height */
            el->height = num;
            break;
        case 0x8191: /* MinWidth */
            el->min_width = num;
            break;
        case 0x8190: /* MinHeight */
            el->min_height = num;
            break;
        case 0x818a: /* HorizontalAlignment */
            el->h_align = (int)num;
            break;
        case 0x8198: /* VerticalAlignment */
            el->v_align = (int)num;
            break;
        case 0x8138: /* StackPanel.Orientation */
            el->orientation = (int)num;
            break;
        case 0x8167: /* UIElement.Visibility */
            el->visibility = (int)num;
            break;
        }
    }
    else if (pid < ctx->prop_cnt)
    {
        const struct xbf_prop_info *pi = &ctx->props[pid];
        if (!wcscmp( pi->name, L"Name" ) && sval && sval[0])
        {
            lstrcpynW( el->text, sval, ARRAY_SIZE(el->text) );
        }
        else if (!wcscmp( pi->name, L"ButtonId" ) && sval && sval[0])
        {
            IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;
            const WCHAR *owner_cls = (pi->owner_tid < ctx->type_cnt)
                ? ctx->types[pi->owner_tid].full_name
                : L"CalculatorApp.Controls.CalculatorButton";
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

static void xbf_exec_substream( struct xbf_ctx *ctx, int sub_idx, struct uwp_element *initial_root,
                                struct uwp_element **out_created )
{
    DWORD ss, ee, p, end_p;
    struct uwp_element *obj_stack[64];
    IInspectable *raw_stack[64];
    int obj_sp = 0;
    WORD prop_stack[64];
    int prop_sp = 0;

    if (out_created) *out_created = NULL;
    if (sub_idx < 0 || (DWORD)sub_idx >= ctx->sub_cnt) return;
    if (sub_idx < 256)
    {
        if (ctx->sub_done[sub_idx]) return;
        ctx->sub_done[sub_idx] = TRUE;
    }

    ss = *(const DWORD *)(ctx->nodes + 4 + sub_idx * 8);
    ee = *(const DWORD *)(ctx->nodes + 4 + sub_idx * 8 + 4);
    p = ctx->sub_base + ss;
    end_p = ctx->sub_base + ee;
    if (end_p > ctx->node_sz) end_p = ctx->node_sz;

    if (initial_root)
    {
        obj_stack[0] = initial_root;
        raw_stack[0] = initial_root->outer ? initial_root->outer : &initial_root->IInspectable_iface;
        obj_sp = 1;
    }

    while (p < end_p)
    {
        BYTE op = ctx->nodes[p++];
        if (op == 0x12) /* LineInfo (6 bytes) */
        {
            if (p + 6 > end_p) break;
            p += 6;
        }
        else if (op == 0x03) /* Xmlns prefix */
        {
            DWORD plen;
            if (p + 6 > end_p) break;
            plen = *(const DWORD *)(ctx->nodes + p + 2);
            p += 6 + plen * 2;
        }
        else if (op == 0x17 || op == 0x14 || op == 0x15 || op == 0x16)
        {
            WORD tid;
            const WCHAR *cls_name = L"Windows.UI.Xaml.Controls.ContentControl";
            IInspectable *inst = NULL;
            struct uwp_element *el = NULL;

            if (p + 2 > end_p) break;
            tid = *(const WORD *)(ctx->nodes + p);
            p += 2;

            if (op == 0x17 && ctx->root_el)
            {
                el = ctx->root_el;
                inst = ctx->root_comp;
            }
            else
            {
                if (tid & 0x8000)
                {
                    cls_name = uwp_btype_to_class_name( tid );
                    if (tid == 0x82ea) /* DeferredElement placeholder */
                    {
                        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                        el = uwp_elem_from_any( inst );
                        if (el) el->kind = UWP_KIND_DEFERRED;
                    }
                    else
                    {
                        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                        el = uwp_elem_from_any( inst );
                    }
                }
                else if (tid < ctx->type_cnt)
                {
                    cls_name = ctx->types[tid].full_name;
                    /* Do not activate ResourceString or ResourceVirtualKey via provider if we evaluate it in XBF */
                    if (wcsstr( cls_name, L"ResourceString" ) || wcsstr( cls_name, L"ResourceVirtualKey" ))
                    {
                        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                        el = uwp_elem_from_any( inst );
                    }
                    else
                    {
                        if (FAILED(uwp_create_instance_from_provider( cls_name, (void **)&inst )) || !inst)
                            uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                        el = uwp_elem_from_any( inst );
                    }
                }
                else
                {
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                }
            }

            if (el && ctx->root_el) el->owner_component = ctx->root_el;
            if (out_created && !*out_created && el) *out_created = el;

            if (obj_sp < (int)ARRAY_SIZE(obj_stack))
            {
                obj_stack[obj_sp] = el;
                raw_stack[obj_sp] = inst;
                obj_sp++;
            }
        }
        else if (op == 0x0b) /* x:Class */
        {
            DWORD slen;
            if (p + 4 > end_p) break;
            slen = *(const DWORD *)(ctx->nodes + p);
            p += 4 + slen * 2;
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
                }
            }
        }
        else if (op == 0x0d) /* x:Name */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            if (obj_sp > 0 && obj_stack[obj_sp - 1] && sbuf[0])
            {
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                lstrcpynW( top_el->name, sbuf, ARRAY_SIZE(top_el->name) );
                if (ctx->root_el && ctx->root_el->named_cnt < (int)ARRAY_SIZE(ctx->root_el->named_elems))
                    ctx->root_el->named_elems[ctx->root_el->named_cnt++] = top_el;
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
        else if (op == 0x02) /* PopProperty */
        {
            if (prop_sp > 0) prop_sp--;
        }
        else if (op >= 0x1a && op <= 0x1f) /* SetValue */
        {
            WORD pid;
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            if (p + 2 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 )) break;
            if (obj_sp > 0 && obj_stack[obj_sp - 1])
                xbf_apply_property( ctx, obj_stack[obj_sp - 1], pid, vt, num, sbuf, vec4 );
        }
        else if (op == 0x0f) /* DeferSubstream */
        {
            WORD def_sub;
            BYTE def_flags, def_kind;
            if (p + 4 > end_p) break;
            def_sub = *(const WORD *)(ctx->nodes + p);
            def_flags = ctx->nodes[p + 2];
            def_kind = ctx->nodes[p + 3];
            (void)def_flags;
            p += 4;

            if (def_kind == 0x09) /* DeferredElement: <name_sid:u16> <00> <01> */
            {
                if (p + 4 > end_p) break;
                {
                    WORD nsid = *(const WORD *)(ctx->nodes + p);
                    const WCHAR *dname = (nsid < ctx->str_cnt && ctx->strs[nsid]) ? ctx->strs[nsid] : L"";
                    p += 4;
                    if (obj_sp > 0 && obj_stack[obj_sp - 1])
                        lstrcpynW( obj_stack[obj_sp - 1]->name, dname, ARRAY_SIZE(obj_stack[obj_sp - 1]->name) );
                    if (ctx->root_el && ctx->root_el->defer_cnt < (int)ARRAY_SIZE(ctx->root_el->defer_entries))
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
            else if (def_kind == 0x0a || def_kind == 0x05)
            {
                /* Skip compressed index table up to matching 0x02 (PopProperty) */
                if (def_kind == 0x0a)
                    xbf_exec_substream( ctx, def_sub, ctx->root_el, NULL );
                else if (def_kind == 0x05)
                    xbf_exec_substream( ctx, def_sub, ctx->root_el, NULL );

                while (p < end_p)
                {
                    /* Wait for 0x02 followed by next valid opcode (0x13, 0x14, 0x21, etc.) */
                    if (ctx->nodes[p] == 0x02 &&
                        (p + 1 == end_p || ctx->nodes[p + 1] == 0x13 || ctx->nodes[p + 1] == 0x14 ||
                         ctx->nodes[p + 1] == 0x21 || ctx->nodes[p + 1] == 0x12))
                    {
                        p++;
                        if (prop_sp > 0) prop_sp--;
                        break;
                    }
                    p++;
                }
            }
            else
            {
                break;
            }
        }
        else if (op == 0x21)
        {
            BYTE subop;
            if (p >= end_p) break;
            subop = ctx->nodes[p++];
            if (subop == 0x08) /* AddToCollection */
            {
                if (obj_sp >= 2)
                {
                    struct uwp_element *child_el = obj_stack[obj_sp - 1];
                    IInspectable *child_raw = raw_stack[obj_sp - 1];
                    struct uwp_element *parent_el = obj_stack[obj_sp - 2];
                    WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : 0x8288;
                    obj_sp--;

                    if (parent_el && child_raw)
                    {
                        if (pid == 0x835d) /* RowDefinitions */
                        {
                            if (!parent_el->row_defs_coll) uwp_vec_create( parent_el, &parent_el->row_defs_coll );
                            uwp_vec_Append( parent_el->row_defs_coll, child_raw );
                        }
                        else if (pid == 0x835a) /* ColumnDefinitions */
                        {
                            if (!parent_el->col_defs_coll) uwp_vec_create( parent_el, &parent_el->col_defs_coll );
                            uwp_vec_Append( parent_el->col_defs_coll, child_raw );
                        }
                        else if (pid == 0x8288 || parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL)
                        {
                            if (!parent_el->children_coll) uwp_vec_create( parent_el, &parent_el->children_coll );
                            uwp_vec_Append( parent_el->children_coll, child_raw );
                            if (child_el) child_el->parent = parent_el;
                        }
                    }
                }
            }
            else if (subop == 0x07) /* SetObjectProperty */
            {
                WORD pid = 0;
                if (p + 2 <= end_p) { pid = *(const WORD *)(ctx->nodes + p); p += 2; }
                if (obj_sp >= 2)
                {
                    struct uwp_element *child_el = obj_stack[obj_sp - 1];
                    IInspectable *child_raw = raw_stack[obj_sp - 1];
                    struct uwp_element *parent_el = obj_stack[obj_sp - 2];
                    obj_sp--;
                    if (parent_el && child_raw &&
                        (pid == 0x8340 || pid == 0x83e7 || pid == 0x8134 || pid == 0x829c || !parent_el->content))
                    {
                        if (parent_el->content) IInspectable_Release( parent_el->content );
                        parent_el->content = child_raw;
                        IInspectable_AddRef( child_raw );
                        if (child_el) child_el->parent = parent_el;
                    }
                }
            }
            else if (subop == 0x0a) /* AddToDictionary */
            {
                int vt = 0;
                double num = 0;
                WCHAR kbuf[128] = {0};
                if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, kbuf, ARRAY_SIZE(kbuf), NULL )) break;
                if (obj_sp >= 2)
                {
                    IInspectable *val_raw = raw_stack[obj_sp - 1];
                    struct uwp_element *parent_el = obj_stack[obj_sp - 2];
                    obj_sp--;
                    if (parent_el && kbuf[0] && val_raw)
                    {
                        void *rd = NULL;
                        uwp_fe_get_Resources( &parent_el->IFrameworkElement_iface, &rd );
                        if (rd)
                        {
                            uwp_rd_insert( rd, kbuf, val_raw );
                            IInspectable_Release( (IInspectable *)rd );
                        }
                    }
                }
            }
            else if (subop == 0x20) /* ProvideValue and SetProperty (u16 pid) */
            {
                WORD pid = 0;
                if (p + 2 > end_p) break;
                pid = *(const WORD *)(ctx->nodes + p);
                p += 2;
                if (obj_sp >= 2)
                {
                    struct uwp_element *ext_el = obj_stack[obj_sp - 1];
                    struct uwp_element *target_el = obj_stack[obj_sp - 2];
                    obj_sp--;
                    if (ext_el && target_el && ext_el->text[0])
                    {
                        WCHAR resolved[256] = {0};
                        if (uwp_pri_lookup_string( ext_el->text, resolved, ARRAY_SIZE(resolved) ) && resolved[0])
                        {
                            if (pid == 0x8340 || pid == 0x8769 || !target_el->text[0])
                                lstrcpynW( target_el->text, resolved, ARRAY_SIZE(target_el->text) );
                        }
                    }
                }
            }
            else if (subop == 0x01)
            {
                if (obj_sp > 1) obj_sp--;
            }
        }
        else if (op == 0x22)
        {
            break;
        }
        else
        {
            /* Unrecognized opcode: stop substream cleanly */
            break;
        }
    }
}

static void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created )
{
    struct xbf_ctx ctx = {0};
    UINT64 offs[6];
    DWORD i;

    if (!xbf || xbf_sz < 0x50 || memcmp( xbf, "XBF\0", 4 ) != 0) return;

    ctx.xbf = xbf;
    ctx.xbf_sz = xbf_sz;
    ctx.meta_sz = *(const DWORD *)(xbf + 4);
    ctx.node_sz = *(const DWORD *)(xbf + 8);
    if (12 + ctx.meta_sz + ctx.node_sz > xbf_sz) return;

    memcpy( offs, xbf + 20, sizeof(offs) );
    ctx.app_outer = app_outer;
    ctx.connector = connector;
    ctx.is_uwp_conn = is_uwp_conn;
    ctx.root_comp = component;
    ctx.root_el = attach_root ? attach_root : uwp_elem_from_any( component );

    /* 1. String Table */
    {
        const BYTE *p = xbf + 12 + (size_t)offs[0];
        if (p + 4 > xbf + 12 + ctx.meta_sz) return;
        ctx.str_cnt = *(const DWORD *)p;
        p += 4;
        if (ctx.str_cnt > 4096) return;
        ctx.strs = calloc( ctx.str_cnt, sizeof(WCHAR *) );
        for (i = 0; i < ctx.str_cnt && p + 4 <= xbf + 12 + ctx.meta_sz; i++)
        {
            DWORD len = *(const DWORD *)p;
            p += 4;
            if (p + len * 2 <= xbf + 12 + ctx.meta_sz)
            {
                ctx.strs[i] = calloc( len + 1, sizeof(WCHAR) );
                if (ctx.strs[i]) memcpy( ctx.strs[i], p, len * 2 );
                p += len * 2 + 2;
            }
        }
    }

    /* 2. XmlNamespace Table -> Type Table -> Property Table */
    {
        const BYTE *xp = xbf + 12 + (size_t)offs[5];
        DWORD xcnt = (xp + 4 <= xbf + 12 + ctx.meta_sz) ? *(const DWORD *)xp : 0;
        const DWORD *x_sids = (const DWORD *)(xp + 4);

        const BYTE *tp = xbf + 12 + (size_t)offs[3];
        if (tp + 4 <= xbf + 12 + ctx.meta_sz)
        {
            ctx.type_cnt = *(const DWORD *)tp;
            if (ctx.type_cnt > 0 && ctx.type_cnt < 1024)
            {
                ctx.types = calloc( ctx.type_cnt, sizeof(*ctx.types) );
                for (i = 0; i < ctx.type_cnt && tp + 4 + (i + 1) * 12 <= xbf + 12 + ctx.meta_sz; i++)
                {
                    DWORD xid = *(const DWORD *)(tp + 4 + i * 12 + 4);
                    DWORD sid = *(const DWORD *)(tp + 4 + i * 12 + 8);
                    const WCHAR *sname = (sid < ctx.str_cnt && ctx.strs[sid]) ? ctx.strs[sid] : L"";
                    const WCHAR *ns = (xid < xcnt && x_sids[xid] < ctx.str_cnt) ? ctx.strs[x_sids[xid]] : L"";
                    lstrcpynW( ctx.types[i].short_name, sname, 64 );
                    if (ns && !wcsncmp( ns, L"using:", 6 ))
                        swprintf( ctx.types[i].full_name, 128, L"%s.%s", ns + 6, sname );
                    else
                        swprintf( ctx.types[i].full_name, 128, L"Windows.UI.Xaml.Controls.%s", sname );
                }
            }
        }

        const BYTE *pp = xbf + 12 + (size_t)offs[4];
        if (pp + 4 <= xbf + 12 + ctx.meta_sz)
        {
            ctx.prop_cnt = *(const DWORD *)pp;
            if (ctx.prop_cnt > 0 && ctx.prop_cnt < 1024)
            {
                ctx.props = calloc( ctx.prop_cnt, sizeof(*ctx.props) );
                for (i = 0; i < ctx.prop_cnt && pp + 4 + (i + 1) * 12 <= xbf + 12 + ctx.meta_sz; i++)
                {
                    DWORD tid = *(const DWORD *)(pp + 4 + i * 12 + 4);
                    DWORD sid = *(const DWORD *)(pp + 4 + i * 12 + 8);
                    ctx.props[i].owner_tid = (WORD)tid;
                    if (sid < ctx.str_cnt && ctx.strs[sid])
                        lstrcpynW( ctx.props[i].name, ctx.strs[sid], 64 );
                }
            }
        }
    }

    /* 3. Node Substreams */
    ctx.nodes = xbf + 12 + ctx.meta_sz;
    if (ctx.node_sz >= 12)
    {
        ctx.sub_cnt = *(const DWORD *)ctx.nodes;
        ctx.sub_base = 4 + ctx.sub_cnt * 8;
        if (ctx.sub_cnt < 256 && ctx.sub_base <= ctx.node_sz)
        {
            if (only_sub_idx >= 0)
            {
                xbf_exec_substream( &ctx, only_sub_idx, NULL, out_created );
            }
            else
            {
                xbf_exec_substream( &ctx, 0, ctx.root_el, out_created );
                /* Also execute substream 1..N for VisualStateGroups / Styles that hold connection IDs */
                for (i = 1; i < ctx.sub_cnt; i++)
                {
                    /* Skip deferred x:Load substreams until FindName realizes them */
                    BOOL is_lazy = FALSE;
                    int d;
                    if (ctx.root_el)
                    {
                        for (d = 0; d < ctx.root_el->defer_cnt; d++)
                            if (ctx.root_el->defer_entries[d].sub_idx == (int)i) { is_lazy = TRUE; break; }
                    }
                    if (!is_lazy)
                        xbf_exec_substream( &ctx, (int)i, ctx.root_el, NULL );
                }
            }
        }
    }

    for (i = 0; i < ctx.str_cnt; i++) free( ctx.strs[i] );
    free( ctx.strs );
    free( ctx.types );
    free( ctx.props );
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
        HRESULT (WINAPI *get_raw)(void *, HSTRING *) = (void *)(*(void ***)uri_rt)[6];
        HSTRING hraw = NULL;
        if (SUCCEEDED(get_raw( uri_rt, &hraw )) && hraw)
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

    /* 1. Connect(1, component) first so root field is wired */
    {
        HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)conn)[6];
        conn_fn( conn, 1, component );
    }

    /* 2. Build visual tree from XBF v2 stream and Connect child elements */
    uwp_parse_and_build_xbf( xbf, xbf_sz, (IInspectable *)component, conn, TRUE, app_outer, -1, NULL, NULL );

    /* 3. Query IComponentConnector2 (slot 6 = GetBindingConnector) and initialize {x:Bind} */
    if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
    {
        HRESULT (WINAPI *get_bc)(void *, int, void *, void **) = (void *)(*(void ***)conn2)[6];
        if (get_bc && SUCCEEDED(get_bc( conn2, 1, component, &binding )) && binding)
        {
            HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)binding)[6];
            if (b_conn) b_conn( binding, 1, component );
            IUnknown_Release( (IUnknown *)binding );
        }
        IUnknown_Release( (IUnknown *)conn2 );
    }

    IUnknown_Release( (IUnknown *)conn );
    depth--;
    return TRUE;
}

/* ============================================================================
 * 7. Genuine Visual Tree Layout, Rendering & Input Dispatch
 * ============================================================================ */

static void uwp_arrange_element( struct uwp_element *el, RECT rc )
{
    int w, h;
    if (!el) return;
    if (el->visibility != 0 || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_DEFERRED)
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

        /* Also infer max row/col from children if RowDefinitions/ColumnDefinitions were smaller */
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED) continue;
            if (ch->grid_row + ch->grid_row_span > nrows) nrows = ch->grid_row + ch->grid_row_span;
            if (ch->grid_col + ch->grid_col_span > ncols) ncols = ch->grid_col + ch->grid_col_span;
        }
        if (nrows > 30) nrows = 30;
        if (ncols > 30) ncols = 30;

        for (i = 0; i < nrows; i++)
        {
            struct uwp_element *rd = (rows && (unsigned)i < rows->count) ? uwp_elem_from_any( rows->items[i] ) : NULL;
            r_type[i] = rd ? rd->grid_len_type : 2;
            r_val[i]  = rd ? rd->grid_len_val  : 1.0;
            /* Collapse empty history/dock or 0-star rows */
            if (r_type[i] == 2 && r_val[i] <= 0.001) { r_type[i] = 1; r_val[i] = 0; }
            if (r_type[i] == 0) /* Auto */
            {
                /* Check if any visible child is in this row */
                BOOL has_vis = FALSE;
                for (k = 0; k < children->count; k++)
                {
                    struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                    if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED && ch->grid_row == i)
                    { has_vis = TRUE; break; }
                }
                r_type[i] = 1;
                r_val[i] = has_vis ? 36.0 : 0.0;
            }
            if (r_type[i] == 1) r_fixed += r_val[i];
            else r_stars += (r_val[i] > 0 ? r_val[i] : 1.0);
        }

        for (i = 0; i < ncols; i++)
        {
            struct uwp_element *cd = (cols && (unsigned)i < cols->count) ? uwp_elem_from_any( cols->items[i] ) : NULL;
            c_type[i] = cd ? cd->grid_len_type : 2;
            c_val[i]  = cd ? cd->grid_len_val  : 1.0;
            /* Collapse ColumnHistory or 0-star columns when narrower than wide view */
            if (cd && !wcscmp( cd->name, L"ColumnHistory" )) { c_type[i] = 1; c_val[i] = 0; }
            if (c_type[i] == 2 && c_val[i] <= 0.001) { c_type[i] = 1; c_val[i] = 0; }
            if (c_type[i] == 0) { c_type[i] = 1; c_val[i] = 0; }
            if (c_type[i] == 1) c_fixed += c_val[i];
            else c_stars += (c_val[i] > 0 ? c_val[i] : 1.0);
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
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED) vis_cnt++;
        }
        if (vis_cnt > 0)
        {
            int idx = 0;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                RECT crc = rc;
                if (!ch || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED) continue;
                if (el->orientation == 1) /* Horizontal */
                {
                    crc.left  = rc.left + (w * idx) / (int)vis_cnt;
                    crc.right = rc.left + (w * (idx + 1)) / (int)vis_cnt;
                }
                else
                {
                    crc.top    = rc.top + (h * idx) / (int)vis_cnt;
                    crc.bottom = rc.top + (h * (idx + 1)) / (int)vis_cnt;
                }
                idx++;
                uwp_arrange_element( ch, crc );
            }
        }
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
            uwp_arrange_element( ch, rc );
    }
}

/* Format Segoe MDL2 Assets / private use glyphs to readable symbols if font lacks them */
static const WCHAR *uwp_format_display_label( const struct uwp_element *el, WCHAR *tmp, size_t tmp_len )
{
    const WCHAR *s = el->text;
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

static void uwp_paint_element( HDC hdc, struct uwp_element *el )
{
    if (!el || !el->has_bounds || el->visibility != 0) return;

    if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        RECT brc = el->bounds;
        WCHAR tmp[64];
        const WCHAR *label = uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) );
        COLORREF bg;
        HBRUSH br;
        HPEN pen;
        HFONT font;
        HGDIOBJ old_br, old_pen, old_font;
        int fh;

        /* Skip invisible shortcut-only buttons (CopyButton, PasteButton) that have no label and tiny/zero cell */
        if (!label[0] && (wcsstr( el->name, L"CopyButton" ) || wcsstr( el->name, L"PasteButton" )))
            return;

        InflateRect( &brc, -2, -2 );
        if (brc.right <= brc.left || brc.bottom <= brc.top) return;

        if (!wcscmp( el->name, L"EqualButton" ) || (!wcscmp( label, L"=" ) && label[1] == 0))
            bg = el->is_pressed ? RGB( 0, 90, 170 ) : RGB( 0, 120, 215 );
        else if (wcsstr( el->name, L"Num" ) || !wcscmp( el->name, L"DecimalSeparatorButton" ) ||
                 !wcscmp( el->name, L"NegateButton" ))
            bg = el->is_pressed ? RGB( 45, 45, 45 ) : (el->is_enabled ? RGB( 59, 59, 59 ) : RGB( 38, 38, 38 ));
        else
            bg = el->is_pressed ? RGB( 40, 40, 40 ) : (el->is_enabled ? RGB( 50, 50, 50 ) : RGB( 36, 36, 36 ));

        br = CreateSolidBrush( bg );
        pen = CreatePen( PS_SOLID, 1, RGB( 68, 68, 68 ) );
        fh = max( 12, min( 24, (int)((brc.bottom - brc.top) * 2 / 5) ) );
        font = CreateFontW( -fh, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );

        old_br = SelectObject( hdc, br );
        old_pen = SelectObject( hdc, pen );
        old_font = SelectObject( hdc, font );
        RoundRect( hdc, brc.left, brc.top, brc.right, brc.bottom, 6, 6 );
        SetTextColor( hdc, el->is_enabled ? RGB( 255, 255, 255 ) : RGB( 120, 120, 120 ) );
        SetBkMode( hdc, TRANSPARENT );
        if (label[0])
            DrawTextW( hdc, label, -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( font );
        DeleteObject( pen );
        DeleteObject( br );
        return;
    }

    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        /* Avoid double-painting CalculationResult if its inner NormalOutput TextBlock is painted */
        if (el->tpl_normal_output && el->content == &el->tpl_normal_output->IInspectable_iface)
        {
            uwp_paint_element( hdc, el->tpl_normal_output );
            return;
        }
        if (el->text[0])
        {
            RECT trc = el->bounds;
            BOOL is_result = (!wcscmp( el->name, L"NormalOutput" ) || !wcscmp( el->name, L"Results" ) ||
                              wcsstr( el->class_name, L"CalculationResult" ) != NULL);
            int fh = is_result
                ? max( 20, min( 56, (int)((trc.bottom - trc.top) * 3 / 5) ) )
                : max( 12, min( 22, (int)el->font_size ) );
            HFONT font = CreateFontW( -fh, 0, 0, 0, is_result ? FW_SEMIBOLD : FW_NORMAL,
                                      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            SetTextColor( hdc, RGB( 255, 255, 255 ) );
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
    struct uwp_element *root_el;

    if (width <= 0 || height <= 0 || !hdc) return;
    SetRect( &rc, 0, 0, width, height );
    bg_brush = CreateSolidBrush( RGB( 32, 32, 32 ) );
    FillRect( hdc, &rc, bg_brush );
    DeleteObject( bg_brush );

    root_el = uwp_elem_from_any( content );
    if (root_el)
    {
        uwp_arrange_element( root_el, rc );
        uwp_paint_element( hdc, root_el );
    }
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

    if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        if (!el->text[0] && (wcsstr( el->name, L"CopyButton" ) || wcsstr( el->name, L"PasteButton" )))
            return NULL;
        return el;
    }
    return NULL;
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
        void *cmd_iface = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( btn->command, &IID_ICommand_UWP, &cmd_iface )) && cmd_iface)
        {
            /* Slot 8: CanExecute(object, boolean*), Slot 9: Execute(object) */
            HRESULT (WINAPI *exec_fn)(void *, void *) = (void *)(*(void ***)cmd_iface)[9];
            if (exec_fn) exec_fn( cmd_iface, btn->command_param );
            IUnknown_Release( (IUnknown *)cmd_iface );
        }
        else
        {
            /* Direct vtbl call if already on ICommand face */
            HRESULT (WINAPI *exec_fn)(void *, void *) = (void *)(*(void ***)btn->command)[9];
            if (exec_fn) exec_fn( btn->command, btn->command_param );
        }
    }

    /* 2. Invoke registered Click handlers */
    for (i = 0; i < btn->click_cnt; i++)
    {
        if (btn->click_handlers[i])
        {
            IInspectable *sender = btn->outer ? btn->outer : &btn->IInspectable_iface;
            HRESULT (WINAPI *invoke)(void *, void *, void *) = (void *)(*(void ***)btn->click_handlers[i])[3];
            invoke( btn->click_handlers[i], sender, &g_dummy_routed_args );
        }
    }

    uwp_invalidate_window();
}

static struct uwp_element *g_pressed_btn = NULL;

BOOL uwp_handle_mouse_input( HWND hwnd, UINT msg, int x, int y, int width, int height, IInspectable *root_content )
{
    struct uwp_element *root_el = uwp_elem_from_any( root_content );
    if (!root_el) return FALSE;

    if (!root_el->has_bounds && width > 0 && height > 0)
    {
        RECT rc = { 0, 0, width, height };
        uwp_arrange_element( root_el, rc );
    }

    if (msg == WM_LBUTTONDOWN)
    {
        struct uwp_element *btn = uwp_hit_test_button( root_el, x, y );
        if (btn)
        {
            g_pressed_btn = btn;
            btn->is_pressed = TRUE;
            uwp_invoke_button( btn );
            btn->is_pressed = FALSE;
            return TRUE;
        }
    }
    else if (msg == WM_LBUTTONUP)
    {
        if (g_pressed_btn)
        {
            g_pressed_btn->is_pressed = FALSE;
            g_pressed_btn = NULL;
            uwp_invalidate_window();
            return TRUE;
        }
    }
    return FALSE;
}

/* ============================================================================
 * 8. UWP Helper & Statics Factories (XamlBindingHelper, VisualStateManager, etc.)
 * ============================================================================ */

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
        IsEqualGUID( iid, &IID_IXamlBindingHelperStatics_UWP ))
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

struct uwp_class_factory
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IComposableFactory_iface;
    IInspectable IStatics_iface;
    const WCHAR *class_name;
};

static inline struct uwp_class_factory *uwp_cf_from_any( void *iface )
{
    int i;
    extern struct uwp_class_factory g_dyn_factories[64];
    for (i = 0; i < 64; i++)
    {
        if ((char *)iface >= (char *)&g_dyn_factories[i] &&
            (char *)iface < (char *)&g_dyn_factories[i + 1])
            return &g_dyn_factories[i];
    }
    return NULL;
}

static HRESULT WINAPI uwp_cf_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    struct uwp_class_factory *f = uwp_cf_from_any( iface );
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
    if (IsEqualGUID( iid, &IID_IFrameFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPageFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUserControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IControlFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObjectFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IButtonBaseFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IToggleButtonFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IMarkupExtensionFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgsFactory_UWP ))
    {
        *out = f ? (void *)&f->IComposableFactory_iface : (void *)iface;
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
    TRACE( "uwp_cf_CreateInstance(%s, outer=%p, inner=%p, inst=%p)\n", debugstr_w(cls), outer, inner, instance );
    return uwp_element_create( cls, outer, inner, instance );
}

static HRESULT WINAPI uwp_cf_dp_prop( void *iface, void **out )
{
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

static const void *uwp_cf_comp_vtbl[16] =
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
    [10 ... 15] = uwp_generic_noop,
};

static const void *uwp_cf_statics_vtbl[48] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6 ... 47] = uwp_cf_dp_prop,
};

/* VisualStateManagerStatics (slot 6 = GoToState(Control, HSTRING, bool, bool*), slot 7 = GetVisualStateGroups) */
static HRESULT WINAPI vsm_GoToState( void *iface, void *ctrl, HSTRING state, boolean use_trans, boolean *ret )
{
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
static struct uwp_class_factory vsm_factory_obj = { {(const struct IActivationFactoryVtbl *)vsm_vtbl}, {NULL}, {NULL}, L"Windows.UI.Xaml.VisualStateManager" };

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
    { {(const struct IActivationFactoryVtbl *)attached_statics_vtbl}, {NULL}, {NULL}, L"Windows.UI.Xaml.Automation.AutomationProperties" };

struct uwp_class_factory g_dyn_factories[64];
static WCHAR g_dyn_factory_names[64][128];
static int g_dyn_factory_cnt = 0;

HRESULT uwp_get_activation_factory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buf = WindowsGetStringRawBuffer( classid, NULL );
    int i;

    if (!buf || !factory) return E_NOINTERFACE;

    if (!wcscmp( buf, L"Windows.UI.Xaml.Markup.XamlBindingHelper" ) ||
        !wcscmp( buf, L"Microsoft.UI.Xaml.Markup.XamlBindingHelper" ))
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
    if (!wcscmp( buf, L"Windows.UI.Xaml.Automation.AutomationProperties" ) ||
        !wcscmp( buf, L"Windows.UI.Xaml.Controls.ToolTipService" ))
    {
        *factory = &auto_props_factory_obj.IActivationFactory_iface;
        return S_OK;
    }

    /* For Calculator (UWP), serve all Windows.UI.Xaml.* runtime classes with our UWP multi-iface element factory */
    if (is_calculator_app() && !wcsncmp( buf, L"Windows.UI.Xaml.", 16 ) &&
        wcscmp( buf, L"Windows.UI.Xaml.Application" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Window" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.PropertyMetadata" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.DependencyProperty" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.ResourceDictionary" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.PropertyPath" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.SizeHelper" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Data.PropertyChangedEventArgs" ) != 0 &&
        wcscmp( buf, L"Windows.UI.Xaml.Data.CollectionViewSource" ) != 0)
    {
        for (i = 0; i < g_dyn_factory_cnt; i++)
        {
            if (!wcscmp( g_dyn_factory_names[i], buf ))
            {
                *factory = &g_dyn_factories[i].IActivationFactory_iface;
                return S_OK;
            }
        }
        if (g_dyn_factory_cnt < 64)
        {
            int idx = g_dyn_factory_cnt++;
            lstrcpynW( g_dyn_factory_names[idx], buf, 128 );
            g_dyn_factories[idx].IActivationFactory_iface.lpVtbl = (const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl;
            g_dyn_factories[idx].IComposableFactory_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cf_comp_vtbl;
            g_dyn_factories[idx].IStatics_iface.lpVtbl = (const struct IInspectableVtbl *)uwp_cf_statics_vtbl;
            g_dyn_factories[idx].class_name = g_dyn_factory_names[idx];
            *factory = &g_dyn_factories[idx].IActivationFactory_iface;
            return S_OK;
        }
    }

    return E_NOINTERFACE;
}



