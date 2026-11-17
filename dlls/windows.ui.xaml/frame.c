/* WinRT Windows.UI.Xaml.Controls.Frame Implementation
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

#include <stdarg.h>
#include <stdio.h>

#include "private.h"
#include "winreg.h"
#include "oleauto.h"
#include "weakreference.h"

#include "wine/exception.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

const GUID IID_IReferenceTrackerTarget_WinUI =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
const GUID IID_IReferenceTracker =
    { 0xae927363, 0x74e4, 0x47e7, { 0xab, 0x5e, 0x6e, 0x97, 0xd6, 0xc2, 0x88, 0x5b } };

/* Windows.UI.Xaml.Controls.Frame interface GUIDs */
static const GUID IID_IFrameFactory =
    { 0x02ee93d4, 0x448e, 0x469e, { 0x97, 0x99, 0x0a, 0x8a, 0x1f, 0x70, 0xf1, 0x71 } };
static const GUID IID_IPageFactory =
    { 0xdfa149ac, 0x1849, 0x445e, { 0x93, 0x7c, 0x40, 0xa9, 0x59, 0x0c, 0xc0, 0x76 } };
static const GUID IID_IFrame =
    { 0x68ea500b, 0x3fe9, 0x4735, { 0xb6, 0xa0, 0xe7, 0x60, 0x19, 0x33, 0xb0, 0x89 } };
static const GUID IID_IGridFactory =
    { 0xb16bf561, 0xfc6c, 0x57c6, { 0x8e, 0xbc, 0x0b, 0x06, 0xce, 0x45, 0x13, 0xaa } };
static const GUID IID_IContentControlFactory =
    { 0x3dea958e, 0x5acd, 0x5f80, { 0x89, 0x38, 0x38, 0x63, 0x4f, 0x51, 0x49, 0x3a } };
static const GUID IID_IGridStatics =
    { 0xef9cf81d, 0xa431, 0x50f4, { 0xab, 0xf5, 0x30, 0x23, 0xfe, 0x44, 0x77, 0x04 } };
static const GUID IID_INavigate =
    { 0xe7337f7a, 0x5e13, 0x4d32, { 0xb5, 0x72, 0x89, 0x35, 0x6e, 0x98, 0x1d, 0x96 } };
static const GUID IID_IDependencyObject_g =
    { 0x5c526665, 0xf60e, 0x4912, { 0xaf, 0x59, 0x5f, 0xe0, 0x68, 0x0f, 0x08, 0x9d } };
static const GUID IID_IUIElement_g =
    { 0x676d0be9, 0xb65c, 0x41c6, { 0xba, 0x40, 0x58, 0xcf, 0x87, 0xf2, 0x01, 0xc1 } };
static const GUID IID_IFrameworkElement =
    { 0xa391d09b, 0x4a99, 0x4b7c, { 0x9d, 0x8d, 0x6f, 0xa5, 0xd0, 0x1f, 0x6f, 0xbf } };
static const GUID IID_IContentControl =
    { 0xa26dd1dc, 0xcd44, 0x435c, { 0xbe, 0x94, 0x01, 0xd6, 0x24, 0x1c, 0x23, 0x1c } };
static const GUID IID_IControl_g =
    { 0xedf04eb1, 0x37d1, 0x471f, { 0x85, 0x70, 0x38, 0x29, 0xee, 0x5b, 0x2b, 0xc6 } };
static const GUID IID_IGrid =
    { 0xc4496219, 0x9014, 0x58a1, { 0xb4, 0xad, 0xc5, 0x04, 0x49, 0x13, 0xa5, 0xbb } };
/* WinUI3 (Microsoft.UI.Xaml.Controls.Grid) interface versions asked by the
 * .NET 8 projection when casting RootVisual children to Grid / app views. */
static const GUID IID_IGrid_WinUI =
    { 0xad401812, 0xb091, 0x51d0, { 0xb9, 0x15, 0x2d, 0x68, 0x2c, 0xd2, 0xaf, 0x10 } };
static const GUID IID_IGrid_WinUI2 =
    { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };
static const GUID IID_IGrid_v1 =
    { 0xf6790987, 0xe6e5, 0x47f2, { 0x92, 0xc6, 0xec, 0xcc, 0xe4, 0xba, 0x15, 0x9a } };
static const GUID IID_IView_IV1 =
    { 0x9e5f3ed0, 0xcf1c, 0x5d38, { 0x83, 0x2c, 0xac, 0xea, 0x61, 0x64, 0xbf, 0x5c } };
static const GUID IID_IView_IV2 =
    { 0x90c71c29, 0xa9b5, 0x5267, { 0xa5, 0xad, 0x8b, 0x75, 0x67, 0x36, 0x31, 0x7c } };
static const GUID IID_IPanel =
    { 0x27a1b418, 0x56f3, 0x525e, { 0xb8, 0x83, 0xce, 0xfe, 0xd9, 0x05, 0xee, 0xd3 } };
static const GUID IID_IRowDefinition =
    { 0xfe870f2f, 0x89ef, 0x5dac, { 0x9f, 0x33, 0x96, 0x8d, 0x0d, 0xc5, 0x77, 0xc3 } };
static const GUID IID_IRowDefinitionStatics =
    { 0x7317c7c5, 0xfd6f, 0x58d9, { 0x9b, 0x53, 0x49, 0x78, 0xdb, 0xac, 0xde, 0x23 } };
static const GUID IID_IColumnDefinition =
    { 0x454cea14, 0x87ec, 0x5890, { 0xbb, 0x62, 0xf1, 0xd8, 0x2a, 0x94, 0x75, 0x8e } };
static const GUID IID_IColumnDefinitionStatics =
    { 0xaba4b755, 0xca63, 0x5ed7, { 0xae, 0x31, 0x45, 0xf9, 0xf9, 0x19, 0x20, 0xb0 } };
