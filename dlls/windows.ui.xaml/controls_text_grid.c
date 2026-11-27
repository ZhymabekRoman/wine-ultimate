/* WinRT Windows.UI.Xaml UWP Grid, TextBlock, Binding, Row/Column & Flyout Popup
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
#include <stdlib.h>
#include <string.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "uwp_private.h"

const void *uwp_grid_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_grid_get_RowDefinitions,
    [7] = uwp_grid_get_ColumnDefinitions,
    [8 ... 31] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_grid3_get_double( void *iface, double *val ) { if (val) *val = 0.0; return S_OK; }
static HRESULT WINAPI uwp_grid3_put_double( void *iface, double val ) { return S_OK; }
const void *uwp_grid3_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_grid3_get_double,
    [7] = uwp_grid3_put_double,
    [8] = uwp_grid3_get_double,
    [9] = uwp_grid3_put_double,
    [10 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_tb_get_TextWrapping( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->text_wrapping : 1;
    return S_OK;
}
static HRESULT WINAPI uwp_tb_put_TextWrapping( void *iface, INT32 val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->text_wrapping = val;
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_tb_get_MaxLines( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = impl ? impl->max_lines : 0;
    return S_OK;
}
static HRESULT WINAPI uwp_tb_put_MaxLines( void *iface, INT32 val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl)
    {
        impl->max_lines = max( 0, val );
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static HRESULT WINAPI uwp_rtb2_get_MaxLines( void *iface, void *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if (uwp_is_rich_text_block( impl ))
        *(INT32 *)out = impl->max_lines;
    else
        *(void **)out = NULL;
    return S_OK;
}
static HRESULT WINAPI uwp_rtb2_put_MaxLines( void *iface, ULONG_PTR val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && (val < 0x10000 || uwp_is_rich_text_block( impl )))
    {
        impl->max_lines = max( 0, (INT32)val );
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}
static BOOL uwp_tb_is_trimmed_or_overflow( const struct uwp_element *impl )
{
    WCHAR rich_buf[2048];
    const WCHAR *txt;
    double fs, eff_w, max_w;
    int lines = 0;
    const WCHAR *p;

    if (!impl || impl->max_lines <= 0) return FALSE;
    txt = uwp_elem_text( impl );
    if (!txt[0] && impl->inlines_coll && uwp_flatten_inlines_text( impl, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
        txt = rich_buf;
    if (!txt[0]) return FALSE;
    fs = (impl->font_size > 0.0 ? impl->font_size : 14.0);
    eff_w = (impl->actual_w > 32.0) ? impl->actual_w : ((impl->width > 32.0) ? impl->width : 600.0);
    max_w = (eff_w > 48.0) ? (eff_w - 16.0) : eff_w;
    if (impl->max_width > 0.0 && impl->max_width < max_w) max_w = impl->max_width;
    p = txt;
    while (*p)
    {
        const WCHAR *eol = p;
        double seg_w;
        int seg_lines = 1;
        while (*eol && *eol != L'\n' && *eol != L'\r') eol++;
        seg_w = (double)(eol - p) * fs * 0.56;
        if (max_w > 32.0 && seg_w > max_w)
            seg_lines = (int)((seg_w + max_w - 1.0) / max_w);
        lines += seg_lines;
        p = eol;
        if (*p == L'\r') p++;
        if (*p == L'\n') p++;
    }
    return lines > impl->max_lines;
}
static HRESULT WINAPI uwp_tb_get_IsTextTrimmed( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = uwp_tb_is_trimmed_or_overflow( impl ) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT WINAPI uwp_rtb_get_HasOverflowContent( void *iface, boolean *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (out) *out = uwp_tb_is_trimmed_or_overflow( impl ) ? TRUE : FALSE;
    return S_OK;
}

static HRESULT WINAPI uwp_tb_get_FontWeight( void *iface, UINT16 *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (val) *val = (impl && impl->font_weight > 0) ? (UINT16)impl->font_weight : 400;
    return S_OK;
}
static HRESULT WINAPI uwp_tb_put_FontWeight( void *iface, UINT16 val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl && val >= 100 && val <= 900)
    {
        impl->font_weight = (int)val;
        impl->local_prop_mask |= UWP_LP_FONT_WEIGHT;
        uwp_invalidate_window();
    }
    return S_OK;
}

const void *uwp_tb_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ctrl_get_FontSize,     /* get_FontSize */
    [7] = uwp_ctrl_put_FontSize,     /* put_FontSize */
    [8] = uwp_ctrl_get_FontFamily,   /* get_FontFamily */
    [9] = uwp_ctrl_put_FontFamily,   /* put_FontFamily */
    [10] = uwp_tb_get_FontWeight,    /* get_FontWeight */
    [11] = uwp_tb_put_FontWeight,    /* put_FontWeight */
    [12 ... 19] = uwp_generic_noop,
    [20] = uwp_tb_get_TextWrapping,  /* get_TextWrapping */
    [21] = uwp_tb_put_TextWrapping,  /* put_TextWrapping */
    [22 ... 23] = uwp_generic_noop,
    [24] = uwp_tb_get_Inlines,
    [25] = uwp_generic_noop,
    [26] = uwp_tb_get_Text,
    [27] = uwp_tb_put_Text,
    [28] = uwp_tb_get_Inlines,       /* get_Inlines */
    [29 ... 33] = uwp_generic_noop,
    [34] = uwp_tb_get_Inlines,
    [35 ... 63] = uwp_generic_noop,
};

