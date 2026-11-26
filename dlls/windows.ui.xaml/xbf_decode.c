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
                lstrcatW( pri_path, L"resources.pri" );
                pri_map_file_to( pri_path, &g_winui_pri_data, &g_winui_pri_size );
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

static DWORD pri_select_english_cand_offset( const BYTE *pri, DWORD pri_sz, DWORD dec )
{
    const BYTE *decn_sec = NULL, *d;
    DWORD decn_sz = 0, size, qualin_at, dq_at, idx_at, val_at;
    WORD ndist, nqual, nqset, ndec, nidx, dec_first, dec_n, j, k;
    DWORD fallback_en = (~0u);

    if (!pri_get_section( pri, pri_sz, 0, &decn_sec, &decn_sz ) || decn_sz < 0x2c)
        return 0;
    if (memcmp( decn_sec, "[mrm_decn_info]", 15 ) != 0)
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
            if (!wcsicmp( val_str, L"en-US" ))
                return j;
            if (fallback_en == (~0u) && (!wcsnicmp( val_str, L"en-", 3 ) || !wcsicmp( val_str, L"en" )))
                fallback_en = j;
        }
    }
    return (fallback_en != (~0u)) ? fallback_en : 0;
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
    cand_idx = cand_base + pri_select_english_cand_offset( pri, pri_sz, dec );
    if (cand_off + (cand_idx + 1) * 8 > rm_sz) cand_idx = cand_base;
    if (cand_off + (cand_idx + 1) * 8 > rm_sz) return FALSE;
    if (out_vti) *out_vti = *(const BYTE *)(rm + cand_off + cand_idx * 8 + 1);
    data_idx = *(const WORD *)(rm + cand_off + cand_idx * 8 + 4);
    sec_idx = *(const WORD *)(rm + cand_off + cand_idx * 8 + 6);
    return pri_get_data_item( pri, pri_sz, sec_idx, data_idx, out_ptr, out_sz );
}

