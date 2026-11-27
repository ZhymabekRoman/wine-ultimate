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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <excpt.h>
#include "wine/exception.h"
#include "private.h"
#include "xbf_known_types.h"
#include "weakreference.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "uwp_private.h"
#include "xbf_private.h"
WINE_DEFAULT_DEBUG_CHANNEL(xaml);

static SRWLOCK g_xbf_aot_lock = SRWLOCK_INIT;
static struct xbf_aot_cache_slot g_xbf_aot_cache[XBF_AOT_CACHE_SLOTS];
static UINT64 g_xbf_aot_lru = 0;

UINT64 xbf_aot_compute_hash( const BYTE *xbf, DWORD xbf_sz, const WCHAR *root_cls, int only_sub_idx )
{
    UINT64 h = 14695981039346656037ULL;
    DWORD i;
    const BYTE *p;

    if (xbf_is_immutable_pri_blob( xbf, xbf_sz ))
    {
        uintptr_t ptr_val = (uintptr_t)xbf;
        h ^= 0x5au;
        h *= 1099511628211ULL;
        p = (const BYTE *)&ptr_val;
        for (i = 0; i < sizeof(ptr_val); i++)
        {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    }
    else
    {
        for (i = 0; i < xbf_sz; i++)
        {
            h ^= xbf[i];
            h *= 1099511628211ULL;
        }
    }
    p = (const BYTE *)&xbf_sz;
    for (i = 0; i < sizeof(xbf_sz); i++)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    if (root_cls)
    {
        const BYTE *wc = (const BYTE *)root_cls;
        size_t wlen = wcslen( root_cls ) * sizeof(WCHAR);
        size_t j;
        for (j = 0; j < wlen; j++)
        {
            h ^= wc[j];
            h *= 1099511628211ULL;
        }
    }
    p = (const BYTE *)&only_sub_idx;
    for (i = 0; i < sizeof(only_sub_idx); i++)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

UINT32 xbf_aot_intern_str( struct xbf_aot_builder *b, const WCHAR *s )
{
    size_t len, bytes;
    UINT32 off;

    if (!b || !s || !s[0]) return 0;
    if (b->str_pool_bytes == 0)
    {
        /* Reserve offset 0 for empty string L"" */
        if (!xaml_vec_grow( (void **)&b->str_pool, &b->str_pool_cap, 2, 1, 1024 ))
        {
            b->aborted = TRUE;
            return 0;
        }
        b->str_pool[0] = 0;
        b->str_pool[1] = 0;
        b->str_pool_bytes = 2;
    }
    len = wcslen( s );
    bytes = (len + 1) * sizeof(WCHAR);
    off = (b->str_pool_bytes + 1u) & ~1u;
    if (!xaml_vec_grow( (void **)&b->str_pool, &b->str_pool_cap, off + (UINT32)bytes, 1, 2048 ))
    {
        b->aborted = TRUE;
        return 0;
    }
    memcpy( b->str_pool + off, s, bytes );
    b->str_pool_bytes = off + (UINT32)bytes;
    return off;
}

void xbf_aot_add_ref( struct xbf_aot_builder *b, const WCHAR *full_name, const WCHAR *short_name, BOOL is_res )
{
    struct xbf_aot_ref *r;
    if (!b || b->aborted) return;
    if (!xaml_vec_grow( (void **)&b->refs, &b->ref_cap, b->ref_cnt + 1, sizeof(*b->refs), 16 ))
    {
        b->aborted = TRUE;
        return;
    }
    r = &b->refs[b->ref_cnt++];
    r->full_name_off = xbf_aot_intern_str( b, full_name );
    r->short_name_off = xbf_aot_intern_str( b, short_name );
    r->is_resource_path = is_res ? 1u : 0u;
}

void xbf_aot_add_res_uri( struct xbf_aot_builder *b, const WCHAR *uri )
{
    if (!b || b->aborted || !uri || !uri[0]) return;
    if (!xaml_vec_grow( (void **)&b->res_uris, &b->res_uri_cap, b->res_uri_cnt + 1, sizeof(*b->res_uris), 8 ))
    {
        b->aborted = TRUE;
        return;
    }
    b->res_uris[b->res_uri_cnt++] = xbf_aot_intern_str( b, uri );
}

UINT32 xbf_aot_add_node( struct xbf_aot_builder *b, const WCHAR *cls_name, const WCHAR *short_name,
                                WORD tid, UINT8 flags, BOOL has_init_argb, UINT32 init_argb )
{
    struct xbf_aot_node *n;
    if (!b || b->aborted) return (~0u);
    if (!xaml_vec_grow( (void **)&b->nodes, &b->node_cap, b->node_cnt + 1, sizeof(*b->nodes), 32 ))
    {
        b->aborted = TRUE;
        return (~0u);
    }
    n = &b->nodes[b->node_cnt];
    n->class_name_off = xbf_aot_intern_str( b, cls_name );
    n->short_name_off = xbf_aot_intern_str( b, short_name );
    n->tid = tid;
    n->flags = flags;
    n->has_init_argb = has_init_argb ? 1u : 0u;
    n->init_argb = init_argb;
    n->template_sub_idx = 0xffffffffu;
    return b->node_cnt++;
}

void xbf_aot_emit_op( struct xbf_aot_builder *b, const struct xbf_aot_op *op_in )
{
    if (!b || b->aborted) return;
    if (!xaml_vec_grow( (void **)&b->ops, &b->op_cap, b->op_cnt + 1, sizeof(*b->ops), 64 ))
    {
        b->aborted = TRUE;
        return;
    }
    b->ops[b->op_cnt++] = *op_in;
}

void xbf_aot_add_defer( struct xbf_aot_builder *b, const WCHAR *name, int sub_idx, UINT32 parent_panel_idx )
{
    struct xbf_aot_defer *d;
    if (!b || b->aborted) return;
    if (!xaml_vec_grow( (void **)&b->defers, &b->defer_cap, b->defer_cnt + 1, sizeof(*b->defers), 8 ))
    {
        b->aborted = TRUE;
        return;
    }
    d = &b->defers[b->defer_cnt++];
    d->name_off = xbf_aot_intern_str( b, name );
    d->sub_idx = sub_idx;
    d->parent_panel_idx = parent_panel_idx;
}

UINT32 xbf_aot_append_setters( struct xbf_aot_builder *b, const struct uwp_style_setter *setters, int cnt )
{
    UINT32 start;
    if (!b || b->aborted || !setters || cnt <= 0) return 0;
    start = b->style_setter_cnt;
    if (!xaml_vec_grow( (void **)&b->style_setters, &b->style_setter_cap, start + (UINT32)cnt,
                        sizeof(*b->style_setters), 16 ))
    {
        b->aborted = TRUE;
        return 0;
    }
    memcpy( b->style_setters + start, setters, (size_t)cnt * sizeof(*b->style_setters) );
    b->style_setter_cnt += (UINT32)cnt;
    return start;
}

void xbf_aot_snapshot_vsm( struct xbf_aot_builder *b, const struct xbf_vsm_holder *holder, int start_group_idx )
{
    int g, s, t;
    if (!b || b->aborted || !holder || start_group_idx >= holder->vsm_group_cnt) return;
    for (g = start_group_idx; g < holder->vsm_group_cnt; g++)
    {
        const struct uwp_vsm_group *src_g = &holder->vsm_groups[g];
        struct xbf_aot_vsm_group *dst_g;
        UINT32 state_start = b->vsm_state_cnt;

        if (!xaml_vec_grow( (void **)&b->vsm_groups, &b->vsm_group_cap, b->vsm_group_cnt + 1,
                            sizeof(*b->vsm_groups), 8 ))
        {
            b->aborted = TRUE;
            return;
        }
        dst_g = &b->vsm_groups[b->vsm_group_cnt++];
        dst_g->name_off = xbf_aot_intern_str( b, src_g->name );
        dst_g->state_start = state_start;
        dst_g->state_cnt = (UINT32)src_g->state_cnt;

        for (s = 0; s < src_g->state_cnt; s++)
        {
            const struct uwp_vsm_state *src_s = &src_g->states[s];
            struct xbf_aot_vsm_state *dst_s;
            UINT32 setter_start = b->vsm_setter_cnt;

            if (!xaml_vec_grow( (void **)&b->vsm_states, &b->vsm_state_cap, b->vsm_state_cnt + 1,
                                sizeof(*b->vsm_states), 16 ))
            {
                b->aborted = TRUE;
                return;
            }
            dst_s = &b->vsm_states[b->vsm_state_cnt++];
            dst_s->name_off = xbf_aot_intern_str( b, src_s->name );
            dst_s->has_trigger = src_s->has_trigger ? 1u : 0u;
            dst_s->min_window_width = src_s->min_window_width;
            dst_s->min_window_height = src_s->min_window_height;
            dst_s->setter_start = setter_start;
            dst_s->setter_cnt = (UINT32)src_s->setter_cnt;

            for (t = 0; t < src_s->setter_cnt; t++)
            {
                const struct uwp_vsm_setter *src_t = &src_s->setters[t];
                struct xbf_aot_vsm_setter *dst_t;
                if (!xaml_vec_grow( (void **)&b->vsm_setters, &b->vsm_setter_cap, b->vsm_setter_cnt + 1,
                                    sizeof(*b->vsm_setters), 32 ))
                {
                    b->aborted = TRUE;
                    return;
                }
                dst_t = &b->vsm_setters[b->vsm_setter_cnt++];
                dst_t->target_off = xbf_aot_intern_str( b, src_t->target );
                dst_t->property_off = xbf_aot_intern_str( b, src_t->property );
                dst_t->val_str_off = xbf_aot_intern_str( b, src_t->val_str );
                dst_t->vt = src_t->vt;
                dst_t->is_res_ref = src_t->is_res_ref ? 1 : 0;
                dst_t->num = src_t->num;
            }
        }
    }
}

void xbf_aot_builder_free( struct xbf_aot_builder *b )
{
    if (!b) return;
    free( b->refs );
    free( b->res_uris );
    free( b->nodes );
    free( b->ops );
    free( b->style_setters );
    free( b->vsm_groups );
    free( b->vsm_states );
    free( b->vsm_setters );
    free( b->defers );
    free( b->str_pool );
}

struct xbf_aot_header *xbf_aot_finalize( struct xbf_aot_builder *b, const struct xbf_ctx *ctx )
{
    struct xbf_aot_header hdr = {0};
    UINT32 off;
    BYTE *buf;

    if (!b || b->aborted) return NULL;
    if (b->str_pool_bytes == 0)
        xbf_aot_intern_str( b, L"" );

    hdr.magic = XBF_AOT_MAGIC;
    hdr.version = XBF_AOT_VERSION;
    hdr.hash = b->hash;
    hdr.xbf_sz = b->xbf_sz;
    hdr.only_sub_idx = b->only_sub_idx;

    hdr.type_cnt = ctx ? ctx->type_cnt : 0;
    hdr.prop_cnt = ctx ? ctx->prop_cnt : 0;
    hdr.ref_cnt = b->ref_cnt;
    hdr.res_uri_cnt = b->res_uri_cnt;
    hdr.node_cnt = b->node_cnt;
    hdr.op_cnt = b->op_cnt;
    hdr.style_setter_cnt = b->style_setter_cnt;
    hdr.vsm_group_cnt = b->vsm_group_cnt;
    hdr.vsm_state_cnt = b->vsm_state_cnt;
    hdr.vsm_setter_cnt = b->vsm_setter_cnt;
    hdr.defer_cnt = b->defer_cnt;
    hdr.str_pool_bytes = b->str_pool_bytes;

    off = (sizeof(hdr) + 7u) & ~7u;
    hdr.off_types = off;         off += (hdr.type_cnt * (UINT32)sizeof(struct xbf_type_info) + 7u) & ~7u;
    hdr.off_props = off;         off += (hdr.prop_cnt * (UINT32)sizeof(struct xbf_prop_info) + 7u) & ~7u;
    hdr.off_refs = off;          off += (hdr.ref_cnt * (UINT32)sizeof(struct xbf_aot_ref) + 7u) & ~7u;
    hdr.off_res_uris = off;      off += (hdr.res_uri_cnt * (UINT32)sizeof(UINT32) + 7u) & ~7u;
    hdr.off_nodes = off;         off += (hdr.node_cnt * (UINT32)sizeof(struct xbf_aot_node) + 7u) & ~7u;
    hdr.off_ops = off;           off += (hdr.op_cnt * (UINT32)sizeof(struct xbf_aot_op) + 7u) & ~7u;
    hdr.off_style_setters = off; off += (hdr.style_setter_cnt * (UINT32)sizeof(struct uwp_style_setter) + 7u) & ~7u;
    hdr.off_vsm_groups = off;    off += (hdr.vsm_group_cnt * (UINT32)sizeof(struct xbf_aot_vsm_group) + 7u) & ~7u;
    hdr.off_vsm_states = off;    off += (hdr.vsm_state_cnt * (UINT32)sizeof(struct xbf_aot_vsm_state) + 7u) & ~7u;
    hdr.off_vsm_setters = off;   off += (hdr.vsm_setter_cnt * (UINT32)sizeof(struct xbf_aot_vsm_setter) + 7u) & ~7u;
    hdr.off_defers = off;        off += (hdr.defer_cnt * (UINT32)sizeof(struct xbf_aot_defer) + 7u) & ~7u;
    hdr.off_str_pool = off;      off += (hdr.str_pool_bytes + 7u) & ~7u;
    hdr.total_bytes = off;

    buf = calloc( 1, hdr.total_bytes );
    if (!buf) return NULL;

    memcpy( buf, &hdr, sizeof(hdr) );
    if (hdr.type_cnt)         memcpy( buf + hdr.off_types, ctx->types, hdr.type_cnt * sizeof(struct xbf_type_info) );
    if (hdr.prop_cnt)         memcpy( buf + hdr.off_props, ctx->props, hdr.prop_cnt * sizeof(struct xbf_prop_info) );
    if (hdr.ref_cnt)          memcpy( buf + hdr.off_refs, b->refs, hdr.ref_cnt * sizeof(struct xbf_aot_ref) );
    if (hdr.res_uri_cnt)      memcpy( buf + hdr.off_res_uris, b->res_uris, hdr.res_uri_cnt * sizeof(UINT32) );
    if (hdr.node_cnt)         memcpy( buf + hdr.off_nodes, b->nodes, hdr.node_cnt * sizeof(struct xbf_aot_node) );
    if (hdr.op_cnt)           memcpy( buf + hdr.off_ops, b->ops, hdr.op_cnt * sizeof(struct xbf_aot_op) );
    if (hdr.style_setter_cnt) memcpy( buf + hdr.off_style_setters, b->style_setters, hdr.style_setter_cnt * sizeof(struct uwp_style_setter) );
    if (hdr.vsm_group_cnt)    memcpy( buf + hdr.off_vsm_groups, b->vsm_groups, hdr.vsm_group_cnt * sizeof(struct xbf_aot_vsm_group) );
    if (hdr.vsm_state_cnt)    memcpy( buf + hdr.off_vsm_states, b->vsm_states, hdr.vsm_state_cnt * sizeof(struct xbf_aot_vsm_state) );
    if (hdr.vsm_setter_cnt)   memcpy( buf + hdr.off_vsm_setters, b->vsm_setters, hdr.vsm_setter_cnt * sizeof(struct xbf_aot_vsm_setter) );
    if (hdr.defer_cnt)        memcpy( buf + hdr.off_defers, b->defers, hdr.defer_cnt * sizeof(struct xbf_aot_defer) );
    if (hdr.str_pool_bytes)   memcpy( buf + hdr.off_str_pool, b->str_pool, hdr.str_pool_bytes );

    return (struct xbf_aot_header *)buf;
}

static const WCHAR *xbf_aot_str( const struct xbf_aot_header *hdr, UINT32 off )
{
    const BYTE *pool;
    if (!hdr || off + 1 >= hdr->str_pool_bytes) return L"";
    pool = (const BYTE *)hdr + hdr->off_str_pool;
    return (const WCHAR *)(pool + off);
}

struct xbf_aot_cache_slot *xbf_aot_cache_acquire( UINT64 hash, DWORD xbf_sz, int only_sub_idx )
{
    int i;

    AcquireSRWLockShared( &g_xbf_aot_lock );
    for (i = 0; i < XBF_AOT_CACHE_SLOTS; i++)
    {
        if (g_xbf_aot_cache[i].tpl && g_xbf_aot_cache[i].hash == hash &&
            g_xbf_aot_cache[i].tpl->xbf_sz == xbf_sz &&
            g_xbf_aot_cache[i].tpl->only_sub_idx == only_sub_idx)
        {
            InterlockedIncrement( &g_xbf_aot_cache[i].ref );
            g_xbf_aot_cache[i].last_used = ++g_xbf_aot_lru;
            ReleaseSRWLockShared( &g_xbf_aot_lock );
            return &g_xbf_aot_cache[i];
        }
    }
    ReleaseSRWLockShared( &g_xbf_aot_lock );
    return NULL;
}

void xbf_aot_cache_insert( struct xbf_aot_header *tpl )
{
    int i, victim = -1;
    UINT64 best_lru = (~0ULL);
    if (!tpl) return;

    AcquireSRWLockExclusive( &g_xbf_aot_lock );
    for (i = 0; i < XBF_AOT_CACHE_SLOTS; i++)
    {
        if (g_xbf_aot_cache[i].tpl && g_xbf_aot_cache[i].hash == tpl->hash)
        {
            ReleaseSRWLockExclusive( &g_xbf_aot_lock );
            free( tpl );
            return;
        }
    }
    for (i = 0; i < XBF_AOT_CACHE_SLOTS; i++)
    {
        if (!g_xbf_aot_cache[i].tpl) { victim = i; break; }
        if (g_xbf_aot_cache[i].ref == 0 && g_xbf_aot_cache[i].last_used < best_lru)
        {
            best_lru = g_xbf_aot_cache[i].last_used;
            victim = i;
        }
    }
    if (victim >= 0)
    {
        free( g_xbf_aot_cache[victim].tpl );
        g_xbf_aot_cache[victim].hash = tpl->hash;
        g_xbf_aot_cache[victim].tpl = tpl;
        g_xbf_aot_cache[victim].last_used = ++g_xbf_aot_lru;
        g_xbf_aot_cache[victim].ref = 0;
        ReleaseSRWLockExclusive( &g_xbf_aot_lock );
        return;
    }
    ReleaseSRWLockExclusive( &g_xbf_aot_lock );
    free( tpl );
}

struct xbf_aot_exec_ctx
{
    const struct xbf_aot_header *hdr;
    const struct xbf_aot_node *nodes;
    const struct xbf_aot_op *ops;
    const struct uwp_style_setter *style_setters;
    const BYTE *xbf;
    DWORD xbf_sz;
    IInspectable *component;
    void *connector;
    void *app_outer;
    struct uwp_element *root_el;
    struct uwp_element **out_created;
    const char *base_xbf_dir;
    struct uwp_rd_bucket *target_rd;
    BOOL is_template;
    struct xbf_ctx fake_ctx;
    struct uwp_element **nodes_el;
    IInspectable **nodes_raw;
    UINT32 *deferred_res_ops;
    int deferred_res_cnt;
    int deferred_res_cap;
    void *root_binding;
    void *dt_binding;
};

static void aot_replay_vsm_groups( const struct xbf_aot_header *hdr, struct uwp_element *root_el )
{
    const struct xbf_aot_vsm_group *vsm_groups = (const struct xbf_aot_vsm_group *)((const BYTE *)hdr + hdr->off_vsm_groups);
    const struct xbf_aot_vsm_state *vsm_states = (const struct xbf_aot_vsm_state *)((const BYTE *)hdr + hdr->off_vsm_states);
    const struct xbf_aot_vsm_setter *vsm_setters = (const struct xbf_aot_vsm_setter *)((const BYTE *)hdr + hdr->off_vsm_setters);
    UINT32 g, s, t;

    if (!root_el || hdr->vsm_group_cnt == 0) return;
    for (g = 0; g < hdr->vsm_group_cnt; g++)
    {
        const struct xbf_aot_vsm_group *ag = &vsm_groups[g];
        struct uwp_vsm_group *grp = uwp_vsm_add_group( root_el, xbf_aot_str( hdr, ag->name_off ) );
        if (!grp) break;
        for (s = 0; s < ag->state_cnt && ag->state_start + s < hdr->vsm_state_cnt; s++)
        {
            const struct xbf_aot_vsm_state *as = &vsm_states[ag->state_start + s];
            struct uwp_vsm_state *st = uwp_vsm_add_state( grp, xbf_aot_str( hdr, as->name_off ) );
            if (!st) break;
            st->has_trigger = as->has_trigger != 0;
            st->min_window_width = as->min_window_width;
            st->min_window_height = as->min_window_height;
            for (t = 0; t < as->setter_cnt && as->setter_start + t < hdr->vsm_setter_cnt; t++)
            {
                const struct xbf_aot_vsm_setter *at = &vsm_setters[as->setter_start + t];
                uwp_vsm_state_add_setter( st, xbf_aot_str( hdr, at->target_off ),
                                          xbf_aot_str( hdr, at->property_off ),
                                          xbf_aot_str( hdr, at->val_str_off ),
                                          at->vt, at->num, at->is_res_ref != 0 );
            }
        }
    }
}

static void aot_load_custom_rd_xbf( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_node *nd,
                                    struct uwp_rd_bucket *sub_rd )
{
    const WCHAR *ns, *dot;
    WCHAR xaml_name[256];
    BOOL loaded = FALSE;

    if (!sub_rd || (nd->tid & 0x8000) || nd->tid >= ec->hdr->type_cnt) return;
    if (wcsncmp( ec->fake_ctx.types[nd->tid].xmlns, L"using:", 6 ) != 0) return;
    if (!ec->fake_ctx.types[nd->tid].short_name[0] ||
        !wcscmp( ec->fake_ctx.types[nd->tid].short_name, L"XamlControlsResources" ))
        return;

    ns = ec->fake_ctx.types[nd->tid].xmlns + 6;
    dot = wcschr( ns, L'.' );
    if (dot && dot[1])
    {
        WCHAR rel_ns[128];
        int ni;
        lstrcpynW( rel_ns, dot + 1, ARRAY_SIZE(rel_ns) );
        for (ni = 0; rel_ns[ni]; ni++)
            if (rel_ns[ni] == L'.') rel_ns[ni] = L'/';
        swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s/%s.xaml", rel_ns, ec->fake_ctx.types[nd->tid].short_name );
        uwp_load_res_xbf_into_bucket( ec->base_xbf_dir, xaml_name, sub_rd );
        loaded = (sub_rd->count > 0 || sub_rd->merged_cnt > 0 || sub_rd->theme_dark || sub_rd->theme_default);
    }
    if (!loaded && ns[0])
    {
        WCHAR full_ns[128];
        int ni;
        lstrcpynW( full_ns, ns, ARRAY_SIZE(full_ns) );
        for (ni = 0; full_ns[ni]; ni++)
            if (full_ns[ni] == L'.') full_ns[ni] = L'/';
        swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s/%s.xaml", full_ns, ec->fake_ctx.types[nd->tid].short_name );
        uwp_load_res_xbf_into_bucket( ec->base_xbf_dir, xaml_name, sub_rd );
        loaded = (sub_rd->count > 0 || sub_rd->merged_cnt > 0 || sub_rd->theme_dark || sub_rd->theme_default);
    }
    if (!loaded)
    {
        swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s.xaml", ec->fake_ctx.types[nd->tid].short_name );
        uwp_load_res_xbf_into_bucket( ec->base_xbf_dir, xaml_name, sub_rd );
    }
}

static void aot_exec_create_app_node( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_node *nd,
                                      const WCHAR *cls_name, const WCHAR *short_name,
                                      struct uwp_element **out_el, IInspectable **out_inst )
{
    struct uwp_element *el = NULL, *prev_head;
    IInspectable *inst = NULL;
    HRESULT phr;

    if (ec->root_el) uwp_mark_xbf_ref_instantiated( ec->root_el, cls_name );
    if (nd->flags & XBF_AOT_NODE_NO_PROVIDER)
    {
        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        *out_el = uwp_elem_from_any( inst );
        *out_inst = inst;
        return;
    }

    AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
    prev_head = g_all_uwp_elems_head;
    ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );

    phr = uwp_create_instance_from_provider( cls_name, (void **)&inst );
    if (SUCCEEDED(phr) && inst && g_all_uwp_elems_head != prev_head)
        el = uwp_elem_from_any( inst );
    if (!el)
    {
        AcquireSRWLockExclusive( &g_all_uwp_elems_lock );
        if (g_all_uwp_elems_head != prev_head)
        {
            struct uwp_element *e;
            for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
                if (e->kind == UWP_KIND_USER_CONTROL || e->kind == UWP_KIND_PAGE) el = e;
            if (!el)
                for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg) el = e;
        }
        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
    }
    if (el)
    {
        if (inst && !el->provider_inst)
        {
            el->provider_inst = inst;
            uwp_elem_register_alias( el, inst );
        }
        else if (!inst) inst = el->outer ? el->outer : &el->IInspectable_iface;
        lstrcpynW( el->class_name, cls_name, ARRAY_SIZE(el->class_name) );
        el->kind = classify_uwp_class( el->class_name );
        uwp_elem_sync_class_flags( el );
    }
    if (FAILED(phr) || !inst)
    {
        if (!inst) uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        if (!el) el = uwp_elem_from_any( inst );
        if (el && !el->xbf_parsed && !el->children_coll && !el->content &&
            (nd->flags & XBF_AOT_NODE_USING_NS) && short_name[0])
            uwp_populate_element_from_pri_xbf( el, short_name );
    }
    *out_el = el;
    *out_inst = inst;
}