const void *uwp_rtb_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_ctrl_get_FontSize,          /* get_FontSize */
    [7] = uwp_ctrl_put_FontSize,          /* put_FontSize */
    [8] = uwp_ctrl_get_FontFamily,        /* get_FontFamily */
    [9] = uwp_ctrl_put_FontFamily,        /* put_FontFamily */
    [10 ... 19] = uwp_generic_noop,
    [20] = uwp_tb_get_TextWrapping,       /* get_TextWrapping */
    [21] = uwp_tb_put_TextWrapping,       /* put_TextWrapping */
    [22 ... 23] = uwp_generic_noop,
    [24] = uwp_tb_get_Inlines,
    [25 ... 33] = uwp_generic_noop,
    [34] = uwp_tb_get_Inlines,            /* get_Blocks */
    [35] = uwp_rtb_get_HasOverflowContent,/* get_HasOverflowContent */
    [36] = uwp_generic_noop,
    [37] = uwp_rtb_get_HasOverflowContent,/* IRichTextBlock::get_HasOverflowContent (slot 0x25) */
    [38 ... 63] = uwp_generic_noop,
};

const void *uwp_tb2_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_rtb2_get_MaxLines,          /* IRichTextBlock2::get_MaxLines */
    [7] = uwp_rtb2_put_MaxLines,          /* IRichTextBlock2::put_MaxLines */
    [8] = uwp_tb_get_MaxLines,            /* ITextBlock2::get_MaxLines */
    [9] = uwp_tb_put_MaxLines,            /* ITextBlock2::put_MaxLines */
    [10 ... 23] = uwp_generic_noop,
};

const void *uwp_tb6_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_IsTextTrimmed,       /* get_IsTextTrimmed */
    [7 ... 15] = uwp_generic_noop,
};

const void *uwp_paragraph_vtbl[64] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_Inlines,
    [7 ... 23] = uwp_generic_noop,
    [24] = uwp_tb_get_Inlines,
    [25 ... 33] = uwp_generic_noop,
    [34] = uwp_tb_get_Inlines,
    [35 ... 63] = uwp_generic_noop,
};

const void *uwp_run_vtbl[16] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_tb_get_Text,
    [7] = uwp_tb_put_Text,
    [8 ... 15] = uwp_generic_noop,
};

static HRESULT WINAPI uwp_binding_get_Path( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->tag : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_Path( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->tag != val)
    {
        if (impl->tag) IInspectable_Release( impl->tag );
        impl->tag = val;
        if (val) IInspectable_AddRef( val );
    }
    return S_OK;
}

static HRESULT WINAPI uwp_binding_get_Mode( void *iface, INT32 *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->binding_mode) ? impl->binding_mode : 1; /* OneWay */
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_Mode( void *iface, INT32 val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (impl) impl->binding_mode = (BYTE)val;
    return S_OK;
}

static HRESULT WINAPI uwp_binding_get_Source( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = impl ? impl->data_context : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_Source( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!impl) return S_OK;
    if (impl->data_context != val)
    {
        if (impl->data_context) IInspectable_Release( impl->data_context );
        impl->data_context = val;
        if (val) IInspectable_AddRef( val );
        impl->probe_flags = 0;
    }
    return S_OK;
}

