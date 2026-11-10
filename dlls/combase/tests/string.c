/*
 * Unit tests for Windows String functions
 *
 * Copyright (c) 2014 Martin Storsjo
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

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"

#include "wine/test.h"

#define check_string(str, content, length, has_null) _check_string(__LINE__, str, content, length, has_null)
static void _check_string(int line, HSTRING str, LPCWSTR content, UINT32 length, BOOL has_null)
{
    BOOL out_null;
    BOOL empty = length == 0;
    UINT32 out_length;
    LPCWSTR ptr;

    ok_(__FILE__, line)(WindowsIsStringEmpty(str) == empty, "WindowsIsStringEmpty failed\n");
    ok_(__FILE__, line)(WindowsStringHasEmbeddedNull(str, &out_null) == S_OK, "WindowsStringHasEmbeddedNull failed\n");
    ok_(__FILE__, line)(out_null == has_null, "WindowsStringHasEmbeddedNull failed\n");
    ok_(__FILE__, line)(WindowsGetStringLen(str) == length, "WindowsGetStringLen failed\n");
    ptr = WindowsGetStringRawBuffer(str, &out_length);
    /* WindowsGetStringRawBuffer should return a non-null, null terminated empty string
     * even if str is NULL. */
    ok_(__FILE__, line)(ptr != NULL, "WindowsGetStringRawBuffer returned null\n");
    ok_(__FILE__, line)(out_length == length, "WindowsGetStringRawBuffer returned incorrect length\n");
    ptr = WindowsGetStringRawBuffer(str, NULL);
    ok_(__FILE__, line)(ptr != NULL, "WindowsGetStringRawBuffer returned null\n");
    ok_(__FILE__, line)(ptr[length] == '\0', "WindowsGetStringRawBuffer doesn't return a null terminated buffer\n");
    ok_(__FILE__, line)(memcmp(ptr, content, sizeof(*content) * length) == 0, "Incorrect string content\n");
}

static const WCHAR input_string[] = L"abcdef\0";
static const WCHAR input_string1[] = L"abc";
static const WCHAR input_string2[] = L"def";
static const WCHAR input_embed_null[] = L"a\0c\0ef";
static const WCHAR output_substring[] = L"cdef";

