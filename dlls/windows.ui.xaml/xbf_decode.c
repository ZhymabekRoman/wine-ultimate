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
#include "xbf_known_types.h"
#include "weakreference.h"
#include "winreg.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"
#include "xbf_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

struct uwp_rd_bucket *g_rd_buckets_head = NULL;
int g_app_rd_sentinel = 0;
int g_theme_rd_sentinel = 0;
BOOL g_app_res_loaded = FALSE;
BOOL g_app_res_loading = FALSE;
int g_loaded_res_xbf_cnt = 0;

BOOL uwp_panel_has_concrete_child( struct uwp_element *panel )
{
    struct uwp_vec_view *v;
    unsigned int k;
    if (!panel) return FALSE;
    if (panel->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( panel->content );
        if (ch && ch->kind != UWP_KIND_DEFERRED && ch->kind != UWP_KIND_STYLE &&
            ch->kind != UWP_KIND_BRUSH && ch->kind != UWP_KIND_ACCELERATOR &&
            ch->kind != UWP_KIND_XAML_COMMAND && !(ch->class_flags & UWP_CF_TEMPLATE) &&
            ch->visibility == 0) return TRUE;
    }
    if (!panel->children_coll) return FALSE;
    v = CONTAINING_RECORD( panel->children_coll, struct uwp_vec_view, IInspectable_iface );
    for (k = 0; k < v->count; k++)
    {
        struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
        if (ch && ch->kind != UWP_KIND_DEFERRED && ch->kind != UWP_KIND_STYLE &&
            ch->kind != UWP_KIND_BRUSH && ch->kind != UWP_KIND_ACCELERATOR &&
            ch->kind != UWP_KIND_XAML_COMMAND && !(ch->class_flags & UWP_CF_TEMPLATE) &&
            ch->visibility == 0) return TRUE;
    }
    return FALSE;
}

BOOL xbf_read_uleb( const BYTE *buf, DWORD *io_p, DWORD end_p, DWORD *out_val )
{
    DWORD p, val = 0;
    int sh = 0;
    if (!buf || !io_p || !out_val) return FALSE;
    p = *io_p;
    while (p < end_p)
    {
        BYTE b = buf[p++];
        if (sh >= 35 || (sh == 28 && (b & 0x70))) return FALSE;
        val |= (DWORD)(b & 0x7f) << sh;
        if (!(b & 0x80))
        {
            *io_p = p;
            *out_val = val;
            return TRUE;
        }
        sh += 7;
    }
    return FALSE;
}

BOOL xbf_get_substream_bounds( const struct xbf_ctx *ctx, DWORD sub_idx,
                                      DWORD *out_start, DWORD *out_end, DWORD *out_size )
{
    DWORD ss, ee;
    if (!ctx || !ctx->nodes || sub_idx >= ctx->sub_cnt) return FALSE;
    if (ctx->sub_base < 4 || ctx->sub_base > ctx->node_sz || sub_idx >= (ctx->sub_base - 4) / 8) return FALSE;
    ss = *(const DWORD *)(ctx->nodes + 4 + sub_idx * 8);
    ee = *(const DWORD *)(ctx->nodes + 4 + sub_idx * 8 + 4);
    if (ss > ee || ee > ctx->node_sz - ctx->sub_base) return FALSE;
    if (out_start) *out_start = ctx->sub_base + ss;
    if (out_end) *out_end = ctx->sub_base + ee;
    if (out_size) *out_size = ee - ss;
    return TRUE;
}

static void xbf_setter_fill_prop_meta( struct xbf_ctx *ctx, struct uwp_style_setter *st, WORD pid )
{
    if (!ctx || !st || pid >= ctx->prop_cnt) return;
    if (ctx->props[pid].name[0])
        lstrcpynW( st->prop_name, ctx->props[pid].name, ARRAY_SIZE(st->prop_name) );
    if (ctx->props[pid].owner_tid < ctx->type_cnt && ctx->types[ctx->props[pid].owner_tid].full_name[0])
        lstrcpynW( st->owner_cls, ctx->types[ctx->props[pid].owner_tid].full_name, ARRAY_SIZE(st->owner_cls) );
}

static void xbf_stack_val_add_setter( struct xbf_stack_val *sv, WORD pid, int vt, BOOL is_res_ref,
                                      double num, const WCHAR *sval, const double vec4[4] )
{
    struct uwp_style_setter *st;
    if (!sv) return;
    if (sv->setter_cnt >= sv->setter_cap)
    {
        int ncap = sv->setter_cap ? sv->setter_cap * 2 : 16;
        struct uwp_style_setter *ns = realloc( sv->setters, ncap * sizeof(*ns) );
        if (!ns) return;
        sv->setters = ns;
        sv->setter_cap = ncap;
    }
    st = &sv->setters[sv->setter_cnt++];
    memset( st, 0, sizeof(*st) );
    st->pid = pid;
    st->vt = vt;
    st->is_res_ref = is_res_ref;
    st->num = num;
    if (vec4) memcpy( st->vec4, vec4, sizeof(st->vec4) );
    if (sval && sval[0]) lstrcpynW( st->str, sval, ARRAY_SIZE(st->str) );
}

void uwp_rd_store_stack_val( struct uwp_rd_bucket *b, const WCHAR *key, struct xbf_stack_val *sv )
{
    struct uwp_rd_entry *e;
    if (!b || !key || !key[0] || !sv) return;
    TRACE( "uwp_rd_store_stack_val b=%p key=%s kind=%d dval=%f\n", b, debugstr_w(key), sv->kind, sv->dval );
    e = rd_bucket_upsert( b, key );
    if (!e) return;

    if (e->value) { IInspectable_Release( e->value ); e->value = NULL; }
    free( e->setters );
    e->setters = NULL;
    e->setter_cnt = e->setter_cap = 0;

    e->kind = sv->kind;
    e->tid = sv->tid;
    e->argb = sv->argb;
    e->has_argb = sv->has_argb;
    e->opacity = sv->opacity > 0.0 ? sv->opacity : (sv->kind == UWP_RD_BRUSH ? sv->opacity : 1.0);
    if (sv->kind != UWP_RD_BRUSH && e->opacity == 0.0) e->opacity = 1.0;
    e->dval = sv->dval;
    memcpy( e->vec4, sv->vec4, sizeof(e->vec4) );
    lstrcpynW( e->str_val, (sv->kind == UWP_RD_STYLE) ? sv->based_on : sv->str_val, ARRAY_SIZE(e->str_val) );
    e->template_xbf_data = sv->template_xbf_data;
    e->template_xbf_size = sv->template_xbf_size;
    e->template_sub_idx = sv->template_sub_idx;
    uwp_set_template_owner( &e->template_owner_comp, sv->template_owner_comp );

    if (sv->kind == UWP_RD_STYLE && sv->setter_cnt > 0)
    {
        e->setters = malloc( (size_t)sv->setter_cnt * sizeof(*e->setters) );
        if (e->setters)
        {
            memcpy( e->setters, sv->setters, (size_t)sv->setter_cnt * sizeof(*e->setters) );
            e->setter_cnt = sv->setter_cnt;
            e->setter_cap = sv->setter_cnt;
        }
    }
    if (sv->el && sv->kind == UWP_RD_BRUSH)
    {
        if (uwp_elem_has_brush_argb( sv->el ))
        {
            e->argb = uwp_elem_brush_argb( sv->el );
            e->has_argb = TRUE;
        }
        e->opacity = uwp_elem_brush_opacity( sv->el );
        if (!e->str_val[0] && sv->el->style_anim && sv->el->style_anim->brush_color_ref[0])
            lstrcpynW( e->str_val, sv->el->style_anim->brush_color_ref, ARRAY_SIZE(e->str_val) );
    }
    if (sv->kind == UWP_RD_OBJ && sv->raw)
    {
        e->value = sv->raw;
        IInspectable_AddRef( sv->raw );
    }
    if (e->kind == UWP_RD_FONT_FAMILY && e->str_val[0])
    {
        WCHAR face[64] = {0};
        uwp_resolve_font_family( e->str_val, face, ARRAY_SIZE(face) );
    }
    /* Defer ThemeResource/StaticResource brush color resolution to lookup time so
     * theme dictionaries and Light/Dark polarity resolve accurately after full RD load. */
    if (e->kind == UWP_RD_BRUSH && !e->has_argb && e->str_val[0])
    {
        UINT32 parsed = 0;
        if (uwp_parse_color_string( e->str_val, &parsed ))
        {
            e->argb = parsed;
            e->has_argb = TRUE;
        }
    }
}

BOOL xbf_read_value( struct xbf_ctx *ctx, const BYTE *nd, DWORD *io_p, DWORD end_p,
                            int *out_vt, double *out_num, WCHAR *out_str, int max_str,
                            double out_vec4[4] )
{
    DWORD p = *io_p;
    BYTE vt;
    if (p >= end_p) return FALSE;
    vt = nd[p++];
    *out_vt = vt;
    *out_num = 0.0;
    if (out_str && max_str > 0) out_str[0] = 0;
    if (out_vec4) { out_vec4[0] = out_vec4[1] = out_vec4[2] = out_vec4[3] = 0.0; }

    switch (vt)
    {
    case 0x00: /* bool false */
    case 0x01: /* bool false / zero */
        *out_num = 0.0;
        break;
    case 0x02: /* bool true */
        *out_num = 1.0;
        break;
    case 0x03: /* float32 */
        if (p + 4 > end_p) return FALSE;
        *out_num = (double)*(const float *)(nd + p);
        p += 4;
        break;
    case 0x04: /* int32 */
        if (p + 4 > end_p) return FALSE;
        *out_num = (double)*(const INT32 *)(nd + p);
        p += 4;
        break;
    case 0x05: /* string table ref (u16) */
        if (p + 2 > end_p) return FALSE;
        {
            WORD sid = *(const WORD *)(nd + p);
            p += 2;
            if (sid < ctx->str_cnt && ctx->strs[sid] && out_str)
                lstrcpynW( out_str, ctx->strs[sid], max_str );
        }
        break;
    case 0x06: /* Thickness (4 floats) */
        if (p + 16 > end_p) return FALSE;
        if (out_vec4)
        {
            out_vec4[0] = *(const float *)(nd + p);
            out_vec4[1] = *(const float *)(nd + p + 4);
            out_vec4[2] = *(const float *)(nd + p + 8);
            out_vec4[3] = *(const float *)(nd + p + 12);
        }
        p += 16;
        break;
    case 0x07: /* GridLength (u32 type, f32 val) */
        if (p + 8 > end_p) return FALSE;
        if (out_vec4)
        {
            out_vec4[0] = (double)*(const DWORD *)(nd + p);
            out_vec4[1] = (double)*(const float *)(nd + p + 4);
        }
        *out_num = (double)*(const float *)(nd + p + 4);
        p += 8;
        break;
    case 0x08: /* Color (u32 ARGB) */
        if (p + 4 > end_p) return FALSE;
        *out_num = (double)*(const DWORD *)(nd + p);
        p += 4;
        break;
    case 0x09: /* Inline UTF-16 string (u32 len + chars) */
        if (p + 4 > end_p) return FALSE;
        {
            DWORD ccnt = *(const DWORD *)(nd + p);
            p += 4;
            if (ccnt > (end_p - p) / 2) return FALSE;
            if (out_str && max_str > 0)
            {
                DWORD cp = min( ccnt, (DWORD)(max_str - 1) );
                memcpy( out_str, nd + p, cp * sizeof(WCHAR) );
                out_str[cp] = 0;
            }
            p += ccnt * 2;
        }
        break;
    case 0x0b: /* Enum (u16 type_id, u32 val) */
        if (p + 6 > end_p) return FALSE;
        *out_num = (double)*(const INT32 *)(nd + p + 2);
        p += 6;
        break;
    default:
        return FALSE;
    }
    *io_p = p;
    return TRUE;
}

