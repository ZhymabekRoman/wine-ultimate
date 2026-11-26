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
           pid == XBF_PROP_STACKPANEL_BORDER_BRUSH;
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
        is_para = (blk->class_flags & (UWP_CF_PARAGRAPH | UWP_CF_LINE_BREAK)) != 0;
        if (uwp_has_text( blk ))
        {
            if (is_para && pos > 0 && out_buf[pos - 1] != '\n') out_buf[pos++] = '\n';
            lstrcpynW( out_buf + pos, uwp_elem_text( blk ), (int)(out_cap - pos) );
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
                        const struct abi_run *arun = (const struct abi_run *)run_iface;
                        HSTRING htxt = NULL;
                        if (arun->lpVtbl->get_Text && SUCCEEDED(arun->lpVtbl->get_Text( run_iface, &htxt )) && htxt)
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
                        static const GUID IID_ISpan_uwp =
                            { 0x9839d4a9, 0x02af, 0x4811, { 0xaa, 0x1d, 0x69, 0x6a, 0x68, 0x2a, 0x45, 0x26 } };
                        void *span_iface = NULL;
                        if ((SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)iv->items[j], &IID_ISpan_g, &span_iface )) && span_iface) ||
                            (SUCCEEDED(IUnknown_QueryInterface( (IUnknown *)iv->items[j], &IID_ISpan_uwp, &span_iface )) && span_iface))
                        {
                            const struct abi_span *aspan = (const struct abi_span *)span_iface;
                            void *sub_inlines = NULL;
                            if (aspan->lpVtbl->get_Inlines && SUCCEEDED(aspan->lpVtbl->get_Inlines( span_iface, &sub_inlines )) && sub_inlines)
                            {
                                struct uwp_vec_view *sv = CONTAINING_RECORD( sub_inlines, struct uwp_vec_view, IInspectable_iface );
                                unsigned int k;
                                for (k = 0; sv && k < sv->count && pos + 2 < out_cap; k++)
                                {
                                    struct uwp_element *sub = uwp_elem_from_any( sv->items[k] );
                                    if (uwp_has_text( sub ))
                                    {
                                        lstrcpynW( out_buf + pos, uwp_elem_text( sub ), (int)(out_cap - pos) );
                                        pos += wcslen( out_buf + pos );
                                    }
                                }
                                IInspectable_Release( (IInspectable *)sub_inlines );
                            }
                            IUnknown_Release( (IUnknown *)span_iface );
                        }
                    }
                }
                else if (uwp_has_text( inl ))
                {
                    lstrcpynW( out_buf + pos, uwp_elem_text( inl ), (int)(out_cap - pos) );
                    pos += wcslen( out_buf + pos );
                }
                else if (inl && inl->inlines_coll)
                {
                    struct uwp_vec_view *sv = CONTAINING_RECORD( inl->inlines_coll, struct uwp_vec_view, IInspectable_iface );
                    unsigned int k;
                    for (k = 0; sv && k < sv->count && pos + 2 < out_cap; k++)
                    {
                        struct uwp_element *sub = uwp_elem_from_any( sv->items[k] );
                        if (uwp_has_text( sub ))
                        {
                            lstrcpynW( out_buf + pos, uwp_elem_text( sub ), (int)(out_cap - pos) );
                            pos += wcslen( out_buf + pos );
                        }
                    }
                }
            }
        }
    }
    return pos;
}

static void uwp_invoke_winui_measure_arrange( struct uwp_element *el, int w, int h )
{
    void *fe_ovr = NULL;
    struct uwp_size_f avail, desired = {0}, arranged = {0};
    HRESULT hr;

    if (!el || !el->outer || el->in_measure_arrange || w <= 0) return;
    if (!uwp_is_items_repeater( el )) return;
    if (h <= 0) h = 204;
    if (el->last_measured_w == (float)w && el->last_measured_h == (float)h &&
        el->children_coll &&
        CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count > 0)
        return;

    el->in_measure_arrange = TRUE;
    el->last_measured_w = (float)w;
    el->last_measured_h = (float)h;
    avail.width = (float)w;
    avail.height = (float)h;

    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IFrameworkElementOverrides_UWP, &fe_ovr )) && fe_ovr)
    {
        const struct abi_fe_overrides *afo = (const struct abi_fe_overrides *)fe_ovr;
        HRESULT (WINAPI *measure_ovr)(void *, struct uwp_size_f, struct uwp_size_f *) = afo->lpVtbl->MeasureOverride;
        HRESULT (WINAPI *arrange_ovr)(void *, struct uwp_size_f, struct uwp_size_f *) = afo->lpVtbl->ArrangeOverride;
        __TRY
        {
            if (measure_ovr)
            {
                hr = measure_ovr( fe_ovr, avail, &desired );
                if ((desired.width <= 1.5f && desired.height <= 1.5f && UWP_EV_CNT( el, evc_reg ) > 0) ||
                    (UWP_EV_CNT( el, evc_reg ) > 0 && !el->evc_fired))
                {
                    el->evc_fired = FALSE;
                    if (el->actual_w <= 10.0) el->actual_w = (double)w;
                    if (el->actual_h <= 10.0) el->actual_h = (double)h;
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
                    if (desired.width == 1.0f && desired.height == 1.0f)
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
            if (ch && ch != el && uwp_loaded_needs_fire( ch ))
                uwp_fire_loaded_tree( ch );
        }
    }
}

#define UWP_MEMO_SHIMMER_VALID   0x01
#define UWP_MEMO_SHIMMER_VAL     0x02
#define UWP_MEMO_SPOTLIGHT_VALID 0x04
#define UWP_MEMO_SPOTLIGHT_VAL   0x08
#define UWP_MEMO_SCROLL_VALID    0x10
#define UWP_MEMO_SCROLL_VAL      0x20

static BOOL uwp_has_shimmer_descendant_ex( struct uwp_element *el, int depth, BOOL *trunc )
{
    BOOL res = FALSE, child_trunc = FALSE;
    if (!el) return FALSE;
    if (depth > 8) { if (trunc) *trunc = TRUE; return FALSE; }
    if (el->content_flags_gen == g_layout_gen && (el->cached_content_flags & UWP_MEMO_SHIMMER_VALID))
        return (el->cached_content_flags & UWP_MEMO_SHIMMER_VAL) != 0;
    if (uwp_is_shimmer( el ))
        res = TRUE;
    if (!res && el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_has_shimmer_descendant_ex( ch, depth + 1, &child_trunc )) res = TRUE;
    }
    if (!res && el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_has_shimmer_descendant_ex( ch, depth + 1, &child_trunc )) { res = TRUE; break; }
        }
    }
    if (res || !child_trunc)
    {
        if (el->content_flags_gen != g_layout_gen)
        {
            el->content_flags_gen = g_layout_gen;
            el->cached_content_flags = 0;
        }
        el->cached_content_flags |= UWP_MEMO_SHIMMER_VALID | (res ? UWP_MEMO_SHIMMER_VAL : 0);
    }
    else if (trunc)
        *trunc = TRUE;
    return res;
}

static BOOL uwp_has_spotlight_descendant_ex( struct uwp_element *el, int depth, BOOL *trunc )
{
    BOOL res = FALSE, child_trunc = FALSE;
    if (!el || el->visibility != 0) return FALSE;
    if (depth > 10) { if (trunc) *trunc = TRUE; return FALSE; }
    if (el->content_flags_gen == g_layout_gen && (el->cached_content_flags & UWP_MEMO_SPOTLIGHT_VALID))
        return (el->cached_content_flags & UWP_MEMO_SPOTLIGHT_VAL) != 0;
    if (el->class_flags & UWP_CF_SPOTLIGHT_DECOR)
        res = TRUE;
    if (!res && el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_has_spotlight_descendant_ex( ch, depth + 1, &child_trunc )) res = TRUE;
    }
    if (!res && el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_has_spotlight_descendant_ex( ch, depth + 1, &child_trunc )) { res = TRUE; break; }
        }
    }
    if (res || !child_trunc)
    {
        if (el->content_flags_gen != g_layout_gen)
        {
            el->content_flags_gen = g_layout_gen;
            el->cached_content_flags = 0;
        }
        el->cached_content_flags |= UWP_MEMO_SPOTLIGHT_VALID | (res ? UWP_MEMO_SPOTLIGHT_VAL : 0);
    }
    else if (trunc)
        *trunc = TRUE;
    return res;
}

static BOOL uwp_has_spotlight_descendant( struct uwp_element *el, int depth )
{
    return uwp_has_spotlight_descendant_ex( el, depth, NULL );
}

