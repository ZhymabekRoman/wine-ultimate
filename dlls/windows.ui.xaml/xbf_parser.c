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
        WORD name_off = *(const WORD *)(hs + node_off + (DWORD)n * 12 + 8);
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

static BOOL pri_resolve_item_data( const BYTE *pri, DWORD pri_sz, const BYTE *rm, DWORD rm_sz,
                                   DWORD count1, DWORD extra_sz, DWORD map_off, DWORD cand_off,
                                   DWORD item_id, const BYTE **out_ptr, DWORD *out_sz )
{
    DWORD cand_base = 0;
    WORD data_idx, sec_idx;
    if (item_id < count1)
        cand_base = *(const WORD *)(rm + map_off + item_id * 4 + 2);
    else if (extra_sz >= 28)
    {
        DWORD r_off = map_off + count1 * 4 + 28 + (item_id - count1) * 8;
        if (r_off + 8 > cand_off) return FALSE;
        cand_base = *(const DWORD *)(rm + r_off + 4);
    }
    else return FALSE;
    if (cand_off + (cand_base + 1) * 8 > rm_sz) return FALSE;
    data_idx = *(const WORD *)(rm + cand_off + cand_base * 8 + 4);
    sec_idx = *(const WORD *)(rm + cand_off + cand_base * 8 + 6);
    return pri_get_data_item( pri, pri_sz, sec_idx, data_idx, out_ptr, out_sz );
}

static BOOL pri_find_item_by_path( const char *want_path, const char *want_suffix,
                                   const BYTE **out_ptr, DWORD *out_sz )
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
                                  (DWORD)matched_item, out_ptr, out_sz );
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
                                   iid, &ptr, &sz ) && ptr && sz > 0x50 && !memcmp( ptr, "XBF", 4 ))
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
        if (fallback < 0 && !strcasecmp( cat->entries[k].base, suffix ))
            fallback = (int)k;
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

BOOL uwp_pri_find_xbf_rel( const char *base_xbf_dir, const WCHAR *uri_or_name,
                                  const BYTE **out_ptr, DWORD *out_sz,
                                  char *out_resolved, size_t out_resolved_sz )
{
    InitOnceExecuteOnce( &g_pri_xbf_cat_once, init_pri_xbf_cat_once, NULL, NULL );
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
    size_t i, len;

    if (!res_name || !out_buf || out_chars <= 0) return FALSE;
    out_buf[0] = 0;

    len = wcslen( res_name );
    for (i = 0; i < len && i + 11 < sizeof(full_path) - 1; i++)
        full_path[10 + i] = (res_name[i] == '\\') ? '/' : (char)res_name[i];
    full_path[10 + i] = 0;
    snprintf( suffix, sizeof(suffix), "%s", full_path + 10 );

    if (!pri_find_item_by_path( full_path, suffix, &ptr, &sz ) || !ptr || !sz)
        return FALSE;

    if (sz >= 2 && (sz % 2) == 0 && ptr[1] == 0 && ptr[sz - 1] == 0)
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


struct uwp_style_setter
{
    WORD pid;
    int vt;
    BOOL is_res_ref;
    double num;
    double vec4[4];
    WCHAR str[128];
};



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

static HRESULT WINAPI xaml_ct_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IXamlType_UWP_g ) ||
        IsEqualGUID( iid, &IID_IXamlType_WinUI3_g ))
    {
        *out = iface;
        InterlockedIncrement( &((struct xaml_cached_type *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xaml_ct_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct xaml_cached_type *)iface)->ref );
}

static ULONG WINAPI xaml_ct_Release( void *iface )
{
    /* Singleton-cached in xaml_meta_cache for process lifetime; ref stays >= 1 */
    struct xaml_cached_type *t = (struct xaml_cached_type *)iface;
    LONG r = InterlockedDecrement( &t->ref );
    if (r == 0)
    {
        free( t->full_name );
        free( t );
    }
    return r;
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

static const void *xaml_cached_type_vtbl[] =
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
    xaml_ct_get_Name,            /* 17: get_Name */
    xaml_ct_ActivateInstance,    /* 18: ActivateInstance (UWP) */
    xaml_ct_CreateFromString,    /* 19: CreateFromString */
    xaml_ct_GetMember,           /* 20: GetMember */
    xaml_ct_RunInitializer,      /* 21: RunInitializer */
    xaml_ct_get_UnderlyingType,  /* 22: get_UnderlyingType */
};

