/*
 * PRI v2 ("mrm_pri2") reader for windows.ui.xaml.
 *
 * Copyright (C) 2026 Wine Developers
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

#include "pri.h"
#include "private.h"
#include "uwp_private.h"
#include "xbf_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(resources);

#include "wine/winrt_pri.h"

pri_index *pri_load( const WCHAR *path )
{
    return winrt_pri_load( path );
}

void pri_release( pri_index *index )
{
    winrt_pri_release( index );
}

UINT32 pri_top_count( pri_index *index )
{
    return winrt_pri_top_count( index );
}

BOOL pri_top_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    return winrt_pri_top_name( index, index_, buffer, len );
}

UINT32 pri_name_count( pri_index *index )
{
    return winrt_pri_name_count( index );
}

BOOL pri_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    return winrt_pri_name( index, index_, buffer, len );
}

UINT32 pri_map_count( pri_index *index, const WCHAR *prefix )
{
    return winrt_pri_map_count( index, prefix );
}

BOOL pri_map_fullname( pri_index *index, const WCHAR *prefix, UINT32 i, WCHAR *buffer, DWORD len )
{
    return winrt_pri_map_fullname( index, prefix, i, buffer, len );
}

BOOL pri_map_get( pri_index *index, const WCHAR *prefix, UINT32 i, const pri_qual *ctx, UINT32 ctxcount,
                  WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out )
{
    return winrt_pri_map_get( index, prefix, i, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
}

BOOL pri_lookup( pri_index *index, const WCHAR *key, const pri_qual *ctx, UINT32 ctxcount,
                 WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out )
{
    return winrt_pri_lookup( index, key, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
}

void pri_free_result( WCHAR *value, pri_qual *quals )
{
    winrt_pri_free_result( value, quals );
}

void pri_map_file_to( const WCHAR *pri_path, const BYTE **out_data, DWORD *out_size )
{
    HANDLE hFile, hMap;
    DWORD sz;

    hFile = CreateFileW( pri_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (hFile != INVALID_HANDLE_VALUE)
    {
        sz = GetFileSize( hFile, NULL );
        if (sz > 0x40 && sz < 64 * 1024 * 1024)
        {
            hMap = CreateFileMappingW( hFile, NULL, PAGE_READONLY, 0, 0, NULL );
            if (hMap)
            {
                const BYTE *mapped = MapViewOfFile( hMap, FILE_MAP_READ, 0, 0, 0 );
                if (mapped && !memcmp( mapped, "mrm_pri2", 8 ))
                {
                    *out_data = mapped;
                    *out_size = sz;
                    TRACE( "Mapped %s (%lu bytes) at %p\n", debugstr_w(pri_path), (unsigned long)sz, mapped );
                }
                else if (mapped)
                {
                    UnmapViewOfFile( mapped );
                }
                CloseHandle( hMap );
            }
        }
        CloseHandle( hFile );
    }
}

const BYTE *pri_get_data( DWORD *out_size )
{
    InitOnceExecuteOnce( &g_pri_once, init_pri_once, NULL, NULL );
    if (out_size) *out_size = g_pri_size;
    return g_pri_data;
}

const BYTE *winui_pri_get_data( DWORD *out_size )
{
    if (!g_winui_pri_probed)
    {
        HMODULE hmod = GetModuleHandleW( L"Microsoft.UI.Xaml.dll" );
        if (hmod)
        {
            WCHAR pri_path[MAX_PATH];
            WCHAR *slash;
            g_winui_pri_probed = TRUE;
            if (GetModuleFileNameW( hmod, pri_path, MAX_PATH ))
            {
                slash = wcsrchr( pri_path, '\\' );
                if (slash) *(slash + 1) = 0;
                if (wcslen( pri_path ) + ARRAY_SIZE(L"resources.pri") <= ARRAY_SIZE(pri_path))
                {
                    lstrcpynW( pri_path + wcslen( pri_path ), L"resources.pri",
                               (int)(ARRAY_SIZE(pri_path) - wcslen( pri_path )) );
                    pri_map_file_to( pri_path, &g_winui_pri_data, &g_winui_pri_size );
                }
            }
        }
    }
    if (out_size) *out_size = g_winui_pri_size;
    return g_winui_pri_data;
}

static BOOL pri_get_section( const BYTE *pri, DWORD pri_sz, int sec_idx, const BYTE **out_sec, DWORD *out_sz )
{
    DWORD toc_off, sec_base, off, sz;
    WORD num_sec;
    if (!pri || pri_sz < 0x20) return FALSE;
    toc_off = *(const DWORD *)(pri + 0x10);
    sec_base = *(const DWORD *)(pri + 0x14);
    num_sec = *(const WORD *)(pri + 0x18);
    if (sec_idx < 0 || sec_idx >= num_sec) return FALSE;
    if (toc_off + (DWORD)(sec_idx + 1) * 32 > pri_sz) return FALSE;
    off = *(const DWORD *)(pri + toc_off + sec_idx * 32 + 24);
    sz = *(const DWORD *)(pri + toc_off + sec_idx * 32 + 28);
    if (sec_base + off + sz > pri_sz) return FALSE;
    *out_sec = pri + sec_base + off;
    *out_sz = sz;
    return TRUE;
}

static BOOL pri_find_section_by_magic( const BYTE *pri, DWORD pri_sz, const char *magic, size_t magic_len,
                                       const BYTE **out_sec, DWORD *out_sz )
{
    DWORD toc_off, sec_base;
    WORD num_sec, i;

    if (!pri || pri_sz < 0x20 || !magic || magic_len == 0 || magic_len > 16) return FALSE;
    toc_off = *(const DWORD *)(pri + 0x10);
    sec_base = *(const DWORD *)(pri + 0x14);
    num_sec = *(const WORD *)(pri + 0x18);
    for (i = 0; i < num_sec; i++)
    {
        DWORD toc_entry = toc_off + (DWORD)i * 32;
        DWORD off, sz;
        if (toc_entry + 32 > pri_sz) break;
        if (memcmp( pri + toc_entry, magic, magic_len ) != 0) continue;
        off = *(const DWORD *)(pri + toc_entry + 24);
        sz = *(const DWORD *)(pri + toc_entry + 28);
        if (sec_base + off + sz > pri_sz || sz < magic_len) continue;
        if (memcmp( pri + sec_base + off, magic, magic_len ) != 0) continue;
        *out_sec = pri + sec_base + off;
        *out_sz = sz;
        return TRUE;
    }
    return FALSE;
}

static BOOL pri_get_data_item( const BYTE *pri, DWORD pri_sz, int sec_idx, int data_idx, const BYTE **out_ptr, DWORD *out_sz )
{
    const BYTE *di = NULL;
    DWORD di_sz = 0, payload_off, item_off, item_sz;
    WORD n_small, n_large;

    if (!pri_get_section( pri, pri_sz, sec_idx, &di, &di_sz ) || di_sz < 0x2c)
        return FALSE;
    if (memcmp( di, "[mrm_dataitem]", 14 ) != 0)
        return FALSE;

    n_small = *(const WORD *)(di + 0x24);
    n_large = *(const WORD *)(di + 0x26);
    payload_off = 0x2c + (DWORD)n_small * 4 + (DWORD)n_large * 8;
    if (payload_off > di_sz) return FALSE;

    if (data_idx < n_small)
    {
        item_off = *(const WORD *)(di + 0x2c + data_idx * 4);
        item_sz = *(const WORD *)(di + 0x2c + data_idx * 4 + 2);
    }
    else if (data_idx - n_small < n_large)
    {
        int lidx = data_idx - n_small;
        item_off = *(const DWORD *)(di + 0x2c + n_small * 4 + lidx * 8);
        item_sz = *(const DWORD *)(di + 0x2c + n_small * 4 + lidx * 8 + 4);
    }
    else return FALSE;

    if (payload_off + item_off + item_sz > di_sz) return FALSE;
    *out_ptr = di + payload_off + item_off;
    *out_sz = item_sz;
    return TRUE;
}

/* Reconstruct full path of node_id in [mrm_hschemaex] into ASCII buffer */
static void pri_build_node_path( const BYTE *hs, DWORD hs_sz, DWORD node_off, DWORD num_nodes,
                                 DWORD pool_off, WORD nid, char *buf, size_t buf_sz )
{
    WORD stack[24];
    int depth = 0, i;
    size_t pos = 0;
    WORD cur = nid;

    while (cur < num_nodes && depth < 24)
    {
        WORD parent = *(const WORD *)(hs + node_off + (DWORD)cur * 12);
        stack[depth++] = cur;
        if (cur == 0 || parent == cur) break;
        cur = parent;
    }

    buf[0] = 0;
    for (i = depth - 1; i >= 0; i--)
    {
        WORD n = stack[i];
        DWORD name_off = ((DWORD)(hs[node_off + (DWORD)n * 12 + 7] & 0x0f) << 16)
                       | *(const WORD *)(hs + node_off + (DWORD)n * 12 + 8);
        if (pool_off + name_off < hs_sz)
        {
            const char *s = (const char *)(hs + pool_off + name_off);
            size_t max_rem = hs_sz - (pool_off + name_off);
            size_t slen = strnlen( s, max_rem );
            if (slen > 0)
            {
                if (pos > 0 && pos + 1 < buf_sz) buf[pos++] = '/';
                if (pos + slen < buf_sz)
                {
                    memcpy( buf + pos, s, slen );
                    pos += slen;
                }
            }
        }
    }
    buf[pos] = 0;
}