static BOOL uwp_subtree_has_rich_control( const struct uwp_element *el, int depth )
{
    if (!el || depth > 5 || el->visibility != 0) return FALSE;
    if (el->kind == UWP_KIND_IMAGE || el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
        el->kind == UWP_KIND_SCROLL_VIEWER || uwp_is_scroll_viewer( el ) || uwp_is_items_repeater( el ) ||
        uwp_is_text_input( el ) || uwp_is_combobox( el ) ||
        (el->class_flags & (UWP_CF_SPOTLIGHT | UWP_CF_MEDIA_PLAYER | UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW | UWP_CF_CARD)))
        return TRUE;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_subtree_has_rich_control( ch, depth + 1 )) return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; v && i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_subtree_has_rich_control( ch, depth + 1 )) return TRUE;
        }
    }
    return FALSE;
}

static BOOL uwp_is_scroll_overlay_title( struct uwp_element *ch, struct uwp_element *sv_child )
{
    if (!ch || !sv_child || ch == sv_child || ch->visibility != 0) return FALSE;
    if (ch->grid_row != sv_child->grid_row || ch->grid_col != sv_child->grid_col) return FALSE;
    if (uwp_subtree_has_rich_control( ch, 0 )) return FALSE;
    if (!uwp_paint_btn_child_has_text( ch, 0 )) return FALSE;
    return uwp_has_spotlight_descendant( sv_child, 0 );
}

static BOOL uwp_scroll_child_has_content_ex( struct uwp_element *el, int depth, BOOL *trunc )
{
    BOOL res = FALSE, child_trunc = FALSE;
    if (!el || el->visibility != 0) return FALSE;
    if (depth > 14) { if (trunc) *trunc = TRUE; return FALSE; }
    if (el->content_flags_gen == g_layout_gen && (el->cached_content_flags & UWP_MEMO_SCROLL_VALID))
        return (el->cached_content_flags & UWP_MEMO_SCROLL_VAL) != 0;
    if (uwp_has_spotlight_descendant_ex( el, 0, &child_trunc ))
        res = TRUE;
    else if (uwp_has_shimmer_descendant_ex( el, 0, &child_trunc ) && el->kind == UWP_KIND_BORDER)
        res = FALSE;
    else if (uwp_has_text( el ) || el->inlines_coll || (el->media && (el->media->img_pixels || el->media->img_loaded)))
        res = TRUE;
    else if (uwp_is_items_repeater( el ))
    {
        int di;
        if (el->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            if (v->count > 0) res = TRUE;
        }
        for (di = 0; !res && di < el->dp_cnt; di++)
        {
            if (el->dp_store[di].value && uwp_winrt_vec_count( el->dp_store[di].value ) > 0)
                res = TRUE;
        }
    }
    else
    {
        if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el && uwp_scroll_child_has_content_ex( ch, depth + 1, &child_trunc )) res = TRUE;
        }
        if (!res && el->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int i;
            for (i = 0; i < v->count; i++)
            {
                struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
                if (ch && ch != el && uwp_scroll_child_has_content_ex( ch, depth + 1, &child_trunc )) { res = TRUE; break; }
            }
        }
    }
    if (res || !child_trunc)
    {
        if (el->content_flags_gen != g_layout_gen)
        {
            el->content_flags_gen = g_layout_gen;
            el->cached_content_flags = 0;
        }
        el->cached_content_flags |= UWP_MEMO_SCROLL_VALID | (res ? UWP_MEMO_SCROLL_VAL : 0);
    }
    else if (trunc)
        *trunc = TRUE;
    return res;
}

static BOOL uwp_scroll_child_has_content( struct uwp_element *el, int depth )
{
    return uwp_scroll_child_has_content_ex( el, depth, NULL );
}

static void uwp_arrange_element_inner( struct uwp_element *el, RECT rc );
static double uwp_measure_natural_height( const struct uwp_element *el, int avail_w, int depth );

void uwp_arrange_element( struct uwp_element *el, RECT rc )
{
    static int depth = 0;
    if (!el || depth >= 48) return;
    if (depth == 0)
    {
        if (++g_layout_gen == 0) g_layout_gen = 1;
    }
    depth++;
    uwp_arrange_element_inner( el, rc );
    depth--;
    if (depth == 0)
    {
        struct uwp_element *pops[16];
        int pcnt = uwp_get_open_popups( pops, ARRAY_SIZE(pops) ), pi;
        int root_w = rc.right - rc.left;
        int root_h = rc.bottom - rc.top;
        for (pi = 0; pi < pcnt && root_w > 64 && root_h > 64; pi++)
        {
            struct uwp_element *pop = pops[pi];
            struct uwp_element *ch = uwp_elem_from_any( pop->content );
            if (!ch) continue;
            {
                double sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0;
                double cw = (ch->width > 40.0) ? ch->width * sc
                          : ((ch->max_width > 40.0) ? min( (double)root_w - 64.0, ch->max_width * sc )
                          : min( 760.0 * sc, (double)root_w - 96.0 ));
                double nat_h = uwp_measure_natural_height( ch, (int)cw, 0 );
                double ch_h = (ch->height > 40.0) ? ch->height * sc
                            : ((nat_h > 40.0) ? min( (double)root_h - 80.0, nat_h + 32.0 * sc )
                            : min( 540.0 * sc, (double)root_h - 96.0 ));
                int pw, ph, px, py;
                RECT prc;
                if (ch->max_height > 40.0 && ch_h > ch->max_height * sc)
                    ch_h = ch->max_height * sc;
                pw = max( 160, min( root_w - 32, (int)(cw + 0.5) ) );
                ph = max( 100, min( root_h - 32, (int)(ch_h + 0.5) ) );
                if (pop->popup_h_offset > 0.0 || pop->popup_v_offset > 0.0)
                {
                    px = (int)(pop->popup_h_offset * sc + 0.5);
                    py = (int)(pop->popup_v_offset * sc + 0.5);
                    if (px + pw > rc.right - 16) px = max( (int)rc.left + 16, (int)rc.right - pw - 16 );
                    if (py + ph > rc.bottom - 16) py = max( (int)rc.top + 40, (int)rc.bottom - ph - 16 );
                    if (px < rc.left + 16) px = rc.left + 16;
                    if (py < rc.top + 40) py = rc.top + 40;
                }
                else
                {
                    px = rc.left + (root_w - pw) / 2;
                    py = rc.top + (root_h - ph) / 2;
                }
                SetRect( &prc, px, py, px + pw, py + ph );
                pop->bounds = prc;
                pop->has_bounds = TRUE;
                depth++;
                uwp_arrange_element_inner( ch, prc );
                depth--;
            }
        }
    }
}

static inline double uwp_clamp_top_margin( double mt )
{
    double cap = 180.0 * g_layout_scale;
    if (mt > cap) return 44.0 * g_layout_scale;
    return max( 0.0, mt );
}

static double uwp_estimate_auto_height( struct uwp_element *el, int depth );

