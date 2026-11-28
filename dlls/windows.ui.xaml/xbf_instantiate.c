/* WinRT Windows.UI.Xaml UWP Visual Tree, Direct XBF v2 Interpreter & Header Cache
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

#define XBF_HDR_CACHE_SLOTS 512

struct xbf_sub_span
{
    DWORD start;
    DWORD end;
    DWORD sz;
};

struct xbf_parsed_ref
{
    WCHAR full_name[128];
    WCHAR short_name[128];
    BOOL  is_resource_path;
};

struct xbf_parsed_hdr
{
    const BYTE *xbf;
    DWORD xbf_sz;
    BOOL  is_uwp_conn;
    BOOL  cached;

    DWORD meta_sz;
    DWORD node_sz;
    const BYTE *nodes;
    DWORD sub_cnt;
    DWORD sub_base;

    WCHAR *str_pool;
    WCHAR **strs;
    DWORD str_cnt;

    struct xbf_type_info *types;
    DWORD type_cnt;

    struct xbf_prop_info *props;
    DWORD prop_cnt;

    struct xbf_sub_span *subs;

    struct xbf_parsed_ref *refs;
    DWORD ref_cnt;
    const WCHAR **res_uris;
    DWORD res_uri_cnt;
    BOOL  refs_scanned;

    LONG   ref;
    UINT64 last_used;

    WCHAR *strs_stk[64];
    struct xbf_type_info types_stk[64];
    struct xbf_prop_info props_stk[64];
    struct xbf_sub_span subs_stk[64];
};

static SRWLOCK g_xbf_hdr_lock = SRWLOCK_INIT;
static struct xbf_parsed_hdr *g_xbf_hdr_cache[XBF_HDR_CACHE_SLOTS];
static UINT64 g_xbf_hdr_lru = 0;

struct xbf_deferred_res
{
    struct uwp_element *el;
    IInspectable *raw;
    WORD pid;
    int vt;
    double num;
    double vec4[4];
    WCHAR sval[128];
    BOOL is_res_ref;
};

struct xbf_exec_ctx
{
    struct xbf_parsed_hdr *hdr;
    struct xbf_ctx ctx;
    struct uwp_element **out_created;
    BOOL reuse_root;
    BOOL winui3_rd_only;
    BOOL refs_applied;
    void *root_binding;
    void *dt_binding;
    int start_defer_cnt;

    BOOL *sub_done;
    BOOL sub_done_stk[64];

    struct xbf_deferred_res *def_res;
    int def_res_cnt;
    int def_res_cap;
    BOOL def_res_heap;
    struct xbf_deferred_res def_res_stk[64];

    struct uwp_element **active_trigs;
    int active_trig_cnt;
    int active_trig_cap;
    BOOL active_trigs_heap;
    struct uwp_element *active_trigs_stk[64];
};

struct xbf_sub_state
{
    struct xbf_exec_ctx *ec;
    struct xbf_ctx *ctx;
    int sub_idx;
    DWORD sub_start;
    DWORD end_p;
    DWORD sub_sz;
    DWORD p;
    const struct xbf_rd_table *rd_tbl;
    struct uwp_element *active_rd_el;
    BOOL vsm_collect;

    struct xbf_stack_val *val_stack;
    DWORD *start_off_stack;
    BOOL *is_rd_stack;
    int val_cap;
    struct xbf_stack_val val_stack_stk[64];
    DWORD start_off_stk[64];
    BOOL is_rd_stk[64];
    int obj_sp;
    int base_sp;
    WORD *prop_stack;
    int prop_cap;
    WORD prop_stack_stk[64];
    int prop_sp;

    struct xbf_stack_val last_popped;
    DWORD last_popped_off;
    BOOL last_popped_is_rd;
    BOOL has_last_popped;
};

static BOOL xbf_sub_ensure_val_cap( struct xbf_sub_state *st, int need )
{
    int ncap;
    struct xbf_stack_val *nv;
    DWORD *no;
    BOOL *nr;
    if (need <= st->val_cap) return TRUE;
    ncap = st->val_cap ? st->val_cap * 2 : 128;
    while (ncap < need) ncap *= 2;
    nv = malloc( (size_t)ncap * sizeof(*nv) );
    no = malloc( (size_t)ncap * sizeof(*no) );
    nr = malloc( (size_t)ncap * sizeof(*nr) );
    if (!nv || !no || !nr)
    {
        free( nv );
        free( no );
        free( nr );
        return FALSE;
    }
    memcpy( nv, st->val_stack, (size_t)st->obj_sp * sizeof(*nv) );
    memcpy( no, st->start_off_stack, (size_t)st->obj_sp * sizeof(*no) );
    memcpy( nr, st->is_rd_stack, (size_t)st->obj_sp * sizeof(*nr) );
    if (st->val_stack != st->val_stack_stk) free( st->val_stack );
    if (st->start_off_stack != st->start_off_stk) free( st->start_off_stack );
    if (st->is_rd_stack != st->is_rd_stk) free( st->is_rd_stack );
    st->val_stack = nv;
    st->start_off_stack = no;
    st->is_rd_stack = nr;
    st->val_cap = ncap;
    return TRUE;
}

static BOOL xbf_sub_ensure_prop_cap( struct xbf_sub_state *st, int need )
{
    int ncap;
    WORD *np;
    if (need <= st->prop_cap) return TRUE;
    ncap = st->prop_cap ? st->prop_cap * 2 : 128;
    while (ncap < need) ncap *= 2;
    if (st->prop_stack == st->prop_stack_stk)
    {
        np = malloc( (size_t)ncap * sizeof(*np) );
        if (!np) return FALSE;
        memcpy( np, st->prop_stack, (size_t)st->prop_sp * sizeof(*np) );
    }
    else
    {
        np = realloc( st->prop_stack, (size_t)ncap * sizeof(*np) );
        if (!np) return FALSE;
    }
    st->prop_stack = np;
    st->prop_cap = ncap;
    return TRUE;
}

static void xbf_parsed_hdr_free( struct xbf_parsed_hdr *h )
{
    if (!h) return;
    if (h->strs && h->strs != h->strs_stk) free( h->strs );
    free( h->str_pool );
    if (h->types && h->types != h->types_stk) free( h->types );
    if (h->props && h->props != h->props_stk) free( h->props );
    if (h->subs && h->subs != h->subs_stk) free( h->subs );
    free( h->refs );
    free( (void *)h->res_uris );
    if (h->cached) free( h );
}

static void xbf_parsed_hdr_release( struct xbf_parsed_hdr *h )
{
    if (!h) return;
    if (h->cached)
        InterlockedDecrement( &h->ref );
    else
        xbf_parsed_hdr_free( h );
}

static BOOL xbf_parse_header_tables( const BYTE *xbf, DWORD xbf_sz, BOOL is_uwp_conn,
                                     BOOL allow_stack, struct xbf_parsed_hdr *h )
{
    const BYTE *meta_base, *meta_end, *p;
    UINT64 offs[6];
    DWORD i, total_wchars = 0;

    if (!xbf || xbf_sz < 0x50 || memcmp( xbf, "XBF\0", 4 ) != 0) return FALSE;

    h->xbf = xbf;
    h->xbf_sz = xbf_sz;
    h->is_uwp_conn = is_uwp_conn;
    h->meta_sz = *(const DWORD *)(xbf + 4);
    h->node_sz = *(const DWORD *)(xbf + 8);
    if (h->meta_sz > xbf_sz - 12 || h->node_sz > xbf_sz - 12 - h->meta_sz) return FALSE;

    meta_base = xbf + 12;
    meta_end = meta_base + h->meta_sz;
    memcpy( offs, xbf + 20, sizeof(offs) );

    /* 1. String Table (two-pass into a single str_pool allocation) */
    if (offs[0] > h->meta_sz || h->meta_sz - (DWORD)offs[0] < 4) return FALSE;
    p = meta_base + (size_t)offs[0];
    h->str_cnt = *(const DWORD *)p;
    p += 4;
    if (h->str_cnt > 16384 || h->str_cnt > (DWORD)(meta_end - p) / 4) return FALSE;

    if (h->str_cnt > 0)
    {
        const BYTE *scan = p;
        for (i = 0; i < h->str_cnt; i++)
        {
            DWORD len;
            if ((size_t)(meta_end - scan) < 4) return FALSE;
            len = *(const DWORD *)scan;
            scan += 4;
            if (len > (DWORD)(meta_end - scan) / 2) return FALSE;
            total_wchars += len + 1;
            scan += len * 2;
            if ((size_t)(meta_end - scan) >= 2) scan += 2;
        }
        if (allow_stack && h->str_cnt <= ARRAY_SIZE(h->strs_stk))
            h->strs = h->strs_stk;
        else
            h->strs = calloc( h->str_cnt, sizeof(WCHAR *) );
        h->str_pool = calloc( max( 1u, total_wchars ), sizeof(WCHAR) );
        if (!h->strs || !h->str_pool) return FALSE;

        {
            WCHAR *dst = h->str_pool;
            for (i = 0; i < h->str_cnt; i++)
            {
                DWORD len = *(const DWORD *)p;
                p += 4;
                h->strs[i] = dst;
                if (len > 0) memcpy( dst, p, len * sizeof(WCHAR) );
                dst[len] = 0;
                dst += len + 1;
                p += len * 2;
                if ((size_t)(meta_end - p) >= 2) p += 2;
            }
        }
    }

    /* 2. XmlNamespace Table -> Type Table -> Property Table */
    {
        const BYTE *xp = (offs[5] <= h->meta_sz) ? meta_base + (size_t)offs[5] : meta_end;
        DWORD xcnt = ((size_t)(meta_end - xp) >= 4) ? *(const DWORD *)xp : 0;
        const DWORD *x_sids = (const DWORD *)(xp + 4);
        const BYTE *tp = (offs[3] <= h->meta_sz) ? meta_base + (size_t)offs[3] : meta_end;
        const BYTE *pp = (offs[4] <= h->meta_sz) ? meta_base + (size_t)offs[4] : meta_end;

        if (!(xcnt > 0 && xcnt < 4096 && (size_t)(meta_end - xp) >= 4 && xcnt <= (DWORD)(meta_end - xp - 4) / 4))
            xcnt = 0;

        if ((size_t)(meta_end - tp) >= 4)
        {
            h->type_cnt = *(const DWORD *)tp;
            if (h->type_cnt > 0 && h->type_cnt < 4096 && h->type_cnt <= (DWORD)(meta_end - tp - 4) / 12)
            {
                if (allow_stack && h->type_cnt <= ARRAY_SIZE(h->types_stk))
                {
                    h->types = h->types_stk;
                    memset( h->types, 0, h->type_cnt * sizeof(*h->types) );
                }
                else
                    h->types = calloc( h->type_cnt, sizeof(*h->types) );
                if (!h->types) return FALSE;
                for (i = 0; i < h->type_cnt; i++)
                {
                    DWORD xid = *(const DWORD *)(tp + 4 + i * 12 + 4);
                    DWORD sid = *(const DWORD *)(tp + 4 + i * 12 + 8);
                    const WCHAR *sname = (sid < h->str_cnt && h->strs[sid]) ? h->strs[sid] : L"";
                    const WCHAR *ns = L"";
                    if (xid < xcnt && x_sids[xid] < h->str_cnt && h->strs[x_sids[xid]])
                        ns = h->strs[x_sids[xid]];
                    lstrcpynW( h->types[i].xmlns, ns, ARRAY_SIZE(h->types[i].xmlns) );
                    lstrcpynW( h->types[i].short_name, sname, 64 );
                    if (ns[0] && !wcsncmp( ns, L"using:", 6 ))
                        swprintf( h->types[i].full_name, 128, L"%s.%s", ns + 6, sname );
                    else
                        swprintf( h->types[i].full_name, 128, L"%s.Controls.%s",
                                  is_uwp_conn ? L"Windows.UI.Xaml" : L"Microsoft.UI.Xaml", sname );
                }
            }
            else
                h->type_cnt = 0;
        }

        if ((size_t)(meta_end - pp) >= 4)
        {
            h->prop_cnt = *(const DWORD *)pp;
            if (h->prop_cnt > 0 && h->prop_cnt < 4096 && h->prop_cnt <= (DWORD)(meta_end - pp - 4) / 12)
            {
                if (allow_stack && h->prop_cnt <= ARRAY_SIZE(h->props_stk))
                {
                    h->props = h->props_stk;
                    memset( h->props, 0, h->prop_cnt * sizeof(*h->props) );
                }
                else
                    h->props = calloc( h->prop_cnt, sizeof(*h->props) );
                if (!h->props) return FALSE;
                for (i = 0; i < h->prop_cnt; i++)
                {
                    DWORD tid = *(const DWORD *)(pp + 4 + i * 12 + 4);
                    DWORD sid = *(const DWORD *)(pp + 4 + i * 12 + 8);
                    h->props[i].owner_tid = (WORD)tid;
                    if (sid < h->str_cnt && h->strs[sid])
                        lstrcpynW( h->props[i].name, h->strs[sid], 64 );
                }
            }
            else
                h->prop_cnt = 0;
        }
    }

    /* 3. Node Substream Span Table */
    h->nodes = xbf + 12 + h->meta_sz;
    if (h->node_sz >= 12)
    {
        DWORD sub_cnt = *(const DWORD *)h->nodes;
        if (sub_cnt > 0 && sub_cnt < 2048 && sub_cnt <= (h->node_sz - 4) / 8)
        {
            struct xbf_ctx tmp_ctx = {0};
            h->sub_cnt = sub_cnt;
            h->sub_base = 4 + sub_cnt * 8;
            if (allow_stack && sub_cnt <= ARRAY_SIZE(h->subs_stk))
                h->subs = h->subs_stk;
            else
                h->subs = calloc( sub_cnt, sizeof(*h->subs) );
            if (!h->subs) return FALSE;
            tmp_ctx.nodes = h->nodes;
            tmp_ctx.node_sz = h->node_sz;
            tmp_ctx.sub_cnt = h->sub_cnt;
            tmp_ctx.sub_base = h->sub_base;
            for (i = 0; i < sub_cnt; i++)
            {
                if (!xbf_get_substream_bounds( &tmp_ctx, i, &h->subs[i].start, &h->subs[i].end, &h->subs[i].sz ))
                {
                    if (!allow_stack || h->subs != h->subs_stk) free( h->subs );
                    h->subs = NULL;
                    h->sub_cnt = 0;
                    break;
                }
            }
        }
    }
    return TRUE;
}

static void xbf_hdr_scan_refs( struct xbf_parsed_hdr *h )
{
    struct xbf_parsed_ref *refs = NULL;
    int ref_cnt = 0, ref_cap = 0;
    const WCHAR **uris = NULL;
    int uri_cnt = 0, uri_cap = 0;
    const BYTE *rx = NULL;
    DWORD rsz = 0, i;

    for (i = 0; i < h->type_cnt; i++)
    {
        if (!wcsncmp( h->types[i].xmlns, L"using:", 6 ) && h->types[i].short_name[0] &&
            uwp_pri_find_xbf( h->types[i].short_name, &rx, &rsz ))
        {
            if (xaml_vec_grow( (void **)&refs, &ref_cap, ref_cnt + 1, sizeof(*refs), 16 ))
            {
                lstrcpynW( refs[ref_cnt].full_name, h->types[i].full_name, ARRAY_SIZE(refs[ref_cnt].full_name) );
                lstrcpynW( refs[ref_cnt].short_name, h->types[i].short_name, ARRAY_SIZE(refs[ref_cnt].short_name) );
                refs[ref_cnt].is_resource_path = FALSE;
                ref_cnt++;
            }
        }
    }
    for (i = 0; i < h->str_cnt; i++)
    {
        const WCHAR *s = h->strs[i];
        size_t slen = s ? wcslen( s ) : 0;
        if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
            uwp_pri_find_xbf( s, &rx, &rsz ))
        {
            if (xaml_vec_grow( (void **)&refs, &ref_cap, ref_cnt + 1, sizeof(*refs), 16 ))
            {
                lstrcpynW( refs[ref_cnt].full_name, s, ARRAY_SIZE(refs[ref_cnt].full_name) );
                lstrcpynW( refs[ref_cnt].short_name, s, ARRAY_SIZE(refs[ref_cnt].short_name) );
                refs[ref_cnt].is_resource_path = TRUE;
                ref_cnt++;
            }
            if (wcsstr( s, L"Resource" ) || wcsstr( s, L"Theme" ) || wcsstr( s, L"Generic" ) ||
                wcsstr( s, L"Dictionary" ) || wcsstr( s, L"Style" ))
            {
                if (xaml_vec_grow( (void **)&uris, &uri_cap, uri_cnt + 1, sizeof(*uris), 8 ))
                    uris[uri_cnt++] = s;
            }
        }
    }
    h->refs = refs;
    h->ref_cnt = (DWORD)ref_cnt;
    h->res_uris = uris;
    h->res_uri_cnt = (DWORD)uri_cnt;
    __atomic_store_n( &h->refs_scanned, TRUE, __ATOMIC_RELEASE );
}

