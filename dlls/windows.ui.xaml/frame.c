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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

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
static const GUID IID_IReferenceTracker =
    { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
static const GUID IID_IReferenceTrackerTarget =
    { 0x5c13e51c, 0x4f32, 0x4726, { 0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0 } };
static const GUID IID_IWeakReferenceSource =
    { 0x00000038, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
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

struct xaml_frame
{
    IInspectable IInspectable_iface;
    LONG ref;
    IInspectable *outer;
    IInspectable *content;
    IDCompositionVisual *visual;
};

static inline struct xaml_frame *impl_from_frame( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_frame, IInspectable_iface );
}

static HRESULT WINAPI frame_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_frame *impl = impl_from_frame( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IFrame ) ||
        IsEqualGUID( iid, &IID_INavigate ) ||
        IsEqualGUID( iid, &IID_IDependencyObject_g ) ||
        IsEqualGUID( iid, &IID_IUIElement_g ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement ) ||
        IsEqualGUID( iid, &IID_IContentControl ) ||
        IsEqualGUID( iid, &IID_IControl_g ) ||
        IsEqualGUID( iid, &IID_IGrid ) ||
        IsEqualGUID( iid, &IID_IPanel ) ||
        IsEqualGUID( iid, &IID_IRowDefinition ) ||
        IsEqualGUID( iid, &IID_IColumnDefinition ))
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

static HRESULT WINAPI frame_generic_slot( void *iface, ... )
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
    frame_stub_ok,                /* 23: Navigate */
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

static HRESULT frame_create( IInspectable *outer, IInspectable **inner, void **instance )
{
    struct xaml_frame *impl;

    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)frame_vtbl;
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
};

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
        IsEqualGUID( iid, &IID_IUIElement_g ) ||
        IsEqualGUID( iid, &IID_IFrameworkElement ) ||
        IsEqualGUID( iid, &IID_IContentControl ) ||
        IsEqualGUID( iid, &IID_IControl_g ) ||
        IsEqualGUID( iid, &IID_IGrid ) ||
        IsEqualGUID( iid, &IID_IPanel ) ||
        IsEqualGUID( iid, &IID_IRowDefinition ) ||
        IsEqualGUID( iid, &IID_IColumnDefinition ))
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
};

static inline struct xaml_collection *impl_from_collection( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_collection, IInspectable_iface );
}

static HRESULT WINAPI collection_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct xaml_collection *impl = impl_from_collection( iface );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    *out = &impl->IInspectable_iface;
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI collection_AddRef( IInspectable *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI collection_Release( IInspectable *iface )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        unsigned int i;
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
    return WindowsCreateString( L"Windows.Foundation.Collections.IVector`1", 39, name );
}

static HRESULT WINAPI collection_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

struct xaml_iterator
{
    IInspectable IInspectable_iface;
    LONG ref;
    struct xaml_collection *coll;
    unsigned int index;
};

