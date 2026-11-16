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

#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static const GUID IID_IReferenceTrackerTarget_WinUI =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
static const GUID IID_IReferenceTracker =
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
static const GUID IID_IReferenceTrackerTarget =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
static const GUID IID_IWeakReferenceSource =
    { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_IWeakReference =
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

struct xaml_frame_prop
{
    void *dp;
    IInspectable *value;
};

struct xaml_frame
{
    IInspectable IInspectable_iface;
    const void **IDependencyObject_iface;
    LONG ref;
    IInspectable *outer;
    IInspectable *content;
    IDCompositionVisual *visual;
    struct xaml_frame_prop props[16];
    int prop_count;
};

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

static IDependencyProperty *wv2_source_dp;

static HRESULT WINAPI wv2_statics_get_SourceProperty( void *iface, void **value )
{
    if (!value) return E_POINTER;
    if (!wv2_source_dp)
        dependency_property_create( NULL, &wv2_source_dp );
    *value = wv2_source_dp;
    if (*value) IDependencyProperty_AddRef( wv2_source_dp );
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI wv2_statics_get_prop( void *iface, void **value )
{
    if (!value) return E_POINTER;
    return dependency_property_create( NULL, (IDependencyProperty **)value );
}

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
static void **vtbl_dup( const void *src );
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
static void **vtbl_dup( const void *src )
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

struct xaml_element
{
    IInspectable IInspectable_iface;
    IInspectable IGrid_iface;
    IInspectable IPanel_iface;
    IInspectable IContentControl_iface;
    IInspectable IRowDefinition_iface;
    IInspectable IColumnDefinition_iface;
    IInspectable IWebView2_iface;
    IInspectable IWebView22_iface;
    IInspectable IUIElement_iface;
    IInspectable IDependencyObject_iface;
    LONG ref;
    int visibility;
    IInspectable *outer;
    IDCompositionVisual *visual;
    IInspectable *row_defs;
    IInspectable *col_defs;
    IInspectable *children;
    IInspectable *content;
    IInspectable *core_webview2;
    IInspectable *init_handler;
    IInspectable *wv2_source;
    HSTRING wv2_source_uri;
    void *loaded_fn;    /* ABI RoutedEventHandler Invoke fn (vtbl[3]) */
    BOOL init_triggered;
    BOOL browser_queued;
    BOOL browser_started;
    BOOL navigated;
    BOOL real_loader;
    BOOL visual_attached;
    BOOL has_comp_ctrl;
    void *loader_env;
    void *loader_controller;
    void *loader_comp_ctrl;
    void *loader_core;
    BOOL is_webview;
    HWND parent_hwnd;
    unsigned int qi_count;
    const WCHAR *class_name;
};

static inline struct xaml_element *impl_from_element( void *iface )
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
static inline struct xaml_element *impl_from_IWebView2( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IWebView2_iface );
}
static inline struct xaml_element *impl_from_IWebView22( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IWebView22_iface );
}
static inline struct xaml_element *impl_from_IDependencyObject( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IDependencyObject_iface );
}

static HRESULT WINAPI element_QueryInterface( IInspectable *iface, REFIID iid, void **out );
static HRESULT WINAPI element_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
static ULONG WINAPI element_AddRef( IInspectable *iface );
static ULONG WINAPI element_Release( IInspectable *iface );

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

static HRESULT WINAPI element_wv2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI element_wv2_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI element_wv2_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI element_wv22_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI element_wv22_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI element_wv22_Release( IInspectable *iface )
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
static HRESULT WINAPI element_wv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
static HRESULT WINAPI element_wv22_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}

static HRESULT WINAPI element_QueryInterface( IInspectable *iface, REFIID iid, void **out )
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

static ULONG WINAPI element_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_element( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    ERR( "element_AddRef iface %p ref %lu\n", iface, ref );
    return ref;
}

static ULONG WINAPI element_Release( IInspectable *iface )
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

static HRESULT WINAPI element_GetIids( IInspectable *iface, ULONG *count, IID **iids )
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

static HRESULT WINAPI element_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_element( iface );
    const WCHAR *str = impl->class_name ? impl->class_name : L"Windows.UI.Xaml.FrameworkElement";
    ERR( "element_GetRuntimeClassName on %p -> %s\n", iface, debugstr_w(str) );
    return WindowsCreateString( str, wcslen(str), name );
}

static HRESULT WINAPI element_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    ERR( "element_GetTrustLevel iface %p\n", iface );
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI element_generic_slot( void *iface, ... )
{
    ERR( "element_generic_slot iface %p\n", iface );
    return S_OK;
}

/* slot-probe prototypes (definitions near wv2_vtbl below) */
#define WVPROT(i) static HRESULT WINAPI wv2_probe##i( void *iface, ... );
WVPROT(0) WVPROT(1) WVPROT(2) WVPROT(3) WVPROT(4) WVPROT(5) WVPROT(6) WVPROT(7)
WVPROT(8) WVPROT(9) WVPROT(10) WVPROT(11) WVPROT(12) WVPROT(13) WVPROT(14) WVPROT(15)
WVPROT(16) WVPROT(17) WVPROT(18) WVPROT(19) WVPROT(20) WVPROT(21) WVPROT(22) WVPROT(23)
WVPROT(24) WVPROT(25) WVPROT(26) WVPROT(27) WVPROT(28) WVPROT(29) WVPROT(30) WVPROT(31)
WVPROT(32) WVPROT(33) WVPROT(34) WVPROT(35) WVPROT(36) WVPROT(37) WVPROT(38) WVPROT(39)
WVPROT(40) WVPROT(41) WVPROT(42) WVPROT(43) WVPROT(44) WVPROT(45) WVPROT(46) WVPROT(47)
WVPROT(48) WVPROT(49) WVPROT(50) WVPROT(51) WVPROT(52) WVPROT(53) WVPROT(54) WVPROT(55)
WVPROT(56) WVPROT(57) WVPROT(58) WVPROT(59) WVPROT(60) WVPROT(61) WVPROT(62) WVPROT(63)

/* FrameworkElement numeric getters (Width/Height/ActualWidth/ActualHeight
 * share this slot body): report the hosting window's real client width,
 * falling back to monitor-derived layout metrics until a window exists. */
static HRESULT WINAPI element_get_double( void *iface, double *val )
{
    struct xaml_element *impl = impl_from_element( (IInspectable *)iface );
    HWND hw = impl->parent_hwnd;
    SIZE sz;

    if (!hw || !IsWindow( hw )) hw = xaml_get_window_hwnd();
    if (val)
    {
        xaml_get_layout_metrics( hw, &sz, NULL );
        *val = (double)sz.cx;
    }
    return S_OK;
}

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
typedef struct IWeakReference IWeakReference;
typedef struct IWeakReferenceVtbl
{
    HRESULT (WINAPI *QueryInterface)( IWeakReference *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( IWeakReference *iface );
    ULONG (WINAPI *Release)( IWeakReference *iface );
    HRESULT (WINAPI *Resolve)( IWeakReference *iface, REFIID iid, void **out );
} IWeakReferenceVtbl;
struct IWeakReference
{
    const IWeakReferenceVtbl *lpVtbl;
};

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

static HRESULT WINAPI weakref_Resolve( IWeakReference *iface, REFIID iid, void **out )
{
    struct xaml_weakref *impl = impl_from_IWeakReference( iface );
    return element_QueryInterface( &impl->element->IInspectable_iface, iid, out );
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

static HRESULT WINAPI element_add_Loaded( void *iface, void *handler, EventRegistrationToken *token )
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

static HRESULT WINAPI element_remove_Loaded( void *iface, EventRegistrationToken token )
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
static HRESULT WINAPI element_grid_get_RowDefinitions_abi3( void *iface, void *count, void **out )
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

struct xaml_async_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    BOOL completed;
    HRESULT hr;
    IAsyncActionCompletedHandler *handler;
};

static inline struct xaml_async_action *impl_from_async_action( IAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_async_action, IAsyncAction_iface );
}

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

static inline struct xaml_async_action *impl_from_async_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_async_action, IAsyncInfo_iface );
}
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

static HRESULT xaml_async_action_create( IAsyncAction **out )
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

enum cwv2_pending_kind
{
    CWV2_OP_SETTING_BOOL,
    CWV2_OP_SETTING_UA,
    CWV2_OP_ADD_HOST_OBJECT,
    CWV2_OP_ADD_SCRIPT,
    CWV2_OP_ADD_WRS_FILTER,
    CWV2_OP_ADD_EVENT,
    CWV2_OP_ADD_EVENT2,
    CWV2_OP_ADD_EVENT11,
    CWV2_OP_DEVTOOLS,
};

struct cwv2_pending_op
{
    struct cwv2_pending_op *next;
    enum cwv2_pending_kind kind;
    int arg_int;
    BOOL arg_bool;
    WCHAR *str1;
    WCHAR *str2;
    void *obj;
    const void **avtbl;
    const GUID *aiid;
};

struct core_webview2
{
    IInspectable IInspectable_iface;
    IInspectable ICoreWebView2_2_iface;
    IInspectable ICoreWebView2_3_iface;
    IInspectable ICoreWebView2_11_iface;
    IInspectable ICoreWebView2_13_iface;
    IInspectable ICoreWebView2_stub_iface;
    LONG ref;
    HSTRING source_uri;
    BOOL real_loader;
    void *loader_core;         /* COM ICoreWebView2* */
    void *loader_core2;        /* COM ICoreWebView2_2* */
    void *loader_core11;       /* COM ICoreWebView2_11* */
    void *loader_core13;       /* COM ICoreWebView2_13* */
    void *tracker_source;      /* webview2loader webview object */
    struct xaml_element *owner;
    struct cwv2_pending_op *pending_head;
    struct cwv2_pending_op *pending_tail;
};

static void cwv2_queue_op( struct core_webview2 *impl, enum cwv2_pending_kind kind,
        int arg_int, BOOL arg_bool, const WCHAR *s1, const WCHAR *s2,
        void *obj, const void **avtbl, const GUID *aiid )
{
    struct cwv2_pending_op *op;
    if (!impl || !(op = calloc( 1, sizeof(*op) ))) return;
    op->kind = kind;
    op->arg_int = arg_int;
    op->arg_bool = arg_bool;
    if (s1) op->str1 = wcsdup( s1 );
    if (s2) op->str2 = wcsdup( s2 );
    op->obj = obj;
    if (obj) ((IUnknown *)obj)->lpVtbl->AddRef( (IUnknown *)obj );
    op->avtbl = avtbl;
    op->aiid = aiid;
    if (impl->pending_tail) impl->pending_tail->next = op;
    else impl->pending_head = op;
    impl->pending_tail = op;
}

static const GUID IID_ICoreWebView2_com =
    { 0x76eceacb, 0x0462, 0x4d94, { 0xac, 0x83, 0x42, 0x3a, 0x67, 0x93, 0x77, 0x5e } };
static const GUID IID_ICoreWebView2_2_com =
    { 0x9e8f0cf8, 0xe670, 0x4b5e, { 0xb2, 0xbc, 0x73, 0xe0, 0x61, 0xe3, 0x18, 0x4c } };
static const GUID IID_ICoreWebView2_11_com =
    { 0x0be78e56, 0xc193, 0x4051, { 0xb9, 0x43, 0x23, 0xb4, 0x60, 0xc0, 0x8b, 0xdb } };
static const GUID IID_ICoreWebView2_13_com =
    { 0xf75f09a8, 0x667e, 0x4983, { 0x88, 0xd6, 0xc8, 0x77, 0x3f, 0x31, 0x5e, 0x84 } };
static const GUID IID_ICoreWebView2Settings_com =
    { 0xe562e4f0, 0xd7fa, 0x43ac, { 0x8d, 0x71, 0xc0, 0x51, 0x50, 0x49, 0x9f, 0x00 } };
static const GUID IID_ICoreWebView2Settings2_com =
    { 0xee9a0f68, 0xf46c, 0x4e32, { 0xac, 0x23, 0xef, 0x8c, 0xac, 0x22, 0x4d, 0x2a } };

static const GUID IID_ICoreWebView2 =
    { 0x3a3f559a, 0xe5e9, 0x5338, { 0xbb, 0x67, 0x4e, 0xb0, 0x50, 0x4a, 0x4f, 0x14 } };
static const GUID IID_ICoreWebView2Profile =
    { 0xb87fd0bc, 0x8e64, 0x56a0, { 0x94, 0x87, 0x4b, 0x13, 0x98, 0x1f, 0xd2, 0x9e } };
static const GUID IID_ICoreWebView2_2 =
    { 0x578cb133, 0x2873, 0x5408, { 0xbd, 0x9e, 0x38, 0x9b, 0xbe, 0x9f, 0xa7, 0xfa } };
static const GUID IID_ICoreWebView2_3 =
    { 0xa8c76ae7, 0x6170, 0x5dfe, { 0x8f, 0x00, 0x79, 0xcd, 0x76, 0xa9, 0xb4, 0xd9 } };
static const GUID IID_ICoreWebView2_4 =
    { 0x4ac595ce, 0x1502, 0x5775, { 0xb2, 0xc8, 0x22, 0xc1, 0x1a, 0x36, 0x9c, 0x25 } };
static const GUID IID_ICoreWebView2_5 =
    { 0xdd6af643, 0x220c, 0x5dc6, { 0xb0, 0xa8, 0x22, 0xc4, 0x1e, 0x47, 0x25, 0x95 } };
static const GUID IID_ICoreWebView2_6 =
    { 0x92b34b96, 0x853d, 0x5bb6, { 0xac, 0x52, 0x30, 0x29, 0x7c, 0xe8, 0x05, 0xf1 } };
static const GUID IID_ICoreWebView2_7 =
    { 0xf9b7107a, 0x2e09, 0x5596, { 0xa0, 0x33, 0x91, 0x1b, 0xa1, 0x23, 0x15, 0xf7 } };
static const GUID IID_ICoreWebView2_8 =
    { 0xaa2503c0, 0x8d1c, 0x5a3d, { 0xb8, 0x98, 0xf5, 0x5f, 0x75, 0x95, 0x26, 0x8a } };
static const GUID IID_ICoreWebView2_9 =
    { 0x64b2ec16, 0x0b29, 0x5216, { 0xbf, 0x86, 0xe5, 0x75, 0xc8, 0x8f, 0x70, 0x31 } };
static const GUID IID_ICoreWebView2_10 =
    { 0xa7b20434, 0x970f, 0x54b1, { 0xaa, 0x63, 0x3c, 0x42, 0x67, 0x1f, 0xa9, 0xab } };
static const GUID IID_ICoreWebView2_11 =
    { 0xc00acbb1, 0xae32, 0x501f, { 0xad, 0x19, 0x9d, 0x0a, 0xc3, 0x2d, 0x61, 0x42 } };
static const GUID IID_ICoreWebView2_12 =
    { 0xdbbbe9a1, 0x18d3, 0x5f67, { 0xb3, 0x62, 0x0f, 0x4a, 0xe9, 0x37, 0xd7, 0x54 } };
static const GUID IID_ICoreWebView2_13 =
    { 0x314b5846, 0xdbc7, 0x5de4, { 0xa7, 0x92, 0x64, 0x7e, 0xa0, 0xf3, 0x29, 0x6a } };

static const GUID IID_ICoreWebView2Settings =
    { 0x003b325e, 0x74cd, 0x52dd, { 0x80, 0x24, 0xeb, 0xb8, 0xbe, 0x38, 0xe4, 0x8e } };
static const GUID IID_ICoreWebView2Settings2 =
    { 0x377d3480, 0xfdb2, 0x56e7, { 0xba, 0xde, 0x50, 0x7d, 0x35, 0x28, 0x87, 0xe9 } };
static const GUID IID_ICoreWebView2Settings3 =
    { 0x52200f01, 0x5309, 0x5b2e, { 0xa0, 0x3c, 0x3d, 0x26, 0x77, 0x59, 0x19, 0x40 } };
static const GUID IID_ICoreWebView2Settings4 =
    { 0xd6a955f0, 0xdaef, 0x5a6a, { 0xa6, 0xf6, 0xc7, 0x2f, 0x0e, 0xde, 0x76, 0x20 } };
static const GUID IID_ICoreWebView2Settings5 =
    { 0xafc42b23, 0x4839, 0x5d73, { 0xac, 0xf7, 0xe0, 0x33, 0x56, 0x31, 0xab, 0xf5 } };
static const GUID IID_ICoreWebView2Settings6 =
    { 0x3fe4ae85, 0x0540, 0x5bf1, { 0xb4, 0xd9, 0x99, 0xec, 0x57, 0xaa, 0x64, 0xf5 } };
static const GUID IID_ICoreWebView2Settings7 =
    { 0x688027cd, 0x9f84, 0x59e8, { 0x8d, 0x5c, 0x91, 0x12, 0x3d, 0xf2, 0x4b, 0x92 } };
static const GUID IID_ICoreWebView2Settings8 =
    { 0x956f1a8f, 0x3198, 0x5577, { 0xb2, 0x50, 0x7d, 0x91, 0xd1, 0x7f, 0x7e, 0xed } };
static const GUID IID_ICoreWebView2Settings9 =
    { 0x4c346681, 0x714d, 0x5a3d, { 0x81, 0x05, 0x2a, 0x7b, 0x80, 0xbe, 0xea, 0xb5 } };
static const GUID IID_ICoreWebView2Settings_Manual =
    { 0x0a538c87, 0xe000, 0x511c, { 0x87, 0xca, 0xde, 0xd3, 0x41, 0x3d, 0x03, 0xda } };
static const GUID IID_ICoreWebView2Environment =
    { 0xd8cc7831, 0xb783, 0x556b, { 0xb9, 0xce, 0x89, 0x9c, 0x1e, 0x95, 0xd5, 0x85 } };

struct cwv2_settings
{
    IInspectable IInspectable_iface;
    IInspectable ISettings2_iface;
    IInspectable ISettings_stub_iface;
    LONG ref;
    void *loader_settings;   /* COM ICoreWebView2Settings* */
    void *loader_settings2;  /* COM ICoreWebView2Settings2* */
    struct core_webview2 *owner;
};

static inline struct cwv2_settings *impl_from_settings( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_settings, IInspectable_iface );
}
static inline struct cwv2_settings *impl_from_settings2( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_settings, ISettings2_iface );
}
static inline struct cwv2_settings *impl_from_settings_stub( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_settings, ISettings_stub_iface );
}

static HRESULT WINAPI cwv2_settings_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct cwv2_settings *impl = impl_from_settings( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2Settings2 ))
    {
        *out = &impl->ISettings2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2Settings3 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings4 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings5 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings6 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings7 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings8 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings9 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings_Manual ))
    {
        *out = &impl->ISettings_stub_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    ERR( "cwv2_settings_QueryInterface: unknown IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_settings_AddRef( IInspectable *iface )
{
    struct cwv2_settings *impl = impl_from_settings( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cwv2_settings_Release( IInspectable *iface )
{
    struct cwv2_settings *impl = impl_from_settings( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->loader_settings2) ((IUnknown *)impl->loader_settings2)->lpVtbl->Release( impl->loader_settings2 );
        if (impl->loader_settings) ((IUnknown *)impl->loader_settings)->lpVtbl->Release( impl->loader_settings );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI cwv2_settings2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return cwv2_settings_QueryInterface( &impl_from_settings2( iface )->IInspectable_iface, iid, out );
}
static ULONG WINAPI cwv2_settings2_AddRef( IInspectable *iface )
{
    return cwv2_settings_AddRef( &impl_from_settings2( iface )->IInspectable_iface );
}
static ULONG WINAPI cwv2_settings2_Release( IInspectable *iface )
{
    return cwv2_settings_Release( &impl_from_settings2( iface )->IInspectable_iface );
}
static HRESULT WINAPI cwv2_settings_stub_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return cwv2_settings_QueryInterface( &impl_from_settings_stub( iface )->IInspectable_iface, iid, out );
}
static ULONG WINAPI cwv2_settings_stub_AddRef( IInspectable *iface )
{
    return cwv2_settings_AddRef( &impl_from_settings_stub( iface )->IInspectable_iface );
}
static ULONG WINAPI cwv2_settings_stub_Release( IInspectable *iface )
{
    return cwv2_settings_Release( &impl_from_settings_stub( iface )->IInspectable_iface );
}
static HRESULT WINAPI cwv2_settings_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    IID *arr;
    if (!(arr = CoTaskMemAlloc( sizeof(IID) ))) return E_OUTOFMEMORY;
    *arr = IID_ICoreWebView2Settings;
    if (count) *count = 1;
    if (iids) *iids = arr; else CoTaskMemFree( arr );
    return S_OK;
}
static HRESULT WINAPI cwv2_settings_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Settings", 47, name ); }
static HRESULT WINAPI cwv2_settings_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT settings_get_bool( struct cwv2_settings *impl, int com_slot, boolean *val )
{
    BOOL b = TRUE;
    if (!val) return E_POINTER;
    if (impl->loader_settings && *(void ***)impl->loader_settings)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = (*(void ***)impl->loader_settings)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_settings, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = TRUE;
    return S_OK;
}
static HRESULT settings_put_bool( struct cwv2_settings *impl, int com_slot, boolean val )
{
    if (impl->loader_settings && *(void ***)impl->loader_settings)
    {
        HRESULT (WINAPI *fn)( void *, BOOL ) = (*(void ***)impl->loader_settings)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_settings, !!val );
    }
    if (impl->owner)
        cwv2_queue_op( impl->owner, CWV2_OP_SETTING_BOOL, com_slot, !!val, NULL, NULL, NULL, NULL, NULL );
    return S_OK;
}

#define SETTINGS_BOOL_PAIR(name, com_get_slot, com_put_slot) \
    static HRESULT WINAPI cwv2_settings_get_##name( void *iface, boolean *val ) \
    { return settings_get_bool( impl_from_settings( iface ), com_get_slot, val ); } \
    static HRESULT WINAPI cwv2_settings_put_##name( void *iface, boolean val ) \
    { return settings_put_bool( impl_from_settings( iface ), com_put_slot, val ); }

SETTINGS_BOOL_PAIR( IsScriptEnabled, 3, 4 )
SETTINGS_BOOL_PAIR( IsWebMessageEnabled, 5, 6 )
SETTINGS_BOOL_PAIR( AreDefaultScriptDialogsEnabled, 7, 8 )
SETTINGS_BOOL_PAIR( IsStatusBarEnabled, 9, 10 )
SETTINGS_BOOL_PAIR( AreDevToolsEnabled, 11, 12 )
SETTINGS_BOOL_PAIR( AreDefaultContextMenusEnabled, 13, 14 )
SETTINGS_BOOL_PAIR( AreHostObjectsAllowed, 15, 16 )
SETTINGS_BOOL_PAIR( IsZoomControlEnabled, 17, 18 )
SETTINGS_BOOL_PAIR( IsBuiltInErrorPageEnabled, 19, 20 )

static HRESULT WINAPI cwv2_settings2_get_UserAgent( void *iface, HSTRING *ua )
{
    struct cwv2_settings *impl = impl_from_settings2( iface );
    if (!ua) return E_POINTER;
    if (impl->loader_settings2 && *(void ***)impl->loader_settings2)
    {
        HRESULT (WINAPI *fn)( void *, LPWSTR * ) = (*(void ***)impl->loader_settings2)[21];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            LPWSTR raw = NULL;
            HRESULT hr = fn( impl->loader_settings2, &raw );
            if (SUCCEEDED( hr ) && raw)
            {
                hr = WindowsCreateString( raw, lstrlenW( raw ), ua );
                CoTaskMemFree( raw );
                return hr;
            }
            if (raw) CoTaskMemFree( raw );
            return hr;
        }
    }
    return WindowsCreateString( L"", 0, ua );
}

