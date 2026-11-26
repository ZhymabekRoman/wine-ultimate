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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "weakreference.h"
#include "mfapi.h"
#include "mfidl.h"
#include "mfreadwrite.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);



/* Brush, gradient, image-surface, media-player and icon helpers. */

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
    InterlockedExchange( &impl->video_started, 0 );
    uwp_invalidate_window();
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
    InterlockedExchange( &impl->video_started, 0 );
    uwp_invalidate_window();
    return S_OK;
}
const void *uwp_mpe_vtbl[24] =
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
static HRESULT WINAPI uwp_img_add_ImageOpened( void *iface, void *handler, EventRegistrationToken *tok );
static HRESULT WINAPI uwp_img_remove_ImageOpened( void *iface, EventRegistrationToken tok );
const void *uwp_bi_vtbl[24] =
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
    [16] = uwp_img_add_ImageOpened,    /* add_ImageOpened */
    [17] = uwp_img_remove_ImageOpened, /* remove_ImageOpened */
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
const void *uwp_bs_vtbl[16] =
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
static HRESULT WINAPI uwp_st_SetActive( void *iface, boolean is_active )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    boolean was_active;
    if (!impl) return S_OK;
    was_active = impl->state_trigger_active;
    impl->state_trigger_active = is_active;
    if (is_active != was_active)
    {
        struct uwp_element *p;
        for (p = impl->parent; p; p = p->parent)
        {
            if (!wcscmp( p->class_name, L"Windows.UI.Xaml.VisualState" ) && p->name[0])
            {
                struct uwp_element *scope = p->owner_component ? p->owner_component : impl->owner_component;
                if (scope)
                {
                    TRACE( "StateTrigger %s (%s) is_active=%d -> state=%s on scope=%s (%s)\n",
                           debugstr_w(impl->class_name), debugstr_w(impl->name), is_active,
                           debugstr_w(p->name), debugstr_w(scope->class_name), debugstr_w(scope->name) );
                    if (is_active)
                        uwp_vsm_goto_state_on_tree( scope, p->name );
                    else
                        uwp_vsm_leave_state_on_tree( scope, p->name );
                    g_layout_dirty = TRUE;
                }
                break;
            }
        }
    }
    return S_OK;
}
static HRESULT WINAPI uwp_st_get_IsActive( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->state_trigger_active : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_st_put_IsActive( void *iface, boolean is_active )
{
    return uwp_st_SetActive( iface, is_active );
}
const struct uwp_state_trigger_vtbl uwp_state_trigger_vtbl =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_st_SetActive,
    { [0 ... 8] = uwp_generic_noop },
};
const struct uwp_state_trigger_pub_vtbl uwp_state_trigger_pub_vtbl =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_st_get_IsActive,
    uwp_st_put_IsActive,
    { [0 ... 7] = uwp_generic_noop },
};

static struct uwp_element *uwp_elem_find_top_root( struct uwp_element *el )
{
    int guard = 0;
    while (el && el->parent && guard++ < 64)
        el = el->parent;
    return el;
}