void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                                WORD pid, int vt, double num, const WCHAR *sval,
                                const double vec4[4], BOOL is_res_ref );

static int uwp_parse_font_weight( const WCHAR *s, double num, int vt )
{
    if (vt == 0x03 || vt == 0x04)
    {
        int w = (int)(num + 0.5);
        if (w >= 100 && w <= 950) return w;
    }
    if (!s || !s[0]) return 0;
    if (!wcsicmp( s, L"Thin" )) return FW_THIN;
    if (!wcsicmp( s, L"ExtraLight" ) || !wcsicmp( s, L"UltraLight" )) return FW_EXTRALIGHT;
    if (!wcsicmp( s, L"Light" )) return FW_LIGHT;
    if (!wcsicmp( s, L"SemiLight" )) return 350;
    if (!wcsicmp( s, L"Normal" ) || !wcsicmp( s, L"Regular" )) return FW_NORMAL;
    if (!wcsicmp( s, L"Medium" )) return FW_MEDIUM;
    if (!wcsicmp( s, L"SemiBold" ) || !wcsicmp( s, L"DemiBold" )) return FW_SEMIBOLD;
    if (!wcsicmp( s, L"Bold" )) return FW_BOLD;
    if (!wcsicmp( s, L"ExtraBold" ) || !wcsicmp( s, L"UltraBold" )) return FW_EXTRABOLD;
    if (!wcsicmp( s, L"Black" ) || !wcsicmp( s, L"Heavy" )) return FW_HEAVY;
    if (s[0] >= L'0' && s[0] <= L'9')
    {
        int w = (int)wcstol( s, NULL, 10 );
        if (w >= 100 && w <= 950) return w;
    }
    return 0;
}

static inline BOOL xbf_rd_find_cached( struct xbf_ctx *ctx, struct uwp_rd_bucket *start_bucket,
                                       struct uwp_element *scope_el, const WCHAR *raw_key,
                                       struct uwp_rd_entry **out_entry, struct uwp_rd_bucket **out_bucket )
{
    if (ctx && ctx->res_cache_valid && ctx->res_cache_scope_el == scope_el &&
        ctx->res_cache_key && raw_key && !wcscmp( ctx->res_cache_key, raw_key ))
    {
        *out_entry = ctx->res_cache_entry;
        if (out_bucket) *out_bucket = ctx->res_cache_bucket;
        return ctx->res_cache_entry != NULL;
    }
    return uwp_rd_find_entry( start_bucket, scope_el, raw_key, 0, out_entry, out_bucket );
}

static BOOL xbf_resolve_double_prop( struct xbf_ctx *ctx, struct uwp_element *el,
                                     int vt, double num, const WCHAR *sval, BOOL is_res_ref,
                                     double *out_val )
{
    if (vt == 0x03 || vt == 0x04)
    {
        *out_val = num;
        return TRUE;
    }
    if (is_res_ref && sval && sval[0])
    {
        struct uwp_rd_entry *de = NULL;
        struct uwp_rd_bucket *hint_rd = ctx ? (ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd) : NULL;
        if (xbf_rd_find_cached( ctx, hint_rd, el, sval, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
        {
            *out_val = de->dval;
            return TRUE;
        }
    }
    if (sval && sval[0])
    {
        WCHAR *endp = NULL;
        double d = wcstod( sval, &endp );
        if (endp && endp != sval)
        {
            *out_val = d;
            return TRUE;
        }
    }
    return FALSE;
}

static inline BOOL xbf_prop_blocked_by_local( const struct xbf_ctx *ctx, const struct uwp_element *el, UINT32 bit )
{
    return (ctx && ctx->in_style_apply && (el->local_prop_mask & bit) != 0);
}

static inline void xbf_prop_mark_local( const struct xbf_ctx *ctx, struct uwp_element *el, UINT32 bit )
{
    if (!ctx || !ctx->in_style_apply) el->local_prop_mask |= bit;
}

static BOOL xbf_apply_nav_prop_name( struct xbf_ctx *ctx, struct uwp_element *el,
                                     const WCHAR *prop_name, int vt, double num,
                                     const WCHAR *sval, BOOL is_res_ref,
                                     double *out_resolved_d, BOOL *out_has_resolved_d )
{
    if (!wcscmp( prop_name, L"DisplayMemberPath" ) && sval && sval[0])
    {
        struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
        if (ne) lstrcpynW( ne->display_member_path, sval, ARRAY_SIZE(ne->display_member_path) );
        return TRUE;
    }
    if (!uwp_is_nav_view( el )) return FALSE;
    if (!wcscmp( prop_name, L"PaneDisplayMode" ))
    {
        int mode = -1;
        if (vt == 0x04 || vt == 0x0b) mode = (int)num;
        else if (sval && sval[0])
        {
            if (!wcscmp( sval, L"Auto" )) mode = 0;
            else if (!wcscmp( sval, L"Left" )) mode = 1;
            else if (!wcscmp( sval, L"Top" )) mode = 2;
            else if (!wcscmp( sval, L"LeftCompact" )) mode = 3;
            else if (!wcscmp( sval, L"LeftMinimal" )) mode = 4;
        }
        if (mode >= 0 && mode <= 4)
        {
            struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
            if (ne) { ne->nav_pane_display_mode = mode; ne->nav_pane_display_mode_set = TRUE; }
            TRACE( "xbf_apply_nav_prop: NavView(%s) PaneDisplayMode=%d (%s)\n",
                   debugstr_w(el->name), mode, sval ? debugstr_w(sval) : "" );
        }
        return TRUE;
    }
    if (!wcscmp( prop_name, L"IsPaneOpen" ))
    {
        struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
        if (ne)
        {
            if (vt == 0x01 || vt == 0x02) ne->nav_is_pane_open = (vt == 0x02);
            else if (sval && sval[0]) ne->nav_is_pane_open = !wcsicmp( sval, L"True" );
        }
        uwp_nav_track_open_state( el );
        return TRUE;
    }
    if (!wcscmp( prop_name, L"OpenPaneLength" ) || !wcscmp( prop_name, L"CompactPaneLength" ) ||
        !wcscmp( prop_name, L"CompactModeThresholdWidth" ) || !wcscmp( prop_name, L"ExpandedModeThresholdWidth" ))
    {
        double d = 0.0;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &d ) && d > 0.0)
        {
            struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
            if (ne)
            {
                if (!wcscmp( prop_name, L"OpenPaneLength" )) ne->nav_open_pane_length = d;
                else if (!wcscmp( prop_name, L"CompactPaneLength" )) ne->nav_compact_pane_length = d;
                else if (!wcscmp( prop_name, L"CompactModeThresholdWidth" )) ne->nav_compact_threshold = d;
                else ne->nav_expanded_threshold = d;
            }
            if (out_resolved_d) *out_resolved_d = d;
            if (out_has_resolved_d) *out_has_resolved_d = TRUE;
        }
        return TRUE;
    }
    if (!wcscmp( prop_name, L"IsSettingsVisible" ) || !wcscmp( prop_name, L"IsPaneToggleButtonVisible" ))
    {
        struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
        BOOL flag;
        if (!ne) return TRUE;
        if (vt == 0x01 || vt == 0x02) flag = (vt == 0x02);
        else if (sval && sval[0]) flag = !wcsicmp( sval, L"True" );
        else return TRUE;
        if (!wcscmp( prop_name, L"IsSettingsVisible" )) ne->nav_is_settings_visible = flag;
        else ne->nav_is_pane_toggle_visible = flag;
        return TRUE;
    }
    return FALSE;
}

static int xbf_parse_h_align( int vt, double num, const WCHAR *sval )
{
    if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
    {
        if (!wcsicmp( sval, L"Left" )) return 0;
        if (!wcsicmp( sval, L"Center" )) return 1;
        if (!wcsicmp( sval, L"Right" )) return 2;
        if (!wcsicmp( sval, L"Stretch" )) return 3;
    }
    return (int)num;
}

static int xbf_parse_v_align( int vt, double num, const WCHAR *sval )
{
    if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
    {
        if (!wcsicmp( sval, L"Top" )) return 0;
        if (!wcsicmp( sval, L"Center" )) return 1;
        if (!wcsicmp( sval, L"Bottom" )) return 2;
        if (!wcsicmp( sval, L"Stretch" )) return 3;
    }
    return (int)num;
}

static int xbf_parse_text_align( int vt, double num, const WCHAR *sval )
{
    if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
    {
        if (!wcsicmp( sval, L"Center" )) return 0;
        if (!wcsicmp( sval, L"Left" )) return 1;
        if (!wcsicmp( sval, L"Right" )) return 2;
        if (!wcsicmp( sval, L"Justify" )) return 3;
    }
    return (int)num;
}