static void pri_get_user_locale( WCHAR *out_full, size_t full_cap, WCHAR *out_lang, size_t lang_cap )
{
    static WCHAR cached_full[LOCALE_NAME_MAX_LENGTH] = {0};
    static WCHAR cached_lang[16] = {0};
    static BOOL init = FALSE;

    if (!init)
    {
        WCHAR buf[LOCALE_NAME_MAX_LENGTH] = {0};
        if (GetUserDefaultLocaleName( buf, ARRAY_SIZE(buf) ) > 0 && buf[0])
        {
            const WCHAR *dash = wcschr( buf, L'-' );
            lstrcpynW( cached_full, buf, ARRAY_SIZE(cached_full) );
            if (dash && dash > buf)
            {
                size_t llen = min( (size_t)(dash - buf), ARRAY_SIZE(cached_lang) - 1 );
                memcpy( cached_lang, buf, llen * sizeof(WCHAR) );
                cached_lang[llen] = 0;
            }
            else
                lstrcpynW( cached_lang, buf, ARRAY_SIZE(cached_lang) );
        }
        init = TRUE;
    }
    lstrcpynW( out_full, cached_full, (int)full_cap );
    lstrcpynW( out_lang, cached_lang, (int)lang_cap );
}

static DWORD pri_select_locale_cand_offset( const BYTE *pri, DWORD pri_sz, DWORD dec )
{
    const BYTE *decn_sec = NULL, *d;
    DWORD decn_sz = 0, size, qualin_at, dq_at, idx_at, val_at;
    WORD ndist, nqual, nqset, ndec, nidx, dec_first, dec_n, j, k;
    WCHAR user_full[LOCALE_NAME_MAX_LENGTH] = {0}, user_lang[16] = {0};
    size_t user_lang_len;
    DWORD match_user_lang = (~0u), match_en_us = (~0u), match_en = (~0u);

    if (!pri_find_section_by_magic( pri, pri_sz, "[mrm_decn_info]", 15, &decn_sec, &decn_sz ) || decn_sz < 0x2c)
        return 0;
    d = decn_sec + 0x20;
    size = decn_sz - 0x20;
    ndist = *(const WORD *)(d + 0);
    nqual = *(const WORD *)(d + 2);
    nqset = *(const WORD *)(d + 4);
    ndec  = *(const WORD *)(d + 6);
    nidx  = *(const WORD *)(d + 8);
    if (dec >= ndec || ndist > 4096 || (DWORD)nidx * 2 > size) return 0;

    qualin_at = 12 + (DWORD)ndec * 4 + (DWORD)nqset * 4;
    dq_at     = qualin_at + (DWORD)nqual * 8;
    idx_at    = dq_at + (DWORD)ndist * 12;
    val_at    = idx_at + (DWORD)nidx * 2;
    if (val_at > size) return 0;

    dec_first = *(const WORD *)(d + 12 + dec * 4);
    dec_n     = *(const WORD *)(d + 12 + dec * 4 + 2);
    if (dec_n <= 1) return 0;

    pri_get_user_locale( user_full, ARRAY_SIZE(user_full), user_lang, ARRAY_SIZE(user_lang) );
    user_lang_len = wcslen( user_lang );

    for (j = 0; j < dec_n; j++)
    {
        WORD qs_idx, qf, qn;
        if ((DWORD)(dec_first + j) >= nidx) break;
        qs_idx = *(const WORD *)(d + idx_at + (dec_first + j) * 2);
        if (qs_idx >= nqset) continue;
        qf = *(const WORD *)(d + 12 + (DWORD)ndec * 4 + (DWORD)qs_idx * 4);
        qn = *(const WORD *)(d + 12 + (DWORD)ndec * 4 + (DWORD)qs_idx * 4 + 2);
        for (k = 0; k < qn; k++)
        {
            WORD qi, di, qtype;
            DWORD voff, at;
            const WCHAR *val_str;
            if ((DWORD)(qf + k) >= nidx) break;
            qi = *(const WORD *)(d + idx_at + (qf + k) * 2);
            if (qi >= nqual) continue;
            di = *(const WORD *)(d + qualin_at + (DWORD)qi * 8);
            if (di >= ndist) continue;
            qtype = *(const WORD *)(d + dq_at + (DWORD)di * 12 + 2);
            voff  = *(const DWORD *)(d + dq_at + (DWORD)di * 12 + 8);
            at    = val_at + voff * 2;
            if (qtype != 0 || at + 2 > size) continue;
            val_str = (const WCHAR *)(d + at);
            if (user_full[0] && !wcsicmp( val_str, user_full ))
                return j;
            if (match_user_lang == (~0u) && user_lang_len > 0 &&
                !wcsnicmp( val_str, user_lang, user_lang_len ) &&
                (val_str[user_lang_len] == 0 || val_str[user_lang_len] == L'-'))
                match_user_lang = j;
            if (match_en_us == (~0u) && !wcsicmp( val_str, L"en-US" ))
                match_en_us = j;
            if (match_en == (~0u) && (!wcsnicmp( val_str, L"en-", 3 ) || !wcsicmp( val_str, L"en" )))
                match_en = j;
        }
    }
    if (match_user_lang != (~0u)) return match_user_lang;
    if (match_en_us != (~0u)) return match_en_us;
    if (match_en != (~0u)) return match_en;
    return 0;
}