static BOOL pri_find_item_by_path( const char *want_path, const char *want_suffix,
                                   const BYTE **out_ptr, DWORD *out_sz, BYTE *out_vti )
{
    DWORD pri_sz = 0, hs_sz = 0, rm_sz = 0;
    const BYTE *pri = pri_get_data( &pri_sz );
    const BYTE *hs = NULL, *rm = NULL;
    DWORD num_nodes, num_scopes, num_items;
    DWORD node_off, scope_off, item_off, pool_off, map_off, cand_off;
    DWORD count1, extra_sz;
    WORD l1, l2, num_env;
    DWORD hs_base;
    int matched_item = -1, fallback_item = -1;
    DWORD iid;

    if (!pri) return FALSE;
    if (!pri_get_section( pri, pri_sz, 2, &hs, &hs_sz ) || hs_sz < 0xd8) return FALSE;
    if (!pri_get_section( pri, pri_sz, 3, &rm, &rm_sz ) || rm_sz < 0x80) return FALSE;

    l1 = *(const WORD *)(hs + 0x22);
    l2 = *(const WORD *)(hs + 0x24);
    hs_base = ((0x48 + ((DWORD)l1 + (DWORD)l2) * 2 + 3) & ~3u) + 8;
    if (hs_base + 0x18 > hs_sz) return FALSE;

    num_nodes = *(const DWORD *)(hs + hs_base);
    num_scopes = *(const DWORD *)(hs + hs_base + 4);
    num_items = *(const DWORD *)(hs + hs_base + 8);
    node_off = hs_base + 0x18;
    scope_off = node_off + num_nodes * 12;
    item_off = scope_off + num_scopes * 8;
    pool_off = item_off + num_items * 2 + *(const DWORD *)(hs + hs_base + 12) * 2;
    if (pool_off >= hs_sz) return FALSE;

    num_env = *(const WORD *)(rm + 0x2a);
    count1 = *(const DWORD *)(rm + 0x30);
    extra_sz = *(const DWORD *)(rm + 0x3c);
    map_off = 0x40 + (DWORD)num_env * 8 + 8;
    cand_off = map_off + count1 * 4 + extra_sz;
    if (cand_off >= rm_sz) return FALSE;

    for (iid = 0; iid < num_items; iid++)
    {
        WORD nid = *(const WORD *)(hs + item_off + iid * 2);
        char path[256];
        pri_build_node_path( hs, hs_sz, node_off, num_nodes, pool_off, nid, path, sizeof(path) );
        if (want_path && !strcasecmp( path, want_path ))
        {
            matched_item = (int)iid;
            break;
        }
        if (want_suffix && fallback_item < 0)
        {
            size_t plen = strlen( path ), slen = strlen( want_suffix );
            if (plen >= slen && !strcasecmp( path + plen - slen, want_suffix ) &&
                (plen == slen || path[plen - slen - 1] == '/'))
            {
                fallback_item = (int)iid;
            }
        }
    }

    if (matched_item < 0) matched_item = fallback_item;
    if (matched_item < 0) return FALSE;

    return pri_resolve_item_data( pri, pri_sz, rm, rm_sz, count1, extra_sz, map_off, cand_off,
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
    if (!pri_get_section( pri, pri_sz, 2, &hs, &hs_sz ) || hs_sz < 0xd8) return;
    if (!pri_get_section( pri, pri_sz, 3, &rm, &rm_sz ) || rm_sz < 0x80) return;

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
        if (plen < 4 || strcasecmp( path + plen - 4, ".xbf" )) continue;
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

BOOL pri_xbf_collect_cb( const char *path, const BYTE *xbf, DWORD sz, void *ctx )
{
    struct pri_xbf_catalog *cat = ctx;
    const char *slash;
    if (cat->cnt >= cat->cap)
    {
        DWORD ncap = cat->cap ? cat->cap * 2 : 64;
        struct pri_xbf_entry *ne = realloc( cat->entries, ncap * sizeof(*ne) );
        if (!ne) return FALSE;
        cat->entries = ne;
        cat->cap = ncap;
    }
    snprintf( cat->entries[cat->cnt].path, sizeof(cat->entries[0].path), "%s", path );
    slash = strrchr( cat->entries[cat->cnt].path, '/' );
    cat->entries[cat->cnt].base = slash ? slash + 1 : cat->entries[cat->cnt].path;
    cat->entries[cat->cnt].data = xbf;
    cat->entries[cat->cnt].size = sz;
    cat->cnt++;
    return TRUE;
}

void scan_disk_xbfs_in_dir( const WCHAR *dir_path, const char *rel_prefix, struct pri_xbf_catalog *cat )
{
    WCHAR pattern[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hfind;

    if (wcslen( dir_path ) + 7 >= ARRAY_SIZE(pattern)) return;
    lstrcpyW( pattern, dir_path );
    lstrcatW( pattern, L"\\*.xbf" );
    hfind = FindFirstFileW( pattern, &fd );
    if (hfind == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcslen( dir_path ) + 1 + wcslen( fd.cFileName ) < ARRAY_SIZE(full))
        {
            HANDLE hfile, hmap;
            DWORD sz;
            lstrcpyW( full, dir_path );
            lstrcatW( full, L"\\" );
            lstrcatW( full, fd.cFileName );
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

static BOOL pri_cat_find_xbf( struct pri_xbf_catalog *cat, const char *base_xbf_dir,
                              const WCHAR *uri_or_name, const BYTE **out_ptr, DWORD *out_sz,
                              char *out_resolved, size_t out_resolved_sz )
{
    char full_path[256] = "Files/";
    char rel_path[256] = {0};
    char suffix[256] = {0};
    const WCHAR *p = uri_or_name;
    size_t i, len;
    DWORD k;
    int fallback = -1;
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

    /* Replace .xaml with .xbf, or append .xbf */
    len = strlen( full_path );
    if (len >= 5 && !strcasecmp( full_path + len - 5, ".xaml" ))
        strcpy( full_path + len - 5, ".xbf" );
    else if (len < 4 || strcasecmp( full_path + len - 4, ".xbf" ))
        strcat( full_path, ".xbf" );

    if (!is_appx && base_xbf_dir && base_xbf_dir[0])
    {
        snprintf( rel_path, sizeof(rel_path), "%s%s", base_xbf_dir, full_path + 6 );
        for (k = 0; k < cat->cnt; k++)
        {
            if (!strcasecmp( cat->entries[k].path, rel_path ))
            {
                *out_ptr = cat->entries[k].data;
                *out_sz = cat->entries[k].size;
                if (out_resolved && out_resolved_sz)
                    snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[k].path );
                TRACE( "Resolved PRI XBF rel '%s' -> %p (%lu bytes)\n",
                       cat->entries[k].path, *out_ptr, (unsigned long)*out_sz );
                return TRUE;
            }
        }
    }

    {
        const char *slash = strrchr( full_path, '/' );
        snprintf( suffix, sizeof(suffix), "%s", slash ? slash + 1 : full_path );
    }

    for (k = 0; k < cat->cnt; k++)
    {
        if (!strcasecmp( cat->entries[k].path, full_path ))
        {
            *out_ptr = cat->entries[k].data;
            *out_sz = cat->entries[k].size;
            if (out_resolved && out_resolved_sz)
                snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[k].path );
            TRACE( "Resolved PRI XBF '%s' (suffix '%s') -> %p (%lu bytes)\n",
                   full_path, suffix, *out_ptr, (unsigned long)*out_sz );
            return TRUE;
        }
        if (fallback < 0)
        {
            const char *slash = strrchr( cat->entries[k].path, '/' );
            const char *base = slash ? slash + 1 : cat->entries[k].path;
            if (!strcasecmp( base, suffix ))
                fallback = (int)k;
        }
    }
    if (fallback >= 0)
    {
        *out_ptr = cat->entries[fallback].data;
        *out_sz = cat->entries[fallback].size;
        if (out_resolved && out_resolved_sz)
            snprintf( out_resolved, out_resolved_sz, "%s", cat->entries[fallback].path );
        TRACE( "Resolved PRI XBF '%s' (suffix '%s') -> %p (%lu bytes)\n",
               cat->entries[fallback].path, suffix, *out_ptr, (unsigned long)*out_sz );
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
    TRACE( "PRI string '%s' -> %s\n", full_path, debugstr_w(out_buf) );
    return out_buf[0] != 0;
}

/* ============================================================================
 * 2. PropertyValue Boxing & Unboxing Helpers
 * ============================================================================ */

void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                                   struct uwp_rd_bucket *target_rd );

struct uwp_rd_bucket *g_rd_buckets_head = NULL;
int g_app_rd_sentinel = 0;
int g_theme_rd_sentinel = 0;
BOOL g_app_res_loaded = FALSE;
BOOL g_app_res_loading = FALSE;
char g_loaded_res_xbfs[128][256];
int g_loaded_res_xbf_cnt = 0;

static UINT32 xaml_meta_hash( const WCHAR *s )
{
    UINT32 h = 2166136261u;
    for (; *s; s++)
    {
        h ^= (UINT32)*s;
        h *= 16777619u;
    }
    return h;
}

static inline struct xaml_cached_type *xaml_ct_from_uwp( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_cached_type, lpVtbl );
}

static inline struct xaml_cached_type *xaml_ct_from_winui3( void *iface )
{
    return CONTAINING_RECORD( iface, struct xaml_cached_type, lpVtbl_winui3 );
}

static HRESULT WINAPI xaml_ct_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXamlType_UWP_g ))
    {
        *out = &t->lpVtbl;
        InterlockedIncrement( &t->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IXamlType_WinUI3_g ))
    {
        *out = &t->lpVtbl_winui3;
        InterlockedIncrement( &t->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xaml_ct_AddRef( void *iface )
{
    return InterlockedIncrement( &xaml_ct_from_uwp( iface )->ref );
}

static ULONG WINAPI xaml_ct_Release( void *iface )
{
    /* Singleton-cached in xaml_meta_cache for process lifetime; ref stays >= 1 */
    struct xaml_cached_type *t = xaml_ct_from_uwp( iface );
    LONG r = InterlockedDecrement( &t->ref );
    if (r == 0)
    {
        free( t->full_name );
        free( t );
    }
    return r;
}

static HRESULT WINAPI xaml_ct_winui3_QueryInterface( void *iface, REFIID iid, void **out )
{
    return xaml_ct_QueryInterface( &xaml_ct_from_winui3( iface )->lpVtbl, iid, out );
}

static ULONG WINAPI xaml_ct_winui3_AddRef( void *iface )
{
    return xaml_ct_AddRef( &xaml_ct_from_winui3( iface )->lpVtbl );
}

static ULONG WINAPI xaml_ct_winui3_Release( void *iface )
{
    return xaml_ct_Release( &xaml_ct_from_winui3( iface )->lpVtbl );
}

static HRESULT WINAPI xaml_ct_GetIids( void *iface, ULONG *cnt, IID **iids )
{
    if (cnt) *cnt = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Xaml.Markup.IXamlType", 32, name );
}

static HRESULT WINAPI xaml_ct_GetTrustLevel( void *iface, TrustLevel *lvl )
{
    if (lvl) *lvl = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_NullPtr( void *iface, void **out )
{
    if (out) *out = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_False( void *iface, boolean *out )
{
    if (out) *out = FALSE;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_True( void *iface, boolean *out )
{
    if (out) *out = TRUE;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_FullName( void *iface, HSTRING *name )
{
    struct xaml_cached_type *t = (struct xaml_cached_type *)iface;
    if (!name) return E_POINTER;
    return WindowsCreateString( t->full_name, wcslen( t->full_name ), name );
}

static HRESULT WINAPI xaml_ct_get_Name( void *iface, HSTRING *name )
{
    struct xaml_cached_type *t = (struct xaml_cached_type *)iface;
    if (!name) return E_POINTER;
    return WindowsCreateString( t->short_name, wcslen( t->short_name ), name );
}

static HRESULT WINAPI xaml_ct_ActivateInstance( void *iface, IInspectable **instance )
{
    struct xaml_cached_type *t = (struct xaml_cached_type *)iface;
    HRESULT hr;
    if (!instance) return E_POINTER;
    *instance = NULL;
    hr = uwp_element_create( t->full_name, NULL, NULL, (void **)instance );
    if (SUCCEEDED(hr) && *instance) return hr;
    {
        HSTRING h_cls = NULL;
        if (SUCCEEDED(WindowsCreateString( t->full_name, wcslen( t->full_name ), &h_cls )))
        {
            hr = RoActivateInstance( h_cls, instance );
            WindowsDeleteString( h_cls );
        }
    }
    return hr;
}

static HRESULT WINAPI xaml_ct_CreateFromString( void *iface, HSTRING input, void **value )
{
    if (value) *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI xaml_ct_GetMember( void *iface, HSTRING name, struct xaml_member **member )
{
    if (member) *member = NULL;
    return S_OK;
}

static HRESULT WINAPI xaml_ct_RunInitializer( void *iface )
{
    return S_OK;
}

static HRESULT WINAPI xaml_ct_get_UnderlyingType( void *iface, TypeName *type )
{
    struct xaml_cached_type *t = (struct xaml_cached_type *)iface;
    if (!type) return E_POINTER;
    type->Kind = TypeKind_Metadata;
    return WindowsCreateString( t->full_name, wcslen( t->full_name ), &type->Name );
}

static HRESULT WINAPI xaml_ct_AddToVector( void *iface, IInspectable *instance, IInspectable *value )
{
    return S_OK;
}

static HRESULT WINAPI xaml_ct_AddToMap( void *iface, IInspectable *instance, IInspectable *key, IInspectable *value )
{
    return S_OK;
}

static const void *xaml_cached_type_uwp_vtbl[] =
{
    xaml_ct_QueryInterface,      /* 0 */
    xaml_ct_AddRef,              /* 1 */
    xaml_ct_Release,             /* 2 */
    xaml_ct_GetIids,             /* 3 */
    xaml_ct_GetRuntimeClassName, /* 4 */
    xaml_ct_GetTrustLevel,       /* 5 */
    xaml_ct_get_NullPtr,         /* 6: get_BaseType */
    xaml_ct_get_NullPtr,         /* 7: get_ContentProperty */
    xaml_ct_get_FullName,        /* 8: get_FullName */
    xaml_ct_get_False,           /* 9: get_IsArray */
    xaml_ct_get_False,           /* 10: get_IsCollection */
    xaml_ct_get_True,            /* 11: get_IsConstructible */
    xaml_ct_get_False,           /* 12: get_IsDictionary */
    xaml_ct_get_False,           /* 13: get_IsMarkupExtension */
    xaml_ct_get_False,           /* 14: get_IsBindable */
    xaml_ct_get_NullPtr,         /* 15: get_ItemType */
    xaml_ct_get_NullPtr,         /* 16: get_KeyType */
    xaml_ct_get_UnderlyingType,  /* 17: get_UnderlyingType */
    xaml_ct_ActivateInstance,    /* 18: ActivateInstance (UWP) */
    xaml_ct_CreateFromString,    /* 19: CreateFromString */
    xaml_ct_GetMember,           /* 20: GetMember */
    xaml_ct_AddToVector,         /* 21: AddToVector */
    xaml_ct_AddToMap,            /* 22: AddToMap */
    xaml_ct_RunInitializer,      /* 23: RunInitializer */
};

static HRESULT WINAPI xaml_ct_winui3_get_FullName( void *iface, HSTRING *name )
{ return xaml_ct_get_FullName( &xaml_ct_from_winui3( iface )->lpVtbl, name ); }
static HRESULT WINAPI xaml_ct_winui3_get_Name( void *iface, HSTRING *name )
{ return xaml_ct_get_Name( &xaml_ct_from_winui3( iface )->lpVtbl, name ); }
static HRESULT WINAPI xaml_ct_winui3_ActivateInstance( void *iface, IInspectable **instance )
{ return xaml_ct_ActivateInstance( &xaml_ct_from_winui3( iface )->lpVtbl, instance ); }
static HRESULT WINAPI xaml_ct_winui3_get_UnderlyingType( void *iface, TypeName *type )
{ return xaml_ct_get_UnderlyingType( &xaml_ct_from_winui3( iface )->lpVtbl, type ); }

static const void *xaml_cached_type_winui3_vtbl[] =
{
    xaml_ct_winui3_QueryInterface,      /* 0 */
    xaml_ct_winui3_AddRef,              /* 1 */
    xaml_ct_winui3_Release,             /* 2 */
    xaml_ct_GetIids,                    /* 3 */
    xaml_ct_GetRuntimeClassName,        /* 4 */
    xaml_ct_GetTrustLevel,              /* 5 */
    xaml_ct_get_NullPtr,                /* 6: get_BaseType */
    xaml_ct_get_NullPtr,                /* 7: get_ContentProperty */
    xaml_ct_winui3_get_FullName,        /* 8: get_FullName */
    xaml_ct_get_False,                  /* 9: get_IsArray */
    xaml_ct_get_False,                  /* 10: get_IsCollection */
    xaml_ct_get_True,                   /* 11: get_IsConstructible */
    xaml_ct_get_False,                  /* 12: get_IsDictionary */
    xaml_ct_get_False,                  /* 13: get_IsMarkupExtension */
    xaml_ct_get_False,                  /* 14: get_IsEnum */
    xaml_ct_get_False,                  /* 15: get_IsBindable */
    xaml_ct_get_NullPtr,                /* 16: get_ItemType */
    xaml_ct_get_NullPtr,                /* 17: get_KeyType */
    xaml_ct_winui3_get_Name,            /* 18: get_Name */
    xaml_ct_winui3_ActivateInstance,    /* 19: ActivateInstance (WinUI 3) */
    xaml_ct_CreateFromString,           /* 20: CreateFromString */
    xaml_ct_GetMember,                  /* 21: GetMember */
    xaml_ct_RunInitializer,             /* 22: RunInitializer */
    xaml_ct_winui3_get_UnderlyingType,  /* 23: get_UnderlyingType */
};

static struct xaml_cached_type *xaml_cached_type_create( const WCHAR *full_name, WORD xbf_id )
{
    struct xaml_cached_type *t = calloc( 1, sizeof(*t) );
    const WCHAR *dot;
    if (!t) return NULL;
    t->lpVtbl = xaml_cached_type_uwp_vtbl;
    t->lpVtbl_winui3 = xaml_cached_type_winui3_vtbl;
    t->ref = 1;
    t->full_name = wcsdup( full_name );
    if (!t->full_name) { free( t ); return NULL; }
    dot = wcsrchr( t->full_name, '.' );
    t->short_name = dot ? dot + 1 : t->full_name;
    t->xbf_type_id = xbf_id;
    return t;
}

struct xaml_meta_cache_entry *xaml_meta_insert_locked( const WCHAR *full_name, WORD xbf_id )
{
    UINT32 bucket = xaml_meta_hash( full_name ) & (XAML_META_CACHE_SIZE - 1);
    struct xaml_meta_cache_entry *e;

    for (e = xaml_meta_cache[bucket]; e; e = e->next)
    {
        if (!wcscmp( e->full_name, full_name ))
        {
            if (xbf_id && !e->xbf_type_id) e->xbf_type_id = xbf_id;
            return e;
        }
    }

    e = calloc( 1, sizeof(*e) );
    if (!e) return NULL;
    e->full_name = wcsdup( full_name );
    if (!e->full_name) { free( e ); return NULL; }
    e->xbf_type_id = xbf_id;
    e->next = xaml_meta_cache[bucket];
    xaml_meta_cache[bucket] = e;
    return e;
}

static HRESULT xaml_metadata_cache_get_type( const WCHAR *full_name, void **out )
{
    struct xaml_meta_cache_entry *e;
    struct xaml_cached_type *ct = NULL;
    UINT32 bucket;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!full_name || !full_name[0]) return E_INVALIDARG;

    InitOnceExecuteOnce( &xaml_meta_cache_once, init_xaml_meta_cache, NULL, NULL );
    bucket = xaml_meta_hash( full_name ) & (XAML_META_CACHE_SIZE - 1);

    AcquireSRWLockShared( &xaml_meta_cache_lock );
    for (e = xaml_meta_cache[bucket]; e; e = e->next)
    {
        if (!wcscmp( e->full_name, full_name ) && e->builtin_type)
        {
            ct = e->builtin_type;
            InterlockedIncrement( &ct->ref );
            ReleaseSRWLockShared( &xaml_meta_cache_lock );
            *out = ct;
            return S_OK;
        }
    }
    ReleaseSRWLockShared( &xaml_meta_cache_lock );

    AcquireSRWLockExclusive( &xaml_meta_cache_lock );
    e = xaml_meta_insert_locked( full_name, 0 );
    if (e)
    {
        if (!e->builtin_type)
            e->builtin_type = xaml_cached_type_create( full_name, e->xbf_type_id );
        ct = e->builtin_type;
        if (ct) InterlockedIncrement( &ct->ref );
    }
    ReleaseSRWLockExclusive( &xaml_meta_cache_lock );

    *out = ct;
    return ct ? S_OK : E_OUTOFMEMORY;
}

HRESULT xaml_metadata_cache_get_type_hstring( HSTRING full_name, void **out )
{
    const WCHAR *raw;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!full_name) return S_OK;
    raw = WindowsGetStringRawBuffer( full_name, NULL );
    if (!raw || !raw[0]) return S_OK;
    return xaml_metadata_cache_get_type( raw, out );
}

struct xaml_type *uwp_provider_get_xaml_type_cached( struct xaml_metadata_provider *mp,
                                                            const WCHAR *owner_type_name )
{
    struct xaml_meta_cache_entry *e;
    struct xaml_type *xt = NULL;
    HSTRING h_owner = NULL;
    UINT32 bucket;

    if (!mp || !owner_type_name || !owner_type_name[0]) return NULL;
    InitOnceExecuteOnce( &xaml_meta_cache_once, init_xaml_meta_cache, NULL, NULL );
    bucket = xaml_meta_hash( owner_type_name ) & (XAML_META_CACHE_SIZE - 1);

    AcquireSRWLockShared( &xaml_meta_cache_lock );
    for (e = xaml_meta_cache[bucket]; e; e = e->next)
    {
        if (!wcscmp( e->full_name, owner_type_name ) && e->provider_key == mp && e->provider_type)
        {
            xt = e->provider_type;
            IInspectable_AddRef( (IInspectable *)xt );
            ReleaseSRWLockShared( &xaml_meta_cache_lock );
            return xt;
        }
    }
    ReleaseSRWLockShared( &xaml_meta_cache_lock );

    if (mp->lpVtbl->GetXamlTypeByFullName &&
        SUCCEEDED(WindowsCreateString( owner_type_name, wcslen( owner_type_name ), &h_owner )))
    {
        if (SUCCEEDED(mp->lpVtbl->GetXamlTypeByFullName( mp, h_owner, &xt )) && xt)
        {
            AcquireSRWLockExclusive( &xaml_meta_cache_lock );
            e = xaml_meta_insert_locked( owner_type_name, 0 );
            if (e && !e->provider_type)
            {
                e->provider_key = mp;
                e->provider_type = xt;
                IInspectable_AddRef( (IInspectable *)xt );
            }
            ReleaseSRWLockExclusive( &xaml_meta_cache_lock );
        }
        WindowsDeleteString( h_owner );
    }
    return xt;
}

void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created );

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

void uwp_parse_and_build_xbf( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                     void *connector, BOOL is_uwp_conn, void *app_outer,
                                     int only_sub_idx, struct uwp_element *attach_root,
                                     struct uwp_element **out_created );


typedef LONG (WINAPI *p_RegOpenKeyExW_t)( HKEY, LPCWSTR, DWORD, REGSAM, PHKEY );
typedef LONG (WINAPI *p_RegEnumKeyExW_t)( HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME );
typedef LONG (WINAPI *p_RegQueryValueExW_t)( HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD );
typedef LONG (WINAPI *p_RegCloseKey_t)( HKEY );

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
        if (sv->el->has_brush_argb)
        {
            e->argb = sv->el->brush_argb;
            e->has_argb = TRUE;
        }
        e->opacity = sv->el->brush_opacity;
        if (!e->str_val[0] && sv->el->brush_color_ref[0])
            lstrcpynW( e->str_val, sv->el->brush_color_ref, ARRAY_SIZE(e->str_val) );
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
        if (uwp_rd_find_entry( hint_rd, el, sval, 0, &de, NULL ) && de && de->kind == UWP_RD_DOUBLE)
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

static void xbf_apply_named_property( struct xbf_ctx *ctx, struct uwp_element *el,
                                      const WCHAR *owner_cls, const WCHAR *prop_name,
                                      int vt, double num, const WCHAR *sval, BOOL is_res_ref )
{
    double resolved_d = 0.0;
    BOOL has_resolved_d = FALSE;
    if (!el || !prop_name || !prop_name[0]) return;
    if (!wcscmp( prop_name, L"Visibility" ))
    {
        if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
            el->visibility = !wcsicmp( sval, L"Collapsed" ) ? 1 : 0;
        else
            el->visibility = (int)num;
        return;
    }
    if (!wcscmp( prop_name, L"Opacity" ))
    {
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ))
            el->opacity = resolved_d;
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
    if (!wcscmp( prop_name, L"DisplayMemberPath" ) && sval && sval[0])
    {
        lstrcpynW( el->display_member_path, sval, ARRAY_SIZE(el->display_member_path) );
    }
    else if (!wcscmp( prop_name, L"PaneDisplayMode" ) && uwp_is_nav_view( el ))
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
            el->nav_pane_display_mode = mode;
            el->nav_pane_display_mode_set = TRUE;
            TRACE( "xbf_apply_named_property: NavView(%s) PaneDisplayMode=%d (%s)\n",
                   debugstr_w(el->name), mode, sval ? debugstr_w(sval) : "" );
        }
    }
    else if (!wcscmp( prop_name, L"IsPaneOpen" ) && uwp_is_nav_view( el ))
    {
        if (vt == 0x01 || vt == 0x02) el->nav_is_pane_open = (vt == 0x02);
        else if (sval && sval[0]) el->nav_is_pane_open = !wcsicmp( sval, L"True" );
    }
    else if (!wcscmp( prop_name, L"OpenPaneLength" ) && uwp_is_nav_view( el ))
    {
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ) && resolved_d > 0.0)
        {
            el->nav_open_pane_length = resolved_d;
            has_resolved_d = TRUE;
        }
    }
    else if (!wcscmp( prop_name, L"CompactPaneLength" ) && uwp_is_nav_view( el ))
    {
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ) && resolved_d > 0.0)
        {
            el->nav_compact_pane_length = resolved_d;
            has_resolved_d = TRUE;
        }
    }
    else if (!wcscmp( prop_name, L"CompactModeThresholdWidth" ) && uwp_is_nav_view( el ))
    {
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ) && resolved_d > 0.0)
        {
            el->nav_compact_threshold = resolved_d;
            has_resolved_d = TRUE;
        }
    }
    else if (!wcscmp( prop_name, L"ExpandedModeThresholdWidth" ) && uwp_is_nav_view( el ))
    {
        if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &resolved_d ) && resolved_d > 0.0)
        {
            el->nav_expanded_threshold = resolved_d;
            has_resolved_d = TRUE;
        }
    }
    else if (!wcscmp( prop_name, L"IsSettingsVisible" ) && uwp_is_nav_view( el ))
    {
        if (vt == 0x01 || vt == 0x02) el->nav_is_settings_visible = (vt == 0x02);
        else if (sval && sval[0]) el->nav_is_settings_visible = !wcsicmp( sval, L"True" );
    }
    else if (!wcscmp( prop_name, L"IsPaneToggleButtonVisible" ) && uwp_is_nav_view( el ))
    {
        if (vt == 0x01 || vt == 0x02) el->nav_is_pane_toggle_visible = (vt == 0x02);
        else if (sval && sval[0]) el->nav_is_pane_toggle_visible = !wcsicmp( sval, L"True" );
    }

    if (owner_cls && owner_cls[0] &&
        !wcscmp( owner_cls, L"Microsoft.UI.Xaml.Controls.NavigationView" ) &&
        (!wcscmp( prop_name, L"PaneDisplayMode" ) || !wcscmp( prop_name, L"IsPaneOpen" ) ||
         !wcscmp( prop_name, L"OpenPaneLength" ) || !wcscmp( prop_name, L"CompactPaneLength" ) ||
         !wcscmp( prop_name, L"IsSettingsVisible" ) || !wcscmp( prop_name, L"IsPaneToggleButtonVisible" ) ||
         !wcscmp( prop_name, L"CompactModeThresholdWidth" ) || !wcscmp( prop_name, L"ExpandedModeThresholdWidth" )))
    {
        void *ao = (ctx && ctx->app_outer) ? ctx->app_outer : uwp_get_app_outer();
        if (ao)
        {
            IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;
            if (has_resolved_d)
            {
                WCHAR nbuf[64];
                swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", resolved_d );
                uwp_set_custom_xaml_member( ao, owner_cls, prop_name, target, nbuf );
            }
            else if (vt == 0x01 || vt == 0x02)
                uwp_set_custom_xaml_member( ao, owner_cls, prop_name, target, (vt == 0x02) ? L"True" : L"False" );
            else if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
                uwp_set_custom_xaml_member( ao, owner_cls, prop_name, target, sval );
            else if (vt == 0x03 || vt == 0x04 || vt == 0x0b)
            {
                if (!wcscmp( prop_name, L"PaneDisplayMode" ))
                {
                    static const WCHAR *const modes[] = { L"Auto", L"Left", L"Top", L"LeftCompact", L"LeftMinimal" };
                    int m = (int)num;
                    uwp_set_custom_xaml_member( ao, owner_cls, prop_name, target,
                                                (m >= 0 && m < 5) ? modes[m] : L"Auto" );
                }
                else
                {
                    WCHAR nbuf[64];
                    swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", num );
                    uwp_set_custom_xaml_member( ao, owner_cls, prop_name, target, nbuf );
                }
            }
        }
    }
}