static HRESULT WINAPI cwv2_settings2_put_UserAgent( void *iface, HSTRING ua )
{
    struct cwv2_settings *impl = impl_from_settings2( iface );
    const WCHAR *s = ua ? WindowsGetStringRawBuffer( ua, NULL ) : L"";
    ERR( "cwv2_settings2_put_UserAgent: %s\n", debugstr_w( s ) );
    if (impl->loader_settings2 && *(void ***)impl->loader_settings2)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = (*(void ***)impl->loader_settings2)[22];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_settings2, s );
    }
    if (impl->owner)
        cwv2_queue_op( impl->owner, CWV2_OP_SETTING_UA, 0, FALSE, s, NULL, NULL, NULL, NULL );
    return S_OK;
}

static const void *cwv2_settings_vtbl[256] =
{
    [0] = cwv2_settings_QueryInterface,
    [1] = cwv2_settings_AddRef,
    [2] = cwv2_settings_Release,
    [3] = cwv2_settings_GetIids,
    [4] = cwv2_settings_GetRuntimeClassName,
    [5] = cwv2_settings_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = cwv2_settings_get_IsScriptEnabled,
    [7] = cwv2_settings_put_IsScriptEnabled,
    [8] = cwv2_settings_get_IsWebMessageEnabled,
    [9] = cwv2_settings_put_IsWebMessageEnabled,
    [10] = cwv2_settings_get_AreDefaultScriptDialogsEnabled,
    [11] = cwv2_settings_put_AreDefaultScriptDialogsEnabled,
    [12] = cwv2_settings_get_IsStatusBarEnabled,
    [13] = cwv2_settings_put_IsStatusBarEnabled,
    [14] = cwv2_settings_get_AreDevToolsEnabled,
    [15] = cwv2_settings_put_AreDevToolsEnabled,
    [16] = cwv2_settings_get_AreDefaultContextMenusEnabled,
    [17] = cwv2_settings_put_AreDefaultContextMenusEnabled,
    [18] = cwv2_settings_get_AreHostObjectsAllowed,
    [19] = cwv2_settings_put_AreHostObjectsAllowed,
    [20] = cwv2_settings_get_IsZoomControlEnabled,
    [21] = cwv2_settings_put_IsZoomControlEnabled,
    [22] = cwv2_settings_get_IsBuiltInErrorPageEnabled,
    [23] = cwv2_settings_put_IsBuiltInErrorPageEnabled,
};

static const void *cwv2_settings2_vtbl[256] =
{
    [0] = cwv2_settings2_QueryInterface,
    [1] = cwv2_settings2_AddRef,
    [2] = cwv2_settings2_Release,
    [3] = cwv2_settings_GetIids,
    [4] = cwv2_settings_GetRuntimeClassName,
    [5] = cwv2_settings_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = cwv2_settings2_get_UserAgent,
    [7] = cwv2_settings2_put_UserAgent,
};

static const void *cwv2_settings_stub_vtbl[256] =
{
    [0] = cwv2_settings_stub_QueryInterface,
    [1] = cwv2_settings_stub_AddRef,
    [2] = cwv2_settings_stub_Release,
    [3] = cwv2_settings_GetIids,
    [4] = cwv2_settings_GetRuntimeClassName,
    [5] = cwv2_settings_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
};

struct cwv2_env
{
    IInspectable IInspectable_iface;
    LONG ref;
};

static HRESULT WINAPI cwv2_env_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Environment ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_env_AddRef( IInspectable *iface )
{
    struct cwv2_env *impl = (struct cwv2_env *)iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cwv2_env_Release( IInspectable *iface )
{
    struct cwv2_env *impl = (struct cwv2_env *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cwv2_env_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cwv2_env_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Environment", 50, name ); }
static HRESULT WINAPI cwv2_env_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static const void *cwv2_env_vtbl[256] =
{
    [0] = cwv2_env_QueryInterface,
    [1] = cwv2_env_AddRef,
    [2] = cwv2_env_Release,
    [3] = cwv2_env_GetIids,
    [4] = cwv2_env_GetRuntimeClassName,
    [5] = cwv2_env_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
};

static inline struct core_webview2 *impl_from_cwv2( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, IInspectable_iface );
}
static inline struct core_webview2 *impl_from_cwv2_2( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_2_iface );
}
static inline struct core_webview2 *impl_from_cwv2_3( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_3_iface );
}
static inline struct core_webview2 *impl_from_cwv2_11( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_11_iface );
}
static inline struct core_webview2 *impl_from_cwv2_13( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_13_iface );
}
static inline struct core_webview2 *impl_from_cwv2_stub( void *iface )
{
    return CONTAINING_RECORD( iface, struct core_webview2, ICoreWebView2_stub_iface );
}

static HRESULT WINAPI cwv2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    TRACE( "cwv2_QI %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2 ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_2 ))
    {
        *out = &impl->ICoreWebView2_2_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_3 ))
    {
        *out = &impl->ICoreWebView2_3_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_11 ))
    {
        *out = &impl->ICoreWebView2_11_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_13 ))
    {
        *out = &impl->ICoreWebView2_13_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_4 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_5 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_6 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_7 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_8 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_9 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_10 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_12 ))
    {
        *out = &impl->ICoreWebView2_stub_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    ERR( "cwv2_QueryInterface: unknown IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_AddRef( IInspectable *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cwv2_Release( IInspectable *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->source_uri) WindowsDeleteString( impl->source_uri );
        if (impl->loader_core2) ((IUnknown *)impl->loader_core2)->lpVtbl->Release( impl->loader_core2 );
        if (impl->loader_core11) ((IUnknown *)impl->loader_core11)->lpVtbl->Release( impl->loader_core11 );
        if (impl->loader_core13) ((IUnknown *)impl->loader_core13)->lpVtbl->Release( impl->loader_core13 );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI cwv2_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    IID *arr;
    ERR( "cwv2_GetIids iface %p\n", iface );
    if (!(arr = CoTaskMemAlloc( sizeof(IID) ))) return E_OUTOFMEMORY;
    *arr = IID_ICoreWebView2;
    if (count) *count = 1;
    if (iids) *iids = arr; else CoTaskMemFree( arr );
    return S_OK;
}
static HRESULT WINAPI cwv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ ERR( "cwv2_GetRuntimeClassName\n" ); return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2", 39, name ); }
static HRESULT WINAPI cwv2_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI cwv2_2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    return cwv2_QueryInterface( &impl_from_cwv2_2( iface )->IInspectable_iface, iid, out );
}
static ULONG WINAPI cwv2_2_AddRef( IInspectable *iface )
{
    return cwv2_AddRef( &impl_from_cwv2_2( iface )->IInspectable_iface );
}
static ULONG WINAPI cwv2_2_Release( IInspectable *iface )
{
    return cwv2_Release( &impl_from_cwv2_2( iface )->IInspectable_iface );
}

#define CWV2_SUB_IUNKNOWN(sub) \
    static HRESULT WINAPI cwv2_##sub##_QueryInterface( IInspectable *iface, REFIID iid, void **out ) \
    { return cwv2_QueryInterface( &impl_from_cwv2_##sub( iface )->IInspectable_iface, iid, out ); } \
    static ULONG WINAPI cwv2_##sub##_AddRef( IInspectable *iface ) \
    { return cwv2_AddRef( &impl_from_cwv2_##sub( iface )->IInspectable_iface ); } \
    static ULONG WINAPI cwv2_##sub##_Release( IInspectable *iface ) \
    { return cwv2_Release( &impl_from_cwv2_##sub( iface )->IInspectable_iface ); }

CWV2_SUB_IUNKNOWN( 3 )
CWV2_SUB_IUNKNOWN( 11 )
CWV2_SUB_IUNKNOWN( 13 )
CWV2_SUB_IUNKNOWN( stub )

static HRESULT WINAPI cwv2_get_Settings( void *iface, void **settings )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    struct cwv2_settings *s;
    void *com_settings = NULL;

    if (!settings) return E_POINTER;
    if (!(s = calloc( 1, sizeof(*s) ))) return E_OUTOFMEMORY;
    s->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_settings_vtbl );
    s->ISettings2_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_settings2_vtbl );
    s->ISettings_stub_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_settings_stub_vtbl );
    s->ref = 1;
    s->owner = impl;

    if (impl->loader_core && impl->real_loader && *(void ***)impl->loader_core)
    {
        HRESULT (WINAPI *fn)( void *, void ** ) = (*(void ***)impl->loader_core)[3];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            __TRY
            {
                HRESULT hr = fn( impl->loader_core, &com_settings );
                if (SUCCEEDED( hr ) && com_settings)
                {
                    s->loader_settings = com_settings;
                    ((IUnknown *)com_settings)->lpVtbl->QueryInterface( com_settings,
                            &IID_ICoreWebView2Settings2_com, &s->loader_settings2 );
                }
            }
            __EXCEPT_PAGE_FAULT
            {
                ERR( "cwv2_get_Settings: loader_core->get_Settings faulted\n" );
            }
            __ENDTRY;
        }
    }

    *settings = &s->IInspectable_iface;
    ERR( "cwv2_get_Settings -> %p (com=%p com2=%p)\n", *settings, s->loader_settings, s->loader_settings2 );
    return S_OK;
}

typedef struct ICoreWebView2Controller ICoreWebView2Controller;
typedef struct ICoreWebView2ControllerVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWebView2Controller *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWebView2Controller *This );
    ULONG (WINAPI *Release)( ICoreWebView2Controller *This );
    HRESULT (WINAPI *get_IsVisible)( ICoreWebView2Controller *This, BOOL *isVisible );
    HRESULT (WINAPI *put_IsVisible)( ICoreWebView2Controller *This, BOOL isVisible );
    HRESULT (WINAPI *get_Bounds)( ICoreWebView2Controller *This, RECT *bounds );
    HRESULT (WINAPI *put_Bounds)( ICoreWebView2Controller *This, RECT bounds );
    HRESULT (WINAPI *get_ZoomFactor)( ICoreWebView2Controller *This, double *zoomFactor );
    HRESULT (WINAPI *put_ZoomFactor)( ICoreWebView2Controller *This, double zoomFactor );
    void *add_ZoomFactorChanged;
    void *remove_ZoomFactorChanged;
    HRESULT (WINAPI *SetBoundsAndZoomFactor)( ICoreWebView2Controller *This, RECT bounds, double zoomFactor );
    HRESULT (WINAPI *MoveFocus)( ICoreWebView2Controller *This, int reason );
    void *add_MoveFocusRequested;
    void *remove_MoveFocusRequested;
    void *add_GotFocus;
    void *remove_GotFocus;
    void *add_LostFocus;
    void *remove_LostFocus;
    void *add_AcceleratorKeyPressed;
    void *remove_AcceleratorKeyPressed;
    HRESULT (WINAPI *get_ParentWindow)( ICoreWebView2Controller *This, HWND *parentWindow );
    HRESULT (WINAPI *put_ParentWindow)( ICoreWebView2Controller *This, HWND parentWindow );
    HRESULT (WINAPI *NotifyParentWindowPositionChanged)( ICoreWebView2Controller *This );
    HRESULT (WINAPI *Close)( ICoreWebView2Controller *This );
    HRESULT (WINAPI *get_CoreWebView2)( ICoreWebView2Controller *This, void **coreWebView2 );
} ICoreWebView2ControllerVtbl;

struct ICoreWebView2Controller
{
    const ICoreWebView2ControllerVtbl *lpVtbl;
};

typedef struct ICoreWebView2CompositionController ICoreWebView2CompositionController;
/* Verified against WebView2.idl / WebView2.h: ICoreWebView2CompositionController has
 * 11 vtable slots (0..2 IUnknown, 3 get_RootVisualTarget, 4 put_RootVisualTarget,
 * 5 SendMouseInput, 6 SendPointerInput, 7 get_Cursor, 8 get_SystemCursorId,
 * 9 add_CursorChanged, 10 remove_CursorChanged). There is no SendKeyboardInput or
 * SendCharacterInput slot on ICoreWebView2CompositionController; keyboard/character
 * input in visual-hosting mode enters EmbeddedBrowserWebView.dll through its
 * in-process ClientWindowWin32 child HWND (ClientWindow::OnKeyEvent ->
 * Controller::SendKeyEvent -> EmbeddedBrowserProxy::SendInputEvent). */
typedef struct ICoreWebView2CompositionControllerVtbl
{
    HRESULT (WINAPI *QueryInterface)( ICoreWebView2CompositionController *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ICoreWebView2CompositionController *This );
    ULONG (WINAPI *Release)( ICoreWebView2CompositionController *This );
    HRESULT (WINAPI *get_RootVisualTarget)( ICoreWebView2CompositionController *This, IUnknown **target );
    HRESULT (WINAPI *put_RootVisualTarget)( ICoreWebView2CompositionController *This, IUnknown *target );
    HRESULT (WINAPI *SendMouseInput)( ICoreWebView2CompositionController *This, UINT32 eventKind,
                                      UINT32 virtualKeys, UINT32 mouseData, POINT point );
    HRESULT (WINAPI *SendPointerInput)( ICoreWebView2CompositionController *This, UINT32 eventKind, void *pointerInfo );
    HRESULT (WINAPI *get_Cursor)( ICoreWebView2CompositionController *This, HCURSOR *cursor );
    HRESULT (WINAPI *get_SystemCursorId)( ICoreWebView2CompositionController *This, UINT32 *systemCursorId );
    void *add_CursorChanged;
    void *remove_CursorChanged;
} ICoreWebView2CompositionControllerVtbl;

struct ICoreWebView2CompositionController
{
    const ICoreWebView2CompositionControllerVtbl *lpVtbl;
};

static inline void *wv2_slot_fn( void *obj, BOOL real_loader, int slot_real, int slot_builtin )
{
    int slot = real_loader ? slot_real : slot_builtin;
    if (!obj || !*(void ***)obj || slot < 0) return NULL;
    return (*(void ***)obj)[slot];
}

static HRESULT wv2_call_navigate( void *obj, BOOL real_loader, int slot_real, int slot_builtin, LPCWSTR uri )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    if (real_loader)
        return ((HRESULT (WINAPI *)( void *, LPCWSTR ))fn)( obj, uri );
    return ((HRESULT (WINAPI *)( void *, LPCWSTR, void * ))fn)( obj, uri, NULL );
}

static HRESULT wv2_call_get_source( void *obj, BOOL real_loader, int slot_real, int slot_builtin, LPWSTR *raw )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, LPWSTR * ))fn)( obj, raw );
}

static HRESULT wv2_call_bounds( void *obj, BOOL real_loader, int slot_real, int slot_builtin, RECT rc )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, RECT ))fn)( obj, rc );
}

static HRESULT wv2_call_bool( void *obj, BOOL real_loader, int slot_real, int slot_builtin, BOOL val )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, BOOL ))fn)( obj, val );
}

static HRESULT wv2_call_hwnd( void *obj, BOOL real_loader, int slot_real, int slot_builtin, HWND hwnd )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void *, HWND ))fn)( obj, hwnd );
}

static HRESULT wv2_call_void( void *obj, BOOL real_loader, int slot_real, int slot_builtin )
{
    void *fn = wv2_slot_fn( obj, real_loader, slot_real, slot_builtin );
    if (!fn || (ULONG_PTR)fn >= (ULONG_PTR)0x7ffc00000000) return E_NOINTERFACE;
    return ((HRESULT (WINAPI *)( void * ))fn)( obj );
}

/* Handler faces the app hands to our shells: every WinRT event/completed delegate starts
 * { QueryInterface, AddRef, Release, Invoke }. The 2nd/3rd Invoke parameters differ per
 * operation, so name the layout for each shape we fire instead of indexing slot 3. */
struct cwv2_action_completed_handler;
struct cwv2_action_completed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct cwv2_action_completed_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct cwv2_action_completed_handler *This );
    ULONG (WINAPI *Release)( struct cwv2_action_completed_handler *This );
    HRESULT (WINAPI *Invoke)( struct cwv2_action_completed_handler *This, void *async_info, AsyncStatus status );
};
struct cwv2_action_completed_handler
{
    const struct cwv2_action_completed_handler_vtbl *lpVtbl;
};

struct cwv2_result_completed_handler;
struct cwv2_result_completed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct cwv2_result_completed_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct cwv2_result_completed_handler *This );
    ULONG (WINAPI *Release)( struct cwv2_result_completed_handler *This );
    HRESULT (WINAPI *Invoke)( struct cwv2_result_completed_handler *This, HRESULT status, void *result );
};
struct cwv2_result_completed_handler
{
    const struct cwv2_result_completed_handler_vtbl *lpVtbl;
};

struct cwv2_typed_handler;
struct cwv2_typed_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct cwv2_typed_handler *This, REFIID riid, void **ppv );
    ULONG (WINAPI *AddRef)( struct cwv2_typed_handler *This );
    ULONG (WINAPI *Release)( struct cwv2_typed_handler *This );
    HRESULT (WINAPI *Invoke)( struct cwv2_typed_handler *This, void *sender, void *args );
};
struct cwv2_typed_handler
{
    const struct cwv2_typed_handler_vtbl *lpVtbl;
};

static HRESULT wv2_call_get_core( void *controller, BOOL real_loader, int slot_real, int slot_builtin, void **core )
{
    HRESULT (WINAPI *get_core)( void *, void ** ) = wv2_slot_fn( controller, real_loader, slot_real, slot_builtin );
    if ((ULONG_PTR)get_core >= (ULONG_PTR)0x7ffc00000000)  /* stale shared-page slot, see s380 */
    {
        HMODULE loader = GetModuleHandleW( L"webview2loader.dll" );
        get_core = (void *)GetProcAddress( loader, "GetCoreWebView2FromController" );
        ERR( "wv2_call_get_core: stale slot%d, using export %p\n", real_loader ? slot_real : slot_builtin, get_core );
    }
    if (!get_core) return E_NOINTERFACE;
    return get_core( controller, core );
}

static BOOL wv2_probe_bounds( ICoreWebView2Controller *c )  /* call slot-4 getter/putter with known rect, read back */
{
    static const RECT probe_rc = { 0, 0, 321, 123 };
    RECT orig = {0}, readback = {0};
    HRESULT hr_put, hr_get;
    BOOL ok;

    if (!c || !c->lpVtbl || !c->lpVtbl->get_Bounds || !c->lpVtbl->put_Bounds)
        return FALSE;
    c->lpVtbl->get_Bounds( c, &orig );
    hr_put = c->lpVtbl->put_Bounds( c, probe_rc );
    hr_get = c->lpVtbl->get_Bounds( c, &readback );
    ok = SUCCEEDED( hr_put ) && SUCCEEDED( hr_get ) &&
         readback.right == probe_rc.right && readback.bottom == probe_rc.bottom;
    c->lpVtbl->put_Bounds( c, orig );
    ERR( "wv2_probe_bounds: ctrl=%p put=%#lx get=%#lx read=%ldx%ld ok=%d\n",
         c, hr_put, hr_get, readback.right, readback.bottom, ok );
    return ok;
}

static HRESULT WINAPI cwv2_Navigate( void *iface, HSTRING uri )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    ERR( "=== CoreWebView2::Navigate: %s ===\n", debugstr_hstring( uri ) );
    if (impl->source_uri) WindowsDeleteString( impl->source_uri );
    impl->source_uri = NULL;
    if (uri) WindowsDuplicateString( uri, &impl->source_uri );
    if (impl->loader_core)
    {
        const WCHAR *s = uri ? WindowsGetStringRawBuffer( uri, NULL ) : L"";
        HRESULT hr2 = wv2_call_navigate( impl->loader_core, impl->real_loader, 5, 6, s );
        if (impl->owner) impl->owner->navigated = TRUE;
        ERR( "cwv2_Navigate: loader forward hr %#lx\n", hr2 );
        return hr2;
    }
    return S_OK;
}
static HRESULT WINAPI cwv2_NavigateToString( void *iface, HSTRING html )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *raw = html ? WindowsGetStringRawBuffer( html, NULL ) : L"";
    ERR( "=== CoreWebView2::NavigateToString (%u chars) ===\n", (unsigned)lstrlenW( raw ) );
    if (impl->owner) impl->owner->navigated = TRUE;
    if (impl->loader_core)
    {
        LPCWSTR s = impl->real_loader ? raw : L"data:text/html,";
        HRESULT hr2 = wv2_call_navigate( impl->loader_core, impl->real_loader, 6, 6, s );
        ERR( "cwv2_NavigateToString: loader forward hr %#lx\n", hr2 );
        return hr2;
    }
    return S_OK;
}
static HRESULT WINAPI cwv2_get_Source( void *iface, HSTRING *uri )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (!uri) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        LPWSTR raw = NULL;
        if (SUCCEEDED( wv2_call_get_source( impl->loader_core, impl->real_loader, 4, 8, &raw ) ) && raw)
        {
            HRESULT hr = WindowsCreateString( raw, lstrlenW( raw ), uri );
            CoTaskMemFree( raw );
            return hr;
        }
    }
    if (impl->source_uri) return WindowsDuplicateString( impl->source_uri, uri );
    if (impl->owner && impl->owner->wv2_source_uri)
        return WindowsDuplicateString( impl->owner->wv2_source_uri, uri );
    return WindowsCreateString( L"about:blank", 11, uri );
}

static HRESULT WINAPI cwv2_2_get_Environment( void *iface, void **env )
{
    struct cwv2_env *e;
    if (!env) return E_POINTER;
    if (!(e = calloc( 1, sizeof(*e) ))) return E_OUTOFMEMORY;
    e->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_env_vtbl );
    e->ref = 1;
    *env = &e->IInspectable_iface;
    return S_OK;
}

/* WinRT IAsyncOperation<HSTRING> shell: supports both synchronous completion
 * (completed = TRUE at creation) and asynchronous completion driven by a COM
 * ICoreWebView2*CompletedHandler callback (completed = FALSE until Invoke). */
struct cwv2_asyncop
{
    IInspectable IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    BOOL completed;
    HRESULT hr;
    HSTRING result;
    void *handler;          /* AsyncOperationCompletedHandler<HSTRING> */
};

static inline struct cwv2_asyncop *impl_from_asyncop( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_asyncop, IInspectable_iface );
}
static inline struct cwv2_asyncop *impl_from_asyncop_info( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_asyncop, IAsyncInfo_iface );
}

static HRESULT WINAPI asyncop_QueryInterface( void *iface, REFIID iid, void **obj )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    *obj = NULL;
    if (!iid) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_HSTRING ))
    {
        *obj = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*obj );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *obj = &impl->IAsyncInfo_iface;
        IInspectable_AddRef( (IInspectable *)&impl->IInspectable_iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI asyncop_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_asyncop( iface )->ref );
}
static ULONG WINAPI asyncop_Release( void *iface )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->result) WindowsDeleteString( impl->result );
        if (impl->handler) ((IInspectable *)impl->handler)->lpVtbl->Release( impl->handler );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI asyncop_GetResults( void *iface, HSTRING *s )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    if (!s) return E_POINTER;
    if (FAILED( impl->hr )) return impl->hr;
    if (impl->result) return WindowsDuplicateString( impl->result, s );
    return WindowsCreateString( L"{}", 2, s );
}
static HRESULT WINAPI asyncop_put_Completed( void *iface, void *handler )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    struct cwv2_action_completed_handler *h = (struct cwv2_action_completed_handler *)handler;
    if (impl->handler) ((IInspectable *)impl->handler)->lpVtbl->Release( impl->handler );
    impl->handler = handler;
    if (handler)
    {
        ((IInspectable *)handler)->lpVtbl->AddRef( handler );
        if (impl->completed)
            h->lpVtbl->Invoke( h, iface, FAILED( impl->hr ) ? Error : Completed );
    }
    return S_OK;
}
static HRESULT WINAPI asyncop_get_Completed( void *iface, void **handler )
{
    struct cwv2_asyncop *impl = impl_from_asyncop( iface );
    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) ((IInspectable *)*handler)->lpVtbl->AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI asyncop_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **obj )
{
    return asyncop_QueryInterface( &impl_from_asyncop_info( iface )->IInspectable_iface, iid, obj );
}
static ULONG WINAPI asyncop_info_AddRef( IAsyncInfo *iface )
{
    return asyncop_AddRef( &impl_from_asyncop_info( iface )->IInspectable_iface );
}
static ULONG WINAPI asyncop_info_Release( IAsyncInfo *iface )
{
    return asyncop_Release( &impl_from_asyncop_info( iface )->IInspectable_iface );
}
static HRESULT WINAPI asyncop_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return cwv2_GetIids( &impl_from_asyncop_info( iface )->IInspectable_iface, count, iids );
}
static HRESULT WINAPI asyncop_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<String>", 44, name );
}
static HRESULT WINAPI asyncop_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (id) *id = 1;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct cwv2_asyncop *impl = impl_from_asyncop_info( iface );
    if (status)
        *status = impl->completed ? (FAILED( impl->hr ) ? Error : Completed) : Started;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *err )
{
    struct cwv2_asyncop *impl = impl_from_asyncop_info( iface );
    if (err) *err = impl->hr;
    return S_OK;
}
static HRESULT WINAPI asyncop_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI asyncop_info_Close( IAsyncInfo *iface ) { return S_OK; }