static BOOL pri_resolve_item_data( const BYTE *pri, DWORD pri_sz, const BYTE *rm, DWORD rm_sz,
                                   DWORD count1, DWORD extra_sz, DWORD map_off, DWORD cand_off,
                                   DWORD item_id, const BYTE **out_ptr, DWORD *out_sz, BYTE *out_vti )
{
    DWORD dec = 0, cand_base = 0, cand_idx;
    WORD data_idx, sec_idx;
    if (item_id < count1)
    {
        dec       = *(const WORD *)(rm + map_off + item_id * 4);
        cand_base = *(const WORD *)(rm + map_off + item_id * 4 + 2);
    }
    else if (extra_sz >= 28)
    {
        DWORD r_off = map_off + count1 * 4 + 28 + (item_id - count1) * 8;
        if (r_off + 8 > cand_off) return FALSE;
        dec       = *(const DWORD *)(rm + r_off);
        cand_base = *(const DWORD *)(rm + r_off + 4);
    }
    else return FALSE;
    cand_idx = cand_base + pri_select_locale_cand_offset( pri, pri_sz, dec );
    if (cand_off + (cand_idx + 1) * 8 > rm_sz) cand_idx = cand_base;
    if (cand_off + (cand_idx + 1) * 8 > rm_sz) return FALSE;
    if (out_vti) *out_vti = *(const BYTE *)(rm + cand_off + cand_idx * 8 + 1);
    data_idx = *(const WORD *)(rm + cand_off + cand_idx * 8 + 4);
    sec_idx = *(const WORD *)(rm + cand_off + cand_idx * 8 + 6);
    return pri_get_data_item( pri, pri_sz, sec_idx, data_idx, out_ptr, out_sz );
}

static inline UINT32 pri_hash_str_ci( const char *s )
{
    UINT32 h = 2166136261u;
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c += 32;
        h ^= (UINT32)c;
        h *= 16777619u;
    }
    return h;
}

static inline UINT32 pri_hash_str_ci_n( const char *s, size_t len )
{
    UINT32 h = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        h ^= (UINT32)c;
        h *= 16777619u;
    }
    return h;
}

struct pri_item_index
{
    BOOL valid;
    const BYTE *pri;
    DWORD pri_sz;
    const BYTE *rm;
    DWORD rm_sz;
    DWORD count1, extra_sz, map_off, cand_off;
    DWORD num_items;
    char **paths;
    const char **leaves;
    const char **seg2s;
    const char **res_uids;
    size_t *res_uid_lens;
    const char **res_prop_rels;
    UINT32 *full_slots;
    UINT32 *leaf_slots;
    UINT32 *seg2_slots;
    UINT32 *res_uid_heads;
    UINT32 *res_uid_nexts;
    UINT32 hash_mask;
};