static HRESULT WINAPI uwp_binding_get_ElementName( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *en = uwp_elem_placeholder( impl );
    if (!out) return E_POINTER;
    if (en[0]) return WindowsCreateString( en, (UINT32)wcslen( en ), out );
    *out = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_ElementName( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    const WCHAR *en = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (impl) uwp_elem_set_placeholder( impl, en );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_get_Converter( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->style_anim) ? impl->style_anim->converter : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_Converter( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_style_anim_ext *sa = impl ? uwp_elem_ensure_style_anim( impl ) : NULL;
    if (sa) uwp_replace_iface( &sa->converter, val );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_get_ConverterParameter( void *iface, IInspectable **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = (impl && impl->style_anim) ? impl->style_anim->converter_param : NULL;
    if (*out) IInspectable_AddRef( *out );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_ConverterParameter( void *iface, IInspectable *val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_style_anim_ext *sa = impl ? uwp_elem_ensure_style_anim( impl ) : NULL;
    if (sa) uwp_replace_iface( &sa->converter_param, val );
    return S_OK;
}

static HRESULT WINAPI uwp_binding_get_ConverterLanguage( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if (impl && impl->style_anim && impl->style_anim->converter_lang[0])
        return WindowsCreateString( impl->style_anim->converter_lang,
                                    (UINT32)wcslen( impl->style_anim->converter_lang ), out );
    *out = NULL;
    return S_OK;
}

static HRESULT WINAPI uwp_binding_put_ConverterLanguage( void *iface, HSTRING val )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    struct uwp_style_anim_ext *sa = impl ? uwp_elem_ensure_style_anim( impl ) : NULL;
    const WCHAR *s = val ? WindowsGetStringRawBuffer( val, NULL ) : L"";
    if (sa) lstrcpynW( sa->converter_lang, s ? s : L"", ARRAY_SIZE(sa->converter_lang) );
    return S_OK;
}

const struct uwp_binding_vtbl uwp_binding_vtbl =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_binding_get_Path,
    uwp_binding_put_Path,
    uwp_binding_get_Mode,
    uwp_binding_put_Mode,
    uwp_binding_get_Source,
    uwp_binding_put_Source,
    { [0 ... 1] = uwp_generic_noop },
    uwp_binding_get_ElementName,
    uwp_binding_put_ElementName,
    uwp_binding_get_Converter,
    uwp_binding_put_Converter,
    uwp_binding_get_ConverterParameter,
    uwp_binding_put_ConverterParameter,
    uwp_binding_get_ConverterLanguage,
    uwp_binding_put_ConverterLanguage,
    { [0 ... 9] = uwp_generic_noop },
};

static HRESULT WINAPI uwp_pp_get_Path( void *iface, HSTRING *out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    if (uwp_has_text( impl ))
    {
        const WCHAR *path = uwp_elem_text( impl );
        return WindowsCreateString( path, (UINT32)wcslen( path ), out );
    }
    *out = NULL;
    return S_OK;
}

const struct uwp_pp_vtbl uwp_pp_vtbl =
{
    uwp_sub_QueryInterface,
    uwp_sub_AddRef,
    uwp_sub_Release,
    uwp_sub_GetIids,
    uwp_sub_GetRuntimeClassName,
    uwp_sub_GetTrustLevel,
    uwp_pp_get_Path,
    { [0 ... 8] = uwp_generic_noop },
};

const void *uwp_rowcol_vtbl[32] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_rowcol_get_Length,
    [7] = uwp_rowcol_put_Length,
    [8 ... 31] = uwp_generic_noop,
};

struct uwp_element *uwp_nav_find_item( struct uwp_element *el, int depth )
{
    if (!el || depth > 6) return NULL;
    if (el->class_flags2 & UWP_CF2_NAV_VIEW_ITEM_CTRL)
        return el;
    if (!wcscmp( el->name, L"MenuItemsHost" ) && el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        if (v->count > 0)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[0] );
            if (ch) return ch;
        }
    }
    if (el->content)
    {
        struct uwp_element *r = uwp_nav_find_item( uwp_elem_from_any( el->content ), depth + 1 );
        if (r) return r;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *r = uwp_nav_find_item( uwp_elem_from_any( v->items[k] ), depth + 1 );
            if (r) return r;
        }
    }
    return NULL;
}

static void uwp_fire_flyout_closed( struct uwp_element *flyout )
{
    if (!flyout || UWP_EV_CNT( flyout, flyout_closed_reg ) == 0) return;
    uwp_fire_event_registry( &flyout->events->flyout_closed_reg,
                             flyout->outer ? flyout->outer : &flyout->IFlyout_iface,
                             NULL, "Flyout::Closed" );
}

BOOL uwp_popup_is_open_for( struct uwp_element *flyout )
{
    return flyout && g_popup_hwnd && IsWindow( g_popup_hwnd ) && g_popup_owner_el == flyout;
}

void uwp_close_flyout_popup( BOOL fire_closed )
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

