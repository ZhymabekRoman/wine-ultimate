#ifndef __WINE_MRT_PRI_H
#define __WINE_MRT_PRI_H

#include <windows.h>

/*
 * Minimal reader for the "mrm_pri2" (PRI v2 / MRT) container.
 *
 * Implemented subset (verified against Calculator's resources.pri):
 *   - 32-byte file header: magic[8] "mrm_pri2", u32 version, u32 filesize,
 *     u32 headersize, u32 sectiontablesize, u16 count, u16 0xffff, u32 0.
 *   - section directory: 32-byte entries {char name[16]; u64 reserved;
 *     u32 offset; u32 size}, entries named "[mrm_res_map2_]", "[mrm_dataitem]",
 *     "[mrm_decn_info]", "[mrm_hschemaex]", "[mrm_pridescex]".
 *   - the res_map2 section begins with a NUL-separated ASCII table of the
 *     hierarchical resource names ('\'-separated); we index that table.
 *   - value resolution: the "mrm_data" blob after the sections holds UTF-16
 *     value strings; we search it for an exact (case-insensitive) match of
 *     the leaf name.  When no match is found the leaf name itself is
 *     returned as the candidate value (documented stub tier).
 *
 * Not implemented (format gaps, documented):
 *   - hschemaex qualifier-aware candidate selection (top/resource name index
 *     tables, qualifier atom matching against decn_info defaults).
 *   - res_map2 u16 index arrays mapping a resource name to its
 *     resourceValueIndex/dataitem entries (needed to return the *primary*
 *     qualified value and to enumerate per-qualifier candidates).
 *   - "[mrm_fileid_]" file-path resources (URI/FilePath candidates).
 */

typedef struct pri_index pri_index;

pri_index *pri_load( const WCHAR *path );
void pri_release( pri_index *index );
UINT32 pri_top_count( pri_index *index );
BOOL pri_top_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len );
BOOL pri_lookup( pri_index *index, const WCHAR *key, WCHAR *name_out, DWORD name_len, WCHAR **value_out );
UINT32 pri_name_count( pri_index *index );
BOOL pri_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len );

#endif /* __WINE_MRT_PRI_H */
