/* WinRT Windows.UI.Xaml GPU Visual Tree Renderer
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

static inline UINT32 cr_to_argb( COLORREF cr )
{
    return uwp_cr_to_argb( cr );
}

static struct uwp_element *g_paint_ancestors[48];
static int g_paint_depth = 0;

static void uwp_gpu_paint_element( ID3D11DeviceContext *ctx, struct uwp_element *el );

static BOOL uwp_gpu_paint_brush_fill( ID3D11DeviceContext *ctx, struct uwp_element *el,
                                      IInspectable *brush, const RECT *rc, float rad, float opacity )
{
    struct uwp_element *b_el;
    int bw, bh;

    if (!brush || !rc) return FALSE;
    bw = rc->right - rc->left;
    bh = rc->bottom - rc->top;
    if (bw <= 0 || bh <= 0) return FALSE;

    uwp_xcbb_ensure_connected( brush );
    b_el = uwp_elem_from_any( brush );
    if (!b_el) return FALSE;

    /* 1. LinearGradientBrush */
    if ((b_el->style_anim && b_el->style_anim->gradient_stops_coll) || (b_el->class_flags2 & UWP_CF2_LINEAR_GRADIENT))
    {
        struct uwp_vec_view *gv = NULL;
        struct { double off; UINT32 argb; } stops[16];
        int scnt = 0, i, j;
        float eff_op = (float)(opacity * uwp_elem_brush_opacity( b_el ));
        float g_sx = b_el->style_anim ? b_el->style_anim->grad_start_x : 0.0f;
        float g_sy = b_el->style_anim ? b_el->style_anim->grad_start_y : 0.0f;
        float g_ex = b_el->style_anim ? b_el->style_anim->grad_end_x : 1.0f;
        float g_ey = b_el->style_anim ? b_el->style_anim->grad_end_y : 1.0f;

        if (b_el->style_anim && b_el->style_anim->gradient_stops_coll)
        {
            struct uwp_element *gsc_el = uwp_elem_from_any( b_el->style_anim->gradient_stops_coll );
            if (gsc_el)
            {
                if (gsc_el->children_coll)
                    gv = CONTAINING_RECORD( gsc_el->children_coll, struct uwp_vec_view, IInspectable_iface );
            }
            else
            {
                gv = CONTAINING_RECORD( b_el->style_anim->gradient_stops_coll, struct uwp_vec_view, IInspectable_iface );
            }
        }
        else if (b_el->children_coll)
        {
            gv = CONTAINING_RECORD( b_el->children_coll, struct uwp_vec_view, IInspectable_iface );
        }

        if (gv)
        {
            unsigned int k;
            for (k = 0; k < gv->count && scnt < (int)ARRAY_SIZE(stops); k++)
            {
                struct uwp_element *gs = uwp_elem_from_any( gv->items[k] );
                if (!gs) continue;
                stops[scnt].off = gs->style_anim ? gs->style_anim->grad_stop_offset : 0.0;
                stops[scnt].argb = uwp_elem_has_brush_argb( gs ) ? uwp_elem_brush_argb( gs ) : 0;
                scnt++;
            }
        }
        if (scnt <= 0) return FALSE;
        /* Sort stops by offset */
        for (i = 0; i < scnt - 1; i++)
        {
            for (j = i + 1; j < scnt; j++)
            {
                if (stops[j].off < stops[i].off)
                {
                    double to = stops[i].off; UINT32 tc = stops[i].argb;
                    stops[i].off = stops[j].off; stops[i].argb = stops[j].argb;
                    stops[j].off = to; stops[j].argb = tc;
                }
            }
        }
        if (scnt == 1)
        {
            xaml_gpu_push_rect( (float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom, NULL,
                                rad, apply_opacity_to_argb( stops[0].argb, eff_op ), 0, 0.0f, 1.0f );
            return TRUE;
        }
        if (scnt == 2)
        {
            xaml_gpu_push_gradient_rect( (float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom, NULL,
                                         rad, stops[0].argb, stops[1].argb,
                                         g_sx, g_sy, g_ex, g_ey,
                                         (float)stops[0].off, (float)stops[1].off, eff_op );
            return TRUE;
        }
        /* Multi-stop vertical or horizontal gradient bands */
        {
            float dx = g_ex - g_sx;
            float dy = g_ey - g_sy;
            if (dx > -0.05f && dx < 0.05f && (dy > 0.05f || dy < -0.05f))
            {
                float y_start = (float)rc->top + g_sy * (float)bh;
                float y_span = dy * (float)bh;
                for (i = 0; i < scnt - 1; i++)
                {
                    float y0 = y_start + (float)stops[i].off * y_span;
                    float y1 = y_start + (float)stops[i + 1].off * y_span;
                    if (i == 0 && y0 > (float)rc->top)
                        xaml_gpu_push_rect( (float)rc->left, (float)rc->top, (float)rc->right, y0, rc,
                                            0.0f, apply_opacity_to_argb( stops[0].argb, eff_op ), 0, 0.0f, 1.0f );
                    if (y1 > y0)
                        xaml_gpu_push_gradient_rect( (float)rc->left, y0, (float)rc->right, y1, rc,
                                                     0.0f, stops[i].argb, stops[i + 1].argb,
                                                     0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, eff_op );
                    else if (y0 > y1)
                        xaml_gpu_push_gradient_rect( (float)rc->left, y1, (float)rc->right, y0, rc,
                                                     0.0f, stops[i + 1].argb, stops[i].argb,
                                                     0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, eff_op );
                    if (i == scnt - 2 && y1 < (float)rc->bottom)
                        xaml_gpu_push_rect( (float)rc->left, y1, (float)rc->right, (float)rc->bottom, rc,
                                            0.0f, apply_opacity_to_argb( stops[scnt - 1].argb, eff_op ), 0, 0.0f, 1.0f );
                }
                return TRUE;
            }
            if (dy > -0.05f && dy < 0.05f && (dx > 0.05f || dx < -0.05f))
            {
                float x_start = (float)rc->left + g_sx * (float)bw;
                float x_span = dx * (float)bw;
                for (i = 0; i < scnt - 1; i++)
                {
                    float x0 = x_start + (float)stops[i].off * x_span;
                    float x1 = x_start + (float)stops[i + 1].off * x_span;
                    if (i == 0 && x0 > (float)rc->left)
                        xaml_gpu_push_rect( (float)rc->left, (float)rc->top, x0, (float)rc->bottom, rc,
                                            0.0f, apply_opacity_to_argb( stops[0].argb, eff_op ), 0, 0.0f, 1.0f );
                    if (x1 > x0)
                        xaml_gpu_push_gradient_rect( x0, (float)rc->top, x1, (float)rc->bottom, rc,
                                                     0.0f, stops[i].argb, stops[i + 1].argb,
                                                     0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 1.0f, eff_op );
                    if (i == scnt - 2 && x1 < (float)rc->right)
                        xaml_gpu_push_rect( x1, (float)rc->top, (float)rc->right, (float)rc->bottom, rc,
                                            0.0f, apply_opacity_to_argb( stops[scnt - 1].argb, eff_op ), 0, 0.0f, 1.0f );
                }
                return TRUE;
            }
            xaml_gpu_push_gradient_rect( (float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom, NULL,
                                         rad, stops[0].argb, stops[scnt - 1].argb,
                                         g_sx, g_sy, g_ex, g_ey,
                                         (float)stops[0].off, (float)stops[scnt - 1].off, eff_op );
            return TRUE;
        }
    }

    /* 2. ImageBrush (or CompositionSurfaceBrush) */
    {
        WCHAR uri[512] = {0};
        if (b_el->media && b_el->media->img_source)
        {
            struct uwp_element *src_el = uwp_elem_from_any( b_el->media->img_source );
            const WCHAR *src_s = uwp_elem_image_src( src_el );
            if (src_s[0])
                lstrcpynW( uri, src_s, ARRAY_SIZE(uri) );
            else
                uwp_extract_uri_string( b_el->media->img_source, uri, ARRAY_SIZE(uri) );
        }
        if (!uri[0] && uwp_elem_image_src( b_el )[0])
            lstrcpynW( uri, uwp_elem_image_src( b_el ), ARRAY_SIZE(uri) );
        if (!uri[0] && b_el->comp_brush)
            uwp_extract_comp_brush_image_src( b_el->comp_brush, uri, ARRAY_SIZE(uri) );

        if (uri[0])
        {
            struct uwp_media_ext *bm = uwp_elem_ensure_media( b_el );
            if (bm && wcscmp( bm->image_src, uri ))
            {
                lstrcpynW( bm->image_src, uri, ARRAY_SIZE(bm->image_src) );
                bm->img_loaded = FALSE;
                InterlockedExchange( &bm->img_loading, 0 );
            }
            uwp_load_image_asset( b_el );
            if (bm && bm->img_pixels && bm->img_w > 0 && bm->img_h > 0)
            {
                RECT dst = *rc;
                float uv_crop[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
                if (bm->media_stretch == 3) /* UniformToFill */
                {
                    double dst_ar = (double)bw / (double)bh;
                    double src_ar = (double)bm->img_w / (double)bm->img_h;
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
                else if (bm->media_stretch == 2) /* Uniform */
                {
                    int dw = (int)bm->img_w, dh = (int)bm->img_h, dx, dy;
                    if (dw * bh > dh * bw) { dh = MulDiv( dh, bw, dw ); dw = bw; }
                    else { dw = MulDiv( dw, bh, dh ); dh = bh; }
                    dx = rc->left + (bw - dw) / 2;
                    dy = rc->top + (bh - dh) / 2;
                    SetRect( &dst, dx, dy, dx + dw, dy + dh );
                }
                xaml_gpu_push_image_uv_rounded( ctx, (UINT64)(ULONG_PTR)b_el, bm->img_pixels,
                                                bm->img_w, bm->img_h, 0, &dst, uv_crop, rc,
                                                rad, (float)(opacity * uwp_elem_brush_opacity( b_el )) );
                return TRUE;
            }
        }
    }
    return FALSE;
}

static void paint_gpu_image( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    uwp_resolve_and_load_image( el );
    if (el->media && el->media->img_pixels && el->media->img_w > 0 && el->media->img_h > 0)
    {
        struct uwp_media_ext *m = el->media;
        RECT rc = el->bounds, dst = el->bounds;
        int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
        int dw = (int)m->img_w, dh = (int)m->img_h;
        float rad = (el->corner_radius > 0.0) ? (float)el->corner_radius : 0.0f;
        float uv_crop[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        if (rad <= 0.0f && el->parent && el->parent->corner_radius > 0.0)
            rad = (float)el->parent->corner_radius;
        if (m->media_stretch == 3) /* UniformToFill */
        {
            uwp_compute_uniform_to_fill_uv( bw, bh, dw, dh, uv_crop );
            xaml_gpu_push_image_uv_rounded( ctx, (UINT64)(ULONG_PTR)el, m->img_pixels,
                                            m->img_w, m->img_h, 0, &dst, uv_crop, &rc,
                                            rad, (float)el->opacity );
        }
        else
        {
            int dx, dy;
            if (m->media_stretch != 1 && bw > 0 && bh > 0 && dw > 0 && dh > 0)
            {
                if (dw * bh > dh * bw) { dh = MulDiv( dh, bw, dw ); dw = bw; }
                else { dw = MulDiv( dw, bh, dh ); dh = bh; }
            }
            dx = rc.left + (bw - dw) / 2;
            dy = rc.top + (bh - dh) / 2;
            SetRect( &dst, dx, dy, dx + dw, dy + dh );
            xaml_gpu_push_image_uv_rounded( ctx, (UINT64)(ULONG_PTR)el, m->img_pixels,
                                            m->img_w, m->img_h, 0, &dst, NULL, &rc,
                                            rad, (float)el->opacity );
        }
    }
}

static void paint_gpu_media( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    RECT rc = el->bounds;
    struct uwp_media_ext *m;
    uwp_media_ensure_started( el );
    m = el->media;
    if (!m) return;
    AcquireSRWLockShared( &m->video_lock );
    if (m->video_pixels && m->video_w > 0 && m->video_h > 0)
    {
        int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
        int vw = (int)m->video_w, vh = (int)m->video_h;
        RECT dst = rc;
        float uv_crop[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        if (bw > 0 && bh > 0 && vw > 0 && vh > 0)
        {
            if (m->media_stretch == 3) /* UniformToFill */
            {
                double dst_ar = (double)bw / (double)bh;
                double src_ar = (double)vw / (double)vh;
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
            else if (m->media_stretch == 2) /* Uniform */
            {
                int dw = vw, dh = vh, dx, dy;
                if (dw * bh > dh * bw) { dh = MulDiv( dh, bw, dw ); dw = bw; }
                else { dw = MulDiv( dw, bh, dh ); dh = bh; }
                dx = rc.left + (bw - dw) / 2;
                dy = rc.top + (bh - dh) / 2;
                SetRect( &dst, dx, dy, dx + dw, dy + dh );
            }
        }
        xaml_gpu_push_image_uv( ctx, (UINT64)(ULONG_PTR)el | 0x4000000000000000ULL,
                                m->video_pixels, m->video_w, m->video_h,
                                m->video_frame_seq, &dst, uv_crop, &rc );
    }
    ReleaseSRWLockShared( &m->video_lock );
}

static void paint_gpu_combobox( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    RECT rc = el->bounds, trc, chev_rc;
    UINT32 bg_argb = 0, bd_argb = 0, fg_argb = 0xffffffffu;
    float rad = (el->corner_radius > 0.0) ? (float)el->corner_radius : 4.0f;
    WCHAR label[128] = {0};
    int fh = max( (int)(12 * g_layout_scale + 0.5f),
                  min( (int)(18 * g_layout_scale + 0.5f),
                       (int)((el->has_font_size ? el->font_size : 14.0) * g_layout_scale + 0.5f) ) );
    int fw = (el->font_weight > 0) ? el->font_weight : FW_NORMAL;

    if (el->has_bg && el->bg_alpha > 0)
        bg_argb = apply_opacity_to_argb( el->bg_argb, el->opacity );
    else if (uwp_resolve_brush_or_color( NULL, el, L"ComboBoxBackground", 0, &bg_argb ) && ((bg_argb >> 24) & 0xff))
        bg_argb = apply_opacity_to_argb( bg_argb, el->opacity );
    else
        bg_argb = 0x33ffffffu;

    if (el->is_dropdown_open)
        bd_argb = uwp_is_light_theme( el ) ? 0xff0067c0u : 0xff60cdffu;
    else if (el->has_border && el->border_alpha > 0)
        bd_argb = apply_opacity_to_argb( el->border_argb, el->opacity );
    else if (uwp_resolve_brush_or_color( NULL, el, L"ComboBoxBorderBrush", 0, &bd_argb ) && ((bd_argb >> 24) & 0xff))
        bd_argb = apply_opacity_to_argb( bd_argb, el->opacity );
    else
        bd_argb = 0x44ffffffu;

    if (el->has_fg && el->fg_alpha > 0)
        fg_argb = el->fg_argb;
    else if (!uwp_resolve_brush_or_color( NULL, el, L"ComboBoxForeground", 0, &fg_argb ))
        fg_argb = 0xffffffffu;

    if (uwp_elem_selected_item( el ))
        uwp_combobox_get_item_label( el, uwp_elem_selected_item( el ), label, ARRAY_SIZE(label) );
    if (!label[0] && uwp_elem_selected_index( el ) >= 0)
    {
        IInspectable *items[256];
        int cnt = uwp_selector_get_items( el, items, ARRAY_SIZE(items) );
        int i, sidx = uwp_elem_selected_index( el );
        if (sidx < cnt)
            uwp_combobox_get_item_label( el, items[sidx], label, ARRAY_SIZE(label) );
        for (i = 0; i < cnt; i++) IInspectable_Release( items[i] );
    }
    if (!label[0] && uwp_has_text( el ))
        lstrcpynW( label, uwp_elem_text( el ), ARRAY_SIZE(label) );
    if (!label[0] && uwp_elem_placeholder( el )[0])
        lstrcpynW( label, uwp_elem_placeholder( el ), ARRAY_SIZE(label) );
    if (!label[0] && el->dp_cnt > 0 && el->dp_store)
    {
        int di;
        AcquireSRWLockShared( &el->dp_lock );
        for (di = 0; di < el->dp_cnt && !label[0]; di++)
        {
            const WCHAR *dpn = xaml_dp_get_name( el->dp_store[di].dp );
            if (el->dp_store[di].value &&
                (!wcscmp( dpn, L"SelectionBoxItem" ) || !wcscmp( dpn, L"PlaceholderText" ) || !wcscmp( dpn, L"Header" )))
                uwp_combobox_get_item_label( el, el->dp_store[di].value, label, ARRAY_SIZE(label) );
        }
        ReleaseSRWLockShared( &el->dp_lock );
    }
    if (!label[0] && el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count && !label[0]; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (uwp_has_text( ch ))
                lstrcpynW( label, uwp_elem_text( ch ), ARRAY_SIZE(label) );
        }
    }
    if (!label[0] && el->content)
    {
        struct uwp_element *cel = uwp_elem_from_any( el->content );
        if (cel) uwp_paint_first_subtree_text( cel, label, ARRAY_SIZE(label), 0 );
    }
    if (!label[0] && el->data_context)
        uwp_combobox_get_item_label( el, el->data_context, label, ARRAY_SIZE(label) );

    xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                        rad, bg_argb, bd_argb, 1.0f, 1.0f );
    if (label[0])
    {
        SetRect( &trc, rc.left + 12, rc.top, max( rc.left + 12, rc.right - 28 ), rc.bottom );
        xaml_gpu_push_text( ctx, label, fh, fw, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                            &trc, &rc, blend_argb_over( fg_argb, surface_bg ), 1.0f );
    }
    SetRect( &chev_rc, max( rc.left, rc.right - 28 ), rc.top, rc.right - 6, rc.bottom );
    xaml_gpu_push_text( ctx, el->is_dropdown_open ? L"\x25b4" : L"\x25be", max( 11, fh - 1 ), FW_NORMAL,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                        &chev_rc, &rc, blend_argb_over( fg_argb, surface_bg ), 0.85f );
}

static void paint_gpu_shape( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    WCHAR resolved_uri[512] = {0};
    BOOL brush_painted = FALSE;
    if (el->fill_brush)
        brush_painted = uwp_gpu_paint_brush_fill( ctx, el, el->fill_brush, &el->bounds,
                                                  (float)el->corner_radius, (float)el->opacity );
    if (!brush_painted && el->has_bg && el->bg_alpha > 0)
    {
        UINT32 fill_c = apply_opacity_to_argb( el->bg_argb, el->opacity );
        UINT32 bord_c = (el->has_border && el->border_alpha > 0)
            ? apply_opacity_to_argb( el->border_argb, el->opacity ) : 0;
        xaml_gpu_push_rect( (float)el->bounds.left, (float)el->bounds.top,
                            (float)el->bounds.right, (float)el->bounds.bottom,
                            NULL, (float)el->corner_radius, fill_c, bord_c,
                            bord_c ? 1.0f : 0.0f, 1.0f );
        brush_painted = TRUE;
    }
    if (!brush_painted && el->fill_brush)
    {
        struct uwp_element *b_el;
        uwp_xcbb_ensure_connected( el->fill_brush );
        b_el = uwp_elem_from_any( el->fill_brush );
        if (b_el)
        {
            const WCHAR *b_src = uwp_elem_image_src( b_el );
            if (b_src[0])
                lstrcpynW( resolved_uri, b_src, ARRAY_SIZE(resolved_uri) );
            else if (b_el->comp_brush)
                uwp_extract_comp_brush_image_src( b_el->comp_brush, resolved_uri, ARRAY_SIZE(resolved_uri) );
        }
    }
    if (!brush_painted && !resolved_uri[0] && el->dp_cnt > 0 && el->dp_store)
    {
        int di;
        for (di = 0; di < el->dp_cnt; di++)
        {
            struct uwp_element *b_el = el->dp_store[di].value ? uwp_elem_from_any( el->dp_store[di].value ) : NULL;
            if (!b_el) continue;
            if (b_el->class_flags & UWP_CF_BRUSH)
            {
                if (uwp_gpu_paint_brush_fill( ctx, el, el->dp_store[di].value, &el->bounds,
                                              (float)el->corner_radius, (float)el->opacity ))
                {
                    brush_painted = TRUE;
                    break;
                }
            }
            if (uwp_elem_image_src( b_el )[0])
            {
                lstrcpynW( resolved_uri, uwp_elem_image_src( b_el ), ARRAY_SIZE(resolved_uri) );
                break;
            }
            if (b_el->comp_brush && uwp_extract_comp_brush_image_src( b_el->comp_brush, resolved_uri, ARRAY_SIZE(resolved_uri) ))
                break;
        }
    }
    if (!brush_painted && !resolved_uri[0] && el->ecp_visual)
        uwp_extract_visual_image_src( el->ecp_visual, resolved_uri, ARRAY_SIZE(resolved_uri) );
    if (!brush_painted && resolved_uri[0] && wcscmp( uwp_elem_image_src( el ), resolved_uri ))
    {
        struct uwp_media_ext *m = uwp_elem_ensure_media( el );
        if (m)
        {
            lstrcpynW( m->image_src, resolved_uri, ARRAY_SIZE(m->image_src) );
            m->img_loaded = FALSE;
            InterlockedExchange( &m->img_loading, 0 );
        }
    }
    if (!brush_painted && uwp_elem_image_src( el )[0])
    {
        uwp_load_image_asset( el );
        if (el->media && el->media->img_pixels && el->media->img_w > 0 && el->media->img_h > 0)
        {
            struct uwp_media_ext *m = el->media;
            RECT rc = el->bounds;
            int rw = rc.right - rc.left, rh = rc.bottom - rc.top;
            if (rw >= 600 && rh >= 120)
            {
                float uv_crop[4];
                uwp_compute_uniform_to_fill_uv( rw, rh, (int)m->img_w, (int)m->img_h, uv_crop );
                xaml_gpu_push_image_uv_rounded( ctx, (UINT64)(ULONG_PTR)el, m->img_pixels,
                                                m->img_w, m->img_h, 0, &rc, uv_crop, &rc,
                                                (float)el->corner_radius, (float)el->opacity );
                xaml_gpu_push_gradient_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                                             (float)el->corner_radius, 0xf20e1522u, 0x220e1522u,
                                             0.0f, 0.5f, 0.92f, 0.5f, 0.22f, 0.88f, 1.0f );
                xaml_gpu_push_gradient_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                                             (float)el->corner_radius, 0x000a0e16u, 0xc40a0e16u,
                                             0.5f, 0.38f, 0.5f, 1.0f, 0.0f, 1.0f, 1.0f );
            }
            else
            {
                xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, m->img_pixels, m->img_w, m->img_h, &rc, NULL );
            }
        }
    }
}

static void paint_gpu_font_icon( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    WCHAR ff_buf[64];
    BOOL light = uwp_is_light_theme( el );
    int fh = max( (int)(12 * g_layout_scale + 0.5f),
                  min( (int)(64 * g_layout_scale + 0.5f),
                       (int)((el->has_font_size ? el->font_size : 16.0) * g_layout_scale + 0.5f) ) );
    COLORREF tfg = (el->has_fg && el->fg_alpha > 0)
        ? blend_argb_over( el->fg_argb, surface_bg )
        : uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush",
                                   light ? RGB( 26, 26, 26 ) : RGB( 255, 255, 255 ), surface_bg );
    xaml_gpu_push_text_font( ctx, uwp_elem_text( el ), uwp_resolve_elem_font_family( el, ff_buf, ARRAY_SIZE(ff_buf) ),
                             fh, (el->font_weight > 0) ? el->font_weight : FW_NORMAL,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE, &el->bounds, NULL, tfg, (float)el->opacity );
}

static void paint_gpu_progress_ring( struct uwp_element *el, COLORREF surface_bg )
{
    RECT rc = el->bounds;
    int cx = (rc.left + rc.right) / 2;
    int cy = min( rc.bottom - 36, (rc.top + rc.bottom) / 2 + 120 );
    int r = 18;
    COLORREF track_cr, arc_cr;
    uwp_paint_resolve_progress_ring_colors( el, surface_bg, &track_cr, &arc_cr );
    xaml_gpu_push_ring( (float)(cx - r), (float)(cy - r), (float)(cx + r), (float)(cy + r), NULL, cr_to_argb( track_cr ), 3.0f, FALSE );
    xaml_gpu_push_ring( (float)(cx - r), (float)(cy - r), (float)(cx + r), (float)(cy + r), NULL, cr_to_argb( arc_cr ), 3.0f, TRUE );
}

static void paint_gpu_progress_bar( struct uwp_element *el, COLORREF surface_bg )
{
    RECT rc = el->bounds;
    int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
    double rmin = el->range_min;
    double rmax = (el->range_max > rmin) ? el->range_max : (rmin + 100.0);
    double rval = min( rmax, max( rmin, el->range_val ) );
    double frac = (rmax > rmin) ? ((rval - rmin) / (rmax - rmin)) : 0.0;
    int bar_h = min( max( 4, bh ), (int)(8 * g_layout_scale + 0.5f) );
    int y0 = rc.top + (bh - bar_h) / 2;
    int y1 = y0 + bar_h;
    float rad = (el->corner_radius > 0.0) ? (float)el->corner_radius : (float)bar_h * 0.5f;
    COLORREF track_cr = (el->has_bg && el->bg_alpha > 0)
        ? blend_argb_over( el->bg_argb, surface_bg )
        : uwp_paint_resolve_color( el, L"ControlStrongStrokeColorDefaultBrush",
                                   uwp_is_light_theme( el ) ? RGB( 210, 210, 214 ) : RGB( 68, 68, 74 ), surface_bg );
    COLORREF fill_cr = (el->has_fg && el->fg_alpha > 0)
        ? blend_argb_over( el->fg_argb, surface_bg )
        : uwp_paint_resolve_color( el, L"AccentFillColorDefaultBrush",
                                   uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
    if (bw <= 2 || bh <= 1) return;
    xaml_gpu_push_rect( (float)rc.left, (float)y0, (float)rc.right, (float)y1, NULL,
                        rad, cr_to_argb( track_cr ), 0, 0.0f, (float)el->opacity );
    if (frac > 0.001)
    {
        int fill_w = max( bar_h, (int)(bw * frac + 0.5) );
        if (fill_w > bw) fill_w = bw;
        xaml_gpu_push_rect( (float)rc.left, (float)y0, (float)(rc.left + fill_w), (float)y1, NULL,
                            rad, cr_to_argb( fill_cr ), 0, 0.0f, (float)el->opacity );
    }
}

static void paint_gpu_searchbox( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    RECT rc = el->bounds, pill_rc, txt_rc;
    int bw = rc.right - rc.left, bh = rc.bottom - rc.top;
    int pill_w, pill_h, px0, py0;
    struct uwp_element *inner_tb = uwp_paint_find_inner_search_textbox( el, 0 );
    const WCHAR *cur_txt = uwp_has_text( el ) ? uwp_elem_text( el ) : uwp_elem_text( inner_tb );
    BOOL focused = (g_focused_textbox == el) || (inner_tb && g_focused_textbox == inner_tb) ||
                   uwp_elem_is_descendant( el, g_focused_textbox );
    COLORREF sbg, sbd, sfg;
    WCHAR disp[264];

    uwp_paint_resolve_searchbox_style( el, surface_bg, focused, cur_txt, &sbg, &sbd, &sfg, disp, ARRAY_SIZE(disp) );

    if (bw >= 600)
    {
        pill_w = min( 560, max( 320, bw / 2 ) );
        pill_h = min( 32, max( 26, bh - 8 ) );
        px0 = rc.left + (bw - pill_w) / 2;
        py0 = rc.top + (bh - pill_h) / 2;
        SetRect( &pill_rc, px0, py0, px0 + pill_w, py0 + pill_h );
    }
    else
    {
        pill_rc = rc;
    }
    xaml_gpu_push_rect( (float)pill_rc.left, (float)pill_rc.top, (float)pill_rc.right, (float)pill_rc.bottom, NULL,
                        6.0f, cr_to_argb( sbg ), cr_to_argb( sbd ), focused ? 2.0f : 1.0f, 1.0f );
    txt_rc = pill_rc;
    txt_rc.left += 14;
    txt_rc.right -= 14;
    xaml_gpu_push_text( ctx, disp, 13, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE, &txt_rc, NULL, sfg, 1.0f );
}

static void paint_gpu_tabview( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    RECT trc = el->bounds;
    IInspectable *items[64];
    int count = uwp_tabview_get_items( el, items, ARRAY_SIZE(items) );
    int i;
    if (count == 0)
    {
        RECT pill, close_rc, add_rc;
        uwp_tabview_compute_rects( &trc, 1, 0, &pill, &close_rc, &add_rc );
        if (uwp_has_text( el ))
        {
            xaml_gpu_push_rect( (float)pill.left, (float)pill.top, (float)pill.right, (float)pill.bottom, NULL,
                                0.0f, cr_to_argb( RGB( 44, 44, 44 ) ), 0, 0.0f, 1.0f );
            pill.left += 10;
            pill.right = max( pill.left, close_rc.left - 2 );
            xaml_gpu_push_text( ctx, uwp_elem_text( el ), 13, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, &pill, NULL, RGB( 255, 255, 255 ), 1.0f );
            xaml_gpu_push_text( ctx, L"\x00d7", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &close_rc, NULL, RGB( 185, 185, 185 ), 1.0f );
        }
        if (add_rc.right > add_rc.left + 12)
            xaml_gpu_push_text( ctx, L"+", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &add_rc, NULL, RGB( 210, 210, 210 ), 1.0f );
        return;
    }
    for (i = 0; i < count; i++)
    {
        WCHAR tab_title[128] = {0};
        RECT pill, close_rc, add_rc;
        IInspectable *sel = uwp_elem_selected_item( el );
        BOOL is_sel = sel ? (items[i] == sel) : (i == 0);
        COLORREF bg = is_sel ? RGB( 44, 44, 44 ) : RGB( 28, 28, 28 );
        COLORREF fg = is_sel ? RGB( 255, 255, 255 ) : RGB( 195, 195, 195 );
        uwp_tabview_get_item_title( items[i], tab_title, ARRAY_SIZE(tab_title) );
        uwp_tabview_compute_rects( &trc, count, i, &pill, &close_rc, &add_rc );
        xaml_gpu_push_rect( (float)pill.left, (float)pill.top, (float)pill.right, (float)pill.bottom, NULL,
                            0.0f, cr_to_argb( bg ), is_sel ? 0 : cr_to_argb( RGB( 54, 54, 54 ) ), is_sel ? 0.0f : 1.0f, 1.0f );
        pill.left += 10;
        pill.right = max( pill.left, close_rc.left - 2 );
        xaml_gpu_push_text( ctx, tab_title, 13, is_sel ? FW_SEMIBOLD : FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, &pill, NULL, fg, 1.0f );
        xaml_gpu_push_text( ctx, L"\x00d7", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &close_rc, NULL, RGB( 185, 185, 185 ), 1.0f );
        if (i == count - 1 && add_rc.right > add_rc.left + 12)
            xaml_gpu_push_text( ctx, L"+", 13, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &add_rc, NULL, RGB( 210, 210, 210 ), 1.0f );
        IInspectable_Release( items[i] );
    }
}

static void paint_gpu_pivot_item( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    RECT trc = el->bounds;
    BOOL is_first = FALSE;
    int fh = 13;
    COLORREF tfg, acc;
    uwp_paint_resolve_pivot_item_style( el, surface_bg, &is_first, &fh, &tfg, &acc );
    xaml_gpu_push_text( ctx, uwp_elem_text( el ), fh, is_first ? FW_SEMIBOLD : FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE, &trc, NULL, tfg, 1.0f );
    if (is_first)
    {
        xaml_gpu_push_rect( (float)trc.left, (float)(trc.bottom - 3), (float)min( trc.right, trc.left + 52 ), (float)trc.bottom,
                            NULL, 0.0f, cr_to_argb( acc ), 0, 0.0f, 1.0f );
    }
}

static BOOL paint_gpu_container( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    BOOL skip_fullscreen_bg = xaml_has_active_swapchain_panel() &&
        (el->bounds.right - el->bounds.left) >= 1000 &&
        (el->bounds.bottom - el->bounds.top) >= 600;
    BOOL is_ellipse = (el->class_flags & UWP_CF_ELLIPSE) != 0;
    float rad = is_ellipse
        ? (float)min( el->bounds.right - el->bounds.left, el->bounds.bottom - el->bounds.top ) * 0.5f
        : ((el->corner_radius > 0.0) ? (float)el->corner_radius : 0.0f);
    BOOL painted_bg_brush = FALSE;
    if (!skip_fullscreen_bg && el->bg_brush)
        painted_bg_brush = uwp_gpu_paint_brush_fill( ctx, el, el->bg_brush, &el->bounds, rad, (float)el->opacity );
    if ((!skip_fullscreen_bg && !painted_bg_brush && el->has_bg && el->bg_alpha > 0) ||
        (el->has_border && el->border_alpha > 0) ||
        (is_ellipse && (el->bounds.right - el->bounds.left) <= 64))
    {
        UINT32 fill_c = (!skip_fullscreen_bg && !painted_bg_brush && el->has_bg && el->bg_alpha > 0)
            ? apply_opacity_to_argb( el->bg_argb, el->opacity ) : 0;
        UINT32 bord_c = (el->has_border && el->border_alpha > 0)
            ? apply_opacity_to_argb( el->border_argb, el->opacity ) : 0;
        float tl = (float)max( 0.0, el->border_thick_l );
        float tt = (float)max( 0.0, el->border_thick_t );
        float tr = (float)max( 0.0, el->border_thick_r );
        float tb = (float)max( 0.0, el->border_thick_b );
        BOOL has_thick = (tl > 0.0f || tt > 0.0f || tr > 0.0f || tb > 0.0f);
        BOOL uniform_thick = has_thick && (tl == tt && tt == tr && tr == tb);

        if (is_ellipse && !has_thick && bord_c == 0 && (el->bounds.right - el->bounds.left) <= 64)
        {
            bord_c = uwp_is_light_theme( el ) ? 0x30000000u : 0x40ffffffu;
            tl = tt = tr = tb = 1.0f;
            has_thick = uniform_thick = TRUE;
        }

        if (uniform_thick && bord_c != 0)
        {
            xaml_gpu_push_rect( (float)el->bounds.left, (float)el->bounds.top,
                                (float)el->bounds.right, (float)el->bounds.bottom,
                                NULL, rad, fill_c, bord_c, tl, 1.0f );
        }
        else
        {
            if (fill_c != 0)
                xaml_gpu_push_rect( (float)el->bounds.left, (float)el->bounds.top,
                                    (float)el->bounds.right, (float)el->bounds.bottom,
                                    NULL, rad, fill_c, 0, 0.0f, 1.0f );
            if (has_thick && bord_c != 0)
            {
                float x0 = (float)el->bounds.left, y0 = (float)el->bounds.top;
                float x1 = (float)el->bounds.right, y1 = (float)el->bounds.bottom;
                if (tl > 0.0f) xaml_gpu_push_rect( x0, y0, min( x1, x0 + tl ), y1, NULL, 0.0f, bord_c, 0, 0.0f, 1.0f );
                if (tt > 0.0f) xaml_gpu_push_rect( x0, y0, x1, min( y1, y0 + tt ), NULL, 0.0f, bord_c, 0, 0.0f, 1.0f );
                if (tr > 0.0f) xaml_gpu_push_rect( max( x0, x1 - tr ), y0, x1, y1, NULL, 0.0f, bord_c, 0, 0.0f, 1.0f );
                if (tb > 0.0f) xaml_gpu_push_rect( x0, max( y0, y1 - tb ), x1, y1, NULL, 0.0f, bord_c, 0, 0.0f, 1.0f );
            }
        }
    }
    if (uwp_is_tabview( el ))
    {
        paint_gpu_tabview( ctx, el );
        return TRUE;
    }
    if (uwp_is_pivot_item( el ) && uwp_has_text( el ))
    {
        paint_gpu_pivot_item( ctx, el, surface_bg );
        if (uwp_paint_pivot_item_is_selected( el ) && el->content)
        {
            struct uwp_element *body = uwp_elem_from_any( el->content );
            if (body && body != el)
                uwp_gpu_paint_element( ctx, body );
        }
        return TRUE;
    }
    return FALSE;
}

static void paint_gpu_button( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    RECT brc = el->bounds;
    WCHAR tmp[64];
    const WCHAR *label = uwp_format_display_label( el, tmp, ARRAY_SIZE(tmp) );
    COLORREF bg, fg, bd;
    BOOL draw_bg = TRUE, draw_bd = TRUE;
    int fh;

    if (!label[0] && (!el->content || !uwp_elem_from_any( el->content )) &&
        !(el->probe_flags & (UWP_PF_NO_BTN_LBL | UWP_PF_NO_BTN_IMG)))
    {
        IInspectable *vm = (el->content && !uwp_elem_from_any( el->content )) ? el->content : el->data_context;
        if (vm)
        {
            static const WCHAR *const title_props[] = { L"Title", L"ProductTitle", L"Name", L"Label", L"DisplayName", L"Header", L"Text", L"ShortTitle" };
            static const WCHAR *const sub_props[] = { L"DisplayPrice", L"FormattedPrice", L"Price", L"Category", L"PublisherName", L"Subtitle", L"AverageRating" };
            static const WCHAR *const nested_props[] = { L"Product", L"ViewModel", L"Model", L"Data", L"Item", L"ProductSummary" };
            WCHAR title_buf[128] = {0}, sub_buf[128] = {0};
            IInspectable *target_vm = vm;
            IInspectable *nested_vm = NULL;

            if (!uwp_vm_query_string_prop( vm, title_props, ARRAY_SIZE(title_props), title_buf, ARRAY_SIZE(title_buf) ))
            {
                HSTRING hcls = NULL;
                if (SUCCEEDED(IInspectable_GetRuntimeClassName( vm, &hcls )) && hcls)
                {
                    const WCHAR *cls = WindowsGetStringRawBuffer( hcls, NULL );
                    unsigned int ni;
                    for (ni = 0; ni < ARRAY_SIZE(nested_props) && !nested_vm; ni++)
                    {
                        IInspectable *nv = NULL;
                        if (cls && cls[0] && uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, nested_props[ni], vm, &nv ) && nv)
                            nested_vm = nv;
                    }
                    WindowsDeleteString( hcls );
                }
                if (nested_vm)
                {
                    target_vm = nested_vm;
                    uwp_vm_query_string_prop( target_vm, title_props, ARRAY_SIZE(title_props), title_buf, ARRAY_SIZE(title_buf) );
                }
            }
            if (title_buf[0])
            {
                if (uwp_vm_query_string_prop( target_vm, sub_props, ARRAY_SIZE(sub_props), sub_buf, ARRAY_SIZE(sub_buf) ))
                {
                    WCHAR combined[256];
                    swprintf( combined, ARRAY_SIZE(combined), L"%.120s\n%.120s", title_buf, sub_buf );
                    uwp_elem_set_text( el, combined );
                }
                else
                    uwp_elem_set_text( el, title_buf );
                label = uwp_elem_text( el );
            }
            else
            {
                el->probe_flags |= UWP_PF_NO_BTN_LBL;
            }
            if (nested_vm) IInspectable_Release( nested_vm );
        }
    }

    if (el->data_context && (brc.right - brc.left) >= 60 && (brc.bottom - brc.top) >= 28)
    {
        BOOL in_items_host = FALSE;
        struct uwp_element *anc = el->parent;
        int hop;
        for (hop = 0; anc && hop < 5; anc = anc->parent, hop++)
        {
            if (anc->class_flags & (UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW | UWP_CF_ITEMS_REPEATER | UWP_CF_FLIPVIEW))
            {
                in_items_host = TRUE;
                break;
            }
        }
        if (in_items_host && (!el->content || !uwp_elem_from_any( el->content )))
        {
            if (!uwp_elem_image_src( el )[0] && !(el->probe_flags & UWP_PF_NO_BTN_IMG))
            {
                static const WCHAR *const item_img_props[] = { L"Uri", L"Url", L"ImageUrl", L"ImageUri", L"ThumbnailUrl", L"PreviewImageUrl", L"ScreenshotUrl", L"Image", L"Images", L"Source", L"IconUrl" };
                WCHAR img_buf[512] = {0};
                if (uwp_vm_query_string_prop_depth( el->data_context, item_img_props, ARRAY_SIZE(item_img_props), img_buf, ARRAY_SIZE(img_buf), 2 ) &&
                    img_buf[0])
                {
                    struct uwp_media_ext *m = uwp_elem_ensure_media( el );
                    if (m) lstrcpynW( m->image_src, img_buf, ARRAY_SIZE(m->image_src) );
                }
                else
                    el->probe_flags |= UWP_PF_NO_BTN_IMG;
            }
            if (uwp_elem_image_src( el )[0])
                uwp_load_image_asset( el );
            if (el->media && el->media->img_pixels && el->media->img_w > 0 && el->media->img_h > 0)
            {
                BOOL light = uwp_is_light_theme( el );
                RECT irc = brc;
                InflateRect( &irc, -2, -2 );
                xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                    6.0f, cr_to_argb( light ? RGB( 240, 240, 240 ) : RGB( 32, 32, 32 ) ),
                                    cr_to_argb( light ? RGB( 218, 218, 218 ) : RGB( 68, 68, 68 ) ), 1.0f, 1.0f );
                xaml_gpu_push_image( ctx, (UINT64)(ULONG_PTR)el, el->media->img_pixels, el->media->img_w, el->media->img_h, &irc, NULL );
                return;
            }
        }
    }

    if (label[0] && (wcschr( label, '\n' ) || uwp_is_card( el )) &&
        (brc.right - brc.left) >= 160 && (brc.bottom - brc.top) >= 60)
    {
        const WCHAR *nl = wcschr( label, '\n' );
        WCHAR tline[128] = {0};
        RECT trc, src;
        size_t tlen = nl ? (size_t)(nl - label) : wcslen( label );
        if (tlen >= ARRAY_SIZE(tline)) tlen = ARRAY_SIZE(tline) - 1;
        memcpy( tline, label, tlen * sizeof(WCHAR) );
        tline[tlen] = 0;
        InflateRect( &brc, -2, -2 );
        {
            BOOL light = uwp_is_light_theme( el );
            COLORREF cbg = light ? (el->is_hovered ? RGB( 242, 242, 242 ) : RGB( 255, 255, 255 ))
                                 : (el->is_hovered ? RGB( 52, 52, 52 ) : RGB( 42, 42, 42 ));
            COLORREF cbd = light ? RGB( 224, 224, 224 ) : RGB( 68, 68, 68 );
            COLORREF cfg = light ? RGB( 26, 26, 26 ) : RGB( 255, 255, 255 );
            COLORREF csfg = light ? RGB( 92, 92, 92 ) : RGB( 180, 180, 180 );
            xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                6.0f, cr_to_argb( cbg ), cr_to_argb( cbd ), 1.0f, 1.0f );
            SetRect( &trc, brc.left + 14, brc.top + 14, brc.right - 14, brc.top + 48 );
            xaml_gpu_push_text( ctx, tline, 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                &trc, NULL, cfg, 1.0f );
            if (nl)
            {
                SetRect( &src, brc.left + 14, brc.top + 52, brc.right - 14, brc.bottom - 12 );
                xaml_gpu_push_text( ctx, nl + 1, 12, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &src, NULL, csfg, 1.0f );
            }
        }
        return;
    }

    if (uwp_paint_is_invisible_shortcut_button( el ))
        return;

    if (el->kind == UWP_KIND_TOGGLE_BUTTON && !wcscmp( el->class_name, L"Windows.UI.Xaml.Controls.RadioButton" ))
    {
        float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        BOOL light = uwp_is_light_theme( el );
        BOOL checked = (el->is_checked == 1);
        int cy = (brc.top + brc.bottom) / 2;
        int r_out = max( 8, (int)(9 * sc + 0.5f) );
        int cx = brc.left + r_out + (int)(2 * sc);
        UINT32 acc_c = light ? 0xff005fb8u : 0xff60cdffu;
        UINT32 ring_bg = checked ? acc_c : (el->is_hovered ? (light ? 0x10000000u : 0x18ffffffu) : 0x00000000u);
        UINT32 ring_bd = checked ? acc_c : (light ? 0x80000000u : 0x88ffffffu);
        COLORREF fg_c = light ? RGB( 28, 28, 28 ) : RGB( 245, 245, 245 );
        xaml_gpu_push_rect( (float)(cx - r_out), (float)(cy - r_out), (float)(cx + r_out), (float)(cy + r_out),
                            NULL, (float)r_out, ring_bg, ring_bd, checked ? 1.5f : 1.2f, 1.0f );
        if (checked)
        {
            int r_in = max( 3, (int)(4 * sc + 0.5f) );
            UINT32 dot_c = light ? 0xffffffffu : 0xff000000u;
            xaml_gpu_push_rect( (float)(cx - r_in), (float)(cy - r_in), (float)(cx + r_in), (float)(cy + r_in),
                                NULL, (float)r_in, dot_c, 0, 0.0f, 1.0f );
        }
        if (label[0])
        {
            RECT trc = { cx + r_out + (int)(8 * sc), brc.top, brc.right, brc.bottom };
            xaml_gpu_push_text( ctx, label, max( 12, (int)(13 * sc + 0.5f) ), FW_NORMAL,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE, &trc, NULL, fg_c, 1.0f );
        }
        return;
    }

    if (uwp_paint_is_nav_toggle_item( el ))
    {
        RECT rc = el->bounds;
        BOOL selected = (el->is_checked == 1);
        if (selected || el->is_hovered || el->is_pressed)
        {
            COLORREF nav_bg = el->is_pressed ? RGB( 52, 52, 52 ) : (selected ? RGB( 40, 40, 40 ) : RGB( 34, 34, 34 ));
            xaml_gpu_push_rect( (float)(rc.left + 2), (float)(rc.top + 2), (float)(rc.right - 2), (float)(rc.bottom - 2),
                                NULL, 3.0f, cr_to_argb( nav_bg ), 0, 0.0f, 1.0f );
        }
        if (selected)
        {
            COLORREF accent_cr = uwp_paint_resolve_color( el, L"AccentBrush", uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 215 ), surface_bg ), surface_bg );
            xaml_gpu_push_rect( (float)rc.left, (float)(rc.top + 4), (float)(rc.left + 3), (float)(rc.bottom - 4),
                                NULL, 0.0f, cr_to_argb( accent_cr ), 0, 0.0f, 1.0f );
        }
        if (label[0])
        {
            COLORREF nfg = selected
                ? uwp_paint_resolve_color( el, L"PrimaryForegroundBrush", RGB( 255, 255, 255 ), surface_bg )
                : uwp_paint_resolve_color( el, L"SecondaryForegroundBrush", RGB( 180, 180, 180 ), surface_bg );
            xaml_gpu_push_text( ctx, label, 12, selected ? FW_SEMIBOLD : FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &rc, NULL, nfg, 1.0f );
        }
        return;
    }

    if (!wcscmp( el->name, L"TogglePaneButton" ))
    {
        BOOL light = uwp_is_light_theme( el );
        RECT trc = el->bounds;
        COLORREF t_fg = light ? RGB( 26, 26, 26 ) : RGB( 242, 242, 245 );
        int t_fh = max( 14, (int)(16 * g_layout_scale + 0.5f) );
        if (el->is_pressed || el->is_hovered)
        {
            COLORREF hbg = light
                ? (el->is_pressed ? RGB( 218, 218, 224 ) : RGB( 232, 232, 236 ))
                : (el->is_pressed ? RGB( 58, 58, 64 ) : RGB( 48, 48, 54 ));
            xaml_gpu_push_rect( (float)trc.left, (float)trc.top, (float)trc.right, (float)trc.bottom, NULL,
                                4.0f, cr_to_argb( hbg ), 0, 0.0f, 1.0f );
        }
        xaml_gpu_push_text( ctx, L"\x2630", t_fh, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                            &trc, NULL, t_fg, 1.0f );
        return;
    }

    InflateRect( &brc, -2, -2 );
    if (brc.right <= brc.left || brc.bottom <= brc.top) return;

    {
        UINT32 bg_argb = 0, bd_argb = 0;
        float rad = (el->corner_radius > 0.0) ? (float)el->corner_radius : 4.0f;
        int fw = (el->font_weight > 0) ? el->font_weight : FW_SEMIBOLD;

        uwp_paint_resolve_button_style( el, surface_bg, label, &draw_bg, &draw_bd,
                                        &bg, &fg, &bd, &bg_argb, &bd_argb );

        {
            int bh = brc.bottom - brc.top;
            double base_fs = el->has_font_size ? el->font_size : 14.0;
            int max_fh = max( (int)(12 * g_layout_scale + 0.5f), min( (int)(26 * g_layout_scale + 0.5f), (bh * 3) / 5 ) );
            fh = max( (int)(12 * g_layout_scale + 0.5f), min( max_fh, (int)(base_fs * g_layout_scale + 0.5f) ) );
        }
        if (draw_bg || draw_bd)
        {
            xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                rad, draw_bg ? bg_argb : 0, draw_bd ? bd_argb : 0,
                                draw_bd ? 1.0f : 0.0f, 1.0f );
        }
        if (el->content_kind == UWP_CONTENT_ELEMENT || (el->content && uwp_elem_from_any( el->content )))
        {
            struct uwp_element *ce = uwp_elem_from_any( el->content );
            if (ce && ce != el) uwp_gpu_paint_element( ctx, ce );
            if (label[0] && (!ce || !uwp_paint_btn_child_has_text( ce, 0 )))
            {
                WCHAR ff_buf[64];
                RECT trc = brc;
                xaml_gpu_push_text_font( ctx, label, uwp_resolve_elem_font_family( el, ff_buf, ARRAY_SIZE(ff_buf) ),
                                         fh, fw, DT_SINGLELINE | DT_CENTER | DT_VCENTER, &trc, NULL, fg, 1.0f );
            }
        }
        else if (label[0])
        {
            WCHAR ff_buf[64];
            RECT trc = brc;
            UINT dt_flags = DT_SINGLELINE;
            int hca = el->has_h_content_align ? el->h_content_align : 1; /* default Center */
            int vca = el->has_v_content_align ? el->v_content_align : 1; /* default Center */
            if ((el->class_flags & UWP_CF_DROP_DOWN_BUTTON) && (brc.right - brc.left) > 36)
            {
                RECT crc = { brc.right - 24, brc.top, brc.right - 4, brc.bottom };
                WCHAR cff_buf[64];
                uwp_resolve_font_family( L"Segoe MDL2 Assets", cff_buf, ARRAY_SIZE(cff_buf) );
                xaml_gpu_push_text_font( ctx, L"\xE70D", cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                         max( 11, (int)(11 * g_layout_scale + 0.5f) ), FW_NORMAL,
                                         DT_CENTER | DT_VCENTER | DT_SINGLELINE, &crc, NULL, fg, 1.0f );
                trc.right -= 18;
            }
            if (hca == 0) { dt_flags |= DT_LEFT; trc.left += max( 4, (int)el->padding_l ); }
            else if (hca == 2) { dt_flags |= DT_RIGHT; trc.right -= max( 4, (int)el->padding_r ); }
            else dt_flags |= DT_CENTER;
            if (vca == 0) { dt_flags |= DT_TOP; trc.top += max( 2, (int)el->padding_t ); }
            else if (vca == 2) { dt_flags |= DT_BOTTOM; trc.bottom -= max( 2, (int)el->padding_b ); }
            else dt_flags |= DT_VCENTER;
            xaml_gpu_push_text_font( ctx, label, uwp_resolve_elem_font_family( el, ff_buf, ARRAY_SIZE(ff_buf) ),
                                     fh, fw, dt_flags, &trc, NULL, fg, 1.0f );
        }
        else if (!wcscmp( el->name, L"CloseButton" ))
        {
            WCHAR cff_buf[64];
            RECT trc = brc;
            uwp_resolve_font_family( L"Segoe MDL2 Assets", cff_buf, ARRAY_SIZE(cff_buf) );
            xaml_gpu_push_text_font( ctx, L"\xE711", cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                     max( 12, (int)(12 * g_layout_scale + 0.5f) ), FW_NORMAL,
                                     DT_SINGLELINE | DT_CENTER | DT_VCENTER, &trc, NULL, fg, 1.0f );
        }
        (void)bd;
    }
}