static void xbf_apply_named_property( struct xbf_ctx *ctx, struct uwp_element *el,
                                      const WCHAR *owner_cls, const WCHAR *prop_name,
                                      int vt, double num, const WCHAR *sval, BOOL is_res_ref )
{
    double resolved_d = 0.0;
    BOOL has_resolved_d = FALSE;
    if (!el || !prop_name || !prop_name[0]) return;
    if (!wcscmp( prop_name, L"Visibility" ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_VISIBILITY )) return;
        if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
            el->visibility = !wcsicmp( sval, L"Collapsed" ) ? 1 : 0;
        else
            el->visibility = (int)num;
        xbf_prop_mark_local( ctx, el, UWP_LP_VISIBILITY );
        return;
    }
    if (!wcscmp( prop_name, L"Opacity" ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_OPACITY )) return;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ))
        {
            el->opacity = resolved_d;
            xbf_prop_mark_local( ctx, el, UWP_LP_OPACITY );
        }
        return;
    }
    if (!wcscmp( prop_name, L"MaxLines" ))
    {
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ))
            el->max_lines = max( 0, (int)resolved_d );
        return;
    }
    if (!wcscmp( prop_name, L"TextWrapping" ))
    {
        if (sval && sval[0])
        {
            if (!wcsicmp( sval, L"NoWrap" )) el->text_wrapping = 1;
            else if (!wcsicmp( sval, L"WrapWholeWords" )) el->text_wrapping = 3;
            else if (!wcsicmp( sval, L"Wrap" )) el->text_wrapping = 2;
        }
        else el->text_wrapping = (int)num;
        return;
    }
    if (!wcscmp( prop_name, L"FontSize" ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_FONT_SIZE )) return;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ) && resolved_d > 0.0)
        {
            el->font_size = resolved_d;
            el->has_font_size = TRUE;
            xbf_prop_mark_local( ctx, el, UWP_LP_FONT_SIZE );
        }
        return;
    }
    if (!wcscmp( prop_name, L"FontWeight" ))
    {
        int fw;
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_FONT_WEIGHT )) return;
        fw = uwp_parse_font_weight( sval, num, vt );
        if (fw > 0)
        {
            el->font_weight = fw;
            xbf_prop_mark_local( ctx, el, UWP_LP_FONT_WEIGHT );
        }
        return;
    }
    if (!wcscmp( prop_name, L"HorizontalAlignment" ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_H_ALIGN )) return;
        el->h_align = xbf_parse_h_align( vt, num, sval );
        xbf_prop_mark_local( ctx, el, UWP_LP_H_ALIGN );
        return;
    }
    if (!wcscmp( prop_name, L"HorizontalContentAlignment" ))
    {
        el->h_content_align = xbf_parse_h_align( vt, num, sval );
        el->has_h_content_align = TRUE;
        return;
    }
    if (!wcscmp( prop_name, L"TextAlignment" ))
    {
        el->text_align = xbf_parse_text_align( vt, num, sval );
        el->has_text_align = TRUE;
        return;
    }
    if (!wcscmp( prop_name, L"FlowDirection" ))
    {
        if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
            el->flow_direction = !wcsicmp( sval, L"RightToLeft" ) ? 2 : 1;
        else
            el->flow_direction = ((int)num == 1) ? 2 : 1;
        return;
    }
    if (!wcscmp( prop_name, L"IsActive" ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_IS_ACTIVE )) return;
        if (vt == 0x01 || vt == 0x02) el->is_active = (vt == 0x02) ? 1 : 0;
        else if (sval && sval[0]) el->is_active = !wcsicmp( sval, L"True" ) ? 1 : 0;
        else el->is_active = (num != 0.0) ? 1 : 0;
        xbf_prop_mark_local( ctx, el, UWP_LP_IS_ACTIVE );
        return;
    }
    xbf_apply_nav_prop_name( ctx, el, prop_name, vt, num, sval, is_res_ref, &resolved_d, &has_resolved_d );

    if (((owner_cls && owner_cls[0] &&
          !wcscmp( owner_cls, L"Microsoft.UI.Xaml.Controls.NavigationView" ) &&
          (!wcscmp( prop_name, L"PaneDisplayMode" ) || !wcscmp( prop_name, L"IsPaneOpen" ) ||
           !wcscmp( prop_name, L"OpenPaneLength" ) || !wcscmp( prop_name, L"CompactPaneLength" ) ||
           !wcscmp( prop_name, L"IsSettingsVisible" ) || !wcscmp( prop_name, L"IsPaneToggleButtonVisible" ) ||
           !wcscmp( prop_name, L"CompactModeThresholdWidth" ) || !wcscmp( prop_name, L"ExpandedModeThresholdWidth" ))) ||
         (el->class_name[0] && !(el->class_flags & (UWP_CF_UWP_PREFIX | UWP_CF_WINUI_PREFIX)) &&
          (!owner_cls || !owner_cls[0] ||
           (wcsncmp( owner_cls, L"Windows.UI.Xaml.", 16 ) != 0 &&
            wcsncmp( owner_cls, L"Microsoft.UI.Xaml.", 18 ) != 0)))))
    {
        const WCHAR *set_cls = (owner_cls && owner_cls[0]) ? owner_cls : el->class_name;
        void *ao = (ctx && ctx->app_outer) ? ctx->app_outer : uwp_get_app_outer();
        if (ao && set_cls[0])
        {
            IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;
            if (!has_resolved_d && (vt == 0x03 || vt == 0x04 || is_res_ref) &&
                xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ))
                has_resolved_d = TRUE;
            if (has_resolved_d)
            {
                WCHAR nbuf[64];
                swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", resolved_d );
                uwp_set_custom_xaml_member( ao, set_cls, prop_name, target, nbuf );
            }
            else if (vt == 0x01 || vt == 0x02)
                uwp_set_custom_xaml_member( ao, set_cls, prop_name, target, (vt == 0x02) ? L"True" : L"False" );
            else if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
                uwp_set_custom_xaml_member( ao, set_cls, prop_name, target, sval );
            else if (vt == 0x03 || vt == 0x04 || vt == 0x0b)
            {
                if (!wcscmp( prop_name, L"PaneDisplayMode" ))
                {
                    static const WCHAR *const modes[] = { L"Auto", L"Left", L"Top", L"LeftCompact", L"LeftMinimal" };
                    int m = (int)num;
                    uwp_set_custom_xaml_member( ao, set_cls, prop_name, target,
                                                (m >= 0 && m < 5) ? modes[m] : L"Auto" );
                }
                else
                {
                    WCHAR nbuf[64];
                    swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", num );
                    uwp_set_custom_xaml_member( ao, set_cls, prop_name, target, nbuf );
                }
            }
        }
    }
}

void uwp_apply_style_entry( struct xbf_ctx *ctx, struct uwp_element *el,
                                   struct uwp_rd_bucket *b, struct uwp_rd_entry *se, int depth )
{
    struct xbf_ctx local_ctx = {0};
    struct xbf_ctx *use_ctx = ctx ? ctx : &local_ctx;
    BOOL prev_in_style;
    int i;
    if (!el || !se || se->kind != UWP_RD_STYLE || depth > 8) return;
    prev_in_style = use_ctx->in_style_apply;
    use_ctx->in_style_apply = TRUE;
    if (uwp_style_is_accent_name( se->key ))
        el->is_accent_style = TRUE;
    if (se->str_val[0])
    {
        struct uwp_rd_entry *base_se = NULL;
        struct uwp_rd_bucket *base_b = NULL;
        if (uwp_style_is_accent_name( se->str_val ))
            el->is_accent_style = TRUE;
        if (uwp_rd_find_entry( b, el, se->str_val, 0, &base_se, &base_b ) && base_se)
            uwp_apply_style_entry( use_ctx, el, base_b, base_se, depth + 1 );
    }
    if (se->template_xbf_data && se->template_xbf_size)
    {
        el->template_xbf_data = se->template_xbf_data;
        el->template_xbf_size = se->template_xbf_size;
        el->template_sub_idx = se->template_sub_idx;
        if (b) el->template_rd_bucket = b;
    }
    for (i = 0; i < se->setter_cnt; i++)
    {
        const struct uwp_style_setter *st = &se->setters[i];
        if (!(st->pid & 0x8000) && st->prop_name[0])
            xbf_apply_named_property( use_ctx, el, st->owner_cls, st->prop_name, st->vt, st->num, st->str, st->is_res_ref );
        else
            xbf_apply_property( use_ctx, el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
    }
    use_ctx->in_style_apply = prev_in_style;
}

static void xbf_apply_stack_val_prop( struct xbf_ctx *ctx, struct uwp_element *el,
                                      struct xbf_stack_val *sv, WORD pid, int vt,
                                      double num, const WCHAR *sval, BOOL is_res_ref )
{
    UINT32 argb = 0;

    if (!sv) return;
    if (sv->kind == UWP_RD_BRUSH)
    {
        if (pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR || pid == XBF_PROP_FALLBACK_COLOR)
        {
            if (vt == 0x08) { sv->argb = (UINT32)num; sv->has_argb = TRUE; }
            else if (sval && sval[0])
            {
                if (uwp_parse_color_string( sval, &argb )) { sv->argb = argb; sv->has_argb = TRUE; }
                else lstrcpynW( sv->str_val, sval, ARRAY_SIZE(sv->str_val) );
            }
        }
        else if (pid == XBF_PROP_BRUSH_OPACITY)
        {
            if (vt == 0x03 || vt == 0x04) sv->opacity = num;
            else if (sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (xbf_rd_find_cached( ctx, ctx ? ctx->target_rd : NULL, el, sval, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    sv->opacity = de->dval;
            }
        }
    }
    else if (sv->kind == UWP_RD_STYLE && pid == XBF_PROP_BASED_ON && sval && sval[0])
        uwp_extract_resource_key( sval, sv->based_on, ARRAY_SIZE(sv->based_on) );
    else if (sv->kind == UWP_RD_COLOR && pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR && vt == 0x08)
    {
        sv->argb = (UINT32)num;
        sv->has_argb = TRUE;
    }

    if (el || !sv->raw || !ctx || (pid & 0x8000) || pid >= ctx->prop_cnt) return;
    if (!ctx->props[pid].name[0] || ctx->props[pid].owner_tid >= ctx->type_cnt) return;
    {
        const struct xbf_prop_info *pi = &ctx->props[pid];
        const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
        void *ao = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();
        if (!owner_cls[0] || !ao) return;
        if (is_res_ref && sval && sval[0])
        {
            struct uwp_rd_entry *re = NULL;
            struct uwp_rd_bucket *found_b = NULL;
            IInspectable *res_val = NULL;
            if (xbf_rd_find_cached( ctx, ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd,
                                     ctx->root_el, sval, &re, &found_b ) && re)
            {
                if (re->kind == UWP_RD_DOUBLE)
                {
                    WCHAR nbuf[64];
                    swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", re->dval );
                    uwp_set_custom_xaml_member( ao, owner_cls, pi->name, sv->raw, nbuf );
                }
                else
                    res_val = uwp_rd_materialize_entry( found_b, re );
            }
            if (res_val)
                uwp_set_custom_xaml_member_obj( ao, owner_cls, pi->name, sv->raw, res_val );
        }
        else if (vt == 0x01 || vt == 0x02)
            uwp_set_custom_xaml_member( ao, owner_cls, pi->name, sv->raw, (vt == 0x02) ? L"True" : L"False" );
        else if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
            uwp_set_custom_xaml_member( ao, owner_cls, pi->name, sv->raw, sval );
        else if (vt == 0x03 || vt == 0x04 || vt == 0x0b)
        {
            WCHAR nbuf[64];
            swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", num );
            uwp_set_custom_xaml_member( ao, owner_cls, pi->name, sv->raw, nbuf );
        }
    }
}

static BOOL xbf_resolve_brush_cached( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *sval, UINT32 *out_argb )
{
    if (ctx && ctx->res_cache_valid && ctx->res_cache_scope_el == el &&
        ctx->res_cache_key && sval && !wcscmp( ctx->res_cache_key, sval ) && ctx->res_cache_entry)
    {
        if (uwp_resolve_brush_from_entry( ctx->res_cache_start_b, el, ctx->res_cache_entry,
                                          ctx->res_cache_bucket, 0, out_argb ))
            return TRUE;
    }
    return uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, out_argb );
}

static BOOL xbf_apply_brush_or_grad_prop( struct xbf_ctx *ctx, struct uwp_element *el,
                                          WORD pid, int vt, double num, const WCHAR *sval )
{
    UINT32 argb = 0;

    if (el->kind == UWP_KIND_BRUSH)
    {
        if (pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR || pid == XBF_PROP_FALLBACK_COLOR)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (!sa) return TRUE;
            if (vt == 0x08) { sa->brush_argb = (UINT32)num; sa->has_brush_argb = TRUE; }
            else if (sval && sval[0])
            {
                if (xbf_resolve_brush_cached( ctx, el, sval, &argb ))
                {
                    sa->brush_argb = argb;
                    sa->has_brush_argb = TRUE;
                }
                else lstrcpynW( sa->brush_color_ref, sval, ARRAY_SIZE(sa->brush_color_ref) );
            }
            return TRUE;
        }
        if (pid == XBF_PROP_BRUSH_OPACITY)
        {
            if (vt == 0x03 || vt == 0x04)
            {
                struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
                if (sa) sa->brush_opacity = num;
            }
            return TRUE;
        }
        if ((pid == XBF_PROP_LGB_START_POINT || pid == XBF_PROP_LGB_END_POINT) && sval && sval[0])
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            WCHAR *endp = NULL;
            float px = (float)wcstod( sval, &endp ), py = 0.0f;
            if (endp)
            {
                while (*endp == L' ' || *endp == L',' || *endp == L';') endp++;
                py = (float)wcstod( endp, NULL );
            }
            if (sa)
            {
                if (pid == XBF_PROP_LGB_START_POINT) { sa->grad_start_x = px; sa->grad_start_y = py; }
                else { sa->grad_end_x = px; sa->grad_end_y = py; }
            }
            return TRUE;
        }
        if (pid == XBF_PROP_TILEBRUSH_ALIGNMENT_X || pid == XBF_PROP_TILEBRUSH_ALIGNMENT_Y)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa)
            {
                if (pid == XBF_PROP_TILEBRUSH_ALIGNMENT_X) sa->tile_align_x = (int)num;
                else sa->tile_align_y = (int)num;
            }
            return TRUE;
        }
        if (pid == XBF_PROP_TILEBRUSH_STRETCH)
        {
            struct uwp_media_ext *m = uwp_elem_ensure_media( el );
            if (m) m->media_stretch = (int)num;
            return TRUE;
        }
    }

    if (pid == XBF_PROP_GRADIENTSTOP_COLOR)
    {
        struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
        if (!sa) return TRUE;
        if (vt == 0x08) { sa->brush_argb = (UINT32)num; sa->has_brush_argb = TRUE; }
        else if (sval && sval[0] && uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
        {
            sa->brush_argb = argb;
            sa->has_brush_argb = TRUE;
        }
        return TRUE;
    }
    if (pid == XBF_PROP_GRADIENTSTOP_OFFSET)
    {
        if (vt == 0x03 || vt == 0x04)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa) sa->grad_stop_offset = num;
        }
        return TRUE;
    }
    if (el->kind == UWP_KIND_RESOURCE_DICT && pid == XBF_PROP_RESOURCE_DICTIONARY_SOURCE && sval && sval[0])
    {
        uwp_load_res_xbf_into_bucket( ctx ? ctx->base_xbf_dir : NULL, sval, uwp_rd_bucket_for_el( el, TRUE ) );
        return TRUE;
    }

    if (!(pid & 0x8000)) return FALSE;
    if (uwp_is_bg_pid( pid ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_BACKGROUND )) return TRUE;
        if (vt == 0x08) { uwp_el_set_bg_argb( el, (UINT32)num ); xbf_prop_mark_local( ctx, el, UWP_LP_BACKGROUND ); }
        else if (sval && sval[0])
        {
            UINT32 lit_c = 0;
            if (uwp_brush_key_is_accent( sval ))
                el->is_accent_style = TRUE;
            if (xbf_resolve_brush_cached( ctx, el, sval, &argb ))
            {
                uwp_el_set_bg_argb( el, argb );
                if (!uwp_parse_color_string( sval, &lit_c ))
                {
                    struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
                    if (sa) lstrcpynW( sa->bg_theme_key, sval, ARRAY_SIZE(sa->bg_theme_key) );
                }
                xbf_prop_mark_local( ctx, el, UWP_LP_BACKGROUND );
            }
        }
        return TRUE;
    }
    if (uwp_is_fg_pid( pid ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_FOREGROUND )) return TRUE;
        if (vt == 0x08) { uwp_el_set_fg_argb( el, (UINT32)num ); xbf_prop_mark_local( ctx, el, UWP_LP_FOREGROUND ); }
        else if (sval && sval[0] && xbf_resolve_brush_cached( ctx, el, sval, &argb ))
        {
            UINT32 lit_c = 0;
            uwp_el_set_fg_argb( el, argb );
            if (!uwp_parse_color_string( sval, &lit_c ))
            {
                struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
                if (sa) lstrcpynW( sa->fg_theme_key, sval, ARRAY_SIZE(sa->fg_theme_key) );
            }
            xbf_prop_mark_local( ctx, el, UWP_LP_FOREGROUND );
        }
        return TRUE;
    }
    if (uwp_is_border_pid( pid ))
    {
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_BORDER_BRUSH )) return TRUE;
        if (vt == 0x08) { uwp_el_set_border_argb( el, (UINT32)num ); xbf_prop_mark_local( ctx, el, UWP_LP_BORDER_BRUSH ); }
        else if (sval && sval[0] && xbf_resolve_brush_cached( ctx, el, sval, &argb ))
        {
            UINT32 lit_c = 0;
            uwp_el_set_border_argb( el, argb );
            if (!uwp_parse_color_string( sval, &lit_c ))
            {
                struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
                if (sa) lstrcpynW( sa->border_theme_key, sval, ARRAY_SIZE(sa->border_theme_key) );
            }
            xbf_prop_mark_local( ctx, el, UWP_LP_BORDER_BRUSH );
        }
        return TRUE;
    }
    return FALSE;
}

