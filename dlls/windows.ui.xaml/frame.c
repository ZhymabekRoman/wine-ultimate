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

#include "private.h"
#include "weakreference.h"

#include "wine/exception.h"
#include "uwp_private.h"
#include "winui_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

const GUID IID_IReferenceTrackerTarget_WinUI =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
const GUID IID_IReferenceTracker =
    { 0xae927363, 0x74e4, 0x47e7, { 0xab, 0x5e, 0x6e, 0x97, 0xd6, 0xc2, 0x88, 0x5b } };

/* Windows.UI.Xaml.Controls.Frame interface GUIDs */
static const GUID IID_IGridFactory =
    { 0xb16bf561, 0xfc6c, 0x57c6, { 0x8e, 0xbc, 0x0b, 0x06, 0xce, 0x45, 0x13, 0xaa } };
static const GUID IID_IContentControlFactory =
    { 0x3dea958e, 0x5acd, 0x5f80, { 0x89, 0x38, 0x38, 0x63, 0x4f, 0x51, 0x49, 0x3a } };
static const GUID IID_IGridStatics =
    { 0xef9cf81d, 0xa431, 0x50f4, { 0xab, 0xf5, 0x30, 0x23, 0xfe, 0x44, 0x77, 0x04 } };
static const GUID IID_IControl_g =
    { 0xedf04eb1, 0x37d1, 0x471f, { 0x85, 0x70, 0x38, 0x29, 0xee, 0x5b, 0x2b, 0xc6 } };
static const GUID IID_IGrid =
    { 0xc4496219, 0x9014, 0x58a1, { 0xb4, 0xad, 0xc5, 0x04, 0x49, 0x13, 0xa5, 0xbb } };
/* WinUI3 (Microsoft.UI.Xaml.Controls.Grid) interface versions asked by the
 * .NET 8 projection when casting RootVisual children to Grid / app views. */
static const GUID IID_IGrid_WinUI2 =
    { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };
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
static const GUID IID_IUIElementProtected_WinUI3 =
    { 0x8f69b9e9, 0x1f00, 0x5834, { 0x9b, 0xf1, 0xa9, 0x25, 0x7b, 0xed, 0x39, 0xf0 } };
static const GUID IID_IFrameworkElement_WinUI =
    { 0xfe08f13d, 0xdc6a, 0x5495, { 0xad, 0x44, 0xc2, 0xd8, 0xd2, 0x18, 0x63, 0xb0 } };
static const GUID IID_IControl_WinUI =
    { 0x857d6e8a, 0xd45a, 0x5c69, { 0xa9, 0x9c, 0xbf, 0x6a, 0x5c, 0x54, 0xfb, 0x38 } };
static const GUID IID_IContentControl_WinUI =
    { 0x07e81761, 0x11b2, 0x52ae, { 0x8f, 0x8b, 0x4d, 0x53, 0xd2, 0xb5, 0x90, 0x0a } };
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
static const GUID IID_IPanelFactory_WinUI3 =
    { 0xf5e7e21c, 0x4c97, 0x5d20, { 0xbe, 0xe6, 0x3e, 0x4f, 0xc6, 0xab, 0x14, 0xe9 } };
static const GUID IID_IUIElementStatics_WinUI3 =
    { 0xd2921d87, 0x3584, 0x5e22, { 0x8a, 0x3a, 0xc2, 0xc7, 0x8d, 0xab, 0x4f, 0x6e } };
static const GUID IID_ISolidColorBrushFactory_WinUI3 =
    { 0x7b559384, 0x4daa, 0x54f4, { 0x91, 0xef, 0x33, 0xa2, 0x3f, 0xd8, 0x16, 0xca } };

static HRESULT WINAPI scb_factory_CreateInstanceWithColor( void *iface, UINT32 color, void **out )
{
    if (!out) return E_POINTER;
    return element_create_typed( L"Microsoft.UI.Xaml.Media.SolidColorBrush", out );
}