static struct pri_item_index g_pri_item_idx = {0};
static INIT_ONCE g_pri_item_idx_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_pri_item_index( INIT_ONCE *once, void *param, void **ctx )
{
    DWORD pri_sz = 0, hs_sz = 0, rm_sz = 0;
    const BYTE *pri = pri_get_data( &pri_sz );
    const BYTE *hs = NULL, *rm = NULL;
    DWORD num_nodes, num_scopes, num_items;
    DWORD node_off, scope_off, item_off, pool_off, map_off, cand_off;
    DWORD count1, extra_sz, hs_base, iid;
    WORD l1, l2, num_env;
    UINT32 cap = 64, mask;

    if (!pri) return TRUE;
    if (!pri_find_section_by_magic( pri, pri_sz, "[mrm_hschemaex]", 15, &hs, &hs_sz ) || hs_sz < 0xd8) return TRUE;
    if (!pri_find_section_by_magic( pri, pri_sz, "[mrm_res_map2_]", 15, &rm, &rm_sz ) || rm_sz < 0x80) return TRUE;

    l1 = *(const WORD *)(hs + 0x22);
    l2 = *(const WORD *)(hs + 0x24);
    hs_base = ((0x48 + ((DWORD)l1 + (DWORD)l2) * 2 + 3) & ~3u) + 8;
    if (hs_base + 0x18 > hs_sz) return TRUE;

    num_nodes = *(const DWORD *)(hs + hs_base);
    num_scopes = *(const DWORD *)(hs + hs_base + 4);
    num_items = *(const DWORD *)(hs + hs_base + 8);
    node_off = hs_base + 0x18;
    scope_off = node_off + num_nodes * 12;
    item_off = scope_off + num_scopes * 8;
    pool_off = item_off + num_items * 2 + *(const DWORD *)(hs + hs_base + 12) * 2;
    if (pool_off >= hs_sz || num_items == 0) return TRUE;

    num_env = *(const WORD *)(rm + 0x2a);
    count1 = *(const DWORD *)(rm + 0x30);
    extra_sz = *(const DWORD *)(rm + 0x3c);
    map_off = 0x40 + (DWORD)num_env * 8 + 8;
    cand_off = map_off + count1 * 4 + extra_sz;
    if (cand_off >= rm_sz) return TRUE;

    while (cap < num_items * 2) cap *= 2;
    mask = cap - 1;
    g_pri_item_idx.paths = calloc( num_items, sizeof(char *) );
    g_pri_item_idx.leaves = calloc( num_items, sizeof(const char *) );
    g_pri_item_idx.seg2s = calloc( num_items, sizeof(const char *) );
    g_pri_item_idx.res_uids = calloc( num_items, sizeof(const char *) );
    g_pri_item_idx.res_uid_lens = calloc( num_items, sizeof(size_t) );
    g_pri_item_idx.res_prop_rels = calloc( num_items, sizeof(const char *) );
    g_pri_item_idx.full_slots = calloc( cap, sizeof(UINT32) );
    g_pri_item_idx.leaf_slots = calloc( cap, sizeof(UINT32) );
    g_pri_item_idx.seg2_slots = calloc( cap, sizeof(UINT32) );
    g_pri_item_idx.res_uid_heads = calloc( cap, sizeof(UINT32) );
    g_pri_item_idx.res_uid_nexts = calloc( num_items, sizeof(UINT32) );
    if (!g_pri_item_idx.paths || !g_pri_item_idx.leaves || !g_pri_item_idx.seg2s ||
        !g_pri_item_idx.res_uids || !g_pri_item_idx.res_uid_lens || !g_pri_item_idx.res_prop_rels ||
        !g_pri_item_idx.full_slots || !g_pri_item_idx.leaf_slots || !g_pri_item_idx.seg2_slots ||
        !g_pri_item_idx.res_uid_heads || !g_pri_item_idx.res_uid_nexts)
        return TRUE;

    g_pri_item_idx.pri = pri;
    g_pri_item_idx.pri_sz = pri_sz;
    g_pri_item_idx.rm = rm;
    g_pri_item_idx.rm_sz = rm_sz;
    g_pri_item_idx.count1 = count1;
    g_pri_item_idx.extra_sz = extra_sz;
    g_pri_item_idx.map_off = map_off;
    g_pri_item_idx.cand_off = cand_off;
    g_pri_item_idx.num_items = num_items;
    g_pri_item_idx.hash_mask = mask;

    for (iid = 0; iid < num_items; iid++)
    {
        WORD nid = *(const WORD *)(hs + item_off + iid * 2);
        char path[256];
        char *dup;
        const char *slash, *leaf, *seg2 = NULL;
        UINT32 slot;

        pri_build_node_path( hs, hs_sz, node_off, num_nodes, pool_off, nid, path, sizeof(path) );
        dup = strdup( path );
        if (!dup) continue;
        g_pri_item_idx.paths[iid] = dup;
        slash = strrchr( dup, '/' );
        leaf = slash ? slash + 1 : dup;
        g_pri_item_idx.leaves[iid] = leaf;
        if (slash && slash > dup)
        {
            const char *p2 = slash - 1;
            while (p2 > dup && *p2 != '/') p2--;
            seg2 = (*p2 == '/') ? (p2 + 1) : dup;
            g_pri_item_idx.seg2s[iid] = seg2;
        }

        slot = pri_hash_str_ci( dup ) & mask;
        while (g_pri_item_idx.full_slots[slot])
        {
            if (!_stricmp( g_pri_item_idx.paths[g_pri_item_idx.full_slots[slot] - 1], dup ))
                break;
            slot = (slot + 1) & mask;
        }
        if (!g_pri_item_idx.full_slots[slot])
            g_pri_item_idx.full_slots[slot] = iid + 1;

        slot = pri_hash_str_ci( leaf ) & mask;
        while (g_pri_item_idx.leaf_slots[slot])
        {
            if (!_stricmp( g_pri_item_idx.leaves[g_pri_item_idx.leaf_slots[slot] - 1], leaf ))
                break;
            slot = (slot + 1) & mask;
        }
        if (!g_pri_item_idx.leaf_slots[slot])
            g_pri_item_idx.leaf_slots[slot] = iid + 1;

        if (seg2)
        {
            slot = pri_hash_str_ci( seg2 ) & mask;
            while (g_pri_item_idx.seg2_slots[slot])
            {
                if (!_stricmp( g_pri_item_idx.seg2s[g_pri_item_idx.seg2_slots[slot] - 1], seg2 ))
                    break;
                slot = (slot + 1) & mask;
            }
            if (!g_pri_item_idx.seg2_slots[slot])
                g_pri_item_idx.seg2_slots[slot] = iid + 1;
        }

        {
            const char *res_after = NULL;
            if (!_strnicmp( dup, "Resources/", 10 ))
                res_after = dup + 10;
            else
            {
                const char *p = dup;
                while ((p = strchr( p, '/' )) != NULL)
                {
                    if (!_strnicmp( p + 1, "Resources/", 10 ))
                    {
                        res_after = p + 11;
                        break;
                    }
                    p++;
                }
            }
            if (res_after && *res_after)
            {
                const char *uid_slash = strchr( res_after, '/' );
                if (uid_slash && uid_slash > res_after && uid_slash[1])
                {
                    size_t ulen = (size_t)(uid_slash - res_after);
                    UINT32 uslot = pri_hash_str_ci_n( res_after, ulen ) & mask;
                    g_pri_item_idx.res_uids[iid] = res_after;
                    g_pri_item_idx.res_uid_lens[iid] = ulen;
                    g_pri_item_idx.res_prop_rels[iid] = uid_slash + 1;
                    g_pri_item_idx.res_uid_nexts[iid] = g_pri_item_idx.res_uid_heads[uslot];
                    g_pri_item_idx.res_uid_heads[uslot] = iid + 1;
                }
            }
        }
    }
    g_pri_item_idx.valid = TRUE;
    return TRUE;
}