static double uwp_estimate_auto_height_uncached( struct uwp_element *el, int depth )
{
    double best = 0.0;
    if (!el || depth > 24 || el->visibility != 0) return 0.0;
    if (el->kind == UWP_KIND_DEFERRED || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_RESOURCE_DICT ||
        el->kind == UWP_KIND_COLOR || el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND)
        return 0.0;
    if (el->height > 0.0 && !isnan( el->height ))
        return el->height + uwp_clamp_top_margin( el->margin_t ) + max( 0.0, el->margin_b );
    if (el->min_height > 0.0 && !isnan( el->min_height ))
        best = el->min_height;
    if (uwp_is_items_repeater( el ) &&
        !wcsstr( el->name, L"MenuItems" ) && !wcsstr( el->name, L"FooterMenuItems" ))
    {
        int aw = 960;
        const struct uwp_element *anc;
        for (anc = el->parent; anc; anc = anc->parent)
        {
            if (anc->actual_w > 200.0) { aw = (int)anc->actual_w; break; }
            if (anc->width > 200.0 && !isnan( anc->width )) { aw = (int)anc->width; break; }
        }
        if (el->children_coll)
        {
            struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k, vis_cnt = 0;
            struct uwp_element *ch0 = NULL;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (ch && ch->visibility == 0)
                {
                    if (!ch0) ch0 = ch;
                    vis_cnt++;
                }
            }
            if (vis_cnt > 0)
            {
                int cols = max( 1, (aw - 12) / 270 );
                int rows_cnt = uwp_is_card( ch0 ) ? 1 : (((int)vis_cnt + cols - 1) / cols);
                double rh = (double)(24 + rows_cnt * 96 + max( 0, rows_cnt - 1 ) * 12) * g_layout_scale;
                if (rh > best) best = rh;
            }
        }
        if (best <= 0.0)
        {
            int di;
            for (di = 0; di < el->dp_cnt; di++)
            {
                if (el->dp_store[di].value && uwp_winrt_vec_count( el->dp_store[di].value ) > 0)
                {
                    best = 120.0 * g_layout_scale;
                    break;
                }
            }
        }
        return best + uwp_clamp_top_margin( el->margin_t ) + max( 0.0, el->margin_b );
    }
    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
        double line_h = max( 18.0 * g_layout_scale, fs * 1.42 );
        int lines = 1;
        WCHAR rich_buf[2048];
        const WCHAR *txt = uwp_elem_text( el );
        if (!txt[0] && el->inlines_coll && uwp_flatten_inlines_text( el, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
            txt = rich_buf;
        if (txt[0])
        {
            const WCHAR *p;
            int nl_lines = 1;
            for (p = txt; *p; p++) if (*p == L'\n') nl_lines++;
            lines = nl_lines;
            if (el->text_wrapping >= 2 || nl_lines > 1 || wcslen( txt ) > 64)
            {
                double max_w = 540.0 * g_layout_scale;
                const struct uwp_element *anc;
                for (anc = el->parent; anc; anc = anc->parent)
                {
                    if (anc->actual_w > 48.0) { max_w = anc->actual_w; break; }
                    if (anc->width > 48.0 && !isnan( anc->width )) { max_w = anc->width; break; }
                }
                if (el->max_width > 32.0 && el->max_width < max_w) max_w = el->max_width;
                if (max_w > 32.0)
                {
                    int total_wrapped = 0;
                    const WCHAR *seg = txt;
                    while (*seg)
                    {
                        const WCHAR *nl = wcschr( seg, L'\n' );
                        size_t slen = nl ? (size_t)(nl - seg) : wcslen( seg );
                        if (slen > 0 && slen * fs * 0.58 > max_w)
                            total_wrapped += (int)ceil( ((double)slen * fs * 0.58) / max_w );
                        else
                            total_wrapped += 1;
                        if (!nl) break;
                        seg = nl + 1;
                    }
                    if (total_wrapped > lines) lines = total_wrapped;
                }
            }
        }
        if (el->max_lines > 0 && lines > el->max_lines)
            lines = el->max_lines;
        if (lines > 120) lines = 120;
        {
            double th = line_h * lines + 4.0;
            if (th > best) best = th;
        }
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
                        if (ch_h > 0.0) r_vis = TRUE;
                        if (ch_h > rh) rh = ch_h;
                    }
                }
                if (r_vis && rh < 24.0 * g_layout_scale) rh = 24.0 * g_layout_scale;
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
        unsigned int k, vis = 0;
        BOOL is_vert_list = ((el->class_flags & (UWP_CF_LISTVIEW | UWP_CF_HAS_ITEM)) == UWP_CF_LISTVIEW &&
                             el->orientation == 0 && el->item_template != NULL);
        double acc = 0.0;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && ch->visibility == 0)
            {
                double ch_h = uwp_estimate_auto_height( ch, depth + 1 );
                if (is_vert_list && ch_h > 0.0)
                {
                    acc += ch_h + (vis > 0 ? 12.0 * g_layout_scale : 0.0);
                    vis++;
                }
                else if (ch_h > best) best = ch_h;
            }
        }
        if (is_vert_list && acc > best) best = acc;
    }
    if ((el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON) &&
        best < 36.0 * g_layout_scale)
        best = 36.0 * g_layout_scale;
    if (el->max_height > 0.0 && !isnan( el->max_height ) && best > el->max_height)
        best = el->max_height;
    if (best > 0.0)
        best += uwp_clamp_top_margin( el->margin_t ) + max( 0.0, el->margin_b );
    return best;
}

static BOOL g_auto_h_trunc = FALSE;

static double uwp_estimate_auto_height( struct uwp_element *el, int depth )
{
    double res;
    BOOL saved_trunc;
    if (!el || el->visibility != 0) return 0.0;
    if (depth > 24) { g_auto_h_trunc = TRUE; return 0.0; }
    if (el->auto_h_gen == g_layout_gen)
        return el->cached_auto_h;
    saved_trunc = g_auto_h_trunc;
    g_auto_h_trunc = FALSE;
    res = uwp_estimate_auto_height_uncached( el, depth );
    if (!g_auto_h_trunc)
    {
        el->cached_auto_h = res;
        el->auto_h_gen = g_layout_gen;
    }
    g_auto_h_trunc = saved_trunc || g_auto_h_trunc;
    return res;
}

static BOOL uwp_apply_margins_clamped( const struct uwp_element *el, RECT *rc )
{
    int orig_h = rc->bottom - rc->top;
    int mt = (int)el->margin_t;
    int mb = (int)el->margin_b;
    BOOL clamped = FALSE;

    if (mt > (int)(180.0 * g_layout_scale))
    {
        mt = (int)(44.0 * g_layout_scale);
        clamped = TRUE;
    }
    else if (orig_h >= 200 && mt > 200 && mt + mb >= orig_h)
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

int uwp_nav_effective_display_mode( const struct uwp_element *nav, int avail_w )
{
    double scale, w_dip;
    int pmode;
    double compact_th, expanded_th;
    BOOL is_open;
    if (!nav) return 1;
    pmode = nav->nav ? nav->nav->nav_pane_display_mode : 0;
    compact_th = nav->nav ? nav->nav->nav_compact_threshold : 641.0;
    expanded_th = nav->nav ? nav->nav->nav_expanded_threshold : 1008.0;
    is_open = uwp_elem_nav_is_pane_open( nav );
    switch (pmode)
    {
    case 4: /* LeftMinimal */
    case 2: /* Top */
        return 0; /* Minimal (no left rail when closed) */
    case 3: /* LeftCompact */
        return 1; /* Compact */
    case 1: /* Left */
        scale = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        w_dip = (double)avail_w / scale;
        return (w_dip >= expanded_th) ? 2 : 1;
    default: /* 0 = Auto */
        if (avail_w <= 0)
            return (nav->nav && nav->nav->nav_display_mode_set) ? nav->nav->nav_display_mode : 1;
        scale = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        w_dip = (double)avail_w / scale;
        if (w_dip < compact_th) return 0;
        if (w_dip < expanded_th) return 1;
        return is_open ? 2 : 1;
    }
}

BOOL uwp_nav_is_overlay_open( const struct uwp_element *nav )
{
    if (!nav || !uwp_is_nav_view( nav ) || !uwp_elem_nav_is_pane_open( nav )) return FALSE;
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

static double uwp_measure_natural_width( const struct uwp_element *el, int depth );

static double uwp_measure_natural_width_uncached( const struct uwp_element *el, int depth )
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
        const WCHAR *txt = uwp_elem_text( el );
        double raw_fs = (el->font_size > 0.0 ? el->font_size : 14.0);
        int eff_fw = el->font_weight;
        BOOL is_sec_hdr = FALSE;
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
                if (anc->kind == UWP_KIND_BUTTON && !anc->has_bg && !anc->bg_brush && !anc->has_border &&
                    !anc->is_accent_style && anc->h_align != 1 &&
                    (raw_fs >= 18.0 || eff_fw >= FW_SEMIBOLD))
                    is_sec_hdr = TRUE;
                break;
            }
        }
        if (!is_sec_hdr && raw_fs >= 18.0 && eff_fw >= FW_SEMIBOLD)
            is_sec_hdr = TRUE;
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
        if (best <= 0.0 && el->media && el->media->img_w > 0 && el->media->img_h > 0)
        {
            double th = (el->height > 0.0 && !isnan( el->height )) ? el->height : min( 160.0 * g_layout_scale, (double)el->media->img_h );
            best = max( 120.0 * g_layout_scale, min( 320.0 * g_layout_scale, th * (double)el->media->img_w / (double)el->media->img_h ) );
        }
        if (best <= 0.0)
        {
            double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
            size_t len = wcslen( uwp_elem_text( el ) );
            best = max( 80.0 * g_layout_scale, (double)len * fs * 0.64 + 32.0 + el->padding_l + el->padding_r );
        }
    }
    else if (uwp_is_combobox( el ))
    {
        best = 160.0 * g_layout_scale;
    }
    else if (el->kind == UWP_KIND_IMAGE)
    {
        if (el->media && el->media->img_w > 0) best = (double)el->media->img_w;
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
    else if ((el->kind == UWP_KIND_BORDER || el->kind == UWP_KIND_CONTENT_CONTROL || el->kind == UWP_KIND_USER_CONTROL) &&
             !(el->class_flags & UWP_CF_RADIO_BUTTON))
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
        if (best <= 0.0 && uwp_has_text( el ))
        {
            double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
            best = (double)wcslen( uwp_elem_text( el ) ) * fs * 0.56 + 24.0 + el->padding_l + el->padding_r;
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
    else if ((el->kind == UWP_KIND_GRID || el->kind == UWP_KIND_GENERIC || (el->class_flags & UWP_CF_RADIO_BUTTON)) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k, vis = 0;
        BOOL sum_horiz = (el->class_flags & (UWP_CF_LISTBOX | UWP_CF_ITEMS_CONTROL | UWP_CF_RADIO_BUTTON)) != 0;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double cw = (ch && ch != el && !(ch->class_flags & UWP_CF_TEMPLATE)) ? uwp_measure_natural_width( ch, depth + 1 ) : 0.0;
            if (cw > 0.0)
            {
                cw += max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
                if (sum_horiz)
                {
                    best += cw + (vis > 0 ? 12.0 : 0.0);
                    vis++;
                }
                else if (cw > best) best = cw;
            }
        }
    }
    else if (uwp_is_font_icon( el ))
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

static BOOL g_nat_w_trunc = FALSE;

static double uwp_measure_natural_width( const struct uwp_element *el, int depth )
{
    struct uwp_element *mel = (struct uwp_element *)el;
    double res;
    BOOL saved_trunc;
    if (!el || el->visibility != 0) return 0.0;
    if (depth > 10) { g_nat_w_trunc = TRUE; return 0.0; }
    if (mel->nat_w_gen == g_layout_gen)
        return mel->cached_nat_w;
    saved_trunc = g_nat_w_trunc;
    g_nat_w_trunc = FALSE;
    res = uwp_measure_natural_width_uncached( el, depth );
    if (!g_nat_w_trunc)
    {
        mel->cached_nat_w = res;
        mel->nat_w_gen = g_layout_gen;
    }
    g_nat_w_trunc = saved_trunc || g_nat_w_trunc;
    return res;
}

static BOOL uwp_is_horiz_listview( const struct uwp_element *el )
{
    if (!el) return FALSE;
    if ((el->class_flags & (UWP_CF_GRIDVIEW | UWP_CF_HAS_ITEM)) == UWP_CF_GRIDVIEW)
        return TRUE;
    if ((el->class_flags & (UWP_CF_LISTVIEW | UWP_CF_HAS_ITEM)) == UWP_CF_LISTVIEW)
    {
        if (el->orientation == 1) return TRUE;
        if (el->item_template) return FALSE;
        if (el->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k;
            for (k = 0; v && k < v->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
                if (!ch || ch->visibility != 0 || (ch->class_flags & UWP_CF_TEMPLATE)) continue;
                if (ch->kind == UWP_KIND_USER_CONTROL)
                    return FALSE;
                break;
            }
        }
        return TRUE;
    }
    return FALSE;
}

static BOOL uwp_has_horiz_listview_child( const struct uwp_element *el, int depth )
{
    if (!el || depth > 2 || el->visibility != 0) return FALSE;
    if (uwp_is_horiz_listview( el ))
        return TRUE;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_has_horiz_listview_child( ch, depth + 1 )) return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; v && k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el && uwp_has_horiz_listview_child( ch, depth + 1 )) return TRUE;
        }
    }
    return FALSE;
}

