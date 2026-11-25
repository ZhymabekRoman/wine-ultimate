/* WinRT Windows.UI.Xaml VisualStateManager, Storyboard, VisualTreeHelper & Static Factories
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

#include <stdlib.h>
#include <string.h>
#include "private.h"
#include "weakreference.h"
#include "uwp_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

enum uwp_vsm_prop_kind
{
    UWP_VSM_PROP_NONE = 0,
    UWP_VSM_PROP_GRID_LEN,
    UWP_VSM_PROP_WIDTH,
    UWP_VSM_PROP_HEIGHT,
    UWP_VSM_PROP_MIN_WIDTH,
    UWP_VSM_PROP_MAX_WIDTH,
    UWP_VSM_PROP_MIN_HEIGHT,
    UWP_VSM_PROP_MAX_HEIGHT,
    UWP_VSM_PROP_VISIBILITY,
    UWP_VSM_PROP_IS_ENABLED,
    UWP_VSM_PROP_GRID_ROW,
    UWP_VSM_PROP_GRID_ROW_SPAN,
    UWP_VSM_PROP_GRID_COL,
    UWP_VSM_PROP_GRID_COL_SPAN,
    UWP_VSM_PROP_FONT_SIZE,
    UWP_VSM_PROP_OPACITY,
    UWP_VSM_PROP_MARGIN,
    UWP_VSM_PROP_PADDING,
    UWP_VSM_PROP_H_ALIGN,
    UWP_VSM_PROP_V_ALIGN,
    UWP_VSM_PROP_TEXT_ALIGN,
    UWP_VSM_PROP_MAX_LINES,
    UWP_VSM_PROP_BG,
    UWP_VSM_PROP_FG,
    UWP_VSM_PROP_ORIENTATION,
    UWP_VSM_PROP_ITEM_TPL_SEL,
    UWP_VSM_PROP_ITEM_TPL,
};

struct uwp_vsm_snapshot_entry
{
    struct uwp_element *target;
    enum uwp_vsm_prop_kind prop_kind;
    int old_int;
    double old_num;
    double old_vec4[4];
    UINT32 old_argb;
    COLORREF old_cr;
    BYTE old_alpha;
    BOOL old_flag;
    IInspectable *old_obj;
};

void uwp_vsm_invalidate_eval_chain( struct uwp_element *el )
{
    int depth = 0;
    while (el && depth++ < 32)
    {
        el->vsm_eval_valid = FALSE;
        if (el->owner_component && el->owner_component != el)
            el->owner_component->vsm_eval_valid = FALSE;
        if (el->parent == el) break;
        el = el->parent;
    }
}

struct uwp_vsm_group *uwp_vsm_add_group( struct uwp_element *el, const WCHAR *name )
{
    struct uwp_vsm_group *g;
    if (!el) return NULL;
    if (el->vsm_group_cnt >= el->vsm_group_cap)
    {
        int ncap = el->vsm_group_cap ? el->vsm_group_cap * 2 : 4;
        struct uwp_vsm_group *ng = realloc( el->vsm_groups, (size_t)ncap * sizeof(*ng) );
        if (!ng) return NULL;
        el->vsm_groups = ng;
        el->vsm_group_cap = ncap;
    }
    g = &el->vsm_groups[el->vsm_group_cnt++];
    memset( g, 0, sizeof(*g) );
    g->active_state_idx = -1;
    if (name && name[0]) lstrcpynW( g->name, name, ARRAY_SIZE(g->name) );
    uwp_vsm_invalidate_eval_chain( el );
    return g;
}

struct uwp_vsm_state *uwp_vsm_add_state( struct uwp_vsm_group *g, const WCHAR *name )
{
    struct uwp_vsm_state *st;
    if (!g) return NULL;
    if (g->state_cnt >= g->state_cap)
    {
        int ncap = g->state_cap ? g->state_cap * 2 : 8;
        struct uwp_vsm_state *ns = realloc( g->states, (size_t)ncap * sizeof(*ns) );
        if (!ns) return NULL;
        g->states = ns;
        g->state_cap = ncap;
    }
    st = &g->states[g->state_cnt++];
    memset( st, 0, sizeof(*st) );
    if (name && name[0]) lstrcpynW( st->name, name, ARRAY_SIZE(st->name) );
    return st;
}

void uwp_vsm_state_add_setter( struct uwp_vsm_state *st, const WCHAR *target, const WCHAR *prop,
                                      const WCHAR *val_str, int vt, double num, BOOL is_res_ref )
{
    struct uwp_vsm_setter *s;
    if (!st || !target || !target[0] || !prop || !prop[0]) return;
    if (st->setter_cnt >= st->setter_cap)
    {
        int ncap = st->setter_cap ? st->setter_cap * 2 : 8;
        struct uwp_vsm_setter *ns = realloc( st->setters, (size_t)ncap * sizeof(*ns) );
        if (!ns) return;
        st->setters = ns;
        st->setter_cap = ncap;
    }
    s = &st->setters[st->setter_cnt++];
    memset( s, 0, sizeof(*s) );
    lstrcpynW( s->target, target, ARRAY_SIZE(s->target) );
    lstrcpynW( s->property, prop, ARRAY_SIZE(s->property) );
    if (val_str && val_str[0]) lstrcpynW( s->val_str, val_str, ARRAY_SIZE(s->val_str) );
    s->vt = vt;
    s->num = num;
    s->is_res_ref = is_res_ref;
}

void vsm_clean_prop_name( const WCHAR *raw, WCHAR *out_prop, size_t max_prop )
{
    const WCHAR *p = raw;
    size_t len;
    if (!out_prop || !max_prop) return;
    out_prop[0] = 0;
    if (!p) return;
    while (*p == L' ' || *p == L'(') p++;
    len = wcslen( p );
    while (len > 0 && (p[len - 1] == L')' || p[len - 1] == L' ')) len--;
    if (len >= max_prop) len = max_prop - 1;
    memcpy( out_prop, p, len * sizeof(WCHAR) );
    out_prop[len] = 0;
}

void vsm_split_setter_target( const WCHAR *spec, WCHAR *out_target, size_t max_target,
                                     WCHAR *out_prop, size_t max_prop )
{
    const WCHAR *dot;
    size_t tlen;
    if (out_target && max_target) out_target[0] = 0;
    if (out_prop && max_prop) out_prop[0] = 0;
    if (!spec || !spec[0]) return;
    dot = wcschr( spec, L'.' );
    if (!dot) return;
    tlen = (size_t)(dot - spec);
    if (out_target && max_target)
    {
        if (tlen >= max_target) tlen = max_target - 1;
        memcpy( out_target, spec, tlen * sizeof(WCHAR) );
        out_target[tlen] = 0;
    }
    vsm_clean_prop_name( dot + 1, out_prop, max_prop );
}

void uwp_apply_style_by_name( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *style_key )
{
    WCHAR key[128] = {0};
    struct uwp_rd_entry *se = NULL;
    struct uwp_rd_bucket *sb = NULL;
    if (!el || !style_key || !style_key[0]) return;
    uwp_extract_resource_key( style_key, key, ARRAY_SIZE(key) );
    if (!key[0]) return;
    if (uwp_style_is_accent_name( key ))
        el->is_accent_style = TRUE;
    uwp_ensure_app_resources_loaded();
    if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, key, 0, &se, &sb ) && se && se->kind == UWP_RD_STYLE)
    {
        uwp_apply_style_entry( ctx, el, sb, se, 0 );
        return;
    }
    /* Standard UWP generic.xaml TextBlock typography styles */
    if (!wcscmp( key, L"HeaderTextBlockStyle" ))
    {
        if (!el->has_font_size) el->font_size = 46.0;
        if (!el->font_weight) el->font_weight = FW_LIGHT;
    }
    else if (!wcscmp( key, L"SubheaderTextBlockStyle" ))
    {
        if (!el->has_font_size) el->font_size = 34.0;
        if (!el->font_weight) el->font_weight = FW_LIGHT;
    }
    else if (!wcscmp( key, L"TitleTextBlockStyle" ))
    {
        if (!el->has_font_size) el->font_size = 24.0;
        if (!el->font_weight) el->font_weight = FW_LIGHT;
    }
    else if (!wcscmp( key, L"SubtitleTextBlockStyle" ))
    {
        if (!el->has_font_size) el->font_size = 20.0;
        if (!el->font_weight) el->font_weight = FW_NORMAL;
    }
    else if (!wcscmp( key, L"BaseTextBlockStyle" ))
    {
        if (!el->has_font_size) el->font_size = 14.0;
        if (!el->font_weight) el->font_weight = FW_SEMIBOLD;
    }
    else if (!wcscmp( key, L"BodyTextBlockStyle" ))
    {
        if (!el->has_font_size) el->font_size = 14.0;
        if (!el->font_weight) el->font_weight = FW_NORMAL;
    }
    else if (!wcscmp( key, L"CaptionTextBlockStyle" ))
    {
        if (!el->has_font_size) el->font_size = 12.0;
        if (!el->font_weight) el->font_weight = FW_NORMAL;
    }
}