static const IAsyncInfoVtbl cwv2_asyncop_info_vtbl =
{
    asyncop_info_QueryInterface,
    asyncop_info_AddRef,
    asyncop_info_Release,
    asyncop_info_GetIids,
    asyncop_info_GetRuntimeClassName,
    asyncop_info_GetTrustLevel,
    asyncop_info_get_Id,
    asyncop_info_get_Status,
    asyncop_info_get_ErrorCode,
    asyncop_info_Cancel,
    asyncop_info_Close,
};

static const void *cwv2_asyncop_vtbl[256] =
{
    [0] = asyncop_QueryInterface,
    [1] = asyncop_AddRef,
    [2] = asyncop_Release,
    [3] = cwv2_GetIids,             /* IInspectable: GetIids/GetRuntimeClassName/GetTrustLevel */
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6] = asyncop_put_Completed,
    [7] = asyncop_get_Completed,
    [8] = asyncop_GetResults,
};

/* Create an async-op carrying `result` (may be NULL -> "{}"). */
static HRESULT cwv2_asyncop_create_ex( HSTRING result, BOOL completed, struct cwv2_asyncop **out_impl )
{
    struct cwv2_asyncop *op = calloc( 1, sizeof(*op) );
    if (!op) return E_OUTOFMEMORY;
    op->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_asyncop_vtbl );
    op->IAsyncInfo_iface.lpVtbl = &cwv2_asyncop_info_vtbl;
    op->ref = 1;
    op->completed = completed;
    op->hr = S_OK;
    if (result) WindowsDuplicateString( result, &op->result );
    *out_impl = op;
    return S_OK;
}

static HRESULT cwv2_asyncop_create( HSTRING result, void **out )
{
    struct cwv2_asyncop *op = NULL;
    HRESULT hr = cwv2_asyncop_create_ex( result, TRUE, &op );
    if (SUCCEEDED( hr )) *out = &op->IInspectable_iface;
    return hr;
}

/* COM ICoreWebView2*CompletedHandler(HRESULT, LPCWSTR) -> cwv2_asyncop bridge */
struct cwv2_com_str_handler
{
    const void **vtbl;
    LONG ref;
    struct cwv2_asyncop *op;
};

static HRESULT WINAPI com_str_h_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = t;
        InterlockedIncrement( (LONG *)((char *)t + sizeof(void *)) );
        return S_OK;
    }
    /* Accept any specific WebView2 completed handler IID */
    *out = t;
    InterlockedIncrement( (LONG *)((char *)t + sizeof(void *)) );
    return S_OK;
}
static ULONG WINAPI com_str_h_AddRef( void *t )
{
    return InterlockedIncrement( &((struct cwv2_com_str_handler *)t)->ref );
}
static ULONG WINAPI com_str_h_Release( void *t )
{
    struct cwv2_com_str_handler *h = t;
    ULONG r = InterlockedDecrement( &h->ref );
    if (!r)
    {
        if (h->op) asyncop_Release( &h->op->IInspectable_iface );
        HeapFree( GetProcessHeap(), 0, (void *)h->vtbl );
        HeapFree( GetProcessHeap(), 0, h );
    }
    return r;
}
static HRESULT WINAPI com_str_h_Invoke( void *t, HRESULT err, LPCWSTR res )
{
    struct cwv2_com_str_handler *h = t;
    struct cwv2_asyncop *op = h->op;
    if (op)
    {
        op->hr = err;
        if (op->result) { WindowsDeleteString( op->result ); op->result = NULL; }
        if (res) WindowsCreateString( res, lstrlenW( res ), &op->result );
        op->completed = TRUE;
        if (op->handler)
        {
            struct cwv2_action_completed_handler *wh = (struct cwv2_action_completed_handler *)op->handler;
            wh->lpVtbl->Invoke( wh, &op->IInspectable_iface, FAILED( err ) ? Error : Completed );
        }
    }
    return S_OK;
}
static struct cwv2_com_str_handler *cwv2_com_str_handler_create( struct cwv2_asyncop *op )
{
    struct cwv2_com_str_handler *h = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h) );
    const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
    if (!h || !vt)
    {
        HeapFree( GetProcessHeap(), 0, h );
        HeapFree( GetProcessHeap(), 0, vt );
        return NULL;
    }
    vt[0] = com_str_h_QueryInterface;
    vt[1] = com_str_h_AddRef;
    vt[2] = com_str_h_Release;
    vt[3] = com_str_h_Invoke;
    h->vtbl = vt;
    h->ref = 1;
    h->op = op;
    if (op) asyncop_AddRef( &op->IInspectable_iface );
    return h;
}

/* s480: CoreWebView2Profile shell. The app awaits
 * CoreWebView2.Profile.AddBrowserExtensionAsync(path); slot 8 is get_Profile
 * and the profile's AddBrowserExtension slot returns a completed
 * IAsyncOperation<CoreWebView2BrowserExtension> (asyncop shell). */
struct cwv2_profile
{
    IInspectable IInspectable_iface;
    LONG ref;
};

static inline struct cwv2_profile *impl_from_profile( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_profile, IInspectable_iface );
}

static HRESULT WINAPI profile_QueryInterface( void *iface, REFIID iid, void **obj )
{
    struct cwv2_profile *impl = impl_from_profile( iface );
    *obj = NULL;
    if (!iid) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ))
        return E_NOINTERFACE;
    /* Answer WebView2 Profile IIDs + IUnknown/IInspectable/IAgileObject with self. */
    *obj = &impl->IInspectable_iface;
    IInspectable_AddRef( *obj );
    return S_OK;
}
static HRESULT WINAPI profile_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI profile_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Profile", 47, name );
}
static ULONG WINAPI profile_AddRef( void *iface )
{
    return InterlockedIncrement( &impl_from_profile( iface )->ref );
}
static ULONG WINAPI profile_Release( void *iface )
{
    struct cwv2_profile *impl = impl_from_profile( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
/* AddBrowserExtension(folderPath, ICoreWebView2ProfileAddBrowserExtensionCompletedHandler*)
 * .NET SDK uses the completed-handler pattern: fire the handler synchronously
 * with a shell extension object (the asyncop shell answers get_Id/get_Name). */
static HRESULT WINAPI profile_AddBrowserExtension( void *iface, HSTRING path, void *handler )
{
    struct cwv2_result_completed_handler *h = (struct cwv2_result_completed_handler *)handler;
    void *ext = NULL;
    (void)iface;
    ERR( "profile_AddBrowserExtension %s handler %p\n", debugstr_hstring( path ), handler );
    cwv2_asyncop_create( NULL, &ext );
    if (handler) h->lpVtbl->Invoke( h, S_OK, ext );
    return S_OK;
}
static HRESULT WINAPI profile_ClearBrowsingDataAllAsync( void *iface, void **op )
{
    ERR( "profile_ClearBrowsingDataAllAsync iface %p, op %p\n", iface, op );
    return xaml_async_action_create( (IAsyncAction **)op );
}

/* the returned extension object answers .Id with the Zoom extension id */
static HRESULT WINAPI profile_generic_slot( void *iface, ... )
{
    const void **vt = *(const void ***)iface;
    int slot = 0;
    while (slot < 256 && vt[slot] != (const void *)profile_generic_slot) slot++;
    ERR( "profile_slot %d iface %p\n", slot, iface );
    return S_OK;
}

static const void *cwv2_profile_vtbl[256] =
{
    [0] = profile_QueryInterface,
    [1] = profile_AddRef,
    [2] = profile_Release,
    [3] = profile_GetIids,
    [4] = profile_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = profile_generic_slot,
    [7] = profile_ClearBrowsingDataAllAsync,
    [8] = profile_AddBrowserExtension,   /* WinRT class: 6 BrowserExtensions, 7 DefaultBackgroundColor/ClearBrowsingDataAsync, 8 AddBrowserExtension */
};

static void *cwv2_get_com_iface( struct core_webview2 *impl, void **cached, const GUID *iid )
{
    if (*cached) return *cached;
    if (impl->loader_core && impl->real_loader)
        ((IUnknown *)impl->loader_core)->lpVtbl->QueryInterface( impl->loader_core, iid, cached );
    return *cached;
}

static HRESULT WINAPI cwv2_get_BrowserProcessId( void *iface, UINT32 *pid )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (!pid) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, UINT32 * ) = wv2_slot_fn( impl->loader_core, TRUE, 37, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, pid );
    }
    *pid = GetCurrentProcessId();
    return S_OK;
}

static HRESULT WINAPI cwv2_get_CanGoBack( void *iface, boolean *val )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    BOOL b = FALSE;
    if (!val) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = wv2_slot_fn( impl->loader_core, TRUE, 38, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI cwv2_get_CanGoForward( void *iface, boolean *val )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    BOOL b = FALSE;
    if (!val) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = wv2_slot_fn( impl->loader_core, TRUE, 39, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI cwv2_get_DocumentTitle( void *iface, HSTRING *title )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (!title) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        LPWSTR raw = NULL;
        HRESULT (WINAPI *fn)( void *, LPWSTR * ) = wv2_slot_fn( impl->loader_core, TRUE, 48, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &raw );
            if (SUCCEEDED( hr ) && raw)
            {
                hr = WindowsCreateString( raw, lstrlenW( raw ), title );
                CoTaskMemFree( raw );
                return hr;
            }
            if (raw) CoTaskMemFree( raw );
            return hr;
        }
    }
    return WindowsCreateString( L"", 0, title );
}

static HRESULT WINAPI cwv2_get_ContainsFullScreenElement( void *iface, boolean *val )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    BOOL b = FALSE;
    if (!val) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = wv2_slot_fn( impl->loader_core, TRUE, 54, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->loader_core, &b );
            *val = !!b;
            return hr;
        }
    }
    *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI cwv2_AddScriptToExecuteOnDocumentCreatedAsync( void *iface, HSTRING js, void **op )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    struct cwv2_asyncop *aop = NULL;
    const WCHAR *s = js ? WindowsGetStringRawBuffer( js, NULL ) : L"";
    HRESULT hr;
    if (!op) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, void * ) = wv2_slot_fn( impl->loader_core, TRUE, 27, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            struct cwv2_com_str_handler *ch;
            if (FAILED( hr = cwv2_asyncop_create_ex( NULL, FALSE, &aop ) )) return hr;
            ch = cwv2_com_str_handler_create( aop );
            if (!ch) { asyncop_Release( &aop->IInspectable_iface ); return E_OUTOFMEMORY; }
            hr = fn( impl->loader_core, s, ch );
            com_str_h_Release( ch );
            if (FAILED( hr )) { aop->hr = hr; aop->completed = TRUE; }
            *op = &aop->IInspectable_iface;
            return S_OK;
        }
    }
    cwv2_queue_op( impl, CWV2_OP_ADD_SCRIPT, 0, FALSE, s, NULL, NULL, NULL, NULL );
    return cwv2_asyncop_create( NULL, op );
}

static HRESULT WINAPI cwv2_RemoveScriptToExecuteOnDocumentCreated( void *iface, HSTRING id )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = id ? WindowsGetStringRawBuffer( id, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 28, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_ExecuteScriptAsync( void *iface, HSTRING js, void **op )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    struct cwv2_asyncop *aop = NULL;
    const WCHAR *s = js ? WindowsGetStringRawBuffer( js, NULL ) : L"";
    HRESULT hr;
    if (!op) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, void * ) = wv2_slot_fn( impl->loader_core, TRUE, 29, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            struct cwv2_com_str_handler *ch;
            if (FAILED( hr = cwv2_asyncop_create_ex( NULL, FALSE, &aop ) )) return hr;
            ch = cwv2_com_str_handler_create( aop );
            if (!ch) { asyncop_Release( &aop->IInspectable_iface ); return E_OUTOFMEMORY; }
            hr = fn( impl->loader_core, s, ch );
            com_str_h_Release( ch );
            if (FAILED( hr )) { aop->hr = hr; aop->completed = TRUE; }
            *op = &aop->IInspectable_iface;
            return S_OK;
        }
    }
    return cwv2_asyncop_create( NULL, op );
}

static HRESULT WINAPI cwv2_Reload( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 31, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_PostWebMessageAsJson( void *iface, HSTRING json )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = json ? WindowsGetStringRawBuffer( json, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 32, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_PostWebMessageAsString( void *iface, HSTRING msg )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = msg ? WindowsGetStringRawBuffer( msg, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 33, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_CallDevToolsProtocolMethodAsync( void *iface, HSTRING method, HSTRING params, void **op )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    struct cwv2_asyncop *aop = NULL;
    const WCHAR *m = method ? WindowsGetStringRawBuffer( method, NULL ) : L"";
    const WCHAR *p = params ? WindowsGetStringRawBuffer( params, NULL ) : L"{}";
    HRESULT hr;
    ERR( "cwv2_CallDevToolsProtocolMethodAsync %s %s\n", debugstr_w( m ), debugstr_w( p ) );
    if (!op) return E_POINTER;
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, LPCWSTR, void * ) = wv2_slot_fn( impl->loader_core, TRUE, 36, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            struct cwv2_com_str_handler *ch;
            if (FAILED( hr = cwv2_asyncop_create_ex( NULL, FALSE, &aop ) )) return hr;
            ch = cwv2_com_str_handler_create( aop );
            if (!ch) { asyncop_Release( &aop->IInspectable_iface ); return E_OUTOFMEMORY; }
            hr = fn( impl->loader_core, m, p, ch );
            com_str_h_Release( ch );
            if (FAILED( hr )) { aop->hr = hr; aop->completed = TRUE; }
            *op = &aop->IInspectable_iface;
            return S_OK;
        }
    }
    cwv2_queue_op( impl, CWV2_OP_DEVTOOLS, 0, FALSE, m, p, NULL, NULL, NULL );
    return cwv2_asyncop_create( NULL, op );
}

static HRESULT WINAPI cwv2_GoBack( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 40, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_GoForward( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 41, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_Stop( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 43, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_AddHostObjectToScript( void *iface, HSTRING name, IInspectable *raw_object )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    ERR( "=== CoreWebView2::AddHostObjectToScript: %s obj=%p ===\n", debugstr_w( s ), raw_object );
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, VARIANT * ) = wv2_slot_fn( impl->loader_core, TRUE, 49, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            VARIANT v;
            IDispatch *disp = NULL;
            HRESULT hr;
            memset( &v, 0, sizeof(v) );
            if (raw_object && SUCCEEDED( IInspectable_QueryInterface( raw_object, &IID_IDispatch, (void **)&disp ) ) && disp)
            {
                V_VT( &v ) = VT_DISPATCH;
                V_DISPATCH( &v ) = disp;
            }
            else
            {
                V_VT( &v ) = VT_UNKNOWN;
                V_UNKNOWN( &v ) = (IUnknown *)raw_object;
                if (raw_object) IInspectable_AddRef( raw_object );
            }
            hr = fn( impl->loader_core, s, &v );
            if (V_UNKNOWN( &v )) ((IUnknown *)V_UNKNOWN( &v ))->lpVtbl->Release( V_UNKNOWN( &v ) );
            ERR( "cwv2_AddHostObjectToScript(%s) -> %#lx\n", debugstr_w( s ), hr );
            return hr;
        }
    }
    cwv2_queue_op( impl, CWV2_OP_ADD_HOST_OBJECT, 0, FALSE, s, NULL, raw_object, NULL, NULL );
    return S_OK;
}

static HRESULT WINAPI cwv2_RemoveHostObjectFromScript( void *iface, HSTRING name )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = name ? WindowsGetStringRawBuffer( name, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR ) = wv2_slot_fn( impl->loader_core, TRUE, 50, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s );
    }
    return S_OK;
}

static HRESULT WINAPI cwv2_OpenDevToolsWindow( void *iface )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (impl->loader_core && impl->real_loader)
        return wv2_call_void( impl->loader_core, TRUE, 51, -1 );
    return S_OK;
}

static HRESULT WINAPI cwv2_AddWebResourceRequestedFilter( void *iface, HSTRING uri, int resource_ctx )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = uri ? WindowsGetStringRawBuffer( uri, NULL ) : L"";
    ERR( "cwv2_AddWebResourceRequestedFilter: %s ctx=%d\n", debugstr_w( s ), resource_ctx );
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, int ) = wv2_slot_fn( impl->loader_core, TRUE, 57, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s, resource_ctx );
    }
    cwv2_queue_op( impl, CWV2_OP_ADD_WRS_FILTER, resource_ctx, FALSE, s, NULL, NULL, NULL, NULL );
    return S_OK;
}

static HRESULT WINAPI cwv2_RemoveWebResourceRequestedFilter( void *iface, HSTRING uri, int resource_ctx )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    const WCHAR *s = uri ? WindowsGetStringRawBuffer( uri, NULL ) : L"";
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, LPCWSTR, int ) = wv2_slot_fn( impl->loader_core, TRUE, 58, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->loader_core, s, resource_ctx );
    }
    return S_OK;
}

/* WinRT EventArgs wrapper around COM ICoreWebView2*EventArgs */
static const GUID IID_ICoreWebView2NavigationStartingEventArgs_rt =
    { 0x548d23d3, 0xfea3, 0x5616, { 0xbd, 0x05, 0xae, 0x08, 0x06, 0x6c, 0x86, 0xd3 } };
static const GUID IID_ICoreWebView2NavigationCompletedEventArgs_rt =
    { 0x4865e238, 0x036a, 0x5664, { 0x95, 0xa3, 0x44, 0x7e, 0xc4, 0x4c, 0xf4, 0x98 } };
static const GUID IID_ICoreWebView2WebMessageReceivedEventArgs_rt =
    { 0xeb066159, 0xb725, 0x5d5b, { 0xad, 0xc8, 0xf5, 0xd7, 0xb9, 0x29, 0x03, 0x04 } };
static const GUID IID_ICoreWebView2WebResourceRequestedEventArgs_rt =
    { 0x577f1fc4, 0xc943, 0x54a9, { 0x97, 0x00, 0xbd, 0x46, 0x9b, 0x48, 0xbd, 0x41 } };
static const GUID IID_ICoreWebView2DOMContentLoadedEventArgs_rt =
    { 0xc474d0a3, 0x24ac, 0x59fc, { 0xb7, 0x8b, 0xda, 0x75, 0x62, 0xa6, 0xa0, 0x52 } };
static const GUID IID_ICoreWebView2ContextMenuRequestedEventArgs_rt =
    { 0xd77bdd8c, 0x9b3e, 0x596e, { 0xae, 0x80, 0x32, 0x0c, 0x0d, 0xf4, 0xec, 0xbc } };

struct cwv2_event_args
{
    IInspectable IInspectable_iface;
    LONG ref;
    GUID winrt_iid;
    void *com_args;
};

static inline struct cwv2_event_args *impl_from_evargs( void *iface )
{
    return CONTAINING_RECORD( iface, struct cwv2_event_args, IInspectable_iface );
}
static HRESULT WINAPI evargs_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->winrt_iid ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI evargs_AddRef( IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_evargs( iface )->ref );
}
static ULONG WINAPI evargs_Release( IInspectable *iface )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->com_args) ((IUnknown *)impl->com_args)->lpVtbl->Release( impl->com_args );
        free( impl );
    }
    return r;
}

static HRESULT evargs_get_str( struct cwv2_event_args *impl, int com_slot, HSTRING *out )
{
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, LPWSTR * ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            LPWSTR raw = NULL;
            HRESULT hr = fn( impl->com_args, &raw );
            if (SUCCEEDED( hr ) && raw)
            {
                hr = WindowsCreateString( raw, lstrlenW( raw ), out );
                CoTaskMemFree( raw );
                return hr;
            }
            if (raw) CoTaskMemFree( raw );
            return hr;
        }
    }
    return WindowsCreateString( L"", 0, out );
}
static HRESULT evargs_get_bool( struct cwv2_event_args *impl, int com_slot, boolean *out )
{
    BOOL b = FALSE;
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, BOOL * ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            HRESULT hr = fn( impl->com_args, &b );
            *out = !!b;
            return hr;
        }
    }
    *out = FALSE;
    return S_OK;
}
static HRESULT evargs_put_bool( struct cwv2_event_args *impl, int com_slot, boolean val )
{
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, BOOL ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args, !!val );
    }
    return S_OK;
}
static HRESULT evargs_get_u64( struct cwv2_event_args *impl, int com_slot, UINT64 *out )
{
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, UINT64 * ) = (*(void ***)impl->com_args)[com_slot];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( impl->com_args, out );
    }
    *out = 1;
    return S_OK;
}

/* NavigationStartingEventArgs: 6=get_Uri, 7=get_IsUserInitiated, 8=get_IsRedirected,
 * 9=get_RequestHeaders, 10=get_Cancel, 11=put_Cancel, 12=get_NavigationId (COM 3..9) */
static HRESULT WINAPI navstart_get_Uri( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 3, out ); }
static HRESULT WINAPI navstart_get_IsUserInitiated( void *iface, boolean *out )
{ return evargs_get_bool( impl_from_evargs( iface ), 4, out ); }
static HRESULT WINAPI navstart_get_IsRedirected( void *iface, boolean *out )
{ return evargs_get_bool( impl_from_evargs( iface ), 5, out ); }
static HRESULT WINAPI navstart_get_Cancel( void *iface, boolean *out )
{ return evargs_get_bool( impl_from_evargs( iface ), 7, out ); }
static HRESULT WINAPI navstart_put_Cancel( void *iface, boolean val )
{ return evargs_put_bool( impl_from_evargs( iface ), 8, val ); }
static HRESULT WINAPI navstart_get_NavigationId( void *iface, UINT64 *out )
{ return evargs_get_u64( impl_from_evargs( iface ), 9, out ); }

static const void *cwv2_navstart_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = navstart_get_Uri, [7] = navstart_get_IsUserInitiated, [8] = navstart_get_IsRedirected,
    [10] = navstart_get_Cancel, [11] = navstart_put_Cancel, [12] = navstart_get_NavigationId,
};

/* NavigationCompletedEventArgs: 6=get_IsSuccess, 7=get_WebErrorStatus, 8=get_NavigationId (COM 3..5) */
static HRESULT WINAPI navcomp_get_IsSuccess( void *iface, boolean *out )
{ return evargs_get_bool( impl_from_evargs( iface ), 3, out ); }
static HRESULT WINAPI navcomp_get_WebErrorStatus( void *iface, int *out )
{
    struct cwv2_event_args *impl = impl_from_evargs( iface );
    if (!out) return E_POINTER;
    if (impl->com_args && *(void ***)impl->com_args)
    {
        HRESULT (WINAPI *fn)( void *, int * ) = (*(void ***)impl->com_args)[4];
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000) return fn( impl->com_args, out );
    }
    *out = 0;
    return S_OK;
}
static HRESULT WINAPI navcomp_get_NavigationId( void *iface, UINT64 *out )
{ return evargs_get_u64( impl_from_evargs( iface ), 5, out ); }

static const void *cwv2_navcomp_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = navcomp_get_IsSuccess, [7] = navcomp_get_WebErrorStatus, [8] = navcomp_get_NavigationId,
};