static struct xbf_parsed_hdr *xbf_acquire_parsed_hdr( const BYTE *xbf, DWORD xbf_sz,
                                                      BOOL is_uwp_conn, BOOL need_refs,
                                                      struct xbf_parsed_hdr *local_buf )
{
    BOOL immutable = xbf_is_immutable_pri_blob( xbf, xbf_sz );
    struct xbf_parsed_hdr *h = NULL;
    int i, victim = -1;
    UINT64 best_lru = (~0ULL);

    if (!immutable)
    {
        memset( local_buf, 0, sizeof(*local_buf) );
        if (!xbf_parse_header_tables( xbf, xbf_sz, is_uwp_conn, TRUE, local_buf ))
        {
            xbf_parsed_hdr_free( local_buf );
            return NULL;
        }
        if (need_refs) xbf_hdr_scan_refs( local_buf );
        return local_buf;
    }

    AcquireSRWLockShared( &g_xbf_hdr_lock );
    for (i = 0; i < XBF_HDR_CACHE_SLOTS; i++)
    {
        struct xbf_parsed_hdr *c = g_xbf_hdr_cache[i];
        if (c && c->xbf == xbf && c->xbf_sz == xbf_sz && c->is_uwp_conn == is_uwp_conn)
        {
            InterlockedIncrement( &c->ref );
            __atomic_store_n( &c->last_used,
                              __atomic_add_fetch( &g_xbf_hdr_lru, 1, __ATOMIC_RELAXED ),
                              __ATOMIC_RELAXED );
            h = c;
            break;
        }
    }
    ReleaseSRWLockShared( &g_xbf_hdr_lock );

    if (h)
    {
        if (need_refs && !__atomic_load_n( &h->refs_scanned, __ATOMIC_ACQUIRE ))
        {
            AcquireSRWLockExclusive( &g_xbf_hdr_lock );
            if (!__atomic_load_n( &h->refs_scanned, __ATOMIC_RELAXED )) xbf_hdr_scan_refs( h );
            ReleaseSRWLockExclusive( &g_xbf_hdr_lock );
        }
        return h;
    }

    h = calloc( 1, sizeof(*h) );
    if (!h) return NULL;
    h->cached = TRUE;
    h->ref = 1;
    if (!xbf_parse_header_tables( xbf, xbf_sz, is_uwp_conn, FALSE, h ))
    {
        xbf_parsed_hdr_free( h );
        return NULL;
    }
    if (need_refs) xbf_hdr_scan_refs( h );

    AcquireSRWLockExclusive( &g_xbf_hdr_lock );
    for (i = 0; i < XBF_HDR_CACHE_SLOTS; i++)
    {
        struct xbf_parsed_hdr *c = g_xbf_hdr_cache[i];
        if (c && c->xbf == xbf && c->xbf_sz == xbf_sz && c->is_uwp_conn == is_uwp_conn)
        {
            InterlockedIncrement( &c->ref );
            __atomic_store_n( &c->last_used,
                              __atomic_add_fetch( &g_xbf_hdr_lru, 1, __ATOMIC_RELAXED ),
                              __ATOMIC_RELAXED );
            if (need_refs && !__atomic_load_n( &c->refs_scanned, __ATOMIC_RELAXED )) xbf_hdr_scan_refs( c );
            ReleaseSRWLockExclusive( &g_xbf_hdr_lock );
            xbf_parsed_hdr_free( h );
            return c;
        }
    }
    for (i = 0; i < XBF_HDR_CACHE_SLOTS; i++)
    {
        struct xbf_parsed_hdr *c = g_xbf_hdr_cache[i];
        UINT64 lu;
        if (!c) { victim = i; break; }
        lu = __atomic_load_n( &c->last_used, __ATOMIC_RELAXED );
        if (__atomic_load_n( &c->ref, __ATOMIC_ACQUIRE ) == 0 && lu < best_lru)
        {
            best_lru = lu;
            victim = i;
        }
    }
    if (victim >= 0)
    {
        xbf_parsed_hdr_free( g_xbf_hdr_cache[victim] );
        __atomic_store_n( &h->last_used,
                          __atomic_add_fetch( &g_xbf_hdr_lru, 1, __ATOMIC_RELAXED ),
                          __ATOMIC_RELAXED );
        g_xbf_hdr_cache[victim] = h;
    }
    else
    {
        /* Cache full with all slots pinned: treat as ephemeral heap header */
        h->cached = FALSE;
        h->last_used = 0;
    }
    ReleaseSRWLockExclusive( &g_xbf_hdr_lock );
    return h;
}

static BOOL xbf_hdr_substream_bounds( const struct xbf_parsed_hdr *h, DWORD sub_idx,
                                      DWORD *out_start, DWORD *out_end, DWORD *out_sz )
{
    if (!h || !h->subs || sub_idx >= h->sub_cnt) return FALSE;
    if (out_start) *out_start = h->subs[sub_idx].start;
    if (out_end) *out_end = h->subs[sub_idx].end;
    if (out_sz) *out_sz = h->subs[sub_idx].sz;
    return TRUE;
}

static void xbf_sub_clear_last_popped( struct xbf_sub_state *st )
{
    free( st->last_popped.setters );
    memset( &st->last_popped, 0, sizeof(st->last_popped) );
    st->last_popped_is_rd = FALSE;
    st->has_last_popped = FALSE;
}

static BOOL xbf_sub_seek_next_rd_entry( struct xbf_sub_state *st, DWORD cur_rel_off )
{
    DWORD next_off = 0xffffffffu;
    int k;

    if (!st->rd_tbl) return FALSE;
    for (k = 0; k < st->rd_tbl->count; k++)
    {
        if (st->rd_tbl->items[k].sub_off > cur_rel_off && st->rd_tbl->items[k].sub_off < next_off)
            next_off = st->rd_tbl->items[k].sub_off;
    }
    if (next_off == 0xffffffffu || next_off >= st->sub_sz) return FALSE;

    st->p = st->sub_start + next_off;
    while (st->obj_sp > st->base_sp)
    {
        st->obj_sp--;
        free( st->val_stack[st->obj_sp].setters );
        memset( &st->val_stack[st->obj_sp], 0, sizeof(st->val_stack[st->obj_sp]) );
    }
    st->prop_sp = 0;
    xbf_sub_clear_last_popped( st );
    return TRUE;
}

static void xbf_exec_init_sv_kind( struct xbf_ctx *ctx, BYTE op, WORD tid,
                                   int vt_init, double num_init, const WCHAR *sbuf_init,
                                   const double vec4_init[4], struct xbf_stack_val *sv )
{
    if (tid == XBF_TYPE_COLOR)
    {
        sv->kind = UWP_RD_COLOR;
        if (vt_init == 0x08) { sv->argb = (UINT32)num_init; sv->has_argb = TRUE; }
        else if (sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv->argb )) sv->has_argb = TRUE;
    }
    else if (tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_INT32 ||
             ((op == 0x15 || op == 0x16) && (vt_init == 0x03 || vt_init == 0x04 || vt_init == 0x0b)))
    {
        sv->kind = UWP_RD_DOUBLE;
        sv->dval = num_init;
    }
    else if (tid == XBF_TYPE_GRID_LENGTH)
    {
        sv->kind = UWP_RD_DOUBLE;
        sv->dval = (vt_init == 0x07) ? vec4_init[1] : num_init;
    }
    else if (tid == XBF_TYPE_THICKNESS)
    {
        sv->kind = UWP_RD_THICKNESS;
        memcpy( sv->vec4, vec4_init, sizeof(sv->vec4) );
    }
    else if (tid == XBF_TYPE_CORNER_RADIUS)
    {
        sv->kind = UWP_RD_THICKNESS;
        if (vt_init == 0x06) memcpy( sv->vec4, vec4_init, sizeof(sv->vec4) );
        else if (vt_init == 0x03 || vt_init == 0x04)
            sv->vec4[0] = sv->vec4[1] = sv->vec4[2] = sv->vec4[3] = num_init;
        else if (sbuf_init[0])
        {
            const WCHAR *src = sbuf_init;
            WCHAR *ep = NULL;
            double v0 = wcstod( src, &ep );
            sv->vec4[0] = sv->vec4[1] = sv->vec4[2] = sv->vec4[3] = v0;
            if (ep && (*ep == L',' || *ep == L' '))
            {
                int ci;
                for (ci = 1; ci < 4 && *ep; ci++)
                {
                    while (*ep == L',' || *ep == L' ') ep++;
                    if (!*ep) break;
                    sv->vec4[ci] = wcstod( ep, &ep );
                }
            }
        }
    }
    else if (tid == XBF_TYPE_FONT_FAMILY)
    {
        sv->kind = UWP_RD_FONT_FAMILY;
        if (sbuf_init[0])
        {
            WCHAR face[64] = {0};
            lstrcpynW( sv->str_val, sbuf_init, ARRAY_SIZE(sv->str_val) );
            uwp_resolve_font_family( sbuf_init, face, ARRAY_SIZE(face) );
        }
    }
    else if (tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_BRUSH_B ||
             tid == XBF_TYPE_IMAGE_BRUSH || tid == XBF_TYPE_LINEAR_GRADIENT_BRUSH ||
             (!(tid & 0x8000) && tid < ctx->type_cnt && wcsstr( ctx->types[tid].short_name, L"Brush" )))
    {
        sv->kind = UWP_RD_BRUSH;
        if (op == 0x16 && sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv->argb ))
            sv->has_argb = TRUE;
    }
    else if (tid == XBF_TYPE_DATA_TEMPLATE || tid == XBF_TYPE_CONTROL_TEMPLATE)
        sv->kind = UWP_RD_DATA_TEMPLATE;
    else if (tid == XBF_TYPE_STYLE)
        sv->kind = UWP_RD_STYLE;
    else if ((op == 0x15 || op == 0x16) && sbuf_init[0])
    {
        sv->kind = UWP_RD_STRING;
        lstrcpynW( sv->str_val, sbuf_init, ARRAY_SIZE(sv->str_val) );
    }
    sv->tid = tid;
}

static void xbf_exec_load_custom_rd_xbf( struct xbf_exec_ctx *ec, WORD tid, struct uwp_rd_bucket *sub_rd )
{
    const WCHAR *ns, *dot;
    WCHAR xaml_name[256];
    BOOL loaded = FALSE;

    if (!sub_rd || (tid & 0x8000) || tid >= ec->ctx.type_cnt) return;
    if (wcsncmp( ec->ctx.types[tid].xmlns, L"using:", 6 ) != 0) return;
    if (!ec->ctx.types[tid].short_name[0] ||
        !wcscmp( ec->ctx.types[tid].short_name, L"XamlControlsResources" ))
        return;

    ns = ec->ctx.types[tid].xmlns + 6;
    dot = wcschr( ns, L'.' );
    if (dot && dot[1])
    {
        WCHAR rel_ns[128];
        int ni;
        lstrcpynW( rel_ns, dot + 1, ARRAY_SIZE(rel_ns) );
        for (ni = 0; rel_ns[ni]; ni++)
            if (rel_ns[ni] == L'.') rel_ns[ni] = L'/';
        swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s/%s.xaml", rel_ns, ec->ctx.types[tid].short_name );
        uwp_load_res_xbf_into_bucket( ec->ctx.base_xbf_dir[0] ? ec->ctx.base_xbf_dir : NULL, xaml_name, sub_rd );
        loaded = (sub_rd->count > 0 || sub_rd->merged_cnt > 0 || sub_rd->theme_dark || sub_rd->theme_default);
    }
    if (!loaded && ns[0])
    {
        WCHAR full_ns[128];
        int ni;
        lstrcpynW( full_ns, ns, ARRAY_SIZE(full_ns) );
        for (ni = 0; full_ns[ni]; ni++)
            if (full_ns[ni] == L'.') full_ns[ni] = L'/';
        swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s/%s.xaml", full_ns, ec->ctx.types[tid].short_name );
        uwp_load_res_xbf_into_bucket( ec->ctx.base_xbf_dir[0] ? ec->ctx.base_xbf_dir : NULL, xaml_name, sub_rd );
        loaded = (sub_rd->count > 0 || sub_rd->merged_cnt > 0 || sub_rd->theme_dark || sub_rd->theme_default);
    }
    if (!loaded)
    {
        swprintf( xaml_name, ARRAY_SIZE(xaml_name), L"%s.xaml", ec->ctx.types[tid].short_name );
        uwp_load_res_xbf_into_bucket( ec->ctx.base_xbf_dir[0] ? ec->ctx.base_xbf_dir : NULL, xaml_name, sub_rd );
    }
}

static void xbf_exec_apply_root_refs( struct xbf_exec_ctx *ec )
{
    const struct xbf_parsed_hdr *h = ec->hdr;
    DWORD i;
    if (ec->refs_applied || !h || !h->refs_scanned) return;
    ec->refs_applied = TRUE;
    if (ec->ctx.root_el)
    {
        for (i = 0; i < h->ref_cnt; i++)
            uwp_record_xbf_ref( ec->ctx.root_el, h->refs[i].full_name,
                                h->refs[i].short_name, h->refs[i].is_resource_path );
        if (!ec->winui3_rd_only)
        {
            struct uwp_rd_bucket *b = ec->ctx.target_rd ? ec->ctx.target_rd : uwp_rd_bucket_for_el( ec->ctx.root_el, TRUE );
            for (i = 0; i < h->res_uri_cnt; i++)
                uwp_load_res_xbf_into_bucket( NULL, h->res_uris[i], b );
        }
    }
}

static void xbf_exec_create_app_node( struct xbf_exec_ctx *ec, const WCHAR *cls_name,
                                      const WCHAR *short_name, BOOL no_provider, BOOL using_ns,
                                      struct uwp_element **out_el, IInspectable **out_inst )
{
    struct uwp_element *el = NULL;
    IInspectable *inst = NULL;
    HRESULT phr;

    if (ec->ctx.root_el) uwp_mark_xbf_ref_instantiated( ec->ctx.root_el, cls_name );
    if (no_provider)
    {
        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        *out_el = uwp_elem_from_any( inst );
        *out_inst = inst;
        return;
    }

    phr = uwp_create_instance_from_provider_el( cls_name, (void **)&inst, &el );
    if (el)
    {
        if (inst && !el->provider_inst)
        {
            el->provider_inst = inst;
            uwp_elem_register_alias( el, inst );
        }
        else if (!inst) inst = el->outer ? el->outer : &el->IInspectable_iface;
        lstrcpynW( el->class_name, cls_name, ARRAY_SIZE(el->class_name) );
        {
            enum uwp_elem_kind new_kind = classify_uwp_class( el->class_name );
            if (new_kind != UWP_KIND_GENERIC || el->kind == UWP_KIND_GENERIC)
                el->kind = new_kind;
        }
        uwp_elem_sync_class_flags( el );
    }
    if (FAILED(phr) || !inst)
    {
        if (!inst) uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        if (!el) el = uwp_elem_from_any( inst );
        if (el && !el->xbf_parsed && !el->children_coll && !el->content &&
            using_ns && short_name[0])
            uwp_populate_element_from_pri_xbf( el, short_name );
    }
    *out_el = el;
    *out_inst = inst;
}

