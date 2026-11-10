/*
 * Copyright 2014 Martin Storsjo
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

#include <string.h>
#include <wchar.h>

#include "windows.h"
#include "winerror.h"
#include "hstring.h"
#include "wine/debug.h"
#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(winstring);

#define HSTRING_REFERENCE_FLAG 1
#define HSTRING_INLINE_LIMIT   24

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
    WCHAR buffer[HSTRING_INLINE_LIMIT + 1];
};

static const WCHAR empty[1];

C_ASSERT(sizeof(struct hstring_header) <= sizeof(HSTRING_HEADER));
C_ASSERT(offsetof(struct hstring_private, header) == 0);
C_ASSERT(offsetof(struct hstring_private, refcount) == sizeof(struct hstring_header));
C_ASSERT(offsetof(struct hstring_private, buffer) == sizeof(struct hstring_header) + sizeof(LONG));

static inline struct hstring_private *impl_from_HSTRING(HSTRING string)
{
    return (struct hstring_private *)string;
}

static inline struct hstring_private *impl_from_HSTRING_HEADER(HSTRING_HEADER *header)
{
    return CONTAINING_RECORD(header, struct hstring_private, header);
}

static inline struct hstring_private *impl_from_HSTRING_BUFFER(HSTRING_BUFFER buffer)
{
    return CONTAINING_RECORD(buffer, struct hstring_private, buffer);
}

/*
 * Virtualized HSTRINGs.
 *
 * Windows' winstring handles carry a type tag in the low 3 bits, the payload
 * being 8-byte aligned at (handle & ~7).  Handles handed out by this file are
 * malloc'ed, so their low bits are always 0 (tag 0 = our own hstring_private).
 * WinRT virtualized-string producers pass tagged handles straight through as
 * HSTRING arguments: .NET Native / CoreRT AOT code forwards managed GC
 * strings directly (observed with IMapView<HSTRING,HSTRING>::Get keys, which
 * produced "Lookup (null)" -> ELEMENT_NOT_FOUND -> failfast c0000602):
 *
 *   0 = allocated handle (struct hstring_private, our own heap)
 *   1 = atom-table string: the payload is an atom index, not a pointer.  This
 *       tree keeps no winstring atom table, so the content cannot be
 *       resolved: never dereference the index, never free it, never
 *       fabricate a buffer.
 *   2 = inline buffer {UINT32 cch; WCHAR chars[cch+1]} at (handle & ~7)
 *   7 = managed string object at (handle & ~7): x64 GC ABI MethodTable*@+0x00,
 *       int32 cch@+0x08, chars inline right after the length -- coreclr puts
 *       m_firstChar at +0x0C, the RTString layout note at +0x10.  Probe both
 *       and adopt the offset whose chars[cch] NUL-terminates: offsets proven
 *       at runtime, not assumed.
 *
 * Every public entry decodes through hstring_get_data() below, so it is the
 * single source of truth.  Virtualized handles are not heap-owned:
 * WindowsDeleteString must not free them and WindowsDuplicateString returns
 * the handle unchanged.  Internal to combase (other combase HSTRING users --
 * roapi.c, usrmarshal.c -- go through these public entries).
 */
#define HSTRING_TAG_MASK    7
#define HSTRING_TAG_ATOM    1
#define HSTRING_TAG_BUFFER  2
#define HSTRING_TAG_MANAGED 7

/* Sanity bound against wild lengths only: the NUL-terminator probe is what
 * actually proves the payload, so virtualized strings are not capped to the
 * short lengths the probe alone would tolerate. */
#define HSTRING_MAX_DECODE_LEN 0x1000000

static inline BOOL hstring_is_virtualized( HSTRING str )
{
    return (UINT_PTR)str & HSTRING_TAG_MASK;
}