static struct xaml_cached_type *xaml_cached_type_create( const WCHAR *full_name, WORD xbf_id )
{
    struct xaml_cached_type *t = calloc( 1, sizeof(*t) );
    const WCHAR *dot;
    if (!t) return NULL;
    t->lpVtbl = xaml_cached_type_vtbl;
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

WORD xaml_metadata_cache_find_xbf_id( const WCHAR *full_name )
{
    struct xaml_meta_cache_entry *e;
    WORD id = 0;

    if (!full_name || !full_name[0]) return 0;
    InitOnceExecuteOnce( &xaml_meta_cache_once, init_xaml_meta_cache, NULL, NULL );

    AcquireSRWLockShared( &xaml_meta_cache_lock );
    for (e = xaml_meta_cache[xaml_meta_hash( full_name ) & (XAML_META_CACHE_SIZE - 1)]; e; e = e->next)
    {
        if (!wcscmp( e->full_name, full_name ))
        {
            id = e->xbf_type_id;
            break;
        }
    }
    ReleaseSRWLockShared( &xaml_meta_cache_lock );
    return id;
}

HRESULT xaml_metadata_cache_get_type( const WCHAR *full_name, void **out )
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
        if (ch && ch->kind != UWP_KIND_DEFERRED && ch->visibility == 0) return TRUE;
    }
    if (!panel->children_coll) return FALSE;
    v = CONTAINING_RECORD( panel->children_coll, struct uwp_vec_view, IInspectable_iface );
    for (k = 0; k < v->count; k++)
    {
        struct uwp_element *ch = uwp_elem_from_any( v->items[k] );
        if (ch && ch->kind != UWP_KIND_DEFERRED && ch->visibility == 0) return TRUE;
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

static BOOL xbf_read_uleb( const BYTE *buf, DWORD *io_p, DWORD end_p, DWORD *out_val )
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

static BOOL xbf_get_substream_bounds( const struct xbf_ctx *ctx, DWORD sub_idx,
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

static void uwp_rd_store_stack_val( struct uwp_rd_bucket *b, const WCHAR *key, struct xbf_stack_val *sv )
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
    e->argb = sv->argb;
    e->has_argb = sv->has_argb;
    e->opacity = sv->opacity > 0.0 ? sv->opacity : (sv->kind == UWP_RD_BRUSH ? sv->opacity : 1.0);
    if (sv->kind != UWP_RD_BRUSH && e->opacity == 0.0) e->opacity = 1.0;
    e->dval = sv->dval;
    memcpy( e->vec4, sv->vec4, sizeof(e->vec4) );
    lstrcpynW( e->str_val, (sv->kind == UWP_RD_STYLE) ? sv->based_on : sv->str_val, ARRAY_SIZE(e->str_val) );

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
    /* If it's a brush referencing a color key, try eager resolve if already in scope */
    if (e->kind == UWP_RD_BRUSH && !e->has_argb && e->str_val[0])
    {
        UINT32 resolved = 0;
        if (uwp_resolve_brush_or_color( b, b->owner_el, e->str_val, 0, &resolved ))
        {
            e->argb = resolved;
            e->has_argb = TRUE;
        }
    }
}

static BOOL xbf_read_value( struct xbf_ctx *ctx, const BYTE *nd, DWORD *io_p, DWORD end_p,
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

static void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
                                WORD pid, int vt, double num, const WCHAR *sval,
                                const double vec4[4], BOOL is_res_ref );

void uwp_apply_style_entry( struct xbf_ctx *ctx, struct uwp_element *el,
                                   struct uwp_rd_bucket *b, struct uwp_rd_entry *se, int depth )
{
    int i;
    if (!el || !se || se->kind != UWP_RD_STYLE || depth > 8) return;
    if (se->str_val[0])
    {
        struct uwp_rd_entry *base_se = NULL;
        struct uwp_rd_bucket *base_b = NULL;
        if (uwp_rd_find_entry( b, el, se->str_val, 0, &base_se, &base_b ) && base_se)
            uwp_apply_style_entry( ctx, el, base_b, base_se, depth + 1 );
    }
    for (i = 0; i < se->setter_cnt; i++)
    {
        const struct uwp_style_setter *st = &se->setters[i];
        xbf_apply_property( ctx, el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
    }
}

static void xbf_apply_property( struct xbf_ctx *ctx, struct uwp_element *el, struct xbf_stack_val *sv,
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

    if (!el) return;

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
        case XBF_PROP_FRAMEWORKELEMENT_MAXHEIGHT: /* FrameworkElement.MaxWidth */
            if (vt == 0x03 || vt == 0x04) el->max_width = num;
            break;
        case XBF_PROP_COLUMNDEF_MINWIDTH: /* ColumnDefinition.MinWidth */
            if (vt == 0x03 || vt == 0x04) el->min_width = num;
            break;
        case XBF_PROP_ROWDEF_MAXHEIGHT: /* RowDefinition.MaxHeight */
        case XBF_PROP_FRAMEWORKELEMENT_MAXWIDTH: /* FrameworkElement.MaxHeight */
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
        case XBF_PROP_PANEL_IS_IGNORING_TRANSITIONS: /* Border.Padding */
            if (vt == 0x06)
            {
                el->padding_l = vec4[0]; el->padding_t = vec4[1];
                el->padding_r = vec4[2]; el->padding_b = vec4[3];
            }
            break;
        case XBF_PROP_CONTROL_BORDER_THICKNESS: /* Control.BorderThickness */
        case XBF_PROP_PANEL_CHILDREN_TRANSITIONS: /* Border.BorderThickness */
            if (vt == 0x06)
            {
                el->border_thick_l = vec4[0]; el->border_thick_t = vec4[1];
                el->border_thick_r = vec4[2]; el->border_thick_b = vec4[3];
            }
            break;
        case XBF_PROP_CONTENT: /* ContentControl.Content */
        case XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV: /* TextBlock.Text */
        case XBF_PROP_UIELEMENT_EXIT_DISPLAY_MODE: /* FontIcon.Glyph */
            if (sval && sval[0])
                lstrcpynW( el->text, sval, ARRAY_SIZE(el->text) );
            break;
        case XBF_PROP_IMAGE_SOURCE: /* Image.Source */
            if (sval && sval[0])
                lstrcpynW( el->image_src, sval, ARRAY_SIZE(el->image_src) );
            break;
        case XBF_PROP_AUTOMATION_ID: /* x:Uid or Name */
            if (sval && sval[0])
            {
                lstrcpynW( el->uid, sval, ARRAY_SIZE(el->uid) );
                if (!el->text[0])
                {
                    WCHAR res_key[256], res_val[128];
                    static const WCHAR *std_props[] = { L"Content", L"Text" };
                    unsigned int sp;
                    DWORD k;

                    for (sp = 0; sp < ARRAY_SIZE(std_props) && !el->text[0]; sp++)
                    {
                        swprintf( res_key, ARRAY_SIZE(res_key), L"%s/%s", sval, std_props[sp] );
                        if (uwp_pri_lookup_string( res_key, res_val, ARRAY_SIZE(res_val) ) && res_val[0])
                            lstrcpynW( el->text, res_val, ARRAY_SIZE(el->text) );
                    }
                    for (k = 0; k < ctx->prop_cnt && !el->text[0]; k++)
                    {
                        const struct xbf_prop_info *kpi = &ctx->props[k];
                        if (kpi->owner_tid < ctx->type_cnt &&
                            !wcsncmp( ctx->types[kpi->owner_tid].xmlns, L"using:", 6 ) &&
                            (!wcscmp( kpi->name, L"Character" ) ||
                             !wcscmp( kpi->name, L"Content" ) ||
                             !wcscmp( kpi->name, L"Text" )))
                        {
                            swprintf( res_key, ARRAY_SIZE(res_key), L"%s/[%s]%s/%s",
                                      sval, ctx->types[kpi->owner_tid].xmlns,
                                      ctx->types[kpi->owner_tid].short_name, kpi->name );
                            if (uwp_pri_lookup_string( res_key, res_val, ARRAY_SIZE(res_val) ) && res_val[0])
                                lstrcpynW( el->text, res_val, ARRAY_SIZE(el->text) );
                        }
                    }
                }
            }
            break;
        case XBF_PROP_POPUP_HORIZONTAL_OFFSET: /* FontSize */
        case XBF_PROP_CONTROL_FONTSIZE: /* Control.FontSize */
        case XBF_PROP_TEXTBLOCK_FONTSIZE: /* TextBlock.FontSize */
            if (vt == 0x03 || vt == 0x04)
            {
                if (num > 0) el->font_size = num;
            }
            else if (is_res_ref && sval && sval[0])
            {
                struct uwp_rd_entry *de = NULL;
                if (uwp_rd_find_entry( ctx ? ctx->target_rd : NULL, el, sval, 0, &de, NULL ) &&
                    de && de->kind == UWP_RD_DOUBLE && de->dval > 0)
                    el->font_size = de->dval;
            }
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
            el->orientation = (int)num;
            break;
        case XBF_PROP_UIELEMENT_VISIBILITY_V2: /* UIElement.Visibility (XBF v2) */
        case XBF_PROP_UIELEMENT_VISIBILITY: /* UIElement.Visibility */
            el->visibility = (int)num;
            break;
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
        }
    }
    else if (ctx && pid < ctx->prop_cnt)
    {
        const struct xbf_prop_info *pi = &ctx->props[pid];
        if (!wcscmp( pi->name, L"Name" ) && sval && sval[0])
        {
            lstrcpynW( el->text, sval, ARRAY_SIZE(el->text) );
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
            if (!el->text[0])
            {
                if (!wcscmp( sval, L"Zero" )) lstrcpyW( el->text, L"0" );
                else if (!wcscmp( sval, L"One" )) lstrcpyW( el->text, L"1" );
                else if (!wcscmp( sval, L"Two" )) lstrcpyW( el->text, L"2" );
                else if (!wcscmp( sval, L"Three" )) lstrcpyW( el->text, L"3" );
                else if (!wcscmp( sval, L"Four" )) lstrcpyW( el->text, L"4" );
                else if (!wcscmp( sval, L"Five" )) lstrcpyW( el->text, L"5" );
                else if (!wcscmp( sval, L"Six" )) lstrcpyW( el->text, L"6" );
                else if (!wcscmp( sval, L"Seven" )) lstrcpyW( el->text, L"7" );
                else if (!wcscmp( sval, L"Eight" )) lstrcpyW( el->text, L"8" );
                else if (!wcscmp( sval, L"Nine" )) lstrcpyW( el->text, L"9" );
                else if (!wcscmp( sval, L"Decimal" )) lstrcpyW( el->text, L"." );
                else if (!wcscmp( sval, L"Add" )) lstrcpyW( el->text, L"+" );
                else if (!wcscmp( sval, L"Subtract" )) lstrcpyW( el->text, L"-" );
                else if (!wcscmp( sval, L"Multiply" )) lstrcpyW( el->text, L"\x00d7" );
                else if (!wcscmp( sval, L"Divide" )) lstrcpyW( el->text, L"\x00f7" );
                else if (!wcscmp( sval, L"Equals" )) lstrcpyW( el->text, L"=" );
                else if (!wcscmp( sval, L"Clear" )) lstrcpyW( el->text, L"C" );
                else if (!wcscmp( sval, L"ClearEntry" )) lstrcpyW( el->text, L"CE" );
                else if (!wcscmp( sval, L"Backspace" )) lstrcpyW( el->text, L"\x232b" );
                else if (!wcscmp( sval, L"Negate" )) lstrcpyW( el->text, L"+/-" );
                else if (!wcscmp( sval, L"Percent" )) lstrcpyW( el->text, L"%" );
                else if (!wcscmp( sval, L"Invert" )) lstrcpyW( el->text, L"1/x" );
                else if (!wcscmp( sval, L"Power" )) lstrcpyW( el->text, L"x\x00b2" );
                else if (!wcscmp( sval, L"Sqrt" )) lstrcpyW( el->text, L"\x221a" );
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
        if ((op == 0x24 || op == 0x1e || op == 0x1a) && p + 2 <= end_p)
        {
            WORD vpid = *(const WORD *)(ctx->nodes + p);
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            (void)vpid;
            p += 2;
            if (xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 ))
                xbf_stack_val_add_setter( style_sv, target_pid, vt, (op == 0x24 || op == 0x1e), num, sbuf, vec4 );
        }
        else if (op == 0x14 && p + 2 <= end_p)
        {
            WORD tid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (tid == XBF_TYPE_BRUSH_C) /* inline SolidColorBrush */
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

static BOOL xbf_read_vsm_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
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
    /* Find VisualStateGroups footer: 00 02 00 00 01 <cnt_uleb> <cnt*u16> 02 */
    while (p < end_p)
    {
        if (end_p - p >= 7 &&
            nodes[p] == 0x00 && nodes[p + 1] == 0x02 &&
            nodes[p + 2] == 0x00 && nodes[p + 3] == 0x00 &&
            nodes[p + 4] == 0x01)
        {
            DWORD q = p + 5, cnt = 0;
            if (xbf_read_uleb( nodes, &q, end_p, &cnt ) &&
                cnt >= n_states && cnt <= (end_p - q) / 2 &&
                q + cnt * 2 < end_p && nodes[q + cnt * 2] == 0x02)
            {
                p = q + cnt * 2 + 1;
                if (io_prop_sp && *io_prop_sp > 0) (*io_prop_sp)--;
                *io_p = p;
                return TRUE;
            }
        }
        p++;
    }
    *io_p = p;
    return TRUE;
}

static BOOL xbf_read_style_defer_table( struct xbf_ctx *ctx, DWORD *io_p, DWORD end_p,
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
            if ((sop == 0x11 || sop == 0x12) && soff < def_sub_sz)
                xbf_parse_style_setter_sub( ctx, (int)def_sub, soff, spid, style_sv );
        }
        else if (sop == 0x01 || sop == 0x02 || sop == 0x20)
        {
            DWORD soff = 0;
            if (end_p - p < 5) return FALSE;
            p += 5;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &soff ) || soff > def_sub_sz)
                return FALSE;
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

static BOOL xbf_read_rd_defer_header( const BYTE *nodes, DWORD *io_p, DWORD end_p,
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

/* ============================================================================
 * 6b. Ahead-of-Time (AOT) XBF v2 -> Native Binary Scene-Graph Compiler & Cache
 * ============================================================================ */

#define XBF_AOT_MAGIC       0x58424641u /* 'XBFA' */
#define XBF_AOT_VERSION     1u
#define XBF_AOT_CACHE_SLOTS 64

enum xbf_aot_node_flags
{
    XBF_AOT_NODE_ROOT_REUSE  = 0x01,
    XBF_AOT_NODE_LIGHTWEIGHT = 0x02,
    XBF_AOT_NODE_BUILTIN     = 0x04,
    XBF_AOT_NODE_APP_TYPE    = 0x08,
    XBF_AOT_NODE_NO_PROVIDER = 0x10,
    XBF_AOT_NODE_USING_NS    = 0x20,
    XBF_AOT_NODE_IS_RD       = 0x40,
    XBF_AOT_NODE_DEFERRED    = 0x80,
};

enum xbf_aot_op_kind
{
    XBF_AOT_OP_CREATE_NODE = 1,
    XBF_AOT_OP_SET_XCLASS,
    XBF_AOT_OP_SET_XNAME,
    XBF_AOT_OP_CONNECT,
    XBF_AOT_OP_SET_PROP,
    XBF_AOT_OP_ADD_CHILD,
    XBF_AOT_OP_SET_OBJ_PROP,
    XBF_AOT_OP_PROVIDE_VALUE,
    XBF_AOT_OP_RD_LINK_THEME,
    XBF_AOT_OP_RD_STORE,
};

struct xbf_aot_header
{
    UINT32 magic;
    UINT32 version;
    UINT64 hash;
    UINT32 xbf_sz;
    INT32  only_sub_idx;
    UINT32 total_bytes;

    UINT32 type_cnt;
    UINT32 prop_cnt;
    UINT32 ref_cnt;
    UINT32 res_uri_cnt;
    UINT32 node_cnt;
    UINT32 op_cnt;
    UINT32 style_setter_cnt;
    UINT32 vsm_group_cnt;
    UINT32 vsm_state_cnt;
    UINT32 vsm_setter_cnt;
    UINT32 defer_cnt;
    UINT32 str_pool_bytes;

    UINT32 off_types;
    UINT32 off_props;
    UINT32 off_refs;
    UINT32 off_res_uris;
    UINT32 off_nodes;
    UINT32 off_ops;
    UINT32 off_style_setters;
    UINT32 off_vsm_groups;
    UINT32 off_vsm_states;
    UINT32 off_vsm_setters;
    UINT32 off_defers;
    UINT32 off_str_pool;
};

struct xbf_aot_ref
{
    UINT32 full_name_off;
    UINT32 short_name_off;
    UINT32 is_resource_path;
};

struct xbf_aot_node
{
    UINT32 class_name_off;
    UINT32 short_name_off;
    UINT16 tid;
    UINT8  flags;
    UINT8  has_init_argb;
    UINT32 init_argb;
};

struct xbf_aot_op
{
    UINT8  kind;
    UINT8  sv_kind;
    UINT8  is_res_ref;
    UINT8  has_argb;
    UINT16 pid;
    UINT16 tid;
    UINT32 node_idx;
    UINT32 child_idx;
    INT32  vt;
    INT32  cid;
    UINT32 str_off;
    UINT32 str2_off;
    UINT32 argb;
    UINT32 setter_start;
    UINT32 setter_cnt;
    double num;
    double vec4[4];
};

struct xbf_aot_vsm_group
{
    UINT32 name_off;
    UINT32 state_start;
    UINT32 state_cnt;
};

struct xbf_aot_vsm_state
{
    UINT32 name_off;
    UINT32 has_trigger;
    double min_window_width;
    double min_window_height;
    UINT32 setter_start;
    UINT32 setter_cnt;
};

struct xbf_aot_vsm_setter
{
    UINT32 target_off;
    UINT32 property_off;
    UINT32 val_str_off;
    INT32  vt;
    INT32  is_res_ref;
    double num;
};

struct xbf_aot_defer
{
    UINT32 name_off;
    INT32  sub_idx;
    UINT32 parent_panel_idx;
};

struct xbf_aot_builder
{
    UINT64 hash;
    UINT32 xbf_sz;
    INT32  only_sub_idx;
    BOOL   aborted;

    struct xbf_aot_ref *refs;
    UINT32 ref_cnt;
    int    ref_cap;

    UINT32 *res_uris;
    UINT32 res_uri_cnt;
    int    res_uri_cap;

    struct xbf_aot_node *nodes;
    UINT32 node_cnt;
    int    node_cap;

    struct xbf_aot_op *ops;
    UINT32 op_cnt;
    int    op_cap;

    struct uwp_style_setter *style_setters;
    UINT32 style_setter_cnt;
    int    style_setter_cap;

    struct xbf_aot_vsm_group *vsm_groups;
    UINT32 vsm_group_cnt;
    int    vsm_group_cap;

    struct xbf_aot_vsm_state *vsm_states;
    UINT32 vsm_state_cnt;
    int    vsm_state_cap;

    struct xbf_aot_vsm_setter *vsm_setters;
    UINT32 vsm_setter_cnt;
    int    vsm_setter_cap;

    struct xbf_aot_defer *defers;
    UINT32 defer_cnt;
    int    defer_cap;

    BYTE  *str_pool;
    UINT32 str_pool_bytes;
    int    str_pool_cap;
};

struct xbf_aot_cache_slot
{
    UINT64 hash;
    struct xbf_aot_header *tpl;
    UINT64 last_used;
    LONG   ref;
};

static SRWLOCK g_xbf_aot_lock = SRWLOCK_INIT;
static struct xbf_aot_cache_slot g_xbf_aot_cache[XBF_AOT_CACHE_SLOTS];
static UINT64 g_xbf_aot_lru = 0;

static UINT64 xbf_aot_compute_hash( const BYTE *xbf, DWORD xbf_sz, const WCHAR *root_cls, int only_sub_idx )
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

static UINT32 xbf_aot_intern_str( struct xbf_aot_builder *b, const WCHAR *s )
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

static void xbf_aot_add_ref( struct xbf_aot_builder *b, const WCHAR *full_name, const WCHAR *short_name, BOOL is_res )
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

static void xbf_aot_add_res_uri( struct xbf_aot_builder *b, const WCHAR *uri )
{
    if (!b || b->aborted || !uri || !uri[0]) return;
    if (!xaml_vec_grow( (void **)&b->res_uris, &b->res_uri_cap, b->res_uri_cnt + 1, sizeof(*b->res_uris), 8 ))
    {
        b->aborted = TRUE;
        return;
    }
    b->res_uris[b->res_uri_cnt++] = xbf_aot_intern_str( b, uri );
}

static UINT32 xbf_aot_add_node( struct xbf_aot_builder *b, const WCHAR *cls_name, const WCHAR *short_name,
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
    return b->node_cnt++;
}

static void xbf_aot_emit_op( struct xbf_aot_builder *b, const struct xbf_aot_op *op_in )
{
    if (!b || b->aborted) return;
    if (!xaml_vec_grow( (void **)&b->ops, &b->op_cap, b->op_cnt + 1, sizeof(*b->ops), 64 ))
    {
        b->aborted = TRUE;
        return;
    }
    b->ops[b->op_cnt++] = *op_in;
}

static void xbf_aot_add_defer( struct xbf_aot_builder *b, const WCHAR *name, int sub_idx, UINT32 parent_panel_idx )
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

static UINT32 xbf_aot_append_setters( struct xbf_aot_builder *b, const struct uwp_style_setter *setters, int cnt )
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

static void xbf_aot_snapshot_vsm( struct xbf_aot_builder *b, struct uwp_element *root_el, int start_group_idx )
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

static void xbf_aot_builder_free( struct xbf_aot_builder *b )
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

static struct xbf_aot_header *xbf_aot_finalize( struct xbf_aot_builder *b, const struct xbf_ctx *ctx )
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

static void xbf_aot_save_to_disk( const struct xbf_aot_header *hdr )
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

static struct xbf_aot_cache_slot *xbf_aot_cache_acquire( UINT64 hash, DWORD xbf_sz, int only_sub_idx, BOOL *from_disk )
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

static void xbf_aot_cache_insert( struct xbf_aot_header *tpl )
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

static void xbf_aot_instantiate( const struct xbf_aot_header *hdr, const BYTE *xbf, DWORD xbf_sz,
                                 IInspectable *component, void *connector, BOOL is_uwp_conn, void *app_outer,
                                 struct uwp_element *root_el, struct uwp_element **out_created,
                                 struct uwp_rd_bucket *target_rd )
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
                if (root_el && root_el->binding_connector)
                {
                    HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)root_el->binding_connector)[6];
                    if (b_conn) b_conn( root_el->binding_connector, op->cid, raw );
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
            struct uwp_element *child_el = (op->child_idx < hdr->node_cnt) ? nodes_el[op->child_idx] : NULL;
            IInspectable *child_raw = (op->child_idx < hdr->node_cnt) ? nodes_raw[op->child_idx] : NULL;
            if (!parent_el) break;
            if (op->pid == XBF_PROP_MERGED_DICTIONARIES && child_el && child_el->kind == UWP_KIND_RESOURCE_DICT)
            {
                struct uwp_rd_bucket *parent_rd = (parent_el->kind == UWP_KIND_RESOURCE_DICT)
                    ? uwp_rd_bucket_for_el( parent_el, TRUE )
                    : (target_rd ? target_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( child_el, TRUE );
                uwp_rd_link_merged( parent_rd, child_rd );
            }
            else if (child_raw)
            {
                if (op->pid == XBF_PROP_KEYBOARD_ACCELERATORS || (child_el && (child_el->kind == UWP_KIND_ACCELERATOR ||
                                                                              wcsstr( child_el->class_name, L"Accelerator" ))))
                {
                    if (!parent_el->accelerators_coll) uwp_vec_create( parent_el, &parent_el->accelerators_coll );
                    uwp_vec_Append( parent_el->accelerators_coll, child_raw );
                    if (child_el)
                    {
                        child_el->accel_target = parent_el;
                        uwp_accel_register( child_el, parent_el, child_el->accel_key, child_el->accel_mods, child_el->accel_enabled );
                    }
                }
                else if (op->pid == XBF_PROP_GRID_ROW_DEFINITIONS)
                {
                    if (!parent_el->row_defs_coll) uwp_vec_create( parent_el, &parent_el->row_defs_coll );
                    uwp_vec_Append( parent_el->row_defs_coll, child_raw );
                }
                else if (op->pid == XBF_PROP_GRID_COLUMN_DEFINITIONS)
                {
                    if (!parent_el->col_defs_coll) uwp_vec_create( parent_el, &parent_el->col_defs_coll );
                    uwp_vec_Append( parent_el->col_defs_coll, child_raw );
                }
                else if (op->pid == XBF_PROP_PANEL_CHILDREN || op->pid == XBF_PROP_ITEMS ||
                         parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL)
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
            else if (parent_el)
            {
                if (op->sv_kind == UWP_RD_BRUSH || op->sv_kind == UWP_RD_COLOR)
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
                         (op->pid == XBF_PROP_CONTEXT_FLYOUT ||
                          (op->pid < hdr->prop_cnt && !wcscmp( fake_ctx.props[op->pid].name, L"ContextFlyout" ))))
                {
                    if (parent_el->context_flyout) IInspectable_Release( parent_el->context_flyout );
                    parent_el->context_flyout = child_raw;
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
                uwp_rd_store_stack_val( dest_rd, key, &sv );
                if (target_rd && target_rd != dest_rd)
                    uwp_rd_store_stack_val( target_rd, key, &sv );
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

    free( nodes_el );
    free( nodes_raw );
}

static void xbf_exec_substream_rd( struct xbf_ctx *ctx, int sub_idx, struct uwp_element *initial_root,
                                   struct uwp_element **out_created, const struct xbf_rd_table *rd_tbl,
                                   struct uwp_rd_bucket *active_rd )
{
    DWORD sub_start = 0, end_p = 0, sub_sz = 0, p;
    struct uwp_element *obj_stack[64];
    IInspectable *raw_stack[64];
    struct xbf_stack_val val_stack[64];
    DWORD start_off_stack[64];
    UINT32 node_idx_stack[64];
    int obj_sp = 0, base_sp = 0;
    WORD prop_stack[64];
    int prop_sp = 0;
    struct xbf_stack_val last_popped;
    DWORD last_popped_off = 0;
    UINT32 last_popped_node_idx = (~0u);
    BOOL has_last_popped = FALSE;

    memset( val_stack, 0, sizeof(val_stack) );
    memset( &last_popped, 0, sizeof(last_popped) );
    memset( start_off_stack, 0, sizeof(start_off_stack) );
    memset( node_idx_stack, 0xff, sizeof(node_idx_stack) );

    if (out_created) *out_created = NULL;
    if (sub_idx < 0 || !xbf_get_substream_bounds( ctx, (DWORD)sub_idx, &sub_start, &end_p, &sub_sz ))
        return;
    if (ctx->sub_done)
    {
        if (ctx->sub_done[sub_idx]) return;
        ctx->sub_done[sub_idx] = TRUE;
    }

    p = sub_start;

    if (initial_root)
    {
        obj_stack[0] = initial_root;
        raw_stack[0] = initial_root->outer ? initial_root->outer : &initial_root->IInspectable_iface;
        node_idx_stack[0] = (ctx->aot && initial_root == ctx->root_el) ? 0u : (~0u);
        obj_sp = 1;
        base_sp = 1;
    }

    while (p < end_p)
    {
        DWORD cur_rel_off = p - sub_start;
        BYTE op = ctx->nodes[p++];
        if (op == 0x12) /* LineInfo (6 bytes) */
        {
            if (end_p - p < 6) break;
            p += 6;
        }
        else if (op == 0x03) /* Xmlns prefix */
        {
            DWORD plen;
            if (end_p - p < 6) break;
            plen = *(const DWORD *)(ctx->nodes + p + 2);
            if (plen > (end_p - (p + 6)) / 2) break;
            p += 6 + plen * 2;
        }
        else if (op == 0x17 || op == 0x14 || op == 0x15 || op == 0x16)
        {
            WORD tid;
            const WCHAR *cls_name = L"Windows.UI.Xaml.Controls.ContentControl";
            IInspectable *inst = NULL;
            struct uwp_element *el = NULL;
            struct xbf_stack_val sv = {0};
            int vt_init = 0;
            double num_init = 0, vec4_init[4] = {0};
            WCHAR sbuf_init[256] = {0};
            BOOL is_rd_type = FALSE;

            sv.opacity = 1.0;
            if (p + 2 > end_p) break;
            tid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (op == 0x15 || op == 0x16)
            {
                if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt_init, &num_init, sbuf_init, ARRAY_SIZE(sbuf_init), vec4_init ))
                    break;
            }
            TRACE( "XBF push op=%#x tid=%#x cls=%s at %lu/%lu (sub %d)\n", op, tid,
                   (tid < ctx->type_cnt) ? debugstr_w(ctx->types[tid].full_name) : "(btype)",
                   (unsigned long)cur_rel_off, (unsigned long)end_p, sub_idx );

            if (tid == XBF_TYPE_RESOURCE_DICTIONARY) is_rd_type = TRUE;
            else if (!(tid & 0x8000) && tid < ctx->type_cnt &&
                     (wcsstr( ctx->types[tid].short_name, L"ResourceDictionary" ) ||
                      !wcscmp( ctx->types[tid].short_name, L"XamlControlsResources" )))
                is_rd_type = TRUE;

            if (tid == XBF_TYPE_COLOR)
            {
                sv.kind = UWP_RD_COLOR;
                if (vt_init == 0x08) { sv.argb = (UINT32)num_init; sv.has_argb = TRUE; }
                else if (sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv.argb )) sv.has_argb = TRUE;
            }
            else if (tid == XBF_TYPE_DOUBLE)
            {
                sv.kind = UWP_RD_DOUBLE;
                sv.dval = num_init;
            }
            else if (tid == XBF_TYPE_GRID_LENGTH)
            {
                sv.kind = UWP_RD_DOUBLE;
                sv.dval = (vt_init == 0x07) ? vec4_init[1] : num_init;
            }
            else if (tid == XBF_TYPE_THICKNESS)
            {
                sv.kind = UWP_RD_THICKNESS;
                memcpy( sv.vec4, vec4_init, sizeof(sv.vec4) );
            }
            else if (tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_BRUSH_A || tid == XBF_TYPE_BRUSH_B ||
                     (!(tid & 0x8000) && tid < ctx->type_cnt && wcsstr( ctx->types[tid].short_name, L"Brush" )))
            {
                sv.kind = UWP_RD_BRUSH;
                if (op == 0x16 && sbuf_init[0] && uwp_parse_color_string( sbuf_init, &sv.argb ))
                    sv.has_argb = TRUE;
            }
            else if (tid == XBF_TYPE_STYLE)
            {
                sv.kind = UWP_RD_STYLE;
            }
            else if ((op == 0x15 || op == 0x16) && sbuf_init[0])
            {
                sv.kind = UWP_RD_STRING;
                lstrcpynW( sv.str_val, sbuf_init, ARRAY_SIZE(sv.str_val) );
            }

            if (op == 0x17 && ctx->root_el)
            {
                el = ctx->root_el;
                inst = ctx->root_comp;
                if (is_rd_type && active_rd)
                {
                    el->rd_bucket = active_rd;
                    active_rd->owner_el = el;
                }
            }
            else if ((ctx->rd_only || rd_tbl) && !is_rd_type &&
                     (!ctx->connector ||
                      tid == XBF_TYPE_STYLE || tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_SETTER_A || tid == XBF_TYPE_SETTER_B ||
                      tid == XBF_TYPE_BRUSH_A || tid == XBF_TYPE_BRUSH_B || tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_STRING ||
                      tid == XBF_TYPE_THICKNESS || tid == XBF_TYPE_COLUMN_DEFINITION || tid == XBF_TYPE_RESOURCE_DICTIONARY))
            {
                /* Lightweight mode inside ResourceDictionary XBFs: no COM/visual element needed */
                el = NULL;
                inst = NULL;
            }
            else
            {
                if (tid & 0x8000)
                {
                    cls_name = uwp_btype_to_class_name( tid );
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                    if (el && tid == XBF_TYPE_DEFERRED_ELEMENT) el->kind = UWP_KIND_DEFERRED;
                }
                else if (tid < ctx->type_cnt)
                {
                    cls_name = ctx->types[tid].full_name;
                    if (ctx->root_el) uwp_mark_xbf_ref_instantiated( ctx->root_el, cls_name );
                    /* Do not activate ResourceString or ResourceVirtualKey via provider */
                    if (is_rd_type || wcsstr( cls_name, L"ResourceString" ) ||
                        wcsstr( cls_name, L"ResourceVirtualKey" ))
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
                                !wcsncmp( ctx->types[tid].xmlns, L"using:", 6 ))
                            {
                                uwp_populate_element_from_pri_xbf( el, ctx->types[tid].short_name );
                            }
                        }
                    }
                }
                else
                {
                    uwp_element_create( cls_name, NULL, NULL, (void **)&inst );
                    el = uwp_elem_from_any( inst );
                }
            }

            if (el)
            {
                if (ctx->root_el) el->owner_component = ctx->root_el;
                if (sv.kind == UWP_RD_BRUSH && sv.has_argb)
                {
                    el->brush_argb = sv.argb;
                    el->has_brush_argb = TRUE;
                }
                if (is_rd_type)
                {
                    if (op == 0x17 && active_rd)
                    {
                        el->rd_bucket = active_rd;
                        active_rd->owner_el = el;
                    }
                    else
                    {
                        uwp_rd_bucket_for_el( el, TRUE );
                    }
                }
            }
            sv.tid = tid;
            sv.el = el;
            sv.raw = inst;
            if (out_created && !*out_created && el) *out_created = el;

            if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el)
            {
                if (tid == XBF_TYPE_VISUAL_STATE_GROUP)
                    uwp_vsm_add_group( ctx->root_el, NULL );
                else if (tid == XBF_TYPE_VISUAL_STATE)
                {
                    struct uwp_vsm_group *grp = (ctx->root_el->vsm_group_cnt > 0)
                        ? &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1]
                        : uwp_vsm_add_group( ctx->root_el, L"DefaultGroup" );
                    uwp_vsm_add_state( grp, NULL );
                }
            }

            if (obj_sp < (int)ARRAY_SIZE(obj_stack))
            {
                UINT32 cur_node_idx = (~0u);
                if (ctx->aot && !ctx->aot->aborted)
                {
                    UINT8 nflags = 0;
                    const WCHAR *short_n = L"";
                    if (is_rd_type) nflags |= XBF_AOT_NODE_IS_RD;
                    if (op == 0x17 && ctx->root_el)
                    {
                        nflags |= XBF_AOT_NODE_ROOT_REUSE;
                        cur_node_idx = 0;
                        if (ctx->aot->node_cnt > 0)
                            ctx->aot->nodes[0].flags |= nflags;
                    }
                    else if ((ctx->rd_only || rd_tbl) && !is_rd_type &&
                             (!ctx->connector ||
                              tid == XBF_TYPE_STYLE || tid == XBF_TYPE_BRUSH_C || tid == XBF_TYPE_SETTER_A || tid == XBF_TYPE_SETTER_B ||
                              tid == XBF_TYPE_BRUSH_A || tid == XBF_TYPE_BRUSH_B || tid == XBF_TYPE_DOUBLE || tid == XBF_TYPE_STRING ||
                              tid == XBF_TYPE_THICKNESS || tid == XBF_TYPE_COLUMN_DEFINITION || tid == XBF_TYPE_RESOURCE_DICTIONARY))
                    {
                        nflags |= XBF_AOT_NODE_LIGHTWEIGHT;
                        cur_node_idx = xbf_aot_add_node( ctx->aot, L"", L"", tid, nflags, FALSE, 0 );
                    }
                    else if (tid & 0x8000)
                    {
                        nflags |= XBF_AOT_NODE_BUILTIN;
                        if (tid == XBF_TYPE_DEFERRED_ELEMENT) nflags |= XBF_AOT_NODE_DEFERRED;
                        cur_node_idx = xbf_aot_add_node( ctx->aot, cls_name, L"", tid, nflags,
                                                         (sv.kind == UWP_RD_BRUSH && sv.has_argb), sv.argb );
                    }
                    else if (tid < ctx->type_cnt)
                    {
                        nflags |= XBF_AOT_NODE_APP_TYPE;
                        short_n = ctx->types[tid].short_name;
                        if (!wcsncmp( ctx->types[tid].xmlns, L"using:", 6 )) nflags |= XBF_AOT_NODE_USING_NS;
                        if (is_rd_type || wcsstr( cls_name, L"ResourceString" ) || wcsstr( cls_name, L"ResourceVirtualKey" ))
                            nflags |= XBF_AOT_NODE_NO_PROVIDER;
                        cur_node_idx = xbf_aot_add_node( ctx->aot, cls_name, short_n, tid, nflags,
                                                         (sv.kind == UWP_RD_BRUSH && sv.has_argb), sv.argb );
                    }
                    else
                    {
                        cur_node_idx = xbf_aot_add_node( ctx->aot, cls_name, L"", tid, nflags,
                                                         (sv.kind == UWP_RD_BRUSH && sv.has_argb), sv.argb );
                    }
                    if (cur_node_idx != (~0u))
                    {
                        struct xbf_aot_op aop = {0};
                        aop.kind = XBF_AOT_OP_CREATE_NODE;
                        aop.node_idx = cur_node_idx;
                        aop.child_idx = (~0u);
                        xbf_aot_emit_op( ctx->aot, &aop );
                    }
                }
                obj_stack[obj_sp] = el;
                raw_stack[obj_sp] = inst;
                val_stack[obj_sp] = sv;
                start_off_stack[obj_sp] = cur_rel_off;
                node_idx_stack[obj_sp] = cur_node_idx;
                obj_sp++;
            }
            else
            {
                free( sv.setters );
            }
        }
        else if (op == 0x0b) /* x:Class */
        {
            DWORD slen;
            if (end_p - p < 4) break;
            slen = *(const DWORD *)(ctx->nodes + p);
            p += 4;
            if (slen > (end_p - p) / 2) break;
            if (obj_sp > 0 && obj_stack[obj_sp - 1] && slen > 0)
            {
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                DWORD cp = min( slen, (DWORD)(ARRAY_SIZE(top_el->x_class) - 1) );
                memcpy( top_el->x_class, ctx->nodes + p, cp * sizeof(WCHAR) );
                top_el->x_class[cp] = 0;
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_XCLASS;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = (~0u);
                    aop.str_off = xbf_aot_intern_str( ctx->aot, top_el->x_class );
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            p += slen * 2;
        }
        else if (op == 0x0c) /* ConnectionId */
        {
            int vt = 0;
            double num = 0;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, NULL, 0, NULL )) break;
            if (obj_sp > 0)
            {
                int cid = (int)num;
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                IInspectable *top_raw = raw_stack[obj_sp - 1];
                if (top_el) top_el->conn_id = cid;
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_CONNECT;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = (~0u);
                    aop.cid = cid;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                if (ctx->connector && top_raw && cid != 1)
                {
                    HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)ctx->connector)[6];
                    TRACE( "XBF Connect(cid=%d, target=%p [%s %s])\n",
                           cid, top_raw, top_el ? debugstr_w(top_el->class_name) : "?",
                           top_el ? debugstr_w(top_el->name) : "" );
                    conn_fn( ctx->connector, cid, top_raw );
                    if (ctx->root_el && ctx->root_el->binding_connector)
                    {
                        HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)ctx->root_el->binding_connector)[6];
                        if (b_conn) b_conn( ctx->root_el->binding_connector, cid, top_raw );
                    }
                }
            }
        }
        else if (op == 0x0d) /* x:Name */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            if (obj_sp > 0 && sbuf[0])
            {
                struct uwp_element *top_el = obj_stack[obj_sp - 1];
                WORD top_tid = val_stack[obj_sp - 1].tid;
                if (top_el)
                {
                    lstrcpynW( top_el->name, sbuf, ARRAY_SIZE(top_el->name) );
                    if (ctx->root_el &&
                        xaml_vec_grow( (void **)&ctx->root_el->named_elems, &ctx->root_el->named_cap,
                                       ctx->root_el->named_cnt + 1, sizeof(*ctx->root_el->named_elems), 32 ))
                    {
                        ctx->root_el->named_elems[ctx->root_el->named_cnt++] = top_el;
                    }
                    if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u))
                    {
                        struct xbf_aot_op aop = {0};
                        aop.kind = XBF_AOT_OP_SET_XNAME;
                        aop.node_idx = node_idx_stack[obj_sp - 1];
                        aop.child_idx = (~0u);
                        aop.str_off = xbf_aot_intern_str( ctx->aot, sbuf );
                        xbf_aot_emit_op( ctx->aot, &aop );
                    }
                }
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    if (top_tid == XBF_TYPE_VISUAL_STATE_GROUP)
                        lstrcpynW( grp->name, sbuf, ARRAY_SIZE(grp->name) );
                    else if (top_tid == XBF_TYPE_VISUAL_STATE && grp->state_cnt > 0)
                        lstrcpynW( grp->states[grp->state_cnt - 1].name, sbuf, ARRAY_SIZE(grp->states[grp->state_cnt - 1].name) );
                }
            }
        }
        else if (op == 0x0e) /* x:Key */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
        }
        else if (op == 0x13) /* PushProperty */
        {
            WORD pid;
            if (p + 2 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (prop_sp < (int)ARRAY_SIZE(prop_stack))
                prop_stack[prop_sp++] = pid;
        }
        else if (op == 0x01 || op == 0x27) /* EndOfAttributes / EndConditionalScope */
        {
        }
        else if (op == 0x26) /* BeginConditionalScope <pred_tid:u16> <arg_sid:u16> */
        {
            if (p + 4 > end_p) break;
            p += 4;
        }
        else if (op == 0x11) /* SetDeferredProperty <pid:u16> <sub_idx:uleb> <c1:uleb> <c2:uleb> <(c1+c2)*u16> */
        {
            DWORD sub_i = 0, def_sz = 0, c1 = 0, c2 = 0;
            if (end_p - p < 2) break;
            p += 2;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &sub_i )) break;
            if (!xbf_get_substream_bounds( ctx, sub_i, NULL, NULL, &def_sz )) break;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c1 )) break;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &c2 )) break;
            (void)def_sz;
            if (c1 > (end_p - p) / 2 || c2 > (end_p - p) / 2 - c1) break;
            p += (c1 + c2) * 2;
        }
        else if (op == 0x02) /* PopProperty */
        {
            if (prop_sp > 0) prop_sp--;
        }
        else if (op == 0x1d) /* SetValueWithType: <pid:u16> <tid:u16> */
        {
            WORD pid, tid;
            if (p + 4 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            tid = *(const WORD *)(ctx->nodes + p + 2);
            (void)pid; (void)tid;
            p += 4;
        }
        else if ((op >= 0x1a && op <= 0x1f) || op == 0x24) /* SetValue / SetStaticResource / SetThemeResource */
        {
            WORD pid;
            int vt = 0;
            double num = 0, vec4[4] = {0};
            WCHAR sbuf[256] = {0};
            BOOL is_res_ref = (op == 0x1e || op == 0x24);
            if (p + 2 > end_p) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), vec4 )) break;
            if (obj_sp > 0)
            {
                struct xbf_stack_val *top_sv = &val_stack[obj_sp - 1];
                xbf_apply_property( ctx, obj_stack[obj_sp - 1], top_sv,
                                    pid, vt, num, sbuf, vec4, is_res_ref );
                if (ctx->aot && obj_stack[obj_sp - 1] && node_idx_stack[obj_sp - 1] != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_PROP;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = (~0u);
                    aop.pid = pid;
                    aop.vt = vt;
                    aop.num = num;
                    memcpy( aop.vec4, vec4, sizeof(aop.vec4) );
                    aop.str_off = xbf_aot_intern_str( ctx->aot, sbuf );
                    aop.is_res_ref = is_res_ref ? 1 : 0;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    struct uwp_vsm_state *st = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
                    if (top_sv->tid == XBF_TYPE_ADAPTIVE_TRIGGER && st && (pid == XBF_PROP_MIN_WINDOW_HEIGHT || pid == XBF_PROP_MIN_WINDOW_WIDTH))
                    {
                        double trig_val = num;
                        if ((is_res_ref || vt == 0x05 || vt == 0x09) && sbuf[0])
                        {
                            struct uwp_rd_entry *de = NULL;
                            if (uwp_rd_find_entry( active_rd ? active_rd : ctx->target_rd, ctx->root_el, sbuf, 0, &de, NULL ) &&
                                de && de->kind == UWP_RD_DOUBLE)
                                trig_val = de->dval;
                            else
                                trig_val = wcstod( sbuf, NULL );
                        }
                        if (pid == XBF_PROP_MIN_WINDOW_HEIGHT) st->min_window_height = trig_val;
                        else st->min_window_width = trig_val;
                        st->has_trigger = TRUE;
                    }
                    else if ((top_sv->tid == XBF_TYPE_SETTER_A || top_sv->tid == XBF_TYPE_SETTER_B) && st)
                    {
                        if (pid == XBF_PROP_SETTER_TARGET && sbuf[0])
                            vsm_split_setter_target( sbuf, top_sv->vsm_target, ARRAY_SIZE(top_sv->vsm_target),
                                                     top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
                        else if (pid == XBF_PROP_SETTER_VALUE)
                        {
                            lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                            top_sv->vsm_vt = vt;
                            top_sv->vsm_num = num;
                            top_sv->vsm_is_res_ref = is_res_ref;
                            top_sv->vsm_has_val = TRUE;
                        }
                    }
                    else if (top_sv->tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES && st)
                    {
                        if (pid == XBF_PROP_STORYBOARD_TARGET_NAME && sbuf[0])
                            lstrcpynW( top_sv->vsm_target, sbuf, ARRAY_SIZE(top_sv->vsm_target) );
                        else if (pid == XBF_PROP_STORYBOARD_TARGET_PROPERTY && sbuf[0])
                            vsm_clean_prop_name( sbuf, top_sv->vsm_prop, ARRAY_SIZE(top_sv->vsm_prop) );
                    }
                    else if (top_sv->tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && st && pid == XBF_PROP_KEYFRAME_VALUE)
                    {
                        lstrcpynW( top_sv->vsm_val_str, sbuf, ARRAY_SIZE(top_sv->vsm_val_str) );
                        top_sv->vsm_vt = vt;
                        top_sv->vsm_num = num;
                        top_sv->vsm_is_res_ref = is_res_ref;
                        top_sv->vsm_has_val = TRUE;
                    }
                }
            }
        }
        else if (op == 0x22) /* AddStaticResourceValue <xbf_value> */
        {
            int vt = 0;
            double num = 0;
            WCHAR sbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, sbuf, ARRAY_SIZE(sbuf), NULL )) break;
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped.kind = UWP_RD_ALIAS;
            last_popped.opacity = 1.0;
            lstrcpynW( last_popped.str_val, sbuf, ARRAY_SIZE(last_popped.str_val) );
            last_popped_off = cur_rel_off;
            last_popped_node_idx = (~0u);
            has_last_popped = TRUE;
        }
        else if (op == 0x21) /* EndOfObject / PopObject */
        {
            if (obj_sp > base_sp)
            {
                free( last_popped.setters );
                obj_sp--;
                last_popped = val_stack[obj_sp];
                last_popped_off = start_off_stack[obj_sp];
                last_popped_node_idx = node_idx_stack[obj_sp];
                memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) );
                has_last_popped = TRUE;

                if ((sub_idx == 0 || ctx->vsm_collect) && !ctx->rd_only && ctx->root_el && ctx->root_el->vsm_group_cnt > 0)
                {
                    struct uwp_vsm_group *grp = &ctx->root_el->vsm_groups[ctx->root_el->vsm_group_cnt - 1];
                    struct uwp_vsm_state *st = (grp->state_cnt > 0) ? &grp->states[grp->state_cnt - 1] : NULL;
                    if ((last_popped.tid == XBF_TYPE_SETTER_A || last_popped.tid == XBF_TYPE_SETTER_B) && st &&
                        last_popped.vsm_target[0] && last_popped.vsm_prop[0] && last_popped.vsm_has_val)
                    {
                        uwp_vsm_state_add_setter( st, last_popped.vsm_target, last_popped.vsm_prop,
                                                  last_popped.vsm_val_str, last_popped.vsm_vt,
                                                  last_popped.vsm_num, last_popped.vsm_is_res_ref );
                    }
                    else if (last_popped.tid == XBF_TYPE_DISCRETE_OBJECT_KEY_FRAME && st && last_popped.vsm_has_val &&
                             obj_sp > 0 && val_stack[obj_sp - 1].tid == XBF_TYPE_OBJECT_ANIMATION_USING_KEY_FRAMES &&
                             val_stack[obj_sp - 1].vsm_target[0] && val_stack[obj_sp - 1].vsm_prop[0])
                    {
                        uwp_vsm_state_add_setter( st, val_stack[obj_sp - 1].vsm_target,
                                                  val_stack[obj_sp - 1].vsm_prop,
                                                  last_popped.vsm_val_str, last_popped.vsm_vt,
                                                  last_popped.vsm_num, last_popped.vsm_is_res_ref );
                    }
                }

                /* Indexed ResourceDictionary entry without trailing 0x0a AddToDictionary (e.g. Dark.xbf) */
                if (active_rd && rd_tbl && obj_sp == base_sp)
                {
                    BYTE next_op = (p < end_p) ? ctx->nodes[p] : 0xff;
                    if (next_op != 0x0a && next_op != 0x07 && next_op != 0x08 && next_op != 0x09 && next_op != 0x20)
                    {
                        int k;
                        for (k = 0; k < rd_tbl->count; k++)
                        {
                            if (rd_tbl->items[k].sub_off == last_popped_off &&
                                rd_tbl->items[k].key_sid < ctx->str_cnt && ctx->strs[rd_tbl->items[k].key_sid])
                            {
                                uwp_rd_store_stack_val( active_rd, ctx->strs[rd_tbl->items[k].key_sid], &last_popped );
                            }
                        }
                        free( last_popped.setters );
                        memset( &last_popped, 0, sizeof(last_popped) );
                        has_last_popped = FALSE;
                    }
                }
            }
            else
            {
                free( last_popped.setters );
                memset( &last_popped, 0, sizeof(last_popped) );
                last_popped_node_idx = (~0u);
                has_last_popped = FALSE;
            }
        }
        else if (op == 0x0a) /* AddToDictionary <xbf_value> */
        {
            int vt = 0;
            double num = 0;
            WCHAR kbuf[128] = {0};
            if (!xbf_read_value( ctx, ctx->nodes, &p, end_p, &vt, &num, kbuf, ARRAY_SIZE(kbuf), NULL )) break;
            if (has_last_popped && kbuf[0])
            {
                struct uwp_element *parent_el = (obj_sp > 0) ? obj_stack[obj_sp - 1] : NULL;
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : 0;
                if (pid == XBF_PROP_THEME_DICTIONARIES && last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT)
                {
                    struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                    struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    uwp_rd_link_theme( parent_rd, kbuf, child_rd );
                    if (ctx->aot && last_popped_node_idx != (~0u))
                    {
                        struct xbf_aot_op aop = {0};
                        aop.kind = XBF_AOT_OP_RD_LINK_THEME;
                        aop.node_idx = (obj_sp > 0) ? node_idx_stack[obj_sp - 1] : (~0u);
                        aop.child_idx = last_popped_node_idx;
                        aop.str_off = xbf_aot_intern_str( ctx->aot, kbuf );
                        xbf_aot_emit_op( ctx->aot, &aop );
                    }
                }
                else
                {
                    struct uwp_rd_bucket *dest_rd = NULL;
                    if (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        dest_rd = uwp_rd_bucket_for_el( parent_el, TRUE );
                    else
                        dest_rd = active_rd ? active_rd : (parent_el ? uwp_rd_bucket_for_el( parent_el, TRUE ) : NULL);
                    if (dest_rd)
                    {
                        uwp_rd_store_stack_val( dest_rd, kbuf, &last_popped );
                        if (active_rd && active_rd != dest_rd)
                            uwp_rd_store_stack_val( active_rd, kbuf, &last_popped );
                        if (ctx->aot)
                        {
                            struct xbf_aot_op aop = {0};
                            aop.kind = XBF_AOT_OP_RD_STORE;
                            aop.sv_kind = (UINT8)last_popped.kind;
                            aop.has_argb = last_popped.has_argb ? 1 : 0;
                            aop.tid = last_popped.tid;
                            aop.node_idx = (obj_sp > 0) ? node_idx_stack[obj_sp - 1] : (~0u);
                            aop.child_idx = last_popped_node_idx;
                            aop.str_off = xbf_aot_intern_str( ctx->aot, kbuf );
                            aop.str2_off = xbf_aot_intern_str( ctx->aot,
                                (last_popped.kind == UWP_RD_STYLE) ? last_popped.based_on : last_popped.str_val );
                            aop.argb = last_popped.argb;
                            aop.num = (last_popped.kind == UWP_RD_BRUSH || last_popped.kind == UWP_RD_COLOR)
                                ? last_popped.opacity : last_popped.dval;
                            memcpy( aop.vec4, last_popped.vec4, sizeof(aop.vec4) );
                            if (last_popped.setter_cnt > 0 && last_popped.setters)
                            {
                                aop.setter_start = xbf_aot_append_setters( ctx->aot, last_popped.setters, last_popped.setter_cnt );
                                aop.setter_cnt = (UINT32)last_popped.setter_cnt;
                            }
                            xbf_aot_emit_op( ctx->aot, &aop );
                        }
                    }
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x08) /* AddToCollection */
        {
            if (has_last_popped && obj_sp > 0)
            {
                struct uwp_element *parent_el = obj_stack[obj_sp - 1];
                WORD pid = (prop_sp > 0) ? prop_stack[prop_sp - 1] : XBF_PROP_PANEL_CHILDREN;
                if (pid == XBF_PROP_MERGED_DICTIONARIES && last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT)
                {
                    struct uwp_rd_bucket *parent_rd = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : uwp_rd_bucket_for_el( parent_el, TRUE ));
                    struct uwp_rd_bucket *child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    uwp_rd_link_merged( parent_rd, child_rd );
                }
                else if (parent_el && last_popped.raw)
                {
                    if (pid == XBF_PROP_KEYBOARD_ACCELERATORS || (last_popped.el && (last_popped.el->kind == UWP_KIND_ACCELERATOR ||
                                                             wcsstr( last_popped.el->class_name, L"Accelerator" ))))
                    {
                        if (!parent_el->accelerators_coll) uwp_vec_create( parent_el, &parent_el->accelerators_coll );
                        uwp_vec_Append( parent_el->accelerators_coll, last_popped.raw );
                        if (last_popped.el)
                        {
                            last_popped.el->accel_target = parent_el;
                            uwp_accel_register( last_popped.el, parent_el,
                                                last_popped.el->accel_key, last_popped.el->accel_mods,
                                                last_popped.el->accel_enabled );
                        }
                    }
                    else if (pid == XBF_PROP_GRID_ROW_DEFINITIONS) /* RowDefinitions */
                    {
                        if (!parent_el->row_defs_coll) uwp_vec_create( parent_el, &parent_el->row_defs_coll );
                        uwp_vec_Append( parent_el->row_defs_coll, last_popped.raw );
                    }
                    else if (pid == XBF_PROP_GRID_COLUMN_DEFINITIONS) /* ColumnDefinitions */
                    {
                        if (!parent_el->col_defs_coll) uwp_vec_create( parent_el, &parent_el->col_defs_coll );
                        uwp_vec_Append( parent_el->col_defs_coll, last_popped.raw );
                    }
                    else if (pid == XBF_PROP_PANEL_CHILDREN || pid == XBF_PROP_ITEMS || parent_el->kind == UWP_KIND_GRID || parent_el->kind == UWP_KIND_STACK_PANEL)
                    {
                        if (!parent_el->children_coll) uwp_vec_create( parent_el, &parent_el->children_coll );
                        uwp_vec_Append( parent_el->children_coll, last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                }
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u) && last_popped_node_idx != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_ADD_CHILD;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = last_popped_node_idx;
                    aop.pid = pid;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x07) /* SetObjectProperty <pid:u16> */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            TRACE( "xbf-trace: op=0x07 pid=%#x has_last=%d last_el=%p\n",
                   pid, has_last_popped, last_popped.el );
            if (has_last_popped)
            {
                struct uwp_element *parent_el = (obj_sp > 0) ? obj_stack[obj_sp - 1] : NULL;
                BOOL is_dict = (last_popped.el && last_popped.el->kind == UWP_KIND_RESOURCE_DICT);
                struct uwp_rd_bucket *child_rd = last_popped.el ? uwp_rd_bucket_for_el( last_popped.el, FALSE ) : NULL;

                if (is_dict || (child_rd && (pid == XBF_PROP_FRAMEWORKELEMENT_RESOURCES || pid == XBF_PROP_MERGED_DICTIONARIES || pid == XBF_PROP_THEME_DICTIONARIES)))
                {
                    struct uwp_rd_bucket *dest_b = (parent_el && parent_el->kind == UWP_KIND_RESOURCE_DICT)
                        ? uwp_rd_bucket_for_el( parent_el, TRUE )
                        : (active_rd ? active_rd : ctx->target_rd);
                    if (!child_rd && last_popped.el)
                        child_rd = uwp_rd_bucket_for_el( last_popped.el, TRUE );
                    if (dest_b && child_rd && dest_b != child_rd)
                    {
                        if (pid == XBF_PROP_THEME_DICTIONARIES) /* ThemeDictionaries */
                            uwp_rd_link_theme( dest_b, (last_popped.el && last_popped.el->name[0]) ? last_popped.el->name : L"Default", child_rd );
                        else
                            uwp_rd_link_merged( dest_b, child_rd );
                        TRACE( "xbf-res: linked child_rd %p into dest_b %p (parent_el %p pid %#x)\n",
                               child_rd, dest_b, parent_el, pid );
                    }
                }
                else if (parent_el)
                {
                    if (last_popped.kind == UWP_RD_BRUSH || last_popped.kind == UWP_RD_COLOR)
                    {
                        UINT32 argb = 0;
                        BOOL got = FALSE;
                        if (last_popped.has_argb)
                        {
                            argb = apply_opacity_to_argb( last_popped.argb, last_popped.opacity );
                            got = TRUE;
                        }
                        else if (last_popped.str_val[0] &&
                                 uwp_resolve_brush_or_color( active_rd ? active_rd : ctx->target_rd, parent_el,
                                                             last_popped.str_val, 0, &argb ))
                        {
                            argb = apply_opacity_to_argb( argb, last_popped.opacity );
                            got = TRUE;
                        }
                        else if (last_popped.el && last_popped.el->has_brush_argb)
                        {
                            argb = apply_opacity_to_argb( last_popped.el->brush_argb, last_popped.el->brush_opacity );
                            got = TRUE;
                        }
                        if (got)
                        {
                            if (uwp_is_bg_pid( pid )) uwp_el_set_bg_argb( parent_el, argb );
                            else if (uwp_is_fg_pid( pid )) uwp_el_set_fg_argb( parent_el, argb );
                            else if (uwp_is_border_pid( pid )) uwp_el_set_border_argb( parent_el, argb );
                        }
                    }
                    else if (last_popped.kind == UWP_RD_STYLE && pid == XBF_PROP_STYLE)
                    {
                        int si;
                        if (last_popped.based_on[0])
                            uwp_apply_style_by_name( ctx, parent_el, last_popped.based_on );
                        for (si = 0; si < last_popped.setter_cnt; si++)
                        {
                            const struct uwp_style_setter *st = &last_popped.setters[si];
                            xbf_apply_property( ctx, parent_el, NULL, st->pid, st->vt, st->num, st->str, st->vec4, st->is_res_ref );
                        }
                    }
                    else if (pid == 0x000c && last_popped.el && last_popped.el->text[0])
                    {
                        if (!parent_el->text[0])
                            lstrcpynW( parent_el->text, last_popped.el->text, ARRAY_SIZE(parent_el->text) );
                    }
                    else if (last_popped.raw &&
                             (pid == XBF_PROP_CONTEXT_FLYOUT ||
                              (ctx && pid < ctx->prop_cnt && !wcscmp( ctx->props[pid].name, L"ContextFlyout" ))))
                    {
                        if (parent_el->context_flyout) IInspectable_Release( parent_el->context_flyout );
                        parent_el->context_flyout = last_popped.raw;
                        IInspectable_AddRef( last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                    else if (last_popped.raw &&
                             last_popped.kind == UWP_RD_OBJ &&
                             (!last_popped.el ||
                              (last_popped.el->kind != UWP_KIND_STYLE &&
                               last_popped.el->kind != UWP_KIND_BRUSH &&
                               last_popped.el->kind != UWP_KIND_COLOR)) &&
                             (pid == XBF_PROP_CONTENT || pid == XBF_PROP_USERCONTROL_CONTENT || pid == XBF_PROP_BORDER_CHILD || pid == XBF_PROP_RENDER_TRANSFORM_ORIGIN || pid == XBF_PROP_POPUP_CHILD ||
                              (!parent_el->content && pid != XBF_PROP_UIELEMENT_RENDER_TRANSFORM && pid != XBF_PROP_FRAMEWORKELEMENT_RESOURCES && pid != XBF_PROP_AUTOMATION_NAME && pid != 0x000c)))
                    {
                        if (parent_el->content) IInspectable_Release( parent_el->content );
                        parent_el->content = last_popped.raw;
                        IInspectable_AddRef( last_popped.raw );
                        if (last_popped.el) last_popped.el->parent = parent_el;
                    }
                }
                if (ctx->aot && (obj_sp == 0 || node_idx_stack[obj_sp - 1] != (~0u)))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_SET_OBJ_PROP;
                    aop.sv_kind = (UINT8)last_popped.kind;
                    aop.has_argb = last_popped.has_argb ? 1 : 0;
                    aop.pid = pid;
                    aop.node_idx = (obj_sp > 0) ? node_idx_stack[obj_sp - 1] : (~0u);
                    aop.child_idx = last_popped_node_idx;
                    aop.argb = last_popped.argb;
                    aop.num = last_popped.opacity;
                    if (last_popped.kind == UWP_RD_STYLE)
                    {
                        aop.str_off = xbf_aot_intern_str( ctx->aot, last_popped.based_on );
                        if (last_popped.setter_cnt > 0 && last_popped.setters)
                        {
                            aop.setter_start = xbf_aot_append_setters( ctx->aot, last_popped.setters, last_popped.setter_cnt );
                            aop.setter_cnt = (UINT32)last_popped.setter_cnt;
                        }
                    }
                    else
                    {
                        aop.str_off = xbf_aot_intern_str( ctx->aot, last_popped.str_val );
                    }
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x20) /* ProvideValue and SetProperty (u16 pid) */
        {
            WORD pid = 0;
            if (end_p - p < 2) break;
            pid = *(const WORD *)(ctx->nodes + p);
            p += 2;
            if (has_last_popped && last_popped.el && obj_sp > 0 && obj_stack[obj_sp - 1] && last_popped.el->text[0])
            {
                struct uwp_element *target_el = obj_stack[obj_sp - 1];
                WCHAR resolved[256] = {0};
                if (uwp_pri_lookup_string( last_popped.el->text, resolved, ARRAY_SIZE(resolved) ) && resolved[0])
                {
                    const WCHAR *pname = (ctx && pid < ctx->prop_cnt) ? ctx->props[pid].name : L"";
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
                        if (vk)
                            uwp_accel_register( NULL, target_el, vk, mods, TRUE );
                    }
                    if (pid == XBF_PROP_CONTENT || pid == XBF_PROP_UIELEMENT_XY_FOCUS_KEYBOARD_NAV || pid == XBF_PROP_TEXTBLOCK_TEXT || (!is_vk && !target_el->text[0]))
                        lstrcpynW( target_el->text, resolved, ARRAY_SIZE(target_el->text) );
                }
                if (ctx->aot && node_idx_stack[obj_sp - 1] != (~0u) && last_popped_node_idx != (~0u))
                {
                    struct xbf_aot_op aop = {0};
                    aop.kind = XBF_AOT_OP_PROVIDE_VALUE;
                    aop.pid = pid;
                    aop.node_idx = node_idx_stack[obj_sp - 1];
                    aop.child_idx = last_popped_node_idx;
                    xbf_aot_emit_op( ctx->aot, &aop );
                }
            }
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x09)
        {
            free( last_popped.setters );
            memset( &last_popped, 0, sizeof(last_popped) );
            last_popped_node_idx = (~0u);
            has_last_popped = FALSE;
        }
        else if (op == 0x0f) /* DeferSubstream */
        {
            DWORD def_sub = 0, def_sub_sz = 0;
            BYTE def_kind;
            if (!xbf_read_uleb( ctx->nodes, &p, end_p, &def_sub ) || end_p - p < 3) break;
            if (!xbf_get_substream_bounds( ctx, def_sub, NULL, NULL, &def_sub_sz )) break;
            def_kind = ctx->nodes[p + 2];
            p += 3;

            if (def_kind == 0x09) /* DeferredElement: <name_sid:u16> <00> <01> */
            {
                if (end_p - p < 4) break;
                {
                    WORD nsid = *(const WORD *)(ctx->nodes + p);
                    const WCHAR *dname = (nsid < ctx->str_cnt && ctx->strs[nsid]) ? ctx->strs[nsid] : L"";
                    p += 4;
                    if (obj_sp > 0 && obj_stack[obj_sp - 1])
                        lstrcpynW( obj_stack[obj_sp - 1]->name, dname, ARRAY_SIZE(obj_stack[obj_sp - 1]->name) );
                    if (ctx->root_el &&
                        xaml_vec_grow( (void **)&ctx->root_el->defer_entries, &ctx->root_el->defer_cap,
                                       ctx->root_el->defer_cnt + 1, sizeof(*ctx->root_el->defer_entries), 16 ))
                    {
                        struct uwp_defer_entry *de = &ctx->root_el->defer_entries[ctx->root_el->defer_cnt++];
                        lstrcpynW( de->name, dname, ARRAY_SIZE(de->name) );
                        de->sub_idx = def_sub;
                        de->parent_panel = (obj_sp >= 2) ? obj_stack[obj_sp - 2] : ctx->root_el;
                        de->xbf_data = ctx->xbf;
                        de->xbf_size = ctx->xbf_sz;
                        de->root_component = ctx->root_comp;
                        de->realized = FALSE;
                        if (ctx->aot)
                        {
                            UINT32 ppanel_idx = (obj_sp >= 2 && node_idx_stack[obj_sp - 2] != (~0u))
                                ? node_idx_stack[obj_sp - 2] : 0u;
                            xbf_aot_add_defer( ctx->aot, dname, def_sub, ppanel_idx );
                        }
                    }
                }
            }
            else if (def_kind == 0x05) /* VisualStateGroupCollection */
            {
                if (!xbf_read_vsm_defer_header( ctx->nodes, &p, end_p, def_sub_sz, &prop_sp ))
                    break;
                if (!ctx->rd_only)
                {
                    BOOL prev_vsm = ctx->vsm_collect;
                    ctx->vsm_collect = (sub_idx == 0);
                    xbf_exec_substream_rd( ctx, (int)def_sub, ctx->root_el, NULL, NULL, active_rd );
                    ctx->vsm_collect = prev_vsm;
                }
                else if (ctx->sub_done && def_sub < ctx->sub_cnt)
                    ctx->sub_done[def_sub] = TRUE;
            }
            else if (def_kind == 0x08) /* Style Setters table */
            {
                struct xbf_stack_val *style_sv = (obj_sp > 0) ? &val_stack[obj_sp - 1] : NULL;
                if (ctx->sub_done && def_sub < ctx->sub_cnt)
                    ctx->sub_done[def_sub] = TRUE;
                if (!xbf_read_style_defer_table( ctx, &p, end_p, def_sub, def_sub_sz, style_sv, &prop_sp ))
                    break;
            }
            else if (def_kind == 0x0a) /* ResourceDictionary table */
            {
                struct xbf_rd_table tbl = {0};
                struct uwp_rd_bucket *sub_rd = NULL;
                if (!xbf_read_rd_defer_header( ctx->nodes, &p, end_p, def_sub_sz, &tbl, &prop_sp ))
                {
                    free( tbl.items );
                    break;
                }
                if (obj_sp > 0 && obj_stack[obj_sp - 1])
                    sub_rd = uwp_rd_bucket_for_el( obj_stack[obj_sp - 1], TRUE );
                else
                    sub_rd = active_rd ? active_rd : ctx->target_rd;
                {
                    BOOL prev_rd_only = ctx->rd_only;
                    ctx->rd_only = TRUE;
                    xbf_exec_substream_rd( ctx, (int)def_sub, NULL, NULL, &tbl, sub_rd );
                    ctx->rd_only = prev_rd_only;
                }
                free( tbl.items );
            }
            else
            {
                if (rd_tbl)
                {
                    DWORD next_off = 0xffffffffu;
                    int k;
                    for (k = 0; k < rd_tbl->count; k++)
                    {
                        if (rd_tbl->items[k].sub_off > cur_rel_off && rd_tbl->items[k].sub_off < next_off)
                            next_off = rd_tbl->items[k].sub_off;
                    }
                    if (next_off != 0xffffffffu && next_off < sub_sz)
                    {
                        p = sub_start + next_off;
                        while (obj_sp > base_sp) { obj_sp--; free( val_stack[obj_sp].setters ); memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) ); }
                        prop_sp = 0;
                        free( last_popped.setters );
                        memset( &last_popped, 0, sizeof(last_popped) );
                        has_last_popped = FALSE;
                        continue;
                    }
                }
                TRACE( "XBF unknown DeferSubstream kind=%#x at %lu/%lu\n", def_kind, (unsigned long)p, (unsigned long)end_p );
                break;
            }
        }
        else
        {
            if (rd_tbl)
            {
                DWORD next_off = 0xffffffffu;
                int k;
                for (k = 0; k < rd_tbl->count; k++)
                {
                    if (rd_tbl->items[k].sub_off > cur_rel_off && rd_tbl->items[k].sub_off < next_off)
                        next_off = rd_tbl->items[k].sub_off;
                }
                if (next_off != 0xffffffffu && next_off < sub_sz)
                {
                    p = sub_start + next_off;
                    while (obj_sp > base_sp) { obj_sp--; free( val_stack[obj_sp].setters ); memset( &val_stack[obj_sp], 0, sizeof(val_stack[obj_sp]) ); }
                    prop_sp = 0;
                    free( last_popped.setters );
                    memset( &last_popped, 0, sizeof(last_popped) );
                    has_last_popped = FALSE;
                    continue;
                }
            }
            TRACE( "XBF unrecognized op=%#x at %lu/%lu (sub %d), scanning forward\n",
                   op, (unsigned long)cur_rel_off, (unsigned long)end_p, sub_idx );
            while (p < end_p)
            {
                if (ctx->nodes[p] == 0x21 || ctx->nodes[p] == 0x07 || ctx->nodes[p] == 0x08 ||
                    ctx->nodes[p] == 0x14 || ctx->nodes[p] == 0x15)
                    break;
                p++;
            }
            if (p >= end_p) break;
        }
    }

    while (obj_sp > base_sp)
    {
        obj_sp--;
        free( val_stack[obj_sp].setters );
    }
    free( last_popped.setters );
}

void uwp_parse_xbf_internal( const BYTE *xbf, DWORD xbf_sz, IInspectable *component,
                                    void *connector, BOOL is_uwp_conn, void *app_outer,
                                    int only_sub_idx, struct uwp_element *attach_root,
                                    struct uwp_element **out_created,
                                    const char *base_xbf_path, struct uwp_rd_bucket *target_rd,
                                    BOOL rd_only )
{
    struct xbf_ctx ctx = {0};
    struct xbf_aot_builder aot_b = {0};
    struct uwp_element *root_el_pre;
    LARGE_INTEGER t0 = {0}, t1 = {0}, freq = {0};
    int initial_vsm_cnt = 0;
    BOOL aot_compiled_ok = FALSE;
    const BYTE *meta_base, *meta_end;
    UINT64 offs[6];
    DWORD i;

    if (!xbf || xbf_sz < 0x50 || memcmp( xbf, "XBF\0", 4 ) != 0) return;

    root_el_pre = attach_root ? attach_root : uwp_elem_from_any( component );
    if (!rd_only)
    {
        UINT64 aot_hash = xbf_aot_compute_hash( xbf, xbf_sz,
            (root_el_pre && root_el_pre->class_name[0]) ? root_el_pre->class_name : L"", only_sub_idx );
        BOOL from_disk = FALSE;
        struct xbf_aot_cache_slot *slot;

        QueryPerformanceFrequency( &freq );
        QueryPerformanceCounter( &t0 );

        slot = xbf_aot_cache_acquire( aot_hash, xbf_sz, only_sub_idx, &from_disk );
        if (slot)
        {
            const struct xbf_aot_header *tpl = slot->tpl;
            double us;
            xbf_aot_instantiate( tpl, xbf, xbf_sz, component, connector, is_uwp_conn, app_outer,
                                 root_el_pre, out_created, target_rd );
            QueryPerformanceCounter( &t1 );
            us = freq.QuadPart ? ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart) : 0.0;
            TRACE( "XBF AOT %s hit (%s, sub=%d, %lu xbf -> %lu aot bytes, %u nodes, %u ops) in %.1f us\n",
                   from_disk ? "DISK" : "MEM",
                   debugstr_w(root_el_pre ? root_el_pre->class_name : L""),
                   only_sub_idx, (unsigned long)xbf_sz, (unsigned long)tpl->total_bytes,
                   tpl->node_cnt, tpl->op_cnt, us );
            InterlockedDecrement( &slot->ref );
            return;
        }

        aot_b.hash = aot_hash;
        aot_b.xbf_sz = xbf_sz;
        aot_b.only_sub_idx = only_sub_idx;
        if (root_el_pre)
            xbf_aot_add_node( &aot_b, root_el_pre->class_name, L"", 0, XBF_AOT_NODE_ROOT_REUSE, FALSE, 0 );
        initial_vsm_cnt = root_el_pre ? root_el_pre->vsm_group_cnt : 0;
        ctx.aot = &aot_b;
    }

    ctx.xbf = xbf;
    ctx.xbf_sz = xbf_sz;
    ctx.meta_sz = *(const DWORD *)(xbf + 4);
    ctx.node_sz = *(const DWORD *)(xbf + 8);
    if (ctx.meta_sz > xbf_sz - 12 || ctx.node_sz > xbf_sz - 12 - ctx.meta_sz) return;

    meta_base = xbf + 12;
    meta_end = meta_base + ctx.meta_sz;
    memcpy( offs, xbf + 20, sizeof(offs) );
    ctx.app_outer = app_outer;
    ctx.connector = connector;
    ctx.is_uwp_conn = is_uwp_conn;
    ctx.root_comp = component;
    ctx.target_rd = target_rd;
    ctx.rd_only = rd_only;
    if (base_xbf_path && base_xbf_path[0])
    {
        const char *slash = strrchr( base_xbf_path, '/' );
        if (slash)
        {
            size_t dlen = min( (size_t)(slash - base_xbf_path + 1), sizeof(ctx.base_xbf_dir) - 1 );
            memcpy( ctx.base_xbf_dir, base_xbf_path, dlen );
            ctx.base_xbf_dir[dlen] = 0;
        }
    }
    ctx.root_el = attach_root ? attach_root : uwp_elem_from_any( component );
    if (ctx.root_el) ctx.root_el->xbf_parsed = TRUE;

    /* 1. String Table */
    {
        const BYTE *p;
        if (offs[0] > ctx.meta_sz || ctx.meta_sz - (DWORD)offs[0] < 4) return;
        p = meta_base + (size_t)offs[0];
        ctx.str_cnt = *(const DWORD *)p;
        p += 4;
        if (ctx.str_cnt > 16384 || ctx.str_cnt > (DWORD)(meta_end - p) / 4) return;
        ctx.strs = calloc( ctx.str_cnt, sizeof(WCHAR *) );
        if (!ctx.strs && ctx.str_cnt > 0) return;
        for (i = 0; i < ctx.str_cnt; i++)
        {
            DWORD len;
            if ((size_t)(meta_end - p) < 4) goto cleanup;
            len = *(const DWORD *)p;
            p += 4;
            if (len > (DWORD)(meta_end - p) / 2) goto cleanup;
            ctx.strs[i] = calloc( len + 1, sizeof(WCHAR) );
            if (ctx.strs[i]) memcpy( ctx.strs[i], p, len * 2 );
            p += len * 2;
            if ((size_t)(meta_end - p) >= 2) p += 2;
        }
    }

    /* 2. XmlNamespace Table -> Type Table -> Property Table */
    {
        const BYTE *xp = (offs[5] <= ctx.meta_sz) ? meta_base + (size_t)offs[5] : meta_end;
        DWORD xcnt = ((size_t)(meta_end - xp) >= 4) ? *(const DWORD *)xp : 0;
        const DWORD *x_sids = (const DWORD *)(xp + 4);
        const BYTE *tp = (offs[3] <= ctx.meta_sz) ? meta_base + (size_t)offs[3] : meta_end;

        if (xcnt > 0 && xcnt < 4096 && (size_t)(meta_end - xp) >= 4 && xcnt <= (DWORD)(meta_end - xp - 4) / 4)
        {
            ctx.xmlns_cnt = xcnt;
            ctx.xmlns = calloc( xcnt, sizeof(WCHAR *) );
            for (i = 0; i < xcnt && ctx.xmlns; i++)
            {
                DWORD xsid = x_sids[i];
                const WCHAR *ns = (xsid < ctx.str_cnt && ctx.strs[xsid]) ? ctx.strs[xsid] : L"";
                ctx.xmlns[i] = calloc( wcslen(ns) + 1, sizeof(WCHAR) );
                if (ctx.xmlns[i]) lstrcpyW( ctx.xmlns[i], ns );
            }
        }
        else
        {
            xcnt = 0;
        }

        if ((size_t)(meta_end - tp) >= 4)
        {
            ctx.type_cnt = *(const DWORD *)tp;
            if (ctx.type_cnt > 0 && ctx.type_cnt < 4096 && ctx.type_cnt <= (DWORD)(meta_end - tp - 4) / 12)
            {
                ctx.types = calloc( ctx.type_cnt, sizeof(*ctx.types) );
                for (i = 0; i < ctx.type_cnt && ctx.types; i++)
                {
                    DWORD xid = *(const DWORD *)(tp + 4 + i * 12 + 4);
                    DWORD sid = *(const DWORD *)(tp + 4 + i * 12 + 8);
                    const WCHAR *sname = (sid < ctx.str_cnt && ctx.strs[sid]) ? ctx.strs[sid] : L"";
                    const WCHAR *ns = (xid < ctx.xmlns_cnt && ctx.xmlns && ctx.xmlns[xid])
                        ? ctx.xmlns[xid]
                        : ((xid < xcnt && x_sids[xid] < ctx.str_cnt) ? ctx.strs[x_sids[xid]] : L"");
                    lstrcpynW( ctx.types[i].xmlns, ns ? ns : L"", ARRAY_SIZE(ctx.types[i].xmlns) );
                    lstrcpynW( ctx.types[i].short_name, sname, 64 );
                    if (ns && !wcsncmp( ns, L"using:", 6 ))
                        swprintf( ctx.types[i].full_name, 128, L"%s.%s", ns + 6, sname );
                    else
                        swprintf( ctx.types[i].full_name, 128, L"Windows.UI.Xaml.Controls.%s", sname );
                }
            }
            else
            {
                ctx.type_cnt = 0;
            }
        }

        {
            const BYTE *pp = (offs[4] <= ctx.meta_sz) ? meta_base + (size_t)offs[4] : meta_end;
            if ((size_t)(meta_end - pp) >= 4)
            {
                ctx.prop_cnt = *(const DWORD *)pp;
                if (ctx.prop_cnt > 0 && ctx.prop_cnt < 4096 && ctx.prop_cnt <= (DWORD)(meta_end - pp - 4) / 12)
                {
                    ctx.props = calloc( ctx.prop_cnt, sizeof(*ctx.props) );
                    for (i = 0; i < ctx.prop_cnt && ctx.props; i++)
                    {
                        DWORD tid = *(const DWORD *)(pp + 4 + i * 12 + 4);
                        DWORD sid = *(const DWORD *)(pp + 4 + i * 12 + 8);
                        ctx.props[i].owner_tid = (WORD)tid;
                        if (sid < ctx.str_cnt && ctx.strs[sid])
                            lstrcpynW( ctx.props[i].name, ctx.strs[sid], 64 );
                    }
                }
                else
                {
                    ctx.prop_cnt = 0;
                }
            }
        }
    }

    /* 3. Record XBF-referenced resources and using: controls from metadata tables */
    if (!rd_only && ctx.root_el && only_sub_idx < 0)
    {
        const BYTE *rx = NULL;
        DWORD rsz = 0;
        for (i = 0; i < ctx.type_cnt; i++)
        {
            if (!wcsncmp( ctx.types[i].xmlns, L"using:", 6 ) && ctx.types[i].short_name[0] &&
                uwp_pri_find_xbf( ctx.types[i].short_name, &rx, &rsz ))
            {
                uwp_record_xbf_ref( ctx.root_el, ctx.types[i].full_name, ctx.types[i].short_name, FALSE );
                xbf_aot_add_ref( ctx.aot, ctx.types[i].full_name, ctx.types[i].short_name, FALSE );
            }
        }
        for (i = 0; i < ctx.str_cnt; i++)
        {
            const WCHAR *s = ctx.strs[i];
            size_t slen = s ? wcslen( s ) : 0;
            if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
                uwp_pri_find_xbf( s, &rx, &rsz ))
            {
                uwp_record_xbf_ref( ctx.root_el, s, s, TRUE );
                xbf_aot_add_ref( ctx.aot, s, s, TRUE );
                if (wcsstr( s, L"Resource" ) || wcsstr( s, L"Theme" ) || wcsstr( s, L"Generic" ) ||
                    wcsstr( s, L"Dictionary" ) || wcsstr( s, L"Style" ))
                {
                    uwp_load_res_xbf_into_bucket( ctx.base_xbf_dir, s,
                        target_rd ? target_rd : uwp_rd_bucket_for_el( ctx.root_el, TRUE ) );
                    xbf_aot_add_res_uri( ctx.aot, s );
                }
            }
        }
    }
    else if (rd_only && target_rd)
    {
        /* Also follow any explicit ResourceDictionary.xaml / Themes XAML strings referenced in this RD */
        for (i = 0; i < ctx.str_cnt; i++)
        {
            const WCHAR *s = ctx.strs[i];
            size_t slen = s ? wcslen( s ) : 0;
            if (slen >= 5 && (!wcsicmp( s + slen - 5, L".xaml" ) || !wcsicmp( s + slen - 4, L".xbf" )) &&
                (wcsstr( s, L"Resource" ) || wcsstr( s, L"Themes/" ) || wcsstr( s, L"Generic" ) ||
                 wcsstr( s, L"Dark" ) || wcsstr( s, L"Dictionary" )))
            {
                struct uwp_rd_bucket *child = get_rd_bucket( (void *)(ULONG_PTR)(0x70000000u + (unsigned int)i + (unsigned int)g_loaded_res_xbf_cnt), TRUE );
                if (child)
                {
                    uwp_load_res_xbf_into_bucket( ctx.base_xbf_dir, s, child );
                    if (wcsstr( s, L"Dark" )) uwp_rd_link_theme( target_rd, L"Dark", child );
                    else if (!wcsstr( s, L"Light" ) && !wcsstr( s, L"HighContrast" ))
                        uwp_rd_link_merged( target_rd, child );
                }
            }
        }
    }

    /* 4. Node Substreams */
    ctx.nodes = xbf + 12 + ctx.meta_sz;
    if (ctx.node_sz >= 12)
    {
        ctx.sub_cnt = *(const DWORD *)ctx.nodes;
        if (ctx.sub_cnt > 0 && ctx.sub_cnt < 2048 && ctx.sub_cnt <= (ctx.node_sz - 4) / 8)
        {
            BOOL subs_valid = TRUE;
            ctx.sub_base = 4 + ctx.sub_cnt * 8;
            for (i = 0; i < ctx.sub_cnt; i++)
            {
                if (!xbf_get_substream_bounds( &ctx, i, NULL, NULL, NULL ))
                {
                    subs_valid = FALSE;
                    break;
                }
            }
            if (subs_valid)
            {
                ctx.sub_done = calloc( ctx.sub_cnt, sizeof(BOOL) );
                if (only_sub_idx >= 0)
                {
                    xbf_exec_substream_rd( &ctx, only_sub_idx, NULL, out_created, NULL, target_rd );
                }
                else if (rd_only)
                {
                    xbf_exec_substream_rd( &ctx, 0, NULL, NULL, NULL, target_rd );
                }
                else
                {
                    xbf_exec_substream_rd( &ctx, 0, ctx.root_el, out_created, NULL, target_rd );
                    /* Also execute substream 1..N for VisualStateGroups / Styles that hold connection IDs */
                    for (i = 1; i < ctx.sub_cnt; i++)
                    {
                        BOOL is_lazy = FALSE;
                        int d;
                        if (ctx.root_el)
                        {
                            for (d = 0; d < ctx.root_el->defer_cnt; d++)
                                if (ctx.root_el->defer_entries[d].sub_idx == (int)i) { is_lazy = TRUE; break; }
                        }
                        if (!is_lazy)
                            xbf_exec_substream_rd( &ctx, (int)i, NULL, NULL, NULL, target_rd );
                    }
                }
                free( ctx.sub_done );
                aot_compiled_ok = TRUE;
            }
        }
    }

    if (ctx.aot && aot_compiled_ok && !ctx.aot->aborted)
    {
        struct xbf_aot_header *tpl;
        xbf_aot_snapshot_vsm( ctx.aot, ctx.root_el, initial_vsm_cnt );
        tpl = xbf_aot_finalize( ctx.aot, &ctx );
        if (tpl)
        {
            double us;
            xbf_aot_save_to_disk( tpl );
            QueryPerformanceCounter( &t1 );
            us = freq.QuadPart ? ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)freq.QuadPart) : 0.0;
            TRACE( "XBF AOT COMPILE (%s, sub=%d, %lu xbf -> %lu aot bytes, %u nodes, %u ops) in %.1f us\n",
                   debugstr_w(ctx.root_el ? ctx.root_el->class_name : L""),
                   only_sub_idx, (unsigned long)xbf_sz, (unsigned long)tpl->total_bytes,
                   tpl->node_cnt, tpl->op_cnt, us );
            xbf_aot_cache_insert( tpl );
        }
    }