static void xbf_exec_record_active_trigger( struct xbf_exec_ctx *ec, struct uwp_element *trig )
{
    if (!ec || !trig || !trig->state_trigger_active) return;
    if (!ec->active_trigs)
    {
        ec->active_trigs = ec->active_trigs_stk;
        ec->active_trig_cap = ARRAY_SIZE(ec->active_trigs_stk);
        ec->active_trigs_heap = FALSE;
    }
    if (ec->active_trig_cnt >= ec->active_trig_cap)
    {
        int ncap = ec->active_trig_cap ? ec->active_trig_cap * 2 : 128;
        struct uwp_element **na;
        if (!ec->active_trigs_heap)
        {
            na = malloc( (size_t)ncap * sizeof(*na) );
            if (!na) return;
            memcpy( na, ec->active_trigs, (size_t)ec->active_trig_cnt * sizeof(*na) );
            ec->active_trigs_heap = TRUE;
        }
        else
        {
            na = realloc( ec->active_trigs, (size_t)ncap * sizeof(*na) );
            if (!na) return;
        }
        ec->active_trigs = na;
        ec->active_trig_cap = ncap;
    }
    ec->active_trigs[ec->active_trig_cnt++] = trig;
}

static void xbf_exec_queue_deferred_res( struct xbf_exec_ctx *ec, struct uwp_element *el,
                                         IInspectable *raw, WORD pid, int vt, double num,
                                         const double vec4[4], const WCHAR *sval, BOOL is_res_ref )
{
    struct xbf_deferred_res *dr;
    if (!ec->def_res)
    {
        ec->def_res = ec->def_res_stk;
        ec->def_res_cap = ARRAY_SIZE(ec->def_res_stk);
        ec->def_res_heap = FALSE;
    }
    if (ec->def_res_cnt >= ec->def_res_cap)
    {
        int ncap = ec->def_res_cap ? ec->def_res_cap * 2 : 128;
        struct xbf_deferred_res *nd;
        if (!ec->def_res_heap)
        {
            nd = malloc( (size_t)ncap * sizeof(*nd) );
            if (!nd) return;
            memcpy( nd, ec->def_res, (size_t)ec->def_res_cnt * sizeof(*nd) );
            ec->def_res_heap = TRUE;
        }
        else
        {
            nd = realloc( ec->def_res, (size_t)ncap * sizeof(*nd) );
            if (!nd) return;
        }
        ec->def_res = nd;
        ec->def_res_cap = ncap;
    }
    dr = &ec->def_res[ec->def_res_cnt++];
    dr->el = el;
    dr->raw = raw;
    dr->pid = pid;
    dr->vt = vt;
    dr->num = num;
    if (vec4) memcpy( dr->vec4, vec4, sizeof(dr->vec4) );
    else memset( dr->vec4, 0, sizeof(dr->vec4) );
    lstrcpynW( dr->sval, sval ? sval : L"", ARRAY_SIZE(dr->sval) );
    dr->is_res_ref = is_res_ref;
}

static void xbf_exec_substream( struct xbf_exec_ctx *ec, int sub_idx, BOOL has_initial_root,
                                const struct xbf_rd_table *rd_tbl, struct uwp_element *active_rd_el,
                                BOOL vsm_collect );

static BOOL xbf_exec_push_object( struct xbf_sub_state *st, BYTE op, DWORD cur_rel_off )
{
    struct xbf_exec_ctx *ec = st->ec;
    struct xbf_ctx *ctx = st->ctx;
    WORD tid;
    const WCHAR *cls_name = L"Windows.UI.Xaml.Controls.ContentControl";
    const WCHAR *short_n = L"";
    struct xbf_stack_val sv = {0};
    int vt_init = 0;
    double num_init = 0, vec4_init[4] = {0};
    WCHAR sbuf_init[256] = {0};
    WCHAR winui_buf[160];
    BOOL is_rd_type = FALSE;
    BOOL is_builtin = FALSE;
    BOOL is_app_type = FALSE;
    BOOL no_provider = FALSE;
    BOOL using_ns = FALSE;
    BOOL is_lightweight = FALSE;
    BOOL reuse_active_rd;
    BOOL replace_slot0;
    struct uwp_element *el = NULL;
    IInspectable *inst = NULL;

    sv.opacity = 1.0;
    if (st->p + 2 > st->end_p) return FALSE;
    tid = *(const WORD *)(ctx->nodes + st->p);
    st->p += 2;
    if ((op == 0x15 || op == 0x16) &&
        !xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt_init, &num_init,
                         sbuf_init, ARRAY_SIZE(sbuf_init), vec4_init ))
        return FALSE;

    TRACE( "XBF exec push op=%#x tid=%#x cls=%s at %lu/%lu (sub %d)\n", op, tid,
           (tid < ctx->type_cnt) ? debugstr_w(ctx->types[tid].full_name) : "(btype)",
           (unsigned long)cur_rel_off, (unsigned long)st->end_p, st->sub_idx );

    if (tid == XBF_TYPE_RESOURCE_DICTIONARY) is_rd_type = TRUE;
    else if (!(tid & 0x8000) && tid < ctx->type_cnt &&
             (wcsstr( ctx->types[tid].short_name, L"ResourceDictionary" ) ||
              !wcscmp( ctx->types[tid].short_name, L"XamlControlsResources" ) ||
              (st->prop_sp > 0 && st->prop_stack[st->prop_sp - 1] == XBF_PROP_MERGED_DICTIONARIES)))
        is_rd_type = TRUE;

    xbf_exec_init_sv_kind( ctx, op, tid, vt_init, num_init, sbuf_init, vec4_init, &sv );

    if ((st->sub_idx == 0 || st->vsm_collect) && !ctx->rd_only && ctx->root_el)
    {
        if (tid == XBF_TYPE_VISUAL_STATE_GROUP)
            uwp_vsm_add_group( ctx->root_el, NULL );
        else if (tid == XBF_TYPE_VISUAL_STATE && ctx->root_el->vsm_groups && ctx->root_el->vsm_group_cnt > 0)
            uwp_vsm_add_state( &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1], NULL );
    }

    replace_slot0 = (op == 0x17 && st->base_sp == 1 && st->obj_sp >= 1);
    if (!replace_slot0 && !xbf_sub_ensure_val_cap( st, st->obj_sp + 1 ))
    {
        free( sv.setters );
        return FALSE;
    }

    reuse_active_rd = (st->rd_tbl != NULL && st->obj_sp == 0 && is_rd_type && st->active_rd_el != NULL);
    if (tid & 0x8000)
    {
        cls_name = uwp_btype_to_class_name( tid );
        if (!ctx->is_uwp_conn && !wcsncmp( cls_name, L"Windows.UI.Xaml.", 16 ))
        {
            swprintf( winui_buf, ARRAY_SIZE(winui_buf), L"Microsoft.UI.Xaml.%s", cls_name + 16 );
            cls_name = winui_buf;
        }
        is_builtin = TRUE;
    }
    else if (tid < ctx->type_cnt)
    {
        cls_name = ctx->types[tid].full_name;
        short_n = ctx->types[tid].short_name;
        is_app_type = TRUE;
        if (!wcsncmp( ctx->types[tid].xmlns, L"using:", 6 )) using_ns = TRUE;
        if (is_rd_type) no_provider = TRUE;
    }

    if (op == 0x17 && st->sub_idx == 0 && !ctx->rd_only && !ec->refs_applied)
    {
        if (!ctx->is_uwp_conn && ctx->connector && !ctx->target_rd && is_rd_type)
        {
            struct uwp_rd_bucket *comp_rd = ctx->root_el
                ? uwp_rd_bucket_for_el( ctx->root_el, TRUE )
                : get_rd_bucket( ctx->root_comp, TRUE );
            DWORD ri;
            TRACE( "xbf_exec: loading WinUI 3 ResourceDictionary root into bucket %p\n", comp_rd );
            ctx->target_rd = comp_rd;
            ec->winui3_rd_only = TRUE;
            ec->reuse_root = FALSE;
            if (comp_rd && ec->hdr->refs_scanned)
            {
                const char *bdir = ctx->base_xbf_dir[0] ? ctx->base_xbf_dir : NULL;
                for (ri = 0; ri < ec->hdr->res_uri_cnt; ri++)
                    uwp_load_res_xbf_into_bucket( bdir, ec->hdr->res_uris[ri], comp_rd );
            }
        }
        xbf_exec_apply_root_refs( ec );
    }

    if (reuse_active_rd)
    {
        el = st->active_rd_el;
        inst = el->outer ? el->outer : &el->IInspectable_iface;
    }
    else if ((ctx->rd_only || st->rd_tbl) && !is_rd_type && op != 0x17 &&
             (sv.kind == UWP_RD_BRUSH || sv.kind == UWP_RD_COLOR ||
              tid == XBF_TYPE_STYLE || tid == XBF_TYPE_SETTER_A || tid == XBF_TYPE_SETTER_B ||
              tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_INT32 || tid == XBF_TYPE_STRING ||
              tid == XBF_TYPE_THICKNESS || tid == XBF_TYPE_CORNER_RADIUS || tid == XBF_TYPE_FONT_FAMILY ||
              tid == XBF_TYPE_COLUMN_DEFINITION || tid == XBF_TYPE_GRID_LENGTH))
    {
        is_lightweight = TRUE;
    }
    else if (ec->winui3_rd_only)
    {
        if (is_rd_type)
        {
            uwp_element_create( L"Windows.UI.Xaml.ResourceDictionary", NULL, NULL, (void **)&inst );
            el = uwp_elem_from_any( inst );
        }
    }
    else if (replace_slot0 && ec->reuse_root)
    {
        el = ctx->root_el;
        inst = ctx->root_comp ? ctx->root_comp : (el ? (el->outer ? el->outer : &el->IInspectable_iface) : NULL);
        if (is_rd_type && ctx->target_rd && el)
        {
            el->rd_bucket = ctx->target_rd;
            ctx->target_rd->owner_el = el;
        }
    }
    else if (!ctx->is_uwp_conn && ctx->connector)
    {
        if (is_builtin)
            element_create_typed( cls_name, (void **)&inst );
        else if (is_app_type)
        {
            if (no_provider ||
                !wcsncmp( cls_name, L"Microsoft.UI.Xaml.", 18 ) ||
                !wcsncmp( cls_name, L"Windows.UI.Xaml.", 16 ))
                element_create_typed( cls_name, (void **)&inst );
            else if (FAILED(uwp_create_instance_from_provider( cls_name, (void **)&inst )) || !inst)
                element_create_typed( cls_name, (void **)&inst );
        }
        else
            element_create_typed( cls_name, (void **)&inst );
    }
    else if (is_builtin)
    {
        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        el = uwp_elem_from_any( inst );
        if (el && tid == XBF_TYPE_DEFERRED_ELEMENT) el->kind = UWP_KIND_DEFERRED;
    }
    else if (is_app_type)
    {
        xbf_exec_create_app_node( ec, cls_name, short_n, no_provider, using_ns, &el, &inst );
    }
    else
    {
        uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
        el = uwp_elem_from_any( inst );
    }

    if (el && !reuse_active_rd)
    {
        if (ctx->root_el) el->owner_component = ctx->root_el;
        if (sv.kind == UWP_RD_BRUSH && sv.has_argb)
        {
            struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( el );
            if (sa) { sa->brush_argb = sv.argb; sa->has_brush_argb = TRUE; }
        }
        if (is_rd_type)
        {
            el->kind = UWP_KIND_RESOURCE_DICT;
            if ((op == 0x17 || replace_slot0) && ctx->target_rd)
            {
                el->rd_bucket = ctx->target_rd;
                ctx->target_rd->owner_el = el;
            }
            else if (!(replace_slot0 && ec->reuse_root))
                xbf_exec_load_custom_rd_xbf( ec, tid, uwp_rd_bucket_for_el( el, TRUE ) );
        }
    }

    sv.el = el;
    sv.raw = inst;
    if (ec->out_created && !*ec->out_created && el && !is_lightweight)
        *ec->out_created = el;

    if (replace_slot0)
    {
        free( st->val_stack[0].setters );
        st->val_stack[0] = sv;
        st->start_off_stack[0] = cur_rel_off;
        st->is_rd_stack[0] = is_rd_type;
    }
    else
    {
        st->val_stack[st->obj_sp] = sv;
        st->start_off_stack[st->obj_sp] = cur_rel_off;
        st->is_rd_stack[st->obj_sp] = is_rd_type;
        st->obj_sp++;
    }
    return TRUE;
}

