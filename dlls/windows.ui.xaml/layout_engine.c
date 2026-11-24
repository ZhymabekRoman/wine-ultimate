/* WinRT Windows.UI.Xaml Pure Measure & Arrange Layout Engine
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
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

BOOL uwp_is_bg_pid( WORD pid )
{
    return pid == XBF_PROP_CONTROL_BACKGROUND || pid == XBF_PROP_PANEL_BACKGROUND || pid == XBF_PROP_BORDER_BACKGROUND || pid == XBF_PROP_SHAPE_FILL;
}

BOOL uwp_is_fg_pid( WORD pid )
{
    return pid == XBF_PROP_CONTROL_FOREGROUND || pid == XBF_PROP_TEXTBLOCK_FOREGROUND || pid == XBF_PROP_ICONELEMENT_FOREGROUND || pid == XBF_PROP_TEXTELEMENT_FOREGROUND;
}

BOOL uwp_is_border_pid( WORD pid )
{
    return pid == XBF_PROP_CONTROL_BORDER_BRUSH || pid == XBF_PROP_BORDER_BRUSH ||
           pid == XBF_PROP_GRID_BORDER_BRUSH || pid == XBF_PROP_SHAPE_STROKE ||
           pid == 0x869d; /* StackPanel.BorderBrush */
}

size_t uwp_flatten_inlines_text( const struct uwp_element *el, WCHAR *out_buf, size_t out_cap )
{
    struct uwp_vec_view *v;
    unsigned int i, j;
    size_t pos = 0;

    if (!out_buf || out_cap < 2) return 0;
    out_buf[0] = 0;
    if (!el || !el->inlines_coll) return 0;

    v = CONTAINING_RECORD( el->inlines_coll, struct uwp_vec_view, IInspectable_iface );
    for (i = 0; v && i < v->count && pos + 2 < out_cap; i++)
    {
        struct uwp_element *blk = uwp_elem_from_any( v->items[i] );
        BOOL is_para;
        if (!blk) continue;
        is_para = wcsstr( blk->class_name, L"Paragraph" ) != NULL ||
                  wcsstr( blk->class_name, L"LineBreak" ) != NULL;
        if (blk->text[0])
        {
            if (is_para && pos > 0 && out_buf[pos - 1] != '\n') out_buf[pos++] = '\n';
            lstrcpynW( out_buf + pos, blk->text, (int)(out_cap - pos) );
            pos += wcslen( out_buf + pos );
        }
        else if (blk->inlines_coll)
        {
            struct uwp_vec_view *iv = CONTAINING_RECORD( blk->inlines_coll, struct uwp_vec_view, IInspectable_iface );
            if (is_para && pos > 0 && out_buf[pos - 1] != '\n') out_buf[pos++] = '\n';
            for (j = 0; iv && j < iv->count && pos + 2 < out_cap; j++)
            {
                struct uwp_element *inl = uwp_elem_from_any( iv->items[j] );
                if (!inl && iv->items[j])
                {
                    static const GUID IID_IRun_g =
                        { 0x59553c83, 0x0e14, 0x49bd, { 0xb8, 0x4b, 0xc5, 0x26, 0xf3, 0x03, 0x43, 0x49 } };
                    void *run_iface = NULL;
                    if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)iv->items[j], &IID_IRun_g, &run_iface )) && run_iface)
                    {
                        HSTRING htxt = NULL;
                        HRESULT (WINAPI *get_txt)(void *, HSTRING *) = (void *)(*(void ***)run_iface)[6];
                        if (get_txt && SUCCEEDED(get_txt( run_iface, &htxt )) && htxt)
                        {
                            const WCHAR *s = WindowsGetStringRawBuffer( htxt, NULL );
                            if (s && s[0])
                            {
                                lstrcpynW( out_buf + pos, s, (int)(out_cap - pos) );
                                pos += wcslen( out_buf + pos );
                            }
                            WindowsDeleteString( htxt );
                        }
                        IUnknown_Release( (IUnknown *)run_iface );
                    }
                    else
                    {
                        static const GUID IID_ISpan_g =
                            { 0x54f03373, 0xc273, 0x4f3b, { 0xb3, 0x71, 0x6e, 0x2b, 0x4b, 0x2a, 0x96, 0x91 } };
                        void *span_iface = NULL;
                        if (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)iv->items[j], &IID_ISpan_g, &span_iface )) && span_iface)
                        {
                            void *sub_inlines = NULL;
                            HRESULT (WINAPI *get_inlines)(void *, void **) = (void *)(*(void ***)span_iface)[6];
                            if (get_inlines && SUCCEEDED(get_inlines( span_iface, &sub_inlines )) && sub_inlines)
                            {
                                struct uwp_vec_view *sv = CONTAINING_RECORD( sub_inlines, struct uwp_vec_view, IInspectable_iface );
                                unsigned int k;
                                for (k = 0; sv && k < sv->count && pos + 2 < out_cap; k++)
                                {
                                    struct uwp_element *sub = uwp_elem_from_any( sv->items[k] );
                                    if (sub && sub->text[0])
                                    {
                                        lstrcpynW( out_buf + pos, sub->text, (int)(out_cap - pos) );
                                        pos += wcslen( out_buf + pos );
                                    }
                                }
                                IInspectable_Release( (IInspectable *)sub_inlines );
                            }
                            IUnknown_Release( (IUnknown *)span_iface );
                        }
                    }
                }
                else if (inl && inl->text[0])
                {
                    lstrcpynW( out_buf + pos, inl->text, (int)(out_cap - pos) );
                    pos += wcslen( out_buf + pos );
                }
                else if (inl && inl->inlines_coll)
                {
                    struct uwp_vec_view *sv = CONTAINING_RECORD( inl->inlines_coll, struct uwp_vec_view, IInspectable_iface );
                    unsigned int k;
                    for (k = 0; sv && k < sv->count && pos + 2 < out_cap; k++)
                    {
                        struct uwp_element *sub = uwp_elem_from_any( sv->items[k] );
                        if (sub && sub->text[0])
                        {
                            lstrcpynW( out_buf + pos, sub->text, (int)(out_cap - pos) );
                            pos += wcslen( out_buf + pos );
                        }
                    }
                }
            }
        }
    }
    return pos;
}

struct uwp_size_f
{
    float width;
    float height;
};

static void uwp_invoke_winui_measure_arrange( struct uwp_element *el, int w, int h )
{
    void *fe_ovr = NULL;
    struct uwp_size_f avail, desired = {0}, arranged = {0};
    HRESULT hr;

    if (!el || !el->outer || el->in_measure_arrange || w <= 0) return;
    if (!wcsstr( el->class_name, L"ItemsRepeater" )) return;
    if (h <= 0) h = 204;
    if (el->last_measured_w == (float)w && el->last_measured_h == (float)h && el->children_coll &&
        CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count > 0)
        return;

    el->in_measure_arrange = TRUE;
    el->last_measured_w = (float)w;
    el->last_measured_h = (float)h;
    avail.width = (float)w;
    avail.height = (float)h;

    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IFrameworkElementOverrides_UWP, &fe_ovr )) && fe_ovr)
    {
        HRESULT (WINAPI *measure_ovr)(void *, struct uwp_size_f, struct uwp_size_f *) = (void *)(*(void ***)fe_ovr)[6];
        HRESULT (WINAPI *arrange_ovr)(void *, struct uwp_size_f, struct uwp_size_f *) = (void *)(*(void ***)fe_ovr)[7];
        __TRY
        {
            if (measure_ovr)
            {
                hr = measure_ovr( fe_ovr, avail, &desired );
                if (desired.width <= 1.5f && desired.height <= 1.5f && el->evc_cnt > 0)
                {
                    el->evc_fired = FALSE;
                    uwp_fire_evc_if_needed( el );
                    hr = measure_ovr( fe_ovr, avail, &desired );
                }
                if (w >= 480)
                {
                    WARN( "MeasureOverride(%s %s %dx%d) -> hr %#lx desired=(%.0f,%.0f) kids=%u dp_cnt=%d\n",
                          debugstr_w(el->class_name), debugstr_w(el->name), w, h, hr,
                          desired.width, desired.height,
                          el->children_coll ? CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count : 0,
                          el->dp_cnt );
                    if ((desired.width == 1.0f && desired.height == 1.0f) || !wcscmp( el->name, L"TileList" ))
                    {
                        int di;
                        for (di = 0; di < el->dp_cnt && di < 8; di++)
                        {
                            HSTRING hc = NULL;
                            UINT32 vsz = 0xffffffffu;
                            struct uwp_element *de = uwp_elem_from_any( el->dp_store[di].value );
                            if (el->dp_store[di].value)
                            {
                                IInspectable_GetRuntimeClassName( el->dp_store[di].value, &hc );
                                vsz = uwp_winrt_vec_count( el->dp_store[di].value );
                            }
                            WARN( "  dp[%d] val=%p cls=%s uwp=%s sz=%u\n", di,
                                  el->dp_store[di].value,
                                  hc ? debugstr_w(WindowsGetStringRawBuffer(hc, NULL)) : "\"null\"",
                                  de ? debugstr_w(de->class_name) : "\"-\"", vsz );
                            if (hc) WindowsDeleteString( hc );
                        }
                    }
                }
            }
            if (arrange_ovr)
            {
                hr = arrange_ovr( fe_ovr, avail, &arranged );
                TRACE( "ArrangeOverride(%s %s %dx%d) -> hr %#lx arranged=(%.0f,%.0f)\n",
                       debugstr_w(el->class_name), debugstr_w(el->name), w, h, hr, arranged.width, arranged.height );
            }
        }
        __EXCEPT_ALL
        {
            WARN( "Measure/ArrangeOverride exception on %s (%s)\n", debugstr_w(el->class_name), debugstr_w(el->name) );
        }
        __ENDTRY;
        IUnknown_Release( (IUnknown *)fe_ovr );
    }
    el->in_measure_arrange = FALSE;

    if (el->loaded_fired && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && (!ch->loaded_fired || ch->loaded_fired_cnt < ch->loaded_cnt))
                uwp_fire_loaded_tree( ch );
        }
    }
}