static BOOL paint_gpu_text( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    WCHAR rich_buf[2048];
    const WCHAR *draw_txt = uwp_elem_text( el );
    BOOL is_rich = FALSE;

    if (el->has_fg && el->fg_alpha == 0)
        return TRUE;

    if (!draw_txt[0] && el->inlines_coll)
    {
        if (uwp_flatten_inlines_text( el, rich_buf, ARRAY_SIZE(rich_buf) ) > 0)
        {
            draw_txt = rich_buf;
            is_rich = TRUE;
        }
    }
    if (!draw_txt[0] && !wcscmp( el->name, L"InitialsTextBlock" ))
    {
        RECT trc = el->bounds;
        COLORREF tfg = (el->has_fg && el->fg_alpha > 0)
            ? blend_argb_over( el->fg_argb, surface_bg )
            : uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush",
                                       uwp_is_light_theme( el ) ? RGB( 26, 26, 26 ) : RGB( 235, 235, 240 ), surface_bg );
        xaml_gpu_push_text_font( ctx, L"\xE77B", L"Segoe MDL2 Assets",
                                 max( 12, (int)(14 * g_layout_scale + 0.5f) ), FW_NORMAL,
                                 DT_CENTER | DT_VCENTER | DT_SINGLELINE, &trc, NULL, tfg, 1.0f );
        return TRUE;
    }
    if (draw_txt[0])
    {
        RECT trc = el->bounds;
        double eff_fs = el->font_size;
        int eff_fw = el->font_weight;
        BOOL has_anc_fg = FALSE;
        COLORREF anc_fg = 0;
        BOOL is_sec_hdr = FALSE;
        BYTE eff_fd = el->flow_direction;
        BOOL eff_rtl;
        int anc_h_content_align = -1;
        struct uwp_element *anc_btn = NULL;
        struct uwp_element *parent_btn = NULL;
        struct uwp_element *anc;
        int hop;
        int fh;
        COLORREF tfg;
        if (el->parent && el->parent->kind == UWP_KIND_GRID && el->parent->children_coll)
        {
            struct uwp_vec_view *pv = CONTAINING_RECORD( el->parent->children_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int pk;
            BOOL seen_self = FALSE;
            for (pk = 0; pv && pk < pv->count; pk++)
            {
                struct uwp_element *sib = uwp_elem_from_any( pv->items[pk] );
                if (sib == el) { seen_self = TRUE; continue; }
                if (seen_self && sib && sib->kind == UWP_KIND_TEXT_BLOCK &&
                    sib->visibility == 0 && uwp_has_text( sib ) &&
                    sib->grid_row == el->grid_row && sib->grid_col == el->grid_col)
                    return TRUE;
            }
        }
        for (anc = el->parent, hop = 0; anc && hop < 6; anc = anc->parent, hop++)
        {
            if (eff_fd == 0 && anc->flow_direction != 0)
                eff_fd = anc->flow_direction;
            if (!el->has_font_size && eff_fs == el->font_size && anc->has_font_size && anc->font_size > 0.0)
                eff_fs = anc->font_size;
            if (eff_fw <= 0 && anc->font_weight > 0)
                eff_fw = anc->font_weight;
            if (anc_h_content_align < 0)
            {
                if (anc->has_h_content_align)
                    anc_h_content_align = anc->h_content_align;
                else if ((anc->kind == UWP_KIND_SCROLL_VIEWER || anc->template_root != NULL) &&
                         (anc->h_align == 0 || anc->h_align == 1 || anc->h_align == 2))
                    anc_h_content_align = anc->h_align;
            }
            if (!el->has_fg && !has_anc_fg && anc->has_fg && anc->fg_alpha > 0)
            {
                anc_fg = blend_argb_over( anc->fg_argb, surface_bg );
                has_anc_fg = TRUE;
            }
            if (anc->kind == UWP_KIND_BUTTON || anc->kind == UWP_KIND_TOGGLE_BUTTON || anc->kind == UWP_KIND_CONTENT_CONTROL)
            {
                if (anc->kind == UWP_KIND_BUTTON || anc->kind == UWP_KIND_TOGGLE_BUTTON)
                    parent_btn = anc;
                if (!el->has_fg && !has_anc_fg && (anc->is_accent_style || (anc->kind == UWP_KIND_TOGGLE_BUTTON && anc->is_checked == 1)))
                {
                    anc_fg = uwp_paint_resolve_color( anc, L"AccentButtonForeground", RGB( 255, 255, 255 ), surface_bg );
                    has_anc_fg = TRUE;
                    if (eff_fw < FW_SEMIBOLD) eff_fw = FW_SEMIBOLD;
                }
                if (anc->kind == UWP_KIND_BUTTON && !anc->has_bg && !anc->bg_brush && !anc->has_border &&
                    !anc->is_accent_style && anc->h_align != 1 &&
                    (eff_fs >= 18.0 || eff_fw >= FW_SEMIBOLD))
                {
                    is_sec_hdr = TRUE;
                    anc_btn = anc;
                }
                break;
            }
            if (anc->template_root != NULL)
                break;
        }
        eff_rtl = (eff_fd == 2);
        if (!is_sec_hdr && eff_fs >= 18.0 && eff_fw >= FW_SEMIBOLD)
            is_sec_hdr = TRUE;
        if (parent_btn && !is_sec_hdr && parent_btn->has_bounds &&
            (parent_btn->is_accent_style ||
             parent_btn->content == &el->IInspectable_iface || parent_btn->content == el->outer))
        {
            trc = parent_btn->bounds;
            InflateRect( &trc, -2, -2 );
        }
        if (is_sec_hdr)
        {
            if (eff_fs < 18.0) eff_fs = 18.0;
            if (eff_fw < FW_SEMIBOLD) eff_fw = FW_SEMIBOLD;
        }
        if (is_rich && el->inlines_coll)
        {
            struct uwp_vec_view *iv = CONTAINING_RECORD( el->inlines_coll, struct uwp_vec_view, IInspectable_iface );
            unsigned int ik;
            for (ik = 0; iv && ik < iv->count; ik++)
            {
                struct uwp_element *inl = uwp_elem_from_any( iv->items[ik] );
                if (inl && inl->has_font_size && inl->font_size > eff_fs)
                    eff_fs = inl->font_size;
            }
        }
        fh = max( (int)(11 * g_layout_scale + 0.5f), min( (int)(96 * g_layout_scale + 0.5f), (int)(eff_fs * g_layout_scale + 0.5f) ) );
        if (is_sec_hdr)
            tfg = uwp_is_light_theme( el ) ? RGB( 22, 22, 26 ) : RGB( 250, 250, 252 );
        else if (el->has_fg && el->fg_alpha > 0)
            tfg = blend_argb_over( el->fg_argb, surface_bg );
        else if (has_anc_fg)
            tfg = anc_fg;
        else
            tfg = uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush",
                                           uwp_is_light_theme( el ) ? RGB( 26, 26, 26 ) : RGB( 255, 255, 255 ), surface_bg );
        if (uwp_is_menubar_item( el ) &&
            (el->is_pressed || el->is_hovered || uwp_popup_is_open_for( el )))
        {
            RECT hrc = trc;
            InflateRect( &hrc, -2, -3 );
            xaml_gpu_push_rect( (float)hrc.left, (float)hrc.top, (float)hrc.right, (float)hrc.bottom, NULL,
                                3.0f, cr_to_argb( RGB( 54, 54, 54 ) ), 0, 0.0f, 1.0f );
        }
        if (trc.right - trc.left > 20)
            InflateRect( &trc, -4, 0 );
        {
            int fw = (eff_fw > 0) ? eff_fw : FW_NORMAL;
            BOOL in_horiz_stack = (el->parent && el->parent->kind == UWP_KIND_STACK_PANEL && el->parent->orientation == 1);
            UINT hflag;
            UINT dt;
            if (el->has_text_align)
                hflag = (el->text_align == 0) ? DT_CENTER : (el->text_align == 2 ? DT_RIGHT : DT_LEFT);
            else if (in_horiz_stack)
                hflag = DT_LEFT;
            else if (el->h_align == 1)
                hflag = DT_CENTER;
            else if (el->h_align == 2)
                hflag = DT_RIGHT;
            else if (parent_btn && !is_sec_hdr && el->h_align == 3)
            {
                int hca = parent_btn->has_h_content_align ? parent_btn->h_content_align : 1;
                hflag = (hca == 0) ? DT_LEFT : (hca == 2 ? DT_RIGHT : DT_CENTER);
            }
            else if (anc_h_content_align >= 0 && el->h_align == 3)
                hflag = (anc_h_content_align == 0) ? DT_LEFT : (anc_h_content_align == 2 ? DT_RIGHT : (anc_h_content_align == 1 ? DT_CENTER : DT_LEFT));
            else
                hflag = DT_LEFT;
            if (eff_rtl || (el->parent && uwp_is_scroll_viewer( el->parent ) && el->parent->flow_direction == 2))
            {
                if (hflag == DT_LEFT) hflag = DT_RIGHT;
                else if (hflag == DT_RIGHT) hflag = DT_LEFT;
            }

            if (!parent_btn && (el->text_wrapping >= 2 || wcschr( draw_txt, L'\n' ) != NULL))
            {
                dt = hflag | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL;
                if (el->max_lines > 0)
                {
                    int max_bh = (int)(el->max_lines * fh * 1.32f + 2.0f);
                    if (trc.bottom - trc.top > max_bh)
                        trc.bottom = trc.top + max_bh;
                }
            }
            else
            {
                int box_h = trc.bottom - trc.top;
                if (box_h > 8 && fh > box_h)
                    fh = max( 10, box_h - 1 );
                dt = hflag | DT_VCENTER | DT_SINGLELINE;
            }

            if (is_sec_hdr && anc_btn && anc_btn->is_enabled && (trc.right - trc.left) > 40)
            {
                int chev_x = min( (int)trc.right - 20, (int)trc.left + (int)(wcslen( draw_txt ) * fh * 0.50f) + 6 );
                RECT crc = { chev_x, trc.top + 2, chev_x + 18, trc.bottom };
                RECT lrc = { trc.left, trc.top, trc.right - 18, trc.bottom };
                COLORREF cfg = anc_btn->is_hovered ? tfg : (uwp_is_light_theme( el ) ? RGB( 96, 96, 106 ) : RGB( 185, 185, 198 ));
                WCHAR ff_buf[64], cff_buf[64];
                xaml_gpu_push_text_font( ctx, draw_txt, uwp_resolve_elem_font_family( el, ff_buf, ARRAY_SIZE(ff_buf) ),
                                         fh, fw, dt, &lrc, NULL, tfg, 1.0f );
                uwp_resolve_font_family( L"Segoe MDL2 Assets", cff_buf, ARRAY_SIZE(cff_buf) );
                xaml_gpu_push_text_font( ctx, L"\xE76C", cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                         max( 11, (int)(11 * g_layout_scale + 0.5f) ), FW_NORMAL,
                                         DT_CENTER | DT_VCENTER | DT_SINGLELINE, &crc, NULL, cfg, 1.0f );
            }
            else
            {
                WCHAR ff_buf[64];
                xaml_gpu_push_text_font( ctx, draw_txt, uwp_resolve_elem_font_family( el, ff_buf, ARRAY_SIZE(ff_buf) ),
                                         fh, fw, dt, &trc, NULL, tfg, 1.0f );
            }
        }
    }
    return FALSE;
}

