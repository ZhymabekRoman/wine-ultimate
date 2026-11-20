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
#include <wctype.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "appmodel.h"
#include "weakreference.h"
#include "winreg.h"
#include "wincodec.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

COLORREF argb_to_colorref( UINT32 argb )
{
    return RGB( (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff );
}

UINT32 apply_opacity_to_argb( UINT32 argb, double opacity )
{
    BYTE a;
    if (opacity >= 0.999) return argb;
    if (opacity <= 0.0) return argb & 0x00ffffffu;
    a = (BYTE)((double)((argb >> 24) & 0xff) * opacity + 0.5);
    return (argb & 0x00ffffffu) | ((UINT32)a << 24);
}

COLORREF blend_argb_over( UINT32 fg_argb, COLORREF bg_cr )
{
    BYTE a = (BYTE)((fg_argb >> 24) & 0xff);
    BYTE r = (BYTE)((fg_argb >> 16) & 0xff);
    BYTE g = (BYTE)((fg_argb >> 8) & 0xff);
    BYTE b = (BYTE)(fg_argb & 0xff);
    BYTE br, bg, bb;
    if (a == 255) return RGB( r, g, b );
    if (a == 0) return bg_cr;
    br = GetRValue( bg_cr );
    bg = GetGValue( bg_cr );
    bb = GetBValue( bg_cr );
    return RGB( (BYTE)((r * a + br * (255 - a)) / 255),
                (BYTE)((g * a + bg * (255 - a)) / 255),
                (BYTE)((b * a + bb * (255 - a)) / 255) );
}

static int hex_nib( WCHAR c )
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
    return -1;
}

BOOL uwp_parse_color_string( const WCHAR *s, UINT32 *out_argb )
{
    size_t len;
    if (!s || !s[0] || !out_argb) return FALSE;
    while (*s == L' ' || *s == L'\t') s++;
    if (s[0] == L'#')
    {
        UINT32 v = 0;
        size_t i;
        s++;
        len = wcslen( s );
        while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t')) len--;
        if (len != 3 && len != 4 && len != 6 && len != 8) return FALSE;
        for (i = 0; i < len; i++)
        {
            int n = hex_nib( s[i] );
            if (n < 0) return FALSE;
            v = (v << 4) | (UINT32)n;
        }
        if (len == 3)
        {
            UINT32 r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            *out_argb = 0xff000000u | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
        }
        else if (len == 4)
        {
            UINT32 a = (v >> 12) & 0xf, r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            *out_argb = (a * 0x11 << 24) | (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
        }
        else if (len == 6)
            *out_argb = 0xff000000u | v;
        else
            *out_argb = v;
        return TRUE;
    }
    if (!wcsicmp( s, L"Transparent" )) { *out_argb = 0x00ffffffu; return TRUE; }
    if (!wcsicmp( s, L"Black" ))       { *out_argb = 0xff000000u; return TRUE; }
    if (!wcsicmp( s, L"White" ))       { *out_argb = 0xffffffffu; return TRUE; }
    if (!wcsicmp( s, L"Red" ))         { *out_argb = 0xffff0000u; return TRUE; }
    if (!wcsicmp( s, L"Green" ))       { *out_argb = 0xff008000u; return TRUE; }
    if (!wcsicmp( s, L"Blue" ))        { *out_argb = 0xff0000ffu; return TRUE; }
    if (!wcsicmp( s, L"Gray" ))        { *out_argb = 0xff808080u; return TRUE; }
    if (!wcsicmp( s, L"DarkGray" ))    { *out_argb = 0xffa9a9a9u; return TRUE; }
    if (!wcsicmp( s, L"LightGray" ))   { *out_argb = 0xffd3d3d3u; return TRUE; }
    if (!wcsicmp( s, L"Yellow" ))      { *out_argb = 0xffffff00u; return TRUE; }
    if (!wcsicmp( s, L"Orange" ))      { *out_argb = 0xffffa500u; return TRUE; }
    return FALSE;
}

void store_make_icon_monogram( const WCHAR *title, WCHAR *icon, size_t icon_len )
{
    size_t n = 0;
    const WCHAR *p = title;
    BOOL take_next = TRUE;

    while (*p && n + 1 < icon_len && n < 3)
    {
        WCHAR ch = *p++;
        if (ch == L'.' || ch == L' ' || ch == L'-' || ch == L'_')
        {
            take_next = TRUE;
            continue;
        }
        if (take_next)
        {
            if (ch >= L'a' && ch <= L'z') ch -= (L'a' - L'A');
            icon[n++] = ch;
            take_next = FALSE;
        }
    }
    if (!n && title[0])
    {
        WCHAR ch = title[0];
        if (ch >= L'a' && ch <= L'z') ch -= (L'a' - L'A');
        icon[n++] = ch;
    }
    icon[n] = 0;
}

void uwp_el_set_bg_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->bg_argb = argb;
    el->bg_color = argb_to_colorref( argb );
    el->bg_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_bg = TRUE;
}

void uwp_el_set_fg_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->fg_argb = argb;
    el->fg_color = argb_to_colorref( argb );
    el->fg_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_fg = TRUE;
}

void uwp_el_set_border_argb( struct uwp_element *el, UINT32 argb )
{
    if (!el) return;
    el->border_argb = argb;
    el->border_color = argb_to_colorref( argb );
    el->border_alpha = (BYTE)((argb >> 24) & 0xff);
    el->has_border = TRUE;
}

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
    return pid == XBF_PROP_CONTROL_BORDER_BRUSH || pid == XBF_PROP_BORDER_BRUSH || pid == XBF_PROP_GRID_BORDER_BRUSH || pid == XBF_PROP_SHAPE_STROKE;
}

struct uwp_size_f
{
    float width;
    float height;
};

static const GUID IID_IFrameworkElementOverrides_layout =
    { 0xda007e54, 0xb3c2, 0x4b9a, { 0xaa, 0x8e, 0xd3, 0xf0, 0x71, 0x26, 0x2b, 0x97 } };