/* WebMessageReceivedEventArgs: 6=get_Source, 7=get_WebMessageAsJson, 8=TryGetWebMessageAsString (COM 3..5) */
static HRESULT WINAPI webmsg_get_Source( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 3, out ); }
static HRESULT WINAPI webmsg_get_WebMessageAsJson( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 4, out ); }
static HRESULT WINAPI webmsg_TryGetWebMessageAsString( void *iface, HSTRING *out )
{ return evargs_get_str( impl_from_evargs( iface ), 5, out ); }

static const void *cwv2_webmsg_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = webmsg_get_Source, [7] = webmsg_get_WebMessageAsJson, [8] = webmsg_TryGetWebMessageAsString,
};

/* DOMContentLoadedEventArgs: 6=get_NavigationId (COM 3) */
static HRESULT WINAPI domloaded_get_NavigationId( void *iface, UINT64 *out )
{ return evargs_get_u64( impl_from_evargs( iface ), 3, out ); }

static const void *cwv2_domloaded_args_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = domloaded_get_NavigationId,
};

/* Generic EventArgs fallback (WebResourceRequested / ContextMenuRequested) */
static const void *cwv2_generic_evargs_vtbl[256] =
{
    [0] = evargs_QueryInterface, [1] = evargs_AddRef, [2] = evargs_Release,
    [3] = cwv2_GetIids, [4] = cwv2_GetRuntimeClassName, [5] = cwv2_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
};

/* COM event handler -> WinRT TypedEventHandler<CoreWebView2, TArgs> bridge */
struct cwv2_event_bridge
{
    const void **vtbl;
    LONG ref;
    struct core_webview2 *owner;
    IInspectable *winrt_handler;
    const void **args_vtbl;
    GUID args_iid;
};

static HRESULT WINAPI evbridge_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = t;
    InterlockedIncrement( &((struct cwv2_event_bridge *)t)->ref );
    return S_OK;
}
static ULONG WINAPI evbridge_AddRef( void *t )
{
    return InterlockedIncrement( &((struct cwv2_event_bridge *)t)->ref );
}
static ULONG WINAPI evbridge_Release( void *t )
{
    struct cwv2_event_bridge *b = t;
    ULONG r = InterlockedDecrement( &b->ref );
    if (!r)
    {
        if (b->winrt_handler) IInspectable_Release( b->winrt_handler );
        if (b->owner) cwv2_Release( &b->owner->IInspectable_iface );
        HeapFree( GetProcessHeap(), 0, (void *)b->vtbl );
        HeapFree( GetProcessHeap(), 0, b );
    }
    return r;
}
static HRESULT WINAPI evbridge_Invoke( void *t, void *com_sender, void *com_args )
{
    struct cwv2_event_bridge *b = t;
    struct cwv2_typed_handler *wh = (struct cwv2_typed_handler *)b->winrt_handler;
    struct cwv2_event_args *ea = NULL;
    HRESULT hr = S_OK;
    if (!wh || !b->owner) return S_OK;
    if (com_args && (ea = calloc( 1, sizeof(*ea) )))
    {
        ea->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( b->args_vtbl ? b->args_vtbl : cwv2_generic_evargs_vtbl );
        ea->ref = 1;
        ea->winrt_iid = b->args_iid;
        ea->com_args = com_args;
        ((IUnknown *)com_args)->lpVtbl->AddRef( com_args );
    }
    hr = wh->lpVtbl->Invoke( wh, &b->owner->IInspectable_iface, ea ? &ea->IInspectable_iface : NULL );
    if (ea) evargs_Release( &ea->IInspectable_iface );
    return hr;
}

static HRESULT cwv2_forward_add_event( struct core_webview2 *impl, void *com_obj, int com_slot,
        void *winrt_handler, EventRegistrationToken *token, const void **args_vtbl, const GUID *args_iid )
{
    static LONG fallback_tok = 100;
    if (token) token->value = InterlockedIncrement( &fallback_tok );
    if (com_obj && impl->real_loader && winrt_handler)
    {
        HRESULT (WINAPI *fn)( void *, void *, EventRegistrationToken * ) = wv2_slot_fn( com_obj, TRUE, com_slot, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
        {
            struct cwv2_event_bridge *b = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b) );
            const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
            HRESULT hr;
            if (!b || !vt) { HeapFree( GetProcessHeap(), 0, b ); HeapFree( GetProcessHeap(), 0, vt ); return E_OUTOFMEMORY; }
            vt[0] = evbridge_QueryInterface;
            vt[1] = evbridge_AddRef;
            vt[2] = evbridge_Release;
            vt[3] = evbridge_Invoke;
            b->vtbl = vt;
            b->ref = 1;
            b->owner = impl;
            cwv2_AddRef( &impl->IInspectable_iface );
            b->winrt_handler = (IInspectable *)winrt_handler;
            IInspectable_AddRef( b->winrt_handler );
            b->args_vtbl = args_vtbl;
            if (args_iid) b->args_iid = *args_iid;
            hr = fn( com_obj, b, token );
            evbridge_Release( b );
            return hr;
        }
    }
    if (!com_obj && winrt_handler)
    {
        enum cwv2_pending_kind ekind = (com_slot == 61 || com_slot == 64) ? CWV2_OP_ADD_EVENT2 :
                                       (com_slot == 100) ? CWV2_OP_ADD_EVENT11 : CWV2_OP_ADD_EVENT;
        cwv2_queue_op( impl, ekind, com_slot, FALSE, NULL, NULL, winrt_handler, args_vtbl, args_iid );
    }
    return S_OK;
}

static HRESULT cwv2_forward_remove_event( struct core_webview2 *impl, void *com_obj, int com_slot,
        EventRegistrationToken token )
{
    if (com_obj && impl->real_loader)
    {
        HRESULT (WINAPI *fn)( void *, EventRegistrationToken ) = wv2_slot_fn( com_obj, TRUE, com_slot, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( com_obj, token );
    }
    return S_OK;
}

#define CWV2_EVENT_PAIR(name, com_add, com_rem, avtbl, aiid) \
    static HRESULT WINAPI cwv2_add_##name( void *iface, void *handler, EventRegistrationToken *token ) \
    { struct core_webview2 *impl = impl_from_cwv2( iface ); \
      return cwv2_forward_add_event( impl, impl->loader_core, com_add, handler, token, avtbl, aiid ); } \
    static HRESULT WINAPI cwv2_remove_##name( void *iface, EventRegistrationToken token ) \
    { struct core_webview2 *impl = impl_from_cwv2( iface ); \
      return cwv2_forward_remove_event( impl, impl->loader_core, com_rem, token ); }

CWV2_EVENT_PAIR( NavigationStarting, 7, 8, cwv2_navstart_args_vtbl, &IID_ICoreWebView2NavigationStartingEventArgs_rt )
CWV2_EVENT_PAIR( ContentLoading, 9, 10, NULL, NULL )
CWV2_EVENT_PAIR( SourceChanged, 11, 12, NULL, NULL )
CWV2_EVENT_PAIR( HistoryChanged, 13, 14, NULL, NULL )
CWV2_EVENT_PAIR( NavigationCompleted, 15, 16, cwv2_navcomp_args_vtbl, &IID_ICoreWebView2NavigationCompletedEventArgs_rt )
CWV2_EVENT_PAIR( FrameNavigationStarting, 17, 18, cwv2_navstart_args_vtbl, &IID_ICoreWebView2NavigationStartingEventArgs_rt )
CWV2_EVENT_PAIR( FrameNavigationCompleted, 19, 20, cwv2_navcomp_args_vtbl, &IID_ICoreWebView2NavigationCompletedEventArgs_rt )
CWV2_EVENT_PAIR( ScriptDialogOpening, 21, 22, NULL, NULL )
CWV2_EVENT_PAIR( PermissionRequested, 23, 24, NULL, NULL )
CWV2_EVENT_PAIR( ProcessFailed, 25, 26, NULL, NULL )
CWV2_EVENT_PAIR( WebMessageReceived, 34, 35, cwv2_webmsg_args_vtbl, &IID_ICoreWebView2WebMessageReceivedEventArgs_rt )
CWV2_EVENT_PAIR( NewWindowRequested, 44, 45, NULL, NULL )
CWV2_EVENT_PAIR( DocumentTitleChanged, 46, 47, NULL, NULL )
CWV2_EVENT_PAIR( ContainsFullScreenElementChanged, 52, 53, NULL, NULL )
CWV2_EVENT_PAIR( WebResourceRequested, 55, 56, cwv2_generic_evargs_vtbl, &IID_ICoreWebView2WebResourceRequestedEventArgs_rt )
CWV2_EVENT_PAIR( WindowCloseRequested, 59, 60, NULL, NULL )

static HRESULT WINAPI cwv2_2_add_WebResourceResponseReceived( void *iface, void *handler, EventRegistrationToken *token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_add_event( impl, c2, 61, handler, token, NULL, NULL );
}
static HRESULT WINAPI cwv2_2_remove_WebResourceResponseReceived( void *iface, EventRegistrationToken token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_remove_event( impl, c2, 62, token );
}
static HRESULT WINAPI cwv2_2_NavigateWithWebResourceRequest( void *iface, void *request )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    ERR( "cwv2_2_NavigateWithWebResourceRequest request=%p c2=%p\n", request, c2 );
    if (impl->owner) impl->owner->navigated = TRUE;
    if (c2 && request)
    {
        HRESULT (WINAPI *fn)( void *, void * ) = wv2_slot_fn( c2, TRUE, 63, -1 );
        if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
            return fn( c2, request );
    }
    return S_OK;
}
static HRESULT WINAPI cwv2_2_add_DOMContentLoaded( void *iface, void *handler, EventRegistrationToken *token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_add_event( impl, c2, 64, handler, token, cwv2_domloaded_args_vtbl, &IID_ICoreWebView2DOMContentLoadedEventArgs_rt );
}
static HRESULT WINAPI cwv2_2_remove_DOMContentLoaded( void *iface, EventRegistrationToken token )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    void *c2 = cwv2_get_com_iface( impl, &impl->loader_core2, &IID_ICoreWebView2_2_com );
    return cwv2_forward_remove_event( impl, c2, 65, token );
}

static HRESULT WINAPI cwv2_11_add_ContextMenuRequested( void *iface, void *handler, EventRegistrationToken *token )
{
    struct core_webview2 *impl = impl_from_cwv2_11( iface );
    void *c11 = cwv2_get_com_iface( impl, &impl->loader_core11, &IID_ICoreWebView2_11_com );
    return cwv2_forward_add_event( impl, c11, 100, handler, token, cwv2_generic_evargs_vtbl, &IID_ICoreWebView2ContextMenuRequestedEventArgs_rt );
}
static HRESULT WINAPI cwv2_11_remove_ContextMenuRequested( void *iface, EventRegistrationToken token )
{
    struct core_webview2 *impl = impl_from_cwv2_11( iface );
    void *c11 = cwv2_get_com_iface( impl, &impl->loader_core11, &IID_ICoreWebView2_11_com );
    return cwv2_forward_remove_event( impl, c11, 101, token );
}
static HRESULT WINAPI cwv2_11_CallDevToolsProtocolMethodForSessionAsync( void *iface, HSTRING sid, HSTRING method, HSTRING params, void **op )
{
    if (!op) return E_POINTER;
    return cwv2_asyncop_create( NULL, op );
}

static HRESULT WINAPI cwv2_get_Profile( void *iface, void **out )
{
    struct cwv2_profile *p;
    (void)iface;
    ERR( "cwv2_get_Profile\n" );
    if (!out) return E_POINTER;
    if (!(p = calloc( 1, sizeof(*p) ))) return E_OUTOFMEMORY;
    p->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_profile_vtbl );
    p->ref = 1;
    *out = &p->IInspectable_iface;
    return S_OK;
}

/* Breadcrumb generic slot for unimplemented CoreWebView2 slots */
static HRESULT WINAPI cwv2_generic_slot( void *iface, ... )
{
    const void **vt = *(const void ***)iface;
    int slot = 0;
    va_list arg;
    while (slot < 256 && vt[slot] != (const void *)cwv2_generic_slot) slot++;
    va_start( arg, iface );
    {
        void *a1 = va_arg( arg, void * );
        void *a2 = va_arg( arg, void * );
        ERR( "cwv2_slot %d iface %p a1 %p a2 %p\n", slot, iface, a1, a2 );
    }
    va_end( arg );
    return S_OK;
}

static const void *cwv2_vtbl[256] =
{
    [0] = cwv2_QueryInterface,
    [1] = cwv2_AddRef,
    [2] = cwv2_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
    [6] = cwv2_get_Settings,
    [7] = cwv2_get_Source,
    [8] = cwv2_get_BrowserProcessId,
    [9] = cwv2_get_CanGoBack,
    [10] = cwv2_get_CanGoForward,
    [11] = cwv2_get_DocumentTitle,
    [12] = cwv2_get_ContainsFullScreenElement,
    [13] = cwv2_add_NavigationStarting,
    [14] = cwv2_remove_NavigationStarting,
    [15] = cwv2_add_ContentLoading,
    [16] = cwv2_remove_ContentLoading,
    [17] = cwv2_add_SourceChanged,
    [18] = cwv2_remove_SourceChanged,
    [19] = cwv2_add_HistoryChanged,
    [20] = cwv2_remove_HistoryChanged,
    [21] = cwv2_add_NavigationCompleted,
    [22] = cwv2_remove_NavigationCompleted,
    [23] = cwv2_add_FrameNavigationStarting,
    [24] = cwv2_remove_FrameNavigationStarting,
    [25] = cwv2_add_FrameNavigationCompleted,
    [26] = cwv2_remove_FrameNavigationCompleted,
    [27] = cwv2_add_ScriptDialogOpening,
    [28] = cwv2_remove_ScriptDialogOpening,
    [29] = cwv2_add_PermissionRequested,
    [30] = cwv2_remove_PermissionRequested,
    [31] = cwv2_add_ProcessFailed,
    [32] = cwv2_remove_ProcessFailed,
    [33] = cwv2_add_WebMessageReceived,
    [34] = cwv2_remove_WebMessageReceived,
    [35] = cwv2_add_NewWindowRequested,
    [36] = cwv2_remove_NewWindowRequested,
    [37] = cwv2_add_DocumentTitleChanged,
    [38] = cwv2_remove_DocumentTitleChanged,
    [39] = cwv2_add_ContainsFullScreenElementChanged,
    [40] = cwv2_remove_ContainsFullScreenElementChanged,
    [41] = cwv2_add_WebResourceRequested,
    [42] = cwv2_remove_WebResourceRequested,
    [43] = cwv2_add_WindowCloseRequested,
    [44] = cwv2_remove_WindowCloseRequested,
    [45] = cwv2_Navigate,
    [46] = cwv2_NavigateToString,
    [47] = cwv2_AddScriptToExecuteOnDocumentCreatedAsync,
    [48] = cwv2_RemoveScriptToExecuteOnDocumentCreated,
    [49] = cwv2_ExecuteScriptAsync,
    [51] = cwv2_Reload,
    [52] = cwv2_PostWebMessageAsJson,
    [53] = cwv2_PostWebMessageAsString,
    [54] = cwv2_CallDevToolsProtocolMethodAsync,
    [55] = cwv2_GoBack,
    [56] = cwv2_GoForward,
    [58] = cwv2_Stop,
    [59] = cwv2_AddHostObjectToScript,
    [60] = cwv2_RemoveHostObjectFromScript,
    [61] = cwv2_OpenDevToolsWindow,
    [62] = cwv2_AddWebResourceRequestedFilter,
    [63] = cwv2_RemoveWebResourceRequestedFilter,
};

static const void *cwv2_2_vtbl[256] =
{
    [0] = cwv2_2_QueryInterface,
    [1] = cwv2_2_AddRef,
    [2] = cwv2_2_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
    [7] = cwv2_2_get_Environment,
    [8] = cwv2_2_add_WebResourceResponseReceived,
    [9] = cwv2_2_remove_WebResourceResponseReceived,
    [10] = cwv2_2_add_DOMContentLoaded,
    [11] = cwv2_2_remove_DOMContentLoaded,
    [12] = cwv2_2_NavigateWithWebResourceRequest,
};

static const void *cwv2_3_vtbl[256] =
{
    [0] = cwv2_3_QueryInterface,
    [1] = cwv2_3_AddRef,
    [2] = cwv2_3_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
};

static const void *cwv2_11_vtbl[256] =
{
    [0] = cwv2_11_QueryInterface,
    [1] = cwv2_11_AddRef,
    [2] = cwv2_11_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
    [6] = cwv2_11_add_ContextMenuRequested,
    [7] = cwv2_11_remove_ContextMenuRequested,
    [8] = cwv2_11_CallDevToolsProtocolMethodForSessionAsync,
};

static const void *cwv2_13_vtbl[256] =
{
    [0] = cwv2_13_QueryInterface,
    [1] = cwv2_13_AddRef,
    [2] = cwv2_13_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
    [6] = cwv2_get_Profile,
};

static const void *cwv2_stub_vtbl[256] =
{
    [0] = cwv2_stub_QueryInterface,
    [1] = cwv2_stub_AddRef,
    [2] = cwv2_stub_Release,
    [3] = cwv2_GetIids,
    [4] = cwv2_GetRuntimeClassName,
    [5] = cwv2_GetTrustLevel,
    [6 ... 255] = cwv2_generic_slot,
};

static const GUID IID_ICoreWebView2InitializedEventArgs =
    { 0xee59d277, 0x8b2e, 0x57ab, { 0x86, 0x31, 0x91, 0xd2, 0x7b, 0x12, 0xeb, 0xd9 } };

struct cwv2_args
{
    IInspectable IInspectable_iface;
    LONG ref;
};