static BOOL decode_virtualized_hstring( HSTRING str, const WCHAR **buffer, UINT32 *length )
{
    static const WORD managed_offsets[] = { 0x0C, 0x10 };
    UINT_PTR v = (UINT_PTR)str;
    UINT32 i, cch, tag = (UINT32)(v & HSTRING_TAG_MASK);
    const BYTE *addr = (const BYTE *)(v & ~(UINT_PTR)HSTRING_TAG_MASK);
    const WCHAR *decoded_buffer = NULL;
    UINT32 decoded_length = 0;
    BOOL decoded = FALSE;

    if (!addr) return FALSE;

    __TRY
    {
        if (tag == HSTRING_TAG_BUFFER)
        {
            cch = *(const UINT32 *)addr;
            if (cch <= HSTRING_MAX_DECODE_LEN && !*(const WCHAR *)(addr + 4 + 2 * (SIZE_T)cch))
            {
                decoded_buffer = (const WCHAR *)(addr + 4);
                decoded_length = cch;
                decoded = TRUE;
            }
        }
        else if (tag == HSTRING_TAG_MANAGED)
        {
            cch = *(const UINT32 *)(addr + 8);
            if (cch <= HSTRING_MAX_DECODE_LEN)
            {
                for (i = 0; i < ARRAYSIZE( managed_offsets ); i++)
                {
                    const WCHAR *chars = (const WCHAR *)(addr + managed_offsets[i]);
                    if (!chars[cch])
                    {
                        decoded_buffer = chars;
                        decoded_length = cch;
                        decoded = TRUE;
                        break;
                    }
                }
            }
        }
    }
    __EXCEPT_ALL
    {
        decoded = FALSE; /* wild handle: report it, never fabricate content */
    }
    __ENDTRY

    if (decoded)
    {
        if (buffer) *buffer = decoded_buffer;
        if (length) *length = decoded_length;
        return TRUE;
    }
    if (tag == HSTRING_TAG_ATOM) FIXME( "atom hstring %p: no atom table to resolve against\n", str );
    else TRACE( "hstring %p: undecodable, tag %u\n", str, tag );
    return FALSE;
}

/* Buffer + length for any HSTRING: allocated, reference, or virtualized.
 * Undecodable handles report the empty string -- the same non-NULL
 * NUL-terminated buffer WindowsGetStringRawBuffer gives for a NULL handle. */
static void hstring_get_data( HSTRING str, const WCHAR **buffer, UINT32 *length )
{
    struct hstring_private *priv;

    *buffer = empty;
    *length = 0;
    if (!str) return;
    if (hstring_is_virtualized( str ))
    {
        decode_virtualized_hstring( str, buffer, length );
        return;
    }
    priv = impl_from_HSTRING( str );
    *buffer = priv->header.str;
    *length = priv->header.length;
}

static BOOL alloc_string(UINT32 len, HSTRING *out)
{
    struct hstring_private *priv;
    SIZE_T size;

    if (len >= (~0U - sizeof(struct hstring_private)) / sizeof(WCHAR))
        return FALSE;

    if (len <= HSTRING_INLINE_LIMIT)
        size = sizeof(struct hstring_private);
    else
        size = offsetof(struct hstring_private, buffer[len + 1]);

    priv = malloc(size);
    if (!priv)
        return FALSE;

    priv->header.flags = 0;
    priv->header.length = len;
    priv->header.padding1 = 0;
    priv->header.padding2 = 0;
    priv->header.str = priv->buffer;

    priv->refcount = 1;
    priv->buffer[len] = '\0';

    *out = (HSTRING)priv;
    return TRUE;
}

/***********************************************************************
 *      WindowsCreateString (combase.@)
 */
HRESULT WINAPI WindowsCreateString(LPCWSTR ptr, UINT32 len,
                                   HSTRING *out)
{
    struct hstring_private *priv;

    TRACE("(%s, %u, %p)\n", debugstr_wn(ptr, len), len, out);

    if (out == NULL)
        return E_INVALIDARG;
    if (len == 0)
    {
        *out = NULL;
        return S_OK;
    }
    if (ptr == NULL)
        return E_POINTER;
    if (!alloc_string(len, out))
        return E_OUTOFMEMORY;
    priv = impl_from_HSTRING(*out);
    memcpy(priv->buffer, ptr, len * sizeof(*priv->buffer));
    return S_OK;
}

/***********************************************************************
 *      WindowsCreateStringReference (combase.@)
 */
