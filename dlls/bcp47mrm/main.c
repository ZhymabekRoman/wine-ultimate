/*
 * BCP-47 language tag resource management functions
 *
 * Copyright 2026 Wine Project contributors
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1300, USA
 *
 * The Windows bcp47mrm.dll exports 17 functions (ordinals 1-17, in
 * alphabetical order).  Two classes of functionality exist:
 *
 *   - BCP-47 tag syntax validation (IsWellFormedTag, IsValidTag,
 *     Bcp47IsValid), implemented below.
 *   - MRM (Modern Resource Management) specific helpers that consult
 *     the MRM language tables stored in the system (distance tables,
 *     application/user language lists, region composites, layout
 *     direction).  Those are stubbed.
 */

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(bcp47mrm);

/* Maximum tag length accepted by the Windows implementation (0x55). */
#define MAX_TAG_LEN 0x55

static BOOL is_alpha( WCHAR c )
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static BOOL is_digit( WCHAR c )
{
    return c >= '0' && c <= '9';
}

static BOOL is_alnum( WCHAR c )
{
    return is_alpha( c ) || is_digit( c );
}

static WCHAR tolowerW( WCHAR c )
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

/* Length of the subtag starting at s, terminated by '-' or the string end. */
static unsigned int subtag_len( const WCHAR *s )
{
    unsigned int len = 0;

    while (s[len] && s[len] != L'-') len++;
    return len;
}

static BOOL subtag_is_alpha( const WCHAR *s, unsigned int len )
{
    unsigned int i;

    for (i = 0; i < len; i++)
        if (!is_alpha( s[i] )) return FALSE;
    return TRUE;
}

static BOOL subtag_is_digit( const WCHAR *s, unsigned int len )
{
    unsigned int i;

    for (i = 0; i < len; i++)
        if (!is_digit( s[i] )) return FALSE;
    return TRUE;
}

static BOOL subtag_is_alnum( const WCHAR *s, unsigned int len )
{
    unsigned int i;

    for (i = 0; i < len; i++)
        if (!is_alnum( s[i] )) return FALSE;
    return TRUE;
}

/* Consume the subtags following an "x" subtag: one or more 1-8 character alphanumeric subtags. */
static BOOL consume_private_sequence( const WCHAR **pp )
{
    unsigned int count = 0;
    const WCHAR *p = *pp;

    while (*p == L'-')
    {
        unsigned int sublen;

        p++;
        sublen = subtag_len( p );
        if (!sublen || sublen > 8 || !subtag_is_alnum( p, sublen )) return FALSE;
        count++;
        p += sublen;
    }
    if (!count) return FALSE;
    *pp = p;
    return TRUE;
}

/***********************************************************************
 *              bcp47_syntax_check
 *
 * Checks a tag against the BCP-47 (RFC 5646) grammar:
 *
 *   langtag = language ["-" script] ["-" region] *("-" variant)
 *             *["-" extension] ["-" privateuse]
 *   language = primary-language *3("-" extlang)
 *   primary-language = 2*3ALPHA
 *   extlang          = 3ALPHA
 *   script           = 4ALPHA
 *   region           = 2ALPHA / 3DIGIT
 *   variant          = 5*8ALPHA-DIGIT
 *   extension        = singleton 1*("-" 2*8ALPHA-DIGIT)
 *   privateuse       = "x" 1*("-" 1*8ALPHA-DIGIT)
 *   singleton        = ALPHA / DIGIT  ; other than "x" / "X"
 *
 * The Windows implementation is more lenient than the RFC:
 *   - a "x"-prefixed private use sequence may stand alone and may be
 *     followed by extension sequences;
 *   - 4-digit region subtags (e.g. the former Kosovo code "1234") and
 *     4-character mixed alphanumeric subtags are accepted;
 *   - all-digit variant subtags of 5-8 characters are accepted.
 */