static BOOL xbf_apply_known_layout_prop( struct xbf_ctx *ctx, struct uwp_element *el,
                                         WORD pid, int vt, double num, const WCHAR *sval,
                                         const double vec4[4], BOOL is_res_ref )
{
    double resolved = 0.0;

    switch (pid)
    {
    case XBF_PROP_STYLE:
        if (sval && sval[0]) uwp_apply_style_by_name( ctx, el, sval );
        return TRUE;
    case XBF_PROP_GRID_ROW: el->grid_row = (int)num; return TRUE;
    case XBF_PROP_GRID_COLUMN: el->grid_col = (int)num; return TRUE;
    case XBF_PROP_GRID_ROW_SPAN: el->grid_row_span = max( 1, (int)num ); return TRUE;
    case XBF_PROP_GRID_COLUMN_SPAN: el->grid_col_span = max( 1, (int)num ); return TRUE;
    case XBF_PROP_ROWDEF_HEIGHT:
    case XBF_PROP_COLUMNDEF_WIDTH:
        if (vt == 0x07) { el->grid_len_type = (int)vec4[0]; el->grid_len_val = vec4[1]; }
        else if (vt == 0x03 || vt == 0x04) { el->grid_len_type = 1; el->grid_len_val = num; }
        else if (is_res_ref && sval && sval[0])
        {
            struct uwp_rd_entry *de = NULL;
            if (xbf_rd_find_cached( ctx, ctx ? ctx->target_rd : NULL, el, sval, &de, NULL ) &&
                de && de->kind == UWP_RD_DOUBLE)
            {
                el->grid_len_type = 1;
                el->grid_len_val = de->dval;
            }
        }
        return TRUE;
    case XBF_PROP_COLUMNDEF_MAXWIDTH:
    case XBF_PROP_FRAMEWORKELEMENT_MAXWIDTH:
        if (vt == 0x03 || vt == 0x04) el->max_width = num;
        return TRUE;
    case XBF_PROP_COLUMNDEF_MINWIDTH:
        if (vt == 0x03 || vt == 0x04) el->min_width = num;
        return TRUE;
    case XBF_PROP_ROWDEF_MAXHEIGHT:
    case XBF_PROP_FRAMEWORKELEMENT_MAXHEIGHT:
        if (vt == 0x03 || vt == 0x04) el->max_height = num;
        return TRUE;
    case XBF_PROP_ROWDEF_MINHEIGHT:
        if (vt == 0x03 || vt == 0x04) el->min_height = num;
        return TRUE;
    case XBF_PROP_MARGIN:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_MARGIN )) return TRUE;
        if (vt == 0x06)
        {
            el->margin_l = vec4[0]; el->margin_t = vec4[1];
            el->margin_r = vec4[2]; el->margin_b = vec4[3];
            xbf_prop_mark_local( ctx, el, UWP_LP_MARGIN );
        }
        return TRUE;
    case XBF_PROP_CONTROL_PADDING:
    case XBF_PROP_BORDER_PADDING:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_PADDING )) return TRUE;
        if (vt == 0x06)
        {
            el->padding_l = vec4[0]; el->padding_t = vec4[1];
            el->padding_r = vec4[2]; el->padding_b = vec4[3];
            xbf_prop_mark_local( ctx, el, UWP_LP_PADDING );
        }
        return TRUE;
    case XBF_PROP_CONTROL_BORDER_THICKNESS:
    case XBF_PROP_BORDER_THICKNESS:
    case XBF_PROP_GRID_BORDER_THICKNESS:
    case XBF_PROP_STACKPANEL_BORDER_THICKNESS:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_BORDER_THICKNESS )) return TRUE;
        if (vt == 0x06)
        {
            el->border_thick_l = vec4[0]; el->border_thick_t = vec4[1];
            el->border_thick_r = vec4[2]; el->border_thick_b = vec4[3];
            xbf_prop_mark_local( ctx, el, UWP_LP_BORDER_THICKNESS );
        }
        else if (is_res_ref && sval && sval[0])
        {
            struct uwp_rd_entry *te = NULL;
            struct uwp_rd_bucket *hint_rd = ctx ? (ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd) : NULL;
            if (xbf_rd_find_cached( ctx, hint_rd, el, sval, &te, NULL ) && te && te->kind == UWP_RD_THICKNESS)
            {
                el->border_thick_l = te->vec4[0]; el->border_thick_t = te->vec4[1];
                el->border_thick_r = te->vec4[2]; el->border_thick_b = te->vec4[3];
                xbf_prop_mark_local( ctx, el, UWP_LP_BORDER_THICKNESS );
            }
        }
        return TRUE;
    case XBF_PROP_BORDER_CORNER_RADIUS:
    case XBF_PROP_GRID_CORNER_RADIUS:
    case XBF_PROP_STACKPANEL_CORNER_RADIUS:
    case XBF_PROP_CONTROL_CORNER_RADIUS:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_CORNER_RADIUS )) return TRUE;
        if (vt == 0x06)
        {
            el->corner_radius = max( max( vec4[0], vec4[1] ), max( vec4[2], vec4[3] ) );
            xbf_prop_mark_local( ctx, el, UWP_LP_CORNER_RADIUS );
        }
        else if (vt == 0x03 || vt == 0x04)
        {
            el->corner_radius = num;
            xbf_prop_mark_local( ctx, el, UWP_LP_CORNER_RADIUS );
        }
        else if (is_res_ref && sval && sval[0])
        {
            struct uwp_rd_entry *ce = NULL;
            struct uwp_rd_bucket *hint_rd = ctx ? (ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd) : NULL;
            if (xbf_rd_find_cached( ctx, hint_rd, el, sval, &ce, NULL ) && ce)
            {
                if (ce->kind == UWP_RD_THICKNESS)
                {
                    el->corner_radius = max( max( ce->vec4[0], ce->vec4[1] ), max( ce->vec4[2], ce->vec4[3] ) );
                    xbf_prop_mark_local( ctx, el, UWP_LP_CORNER_RADIUS );
                }
                else if (ce->kind == UWP_RD_DOUBLE && ce->dval > 0.0)
                {
                    el->corner_radius = ce->dval;
                    xbf_prop_mark_local( ctx, el, UWP_LP_CORNER_RADIUS );
                }
            }
        }
        else if (sval && sval[0])
        {
            double cr = wcstod( sval, NULL );
            if (cr > 0.0) { el->corner_radius = cr; xbf_prop_mark_local( ctx, el, UWP_LP_CORNER_RADIUS ); }
        }
        return TRUE;
    case XBF_PROP_RECTANGLE_RADIUS_X:
    case XBF_PROP_RECTANGLE_RADIUS_Y:
        if ((vt == 0x03 || vt == 0x04) && num > el->corner_radius)
            el->corner_radius = num;
        return TRUE;
    case XBF_PROP_CONTROL_HORIZONTAL_CONTENT_ALIGNMENT:
        el->h_content_align = xbf_parse_h_align( vt, num, sval ); el->has_h_content_align = TRUE; return TRUE;
    case XBF_PROP_CONTROL_VERTICAL_CONTENT_ALIGNMENT:
        el->v_content_align = xbf_parse_v_align( vt, num, sval ); el->has_v_content_align = TRUE; return TRUE;
    case XBF_PROP_CONTROL_IS_ENABLED:
        if (vt == 0x01 || vt == 0x02) el->is_enabled = (vt == 0x02);
        else if (sval && sval[0]) el->is_enabled = !wcsicmp( sval, L"True" );
        else el->is_enabled = (num != 0.0);
        return TRUE;
    case XBF_PROP_PROGRESS_RING_IS_ACTIVE:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_IS_ACTIVE )) return TRUE;
        if (vt == 0x01 || vt == 0x02) el->is_active = (vt == 0x02) ? 1 : 0;
        else if (sval && sval[0]) el->is_active = !wcsicmp( sval, L"True" ) ? 1 : 0;
        else el->is_active = (num != 0.0) ? 1 : 0;
        xbf_prop_mark_local( ctx, el, UWP_LP_IS_ACTIVE );
        return TRUE;
    case XBF_PROP_SELECTOR_ITEM_IS_SELECTED:
        if (vt == 0x01 || vt == 0x02) el->is_checked = el->is_selected = (vt == 0x02) ? 1 : 0;
        else if (sval && sval[0]) el->is_checked = el->is_selected = !wcsicmp( sval, L"True" ) ? 1 : 0;
        else el->is_checked = el->is_selected = (num != 0.0) ? 1 : 0;
        return TRUE;
    case XBF_PROP_WIDTH:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_WIDTH )) return TRUE;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved ))
        {
            el->width = resolved;
            xbf_prop_mark_local( ctx, el, UWP_LP_WIDTH );
        }
        return TRUE;
    case XBF_PROP_FRAMEWORKELEMENT_HEIGHT:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_HEIGHT )) return TRUE;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved ))
        {
            el->height = resolved;
            xbf_prop_mark_local( ctx, el, UWP_LP_HEIGHT );
        }
        return TRUE;
    case XBF_PROP_MIN_WIDTH:
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved )) el->min_width = resolved;
        return TRUE;
    case XBF_PROP_MIN_HEIGHT:
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved )) el->min_height = resolved;
        return TRUE;
    case XBF_PROP_HORIZONTAL_ALIGNMENT:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_H_ALIGN )) return TRUE;
        el->h_align = xbf_parse_h_align( vt, num, sval );
        xbf_prop_mark_local( ctx, el, UWP_LP_H_ALIGN );
        return TRUE;
    case XBF_PROP_VERTICAL_ALIGNMENT:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_V_ALIGN )) return TRUE;
        el->v_align = xbf_parse_v_align( vt, num, sval );
        xbf_prop_mark_local( ctx, el, UWP_LP_V_ALIGN );
        return TRUE;
    case XBF_PROP_ORIENTATION:
    case XBF_PROP_STACKPANEL_ORIENTATION: el->orientation = (int)num; return TRUE;
    case XBF_PROP_STACKPANEL_SPACING:
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved )) el->spacing = resolved;
        return TRUE;
    case XBF_PROP_GRID_COLUMN_SPACING:
    case XBF_PROP_GRID_ROW_SPACING:
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved ) && resolved >= 0.0)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa)
            {
                if (pid == XBF_PROP_GRID_COLUMN_SPACING) sa->grid_col_spacing = resolved;
                else sa->grid_row_spacing = resolved;
            }
        }
        return TRUE;
    case XBF_PROP_UIELEMENT_VISIBILITY_V2:
    case XBF_PROP_UIELEMENT_VISIBILITY:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_VISIBILITY )) return TRUE;
        if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
            el->visibility = !wcsicmp( sval, L"Collapsed" ) ? 1 : 0;
        else
            el->visibility = (int)num;
        xbf_prop_mark_local( ctx, el, UWP_LP_VISIBILITY );
        return TRUE;
    case XBF_PROP_UIELEMENT_OPACITY:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_OPACITY )) return TRUE;
        if (vt == 0x03 || vt == 0x04 || vt == 0x01 || vt == 0x02) { el->opacity = num; xbf_prop_mark_local( ctx, el, UWP_LP_OPACITY ); }
        else if ((vt == 0x05 || vt == 0x09) && sval && sval[0]) { el->opacity = wcstod( sval, NULL ); xbf_prop_mark_local( ctx, el, UWP_LP_OPACITY ); }
        return TRUE;
    case XBF_PROP_POPUP_HORIZONTAL_OFFSET:
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved ))
            el->popup_h_offset = resolved;
        return TRUE;
    default:
        return FALSE;
    }
}