static struct uwp_element *uwp_vsm_find_target_in_tree( struct uwp_element *root, const WCHAR *name )
{
    unsigned int k;
    struct uwp_element *hit;
    if (!root || !name || !name[0]) return NULL;
    if (!wcscmp( root->name, name )) return root;

    if (root->row_defs_coll)
    {
        struct uwp_vec_view *rows = CONTAINING_RECORD( root->row_defs_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < rows->count; k++)
        {
            struct uwp_element *rd = uwp_elem_from_any( rows->items[k] );
            if (rd && !wcscmp( rd->name, name )) return rd;
        }
    }
    if (root->col_defs_coll)
    {
        struct uwp_vec_view *cols = CONTAINING_RECORD( root->col_defs_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < cols->count; k++)
        {
            struct uwp_element *cd = uwp_elem_from_any( cols->items[k] );
            if (cd && !wcscmp( cd->name, name )) return cd;
        }
    }
    if (root->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( root->content );
        if (ch && ch != root && (hit = uwp_vsm_find_target_in_tree( ch, name )) != NULL)
            return hit;
    }
    if (root->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( root->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != root && (hit = uwp_vsm_find_target_in_tree( ch, name )) != NULL)
                return hit;
        }
    }
    return NULL;
}

static struct uwp_element *uwp_vsm_find_target( struct uwp_element *scope, const WCHAR *name )
{
    int i;
    struct uwp_element *hit;
    if (!scope || !name || !name[0]) return NULL;
    for (i = 0; i < scope->named_cnt; i++)
    {
        if (scope->named_elems[i] && !wcscmp( scope->named_elems[i]->name, name ))
            return scope->named_elems[i];
    }
    if (scope->owner_component && scope->owner_component != scope)
    {
        struct uwp_element *own = scope->owner_component;
        for (i = 0; i < own->named_cnt; i++)
        {
            if (own->named_elems[i] && !wcscmp( own->named_elems[i]->name, name ))
                return own->named_elems[i];
        }
    }
    hit = uwp_vsm_find_target_in_tree( scope, name );
    if (hit) return hit;
    if (scope->owner_component && scope->owner_component != scope)
        return uwp_vsm_find_target_in_tree( scope->owner_component, name );
    return NULL;
}