static void paint_gpu_nav_item( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    RECT brc = el->bounds;
    BOOL light = uwp_is_light_theme( el );
    float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
    BOOL is_expanded = (brc.right - brc.left) >= (int)(130 * sc);
    struct uwp_element *nav = el->parent->parent;
    WCHAR label[128] = {0};

    if (uwp_is_nav_separator( el ))
    {
        int ym = (brc.top + brc.bottom) / 2;
        COLORREF sep_cr = light ? RGB( 218, 218, 222 ) : RGB( 62, 62, 68 );
        xaml_gpu_push_rect( (float)(brc.left + 8), (float)ym, (float)(brc.right - 8), (float)(ym + 1), NULL,
                            0.0f, cr_to_argb( sep_cr ), 0, 0.0f, 1.0f );
        return;
    }

    if (uwp_has_text( el ))
        lstrcpynW( label, uwp_elem_text( el ), ARRAY_SIZE(label) );
    if (!label[0] && el->content)
        uwp_combobox_get_item_label( NULL, el->content, label, ARRAY_SIZE(label) );
    if (!label[0] && el->data_context)
        uwp_combobox_get_item_label( NULL, el->data_context, label, ARRAY_SIZE(label) );
    if (!label[0] && uwp_is_nav_settings_item( el ))
        lstrcpynW( label, L"Settings", ARRAY_SIZE(label) );
    if (label[0] && !uwp_has_text( el ))
        uwp_elem_set_text( el, label );

    if (uwp_is_nav_header( el ))
    {
        if (is_expanded && label[0])
        {
            RECT hrc;
            COLORREF hfg = light ? RGB( 96, 96, 102 ) : RGB( 168, 168, 176 );
            SetRect( &hrc, brc.left + (int)(12 * sc), brc.top + (int)(4 * sc), brc.right - (int)(8 * sc), brc.bottom );
            xaml_gpu_push_text( ctx, label, max( 11, (int)(12 * sc + 0.5f) ), FW_SEMIBOLD,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                &hrc, &brc, hfg, 1.0f );
        }
        return;
    }

    if (!is_expanded && uwp_is_nav_settings_item( el ))
        return;

    if (label[0])
    {
        WCHAR glyph[16] = {0};
        WCHAR gfont[64] = {0};
        BOOL any_checked = (nav && uwp_is_nav_view( nav )) ? uwp_paint_nav_tree_has_checked_item( nav, 0 ) : FALSE;
        BOOL is_sel = (el->is_checked == 1);
        COLORREF accent_cr = light ? RGB( 0, 103, 192 ) : RGB( 96, 205, 255 );

        IInspectable *nav_sel = uwp_elem_selected_item( nav );
        if (!is_sel && !any_checked && nav && uwp_is_nav_view( nav ) && nav_sel)
        {
            if ((el->content && nav_sel == el->content) ||
                (el->data_context && nav_sel == el->data_context) ||
                (el->outer && nav_sel == el->outer) ||
                nav_sel == &el->IInspectable_iface)
                is_sel = TRUE;
        }
        if (!is_sel && !any_checked && (!nav || !nav_sel) && uwp_paint_nav_is_first_selectable_item( el ))
            is_sel = TRUE;

        if (el->nav && el->nav->nav_icon)
        {
            struct uwp_element *iel = uwp_elem_from_any( el->nav->nav_icon );
            if (uwp_has_text( iel ))
            {
                lstrcpynW( glyph, uwp_elem_text( iel ), ARRAY_SIZE(glyph) );
                if (uwp_elem_font_family( iel )[0])
                    uwp_resolve_elem_font_family( iel, gfont, ARRAY_SIZE(gfont) );
            }
        }
        if (!glyph[0] && el->dp_cnt > 0 && el->dp_store)
        {
            int di;
            for (di = 0; di < el->dp_cnt; di++)
            {
                if (!wcscmp( xaml_dp_get_name( el->dp_store[di].dp ), L"Icon" ) && el->dp_store[di].value)
                {
                    struct uwp_element *iel = uwp_elem_from_any( el->dp_store[di].value );
                    if (uwp_has_text( iel ))
                    {
                        lstrcpynW( glyph, uwp_elem_text( iel ), ARRAY_SIZE(glyph) );
                        if (uwp_elem_font_family( iel )[0])
                            uwp_resolve_elem_font_family( iel, gfont, ARRAY_SIZE(gfont) );
                    }
                    break;
                }
            }
        }
        if (!glyph[0])
        {
            static const WCHAR *const glyph_props[] = { L"Glyph", L"IconGlyph", L"Icon" };
            IInspectable *vm = (el->content && !uwp_elem_from_any( el->content )) ? el->content : el->data_context;
            if (vm)
                uwp_vm_query_string_prop( vm, glyph_props, ARRAY_SIZE(glyph_props), glyph, ARRAY_SIZE(glyph) );
        }
        if (!glyph[0])
        {
            if (!wcscmp( el->name, L"SettingsItem" ) || !wcscmp( label, L"Settings" ))
                lstrcpynW( glyph, L"\x2699", ARRAY_SIZE(glyph) );
            else if (!wcscmp( label, L"Home" )) lstrcpynW( glyph, L"\x2302", ARRAY_SIZE(glyph) );
            else if (!wcscmp( label, L"Apps" )) lstrcpynW( glyph, L"\x25A6", ARRAY_SIZE(glyph) );
            else if (!wcscmp( label, L"Gaming" )) lstrcpynW( glyph, L"\x2726", ARRAY_SIZE(glyph) );
            else if (!wcscmp( label, L"Themes" )) lstrcpynW( glyph, L"\x25C8", ARRAY_SIZE(glyph) );
            else if (!wcscmp( label, L"Library" )) lstrcpynW( glyph, L"\x25A4", ARRAY_SIZE(glyph) );
            else if (!wcscmp( label, L"Help" )) lstrcpynW( glyph, L"?", ARRAY_SIZE(glyph) );
            else lstrcpynW( glyph, L"\x2022", ARRAY_SIZE(glyph) );
        }
        if (!gfont[0])
        {
            if (glyph[0] >= 0xE000 && glyph[0] <= 0xF8FF)
            {
                WCHAR tmp_ff[64] = {0};
                uwp_resolve_font_family( L"Segoe MDL2 Assets", tmp_ff, ARRAY_SIZE(tmp_ff) );
                lstrcpynW( gfont, tmp_ff[0] ? tmp_ff : L"Segoe MDL2 Assets", ARRAY_SIZE(gfont) );
            }
            else
                lstrcpynW( gfont, L"Segoe UI", ARRAY_SIZE(gfont) );
        }

        if (is_expanded)
        {
            RECT irc, lrc;
            COLORREF fg_cr = is_sel
                ? (light ? RGB( 16, 16, 16 ) : RGB( 255, 255, 255 ))
                : (light ? RGB( 38, 38, 42 ) : RGB( 228, 228, 232 ));
            if (is_sel || el->is_hovered || el->is_pressed)
            {
                COLORREF nbg = light
                    ? (el->is_pressed ? RGB( 220, 220, 226 ) : RGB( 232, 232, 238 ))
                    : (el->is_pressed ? RGB( 58, 58, 64 ) : RGB( 50, 50, 56 ));
                xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                    4.0f, cr_to_argb( nbg ), 0, 0.0f, 1.0f );
            }
            if (is_sel)
            {
                int ym = (brc.top + brc.bottom) / 2;
                xaml_gpu_push_rect( (float)brc.left, (float)(ym - 8), (float)(brc.left + 3), (float)(ym + 8), NULL,
                                    1.5f, cr_to_argb( accent_cr ), 0, 0.0f, 1.0f );
            }
            SetRect( &irc, brc.left + (int)(10 * sc), brc.top, brc.left + (int)(36 * sc), brc.bottom );
            SetRect( &lrc, brc.left + (int)(42 * sc), brc.top, brc.right - (int)(10 * sc), brc.bottom );
            xaml_gpu_push_text_font( ctx, glyph, gfont, max( 13, (int)(15 * sc + 0.5f) ), FW_NORMAL,
                                     DT_CENTER | DT_VCENTER | DT_SINGLELINE, &irc, &brc, fg_cr, 1.0f );
            xaml_gpu_push_text( ctx, label, max( 12, (int)(14 * sc + 0.5f) ),
                                is_sel ? FW_SEMIBOLD : FW_NORMAL,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                &lrc, &brc, fg_cr, 1.0f );
        }
        else
        {
            RECT irc, lrc;
            InflateRect( &brc, -2, -2 );
            if (is_sel || el->is_hovered || el->is_pressed)
            {
                COLORREF nbg = light
                    ? (el->is_pressed ? RGB( 220, 220, 226 ) : RGB( 232, 232, 238 ))
                    : (el->is_pressed ? RGB( 54, 54, 58 ) : RGB( 45, 45, 48 ));
                xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                    6.0f, cr_to_argb( nbg ), 0, 0.0f, 1.0f );
            }
            if (is_sel)
            {
                int ym = (brc.top + brc.bottom) / 2;
                xaml_gpu_push_rect( (float)brc.left, (float)(ym - 10), (float)(brc.left + 3), (float)(ym + 10), NULL,
                                    1.5f, cr_to_argb( accent_cr ), 0, 0.0f, 1.0f );
            }
            SetRect( &irc, brc.left, brc.top + 4, brc.right, brc.top + 28 );
            SetRect( &lrc, brc.left, brc.top + 28, brc.right, brc.bottom - 3 );
            xaml_gpu_push_text_font( ctx, glyph, gfont, 15, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                     &irc, &brc,
                                     is_sel ? accent_cr : (light ? RGB( 60, 60, 66 ) : RGB( 200, 200, 205 )), 1.0f );
            xaml_gpu_push_text( ctx, label, 10, is_sel ? FW_SEMIBOLD : FW_NORMAL,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                &lrc, &brc,
                                is_sel ? (light ? RGB( 16, 16, 16 ) : RGB( 255, 255, 255 ))
                                       : (light ? RGB( 82, 82, 88 ) : RGB( 185, 185, 190 )), 1.0f );
        }
    }
}