struct uid_pri_apply_ctx
{
    struct uwp_element *el;
    void *ao;
    IInspectable *target;
};

static BOOL uid_prop_is_display_text( const WCHAR *prop )
{
    return !wcscmp( prop, L"Content" ) || !wcscmp( prop, L"Text" ) ||
           !wcscmp( prop, L"Title" ) || !wcscmp( prop, L"Header" ) ||
           !wcscmp( prop, L"Label" ) || !wcscmp( prop, L"PlaceholderText" ) ||
           !wcscmp( prop, L"ButtonText" ) || !wcscmp( prop, L"Glyph" );
}

static void xbf_uid_pri_prop_cb( const WCHAR *prop_rel, const WCHAR *val, void *user )
{
    struct uid_pri_apply_ctx *ac = user;
    struct uwp_element *el = ac->el;

    if (!prop_rel || !prop_rel[0] || !val || !val[0]) return;

    if (!wcsncmp( prop_rel, L"[using:", 7 ))
    {
        const WCHAR *rb = wcschr( prop_rel + 7, L']' );
        const WCHAR *sl = rb ? wcschr( rb + 1, L'/' ) : NULL;
        if (rb && sl && rb > prop_rel + 7 && sl > rb + 1 && sl[1])
        {
            WCHAR owner_cls[256];
            size_t ns_len = (size_t)(rb - (prop_rel + 7));
            size_t ty_len = (size_t)(sl - (rb + 1));
            const WCHAR *prop_name = sl + 1;

            if (ns_len + 1 + ty_len < ARRAY_SIZE(owner_cls))
            {
                memcpy( owner_cls, prop_rel + 7, ns_len * sizeof(WCHAR) );
                owner_cls[ns_len] = L'.';
                memcpy( owner_cls + ns_len + 1, rb + 1, ty_len * sizeof(WCHAR) );
                owner_cls[ns_len + 1 + ty_len] = 0;

                if (!wcscmp( owner_cls, L"Windows.UI.Xaml.Automation.AutomationProperties" ))
                {
                    if (!wcscmp( prop_name, L"Name" ))
                        uwp_elem_set_automation_name( el, val );
                }
                else if (!wcscmp( owner_cls, L"Windows.UI.Xaml.Controls.ToolTipService" ))
                {
                    /* ToolTip text should not replace visible control content */
                }
                else if (wcsncmp( owner_cls, L"Windows.UI.Xaml.", 16 ) != 0)
                {
                    if (ac->ao)
                        uwp_set_custom_xaml_member( ac->ao, owner_cls, prop_name, ac->target, val );
                    if (uid_prop_is_display_text( prop_name ) && !uwp_has_text( el ) &&
                        (!el->content || el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TEXT_BLOCK))
                        uwp_elem_set_text( el, val );
                }
            }
        }
        return;
    }

    if (!wcscmp( prop_rel, L"AutomationProperties.Name" ))
    {
        uwp_elem_set_automation_name( el, val );
        return;
    }
    if (!wcscmp( prop_rel, L"Tag" ))
    {
        IInspectable *boxed = box_string_value( val );
        if (boxed)
        {
            if (el->tag) IInspectable_Release( el->tag );
            el->tag = boxed;
            uwp_sync_all_template_presenters( el );
        }
        return;
    }
    if (!wcscmp( prop_rel, L"ToolTipService.ToolTip" ))
        return;
    if (!wcscmp( prop_rel, L"FontFamily" ))
    {
        uwp_elem_set_font_family( el, val );
        return;
    }
    if (uwp_is_calendar_date_picker( el ) && !wcscmp( prop_rel, L"Header" ))
    {
        struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
        if (ne) lstrcpynW( ne->cdp_header, val, ARRAY_SIZE(ne->cdp_header) );
        return;
    }
    if (uwp_is_calendar_date_picker( el ) && !wcscmp( prop_rel, L"PlaceholderText" ))
    {
        struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
        if (ne) lstrcpynW( ne->cdp_placeholder, val, ARRAY_SIZE(ne->cdp_placeholder) );
        if (!ne || !ne->cdp_has_date) uwp_elem_set_text( el, val );
        return;
    }
    if (uid_prop_is_display_text( prop_rel ))
    {
        if (!uwp_has_text( el ) && (!el->content || el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TEXT_BLOCK))
            uwp_elem_set_text( el, val );
    }
    if (el->outer && ac->ao && !(el->class_flags & UWP_CF_UWP_PREFIX) && !wcschr( prop_rel, L'/' ))
        uwp_set_custom_xaml_member( ac->ao, el->class_name, prop_rel, ac->target, val );
}

static void xbf_apply_uid_pri_lookup( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *sval )
{
    struct uid_pri_apply_ctx ac;

    uwp_elem_set_uid( el, sval );
    ac.el = el;
    ac.ao = (ctx && ctx->app_outer) ? ctx->app_outer : uwp_get_app_outer();
    ac.target = el->outer ? el->outer : &el->IInspectable_iface;
    uwp_pri_enum_uid_props( sval, xbf_uid_pri_prop_cb, &ac );
}