static HRESULT WINAPI xaml_dp_token_QI( IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI xaml_dp_token_AddRef( IInspectable *iface ) { return 2; }
static ULONG WINAPI xaml_dp_token_Release( IInspectable *iface ) { return 1; }
static HRESULT WINAPI xaml_dp_token_GetIids( IInspectable *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI xaml_dp_token_GetName( IInspectable *iface, HSTRING *n ) { return WindowsCreateString( L"Microsoft.UI.Xaml.DependencyProperty", 36, n ); }
static HRESULT WINAPI xaml_dp_token_GetTrust( IInspectable *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static const void *xaml_dp_token_vtbl[] =
{
    xaml_dp_token_QI, xaml_dp_token_AddRef, xaml_dp_token_Release,
    xaml_dp_token_GetIids, xaml_dp_token_GetName, xaml_dp_token_GetTrust,
};
static IInspectable xaml_dp_token_obj = { (const IInspectableVtbl *)xaml_dp_token_vtbl };

static HRESULT WINAPI xaml_uielem_statics_getter( void *iface, void **out )
{
    if (out) *out = &xaml_dp_token_obj;
    return S_OK;
}
static const void *xaml_uielem_statics_vtbl[128] =
{
    [0] = xaml_dp_token_QI,
    [1] = xaml_dp_token_AddRef,
    [2] = xaml_dp_token_Release,
    [3] = xaml_dp_token_GetIids,
    [4] = xaml_dp_token_GetName,
    [5] = xaml_dp_token_GetTrust,
    [6 ... 127] = xaml_uielem_statics_getter,
};
static IInspectable xaml_uielem_statics_obj = { (const IInspectableVtbl *)xaml_uielem_statics_vtbl };

static const void *scb_factory_vtbl[] =
{
    xaml_dp_token_QI, xaml_dp_token_AddRef, xaml_dp_token_Release,
    xaml_dp_token_GetIids, xaml_dp_token_GetName, xaml_dp_token_GetTrust,
    scb_factory_CreateInstanceWithColor,
};
static IInspectable winui_scb_factory_obj = { (const IInspectableVtbl *)scb_factory_vtbl };

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
static const GUID IID_ICanvasFactory =
    { 0x005ac3d1, 0xb031, 0x58ab, { 0x91, 0x8d, 0x03, 0x0f, 0xab, 0xae, 0xaf, 0x87 } };
static const GUID IID_ICanvas =
    { 0x70e05ac4, 0xd38d, 0x4bab, { 0x83, 0x1f, 0x4a, 0x22, 0xef, 0x52, 0xac, 0x86 } };
static const GUID IID_IProgressRing =
    { 0x2670d03f, 0xe28c, 0x5652, { 0xbe, 0xe2, 0xb5, 0x21, 0x2e, 0xbd, 0xf7, 0xff } };
static const GUID IID_IImage_WinUI =
    { 0x495b7402, 0x9af3, 0x4e50, { 0xaa, 0x90, 0x03, 0x38, 0x8f, 0x30, 0x86, 0xd2 } };
static const GUID IID_ISolidColorBrush_WinUI3 =
    { 0xb3865c31, 0x37c8, 0x55c1, { 0x8a, 0x72, 0xd4, 0x1c, 0x67, 0x64, 0x2e, 0x2a } };



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

    if (IsEqualGUID( iid, &IID_IDependencyObject_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ))
    {
        *out = &impl->IDependencyObject_iface;
        IInspectable_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFrame_UWP ) ||
        IsEqualGUID( iid, &IID_INavigate_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElement_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IContentControl_UWP ) ||
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
        IsEqualGUID( iid, &IID_IDataTemplate_UWP ) ||
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

HRESULT WINAPI frame_generic_slot( void *iface, ... )
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
    if (impl->prop_count < (int)ARRAY_SIZE(impl->props))
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

    if (IsEqualGUID( iid, &IID_IUIElementStatics_WinUI3 ))
    {
        *out = &xaml_uielem_statics_obj;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ISolidColorBrushFactory_WinUI3 ))
    {
        *out = &winui_scb_factory_obj;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IFrameFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPageFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IGridFactory ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_UWP2 ) ||
        IsEqualGUID( iid, &IID_IPanelFactory_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgsFactory_WinUI3 ) ||
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
    { NULL },
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
        IsEqualGUID( iid, &IID_IFrame_UWP ) ||
        IsEqualGUID( iid, &IID_INavigate_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ) ||
        IsEqualGUID( iid, &IID_IUIElement_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IContentControl_UWP ) ||
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
        IsEqualGUID( iid, &IID_IDataTemplate_UWP ) ||
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
        IsEqualGUID( iid, &IID_IPageFactory_UWP ))
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


/* Generic XAML control / element (Grid, RowDefinition, ColumnDefinition, ContentControl, Image, WebView2) */
static HRESULT WINAPI xaml_ConnectFromTrackerSource( void *iface, void *source );
static HRESULT WINAPI xaml_AddRefFromTrackerSource( void *iface );
static HRESULT WINAPI xaml_ReleaseFromTrackerSource( void *iface );

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
static inline struct xaml_element *impl_from_IUIElement( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IUIElement_iface );
}
static inline struct xaml_element *impl_from_IPanel( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IPanel_iface );
}
extern struct xaml_element *impl_from_IDependencyObject( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IDependencyObject_iface );
}

HRESULT WINAPI element_QueryInterface( IInspectable *iface, REFIID iid, void **out );
HRESULT WINAPI element_GetRuntimeClassName( IInspectable *iface, HSTRING *name );
ULONG WINAPI element_AddRef( IInspectable *iface );
ULONG WINAPI element_Release( IInspectable *iface );

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

struct xaml_weak_ref
{
    IUnknown IWeakReference_iface;
    LONG ref;
    IInspectable *target;
};

static inline struct xaml_weak_ref *impl_from_weak_ref( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_weak_ref, IWeakReference_iface );
}

static HRESULT WINAPI weak_ref_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IWeakReference ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        IUnknown_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI weak_ref_AddRef( IUnknown *iface )
{
    struct xaml_weak_ref *w = impl_from_weak_ref( iface );
    return InterlockedIncrement( &w->ref );
}

static ULONG WINAPI weak_ref_Release( IUnknown *iface )
{
    struct xaml_weak_ref *w = impl_from_weak_ref( iface );
    ULONG ref = InterlockedDecrement( &w->ref );
    if (!ref) free( w );
    return ref;
}

static HRESULT WINAPI weak_ref_Resolve( IUnknown *iface, REFIID iid, IInspectable **out )
{
    struct xaml_weak_ref *w = impl_from_weak_ref( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!w->target) return S_OK;
    return IInspectable_QueryInterface( w->target, iid, (void **)out );
}

static const void *weak_ref_vtbl[] =
{
    weak_ref_QueryInterface,
    weak_ref_AddRef,
    weak_ref_Release,
    weak_ref_Resolve,
};

static inline struct xaml_element *impl_from_weak_src( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_element, IWeakReferenceSource_iface );
}

static HRESULT WINAPI weak_src_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_weak_src( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}

static ULONG WINAPI weak_src_AddRef( IUnknown *iface )
{
    struct xaml_element *impl = impl_from_weak_src( iface );
    return element_AddRef( &impl->IInspectable_iface );
}