static void paint_gpu_content_control_vm( ID3D11DeviceContext *ctx, struct uwp_element *el, COLORREF surface_bg )
{
    WCHAR label[128] = {0};
    if (el->probe_flags & UWP_PF_NO_VM) return;
    if (uwp_has_text( el ))
    {
        lstrcpynW( label, uwp_elem_text( el ), ARRAY_SIZE(label) );
    }
    else if (!uwp_unbox_string( el->content, label, ARRAY_SIZE(label) ))
    {
        /* Resolve ViewModel label via IXamlMember (Title/Label/DisplayName/Text) or IStringable */
        HSTRING hcls = NULL;
        if (SUCCEEDED(IInspectable_GetRuntimeClassName( el->content, &hcls )) && hcls)
        {
            const WCHAR *cls = WindowsGetStringRawBuffer( hcls, NULL );
            if (cls && cls[0])
            {
                static const WCHAR *const prop_names[] = { L"Title", L"Label", L"DisplayName", L"Header", L"Text", L"Name" };
                unsigned int pi;
                for (pi = 0; pi < ARRAY_SIZE(prop_names) && !label[0]; pi++)
                {
                    IInspectable *pval = NULL;
                    if (uwp_get_custom_xaml_member( uwp_get_app_outer(), cls, prop_names[pi], el->content, &pval ) && pval)
                    {
                        uwp_unbox_string( pval, label, ARRAY_SIZE(label) );
                        IInspectable_Release( pval );
                    }
                }
            }
            WindowsDeleteString( hcls );
        }
        if (!label[0])
        {
            /* Try ICustomPropertyProvider ({7c925755-3e48-42b4-8677-76372267033f}) for managed ViewModels */
            void *cpp = NULL;
            if (SUCCEEDED(IInspectable_QueryInterface( el->content, &IID_ICustomPropertyProvider_UWP, &cpp )) && cpp)
            {
                static const WCHAR *const prop_names[] = { L"Title", L"Label", L"DisplayName", L"Header", L"Text", L"Content", L"Name" };
                const struct abi_custom_property_provider *acpp = (const struct abi_custom_property_provider *)cpp;
                unsigned int pi;
                for (pi = 0; pi < ARRAY_SIZE(prop_names) && !label[0]; pi++)
                {
                    HSTRING hp = NULL;
                    void *cp = NULL;
                    if (acpp->lpVtbl->GetCustomProperty && SUCCEEDED(WindowsCreateString( prop_names[pi], wcslen(prop_names[pi]), &hp )) && hp)
                    {
                        if (SUCCEEDED(acpp->lpVtbl->GetCustomProperty( cpp, hp, &cp )) && cp)
                        {
                            const struct abi_custom_property *acp = (const struct abi_custom_property *)cp;
                            IInspectable *pval = NULL;
                            if (acp->lpVtbl->GetValue && SUCCEEDED(acp->lpVtbl->GetValue( cp, el->content, &pval )) && pval)
                            {
                                uwp_unbox_string( pval, label, ARRAY_SIZE(label) );
                                IInspectable_Release( pval );
                            }
                            IUnknown_Release( (IUnknown *)cp );
                        }
                        WindowsDeleteString( hp );
                    }
                }
                IUnknown_Release( (IUnknown *)cpp );
            }
        }
        if (!label[0])
        {
            WCHAR str_buf[128] = {0};
            if (uwp_inspectable_to_string( el->content, str_buf, ARRAY_SIZE(str_buf) ) &&
                !uwp_paint_is_clr_type_name( str_buf ))
                lstrcpynW( label, str_buf, ARRAY_SIZE(label) );
        }
        if (label[0])
            uwp_elem_set_text( el, label );
        else
            el->probe_flags |= UWP_PF_NO_VM;
    }
    if (label[0] && !uwp_is_nav_settings_item( el ))
    {
        RECT brc = el->bounds;
        BOOL is_nav_rail = el->parent &&
            (!wcscmp( el->parent->name, L"MenuItemsHost" ) || !wcscmp( el->parent->name, L"FooterMenuItemsHost" ));
        if (is_nav_rail)
        {
            paint_gpu_nav_item( ctx, el );
            return;
        }
        else
        {
            COLORREF bg = (el->is_pressed || el->is_hovered) ? RGB( 60, 60, 60 ) : RGB( 42, 42, 42 );
            COLORREF fg = uwp_paint_resolve_color( el, L"DefaultTextForegroundThemeBrush", RGB( 240, 240, 240 ), surface_bg );
            InflateRect( &brc, -2, -4 );
            xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                6.0f, cr_to_argb( bg ), 0, 0.0f, 1.0f );
            xaml_gpu_push_text( ctx, label, 11, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                &brc, NULL, fg, 1.0f );
        }
    }
}