const GUID IID_IReferenceTrackerTarget =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
const GUID IID_IWeakReferenceSource =
    { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_IWeakReference =
    { 0x00000037, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IUIElement_WinUI =
    { 0xc3c01020, 0x320c, 0x5cf6, { 0x9d, 0x24, 0xd3, 0x96, 0xbb, 0xfa, 0x4d, 0x8b } };
static const GUID IID_IFrameworkElement_WinUI =
    { 0xfe08f13d, 0xdc6a, 0x5495, { 0xad, 0x44, 0xc2, 0xd8, 0xd2, 0x18, 0x63, 0xb0 } };
static const GUID IID_IControl_WinUI =
    { 0x857d6e8a, 0xd45a, 0x5c69, { 0xa9, 0x9c, 0xbf, 0x6a, 0x5c, 0x54, 0xfb, 0x38 } };
static const GUID IID_IContentControl_WinUI =
    { 0x07e81761, 0x11b2, 0x52ae, { 0x8f, 0x8b, 0x4d, 0x53, 0xd2, 0xb5, 0x90, 0x0a } };
static const GUID IID_IDependencyObject_WinUI =
    { 0xe7beaee7, 0x160e, 0x50f7, { 0x87, 0x89, 0xd6, 0x34, 0x63, 0xf9, 0x79, 0xfa } };
static const GUID IID_IUIElementCollection =
    { 0x23050cb1, 0xdb88, 0x54ed, { 0x90, 0x83, 0x5e, 0xcf, 0xb1, 0x25, 0x12, 0xfd } };
static const GUID IID_IImage =
    { 0x220d3d8d, 0x66de, 0x53a1, { 0xa2, 0x15, 0xba, 0x9c, 0x16, 0x55, 0x65, 0xab } };
static const GUID IID_IUIElementOverrides =
    { 0x9034f41e, 0xab7b, 0x59e7, { 0x81, 0x68, 0x50, 0xde, 0x6b, 0x68, 0x9d, 0xde } };
static const GUID IID_IFrameworkElementOverrides =
    { 0xffc6fd98, 0xf38c, 0x5904, { 0x9c, 0xe4, 0x97, 0xa3, 0x42, 0x7c, 0xf4, 0xba } };
static const GUID IID_IWebView2Factory =
    { 0xfb4ec2ce, 0x3074, 0x5c42, { 0xb6, 0x55, 0x64, 0xfa, 0x72, 0x45, 0xb1, 0xc6 } };
static const GUID IID_IWebView2Factory2 =
    { 0xfb4ec2ce, 0x3074, 0x5c42, { 0xb6, 0x55, 0x64, 0xfb, 0x81, 0xfb, 0xd0, 0x40 } };
static const GUID IID_IWebView2Statics =
    { 0x7f3226d0, 0xf6d5, 0x5aa7, { 0xb1, 0x75, 0x64, 0x54, 0x51, 0xca, 0x3c, 0x85 } };
static const GUID IID_IUserControlFactory =
    { 0x61ac9074, 0xaab3, 0x50b8, { 0x8b, 0x18, 0xd4, 0xd2, 0x57, 0x3a, 0x52, 0x35 } };
static const GUID IID_IUserControlStatics =
    { 0x575e8fa1, 0x2e2d, 0x5857, { 0x9a, 0x93, 0x6b, 0x11, 0xe5, 0x43, 0x16, 0xba } };
static const GUID IID_IUserControl =
    { 0xab8a0a51, 0x44cf, 0x5337, { 0xad, 0x77, 0x8e, 0xda, 0x17, 0x95, 0xe9, 0xf6 } };
static const GUID IID_IRectangle =
    { 0xbf7d30b9, 0x152c, 0x556e, { 0x9f, 0x10, 0xd0, 0xb7, 0xeb, 0xa4, 0xe5, 0x2f } };
static const GUID IID_IShape =
    { 0x9941aad3, 0x6af2, 0x5ba2, { 0x90, 0x85, 0x85, 0x06, 0xd5, 0xf2, 0x48, 0x5e } };
static const GUID IID_IViewbox =
    { 0x510683e8, 0xd0fe, 0x5ef4, { 0x85, 0xbd, 0xe1, 0x13, 0x10, 0x76, 0xac, 0x22 } };
static const GUID IID_ITextBlock =
    { 0x1ac8d84f, 0x392c, 0x5c7e, { 0x83, 0xf5, 0xa5, 0x3e, 0x3b, 0xf0, 0xab, 0xb0 } };
static const GUID IID_IBorder =
    { 0x1ca13b47, 0xff5c, 0x5abc, { 0xa4, 0x11, 0xa1, 0x77, 0xdf, 0x94, 0x82, 0xa9 } };
static const GUID IID_IStackPanel =
    { 0x493ab00b, 0x3a6a, 0x5e4a, { 0x94, 0x52, 0x40, 0x7c, 0xd5, 0x19, 0x74, 0x06 } };
static const GUID IID_IButtonFactory =
    { 0xfe393422, 0xd91c, 0x57b1, { 0x9a, 0x9c, 0x2c, 0x7e, 0x3f, 0x41, 0xf7, 0x7c } };
static const GUID IID_IButton =
    { 0x216c183d, 0xd07a, 0x5aa5, { 0xb8, 0xa4, 0x03, 0x00, 0xa2, 0x68, 0x3e, 0x87 } };
static const GUID IID_IButtonBase =
    { 0x65714269, 0x2473, 0x5327, { 0xa6, 0x52, 0x0e, 0xa6, 0xbc, 0xe7, 0xf4, 0x03 } };
static const GUID IID_IStyle =
    { 0x65e1d164, 0x572f, 0x5b0e, { 0xa8, 0x0f, 0x9c, 0x02, 0x44, 0x1f, 0xac, 0x49 } };
static const GUID IID_ICanvasStatics =
    { 0x40ce5c46, 0x2962, 0x446f, { 0xaa, 0xfb, 0x4c, 0xdc, 0x48, 0x69, 0x39, 0xc9 } };
static const GUID IID_ICanvasFactory =
    { 0x005ac3d1, 0xb031, 0x58ab, { 0x91, 0x8d, 0x03, 0x0f, 0xab, 0xae, 0xaf, 0x87 } };
static const GUID IID_ICanvas =
    { 0x70e05ac4, 0xd38d, 0x4bab, { 0x83, 0x1f, 0x4a, 0x22, 0xef, 0x52, 0xac, 0x86 } };
static const GUID IID_IComponentConnector_UWP =
    { 0xf6790987, 0xe6e5, 0x47f2, { 0x92, 0xc6, 0xec, 0xcc, 0xe4, 0xba, 0x15, 0x9a } };
static const GUID IID_IComponentConnector_x =
    { 0xad401812, 0xb091, 0x51d0, { 0xb9, 0x15, 0x2d, 0x68, 0x2c, 0xd2, 0xaf, 0x10 } };
static const GUID IID_IDataTemplate =
    { 0x9910aec7, 0x8ab5, 0x4118, { 0x9b, 0xc6, 0x09, 0xf4, 0x5a, 0x35, 0x07, 0x3d } };
static const GUID IID_IProgressRing =
    { 0x2670d03f, 0xe28c, 0x5652, { 0xbe, 0xe2, 0xb5, 0x21, 0x2e, 0xbd, 0xf7, 0xff } };
static const GUID IID_IImage_WinUI =
    { 0x495b7402, 0x9af3, 0x4e50, { 0xaa, 0x90, 0x03, 0x38, 0x8f, 0x30, 0x86, 0xd2 } };
static const GUID IID_IBrush_local =
    { 0x8806a321, 0x1e06, 0x422c, { 0xa1, 0xcc, 0x01, 0x69, 0x65, 0x59, 0xe0, 0x21 } };
static const GUID IID_ISolidColorBrush_local =
    { 0x9d850850, 0x66f3, 0x48df, { 0x9a, 0x8f, 0x82, 0x4b, 0xd5, 0xe0, 0x70, 0xaf } };
static const GUID IID_IStyle_local =
    { 0xc4a9f225, 0x9db7, 0x4a7d, { 0xb6, 0xd1, 0xf7, 0x4e, 0xdb, 0x92, 0x93, 0xc2 } };



static inline struct xaml_frame *impl_from_frame( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_frame, IInspectable_iface );
}

static HRESULT WINAPI frame_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_frame *impl = impl_from_frame( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IDependencyObject_g ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ))
    {
        *out = &impl->IDependencyObject_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFrame ) ||
        IsEqualGUID( iid, &IID_INavigate ) ||
        IsEqualGUID( iid, &IID_IUIElement_g ) ||
        IsEqualGUID( iid, &IID_IUIElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IContentControl ) ||
        IsEqualGUID( iid, &IID_IContentControl_WinUI ) ||
        IsEqualGUID( iid, &IID_IUserControl ) ||
        IsEqualGUID( iid, &IID_IControl_g ) ||
        IsEqualGUID( iid, &IID_IControl_WinUI ) ||
        IsEqualGUID( iid, &IID_IGrid ) ||
        IsEqualGUID( iid, &IID_IPanel ) ||
        IsEqualGUID( iid, &IID_ICanvas ) ||
        IsEqualGUID( iid, &IID_IStackPanel ) ||
        IsEqualGUID( iid, &IID_IBorder ) ||
        IsEqualGUID( iid, &IID_IButton ) ||
        IsEqualGUID( iid, &IID_IButtonBase ) ||
        IsEqualGUID( iid, &IID_IRowDefinition ) ||
        IsEqualGUID( iid, &IID_IColumnDefinition ) ||
        IsEqualGUID( iid, &IID_IDataTemplate ) ||
        IsEqualGUID( iid, &IID_IProgressRing ) ||
        IsEqualGUID( iid, &IID_IImage_WinUI ) ||
        IsEqualGUID( iid, &IID_IStyle ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDCompositionVisual ) ||
        IsEqualGUID( iid, &IID_IDCompositionVisual2 ))
    {
        if (impl->visual)
        {
            *out = impl->visual;
            IDCompositionVisual_AddRef( impl->visual );
            return S_OK;
        }
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI frame_AddRef( IInspectable *iface )
{
    struct xaml_frame *impl = impl_from_frame( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI frame_Release( IInspectable *iface )
{
    struct xaml_frame *impl = impl_from_frame( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p ref %lu.\n", iface, ref );
    if (!ref)
    {
        int i;
        for (i = 0; i < impl->prop_count; i++)
            if (impl->props[i].value) IInspectable_Release( impl->props[i].value );
        if (impl->visual) IDCompositionVisual_Release( impl->visual );
        if (impl->content) IInspectable_Release( impl->content );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI frame_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI frame_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.Frame", 30, name );
}

static HRESULT WINAPI frame_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI frame_get_Content( void *iface, void **value )
{
    struct xaml_frame *impl = impl_from_frame( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->content;
    if (*value) IInspectable_AddRef( (IInspectable *)*value );
    return S_OK;
}

static HRESULT WINAPI frame_put_Content( void *iface, void *value )
{
    struct xaml_frame *impl = impl_from_frame( iface );
    TRACE( "iface %p, value %p\n", iface, value );
    if (impl->content)
    {
        if (impl->visual) IDCompositionVisual_RemoveAllVisuals( impl->visual );
        IInspectable_Release( impl->content );
    }
    impl->content = (IInspectable *)value;
    if (impl->content)
    {
        IInspectable_AddRef( impl->content );
        if (impl->visual)
        {
            IDCompositionVisual *child_visual = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( impl->content, &IID_IDCompositionVisual, (void **)&child_visual )))
            {
                IDCompositionVisual_AddVisual( impl->visual, child_visual, TRUE, NULL );
                IDCompositionVisual_Release( child_visual );
            }
        }
    }
    return S_OK;
}

static HRESULT WINAPI frame_stub_zero( void *iface, void *out )
{
    return S_OK;
}

static HRESULT WINAPI frame_stub_ok( void *iface, ... )
{
    return S_OK;
}

static HRESULT WINAPI frame_event_add( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI frame_event_remove( void *iface, EventRegistrationToken token )
{
    return S_OK;
}

static HRESULT WINAPI frame_NavigateWithParam( void *iface, void *type_name_ptr, void *param, boolean *result )
{
    struct xaml_frame *impl = impl_from_frame( iface );
    TypeName *type = (TypeName *)type_name_ptr;
    const WCHAR *name = NULL;

    if (type && type->Name) name = WindowsGetStringRawBuffer( type->Name, NULL );
    TRACE( "iface %p, type %s, param %p, result %p\n",
           iface, debugstr_w( name ), param, result );

    if (name && !impl->content)
    {
        HRESULT hr = xaml_create_page( type_name_ptr, &impl->content );
        TRACE( "xaml_create_page(%s) returned hr %#lx, content %p\n", debugstr_w( name ), hr, impl->content );
        if (SUCCEEDED(hr) && impl->content && impl->visual)
        {
            IDCompositionVisual *page_visual = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( impl->content, &IID_IDCompositionVisual, (void **)&page_visual )))
            {
                IDCompositionVisual_AddVisual( impl->visual, page_visual, TRUE, NULL );
                IDCompositionVisual_Release( page_visual );
            }
        }
        xaml_window_present_current();
    }

    if (result) *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI frame_Navigate( void *iface, void *type_name_ptr, boolean *result )
{
    return frame_NavigateWithParam( iface, type_name_ptr, NULL, result );
}

static HRESULT WINAPI frame_generic_slot( void *iface, ... )
{
    ERR( "frame generic slot called on %p\n", iface );
    return S_OK;
}

static const void *frame_vtbl[] =
{
    frame_QueryInterface,         /* 0: QI */
    frame_AddRef,                 /* 1: AddRef */
    frame_Release,                /* 2: Release */
    frame_GetIids,                /* 3: GetIids */
    frame_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    frame_GetTrustLevel,          /* 5: GetTrustLevel */
    frame_get_Content,            /* 6: get_Content */
    frame_put_Content,            /* 7: put_Content */
    frame_stub_zero,              /* 8: get_CacheSize */
    frame_stub_ok,                /* 9: put_CacheSize */
    frame_stub_zero,              /* 10: get_CanGoBack */
    frame_stub_zero,              /* 11: get_CanGoForward */
    frame_event_add,              /* 12: add_Navigated */
    frame_event_remove,           /* 13: remove_Navigated */
    frame_event_add,              /* 14: add_Navigating */
    frame_event_remove,           /* 15: remove_Navigating */
    frame_event_add,              /* 16: add_NavigationFailed */
    frame_event_remove,           /* 17: remove_NavigationFailed */
    frame_event_add,              /* 18: add_NavigationStopped */
    frame_event_remove,           /* 19: remove_NavigationStopped */
    frame_stub_zero,              /* 20: get_SourcePageType */
    frame_stub_ok,                /* 21: put_SourcePageType */
    frame_stub_zero,              /* 22: get_CurrentSourcePageType */
    frame_Navigate,               /* 23: Navigate */
    frame_NavigateWithParam,      /* 24: NavigateWithParam */
    frame_stub_ok,                /* 25: GoBack */
    frame_stub_ok,                /* 26: GoForward */
    frame_stub_zero,              /* 27: GetNavigationState */
    frame_stub_ok,                /* 28: SetNavigationState */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
};

static inline struct xaml_frame *impl_from_depobj( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_frame, IDependencyObject_iface );
}

static HRESULT WINAPI frame_do_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_frame *impl = impl_from_depobj( iface );
    return frame_QueryInterface( &impl->IInspectable_iface, iid, out );
}

static ULONG WINAPI frame_do_AddRef( void *iface )
{
    struct xaml_frame *impl = impl_from_depobj( iface );
    return frame_AddRef( &impl->IInspectable_iface );
}

static ULONG WINAPI frame_do_Release( void *iface )
{
    struct xaml_frame *impl = impl_from_depobj( iface );
    return frame_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI frame_do_GetIids( void *iface, ULONG *count, IID **iids )
{
    return frame_GetIids( NULL, count, iids );
}

static HRESULT WINAPI frame_do_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return frame_GetRuntimeClassName( NULL, name );
}

static HRESULT WINAPI frame_do_GetTrustLevel( void *iface, TrustLevel *level )
{
    return frame_GetTrustLevel( NULL, level );
}

static HRESULT WINAPI frame_do_GetValue( void *iface, void *dp, IInspectable **value )
{
    struct xaml_frame *impl = impl_from_depobj( iface );
    int i;

    TRACE( "iface %p, dp %p, value %p.\n", iface, dp, value );
    if (!value) return E_POINTER;
    *value = NULL;
    for (i = 0; i < impl->prop_count; i++)
    {
        if (impl->props[i].dp == dp)
        {
            *value = impl->props[i].value;
            if (*value) IInspectable_AddRef( *value );
            return S_OK;
        }
    }
    return S_OK;
}

static HRESULT WINAPI frame_do_SetValue( void *iface, void *dp, IInspectable *value )
{
    struct xaml_frame *impl = impl_from_depobj( iface );
    int i;

    TRACE( "iface %p, dp %p, value %p.\n", iface, dp, value );
    for (i = 0; i < impl->prop_count; i++)
    {
        if (impl->props[i].dp == dp)
        {
            if (impl->props[i].value) IInspectable_Release( impl->props[i].value );
            impl->props[i].value = value;
            if (value) IInspectable_AddRef( value );
            return S_OK;
        }
    }
    if (impl->prop_count < ARRAY_SIZE(impl->props))
    {
        impl->props[impl->prop_count].dp = dp;
        impl->props[impl->prop_count].value = value;
        if (value) IInspectable_AddRef( value );
        impl->prop_count++;
    }
    return S_OK;
}

static HRESULT WINAPI frame_do_ClearValue( void *iface, void *dp )
{
    return frame_do_SetValue( iface, dp, NULL );
}

static HRESULT WINAPI frame_do_GetAnimationBaseValue( void *iface, void *dp, IInspectable **value )
{
    return frame_do_GetValue( iface, dp, value );
}

static HRESULT WINAPI frame_do_get_Dispatcher( void *iface, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}

static HRESULT WINAPI frame_do_RegisterPropertyChangedCallback( void *iface, void *dp, void *cb, INT64 *token )
{
    if (token) *token = 1;
    return S_OK;
}

static HRESULT WINAPI frame_do_UnregisterPropertyChangedCallback( void *iface, void *dp, INT64 token )
{
    return S_OK;
}

static const void *frame_depobj_vtbl[] =
{
    frame_do_QueryInterface,
    frame_do_AddRef,
    frame_do_Release,
    frame_do_GetIids,
    frame_do_GetRuntimeClassName,
    frame_do_GetTrustLevel,
    frame_do_GetValue,
    frame_do_SetValue,
    frame_do_ClearValue,
    frame_do_GetValue, /* ReadLocalValue */
    frame_do_GetAnimationBaseValue,
    frame_do_get_Dispatcher,
    frame_do_RegisterPropertyChangedCallback,
    frame_do_UnregisterPropertyChangedCallback,
};

static HRESULT frame_create( IInspectable *outer, IInspectable **inner, void **instance )
{
    struct xaml_frame *impl;

    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)frame_vtbl;
    impl->IDependencyObject_iface = frame_depobj_vtbl;
    impl->ref = 1;
    impl->outer = outer;
    xaml_create_dcomp_visual( &impl->visual );

    if (inner)
    {
        *inner = &impl->IInspectable_iface;
        IInspectable_AddRef( *inner );
    }
    *instance = &impl->IInspectable_iface;
    return S_OK;
}

/* Frame factory */
struct frame_factory
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IComposableFactory_iface;
    LONG ref;
    IInspectable IStatics_iface;
};

IDependencyProperty *wv2_source_dp;

static inline struct frame_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct frame_factory, IActivationFactory_iface );
}

static HRESULT WINAPI frame_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out );
static ULONG WINAPI frame_factory_AddRef( IActivationFactory *iface );
static ULONG WINAPI frame_factory_Release( IActivationFactory *iface );

static inline struct frame_factory *impl_from_IComposableFactory( void *iface )
{
    return CONTAINING_RECORD( iface, struct frame_factory, IComposableFactory_iface );
}

static inline struct frame_factory *impl_from_IStatics( void *iface )
{
    return CONTAINING_RECORD( iface, struct frame_factory, IStatics_iface );
}

static HRESULT WINAPI statics_factory_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct frame_factory *impl = impl_from_IStatics( iface );
    return frame_factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI statics_factory_AddRef( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IStatics( iface );
    return frame_factory_AddRef( &impl->IActivationFactory_iface );
}
static ULONG WINAPI statics_factory_Release( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IStatics( iface );
    return frame_factory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI composable_factory_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct frame_factory *impl = impl_from_IComposableFactory( iface );
    return frame_factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
static ULONG WINAPI composable_factory_AddRef( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IComposableFactory( iface );
    return frame_factory_AddRef( &impl->IActivationFactory_iface );
}
static ULONG WINAPI composable_factory_Release( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IComposableFactory( iface );
    return frame_factory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI frame_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct frame_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IWebView2Statics ) && impl->IStatics_iface.lpVtbl)
    {
        *out = &impl->IStatics_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IGridStatics ) ||
        IsEqualGUID( iid, &IID_IRowDefinitionStatics ) ||
        IsEqualGUID( iid, &IID_IColumnDefinitionStatics ) ||
        IsEqualGUID( iid, &IID_IWebView2Statics ) ||
        IsEqualGUID( iid, &IID_IUserControlStatics ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IFrameFactory ) ||
        IsEqualGUID( iid, &IID_IPageFactory ) ||
        IsEqualGUID( iid, &IID_IGridFactory ) ||
        IsEqualGUID( iid, &IID_IContentControlFactory ) ||
        IsEqualGUID( iid, &IID_IWebView2Factory ) ||
        IsEqualGUID( iid, &IID_IWebView2Factory2 ) ||
        IsEqualGUID( iid, &IID_IUserControlFactory ) ||
        IsEqualGUID( iid, &IID_IButtonFactory ))
    {
        *out = &impl->IComposableFactory_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI frame_factory_AddRef( IActivationFactory *iface )
{
    struct frame_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI frame_factory_Release( IActivationFactory *iface )
{
    struct frame_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI frame_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI frame_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.Frame", 30, name );
}

static HRESULT WINAPI frame_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI frame_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return frame_create( NULL, NULL, (void **)instance );
}

static HRESULT WINAPI frame_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    TRACE( "iface %p, outer %p, inner %p, instance %p\n", iface, outer, inner, instance );
    return frame_create( (IInspectable *)outer, (IInspectable **)inner, instance );
}