static BOOL pri_find_item_by_path( const char *want_path, const char *want_suffix,
                                   const BYTE **out_ptr, DWORD *out_sz, BYTE *out_vti )
{
    int matched_item = -1;
    UINT32 mask;

    InitOnceExecuteOnce( &g_pri_item_idx_once, init_pri_item_index, NULL, NULL );
    if (!g_pri_item_idx.valid) return FALSE;
    mask = g_pri_item_idx.hash_mask;

    if (want_path && want_path[0])
    {
        UINT32 slot = pri_hash_str_ci( want_path ) & mask;
        UINT32 v;
        while ((v = g_pri_item_idx.full_slots[slot]) != 0)
        {
            if (!_stricmp( g_pri_item_idx.paths[v - 1], want_path ))
            {
                matched_item = (int)(v - 1);
                break;
            }
            slot = (slot + 1) & mask;
        }
    }
    if (matched_item < 0 && want_suffix && want_suffix[0])
    {
        const char *slash = strchr( want_suffix, '/' );
        if (!slash)
        {
            UINT32 slot = pri_hash_str_ci( want_suffix ) & mask;
            UINT32 v;
            while ((v = g_pri_item_idx.leaf_slots[slot]) != 0)
            {
                if (!_stricmp( g_pri_item_idx.leaves[v - 1], want_suffix ))
                {
                    matched_item = (int)(v - 1);
                    break;
                }
                slot = (slot + 1) & mask;
            }
        }
        else if (!strchr( slash + 1, '/' ))
        {
            UINT32 slot = pri_hash_str_ci( want_suffix ) & mask;
            UINT32 v;
            while ((v = g_pri_item_idx.seg2_slots[slot]) != 0)
            {
                if (!_stricmp( g_pri_item_idx.seg2s[v - 1], want_suffix ))
                {
                    matched_item = (int)(v - 1);
                    break;
                }
                slot = (slot + 1) & mask;
            }
        }
        else
        {
            size_t slen = strlen( want_suffix );
            DWORD iid;
            for (iid = 0; iid < g_pri_item_idx.num_items; iid++)
            {
                const char *p = g_pri_item_idx.paths[iid];
                size_t plen;
                if (!p) continue;
                plen = strlen( p );
                if (plen >= slen && !_stricmp( p + plen - slen, want_suffix ) &&
                    (plen == slen || p[plen - slen - 1] == '/'))
                {
                    matched_item = (int)iid;
                    break;
                }
            }
        }
    }

    if (matched_item < 0) return FALSE;

    return pri_resolve_item_data( g_pri_item_idx.pri, g_pri_item_idx.pri_sz,
                                  g_pri_item_idx.rm, g_pri_item_idx.rm_sz,
                                  g_pri_item_idx.count1, g_pri_item_idx.extra_sz,
                                  g_pri_item_idx.map_off, g_pri_item_idx.cand_off,
                                  (DWORD)matched_item, out_ptr, out_sz, out_vti );
}

void uwp_pri_enum_xbfs_in_blob( const BYTE *pri, DWORD pri_sz,
                                       BOOL (*cb)(const char *path, const BYTE *xbf, DWORD sz, void *ctx), void *ctx )
{
    DWORD hs_sz = 0, rm_sz = 0;
    const BYTE *hs = NULL, *rm = NULL;
    DWORD num_nodes, num_scopes, num_items;
    DWORD node_off, scope_off, item_off, pool_off, map_off, cand_off;
    DWORD count1, extra_sz;
    WORD l1, l2, num_env;
    DWORD hs_base, iid;

    if (!pri || !cb) return;
    if (!pri_find_section_by_magic( pri, pri_sz, "[mrm_hschemaex]", 15, &hs, &hs_sz ) || hs_sz < 0xd8) return;
    if (!pri_find_section_by_magic( pri, pri_sz, "[mrm_res_map2_]", 15, &rm, &rm_sz ) || rm_sz < 0x80) return;

    l1 = *(const WORD *)(hs + 0x22);
    l2 = *(const WORD *)(hs + 0x24);
    hs_base = ((0x48 + ((DWORD)l1 + (DWORD)l2) * 2 + 3) & ~3u) + 8;
    if (hs_base + 0x18 > hs_sz) return;

    num_nodes = *(const DWORD *)(hs + hs_base);
    num_scopes = *(const DWORD *)(hs + hs_base + 4);
    num_items = *(const DWORD *)(hs + hs_base + 8);
    node_off = hs_base + 0x18;
    scope_off = node_off + num_nodes * 12;
    item_off = scope_off + num_scopes * 8;
    pool_off = item_off + num_items * 2 + *(const DWORD *)(hs + hs_base + 12) * 2;
    if (pool_off >= hs_sz) return;

    num_env = *(const WORD *)(rm + 0x2a);
    count1 = *(const DWORD *)(rm + 0x30);
    extra_sz = *(const DWORD *)(rm + 0x3c);
    map_off = 0x40 + (DWORD)num_env * 8 + 8;
    cand_off = map_off + count1 * 4 + extra_sz;
    if (cand_off >= rm_sz) return;

    for (iid = 0; iid < num_items; iid++)
    {
        WORD nid = *(const WORD *)(hs + item_off + iid * 2);
        char path[256];
        size_t plen;
        const BYTE *ptr = NULL;
        DWORD sz = 0;

        pri_build_node_path( hs, hs_sz, node_off, num_nodes, pool_off, nid, path, sizeof(path) );
        plen = strlen( path );
        if (plen < 4 || _stricmp( path + plen - 4, ".xbf" )) continue;
        if (pri_resolve_item_data( pri, pri_sz, rm, rm_sz, count1, extra_sz, map_off, cand_off,
                                   iid, &ptr, &sz, NULL ) && ptr && sz > 0x50 && !memcmp( ptr, "XBF", 4 ))
        {
            if (!cb( path, ptr, sz, ctx )) break;
        }
    }
}

struct pri_xbf_entry
{
    char path[256];
    const char *base;
    const BYTE *data;
    DWORD size;
};

struct pri_xbf_catalog g_pri_cat = {0};
static INIT_ONCE g_pri_xbf_cat_once = INIT_ONCE_STATIC_INIT;

struct pri_xbf_catalog g_winui_pri_cat = {0};
BOOL g_winui_pri_cat_done = FALSE;

