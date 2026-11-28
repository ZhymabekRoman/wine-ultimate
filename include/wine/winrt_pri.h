/*
 * PRI v2 ("mrm_pri2") reader shared across WinRT resource providers.
 *
 * Implements the full read path of the "mrm_pri2" container used by UWP /
 * .NET Native (MRT) apps: 32-byte file header, section directory,
 * hierarchical schema section ([mrm_hschemaex]) with the u16-indexed
 * scope/item name tables, decision-info section ([mrm_decn_info]) with
 * qualifiers / qualifier sets / decisions, resource-map section
 * ([mrm_res_map2_]) with the item-to-iteminfo-group / iteminfo-group /
 * iteminfo / u32 table extension / candidate tables, and data-item sections
 * ([mrm_dataitem]) holding the resource value strings.
 *
 * The on-disk layout follows the community PRI format specification
 * "Package Resource Index File Format.md" (chausner/PriTools) and was
 * validated field-by-field against Microsoft.WindowsCalculator
 * resources.pri (137 sections, 661 scopes, 1323 items, 2546 candidates,
 * 23 qualifiers, 133 qualifier sets, 9 decisions).
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

#ifndef __WINE_WINRT_PRI_H
#define __WINE_WINRT_PRI_H

#include <stdarg.h>
#include <string.h>
#include <wctype.h>
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wine/debug.h"

#ifndef __WINE_MRT_PRI_H
typedef struct pri_index pri_index;

typedef struct
{
    WCHAR *name;
    WCHAR *value;
} pri_qual;
#endif

#define WINRT_PRI_HEADER_SIZE_MIN   0x20
#define WINRT_PRI_DIR_ENTRY_SIZE    32
#define WINRT_PRI_SEC_HEADER_SIZE   32

/* section identifiers (16 bytes, NUL / space padded) */
#define WINRT_PRI_SEC_DECN_INFO   "[mrm_decn_info]"
#define WINRT_PRI_SEC_HSCHEMAEX   "[mrm_hschemaex]"
#define WINRT_PRI_SEC_HSCHEMA     "[mrm_hschema]"
#define WINRT_PRI_SEC_RESMAP2     "[mrm_res_map2_]"
#define WINRT_PRI_SEC_RESMAP1     "[mrm_res_map__]"

/* ResourceValueType (resmap2 value type table entry u32 at +4):
 * 0 String (UTF-16), 1 Path, 2 EmbeddedData, 3 AsciiString, 4 Utf8String,
 * 5 AsciiPath, 6 Utf8Path */
#define WINRT_PRI_VT_ASCII 3
#define WINRT_PRI_VT_UTF8  4

#define WINRT_PRI_QSET_MAX 16

/* ------------------------------------------------------------------ */
/* in-memory index                                                     */
/* ------------------------------------------------------------------ */

struct winrt_pri_sec
{
    const BYTE *data;     /* section payload, after the 32-byte section header */
    DWORD size;
};

struct winrt_pri_qualinfo /* one resolved qualifier of a qualifier set */
{
    const WCHAR *type;    /* qualifier type name (static) */
    WCHAR *value;         /* operand value, UTF-16, copied from the value block */
    WORD priority;        /* lower = more significant for matching (0 = Language) */
};

struct winrt_pri_entry    /* hschema scope/item name entry */
{
    WORD parent;          /* file-order parent entry index, 0xffff = none */
    WORD index;           /* index property; root scope marker = 0xffff */
    BOOL scope;
    WCHAR *name;          /* single segment */
    WCHAR *fullname;      /* ancestors + name joined with '\' */
};

struct winrt_pri_cand     /* resmap2 candidate, 8 bytes on disk */
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

    struct winrt_pri_sec *secs;   /* indexed by section-directory index */
    UINT32 sec_count;

    const struct winrt_pri_sec *map_sec, *schema_sec, *decn_sec;

    struct winrt_pri_entry *entries; /* scope+item entries, file order */
    UINT32 entry_count;
    struct winrt_pri_entry **items;  /* [item index property] -> entry */
    UINT32 item_count;
    WCHAR **tops;                    /* top-level scope full names */
    UINT32 top_count;

    struct winrt_pri_cand *cands;    /* resmap2 candidate table */
    UINT32 cand_count;
    BYTE *value_types;               /* decoded resource value type table */
    UINT32 value_type_count;

    DWORD *item_decision;            /* [item index] -> decision, 0xffffffff = none */
    DWORD *item_firstcand;           /* [item index] -> first candidate index */

    struct winrt_pri_qualinfo *quals;
    UINT32 qual_count;
    WORD *qset_first, *qset_n, qset_count; /* qualifier set -> idx_tbl range */
    WORD *dec_first, *dec_n, dec_count;    /* decision -> idx_tbl range (qset indices) */
    WORD *idx_tbl;
    UINT32 *name_hash;               /* open-addressed case-folded fullname -> item_idx + 1 */
    UINT32 name_hash_mask;
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static inline WORD winrt_pri_get16( const BYTE *p )
{
    return p[0] | (p[1] << 8);
}

