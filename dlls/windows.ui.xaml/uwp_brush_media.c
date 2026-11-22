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
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "weakreference.h"
#include "wincodec.h"
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
    [10] = uwp_ui_event_add,     /* add_ImageFailed */
    [11] = uwp_generic_noop,     /* remove_ImageFailed */
    [12] = uwp_ui_event_add,     /* add_ImageOpened */
    [13] = uwp_generic_noop,     /* remove_ImageOpened */
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