static void uwp_vsm_parse_grid_length( struct uwp_element *scope, const struct uwp_vsm_setter *s,
                                       int *out_type, double *out_val )
{
    if (s->vt == 0x07)
    {
        *out_type = 1;
        *out_val = s->num;
        return;
    }
    if (s->val_str[0])
    {
        const WCHAR *str = s->val_str;
        const WCHAR *star = wcschr( str, L'*' );
        if (!wcsicmp( str, L"Auto" ))
        {
            *out_type = 0;
            *out_val = 0.0;
            return;
        }
        if (star)
        {
            double v = (star == str) ? 1.0 : wcstod( str, NULL );
            *out_type = 2;
            *out_val = v;
            return;
        }
        if ((str[0] >= L'0' && str[0] <= L'9') || str[0] == L'-' || str[0] == L'+')
        {
            *out_type = 1;
            *out_val = wcstod( str, NULL );
            return;
        }
        {
            struct uwp_rd_entry *de = NULL;
            if (uwp_rd_find_entry( NULL, scope, str, 0, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
            {
                *out_type = 1;
                *out_val = de->dval;
                return;
            }
        }
    }
    *out_type = 1;
    *out_val = s->num;
}

static double uwp_vsm_parse_double( struct uwp_element *scope, const struct uwp_vsm_setter *s )
{
    if (s->val_str[0])
    {
        const WCHAR *str = s->val_str;
        if ((str[0] >= L'0' && str[0] <= L'9') || str[0] == L'-' || str[0] == L'+')
            return wcstod( str, NULL );
        {
            struct uwp_rd_entry *de = NULL;
            if (uwp_rd_find_entry( NULL, scope, str, 0, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
                return de->dval;
        }
    }
    return s->num;
}

static void uwp_vsm_record_snapshot( struct uwp_vsm_group *grp, struct uwp_element *target,
                                     enum uwp_vsm_prop_kind prop_kind )
{
    int i;
    struct uwp_vsm_snapshot_entry *e;
    if (!grp || !target || prop_kind == UWP_VSM_PROP_NONE) return;

    /* First-wins for current state entry */
    for (i = 0; i < grp->snapshot_cnt; i++)
    {
        if (grp->snapshots[i].target == target && grp->snapshots[i].prop_kind == prop_kind)
            return;
    }

    if (!xaml_vec_grow( (void **)&grp->snapshots, &grp->snapshot_cap, grp->snapshot_cnt + 1,
                        sizeof(*grp->snapshots), 16 ))
        return;

    e = &grp->snapshots[grp->snapshot_cnt++];
    memset( e, 0, sizeof(*e) );
    e->target = target;
    e->prop_kind = prop_kind;
    switch (prop_kind)
    {
    case UWP_VSM_PROP_GRID_LEN:
        e->old_int = target->grid_len_type;
        e->old_num = target->grid_len_val;
        break;
    case UWP_VSM_PROP_WIDTH:         e->old_num = target->width; break;
    case UWP_VSM_PROP_HEIGHT:        e->old_num = target->height; break;
    case UWP_VSM_PROP_MIN_WIDTH:     e->old_num = target->min_width; break;
    case UWP_VSM_PROP_MAX_WIDTH:     e->old_num = target->max_width; break;
    case UWP_VSM_PROP_MIN_HEIGHT:    e->old_num = target->min_height; break;
    case UWP_VSM_PROP_MAX_HEIGHT:    e->old_num = target->max_height; break;
    case UWP_VSM_PROP_VISIBILITY:    e->old_int = target->visibility; break;
    case UWP_VSM_PROP_IS_ENABLED:    e->old_int = target->is_enabled; break;
    case UWP_VSM_PROP_GRID_ROW:      e->old_int = target->grid_row; break;
    case UWP_VSM_PROP_GRID_ROW_SPAN: e->old_int = target->grid_row_span; break;
    case UWP_VSM_PROP_GRID_COL:      e->old_int = target->grid_col; break;
    case UWP_VSM_PROP_GRID_COL_SPAN: e->old_int = target->grid_col_span; break;
    case UWP_VSM_PROP_FONT_SIZE:     e->old_num = target->font_size; break;
    case UWP_VSM_PROP_OPACITY:       e->old_num = target->opacity; break;
    case UWP_VSM_PROP_MARGIN:
        e->old_vec4[0] = target->margin_l; e->old_vec4[1] = target->margin_t;
        e->old_vec4[2] = target->margin_r; e->old_vec4[3] = target->margin_b;
        break;
    case UWP_VSM_PROP_PADDING:
        e->old_vec4[0] = target->padding_l; e->old_vec4[1] = target->padding_t;
        e->old_vec4[2] = target->padding_r; e->old_vec4[3] = target->padding_b;
        break;
    case UWP_VSM_PROP_H_ALIGN:       e->old_int = target->h_align; break;
    case UWP_VSM_PROP_V_ALIGN:       e->old_int = target->v_align; break;
    case UWP_VSM_PROP_TEXT_ALIGN:
        e->old_int = target->text_align;
        e->old_flag = target->has_text_align;
        break;
    case UWP_VSM_PROP_MAX_LINES:     e->old_int = target->max_lines; break;
    case UWP_VSM_PROP_BG:
        e->old_flag = target->has_bg; e->old_cr = target->bg_color;
        e->old_argb = target->bg_argb; e->old_alpha = target->bg_alpha;
        break;
    case UWP_VSM_PROP_FG:
        e->old_flag = target->has_fg; e->old_cr = target->fg_color;
        e->old_argb = target->fg_argb; e->old_alpha = target->fg_alpha;
        break;
    case UWP_VSM_PROP_ORIENTATION:   e->old_int = target->orientation; break;
    case UWP_VSM_PROP_ITEM_TPL_SEL:
        e->old_obj = target->item_template_selector;
        if (e->old_obj) IInspectable_AddRef( e->old_obj );
        break;
    case UWP_VSM_PROP_ITEM_TPL:
        e->old_obj = target->item_template;
        if (e->old_obj) IInspectable_AddRef( e->old_obj );
        break;
    default: break;
    }
}

static void uwp_vsm_restore_snapshots( struct uwp_vsm_group *grp )
{
    int i;
    if (!grp) return;
    for (i = grp->snapshot_cnt - 1; i >= 0; i--)
    {
        const struct uwp_vsm_snapshot_entry *e = &grp->snapshots[i];
        struct uwp_element *target = e->target;
        if (!target) continue;
        switch (e->prop_kind)
        {
        case UWP_VSM_PROP_GRID_LEN:
            target->grid_len_type = e->old_int;
            target->grid_len_val  = e->old_num;
            break;
        case UWP_VSM_PROP_WIDTH:         target->width          = e->old_num; break;
        case UWP_VSM_PROP_HEIGHT:        target->height         = e->old_num; break;
        case UWP_VSM_PROP_MIN_WIDTH:     target->min_width      = e->old_num; break;
        case UWP_VSM_PROP_MAX_WIDTH:     target->max_width      = e->old_num; break;
        case UWP_VSM_PROP_MIN_HEIGHT:    target->min_height     = e->old_num; break;
        case UWP_VSM_PROP_MAX_HEIGHT:    target->max_height     = e->old_num; break;
        case UWP_VSM_PROP_VISIBILITY:    target->visibility     = e->old_int; break;
        case UWP_VSM_PROP_IS_ENABLED:    target->is_enabled     = (boolean)e->old_int; break;
        case UWP_VSM_PROP_GRID_ROW:      target->grid_row       = e->old_int; break;
        case UWP_VSM_PROP_GRID_ROW_SPAN: target->grid_row_span  = e->old_int; break;
        case UWP_VSM_PROP_GRID_COL:      target->grid_col       = e->old_int; break;
        case UWP_VSM_PROP_GRID_COL_SPAN: target->grid_col_span  = e->old_int; break;
        case UWP_VSM_PROP_FONT_SIZE:     target->font_size      = e->old_num; break;
        case UWP_VSM_PROP_OPACITY:       target->opacity        = e->old_num; break;
        case UWP_VSM_PROP_MARGIN:
            target->margin_l = e->old_vec4[0]; target->margin_t = e->old_vec4[1];
            target->margin_r = e->old_vec4[2]; target->margin_b = e->old_vec4[3];
            break;
        case UWP_VSM_PROP_PADDING:
            target->padding_l = e->old_vec4[0]; target->padding_t = e->old_vec4[1];
            target->padding_r = e->old_vec4[2]; target->padding_b = e->old_vec4[3];
            break;
        case UWP_VSM_PROP_H_ALIGN:       target->h_align        = e->old_int; break;
        case UWP_VSM_PROP_V_ALIGN:       target->v_align        = e->old_int; break;
        case UWP_VSM_PROP_TEXT_ALIGN:
            target->text_align = e->old_int;
            target->has_text_align = e->old_flag;
            break;
        case UWP_VSM_PROP_MAX_LINES:     target->max_lines      = e->old_int; break;
        case UWP_VSM_PROP_BG:
            target->has_bg = e->old_flag; target->bg_color = e->old_cr;
            target->bg_argb = e->old_argb; target->bg_alpha = e->old_alpha;
            break;
        case UWP_VSM_PROP_FG:
            target->has_fg = e->old_flag; target->fg_color = e->old_cr;
            target->fg_argb = e->old_argb; target->fg_alpha = e->old_alpha;
            break;
        case UWP_VSM_PROP_ORIENTATION:   target->orientation    = e->old_int; break;
        case UWP_VSM_PROP_ITEM_TPL_SEL:
            if (target->item_template_selector) IInspectable_Release( target->item_template_selector );
            target->item_template_selector = e->old_obj;
            break;
        case UWP_VSM_PROP_ITEM_TPL:
            if (target->item_template) IInspectable_Release( target->item_template );
            target->item_template = e->old_obj;
            break;
        default: break;
        }
    }
    grp->snapshot_cnt = 0;
}

static BOOL uwp_vsm_parse_thickness( struct uwp_element *scope, const struct uwp_vsm_setter *s, double out[4] )
{
    if (s->val_str[0])
    {
        struct uwp_rd_entry *te = NULL;
        if (s->is_res_ref && uwp_rd_find_entry( NULL, scope, s->val_str, 0, &te, NULL ) && te && te->kind == UWP_RD_THICKNESS)
        {
            memcpy( out, te->vec4, 4 * sizeof(double) );
            return TRUE;
        }
        {
            double v[4] = {0};
            int cnt = 0;
            const WCHAR *p = s->val_str;
            while (*p && cnt < 4)
            {
                WCHAR *endp = NULL;
                while (*p == L' ' || *p == L',') p++;
                if (!*p) break;
                v[cnt++] = wcstod( p, &endp );
                if (!endp || endp == p) break;
                p = endp;
            }
            if (cnt == 1) { out[0] = out[1] = out[2] = out[3] = v[0]; return TRUE; }
            if (cnt == 2) { out[0] = out[2] = v[0]; out[1] = out[3] = v[1]; return TRUE; }
            if (cnt == 4) { memcpy( out, v, 4 * sizeof(double) ); return TRUE; }
        }
    }
    return FALSE;
}

static void uwp_vsm_apply_setter( struct uwp_element *scope, struct uwp_vsm_group *grp,
                                  const struct uwp_vsm_setter *s )
{
    struct uwp_element *target = uwp_vsm_find_target( scope, s->target );
    const WCHAR *prop = s->property;
    if (!target || !prop[0]) return;

    if (!wcscmp( prop, L"Width" ) || wcsstr( prop, L".Width" ))
    {
        if (target->kind == UWP_KIND_COL_DEF || target->kind == UWP_KIND_ROW_DEF)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_LEN );
            uwp_vsm_parse_grid_length( scope, s, &target->grid_len_type, &target->grid_len_val );
        }
        else
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_WIDTH );
            target->width = uwp_vsm_parse_double( scope, s );
        }
    }
    else if (!wcscmp( prop, L"Height" ) || wcsstr( prop, L".Height" ))
    {
        if (target->kind == UWP_KIND_ROW_DEF || target->kind == UWP_KIND_COL_DEF)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_LEN );
            uwp_vsm_parse_grid_length( scope, s, &target->grid_len_type, &target->grid_len_val );
        }
        else
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_HEIGHT );
            target->height = uwp_vsm_parse_double( scope, s );
        }
    }
    else if (!wcscmp( prop, L"MinWidth" ) || wcsstr( prop, L".MinWidth" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MIN_WIDTH );
        target->min_width = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MaxWidth" ) || wcsstr( prop, L".MaxWidth" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MAX_WIDTH );
        target->max_width = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MinHeight" ) || wcsstr( prop, L".MinHeight" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MIN_HEIGHT );
        target->min_height = uwp_vsm_parse_double( scope, s );
    }
    else if (!wcscmp( prop, L"MaxHeight" ) || wcsstr( prop, L".MaxHeight" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MAX_HEIGHT );
        target->max_height = uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"Visibility" ))
    {
        int vis = target->visibility;
        if (s->val_str[0])
        {
            if (!wcsicmp( s->val_str, L"Collapsed" ) || !wcscmp( s->val_str, L"1" )) vis = 1;
            else if (!wcsicmp( s->val_str, L"Visible" ) || !wcscmp( s->val_str, L"0" )) vis = 0;
        }
        else
        {
            vis = ((int)s->num != 0) ? 1 : 0;
        }
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_VISIBILITY );
        target->visibility = vis;
        if (!wcscmp( target->name, L"DockPanel" ))
        {
            struct uwp_element *hbtn = uwp_vsm_find_target( scope, L"HistoryButton" );
            struct uwp_element *mbtn = uwp_vsm_find_target( scope, L"MemoryButton" );
            if (hbtn)
            {
                uwp_vsm_record_snapshot( grp, hbtn, UWP_VSM_PROP_VISIBILITY );
                hbtn->visibility = (vis == 0) ? 1 : 0;
            }
            if (mbtn)
            {
                uwp_vsm_record_snapshot( grp, mbtn, UWP_VSM_PROP_VISIBILITY );
                mbtn->visibility = (vis == 0) ? 1 : 0;
            }
        }
    }
    else if (wcsstr( prop, L"IsEnabled" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_IS_ENABLED );
        if (s->val_str[0])
            target->is_enabled = (!wcsicmp( s->val_str, L"True" ) || !wcscmp( s->val_str, L"1" )) ? TRUE : FALSE;
        else
            target->is_enabled = ((int)s->num != 0) ? TRUE : FALSE;
    }
    else if (wcsstr( prop, L"Grid.RowSpan" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_ROW_SPAN );
        target->grid_row_span = max( 1, (int)uwp_vsm_parse_double( scope, s ) );
    }
    else if (wcsstr( prop, L"Grid.Row" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_ROW );
        target->grid_row = (int)uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"Grid.ColumnSpan" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_COL_SPAN );
        target->grid_col_span = max( 1, (int)uwp_vsm_parse_double( scope, s ) );
    }
    else if (wcsstr( prop, L"Grid.Column" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_GRID_COL );
        target->grid_col = (int)uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"FontSize" ))
    {
        double fs = uwp_vsm_parse_double( scope, s );
        if (fs > 0.0)
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_FONT_SIZE );
            target->font_size = fs;
        }
    }
    else if (wcsstr( prop, L"Opacity" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_OPACITY );
        target->opacity = uwp_vsm_parse_double( scope, s );
    }
    else if (wcsstr( prop, L"Margin" ))
    {
        double v[4];
        if (uwp_vsm_parse_thickness( scope, s, v ))
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MARGIN );
            target->margin_l = v[0]; target->margin_t = v[1];
            target->margin_r = v[2]; target->margin_b = v[3];
        }
    }
    else if (wcsstr( prop, L"Padding" ))
    {
        double v[4];
        if (uwp_vsm_parse_thickness( scope, s, v ))
        {
            uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_PADDING );
            target->padding_l = v[0]; target->padding_t = v[1];
            target->padding_r = v[2]; target->padding_b = v[3];
        }
    }
    else if (wcsstr( prop, L"HorizontalAlignment" ))
    {
        int ha = target->h_align;
        if (s->val_str[0])
        {
            if (!wcsicmp( s->val_str, L"Left" )) ha = 0;
            else if (!wcsicmp( s->val_str, L"Center" )) ha = 1;
            else if (!wcsicmp( s->val_str, L"Right" )) ha = 2;
            else if (!wcsicmp( s->val_str, L"Stretch" )) ha = 3;
        }
        else ha = (int)s->num;
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_H_ALIGN );
        target->h_align = ha;
    }
    else if (wcsstr( prop, L"VerticalAlignment" ))
    {
        int va = target->v_align;
        if (s->val_str[0])
        {
            if (!wcsicmp( s->val_str, L"Top" )) va = 0;
            else if (!wcsicmp( s->val_str, L"Center" )) va = 1;
            else if (!wcsicmp( s->val_str, L"Bottom" )) va = 2;
            else if (!wcsicmp( s->val_str, L"Stretch" )) va = 3;
        }
        else va = (int)s->num;
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_V_ALIGN );
        target->v_align = va;
    }
    else if (wcsstr( prop, L"TextAlignment" ) || wcsstr( prop, L"HorizontalTextAlignment" ))
    {
        int ta = target->text_align;
        if (s->val_str[0])
        {
            if (!wcsicmp( s->val_str, L"Center" )) ta = 0;
            else if (!wcsicmp( s->val_str, L"Left" )) ta = 1;
            else if (!wcsicmp( s->val_str, L"Right" )) ta = 2;
            else if (!wcsicmp( s->val_str, L"Justify" )) ta = 3;
        }
        else ta = (int)s->num;
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_TEXT_ALIGN );
        target->text_align = ta;
        target->has_text_align = TRUE;
    }
    else if (wcsstr( prop, L"MaxLines" ))
    {
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_MAX_LINES );
        target->max_lines = max( 0, (int)uwp_vsm_parse_double( scope, s ) );
    }
    else if (wcsstr( prop, L"Orientation" ))
    {
        int ori = target->orientation;
        if (s->val_str[0])
        {
            if (!wcsicmp( s->val_str, L"Horizontal" )) ori = 1;
            else if (!wcsicmp( s->val_str, L"Vertical" )) ori = 0;
        }
        else ori = (int)s->num;
        uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_ORIENTATION );
        target->orientation = ori;
    }
    else if (wcsstr( prop, L"Background" ) || wcsstr( prop, L"Foreground" ))
    {
        UINT32 argb = 0;
        BOOL is_bg = wcsstr( prop, L"Background" ) != NULL;
        if (s->val_str[0] && uwp_resolve_brush_or_color( scope ? scope->rd_bucket : NULL, target, s->val_str, 0, &argb ))
        {
            uwp_vsm_record_snapshot( grp, target, is_bg ? UWP_VSM_PROP_BG : UWP_VSM_PROP_FG );
            if (is_bg)
            {
                target->has_bg = TRUE;
                target->bg_argb = argb;
                target->bg_alpha = (BYTE)((argb >> 24) & 0xff);
                target->bg_color = RGB( (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff );
            }
            else
            {
                target->has_fg = TRUE;
                target->fg_argb = argb;
                target->fg_alpha = (BYTE)((argb >> 24) & 0xff);
                target->fg_color = RGB( (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff );
            }
        }
    }
    else if (wcsstr( prop, L"Style" ) && s->val_str[0])
    {
        uwp_apply_style_by_name( NULL, target, s->val_str );
    }
    else if ((wcsstr( prop, L"ItemTemplateSelector" ) || wcsstr( prop, L"ItemTemplate" ) || wcsstr( prop, L"ItemsPanel" )) &&
             s->val_str[0])
    {
        struct uwp_rd_entry *re = NULL;
        struct uwp_rd_bucket *fb = NULL;
        if (uwp_rd_find_entry( scope ? scope->rd_bucket : NULL, target, s->val_str, 0, &re, &fb ) && re)
        {
            IInspectable *res_val = uwp_rd_materialize_entry( fb, re );
            if (res_val)
            {
                if (wcsstr( prop, L"ItemTemplateSelector" ))
                {
                    uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_ITEM_TPL_SEL );
                    if (target->item_template_selector) IInspectable_Release( target->item_template_selector );
                    target->item_template_selector = res_val;
                    IInspectable_AddRef( res_val );
                }
                else if (wcsstr( prop, L"ItemTemplate" ))
                {
                    uwp_vsm_record_snapshot( grp, target, UWP_VSM_PROP_ITEM_TPL );
                    if (target->item_template) IInspectable_Release( target->item_template );
                    target->item_template = res_val;
                    IInspectable_AddRef( res_val );
                }
                IInspectable_Release( res_val );
            }
        }
    }
}