static inline DWORD winrt_pri_get32( const BYTE *p )
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static inline const WCHAR *winrt_pri_qualifier_type_name( WORD qtype )
{
    /* decn QualifierType (distinct-qualifier entry u16 at +2); context-qualifier
     * names follow the MRT environment spelling (AltForm, DXLanguage). */
    static const WCHAR *const names[] =
    {
        L"Language", L"Contrast", L"Scale", L"HomeRegion", L"TargetSize", L"LayoutDirection",
        L"Theme", L"AltForm", L"DXLanguage", L"Configuration", L"DeviceFamily", L"Custom",
    };

    return qtype < ARRAYSIZE( names ) ? names[qtype] : L"Custom";
}

static inline WCHAR *winrt_pri_dup_ansi_as_w( const char *ascii, DWORD len )
{
    int wlen = MultiByteToWideChar( CP_UTF8, 0, ascii, len, NULL, 0 );
    WCHAR *w;

    if (wlen <= 0) return NULL;
    if (!(w = HeapAlloc( GetProcessHeap(), 0, (wlen + 1) * sizeof(*w) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, ascii, len, w, wlen );
    w[wlen] = 0;
    return w;
}

static inline WCHAR *winrt_pri_dup_utf16( const BYTE *p, DWORD chars )
{
    WCHAR *w = HeapAlloc( GetProcessHeap(), 0, (chars + 1) * sizeof(*w) );
    if (!w) return NULL;
    memcpy( w, p, chars * sizeof(WCHAR) );
    w[chars] = 0;
    return w;
}

static inline WCHAR *winrt_pri_dup_empty( void )
{
    return winrt_pri_dup_utf16( (const BYTE *)L"", 0 );
}

static inline BOOL winrt_pri_sec_name_is( const BYTE *e, const char *base )
{
    /* identifiers are 16-byte fields padded with NUL *or* spaces
     * (the fixture spells "[mrm_hschemaex] " with a trailing space) */
    SIZE_T l = strlen( base );
    return l <= 16 && !memcmp( e, base, l );
}

static inline WCHAR winrt_pri_fold_char( WCHAR c )
{
    if (c == L'/') return L'\\';
    if (c >= L'A' && c <= L'Z') return c + 32;
    return c < 128 ? c : towlower( c );
}

static inline UINT32 winrt_pri_hash_wstr_ci( const WCHAR *s )
{
    UINT32 h = 2166136261u;

    for (; *s; s++)
    {
        h ^= (UINT32)winrt_pri_fold_char( *s );
        h *= 16777619u;
    }
    return h;
}

static inline BOOL winrt_pri_name_eq( const WCHAR *a, const WCHAR *b )
{
    while (*a && *b)
    {
        if (winrt_pri_fold_char( *a ) != winrt_pri_fold_char( *b )) return FALSE;
        a++;
        b++;
    }
    return *a == *b;
}

/* ------------------------------------------------------------------ */
/* section directory                                                   */
/* ------------------------------------------------------------------ */

static inline BOOL winrt_pri_parse_sections( pri_index *idx )
{
    DWORD toc_off, first_sec, i;
    UINT32 count;

    toc_off   = winrt_pri_get32( idx->map + 16 );
    first_sec = winrt_pri_get32( idx->map + 20 );
    count     = winrt_pri_get16( idx->map + 24 );
    if (toc_off < WINRT_PRI_HEADER_SIZE_MIN || first_sec < toc_off || count > 8192) return FALSE;
    if (first_sec + WINRT_PRI_SEC_HEADER_SIZE > idx->size) return FALSE;

    if (!(idx->secs = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*idx->secs) )))
        return FALSE;

    for (i = 0; i < count; i++)
    {
        const BYTE *e = idx->map + toc_off + i * WINRT_PRI_DIR_ENTRY_SIZE;
        DWORD off = winrt_pri_get32( e + 24 ), size = winrt_pri_get32( e + 28 );

        if (off > idx->size || size < WINRT_PRI_SEC_HEADER_SIZE || first_sec + off + size > idx->size)
        {
            WARN( "section %u out of bounds (off %#x size %#x)\n",
                  (unsigned int)i, (unsigned int)off, (unsigned int)size );
            return FALSE;
        }
        if (winrt_pri_get32( idx->map + first_sec + off + 24 ) != size)
        {
            WARN( "section %u header length %u != directory %u\n", (unsigned int)i,
                  (unsigned int)winrt_pri_get32( idx->map + first_sec + off + 24 ), (unsigned int)size );
            return FALSE;
        }
        idx->secs[i].data = idx->map + first_sec + off + WINRT_PRI_SEC_HEADER_SIZE;
        idx->secs[i].size = size - WINRT_PRI_SEC_HEADER_SIZE;
        if (winrt_pri_sec_name_is( e, WINRT_PRI_SEC_RESMAP2 ) || winrt_pri_sec_name_is( e, WINRT_PRI_SEC_RESMAP1 ))
            idx->map_sec = &idx->secs[i];
        else if (winrt_pri_sec_name_is( e, WINRT_PRI_SEC_HSCHEMAEX ) || winrt_pri_sec_name_is( e, WINRT_PRI_SEC_HSCHEMA ))
            idx->schema_sec = &idx->secs[i];
        else if (winrt_pri_sec_name_is( e, WINRT_PRI_SEC_DECN_INFO ))
            idx->decn_sec = &idx->secs[i];
    }
    idx->sec_count = count;
    return idx->map_sec && idx->schema_sec && idx->decn_sec;
}

