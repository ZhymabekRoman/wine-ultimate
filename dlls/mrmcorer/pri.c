/*
 * PRI v2 ("mrm_pri2") reader for the Resources.Core layer.
 *
 * Implements the full read path of the "mrm_pri2" container used by UWP /
 * .NET Native (MRT) apps: 32-byte file header, section directory,
 * hierarchical schema section ([mrm_hschemaex]) with the u16-indexed
 * scope/item name tables, decision-info section ([mrm_decn_info]) with
 * qualifiers / qualifier sets / decisions, resource-map section
 * ([mrm_res_map2_]) with the item-to-iteminfo-group / iteminfo-group /
 * iteminfo / candidate u16 tables, and data-item sections
 * ([mrm_dataitem]) holding the resource value strings.
 *
 * The on-disk layout follows the community PRI format specification
 * "Package Resource Index File Format.md" (chausner/PriTools) and was
 * validated field-by-field against Microsoft.WindowsCalculator
 * resources.pri (137 sections, 661 scopes, 1323 items, 2546 candidates,
 * 23 qualifiers, 133 qualifier sets, 9 decisions).  Each struct comment
 * below cites the offsets as implemented against that fixture.
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

#define PRI_HEADER_SIZE_MIN   0x20
#define PRI_DIR_ENTRY_SIZE    32
#define PRI_SEC_HEADER_SIZE   32

/* section identifiers (16 bytes, NUL / space padded) */
#define SEC_DECN_INFO   "[mrm_decn_info]"
#define SEC_HSCHEMAEX   "[mrm_hschemaex]"
#define SEC_HSCHEMA     "[mrm_hschema]"
#define SEC_RESMAP2     "[mrm_res_map2_]"
#define SEC_RESMAP1     "[mrm_res_map__]"

/* ResourceValueType (resmap2 value type table entry u32 at +4):
 * 0 String (UTF-16), 1 Path, 2 EmbeddedData, 3 AsciiString, 4 Utf8String,
 * 5 AsciiPath, 6 Utf8Path */
#define VT_ASCII 3
#define VT_UTF8  4

/* decn QualifierType (distinct-qualifier entry u16 at +2); context-qualifier
 * names follow the MRT environment spelling (AltForm, DXLanguage). */
static const WCHAR *const qualifier_type_names[] =
{
    L"Language", L"Contrast", L"Scale", L"HomeRegion", L"TargetSize", L"LayoutDirection",
    L"Theme", L"AltForm", L"DXLanguage", L"Configuration", L"DeviceFamily", L"Custom",
};

/* ------------------------------------------------------------------ */
/* in-memory index                                                     */
/* ------------------------------------------------------------------ */

struct pri_sec
{
    const BYTE *data;     /* section payload, after the 32-byte section header */
    DWORD size;
};

struct pri_qualinfo       /* one resolved qualifier of a qualifier set */
{
    const WCHAR *type;    /* qualifier_type_names[] entry (static) */
    WCHAR *value;         /* operand value, UTF-16, copied from the value block */
    WORD priority;        /* lower = more significant for matching (0 = Language) */
};

struct pri_entry          /* hschema scope/item name entry */
{
    WORD parent;          /* file-order parent entry index, 0xffff = none */
    WORD index;           /* index property; root scope marker = 0xffff */
    BOOL scope;
    WCHAR *name;          /* single segment */
    WCHAR *fullname;      /* ancestors + name joined with '\' */
};

struct pri_cand           /* resmap2 candidate, 8 bytes on disk */
{
    BYTE type;            /* 0 = embedded data block, 1 = data item section */
    BYTE vti;             /* index into the resource value type table */
    WORD a;               /* type 1: source file (0 = this PRI) */
    WORD b;               /* type 1: data item index in the section */
    WORD c;               /* type 1: data item section (section directory index) */
};

struct pri_index
{
    BYTE *map;
    DWORD size;
    HANDLE file;
    HANDLE mapping;

    struct pri_sec *secs; /* indexed by section-directory index */
    UINT32 sec_count;

    const struct pri_sec *map_sec, *schema_sec, *decn_sec;

    struct pri_entry *entries;    /* scope+item entries, file order */
    UINT32 entry_count;
    struct pri_entry **items;     /* [item index property] -> entry */
    UINT32 item_count;
    WCHAR **tops;                 /* top-level scope full names */
    UINT32 top_count;

    struct pri_cand *cands;       /* resmap2 candidate table */
    UINT32 cand_count;
    BYTE *value_types;            /* decoded resource value type table */
    UINT32 value_type_count;

    WORD *item_decision;          /* [item index] -> decision, 0xffff = none */
    WORD *item_firstcand;         /* [item index] -> first candidate index */

