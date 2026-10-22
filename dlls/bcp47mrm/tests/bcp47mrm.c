/*
 * BCP-47 language tag tests for bcp47mrm
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
 * The tag expectations below follow the RFC 5646 grammar accepted by
 * the implementation, including its documented leniencies: standalone
 * private use sequences, 4-digit region codes, 4-character mixed
 * alphanumeric variant subtags and all-digit 5-8 character variants.
 */

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "wine/test.h"

/* bcp47mrm has no public header; declare the functions under test. */
BOOL WINAPI IsWellFormedTag( const WCHAR *tag );
BOOL WINAPI IsValidTag( const WCHAR *tag );
HRESULT WINAPI Bcp47IsValid( const WCHAR *tag, BOOL *valid );
int WINAPI CompareBcp47Tags( const WCHAR *tag1, const WCHAR *tag2 );
HRESULT WINAPI Bcp47GetDistanceCString( const WCHAR *tag, const WCHAR *tag2, INT *distance );
HRESULT WINAPI Bcp47GetNeutralFormCString( const WCHAR *tag, WCHAR *buffer );
HRESULT WINAPI FormatLanguageList( const WCHAR *list, WCHAR delimiter, DWORD flags,
                                   WCHAR *buffer, size_t *chars_written );
HRESULT WINAPI FormatLanguageTag( const WCHAR *tag, DWORD flags, WCHAR *buffer, WCHAR *out );
HRESULT WINAPI GetApplicationLanguagesWithUserLanguagesFallback( const WCHAR *languages,
                                                                 const WCHAR *user_languages,
                                                                 WCHAR **out );
HRESULT WINAPI GetApplicationLayoutDirection( const WCHAR *lang, INT *direction );
HRESULT WINAPI GetClosenessOfUnIsoRegionTags( const WCHAR *tag1, const WCHAR *tag2, double *closeness );
WORD WINAPI GetCompositeRegionCode( const WCHAR *region );
HRESULT WINAPI GetDistanceOfClosestLanguageInList( const WCHAR *language, const WCHAR *languages_list,
                                                   WCHAR delimiter, double *distance );
HRESULT WINAPI GetLanguageDirectionality( const WCHAR *lang, INT *direction );
HRESULT WINAPI GetMrtDisplayLanguageList( void **list, size_t *count );
WORD WINAPI GetParentCompositeRegionCode( WORD code );
BOOL WINAPI IsValidUnIsoRegionTag( const WCHAR *tag );

