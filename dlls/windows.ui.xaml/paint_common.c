/* WinRT Windows.UI.Xaml Shared Painting Helpers (GPU + GDI Backends)
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
#include "private.h"
#include "uwp_private.h"

UINT32 uwp_cr_to_argb( COLORREF cr )
{
    return 0xff000000u | ((UINT32)GetRValue( cr ) << 16) |
           ((UINT32)GetGValue( cr ) << 8) | (UINT32)GetBValue( cr );
}

/* Standard WinUI NavigationView ControlTemplate part check ("SettingsItem"). */
BOOL uwp_is_nav_settings_item( const struct uwp_element *el )
{
    const struct uwp_element *cur;
    int hop;
    for (cur = el, hop = 0; cur && hop < 4; cur = cur->parent, hop++)
        if (!wcscmp( cur->name, L"SettingsItem" ))
            return TRUE;
    return FALSE;
}

BOOL uwp_is_skeleton_subtree( const struct uwp_element *el, int depth )
{
    if (!el || depth > 6) return FALSE;
    if ((el->class_flags & (UWP_CF_SHIMMER | UWP_CF_SKELETON)) != 0)
        return TRUE;
    if (el->kind == UWP_KIND_GRID || el->kind == UWP_KIND_STACK_PANEL ||
        el->kind == UWP_KIND_BORDER || el->kind == UWP_KIND_CONTENT_CONTROL ||
        el->kind == UWP_KIND_USER_CONTROL)
    {
        BOOL any_vis = FALSE;
        if (el->content)
        {
            struct uwp_element *ch = uwp_elem_from_any( el->content );
            if (ch && ch != el && ch->visibility == 0)
            {
                any_vis = TRUE;
                if (!uwp_is_skeleton_subtree( ch, depth + 1 )) return FALSE;
            }
        }
        if (el->children_coll)
        {
            struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int i;
            for (i = 0; v && i < v->count; i++)
            {
                struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
                if (!ch || ch == el || ch->visibility != 0) continue;
                any_vis = TRUE;
                if (!uwp_is_skeleton_subtree( ch, depth + 1 )) return FALSE;
            }
        }
        return any_vis;
    }
    return FALSE;
}

BOOL uwp_is_zero_layout_anchor( const struct uwp_element *el )
{
    BOOL has_children = FALSE;
    if (!el) return FALSE;
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        has_children = (v && v->count > 0);
    }
    if (has_children || el->content || uwp_has_text( el ) || el->has_bg || el->bg_brush || el->has_border)
        return FALSE;
    if ((el->width > 0.0 && !isnan( el->width )) || (el->height > 0.0 && !isnan( el->height )))
        return FALSE;
    if (el->kind == UWP_KIND_IMAGE || el->kind == UWP_KIND_PROGRESS_RING ||
        el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON ||
        el->kind == UWP_KIND_TEXT_BLOCK || el->kind == UWP_KIND_FRAME ||
        el->kind == UWP_KIND_SCROLL_VIEWER)
        return FALSE;
    if (el->class_flags & (UWP_CF_TEXT_BOX | UWP_CF_AUTO_SUGGEST_BOX | UWP_CF_COMBOBOX |
                           UWP_CF_PROGRESS_BAR | UWP_CF_RECTANGLE | UWP_CF_ELLIPSE |
                           UWP_CF_MEDIA_PLAYER | UWP_CF_NAV_VIEW |
                           UWP_CF_TABVIEW | UWP_CF_ITEMS_REPEATER | UWP_CF_LISTVIEW |
                           UWP_CF_GRIDVIEW | UWP_CF_FONT_ICON | UWP_CF_SYMBOL_ICON))
        return FALSE;
    /* Empty unsized container sitting alongside a TeachingTip acts as a TeachingTip target anchor. */
    if (el->parent && el->parent->children_coll)
    {
        struct uwp_vec_view *pv = CONTAINING_RECORD( el->parent->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; pv && i < pv->count; i++)
        {
            struct uwp_element *sib = uwp_elem_from_any( pv->items[i] );
            if (sib && sib != el && uwp_is_teaching_tip( sib ))
                return TRUE;
        }
    }
    return FALSE;
}