BOOL xbf_is_immutable_pri_blob( const BYTE *xbf, DWORD xbf_sz )
{
    DWORD k;
    if (!xbf || !xbf_sz) return FALSE;
    if (g_pri_data && xbf >= g_pri_data && xbf_sz <= g_pri_size &&
        (DWORD)(xbf - g_pri_data) <= g_pri_size - xbf_sz)
        return TRUE;
    if (g_winui_pri_data && xbf >= g_winui_pri_data && xbf_sz <= g_winui_pri_size &&
        (DWORD)(xbf - g_winui_pri_data) <= g_winui_pri_size - xbf_sz)
        return TRUE;
    for (k = 0; k < g_pri_cat.cnt; k++)
    {
        const BYTE *d = g_pri_cat.entries[k].data;
        DWORD sz = g_pri_cat.entries[k].size;
        if (d && xbf >= d && xbf_sz <= sz && (DWORD)(xbf - d) <= sz - xbf_sz)
            return TRUE;
    }
    for (k = 0; k < g_winui_pri_cat.cnt; k++)
    {
        const BYTE *d = g_winui_pri_cat.entries[k].data;
        DWORD sz = g_winui_pri_cat.entries[k].size;
        if (d && xbf >= d && xbf_sz <= sz && (DWORD)(xbf - d) <= sz - xbf_sz)
            return TRUE;
    }
    return FALSE;
}

static void pri_cat_rehash( struct pri_xbf_catalog *cat, DWORD new_size )
{
    int *ps = calloc( new_size, sizeof(int) );
    int *bs = calloc( new_size, sizeof(int) );
    DWORD mask = new_size - 1, k;
    if (!ps || !bs)
    {
        free( ps );
        free( bs );
        return;
    }
    for (k = 0; k < cat->cnt; k++)
    {
        UINT32 slot;
        const char *slash = strrchr( cat->entries[k].path, '/' );
        cat->entries[k].base = slash ? slash + 1 : cat->entries[k].path;

        slot = pri_hash_str_ci( cat->entries[k].path ) & mask;
        while (ps[slot])
        {
            if (!_stricmp( cat->entries[ps[slot] - 1].path, cat->entries[k].path ))
                break;
            slot = (slot + 1) & mask;
        }
        if (!ps[slot]) ps[slot] = (int)(k + 1);

        slot = pri_hash_str_ci( cat->entries[k].base ) & mask;
        while (bs[slot])
        {
            if (!_stricmp( cat->entries[bs[slot] - 1].base, cat->entries[k].base ))
                break;
            slot = (slot + 1) & mask;
        }
        if (!bs[slot]) bs[slot] = (int)(k + 1);
    }
    free( cat->path_slots );
    free( cat->base_slots );
    cat->path_slots = ps;
    cat->base_slots = bs;
    cat->hash_mask = mask;
}

BOOL pri_xbf_collect_cb( const char *path, const BYTE *xbf, DWORD sz, void *ctx )
{
    struct pri_xbf_catalog *cat = ctx;
    const char *slash;
    BOOL moved = FALSE;
    if (cat->cnt >= cat->cap)
    {
        DWORD ncap = cat->cap ? cat->cap * 2 : 64;
        struct pri_xbf_entry *ne = realloc( cat->entries, ncap * sizeof(*ne) );
        if (!ne) return FALSE;
        moved = (ne != cat->entries);
        cat->entries = ne;
        cat->cap = ncap;
        if (moved)
        {
            DWORD k;
            for (k = 0; k < cat->cnt; k++)
            {
                const char *sl = strrchr( cat->entries[k].path, '/' );
                cat->entries[k].base = sl ? sl + 1 : cat->entries[k].path;
            }
        }
    }
    snprintf( cat->entries[cat->cnt].path, sizeof(cat->entries[0].path), "%s", path );
    slash = strrchr( cat->entries[cat->cnt].path, '/' );
    cat->entries[cat->cnt].base = slash ? slash + 1 : cat->entries[cat->cnt].path;
    cat->entries[cat->cnt].data = xbf;
    cat->entries[cat->cnt].size = sz;
    cat->cnt++;
    if (!cat->path_slots || cat->cnt * 10 >= (cat->hash_mask + 1) * 6)
    {
        DWORD nsz = cat->path_slots ? (cat->hash_mask + 1) * 2 : 128;
        while (cat->cnt * 10 >= nsz * 6) nsz *= 2;
        pri_cat_rehash( cat, nsz );
    }
    else
    {
        DWORD k = cat->cnt - 1;
        DWORD mask = cat->hash_mask;
        UINT32 slot = pri_hash_str_ci( cat->entries[k].path ) & mask;
        while (cat->path_slots[slot])
        {
            if (!_stricmp( cat->entries[cat->path_slots[slot] - 1].path, cat->entries[k].path ))
                break;
            slot = (slot + 1) & mask;
        }
        if (!cat->path_slots[slot]) cat->path_slots[slot] = (int)(k + 1);

        slot = pri_hash_str_ci( cat->entries[k].base ) & mask;
        while (cat->base_slots[slot])
        {
            if (!_stricmp( cat->entries[cat->base_slots[slot] - 1].base, cat->entries[k].base ))
                break;
            slot = (slot + 1) & mask;
        }
        if (!cat->base_slots[slot]) cat->base_slots[slot] = (int)(k + 1);
    }
    return TRUE;
}