static const struct
{
    const WCHAR *tag;
    BOOL expected;
}
syntax_cases[] =
{
    /* primary language subtags: 2-3 ASCII letters, any case */
    { L"en", TRUE },
    { L"de", TRUE },
    { L"ien", TRUE },
    { L"art", TRUE },
    { L"EN", TRUE },
    { L"eN", TRUE },

    /* language + region */
    { L"en-US", TRUE },
    { L"en-us", TRUE },
    { L"EN-us", TRUE },
    { L"en-gb", TRUE },
    { L"en-001", TRUE },
    { L"en-150", TRUE },
    { L"en-419", TRUE },

    /* language + script (+ region) */
    { L"zh-Hans", TRUE },
    { L"zh-Hant", TRUE },
    { L"zh-Hant-TW", TRUE },
    { L"zh-Hans-CN", TRUE },
    { L"sr-Cyrl-RS", TRUE },
    { L"sr-Latn-RS", TRUE },
    { L"shi-Latn", TRUE },
    { L"shi-Latn-MA", TRUE },
    { L"en-abcd-US", TRUE },

    /* 4-digit and mixed 4-character subtags: a 4-digit subtag fills a single
     * region slot, so it is accepted only when no region is present yet */
    { L"en-1234", TRUE },
    { L"de-DE-1901", FALSE },
    { L"de-DE-1996", FALSE },
    { L"de-CH-1901", FALSE },
    { L"en-ab12", TRUE },
    { L"en-12ab", TRUE },
    { L"en-US-12ab", TRUE },

    /* 5-8 character alphanumeric variants, including all-digit ones */
    { L"en-oxford", TRUE },
    { L"en-fonipa", TRUE },
    { L"en-fonupa", TRUE },
    { L"en-US-fonipa", TRUE },
    { L"zh-guoyim", TRUE },
    { L"zh-Latn-pinyin", TRUE },
    { L"sl-rozaj", TRUE },
    { L"sl-rozaj-biske", TRUE },
    { L"sl-rozaj-biske-1994", FALSE }, /* 4-digit subtag after a region */
    { L"de-DE-formal", TRUE },
    { L"sr-Latn-RS-ekavsk", TRUE },
    { L"en-12345", TRUE },
    { L"en-12345678", TRUE },
    { L"en-Latn-US-oxford-fonipa", TRUE },
    { L"en-Latn-ab12-oxford", TRUE },

    /* extended language subtags: 3 letters, at most three, before script/region */
    { L"zh-yue", TRUE },
    { L"zh-yue-HK", TRUE },
    { L"zh-yue-hak", TRUE },
    { L"zh-yue-hak-ksf", TRUE },

    /* extension sequences: singleton + 2-8 character subtags */
    { L"en-a-de", TRUE },
    { L"en-a-de-fg", TRUE },
    { L"en-a-de-fg-gh", TRUE },
    { L"en-1-abc", TRUE },
    { L"en-z-foo", TRUE },
    { L"en-a-12345678", TRUE },
    { L"en-a-99999999", TRUE },
    { L"en-a-de-b-xy", TRUE },
    { L"en-a-de-a-xy", TRUE },
    { L"abc-a-bc", TRUE },
    { L"en-Latn-US-oxford-a-de", TRUE },

    /* private use sequences: standalone, trailing and embedded */
    { L"x-priv", TRUE },
    { L"x-a", TRUE },
    { L"x-priv-abc-123", TRUE },
    { L"x-abcdefgh", TRUE },
    { L"X-priv", TRUE },
    { L"en-x-priv", TRUE },
    { L"en-X-priv", TRUE },
    { L"en-US-x-priv", TRUE },
    { L"en-x-a", TRUE },
    { L"en-x-priv-a-de", TRUE },
    { L"en-Latn-US-oxford-x-private", TRUE },
    /* a private use sequence consumes all subsequent alphanumerics,
     * including subtags that look like regions */
    { L"en-US-x-priv-DE", TRUE },
    { L"en-x-US", TRUE },
    { L"en-Latn-x-priv-US", TRUE },

    /* malformed: empty, separators, length and charset errors */
    { NULL, FALSE },
    { L"", FALSE },
    { L"-", FALSE },
    { L"-en", FALSE },
    { L"en-", FALSE },
    { L"en--US", FALSE },
    { L"en-US-", FALSE },
    { L"e", FALSE },
    { L"abcd", FALSE },
    { L"xpriv", FALSE },
    { L"12", FALSE },
    { L"12en", FALSE },
    { L"en ", FALSE },
    { L"en -US", FALSE },
    { L"en_US", FALSE },
    { L"en..US", FALSE },
    { L"en-US!", FALSE },
    { L"en-US ", FALSE },
    { L"en-12", FALSE },
    { L"en-123456789", FALSE },
    { L"en-abcdefghi", FALSE },

    /* malformed private use sequences */
    { L"x", FALSE },
    { L"x-", FALSE },
    { L"x-abcdefghi", FALSE },
    { L"x-x-", FALSE },
    { L"en-x", FALSE },

    /* malformed extension sequences: missing sequence, lone trailing singleton */
    { L"en-a", FALSE },
    { L"en-a-", FALSE },
    { L"en-a-1", FALSE },
    { L"en-a-de-f", FALSE },
    { L"en-a-de-1", FALSE },
    { L"en-a-de-x", FALSE },
    { L"en-1", FALSE },
    { L"en-1-2", FALSE },
    { L"en-!", FALSE },

    /* subtag ordering violations */
    { L"en-Hans-Hans", FALSE },
    { L"en-Latn-US-Latn", FALSE },
    { L"zh-TW-yue", FALSE },
    { L"zh-Hans-CN-yue", FALSE },
    { L"zh-yue-Hans-TW-yue", FALSE },
    { L"zh-yue-hak-ksf-jkl", FALSE },
    { L"en-US-1901-DE", FALSE },
    { L"en-1234-1234", FALSE },
    { L"en-US-oxford-123456789", FALSE },
};