static double uwp_measure_natural_height_uncached( const struct uwp_element *el, int avail_w, int depth )
{
    double best = 0.0;
    if (!el || depth > 28 || el->visibility != 0) return 0.0;
    if (el->kind == UWP_KIND_DEFERRED || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_RESOURCE_DICT ||
        el->kind == UWP_KIND_COLOR || el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND ||
        (el->class_flags & UWP_CF_TEMPLATE) != 0)
        return 0.0;
    if (!el->template_inflated && !el->content && !el->children_coll)
        uwp_inflate_control_template( (struct uwp_element *)el );
    if (el->items_source && !uwp_is_combobox( el ) && !uwp_panel_has_concrete_child( (struct uwp_element *)el ))
        uwp_items_ctrl_materialize_source( (struct uwp_element *)el, el->items_source );
    if (avail_w >= 480 && uwp_has_horiz_listview_child( el, 0 ))
        return 148.0 * g_layout_scale;
    if (el->height > 0.0 && !isnan( el->height ))
        return el->height;
    if ((el->class_flags & UWP_CF_CONSTRAINED_BOX) && avail_w > 0 && avail_w <= 320)
        return (double)avail_w;
    if (uwp_is_items_repeater( el ) &&
        !wcsstr( el->name, L"MenuItems" ) && !wcsstr( el->name, L"FooterMenuItems" ))
    {
        if (!el->children_coll && avail_w >= 400)
        {
            if (uwp_loaded_needs_fire( el ))
                uwp_fire_loaded_tree( (struct uwp_element *)el );
            uwp_invoke_winui_measure_arrange( (struct uwp_element *)el, avail_w, 204 );
        }
        if (el->children_coll && avail_w >= 400)
        {
            struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k, vis_cnt = 0;
            struct uwp_element *ch0 = NULL;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (ch && ch->visibility == 0)
                {
                    if (!ch0) ch0 = ch;
                    vis_cnt++;
                }
            }
            if (vis_cnt > 0)
            {
                int cols = max( 1, (avail_w - 12) / 270 );
                int rows_cnt = uwp_is_card( ch0 ) ? 1 : (((int)vis_cnt + cols - 1) / cols);
                int row_h = (ch0 && !uwp_is_card( ch0 )) ? 124 : 96;
                return (double)(24 + rows_cnt * row_h + max( 0, rows_cnt - 1 ) * 12) * g_layout_scale;
            }
        }
        return 0.0;
    }
    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        double fs = (el->font_size > 0.0 ? el->font_size : 14.0) * g_layout_scale;
        double line_h = max( 20.0 * g_layout_scale, fs * 1.45 );
        int lines = 1;
        WCHAR rich_buf[2048];
        const WCHAR *txt = uwp_elem_text( el );
        if (!txt[0] && el->inlines_coll && uwp_flatten_inlines_text( el, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
            txt = rich_buf;
        if (txt[0])
        {
            const WCHAR *p;
            int nl_lines = 1;
            for (p = txt; *p; p++) if (*p == L'\n') nl_lines++;
            lines = nl_lines;
            if ((el->text_wrapping >= 2 || nl_lines > 1 || wcslen( txt ) > 64) && avail_w > 32)
            {
                double max_w = (double)avail_w - 16.0;
                if (el->max_width > 0.0 && el->max_width < max_w) max_w = el->max_width;
                if (max_w > 32.0)
                {
                    int total_wrapped = 0;
                    const WCHAR *seg = txt;
                    while (*seg)
                    {
                        const WCHAR *nl = wcschr( seg, L'\n' );
                        size_t slen = nl ? (size_t)(nl - seg) : wcslen( seg );
                        if (slen > 0 && slen * fs * 0.58 > max_w)
                            total_wrapped += (int)ceil( ((double)slen * fs * 0.58) / max_w );
                        else
                            total_wrapped += 1;
                        if (!nl) break;
                        seg = nl + 1;
                    }
                    if (total_wrapped > lines) lines = total_wrapped;
                }
            }
        }
        if (el->max_lines > 0 && lines > el->max_lines)
            lines = el->max_lines;
        if (lines > 120) lines = 120;
        best = line_h * lines + 4.0;
    }
    else if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        if (el->media && el->media->img_w > 0 && el->media->img_h > 0)
        {
            double aw = (avail_w > 0) ? (double)avail_w : 240.0 * g_layout_scale;
            best = min( 180.0 * g_layout_scale, max( 96.0 * g_layout_scale, aw * (double)el->media->img_h / (double)el->media->img_w ) );
        }
        else if (uwp_elem_image_src( el )[0])
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
        if (el->media && el->media->img_h > 0) best = (double)el->media->img_h;
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
                uwp_is_teaching_tip( ch ) || uwp_is_zero_layout_anchor( ch ))
                continue;
            ch_h = uwp_measure_natural_height( ch, inner_w, depth + 1 );
            if (ch_h <= 0.0)
                ch_h = uwp_estimate_auto_height( ch, depth + 1 );
            if (ch_h <= 0.0) continue;
            ch_h += uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b );
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
        BOOL row_vis[32] = {0};
        double max_span_h = 0.0;
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
            struct uwp_element *rd;
            double ch_h;
            int cr;
            if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED ||
                ch->kind == UWP_KIND_STYLE || ch->kind == UWP_KIND_BRUSH ||
                ch->kind == UWP_KIND_FLYOUT || ch->kind == UWP_KIND_POPUP ||
                ch->kind == UWP_KIND_ACCELERATOR || ch->kind == UWP_KIND_XAML_COMMAND)
                continue;
            cr = (ch->grid_row >= 0 && ch->grid_row < nrows) ? ch->grid_row : 0;
            rd = (rows && (unsigned)cr < rows->count) ? uwp_elem_from_any( rows->items[cr] ) : NULL;
            ch_h = uwp_measure_natural_height( ch, avail_w, depth + 1 );
            if (ch_h > 0.0)
                ch_h += uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b );
            if (rd && rd->grid_len_type == 0)
            {
                double est_h = uwp_estimate_auto_height( ch, depth + 1 );
                if (est_h > ch_h) ch_h = est_h;
            }
            if (ch_h > 0.0) row_vis[cr] = TRUE;
            if (ch->grid_row_span > 1 && nrows > 1)
            {
                if (ch_h > max_span_h) max_span_h = ch_h;
                continue;
            }
            if (ch_h > row_h[cr]) row_h[cr] = ch_h;
        }
        for (r = 0; r < nrows; r++)
        {
            struct uwp_element *rd = (rows && (unsigned)r < rows->count) ? uwp_elem_from_any( rows->items[r] ) : NULL;
            if (rd && rd->grid_len_type == 0 && row_vis[r] && row_h[r] > 0.0 && row_h[r] < 24.0 * g_layout_scale)
                row_h[r] = 24.0 * g_layout_scale;
            best += row_h[r];
        }
        if (max_span_h > best) best = max_span_h;
    }
    else if (el->content || el->children_coll)
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
        if (best <= 0.0 && el->children_coll && !uwp_is_autosuggest( el ))
        {
            struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k, vis = 0;
            BOOL is_vert_list = ((el->class_flags & (UWP_CF_LISTVIEW | UWP_CF_HAS_ITEM)) == UWP_CF_LISTVIEW &&
                                 !uwp_is_horiz_listview( el ));
            double acc = 0.0;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                double ch_h;
                if (!ch || ch == el || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED ||
                    (ch->class_flags & UWP_CF_TEMPLATE)) continue;
                ch_h = uwp_measure_natural_height( ch, avail_w, depth + 1 );
                if (ch_h > 0.0)
                {
                    ch_h += uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b );
                    if (is_vert_list)
                    {
                        acc += ch_h + (vis > 0 ? 12.0 * g_layout_scale : 0.0);
                        vis++;
                    }
                    else if (ch_h > best) best = ch_h;
                }
            }
            if (is_vert_list && acc > best) best = acc;
            if (best > 0.0) best += el->padding_t + el->padding_b;
        }
    }
    if (el->min_height > 0.0 && !isnan( el->min_height ) && best < el->min_height)
        best = el->min_height;
    if (el->max_height > 0.0 && !isnan( el->max_height ) && best > el->max_height)
        best = el->max_height;
    return best;
}