static const void *frame_factory_vtbl[] =
{
    frame_factory_QueryInterface,
    frame_factory_AddRef,
    frame_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    frame_factory_ActivateInstance,  /* slot 6: ActivateInstance */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static const void *composable_factory_vtbl[] =
{
    composable_factory_QueryInterface,     /* 0: QI */
    composable_factory_AddRef,             /* 1: AddRef */
    composable_factory_Release,            /* 2: Release */
    frame_factory_GetIids,                 /* 3: GetIids */
    frame_factory_GetRuntimeClassName,     /* 4: GetRuntimeClassName */
    frame_factory_GetTrustLevel,           /* 5: GetTrustLevel */
    frame_factory_CreateInstance,          /* 6: CreateInstance(outer, inner, instance) */
    frame_factory_CreateInstance,          /* 7 */
    frame_factory_CreateInstance,          /* 8 */
    frame_factory_CreateInstance,          /* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot,
};

static struct frame_factory frame_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)frame_factory_vtbl},
    {(const struct IInspectableVtbl *)composable_factory_vtbl},
    1,
};

IActivationFactory *frame_factory = &frame_factory_obj.IActivationFactory_iface;

/* Windows.UI.Xaml.Controls.Page */
struct xaml_page
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *outer;
    IInspectable *content;
    IDCompositionVisual *visual;
};

static inline struct xaml_page *impl_from_page( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_page, IInspectable_iface );
}

static HRESULT WINAPI page_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_page *impl = impl_from_page( iface );
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFrame ) ||
        IsEqualGUID( iid, &IID_INavigate ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_g ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ) ||
        IsEqualGUID( iid, &IID_IUIElement_g ) ||
        IsEqualGUID( iid, &IID_IUIElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IContentControl ) ||
        IsEqualGUID( iid, &IID_IContentControl_WinUI ) ||
        IsEqualGUID( iid, &IID_IUserControl ) ||
        IsEqualGUID( iid, &IID_IControl_g ) ||
        IsEqualGUID( iid, &IID_IControl_WinUI ) ||
        IsEqualGUID( iid, &IID_IGrid ) ||
        IsEqualGUID( iid, &IID_IPanel ) ||
        IsEqualGUID( iid, &IID_ICanvas ) ||
        IsEqualGUID( iid, &IID_IStackPanel ) ||
        IsEqualGUID( iid, &IID_IBorder ) ||
        IsEqualGUID( iid, &IID_IButton ) ||
        IsEqualGUID( iid, &IID_IButtonBase ) ||
        IsEqualGUID( iid, &IID_IRowDefinition ) ||
        IsEqualGUID( iid, &IID_IColumnDefinition ) ||
        IsEqualGUID( iid, &IID_IDataTemplate ) ||
        IsEqualGUID( iid, &IID_IProgressRing ) ||
        IsEqualGUID( iid, &IID_IImage_WinUI ) ||
        IsEqualGUID( iid, &IID_IStyle ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDCompositionVisual ) ||
        IsEqualGUID( iid, &IID_IDCompositionVisual2 ))
    {
        if (impl->visual)
        {
            *out = impl->visual;
            IDCompositionVisual_AddRef( impl->visual );
            return S_OK;
        }
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI page_AddRef( IInspectable *iface )
{
    struct xaml_page *impl = impl_from_page( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI page_Release( IInspectable *iface )
{
    struct xaml_page *impl = impl_from_page( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->visual) IDCompositionVisual_Release( impl->visual );
        if (impl->content) IInspectable_Release( impl->content );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI page_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI page_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.Page", 29, name );
}

static HRESULT WINAPI page_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static const void *page_vtbl[] =
{
    page_QueryInterface,         /* 0: QI */
    page_AddRef,                 /* 1: AddRef */
    page_Release,                /* 2: Release */
    page_GetIids,                /* 3: GetIids */
    page_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    page_GetTrustLevel,          /* 5: GetTrustLevel */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
};

static HRESULT page_create( IInspectable *outer, IInspectable **inner, void **instance )
{
    struct xaml_page *impl;

    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)page_vtbl;
    impl->ref = 1;
    impl->outer = outer;
    xaml_create_dcomp_visual( &impl->visual );

    if (inner)
    {
        *inner = &impl->IInspectable_iface;
        IInspectable_AddRef( *inner );
    }
    *instance = &impl->IInspectable_iface;
    return S_OK;
}

/* Page factory */
struct page_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct page_factory *impl_from_page_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct page_factory, IActivationFactory_iface );
}

static HRESULT WINAPI page_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct page_factory *impl = impl_from_page_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IPageFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI page_factory_AddRef( IActivationFactory *iface )
{
    struct page_factory *impl = impl_from_page_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI page_factory_Release( IActivationFactory *iface )
{
    struct page_factory *impl = impl_from_page_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI page_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI page_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.Page", 29, name );
}

static HRESULT WINAPI page_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI page_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return page_create( NULL, NULL, (void **)instance );
}

static HRESULT WINAPI page_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    TRACE( "iface %p, outer %p, inner %p, instance %p\n", iface, outer, inner, instance );
    return page_create( (IInspectable *)outer, (IInspectable **)inner, instance );
}

static const void *page_factory_vtbl[] =
{
    page_factory_QueryInterface,
    page_factory_AddRef,
    page_factory_Release,
    page_factory_GetIids,
    page_factory_GetRuntimeClassName,
    page_factory_GetTrustLevel,
    page_factory_CreateInstance,    /* slot 6: CreateInstance */
    page_factory_CreateInstance,    /* slot 7 */
    page_factory_ActivateInstance,  /* slot 8: ActivateInstance */
    page_factory_CreateInstance,    /* slot 9 */
    page_factory_CreateInstance,    /* slot 10 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static struct page_factory page_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)page_factory_vtbl},
    1,
};

IActivationFactory *page_factory = &page_factory_obj.IActivationFactory_iface;

/* Generic collection for RowDefinitions, ColumnDefinitions, Children */
struct xaml_collection
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable **items;
    unsigned int count;
    unsigned int capacity;
    void *iterable_view;  /* cached IIterable<RowDefinition> view for IV {5ddd9577} */
    void *vector_view;    /* cached IVectorView<RowDefinition> for IV3 {94ea2b94} */
    void *parent;         /* owner collection of a view shard (back-pointer) */
};

static inline struct xaml_collection *impl_from_collection( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_collection, IInspectable_iface );
}

/* WinUI3 RowDefinitionCollection enumerable IV (IIterable<RowDefinition> dephased view) */
static const GUID IID_IIterable_RowDef =
    { 0x5ddd9577, 0x3f94, 0x567f, { 0xbe, 0xef, 0x54, 0x05, 0x68, 0x52, 0x22, 0x89 } };