/* 85 characters (the implementation limit): accepted; 86: rejected. */
static void test_tag_length(void)
{
    static const WCHAR base[] = L"x-a";
    WCHAR tag85[86], tag86[87];
    unsigned int i;

    lstrcpyW( tag85, base );
    for (i = 0; i < 41; i++) lstrcatW( tag85, L"-a" );
    lstrcpyW( tag86, L"x-ab" );
    for (i = 0; i < 41; i++) lstrcatW( tag86, L"-a" );

    ok( lstrlenW( tag85 ) == 85, "tag85 length %u, expected 85\n", lstrlenW( tag85 ) );
    ok( lstrlenW( tag86 ) == 86, "tag86 length %u, expected 86\n", lstrlenW( tag86 ) );

    /* sanity: both are 1-character private use subtag chains */
    ok( IsWellFormedTag( tag85 ) == TRUE, "IsWellFormedTag(85-char tag) returned %d, expected TRUE\n",
        IsWellFormedTag( tag85 ) );
    ok( IsValidTag( tag85 ) == TRUE, "IsValidTag(85-char tag) returned %d, expected TRUE\n",
        IsValidTag( tag85 ) );
    ok( IsWellFormedTag( tag86 ) == FALSE, "IsWellFormedTag(86-char tag) returned %d, expected FALSE\n",
        IsWellFormedTag( tag86 ) );
    ok( IsValidTag( tag86 ) == FALSE, "IsValidTag(86-char tag) returned %d, expected FALSE\n",
        IsValidTag( tag86 ) );
}

static void test_syntax_table(void)
{
    unsigned int i;

    for (i = 0; i < ARRAYSIZE( syntax_cases ); i++)
    {
        const WCHAR *tag = syntax_cases[i].tag;
        BOOL expected = syntax_cases[i].expected;
        BOOL well_formed = IsWellFormedTag( tag );
        BOOL valid = IsValidTag( tag );
        BOOL out = !expected;
        HRESULT hr = Bcp47IsValid( tag, &out );

        ok( well_formed == expected, "IsWellFormedTag(%s) returned %d, expected %d\n",
            debugstr_w( tag ), well_formed, expected );
        ok( valid == expected, "IsValidTag(%s) returned %d, expected %d\n",
            debugstr_w( tag ), valid, expected );
        ok( hr == S_OK, "Bcp47IsValid(%s) returned %#lx, expected S_OK\n", debugstr_w( tag ), hr );
        ok( out == expected, "Bcp47IsValid(%s) set valid to %d, expected %d\n",
            debugstr_w( tag ), out, expected );
    }
}

static void test_bcp47_is_valid(void)
{
    BOOL valid = TRUE;
    HRESULT hr;

    hr = Bcp47IsValid( L"en-US", NULL );
    ok( hr == E_POINTER, "Bcp47IsValid(en-US, NULL) returned %#lx, expected E_POINTER\n", hr );

    hr = Bcp47IsValid( NULL, &valid );
    ok( hr == S_OK, "Bcp47IsValid(NULL) returned %#lx, expected S_OK\n", hr );
    ok( valid == FALSE, "Bcp47IsValid(NULL) set valid to %d, expected FALSE\n", valid );
}

static void test_compare_tags(void)
{
    ok( CompareBcp47Tags( L"en-US", L"en-US" ) == 0, "CompareBcp47Tags(en-US, en-US) != 0\n" );
    ok( CompareBcp47Tags( L"en-US", L"en-us" ) == 0, "CompareBcp47Tags(en-US, en-us) != 0 (case insensitive)\n" );
    ok( CompareBcp47Tags( L"aaa", L"aab" ) < 0, "CompareBcp47Tags(aaa, aab) >= 0\n" );
    ok( CompareBcp47Tags( L"aab", L"aaa" ) > 0, "CompareBcp47Tags(aab, aaa) <= 0\n" );
    ok( CompareBcp47Tags( NULL, L"en" ) == -1, "CompareBcp47Tags(NULL, en) != -1\n" );
    ok( CompareBcp47Tags( L"en", NULL ) == 1, "CompareBcp47Tags(en, NULL) != 1\n" );
}