static HRESULT WINAPI cwv2_args_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2InitializedEventArgs ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_args_AddRef( IInspectable *iface )
{
    struct cwv2_args *impl = (struct cwv2_args *)iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cwv2_args_Release( IInspectable *iface )
{
    struct cwv2_args *impl = (struct cwv2_args *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cwv2_args_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cwv2_args_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.UI.Xaml.Controls.CoreWebView2InitializedEventArgs", 59, name ); }
static HRESULT WINAPI cwv2_args_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI cwv2_args_get_Exception( void *iface, HRESULT *ex )
{
    if (ex) *ex = S_OK;
    return S_OK;
}

static const void *cwv2_args_vtbl[256] =
{
    [0] = cwv2_args_QueryInterface,
    [1] = cwv2_args_AddRef,
    [2] = cwv2_args_Release,
    [3] = cwv2_args_GetIids,
    [4] = cwv2_args_GetRuntimeClassName,
    [5] = cwv2_args_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = cwv2_args_get_Exception,    /* 6: get_Exception */
};

static HRESULT WINAPI wv2_get_CoreWebView2( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_get_CoreWebView2 iface %p, out %p, core %p\n", iface, out, impl->core_webview2 );
    if (!out) return E_POINTER;
    *out = impl->core_webview2;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static void trigger_core_webview2_initialized( struct xaml_element *impl, void *iface );
static void wv2_start_browser( struct xaml_element *impl );
static void wv2_defer_start_browser( struct xaml_element *impl );
static void wv2_paste_clipboard_text( HWND hwnd );

static struct xaml_element *g_active_wv2_impl;
static HWND g_wv2_core_hwnd;
static HWND g_wv2_host_hwnd;
static HWND g_wv2_subclassed_hwnd;
static WNDPROC g_wv2_prev_wndproc;
static HWND g_wv2_sta_hwnd;
static BOOL g_wv2_focused;
static BOOL g_wv2_ctrl_down;
static BOOL g_wv2_shift_down;
static BOOL g_wv2_alt_down;
#define WM_APP_WV2_WHEEL (WM_APP + 0x58)
#define WM_APP_WV2_MOUSE (WM_APP + 0x59)
#define WM_APP_WV2_KEY   (WM_APP + 0x5a)
#define WM_APP_WV2_CHAR  (WM_APP + 0x5b)
#define WM_APP_WV2_PASTE (WM_APP + 0x5c)

static BOOL CALLBACK wv2_post_wheel_cb( HWND child, LPARAM lparam )
{
    WCHAR cls[128] = {0};
    const MSG *m = (const MSG *)lparam;

    GetClassNameW( child, cls, ARRAY_SIZE( cls ) );
    if (wcsstr( cls, L"Chrome_RenderWidgetHostHWND" ) || wcsstr( cls, L"Chrome_WidgetWin" ))
        PostMessageW( child, m->message, m->wParam, m->lParam );
    EnumChildWindows( child, wv2_post_wheel_cb, lparam );
    return TRUE;
}

static BOOL CALLBACK wv2_find_client_cb( HWND child, LPARAM lparam )
{
    WCHAR cls[128] = {0};
    DWORD pid = 0;

    GetWindowThreadProcessId( child, &pid );
    if (pid == GetCurrentProcessId())
    {
        GetClassNameW( child, cls, ARRAY_SIZE( cls ) );
        if (wcsstr( cls, L"Chrome_WidgetWin" ))
        {
            *(HWND *)lparam = child;
            return FALSE;
        }
    }
    EnumChildWindows( child, wv2_find_client_cb, lparam );
    return *(HWND *)lparam == NULL;
}

static HWND wv2_find_client_hwnd( HWND host )
{
    HWND vis, cli = NULL;

    if (host && IsWindow( host ))
    {
        vis = (HWND)GetPropW( host, L"ClientVisualWindow" );
        if (vis && IsWindow( vis ))
        {
            cli = (HWND)GetPropW( vis, L"ClientWindow" );
            if (cli && IsWindow( cli )) return cli;
        }
        EnumChildWindows( host, wv2_find_client_cb, (LPARAM)&cli );
        if (cli && IsWindow( cli )) return cli;
    }
    return NULL;
}

static void wv2_ensure_focus( struct xaml_element *impl, HWND host, BOOL force )
{
    HWND cli;

    if (!impl) return;
    if (g_wv2_focused && !force) return;
    cli = wv2_find_client_hwnd( host );
    if (cli && IsWindow( cli ))
    {
        SendMessageW( cli, WM_SETFOCUS, 0, 0 );
        g_wv2_focused = TRUE;
    }
    if (force && impl->loader_core && impl->real_loader)
    {
        /* Slot 29 on ICoreWebView2: ExecuteScript(LPCWSTR javaScript, ICoreWebView2ExecuteScriptCompletedHandler *handler) */
        HRESULT (WINAPI *exec_script)( void *, LPCWSTR, void * ) =
            wv2_slot_fn( impl->loader_core, TRUE, 29, -1 );
        if (exec_script)
        {
            exec_script( impl->loader_core,
                L"(function(){function f(){var a=document.activeElement;"
                L"if(!a||(a.tagName!=='INPUT'&&a.tagName!=='TEXTAREA'&&!a.isContentEditable)){"
                L"var el=document.querySelector('input:not([type=hidden]),textarea,[contenteditable=true]');"
                L"if(el)el.focus();}}f();setTimeout(f,200);setTimeout(f,600);setTimeout(f,1500);setTimeout(f,3000);})()",
                NULL );
        }
    }
}

static HRESULT wv2_sta_send_keyboard_input( struct xaml_element *impl, HWND host,
                                            UINT key_msg, UINT32 vk, UINT32 scan,
                                            UINT32 repeat, UINT32 mods, LPARAM lparam )
{
    HWND cli;
    BYTE ks[256];
    BOOL had_ks = FALSE;
    HRESULT hr = E_NOINTERFACE;

    if (!impl) return E_POINTER;
    wv2_ensure_focus( impl, host, (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) && key_msg == WM_KEYDOWN );
    cli = wv2_find_client_hwnd( host );
    if (key_msg == WM_KEYDOWN && vk == 'V' && (mods & MK_CONTROL))
    {
        /* Ctrl+V is handled by wv2_paste_clipboard_text via per-char SendCharacterInput;
         * skip forwarding raw Ctrl+V to ClientWindowWin32 to avoid double-pasting. */
        hr = (cli || impl->loader_comp_ctrl) ? S_OK : S_FALSE;
        TRACE( "wv2_sta_wndproc: SendKeyboardInput hr=%#lx msg=%#x vk=%#x scan=%#x repeat=%u mods=%#x cli=%p\n",
               hr, key_msg, vk, scan, repeat, mods, cli );
        return hr;
    }
    if (!(mods & MK_CONTROL) && GetKeyboardState( ks ))
    {
        BYTE clean_ks[256];
        memcpy( clean_ks, ks, sizeof(clean_ks) );
        clean_ks[VK_CONTROL] = clean_ks[VK_LCONTROL] = clean_ks[VK_RCONTROL] = 0;
        clean_ks[VK_MENU] = clean_ks[VK_LMENU] = clean_ks[VK_RMENU] = 0;
        SetKeyboardState( clean_ks );
        had_ks = TRUE;
    }
    if (cli && IsWindow( cli ))
    {
        SendMessageW( cli, key_msg, vk, lparam );
        hr = S_OK;
    }
    else if (host && IsWindow( host ))
    {
        MSG m = { host, key_msg, vk, lparam };
        EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        hr = impl->loader_comp_ctrl ? S_OK : S_FALSE;
    }
    if (had_ks)
        SetKeyboardState( ks );
    TRACE( "wv2_sta_wndproc: SendKeyboardInput hr=%#lx msg=%#x vk=%#x scan=%#x repeat=%u mods=%#x cli=%p\n",
           hr, key_msg, vk, scan, repeat, mods, cli );
    return hr;
}

static HRESULT wv2_sta_send_character_input( struct xaml_element *impl, HWND host,
                                             WCHAR ch, BOOL keydown, LPARAM lparam )
{
    HWND cli;
    BYTE ks[256];
    BOOL had_ks = FALSE;
    HRESULT hr = E_NOINTERFACE;

    if (!impl) return E_POINTER;
    wv2_ensure_focus( impl, host, FALSE );
    cli = wv2_find_client_hwnd( host );
    if (GetKeyboardState( ks ))
    {
        BYTE clean_ks[256];
        memcpy( clean_ks, ks, sizeof(clean_ks) );
        clean_ks[VK_CONTROL] = clean_ks[VK_LCONTROL] = clean_ks[VK_RCONTROL] = 0;
        clean_ks[VK_MENU] = clean_ks[VK_LMENU] = clean_ks[VK_RMENU] = 0;
        SetKeyboardState( clean_ks );
        had_ks = TRUE;
    }
    if (cli && IsWindow( cli ))
    {
        SendMessageW( cli, WM_CHAR, (WPARAM)ch, lparam ? lparam : 1 );
        hr = S_OK;
    }
    else if (host && IsWindow( host ))
    {
        MSG m = { host, WM_CHAR, (WPARAM)ch, lparam ? lparam : 1 };
        EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        hr = impl->loader_comp_ctrl ? S_OK : S_FALSE;
    }
    if (impl->loader_core && impl->real_loader)
    {
        HRESULT (WINAPI *exec_script)( void *, LPCWSTR, void * ) =
            wv2_slot_fn( impl->loader_core, TRUE, 29, -1 );
        if (exec_script)
        {
            WCHAR script[512];
            if (ch == '\b')
            {
                lstrcpyW( script,
                    L"(function(){var a=document.activeElement;"
                    L"if(!a||(a.tagName!=='INPUT'&&a.tagName!=='TEXTAREA'&&!a.isContentEditable)){"
                    L"a=document.querySelector('input:not([type=hidden]),textarea,[contenteditable=true]');"
                    L"if(a)a.focus();}"
                    L"if(a)document.execCommand('delete',false,null);})()" );
            }
            else if (ch >= 0x20)
            {
                swprintf( script, ARRAY_SIZE(script),
                    L"(function(){var a=document.activeElement;"
                    L"if(!a||(a.tagName!=='INPUT'&&a.tagName!=='TEXTAREA'&&!a.isContentEditable)){"
                    L"a=document.querySelector('input:not([type=hidden]),textarea,[contenteditable=true]');"
                    L"if(a)a.focus();}"
                    L"if(a)document.execCommand('insertText',false,'\\u%04x');})()",
                    (unsigned int)ch );
            }
            else
            {
                script[0] = 0;
            }
            if (script[0])
                exec_script( impl->loader_core, script, NULL );
            hr = S_OK;
        }
    }
    if (had_ks)
        SetKeyboardState( ks );
    TRACE( "wv2_sta_wndproc: SendCharacterInput hr=%#lx ch=%#x keydown=%d cli=%p\n",
           hr, (unsigned int)ch, keydown, cli );
    return hr;
}

static LRESULT CALLBACK wv2_sta_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_APP_WV2_WHEEL && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        POINT pt = { (short)LOWORD( lparam ), (short)HIWORD( lparam ) };
        if (host && IsWindow( host )) ScreenToClient( host, &pt );
        if (g_active_wv2_impl->loader_comp_ctrl)
        {
            ICoreWebView2CompositionController *comp_ctrl = g_active_wv2_impl->loader_comp_ctrl;
            HRESULT hr_m = comp_ctrl->lpVtbl->SendMouseInput( comp_ctrl, WM_MOUSEWHEEL,
                (UINT32)LOWORD( wparam ), (UINT32)(short)HIWORD( wparam ), pt );
            TRACE( "wv2_sta_wndproc: SendMouseInput(WM_MOUSEWHEEL) pt=%ld,%ld delta=%d hr=%#lx\n",
                   pt.x, pt.y, (int)(short)HIWORD( wparam ), hr_m );
        }
        else if (host)
        {
            MSG m = { host, WM_MOUSEWHEEL, wparam, lparam };
            EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        }
        return 0;
    }
    if (msg == WM_APP_WV2_MOUSE && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        UINT32 ev_kind = LOWORD( wparam );
        UINT32 vkeys = HIWORD( wparam );
        POINT pt = { (short)LOWORD( lparam ), (short)HIWORD( lparam ) };
        if (ev_kind == WM_MOUSELEAVE)
        {
            vkeys = 0;
            pt.x = 0;
            pt.y = 0;
        }
        if (ev_kind == WM_LBUTTONDOWN || ev_kind == WM_LBUTTONUP ||
            ev_kind == WM_RBUTTONDOWN || ev_kind == WM_MBUTTONDOWN)
            wv2_ensure_focus( g_active_wv2_impl, host, TRUE );
        if (g_active_wv2_impl->loader_comp_ctrl)
        {
            ICoreWebView2CompositionController *comp_ctrl = g_active_wv2_impl->loader_comp_ctrl;
            HRESULT hr_m = comp_ctrl->lpVtbl->SendMouseInput( comp_ctrl, ev_kind, vkeys, 0, pt );
            if (ev_kind != WM_MOUSEMOVE)
                TRACE( "wv2_sta_wndproc: SendMouseInput(%#x) pt=%ld,%ld vkeys=%#x hr=%#lx\n",
                       ev_kind, pt.x, pt.y, vkeys, hr_m );
        }
        else if (host)
        {
            MSG m = { host, ev_kind, vkeys, lparam };
            EnumChildWindows( host, wv2_post_wheel_cb, (LPARAM)&m );
        }
        return 0;
    }
    if (msg == WM_APP_WV2_KEY && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        UINT32 vk = LOWORD( wparam ) & 0xff;
        UINT32 mods = (LOWORD( wparam ) >> 8) & 0xff;
        UINT key_msg = HIWORD( wparam );
        UINT32 repeat = (UINT32)LOWORD( lparam );
        UINT32 scan = (UINT32)((lparam >> 16) & 0xff);
        wv2_sta_send_keyboard_input( g_active_wv2_impl, host, key_msg, vk, scan, repeat, mods, lparam );
        return 0;
    }
    if (msg == WM_APP_WV2_CHAR && g_active_wv2_impl)
    {
        HWND host = g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
        WCHAR ch = (WCHAR)LOWORD( wparam );
        BOOL keydown = HIWORD( wparam ) != 0;
        wv2_sta_send_character_input( g_active_wv2_impl, host, ch, keydown, lparam );
        return 0;
    }
    if (msg == WM_APP_WV2_PASTE)
    {
        /* The whole clipboard read + per-character key/char pump runs here, on
         * the STA bridge thread (same pattern as WM_APP_WV2_WHEEL), so the
         * host wndproc returns from the Ctrl+V keydown without blocking. */
        wv2_paste_clipboard_text( hwnd );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void wv2_ensure_sta_hwnd( void )
{
    WNDCLASSW wc;
    if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd )) return;
    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc = wv2_sta_wndproc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"WineWv2StaWheelBridge";
    RegisterClassW( &wc );
    g_wv2_sta_hwnd = CreateWindowExW( 0, L"WineWv2StaWheelBridge", L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, NULL, wc.hInstance, NULL );
}

static void wv2_send_keyboard_input( HWND hwnd, UINT key_msg, UINT32 vk, UINT32 scan,
                                     UINT32 repeat, UINT32 mods, LPARAM lparam )
{
    WPARAM wp = MAKEWPARAM( (vk & 0xff) | ((mods & 0xff) << 8), key_msg & 0xffff );
    if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
        PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_KEY, wp, lparam );
    else if (hwnd)
    {
        MSG m = { hwnd, key_msg, vk, lparam };
        EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
    }
}

static void wv2_send_character_input( HWND hwnd, WCHAR ch, BOOL keydown, LPARAM lparam )
{
    WPARAM wp = MAKEWPARAM( (WORD)ch, (WORD)(keydown ? 1 : 0) );
    if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
        PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_CHAR, wp, lparam );
    else if (hwnd)
    {
        MSG m = { hwnd, WM_CHAR, (WPARAM)ch, lparam };
        EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
    }
}

/* Paste path choice: read CF_UNICODETEXT (with CF_TEXT fallback) from the Win32
 * clipboard and dispatch a per-character SendKeyboardInput(WM_KEYDOWN) +
 * SendCharacterInput(WM_CHAR) + SendKeyboardInput(WM_KEYUP) loop with VK_CONTROL
 * cleared in the STA thread's keyboard state. Chosen over CoreWebView2
 * ExecuteScript("document.execCommand('paste')") because Chromium blocks
 * programmatic paste without clipboard-read permission / transient user activation
 * and bypasses the native keydown/keypress/beforeinput/input sequence expected by
 * controlled React inputs (such as WhatsApp Web's phone-number field). */
static void wv2_paste_clipboard_text( HWND hwnd )
{
    WCHAR *buf = NULL;
    size_t i, len = 0;

    if (!OpenClipboard( NULL ) && (!hwnd || !OpenClipboard( hwnd )))
    {
        TRACE( "wv2_paste_clipboard_text: OpenClipboard failed err=%lu\n", GetLastError() );
        return;
    }

    if (IsClipboardFormatAvailable( CF_UNICODETEXT ))
    {
        HANDLE h = GetClipboardData( CF_UNICODETEXT );
        const WCHAR *src = h ? GlobalLock( h ) : NULL;
        if (src)
        {
            len = lstrlenW( src );
            if ((buf = HeapAlloc( GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR) )))
                memcpy( buf, src, (len + 1) * sizeof(WCHAR) );
            GlobalUnlock( h );
        }
    }
    else if (IsClipboardFormatAvailable( CF_TEXT ))
    {
        HANDLE h = GetClipboardData( CF_TEXT );
        const char *src = h ? GlobalLock( h ) : NULL;
        if (src)
        {
            int wlen = MultiByteToWideChar( CP_UTF8, 0, src, -1, NULL, 0 );
            if (wlen > 0 && (buf = HeapAlloc( GetProcessHeap(), 0, wlen * sizeof(WCHAR) )))
            {
                MultiByteToWideChar( CP_UTF8, 0, src, -1, buf, wlen );
                len = (size_t)(wlen - 1);
            }
            GlobalUnlock( h );
        }
    }
    CloseClipboard();

    if (!buf || !len)
    {
        HeapFree( GetProcessHeap(), 0, buf );
        return;
    }

    if (len > 32768) len = 32768; /* clamp to 64 KiB of UTF-16 code units: each
                                   * unit costs a PostMessage + a Blink
                                   * execCommand round-trip, unbounded clips
                                   * stalled the paste pump */

    TRACE( "wv2_paste_clipboard_text: pasting %zu chars %s\n", len, debugstr_w( buf ) );
    for (i = 0; i < len; i++)
    {
        WCHAR ch = buf[i], ch2 = 0;
        SHORT vks;
        UINT32 vk = 0, scan = 0;
        LPARAM lp_down, lp_up;

        /* CRLF is one Enter keypress: emit it as keydown(VK_RETURN) +
         * WM_CHAR('\r') + keyup once (the CR carried the key events, the LF
         * used to arrive as a bare, keydown-less WM_CHAR('\n')). */
        if (ch == '\r' && i + 1 < len && buf[i + 1] == '\n') i++;
        else if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < len &&
                 buf[i + 1] >= 0xdc00 && buf[i + 1] <= 0xdfff)
        {
            /* Surrogate pair (emoji, astral CJK): Chromium's WM_CHAR
             * convention sends a pair as two WM_CHAR messages — high unit
             * first, then low — inside ONE keydown/keyup press. Two separate
             * keypresses made Blink decode 2× U+FFFD. */
            ch2 = buf[++i];
        }

        vks = VkKeyScanW( ch );
        if (vks != -1)
        {
            vk = LOBYTE( vks ) & 0xff;
            scan = MapVirtualKeyW( vk, MAPVK_VK_TO_VSC ) & 0xff;
        }
        lp_down = 1 | ((LPARAM)scan << 16);
        lp_up = lp_down | (LPARAM)0xc0000000;
        if (vk)
            wv2_send_keyboard_input( hwnd, WM_KEYDOWN, vk, scan, 1, 0, lp_down );
        wv2_send_character_input( hwnd, ch, TRUE, lp_down );
        if (ch2)
            wv2_send_character_input( hwnd, ch2, TRUE, lp_down );
        if (vk)
            wv2_send_keyboard_input( hwnd, WM_KEYUP, vk, scan, 1, 0, lp_up );
    }
    HeapFree( GetProcessHeap(), 0, buf );
}

static LRESULT CALLBACK wv2_host_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_MOUSEWHEEL && g_active_wv2_impl)
    {
        if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
        {
            PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_WHEEL, wparam, lparam );
            TRACE( "wv2_host_wndproc: forwarded WM_MOUSEWHEEL delta=%d to sta=%p\n",
                   (int)(short)HIWORD( wparam ), g_wv2_sta_hwnd );
        }
        else
        {
            MSG m = { hwnd, msg, wparam, lparam };
            EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
        }
    }
    else if ((msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
              msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP ||
              msg == WM_RBUTTONDBLCLK || msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP ||
              msg == WM_MBUTTONDBLCLK || msg == WM_MOUSELEAVE) && g_active_wv2_impl)
    {
        WPARAM wp = MAKEWPARAM( msg, LOWORD( wparam ) & 0x7f );
        if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
            PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_MOUSE, wp, lparam );
        else
        {
            MSG m = { hwnd, msg, wparam, lparam };
            EnumChildWindows( hwnd, wv2_post_wheel_cb, (LPARAM)&m );
        }
    }
    else if ((msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) &&
             g_active_wv2_impl)
    {
        UINT32 vk = (UINT32)wparam;
        UINT32 repeat = (UINT32)LOWORD( lparam );
        UINT32 scan = (UINT32)((lparam >> 16) & 0xff);
        UINT32 mods = 0;
        BOOL is_down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);

        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) g_wv2_ctrl_down = is_down;
        if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) g_wv2_shift_down = is_down;
        if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) g_wv2_alt_down = is_down;
        if (g_wv2_shift_down || (GetKeyState( VK_SHIFT ) & 0x8000)) mods |= MK_SHIFT;
        if (g_wv2_ctrl_down || (GetKeyState( VK_CONTROL ) & 0x8000) || (GetAsyncKeyState( VK_CONTROL ) & 0x8000))
            mods |= MK_CONTROL;
        if (g_wv2_alt_down || (GetKeyState( VK_MENU ) & 0x8000)) mods |= 0x20;

        wv2_send_keyboard_input( hwnd, msg, vk, scan, repeat, mods, lparam );
        if (msg == WM_KEYDOWN && vk == 'V' && (mods & MK_CONTROL))
        {
            /* Hand the paste loop to the STA bridge (pattern: WM_APP_WV2_WHEEL)
             * so the clipboard read + per-char pump does not block this
             * wndproc; the bridge window always exists once the host window is
             * live, the inline call is the fallback. */
            wv2_ensure_sta_hwnd();
            if (g_wv2_sta_hwnd && IsWindow( g_wv2_sta_hwnd ))
                PostMessageW( g_wv2_sta_hwnd, WM_APP_WV2_PASTE, 0, 0 );
            else
                wv2_paste_clipboard_text( hwnd );
        }
    }
    else if (msg == WM_CHAR && g_active_wv2_impl)
    {
        WCHAR ch = (WCHAR)wparam;
        if (!g_wv2_ctrl_down && (ch >= 0x20 || ch == '\b' || ch == '\t' || ch == '\r'))
            wv2_send_character_input( hwnd, ch, TRUE, lparam );
    }
    if (g_wv2_prev_wndproc && g_wv2_prev_wndproc != wv2_host_wndproc)
        return CallWindowProcW( g_wv2_prev_wndproc, hwnd, msg, wparam, lparam );
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void wv2_ensure_host_subclass( HWND parent )
{
    WNDPROC prev;
    if (!parent || !IsWindow( parent ) || g_wv2_subclassed_hwnd == parent) return;
    if (GetWindowThreadProcessId( parent, NULL ) != GetCurrentThreadId()) return;
    prev = (WNDPROC)SetWindowLongPtrW( parent, GWLP_WNDPROC, (LONG_PTR)wv2_host_wndproc );
    if (prev && prev != wv2_host_wndproc)
    {
        g_wv2_prev_wndproc = prev;
        g_wv2_subclassed_hwnd = parent;
        ERR( "wv2_ensure_host_subclass: subclassed parent=%p prev=%p\n", parent, prev );
    }
}

struct wv2_enum_ctx
{
    int width;
    int height;
    int count;
};

static HWND wv2_get_controller_parent( void );

static BOOL CALLBACK wv2_sync_child_cb( HWND child, LPARAM lparam )
{
    struct wv2_enum_ctx *ctx = (struct wv2_enum_ctx *)lparam;
    WCHAR cls[128] = {0};

    GetClassNameW( child, cls, ARRAY_SIZE( cls ) );
    if (wcsstr( cls, L"Chrome_WidgetWin" ) || wcsstr( cls, L"Intermediate D3D" ) ||
        wcsstr( cls, L"Edge" ) || wcsstr( cls, L"WebView" ) || wcsstr( cls, L"RenderWidget" ))
    {
        if (g_active_wv2_impl && g_active_wv2_impl->has_comp_ctrl)
        {
            HWND parent = GetParent( child );
            int pos = (!parent || parent == wv2_get_controller_parent()) ? -4000 : 0;
            SetWindowPos( child, HWND_BOTTOM, pos, pos, ctx->width, ctx->height,
                          SWP_NOACTIVATE );
        }
        else
        {
            ShowWindow( child, SW_SHOW );
            SetWindowPos( child, HWND_TOP, 0, 0, ctx->width, ctx->height,
                          SWP_SHOWWINDOW | SWP_NOACTIVATE );
            ERR( "wv2_sync_child_cb: positioned child %p (%s) to %dx%d\n",
                   child, debugstr_w( cls ), ctx->width, ctx->height );
        }
        ctx->count++;
    }
    EnumChildWindows( child, wv2_sync_child_cb, lparam );
    return TRUE;
}

static HANDLE g_xaml_window_ready_event;

static HANDLE get_xaml_window_ready_event( void )
{
    if (!g_xaml_window_ready_event)
    {
        HANDLE ev = CreateEventW( NULL, FALSE, FALSE, NULL );
        if (ev && InterlockedCompareExchangePointer( &g_xaml_window_ready_event, ev, NULL ) != NULL)
            CloseHandle( ev );
    }
    return g_xaml_window_ready_event;
}

static BOOL CALLBACK wv2_find_process_window_cb( HWND hwnd, LPARAM lparam )
{
    DWORD pid = 0;
    if (!IsWindowVisible( hwnd )) return TRUE;
    if ((GetWindowLongW( hwnd, GWL_STYLE ) & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW) return TRUE;
    GetWindowThreadProcessId( hwnd, &pid );
    if (pid != GetCurrentProcessId()) return TRUE;
    *(HWND *)lparam = hwnd;
    return FALSE;
}

static HWND wv2_get_controller_parent( void )
{
    HWND hw = g_active_wv2_impl && g_active_wv2_impl->parent_hwnd ? g_active_wv2_impl->parent_hwnd : g_wv2_core_hwnd;
    if (hw && IsWindow( hw ))
    {
        HWND root = GetAncestor( hw, GA_ROOT );
        return root ? root : hw;
    }
    hw = xaml_get_window_hwnd();
    if (hw && IsWindow( hw )) return hw;
    hw = xaml_find_core_window();
    if (hw) return hw;
    hw = NULL;
    EnumWindows( wv2_find_process_window_cb, (LPARAM)&hw );
    if (hw) return hw;
    return NULL;
}

static BOOL CALLBACK wv2_sync_top_cb( HWND hwnd, LPARAM lparam )
{
    struct wv2_enum_ctx *ctx = (struct wv2_enum_ctx *)lparam;
    WCHAR cls[128] = {0};
    DWORD pid = 0;

    GetWindowThreadProcessId( hwnd, &pid );
    if (pid != GetCurrentProcessId()) return TRUE;

    GetClassNameW( hwnd, cls, ARRAY_SIZE( cls ) );
    TRACE( "wv2_sync_top_cb: seen hwnd %p class %s\n", hwnd, debugstr_w(cls) );
    if (wcsstr( cls, L"Chrome_WidgetWin" ) || wcsstr( cls, L"Intermediate D3D" ) ||
        wcsstr( cls, L"RenderWidget" ))
    {
        HWND parent = wv2_get_controller_parent();
        if (parent && IsWindow( parent ) && hwnd != parent)
        {
            SetParent( hwnd, parent );
            if (g_active_wv2_impl && g_active_wv2_impl->has_comp_ctrl)
            {
                SetWindowPos( hwnd, HWND_BOTTOM, -4000, -4000, ctx->width, ctx->height,
                              SWP_NOACTIVATE );
            }
            else
            {
                ShowWindow( hwnd, SW_SHOW );
                SetWindowPos( hwnd, HWND_TOP, 0, 0, ctx->width, ctx->height,
                              SWP_SHOWWINDOW | SWP_NOACTIVATE );
                ERR( "wv2_sync_top_cb: re-parented and positioned child %p (%s) to parent %p %dx%d\n",
                     hwnd, debugstr_w( cls ), parent, ctx->width, ctx->height );
            }
            EnumChildWindows( hwnd, wv2_sync_child_cb, lparam );
            ctx->count++;
        }
    }
    return TRUE;
}

static void wv2_sync_child_hwnds( HWND parent, UINT width, UINT height )
{
    struct wv2_enum_ctx ctx;

    if (!parent || !IsWindow( parent )) parent = wv2_get_controller_parent();
    if ((int)width <= 0 || (int)height <= 0)
    {
        SIZE sz;
        xaml_get_layout_metrics( parent, &sz, NULL );
        if ((int)width <= 0) width = (UINT)sz.cx;
        if ((int)height <= 0) height = (UINT)sz.cy;
    }

    ctx.width = (int)width;
    ctx.height = (int)height;
    ctx.count = 0;

    if (parent && IsWindow( parent ))
        EnumChildWindows( parent, wv2_sync_child_cb, (LPARAM)&ctx );
    EnumWindows( wv2_sync_top_cb, (LPARAM)&ctx );
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

static DWORD WINAPI wv2_sta_thread( void *arg )
{
    struct xaml_element *impl = arg;
    HWND hw = NULL;
    HANDLE ev;
    DWORD start;
    MSG msg;

    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
    wv2_ensure_sta_hwnd();
    ev = get_xaml_window_ready_event();
    start = GetTickCount();
    while (!(hw = xaml_get_window_hwnd()) || !IsWindow( hw ))
    {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= 2000) break;
        if (!ev || WaitForSingleObject( ev, 2000 - elapsed ) != WAIT_OBJECT_0)
        {
            hw = xaml_get_window_hwnd();
            break;
        }
    }
    ERR( "wv2_sta_thread: starting browser, target=%p\n", hw );
    impl->parent_hwnd = hw;
    g_wv2_core_hwnd = hw;
    wv2_start_browser( impl );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}

static void wv2_defer_start_browser( struct xaml_element *impl )
{
    HANDLE thread;

    g_active_wv2_impl = impl;
    if (impl->browser_queued) return;
    impl->browser_queued = TRUE;
    if ((thread = CreateThread( NULL, 0, wv2_sta_thread, impl, 0, NULL )))
        CloseHandle( thread );
    else
        wv2_start_browser( impl );
}

static HRESULT WINAPI wv2_add_CoreWebView2Initialized( void *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_add_CoreWebView2Initialized iface %p, handler %p, token %p\n", iface, handler, token );
    impl->init_handler = (IInspectable *)handler;
    if (impl->init_handler) IInspectable_AddRef( impl->init_handler );
    if (token) token->value = 1;
    return S_OK;
}

static HRESULT WINAPI wv2_remove_CoreWebView2Initialized( void *iface, EventRegistrationToken token )
{
    TRACE( "wv2_remove_CoreWebView2Initialized iface %p\n", iface );
    return S_OK;
}

static HSTRING wv2_extract_uri_string( IInspectable *obj )
{
    static const GUID IID_IUriRuntimeClass_local =
        { 0x9e365e57, 0x48b2, 0x4160, { 0x95, 0x6f, 0xc7, 0x38, 0x51, 0x20, 0xbb, 0xfc } };
    static const GUID IID_IPropertyValue_local =
        { 0x4bd682dd, 0x7554, 0x40e9, { 0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62 } };
    void *uri_rt = NULL, *pv = NULL;
    HSTRING hstr = NULL;

    if (!obj) return NULL;
    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IUriRuntimeClass_local, &uri_rt )) && uri_rt)
    {
        if (FAILED(IUriRuntimeClass_get_AbsoluteUri( (IUriRuntimeClass *)uri_rt, &hstr )) || !hstr)
        {
            IUriRuntimeClass_get_RawUri( (IUriRuntimeClass *)uri_rt, &hstr );
        }
        IInspectable_Release( (IInspectable *)uri_rt );
        if (hstr) return hstr;
    }
    if (SUCCEEDED(IInspectable_QueryInterface( obj, &IID_IPropertyValue_local, &pv )) && pv)
    {
        IPropertyValue_GetString( (IPropertyValue *)pv, &hstr );
        IInspectable_Release( (IInspectable *)pv );
    }
    return hstr;
}

