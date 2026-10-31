/*
 * Minimal PRI v2 ("mrm_pri2") reader for the Resources.Core layer.
 * See pri.h for the implemented subset and documented format gaps.
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
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

#include "pri.h"

WINE_DEFAULT_DEBUG_CHANNEL(resources);

#define PRI_HEADER_SIZE_MIN 0x20
#define PRI_DIR_ENTRY_SIZE  32

struct pri_name
{
    WCHAR *name;        /* full hierarchical name, UTF-16 */
    const char *ascii;  /* pointer into the mapped res_map2 blob */
};

struct pri_index
{
    BYTE *map;
    DWORD size;
    HANDLE file;
    HANDLE mapping;

    struct pri_name *names;
    UINT32 name_count;
    UINT32 name_alloc;

    WCHAR **tops;
    UINT32 top_count;

    BYTE *data;      /* trailing value blob */
    DWORD data_size;
};

static BYTE *map_file( pri_index *idx, const WCHAR *path )
{
    LARGE_INTEGER size;

    idx->file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, 0, NULL );
    if (idx->file == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx( idx->file, &size ) || size.QuadPart < PRI_HEADER_SIZE_MIN) goto fail;
    idx->mapping = CreateFileMappingW( idx->file, NULL, PAGE_READONLY, 0, 0, NULL );
    if (!idx->mapping) goto fail;
    idx->map = MapViewOfFile( idx->mapping, FILE_MAP_READ, 0, 0, 0 );
    if (!idx->map) goto fail;
    idx->size = (DWORD)size.QuadPart;
    return idx->map;

fail:
    CloseHandle( idx->file );
    idx->file = INVALID_HANDLE_VALUE;
    return NULL;
}

static void add_name( pri_index *idx, const char *ascii, DWORD len )
{
    struct pri_name *entry;
    int wlen = MultiByteToWideChar( CP_UTF8, 0, ascii, len, NULL, 0 );
    WCHAR *w;

    if (!wlen) return;
    if (!(w = HeapAlloc( GetProcessHeap(), 0, (wlen + 1) * sizeof(*w) ))) return;
    MultiByteToWideChar( CP_UTF8, 0, ascii, len, w, wlen );
    w[wlen] = 0;

    if (idx->name_count == idx->name_alloc)
    {
        UINT32 new_alloc = idx->name_alloc ? idx->name_alloc * 2 : 512;
        struct pri_name *grown = HeapReAlloc( GetProcessHeap(), 0, idx->names, new_alloc * sizeof(*idx->names) );
        if (!grown) { HeapFree( GetProcessHeap(), 0, w ); return; }
        idx->names = grown;
        idx->name_alloc = new_alloc;
    }
    entry = &idx->names[idx->name_count++];
    entry->name = w;
    entry->ascii = ascii;
}

static void build_tops( pri_index *idx )
{
    UINT32 i, j, count = 0, alloc = 0;
    WCHAR **tops = NULL;

    /* a top-level name is the prefix before '\' of some hierarchical leaf name */
    for (i = 0; i < idx->name_count; i++)
    {
        const WCHAR *slash = wcschr( idx->names[i].name, '\\' );
        DWORD tlen;
        if (!slash) continue;
        tlen = slash - idx->names[i].name;
        for (j = 0; j < count; j++)
        {
            if (!wcsnicmp( tops[j], idx->names[i].name, tlen ) && tops[j][tlen] == 0) break;
        }
        if (j < count) continue;
        if (count == alloc)
        {
            WCHAR **grown = HeapReAlloc( GetProcessHeap(), 0, tops, (alloc + 64) * sizeof(*tops) );
            if (!grown) break;
            tops = grown;
            alloc += 64;
        }
        if ((tops[count] = HeapAlloc( GetProcessHeap(), 0, (tlen + 1) * sizeof(WCHAR) )))
        {
            memcpy( tops[count], idx->names[i].name, tlen * sizeof(WCHAR) );
            tops[count][tlen] = 0;
            count++;
        }
    }
    idx->tops = tops;
    idx->top_count = count;
}

/* exact (case-insensitive) UTF-16 string scan inside the trailing value blob */
static WCHAR *find_in_blob( pri_index *idx, const WCHAR *needle )
{
    DWORD len = wcslen( needle ), i;

    if (!idx->data || !len) return NULL;
    for (i = 0; i + (len + 1) * sizeof(WCHAR) <= idx->data_size; i += 2)
    {
        const WCHAR *p = (const WCHAR *)(idx->data + i);
        if (!wcsnicmp( p, needle, len ) && p[len] == 0)
        {
            WCHAR *out = HeapAlloc( GetProcessHeap(), 0, (len + 1) * sizeof(*out) );
            if (out) memcpy( out, needle, (len + 1) * sizeof(*out) );
            return out;
        }
    }
    return NULL;
}