void scan_disk_xbfs_in_dir( const WCHAR *dir_path, const char *rel_prefix, struct pri_xbf_catalog *cat )
{
    WCHAR pattern[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hfind;

    if (wcslen( dir_path ) + 7 >= ARRAY_SIZE(pattern)) return;
    swprintf( pattern, ARRAY_SIZE(pattern), L"%s\\*.xbf", dir_path );
    hfind = FindFirstFileW( pattern, &fd );
    if (hfind == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcslen( dir_path ) + 1 + wcslen( fd.cFileName ) < ARRAY_SIZE(full))
        {
            HANDLE hfile, hmap;
            DWORD sz;
            swprintf( full, ARRAY_SIZE(full), L"%s\\%s", dir_path, fd.cFileName );
            hfile = CreateFileW( full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
            if (hfile != INVALID_HANDLE_VALUE)
            {
                sz = GetFileSize( hfile, NULL );
                if (sz > 0x50 && sz < 16 * 1024 * 1024 &&
                    (hmap = CreateFileMappingW( hfile, NULL, PAGE_READONLY, 0, 0, NULL )))
                {
                    const BYTE *mapped = MapViewOfFile( hmap, FILE_MAP_READ, 0, 0, 0 );
                    if (mapped && !memcmp( mapped, "XBF", 4 ))
                    {
                        char name_a[128] = {0}, vpath[256];
                        WideCharToMultiByte( CP_UTF8, 0, fd.cFileName, -1, name_a, sizeof(name_a) - 1, NULL, NULL );
                        if (rel_prefix && rel_prefix[0])
                            snprintf( vpath, sizeof(vpath), "Files/%s/%s", rel_prefix, name_a );
                        else
                            snprintf( vpath, sizeof(vpath), "Files/%s", name_a );
                        pri_xbf_collect_cb( vpath, mapped, sz, cat );
                    }
                    else if (mapped) UnmapViewOfFile( mapped );
                    CloseHandle( hmap );
                }
                CloseHandle( hfile );
            }
        }
    } while (FindNextFileW( hfind, &fd ));
    FindClose( hfind );
}

static int pri_cat_lookup_path( const struct pri_xbf_catalog *cat, const char *path )
{
    if (!cat || !cat->cnt || !path || !path[0]) return -1;
    if (cat->path_slots)
    {
        DWORD mask = cat->hash_mask;
        UINT32 slot = pri_hash_str_ci( path ) & mask;
        int v;
        while ((v = cat->path_slots[slot]) != 0)
        {
            if (!_stricmp( cat->entries[v - 1].path, path ))
                return v - 1;
            slot = (slot + 1) & mask;
        }
        return -1;
    }
    {
        DWORD k;
        for (k = 0; k < cat->cnt; k++)
            if (!_stricmp( cat->entries[k].path, path )) return (int)k;
    }
    return -1;
}

static int pri_cat_lookup_base( const struct pri_xbf_catalog *cat, const char *base )
{
    if (!cat || !cat->cnt || !base || !base[0]) return -1;
    if (cat->base_slots)
    {
        DWORD mask = cat->hash_mask;
        UINT32 slot = pri_hash_str_ci( base ) & mask;
        int v;
        while ((v = cat->base_slots[slot]) != 0)
        {
            if (!_stricmp( cat->entries[v - 1].base, base ))
                return v - 1;
            slot = (slot + 1) & mask;
        }
        return -1;
    }
    {
        DWORD k;
        for (k = 0; k < cat->cnt; k++)
        {
            const char *slash = strrchr( cat->entries[k].path, '/' );
            const char *b = slash ? slash + 1 : cat->entries[k].path;
            if (!_stricmp( b, base )) return (int)k;
        }
    }
    return -1;
}

static BOOL pri_cat_find_xbf( struct pri_xbf_catalog *cat, const char *base_xbf_dir,
                              const WCHAR *uri_or_name, const BYTE **out_ptr, DWORD *out_sz,
                              char *out_resolved, size_t out_resolved_sz )
{
    char full_path[256] = "Files/";
    char rel_path[256] = {0};
    char suffix[256] = {0};
    const WCHAR *p = uri_or_name;
    size_t i, len;
    int hit;
    BOOL is_appx = FALSE;

    if (!cat || !uri_or_name || !out_ptr || !out_sz) return FALSE;
    *out_ptr = NULL;
    *out_sz = 0;
    if (out_resolved && out_resolved_sz) out_resolved[0] = 0;

    if (!wcsncmp( p, L"ms-appx:///", 11 )) { p += 11; is_appx = TRUE; }
    else if (!wcsncmp( p, L"ms-appx://", 10 )) { p += 10; is_appx = TRUE; }
    while (*p == '/' || *p == '\\') { p++; is_appx = TRUE; }

    len = wcslen( p );
    for (i = 0; i < len && i + 7 < sizeof(full_path) - 5; i++)
        full_path[6 + i] = (p[i] == '\\') ? '/' : (char)p[i];
    full_path[6 + i] = 0;

    len = strlen( full_path );
    if (len >= 5 && !_stricmp( full_path + len - 5, ".xaml" ))
        memcpy( full_path + len - 5, ".xbf", 5 );
    else if ((len < 4 || _stricmp( full_path + len - 4, ".xbf" )) && len + 5 <= sizeof(full_path))
        memcpy( full_path + len, ".xbf", 5 );

    if (!is_appx && base_xbf_dir && base_xbf_dir[0])
    {
        snprintf( rel_path, sizeof(rel_path), "%s%s", base_xbf_dir, full_path + 6 );
        hit = pri_cat_lookup_path( cat, rel_path );
        if (hit >= 0)
        {
            *out_ptr = cat->entries[hit].data;
            *out_sz = cat->entries[hit].size;
            if (out_resolved && out_resolved_sz)
                snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[hit].path );
            TRACE( "Resolved PRI XBF rel '%s' -> %p (%lu bytes)\n",
                   cat->entries[hit].path, *out_ptr, (unsigned long)*out_sz );
            return TRUE;
        }
    }

    {
        const char *slash = strrchr( full_path, '/' );
        snprintf( suffix, sizeof(suffix), "%s", slash ? slash + 1 : full_path );
    }

    hit = pri_cat_lookup_path( cat, full_path );
    if (hit >= 0)
    {
        *out_ptr = cat->entries[hit].data;
        *out_sz = cat->entries[hit].size;
        if (out_resolved && out_resolved_sz)
            snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[hit].path );
        TRACE( "Resolved PRI XBF '%s' (suffix '%s') -> %p (%lu bytes)\n",
               full_path, suffix, *out_ptr, (unsigned long)*out_sz );
        return TRUE;
    }

    hit = pri_cat_lookup_base( cat, suffix );
    if (hit >= 0)
    {
        *out_ptr = cat->entries[hit].data;
        *out_sz = cat->entries[hit].size;
        if (out_resolved && out_resolved_sz)
            snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[hit].path );
        TRACE( "Resolved PRI XBF '%s' (suffix '%s') -> %p (%lu bytes)\n",
               cat->entries[hit].path, suffix, *out_ptr, (unsigned long)*out_sz );
        return TRUE;
    }
    return FALSE;
}

static void uwp_pri_ensure_xbf_cat( void )
{
    InitOnceExecuteOnce( &g_pri_xbf_cat_once, init_pri_xbf_cat_once, NULL, NULL );
}