static void wv2_navigate_once( struct xaml_element *impl )
{
    const WCHAR *url = NULL;

    if (impl->navigated) return;
    if (!impl->loader_core) { ERR( "wv2_navigate_once: no loader_core\n" ); return; }
    if (impl->wv2_source_uri)
        url = WindowsGetStringRawBuffer( impl->wv2_source_uri, NULL );
    else if (impl->core_webview2)
    {
        struct core_webview2 *cwv = impl_from_cwv2( impl->core_webview2 );
        if (cwv->source_uri)
            url = WindowsGetStringRawBuffer( cwv->source_uri, NULL );
    }
    if (!url || !*url)
    {
        ERR( "wv2_navigate_once: no WebView2.Source URI set, deferring navigation\n" );
        return;
    }
    impl->navigated = TRUE;
    {
        HRESULT hr = wv2_call_navigate( impl->loader_core, impl->real_loader, 5, 6, url );
        ERR( "wv2_navigate_once (%s): loader_core %p url=%s hr %#lx\n",
             impl->real_loader ? "real" : "builtin", impl->loader_core, debugstr_w( url ), hr );
    }
}

static HRESULT WINAPI wv2_get_Source( void *iface, IInspectable **value )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    if (!value) return E_POINTER;
    *value = impl->wv2_source;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI wv2_put_Source( void *iface, IInspectable *value )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    HSTRING hstr;

    if (impl->wv2_source) IInspectable_Release( impl->wv2_source );
    impl->wv2_source = value;
    if (impl->wv2_source) IInspectable_AddRef( impl->wv2_source );

    if (impl->wv2_source_uri)
    {
        WindowsDeleteString( impl->wv2_source_uri );
        impl->wv2_source_uri = NULL;
    }
    hstr = wv2_extract_uri_string( value );
    if (hstr)
    {
        impl->wv2_source_uri = hstr;
        ERR( "wv2_put_Source: set Source URI %s on %p\n", debugstr_hstring( hstr ), impl );
        if (impl->core_webview2)
        {
            struct core_webview2 *cwv = impl_from_cwv2( impl->core_webview2 );
            if (cwv->source_uri) WindowsDeleteString( cwv->source_uri );
            cwv->source_uri = NULL;
            WindowsDuplicateString( hstr, &cwv->source_uri );
        }
        if (impl->loader_core)
        {
            impl->navigated = FALSE;
            wv2_navigate_once( impl );
        }
    }
    return S_OK;
}

static void trigger_core_webview2_initialized( struct xaml_element *impl, void *iface )
{
    if (!impl->core_webview2)
    {
        struct core_webview2 *cwv = calloc( 1, sizeof(*cwv) );
        if (cwv)
        {
            cwv->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_vtbl );
            cwv->ICoreWebView2_2_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_2_vtbl );
            cwv->ICoreWebView2_3_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_3_vtbl );
            cwv->ICoreWebView2_11_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_11_vtbl );
            cwv->ICoreWebView2_13_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_13_vtbl );
            cwv->ICoreWebView2_stub_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_stub_vtbl );
            cwv->ref = 1;
            cwv->real_loader = impl->real_loader;
            cwv->loader_core = impl->loader_core;
            cwv->owner = impl;
            if (impl->wv2_source_uri)
                WindowsDuplicateString( impl->wv2_source_uri, &cwv->source_uri );
            impl->core_webview2 = &cwv->IInspectable_iface;
        }
    }
    else if (impl->loader_core)
    {
        struct core_webview2 *cwv = impl_from_cwv2( impl->core_webview2 );
        cwv->loader_core = impl->loader_core;
        cwv->real_loader = impl->real_loader;
    }
    if (impl->init_handler && !impl->init_triggered)
    {
        struct cwv2_args *args = calloc( 1, sizeof(*args) );
        struct cwv2_typed_handler *h = (struct cwv2_typed_handler *)impl->init_handler;
        impl->init_triggered = TRUE;
        if (args)
        {
            args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup( cwv2_args_vtbl );
            args->ref = 1;
            ERR( "invoking init_handler %p on WebView %p with args %p\n", impl->init_handler, &impl->IInspectable_iface, args );
            h->lpVtbl->Invoke( h, &impl->IInspectable_iface, &args->IInspectable_iface );
            cwv2_args_Release( &args->IInspectable_iface );
        }
    }
    wv2_navigate_once( impl );
}

/* Drive the real browser: the webview2loader builtin launches msedgewebview2
 * (env creation) and hands back an environment/controller/CoreWebView2 chain.
 * Kick it off from EnsureCoreWebView2Async so the app's init handler receives a
 * live CoreWebView2 and its Navigate reaches the browser over the mojo pipes. */
struct wv2_env_handler
{
    const void **vtbl;
    LONG ref;
    struct xaml_element *impl;
};

struct wv2_ctrl_handler
{
    const void **vtbl;
    LONG ref;
    struct xaml_element *impl;
};

static const GUID IID_ICoreWebView2CreateEnvHandler =
    { 0x4e8a3389, 0xc9d8, 0x4bd2, { 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d } };
static const GUID IID_ICoreWebView2CreateCtrlHandler =
    { 0x6c4819f3, 0xc9b7, 0x4260, { 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c } };
static const GUID IID_ICoreWebView2CreateCompCtrlHandler =
    { 0x02fab84b, 0x1428, 0x4fb7, { 0xad, 0x45, 0x1b, 0x2e, 0x64, 0x73, 0x61, 0x84 } };
static const GUID IID_ICoreWebView2Environment3_g =
    { 0x80a22ae3, 0xbe7c, 0x4ce2, { 0xaf, 0xe1, 0x5a, 0x50, 0x05, 0x6c, 0xde, 0xeb } };
static const GUID IID_ICoreWebView2CompositionController_g =
    { 0x3df9b733, 0xb9ae, 0x4a15, { 0x86, 0xb4, 0xeb, 0x9e, 0xe9, 0x82, 0x64, 0x69 } };
static const GUID IID_ICoreWebView2Controller_g =
    { 0x4d00c0d1, 0x9434, 0x4eb6, { 0x80, 0x78, 0x86, 0x97, 0xa5, 0x60, 0x33, 0x4f } };

static HRESULT WINAPI wv2h_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2CreateEnvHandler ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2CreateCtrlHandler ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2CreateCompCtrlHandler ))
    {
        *out = t;
        InterlockedIncrement( (LONG *)((char *)t + sizeof(void *)) );
        return S_OK;
    }
    ERR( "wv2h_QueryInterface: unknown iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI wv2h_AddRef( void *t )
{
    LONG *ref = (LONG *)((char *)t + sizeof(void *));
    return InterlockedIncrement( ref );
}

static ULONG WINAPI wv2h_Release( void *t )
{
    LONG *ref = (LONG *)((char *)t + sizeof(void *));
    ULONG r = InterlockedDecrement( ref );
    if (!r)
    {
        HeapFree( GetProcessHeap(), 0, *(void **)t );
        HeapFree( GetProcessHeap(), 0, t );
    }
    return r;
}

static HRESULT WINAPI wv2_ctrl_handler_Invoke( void *t, HRESULT hr, void *controller )
{
    struct wv2_ctrl_handler *h = t;
    void *core = NULL;

    ERR( "wv2_ctrl_handler_Invoke hr %#lx controller %p real=%d\n", hr, controller, h->impl->real_loader );
    if (FAILED( hr ))
    {
        ERR( "wv2_ctrl_handler_Invoke: controller creation failed (%#lx), ignoring broken controller\n", hr );
        return hr;
    }
    if (controller)
    {
        if (h->impl->real_loader)
        {
            static BOOL bounds_probed;
            HWND parent = h->impl->parent_hwnd ? h->impl->parent_hwnd : wv2_get_controller_parent();
            SIZE sz;
            float scale = 1.0f;
            RECT rc;
            HRESULT hr_b = S_OK, hr_v = S_OK, hr_c = S_OK;
            ICoreWebView2CompositionController *comp_ctrl = NULL;
            ICoreWebView2Controller *ctrl = NULL;

            xaml_get_layout_metrics( parent, &sz, &scale );
            SetRect( &rc, 0, 0, sz.cx, sz.cy );
            if (parent && IsWindow( parent ))
            {
                RECT cr;
                if (GetClientRect( parent, &cr ) && cr.right > 0 && cr.bottom > 0)
                    rc = cr;
            }

            if (SUCCEEDED( ((IUnknown *)controller)->lpVtbl->QueryInterface(
                    (IUnknown *)controller, &IID_ICoreWebView2CompositionController_g, (void **)&comp_ctrl) ) && comp_ctrl)
            {
                IDCompositionVisual *root = xaml_get_dcomp_root_visual();
                IDCompositionVisual *vis = h->impl->visual;
                D2D_RECT_F clip = { 0.0f, 0.0f, (float)rc.right, (float)rc.bottom };
                if (!vis) { xaml_create_dcomp_visual( &h->impl->visual ); vis = h->impl->visual; }
                if (root)
                    IDCompositionVisual_SetClip( root, &clip );
                if (vis)
                    IDCompositionVisual_SetClip( vis, &clip );
                if (root && vis && vis != root && !h->impl->visual_attached)
                {
                    HRESULT hr_add = IDCompositionVisual_AddVisual( root, vis, TRUE, NULL );
                    if (SUCCEEDED( hr_add ))
                    {
                        h->impl->visual_attached = TRUE;
                        xaml_dcomp_commit();
                    }
                    ERR( "wv2_ctrl_handler_Invoke: AddVisual root=%p vis=%p scale=%.2f hr=%#lx\n", root, vis, scale, hr_add );
                }
                ERR( "wv2_ctrl_handler_Invoke: found CompositionController %p visual=%p root=%p scale=%.2f\n", comp_ctrl, vis, root, scale );
                if (vis)
                {
                    HRESULT hr_rv = comp_ctrl->lpVtbl->put_RootVisualTarget( comp_ctrl, (IUnknown *)vis );
                    if (SUCCEEDED( hr_rv ))
                        h->impl->has_comp_ctrl = TRUE;
                    ERR( "wv2_ctrl_handler_Invoke: put_RootVisualTarget hr=%#lx\n", hr_rv );
                }
                h->impl->loader_comp_ctrl = comp_ctrl;
            }

            if (SUCCEEDED( ((IUnknown *)controller)->lpVtbl->QueryInterface(
                    (IUnknown *)controller, &IID_ICoreWebView2Controller_g, (void **)&ctrl) ) && ctrl)
            {
                h->impl->loader_controller = ctrl;
                if (!bounds_probed)
                {
                    bounds_probed = TRUE;
                    wv2_probe_bounds( ctrl );
                }
                if (parent) ctrl->lpVtbl->put_ParentWindow( ctrl, parent );
                hr_b = ctrl->lpVtbl->put_Bounds( ctrl, rc );
                hr_v = ctrl->lpVtbl->put_IsVisible( ctrl, TRUE );
                ctrl->lpVtbl->NotifyParentWindowPositionChanged( ctrl );
                hr_c = ctrl->lpVtbl->get_CoreWebView2( ctrl, &core );
                ERR( "wv2_ctrl_handler_Invoke (ctrl): parent=%p rc=%ldx%ld put_Bounds=%#lx put_IsVisible=%#lx get_CoreWebView2=%#lx core=%p\n",
                     parent, rc.right, rc.bottom, hr_b, hr_v, hr_c, core );
            }
            else if (!comp_ctrl)
            {
                /* Last-resort fallback only when controller is NOT a 7-slot CompositionController */
                h->impl->loader_controller = controller;
                ((IUnknown *)controller)->lpVtbl->AddRef( (IUnknown *)controller );
                if (parent) wv2_call_hwnd( controller, TRUE, 22, -1, parent );
                hr_b = wv2_call_bounds( controller, TRUE, 6, 4, rc );
                hr_v = wv2_call_bool( controller, TRUE, 4, 4, TRUE );
                wv2_call_void( controller, TRUE, 23, -1 );
                hr_c = wv2_call_get_core( controller, TRUE, 25, 7, &core );
                ERR( "wv2_ctrl_handler_Invoke (fallback): parent=%p rc=%ldx%ld put_Bounds=%#lx put_IsVisible=%#lx hr_c=%#lx core=%p\n",
                     parent, rc.right, rc.bottom, hr_b, hr_v, hr_c, core );
            }
            wv2_ensure_sta_hwnd();
            wv2_ensure_host_subclass( parent );
            wv2_sync_child_hwnds( parent, rc.right, rc.bottom );

            if (core && !h->impl->loader_core)
            {
                h->impl->loader_core = core;
                if (h->impl->core_webview2)
                {
                    struct core_webview2 *cwv = impl_from_cwv2( h->impl->core_webview2 );
                    void *ls = NULL, *ls2 = NULL;
                    struct cwv2_pending_op *op, *next;
                    int replayed = 0;

                    cwv->loader_core = core;
                    cwv->real_loader = h->impl->real_loader;
                    for (op = cwv->pending_head; op; op = next)
                    {
                        next = op->next;
                        switch (op->kind)
                        {
                        case CWV2_OP_SETTING_BOOL:
                        case CWV2_OP_SETTING_UA:
                            if (!ls && *(void ***)core)
                            {
                                HRESULT (WINAPI *get_set)( void *, void ** ) = (*(void ***)core)[3];
                                if (get_set && (ULONG_PTR)get_set < (ULONG_PTR)0x7ffc00000000)
                                {
                                    __TRY
                                    {
                                        if (SUCCEEDED( get_set( core, &ls ) ) && ls)
                                            ((IUnknown *)ls)->lpVtbl->QueryInterface( ls, &IID_ICoreWebView2Settings2_com, &ls2 );
                                    }
                                    __EXCEPT_PAGE_FAULT
                                    {
                                        ERR( "cwv2_replay: core->get_Settings faulted\n" );
                                        ls = NULL;
                                    }
                                    __ENDTRY;
                                }
                            }
                            if (op->kind == CWV2_OP_SETTING_BOOL && ls && *(void ***)ls)
                            {
                                HRESULT (WINAPI *fn)( void *, BOOL ) = (*(void ***)ls)[op->arg_int];
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000) fn( ls, op->arg_bool );
                            }
                            else if (op->kind == CWV2_OP_SETTING_UA && ls2 && *(void ***)ls2)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR ) = (*(void ***)ls2)[22];
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000) fn( ls2, op->str1 ? op->str1 : L"" );
                            }
                            break;
                        case CWV2_OP_ADD_HOST_OBJECT:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, VARIANT * ) = wv2_slot_fn( core, TRUE, 49, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                {
                                    VARIANT v;
                                    IDispatch *disp = NULL;
                                    HRESULT hr_h;
                                    memset( &v, 0, sizeof(v) );
                                    if (op->obj && SUCCEEDED( ((IUnknown *)op->obj)->lpVtbl->QueryInterface( op->obj, &IID_IDispatch, (void **)&disp ) ) && disp)
                                    {
                                        V_VT( &v ) = VT_DISPATCH;
                                        V_DISPATCH( &v ) = disp;
                                    }
                                    else
                                    {
                                        V_VT( &v ) = VT_UNKNOWN;
                                        V_UNKNOWN( &v ) = (IUnknown *)op->obj;
                                        if (op->obj) ((IUnknown *)op->obj)->lpVtbl->AddRef( (IUnknown *)op->obj );
                                    }
                                    hr_h = fn( core, op->str1 ? op->str1 : L"", &v );
                                    if (V_UNKNOWN( &v )) ((IUnknown *)V_UNKNOWN( &v ))->lpVtbl->Release( V_UNKNOWN( &v ) );
                                    ERR( "cwv2_replay AddHostObjectToScript(%s) -> %#lx\n", debugstr_w( op->str1 ), hr_h );
                                }
                            }
                            break;
                        case CWV2_OP_ADD_SCRIPT:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, void * ) = wv2_slot_fn( core, TRUE, 27, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                    fn( core, op->str1 ? op->str1 : L"", NULL );
                            }
                            break;
                        case CWV2_OP_ADD_WRS_FILTER:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, int ) = wv2_slot_fn( core, TRUE, 57, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                    fn( core, op->str1 ? op->str1 : L"", op->arg_int );
                            }
                            break;
                        case CWV2_OP_ADD_EVENT:
                        case CWV2_OP_ADD_EVENT2:
                        case CWV2_OP_ADD_EVENT11:
                        {
                            void *com_obj = core;
                            EventRegistrationToken dummy_tok;
                            if (op->kind == CWV2_OP_ADD_EVENT2)
                                com_obj = cwv2_get_com_iface( cwv, &cwv->loader_core2, &IID_ICoreWebView2_2_com );
                            else if (op->kind == CWV2_OP_ADD_EVENT11)
                                com_obj = cwv2_get_com_iface( cwv, &cwv->loader_core11, &IID_ICoreWebView2_11_com );
                            if (com_obj)
                                cwv2_forward_add_event( cwv, com_obj, op->arg_int, op->obj, &dummy_tok, op->avtbl, op->aiid );
                            break;
                        }
                        case CWV2_OP_DEVTOOLS:
                            if (cwv->real_loader)
                            {
                                HRESULT (WINAPI *fn)( void *, LPCWSTR, LPCWSTR, void * ) = wv2_slot_fn( core, TRUE, 36, -1 );
                                if (fn && (ULONG_PTR)fn < (ULONG_PTR)0x7ffc00000000)
                                    fn( core, op->str1 ? op->str1 : L"", op->str2 ? op->str2 : L"{}", NULL );
                            }
                            break;
                        }
                        if (op->obj) ((IUnknown *)op->obj)->lpVtbl->Release( (IUnknown *)op->obj );
                        free( op->str1 );
                        free( op->str2 );
                        free( op );
                        replayed++;
                    }
                    cwv->pending_head = cwv->pending_tail = NULL;
                    if (ls2) ((IUnknown *)ls2)->lpVtbl->Release( (IUnknown *)ls2 );
                    if (ls) ((IUnknown *)ls)->lpVtbl->Release( (IUnknown *)ls );
                    ERR( "wv2_ctrl_handler_Invoke: replayed %d queued core_webview2 ops\n", replayed );
                }
                wv2_navigate_once( h->impl );
                trigger_core_webview2_initialized( h->impl, &h->impl->IWebView2_iface );
            }
            return S_OK;
        }

        if (SUCCEEDED( wv2_call_get_core( controller, FALSE, 25, 7, &core ) ) && core && !h->impl->loader_core)
        {
            /* Keep the loader object as the forward target; trigger creates the
             * cwv2 shell (static vtbl) as the RCW the app sees (r46 lesson: the
             * loader heap-vtbl object crashes ComWrappers with 0x80131506). */
            h->impl->loader_core = core;
            trigger_core_webview2_initialized( h->impl, &h->impl->IWebView2_iface );
        }
    }
    return S_OK;
}

/* Heap vtbls (per-slot code stores): static .rdata vtbl arrays live in shared
 * builtin pages that get relocated per-page by whichever process maps them
 * first (app vs msedgewebview2 child), so their entries read back as child
 * image-base VAs in the app (r39: ctrl handler dispatch jumped to a stale
 * child VA -> privileged instruction). */
static const void **wv2_ctrl_handler_vtbl_fill( void )
{
    const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
    if (vt)
    {
        vt[0] = wv2h_QueryInterface;
        vt[1] = wv2h_AddRef;
        vt[2] = wv2h_Release;
        vt[3] = wv2_ctrl_handler_Invoke;
    }
    return vt;
}