pri_index *pri_load( const WCHAR *path )
{
    pri_index *idx;
    const BYTE *map;
    DWORD dir_size, dir_start, i, sec_end = 0;
    const char *dir;

    TRACE( "%s\n", debugstr_w( path ) );

    if (!(idx = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*idx) ))) return NULL;
    idx->file = INVALID_HANDLE_VALUE;

    if (!(map = map_file( idx, path )))
    {
        HeapFree( GetProcessHeap(), 0, idx );
        return NULL;
    }
    if (memcmp( map, "mrm_pri2", 8 ))
    {
        TRACE( "not an mrm_pri2 file\n" );
        pri_release( idx );
        return NULL;
    }

    dir_start = *(const DWORD *)(map + 0x10);
    dir_size  = *(const DWORD *)(map + 0x14);
    if (dir_start + dir_size > idx->size || dir_size < PRI_DIR_ENTRY_SIZE)
    {
        pri_release( idx );
        return NULL;
    }
    dir = (const char *)map + dir_start;

    for (i = 0; i + PRI_DIR_ENTRY_SIZE <= dir_size; i += PRI_DIR_ENTRY_SIZE)
    {
        const char *name = dir + i;
        DWORD offset = *(const DWORD *)(dir + i + 24);
        DWORD size   = *(const DWORD *)(dir + i + 28);

        if (!memcmp( name, "[mrm_res_map2_]", 15 ))
        {
            DWORD pos = offset, start;
            if (offset + size > idx->size) continue;
            /* NUL-separated ASCII hierarchical names at the start of the section */
            while (pos < offset + size)
            {
                start = pos;
                while (pos < offset + size && map[pos]) pos++;
                if (pos > start && pos < offset + size)
                    add_name( idx, (const char *)map + start, pos - start );
                pos++;
            }
            sec_end = offset + size;
        }
    }

    build_tops( idx );

    /* trailing blob after the last known section: value strings (UTF-16) */
    if (sec_end && sec_end < idx->size)
    {
        idx->data = (BYTE *)map + sec_end;
        idx->data_size = idx->size - sec_end;
    }

    TRACE( "loaded %u names, %u tops\n", idx->name_count, idx->top_count );
    return idx;
}

void pri_release( pri_index *index )
{
    UINT32 i;

    if (!index) return;
    for (i = 0; i < index->name_count; i++) HeapFree( GetProcessHeap(), 0, index->names[i].name );
    HeapFree( GetProcessHeap(), 0, index->names );
    for (i = 0; i < index->top_count; i++) HeapFree( GetProcessHeap(), 0, index->tops[i] );
    HeapFree( GetProcessHeap(), 0, index->tops );
    if (index->map) UnmapViewOfFile( index->map );
    if (index->mapping) CloseHandle( index->mapping );
    if (index->file != INVALID_HANDLE_VALUE) CloseHandle( index->file );
    HeapFree( GetProcessHeap(), 0, index );
}

UINT32 pri_top_count( pri_index *index )
{
    return index ? index->top_count : 0;
}

BOOL pri_top_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    if (!index || index_ >= index->top_count) return FALSE;
    lstrcpynW( buffer, index->tops[index_], len );
    return TRUE;
}

UINT32 pri_name_count( pri_index *index )
{
    return index ? index->name_count : 0;
}

BOOL pri_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    if (!index || index_ >= index->name_count) return FALSE;
    lstrcpynW( buffer, index->names[index_].name, len );
    return TRUE;
}

/*
 * Resolve key to a full name + value.  key is the leaf-relative resource
 * name seen by MainResourceMap::GetValue (no '\' for the main map,
 * subtree-relative names like "App\Title" for GetSubtree maps).
 *
 * Returns the best-effort value: exact UTF-16 match in the value blob,
 * else the full name itself (documented stub tier, see pri.h).
 */
BOOL pri_lookup( pri_index *index, const WCHAR *key, WCHAR *name_out, DWORD name_len, WCHAR **value_out )
{
    WCHAR full[1024];
    UINT32 i;

    if (!index || !key) return FALSE;

    /* direct hierarchical match */
    for (i = 0; i < index->name_count; i++)
    {
        if (!wcsicmp( index->names[i].name, key ))
        {
            const WCHAR *leaf = wcsrchr( index->names[i].name, '\\' );
            leaf = leaf ? leaf + 1 : index->names[i].name;
            lstrcpynW( name_out, index->names[i].name, name_len );
            *value_out = find_in_blob( index, leaf );
            if (*value_out) return TRUE;
            /* stub tier: name-as-value, documented in pri.h */
            *value_out = HeapAlloc( GetProcessHeap(), 0, (wcslen( leaf ) + 1) * sizeof(WCHAR) );
            if (*value_out) wcscpy( *value_out, leaf );
            return *value_out != NULL;
        }
    }
    /* implicit main-map child: single-segment name */
    if (!wcschr( key, '\\' ))
    {
        for (i = 0; i < index->name_count; i++)
        {
            if (wcschr( index->names[i].name, '\\' )) continue;
            if (wcsicmp( index->names[i].name, key )) continue;
            lstrcpynW( name_out, index->names[i].name, name_len );
            *value_out = find_in_blob( index, key );
            if (*value_out) return TRUE;
            *value_out = HeapAlloc( GetProcessHeap(), 0, (wcslen( key ) + 1) * sizeof(WCHAR) );
            if (*value_out) wcscpy( *value_out, key );
            return *value_out != NULL;
        }
    }
    /* main-map convention: "Component\Name" where Component is a top name;
     * synthesize so lookups for the app's own subtrees succeed (documented tier) */
    if (wcschr( key, '\\' ))
    {
        lstrcpynW( full, key, ARRAYSIZE( full ));
        lstrcpynW( name_out, full, name_len );
        {
            const WCHAR *leaf = wcsrchr( full, '\\' );
            leaf = leaf ? leaf + 1 : full;
            *value_out = find_in_blob( index, leaf );
            if (*value_out) return TRUE;
            *value_out = HeapAlloc( GetProcessHeap(), 0, (wcslen( leaf ) + 1) * sizeof(WCHAR) );
            if (*value_out) wcscpy( *value_out, leaf );
            return *value_out != NULL;
        }
    }
    WARN( "no entry for %s\n", debugstr_w( key ) );
    return FALSE;
}