/* WinUI3 RowDefinitionCollection IV3 = IVectorView<RowDefinition> (GetMany at slot 10) */
static const GUID IID_IVectorView_RowDef =
    { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

static const void *collection_view_vtbl[256];
void **vtbl_dup( const void *src );
static HRESULT collection_iterable_view( struct xaml_collection *coll, void **out );

static HRESULT WINAPI collection_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_collection *impl = impl_from_collection( iface );

    ERR( "collection_QueryInterface %p %s\n", iface, debugstr_guid(iid) );
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IIterable_RowDef ))
        return collection_iterable_view( impl, out );

    if (IsEqualGUID( iid, &IID_IVectorView_RowDef ))
    {
        struct xaml_collection *v = impl->vector_view;
        if (!v)
        {
            if (!(v = calloc( 1, sizeof(*v) ))) return E_OUTOFMEMORY;
            *v = *impl;                                   /* shallow: items/count shared */
            v->ref = 1;
            v->capacity = ~0u;                            /* sentinel: view owns no array */
            v->vector_view = v;
            v->iterable_view = NULL;
            v->parent = impl;
            v->IInspectable_iface.lpVtbl =
                (const struct IInspectableVtbl *)vtbl_dup( collection_view_vtbl );
            impl->vector_view = v;
            ERR( "collection QI %p IV3 vectorview created %p\n", iface, v );
        }
        else InterlockedIncrement( &v->ref );
        *out = (IInspectable *)v;
        return S_OK;
    }

    *out = &impl->IInspectable_iface;
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI collection_AddRef( IInspectable *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    ERR( "collection_AddRef iface %p ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI collection_Release( IInspectable *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        unsigned int i;
        if (impl->capacity == ~0u)                        /* IV3 view shard: array shared */
        {
            ERR( "collection_Release view %p freed\n", iface );
            if (impl->vector_view == impl) impl->vector_view = NULL;
            if (impl->parent)                             /* drop parent's stale cache slot */
            {
                struct xaml_collection *p = impl->parent;
                if (p->vector_view == (void *)impl) p->vector_view = NULL;
            }
            free( impl );
            return 0;
        }
        InterlockedIncrement( &impl->ref );               /* leak-keep: GC-strand guard */
        return 1;
        for (i = 0; i < impl->count; i++)
            if (impl->items[i]) IInspectable_Release( impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI collection_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI collection_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Foundation.Collections.IVector`1<Windows.UI.Xaml.Controls.RowDefinition>";
    ERR( "collection_GetRuntimeClassName iface %p\n", iface );
    return WindowsCreateString( nm, ARRAYSIZE(nm) - 1, name );
}

static HRESULT WINAPI collection_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    ERR( "collection_GetTrustLevel iface %p\n", iface );
    if (level) *level = BaseTrust;
    return S_OK;
}

struct xaml_iterator
{
    IInspectable IInspectable_iface;
    LONG ref;
    struct xaml_collection *coll;
    unsigned int index;
    unsigned int end;    /* snapshot of coll->count at First(): appends during enumeration stop growing */
};

static HRESULT WINAPI iterator_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    ERR( "iterator_QueryInterface %p %s\n", iface, debugstr_guid(iid) );
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    IInspectable_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI iterator_AddRef( IInspectable *iface )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    ULONG ref = InterlockedIncrement( &impl->ref );
    ERR( "iterator_AddRef iface %p ref %lu\n", iface, ref );
    return ref;
}
static ULONG WINAPI iterator_Release( IInspectable *iface )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    ULONG ref;
    ERR( "iterator_Release iface %p\n", iface );
    ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        /* leak-keep: app caches raw ptrs in CoreRT RCWs; GC compaction
         * strands freed objects -> stale-vtbl AV in WindowEx ctor. */
        InterlockedIncrement( &impl->ref );
        return 1;
    }
    return ref;
}
static HRESULT WINAPI iterator_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    ERR( "iterator_GetIids iface %p\n", iface );
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI iterator_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Xaml.Controls.RowDefinition>";
    ERR( "iterator_GetRuntimeClassName iface %p\n", iface );
    return WindowsCreateString( nm, ARRAYSIZE(nm) - 1, name );
}
static HRESULT WINAPI iterator_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    ERR( "iterator_GetTrustLevel iface %p\n", iface );
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI iterator_get_Current( void *iface, void **current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    ERR( "iterator_get_Current iface %p current %p\n", iface, current );
    if (!current) return E_POINTER;
    if (impl->coll && impl->index < impl->end)
    {
        *current = impl->coll->items[impl->index];
        if (*current) IInspectable_AddRef( (IInspectable *)*current );
        return S_OK;
    }
    *current = NULL;
    return 0x8000000b; /* E_BOUNDS */
}

static HRESULT WINAPI iterator_get_HasCurrent( void *iface, boolean *has_current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    if (has_current) *has_current = (impl->coll && impl->index < impl->end);
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext( void *iface, boolean *has_current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    ERR( "iterator_MoveNext iface %p has %p\n", iface, has_current );
    if (impl->coll && impl->index < impl->end) impl->index++;
    if (has_current) *has_current = (impl->coll && impl->index < impl->end);
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany( void *iface, unsigned int count, void **items, unsigned int *actual )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    unsigned int n = 0;
    ERR( "iterator_GetMany iface %p count %u\n", iface, count );
    if (impl->coll && impl->index < impl->coll->count)
    {
        n = min( count, impl->coll->count - impl->index );
        memcpy( items, &impl->coll->items[impl->index], n * sizeof(*impl->coll->items) );
        impl->index += n;
    }
    if (actual) *actual = n;
    return S_OK;
}

static HRESULT WINAPI xaml_ConnectFromTrackerSource( void *iface, void *source );
static HRESULT WINAPI xaml_AddRefFromTrackerSource( void *iface );
static HRESULT WINAPI xaml_ReleaseFromTrackerSource( void *iface );

/* Per-instance vtbl: .NET ComWrappers writes the RCW managed pointer into
 * slot 6 (SetManagedReference), so a shared static vtbl array gets clobbered
 * for every object using it. Give each object its own copy. */
void **vtbl_dup( const void *src )
{
    void **d = calloc( 256, sizeof(*d) );
    if (d) memcpy( d, src, 256 * sizeof(*d) );
    return d;
}

static const void *iterator_vtbl[256] =
{
    [0] = iterator_QueryInterface,         /* 0: QI */
    [1] = iterator_AddRef,                 /* 1: AddRef */
    [2] = iterator_Release,                /* 2: Release */
    [3] = iterator_GetIids,                /* 3: GetIids */
    [4] = iterator_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = iterator_GetTrustLevel,          /* 5: GetTrustLevel */
    [6 ... 255] = frame_generic_slot,
    [6] = iterator_MoveNext,               /* 6: MoveNext (IIterator untracked layout) */
    [7] = iterator_get_Current,            /* 7: get_Current */
    [8] = iterator_get_HasCurrent,         /* 8: get_HasCurrent */
    [9] = iterator_GetMany,                /* 9: GetMany */
};

/* IIterable<RowDefinition> view: slot 6 = First() -> fresh IIterator enumerator */
static HRESULT WINAPI iterable_First( void *iface, void **current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    struct xaml_iterator *it;
    ERR( "iterable_First iface %p out %p\n", iface, current );
    if (!current) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( iterator_vtbl );
    it->ref = 1;
    it->coll = impl->coll;
    it->index = 0;
    it->end = impl->end;
    *current = &it->IInspectable_iface;
    return S_OK;
}

static const void *iterable_vtbl[256] =
{
    [0] = iterator_QueryInterface,
    [1] = iterator_AddRef,
    [2] = iterator_Release,
    [3] = iterator_GetIids,
    [4] = iterator_GetRuntimeClassName,
    [5] = iterator_GetTrustLevel,
    [6 ... 255] = frame_generic_slot,
    [6] = iterable_First,                  /* IIterable: First */
};

static HRESULT collection_iterable_view( struct xaml_collection *coll, void **out )
{
    struct xaml_iterator *v;
    if (coll->iterable_view)
    {
        *out = coll->iterable_view;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (!(v = calloc( 1, sizeof(*v) ))) return E_OUTOFMEMORY;
    v->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( iterable_vtbl );
    v->ref = 1;
    v->coll = coll;
    v->end = coll->count;
    coll->iterable_view = &v->IInspectable_iface;  /* leak-kept for collection lifetime */
    *out = &v->IInspectable_iface;
    return S_OK;
}

static HRESULT WINAPI collection_GetAt( void *iface, ULONG_PTR index, void **item );
static HRESULT WINAPI collection_get_Size( void *iface, unsigned int *size );
static HRESULT WINAPI collection_GetView( void *iface, void **view );
static HRESULT WINAPI collection_IndexOf( void *iface, void *item, unsigned int *index, boolean *found );
static HRESULT WINAPI collection_SetAt( void *iface, unsigned int index, void *item );
static HRESULT WINAPI collection_InsertAt( void *iface, unsigned int index, void *item );
static HRESULT WINAPI collection_RemoveAt( void *iface, unsigned int index );
static HRESULT WINAPI collection_Append( void *iface, void *item );
static HRESULT WINAPI collection_Clear( void *iface );
static HRESULT WINAPI collection_GetMany( void *iface, unsigned int start, unsigned int count, void **items, unsigned int *actual );

static const void *iterator_vector_vtbl[256] =
{
    [0] = iterator_QueryInterface,
    [1] = iterator_AddRef,
    [2] = iterator_Release,
    [3] = iterator_GetIids,
    [4] = iterator_GetRuntimeClassName,
    [5] = iterator_GetTrustLevel,
    [6 ... 255] = frame_generic_slot,
    [6] = collection_GetAt,                /* IVector: GetAt */
    [7] = collection_get_Size,             /* IVector: get_Size */
    [8] = collection_GetView,              /* IVector: GetView */
    [9] = collection_IndexOf,              /* IVector: IndexOf */
    [10] = collection_SetAt,               /* IVector: SetAt */
    [11] = collection_InsertAt,            /* IVector: InsertAt */
    [12] = collection_RemoveAt,            /* IVector: RemoveAt */
    [13] = collection_Append,              /* IVector: Append */
    [15] = collection_Clear,               /* IVector: Clear */
    [16] = collection_GetMany,             /* IVector: GetMany */
};

static HRESULT WINAPI collection_GetAt( void *iface, ULONG_PTR index, void **item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    void **cv = (void **)iface;
    void **cv0 = *(void ***)cv;
    ERR( "collection_GetAt iface %p, index %lu (0x%lx), item %p, count %u\n", iface, (unsigned long)index, (unsigned long)index, item, impl->count );
    ERR( "COLDUMP self %p lpVtbl %p v0 %p v1 %p v6 %p v7 %p\n", cv, cv0, cv0[0], cv0[1], cv0[6], cv0[7] );
    if (!item || index > 0x10000)
    {
        void **first = (void **)index;
        struct xaml_iterator *it;
        if (!first) first = item;
        if (!first) return E_POINTER;
        if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
        it->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( iterator_vtbl );
        it->ref = 1;
        it->coll = impl;
        it->index = 0;
        it->end = impl->count;
        IInspectable_AddRef( &impl->IInspectable_iface );
        *first = &it->IInspectable_iface;
        ERR( "IIterable::First created iterator %p for coll %p at %p\n", *first, impl, first );
        {
            void **v = (void **)*first;
            void **v0 = *(void ***)v;
            ERR( "ITERDUMP self %p lpVtbl %p v0 %p v1 %p v6 %p v7 %p v8 %p v9 %p\n",
                 v, v0, v0[0], v0[1], v0[6], v0[7], v0[8], v0[9] );
        }
        return S_OK;
    }
    if (index >= impl->count)
    {
        *item = NULL;
        return 0x8000000b; /* E_BOUNDS */
    }
    *item = impl->items[index];
    if (*item) IInspectable_AddRef( (IInspectable *)*item );
    return S_OK;
}

static HRESULT WINAPI collection_get_Size( void *iface, unsigned int *size )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ERR( "collection_get_Size iface %p size %p count %u\n", iface, size, impl->count );
    if (size) *size = impl->count;
    return S_OK;
}

static HRESULT WINAPI collection_GetView( void *iface, void **view )
{
    if (view)
    {
        *view = iface;
        IInspectable_AddRef( (IInspectable *)iface );
    }
    return S_OK;
}

static HRESULT WINAPI collection_IndexOf( void *iface, void *item, unsigned int *index, boolean *found )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int i;
    for (i = 0; i < impl->count; i++)
    {
        if (impl->items[i] == item)
        {
            if (index) *index = i;
            if (found) *found = TRUE;
            return S_OK;
        }
    }
    if (index) *index = 0;
    if (found) *found = FALSE;
    return S_OK;
}

static HRESULT WINAPI collection_SetAt( void *iface, unsigned int index, void *item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    if (index >= impl->count) return 0x8000000b; /* E_BOUNDS */
    if (impl->items[index]) IInspectable_Release( impl->items[index] );
    impl->items[index] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    return S_OK;
}

static HRESULT WINAPI collection_InsertAt( void *iface, unsigned int index, void *item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    if (index > impl->count) return 0x8000000b; /* E_BOUNDS */
    if (impl->count >= impl->capacity)
    {
        unsigned int new_cap = impl->capacity ? impl->capacity * 2 : 8;
        void *new_items = realloc( impl->items, new_cap * sizeof(*impl->items) );
        if (!new_items) return E_OUTOFMEMORY;
        impl->items = new_items;
        impl->capacity = new_cap;
    }
    memmove( &impl->items[index + 1], &impl->items[index], (impl->count - index) * sizeof(*impl->items) );
    impl->items[index] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI collection_RemoveAt( void *iface, unsigned int index )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    if (index >= impl->count) return 0x8000000b; /* E_BOUNDS */
    if (impl->items[index]) IInspectable_Release( impl->items[index] );
    memmove( &impl->items[index], &impl->items[index + 1], (impl->count - index - 1) * sizeof(*impl->items) );
    impl->count--;
    return S_OK;
}

static HRESULT WINAPI collection_Append( void *iface, void *item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ERR( "collection_Append iface %p, item %p, count was %u\n", iface, item, impl->count );
    if (impl->count >= impl->capacity)
    {
        unsigned int new_cap = impl->capacity ? impl->capacity * 2 : 8;
        void *new_items = realloc( impl->items, new_cap * sizeof(*impl->items) );
        if (!new_items) return E_OUTOFMEMORY;
        impl->items = new_items;
        impl->capacity = new_cap;
    }
    impl->items[impl->count] = (IInspectable *)item;
    if (item) IInspectable_AddRef( (IInspectable *)item );
    impl->count++;
    return S_OK;
}

static HRESULT WINAPI collection_Clear( void *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int i;
    for (i = 0; i < impl->count; i++)
        if (impl->items[i]) IInspectable_Release( impl->items[i] );
    impl->count = 0;
    return S_OK;
}

static HRESULT WINAPI collection_GetMany( void *iface, unsigned int start, unsigned int count, void **items, unsigned int *actual )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int n = 0;
    if (start < impl->count)
    {
        n = min(count, impl->count - start);
        memcpy(items, &impl->items[start], n * sizeof(*impl->items));
    }
    if (actual) *actual = n;
    return S_OK;
}

/* IVectorView<T>::GetMany is (count, items, actual) — 3 args, no start index.
 * IObservableVector<T>::GetMany is (start, count, items, actual). Feeding the
 * 4-arg form through the 3-arg slot shifted every returned item pointer by
 * one qword, handing the app a stack-junk ComObject (AV in set_MinHeight). */
static HRESULT WINAPI collection_GetMany_view( void *iface, unsigned int count, void **items, unsigned int *actual )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    unsigned int n = min(count, impl->count);
    if (items && n) memcpy( items, impl->items, n * sizeof(*impl->items) );
    ERR( "collection_GetMany_view iface %p count %u -> %u\n", iface, count, n );
    if (actual) *actual = n;
    return S_OK;
}

/* WinUI3 RowDefinitionCollection IV3 {94ea2b94} = IVectorView<RowDefinition>:
 * GetAt6, Size7, GetView8, IndexOf9, GetMany10 (IObservableVector has SetAt10). */
static const void *collection_view_vtbl[256] =
{
    [0] = collection_QueryInterface,
    [1] = collection_AddRef,
    [2] = collection_Release,
    [3] = collection_GetIids,
    [4] = collection_GetRuntimeClassName,
    [5] = collection_GetTrustLevel,
    [6 ... 255] = frame_generic_slot,
    [6] = collection_GetAt,
    [7] = collection_get_Size,
    [8] = collection_GetView,
    [9] = collection_IndexOf,
    [10] = collection_GetMany_view,
    [13] = collection_Append,
    [15] = collection_Clear,
    [16] = collection_GetMany,
};

static const void *collection_vtbl[256] =
{
    [0] = collection_QueryInterface,         /* 0: QI */
    [1] = collection_AddRef,                 /* 1: AddRef */
    [2] = collection_Release,                /* 2: Release */
    [3] = collection_GetIids,                /* 3: GetIids */
    [4] = collection_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = collection_GetTrustLevel,          /* 5: GetTrustLevel */
    [6 ... 255] = frame_generic_slot,
    [6] = collection_GetAt,                  /* 6: First */
    [7] = collection_get_Size,               /* 7: get_Size */
    [8] = collection_GetView,                /* 8: GetView */
    [9] = collection_IndexOf,                /* 9: IndexOf */
    [10] = collection_SetAt,                 /* 10: SetAt */
    [11] = collection_InsertAt,              /* 11: InsertAt */
    [12] = collection_RemoveAt,              /* 12: RemoveAt */
    [13] = collection_Append,                /* 13: Append */
    [14] = frame_generic_slot,               /* 14: RemoveAtEnd */
    [15] = collection_Clear,                 /* 15: Clear */
    [16] = collection_GetMany,               /* 16: GetMany */
};

static HRESULT collection_create( void **instance )
{
    struct xaml_collection *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( collection_vtbl );
    impl->ref = 1;
    *instance = &impl->IInspectable_iface;
    return S_OK;
}

/* Generic XAML control / element (Grid, RowDefinition, ColumnDefinition, ContentControl, Image, WebView2) */
static const GUID IID_IWebView2_g =
    { 0x2b2c76c2, 0x997c, 0x5069, { 0xa8, 0xf0, 0x9b, 0x84, 0xcd, 0x7e, 0x62, 0x4b } };
static const GUID IID_IWebView22_g =
    { 0x560c5eed, 0x3e7a, 0x51e4, { 0xb1, 0x4f, 0x10, 0x7b, 0xa0, 0x2b, 0x89, 0xbe } };
static const GUID IID_IWebView2IV1_g =
    { 0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90 } };


extern struct xaml_element *impl_from_element( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IInspectable_iface );
}
static inline struct xaml_element *impl_from_IGrid( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IGrid_iface );
}
static inline struct xaml_element *impl_from_IUIElement( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IUIElement_iface );
}
static inline struct xaml_element *impl_from_IPanel( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IPanel_iface );
}
static inline struct xaml_element *impl_from_IContentControl( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IContentControl_iface );
}
static inline struct xaml_element *impl_from_IRowDefinition( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IRowDefinition_iface );
}
static inline struct xaml_element *impl_from_IColumnDefinition( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IColumnDefinition_iface );
}
extern struct xaml_element *impl_from_IDependencyObject( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IDependencyObject_iface );
}

HRESULT WINAPI element_QueryInterface( IInspectable *iface, REFIID iid, void **out );
HRESULT WINAPI element_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
ULONG WINAPI element_AddRef( IInspectable *iface );
ULONG WINAPI element_Release( IInspectable *iface );

static HRESULT WINAPI element_grid_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IGrid( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI element_grid_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IGrid( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI element_grid_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IGrid( iface );
    return element_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI element_panel_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IPanel( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI element_panel_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IPanel( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI element_panel_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IPanel( iface );
    return element_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI element_content_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IContentControl( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI element_content_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IContentControl( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI element_content_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IContentControl( iface );
    return element_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI element_row_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IRowDefinition( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI element_row_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IRowDefinition( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI element_row_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IRowDefinition( iface );
    return element_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI element_col_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IColumnDefinition( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI element_col_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IColumnDefinition( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI element_col_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IColumnDefinition( iface );
    return element_Release( &impl->IInspectable_iface );
}

HRESULT WINAPI element_wv2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
ULONG WINAPI element_wv2_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
ULONG WINAPI element_wv2_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_Release( &impl->IInspectable_iface );
}

HRESULT WINAPI element_wv22_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
ULONG WINAPI element_wv22_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
ULONG WINAPI element_wv22_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    return element_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI element_grid_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IGrid( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
static HRESULT WINAPI element_panel_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IPanel( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
static HRESULT WINAPI element_content_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IContentControl( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
static HRESULT WINAPI element_row_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IRowDefinition( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
static HRESULT WINAPI element_col_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IColumnDefinition( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
HRESULT WINAPI element_wv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
HRESULT WINAPI element_wv22_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}

HRESULT WINAPI element_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_element( iface );

    if (!out) return E_POINTER;

    ERR( "element_QueryInterface(%p vt=%p, %s, class=%s)\n", iface, iface->lpVtbl, debugstr_guid( iid ), debugstr_w( impl->class_name ) );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (impl->is_webview)
    {
        if (IsEqualGUID( iid, &IID_IWebView2_g ) ||
            IsEqualGUID( iid, &IID_IGrid_WinUI ) ||
            IsEqualGUID( iid, &IID_IGrid_WinUI2 ) ||
            IsEqualGUID( iid, &IID_IGrid_v1 ))
        {
            /* WinUI3 WebView2 IV1/IV2: serve the real IWebView2 vtbl, not the
             * generic element slots (r100: get_double outs AV webview calls). */
            *out = &impl->IWebView2_iface;
            element_AddRef( iface );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IWebView22_g ) ||
            IsEqualGUID( iid, &IID_IWebView2IV1_g ))
        {
            *out = &impl->IWebView22_iface;
            element_AddRef( iface );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IDependencyObject_g ) ||
            IsEqualGUID( iid, &IID_IDependencyObject_WinUI ))
        {
            *out = &impl->IDependencyObject_iface;
            element_AddRef( iface );
            return S_OK;
        }
    }

    if (IsEqualGUID( iid, &IID_IUIElement_g ) ||
        IsEqualGUID( iid, &IID_IUIElement_WinUI ))
    {
        *out = &impl->IUIElement_iface;
        element_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPanel ))
    {
        *out = &impl->IPanel_iface;
        element_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IGrid ) ||
        IsEqualGUID( iid, &IID_IGrid_WinUI ) ||
        IsEqualGUID( iid, &IID_IGrid_WinUI2 ) ||
        IsEqualGUID( iid, &IID_IGrid_v1 ) ||
        IsEqualGUID( iid, &IID_IView_IV1 ) ||
        IsEqualGUID( iid, &IID_IView_IV2 ) ||
        IsEqualGUID( iid, &IID_ICanvas ) ||
        IsEqualGUID( iid, &IID_IStackPanel ) ||
        IsEqualGUID( iid, &IID_IContentControl ) ||
        IsEqualGUID( iid, &IID_IContentControl_WinUI ) ||
        IsEqualGUID( iid, &IID_IUserControl ) ||
        IsEqualGUID( iid, &IID_IBorder ) ||
        IsEqualGUID( iid, &IID_IButton ) ||
        IsEqualGUID( iid, &IID_IButtonBase ) ||
        IsEqualGUID( iid, &IID_IRowDefinition ) ||
        IsEqualGUID( iid, &IID_IColumnDefinition ) ||
        IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IControl_g ) ||
        IsEqualGUID( iid, &IID_IControl_WinUI ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_g ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ) ||
        IsEqualGUID( iid, &IID_IUIElementOverrides ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementOverrides ) ||
        IsEqualGUID( iid, &IID_IImage ) ||
        IsEqualGUID( iid, &IID_ITextBlock ) ||
        IsEqualGUID( iid, &IID_IRectangle ) ||
        IsEqualGUID( iid, &IID_IShape ) ||
        IsEqualGUID( iid, &IID_IViewbox ) ||
        IsEqualGUID( iid, &IID_IDataTemplate ) ||
        IsEqualGUID( iid, &IID_IProgressRing ) ||
        IsEqualGUID( iid, &IID_IImage_WinUI ) ||
        IsEqualGUID( iid, &IID_IStyle ) ||
        IsEqualGUID( iid, &IID_IBrush_local ) ||
        IsEqualGUID( iid, &IID_ISolidColorBrush_local ) ||
        IsEqualGUID( iid, &IID_IStyle_local ))
    {
        *out = &impl->IInspectable_iface;
        element_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDCompositionVisual ) ||
        IsEqualGUID( iid, &IID_IDCompositionVisual2 ))
    {
        if (impl->visual)
        {
            *out = impl->visual;
            IDCompositionVisual_AddRef( impl->visual );
            return S_OK;
        }
    }

    if (impl->outer && (IsEqualGUID( iid, &IID_IComponentConnector_x ) ||
                        IsEqualGUID( iid, &IID_IComponentConnector_UWP )))
    {
        /* The app's compiled BindComponent walks the visual tree via the
         * connector it QIs off each element. When our synthetic tree feeds a
         * connector back onto its own element the walk cycles: one Grid gets
         * QI'd 18k times nested on the stack (~3.4 KB/level, stack overflow
         * even at 16 MB). Answer the first 128 connector requests per element;
         * past that report E_NOINTERFACE so the binder treats the element as
         * a leaf and unwinds. */
        if (impl->qi_count > 16)
        {
            *out = NULL;
            return E_NOINTERFACE;
        }
        impl->qi_count++;
        return IInspectable_QueryInterface( impl->outer, iid, out );
    }

    ERR( "element_QueryInterface %s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

ULONG WINAPI element_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_element( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    ERR( "element_AddRef iface %p ref %lu\n", iface, ref );
    return ref;
}

ULONG WINAPI element_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_element( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        /* Leak-keep elements: the .NET 8 ComWrappers RCW caches the native
         * iface and QIs it after our ref drains (r100-r106 stale-iface AV in
         * element_QueryInterface via coreclr thunk). Tree is app-lifetime. */
        ERR( "element_Release iface %p ref 0 (leak-kept)\n", iface );
        InterlockedIncrement( &impl->ref );
        return 1;
    }
    return ref;
}

HRESULT WINAPI element_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    static const IID *probes[] =
    {
        (const IID *)&IID_IReferenceTracker,
        (const IID *)&IID_IWeakReferenceSource,
        (const IID *)&IID_IGrid,
    };
    if (count) *count = 0;
    if (iids)
    {
        IID *arr = CoTaskMemAlloc( sizeof(IID) * 3 );
        if (arr)
        {
            arr[0] = IID_IReferenceTracker;
            arr[1] = IID_IWeakReferenceSource;
            arr[2] = IID_IGrid;
            ERR( "element_GetIids arr %p count 3\n", arr );
            *count = 3;
            *iids = arr;
        }
    }
    (void)probes;
    return S_OK;
}

HRESULT WINAPI element_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_element( iface );
    const WCHAR *str = impl->class_name ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    ERR( "element_GetRuntimeClassName on %p -> %s\n", iface, debugstr_w(str) );
    return WindowsCreateString( str, wcslen(str), name );
}

HRESULT WINAPI element_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    ERR( "element_GetTrustLevel iface %p\n", iface );
    if (level) *level = BaseTrust;
    return S_OK;
}

HRESULT WINAPI element_generic_slot( void *iface, ... )
{
    ERR( "element_generic_slot iface %p\n", iface );
    return S_OK;
}

/* slot-probe prototypes (definitions near wv2_vtbl below) */
static HRESULT WINAPI element_get_height_double( void *iface, double *val )
{
    struct xaml_element *impl = impl_from_element( (IInspectable *)iface );
    HWND hw = impl->parent_hwnd;
    SIZE sz;

    if (!hw || !IsWindow( hw )) hw = xaml_get_window_hwnd();
    if (val)
    {
        xaml_get_layout_metrics( hw, &sz, NULL );
        *val = (double)sz.cy;
    }
    return S_OK;
}

static HRESULT WINAPI element_put_double( void *iface, double val )
{
    (void)iface;
    (void)val;
    return S_OK;
}

/* IReferenceTracker slots 6-8 (WinUI3 tracked classes; .NET RCW calls
 * ConnectFromTrackerSource + AddRefFromTrackerSource after wrap). */
struct tracker_entry
{
    void *iface;
    void *source;
    unsigned int idx;
};
static struct tracker_entry tracker_map[128];

static struct tracker_entry *tracker_lookup( void *iface, int create )
{
    unsigned int i, free_idx = 128;
    for (i = 0; i < 128; i++)
    {
        if (tracker_map[i].iface == iface) return &tracker_map[i];
        if (!tracker_map[i].iface && free_idx == 128) free_idx = i;
    }
    if (!create || free_idx == 128) return NULL;
    tracker_map[free_idx].iface = iface;
    tracker_map[free_idx].idx = free_idx;
    return &tracker_map[free_idx];
}

static HRESULT WINAPI xaml_ConnectFromTrackerSource( void *iface, void *source )
{
    struct tracker_entry *e = tracker_lookup( iface, 1 );
    ERR( "xaml_ConnectFromTrackerSource iface %p source %p\n", iface, source );
    if (e && !e->source && source)
    {
        /* .NET RCW passes its own GCHandle as source, not a COM pointer.
         * Store only; do not AddRef. */
        e->source = source;
    }
    return S_OK;
}
static HRESULT WINAPI xaml_AddRefFromTrackerSource( void *iface )
{
    struct tracker_entry *e = tracker_lookup( iface, 0 );
    ERR( "xaml_AddRefFromTrackerSource iface %p source %p\n", iface, e ? e->source : NULL );
    return S_OK;
}

struct xaml_weakref
{
    IWeakReference IWeakReference_iface;
    LONG ref;
    struct xaml_element *element;
};

static inline struct xaml_weakref *impl_from_IWeakReference( IWeakReference *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_weakref, IWeakReference_iface );
}

static ULONG WINAPI weakref_AddRef( IWeakReference *iface );

static HRESULT WINAPI weakref_QueryInterface( IWeakReference *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IWeakReference ) || IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = iface;
        weakref_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI weakref_AddRef( IWeakReference *iface )
{
    struct xaml_weakref *impl = impl_from_IWeakReference( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI weakref_Release( IWeakReference *iface )
{
    struct xaml_weakref *impl = impl_from_IWeakReference( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        IInspectable_Release( &impl->element->IInspectable_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI weakref_Resolve( IWeakReference *iface, REFIID iid, IInspectable **out )
{
    struct xaml_weakref *impl = impl_from_IWeakReference( iface );
    return element_QueryInterface( &impl->element->IInspectable_iface, iid, (void **)out );
}

static const IWeakReferenceVtbl weakref_vtbl =
{
    weakref_QueryInterface,
    weakref_AddRef,
    weakref_Release,
    weakref_Resolve,
};

static HRESULT WINAPI element_GetWeakReference( IInspectable *iface, IWeakReference **out )
{
    struct xaml_weakref *impl;

    if (!out) return E_POINTER;
    impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->IWeakReference_iface.lpVtbl = &weakref_vtbl;
    impl->ref = 1;
    impl->element = impl_from_element( (void *)iface );
    IInspectable_AddRef( &impl->element->IInspectable_iface );
    *out = &impl->IWeakReference_iface;
    return S_OK;
}

static HRESULT WINAPI xaml_ReleaseFromTrackerSource( void *iface )
{
    struct tracker_entry *e = tracker_lookup( iface, 0 );
    if (e && e->idx < ARRAY_SIZE( tracker_map ))
        memset( &tracker_map[e->idx], 0, sizeof(tracker_map[e->idx]) );
    return S_OK;
}

static IInspectable *loaded_target;

HRESULT WINAPI element_add_Loaded( void *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_element *impl = impl_from_element( (IInspectable *)iface );
    ERR( "element_add_Loaded iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    impl->loaded_fn = handler;
    xaml_register_loaded( handler, (IInspectable *)iface );
    return S_OK;
}

struct loaded_reg { void *handler; IInspectable *sender; };
static struct loaded_reg *loaded_regs;
static int loaded_count;
static int loaded_cap;
static SRWLOCK loaded_regs_lock = SRWLOCK_INIT;

void xaml_register_loaded( void *handler, IInspectable *sender );
void xaml_register_loaded( void *handler, IInspectable *sender )
{
    int i;
    AcquireSRWLockExclusive( &loaded_regs_lock );
    for (i = 0; i < loaded_count; i++)
    {
        if (loaded_regs[i].sender == sender)
        {
            loaded_regs[i].handler = handler;
            ReleaseSRWLockExclusive( &loaded_regs_lock );
            return;
        }
    }
    if (xaml_vec_grow( (void **)&loaded_regs, &loaded_cap, loaded_count + 1, sizeof(*loaded_regs), 8 ))
    {
        loaded_regs[loaded_count].handler = handler;
        loaded_regs[loaded_count].sender = sender;
        loaded_count++;
    }
    ReleaseSRWLockExclusive( &loaded_regs_lock );
}

void xaml_fire_all_loaded( void );
void xaml_fire_all_loaded( void )
{
    int i, cnt;
    struct loaded_reg *snap = NULL;
    AcquireSRWLockExclusive( &loaded_regs_lock );
    cnt = loaded_count;
    if (cnt > 0)
    {
        snap = malloc( (size_t)cnt * sizeof(*snap) );
        if (snap) memcpy( snap, loaded_regs, (size_t)cnt * sizeof(*snap) );
    }
    ReleaseSRWLockExclusive( &loaded_regs_lock );
    if (!snap) return;
    for (i = 0; i < cnt; i++)
    {
        HRESULT (WINAPI *fn)( void *, void *, void * );
        if (!snap[i].handler) continue;
        fn = (void *)((void **)((void **)snap[i].handler)[0])[3];
        ERR( "firing Loaded on %p handler %p\n", (void *)snap[i].sender, snap[i].handler );
        fn( snap[i].handler, (void *)snap[i].sender, NULL );
    }
    free( snap );
}

HRESULT WINAPI element_remove_Loaded( void *iface, EventRegistrationToken token )
{
    TRACE( "element_remove_Loaded iface %p, token %I64u\n", iface, token.value );
    return S_OK;
}

static HRESULT WINAPI element_get_Content( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_element( iface );
    TRACE( "element_get_Content iface %p, out %p\n", iface, out );
    ERR( "TRACEROWS element_get_Content iface %p out %p content %p\n", iface, out, impl->content );
    if (!out) return E_POINTER;
    *out = impl->content;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI element_put_Content( void *iface, void *val )
{
    struct xaml_element *impl = impl_from_element( iface );
    ERR( "element_put_Content iface %p, val %p\n", iface, val );
    impl->content = (IInspectable *)val;
    if (impl->content) IInspectable_AddRef( impl->content );
    return S_OK;
}

static HRESULT WINAPI element_grid_get_RowDefinitions( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_element( iface );
    ERR( "TRACEROWS element_grid_get_RowDefinitions iface %p out %p\n", iface, out );
    TRACE( "element_grid_get_RowDefinitions iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    {
        IInspectable *rd = NULL;
        collection_create( (void **)&impl->row_defs );
        element_create_typed( L"Windows.UI.Xaml.Controls.RowDefinition", (void **)&rd );
        if (rd)
        {
            IInspectable *rd2 = NULL;
            struct xaml_collection *c = impl_from_collection( (void *)impl->row_defs );
            element_create_typed( L"Windows.UI.Xaml.Controls.RowDefinition", (void **)&rd2 );
            collection_Append( (void *)impl->row_defs, rd );
            collection_Append( (void *)impl->row_defs, rd2 ? rd2 : rd );
            ERR( "seeded fresh row_defs count %u\n", c->count );
        }
    }
    /* Slots are shared: IUIElement::set_Visibility(iface, int) can land on a
     * collection getter whose second param is the out pointer. Guard the store. */
    __TRY
    {
        *out = impl->row_defs;
        if (*out) IInspectable_AddRef( (IInspectable *)*out );
    }
    __EXCEPT_ALL { }
    __ENDTRY;
    return S_OK;
}

/* WinUI3 Grid IV1 (c3c01020) slot 10: get_RowDefinitions(HSTRING count_in, IObservableVector** out) */
HRESULT WINAPI element_grid_get_RowDefinitions_abi3( void *iface, void *count, void **out )
{
    (void)count;
    return element_grid_get_RowDefinitions( iface, out );
}

static HRESULT WINAPI element_grid_get_ColumnDefinitions( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_element( iface );
    TRACE( "element_grid_get_ColumnDefinitions iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    if (!impl->col_defs)
    {
        void *cd = NULL, *cd2 = NULL;
        collection_create( (void **)&impl->col_defs );
        element_create_typed( L"Windows.UI.Xaml.Controls.ColumnDefinition", (void **)&cd );
        element_create_typed( L"Windows.UI.Xaml.Controls.ColumnDefinition", (void **)&cd2 );
        collection_Append( (void *)impl->col_defs, cd );
        collection_Append( (void *)impl->col_defs, cd2 ? cd2 : cd );
        ERR( "seeded fresh col_defs\n" );
    }
    __TRY
    {
        *out = impl->col_defs;
        if (*out) IInspectable_AddRef( (IInspectable *)*out );
    }
    __EXCEPT_ALL { }
    __ENDTRY;
    return S_OK;
}

/* WinUI3 Grid IV2 (94ea2b94) slots 10/11: get_RowDefinitions / get_ColumnDefinitions (HSTRING in, out) */
static HRESULT WINAPI element_grid_get_ColumnDefinitions_abi3( void *iface, void *count, void **out )
{
    (void)count;
    return element_grid_get_ColumnDefinitions( iface, out );
}

static HRESULT WINAPI element_panel_get_Children( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_IPanel( iface );
    TRACE( "element_panel_get_Children iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    if (!impl->children) collection_create( (void **)&impl->children );
    *out = impl->children;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI element_get_RequestedTheme( void *iface, int *theme )
{
    if (theme) *theme = 0;
    return S_OK;
}
static HRESULT WINAPI element_put_RequestedTheme( void *iface, int theme )
{
    return S_OK;
}

static const void *grid_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [9] = xaml_ConnectFromTrackerSource,
    [10] = element_grid_get_RowDefinitions_abi3,  /* IV2 94ea2b94 WinUI3 view: RowDefinitions(HSTRING, vec**) */
    [11] = element_grid_get_ColumnDefinitions_abi3,
    [6] = element_grid_get_RowDefinitions,
    [7] = element_grid_get_ColumnDefinitions,
    [12] = element_grid_get_ColumnDefinitions,
    [13] = wv2_probe57, [14] = wv2_probe58, [15] = wv2_probe59,
    [16] = element_get_Content, [17] = wv2_probe61, [18] = wv2_probe62,
    [19] = wv2_probe63, [20] = wv2_probe0,
    [23] = wv2_probe1, [24] = wv2_probe2, [25] = wv2_probe3,
    [26] = wv2_probe4, [27] = wv2_probe5, [28] = wv2_probe6,
    [29] = wv2_probe7, [30] = wv2_probe8, [31] = wv2_probe9,
    [32] = wv2_probe10, [33] = wv2_probe11, [34] = wv2_probe12,
    [35] = wv2_probe13, [36] = wv2_probe14, [37] = wv2_probe15,
    [38] = wv2_probe16, [39] = wv2_probe17, [40] = wv2_probe18,
    [20] = wv2_probe0,  [21] = wv2_probe1,  [22] = wv2_probe2,  [23] = wv2_probe3,
    [24] = wv2_probe4,  [25] = wv2_probe5,  [26] = wv2_probe6,  [27] = wv2_probe7,
    [28] = wv2_probe8,  [29] = wv2_probe9,  [30] = wv2_probe10, [31] = wv2_probe11,
    [32] = wv2_probe12, [33] = wv2_probe13, [34] = wv2_probe14, [35] = wv2_probe15,
    [36] = wv2_probe16, [37] = wv2_probe17, [38] = wv2_probe18, [39] = wv2_probe19,
    [40] = wv2_probe20, [41] = wv2_probe21, [42] = wv2_probe22, [43] = wv2_probe23,
    [44] = wv2_probe24, [45] = wv2_probe25, [46] = wv2_probe26, [47] = wv2_probe27,
    [48] = wv2_probe28, [49] = wv2_probe29, [50] = wv2_probe30, [51] = wv2_probe31,
    [52] = wv2_probe32, [53] = wv2_probe33, [54] = wv2_probe34, [55] = wv2_probe35,
    [56] = wv2_probe36, [57] = element_get_RequestedTheme, [58] = element_put_RequestedTheme, [59] = wv2_probe39,
    [60] = wv2_probe40, [61] = element_add_Loaded, [62] = element_remove_Loaded, [63] = wv2_probe43,
    [64] = wv2_probe44, [65] = wv2_probe45, [66] = wv2_probe46, [67] = wv2_probe47,
    [68] = wv2_probe48, [69] = wv2_probe49, [70] = wv2_probe50, [71] = wv2_probe51,
    [72] = wv2_probe52,
    [73] = element_remove_Loaded,
    [74] = wv2_probe53, [75] = wv2_probe54, [76] = wv2_probe55, [77] = wv2_probe56,
    [78] = wv2_probe57, [79] = wv2_probe58, [80] = wv2_probe59, [81] = wv2_probe60,
    [82] = wv2_probe61, [83] = wv2_probe62, [84] = wv2_probe63,
    [85] = wv2_probe0,  [86] = wv2_probe1,  [87] = wv2_probe2,  [88] = wv2_probe3,
    [89] = wv2_probe4,  [90] = wv2_probe5,  [91] = wv2_probe6,  [92] = wv2_probe7,
    [93] = wv2_probe8,  [94] = wv2_probe9,  [95] = wv2_probe10, [96] = wv2_probe11,
    [97] = wv2_probe12, [98] = wv2_probe13, [99] = wv2_probe14, [100] = wv2_probe15,
    [72] = element_add_Loaded,
    [73] = element_remove_Loaded,
    [127] = element_get_Content,
    [128] = element_put_Content,
};

static const void *panel_vtbl[256] =
{
    [0] = element_panel_QueryInterface,
    [1] = element_panel_AddRef,
    [2] = element_panel_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = element_panel_get_Children,
    [7] = xaml_AddRefFromTrackerSource,
    [8] = xaml_ReleaseFromTrackerSource,
    [9] = element_panel_get_Children,
    [13] = element_panel_get_Children,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
    [130] = element_get_Content,
    [131] = element_put_Content,
};

static const void *content_control_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = xaml_ConnectFromTrackerSource,
    [7] = xaml_AddRefFromTrackerSource,
    [8] = xaml_ReleaseFromTrackerSource,
    [9] = element_get_Content,
    [10] = element_put_Content,
    [17] = element_add_Loaded,
    [18] = element_remove_Loaded,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
    [130] = element_get_Content,
    [131] = element_put_Content,
};

/* WinUI3 IRowDefinition ABI: [6]get_Height(GridLength) [8]get_MaxHeight [10]get_MinHeight
 * [12]get_Priority. GridLength = { double value; GridUnitType unit(Star=2) }. */
struct xaml_grid_length { double value; unsigned int unit; };
static HRESULT WINAPI rowdef_get_Height( void *iface, void *out )
{
    struct xaml_grid_length gl = { 1.0, 2 };
    ERR( "rowdef_get_Height iface %p out %p\n", iface, out );
    if (out) *(struct xaml_grid_length *)out = gl;
    return S_OK;
}
static HRESULT WINAPI rowdef_get_max( void *iface, double *out )
{
    if (out) *out = 1.7976931348623157e308;
    return S_OK;
}
static HRESULT WINAPI rowdef_get_min( void *iface, double *out )
{
    if (out) *out = 0.0;
    return S_OK;
}
static HRESULT WINAPI rowdef_get_priority( void *iface, int *out )
{
    if (out) *out = 5;
    return S_OK;
}
static HRESULT WINAPI rowdef_get_actual( void *iface, double *out )
{
    if (out) *out = 0.0;
    return S_OK;
}
static HRESULT WINAPI rowdef_set_Height( void *iface, struct xaml_grid_length val )
{
    (void)val;
    return S_OK;
}
static HRESULT WINAPI rowdef_set_max( void *iface, double val )
{
    (void)val;
    return S_OK;
}
static HRESULT WINAPI rowdef_set_min( void *iface, double val )
{
    (void)val;
    return S_OK;
}

static const void *row_def_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    /* CsWinRT IRowDefinition: 6 ActualHeight, 7 get_Height, 8 set_Height,
     * 9 get_MaxHeight, 10 set_MaxHeight, 11 get_MinHeight, 12 set_MinHeight. */
    [6] = rowdef_get_actual,
    [7] = rowdef_get_Height,
    [8] = rowdef_set_Height,
    [9] = rowdef_get_max,
    [10] = rowdef_set_max,
    [11] = rowdef_get_min,
    [12] = rowdef_set_min,
    [130] = element_get_Content,
    [131] = element_put_Content,
};

static const void *col_def_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    /* CsWinRT IColumnDefinition: 6 ActualWidth, 7 get_Width, 8 set_Width,
     * 9 get_MaxWidth, 10 set_MaxWidth, 11 get_MinWidth, 12 set_MinWidth. */
    [6] = rowdef_get_actual,
    [7] = rowdef_get_Height,
    [8] = rowdef_set_Height,
    [9] = rowdef_get_max,
    [10] = rowdef_set_max,
    [11] = rowdef_get_min,
    [12] = rowdef_set_min,
    [130] = element_get_Content,
    [131] = element_put_Content,
};

static const void *element_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = xaml_ConnectFromTrackerSource,
    [7] = xaml_AddRefFromTrackerSource,
    [8] = xaml_ReleaseFromTrackerSource,
    [9] = element_get_Content,
    [10] = element_put_Content,
    [11] = element_get_double,
    [12] = element_put_double,
    [13] = element_get_height_double,
    [14] = element_put_double,
    [15] = element_get_double,
    [16] = element_put_double,
    [17] = element_get_double,
    [18] = element_put_double,
    [19] = element_get_height_double,
    [20] = element_put_double,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
    [130] = element_get_Content,
    [131] = element_put_Content,
};


static HRESULT WINAPI xaml_async_action_QueryInterface( IAsyncAction *iface, REFIID iid, void **out )
{
    struct xaml_async_action *impl = impl_from_async_action( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        *out = &impl->IAsyncAction_iface;
        IAsyncAction_AddRef( iface );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        IAsyncAction_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI xaml_async_action_AddRef( IAsyncAction *iface )
{
    struct xaml_async_action *impl = impl_from_async_action( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI xaml_async_action_Release( IAsyncAction *iface )
{
    struct xaml_async_action *impl = impl_from_async_action( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler) IAsyncActionCompletedHandler_Release( impl->handler );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI xaml_async_action_GetIids( IAsyncAction *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0; if (iids) *iids = NULL; return S_OK;
}
static HRESULT WINAPI xaml_async_action_GetRuntimeClassName( IAsyncAction *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncAction", 31, name );
}
static HRESULT WINAPI xaml_async_action_GetTrustLevel( IAsyncAction *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI xaml_async_action_put_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler *handler )
{
    struct xaml_async_action *impl = impl_from_async_action( iface );
    if (impl->handler) IAsyncActionCompletedHandler_Release( impl->handler );
    impl->handler = handler;
    if (impl->handler)
    {
        IAsyncActionCompletedHandler_AddRef( impl->handler );
        if (impl->completed)
            IAsyncActionCompletedHandler_Invoke( impl->handler, iface, FAILED( impl->hr ) ? 3 /* Error */ : 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI xaml_async_action_get_Completed( IAsyncAction *iface, IAsyncActionCompletedHandler **handler )
{
    struct xaml_async_action *impl = impl_from_async_action( iface );
    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) IAsyncActionCompletedHandler_AddRef( *handler );
    return S_OK;
}
static HRESULT WINAPI xaml_async_action_GetResults( IAsyncAction *iface ) { return S_OK; }

static const struct IAsyncActionVtbl xaml_async_action_vtbl =
{
    xaml_async_action_QueryInterface,
    xaml_async_action_AddRef,
    xaml_async_action_Release,
    xaml_async_action_GetIids,
    xaml_async_action_GetRuntimeClassName,
    xaml_async_action_GetTrustLevel,
    xaml_async_action_put_Completed,
    xaml_async_action_get_Completed,
    xaml_async_action_GetResults,
};

static HRESULT WINAPI xaml_async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return xaml_async_action_QueryInterface( &impl_from_async_info( iface )->IAsyncAction_iface, iid, out );
}
static ULONG WINAPI xaml_async_info_AddRef( IAsyncInfo *iface )
{
    return xaml_async_action_AddRef( &impl_from_async_info( iface )->IAsyncAction_iface );
}
static ULONG WINAPI xaml_async_info_Release( IAsyncInfo *iface )
{
    return xaml_async_action_Release( &impl_from_async_info( iface )->IAsyncAction_iface );
}
static HRESULT WINAPI xaml_async_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI xaml_async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name ) { return E_NOTIMPL; }
static HRESULT WINAPI xaml_async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level ) { return E_NOTIMPL; }
static HRESULT WINAPI xaml_async_info_get_Id( IAsyncInfo *iface, unsigned int *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI xaml_async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct xaml_async_action *impl = impl_from_async_info( iface );
    if (status) *status = !impl->completed ? 0 /* Started */ : (FAILED( impl->hr ) ? 3 /* Error */ : 1 /* Completed */);
    return S_OK;
}
static HRESULT WINAPI xaml_async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *code )
{
    struct xaml_async_action *impl = impl_from_async_info( iface );
    if (code) *code = impl->hr;
    return S_OK;
}
static HRESULT WINAPI xaml_async_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI xaml_async_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const struct IAsyncInfoVtbl xaml_async_info_vtbl =
{
    xaml_async_info_QueryInterface,
    xaml_async_info_AddRef,
    xaml_async_info_Release,
    xaml_async_info_GetIids,
    xaml_async_info_GetRuntimeClassName,
    xaml_async_info_GetTrustLevel,
    xaml_async_info_get_Id,
    xaml_async_info_get_Status,
    xaml_async_info_get_ErrorCode,
    xaml_async_info_Cancel,
    xaml_async_info_Close,
};

static HRESULT xaml_async_action_create_ex( BOOL completed, struct xaml_async_action **out_impl, IAsyncAction **out )
{
    struct xaml_async_action *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncAction_iface.lpVtbl = &xaml_async_action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &xaml_async_info_vtbl;
    impl->ref = 1;
    impl->completed = completed;
    impl->hr = S_OK;
    *out = &impl->IAsyncAction_iface;
    if (out_impl) *out_impl = impl;
    return S_OK;
}

HRESULT xaml_async_action_create( IAsyncAction **out )
{
    return xaml_async_action_create_ex( TRUE, NULL, out );
}

static void xaml_async_action_complete( struct xaml_async_action *impl, HRESULT hr )
{
    if (!impl || impl->completed) return;
    impl->hr = hr;
    impl->completed = TRUE;
    if (impl->handler)
        IAsyncActionCompletedHandler_Invoke( impl->handler, &impl->IAsyncAction_iface,
                FAILED( hr ) ? 3 /* Error */ : 1 /* Completed */ );
}




void xaml_wv2_on_window_ready( HWND hwnd, UINT width, UINT height )
{
    ERR( "xaml_wv2_on_window_ready hwnd=%p %ux%u active=%p queued=%d\n",
         hwnd, width, height, g_active_wv2_impl, g_active_wv2_impl ? g_active_wv2_impl->browser_queued : -1 );
    g_wv2_core_hwnd = hwnd;
    if (g_active_wv2_impl) g_active_wv2_impl->parent_hwnd = hwnd;
    wv2_ensure_host_subclass( hwnd );
    if (hwnd && IsWindow( hwnd ))
    {
        HANDLE ev = get_xaml_window_ready_event();
        if (ev) SetEvent( ev );
    }
    if (g_active_wv2_impl && g_active_wv2_impl->visual && !g_active_wv2_impl->visual_attached)
    {
        IDCompositionVisual *root = xaml_get_dcomp_root_visual();
        if (root && g_active_wv2_impl->visual != root)
        {
            HRESULT hr_add = IDCompositionVisual_AddVisual( root, g_active_wv2_impl->visual, TRUE, NULL );
            if (SUCCEEDED( hr_add ))
            {
                g_active_wv2_impl->visual_attached = TRUE;
                xaml_dcomp_commit();
            }
            ERR( "xaml_wv2_on_window_ready: AddVisual root=%p vis=%p hr=%#lx\n",
                 root, g_active_wv2_impl->visual, hr_add );
        }
    }
    if (g_wv2_host_hwnd && IsWindow( g_wv2_host_hwnd ) && width > 0 && height > 0)
    {
        SetWindowPos( g_wv2_host_hwnd, HWND_TOP, 0, 0, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE );
    }
    wv2_sync_child_hwnds( hwnd, width, height );
}

void xaml_wv2_on_window_resize( HWND hwnd, UINT width, UINT height )
{
    static BOOL in_resize;
    if (in_resize) return;
    in_resize = TRUE;
    if (g_wv2_host_hwnd && IsWindow( g_wv2_host_hwnd ) && width > 0 && height > 0)
        SetWindowPos( g_wv2_host_hwnd, HWND_TOP, 0, 0, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE );
    if (g_active_wv2_impl && g_active_wv2_impl->loader_controller && g_active_wv2_impl->real_loader &&
        width > 0 && height > 0)
    {
        ICoreWebView2Controller *ctrl = g_active_wv2_impl->loader_controller;
        RECT rc = { 0, 0, (LONG)width, (LONG)height };
        ctrl->lpVtbl->put_Bounds( ctrl, rc );
        ctrl->lpVtbl->NotifyParentWindowPositionChanged( ctrl );
        wv2_sync_child_hwnds( wv2_get_controller_parent(), width, height );
    }
    in_resize = FALSE;
}

void xaml_wv2_handle_start_msg( LPARAM lparam )
{
    struct xaml_element *impl = (struct xaml_element *)lparam;
    if (!impl) impl = g_active_wv2_impl;
    if (impl) wv2_start_browser( impl );
}

static HRESULT WINAPI uie_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IUIElement( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI uie_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IUIElement( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI uie_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IUIElement( iface );
    return element_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI uie_set_visibility( void *iface, int vis )
{
    struct xaml_element *impl = impl_from_IUIElement( iface );
    impl->visibility = vis;
    return S_OK;
}
static HRESULT WINAPI uie_get_visibility( void *iface, int *out )
{
    struct xaml_element *impl = impl_from_IUIElement( iface );
    if (out) *out = impl->visibility;
    return S_OK;
}
static HRESULT WINAPI uie_get_ptr( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static const void *uielement_vtbl[64] =
{
    [0] = uie_QueryInterface,
    [1] = uie_AddRef,
    [2] = uie_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 63] = element_generic_slot,
    [8] = uie_get_ptr,   /* GetValue */
    [10] = uie_set_visibility,
    [11] = uie_get_visibility,
    [12] = uie_get_ptr,  /* ReadLocalValue */
    [13] = uie_get_ptr,  /* GetAnimationBaseValue */
};

HRESULT element_create( IInspectable *outer, IInspectable **inner, void **instance )
{
    struct xaml_element *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( grid_vtbl );
    impl->IGrid_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( grid_vtbl );
    impl->IPanel_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( panel_vtbl );
    impl->IContentControl_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( content_control_vtbl );
    impl->IRowDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( row_def_vtbl );
    impl->IColumnDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( col_def_vtbl );
    impl->IWebView2_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( wv2_vtbl );
    impl->IWebView22_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( wv22_vtbl );
    impl->IUIElement_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( uielement_vtbl );
    impl->IDependencyObject_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( wv2_do_vtbl );
    impl->class_name = L"Windows.UI.Xaml.Controls.Grid";
    impl->ref = 1;
    impl->outer = outer;
    xaml_create_dcomp_visual( &impl->visual );
    if (inner)
    {
        *inner = &impl->IInspectable_iface;
        IInspectable_AddRef( *inner );
    }
    *instance = &impl->IInspectable_iface;
    return S_OK;
}

HRESULT element_create_typed( const WCHAR *class_name, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create( NULL, NULL, (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        impl->class_name = class_name;
        if (class_name && (wcsstr(class_name, L"Grid") || wcsstr(class_name, L"Panel") || wcsstr(class_name, L"Canvas")))
            impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)grid_vtbl;
        else if (class_name && (wcsstr(class_name, L"ContentControl") || wcsstr(class_name, L"Border") || wcsstr(class_name, L"Button") || wcsstr(class_name, L"UserControl")))
            impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)content_control_vtbl;
        else if (class_name && wcsstr(class_name, L"RowDefinition"))
            impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)row_def_vtbl;
        else if (class_name && wcsstr(class_name, L"ColumnDefinition"))
            impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)col_def_vtbl;
        else if (class_name && wcsstr(class_name, L"WebView"))
        {
            impl->is_webview = TRUE;
            impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)element_vtbl;
        }
        else
            impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)element_vtbl;

        *instance = &impl->IInspectable_iface;
    }
    return hr;
}

HRESULT xaml_create_webview2( void **out )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create( NULL, NULL, (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        impl->is_webview = TRUE;
        *out = &impl->IInspectable_iface;
    }
    return hr;
}

static HRESULT WINAPI grid_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return element_create( NULL, NULL, (void **)instance );
}

static HRESULT WINAPI grid_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    TRACE( "iface %p, outer %p, inner %p, instance %p\n", iface, outer, inner, instance );
    return element_create( (IInspectable *)outer, (IInspectable **)inner, instance );
}

/* RowDefinition/ColumnDefinition need their own vtbl: the app's WindowEx ctor calls
 * `new RowDefinition()` then rd.MinHeight = 48. On the shared grid vtbl, slot 12 is
 * get_ColumnDefinitions(iface, out) and the double 48.0 lands as `out` -> store to
 * 0x4048000000000000 -> AccessViolationException (AV bt[9] get_ColumnDefinitions+0x26). */
static HRESULT WINAPI rowdef_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.Controls.RowDefinition", (void **)instance );
    ERR( "rowdef_factory_ActivateInstance -> %p hr %08x\n", (void *)*instance, hr );
    return hr;
}

static HRESULT WINAPI rowdef_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.Controls.RowDefinition", (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        if (inner) { *inner = (void *)impl; IInspectable_AddRef( (IInspectable *)*inner ); }
        *instance = (void *)impl;
    }
    ERR( "rowdef_factory_CreateInstance outer %p -> %p hr %08x\n", outer, (void *)impl, hr );
    return hr;
}

static HRESULT WINAPI coldef_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.Controls.ColumnDefinition", (void **)instance );
    ERR( "coldef_factory_ActivateInstance -> %p hr %08x\n", (void *)*instance, hr );
    return hr;
}

static HRESULT WINAPI coldef_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.Controls.ColumnDefinition", (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        if (inner) { *inner = (void *)impl; IInspectable_AddRef( (IInspectable *)*inner ); }
        *instance = (void *)impl;
    }
    ERR( "coldef_factory_CreateInstance outer %p -> %p hr %08x\n", outer, (void *)impl, hr );
    return hr;
}

static HRESULT WINAPI coll_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return collection_create( (void **)instance );
}

static HRESULT WINAPI coll_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    return collection_create( instance );
}

static const void *grid_factory_vtbl[] =
{
    frame_factory_QueryInterface,
    frame_factory_AddRef,
    frame_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    grid_factory_ActivateInstance,  /* slot 6: ActivateInstance */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static const void *grid_composable_vtbl[] =
{
    composable_factory_QueryInterface,     /* 0: QI */
    composable_factory_AddRef,             /* 1: AddRef */
    composable_factory_Release,            /* 2: Release */
    frame_factory_GetIids,                 /* 3: GetIids */
    frame_factory_GetRuntimeClassName,     /* 4: GetRuntimeClassName */
    frame_factory_GetTrustLevel,           /* 5: GetTrustLevel */
    grid_factory_CreateInstance,           /* 6: CreateInstance(outer, inner, instance) */
    grid_factory_CreateInstance,           /* 7 */
    grid_factory_CreateInstance,           /* 8 */
    grid_factory_CreateInstance,           /* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static struct frame_factory grid_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)grid_factory_vtbl},
    {(const struct IInspectableVtbl *)grid_composable_vtbl},
    1,
};
IActivationFactory *grid_factory = &grid_factory_obj.IActivationFactory_iface;

static const void *row_def_factory_vtbl[64] =
{
    [0] = frame_factory_QueryInterface,
    [1] = frame_factory_AddRef,
    [2] = frame_factory_Release,
    [3] = frame_factory_GetIids,
    [4] = frame_factory_GetRuntimeClassName,
    [5] = frame_factory_GetTrustLevel,
    [6] = rowdef_factory_ActivateInstance,
    [7 ... 63] = frame_generic_slot,
};

static const void *row_def_composable_vtbl[64] =
{
    [0] = composable_factory_QueryInterface,
    [1] = composable_factory_AddRef,
    [2] = composable_factory_Release,
    [3] = frame_factory_GetIids,
    [4] = frame_factory_GetRuntimeClassName,
    [5] = frame_factory_GetTrustLevel,
    [6 ... 9] = rowdef_factory_CreateInstance,
    [10 ... 63] = frame_generic_slot,
};

static const void *col_def_factory_vtbl[64] =
{
    [0] = frame_factory_QueryInterface,
    [1] = frame_factory_AddRef,
    [2] = frame_factory_Release,
    [3] = frame_factory_GetIids,
    [4] = frame_factory_GetRuntimeClassName,
    [5] = frame_factory_GetTrustLevel,
    [6] = coldef_factory_ActivateInstance,
    [7 ... 63] = frame_generic_slot,
};

static const void *col_def_composable_vtbl[64] =
{
    [0] = composable_factory_QueryInterface,
    [1] = composable_factory_AddRef,
    [2] = composable_factory_Release,
    [3] = frame_factory_GetIids,
    [4] = frame_factory_GetRuntimeClassName,
    [5] = frame_factory_GetTrustLevel,
    [6 ... 9] = coldef_factory_CreateInstance,
    [10 ... 63] = frame_generic_slot,
};

static struct frame_factory row_def_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)row_def_factory_vtbl},
    {(const struct IInspectableVtbl *)row_def_composable_vtbl},
    1,
};
IActivationFactory *row_def_factory = &row_def_factory_obj.IActivationFactory_iface;

static struct frame_factory col_def_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)col_def_factory_vtbl},
    {(const struct IInspectableVtbl *)col_def_composable_vtbl},
    1,
};
IActivationFactory *col_def_factory = &col_def_factory_obj.IActivationFactory_iface;

/* Canvas factory */
static HRESULT WINAPI canvas_get_property( void *iface, void **value )
{
    TRACE( "iface %p, value %p\n", iface, value );
    return dependency_property_create( NULL, (IDependencyProperty **)value );
}
static HRESULT WINAPI canvas_stub_f64( void *iface, void *elem, double *val )
{
    if (val) *val = 0.0;
    return S_OK;
}
static HRESULT WINAPI canvas_stub_i32( void *iface, void *elem, int *val )
{
    if (val) *val = 0;
    return S_OK;
}
static HRESULT WINAPI canvas_stub_ok( void *iface, ... )
{
    return S_OK;
}

struct canvas_factory_s
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IComposableFactory_iface;
    IInspectable ICanvasStatics_iface;
};

static struct canvas_factory_s canvas_factory_obj;

static ULONG WINAPI canvas_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI canvas_factory_Release( void *iface ) { return 1; }

static HRESULT WINAPI canvas_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &canvas_factory_obj.IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICanvasStatics ))
    {
        *out = &canvas_factory_obj.ICanvasStatics_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICanvasFactory ))
    {
        *out = &canvas_factory_obj.IComposableFactory_iface;
        return S_OK;
    }
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI canvas_factory_GetIids( void *iface, ULONG *count, IID **iids )
{
    IID *array;
    if (!count || !iids) return E_POINTER;
    array = CoTaskMemAlloc( sizeof(IID) );
    if (!array) return E_OUTOFMEMORY;
    array[0] = IID_ICanvasStatics;
    *count = 1;
    *iids = array;
    return S_OK;
}
static HRESULT WINAPI canvas_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.Canvas", 31, name );
}
static HRESULT WINAPI canvas_factory_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI canvas_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return element_create_typed( L"Microsoft.UI.Xaml.Controls.Canvas", (void **)instance );
}
static HRESULT WINAPI canvas_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.Controls.Canvas", (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        impl->outer = (IInspectable *)outer;
        if (inner) { *inner = &impl->IInspectable_iface; IInspectable_AddRef(*inner); }
        *instance = &impl->IInspectable_iface;
    }
    return hr;
}

static const void *canvas_activation_vtbl[] =
{
    canvas_factory_QueryInterface,           /* 0: QI */
    canvas_factory_AddRef,                   /* 1: AddRef */
    canvas_factory_Release,                  /* 2: Release */
    canvas_factory_GetIids,                  /* 3: GetIids */
    canvas_factory_GetRuntimeClassName,      /* 4: GetRuntimeClassName */
    canvas_factory_GetTrustLevel,            /* 5: GetTrustLevel */
    canvas_factory_ActivateInstance,         /* 6: ActivateInstance */
};

static const void *canvas_composable_vtbl[] =
{
    canvas_factory_QueryInterface,           /* 0: QI */
    canvas_factory_AddRef,                   /* 1: AddRef */
    canvas_factory_Release,                  /* 2: Release */
    canvas_factory_GetIids,                  /* 3: GetIids */
    canvas_factory_GetRuntimeClassName,      /* 4: GetRuntimeClassName */
    canvas_factory_GetTrustLevel,            /* 5: GetTrustLevel */
    canvas_factory_CreateInstance,           /* 6: CreateInstance(outer, inner, instance) */
    canvas_factory_CreateInstance,           /* 7 */
    canvas_factory_CreateInstance,           /* 8 */
    canvas_factory_CreateInstance,           /* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot,
};

static const void *canvas_statics_vtbl[] =
{
    canvas_factory_QueryInterface,           /* 0: QI */
    canvas_factory_AddRef,                   /* 1: AddRef */
    canvas_factory_Release,                  /* 2: Release */
    canvas_factory_GetIids,                  /* 3: GetIids */
    canvas_factory_GetRuntimeClassName,      /* 4: GetRuntimeClassName */
    canvas_factory_GetTrustLevel,            /* 5: GetTrustLevel */
    canvas_get_property,                     /* 6: get_LeftProperty */
    canvas_stub_ok,                          /* 7: SetLeft */
    canvas_stub_f64,                         /* 8: GetLeft */
    canvas_get_property,                     /* 9: get_TopProperty */
    canvas_stub_ok,                          /* 10: SetTop */
    canvas_stub_f64,                         /* 11: GetTop */
    canvas_get_property,                     /* 12: get_ZIndexProperty */
    canvas_stub_ok,                          /* 13: SetZIndex */
    canvas_stub_i32,                         /* 14: GetZIndex */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
};

static struct canvas_factory_s canvas_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)canvas_activation_vtbl},
    {(const struct IInspectableVtbl *)canvas_composable_vtbl},
    {(const struct IInspectableVtbl *)canvas_statics_vtbl},
};
IActivationFactory *canvas_factory = &canvas_factory_obj.IActivationFactory_iface;

static HRESULT WINAPI content_control_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return element_create_typed( L"Microsoft.UI.Xaml.Controls.ContentControl", (void **)instance );
}
static HRESULT WINAPI content_control_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.Controls.ContentControl", (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        impl->outer = (IInspectable *)outer;
        if (inner) { *inner = &impl->IInspectable_iface; IInspectable_AddRef(*inner); }
        *instance = &impl->IInspectable_iface;
    }
    return hr;
}

static const void *content_control_factory_vtbl[] =
{
    frame_factory_QueryInterface,
    frame_factory_AddRef,
    frame_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    content_control_factory_ActivateInstance,
};

static const void *content_control_composable_vtbl[] =
{
    composable_factory_QueryInterface,     /* 0: QI */
    composable_factory_AddRef,             /* 1: AddRef */
    composable_factory_Release,            /* 2: Release */
    frame_generic_slot, frame_generic_slot, frame_generic_slot,
    content_control_factory_CreateInstance,/* 6: CreateInstance */
    content_control_factory_CreateInstance,/* 7 */
    content_control_factory_CreateInstance,/* 8 */
    content_control_factory_CreateInstance,/* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot,
};

static struct frame_factory content_control_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)content_control_factory_vtbl},
    {(const struct IInspectableVtbl *)content_control_composable_vtbl},
    1,
};
IActivationFactory *content_control_factory = &content_control_factory_obj.IActivationFactory_iface;

static HRESULT WINAPI element_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return element_create_typed( L"Microsoft.UI.Xaml.FrameworkElement", (void **)instance );
}
static HRESULT WINAPI element_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.FrameworkElement", (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        impl->outer = (IInspectable *)outer;
        if (inner) { *inner = &impl->IInspectable_iface; IInspectable_AddRef(*inner); }
        *instance = &impl->IInspectable_iface;
    }
    return hr;
}

static const void *element_factory_vtbl[] =
{
    frame_factory_QueryInterface,
    frame_factory_AddRef,
    frame_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    element_factory_ActivateInstance,
};

static const void *element_composable_vtbl[] =
{
    composable_factory_QueryInterface,     /* 0: QI */
    composable_factory_AddRef,             /* 1: AddRef */
    composable_factory_Release,            /* 2: Release */
    frame_generic_slot, frame_generic_slot, frame_generic_slot,
    element_factory_CreateInstance,        /* 6: CreateInstance */
    element_factory_CreateInstance,        /* 7 */
    element_factory_CreateInstance,        /* 8 */
    element_factory_CreateInstance,        /* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot,
};

static struct frame_factory element_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)element_factory_vtbl},
    {(const struct IInspectableVtbl *)element_composable_vtbl},
    1,
};
IActivationFactory *element_factory = &element_factory_obj.IActivationFactory_iface;

static HRESULT WINAPI webview2_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create( (IInspectable *)outer, (IInspectable **)inner, (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        impl->is_webview = TRUE;
        impl->class_name = L"Microsoft.UI.Xaml.Controls.WebView2";
        *instance = &impl->IInspectable_iface;
    }
    return hr;
}

static HRESULT WINAPI webview2_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return webview2_factory_CreateInstance( iface, NULL, NULL, (void **)instance );
}

static const void *webview2_factory_vtbl[] =
{
    frame_factory_QueryInterface,
    frame_factory_AddRef,
    frame_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    webview2_factory_ActivateInstance,  /* slot 6: ActivateInstance */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static const void *webview2_composable_vtbl[] =
{
    composable_factory_QueryInterface,     /* 0: QI */
    composable_factory_AddRef,             /* 1: AddRef */
    composable_factory_Release,            /* 2: Release */
    frame_factory_GetIids,                 /* 3: GetIids */
    frame_factory_GetRuntimeClassName,     /* 4: GetRuntimeClassName */
    frame_factory_GetTrustLevel,           /* 5: GetTrustLevel */
    webview2_factory_CreateInstance,       /* 6: CreateInstance(outer, inner, instance) */
    webview2_factory_CreateInstance,       /* 7 */
    webview2_factory_CreateInstance,       /* 8 */
    webview2_factory_CreateInstance,       /* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static const void *webview2_statics_vtbl[] =
{
    statics_factory_QueryInterface,
    statics_factory_AddRef,
    statics_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    wv2_statics_get_SourceProperty,        /* 6: get_SourceProperty */
    wv2_statics_get_prop,                  /* 7: get_CanGoForwardProperty */
    wv2_statics_get_prop,                  /* 8: get_CanGoBackProperty */
    wv2_statics_get_prop,                  /* 9: get_DefaultBackgroundColorProperty */
};

static struct frame_factory webview2_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)webview2_factory_vtbl},
    {(const struct IInspectableVtbl *)webview2_composable_vtbl},
    1,
    {(const struct IInspectableVtbl *)webview2_statics_vtbl},
};
IActivationFactory *webview2_factory = &webview2_factory_obj.IActivationFactory_iface;

static HRESULT WINAPI user_control_factory_CreateInstance( void *iface, void *outer, void **inner, void **instance )
{
    struct xaml_element *impl = NULL;
    HRESULT hr = element_create( (IInspectable *)outer, (IInspectable **)inner, (void **)&impl );
    if (SUCCEEDED(hr) && impl)
    {
        impl->class_name = L"Microsoft.UI.Xaml.Controls.UserControl";
        *instance = &impl->IInspectable_iface;
    }
    return hr;
}

static HRESULT WINAPI user_control_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    return user_control_factory_CreateInstance( iface, NULL, NULL, (void **)instance );
}

static const void *user_control_factory_vtbl[] =
{
    frame_factory_QueryInterface,
    frame_factory_AddRef,
    frame_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    user_control_factory_ActivateInstance,  /* slot 6: ActivateInstance */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static const void *user_control_composable_vtbl[] =
{
    composable_factory_QueryInterface,     /* 0: QI */
    composable_factory_AddRef,             /* 1: AddRef */
    composable_factory_Release,            /* 2: Release */
    frame_factory_GetIids,                 /* 3: GetIids */
    frame_factory_GetRuntimeClassName,     /* 4: GetRuntimeClassName */
    frame_factory_GetTrustLevel,           /* 5: GetTrustLevel */
    user_control_factory_CreateInstance,   /* 6: CreateInstance(outer, inner, instance) */
    user_control_factory_CreateInstance,   /* 7 */
    user_control_factory_CreateInstance,   /* 8 */
    user_control_factory_CreateInstance,   /* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static struct frame_factory user_control_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)user_control_factory_vtbl},
    {(const struct IInspectableVtbl *)user_control_composable_vtbl},
    1,
};
IActivationFactory *user_control_factory = &user_control_factory_obj.IActivationFactory_iface;

static const void *coll_factory_vtbl[] =
{
    frame_factory_QueryInterface,
    frame_factory_AddRef,
    frame_factory_Release,
    frame_factory_GetIids,
    frame_factory_GetRuntimeClassName,
    frame_factory_GetTrustLevel,
    coll_factory_ActivateInstance,  /* slot 6: ActivateInstance */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static const void *coll_composable_vtbl[] =
{
    composable_factory_QueryInterface,     /* 0: QI */
    composable_factory_AddRef,             /* 1: AddRef */
    composable_factory_Release,            /* 2: Release */
    frame_factory_GetIids,                 /* 3: GetIids */
    frame_factory_GetRuntimeClassName,     /* 4: GetRuntimeClassName */
    frame_factory_GetTrustLevel,           /* 5: GetTrustLevel */
    coll_factory_CreateInstance,           /* 6: CreateInstance(outer, inner, instance) */
    coll_factory_CreateInstance,           /* 7 */
    coll_factory_CreateInstance,           /* 8 */
    coll_factory_CreateInstance,           /* 9 */
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot, frame_generic_slot, frame_generic_slot, frame_generic_slot,
    frame_generic_slot,
};

static struct frame_factory coll_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)coll_factory_vtbl},
    {(const struct IInspectableVtbl *)coll_composable_vtbl},
    1,
};
IActivationFactory *coll_factory = &coll_factory_obj.IActivationFactory_iface;