BOOL uwp_paint_is_invisible_shortcut_button( const struct uwp_element *el )
{
    BOOL has_children = FALSE;
    if (!el) return FALSE;
    if (el->opacity <= 0.005) return TRUE;
    if (el->margin_l < -10.0 || el->margin_t < -10.0)
        return TRUE;
    if (!wcscmp( el->name, L"TogglePaneButton" ) || !wcscmp( el->name, L"CloseButton" ))
        return FALSE;
    if (el->class_flags & (UWP_CF_RADIO_BUTTON | UWP_CF_NAV_ITEM | UWP_CF_DROP_DOWN_BUTTON))
        return FALSE;
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        has_children = (v && v->count > 0);
    }
    if (!uwp_has_text( el ) && !el->content && !has_children &&
        !el->has_bg && !el->bg_brush && !el->has_border &&
        el->command != NULL && el->visibility == 0)
        return TRUE;
    return FALSE;
}

BOOL uwp_paint_is_nav_toggle_item( const struct uwp_element *el )
{
    if (!el) return FALSE;
    if (el->class_flags & UWP_CF_NAV_ITEM) return TRUE;
    if (el->parent &&
        (!wcscmp( el->parent->name, L"MenuItemsHost" ) ||
         !wcscmp( el->parent->name, L"FooterMenuItemsHost" ) ||
         uwp_is_nav_view( el->parent )))
        return TRUE;
    return FALSE;
}

BOOL uwp_paint_pivot_item_is_selected( const struct uwp_element *el )
{
    if (!el) return FALSE;
    if (el->is_checked == 1) return TRUE;
    if (el->parent && el->parent->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->parent->children_coll, struct uwp_vec_view, IInspectable_iface );
        INT32 sel_idx = uwp_elem_selected_index( el->parent );
        IInspectable *sel_item = uwp_elem_selected_item( el->parent );
        unsigned int k;
        if (sel_item)
        {
            if (uwp_elem_from_any( sel_item ) == el ||
                sel_item == &el->IInspectable_iface || (el->outer && sel_item == el->outer) ||
                (el->content && sel_item == el->content))
                return TRUE;
            return FALSE;
        }
        if (sel_idx >= 0 && v && (unsigned int)sel_idx < v->count)
            return uwp_elem_from_any( v->items[sel_idx] ) == el;
        for (k = 0; v && k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch->visibility == 0 && uwp_is_pivot_item( ch ))
                return ch == el;
        }
    }
    return FALSE;
}

void uwp_paint_resolve_pivot_item_style( struct uwp_element *el, COLORREF surface_bg,
                                         BOOL *out_selected, int *out_fh,
                                         COLORREF *out_fg, COLORREF *out_accent )
{
    BOOL sel = uwp_paint_pivot_item_is_selected( el );
    if (out_selected) *out_selected = sel;
    if (out_fh) *out_fh = max( 13, (int)(15 * g_layout_scale + 0.5f) );
    if (out_fg)
    {
        *out_fg = sel
            ? uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg )
            : uwp_paint_resolve_color( el, L"TextFillColorSecondaryBrush", RGB( 175, 175, 175 ), surface_bg );
    }
    if (out_accent)
    {
        *out_accent = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                          uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
    }
}

