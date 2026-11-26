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
#include "frame_private.h"

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
const GUID IID_IControl_g =
    { 0xedf04eb1, 0x37d1, 0x471f, { 0x85, 0x70, 0x38, 0x29, 0xee, 0x5b, 0x2b, 0xc6 } };
const GUID IID_IGrid =
    { 0xc4496219, 0x9014, 0x58a1, { 0xb4, 0xad, 0xc5, 0x04, 0x49, 0x13, 0xa5, 0xbb } };
const GUID IID_IPanel =
    { 0x27a1b418, 0x56f3, 0x525e, { 0xb8, 0x83, 0xce, 0xfe, 0xd9, 0x05, 0xee, 0xd3 } };
const GUID IID_IRowDefinition =
    { 0xfe870f2f, 0x89ef, 0x5dac, { 0x9f, 0x33, 0x96, 0x8d, 0x0d, 0xc5, 0x77, 0xc3 } };
static const GUID IID_IRowDefinitionStatics =
    { 0x7317c7c5, 0xfd6f, 0x58d9, { 0x9b, 0x53, 0x49, 0x78, 0xdb, 0xac, 0xde, 0x23 } };
const GUID IID_IColumnDefinition =
    { 0x454cea14, 0x87ec, 0x5890, { 0xbb, 0x62, 0xf1, 0xd8, 0x2a, 0x94, 0x75, 0x8e } };
static const GUID IID_IColumnDefinitionStatics =
    { 0xaba4b755, 0xca63, 0x5ed7, { 0xae, 0x31, 0x45, 0xf9, 0xf9, 0x19, 0x20, 0xb0 } };
const GUID IID_IReferenceTrackerTarget =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
const GUID IID_IWeakReferenceSource =
    { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_IWeakReference =
    { 0x00000037, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_IUIElement_WinUI =
    { 0xc3c01020, 0x320c, 0x5cf6, { 0x9d, 0x24, 0xd3, 0x96, 0xbb, 0xfa, 0x4d, 0x8b } };
const GUID IID_IFrameworkElement_WinUI =
    { 0xfe08f13d, 0xdc6a, 0x5495, { 0xad, 0x44, 0xc2, 0xd8, 0xd2, 0x18, 0x63, 0xb0 } };
const GUID IID_IControl_WinUI =
    { 0x857d6e8a, 0xd45a, 0x5c69, { 0xa9, 0x9c, 0xbf, 0x6a, 0x5c, 0x54, 0xfb, 0x38 } };
const GUID IID_IContentControl_WinUI =
    { 0x07e81761, 0x11b2, 0x52ae, { 0x8f, 0x8b, 0x4d, 0x53, 0xd2, 0xb5, 0x90, 0x0a } };
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
const GUID IID_IUserControl =
    { 0xab8a0a51, 0x44cf, 0x5337, { 0xad, 0x77, 0x8e, 0xda, 0x17, 0x95, 0xe9, 0xf6 } };
const GUID IID_IBorder =
    { 0x1ca13b47, 0xff5c, 0x5abc, { 0xa4, 0x11, 0xa1, 0x77, 0xdf, 0x94, 0x82, 0xa9 } };
const GUID IID_IStackPanel =
    { 0x493ab00b, 0x3a6a, 0x5e4a, { 0x94, 0x52, 0x40, 0x7c, 0xd5, 0x19, 0x74, 0x06 } };
static const GUID IID_IButtonFactory =
    { 0xfe393422, 0xd91c, 0x57b1, { 0x9a, 0x9c, 0x2c, 0x7e, 0x3f, 0x41, 0xf7, 0x7c } };
const GUID IID_IButton =
    { 0x216c183d, 0xd07a, 0x5aa5, { 0xb8, 0xa4, 0x03, 0x00, 0xa2, 0x68, 0x3e, 0x87 } };
const GUID IID_IButtonBase =
    { 0x65714269, 0x2473, 0x5327, { 0xa6, 0x52, 0x0e, 0xa6, 0xbc, 0xe7, 0xf4, 0x03 } };
const GUID IID_IStyle =
    { 0x65e1d164, 0x572f, 0x5b0e, { 0xa8, 0x0f, 0x9c, 0x02, 0x44, 0x1f, 0xac, 0x49 } };
const GUID IID_ICanvas =
    { 0x70e05ac4, 0xd38d, 0x4bab, { 0x83, 0x1f, 0x4a, 0x22, 0xef, 0x52, 0xac, 0x86 } };
const GUID IID_IProgressRing =
    { 0x2670d03f, 0xe28c, 0x5652, { 0xbe, 0xe2, 0xb5, 0x21, 0x2e, 0xbd, 0xf7, 0xff } };
const GUID IID_IImage_WinUI =
    { 0x495b7402, 0x9af3, 0x4e50, { 0xaa, 0x90, 0x03, 0x38, 0x8f, 0x30, 0x86, 0xd2 } };



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
    TRACE( "frame generic slot called on %p\n", iface );
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
IDependencyProperty *wv2_source_dp;

static inline struct frame_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct frame_factory, IActivationFactory_iface );
}

static inline struct frame_factory *impl_from_IComposableFactory( void *iface )
{
    return CONTAINING_RECORD( iface, struct frame_factory, IComposableFactory_iface );
}

static inline struct frame_factory *impl_from_IStatics( void *iface )
{
    return CONTAINING_RECORD( iface, struct frame_factory, IStatics_iface );
}

HRESULT WINAPI statics_factory_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct frame_factory *impl = impl_from_IStatics( iface );
    return frame_factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
ULONG WINAPI statics_factory_AddRef( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IStatics( iface );
    return frame_factory_AddRef( &impl->IActivationFactory_iface );
}
ULONG WINAPI statics_factory_Release( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IStatics( iface );
    return frame_factory_Release( &impl->IActivationFactory_iface );
}

HRESULT WINAPI composable_factory_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct frame_factory *impl = impl_from_IComposableFactory( iface );
    return frame_factory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}
ULONG WINAPI composable_factory_AddRef( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IComposableFactory( iface );
    return frame_factory_AddRef( &impl->IActivationFactory_iface );
}
ULONG WINAPI composable_factory_Release( IInspectable *iface )
{
    struct frame_factory *impl = impl_from_IComposableFactory( iface );
    return frame_factory_Release( &impl->IActivationFactory_iface );
}

HRESULT WINAPI frame_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
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

ULONG WINAPI frame_factory_AddRef( IActivationFactory *iface )
{
    struct frame_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

ULONG WINAPI frame_factory_Release( IActivationFactory *iface )
{
    struct frame_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

HRESULT WINAPI frame_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

HRESULT WINAPI frame_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Controls.Frame", 30, name );
}

HRESULT WINAPI frame_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
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