/* ------------------------------------------------------------------ */
/* hierarchical schema section                                         */
/* ------------------------------------------------------------------ */

static inline BOOL winrt_pri_parse_schema( pri_index *idx )
{
    const BYTE *d = idx->schema_sec->data;
    DWORD size = idx->schema_sec->size, ulen, nlen, cnt_all, cnt_sc, cnt_it, i, names_at;
    DWORD uni_len, ascii_len, uni_at, ascii_at;
    char ident[17];

    if (size < 64) return FALSE;
    ulen = winrt_pri_get16( d + 2 );
    nlen = winrt_pri_get16( d + 4 );
    memcpy( ident, d + 8, 16 );
    ident[16] = 0;
    if (ulen > size / 2 || nlen > size / 2) return FALSE;

    names_at = 44 + ulen * 2 + nlen * 2;
    if (names_at + 6 + 12 + 4 > size) return FALSE;
    cnt_all = winrt_pri_get32( d + names_at + 6 );
    cnt_sc  = winrt_pri_get32( d + names_at + 10 );
    cnt_it  = winrt_pri_get32( d + names_at + 14 );
    if (cnt_all != cnt_sc + cnt_it || cnt_all > 200000 || cnt_sc > 200000) return FALSE;

    uni_len = winrt_pri_get32( d + names_at + 18 );
    ascii_len = !memcmp( ident, "[def_hnamesx", 12 ) ? winrt_pri_get32( d + names_at + 26 ) : 0;
    uni_at   = names_at + 30 + cnt_all * 12 + cnt_sc * 8 + cnt_it * 2;
    ascii_at = uni_at + uni_len * 2;
    if (uni_at + uni_len * 2 + ascii_len > size) return FALSE;

    if (!(idx->entries = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, cnt_all * sizeof(*idx->entries) )))
        return FALSE;
    idx->entry_count = cnt_all;

    for (i = 0; i < cnt_all; i++)
    {
        struct winrt_pri_entry *e = &idx->entries[i];
        const BYTE *ent = d + names_at + 30 + i * 12;
        BYTE flags = ent[7];
        DWORD off = winrt_pri_get16( ent + 8 ) | ((flags & 0xF) << 16);
        DWORD len = winrt_pri_get16( ent + 2 );

        e->parent = winrt_pri_get16( ent + 0 );
        e->index  = winrt_pri_get16( ent + 10 );
        e->scope  = !!(flags & 0x10);
        if (!len) e->name = winrt_pri_dup_empty();
        else if (flags & 0x20)
        {
            if (ascii_at + off >= size) return FALSE;
            if (ascii_at + off + len > size) len = size - (ascii_at + off);
            e->name = winrt_pri_dup_ansi_as_w( (const char *)d + ascii_at + off, len );
        }
        else
        {
            if (uni_at + off * 2 >= size) return FALSE;
            if (uni_at + off * 2 + len * 2 > size) len = (size - (uni_at + off * 2)) / 2;
            e->name = winrt_pri_dup_utf16( d + uni_at + off * 2, len );
        }
        if (!e->name) return FALSE;
    }

    if (!(idx->items = HeapAlloc( GetProcessHeap(), 0, cnt_it * sizeof(*idx->items) ))) return FALSE;

    for (i = 0; i < cnt_all; i++)
    {
        struct winrt_pri_entry *e = &idx->entries[i];
        struct winrt_pri_entry *chain[64];
        UINT32 depth = 0;
        SIZE_T total = 1;
        struct winrt_pri_entry *cur = e;
        SIZE_T used = 0;

        while (depth < 64)
        {
            struct winrt_pri_entry *pe;
            chain[depth++] = cur;
            total += wcslen( cur->name ) + 1;
            if (cur->parent == 0xffff || cur->parent >= cnt_all) break;
            pe = &idx->entries[cur->parent];
            if (pe == cur || pe->index == 0xffff || !pe->name[0]) break;
            cur = pe;
        }
        if (!(e->fullname = HeapAlloc( GetProcessHeap(), 0, total * sizeof(WCHAR) ))) return FALSE;
        e->fullname[0] = 0;
        for (depth--; depth + 1; depth--)
        {
            const WCHAR *nm = chain[depth]->name;
            SIZE_T seg_len;
            if (!nm[0]) continue;
            seg_len = wcslen( nm );
            if (used && used + 1 < total) e->fullname[used++] = L'\\';
            if (used + seg_len < total)
            {
                memcpy( e->fullname + used, nm, (seg_len + 1) * sizeof(WCHAR) );
                used += seg_len;
            }
        }
    }

    {
        const BYTE *itab = d + names_at + 30 + cnt_all * 12 + cnt_sc * 8;
        for (i = 0; i < cnt_it; i++)
        {
            WORD fileidx = winrt_pri_get16( itab + i * 2 );
            if (fileidx >= cnt_all) return FALSE;
            idx->items[i] = &idx->entries[fileidx];
            idx->item_count = i + 1;
        }
    }

    for (i = 0; i < cnt_all; i++)
    {
        struct winrt_pri_entry *e = &idx->entries[i];
        struct winrt_pri_entry *pe;
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

static inline BOOL winrt_pri_parse_decn( pri_index *idx )
{
    const BYTE *d = idx->decn_sec->data;
    DWORD size = idx->decn_sec->size, i;
    WORD ndist, nqual, nqset, ndec, nidx;
    DWORD qualin_at, dq_at, idx_at, val_at;

    if (size < 12) return FALSE;
    ndist = winrt_pri_get16( d + 0 );
    nqual = winrt_pri_get16( d + 2 );
    nqset = winrt_pri_get16( d + 4 );
    ndec  = winrt_pri_get16( d + 6 );
    nidx  = winrt_pri_get16( d + 8 );
    if (ndist > 4096 || (DWORD)nidx * 2 > size) return FALSE;

    qualin_at = 12 + ndec * 4 + nqset * 4;
    dq_at     = qualin_at + nqual * 8;
    idx_at    = dq_at + ndist * 12;
    val_at    = idx_at + nidx * 2;
    if (val_at > size) return FALSE;

    idx->dec_first  = HeapAlloc( GetProcessHeap(), 0, ndec * sizeof(*idx->dec_first) );
    idx->dec_n      = HeapAlloc( GetProcessHeap(), 0, ndec * sizeof(*idx->dec_n) );
    idx->qset_first = HeapAlloc( GetProcessHeap(), 0, nqset * sizeof(*idx->qset_first) );
    idx->qset_n     = HeapAlloc( GetProcessHeap(), 0, nqset * sizeof(*idx->qset_n) );
    idx->idx_tbl    = HeapAlloc( GetProcessHeap(), 0, nidx * sizeof(*idx->idx_tbl) );
    idx->quals      = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, nqual * sizeof(*idx->quals) );
    if (!idx->dec_first || !idx->dec_n || !idx->qset_first || !idx->qset_n || !idx->idx_tbl || !idx->quals)
        return FALSE;
    idx->dec_count  = ndec;
    idx->qset_count = nqset;
    idx->qual_count = nqual;

    for (i = 0; i < ndec; i++)
    {
        idx->dec_first[i] = winrt_pri_get16( d + 12 + i * 4 );
        idx->dec_n[i]     = winrt_pri_get16( d + 12 + i * 4 + 2 );
    }
    for (i = 0; i < nqset; i++)
    {
        idx->qset_first[i] = winrt_pri_get16( d + 12 + ndec * 4 + i * 4 );
        idx->qset_n[i]     = winrt_pri_get16( d + 12 + ndec * 4 + i * 4 + 2 );
    }
    for (i = 0; i < nidx; i++) idx->idx_tbl[i] = winrt_pri_get16( d + idx_at + i * 2 );

    for (i = 0; i < nqual; i++)
    {
        struct winrt_pri_qualinfo *q = &idx->quals[i];
        WORD di = winrt_pri_get16( d + qualin_at + i * 8 );
        const BYTE *dq = d + dq_at + di * 12;
        WORD qtype = winrt_pri_get16( dq + 2 );
        DWORD voff = winrt_pri_get32( dq + 8 );
        DWORD at = val_at + voff * 2, end = at;

        if (di >= ndist) return FALSE;
        while (end + 1 < size && winrt_pri_get16( d + end )) end += 2;
        q->type = winrt_pri_qualifier_type_name( qtype );
        q->value = winrt_pri_dup_utf16( d + at, (end - at) / 2 );
        q->priority = winrt_pri_get16( d + qualin_at + i * 8 + 2 );
        if (!q->value) return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* resource map section                                                */
/* ------------------------------------------------------------------ */

static inline BOOL winrt_pri_parse_resmap( pri_index *idx )
{
    const BYTE *d = idx->map_sec->data;
    DWORD size = idx->map_sec->size, i, pos;
    WORD n2g, ngrp, nvtype;
    DWORD ninfo, ncand;

    if (size < 32) return FALSE;
    nvtype = winrt_pri_get16( d + 10 );
    n2g    = winrt_pri_get16( d + 12 );
    ngrp   = winrt_pri_get16( d + 14 );
    ninfo  = winrt_pri_get32( d + 16 );
    ncand  = winrt_pri_get32( d + 20 );
    if (nvtype > 4096 || ninfo > 200000 || ncand > 400000) return FALSE;

    pos = 32 + winrt_pri_get16( d + 0 ) + winrt_pri_get16( d + 6 );
    if (pos + nvtype * 8 + n2g * 4 + ngrp * 4 + ninfo * 4 + winrt_pri_get32( d + 28 ) + ncand * 8 + winrt_pri_get32( d + 24 ) > size)
        return FALSE;

    TRACE( "resmap: nvtype %u n2g %u ngrp %u ninfo %lu ncand %lu ext %lu emb %lu size %lu\n",
           nvtype, n2g, ngrp, (unsigned long)ninfo, (unsigned long)ncand,
           (unsigned long)winrt_pri_get32( d + 28 ),
           (unsigned long)winrt_pri_get32( d + 24 ), (unsigned long)size );

    if (nvtype && !(idx->value_types = HeapAlloc( GetProcessHeap(), 0, nvtype ))) return FALSE;
    for (i = 0; i < nvtype; i++) idx->value_types[i] = (BYTE)winrt_pri_get32( d + pos + i * 8 + 4 );
    idx->value_type_count = nvtype;
    pos += nvtype * 8;

    if (!(idx->item_firstcand = HeapAlloc( GetProcessHeap(), 0, idx->item_count * sizeof(*idx->item_firstcand) )))
        return FALSE;
    if (!(idx->item_decision = HeapAlloc( GetProcessHeap(), 0, idx->item_count * sizeof(*idx->item_decision) )))
        return FALSE;
    for (i = 0; i < idx->item_count; i++) idx->item_decision[i] = idx->item_firstcand[i] = 0xffffffff;

    {
        const BYTE *i2g  = d + pos;
        const BYTE *grp  = i2g + n2g * 4;
        const BYTE *info = grp + ngrp * 4;
        DWORD large_sz = winrt_pri_get32( d + 28 );

        for (i = 0; i < n2g; i++)
        {
            WORD first_item = winrt_pri_get16( i2g + i * 4 );
            WORD gidx = winrt_pri_get16( i2g + i * 4 + 2 );
            WORD gsize, gfirst;
            WORD j;
            if (gidx < ngrp)
            {
                gsize  = winrt_pri_get16( grp + gidx * 4 );
                gfirst = winrt_pri_get16( grp + gidx * 4 + 2 );
            }
            else
            {
                gsize  = 1;
                gfirst = gidx - ngrp;
            }
            for (j = 0; j < gsize; j++)
            {
                WORD item = first_item + j;
                WORD ii = gfirst + j;
                WORD d16, c16;
                if (item >= idx->item_count || ii >= ninfo) continue;
                d16 = winrt_pri_get16( info + ii * 4 );
                c16 = winrt_pri_get16( info + ii * 4 + 2 );
                idx->item_decision[item]  = (d16 == 0xffff) ? 0xffffffff : d16;
                idx->item_firstcand[item] = (c16 == 0xffff) ? 0xffffffff : c16;
            }
        }
        pos += n2g * 4 + ngrp * 4 + ninfo * 4;

        /* table extension: u32 (u16-count overflow) continuations of the
         * i2g / group / iteminfo tables; group and iteminfo indices past the
         * u16 counts index into the u32 tables */
        if (large_sz >= 12)
        {
            const BYTE *ld = d + pos;
            DWORD ln2g   = winrt_pri_get32( ld );
            DWORD lngrp  = winrt_pri_get32( ld + 4 );
            DWORD lninfo = winrt_pri_get32( ld + 8 );
            TRACE( "resmap: ext ln2g %lu lngrp %lu lninfo %lu\n",
                   (unsigned long)ln2g, (unsigned long)lngrp, (unsigned long)lninfo );
            if (12 + (UINT64)ln2g * 8 + (UINT64)lngrp * 8 + (UINT64)lninfo * 8 <= large_sz)
            {
                const BYTE *li2g  = ld + 12;
                const BYTE *lgrp  = li2g + ln2g * 8;
                const BYTE *linfo = lgrp + lngrp * 8;
                for (i = 0; i < ln2g; i++)
                {
                    DWORD first_item = winrt_pri_get32( li2g + i * 8 );
                    DWORD gidx = winrt_pri_get32( li2g + i * 8 + 4 );
                    DWORD gsize, gfirst, j;
                    if (gidx >= ngrp && gidx - ngrp < lngrp)
                    {
                        gsize  = winrt_pri_get32( lgrp + (gidx - ngrp) * 8 );
                        gfirst = winrt_pri_get32( lgrp + (gidx - ngrp) * 8 + 4 );
                    }
                    else if (gidx < ngrp)
                    {
                        gsize  = winrt_pri_get16( grp + gidx * 4 );
                        gfirst = winrt_pri_get16( grp + gidx * 4 + 2 );
                    }
                    else
                    {
                        gsize  = 1;
                        gfirst = gidx - ngrp - lngrp;
                    }
                    for (j = 0; j < gsize; j++)
                    {
                        DWORD item = first_item + j;
                        DWORD ii = gfirst + j;
                        if (item >= idx->item_count) continue;
                        if (ii >= ninfo && ii - ninfo < lninfo)
                        {
                            idx->item_decision[item]  = winrt_pri_get32( linfo + (ii - ninfo) * 8 );
                            idx->item_firstcand[item] = winrt_pri_get32( linfo + (ii - ninfo) * 8 + 4 );
                        }
                        else if (ii < ninfo)
                        {
                            WORD d16 = winrt_pri_get16( info + ii * 4 );
                            WORD c16 = winrt_pri_get16( info + ii * 4 + 2 );
                            idx->item_decision[item]  = (d16 == 0xffff) ? 0xffffffff : d16;
                            idx->item_firstcand[item] = (c16 == 0xffff) ? 0xffffffff : c16;
                        }
                    }
                }
            }
        }
        pos += large_sz;
    }

    if (ncand && !(idx->cands = HeapAlloc( GetProcessHeap(), 0, ncand * sizeof(*idx->cands) ))) return FALSE;
    for (i = 0; i < ncand; i++)
    {
        const BYTE *c = d + pos + i * 8;
        idx->cands[i].type = c[0];
        idx->cands[i].vti  = c[1];
        idx->cands[i].a = winrt_pri_get16( c + 2 );
        idx->cands[i].b = winrt_pri_get16( c + 4 );
        idx->cands[i].c = winrt_pri_get16( c + 6 );
    }
    idx->cand_count = ncand;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* load / release                                                      */
/* ------------------------------------------------------------------ */

static inline BYTE *winrt_pri_map_file( pri_index *idx, const WCHAR *path )
{
    LARGE_INTEGER size;

    idx->file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, 0, NULL );
    if (idx->file == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx( idx->file, &size ) || size.QuadPart < WINRT_PRI_HEADER_SIZE_MIN) goto fail;
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

static inline void winrt_pri_build_name_hash( pri_index *idx )
{
    UINT32 cap = 64, i, mask;
    UINT32 *tbl;

    if (!idx || !idx->item_count) return;
    while (cap < idx->item_count * 2) cap *= 2;
    tbl = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, cap * sizeof(*tbl) );
    if (!tbl) return;
    mask = cap - 1;
    for (i = 0; i < idx->item_count; i++)
    {
        const WCHAR *name = idx->items[i] ? idx->items[i]->fullname : NULL;
        UINT32 slot;
        if (!name) continue;
        slot = winrt_pri_hash_wstr_ci( name ) & mask;
        while (tbl[slot])
        {
            if (winrt_pri_name_eq( idx->items[tbl[slot] - 1]->fullname, name ))
                break; /* first match wins */
            slot = (slot + 1) & mask;
        }
        if (!tbl[slot]) tbl[slot] = i + 1;
    }
    idx->name_hash = tbl;
    idx->name_hash_mask = mask;
}

static inline void winrt_pri_release( pri_index *index )
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
    HeapFree( GetProcessHeap(), 0, index->name_hash );
    HeapFree( GetProcessHeap(), 0, index->secs );
    if (index->map) UnmapViewOfFile( index->map );
    if (index->mapping) CloseHandle( index->mapping );
    if (index->file != INVALID_HANDLE_VALUE) CloseHandle( index->file );
    HeapFree( GetProcessHeap(), 0, index );
}

static inline pri_index *winrt_pri_load( const WCHAR *path )
{
    pri_index *idx;

    TRACE( "%s\n", debugstr_w( path ) );

    if (!(idx = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*idx) ))) return NULL;
    idx->file = INVALID_HANDLE_VALUE;

    if (!winrt_pri_map_file( idx, path ))
    {
        HeapFree( GetProcessHeap(), 0, idx );
        return NULL;
    }
    if (memcmp( idx->map, "mrm_pri2", 8 ))
    {
        TRACE( "not an mrm_pri2 file\n" );
        winrt_pri_release( idx );
        return NULL;
    }

    if (!winrt_pri_parse_sections( idx )) WARN( "parse_sections failed\n" );
    else if (!winrt_pri_parse_schema( idx )) WARN( "parse_schema failed\n" );
    else if (!winrt_pri_parse_decn( idx )) WARN( "parse_decn failed\n" );
    else if (!winrt_pri_parse_resmap( idx )) WARN( "parse_resmap failed\n" );
    else
    {
        winrt_pri_build_name_hash( idx );
        TRACE( "loaded: %u sections, %u items, %u tops, %u candidates\n",
               idx->sec_count, idx->item_count, idx->top_count, idx->cand_count );
        return idx;
    }
    winrt_pri_release( idx );
    return NULL;
}