static void xbf_apply_known_text_and_control_prop( struct xbf_ctx *ctx, struct uwp_element *el,
                                                   WORD pid, int vt, double num, const WCHAR *sval,
                                                   BOOL is_res_ref )
{
    switch (pid)
    {
    case XBF_PROP_CONTENT:
    case XBF_PROP_FONTICON_GLYPH:
    case XBF_PROP_TEXTBLOCK_TEXT:
    case XBF_PROP_RUN_TEXT:
        if (sval && sval[0])
            uwp_elem_set_text( el, sval );
        break;
    case XBF_PROP_TEMPLATE_BINDING_PROPERTY:
        if ((el->class_flags2 & UWP_CF2_TEMPLATE_BINDING) && sval && sval[0])
            uwp_elem_set_text( el, sval );
        break;
    case XBF_PROP_BINDING_PATH:
        if ((el->class_flags2 & UWP_CF2_BINDING) && sval && sval[0])
            uwp_elem_set_text( el, sval );
        break;
    case XBF_PROP_BINDING_ELEMENT_NAME:
        if ((el->class_flags2 & UWP_CF2_BINDING) && sval && sval[0])
            uwp_elem_set_placeholder( el, sval );
        break;
    case XBF_PROP_BINDING_MODE:
        if (el->class_flags2 & UWP_CF2_BINDING)
        {
            if (sval && sval[0])
                el->binding_mode = !wcsicmp( sval, L"TwoWay" ) ? 3 : (!wcsicmp( sval, L"OneTime" ) ? 2 : 1);
            else
                el->binding_mode = (BYTE)(int)num;
        }
        break;
    case XBF_PROP_BINDING_CONVERTER:
        if ((el->class_flags2 & UWP_CF2_BINDING) && sval && sval[0])
        {
            struct uwp_rd_entry *ce = NULL;
            if (xbf_rd_find_cached( ctx, ctx ? ctx->target_rd : NULL, el, sval, &ce, NULL ) &&
                ce && ce->value)
            {
                struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
                if (sa) uwp_replace_iface( &sa->converter, ce->value );
            }
        }
        break;
    case XBF_PROP_BINDING_CONVERTER_LANGUAGE:
        if ((el->class_flags2 & UWP_CF2_BINDING) && sval && sval[0])
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa) lstrcpynW( sa->converter_lang, sval, ARRAY_SIZE(sa->converter_lang) );
        }
        break;
    case XBF_PROP_BINDING_CONVERTER_PARAMETER:
        if (el->class_flags2 & UWP_CF2_BINDING)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa)
            {
                IInspectable *boxed = NULL;
                if (sval && sval[0]) boxed = box_string_value( sval );
                else if (vt == 0x01 || vt == 0x02) boxed = box_bool_value( vt == 0x02 );
                else if (vt == 0x03) boxed = box_int32_value( (INT32)num );
                else if (vt == 0x04) boxed = box_double_value( num );
                if (boxed)
                {
                    uwp_replace_iface( &sa->converter_param, boxed );
                    IInspectable_Release( boxed );
                }
            }
        }
        break;
    case XBF_PROP_IMAGE_SOURCE:
        if (sval && sval[0])
        {
            struct uwp_media_ext *m = uwp_elem_ensure_media( el );
            if (m) lstrcpynW( m->image_src, sval, ARRAY_SIZE(m->image_src) );
        }
        break;
    case XBF_PROP_IMAGE_STRETCH:
    case XBF_PROP_TILEBRUSH_STRETCH:
        if (vt == 0x04 || vt == 0x0b)
        {
            struct uwp_media_ext *m = uwp_elem_ensure_media( el );
            if (m) m->media_stretch = (int)num;
        }
        break;
    case XBF_PROP_AUTOMATION_NAME:
        if (sval && sval[0]) uwp_elem_set_automation_name( el, sval );
        break;
    case XBF_PROP_AUTOMATION_ID:
        if (sval && sval[0]) xbf_apply_uid_pri_lookup( ctx, el, sval );
        break;
    case XBF_PROP_FRAMEWORKELEMENT_TAG:
    {
        IInspectable *boxed = NULL;
        if (sval && sval[0]) boxed = box_string_value( sval );
        else if (vt == 0x01 || vt == 0x02) boxed = box_bool_value( vt == 0x02 );
        else if (vt == 0x03) boxed = box_int32_value( (INT32)num );
        else if (vt == 0x04) boxed = box_double_value( num );
        if (boxed)
        {
            if (el->tag) IInspectable_Release( el->tag );
            el->tag = boxed;
            uwp_sync_all_template_presenters( el );
        }
        break;
    }
    case XBF_PROP_TEXTELEMENT_FONT_FAMILY:
    case XBF_PROP_CONTENTPRESENTER_FONT_FAMILY:
    case XBF_PROP_CONTROL_FONT_FAMILY:
    case XBF_PROP_TEXTBLOCK_FONT_FAMILY:
    case XBF_PROP_FONTICON_FONT_FAMILY:
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_FONT_FAMILY )) break;
        if (sval && sval[0])
        {
            WCHAR face[64] = {0};
            const WCHAR *ff_spec = sval;
            if (is_res_ref || !wcschr( sval, L'#' ))
            {
                struct uwp_rd_entry *fe = NULL;
                if (xbf_rd_find_cached( ctx, ctx ? ctx->target_rd : NULL, el, sval, &fe, NULL ) &&
                    fe && (fe->kind == UWP_RD_FONT_FAMILY || fe->kind == UWP_RD_STRING) && fe->str_val[0])
                    ff_spec = fe->str_val;
            }
            uwp_elem_set_font_family( el, ff_spec );
            uwp_resolve_font_family( ff_spec, face, ARRAY_SIZE(face) );
            xbf_prop_mark_local( ctx, el, UWP_LP_FONT_FAMILY );
        }
        break;
    case XBF_PROP_CONTROL_FONTSIZE:
    case XBF_PROP_TEXTBLOCK_FONTSIZE:
    case XBF_PROP_TEXTELEMENT_FONTSIZE:
    case XBF_PROP_FONTICON_FONTSIZE:
    {
        double fs = 0.0;
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_FONT_SIZE )) break;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &fs ) && fs > 0.0)
        {
            el->font_size = fs;
            el->has_font_size = TRUE;
            xbf_prop_mark_local( ctx, el, UWP_LP_FONT_SIZE );
        }
        break;
    }
    case XBF_PROP_TEXTBLOCK_FONT_WEIGHT:
    case XBF_PROP_CONTROL_FONT_WEIGHT:
    case XBF_PROP_TEXTELEMENT_FONT_WEIGHT:
    {
        int fw;
        if (xbf_prop_blocked_by_local( ctx, el, UWP_LP_FONT_WEIGHT )) break;
        fw = uwp_parse_font_weight( sval, num, vt );
        if (fw > 0)
        {
            el->font_weight = fw;
            xbf_prop_mark_local( ctx, el, UWP_LP_FONT_WEIGHT );
        }
        break;
    }
    case XBF_PROP_TEXTBLOCK_TEXT_ALIGNMENT:
        el->text_align = xbf_parse_text_align( vt, num, sval ); el->has_text_align = TRUE; break;
    case XBF_PROP_FLOW_DIRECTION:
        if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
            el->flow_direction = !wcsicmp( sval, L"RightToLeft" ) ? 2 : 1;
        else
            el->flow_direction = ((int)num == 1) ? 2 : 1;
        break;
    case XBF_PROP_TEXTBLOCK_TEXT_WRAPPING:
    case XBF_PROP_RICHTEXTBLOCK_TEXT_WRAPPING:
        if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
        {
            if (!wcsicmp( sval, L"NoWrap" )) el->text_wrapping = 1;
            else if (!wcsicmp( sval, L"WrapWholeWords" )) el->text_wrapping = 3;
            else if (!wcsicmp( sval, L"Wrap" )) el->text_wrapping = 2;
        }
        else el->text_wrapping = (int)num;
        break;
    case XBF_PROP_TEXTBLOCK_MAX_LINES:
    case XBF_PROP_RICHTEXTBLOCK_MAX_LINES:
        if (vt == 0x03 || vt == 0x04) el->max_lines = max( 0, (int)num );
        else if (sval && sval[0]) el->max_lines = max( 0, (int)wcstol( sval, NULL, 10 ) );
        break;
    case XBF_PROP_STORYBOARD_TARGET_NAME:
        if (sval && sval[0])
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa) lstrcpynW( sa->anim_target_name, sval, ARRAY_SIZE(sa->anim_target_name) );
        }
        break;
    case XBF_PROP_STORYBOARD_TARGET_PROPERTY:
        if (sval && sval[0])
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa) vsm_clean_prop_name( sval, sa->anim_target_prop, ARRAY_SIZE(sa->anim_target_prop) );
        }
        break;
    case XBF_PROP_DOUBLE_ANIMATION_TO:
    {
        double to_val = 0.0;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &to_val ))
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa) { sa->anim_to = to_val; sa->anim_has_to = TRUE; }
        }
        break;
    }
    case XBF_PROP_ACCELERATOR_KEY:
        el->accel_key = (UINT)num;
        uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
        break;
    case XBF_PROP_ACCELERATOR_MODIFIERS:
        el->accel_mods = (UINT)num;
        uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
        break;
    case XBF_PROP_ACCELERATOR_IS_ENABLED:
        el->accel_enabled = (num != 0.0);
        uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
        break;
    case XBF_PROP_ITEMS_CONTROL_DISPLAY_MEMBER_PATH:
        if (sval && sval[0])
        {
            struct uwp_nav_ext *ne = uwp_elem_ensure_nav( el );
            if (ne) lstrcpynW( ne->display_member_path, sval, ARRAY_SIZE(ne->display_member_path) );
        }
        break;
    case XBF_PROP_SYMBOL_ICON_SYMBOL:
        if ((vt == 0x04 || vt == 0x0b) && (int)num >= 0xE000 && (int)num <= 0xFFFF)
        {
            WCHAR sym = (WCHAR)(int)num;
            uwp_elem_set_text_n( el, &sym, 1 );
            if (!uwp_elem_font_family( el )[0])
                uwp_elem_set_font_family( el, L"Segoe MDL2 Assets" );
        }
        break;
    case XBF_PROP_TOGGLE_BUTTON_IS_CHECKED:
    case XBF_PROP_TOGGLE_MENU_FLYOUT_ITEM_IS_CHECKED:
        if (vt == 0x01 || vt == 0x02) el->is_checked = (vt == 0x02) ? 1 : 0;
        else if (sval && sval[0]) el->is_checked = !wcsicmp( sval, L"True" ) ? 1 : 0;
        else el->is_checked = (num != 0.0) ? 1 : 0;
        break;
    }
}