static BOOL uwp_has_shimmer_descendant( struct uwp_element *el, int depth )
{
    if (!el || depth > 8) return FALSE;
    if (wcsstr( el->class_name, L"Shimmer" )) return TRUE;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_has_shimmer_descendant( ch, depth + 1 )) return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_has_shimmer_descendant( ch, depth + 1 )) return TRUE;
        }
    }
    return FALSE;
}

static BOOL uwp_has_spotlight_descendant( struct uwp_element *el, int depth )
{
    if (!el || el->visibility != 0 || depth > 10) return FALSE;
    if (wcsstr( el->class_name, L"DynamicSpotlightControl" ) ||
        wcsstr( el->class_name, L"ScrollingSpotlightControl" ) ||
        wcsstr( el->class_name, L"CarrouselSpotlightControl" ) ||
        wcsstr( el->class_name, L"ScrollingPreviewListView" ) ||
        wcsstr( el->class_name, L"SpotlightHeroImage" ))
        return TRUE;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_has_spotlight_descendant( ch, depth + 1 )) return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_has_spotlight_descendant( ch, depth + 1 )) return TRUE;
        }
    }
    return FALSE;
}

static BOOL uwp_is_home_title_panel( struct uwp_element *el, int depth )
{
    if (!el || depth > 4) return FALSE;
    if (!wcscmp( el->name, L"HeaderTitle" ) && !wcscmp( el->text, L"Home" )) return TRUE;
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_is_home_title_panel( ch, depth + 1 )) return TRUE;
        }
    }
    return FALSE;
}

static BOOL uwp_scroll_child_has_content( struct uwp_element *el, int depth )
{
    if (!el || el->visibility != 0 || depth > 14) return FALSE;
    if (depth == 0 && uwp_is_home_title_panel( el, 0 )) return FALSE;
    if (uwp_has_spotlight_descendant( el, 0 )) return TRUE;
    if (uwp_has_shimmer_descendant( el, 0 ) && el->kind == UWP_KIND_BORDER) return FALSE;
    if (el->text[0] || el->inlines_coll || el->img_pixels || el->img_loaded) return TRUE;
    if (wcsstr( el->class_name, L"ItemsRepeater" ))
    {
        int di;
        if (el->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            if (v->count > 0) return TRUE;
        }
        for (di = 0; di < el->dp_cnt; di++)
        {
            if (el->dp_store[di].value && uwp_winrt_vec_count( el->dp_store[di].value ) > 0)
                return TRUE;
        }
        return FALSE;
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_scroll_child_has_content( ch, depth + 1 )) return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_scroll_child_has_content( ch, depth + 1 )) return TRUE;
        }
    }
    return FALSE;
}

static void uwp_arrange_element_inner( struct uwp_element *el, RECT rc );

void uwp_arrange_element( struct uwp_element *el, RECT rc )
{
    static int depth = 0;
    if (!el || depth >= 48) return;
    depth++;
    uwp_arrange_element_inner( el, rc );
    depth--;
}

static double uwp_estimate_auto_height( struct uwp_element *el, int depth )
{
    double best = 0.0;
    if (!el || depth > 12 || el->visibility != 0) return 0.0;
    if (el->kind == UWP_KIND_DEFERRED || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_RESOURCE_DICT ||
        el->kind == UWP_KIND_COLOR || el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND)
        return 0.0;
    if (el->height > 0.0 && !isnan( el->height ))
        return el->height + max( 0.0, el->margin_t ) + max( 0.0, el->margin_b );
    if (el->min_height > 0.0 && !isnan( el->min_height ))
        best = el->min_height;
    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
        double th = max( 18.0 * g_layout_scale, fs * 1.38 + 4.0 );
        if (th > best) best = th;
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
        {
            double ch_h = uwp_estimate_auto_height( ch, depth + 1 );
            if (ch_h > best) best = ch_h;
        }
    }
    if (el->kind == UWP_KIND_GRID && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        struct uwp_vec_view *rows = el->row_defs_coll ? CONTAINING_RECORD( el->row_defs_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        int nrows = (rows && rows->count > 0) ? min( (int)rows->count, 32 ) : 1;
        double sum_h = 0.0;
        int r;
        unsigned int k;
        for (r = 0; r < nrows; r++)
        {
            struct uwp_element *rd = (rows && (unsigned)r < rows->count) ? uwp_elem_from_any( rows->items[r] ) : NULL;
            double rh = 0.0;
            BOOL r_vis = FALSE;
            if (rd && rd->grid_len_type == 1)
            {
                rh = rd->grid_len_val;
            }
            else
            {
                for (k = 0; k < children->count; k++)
                {
                    struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                    if (ch && ch != el && ch->visibility == 0 && ch->grid_row == r && ch->grid_row_span == 1)
                    {
                        double ch_h = uwp_estimate_auto_height( ch, depth + 1 );
                        r_vis = TRUE;
                        if (ch_h > rh) rh = ch_h;
                    }
                }
                if (r_vis && rh < 36.0 * g_layout_scale) rh = 36.0 * g_layout_scale;
            }
            sum_h += rh;
        }
        if (sum_h > best) best = sum_h;
    }
    else if (el->kind == UWP_KIND_STACK_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        double acc = 0.0;
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && ch->visibility == 0)
            {
                double ch_h = uwp_estimate_auto_height( ch, depth + 1 );
                if (ch_h <= 0.0 && (ch->kind == UWP_KIND_BUTTON || ch->kind == UWP_KIND_TEXT_BLOCK))
                    ch_h = 36.0 * g_layout_scale;
                if (el->orientation == 1) { if (ch_h > acc) acc = ch_h; }
                else acc += ch_h;
            }
        }
        if (acc > best) best = acc;
    }
    else if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && ch->visibility == 0)
            {
                double ch_h = uwp_estimate_auto_height( ch, depth + 1 );
                if (ch_h > best) best = ch_h;
            }
        }
    }
    if (el->max_height > 0.0 && !isnan( el->max_height ) && best > el->max_height)
        best = el->max_height;
    if (best > 0.0)
        best += max( 0.0, el->margin_t ) + max( 0.0, el->margin_b );
    return best;
}

static BOOL uwp_apply_margins_clamped( const struct uwp_element *el, RECT *rc )
{
    int orig_h = rc->bottom - rc->top;
    int mt = (int)el->margin_t;
    int mb = (int)el->margin_b;
    BOOL clamped = FALSE;

    if (orig_h >= 200 && mt > 200 && mt + mb >= orig_h)
    {
        mt = orig_h / 3;
        clamped = TRUE;
    }
    rc->left   += (int)el->margin_l;
    rc->top    += mt;
    rc->right  -= (int)el->margin_r;
    rc->bottom -= mb;
    if (rc->right < rc->left) rc->right = rc->left;
    if (rc->bottom < rc->top) rc->bottom = rc->top;
    return clamped;
}

BOOL uwp_is_combobox( const struct uwp_element *el )
{
    return el && wcsstr( el->class_name, L"ComboBox" ) && !wcsstr( el->class_name, L"ComboBoxItem" );
}

BOOL uwp_is_nav_view( const struct uwp_element *el )
{
    return el && (!wcscmp( el->class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ) ||
                  !wcscmp( el->class_name, L"Windows.UI.Xaml.Controls.NavigationView" ) ||
                  !wcscmp( el->default_style_key_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ) ||
                  !wcscmp( el->default_style_key_name, L"Windows.UI.Xaml.Controls.NavigationView" ));
}

BOOL uwp_is_nav_header( const struct uwp_element *el )
{
    return el && (wcsstr( el->class_name, L"NavigationViewItemHeader" ) != NULL ||
                  wcsstr( el->default_style_key_name, L"NavigationViewItemHeader" ) != NULL);
}

BOOL uwp_is_nav_separator( const struct uwp_element *el )
{
    return el && (wcsstr( el->class_name, L"NavigationViewItemSeparator" ) != NULL ||
                  wcsstr( el->default_style_key_name, L"NavigationViewItemSeparator" ) != NULL);
}

int uwp_nav_effective_display_mode( const struct uwp_element *nav, int avail_w )
{
    double scale, w_dip;
    if (!nav) return 1;
    switch (nav->nav_pane_display_mode)
    {
    case 4: /* LeftMinimal */
    case 2: /* Top */
        return 0; /* Minimal (no left rail when closed) */
    case 3: /* LeftCompact */
        return 1; /* Compact */
    case 1: /* Left */
        scale = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        w_dip = (double)avail_w / scale;
        return (w_dip >= nav->nav_expanded_threshold) ? 2 : 1;
    default: /* 0 = Auto */
        if (avail_w <= 0)
            return nav->nav_display_mode_set ? nav->nav_display_mode : 1;
        scale = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        w_dip = (double)avail_w / scale;
        if (w_dip < nav->nav_compact_threshold) return 0;
        if (w_dip < nav->nav_expanded_threshold) return 1;
        return nav->nav_is_pane_open ? 2 : 1;
    }
}