HRESULT WINAPI WindowsCreateStringReference(LPCWSTR ptr, UINT32 len,
                                            HSTRING_HEADER *header, HSTRING *out)
{
    struct hstring_private *priv;

    TRACE("(%s, %u, %p, %p)\n", debugstr_wn(ptr, len), len, header, out);

    if (out == NULL || header == NULL)
        return E_INVALIDARG;
    /* The handle we hand out must stay tag-free, so the header has to be
     * 8-byte aligned (Windows requires the same of HSTRING_HEADER). */
    if ((UINT_PTR)header & HSTRING_TAG_MASK)
        return E_INVALIDARG;
    if (ptr != NULL && ptr[len] != '\0')
        return E_INVALIDARG;
    if (len == 0)
    {
        *out = NULL;
        return S_OK;
    }
    if (ptr == NULL)
        return E_POINTER;

    priv = impl_from_HSTRING_HEADER(header);
    priv->header.flags = HSTRING_REFERENCE_FLAG;
    priv->header.length = len;
    priv->header.padding1 = 0;
    priv->header.padding2 = 0;
    priv->header.str = ptr;

    *out = (HSTRING)priv;
    return S_OK;
}

/***********************************************************************
 *      WindowsDeleteString (combase.@)
 */
HRESULT WINAPI WindowsDeleteString(HSTRING str)
{
    struct hstring_private *priv;

    TRACE("(%p)\n", str);

    if (str == NULL)
        return S_OK;
    if (hstring_is_virtualized(str))
        return S_OK; /* virtualized handles are not heap-owned: nothing to release */
    priv = impl_from_HSTRING(str);
    if (priv->header.flags & HSTRING_REFERENCE_FLAG)
        return S_OK;
    if (InterlockedDecrement(&priv->refcount) == 0)
        free(priv);
    return S_OK;
}

/***********************************************************************
 *      WindowsDuplicateString (combase.@)
 */
HRESULT WINAPI WindowsDuplicateString(HSTRING str, HSTRING *out)
{
    struct hstring_private *priv;

    TRACE("(%p, %p)\n", str, out);

    if (out == NULL)
        return E_INVALIDARG;
    if (str == NULL)
    {
        *out = NULL;
        return S_OK;
    }
    if (hstring_is_virtualized(str))
    {
        *out = str; /* the handle carries its content: no refcount to bump */
        return S_OK;
    }
    priv = impl_from_HSTRING(str);
    if (priv->header.flags & HSTRING_REFERENCE_FLAG)
        return WindowsCreateString(priv->header.str, priv->header.length, out);
    InterlockedIncrement(&priv->refcount);
    *out = str;
    return S_OK;
}

/***********************************************************************
 *      WindowsPreallocateStringBuffer (combase.@)
 */
HRESULT WINAPI WindowsPreallocateStringBuffer(UINT32 len, WCHAR **outptr,
                                              HSTRING_BUFFER *out)
{
    struct hstring_private *priv;
    HSTRING str;

    TRACE("(%u, %p, %p)\n", len, outptr, out);

    if (outptr == NULL || out == NULL)
        return E_POINTER;
    if (len == 0)
    {
        *outptr = (LPWSTR)empty;
        *out = NULL;
        return S_OK;
    }

    if (!alloc_string(len, &str))
        return E_OUTOFMEMORY;
    priv = impl_from_HSTRING(str);
    *outptr = priv->buffer;
    *out = (HSTRING_BUFFER)&priv->buffer;
    return S_OK;
}

/***********************************************************************
 *      WindowsDeleteStringBuffer (combase.@)
 */
HRESULT WINAPI WindowsDeleteStringBuffer(HSTRING_BUFFER buf)
{
    struct hstring_private *priv = NULL;

    TRACE("(%p)\n", buf);

    /* HSTRING_BUFFER is an interior pointer (&priv->buffer, offset 28 on x64),
     * not a tagged handle: no tag checks apply here. */
    if(buf)
        priv = impl_from_HSTRING_BUFFER(buf);

    return WindowsDeleteString((HSTRING)priv);
}

/***********************************************************************
 *      WindowsPromoteStringBuffer (combase.@)
 */
HRESULT WINAPI WindowsPromoteStringBuffer(HSTRING_BUFFER buf, HSTRING *out)
{
    struct hstring_private *priv;

    TRACE("(%p, %p)\n", buf, out);

    if (out == NULL)
        return E_POINTER;
    if (buf == NULL)
    {
        *out = NULL;
        return S_OK;
    }
    priv = impl_from_HSTRING_BUFFER(buf);
    if (priv->buffer[priv->header.length] != 0 || priv->header.flags & HSTRING_REFERENCE_FLAG || priv->refcount != 1)
        return E_INVALIDARG;
    *out = (HSTRING)priv;
    return S_OK;
}