static void uwp_invoke_winui_measure_arrange( struct uwp_element *el, int w, int h )
{
    void *fe_ovr = NULL;
    struct uwp_size_f avail, desired = {0}, arranged = {0};
    HRESULT hr;

    if (!el || !el->outer || el->in_measure_arrange || w <= 0 || h <= 0) return;
    if (!wcsstr( el->class_name, L"ItemsRepeater" )) return;
    if (el->last_measured_w == (float)w && el->last_measured_h == (float)h && el->children_coll &&
        CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count > 0)
        return;

    el->in_measure_arrange = TRUE;
    el->last_measured_w = (float)w;
    el->last_measured_h = (float)h;
    avail.width = (float)w;
    avail.height = (float)h;

    if (SUCCEEDED(IInspectable_QueryInterface( el->outer, &IID_IFrameworkElementOverrides_layout, &fe_ovr )) && fe_ovr)
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
                                static const GUID IID_IVectorView_Obj =
                                    { 0xa6487363, 0xb074, 0x5c60, { 0xab, 0x16, 0x86, 0x6d, 0xce, 0x4e, 0xe5, 0x4d } };
                                void *vv = NULL;
                                IInspectable_GetRuntimeClassName( el->dp_store[di].value, &hc );
                                if (SUCCEEDED(IInspectable_QueryInterface( el->dp_store[di].value, &IID_IVectorView_Obj, &vv )) && vv)
                                {
                                    HRESULT (WINAPI *get_sz)(void *, UINT32 *) = (void *)(*(void ***)vv)[7];
                                    if (get_sz) get_sz( vv, &vsz );
                                    IUnknown_Release( (IUnknown *)vv );
                                }
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

static BOOL uwp_scroll_child_has_content( struct uwp_element *el, int depth )
{
    if (!el || el->visibility != 0 || depth > 14) return FALSE;
    if (uwp_has_shimmer_descendant( el, 0 ) && el->kind == UWP_KIND_BORDER) return FALSE;
    if (el->text[0] || el->img_pixels || el->img_loaded) return TRUE;
    if (wcsstr( el->class_name, L"Spotlight" )) return TRUE;
    if (wcsstr( el->class_name, L"ItemsRepeater" ))
    {
        if (el->data_context || el->dp_cnt >= 2) return TRUE;
        if (el->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            if (v->count > 0) return TRUE;
        }
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

void uwp_arrange_element( struct uwp_element *el, RECT rc )
{
    int w, h;
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

    rc.left   += (int)el->margin_l;
    rc.top    += (int)el->margin_t;
    rc.right  -= (int)el->margin_r;
    rc.bottom -= (int)el->margin_b;
    if (rc.right < rc.left) rc.right = rc.left;
    if (rc.bottom < rc.top) rc.bottom = rc.top;

    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (el->width > 0.0 && !isnan( el->width ))
    {
        int ew = (int)(el->width + 0.5);
        if (ew < w)
        {
            if (el->h_align == 1) rc.left += (w - ew) / 2;
            else if (el->h_align == 2) rc.left = rc.right - ew;
            rc.right = rc.left + ew;
            w = ew;
        }
    }
    if (el->height > 0.0 && !isnan( el->height ))
    {
        int eh = (int)(el->height + 0.5);
        if (eh < h)
        {
            if (el->v_align == 1) rc.top += (h - eh) / 2;
            else if (el->v_align == 2) rc.top = rc.bottom - eh;
            rc.bottom = rc.top + eh;
            h = eh;
        }
    }
    {
        double prev_w = el->actual_w, prev_h = el->actual_h;
        el->bounds = rc;
        el->has_bounds = (w > 0 && h > 0);
        el->actual_w = (double)w;
        el->actual_h = (double)h;
        if (w > 0 && h > 0 && (prev_w != el->actual_w || prev_h != el->actual_h))
            uwp_fire_size_and_layout_changed( el );
    }

    if (!el->loaded_fired || el->loaded_fired_cnt < el->loaded_cnt)
        uwp_fire_loaded_tree( el );

    uwp_invoke_winui_measure_arrange( el, w, h );
    uwp_fire_evc_if_needed( el );

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

        /* Infer max row/col from children only for synthetic grids without explicit definitions */
        if (!rows && !cols)
        {
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                if (!ch || ch->visibility != 0 || ch->kind == UWP_KIND_DEFERRED) continue;
                if (ch->grid_row + ch->grid_row_span > nrows) nrows = ch->grid_row + ch->grid_row_span;
                if (ch->grid_col + ch->grid_col_span > ncols) ncols = ch->grid_col + ch->grid_col_span;
            }
        }
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
                        double cw = (ch->width > 0.0 && !isnan( ch->width )) ? ch->width
                                  : (ch->min_width > 0.0 ? ch->min_width : 160.0);
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
                r_type[i] = 1;
                r_val[i] = has_vis ? 36.0 * g_layout_scale : 0.0;
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
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch->visibility == 0 && ch->kind != UWP_KIND_DEFERRED &&
                ch->kind != UWP_KIND_ACCELERATOR && ch->kind != UWP_KIND_XAML_COMMAND &&
                !wcsstr( ch->class_name, L"TeachingTip" ) &&
                wcscmp( ch->name, L"TeachingTipTarget" ) != 0)
            {
                vis_cnt++;
                if (ch->height > 0.0 && !isnan( ch->height ) && (int)ch->height < h)
                    fixed_h += (int)(ch->height + 0.5);
                else
                    auto_cnt++;
            }
        }
        if (vis_cnt > 0)
        {
            int idx = 0, auto_idx = 0, cur_y = rc.top;
            int auto_total_h = max( 0, h - fixed_h );
            BOOL in_scroll = FALSE;
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
                    crc.left  = rc.left + (w * idx) / (int)vis_cnt;
                    crc.right = rc.left + (w * (idx + 1)) / (int)vis_cnt;
                }
                else if (ch->height > 0.0 && !isnan( ch->height ) && (int)ch->height < h)
                {
                    if (in_scroll && cur_y >= rc.bottom)
                    {
                        ch->has_bounds = FALSE;
                        idx++;
                        continue;
                    }
                    crc.top    = cur_y;
                    crc.bottom = min( (int)rc.bottom, cur_y + (int)(ch->height + 0.5) );
                    cur_y = crc.bottom;
                }
                else if (in_scroll)
                {
                    int nat_h;
                    if (ch->kind == UWP_KIND_TEXT_BLOCK)
                        nat_h = 44;
                    else if (!uwp_scroll_child_has_content( ch, 0 ))
                        nat_h = 20;
                    else
                        nat_h = 240;
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
                else
                {
                    int slice = (auto_cnt > 0)
                        ? ((auto_total_h * (auto_idx + 1)) / auto_cnt - (auto_total_h * auto_idx) / auto_cnt)
                        : 0;
                    crc.top    = cur_y;
                    crc.bottom = min( (int)rc.bottom, cur_y + slice );
                    cur_y = crc.bottom;
                    auto_idx++;
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

    if (!wcscmp( el->class_name, L"Microsoft.UI.Xaml.Controls.NavigationView" ))
    {
        int rail_w = 68;
        if (el->children_coll)
        {
            struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int k;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                RECT crc;
                if (!ch || ch->visibility != 0) continue;
                if (!wcscmp( ch->name, L"TogglePaneButton" ))
                    SetRect( &crc, rc.left + 8, rc.top + 8, rc.left + 56, rc.top + 48 );
                else if (!wcscmp( ch->name, L"MenuItemsHost" ))
                    SetRect( &crc, rc.left, rc.top + 56, rc.left + rail_w, max( rc.top + 120, rc.bottom - 180 ) );
                else if (!wcscmp( ch->name, L"FooterMenuItemsHost" ))
                    SetRect( &crc, rc.left, max( rc.top + 120, rc.bottom - 172 ), rc.left + rail_w, rc.bottom - 8 );
                else
                    crc = rc;
                uwp_arrange_element( ch, crc );
            }
        }
        if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el)
            {
                RECT content_rc;
                SetRect( &content_rc, rc.left + rail_w, rc.top, rc.right, rc.bottom );
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
        {
            int cur_y = rc.top;
            int item_h = is_nav_rail ? 48 : 80;
            for (k = 0; k < children->count; k++)
            {
                struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
                RECT crc;
                if (!ch || ch->visibility != 0) continue;
                SetRect( &crc, rc.left + 4, cur_y, rc.right - 4, min( (int)rc.bottom, cur_y + item_h ) );
                uwp_arrange_element( ch, crc );
                cur_y += item_h + 4;
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

    if (wcsstr( el->name, L"TileCard" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (wcsstr( ch->name, L"AppIcon" ))
                SetRect( &ch->bounds, rc.left + 12, rc.top + 12, rc.left + 54, rc.top + 54 );
            else if (wcsstr( ch->name, L"TitleText" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 10, rc.right - 10, rc.top + 30 );
            else if (wcsstr( ch->name, L"CategoryText" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 31, rc.right - 10, rc.top + 47 );
            else if (wcsstr( ch->name, L"RatingText" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 48, rc.right - 10, rc.top + 66 );
            else if (wcsstr( ch->name, L"GetButton" ))
                SetRect( &ch->bounds, rc.left + 64, rc.top + 70, rc.left + 138, rc.top + 96 );
            else
                ch->bounds = rc;
            ch->has_bounds = TRUE;
        }
        return;
    }
    if (!wcscmp( el->name, L"RootStoreGrid" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (!wcscmp( ch->name, L"NavRail" ))
            {
                RECT nrc;
                SetRect( &nrc, rc.left, rc.top, rc.left + 68, rc.bottom );
                uwp_arrange_element( ch, nrc );
            }
            else if (!wcscmp( ch->name, L"MainContentGrid" ))
            {
                RECT mrc;
                SetRect( &mrc, rc.left + 68, rc.top, rc.right, rc.bottom );
                uwp_arrange_element( ch, mrc );
            }
            else
                uwp_arrange_element( ch, rc );
        }
        return;
    }
    if (!wcscmp( el->name, L"NavRail" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int nav_y = rc.top + 60;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (!wcscmp( ch->name, L"StoreBrandLogo" ))
            {
                SetRect( &ch->bounds, rc.left + 4, rc.top + 16, rc.right - 4, rc.top + 44 );
                ch->has_bounds = TRUE;
            }
            else if (wcsstr( ch->name, L"NavItem" ))
            {
                if (wcsstr( ch->name, L"Library" ))
                    SetRect( &ch->bounds, rc.left + 4, rc.bottom - 56, rc.right - 4, rc.bottom - 12 );
                else
                {
                    SetRect( &ch->bounds, rc.left + 4, nav_y, rc.right - 4, nav_y + 48 );
                    nav_y += 54;
                }
                ch->has_bounds = TRUE;
            }
            else
                uwp_arrange_element( ch, rc );
        }
        return;
    }
    if (!wcscmp( el->name, L"MainContentGrid" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (!ch) continue;
            if (wcsstr( ch->name, L"SearchBox" ))
            {
                RECT src;
                SetRect( &src, rc.left + 24, rc.top + 14, min( rc.left + 520, rc.right - 40 ), rc.top + 50 );
                uwp_arrange_element( ch, src );
            }
            else if (wcsstr( ch->name, L"HeroBanner" ))
            {
                RECT hrc;
                SetRect( &hrc, rc.left + 24, rc.top + 64, rc.right - 24, rc.top + 188 );
                uwp_arrange_element( ch, hrc );
            }
            else if (wcsstr( ch->name, L"TopAppsHeader" ))
            {
                RECT thrc;
                SetRect( &thrc, rc.left + 24, rc.top + 204, rc.right - 24, rc.top + 234 );
                uwp_arrange_element( ch, thrc );
            }
            else if (wcsstr( ch->name, L"HomeIslandTilesGrid" ))
            {
                RECT tgrc;
                SetRect( &tgrc, rc.left + 24, rc.top + 244, rc.right - 24, rc.bottom - 16 );
                uwp_arrange_element( ch, tgrc );
            }
            else
                uwp_arrange_element( ch, rc );
        }
        return;
    }
    if (!wcscmp( el->name, L"HomeIslandTilesGrid" ) && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        int tw = rc.right - rc.left;
        int cw = max( 180, (tw - 3 * 16) / 4 );
        int ch = 106;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch_elem = uwp_elem_from_any( children->items[k] );
            int r, c;
            RECT card_rc;
            if (!ch_elem) continue;
            r = (int)(k / 4);
            c = (int)(k % 4);
            SetRect( &card_rc, rc.left + c * (cw + 16), rc.top + r * (ch + 16),
                     rc.left + c * (cw + 16) + cw, rc.top + r * (ch + 16) + ch );
            uwp_arrange_element( ch_elem, card_rc );
        }
        return;
    }
    if (el->kind != UWP_KIND_GRID && el->kind != UWP_KIND_STACK_PANEL && el->children_coll)
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

/* Format Segoe MDL2 Assets / private use glyphs to readable symbols if font lacks them */
const WCHAR *uwp_format_display_label( const struct uwp_element *el, WCHAR *tmp, size_t tmp_len )
{
    const WCHAR *s = el->text;
    if (el->name[0])
    {
        if (!wcscmp( el->name, L"DecimalSeparatorButton" )) return L".";
        if (!wcscmp( el->name, L"NegateButton" )) return L"+/-";
        if (!wcscmp( el->name, L"ClearMemoryButton" )) return L"MC";
        if (!wcscmp( el->name, L"MemRecall" )) return L"MR";
        if (!wcscmp( el->name, L"MemPlus" )) return L"M+";
        if (!wcscmp( el->name, L"MemMinus" )) return L"M-";
        if (!wcscmp( el->name, L"MemButton" )) return L"MS";
        if (!wcscmp( el->name, L"MemoryButton" )) return L"M\x25be";
    }
    if (!s || !s[0]) return L"";
    if (s[1] == 0)
    {
        switch (s[0])
        {
        case 0xe94c: return L"%";
        case 0xe94f: return L"\x232b";
        case 0xe94a: return L"\x00f7";
        case 0xe947: return L"\x00d7";
        case 0xe949: return L"-";
        case 0xe948: return L"+";
        case 0xe94e: return L"=";
        case 0xe94d: return L"+/-";
        case 0xf7c9: return L"1/x";
        case 0xf7c8: return L"x\x00b2";
        case 0xf899: return L"\x221a";
        case 0xe700: return L"\x2630";
        case 0xe81c: return L"H";
        case 0xee49: return L"\x29c9";
        }
    }
    (void)tmp; (void)tmp_len;
    return s;
}

COLORREF g_paint_page_bg = RGB( 31, 31, 31 );

void uwp_load_image_asset( struct uwp_element *el )
{
    WCHAR rel[MAX_PATH] = {0}, base_dir[MAX_PATH] = {0}, candidate[MAX_PATH] = {0};
    const WCHAR *p = el->image_src;
    static const WCHAR *scales[] = { L"", L".scale-100", L".scale-125", L".scale-150", L".scale-200", L".scale-400" };
    WCHAR *slash, *dot;
    size_t i;
    IWICImagingFactory *wic = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    UINT w = 0, h = 0;

    if (!el || el->img_loaded || !el->image_src[0]) return;
    el->img_loaded = TRUE;

    if (!wcsncmp( p, L"ms-appx:///", 11 )) p += 11;
    else if (!wcsncmp( p, L"ms-appx://", 10 )) p += 10;
    while (*p == '/' || *p == '\\') p++;
    lstrcpynW( rel, p, ARRAY_SIZE(rel) );
    for (i = 0; rel[i]; i++) if (rel[i] == '/') rel[i] = '\\';

    GetModuleFileNameW( NULL, base_dir, ARRAY_SIZE(base_dir) );
    slash = wcsrchr( base_dir, '\\' );
    if (slash) *(slash + 1) = 0;

    dot = wcsrchr( rel, '.' );
    for (i = 0; i < ARRAY_SIZE(scales); i++)
    {
        if (i > 0 && !dot) break;
        if (i == 0)
            swprintf( candidate, ARRAY_SIZE(candidate), L"%s%s", base_dir, rel );
        else
        {
            WCHAR stem[MAX_PATH] = {0};
            size_t slen = min( (size_t)(dot - rel), ARRAY_SIZE(stem) - 1 );
            memcpy( stem, rel, slen * sizeof(WCHAR) );
            stem[slen] = 0;
            swprintf( candidate, ARRAY_SIZE(candidate), L"%s%s%s%s", base_dir, stem, scales[i], dot );
        }
        if (GetFileAttributesW( candidate ) != INVALID_FILE_ATTRIBUTES) break;
        candidate[0] = 0;
    }
    if (!candidate[0]) return;

    if (SUCCEEDED(CoCreateInstance( &CLSID_WICImagingFactory_local, NULL, CLSCTX_INPROC_SERVER,
                                    &IID_IWICImagingFactory_local, (void **)&wic )) && wic)
    {
        if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename( wic, candidate, NULL, GENERIC_READ,
                                                                    WICDecodeMetadataCacheOnLoad, &dec )) && dec)
        {
            if (SUCCEEDED(IWICBitmapDecoder_GetFrame( dec, 0, &frame )) && frame)
            {
                if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter( wic, &conv )) && conv)
                {
                    if (SUCCEEDED(IWICFormatConverter_Initialize( conv, (IWICBitmapSource *)frame,
                                                                  &GUID_WICPixelFormat32bppBGRA_local,
                                                                  WICBitmapDitherTypeNone, NULL, 0.0,
                                                                  WICBitmapPaletteTypeCustom )) &&
                        SUCCEEDED(IWICFormatConverter_GetSize( conv, &w, &h )) &&
                        w > 0 && h > 0 && w <= 4096 && h <= 4096)
                    {
                        BYTE *buf = malloc( (size_t)w * h * 4 );
                        if (buf)
                        {
                            if (SUCCEEDED(IWICFormatConverter_CopyPixels( conv, NULL, w * 4, w * h * 4, buf )))
                            {
                                size_t npix = (size_t)w * h, px;
                                BYTE pbr = GetRValue( g_paint_page_bg );
                                BYTE pbg = GetGValue( g_paint_page_bg );
                                BYTE pbb = GetBValue( g_paint_page_bg );
                                for (px = 0; px < npix; px++)
                                {
                                    BYTE b = buf[px * 4 + 0];
                                    BYTE g = buf[px * 4 + 1];
                                    BYTE r = buf[px * 4 + 2];
                                    BYTE a = buf[px * 4 + 3];
                                    buf[px * 4 + 0] = (BYTE)((b * a + pbb * (255 - a)) / 255);
                                    buf[px * 4 + 1] = (BYTE)((g * a + pbg * (255 - a)) / 255);
                                    buf[px * 4 + 2] = (BYTE)((r * a + pbr * (255 - a)) / 255);
                                    buf[px * 4 + 3] = 255;
                                }
                                el->img_pixels = buf;
                                el->img_w = w;
                                el->img_h = h;
                                TRACE( "uwp_load_image_asset: loaded %s (%ux%u)\n", debugstr_w(candidate), w, h );
                            }
                            else free( buf );
                        }
                    }
                    IWICFormatConverter_Release( conv );
                }
                IWICBitmapFrameDecode_Release( frame );
            }
            IWICBitmapDecoder_Release( dec );
        }
        IWICImagingFactory_Release( wic );
    }
}

COLORREF uwp_paint_resolve_color( struct uwp_element *el, const WCHAR *key,
                                         COLORREF fallback, COLORREF surface_bg )
{
    UINT32 argb = 0;
    if (key && uwp_resolve_brush_or_color( NULL, el, key, 0, &argb ))
        return blend_argb_over( argb, surface_bg );
    return fallback;
}

static void uwp_paint_text( HDC hdc, const RECT *rc, const WCHAR *text, int height,
                            int weight, UINT flags, COLORREF cr )
{
    HFONT font = CreateFontW( -height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
    HGDIOBJ old = font ? SelectObject( hdc, font ) : NULL;
    COLORREF old_cr = SetTextColor( hdc, cr );
    SetBkMode( hdc, TRANSPARENT );
    DrawTextW( hdc, text, -1, (RECT *)rc, flags | DT_SINGLELINE );
    SetTextColor( hdc, old_cr );
    if (old) SelectObject( hdc, old );
    if (font) DeleteObject( font );
}

static void uwp_paint_element( HDC hdc, struct uwp_element *el )
{
    COLORREF surface_bg = g_paint_page_bg;
    if (!el || !el->has_bounds || el->visibility != 0) return;
    TRACE( "uwp_paint %s (%s) kind %d bounds %ld,%ld-%ld,%ld\n",
           debugstr_w(el->class_name), debugstr_w(el->name), el->kind,
           el->bounds.left, el->bounds.top, el->bounds.right, el->bounds.bottom );

    if (el->kind == UWP_KIND_IMAGE)
    {
        uwp_load_image_asset( el );
        if (el->img_pixels && el->img_w > 0 && el->img_h > 0)
        {
            RECT rc = el->bounds;
            int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
            int dw = (int)el->img_w, dh = (int)el->img_h;
            int dx, dy;
            BITMAPINFO bmi = {0};
            void *dib_bits = NULL;
            HDC memdc = CreateCompatibleDC( hdc );
            HBITMAP hbm;

            if (dw > bw && bw > 0) { dh = MulDiv( dh, bw, dw ); dw = bw; }
            if (dh > bh && bh > 0) { dw = MulDiv( dw, bh, dh ); dh = bh; }
            dx = rc.left + (bw - dw) / 2;
            dy = rc.top + (bh - dh) / 2;
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = (LONG)el->img_w;
            bmi.bmiHeader.biHeight = -(LONG)el->img_h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            hbm = CreateDIBSection( memdc, &bmi, DIB_RGB_COLORS, &dib_bits, NULL, 0 );
            if (memdc && hbm && dib_bits)
            {
                HGDIOBJ old_bm = SelectObject( memdc, hbm );
                BOOL ok;
                memcpy( dib_bits, el->img_pixels, (size_t)el->img_w * el->img_h * 4 );
                if (dw == (int)el->img_w && dh == (int)el->img_h)
                    ok = BitBlt( hdc, dx, dy, dw, dh, memdc, 0, 0, SRCCOPY );
                else
                {
                    SetStretchBltMode( hdc, HALFTONE );
                    ok = StretchBlt( hdc, dx, dy, dw, dh, memdc, 0, 0, el->img_w, el->img_h, SRCCOPY );
                }
                TRACE( "uwp_paint IMAGE (%dx%d -> %d,%d %dx%d) ok=%d center_px=%06lx\n",
                       el->img_w, el->img_h, dx, dy, dw, dh, ok, GetPixel( hdc, dx + dw / 2, dy + dh / 2 ) );
                SelectObject( memdc, old_bm );
            }
            if (hbm) DeleteObject( hbm );
            if (memdc) DeleteDC( memdc );
        }
        return;
    }

    if (el->kind == UWP_KIND_PROGRESS_RING)
    {
        RECT rc = el->bounds;
        int cx = (rc.left + rc.right) / 2;
        int cy = min( rc.bottom - 36, (rc.top + rc.bottom) / 2 + 120 );
        int r = 18;
        COLORREF track_cr = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 68, 68, 68 ), surface_bg );
        COLORREF arc_cr = (el->has_fg && el->fg_alpha > 0)
            ? blend_argb_over( el->fg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"AccentBrush", uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
        HPEN track_pen = CreatePen( PS_SOLID, 3, track_cr );
        HPEN arc_pen = CreatePen( PS_SOLID, 3, arc_cr );
        HBRUSH null_br = GetStockObject( NULL_BRUSH );
        HGDIOBJ old_pen = SelectObject( hdc, track_pen );
        HGDIOBJ old_br = SelectObject( hdc, null_br );
        BOOL eok = Ellipse( hdc, cx - r, cy - r, cx + r, cy + r );
        BOOL aok;
        SelectObject( hdc, arc_pen );
        aok = Arc( hdc, cx - r, cy - r, cx + r, cy + r, cx, cy - r, cx - r, cy );
        TRACE( "uwp_paint PROGRESS_RING (%d,%d r=%d) eok=%d aok=%d top_px=%06lx\n",
               cx, cy, r, eok, aok, GetPixel( hdc, cx, cy - r ) );
        SelectObject( hdc, old_br );
        SelectObject( hdc, old_pen );
        DeleteObject( track_pen );
        DeleteObject( arc_pen );
        return;
    }

    if (wcsstr( el->name, L"NavRail" ))
    {
        RECT rc = el->bounds;
        COLORREF rail_bg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"BodyBackgroundBrush", RGB( 22, 22, 22 ), surface_bg );
        COLORREF rail_bd = (el->has_border && el->border_alpha > 0)
            ? blend_argb_over( el->border_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"BodyBackgroundStroke", RGB( 38, 38, 38 ), surface_bg );
        HBRUSH br = CreateSolidBrush( rail_bg );
        HPEN pen = CreatePen( PS_SOLID, 1, rail_bd );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        Rectangle( hdc, rc.left, rc.top, rc.right, rc.bottom );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( pen );
        DeleteObject( br );
    }
    else if (wcsstr( el->class_name, L"AppTitleBar" ))
    {
        if (el->text[0])
        {
            RECT trc = { el->bounds.left + 12, el->bounds.top, el->bounds.left + 200, el->bounds.bottom };
            uwp_paint_text( hdc, &trc, el->text, 13, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                            uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg ) );
        }
    }
    else if (wcsstr( el->name, L"ContentFrame1" ))
    {
        RECT rc = el->bounds;
        COLORREF cbg = uwp_paint_resolve_color( el, L"CardBackgroundFillColorDefaultBrush", RGB( 40, 40, 40 ), surface_bg );
        HBRUSH br = CreateSolidBrush( cbg );
        HGDIOBJ old_br = SelectObject( hdc, br );
        Rectangle( hdc, rc.left, rc.top, rc.right, rc.bottom );
        SelectObject( hdc, old_br );
        DeleteObject( br );
    }
    else if (wcsstr( el->name, L"SearchBox" ) || wcsstr( el->class_name, L"AutoSuggestBox" ) ||
             wcsstr( el->name, L"SuggestBox" ))
    {
        RECT rc = el->bounds;
        BOOL focused = (g_focused_textbox == el);
        COLORREF sbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 42, 42, 42 ), surface_bg );
        COLORREF sbd = focused
            ? uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg )
            : uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 90, 90, 90 ), surface_bg );
        COLORREF sfg = el->text[0]
            ? uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), sbg )
            : ((el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, sbg )
                : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 185, 185, 185 ), sbg ));
        HBRUSH br = CreateSolidBrush( sbg );
        HPEN pen = CreatePen( PS_SOLID, focused ? 2 : 1, sbd );
        HFONT font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        HGDIOBJ old_font = SelectObject( hdc, font );
        WCHAR disp[264];
        const WCHAR *fallback = wcsstr( el->name, L"SearchBox" )
            ? L"\x26b2  Search apps, games, movies and more"
            : L"Find";
        if (el->text[0])
            swprintf( disp, ARRAY_SIZE(disp), focused ? L"\x26b2  %s|" : L"\x26b2  %s", el->text );
        else if (focused)
            lstrcpynW( disp, L"\x26b2  |", ARRAY_SIZE(disp) );
        else
            lstrcpynW( disp, fallback, ARRAY_SIZE(disp) );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6 );
        SetTextColor( hdc, sfg );
        SetBkMode( hdc, TRANSPARENT );
        rc.left += 10;
        DrawTextW( hdc, disp, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( font );
        DeleteObject( pen );
        DeleteObject( br );
        return;
    }
    else if (wcsstr( el->class_name, L"TeachingTip" ) && el->is_open)
    {
        RECT rc = el->bounds;
        COLORREF tbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"SolidBackgroundFillColorTertiaryBrush", RGB( 44, 44, 44 ), surface_bg );
        COLORREF tbd = uwp_paint_resolve_color( el, L"SurfaceStrokeColorFlyoutBrush", RGB( 82, 82, 82 ), surface_bg );
        HBRUSH br = CreateSolidBrush( tbg );
        HPEN pen = CreatePen( PS_SOLID, 1, tbd );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8 );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( pen );
        DeleteObject( br );
    }
    else if (wcsstr( el->name, L"HeroBanner" ))
    {
        RECT rc = el->bounds, trc;
        COLORREF hbg = uwp_paint_resolve_color( el, L"SystemAccentColorDark2", RGB( 20, 36, 56 ), surface_bg );
        COLORREF hbd = uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg );
        COLORREF hfg = uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), hbg );
        COLORREF hsfg = uwp_paint_resolve_color( el, L"AccentTextFillColorPrimaryBrush", RGB( 200, 220, 240 ), hbg );
        HBRUSH br = CreateSolidBrush( hbg );
        HPEN pen = CreatePen( PS_SOLID, 1, hbd );
        HFONT title_font = CreateFontW( -22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HFONT sub_font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        HGDIOBJ old_font;
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12 );
        SetBkMode( hdc, TRANSPARENT );
        trc = rc;
        trc.left += 28; trc.top += 24; trc.right -= 28;
        old_font = SelectObject( hdc, title_font );
        SetTextColor( hdc, hfg );
        DrawTextW( hdc, L"Featured: Essential Windows Apps", -1, &trc, DT_LEFT | DT_TOP | DT_SINGLELINE );
        SelectObject( hdc, sub_font );
        trc.top += 34;
        SetTextColor( hdc, hsfg );
        DrawTextW( hdc, L"Discover top-rated tools for communication, productivity, and entertainment.", -1, &trc, DT_LEFT | DT_TOP | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( title_font );
        DeleteObject( sub_font );
        DeleteObject( pen );
        DeleteObject( br );
        return;
    }
    else if (wcsstr( el->name, L"TileCard" ))
    {
        RECT rc = el->bounds;
        COLORREF cbg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"ProductCardBackground", uwp_paint_resolve_color( el, L"CardBackgroundFillColorDefaultBrush", RGB( 38, 38, 38 ), surface_bg ), surface_bg );
        COLORREF cbd = (el->has_border && el->border_alpha > 0)
            ? blend_argb_over( el->border_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"DefaultCardBorderBrush", RGB( 58, 58, 58 ), surface_bg );
        HBRUSH br = CreateSolidBrush( cbg );
        HPEN pen = CreatePen( PS_SOLID, 1, cbd );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 10, 10 );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( pen );
        DeleteObject( br );
    }
    else if (wcsstr( el->name, L"AppIcon" ))
    {
        static const COLORREF colors[] = {
            RGB(37, 211, 102), RGB(30, 215, 96), RGB(229, 9, 20), RGB(0, 122, 204),
            RGB(16, 124, 65), RGB(88, 101, 242), RGB(85, 170, 85), RGB(255, 136, 0)
        };
        RECT rc = el->bounds;
        int idx = (int)(el->bounds.left / 100 + el->bounds.top / 80) % ARRAY_SIZE(colors);
        HBRUSH br = CreateSolidBrush( colors[idx] );
        HPEN pen = CreatePen( PS_SOLID, 1, RGB( 70, 70, 70 ) );
        HFONT font = CreateFontW( -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        HGDIOBJ old_br = SelectObject( hdc, br );
        HGDIOBJ old_pen = SelectObject( hdc, pen );
        HGDIOBJ old_font = SelectObject( hdc, font );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8 );
        SetTextColor( hdc, RGB( 255, 255, 255 ) );
        SetBkMode( hdc, TRANSPARENT );
        if (el->text[0])
            DrawTextW( hdc, el->text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( font );
        DeleteObject( pen );
        DeleteObject( br );
        return;
    }
    else if (el->kind != UWP_KIND_BUTTON && el->kind != UWP_KIND_TOGGLE_BUTTON && el->kind != UWP_KIND_TEXT_BLOCK)
    {
        /* Paint resolved Background / BorderBrush for containers (Page, UserControl, Grid, Border, StackPanel) */
        if (el->has_bg && el->bg_alpha > 0)
        {
            COLORREF bg = blend_argb_over( el->bg_argb, surface_bg );
            HBRUSH br = CreateSolidBrush( bg );
            FillRect( hdc, &el->bounds, br );
            DeleteObject( br );
        }
        if (el->has_border && el->border_alpha > 0)
        {
            COLORREF bd = blend_argb_over( el->border_argb, surface_bg );
            HBRUSH br = CreateSolidBrush( bd );
            FrameRect( hdc, &el->bounds, br );
            DeleteObject( br );
        }
        if (wcsstr( el->class_name, L"TabView" ) && !wcsstr( el->class_name, L"TabViewItem" ))
        {
            RECT trc = el->bounds;
            IInspectable *items[64];
            int count = uwp_tabview_get_items( el, items, ARRAY_SIZE(items) );
            int i;
            HFONT font = CreateFontW( -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            SetBkMode( hdc, TRANSPARENT );
            if (count == 0)
            {
                RECT pill, close_rc, add_rc;
                uwp_tabview_compute_rects( &trc, 1, 0, &pill, &close_rc, &add_rc );
                if (el->text[0])
                {
                    HBRUSH tbr = CreateSolidBrush( RGB( 44, 44, 44 ) );
                    FillRect( hdc, &pill, tbr );
                    DeleteObject( tbr );
                    SetTextColor( hdc, RGB( 255, 255, 255 ) );
                    pill.left += 10;
                    pill.right = max( pill.left, close_rc.left - 2 );
                    DrawTextW( hdc, el->text, -1, &pill, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS );
                    SetTextColor( hdc, RGB( 185, 185, 185 ) );
                    DrawTextW( hdc, L"\x00d7", -1, &close_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                }
                if (add_rc.right > add_rc.left + 12)
                {
                    SetTextColor( hdc, RGB( 210, 210, 210 ) );
                    DrawTextW( hdc, L"+", -1, &add_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                }
            }
            for (i = 0; i < count; i++)
            {
                WCHAR tab_title[128] = {0};
                RECT pill, close_rc, add_rc;
                BOOL is_sel = el->selected_item ? (items[i] == el->selected_item) : (i == 0);
                HBRUSH tbr = CreateSolidBrush( is_sel ? RGB( 44, 44, 44 ) : RGB( 28, 28, 28 ) );
                uwp_tabview_get_item_title( items[i], tab_title, ARRAY_SIZE(tab_title) );
                uwp_tabview_compute_rects( &trc, count, i, &pill, &close_rc, &add_rc );
                FillRect( hdc, &pill, tbr );
                DeleteObject( tbr );
                SetTextColor( hdc, is_sel ? RGB( 255, 255, 255 ) : RGB( 195, 195, 195 ) );
                pill.left += 10;
                pill.right = max( pill.left, close_rc.left - 2 );
                DrawTextW( hdc, tab_title, -1, &pill, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS );
                SetTextColor( hdc, RGB( 185, 185, 185 ) );
                DrawTextW( hdc, L"\x00d7", -1, &close_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                if (i == count - 1 && add_rc.right > add_rc.left + 12)
                {
                    SetTextColor( hdc, RGB( 210, 210, 210 ) );
                    DrawTextW( hdc, L"+", -1, &add_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                }
                IInspectable_Release( items[i] );
            }
            SelectObject( hdc, old_font );
            DeleteObject( font );
            return;
        }
        if (wcsstr( el->class_name, L"PivotItem" ) && el->text[0])
        {
            RECT trc = el->bounds;
            BOOL is_first = (wcsstr( el->name, L"History" ) != NULL);
            int fh = max( 13, (int)(15 * g_layout_scale + 0.5f) );
            HFONT font = CreateFontW( -fh, 0, 0, 0, is_first ? FW_SEMIBOLD : FW_NORMAL,
                                      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF tfg = is_first
                ? uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg )
                : uwp_paint_resolve_color( el, L"TextFillColorSecondaryBrush", RGB( 175, 175, 175 ), surface_bg );
            SetTextColor( hdc, tfg );
            SetBkMode( hdc, TRANSPARENT );
            DrawTextW( hdc, el->text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
            if (is_first)
            {
                RECT bar = { trc.left, trc.bottom - 3, min( trc.right, trc.left + 52 ), trc.bottom };
                COLORREF acc = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                                   uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
                HBRUSH abr = CreateSolidBrush( acc );
                FillRect( hdc, &bar, abr );
                DeleteObject( abr );
            }
            return;
        }
    }

    if (el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON)
    {
        RECT brc = el->bounds;
        WCHAR tmp[64];
        const WCHAR *label = uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) );
        COLORREF bg, fg, bd;
        BOOL draw_bg = TRUE, draw_bd = TRUE;
        HBRUSH br = NULL;
        HPEN pen = NULL;
        HFONT font;
        HGDIOBJ old_br = NULL, old_pen = NULL, old_font;
        int fh;

        /* Skip invisible shortcut-only buttons (CopyButton, PasteButton) */
        if (wcsstr( el->name, L"CopyButton" ) || wcsstr( el->name, L"PasteButton" ))
            return;

        if (wcsstr( el->name, L"NavItem" ))
        {
            RECT rc = el->bounds;
            BOOL selected = (el->is_checked == 1);
            if (selected || el->is_hovered || el->is_pressed)
            {
                COLORREF nav_bg = el->is_pressed ? RGB( 52, 52, 52 ) : (selected ? RGB( 40, 40, 40 ) : RGB( 34, 34, 34 ));
                HBRUSH nbr = CreateSolidBrush( nav_bg );
                HPEN npen = CreatePen( PS_SOLID, 1, nav_bg );
                HGDIOBJ obr = SelectObject( hdc, nbr );
                HGDIOBJ open = SelectObject( hdc, npen );
                RoundRect( hdc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2, 6, 6 );
                SelectObject( hdc, open );
                SelectObject( hdc, obr );
                DeleteObject( npen );
                DeleteObject( nbr );
            }
            if (selected)
            {
                RECT bar = rc;
                COLORREF accent_cr = uwp_paint_resolve_color( el, L"AccentBrush", uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
                HBRUSH abr = CreateSolidBrush( accent_cr );
                bar.right = bar.left + 3;
                bar.top += 4; bar.bottom -= 4;
                FillRect( hdc, &bar, abr );
                DeleteObject( abr );
            }
            if (label[0])
            {
                HFONT nav_font = CreateFontW( -12, 0, 0, 0, selected ? FW_SEMIBOLD : FW_NORMAL,
                                              FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
                HGDIOBJ old_nav_font = SelectObject( hdc, nav_font );
                COLORREF nfg = selected
                    ? uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), surface_bg )
                    : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 180, 180, 180 ), surface_bg );
                SetTextColor( hdc, nfg );
                SetBkMode( hdc, TRANSPARENT );
                DrawTextW( hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
                SelectObject( hdc, old_nav_font );
                DeleteObject( nav_font );
            }
            return;
        }

        InflateRect( &brc, -2, -2 );
        if (brc.right <= brc.left || brc.bottom <= brc.top) return;

        if (el->has_bg)
        {
            if (el->bg_alpha == 0)
            {
                draw_bg = el->is_pressed || el->is_hovered;
                bg = el->is_pressed ? RGB( 55, 55, 55 ) : (el->is_hovered ? RGB( 45, 45, 45 ) : surface_bg);
            }
            else
            {
                bg = blend_argb_over( el->bg_argb, surface_bg );
                if (el->is_pressed)
                    bg = RGB( max( 0, (int)GetRValue(bg) - 20 ), max( 0, (int)GetGValue(bg) - 20 ), max( 0, (int)GetBValue(bg) - 20 ) );
            }
        }
        else if (wcsstr( el->name, L"GetButton" ) || !wcscmp( label, L"Get" ) || !wcscmp( label, L"Free" ) ||
                 !wcscmp( el->name, L"EqualButton" ) || (!wcscmp( label, L"=" ) && label[1] == 0))
        {
            bg = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 25 ), max( 0, (int)GetGValue(bg) - 25 ), max( 0, (int)GetBValue(bg) - 25 ) );
        }
        else if (wcsstr( el->name, L"Num" ) || !wcscmp( el->name, L"DecimalSeparatorButton" ) ||
                 !wcscmp( el->name, L"NegateButton" ))
        {
            bg = uwp_paint_resolve_color( el, L"CalcButtonFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 59, 59, 59 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 14 ), max( 0, (int)GetGValue(bg) - 14 ), max( 0, (int)GetBValue(bg) - 14 ) );
        }
        else
        {
            bg = uwp_paint_resolve_color( el, L"CalcButtonAltFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 50, 50, 50 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 12 ), max( 0, (int)GetGValue(bg) - 12 ), max( 0, (int)GetBValue(bg) - 12 ) );
        }

        if (el->has_border)
        {
            draw_bd = (el->border_alpha > 0);
            bd = blend_argb_over( el->border_argb, surface_bg );
        }
        else if (!draw_bg)
        {
            draw_bd = FALSE;
            bd = surface_bg;
        }
        else
        {
            bd = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 68, 68, 68 ), surface_bg );
        }

        if (!el->is_enabled)
            fg = uwp_paint_resolve_color( el, L"TextFillColorDisabledBrush", RGB( 120, 120, 120 ), bg );
        else if (el->has_fg && el->fg_alpha > 0)
            fg = blend_argb_over( el->fg_argb, bg );
        else
            fg = uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), bg );

        fh = max( (int)(12 * g_layout_scale + 0.5f), min( (int)(24 * g_layout_scale + 0.5f), (int)((brc.bottom - brc.top) * 2 / 5) ) );
        font = CreateFontW( -fh, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );

        old_font = SelectObject( hdc, font );
        if (draw_bg || draw_bd)
        {
            br = draw_bg ? CreateSolidBrush( bg ) : NULL;
            pen = draw_bd ? CreatePen( PS_SOLID, 1, bd ) : NULL;
            old_br = SelectObject( hdc, br ? (HGDIOBJ)br : GetStockObject( NULL_BRUSH ) );
            old_pen = SelectObject( hdc, pen ? (HGDIOBJ)pen : GetStockObject( NULL_PEN ) );
            RoundRect( hdc, brc.left, brc.top, brc.right, brc.bottom, 6, 6 );
            SelectObject( hdc, old_pen );
            SelectObject( hdc, old_br );
            if (pen) DeleteObject( pen );
            if (br) DeleteObject( br );
        }
        SetTextColor( hdc, fg );
        SetBkMode( hdc, TRANSPARENT );
        if (label[0])
            DrawTextW( hdc, label, -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
        SelectObject( hdc, old_font );
        DeleteObject( font );
        return;
    }

    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        if (wcsstr( el->name, L"RatingText" ))
        {
            RECT rc = el->bounds;
            HFONT font = CreateFontW( -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF rfg = (el->has_fg && el->fg_alpha > 0) ? blend_argb_over( el->fg_argb, surface_bg ) : RGB( 255, 215, 0 );
            SetTextColor( hdc, rfg );
            SetBkMode( hdc, TRANSPARENT );
            DrawTextW( hdc, el->text[0] ? el->text : L"\x2605 4.8", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
            return;
        }
        if (wcsstr( el->name, L"CategoryText" ))
        {
            RECT rc = el->bounds;
            HFONT font = CreateFontW( -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF cfg = (el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, surface_bg )
                : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 150, 150, 150 ), surface_bg );
            SetTextColor( hdc, cfg );
            SetBkMode( hdc, TRANSPARENT );
            DrawTextW( hdc, el->text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
            return;
        }
        /* Avoid double-painting CalculationResult if its inner NormalOutput TextBlock is painted */
        if (el->tpl_normal_output && el->content == &el->tpl_normal_output->IInspectable_iface &&
            el->tpl_normal_output->has_bounds && el->tpl_normal_output->visibility == 0)
        {
            if (el->text[0] && wcscmp( el->tpl_normal_output->text, el->text ) != 0)
                lstrcpynW( el->tpl_normal_output->text, el->text, ARRAY_SIZE(el->tpl_normal_output->text) );
            uwp_paint_element( hdc, el->tpl_normal_output );
            return;
        }
        if (el->text[0])
        {
            RECT trc = el->bounds;
            BOOL is_result = uwp_is_display_result_element( el );
            int fh = is_result
                ? max( (int)(20 * g_layout_scale + 0.5f), min( (int)(56 * g_layout_scale + 0.5f), (int)((trc.bottom - trc.top) * 3 / 5) ) )
                : max( (int)(12 * g_layout_scale + 0.5f), min( (int)(22 * g_layout_scale + 0.5f), (int)(el->font_size * g_layout_scale + 0.5f) ) );
            HFONT font = CreateFontW( -fh, 0, 0, 0, is_result ? FW_SEMIBOLD : FW_NORMAL,
                                      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            HGDIOBJ old_font = SelectObject( hdc, font );
            COLORREF tfg = (el->has_fg && el->fg_alpha > 0)
                ? blend_argb_over( el->fg_argb, surface_bg )
                : uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush", RGB( 255, 255, 255 ), surface_bg );
            if (wcsstr( el->class_name, L"MenuBarItem" ) &&
                (el->is_pressed || el->is_hovered || uwp_popup_is_open_for( el )))
            {
                RECT hrc = trc;
                HBRUSH hbr = CreateSolidBrush( RGB( 54, 54, 54 ) );
                InflateRect( &hrc, -2, -3 );
                FillRect( hdc, &hrc, hbr );
                DeleteObject( hbr );
            }
            SetTextColor( hdc, tfg );
            SetBkMode( hdc, TRANSPARENT );
            InflateRect( &trc, -8, 0 );
            DrawTextW( hdc, el->text, -1, &trc,
                       (is_result ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
        }
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_paint_element( hdc, ch );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_paint_element( hdc, ch );
        }
    }
}

void xaml_layout_render( HWND hwnd, HDC hdc, int width, int height, IInspectable *content )
{
    RECT rc;
    HBRUSH bg_brush;
    struct uwp_element *root_el, *bg_scope;
    COLORREF page_bg;
    double eval_w, eval_h;

    if (width <= 0 || height <= 0 || !hdc) return;
    uwp_ensure_app_resources_loaded();
    xaml_get_layout_metrics( hwnd, NULL, &g_layout_scale );
    SetRect( &rc, 0, 0, width, height );
    eval_w = (double)width / g_layout_scale;
    eval_h = (double)height / g_layout_scale;
    if (hwnd && IsWindow( hwnd ))
    {
        HWND top = GetAncestor( hwnd, GA_ROOT );
        RECT top_rc;
        if (top && GetClientRect( top, &top_rc ) && top_rc.right > 0 && top_rc.bottom > 0)
        {
            eval_w = (double)(top_rc.right - top_rc.left) / g_layout_scale;
            eval_h = (double)(top_rc.bottom - top_rc.top) / g_layout_scale;
        }
    }

    root_el = uwp_elem_from_any( content );
    bg_scope = (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
                !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        ? g_navigated_page_el : root_el;

    if (bg_scope && bg_scope->has_bg && bg_scope->bg_alpha > 0)
        page_bg = blend_argb_over( bg_scope->bg_argb, RGB( 31, 31, 31 ) );
    else
        page_bg = uwp_paint_resolve_color( bg_scope, L"ApplicationPageBackgroundThemeBrush",
                                           RGB( 31, 31, 31 ), RGB( 31, 31, 31 ) );
    g_paint_page_bg = page_bg;

    bg_brush = CreateSolidBrush( page_bg );
    FillRect( hdc, &rc, bg_brush );
    DeleteObject( bg_brush );

    if (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
        !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
    {
        /* Frame.content holds an MCG RCW pointer; render the adopted page. */
        uwp_vsm_evaluate_tree( g_navigated_page_el, eval_w, eval_h );
        uwp_arrange_element( g_navigated_page_el, rc );
        uwp_paint_element( hdc, g_navigated_page_el );
        return;
    }
    if (root_el && (root_el->content || root_el->children_coll || root_el->xbf_parsed))
    {
        struct uwp_vec_view *chv = root_el->children_coll ?
            CONTAINING_RECORD( root_el->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
        TRACE( "xaml_layout_render root %s kind %d content %p children %u\n",
               debugstr_w(root_el->class_name), root_el->kind, (void *)root_el->content,
               chv ? chv->count : 0xFFFFFFFFu );
        uwp_vsm_evaluate_tree( root_el, eval_w, eval_h );
        uwp_arrange_element( root_el, rc );
        {
            struct uwp_vec_view *av = root_el->children_coll ?
                CONTAINING_RECORD( root_el->children_coll, struct uwp_vec_view, IInspectable_iface ) : NULL;
            TRACE( "xaml_layout_render after arrange root bounds %ld,%ld-%ld,%ld has_bounds %d\n",
                   root_el->bounds.left, root_el->bounds.top, root_el->bounds.right, root_el->bounds.bottom,
                   root_el->has_bounds );
            if (av && av->count)
            {
                struct uwp_element *c0 = uwp_elem_from_any( av->items[0] );
                if (c0) TRACE( "xaml_layout_render child0 %s bounds %ld,%ld-%ld,%ld vis %d\n",
                               debugstr_w(c0->class_name), c0->bounds.left, c0->bounds.top,
                               c0->bounds.right, c0->bounds.bottom, c0->visibility );
            }
        }
        uwp_paint_element( hdc, root_el );
    }
    else TRACE( "xaml_layout_render: root_el NULL (content %p)\n", (void *)content );
}