static HRESULT WINAPI uwp_uie10_get_ActualOffset( void *iface, void *out_ptr )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    float *out = (float *)out_ptr;
    if (!out) return E_POINTER;
    if (impl && impl->has_bounds)
    {
        int px = (impl->parent && impl->parent->has_bounds) ? impl->parent->bounds.left : 0;
        int py = (impl->parent && impl->parent->has_bounds) ? impl->parent->bounds.top : 0;
        out[0] = (float)(impl->bounds.left - px);
        out[1] = (float)(impl->bounds.top - py);
        out[2] = 0.0f;
    }
    else
    {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_uie10_get_ActualSize( void *iface, void *out_ptr )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    float *out = (float *)out_ptr;
    if (!out) return E_POINTER;
    if (impl && impl->has_bounds)
    {
        out[0] = (float)(impl->bounds.right - impl->bounds.left);
        out[1] = (float)(impl->bounds.bottom - impl->bounds.top);
    }
    else if (impl)
    {
        out[0] = (float)((impl->actual_w > 0) ? impl->actual_w : ((impl->width > 0 && impl->width == impl->width) ? impl->width : 0.0));
        out[1] = (float)((impl->actual_h > 0) ? impl->actual_h : ((impl->height > 0 && impl->height == impl->height) ? impl->height : 0.0));
    }
    else
    {
        out[0] = 0.0f;
        out[1] = 0.0f;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_uie10_get_UIContext( void *iface, void **out )
{
    (void)iface;
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_uie10_get_Shadow( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->xaml_root_shadow : NULL;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_uie10_put_Shadow( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->xaml_root_shadow == (IInspectable *)val) return S_OK;
    if (impl->xaml_root_shadow) IInspectable_Release( impl->xaml_root_shadow );
    impl->xaml_root_shadow = (IInspectable *)val;
    if (impl->xaml_root_shadow) IInspectable_AddRef( impl->xaml_root_shadow );
    return S_OK;
}

static HRESULT WINAPI uwp_uie10_get_XamlRoot( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *root;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (impl->xaml_root_override)
    {
        *out = impl->xaml_root_override;
        IInspectable_AddRef( (IInspectable *)*out );
        return S_OK;
    }
    root = uwp_elem_find_top_root( impl );
    if (!root) root = impl;
    *out = &root->IXamlRoot_iface;
    uwp_sub_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_uie10_put_XamlRoot( void *iface, void *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->xaml_root_override == (IInspectable *)val) return S_OK;
    if (impl->xaml_root_override) IInspectable_Release( impl->xaml_root_override );
    impl->xaml_root_override = (IInspectable *)val;
    if (impl->xaml_root_override) IInspectable_AddRef( impl->xaml_root_override );
    return S_OK;
}

const void *uwp_uie10_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_uie10_get_ActualOffset,
    [7] = uwp_uie10_get_ActualSize,
    [8] = uwp_uie10_get_XamlRoot,
    [9] = uwp_uie10_put_XamlRoot,
    [10] = uwp_uie10_get_UIContext,
    [11] = uwp_uie10_get_Shadow,
    [12] = uwp_uie10_put_Shadow,
    [13 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_xr_get_Content( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_element *root, *target = NULL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    root = uwp_elem_find_top_root( impl );
    if (!root) root = impl;
    if (wcsstr( root->class_name, L"DesktopWindowXamlSource" ) && root->content)
    {
        struct uwp_element *ce = uwp_elem_from_any( root->content );
        if (ce) target = ce;
        else
        {
            if (FAILED(IInspectable_QueryInterface( root->content, &IID_IUIElement_UWP, out )))
            {
                *out = root->content;
                IInspectable_AddRef( (IInspectable *)*out );
            }
            return S_OK;
        }
    }
    else if (root->kind == UWP_KIND_FRAME && g_navigated_page_el)
        target = g_navigated_page_el;
    else
        target = root;
    if (target)
    {
        if (target->outer && SUCCEEDED(IInspectable_QueryInterface( target->outer, &IID_IUIElement_UWP, out )) && *out)
            return S_OK;
        *out = &target->IUIElement_iface;
        uwp_sub_AddRef( (IInspectable *)*out );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_xr_get_Size( void *iface, Size *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    SIZE sz = { 1280, 800 };
    if (!out) return E_POINTER;
    if (impl && impl->actual_w > 0.0 && impl->actual_h > 0.0)
    {
        out->Width = (float)impl->actual_w;
        out->Height = (float)impl->actual_h;
        return S_OK;
    }
    xaml_get_layout_metrics( (impl && impl->island_hwnd) ? impl->island_hwnd : xaml_get_window_hwnd(), &sz, NULL );
    out->Width = (float)(sz.cx > 0 ? sz.cx : 1280);
    out->Height = (float)(sz.cy > 0 ? sz.cy : 800);
    return S_OK;
}

static HRESULT WINAPI uwp_xr_get_RasterizationScale( void *iface, double *out )
{
    if (out) *out = (double)(g_layout_scale > 0.25f ? g_layout_scale : 1.0f);
    return S_OK;
}

static HRESULT WINAPI uwp_xr_get_IsHostVisible( void *iface, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}

static HRESULT WINAPI uwp_xr_add_Changed( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    return xaml_event_registry_add( &impl->xaml_root_changed_reg, handler, tok );
}

static HRESULT WINAPI uwp_xr_remove_Changed( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    return xaml_event_registry_remove( &impl->xaml_root_changed_reg, tok );
}

const void *uwp_xaml_root_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_xr_get_Content,
    [7] = uwp_xr_get_Size,
    [8] = uwp_xr_get_RasterizationScale,
    [9] = uwp_xr_get_IsHostVisible,
    [10] = uwp_uie10_get_UIContext,
    [11] = uwp_xr_add_Changed,
    [12] = uwp_xr_remove_Changed,
    [13 ... 15] = uwp_generic_noop,
};
static HRESULT WINAPI uwp_img_add_ImageOpened( void *iface, void *handler, EventRegistrationToken *tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl || !handler) { if (tok) tok->value = 1; return S_OK; }
    return xaml_event_registry_add( &impl->img_opened_reg, handler, tok );
}

static HRESULT WINAPI uwp_img_remove_ImageOpened( void *iface, EventRegistrationToken tok )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    return xaml_event_registry_remove( &impl->img_opened_reg, tok );
}

const void *uwp_img_vtbl[24] =
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
    [10] = uwp_ui_event_add,         /* add_ImageFailed */
    [11] = uwp_generic_noop,         /* remove_ImageFailed */
    [12] = uwp_img_add_ImageOpened,  /* add_ImageOpened */
    [13] = uwp_img_remove_ImageOpened, /* remove_ImageOpened */
    [14 ... 23] = uwp_generic_noop,
};

static BOOL uwp_extract_comp_brush_image_src_depth( IInspectable *brush, WCHAR *out_uri, size_t cap, int depth )
{
    static const GUID IID_ICompositionSurfaceBrush_UWP =
        { 0xad016d79, 0x1e4c, 0x4c0d, { 0x9c, 0x29, 0x83, 0x33, 0x8c, 0x87, 0xc1, 0x62 } };
    static const GUID IID_ICompositionMaskBrush_UWP =
        { 0x522cf09e, 0xbe6b, 0x4f41, { 0xbe, 0x49, 0xf9, 0x22, 0x6d, 0x47, 0x1b, 0x4a } };
    void *sb = NULL, *mb = NULL;

    if (!brush || !out_uri || cap == 0 || depth > 8) return FALSE;
    if (SUCCEEDED(IInspectable_QueryInterface( brush, &IID_ICompositionSurfaceBrush_UWP, &sb )) && sb)
    {
        typedef HRESULT (WINAPI *get_surf_fn)( void *, IInspectable ** );
        void **vtbl = *(void ***)sb;
        IInspectable *surf = NULL;
        if (vtbl && vtbl[12] && SUCCEEDED(((get_surf_fn)vtbl[12])( sb, &surf )) && surf)
        {
            struct uwp_element *surf_el = uwp_elem_from_any( surf );
            BOOL ok = FALSE;
            if (surf_el && surf_el->image_src[0])
            {
                lstrcpynW( out_uri, surf_el->image_src, cap );
                ok = TRUE;
            }
            IInspectable_Release( surf );
            IUnknown_Release( (IUnknown *)sb );
            return ok;
        }
        IUnknown_Release( (IUnknown *)sb );
    }
    if (SUCCEEDED(IInspectable_QueryInterface( brush, &IID_ICompositionMaskBrush_UWP, &mb )) && mb)
    {
        typedef HRESULT (WINAPI *get_brush_fn)( void *, IInspectable ** );
        void **vtbl = *(void ***)mb;
        IInspectable *src_brush = NULL;
        if (vtbl && vtbl[8] && SUCCEEDED(((get_brush_fn)vtbl[8])( mb, &src_brush )) && src_brush)
        {
            BOOL ok = uwp_extract_comp_brush_image_src_depth( src_brush, out_uri, cap, depth + 1 );
            IInspectable_Release( src_brush );
            IUnknown_Release( (IUnknown *)mb );
            return ok;
        }
        IUnknown_Release( (IUnknown *)mb );
    }
    return FALSE;
}

BOOL uwp_extract_comp_brush_image_src( IInspectable *brush, WCHAR *out_uri, size_t cap )
{
    return uwp_extract_comp_brush_image_src_depth( brush, out_uri, cap, 0 );
}

void uwp_xcbb_ensure_connected( IInspectable *brush )
{
    static const GUID IID_IXamlCompositionBrushBaseOverrides_UWP =
        { 0xd19127f1, 0x38b4, 0x4ea1, { 0x8f, 0x33, 0x84, 0x96, 0x29, 0xa4, 0xc9, 0xc1 } };
    struct uwp_element *b_el;
    IUnknown *target;
    void *ov = NULL;

    if (!brush) return;
    b_el = uwp_elem_from_any( brush );
    if (b_el && (b_el->xcbb_connected || b_el->comp_brush)) return;
    if (b_el && (!wcsncmp( b_el->class_name, L"Windows.UI.Xaml.", 16 ) ||
                 !wcsncmp( b_el->class_name, L"Microsoft.UI.Xaml.", 18 )))
        return;
    target = (b_el && b_el->outer_unk) ? (IUnknown *)b_el->outer_unk : (IUnknown *)brush;
    if (SUCCEEDED(IUnknown_QueryInterface( target, &IID_IXamlCompositionBrushBaseOverrides_UWP, &ov )) && ov)
    {
        typedef HRESULT (WINAPI *on_connected_fn)( void * );
        void **vtbl = *(void ***)ov;
        if (b_el) b_el->xcbb_connected = TRUE;
        if (vtbl && vtbl[6])
        {
            HRESULT hr = ((on_connected_fn)vtbl[6])( ov );
            TRACE( "uwp_xcbb_ensure_connected(%s): OnConnected hr=%#lx\n",
                   b_el ? debugstr_w(b_el->class_name) : "?", hr );
        }
        IUnknown_Release( (IUnknown *)ov );
    }
}

static BOOL uwp_extract_visual_image_src_depth( IInspectable *visual, WCHAR *out_uri, size_t cap, int depth )
{
    static const GUID IID_ISpriteVisual_UWP =
        { 0x08e05581, 0x1ad1, 0x4f97, { 0x97, 0x57, 0x40, 0x2d, 0x76, 0xe4, 0x23, 0x3b } };
    static const GUID IID_IContainerVisual_UWP =
        { 0x02f6bc74, 0xed20, 0x4773, { 0xaf, 0xe6, 0xd4, 0x9b, 0x4a, 0x93, 0xdb, 0x32 } };
    static const GUID IID_IIterable_Visual_UWP =
        { 0x4eed7dce, 0xe4ac, 0x5891, { 0x9b, 0x52, 0x79, 0x9d, 0x70, 0xdf, 0x47, 0xfe } };
    void *sv = NULL, *cv = NULL;

    if (!visual || !out_uri || cap == 0 || depth > 8) return FALSE;
    if (SUCCEEDED(IInspectable_QueryInterface( visual, &IID_ISpriteVisual_UWP, &sv )) && sv)
    {
        typedef HRESULT (WINAPI *get_brush_fn)( void *, IInspectable ** );
        void **vtbl = *(void ***)sv;
        IInspectable *brush = NULL;
        if (vtbl && vtbl[6] && SUCCEEDED(((get_brush_fn)vtbl[6])( sv, &brush )) && brush)
        {
            BOOL ok = uwp_extract_comp_brush_image_src( brush, out_uri, cap );
            IInspectable_Release( brush );
            IUnknown_Release( (IUnknown *)sv );
            if (ok) return TRUE;
        }
        else
        {
            IUnknown_Release( (IUnknown *)sv );
        }
    }
    if (SUCCEEDED(IInspectable_QueryInterface( visual, &IID_IContainerVisual_UWP, &cv )) && cv)
    {
        typedef HRESULT (WINAPI *get_children_fn)( void *, IInspectable ** );
        void **vtbl = *(void ***)cv;
        IInspectable *children = NULL;
        BOOL found = FALSE;
        if (vtbl && vtbl[6] && SUCCEEDED(((get_children_fn)vtbl[6])( cv, &children )) && children)
        {
            void *iterable = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( children, &IID_IIterable_Visual_UWP, &iterable )) && iterable)
            {
                typedef HRESULT (WINAPI *first_fn)( void *, void ** );
                void **it_vtbl = *(void ***)iterable;
                void *iter = NULL;
                if (it_vtbl && it_vtbl[6] && SUCCEEDED(((first_fn)it_vtbl[6])( iterable, &iter )) && iter)
                {
                    typedef HRESULT (WINAPI *get_cur_fn)( void *, IInspectable ** );
                    typedef HRESULT (WINAPI *get_has_fn)( void *, boolean * );
                    typedef HRESULT (WINAPI *move_next_fn)( void *, boolean * );
                    void **iv = *(void ***)iter;
                    boolean has = FALSE;
                    int guard = 0;
                    if (iv && iv[7] && SUCCEEDED(((get_has_fn)iv[7])( iter, &has )))
                    {
                        while (has && guard++ < 64)
                        {
                            IInspectable *child = NULL;
                            if (iv[6] && SUCCEEDED(((get_cur_fn)iv[6])( iter, &child )) && child)
                            {
                                found = uwp_extract_visual_image_src_depth( child, out_uri, cap, depth + 1 );
                                IInspectable_Release( child );
                                if (found) break;
                            }
                            has = FALSE;
                            if (!iv[8] || FAILED(((move_next_fn)iv[8])( iter, &has ))) break;
                        }
                    }
                    IUnknown_Release( (IUnknown *)iter );
                }
                IUnknown_Release( (IUnknown *)iterable );
            }
            IInspectable_Release( children );
        }
        IUnknown_Release( (IUnknown *)cv );
        if (found) return TRUE;
    }
    return FALSE;
}