static void xbf_apply_custom_prop( struct xbf_ctx *ctx, struct uwp_element *el,
                                   const struct xbf_prop_info *pi, int vt,
                                   double num, const WCHAR *sval, BOOL is_res_ref )
{
    if (!wcscmp( pi->name, L"IsChecked" ) || !wcscmp( pi->name, L"IsSelected" ))
    {
        if (vt == 0x01 || vt == 0x02) el->is_checked = (vt == 0x02) ? 1 : 0;
        else if (sval && sval[0]) el->is_checked = !wcsicmp( sval, L"True" ) ? 1 : 0;
        else el->is_checked = (num != 0.0) ? 1 : 0;
    }
    xbf_apply_nav_prop_name( ctx, el, pi->name, vt, num, sval, is_res_ref, NULL, NULL );

    if (!wcscmp( pi->name, L"Name" ) && sval && sval[0] && !el->name[0])
    {
        lstrcpynW( el->name, sval, ARRAY_SIZE(el->name) );
    }
    if (el->kind == UWP_KIND_GRID && (!wcscmp( pi->name, L"ColumnSpacing" ) || !wcscmp( pi->name, L"RowSpacing" )))
    {
        double sp = 0.0;
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &sp ) && sp >= 0.0)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa)
            {
                if (!wcscmp( pi->name, L"ColumnSpacing" )) sa->grid_col_spacing = sp;
                else sa->grid_row_spacing = sp;
            }
        }
    }
    if (!wcscmp( pi->name, L"Text" ) && sval && sval[0] &&
        (el->kind == UWP_KIND_TEXT_BLOCK || (el->class_flags & (UWP_CF_RUN | UWP_CF_INLINE))))
    {
        uwp_elem_set_text( el, sval );
    }
    else if ((!wcscmp( pi->name, L"Source" ) || !wcscmp( pi->name, L"UriSource" ) || !wcscmp( pi->name, L"ImageSource" )) &&
             sval && sval[0] && !is_res_ref)
    {
        struct uwp_media_ext *m = uwp_elem_ensure_media( el );
        if (m) lstrcpynW( m->image_src, sval, ARRAY_SIZE(m->image_src) );
    }
    else if (!wcscmp( pi->name, L"Stretch" ))
    {
        struct uwp_media_ext *m = uwp_elem_ensure_media( el );
        if (m)
        {
            if (vt == 0x04 || vt == 0x0b) m->media_stretch = (int)num;
            else if (sval && sval[0])
            {
                if (!wcsicmp( sval, L"None" )) m->media_stretch = 0;
                else if (!wcsicmp( sval, L"Fill" )) m->media_stretch = 1;
                else if (!wcsicmp( sval, L"Uniform" )) m->media_stretch = 2;
                else if (!wcsicmp( sval, L"UniformToFill" )) m->media_stretch = 3;
            }
        }
    }
    else if (!wcscmp( pi->name, L"FontFamily" ) && sval && sval[0])
    {
        if (!xbf_prop_blocked_by_local( ctx, el, UWP_LP_FONT_FAMILY ))
        {
            WCHAR face[64] = {0};
            const WCHAR *ff_spec = sval;
            struct uwp_rd_entry *fe = NULL;
            if ((is_res_ref || !wcschr( sval, L'#' )) &&
                xbf_rd_find_cached( ctx, ctx->target_rd, el, sval, &fe, NULL ) &&
                fe && (fe->kind == UWP_RD_FONT_FAMILY || fe->kind == UWP_RD_STRING) && fe->str_val[0])
                ff_spec = fe->str_val;
            uwp_elem_set_font_family( el, ff_spec );
            uwp_resolve_font_family( ff_spec, face, ARRAY_SIZE(face) );
            xbf_prop_mark_local( ctx, el, UWP_LP_FONT_FAMILY );
        }
    }
    else if (is_res_ref && sval && sval[0] && pi->name[0] &&
             pi->owner_tid < ctx->type_cnt && ctx->types[pi->owner_tid].full_name[0] &&
             wcsncmp( ctx->types[pi->owner_tid].full_name, L"Windows.UI.Xaml.", 16 ) != 0)
    {
        void *ao = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();
        struct uwp_rd_entry *re = NULL;
        struct uwp_rd_bucket *found_b = NULL;
        if (ao && xbf_rd_find_cached( ctx, ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd, el, sval, &re, &found_b ) && re)
        {
            const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
            IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;
            if (re->kind == UWP_RD_DOUBLE)
            {
                WCHAR nbuf[64];
                swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", re->dval );
                uwp_set_custom_xaml_member( ao, owner_cls, pi->name, target, nbuf );
            }
            else
            {
                IInspectable *res_val = uwp_rd_materialize_entry( found_b, re );
                if (res_val) uwp_set_custom_xaml_member_obj( ao, owner_cls, pi->name, target, res_val );
            }
        }
    }
    else if (pi->name[0] && pi->owner_tid < ctx->type_cnt &&
             ctx->types[pi->owner_tid].full_name[0] &&
             wcsncmp( ctx->types[pi->owner_tid].full_name, L"Windows.UI.Xaml.", 16 ) != 0 &&
             (vt == 0x01 || vt == 0x02 || vt == 0x03 || vt == 0x04 || vt == 0x0b || vt == 0x0c ||
              ((vt == 0x05 || vt == 0x09) && sval && sval[0])))
    {
        void *ao = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();
        if (ao)
        {
            const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
            IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;
            BOOL is_nav_pane_mode = (!wcscmp( owner_cls, L"Microsoft.UI.Xaml.Controls.NavigationView" ) &&
                                     !wcscmp( pi->name, L"PaneDisplayMode" ));
            if (vt == 0x01 || vt == 0x02)
                uwp_set_custom_xaml_member( ao, owner_cls, pi->name, target, (vt == 0x02) ? L"True" : L"False" );
            else if (vt == 0x05 || vt == 0x09)
            {
                if (sval && sval[0])
                    uwp_set_custom_xaml_member( ao, owner_cls, pi->name, target, sval );
            }
            else if (is_nav_pane_mode)
            {
                static const WCHAR *const modes[] = { L"Auto", L"Left", L"Top", L"LeftCompact", L"LeftMinimal" };
                int m = (int)num;
                uwp_set_custom_xaml_member( ao, owner_cls, pi->name, target, (m >= 0 && m < 5) ? modes[m] : L"Auto" );
            }
            else
            {
                WCHAR nbuf[64];
                swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", num );
                uwp_set_custom_xaml_member( ao, owner_cls, pi->name, target, nbuf );
            }
        }
    }
}

static void xbf_apply_item_template_res_ref( struct xbf_ctx *ctx, struct uwp_element *el,
                                             WORD pid, const WCHAR *sval )
{
    BOOL is_sel = FALSE;
    struct uwp_rd_entry *re = NULL;
    struct uwp_rd_bucket *found_b = NULL;
    IInspectable *res_val;

    if (!el || !sval || !sval[0] || !xbf_prop_is_item_template( pid, ctx, &is_sel )) return;
    if (!xbf_rd_find_cached( ctx, ctx ? (ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd) : NULL,
                             el, sval, &re, &found_b ) || !re)
        return;
    res_val = uwp_rd_materialize_entry( found_b, re );
    if (!res_val) return;
    if (is_sel)
    {
        if (el->item_template_selector) IInspectable_Release( el->item_template_selector );
        el->item_template_selector = res_val;
        IInspectable_AddRef( res_val );
    }
    else
    {
        if (el->item_template) IInspectable_Release( el->item_template );
        el->item_template = res_val;
        IInspectable_AddRef( res_val );
    }
    TRACE( "xbf_apply_property: %s (%s) pid=%#x -> res %s (%p)\n",
           debugstr_w(el->class_name), debugstr_w(el->name), pid, debugstr_w(sval), res_val );
}

void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                         WORD pid, int vt, double num, const WCHAR *sval,
                         const double vec4[4], BOOL is_res_ref )
{
    if (sv)
        xbf_apply_stack_val_prop( ctx, el, sv, pid, vt, num, sval, is_res_ref );
    if (!el) return;
    if (xbf_apply_brush_or_grad_prop( ctx, el, pid, vt, num, sval )) return;

    if (pid & 0x8000)
    {
        if (!xbf_apply_known_layout_prop( ctx, el, pid, vt, num, sval, vec4, is_res_ref ))
            xbf_apply_known_text_and_control_prop( ctx, el, pid, vt, num, sval, is_res_ref );
    }
    else if (ctx && pid < ctx->prop_cnt)
    {
        xbf_apply_custom_prop( ctx, el, &ctx->props[pid], vt, num, sval, is_res_ref );
    }

    if (is_res_ref)
        xbf_apply_item_template_res_ref( ctx, el, pid, sval );
}

static void xbf_parse_style_setter_sub( struct xbf_ctx *ctx, int sub_idx, DWORD off, WORD target_pid,
                                        struct xbf_stack_val *style_sv )
{
    DWORD sub_start = 0, end_p = 0, sub_sz = 0, p;
    if (!ctx || !style_sv || sub_idx < 0) return;
    if (!xbf_get_substream_bounds( ctx, (DWORD)sub_idx, &sub_start, &end_p, &sub_sz )) return;
    if (off >= sub_sz) return;
    p = sub_start + off;
    if (p < end_p)
    {
        BYTE op = ctx->nodes[p++];
        if ((op == 0x24 || op == 0x1e || op == 0x1a || op == 0x1b) && p + 2 <= end_p)
        {
            WORD vpid = *(const WORD *)(ctx->nodes + p);
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            (void)vpid;
            p += 2;
            if (xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
            {
                xbf_stack_val_add_setter( style_sv, target_pid, vt, (op == 0x24 || op == 0x1e), num, sbuf, vec4 );
                if (style_sv->setter_cnt > 0)
                    xbf_setter_fill_prop_meta( ctx, &style_sv->setters[style_sv->setter_cnt - 1], target_pid );
            }
        }
        else if ((op == 0x15 || op == 0x16) && p + 2 <= end_p)
        {
            WORD tid = *(const WORD *)(ctx->nodes + p);
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            (void)tid;
            p += 2;
            if (xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
            {
                xbf_stack_val_add_setter( style_sv, target_pid, vt, FALSE, num, sbuf, vec4 );
                if (style_sv->setter_cnt > 0)
                    xbf_setter_fill_prop_meta( ctx, &style_sv->setters[style_sv->setter_cnt - 1], target_pid );
            }
        }
        else if (op == 0x14 && p + 2 <= end_p)
        {
            WORD tid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (tid == XBF_TYPE_CONTROL_TEMPLATE)
            {
                while (p < end_p)
                {
                    BYTE sop = ctx->nodes[p++];
                    if (sop == 0x21 || sop == 0x02) break;
                    if (sop == 0x1d && p + 4 <= end_p)
                    {
                        p += 4;
                    }
                    else if (sop == 0x11 && p + 2 <= end_p)
                    {
                        DWORD sub_i = 0, c1 = 0, c2 = 0;
                        p += 2; /* pid (XBF_PROP_FRAMEWORK_TEMPLATE_TEMPLATE) */
                        if (!xbf_read_uleb( ctx->nodes, &p, end_p, &sub_i )) break;
                        if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c1 )) break;
                        if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c2 )) break;
                        if (c1 <= (end_p - p) / 2 && c2 <= (end_p - p) / 2 - c1)
                            p += (c1 + c2) * 2;
                        style_sv->template_xbf_data = ctx->xbf;
                        style_sv->template_xbf_size = ctx->xbf_sz;
                        style_sv->template_sub_idx = sub_i;
                        TRACE( "xbf_parse_style_setter_sub: captured ControlTemplate sub=%u\n", (unsigned)sub_i );
                    }
                    else if ((sop == 0x1a || sop == 0x1b || sop == 0x1e || sop == 0x24) && p + 2 <= end_p)
                    {
                        int vt = 0; double num = 0;
                        p += 2;
                        if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, NULL, 0, NULL )) break;
                    }
                    else break;
                }
            }
            else if (tid == XBF_TYPE_BRUSH_C) /* inline SolidColorBrush */
            {
                UINT32 argb = 0;
                BOOL has_c = FALSE;
                WCHAR cref[128] = {0};
                while (p < end_p)
                {
                    BYTE sop = ctx->nodes[p++];
                    if (sop == 0x21 || sop == 0x02) break;
                    if ((sop == 0x1a || sop == 0x1e || sop == 0x24) && p + 2 <= end_p)
                    {
                        WORD spid = *(const WORD *)(ctx->nodes + p);
                        int vt = 0;
                        double num = 0;
                        WCHAR sbuf[128] = {0};
                        p += 2;
                        if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
                        if (spid == XBF_PROP_SOLIDCOLORBRUSH_COLOR)
                        {
                            if (vt == 0x08) { argb = (UINT32)num; has_c = TRUE; }
                            else if (sbuf[0]) lstrcpynW( cref, sbuf, ARRAY_SIZE(cref) );
                        }
                    }
                    else break;
                }
                if (has_c)
                    xbf_stack_val_add_setter( style_sv, target_pid, 0x08, FALSE, (double)argb, NULL, NULL );
                else if (cref[0])
                    xbf_stack_val_add_setter( style_sv, target_pid, 0x05, TRUE, 0, cref, NULL );
            }
        }
    }
}