static ULONG WINAPI weak_src_Release( IUnknown *iface )
{
    struct xaml_element *impl = impl_from_weak_src( iface );
    return element_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI weak_src_GetWeakReference( IUnknown *iface, IUnknown **out )
{
    struct xaml_element *impl = impl_from_weak_src( iface );
    if (!out) return E_POINTER;
    if (!impl->weak_ref)
    {
        struct xaml_weak_ref *w = calloc( 1, sizeof(*w) );
        if (!w) { *out = NULL; return E_OUTOFMEMORY; }
        w->IWeakReference_iface.lpVtbl = (const IUnknownVtbl *)weak_ref_vtbl;
        w->ref = 1;
        w->target = impl->outer ? impl->outer : &impl->IInspectable_iface;
        impl->weak_ref = w;
    }
    *out = &impl->weak_ref->IWeakReference_iface;
    IUnknown_AddRef( *out );
    return S_OK;
}

static const void *weak_src_vtbl[] =
{
    weak_src_QueryInterface,
    weak_src_AddRef,
    weak_src_Release,
    weak_src_GetWeakReference,
};

HRESULT WINAPI element_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_element( iface );

    if (!out) return E_POINTER;

    TRACE( "element_QueryInterface(%p vt=%p, %s, class=%s)\n", iface, iface->lpVtbl, debugstr_guid( iid ), debugstr_w( impl->class_name ) );

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &impl->IWeakReferenceSource_iface;
        element_AddRef( iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDependencyObject_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ))
    {
        *out = &impl->IDependencyObject_iface;
        element_AddRef( iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_WinUI ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (impl->is_webview)
    {
        if (IsEqualGUID( iid, &IID_IWebView2_g ) ||
            IsEqualGUID( iid, &IID_IGrid_WinUI2 ))
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
        if (IsEqualGUID( iid, &IID_IDependencyObject_UWP ) ||
            IsEqualGUID( iid, &IID_IDependencyObject_WinUI ))
        {
            *out = &impl->IDependencyObject_iface;
            element_AddRef( iface );
            return S_OK;
        }
    }

    if (IsEqualGUID( iid, &IID_IUIElement_UWP ) ||
        IsEqualGUID( iid, &IID_IUIElement_WinUI ) ||
        IsEqualGUID( iid, &IID_IUIElementProtected_WinUI3 ))
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
        IsEqualGUID( iid, &IID_IGrid_WinUI2 ) ||
        IsEqualGUID( iid, &IID_IView_IV1 ) ||
        IsEqualGUID( iid, &IID_IView_IV2 ) ||
        IsEqualGUID( iid, &IID_ICanvas ) ||
        IsEqualGUID( iid, &IID_IStackPanel ) ||
        IsEqualGUID( iid, &IID_IContentControl_UWP ) ||
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
        IsEqualGUID( iid, &IID_IDependencyObject_UWP ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_WinUI ) ||
        IsEqualGUID( iid, &IID_IUIElementOverrides ) ||
        IsEqualGUID( iid, &IID_IFrameworkElementOverrides ) ||
        IsEqualGUID( iid, &IID_IImage ) ||
        IsEqualGUID( iid, &IID_IRectangle ) ||
        IsEqualGUID( iid, &IID_IShape ) ||
        IsEqualGUID( iid, &IID_IViewbox ) ||
        IsEqualGUID( iid, &IID_IDataTemplate_UWP ) ||
        IsEqualGUID( iid, &IID_IProgressRing ) ||
        IsEqualGUID( iid, &IID_IImage_WinUI ) ||
        IsEqualGUID( iid, &IID_IStyle ) ||
        IsEqualGUID( iid, &IID_IBrush_UWP ) ||
        IsEqualGUID( iid, &IID_ISolidColorBrush_UWP ) ||
        IsEqualGUID( iid, &IID_ISolidColorBrush_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IRoutedEventArgs_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IStyle_UWP ))
    {
        *out = &impl->IInspectable_iface;
        element_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    {
        static const GUID IID_IStoryboard_WinUI3 =
            { 0x04d41bb3, 0x8721, 0x519e, { 0x8e, 0x53, 0xfb, 0x8b, 0x34, 0x92, 0x03, 0x05 } };
        if (IsEqualGUID( iid, &IID_IStoryboard_WinUI3 ))
        {
            *out = &impl->IInspectable_iface;
            element_AddRef( &impl->IInspectable_iface );
            return S_OK;
        }
    }

    if (IsEqualGUID( iid, &IID_IFrameworkElement_UWP ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement_WinUI ))
    {
        *out = &impl->IFrameworkElement_iface;
        element_AddRef( &impl->IInspectable_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_ITextBlock ))
    {
        *out = &impl->ITextBlock_iface;
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

    if (IsEqualGUID( iid, &IID_IComponentConnector_WinUI ) ||
        IsEqualGUID( iid, &IID_IComponentConnector_UWP_g ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    TRACE( "element_QueryInterface %s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

ULONG WINAPI element_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_element( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "element_AddRef iface %p ref %lu\n", iface, ref );
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
    TRACE( "element_GetRuntimeClassName on %p -> %s\n", iface, debugstr_w(str) );
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

static HRESULT WINAPI xaml_ReleaseFromTrackerSource( void *iface )
{
    struct tracker_entry *e = tracker_lookup( iface, 0 );
    if (e && e->idx < ARRAY_SIZE( tracker_map ))
        memset( &tracker_map[e->idx], 0, sizeof(tracker_map[e->idx]) );
    return S_OK;
}

static HRESULT WINAPI element_get_IsLoaded( void *iface, boolean *value )
{
    if (value) *value = TRUE;
    return S_OK;
}

float g_winui_actual_w = 0.0f;
float g_winui_actual_h = 0.0f;

static HRESULT WINAPI element_get_ActualWidth( void *iface, double *value )
{
    SIZE sz = { 1272, 990 };
    if (value)
    {
        if (g_winui_actual_w > 0.0f)
            *value = (double)g_winui_actual_w;
        else
        {
            xaml_get_layout_metrics( xaml_get_window_hwnd(), &sz, NULL );
            *value = (double)(sz.cx > 0 ? sz.cx : 1272);
        }
    }
    return S_OK;
}

static HRESULT WINAPI element_get_ActualHeight( void *iface, double *value )
{
    SIZE sz = { 1272, 990 };
    if (value)
    {
        if (g_winui_actual_h > 0.0f)
            *value = (double)g_winui_actual_h;
        else
        {
            xaml_get_layout_metrics( xaml_get_window_hwnd(), &sz, NULL );
            *value = (double)(sz.cy > 0 ? sz.cy : 990);
        }
    }
    return S_OK;
}

static HRESULT WINAPI element_add_event_generic( void *iface, void *handler, EventRegistrationToken *token )
{
    static LONG next_tok = 10;
    if (token) token->value = InterlockedIncrement( &next_tok );
    return S_OK;
}

static HRESULT WINAPI element_remove_event_generic( void *iface, EventRegistrationToken token )
{
    return S_OK;
}

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
    [6] = element_grid_get_RowDefinitions,
    [7] = element_grid_get_ColumnDefinitions,
    [8] = element_generic_slot,
    [9] = xaml_ConnectFromTrackerSource,
    [10] = element_grid_get_RowDefinitions_abi3,  /* IV2 94ea2b94 WinUI3 view: RowDefinitions(HSTRING, vec**) */
    [11] = element_grid_get_ColumnDefinitions_abi3,
    [12] = element_grid_get_ColumnDefinitions,
    [13] = element_get_ActualWidth, [14] = element_get_ActualHeight, [15] = wv2_probe59,
    [16] = element_get_Content, [17] = wv2_probe61, [18] = wv2_probe62,
    [19] = wv2_probe63,
    [20] = wv2_probe0,  [21] = wv2_probe1,  [22] = wv2_probe2,  [23] = wv2_probe3,
    [24] = wv2_probe4,  [25] = wv2_probe5,  [26] = wv2_probe6,  [27] = wv2_probe7,
    [28] = wv2_probe8,  [29] = wv2_probe9,  [30] = wv2_probe10, [31] = wv2_probe11,
    [32] = wv2_probe12, [33] = wv2_probe13, [34] = wv2_probe14, [35] = wv2_probe15,
    [36] = wv2_probe16, [37] = wv2_probe17, [38] = wv2_probe18, [39] = wv2_probe19,
    [40] = wv2_probe20, [41] = wv2_probe21, [42] = wv2_probe22, [43] = wv2_probe23,
    [44] = wv2_probe24, [45] = wv2_probe25, [46] = wv2_probe26, [47] = wv2_probe27,
    [48] = wv2_probe28, [49] = wv2_probe29, [50] = wv2_probe30, [51] = wv2_probe31,
    [52] = wv2_probe32, [53] = wv2_probe33, [54] = wv2_probe34, [55] = wv2_probe35,
    [56] = wv2_probe36, [57] = element_get_RequestedTheme, [58] = element_put_RequestedTheme, [59] = element_get_IsLoaded,
    [60] = wv2_probe40, [61] = element_add_Loaded, [62] = element_remove_Loaded,
    [63] = element_add_event_generic, [64] = element_remove_event_generic,
    [65] = element_add_event_generic, [66] = element_remove_event_generic,
    [67] = element_add_event_generic, [68] = element_remove_event_generic,
    [69] = element_add_event_generic, [70] = element_remove_event_generic,
    [71] = element_add_event_generic,
    [72] = element_add_Loaded,
    [73] = element_add_event_generic, [74] = element_remove_event_generic, /* ActualThemeChanged */
    [75] = element_add_event_generic, [76] = element_remove_event_generic, /* EffectiveViewportChanged */
    [77] = wv2_probe56,
    [78] = wv2_probe57, [79] = wv2_probe58, [80] = wv2_probe59, [81] = wv2_probe60,
    [82] = wv2_probe61, [83] = wv2_probe62, [84] = wv2_probe63,
    [85] = wv2_probe0,  [86] = wv2_probe1,  [87] = wv2_probe2,  [88] = wv2_probe3,
    [89] = wv2_probe4,  [90] = wv2_probe5,  [91] = wv2_probe6,  [92] = wv2_probe7,
    [93] = wv2_probe8,  [94] = wv2_probe9,  [95] = wv2_probe10, [96] = wv2_probe11,
    [97] = wv2_probe12, [98] = wv2_probe13, [99] = wv2_probe14, [100] = wv2_probe15,
    [101 ... 126] = element_generic_slot,
    [127] = element_get_Content,
    [128] = element_put_Content,
    [129 ... 255] = element_generic_slot,
};

static const void *panel_vtbl[256] =
{
    [0] = element_panel_QueryInterface,
    [1] = element_panel_AddRef,
    [2] = element_panel_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6] = element_panel_get_Children,
    [7] = xaml_AddRefFromTrackerSource,
    [8] = xaml_ReleaseFromTrackerSource,
    [9] = element_panel_get_Children,
    [10 ... 12] = element_generic_slot,
    [13] = element_panel_get_Children,
    [14 ... 74] = element_generic_slot,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
    [77 ... 129] = element_generic_slot,
    [130] = element_get_Content,
    [131] = element_put_Content,
    [132 ... 255] = element_generic_slot,
};

static const void *content_control_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6] = xaml_ConnectFromTrackerSource,
    [7] = xaml_AddRefFromTrackerSource,
    [8] = xaml_ReleaseFromTrackerSource,
    [9] = element_get_Content,
    [10] = element_put_Content,
    [11 ... 16] = element_generic_slot,
    [17] = element_add_Loaded,
    [18] = element_remove_Loaded,
    [19 ... 74] = element_generic_slot,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
    [77 ... 129] = element_generic_slot,
    [130] = element_get_Content,
    [131] = element_put_Content,
    [132 ... 255] = element_generic_slot,
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
    /* CsWinRT IRowDefinition: 6 ActualHeight, 7 get_Height, 8 set_Height,
     * 9 get_MaxHeight, 10 set_MaxHeight, 11 get_MinHeight, 12 set_MinHeight. */
    [6] = rowdef_get_actual,
    [7] = rowdef_get_Height,
    [8] = rowdef_set_Height,
    [9] = rowdef_get_max,
    [10] = rowdef_set_max,
    [11] = rowdef_get_min,
    [12] = rowdef_set_min,
    [13 ... 129] = element_generic_slot,
    [130] = element_get_Content,
    [131] = element_put_Content,
    [132 ... 255] = element_generic_slot,
};

static const void *col_def_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    /* CsWinRT IColumnDefinition: 6 ActualWidth, 7 get_Width, 8 set_Width,
     * 9 get_MaxWidth, 10 set_MaxWidth, 11 get_MinWidth, 12 set_MinWidth. */
    [6] = rowdef_get_actual,
    [7] = rowdef_get_Height,
    [8] = rowdef_set_Height,
    [9] = rowdef_get_max,
    [10] = rowdef_set_max,
    [11] = rowdef_get_min,
    [12] = rowdef_set_min,
    [13 ... 129] = element_generic_slot,
    [130] = element_get_Content,
    [131] = element_put_Content,
    [132 ... 255] = element_generic_slot,
};

static const void *element_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
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
    [21 ... 74] = element_generic_slot,
    [75] = element_add_Loaded,
    [76] = element_remove_Loaded,
    [77 ... 129] = element_generic_slot,
    [130] = element_get_Content,
    [131] = element_put_Content,
    [132 ... 255] = element_generic_slot,
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

static const void *xaml_root_obj;
static const void *xaml_root2_obj;
static const void *xaml_root_native_obj;
static const void *xaml_root_weak_src_obj;
static const void *xaml_root_weak_ref_obj;

static HRESULT WINAPI xaml_root_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IXamlRoot_WinUI3 =
        { 0x60cb215a, 0xad15, 0x520a, { 0x8b, 0x01, 0x44, 0x16, 0x82, 0x4f, 0x04, 0x41 } };
    static const GUID IID_IXamlRoot2_WinUI3 =
        { 0xbdee0f42, 0x71cb, 0x50c5, { 0x82, 0x9b, 0x46, 0x14, 0xd9, 0x8c, 0x57, 0x94 } };
    static const GUID IID_IXamlRootNative =
        { 0xf88a52af, 0x87d6, 0x4f58, { 0x87, 0x58, 0x55, 0xed, 0x42, 0xbd, 0xcc, 0xf3 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXamlRoot_WinUI3 ) ||
        IsEqualGUID( iid, &IID_IXamlRoot_UWP ))
    {
        *out = (void *)&xaml_root_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IXamlRoot2_WinUI3 ))
    {
        *out = (void *)&xaml_root2_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IXamlRootNative ))
    {
        *out = (void *)&xaml_root_native_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource_local ))
    {
        *out = (void *)&xaml_root_weak_src_obj;
        return S_OK;
    }
    ERR( "xaml_root_QueryInterface unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI xaml_root_AddRef( void *iface ) { return 2; }
static ULONG WINAPI xaml_root_Release( void *iface ) { return 1; }
static HRESULT WINAPI xaml_root_weak_ref_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IWeakReference_local ))
    {
        *out = (void *)&xaml_root_weak_ref_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI xaml_root_weak_Resolve( void *iface, REFIID iid, IInspectable **out )
{
    return xaml_root_QueryInterface( (void *)&xaml_root_obj, iid, (void **)out );
}
static const void *xaml_root_weak_ref_vtbl[4] =
{
    [0] = xaml_root_weak_ref_QI,
    [1] = xaml_root_AddRef,
    [2] = xaml_root_Release,
    [3] = xaml_root_weak_Resolve,
};
static const void *xaml_root_weak_ref_obj = xaml_root_weak_ref_vtbl;
static HRESULT WINAPI xaml_root_GetWeakReference( void *iface, void **out )
{
    if (!out) return E_POINTER;
    *out = (void *)&xaml_root_weak_ref_obj;
    return S_OK;
}
static const void *xaml_root_weak_src_vtbl[4] =
{
    [0] = xaml_root_QueryInterface,
    [1] = xaml_root_AddRef,
    [2] = xaml_root_Release,
    [3] = xaml_root_GetWeakReference,
};
static const void *xaml_root_weak_src_obj = xaml_root_weak_src_vtbl;
static HRESULT WINAPI xaml_root_native_get_HostWindow( void *iface, HWND *out )
{
    if (!out) return E_POINTER;
    *out = xaml_get_window_hwnd();
    ERR( "xaml_root_native_get_HostWindow -> %p\n", *out );
    return S_OK;
}
static const void *xaml_root_native_vtbl[4] =
{
    [0] = xaml_root_QueryInterface,
    [1] = xaml_root_AddRef,
    [2] = xaml_root_Release,
    [3] = xaml_root_native_get_HostWindow,
};
static const void *xaml_root_native_obj = xaml_root_native_vtbl;
HRESULT WINAPI xaml_root_GetIids( void *iface, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
HRESULT WINAPI xaml_root_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Microsoft.UI.Xaml.XamlRoot", 26, name );
}
HRESULT WINAPI xaml_root_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI xaml_root_get_Content( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI xaml_root_get_Size( void *iface, Size *out )
{
    SIZE sz = { 1272, 990 };
    if (out)
    {
        if (g_winui_actual_w > 0.0f && g_winui_actual_h > 0.0f)
        {
            out->Width = g_winui_actual_w;
            out->Height = g_winui_actual_h;
        }
        else
        {
            xaml_get_layout_metrics( xaml_get_window_hwnd(), &sz, NULL );
            out->Width = (float)(sz.cx > 0 ? sz.cx : 1272);
            out->Height = (float)(sz.cy > 0 ? sz.cy : 990);
        }
    }
    return S_OK;
}
static HRESULT WINAPI xaml_root_get_RasterizationScale( void *iface, double *out )
{
    if (out) *out = 1.0;
    return S_OK;
}
static HRESULT WINAPI xaml_root_get_IsHostVisible( void *iface, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}
static const void *xaml_root_vtbl[16] =
{
    [0] = xaml_root_QueryInterface,
    [1] = xaml_root_AddRef,
    [2] = xaml_root_Release,
    [3] = xaml_root_GetIids,
    [4] = xaml_root_GetRuntimeClassName,
    [5] = xaml_root_GetTrustLevel,
    [6] = xaml_root_get_Content,
    [7] = xaml_root_get_Size,
    [8] = xaml_root_get_RasterizationScale,
    [9] = xaml_root_get_IsHostVisible,
    [10] = element_add_event_generic,
    [11] = element_remove_event_generic,
    [12 ... 15] = element_generic_slot,
};
static const void *xaml_root_obj = xaml_root_vtbl;

static HRESULT WINAPI xaml_root2_get_ContentIslandEnvironment( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static const void *xaml_root2_vtbl[8] =
{
    [0] = xaml_root_QueryInterface,
    [1] = xaml_root_AddRef,
    [2] = xaml_root_Release,
    [3] = xaml_root_GetIids,
    [4] = xaml_root_GetRuntimeClassName,
    [5] = xaml_root_GetTrustLevel,
    [6] = xaml_root2_get_ContentIslandEnvironment,
    [7] = element_generic_slot,
};
static const void *xaml_root2_obj = xaml_root2_vtbl;

struct uie_vec2 { float x, y; };
struct uie_vec3 { float x, y, z; };

static HRESULT WINAPI uie_get_ActualOffset( void *iface, struct uie_vec3 *out )
{
    if (out) { out->x = 0.0f; out->y = 0.0f; out->z = 0.0f; }
    return S_OK;
}
static HRESULT WINAPI uie_get_ActualSize( void *iface, struct uie_vec2 *out )
{
    SIZE sz = { 1272, 990 };
    if (out)
    {
        if (g_winui_actual_w > 0.0f && g_winui_actual_h > 0.0f)
        {
            out->x = g_winui_actual_w;
            out->y = g_winui_actual_h;
        }
        else
        {
            xaml_get_layout_metrics( xaml_get_window_hwnd(), &sz, NULL );
            out->x = (float)(sz.cx > 0 ? sz.cx : 1272);
            out->y = (float)(sz.cy > 0 ? sz.cy : 990);
        }
    }
    return S_OK;
}
static HRESULT WINAPI uie_get_XamlRoot( void *iface, void **out )
{
    if (!out) return E_POINTER;
    *out = (void *)&xaml_root_obj;
    return S_OK;
}
static HRESULT WINAPI uie_get_RasterizationScale( void *iface, double *out )
{
    if (out) *out = 1.0;
    return S_OK;
}
static const void *general_transform_obj;
static HRESULT WINAPI general_transform_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal )) { *out = NULL; return E_NOINTERFACE; }
    *out = (void *)&general_transform_obj;
    return S_OK;
}
static HRESULT WINAPI general_transform_TransformPoint( void *iface, Point pt, Point *out )
{
    if (out) *out = pt;
    return S_OK;
}
static HRESULT WINAPI general_transform_TryTransform( void *iface, Point in_pt, Point *out_pt, boolean *ret )
{
    if (out_pt) *out_pt = in_pt;
    if (ret) *ret = TRUE;
    return S_OK;
}
static const void *general_transform_vtbl[12] =
{
    [0] = general_transform_QI,
    [1] = xaml_root_AddRef,
    [2] = xaml_root_Release,
    [3] = xaml_root_GetIids,
    [4] = xaml_root_GetRuntimeClassName,
    [5] = xaml_root_GetTrustLevel,
    [6] = general_transform_QI, /* get_Inverse */
    [7] = general_transform_TransformPoint,
    [8] = general_transform_TryTransform,
    [9 ... 11] = element_generic_slot,
};
static const void *general_transform_obj = general_transform_vtbl;