static BOOL g_nat_h_trunc = FALSE;

static double uwp_measure_natural_height( const struct uwp_element *el, int avail_w, int depth )
{
    struct uwp_element *mel = (struct uwp_element *)el;
    double res;
    BOOL saved_trunc;
    if (!el || el->visibility != 0) return 0.0;
    if (depth > 28) { g_nat_h_trunc = TRUE; return 0.0; }
    if (mel->nat_h_gen == g_layout_gen && mel->cached_nat_h_avail_w == avail_w)
        return mel->cached_nat_h;
    saved_trunc = g_nat_h_trunc;
    g_nat_h_trunc = FALSE;
    res = uwp_measure_natural_height_uncached( el, avail_w, depth );
    if (!g_nat_h_trunc)
    {
        mel->cached_nat_h = res;
        mel->cached_nat_h_avail_w = avail_w;
        mel->nat_h_gen = g_layout_gen;
    }
    g_nat_h_trunc = saved_trunc || g_nat_h_trunc;
    return res;
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

static void arrange_grid_compute_tracks( struct uwp_element *el, struct uwp_vec_view *children,
                                         RECT rc, int w, int h,
                                         int *out_nrows, int *out_ncols,
                                         int row_y[32], int col_x[32] )
{
    struct uwp_vec_view *rows = el->row_defs_coll ? CONTAINING_RECORD( el->row_defs_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
    struct uwp_vec_view *cols = el->col_defs_coll ? CONTAINING_RECORD( el->col_defs_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
    int nrows = (rows && rows->count > 0) ? (int)rows->count : 1;
    int ncols = (cols && cols->count > 0) ? (int)cols->count : 1;
    double r_type[32], r_val[32], c_type[32], c_val[32];
    double r_fixed = 0, r_stars = 0, c_fixed = 0, c_stars = 0;
    BOOL has_star_row = FALSE;
    unsigned int k;
    int i;

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
                    else cw = (ch->min_width > 0.0 ? ch->min_width : 0.0);
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

    if (c_stars > 0)
    {
        double rem_w = max( 0.0, (double)w - c_fixed );
        for (i = 0; i < ncols; i++)
        {
            struct uwp_element *cd = (cols && (unsigned)i < cols->count) ? uwp_elem_from_any( cols->items[i] ) : NULL;
            if (c_type[i] != 1 && cd && cd->max_width > 0.0 && c_stars > 0)
            {
                double st = (c_val[i] > 0 ? c_val[i] : 1.0);
                if (rem_w * (st / c_stars) > cd->max_width)
                {
                    c_type[i] = 1; c_val[i] = cd->max_width;
                    c_fixed += cd->max_width; c_stars -= st;
                    rem_w = max( 0.0, (double)w - c_fixed );
                }
            }
        }
    }

    {
        double rem_w = max( 0.0, (double)w - c_fixed ), cur_x = (double)rc.left;
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
        int rt = rd ? rd->grid_len_type : 2;
        double rv = rd ? rd->grid_len_val : 1.0;
        if (rt == 2 && rv > 0.001) { has_star_row = TRUE; break; }
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
                if (col_x[c1] > col_x[c0]) { has_vis = TRUE; break; }
            }
        }
        r_type[i] = rd ? rd->grid_len_type : 2;
        r_val[i]  = rd ? rd->grid_len_val  : 1.0;
        if (r_type[i] == 2 && (r_val[i] <= 0.001 || (i > 0 && !has_vis)))
        { r_type[i] = 1; r_val[i] = 0; }
        if (r_type[i] == 0)
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
                        double nat_ch_h = uwp_measure_natural_height( ch, w, 0 );
                        if (nat_ch_h > 0.0)
                            nat_ch_h += uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b );
                        if (nat_ch_h > ch_h) ch_h = nat_ch_h;
                        if (ch_h > auto_h) auto_h = ch_h;
                    }
                }
                if (auto_h > 0.0 && auto_h < 24.0 * g_layout_scale) auto_h = 24.0 * g_layout_scale;
                if (rd && rd->max_height > 0.0 && auto_h > rd->max_height) auto_h = rd->max_height;
                if (rd && rd->min_height > 0.0 && auto_h < rd->min_height) auto_h = rd->min_height;
                if (has_star_row && h >= 400 && auto_h > min( (double)h * 0.35, 320.0 ))
                    auto_h = min( (double)h * 0.35, 320.0 );
            }
            r_type[i] = 1; r_val[i] = auto_h;
        }
        if (r_type[i] == 1) r_fixed += r_val[i];
        else r_stars += (r_val[i] > 0 ? r_val[i] : 1.0);
    }

    {
        double rem_h = max( 0.0, (double)h - r_fixed ), cur_y = (double)rc.top;
        for (i = 0; i < nrows; i++)
        {
            row_y[i] = (int)(cur_y + 0.5);
            if (r_type[i] == 1) cur_y += r_val[i];
            else if (r_stars > 0) cur_y += rem_h * (r_val[i] / r_stars);
        }
        row_y[nrows] = (r_stars == 0 && (int)(cur_y + 0.5) > (int)rc.bottom) ? (int)(cur_y + 0.5) : (int)rc.bottom;
    }
    *out_nrows = nrows;
    *out_ncols = ncols;
}

static void arrange_grid( struct uwp_element *el, RECT rc, int w, int h )
{
    struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
    int nrows = 1, ncols = 1;
    int row_y[32], col_x[32];
    struct uwp_element *sv_child = NULL;
    BOOL has_hydrated_child = FALSE;
    unsigned int k;

    arrange_grid_compute_tracks( el, children, rc, w, h, &nrows, &ncols, row_y, col_x );

    for (k = 0; k < children->count; k++)
    {
        struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
        if (!ch || ch->visibility != 0) continue;
        if (!sv_child && uwp_is_scroll_viewer( ch )) sv_child = ch;
        if (!uwp_is_skeleton_subtree( ch, 0 ) && uwp_scroll_child_has_content( ch, 0 ))
            has_hydrated_child = TRUE;
    }
    for (k = 0; k < children->count; k++)
    {
        struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
        int r0, r1, c0, c1;
        RECT crc;
        if (!ch) continue;
        if (sv_child && uwp_is_scroll_overlay_title( ch, sv_child ))
        {
            uwp_clear_bounds_tree( ch );
            continue;
        }
        if (has_hydrated_child && uwp_is_skeleton_subtree( ch, 0 ))
        {
            uwp_clear_bounds_tree( ch );
            continue;
        }
        r0 = max( 0, min( nrows - 1, ch->grid_row ) );
        r1 = max( r0 + 1, min( nrows, r0 + ch->grid_row_span ) );
        c0 = max( 0, min( ncols - 1, ch->grid_col ) );
        c1 = max( c0 + 1, min( ncols, c0 + ch->grid_col_span ) );
        SetRect( &crc, col_x[c0], row_y[r0], col_x[c1], row_y[r1] );
        uwp_arrange_element( ch, crc );
    }
}