BOOL xbf_read_vsm_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                                       DWORD def_sub_sz, int *io_prop_sp )
{
    DWORD p = *io_p, n_states = 0, s_idx;
    if (!xbf_read_uleb( nodes, &p, end_p, &n_states ) || n_states > end_p - p || n_states > def_sub_sz)
        return FALSE;
    for (s_idx = 0; s_idx < n_states; s_idx++)
    {
        DWORD st_off = 0;
        if (!xbf_read_uleb( nodes, &p, end_p, &st_off ) || st_off > def_sub_sz)
            return FALSE;
    }
    /* Find VisualStateGroups footer: 00 00 01 <cnt_uleb> <cnt*u16> 02
     * (preceded by 00 02 when no transitions, or transition metadata like 0f 05). */
    while (p < end_p)
    {
        if (end_p - p >= 5 &&
            nodes[p] == 0x00 && nodes[p + 1] == 0x00 &&
            nodes[p + 2] == 0x01)
        {
            DWORD q = p + 3, cnt = 0;
            if (xbf_read_uleb( nodes, &q, end_p, &cnt ) &&
                cnt <= (end_p - q) / 2 &&
                q + cnt * 2 < end_p && nodes[q + cnt * 2] == 0x02)
            {
                DWORD next_p = q + cnt * 2 + 1;
                if (next_p == end_p ||
                    nodes[next_p] == 0x13 || nodes[next_p] == 0x14 ||
                    nodes[next_p] == 0x15 || nodes[next_p] == 0x16 ||
                    nodes[next_p] == 0x17 || nodes[next_p] == 0x21 ||
                    nodes[next_p] == 0x02 || nodes[next_p] == 0x07 ||
                    nodes[next_p] == 0x08 || nodes[next_p] == 0x0a ||
                    nodes[next_p] == 0x0f || nodes[next_p] == 0x1a ||
                    nodes[next_p] == 0x1b || nodes[next_p] == 0x1d ||
                    nodes[next_p] == 0x20 || nodes[next_p] == 0x26)
                {
                    p = next_p;
                    if (io_prop_sp && *io_prop_sp > 0) (*io_prop_sp)--;
                    *io_p = p;
                    return TRUE;
                }
            }
        }
        p++;
    }
    *io_p = p;
    return TRUE;
}

BOOL xbf_read_style_defer_table( struct xbf_ctx *ctx, DWORD *io_p, DWORD end_p,
                                        DWORD def_sub, DWORD def_sub_sz,
                                        struct xbf_stack_val *style_sv, int *io_prop_sp )
{
    DWORD p = *io_p, scnt = 0, si;
    if (!xbf_read_uleb( ctx->nodes, &p, end_p, &scnt ) || scnt > (end_p - p) / 3)
        return FALSE;
    for (si = 0; si < scnt; si++)
    {
        BYTE sop;
        if (p >= end_p) return FALSE;
        sop = ctx->nodes[p];
        if (sop == 0x30)
        {
            WORD spid;
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            if (end_p - p < 3) return FALSE;
            spid = *(const WORD *)(ctx->nodes + p + 1);
            p += 3;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
                return FALSE;
            xbf_stack_val_add_setter( style_sv, spid, vt, FALSE, num, sbuf, vec4 );
            if (style_sv && style_sv->setter_cnt > 0)
                xbf_setter_fill_prop_meta( ctx, &style_sv->setters[style_sv->setter_cnt - 1], spid );
        }
        else if (sop == 0x11 || sop == 0x12 || sop == 0x18)
        {
            WORD spid;
            DWORD soff = 0;
            if (end_p - p < 3) return FALSE;
            spid = *(const WORD *)(ctx->nodes + p + 1);
            p += 3;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &soff ) || soff > def_sub_sz)
                return FALSE;
            if ((sop == 0x11 || sop == 0x12 || sop == 0x18) && soff < def_sub_sz)
                xbf_parse_style_setter_sub( ctx, (int)def_sub, soff, spid, style_sv );
        }
        else if (sop == 0x20)
        {
            WORD stid, spid;
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            if (end_p - p < 5) return FALSE;
            stid = *(const WORD *)(ctx->nodes + p + 1);
            spid = *(const WORD *)(ctx->nodes + p + 3);
            p += 5;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
                return FALSE;
            xbf_stack_val_add_setter( style_sv, 0, vt, FALSE, num, sbuf, vec4 );
            if (style_sv && style_sv->setter_cnt > 0)
            {
                struct uwp_style_setter *st = &style_sv->setters[style_sv->setter_cnt - 1];
                if (stid < ctx->str_cnt && ctx->strs[stid] && ctx->strs[stid][0])
                    lstrcpynW( st->prop_name, ctx->strs[stid], ARRAY_SIZE(st->prop_name) );
                if (spid < ctx->type_cnt && ctx->types[spid].full_name[0])
                    lstrcpynW( st->owner_cls, ctx->types[spid].full_name, ARRAY_SIZE(st->owner_cls) );
            }
        }
        else if (sop == 0x01 || sop == 0x02)
        {
            WORD stid, spid;
            DWORD soff = 0;
            if (end_p - p < 5) return FALSE;
            stid = *(const WORD *)(ctx->nodes + p + 1);
            spid = *(const WORD *)(ctx->nodes + p + 3);
            p += 5;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &soff ) || soff > def_sub_sz)
                return FALSE;
            if (soff < def_sub_sz)
            {
                int prev_cnt = style_sv ? style_sv->setter_cnt : 0;
                xbf_parse_style_setter_sub( ctx, (int)def_sub, soff, spid, style_sv );
                if (style_sv && style_sv->setter_cnt > prev_cnt)
                {
                    struct uwp_style_setter *st = &style_sv->setters[style_sv->setter_cnt - 1];
                    if (stid < ctx->str_cnt && ctx->strs[stid] && ctx->strs[stid][0])
                        lstrcpynW( st->prop_name, ctx->strs[stid], ARRAY_SIZE(st->prop_name) );
                    if (spid < ctx->type_cnt && ctx->types[spid].full_name[0])
                        lstrcpynW( st->owner_cls, ctx->types[spid].full_name, ARRAY_SIZE(st->owner_cls) );
                }
            }
        }
        else
        {
            break;
        }
    }
    while (p < end_p)
    {
        if (ctx->nodes[p] == 0x21 ||
            (ctx->nodes[p] == 0x02 && (p + 1 == end_p || ctx->nodes[p + 1] == 0x13 || ctx->nodes[p + 1] == 0x14)))
        {
            if (ctx->nodes[p] == 0x02)
            {
                p++;
                if (io_prop_sp && *io_prop_sp > 0) (*io_prop_sp)--;
            }
            break;
        }
        p++;
    }
    *io_p = p;
    return TRUE;
}

static BOOL xbf_rd_table_push( struct xbf_rd_table *tbl, WORD sid, DWORD off )
{
    if (!tbl->items)
    {
        tbl->items = tbl->stack_items;
        tbl->cap = ARRAY_SIZE(tbl->stack_items);
        tbl->heap_owned = FALSE;
    }
    if (tbl->count >= tbl->cap)
    {
        int ncap = tbl->cap ? tbl->cap * 2 : 128;
        struct xbf_rd_map_item *ni;
        if (!tbl->heap_owned)
        {
            ni = malloc( (size_t)ncap * sizeof(*ni) );
            if (!ni) return FALSE;
            memcpy( ni, tbl->items, (size_t)tbl->count * sizeof(*ni) );
            tbl->heap_owned = TRUE;
        }
        else
        {
            ni = realloc( tbl->items, (size_t)ncap * sizeof(*ni) );
            if (!ni) return FALSE;
        }
        tbl->items = ni;
        tbl->cap = ncap;
    }
    tbl->items[tbl->count].key_sid = sid;
    tbl->items[tbl->count].sub_off = off;
    tbl->count++;
    return TRUE;
}

BOOL xbf_read_rd_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
                                      DWORD def_sub_sz, struct xbf_rd_table *tbl, int *io_prop_sp )
{
    DWORD p = *io_p, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0, c6 = 0, ri, ki;

    if (!xbf_read_uleb( nodes, &p, end_p, &c1 ) || c1 >= 10000 || c1 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c1; ri++)
    {
        WORD sid;
        DWORD off = 0;
        if (end_p - p < 2) return FALSE;
        sid = *(const WORD *)(nodes + p);
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &off ) || off > def_sub_sz)
            return FALSE;
        if (!xbf_rd_table_push( tbl, sid, off ))
            return FALSE;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c2 ) || c2 > (end_p - p) / 2)
        return FALSE;
    p += c2 * 2;

    if (!xbf_read_uleb( nodes, &p, end_p, &c3 ) || c3 >= 10000 || c3 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c3; ri++)
    {
        WORD sid;
        DWORD off = 0;
        if (end_p - p < 2) return FALSE;
        sid = *(const WORD *)(nodes + p);
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &off ) || off > def_sub_sz)
            return FALSE;
        if (!xbf_rd_table_push( tbl, sid, off ))
            return FALSE;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c4 ) || c4 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c4; ri++)
    {
        DWORD kc = 0, dummy = 0;
        if (end_p - p < 2) return FALSE;
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &kc ) || kc > end_p - p)
            return FALSE;
        for (ki = 0; ki < kc; ki++)
            if (!xbf_read_uleb( nodes, &p, end_p, &dummy )) return FALSE;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c5 ) || c5 > (end_p - p) / 3)
        return FALSE;
    for (ri = 0; ri < c5; ri++)
    {
        DWORD kc = 0, dummy = 0;
        if (end_p - p < 2) return FALSE;
        p += 2;
        if (!xbf_read_uleb( nodes, &p, end_p, &kc ) || kc > end_p - p)
            return FALSE;
        for (ki = 0; ki < kc; ki++)
            if (!xbf_read_uleb( nodes, &p, end_p, &dummy )) return FALSE;
    }

    if (!xbf_read_uleb( nodes, &p, end_p, &c6 ) || c6 > (end_p - p) / 6)
        return FALSE;
    for (ri = 0; ri < c6; ri++)
    {
        DWORD dummy = 0;
        if (!xbf_read_uleb( nodes, &p, end_p, &dummy ) || end_p - p < 5)
            return FALSE;
        p += 5;
    }

    if (p < end_p && nodes[p] == 0x02)
    {
        p++;
        if (io_prop_sp && *io_prop_sp > 0) (*io_prop_sp)--;
    }
    *io_p = p;
    return TRUE;
}