static void uwp_vsm_transition_group( struct uwp_element *scope, struct uwp_vsm_group *grp, int new_idx )
{
    int k;
    if (!scope || !grp || new_idx < -1 || new_idx >= grp->state_cnt) return;
    if (grp->active_state_idx == new_idx) return;

    if (grp->active_state_idx >= 0 && grp->active_state_idx < grp->state_cnt)
        uwp_vsm_restore_snapshots( grp );

    grp->active_state_idx = new_idx;
    if (new_idx >= 0)
    {
        struct uwp_vsm_state *new_st = &grp->states[new_idx];
        lstrcpynW( grp->current_state, new_st->name, ARRAY_SIZE(grp->current_state) );
        for (k = 0; k < new_st->setter_cnt; k++)
            uwp_vsm_apply_setter( scope, grp, &new_st->setters[k] );
        TRACE( "VSM group '%s' on %s (%s) -> state '%s' (%d setters)\n",
               debugstr_w(grp->name), debugstr_w(scope->class_name), debugstr_w(scope->name),
               debugstr_w(new_st->name), new_st->setter_cnt );
    }
    else
    {
        grp->current_state[0] = 0;
    }
    g_layout_dirty = TRUE;
}

void uwp_vsm_evaluate_tree( struct uwp_element *el, double win_w, double win_h )
{
    static int vsm_depth = 0;
    int g, s, i, j;
    unsigned int k;
    if (!el || vsm_depth >= 64) return;
    if (el->vsm_eval_valid && el->vsm_eval_width == win_w && el->vsm_eval_height == win_h)
        return;
    vsm_depth++;

    el->vsm_eval_width = win_w;
    el->vsm_eval_height = win_h;
    el->vsm_eval_valid = TRUE;

    for (g = 0; g < el->vsm_group_cnt; g++)
    {
        struct uwp_vsm_group *grp = &el->vsm_groups[g];
        int order[64];
        int n_trig = 0;
        int matched = -1;
        for (s = 0; s < grp->state_cnt && n_trig < (int)ARRAY_SIZE(order); s++)
        {
            if (grp->states[s].has_trigger)
                order[n_trig++] = s;
        }
        if (!n_trig) continue;

        /* Sort trigger states in ascending MinWindowWidth (then MinWindowHeight);
         * on equal trigger thresholds, place later-declared first so the LAST match
         * in ascending order resolves stable ties to the first-declared state. */
        for (i = 1; i < n_trig; i++)
        {
            int cur = order[i];
            const struct uwp_vsm_state *st_cur = &grp->states[cur];
            j = i - 1;
            while (j >= 0)
            {
                int prev = order[j];
                const struct uwp_vsm_state *st_prev = &grp->states[prev];
                BOOL swap = FALSE;
                if (st_cur->min_window_width < st_prev->min_window_width)
                    swap = TRUE;
                else if (st_cur->min_window_width == st_prev->min_window_width)
                {
                    if (st_cur->min_window_height < st_prev->min_window_height)
                        swap = TRUE;
                    else if (st_cur->min_window_height == st_prev->min_window_height && cur > prev)
                        swap = TRUE;
                }
                if (!swap) break;
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = cur;
        }

        for (i = 0; i < n_trig; i++)
        {
            int idx = order[i];
            const struct uwp_vsm_state *st = &grp->states[idx];
            if (win_w >= st->min_window_width && win_h >= st->min_window_height)
                matched = idx;
        }
        if (matched < 0 && grp->state_cnt > 0)
            matched = 0;
        uwp_vsm_transition_group( el, grp, matched );
    }

    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el) uwp_vsm_evaluate_tree( ch, win_w, win_h );
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el) uwp_vsm_evaluate_tree( ch, win_w, win_h );
        }
    }
    vsm_depth--;
}