void uwp_paint_resolve_progress_ring_colors( struct uwp_element *el, COLORREF surface_bg,
                                             COLORREF *out_track, COLORREF *out_arc )
{
    if (out_track)
        *out_track = uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush", RGB( 68, 68, 68 ), surface_bg );
    if (out_arc)
    {
        *out_arc = (el->has_fg && el->fg_alpha > 0)
            ? blend_argb_over( el->fg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"AccentBrush",
                  uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
    }
}

void uwp_paint_resolve_teaching_tip_colors( struct uwp_element *el, COLORREF surface_bg,
                                            COLORREF *out_bg, COLORREF *out_bd )
{
    BOOL light = uwp_is_light_theme( el );
    if (out_bg)
    {
        *out_bg = (el->has_bg && el->bg_alpha > 0)
            ? blend_argb_over( el->bg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"SolidBackgroundFillColorTertiaryBrush",
                                       light ? RGB( 249, 249, 249 ) : RGB( 44, 44, 44 ), surface_bg );
    }
    if (out_bd)
        *out_bd = uwp_paint_resolve_color( el, L"SurfaceStrokeColorFlyoutBrush",
                                           light ? RGB( 220, 220, 224 ) : RGB( 82, 82, 82 ), surface_bg );
}

void uwp_paint_resolve_searchbox_style( struct uwp_element *el, COLORREF surface_bg,
                                        BOOL focused, const WCHAR *cur_txt,
                                        COLORREF *out_bg, COLORREF *out_bd, COLORREF *out_fg,
                                        WCHAR *disp, size_t disp_cap )
{
    BOOL light = uwp_is_light_theme( el );
    COLORREF sbg = (el->has_bg && el->bg_alpha > 0)
        ? blend_argb_over( el->bg_argb, surface_bg )
        : uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush",
                                   light ? RGB( 255, 255, 255 ) : RGB( 45, 45, 48 ), surface_bg );
    COLORREF sbd = focused
        ? uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg )
        : uwp_paint_resolve_color( el, L"ControlStrokeColorDefaultBrush",
                                   light ? RGB( 210, 210, 214 ) : RGB( 78, 78, 84 ), surface_bg );
    COLORREF sfg = (cur_txt && cur_txt[0])
        ? uwp_paint_resolve_color( el, L"PrimaryForegroundBrush",
                                   light ? RGB( 26, 26, 26 ) : RGB( 255, 255, 255 ), sbg )
        : ((el->has_fg && el->fg_alpha > 0)
            ? blend_argb_over( el->fg_argb, sbg )
            : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush",
                                       light ? RGB( 96, 96, 102 ) : RGB( 190, 190, 195 ), sbg ));
    const WCHAR *ph = uwp_elem_placeholder( el );
    const WCHAR *fallback = (ph && ph[0]) ? ph : L"\x26b2   Search";

    if (out_bg) *out_bg = sbg;
    if (out_bd) *out_bd = sbd;
    if (out_fg) *out_fg = sfg;
    if (disp && disp_cap > 0)
    {
        if (cur_txt && cur_txt[0])
            swprintf( disp, disp_cap, focused ? L"\x26b2   %s|" : L"\x26b2   %s", cur_txt );
        else if (focused)
            lstrcpynW( disp, L"\x26b2   |", (int)disp_cap );
        else if (ph && ph[0])
            swprintf( disp, disp_cap, L"\x26b2   %s", ph );
        else
            lstrcpynW( disp, fallback, (int)disp_cap );
    }
}