static void aot_exec_create_node( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_op *op )
{
    const struct xbf_aot_node *nd;
    const WCHAR *cls_name, *short_name;
    struct uwp_element *el = NULL;
    IInspectable *inst = NULL;

    if (op->node_idx >= ec->hdr->node_cnt) return;
    nd = &ec->nodes[op->node_idx];
    cls_name = xbf_aot_str( ec->hdr, nd->class_name_off );
    short_name = xbf_aot_str( ec->hdr, nd->short_name_off );

    if (nd->flags & XBF_AOT_NODE_ROOT_REUSE)
    {
        el = ec->root_el;
        inst = ec->component;
        if ((nd->flags & XBF_AOT_NODE_IS_RD) && ec->target_rd && el)
        {
            el->rd_bucket = ec->target_rd;
            ec->target_rd->owner_el = el;
        }
    }
    else if (nd->flags & XBF_AOT_NODE_LIGHTWEIGHT)
    {
        el = NULL;
        inst = NULL;
    }
    else if (!ec->fake_ctx.is_uwp_conn && ec->connector)
    {
        if (nd->flags & XBF_AOT_NODE_BUILTIN)
        {
            element_create_typed( cls_name, (void **)&inst );
        }
        else if (nd->flags & XBF_AOT_NODE_APP_TYPE)
        {
            if ((nd->flags & XBF_AOT_NODE_NO_PROVIDER) ||
                !wcsncmp( cls_name, L"Microsoft.UI.Xaml.", 18 ) ||
                !wcsncmp( cls_name, L"Windows.UI.Xaml.", 16 ))
                element_create_typed( cls_name, (void **)&inst );
            else if (FAILED(uwp_create_instance_from_provider( cls_name, (void **)&inst )) || !inst)
                element_create_typed( cls_name, (void **)&inst );
        }
        else
        {
            element_create_typed( cls_name, (void **)&inst );
        }
        el = NULL;
    }
    else if (nd->flags & XBF_AOT_NODE_BUILTIN)
    {
        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        el = uwp_elem_from_any( inst );
        if (el && nd->tid == XBF_TYPE_DEFERRED_ELEMENT) el->kind = UWP_KIND_DEFERRED;
    }
    else if (nd->flags & XBF_AOT_NODE_APP_TYPE)
        aot_exec_create_app_node( ec, nd, cls_name, short_name, &el, &inst );
    else
    {
        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        el = uwp_elem_from_any( inst );
    }

    if (el)
    {
        if (ec->root_el) el->owner_component = ec->root_el;
        if (nd->has_init_argb)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa) { sa->brush_argb = nd->init_argb; sa->has_brush_argb = TRUE; }
        }
        if (nd->template_sub_idx != 0xffffffffu && ec->xbf && ec->xbf_sz)
        {
            el->template_xbf_data = ec->xbf;
            el->template_xbf_size = ec->xbf_sz;
            el->template_sub_idx = nd->template_sub_idx;
            uwp_set_template_owner( &el->template_owner_comp, ec->component );
        }
        if (nd->flags & XBF_AOT_NODE_IS_RD)
        {
            el->kind = UWP_KIND_RESOURCE_DICT;
            if (((nd->flags & XBF_AOT_NODE_ROOT_REUSE) || op->node_idx == 0) && ec->target_rd)
            {
                el->rd_bucket = ec->target_rd;
                ec->target_rd->owner_el = el;
            }
            else
                aot_load_custom_rd_xbf( ec, nd, uwp_rd_bucket_for_el( el, TRUE ) );
        }
    }
    ec->nodes_el[op->node_idx] = el;
    ec->nodes_raw[op->node_idx] = inst;
    if (ec->out_created && !*ec->out_created && el) *ec->out_created = el;
}