static HRESULT WINAPI uie_TransformToVisual( void *iface, void *visual, void **out )
{
    if (!out) return E_POINTER;
    *out = (void *)&general_transform_obj;
    return S_OK;
}

static HRESULT WINAPI uie_Focus( void *iface, int state, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}

#define DEFINE_UIE_EV_ADD( name, kind ) \
static HRESULT WINAPI uie_add_##name( void *iface, void *handler, EventRegistrationToken *token ) \
{ \
    return winui_ev_add( kind, impl_from_IUIElement( iface ), handler, token ); \
}

DEFINE_UIE_EV_ADD( KeyUp, WINUI_EV_KEY_UP )
DEFINE_UIE_EV_ADD( KeyDown, WINUI_EV_KEY_DOWN )
DEFINE_UIE_EV_ADD( GotFocus, WINUI_EV_GOT_FOCUS )
DEFINE_UIE_EV_ADD( LostFocus, WINUI_EV_LOST_FOCUS )
DEFINE_UIE_EV_ADD( PointerPressed, WINUI_EV_POINTER_PRESSED )
DEFINE_UIE_EV_ADD( PointerMoved, WINUI_EV_POINTER_MOVED )
DEFINE_UIE_EV_ADD( PointerReleased, WINUI_EV_POINTER_RELEASED )
DEFINE_UIE_EV_ADD( PointerEntered, WINUI_EV_POINTER_ENTERED )
DEFINE_UIE_EV_ADD( PointerExited, WINUI_EV_POINTER_EXITED )
DEFINE_UIE_EV_ADD( PointerCaptureLost, WINUI_EV_POINTER_CAPTURE_LOST )
DEFINE_UIE_EV_ADD( PointerWheelChanged, WINUI_EV_POINTER_WHEEL )