static void test_stubs(void)
{
    WCHAR buffer[64];
    INT direction, distance;
    size_t count = 12345;
    double closeness = 1.0, dist = 1.0;
    void *list = (void *)0x1;
    HRESULT hr;

    distance = 12345;
    hr = Bcp47GetDistanceCString( L"en", L"fr", &distance );
    ok( hr == E_NOTIMPL, "Bcp47GetDistanceCString returned %#lx, expected E_NOTIMPL\n", hr );
    ok( distance == 0, "Bcp47GetDistanceCString set distance to %d, expected 0\n", distance );
    hr = Bcp47GetDistanceCString( NULL, L"fr", &distance );
    ok( hr == E_INVALIDARG, "Bcp47GetDistanceCString(NULL) returned %#lx, expected E_INVALIDARG\n", hr );
    hr = Bcp47GetDistanceCString( L"en", L"fr", NULL );
    ok( hr == E_INVALIDARG, "Bcp47GetDistanceCString(NULL out) returned %#lx, expected E_INVALIDARG\n", hr );

    hr = Bcp47GetNeutralFormCString( L"en", buffer );
    ok( hr == E_NOTIMPL, "Bcp47GetNeutralFormCString returned %#lx, expected E_NOTIMPL\n", hr );
    hr = Bcp47GetNeutralFormCString( NULL, buffer );
    ok( hr == E_INVALIDARG, "Bcp47GetNeutralFormCString(NULL) returned %#lx, expected E_INVALIDARG\n", hr );
    hr = Bcp47GetNeutralFormCString( L"en", NULL );
    ok( hr == E_INVALIDARG, "Bcp47GetNeutralFormCString(NULL out) returned %#lx, expected E_INVALIDARG\n", hr );

    count = 12345;
    hr = FormatLanguageList( L"en", L',', 0, buffer, &count );
    ok( hr == E_NOTIMPL, "FormatLanguageList returned %#lx, expected E_NOTIMPL\n", hr );
    hr = FormatLanguageList( NULL, L',', 0, buffer, &count );
    ok( hr == E_INVALIDARG, "FormatLanguageList(NULL) returned %#lx, expected E_INVALIDARG\n", hr );
    hr = FormatLanguageList( L"en", L',', 0, NULL, &count );
    ok( hr == E_INVALIDARG, "FormatLanguageList(NULL buffer) returned %#lx, expected E_INVALIDARG\n", hr );
    hr = FormatLanguageList( L"en", L',', 0, buffer, NULL );
    ok( hr == E_INVALIDARG, "FormatLanguageList(NULL count) returned %#lx, expected E_INVALIDARG\n", hr );

    hr = FormatLanguageTag( L"en", 0, buffer, buffer );
    ok( hr == E_NOTIMPL, "FormatLanguageTag returned %#lx, expected E_NOTIMPL\n", hr );
    hr = FormatLanguageTag( NULL, 0, buffer, buffer );
    ok( hr == E_INVALIDARG, "FormatLanguageTag(NULL) returned %#lx, expected E_INVALIDARG\n", hr );
    hr = FormatLanguageTag( L"en", 0, NULL, buffer );
    ok( hr == E_INVALIDARG, "FormatLanguageTag(NULL buffer) returned %#lx, expected E_INVALIDARG\n", hr );
    hr = FormatLanguageTag( L"en", 0, buffer, NULL );
    ok( hr == E_INVALIDARG, "FormatLanguageTag(NULL out) returned %#lx, expected E_INVALIDARG\n", hr );

    hr = GetApplicationLanguagesWithUserLanguagesFallback( L"en", NULL, NULL );
    ok( hr == E_POINTER, "GetApplicationLanguagesWithUserLanguagesFallback(NULL user) returned %#lx, "
        "expected E_POINTER\n", hr );
    hr = GetApplicationLanguagesWithUserLanguagesFallback( L"en", L"fr", NULL );
    ok( hr == E_NOTIMPL, "GetApplicationLanguagesWithUserLanguagesFallback returned %#lx, "
        "expected E_NOTIMPL\n", hr );

    direction = 12345;
    hr = GetApplicationLayoutDirection( L"en", &direction );
    ok( hr == E_NOTIMPL, "GetApplicationLayoutDirection returned %#lx, expected E_NOTIMPL\n", hr );
    ok( direction == -1, "GetApplicationLayoutDirection set direction to %d, expected -1\n", direction );
    hr = GetApplicationLayoutDirection( L"en", NULL );
    ok( hr == E_POINTER, "GetApplicationLayoutDirection(NULL) returned %#lx, expected E_POINTER\n", hr );

    closeness = 1.0;
    hr = GetClosenessOfUnIsoRegionTags( L"en", L"fr", &closeness );
    ok( hr == E_NOTIMPL, "GetClosenessOfUnIsoRegionTags returned %#lx, expected E_NOTIMPL\n", hr );
    ok( closeness == 0.0, "GetClosenessOfUnIsoRegionTags set closeness to %g, expected 0\n", closeness );
    hr = GetClosenessOfUnIsoRegionTags( NULL, L"fr", &closeness );
    ok( hr == E_POINTER, "GetClosenessOfUnIsoRegionTags(NULL) returned %#lx, expected E_POINTER\n", hr );

    hr = GetDistanceOfClosestLanguageInList( L"en", L"fr", L',', &dist );
    ok( hr == HRESULT_FROM_WIN32( ERROR_NO_MATCH ), "GetDistanceOfClosestLanguageInList returned %#lx, "
        "expected %#lx\n", hr, HRESULT_FROM_WIN32( ERROR_NO_MATCH ) );
    ok( dist == 0.0, "GetDistanceOfClosestLanguageInList set distance to %g, expected 0\n", dist );
    hr = GetDistanceOfClosestLanguageInList( NULL, L"fr", L',', &dist );
    ok( hr == E_POINTER, "GetDistanceOfClosestLanguageInList(NULL) returned %#lx, expected E_POINTER\n", hr );
    hr = GetDistanceOfClosestLanguageInList( L"en", L"fr", L',', NULL );
    ok( hr == E_POINTER, "GetDistanceOfClosestLanguageInList(NULL out) returned %#lx, expected E_POINTER\n", hr );

    direction = 12345;
    hr = GetLanguageDirectionality( L"en", &direction );
    ok( hr == E_NOTIMPL, "GetLanguageDirectionality returned %#lx, expected E_NOTIMPL\n", hr );
    ok( direction == -1, "GetLanguageDirectionality set direction to %d, expected -1\n", direction );
    hr = GetLanguageDirectionality( L"en", NULL );
    ok( hr == E_POINTER, "GetLanguageDirectionality(NULL) returned %#lx, expected E_POINTER\n", hr );

    list = (void *)0x1;
    count = 12345;
    hr = GetMrtDisplayLanguageList( &list, &count );
    ok( hr == E_NOTIMPL, "GetMrtDisplayLanguageList returned %#lx, expected E_NOTIMPL\n", hr );
    ok( list == NULL, "GetMrtDisplayLanguageList set list to %p, expected NULL\n", list );
    ok( count == 0, "GetMrtDisplayLanguageList set count to %u, expected 0\n", (unsigned int)count );
    hr = GetMrtDisplayLanguageList( NULL, &count );
    ok( hr == E_POINTER, "GetMrtDisplayLanguageList(NULL) returned %#lx, expected E_POINTER\n", hr );

    ok( GetCompositeRegionCode( L"US" ) == 0, "GetCompositeRegionCode returned %u, expected 0\n",
        GetCompositeRegionCode( L"US" ) );
    ok( GetParentCompositeRegionCode( 0x1f4 ) == 0, "GetParentCompositeRegionCode returned %u, expected 0\n",
        GetParentCompositeRegionCode( 0x1f4 ) );
    ok( IsValidUnIsoRegionTag( L"1234" ) == FALSE, "IsValidUnIsoRegionTag returned %d, expected FALSE\n",
        IsValidUnIsoRegionTag( L"1234" ) );
}

START_TEST(bcp47mrm)
{
    test_syntax_table();
    test_tag_length();
    test_bcp47_is_valid();
    test_compare_tags();
    test_stubs();
}