    struct pri_qualinfo *quals;
    UINT32 qual_count;
    WORD *qset_first, *qset_n, qset_count;   /* qualifier set -> idx_tbl range */
    WORD *dec_first, *dec_n, dec_count;      /* decision -> idx_tbl range (qset indices) */
    WORD *idx_tbl;
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static WORD get16( const BYTE *p ) { return p[0] | (p[1] << 8); }
static DWORD get32( const BYTE *p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

static WCHAR *dup_ansi_as_w( const char *ascii, DWORD len )
{
    int wlen = MultiByteToWideChar( CP_UTF8, 0, ascii, len, NULL, 0 );
    WCHAR *w;

    if (wlen <= 0) return NULL;
    if (!(w = HeapAlloc( GetProcessHeap(), 0, (wlen + 1) * sizeof(*w) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, ascii, len, w, wlen );
    w[wlen] = 0;
    return w;
}

static WCHAR *dup_utf16( const BYTE *p, DWORD chars )
{
    WCHAR *w = HeapAlloc( GetProcessHeap(), 0, (chars + 1) * sizeof(*w) );
    if (!w) return NULL;
    memcpy( w, p, chars * sizeof(WCHAR) );
    w[chars] = 0;
    return w;
}

static const WCHAR *dup_empty(void)
{
    return dup_utf16( (const BYTE *)L"", 0 );
}

static BOOL sec_name_is( const BYTE *e, const char *base )
{
    /* identifiers are 16-byte fields padded with NUL *or* spaces
     * (the fixture spells "[mrm_hschemaex] " with a trailing space) */
    SIZE_T l = strlen( base );
    return l <= 16 && !memcmp( e, base, l );
}

/* ------------------------------------------------------------------ */
/* section directory                                                   */
/* ------------------------------------------------------------------ */

/*
 * File header (32 bytes):
 *   0  char[8]  magic "mrm_pri2"
 *   8  u16      0
 *   10 u16      1
 *   12 u32      total file size
 *   16 u32      section directory offset ("table of contents")
 *   20 u32      offset of first section
 *   24 u16      number of sections
 *   26 u16      0xffff
 *   28 u32      0
 *
 * Section directory entry (32 bytes):
 *   0  char[16] section identifier
 *   16 u16      flags
 *   18 u16      section flags
 *   20 u32      section qualifier
 *   24 u32      section offset, relative to the first-section offset
 *   28 u32      section length (payload; the 32-byte section header precedes it)
 *
 * Each section's 32-byte header at (first_section + entry.offset):
 *   0  char[16] identifier, 16 u32 qualifier, 20 u16 flags, 22 u16 sflags,
 *   24 u32 length (header *inclusive*, equals the directory length), 28 u32 0.
 * Payload starts at header + 32 and spans (length - 32) bytes; sections tile
 * the file contiguously (verified on the Calculator fixture: the final
 * dataitem section's region ends flush with the 16-byte file footer at
 * 0xFFDEDEFA + file size + magic).
 */
static BOOL parse_sections( pri_index *idx )
{
    DWORD toc_off, first_sec, i;
    UINT32 count;

    toc_off   = get32( idx->map + 16 );
    first_sec = get32( idx->map + 20 );
    count     = get16( idx->map + 24 );
    if (toc_off < PRI_HEADER_SIZE_MIN || first_sec < toc_off || count > 8192) return FALSE;
    if (first_sec + PRI_SEC_HEADER_SIZE > idx->size) return FALSE;

    if (!(idx->secs = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*idx->secs) ))) return FALSE;

    for (i = 0; i < count; i++)
    {
        const BYTE *e = idx->map + toc_off + i * PRI_DIR_ENTRY_SIZE;
        DWORD off = get32( e + 24 ), size = get32( e + 28 );

        if (off > idx->size || size < PRI_SEC_HEADER_SIZE || first_sec + off + size > idx->size)
        {
            WARN( "section %u out of bounds (off %#x size %#x)\n", i, (unsigned int)off, (unsigned int)size );
            return FALSE;
        }
        if (get32( idx->map + first_sec + off + 24 ) != size)
        {
            WARN( "section %u header length %u != directory %u\n", i,
                  (unsigned int)get32( idx->map + first_sec + off + 24 ), (unsigned int)size );
            return FALSE;
        }
        idx->secs[i].data = idx->map + first_sec + off + PRI_SEC_HEADER_SIZE;
        idx->secs[i].size = size - PRI_SEC_HEADER_SIZE;
        if (sec_name_is( e, SEC_RESMAP2 ) || sec_name_is( e, SEC_RESMAP1 )) idx->map_sec = &idx->secs[i];
        else if (sec_name_is( e, SEC_HSCHEMAEX ) || sec_name_is( e, SEC_HSCHEMA )) idx->schema_sec = &idx->secs[i];
        else if (sec_name_is( e, SEC_DECN_INFO )) idx->decn_sec = &idx->secs[i];
    }
    idx->sec_count = count;
    return idx->map_sec && idx->schema_sec && idx->decn_sec;
}

/* ------------------------------------------------------------------ */
/* hierarchical schema section                                         */
/* ------------------------------------------------------------------ */

/*
 * "[mrm_hschemaex]" payload (extended layout):
 *   0  u16  1
 *   2  u16  unique-name length (chars, NUL included)
 *   4  u16  name length (chars, NUL included)
 *   6  u16  0
 *   8  char[16] hname identifier "[def_hnamesx]  \0"
 *   24 HSCHEMA_VERSION_INFO (20 bytes): u16 major, u16 minor, u32 0,
 *      u32 checksum, u32 numScopes, u32 numItems
 *   44 wcharz unique name (uniqueLen chars), wcharz name (nameLen chars)
 *   +0 u16 0, u16 max full path length, u16 0
 *   +6 u32 numNames (= numScopes + numItems), u32 numScopes, u32 numItems
 *   +18 u32 Unicode name block length (UTF-16 code units)
 *   +22 u32 total names-region length (from the max-path field to end+pad)
 *   +26 u32 ASCII name block length (chars)        [extended only]
 *   then numNames * 12-byte entries:
 *      u16 parent (file-order entry index), u16 full path length (chars,
 *      NUL included; 0 for the root), u16 first char uppercased,
 *      u8 name length, u8 flags (bits 0-3 = name offset bits 16-19,
 *      bit 4 = scope, bit 5 = name stored in the ASCII block),
 *      u16 name offset (ASCII: bytes; Unicode: code units), u16 index property
 *   then numScopes * 8-byte scope records: u16 file index, u16 child count,
 *      u16 first child file index, u16 0
 *   then numItems * u16 file entry index (indexed by item index property)
 *   then the Unicode name block, then the ASCII name block.
 */
static BOOL parse_schema( pri_index *idx )
{
    const BYTE *d = idx->schema_sec->data;
    DWORD size = idx->schema_sec->size, ulen, nlen, cnt_all, cnt_sc, cnt_it, i, names_at;
    DWORD uni_len, ascii_len, uni_at, ascii_at;
    char ident[17];

    if (size < 64) return FALSE;
    ulen = get16( d + 2 );
    nlen = get16( d + 4 );
    memcpy( ident, d + 8, 16 );
    ident[16] = 0;
    if (ulen > size / 2 || nlen > size / 2) return FALSE;

    names_at = 44 + ulen * 2 + nlen * 2;
    if (names_at + 6 + 12 + 4 > size) return FALSE;
    cnt_all = get32( d + names_at + 6 );
    cnt_sc  = get32( d + names_at + 10 );
    cnt_it  = get32( d + names_at + 14 );
    if (cnt_all != cnt_sc + cnt_it || cnt_all > 200000 || cnt_sc > 200000) return FALSE;

    uni_len = get32( d + names_at + 18 );
    ascii_len = !memcmp( ident, "[def_hnamesx", 12 ) ? get32( d + names_at + 26 ) : 0;
    /* the two name blocks follow the entry / scope / item tables */
    uni_at   = names_at + 30 + cnt_all * 12 + cnt_sc * 8 + cnt_it * 2;
    ascii_at = uni_at + uni_len * 2;
    if (uni_at + uni_len * 2 + ascii_len > size) return FALSE;

    if (!(idx->entries = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, cnt_all * sizeof(*idx->entries) )))
        return FALSE;
    idx->entry_count = cnt_all;

    for (i = 0; i < cnt_all; i++)
    {
        struct pri_entry *e = &idx->entries[i];
        const BYTE *ent = d + names_at + 30 + i * 12;
        BYTE flags = ent[7];
        DWORD off = get16( ent + 8 ) | ((flags & 0xF) << 16);
        DWORD len = get16( ent + 2 );

        e->parent = get16( ent + 0 );
        e->index  = get16( ent + 10 );
        e->scope  = !!(flags & 0x10);
        /* the window is bounded by the *full path* length (ancestor prefix +
         * name + NUL), so it can extend past the end of the name block for
         * the final entries; the segment itself is NUL-terminated, so cap
         * the window at the payload end */
        if (!len) e->name = (WCHAR *)dup_empty();
        else if (flags & 0x20)
        {
            if (ascii_at + off >= size) return FALSE;
            if (ascii_at + off + len > size) len = size - (ascii_at + off);
            e->name = dup_ansi_as_w( (const char *)d + ascii_at + off, len );
        }
        else
        {
            if (uni_at + off * 2 >= size) return FALSE;
            if (uni_at + off * 2 + len * 2 > size) len = (size - (uni_at + off * 2)) / 2;
            e->name = dup_utf16( d + uni_at + off * 2, len );
        }
        if (!e->name) return FALSE;
    }

    if (!(idx->items = HeapAlloc( GetProcessHeap(), 0, cnt_it * sizeof(*idx->items) ))) return FALSE;

    /* full names: walk the parent chain, join with '\'; the root scope has an
     * empty name and its index property is 0xffff, terminating the walk. */
    for (i = 0; i < cnt_all; i++)
    {
        struct pri_entry *e = &idx->entries[i];
        struct pri_entry *chain[64];
        UINT32 depth = 0;
        SIZE_T total = 1;
        struct pri_entry *cur = e;
        SIZE_T used = 0;

        while (depth < 64)
        {
            struct pri_entry *pe;
            chain[depth++] = cur;
            total += wcslen( cur->name ) + 1;
            if (cur->parent == 0xffff || cur->parent >= cnt_all) break;
            pe = &idx->entries[cur->parent];
            if (pe->index == 0xffff) break;
            cur = pe;
        }
        if (!(e->fullname = HeapAlloc( GetProcessHeap(), 0, total * sizeof(WCHAR) ))) return FALSE;
        e->fullname[0] = 0;
        for (depth--; depth + 1; depth--)
        {
            const WCHAR *nm = chain[depth]->name;
            if (!nm[0]) continue;
            if (used) e->fullname[used++] = L'\\';
            wcscpy( e->fullname + used, nm );
            used += wcslen( nm );
        }
    }

    /* item table (indexed by item index property) */
    {
        const BYTE *itab = d + names_at + 30 + cnt_all * 12 + cnt_sc * 8;
        for (i = 0; i < cnt_it; i++)
        {
            WORD fileidx = get16( itab + i * 2 );
            if (fileidx >= cnt_all) return FALSE;
            idx->items[i] = &idx->entries[fileidx];
            idx->item_count = i + 1;
        }
    }

    /* top-level scopes: children of the root (root index property 0xffff) */
    for (i = 0; i < cnt_all; i++)
    {
        struct pri_entry *e = &idx->entries[i];
        struct pri_entry *pe;
        WCHAR **grown;

        if (!e->scope || !e->fullname[0]) continue;
        if (e->parent == 0xffff || e->parent >= cnt_all) continue;
        pe = &idx->entries[e->parent];
        if (pe->index != 0xffff && pe->fullname[0]) continue;
        grown = HeapReAlloc( GetProcessHeap(), 0, idx->tops, (idx->top_count + 1) * sizeof(*idx->tops) );
        if (!grown) break;
        idx->tops = grown;
        idx->tops[idx->top_count++] = e->fullname;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* decision info section                                               */
/* ------------------------------------------------------------------ */

/*
 * "[mrm_decn_info]" payload:
 *   0  u16 numDistinctQualifiers
 *   2  u16 numQualifiers
 *   4  u16 numQualifierSets
 *   6  u16 numDecisions
 *   8  u16 numIndexTableEntries
 *   10 u16 value block length (chars)
 *   then numDecisions * (u16 firstIndexTableIndex, u16 numQualifierSets)
 *   then numQualifierSets * (u16 firstIndexTableIndex, u16 numQualifiers)
 *   then numQualifiers * (u16 distinctIndex, u16 priority, u16 fallbackScore, u16 0)
 *   then numDistinct * (u16 attributeAtom, u16 qualifierType,
 *        u16 conditionOperator, u16 valueType, u32 valueOffsetChars)
 *   then the index table (u16[numIndexTableEntries]): decision ranges hold
 *   qualifier-set indices, qualifier-set ranges hold qualifier indices
 *   (verified on the fixture: qset ranges occupy indices 0..303 of 468,
 *   decision ranges 304..467),
 *   then the UTF-16 value block of NUL-terminated strings.
 */
static BOOL parse_decn( pri_index *idx )
{
    const BYTE *d = idx->decn_sec->data;
    DWORD size = idx->decn_sec->size, i;
    WORD ndist, nqual, nqset, ndec, nidx;
    DWORD qualin_at, dq_at, idx_at, val_at;

    ndist = get16( d + 0 );
    nqual = get16( d + 2 );
    nqset = get16( d + 4 );
    ndec  = get16( d + 6 );
    nidx  = get16( d + 8 );
    if (ndist > 4096 || (DWORD)nidx * 2 > size) return FALSE;

    qualin_at = 12 + ndec * 4 + nqset * 4;
    dq_at     = qualin_at + nqual * 8;
    idx_at    = dq_at + ndist * 12;
    val_at    = idx_at + nidx * 2;
    if (val_at > size) return FALSE;

    idx->dec_first = HeapAlloc( GetProcessHeap(), 0, ndec * sizeof(*idx->dec_first) );
    idx->dec_n     = HeapAlloc( GetProcessHeap(), 0, ndec * sizeof(*idx->dec_n) );
    idx->qset_first = HeapAlloc( GetProcessHeap(), 0, nqset * sizeof(*idx->qset_first) );
    idx->qset_n     = HeapAlloc( GetProcessHeap(), 0, nqset * sizeof(*idx->qset_n) );
    idx->idx_tbl = HeapAlloc( GetProcessHeap(), 0, nidx * sizeof(*idx->idx_tbl) );
    idx->quals   = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, nqual * sizeof(*idx->quals) );
    if (!idx->dec_first || !idx->dec_n || !idx->qset_first || !idx->qset_n || !idx->idx_tbl || !idx->quals)
        return FALSE;
    idx->dec_count = ndec;
    idx->qset_count = nqset;
    idx->qual_count = nqual;

    for (i = 0; i < ndec; i++)
    {
        idx->dec_first[i] = get16( d + 12 + i * 4 );
        idx->dec_n[i]     = get16( d + 12 + i * 4 + 2 );
    }
    for (i = 0; i < nqset; i++)
    {
        idx->qset_first[i] = get16( d + 12 + ndec * 4 + i * 4 );
        idx->qset_n[i]     = get16( d + 12 + ndec * 4 + i * 4 + 2 );
    }
    for (i = 0; i < nidx; i++) idx->idx_tbl[i] = get16( d + idx_at + i * 2 );

    for (i = 0; i < nqual; i++)
    {
        struct pri_qualinfo *q = &idx->quals[i];
        WORD di = get16( d + qualin_at + i * 8 );
        const BYTE *dq = d + dq_at + di * 12;
        WORD qtype = get16( dq + 2 );
        DWORD voff = get32( dq + 8 );
        DWORD at = val_at + voff * 2, end = at;

        if (di >= ndist) return FALSE;
        while (end + 1 < size && get16( d + end )) end += 2;
        q->type = qtype < ARRAYSIZE( qualifier_type_names ) ? qualifier_type_names[qtype] : L"Custom";
        q->value = dup_utf16( d + at, (end - at) / 2 );
        q->priority = get16( d + qualin_at + i * 8 + 2 );
        if (!q->value) return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* resource map section                                                */
/* ------------------------------------------------------------------ */

/*
 * "[mrm_res_map2_]" payload (v2; environment references are empty):
 *   0  u16 envRefs block length,  2 u16 envRefs count,
 *   4  u16 schema section index (section-directory index),
 *   6  u16 schema reference block length,
 *   8  u16 decn section index, 10 u16 value type table count,
 *   12 u16 item-to-iteminfo-group count, 14 u16 iteminfo-group count
 *   16 u32 iteminfo count, 20 u32 candidate count,
 *   24 u32 embedded data block length, 28 u32 table extension block length
 *   then envRefs block, schema reference block,
 *   value type table: count * (u32 4, u32 resourceValueType)
 *   item->iteminfo group: count * (u16 firstItemIndexProperty, u16 groupIndex;
 *       a groupIndex >= group count denotes a virtual size-1 group at
 *       iteminfo index groupIndex - groupCount - verified on the fixture)
 *   iteminfo group: count * (u16 groupSize, u16 firstItemInfoIndex)
 *   iteminfo: count * (u16 decisionIndex, u16 firstCandidateIndex)
 *   table extension (u32 variants of the three tables; absent in v2 app files)
 *   candidates: count * 8 bytes:
 *       type 1: u8 1, u8 valueTypeIndex, u16 sourceFile, u16 dataItemIndex,
 *               u16 dataItemSection (section-directory index)
 *       type 0: u8 0, u8 valueTypeIndex, u16 length, u32 embeddedOffset
 *   embedded data block.
 */
static BOOL parse_resmap( pri_index *idx )
{
    const BYTE *d = idx->map_sec->data;
    DWORD size = idx->map_sec->size, i, pos;
    WORD n2g, ngrp, nvtype;
    DWORD ninfo, ncand;

    if (size < 32) return FALSE;
    nvtype = get16( d + 10 );
    n2g    = get16( d + 12 );
    ngrp   = get16( d + 14 );
    ninfo  = get32( d + 16 );
    ncand  = get32( d + 20 );
    if (nvtype > 4096 || ninfo > 200000 || ncand > 400000) return FALSE;

    pos = 32 + get16( d + 0 ) + get16( d + 6 );
    if (pos + nvtype * 8 + n2g * 4 + ngrp * 4 + ninfo * 4 + get32( d + 28 ) + ncand * 8 + get32( d + 24 ) > size)
        return FALSE;

    if (nvtype && !(idx->value_types = HeapAlloc( GetProcessHeap(), 0, nvtype ))) return FALSE;
    for (i = 0; i < nvtype; i++) idx->value_types[i] = (BYTE)get32( d + pos + i * 8 + 4 );
    idx->value_type_count = nvtype;
    pos += nvtype * 8;

    if (!(idx->item_firstcand = HeapAlloc( GetProcessHeap(), 0, idx->item_count * sizeof(*idx->item_firstcand) )))
        return FALSE;
    if (!(idx->item_decision = HeapAlloc( GetProcessHeap(), 0, idx->item_count * sizeof(*idx->item_decision) )))
        return FALSE;
    for (i = 0; i < idx->item_count; i++) idx->item_decision[i] = idx->item_firstcand[i] = 0xffff;

    {
        const BYTE *i2g  = d + pos;
        const BYTE *grp  = i2g + n2g * 4;
        const BYTE *info = grp + ngrp * 4;
        for (i = 0; i < n2g; i++)
        {
            WORD first_item = get16( i2g + i * 4 );
            WORD gidx = get16( i2g + i * 4 + 2 );
            WORD gsize, gfirst;
            WORD j;
            if (gidx < ngrp) { gsize = get16( grp + gidx * 4 ); gfirst = get16( grp + gidx * 4 + 2 ); }
            else { gsize = 1; gfirst = gidx - ngrp; }
            for (j = 0; j < gsize; j++)
            {
                WORD item = first_item + j;
                WORD ii = gfirst + j;
                if (item >= idx->item_count || ii >= ninfo) continue;
                idx->item_decision[item]  = get16( info + ii * 4 );
                idx->item_firstcand[item] = get16( info + ii * 4 + 2 );
            }
        }
        pos += n2g * 4 + ngrp * 4 + ninfo * 4 + get32( d + 28 );
    }

    if (ncand && !(idx->cands = HeapAlloc( GetProcessHeap(), 0, ncand * sizeof(*idx->cands) ))) return FALSE;
    for (i = 0; i < ncand; i++)
    {
        const BYTE *c = d + pos + i * 8;
        idx->cands[i].type = c[0];
        idx->cands[i].vti  = c[1];
        idx->cands[i].a = get16( c + 2 );
        idx->cands[i].b = get16( c + 4 );
        idx->cands[i].c = get16( c + 6 );
    }
    idx->cand_count = ncand;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* load / release                                                      */
/* ------------------------------------------------------------------ */

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

pri_index *pri_load( const WCHAR *path )
{
    pri_index *idx;

    TRACE( "%s\n", debugstr_w( path ) );

    if (!(idx = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*idx) ))) return NULL;
    idx->file = INVALID_HANDLE_VALUE;

    if (!map_file( idx, path ))
    {
        HeapFree( GetProcessHeap(), 0, idx );
        return NULL;
    }
    if (memcmp( idx->map, "mrm_pri2", 8 ))
    {
        TRACE( "not an mrm_pri2 file\n" );
        pri_release( idx );
        return NULL;
    }

    if (!parse_sections( idx )) WARN( "parse_sections failed\n" );
    else if (!parse_schema( idx )) WARN( "parse_schema failed\n" );
    else if (!parse_decn( idx )) WARN( "parse_decn failed\n" );
    else if (!parse_resmap( idx )) WARN( "parse_resmap failed\n" );
    else
    {
        TRACE( "loaded: %u sections, %u items, %u tops, %u candidates\n",
               idx->sec_count, idx->item_count, idx->top_count, idx->cand_count );
        return idx;
    }
    pri_release( idx );
    return NULL;
}

void pri_release( pri_index *index )
{
    UINT32 i;

    if (!index) return;
    if (index->entries)
    {
        for (i = 0; i < index->entry_count; i++)
        {
            HeapFree( GetProcessHeap(), 0, index->entries[i].name );
            HeapFree( GetProcessHeap(), 0, index->entries[i].fullname );
        }
        HeapFree( GetProcessHeap(), 0, index->entries );
    }
    HeapFree( GetProcessHeap(), 0, index->items );
    HeapFree( GetProcessHeap(), 0, index->tops );
    HeapFree( GetProcessHeap(), 0, index->cands );
    HeapFree( GetProcessHeap(), 0, index->value_types );
    HeapFree( GetProcessHeap(), 0, index->item_decision );
    HeapFree( GetProcessHeap(), 0, index->item_firstcand );
    if (index->quals)
    {
        for (i = 0; i < index->qual_count; i++) HeapFree( GetProcessHeap(), 0, index->quals[i].value );
        HeapFree( GetProcessHeap(), 0, index->quals );
    }
    HeapFree( GetProcessHeap(), 0, index->dec_first );
    HeapFree( GetProcessHeap(), 0, index->dec_n );
    HeapFree( GetProcessHeap(), 0, index->qset_first );
    HeapFree( GetProcessHeap(), 0, index->qset_n );
    HeapFree( GetProcessHeap(), 0, index->idx_tbl );
    HeapFree( GetProcessHeap(), 0, index->secs );
    if (index->map) UnmapViewOfFile( index->map );
    if (index->mapping) CloseHandle( index->mapping );
    if (index->file != INVALID_HANDLE_VALUE) CloseHandle( index->file );
    HeapFree( GetProcessHeap(), 0, index );
}

/* ------------------------------------------------------------------ */
/* enumeration                                                         */
/* ------------------------------------------------------------------ */

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
    return index ? index->item_count : 0;
}

BOOL pri_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    if (!index || index_ >= index->item_count) return FALSE;
    lstrcpynW( buffer, index->items[index_]->fullname, len );
    return TRUE;
}

static BOOL prefix_match( const WCHAR *fullname, const WCHAR *prefix )
{
    return !prefix || !prefix[0] || !wcsnicmp( fullname, prefix, wcslen( prefix ) );
}

UINT32 pri_map_count( pri_index *index, const WCHAR *prefix )
{
    UINT32 i, count = 0;

    if (!index) return 0;
    for (i = 0; i < index->item_count; i++)
        if (prefix_match( index->items[i]->fullname, prefix )) count++;
    return count;
}

BOOL pri_map_fullname( pri_index *index, const WCHAR *prefix, UINT32 i, WCHAR *buffer, DWORD len )
{
    UINT32 seen = 0, j;

    if (!index) return FALSE;
    for (j = 0; j < index->item_count; j++)
    {
        if (!prefix_match( index->items[j]->fullname, prefix )) continue;
        if (seen++ == i)
        {
            lstrcpynW( buffer, index->items[j]->fullname, len );
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* values, qualifier matching, lookup                                  */
/* ------------------------------------------------------------------ */

/*
 * "[mrm_dataitem] " payload:
 *   0  u32 0
 *   4  u16 numStrings
 *   6  u16 numBlobs
 *   8  u32 stored data length
 *   then numStrings * (u16 offset, u16 length bytes),
 *   then numBlobs * (u32 offset, u32 length),
 *   then the stored data block.
 * String encoding follows the candidate's resource value type:
 * AsciiString/AsciiPath (3/5) => ANSI, Utf8String/Utf8Path (4/6) => UTF-8,
 * String (0) => UTF-16; values are NUL-terminated inside the item.
 */
static WCHAR *dataitem_value( pri_index *idx, WORD section, WORD item, BYTE vti )
{
    const BYTE *d;
    DWORD off = 0, ln = 0, sec_size, datastart;
    WORD nstr, nblob;

    if (section >= idx->sec_count) return NULL;
    d = idx->secs[section].data;
    sec_size = idx->secs[section].size;
    if (sec_size < 12) return NULL;

    nstr  = get16( d + 4 );
    nblob = get16( d + 6 );
    datastart = 12 + nstr * 4 + nblob * 8;
    if (item < nstr)
    {
        if (12 + (DWORD)item * 4 + 4 > sec_size) return NULL;
        off = get16( d + 12 + (DWORD)item * 4 );
        ln  = get16( d + 12 + (DWORD)item * 4 + 2 );
    }
    else
    {
        DWORD bo = 12 + (DWORD)nstr * 4 + (item - nstr) * 8;
        if (bo + 8 > sec_size) return NULL;
        off = get32( d + bo );
        ln  = get32( d + bo + 4 );
    }
    if (datastart + off + ln > sec_size) return NULL;

    {
        const BYTE *raw = d + datastart + off;
        switch (vti)
        {
        case VT_ASCII:
            {
                DWORD chars = ln;
                while (chars && !raw[chars - 1]) chars--;
                return dup_ansi_as_w( (const char *)raw, chars );
            }
        case VT_UTF8:
            {
                DWORD chars = ln;
                int wlen;
                WCHAR *w;
                while (chars && !raw[chars - 1]) chars--;
                wlen = MultiByteToWideChar( CP_UTF8, 0, (const char *)raw, chars, NULL, 0 );
                if (wlen <= 0) return NULL;
                if (!(w = HeapAlloc( GetProcessHeap(), 0, (wlen + 1) * sizeof(*w) ))) return NULL;
                MultiByteToWideChar( CP_UTF8, 0, (const char *)raw, chars, w, wlen );
                w[wlen] = 0;
                return w;
            }
        default:
            /* value types other than string/UTF-8 (e.g. file paths, type 1)
             * store NUL-terminated ASCII in mrm_pri2 app files; sniff the
             * shape so a genuine UTF-16 block still decodes. */
            if (ln >= 2 && raw[0] && !raw[1])
            {
                DWORD chars = ln / 2;
                while (chars && !get16( raw + (chars - 1) * 2 )) chars--;
                return dup_utf16( raw, chars );
            }
            {
                DWORD chars = ln;
                while (chars && !raw[chars - 1]) chars--;
                return dup_ansi_as_w( (const char *)raw, chars );
            }
        }
    }
}

/*
 * Qualifier matching: context values compared case-insensitively.
 * score = 2 exact, 1 wildcard ("*" / empty value, or context silent),
 * 0 mismatch.  Candidate qualifier vectors are ordered by ascending
 * decn priority (0 = Language) and compared lexicographically.
 */
static int qualifier_score( const pri_qual *ctx, UINT32 ctxcount, const WCHAR *type, const WCHAR *value )
{
    UINT32 i;

    if (!value[0] || !wcscmp( value, L"*" )) return 1;
    for (i = 0; i < ctxcount; i++)
        if (!lstrcmpiW( ctx[i].name, type )) return !lstrcmpiW( ctx[i].value, value ) ? 2 : 0;
    return 1;
}

static void score_qset( pri_index *idx, WORD qs, const pri_qual *ctx, UINT32 ctxcount,
                        WORD *prios, int *scores, WORD max )
{
    WORD first = idx->qset_first[qs], n = idx->qset_n[qs], i, j;

    if (n > max) n = max;
    for (i = 0; i < n; i++)
    {
        struct pri_qualinfo *q = &idx->quals[idx->idx_tbl[first + i]];
        prios[i] = q->priority;
        scores[i] = qualifier_score( ctx, ctxcount, q->type, q->value );
    }
    for (i = 1; i < n; i++)  /* insertion sort by priority asc, score desc */
    {
        WORD p = prios[i];
        int s = scores[i];
        for (j = i; j && (p < prios[j-1] || (p == prios[j-1] && s > scores[j-1])); j--)
        {
            prios[j] = prios[j-1];
            scores[j] = scores[j-1];
        }
        prios[j] = p;
        scores[j] = s;
    }
    for (; i < max; i++) prios[i] = 0xffff, scores[i] = -1;
}

#define PRI_QSET_MAX 16

static BOOL better_cand( pri_index *idx, WORD qs_a, WORD qs_b, const pri_qual *ctx, UINT32 ctxcount )
{
    WORD pa[PRI_QSET_MAX], pb[PRI_QSET_MAX];
    int sa[PRI_QSET_MAX], sb[PRI_QSET_MAX];
    WORD i;

    score_qset( idx, qs_a, ctx, ctxcount, pa, sa, PRI_QSET_MAX );
    score_qset( idx, qs_b, ctx, ctxcount, pb, sb, PRI_QSET_MAX );
    for (i = 0; i < PRI_QSET_MAX; i++)
    {
        if (pa[i] != pb[i]) /* longer (more qualified) vector wins at divergence */
            return idx->qset_n[qs_b] > idx->qset_n[qs_a];
        if (sa[i] != sb[i]) return sb[i] > sa[i];
    }
    return FALSE;
}

static WORD find_item_by_name( pri_index *idx, const WCHAR *key )
{
    UINT32 i;

    for (i = 0; i < idx->item_count; i++)
        if (!wcsicmp( idx->items[i]->fullname, key )) return (WORD)i;
    return 0xffff;
}

static BOOL resolve_item( pri_index *idx, WORD item, const pri_qual *ctx, UINT32 ctxcount,
                          WCHAR *name_buf, DWORD name_len, WCHAR **value_out,
                          pri_qual **quals_out, UINT32 *qcount_out )
{
    WORD dec, fc, best = 0xffff, best_qs = 0xffff, j, k;
    const struct pri_cand *c;
    pri_qual *qs = NULL;
    WCHAR *value = NULL;
    BYTE vti = VT_ASCII;

    *value_out = NULL;
    if (quals_out) *quals_out = NULL;
    if (qcount_out) *qcount_out = 0;
    if (item >= idx->item_count) return FALSE;
    lstrcpynW( name_buf, idx->items[item]->fullname, name_len );

    dec = idx->item_decision[item];
    fc  = idx->item_firstcand[item];
    if (dec == 0xffff || dec >= idx->dec_count || fc == 0xffff) return FALSE;

    for (j = 0; j < idx->dec_n[dec]; j++)
    {
        WORD qs_idx = idx->idx_tbl[idx->dec_first[dec] + j];
        if (qs_idx >= idx->qset_count || fc + j >= idx->cand_count) break;
        if (best == 0xffff || better_cand( idx, best_qs, qs_idx, ctx, ctxcount )) { best = j; best_qs = qs_idx; }
    }
    if (best == 0xffff) return FALSE;
    c = &idx->cands[fc + best];

    /* candidate type 1 (path) stores its value in the data item; in this
     * app-file fixture type 0 (manifest) carries the plain string values
     * (e.g. item 0 "CEngineStrings\10" -> dataitem sec 5 / 248 = "Rsh"),
     * its raw vti indexes the value-type table directly. */
    if (c->type == 1 && idx->value_type_count) vti = idx->value_types[c->vti % idx->value_type_count];
    if (c->type <= 1) value = dataitem_value( idx, c->c, c->b, vti );
    if (!value) return FALSE;

    if (quals_out && qcount_out)
    {
        WORD qs_idx = idx->idx_tbl[idx->dec_first[dec] + best];
        WORD qn = idx->qset_n[qs_idx];
        if (qn && (qs = HeapAlloc( GetProcessHeap(), 0, qn * sizeof(*qs) )))
        {
            for (k = 0; k < qn; k++)
            {
                struct pri_qualinfo *qi = &idx->quals[idx->idx_tbl[idx->qset_first[qs_idx] + k]];
                qs[k].name = (WCHAR *)qi->type;
                qs[k].value = qi->value;
            }
            *quals_out = qs;
            *qcount_out = qn;
        }
    }
    *value_out = value;
    return TRUE;
}

BOOL pri_map_get( pri_index *index, const WCHAR *prefix, UINT32 i, const pri_qual *ctx, UINT32 ctxcount,
                  WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out )
{
    UINT32 seen = 0, j;

    if (!index) return FALSE;
    for (j = 0; j < index->item_count; j++)
    {
        if (!prefix_match( index->items[j]->fullname, prefix )) continue;
        if (seen++ != i) continue;
        return resolve_item( index, j, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
    }
    return FALSE;
}

BOOL pri_lookup( pri_index *index, const WCHAR *key, const pri_qual *ctx, UINT32 ctxcount,
                 WCHAR *name_buf, DWORD name_len, WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out )
{
    WORD item;

    if (!index || !key) return FALSE;
    item = find_item_by_name( index, key );
    if (item == 0xffff) return FALSE;
    return resolve_item( index, item, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
}

void pri_free_result( WCHAR *value, pri_qual *quals )
{
    HeapFree( GetProcessHeap(), 0, value );
    HeapFree( GetProcessHeap(), 0, quals );
}

HRESULT pri_lookup_dup( pri_index *index, const WCHAR *key, const pri_qual *ctx, UINT32 ctxcount,
                        WCHAR **value_out )
{
    WCHAR namebuf[1024];
    pri_qual *quals = NULL;
    UINT32 qcount = 0;
    WCHAR *value = NULL;

    *value_out = NULL;
    if (!pri_lookup( index, key, ctx, ctxcount, namebuf, ARRAYSIZE( namebuf ),
                     &value, &quals, &qcount ))
        return E_FAIL;
    *value_out = value;
    HeapFree( GetProcessHeap(), 0, quals );
    return S_OK;
}