static const void *uielement_vtbl[256] =
{
    [0] = uie_QueryInterface,
    [1] = uie_AddRef,
    [2] = uie_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 7] = element_generic_slot,
    [8] = uie_get_ptr,   /* GetValue */
    [9] = element_generic_slot,
    [10] = uie_set_visibility,
    [11] = uie_get_visibility,
    [12] = uie_get_ptr,  /* ReadLocalValue */
    [13] = uie_get_ptr,  /* GetAnimationBaseValue */
    [14 ... 22] = element_generic_slot,
    [23] = uie_get_visibility,
    [24] = uie_set_visibility,
    [25 ... 106] = element_generic_slot,
    [107] = uie_get_ActualOffset,
    [108] = uie_get_ActualSize,
    [109] = uie_get_XamlRoot,
    [110 ... 112] = element_generic_slot,
    [113] = uie_get_RasterizationScale,
    [114 ... 129] = element_generic_slot,
    [130] = uie_add_KeyUp, [131] = winui_ev_remove,
    [132] = uie_add_KeyDown, [133] = winui_ev_remove,
    [134] = uie_add_GotFocus, [135] = winui_ev_remove,
    [136] = uie_add_LostFocus, [137] = winui_ev_remove,
    [138 ... 143] = element_generic_slot,
    [144] = element_add_event_generic, [145] = element_remove_event_generic, /* DragEnter */
    [146] = element_add_event_generic, [147] = element_remove_event_generic, /* DragLeave */
    [148] = element_add_event_generic, [149] = element_remove_event_generic, /* DragOver */
    [150] = element_add_event_generic, [151] = element_remove_event_generic, /* Drop */
    [152] = uie_add_PointerPressed, [153] = winui_ev_remove,
    [154] = uie_add_PointerMoved, [155] = winui_ev_remove,
    [156] = uie_add_PointerReleased, [157] = winui_ev_remove,
    [158] = uie_add_PointerEntered, [159] = winui_ev_remove,
    [160] = uie_add_PointerExited, [161] = winui_ev_remove,
    [162] = uie_add_PointerCaptureLost, [163] = winui_ev_remove,
    [164] = element_add_event_generic, [165] = element_remove_event_generic, /* PointerCanceled */
    [166] = uie_add_PointerWheelChanged, [167] = winui_ev_remove,
    [168 ... 203] = element_generic_slot,
    [204] = element_add_event_generic, [205] = element_remove_event_generic, /* PreviewKeyDown */
    [206] = element_add_event_generic, [207] = element_remove_event_generic, /* PreviewKeyUp */
    [208 ... 216] = element_generic_slot,
    [217] = uie_TransformToVisual,
    [218 ... 225] = element_generic_slot,
    [226] = uie_Focus,
    [227 ... 255] = element_generic_slot,
};