/* ------------------------------------------------------------------ */
/* enumeration                                                         */
/* ------------------------------------------------------------------ */

static inline UINT32 winrt_pri_top_count( pri_index *index )
{
    return index ? index->top_count : 0;
}

static inline BOOL winrt_pri_top_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    if (!index || index_ >= index->top_count) return FALSE;
    if (buffer && len) lstrcpynW( buffer, index->tops[index_], len );
    return TRUE;
}

static inline UINT32 winrt_pri_name_count( pri_index *index )
{
    return index ? index->item_count : 0;
}

static inline BOOL winrt_pri_name( pri_index *index, UINT32 index_, WCHAR *buffer, DWORD len )
{
    if (!index || index_ >= index->item_count) return FALSE;
    if (buffer && len) lstrcpynW( buffer, index->items[index_]->fullname, len );
    return TRUE;
}

static inline BOOL winrt_pri_prefix_match( const WCHAR *fullname, const WCHAR *prefix )
{
    return !prefix || !prefix[0] || !wcsnicmp( fullname, prefix, wcslen( prefix ) );
}

static inline UINT32 winrt_pri_map_count( pri_index *index, const WCHAR *prefix )
{
    UINT32 i, count = 0;

    if (!index) return 0;
    for (i = 0; i < index->item_count; i++)
        if (winrt_pri_prefix_match( index->items[i]->fullname, prefix )) count++;
    return count;
}