BOOL uwp_nav_is_overlay_open( const struct uwp_element *nav )
{
    if (!nav || !uwp_is_nav_view( nav ) || !nav->nav_is_pane_open) return FALSE;
    return uwp_nav_effective_display_mode( nav, nav->bounds.right - nav->bounds.left ) != 2;
}

static void uwp_clear_bounds_tree( struct uwp_element *el )
{
    if (!el) return;
    el->has_bounds = FALSE;
    SetRectEmpty( &el->bounds );
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_clear_bounds_tree( ch );
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_clear_bounds_tree( ch );
    }
}

static double uwp_measure_natural_width( const struct uwp_element *el, int depth )
{
    double best = 0.0;
    if (!el || depth > 10 || el->visibility != 0) return 0.0;
    if (el->kind == UWP_KIND_DEFERRED || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_RESOURCE_DICT ||
        el->kind == UWP_KIND_COLOR || el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND)
        return 0.0;
    if (el->width > 0.0 && !isnan( el->width ))
        best = el->width;
    else if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        WCHAR rich_buf[2048];
        const WCHAR *txt = el->text;
        double raw_fs = (el->font_size > 0.0 ? el->font_size : 14.0);
        int eff_fw = el->font_weight;
        BOOL is_sec_hdr = (!wcscmp( el->name, L"SectionTitle" ));
        const struct uwp_element *anc;
        int hop;
        double fs;
        double cw_factor;
        size_t len;
        for (anc = el->parent, hop = 0; anc && hop < 3; anc = anc->parent, hop++)
        {
            if (anc->kind == UWP_KIND_BUTTON || anc->kind == UWP_KIND_TOGGLE_BUTTON || anc->kind == UWP_KIND_CONTENT_CONTROL)
            {
                if (!el->has_font_size && anc->has_font_size && anc->font_size > 0.0)
                    raw_fs = anc->font_size;
                if (eff_fw <= 0 && anc->font_weight > 0)
                    eff_fw = anc->font_weight;
                if (!wcscmp( anc->name, L"ShowAllButton" ) || !wcscmp( anc->name, L"SectionViewAllButton" ))
                    is_sec_hdr = TRUE;
                break;
            }
        }
        if (is_sec_hdr)
        {
            if (raw_fs < 18.0) raw_fs = 18.0;
            if (eff_fw < FW_SEMIBOLD) eff_fw = FW_SEMIBOLD;
        }
        cw_factor = (eff_fw >= FW_SEMIBOLD) ? 0.66 : 0.60;
        if (!txt[0] && el->inlines_coll && uwp_flatten_inlines_text( el, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
        {
            struct uwp_vec_view *iv = CONTAINING_RECORD( el->inlines_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int ik;
            txt = rich_buf;
            for (ik = 0; iv && ik < iv->count; ik++)
            {
                struct uwp_element *inl = uwp_elem_from_any( iv->items[ik] );
                if (inl && inl->has_font_size && inl->font_size > raw_fs)
                    raw_fs = inl->font_size;
            }
        }
        fs = raw_fs * g_layout_scale;
        len = wcslen( txt );
        if (len > 0)
            best = (double)len * fs * cw_factor + (is_sec_hdr ? 44.0 : 20.0);
    }
    else if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el)
            {
                double cw = uwp_measure_natural_width( ch, depth + 1 );
                if (cw > 0.0) best = cw + el->padding_l + el->padding_r + 16.0;
            }
        }
        if (best <= 0.0 && el->img_w > 0 && el->img_h > 0)
        {
            double th = (el->height > 0.0 && !isnan( el->height )) ? el->height : min( 160.0 * g_layout_scale, (double)el->img_h );
            best = max( 120.0 * g_layout_scale, min( 320.0 * g_layout_scale, th * (double)el->img_w / (double)el->img_h ) );
        }
        if (best <= 0.0)
        {
            double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
            size_t len = wcslen( el->text );
            best = max( 80.0 * g_layout_scale, (double)len * fs * 0.64 + 32.0 + el->padding_l + el->padding_r );
        }
    }
    else if (uwp_is_combobox( el ))
    {
        best = 160.0 * g_layout_scale;
    }
    else if (el->kind == UWP_KIND_IMAGE)
    {
        if (el->img_w > 0) best = (double)el->img_w;
    }
    else if (el->kind == UWP_KIND_STACK_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k, vis = 0;
        double acc = 0.0;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double cw;
            if (!ch || ch == el || ch->visibility != 0) continue;
            cw = uwp_measure_natural_width( ch, depth + 1 );
            if (cw > 0.0)
            {
                cw += max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
                if (el->orientation == 1) acc += cw;
                else if (cw > acc) acc = cw;
                vis++;
            }
        }
        if (el->orientation == 1 && vis > 0 && el->spacing > 0.0)
        {
            struct uwp_element *ch0 = (children->count > 0) ? uwp_elem_from_any( children->items[0] ) : NULL;
            unsigned int gaps = (vis > 1) ? (vis - 1) : 0;
            if (ch0 && ch0 != el && ch0->visibility != 0)
                gaps++;
            acc += (double)gaps * el->spacing;
        }
        best = acc;
    }
    else if (el->kind == UWP_KIND_BORDER || el->kind == UWP_KIND_CONTENT_CONTROL || el->kind == UWP_KIND_USER_CONTROL)
    {
        if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el)
            {
                double cw = uwp_measure_natural_width( ch, depth + 1 );
                if (cw > 0.0) best = cw + el->padding_l + el->padding_r;
            }
        }
        else if (el->text[0])
        {
            double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
            best = (double)wcslen( el->text ) * fs * 0.56 + 24.0 + el->padding_l + el->padding_r;
        }
    }
    else if (el->kind == UWP_KIND_WRAP_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k, vis = 0;
        double acc = 0.0;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double cw;
            if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED) continue;
            cw = uwp_measure_natural_width( ch, depth + 1 );
            if (cw > 0.0)
            {
                acc += cw + max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
                vis++;
            }
        }
        if (vis > 1 && el->spacing > 0.0) acc += (double)(vis - 1) * el->spacing;
        best = acc;
    }
    else if ((el->kind == UWP_KIND_GRID || el->kind == UWP_KIND_GENERIC) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double cw = (ch && ch != el) ? uwp_measure_natural_width( ch, depth + 1 ) : 0.0;
            if (cw > 0.0)
            {
                cw += max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
                if (cw > best) best = cw;
            }
        }
    }
    else if (wcsstr( el->class_name, L"FontIcon" ))
    {
        double fs = (el->has_font_size ? el->font_size : 16.0) * g_layout_scale;
        best = max( 20.0 * g_layout_scale, fs + 8.0 );
    }
    else if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
            best = uwp_measure_natural_width( ch, depth + 1 );
    }
    if (el->min_width > 0.0 && !isnan( el->min_width ) && best < el->min_width)
        best = el->min_width;
    if (el->max_width > 0.0 && !isnan( el->max_width ) && best > el->max_width)
        best = el->max_width;
    return best;
}