static inline struct xaml_element *impl_from_IFrameworkElement( void *iface )
{
    return CONTAINING_RECORD( (IInspectable *)iface, struct xaml_element, IFrameworkElement_iface );
}
static HRESULT WINAPI fe_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI fe_AddRef( void *iface )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI fe_Release( void *iface )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI fe_GetIids( void *iface, ULONG *count, IID **iids )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_GetIids( &impl->IInspectable_iface, count, iids );
}
static HRESULT WINAPI fe_GetRuntimeClassName( void *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
static HRESULT WINAPI fe_GetTrustLevel( void *iface, TrustLevel *level )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_GetTrustLevel( &impl->IInspectable_iface, level );
}
static HRESULT WINAPI fe_get_Resources( void *iface, void **out )
{
    static IInspectable *g_empty_fe_rd = NULL;
    if (!out) return E_POINTER;
    if (!g_empty_fe_rd && resource_dictionary_factory)
        IActivationFactory_ActivateInstance( resource_dictionary_factory, &g_empty_fe_rd );
    *out = g_empty_fe_rd;
    if (g_empty_fe_rd) IInspectable_AddRef( g_empty_fe_rd );
    return S_OK;
}
static HRESULT WINAPI fe_add_Loaded( void *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_add_Loaded( &impl->IInspectable_iface, handler, token );
}
static HRESULT WINAPI fe_remove_Loaded( void *iface, EventRegistrationToken token )
{
    struct xaml_element *impl = impl_from_IFrameworkElement( iface );
    return element_remove_Loaded( &impl->IInspectable_iface, token );
}
static HRESULT WINAPI fe_add_SizeChanged( void *iface, void *handler, EventRegistrationToken *token )
{
    return winui_ev_add( WINUI_EV_SIZE_CHANGED, impl_from_IFrameworkElement( iface ), handler, token );
}
static HRESULT WINAPI fe_add_EffectiveViewportChanged( void *iface, void *handler, EventRegistrationToken *token )
{
    return winui_ev_add( WINUI_EV_EFFECTIVE_VIEWPORT, impl_from_IFrameworkElement( iface ), handler, token );
}

