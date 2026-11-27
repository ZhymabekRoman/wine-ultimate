/* WinRT Windows.UI.Xaml PRI/XBF Catalog Initialization
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
#include "private.h"
#include "xbf_known_types.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

/* ============================================================================
 * 1. PRI v2 (mrm_pri2) Container Reader
 * ============================================================================ */

const BYTE *g_pri_data = NULL;
DWORD g_pri_size = 0;
INIT_ONCE g_pri_once = INIT_ONCE_STATIC_INIT;

const BYTE *g_winui_pri_data = NULL;
DWORD g_winui_pri_size = 0;
BOOL g_winui_pri_probed = FALSE;

BOOL WINAPI init_pri_once( INIT_ONCE *once, void *param, void **ctx )
{
    WCHAR pri_path[MAX_PATH];
    WCHAR *slash;

    GetModuleFileNameW( NULL, pri_path, MAX_PATH );
    slash = wcsrchr( pri_path, '\\' );
    if (slash) *(slash + 1) = 0;
    if (wcslen( pri_path ) + ARRAY_SIZE(L"resources.pri") <= ARRAY_SIZE(pri_path))
    {
        lstrcpynW( pri_path + wcslen( pri_path ), L"resources.pri",
                   (int)(ARRAY_SIZE(pri_path) - wcslen( pri_path )) );
        pri_map_file_to( pri_path, &g_pri_data, &g_pri_size );
    }
    return TRUE;
}

BOOL WINAPI init_pri_xbf_cat_once( INIT_ONCE *once, void *param, void **ctx )
{
    DWORD pri_sz = 0;
    const BYTE *pri = pri_get_data( &pri_sz );
    WCHAR exe_dir[MAX_PATH], pattern[MAX_PATH];
    WCHAR *slash;

    uwp_pri_enum_xbfs_in_blob( pri, pri_sz, pri_xbf_collect_cb, &g_pri_cat );

    if (GetModuleFileNameW( NULL, exe_dir, ARRAY_SIZE(exe_dir) ) && (slash = wcsrchr( exe_dir, '\\' )))
    {
        WIN32_FIND_DATAW fd;
        HANDLE hfind;
        *slash = 0;
        scan_disk_xbfs_in_dir( exe_dir, "", &g_pri_cat );
        if (wcslen( exe_dir ) + 3 < ARRAY_SIZE(pattern))
        {
            swprintf( pattern, ARRAY_SIZE(pattern), L"%.256s\\*", exe_dir );
            hfind = FindFirstFileW( pattern, &fd );
            if (hfind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        wcscmp( fd.cFileName, L"." ) && wcscmp( fd.cFileName, L".." ) &&
                        wcsicmp( fd.cFileName, L"dependencies" ))
                    {
                        WCHAR sub_dir[MAX_PATH];
                        char sub_a[128] = {0};
                        if (wcslen( exe_dir ) + 1 + wcslen( fd.cFileName ) < ARRAY_SIZE(sub_dir))
                        {
                            swprintf( sub_dir, ARRAY_SIZE(sub_dir), L"%s\\%s", exe_dir, fd.cFileName );
                            WideCharToMultiByte( CP_UTF8, 0, fd.cFileName, -1, sub_a, sizeof(sub_a) - 1, NULL, NULL );
                            scan_disk_xbfs_in_dir( sub_dir, sub_a, &g_pri_cat );
                        }
                    }
                } while (FindNextFileW( hfind, &fd ));
                FindClose( hfind );
            }
        }
    }
    TRACE( "PRI XBF catalog indexed %lu .xbf entries\n", (unsigned long)g_pri_cat.cnt );
    return TRUE;
}

void ensure_winui_pri_cat( void )
{
    if (!g_winui_pri_cat_done)
    {
        DWORD sz = 0;
        const BYTE *pri = winui_pri_get_data( &sz );
        if (pri)
        {
            g_winui_pri_cat_done = TRUE;
            uwp_pri_enum_xbfs_in_blob( pri, sz, pri_xbf_collect_cb, &g_winui_pri_cat );
            TRACE( "WinUI PRI XBF catalog indexed %lu .xbf entries\n", (unsigned long)g_winui_pri_cat.cnt );
        }
    }
}

BOOL WINAPI init_xaml_meta_cache( INIT_ONCE *once, void *param, void **ctx )
{
    size_t i;
    AcquireSRWLockExclusive( &xaml_meta_cache_lock );
    for (i = 0; i < ARRAY_SIZE(xbf_known_types); i++)
    {
        if (xbf_known_types[i])
            xaml_meta_insert_locked( xbf_known_types[i], (WORD)(0x8000 | i) );
    }
    ReleaseSRWLockExclusive( &xaml_meta_cache_lock );
    return TRUE;
}
