/* WinRT Windows.UI.Xaml Legacy GDI Software Fallback Renderer
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
#include "private.h"
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

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
    if (!el || !el->has_bounds || el->visibility != 0 || el->opacity <= 0.005) return;
    TRACE( "uwp_paint %s (%s) kind %d bounds %ld,%ld-%ld,%ld\n",
           debugstr_w(el->class_name), debugstr_w(el->name), el->kind,
           el->bounds.left, el->bounds.top, el->bounds.right, el->bounds.bottom );

    if (el->kind == UWP_KIND_IMAGE)
    {
        uwp_load_image_asset( el );
        if (el->media && el->media->img_pixels && el->media->img_w > 0 && el->media->img_h > 0)
        {
            struct uwp_media_ext *m = el->media;
            RECT rc = el->bounds;
            int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
            int dw = (int)m->img_w, dh = (int)m->img_h;
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
            bmi.bmiHeader.biWidth = (LONG)m->img_w;
            bmi.bmiHeader.biHeight = -(LONG)m->img_h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            hbm = CreateDIBSection( memdc, &bmi, DIB_RGB_COLORS, &dib_bits, NULL, 0 );
            if (memdc && hbm && dib_bits)
            {
                HGDIOBJ old_bm = SelectObject( memdc, hbm );
                BOOL ok;
                memcpy( dib_bits, m->img_pixels, (size_t)m->img_w * m->img_h * 4 );
                if (dw == (int)m->img_w && dh == (int)m->img_h)
                    ok = BitBlt( hdc, dx, dy, dw, dh, memdc, 0, 0, SRCCOPY );
                else
                {
                    SetStretchBltMode( hdc, HALFTONE );
                    ok = StretchBlt( hdc, dx, dy, dw, dh, memdc, 0, 0, m->img_w, m->img_h, SRCCOPY );
                }
                TRACE( "uwp_paint IMAGE (%dx%d -> %d,%d %dx%d) ok=%d center_px=%06lx\n",
                       m->img_w, m->img_h, dx, dy, dw, dh, ok, GetPixel( hdc, dx + dw / 2, dy + dh / 2 ) );
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
        COLORREF track_cr, arc_cr;
        HPEN track_pen, arc_pen;
        HBRUSH null_br = GetStockObject( NULL_BRUSH );
        HGDIOBJ old_pen, old_br;
        BOOL eok, aok;
        uwp_paint_resolve_progress_ring_colors( el, surface_bg, &track_cr, &arc_cr );
        track_pen = CreatePen( PS_SOLID, 3, track_cr );
        arc_pen = CreatePen( PS_SOLID, 3, arc_cr );
        old_pen = SelectObject( hdc, track_pen );
        old_br = SelectObject( hdc, null_br );
        eok = Ellipse( hdc, cx - r, cy - r, cx + r, cy + r );
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

    if (el->class_flags & UWP_CF_APP_TITLE_BAR)
    {
        if (uwp_has_text( el ))
        {
            RECT trc = { el->bounds.left + 12, el->bounds.top, el->bounds.left + 200, el->bounds.bottom };
            uwp_paint_text( hdc, &trc, uwp_elem_text( el ), 13, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                            uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg ) );
        }
    }
    else if (uwp_is_text_input( el ))
    {
        RECT rc = el->bounds;
        struct uwp_element *inner_tb = uwp_paint_find_inner_search_textbox( el, 0 );
        const WCHAR *txt = uwp_has_text( el ) ? uwp_elem_text( el ) : uwp_elem_text( inner_tb );
        BOOL focused = (g_focused_textbox == el) || (inner_tb && g_focused_textbox == inner_tb) ||
                       uwp_elem_is_descendant( el, g_focused_textbox );
        COLORREF sbg, sbd, sfg;
        HBRUSH br;
        HPEN pen;
        HFONT font;
        HGDIOBJ old_br, old_pen, old_font;
        WCHAR disp[264];
        uwp_paint_resolve_searchbox_style( el, surface_bg, focused, txt, &sbg, &sbd, &sfg, disp, ARRAY_SIZE(disp) );
        br = CreateSolidBrush( sbg );
        pen = CreatePen( PS_SOLID, focused ? 2 : 1, sbd );
        font = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
        old_br = SelectObject( hdc, br );
        old_pen = SelectObject( hdc, pen );
        old_font = SelectObject( hdc, font );
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
    else if (uwp_is_teaching_tip( el ) && el->is_open)
    {
        RECT rc = el->bounds;
        COLORREF tbg, tbd;
        HBRUSH br;
        HPEN pen;
        HGDIOBJ old_br, old_pen;
        uwp_paint_resolve_teaching_tip_colors( el, surface_bg, &tbg, &tbd );
        br = CreateSolidBrush( tbg );
        pen = CreatePen( PS_SOLID, 1, tbd );
        old_br = SelectObject( hdc, br );
        old_pen = SelectObject( hdc, pen );
        RoundRect( hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8 );
        SelectObject( hdc, old_pen );
        SelectObject( hdc, old_br );
        DeleteObject( pen );
        DeleteObject( br );
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
        if (uwp_is_tabview( el ))
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
                if (uwp_has_text( el ))
                {
                    HBRUSH tbr = CreateSolidBrush( RGB( 44, 44, 44 ) );
                    FillRect( hdc, &pill, tbr );
                    DeleteObject( tbr );
                    SetTextColor( hdc, RGB( 255, 255, 255 ) );
                    pill.left += 10;
                    pill.right = max( pill.left, close_rc.left - 2 );
                    DrawTextW( hdc, uwp_elem_text( el ), -1, &pill, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS );
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
                IInspectable *sel = uwp_elem_selected_item( el );
                BOOL is_sel = sel ? (items[i] == sel) : (i == 0);
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
        if (uwp_is_pivot_item( el ) && uwp_has_text( el ))
        {
            RECT trc = el->bounds;
            BOOL is_first = FALSE;
            int fh = 13;
            COLORREF tfg, acc;
            HFONT font;
            HGDIOBJ old_font;
            uwp_paint_resolve_pivot_item_style( el, surface_bg, &is_first, &fh, &tfg, &acc );
            font = CreateFontW( -fh, 0, 0, 0, is_first ? FW_SEMIBOLD : FW_NORMAL,
                                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI" );
            old_font = SelectObject( hdc, font );
            SetTextColor( hdc, tfg );
            SetBkMode( hdc, TRANSPARENT );
            DrawTextW( hdc, uwp_elem_text( el ), -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
            SelectObject( hdc, old_font );
            DeleteObject( font );
            if (is_first)
            {
                RECT bar = { trc.left, trc.bottom - 3, min( trc.right, trc.left + 52 ), trc.bottom };
                HBRUSH abr = CreateSolidBrush( acc );
                FillRect( hdc, &bar, abr );
                DeleteObject( abr );
                if (el->content)
                {
                    struct uwp_element *body = uwp_elem_from_any( el->content );
                    if (body && body != el)
                        uwp_paint_element( hdc, body );
                }
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

        if (uwp_paint_is_invisible_shortcut_button( el ))
            return;

        if (uwp_paint_is_nav_toggle_item( el ))
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

        uwp_paint_resolve_button_style( el, surface_bg, label, &draw_bg, &draw_bd, &bg, &fg, &bd, NULL, NULL );

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
        /* Avoid double-painting CalculationResult if its inner NormalOutput TextBlock is painted */
        if (el->tpl_normal_output && el->content == &el->tpl_normal_output->IInspectable_iface &&
            el->tpl_normal_output->has_bounds && el->tpl_normal_output->visibility == 0)
        {
            if (uwp_has_text( el ) && wcscmp( uwp_elem_text( el->tpl_normal_output ), uwp_elem_text( el ) ) != 0)
                uwp_elem_set_text( el->tpl_normal_output, uwp_elem_text( el ) );
            uwp_paint_element( hdc, el->tpl_normal_output );
            return;
        }
        if (uwp_has_text( el ))
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
            if (uwp_is_menubar_item( el ) &&
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
            DrawTextW( hdc, uwp_elem_text( el ), -1, &trc,
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
    if (uwp_nav_is_overlay_open( el ) && el->nav && el->nav->nav_pane_bounds.right > el->nav->nav_pane_bounds.left)
    {
        BOOL light = uwp_is_light_theme( el );
        HBRUSH pbr = CreateSolidBrush( light ? RGB( 243, 243, 243 ) : RGB( 39, 39, 43 ) );
        HPEN ppen = CreatePen( PS_SOLID, 1, light ? RGB( 212, 212, 212 ) : RGB( 68, 68, 74 ) );
        HPEN op = SelectObject( hdc, ppen );
        FillRect( hdc, &el->nav->nav_pane_bounds, pbr );
        MoveToEx( hdc, el->nav->nav_pane_bounds.right - 1, el->nav->nav_pane_bounds.top, NULL );
        LineTo( hdc, el->nav->nav_pane_bounds.right - 1, el->nav->nav_pane_bounds.bottom );
        SelectObject( hdc, op );
        DeleteObject( ppen );
        DeleteObject( pbr );
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

static void uwp_gdi_paint_combobox_popup( HDC hdc, int width, int height )
{
    struct uwp_element *cb = g_open_combobox;
    IInspectable *items[256];
    int count, i, item_h = 32, vis_cnt = 0, pad_y = 4;
    RECT prc;
    BOOL light;
    HBRUSH bg_br, sel_br, hov_br;
    HPEN bd_pen;

    if (!cb || !cb->is_dropdown_open || !cb->has_bounds || cb->visibility != 0) return;
    count = uwp_selector_get_items( cb, items, ARRAY_SIZE(items) );
    if (count <= 0) return;
    if (!uwp_combobox_compute_popup_rect( cb, count, width, height, &prc, &item_h, &vis_cnt, &pad_y ))
    {
        for (i = 0; i < count; i++) IInspectable_Release( items[i] );
        return;
    }

    light = uwp_is_light_theme( cb );
    bg_br = CreateSolidBrush( light ? RGB( 249, 249, 249 ) : RGB( 44, 44, 48 ) );
    sel_br = CreateSolidBrush( light ? RGB( 232, 232, 236 ) : RGB( 58, 58, 64 ) );
    hov_br = CreateSolidBrush( light ? RGB( 238, 238, 242 ) : RGB( 54, 54, 60 ) );
    bd_pen = CreatePen( PS_SOLID, 1, light ? RGB( 205, 205, 210 ) : RGB( 82, 82, 88 ) );
    {
        HBRUSH ob = SelectObject( hdc, bg_br );
        HPEN op = SelectObject( hdc, bd_pen );
        RoundRect( hdc, prc.left, prc.top, prc.right, prc.bottom, 8, 8 );
        SelectObject( hdc, ob );
        SelectObject( hdc, op );
    }
    for (i = 0; i < vis_cnt && (cb->dropdown_scroll_offset + i) < count; i++)
    {
        int idx = cb->dropdown_scroll_offset + i;
        RECT rrc, trc;
        WCHAR label[128] = {0};
        IInspectable *sel = uwp_elem_selected_item( cb );
        BOOL is_sel = (idx == uwp_elem_selected_index( cb )) || (sel && items[idx] == sel);
        BOOL is_hov = (idx == cb->hovered_dropdown_idx);
        SetRect( &rrc, prc.left + 4, prc.top + pad_y + i * item_h, prc.right - 4, prc.top + pad_y + (i + 1) * item_h );
        if (is_sel || is_hov)
            FillRect( hdc, &rrc, is_sel ? sel_br : hov_br );
        uwp_combobox_get_item_label( cb, items[idx], label, ARRAY_SIZE(label) );
        SetRect( &trc, rrc.left + 12, rrc.top, rrc.right - 8, rrc.bottom );
        uwp_paint_text( hdc, &trc, label, max( 12, (int)(13 * g_layout_scale) ),
                        is_sel ? FW_SEMIBOLD : FW_NORMAL,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                        light ? RGB( 26, 26, 26 ) : RGB( 245, 245, 245 ) );
    }
    DeleteObject( bg_br );
    DeleteObject( sel_br );
    DeleteObject( hov_br );
    DeleteObject( bd_pen );
    for (i = 0; i < count; i++) IInspectable_Release( items[i] );
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
    uwp_paint_resolve_eval_size( hwnd, width, height, &eval_w, &eval_h );
    root_el = uwp_elem_from_any( content );
    page_bg = uwp_paint_resolve_page_bg( root_el, &bg_scope );
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
        uwp_gdi_paint_combobox_popup( hdc, width, height );
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
        uwp_gdi_paint_combobox_popup( hdc, width, height );
    }
    else TRACE( "xaml_layout_render: root_el NULL (content %p)\n", (void *)content );
}