static void paint_gpu_vm_card( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    DWORD now_tick = GetTickCount();
    const WCHAR *cur_txt = uwp_elem_text( el );
    BOOL incomplete = !cur_txt[0] || (!wcschr( cur_txt, L'\n' ) && !uwp_elem_image_src( el )[0]);
    BOOL needs_hydrate = !cur_txt[0] ||
        (incomplete && el->hovered_dropdown_idx < 4 &&
         (el->dropdown_scroll_offset == 0 || (int)(now_tick - (DWORD)el->dropdown_scroll_offset) >= 900));
    if (needs_hydrate && !(el->probe_flags & UWP_PF_NO_CARD))
    {
        static const WCHAR *const title_props[] =
            { L"Title", L"ProductTitle", L"DisplayTitle", L"Name", L"Label", L"DisplayName", L"Header", L"Text", L"ShortTitle" };
        static const WCHAR *const cat_props[] =
            { L"FirstCategoryText", L"SubTitle", L"Category", L"SubcategoryName", L"Categories", L"AppExtCategoryNames", L"Subtitle", L"ProductFamilyName", L"ProductType" };
        static const WCHAR *const rating_props[] =
            { L"AverageRating", L"StarRating", L"AverageRatingText", L"FormattedAverage", L"AverageRatingScore", L"Rating" };
        static const WCHAR *const price_props[] =
            { L"DisplayPrice", L"PriceOrStatus", L"ProductStatusOrPriceDisplayString", L"PreTaxDisplayPrice",
              L"BadgeText", L"DisplayMSRP", L"FormattedPrice", L"PriceText", L"SalePrice", L"NarratorPrice",
              L"PriceInfo", L"SkuSummaryList", L"SkuSummaries" };
        static const WCHAR *const fallback_sub_props[] =
            { L"PublisherName", L"Publisher", L"Description" };
        static const WCHAR *const img_props[] =
            { L"Images", L"Icons", L"ImageUrl", L"IconUrl", L"IconUri", L"ImageUri",
              L"LogoUrl", L"ThumbnailUrl", L"ImageLinkUri", L"IconImage", L"LogoImage",
              L"BackgroundImage", L"Icon", L"Image", L"ImageSource", L"Uri" };
        static const WCHAR *const nested_props[] =
            { L"Content", L"CardModel", L"Card", L"Product", L"ViewModel", L"Model", L"Data", L"Item", L"ProductSummary" };
        WCHAR title_buf[128] = {0}, cat_buf[80] = {0}, rating_buf[24] = {0}, price_buf[48] = {0}, img_buf[512] = {0};
        IInspectable *vm0 = (el->content && !uwp_elem_from_any( el->content )) ? el->content : el->data_context;
        IInspectable *vm1 = uwp_paint_vm_unwrap_one( vm0, nested_props, ARRAY_SIZE(nested_props) );
        IInspectable *vm2 = (vm1 && vm1 != vm0) ? uwp_paint_vm_unwrap_one( vm1, nested_props, ARRAY_SIZE(nested_props) ) : NULL;
        IInspectable *vms[3] = { vm1 ? vm1 : vm0, vm2, vm0 };
        unsigned int vi;
        el->dropdown_scroll_offset = (int)now_tick;
        el->hovered_dropdown_idx++;

        if (cur_txt[0] && !wcschr( cur_txt, L'\n' ))
            lstrcpynW( title_buf, cur_txt, ARRAY_SIZE(title_buf) );
        for (vi = 0; vi < ARRAY_SIZE(vms) && !title_buf[0]; vi++)
            if (vms[vi]) uwp_vm_query_string_prop_depth( vms[vi], title_props, ARRAY_SIZE(title_props), title_buf, ARRAY_SIZE(title_buf), 2 );

        if (title_buf[0])
        {
            for (vi = 0; vi < ARRAY_SIZE(vms) && !cat_buf[0]; vi++)
                if (vms[vi]) uwp_vm_query_string_prop_depth( vms[vi], cat_props, ARRAY_SIZE(cat_props), cat_buf, ARRAY_SIZE(cat_buf), 2 );
            for (vi = 0; vi < ARRAY_SIZE(vms) && !rating_buf[0]; vi++)
                if (vms[vi]) uwp_vm_query_string_prop_depth( vms[vi], rating_props, ARRAY_SIZE(rating_props), rating_buf, ARRAY_SIZE(rating_buf), 2 );
            if (rating_buf[0])
            {
                double rv = wcstod( rating_buf, NULL );
                if (rv >= 1.0 && rv <= 5.0)
                    swprintf( rating_buf, ARRAY_SIZE(rating_buf), L"%.1f", rv );
                else
                    rating_buf[0] = 0;
            }
            for (vi = 0; vi < ARRAY_SIZE(vms) && !price_buf[0]; vi++)
                if (vms[vi]) uwp_vm_query_string_prop_depth( vms[vi], price_props, ARRAY_SIZE(price_props), price_buf, ARRAY_SIZE(price_buf), 3 );
            if (!price_buf[0] && (rating_buf[0] || cat_buf[0]))
                lstrcpynW( price_buf, L"Free", ARRAY_SIZE(price_buf) );
            if (!cat_buf[0] && !rating_buf[0] && !price_buf[0])
            {
                for (vi = 0; vi < ARRAY_SIZE(vms) && !cat_buf[0]; vi++)
                    if (vms[vi]) uwp_vm_query_string_prop_depth( vms[vi], fallback_sub_props, ARRAY_SIZE(fallback_sub_props), cat_buf, ARRAY_SIZE(cat_buf), 1 );
            }
            if (cat_buf[0] || rating_buf[0] || price_buf[0])
            {
                WCHAR combined[256];
                swprintf( combined, ARRAY_SIZE(combined), L"%.110s\n%.60s\t%.16s\t%.32s", title_buf, cat_buf, rating_buf, price_buf );
                uwp_elem_set_text( el, combined );
            }
            else
                uwp_elem_set_text( el, title_buf );

            if (!uwp_elem_image_src( el )[0])
            {
                for (vi = 0; vi < ARRAY_SIZE(vms) && !img_buf[0]; vi++)
                    if (vms[vi]) uwp_vm_query_string_prop_depth( vms[vi], img_props, ARRAY_SIZE(img_props), img_buf, ARRAY_SIZE(img_buf), 2 );
                if (img_buf[0])
                {
                    struct uwp_media_ext *m = uwp_elem_ensure_media( el );
                    if (m) lstrcpynW( m->image_src, img_buf, ARRAY_SIZE(m->image_src) );
                }
            }
            if (!uwp_elem_image_src( el )[0] && el->data_context)
            {
                static const WCHAR *const item_img_props[] = { L"Uri", L"Url", L"ImageUrl", L"ImageUri", L"ThumbnailUrl", L"PreviewImageUrl", L"Image", L"Images", L"Source" };
                if (uwp_vm_query_string_prop_depth( el->data_context, item_img_props, ARRAY_SIZE(item_img_props), img_buf, ARRAY_SIZE(img_buf), 2 ) &&
                    img_buf[0])
                {
                    struct uwp_media_ext *m = uwp_elem_ensure_media( el );
                    if (m) lstrcpynW( m->image_src, img_buf, ARRAY_SIZE(m->image_src) );
                }
            }
        }
        else
        {
            el->probe_flags |= UWP_PF_NO_CARD;
        }
        if (vm2) IInspectable_Release( vm2 );
        if (vm1) IInspectable_Release( vm1 );
    }
    if (uwp_has_text( el ))
    {
        RECT brc = el->bounds, trc;
        int text_left;
        BOOL light = uwp_is_light_theme( el );
        const WCHAR *etext = uwp_elem_text( el );
        const WCHAR *nl = wcschr( etext, '\n' );
        WCHAR tline[128] = {0}, cat_str[80] = {0}, rating_str[24] = {0}, price_str[48] = {0};
        size_t tlen = nl ? (size_t)(nl - etext) : wcslen( etext );
        if (tlen >= ARRAY_SIZE(tline)) tlen = ARRAY_SIZE(tline) - 1;
        memcpy( tline, etext, tlen * sizeof(WCHAR) );
        tline[tlen] = 0;
        if (nl)
        {
            const WCHAR *s1 = nl + 1;
            const WCHAR *tab1 = wcschr( s1, '\t' );
            if (tab1)
            {
                const WCHAR *s2 = tab1 + 1;
                const WCHAR *tab2 = wcschr( s2, '\t' );
                size_t l1 = min( (size_t)(tab1 - s1), ARRAY_SIZE(cat_str) - 1 );
                memcpy( cat_str, s1, l1 * sizeof(WCHAR) );
                cat_str[l1] = 0;
                if (tab2)
                {
                    size_t l2 = min( (size_t)(tab2 - s2), ARRAY_SIZE(rating_str) - 1 );
                    memcpy( rating_str, s2, l2 * sizeof(WCHAR) );
                    rating_str[l2] = 0;
                    lstrcpynW( price_str, tab2 + 1, ARRAY_SIZE(price_str) );
                }
                else
                {
                    lstrcpynW( rating_str, s2, ARRAY_SIZE(rating_str) );
                }
            }
            else
            {
                lstrcpynW( cat_str, s1, ARRAY_SIZE(cat_str) );
            }
        }
        InflateRect( &brc, -2, -2 );
        {
            COLORREF cbg = light ? (el->is_hovered ? RGB( 246, 246, 248 ) : RGB( 255, 255, 255 ))
                                 : (el->is_hovered ? RGB( 50, 50, 55 ) : RGB( 39, 39, 43 ));
            COLORREF cbd = light ? RGB( 224, 224, 228 ) : RGB( 60, 60, 66 );
            xaml_gpu_push_rect( (float)brc.left, (float)brc.top, (float)brc.right, (float)brc.bottom, NULL,
                                8.0f, cr_to_argb( cbg ), cr_to_argb( cbd ), 1.0f, 1.0f );
        }
        text_left = brc.left + 14;
        if (uwp_elem_image_src( el )[0])
            uwp_load_image_asset( el );
        if (el->media && el->media->img_pixels && el->media->img_w > 0 && el->media->img_h > 0)
        {
            struct uwp_media_ext *m = el->media;
            if (!nl && el->parent && (el->parent->class_flags & (UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW)))
            {
                RECT irc = brc;
                InflateRect( &irc, -2, -2 );
                xaml_gpu_push_image_uv_rounded( ctx, (UINT64)(ULONG_PTR)el, m->img_pixels, m->img_w, m->img_h,
                                                0, &irc, NULL, &brc, 8.0f, 1.0f );
                tline[0] = 0;
            }
            else
            {
                int bh = brc.bottom - brc.top;
                int isz = min( 64, max( 36, bh - 24 ) );
                int iy = brc.top + (bh - isz) / 2;
                RECT irc = { brc.left + 12, iy, brc.left + 12 + isz, iy + isz };
                xaml_gpu_push_image_uv_rounded( ctx, (UINT64)(ULONG_PTR)el, m->img_pixels, m->img_w, m->img_h,
                                                0, &irc, NULL, &brc, 8.0f, 1.0f );
                text_left = irc.right + 12;
            }
        }
        if (tline[0])
        {
            COLORREF tfg = light ? RGB( 22, 22, 26 ) : RGB( 250, 250, 252 );
            if (wcslen( tline ) > 22)
            {
                SetRect( &trc, text_left, brc.top + 10, brc.right - 12, brc.top + 48 );
                xaml_gpu_push_text( ctx, tline, 13, FW_SEMIBOLD, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS,
                                    &trc, &brc, tfg, 1.0f );
            }
            else
            {
                SetRect( &trc, text_left, brc.top + 12, brc.right - 12, brc.top + 40 );
                xaml_gpu_push_text( ctx, tline, 13, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &trc, &brc, tfg, 1.0f );
            }
        }
        if (nl)
        {
            int meta_right = brc.right - 12;
            int cur_mx = text_left;
            COLORREF sfg = light ? RGB( 92, 92, 100 ) : RGB( 176, 176, 184 );

            if (price_str[0])
            {
                int pw = max( 42, min( 84, (int)wcslen( price_str ) * 7 + 16 ) );
                RECT prc = { brc.right - 10 - pw, brc.bottom - 30, brc.right - 10, brc.bottom - 10 };
                COLORREF pbg = light ? RGB( 236, 236, 240 ) : RGB( 54, 54, 60 );
                COLORREF pbd = light ? RGB( 216, 216, 222 ) : RGB( 72, 72, 80 );
                COLORREF pfg = light ? RGB( 32, 32, 36 ) : RGB( 238, 238, 242 );
                xaml_gpu_push_rect( (float)prc.left, (float)prc.top, (float)prc.right, (float)prc.bottom, &brc,
                                    4.0f, cr_to_argb( pbg ), cr_to_argb( pbd ), 1.0f, 1.0f );
                xaml_gpu_push_text( ctx, price_str, 11, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &prc, &brc, pfg, 1.0f );
                meta_right = prc.left - 6;
            }
            if (rating_str[0] && cur_mx + 34 <= meta_right)
            {
                RECT rrc = { cur_mx, brc.bottom - 31, cur_mx + 22, brc.bottom - 9 };
                RECT strc = { cur_mx + 20, brc.bottom - 31, cur_mx + 34, brc.bottom - 9 };
                COLORREF rfg = light ? RGB( 72, 72, 80 ) : RGB( 205, 205, 212 );
                COLORREF star_cr = light ? RGB( 196, 132, 0 ) : RGB( 255, 200, 61 );
                WCHAR sff[64] = {0};
                xaml_gpu_push_text( ctx, rating_str, 11, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                                    &rrc, &brc, rfg, 1.0f );
                uwp_resolve_font_family( L"Segoe MDL2 Assets", sff, ARRAY_SIZE(sff) );
                xaml_gpu_push_text_font( ctx, L"\xE735", sff[0] ? sff : L"Segoe MDL2 Assets",
                                         10, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                         &strc, &brc, star_cr, 1.0f );
                cur_mx += 36;
            }
            if (cat_str[0] && cur_mx + 20 < meta_right)
            {
                WCHAR cat_disp[96] = {0};
                RECT src = { cur_mx, brc.bottom - 31, meta_right, brc.bottom - 9 };
                if (rating_str[0])
                    swprintf( cat_disp, ARRAY_SIZE(cat_disp), L"\x00B7 %.80s", cat_str );
                else
                    lstrcpynW( cat_disp, cat_str, ARRAY_SIZE(cat_disp) );
                xaml_gpu_push_text( ctx, cat_disp, 11, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &src, &brc, sfg, 1.0f );
            }
        }
    }
}