BOOL uwp_vsm_leave_state_on_tree( struct uwp_element *el, const WCHAR *state_name )
{
    int g, s;
    unsigned int k;
    BOOL found = FALSE;
    if (!el || !state_name || !state_name[0]) return FALSE;
    for (g = 0; g < el->vsm_group_cnt; g++)
    {
        struct uwp_vsm_group *grp = &el->vsm_groups[g];
        for (s = 0; s < grp->state_cnt; s++)
        {
            if (!wcscmp( grp->states[s].name, state_name ))
            {
                if (grp->active_state_idx == s)
                    uwp_vsm_transition_group( el, grp, -1 );
                found = TRUE;
                break;
            }
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_vsm_leave_state_on_tree( ch, state_name ))
            found = TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && uwp_vsm_leave_state_on_tree( ch, state_name ))
                found = TRUE;
        }
    }
    return found;
}

BOOL uwp_vsm_goto_state_on_tree( struct uwp_element *el, const WCHAR *state_name )
{
    int g, s;
    unsigned int k;
    BOOL found = FALSE;
    if (!el || !state_name || !state_name[0]) return FALSE;

    for (g = 0; g < el->vsm_group_cnt; g++)
    {
        struct uwp_vsm_group *grp = &el->vsm_groups[g];
        for (s = 0; s < grp->state_cnt; s++)
        {
            if (!wcscmp( grp->states[s].name, state_name ))
            {
                uwp_vsm_transition_group( el, grp, s );
                found = TRUE;
                break;
            }
        }
    }
    if (el->content)
    {
        struct uwp_element *ch = uwp_elem_from_any( el->content );
        if (ch && ch != el && uwp_vsm_goto_state_on_tree( ch, state_name ))
            found = TRUE;
    }
    if (el->children_coll)
    {
        struct uwp_vec_view *children = CONTAINING_RECORD( el->children_coll, struct uwp_vec_view, IInspectable_iface );
        for (k = 0; k < children->count; k++)
        {
            struct uwp_element *ch = uwp_elem_from_any( children->items[k] );
            if (ch && ch != el && uwp_vsm_goto_state_on_tree( ch, state_name ))
                found = TRUE;
        }
    }
    return found;
}