static double uwp_measure_natural_height( const struct uwp_element *el, int avail_w, int depth )
{
    double best = 0.0;
    if (!el || depth > 10 || el->visibility != 0) return 0.0;
    if (el->kind == UWP_KIND_DEFERRED || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_RESOURCE_DICT ||
        el->kind == UWP_KIND_COLOR || el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND)
        return 0.0;
    if (el->height > 0.0 && !isnan( el->height ))
        return el->height;
    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
        double line_h = max( 20.0 * g_layout_scale, fs * 1.45 );
        int lines = 1;
        WCHAR rich_buf[2048];
        const WCHAR *txt = el->text;
        if (!txt[0] && el->inlines_coll && uwp_flatten_inlines_text( el, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
            txt = rich_buf;
        if (txt[0])
        {
            const WCHAR *p;
            int nl_lines = 1;
            for (p = txt; *p; p++) if (*p == L'\n') nl_lines++;
            lines = nl_lines;
            if (el->text_wrapping >= 2 && avail_w > 32)
            {
                double est_w = (double)wcslen( txt ) * fs * 0.60;
                double max_w = (double)avail_w - 16.0;
                int wrap_lines;
                if (el->max_width > 0.0 && el->max_width < max_w) max_w = el->max_width;
                if (max_w > 32.0 && est_w > max_w)
                {
                    wrap_lines = min( 32, (int)ceil( est_w / max_w ) );
                    if (wrap_lines > lines) lines = wrap_lines;
                }
            }
        }
        if (el->max_lines > 0 && lines > el->max_lines)
            lines = el->max_lines;
        best = line_h * lines + 4.0;
    }
    else if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        if (el->img_w > 0 && el->img_h > 0)
        {
            double aw = (avail_w > 0) ? (double)avail_w : 240.0 * g_layout_scale;
            best = min( 180.0 * g_layout_scale, max( 96.0 * g_layout_scale, aw * (double)el->img_h / (double)el->img_w ) );
        }
        else if (el->image_src[0])
        {
            best = 140.0 * g_layout_scale;
        }
        else if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el)
            {
                double ch_h = uwp_measure_natural_height( ch, avail_w, depth + 1 );
                if (ch_h > 0.0) best = ch_h + max( 0.0, el->padding_t ) + max( 0.0, el->padding_b );
            }
        }
        if (best <= 0.0)
        {
            double pad_v = max( 0.0, el->padding_t ) + max( 0.0, el->padding_b );
            double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
            best = max( 38.0 * g_layout_scale, fs * 1.4 + pad_v + 12.0 * g_layout_scale );
        }
    }
    else if (uwp_is_combobox( el ))
    {
        best = 36.0 * g_layout_scale;
    }
    else if (el->kind == UWP_KIND_IMAGE)
    {
        if (el->img_h > 0) best = (double)el->img_h;
    }
    else if (el->kind == UWP_KIND_STACK_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k, vis = 0;
        double acc = 0.0;
        int inner_w = avail_w;
        if (el->width > 0.0 && !isnan( el->width ) && (int)el->width < inner_w)
            inner_w = (int)el->width;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double ch_h;
            if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED ||
                ch->kind == UWP_KIND_ACCELERATOR || ch->kind == UWP_KIND_XAML_COMMAND ||
                wcsstr( ch->class_name, L"TeachingTip" ) || !wcscmp( ch->name, L"TeachingTipTarget" ))
                continue;
            ch_h = uwp_measure_natural_height( ch, inner_w, depth + 1 );
            if (ch_h <= 0.0)
                ch_h = uwp_estimate_auto_height( ch, depth + 1 );
            if (ch_h <= 0.0) continue;
            ch_h += max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b );
            if (el->orientation == 1)
            {
                if (ch_h > acc) acc = ch_h;
            }
            else
            {
                acc += ch_h;
            }
            vis++;
        }
        if (el->orientation == 0 && vis > 1 && el->spacing > 0.0)
            acc += (double)(vis - 1) * el->spacing;
        best = acc;
    }
    else if (el->kind == UWP_KIND_WRAP_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        double cur_x = 0.0, row_h = 0.0, total_h = 0.0;
        double lim_w = (avail_w > 0) ? (double)avail_w : 600.0;
        double sp = (el->spacing > 0.0) ? el->spacing : 4.0;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double cw, ch_h;
            if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED) continue;
            cw = uwp_measure_natural_width( ch, depth + 1 );
            if (cw <= 0.0) cw = 80.0;
            cw += max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
            ch_h = uwp_measure_natural_height( ch, (int)cw, depth + 1 );
            if (ch_h <= 0.0) ch_h = 32.0;
            ch_h += max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b );
            if (cur_x > 0.0 && cur_x + cw > lim_w)
            {
                total_h += row_h + sp;
                cur_x = 0.0;
                row_h = 0.0;
            }
            cur_x += cw + sp;
            if (ch_h > row_h) row_h = ch_h;
        }
        total_h += row_h;
        best = total_h;
    }
    else if (el->kind == UWP_KIND_GRID && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        struct uwp_vec_view *rows = el->row_defs_coll ? CONTAINING_RECORD( el->row_defs_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        int nrows = (rows && rows->count > 0) ? min( (int)rows->count, 30 ) : 1;
        double row_h[32] = {0};
        unsigned int k;
        int r;
        if (rows)
        {
            for (r = 0; r < nrows; r++)
            {
                struct uwp_element *rd = uwp_elem_from_any( rows->items[r] );
                if (rd && rd->grid_len_type == 1 && rd->grid_len_val > 0.0)
                    row_h[r] = rd->grid_len_val;
            }
        }
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double ch_h;
            int cr;
            if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED ||
                ch->kind == UWP_KIND_STYLE || ch->kind == UWP_KIND_BRUSH ||
                ch->kind == UWP_KIND_FLYOUT || ch->kind == UWP_KIND_POPUP ||
                ch->kind == UWP_KIND_ACCELERATOR || ch->kind == UWP_KIND_XAML_COMMAND)
                continue;
            cr = (ch->grid_row >= 0 && ch->grid_row < nrows) ? ch->grid_row : 0;
            if (ch->grid_row_span > 1) continue;
            ch_h = uwp_measure_natural_height( ch, avail_w, depth + 1 );
            if (ch_h > 0.0)
            {
                ch_h += max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b );
                if (ch_h > row_h[cr]) row_h[cr] = ch_h;
            }
        }
        for (r = 0; r < nrows; r++)
            best += row_h[r];
    }
    else if ((el->kind == UWP_KIND_BORDER || el->kind == UWP_KIND_CONTENT_CONTROL ||
              el->kind == UWP_KIND_USER_CONTROL || el->kind == UWP_KIND_GENERIC) &&
             (el->content || el->children_coll))
    {
        if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el)
            {
                double ch_h = uwp_measure_natural_height( ch, avail_w, depth + 1 );
                if (ch_h > 0.0) best = ch_h + el->padding_t + el->padding_b;
            }
        }
        if (best <= 0.0 && el->children_coll)
        {
            struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                double ch_h;
                if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED) continue;
                ch_h = uwp_measure_natural_height( ch, avail_w, depth + 1 );
                if (ch_h > 0.0)
                {
                    ch_h += max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b );
                    if (ch_h > best) best = ch_h;
                }
            }
            if (best > 0.0) best += el->padding_t + el->padding_b;
        }
    }
    if (el->min_height > 0.0 && !isnan( el->min_height ) && best < el->min_height)
        best = el->min_height;
    if (el->max_height > 0.0 && !isnan( el->max_height ) && best > el->max_height)
        best = el->max_height;
    return best;
}

static int uwp_align_axis( LONG *start, LONG *end, double explicit_size, int align )
{
    int span = *end - *start;
    if (explicit_size > 0.0 && !isnan( explicit_size ))
    {
        int es = (int)(explicit_size + 0.5);
        if (es < span)
        {
            if (align == 1 || align == 3) *start += (span - es) / 2;
            else if (align == 2) *start = *end - es;
            *end = *start + es;
            return es;
        }
    }
    return span;
}