static const void *framework_element_vtbl[96] =
{
    [0] = fe_QueryInterface,
    [1] = fe_AddRef,
    [2] = fe_Release,
    [3] = fe_GetIids,
    [4] = fe_GetRuntimeClassName,
    [5] = fe_GetTrustLevel,
    [6] = element_generic_slot,
    [7] = fe_get_Resources,
    [8 ... 12] = element_generic_slot,
    [13] = element_get_ActualWidth,
    [14] = element_get_ActualHeight,
    [15 ... 56] = element_generic_slot,
    [57] = element_get_RequestedTheme,
    [58] = element_put_RequestedTheme,
    [59] = element_get_IsLoaded,
    [60] = element_generic_slot,
    [61] = fe_add_Loaded,
    [62] = fe_remove_Loaded,
    [63] = element_add_event_generic, [64] = element_remove_event_generic,
    [65] = element_add_event_generic, [66] = element_remove_event_generic,
    [67] = fe_add_SizeChanged, [68] = winui_ev_remove,
    [69] = element_add_event_generic, [70] = element_remove_event_generic,
    [71] = element_add_event_generic,
    [72] = fe_add_Loaded,
    [73] = element_add_event_generic, [74] = element_remove_event_generic,
    [75] = fe_add_EffectiveViewportChanged, [76] = winui_ev_remove,
    [77 ... 95] = element_generic_slot,
};