static int g_paint_win_w = 0;
static int g_paint_win_h = 0;

static void uwp_gpu_paint_element_inner( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    COLORREF surface_bg = g_paint_page_bg;
    BOOL out_of_view = FALSE;
    TRACE( "paint-enter %s (%s) bounds=%d (%ld,%ld-%ld,%ld) bg=%d:%08x vis=%u kids=%p content=%p\n",
           debugstr_w(el ? el->class_name : L"?"), debugstr_w(el ? el->name : L"?"),
           el ? el->has_bounds : 0,
           el ? el->bounds.left : 0, el ? el->bounds.top : 0,
           el ? el->bounds.right : 0, el ? el->bounds.bottom : 0,
           el ? el->has_bg : 0, el ? el->bg_argb : 0,
           el ? el->visibility : 0xffff,
           (void *)(el ? el->children_coll : NULL), (void *)(el ? el->content : NULL) );
    if (!el || !el->has_bounds || el->visibility != 0 || el->opacity <= 0.005) return;
    if ((el->class_flags & UWP_CF_INFO_BADGE) && !uwp_has_text( el )) return;
    if (UWP_EV_CNT( el, evc_reg ) > 0 && !el->evc_fired)
        uwp_fire_evc_if_needed( el );
    if (el->items_source && !uwp_panel_has_concrete_child( el ))
    {
        uwp_items_ctrl_materialize_source( el, el->items_source );
        if (el->children_coll)
        {
            g_layout_dirty = TRUE;
            uwp_invalidate_window();
        }
    }

    if (g_paint_win_w > 0 && g_paint_win_h > 0 &&
        el->bounds.right > el->bounds.left && el->bounds.bottom > el->bounds.top)
    {
        BOOL has_clip = FALSE;
        RECT vis_rc = { 0, 0, g_paint_win_w, g_paint_win_h }, clip_rc = { 0 };
        xaml_gpu_get_clip( &has_clip, &clip_rc );
        if (has_clip)
        {
            if (clip_rc.left > vis_rc.left) vis_rc.left = clip_rc.left;
            if (clip_rc.top > vis_rc.top) vis_rc.top = clip_rc.top;
            if (clip_rc.right < vis_rc.right) vis_rc.right = clip_rc.right;
            if (clip_rc.bottom < vis_rc.bottom) vis_rc.bottom = clip_rc.bottom;
        }
        if (el->bounds.right <= vis_rc.left || el->bounds.left >= vis_rc.right ||
            el->bounds.bottom <= vis_rc.top || el->bounds.top >= vis_rc.bottom)
        {
            out_of_view = TRUE;
            /* Leaf widgets, clipping ScrollViewers, and self-contained cards/buttons skip recursion too. */
            if ((!el->content && !el->children_coll) ||
                uwp_is_scroll_viewer( el ) ||
                el->kind == UWP_KIND_IMAGE ||
                el->kind == UWP_KIND_BUTTON ||
                el->kind == UWP_KIND_TOGGLE_BUTTON ||
                el->kind == UWP_KIND_TEXT_BLOCK ||
                el->kind == UWP_KIND_PROGRESS_RING ||
                uwp_is_combobox( el ) ||
                uwp_is_calendar_date_picker( el ) ||
                uwp_is_expander( el ) ||
                uwp_is_font_icon( el ) ||
                uwp_is_text_input( el ) ||
                uwp_is_card( el ) ||
                (el->class_flags & (UWP_CF_MEDIA_PLAYER | UWP_CF_PROGRESS_BAR)) != 0)
                return;
        }
    }

    if (out_of_view) goto recurse_children;

    if (el->kind == UWP_KIND_IMAGE)
    {
        if (el->media && el->media->img_pixels) uwp_on_image_loaded( el );
        paint_gpu_image( ctx, el );
        return;
    }

    if (el->class_flags & UWP_CF_MEDIA_PLAYER)
    {
        paint_gpu_media( ctx, el );
        return;
    }

    if (uwp_is_combobox( el ))
    {
        paint_gpu_combobox( ctx, el, surface_bg );
        return;
    }

    if (uwp_is_calendar_date_picker( el ))
    {
        float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        BOOL light = uwp_is_light_theme( el );
        RECT rc = el->bounds;
        RECT box_rc = rc;
        WCHAR date_buf[96] = {0};
        WCHAR cff_buf[64];
        UINT32 bg_c = light ? 0xf2ffffffu : 0x10ffffffu;
        UINT32 bd_c = el->is_dropdown_open
            ? cr_to_argb( uwp_paint_resolve_color( el, L"SystemAccentColor", RGB( 0, 120, 212 ), surface_bg ) )
            : (light ? 0x26000000u : 0x26ffffffu);
        COLORREF fg_c = light ? RGB( 28, 28, 28 ) : RGB( 245, 245, 245 );
        int fh = max( 12, (int)(14 * sc + 0.5f) );

        if (el->nav && el->nav->cdp_header[0])
        {
            RECT hrc = { rc.left, rc.top, rc.right, rc.top + (int)(22 * sc) };
            xaml_gpu_push_text( ctx, el->nav->cdp_header, max( 12, (int)(13 * sc + 0.5f) ), FW_NORMAL,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE, &hrc, NULL, fg_c, 1.0f );
            box_rc.top = min( rc.bottom - (int)(24 * sc), rc.top + (int)(24 * sc) );
        }
        xaml_gpu_push_rect( (float)box_rc.left, (float)box_rc.top, (float)box_rc.right, (float)box_rc.bottom,
                            NULL, 4.0f * sc, bg_c, bd_c, 1.0f, 1.0f );
        uwp_cdp_format_date( el, date_buf, ARRAY_SIZE(date_buf) );
        {
            RECT trc = { box_rc.left + (int)(10 * sc), box_rc.top, max( box_rc.left + (int)(10 * sc), box_rc.right - (int)(32 * sc) ), box_rc.bottom };
            xaml_gpu_push_text( ctx, date_buf, fh, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                &trc, NULL, fg_c, 1.0f );
        }
        {
            RECT irc = { max( box_rc.left, box_rc.right - (int)(30 * sc) ), box_rc.top, box_rc.right - (int)(6 * sc), box_rc.bottom };
            uwp_resolve_font_family( L"Segoe MDL2 Assets", cff_buf, ARRAY_SIZE(cff_buf) );
            xaml_gpu_push_text_font( ctx, L"\xE787", cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                     max( 12, (int)(14 * sc + 0.5f) ), FW_NORMAL,
                                     DT_CENTER | DT_VCENTER | DT_SINGLELINE, &irc, NULL, fg_c, 1.0f );
        }
        return;
    }

    if (uwp_is_expander( el ))
    {
        float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
        BOOL light = uwp_is_light_theme( el );
        RECT rc = el->bounds;
        int hdr_h = (int)(64 * sc + 0.5f);
        struct uwp_element *he = el->pane ? uwp_elem_from_any( el->pane ) : NULL;
        struct uwp_element *be = el->content ? uwp_elem_from_any( el->content ) : NULL;
        UINT32 card_bg = light ? 0xb3ffffffu : 0x0dffffffu;
        UINT32 card_bd = light ? 0x18000000u : 0x18ffffffu;
        COLORREF fg_c = light ? RGB( 28, 28, 28 ) : RGB( 245, 245, 245 );
        WCHAR cff_buf[64];

        if (he && he != el && he->has_bounds && he->bounds.bottom + (int)(10 * sc) - rc.top > hdr_h)
            hdr_h = he->bounds.bottom + (int)(10 * sc) - rc.top;
        if (hdr_h > rc.bottom - rc.top) hdr_h = rc.bottom - rc.top;

        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom,
                            NULL, 4.0f * sc, card_bg, card_bd, 1.0f, 1.0f );
        if (he && he != el)
            uwp_gpu_paint_element( ctx, he );
        else if (!uwp_has_text( el ) && be && be != el)
        {
            uwp_gpu_paint_element( ctx, be );
            return;
        }
        else if (uwp_has_text( el ))
        {
            RECT trc = { rc.left + (int)(16 * sc), rc.top, rc.right - (int)(48 * sc), rc.top + hdr_h };
            xaml_gpu_push_text( ctx, uwp_elem_text( el ), max( 13, (int)(14 * sc + 0.5f) ), FW_NORMAL,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE, &trc, NULL, fg_c, 1.0f );
        }
        {
            RECT crc = { rc.right - (int)(40 * sc), rc.top, rc.right - (int)(12 * sc), rc.top + hdr_h };
            uwp_resolve_font_family( L"Segoe MDL2 Assets", cff_buf, ARRAY_SIZE(cff_buf) );
            xaml_gpu_push_text_font( ctx, el->is_open ? L"\xE70E" : L"\xE70D",
                                     cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                     max( 11, (int)(12 * sc + 0.5f) ), FW_NORMAL,
                                     DT_CENTER | DT_VCENTER | DT_SINGLELINE, &crc, NULL, fg_c, 1.0f );
        }
        if (el->is_open && be && be != el)
        {
            xaml_gpu_push_rect( (float)rc.left, (float)(rc.top + hdr_h), (float)rc.right, (float)(rc.top + hdr_h + 1),
                                NULL, 0.0f, card_bd, 0, 0.0f, 1.0f );
            uwp_gpu_paint_element( ctx, be );
        }
        return;
    }

    if (el->fill_brush || el->ecp_visual || (el->class_flags & UWP_CF_RECTANGLE))
        paint_gpu_shape( ctx, el );

    if (uwp_is_font_icon( el ) && uwp_has_text( el ))
    {
        paint_gpu_font_icon( ctx, el, surface_bg );
        return;
    }

    if (el->kind == UWP_KIND_PROGRESS_RING)
    {
        paint_gpu_progress_ring( el, surface_bg );
        return;
    }

    if (el->class_flags & UWP_CF_PROGRESS_BAR)
    {
        paint_gpu_progress_bar( el, surface_bg );
        return;
    }

    if (el->class_flags & UWP_CF_APP_TITLE_BAR)
    {
        if (uwp_has_text( el ))
        {
            RECT rc = el->bounds;
            COLORREF tfg = uwp_paint_resolve_color( el, L"TextFillColorPrimaryBrush", RGB( 255, 255, 255 ), surface_bg );
            RECT trc = { rc.left + 12, rc.top, rc.left + 200, rc.bottom };
            xaml_gpu_push_text( ctx, uwp_elem_text( el ), 13, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                                &trc, NULL, tfg, 1.0f );
        }
    }
    else if (uwp_is_text_input( el ))
    {
        paint_gpu_searchbox( ctx, el, surface_bg );
        return;
    }
    else if (uwp_is_teaching_tip( el ) && el->is_open)
    {
        RECT rc = el->bounds;
        COLORREF tbg, tbd;
        uwp_paint_resolve_teaching_tip_colors( el, surface_bg, &tbg, &tbd );
        xaml_gpu_push_rect( (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom, NULL,
                            4.0f, cr_to_argb( tbg ), cr_to_argb( tbd ), 1.0f, 1.0f );
    }
    else if (!uwp_is_card( el ) && el->kind != UWP_KIND_BUTTON && el->kind != UWP_KIND_TOGGLE_BUTTON && el->kind != UWP_KIND_TEXT_BLOCK)
    {
        if (paint_gpu_container( ctx, el, surface_bg ))
            return;
    }

    if ((el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TOGGLE_BUTTON) &&
        !uwp_is_card( el ) &&
        !(el->kind == UWP_KIND_TOGGLE_BUTTON && (el->class_flags & UWP_CF_RADIO_BUTTON) && el->children_coll))
    {
        paint_gpu_button( ctx, el, surface_bg );
        return;
    }

    if (el->kind == UWP_KIND_TEXT_BLOCK)
    {
        if (paint_gpu_text( ctx, el, surface_bg ))
            return;
    }

    if (el->parent &&
        (!wcscmp( el->parent->name, L"MenuItemsHost" ) || !wcscmp( el->parent->name, L"FooterMenuItemsHost" )))
    {
        paint_gpu_nav_item( ctx, el );
        return;
    }

recurse_children:
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el)
            uwp_gpu_paint_element( ctx, ch );
        else if (!out_of_view && el->kind == UWP_KIND_CONTENT_CONTROL && !uwp_is_card( el ) &&
                 (!el->children_coll || CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface )->count == 0))
            paint_gpu_content_control_vm( ctx, el, surface_bg );
    }
    if (!out_of_view && !el->children_coll && (el->data_context || (el->content && !uwp_elem_from_any( el->content ))) &&
        !(el->content && uwp_elem_from_any( el->content )) &&
        (uwp_is_card( el ) || el->kind != UWP_KIND_CONTENT_CONTROL) &&
        (el->bounds.right - el->bounds.left) >= 140 && (el->bounds.bottom - el->bounds.top) >= 56)
    {
        paint_gpu_vm_card( ctx, el );
    }
    if (!out_of_view && uwp_nav_is_overlay_open( el ) && el->nav && el->nav->nav_pane_bounds.right > el->nav->nav_pane_bounds.left)
    {
        RECT prc = el->nav->nav_pane_bounds;
        BOOL light = uwp_is_light_theme( el );
        UINT32 pane_bg = light ? 0xfff3f3f3u : 0xff27272bu;
        UINT32 pane_bd = light ? 0xffd4d4d8u : 0xff44444cu;
        xaml_gpu_push_rect( (float)prc.right, (float)prc.top, (float)(prc.right + 8), (float)prc.bottom, NULL,
                            0.0f, 0x38000000u, 0, 0.0f, 1.0f );
        xaml_gpu_push_rect( (float)prc.left, (float)prc.top, (float)prc.right, (float)prc.bottom, NULL,
                            0.0f, pane_bg, pane_bd, 1.0f, 1.0f );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        unsigned int k;
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_gpu_paint_element( ctx, ch );
        }
    }
}