static BOOL xbf_exec_set_prop( struct xbf_sub_state *st, BYTE op )
{
    struct xbf_exec_ctx *ec = st->ec;
    struct xbf_ctx *ctx = st->ctx;
    WORD pid;
    int vt = 0;
    double num = 0, vec4[4] = {0};
    WCHAR sbuf[256] = {0};
    BOOL is_res_ref = (op == 0x1e || op == 0x24);
    struct xbf_stack_val *top_sv;

    if (st->p + 2 > st->end_p) return FALSE;
    pid = *(const WORD *)(ctx->nodes + st->p);
    st->p += 2;
    if (!xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
        return FALSE;
    if (st->obj_sp <= 0) return TRUE;

    top_sv = &st->val_stack[st->obj_sp - 1];
    if (!top_sv->el && !top_sv->raw)
    {
        xbf_apply_property( ctx, NULL, top_sv, pid, vt, num, sbuf, vec4, is_res_ref );
    }
    else if (is_res_ref && sbuf[0])
    {
        struct uwp_rd_entry *re = NULL;
        struct uwp_rd_bucket *found_b = NULL;
        struct uwp_rd_bucket *hint_rd = ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd;
        if (!uwp_rd_find_entry( hint_rd, top_sv->el, sbuf, 0, &re, &found_b ) || !re)
        {
            xbf_exec_queue_deferred_res( ec, top_sv->el, top_sv->raw, pid, vt, num, vec4, sbuf, TRUE );
        }
        else
        {
            ctx->res_cache_key = sbuf;
            ctx->res_cache_start_b = hint_rd;
            ctx->res_cache_scope_el = top_sv->el;
            ctx->res_cache_entry = re;
            ctx->res_cache_bucket = found_b;
            ctx->res_cache_valid = TRUE;
            xbf_apply_property( ctx, top_sv->el, top_sv, pid, vt, num, sbuf, vec4, TRUE );
            ctx->res_cache_valid = FALSE;
        }
    }
    else
    {
        xbf_apply_property( ctx, top_sv->el, top_sv, pid, vt, num, sbuf, vec4, is_res_ref );
    }

    if ((st->sub_idx == 0 || st->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
    {
        struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
        struct uwp_vsm_state *vst = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
        if (!vst) return TRUE;
        if (top_sv->tid == XBF_TYPE_ADAPTIVE_TRIGGER && (pid == XBF_PROP_MIN_WINDOW_HEIGHT || pid == XBF_PROP_MIN_WINDOW_WIDTH))
        {
            double trig_val = num;
            if ((is_res_ref || vt == 0x05 || vt == 0x09) && sbuf[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx->target_rd, ctx->root_el, sbuf, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    trig_val = de->dval;
                else
                    trig_val = wcstod( sbuf, NULL );
            }
            if (pid == XBF_PROP_MIN_WINDOW_HEIGHT) vst->min_window_height = trig_val;
            else vst->min_window_width = trig_val;
            vst->has_trigger = TRUE;
        }
        else if (top_sv->tid == XBF_TYPE_SETTER_A || top_sv->tid == XBF_TYPE_SETTER_B)
        {
            if (pid == XBF_PROP_SETTER_TARGET && sbuf[0])
                vsm_split_setter_target( sbuf, top_sv->vsm_target, ARRAY_SIZE(top_sv->vsm_target),
                                         top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
            else if (pid == XBF_PROP_SETTER_VALUE)
            {
                if (vt == 0x07 && !sbuf[0])
                {
                    int gl_type = (int)vec4[0];
                    if (gl_type == 0) lstrcpynW( top_sv->vsm_val_str, L"Auto", ARRAY_SIZE(top_sv->vsm_val_str) );
                    else if (gl_type == 2) swprintf( top_sv->vsm_val_str, ARRAY_SIZE(top_sv->vsm_val_str), L"%.6g*", vec4[1] );
                    else top_sv->vsm_val_str[0] = 0;
                }
                else
                    lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                top_sv->vsm_vt = vt; top_sv->vsm_num = num;
                top_sv->vsm_is_res_ref = is_res_ref; top_sv->vsm_has_val = TRUE;
            }
        }
        else if (top_sv->tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES)
        {
            if (pid == XBF_PROP_STORYBOARD_TARGET_NAME && sbuf[0])
                lstrcpynW( top_sv->vsm_target, sbuf, ARRAY_SIZE(top_sv->vsm_target) );
            else if (pid == XBF_PROP_STORYBOARD_TARGET_PROPERTY && sbuf[0])
                vsm_clean_prop_name( sbuf, top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
        }
        else if (top_sv->tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && pid == XBF_PROP_KEYFRAME_VALUE)
        {
            if (vt == 0x07 && !sbuf[0])
            {
                int gl_type = (int)vec4[0];
                if (gl_type == 0) lstrcpynW( top_sv->vsm_val_str, L"Auto", ARRAY_SIZE(top_sv->vsm_val_str) );
                else if (gl_type == 2) swprintf( top_sv->vsm_val_str, ARRAY_SIZE(top_sv->vsm_val_str), L"%.6g*", vec4[1] );
                else top_sv->vsm_val_str[0] = 0;
            }
            else
                lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
            top_sv->vsm_vt = vt; top_sv->vsm_num = num;
            top_sv->vsm_is_res_ref = is_res_ref; top_sv->vsm_has_val = TRUE;
        }
        else if (top_sv->tid == XBF_TYPE_DOUBLE_ANIMATION || top_sv->tid == XBF_TYPE_DOUBLE_ANIMATION_B)
        {
            if (pid == XBF_PROP_STORYBOARD_TARGET_NAME && sbuf[0])
                lstrcpynW( top_sv->vsm_target, sbuf, ARRAY_SIZE(top_sv->vsm_target) );
            else if (pid == XBF_PROP_STORYBOARD_TARGET_PROPERTY && sbuf[0])
                vsm_clean_prop_name( sbuf, top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
            else if (pid == XBF_PROP_DOUBLE_ANIMATION_TO)
            {
                lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                top_sv->vsm_vt = vt; top_sv->vsm_num = num;
                top_sv->vsm_is_res_ref = is_res_ref; top_sv->vsm_has_val = TRUE;
            }
        }
    }
    return TRUE;
}

static void xbf_exec_set_xname( struct xbf_ctx *ctx, struct uwp_element *el, const WCHAR *name )
{
    if (!el || !name || !name[0]) return;
    lstrcpynW( el->name, name, ARRAY_SIZE(el->name) );
    if (ctx->root_el && xaml_vec_grow( (void **)&ctx->root_el->named_elems, &ctx->root_el->named_cap,
                                       ctx->root_el->named_cnt + 1, sizeof(*ctx->root_el->named_elems), 32 ))
    {
        ctx->root_el->named_elems[ctx->root_el->named_cnt++] = el;
        el->in_visual_tree = TRUE;
    }
}

static void xbf_exec_rd_store_val( struct xbf_sub_state *st, struct uwp_element *parent_el,
                                   const WCHAR *key, struct xbf_stack_val *sv )
{
    struct xbf_ctx *ctx = st->ctx;
    struct uwp_rd_bucket *dest_rd;

    if (!key || !key[0] || !sv) return;
    dest_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
        ? uwp_rd_bucket_for_el( parent_el, TRUE )
        : (ctx->target_rd ? ctx->target_rd : (parent_el ? uwp_rd_bucket_for_el( parent_el, TRUE ) : NULL));
    if (!dest_rd && ctx->root_el) dest_rd = uwp_rd_bucket_for_el( ctx->root_el, TRUE );
    if (!dest_rd) return;

    if (sv->el && sv->el->template_xbf_data && sv->el->template_xbf_size)
    {
        sv->template_xbf_data = sv->el->template_xbf_data;
        sv->template_xbf_size = sv->el->template_xbf_size;
        sv->template_sub_idx = sv->el->template_sub_idx;
        sv->template_owner_comp = sv->el->template_owner_comp;
    }
    else if (sv->template_xbf_data && sv->template_xbf_size && !sv->template_owner_comp)
    {
        sv->template_owner_comp = ctx->root_comp;
    }

    uwp_rd_store_stack_val( dest_rd, key, sv );
    if (ctx->root_el && (!parent_el || !parent_el->name[0] ||
                         (wcsicmp( parent_el->name, L"HighContrast" ) &&
                          wcsicmp( parent_el->name, L"Light" ))))
    {
        struct uwp_rd_bucket *root_rd = uwp_rd_bucket_for_el( ctx->root_el, TRUE );
        if (root_rd && root_rd != dest_rd && root_rd != ctx->target_rd)
            uwp_rd_store_stack_val( root_rd, key, sv );
    }
}

static void xbf_exec_pop_object( struct xbf_sub_state *st )
{
    struct xbf_ctx *ctx = st->ctx;
    BYTE next_op;

    if (st->obj_sp <= st->base_sp)
    {
        xbf_sub_clear_last_popped( st );
        return;
    }

    free( st->last_popped.setters );
    st->obj_sp--;
    st->last_popped = st->val_stack[st->obj_sp];
    st->last_popped_off = st->start_off_stack[st->obj_sp];
    st->last_popped_is_rd = st->is_rd_stack[st->obj_sp];
    memset( &st->val_stack[st->obj_sp], 0, sizeof(st->val_stack[st->obj_sp]) );
    st->has_last_popped = TRUE;

    if (st->last_popped.el && st->last_popped.el->state_trigger_active)
        xbf_exec_record_active_trigger( st->ec, st->last_popped.el );

    if ((st->sub_idx == 0 || st->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
    {
        struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
        struct uwp_vsm_state *vst = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
        if (vst)
        {
            if ((st->last_popped.tid == XBF_TYPE_SETTER_A || st->last_popped.tid == XBF_TYPE_SETTER_B ||
                 st->last_popped.tid == XBF_TYPE_DOUBLE_ANIMATION || st->last_popped.tid == XBF_TYPE_DOUBLE_ANIMATION_B) &&
                st->last_popped.vsm_target[0] && st->last_popped.vsm_prop[0] && st->last_popped.vsm_has_val)
            {
                uwp_vsm_state_add_setter( vst, st->last_popped.vsm_target, st->last_popped.vsm_prop,
                                          st->last_popped.vsm_val_str, st->last_popped.vsm_vt,
                                          st->last_popped.vsm_num, st->last_popped.vsm_is_res_ref );
            }
            else if (st->last_popped.tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && st->last_popped.vsm_has_val &&
                     st->obj_sp > 0 && st->val_stack[st->obj_sp - 1].tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES &&
                     st->val_stack[st->obj_sp - 1].vsm_target[0] && st->val_stack[st->obj_sp - 1].vsm_prop[0])
            {
                uwp_vsm_state_add_setter( vst, st->val_stack[st->obj_sp - 1].vsm_target,
                                          st->val_stack[st->obj_sp - 1].vsm_prop,
                                          st->last_popped.vsm_val_str, st->last_popped.vsm_vt,
                                          st->last_popped.vsm_num, st->last_popped.vsm_is_res_ref );
            }
            else if (st->last_popped.tid == XBF_TYPE_VISUAL_STATE && !vst->name[0])
            {
                WCHAR synth[64];
                swprintf( synth, ARRAY_SIZE(synth), L"__vsm_g%d_s%d",
                          ctx->root_el->vsm_group_cnt - 1, grp->state_cnt - 1 );
                lstrcpynW( vst->name, synth, ARRAY_SIZE(vst->name) );
                xbf_exec_set_xname( ctx, st->last_popped.el, synth );
            }
        }
    }

    next_op = (st->p < st->end_p) ? ctx->nodes[st->p] : 0xff;
    if (next_op == 0x0a || next_op == 0x07 || next_op == 0x08 || next_op == 0x09 || next_op == 0x20)
        return;

    /* Indexed ResourceDictionary entry without trailing 0x0a AddToDictionary */
    if (st->rd_tbl &&
        (st->obj_sp == st->base_sp || (st->obj_sp == st->base_sp + 1 && st->is_rd_stack[st->obj_sp - 1])))
    {
        struct uwp_element *rd_parent = (st->obj_sp > 0 && st->is_rd_stack[st->obj_sp - 1])
            ? st->val_stack[st->obj_sp - 1].el : st->active_rd_el;
        int k;
        BOOL matched_key = FALSE;
        for (k = 0; k < st->rd_tbl->count; k++)
        {
            if (st->rd_tbl->items[k].sub_off == st->last_popped_off &&
                st->rd_tbl->items[k].key_sid < ctx->str_cnt && ctx->strs[st->rd_tbl->items[k].key_sid])
            {
                xbf_exec_rd_store_val( st, rd_parent, ctx->strs[st->rd_tbl->items[k].key_sid], &st->last_popped );
                matched_key = TRUE;
            }
        }
        if (matched_key)
        {
            xbf_sub_clear_last_popped( st );
            return;
        }
    }

    /* Implicit Style (TargetType without explicit x:Key) inside a ResourceDictionary */
    if (st->has_last_popped && st->last_popped.kind == UWP_RD_STYLE && st->last_popped.target_type[0] &&
        (ctx->rd_only || (st->obj_sp > 0 && st->is_rd_stack[st->obj_sp - 1])))
    {
        struct uwp_element *rd_parent = (st->obj_sp > 0 && st->is_rd_stack[st->obj_sp - 1])
            ? st->val_stack[st->obj_sp - 1].el : st->active_rd_el;
        xbf_exec_rd_store_val( st, rd_parent, st->last_popped.target_type, &st->last_popped );
    }
}

static void xbf_exec_add_child( struct xbf_sub_state *st )
{
    struct xbf_ctx *ctx = st->ctx;
    struct xbf_stack_val *parent_sv = (st->obj_sp > 0) ? &st->val_stack[st->obj_sp - 1] : NULL;
    struct uwp_element *parent_el = parent_sv ? parent_sv->el : NULL;
    IInspectable *parent_raw = parent_sv ? parent_sv->raw : NULL;
    struct uwp_element *child_el = st->last_popped.el;
    IInspectable *child_raw = st->last_popped.raw;
    WORD pid = (st->prop_sp > 0) ? st->prop_stack[st->prop_sp - 1] : XBF_PROP_PANEL_CHILDREN;
    BOOL handled_custom = FALSE;
    void *ao = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();

    if (!st->has_last_popped || (!parent_el && !parent_raw)) return;
    if (parent_el && pid == XBF_PROP_MERGED_DICTIONARIES && child_el && child_el->kind == UWP_KIND_RESOURCE_DICT)
    {
        struct uwp_rd_bucket *parent_rd = (parent_el->kind == UWP_KIND_RESOURCE_DICT)
            ? uwp_rd_bucket_for_el( parent_el, TRUE )
            : (ctx->target_rd ? ctx->target_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
        uwp_rd_link_merged( parent_rd, uwp_rd_bucket_for_el( child_el, TRUE ) );
        return;
    }
    if (!child_raw) return;

    if (parent_el && (pid == XBF_PROP_KEYBOARD_ACCELERATORS || (child_el && (child_el->kind == UWP_KIND_ACCELERATOR ||
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
    else if (parent_el && pid == XBF_PROP_GRID_ROW_DEFINITIONS)
    {
        if (!parent_el->row_defs_coll) uwp_vec_create( parent_el, &parent_el->row_defs_coll );
        uwp_vec_Append( parent_el->row_defs_coll, child_raw );
        handled_custom = TRUE;
    }
    else if (parent_el && pid == XBF_PROP_GRID_COLUMN_DEFINITIONS)
    {
        if (!parent_el->col_defs_coll) uwp_vec_create( parent_el, &parent_el->col_defs_coll );
        uwp_vec_Append( parent_el->col_defs_coll, child_raw );
        handled_custom = TRUE;
    }
    else if (parent_el && (pid == XBF_PROP_GRADIENTBRUSH_STOPS ||
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
    else if (parent_raw && ao && !(parent_sv->tid & 0x8000) && parent_sv->tid < ctx->type_cnt &&
             ctx->types[parent_sv->tid].full_name[0] &&
             wcsstr( ctx->types[parent_sv->tid].full_name, L"BreakpointCollection" ))
    {
        void *provider = NULL;
        const WCHAR *coll_cls = ctx->types[parent_sv->tid].full_name;
        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)ao, &IID_IXamlMetadataProvider_UWP, &provider )) && provider)
        {
            struct xaml_type *ct = (struct xaml_type *)uwp_provider_get_xaml_type_cached( (struct xaml_metadata_provider *)provider, coll_cls );
            if (ct && ct->lpVtbl->AddToVector)
            {
                HRESULT ahr = ct->lpVtbl->AddToVector( ct, parent_raw, child_raw );
                handled_custom = SUCCEEDED(ahr);
                TRACE( "XBF AddToVector(%s coll=%p item=%p) -> hr %#lx\n",
                       debugstr_w(coll_cls), parent_raw, child_raw, ahr );
            }
            if (ct) IInspectable_Release( (IInspectable *)ct );
            IInspectable_Release( (IInspectable *)provider );
        }
    }
    else if (!(pid & 0x8000) && pid < ctx->prop_cnt &&
             ctx->props[pid].owner_tid < ctx->type_cnt &&
             ctx->props[pid].name[0] &&
             wcscmp( ctx->props[pid].name, L"Children" ) &&
             (wcscmp( ctx->props[pid].name, L"Items" ) ||
              (parent_el && (parent_el->class_flags & UWP_CF_RADIO_BUTTON))) &&
             wcscmp( ctx->props[pid].name, L"MergedDictionaries" ) &&
             wcscmp( ctx->props[pid].name, L"RowDefinitions" ) &&
             wcscmp( ctx->props[pid].name, L"ColumnDefinitions" ) &&
             wcscmp( ctx->props[pid].name, L"KeyboardAccelerators" ))
    {
        const struct xbf_prop_info *pi = &ctx->props[pid];
        const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
        IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
        if (owner_cls[0] && target)
            handled_custom = uwp_add_custom_xaml_collection_item( ao, owner_cls, pi->name, target, child_raw );
        if (parent_el && (parent_el->class_flags & UWP_CF_RADIO_BUTTON) && !wcscmp( pi->name, L"Items" ))
            handled_custom = FALSE;
    }

    if (!handled_custom && parent_el &&
        (pid == XBF_PROP_TEXTBLOCK_INLINES || parent_el->kind == UWP_KIND_TEXT_BLOCK ||
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
        (pid == XBF_PROP_PANEL_CHILDREN || pid == XBF_PROP_ITEMS ||
         parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL ||
         parent_el->kind == UWP_KIND_WRAP_PANEL || parent_el->kind == UWP_KIND_FLYOUT ||
         (parent_el->class_flags & (UWP_CF_STORYBOARD | UWP_CF_RADIO_BUTTON))))
    {
        if (!parent_el->children_coll) uwp_vec_create( parent_el, &parent_el->children_coll );
        uwp_vec_Append( parent_el->children_coll, child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    if (child_el && parent_el && !child_el->parent) child_el->parent = parent_el;
    if (child_el && ctx->root_el && !child_el->owner_component) child_el->owner_component = ctx->root_el;
}

static void xbf_exec_set_obj_prop( struct xbf_sub_state *st, WORD pid )
{
    struct xbf_ctx *ctx = st->ctx;
    struct xbf_stack_val *parent_sv = (st->obj_sp > 0) ? &st->val_stack[st->obj_sp - 1] : NULL;
    struct uwp_element *parent_el = parent_sv ? parent_sv->el : NULL;
    IInspectable *parent_raw = parent_sv ? parent_sv->raw : NULL;
    struct xbf_stack_val *child_sv = &st->last_popped;
    struct uwp_element *child_el = child_sv->el;
    IInspectable *child_raw = child_sv->raw;
    BOOL is_dict = (child_el && child_el->kind == UWP_KIND_RESOURCE_DICT);
    struct uwp_rd_bucket *child_rd = child_el ? uwp_rd_bucket_for_el( child_el, FALSE ) : NULL;
    BOOL handled_custom_obj = FALSE, is_tpl_sel = FALSE;

    if (!st->has_last_popped) return;
    if (is_dict || (child_rd && (pid == XBF_PROP_FRAMEWORKELEMENT_RESOURCES ||
                                 pid == XBF_PROP_MERGED_DICTIONARIES ||
                                 pid == XBF_PROP_THEME_DICTIONARIES)))
    {
        struct uwp_rd_bucket *dest_b = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
            ? uwp_rd_bucket_for_el( parent_el, TRUE ) : ctx->target_rd;
        if (!child_rd && child_el) child_rd = uwp_rd_bucket_for_el( child_el, TRUE );
        if (dest_b && child_rd && dest_b != child_rd)
        {
            if (pid == XBF_PROP_THEME_DICTIONARIES)
                uwp_rd_link_theme( dest_b, (child_el && child_el->name[0]) ? child_el->name : L"Default", child_rd );
            else
                uwp_rd_link_merged( dest_b, child_rd );
        }
        return;
    }
    if (!parent_el && !parent_raw) return;

    if (child_raw && !(pid & 0x8000) && pid < ctx->prop_cnt &&
        ctx->props[pid].owner_tid < ctx->type_cnt &&
        ctx->props[pid].name[0])
    {
        const struct xbf_prop_info *pi = &ctx->props[pid];
        const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
        IInspectable *target = parent_raw ? parent_raw : (parent_el ? (parent_el->outer ? parent_el->outer : &parent_el->IInspectable_iface) : NULL);
        BOOL is_uwp_ui_owner = !wcsncmp( owner_cls, L"Windows.UI.Xaml.", 16 );
        if (owner_cls[0] && target &&
            wcscmp( pi->name, L"Style" ) && wcscmp( pi->name, L"Resources" ) &&
            wcscmp( pi->name, L"MergedDictionaries" ) && wcscmp( pi->name, L"ThemeDictionaries" ) &&
            wcscmp( pi->name, L"ContextFlyout" ) &&
            (!is_uwp_ui_owner ||
             (wcscmp( pi->name, L"Background" ) && wcscmp( pi->name, L"Foreground" ) &&
              wcscmp( pi->name, L"BorderBrush" ))))
        {
            if (parent_el && child_el && (child_el->class_flags2 & UWP_CF2_BINDING))
            {
                uwp_add_xaml_binding( parent_el, owner_cls, pi->name, child_el );
                handled_custom_obj = TRUE;
            }
            else if (parent_el && (parent_el->class_flags2 & UWP_CF2_BINDING) &&
                     !wcscmp( pi->name, L"Converter" ))
            {
                struct uwp_style_anim_ext *bsa = uwp_elem_ensure_style_anim( parent_el );
                if (bsa) uwp_replace_iface( &bsa->converter, child_raw );
                handled_custom_obj = TRUE;
            }
            else if (parent_el && (parent_el->class_flags2 & UWP_CF2_BINDING) &&
                     !wcscmp( pi->name, L"ConverterParameter" ))
            {
                struct uwp_style_anim_ext *bsa = uwp_elem_ensure_style_anim( parent_el );
                if (bsa) uwp_replace_iface( &bsa->converter_param, child_raw );
                handled_custom_obj = TRUE;
            }
            else
            {
                IInspectable *set_val = child_raw;
                IInspectable *provided = NULL;
                void *me_ovr = NULL;
                if (SUCCEEDED(IInspectable_QueryInterface( child_raw, &IID_IMarkupExtensionOverrides_UWP, &me_ovr )) && me_ovr)
                {
                    if (!child_el || !((char *)me_ovr >= (char *)child_el && (char *)me_ovr < (char *)(child_el + 1)))
                    {
                        const struct abi_markup_ext_overrides *amo = (const struct abi_markup_ext_overrides *)me_ovr;
                        HRESULT phr = E_FAIL;
                        if (amo->lpVtbl->ProvideValue)
                        {
                            __TRY { phr = amo->lpVtbl->ProvideValue( me_ovr, &provided ); }
                            __EXCEPT_ALL { phr = E_FAIL; provided = NULL; }
                            __ENDTRY;
                        }
                        if (SUCCEEDED(phr) && provided)
                            set_val = provided;
                    }
                    IUnknown_Release( (IUnknown *)me_ovr );
                }
                if (uwp_set_custom_xaml_member_obj( ctx->app_outer ? ctx->app_outer : uwp_get_app_outer(), owner_cls, pi->name, target, set_val ))
                {
                    if (parent_el && !wcscmp( pi->name, L"Icon" ))
                    {
                        struct uwp_nav_ext *pne = uwp_elem_ensure_nav( parent_el );
                        if (pne && pne->nav_icon != set_val)
                        {
                            if (pne->nav_icon) IInspectable_Release( pne->nav_icon );
                            pne->nav_icon = set_val;
                            IInspectable_AddRef( set_val );
                        }
                    }
                    else if (parent_el && !wcscmp( pi->name, L"Header" ) &&
                             child_el && child_el != parent_el)
                    {
                        if (parent_el->pane != set_val)
                        {
                            if (parent_el->pane) IInspectable_Release( parent_el->pane );
                            parent_el->pane = set_val;
                            IInspectable_AddRef( set_val );
                        }
                        child_el->parent = parent_el;
                        child_el->in_visual_tree = TRUE;
                    }
                    if (wcscmp( pi->name, L"Content" ) && wcscmp( pi->name, L"Child" ))
                        handled_custom_obj = TRUE;
                }
                if (provided) IInspectable_Release( provided );
            }
        }
    }
    if (handled_custom_obj || !parent_el) return;

    if (child_sv->kind == UWP_RD_BRUSH || child_sv->kind == UWP_RD_COLOR)
    {
        UINT32 argb = 0;
        BOOL got = FALSE;
        const WCHAR *sval = child_sv->str_val;
        double opacity = child_sv->opacity;
        if (child_raw && uwp_is_bg_pid( pid ))
        {
            IInspectable **slot = (pid == XBF_PROP_SHAPE_FILL) ? &parent_el->fill_brush : &parent_el->bg_brush;
            if (*slot != child_raw)
            {
                if (*slot) IInspectable_Release( *slot );
                *slot = child_raw;
                IInspectable_AddRef( child_raw );
            }
        }
        if (child_sv->has_argb) { argb = apply_opacity_to_argb( child_sv->argb, opacity ); got = TRUE; }
        else if (sval[0] && uwp_resolve_brush_or_color( ctx->target_rd, parent_el, sval, 0, &argb ))
        {
            argb = apply_opacity_to_argb( argb, opacity );
            got = TRUE;
        }
        else if (child_el && uwp_elem_has_brush_argb( child_el ))
        {
            argb = apply_opacity_to_argb( uwp_elem_brush_argb( child_el ), uwp_elem_brush_opacity( child_el ) );
            got = TRUE;
        }
        if (got)
        {
            UINT32 lit_c = 0;
            BOOL is_res_key = !child_sv->has_argb && sval[0] && !uwp_parse_color_string( sval, &lit_c );
            if (uwp_is_bg_pid( pid ))
            {
                uwp_el_set_bg_argb( parent_el, argb );
                if (is_res_key)
                {
                    struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( parent_el );
                    if (sa) lstrcpynW( sa->bg_theme_key, sval, ARRAY_SIZE(sa->bg_theme_key) );
                }
                if (!ctx->in_style_apply) parent_el->local_prop_mask |= UWP_LP_BACKGROUND;
            }
            else if (uwp_is_fg_pid( pid ))
            {
                uwp_el_set_fg_argb( parent_el, argb );
                if (is_res_key)
                {
                    struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( parent_el );
                    if (sa) lstrcpynW( sa->fg_theme_key, sval, ARRAY_SIZE(sa->fg_theme_key) );
                }
                if (!ctx->in_style_apply) parent_el->local_prop_mask |= UWP_LP_FOREGROUND;
            }
            else if (uwp_is_border_pid( pid ))
            {
                uwp_el_set_border_argb( parent_el, argb );
                if (is_res_key)
                {
                    struct uwp_style_anim_ext *sa = uwp_elem_ensure_style_anim( parent_el );
                    if (sa) lstrcpynW( sa->border_theme_key, sval, ARRAY_SIZE(sa->border_theme_key) );
                }
                if (!ctx->in_style_apply) parent_el->local_prop_mask |= UWP_LP_BORDER_BRUSH;
            }
        }
    }
    else if (child_sv->kind == UWP_RD_STYLE && pid == XBF_PROP_STYLE)
    {
        const WCHAR *based_on = child_sv->based_on;
        BOOL prev_in_style = ctx->in_style_apply;
        int si;
        ctx->in_style_apply = TRUE;
        if (based_on[0]) uwp_apply_style_by_name( ctx, parent_el, based_on );
        for (si = 0; si < child_sv->setter_cnt && child_sv->setters; si++)
        {
            const struct uwp_style_setter *sst = &child_sv->setters[si];
            xbf_apply_property( ctx, parent_el, NULL, sst->pid, sst->vt, sst->num, sst->str, sst->vec4, sst->is_res_ref );
        }
        ctx->in_style_apply = prev_in_style;
    }
    else if (pid == 0x000c && uwp_has_text( child_el ))
    {
        if (!uwp_has_text( parent_el )) uwp_elem_set_text( parent_el, uwp_elem_text( child_el ) );
    }
    else if (child_raw && (pid == XBF_PROP_DATACONTEXT ||
             (!(pid & 0x8000) && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"DataContext" ))))
    {
        uwp_fe_put_DataContext( &parent_el->IInspectable_iface, child_raw );
    }
    else if (child_raw && (pid == XBF_PROP_CONTEXT_FLYOUT ||
             (!(pid & 0x8000) && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"ContextFlyout" ))))
    {
        if (parent_el->context_flyout) IInspectable_Release( parent_el->context_flyout );
        parent_el->context_flyout = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    else if (child_raw && (pid == XBF_PROP_BUTTON_FLYOUT ||
             (!(pid & 0x8000) && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"Flyout" )) ||
             (child_el && child_el->kind == UWP_KIND_FLYOUT)))
    {
        if (parent_el->flyout) IInspectable_Release( parent_el->flyout );
        parent_el->flyout = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    else if (child_raw && child_sv->kind == UWP_RD_OBJ && !(pid & 0x8000) && pid < ctx->prop_cnt &&
             !wcscmp( ctx->props[pid].name, L"Pane" ))
    {
        if (parent_el->pane) IInspectable_Release( parent_el->pane );
        parent_el->pane = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
    else if (child_raw && child_sv->kind == UWP_RD_OBJ &&
             (pid == XBF_PROP_IMAGE_SOURCE ||
              (!(pid & 0x8000) && pid < ctx->prop_cnt &&
               (!wcscmp( ctx->props[pid].name, L"Source" ) ||
                !wcscmp( ctx->props[pid].name, L"ImageSource" )))))
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
    else if (child_raw && (xbf_prop_is_item_template( pid, ctx, &is_tpl_sel ) ||
             (child_el && (child_el->class_flags & UWP_CF_DATA_TEMPLATE) &&
              pid != XBF_PROP_CONTENT && pid != XBF_PROP_USERCONTROL_CONTENT)))
    {
        IInspectable **slot = is_tpl_sel ? &parent_el->item_template_selector : &parent_el->item_template;
        if (*slot) IInspectable_Release( *slot );
        *slot = child_raw;
        IInspectable_AddRef( child_raw );
        if (parent_el->items_source && !uwp_is_combobox( parent_el ))
            uwp_items_ctrl_materialize_source( parent_el, parent_el->items_source );
    }
    else if (child_raw && child_sv->kind == UWP_RD_OBJ &&
             (parent_el->class_flags2 & UWP_CF2_BINDING) &&
             (pid == XBF_PROP_BINDING_CONVERTER || pid == XBF_PROP_BINDING_CONVERTER_PARAMETER))
    {
        struct uwp_style_anim_ext *bsa = uwp_elem_ensure_style_anim( parent_el );
        if (bsa)
        {
            if (pid == XBF_PROP_BINDING_CONVERTER)
                uwp_replace_iface( &bsa->converter, child_raw );
            else
                uwp_replace_iface( &bsa->converter_param, child_raw );
        }
    }
    else if (child_raw && child_sv->kind == UWP_RD_OBJ &&
             (!child_el || (child_el->kind != UWP_KIND_STYLE && child_el->kind != UWP_KIND_BRUSH &&
                            child_el->kind != UWP_KIND_COLOR && child_el->kind != UWP_KIND_FLYOUT &&
                            !(child_el->class_flags & UWP_CF_DATA_TEMPLATE))) &&
             (pid == XBF_PROP_CONTENT || pid == XBF_PROP_USERCONTROL_CONTENT ||
              pid == XBF_PROP_FLYOUT_CONTENT ||
              pid == XBF_PROP_BORDER_CHILD || pid == XBF_PROP_RENDER_TRANSFORM_ORIGIN ||
              pid == XBF_PROP_POPUP_CHILD ||
              (!parent_el->content && pid != XBF_PROP_UIELEMENT_RENDER_TRANSFORM &&
               pid != XBF_PROP_FRAMEWORKELEMENT_RESOURCES && pid != XBF_PROP_AUTOMATION_NAME && pid != 0x000c)))
    {
        if (parent_el->content) IInspectable_Release( parent_el->content );
        parent_el->content = child_raw;
        IInspectable_AddRef( child_raw );
        if (child_el) child_el->parent = parent_el;
    }
}

static void xbf_exec_provide_value( struct xbf_sub_state *st, WORD pid )
{
    struct xbf_ctx *ctx = st->ctx;
    struct uwp_element *target_el = (st->obj_sp > 0) ? st->val_stack[st->obj_sp - 1].el : NULL;
    IInspectable *target_raw = (st->obj_sp > 0) ? st->val_stack[st->obj_sp - 1].raw : NULL;
    struct uwp_element *val_el = st->last_popped.el;
    IInspectable *val_raw = st->last_popped.raw;
    WCHAR resolved[256] = {0};
    WCHAR raw_key_buf[256] = {0};

    if (!st->has_last_popped || (!target_el && !target_raw) || (!val_el && !val_raw)) return;
    if (val_el && (val_el->class_flags2 & UWP_CF2_NULL_EXT))
    {
        if (target_el && uwp_is_fg_pid( pid ))
        {
            target_el->has_fg = TRUE;
            target_el->fg_argb = 0;
            target_el->fg_alpha = 0;
        }
        return;
    }
    if (val_raw)
    {
        void *me_ovr = NULL;
        if (SUCCEEDED(IInspectable_QueryInterface( val_raw, &IID_IMarkupExtensionOverrides_UWP, &me_ovr )) && me_ovr)
        {
            BOOL is_ext = !val_el || !((char *)me_ovr >= (char *)val_el && (char *)me_ovr < (char *)(val_el + 1));
            if (is_ext)
            {
                const struct abi_markup_ext_overrides *amo = (const struct abi_markup_ext_overrides *)me_ovr;
                IInspectable *provided = NULL;
                HRESULT phr = E_FAIL;
                if (amo->lpVtbl->ProvideValue)
                {
                    __TRY { phr = amo->lpVtbl->ProvideValue( me_ovr, &provided ); }
                    __EXCEPT_ALL { phr = E_FAIL; provided = NULL; }
                    __ENDTRY;
                }
                IUnknown_Release( (IUnknown *)me_ovr );
                if (SUCCEEDED(phr) && provided)
                {
                    const WCHAR *pname = (!(pid & 0x8000) && pid < ctx->prop_cnt) ? ctx->props[pid].name : L"";
                    if (pname[0] && !(pid & 0x8000) && ctx->props[pid].owner_tid < ctx->type_cnt)
                    {
                        const WCHAR *owner_cls = ctx->types[ctx->props[pid].owner_tid].full_name;
                        IInspectable *target = target_raw ? target_raw : (target_el->outer ? target_el->outer : &target_el->IInspectable_iface);
                        if (owner_cls[0] && target)
                            uwp_set_custom_xaml_member_obj( ctx->app_outer ? ctx->app_outer : uwp_get_app_outer(),
                                                            owner_cls, pname, target, provided );
                    }
                    IInspectable_Release( provided );
                }
                return;
            }
            IUnknown_Release( (IUnknown *)me_ovr );
        }
    }
    if (!target_el) return;
    if (val_el && !wcscmp( val_el->class_name, L"Windows.UI.Xaml.Data.Binding" ))
    {
        const WCHAR *dp_name = NULL, *owner_cls = NULL;
        switch (pid)
        {
        case XBF_PROP_ITEMS_CONTROL_ITEMS_SOURCE: dp_name = L"ItemsSource"; break;
        case XBF_PROP_CONTENT:
        case XBF_PROP_USERCONTROL_CONTENT:
        case XBF_PROP_FLYOUT_CONTENT: dp_name = L"Content"; break;
        case XBF_PROP_TEXTBLOCK_TEXT:
        case XBF_PROP_RUN_TEXT: dp_name = L"Text"; break;
        case XBF_PROP_UIELEMENT_VISIBILITY:
        case XBF_PROP_UIELEMENT_VISIBILITY_V2: dp_name = L"Visibility"; break;
        case XBF_PROP_TOGGLE_BUTTON_IS_CHECKED:
        case XBF_PROP_TOGGLE_MENU_FLYOUT_ITEM_IS_CHECKED: dp_name = L"IsChecked"; break;
        case XBF_PROP_ACCELERATOR_IS_ENABLED:
        case XBF_PROP_CONTROL_IS_ENABLED: dp_name = L"IsEnabled"; break;
        case XBF_PROP_PROGRESS_RING_IS_ACTIVE: dp_name = L"IsActive"; break;
        case XBF_PROP_SELECTOR_ITEM_IS_SELECTED:
            dp_name = (target_el->class_flags & (UWP_CF_COMBOBOX | UWP_CF_LISTVIEW | UWP_CF_GRIDVIEW | UWP_CF_FLIPVIEW | UWP_CF_PIVOT))
                ? L"SelectedItem" : L"IsSelected";
            break;
        default:
            if (pid & 0x8000)
                dp_name = xbf_bprop_to_name( pid );
            else if (pid < ctx->prop_cnt && ctx->props[pid].name[0])
                dp_name = ctx->props[pid].name;
            break;
        }
        if (!dp_name || !dp_name[0]) return;
        if (!(pid & 0x8000) && pid < ctx->prop_cnt && ctx->props[pid].owner_tid < ctx->type_cnt)
            owner_cls = ctx->types[ctx->props[pid].owner_tid].full_name;
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
        const WCHAR *res_key = NULL;
        if (val_el)
            res_key = uwp_has_text( val_el ) ? uwp_elem_text( val_el ) : val_el->name;
        else if (val_raw)
        {
            WORD vtid = st->last_popped.tid;
            const WCHAR *val_cls = (vtid & 0x8000)
                ? uwp_btype_to_class_name( vtid )
                : ((vtid < ctx->type_cnt) ? ctx->types[vtid].full_name : L"");
            void *ao = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();
            IInspectable *name_obj = NULL;
            if (ao && val_cls && val_cls[0] &&
                uwp_get_custom_xaml_member( ao, val_cls, L"Name", val_raw, &name_obj ) && name_obj)
            {
                if (uwp_unbox_string( name_obj, raw_key_buf, ARRAY_SIZE(raw_key_buf) ))
                    res_key = raw_key_buf;
                IInspectable_Release( name_obj );
            }
        }
        if (!res_key || !res_key[0]) return;
        if (val_el && (val_el->class_flags2 & UWP_CF2_TEMPLATE_BINDING))
        {
            const WCHAR *tprop = (pid & 0x8000)
                ? xbf_bprop_to_name( pid )
                : ((pid < ctx->prop_cnt) ? ctx->props[pid].name : L"");
            uwp_sa_add_tpl_binding( target_el, pid, res_key, tprop );
            return;
        }
        if ((target_el->class_flags2 & UWP_CF2_BINDING) &&
            (pid == XBF_PROP_BINDING_CONVERTER ||
             (!(pid & 0x8000) && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"Converter" ))))
        {
            struct uwp_rd_entry *ce = NULL;
            if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, target_el, res_key, 0, &ce, NULL ) &&
                ce && ce->value)
            {
                struct uwp_style_anim_ext *bsa = uwp_elem_ensure_style_anim( target_el );
                if (bsa) uwp_replace_iface( &bsa->converter, ce->value );
            }
            return;
        }
        if (uwp_pri_lookup_string( res_key, resolved, ARRAY_SIZE(resolved) ) && resolved[0])
        {
            const WCHAR *pname = (!(pid & 0x8000) && pid < ctx->prop_cnt) ? ctx->props[pid].name : L"";
            BOOL is_text_prop = (!wcscmp( pname, L"Text" ) || !wcscmp( pname, L"Content" ) ||
                                 !wcscmp( pname, L"Glyph" ) || !wcscmp( pname, L"Label" ) ||
                                 !wcscmp( pname, L"Header" ) || !wcscmp( pname, L"Title" ) ||
                                 !wcscmp( pname, L"Description" ) ||
                                 !wcscmp( pname, L"PlaceholderText" ));
            if (pid == XBF_PROP_AUTOMATION_NAME)
                uwp_elem_set_automation_name( target_el, resolved );
            else if (pid == XBF_PROP_FRAMEWORKELEMENT_TAG)
            {
                IInspectable *boxed = box_string_value( resolved );
                if (boxed)
                {
                    if (target_el->tag) IInspectable_Release( target_el->tag );
                    target_el->tag = boxed;
                    uwp_sync_all_template_presenters( target_el );
                }
            }
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
            else if (pid == XBF_PROP_CONTENT ||
                     pid == XBF_PROP_TEXTBLOCK_TEXT ||
                     pid == XBF_PROP_FONTICON_GLYPH || pid == XBF_PROP_RUN_TEXT ||
                     (pid != XBF_PROP_AUTOMATION_ID && is_text_prop && !uwp_has_text( target_el )))
                uwp_elem_set_text( target_el, resolved );

            if (pname[0] && !(pid & 0x8000) && pid < ctx->prop_cnt && ctx->props[pid].owner_tid < ctx->type_cnt)
            {
                const WCHAR *owner_cls = ctx->types[ctx->props[pid].owner_tid].full_name;
                if (owner_cls[0] && wcsncmp( owner_cls, L"Windows.UI.Xaml.", 16 ))
                {
                    IInspectable *target = target_raw
                        ? target_raw
                        : (target_el->outer ? target_el->outer : &target_el->IInspectable_iface);
                    uwp_set_custom_xaml_member( ctx->app_outer ? ctx->app_outer : uwp_get_app_outer(),
                                                owner_cls, pname, target, resolved );
                }
            }
        }
    }
}

static BOOL xbf_exec_attach_ops( struct xbf_sub_state *st, BYTE op )
{
    struct xbf_ctx *ctx = st->ctx;

    if (op == 0x0a)
    {
        int vt = 0;
        double num = 0;
        WCHAR kbuf[128] = {0};
        if (!xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt, &num, kbuf, ARRAY_SIZE(kbuf), NULL ))
            return FALSE;
        if (st->has_last_popped && kbuf[0])
        {
            WORD pid = (st->prop_sp > 0) ? st->prop_stack[st->prop_sp - 1] : 0;
            struct uwp_element *parent_el = (st->obj_sp > 0 && st->val_stack[st->obj_sp - 1].el)
                ? st->val_stack[st->obj_sp - 1].el : st->active_rd_el;
            if (pid == XBF_PROP_THEME_DICTIONARIES && st->last_popped_is_rd &&
                st->last_popped.el && st->last_popped.el->kind == UWP_KIND_RESOURCE_DICT)
            {
                struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                    ? uwp_rd_bucket_for_el( parent_el, TRUE )
                    : (ctx->target_rd ? ctx->target_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                uwp_rd_link_theme( parent_rd, kbuf, uwp_rd_bucket_for_el( st->last_popped.el, TRUE ) );
            }
            else
                xbf_exec_rd_store_val( st, parent_el, kbuf, &st->last_popped );
        }
        xbf_sub_clear_last_popped( st );
        return TRUE;
    }
    if (op == 0x08)
    {
        xbf_exec_add_child( st );
        xbf_sub_clear_last_popped( st );
        return TRUE;
    }
    if (op == 0x07)
    {
        WORD pid;
        if (st->end_p - st->p < 2) return FALSE;
        pid = *(const WORD *)(ctx->nodes + st->p);
        st->p += 2;
        xbf_exec_set_obj_prop( st, pid );
        xbf_sub_clear_last_popped( st );
        return TRUE;
    }
    if (op == 0x20)
    {
        WORD pid;
        if (st->end_p - st->p < 2) return FALSE;
        pid = *(const WORD *)(ctx->nodes + st->p);
        st->p += 2;
        if (!st->ec->winui3_rd_only)
            xbf_exec_provide_value( st, pid );
        xbf_sub_clear_last_popped( st );
        return TRUE;
    }
    if (st->has_last_popped)
    {
        const WCHAR *cls = (st->last_popped.tid & 0x8000) ? uwp_btype_to_class_name( st->last_popped.tid )
            : ((st->last_popped.tid < ctx->type_cnt) ? ctx->types[st->last_popped.tid].full_name : L"?");
        TRACE( "XBF op=%#x unhandled attach op dropping last_popped=%s (tid=%#x) next=[%02x %02x %02x %02x]\n",
               op, debugstr_w(cls), st->last_popped.tid,
               st->p < st->end_p ? ctx->nodes[st->p] : 0, st->p + 1 < st->end_p ? ctx->nodes[st->p + 1] : 0,
               st->p + 2 < st->end_p ? ctx->nodes[st->p + 2] : 0, st->p + 3 < st->end_p ? ctx->nodes[st->p + 3] : 0 );
    }
    xbf_sub_clear_last_popped( st );
    return TRUE;
}

static void xbf_exec_connect( struct xbf_exec_ctx *ec, struct uwp_element *el, IInspectable *raw, int cid )
{
    struct xbf_ctx *ctx = &ec->ctx;
    const struct abi_component_connector *acc;

    if (el) el->conn_id = cid;
    if (!ctx->connector || !raw) return;

    acc = (const struct abi_component_connector *)ctx->connector;
    TRACE( "XBF Connect(cid=%d, target=%p [%s %s])\n", cid, raw,
           el ? debugstr_w(el->class_name) : "?", el ? debugstr_w(el->name) : "" );
    acc->lpVtbl->Connect( ctx->connector, cid, raw );

    if (cid == 1 && !ctx->is_template && !ec->out_created)
    {
        void *binding = NULL;
        if (ctx->is_uwp_conn)
        {
            void *conn2 = NULL;
            if (ctx->root_comp &&
                SUCCEEDED(IInspectable_QueryInterface( ctx->root_comp, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
            {
                const struct abi_component_connector2 *acc2 = (const struct abi_component_connector2 *)conn2;
                if (acc2->lpVtbl->GetBindingConnector)
                    acc2->lpVtbl->GetBindingConnector( conn2, 1, raw, &binding );
                IUnknown_Release( (IUnknown *)conn2 );
            }
        }
        else
        {
            const struct abi_component_connector_winui *wcc = (const struct abi_component_connector_winui *)ctx->connector;
            if (wcc->lpVtbl->GetBindingConnector)
                wcc->lpVtbl->GetBindingConnector( ctx->connector, 1, raw, &binding );
        }
        if (binding)
        {
            const struct abi_component_connector *bcc = (const struct abi_component_connector *)binding;
            if (ec->root_binding) IUnknown_Release( (IUnknown *)ec->root_binding );
            ec->root_binding = binding;
            if (ctx->root_el)
            {
                if (ctx->root_el->binding_connector) IUnknown_Release( (IUnknown *)ctx->root_el->binding_connector );
                ctx->root_el->binding_connector = binding;
                IUnknown_AddRef( (IUnknown *)binding );
            }
            if (bcc->lpVtbl->Connect) bcc->lpVtbl->Connect( binding, 1, raw );
        }
        return;
    }

    if (!ctx->is_template)
    {
        void *bc = ec->root_binding ? ec->root_binding : (ctx->root_el ? ctx->root_el->binding_connector : NULL);
        if (bc)
        {
            const struct abi_component_connector *bcc = (const struct abi_component_connector *)bc;
            if (bcc->lpVtbl->Connect) bcc->lpVtbl->Connect( bc, cid, raw );
        }
    }
    else if (ec->out_created)
    {
        if (!ec->dt_binding && ctx->root_comp)
        {
            void *bc = NULL;
            if (ctx->is_uwp_conn)
            {
                void *conn2 = NULL;
                if (SUCCEEDED(IInspectable_QueryInterface( ctx->root_comp, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
                {
                    const struct abi_component_connector2 *acc2 = (const struct abi_component_connector2 *)conn2;
                    HRESULT ghr = E_NOTIMPL;
                    if (acc2->lpVtbl->GetBindingConnector)
                        ghr = acc2->lpVtbl->GetBindingConnector( conn2, cid, raw, &bc );
                    TRACE( "XBF DT GetBindingConnector(cid=%d, raw=%p) -> hr=%#lx bc=%p\n", cid, raw, ghr, bc );
                    IUnknown_Release( (IUnknown *)conn2 );
                }
                else
                    TRACE( "XBF DT QI(IComponentConnector2) failed on comp=%p\n", ctx->root_comp );
            }
            else
            {
                const struct abi_component_connector_winui *wcc = (const struct abi_component_connector_winui *)ctx->connector;
                if (wcc->lpVtbl->GetBindingConnector)
                    wcc->lpVtbl->GetBindingConnector( ctx->connector, cid, raw, &bc );
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
            if (bcc->lpVtbl->Connect) bcc->lpVtbl->Connect( ec->dt_binding, cid, raw );
        }
    }
}

/* Returns 1 = handled, 0 = break loop, -1 = not a meta op */
static int xbf_exec_meta_ops( struct xbf_sub_state *st, BYTE op, DWORD cur_rel_off )
{
    struct xbf_ctx *ctx = st->ctx;

    switch (op)
    {
    case 0x12:
        if (st->end_p - st->p < 6) return 0;
        st->p += 6;
        return 1;
    case 0x03:
    {
        DWORD plen;
        if (st->end_p - st->p < 6) return 0;
        plen = *(const DWORD *)(ctx->nodes + st->p + 2);
        if (plen > (st->end_p - (st->p + 6)) / 2) return 0;
        st->p += 6 + plen * 2;
        return 1;
    }
    case 0x0b:
    {
        DWORD slen;
        if (st->end_p - st->p < 4) return 0;
        slen = *(const DWORD *)(ctx->nodes + st->p);
        st->p += 4;
        if (slen > (st->end_p - st->p) / 2) return 0;
        if (st->obj_sp > 0 && st->val_stack[st->obj_sp - 1].el && slen > 0)
        {
            WCHAR xcls[128];
            DWORD cp = min( slen, (DWORD)(ARRAY_SIZE(xcls) - 1) );
            memcpy( xcls, ctx->nodes + st->p, cp * sizeof(WCHAR) );
            xcls[cp] = 0;
            uwp_elem_set_x_class( st->val_stack[st->obj_sp - 1].el, xcls );
        }
        st->p += slen * 2;
        return 1;
    }
    case 0x0c:
    {
        int vt = 0;
        double num = 0;
        if (!xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt, &num, NULL, 0, NULL )) return 0;
        if (!st->ec->winui3_rd_only && st->obj_sp > 0)
        {
            struct xbf_stack_val *top_sv = &st->val_stack[st->obj_sp - 1];
            xbf_exec_connect( st->ec, top_sv->el, top_sv->raw, (int)num );
        }
        return 1;
    }
    case 0x0d:
    {
        int vt = 0;
        double num = 0;
        WCHAR sbuf[128] = {0};
        if (!xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) return 0;
        if (st->obj_sp > 0 && sbuf[0])
        {
            struct xbf_stack_val *top_sv = &st->val_stack[st->obj_sp - 1];
            if (top_sv->el)
                xbf_exec_set_xname( ctx, top_sv->el, sbuf );
            if ((st->sub_idx == 0 || st->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
            {
                struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                if (top_sv->tid == XBF_TYPE_VISUAL_STATE_GROUP)
                    lstrcpynW( grp->name, sbuf, ARRAY_SIZE(grp->name) );
                else if (top_sv->tid == XBF_TYPE_VISUAL_STATE && grp->state_cnt > 0)
                    lstrcpynW( grp->states[grp->state_cnt - 1].name, sbuf, ARRAY_SIZE(grp->states[grp->state_cnt - 1].name) );
            }
        }
        return 1;
    }
    case 0x0e:
    {
        int vt = 0;
        double num = 0;
        WCHAR sbuf[128] = {0};
        if (!xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) return 0;
        if (sbuf[0] && st->obj_sp > 0)
        {
            struct xbf_stack_val *top_sv = &st->val_stack[st->obj_sp - 1];
            if (top_sv->el || top_sv->raw)
                xbf_apply_property( ctx, top_sv->el, top_sv, XBF_PROP_AUTOMATION_ID, vt, 0, sbuf, NULL, FALSE );
        }
        return 1;
    }
    case 0x13:
        if (st->p + 2 > st->end_p) return 0;
        if (xbf_sub_ensure_prop_cap( st, st->prop_sp + 1 ))
            st->prop_stack[st->prop_sp++] = *(const WORD *)(ctx->nodes + st->p);
        st->p += 2;
        return 1;
    case 0x01:
    case 0x27:
        return 1;
    case 0x26:
        if (st->p + 4 > st->end_p) return 0;
        st->p += 4;
        return 1;
    case 0x04:
    {
        int vt = 0;
        double num = 0, vec4[4] = {0};
        WCHAR sbuf[256] = {0};
        return xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ) ? 1 : 0;
    }
    case 0x11:
    {
        DWORD sub_i = 0, def_sz = 0, c1 = 0, c2 = 0;
        if (st->end_p - st->p < 2) return 0;
        st->p += 2;
        if (!xbf_read_uleb( ctx->nodes, &st->p, st->end_p, &sub_i )) return 0;
        if (!xbf_hdr_substream_bounds( st->ec->hdr, sub_i, NULL, NULL, &def_sz )) return 0;
        if (!xbf_read_uleb( ctx->nodes, &st->p, st->end_p, &c1 )) return 0;
        if (!xbf_read_uleb( ctx->nodes, &st->p, st->end_p, &c2 )) return 0;
        (void)def_sz;
        if (c1 > (st->end_p - st->p) / 2 || c2 > (st->end_p - st->p) / 2 - c1) return 0;
        st->p += (c1 + c2) * 2;
        if (st->obj_sp > 0 && (st->val_stack[st->obj_sp - 1].tid == XBF_TYPE_DATA_TEMPLATE ||
                               st->val_stack[st->obj_sp - 1].tid == XBF_TYPE_CONTROL_TEMPLATE))
        {
            struct xbf_stack_val *tsv = &st->val_stack[st->obj_sp - 1];
            tsv->template_xbf_data = ctx->xbf;
            tsv->template_xbf_size = ctx->xbf_sz;
            tsv->template_sub_idx = sub_i;
            tsv->template_owner_comp = ctx->root_comp;
            if (tsv->el)
            {
                tsv->el->template_xbf_data = ctx->xbf;
                tsv->el->template_xbf_size = ctx->xbf_sz;
                tsv->el->template_sub_idx = sub_i;
                uwp_set_template_owner( &tsv->el->template_owner_comp, ctx->root_comp );
            }
            TRACE( "XBF Template SetDeferredProperty captured substream %u\n", (unsigned)sub_i );
        }
        return 1;
    }
    case 0x02:
        if (st->prop_sp > 0) st->prop_sp--;
        return 1;
    case 0x1d:
    {
        WORD pid, t_tid;
        if (st->p + 4 > st->end_p) return 0;
        pid = *(const WORD *)(ctx->nodes + st->p);
        t_tid = *(const WORD *)(ctx->nodes + st->p + 2);
        st->p += 4;
        if (st->obj_sp > 0 && st->val_stack[st->obj_sp - 1].kind == UWP_RD_STYLE && pid == XBF_PROP_STYLE_TARGET_TYPE)
        {
            const WCHAR *tname = (t_tid & 0x8000)
                ? uwp_btype_to_class_name( t_tid )
                : ((t_tid < ctx->type_cnt) ? ctx->types[t_tid].full_name : L"");
            if (tname && tname[0])
                lstrcpynW( st->val_stack[st->obj_sp - 1].target_type, tname, ARRAY_SIZE(st->val_stack[st->obj_sp - 1].target_type) );
        }
        return 1;
    }
    case 0x1f:
    {
        WORD pid, src_pid;
        if (st->p + 4 > st->end_p) return 0;
        pid = *(const WORD *)(ctx->nodes + st->p);
        src_pid = *(const WORD *)(ctx->nodes + st->p + 2);
        st->p += 4;
        if (st->obj_sp > 0 && st->val_stack[st->obj_sp - 1].el)
        {
            struct uwp_element *target_el = st->val_stack[st->obj_sp - 1].el;
            const WCHAR *src_name = (src_pid & 0x8000)
                ? xbf_bprop_to_name( src_pid )
                : ((src_pid < ctx->prop_cnt) ? ctx->props[src_pid].name : L"");
            if (src_name && src_name[0])
            {
                const WCHAR *tprop = (pid & 0x8000)
                    ? xbf_bprop_to_name( pid )
                    : ((pid < ctx->prop_cnt) ? ctx->props[pid].name : L"");
                uwp_sa_add_tpl_binding( target_el, pid, src_name, tprop );
                if (pid == XBF_PROP_FRAMEWORKELEMENT_HEIGHT)
                    target_el->height = 0.1;
            }
        }
        return 1;
    }
    case 0x22:
    {
        int vt = 0;
        double num = 0;
        WCHAR sbuf[128] = {0};
        if (!xbf_read_value( ctx, ctx->nodes, &st->p, st->end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) return 0;
        xbf_sub_clear_last_popped( st );
        st->last_popped.kind = UWP_RD_ALIAS;
        st->last_popped.opacity = 1.0;
        lstrcpynW( st->last_popped.str_val, sbuf, ARRAY_SIZE(st->last_popped.str_val) );
        st->last_popped_off = cur_rel_off;
        st->has_last_popped = TRUE;
        return 1;
    }
    default:
        return -1;
    }
}

static BOOL xbf_exec_defer_substream( struct xbf_sub_state *st, DWORD cur_rel_off )
{
    struct xbf_ctx *ctx = st->ctx;
    DWORD def_sub = 0, def_sub_sz = 0;
    BYTE def_kind;

    if (!xbf_read_uleb( ctx->nodes, &st->p, st->end_p, &def_sub ) || st->end_p - st->p < 3) return FALSE;
    if (!xbf_hdr_substream_bounds( st->ec->hdr, def_sub, NULL, NULL, &def_sub_sz )) return FALSE;
    def_kind = ctx->nodes[st->p + 2];
    st->p += 3;

    if (def_kind == 0x09)
    {
        WORD nsid;
        const WCHAR *dname;
        struct uwp_element *ppanel_el;
        if (st->end_p - st->p < 4) return FALSE;
        nsid = *(const WORD *)(ctx->nodes + st->p);
        dname = (nsid < ctx->str_cnt && ctx->strs[nsid]) ? ctx->strs[nsid] : L"";
        ppanel_el = (st->obj_sp >= 2 && st->val_stack[st->obj_sp - 2].el)
            ? st->val_stack[st->obj_sp - 2].el : ctx->root_el;
        st->p += 4;
        if (ctx->root_el &&
            xaml_vec_grow( (void **)&ctx->root_el->defer_entries, &ctx->root_el->defer_cap,
                           ctx->root_el->defer_cnt + 1, sizeof(*ctx->root_el->defer_entries), 16 ))
        {
            struct uwp_defer_entry *de = &ctx->root_el->defer_entries[ctx->root_el->defer_cnt++];
            lstrcpynW( de->name, dname, ARRAY_SIZE(de->name) );
            de->sub_idx = (int)def_sub;
            de->parent_panel = ppanel_el;
            de->xbf_data = ctx->xbf;
            de->xbf_size = ctx->xbf_sz;
            de->root_component = ctx->root_comp;
            de->realized = FALSE;
        }
        return TRUE;
    }
    if (def_kind == 0x05)
    {
        if (!xbf_read_vsm_defer_header( ctx->nodes, &st->p, st->end_p, def_sub_sz, &st->prop_sp ))
            return FALSE;
        if (!ctx->rd_only)
        {
            BOOL has_root_slot = (ctx->root_el != NULL || ctx->root_comp != NULL);
            xbf_exec_substream( st->ec, (int)def_sub, has_root_slot, NULL, st->active_rd_el, (st->sub_idx == 0) );
        }
        else if (st->ec->sub_done && def_sub < st->ec->hdr->sub_cnt)
            st->ec->sub_done[def_sub] = TRUE;
        return TRUE;
    }
    if (def_kind == 0x08)
    {
        struct xbf_stack_val *style_sv = (st->obj_sp > 0) ? &st->val_stack[st->obj_sp - 1] : NULL;
        if (st->ec->sub_done && def_sub < st->ec->hdr->sub_cnt)
            st->ec->sub_done[def_sub] = TRUE;
        return xbf_read_style_defer_table( ctx, &st->p, st->end_p, def_sub, def_sub_sz, style_sv, &st->prop_sp );
    }
    if (def_kind == 0x0a)
    {
        struct xbf_rd_table tbl = {0};
        struct uwp_element *sub_rd_el = (st->obj_sp > 0 && st->val_stack[st->obj_sp - 1].el)
            ? st->val_stack[st->obj_sp - 1].el : st->active_rd_el;
        BOOL prev_rd_only;
        if (!xbf_read_rd_defer_header( ctx->nodes, &st->p, st->end_p, def_sub_sz, &tbl, &st->prop_sp ))
        {
            if (tbl.heap_owned) free( tbl.items );
            return FALSE;
        }
        prev_rd_only = ctx->rd_only;
        ctx->rd_only = TRUE;
        xbf_exec_substream( st->ec, (int)def_sub, FALSE, &tbl, sub_rd_el, st->vsm_collect );
        ctx->rd_only = prev_rd_only;
        if (tbl.heap_owned) free( tbl.items );
        return TRUE;
    }

    if (st->obj_sp > 0 && st->val_stack[st->obj_sp - 1].tid == XBF_TYPE_DATA_TEMPLATE)
    {
        struct xbf_stack_val *tsv = &st->val_stack[st->obj_sp - 1];
        tsv->template_xbf_data = ctx->xbf;
        tsv->template_xbf_size = ctx->xbf_sz;
        tsv->template_sub_idx = def_sub;
        tsv->template_owner_comp = ctx->root_comp;
        if (tsv->el)
        {
            tsv->el->template_xbf_data = ctx->xbf;
            tsv->el->template_xbf_size = ctx->xbf_sz;
            tsv->el->template_sub_idx = def_sub;
            uwp_set_template_owner( &tsv->el->template_owner_comp, ctx->root_comp );
        }
        TRACE( "XBF DataTemplate captured deferred substream %u (kind=%#x)\n", (unsigned)def_sub, def_kind );
    }
    if (xbf_sub_seek_next_rd_entry( st, cur_rel_off )) return TRUE;
    TRACE( "XBF unknown DeferSubstream kind=%#x at %lu/%lu\n", def_kind, (unsigned long)st->p, (unsigned long)st->end_p );
    return FALSE;
}

static void xbf_exec_substream( struct xbf_exec_ctx *ec, int sub_idx, BOOL has_initial_root,
                                const struct xbf_rd_table *rd_tbl, struct uwp_element *active_rd_el,
                                BOOL vsm_collect )
{
    struct xbf_ctx *ctx = &ec->ctx;
    struct xbf_sub_state st;

    memset( &st, 0, sizeof(st) );
    st.ec = ec;
    st.ctx = ctx;
    st.sub_idx = sub_idx;
    st.rd_tbl = rd_tbl;
    st.active_rd_el = active_rd_el;
    st.vsm_collect = vsm_collect;
    st.val_stack = st.val_stack_stk;
    st.start_off_stack = st.start_off_stk;
    st.is_rd_stack = st.is_rd_stk;
    st.val_cap = ARRAY_SIZE(st.val_stack_stk);
    st.prop_stack = st.prop_stack_stk;
    st.prop_cap = ARRAY_SIZE(st.prop_stack_stk);

    if (sub_idx < 0 || !xbf_hdr_substream_bounds( ec->hdr, (DWORD)sub_idx, &st.sub_start, &st.end_p, &st.sub_sz ))
        return;
    if (ec->sub_done)
    {
        if (ec->sub_done[sub_idx]) return;
        ec->sub_done[sub_idx] = TRUE;
    }

    st.p = st.sub_start;
    if (has_initial_root && (ctx->root_el || ctx->root_comp))
    {
        st.val_stack[0].el = ctx->root_el;
        st.val_stack[0].raw = ctx->root_comp ? ctx->root_comp
            : (ctx->root_el->outer ? ctx->root_el->outer : &ctx->root_el->IInspectable_iface);
        st.val_stack[0].opacity = 1.0;
        st.obj_sp = 1;
        st.base_sp = 1;
    }

    while (st.p < st.end_p)
    {
        DWORD cur_rel_off = st.p - st.sub_start;
        BYTE op = ctx->nodes[st.p++];
        int meta_rc;

        if (op == 0x17 || op == 0x14 || op == 0x15 || op == 0x16)
        {
            if (!xbf_exec_push_object( &st, op, cur_rel_off )) break;
        }
        else if ((op >= 0x1a && op <= 0x1c) || op == 0x1e || op == 0x24)
        {
            if (!xbf_exec_set_prop( &st, op )) break;
        }
        else if (op == 0x21)
        {
            xbf_exec_pop_object( &st );
        }
        else if (op == 0x0a || op == 0x08 || op == 0x07 || op == 0x20 || op == 0x09)
        {
            if (!xbf_exec_attach_ops( &st, op )) break;
        }
        else if (op == 0x0f)
        {
            if (!xbf_exec_defer_substream( &st, cur_rel_off )) break;
        }
        else if ((meta_rc = xbf_exec_meta_ops( &st, op, cur_rel_off )) >= 0)
        {
            if (meta_rc == 0) break;
        }
        else
        {
            TRACE( "XBF unknown op=%#x at %lu/%lu (sub %d) top_tid=%#x next=[%02x %02x %02x %02x]\n",
                   op, (unsigned long)cur_rel_off, (unsigned long)st.sub_sz, sub_idx,
                   st.obj_sp > 0 ? st.val_stack[st.obj_sp - 1].tid : 0,
                   st.p < st.end_p ? ctx->nodes[st.p] : 0, st.p + 1 < st.end_p ? ctx->nodes[st.p + 1] : 0,
                   st.p + 2 < st.end_p ? ctx->nodes[st.p + 2] : 0, st.p + 3 < st.end_p ? ctx->nodes[st.p + 3] : 0 );
            if (xbf_sub_seek_next_rd_entry( &st, cur_rel_off )) continue;
            TRACE( "XBF unrecognized op=%#x at %lu/%lu (sub %d), scanning forward\n",
                   op, (unsigned long)cur_rel_off, (unsigned long)st.end_p, sub_idx );
            while (st.p < st.end_p)
            {
                if (ctx->nodes[st.p] == 0x21 || ctx->nodes[st.p] == 0x07 || ctx->nodes[st.p] == 0x08 ||
                    ctx->nodes[st.p] == 0x14 || ctx->nodes[st.p] == 0x15)
                    break;
                st.p++;
            }
            if (st.p >= st.end_p) break;
        }
    }

    while (st.obj_sp > st.base_sp)
    {
        st.obj_sp--;
        free( st.val_stack[st.obj_sp].setters );
    }
    if (st.base_sp > 0) free( st.val_stack[0].setters );
    free( st.last_popped.setters );
    if (st.val_stack != st.val_stack_stk) free( st.val_stack );
    if (st.start_off_stack != st.start_off_stk) free( st.start_off_stack );
    if (st.is_rd_stack != st.is_rd_stk) free( st.is_rd_stack );
    if (st.prop_stack != st.prop_stack_stk) free( st.prop_stack );
}

void uwp_parse_xbf_internal( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                             void *connector, BOOL is_uwp_conn, void *app_outer,
                             int only_sub_idx, struct uwp_element *attach_root,
                             struct uwp_element **out_created,
                             const char *base_xbf_path, struct uwp_rd_bucket *target_rd,
                             BOOL rd_only )
{
    struct xbf_parsed_hdr local_hdr;
    struct xbf_parsed_hdr *hdr;
    struct xbf_exec_ctx ec = {0};
    struct uwp_element *root_el_pre;
    BOOL is_template = (out_created != NULL && attach_root == NULL);
    LARGE_INTEGER t0 = {0}, t1 = {0}, freq = {0};
    DWORD i;

    if (out_created) *out_created = NULL;
    if (!xbf || xbf_sz < 0x50 || memcmp( xbf, "XBF\0", 4 ) != 0) return;

    root_el_pre = attach_root ? attach_root : uwp_elem_from_any( component );
    if (root_el_pre) root_el_pre->xbf_parsed = TRUE;

    if (!rd_only)
    {
        QueryPerformanceFrequency( &freq );
        QueryPerformanceCounter( &t0 );
    }

    hdr = xbf_acquire_parsed_hdr( xbf, xbf_sz, is_uwp_conn, (!rd_only && only_sub_idx < 0), &local_hdr );
    if (!hdr) return;

    ec.hdr = hdr;
    ec.out_created = out_created;
    ec.reuse_root = (!is_template && (root_el_pre != NULL || (component != NULL && !target_rd)));
    ec.start_defer_cnt = root_el_pre ? root_el_pre->defer_cnt : 0;

    ec.ctx.xbf = xbf;
    ec.ctx.xbf_sz = xbf_sz;
    ec.ctx.meta_sz = hdr->meta_sz;
    ec.ctx.node_sz = hdr->node_sz;
    ec.ctx.strs = hdr->strs;
    ec.ctx.str_cnt = hdr->str_cnt;
    ec.ctx.types = hdr->types;
    ec.ctx.type_cnt = hdr->type_cnt;
    ec.ctx.props = hdr->props;
    ec.ctx.prop_cnt = hdr->prop_cnt;
    ec.ctx.nodes = hdr->nodes;
    ec.ctx.sub_cnt = hdr->sub_cnt;
    ec.ctx.sub_base = hdr->sub_base;
    ec.ctx.app_outer = app_outer;
    ec.ctx.connector = connector;
    ec.ctx.is_uwp_conn = is_uwp_conn;
    ec.ctx.root_el = root_el_pre;
    ec.ctx.root_comp = component;
    ec.ctx.target_rd = target_rd;
    ec.ctx.rd_only = rd_only;
    ec.ctx.is_template = is_template;

    if (base_xbf_path && base_xbf_path[0])
    {
        const char *slash = strrchr( base_xbf_path, '/' );
        if (slash)
        {
            size_t dlen = min( (size_t)(slash - base_xbf_path + 1), sizeof(ec.ctx.base_xbf_dir) - 1 );
            memcpy( ec.ctx.base_xbf_dir, base_xbf_path, dlen );
            ec.ctx.base_xbf_dir[dlen] = 0;
        }
    }

    if (rd_only && target_rd)
    {
        for (i = 0; i < hdr->str_cnt; i++)
        {
            const WCHAR *s = hdr->strs[i];
            size_t slen = s ? wcslen( s ) : 0;
            if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
                (wcsstr( s, L"Resource" ) || wcsstr( s, L"Themes/" ) || wcsstr( s, L"Generic" ) ||
                 wcsstr( s, L"Dark" ) || wcsstr( s, L"Dictionary" )))
            {
                struct uwp_rd_bucket *child = get_rd_bucket( (void *)(ULONG_PTR)(0x70000000u + (unsigned int)i + (unsigned int)g_loaded_res_xbf_cnt), TRUE );
                if (child)
                {
                    uwp_load_res_xbf_into_bucket( ec.ctx.base_xbf_dir, s, child );
                    if (wcsstr( s, L"Dark" )) uwp_rd_link_theme( target_rd, L"Dark", child );
                    else if (!wcsstr( s, L"Light" ) && !wcsstr( s, L"HighContrast" ))
                        uwp_rd_link_merged( target_rd, child );
                }
            }
        }
    }

    if (hdr->sub_cnt > 0)
    {
        if (hdr->sub_cnt <= ARRAY_SIZE(ec.sub_done_stk))
        {
            ec.sub_done = ec.sub_done_stk;
            memset( ec.sub_done, 0, hdr->sub_cnt * sizeof(BOOL) );
        }
        else
            ec.sub_done = calloc( hdr->sub_cnt, sizeof(BOOL) );

        if (only_sub_idx >= 0)
        {
            xbf_exec_substream( &ec, only_sub_idx, FALSE, NULL, NULL, FALSE );
        }
        else if (rd_only)
        {
            xbf_exec_substream( &ec, 0, FALSE, NULL, NULL, FALSE );
        }
        else
        {
            xbf_exec_substream( &ec, 0, TRUE, NULL, NULL, FALSE );
            if (!ec.refs_applied) xbf_exec_apply_root_refs( &ec );
            for (i = 1; i < hdr->sub_cnt; i++)
            {
                BOOL is_lazy = FALSE;
                if (root_el_pre)
                {
                    int d;
                    for (d = ec.start_defer_cnt; d < root_el_pre->defer_cnt; d++)
                        if (root_el_pre->defer_entries[d].sub_idx == (int)i) { is_lazy = TRUE; break; }
                }
                if (!is_lazy)
                    xbf_exec_substream( &ec, (int)i, FALSE, NULL, NULL, FALSE );
            }
        }
        if (ec.sub_done != ec.sub_done_stk) free( ec.sub_done );
    }

    /* Retry deferred resource-ref properties once all substreams & local RDs are populated */
    if (ec.def_res_cnt > 0)
    {
        int d;
        struct uwp_rd_bucket *hint_rd = ec.ctx.root_el ? ec.ctx.root_el->rd_bucket : ec.ctx.target_rd;
        for (d = 0; d < ec.def_res_cnt; d++)
        {
            const struct xbf_deferred_res *dr = &ec.def_res[d];
            if (dr->el || dr->raw)
            {
                struct xbf_stack_val tmp_sv = {0};
                struct uwp_rd_entry *re = NULL;
                struct uwp_rd_bucket *found_b = NULL;
                tmp_sv.raw = dr->raw;
                if (dr->sval[0] && uwp_rd_find_entry( hint_rd, dr->el, dr->sval, 0, &re, &found_b ) && re)
                {
                    ec.ctx.res_cache_key = dr->sval;
                    ec.ctx.res_cache_start_b = hint_rd;
                    ec.ctx.res_cache_scope_el = dr->el;
                    ec.ctx.res_cache_entry = re;
                    ec.ctx.res_cache_bucket = found_b;
                    ec.ctx.res_cache_valid = TRUE;
                }
                xbf_apply_property( &ec.ctx, dr->el, dr->el ? NULL : &tmp_sv, dr->pid, dr->vt,
                                    dr->num, dr->sval, dr->vec4, dr->is_res_ref );
                ec.ctx.res_cache_valid = FALSE;
            }
        }
    }

    /* Evaluate any active StateTriggers collected during instantiation */
    if (ec.active_trig_cnt > 0)
    {
        int ti;
        for (ti = 0; ti < ec.active_trig_cnt; ti++)
        {
            struct uwp_element *trig = ec.active_trigs[ti], *p;
            if (!trig || !trig->state_trigger_active) continue;
            for (p = trig->parent; p; p = p->parent)
            {
                if ((p->class_flags2 & UWP_CF2_VISUAL_STATE) && p->name[0])
                {
                    struct uwp_element *scope = p->owner_component ? p->owner_component : root_el_pre;
                    if (scope) uwp_vsm_goto_state_on_tree( scope, p->name );
                    break;
                }
            }
        }
    }

    if (!rd_only)
    {
        double us;
        QueryPerformanceCounter( &t1 );
        us = freq.QuadPart ? ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart) : 0.0;
        TRACE( "XBF EXEC (%s, sub=%d, %lu xbf, %lu subs, hdr=%s) in %.1f us\n",
               debugstr_w(root_el_pre ? root_el_pre->class_name : L""),
               only_sub_idx, (unsigned long)xbf_sz, (unsigned long)hdr->sub_cnt,
               hdr->cached ? "cached" : "local", us );
    }

    if (ec.root_binding) IUnknown_Release( (IUnknown *)ec.root_binding );
    if (ec.dt_binding) IUnknown_Release( (IUnknown *)ec.dt_binding );
    if (ec.def_res_heap) free( ec.def_res );
    if (ec.active_trigs_heap) free( ec.active_trigs );
    xbf_parsed_hdr_release( hdr );
}

void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                            struct uwp_rd_bucket *target_rd )
{
    uwp_parse_xbf_internal( xbf, xbf_sz, NULL, NULL, !xaml_get_app_caps()->winui3, NULL, -1, NULL, NULL,
                            base_xbf_path, target_rd, TRUE );
}

void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                              void *connector, BOOL is_uwp_conn, void *app_outer,
                              int only_sub_idx, struct uwp_element *attach_root,
                              struct uwp_element **out_created )
{
    uwp_ensure_app_resources_loaded();
    uwp_parse_xbf_internal( xbf, xbf_sz, component, connector, is_uwp_conn, app_outer,
                            only_sub_idx, attach_root, out_created, NULL, NULL, FALSE );
}

void uwp_realize_deferred_entry( struct uwp_defer_entry *de )
{
    void *conn = NULL;
    struct uwp_element *created = NULL;
    if (!de || de->realized || !de->xbf_data || !de->root_component) return;
    de->realized = TRUE;

    TRACE( "Realizing deferred substream %d for '%s'\n", de->sub_idx, debugstr_w(de->name) );
    IInspectable_QueryInterface( de->root_component, &IID_IComponentConnector_UWP_g, &conn );
    uwp_parse_and_build_xbf( de->xbf_data, de->xbf_size, de->root_component,
                             conn, TRUE, uwp_get_app_outer(),
                             de->sub_idx, uwp_elem_from_any( de->root_component ), &created );
    if (conn) IUnknown_Release( (IUnknown *)conn );

    if (created)
    {
        struct uwp_element *root_el = uwp_elem_from_any( de->root_component );
        if (root_el && !created->owner_component)
            created->owner_component = root_el;
        if (!created->name[0] && de->name[0])
        {
            lstrcpynW( created->name, de->name, ARRAY_SIZE(created->name) );
            if (root_el &&
                xaml_vec_grow( (void **)&root_el->named_elems, &root_el->named_cap,
                               root_el->named_cnt + 1, sizeof(*root_el->named_elems), 32 ))
            {
                root_el->named_elems[root_el->named_cnt++] = created;
            }
        }
    }
    if (created && de->parent_panel)
    {
        IInspectable *ch_iface = created->outer ? created->outer : &created->IInspectable_iface;
        created->parent = de->parent_panel;
        if (!de->parent_panel->children_coll)
            uwp_vec_create( de->parent_panel, &de->parent_panel->children_coll );
        uwp_vec_Append( de->parent_panel->children_coll, ch_iface );
        if (de->parent_panel->data_context && !created->data_context)
            uwp_propagate_data_context( created, de->parent_panel->data_context );
        uwp_vsm_invalidate_eval_chain( created );
        {
            struct uwp_element *root_el = uwp_elem_from_any( de->root_component );
            if (root_el && root_el->loaded_fired)
            {
                if (UWP_EV_CNT( root_el, loading_reg ) > 0)
                {
                    IInspectable *sender = root_el->outer ? root_el->outer : &root_el->IInspectable_iface;
                    uwp_fire_event_registry( &root_el->events->loading_reg, sender, NULL, "Loading(defer)" );
                }
                uwp_fire_loaded_tree( created );
            }
        }
        uwp_invalidate_window();
    }
}

IInspectable *uwp_instantiate_xbf_substream_rd( const BYTE *xbf_data, DWORD xbf_size, DWORD sub_idx,
                                                IInspectable *owner_comp, struct uwp_rd_bucket *rd_bucket )
{
    struct uwp_element *created = NULL;
    void *conn = NULL;
    if (!xbf_data || !xbf_size) return NULL;
    if (owner_comp)
        IInspectable_QueryInterface( owner_comp, &IID_IComponentConnector_UWP_g, &conn );
    uwp_ensure_app_resources_loaded();
    uwp_parse_xbf_internal( xbf_data, xbf_size, owner_comp, conn, TRUE, uwp_get_app_outer(),
                            (int)sub_idx, NULL, &created, NULL, rd_bucket, FALSE );
    if (conn) IUnknown_Release( (IUnknown *)conn );
    if (!created) return NULL;
    {
        IInspectable *ret = created->outer ? created->outer : &created->IInspectable_iface;
        IInspectable_AddRef( ret );
        return ret;
    }
}

IInspectable *uwp_instantiate_xbf_substream( const BYTE *xbf_data, DWORD xbf_size, DWORD sub_idx, IInspectable *owner_comp )
{
    return uwp_instantiate_xbf_substream_rd( xbf_data, xbf_size, sub_idx, owner_comp, NULL );
}

BOOL uwp_try_load_xbf_component( void *component, void *resourceLocator, void *app_outer )
{
    static int depth = 0;
    void *conn = NULL;
    void *uri_rt = NULL;
    WCHAR uri_str[MAX_PATH] = {0};
    char resolved_path[256] = {0};
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;
    BOOL is_uwp_conn = TRUE;

    if (!component || !resourceLocator) return FALSE;
    if (xaml_get_app_caps()->winui3)
    {
        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector_WinUI, &conn )) && conn)
            is_uwp_conn = FALSE;
        else if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector_UWP_g, &conn )) && conn)
            is_uwp_conn = TRUE;
        else
            return FALSE;
    }
    else
    {
        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector_UWP_g, &conn )) && conn)
            is_uwp_conn = TRUE;
        else if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector_WinUI, &conn )) && conn)
            is_uwp_conn = FALSE;
        else
            return FALSE;
    }

    if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)resourceLocator, &IID_IUriRuntimeClass_g, &uri_rt )) && uri_rt)
    {
        HSTRING hraw = NULL;
        if (SUCCEEDED(IUriRuntimeClass_get_AbsoluteUri( (IUriRuntimeClass *)uri_rt, &hraw )) && hraw)
        {
            const WCHAR *s = WindowsGetStringRawBuffer( hraw, NULL );
            if (s) lstrcpynW( uri_str, s, MAX_PATH );
            WindowsDeleteString( hraw );
        }
        IInspectable_Release( (IInspectable *)uri_rt );
    }

    if (!uri_str[0] || !uwp_pri_find_xbf_rel( NULL, uri_str, &xbf, &xbf_sz, resolved_path, sizeof(resolved_path) ))
    {
        IUnknown_Release( (IUnknown *)conn );
        return FALSE;
    }

    if (depth > 8)
    {
        IUnknown_Release( (IUnknown *)conn );
        return TRUE;
    }
    depth++;

    TRACE( "uwp_try_load_xbf_component: %s (%lu bytes) on %p (uwp=%d depth=%d)\n",
           debugstr_w(uri_str), (unsigned long)xbf_sz, component, is_uwp_conn, depth );

    /* Build visual tree and drive Connect/GetBindingConnector from real XBF v2 opcodes */
    if (is_uwp_conn) uwp_ensure_app_resources_loaded();
    uwp_parse_xbf_internal( xbf, xbf_sz, (IInspectable *)component, conn, is_uwp_conn, app_outer,
                            -1, NULL, NULL, resolved_path[0] ? resolved_path : NULL, NULL, FALSE );

    /* Realize default deferred elements inside empty panels */
    if (is_uwp_conn)
    {
        struct uwp_element *comp_el = uwp_elem_from_any( component );
        if (comp_el)
            uwp_realize_empty_panel_defers( comp_el );
    }

    IUnknown_Release( (IUnknown *)conn );
    depth--;
    return TRUE;
}

/* ============================================================================
 * 7. Genuine Visual Tree Layout, Rendering & Input Dispatch
 * ============================================================================ */

float g_layout_scale = 1.0f;
