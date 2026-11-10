/*
 * NTDLL wide-char functions
 *
 * Copyright 2000 Alexandre Julliard
 * Copyright 2000 Jon Griffiths
 * Copyright 2003 Thomas Mertes
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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winternl.h"
#include "ntdll_misc.h"

#ifdef __x86_64__
#include <immintrin.h>

static inline int has_avx2(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        unsigned int eax, ebx, ecx, edx;
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
        if ((ecx & (1 << 27)) && (ecx & (1 << 28))) /* OSXSAVE and AVX */
        {
            unsigned int xcr0_low, xcr0_high;
            __asm__ volatile ("xgetbv" : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0));
            if ((xcr0_low & 6) == 6) /* XMM (bit 1) and YMM (bit 2) state enabled by OS */
            {
                __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
                if (eax >= 7)
                {
                    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
                    cached = (ebx & (1 << 5)) != 0;
                    return cached;
                }
            }
        }
        cached = 0;
    }
    return cached;
}

__attribute__((target("avx2")))
static inline size_t avx2_wcslen( LPCWSTR str )
{
    const WCHAR *s = str;
    __m256i zero;

    if (((ULONG_PTR)s & 1) != 0)
    {
        while (*s) s++;
        return s - str;
    }

    zero = _mm256_setzero_si256();

    if (((ULONG_PTR)s & 0xfff) <= 0xfe0)
    {
        __m256i v = _mm256_loadu_si256( (const __m256i *)s );
        int mask = _mm256_movemask_epi8( _mm256_cmpeq_epi16( v, zero ) );
        if (mask)
            return (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
        s += 16;
    }

    while ((ULONG_PTR)s & 31)
    {
        if (!*s) return s - str;
        s++;
    }

    for (;;)
    {
        __m256i v = _mm256_load_si256( (const __m256i *)s );
        int mask = _mm256_movemask_epi8( _mm256_cmpeq_epi16( v, zero ) );
        if (mask)
            return (s - str) + ((size_t)__builtin_ctz( (unsigned int)mask ) >> 1);
        s += 16;
    }
}

static inline size_t sse2_wcslen( LPCWSTR str )
{
    const WCHAR *s = str;
    __m128i zero;

    if (((ULONG_PTR)s & 1) != 0)
    {
        while (*s) s++;
        return s - str;
    }

    zero = _mm_setzero_si128();

    if (((ULONG_PTR)s & 0xfff) <= 0xff0)
    {
        __m128i v = _mm_loadu_si128( (const __m128i *)s );
        int mask = _mm_movemask_epi8( _mm_cmpeq_epi16( v, zero ) );
        if (mask)
            return (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
        s += 8;
    }

    while ((ULONG_PTR)s & 15)
    {
        if (!*s) return s - str;
        s++;
    }

    for (;;)
    {
        __m128i v = _mm_load_si128( (const __m128i *)s );
        int mask = _mm_movemask_epi8( _mm_cmpeq_epi16( v, zero ) );
        if (mask)
            return (s - str) + ((size_t)__builtin_ctz( (unsigned int)mask ) >> 1);
        s += 8;
    }
}

__attribute__((target("avx2")))
static inline size_t avx2_wcsnlen( const WCHAR *str, size_t len )
{
    const WCHAR *s = str;
    __m256i zero;

    if (((ULONG_PTR)s & 1) != 0 || len < 16)
    {
        while (len && *s) { s++; len--; }
        return s - str;
    }

    zero = _mm256_setzero_si256();

    if (((ULONG_PTR)s & 0xfff) <= 0xfe0)
    {
        __m256i v = _mm256_loadu_si256( (const __m256i *)s );
        int mask = _mm256_movemask_epi8( _mm256_cmpeq_epi16( v, zero ) );
        if (mask)
        {
            size_t idx = (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
            return idx < len ? idx : len;
        }
        s += 16;
        len -= 16;
    }

    while (((ULONG_PTR)s & 31) && len)
    {
        if (!*s) return s - str;
        s++;
        len--;
    }

    while (len >= 16)
    {
        __m256i v = _mm256_load_si256( (const __m256i *)s );
        int mask = _mm256_movemask_epi8( _mm256_cmpeq_epi16( v, zero ) );
        if (mask)
        {
            size_t idx = (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
            return (s - str) + idx;
        }
        s += 16;
        len -= 16;
    }

    while (len && *s) { s++; len--; }
    return s - str;
}

static inline size_t sse2_wcsnlen( const WCHAR *str, size_t len )
{
    const WCHAR *s = str;
    __m128i zero;

    if (((ULONG_PTR)s & 1) != 0 || len < 8)
    {
        while (len && *s) { s++; len--; }
        return s - str;
    }

    zero = _mm_setzero_si128();

    if (((ULONG_PTR)s & 0xfff) <= 0xff0)
    {
        __m128i v = _mm_loadu_si128( (const __m128i *)s );
        int mask = _mm_movemask_epi8( _mm_cmpeq_epi16( v, zero ) );
        if (mask)
        {
            size_t idx = (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
            return idx < len ? idx : len;
        }
        s += 8;
        len -= 8;
    }

    while (((ULONG_PTR)s & 15) && len)
    {
        if (!*s) return s - str;
        s++;
        len--;
    }

    while (len >= 8)
    {
        __m128i v = _mm_load_si128( (const __m128i *)s );
        int mask = _mm_movemask_epi8( _mm_cmpeq_epi16( v, zero ) );
        if (mask)
        {
            size_t idx = (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
            return (s - str) + idx;
        }
        s += 8;
        len -= 8;
    }

    while (len && *s) { s++; len--; }
    return s - str;
}

__attribute__((target("avx2")))
static inline LPWSTR avx2_wcschr( LPCWSTR str, WCHAR ch )
{
    const WCHAR *s = str;
    __m256i target;
    __m256i zero;

    if (!ch) return (WCHAR *)str + avx2_wcslen( str );

    if (((ULONG_PTR)s & 1) != 0)
    {
        do { if (*s == ch) return (WCHAR *)(ULONG_PTR)s; } while (*s++);
        return NULL;
    }

    target = _mm256_set1_epi16( ch );
    zero = _mm256_setzero_si256();

    if (((ULONG_PTR)s & 0xfff) <= 0xfe0)
    {
        __m256i v = _mm256_loadu_si256( (const __m256i *)s );
        __m256i eq_ch = _mm256_cmpeq_epi16( v, target );
        __m256i eq_zero = _mm256_cmpeq_epi16( v, zero );
        int mask = _mm256_movemask_epi8( _mm256_or_si256( eq_ch, eq_zero ) );
        if (mask)
        {
            int mask_ch = _mm256_movemask_epi8( eq_ch );
            int mask_zero = _mm256_movemask_epi8( eq_zero );
            if (mask_ch && (!mask_zero || __builtin_ctz((unsigned int)mask_ch) < __builtin_ctz((unsigned int)mask_zero)))
                return (WCHAR *)s + ((size_t)__builtin_ctz((unsigned int)mask_ch) >> 1);
            return NULL;
        }
        s += 16;
    }

    while ((ULONG_PTR)s & 31)
    {
        if (*s == ch) return (WCHAR *)s;
        if (!*s) return NULL;
        s++;
    }

    for (;;)
    {
        __m256i v = _mm256_load_si256( (const __m256i *)s );
        __m256i eq_ch = _mm256_cmpeq_epi16( v, target );
        __m256i eq_zero = _mm256_cmpeq_epi16( v, zero );
        int mask = _mm256_movemask_epi8( _mm256_or_si256( eq_ch, eq_zero ) );
        if (mask)
        {
            int mask_ch = _mm256_movemask_epi8( eq_ch );
            int mask_zero = _mm256_movemask_epi8( eq_zero );
            if (mask_ch && (!mask_zero || __builtin_ctz((unsigned int)mask_ch) < __builtin_ctz((unsigned int)mask_zero)))
                return (WCHAR *)s + ((size_t)__builtin_ctz((unsigned int)mask_ch) >> 1);
            return NULL;
        }
        s += 16;
    }
}

static inline LPWSTR sse2_wcschr( LPCWSTR str, WCHAR ch )
{
    const WCHAR *s = str;
    __m128i target;
    __m128i zero;

    if (!ch) return (WCHAR *)str + sse2_wcslen( str );

    if (((ULONG_PTR)s & 1) != 0)
    {
        do { if (*s == ch) return (WCHAR *)(ULONG_PTR)s; } while (*s++);
        return NULL;
    }

    target = _mm_set1_epi16( ch );
    zero = _mm_setzero_si128();

    if (((ULONG_PTR)s & 0xfff) <= 0xff0)
    {
        __m128i v = _mm_loadu_si128( (const __m128i *)s );
        __m128i eq_ch = _mm_cmpeq_epi16( v, target );
        __m128i eq_zero = _mm_cmpeq_epi16( v, zero );
        int mask = _mm_movemask_epi8( _mm_or_si128( eq_ch, eq_zero ) );
        if (mask)
        {
            int mask_ch = _mm_movemask_epi8( eq_ch );
            int mask_zero = _mm_movemask_epi8( eq_zero );
            if (mask_ch && (!mask_zero || __builtin_ctz((unsigned int)mask_ch) < __builtin_ctz((unsigned int)mask_zero)))
                return (WCHAR *)s + ((size_t)__builtin_ctz((unsigned int)mask_ch) >> 1);
            return NULL;
        }
        s += 8;
    }

    while ((ULONG_PTR)s & 15)
    {
        if (*s == ch) return (WCHAR *)s;
        if (!*s) return NULL;
        s++;
    }

    for (;;)
    {
        __m128i v = _mm_load_si128( (const __m128i *)s );
        __m128i eq_ch = _mm_cmpeq_epi16( v, target );
        __m128i eq_zero = _mm_cmpeq_epi16( v, zero );
        int mask = _mm_movemask_epi8( _mm_or_si128( eq_ch, eq_zero ) );
        if (mask)
        {
            int mask_ch = _mm_movemask_epi8( eq_ch );
            int mask_zero = _mm_movemask_epi8( eq_zero );
            if (mask_ch && (!mask_zero || __builtin_ctz((unsigned int)mask_ch) < __builtin_ctz((unsigned int)mask_zero)))
                return (WCHAR *)s + ((size_t)__builtin_ctz((unsigned int)mask_ch) >> 1);
            return NULL;
        }
        s += 8;
    }
}

__attribute__((target("avx2")))
static inline LPWSTR avx2_wcsrchr( LPCWSTR str, WCHAR ch )
{
    size_t len;
    const WCHAR *p;
    __m256i target;

    if (((ULONG_PTR)str & 1) != 0)
    {
        WCHAR *ret = NULL;
        do { if (*str == ch) ret = (WCHAR *)(ULONG_PTR)str; } while (*str++);
        return ret;
    }

    len = avx2_wcslen( str );
    if (!ch) return (WCHAR *)str + len;
    if (!len) return NULL;

    p = str + len;
    target = _mm256_set1_epi16( ch );

    while (p >= str + 16)
    {
        __m256i v;
        int mask;
        p -= 16;
        v = _mm256_loadu_si256( (const __m256i *)p );
        mask = _mm256_movemask_epi8( _mm256_cmpeq_epi16( v, target ) );
        if (mask)
            return (WCHAR *)p + ((size_t)(31 - __builtin_clz( (unsigned int)mask )) >> 1);
    }

    while (p > str)
    {
        p--;
        if (*p == ch) return (WCHAR *)p;
    }
    return NULL;
}

static inline LPWSTR sse2_wcsrchr( LPCWSTR str, WCHAR ch )
{
    size_t len;
    const WCHAR *p;
    __m128i target;

    if (((ULONG_PTR)str & 1) != 0)
    {
        WCHAR *ret = NULL;
        do { if (*str == ch) ret = (WCHAR *)(ULONG_PTR)str; } while (*str++);
        return ret;
    }

    len = sse2_wcslen( str );
    if (!ch) return (WCHAR *)str + len;
    if (!len) return NULL;

    p = str + len;
    target = _mm_set1_epi16( ch );

    while (p >= str + 8)
    {
        __m128i v;
        int mask;
        p -= 8;
        v = _mm_loadu_si128( (const __m128i *)p );
        mask = _mm_movemask_epi8( _mm_cmpeq_epi16( v, target ) );
        if (mask)
            return (WCHAR *)p + ((size_t)(31 - __builtin_clz( (unsigned int)mask )) >> 1);
    }

    while (p > str)
    {
        p--;
        if (*p == ch) return (WCHAR *)p;
    }
    return NULL;
}

__attribute__((target("avx2")))
static inline int avx2_wcscmp( LPCWSTR str1, LPCWSTR str2 )
{
    __m256i zero;

    if (str1 == str2) return 0;

    if ((((ULONG_PTR)str1 | (ULONG_PTR)str2) & 1) != 0)
    {
        while (*str1 && *str1 == *str2) { str1++; str2++; }
        return (int)*str1 - (int)*str2;
    }

    zero = _mm256_setzero_si256();

    for (;;)
    {
        if (((ULONG_PTR)str1 & 0xfff) <= 0xfe0 && ((ULONG_PTR)str2 & 0xfff) <= 0xfe0)
        {
            __m256i v1 = _mm256_loadu_si256( (const __m256i *)str1 );
            __m256i v2 = _mm256_loadu_si256( (const __m256i *)str2 );
            __m256i eq = _mm256_cmpeq_epi16( v1, v2 );
            __m256i z = _mm256_cmpeq_epi16( v1, zero );
            int eq_mask = _mm256_movemask_epi8( eq );
            int z_mask = _mm256_movemask_epi8( z );

            if (eq_mask == (int)0xffffffff && z_mask == 0)
            {
                str1 += 16;
                str2 += 16;
                continue;
            }
            break;
        }

        if (*str1 != *str2 || !*str1)
            return (int)*str1 - (int)*str2;
        str1++;
        str2++;
    }

    while (*str1 && *str1 == *str2) { str1++; str2++; }
    return (int)*str1 - (int)*str2;
}

static inline int sse2_wcscmp( LPCWSTR str1, LPCWSTR str2 )
{
    __m128i zero;

    if (str1 == str2) return 0;

    if ((((ULONG_PTR)str1 | (ULONG_PTR)str2) & 1) != 0)
    {
        while (*str1 && *str1 == *str2) { str1++; str2++; }
        return (int)*str1 - (int)*str2;
    }

    zero = _mm_setzero_si128();

    for (;;)
    {
        if (((ULONG_PTR)str1 & 0xfff) <= 0xff0 && ((ULONG_PTR)str2 & 0xfff) <= 0xff0)
        {
            __m128i v1 = _mm_loadu_si128( (const __m128i *)str1 );
            __m128i v2 = _mm_loadu_si128( (const __m128i *)str2 );
            __m128i eq = _mm_cmpeq_epi16( v1, v2 );
            __m128i z = _mm_cmpeq_epi16( v1, zero );
            int eq_mask = _mm_movemask_epi8( eq );
            int z_mask = _mm_movemask_epi8( z );

            if (eq_mask == 0xffff && z_mask == 0)
            {
                str1 += 8;
                str2 += 8;
                continue;
            }
            break;
        }

        if (*str1 != *str2 || !*str1)
            return (int)*str1 - (int)*str2;
        str1++;
        str2++;
    }

    while (*str1 && *str1 == *str2) { str1++; str2++; }
    return (int)*str1 - (int)*str2;
}

__attribute__((target("avx2")))
static inline int avx2_wcsncmp( LPCWSTR str1, LPCWSTR str2, size_t n )
{
    __m256i zero;

    if (!n || str1 == str2) return 0;

    if ((((ULONG_PTR)str1 | (ULONG_PTR)str2) & 1) != 0)
    {
        while (n > 0)
        {
            if (--n == 0 || *str1 != *str2 || !*str1)
                return (int)*str1 - (int)*str2;
            str1++;
            str2++;
        }
        return 0;
    }

    zero = _mm256_setzero_si256();

    while (n >= 16)
    {
        if (((ULONG_PTR)str1 & 0xfff) <= 0xfe0 && ((ULONG_PTR)str2 & 0xfff) <= 0xfe0)
        {
            __m256i v1 = _mm256_loadu_si256( (const __m256i *)str1 );
            __m256i v2 = _mm256_loadu_si256( (const __m256i *)str2 );
            __m256i eq = _mm256_cmpeq_epi16( v1, v2 );
            __m256i z = _mm256_cmpeq_epi16( v1, zero );
            int eq_mask = _mm256_movemask_epi8( eq );
            int z_mask = _mm256_movemask_epi8( z );

            if (eq_mask == (int)0xffffffff && z_mask == 0)
            {
                str1 += 16;
                str2 += 16;
                n -= 16;
                continue;
            }
            break;
        }

        if (*str1 != *str2 || !*str1 || --n == 0)
            return (int)*str1 - (int)*str2;
        str1++;
        str2++;
    }

    while (n > 0)
    {
        if (--n == 0 || *str1 != *str2 || !*str1)
            return (int)*str1 - (int)*str2;
        str1++;
        str2++;
    }
    return 0;
}

static inline int sse2_wcsncmp( LPCWSTR str1, LPCWSTR str2, size_t n )
{
    __m128i zero;

    if (!n || str1 == str2) return 0;

    if ((((ULONG_PTR)str1 | (ULONG_PTR)str2) & 1) != 0)
    {
        while (n > 0)
        {
            if (--n == 0 || *str1 != *str2 || !*str1)
                return (int)*str1 - (int)*str2;
            str1++;
            str2++;
        }
        return 0;
    }

    zero = _mm_setzero_si128();

    while (n >= 8)
    {
        if (((ULONG_PTR)str1 & 0xfff) <= 0xff0 && ((ULONG_PTR)str2 & 0xfff) <= 0xff0)
        {
            __m128i v1 = _mm_loadu_si128( (const __m128i *)str1 );
            __m128i v2 = _mm_loadu_si128( (const __m128i *)str2 );
            __m128i eq = _mm_cmpeq_epi16( v1, v2 );
            __m128i z = _mm_cmpeq_epi16( v1, zero );
            int eq_mask = _mm_movemask_epi8( eq );
            int z_mask = _mm_movemask_epi8( z );

            if (eq_mask == 0xffff && z_mask == 0)
            {
                str1 += 8;
                str2 += 8;
                n -= 8;
                continue;
            }
            break;
        }

        if (*str1 != *str2 || !*str1 || --n == 0)
            return (int)*str1 - (int)*str2;
        str1++;
        str2++;
    }

    while (n > 0)
    {
        if (--n == 0 || *str1 != *str2 || !*str1)
            return (int)*str1 - (int)*str2;
        str1++;
        str2++;
    }
    return 0;
}

__attribute__((target("avx2")))
static inline LPWSTR avx2_wcscpy( LPWSTR dst, LPCWSTR src )
{
    LPWSTR ret = dst;
    const __m256i zero = _mm256_setzero_si256();

    if ((((ULONG_PTR)src | (ULONG_PTR)dst) & 1) != 0)
    {
        WCHAR *p = dst;
        while ((*p++ = *src++));
        return ret;
    }

    while ((ULONG_PTR)src & 31)
    {
        if (!(*dst++ = *src++)) return ret;
    }

    for (;;)
    {
        __m256i chunk = _mm256_load_si256( (const __m256i *)src );
        int mask = _mm256_movemask_epi8( _mm256_cmpeq_epi16( chunk, zero ) );
        if (mask)
        {
            size_t idx, i;
            idx = (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
            for (i = 0; i <= idx; i++)
                dst[i] = src[i];
            return ret;
        }
        _mm256_storeu_si256( (__m256i *)dst, chunk );
        src += 16;
        dst += 16;
    }
}

static inline LPWSTR sse2_wcscpy( LPWSTR dst, LPCWSTR src )
{
    LPWSTR ret = dst;
    const __m128i zero = _mm_setzero_si128();

    if ((((ULONG_PTR)src | (ULONG_PTR)dst) & 1) != 0)
    {
        WCHAR *p = dst;
        while ((*p++ = *src++));
        return ret;
    }

    while ((ULONG_PTR)src & 15)
    {
        if (!(*dst++ = *src++)) return ret;
    }

    for (;;)
    {
        __m128i chunk = _mm_load_si128( (const __m128i *)src );
        int mask = _mm_movemask_epi8( _mm_cmpeq_epi16( chunk, zero ) );
        if (mask)
        {
            size_t idx, i;
            idx = (size_t)__builtin_ctz( (unsigned int)mask ) >> 1;
            for (i = 0; i <= idx; i++)
                dst[i] = src[i];
            return ret;
        }
        _mm_storeu_si128( (__m128i *)dst, chunk );
        src += 8;
        dst += 8;
    }
}
#endif

static const unsigned short wctypes[256] =
{
    /* 00 */
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0068, 0x0028, 0x0028, 0x0028, 0x0028, 0x0020, 0x0020,
    /* 10 */
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    /* 20 */
    0x0048, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    /* 30 */
    0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084, 0x0084,
    0x0084, 0x0084, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    /* 40 */
    0x0010, 0x0181, 0x0181, 0x0181, 0x0181, 0x0181, 0x0181, 0x0101,
    0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
    /* 50 */
    0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
    0x0101, 0x0101, 0x0101, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    /* 60 */
    0x0010, 0x0182, 0x0182, 0x0182, 0x0182, 0x0182, 0x0182, 0x0102,
    0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102,
    /* 70 */
    0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102,
    0x0102, 0x0102, 0x0102, 0x0010, 0x0010, 0x0010, 0x0010, 0x0020,
    /* 80 */
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    /* 90 */
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
    /* a0 */
    0x0048, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    /* b0 */
    0x0010, 0x0010, 0x0014, 0x0014, 0x0010, 0x0010, 0x0010, 0x0010,
    0x0010, 0x0014, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010, 0x0010,
    /* c0 */
    0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
    0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101,
    /* d0 */
    0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0010,
    0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0102,
    /* e0 */
    0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102,
    0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102,
    /* f0 */
    0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0010,
    0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102, 0x0102
};


/*********************************************************************
 *           _wcsicmp    (NTDLL.@)
 */
int __cdecl _wcsicmp( LPCWSTR str1, LPCWSTR str2 )
{
    for (;;)
    {
        WCHAR ch1 = (*str1 >= 'A' && *str1 <= 'Z') ? *str1 + 32 : *str1;
        WCHAR ch2 = (*str2 >= 'A' && *str2 <= 'Z') ? *str2 + 32 : *str2;
        if (ch1 != ch2 || !*str1) return ch1 - ch2;
        str1++;
        str2++;
    }
}


/*********************************************************************
 *           _wcslwr    (NTDLL.@)
 */
LPWSTR __cdecl _wcslwr( LPWSTR str )
{
    WCHAR *ret = str;
    for ( ; *str; str++) if (*str >= 'A' && *str <= 'Z') *str += 'a' - 'A';
    return ret;
}


/*********************************************************************
 *           _wcslwr_s    (NTDLL.@)
 */
errno_t __cdecl _wcslwr_s( wchar_t *str, size_t len )
{
    if (!str) return EINVAL;

    if (wcsnlen( str, len ) == len)
    {
        *str = 0;
        return EINVAL;
    }

    _wcslwr( str );
    return 0;
}


/*********************************************************************
 *           _wcsnicmp    (NTDLL.@)
 */
int __cdecl _wcsnicmp( LPCWSTR str1, LPCWSTR str2, size_t n )
{
    int ret = 0;
    for ( ; n > 0; n--, str1++, str2++)
    {
        WCHAR ch1 = (*str1 >= 'A' && *str1 <= 'Z') ? *str1 + 32 : *str1;
        WCHAR ch2 = (*str2 >= 'A' && *str2 <= 'Z') ? *str2 + 32 : *str2;
        if ((ret = ch1 - ch2) ||  !*str1) break;
    }
    return ret;
}


/*********************************************************************
 *           _wcsupr    (NTDLL.@)
 */
LPWSTR __cdecl _wcsupr( LPWSTR str )
{
    WCHAR *ret = str;

    for ( ; *str; str++) if (*str >= 'a' && *str <= 'z') *str += 'A' - 'a';
    return ret;
}


/*********************************************************************
 *           _wcsupr_s    (NTDLL.@)
 */
errno_t __cdecl _wcsupr_s( wchar_t *str, size_t len )
{
    if (!str) return EINVAL;

    if (wcsnlen( str, len ) == len)
    {
        *str = 0;
        return EINVAL;
    }

    _wcsupr( str );
    return 0;
}


/***********************************************************************
 *           wcscpy    (NTDLL.@)
 */
LPWSTR __cdecl wcscpy( LPWSTR dst, LPCWSTR src )
{
#ifdef __x86_64__
    if (has_avx2())
        return avx2_wcscpy( dst, src );
    return sse2_wcscpy( dst, src );
#else
    WCHAR *p = dst;
    while ((*p++ = *src++));
    return dst;
#endif
}


/*********************************************************************
 *           wcscpy_s    (NTDLL.@)
 */
errno_t __cdecl wcscpy_s( wchar_t *dst, size_t len, const wchar_t *src )
{
    size_t i;

    if (!dst || !len) return EINVAL;
    if (!src)
    {
        *dst = 0;
        return EINVAL;
    }

    for (i = 0; i < len; i++) if (!(dst[i] = src[i])) return 0;
    *dst = 0;
    return ERANGE;
}


/***********************************************************************
 *           wcslen    (NTDLL.@)
 */
size_t __cdecl wcslen( LPCWSTR str )
{
#ifdef __x86_64__
    if (has_avx2())
        return avx2_wcslen( str );
    return sse2_wcslen( str );
#else
    const WCHAR *s = str;
    while (*s) s++;
    return s - str;
#endif
}


/***********************************************************************
 *           wcscat    (NTDLL.@)
 */
LPWSTR __cdecl wcscat( LPWSTR dst, LPCWSTR src )
{
    wcscpy( dst + wcslen(dst), src );
    return dst;
}


/*********************************************************************
 *           wcscat_s    (NTDLL.@)
 */
errno_t __cdecl wcscat_s( wchar_t *dst, size_t len, const wchar_t *src )
{
    size_t i, j;

    if (!dst || !len) return EINVAL;
    if (!src)
    {
        *dst = 0;
        return EINVAL;
    }
    for (i = 0; i < len; i++) if (!dst[i]) break;
    for (j = 0; (j + i) < len; j++) if (!(dst[j + i] = src[j])) return 0;
    *dst = 0;
    return ERANGE;
}


/*********************************************************************
 *           wcschr    (NTDLL.@)
 */
LPWSTR __cdecl wcschr( LPCWSTR str, WCHAR ch )
{
#ifdef __x86_64__
    if (has_avx2())
        return avx2_wcschr( str, ch );
    return sse2_wcschr( str, ch );
#else
    do { if (*str == ch) return (WCHAR *)(ULONG_PTR)str; } while (*str++);
    return NULL;
#endif
}


/*********************************************************************
 *           wcscmp    (NTDLL.@)
 */
int __cdecl wcscmp( LPCWSTR str1, LPCWSTR str2 )
{
#ifdef __x86_64__
    if (has_avx2())
        return avx2_wcscmp( str1, str2 );
    return sse2_wcscmp( str1, str2 );
#else
    while (*str1 && (*str1 == *str2)) { str1++; str2++; }
    return *str1 - *str2;
#endif
}


/*********************************************************************
 *           wcscspn    (NTDLL.@)
 */
size_t __cdecl wcscspn( LPCWSTR str, LPCWSTR reject )
{
    const WCHAR *ptr;
    for (ptr = str; *ptr; ptr++) if (wcschr( reject, *ptr )) break;
    return ptr - str;
}


/*********************************************************************
 *           wcsncat    (NTDLL.@)
 */
LPWSTR __cdecl wcsncat( LPWSTR s1, LPCWSTR s2, size_t n )
{
    LPWSTR ret = s1;
    while (*s1) s1++;
    while (n-- > 0) if (!(*s1++ = *s2++)) return ret;
    *s1 = 0;
    return ret;
}


/*********************************************************************
 *           wcsncat_s    (NTDLL.@)
 */
errno_t __cdecl wcsncat_s( wchar_t *dst, size_t len, const wchar_t *src, size_t count )
{
    size_t i, j;

    if (!dst || !len) return EINVAL;
    if (!count) return 0;
    if (!src)
    {
        *dst = 0;
        return EINVAL;
    }

    for (i = 0; i < len; i++) if (!dst[i]) break;

    if (i == len)
    {
        *dst = 0;
        return EINVAL;
    }

    for (j = 0; (j + i) < len; j++)
    {
        if (count == _TRUNCATE && j + i == len - 1)
        {
            dst[j + i] = 0;
            return STRUNCATE;
        }
        if (j == count || !(dst[j + i] = src[j]))
        {
            dst[j + i] = 0;
            return 0;
        }
    }
    *dst = 0;
    return ERANGE;
}


/*********************************************************************
 *           wcsncmp    (NTDLL.@)
 */
int __cdecl wcsncmp( LPCWSTR str1, LPCWSTR str2, size_t n )
{
#ifdef __x86_64__
    if (has_avx2())
        return avx2_wcsncmp( str1, str2, n );
    return sse2_wcsncmp( str1, str2, n );
#else
    if (n <= 0) return 0;
    while ((--n > 0) && *str1 && (*str1 == *str2)) { str1++; str2++; }
    return *str1 - *str2;
#endif
}


/*********************************************************************
 *           wcsncpy    (NTDLL.@)
 */
#undef wcsncpy
LPWSTR __cdecl wcsncpy( LPWSTR s1, LPCWSTR s2, size_t n )
{
    size_t i;

    for (i = 0; i < n; i++) if(!(s1[i] = s2[i])) break;
    for (; i < n; i++) s1[i] = 0;
    return s1;
}


/*********************************************************************
 *           wcsncpy_s    (NTDLL.@)
 */
errno_t __cdecl wcsncpy_s( wchar_t *dst, size_t len, const wchar_t *src, size_t count )
{
    size_t i, end;

    if (!count)
    {
        if (dst && len) *dst = 0;
        return 0;
    }
    if (!dst || !len) return EINVAL;
    if (!src)
    {
        *dst = 0;
        return EINVAL;
    }

    if (count != _TRUNCATE && count < len)
        end = count;
    else
        end = len - 1;

    for (i = 0; i < end; i++)
        if (!(dst[i] = src[i])) return 0;

    if (count == _TRUNCATE)
    {
        dst[i] = 0;
        return STRUNCATE;
    }
    if (end == count)
    {
        dst[i] = 0;
        return 0;
    }
    dst[0] = 0;
    return ERANGE;
}


/*********************************************************************
 *           wcsnlen    (NTDLL.@)
 */
size_t __cdecl wcsnlen( const WCHAR *str, size_t len )
{
#ifdef __x86_64__
    if (has_avx2())
        return avx2_wcsnlen( str, len );
    return sse2_wcsnlen( str, len );
#else
    const WCHAR *s;
    for (s = str; len && *s; s++, len--) ;
    return s - str;
#endif
}


/*********************************************************************
 *           wcspbrk    (NTDLL.@)
 */
LPWSTR __cdecl wcspbrk( LPCWSTR str, LPCWSTR accept )
{
    for ( ; *str; str++) if (wcschr( accept, *str )) return (WCHAR *)(ULONG_PTR)str;
    return NULL;
}


/*********************************************************************
 *           wcsrchr    (NTDLL.@)
 */
LPWSTR __cdecl wcsrchr( LPCWSTR str, WCHAR ch )
{
#ifdef __x86_64__
    if (has_avx2())
        return avx2_wcsrchr( str, ch );
    return sse2_wcsrchr( str, ch );
#else
    WCHAR *ret = NULL;
    do { if (*str == ch) ret = (WCHAR *)(ULONG_PTR)str; } while (*str++);
    return ret;
#endif
}


/*********************************************************************
 *           wcsspn    (NTDLL.@)
 */
size_t __cdecl wcsspn( LPCWSTR str, LPCWSTR accept )
{
    const WCHAR *ptr;
    for (ptr = str; *ptr; ptr++) if (!wcschr( accept, *ptr )) break;
    return ptr - str;
}


/*********************************************************************
 *           wcsstr    (NTDLL.@)
 */
LPWSTR __cdecl wcsstr( LPCWSTR str, LPCWSTR sub )
{
    while (*str)
    {
        const WCHAR *p1 = str, *p2 = sub;
        while (*p1 && *p2 && *p1 == *p2) { p1++; p2++; }
        if (!*p2) return (WCHAR *)str;
        str++;
    }
    return NULL;
}


/*********************************************************************
 *           wcstok    (NTDLL.@)
 */
LPWSTR __cdecl wcstok( LPWSTR str, LPCWSTR delim )
{
    static LPWSTR next = NULL;
    LPWSTR ret;

    if (!str)
        if (!(str = next)) return NULL;

    while (*str && wcschr( delim, *str )) str++;
    if (!*str) return NULL;
    ret = str++;
    while (*str && !wcschr( delim, *str )) str++;
    if (*str) *str++ = 0;
    next = str;
    return ret;
}


/*********************************************************************
 *                  wcstok_s   (NTDLL.@)
 */
wchar_t * __cdecl wcstok_s( wchar_t *str, const wchar_t *delim, wchar_t **ctx )
{
    wchar_t *next;

    if (!delim || !ctx) return NULL;
    if (!str)
    {
        str = *ctx;
        if (!str) return NULL;
    }
    while (*str && wcschr( delim, *str )) str++;
    if (!*str)
    {
        *ctx = str;
        return NULL;
    }
    next = str + 1;
    while (*next && !wcschr( delim, *next )) next++;
    if (*next) *next++ = 0;
    *ctx = next;
    return str;
}


/*********************************************************************
 *           wcstombs    (NTDLL.@)
 */
size_t __cdecl wcstombs( char *dst, const WCHAR *src, size_t n )
{
    DWORD len;

    if (!dst)
    {
        RtlUnicodeToMultiByteSize( &len, src, wcslen(src) * sizeof(WCHAR) );
        return len;
    }
    else
    {
        if (n <= 0) return 0;
        RtlUnicodeToMultiByteN( dst, n, &len, src, wcslen(src) * sizeof(WCHAR) );
        if (len < n) dst[len] = 0;
    }
    return len;
}


/*********************************************************************
 *           mbstowcs    (NTDLL.@)
 */
size_t __cdecl mbstowcs( WCHAR *dst, const char *src, size_t n )
{
    DWORD len;

    if (!dst)
    {
        RtlMultiByteToUnicodeSize( &len, src, strlen(src) );
    }
    else
    {
        if (n <= 0) return 0;
        RtlMultiByteToUnicodeN( dst, n*sizeof(WCHAR), &len, src, strlen(src) );
        if (len / sizeof(WCHAR) < n) dst[len / sizeof(WCHAR)] = 0;
    }
    return len / sizeof(WCHAR);
}


/*********************************************************************
 *           iswctype    (NTDLL.@)
 */
INT __cdecl iswctype( WCHAR wc, unsigned short type )
{
    if (wc >= 256) return 0;
    return wctypes[wc] & type;
}


/*********************************************************************
 *           iswalnum    (NTDLL.@)
 */
INT __cdecl iswalnum( WCHAR wc )
{
    return iswctype( wc, C1_ALPHA | C1_UPPER | C1_LOWER | C1_DIGIT );
}


/*********************************************************************
 *           iswalpha    (NTDLL.@)
 */
INT __cdecl iswalpha( WCHAR wc )
{
    return iswctype( wc, C1_ALPHA | C1_UPPER | C1_LOWER );
}


/*********************************************************************
 *           iswascii    (NTDLL.@)
 */
INT __cdecl iswascii( WCHAR wc )
{
    return wc < 0x80;
}


/*********************************************************************
 *           iswdigit (NTDLL.@)
 */
INT __cdecl iswdigit( WCHAR wc )
{
    return iswctype( wc, C1_DIGIT );
}


/*********************************************************************
 *           iswgraph    (NTDLL.@)
 */
INT __cdecl iswgraph( WCHAR wc )
{
    return iswctype( wc, C1_ALPHA | C1_UPPER | C1_LOWER | C1_DIGIT | C1_PUNCT );
}


/*********************************************************************
 *           iswlower (NTDLL.@)
 */
INT __cdecl iswlower( WCHAR wc )
{
    return iswctype( wc, C1_LOWER );
}


/*********************************************************************
 *           iswprint    (NTDLL.@)
 */
INT __cdecl iswprint( WCHAR wc )
{
    return iswctype( wc, C1_ALPHA | C1_UPPER | C1_LOWER | C1_DIGIT | C1_PUNCT | C1_BLANK );
}


/*********************************************************************
 *           iswspace (NTDLL.@)
 */
INT __cdecl iswspace( WCHAR wc )
{
    return iswctype( wc, C1_SPACE );
}


/*********************************************************************
 *           iswxdigit (NTDLL.@)
 */
INT __cdecl iswxdigit( WCHAR wc )
{
    return iswctype( wc, C1_XDIGIT );
}


static int wctoint( WCHAR c )
{
    /* NOTE: MAP_FOLDDIGITS supports too many things. */
    /* Unicode points that contain digits 0-9; keep this sorted! */
    static const WCHAR zeros[] =
    {
        0x0660, 0x06f0, 0x0966, 0x09e6, 0x0a66, 0x0ae6, 0x0b66, 0x0c66, 0x0ce6,
        0x0d66, 0x0e50, 0x0ed0, 0x0f20, 0x1040, 0x17e0, 0x1810, 0xff10
    };
    int i;

    if ('0' <= c && c <= '9') return c - '0';
    if ('A' <= c && c <= 'Z') return c - 'A' + 10;
    if ('a' <= c && c <= 'z') return c - 'a' + 10;
    for (i = 0; i < ARRAY_SIZE(zeros) && c >= zeros[i]; i++)
        if (zeros[i] <= c && c <= zeros[i] + 9) return c - zeros[i];

    return -1;
}

/*********************************************************************
 *                  wcstol  (NTDLL.@)
 */
__msvcrt_long __cdecl wcstol(LPCWSTR s, LPWSTR *end, INT base)
{
    BOOL negative = FALSE, empty = TRUE;
    LONG ret = 0;

    if (base < 0 || base == 1 || base > 36) return 0;
    if (end) *end = (WCHAR *)s;
    while (iswspace(*s)) s++;

    if (*s == '-')
    {
        negative = TRUE;
        s++;
    }
    else if (*s == '+') s++;

    if ((base == 0 || base == 16) && !wctoint( *s ) && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    if (base == 0) base = wctoint( *s ) ? 10 : 8;

    while (*s)
    {
        int v = wctoint( *s );
        if (v < 0 || v >= base) break;
        if (negative) v = -v;
        s++;
        empty = FALSE;

        if (!negative && (ret > MAXLONG / base || ret * base > MAXLONG - v))
            ret = MAXLONG;
        else if (negative && (ret < (LONG)MINLONG / base || ret * base < (LONG)(MINLONG - v)))
            ret = MINLONG;
        else
            ret = ret * base + v;
    }

    if (end && !empty) *end = (WCHAR *)s;
    return ret;
}


/*********************************************************************
 *                  wcstoul  (NTDLL.@)
 */
__msvcrt_ulong __cdecl wcstoul(LPCWSTR s, LPWSTR *end, INT base)
{
    BOOL negative = FALSE, empty = TRUE;
    ULONG ret = 0;

    if (base < 0 || base == 1 || base > 36) return 0;
    if (end) *end = (WCHAR *)s;
    while (iswspace(*s)) s++;

    if (*s == '-')
    {
        negative = TRUE;
        s++;
    }
    else if (*s == '+') s++;

    if ((base == 0 || base == 16) && !wctoint( *s ) && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    if (base == 0) base = wctoint( *s ) ? 10 : 8;

    while (*s)
    {
        int v = wctoint( *s );
        if (v < 0 || v >= base) break;
        s++;
        empty = FALSE;

        if (ret > MAXDWORD / base || ret * base > MAXDWORD - v)
            ret = MAXDWORD;
        else
            ret = ret * base + v;
    }

    if (end && !empty) *end = (WCHAR *)s;
    return negative ? -ret : ret;
}


/*********************************************************************
 *                  _wcstoi64  (NTDLL.@)
 */
__int64 __cdecl _wcstoi64( const wchar_t *s, wchar_t **end, int base )
{
    BOOL negative = FALSE, empty = TRUE;
    __int64 ret = 0;

    if (base < 0 || base == 1 || base > 36) return 0;
    if (end) *end = (wchar_t *)s;
    while (iswspace(*s)) s++;

    if (*s == '-')
    {
        negative = TRUE;
        s++;
    }
    else if (*s == '+') s++;

    if ((base == 0 || base == 16) && !wctoint( *s ) && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    if (base == 0) base = wctoint( *s ) ? 10 : 8;

    while (*s)
    {
        int v = wctoint( *s );
        if (v < 0 || v >= base) break;
        if (negative) v = -v;
        s++;
        empty = FALSE;

        if (!negative && (ret > I64_MAX / base || ret * base > I64_MAX - v))
            ret = I64_MAX;
        else if (negative && (ret < I64_MIN / base || ret * base < I64_MIN - v))
            ret = I64_MIN;
        else
            ret = ret * base + v;
    }

    if (end && !empty) *end = (wchar_t *)s;
    return ret;
}


/*********************************************************************
 *                  _wcstoui64  (NTDLL.@)
 */
unsigned __int64 __cdecl _wcstoui64( const wchar_t *s, wchar_t **end, int base )
{
    BOOL negative = FALSE, empty = TRUE;
    unsigned __int64 ret = 0;

    if (base < 0 || base == 1 || base > 36) return 0;
    if (end) *end = (wchar_t *)s;
    while (iswspace(*s)) s++;

    if (*s == '-')
    {
        negative = TRUE;
        s++;
    }
    else if (*s == '+') s++;

    if ((base == 0 || base == 16) && !wctoint( *s ) && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    if (base == 0) base = wctoint( *s ) ? 10 : 8;

    while (*s)
    {
        int v = wctoint( *s );
        if (v < 0 || v >= base) break;
        s++;
        empty = FALSE;

        if (ret > UI64_MAX / base || ret * base > UI64_MAX - v)
            ret = UI64_MAX;
        else
            ret = ret * base + v;
    }

    if (end && !empty) *end = (wchar_t *)s;
    return negative ? -ret : ret;
}


/*********************************************************************
 *      _ultow   (NTDLL.@)
 *
 * Converts an unsigned long integer to a unicode string.
 *
 * RETURNS
 *  Always returns str.
 *
 * NOTES
 *  Converts value to a '\0' terminated wstring which is copied to str.
 *  The maximum length of the copied str is 33 bytes.
 *  Does not check if radix is in the range of 2 to 36.
 *  If str is NULL it just returns NULL.
 */
LPWSTR __cdecl _ultow( __msvcrt_ulong value, LPWSTR str, INT radix )
{
    WCHAR buffer[33];
    PWCHAR pos;
    WCHAR digit;

    pos = &buffer[32];
    *pos = '\0';

    do {
	digit = value % radix;
	value = value / radix;
	if (digit < 10) {
	    *--pos = '0' + digit;
	} else {
	    *--pos = 'a' + digit - 10;
	} /* if */
    } while (value != 0L);

    if (str != NULL) {
	memcpy(str, pos, (&buffer[32] - pos + 1) * sizeof(WCHAR));
    } /* if */
    return str;
}


/*********************************************************************
 *      _ltow   (NTDLL.@)
 *
 * Converts a long integer to a unicode string.
 *
 * RETURNS
 *  Always returns str.
 *
 * NOTES
 *  Converts value to a '\0' terminated wstring which is copied to str.
 *  The maximum length of the copied str is 33 bytes. If radix
 *  is 10 and value is negative, the value is converted with sign.
 *  Does not check if radix is in the range of 2 to 36.
 *  If str is NULL it just returns NULL.
 */
LPWSTR __cdecl _ltow( __msvcrt_long value, LPWSTR str, INT radix )
{
    ULONG val;
    int negative;
    WCHAR buffer[33];
    PWCHAR pos;
    WCHAR digit;

    if (value < 0 && radix == 10) {
	negative = 1;
        val = -value;
    } else {
	negative = 0;
        val = value;
    } /* if */

    pos = &buffer[32];
    *pos = '\0';

    do {
	digit = val % radix;
	val = val / radix;
	if (digit < 10) {
	    *--pos = '0' + digit;
	} else {
	    *--pos = 'a' + digit - 10;
	} /* if */
    } while (val != 0L);

    if (negative) {
	*--pos = '-';
    } /* if */

    if (str != NULL) {
	memcpy(str, pos, (&buffer[32] - pos + 1) * sizeof(WCHAR));
    } /* if */
    return str;
}


/*********************************************************************
 *      _itow    (NTDLL.@)
 *
 * Converts an integer to a unicode string.
 *
 * RETURNS
 *  Always returns str.
 *
 * NOTES
 *  Converts value to a '\0' terminated wstring which is copied to str.
 *  The maximum length of the copied str is 33 bytes. If radix
 *  is 10 and value is negative, the value is converted with sign.
 *  Does not check if radix is in the range of 2 to 36.
 *  If str is NULL it just returns NULL.
 *
 * DIFFERENCES
 * - The native function crashes when the string is longer than 19 chars.
 *   This function does not have this bug.
 */
LPWSTR __cdecl _itow(
    int value,  /* [I] Value to be converted */
    LPWSTR str, /* [O] Destination for the converted value */
    INT radix)  /* [I] Number base for conversion */
{
    return _ltow(value, str, radix);
}


/*********************************************************************
 *      _ui64tow   (NTDLL.@)
 *
 * Converts a large unsigned integer to a unicode string.
 *
 * RETURNS
 *  Always returns str.
 *
 * NOTES
 *  Converts value to a '\0' terminated wstring which is copied to str.
 *  The maximum length of the copied str is 33 bytes.
 *  Does not check if radix is in the range of 2 to 36.
 *  If str is NULL it just returns NULL.
 *
 * DIFFERENCES
 * - This function does not exist in the native DLL (but in msvcrt).
 *   But since the maintenance of all these functions is better done
 *   in one place we implement it here.
 */
LPWSTR __cdecl _ui64tow(
    ULONGLONG value, /* [I] Value to be converted */
    LPWSTR str,      /* [O] Destination for the converted value */
    INT radix)       /* [I] Number base for conversion */
{
    WCHAR buffer[65];
    PWCHAR pos;
    WCHAR digit;

    pos = &buffer[64];
    *pos = '\0';

    do {
	digit = value % radix;
	value = value / radix;
	if (digit < 10) {
	    *--pos = '0' + digit;
	} else {
	    *--pos = 'a' + digit - 10;
	} /* if */
    } while (value != 0L);

    if (str != NULL) {
	memcpy(str, pos, (&buffer[64] - pos + 1) * sizeof(WCHAR));
    } /* if */
    return str;
}


/*********************************************************************
 *      _i64tow   (NTDLL.@)
 *
 * Converts a large integer to a unicode string.
 *
 * RETURNS
 *  Always returns str.
 *
 * NOTES
 *  Converts value to a '\0' terminated wstring which is copied to str.
 *  The maximum length of the copied str is 33 bytes. If radix
 *  is 10 and value is negative, the value is converted with sign.
 *  Does not check if radix is in the range of 2 to 36.
 *  If str is NULL it just returns NULL.
 *
 * DIFFERENCES
 * - The native DLL converts negative values (for base 10) wrong:
 *                     -1 is converted to -18446744073709551615
 *                     -2 is converted to -18446744073709551614
 *   -9223372036854775807 is converted to  -9223372036854775809
 *   -9223372036854775808 is converted to  -9223372036854775808
 *   The native msvcrt _i64tow function and our ntdll function do
 *   not have this bug.
 */
LPWSTR __cdecl _i64tow(
    LONGLONG value, /* [I] Value to be converted */
    LPWSTR str,     /* [O] Destination for the converted value */
    INT radix)      /* [I] Number base for conversion */
{
    ULONGLONG val;
    int negative;
    WCHAR buffer[65];
    PWCHAR pos;
    WCHAR digit;

    if (value < 0 && radix == 10) {
	negative = 1;
        val = -value;
    } else {
	negative = 0;
        val = value;
    } /* if */

    pos = &buffer[64];
    *pos = '\0';

    do {
	digit = val % radix;
	val = val / radix;
	if (digit < 10) {
	    *--pos = '0' + digit;
	} else {
	    *--pos = 'a' + digit - 10;
	} /* if */
    } while (val != 0L);

    if (negative) {
	*--pos = '-';
    } /* if */

    if (str != NULL) {
	memcpy(str, pos, (&buffer[64] - pos + 1) * sizeof(WCHAR));
    } /* if */
    return str;
}


/*********************************************************************
 *      _ui64tow_s  (NTDLL.@)
 */
errno_t __cdecl _ui64tow_s( unsigned __int64 value, wchar_t *str, size_t size, int radix )
{
    wchar_t buffer[65], *pos;

    if (!str || !size) return EINVAL;
    if (radix < 2 || radix > 36)
    {
        str[0] = 0;
        return EINVAL;
    }

    pos = buffer + 64;
    *pos = 0;

    do {
	int digit = value % radix;
	value = value / radix;
	if (digit < 10)
	    *--pos = '0' + digit;
	else
	    *--pos = 'a' + digit - 10;
    } while (value != 0);

    if (buffer - pos + 65 > size)
    {
        str[0] = 0;
        return ERANGE;
    }
    memcpy( str, pos, (buffer - pos + 65) * sizeof(wchar_t) );
    return 0;
}


/*********************************************************************
 *      _ultow_s  (NTDLL.@)
 */
errno_t __cdecl _ultow_s( __msvcrt_ulong value, wchar_t *str, size_t size, int radix )
{
    return _ui64tow_s( value, str, size, radix );
}


/*********************************************************************
 *      _i64tow_s  (NTDLL.@)
 */
errno_t __cdecl _i64tow_s( __int64 value, wchar_t *str, size_t size, int radix )
{
    unsigned __int64 val;
    BOOL is_negative;
    wchar_t buffer[65], *pos;

    if (!str || !size) return EINVAL;
    if (radix < 2 || radix > 36)
    {
        str[0] = 0;
        return EINVAL;
    }

    if (value < 0 && radix == 10)
    {
        is_negative = TRUE;
        val = -value;
    }
    else
    {
        is_negative = FALSE;
        val = value;
    }

    pos = buffer + 64;
    *pos = 0;

    do
    {
        unsigned int digit = val % radix;
        val /= radix;

        if (digit < 10)
            *--pos = '0' + digit;
        else
            *--pos = 'a' + digit - 10;
    }
    while (val != 0);

    if (is_negative) *--pos = '-';

    if (buffer - pos + 65 > size)
    {
        str[0] = 0;
        return ERANGE;
    }
    memcpy( str, pos, (buffer - pos + 65) * sizeof(wchar_t) );
    return 0;
}


/*********************************************************************
 *      _ltow_s  (NTDLL.@)
 */
errno_t __cdecl _ltow_s( __msvcrt_long value, wchar_t *str, size_t size, int radix )
{
    if (value < 0 && radix == 10) return _i64tow_s( value, str, size, radix );
    return _ui64tow_s( (__msvcrt_ulong)value, str, size, radix );
}


/*********************************************************************
 *      _itow_s  (NTDLL.@)
 */
errno_t __cdecl _itow_s( int value, wchar_t *str, size_t size, int radix )
{
    if (value < 0 && radix == 10) return _i64tow_s( value, str, size, radix );
    return _ui64tow_s( (unsigned int)value, str, size, radix );
}


/*********************************************************************
 *      _wtol    (NTDLL.@)
 *
 * Converts a unicode string to a long integer.
 *
 * PARAMS
 *  str [I] Wstring to be converted
 *
 * RETURNS
 *  On success it returns the integer value otherwise it returns 0.
 *
 * NOTES
 *  Accepts: {whitespace} [+|-] {digits}
 *  No check is made for value overflow, only the lower 32 bits are assigned.
 *  If str is NULL it crashes, as the native function does.
 */
__msvcrt_long __cdecl _wtol( LPCWSTR str )
{
    ULONG RunningTotal = 0;
    BOOL bMinus = FALSE;

    while (iswspace(*str)) str++;

    if (*str == '+') {
	str++;
    } else if (*str == '-') {
        bMinus = TRUE;
	str++;
    } /* if */

    while (*str >= '0' && *str <= '9') {
	RunningTotal = RunningTotal * 10 + *str - '0';
	str++;
    } /* while */

    return bMinus ? -RunningTotal : RunningTotal;
}


/*********************************************************************
 *      _wtoi    (NTDLL.@)
 *
 * Converts a unicode string to an integer.
 *
 * PARAMS
 *  str [I] Wstring to be converted
 *
 * RETURNS
 *  On success it returns the integer value otherwise it returns 0.
 *
 * NOTES
 *  Accepts: {whitespace} [+|-] {digits}
 *  No check is made for value overflow, only the lower 32 bits are assigned.
 *  If str is NULL it crashes, as the native function does.
 */
int __cdecl _wtoi( LPCWSTR str )
{
    return _wtol(str);
}


/*********************************************************************
 *      _wtoi64   (NTDLL.@)
 *
 * Converts a unicode string to a large integer.
 *
 * PARAMS
 *  str [I] Wstring to be converted
 *
 * RETURNS
 *  On success it returns the integer value otherwise it returns 0.
 *
 * NOTES
 *  Accepts: {whitespace} [+|-] {digits}
 *  No check is made for value overflow, only the lower 64 bits are assigned.
 *  If str is NULL it crashes, as the native function does.
 */
LONGLONG  __cdecl _wtoi64( LPCWSTR str )
{
    ULONGLONG RunningTotal = 0;
    BOOL bMinus = FALSE;

    while (iswspace(*str)) str++;

    if (*str == '+') {
	str++;
    } else if (*str == '-') {
        bMinus = TRUE;
	str++;
    } /* if */

    while (*str >= '0' && *str <= '9') {
	RunningTotal = RunningTotal * 10 + *str - '0';
	str++;
    } /* while */

    return bMinus ? -RunningTotal : RunningTotal;
}


/******************************************************************
 *      _wsplitpath_s   (NTDLL.@)
 */
errno_t __cdecl _wsplitpath_s( const wchar_t *inpath, wchar_t *drive, size_t sz_drive,
                               wchar_t *dir, size_t sz_dir, wchar_t *fname, size_t sz_fname,
                               wchar_t *ext, size_t sz_ext )
{
    const wchar_t *p, *end;

    if (!inpath || (!drive && sz_drive) ||
        (drive && !sz_drive) ||
        (!dir && sz_dir) ||
        (dir && !sz_dir) ||
        (!fname && sz_fname) ||
        (fname && !sz_fname) ||
        (!ext && sz_ext) ||
        (ext && !sz_ext))
        return EINVAL;

    if (inpath[0] && inpath[1] == ':')
    {
        if (drive)
        {
            if (sz_drive <= 2) goto error;
            drive[0] = inpath[0];
            drive[1] = inpath[1];
            drive[2] = 0;
        }
        inpath += 2;
    }
    else if (drive) drive[0] = '\0';

    /* look for end of directory part */
    end = NULL;
    for (p = inpath; *p; p++) if (*p == '/' || *p == '\\') end = p + 1;

    if (end)  /* got a directory */
    {
        if (dir)
        {
            if (sz_dir <= end - inpath) goto error;
            memcpy( dir, inpath, (end - inpath) * sizeof(wchar_t) );
            dir[end - inpath] = 0;
        }
        inpath = end;
    }
    else if (dir) dir[0] = 0;

    /* look for extension: what's after the last dot */
    end = NULL;
    for (p = inpath; *p; p++) if (*p == '.') end = p;

    if (!end) end = p; /* there's no extension */

    if (fname)
    {
        if (sz_fname <= end - inpath) goto error;
        memcpy( fname, inpath, (end - inpath) * sizeof(wchar_t) );
        fname[end - inpath] = 0;
    }
    if (ext)
    {
        if (sz_ext <= wcslen(end)) goto error;
        wcscpy( ext, end );
    }
    return 0;

error:
    if (drive) drive[0] = 0;
    if (dir) dir[0] = 0;
    if (fname) fname[0]= 0;
    if (ext) ext[0]= 0;
    return ERANGE;
}


/*********************************************************************
 *      _wmakepath_s   (NTDLL.@)
 */
errno_t __cdecl _wmakepath_s( wchar_t *path, size_t size, const wchar_t *drive,
                              const wchar_t *directory, const wchar_t *filename,
                              const wchar_t *extension )
{
    wchar_t *p = path;

    if (!path || !size) return EINVAL;

    if (drive && drive[0])
    {
        if (size <= 2) goto range;
        *p++ = drive[0];
        *p++ = ':';
        size -= 2;
    }

    if (directory && directory[0])
    {
        unsigned int len = wcslen(directory);
        unsigned int needs_separator = directory[len - 1] != '/' && directory[len - 1] != '\\';
        unsigned int copylen = min(size - 1, len);

        if (size < 2) goto range;
        memmove(p, directory, copylen * sizeof(wchar_t));
        if (size <= len) goto range;
        p += copylen;
        size -= copylen;
        if (needs_separator)
        {
            if (size < 2) goto range;
            *p++ = '\\';
            size -= 1;
        }
    }

    if (filename && filename[0])
    {
        unsigned int len = wcslen(filename);
        unsigned int copylen = min(size - 1, len);

        if (size < 2) goto range;
        memmove(p, filename, copylen * sizeof(wchar_t));
        if (size <= len) goto range;
        p += len;
        size -= len;
    }

    if (extension && extension[0])
    {
        unsigned int len = wcslen(extension);
        unsigned int needs_period = extension[0] != '.';
        unsigned int copylen;

        if (size < 2) goto range;
        if (needs_period)
        {
            *p++ = '.';
            size -= 1;
        }
        copylen = min(size - 1, len);
        memcpy(p, extension, copylen * sizeof(wchar_t));
        if (size <= len) goto range;
        p += copylen;
    }

    *p = 0;
    return 0;

range:
    path[0] = 0;
    return ERANGE;
}