BOOL uwp_pri_get_cat_entry( DWORD idx, const char **out_path, const BYTE **out_data, DWORD *out_sz )
{
    uwp_pri_ensure_xbf_cat();
    if (idx >= g_pri_cat.cnt) return FALSE;
    if (out_path) *out_path = g_pri_cat.entries[idx].path;
    if (out_data) *out_data = g_pri_cat.entries[idx].data;
    if (out_sz) *out_sz = g_pri_cat.entries[idx].size;
    return TRUE;
}

BOOL uwp_pri_get_winui_cat_entry( DWORD idx, const char **out_path, const BYTE **out_data, DWORD *out_sz )
{
    ensure_winui_pri_cat();
    if (idx >= g_winui_pri_cat.cnt) return FALSE;
    if (out_path) *out_path = g_winui_pri_cat.entries[idx].path;
    if (out_data) *out_data = g_winui_pri_cat.entries[idx].data;
    if (out_sz) *out_sz = g_winui_pri_cat.entries[idx].size;
    return TRUE;
}

BOOL uwp_pri_find_xbf_rel( const char *base_xbf_dir, const WCHAR *uri_or_name,
                                  const BYTE **out_ptr, DWORD *out_sz,
                                  char *out_resolved, size_t out_resolved_sz )
{
    uwp_pri_ensure_xbf_cat();
    if (pri_cat_find_xbf( &g_pri_cat, base_xbf_dir, uri_or_name, out_ptr, out_sz, out_resolved, out_resolved_sz ))
        return TRUE;
    ensure_winui_pri_cat();
    if (g_winui_pri_cat.cnt > 0 &&
        pri_cat_find_xbf( &g_winui_pri_cat, base_xbf_dir, uri_or_name, out_ptr, out_sz, out_resolved, out_resolved_sz ))
        return TRUE;
    return FALSE;
}

BOOL uwp_pri_find_xbf( const WCHAR *uri_or_name, const BYTE **out_ptr, DWORD *out_sz )
{
    return uwp_pri_find_xbf_rel( NULL, uri_or_name, out_ptr, out_sz, NULL, 0 );
}

static BOOL pri_decode_string_bytes( const BYTE *ptr, DWORD sz, BYTE vti, WCHAR *out_buf, int out_chars )
{
    if (!ptr || !sz || !out_buf || out_chars <= 0) return FALSE;
    out_buf[0] = 0;
    if (vti == 0 || (sz >= 2 && (sz % 2) == 0 && ptr[1] == 0 && ptr[sz - 1] == 0))
    {
        int wlen = (int)(sz / 2);
        if (wlen >= out_chars) wlen = out_chars - 1;
        memcpy( out_buf, ptr, wlen * sizeof(WCHAR) );
        out_buf[wlen] = 0;
    }
    else
    {
        int n = MultiByteToWideChar( CP_UTF8, 0, (const char *)ptr, (int)sz, out_buf, out_chars - 1 );
        if (n < 0) n = 0;
        out_buf[n] = 0;
    }
    return out_buf[0] != 0;
}

void uwp_pri_enum_uid_props( const WCHAR *uid,
                             void (*cb)( const WCHAR *prop_rel, const WCHAR *val, void *user ),
                             void *user )
{
    char uid_a[256] = {0};
    size_t ulen;
    UINT32 uslot, cur;

    if (!uid || !uid[0] || !cb) return;
    InitOnceExecuteOnce( &g_pri_item_idx_once, init_pri_item_index, NULL, NULL );
    if (!g_pri_item_idx.valid) return;

    WideCharToMultiByte( CP_UTF8, 0, uid, -1, uid_a, sizeof(uid_a) - 1, NULL, NULL );
    ulen = strlen( uid_a );
    if (!ulen) return;

    uslot = pri_hash_str_ci_n( uid_a, ulen ) & g_pri_item_idx.hash_mask;
    for (cur = g_pri_item_idx.res_uid_heads[uslot]; cur != 0; cur = g_pri_item_idx.res_uid_nexts[cur - 1])
    {
        DWORD iid = cur - 1;
        const BYTE *ptr = NULL;
        DWORD sz = 0;
        BYTE vti = 0xff;
        WCHAR prop_rel_w[256], val_w[256];
        int pn;

        if (g_pri_item_idx.res_uid_lens[iid] != ulen) continue;
        if (_strnicmp( g_pri_item_idx.res_uids[iid], uid_a, ulen )) continue;
        if (!pri_resolve_item_data( g_pri_item_idx.pri, g_pri_item_idx.pri_sz,
                                    g_pri_item_idx.rm, g_pri_item_idx.rm_sz,
                                    g_pri_item_idx.count1, g_pri_item_idx.extra_sz,
                                    g_pri_item_idx.map_off, g_pri_item_idx.cand_off,
                                    iid, &ptr, &sz, &vti ))
            continue;
        if (!pri_decode_string_bytes( ptr, sz, vti, val_w, ARRAY_SIZE(val_w) ))
            continue;
        pn = MultiByteToWideChar( CP_UTF8, 0, g_pri_item_idx.res_prop_rels[iid], -1,
                                  prop_rel_w, ARRAY_SIZE(prop_rel_w) );
        if (pn <= 0) continue;
        TRACE( "PRI uid '%s' prop '%s' -> %s\n", uid_a, g_pri_item_idx.res_prop_rels[iid], debugstr_w(val_w) );
        cb( prop_rel_w, val_w, user );
    }
}

BOOL uwp_pri_lookup_string( const WCHAR *res_name, WCHAR *out_buf, int out_chars )
{
    char full_path[256] = "Resources/";
    char suffix[256] = {0};
    const BYTE *ptr = NULL;
    DWORD sz = 0;
    BYTE vti = 0xff;
    size_t i, len;

    if (!res_name || !out_buf || out_chars <= 0) return FALSE;
    out_buf[0] = 0;

    len = wcslen( res_name );
    for (i = 0; i < len && i + 11 < sizeof(full_path) - 1; i++)
        full_path[10 + i] = (res_name[i] == '\\') ? '/' : (char)res_name[i];
    full_path[10 + i] = 0;
    snprintf( suffix, sizeof(suffix), "%s", full_path + 10 );

    if (!pri_find_item_by_path( full_path, suffix, &ptr, &sz, &vti ) || !ptr || !sz)
        return FALSE;

    if (!pri_decode_string_bytes( ptr, sz, vti, out_buf, out_chars ))
        return FALSE;
    TRACE( "PRI string '%s' -> %s\n", full_path, debugstr_w(out_buf) );
    return TRUE;
}