void uwp_paint_resolve_button_style( struct uwp_element *el, COLORREF surface_bg, const WCHAR *label,
                                     BOOL *out_draw_bg, BOOL *out_draw_bd,
                                     COLORREF *out_bg, COLORREF *out_fg, COLORREF *out_bd,
                                     UINT32 *out_bg_argb, UINT32 *out_bd_argb )
{
    UINT32 bg_argb = 0, bd_argb = 0, res_argb = 0;
    BOOL draw_bg = TRUE, draw_bd = TRUE;
    COLORREF bg, fg, bd;
    BOOL is_accent_or_checked = el->is_accent_style || (el->kind == UWP_KIND_TOGGLE_BUTTON && el->is_checked == 1);

    if (uwp_is_hyperlink_button( el ))
    {
        BOOL light = uwp_is_light_theme( el );
        if (el->has_bg && el->bg_alpha > 0)
        {
            bg_argb = apply_opacity_to_argb( el->bg_argb, el->opacity );
            bg = blend_argb_over( bg_argb, surface_bg );
        }
        else if (el->is_pressed || el->is_hovered)
        {
            bg_argb = light ? (el->is_pressed ? 0x16000000u : 0x0c000000u)
                            : (el->is_pressed ? 0x18ffffffu : 0x0effffffu);
            bg = blend_argb_over( bg_argb, surface_bg );
        }
        else
        {
            draw_bg = FALSE;
            bg_argb = 0;
            bg = surface_bg;
        }
        if (el->has_border && el->border_alpha > 0)
        {
            bd_argb = apply_opacity_to_argb( el->border_argb, el->opacity );
            bd = blend_argb_over( bd_argb, surface_bg );
        }
        else
        {
            draw_bd = FALSE;
            bd_argb = 0;
            bd = surface_bg;
        }
        if (!el->is_enabled)
            fg = uwp_paint_resolve_color( el, L"TextFillColorDisabledBrush",
                                          light ? RGB( 150, 150, 150 ) : RGB( 120, 120, 120 ), bg );
        else if (el->has_fg && el->fg_alpha > 0)
            fg = blend_argb_over( el->fg_argb, bg );
        else
            fg = uwp_paint_resolve_color( el, L"HyperlinkButtonForeground",
                                          light ? RGB( 0, 62, 146 ) : RGB( 96, 205, 255 ), bg );
        if (out_draw_bg) *out_draw_bg = draw_bg;
        if (out_draw_bd) *out_draw_bd = draw_bd;
        if (out_bg) *out_bg = bg;
        if (out_fg) *out_fg = fg;
        if (out_bd) *out_bd = bd;
        if (out_bg_argb) *out_bg_argb = bg_argb;
        if (out_bd_argb) *out_bd_argb = bd_argb;
        return;
    }

    if ((!label || !label[0]) && !el->has_bg && !is_accent_or_checked && !el->is_hovered && !el->is_pressed)
    {
        draw_bg = FALSE;
        draw_bd = FALSE;
        bg = surface_bg;
    }
    else if (el->has_bg && !(el->kind == UWP_KIND_TOGGLE_BUTTON && el->is_checked == 1))
    {
        if (el->bg_alpha == 0)
        {
            BOOL light = uwp_is_light_theme( el );
            draw_bg = el->is_pressed || el->is_hovered;
            bg = el->is_pressed
                ? (light ? RGB( 218, 218, 224 ) : RGB( 62, 62, 66 ))
                : (el->is_hovered
                    ? (light ? RGB( 230, 230, 234 ) : RGB( 50, 50, 54 ))
                    : surface_bg);
            bg_argb = uwp_cr_to_argb( bg );
        }
        else
        {
            bg_argb = apply_opacity_to_argb( el->bg_argb, el->opacity );
            bg = blend_argb_over( bg_argb, surface_bg );
            if (el->is_pressed)
            {
                bg = RGB( max( 0, (int)GetRValue(bg) - 20 ), max( 0, (int)GetGValue(bg) - 20 ), max( 0, (int)GetBValue(bg) - 20 ) );
                bg_argb = (bg_argb & 0xff000000u) | ((UINT32)GetRValue(bg) << 16) | ((UINT32)GetGValue(bg) << 8) | GetBValue(bg);
            }
        }
    }
    else if (is_accent_or_checked)
    {
        if (uwp_resolve_brush_or_color( NULL, el, L"AccentButtonBackground", 0, &res_argb ) && ((res_argb >> 24) & 0xff))
            bg_argb = apply_opacity_to_argb( res_argb, el->opacity );
        else if (uwp_resolve_brush_or_color( NULL, el, L"AccentFillColorDefaultBrush", 0, &res_argb ) && ((res_argb >> 24) & 0xff))
            bg_argb = apply_opacity_to_argb( res_argb, el->opacity );
        else if (uwp_resolve_brush_or_color( NULL, el, L"AccentBrush", 0, &res_argb ) && ((res_argb >> 24) & 0xff))
            bg_argb = apply_opacity_to_argb( res_argb, el->opacity );
        else
            bg_argb = 0xff0078d7u;
        bg = blend_argb_over( bg_argb, surface_bg );
        if (el->is_pressed)
        {
            bg = RGB( max( 0, (int)GetRValue(bg) - 25 ), max( 0, (int)GetGValue(bg) - 25 ), max( 0, (int)GetBValue(bg) - 25 ) );
            bg_argb = (bg_argb & 0xff000000u) | ((UINT32)GetRValue(bg) << 16) | ((UINT32)GetGValue(bg) << 8) | GetBValue(bg);
        }
    }
    else
    {
        if (el->resources_rd && uwp_resolve_brush_or_color( NULL, el, L"ButtonBackground", 0, &res_argb ) && ((res_argb >> 24) & 0xff))
            bg_argb = apply_opacity_to_argb( res_argb, el->opacity );
        else if (uwp_resolve_brush_or_color( NULL, el, L"ControlFillColorDefaultBrush", 0, &res_argb ) && ((res_argb >> 24) & 0xff))
            bg_argb = apply_opacity_to_argb( res_argb, el->opacity );
        else
            bg_argb = uwp_is_light_theme( el ) ? 0xffffffffu : 0xff323232u;
        bg = blend_argb_over( bg_argb, surface_bg );
        if (el->is_pressed)
        {
            bg = RGB( max( 0, (int)GetRValue(bg) - 12 ), max( 0, (int)GetGValue(bg) - 12 ), max( 0, (int)GetBValue(bg) - 12 ) );
            bg_argb = (bg_argb & 0xff000000u) | ((UINT32)GetRValue(bg) << 16) | ((UINT32)GetGValue(bg) << 8) | GetBValue(bg);
        }
    }

    if (el->has_border && !(el->kind == UWP_KIND_TOGGLE_BUTTON && el->is_checked == 1))
    {
        draw_bd = (el->border_alpha > 0);
        bd_argb = apply_opacity_to_argb( el->border_argb, el->opacity );
        bd = blend_argb_over( bd_argb, surface_bg );
    }
    else if (!draw_bg || is_accent_or_checked)
    {
        draw_bd = FALSE;
        bd = surface_bg;
    }
    else
    {
        if (uwp_resolve_brush_or_color( NULL, el, L"ControlStrokeColorDefaultBrush", 0, &res_argb ) && ((res_argb >> 24) & 0xff))
            bd_argb = apply_opacity_to_argb( res_argb, el->opacity );
        else
            bd_argb = uwp_is_light_theme( el ) ? 0x1a000000u : 0x33ffffffu;
        bd = blend_argb_over( bd_argb, surface_bg );
    }

    if (!el->is_enabled)
        fg = uwp_paint_resolve_color( el, L"TextFillColorDisabledBrush",
                                      uwp_is_light_theme( el ) ? RGB( 150, 150, 150 ) : RGB( 120, 120, 120 ), bg );
    else if (el->has_fg && el->fg_alpha > 0 && !(el->kind == UWP_KIND_TOGGLE_BUTTON && el->is_checked == 1))
        fg = blend_argb_over( el->fg_argb, bg );
    else if (is_accent_or_checked)
        fg = uwp_paint_resolve_color( el, L"AccentButtonForeground", RGB( 255, 255, 255 ), bg );
    else
        fg = uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush",
                                      uwp_is_light_theme( el ) ? RGB( 26, 26, 26 ) : RGB( 255, 255, 255 ), bg );

    if (out_draw_bg) *out_draw_bg = draw_bg;
    if (out_draw_bd) *out_draw_bd = draw_bd;
    if (out_bg) *out_bg = bg;
    if (out_fg) *out_fg = fg;
    if (out_bd) *out_bd = bd;
    if (out_bg_argb) *out_bg_argb = bg_argb;
    if (out_bd_argb) *out_bd_argb = bd_argb;
}