BOOL uwp_popup_handle_escape( void )
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
    if (g_popup_owner_el)
    {
        uwp_close_flyout_popup( TRUE );
        return TRUE;
    }
    if (g_open_combobox && g_open_combobox->is_dropdown_open)
    {
        uwp_combobox_set_open( g_open_combobox, FALSE );
        return TRUE;
    }
    if (g_open_cdp && g_open_cdp->is_dropdown_open)
    {
        uwp_cdp_set_open( g_open_cdp, FALSE );
        return TRUE;
    }
    {
        struct uwp_element *pops[16];
        int pcnt = uwp_get_open_popups( pops, ARRAY_SIZE(pops) );
        if (pcnt > 0)
        {
            uwp_popup_set_open( pops[pcnt - 1], FALSE );
            return TRUE;
        }
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
                if (clicked_el->class_flags & UWP_CF_TOGGLE_MENU_ITEM)
                    clicked_el->is_checked = !clicked_el->is_checked;
                else if (clicked_el->class_flags & UWP_CF_RADIO_MENU_ITEM)
                {
                    int j;
                    for (j = 0; j < st->item_cnt; j++)
                        if (st->items[j].el && (st->items[j].el->class_flags & UWP_CF_RADIO_MENU_ITEM))
                            st->items[j].el->is_checked = 0;
                    clicked_el->is_checked = 1;
                }
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
                    if (clicked_el->class_flags & UWP_CF_TOGGLE_MENU_ITEM)
                        clicked_el->is_checked = !clicked_el->is_checked;
                    else if (clicked_el->class_flags & UWP_CF_RADIO_MENU_ITEM)
                    {
                        int j;
                        for (j = 0; j < st->item_cnt; j++)
                            if (st->items[j].el && (st->items[j].el->class_flags & UWP_CF_RADIO_MENU_ITEM))
                                st->items[j].el->is_checked = 0;
                        clicked_el->is_checked = 1;
                    }
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
    if (!flyout->children_coll && flyout->flyout)
    {
        struct uwp_element *inner_fl = uwp_elem_from_any( flyout->flyout );
        if (inner_fl) flyout = inner_fl;
    }
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
            const struct abi_menu_bar_item *ambi = (const struct abi_menu_bar_item *)mbi;
            void *vec = NULL;
            if (ambi->lpVtbl->get_Items && SUCCEEDED(ambi->lpVtbl->get_Items( mbi, &vec )) && vec)
            {
                const struct abi_winrt_vector *avec = (const struct abi_winrt_vector *)vec;
                UINT32 cnt = 0;
                if (avec->lpVtbl->get_Size && SUCCEEDED(avec->lpVtbl->get_Size( vec, &cnt )))
                {
                    for (k = 0; k < cnt && raw_cnt < ARRAY_SIZE(raw_items); k++)
                    {
                        void *item = NULL;
                        if (avec->lpVtbl->GetAt && SUCCEEDED(avec->lpVtbl->GetAt( vec, k, &item )) && item)
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
        WNDCLASSEXW wc = { .cbSize = sizeof(wc) };
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
        if (!ch || ch->visibility != 0 || uwp_is_accelerator( ch )) continue;

        it = &st->items[st->item_cnt++];
        it->el = ch;
        it->y = cur_y;
        if (uwp_is_separator( ch ))
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
            it->has_sub = ((ch->class_flags & UWP_CF_MENU_FLYOUT_SUB_ITEM) != 0);
            lstrcpynW( it->text, uwp_elem_text( ch ), ARRAY_SIZE(it->text) );
            lstrcpynW( it->accel, uwp_elem_accel_text( ch ), ARRAY_SIZE(it->accel) );
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
                            lstrcpynW( kname, L"Plus", ARRAY_SIZE(kname) );
                        else if (ka->accel_key == VK_SUBTRACT || ka->accel_key == 0xbd)
                            lstrcpynW( kname, L"Minus", ARRAY_SIZE(kname) );
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
            MONITORINFO mi = { .cbSize = sizeof(mi) };
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

void uwp_show_flyout_popup( struct uwp_element *flyout, HWND owner_hwnd, int screen_x, int screen_y )
{
    uwp_close_flyout_popup( FALSE );
    g_popup_owner_el = flyout;
    if (uwp_is_menubar_item( flyout ))
        g_popup_hwnd = uwp_create_flyout_popup_internal( flyout, flyout, owner_hwnd, NULL, screen_x, screen_y );
    uwp_invalidate_window();
}

/* ============================================================================
 * 8. UWP Helper & Statics Factories (XamlBindingHelper, VisualStateManager, etc.)
 * ============================================================================ */

const GUID IID_IXamlReaderStatics_UWP =
    { 0x9891c6bd, 0x534f, 0x4955, { 0xb8, 0x5a, 0x8a, 0x8d, 0xc0, 0xdc, 0xa6, 0x02 } };