void uwp_apply_style_entry( struct xbf_ctx *ctx, struct uwp_element *el,
                                   struct uwp_rd_bucket *b, struct uwp_rd_entry *se, int depth )
{
    int i;
    double saved_fs;
    BOOL saved_has_fs;
    int saved_fw;
    WCHAR saved_ff[128];
    if (!el || !se || se->kind != UWP_RD_STYLE || depth > 8) return;
    saved_fs = el->font_size;
    saved_has_fs = el->has_font_size;
    saved_fw = el->font_weight;
    lstrcpynW( saved_ff, el->font_family, ARRAY_SIZE(saved_ff) );
    if (uwp_style_is_accent_name( se->key ))
        el->is_accent_style = TRUE;
    if (se->str_val[0])
    {
        struct uwp_rd_entry *base_se = NULL;
        struct uwp_rd_bucket *base_b = NULL;
        if (uwp_style_is_accent_name( se->str_val ))
            el->is_accent_style = TRUE;
        if (uwp_rd_find_entry( b, el, se->str_val, 0, &base_se, &base_b ) && base_se)
            uwp_apply_style_entry( ctx, el, base_b, base_se, depth + 1 );
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
            xbf_apply_named_property( ctx, el, st->owner_cls, st->prop_name, st->vt, st->num, st->str, st->is_res_ref );
        else
            xbf_apply_property( ctx, el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
    }
    /* Local typography values take precedence over Style setters regardless of XBF attribute order */
    if (saved_has_fs)
    {
        el->font_size = saved_fs;
        el->has_font_size = TRUE;
    }
    if (saved_fw > 0)
        el->font_weight = saved_fw;
    if (saved_ff[0])
        lstrcpynW( el->font_family, saved_ff, ARRAY_SIZE(el->font_family) );
}

void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                                WORD pid, int vt, double num, const WCHAR *sval,
                                const double vec4[4], BOOL is_res_ref )
{
    UINT32 argb = 0;

    if (sv)
    {
        if (sv->kind == UWP_RD_BRUSH)
        {
            if (pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR || pid == XBF_PROP_FALLBACK_COLOR) /* SolidColorBrush.Color / RevealBrush.Color */
            {
                if (vt == 0x08)
                {
                    sv->argb = (UINT32)num;
                    sv->has_argb = TRUE;
                }
                else if (sval && sval[0])
                {
                    if (uwp_parse_color_string( sval, &argb ))
                    {
                        sv->argb = argb;
                        sv->has_argb = TRUE;
                    }
                    else
                    {
                        lstrcpynW( sv->str_val, sval, ARRAY_SIZE(sv->str_val) );
                    }
                }
            }
            else if (pid == XBF_PROP_BRUSH_OPACITY) /* Brush.Opacity */
            {
                if (vt == 0x03 || vt == 0x04)
                    sv->opacity = num;
                else if (sval && sval[0])
                {
                    struct uwp_rd_entry *de = NULL;
                    if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                        de && de->kind == UWP_RD_DOUBLE)
                        sv->opacity = de->dval;
                }
            }
        }
        else if (sv->kind == UWP_RD_STYLE && pid == XBF_PROP_BASED_ON && sval && sval[0]) /* Style.BasedOn */
        {
            uwp_extract_resource_key( sval, sv->based_on, ARRAY_SIZE(sv->based_on) );
        }
        else if (sv->kind == UWP_RD_COLOR && pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR && vt == 0x08)
        {
            sv->argb = (UINT32)num;
            sv->has_argb = TRUE;
        }
    }

    if (!el)
    {
        if (sv && sv->raw && ctx && !(pid & 0x8000) && pid < ctx->prop_cnt &&
            ctx->props[pid].name[0] && ctx->props[pid].owner_tid < ctx->type_cnt &&
            ctx->types[ctx->props[pid].owner_tid].full_name[0] &&
            (wcsstr( ctx->types[ctx->props[pid].owner_tid].full_name, L"Breakpoint" ) ||
             wcsstr( ctx->types[ctx->props[pid].owner_tid].full_name, L"Converter" ) ||
             wcsstr( ctx->types[ctx->props[pid].owner_tid].full_name, L"Layout" )))
        {
            void *ao = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();
            const struct xbf_prop_info *pi = &ctx->props[pid];
            const WCHAR *owner_cls = ctx->types[pi->owner_tid].full_name;
            if (ao)
            {
                if (is_res_ref && sval && sval[0])
                {
                    struct uwp_rd_entry *re = NULL;
                    struct uwp_rd_bucket *found_b = NULL;
                    IInspectable *res_val = NULL;
                    BOOL release_res = FALSE;
                    if (uwp_rd_find_entry( ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd,
                                           ctx->root_el, sval, 0, &re, &found_b ) && re)
                    {
                        if (re->kind == UWP_RD_DOUBLE)
                        {
                            WCHAR nbuf[64];
                            swprintf( nbuf, ARRAY_SIZE(nbuf), L"%g", re->dval );
                            uwp_set_custom_xaml_member( ao, owner_cls, pi->name, sv->raw, nbuf );
                        }
                        else
                        {
                            res_val = uwp_rd_materialize_entry( found_b, re );
                        }
                    }
                    else if (wcsstr( sval, L"Style" ))
                    {
                        void *st = NULL;
                        if (SUCCEEDED(uwp_element_create( L"Windows.UI.Xaml.Style", NULL, NULL, &st )) && st)
                        {
                            struct uwp_element *st_el = uwp_elem_from_any( st );
                            if (st_el) lstrcpynW( st_el->name, sval, ARRAY_SIZE(st_el->name) );
                            res_val = (IInspectable *)st;
                            release_res = TRUE;
                        }
                    }
                    if (res_val)
                    {
                        uwp_set_custom_xaml_member_obj( ao, owner_cls, pi->name, sv->raw, res_val );
                        if (release_res) IInspectable_Release( res_val );
                    }
                }
                else if (wcsstr( owner_cls, L"Breakpoint" ) || wcsstr( owner_cls, L"Converter" ) || wcsstr( owner_cls, L"Layout" ))
                {
                    if (vt == 0x01 || vt == 0x02)
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
        }
        return;
    }

    if (el->kind == UWP_KIND_BRUSH)
    {
        if (pid == XBF_PROP_SOLIDCOLORBRUSH_COLOR || pid == XBF_PROP_FALLBACK_COLOR)
        {
            if (vt == 0x08)
            {
                el->brush_argb = (UINT32)num;
                el->has_brush_argb = TRUE;
            }
            else if (sval && sval[0])
            {
                if (uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                {
                    el->brush_argb = argb;
                    el->has_brush_argb = TRUE;
                }
                else
                {
                    lstrcpynW( el->brush_color_ref, sval, ARRAY_SIZE(el->brush_color_ref) );
                }
            }
            return;
        }
        if (pid == XBF_PROP_BRUSH_OPACITY)
        {
            if (vt == 0x03 || vt == 0x04) el->brush_opacity = num;
            return;
        }
        if ((pid == XBF_PROP_LGB_START_POINT || pid == XBF_PROP_LGB_END_POINT) && sval && sval[0])
        {
            WCHAR *endp = NULL;
            float px = (float)wcstod( sval, &endp );
            float py = 0.0f;
            if (endp)
            {
                while (*endp == L' ' || *endp == L',' || *endp == L';') endp++;
                py = (float)wcstod( endp, NULL );
            }
            if (pid == XBF_PROP_LGB_START_POINT) { el->grad_start_x = px; el->grad_start_y = py; }
            else { el->grad_end_x = px; el->grad_end_y = py; }
            return;
        }
        if (pid == XBF_PROP_TILEBRUSH_ALIGNMENT_X) { el->tile_align_x = (int)num; return; }
        if (pid == XBF_PROP_TILEBRUSH_ALIGNMENT_Y) { el->tile_align_y = (int)num; return; }
        if (pid == XBF_PROP_TILEBRUSH_STRETCH) { el->media_stretch = (int)num; return; }
    }

    if (pid == XBF_PROP_GRADIENTSTOP_COLOR)
    {
        if (vt == 0x08) { el->brush_argb = (UINT32)num; el->has_brush_argb = TRUE; }
        else if (sval && sval[0] && uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
        {
            el->brush_argb = argb;
            el->has_brush_argb = TRUE;
        }
        return;
    }
    if (pid == XBF_PROP_GRADIENTSTOP_OFFSET)
    {
        if (vt == 0x03 || vt == 0x04) el->grad_stop_offset = num;
        return;
    }

    if (el->kind == UWP_KIND_RESOURCE_DICT && pid == XBF_PROP_RESOURCE_DICTIONARY_SOURCE && sval && sval[0]) /* Source */
    {
        uwp_load_res_xbf_into_bucket( ctx ? ctx->base_xbf_dir : NULL, sval, uwp_rd_bucket_for_el( el, TRUE ) );
        return;
    }

    if (pid & 0x8000)
    {
        if (uwp_is_bg_pid( pid ))
        {
            if (vt == 0x08)
                uwp_el_set_bg_argb( el, (UINT32)num );
            else if (sval && sval[0] &&
                     uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                uwp_el_set_bg_argb( el, argb );
            return;
        }
        if (uwp_is_fg_pid( pid ))
        {
            if (vt == 0x08)
                uwp_el_set_fg_argb( el, (UINT32)num );
            else if (sval && sval[0] &&
                     uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                uwp_el_set_fg_argb( el, argb );
            return;
        }
        if (uwp_is_border_pid( pid ))
        {
            if (vt == 0x08)
                uwp_el_set_border_argb( el, (UINT32)num );
            else if (sval && sval[0] &&
                     uwp_resolve_brush_or_color( ctx ? ctx->target_rd : NULL, el, sval, 0, &argb ))
                uwp_el_set_border_argb( el, argb );
            return;
        }

        switch (pid)
        {
        case XBF_PROP_STYLE: /* FrameworkElement.Style */
            if (sval && sval[0])
                uwp_apply_style_by_name( ctx, el, sval );
            break;
        case XBF_PROP_GRID_ROW: el->grid_row = (int)num; break;
        case XBF_PROP_GRID_COLUMN: el->grid_col = (int)num; break;
        case XBF_PROP_GRID_ROW_SPAN: el->grid_row_span = max( 1, (int)num ); break;
        case XBF_PROP_GRID_COLUMN_SPAN: el->grid_col_span = max( 1, (int)num ); break;
        case XBF_PROP_ROWDEF_HEIGHT: /* RowDefinition.Height */
        case XBF_PROP_COLUMNDEF_WIDTH: /* ColumnDefinition.Width */
            if (vt == 0x07) { el->grid_len_type = (int)vec4[0]; el->grid_len_val = vec4[1]; }
            else if (vt == 0x03 || vt == 0x04) { el->grid_len_type = 1; el->grid_len_val = num; }
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                {
                    el->grid_len_type = 1;
                    el->grid_len_val = de->dval;
                }
            }
            break;
        case XBF_PROP_COLUMNDEF_MAXWIDTH: /* ColumnDefinition.MaxWidth */
        case XBF_PROP_FRAMEWORKELEMENT_MAXWIDTH: /* FrameworkElement.MaxWidth */
            if (vt == 0x03 || vt == 0x04) el->max_width = num;
            break;
        case XBF_PROP_COLUMNDEF_MINWIDTH: /* ColumnDefinition.MinWidth */
            if (vt == 0x03 || vt == 0x04) el->min_width = num;
            break;
        case XBF_PROP_ROWDEF_MAXHEIGHT: /* RowDefinition.MaxHeight */
        case XBF_PROP_FRAMEWORKELEMENT_MAXHEIGHT: /* FrameworkElement.MaxHeight */
            if (vt == 0x03 || vt == 0x04) el->max_height = num;
            break;
        case XBF_PROP_ROWDEF_MINHEIGHT: /* RowDefinition.MinHeight */
            if (vt == 0x03 || vt == 0x04) el->min_height = num;
            break;
        case XBF_PROP_MARGIN: /* Margin */
            if (vt == 0x06)
            {
                el->margin_l = vec4[0]; el->margin_t = vec4[1];
                el->margin_r = vec4[2]; el->margin_b = vec4[3];
            }
            break;
        case XBF_PROP_CONTROL_PADDING: /* Control.Padding */
        case XBF_PROP_BORDER_PADDING: /* Border.Padding */
        case XBF_PROP_PANEL_IS_IGNORING_TRANSITIONS: /* Border.Padding */
            if (vt == 0x06)
            {
                el->padding_l = vec4[0]; el->padding_t = vec4[1];
                el->padding_r = vec4[2]; el->padding_b = vec4[3];
            }
            break;
        case XBF_PROP_CONTROL_BORDER_THICKNESS: /* Control.BorderThickness */
        case XBF_PROP_BORDER_THICKNESS: /* Border.BorderThickness */
        case XBF_PROP_PANEL_CHILDREN_TRANSITIONS: /* Border.BorderThickness */
        case XBF_PROP_GRID_BORDER_THICKNESS:
        case XBF_PROP_STACKPANEL_BORDER_THICKNESS:
            if (vt == 0x06)
            {
                el->border_thick_l = vec4[0]; el->border_thick_t = vec4[1];
                el->border_thick_r = vec4[2]; el->border_thick_b = vec4[3];
            }
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *te = NULL;
                struct uwp_rd_bucket *hint_rd = ctx ? (ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd) : NULL;
                if (uwp_rd_find_entry( hint_rd, el, sval, 0, &te, NULL ) && te && te->kind == UWP_RD_THICKNESS)
                {
                    el->border_thick_l = te->vec4[0]; el->border_thick_t = te->vec4[1];
                    el->border_thick_r = te->vec4[2]; el->border_thick_b = te->vec4[3];
                }
            }
            break;
        case XBF_PROP_BORDER_CORNER_RADIUS: /* Border.CornerRadius */
        case XBF_PROP_GRID_CORNER_RADIUS:
        case XBF_PROP_STACKPANEL_CORNER_RADIUS:
        case XBF_PROP_CONTROL_CORNER_RADIUS:
            if (vt == 0x06)
                el->corner_radius = max( max( vec4[0], vec4[1] ), max( vec4[2], vec4[3] ) );
            else if (vt == 0x03 || vt == 0x04)
                el->corner_radius = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *ce = NULL;
                struct uwp_rd_bucket *hint_rd = ctx ? (ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd) : NULL;
                if (uwp_rd_find_entry( hint_rd, el, sval, 0, &ce, NULL ) && ce)
                {
                    if (ce->kind == UWP_RD_THICKNESS)
                        el->corner_radius = max( max( ce->vec4[0], ce->vec4[1] ), max( ce->vec4[2], ce->vec4[3] ) );
                    else if (ce->kind == UWP_RD_DOUBLE && ce->dval > 0.0)
                        el->corner_radius = ce->dval;
                }
            }
            else if (sval && sval[0])
            {
                double cr = wcstod( sval, NULL );
                if (cr > 0.0) el->corner_radius = cr;
            }
            break;
        case XBF_PROP_CONTENT: /* ContentControl.Content */
        case XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV: /* TextBlock.Text / MenuFlyoutItem.Text */
        case XBF_PROP_UIELEMENT_EXIT_DISPLAY_MODE: /* FontIcon.Glyph */
        case XBF_PROP_FONTICON_GLYPH: /* FontIcon.Glyph */
        case XBF_PROP_TEXTBLOCK_TEXT: /* TextBlock.Text */
        case XBF_PROP_TEMPLATE_BINDING_PROPERTY:
        case XBF_PROP_RUN_TEXT:
            if (sval && sval[0] && !(pid == XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV && el->uid[0] && uwp_has_text( el )))
                uwp_elem_set_text( el, sval );
            break;
        case XBF_PROP_RECTANGLE_RADIUS_X:
        case XBF_PROP_RECTANGLE_RADIUS_Y:
            if (vt == 0x03 || vt == 0x04)
            {
                if (num > el->corner_radius) el->corner_radius = num;
            }
            break;
        case XBF_PROP_CONTROL_HORIZONTAL_CONTENT_ALIGNMENT:
            el->h_content_align = (int)num;
            el->has_h_content_align = TRUE;
            break;
        case XBF_PROP_CONTROL_VERTICAL_CONTENT_ALIGNMENT:
            el->v_content_align = (int)num;
            el->has_v_content_align = TRUE;
            break;
        case XBF_PROP_IMAGE_SOURCE: /* Image.Source */
            if (sval && sval[0])
                lstrcpynW( el->image_src, sval, ARRAY_SIZE(el->image_src) );
            break;
        case XBF_PROP_IMAGE_STRETCH:
        case XBF_PROP_TILEBRUSH_STRETCH:
            if (vt == 0x04 || vt == 0x0b)
                el->media_stretch = (int)num;
            break;
        case XBF_PROP_AUTOMATION_NAME: /* AutomationProperties.Name - never leak into visible text */
            if (sval && sval[0])
                lstrcpynW( el->automation_name, sval, ARRAY_SIZE(el->automation_name) );
            break;
        case XBF_PROP_AUTOMATION_ID: /* x:Uid or AutomationId */
            if (sval && sval[0])
            {
                lstrcpynW( el->uid, sval, ARRAY_SIZE(el->uid) );
                if (!uwp_has_text( el ) || el->kind == UWP_KIND_USER_CONTROL || el->outer)
                {
                    WCHAR res_key[256], res_val[128];
                    static const WCHAR *std_props[] =
                        { L"Content", L"Text", L"Title", L"Header", L"Label", L"PlaceholderText", L"ButtonText" };
                    unsigned int sp;
                    DWORD k;
                    void *ao = (ctx && ctx->app_outer) ? ctx->app_outer : uwp_get_app_outer();
                    IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;

                    for (sp = 0; sp < ARRAY_SIZE(std_props); sp++)
                    {
                        swprintf( res_key, ARRAY_SIZE(res_key), L"%s/%s", sval, std_props[sp] );
                        if (uwp_pri_lookup_string( res_key, res_val, ARRAY_SIZE(res_val) ) && res_val[0])
                        {
                            if (!uwp_has_text( el ) && (!el->content || el->kind == UWP_KIND_BUTTON || el->kind == UWP_KIND_TEXT_BLOCK))
                                uwp_elem_set_text( el, res_val );
                            if (el->outer && ao && !(el->class_flags & UWP_CF_UWP_PREFIX))
                                uwp_set_custom_xaml_member( ao, el->class_name, std_props[sp], target, res_val );
                        }
                    }
                    for (k = 0; ctx && k < ctx->prop_cnt && !uwp_has_text( el ); k++)
                    {
                        const struct xbf_prop_info *kpi = &ctx->props[k];
                        if (kpi->owner_tid < ctx->type_cnt &&
                            !wcsncmp( ctx->types[kpi->owner_tid].xmlns, L"using:", 6 ) &&
                            (!wcscmp( kpi->name, L"Character" ) ||
                             !wcscmp( kpi->name, L"Content" ) ||
                             !wcscmp( kpi->name, L"Text" ) ||
                             !wcscmp( kpi->name, L"Title" ) ||
                             !wcscmp( kpi->name, L"Header" )))
                        {
                            swprintf( res_key, ARRAY_SIZE(res_key), L"%s/[%s]%s/%s",
                                      sval, ctx->types[kpi->owner_tid].xmlns,
                                      ctx->types[kpi->owner_tid].short_name, kpi->name );
                            if (uwp_pri_lookup_string( res_key, res_val, ARRAY_SIZE(res_val) ) && res_val[0])
                                uwp_elem_set_text( el, res_val );
                        }
                    }
                }
            }
            break;
        case XBF_PROP_TEXTELEMENT_FONT_FAMILY:
            if (vt == 0x03 || vt == 0x04)
            {
                if (num > 0) { el->font_size = num; el->has_font_size = TRUE; }
                break;
            }
            if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE && de->dval > 0)
                {
                    el->font_size = de->dval;
                    el->has_font_size = TRUE;
                    break;
                }
            }
            /* fall through */
        case XBF_PROP_CONTENTPRESENTER_FONT_FAMILY:
        case XBF_PROP_CONTROL_FONT_FAMILY:
        case XBF_PROP_TEXTBLOCK_FONT_FAMILY:
        case XBF_PROP_FONTICON_FONT_FAMILY:
            if (sval && sval[0])
            {
                WCHAR face[64] = {0};
                const WCHAR *ff_spec = sval;
                if (is_res_ref || !wcschr( sval, L'#' ))
                {
                    struct uwp_rd_entry *fe = NULL;
                    if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &fe, NULL ) &&
                        fe && (fe->kind == UWP_RD_FONT_FAMILY || fe->kind == UWP_RD_STRING) && fe->str_val[0])
                        ff_spec = fe->str_val;
                }
                lstrcpynW( el->font_family, ff_spec, ARRAY_SIZE(el->font_family) );
                uwp_resolve_font_family( ff_spec, face, ARRAY_SIZE(face) );
            }
            break;
        case XBF_PROP_POPUP_HORIZONTAL_OFFSET: /* FontSize */
        case XBF_PROP_CONTROL_FONTSIZE: /* Control.FontSize */
        case XBF_PROP_TEXTBLOCK_FONTSIZE: /* TextBlock.FontSize */
        case XBF_PROP_TEXTELEMENT_FONTSIZE: /* TextElement.FontSize */
        case XBF_PROP_FONTICON_FONTSIZE: /* FontIcon.FontSize */
            if (vt == 0x03 || vt == 0x04)
            {
                if (num > 0) { el->font_size = num; el->has_font_size = TRUE; }
            }
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE && de->dval > 0)
                {
                    el->font_size = de->dval;
                    el->has_font_size = TRUE;
                }
            }
            break;
        case XBF_PROP_TEXTBLOCK_FONT_WEIGHT: /* TextBlock.FontWeight */
        case XBF_PROP_CONTROL_FONT_WEIGHT: /* Control.FontWeight */
        case XBF_PROP_TEXTELEMENT_FONT_WEIGHT: /* TextElement.FontWeight */
        {
            int fw = uwp_parse_font_weight( sval, num, vt );
            if (fw > 0) el->font_weight = fw;
            break;
        }
        case XBF_PROP_TEXTBLOCK_TEXT_ALIGNMENT: /* TextBlock.TextAlignment */
            el->text_align = (int)num;
            el->has_text_align = TRUE;
            break;
        case XBF_PROP_TEXTBLOCK_TEXT_WRAPPING: /* TextBlock.TextWrapping */
        case XBF_PROP_RICHTEXTBLOCK_TEXT_WRAPPING:
            if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
            {
                if (!wcsicmp( sval, L"NoWrap" )) el->text_wrapping = 1;
                else if (!wcsicmp( sval, L"WrapWholeWords" )) el->text_wrapping = 3;
                else if (!wcsicmp( sval, L"Wrap" )) el->text_wrapping = 2;
            }
            else
                el->text_wrapping = (int)num;
            break;
        case XBF_PROP_TEXTBLOCK_MAX_LINES:
        case XBF_PROP_RICHTEXTBLOCK_MAX_LINES:
            if (vt == 0x03 || vt == 0x04)
                el->max_lines = max( 0, (int)num );
            else if (sval && sval[0])
                el->max_lines = max( 0, (int)wcstol( sval, NULL, 10 ) );
            break;
        case XBF_PROP_WIDTH: /* Width */
            if (vt == 0x03 || vt == 0x04) el->width = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->width = de->dval;
            }
            break;
        case XBF_PROP_FLOW_DIRECTION: /* Height */
        case XBF_PROP_FRAMEWORKELEMENT_HEIGHT: /* FrameworkElement.Height */
            if (vt == 0x03 || vt == 0x04) el->height = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->height = de->dval;
            }
            break;
        case XBF_PROP_MIN_WIDTH: /* MinWidth */
            if (vt == 0x03 || vt == 0x04) el->min_width = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->min_width = de->dval;
            }
            break;
        case XBF_PROP_MIN_HEIGHT: /* MinHeight */
            if (vt == 0x03 || vt == 0x04) el->min_height = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->min_height = de->dval;
            }
            break;
        case XBF_PROP_HORIZONTAL_ALIGNMENT: /* HorizontalAlignment */
            el->h_align = (int)num;
            break;
        case XBF_PROP_VERTICAL_ALIGNMENT: /* VerticalAlignment */
            el->v_align = (int)num;
            break;
        case XBF_PROP_ORIENTATION: /* StackPanel.Orientation */
        case XBF_PROP_STACKPANEL_ORIENTATION: /* StackPanel.Orientation (XBF v2) */
            el->orientation = (int)num;
            break;
        case XBF_PROP_STACKPANEL_SPACING: /* StackPanel.Spacing */
            if (vt == 0x03 || vt == 0x04)
                el->spacing = num;
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE)
                    el->spacing = de->dval;
            }
            break;
        case XBF_PROP_UIELEMENT_VISIBILITY_V2: /* UIElement.Visibility (XBF v2) */
        case XBF_PROP_UIELEMENT_VISIBILITY: /* UIElement.Visibility */
            if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
                el->visibility = !wcsicmp( sval, L"Collapsed" ) ? 1 : 0;
            else
                el->visibility = (int)num;
            break;
        case XBF_PROP_UIELEMENT_OPACITY:
            if (vt == 0x03 || vt == 0x04 || vt == 0x01 || vt == 0x02)
                el->opacity = num;
            else if ((vt == 0x05 || vt == 0x09) && sval && sval[0])
                el->opacity = wcstod( sval, NULL );
            break;
        case XBF_PROP_STORYBOARD_TARGET_NAME: /* Storyboard.TargetName */
            if (sval && sval[0])
                lstrcpynW( el->anim_target_name, sval, ARRAY_SIZE(el->anim_target_name) );
            break;
        case XBF_PROP_STORYBOARD_TARGET_PROPERTY: /* Storyboard.TargetProperty */
            if (sval && sval[0])
                vsm_clean_prop_name( sval, el->anim_target_prop, ARRAY_SIZE(el->anim_target_prop) );
            break;
        case XBF_PROP_DOUBLE_ANIMATION_TO:
        {
            double to_val = 0.0;
            if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &to_val ))
            {
                el->anim_to = to_val;
                el->anim_has_to = TRUE;
            }
            break;
        }
        case XBF_PROP_ACCELERATOR_KEY: /* KeyboardAccelerator.Key */
            el->accel_key = (UINT)num;
            uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
            break;
        case XBF_PROP_ACCELERATOR_MODIFIERS: /* KeyboardAccelerator.Modifiers */
            el->accel_mods = (UINT)num;
            uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
            break;
        case XBF_PROP_ACCELERATOR_SCOPE_OWNER: /* KeyboardAccelerator.IsEnabled */
            el->accel_enabled = (num != 0.0);
            uwp_accel_register( el, el->accel_target, el->accel_key, el->accel_mods, el->accel_enabled );
            break;
        case XBF_PROP_ITEMS_CONTROL_DISPLAY_MEMBER_PATH:
            if (sval && sval[0])
                lstrcpynW( el->display_member_path, sval, ARRAY_SIZE(el->display_member_path) );
            break;
        case XBF_PROP_SYMBOL_ICON_SYMBOL:
            if ((vt == 0x04 || vt == 0x0b) && (int)num >= 0xE000 && (int)num <= 0xFFFF)
            {
                WCHAR sym = (WCHAR)(int)num;
                uwp_elem_set_text_n( el, &sym, 1 );
                if (!el->font_family[0])
                    lstrcpynW( el->font_family, L"Segoe MDL2 Assets", ARRAY_SIZE(el->font_family) );
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
    else if (ctx && pid < ctx->prop_cnt)
    {
        const struct xbf_prop_info *pi = &ctx->props[pid];
        if (!wcscmp( pi->name, L"IsChecked" ) || !wcscmp( pi->name, L"IsSelected" ))
        {
            if (vt == 0x01 || vt == 0x02) el->is_checked = (vt == 0x02) ? 1 : 0;
            else if (sval && sval[0]) el->is_checked = !wcsicmp( sval, L"True" ) ? 1 : 0;
            else el->is_checked = (num != 0.0) ? 1 : 0;
        }
        if (!wcscmp( pi->name, L"DisplayMemberPath" ) && sval && sval[0])
        {
            lstrcpynW( el->display_member_path, sval, ARRAY_SIZE(el->display_member_path) );
        }
        else if (!wcscmp( pi->name, L"PaneDisplayMode" ) && uwp_is_nav_view( el ))
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
                el->nav_pane_display_mode = mode;
                el->nav_pane_display_mode_set = TRUE;
                TRACE( "xbf_apply_property: NavView(%s) PaneDisplayMode=%d (%s)\n",
                       debugstr_w(el->name), mode, sval ? debugstr_w(sval) : "" );
            }
        }
        else if (!wcscmp( pi->name, L"IsPaneOpen" ) && uwp_is_nav_view( el ))
        {
            if (vt == 0x01 || vt == 0x02) el->nav_is_pane_open = (vt == 0x02);
            else if (sval && sval[0]) el->nav_is_pane_open = !wcsicmp( sval, L"True" );
        }
        else if (!wcscmp( pi->name, L"OpenPaneLength" ) && uwp_is_nav_view( el ))
        {
            double d = 0.0;
            if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &d ) && d > 0.0)
                el->nav_open_pane_length = d;
        }
        else if (!wcscmp( pi->name, L"CompactPaneLength" ) && uwp_is_nav_view( el ))
        {
            double d = 0.0;
            if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &d ) && d > 0.0)
                el->nav_compact_pane_length = d;
        }
        else if (!wcscmp( pi->name, L"CompactModeThresholdWidth" ) && uwp_is_nav_view( el ))
        {
            double d = 0.0;
            if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &d ) && d > 0.0)
                el->nav_compact_threshold = d;
        }
        else if (!wcscmp( pi->name, L"ExpandedModeThresholdWidth" ) && uwp_is_nav_view( el ))
        {
            double d = 0.0;
            if (xbf_resolve_double_prop( ctx, el, vt, num, sval, is_res_ref, &d ) && d > 0.0)
                el->nav_expanded_threshold = d;
        }
        else if (!wcscmp( pi->name, L"IsSettingsVisible" ) && uwp_is_nav_view( el ))
        {
            if (vt == 0x01 || vt == 0x02) el->nav_is_settings_visible = (vt == 0x02);
            else if (sval && sval[0]) el->nav_is_settings_visible = !wcsicmp( sval, L"True" );
        }
        else if (!wcscmp( pi->name, L"IsPaneToggleButtonVisible" ) && uwp_is_nav_view( el ))
        {
            if (vt == 0x01 || vt == 0x02) el->nav_is_pane_toggle_visible = (vt == 0x02);
            else if (sval && sval[0]) el->nav_is_pane_toggle_visible = !wcsicmp( sval, L"True" );
        }
        if (!wcscmp( pi->name, L"Text" ) && sval && sval[0] &&
                 (el->kind == UWP_KIND_TEXT_BLOCK || (el->class_flags & (UWP_CF_RUN | UWP_CF_INLINE))))
        {
            uwp_elem_set_text( el, sval );
        }
        else if ((!wcscmp( pi->name, L"Source" ) || !wcscmp( pi->name, L"UriSource" ) || !wcscmp( pi->name, L"ImageSource" )) &&
                 sval && sval[0] && !is_res_ref)
        {
            lstrcpynW( el->image_src, sval, ARRAY_SIZE(el->image_src) );
        }
        else if (!wcscmp( pi->name, L"Stretch" ))
        {
            if (vt == 0x04 || vt == 0x0b) el->media_stretch = (int)num;
            else if (sval && sval[0])
            {
                if (!wcsicmp( sval, L"None" )) el->media_stretch = 0;
                else if (!wcsicmp( sval, L"Fill" )) el->media_stretch = 1;
                else if (!wcsicmp( sval, L"Uniform" )) el->media_stretch = 2;
                else if (!wcsicmp( sval, L"UniformToFill" )) el->media_stretch = 3;
            }
        }
        else if (!wcscmp( pi->name, L"FontFamily" ) && sval && sval[0])
        {
            WCHAR face[64] = {0};
            const WCHAR *ff_spec = sval;
            struct uwp_rd_entry *fe = NULL;
            if ((is_res_ref || !wcschr( sval, L'#' )) &&
                uwp_rd_find_entry( ctx->target_rd, el, sval, 0, &fe, NULL ) &&
                fe && (fe->kind == UWP_RD_FONT_FAMILY || fe->kind == UWP_RD_STRING) && fe->str_val[0])
                ff_spec = fe->str_val;
            lstrcpynW( el->font_family, ff_spec, ARRAY_SIZE(el->font_family) );
            uwp_resolve_font_family( ff_spec, face, ARRAY_SIZE(face) );
        }
        else if ((!wcscmp( pi->name, L"VirtualKeyControlChord" ) ||
                  !wcscmp( pi->name, L"VirtualKeyAltChord" ) ||
                  !wcscmp( pi->name, L"VirtualKeyShiftChord" ) ||
                  !wcscmp( pi->name, L"VirtualKeyControlShiftChord" )) && sval && sval[0])
        {
            UINT vk = uwp_parse_vk_string( sval );
            UINT mods = !wcscmp( pi->name, L"VirtualKeyControlChord" ) ? 1 :
                        (!wcscmp( pi->name, L"VirtualKeyAltChord" ) ? 2 :
                         (!wcscmp( pi->name, L"VirtualKeyShiftChord" ) ? 4 : 5));
            if (vk)
                uwp_accel_register( NULL, el, vk, mods, TRUE );
        }
        else if (!wcscmp( pi->name, L"ButtonId" ) && sval && sval[0])
        {
            IInspectable *target = el->outer ? el->outer : &el->IInspectable_iface;
            const WCHAR *owner_cls = (pi->owner_tid < ctx->type_cnt && ctx->types[pi->owner_tid].full_name[0])
                ? ctx->types[pi->owner_tid].full_name
                : (el->class_name[0] ? el->class_name : NULL);
            if (owner_cls)
                uwp_set_custom_xaml_member( ctx->app_outer, owner_cls, L"ButtonId", target, sval );
            /* Fallback label from ButtonId if button still has no text */
            if (!uwp_has_text( el ))
            {
                static const struct { const WCHAR *id; const WCHAR *label; } btn_id_map[] = {
                    { L"Zero",       L"0" },
                    { L"One",        L"1" },
                    { L"Two",        L"2" },
                    { L"Three",      L"3" },
                    { L"Four",       L"4" },
                    { L"Five",       L"5" },
                    { L"Six",        L"6" },
                    { L"Seven",      L"7" },
                    { L"Eight",      L"8" },
                    { L"Nine",       L"9" },
                    { L"Decimal",    L"." },
                    { L"Add",        L"+" },
                    { L"Subtract",   L"-" },
                    { L"Multiply",   L"\x00d7" },
                    { L"Divide",     L"\x00f7" },
                    { L"Equals",     L"=" },
                    { L"Clear",      L"C" },
                    { L"ClearEntry", L"CE" },
                    { L"Backspace",  L"\x232b" },
                    { L"Negate",     L"+/-" },
                    { L"Percent",    L"%" },
                    { L"Invert",     L"1/x" },
                    { L"Power",      L"x\x00b2" },
                    { L"Sqrt",       L"\x221a" },
                };
                unsigned int bi;
                for (bi = 0; bi < ARRAY_SIZE(btn_id_map); bi++)
                {
                    if (!wcscmp( sval, btn_id_map[bi].id ))
                    {
                        uwp_elem_set_text( el, btn_id_map[bi].label );
                        break;
                    }
                }
            }
        }
        else if (is_res_ref && sval && sval[0] && pi->name[0] &&
                 pi->owner_tid < ctx->type_cnt && ctx->types[pi->owner_tid].full_name[0] &&
                 wcsncmp( ctx->types[pi->owner_tid].full_name, L"Windows.UI.Xaml.", 16 ) != 0)
        {
            void *ao = ctx->app_outer ? ctx->app_outer : uwp_get_app_outer();
            struct uwp_rd_entry *re = NULL;
            struct uwp_rd_bucket *found_b = NULL;
            if (ao && uwp_rd_find_entry( ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd, el, sval, 0, &re, &found_b ) && re)
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
                    if (res_val)
                        uwp_set_custom_xaml_member_obj( ao, owner_cls, pi->name, target, res_val );
                }
            }
        }
        else if (pi->name[0] && pi->owner_tid < ctx->type_cnt &&
                 ctx->types[pi->owner_tid].full_name[0] &&
                 wcsncmp( ctx->types[pi->owner_tid].full_name, L"Windows.UI.Xaml.", 16 ) != 0 &&
                 (vt == 0x01 || vt == 0x02 || vt == 0x03 || vt == 0x04 || vt == 0x09 || vt == 0x0b || vt == 0x0c || (vt == 0x05 && sval && sval[0])))
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
                else if (vt == 0x05 && sval && sval[0])
                    uwp_set_custom_xaml_member( ao, owner_cls, pi->name, target, sval );
                else if (is_nav_pane_mode)
                {
                    static const WCHAR *const modes[] = { L"Auto", L"Left", L"Top", L"LeftCompact", L"LeftMinimal" };
                    int m = (int)num;
                    uwp_set_custom_xaml_member( ao, owner_cls, pi->name, target,
                                                (m >= 0 && m < 5) ? modes[m] : L"Auto" );
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
    {
        BOOL is_sel = FALSE;
        if (el && is_res_ref && sval && sval[0] && xbf_prop_is_item_template( pid, ctx, &is_sel ))
        {
            struct uwp_rd_entry *re = NULL;
            struct uwp_rd_bucket *found_b = NULL;
            if (uwp_rd_find_entry( ctx ? (ctx->root_el ? ctx->root_el->rd_bucket : ctx->target_rd) : NULL,
                                   el, sval, 0, &re, &found_b ) && re)
            {
                IInspectable *res_val = uwp_rd_materialize_entry( found_b, re );
                if (res_val)
                {
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
            }
        }
    }
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
        if (!xaml_vec_grow( (void **)&tbl->items, &tbl->cap, tbl->count + 1, sizeof(*tbl->items), 32 ))
            return FALSE;
        tbl->items[tbl->count].key_sid = sid;
        tbl->items[tbl->count].sub_off = off;
        tbl->count++;
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
        if (!xaml_vec_grow( (void **)&tbl->items, &tbl->cap, tbl->count + 1, sizeof(*tbl->items), 32 ))
            return FALSE;
        tbl->items[tbl->count].key_sid = sid;
        tbl->items[tbl->count].sub_off = off;
        tbl->count++;
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