void uwp_paint_resolve_eval_size( HWND hwnd, int width, int height, double *out_w, double *out_h )
{
    double eval_w = (double)width / g_layout_scale;
    double eval_h = (double)height / g_layout_scale;
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
    if (out_w) *out_w = eval_w;
    if (out_h) *out_h = eval_h;
}

COLORREF uwp_paint_resolve_page_bg( struct uwp_element *root_el, struct uwp_element **out_bg_scope )
{
    struct uwp_element *bg_scope = (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
                                    !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
        ? g_navigated_page_el : root_el;
    COLORREF def_bg = uwp_is_light_theme( bg_scope ) ? RGB( 243, 243, 243 ) : RGB( 31, 31, 31 );
    COLORREF page_bg;
    if (out_bg_scope) *out_bg_scope = bg_scope;
    if (bg_scope && bg_scope->has_bg && bg_scope->bg_alpha > 0)
        page_bg = blend_argb_over( bg_scope->bg_argb, def_bg );
    else
        page_bg = uwp_paint_resolve_color( bg_scope, L"ApplicationPageBackgroundThemeBrush", def_bg, def_bg );
    return page_bg;
}

void uwp_compute_uniform_to_fill_uv( int dst_w, int dst_h, int src_w, int src_h, float uv_crop[4] )
{
    uv_crop[0] = 0.0f;
    uv_crop[1] = 0.0f;
    uv_crop[2] = 1.0f;
    uv_crop[3] = 1.0f;
    if (dst_w > 0 && dst_h > 0 && src_w > 0 && src_h > 0)
    {
        double dst_ar = (double)dst_w / (double)dst_h;
        double src_ar = (double)src_w / (double)src_h;
        if (src_ar > dst_ar)
        {
            float keep = (float)(dst_ar / src_ar);
            uv_crop[0] = 0.5f * (1.0f - keep);
            uv_crop[2] = 1.0f - uv_crop[0];
        }
        else if (src_ar < dst_ar)
        {
            float keep = (float)(src_ar / dst_ar);
            uv_crop[1] = 0.5f * (1.0f - keep);
            uv_crop[3] = 1.0f - uv_crop[1];
        }
    }
}

void uwp_resolve_and_load_image( struct uwp_element *el )
{
    IInspectable *src_obj;
    struct uwp_media_ext *m;
    if (!el) return;
    src_obj = (el->media && el->media->img_source) ? el->media->img_source : el->content;
    if (!uwp_elem_image_src( el )[0] && src_obj)
    {
        struct uwp_element *src_el = uwp_elem_from_any( src_obj );
        const WCHAR *src_s = uwp_elem_image_src( src_el );
        if (src_s[0])
        {
            m = uwp_elem_ensure_media( el );
            if (m)
            {
                lstrcpynW( m->image_src, src_s, ARRAY_SIZE(m->image_src) );
                m->img_loaded = FALSE;
                InterlockedExchange( &m->img_loading, 0 );
            }
        }
        else if (src_el && src_el->dp_cnt > 0 && src_el->dp_store)
        {
            int di;
            for (di = 0; di < src_el->dp_cnt && !uwp_elem_image_src( el )[0]; di++)
            {
                IInspectable *dv = src_el->dp_store[di].value;
                WCHAR tmp[512] = {0};
                if (!dv) continue;
                if (!uwp_unbox_string( dv, tmp, ARRAY_SIZE(tmp) ))
                    uwp_extract_uri_string( dv, tmp, ARRAY_SIZE(tmp) );
                if (tmp[0])
                {
                    m = uwp_elem_ensure_media( el );
                    if (m)
                    {
                        lstrcpynW( m->image_src, tmp, ARRAY_SIZE(m->image_src) );
                        m->img_loaded = FALSE;
                        InterlockedExchange( &m->img_loading, 0 );
                    }
                }
            }
        }
        else
        {
            WCHAR tmp[512] = {0};
            if (!uwp_unbox_string( src_obj, tmp, ARRAY_SIZE(tmp) ))
                uwp_extract_uri_string( src_obj, tmp, ARRAY_SIZE(tmp) );
            if (tmp[0])
            {
                m = uwp_elem_ensure_media( el );
                if (m)
                {
                    lstrcpynW( m->image_src, tmp, ARRAY_SIZE(m->image_src) );
                    m->img_loaded = FALSE;
                    InterlockedExchange( &m->img_loading, 0 );
                }
            }
        }
    }
    if (!uwp_elem_image_src( el )[0] && !(el->probe_flags & UWP_PF_NO_IMG))
    {
        static const WCHAR *const img_props[] =
            { L"Uri", L"Url", L"ImageUrl", L"ImageUri", L"Source", L"IconUrl", L"IconUri",
              L"ThumbnailUrl", L"Images", L"Image", L"Icon" };
        struct uwp_element *p;
        int hop;
        for (p = el->parent, hop = 0; p && hop < 14 && !uwp_elem_image_src( el )[0]; p = p->parent, hop++)
        {
            int di;
            if (p->kind == UWP_KIND_PAGE || p->kind == UWP_KIND_FRAME) break;
            for (di = 0; di < p->dp_cnt && p->dp_store && !uwp_elem_image_src( el )[0]; di++)
            {
                IInspectable *dv = p->dp_store[di].value;
                struct uwp_element *bel;
                WCHAR tmp[512] = {0};
                if (!dv) continue;
                bel = uwp_elem_from_any( dv );
                if (uwp_elem_image_src( bel )[0])
                {
                    m = uwp_elem_ensure_media( el );
                    if (m) lstrcpynW( m->image_src, uwp_elem_image_src( bel ), ARRAY_SIZE(m->image_src) );
                }
                else
                {
                    if (!uwp_unbox_string( dv, tmp, ARRAY_SIZE(tmp) ))
                        uwp_extract_uri_string( dv, tmp, ARRAY_SIZE(tmp) );
                    if (!tmp[0])
                        uwp_vm_query_string_prop_depth( dv, img_props, ARRAY_SIZE(img_props), tmp, ARRAY_SIZE(tmp), 2 );
                    if (tmp[0] && (wcsstr( tmp, L"http://" ) || wcsstr( tmp, L"https://" ) || wcsstr( tmp, L"ms-appx://" )))
                    {
                        m = uwp_elem_ensure_media( el );
                        if (m) lstrcpynW( m->image_src, tmp, ARRAY_SIZE(m->image_src) );
                    }
                }
            }
            if (!uwp_elem_image_src( el )[0] && p->data_context)
            {
                WCHAR img_buf[512] = {0};
                if (uwp_vm_query_string_prop_depth( p->data_context, img_props, ARRAY_SIZE(img_props), img_buf, ARRAY_SIZE(img_buf), 2 ) &&
                    img_buf[0])
                {
                    m = uwp_elem_ensure_media( el );
                    if (m) lstrcpynW( m->image_src, img_buf, ARRAY_SIZE(m->image_src) );
                }
            }
        }
        if (!uwp_elem_image_src( el )[0])
            el->probe_flags |= UWP_PF_NO_IMG;
    }
    uwp_load_image_asset( el );
}

void uwp_paint_first_subtree_text( const struct uwp_element *el, WCHAR *buf, size_t cap, int depth )
{
    if (!el || !buf || !cap || buf[0] || depth > 8 || el->visibility != 0) return;
    if (!uwp_is_font_icon( el ) && uwp_has_text( el ))
    {
        const WCHAR *t = uwp_elem_text( el );
        if (t[0] && !(t[0] >= 0xe000 && t[0] <= 0xf8ff))
        {
            lstrcpynW( buf, t, (int)cap );
            return;
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_paint_first_subtree_text( ch, buf, cap, depth + 1 );
    }
    if (!buf[0] && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count && !buf[0]; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_paint_first_subtree_text( ch, buf, cap, depth + 1 );
        }
    }
}

struct uwp_element *uwp_paint_find_inner_search_textbox( struct uwp_element *el, int depth )
{
    if (!el || depth > 6 || el->visibility != 0) return NULL;
    if (el->content)
    {
        struct uwp_element *hit = uwp_paint_find_inner_search_textbox( uwp_elem_from_any( el->content ), depth + 1 );
        if (hit) return hit;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; v && i < v->count; i++)
        {
            struct uwp_element *hit = uwp_paint_find_inner_search_textbox( uwp_elem_from_any( v->items[i] ), depth + 1 );
            if (hit) return hit;
        }
    }
    if (depth > 0 && (uwp_is_exact_autosuggest( el ) || uwp_is_textbox( el )))
        return el;
    return NULL;
}