static void uwp_arrange_element_inner( struct uwp_element *el, RECT rc )
{
    int w, h;
    double eff_w, eff_h;
    BOOL margin_clamped;
    if (!el) return;
    if (el->visibility != 0 || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_DEFERRED ||
        el->kind == UWP_KIND_RESOURCE_DICT || el->kind == UWP_KIND_COLOR ||
        el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND ||
        (wcsstr( el->class_name, L"TeachingTip" ) && !el->is_open) ||
        !wcscmp( el->name, L"TeachingTipTarget" ))
    {
        el->has_bounds = FALSE;
        return;
    }

    margin_clamped = uwp_apply_margins_clamped( el, &rc );
    eff_w = el->width;
    if ((eff_w <= 0.0 || isnan( eff_w )) && el->h_align != 3 &&
        (el->kind == UWP_KIND_STACK_PANEL || el->kind == UWP_KIND_BORDER ||
         el->kind == UWP_KIND_USER_CONTROL || el->kind == UWP_KIND_CONTENT_CONTROL ||
         el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
         el->kind == UWP_KIND_GENERIC || el->kind == UWP_KIND_WRAP_PANEL ||
         uwp_is_combobox( el )))
    {
        double nw = uwp_measure_natural_width( el, 0 );
        if (nw > 0.0) eff_w = nw;
    }
    if (el->max_width > 0.0 && !isnan( el->max_width ) && (eff_w <= 0.0 || eff_w > el->max_width))
    {
        if (rc.right - rc.left > (LONG)el->max_width)
            eff_w = el->max_width;
    }
    w = uwp_align_axis( &rc.left, &rc.right, eff_w, el->h_align );

    eff_h = el->height;
    if ((eff_h <= 0.0 || isnan( eff_h )) && el->v_align != 3 &&
        (el->kind == UWP_KIND_STACK_PANEL || el->kind == UWP_KIND_TEXT_BLOCK ||
         el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
         el->kind == UWP_KIND_BORDER || uwp_is_combobox( el )))
    {
        double nh = uwp_measure_natural_height( el, w, 0 );
        if (nh > 0.0) eff_h = nh;
    }
    if (el->max_height > 0.0 && !isnan( el->max_height ) && (eff_h <= 0.0 || eff_h > el->max_height))
    {
        if (rc.bottom - rc.top > (LONG)el->max_height)
            eff_h = el->max_height;
    }
    h = uwp_align_axis( &rc.top, &rc.bottom, eff_h, el->v_align );
    {
        double prev_w = el->actual_w, prev_h = el->actual_h;
        el->bounds = rc;
        el->has_bounds = (w > 0 && h > 0);
        el->actual_w = (double)w;
        el->actual_h = (double)h;
        if (!el->loaded_fired || el->loaded_fired_cnt < el->loaded_cnt)
            uwp_fire_loaded_tree( el );
        if (!margin_clamped && w > 0 && h > 0 && (prev_w != el->actual_w || prev_h != el->actual_h))
            uwp_fire_size_and_layout_changed( el );
    }

    uwp_invoke_winui_measure_arrange( el, w, h );
    uwp_fire_evc_if_needed( el );

    if (el->items_source && (!el->children_coll ||
        CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count == 0))
    {
        uwp_items_ctrl_materialize_source( el, el->items_source );
    }

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

        /* In UWP, a Grid with no RowDefinitions/ColumnDefinitions has 1 row / 1 column
         * (out-of-range Grid.Row/Grid.Column values clamp to 0 so overlays stack properly). */
        if (nrows > 30) nrows = 30;
        if (ncols > 30) ncols = 30;

        for (i = 0; i < ncols; i++)
        {
            struct uwp_element *cd = (cols && (unsigned)i < cols->count) ? uwp_elem_from_any( cols->items[i] ) : NULL;
            c_type[i] = cd ? cd->grid_len_type : 2;
            c_val[i]  = cd ? cd->grid_len_val  : 1.0;
            if (c_type[i] == 2 && c_val[i] <= 0.001) { c_type[i] = 1; c_val[i] = 0; }
            if (c_type[i] == 0)
            {
                double auto_w = 0.0;
                for (k = 0; k < children->count; k++)
                {
                    struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                    if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
                        ch->kind != UWP_KIND_STYLE && ch->kind != UWP_KIND_BRUSH &&
                        ch->kind != UWP_KIND_FLYOUT && ch->kind != UWP_KIND_POPUP &&
                        ch->grid_col == i && ch->grid_col_span == 1)
                    {
                        double cw = uwp_measure_natural_width( ch, 0 );
                        if (cw > 0.0) cw += max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
                        else cw = (ch->min_width > 0.0 ? ch->min_width : 160.0);
                        if (cw > auto_w) auto_w = cw;
                    }
                }
                c_type[i] = 1;
                c_val[i] = min( auto_w, (double)w );
            }
            if (c_type[i] == 1)
            {
                if (cd && cd->max_width > 0.0 && c_val[i] > cd->max_width) c_val[i] = cd->max_width;
                if (cd && cd->min_width > 0.0 && c_val[i] < cd->min_width) c_val[i] = cd->min_width;
                c_fixed += c_val[i];
            }
            else c_stars += (c_val[i] > 0 ? c_val[i] : 1.0);
        }

        /* Respect ColumnDefinition.MaxWidth on star columns (e.g. ColumnHistory MaxWidth=320) */
        if (c_stars > 0)
        {
            double rem_w = max( 0.0, (double)w - c_fixed );
            for (i = 0; i < ncols; i++)
            {
                struct uwp_element *cd = (cols && (unsigned)i < cols->count) ? uwp_elem_from_any( cols->items[i] ) : NULL;
                if (c_type[i] != 1 && cd && cd->max_width > 0.0 && c_stars > 0)
                {
                    double st = (c_val[i] > 0 ? c_val[i] : 1.0);
                    double share = rem_w * (st / c_stars);
                    if (share > cd->max_width)
                    {
                        c_type[i] = 1;
                        c_val[i] = cd->max_width;
                        c_fixed += cd->max_width;
                        c_stars -= st;
                        rem_w = max( 0.0, (double)w - c_fixed );
                    }
                }
            }
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

        for (i = 0; i < nrows; i++)
        {
            struct uwp_element *rd = (rows && (unsigned)i < rows->count) ? uwp_elem_from_any( rows->items[i] ) : NULL;
            BOOL has_vis = FALSE;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
                    ch->kind != UWP_KIND_STYLE && ch->kind != UWP_KIND_BRUSH &&
                    ch->kind != UWP_KIND_FLYOUT && ch->kind != UWP_KIND_POPUP &&
                    ch->grid_row <= i && i < ch->grid_row + ch->grid_row_span)
                {
                    int c0 = max( 0, min( ncols - 1, ch->grid_col ) );
                    int c1 = max( c0 + 1, min( ncols, c0 + ch->grid_col_span ) );
                    if (col_x[c1] > col_x[c0])
                    {
                        has_vis = TRUE;
                        break;
                    }
                }
            }
            r_type[i] = rd ? rd->grid_len_type : 2;
            r_val[i]  = rd ? rd->grid_len_val  : 1.0;
            /* Collapse empty history/dock or 0-star rows */
            if (r_type[i] == 2 && (r_val[i] <= 0.001 || (i > 0 && !has_vis)))
            { r_type[i] = 1; r_val[i] = 0; }
            if (r_type[i] == 0) /* Auto */
            {
                double auto_h = 0.0;
                if (has_vis)
                {
                    for (k = 0; k < children->count; k++)
                    {
                        struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                        if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
                            ch->kind != UWP_KIND_STYLE && ch->kind != UWP_KIND_BRUSH &&
                            ch->kind != UWP_KIND_FLYOUT && ch->kind != UWP_KIND_POPUP &&
                            ch->grid_row == i && ch->grid_row_span == 1)
                        {
                            double ch_h = uwp_estimate_auto_height( ch, 0 );
                            if (ch_h > auto_h) auto_h = ch_h;
                        }
                    }
                    if (auto_h < 36.0 * g_layout_scale)
                        auto_h = 36.0 * g_layout_scale;
                    if (rd && rd->max_height > 0.0 && auto_h > rd->max_height)
                        auto_h = rd->max_height;
                    if (rd && rd->min_height > 0.0 && auto_h < rd->min_height)
                        auto_h = rd->min_height;
                    if (h > 0 && auto_h > min( (double)h * 0.35, 320.0 ))
                        auto_h = min( (double)h * 0.35, 320.0 );
                }
                r_type[i] = 1;
                r_val[i] = auto_h;
            }
            if (r_type[i] == 1) r_fixed += r_val[i];
            else r_stars += (r_val[i] > 0 ? r_val[i] : 1.0);
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
        int fixed_h = 0, auto_cnt = 0;
        int spacing_px = (el->spacing > 0.0 && !isnan( el->spacing )) ? (int)(el->spacing + 0.5) : 0;
        BOOL use_horiz_flow = FALSE;
        double horiz_total_nw = 0.0;
        int horiz_unmeasured_cnt = 0;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
                ch->kind != UWP_KIND_ACCELERATOR && ch->kind != UWP_KIND_XAML_COMMAND &&
                !wcsstr( ch->class_name, L"TeachingTip" ) &&
                wcscmp( ch->name, L"TeachingTipTarget" ) != 0)
            {
                vis_cnt++;
                if (el->orientation == 1)
                {
                    double nw = uwp_measure_natural_width( ch, 0 );
                    if (nw > 0.0 || (ch->width > 0.0 && !isnan( ch->width )) ||
                        ch->kind == UWP_KIND_IMAGE || ch->kind == UWP_KIND_TEXT_BLOCK ||
                        ch->kind == UWP_KIND_BUTTON || ch->kind == UWP_KIND_TOGGLE_BUTTON ||
                        ch->kind == UWP_KIND_CONTENT_CONTROL || ch->kind == UWP_KIND_BORDER ||
                        ch->kind == UWP_KIND_USER_CONTROL || uwp_is_combobox( ch ))
                        use_horiz_flow = TRUE;
                    if (nw > 0.0)
                        horiz_total_nw += nw + max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
                    else
                        horiz_unmeasured_cnt++;
                }
                else
                {
                    double nh = uwp_measure_natural_height( ch, w, 0 );
                    if (ch->height > 0.0 && !isnan( ch->height ))
                        fixed_h += (int)(ch->height + max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5);
                    else if (nh > 0.0 && (int)nh < h)
                        fixed_h += (int)(nh + max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5);
                    else
                        auto_cnt++;
                }
            }
        }
        if (vis_cnt > 1 && el->orientation == 0)
            fixed_h += (int)(vis_cnt - 1) * spacing_px;
        if (vis_cnt > 0 && el->orientation == 1)
        {
            struct uwp_element *ch0 = (children->count > 0) ? uwp_elem_from_any( children->items[0] ) : NULL;
            unsigned int gaps = (vis_cnt > 1) ? (vis_cnt - 1) : 0;
            if (ch0 && ch0 != el && ch0->visibility != 0)
                gaps++;
            horiz_total_nw += (double)gaps * spacing_px;
        }
        if (vis_cnt > 0)
        {
            int idx = 0, auto_idx = 0, cur_y = rc.top, cur_x = rc.left;
            int auto_total_h = max( 0, h - fixed_h );
            double horiz_scale = (el->orientation == 1 && horiz_unmeasured_cnt == 0 && horiz_total_nw > (double)w && w > 0)
                ? ((double)w / horiz_total_nw) : 1.0;
            BOOL in_scroll = FALSE;
            if (el->orientation == 1 && use_horiz_flow && spacing_px > 0 && children->count > 1)
            {
                struct uwp_element *ch0 = uwp_elem_from_any( children->items[0] );
                if (ch0 && ch0 != el && ch0->visibility != 0)
                    cur_x += (int)(spacing_px * horiz_scale + 0.5);
            }
            if (el->orientation == 0 && auto_cnt > 1 && auto_total_h / auto_cnt < 140)
            {
                struct uwp_element *p;
                int hop;
                for (p = el->parent, hop = 0; p && hop < 5; p = p->parent, hop++)
                {
                    if (wcsstr( p->class_name, L"ScrollViewer" ))
                    {
                        in_scroll = TRUE;
                        break;
                    }
                }
            }
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                RECT crc = rc;
                if (!ch || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED ||
                    ch->kind == UWP_KIND_ACCELERATOR || ch->kind == UWP_KIND_XAML_COMMAND ||
                    wcsstr( ch->class_name, L"TeachingTip" ) ||
                    !wcscmp( ch->name, L"TeachingTipTarget" )) continue;
                if (el->orientation == 1) /* Horizontal */
                {
                    if (use_horiz_flow)
                    {
                        double nw = uwp_measure_natural_width( ch, 0 );
                        int slot_w = (nw > 0.0)
                            ? max( 16, (int)((nw + max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r )) * horiz_scale + 0.5) )
                            : max( 32, (rc.right - cur_x) / max( 1, (int)vis_cnt - idx ) );
                        crc.left = cur_x;
                        crc.right = min( (int)rc.right, cur_x + slot_w );
                        cur_x = crc.right + (int)(spacing_px * horiz_scale + 0.5);
                    }
                    else
                    {
                        crc.left  = rc.left + (w * idx) / (int)vis_cnt;
                        crc.right = rc.left + (w * (idx + 1)) / (int)vis_cnt;
                    }
                }
                else if (in_scroll)
                {
                    int nat_h;
                    if (ch->height > 0.0 && !isnan( ch->height ))
                    {
                        int slot_h = (int)(ch->height + max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5);
                        if (cur_y >= rc.bottom)
                        {
                            crc.top = cur_y;
                            crc.bottom = cur_y + slot_h;
                            cur_y = crc.bottom + spacing_px;
                            idx++;
                            uwp_arrange_element( ch, crc );
                            continue;
                        }
                        crc.top    = cur_y;
                        crc.bottom = min( (int)rc.bottom, cur_y + slot_h );
                        cur_y = crc.bottom + spacing_px;
                    }
                    else
                    {
                        double mh;
                        if (uwp_is_home_title_panel( ch, 0 ))
                        {
                            ch->has_bounds = FALSE;
                            idx++;
                            continue;
                        }
                        mh = uwp_measure_natural_height( ch, w, 0 );
                        if (mh > 0.0)
                        {
                            nat_h = max( 24, (int)(mh + max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5) );
                        }
                        else if (uwp_has_spotlight_descendant( ch, 0 ))
                            nat_h = 330;
                        else if (!uwp_scroll_child_has_content( ch, 0 ))
                            nat_h = 4;
                        else
                            nat_h = 168;
                        if (cur_y >= rc.bottom)
                        {
                            /* Still arrange off-screen children in virtual coordinates so
                             * their EffectiveViewportChanged/MeasureOverride run for hydration,
                             * then clear has_bounds if completely below viewport. */
                            crc.top = cur_y;
                            crc.bottom = cur_y + nat_h;
                            cur_y = crc.bottom + (nat_h > 24 ? 8 : 0);
                            auto_idx++;
                            idx++;
                            uwp_arrange_element( ch, crc );
                            continue;
                        }
                        crc.top    = cur_y;
                        crc.bottom = min( (int)rc.bottom, cur_y + nat_h );
                        cur_y = crc.bottom + (nat_h > 24 ? 8 : 0);
                        auto_idx++;
                    }
                }
                else
                {
                    BOOL has_explicit_h = (ch->height > 0.0 && !isnan( ch->height ));
                    double nh = has_explicit_h ? ch->height : uwp_measure_natural_height( ch, w, 0 );
                    if (nh > 0.0 && (has_explicit_h || (int)nh < h))
                    {
                        int slot_h = (int)(nh + max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5);
                        crc.top    = cur_y;
                        crc.bottom = min( (int)rc.bottom, cur_y + slot_h );
                        cur_y = crc.bottom + spacing_px;
                    }
                    else
                    {
                        int slice = (auto_cnt > 0)
                            ? ((auto_total_h * (auto_idx + 1)) / auto_cnt - (auto_total_h * auto_idx) / auto_cnt)
                            : 0;
                        crc.top    = cur_y;
                        crc.bottom = min( (int)rc.bottom, cur_y + slice );
                        cur_y = crc.bottom + spacing_px;
                        auto_idx++;
                    }
                }
                idx++;
                uwp_arrange_element( ch, crc );
            }
        }
        if (h >= 60)
        {
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (ch && ch->visibility == 0 && wcsstr( ch->class_name, L"TeachingTip" ) && ch->is_open)
                    uwp_arrange_element( ch, rc );
            }
        }
    }
    else if (wcsstr( el->class_name, L"MenuBar" ) && !wcsstr( el->class_name, L"MenuBarItem" ))
    {
        if (el->outer && (!el->children_coll ||
            CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count == 0))
        {
            void *mb = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IMenuBar_WinUI, &mb )) && mb)
            {
                void *vec = NULL;
                HRESULT (WINAPI *get_items)(void *, void **) = (void *)(*(void ***)mb)[6];
                if (get_items && SUCCEEDED(get_items( mb, &vec )) && vec)
                {
                    UINT32 cnt = 0, k;
                    HRESULT (WINAPI *get_at)(void *, UINT32, void **) = (void *)(*(void ***)vec)[6];
                    HRESULT (WINAPI *get_size)(void *, UINT32 *) = (void *)(*(void ***)vec)[7];
                    if (get_size && SUCCEEDED(get_size( vec, &cnt )) && cnt > 0)
                    {
                        if (!el->children_coll) uwp_vec_create( el, &el->children_coll );
                        for (k = 0; k < cnt; k++)
                        {
                            void *item = NULL;
                            if (get_at && SUCCEEDED(get_at( vec, k, &item )) && item)
                            {
                                if (el->children_coll) uwp_vec_Append( el->children_coll, item );
                                IUnknown_Release( (IUnknown *)item );
                            }
                        }
                    }
                    IUnknown_Release( (IUnknown *)vec );
                }
                IUnknown_Release( (IUnknown *)mb );
            }
        }
        if (el->children_coll)
        {
            struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k, idx = 0;
            int item_w = (int)(56 * g_layout_scale);
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                RECT crc;
                if (!ch || ch->visibility != 0 || wcsstr( ch->class_name, L"Accelerator" )) continue;
                ch->parent = el;
                if (!wcsstr( ch->class_name, L"MenuBarItem" ))
                    lstrcpynW( ch->class_name, L"Microsoft.UI.Xaml.Controls.MenuBarItem", ARRAY_SIZE(ch->class_name) );
                SetRect( &crc, rc.left + 8 + (int)idx * item_w, rc.top,
                         min( (int)rc.right, (int)rc.left + 8 + (int)(idx + 1) * item_w ), rc.bottom );
                ch->bounds = crc;
                ch->has_bounds = (crc.right > crc.left && crc.bottom > crc.top);
                ch->actual_w = (double)(crc.right - crc.left);
                ch->actual_h = (double)(crc.bottom - crc.top);
                if (ch->kind == UWP_KIND_GENERIC && ch->text[0])
                    ch->kind = UWP_KIND_TEXT_BLOCK;
                idx++;
            }
        }
        return;
    }
    else if (wcsstr( el->class_name, L"Pivot" ) && !wcsstr( el->class_name, L"PivotItem" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int tab_w = (int)(84 * g_layout_scale);
        int tab_h = min( h, (int)(36 * g_layout_scale) );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            RECT crc;
            if (!ch || ch->visibility != 0) continue;
            ch->margin_t = 0;
            SetRect( &crc, rc.left + 16 + (int)k * tab_w, rc.top + 8,
                     min( (int)rc.right, (int)rc.left + 16 + (int)(k + 1) * tab_w ), rc.top + 8 + tab_h );
            ch->bounds = crc;
            ch->has_bounds = (crc.right > crc.left && crc.bottom > crc.top);
            ch->actual_w = (double)(crc.right - crc.left);
            ch->actual_h = (double)(crc.bottom - crc.top);
        }
        return;
    }

    if (uwp_is_nav_view( el ))
    {
        float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        int avail_w = rc.right - rc.left;
        int eff_mode = uwp_nav_effective_display_mode( el, avail_w );
        int closed_rail_w = (eff_mode == 1) ? (int)(max( 48.0, el->nav_compact_pane_length ) * sc + 0.5) : 0;
        int open_pane_w = (int)(max( 48.0, min( 340.0, el->nav_open_pane_length > 0.0 ? el->nav_open_pane_length : 280.0 ) ) * sc + 0.5);
        BOOL pane_visible = el->nav_is_pane_open || (eff_mode != 0);
        int pane_w = el->nav_is_pane_open ? open_pane_w : closed_rail_w;
        int content_left = rc.left + ((eff_mode == 2 && el->nav_is_pane_open) ? open_pane_w : closed_rail_w);
        BOOL show_toggle = el->nav_is_pane_toggle_visible && (eff_mode != 2);
        int footer_vis_cnt = 0;
        BOOL has_footer_host = FALSE;

        if (pane_w > avail_w) pane_w = avail_w;
        if (pane_visible && pane_w > 0)
            SetRect( &el->nav_pane_bounds, rc.left, rc.top, min( (int)rc.right, (int)rc.left + pane_w ), rc.bottom );
        else
            SetRectEmpty( &el->nav_pane_bounds );

        if (el->children_coll)
        {
            struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k;
            int menu_top = rc.top + (show_toggle ? (int)(48 * sc + 0.5f) : (int)(12 * sc + 0.5f));
            int footer_h = 0, footer_top;
            BOOL wide_pane = (pane_w >= (int)(130 * sc));

            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (ch && !wcscmp( ch->name, L"FooterMenuItemsHost" ))
                {
                    has_footer_host = TRUE;
                    if (ch->children_coll)
                    {
                        struct uwp_vec_view *fv = CONTAINING_RECORD( ch->children_coll, struct uwp_vec_view, IInspectable_iface );
                        unsigned int fk;
                        for (fk = 0; fk < fv->count; fk++)
                        {
                            struct uwp_element *fch = uwp_elem_from_any( fv->items[fk] );
                            if (!fch) continue;
                            if (!wcscmp( fch->name, L"SettingsItem" ) || !wcscmp( fch->text, L"SettingsButtonName" ))
                            {
                                if (el->nav_is_settings_visible && wide_pane)
                                    footer_vis_cnt++;
                            }
                            else if (!wide_pane && (uwp_is_nav_header( fch ) || uwp_is_nav_separator( fch )))
                            {
                                continue;
                            }
                            else if (fch->visibility == 0)
                            {
                                footer_vis_cnt++;
                            }
                        }
                    }
                }
            }
            if (footer_vis_cnt > 0)
            {
                footer_h = wide_pane
                    ? min( (int)(160 * sc), footer_vis_cnt * (int)(40 * sc) + (int)(12 * sc) )
                    : min( (int)(180 * sc), footer_vis_cnt * (int)(58 * sc) + (int)(8 * sc) );
            }
            else if (has_footer_host && pane_visible && pane_w > 0)
            {
                /* Reserve initial footer strip so WinUI ItemsRepeater runs Measure/Arrange and materializes footer items */
                footer_h = wide_pane
                    ? (el->nav_is_settings_visible ? (int)(46 * sc) : (int)(40 * sc))
                    : (int)(124 * sc);
            }
            footer_top = max( menu_top + (int)(60 * sc), (int)rc.bottom - footer_h - 4 );

            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                RECT crc;
                if (!ch) continue;
                if (!wcscmp( ch->name, L"TogglePaneButton" ))
                {
                    if (!show_toggle || ch->visibility != 0)
                    {
                        uwp_clear_bounds_tree( ch );
                        continue;
                    }
                    SetRect( &crc, rc.left + (int)(6 * sc + 0.5f), rc.top + (int)(6 * sc + 0.5f),
                             rc.left + (int)(44 * sc + 0.5f), rc.top + (int)(42 * sc + 0.5f) );
                    ch->bounds = crc;
                    ch->has_bounds = (crc.right > crc.left && crc.bottom > crc.top);
                    ch->actual_w = (double)(crc.right - crc.left);
                    ch->actual_h = (double)(crc.bottom - crc.top);
                    continue;
                }
                if (!wcscmp( ch->name, L"MenuItemsHost" ))
                {
                    if (!pane_visible || pane_w <= 0 || ch->visibility != 0)
                    {
                        uwp_clear_bounds_tree( ch );
                        continue;
                    }
                    SetRect( &crc, rc.left, menu_top, min( (int)rc.right, (int)rc.left + pane_w ), footer_top );
                    uwp_arrange_element( ch, crc );
                    continue;
                }
                if (!wcscmp( ch->name, L"FooterMenuItemsHost" ))
                {
                    if (!pane_visible || pane_w <= 0 || footer_h <= 0 || ch->visibility != 0)
                    {
                        uwp_clear_bounds_tree( ch );
                        continue;
                    }
                    SetRect( &crc, rc.left, footer_top, min( (int)rc.right, (int)rc.left + pane_w ), rc.bottom - 4 );
                    uwp_arrange_element( ch, crc );
                    continue;
                }
                if (ch->visibility != 0) continue;
                uwp_arrange_element( ch, rc );
            }
        }
        if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el)
            {
                RECT content_rc;
                SetRect( &content_rc, content_left, rc.top, rc.right, rc.bottom );
                uwp_arrange_element( ch, content_rc );
            }
        }
        return;
    }

    if (wcsstr( el->class_name, L"ItemsRepeater" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        BOOL is_nav_rail = (!wcscmp( el->name, L"MenuItemsHost" ) || !wcscmp( el->name, L"FooterMenuItemsHost" ));
        int avail_w = rc.right - rc.left;

        if (!is_nav_rail && avail_w >= 480)
        {
            int pad = 12, gap = 12;
            int cols = max( 1, (avail_w - pad) / 270 );
            int card_w = (avail_w - pad * 2 - gap * (cols - 1)) / cols;
            int card_h = 96;
            int idx = 0;

            if (children->count >= 2)
            {
                static const WCHAR *const title_props[] =
                    { L"Title", L"ProductTitle", L"DisplayTitle", L"Name", L"Label", L"DisplayName", L"Header",
                      L"Content", L"CardModel", L"Card", L"Product", L"ViewModel", L"Model", L"Data", L"Item" };
                unsigned int k_cnt = children->count, full_cnt = 0;
                IInspectable *full_src = NULL;
                struct uwp_element *anc, *slot_el = NULL, *stack_el = NULL, *prev_rep = NULL;
                struct uwp_element *ch0 = uwp_elem_from_any( children->items[0] );
                int hop, di;

                if (ch0 && (ch0->data_context || ch0->content))
                {
                    for (anc = el->parent, hop = 0; anc && hop < 5 && !full_src; anc = anc->parent, hop++)
                    {
                        for (di = 0; di < anc->dp_cnt; di++)
                        {
                            if (anc->dp_store[di].value)
                            {
                                unsigned int vc = uwp_winrt_vec_count( anc->dp_store[di].value );
                                if (vc >= k_cnt * 2)
                                {
                                    full_src = anc->dp_store[di].value;
                                    full_cnt = vc;
                                    break;
                                }
                            }
                        }
                    }
                    for (anc = el->parent, hop = 0; full_src && anc && anc->parent && hop < 8; anc = anc->parent, hop++)
                    {
                        if (anc->parent->kind == UWP_KIND_STACK_PANEL && anc->parent->children_coll)
                        {
                            slot_el = anc;
                            stack_el = anc->parent;
                            break;
                        }
                    }
                    if (full_src && full_cnt >= k_cnt * 2 && slot_el && stack_el)
                    {
                        struct uwp_vec_view *stack_kids = CONTAINING_RECORD( stack_el->children_coll, struct uwp_vec_view, IInspectable_iface );
                        int slot_idx = -1, s;
                        for (s = 0; s < (int)stack_kids->count; s++)
                        {
                            if (uwp_elem_from_any( stack_kids->items[s] ) == slot_el)
                            {
                                slot_idx = s;
                                break;
                            }
                        }
                        for (s = slot_idx - 1; s >= 0 && !prev_rep; s--)
                        {
                            struct uwp_element *ps = uwp_elem_from_any( stack_kids->items[s] );
                            struct uwp_element *stack_q[32];
                            int qh = 0, qt = 0;
                            if (!ps || ps->visibility != 0) continue;
                            stack_q[qt++] = ps;
                            while (qh < qt && !prev_rep)
                            {
                                struct uwp_element *cur = stack_q[qh++];
                                if (!cur || cur->visibility != 0 || wcsstr( cur->class_name, L"Shimmer" )) continue;
                                if (wcsstr( cur->class_name, L"ItemsRepeater" ) &&
                                    !wcsstr( cur->class_name, L"ResponsiveItemsRepeaterList" ))
                                {
                                    if (cur->children_coll &&
                                        CONTAINING_RECORD( cur->children_coll, struct uwp_vec_view, IInspectable_iface )->count >= k_cnt)
                                        prev_rep = cur;
                                    continue;
                                }
                                if (cur->content && qt < (int)ARRAY_SIZE(stack_q))
                                {
                                    struct uwp_element *cch = uwp_elem_from_any( cur->content );
                                    if (cch && cch != cur) stack_q[qt++] = cch;
                                }
                                if (cur->children_coll)
                                {
                                    struct uwp_vec_view *cv = CONTAINING_RECORD( cur->children_coll, struct uwp_vec_view, IInspectable_iface );
                                    unsigned int ci;
                                    for (ci = 0; ci < cv->count && qt < (int)ARRAY_SIZE(stack_q); ci++)
                                    {
                                        struct uwp_element *cch = uwp_elem_from_any( cv->items[ci] );
                                        if (cch && cch != cur) stack_q[qt++] = cch;
                                    }
                                }
                            }
                        }
                        if (prev_rep && prev_rep != el && prev_rep->children_coll)
                        {
                            struct uwp_element *prev_ch0 = uwp_elem_from_any(
                                CONTAINING_RECORD( prev_rep->children_coll, struct uwp_vec_view, IInspectable_iface )->items[0] );
                            WCHAR cur_title[128] = {0}, prev_title[128] = {0};
                            IInspectable *vm_cur = (ch0->content && !uwp_elem_from_any( ch0->content )) ? ch0->content : ch0->data_context;
                            IInspectable *vm_prev = prev_ch0
                                ? ((prev_ch0->content && !uwp_elem_from_any( prev_ch0->content )) ? prev_ch0->content : prev_ch0->data_context)
                                : NULL;
                            if (vm_cur)
                                uwp_vm_query_string_prop_depth( vm_cur, title_props, ARRAY_SIZE(title_props), cur_title, ARRAY_SIZE(cur_title), 2 );
                            if (vm_prev)
                                uwp_vm_query_string_prop_depth( vm_prev, title_props, ARRAY_SIZE(title_props), prev_title, ARRAY_SIZE(prev_title), 2 );
                            if (cur_title[0] && prev_title[0] && !wcscmp( cur_title, prev_title ))
                            {
                                IInspectable *full_items[32];
                                unsigned int snap_cnt = uwp_winrt_vec_snapshot( full_src, full_items, ARRAY_SIZE(full_items) );
                                unsigned int si;
                                if (snap_cnt >= k_cnt * 2)
                                {
                                    for (si = 0; si < k_cnt; si++)
                                    {
                                        struct uwp_element *ch = uwp_elem_from_any( children->items[si] );
                                        IInspectable *new_item = full_items[k_cnt + si];
                                        if (ch && new_item)
                                        {
                                            if (ch->data_context) IInspectable_Release( ch->data_context );
                                            ch->data_context = new_item;
                                            IInspectable_AddRef( new_item );
                                            if (ch->content && !uwp_elem_from_any( ch->content ))
                                            {
                                                IInspectable_Release( ch->content );
                                                ch->content = new_item;
                                                IInspectable_AddRef( new_item );
                                            }
                                            ch->text[0] = 0;
                                            ch->image_src[0] = 0;
                                            ch->img_loaded = FALSE;
                                            InterlockedExchange( &ch->img_loading, 0 );
                                            if (ch->img_pixels)
                                            {
                                                free( ch->img_pixels );
                                                ch->img_pixels = NULL;
                                                ch->img_w = 0;
                                                ch->img_h = 0;
                                            }
                                        }
                                    }
                                }
                                for (si = 0; si < snap_cnt; si++)
                                    IInspectable_Release( full_items[si] );
                            }
                        }
                    }
                }
            }

            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                int col, row, x0, y0;
                RECT crc;
                if (!ch || ch->visibility != 0) continue;
                col = idx % cols;
                row = idx / cols;
                x0 = rc.left + pad + col * (card_w + gap);
                y0 = rc.top + pad + row * (card_h + gap);
                if (y0 >= rc.bottom)
                {
                    ch->has_bounds = FALSE;
                    idx++;
                    continue;
                }
                SetRect( &crc, x0, y0, x0 + card_w, min( (int)rc.bottom, y0 + card_h ) );
                uwp_arrange_element( ch, crc );
                idx++;
            }
            return;
        }
        if (is_nav_rail && avail_w >= (int)(130 * ((g_layout_scale > 0.25f) ? g_layout_scale : 1.0f)))
        {
            float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
            struct uwp_element *nav = el->parent;
            BOOL is_footer = !wcscmp( el->name, L"FooterMenuItemsHost" );
            int scroll_y = (!is_footer && nav && uwp_is_nav_view( nav )) ? nav->nav_scroll_y : 0;
            int cur_y = rc.top - scroll_y;
            BOOL rev = FALSE;
            if (!is_footer && children->count > 1)
            {
                struct uwp_element *last_ch = uwp_elem_from_any( children->items[children->count - 1] );
                if (last_ch && !wcscmp( last_ch->text, L"Home" ))
                    rev = TRUE;
            }
            for (k = 0; k < children->count; k++)
            {
                unsigned int idx = rev ? (children->count - 1 - k) : k;
                struct uwp_element *ch = uwp_elem_from_any( children->items[idx] );
                BOOL is_settings_item;
                int row_h;
                RECT crc;
                if (!ch) continue;
                is_settings_item = (!wcscmp( ch->name, L"SettingsItem" ) || !wcscmp( ch->text, L"SettingsButtonName" ));
                if (is_settings_item)
                {
                    if (!nav || !nav->nav_is_settings_visible)
                    {
                        uwp_clear_bounds_tree( ch );
                        continue;
                    }
                    if (!ch->text[0] || !wcscmp( ch->text, L"SettingsButtonName" ))
                        lstrcpynW( ch->text, L"Settings", ARRAY_SIZE(ch->text) );
                    ch->visibility = 0;
                }
                else if (ch->visibility != 0)
                {
                    uwp_clear_bounds_tree( ch );
                    continue;
                }
                if (uwp_is_nav_header( ch ))
                {
                    if (k > 0) cur_y += (int)(6 * sc + 0.5f);
                    row_h = (int)(30 * sc + 0.5f);
                }
                else if (uwp_is_nav_separator( ch ))
                {
                    row_h = (int)(11 * sc + 0.5f);
                }
                else
                {
                    row_h = (int)(36 * sc + 0.5f);
                }
                if (cur_y + row_h <= rc.top || cur_y >= rc.bottom)
                {
                    uwp_clear_bounds_tree( ch );
                }
                else
                {
                    SetRect( &crc, rc.left + (int)(6 * sc + 0.5f), max( (int)rc.top, cur_y ),
                             rc.right - (int)(6 * sc + 0.5f), min( (int)rc.bottom, cur_y + row_h ) );
                    if (is_settings_item && ch->visibility != 0)
                    {
                        ch->bounds = crc;
                        ch->has_bounds = (crc.right > crc.left && crc.bottom > crc.top);
                        ch->actual_w = (double)(crc.right - crc.left);
                        ch->actual_h = (double)(crc.bottom - crc.top);
                    }
                    else
                    {
                        uwp_arrange_element( ch, crc );
                    }
                }
                cur_y += row_h + 2;
            }
            return;
        }
        {
            int cur_y = rc.top;
            int item_h = is_nav_rail ? 54 : 80;
            BOOL rev = FALSE;
            if (is_nav_rail && children->count > 1)
            {
                struct uwp_element *last_ch = uwp_elem_from_any( children->items[children->count - 1] );
                if (last_ch && !wcscmp( last_ch->text, L"Home" ))
                    rev = TRUE;
            }
            for (k = 0; k < children->count; k++)
            {
                unsigned int idx = rev ? (children->count - 1 - k) : k;
                struct uwp_element *ch = uwp_elem_from_any( children->items[idx] );
                RECT crc;
                if (!ch || ch->visibility != 0) continue;
                if (is_nav_rail && (uwp_is_nav_header( ch ) || uwp_is_nav_separator( ch ) ||
                                    !wcscmp( ch->name, L"SettingsItem" ) || !wcscmp( ch->text, L"SettingsButtonName" )))
                {
                    uwp_clear_bounds_tree( ch );
                    continue;
                }
                SetRect( &crc, rc.left + 4, cur_y, rc.right - 4, min( (int)rc.bottom, cur_y + item_h ) );
                uwp_arrange_element( ch, crc );
                cur_y += item_h + 4;
            }
            return;
        }
    }

    if ((wcsstr( el->class_name, L"ListView" ) || wcsstr( el->class_name, L"GridView" )) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int avail_w = rc.right - rc.left;
        int avail_h = rc.bottom - rc.top;
        if (avail_w >= 480 && avail_h > 0 && avail_h <= 400)
        {
            int pad = 8, gap = 12;
            int item_h = max( 64, avail_h - pad );
            int item_w = max( 180, min( 320, (item_h * 16) / 9 ) );
            int idx = 0;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                int x0;
                RECT crc;
                if (!ch || ch->visibility != 0 || wcsstr( ch->class_name, L"Template" )) continue;
                x0 = rc.left + pad + idx * (item_w + gap);
                if (x0 >= rc.right)
                {
                    ch->has_bounds = FALSE;
                    idx++;
                    continue;
                }
                SetRect( &crc, x0, rc.top + pad / 2,
                         min( (int)rc.right, x0 + item_w ),
                         min( (int)rc.bottom, rc.top + pad / 2 + item_h ) );
                if (!ch->parent) ch->parent = el;
                uwp_arrange_element( ch, crc );
                idx++;
            }
            return;
        }
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
            uwp_arrange_element( ch, rc );
    }

    if (el->kind == UWP_KIND_WRAP_PANEL && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        int cur_x = rc.left, cur_y = rc.top, row_h = 0;
        int sp = (el->spacing > 0.0) ? (int)(el->spacing + 0.5) : 4;
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double nw, nh;
            int cw, ch_h;
            RECT crc;
            if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED ||
                ch->kind == UWP_KIND_ACCELERATOR || ch->kind == UWP_KIND_XAML_COMMAND)
                continue;
            nw = uwp_measure_natural_width( ch, 0 );
            if (nw <= 0.0) nw = 80.0;
            cw = max( 16, (int)(nw + max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r ) + 0.5) );
            nh = uwp_measure_natural_height( ch, cw, 0 );
            if (nh <= 0.0) nh = 32.0;
            ch_h = max( 16, (int)(nh + max( 0.0, ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5) );
            if (cur_x > rc.left && cur_x + cw > rc.right)
            {
                cur_x = rc.left;
                cur_y += row_h + sp;
                row_h = 0;
            }
            crc.left = cur_x;
            crc.top = cur_y;
            crc.right = min( (int)rc.right, cur_x + cw );
            crc.bottom = min( (int)rc.bottom, cur_y + ch_h );
            uwp_arrange_element( ch, crc );
            cur_x = crc.right + sp;
            if (ch_h > row_h) row_h = ch_h;
        }
        return;
    }

    if (el->kind != UWP_KIND_GRID && el->kind != UWP_KIND_STACK_PANEL && el->kind != UWP_KIND_WRAP_PANEL && !uwp_is_combobox( el ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_arrange_element( ch, rc );
        }
    }
}