static HRESULT WINAPI wv2_env_handler_Invoke( void *t, HRESULT hr, void *env )
{
    struct wv2_env_handler *h = t;
    HRESULT (WINAPI *create_ctrl)( void *, HWND, void * );
    struct wv2_ctrl_handler *ch;
    HWND hw;

    ERR( "wv2_env_handler_Invoke hr %#lx env %p real=%d\n", hr, env, h->impl->real_loader );
    if (FAILED( hr ) || !env) return hr;
    if (h->impl->real_loader)
    {
        h->impl->loader_env = env;
        ((IUnknown *)env)->lpVtbl->AddRef( (IUnknown *)env );
    }
    hw = wv2_get_controller_parent();

    if (!h->impl->real_loader)
    {
        /* s430: prefer the synchronous loader export: one call, no handler vtbl hops */
        HMODULE loader = GetModuleHandleW( L"webview2loader.dll" );
        if (!loader) loader = LoadLibraryW( L"webview2loader.dll" );
        void *(WINAPI *p_sync)( HWND, HRESULT * ) = (void *)GetProcAddress( loader, "CreateCoreWebView2ControllerSync" );
        if (p_sync)
        {
            HRESULT hr2 = S_OK;
            void *core;
            ERR( "wv2_env_handler_Invoke: controller parent hwnd %p\n", hw );
            core = p_sync( hw, &hr2 );
            ERR( "wv2_env_handler_Invoke: sync core %p hr %#lx\n", core, hr2 );
            if (SUCCEEDED( hr2 ) && core && !h->impl->core_webview2)
            {
                /* s460: the app's CoreCLR RCW chokes (0x80131506) on the loader
                 * webview object; hand it the builtin cwv2 shell (known-good RCW
                 * layout) and keep the loader object for Navigate forwarding. */
                h->impl->real_loader = FALSE;
                h->impl->loader_core = core;
                trigger_core_webview2_initialized( h->impl, &h->impl->IWebView2_iface );
            }
            {
                SIZE sz;
                xaml_get_layout_metrics( hw, &sz, NULL );
                wv2_sync_child_hwnds( hw, sz.cx, sz.cy );
            }
            return hr2;
        }
    }

    if (!(ch = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ch) ))) return E_OUTOFMEMORY;
    if (!(ch->vtbl = wv2_ctrl_handler_vtbl_fill()))
    {
        HeapFree( GetProcessHeap(), 0, ch );
        return E_OUTOFMEMORY;
    }
    ch->ref = 1;
    ch->impl = h->impl;
    ERR( "wv2_env_handler_Invoke: ch %p vt %p vt3 %p hw %p\n", ch, ch->vtbl,
         ((void **)ch->vtbl)[3], hw );

    if (h->impl->real_loader)
    {
        void *env3 = NULL;
        /* First try ICoreWebView2Environment3::CreateCoreWebView2CompositionController (slot 9) */
        if (SUCCEEDED( ((IUnknown *)env)->lpVtbl->QueryInterface(
                (IUnknown *)env, &IID_ICoreWebView2Environment3_g, &env3 ) ) && env3)
        {
            HRESULT (WINAPI *create_comp_ctrl)( void *, HWND, void * ) =
                (void *)(*(void ***)env3)[9];
            HRESULT hr_comp;
            ERR( "wv2_env_handler_Invoke (real): calling CreateCoreWebView2CompositionController env3=%p slot9=%p hw=%p\n",
                 env3, create_comp_ctrl, hw );
            hr_comp = create_comp_ctrl( env3, hw, ch );
            ERR( "wv2_env_handler_Invoke (real): CreateCoreWebView2CompositionController -> %#lx\n", hr_comp );
            ((IUnknown *)env3)->lpVtbl->Release( (IUnknown *)env3 );
            if (SUCCEEDED( hr_comp )) return hr_comp;
        }
    }

    create_ctrl = (void *)(*(void ***)env)[3];  /* ICoreWebView2Environment::CreateCoreWebView2Controller */
    ERR( "wv2_env_handler_Invoke: env vtbl %p slot3 %p\n", *(void **)env, create_ctrl );
    {
        HRESULT hr_c;
        ERR( "wv2_env_handler_Invoke: calling create_ctrl env=%p hw=%p vis=%d ch=%p\n",
             env, hw, hw ? IsWindowVisible(hw) : -1, ch );
        hr_c = create_ctrl( env, hw, ch );
        ERR( "wv2_env_handler_Invoke: create_ctrl -> %#lx\n", hr_c );
        return hr_c;
    }
}

static const void **wv2_env_handler_vtbl_fill( void )
{
    const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
    if (vt)
    {
        vt[0] = wv2h_QueryInterface;
        vt[1] = wv2h_AddRef;
        vt[2] = wv2h_Release;
        vt[3] = wv2_env_handler_Invoke;
    }
    return vt;
}

static BOOL wv2_folder_has_runtime( const WCHAR *dir )
{
    WCHAR path[1024];
    if (!dir || !*dir) return FALSE;
    lstrcpynW( path, dir, ARRAY_SIZE(path) - 48 );
    lstrcatW( path, L"\\msedgewebview2.exe" );
    if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES) return FALSE;
    lstrcpynW( path, dir, ARRAY_SIZE(path) - 48 );
    lstrcatW( path, L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll" );
    if (GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES) return TRUE;
    lstrcpynW( path, dir, ARRAY_SIZE(path) - 48 );
    lstrcatW( path, L"\\EmbeddedBrowserWebView.dll" );
    return GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES;
}

static int wv2_compare_versions( const WCHAR *a, const WCHAR *b )
{
    unsigned int av[4] = {0}, bv[4] = {0};
    int i;
    swscanf( a, L"%u.%u.%u.%u", &av[0], &av[1], &av[2], &av[3] );
    swscanf( b, L"%u.%u.%u.%u", &bv[0], &bv[1], &bv[2], &bv[3] );
    for (i = 0; i < 4; i++)
    {
        if (av[i] != bv[i]) return av[i] > bv[i] ? 1 : -1;
    }
    return 0;
}

static void wv2_extract_folder_version( const WCHAR *dir, WCHAR *out_ver, size_t out_len )
{
    WCHAR tmp[1024];
    WCHAR *slash;
    size_t len;

    out_ver[0] = 0;
    if (!dir || !*dir) return;
    lstrcpynW( tmp, dir, ARRAY_SIZE(tmp) );
    len = lstrlenW( tmp );
    while (len > 0 && (tmp[len - 1] == '\\' || tmp[len - 1] == '/'))
        tmp[--len] = 0;
    slash = wcsrchr( tmp, '\\' );
    if (!slash) slash = wcsrchr( tmp, '/' );
    slash = slash ? slash + 1 : tmp;
    if (slash[0] >= L'0' && slash[0] <= L'9')
        lstrcpynW( out_ver, slash, out_len );
}

static void wv2_add_runtime_candidate( WCHAR candidates[][1024], WCHAR versions[][64], int *ncand, int max_cand,
                                       const WCHAR *dir, const WCHAR *ver )
{
    WCHAR folder_ver[64] = {0};
    const WCHAR *stored_ver;
    int i;
    if (!dir || !*dir || !wv2_folder_has_runtime( dir )) return;
    wv2_extract_folder_version( dir, folder_ver, ARRAY_SIZE(folder_ver) );
    stored_ver = (ver && *ver && wcscmp( ver, L"9999.0.0.0" )) ? ver : folder_ver;
    for (i = 0; i < *ncand; i++)
    {
        if (!wcsicmp( candidates[i], dir ))
        {
            if (!versions[i][0] && stored_ver[0])
                lstrcpynW( versions[i], stored_ver, 64 );
            return;
        }
    }
    if (*ncand >= max_cand) return;

    /* Insert sorted newest-version-first when version string is known */
    i = *ncand;
    if (ver && *ver)
    {
        for (i = 0; i < *ncand; i++)
        {
            if (!versions[i][0] || wv2_compare_versions( ver, versions[i] ) > 0)
                break;
        }
        if (i < *ncand)
        {
            int j;
            for (j = *ncand; j > i; j--)
            {
                lstrcpynW( candidates[j], candidates[j - 1], 1024 );
                lstrcpynW( versions[j], versions[j - 1], 64 );
            }
        }
    }
    lstrcpynW( candidates[i], dir, 1024 );
    lstrcpynW( versions[i], stored_ver, 64 );
    (*ncand)++;
}

static void wv2_scan_app_base( const WCHAR *base_dir, WCHAR candidates[][1024], WCHAR versions[][64],
                               int *ncand, int max_cand )
{
    WCHAR pattern[1024];
    WIN32_FIND_DATAW fd;
    HANDLE hfind;

    if (!base_dir || !*base_dir) return;
    lstrcpynW( pattern, base_dir, ARRAY_SIZE(pattern) - 4 );
    lstrcatW( pattern, L"\\*" );
    hfind = FindFirstFileW( pattern, &fd );
    if (hfind == INVALID_HANDLE_VALUE) return;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            fd.cFileName[0] >= L'0' && fd.cFileName[0] <= L'9')
        {
            WCHAR full[1024];
            lstrcpynW( full, base_dir, ARRAY_SIZE(full) - 68 );
            lstrcatW( full, L"\\" );
            lstrcatW( full, fd.cFileName );
            wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, full, fd.cFileName );
        }
    } while (FindNextFileW( hfind, &fd ));
    FindClose( hfind );
}

static void wv2_discover_runtimes( WCHAR candidates[][1024], WCHAR versions[][64], int *ncand, int max_cand, const WCHAR *exe_dir )
{
    static const GUID FOLDERID_ProgramFilesX86_g =
        { 0x7c5a40ef, 0xa0fb, 0x4bfc, { 0x87, 0x4a, 0xc0, 0xf2, 0xe0, 0xb9, 0xfa, 0x8e } };
    WCHAR env_browser[1024] = {0}, pf86[1024] = {0}, pf[1024] = {0}, app_dir[1024];
    HMODULE reg_mod, shell_mod;
    typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
    typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
    typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );
    typedef HRESULT (WINAPI *p_SHGetKnownFolderPath_t)( const GUID *, DWORD, HANDLE, PWSTR * );

    *ncand = 0;
    memset( versions, 0, sizeof(WCHAR) * 64 * max_cand );

    if (GetEnvironmentVariableW( L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", env_browser, ARRAY_SIZE(env_browser) ))
        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, env_browser, L"9999.0.0.0" );

    if (ExpandEnvironmentStringsW( L"%ProgramFiles(x86)%", pf86, ARRAY_SIZE(pf86) ) == 0 || wcschr( pf86, L'%' ))
    {
        pf86[0] = 0;
        shell_mod = GetModuleHandleW( L"shell32.dll" );
        if (!shell_mod) shell_mod = LoadLibraryW( L"shell32.dll" );
        if (shell_mod)
        {
            p_SHGetKnownFolderPath_t p_known = (p_SHGetKnownFolderPath_t)GetProcAddress( shell_mod, "SHGetKnownFolderPath" );
            PWSTR p = NULL;
            if (p_known && SUCCEEDED( p_known( &FOLDERID_ProgramFilesX86_g, 0, NULL, &p ) ) && p)
            {
                lstrcpynW( pf86, p, ARRAY_SIZE(pf86) );
                CoTaskMemFree( p );
            }
        }
    }

    reg_mod = GetModuleHandleW( L"kernelbase.dll" );
    if (!reg_mod) reg_mod = LoadLibraryW( L"advapi32.dll" );
    if (reg_mod)
    {
        p_RegOpenKeyExW_t p_open = (p_RegOpenKeyExW_t)GetProcAddress( reg_mod, "RegOpenKeyExW" );
        p_RegQueryValueExW_t p_query = (p_RegQueryValueExW_t)GetProcAddress( reg_mod, "RegQueryValueExW" );
        p_RegCloseKey_t p_close = (p_RegCloseKey_t)GetProcAddress( reg_mod, "RegCloseKey" );
        if (p_open && p_query && p_close)
        {
            static const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
            static const WCHAR *reg_paths[] = {
                L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
                L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
            };
            size_t r, p;
            for (r = 0; r < ARRAY_SIZE(roots); r++)
            {
                for (p = 0; p < ARRAY_SIZE(reg_paths); p++)
                {
                    HKEY hk = NULL;
                    WCHAR pv[64] = {0}, loc[1024] = {0};
                    DWORD type = 0, cb;
                    REGSAM sam = KEY_READ | (p == 1 ? KEY_WOW64_32KEY : 0);
                    if (p_open( roots[r], reg_paths[p], 0, sam, &hk ) != ERROR_SUCCESS) continue;
                    cb = sizeof(pv);
                    if (p_query( hk, L"pv", NULL, &type, (BYTE *)pv, &cb ) != ERROR_SUCCESS || type != REG_SZ)
                        pv[0] = 0;
                    cb = sizeof(loc);
                    if (p_query( hk, L"location", NULL, &type, (BYTE *)loc, &cb ) == ERROR_SUCCESS && type == REG_SZ && loc[0])
                        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, loc, pv );
                    if (pv[0] && pf86[0])
                    {
                        lstrcpynW( app_dir, pf86, ARRAY_SIZE(app_dir) - 96 );
                        lstrcatW( app_dir, L"\\Microsoft\\EdgeWebView\\Application\\" );
                        lstrcatW( app_dir, pv );
                        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, app_dir, pv );
                    }
                    p_close( hk );
                }
            }
        }
    }

    if (pf86[0])
    {
        lstrcpynW( app_dir, pf86, ARRAY_SIZE(app_dir) - 48 );
        lstrcatW( app_dir, L"\\Microsoft\\EdgeWebView\\Application" );
        wv2_scan_app_base( app_dir, candidates, versions, ncand, max_cand );
    }
    if (ExpandEnvironmentStringsW( L"%ProgramFiles%", pf, ARRAY_SIZE(pf) ) > 0 && !wcschr( pf, L'%' ) &&
        (!pf86[0] || wcsicmp( pf, pf86 ) != 0))
    {
        lstrcpynW( app_dir, pf, ARRAY_SIZE(app_dir) - 48 );
        lstrcatW( app_dir, L"\\Microsoft\\EdgeWebView\\Application" );
        wv2_scan_app_base( app_dir, candidates, versions, ncand, max_cand );
    }

    if (exe_dir && *exe_dir)
        wv2_add_runtime_candidate( candidates, versions, ncand, max_cand, exe_dir, L"" );
}

static void wv2_resolve_default_udf( WCHAR *out_udf, size_t out_len )
{
    static const GUID FOLDERID_LocalAppData_g =
        { 0xf1b32785, 0x6fba, 0x4fcf, { 0x9d, 0x55, 0x7b, 0x8e, 0x7f, 0x15, 0x70, 0x91 } };
    typedef HRESULT (WINAPI *p_SHGetKnownFolderPath_t)( const GUID *, DWORD, HANDLE, PWSTR * );
    typedef int (WINAPI *p_SHCreateDirectoryExW_t)( HWND, LPCWSTR, const SECURITY_ATTRIBUTES * );
    typedef LONG (WINAPI *p_GetCurrentPackageFamilyName_t)( UINT32 *, PWSTR );
    HMODULE shell_mod, k32_mod;
    PWSTR local_app_data = NULL;
    WCHAR family[256] = {0};

    out_udf[0] = 0;
    shell_mod = GetModuleHandleW( L"shell32.dll" );
    if (!shell_mod) shell_mod = LoadLibraryW( L"shell32.dll" );
    if (shell_mod)
    {
        p_SHGetKnownFolderPath_t p_known = (p_SHGetKnownFolderPath_t)GetProcAddress( shell_mod, "SHGetKnownFolderPath" );
        p_SHCreateDirectoryExW_t p_mkdir = (p_SHCreateDirectoryExW_t)GetProcAddress( shell_mod, "SHCreateDirectoryExW" );
        if (p_known && SUCCEEDED( p_known( &FOLDERID_LocalAppData_g, 0, NULL, &local_app_data ) ) && local_app_data)
        {
            k32_mod = GetModuleHandleW( L"kernelbase.dll" );
            if (!k32_mod) k32_mod = GetModuleHandleW( L"kernel32.dll" );
            if (k32_mod)
            {
                p_GetCurrentPackageFamilyName_t p_fam =
                    (p_GetCurrentPackageFamilyName_t)GetProcAddress( k32_mod, "GetCurrentPackageFamilyName" );
                UINT32 flen = ARRAY_SIZE(family);
                if (p_fam && p_fam( &flen, family ) == ERROR_SUCCESS && family[0])
                {
                    swprintf( out_udf, out_len, L"%s\\Packages\\%s\\LocalCache\\EBWebView",
                              local_app_data, family );
                    if (GetFileAttributesW( out_udf ) == INVALID_FILE_ATTRIBUTES && p_mkdir)
                        p_mkdir( NULL, out_udf, NULL );
                    CoTaskMemFree( local_app_data );
                    return;
                }
            }
            swprintf( out_udf, out_len, L"%s\\Microsoft\\Edge\\User Data", local_app_data );
            CoTaskMemFree( local_app_data );
            return;
        }
    }
    if (ExpandEnvironmentStringsW( L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data", out_udf, out_len ) == 0 ||
        wcschr( out_udf, L'%' ))
        lstrcpynW( out_udf, L"C:\\Microsoft\\Edge\\User Data", out_len );
}

static const GUID IID_ICoreWebView2EnvironmentOptions_g =
    { 0x2fde08a8, 0x1e9a, 0x4766, { 0x8c, 0x05, 0x95, 0xa9, 0xce, 0xb9, 0xd1, 0xc5 } };

struct wv2_env_options
{
    const void **vtbl;
    LONG ref;
    WCHAR args[1024];
    WCHAR target_version[64];
    HSTRING target_version_hstr;
};

static LPWSTR wv2_cotask_dup( const WCHAR *s )
{
    size_t len = s ? lstrlenW( s ) : 0;
    LPWSTR p = CoTaskMemAlloc( (len + 1) * sizeof(WCHAR) );
    if (p)
    {
        if (len) memcpy( p, s, len * sizeof(WCHAR) );
        p[len] = 0;
    }
    return p;
}

static HRESULT WINAPI wv2_opts_QueryInterface( void *t, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2EnvironmentOptions_g ))
    {
        *out = t;
        InterlockedIncrement( &((struct wv2_env_options *)t)->ref );
        return S_OK;
    }
    TRACE( "wv2_opts_QueryInterface: unknown iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI wv2_opts_AddRef( void *t )
{
    return InterlockedIncrement( &((struct wv2_env_options *)t)->ref );
}

static ULONG WINAPI wv2_opts_Release( void *t )
{
    struct wv2_env_options *o = t;
    ULONG r = InterlockedDecrement( &o->ref );
    if (!r)
    {
        if (o->target_version_hstr) WindowsDeleteString( o->target_version_hstr );
        HeapFree( GetProcessHeap(), 0, (void *)o->vtbl );
        HeapFree( GetProcessHeap(), 0, o );
    }
    return r;
}

static HRESULT WINAPI wv2_opts_get_AdditionalBrowserArguments( void *t, LPWSTR *val )
{
    struct wv2_env_options *o = t;
    if (!val) return E_POINTER;
    *val = wv2_cotask_dup( o->args );
    ERR( "wv2_opts_get_AdditionalBrowserArguments -> %s\n", debugstr_w( o->args ) );
    return *val ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI wv2_opts_put_AdditionalBrowserArguments( void *t, LPCWSTR val )
{
    struct wv2_env_options *o = t;
    lstrcpynW( o->args, val ? val : L"", ARRAY_SIZE(o->args) );
    return S_OK;
}

static HRESULT WINAPI wv2_opts_get_Language( void *t, LPWSTR *val )
{
    if (!val) return E_POINTER;
    *val = NULL;
    return S_OK;
}

static HRESULT WINAPI wv2_opts_put_Language( void *t, LPCWSTR val )
{
    return S_OK;
}

static HRESULT WINAPI wv2_opts_get_TargetCompatibleBrowserVersion( void *t, LPWSTR *val )
{
    struct wv2_env_options *o = t;
    const WCHAR *ver = o->target_version_hstr
        ? WindowsGetStringRawBuffer( o->target_version_hstr, NULL )
        : (o->target_version[0] ? o->target_version : L"156.0.4314.42");
    if (!val) return E_POINTER;
    *val = wv2_cotask_dup( ver );
    return *val ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI wv2_opts_put_TargetCompatibleBrowserVersion( void *t, LPCWSTR val )
{
    struct wv2_env_options *o = t;
    const WCHAR *ver = (val && *val) ? val : L"156.0.4314.42";
    if (o->target_version_hstr)
    {
        WindowsDeleteString( o->target_version_hstr );
        o->target_version_hstr = NULL;
    }
    WindowsCreateString( ver, lstrlenW( ver ), &o->target_version_hstr );
    lstrcpynW( o->target_version, ver, ARRAY_SIZE(o->target_version) );
    return S_OK;
}

static HRESULT WINAPI wv2_opts_get_AllowSSO( void *t, BOOL *val )
{
    if (val) *val = FALSE;
    return S_OK;
}

static HRESULT WINAPI wv2_opts_put_AllowSSO( void *t, BOOL val )
{
    return S_OK;
}

static struct wv2_env_options *wv2_create_env_options( struct xaml_element *impl, const WCHAR *discovered_ver )
{
    struct wv2_env_options *o;
    const void **vt;
    WCHAR env_args[512] = {0};
    WCHAR sf_txt[32] = L"1.00";
    WCHAR sf_arg[64] = {0};
    UINT dpi = 0;
    double sf = 1.0;
    LONG desktop_h = 0;
    SIZE sz;
    HWND hw = impl->parent_hwnd ? impl->parent_hwnd : wv2_get_controller_parent();

    o = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*o) );
    if (!o) return NULL;
    vt = HeapAlloc( GetProcessHeap(), 0, 11 * sizeof(*vt) );
    if (!vt)
    {
        HeapFree( GetProcessHeap(), 0, o );
        return NULL;
    }
    vt[0] = wv2_opts_QueryInterface;
    vt[1] = wv2_opts_AddRef;
    vt[2] = wv2_opts_Release;
    vt[3] = wv2_opts_get_AdditionalBrowserArguments;
    vt[4] = wv2_opts_put_AdditionalBrowserArguments;
    vt[5] = wv2_opts_get_Language;
    vt[6] = wv2_opts_put_Language;
    vt[7] = wv2_opts_get_TargetCompatibleBrowserVersion;
    vt[8] = wv2_opts_put_TargetCompatibleBrowserVersion;
    vt[9] = wv2_opts_get_AllowSSO;
    vt[10] = wv2_opts_put_AllowSSO;
    o->vtbl = vt;
    o->ref = 1;
    wv2_opts_put_TargetCompatibleBrowserVersion( o, discovered_ver );

    GetEnvironmentVariableW( L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", env_args, ARRAY_SIZE(env_args) );
    xaml_get_layout_metrics( hw, &sz, NULL );

    /* Chromium device scale factor, in order of precedence:
     *   1. an explicit --force-device-scale-factor in WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS wins;
     *   2. the real Linux/Wine DPI of the window. Xft.dpi / XSettings / Wayland display scale reach
     *      Wine as HKCU\Control Panel\Desktop\LogPixels, surfaced by GetDpiForWindow()
     *      (96 DPI = 1.00x, 144 = 1.50x, 192 = 2.00x);
     *   3. at the 96 DPI default, a desktop of <= 1024 px means a headless Xvfb gate lane
     *      (1024x768) where the login card only fits above the fold when zoomed out:
     *      desktop height / 2048 clamped to [0.50, 1.00];
     *   4. otherwise no override at all, i.e. the native 1.00x.
     */
    if (wcsstr( env_args, L"--force-device-scale-factor" ))
    {
        lstrcpynW( sf_txt, L"env", ARRAY_SIZE(sf_txt) );
    }
    else
    {
        if (hw && IsWindow( hw )) dpi = GetDpiForWindow( hw );
        if (!dpi) dpi = GetDpiForSystem();
        if (!dpi) dpi = 96;

        if (dpi != 96) sf = (double)dpi / 96.0;
        else
        {
            /* Headless Xvfb gate lanes: a 1024 px desktop (1024x768 screen + Wine virtual desktop
             * 1280x1024), where the WhatsApp login page (1600 px design height) only fits above the
             * fold when zoomed out. A real desktop (2560x1440) keeps the native 1.00x. */
            desktop_h = GetSystemMetrics( SM_CYSCREEN );
            if (desktop_h > 0 && desktop_h <= 1024)
            {
                sf = (double)desktop_h / 2048.0;
                if (sf < 0.5) sf = 0.5;
                if (sf > 1.0) sf = 1.0;
            }
        }
        swprintf( sf_txt, ARRAY_SIZE(sf_txt), L"%.2f", sf );
        if (sf != 1.0) swprintf( sf_arg, ARRAY_SIZE(sf_arg), L"--force-device-scale-factor=%.2f", sf );
    }

    if (env_args[0]) lstrcpynW( o->args, env_args, ARRAY_SIZE(o->args) );
    if (sf_arg[0])
    {
        if (o->args[0]) lstrcatW( o->args, L" " );
        lstrcatW( o->args, sf_arg );
    }
    ERR( "wv2_create_env_options: ver=%s args=%s dpi=%u cy=%d desktop=%d sf=%s\n",
         debugstr_w( o->target_version ), debugstr_w( o->args ), dpi, sz.cy, desktop_h,
         debugstr_w( sf_txt ) );
    return o;
}

static void wv2_start_browser( struct xaml_element *impl )
{
    HRESULT (WINAPI *p_create)( LPCWSTR, LPCWSTR, void *, void * );
    struct wv2_env_handler *eh;
    HMODULE module = NULL;
    WCHAR exe_dir[1024] = {0};
    WCHAR udf[1024] = {0}, default_udf[1024] = {0};
    const WCHAR *browser_folder = NULL, *user_data_folder = NULL;

    if (impl->browser_started) return;
    impl->browser_started = TRUE;

    SetEnvironmentVariableW( L"WINE_HEADLESS_CONSOLE", L"1" );
    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

    if (GetModuleFileNameW( NULL, exe_dir, ARRAY_SIZE(exe_dir) ))
    {
        WCHAR *slash = wcsrchr( exe_dir, '\\' );
        if (slash) *slash = 0;
    }

    /* Optional registry override HKLM\Software\Microsoft\EdgeUpdate\WebView2\LoaderName;
     * otherwise load the standard delegating webview2loader.dll. */
    {
        WCHAR loader_name[256] = {0};
        HKEY hkey = NULL;
        DWORD type = 0, cb = sizeof(loader_name);

        if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\EdgeUpdate\\WebView2",
                           0, KEY_READ, &hkey ) == ERROR_SUCCESS)
        {
            if (RegQueryValueExW( hkey, L"LoaderName", NULL, &type, (BYTE *)loader_name, &cb ) == ERROR_SUCCESS &&
                type == REG_SZ && loader_name[0])
            {
                WCHAR cand_loader[1024] = {0};
                if (wcschr( loader_name, '\\' ) || wcschr( loader_name, '/' ))
                    lstrcpynW( cand_loader, loader_name, ARRAY_SIZE(cand_loader) );
                else if (exe_dir[0])
                    swprintf( cand_loader, ARRAY_SIZE(cand_loader), L"%s\\%s", exe_dir, loader_name );
                if (cand_loader[0] && GetFileAttributesW( cand_loader ) != INVALID_FILE_ATTRIBUTES)
                    module = LoadLibraryW( cand_loader );
                if (!module)
                    module = LoadLibraryW( loader_name );
                ERR( "wv2_start_browser: LoaderName(%s) -> %p\n", debugstr_w( loader_name ), module );
            }
            RegCloseKey( hkey );
        }
    }

    if (!module)
    {
        module = LoadLibraryW( L"webview2loader.dll" );
        ERR( "wv2_start_browser: LoadLibrary(webview2loader.dll) -> %p gle=%lu\n",
             module, GetLastError() );
    }

    if (module && (p_create = (void *)GetProcAddress( module, "CreateCoreWebView2EnvironmentWithOptions" )))
    {
        WCHAR candidates[16][1024];
        WCHAR versions[16][64];
        int ncand = 0, ci;

        wv2_discover_runtimes( candidates, versions, &ncand, ARRAY_SIZE(candidates) - 1, exe_dir );

        if (GetEnvironmentVariableW( L"WEBVIEW2_USER_DATA_FOLDER", udf, ARRAY_SIZE(udf) ) && udf[0])
            user_data_folder = udf;
        else
        {
            wv2_resolve_default_udf( default_udf, ARRAY_SIZE(default_udf) );
            user_data_folder = default_udf;
        }

        if ((eh = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*eh) )))
        {
            eh->vtbl = wv2_env_handler_vtbl_fill();
            if (!eh->vtbl)
            {
                HeapFree( GetProcessHeap(), 0, eh );
                return;
            }
            eh->ref = 1;
            eh->impl = impl;
            impl->real_loader = TRUE;
            for (ci = 0; ci <= ncand; ci++)
            {
                HRESULT hr;
                const WCHAR *chosen_ver = (ci < ncand && versions[ci][0])
                    ? versions[ci]
                    : (ncand > 0 && versions[0][0] ? versions[0] : NULL);
                struct wv2_env_options *opts = wv2_create_env_options( impl, chosen_ver );
                browser_folder = (ci < ncand) ? candidates[ci] : NULL;
                ERR( "wv2_start_browser: try real loader[%d] browser=%s ver=%s udf=%s\n",
                     ci, debugstr_w( browser_folder ), debugstr_w( chosen_ver ), debugstr_w( user_data_folder ) );
                hr = p_create( browser_folder, user_data_folder, opts, eh );
                if (opts) wv2_opts_Release( opts );
                ERR( "wv2_start_browser: real CreateCoreWebView2EnvironmentWithOptions[%d] -> %#lx\n", ci, hr );
                if (SUCCEEDED( hr )) return;
            }
            /* Direct fallback: load EmbeddedBrowserWebView.dll from discovered runtime candidates */
            for (ci = 0; ci < ncand; ci++)
            {
                static const WCHAR *rel_dlls[] = {
                    L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll",
                    L"\\EmbeddedBrowserWebView.dll",
                };
                int ri;
                for (ri = 0; ri < (int)ARRAY_SIZE(rel_dlls); ri++)
                {
                    WCHAR eb_path[1024];
                    HMODULE eb;
                    HRESULT (WINAPI *p_int)( BOOL, int, LPCWSTR, void *, void * );
                    lstrcpynW( eb_path, candidates[ci], ARRAY_SIZE(eb_path) - 48 );
                    lstrcatW( eb_path, rel_dlls[ri] );
                    if (GetFileAttributesW( eb_path ) == INVALID_FILE_ATTRIBUTES) continue;
                    eb = LoadLibraryExW( eb_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH );
                    ERR( "wv2_start_browser: LoadLibraryEx(%s) -> %p gle=%lu\n",
                         debugstr_w( eb_path ), eb, GetLastError() );
                    if (eb && (p_int = (void *)GetProcAddress( eb, "CreateWebViewEnvironmentWithOptionsInternal" )))
                    {
                        const WCHAR *chosen_ver = versions[ci][0] ? versions[ci] : (versions[0][0] ? versions[0] : NULL);
                        struct wv2_env_options *opts = wv2_create_env_options( impl, chosen_ver );
                        HRESULT hr = p_int( TRUE, 0, user_data_folder, opts, eh );
                        if (opts) wv2_opts_Release( opts );
                        ERR( "wv2_start_browser: CreateWebViewEnvironmentWithOptionsInternal -> %#lx\n", hr );
                        if (SUCCEEDED( hr )) return;
                    }
                }
            }
            impl->real_loader = FALSE;
            wv2h_Release( eh );
        }
    }

    if (!(module = LoadLibraryW( L"webview2loader.dll" )))
    {
        ERR( "wv2_start_browser: no webview2loader.dll\n" );
        return;
    }
    if (!(p_create = (void *)GetProcAddress( module, "CreateCoreWebView2EnvironmentWithOptions" )))
    {
        ERR( "wv2_start_browser: no CreateCoreWebView2EnvironmentWithOptions\n" );
        return;
    }
    if (!(eh = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*eh) ))) return;
    if (!(eh->vtbl = wv2_env_handler_vtbl_fill()))
    {
        HeapFree( GetProcessHeap(), 0, eh );
        return;
    }
    eh->ref = 1;
    eh->impl = impl;
    impl->real_loader = FALSE;
    ERR( "wv2_start_browser: creating builtin environment\n" );
    p_create( NULL, NULL, NULL, eh );
}