BOOL uwp_paint_btn_child_has_text( const struct uwp_element *el, int depth )
{
    if (!el || depth > 8 || el->visibility != 0) return FALSE;
    if (uwp_has_text( el ) || el->inlines_coll) return TRUE;
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_paint_btn_child_has_text( ch, depth + 1 )) return TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int i;
        for (i = 0; v && i < v->count; i++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[i] );
            if (ch && ch != el && uwp_paint_btn_child_has_text( ch, depth + 1 )) return TRUE;
        }
    }
    return FALSE;
}

BOOL uwp_paint_nav_tree_has_checked_item( const struct uwp_element *el, int depth )
{
    if (!el || depth > 5) return FALSE;
    if (uwp_is_nav_item( el ) && el->is_checked == 1)
        return TRUE;
    if (el->children_coll)
    {
        struct uwp_vec_view *v = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < v->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
            if (ch && ch != el && !uwp_is_nav_view( ch ) && uwp_paint_nav_tree_has_checked_item( ch, depth + 1 ))
                return TRUE;
        }
    }
    return FALSE;
}

BOOL uwp_paint_nav_is_first_selectable_item( const struct uwp_element *el )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!el || !el->parent || wcscmp( el->parent->name, L"MenuItemsHost" ) || !el->parent->children_coll)
        return FALSE;
    v = CONTAINING_RECORD( el->parent->children_coll, struct uwp_vec_view, IInspectable_iface );
    for (k = 0; k < v->count; k++)
    {
        struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
        if (!ch || ch->visibility != 0 || uwp_is_nav_header( ch ) || uwp_is_nav_separator( ch ))
            continue;
        return ch == el;
    }
    return FALSE;
}