cleanup:
    xbf_aot_builder_free( &aot_b );
    if (ctx.strs)
    {
        for (i = 0; i < ctx.str_cnt; i++) free( ctx.strs[i] );
        free( ctx.strs );
    }
    if (ctx.xmlns)
    {
        for (i = 0; i < ctx.xmlns_cnt; i++) free( ctx.xmlns[i] );
        free( ctx.xmlns );
    }
    free( ctx.types );
    free( ctx.props );
}

void uwp_parse_xbf_into_rd( const BYTE *xbf, DWORD xbf_sz, const char *base_xbf_path,
                                   struct uwp_rd_bucket *target_rd )
{
    uwp_parse_xbf_internal( xbf, xbf_sz, NULL, NULL, FALSE, NULL, -1, NULL, NULL,
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
        uwp_fire_loaded_tree( created );
        uwp_invalidate_window();
    }
}

BOOL uwp_try_load_xbf_component( void *component, void *resourceLocator, void *app_outer )
{
    static int depth = 0;
    void *conn = NULL, *conn2 = NULL, *binding = NULL;
    void *uri_rt = NULL;
    WCHAR uri_str[MAX_PATH] = {0};
    const BYTE *xbf = NULL;
    DWORD xbf_sz = 0;

    if (!component || !resourceLocator) return FALSE;
    if (FAILED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector_UWP_g, &conn )) || !conn)
        return FALSE;

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

    if (!uri_str[0] || !uwp_pri_find_xbf( uri_str, &xbf, &xbf_sz ))
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

    TRACE( "uwp_try_load_xbf_component: %s (%lu bytes) on %p (depth %d)\n",
           debugstr_w(uri_str), (unsigned long)xbf_sz, component, depth );

    /* 1. Connect(1, component) and GetBindingConnector(1, component) first so root & {x:Bind} are wired */
    {
        struct uwp_element *comp_el = uwp_elem_from_any( component );
        HRESULT (WINAPI *conn_fn)(void *, int, void *) = (void *)(*(void ***)conn)[6];
        HRESULT chr = conn_fn( conn, 1, component );
        TRACE( "uwp_try_load_xbf_component: Connect(1,comp) -> %#lx\n", chr );
        if (SUCCEEDED(IInspectable_QueryInterface( (IInspectable *)component, &IID_IComponentConnector2_UWP_g, &conn2 )) && conn2)
        {
            HRESULT (WINAPI *get_bc)(void *, int, void *, void **) = (void *)(*(void ***)conn2)[6];
            if (get_bc && SUCCEEDED(get_bc( conn2, 1, component, &binding )) && binding)
            {
                HRESULT (WINAPI *b_conn)(void *, int, void *) = (void *)(*(void ***)binding)[6];
                if (comp_el) comp_el->binding_connector = binding;
                if (b_conn) b_conn( binding, 1, component );
            }
            IUnknown_Release( (IUnknown *)conn2 );
        }
    }

    /* 2. Build visual tree from XBF v2 stream and Connect child elements */
    uwp_parse_and_build_xbf( xbf, xbf_sz, (IInspectable *)component, conn, TRUE, app_outer, -1, NULL, NULL );

    /* 2b. Realize default deferred elements inside empty panels */
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