BOOL uwp_extract_visual_image_src( IInspectable *visual, WCHAR *out_uri, size_t cap )
{
    return uwp_extract_visual_image_src_depth( visual, out_uri, cap, 0 );
}

static HRESULT WINAPI uwp_shape_get_Fill( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->fill_brush : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_shape_put_Fill( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->fill_brush != val)
    {
        if (impl->fill_brush) IInspectable_Release( impl->fill_brush );
        impl->fill_brush = val;
        if (impl->fill_brush) IInspectable_AddRef( impl->fill_brush );
    }
    if (val)
    {
        struct uwp_element *b_el;
        uwp_xcbb_ensure_connected( val );
        b_el = uwp_elem_from_any( val );
        if (wcsstr( impl->name, L"HeroImage" ))
            TRACE( "uwp_shape_put_Fill(HeroImage %s): val=%p b_el=%p b_cls=%s\n",
                   debugstr_w(impl->name), val, b_el, b_el ? debugstr_w(b_el->class_name) : "?" );
        if (b_el)
        {
            if (b_el->image_src[0])
            {
                lstrcpynW( impl->image_src, b_el->image_src, ARRAY_SIZE(impl->image_src) );
                impl->img_loaded = FALSE;
                InterlockedExchange( &impl->img_loading, 0 );
                TRACE( "uwp_shape_put_Fill(%s %s): uri=%s\n", debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(impl->image_src) );
            }
            else if (b_el->comp_brush && uwp_extract_comp_brush_image_src( b_el->comp_brush, impl->image_src, ARRAY_SIZE(impl->image_src) ))
            {
                impl->img_loaded = FALSE;
                InterlockedExchange( &impl->img_loading, 0 );
                TRACE( "uwp_shape_put_Fill(%s %s): comp_brush uri=%s\n", debugstr_w(impl->class_name), debugstr_w(impl->name), debugstr_w(impl->image_src) );
            }
            if (b_el->has_bg)
            {
                impl->bg_argb = b_el->bg_argb;
                impl->bg_alpha = b_el->bg_alpha;
                impl->has_bg = TRUE;
            }
        }
    }
    uwp_invalidate_window();
    return S_OK;
}
static HRESULT WINAPI uwp_shape_get_Stroke( void *iface, IInspectable **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_shape_put_Stroke( void *iface, IInspectable *val )
{
    return S_OK;
}
const void *uwp_shape_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_shape_get_Fill,
    [7] = uwp_shape_put_Fill,
    [8] = uwp_shape_get_Stroke,
    [9] = uwp_shape_put_Stroke,
    [10 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_xcbb_get_CompositionBrush( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->comp_brush : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}
static HRESULT WINAPI uwp_xcbb_put_CompositionBrush( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->comp_brush != val)
    {
        if (impl->comp_brush) IInspectable_Release( impl->comp_brush );
        impl->comp_brush = val;
        if (impl->comp_brush) IInspectable_AddRef( impl->comp_brush );
    }
    if (val)
    {
        if (uwp_extract_comp_brush_image_src( val, impl->image_src, ARRAY_SIZE(impl->image_src) ))
        {
            TRACE( "uwp_xcbb_put_CompositionBrush(%s): extracted uri=%s\n",
                   debugstr_w(impl->class_name), debugstr_w(impl->image_src) );
            impl->img_loaded = FALSE;
            InterlockedExchange( &impl->img_loading, 0 );
            uwp_load_image_asset( impl );
        }
    }
    uwp_invalidate_window();
    return S_OK;
}
const void *uwp_xcbb_prot_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_xcbb_get_CompositionBrush,
    [7] = uwp_xcbb_put_CompositionBrush,
    [8 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_lis_args_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_ILoadedImageSourceLoadCompletedEventArgs_UWP =
        { 0xe9b0047a, 0x0528, 0x4230, { 0x8d, 0x3b, 0x9b, 0x73, 0x5d, 0x48, 0x6f, 0x70 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ILoadedImageSourceLoadCompletedEventArgs_UWP ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uwp_lis_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI uwp_lis_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI uwp_lis_args_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_lis_args_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Media.LoadedImageSourceLoadCompletedEventArgs", 61, n );
}
static HRESULT WINAPI uwp_lis_args_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI uwp_lis_args_get_Status( void *iface, int *status )
{
    if (status) *status = 0; /* LoadedImageSourceLoadStatus_Success */
    return S_OK;
}
static const void *uwp_lis_args_vtbl[] =
{
    uwp_lis_args_QI, uwp_lis_args_AddRef, uwp_lis_args_Release,
    uwp_lis_args_GetIids, uwp_lis_args_GetRuntimeClassName, uwp_lis_args_GetTrustLevel,
    uwp_lis_args_get_Status,
};
static const void *uwp_lis_args_obj = uwp_lis_args_vtbl;

static HRESULT WINAPI uwp_lis_add_LoadCompleted( void *iface, IInspectable *handler, EventRegistrationToken *tok )
{
    static LONG next_tok = 1;
    if (tok) tok->value = InterlockedIncrement( &next_tok );
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)( void *, void *, void * );
        void **vtbl = *(void ***)handler;
        if (vtbl && vtbl[3])
            ((invoke_fn)vtbl[3])( handler, iface, (void *)&uwp_lis_args_obj );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_lis_get_Size( void *iface, struct __x_ABI_CWindows_CFoundation_CSize *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    out->Width = (impl && impl->img_w) ? (float)impl->img_w : 0.0f;
    out->Height = (impl && impl->img_h) ? (float)impl->img_h : 0.0f;
    return S_OK;
}
const void *uwp_lis_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_lis_get_Size,          /* get_DecodedPhysicalSize */
    [7] = uwp_lis_get_Size,          /* get_DecodedSize */
    [8] = uwp_lis_get_Size,          /* get_NaturalSize */
    [9] = uwp_lis_add_LoadCompleted, /* add_LoadCompleted */
    [10] = uwp_generic_noop,         /* remove_LoadCompleted */
    [11 ... 15] = uwp_generic_noop,
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
const void *uwp_scb_vtbl[16] =
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
const void *uwp_brush_vtbl[16] =
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

/* ============================================================================
 * Bundled XAML FontFamily parser & AddFontResourceExW cache + IFontFamily/IFontIcon
 * ============================================================================ */

static SRWLOCK g_bundled_fonts_lock = SRWLOCK_INIT;
static struct
{
    WCHAR path[MAX_PATH];
    WCHAR face[64];
} g_bundled_fonts[64];
static int g_bundled_font_cnt = 0;

static void uwp_register_bundled_font_file( const WCHAR *rel_or_uri, const WCHAR *face )
{
    WCHAR rel[MAX_PATH] = {0}, full[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0};
    const WCHAR *p = rel_or_uri;
    WCHAR *slash;
    size_t i;
    int k;

    if (!p || !p[0]) return;
    while (*p == L' ' || *p == L'\t') p++;
    if (!wcsnicmp( p, L"ms-appx:///", 11 )) p += 11;
    else if (!wcsnicmp( p, L"ms-appx://", 10 )) p += 10;
    while (*p == L'/' || *p == L'\\') p++;
    if (!*p) return;

    lstrcpynW( rel, p, ARRAY_SIZE(rel) );
    for (i = 0; rel[i]; i++) if (rel[i] == L'/') rel[i] = L'\\';
    /* Trim trailing whitespace */
    i = wcslen( rel );
    while (i > 0 && (rel[i - 1] == L' ' || rel[i - 1] == L'\t')) rel[--i] = 0;
    if (!rel[0]) return;

    if (GetModuleFileNameW( NULL, base_dir, ARRAY_SIZE(base_dir) ) && (slash = wcsrchr( base_dir, L'\\' )))
    {
        *(slash + 1) = 0;
        swprintf( full, ARRAY_SIZE(full), L"%s%s", base_dir, rel );
    }
    else
    {
        lstrcpynW( full, rel, ARRAY_SIZE(full) );
    }

    AcquireSRWLockShared( &g_bundled_fonts_lock );
    for (k = 0; k < g_bundled_font_cnt; k++)
    {
        if (!wcsicmp( g_bundled_fonts[k].path, full ))
        {
            ReleaseSRWLockShared( &g_bundled_fonts_lock );
            return;
        }
    }
    ReleaseSRWLockShared( &g_bundled_fonts_lock );

    if (GetFileAttributesW( full ) == INVALID_FILE_ATTRIBUTES)
    {
        WARN( "uwp_register_bundled_font_file: font file not found: %s\n", debugstr_w(full) );
        return;
    }

    AcquireSRWLockExclusive( &g_bundled_fonts_lock );
    for (k = 0; k < g_bundled_font_cnt; k++)
    {
        if (!wcsicmp( g_bundled_fonts[k].path, full ))
        {
            ReleaseSRWLockExclusive( &g_bundled_fonts_lock );
            return;
        }
    }
    if (g_bundled_font_cnt < (int)ARRAY_SIZE(g_bundled_fonts))
    {
        int added = AddFontResourceExW( full, FR_PRIVATE, 0 );
        TRACE( "uwp_register_bundled_font_file: AddFontResourceExW(%s) face=%s -> %d\n",
               debugstr_w(full), debugstr_w(face ? face : L""), added );
        lstrcpynW( g_bundled_fonts[g_bundled_font_cnt].path, full, ARRAY_SIZE(g_bundled_fonts[0].path) );
        if (face && face[0])
            lstrcpynW( g_bundled_fonts[g_bundled_font_cnt].face, face, ARRAY_SIZE(g_bundled_fonts[0].face) );
        g_bundled_font_cnt++;
    }
    ReleaseSRWLockExclusive( &g_bundled_fonts_lock );
}

BOOL uwp_is_bundled_font_face( const WCHAR *face )
{
    int k;
    BOOL found = FALSE;
    if (!face || !face[0]) return FALSE;
    AcquireSRWLockShared( &g_bundled_fonts_lock );
    for (k = 0; k < g_bundled_font_cnt; k++)
    {
        if (g_bundled_fonts[k].face[0] && !wcsicmp( g_bundled_fonts[k].face, face ))
        {
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockShared( &g_bundled_fonts_lock );
    return found;
}

void uwp_resolve_font_family( const WCHAR *raw_spec, WCHAR *out_face, size_t cap )
{
    WCHAR seg[256] = {0};
    const WCHAR *p, *comma, *hash;
    size_t len;

    if (!out_face || !cap) return;
    lstrcpynW( out_face, L"Segoe UI", cap );
    if (!raw_spec || !raw_spec[0]) return;

    p = raw_spec;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p || !wcsicmp( p, L"XamlAutoFontFamily" )) return;

    /* Take first family in comma-separated list; also register any bundled font in it */
    comma = wcschr( p, L',' );
    len = comma ? (size_t)(comma - p) : wcslen( p );
    if (len >= ARRAY_SIZE(seg)) len = ARRAY_SIZE(seg) - 1;
    memcpy( seg, p, len * sizeof(WCHAR) );
    seg[len] = 0;
    while (len > 0 && (seg[len - 1] == L' ' || seg[len - 1] == L'\t')) seg[--len] = 0;
    if (!seg[0] || !wcsicmp( seg, L"XamlAutoFontFamily" )) return;

    hash = wcschr( seg, L'#' );
    if (hash)
    {
        WCHAR font_path[256] = {0};
        const WCHAR *fam = hash + 1;
        size_t plen = (size_t)(hash - seg);
        while (*fam == L' ' || *fam == L'\t') fam++;
        if (plen >= ARRAY_SIZE(font_path)) plen = ARRAY_SIZE(font_path) - 1;
        memcpy( font_path, seg, plen * sizeof(WCHAR) );
        font_path[plen] = 0;
        if (fam[0]) lstrcpynW( out_face, fam, cap );
        if (font_path[0]) uwp_register_bundled_font_file( font_path, fam );
        return;
    }

    lstrcpynW( out_face, seg, cap );
}

const WCHAR *uwp_resolve_elem_font_family( const struct uwp_element *el, WCHAR *buf, size_t cap )
{
    const struct uwp_element *cur;
    int hops = 0;

    if (!buf || !cap) return L"Segoe UI";
    lstrcpynW( buf, L"Segoe UI", cap );
    for (cur = el; cur && hops < 6; cur = cur->parent, hops++)
    {
        if (cur->font_family[0] && wcsicmp( cur->font_family, L"XamlAutoFontFamily" ))
        {
            if (!wcsicmp( cur->font_family, L"SymbolThemeFontFamily" ))
                uwp_resolve_font_family( L"Segoe MDL2 Assets", buf, cap );
            else
                uwp_resolve_font_family( cur->font_family, buf, cap );
            return buf;
        }
        if (cur->kind == UWP_KIND_PAGE || cur->kind == UWP_KIND_FRAME) break;
    }
    if (el && (wcsstr( el->class_name, L"FontIcon" ) || wcsstr( el->class_name, L"SymbolIcon" ) ||
               (el->text[0] >= 0xE700 && el->text[0] <= 0xF8FF && el->text[1] == 0 &&
                xaml_gpu_font_has_glyphs( L"Segoe MDL2 Assets", el->text ))))
    {
        uwp_resolve_font_family( L"Segoe MDL2 Assets", buf, cap );
    }
    return buf;
}

static HRESULT WINAPI uwp_ff_get_Source( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *src = (impl && impl->font_family[0]) ? impl->font_family : L"Segoe UI";
    if (!out) return E_POINTER;
    return WindowsCreateString( src, wcslen(src), out );
}

const void *uwp_font_family_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ff_get_Source,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_ff_factory_CreateInstanceWithName( void *iface, HSTRING family_name,
                                                             IInspectable *outer, IInspectable **inner, void **instance )
{
    HRESULT hr;
    if (!instance) return E_POINTER;
    *instance = NULL;
    hr = uwp_element_create( L"Windows.UI.Xaml.Media.FontFamily", outer, inner, instance );
    if (SUCCEEDED(hr) && *instance && family_name)
    {
        struct uwp_element *ff_el = uwp_elem_from_any( *instance );
        const WCHAR *raw = WindowsGetStringRawBuffer( family_name, NULL );
        if (ff_el && raw && raw[0])
        {
            WCHAR face[64] = {0};
            lstrcpynW( ff_el->font_family, raw, ARRAY_SIZE(ff_el->font_family) );
            uwp_resolve_font_family( raw, face, ARRAY_SIZE(face) );
            TRACE( "FontFamily::CreateInstanceWithName(%s) -> face=%s\n", debugstr_w(raw), debugstr_w(face) );
        }
    }
    return hr;
}

const void *uwp_font_family_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = uwp_ff_factory_CreateInstanceWithName,
    [7 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_fi_get_FontWeight( void *iface, WORD *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = (impl && impl->font_weight > 0) ? (WORD)impl->font_weight : 400;
    return S_OK;
}
static HRESULT WINAPI uwp_fi_put_FontWeight( void *iface, WORD val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->font_weight = val;
    return S_OK;
}
const void *uwp_font_icon_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_Text,           /* get_Glyph */
    [7] = uwp_tb_put_Text,           /* put_Glyph */
    [8] = uwp_ctrl_get_FontSize,     /* get_FontSize */
    [9] = uwp_ctrl_put_FontSize,     /* put_FontSize */
    [10] = uwp_ctrl_get_FontFamily,  /* get_FontFamily */
    [11] = uwp_ctrl_put_FontFamily,  /* put_FontFamily */
    [12] = uwp_fi_get_FontWeight,    /* get_FontWeight */
    [13] = uwp_fi_put_FontWeight,    /* put_FontWeight */
    [14 ... 23] = uwp_generic_noop,
};

/* ============================================================================
 * IImageBrush, ITileBrush, IGradientBrush, ILinearGradientBrush, IGradientStop,
 * and IPathGeometry implementations
 * ============================================================================ */

const void *uwp_image_brush_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_img_get_Source,        /* get_ImageSource */
    [7] = uwp_img_put_Source,        /* put_ImageSource */
    [8] = uwp_ui_event_add,          /* add_ImageFailed */
    [9] = uwp_generic_noop,          /* remove_ImageFailed */
    [10] = uwp_img_add_ImageOpened,    /* add_ImageOpened */
    [11] = uwp_img_remove_ImageOpened, /* remove_ImageOpened */
    [12 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tile_get_AlignmentX( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->tile_align_x : 1; /* Center */
    return S_OK;
}
static HRESULT WINAPI uwp_tile_put_AlignmentX( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->tile_align_x = val;
    return S_OK;
}
static HRESULT WINAPI uwp_tile_get_AlignmentY( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->tile_align_y : 1; /* Center */
    return S_OK;
}
static HRESULT WINAPI uwp_tile_put_AlignmentY( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->tile_align_y = val;
    return S_OK;
}
const void *uwp_tile_brush_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tile_get_AlignmentX,
    [7] = uwp_tile_put_AlignmentX,
    [8] = uwp_tile_get_AlignmentY,
    [9] = uwp_tile_put_AlignmentY,
    [10] = uwp_img_get_Stretch,
    [11] = uwp_img_put_Stretch,
    [12 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_gb_get_SpreadMethod( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->grad_spread_method : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_gb_put_SpreadMethod( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->grad_spread_method = val;
    return S_OK;
}
static HRESULT WINAPI uwp_gb_get_MappingMode( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->grad_mapping_mode : 1; /* RelativeToBoundingBox */
    return S_OK;
}
static HRESULT WINAPI uwp_gb_put_MappingMode( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->grad_mapping_mode = val;
    return S_OK;
}
static HRESULT WINAPI uwp_gb_get_ColorInterpolationMode( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->grad_color_interp : 1; /* SRgbLinearInterpolation */
    return S_OK;
}
static HRESULT WINAPI uwp_gb_put_ColorInterpolationMode( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->grad_color_interp = val;
    return S_OK;
}
static HRESULT WINAPI uwp_gb_get_GradientStops( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->gradient_stops_coll)
    {
        uwp_vec_create( impl, &impl->gradient_stops_coll );
        if (impl->gradient_stops_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( impl->gradient_stops_coll, struct uwp_vec_view, IInspectable_iface );
            v->runtime_class = L"Windows.UI.Xaml.Media.GradientStopCollection";
        }
    }
    *out = impl->gradient_stops_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_gb_put_GradientStops( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->gradient_stops_coll != val)
    {
        if (impl->gradient_stops_coll) IInspectable_Release( impl->gradient_stops_coll );
        impl->gradient_stops_coll = val;
        if (val) IInspectable_AddRef( val );
    }
    uwp_invalidate_window();
    return S_OK;
}
const void *uwp_grad_brush_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_gb_get_SpreadMethod,
    [7] = uwp_gb_put_SpreadMethod,
    [8] = uwp_gb_get_MappingMode,
    [9] = uwp_gb_put_MappingMode,
    [10] = uwp_gb_get_ColorInterpolationMode,
    [11] = uwp_gb_put_ColorInterpolationMode,
    [12] = uwp_gb_get_GradientStops,
    [13] = uwp_gb_put_GradientStops,
    [14 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_lgb_get_StartPoint( void *iface, struct __x_ABI_CWindows_CFoundation_CPoint *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    out->X = impl ? impl->grad_start_x : 0.0f;
    out->Y = impl ? impl->grad_start_y : 0.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_lgb_put_StartPoint( void *iface, struct __x_ABI_CWindows_CFoundation_CPoint val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->grad_start_x = val.X; impl->grad_start_y = val.Y; uwp_invalidate_window(); }
    return S_OK;
}
static HRESULT WINAPI uwp_lgb_get_EndPoint( void *iface, struct __x_ABI_CWindows_CFoundation_CPoint *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    out->X = impl ? impl->grad_end_x : 1.0f;
    out->Y = impl ? impl->grad_end_y : 1.0f;
    return S_OK;
}
static HRESULT WINAPI uwp_lgb_put_EndPoint( void *iface, struct __x_ABI_CWindows_CFoundation_CPoint val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->grad_end_x = val.X; impl->grad_end_y = val.Y; uwp_invalidate_window(); }
    return S_OK;
}
const void *uwp_lgb_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_lgb_get_StartPoint,
    [7] = uwp_lgb_put_StartPoint,
    [8] = uwp_lgb_get_EndPoint,
    [9] = uwp_lgb_put_EndPoint,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_gs_get_Offset( void *iface, double *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->grad_stop_offset : 0.0;
    return S_OK;
}
static HRESULT WINAPI uwp_gs_put_Offset( void *iface, double val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) { impl->grad_stop_offset = val; uwp_invalidate_window(); }
    return S_OK;
}
const void *uwp_grad_stop_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_scb_get_Color,
    [7] = uwp_scb_put_Color,
    [8] = uwp_gs_get_Offset,
    [9] = uwp_gs_put_Offset,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_pg_get_FillRule( void *iface, int *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->fill_rule : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_pg_put_FillRule( void *iface, int val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->fill_rule = val;
    return S_OK;
}
static HRESULT WINAPI uwp_pg_get_Figures( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->figures_coll)
    {
        uwp_vec_create( impl, &impl->figures_coll );
        if (impl->figures_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( impl->figures_coll, struct uwp_vec_view, IInspectable_iface );
            v->runtime_class = L"Windows.UI.Xaml.Media.PathFigureCollection";
        }
    }
    *out = impl->figures_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}
static HRESULT WINAPI uwp_pg_put_Figures( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->figures_coll != val)
    {
        if (impl->figures_coll) IInspectable_Release( impl->figures_coll );
        impl->figures_coll = val;
        if (val) IInspectable_AddRef( val );
    }
    return S_OK;
}
const void *uwp_path_geom_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_pg_get_FillRule,
    [7] = uwp_pg_put_FillRule,
    [8] = uwp_pg_get_Figures,
    [9] = uwp_pg_put_Figures,
    [10 ... 15] = uwp_generic_noop,
};
const void *uwp_path_geom_f2_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_pg_get_Figures,
    [7] = uwp_pg_put_Figures,
    [8] = uwp_pg_get_FillRule,
    [9] = uwp_pg_put_FillRule,
    [10 ... 15] = uwp_generic_noop,
};

/* ============================================================================
 * MediaPlayerElement video decoding via Media Foundation (IMFSourceReader)
 * ============================================================================ */

static const GUID IID_IMediaPlayerSource2_mpe =
    { 0x82449b9f, 0x7322, 0x4c0b, { 0xb0, 0x3b, 0x3e, 0x69, 0xa4, 0x82, 0x60, 0xc5 } };
static const GUID IID_IMediaSource4_mpe =
    { 0xbdafad57, 0x8eff, 0x4c63, { 0x85, 0xa6, 0x84, 0xde, 0x0a, 0xe3, 0xe4, 0xf2 } };
static const GUID MF_MT_MAJOR_TYPE_local =
    { 0x48eba18e, 0xf8c9, 0x4687, { 0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f } };
static const GUID MF_MT_SUBTYPE_local =
    { 0xf7e34c9a, 0x42e8, 0x4714, { 0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5 } };
static const GUID MFMediaType_Video_local =
    { 0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_RGB32_local =
    { 0x00000016, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_ARGB32_local =
    { 0x00000015, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_NV12_local =
    { 0x3231564e, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_I420_local =
    { 0x30323449, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_IYUV_local =
    { 0x56555949, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MFVideoFormat_YV12_local =
    { 0x32315659, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID MF_MT_FRAME_SIZE_local =
    { 0x1652c33d, 0xd6b2, 0x4012, { 0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d } };
static const GUID MF_MT_DEFAULT_STRIDE_local =
    { 0x644b4e48, 0x1e02, 0x4516, { 0xb0, 0xeb, 0xc0, 0x1c, 0xa9, 0xd4, 0x9a, 0xc6 } };
static const GUID MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING_local =
    { 0xfb394f3d, 0xccf1, 0x42ee, { 0xbb, 0xb3, 0xf9, 0xb8, 0x45, 0xd5, 0x68, 0x1d } };

typedef HRESULT (WINAPI *pfn_MFStartup)( ULONG, DWORD );
typedef HRESULT (WINAPI *pfn_MFShutdown)( void );
typedef HRESULT (WINAPI *pfn_MFCreateMediaType)( IMFMediaType ** );
typedef HRESULT (WINAPI *pfn_MFCreateAttributes)( IMFAttributes **, UINT32 );
typedef HRESULT (WINAPI *pfn_MFCreateSourceReaderFromURL)( LPCWSTR, IMFAttributes *, IMFSourceReader ** );

static BOOL uwp_mpe_extract_uri( struct uwp_element *el, WCHAR *out, size_t cap )
{
    IInspectable *src = NULL;
    BOOL release_src = FALSE;

    if (!el || !out || !cap) return FALSE;
    out[0] = 0;

    if (el->media_source)
    {
        src = el->media_source;
    }
    else if (el->media_player)
    {
        void *src2 = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( el->media_player, &IID_IMediaPlayerSource2_mpe, &src2 )) && src2)
        {
            HRESULT (WINAPI *get_src)( void *, IInspectable ** ) = (void *)(*(void ***)src2)[6];
            if (get_src && SUCCEEDED(get_src( src2, &src )) && src)
                release_src = TRUE;
            IUnknown_Release( (IUnknown *)src2 );
        }
    }

    if (src)
    {
        void *ms4 = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( src, &IID_IMediaSource4_mpe, &ms4 )) && ms4)
        {
            HRESULT (WINAPI *get_uri)( void *, IInspectable ** ) = (void *)(*(void ***)ms4)[9];
            IInspectable *uri_obj = NULL;
            if (get_uri && SUCCEEDED(get_uri( ms4, &uri_obj )) && uri_obj)
            {
                uwp_extract_uri_string( uri_obj, out, cap );
                IInspectable_Release( uri_obj );
            }
            IUnknown_Release( (IUnknown *)ms4 );
        }
        if (!out[0])
            uwp_extract_uri_string( src, out, cap );
        if (release_src)
            IInspectable_Release( src );
    }
    return out[0] != 0;
}

static BOOL uwp_resolve_media_path( const WCHAR *uri, WCHAR *out_path, size_t cap )
{
    const WCHAR *p = uri;
    WCHAR rel[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0};
    WCHAR *slash;
    size_t i;

    if (!uri || !uri[0] || !out_path || !cap) return FALSE;
    out_path[0] = 0;

    if (!wcsncmp( p, L"ms-appx:///", 11 ) || !wcsncmp( p, L"ms-appx://", 10 ))
    {
        p += (!wcsncmp( p, L"ms-appx:///", 11 )) ? 11 : 10;
        while (*p == '/' || *p == '\\') p++;
        lstrcpynW( rel, p, ARRAY_SIZE(rel) );
        for (i = 0; rel[i]; i++) if (rel[i] == '/') rel[i] = '\\';
        GetModuleFileNameW( NULL, base_dir, ARRAY_SIZE(base_dir) );
        slash = wcsrchr( base_dir, '\\' );
        if (slash) *(slash + 1) = 0;
        swprintf( out_path, cap, L"%s%s", base_dir, rel );
        return GetFileAttributesW( out_path ) != INVALID_FILE_ATTRIBUTES;
    }
    if (!wcsncmp( p, L"file:///", 8 ))
    {
        p += 8;
        lstrcpynW( out_path, p, cap );
        for (i = 0; out_path[i]; i++) if (out_path[i] == '/') out_path[i] = '\\';
        return GetFileAttributesW( out_path ) != INVALID_FILE_ATTRIBUTES;
    }
    lstrcpynW( out_path, uri, cap );
    return TRUE;
}

static inline BYTE clamp_u8( int v )
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (BYTE)v;
}

static DWORD WINAPI uwp_media_decode_worker( void *param )
{
    struct uwp_element *el = (struct uwp_element *)param;
    HMODULE h_mfplat = NULL, h_mfrw = NULL;
    pfn_MFStartup pMFStartup;
    pfn_MFShutdown pMFShutdown;
    pfn_MFCreateMediaType pMFCreateMediaType;
    pfn_MFCreateAttributes pMFCreateAttributes;
    pfn_MFCreateSourceReaderFromURL pMFCreateSourceReaderFromURL;
    IMFAttributes *attrs = NULL;
    IMFSourceReader *reader = NULL;
    IMFMediaType *req_type = NULL, *cur_type = NULL;
    WCHAR path[MAX_PATH] = {0};
    GUID out_subtype = { 0 };
    UINT64 frame_sz = 0;
    UINT src_w = 0, src_h = 0, dst_w = 0, dst_h = 0;
    LONG stride = 0;
    UINT32 u32_stride = 0;
    BYTE *work_buf = NULL;
    BOOL mf_started = FALSE, logged_first = FALSE;
    HRESULT hr;

    if (!el || !uwp_resolve_media_path( el->video_uri, path, ARRAY_SIZE(path) ))
    {
        WARN( "uwp_media_decode_worker: failed to resolve path for %s\n", el ? debugstr_w(el->video_uri) : "null" );
        return 0;
    }

    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    h_mfplat = LoadLibraryW( L"mfplat.dll" );
    h_mfrw = LoadLibraryW( L"mfreadwrite.dll" );
    if (!h_mfplat || !h_mfrw)
    {
        WARN( "uwp_media_decode_worker: mfplat/mfreadwrite unavailable\n" );
        goto done;
    }

    pMFStartup = (pfn_MFStartup)GetProcAddress( h_mfplat, "MFStartup" );
    pMFShutdown = (pfn_MFShutdown)GetProcAddress( h_mfplat, "MFShutdown" );
    pMFCreateMediaType = (pfn_MFCreateMediaType)GetProcAddress( h_mfplat, "MFCreateMediaType" );
    pMFCreateAttributes = (pfn_MFCreateAttributes)GetProcAddress( h_mfplat, "MFCreateAttributes" );
    pMFCreateSourceReaderFromURL = (pfn_MFCreateSourceReaderFromURL)GetProcAddress( h_mfrw, "MFCreateSourceReaderFromURL" );
    if (!pMFStartup || !pMFCreateMediaType || !pMFCreateSourceReaderFromURL)
        goto done;

    hr = pMFStartup( MF_VERSION, MFSTARTUP_NOSOCKET );
    if (FAILED(hr))
    {
        WARN( "uwp_media_decode_worker: MFStartup failed %#lx\n", hr );
        goto done;
    }
    mf_started = TRUE;

    if (pMFCreateAttributes && SUCCEEDED(pMFCreateAttributes( &attrs, 1 )) && attrs)
        IMFAttributes_SetUINT32( attrs, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING_local, TRUE );

    hr = pMFCreateSourceReaderFromURL( path, attrs, &reader );
    if (FAILED(hr) || !reader)
    {
        WARN( "uwp_media_decode_worker: MFCreateSourceReaderFromURL(%s) failed %#lx\n", debugstr_w(path), hr );
        goto done;
    }

    hr = pMFCreateMediaType( &req_type );
    if (FAILED(hr) || !req_type) goto done;
    IMFMediaType_SetGUID( req_type, &MF_MT_MAJOR_TYPE_local, &MFMediaType_Video_local );
    IMFMediaType_SetGUID( req_type, &MF_MT_SUBTYPE_local, &MFVideoFormat_RGB32_local );
    hr = IMFSourceReader_SetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, req_type );
    if (FAILED(hr))
    {
        IMFMediaType_SetGUID( req_type, &MF_MT_SUBTYPE_local, &MFVideoFormat_NV12_local );
        hr = IMFSourceReader_SetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, req_type );
        if (FAILED(hr))
        {
            WARN( "uwp_media_decode_worker: SetCurrentMediaType(RGB32/NV12) failed %#lx\n", hr );
            goto done;
        }
    }

    hr = IMFSourceReader_GetCurrentMediaType( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur_type );
    if (FAILED(hr) || !cur_type) goto done;
    IMFMediaType_GetGUID( cur_type, &MF_MT_SUBTYPE_local, &out_subtype );
    if (FAILED(IMFMediaType_GetUINT64( cur_type, &MF_MT_FRAME_SIZE_local, &frame_sz )))
        goto done;
    src_w = (UINT)(frame_sz >> 32);
    src_h = (UINT)(frame_sz & 0xffffffffu);
    if (!src_w || !src_h || src_w > 4096 || src_h > 4096) goto done;

    if (SUCCEEDED(IMFMediaType_GetUINT32( cur_type, &MF_MT_DEFAULT_STRIDE_local, &u32_stride )))
        stride = (LONG)u32_stride;
    else
        stride = (LONG)(src_w * 4);

    /* Cap decoded atlas upload size to <= 960x720 while preserving exact aspect ratio */
    dst_w = src_w;
    dst_h = src_h;
    if (dst_w > 960 || dst_h > 720)
    {
        double sx = 960.0 / (double)dst_w;
        double sy = 720.0 / (double)dst_h;
        double sc = (sx < sy) ? sx : sy;
        dst_w = max( 1u, (UINT)((double)src_w * sc + 0.5) );
        dst_h = max( 1u, (UINT)((double)src_h * sc + 0.5) );
    }

    work_buf = malloc( (size_t)dst_w * dst_h * 4 );
    if (!work_buf) goto done;

    TRACE( "uwp_media_decode_worker: opened %s (%ux%u -> %ux%u, stride=%ld, subtype=%s)\n",
           debugstr_w(path), src_w, src_h, dst_w, dst_h, stride, debugstr_guid(&out_subtype) );

    while (!InterlockedCompareExchange( &el->video_stop, 0, 0 ))
    {
        DWORD stream_idx = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample *sample = NULL;

        if (el->visibility != 0)
        {
            Sleep( 100 );
            continue;
        }

        hr = IMFSourceReader_ReadSample( reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                         &stream_idx, &flags, &ts, &sample );
        if (FAILED(hr))
        {
            WARN( "uwp_media_decode_worker: ReadSample failed %#lx\n", hr );
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            static const GUID guid_null = { 0 };
            PROPVARIANT pos;
            if (sample) IMFSample_Release( sample );
            PropVariantInit( &pos );
            pos.vt = VT_I8;
            pos.hVal.QuadPart = 0;
            hr = IMFSourceReader_SetCurrentPosition( reader, &guid_null, &pos );
            if (FAILED(hr)) break;
            continue;
        }
        if (sample)
        {
            IMFMediaBuffer *buf = NULL;
            if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer( sample, &buf )) && buf)
            {
                BYTE *raw = NULL;
                DWORD max_len = 0, cur_len = 0;
                if (SUCCEEDED(IMFMediaBuffer_Lock( buf, &raw, &max_len, &cur_len )) && raw && cur_len > 0)
                {
                    BOOL is_rgb32 = (cur_len >= (DWORD)((size_t)src_w * src_h * 4)) &&
                                    (IsEqualGUID( &out_subtype, &MFVideoFormat_RGB32_local ) ||
                                     IsEqualGUID( &out_subtype, &MFVideoFormat_ARGB32_local ) ||
                                     (cur_len >= (DWORD)((size_t)src_w * src_h * 4)));
                    BOOL is_nv12 = !is_rgb32 && (cur_len >= (DWORD)((size_t)src_w * src_h * 3 / 2)) &&
                                   IsEqualGUID( &out_subtype, &MFVideoFormat_NV12_local );
                    BOOL is_planar420 = !is_rgb32 && !is_nv12 && (cur_len >= (DWORD)((size_t)src_w * src_h * 3 / 2)) &&
                                        (IsEqualGUID( &out_subtype, &MFVideoFormat_I420_local ) ||
                                         IsEqualGUID( &out_subtype, &MFVideoFormat_IYUV_local ) ||
                                         IsEqualGUID( &out_subtype, &MFVideoFormat_YV12_local ));
                    BOOL decoded_ok = FALSE;

                    if (!logged_first)
                    {
                        logged_first = TRUE;
                        TRACE( "uwp_media_decode_worker: first frame cur_len=%lu rgb32=%d nv12=%d planar420=%d stride=%ld subtype=%s\n",
                               (unsigned long)cur_len, is_rgb32, is_nv12, is_planar420, stride, debugstr_guid(&out_subtype) );
                    }

                    if (is_rgb32)
                    {
                        LONG abs_stride = stride < 0 ? -stride : stride;
                        BOOL bottom_up = (stride < 0);
                        if (abs_stride < (LONG)(src_w * 4))
                            abs_stride = (LONG)(src_w * 4);
                        if ((DWORD)((size_t)abs_stride * src_h) <= cur_len)
                        {
                            UINT y, x;
                            for (y = 0; y < dst_h; y++)
                            {
                                UINT sy = (dst_h == src_h) ? y : (UINT)((UINT64)y * src_h / dst_h);
                                UINT row_idx = bottom_up ? (src_h - 1 - sy) : sy;
                                const BYTE *srow = raw + (size_t)row_idx * (size_t)abs_stride;
                                BYTE *drow = work_buf + (size_t)y * dst_w * 4;
                                for (x = 0; x < dst_w; x++)
                                {
                                    UINT sx = (dst_w == src_w) ? x : (UINT)((UINT64)x * src_w / dst_w);
                                    drow[x * 4 + 0] = srow[sx * 4 + 0];
                                    drow[x * 4 + 1] = srow[sx * 4 + 1];
                                    drow[x * 4 + 2] = srow[sx * 4 + 2];
                                    drow[x * 4 + 3] = 255;
                                }
                            }
                            decoded_ok = TRUE;
                        }
                    }
                    else if (is_nv12 || (!is_planar420 && cur_len >= (DWORD)((size_t)src_w * src_h * 3 / 2)))
                    {
                        UINT y_stride = (stride > 0 && (UINT)stride >= src_w) ? (UINT)stride : src_w;
                        const BYTE *y_plane = raw;
                        const BYTE *uv_plane = raw + (size_t)y_stride * src_h;
                        if ((size_t)y_stride * src_h + (size_t)y_stride * (src_h / 2) <= cur_len)
                        {
                            UINT y, x;
                            for (y = 0; y < dst_h; y++)
                            {
                                UINT sy = (UINT)((UINT64)y * src_h / dst_h);
                                const BYTE *yrow = y_plane + (size_t)sy * y_stride;
                                const BYTE *uvrow = uv_plane + (size_t)(sy >> 1) * y_stride;
                                BYTE *drow = work_buf + (size_t)y * dst_w * 4;
                                for (x = 0; x < dst_w; x++)
                                {
                                    UINT sx = (UINT)((UINT64)x * src_w / dst_w);
                                    int Y = (int)yrow[sx] - 16;
                                    int U = (int)uvrow[(sx & ~1u)] - 128;
                                    int V = (int)uvrow[(sx & ~1u) + 1] - 128;
                                    int C = Y < 0 ? 0 : Y * 298;
                                    drow[x * 4 + 2] = clamp_u8( (C + 409 * V + 128) >> 8 );
                                    drow[x * 4 + 1] = clamp_u8( (C - 100 * U - 208 * V + 128) >> 8 );
                                    drow[x * 4 + 0] = clamp_u8( (C + 516 * U + 128) >> 8 );
                                    drow[x * 4 + 3] = 255;
                                }
                            }
                            decoded_ok = TRUE;
                        }
                    }
                    else if (is_planar420)
                    {
                        BOOL swap_uv = IsEqualGUID( &out_subtype, &MFVideoFormat_YV12_local );
                        UINT y_stride = (stride > 0 && (UINT)stride >= src_w) ? (UINT)stride : src_w;
                        UINT uv_stride = y_stride / 2;
                        const BYTE *y_plane = raw;
                        const BYTE *p1 = raw + (size_t)y_stride * src_h;
                        const BYTE *p2 = p1 + (size_t)uv_stride * (src_h / 2);
                        const BYTE *u_plane = swap_uv ? p2 : p1;
                        const BYTE *v_plane = swap_uv ? p1 : p2;
                        if ((size_t)y_stride * src_h + (size_t)uv_stride * src_h <= cur_len)
                        {
                            UINT y, x;
                            for (y = 0; y < dst_h; y++)
                            {
                                UINT sy = (UINT)((UINT64)y * src_h / dst_h);
                                const BYTE *yrow = y_plane + (size_t)sy * y_stride;
                                const BYTE *urow = u_plane + (size_t)(sy >> 1) * uv_stride;
                                const BYTE *vrow = v_plane + (size_t)(sy >> 1) * uv_stride;
                                BYTE *drow = work_buf + (size_t)y * dst_w * 4;
                                for (x = 0; x < dst_w; x++)
                                {
                                    UINT sx = (UINT)((UINT64)x * src_w / dst_w);
                                    int Y = (int)yrow[sx] - 16;
                                    int U = (int)urow[sx >> 1] - 128;
                                    int V = (int)vrow[sx >> 1] - 128;
                                    int C = Y < 0 ? 0 : Y * 298;
                                    drow[x * 4 + 2] = clamp_u8( (C + 409 * V + 128) >> 8 );
                                    drow[x * 4 + 1] = clamp_u8( (C - 100 * U - 208 * V + 128) >> 8 );
                                    drow[x * 4 + 0] = clamp_u8( (C + 516 * U + 128) >> 8 );
                                    drow[x * 4 + 3] = 255;
                                }
                            }
                            decoded_ok = TRUE;
                        }
                    }

                    if (decoded_ok)
                    {
                        AcquireSRWLockExclusive( &el->video_lock );
                        if (!el->video_pixels || el->video_w != dst_w || el->video_h != dst_h)
                        {
                            free( el->video_pixels );
                            el->video_pixels = malloc( (size_t)dst_w * dst_h * 4 );
                            el->video_w = dst_w;
                            el->video_h = dst_h;
                        }
                        if (el->video_pixels)
                        {
                            memcpy( el->video_pixels, work_buf, (size_t)dst_w * dst_h * 4 );
                            el->video_frame_seq++;
                        }
                        ReleaseSRWLockExclusive( &el->video_lock );
                        uwp_invalidate_window();
                    }
                    IMFMediaBuffer_Unlock( buf );
                }
                IMFMediaBuffer_Release( buf );
            }
            IMFSample_Release( sample );
            Sleep( 33 );
        }
    }

done:
    free( work_buf );
    if (cur_type) IMFMediaType_Release( cur_type );
    if (req_type) IMFMediaType_Release( req_type );
    if (reader) IMFSourceReader_Release( reader );
    if (attrs) IMFAttributes_Release( attrs );
    if (mf_started && pMFShutdown) pMFShutdown();
    if (h_mfrw) FreeLibrary( h_mfrw );
    if (h_mfplat) FreeLibrary( h_mfplat );
    CoUninitialize();
    return 0;
}

void uwp_media_ensure_started( struct uwp_element *el )
{
    WCHAR uri[512] = {0};

    if (!el) return;
    if (InterlockedCompareExchange( &el->video_started, 0, 0 ) != 0)
        return;
    if (!uwp_mpe_extract_uri( el, uri, ARRAY_SIZE(uri) ))
        return;
    if (InterlockedCompareExchange( &el->video_started, 1, 0 ) != 0)
        return;

    lstrcpynW( el->video_uri, uri, ARRAY_SIZE(el->video_uri) );
    InterlockedExchange( &el->video_stop, 0 );
    el->video_thread = CreateThread( NULL, 0, uwp_media_decode_worker, el, 0, NULL );
    if (!el->video_thread)
        InterlockedExchange( &el->video_started, 0 );
}