static HRESULT WINAPI iterator_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
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
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI iterator_Release( IInspectable *iface )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->coll) IInspectable_Release( &impl->coll->IInspectable_iface );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI iterator_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI iterator_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Windows.Foundation.Collections.IIterator`1", 41, name ); }
static HRESULT WINAPI iterator_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI iterator_get_Current( void *iface, void **current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    if (!current) return E_POINTER;
    if (impl->coll && impl->index < impl->coll->count)
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
    if (has_current) *has_current = (impl->coll && impl->index < impl->coll->count);
    return S_OK;
}

static HRESULT WINAPI iterator_MoveNext( void *iface, boolean *has_current )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    if (impl->coll && impl->index < impl->coll->count) impl->index++;
    if (has_current) *has_current = (impl->coll && impl->index < impl->coll->count);
    return S_OK;
}

static HRESULT WINAPI iterator_GetMany( void *iface, unsigned int count, void **items, unsigned int *actual )
{
    struct xaml_iterator *impl = (struct xaml_iterator *)iface;
    unsigned int n = 0;
    if (impl->coll && impl->index < impl->coll->count)
    {
        n = min( count, impl->coll->count - impl->index );
        memcpy( items, &impl->coll->items[impl->index], n * sizeof(*impl->coll->items) );
        impl->index += n;
    }
    if (actual) *actual = n;
    return S_OK;
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
    [6] = iterator_get_Current,            /* 6: get_Current */
    [7] = iterator_get_HasCurrent,         /* 7: get_HasCurrent */
    [8] = iterator_MoveNext,               /* 8: MoveNext */
    [9] = iterator_GetMany,                /* 9: GetMany */
};

static HRESULT WINAPI collection_GetAt( void *iface, ULONG_PTR index, void **item )
{
    struct xaml_collection *impl = impl_from_collection( iface );
    ERR( "collection_GetAt iface %p, index %lu (0x%lx), item %p, count %u\n", iface, index, index, item, impl->count );
    if (!item || index > 0x10000)
    {
        void **first = (void **)index;
        struct xaml_iterator *it;
        if (!first) first = item;
        if (!first) return E_POINTER;
        if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
        it->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)iterator_vtbl;
        it->ref = 1;
        it->coll = impl;
        it->index = 0;
        IInspectable_AddRef( &impl->IInspectable_iface );
        *first = &it->IInspectable_iface;
        ERR( "IIterable::First created iterator %p for coll %p at %p\n", *first, impl, first );
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

static const void *collection_vtbl[256] =
{
    [0] = collection_QueryInterface,         /* 0: QI */
    [1] = collection_AddRef,                 /* 1: AddRef */
    [2] = collection_Release,                /* 2: Release */
    [3] = collection_GetIids,                /* 3: GetIids */
    [4] = collection_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = collection_GetTrustLevel,          /* 5: GetTrustLevel */
    [6 ... 255] = frame_generic_slot,
    [6] = collection_GetAt,                  /* 6: GetAt */
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
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)collection_vtbl;
    impl->ref = 1;
    *instance = &impl->IInspectable_iface;
    return S_OK;
}

/* Generic XAML control / element (Grid, RowDefinition, ColumnDefinition, ContentControl, Image, WebView2) */
static const GUID IID_IWebView2_g =
    { 0x2b2c76c2, 0x997c, 0x5069, { 0xa8, 0xf0, 0x9b, 0x84, 0xcd, 0x7e, 0x62, 0x4b } };
static const GUID IID_IWebView22_g =
    { 0x560c5eed, 0x3e7a, 0x51e4, { 0xb1, 0x4f, 0x10, 0x7b, 0xa0, 0x2b, 0x89, 0xbe } };

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
    LONG ref;
    IInspectable *outer;
    IDCompositionVisual *visual;
    IInspectable *row_defs;
    IInspectable *col_defs;
    IInspectable *children;
    IInspectable *content;
    IInspectable *core_webview2;
    IInspectable *init_handler;
    BOOL init_triggered;
    BOOL is_webview;
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

    ERR( "element_QueryInterface(%p, %s, class=%s)\n", iface, debugstr_guid( iid ), debugstr_w( impl->class_name ) );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ) ||
        IsEqualGUID( iid, &IID_IWeakReferenceSource ) ||
        IsEqualGUID( iid, &IID_IMarshal ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (impl->is_webview)
    {
        if (IsEqualGUID( iid, &IID_IWebView2_g ))
        {
            *out = &impl->IWebView2_iface;
            element_AddRef( iface );
            return S_OK;
        }
        if (IsEqualGUID( iid, &IID_IWebView22_g ))
        {
            *out = &impl->IWebView22_iface;
            element_AddRef( iface );
            return S_OK;
        }
    }

    if (IsEqualGUID( iid, &IID_IGrid ) ||
        IsEqualGUID( iid, &IID_IPanel ) ||
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
        IsEqualGUID( iid, &IID_IUIElement_g ) ||
        IsEqualGUID( iid, &IID_IUIElement_WinUI ) ||
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
        IsEqualGUID( iid, &IID_IStyle ))
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

    ERR( "element_QueryInterface %s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI element_AddRef( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_element( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI element_Release( IInspectable *iface )
{
    struct xaml_element *impl = impl_from_element( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->visual) IDCompositionVisual_Release( impl->visual );
        if (impl->row_defs) IInspectable_Release( impl->row_defs );
        if (impl->col_defs) IInspectable_Release( impl->col_defs );
        if (impl->children) IInspectable_Release( impl->children );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI element_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI element_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    struct xaml_element *impl = impl_from_element( iface );
    const WCHAR *str = impl->class_name ? impl->class_name : L"Microsoft.UI.Xaml.FrameworkElement";
    ERR( "element_GetRuntimeClassName on %p -> %s\n", iface, debugstr_w(str) );
    return WindowsCreateString( str, wcslen(str), name );
}

static HRESULT WINAPI element_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI element_generic_slot( void *iface, ... )
{
    return S_OK;
}

static HRESULT WINAPI element_add_Loaded( void *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "element_add_Loaded iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    return S_OK;
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
    TRACE( "element_grid_get_RowDefinitions iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    if (!impl->row_defs) collection_create( (void **)&impl->row_defs );
    *out = impl->row_defs;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI element_grid_get_ColumnDefinitions( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_element( iface );
    TRACE( "element_grid_get_ColumnDefinitions iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    if (!impl->col_defs) collection_create( (void **)&impl->col_defs );
    *out = impl->col_defs;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI element_panel_get_Children( void *iface, void **out )
{
    struct xaml_element *impl = impl_from_element( iface );
    TRACE( "element_panel_get_Children iface %p, out %p\n", iface, out );
    if (!out) return E_POINTER;
    if (!impl->children) collection_create( (void **)&impl->children );
    *out = impl->children;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
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
    [6] = element_grid_get_RowDefinitions,
    [7] = element_grid_get_ColumnDefinitions,
    [12] = element_grid_get_ColumnDefinitions,
    [19] = element_grid_get_RowDefinitions,
    [72] = element_add_Loaded,
    [73] = element_remove_Loaded,
};

static const void *panel_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
    [6] = element_panel_get_Children,
    [10] = element_panel_get_Children,
    [72] = element_add_Loaded,
    [73] = element_remove_Loaded,
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
    [6] = element_get_Content,
    [7] = element_put_Content,
    [14] = element_add_Loaded,
    [15] = element_remove_Loaded,
    [72] = element_add_Loaded,
    [73] = element_remove_Loaded,
};

static const void *row_def_vtbl[256] =
{
    [0] = element_QueryInterface,
    [1] = element_AddRef,
    [2] = element_Release,
    [3] = element_GetIids,
    [4] = element_GetRuntimeClassName,
    [5] = element_GetTrustLevel,
    [6 ... 255] = element_generic_slot,
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
    [72] = element_add_Loaded,
    [73] = element_remove_Loaded,
};

struct xaml_async_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
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
        IAsyncActionCompletedHandler_Invoke( impl->handler, iface, 1 /* Completed */ );
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
static HRESULT WINAPI xaml_async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status ) { if (status) *status = 1 /* Completed */; return S_OK; }
static HRESULT WINAPI xaml_async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *code ) { if (code) *code = S_OK; return S_OK; }
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

static HRESULT xaml_async_action_create( IAsyncAction **out )
{
    struct xaml_async_action *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncAction_iface.lpVtbl = &xaml_async_action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &xaml_async_info_vtbl;
    impl->ref = 1;
    *out = &impl->IAsyncAction_iface;
    return S_OK;
}

struct core_webview2
{
    IInspectable IInspectable_iface;
    IInspectable ICoreWebView2_2_iface;
    LONG ref;
    HSTRING source_uri;
};

static const GUID IID_ICoreWebView2 =
    { 0x3a3f559a, 0xe5e9, 0x5338, { 0xbb, 0x67, 0x4e, 0xb0, 0x50, 0x4a, 0x4f, 0x14 } };
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
    LONG ref;
};

static HRESULT WINAPI cwv2_settings_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings2 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings3 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings4 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings5 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings6 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings7 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings8 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings9 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2Settings_Manual ))
    {
        *out = iface;
        IInspectable_AddRef( iface );
        return S_OK;
    }
    ERR( "cwv2_settings_QueryInterface: unknown IID %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cwv2_settings_AddRef( IInspectable *iface )
{
    struct cwv2_settings *impl = (struct cwv2_settings *)iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cwv2_settings_Release( IInspectable *iface )
{
    struct cwv2_settings *impl = (struct cwv2_settings *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI cwv2_settings_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cwv2_settings_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2Settings", 47, name ); }
static HRESULT WINAPI cwv2_settings_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static const void *cwv2_settings_vtbl[256] =
{
    [0] = cwv2_settings_QueryInterface,
    [1] = cwv2_settings_AddRef,
    [2] = cwv2_settings_Release,
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

static HRESULT WINAPI cwv2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2 ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreWebView2_2 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_3 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_4 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_5 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_6 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_7 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_8 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_9 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_10 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_11 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_12 ) ||
        IsEqualGUID( iid, &IID_ICoreWebView2_13 ))
    {
        *out = &impl->ICoreWebView2_2_iface;
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
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI cwv2_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ if (count) *count = 0; if (iids) *iids = NULL; return S_OK; }
static HRESULT WINAPI cwv2_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ return WindowsCreateString( L"Microsoft.Web.WebView2.Core.CoreWebView2", 39, name ); }
static HRESULT WINAPI cwv2_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI cwv2_2_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    return cwv2_QueryInterface( &impl->IInspectable_iface, iid, out );
}
static ULONG WINAPI cwv2_2_AddRef( IInspectable *iface )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    return cwv2_AddRef( &impl->IInspectable_iface );
}
static ULONG WINAPI cwv2_2_Release( IInspectable *iface )
{
    struct core_webview2 *impl = impl_from_cwv2_2( iface );
    return cwv2_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI cwv2_get_Settings( void *iface, void **settings )
{
    struct cwv2_settings *s;
    if (!settings) return E_POINTER;
    if (!(s = calloc( 1, sizeof(*s) ))) return E_OUTOFMEMORY;
    s->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)cwv2_settings_vtbl;
    s->ref = 1;
    *settings = &s->IInspectable_iface;
    return S_OK;
}

static HRESULT WINAPI cwv2_Navigate( void *iface, HSTRING uri )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    ERR( "=== CoreWebView2::Navigate: %s ===\n", debugstr_hstring( uri ) );
    if (impl->source_uri) WindowsDeleteString( impl->source_uri );
    WindowsDuplicateString( uri, &impl->source_uri );
    return S_OK;
}
static HRESULT WINAPI cwv2_get_Source( void *iface, HSTRING *uri )
{
    struct core_webview2 *impl = impl_from_cwv2( iface );
    if (!uri) return E_POINTER;
    if (impl->source_uri) return WindowsDuplicateString( impl->source_uri, uri );
    return WindowsCreateString( L"https://web.whatsapp.com", 24, uri );
}

static HRESULT WINAPI cwv2_2_get_Environment( void *iface, void **env )
{
    struct cwv2_env *e;
    if (!env) return E_POINTER;
    if (!(e = calloc( 1, sizeof(*e) ))) return E_OUTOFMEMORY;
    e->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)cwv2_env_vtbl;
    e->ref = 1;
    *env = &e->IInspectable_iface;
    return S_OK;
}

static const void *cwv2_vtbl[256] =
{
    [0] = cwv2_QueryInterface,         /* 0: QI */
    [1] = cwv2_AddRef,                 /* 1: AddRef */
    [2] = cwv2_Release,                /* 2: Release */
    [3] = cwv2_GetIids,                /* 3: GetIids */
    [4] = cwv2_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = cwv2_GetTrustLevel,          /* 5: GetTrustLevel */
    [6 ... 255] = element_generic_slot,
    [6] = cwv2_get_Settings,           /* 6: get_Settings */
    [7] = cwv2_get_Source,             /* 7: get_Source */
    [45] = cwv2_Navigate,              /* 45: Navigate */
};

static const void *cwv2_2_vtbl[256] =
{
    [0] = cwv2_2_QueryInterface,       /* 0: QI */
    [1] = cwv2_2_AddRef,               /* 1: AddRef */
    [2] = cwv2_2_Release,              /* 2: Release */
    [3] = cwv2_GetIids,                /* 3: GetIids */
    [4] = cwv2_GetRuntimeClassName,    /* 4: GetRuntimeClassName */
    [5] = cwv2_GetTrustLevel,          /* 5: GetTrustLevel */
    [6 ... 255] = element_generic_slot,
    [7] = cwv2_2_get_Environment,      /* 7: get_Environment */
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

static HRESULT WINAPI wv2_add_CoreWebView2Initialized( void *iface, void *handler, EventRegistrationToken *token )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_add_CoreWebView2Initialized iface %p, handler %p, token %p\n", iface, handler, token );
    impl->init_handler = (IInspectable *)handler;
    if (impl->init_handler) IInspectable_AddRef( impl->init_handler );
    if (token) token->value = 1;
    if (impl->core_webview2) trigger_core_webview2_initialized( impl, &impl->IInspectable_iface );
    return S_OK;
}

static HRESULT WINAPI wv2_remove_CoreWebView2Initialized( void *iface, EventRegistrationToken token )
{
    TRACE( "wv2_remove_CoreWebView2Initialized iface %p\n", iface );
    return S_OK;
}

static void trigger_core_webview2_initialized( struct xaml_element *impl, void *iface )
{
    if (!impl->core_webview2)
    {
        struct core_webview2 *cwv = calloc( 1, sizeof(*cwv) );
        if (cwv)
        {
            cwv->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)cwv2_vtbl;
            cwv->ICoreWebView2_2_iface.lpVtbl = (const struct IInspectableVtbl *)cwv2_2_vtbl;
            cwv->ref = 1;
            impl->core_webview2 = &cwv->IInspectable_iface;
        }
    }
    if (impl->init_handler && !impl->init_triggered)
    {
        typedef HRESULT (WINAPI *p_Invoke)(void *, void *, void *);
        struct cwv2_args *args = calloc( 1, sizeof(*args) );
        p_Invoke invoke;
        impl->init_triggered = TRUE;
        if (args)
        {
            args->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)cwv2_args_vtbl;
            args->ref = 1;
            invoke = (p_Invoke)(*(void ***)impl->init_handler)[3];
            ERR( "invoking init_handler %p on WebView %p with args %p\n", impl->init_handler, &impl->IInspectable_iface, args );
            invoke( impl->init_handler, &impl->IInspectable_iface, &args->IInspectable_iface );
            cwv2_args_Release( &args->IInspectable_iface );
        }
    }
}

static HRESULT WINAPI wv2_EnsureCoreWebView2Async( void *iface, void **op )
{
    struct xaml_element *impl = impl_from_IWebView2( iface );
    ERR( "wv2_EnsureCoreWebView2Async iface %p, op %p\n", iface, op );
    trigger_core_webview2_initialized( impl, iface );
    return xaml_async_action_create( (IAsyncAction **)op );
}

static HRESULT WINAPI wv22_EnsureCoreWebView2Async( void *iface, void *env, void **op )
{
    struct xaml_element *impl = impl_from_IWebView22( iface );
    ERR( "wv22_EnsureCoreWebView2Async iface %p, env %p, op %p\n", iface, env, op );
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
    [17] = wv2_get_CoreWebView2,              /* 17: get_CoreWebView2 (alias) */
    [24] = wv2_add_CoreWebView2Initialized,   /* 24: add_CoreWebView2Initialized */
    [25] = wv2_remove_CoreWebView2Initialized,
    [30] = wv2_add_CoreWebView2Initialized,   /* 30: add_CoreWebView2Initialized */
    [31] = wv2_remove_CoreWebView2Initialized,/* 31: remove_CoreWebView2Initialized */
    [72] = element_add_Loaded,
    [73] = element_remove_Loaded,
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
    [6] = wv22_EnsureCoreWebView2Async,       /* 6: EnsureCoreWebView2Async */
    [7] = wv22_EnsureCoreWebView2AsyncWithOptions,
};

HRESULT element_create( IInspectable *outer, IInspectable **inner, void **instance )
{
    struct xaml_element *impl;
    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)grid_vtbl;
    impl->IGrid_iface.lpVtbl = (const struct IInspectableVtbl *)grid_vtbl;
    impl->IPanel_iface.lpVtbl = (const struct IInspectableVtbl *)panel_vtbl;
    impl->IContentControl_iface.lpVtbl = (const struct IInspectableVtbl *)content_control_vtbl;
    impl->IRowDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)row_def_vtbl;
    impl->IColumnDefinition_iface.lpVtbl = (const struct IInspectableVtbl *)col_def_vtbl;
    impl->IWebView2_iface.lpVtbl = (const struct IInspectableVtbl *)wv2_vtbl;
    impl->IWebView22_iface.lpVtbl = (const struct IInspectableVtbl *)wv22_vtbl;
    impl->class_name = L"Microsoft.UI.Xaml.Controls.Grid";
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
        if (class_name && (wcsstr(class_name, L"Grid") || wcsstr(class_name, L"Panel")))
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
        else if (class_name && (wcsstr(class_name, L"Grid") || wcsstr(class_name, L"Panel") || wcsstr(class_name, L"Canvas")))
            impl->IInspectable_iface.lpVtbl = (const struct IInspectableVtbl *)grid_vtbl;
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

static struct frame_factory row_def_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)grid_factory_vtbl},
    {(const struct IInspectableVtbl *)grid_composable_vtbl},
    1,
};
IActivationFactory *row_def_factory = &row_def_factory_obj.IActivationFactory_iface;

static struct frame_factory col_def_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)grid_factory_vtbl},
    {(const struct IInspectableVtbl *)grid_composable_vtbl},
    1,
};
IActivationFactory *col_def_factory = &col_def_factory_obj.IActivationFactory_iface;

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

static struct frame_factory webview2_factory_obj =
{
    {(const struct IActivationFactoryVtbl *)webview2_factory_vtbl},
    {(const struct IInspectableVtbl *)webview2_composable_vtbl},
    1,
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