static void uwp_gpu_paint_element( ID3D11DeviceContext *ctx, struct uwp_element *el )
{
    BOOL pushed_clip = FALSE, prev_has_clip = FALSE;
    RECT prev_clip = { 0 };

    if (!el || g_paint_depth >= 48) return;
    if (el->has_bounds && uwp_is_scroll_viewer( el ) &&
        el->bounds.right > el->bounds.left && el->bounds.bottom > el->bounds.top)
    {
        RECT new_clip = el->bounds;
        xaml_gpu_get_clip( &prev_has_clip, &prev_clip );
        if (prev_has_clip)
        {
            if (new_clip.left < prev_clip.left) new_clip.left = prev_clip.left;
            if (new_clip.top < prev_clip.top) new_clip.top = prev_clip.top;
            if (new_clip.right > prev_clip.right) new_clip.right = prev_clip.right;
            if (new_clip.bottom > prev_clip.bottom) new_clip.bottom = prev_clip.bottom;
        }
        xaml_gpu_set_clip( &new_clip );
        pushed_clip = TRUE;
    }
    g_paint_ancestors[g_paint_depth++] = el;
    uwp_gpu_paint_element_inner( ctx, el );
    g_paint_depth--;
    if (pushed_clip)
    {
        int vp_h = el->bounds.bottom - el->bounds.top;
        if (el->scroll_extent_h > vp_h && vp_h > 24)
        {
            int track_h = vp_h - 8;
            int thumb_h = max( 24, (int)((INT64)track_h * vp_h / el->scroll_extent_h) );
            int max_scroll = el->scroll_extent_h - vp_h;
            int clamped_y = min( max( 0, el->scroll_y ), max_scroll );
            int thumb_y = el->bounds.top + 4 + (max_scroll > 0 ? (int)((INT64)(track_h - thumb_h) * clamped_y / max_scroll) : 0);
            BOOL light = uwp_is_light_theme( el );
            UINT32 thumb_argb = light ? 0x70606060u : 0x70a0a0a8u;
            xaml_gpu_push_rect( (float)(el->bounds.right - 7), (float)thumb_y,
                                (float)(el->bounds.right - 3), (float)(thumb_y + thumb_h),
                                &el->bounds, 2.0f, thumb_argb, 0, 0.0f, 1.0f );
        }
        xaml_gpu_set_clip( prev_has_clip ? &prev_clip : NULL );
    }
}

static void uwp_gpu_paint_cdp_popup( ID3D11DeviceContext *ctx, int width, int height )
{
    static const WCHAR * const months[12] = {
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December"
    };
    static const WCHAR * const dows[7] = { L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa" };
    struct uwp_element *cdp = g_open_cdp;
    struct uwp_nav_ext *ne;
    RECT prc;
    float sc = (g_layout_scale > 0.25f) ? g_layout_scale : 1.0f;
    BOOL light;
    UINT32 bg_c, bd_c, acc_c;
    COLORREF fg_c, dim_c, acc_fg;
    int vy, vm, sy = 0, sm = 0, sd = 0, fdow, dim, row, col, cell_w, cell_h, grid_top, hdr_h;
    WCHAR hdr_buf[64], cff_buf[64];

    if (!cdp || !cdp->is_dropdown_open || !cdp->has_bounds || cdp->visibility != 0) return;
    if (!uwp_cdp_compute_popup_rect( cdp, width, height, &prc )) return;
    ne = uwp_elem_ensure_nav( cdp );
    if (!ne) return;

    if (ne->cdp_has_date)
        uwp_cdp_ticks_to_ymd( ne->cdp_date_ticks, &sy, &sm, &sd );
    vy = (ne->cdp_view_year > 0) ? ne->cdp_view_year : (sy > 0 ? sy : 2026);
    vm = (ne->cdp_view_month >= 1 && ne->cdp_view_month <= 12) ? ne->cdp_view_month : (sm > 0 ? sm : 11);

    light = uwp_is_light_theme( cdp );
    bg_c = light ? 0xfff9f9f9u : 0xff2c2c2cu;
    bd_c = light ? 0x33000000u : 0x33ffffffu;
    acc_c = cr_to_argb( uwp_paint_resolve_color( cdp, L"SystemAccentColor", RGB( 0, 120, 212 ),
                                                 light ? RGB( 249, 249, 249 ) : RGB( 44, 44, 44 ) ) );
    fg_c = light ? RGB( 24, 24, 24 ) : RGB( 248, 248, 248 );
    dim_c = light ? RGB( 130, 130, 130 ) : RGB( 150, 150, 150 );
    acc_fg = RGB( 255, 255, 255 );

    xaml_gpu_push_rect( (float)prc.left, (float)prc.top, (float)prc.right, (float)prc.bottom,
                        NULL, 6.0f * sc, bg_c, bd_c, 1.0f, 1.0f );

    hdr_h = (int)(36 * sc + 0.5f);
    swprintf( hdr_buf, ARRAY_SIZE(hdr_buf), L"%s %d", months[vm - 1], vy );
    {
        RECT mrc = { prc.left + (int)(12 * sc), prc.top + (int)(4 * sc), prc.right - (int)(68 * sc), prc.top + hdr_h };
        xaml_gpu_push_text( ctx, hdr_buf, max( 12, (int)(14 * sc + 0.5f) ), FW_SEMIBOLD,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE, &mrc, NULL, fg_c, 1.0f );
    }
    uwp_resolve_font_family( L"Segoe MDL2 Assets", cff_buf, ARRAY_SIZE(cff_buf) );
    {
        RECT prev_rc = { prc.right - (int)(64 * sc), prc.top + (int)(4 * sc), prc.right - (int)(34 * sc), prc.top + hdr_h };
        RECT next_rc = { prc.right - (int)(34 * sc), prc.top + (int)(4 * sc), prc.right - (int)(4 * sc), prc.top + hdr_h };
        xaml_gpu_push_text_font( ctx, L"\xE76B", cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                 max( 11, (int)(12 * sc + 0.5f) ), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                 &prev_rc, NULL, fg_c, 1.0f );
        xaml_gpu_push_text_font( ctx, L"\xE76C", cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                 max( 11, (int)(12 * sc + 0.5f) ), FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                                 &next_rc, NULL, fg_c, 1.0f );
    }

    cell_w = (prc.right - prc.left - (int)(16 * sc)) / 7;
    cell_h = (int)(34 * sc + 0.5f);
    {
        int dow_top = prc.top + hdr_h + (int)(4 * sc);
        for (col = 0; col < 7; col++)
        {
            RECT drc = { prc.left + (int)(8 * sc) + col * cell_w, dow_top,
                         prc.left + (int)(8 * sc) + (col + 1) * cell_w, dow_top + (int)(24 * sc) };
            xaml_gpu_push_text( ctx, dows[col], max( 11, (int)(12 * sc + 0.5f) ), FW_SEMIBOLD,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE, &drc, NULL, dim_c, 1.0f );
        }
        grid_top = dow_top + (int)(26 * sc);
    }

    fdow = uwp_cdp_first_dow( vy, vm );
    dim = uwp_cdp_days_in_month( vy, vm );
    for (row = 0; row < 6; row++)
    {
        for (col = 0; col < 7; col++)
        {
            int day = row * 7 + col - fdow + 1;
            RECT crc;
            WCHAR dbuf[8];
            BOOL is_sel;
            if (day < 1 || day > dim) continue;
            SetRect( &crc, prc.left + (int)(8 * sc) + col * cell_w, grid_top + row * cell_h,
                     prc.left + (int)(8 * sc) + (col + 1) * cell_w, grid_top + (row + 1) * cell_h );
            is_sel = (ne->cdp_has_date && vy == sy && vm == sm && day == sd);
            if (is_sel)
                xaml_gpu_push_rect( (float)(crc.left + 2), (float)(crc.top + 2), (float)(crc.right - 2), (float)(crc.bottom - 2),
                                    NULL, (float)(min( cell_w, cell_h ) / 2), acc_c, 0, 0.0f, 1.0f );
            swprintf( dbuf, ARRAY_SIZE(dbuf), L"%d", day );
            xaml_gpu_push_text( ctx, dbuf, max( 11, (int)(13 * sc + 0.5f) ), is_sel ? FW_SEMIBOLD : FW_NORMAL,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE, &crc, NULL, is_sel ? acc_fg : fg_c, 1.0f );
        }
    }
}

static void uwp_gpu_paint_combobox_popup( ID3D11DeviceContext *ctx, int width, int height )
{
    struct uwp_element *cb = g_open_combobox;
    IInspectable *items[256];
    int count, i, item_h = 32, vis_cnt = 0, pad_y = 4;
    RECT prc;
    BOOL light;
    UINT32 bg_argb, bd_argb, sel_argb, hov_argb;
    COLORREF fg_cr, accent_cr;
    int fh;

    if (!cb || !cb->is_dropdown_open || !cb->has_bounds || cb->visibility != 0) return;
    count = uwp_selector_get_items( cb, items, ARRAY_SIZE(items) );
    if (count <= 0) return;
    if (!uwp_combobox_compute_popup_rect( cb, count, width, height, &prc, &item_h, &vis_cnt, &pad_y ))
    {
        for (i = 0; i < count; i++) IInspectable_Release( items[i] );
        return;
    }

    light = uwp_is_light_theme( cb );
    bg_argb = light ? 0xfff9f9f9u : 0xff2c2c30u;
    bd_argb = light ? 0xffceced4u : 0xff52525au;
    sel_argb = light ? 0xffe6e6ecu : 0xff3b3b42u;
    hov_argb = light ? 0xffededf2u : 0xff35353cu;
    fg_cr = light ? RGB( 24, 24, 28 ) : RGB( 245, 245, 248 );
    accent_cr = light ? RGB( 0, 103, 192 ) : RGB( 96, 205, 255 );
    fh = max( (int)(12 * g_layout_scale + 0.5f),
              min( (int)(16 * g_layout_scale + 0.5f),
                   (int)((cb->has_font_size ? cb->font_size : 14.0) * g_layout_scale + 0.5f) ) );

    /* Subtle drop shadow + rounded themed popup surface */
    xaml_gpu_push_rect( (float)(prc.left - 2), (float)(prc.top + 2), (float)(prc.right + 2), (float)(prc.bottom + 4),
                        NULL, 8.0f, 0x48000000u, 0, 0.0f, 1.0f );
    xaml_gpu_push_rect( (float)prc.left, (float)prc.top, (float)prc.right, (float)prc.bottom,
                        NULL, 6.0f, bg_argb, bd_argb, 1.0f, 1.0f );

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
            xaml_gpu_push_rect( (float)rrc.left, (float)rrc.top, (float)rrc.right, (float)rrc.bottom,
                                &prc, 4.0f, is_sel ? sel_argb : hov_argb, 0, 0.0f, 1.0f );
        if (is_sel)
        {
            int ym = (rrc.top + rrc.bottom) / 2;
            xaml_gpu_push_rect( (float)rrc.left, (float)(ym - 8), (float)(rrc.left + 3), (float)(ym + 8),
                                &prc, 1.5f, cr_to_argb( accent_cr ), 0, 0.0f, 1.0f );
        }
        uwp_combobox_get_item_label( cb, items[idx], label, ARRAY_SIZE(label) );
        if (label[0])
        {
            SetRect( &trc, rrc.left + 12, rrc.top, rrc.right - 10, rrc.bottom );
            xaml_gpu_push_text( ctx, label, fh, is_sel ? FW_SEMIBOLD : FW_NORMAL,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                &trc, &prc, fg_cr, 1.0f );
        }
    }

    if (count > vis_cnt)
    {
        int track_h = max( 16, (prc.bottom - prc.top) - pad_y * 2 );
        int thumb_h = max( 18, (track_h * vis_cnt) / count );
        int max_scroll = max( 1, count - vis_cnt );
        int thumb_y = prc.top + pad_y + ((track_h - thumb_h) * cb->dropdown_scroll_offset) / max_scroll;
        xaml_gpu_push_rect( (float)(prc.right - 5), (float)thumb_y, (float)(prc.right - 2), (float)(thumb_y + thumb_h),
                            &prc, 1.5f, light ? 0x66000000u : 0x66ffffffu, 0, 0.0f, 1.0f );
    }

    for (i = 0; i < count; i++) IInspectable_Release( items[i] );
}