static BOOL stack_child_is_layout_active( const struct uwp_element *ch )
{
    return ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
           ch->kind != UWP_KIND_ACCELERATOR && ch->kind != UWP_KIND_XAML_COMMAND &&
           !uwp_is_teaching_tip( ch ) && !uwp_is_zero_layout_anchor( ch );
}

static void arrange_stack_panel( struct uwp_element *el, RECT rc, int w, int h )
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
        if (!stack_child_is_layout_active( ch )) continue;
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
            if (nw > 0.0) horiz_total_nw += nw + max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r );
            else horiz_unmeasured_cnt++;
        }
        else
        {
            double nh = uwp_measure_natural_height( ch, w, 0 );
            if (nh > 0.0) fixed_h += (int)(nh + uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5);
            else auto_cnt++;
        }
    }
    if (vis_cnt > 1 && el->orientation == 0) fixed_h += (int)(vis_cnt - 1) * spacing_px;
    if (vis_cnt > 0 && el->orientation == 1)
    {
        struct uwp_element *ch0 = (children->count > 0) ? uwp_elem_from_any( children->items[0] ) : NULL;
        unsigned int gaps = (vis_cnt > 1) ? (vis_cnt - 1) : 0;
        if (ch0 && ch0 != el && ch0->visibility != 0) gaps++;
        horiz_total_nw += (double)gaps * spacing_px;
    }

    if (vis_cnt > 0)
    {
        int idx = 0, auto_idx = 0, cur_y = rc.top, cur_x = rc.left;
        int auto_total_h = max( 0, h - fixed_h );
        double horiz_scale = (el->orientation == 1 && horiz_unmeasured_cnt == 0 && horiz_total_nw > (double)w && w > 0)
            ? ((double)w / horiz_total_nw) : 1.0;
        BOOL in_scroll = FALSE, is_top_scroll_panel = TRUE, has_spotlight_slot = FALSE;
        struct uwp_element *sv = NULL;

        if (el->orientation == 1 && use_horiz_flow && spacing_px > 0 && children->count > 1)
        {
            struct uwp_element *ch0 = uwp_elem_from_any( children->items[0] );
            if (ch0 && ch0 != el && ch0->visibility != 0)
                cur_x += (int)(spacing_px * horiz_scale + 0.5);
        }
        if (el->orientation == 0)
        {
            struct uwp_element *p;
            int hop;
            for (p = el->parent, hop = 0; p && hop < 5; p = p->parent, hop++)
            {
                if (p->kind == UWP_KIND_STACK_PANEL && p->orientation == 0) is_top_scroll_panel = FALSE;
                if (uwp_is_scroll_viewer( p )) { sv = p; in_scroll = TRUE; break; }
            }
        }
        if (in_scroll && sv && is_top_scroll_panel)
        {
            sv->scroll_extent_h = 0;
            cur_y = rc.top - sv->scroll_y;
        }
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            RECT crc = rc;
            if (!stack_child_is_layout_active( ch )) continue;
            if (el->orientation == 1)
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
                if (ch->height > 0.0 && !isnan( ch->height ) && !uwp_has_horiz_listview_child( ch, 0 ))
                {
                    int slot_h = (int)(ch->height + uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5);
                    crc.top = cur_y; crc.bottom = cur_y + slot_h;
                    cur_y = crc.bottom + spacing_px;
                }
                else
                {
                    double mh = uwp_measure_natural_height( ch, w, 0 );
                    int nat_h;
                    if (mh > 0.0)
                        nat_h = max( 24, (int)(mh + uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5) );
                    else if (uwp_has_spotlight_descendant( ch, 0 ))
                    {
                        if (has_spotlight_slot) { uwp_clear_bounds_tree( ch ); idx++; continue; }
                        has_spotlight_slot = TRUE; nat_h = 330;
                    }
                    else nat_h = uwp_scroll_child_has_content( ch, 0 ) ? 168 : 4;
                    crc.top = cur_y; crc.bottom = cur_y + nat_h;
                    cur_y = crc.bottom + (nat_h > 24 ? 8 : 0);
                    auto_idx++;
                }
            }
            else
            {
                double nh = uwp_measure_natural_height( ch, w, 0 );
                if (nh > 0.0)
                {
                    int slot_h = (int)(nh + uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5);
                    crc.top = cur_y; crc.bottom = min( (int)rc.bottom, cur_y + slot_h );
                    cur_y = crc.bottom + spacing_px;
                }
                else
                {
                    int slice = (auto_cnt > 0)
                        ? ((auto_total_h * (auto_idx + 1)) / auto_cnt - (auto_total_h * auto_idx) / auto_cnt) : 0;
                    crc.top = cur_y; crc.bottom = min( (int)rc.bottom, cur_y + slice );
                    cur_y = crc.bottom + spacing_px;
                    auto_idx++;
                }
            }
            idx++;
            uwp_arrange_element( ch, crc );
        }
        if (in_scroll && sv)
        {
            int virt_ext = (cur_y + sv->scroll_y) - sv->bounds.top;
            int vp_h = max( 1, (int)(sv->bounds.bottom - sv->bounds.top) );
            if (virt_ext > sv->scroll_extent_h) sv->scroll_extent_h = virt_ext;
            if (is_top_scroll_panel && sv->scroll_y > max( 0, sv->scroll_extent_h - vp_h ))
                sv->scroll_y = max( 0, sv->scroll_extent_h - vp_h );
        }
    }
    if (h >= 60)
    {
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch->visibility == 0 && uwp_is_teaching_tip( ch ) && ch->is_open)
                uwp_arrange_element( ch, rc );
        }
    }
}

static void arrange_menu_bar( struct uwp_element *el, RECT rc )
{
    if (el->outer && (!el->children_coll ||
        CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count == 0))
    {
        void *mb = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IMenuBar_WinUI, &mb )) && mb)
        {
            const struct abi_menu_bar *amb = (const struct abi_menu_bar *)mb;
            void *vec = NULL;
            if (amb->lpVtbl->get_Items && SUCCEEDED(amb->lpVtbl->get_Items( mb, &vec )) && vec)
            {
                const struct abi_winrt_vector *avec = (const struct abi_winrt_vector *)vec;
                UINT32 cnt = 0, k;
                if (avec->lpVtbl->get_Size && SUCCEEDED(avec->lpVtbl->get_Size( vec, &cnt )) && cnt > 0)
                {
                    if (!el->children_coll) uwp_vec_create( el, &el->children_coll );
                    for (k = 0; k < cnt; k++)
                    {
                        void *item = NULL;
                        if (avec->lpVtbl->GetAt && SUCCEEDED(avec->lpVtbl->GetAt( vec, k, &item )) && item)
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
            if (!ch || ch->visibility != 0 || uwp_is_accelerator( ch )) continue;
            ch->parent = el;
            if (!uwp_is_menubar_item( ch ))
            {
                lstrcpynW( ch->class_name, L"Microsoft.UI.Xaml.Controls.MenuBarItem", ARRAY_SIZE(ch->class_name) );
                uwp_elem_sync_class_flags( ch );
            }
            SetRect( &crc, rc.left + 8 + (int)idx * item_w, rc.top,
                     min( (int)rc.right, (int)rc.left + 8 + (int)(idx + 1) * item_w ), rc.bottom );
            ch->bounds = crc;
            ch->has_bounds = (crc.right > crc.left && crc.bottom > crc.top);
            ch->actual_w = (double)(crc.right - crc.left);
            ch->actual_h = (double)(crc.bottom - crc.top);
            if (ch->kind == UWP_KIND_GENERIC && uwp_has_text( ch )) ch->kind = UWP_KIND_TEXT_BLOCK;
            idx++;
        }
    }
}

static void arrange_pivot( struct uwp_element *el, RECT rc, int h )
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
}