static void aot_exec_connect( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_op *op )
{
    struct uwp_element *el = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->node_idx] : NULL;
    IInspectable *raw = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->node_idx] : NULL;
    const struct abi_component_connector *acc;

    if (el) el->conn_id = op->cid;
    if (!ec->connector || !raw) return;

    acc = (const struct abi_component_connector *)ec->connector;
    TRACE( "AOT Connect(cid=%d, target=%p [%s %s])\n", op->cid, raw,
           el ? debugstr_w( el->class_name ) : "?", el ? debugstr_w( el->name ) : "" );
    acc->lpVtbl->Connect( ec->connector, op->cid, raw );

    if (op->cid == 1 && !ec->is_template && !ec->out_created)
    {
        void *binding = NULL;
        if (ec->fake_ctx.is_uwp_conn)
        {
            void *conn2 = NULL;
            if (ec->component &&
                SUCCEEDED(IInspectable_QueryInterface( ec->component, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
            {
                const struct abi_component_connector2 *acc2 = (const struct abi_component_connector2 *)conn2;
                if (acc2->lpVtbl->GetBindingConnector)
                    acc2->lpVtbl->GetBindingConnector( conn2, 1, raw, &binding );
                IUnknown_Release( (IUnknown *)conn2 );
            }
        }
        else
        {
            const struct abi_component_connector_winui *wcc = (const struct abi_component_connector_winui *)ec->connector;
            if (wcc->lpVtbl->GetBindingConnector)
                wcc->lpVtbl->GetBindingConnector( ec->connector, 1, raw, &binding );
        }
        if (binding)
        {
            const struct abi_component_connector *bcc = (const struct abi_component_connector *)binding;
            if (ec->root_binding) IUnknown_Release( (IUnknown *)ec->root_binding );
            ec->root_binding = binding;
            if (ec->root_el)
            {
                if (ec->root_el->binding_connector) IUnknown_Release( (IUnknown *)ec->root_el->binding_connector );
                ec->root_el->binding_connector = binding;
                IUnknown_AddRef( (IUnknown *)binding );
            }
            if (bcc->lpVtbl->Connect) bcc->lpVtbl->Connect( binding, 1, raw );
        }
        return;
    }

    if (!ec->is_template && !ec->out_created)
    {
        void *bc = ec->root_binding ? ec->root_binding : (ec->root_el ? ec->root_el->binding_connector : NULL);
        if (bc)
        {
            const struct abi_component_connector *bcc = (const struct abi_component_connector *)bc;
            if (bcc->lpVtbl->Connect) bcc->lpVtbl->Connect( bc, op->cid, raw );
        }
    }
    else if (ec->out_created)
    {
        if (!ec->dt_binding && ec->component)
        {
            void *bc = NULL;
            if (ec->fake_ctx.is_uwp_conn)
            {
                void *conn2 = NULL;
                if (SUCCEEDED(IInspectable_QueryInterface( ec->component, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
                {
                    const struct abi_component_connector2 *acc2 = (const struct abi_component_connector2 *)conn2;
                    if (acc2->lpVtbl->GetBindingConnector)
                        acc2->lpVtbl->GetBindingConnector( conn2, op->cid, raw, &bc );
                    IUnknown_Release( (IUnknown *)conn2 );
                }
            }
            else
            {
                const struct abi_component_connector_winui *wcc = (const struct abi_component_connector_winui *)ec->connector;
                if (wcc->lpVtbl->GetBindingConnector)
                    wcc->lpVtbl->GetBindingConnector( ec->connector, op->cid, raw, &bc );
            }
            if (bc)
            {
                ec->dt_binding = bc;
                if (el && !el->dt_binding_connector) { el->dt_binding_connector = bc; IUnknown_AddRef( (IUnknown *)bc ); }
                if (el && !el->binding_connector) { el->binding_connector = bc; IUnknown_AddRef( (IUnknown *)bc ); }
            }
        }
        if (ec->dt_binding)
        {
            const struct abi_component_connector *bcc = (const struct abi_component_connector *)ec->dt_binding;
            if (bcc->lpVtbl->Connect) bcc->lpVtbl->Connect( ec->dt_binding, op->cid, raw );
        }
    }
}

static void aot_exec_set_prop( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_op *op, UINT32 op_idx )
{
    struct uwp_element *el = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->node_idx] : NULL;
    IInspectable *raw = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->node_idx] : NULL;
    const WCHAR *sval;
    struct xbf_stack_val tmp_sv = {0};

    if (!el && !raw) return;
    sval = xbf_aot_str( ec->hdr, op->str_off );
    if (op->is_res_ref && sval && sval[0])
    {
        struct uwp_rd_entry *re = NULL;
        struct uwp_rd_bucket *hint_rd = ec->fake_ctx.root_el ? ec->fake_ctx.root_el->rd_bucket : ec->fake_ctx.target_rd;
        if (!uwp_rd_find_entry( hint_rd, el, sval, 0, &re, NULL ) || !re)
        {
            if (xaml_vec_grow( (void **)&ec->deferred_res_ops, &ec->deferred_res_cap,
                               ec->deferred_res_cnt + 1, sizeof(*ec->deferred_res_ops), 16 ))
                ec->deferred_res_ops[ec->deferred_res_cnt++] = op_idx;
            return;
        }
    }
    tmp_sv.raw = raw;
    xbf_apply_property( &ec->fake_ctx, el, el ? NULL : &tmp_sv, op->pid, op->vt, op->num, sval,
                        op->vec4, op->is_res_ref != 0 );
}

static void aot_exec_add_child( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_op *op )
{
    struct uwp_element *parent_el = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->node_idx] : NULL;
    IInspectable *parent_raw = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->node_idx] : NULL;
    struct uwp_element *child_el = (op->child_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->child_idx] : NULL;
    IInspectable *child_raw = (op->child_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->child_idx] : NULL;
    BOOL handled_custom = FALSE;
    void *ao = ec->app_outer ? ec->app_outer : uwp_get_app_outer();

    if (!parent_el && !parent_raw) return;
    if (parent_el && op->pid == XBF_PROP_MERGED_DICTIONARIES && child_el && child_el->kind == UWP_KIND_RESOURCE_DICT)
    {
        struct uwp_rd_bucket *parent_rd = (parent_el->kind == UWP_KIND_RESOURCE_DICT)
            ? uwp_rd_bucket_for_el( parent_el, TRUE )
            : (ec->target_rd ? ec->target_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
        uwp_rd_link_merged( parent_rd, uwp_rd_bucket_for_el( child_el, TRUE ) );
        return;
    }
    if (!child_raw) return;

    if (parent_el && (op->pid == XBF_PROP_KEYBOARD_ACCELERATORS || (child_el && (child_el->kind == UWP_KIND_ACCELERATOR ||
                                                                  (child_el->class_flags & UWP_CF_ACCELERATOR)))))
    {
        if (!parent_el->accelerators_coll) uwp_vec_create( parent_el, &parent_el->accelerators_coll );
        uwp_vec_Append( parent_el->accelerators_coll, child_raw );
        if (child_el)
        {
            child_el->accel_target = parent_el;
            uwp_accel_register( child_el, parent_el, child_el->accel_key, child_el->accel_mods, child_el->accel_enabled );
        }
        handled_custom = TRUE;
    }
    else if (parent_el && op->pid == XBF_PROP_GRID_ROW_DEFINITIONS)
    {
        if (!parent_el->row_defs_coll) uwp_vec_create( parent_el, &parent_el->row_defs_coll );
        uwp_vec_Append( parent_el->row_defs_coll, child_raw );
        handled_custom = TRUE;
    }
    else if (parent_el && op->pid == XBF_PROP_GRID_COLUMN_DEFINITIONS)
    {
        if (!parent_el->col_defs_coll) uwp_vec_create( parent_el, &parent_el->col_defs_coll );
        uwp_vec_Append( parent_el->col_defs_coll, child_raw );
        handled_custom = TRUE;
    }
    else if (parent_el && (op->pid == XBF_PROP_GRADIENTBRUSH_STOPS ||
                           (child_el && (child_el->class_flags2 & UWP_CF2_GRADIENT_STOP))))
    {
        struct uwp_style_anim_ext *psa = uwp_elem_ensure_style_anim( parent_el );
        if (psa)
        {
            if (!psa->gradient_stops_coll)
            {
                uwp_vec_create( parent_el, &psa->gradient_stops_coll );
                if (psa->gradient_stops_coll)
                {
                    struct uwp_vec_view *gv = CONTAINING_RECORD( psa->gradient_stops_coll, struct uwp_vec_view, IInspectable_iface );
                    gv->runtime_class = L"Windows.UI.Xaml.Media.GradientStopCollection";
                }
            }
            if (psa->gradient_stops_coll) uwp_vec_Append( psa->gradient_stops_coll, child_raw );
        }
        handled_custom = TRUE;
    }
    else if (parent_raw && ao && op->node_idx < ec->hdr->node_cnt &&
             ec->nodes[op->node_idx].tid < ec->hdr->type_cnt &&
             ec->fake_ctx.types[ec->nodes[op->node_idx].tid].full_name[0] &&
             wcsstr( ec->fake_ctx.types[ec->nodes[op->node_idx].tid].full_name, L"BreakpointCollection" ))
    {
        void *provider = NULL;
        const WCHAR *coll_cls = ec->fake_ctx.types[ec->nodes[op->node_idx].tid].full_name;
        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)ao, &IID_IXamlMetadataProvider_UWP, &provider )) && provider)
        {
            struct xaml_type *ct = (struct xaml_type *)uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, coll_cls );
            if (ct && ct->lpVtbl->AddToVector)
            {
                HRESULT ahr = ct->lpVtbl->AddToVector( ct, parent_raw, child_raw );
                handled_custom = SUCCEEDED(ahr);
                TRACE( "AOT AddToVector(%s coll=%p item=%p) -> hr %#lx\n",
                       debugstr_w(coll_cls), parent_raw, child_raw, ahr );
            }
            if (ct) IInspectable_Release( (IInspectable *)ct );
            IInspectable_Release( (IInspectable *)provider );
        }
    }
    else if (!(op->pid & 0x8000) && op->pid < ec->hdr->prop_cnt &&
             ec->fake_ctx.props[op->pid].owner_tid < ec->hdr->type_cnt &&
             ec->fake_ctx.props[op->pid].name[0] &&
             wcscmp( ec->fake_ctx.props[op->pid].name, L"Children" ) &&
             (wcscmp( ec->fake_ctx.props[op->pid].name, L"Items" ) ||
              (parent_el && (parent_el->class_flags & UWP_CF_RADIO_BUTTON))) &&
             wcscmp( ec->fake_ctx.props[op->pid].name, L"MergedDictionaries" ) &&
             wcscmp( ec->fake_ctx.props[op->pid].name, L"RowDefinitions" ) &&
             wcscmp( ec->fake_ctx.props[op->pid].name, L"ColumnDefinitions" ) &&
             wcscmp( ec->fake_ctx.props[op->pid].name, L"KeyboardAccelerators" ))
    {
        const struct xbf_prop_info *pi = &ec->fake_ctx.props[op->pid];
        const WCHAR *owner_cls = ec->fake_ctx.types[pi->owner_tid].full_name;
        IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
        if (owner_cls[0] && target)
            handled_custom = uwp_add_custom_xaml_collection_item( ao, owner_cls, pi->name, target, child_raw );
        if (parent_el && (parent_el->class_flags & UWP_CF_RADIO_BUTTON) && !wcscmp( pi->name, L"Items" ))
            handled_custom = FALSE;
    }

    if (!handled_custom && parent_el &&
        (op->pid == XBF_PROP_TEXTBLOCK_INLINES || parent_el->kind == UWP_KIND_TEXT_BLOCK ||
         (parent_el->class_flags & (UWP_CF_RICH_TEXT_BLOCK | UWP_CF_SPAN | UWP_CF_PARAGRAPH))) &&
        child_el && (child_el->class_flags & (UWP_CF_RUN | UWP_CF_SPAN | UWP_CF_INLINE | UWP_CF_PARAGRAPH | UWP_CF_LINE_BREAK)))
    {
        if (!parent_el->inlines_coll)
        {
            uwp_vec_create( parent_el, &parent_el->inlines_coll );
            if (parent_el->inlines_coll)
            {
                struct uwp_vec_view *iv = CONTAINING_RECORD( parent_el->inlines_coll, struct uwp_vec_view, IInspectable_iface );
                iv->runtime_class = uwp_is_rich_text_block( parent_el )
                    ? L"Windows.UI.Xaml.Documents.BlockCollection"
                    : L"Windows.UI.Xaml.Documents.InlineCollection";
            }
        }
        if (parent_el->inlines_coll) uwp_vec_Append( parent_el->inlines_coll, child_raw );
        child_el->parent = parent_el;
        if (uwp_has_text( child_el )) uwp_elem_append_text( parent_el, uwp_elem_text( child_el ) );
        handled_custom = TRUE;
    }
    if (!handled_custom && parent_el && parent_el->kind == UWP_KIND_TEXT_BLOCK && uwp_has_text( child_el ))
    {
        uwp_elem_append_text( parent_el, uwp_elem_text( child_el ) );
        handled_custom = TRUE;
    }
    if (!handled_custom && parent_el &&
        (op->pid == XBF_PROP_PANEL_CHILDREN || op->pid == XBF_PROP_ITEMS ||
         parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL ||
         parent_el->kind == UWP_KIND_WRAP_PANEL || parent_el->kind == UWP_KIND_FLYOUT ||
         (parent_el->class_flags & (UWP_CF_STORYBOARD | UWP_CF_RADIO_BUTTON))))
    {
        if (!parent_el->children_coll) uwp_vec_create( parent_el, &parent_el->children_coll );
        uwp_vec_Append( parent_el->children_coll, child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    if (child_el && parent_el && !child_el->parent) child_el->parent = parent_el;
    if (child_el && ec->root_el && !child_el->owner_component) child_el->owner_component = ec->root_el;
}

static void aot_exec_set_obj_prop( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_op *op )
{
    struct uwp_element *parent_el = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->node_idx] : NULL;
    IInspectable *parent_raw = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->node_idx] : NULL;
    struct uwp_element *child_el = (op->child_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->child_idx] : NULL;
    IInspectable *child_raw = (op->child_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->child_idx] : NULL;
    BOOL is_dict = (child_el && child_el->kind == UWP_KIND_RESOURCE_DICT);
    struct uwp_rd_bucket *child_rd = child_el ? uwp_rd_bucket_for_el( child_el, FALSE ) : NULL;
    BOOL handled_custom_obj = FALSE, is_tpl_sel = FALSE;

    if (is_dict || (child_rd && (op->pid == XBF_PROP_FRAMEWORKELEMENT_RESOURCES ||
                                 op->pid == XBF_PROP_MERGED_DICTIONARIES ||
                                 op->pid == XBF_PROP_THEME_DICTIONARIES)))
    {
        struct uwp_rd_bucket *dest_b = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
            ? uwp_rd_bucket_for_el( parent_el, TRUE ) : ec->target_rd;
        if (!child_rd && child_el) child_rd = uwp_rd_bucket_for_el( child_el, TRUE );
        if (dest_b && child_rd && dest_b != child_rd)
        {
            if (op->pid == XBF_PROP_THEME_DICTIONARIES)
                uwp_rd_link_theme( dest_b, (child_el && child_el->name[0]) ? child_el->name : L"Default", child_rd );
            else
                uwp_rd_link_merged( dest_b, child_rd );
        }
        return;
    }
    if (!parent_el && !parent_raw) return;

    if (child_raw && !(op->pid & 0x8000) && op->pid < ec->hdr->prop_cnt &&
        ec->fake_ctx.props[op->pid].owner_tid < ec->hdr->type_cnt &&
        ec->fake_ctx.props[op->pid].name[0])
    {
        const struct xbf_prop_info *pi = &ec->fake_ctx.props[op->pid];
        const WCHAR *owner_cls = ec->fake_ctx.types[pi->owner_tid].full_name;
        IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
        if (owner_cls[0] && target &&
            wcscmp( pi->name, L"Style" ) && wcscmp( pi->name, L"Resources" ) &&
            wcscmp( pi->name, L"MergedDictionaries" ) && wcscmp( pi->name, L"ThemeDictionaries" ) &&
            wcscmp( pi->name, L"ContextFlyout" ) && wcscmp( pi->name, L"Background" ) &&
            wcscmp( pi->name, L"Foreground" ) && wcscmp( pi->name, L"BorderBrush" ))
        {
            if (parent_el && child_el && (child_el->class_flags2 & UWP_CF2_BINDING))
            {
                uwp_add_xaml_binding( parent_el, owner_cls, pi->name, child_el );
                handled_custom_obj = TRUE;
            }
            else if (uwp_set_custom_xaml_member_obj( ec->app_outer ? ec->app_outer : uwp_get_app_outer(), owner_cls, pi->name, target, child_raw ))
            {
                if (parent_el && !wcscmp( pi->name, L"Icon" ))
                {
                    struct uwp_nav_ext *pne = uwp_elem_ensure_nav( parent_el );
                    if (pne && pne->nav_icon != child_raw)
                    {
                        if (pne->nav_icon) IInspectable_Release( pne->nav_icon );
                        pne->nav_icon = child_raw;
                        IInspectable_AddRef( child_raw );
                    }
                }
                else if (parent_el && uwp_is_expander( parent_el ) && !wcscmp( pi->name, L"Header" ) &&
                         child_el && child_el != parent_el)
                {
                    if (parent_el->pane != child_raw)
                    {
                        if (parent_el->pane) IInspectable_Release( parent_el->pane );
                        parent_el->pane = child_raw;
                        IInspectable_AddRef( child_raw );
                    }
                    child_el->parent = parent_el;
                    child_el->in_visual_tree = TRUE;
                }
                if (wcscmp( pi->name, L"Content" ) && wcscmp( pi->name, L"Child" ))
                    handled_custom_obj = TRUE;
            }
        }
    }
    if (handled_custom_obj || !parent_el) return;

    if (op->sv_kind == UWP_RD_BRUSH || op->sv_kind == UWP_RD_COLOR)
    {
        UINT32 argb = 0;
        BOOL got = FALSE;
        const WCHAR *sval = xbf_aot_str( ec->hdr, op->str_off );
        if (child_raw && uwp_is_bg_pid( op->pid ))
        {
            IInspectable **slot = (op->pid == XBF_PROP_SHAPE_FILL) ? &parent_el->fill_brush : &parent_el->bg_brush;
            if (*slot != child_raw)
            {
                if (*slot) IInspectable_Release( *slot );
                *slot = child_raw;
                IInspectable_AddRef( child_raw );
            }
        }
        if (op->has_argb) { argb = apply_opacity_to_argb( op->argb, op->num ); got = TRUE; }
        else if (sval[0] && uwp_resolve_brush_or_color( ec->target_rd, parent_el, sval, 0, &argb ))
        {
            argb = apply_opacity_to_argb( argb, op->num );
            got = TRUE;
        }
        else if (child_el && uwp_elem_has_brush_argb( child_el ))
        {
            argb = apply_opacity_to_argb( uwp_elem_brush_argb( child_el ), uwp_elem_brush_opacity( child_el ) );
            got = TRUE;
        }
        if (got)
        {
            if (uwp_is_bg_pid( op->pid )) uwp_el_set_bg_argb( parent_el, argb );
            else if (uwp_is_fg_pid( op->pid )) uwp_el_set_fg_argb( parent_el, argb );
            else if (uwp_is_border_pid( op->pid )) uwp_el_set_border_argb( parent_el, argb );
        }
    }
    else if (op->sv_kind == UWP_RD_STYLE && op->pid == XBF_PROP_STYLE)
    {
        const WCHAR *based_on = xbf_aot_str( ec->hdr, op->str_off );
        UINT32 si;
        if (based_on[0]) uwp_apply_style_by_name( &ec->fake_ctx, parent_el, based_on );
        for (si = 0; si < op->setter_cnt && op->setter_start + si < ec->hdr->style_setter_cnt; si++)
        {
            const struct uwp_style_setter *st = &ec->style_setters[op->setter_start + si];
            xbf_apply_property( &ec->fake_ctx, parent_el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
        }
    }
    else if (op->pid == 0x000c && uwp_has_text( child_el ))
    {
        if (!uwp_has_text( parent_el )) uwp_elem_set_text( parent_el, uwp_elem_text( child_el ) );
    }
    else if (child_raw && (op->pid == XBF_PROP_DATACONTEXT ||
             (!(op->pid & 0x8000) && op->pid < ec->hdr->prop_cnt && !wcscmp( ec->fake_ctx.props[op->pid].name, L"DataContext" ))))
    {
        uwp_fe_put_DataContext( &parent_el->IInspectable_iface, child_raw );
    }
    else if (child_raw && (op->pid == XBF_PROP_CONTEXT_FLYOUT ||
             (op->pid < ec->hdr->prop_cnt && !wcscmp( ec->fake_ctx.props[op->pid].name, L"ContextFlyout" ))))
    {
        if (parent_el->context_flyout) IInspectable_Release( parent_el->context_flyout );
        parent_el->context_flyout = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    else if (child_raw && (op->pid == XBF_PROP_BUTTON_FLYOUT ||
             (op->pid < ec->hdr->prop_cnt && !wcscmp( ec->fake_ctx.props[op->pid].name, L"Flyout" )) ||
             (child_el && child_el->kind == UWP_KIND_FLYOUT)))
    {
        if (parent_el->flyout) IInspectable_Release( parent_el->flyout );
        parent_el->flyout = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    else if (child_raw && op->sv_kind == UWP_RD_OBJ && op->pid < ec->hdr->prop_cnt &&
             !wcscmp( ec->fake_ctx.props[op->pid].name, L"Pane" ))
    {
        if (parent_el->pane) IInspectable_Release( parent_el->pane );
        parent_el->pane = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    else if (child_raw && op->sv_kind == UWP_RD_OBJ &&
             (op->pid == XBF_PROP_IMAGE_SOURCE ||
              (op->pid < ec->hdr->prop_cnt &&
               (!wcscmp( ec->fake_ctx.props[op->pid].name, L"Source" ) ||
                !wcscmp( ec->fake_ctx.props[op->pid].name, L"ImageSource" )))))
    {
        struct uwp_media_ext *pm = uwp_elem_ensure_media( parent_el );
        if (pm && pm->img_source != child_raw)
        {
            if (pm->img_source) IInspectable_Release( pm->img_source );
            pm->img_source = child_raw;
            IInspectable_AddRef( child_raw );
        }
        if (pm && uwp_elem_image_src( child_el )[0] && !pm->image_src[0])
            lstrcpynW( pm->image_src, uwp_elem_image_src( child_el ), ARRAY_SIZE(pm->image_src) );
        if (child_el) child_el->parent = parent_el;
    }
    else if (child_raw && (xbf_prop_is_item_template( op->pid, &ec->fake_ctx, &is_tpl_sel ) ||
             (child_el && (child_el->class_flags & UWP_CF_DATA_TEMPLATE) &&
              op->pid != XBF_PROP_CONTENT && op->pid != XBF_PROP_USERCONTROL_CONTENT)))
    {
        IInspectable **slot = is_tpl_sel ? &parent_el->item_template_selector : &parent_el->item_template;
        if (*slot) IInspectable_Release( *slot );
        *slot = child_raw;
        IInspectable_AddRef( child_raw );
        if (parent_el->items_source && !uwp_is_combobox( parent_el ))
            uwp_items_ctrl_materialize_source( parent_el, parent_el->items_source );
    }
    else if (child_raw && op->sv_kind == UWP_RD_OBJ &&
             (!child_el || (child_el->kind != UWP_KIND_STYLE && child_el->kind != UWP_KIND_BRUSH &&
                            child_el->kind != UWP_KIND_COLOR && child_el->kind != UWP_KIND_FLYOUT &&
                            !(child_el->class_flags & UWP_CF_DATA_TEMPLATE))) &&
             (op->pid == XBF_PROP_CONTENT || op->pid == XBF_PROP_USERCONTROL_CONTENT ||
              op->pid == XBF_PROP_BORDER_CHILD || op->pid == XBF_PROP_RENDER_TRANSFORM_ORIGIN ||
              op->pid == XBF_PROP_POPUP_CHILD ||
              (!parent_el->content && op->pid != XBF_PROP_UIELEMENT_RENDER_TRANSFORM &&
               op->pid != XBF_PROP_FRAMEWORKELEMENT_RESOURCES && op->pid != XBF_PROP_AUTOMATION_NAME && op->pid != 0x000c)))
    {
        if (parent_el->content) IInspectable_Release( parent_el->content );
        parent_el->content = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
}

static void aot_exec_provide_value( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_op *op )
{
    struct uwp_element *target_el = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->node_idx] : NULL;
    struct uwp_element *val_el = (op->child_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->child_idx] : NULL;
    WCHAR resolved[256] = {0};

    if (!target_el || !val_el) return;
    if (val_el->class_flags2 & UWP_CF2_NULL_EXT)
    {
        if (uwp_is_fg_pid( op->pid ))
        {
            target_el->has_fg = TRUE;
            target_el->fg_argb = 0;
            target_el->fg_alpha = 0;
        }
        return;
    }
    if (!wcscmp( val_el->class_name, L"Windows.UI.Xaml.Data.Binding" ))
    {
        const WCHAR *dp_name = NULL, *owner_cls = NULL;
        switch (op->pid)
        {
        case XBF_PROP_ITEMS_CONTROL_ITEMS_SOURCE: dp_name = L"ItemsSource"; break;
        case XBF_PROP_CONTENT: dp_name = L"Content"; break;
        case XBF_PROP_TEXTBLOCK_TEXT:
        case XBF_PROP_RUN_TEXT: dp_name = L"Text"; break;
        case XBF_PROP_UIELEMENT_VISIBILITY:
        case XBF_PROP_UIELEMENT_VISIBILITY_V2: dp_name = L"Visibility"; break;
        case XBF_PROP_TOGGLE_BUTTON_IS_CHECKED:
        case XBF_PROP_TOGGLE_MENU_FLYOUT_ITEM_IS_CHECKED: dp_name = L"IsChecked"; break;
        case XBF_PROP_ACCELERATOR_SCOPE_OWNER: dp_name = L"IsEnabled"; break;
        default:
            if (op->pid < ec->hdr->prop_cnt && ec->fake_ctx.props[op->pid].name[0])
                dp_name = ec->fake_ctx.props[op->pid].name;
            break;
        }
        if (!dp_name || !dp_name[0]) return;
        if (op->pid < ec->hdr->prop_cnt && ec->fake_ctx.props[op->pid].owner_tid < ec->hdr->type_cnt)
            owner_cls = ec->fake_ctx.types[ec->fake_ctx.props[op->pid].owner_tid].full_name;
        if (owner_cls && owner_cls[0] && wcsncmp( owner_cls, L"Windows.UI.Xaml.", 16 ))
            uwp_add_xaml_binding( target_el, owner_cls, dp_name, val_el );
        else
        {
            IDependencyProperty *bdp = NULL;
            if (SUCCEEDED(dependency_property_create( NULL, &bdp )) && bdp)
            {
                xaml_dp_set_name( bdp, dp_name );
                uwp_apply_binding( target_el, bdp, val_el );
                IDependencyProperty_Release( bdp );
            }
        }
        return;
    }
    {
        const WCHAR *res_key = uwp_has_text( val_el ) ? uwp_elem_text( val_el ) : val_el->name;
        if (!res_key || !res_key[0]) return;
        if (val_el->class_flags2 & UWP_CF2_TEMPLATE_BINDING)
        {
            struct uwp_style_anim_ext *tsa = uwp_elem_ensure_style_anim( target_el );
            if (tsa)
            {
                tsa->tpl_bind_target_pid = op->pid;
                lstrcpynW( tsa->tpl_bind_src_prop, res_key, ARRAY_SIZE(tsa->tpl_bind_src_prop) );
            }
            return;
        }
        if (uwp_pri_lookup_string( res_key, resolved, ARRAY_SIZE(resolved) ) && resolved[0])
        {
            const WCHAR *pname = (op->pid < ec->hdr->prop_cnt) ? ec->fake_ctx.props[op->pid].name : L"";
            BOOL is_text_prop = (!wcscmp( pname, L"Text" ) || !wcscmp( pname, L"Content" ) ||
                                 !wcscmp( pname, L"Glyph" ) || !wcscmp( pname, L"Label" ) ||
                                 !wcscmp( pname, L"Header" ) || !wcscmp( pname, L"Title" ) ||
                                 !wcscmp( pname, L"Description" ) ||
                                 !wcscmp( pname, L"PlaceholderText" ));
            if (op->pid == XBF_PROP_AUTOMATION_NAME)
                uwp_elem_set_automation_name( target_el, resolved );
            else if (uwp_is_calendar_date_picker( target_el ) && !wcscmp( pname, L"Header" ))
            {
                struct uwp_nav_ext *ne = uwp_elem_ensure_nav( target_el );
                if (ne) lstrcpynW( ne->cdp_header, resolved, ARRAY_SIZE(ne->cdp_header) );
            }
            else if (uwp_is_calendar_date_picker( target_el ) && !wcscmp( pname, L"PlaceholderText" ))
            {
                struct uwp_nav_ext *ne = uwp_elem_ensure_nav( target_el );
                if (ne) lstrcpynW( ne->cdp_placeholder, resolved, ARRAY_SIZE(ne->cdp_placeholder) );
                if (!ne || !ne->cdp_has_date) uwp_elem_set_text( target_el, resolved );
            }
            else if (op->pid == XBF_PROP_CONTENT || op->pid == XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV ||
                     op->pid == XBF_PROP_TEXTBLOCK_TEXT || op->pid == XBF_PROP_UIELEMENT_EXIT_DISPLAY_MODE ||
                     op->pid == XBF_PROP_FONTICON_GLYPH || op->pid == XBF_PROP_RUN_TEXT ||
                     (op->pid != XBF_PROP_AUTOMATION_ID && is_text_prop && !uwp_has_text( target_el )))
                uwp_elem_set_text( target_el, resolved );

            if (pname[0] && op->pid < ec->hdr->prop_cnt && ec->fake_ctx.props[op->pid].owner_tid < ec->hdr->type_cnt)
            {
                const WCHAR *owner_cls = ec->fake_ctx.types[ec->fake_ctx.props[op->pid].owner_tid].full_name;
                if (owner_cls[0] && wcsncmp( owner_cls, L"Windows.UI.Xaml.", 16 ))
                {
                    IInspectable *target = (op->node_idx < ec->hdr->node_cnt && ec->nodes_raw[op->node_idx])
                        ? ec->nodes_raw[op->node_idx]
                        : (target_el->outer ? target_el->outer : &target_el->IInspectable_iface);
                    uwp_set_custom_xaml_member( ec->app_outer ? ec->app_outer : uwp_get_app_outer(),
                                                owner_cls, pname, target, resolved );
                }
            }
        }
    }
}

static void aot_exec_rd_store( struct xbf_aot_exec_ctx *ec, const struct xbf_aot_op *op )
{
    struct uwp_element *parent_el = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->node_idx] : NULL;
    struct uwp_element *child_el = (op->child_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->child_idx] : NULL;
    IInspectable *child_raw = (op->child_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->child_idx] : NULL;
    const WCHAR *key = xbf_aot_str( ec->hdr, op->str_off );
    struct uwp_rd_bucket *dest_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
        ? uwp_rd_bucket_for_el( parent_el, TRUE )
        : (ec->target_rd ? ec->target_rd : (parent_el ? uwp_rd_bucket_for_el( parent_el, TRUE ) : NULL));
    struct xbf_stack_val sv = {0};

    if (!dest_rd && ec->root_el) dest_rd = uwp_rd_bucket_for_el( ec->root_el, TRUE );
    if (!dest_rd || !key[0]) return;

    sv.kind = (enum uwp_rd_kind)op->sv_kind;
    sv.tid = op->tid;
    sv.el = child_el;
    sv.raw = child_raw;
    sv.argb = op->argb;
    sv.has_argb = op->has_argb != 0;
    sv.opacity = (sv.kind == UWP_RD_BRUSH || sv.kind == UWP_RD_COLOR) ? op->num : 1.0;
    sv.dval = op->num;
    memcpy( sv.vec4, op->vec4, sizeof(sv.vec4) );
    if (sv.kind == UWP_RD_STYLE)
        lstrcpynW( sv.based_on, xbf_aot_str( ec->hdr, op->str2_off ), ARRAY_SIZE(sv.based_on) );
    else
        lstrcpynW( sv.str_val, xbf_aot_str( ec->hdr, op->str2_off ), ARRAY_SIZE(sv.str_val) );
    if (op->setter_cnt > 0 && op->setter_start + op->setter_cnt <= ec->hdr->style_setter_cnt)
    {
        sv.setters = (struct uwp_style_setter *)&ec->style_setters[op->setter_start];
        sv.setter_cnt = (int)op->setter_cnt;
    }
    if (child_el && child_el->template_xbf_data && child_el->template_xbf_size)
    {
        sv.template_xbf_data = child_el->template_xbf_data;
        sv.template_xbf_size = child_el->template_xbf_size;
        sv.template_sub_idx = child_el->template_sub_idx;
        sv.template_owner_comp = child_el->template_owner_comp;
    }
    else if (op->cid >= 0 && ec->xbf && ec->xbf_sz)
    {
        sv.template_xbf_data = ec->xbf;
        sv.template_xbf_size = ec->xbf_sz;
        sv.template_sub_idx = (DWORD)op->cid;
        sv.template_owner_comp = ec->component;
    }
    uwp_rd_store_stack_val( dest_rd, key, &sv );
    if (ec->root_el && (!parent_el || !parent_el->name[0] ||
                        (wcsicmp( parent_el->name, L"HighContrast" ) &&
                         wcsicmp( parent_el->name, L"Light" ))))
    {
        struct uwp_rd_bucket *root_rd = uwp_rd_bucket_for_el( ec->root_el, TRUE );
        if (root_rd && root_rd != dest_rd && root_rd != ec->target_rd)
            uwp_rd_store_stack_val( root_rd, key, &sv );
    }
}

static void aot_finalize_deferred_and_triggers( struct xbf_aot_exec_ctx *ec )
{
    const struct xbf_aot_defer *defers = (const struct xbf_aot_defer *)((const BYTE *)ec->hdr + ec->hdr->off_defers);
    int d;
    UINT32 i;

    for (d = 0; d < ec->deferred_res_cnt; d++)
    {
        const struct xbf_aot_op *op = &ec->ops[ec->deferred_res_ops[d]];
        struct uwp_element *el = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_el[op->node_idx] : NULL;
        IInspectable *raw = (op->node_idx < ec->hdr->node_cnt) ? ec->nodes_raw[op->node_idx] : NULL;
        if (el || raw)
        {
            const WCHAR *sval = xbf_aot_str( ec->hdr, op->str_off );
            struct xbf_stack_val tmp_sv = {0};
            tmp_sv.raw = raw;
            xbf_apply_property( &ec->fake_ctx, el, el ? NULL : &tmp_sv, op->pid, op->vt, op->num, sval,
                                op->vec4, op->is_res_ref != 0 );
        }
    }

    if (ec->nodes_el && ec->hdr->node_cnt > 0)
    {
        for (i = 0; i < ec->hdr->node_cnt; i++)
        {
            struct uwp_element *trig = ec->nodes_el[i], *p;
            if (!trig || !trig->state_trigger_active) continue;
            for (p = trig->parent; p; p = p->parent)
            {
                if ((p->class_flags2 & UWP_CF2_VISUAL_STATE) && p->name[0])
                {
                    struct uwp_element *scope = p->owner_component ? p->owner_component : ec->root_el;
                    if (scope) uwp_vsm_goto_state_on_tree( scope, p->name );
                    break;
                }
            }
        }
    }

    if (ec->root_el && ec->hdr->defer_cnt > 0)
    {
        for (i = 0; i < ec->hdr->defer_cnt; i++)
        {
            const struct xbf_aot_defer *ad = &defers[i];
            if (xaml_vec_grow( (void **)&ec->root_el->defer_entries, &ec->root_el->defer_cap,
                               ec->root_el->defer_cnt + 1, sizeof(*ec->root_el->defer_entries), 16 ))
            {
                struct uwp_defer_entry *de = &ec->root_el->defer_entries[ec->root_el->defer_cnt++];
                lstrcpynW( de->name, xbf_aot_str( ec->hdr, ad->name_off ), ARRAY_SIZE(de->name) );
                de->sub_idx = ad->sub_idx;
                de->parent_panel = (ad->parent_panel_idx < ec->hdr->node_cnt && ec->nodes_el && ec->nodes_el[ad->parent_panel_idx])
                    ? ec->nodes_el[ad->parent_panel_idx] : ec->root_el;
                de->xbf_data = ec->xbf;
                de->xbf_size = ec->xbf_sz;
                de->root_component = ec->component;
                de->realized = FALSE;
            }
        }
    }
}

void xbf_aot_instantiate( const struct xbf_aot_header *hdr, const BYTE *xbf, DWORD xbf_sz,
                          IInspectable *component, void *connector, BOOL is_uwp_conn, void *app_outer,
                          struct uwp_element *root_el, struct uwp_element **out_created,
                          const char *base_xbf_dir, struct uwp_rd_bucket *target_rd, BOOL is_template )
{
    const struct xbf_aot_ref *refs = (const struct xbf_aot_ref *)((const BYTE *)hdr + hdr->off_refs);
    const UINT32 *res_uris = (const UINT32 *)((const BYTE *)hdr + hdr->off_res_uris);
    struct xbf_aot_exec_ctx ec = {0};
    UINT32 i;

    if (out_created) *out_created = NULL;
    if (root_el) root_el->xbf_parsed = TRUE;

    ec.hdr = hdr;
    ec.nodes = (const struct xbf_aot_node *)((const BYTE *)hdr + hdr->off_nodes);
    ec.ops = (const struct xbf_aot_op *)((const BYTE *)hdr + hdr->off_ops);
    ec.style_setters = (const struct uwp_style_setter *)((const BYTE *)hdr + hdr->off_style_setters);
    ec.xbf = xbf;
    ec.xbf_sz = xbf_sz;
    ec.component = component;
    ec.connector = connector;
    ec.app_outer = app_outer;
    ec.root_el = root_el;
    ec.out_created = out_created;
    ec.base_xbf_dir = base_xbf_dir;
    ec.target_rd = target_rd;
    ec.is_template = is_template;
    ec.fake_ctx.xbf = xbf;
    ec.fake_ctx.xbf_sz = xbf_sz;
    ec.fake_ctx.types = hdr->type_cnt ? (struct xbf_type_info *)((const BYTE *)hdr + hdr->off_types) : NULL;
    ec.fake_ctx.type_cnt = hdr->type_cnt;
    ec.fake_ctx.props = hdr->prop_cnt ? (struct xbf_prop_info *)((const BYTE *)hdr + hdr->off_props) : NULL;
    ec.fake_ctx.prop_cnt = hdr->prop_cnt;
    ec.fake_ctx.app_outer = app_outer;
    ec.fake_ctx.connector = connector;
    ec.fake_ctx.is_uwp_conn = is_uwp_conn;
    ec.fake_ctx.root_el = root_el;
    ec.fake_ctx.root_comp = component;
    ec.fake_ctx.target_rd = target_rd;

    if (root_el)
    {
        for (i = 0; i < hdr->ref_cnt; i++)
            uwp_record_xbf_ref( root_el, xbf_aot_str( hdr, refs[i].full_name_off ),
                                xbf_aot_str( hdr, refs[i].short_name_off ), refs[i].is_resource_path != 0 );
        for (i = 0; i < hdr->res_uri_cnt; i++)
            uwp_load_res_xbf_into_bucket( NULL, xbf_aot_str( hdr, res_uris[i] ),
                                          target_rd ? target_rd : uwp_rd_bucket_for_el( root_el, TRUE ) );
    }

    if (!is_uwp_conn && connector && !target_rd && hdr->node_cnt > 0 && (ec.nodes[0].flags & XBF_AOT_NODE_IS_RD))
    {
        TRACE( "xbf_aot_instantiate: skipping WinUI 3 ResourceDictionary root\n" );
        return;
    }

    if (hdr->node_cnt > 0)
    {
        ec.nodes_el = calloc( hdr->node_cnt, sizeof(*ec.nodes_el) );
        ec.nodes_raw = calloc( hdr->node_cnt, sizeof(*ec.nodes_raw) );
        if (!ec.nodes_el || !ec.nodes_raw) { free( ec.nodes_el ); free( ec.nodes_raw ); return; }
        if (root_el || (component && (ec.nodes[0].flags & XBF_AOT_NODE_ROOT_REUSE)))
        {
            ec.nodes_el[0] = root_el;
            ec.nodes_raw[0] = component ? component : (root_el->outer ? root_el->outer : &root_el->IInspectable_iface);
        }
    }

    aot_replay_vsm_groups( hdr, root_el );

    for (i = 0; i < hdr->op_cnt; i++)
    {
        const struct xbf_aot_op *op = &ec.ops[i];
        switch (op->kind)
        {
        case XBF_AOT_OP_CREATE_NODE: aot_exec_create_node( &ec, op ); break;
        case XBF_AOT_OP_SET_XCLASS:
            if (op->node_idx < hdr->node_cnt && ec.nodes_el[op->node_idx])
                uwp_elem_set_x_class( ec.nodes_el[op->node_idx], xbf_aot_str( hdr, op->str_off ) );
            break;
        case XBF_AOT_OP_SET_XNAME:
            if (op->node_idx < hdr->node_cnt && ec.nodes_el[op->node_idx])
            {
                struct uwp_element *el = ec.nodes_el[op->node_idx];
                const WCHAR *name = xbf_aot_str( hdr, op->str_off );
                if (name[0])
                {
                    lstrcpynW( el->name, name, ARRAY_SIZE(el->name) );
                    if (root_el && xaml_vec_grow( (void **)&root_el->named_elems, &root_el->named_cap,
                                                  root_el->named_cnt + 1, sizeof(*root_el->named_elems), 32 ))
                    {
                        root_el->named_elems[root_el->named_cnt++] = el;
                        el->in_visual_tree = TRUE;
                    }
                }
            }
            break;
        case XBF_AOT_OP_CONNECT: aot_exec_connect( &ec, op ); break;
        case XBF_AOT_OP_SET_PROP: aot_exec_set_prop( &ec, op, i ); break;
        case XBF_AOT_OP_ADD_CHILD: aot_exec_add_child( &ec, op ); break;
        case XBF_AOT_OP_SET_OBJ_PROP: aot_exec_set_obj_prop( &ec, op ); break;
        case XBF_AOT_OP_PROVIDE_VALUE: aot_exec_provide_value( &ec, op ); break;
        case XBF_AOT_OP_TEMPLATE_BINDING:
            if (op->node_idx < hdr->node_cnt && ec.nodes_el[op->node_idx])
            {
                struct uwp_element *target_el = ec.nodes_el[op->node_idx];
                const WCHAR *src_name = xbf_aot_str( hdr, op->str_off );
                if (src_name && src_name[0])
                {
                    struct uwp_style_anim_ext *tsa = uwp_elem_ensure_style_anim( target_el );
                    if (tsa) { tsa->tpl_bind_target_pid = op->pid; lstrcpynW( tsa->tpl_bind_src_prop, src_name, ARRAY_SIZE(tsa->tpl_bind_src_prop) ); }
                    if (op->pid == XBF_PROP_FRAMEWORKELEMENT_HEIGHT || op->pid == XBF_PROP_FLOW_DIRECTION)
                        target_el->height = 0.1;
                }
            }
            break;
        case XBF_AOT_OP_RD_LINK_THEME:
            if (op->child_idx < hdr->node_cnt && ec.nodes_el[op->child_idx] &&
                ec.nodes_el[op->child_idx]->kind == UWP_KIND_RESOURCE_DICT)
            {
                struct uwp_element *parent_el = (op->node_idx < hdr->node_cnt) ? ec.nodes_el[op->node_idx] : NULL;
                struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                    ? uwp_rd_bucket_for_el( parent_el, TRUE )
                    : (target_rd ? target_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                uwp_rd_link_theme( parent_rd, xbf_aot_str( hdr, op->str_off ), uwp_rd_bucket_for_el( ec.nodes_el[op->child_idx], TRUE ) );
            }
            break;
        case XBF_AOT_OP_RD_STORE: aot_exec_rd_store( &ec, op ); break;
        }
    }

    aot_finalize_deferred_and_triggers( &ec );

    if (ec.root_binding) IUnknown_Release( (IUnknown *)ec.root_binding );
    if (ec.dt_binding) IUnknown_Release( (IUnknown *)ec.dt_binding );
    free( ec.deferred_res_ops );
    free( ec.nodes_el );
    free( ec.nodes_raw );
}