static BOOL bcp47_syntax_check( const WCHAR *tag )
{
    unsigned int len, total = 0;
    const WCHAR *p;
    unsigned int extlang_count = 0;
    BOOL seen_script = FALSE, seen_region = FALSE, seen_variant = FALSE;
    BOOL seen_private = FALSE;

    if (!tag) return FALSE;
    while (tag[total]) total++;
    if (!total || total > MAX_TAG_LEN) return FALSE;

    p = tag;

    /* primary language subtag: 2-3 ASCII letters; a leading "x" subtag
     * introduces a standalone private use sequence */
    len = subtag_len( p );
    if (len == 1 && (tolowerW( *p ) == L'x'))
    {
        p++;
        seen_private = TRUE;
        if (!consume_private_sequence( &p )) return FALSE;
    }
    else
    {
        if (len < 2 || len > 3 || !subtag_is_alpha( p, len )) return FALSE;
        p += len;
    }

    while (*p == L'-')
    {
        p++;
        len = subtag_len( p );
        if (!len || len > 8 || !subtag_is_alnum( p, len )) return FALSE;

        if (len == 1) /* singleton: extension sequence or private use */
        {
            WCHAR c = tolowerW( *p );
            unsigned int count = 0;

            p++;
            if (*p != L'-') return FALSE;  /* a singleton must be followed by a sequence */

            if (c == L'x')
            {
                /* Private use: "x" followed by one or more 1-8 char subtags. */
                seen_private = TRUE;
                if (!consume_private_sequence( &p )) return FALSE;
                continue;
            }

            /* Extension: singleton followed by one or more 2-8 char subtags.
             * The sequence ends at the next singleton subtag. */
            while (*p == L'-')
            {
                unsigned int sublen = subtag_len( p + 1 );

                if (sublen == 1 && count) break;  /* next singleton: resume outer parsing */
                if (sublen < 2 || sublen > 8 || !subtag_is_alnum( p + 1, sublen )) return FALSE;
                count++;
                p += 1 + sublen;
            }
            if (!count) return FALSE;
            continue;
        }

        if (seen_private) return FALSE;  /* only singletons may follow a private use sequence */

        if (len == 4)
        {
            if (subtag_is_alpha( p, len ))
            {
                /* script: 4 letters, before region and variants */
                if (seen_script || seen_region || seen_variant) return FALSE;
                seen_script = TRUE;
            }
            else if (subtag_is_digit( p, len ))
            {
                /* 4-digit region code, before variants */
                if (seen_region || seen_variant) return FALSE;
                seen_region = TRUE;
            }
            else
            {
                /* 4-character mixed alphanumeric subtag: accepted as a variant */
                seen_variant = TRUE;
            }
            p += len;
            continue;
        }

        if ((len == 2 && subtag_is_alpha( p, len )) || (len == 3 && subtag_is_digit( p, len )))
        {
            /* region: 2 letters or 3 digits, before variants */
            if (seen_region || seen_variant) return FALSE;
            seen_region = TRUE;
            p += len;
            continue;
        }

        if (len == 3 && subtag_is_alpha( p, len ) && !seen_script && !seen_region && !seen_variant)
        {
            /* extended language subtag: 3 letters, at most 3, before script/region */
            if (extlang_count >= 3) return FALSE;
            extlang_count++;
            p += len;
            continue;
        }

        if (len >= 5 && len <= 8)
        {
            /* variant: 5-8 alphanumerics */
            seen_variant = TRUE;
            p += len;
            continue;
        }

        return FALSE;
    }

    return !*p;
}

/***********************************************************************
 *           IsWellFormedTag    (bcp47mrm.@)
 *
 * Checks if the given tag is a well-formed BCP-47 language tag
 * (syntax only; the tag need not be registered).
 *
 * RETURNS
 *   TRUE if the tag is well formed, FALSE otherwise.
 */
BOOL WINAPI IsWellFormedTag( const WCHAR *tag )
{
    TRACE( "tag %s\n", debugstr_w( tag ) );

    return bcp47_syntax_check( tag );
}

/***********************************************************************
 *           IsValidTag    (bcp47mrm.@)
 *
 * Checks if the given tag is a valid BCP-47 language tag.  The
 * Windows implementation additionally requires every subtag to be
 * registered in its language tables; here the syntax check is used.
 */
BOOL WINAPI IsValidTag( const WCHAR *tag )
{
    TRACE( "tag %s\n", debugstr_w( tag ) );

    return bcp47_syntax_check( tag );
}

/***********************************************************************
 *           Bcp47IsValid    (bcp47mrm.@)
 */
HRESULT WINAPI Bcp47IsValid( const WCHAR *tag, BOOL *valid )
{
    TRACE( "tag %s, valid %p\n", debugstr_w( tag ), valid );

    if (!valid) return E_POINTER;
    *valid = bcp47_syntax_check( tag );
    return S_OK;
}

/***********************************************************************
 *           Bcp47GetDistanceCString    (bcp47mrm.@)
 */
HRESULT WINAPI Bcp47GetDistanceCString( const WCHAR *tag, const WCHAR *tag2, INT *distance )
{
    FIXME( "tag %s, tag2 %s, distance %p stub!\n", debugstr_w( tag ), debugstr_w( tag2 ), distance );

    if (!tag || !tag2 || !distance) return E_INVALIDARG;
    *distance = 0;
    return E_NOTIMPL;
}

/***********************************************************************
 *           Bcp47GetNeutralFormCString    (bcp47mrm.@)
 */
HRESULT WINAPI Bcp47GetNeutralFormCString( const WCHAR *tag, WCHAR *buffer )
{
    FIXME( "tag %s, buffer %p stub!\n", debugstr_w( tag ), buffer );

    if (!tag || !buffer) return E_INVALIDARG;
    return E_NOTIMPL;
}

/***********************************************************************
 *           CompareBcp47Tags    (bcp47mrm.@)
 */
int WINAPI CompareBcp47Tags( const WCHAR *tag1, const WCHAR *tag2 )
{
    FIXME( "tag1 %s, tag2 %s stub!\n", debugstr_w( tag1 ), debugstr_w( tag2 ) );

    if (!tag1) return -1;
    if (!tag2) return 1;
    return lstrcmpiW( tag1, tag2 );
}

