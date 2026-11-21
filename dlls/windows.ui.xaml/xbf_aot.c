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

    for (i = 0; i < xbf_sz; i++)
    {
        h ^= xbf[i];
        h *= 1099511628211ULL;
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

void xbf_aot_snapshot_vsm( struct xbf_aot_builder *b, struct uwp_element *root_el, int start_group_idx )
{
    int g, s, t;
    if (!b || b->aborted || !root_el || start_group_idx >= root_el->vsm_group_cnt) return;
    for (g = start_group_idx; g < root_el->vsm_group_cnt; g++)
    {
        const struct uwp_vsm_group *src_g = &root_el->vsm_groups[g];
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

static BOOL xbf_aot_validate_tpl( const struct xbf_aot_header *hdr, DWORD buf_sz,
                                  UINT64 hash, DWORD xbf_sz, int only_sub_idx )
{
    if (!hdr || buf_sz < sizeof(*hdr)) return FALSE;
    if (hdr->magic != XBF_AOT_MAGIC || hdr->version != XBF_AOT_VERSION) return FALSE;
    if (hdr->hash != hash || hdr->xbf_sz != xbf_sz || hdr->only_sub_idx != only_sub_idx) return FALSE;
    if (hdr->total_bytes != buf_sz) return FALSE;
    if (hdr->off_types > buf_sz || hdr->type_cnt > (buf_sz - hdr->off_types) / sizeof(struct xbf_type_info)) return FALSE;
    if (hdr->off_props > buf_sz || hdr->prop_cnt > (buf_sz - hdr->off_props) / sizeof(struct xbf_prop_info)) return FALSE;
    if (hdr->off_refs > buf_sz || hdr->ref_cnt > (buf_sz - hdr->off_refs) / sizeof(struct xbf_aot_ref)) return FALSE;
    if (hdr->off_res_uris > buf_sz || hdr->res_uri_cnt > (buf_sz - hdr->off_res_uris) / sizeof(UINT32)) return FALSE;
    if (hdr->off_nodes > buf_sz || hdr->node_cnt > (buf_sz - hdr->off_nodes) / sizeof(struct xbf_aot_node)) return FALSE;
    if (hdr->off_ops > buf_sz || hdr->op_cnt > (buf_sz - hdr->off_ops) / sizeof(struct xbf_aot_op)) return FALSE;
    if (hdr->off_style_setters > buf_sz || hdr->style_setter_cnt > (buf_sz - hdr->off_style_setters) / sizeof(struct uwp_style_setter)) return FALSE;
    if (hdr->off_vsm_groups > buf_sz || hdr->vsm_group_cnt > (buf_sz - hdr->off_vsm_groups) / sizeof(struct xbf_aot_vsm_group)) return FALSE;
    if (hdr->off_vsm_states > buf_sz || hdr->vsm_state_cnt > (buf_sz - hdr->off_vsm_states) / sizeof(struct xbf_aot_vsm_state)) return FALSE;
    if (hdr->off_vsm_setters > buf_sz || hdr->vsm_setter_cnt > (buf_sz - hdr->off_vsm_setters) / sizeof(struct xbf_aot_vsm_setter)) return FALSE;
    if (hdr->off_defers > buf_sz || hdr->defer_cnt > (buf_sz - hdr->off_defers) / sizeof(struct xbf_aot_defer)) return FALSE;
    if (hdr->off_str_pool > buf_sz || hdr->str_pool_bytes > buf_sz - hdr->off_str_pool || hdr->str_pool_bytes < 2) return FALSE;
    return TRUE;
}

static const WCHAR *xbf_aot_str( const struct xbf_aot_header *hdr, UINT32 off )
{
    const BYTE *pool;
    if (!hdr || off + 1 >= hdr->str_pool_bytes) return L"";
    pool = (const BYTE *)hdr + hdr->off_str_pool;
    return (const WCHAR *)(pool + off);
}

static struct xbf_aot_header *xbf_aot_load_from_disk( UINT64 hash, DWORD xbf_sz, int only_sub_idx )
{
    WCHAR path[MAX_PATH];
    HANDLE hf;
    LARGE_INTEGER fsz;
    BYTE *buf;
    DWORD rd = 0;

    swprintf( path, ARRAY_SIZE(path), L"C:\\windows\\temp\\xaml_aot_%016I64x.bin", hash );
    hf = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (hf == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx( hf, &fsz ) || fsz.QuadPart < (LONGLONG)sizeof(struct xbf_aot_header) || fsz.QuadPart > 16 * 1024 * 1024)
    {
        CloseHandle( hf );
        return NULL;
    }
    buf = malloc( (size_t)fsz.QuadPart );
    if (!buf)
    {
        CloseHandle( hf );
        return NULL;
    }
    if (!ReadFile( hf, buf, (DWORD)fsz.QuadPart, &rd, NULL ) || rd != (DWORD)fsz.QuadPart)
    {
        free( buf );
        CloseHandle( hf );
        return NULL;
    }
    CloseHandle( hf );
    if (!xbf_aot_validate_tpl( (const struct xbf_aot_header *)buf, rd, hash, xbf_sz, only_sub_idx ))
    {
        free( buf );
        return NULL;
    }
    return (struct xbf_aot_header *)buf;
}

void xbf_aot_save_to_disk( const struct xbf_aot_header *hdr )
{
    WCHAR path[MAX_PATH], tmp_path[MAX_PATH];
    HANDLE hf;
    DWORD wr = 0;

    if (!hdr) return;
    CreateDirectoryW( L"C:\\windows\\temp", NULL );
    swprintf( path, ARRAY_SIZE(path), L"C:\\windows\\temp\\xaml_aot_%016I64x.bin", hdr->hash );
    swprintf( tmp_path, ARRAY_SIZE(tmp_path), L"C:\\windows\\temp\\xaml_aot_%016I64x.%lu.tmp",
              hdr->hash, (unsigned long)GetCurrentProcessId() );
    hf = CreateFileW( tmp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (hf == INVALID_HANDLE_VALUE) return;
    if (WriteFile( hf, hdr, hdr->total_bytes, &wr, NULL ) && wr == hdr->total_bytes)
    {
        CloseHandle( hf );
        if (!MoveFileExW( tmp_path, path, MOVEFILE_REPLACE_EXISTING ))
            DeleteFileW( tmp_path );
    }
    else
    {
        CloseHandle( hf );
        DeleteFileW( tmp_path );
    }
}

struct xbf_aot_cache_slot *xbf_aot_cache_acquire( UINT64 hash, DWORD xbf_sz, int only_sub_idx, BOOL *from_disk )
{
    int i;
    struct xbf_aot_header *disk_tpl;

    *from_disk = FALSE;
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

    disk_tpl = xbf_aot_load_from_disk( hash, xbf_sz, only_sub_idx );
    if (!disk_tpl) return NULL;

    *from_disk = TRUE;
    AcquireSRWLockExclusive( &g_xbf_aot_lock );
    for (i = 0; i < XBF_AOT_CACHE_SLOTS; i++)
    {
        if (g_xbf_aot_cache[i].tpl && g_xbf_aot_cache[i].hash == hash)
        {
            InterlockedIncrement( &g_xbf_aot_cache[i].ref );
            g_xbf_aot_cache[i].last_used = ++g_xbf_aot_lru;
            ReleaseSRWLockExclusive( &g_xbf_aot_lock );
            free( disk_tpl );
            return &g_xbf_aot_cache[i];
        }
    }
    {
        int victim = -1;
        UINT64 best_lru = (~0ULL);
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
            g_xbf_aot_cache[victim].hash = hash;
            g_xbf_aot_cache[victim].tpl = disk_tpl;
            g_xbf_aot_cache[victim].last_used = ++g_xbf_aot_lru;
            g_xbf_aot_cache[victim].ref = 1;
            ReleaseSRWLockExclusive( &g_xbf_aot_lock );
            return &g_xbf_aot_cache[victim];
        }
    }
    ReleaseSRWLockExclusive( &g_xbf_aot_lock );
    free( disk_tpl );
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

void xbf_aot_instantiate( const struct xbf_aot_header *hdr, const BYTE *xbf, DWORD xbf_sz,
                                 IInspectable *component, void *connector, BOOL is_uwp_conn, void *app_outer,
                                 struct uwp_element *root_el, struct uwp_element **out_created,
                                 struct uwp_rd_bucket *target_rd, BOOL is_template )
{
    const struct xbf_aot_ref *refs = (const struct xbf_aot_ref *)((const BYTE *)hdr + hdr->off_refs);
    const UINT32 *res_uris = (const UINT32 *)((const BYTE *)hdr + hdr->off_res_uris);
    const struct xbf_aot_node *nodes = (const struct xbf_aot_node *)((const BYTE *)hdr + hdr->off_nodes);
    const struct xbf_aot_op *ops = (const struct xbf_aot_op *)((const BYTE *)hdr + hdr->off_ops);
    const struct uwp_style_setter *style_setters = (const struct uwp_style_setter *)((const BYTE *)hdr + hdr->off_style_setters);
    const struct xbf_aot_vsm_group *vsm_groups = (const struct xbf_aot_vsm_group *)((const BYTE *)hdr + hdr->off_vsm_groups);
    const struct xbf_aot_vsm_state *vsm_states = (const struct xbf_aot_vsm_state *)((const BYTE *)hdr + hdr->off_vsm_states);
    const struct xbf_aot_vsm_setter *vsm_setters = (const struct xbf_aot_vsm_setter *)((const BYTE *)hdr + hdr->off_vsm_setters);
    const struct xbf_aot_defer *defers = (const struct xbf_aot_defer *)((const BYTE *)hdr + hdr->off_defers);

    struct xbf_ctx fake_ctx = {0};
    struct uwp_element **nodes_el = NULL;
    IInspectable **nodes_raw = NULL;
    void *dt_binding = NULL;
    UINT32 i;

    if (out_created) *out_created = NULL;
    if (root_el) root_el->xbf_parsed = TRUE;

    fake_ctx.xbf = xbf;
    fake_ctx.xbf_sz = xbf_sz;
    fake_ctx.types = hdr->type_cnt ? (struct xbf_type_info *)((const BYTE *)hdr + hdr->off_types) : NULL;
    fake_ctx.type_cnt = hdr->type_cnt;
    fake_ctx.props = hdr->prop_cnt ? (struct xbf_prop_info *)((const BYTE *)hdr + hdr->off_props) : NULL;
    fake_ctx.prop_cnt = hdr->prop_cnt;
    fake_ctx.app_outer = app_outer;
    fake_ctx.connector = connector;
    fake_ctx.is_uwp_conn = is_uwp_conn;
    fake_ctx.root_el = root_el;
    fake_ctx.root_comp = component;
    fake_ctx.target_rd = target_rd;

    /* 1. Replay recorded XBF refs & resource dictionaries */
    if (root_el)
    {
        for (i = 0; i < hdr->ref_cnt; i++)
            uwp_record_xbf_ref( root_el, xbf_aot_str( hdr, refs[i].full_name_off ),
                                xbf_aot_str( hdr, refs[i].short_name_off ), refs[i].is_resource_path != 0 );
        for (i = 0; i < hdr->res_uri_cnt; i++)
            uwp_load_res_xbf_into_bucket( NULL, xbf_aot_str( hdr, res_uris[i] ),
                                          target_rd ? target_rd : uwp_rd_bucket_for_el( root_el, TRUE ) );
    }

    if (hdr->node_cnt > 0)
    {
        nodes_el = calloc( hdr->node_cnt, sizeof(*nodes_el) );
        nodes_raw = calloc( hdr->node_cnt, sizeof(*nodes_raw) );
        if (!nodes_el || !nodes_raw)
        {
            free( nodes_el );
            free( nodes_raw );
            return;
        }
        if (root_el)
        {
            nodes_el[0] = root_el;
            nodes_raw[0] = component ? component : (root_el->outer ? root_el->outer : &root_el->IInspectable_iface);
        }
    }

    /* 2. Replay scene-graph IR operations */
    for (i = 0; i < hdr->op_cnt; i++)
    {
        const struct xbf_aot_op *op = &ops[i];
        switch (op->kind)
        {
        case XBF_AOT_OP_CREATE_NODE:
        {
            const struct xbf_aot_node *nd;
            const WCHAR *cls_name, *short_name;
            struct uwp_element *el = NULL;
            IInspectable *inst = NULL;

            if (op->node_idx >= hdr->node_cnt) break;
            nd = &nodes[op->node_idx];
            cls_name = xbf_aot_str( hdr, nd->class_name_off );
            short_name = xbf_aot_str( hdr, nd->short_name_off );

            if (nd->flags & XBF_AOT_NODE_ROOT_REUSE)
            {
                el = root_el;
                inst = component;
                if ((nd->flags & XBF_AOT_NODE_IS_RD) && target_rd && el)
                {
                    el->rd_bucket = target_rd;
                    target_rd->owner_el = el;
                }
            }
            else if (nd->flags & XBF_AOT_NODE_LIGHTWEIGHT)
            {
                el = NULL;
                inst = NULL;
            }
            else if (nd->flags & XBF_AOT_NODE_BUILTIN)
            {
                uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                el = uwp_elem_from_any( inst );
                if (el && nd->tid == XBF_TYPE_DEFERRED_ELEMENT) el->kind = UWP_KIND_DEFERRED;
            }
            else if (nd->flags & XBF_AOT_NODE_APP_TYPE)
            {
                if (root_el) uwp_mark_xbf_ref_instantiated( root_el, cls_name );
                if (nd->flags & XBF_AOT_NODE_NO_PROVIDER)
                {
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                }
                else
                {
                    struct uwp_element *prev_head;
                    HRESULT phr;
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
                            {
                                if (e->kind == UWP_KIND_USER_CONTROL || e->kind == UWP_KIND_PAGE)
                                    el = e;
                            }
                            if (!el)
                            {
                                for (e = g_all_uwp_elems_head; e && e != prev_head; e = e->next_reg)
                                    el = e;
                            }
                        }
                        ReleaseSRWLockExclusive( &g_all_uwp_elems_lock );
                    }
                    if (el)
                    {
                        if (inst && !el->provider_inst) el->provider_inst = inst;
                        else if (!inst) inst = el->outer ? el->outer : &el->IInspectable_iface;
                        lstrcpynW( el->class_name, cls_name, ARRAY_SIZE(el->class_name) );
                        el->kind = classify_uwp_class( el->class_name );
                    }
                    if (FAILED(phr) || !inst)
                    {
                        if (!inst) uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                        if (!el) el = uwp_elem_from_any( inst );
                        if (el && !el->xbf_parsed && !el->children_coll && !el->content &&
                            (nd->flags & XBF_AOT_NODE_USING_NS) && short_name[0])
                        {
                            uwp_populate_element_from_pri_xbf( el, short_name );
                        }
                    }
                }
            }
            else
            {
                uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                el = uwp_elem_from_any( inst );
            }

            if (el)
            {
                if (root_el) el->owner_component = root_el;
                if (nd->has_init_argb)
                {
                    el->brush_argb = nd->init_argb;
                    el->has_brush_argb = TRUE;
                }
                if (nd->template_sub_idx != 0xffffffffu && xbf && xbf_sz)
                {
                    el->template_xbf_data = xbf;
                    el->template_xbf_size = xbf_sz;
                    el->template_sub_idx = nd->template_sub_idx;
                    uwp_set_template_owner( &el->template_owner_comp, component );
                }
                if (nd->flags & XBF_AOT_NODE_IS_RD)
                {
                    if ((nd->flags & XBF_AOT_NODE_ROOT_REUSE) && target_rd)
                    {
                        el->rd_bucket = target_rd;
                        target_rd->owner_el = el;
                    }
                    else
                    {
                        uwp_rd_bucket_for_el( el, TRUE );
                    }
                }
            }
            nodes_el[op->node_idx] = el;
            nodes_raw[op->node_idx] = inst;
            if (out_created && !*out_created && el) *out_created = el;
            break;
        }
        case XBF_AOT_OP_SET_XCLASS:
        {
            struct uwp_element *el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            if (el) lstrcpynW( el->x_class, xbf_aot_str( hdr, op->str_off ), ARRAY_SIZE(el->x_class) );
            break;
        }
        case XBF_AOT_OP_SET_XNAME:
        {
            struct uwp_element *el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            const WCHAR *name = xbf_aot_str( hdr, op->str_off );
            if (el && name[0])
            {
                lstrcpynW( el->name, name, ARRAY_SIZE(el->name) );
                if (root_el &&
                    xaml_vec_grow( (void **)&root_el->named_elems, &root_el->named_cap,
                                   root_el->named_cnt + 1, sizeof(*root_el->named_elems), 32 ))
                {
                    root_el->named_elems[root_el->named_cnt++] = el;
                }
            }
            break;
        }
        case XBF_AOT_OP_CONNECT:
        {
            struct uwp_element *el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            IInspectable *raw = (op->node_idx < hdr->node_cnt) ? nodes_raw[op->node_idx] : NULL;
            if (el) el->conn_id = op->cid;
            if (connector && raw && op->cid != 1)
            {
                HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)connector)[6];
                TRACE( "AOT Connect(cid=%d, target=%p [%s %s])\n", op->cid, raw,
                       el ? debugstr_w( el->class_name ) : "?", el ? debugstr_w( el->name ) : "" );
                conn_fn( connector, op->cid, raw );
                if (root_el && root_el->binding_connector && !is_template &&
                    (!out_created || (hdr->node_cnt > 0 && (nodes[0].flags & XBF_AOT_NODE_ROOT_REUSE))))
                {
                    HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)root_el->binding_connector)[6];
                    if (b_conn) b_conn( root_el->binding_connector, op->cid, raw );
                }
                else if (out_created)
                {
                    if (!dt_binding && component)
                    {
                        void *conn2 = NULL, *bc = NULL;
                        if (SUCCEEDED(IInspectable_QueryInterface( component, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
                        {
                            HRESULT (WINAPI *get_bc)(void *, int, void *, void **) = (void *)(*(void ***)conn2)[6];
                            if (get_bc && SUCCEEDED(get_bc( conn2, op->cid, raw, &bc )) && bc)
                            {
                                dt_binding = bc;
                                if (el && !el->dt_binding_connector)
                                {
                                    el->dt_binding_connector = bc;
                                    IUnknown_AddRef( (IUnknown *)bc );
                                }
                                if (el && !el->binding_connector)
                                {
                                    el->binding_connector = bc;
                                    IUnknown_AddRef( (IUnknown *)bc );
                                }
                            }
                            IUnknown_Release( (IUnknown *)conn2 );
                        }
                    }
                    if (dt_binding)
                    {
                        HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)dt_binding)[6];
                        if (b_conn) b_conn( dt_binding, op->cid, raw );
                    }
                }
            }
            break;
        }
        case XBF_AOT_OP_SET_PROP:
        {
            struct uwp_element *el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            if (el)
            {
                const WCHAR *sval = xbf_aot_str( hdr, op->str_off );
                xbf_apply_property( &fake_ctx, el, NULL, op->pid, op->vt, op->num, sval,
                                    op->vec4, op->is_res_ref != 0 );
            }
            break;
        }
        case XBF_AOT_OP_ADD_CHILD:
        {
            struct uwp_element *parent_el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            IInspectable *parent_raw = (op->node_idx < hdr->node_cnt) ? nodes_raw[op->node_idx] : NULL;
            struct uwp_element *child_el = (op->child_idx < hdr->node_cnt) ? nodes_el[op->child_idx] : NULL;
            IInspectable *child_raw = (op->child_idx < hdr->node_cnt) ? nodes_raw[op->child_idx] : NULL;
            if (!parent_el && !parent_raw) break;
            if (parent_el && op->pid == XBF_PROP_MERGED_DICTIONARIES && child_el && child_el->kind == UWP_KIND_RESOURCE_DICT)
            {
                struct uwp_rd_bucket *parent_rd = (parent_el->kind == UWP_KIND_RESOURCE_DICT)
                    ? uwp_rd_bucket_for_el( parent_el, TRUE )
                    : (target_rd ? target_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( child_el, TRUE );
                uwp_rd_link_merged( parent_rd, child_rd );
            }
            else if (child_raw)
            {
                BOOL handled_custom = FALSE;
                if (parent_el && (op->pid == XBF_PROP_KEYBOARD_ACCELERATORS || (child_el && (child_el->kind == UWP_KIND_ACCELERATOR ||
                                                                              wcsstr( child_el->class_name, L"Accelerator" )))))
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
                else if (!(op->pid & 0x8000) && op->pid < hdr->prop_cnt &&
                         fake_ctx.props[op->pid].owner_tid < hdr->type_cnt &&
                         fake_ctx.props[op->pid].name[0] &&
                         wcscmp( fake_ctx.props[op->pid].name, L"Children" ) &&
                         wcscmp( fake_ctx.props[op->pid].name, L"Items" ) &&
                         wcscmp( fake_ctx.props[op->pid].name, L"MergedDictionaries" ) &&
                         wcscmp( fake_ctx.props[op->pid].name, L"RowDefinitions" ) &&
                         wcscmp( fake_ctx.props[op->pid].name, L"ColumnDefinitions" ) &&
                         wcscmp( fake_ctx.props[op->pid].name, L"KeyboardAccelerators" ))
                {
                    const struct xbf_prop_info *pi = &fake_ctx.props[op->pid];
                    const WCHAR *owner_cls = fake_ctx.types[pi->owner_tid].full_name;
                    IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
                    if (owner_cls[0] && target)
                        handled_custom = uwp_add_custom_xaml_collection_item( app_outer, owner_cls, pi->name, target, child_raw );
                }
                if (!handled_custom && parent_el && parent_el->kind == UWP_KIND_TEXT_BLOCK &&
                    child_el && child_el->text[0])
                {
                    size_t cur = wcslen( parent_el->text );
                    if (cur + 1 < ARRAY_SIZE(parent_el->text))
                        lstrcpynW( parent_el->text + cur, child_el->text, (int)(ARRAY_SIZE(parent_el->text) - cur) );
                    handled_custom = TRUE;
                }
                if (!handled_custom && parent_el &&
                    (op->pid == XBF_PROP_PANEL_CHILDREN || op->pid == XBF_PROP_ITEMS ||
                     parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL))
                {
                    if (!parent_el->children_coll) uwp_vec_create( parent_el, &parent_el->children_coll );
                    uwp_vec_Append( parent_el->children_coll, child_raw );
                    if (child_el) child_el->parent = parent_el;
                }
            }
            break;
        }
        case XBF_AOT_OP_SET_OBJ_PROP:
        {
            struct uwp_element *parent_el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            IInspectable *parent_raw = (op->node_idx < hdr->node_cnt) ? nodes_raw[op->node_idx] : NULL;
            struct uwp_element *child_el = (op->child_idx < hdr->node_cnt) ? nodes_el[op->child_idx] : NULL;
            IInspectable *child_raw = (op->child_idx < hdr->node_cnt) ? nodes_raw[op->child_idx] : NULL;
            BOOL is_dict = (child_el && child_el->kind == UWP_KIND_RESOURCE_DICT);
            struct uwp_rd_bucket *child_rd = child_el ? uwp_rd_bucket_for_el( child_el, FALSE ) : NULL;

            if (is_dict || (child_rd && (op->pid == XBF_PROP_FRAMEWORKELEMENT_RESOURCES ||
                                         op->pid == XBF_PROP_MERGED_DICTIONARIES ||
                                         op->pid == XBF_PROP_THEME_DICTIONARIES)))
            {
                struct uwp_rd_bucket *dest_b = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                    ? uwp_rd_bucket_for_el( parent_el, TRUE )
                    : target_rd;
                if (!child_rd && child_el) child_rd = uwp_rd_bucket_for_el( child_el, TRUE );
                if (dest_b && child_rd && dest_b != child_rd)
                {
                    if (op->pid == XBF_PROP_THEME_DICTIONARIES)
                        uwp_rd_link_theme( dest_b, (child_el && child_el->name[0]) ? child_el->name : L"Default", child_rd );
                    else
                        uwp_rd_link_merged( dest_b, child_rd );
                }
            }
            else if (parent_el || parent_raw)
            {
                BOOL handled_custom_obj = FALSE;
                if (child_raw && !(op->pid & 0x8000) && op->pid < hdr->prop_cnt &&
                    fake_ctx.props[op->pid].owner_tid < hdr->type_cnt &&
                    fake_ctx.props[op->pid].name[0])
                {
                    const struct xbf_prop_info *pi = &fake_ctx.props[op->pid];
                    const WCHAR *owner_cls = fake_ctx.types[pi->owner_tid].full_name;
                    IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
                    if (owner_cls[0] && target &&
                        wcscmp( pi->name, L"Style" ) && wcscmp( pi->name, L"Resources" ) &&
                        wcscmp( pi->name, L"MergedDictionaries" ) && wcscmp( pi->name, L"ThemeDictionaries" ) &&
                        wcscmp( pi->name, L"ContextFlyout" ) && wcscmp( pi->name, L"Background" ) &&
                        wcscmp( pi->name, L"Foreground" ) && wcscmp( pi->name, L"BorderBrush" ))
                    {
                        if (uwp_set_custom_xaml_member_obj( app_outer, owner_cls, pi->name, target, child_raw ))
                        {
                            if (wcscmp( pi->name, L"Content" ) && wcscmp( pi->name, L"Child" ))
                                handled_custom_obj = TRUE;
                        }
                    }
                }
                if (handled_custom_obj || !parent_el)
                {
                    /* already applied via IXamlMember::SetValue */
                }
                else if (op->sv_kind == UWP_RD_BRUSH || op->sv_kind == UWP_RD_COLOR)
                {
                    UINT32 argb = 0;
                    BOOL got = FALSE;
                    const WCHAR *sval = xbf_aot_str( hdr, op->str_off );
                    if (op->has_argb)
                    {
                        argb = apply_opacity_to_argb( op->argb, op->num );
                        got = TRUE;
                    }
                    else if (sval[0] && uwp_resolve_brush_or_color( target_rd, parent_el, sval, 0, &argb ))
                    {
                        argb = apply_opacity_to_argb( argb, op->num );
                        got = TRUE;
                    }
                    else if (child_el && child_el->has_brush_argb)
                    {
                        argb = apply_opacity_to_argb( child_el->brush_argb, child_el->brush_opacity );
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
                    const WCHAR *based_on = xbf_aot_str( hdr, op->str_off );
                    UINT32 si;
                    if (based_on[0]) uwp_apply_style_by_name( &fake_ctx, parent_el, based_on );
                    for (si = 0; si < op->setter_cnt && op->setter_start + si < hdr->style_setter_cnt; si++)
                    {
                        const struct uwp_style_setter *st = &style_setters[op->setter_start + si];
                        xbf_apply_property( &fake_ctx, parent_el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
                    }
                }
                else if (op->pid == 0x000c && child_el && child_el->text[0])
                {
                    if (!parent_el->text[0])
                        lstrcpynW( parent_el->text, child_el->text, ARRAY_SIZE(parent_el->text) );
                }
                else if (child_raw &&
                         (op->pid == XBF_PROP_DATACONTEXT ||
                          (!(op->pid & 0x8000) && op->pid < hdr->prop_cnt && !wcscmp( fake_ctx.props[op->pid].name, L"DataContext" ))))
                {
                    uwp_fe_put_DataContext( &parent_el->IInspectable_iface, child_raw );
                }
                else if (child_raw &&
                         (op->pid == XBF_PROP_CONTEXT_FLYOUT ||
                          (op->pid < hdr->prop_cnt && !wcscmp( fake_ctx.props[op->pid].name, L"ContextFlyout" ))))
                {
                    if (parent_el->context_flyout) IInspectable_Release( parent_el->context_flyout );
                    parent_el->context_flyout = child_raw;
                    IInspectable_AddRef( child_raw );
                    if (child_el) child_el->parent = parent_el;
                }
                else if (child_raw && op->sv_kind == UWP_RD_OBJ &&
                         op->pid < hdr->prop_cnt &&
                         !wcscmp( fake_ctx.props[op->pid].name, L"Pane" ))
                {
                    if (parent_el->pane) IInspectable_Release( parent_el->pane );
                    parent_el->pane = child_raw;
                    IInspectable_AddRef( child_raw );
                    if (child_el) child_el->parent = parent_el;
                }
                else if (child_raw && op->sv_kind == UWP_RD_OBJ &&
                         (!child_el || (child_el->kind != UWP_KIND_STYLE &&
                                        child_el->kind != UWP_KIND_BRUSH &&
                                        child_el->kind != UWP_KIND_COLOR)) &&
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
            break;
        }
        case XBF_AOT_OP_PROVIDE_VALUE:
        {
            struct uwp_element *target_el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            struct uwp_element *val_el = (op->child_idx < hdr->node_cnt) ? nodes_el[op->child_idx] : NULL;
            if (target_el && val_el && val_el->text[0])
            {
                WCHAR resolved[256] = {0};
                if (uwp_pri_lookup_string( val_el->text, resolved, ARRAY_SIZE(resolved) ) && resolved[0])
                {
                    const WCHAR *pname = (op->pid < hdr->prop_cnt) ? fake_ctx.props[op->pid].name : L"";
                    BOOL is_chord = (!wcscmp( pname, L"VirtualKeyControlChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyAltChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyShiftChord" ) ||
                                     !wcscmp( pname, L"VirtualKeyControlShiftChord" ));
                    BOOL is_vk = (!wcscmp( pname, L"VirtualKey" ) || is_chord);
                    if (is_chord)
                    {
                        UINT vk = uwp_parse_vk_string( resolved );
                        UINT mods = !wcscmp( pname, L"VirtualKeyControlChord" ) ? 1 :
                                    (!wcscmp( pname, L"VirtualKeyAltChord" ) ? 2 :
                                     (!wcscmp( pname, L"VirtualKeyShiftChord" ) ? 4 : 5));
                        if (vk) uwp_accel_register( NULL, target_el, vk, mods, TRUE );
                    }
                    if (op->pid == XBF_PROP_CONTENT || op->pid == XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV ||
                        op->pid == XBF_PROP_TEXTBLOCK_TEXT || (!is_vk && !target_el->text[0]))
                        lstrcpynW( target_el->text, resolved, ARRAY_SIZE(target_el->text) );
                }
            }
            break;
        }
        case XBF_AOT_OP_RD_LINK_THEME:
        {
            struct uwp_element *parent_el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            struct uwp_element *child_el = (op->child_idx < hdr->node_cnt) ? nodes_el[op->child_idx] : NULL;
            if (child_el && child_el->kind == UWP_KIND_RESOURCE_DICT)
            {
                struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                    ? uwp_rd_bucket_for_el( parent_el, TRUE )
                    : (target_rd ? target_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( child_el, TRUE );
                uwp_rd_link_theme( parent_rd, xbf_aot_str( hdr, op->str_off ), child_rd );
            }
            break;
        }
        case XBF_AOT_OP_RD_STORE:
        {
            struct uwp_element *parent_el = (op->node_idx < hdr->node_cnt) ? nodes_el[op->node_idx] : NULL;
            struct uwp_element *child_el = (op->child_idx < hdr->node_cnt) ? nodes_el[op->child_idx] : NULL;
            IInspectable *child_raw = (op->child_idx < hdr->node_cnt) ? nodes_raw[op->child_idx] : NULL;
            const WCHAR *key = xbf_aot_str( hdr, op->str_off );
            struct uwp_rd_bucket *dest_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                ? uwp_rd_bucket_for_el( parent_el, TRUE )
                : (target_rd ? target_rd : (parent_el ? uwp_rd_bucket_for_el( parent_el, TRUE ) : NULL));
            if (!dest_rd && root_el)
                dest_rd = uwp_rd_bucket_for_el( root_el, TRUE );
            if (dest_rd && key[0])
            {
                struct xbf_stack_val sv = {0};
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
                    lstrcpynW( sv.based_on, xbf_aot_str( hdr, op->str2_off ), ARRAY_SIZE(sv.based_on) );
                else
                    lstrcpynW( sv.str_val, xbf_aot_str( hdr, op->str2_off ), ARRAY_SIZE(sv.str_val) );
                if (op->setter_cnt > 0 && op->setter_start + op->setter_cnt <= hdr->style_setter_cnt)
                {
                    sv.setters = (struct uwp_style_setter *)&style_setters[op->setter_start];
                    sv.setter_cnt = (int)op->setter_cnt;
                }
                if (child_el && child_el->template_xbf_data && child_el->template_xbf_size)
                {
                    sv.template_xbf_data = child_el->template_xbf_data;
                    sv.template_xbf_size = child_el->template_xbf_size;
                    sv.template_sub_idx = child_el->template_sub_idx;
                    sv.template_owner_comp = child_el->template_owner_comp;
                }
                uwp_rd_store_stack_val( dest_rd, key, &sv );
                if (target_rd && target_rd != dest_rd)
                    uwp_rd_store_stack_val( target_rd, key, &sv );
                if (root_el)
                {
                    struct uwp_rd_bucket *root_rd = uwp_rd_bucket_for_el( root_el, TRUE );
                    if (root_rd && root_rd != dest_rd && root_rd != target_rd)
                        uwp_rd_store_stack_val( root_rd, key, &sv );
                }
            }
            break;
        }
        }
    }

    /* 3. Replay VSM groups, states, and setters */
    if (root_el && hdr->vsm_group_cnt > 0)
    {
        UINT32 g, s, t;
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

    /* 4. Replay deferred element entries */
    if (root_el && hdr->defer_cnt > 0)
    {
        for (i = 0; i < hdr->defer_cnt; i++)
        {
            const struct xbf_aot_defer *ad = &defers[i];
            if (xaml_vec_grow( (void **)&root_el->defer_entries, &root_el->defer_cap,
                               root_el->defer_cnt + 1, sizeof(*root_el->defer_entries), 16 ))
            {
                struct uwp_defer_entry *de = &root_el->defer_entries[root_el->defer_cnt++];
                lstrcpynW( de->name, xbf_aot_str( hdr, ad->name_off ), ARRAY_SIZE(de->name) );
                de->sub_idx = ad->sub_idx;
                de->parent_panel = (ad->parent_panel_idx < hdr->node_cnt && nodes_el && nodes_el[ad->parent_panel_idx])
                    ? nodes_el[ad->parent_panel_idx] : root_el;
                de->xbf_data = xbf;
                de->xbf_size = xbf_sz;
                de->root_component = component;
                de->realized = FALSE;
            }
        }
    }

    if (dt_binding) IUnknown_Release( (IUnknown *)dt_binding );
    free( nodes_el );
    free( nodes_raw );
}