static HRESULT WINAPI wv2_EnsureCoreWebView2Async( void *iface, void **op )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_EnsureCoreWebView2Async iface %p, op %p\n", iface, op );
    wv2_start_browser( impl );
    trigger_core_webview2_initialized( impl, iface );
    return xaml_async_action_create( (IAsyncAction **)op );
}

struct rt_ctrl_op_handler
{
    const void **vtbl;
    LONG ref;
    struct xaml_element *impl;
    void *iface;
    HWND hw;
    void *ctrl_op;
    struct xaml_async_action *act;
};

static HRESULT WINAPI rt_ctrl_op_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    InterlockedIncrement( &((struct rt_ctrl_op_handler *)iface)->ref );
    return S_OK;
}
static ULONG WINAPI rt_ctrl_op_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct rt_ctrl_op_handler *)iface)->ref );
}
static ULONG WINAPI rt_ctrl_op_Release( void *iface )
{
    struct rt_ctrl_op_handler *h = iface;
    LONG r = InterlockedDecrement( &h->ref );
    if (!r)
    {
        if (h->ctrl_op) IInspectable_Release( (IInspectable *)h->ctrl_op );
        if (h->act) xaml_async_action_Release( &h->act->IAsyncAction_iface );
        HeapFree( GetProcessHeap(), 0, (void *)h->vtbl );
        HeapFree( GetProcessHeap(), 0, h );
    }
    return r;
}
static HRESULT WINAPI rt_ctrl_op_Invoke( void *iface, void *async_op, int status )
{
    struct rt_ctrl_op_handler *h = iface;
    void *rt_ctrl = NULL, *rt_core = NULL;
    HRESULT hr = E_FAIL;

    ERR( "rt_ctrl_op_Invoke async_op=%p status=%d\n", async_op, status );
    if (async_op && status == 1 && *(void ***)async_op)
    {
        HRESULT (WINAPI *get_results)( void *, void ** ) = (*(void ***)async_op)[8];
        if (get_results)
        {
            hr = get_results( async_op, &rt_ctrl );
            ERR( "rt_ctrl_op_Invoke GetResults -> hr %#lx, rt_ctrl %p\n", hr, rt_ctrl );
        }
    }
    if (SUCCEEDED( hr ) && rt_ctrl && *(void ***)rt_ctrl)
    {
        typedef struct { float X, Y, Width, Height; } winrt_rect_t;
        HRESULT (WINAPI *put_vis)( void *, boolean ) = (*(void ***)rt_ctrl)[7];
        HRESULT (WINAPI *put_bounds)( void *, winrt_rect_t * ) = (*(void ***)rt_ctrl)[9];
        HRESULT (WINAPI *get_core)( void *, void ** ) = (*(void ***)rt_ctrl)[14];
        RECT cr = { 0, 0, 1272, 990 };
        winrt_rect_t wr;
        if (h->hw && IsWindow( h->hw )) GetClientRect( h->hw, &cr );
        wr.X = 0.0f; wr.Y = 0.0f;
        wr.Width = (float)(cr.right > 0 ? cr.right : 1272);
        wr.Height = (float)(cr.bottom > 0 ? cr.bottom : 990);
        if (put_bounds) put_bounds( rt_ctrl, &wr );
        if (put_vis) put_vis( rt_ctrl, TRUE );
        wv2_ensure_host_subclass( h->hw );
        wv2_sync_child_hwnds( h->hw, (int)wr.Width, (int)wr.Height );
        if (get_core)
        {
            hr = get_core( rt_ctrl, &rt_core );
            ERR( "rt_ctrl_op_Invoke get_CoreWebView2 -> hr %#lx, rt_core %p\n", hr, rt_core );
        }
        if (SUCCEEDED( hr ) && rt_core && !h->impl->core_webview2)
            h->impl->core_webview2 = rt_core;
        h->impl->loader_controller = rt_ctrl;
    }
    trigger_core_webview2_initialized( h->impl, h->iface );
    xaml_async_action_complete( h->act, SUCCEEDED( hr ) ? S_OK : hr );
    return S_OK;
}

static HRESULT WINAPI wv22_EnsureCoreWebView2Async( void *iface, void *env, void **op )
{
    static const GUID IID_ICoreWebView2Environment_rt =
        { 0xd8cc7831, 0xb783, 0x556b, { 0xb9, 0xce, 0x89, 0x9c, 0x1e, 0x95, 0xd5, 0x85 } };
    struct xaml_element *impl = impl_from_IWebView22( iface );

    ERR( "wv22_EnsureCoreWebView2Async iface %p, env %p, op %p (core=%p)\n",
         iface, env, op, impl->core_webview2 );

    if (!op) return E_POINTER;
    if (impl->core_webview2)
    {
        trigger_core_webview2_initialized( impl, iface );
        return xaml_async_action_create( (IAsyncAction **)op );
    }

    if (env)
    {
        HWND hw = impl->parent_hwnd ? impl->parent_hwnd : wv2_get_controller_parent();
        HSTRING h_cls = NULL;
        IInspectable *winref_fact = NULL;
        void *winref_stat = NULL, *winref = NULL, *rt_env = NULL, *ctrl_op = NULL;
        HRESULT hr = E_FAIL;

        static const WCHAR winref_cls[] = L"Microsoft.Web.WebView2.Core.CoreWebView2ControllerWindowReference";
        if (SUCCEEDED( WindowsCreateString( winref_cls, ARRAY_SIZE(winref_cls) - 1, &h_cls ) ))
        {
            hr = RoGetActivationFactory( h_cls, &IID_IInspectable, (void **)&winref_fact );
            WindowsDeleteString( h_cls );
        }
        if (SUCCEEDED( hr ) && winref_fact)
        {
            ULONG cnt = 0;
            IID *iids = NULL;
            if (SUCCEEDED( IInspectable_GetIids( winref_fact, &cnt, &iids ) ) && iids && cnt > 0)
            {
                static const GUID IID_IActivationFactory_g =
                    { 0x00000035, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
                ULONG idx = (IsEqualGUID( &iids[0], &IID_IActivationFactory_g ) && cnt > 1) ? 1 : 0;
                ERR( "wv22_EnsureCoreWebView2Async: winref_fact cnt=%lu iid[%lu]=%s\n", cnt, idx, debugstr_guid( &iids[idx] ) );
                hr = IInspectable_QueryInterface( winref_fact, &iids[idx], &winref_stat );
                CoTaskMemFree( iids );
            }
            else
                hr = E_NOINTERFACE;
            IInspectable_Release( winref_fact );
        }
        if (SUCCEEDED( hr ) && winref_stat && *(void ***)winref_stat)
        {
            HRESULT (WINAPI *from_hwnd)( void *, UINT64, void ** ) = (*(void ***)winref_stat)[6];
            if (from_hwnd) hr = from_hwnd( winref_stat, (UINT64)(ULONG_PTR)hw, &winref );
            ERR( "wv22_EnsureCoreWebView2Async: from_hwnd(%p) -> hr %#lx winref %p\n", hw, hr, winref );
            IInspectable_Release( (IInspectable *)winref_stat );
        }
        if (SUCCEEDED( hr ) && winref)
        {
            hr = IInspectable_QueryInterface( (IInspectable *)env, &IID_ICoreWebView2Environment_rt, &rt_env );
            ERR( "wv22_EnsureCoreWebView2Async: QI(rt_env) -> hr %#lx rt_env %p\n", hr, rt_env );
            if (SUCCEEDED( hr ) && rt_env && *(void ***)rt_env)
            {
                HRESULT (WINAPI *create_ctrl)( void *, void *, void ** ) = (*(void ***)rt_env)[9];
                if (create_ctrl) hr = create_ctrl( rt_env, winref, &ctrl_op );
                IInspectable_Release( (IInspectable *)rt_env );
            }
            IInspectable_Release( (IInspectable *)winref );
        }
        ERR( "wv22_EnsureCoreWebView2Async: CreateCoreWebView2ControllerAsync(hw=%p) -> hr %#lx ctrl_op %p\n",
             hw, hr, ctrl_op );
        if (SUCCEEDED( hr ) && ctrl_op && *(void ***)ctrl_op)
        {
            struct xaml_async_action *act = NULL;
            struct rt_ctrl_op_handler *ch = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ch) );
            const void **vt = HeapAlloc( GetProcessHeap(), 0, 4 * sizeof(*vt) );
            if (ch && vt && SUCCEEDED( xaml_async_action_create_ex( FALSE, &act, (IAsyncAction **)op ) ))
            {
                HRESULT (WINAPI *put_completed)( void *, void * ) = (*(void ***)ctrl_op)[6];
                vt[0] = rt_ctrl_op_QueryInterface;
                vt[1] = rt_ctrl_op_AddRef;
                vt[2] = rt_ctrl_op_Release;
                vt[3] = rt_ctrl_op_Invoke;
                ch->vtbl = vt;
                ch->ref = 1;
                ch->impl = impl;
                ch->iface = iface;
                ch->hw = hw;
                ch->ctrl_op = ctrl_op;
                ch->act = act;
                xaml_async_action_AddRef( &act->IAsyncAction_iface );
                if (put_completed) put_completed( ctrl_op, ch );
                rt_ctrl_op_Release( ch );
                return S_OK;
            }
            if (vt) HeapFree( GetProcessHeap(), 0, vt );
            if (ch) HeapFree( GetProcessHeap(), 0, ch );
            IInspectable_Release( (IInspectable *)ctrl_op );
        }
    }

    wv2_defer_start_browser( impl );
    trigger_core_webview2_initialized( impl, iface );
    return xaml_async_action_create( (IAsyncAction **)op );
}

static HRESULT WINAPI wv22_EnsureCoreWebView2AsyncWithOptions( void *iface, void *env, void *options, void **op )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    ERR( "wv22_EnsureCoreWebView2AsyncWithOptions iface %p, env %p, options %p, op %p\n", iface, env, options, op );
    trigger_core_webview2_initialized( impl, iface );
    return xaml_async_action_create( (IAsyncAction **)op );
}

/* IV1 (af86e2e0) slot-layout probe: 64 named variadic stubs cycling across
 * [6..255]; slot identity = (idx-6)%64, unique within 6..69. Writes NULL to
 * the first pointer out-param so getters hand back benign objects. */
#define WVP(i) \
    static HRESULT WINAPI wv2_probe##i( void *iface, ... ) \
    { \
        void *a0; \
        va_list args; va_start( args, iface ); \
        a0 = va_arg( args, void * ); \
        va_end( args ); \
        ERR( "WVPROBE slot%u iface %p a0 %p\n", (i) + 6, iface, a0 ); \
        if (a0) *(void **)a0 = NULL; \
        return S_OK; \
    }
WVP(0) WVP(1) WVP(2) WVP(3) WVP(4) WVP(5) WVP(6) WVP(7)
WVP(8) WVP(9) WVP(10) WVP(11) WVP(12) WVP(13) WVP(14) WVP(15)
WVP(16) WVP(17) WVP(18) WVP(19) WVP(20) WVP(21) WVP(22) WVP(23)
WVP(24) WVP(25) WVP(26) WVP(27) WVP(28) WVP(29) WVP(30) WVP(31)
WVP(32) WVP(33) WVP(34) WVP(35) WVP(36) WVP(37) WVP(38) WVP(39)
WVP(40) WVP(41) WVP(42) WVP(43) WVP(44) WVP(45) WVP(46) WVP(47)
WVP(48) WVP(49) WVP(50) WVP(51) WVP(52) WVP(53) WVP(54) WVP(55)
WVP(56) WVP(57) WVP(58) WVP(59) WVP(60) WVP(61) WVP(62) WVP(63)
static HRESULT WINAPI wv2_do_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI wv2_do_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI wv2_do_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    return element_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI wv2_do_GetValue( void *iface, void *dp, IInspectable **out )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    if (!out) return E_POINTER;
    if (dp && dp == wv2_source_dp)
        return wv2_get_Source( &impl->IWebView2_iface, out );
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI wv2_do_SetValue( void *iface, void *dp, IInspectable *value )
{
    struct xaml_element *impl = impl_from_IDependencyObject( iface );
    if (dp && dp == wv2_source_dp)
        return wv2_put_Source( &impl->IWebView2_iface, value );
    return S_OK;
}

static const void *wv2_do_vtbl[64] =
{
    [0] = wv2_do_QueryInterface,
    [1] = wv2_do_AddRef,
    [2] = wv2_do_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 63] = element_generic_slot,
    [6] = wv2_do_GetValue,
    [7] = wv2_do_SetValue,
    [9] = wv2_do_GetValue,
    [10] = wv2_do_SetValue,
};

static const void *wv2_vtbl[256] =
{
    [0] = element_wv2_QueryInterface,         /* 0: QI */
    [1] = element_wv2_AddRef,                 /* 1: AddRef */
    [2] = element_wv2_Release,                /* 2: Release */
    [3] = element_GetIids,                    /* 3: GetIids */
    [4] = element_wv2_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = element_GetTrustLevel,              /* 5: GetTrustLevel */
    [6 ... 255] = element_generic_slot,
    [6] = wv2_get_CoreWebView2,               /* 6: get_CoreWebView2 */
    [7] = wv2_EnsureCoreWebView2Async,        /* 7: EnsureCoreWebView2Async */
    [9] = wv2_get_Source,                     /* 9: get_Source */
    [10] = wv2_put_Source,                    /* 10: put_Source */
    [12] = wv2_get_Source,                    /* 12: get_Source (legacy alias) */
    [13] = wv2_put_Source,                    /* 13: put_Source (legacy alias) */
    [20] = wv2_get_CoreWebView2,              /* 20: get_CoreWebView2 (alias) */
    [27] = wv2_add_CoreWebView2Initialized,
    [28] = wv2_remove_CoreWebView2Initialized,
    [30] = wv2_add_CoreWebView2Initialized,   /* 30: add_CoreWebView2Initialized */
    [31] = wv2_remove_CoreWebView2Initialized,/* 31: remove_CoreWebView2Initialized */
    [33] = wv2_add_CoreWebView2Initialized,
    [34] = wv2_remove_CoreWebView2Initialized,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
};

static const void *wv22_vtbl[256] =
{
    [0] = element_wv22_QueryInterface,        /* 0: QI */
    [1] = element_wv22_AddRef,                /* 1: AddRef */
    [2] = element_wv22_Release,               /* 2: Release */
    [3] = element_GetIids,                    /* 3: GetIids */
    [4] = element_wv22_GetRuntimeClassName,   /* 4: GetRuntimeClassName */
    [5] = element_GetTrustLevel,              /* 5: GetTrustLevel */
    [6 ... 255] = element_generic_slot,
    [6] = wv22_EnsureCoreWebView2Async,   [7] = wv22_EnsureCoreWebView2AsyncWithOptions,   [8] = wv2_probe2,   [9] = wv2_probe3,
    [10] = element_grid_get_RowDefinitions_abi3,  /* IV1 c3c01020 slot10 = RowDefinitions(this, HSTRING, vec**) */
    [11] = wv2_probe5,  [12] = wv2_probe6,  [13] = wv2_probe7,
    [14] = wv2_probe8,  [15] = wv2_probe9,  [16] = wv2_probe10, [17] = wv2_probe11,
    [18] = wv2_probe12, [19] = wv2_probe13, [20] = wv2_probe14, [21] = wv2_probe15,
    [22] = wv2_probe16, [23] = wv2_probe17, [24] = wv2_probe18, [25] = wv2_probe19,
    [26] = wv2_probe20, [27] = wv2_probe21, [28] = wv2_probe22, [29] = wv2_probe23,
    [30] = wv2_probe24, [31] = wv2_probe25, [32] = wv2_probe26, [33] = wv2_probe27,
    [34] = wv2_probe28, [35] = wv2_probe29, [36] = wv2_probe30, [37] = wv2_probe31,
    [38] = wv2_probe32, [39] = wv2_probe33, [40] = wv2_probe34, [41] = wv2_probe35,
    [42] = wv2_probe36, [43] = wv2_probe37, [44] = wv2_probe38, [45] = wv2_probe39,
    [46] = wv2_probe40, [47] = wv2_probe41, [48] = wv2_probe42, [49] = wv2_probe43,
    [50] = wv2_probe44, [51] = wv2_probe45, [52] = wv2_probe46, [53] = wv2_probe47,
    [54] = wv2_probe48, [55] = wv2_probe49, [56] = wv2_probe50, [57] = wv2_probe51,
    [58] = wv2_probe52, [59] = wv2_probe53, [60] = wv2_probe54, [61] = wv2_probe55,
    [62] = wv2_probe56, [63] = wv2_probe57, [64] = wv2_probe58, [65] = wv2_probe59,
    [66] = wv2_probe60, [67] = wv2_probe61, [68] = wv2_probe62, [69] = wv2_probe63,
};

/* WinUI3 IUIElement IV slots: set_Visibility is slot 10, get_Visibility 11.
 * On the shared element/grid vtbl, slot 10 collides with Grid IV2
 * get_RowDefinitions and WebView2 CreateCoreWebView2Controller — serve IUIElement
 * from its own view so the app's projection hits the right slots. */
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