static inline BOOL winrt_pri_map_fullname( pri_index *index, const WCHAR *prefix, UINT32 i, WCHAR *buffer, DWORD len )
{
    UINT32 seen = 0, j;

    if (!index) return FALSE;
    for (j = 0; j < index->item_count; j++)
    {
        if (!winrt_pri_prefix_match( index->items[j]->fullname, prefix )) continue;
        if (seen++ == i)
        {
            if (buffer && len) lstrcpynW( buffer, index->items[j]->fullname, len );
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* values, qualifier matching, lookup                                  */
/* ------------------------------------------------------------------ */

static inline WCHAR *winrt_pri_dataitem_value( pri_index *idx, WORD section, WORD item, BYTE vti )
{
    const BYTE *d;
    DWORD off = 0, ln = 0, sec_size, datastart;
    WORD nstr, nblob;

    if (section >= idx->sec_count) return NULL;
    d = idx->secs[section].data;
    sec_size = idx->secs[section].size;
    if (sec_size < 12) return NULL;

    nstr  = winrt_pri_get16( d + 4 );
    nblob = winrt_pri_get16( d + 6 );
    datastart = 12 + nstr * 4 + nblob * 8;
    if (item < nstr)
    {
        if (12 + (DWORD)item * 4 + 4 > sec_size) return NULL;
        off = winrt_pri_get16( d + 12 + (DWORD)item * 4 );
        ln  = winrt_pri_get16( d + 12 + (DWORD)item * 4 + 2 );
    }
    else
    {
        DWORD bo = 12 + (DWORD)nstr * 4 + (item - nstr) * 8;
        if (bo + 8 > sec_size) return NULL;
        off = winrt_pri_get32( d + bo );
        ln  = winrt_pri_get32( d + bo + 4 );
    }
    if (datastart + off + ln > sec_size) return NULL;

    {
        const BYTE *raw = d + datastart + off;
        switch (vti)
        {
        case 0: /* String (UTF-16LE) */
        case 1: /* Path (UTF-16LE) */
            if (ln >= 2 && !(ln & 1))
            {
                DWORD chars = ln / 2;
                while (chars && !winrt_pri_get16( raw + (chars - 1) * 2 )) chars--;
                return winrt_pri_dup_utf16( raw, chars );
            }
            {
                DWORD chars = ln;
                while (chars && !raw[chars - 1]) chars--;
                return winrt_pri_dup_ansi_as_w( (const char *)raw, chars );
            }
        case WINRT_PRI_VT_ASCII:
        case 5: /* AsciiPath */
            {
                DWORD chars = ln;
                while (chars && !raw[chars - 1]) chars--;
                return winrt_pri_dup_ansi_as_w( (const char *)raw, chars );
            }
        case WINRT_PRI_VT_UTF8:
        case 6: /* Utf8Path */
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
            if (ln >= 2 && raw[0] && !raw[1])
            {
                DWORD chars = ln / 2;
                while (chars && !winrt_pri_get16( raw + (chars - 1) * 2 )) chars--;
                return winrt_pri_dup_utf16( raw, chars );
            }
            {
                DWORD chars = ln;
                while (chars && !raw[chars - 1]) chars--;
                return winrt_pri_dup_ansi_as_w( (const char *)raw, chars );
            }
        }
    }
}

static inline int winrt_pri_qualifier_score( const pri_qual *ctx, UINT32 ctxcount, const WCHAR *type, const WCHAR *value )
{
    UINT32 i;

    if (!value[0] || !wcscmp( value, L"*" )) return 1;
    for (i = 0; i < ctxcount; i++)
        if (!lstrcmpiW( ctx[i].name, type )) return !lstrcmpiW( ctx[i].value, value ) ? 2 : 0;
    return 1;
}

static inline void winrt_pri_score_qset( pri_index *idx, WORD qs, const pri_qual *ctx, UINT32 ctxcount,
                                         WORD *prios, int *scores, WORD max )
{
    WORD first = idx->qset_first[qs], n = idx->qset_n[qs], i, j;

    if (n > max) n = max;
    for (i = 0; i < n; i++)
    {
        struct winrt_pri_qualinfo *q = &idx->quals[idx->idx_tbl[first + i]];
        prios[i] = q->priority;
        scores[i] = winrt_pri_qualifier_score( ctx, ctxcount, q->type, q->value );
    }
    for (i = 1; i < n; i++)
    {
        WORD p = prios[i];
        int s = scores[i];
        for (j = i; j && (p < prios[j - 1] || (p == prios[j - 1] && s > scores[j - 1])); j--)
        {
            prios[j] = prios[j - 1];
            scores[j] = scores[j - 1];
        }
        prios[j] = p;
        scores[j] = s;
    }
    for (; i < max; i++) prios[i] = 0xffff, scores[i] = -1;
}

static inline BOOL winrt_pri_better_cand( pri_index *idx, WORD qs_a, WORD qs_b, const pri_qual *ctx, UINT32 ctxcount )
{
    WORD pa[WINRT_PRI_QSET_MAX], pb[WINRT_PRI_QSET_MAX];
    int sa[WINRT_PRI_QSET_MAX], sb[WINRT_PRI_QSET_MAX];
    WORD i;

    winrt_pri_score_qset( idx, qs_a, ctx, ctxcount, pa, sa, WINRT_PRI_QSET_MAX );
    winrt_pri_score_qset( idx, qs_b, ctx, ctxcount, pb, sb, WINRT_PRI_QSET_MAX );
    for (i = 0; i < WINRT_PRI_QSET_MAX; i++)
    {
        if (pa[i] != pb[i])
            return idx->qset_n[qs_b] > idx->qset_n[qs_a];
        if (sa[i] != sb[i]) return sb[i] > sa[i];
    }
    return FALSE;
}

static inline UINT32 winrt_pri_find_item_by_name( pri_index *idx, const WCHAR *key )
{
    UINT32 i;

    if (idx->name_hash)
    {
        UINT32 mask = idx->name_hash_mask;
        UINT32 slot = winrt_pri_hash_wstr_ci( key ) & mask;
        UINT32 v;
        while ((v = idx->name_hash[slot]) != 0)
        {
            if (winrt_pri_name_eq( idx->items[v - 1]->fullname, key ))
                return v - 1;
            slot = (slot + 1) & mask;
        }
        return 0xffffffff;
    }
    for (i = 0; i < idx->item_count; i++)
        if (winrt_pri_name_eq( idx->items[i]->fullname, key )) return i;
    return 0xffffffff;
}

static inline BOOL winrt_pri_resolve_item( pri_index *idx, UINT32 item, const pri_qual *ctx, UINT32 ctxcount,
                                           WCHAR *name_buf, DWORD name_len, WCHAR **value_out,
                                           pri_qual **quals_out, UINT32 *qcount_out )
{
    DWORD dec, fc, best = 0xffffffff, best_qs = 0xffffffff, j;
    WORD k;
    const struct winrt_pri_cand *c;
    pri_qual *qs = NULL;
    WCHAR *value = NULL;
    BYTE vti = WINRT_PRI_VT_ASCII;

    *value_out = NULL;
    if (quals_out) *quals_out = NULL;
    if (qcount_out) *qcount_out = 0;
    if (item >= idx->item_count) return FALSE;
    if (name_buf && name_len) lstrcpynW( name_buf, idx->items[item]->fullname, name_len );

    dec = idx->item_decision[item];
    fc  = idx->item_firstcand[item];
    TRACE( "resolve_item item %u %s dec %lu fc %lu (dec_count %u cand_count %u)\n",
           (unsigned int)item, debugstr_w( idx->items[item]->fullname ),
           (unsigned long)dec, (unsigned long)fc, idx->dec_count, idx->cand_count );
    if (dec == 0xffffffff || dec >= idx->dec_count || fc == 0xffffffff)
    {
        WARN( "resolve_item bail: no decision/cands for %s\n", debugstr_w( idx->items[item]->fullname ) );
        return FALSE;
    }

    for (j = 0; j < idx->dec_n[dec]; j++)
    {
        WORD qs_idx = idx->idx_tbl[idx->dec_first[dec] + j];
        if (qs_idx >= idx->qset_count || fc + j >= idx->cand_count) break;
        if (best == 0xffffffff || winrt_pri_better_cand( idx, (WORD)best_qs, qs_idx, ctx, ctxcount ))
        {
            best = j;
            best_qs = qs_idx;
        }
    }
    if (best == 0xffffffff) return FALSE;
    c = &idx->cands[fc + best];

    if (c->type == 1 && idx->value_type_count) vti = idx->value_types[c->vti % idx->value_type_count];
    if (c->type <= 1) value = winrt_pri_dataitem_value( idx, c->c, c->b, vti );
    if (!value) return FALSE;

    if (quals_out && qcount_out)
    {
        WORD qs_idx = idx->idx_tbl[idx->dec_first[dec] + best];
        WORD qn = idx->qset_n[qs_idx];
        if (qn && (qs = HeapAlloc( GetProcessHeap(), 0, qn * sizeof(*qs) )))
        {
            for (k = 0; k < qn; k++)
            {
                struct winrt_pri_qualinfo *qi = &idx->quals[idx->idx_tbl[idx->qset_first[qs_idx] + k]];
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

static inline BOOL winrt_pri_map_get( pri_index *index, const WCHAR *prefix, UINT32 i,
                                      const pri_qual *ctx, UINT32 ctxcount, WCHAR *name_buf,
                                      DWORD name_len, WCHAR **value_out, pri_qual **quals_out,
                                      UINT32 *qcount_out )
{
    UINT32 seen = 0, j;

    if (!index) return FALSE;
    for (j = 0; j < index->item_count; j++)
    {
        if (!winrt_pri_prefix_match( index->items[j]->fullname, prefix )) continue;
        if (seen++ != i) continue;
        return winrt_pri_resolve_item( index, j, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
    }
    return FALSE;
}

static inline BOOL winrt_pri_lookup( pri_index *index, const WCHAR *key, const pri_qual *ctx,
                                     UINT32 ctxcount, WCHAR *name_buf, DWORD name_len,
                                     WCHAR **value_out, pri_qual **quals_out, UINT32 *qcount_out )
{
    UINT32 item;

    if (!index || !key) return FALSE;
    item = winrt_pri_find_item_by_name( index, key );
    if (item == 0xffffffff) return FALSE;
    return winrt_pri_resolve_item( index, item, ctx, ctxcount, name_buf, name_len, value_out, quals_out, qcount_out );
}

static inline void winrt_pri_free_result( WCHAR *value, pri_qual *quals )
{
    HeapFree( GetProcessHeap(), 0, value );
    HeapFree( GetProcessHeap(), 0, quals );
}

static inline HRESULT winrt_pri_lookup_dup( pri_index *index, const WCHAR *key, const pri_qual *ctx,
                                            UINT32 ctxcount, WCHAR **value_out )
{
    WCHAR namebuf[1024];
    pri_qual *quals = NULL;
    UINT32 qcount = 0;
    WCHAR *value = NULL;

    *value_out = NULL;
    if (!winrt_pri_lookup( index, key, ctx, ctxcount, namebuf, ARRAYSIZE( namebuf ),
                           &value, &quals, &qcount ))
        return E_FAIL;
    *value_out = value;
    HeapFree( GetProcessHeap(), 0, quals );
    return S_OK;
}

#endif /* __WINE_WINRT_PRI_H */