static void test_create_delete(void)
{
    HSTRING str;
    HSTRING_HEADER header;

    /* Test normal creation of a string */
    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string\n");
    check_string(str, input_string, 6, FALSE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");
    /* Test error handling in WindowsCreateString */
    ok(WindowsCreateString(input_string, 6, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsCreateString(NULL, 6, &str) == E_POINTER, "Incorrect error handling\n");

    /* Test handling of a NULL string */
    ok(WindowsDeleteString(NULL) == S_OK, "Failed to delete null string\n");

    /* Test creation of a string reference */
    ok(WindowsCreateStringReference(input_string, 6, &header, &str) == S_OK, "Failed to create string ref\n");
    check_string(str, input_string, 6, FALSE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string ref\n");

    /* Test error handling in WindowsCreateStringReference */
    /* Strings to CreateStringReference must be null terminated with the correct
     * length. According to MSDN this should be E_INVALIDARG, but it returns
     * 0x80000017 in practice. */
    ok(FAILED(WindowsCreateStringReference(input_string, 5, &header, &str)), "Incorrect error handling\n");
    /* If the input string is non-null, it must be null-terminated even if the
     * length is zero. */
    ok(FAILED(WindowsCreateStringReference(input_string, 0, &header, &str)), "Incorrect error handling\n");
    ok(WindowsCreateStringReference(input_string, 6, NULL, &str) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsCreateStringReference(input_string, 6, &header, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsCreateStringReference(NULL, 6, &header, &str) == E_POINTER, "Incorrect error handling\n");

    /* Test creating a string without a null-termination at the specified length */
    ok(WindowsCreateString(input_string, 3, &str) == S_OK, "Failed to create string\n");
    check_string(str, input_string, 3, FALSE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    /* Test an empty string */
    ok(WindowsCreateString(L"", 0, &str) == S_OK, "Failed to create string\n");
    ok(str == NULL, "Empty string not a null string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    ok(WindowsCreateString(input_string, 0, &str) == S_OK, "Failed to create string\n");
    ok(str == NULL, "Empty string not a null string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    ok(WindowsCreateStringReference(L"", 0, &header, &str) == S_OK, "Failed to create string\n");
    ok(str == NULL, "Empty string not a null string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    ok(WindowsCreateString(NULL, 0, &str) == S_OK, "Failed to create string\n");
    ok(str == NULL, "Empty string not a null string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    ok(WindowsCreateStringReference(NULL, 0, &header, &str) == S_OK, "Failed to create string\n");
    ok(str == NULL, "Empty string not a null string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");
}

static void test_duplicate(void)
{
    HSTRING str, str2;
    HSTRING_HEADER header;
    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string\n");
    ok(WindowsDuplicateString(str, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsDuplicateString(str, &str2) == S_OK, "Failed to duplicate string\n");
    ok(str == str2, "Duplicated string created new string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string\n");

    ok(WindowsCreateStringReference(input_string, 6, &header, &str) == S_OK, "Failed to create string ref\n");
    ok(WindowsDuplicateString(str, &str2) == S_OK, "Failed to duplicate string\n");
    ok(str != str2, "Duplicated string ref didn't create new string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string ref\n");

    ok(WindowsDuplicateString(NULL, &str2) == S_OK, "Failed to duplicate NULL string\n");
    ok(str2 == NULL, "Duplicated string created new string\n");
    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string\n");
}

static void test_access(void)
{
    HSTRING str;
    HSTRING_HEADER header;

    /* Test handling of a NULL string */
    check_string(NULL, NULL, 0, FALSE);

    /* Test strings with embedded null chars */
    ok(WindowsCreateString(input_embed_null, 6, &str) == S_OK, "Failed to create string\n");
    check_string(str, input_embed_null, 6, TRUE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    ok(WindowsCreateStringReference(input_embed_null, 6, &header, &str) == S_OK, "Failed to create string ref\n");
    check_string(str, input_embed_null, 6, TRUE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string ref\n");

    /* Test normal creation of a string with trailing null */
    ok(WindowsCreateString(input_string, 7, &str) == S_OK, "Failed to create string\n");
    check_string(str, input_string, 7, TRUE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    ok(WindowsCreateStringReference(input_string, 7, &header, &str) == S_OK, "Failed to create string ref\n");
    check_string(str, input_string, 7, TRUE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string ref\n");
}

static void test_string_buffer(void)
{
    /* Initialize ptr to NULL to make sure it actually is set in the first
     * test below. */
    HSTRING_BUFFER buf = NULL;
    WCHAR *ptr = NULL;
    HSTRING str;

    /* Test creation of an empty buffer */
    ok(WindowsPreallocateStringBuffer(0, &ptr, &buf) == S_OK, "Failed to preallocate string buffer\n");
    ok(ptr != NULL, "Empty string didn't return a buffer pointer\n");
    ok(WindowsPromoteStringBuffer(buf, &str) == S_OK, "Failed to promote string buffer\n");
    ok(str == NULL, "Empty string isn't a null string\n");
    check_string(str, L"", 0, FALSE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    ok(WindowsDeleteStringBuffer(NULL) == S_OK, "Failed to delete null string buffer\n");

    /* Test creation and deletion of string buffers */
    ok(WindowsPreallocateStringBuffer(6, &ptr, &buf) == S_OK, "Failed to preallocate string buffer\n");
    ok(WindowsDeleteStringBuffer(buf) == S_OK, "Failed to delete string buffer\n");

    /* Test creation and promotion of string buffers */
    ok(WindowsPreallocateStringBuffer(6, &ptr, &buf) == S_OK, "Failed to preallocate string buffer\n");
    ok(ptr[6] == '\0', "Preallocated string buffer didn't have null termination\n");
    memcpy(ptr, input_string, 6 * sizeof(*input_string));
    ok(WindowsPromoteStringBuffer(buf, NULL) == E_POINTER, "Incorrect error handling\n");
    ok(WindowsPromoteStringBuffer(buf, &str) == S_OK, "Failed to promote string buffer\n");
    check_string(str, input_string, 6, FALSE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    /* Test error handling in preallocation */
    ok(WindowsPreallocateStringBuffer(6, NULL, &buf) == E_POINTER, "Incorrect error handling\n");
    ok(WindowsPreallocateStringBuffer(6, &ptr, NULL) == E_POINTER, "Incorrect error handling\n");

    ok(WindowsPreallocateStringBuffer(6, &ptr, &buf) == S_OK, "Failed to preallocate string buffer\n");
    ptr[6] = 'a'; /* Overwrite the buffer's null termination, promotion should fail */
    ok(WindowsPromoteStringBuffer(buf, &str) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsDeleteStringBuffer(buf) == S_OK, "Failed to delete string buffer\n");

    /* Test strings with trailing null chars */
    ok(WindowsPreallocateStringBuffer(7, &ptr, &buf) == S_OK, "Failed to preallocate string buffer\n");
    memcpy(ptr, input_string, 7 * sizeof(*input_string));
    ok(WindowsPromoteStringBuffer(buf, &str) == S_OK, "Failed to promote string buffer\n");
    check_string(str, input_string, 7, TRUE);
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");
}

static void test_substring(void)
{
    HSTRING str, substr;
    HSTRING_HEADER header;

    /* Test substring of string buffers */
    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string\n");
    ok(WindowsSubstring(str, 2, &substr) == S_OK, "Failed to create substring\n");
    check_string(substr, output_substring, 4, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 2, 3, &substr) == S_OK, "Failed to create substring\n");
    check_string(substr, output_substring, 3, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    /* Test duplication of string using substring */
    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string\n");
    ok(WindowsSubstring(str, 0, &substr) == S_OK, "Failed to create substring\n");
    ok(str != substr, "Duplicated string didn't create new string\n");
    check_string(substr, input_string, 6, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 0, 6, &substr) == S_OK, "Failed to create substring\n");
    ok(str != substr, "Duplicated string didn't create new string\n");
    check_string(substr, input_string, 6, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    /* Test substring of string reference */
    ok(WindowsCreateStringReference(input_string, 6, &header, &str) == S_OK, "Failed to create string ref\n");
    ok(WindowsSubstring(str, 2, &substr) == S_OK, "Failed to create substring of string ref\n");
    check_string(substr, output_substring, 4, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 2, 3, &substr) == S_OK, "Failed to create substring of string ref\n");
    check_string(substr, output_substring, 3, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string ref\n");

    /* Test duplication of string reference using substring */
    ok(WindowsCreateStringReference(input_string, 6, &header, &str) == S_OK, "Failed to create string ref\n");
    ok(WindowsSubstring(str, 0, &substr) == S_OK, "Failed to create substring of string ref\n");
    ok(str != substr, "Duplicated string ref didn't create new string\n");
    check_string(substr, input_string, 6, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 0, 6, &substr) == S_OK, "Failed to create substring of string ref\n");
    ok(str != substr, "Duplicated string ref didn't create new string\n");
    check_string(substr, input_string, 6, FALSE);
    ok(WindowsDeleteString(substr) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string ref\n");

    /* Test get substring of empty string */
    ok(WindowsSubstring(NULL, 0, &substr) == S_OK, "Failed to duplicate NULL string\n");
    ok(substr == NULL, "Substring created new string\n");
    ok(WindowsSubstringWithSpecifiedLength(NULL, 0, 0, &substr) == S_OK, "Failed to duplicate NULL string\n");
    ok(substr == NULL, "Substring created new string\n");

    /* Test get empty substring of string */
    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string\n");
    ok(WindowsSubstring(str, 6, &substr) == S_OK, "Failed to create substring\n");
    ok(substr == NULL, "Substring created new string\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 6, 0, &substr) == S_OK, "Failed to create substring\n");
    ok(substr == NULL, "Substring created new string\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    /* Test handling of using too high start index or length */
    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string\n");
    ok(WindowsSubstring(str, 7, &substr) == E_BOUNDS, "Incorrect error handling\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 7, 0, &substr) == E_BOUNDS, "Incorrect error handling\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 6, 1, &substr) == E_BOUNDS, "Incorrect error handling\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 7, ~0U, &substr) == E_BOUNDS, "Incorrect error handling\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");

    /* Test handling of a NULL string  */
    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string\n");
    ok(WindowsSubstring(str, 7, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsSubstringWithSpecifiedLength(str, 7, 0, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string\n");
}

static void test_concat(void)
{
    HSTRING str1, str2, concat;
    HSTRING_HEADER header1, header2;

    /* Test concatenation of string buffers */
    ok(WindowsCreateString(input_string1, 3, &str1) == S_OK, "Failed to create string\n");
    ok(WindowsCreateString(input_string2, 3, &str2) == S_OK, "Failed to create string\n");

    ok(WindowsConcatString(str1, NULL, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsConcatString(str1, NULL, &concat) == S_OK, "Failed to concatenate string\n");
    ok(str1 == concat, "Concatenate created new string\n");
    check_string(concat, input_string1, 3, FALSE);
    ok(WindowsDeleteString(concat) == S_OK, "Failed to delete string\n");

    ok(WindowsConcatString(NULL, str2, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsConcatString(NULL, str2, &concat) == S_OK, "Failed to concatenate string\n");
    ok(str2 == concat, "Concatenate created new string\n");
    check_string(concat, input_string2, 3, FALSE);
    ok(WindowsDeleteString(concat) == S_OK, "Failed to delete string\n");

    ok(WindowsConcatString(str1, str2, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsConcatString(str1, str2, &concat) == S_OK, "Failed to concatenate string\n");
    check_string(concat, input_string, 6, FALSE);
    ok(WindowsDeleteString(concat) == S_OK, "Failed to delete string\n");

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str1) == S_OK, "Failed to delete string\n");

    /* Test concatenation of string references */
    ok(WindowsCreateStringReference(input_string1, 3, &header1, &str1) == S_OK, "Failed to create string ref\n");
    ok(WindowsCreateStringReference(input_string2, 3, &header2, &str2) == S_OK, "Failed to create string ref\n");

    ok(WindowsConcatString(str1, NULL, &concat) == S_OK, "Failed to concatenate string\n");
    ok(str1 != concat, "Concatenate string ref didn't create new string\n");
    check_string(concat, input_string1, 3, FALSE);
    ok(WindowsDeleteString(concat) == S_OK, "Failed to delete string\n");

    ok(WindowsConcatString(NULL, str2, &concat) == S_OK, "Failed to concatenate string\n");
    ok(str2 != concat, "Concatenate string ref didn't create new string\n");
    check_string(concat, input_string2, 3, FALSE);
    ok(WindowsDeleteString(concat) == S_OK, "Failed to delete string\n");

    ok(WindowsConcatString(str1, str2, &concat) == S_OK, "Failed to concatenate string\n");
    check_string(concat, input_string, 6, FALSE);
    ok(WindowsDeleteString(concat) == S_OK, "Failed to delete string\n");

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string ref\n");
    ok(WindowsDeleteString(str1) == S_OK, "Failed to delete string ref\n");

    /* Test concatenation of two empty strings */
    ok(WindowsConcatString(NULL, NULL, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsConcatString(NULL, NULL, &concat) == S_OK, "Failed to concatenate string\n");
    ok(concat == NULL, "Concatenate created new string\n");
}

static void test_compare(void)
{
    HSTRING str1, str2;
    HSTRING_HEADER header1, header2;
    INT32 res;

    /* Test comparison of string buffers */
    ok(WindowsCreateString(input_string1, 3, &str1) == S_OK, "Failed to create string\n");
    ok(WindowsCreateString(input_string2, 3, &str2) == S_OK, "Failed to create string\n");

    ok(WindowsCompareStringOrdinal(str1, str1, &res) == S_OK, "Failed to compare string\n");
    ok(res == 0, "Expected 0, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str1, str2, &res) == S_OK, "Failed to compare string\n");
    ok(res == -1, "Expected -1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str2, str1, &res) == S_OK, "Failed to compare string\n");
    ok(res == 1, "Expected 1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str2, str2, &res) == S_OK, "Failed to compare string\n");
    ok(res == 0, "Expected 0, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str1, NULL, &res) == S_OK, "Failed to compare string\n");
    ok(res == 1, "Expected 1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(NULL, str1, &res) == S_OK, "Failed to compare string\n");
    ok(res == -1, "Expected -1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str2, NULL, &res) == S_OK, "Failed to compare string\n");
    ok(res == 1, "Expected 1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(NULL, str2, &res) == S_OK, "Failed to compare string\n");
    ok(res == -1, "Expected -1, got %d\n", res);

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str1) == S_OK, "Failed to delete string\n");

    /* Test comparison of string references */
    ok(WindowsCreateStringReference(input_string1, 3, &header1, &str1) == S_OK, "Failed to create string ref\n");
    ok(WindowsCreateStringReference(input_string2, 3, &header2, &str2) == S_OK, "Failed to create string ref\n");

    ok(WindowsCompareStringOrdinal(str1, str1, &res) == S_OK, "Failed to compare string\n");
    ok(res == 0, "Expected 0, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str1, str2, &res) == S_OK, "Failed to compare string\n");
    ok(res == -1, "Expected -1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str2, str1, &res) == S_OK, "Failed to compare string\n");
    ok(res == 1, "Expected 1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str2, str2, &res) == S_OK, "Failed to compare string\n");
    ok(res == 0, "Expected 0, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str1, NULL, &res) == S_OK, "Failed to compare string\n");
    ok(res == 1, "Expected 1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(NULL, str1, &res) == S_OK, "Failed to compare string\n");
    ok(res == -1, "Expected -1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(str2, NULL, &res) == S_OK, "Failed to compare string\n");
    ok(res == 1, "Expected 1, got %d\n", res);
    ok(WindowsCompareStringOrdinal(NULL, str2, &res) == S_OK, "Failed to compare string\n");
    ok(res == -1, "Expected -1, got %d\n", res);

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string ref\n");
    ok(WindowsDeleteString(str1) == S_OK, "Failed to delete string ref\n");

    /* Test comparison of two empty strings */
    ok(WindowsCompareStringOrdinal(NULL, NULL, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsCompareStringOrdinal(NULL, NULL, &res) == S_OK, "Failed to compare NULL string\n");
    ok(res == 0, "Expected 0, got %d\n", res);
}

static void test_trim(void)
{
    HSTRING str1, str2, trimmed;
    HSTRING_HEADER header1, header2;

    /* Test trimming of string buffers */
    ok(WindowsCreateString(input_string, 6, &str1) == S_OK, "Failed to create string\n");
    ok(WindowsCreateString(input_string1, 3, &str2) == S_OK, "Failed to create string\n");

    ok(WindowsTrimStringStart(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    check_string(trimmed, input_string2, 3, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");
    ok(WindowsTrimStringEnd(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    ok(trimmed == str1, "Trimmed string created new string\n");
    check_string(trimmed, input_string, 6, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string\n");
    ok(WindowsCreateString(input_string2, 3, &str2) == S_OK, "Failed to create string\n");

    ok(WindowsTrimStringStart(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    ok(trimmed == str1, "Trimmed string created new string\n");
    check_string(trimmed, input_string, 6, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");
    ok(WindowsTrimStringEnd(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    check_string(trimmed, input_string1, 3, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string\n");
    ok(WindowsDeleteString(str1) == S_OK, "Failed to delete string\n");

    /* Test trimming of string references */
    ok(WindowsCreateStringReference(input_string, 6, &header1, &str1) == S_OK, "Failed to create string ref\n");
    ok(WindowsCreateStringReference(input_string1, 3, &header2, &str2) == S_OK, "Failed to create string ref\n");

    ok(WindowsTrimStringStart(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    check_string(trimmed, input_string2, 3, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");
    ok(WindowsTrimStringEnd(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    ok(trimmed != str1, "Trimmed string ref didn't create new string\n");
    check_string(trimmed, input_string, 6, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string ref\n");
    ok(WindowsCreateStringReference(input_string2, 3, &header2, &str2) == S_OK, "Failed to create string ref\n");

    ok(WindowsTrimStringStart(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    ok(trimmed != str1, "Trimmed string ref didn't create new string\n");
    check_string(trimmed, input_string, 6, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");
    ok(WindowsTrimStringEnd(str1, str2, &trimmed) == S_OK, "Failed to trim string\n");
    check_string(trimmed, input_string1, 3, FALSE);
    ok(WindowsDeleteString(trimmed) == S_OK, "Failed to delete string\n");

    ok(WindowsDeleteString(str2) == S_OK, "Failed to delete string ref\n");
    ok(WindowsDeleteString(str1) == S_OK, "Failed to delete string ref\n");

    /* Test handling of NULL strings */
    ok(WindowsCreateString(input_string, 6, &str1) == S_OK, "Failed to create string\n");
    ok(WindowsTrimStringStart(NULL, NULL, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsTrimStringStart(NULL, str1, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsTrimStringStart(NULL, NULL, &trimmed) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsTrimStringStart(NULL, str1, &trimmed) == S_OK, "Failed to trim empty string\n");
    ok(trimmed == NULL, "Trimming created new string\n");
    ok(WindowsTrimStringEnd(NULL, NULL, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsTrimStringEnd(NULL, str1, NULL) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsTrimStringEnd(NULL, NULL, &trimmed) == E_INVALIDARG, "Incorrect error handling\n");
    ok(WindowsTrimStringEnd(NULL, str1, &trimmed) == S_OK, "Failed to trim empty string\n");
    ok(trimmed == NULL, "Trimming created new string\n");
    ok(WindowsDeleteString(str1) == S_OK, "Failed to delete string\n");
}

static void test_hstring_struct(void)
{
    struct hstring_header
    {
        UINT32 flags;
        UINT32 length;
        UINT32 padding1;
        UINT32 padding2;
        const WCHAR *str;
    };

    struct hstring_private
    {
        struct hstring_header header;
        LONG refcount;
        WCHAR buffer[1];
    };

    HSTRING str;
    HSTRING str2;
    HSTRING_HEADER hdr;
    struct hstring_private* prv;
    struct hstring_private* prv2;

    BOOL arch64 = (sizeof(void*) == 8);

    ok(arch64 ? (sizeof(prv->header) == 24) : (sizeof(prv->header) == 20), "hstring_header size incorrect.\n");

    ok(WindowsCreateString(input_string, 6, &str) == S_OK, "Failed to create string.\n");

    prv = CONTAINING_RECORD(str, struct hstring_private, header);

    ok(prv->header.flags == 0, "Expected 0 in flags field, got %#x.\n", prv->header.flags);
    ok(prv->header.length == 6, "Expected 6 in length field, got %u.\n", prv->header.length);
    ok(prv->header.str == prv->buffer, "Expected str to point at buffer, instead pointing at %p.\n", prv->header.str);
    ok(prv->refcount == 1, "Expected 1 in refcount, got %lu.\n", prv->refcount);
    ok(wcscmp(input_string, prv->buffer) == 0, "Expected strings to match.\n");
    ok(prv->buffer[prv->header.length] == '\0', "Expected buffer to be null terminated.\n");

    ok(WindowsDuplicateString(str, &str2) == S_OK, "Failed to duplicate string.\n");

    prv2 = CONTAINING_RECORD(str2, struct hstring_private, header);

    ok(prv->refcount == 2, "Expected 2 in refcount, got %lu.\n", prv->refcount);
    ok(prv2->refcount == 2, "Expected 2 in refcount, got %lu.\n", prv2->refcount);
    ok(wcscmp(input_string, prv2->buffer) == 0, "Expected strings to match.\n");

    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string.\n");

    ok(prv->refcount == 1, "Expected 1 in refcount, got %lu.\n", prv->refcount);

    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string.\n");

    ok(WindowsCreateStringReference(input_string, 6, &hdr, &str) == S_OK, "Failed to create string ref.\n");

    prv = CONTAINING_RECORD(&hdr, struct hstring_private, header);
    prv2 = CONTAINING_RECORD(str, struct hstring_private, header);

    ok(prv == prv2, "Pointers not identical.\n");
    ok(prv2->header.flags == 1, "Expected HSTRING_REFERENCE_FLAG to be set, got %#x.\n", prv2->header.flags);
    ok(prv2->header.length == 6, "Expected 6 in length field, got %u.\n", prv2->header.length);
    ok(prv2->header.str == input_string, "Expected str to point at input_string, instead pointing at %p.\n", prv2->header.str);

    ok(WindowsDeleteString(str) == S_OK, "Failed to delete string ref.\n");

    /* Test inline buffer string (24 WCHARs) */
    {
        static const WCHAR str24[] = L"012345678901234567891234";
        ok(WindowsCreateString(str24, 24, &str) == S_OK, "Failed to create 24-char string.\n");
        prv = CONTAINING_RECORD(str, struct hstring_private, header);
        ok(prv->header.flags == 0, "Expected 0 flags, got %#x.\n", prv->header.flags);
        ok(prv->header.length == 24, "Expected length 24, got %u.\n", prv->header.length);
        ok(prv->header.str == prv->buffer, "Expected str to point at buffer, got %p.\n", prv->header.str);
        ok(prv->refcount == 1, "Expected refcount 1, got %lu.\n", prv->refcount);
        ok(wcscmp(str24, prv->buffer) == 0, "Expected strings to match.\n");
        ok(prv->buffer[24] == '\0', "Expected null termination.\n");
        ok(WindowsDeleteString(str) == S_OK, "Failed to delete string.\n");
    }

    /* Test string beyond inline limit (40 WCHARs) */
    {
        static const WCHAR str40[] = L"0123456789012345678901234567890123456789";
        ok(WindowsCreateString(str40, 40, &str) == S_OK, "Failed to create 40-char string.\n");
        prv = CONTAINING_RECORD(str, struct hstring_private, header);
        ok(prv->header.flags == 0, "Expected 0 flags, got %#x.\n", prv->header.flags);
        ok(prv->header.length == 40, "Expected length 40, got %u.\n", prv->header.length);
        ok(prv->header.str == prv->buffer, "Expected str to point at buffer, got %p.\n", prv->header.str);
        ok(prv->refcount == 1, "Expected refcount 1, got %lu.\n", prv->refcount);
        ok(wcscmp(str40, prv->buffer) == 0, "Expected strings to match.\n");
        ok(prv->buffer[40] == '\0', "Expected null termination.\n");
        ok(WindowsDeleteString(str) == S_OK, "Failed to delete string.\n");
    }
}

static void * WINAPI user_allocate(SIZE_T size)
{
    ok(0, "unexpected user_allocate call\n");
    return CoTaskMemAlloc(size);
}

static void WINAPI user_free(void *p)
{
    ok(0, "unexpected user_free call\n");
    CoTaskMemFree(p);
}

static void init_user_marshal_cb(USER_MARSHAL_CB *umcb,
                                 PMIDL_STUB_MESSAGE stub_msg,
                                 PRPC_MESSAGE rpc_msg, unsigned char *buffer,
                                 unsigned int size, MSHCTX context)
{
    memset(rpc_msg, 0, sizeof(*rpc_msg));
    rpc_msg->Buffer = buffer;
    rpc_msg->BufferLength = size;

    memset(stub_msg, 0, sizeof(*stub_msg));
    stub_msg->RpcMsg = rpc_msg;
    stub_msg->Buffer = buffer;
    stub_msg->pfnAllocate = user_allocate;
    stub_msg->pfnFree = user_free;

    memset(umcb, 0, sizeof(*umcb));
    umcb->Flags = MAKELONG(context, NDR_LOCAL_DATA_REPRESENTATION);
    umcb->pStubMsg = stub_msg;
    umcb->Signature = USER_MARSHAL_CB_SIGNATURE;
    umcb->CBType = buffer ? USER_MARSHAL_CB_UNMARSHALL : USER_MARSHAL_CB_BUFFER_SIZE;
}

#define ALIGNED_LENGTH(_Len, _Align) (((_Len)+(_Align))&~(_Align))
#define ALIGNED_POINTER(_Ptr, _Align) ((LPVOID)ALIGNED_LENGTH((ULONG_PTR)(_Ptr), _Align))

static void test_marshal(void)
{
    struct hstring_wire_inproc
    {
        ULONG context;
        HSTRING str;
    };

    struct hstring_wire_local
    {
        ULONG context;
        ULONG size;
        WCHAR data[1];
    };

    const ULONG exp_context = sizeof(void *) == 8 ? WDT_INPROC64_CALL : WDT_INPROC_CALL;
    static const WCHAR str_buf[] = L"marshal_test1";
    static const ULONG str_len = ARRAY_SIZE(str_buf) - 1;
    static const ULONG str_bytes = str_len * sizeof(WCHAR);
    HSTRING str, str_empty = NULL, str2 = NULL;
    struct hstring_wire_inproc *inproc;
    struct hstring_wire_local *local;
    MIDL_STUB_MESSAGE stub_msg;
    USER_MARSHAL_CB umcb;
    ULONG size, exp_size;
    RPC_MESSAGE rpc_msg;
    BYTE *next, *buffer;
    INT32 result = -1;
    HRESULT hr;

    buffer = calloc(1, 80);

    hr = WindowsCreateString(str_buf, wcslen(str_buf), &str);
    ok(hr == S_OK, "got hr %#lx\n", hr);

    /* INPROC marshaling */
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, NULL, 0, MSHCTX_INPROC);
    size = HSTRING_UserSize(&umcb.Flags, 0, &str);
    exp_size = sizeof(*inproc);
    ok(size == exp_size, "got size %lu != %lu\n", size, exp_size);
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, buffer, size, MSHCTX_INPROC);
    next = HSTRING_UserMarshal(&umcb.Flags, buffer, &str);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    inproc = (struct hstring_wire_inproc *)buffer;
    ok(inproc->context == exp_context, "got unexpected prefix %#lx != %#lx\n", inproc->context, exp_context);
    /* INPROC marshaling just consists of increasing the refcount and copying the address. */
    ok(inproc->str == str, "got unexpected address %p\n", inproc->str);
    inproc->context = 0xdeadbeef; /* The context value is not validated. */
    next = HSTRING_UserUnmarshal(&umcb.Flags, buffer, &str2);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    ok(str2 == str, "got str2 %p != %p\n", str2, str);
    HSTRING_UserFree(&umcb.Flags, &str2);

    /* Test alignment */
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, NULL, 0, MSHCTX_INPROC);
    size = HSTRING_UserSize(&umcb.Flags, 1, &str);
    exp_size = ALIGNED_LENGTH(1 + sizeof(*inproc), 7);
    ok(size == exp_size, "got size %lu != %lu\n", size, exp_size);
    memset(buffer, 0, 80);
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, buffer, size, MSHCTX_INPROC);
    next = HSTRING_UserMarshal(&umcb.Flags, &buffer[1], &str);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    inproc = ALIGNED_POINTER(&buffer[1], 7);
    ok(inproc->context == exp_context, "got unexpected prefix %#lx != %#lx\n", inproc->context, exp_context);
    ok(inproc->str == str, "got unexpected address %p\n", inproc->str);
    next = HSTRING_UserUnmarshal(&umcb.Flags, &buffer[1], &str2);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    ok(str2 == str, "got str2 %p != %p\n", str2, str);
    HSTRING_UserFree(&umcb.Flags, &str2);

    /* INPROC marshaling with empty/NULL HSTRING */
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, NULL, 0, MSHCTX_INPROC);
    size = HSTRING_UserSize(&umcb.Flags, 0, &str_empty);
    exp_size = sizeof(*inproc);
    ok(size == exp_size, "got size %lu != %lu\n", size, exp_size);
    memset(buffer, 0xff, 80);
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, buffer, size, MSHCTX_INPROC);
    next = HSTRING_UserMarshal(&umcb.Flags, buffer, &str_empty);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    inproc = (struct hstring_wire_inproc *)buffer;
    ok(inproc->context == exp_context, "got unexpected prefix %#lx != %#lx\n", inproc->context, exp_context);
    ok(!inproc->str, "got unexpected address %p\n", inproc->str);
    str2 = NULL;
    next = HSTRING_UserUnmarshal(&umcb.Flags, buffer, &str2);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    ok(!str2, "got str2 %p\n", str2);
    HSTRING_UserFree(&umcb.Flags, &str2);

    /* Out of process marshaling */
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, NULL, 0, MSHCTX_LOCAL);
    size = HSTRING_UserSize(&umcb.Flags, 0, &str);
    exp_size = offsetof(struct hstring_wire_local, data[str_len]);
    ok(size == exp_size, "got size %lu != %lu\n", size, exp_size);
    memset(buffer, 0, 80);
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, buffer, size, MSHCTX_LOCAL);
    next = HSTRING_UserMarshal(&umcb.Flags, buffer, &str);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    local = (struct hstring_wire_local *)buffer;
    ok(local->size == str_bytes, "got buf.size %lu != %lu\n", local->size, str_bytes);
    ok(local->context == exp_context, "got unexpected prefix %#lx != %#lx\n", local->context, exp_context);
    ok(!memcmp(local->data, str_buf, str_bytes), "got buf.data %s\n", debugstr_wn(local->data, str_bytes));
    str2 = NULL;
    local->context = 0xdeadbeef; /* The context value is not validated. */
    next = HSTRING_UserUnmarshal(&umcb.Flags, buffer, &str2);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    /* A new HSTRING should be allocated */
    ok(str2 != str, "got str2 %p\n", str2);
    hr = WindowsCompareStringOrdinal(str2, str, &result);
    ok(hr == S_OK, "got hr %#lx\n", hr);
    ok(!result, "got str2 %s != %s\n", debugstr_hstring(str2), debugstr_hstring(str));
    HSTRING_UserFree(&umcb.Flags, &str2);

    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, NULL, 0, MSHCTX_LOCAL);
    size = HSTRING_UserSize(&umcb.Flags, 1, &str);
    exp_size = ALIGNED_LENGTH(1, 7) + offsetof(struct hstring_wire_local, data[str_len]);
    ok(size == exp_size, "got size %lu != %lu\n", size, exp_size);
    memset(buffer, 0, 80);
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, buffer, size, MSHCTX_LOCAL);
    next = HSTRING_UserMarshal(&umcb.Flags, &buffer[1], &str);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    local = ALIGNED_POINTER(&buffer[1], 7);
    ok(local->size == str_bytes, "got buf.size %lu != %lu\n", local->size, str_bytes);
    ok(local->context == exp_context, "got unexpected prefix %#lx != %#lx\n", local->context, exp_context);
    ok(!memcmp(local->data, str_buf, str_bytes), "got buf.data %s\n", debugstr_wn(local->data, str_bytes));
    next = HSTRING_UserUnmarshal(&umcb.Flags, &buffer[1], &str2);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    ok(str2 != str, "got str2 %p\n", str2);
    result = -1;
    hr = WindowsCompareStringOrdinal(str2, str, &result);
    ok(hr == S_OK, "got hr %#lx\n", hr);
    ok(!result, "got str2 %s != %s\n", debugstr_hstring(str2), debugstr_hstring(str));
    HSTRING_UserFree(&umcb.Flags, &str2);

    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, NULL, 0, MSHCTX_LOCAL);
    size = HSTRING_UserSize(&umcb.Flags, 0, &str_empty);
    exp_size = offsetof(struct hstring_wire_local, data[0]);
    ok(size == exp_size, "got size %lu != %lu\n", size, exp_size);
    memset(buffer, 0xff, 80);
    init_user_marshal_cb(&umcb, &stub_msg, &rpc_msg, buffer, size, MSHCTX_LOCAL);
    next = HSTRING_UserMarshal(&umcb.Flags, buffer, &str_empty);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    local = (struct hstring_wire_local *)buffer;
    ok(local->context == exp_context, "got unexpected prefix %#lx != %#lx\n", local->context, exp_context);
    ok(!local->size, "got buf.size %lu\n", local->size);
    str2 = NULL;
    next = HSTRING_UserUnmarshal(&umcb.Flags, buffer, &str2);
    ok(next == &buffer[size], "got next %p != %p\n", next, &buffer[size]);
    ok(!str2, "got str2 %p\n", str2);
    HSTRING_UserFree(&umcb.Flags, &str2);

    WindowsDeleteString(str);
    free(buffer);
}

/*
 * Virtualized (tagged) HSTRINGs.  WinRT virtualized-string producers (.NET
 * Native / CoreRT AOT code in particular) pass handles whose low 3 bits carry
 * a type tag with the payload 8-byte aligned at (handle & ~7):
 *
 *   0 = plain heap handle (this file's existing tests)
 *   1 = atom-table string: no atom table to resolve against -- must not AV,
 *       must not free, must not fabricate content
 *   2 = inline buffer {UINT32 cch; WCHAR chars[cch+1]} at (handle & ~7)
 *   7 = managed GC string: MethodTable*@+0x00, UINT32 cch@+0x08, chars at
 *       +0x0C (coreclr m_firstChar) or +0x10 (RTString layout note)
 *
 * Windows semantics asserted: decoded length, NUL-terminated UTF-16 raw
 * buffer, and WindowsDeleteString on a virtualized handle = no-op success
 * (virtualized handles are not heap-owned, so deleting must not free).
 */

#define MT_FILLER     ((void *)0xdeadbeefcafe1234) /* MethodTable* slot */
#define PAYLOAD_SIZE  80

static DECLSPEC_ALIGN(8) BYTE managed_a[PAYLOAD_SIZE];   /* chars at +0x0C (coreclr) */
static DECLSPEC_ALIGN(8) BYTE managed_b[PAYLOAD_SIZE];   /* chars at +0x10 (RTString) */
static DECLSPEC_ALIGN(8) BYTE managed_null[PAYLOAD_SIZE];/* chars at +0x0C, embedded NUL */
static DECLSPEC_ALIGN(8) BYTE managed_empty[PAYLOAD_SIZE];/* cch 0 */
static DECLSPEC_ALIGN(8) BYTE managed_bad[PAYLOAD_SIZE]; /* no NUL terminator */
static DECLSPEC_ALIGN(8) BYTE managed_huge[PAYLOAD_SIZE]; /* out-of-range cch */
static DECLSPEC_ALIGN(8) BYTE inline_ok[PAYLOAD_SIZE];   /* tag 2 payload */
static DECLSPEC_ALIGN(8) BYTE inline_null[PAYLOAD_SIZE]; /* tag 2, embedded NUL */
static DECLSPEC_ALIGN(8) BYTE inline_empty[PAYLOAD_SIZE];/* tag 2, cch 0 */

static const WCHAR virt_content[] = L"abcdef";
static const WCHAR virt_null[] = { 'a', 0, 'c', 0 };
static const WCHAR virt_tail[] = L"cdef";
static const WCHAR virt_cat[] = L"abcdefabcdef";
static const WCHAR virt_trim_start[] = L"bcdef";
static const WCHAR virt_trim_end[] = L"abcde";
static const WCHAR virt_char_a[] = L"a";
static const WCHAR virt_char_f[] = L"f";

static HSTRING make_handle( const void *payload, UINT32 tag )
{
    return (HSTRING)(UINT_PTR)((UINT_PTR)payload | tag);
}

static void init_managed( BYTE *p, const WCHAR *chars, UINT32 cch, WORD chars_offset )
{
    memset( p, 0, PAYLOAD_SIZE );
    *(void **)(p + 0x00) = MT_FILLER;
    *(UINT32 *)(p + 0x08) = cch;
    if (chars_offset == 0x10) *(WORD *)(p + 0x0C) = 'X'; /* the +0x0C probe must fail */
    memcpy( p + chars_offset, chars, (cch + 1) * sizeof(WCHAR) );
}

static void init_inline( BYTE *p, const WCHAR *chars, UINT32 cch )
{
    memset( p, 0, PAYLOAD_SIZE );
    *(UINT32 *)(p + 0x00) = cch;
    memcpy( p + 4, chars, (cch + 1) * sizeof(WCHAR) );
}

#define check_decoded(str, content, length, has_null) _check_decoded( __LINE__, str, content, length, has_null )
static void _check_decoded( int line, HSTRING str, LPCWSTR content, UINT32 length, BOOL has_null_expected )
{
    BOOL has_null = 0xaa;
    LPCWSTR ptr;
    UINT32 out_length = 0x55;

    ok_(__FILE__, line)( WindowsIsStringEmpty( str ) == (length == 0),
                         "line %d: WindowsIsStringEmpty(%p), length %u\n", line, str, length );
    ok_(__FILE__, line)( WindowsGetStringLen( str ) == length, "line %d: WindowsGetStringLen(%p) returned %u, expected %u\n",
                         line, str, WindowsGetStringLen( str ), length );
    ptr = WindowsGetStringRawBuffer( str, &out_length );
    ok_(__FILE__, line)( ptr != NULL, "line %d: WindowsGetStringRawBuffer(%p) returned NULL\n", line, str );
    if (!ptr) return;
    ok_(__FILE__, line)( out_length == length, "line %d: raw size %u, expected %u\n", line, out_length, length );
    ok_(__FILE__, line)( !memcmp( ptr, content, length * sizeof(WCHAR) ), "line %d: got raw buffer %s\n",
                         line, debugstr_wn( ptr, length ) );
    ok_(__FILE__, line)( ptr[length] == 0, "line %d: raw buffer is not NUL-terminated\n", line );
    ptr = WindowsGetStringRawBuffer( str, NULL );
    ok_(__FILE__, line)( ptr != NULL, "line %d: WindowsGetStringRawBuffer(%p, NULL) returned NULL\n", line, str );
    ok_(__FILE__, line)( WindowsStringHasEmbeddedNull( str, &has_null ) == S_OK,
                         "line %d: WindowsStringHasEmbeddedNull(%p) failed\n", line, str );
    ok_(__FILE__, line)( has_null == has_null_expected,
                         "line %d: got has_null %d, expected %d, content length %u\n", line, has_null, has_null_expected, length );
}

#define check_undecoded(str) _check_undecoded( __LINE__, str )
static void _check_undecoded( int line, HSTRING str )
{
    LPCWSTR ptr;
    UINT32 out_length = 0x55;
    INT32 res = 0x55;
    HSTRING plain;

    /* Undecodable handles must report the empty string (same as WindowsGetStringRawBuffer(NULL)
     * does): never a fabricated buffer, never a crash. */
    ok_(__FILE__, line)( WindowsGetStringLen( str ) == 0, "line %d: WindowsGetStringLen(%p) returned %u\n",
                         line, str, WindowsGetStringLen( str ) );
    ptr = WindowsGetStringRawBuffer( str, &out_length );
    ok_(__FILE__, line)( ptr != NULL, "line %d: raw buffer NULL\n", line );
    if (ptr)
    {
        ok_(__FILE__, line)( out_length == 0, "line %d: raw size %u\n", line, out_length );
        ok_(__FILE__, line)( ptr[0] == 0, "line %d: fabricated content %s\n", line, debugstr_w( ptr ) );
    }
    ok_(__FILE__, line)( WindowsIsStringEmpty( str ) == TRUE, "line %d: IsStringEmpty\n", line );
    ok(WindowsCreateString( virt_content, 6, &plain ) == S_OK, "Failed to create plain string\n");
    ok_(__FILE__, line)( WindowsCompareStringOrdinal( str, plain, &res ) == S_OK,
                         "line %d: WindowsCompareStringOrdinal(%p) failed\n", line, str );
    ok_(__FILE__, line)( res == -1, "line %d: got %d, expected -1\n", line, res );
    WindowsDeleteString( plain );
}

static void test_virtualized_hstring(void)
{
    HSTRING str, plain, dup, out;
    INT32 res;
    BYTE snapshot[PAYLOAD_SIZE];

    init_managed( managed_a, virt_content, 6, 0x0C );
    init_managed( managed_b, virt_content, 6, 0x10 );
    init_managed( managed_null, virt_null, 3, 0x0C );
    init_managed( managed_empty, L"", 0, 0x0C );
    init_inline( inline_ok, virt_content, 6 );
    init_inline( inline_null, virt_null, 3 );
    init_inline( inline_empty, L"", 0 );
    memset( managed_bad, 'x', PAYLOAD_SIZE );
    *(void **)(managed_bad + 0x00) = MT_FILLER;
    *(UINT32 *)(managed_bad + 0x08) = 6; /* no NUL at any probe offset */
    memset( managed_huge, 'x', PAYLOAD_SIZE );
    *(void **)(managed_huge + 0x00) = MT_FILLER;
    *(UINT32 *)(managed_huge + 0x08) = 0xffffffff;

    /* Tag 7, both documented managed char offsets. */
    str = make_handle( managed_a, 7 );
    check_decoded( str, virt_content, 6, FALSE );
    str = make_handle( managed_b, 7 );
    check_decoded( str, virt_content, 6, FALSE );

    /* Tag 2 inline buffer. */
    str = make_handle( inline_ok, 2 );
    check_decoded( str, virt_content, 6, FALSE );

    /* Embedded NUL is content, not a terminator: length 3, has_null TRUE. */
    str = make_handle( managed_null, 7 );
    check_decoded( str, virt_null, 3, TRUE );
    str = make_handle( inline_null, 2 );
    check_decoded( str, virt_null, 3, TRUE );

    /* Zero-length virtualized strings decode as empty. */
    str = make_handle( managed_empty, 7 );
    check_decoded( str, L"", 0, FALSE );
    str = make_handle( inline_empty, 2 );
    check_decoded( str, L"", 0, FALSE );

    /* Atoms, unknown tags, bad offsets and out-of-range lengths: report empty. */
    check_undecoded( (HSTRING)(UINT_PTR)0x1235 );        /* atom, payload not mapped */
    check_undecoded( make_handle( managed_bad, 7 ) );    /* no NUL terminator */
    check_undecoded( make_handle( managed_huge, 7 ) );   /* cch out of range */
    check_undecoded( make_handle( managed_a, 3 ) );      /* unknown tag */
    check_undecoded( make_handle( managed_a, 1 ) );      /* atom tag over a live payload */
    check_undecoded( (HSTRING)(UINT_PTR)1 );             /* null payload, tag 1 */
    check_undecoded( (HSTRING)(UINT_PTR)2 );             /* null payload, tag 2 */
    check_undecoded( (HSTRING)(UINT_PTR)7 );             /* null payload, tag 7 */

    /* Comparison must see the decoded content on both sides. */
    ok(WindowsCreateString( virt_content, 6, &plain ) == S_OK, "Failed to create plain string\n");
    str = make_handle( managed_a, 7 );
    res = 0x55;
    ok(WindowsCompareStringOrdinal( str, plain, &res ) == S_OK, "Failed to compare virtualized string\n");
    ok(!res, "got %d, expected 0\n", res );
    res = 0x55;
    ok(WindowsCompareStringOrdinal( plain, str, &res ) == S_OK, "Failed to compare virtualized string\n");
    ok(!res, "got %d, expected 0\n", res );
    res = 0x55;
    ok(WindowsCompareStringOrdinal( str, make_handle( managed_b, 7 ), &res ) == S_OK, "Failed to compare virtualized strings\n");
    ok(!res, "got %d, expected 0\n", res );
    res = 0x55;
    ok(WindowsCompareStringOrdinal( str, make_handle( inline_ok, 2 ), &res ) == S_OK, "Failed to compare virtualized strings\n");
    ok(!res, "got %d, expected 0\n", res );
    res = 0x55;
    ok(WindowsCompareStringOrdinal( str, NULL, &res ) == S_OK, "Failed to compare virtualized string\n");
    ok(res == 1, "got %d, expected 1\n", res );
    res = 0x55;
    ok(WindowsCompareStringOrdinal( NULL, str, &res ) == S_OK, "Failed to compare virtualized string\n");
    ok(res == -1, "got %d, expected -1\n", res );

    /* Content operations decode their virtualized inputs. */
    out = NULL;
    ok(WindowsSubstring( str, 2, &out ) == S_OK, "WindowsSubstring failed\n");
    check_string( out, virt_tail, 4, FALSE );
    ok(WindowsDeleteString( out ) == S_OK, "Failed to delete substring\n");
    out = NULL;
    ok(WindowsSubstringWithSpecifiedLength( str, 1, 3, &out ) == S_OK, "WindowsSubstringWithSpecifiedLength failed\n");
    check_string( out, L"bcd", 3, FALSE );
    ok(WindowsDeleteString( out ) == S_OK, "Failed to delete substring\n");
    out = NULL;
    ok(WindowsConcatString( str, plain, &out ) == S_OK, "WindowsConcatString failed\n");
    check_string( out, virt_cat, 12, FALSE );
    ok(WindowsDeleteString( out ) == S_OK, "Failed to delete concatenated string\n");
    ok(WindowsCreateString( virt_char_a, 1, &dup ) == S_OK, "Failed to create trim set\n");
    out = NULL;
    ok(WindowsTrimStringStart( str, dup, &out ) == S_OK, "WindowsTrimStringStart failed\n");
    check_string( out, virt_trim_start, 5, FALSE );
    ok(WindowsDeleteString( out ) == S_OK, "Failed to delete trimmed string\n");
    WindowsDeleteString( dup );
    ok(WindowsCreateString( virt_char_f, 1, &dup ) == S_OK, "Failed to create trim set\n");
    out = NULL;
    ok(WindowsTrimStringEnd( str, dup, &out ) == S_OK, "WindowsTrimStringEnd failed\n");
    check_string( out, virt_trim_end, 5, FALSE );
    ok(WindowsDeleteString( out ) == S_OK, "Failed to delete trimmed string\n");
    WindowsDeleteString( dup );
    out = NULL;
    ok(WindowsDuplicateString( str, &dup ) == S_OK, "WindowsDuplicateString failed\n");
    ok(dup == str, "got %p, expected %p\n", dup, str );
    check_decoded( dup, virt_content, 6, FALSE );
    ok(WindowsDeleteString( dup ) == S_OK, "Failed to delete duplicate\n");
    WindowsDeleteString( plain );

    /* Deleting a virtualized handle must be a successful no-op: the payloads above live in
     * static (non-heap) memory, so any free() would corrupt the allocator, and any write
     * would show up in the snapshot compare below. */
    str = make_handle( managed_a, 7 );
    memcpy( snapshot, managed_a, PAYLOAD_SIZE );
    ok(WindowsDeleteString( str ) == S_OK, "WindowsDeleteString(tag 7) failed\n");
    ok(WindowsDeleteString( str ) == S_OK, "second WindowsDeleteString(tag 7) failed\n");
    ok(!memcmp( snapshot, managed_a, PAYLOAD_SIZE ), "tag 7 payload was modified or freed\n");
    str = make_handle( inline_ok, 2 );
    memcpy( snapshot, inline_ok, PAYLOAD_SIZE );
    ok(WindowsDeleteString( str ) == S_OK, "WindowsDeleteString(tag 2) failed\n");
    ok(!memcmp( snapshot, inline_ok, PAYLOAD_SIZE ), "tag 2 payload was modified or freed\n");
    ok(WindowsDeleteString( (HSTRING)(UINT_PTR)0x1235 ) == S_OK, "WindowsDeleteString(atom) failed\n");
    dup = NULL;
    ok(WindowsDuplicateString( (HSTRING)(UINT_PTR)0x1235, &dup ) == S_OK, "WindowsDuplicateString(atom) failed\n");
    ok(dup == (HSTRING)(UINT_PTR)0x1235, "got %p, expected the atom handle unchanged\n", dup );
    ok(WindowsDeleteString( dup ) == S_OK, "Failed to delete the duplicated atom handle\n");

    /* A heap allocation after all of the above proves the allocator survived the deletes. */
    ok(WindowsCreateString( virt_content, 6, &plain ) == S_OK, "Allocator corrupted by deleting virtualized handles\n");
    check_decoded( plain, virt_content, 6, FALSE );
    ok(WindowsDeleteString( plain ) == S_OK, "Failed to delete plain string\n");
}

START_TEST(string)
{
    test_create_delete();
    test_duplicate();
    test_access();
    test_string_buffer();
    test_substring();
    test_concat();
    test_compare();
    test_trim();
    test_hstring_struct();
    test_marshal();
    test_virtualized_hstring();
}