static inline struct xaml_element *impl_from_ITextBlock( void *iface )
{
    return CONTAINING_RECORD( (IInspectable *)iface, struct xaml_element, ITextBlock_iface );
}
static HRESULT WINAPI tb_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_element *impl = impl_from_ITextBlock( iface );
    return element_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI tb_AddRef( void *iface )
{
    struct xaml_element *impl = impl_from_ITextBlock( iface );
    return element_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI tb_Release( void *iface )
{
    struct xaml_element *impl = impl_from_ITextBlock( iface );
    return element_Release( &impl->IInspectable_iface );
}
static HRESULT WINAPI tb_GetIids( void *iface, ULONG *count, IID **iids )
{
    struct xaml_element *impl = impl_from_ITextBlock( iface );
    return element_GetIids( &impl->IInspectable_iface, count, iids );
}
static HRESULT WINAPI tb_GetRuntimeClassName( void *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_ITextBlock( iface );
    return element_GetRuntimeClassName( &impl->IInspectable_iface, name );
}
static HRESULT WINAPI tb_GetTrustLevel( void *iface, TrustLevel *level )
{
    struct xaml_element *impl = impl_from_ITextBlock( iface );
    return element_GetTrustLevel( &impl->IInspectable_iface, level );
}

static const void *textblock_vtbl[64] =
{
    [0] = tb_QueryInterface,
    [1] = tb_AddRef,
    [2] = tb_Release,
    [3] = tb_GetIids,
    [4] = tb_GetRuntimeClassName,
    [5] = tb_GetTrustLevel,
    [6 ... 63] = element_generic_slot,
};

static HRESULT element_create( IInspectable *outer, IInspectable **inner, void **instance )
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
    impl->IUIElement_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup_n( uielement_vtbl, 256 );
    impl->IFrameworkElement_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup_n( framework_element_vtbl, 96 );
    impl->ITextBlock_iface.lpVtbl = (const struct IInspectableVtbl *)textblock_vtbl;
    impl->IDependencyObject_iface.lpVtbl = (const struct IInspectableVtbl *)vtbl_dup_n( wv2_do_vtbl, 64 );
    impl->IWeakReferenceSource_iface.lpVtbl = (const IUnknownVtbl *)weak_src_vtbl;
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
    ERR( "rowdef_factory_ActivateInstance -> %p hr %08lx\n", (void *)*instance, hr );
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
    ERR( "rowdef_factory_CreateInstance outer %p -> %p hr %08lx\n", outer, (void *)impl, hr );
    return hr;
}

static HRESULT WINAPI coldef_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    HRESULT hr = element_create_typed( L"Microsoft.UI.Xaml.Controls.ColumnDefinition", (void **)instance );
    ERR( "coldef_factory_ActivateInstance -> %p hr %08lx\n", (void *)*instance, hr );
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
    ERR( "coldef_factory_CreateInstance outer %p -> %p hr %08lx\n", outer, (void *)impl, hr );
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
    { NULL },
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
    { NULL },
};
IActivationFactory *row_def_factory = &row_def_factory_obj.IActivationFactory_iface;

static struct frame_factory col_def_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)col_def_factory_vtbl},
    {(const struct IInspectableVtbl *)col_def_composable_vtbl},
    1,
    { NULL },
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
    if (IsEqualGUID( iid, &IID_ICanvasStatics_UWP ))
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
    array[0] = IID_ICanvasStatics_UWP;
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
    { NULL },
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
    { NULL },
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
    { NULL },
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
    { NULL },
};
IActivationFactory *coll_factory = &coll_factory_obj.IActivationFactory_iface;

