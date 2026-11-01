#ifndef __WINE_MRT_PRI_H
#define __WINE_MRT_PRI_H

#include <windows.h>

/*
 * Reader for the "mrm_pri2" (PRI v2 / MRT) container.
 *
 * Implemented (verified field-by-field against Microsoft.WindowsCalculator
 * resources.pri; format per the community "Package Resource Index File
 * Format" specification, chausner/PriTools):
 *   - 32-byte file header + section directory (32-byte entries, offsets
 *     relative to the first-section offset) + 32-byte section headers.
 *   - hierarchical schema section ([mrm_hschemaex]): scope/item name tree
 *     with the 12-byte u16-indexed name entries (parent / full-path length /
 *     flags bits 0-3 = name offset high bits, bit 4 scope, bit 5 ASCII
 *     block), the scope child tables, the item index-property table, and
 *     the Unicode + ASCII name blocks; full names are joined with '\'.
 *   - decision info section ([mrm_decn_info]): distinct qualifiers (type,
 *     value, priority), qualifier sets, decisions, u16 index table, UTF-16
 *     value block.
 *   - resource map section ([mrm_res_map2_]): u16 item-to-iteminfo-group /
 *     iteminfo-group / iteminfo tables (incl. the virtual-group convention
 *     for groupIndex >= group count), the 7-entry resource value type
 *     table and the 8-byte candidate records (data-item section / index).
 *   - data item sections ([mrm_dataitem]): string/blob offset tables;
 *     string encoding per resource value type (ANSI / UTF-8 / UTF-16).
 *   - lookup: name -> item -> (decision, first candidate) -> best candidate
 *     for a qualifier context (exact / wildcard scoring ordered by the
 *     decn priority, Language priority 0) -> value string + qualifiers.
 *
 * Not implemented (documented tier): the embedded-data candidate block
 * (candidate type 0, absent from mrm_pri2 app files), the resmap2 table
 * extension block (u32 table variants), referenced-file sections
 * (ms-resource file URIs) and the "[def_hnamesx]" default-name section.
 */

typedef struct pri_index pri_index;

/* qualifier name/value pair; names are the MRT context spellings
 * (Language, Scale, Contrast, HomeRegion, TargetSize, LayoutDirection,
 *  Theme, AltForm, DXLanguage, Configuration, DeviceFamily, Custom) */
typedef struct
{
    WCHAR *name;
    WCHAR *value;
} pri_qual;

pri_index *pri_load( const WCHAR *path );
void pri_release( pri_index *index );

/* top-level scopes of the primary resource map */
UINT32 pri_top_count( pri_index *index );
BOOL pri_top_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len );

/* all items of the primary map, ordered by item index property */
UINT32 pri_name_count( pri_index *index );
BOOL pri_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len );

/* scoped enumeration: prefix is "Scope\" ("" or NULL = whole map) */
UINT32 pri_map_count( pri_index *index, const WCHAR *prefix );
BOOL pri_map_fullname( pri_index *index, const WCHAR *prefix, UINT32 i, WCHAR *buffer, DWORD len );

/* resolve by map position / by full name.
 * ctx carries the ResourceContext qualifier values used for candidate
 * selection.  On success *value_out and *quals_out are heap allocations
 * released with pri_free_result. */
BOOL pri_map_get( pri_index *index, const WCHAR *prefix, UINT32 i, const pri_qual *ctx, UINT32 ctxcount,
                  WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out );
BOOL pri_lookup( pri_index *index, const WCHAR *key, const pri_qual *ctx, UINT32 ctxcount,
                 WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out );
void pri_free_result( WCHAR *value, pri_qual *quals );

#endif /* __WINE_MRT_PRI_H */