/***********************************************************************
 *      WindowsGetStringLen (combase.@)
 */
UINT32 WINAPI WindowsGetStringLen(HSTRING str)
{
    struct hstring_private *priv;
    UINT32 length;

    TRACE("(%p)\n", str);

    if (!str)
        return 0;
    if (hstring_is_virtualized(str))
    {
        if (decode_virtualized_hstring(str, NULL, &length))
            return length;
        return 0;
    }
    priv = impl_from_HSTRING(str);
    return priv->header.length;
}

/***********************************************************************
 *      WindowsGetStringRawBuffer (combase.@)
 */
LPCWSTR WINAPI WindowsGetStringRawBuffer(HSTRING str, UINT32 *len)
{
    struct hstring_private *priv;
    const WCHAR *buffer;
    UINT32 length;

    TRACE("(%p, %p)\n", str, len);

    if (!str)
    {
        if (len)
            *len = 0;
        return empty;
    }
    if (hstring_is_virtualized(str))
    {
        if (decode_virtualized_hstring(str, &buffer, &length))
        {
            if (len)
                *len = length;
            return buffer;
        }
        if (len)
            *len = 0;
        return empty;
    }
    priv = impl_from_HSTRING(str);
    if (len)
        *len = priv->header.length;
    return priv->header.str;
}

/***********************************************************************
 *      WindowsStringHasEmbeddedNull (combase.@)
 */
HRESULT WINAPI WindowsStringHasEmbeddedNull(HSTRING str, BOOL *out)
{
    struct hstring_private *priv;
    const WCHAR *buffer;
    UINT32 length;

    TRACE("(%p, %p)\n", str, out);

    if (out == NULL)
        return E_INVALIDARG;
    if (!str)
    {
        *out = FALSE;
        return S_OK;
    }
    if (hstring_is_virtualized(str))
    {
        hstring_get_data(str, &buffer, &length);
        *out = (wmemchr(buffer, L'\0', length) != NULL);
        return S_OK;
    }
    priv = impl_from_HSTRING(str);
    *out = (wmemchr(priv->header.str, L'\0', priv->header.length) != NULL);
    return S_OK;
}

/***********************************************************************
 *      WindowsSubstring (combase.@)
 */
HRESULT WINAPI WindowsSubstring(HSTRING str, UINT32 start, HSTRING *out)
{
    const WCHAR *buffer;
    UINT32 len;

    TRACE("(%p, %u, %p)\n", str, start, out);

    if (out == NULL)
        return E_INVALIDARG;
    hstring_get_data(str, &buffer, &len);
    if (start > len)
        return E_BOUNDS;
    if (start == len)
    {
        *out = NULL;
        return S_OK;
    }
    return WindowsCreateString(&buffer[start], len - start, out);
}

/***********************************************************************
 *      WindowsSubstringWithSpecifiedLength (combase.@)
 */
HRESULT WINAPI WindowsSubstringWithSpecifiedLength(HSTRING str, UINT32 start, UINT32 len, HSTRING *out)
{
    const WCHAR *buffer;
    UINT32 str_len;

    TRACE("(%p, %u, %u, %p)\n", str, start, len, out);

    if (out == NULL)
        return E_INVALIDARG;
    hstring_get_data(str, &buffer, &str_len);
    if (start + len < start ||
        start + len > str_len)
        return E_BOUNDS;
    if (len == 0)
    {
        *out = NULL;
        return S_OK;
    }
    return WindowsCreateString(&buffer[start], len, out);
}

/***********************************************************************
 *      WindowsConcatString (combase.@)
 */