static void arrange_nav_view( struct uwp_element *el, RECT rc )
{
    struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
    float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
    int avail_w = rc.right - rc.left;
    int eff_mode = uwp_nav_effective_display_mode( el, avail_w );
    double compact_len = ne ? ne->nav_compact_pane_length : 68.0;
    double open_len = ne ? ne->nav_open_pane_length : 280.0;
    BOOL is_pane_open = ne ? ne->nav_is_pane_open : FALSE;
    BOOL is_settings_vis = ne ? ne->nav_is_settings_visible : TRUE;
    BOOL is_toggle_vis = ne ? ne->nav_is_pane_toggle_visible : TRUE;
    int closed_rail_w = (eff_mode == 1) ? (int)(max( 48.0, compact_len ) * sc + 0.5) : 0;
    int open_pane_w = (int)(max( 48.0, min( 340.0, open_len > 0.0 ? open_len : 280.0 ) ) * sc + 0.5);
    BOOL pane_visible = is_pane_open || (eff_mode != 0);
    int pane_w = min( avail_w, is_pane_open ? open_pane_w : closed_rail_w );
    int content_left = rc.left + ((eff_mode == 2 && is_pane_open) ? open_pane_w : closed_rail_w);
    BOOL show_toggle = is_toggle_vis && (eff_mode != 2);

    if (ne)
    {
        if (pane_visible && pane_w > 0)
            SetRect( &ne->nav_pane_bounds, rc.left, rc.top, min( (int)rc.right, (int)rc.left + pane_w ), rc.bottom );
        else
            SetRectEmpty( &ne->nav_pane_bounds );
    }

    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int menu_top = rc.top + (show_toggle ? (int)(48 * sc + 0.5f) : (int)(12 * sc + 0.5f));
        int footer_vis_cnt = 0, footer_h = 0, footer_top;
        BOOL has_footer_host = FALSE, wide_pane = (pane_w >= (int)(130 * sc));

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
                        if (uwp_is_nav_settings_item( fch ))
                        {
                            if (is_settings_vis && wide_pane) footer_vis_cnt++;
                        }
                        else if (!wide_pane && (uwp_is_nav_header( fch ) || uwp_is_nav_separator( fch )))
                            continue;
                        else if (fch->visibility == 0)
                            footer_vis_cnt++;
                    }
                }
            }
        }
        if (footer_vis_cnt > 0)
            footer_h = wide_pane
                ? min( (int)(160 * sc), footer_vis_cnt * (int)(40 * sc) + (int)(12 * sc) )
                : min( (int)(180 * sc), footer_vis_cnt * (int)(58 * sc) + (int)(26 * sc) );
        else if (has_footer_host && pane_visible && pane_w > 0)
            footer_h = wide_pane ? (is_settings_vis ? (int)(46 * sc) : (int)(40 * sc)) : (int)(124 * sc);
        footer_top = max( menu_top + (int)(60 * sc), (int)rc.bottom - footer_h - 4 );

        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            RECT crc;
            if (!ch) continue;
            if (!wcscmp( ch->name, L"TogglePaneButton" ))
            {
                if (!show_toggle || ch->visibility != 0) { uwp_clear_bounds_tree( ch ); continue; }
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
                if (!pane_visible || pane_w <= 0 || ch->visibility != 0) { uwp_clear_bounds_tree( ch ); continue; }
                SetRect( &crc, rc.left, menu_top, min( (int)rc.right, (int)rc.left + pane_w ), footer_top );
                uwp_arrange_element( ch, crc );
                continue;
            }
            if (!wcscmp( ch->name, L"FooterMenuItemsHost" ))
            {
                if (!pane_visible || pane_w <= 0 || footer_h <= 0 || ch->visibility != 0) { uwp_clear_bounds_tree( ch ); continue; }
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
}

static void arrange_items_repeater( struct uwp_element *el, RECT rc )
{
    struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
    unsigned int k;
    BOOL is_nav_rail = (!wcscmp( el->name, L"MenuItemsHost" ) || !wcscmp( el->name, L"FooterMenuItemsHost" ));
    int avail_w = rc.right - rc.left;
    float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;

    if (!is_nav_rail && avail_w >= 480)
    {
        struct uwp_element *ch0_peek = (children->count > 0) ? uwp_elem_from_any( children->items[0] ) : NULL;
        int pad = 12, gap = 12;
        int cols = max( 1, (avail_w - pad) / 270 );
        int card_w = (avail_w - pad * 2 - gap * (cols - 1)) / cols;
        int card_h = (ch0_peek && !uwp_is_card( ch0_peek )) ? 124 : 96;
        int idx = 0;

        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            int col, row, x0, y0;
            RECT crc;
            if (!ch || ch->visibility != 0) continue;
            col = idx % cols; row = idx / cols;
            x0 = rc.left + pad + col * (card_w + gap);
            y0 = rc.top + pad + row * (card_h + gap);
            if (y0 >= rc.bottom) { ch->has_bounds = FALSE; idx++; continue; }
            SetRect( &crc, x0, y0, x0 + card_w, min( (int)rc.bottom, y0 + card_h ) );
            uwp_arrange_element( ch, crc );
            idx++;
        }
        return;
    }

    if (is_nav_rail && avail_w >= (int)(130 * sc))
    {
        struct uwp_element *nav = el->parent;
        BOOL is_footer = !wcscmp( el->name, L"FooterMenuItemsHost" );
        int scroll_y = (!is_footer && nav && uwp_is_nav_view( nav ) && nav->nav) ? nav->nav->nav_scroll_y : 0;
        int cur_y = rc.top - scroll_y;
        BOOL rev = FALSE;
        if (!is_footer && children->count > 1)
        {
            struct uwp_element *last_ch = uwp_elem_from_any( children->items[children->count - 1] );
            if (last_ch && !wcscmp( uwp_elem_text( last_ch ), L"Home" )) rev = TRUE;
        }
        for (k = 0; k < children->count; k++)
        {
            unsigned int idx = rev ? (children->count - 1 - k) : k;
            struct uwp_element *ch = uwp_elem_from_any( children->items[idx] );
            BOOL is_settings_item;
            int row_h;
            RECT crc;
            if (!ch) continue;
            is_settings_item = uwp_is_nav_settings_item( ch );
            if (is_settings_item)
            {
                if (!nav || !uwp_elem_nav_is_settings_visible( nav )) { uwp_clear_bounds_tree( ch ); continue; }
                if (!uwp_has_text( ch )) uwp_elem_set_text( ch, L"Settings" );
                ch->visibility = 0;
            }
            else if (ch->visibility != 0) { uwp_clear_bounds_tree( ch ); continue; }
            if (uwp_is_nav_header( ch )) { if (k > 0) cur_y += (int)(6 * sc + 0.5f); row_h = (int)(30 * sc + 0.5f); }
            else if (uwp_is_nav_separator( ch )) row_h = (int)(11 * sc + 0.5f);
            else row_h = (int)(36 * sc + 0.5f);
            if (cur_y + row_h <= rc.top || cur_y >= rc.bottom)
                uwp_clear_bounds_tree( ch );
            else
            {
                SetRect( &crc, rc.left + (int)(6 * sc + 0.5f), max( (int)rc.top, cur_y ),
                         rc.right - (int)(6 * sc + 0.5f), min( (int)rc.bottom, cur_y + row_h ) );
                uwp_arrange_element( ch, crc );
            }
            cur_y += row_h + 2;
        }
        return;
    }

    {
        int cur_y = rc.top, item_h = is_nav_rail ? 54 : 80;
        BOOL rev = FALSE;
        if (is_nav_rail && children->count > 1)
        {
            struct uwp_element *last_ch = uwp_elem_from_any( children->items[children->count - 1] );
            if (last_ch && !wcscmp( uwp_elem_text( last_ch ), L"Home" )) rev = TRUE;
        }
        for (k = 0; k < children->count; k++)
        {
            unsigned int idx = rev ? (children->count - 1 - k) : k;
            struct uwp_element *ch = uwp_elem_from_any( children->items[idx] );
            RECT crc;
            if (!ch || ch->visibility != 0) continue;
            if (is_nav_rail && (uwp_is_nav_header( ch ) || uwp_is_nav_separator( ch ) || uwp_is_nav_settings_item( ch )))
            {
                uwp_clear_bounds_tree( ch );
                continue;
            }
            SetRect( &crc, rc.left + 4, cur_y, rc.right - 4, min( (int)rc.bottom, cur_y + item_h ) );
            uwp_arrange_element( ch, crc );
            cur_y += item_h + 4;
        }
    }
}