/***********************************************************************
 *           FormatLanguageList    (bcp47mrm.@)
 */
HRESULT WINAPI FormatLanguageList( const WCHAR *list, WCHAR delimiter, DWORD flags,
                                   WCHAR *buffer, size_t *chars_written )
{
    FIXME( "list %s, delimiter %lc, flags %08x, buffer %p, chars_written %p stub!\n",
           debugstr_w( list ), delimiter, (unsigned int)flags, buffer, chars_written );

    if (!list || !buffer || !chars_written) return E_INVALIDARG;
    return E_NOTIMPL;
}

/***********************************************************************
 *           FormatLanguageTag    (bcp47mrm.@)
 */
HRESULT WINAPI FormatLanguageTag( const WCHAR *tag, DWORD flags, WCHAR *buffer, WCHAR *out )
{
    FIXME( "tag %s, flags %08x, buffer %p, out %p stub!\n",
           debugstr_w( tag ), (unsigned int)flags, buffer, out );

    if (!tag || !buffer || !out) return E_INVALIDARG;
    return E_NOTIMPL;
}

/***********************************************************************
 *           GetApplicationLanguagesWithUserLanguagesFallback    (bcp47mrm.@)
 */
HRESULT WINAPI GetApplicationLanguagesWithUserLanguagesFallback( const WCHAR *languages,
                                                                 const WCHAR *user_languages,
                                                                 WCHAR **out )
{
    FIXME( "languages %s, user_languages %s, out %p stub!\n",
           debugstr_w( languages ), debugstr_w( user_languages ), out );

    if (!user_languages) return E_POINTER;
    return E_NOTIMPL;
}

/***********************************************************************
 *           GetApplicationLayoutDirection    (bcp47mrm.@)
 */
HRESULT WINAPI GetApplicationLayoutDirection( const WCHAR *lang, INT *direction )
{
    FIXME( "lang %s, direction %p stub!\n", debugstr_w( lang ), direction );

    if (!direction) return E_POINTER;
    *direction = -1;
    return E_NOTIMPL;
}

/***********************************************************************
 *           GetClosenessOfUnIsoRegionTags    (bcp47mrm.@)
 */
HRESULT WINAPI GetClosenessOfUnIsoRegionTags( const WCHAR *tag1, const WCHAR *tag2, double *closeness )
{
    FIXME( "tag1 %s, tag2 %s, closeness %p stub!\n", debugstr_w( tag1 ), debugstr_w( tag2 ), closeness );

    if (!tag1 || !tag2 || !closeness) return E_POINTER;
    *closeness = 0;
    return E_NOTIMPL;
}

/***********************************************************************
 *           GetCompositeRegionCode    (bcp47mrm.@)
 */
WORD WINAPI GetCompositeRegionCode( const WCHAR *region )
{
    FIXME( "region %s stub!\n", debugstr_w( region ) );

    return 0;
}

/***********************************************************************
 *           GetDistanceOfClosestLanguageInList    (bcp47mrm.@)
 *
 * Determines the distance between a candidate BCP-47 language tag
 * and the closest tag in a delimited list of tags.
 */
HRESULT WINAPI GetDistanceOfClosestLanguageInList( const WCHAR *language, const WCHAR *languages_list,
                                                   WCHAR delimiter, double *distance )
{
    FIXME( "language %s, languages_list %s, delimiter %lc, distance %p stub!\n",
           debugstr_w( language ), debugstr_w( languages_list ), delimiter, distance );

    if (!language || !languages_list || !distance) return E_POINTER;
    *distance = 0;
    return HRESULT_FROM_WIN32( ERROR_NO_MATCH );
}

/***********************************************************************
 *           GetLanguageDirectionality    (bcp47mrm.@)
 */
HRESULT WINAPI GetLanguageDirectionality( const WCHAR *lang, INT *direction )
{
    FIXME( "lang %s, direction %p stub!\n", debugstr_w( lang ), direction );

    if (!direction) return E_POINTER;
    *direction = -1;
    return E_NOTIMPL;
}

/***********************************************************************
 *           GetMrtDisplayLanguageList    (bcp47mrm.@)
 */
HRESULT WINAPI GetMrtDisplayLanguageList( void **list, size_t *count )
{
    FIXME( "list %p, count %p stub!\n", list, count );

    if (!list || !count) return E_POINTER;
    *list = NULL;
    *count = 0;
    return E_NOTIMPL;
}

/***********************************************************************
 *           GetParentCompositeRegionCode    (bcp47mrm.@)
 */
WORD WINAPI GetParentCompositeRegionCode( WORD code )
{
    FIXME( "code %u stub!\n", code );

    return 0;
}

/***********************************************************************
 *           IsValidUnIsoRegionTag    (bcp47mrm.@)
 */
BOOL WINAPI IsValidUnIsoRegionTag( const WCHAR *tag )
{
    FIXME( "tag %s stub!\n", debugstr_w( tag ) );

    return FALSE;
}