BOOL uwp_paint_is_clr_type_name( const WCHAR *s )
{
    const WCHAR *p;
    BOOL has_dot = FALSE;

    if (!s || !s[0]) return FALSE;
    if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_'))
        return FALSE;

    for (p = s; *p; p++)
    {
        WCHAR c = *p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            return FALSE;
        if (c == '.')
        {
            WCHAR prev = p[-1], next = p[1];
            BOOL prev_ok = (prev >= 'A' && prev <= 'Z') || (prev >= 'a' && prev <= 'z') ||
                           (prev >= '0' && prev <= '9') || prev == '_';
            BOOL next_ok = (next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z') ||
                           next == '_';
            if (!prev_ok || !next_ok) return FALSE;
            has_dot = TRUE;
        }
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '`' ||
                   c == '[' || c == ']' || c == ','))
        {
            return FALSE;
        }
    }
    return has_dot;
}

IInspectable *uwp_paint_vm_unwrap_one( IInspectable *vm, const WCHAR *const *props, unsigned int count )
{
    HSTRING hcls = NULL;
    void *cpp = NULL;
    IInspectable *nv = NULL;
    unsigned int ni;

    if (!vm) return NULL;
    if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
    {
        const WCHAR *cls = WindowsGetStringRawBuffer( hcls, NULL );
        for (ni = 0; ni < count && !nv; ni++)
        {
            if (cls && cls[0] && uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, props[ni], vm, &nv ) && nv)
                break;
        }
        WindowsDeleteString( hcls );
    }
    if (!nv && SUCCEEDED(IInspectable_QueryInterface( vm, &IID_ICustomPropertyProvider_UWP, &cpp )) && cpp)
    {
        const struct abi_custom_property_provider *acpp = (const struct abi_custom_property_provider *)cpp;
        if (acpp->lpVtbl->get_Type)
        {
            TypeName tn = {0};
            if (SUCCEEDED(acpp->lpVtbl->get_Type( cpp, &tn )) && tn.Name)
            {
                WCHAR tcls[256] = {0}, *comma;
                const WCHAR *raw = WindowsGetStringRawBuffer( tn.Name, NULL );
                if (raw) lstrcpynW( tcls, raw, ARRAY_SIZE(tcls) );
                comma = wcschr( tcls, ',' );
                if (comma) *comma = 0;
                for (ni = 0; ni < count && !nv; ni++)
                {
                    if (tcls[0] && uwp_get_custom_xaml_member( uwp_get_app_outer(), tcls, props[ni], vm, &nv ) && nv)
                        break;
                }
                WindowsDeleteString( tn.Name );
            }
        }
        for (ni = 0; ni < count && !nv; ni++)
        {
            HSTRING hp = NULL;
            void *cp = NULL;
            if (acpp->lpVtbl->GetCustomProperty && SUCCEEDED(WindowsCreateString( props[ni], wcslen(props[ni]), &hp )) && hp)
            {
                if (SUCCEEDED(acpp->lpVtbl->GetCustomProperty( cpp, hp, &cp )) && cp)
                {
                    const struct abi_custom_property *acp = (const struct abi_custom_property *)cp;
                    if (acp->lpVtbl->GetValue) acp->lpVtbl->GetValue( cp, vm, &nv );
                    IUnknown_Release( (IUnknown *)cp );
                }
                WindowsDeleteString( hp );
            }
        }
        IUnknown_Release( (IUnknown *)cpp );
    }
    return nv;
}