static BOOL arrange_list_or_grid_view( struct uwp_element *el, RECT rc )
{
    struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
    unsigned int k;
    int avail_w = rc.right - rc.left, avail_h = rc.bottom - rc.top;

    if (!uwp_is_horiz_listview( el ))
    {
        int cur_y = rc.top, gap = (int)(12.0f * g_layout_scale + 0.5f);
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            double nh;
            int item_h;
            RECT crc;
            if (!ch || ch->visibility != 0 || (ch->class_flags & UWP_CF_TEMPLATE)) continue;
            nh = uwp_measure_natural_height( ch, avail_w, 0 );
            if (nh <= 0.0) nh = uwp_estimate_auto_height( ch, 0 );
            if (nh <= 0.0) nh = 110.0 * g_layout_scale;
            item_h = max( 48, (int)(nh + uwp_clamp_top_margin( ch->margin_t ) + max( 0.0, ch->margin_b ) + 0.5) );
            SetRect( &crc, rc.left, cur_y, rc.right, cur_y + item_h );
            if (!ch->parent) ch->parent = el;
            uwp_arrange_element( ch, crc );
            cur_y += item_h + gap;
        }
        return TRUE;
    }
    if (avail_w >= 480 && avail_h > 0 && avail_h <= 400)
    {
        int pad = 8, gap = 12, idx = 0;
        int item_h = max( 64, avail_h - pad );
        int item_w = max( 180, min( 320, (item_h * 16) / 9 ) );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            int x0;
            RECT crc;
            if (!ch || ch->visibility != 0 || (ch->class_flags & UWP_CF_TEMPLATE)) continue;
            x0 = rc.left + pad + idx * (item_w + gap);
            if (x0 >= rc.right) { ch->has_bounds = FALSE; idx++; continue; }
            SetRect( &crc, x0, rc.top + pad / 2,
                     min( (int)rc.right, x0 + item_w ),
                     min( (int)rc.bottom, rc.top + pad / 2 + item_h ) );
            if (!ch->parent) ch->parent = el;
            uwp_arrange_element( ch, crc );
            idx++;
        }
        return TRUE;
    }
    return FALSE;
}

static void arrange_wrap_panel( struct uwp_element *el, RECT rc )
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
            cur_x = rc.left; cur_y += row_h + sp; row_h = 0;
        }
        crc.left = cur_x; crc.top = cur_y;
        crc.right = min( (int)rc.right, cur_x + cw );
        crc.bottom = min( (int)rc.bottom, cur_y + ch_h );
        uwp_arrange_element( ch, crc );
        cur_x = crc.right + sp;
        if (ch_h > row_h) row_h = ch_h;
    }
}

static void arrange_generic_children( struct uwp_element *el, RECT rc )
{
    struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
    unsigned int k;
    BOOL is_horiz_list = ((el->class_flags & (UWP_CF_LISTBOX | UWP_CF_ITEMS_CONTROL | UWP_CF_RADIO_BUTTON)) != 0) &&
                         (rc.bottom - rc.top) <= (int)(64 * g_layout_scale);
    int cur_x = rc.left;
    for (k = 0; k < children->count; k++)
    {
        struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
        if (!ch || ch == el || (ch->class_flags & UWP_CF_TEMPLATE)) continue;
        if (is_horiz_list && ch->visibility == 0)
        {
            double nw = uwp_measure_natural_width( ch, 0 );
            int cw = (nw > 0.0) ? (int)(nw + max( 0.0, ch->margin_l ) + max( 0.0, ch->margin_r ) + 0.5) : (rc.right - cur_x);
            RECT crc = { cur_x, rc.top, min( (int)rc.right, cur_x + cw ), rc.bottom };
            uwp_arrange_element( ch, crc );
            cur_x = crc.right + (int)(12 * g_layout_scale);
        }
        else
            uwp_arrange_element( ch, rc );
    }
}

static void uwp_arrange_element_inner( struct uwp_element *el, RECT rc )
{
    int w, h;
    double eff_w, eff_h, prev_w, prev_h;
    BOOL margin_clamped;

    if (!el) return;
    if (el->visibility != 0 || el->kind == UWP_KIND_STYLE || el->kind == UWP_KIND_BRUSH ||
        el->kind == UWP_KIND_FLYOUT || el->kind == UWP_KIND_POPUP || el->kind == UWP_KIND_DEFERRED ||
        el->kind == UWP_KIND_RESOURCE_DICT || el->kind == UWP_KIND_COLOR ||
        el->kind == UWP_KIND_ACCELERATOR || el->kind == UWP_KIND_XAML_COMMAND ||
        (uwp_is_teaching_tip( el ) && !el->is_open) || uwp_is_zero_layout_anchor( el ))
    {
        uwp_clear_bounds_tree( el );
        return;
    }

    margin_clamped = uwp_apply_margins_clamped( el, &rc );
    eff_w = el->width;
    if ((eff_w <= 0.0 || isnan( eff_w )) && el->h_align != 3 &&
        (el->kind == UWP_KIND_STACK_PANEL || el->kind == UWP_KIND_BORDER ||
         el->kind == UWP_KIND_USER_CONTROL || el->kind == UWP_KIND_CONTENT_CONTROL ||
         el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
         el->kind == UWP_KIND_GENERIC || el->kind == UWP_KIND_WRAP_PANEL || uwp_is_combobox( el )))
    {
        double nw = uwp_measure_natural_width( el, 0 );
        if (nw > 0.0) eff_w = nw;
    }
    if (el->max_width > 0.0 && !isnan( el->max_width ) && (eff_w <= 0.0 || eff_w > el->max_width) &&
        rc.right - rc.left > (LONG)el->max_width)
        eff_w = el->max_width;
    w = uwp_align_axis( &rc.left, &rc.right, eff_w, el->h_align );

    eff_h = el->height;
    if (eff_h > 148.0 * g_layout_scale && w >= 480 && uwp_has_horiz_listview_child( el, 0 ))
        eff_h = 148.0 * g_layout_scale;
    if ((eff_h <= 0.0 || isnan( eff_h )) && (el->class_flags & UWP_CF_CONSTRAINED_BOX) && w > 0 && w < (rc.bottom - rc.top))
    {
        eff_h = (double)w;
        rc.bottom = rc.top + w;
    }
    if ((eff_h <= 0.0 || isnan( eff_h )) && el->v_align != 3 &&
        (el->kind == UWP_KIND_STACK_PANEL || el->kind == UWP_KIND_TEXT_BLOCK ||
         el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
         el->kind == UWP_KIND_BORDER || uwp_is_combobox( el )))
    {
        double nh = uwp_measure_natural_height( el, w, 0 );
        if (nh > 0.0) eff_h = nh;
    }
    if (el->max_height > 0.0 && !isnan( el->max_height ) && (eff_h <= 0.0 || eff_h > el->max_height) &&
        rc.bottom - rc.top > (LONG)el->max_height)
        eff_h = el->max_height;
    h = uwp_align_axis( &rc.top, &rc.bottom, eff_h, el->v_align );

    prev_w = el->actual_w; prev_h = el->actual_h;
    el->bounds = rc;
    el->has_bounds = (w > 0 && h > 0);
    el->actual_w = (double)w;
    el->actual_h = (double)h;
    if (uwp_loaded_needs_fire( el )) uwp_fire_loaded_tree( el );
    if (!margin_clamped && w > 0 && h > 0 &&
        (prev_w != el->actual_w || prev_h != el->actual_h || el->last_sc_w == 0.0f || UWP_EV_CNT( el, layout_updated_reg ) > 0))
        uwp_fire_size_and_layout_changed( el );

    uwp_invoke_winui_measure_arrange( el, w, h );
    uwp_fire_evc_if_needed( el );
    if (el->kind == UWP_KIND_IMAGE) uwp_resolve_and_load_image( el );
    if (el->media && el->media->img_pixels) uwp_on_image_loaded( el );
    if (el->items_source && !uwp_panel_has_concrete_child( el ))
        uwp_items_ctrl_materialize_source( el, el->items_source );

    if (el->kind == UWP_KIND_GRID && el->children_coll) arrange_grid( el, rc, w, h );
    else if (el->kind == UWP_KIND_STACK_PANEL && el->children_coll) arrange_stack_panel( el, rc, w, h );
    else if (uwp_is_menubar( el )) { arrange_menu_bar( el, rc ); return; }
    else if (uwp_is_pivot( el ) && el->children_coll) { arrange_pivot( el, rc, h ); return; }

    if (uwp_is_nav_view( el )) { arrange_nav_view( el, rc ); return; }
    if (uwp_is_items_repeater( el ) && el->children_coll) { arrange_items_repeater( el, rc ); return; }
    if ((el->class_flags & (UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW)) && el->children_coll && arrange_list_or_grid_view( el, rc ))
        return;

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_arrange_element( ch, rc );
    }
    if (el->kind == UWP_KIND_WRAP_PANEL && el->children_coll) { arrange_wrap_panel( el, rc ); return; }
    if (el->kind != UWP_KIND_GRID && el->kind != UWP_KIND_STACK_PANEL && el->kind != UWP_KIND_WRAP_PANEL &&
        !uwp_is_combobox( el ) && !uwp_is_autosuggest( el ) && el->children_coll)
        arrange_generic_children( el, rc );
}