static void uwp_gpu_paint_asb_popup( ID3D11DeviceContext *ctx, int width, int height )
{
    struct uwp_element *asb = g_focused_textbox;
    IInspectable *items[32];
    int count, i, item_h = 38, vis_cnt = 0, pad_y = 6;
    RECT prc;
    BOOL light;
    UINT32 bg_argb, bd_argb, hov_argb;
    COLORREF fg_cr, sub_cr;
    int fh, sfh, icon_fh;

    if (!uwp_is_autosuggest( asb )) return;
    if (!asb->has_bounds || asb->visibility != 0) return;
    if (!asb->is_open) return;
    if (asb->items_source && !uwp_panel_has_concrete_child( asb ))
        uwp_items_ctrl_materialize_source( asb, asb->items_source );
    count = uwp_asb_get_suggestions( asb, items, ARRAY_SIZE(items) );
    if (count <= 0) return;
    if (!uwp_asb_compute_popup_rect( asb, count, width, height, &prc, &item_h, &vis_cnt, &pad_y ))
    {
        for (i = 0; i < count; i++) IInspectable_Release( items[i] );
        return;
    }

    light = uwp_is_light_theme( asb );
    bg_argb = light ? 0xfffcfcfcu : 0xff2c2c30u;
    bd_argb = light ? 0xffceced4u : 0xff52525au;
    hov_argb = light ? 0xffededf2u : 0xff383840u;
    fg_cr = light ? RGB( 24, 24, 28 ) : RGB( 245, 245, 248 );
    sub_cr = light ? RGB( 96, 96, 104 ) : RGB( 172, 172, 180 );
    fh = max( 12, (int)(14.0f * g_layout_scale + 0.5f) );
    sfh = max( 11, (int)(12.0f * g_layout_scale + 0.5f) );
    icon_fh = max( 12, (int)(14.0f * g_layout_scale + 0.5f) );

    xaml_gpu_push_rect( (float)(prc.left - 2), (float)(prc.top + 2), (float)(prc.right + 2), (float)(prc.bottom + 4),
                        NULL, 8.0f, 0x48000000u, 0, 0.0f, 1.0f );
    xaml_gpu_push_rect( (float)prc.left, (float)prc.top, (float)prc.right, (float)prc.bottom,
                        NULL, 6.0f, bg_argb, bd_argb, 1.0f, 1.0f );

    for (i = 0; i < vis_cnt && (asb->dropdown_scroll_offset + i) < count; i++)
    {
        int idx = asb->dropdown_scroll_offset + i;
        RECT rrc, irc, trc;
        WCHAR prim[128] = {0}, sec[128] = {0};
        BOOL is_hov = (idx == asb->hovered_dropdown_idx);

        SetRect( &rrc, prc.left + 4, prc.top + pad_y + i * item_h, prc.right - 4, prc.top + pad_y + (i + 1) * item_h );
        if (is_hov)
            xaml_gpu_push_rect( (float)rrc.left, (float)rrc.top, (float)rrc.right, (float)rrc.bottom,
                                &prc, 4.0f, hov_argb, 0, 0.0f, 1.0f );
        uwp_asb_get_item_label( asb, idx, items[idx], prim, ARRAY_SIZE(prim), sec, ARRAY_SIZE(sec) );
        SetRect( &irc, rrc.left + 8, rrc.top, rrc.left + 32, rrc.bottom );
        xaml_gpu_push_text_font( ctx, L"\xE721", L"Segoe MDL2 Assets", icon_fh, FW_NORMAL,
                                 DT_CENTER | DT_VCENTER | DT_SINGLELINE, &irc, &prc, sub_cr, 1.0f );
        if (prim[0])
        {
            if (sec[0])
            {
                RECT src_rc;
                int mid_y = (rrc.top + rrc.bottom) / 2;
                SetRect( &trc, rrc.left + 36, rrc.top + 2, rrc.right - 10, mid_y + 2 );
                SetRect( &src_rc, rrc.left + 36, mid_y, rrc.right - 10, rrc.bottom - 2 );
                xaml_gpu_push_text( ctx, prim, fh, FW_SEMIBOLD,
                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &trc, &prc, fg_cr, 1.0f );
                xaml_gpu_push_text( ctx, sec, sfh, FW_NORMAL,
                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &src_rc, &prc, sub_cr, 1.0f );
            }
            else
            {
                SetRect( &trc, rrc.left + 36, rrc.top, rrc.right - 10, rrc.bottom );
                xaml_gpu_push_text( ctx, prim, fh, FW_NORMAL,
                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &trc, &prc, fg_cr, 1.0f );
            }
        }
    }

    for (i = 0; i < count; i++) IInspectable_Release( items[i] );
}

static void uwp_gpu_paint_open_popups( ID3D11DeviceContext *ctx, int width, int height )
{
    struct uwp_element *pops[16];
    int pcnt = uwp_get_open_popups( pops, ARRAY_SIZE(pops) ), pi;
    for (pi = 0; pi < pcnt; pi++)
    {
        struct uwp_element *pop = pops[pi];
        struct uwp_element *ch = uwp_elem_from_any( pop->content );
        RECT prc;
        BOOL light;
        UINT32 bg_argb, bd_argb;
        if (!ch || !pop->has_bounds) continue;
        prc = pop->bounds;
        light = uwp_is_light_theme( ch );
        bg_argb = light ? 0xfffcfcfcu : 0xff2b2b30u;
        bd_argb = light ? 0xffceced4u : 0xff52525au;
        xaml_gpu_push_rect( 0.0f, 0.0f, (float)width, (float)height, NULL, 0.0f, 0x66000000u, 0, 0.0f, 1.0f );
        xaml_gpu_push_rect( (float)(prc.left - 3), (float)(prc.top + 3), (float)(prc.right + 3), (float)(prc.bottom + 5),
                            NULL, 10.0f, 0x55000000u, 0, 0.0f, 1.0f );
        xaml_gpu_push_rect( (float)prc.left, (float)prc.top, (float)prc.right, (float)prc.bottom,
                            NULL, 8.0f, bg_argb, bd_argb, 1.0f, 1.0f );
        uwp_gpu_paint_element( ctx, ch );
    }
}

static void uwp_gpu_paint_flyout_popup( ID3D11DeviceContext *ctx, int width, int height )
{
    struct uwp_element *owner = g_popup_owner_el;
    struct uwp_element *anchor;
    struct uwp_element *flyout;
    struct uwp_vec_view *children;
    struct uwp_element *items[32];
    int item_cnt = 0, i, cur_y = 6, max_w = 180;
    RECT orc, prc;
    BOOL light;
    UINT32 bg_argb, bd_argb, hov_argb, sep_argb;
    COLORREF fg_cr, accent_cr;
    int fh;

    if (!owner) return;
    if (g_popup_hwnd && IsWindow( g_popup_hwnd )) return;
    anchor = owner->has_bounds ? owner : owner->parent;
    if (!anchor || !anchor->has_bounds || anchor->visibility != 0) return;
    flyout = owner->flyout ? uwp_elem_from_any( owner->flyout ) : owner;
    if (!flyout || !flyout->children_coll) return;
    children = CONTAINING_RECORD( flyout->children_coll, struct uwp_vec_view, IInspectable_iface );
    for (i = 0; children && i < (int)children->count && item_cnt < (int)ARRAY_SIZE(items); i++)
    {
        struct uwp_element *ch = uwp_elem_from_any( children->items[i] );
        if (!ch || ch->visibility != 0 || uwp_is_accelerator( ch )) continue;
        if (!wcscmp( ch->class_name, L"Windows.UI.Xaml.DeferredElement" )) continue;
        if (uwp_is_separator( ch ))
        {
            if (item_cnt == 0 || uwp_is_separator( items[item_cnt - 1] )) continue;
            items[item_cnt++] = ch;
        }
        else
        {
            items[item_cnt++] = ch;
        }
    }
    while (item_cnt > 0 && uwp_is_separator( items[item_cnt - 1] ))
        item_cnt--;
    for (i = 0; i < item_cnt; i++)
    {
        struct uwp_element *ch = items[i];
        if (uwp_is_separator( ch ))
            cur_y += 9;
        else
        {
            int tw = (int)(wcslen( uwp_elem_text( ch ) ) * 8 * g_layout_scale) + 56;
            if (tw > max_w) max_w = min( 340, tw );
            cur_y += 32;
        }
    }
    if (item_cnt == 0) return;
    {
        int grp_first_radio = -1;
        BOOL grp_has_checked = FALSE;
        for (i = 0; i <= item_cnt; i++)
        {
            if (i == item_cnt || uwp_is_separator( items[i] ))
            {
                if (grp_first_radio >= 0 && !grp_has_checked)
                    items[grp_first_radio]->is_checked = 1;
                grp_first_radio = -1;
                grp_has_checked = FALSE;
            }
            else if (items[i]->class_flags & UWP_CF_RADIO_MENU_ITEM)
            {
                if (grp_first_radio < 0) grp_first_radio = i;
                if (items[i]->is_checked != 0) grp_has_checked = TRUE;
            }
        }
    }

    orc = anchor->bounds;
    prc.left = orc.right - max_w;
    if (prc.left < 8) prc.left = orc.left;
    if (prc.left + max_w > width - 8) prc.left = max( 8, width - max_w - 8 );
    prc.right = prc.left + max_w;
    prc.top = orc.bottom + 4;
    if (prc.top + cur_y + 6 > height - 8) prc.top = max( 8, orc.top - (cur_y + 6) - 4 );
    prc.bottom = prc.top + cur_y + 6;
    flyout->bounds = prc;
    flyout->has_bounds = TRUE;

    light = uwp_is_light_theme( owner );
    bg_argb = light ? 0xfffcfcfcu : 0xff2c2c30u;
    bd_argb = light ? 0xffceced4u : 0xff52525au;
    hov_argb = light ? 0xffededf2u : 0xff3b3b42u;
    sep_argb = light ? 0xffdcdce2u : 0xff46464eu;
    fg_cr = light ? RGB( 24, 24, 28 ) : RGB( 245, 245, 248 );
    accent_cr = light ? RGB( 0, 103, 192 ) : RGB( 96, 205, 255 );
    fh = max( 12, (int)(13.0f * g_layout_scale + 0.5f) );

    xaml_gpu_push_rect( (float)(prc.left - 2), (float)(prc.top + 2), (float)(prc.right + 2), (float)(prc.bottom + 4),
                        NULL, 8.0f, 0x48000000u, 0, 0.0f, 1.0f );
    xaml_gpu_push_rect( (float)prc.left, (float)prc.top, (float)prc.right, (float)prc.bottom,
                        NULL, 6.0f, bg_argb, bd_argb, 1.0f, 1.0f );

    cur_y = prc.top + 6;
    for (i = 0; i < item_cnt; i++)
    {
        struct uwp_element *ch = items[i];
        if (uwp_is_separator( ch ))
        {
            int sy = cur_y + 4;
            xaml_gpu_push_rect( (float)(prc.left + 10), (float)sy, (float)(prc.right - 10), (float)(sy + 1),
                                &prc, 0.0f, sep_argb, 0, 0.0f, 1.0f );
            SetRect( &ch->bounds, prc.left, cur_y, prc.right, cur_y + 9 );
            ch->has_bounds = TRUE;
            cur_y += 9;
        }
        else
        {
            RECT rrc = { prc.left + 4, cur_y, prc.right - 4, cur_y + 32 };
            RECT trc = { prc.left + 34, cur_y, prc.right - 12, cur_y + 32 };
            ch->bounds = rrc;
            ch->has_bounds = TRUE;
            if (ch->is_hovered)
                xaml_gpu_push_rect( (float)rrc.left, (float)rrc.top, (float)rrc.right, (float)rrc.bottom,
                                    &prc, 4.0f, hov_argb, 0, 0.0f, 1.0f );
            if (ch->is_checked != 0)
            {
                if (ch->class_flags & UWP_CF_RADIO_MENU_ITEM)
                {
                    int cx = prc.left + 19, cy = cur_y + 16;
                    xaml_gpu_push_rect( (float)(cx - 3), (float)(cy - 3), (float)(cx + 3), (float)(cy + 3),
                                        &prc, 3.0f, cr_to_argb( accent_cr ), 0, 0.0f, 1.0f );
                }
                else
                {
                    RECT crc = { prc.left + 8, cur_y, prc.left + 30, cur_y + 32 };
                    WCHAR cff_buf[64];
                    uwp_resolve_font_family( L"Segoe MDL2 Assets", cff_buf, ARRAY_SIZE(cff_buf) );
                    xaml_gpu_push_text_font( ctx, L"\xE73E", cff_buf[0] ? cff_buf : L"Segoe MDL2 Assets",
                                             max( 11, (int)(12 * g_layout_scale + 0.5f) ), FW_NORMAL,
                                             DT_CENTER | DT_VCENTER | DT_SINGLELINE, &crc, &prc, fg_cr, 1.0f );
                }
            }
            if (uwp_has_text( ch ))
                xaml_gpu_push_text( ctx, uwp_elem_text( ch ), fh, ch->is_checked ? FW_SEMIBOLD : FW_NORMAL,
                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                    &trc, &prc, fg_cr, 1.0f );
            cur_y += 32;
        }
    }
}

static BOOL xaml_layout_render_gpu_target( HWND hwnd, int width, int height, IInspectable *content,
                                           ID3D11Device *dev, ID3D11DeviceContext *ctx,
                                           IDXGISwapChain1 *swapchain, ID3D11RenderTargetView *rtv )
{
    RECT rc;
    struct uwp_element *root_el, *bg_scope;
    COLORREF page_bg;
    double eval_w, eval_h;
    UINT inst_cnt = 0, hits = 0, misses = 0;

    if (width <= 0 || height <= 0 || !dev || !ctx || (!swapchain && !rtv)) return FALSE;
    uwp_ensure_app_resources_loaded();
    xaml_get_layout_metrics( hwnd, NULL, &g_layout_scale );
    SetRect( &rc, 0, 0, width, height );
    uwp_paint_resolve_eval_size( hwnd, width, height, &eval_w, &eval_h );
    root_el = uwp_elem_from_any( content );
    page_bg = uwp_paint_resolve_page_bg( root_el, &bg_scope );
    g_paint_page_bg = page_bg;
    g_paint_win_w = width;
    g_paint_win_h = height;

    if (rtv ? !xaml_gpu_begin_frame_rtv( dev, ctx, rtv, (UINT)width, (UINT)height, page_bg )
            : !xaml_gpu_begin_frame( dev, ctx, swapchain, (UINT)width, (UINT)height, page_bg ))
        return FALSE;

    {
        static struct uwp_element *s_last_layout_root = NULL;
        static int s_last_layout_w = 0, s_last_layout_h = 0;
        static double s_last_eval_w = 0.0, s_last_eval_h = 0.0;
        struct uwp_element *target_el = NULL;

        if (root_el && root_el->kind == UWP_KIND_FRAME && root_el->content &&
            !uwp_elem_from_any( root_el->content ) && g_navigated_page_el)
            target_el = g_navigated_page_el;
        else if (root_el && (root_el->content || root_el->children_coll || root_el->xbf_parsed))
            target_el = root_el;

        g_paint_dirty = FALSE;
        if (target_el)
        {
            BOOL need_layout = g_layout_dirty || !target_el->has_bounds ||
                               target_el != s_last_layout_root ||
                               width != s_last_layout_w || height != s_last_layout_h ||
                               (eval_w - s_last_eval_w > 0.5) || (s_last_eval_w - eval_w > 0.5) ||
                               (eval_h - s_last_eval_h > 0.5) || (s_last_eval_h - eval_h > 0.5);
            if (need_layout)
            {
                uwp_vsm_evaluate_tree( target_el, eval_w, eval_h );
                g_layout_dirty = FALSE;
                uwp_arrange_element( target_el, rc );
                s_last_layout_root = target_el;
                s_last_layout_w = width;
                s_last_layout_h = height;
                s_last_eval_w = eval_w;
                s_last_eval_h = eval_h;
            }
            uwp_gpu_paint_element( ctx, target_el );
        }
    }

    uwp_gpu_paint_open_popups( ctx, width, height );
    uwp_gpu_paint_combobox_popup( ctx, width, height );
    uwp_gpu_paint_cdp_popup( ctx, width, height );
    uwp_gpu_paint_asb_popup( ctx, width, height );
    uwp_gpu_paint_flyout_popup( ctx, width, height );

    if (rtv ? !xaml_gpu_end_frame_rtv( ctx, &inst_cnt, &hits, &misses )
            : !xaml_gpu_end_frame( ctx, swapchain, &inst_cnt, &hits, &misses ))
        return FALSE;

    TRACE( "xaml_layout_render_gpu: %ux%u instances=%u atlas_hits=%u atlas_misses=%u\n",
           width, height, inst_cnt, hits, misses );
    if (TRACE_ON(xaml))
    {
        HDC probe = GetDC( hwnd );
        if (probe)
        {
            TRACE( "renderpost px(0,0)=%06lx px(200,200)=%06lx px(50,500)=%06lx page_bg=%06lx\n",
                   GetPixel( probe, 0, 0 ), GetPixel( probe, 200, 200 ), GetPixel( probe, 50, 500 ),
                   page_bg );
            ReleaseDC( hwnd, probe );
        }
    }
    return TRUE;
}

BOOL xaml_layout_render_gpu( HWND hwnd, int width, int height, IInspectable *content,
                             ID3D11Device *dev, ID3D11DeviceContext *ctx, IDXGISwapChain1 *swapchain )
{
    return xaml_layout_render_gpu_target( hwnd, width, height, content, dev, ctx, swapchain, NULL );
}

BOOL xaml_layout_render_gpu_rtv( HWND hwnd, int width, int height, IInspectable *content,
                                 ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv )
{
    return xaml_layout_render_gpu_target( hwnd, width, height, content, dev, ctx, NULL, rtv );
}