static HRESULT WINAPI vsm_GoToState( void *iface, void *ctrl, HSTRING state, boolean use_trans, boolean *ret )
{
    struct uwp_element *el = uwp_elem_from_any( ctrl );
    const WCHAR *state_str = WindowsGetStringRawBuffer( state, NULL );
    (void)iface; (void)use_trans;
    if (el && state_str && state_str[0])
    {
        if (!uwp_vsm_goto_state_on_tree( el, state_str ) && el->owner_component && el->owner_component != el)
            uwp_vsm_goto_state_on_tree( el->owner_component, state_str );
        uwp_invalidate_window();
    }
    if (ret) *ret = TRUE;
    return S_OK;
}
static HRESULT WINAPI vsm_GetVisualStateGroups( void *iface, void *obj, void **out )
{
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vsm_get_CustomVisualStateManagerProperty( void *iface, void **out )
{
    (void)iface;
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}
static HRESULT WINAPI vsm_GetCustomVisualStateManager( void *iface, void *obj, void **out )
{
    (void)iface; (void)obj;
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI vsm_SetCustomVisualStateManager( void *iface, void *obj, void *val )
{
    (void)iface; (void)obj; (void)val;
    return S_OK;
}
/* IVisualStateManagerStatics ({01d0e9e0-d713-414e-a74e-e63ec7ac8c3d}):
 *   slot 6:  GetVisualStateGroups
 *   slot 7:  get_CustomVisualStateManagerProperty
 *   slot 8:  GetCustomVisualStateManager
 *   slot 9:  SetCustomVisualStateManager
 *   slot 10: GoToState */
static const void *vsm_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vsm_GetVisualStateGroups,
    [7] = vsm_get_CustomVisualStateManagerProperty,
    [8] = vsm_GetCustomVisualStateManager,
    [9] = vsm_SetCustomVisualStateManager,
    [10] = vsm_GoToState,
    [11 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory vsm_factory_obj =
    { {(const struct IActivationFactoryVtbl *)vsm_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)vsm_vtbl}, L"Windows.UI.Xaml.VisualStateManager", { NULL } };

static HRESULT WINAPI sb_get_hstr_empty( void *iface, void *elem, HSTRING *out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI sb_SetTargetProperty( void *iface, void *elem, HSTRING path )
{
    struct uwp_element *anim = uwp_elem_from_any( elem );
    const WCHAR *s = WindowsGetStringRawBuffer( path, NULL );
    if (anim && s && s[0])
        vsm_clean_prop_name( s, anim->anim_target_prop, ARRAY_SIZE(anim->anim_target_prop) );
    return S_OK;
}
static HRESULT WINAPI sb_SetTargetName( void *iface, void *elem, HSTRING name )
{
    struct uwp_element *anim = uwp_elem_from_any( elem );
    const WCHAR *s = WindowsGetStringRawBuffer( name, NULL );
    if (anim && s && s[0])
        lstrcpynW( anim->anim_target_name, s, ARRAY_SIZE(anim->anim_target_name) );
    return S_OK;
}
static HRESULT WINAPI sb_SetTarget( void *iface, void *elem, void *target )
{
    struct uwp_element *anim = uwp_elem_from_any( elem );
    struct uwp_element *tel = uwp_elem_from_any( target );
    if (anim && tel)
        anim->anim_target_el = tel;
    return S_OK;
}

static HRESULT WINAPI uwp_sb_get_Children( void *iface, void **out )
{
    struct uwp_element *impl = uwp_elem_from_any( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!impl) return S_OK;
    if (!impl->children_coll) uwp_vec_create( impl, &impl->children_coll );
    *out = impl->children_coll;
    if (*out) IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static HRESULT WINAPI uwp_sb_Begin( void *iface )
{
    struct uwp_element *sb = uwp_elem_from_any( iface );
    struct uwp_vec_view *v;
    unsigned int i;
    BOOL changed = FALSE;

    if (!sb || !sb->children_coll) return S_OK;
    v = CONTAINING_RECORD( sb->children_coll, struct uwp_vec_view, IInspectable_iface );
    for (i = 0; v && i < v->count; i++)
    {
        struct uwp_element *anim = uwp_elem_from_any( v->items[i] );
        struct uwp_element *target;
        const WCHAR *prop;
        if (!anim || !anim->anim_has_to) continue;
        target = anim->anim_target_el;
        if (!target && anim->anim_target_name[0])
        {
            struct uwp_element *scope = anim->owner_component ? anim->owner_component : sb->owner_component;
            target = uwp_vsm_find_target( scope, anim->anim_target_name );
        }
        if (!target) continue;
        prop = anim->anim_target_prop;
        if (!prop[0] || wcsstr( prop, L"Opacity" ))
        {
            target->opacity = anim->anim_to;
            changed = TRUE;
        }
        else if (!wcscmp( prop, L"Width" ) || wcsstr( prop, L".Width" ))
        {
            target->width = anim->anim_to;
            changed = TRUE;
        }
        else if (!wcscmp( prop, L"Height" ) || wcsstr( prop, L".Height" ))
        {
            target->height = anim->anim_to;
            changed = TRUE;
        }
        TRACE( "IStoryboard::Begin applied %s.%s = %g\n",
               debugstr_w(target->name[0] ? target->name : target->class_name),
               debugstr_w(prop), anim->anim_to );
    }
    if (changed)
    {
        g_layout_dirty = TRUE;
        uwp_invalidate_window();
    }
    return S_OK;
}

const void *uwp_sb_vtbl[24] =
{
    [0] = uwp_sub_QueryInterface,
    [1] = uwp_sub_AddRef,
    [2] = uwp_sub_Release,
    [3] = uwp_sub_GetIids,
    [4] = uwp_sub_GetRuntimeClassName,
    [5] = uwp_sub_GetTrustLevel,
    [6] = uwp_sb_get_Children, /* get_Children */
    [7] = uwp_generic_noop,    /* Seek */
    [8] = uwp_generic_noop,    /* Stop */
    [9] = uwp_sb_Begin,        /* Begin */
    [10] = uwp_generic_noop,   /* Pause */
    [11] = uwp_generic_noop,   /* Resume */
    [12] = uwp_generic_noop,   /* GetCurrentState */
    [13] = uwp_generic_noop,   /* GetCurrentTime */
    [14] = uwp_generic_noop,   /* SeekAlignedToLastTick */
    [15] = uwp_sb_Begin,       /* SkipToFill */
    [16 ... 23] = uwp_generic_noop,
};

/* IStoryboardStatics ({d82f07d8-73d5-4379-bd48-7e05184a8bad}):
 *   slot 6:  get_TargetPropertyProperty
 *   slot 7:  GetTargetProperty
 *   slot 8:  SetTargetProperty
 *   slot 9:  get_TargetNameProperty
 *   slot 10: GetTargetName
 *   slot 11: SetTargetName
 *   slot 12: SetTarget */
static const void *sb_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vsm_get_CustomVisualStateManagerProperty,
    [7] = sb_get_hstr_empty,
    [8] = sb_SetTargetProperty,
    [9] = vsm_get_CustomVisualStateManagerProperty,
    [10] = sb_get_hstr_empty,
    [11] = sb_SetTargetName,
    [12] = sb_SetTarget,
    [13 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory sb_factory_obj =
    { {(const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)sb_statics_vtbl}, L"Windows.UI.Xaml.Media.Animation.Storyboard", { NULL } };
static struct uwp_class_factory sb_factory_winui3_obj =
    { {(const struct IActivationFactoryVtbl *)uwp_cf_act_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)sb_statics_vtbl}, L"Microsoft.UI.Xaml.Media.Animation.Storyboard", { NULL } };

    /* VisualTreeHelperStatics */
static HRESULT WINAPI vth_FindPoint( void *iface, Point pt, void *subtree, void **out )
{
    (void)pt; (void)subtree;
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindRect( void *iface, const Rect *rc, void *subtree, void **out )
{
    (void)rc; (void)subtree;
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindPointSub( void *iface, Point pt, void *subtree, boolean all, void **out )
{
    (void)pt; (void)subtree; (void)all;
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_FindRectSub( void *iface, const Rect *rc, void *subtree, boolean all, void **out )
{
    (void)rc; (void)subtree; (void)all;
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth2_GetOpenPopups( void *iface, void *window, void **out )
{
    (void)window;
    return uwp_vec_create( NULL, (IInspectable **)out );
}
static HRESULT WINAPI vth_GetChild( void *iface, void *ref, INT32 idx, void **out )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!el || idx < 0) return S_OK;
    if (el->children_coll)
    {
        void *item = NULL;
        HRESULT hr = uwp_vec_GetAt( el->children_coll, (unsigned int)idx, &item );
        if (SUCCEEDED( hr ) && item)
        {
            HRESULT qhr = IInspectable_QueryInterface( (IInspectable *)item, &IID_IDependencyObject, out );
            IInspectable_Release( (IInspectable *)item );
            return SUCCEEDED( qhr ) ? S_OK : S_OK;
        }
        return hr;
    }
    if (el->content && idx == 0)
    {
        if (FAILED( IInspectable_QueryInterface( (IInspectable *)el->content, &IID_IDependencyObject, out ) ))
            *out = NULL;
    }
    return S_OK;
}
static HRESULT WINAPI vth_GetChildrenCount( void *iface, void *ref, INT32 *count )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    if (!count) return E_POINTER;
    *count = 0;
    if (!el) return S_OK;
    if (el->children_coll)
    {
        unsigned int sz = 0;
        uwp_vec_get_Size( el->children_coll, &sz );
        *count = (INT32)sz;
    }
    else if (el->content)
    {
        void *dep = NULL;
        if (SUCCEEDED( IInspectable_QueryInterface( (IInspectable *)el->content, &IID_IDependencyObject, &dep ) ) && dep)
        {
            IInspectable_Release( (IInspectable *)dep );
            *count = 1;
        }
    }
    return S_OK;
}
static HRESULT WINAPI vth_GetParent( void *iface, void *ref, void **out )
{
    struct uwp_element *el = uwp_elem_from_any( ref );
    WARN( "vth_GetParent: ref=%p el=%p (%s) parent=%p (%s)\n", ref, el, el ? debugstr_w(el->class_name) : "?",
          el ? el->parent : NULL, (el && el->parent) ? debugstr_w(el->parent->class_name) : "?" );
    if (!out) return E_POINTER;
    *out = NULL;
    if (el && el->parent)
    {
        IInspectable *p = el->parent->outer ? el->parent->outer : &el->parent->IInspectable_iface;
        if (FAILED( IInspectable_QueryInterface( p, &IID_IDependencyObject, out ) ))
            *out = NULL;
    }
    return S_OK;
}
static HRESULT WINAPI vth_DisconnectChildren( void *iface, void *el )
{
    return S_OK;
}
static HRESULT WINAPI vth_QI( IActivationFactory *iface, REFIID iid, void **out );
static const void *vth2_vtbl[16] =
{
    [0] = vth_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vth2_GetOpenPopups,
    [7 ... 15] = uwp_generic_noop,
};
static struct { const void * const *lpVtbl; } vth2_obj = { vth2_vtbl };

static HRESULT WINAPI vth_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    static const GUID IID_IVisualTreeHelperStatics2_UWP =
        { 0x07bcd176, 0x869f, 0x44a7, { 0x87, 0x97, 0x21, 0x03, 0xa4, 0xc3, 0xe4, 0x7a } };
    static const GUID IID_IVisualTreeHelperStatics2_WinUI =
        { 0x629bdbc8, 0xd932, 0x4ff4, { 0x96, 0xb9, 0x8d, 0x96, 0xc5, 0xc1, 0xe8, 0x58 } };
    static const GUID IID_IVisualTreeHelperStatics2_Alt =
        { 0x05af3c36, 0x3990, 0x445e, { 0xb9, 0x22, 0xe7, 0x3b, 0x22, 0x89, 0x79, 0x87 } };
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IMarshal ) ||
        IsEqualGUID( iid, &IID_IReferenceTracker_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget_UWP2 ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IVisualTreeHelperStatics2_UWP ) ||
        IsEqualGUID( iid, &IID_IVisualTreeHelperStatics2_WinUI ) ||
        IsEqualGUID( iid, &IID_IVisualTreeHelperStatics2_Alt ))
    {
        *out = &vth2_obj;
        return S_OK;
    }
    *out = &vth_factory_obj.IActivationFactory_iface;
    return S_OK;
}
static const void *vth_vtbl[16] =
{
    [0] = vth_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = vth_FindPoint,
    [7] = vth_FindRect,
    [8] = vth_FindPointSub,
    [9] = vth_FindRectSub,
    [10] = vth_GetChild,
    [11] = vth_GetChildrenCount,
    [12] = vth_GetParent,
    [13] = vth_DisconnectChildren,
    [14 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory vth_factory_obj =
    { {(const struct IActivationFactoryVtbl *)vth_vtbl}, {(const struct IInspectableVtbl *)uwp_cf_comp_vtbl}, {(const struct IInspectableVtbl *)vth_vtbl}, L"Windows.UI.Xaml.Media.VisualTreeHelper", { NULL } };

/* AutomationProperties / ToolTipService / GridStatics attached property helpers */
static HRESULT WINAPI attached_prop_get_dp( void *iface, void **out )
{
    return dependency_property_create( NULL, (IDependencyProperty **)out );
}
static HRESULT WINAPI auto_props_GetName( void *iface, void *elem, HSTRING *out )
{
    struct uwp_element *el = uwp_elem_from_any( elem );
    if (!out) return E_POINTER;
    if (el && el->automation_name[0])
        return WindowsCreateString( el->automation_name, (UINT32)wcslen( el->automation_name ), out );
    *out = NULL;
    return S_OK;
}
static HRESULT WINAPI auto_props_SetName( void *iface, void *elem, HSTRING val )
{
    struct uwp_element *el = uwp_elem_from_any( elem );
    const WCHAR *raw = val ? WindowsGetStringRawBuffer( val, NULL ) : NULL;
    if (el)
        lstrcpynW( el->automation_name, raw ? raw : L"", ARRAY_SIZE(el->automation_name) );
    return S_OK;
}
static const void *attached_statics_vtbl[64] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = attached_prop_get_dp,
    [7 ... 8] = uwp_generic_noop,
    [9] = attached_prop_get_dp,
    [10 ... 11] = uwp_generic_noop,
    [12] = attached_prop_get_dp,
    [13 ... 14] = uwp_generic_noop,
    [15] = attached_prop_get_dp,
    [16 ... 17] = uwp_generic_noop,
    [18] = attached_prop_get_dp,
    [19 ... 20] = uwp_generic_noop,
    [21] = attached_prop_get_dp,
    [22 ... 23] = uwp_generic_noop,
    [24] = attached_prop_get_dp,
    [25 ... 26] = uwp_generic_noop,
    [27] = attached_prop_get_dp,
    [28 ... 29] = uwp_generic_noop,
    [30] = attached_prop_get_dp,
    [31] = auto_props_GetName,
    [32] = auto_props_SetName,
    [33 ... 63] = uwp_generic_noop,
};
struct uwp_class_factory auto_props_factory_obj =
    { {(const struct IActivationFactoryVtbl *)attached_statics_vtbl}, {(const struct IInspectableVtbl *)attached_statics_vtbl}, {(const struct IInspectableVtbl *)attached_statics_vtbl}, L"Windows.UI.Xaml.Automation.AutomationProperties", { NULL } };

static HRESULT WINAPI feap_CreatePeerForElement( void *iface, void *el, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}
static HRESULT WINAPI feap_CreateInstanceWithOwner( void *iface, void *owner, IInspectable *outer,
        IInspectable **inner, void **instance )
{
    return uwp_element_create( L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer",
                               outer, inner, instance );
}
static const void *feap_factory_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = feap_CreateInstanceWithOwner,
    [7 ... 15] = uwp_generic_noop,
};
static const void *feap_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = feap_CreatePeerForElement,
    [7] = feap_CreatePeerForElement,
    [8 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory feap_factory_obj =
    { {(const struct IActivationFactoryVtbl *)feap_statics_vtbl}, {(const struct IInspectableVtbl *)feap_factory_vtbl}, {(const struct IInspectableVtbl *)feap_statics_vtbl}, L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer", { NULL } };

static HRESULT WINAPI th_FromLengths( void *iface, double left, double top, double right, double bottom, double *out )
{
    if (!out) return E_POINTER;
    out[0] = left;
    out[1] = top;
    out[2] = right;
    out[3] = bottom;
    return S_OK;
}
static HRESULT WINAPI th_FromUniformLength( void *iface, double uniform, double *out )
{
    if (!out) return E_POINTER;
    out[0] = out[1] = out[2] = out[3] = uniform;
    return S_OK;
}
static const void *th_statics_vtbl[16] =
{
    [0] = uwp_cf_QI,
    [1] = uwp_cf_AddRef,
    [2] = uwp_cf_Release,
    [3] = uwp_cf_GetIids,
    [4] = uwp_cf_GetRuntimeClassName,
    [5] = uwp_cf_GetTrustLevel,
    [6] = th_FromLengths,
    [7] = th_FromUniformLength,
    [8 ... 15] = uwp_generic_noop,
};
struct uwp_class_factory th_factory_obj =
    { {(const struct IActivationFactoryVtbl *)th_statics_vtbl}, {(const struct IInspectableVtbl *)th_statics_vtbl}, {(const struct IInspectableVtbl *)th_statics_vtbl}, L"Windows.UI.Xaml.ThicknessHelper", { NULL } };

/*
 * Static class -> factory map for the UWP/XAML activation factory.  The classes below
 * resolve to a fixed singleton factory object; the lookup used to be a linear chain of
 * wcscmp over every name, re-run on every activation.  A 32-bucket FNV-1a index over the
 * same (name, factory) pairs turns that into a hash probe.  The index is built once
 * (InitOnceExecuteOnce) from this table, then read-only, so it needs no lock after the
 * first call.  Behavior is identical to the old chain: the first matching name wins, and
 * each entry hands out exactly the factory object the old wcscmp branch returned.
 */

const struct xaml_static_factory xaml_static_factories[] =
{
    { L"Windows.UI.Xaml.ThicknessHelper",                                (IActivationFactory *)&th_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.ThicknessHelper",                              (IActivationFactory *)&th_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer",(IActivationFactory *)&feap_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Media.SolidColorBrush",                          (IActivationFactory *)&scb_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Markup.XamlBindingHelper",                       (IActivationFactory *)&xbh_factory_obj },
    { L"Microsoft.UI.Xaml.Markup.XamlBindingHelper",                     (IActivationFactory *)&xbh_factory_obj },
    { L"Windows.UI.Xaml.Markup.XamlReader",                              (IActivationFactory *)&xbh_factory_obj },
    { L"Microsoft.UI.Xaml.Markup.XamlReader",                            (IActivationFactory *)&xbh_factory_obj },
    { L"Windows.UI.Xaml.VisualStateManager",                             (IActivationFactory *)&vsm_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.VisualStateManager",                           (IActivationFactory *)&vsm_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Media.VisualTreeHelper",                         (IActivationFactory *)&vth_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.Media.VisualTreeHelper",                       (IActivationFactory *)&vth_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Automation.AutomationProperties",                (IActivationFactory *)&auto_props_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Controls.ToolTipService",                        (IActivationFactory *)&auto_props_factory_obj.IActivationFactory_iface },
    { L"Windows.UI.Xaml.Media.Animation.Storyboard",                     (IActivationFactory *)&sb_factory_obj.IActivationFactory_iface },
    { L"Microsoft.UI.Xaml.Media.Animation.Storyboard",                   (IActivationFactory *)&sb_factory_winui3_obj.IActivationFactory_iface },
};
const size_t xaml_static_factories_count = ARRAY_SIZE( xaml_static_factories );


const struct xaml_static_factory *xaml_factory_hash[XAML_FACTORY_HASH_SIZE];
INIT_ONCE xaml_factory_hash_once = INIT_ONCE_STATIC_INIT;