HRESULT WINAPI WindowsConcatString(HSTRING str1, HSTRING str2, HSTRING *out)
{
    const WCHAR *buf1, *buf2;
    UINT32 len1, len2;
    struct hstring_private *priv;

    TRACE("(%p, %p, %p)\n", str1, str2, out);

    if (out == NULL)
        return E_INVALIDARG;
    if (str1 == NULL)
        return WindowsDuplicateString(str2, out);
    if (str2 == NULL)
        return WindowsDuplicateString(str1, out);
    hstring_get_data(str1, &buf1, &len1);
    hstring_get_data(str2, &buf2, &len2);
    if (!len1 && !len2)
    {
        *out = NULL;
        return S_OK;
    }
    if (!len1)
        return WindowsDuplicateString(str2, out);
    if (!len2)
        return WindowsDuplicateString(str1, out);
    if (len1 > ~0U - len2)
        return E_OUTOFMEMORY;
    if (!alloc_string(len1 + len2, out))
        return E_OUTOFMEMORY;
    priv = impl_from_HSTRING(*out);
    memcpy(priv->buffer, buf1, len1 * sizeof(*priv->buffer));
    memcpy(priv->buffer + len1, buf2, len2 * sizeof(*priv->buffer));
    return S_OK;
}

/***********************************************************************
 *      WindowsIsStringEmpty (combase.@)
 */
BOOL WINAPI WindowsIsStringEmpty(HSTRING str)
{
    struct hstring_private *priv;
    UINT32 length;

    TRACE("(%p)\n", str);

    if (!str)
        return TRUE;
    if (hstring_is_virtualized(str))
    {
        if (decode_virtualized_hstring(str, NULL, &length))
            return length == 0;
        return TRUE;
    }
    priv = impl_from_HSTRING(str);
    return priv->header.length == 0;
}

/***********************************************************************
 *      WindowsCompareStringOrdinal (combase.@)
 */
HRESULT WINAPI WindowsCompareStringOrdinal(HSTRING str1, HSTRING str2, INT32 *res)
{
    const WCHAR *buf1, *buf2;
    UINT32 len1, len2;

    TRACE("(%p, %p, %p)\n", str1, str2, res);

    if (res == NULL)
        return E_INVALIDARG;
    if (str1 == str2)
    {
        *res = 0;
        return S_OK;
    }
    hstring_get_data(str1, &buf1, &len1);
    hstring_get_data(str2, &buf2, &len2);
    *res = CompareStringOrdinal(buf1, len1, buf2, len2, FALSE) - CSTR_EQUAL;
    return S_OK;
}

/***********************************************************************
 *      WindowsTrimStringStart (combase.@)
 */
HRESULT WINAPI WindowsTrimStringStart(HSTRING str1, HSTRING str2, HSTRING *out)
{
    const WCHAR *buf1, *buf2;
    UINT32 len1, len2, start;

    TRACE("(%p, %p, %p)\n", str1, str2, out);

    if (!out || !str2)
        return E_INVALIDARG;
    hstring_get_data(str2, &buf2, &len2);
    if (!len2)
        return E_INVALIDARG;
    if (!str1)
    {
        *out = NULL;
        return S_OK;
    }
    hstring_get_data(str1, &buf1, &len1);
    for (start = 0; start < len1; start++)
    {
        if (!wmemchr(buf2, buf1[start], len2))
            break;
    }
    if (start == len1)
    {
        *out = NULL;
        return S_OK;
    }
    return start ? WindowsCreateString(&buf1[start], len1 - start, out) :
                   WindowsDuplicateString(str1, out);
}

/***********************************************************************
 *      WindowsTrimStringEnd (combase.@)
 */
HRESULT WINAPI WindowsTrimStringEnd(HSTRING str1, HSTRING str2, HSTRING *out)
{
    const WCHAR *buf1, *buf2;
    UINT32 len1, len2, len;

    TRACE("(%p, %p, %p)\n", str1, str2, out);

    if (!out || !str2)
        return E_INVALIDARG;
    hstring_get_data(str2, &buf2, &len2);
    if (!len2)
        return E_INVALIDARG;
    if (!str1)
    {
        *out = NULL;
        return S_OK;
    }
    hstring_get_data(str1, &buf1, &len1);
    for (len = len1; len > 0; len--)
    {
        if (!wmemchr(buf2, buf1[len - 1], len2))
            break;
    }
    if (len == 0)
    {
        *out = NULL;
        return S_OK;
    }
    return (len < len1) ? WindowsCreateString(buf1, len, out) :
                          WindowsDuplicateString(str1, out);
}
