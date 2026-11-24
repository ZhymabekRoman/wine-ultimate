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

    if (wcsstr( el->class_name, L"AppTitleBar" ))
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
        else if (el->is_accent_style)
        {
            bg = uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                     uwp_paint_resolve_color( el, L"AccentBrush", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
            if (el->is_pressed)
                bg = RGB( max( 0, (int)GetRValue(bg) - 25 ), max( 0, (int)GetGValue(bg) - 25 ), max( 0, (int)GetBValue(bg) - 25 ) );
        }
        else
        {
            bg = uwp_paint_resolve_color( el, L"ControlFillColorDefaultBrush", RGB( 50, 50, 50 ), surface_bg );
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
    if (uwp_nav_is_overlay_open( el ) && el->nav_pane_bounds.right > el->nav_pane_bounds.left)
    {
        BOOL light = uwp_is_light_theme( el );
        HBRUSH pbr = CreateSolidBrush( light ? RGB( 243, 243, 243 ) : RGB( 39, 39, 43 ) );
        HPEN ppen = CreatePen( PS_SOLID, 1, light ? RGB( 212, 212, 212 ) : RGB( 68, 68, 74 ) );
        HPEN op = SelectObject( hdc, ppen );
        FillRect( hdc, &el->nav_pane_bounds, pbr );
        MoveToEx( hdc, el->nav_pane_bounds.right - 1, el->nav_pane_bounds.top, NULL );
        LineTo( hdc, el->nav_pane_bounds.right - 1, el->nav_pane_bounds.bottom );
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
        BOOL is_sel = (idx == cb->selected_index) || (cb->selected_item && items[idx] == cb->selected_item);
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

    {
        COLORREF def_bg = uwp_is_light_theme( bg_scope ) ? RGB( 243, 243, 243 ) : RGB( 31, 31, 31 );
        if (bg_scope && bg_scope->has_bg && bg_scope->bg_alpha > 0)
            page_bg = blend_argb_over( bg_scope->bg_argb, def_bg );
        else
            page_bg = uwp_paint_resolve_color( bg_scope, L"ApplicationPageBackgroundThemeBrush",
                                               def_bg, def_bg );
    }
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
