/*
 * Benchmark & Verification Suite for the SSE2 ASCII -> UTF-16 fast path
 * in dlls/ntdll/locale_private.h (utf8_mbstowcs / utf8_mbstowcs_size)
 *
 * Copyright 2026 Zhymabek Roman
 *
 * The code under test is the *real* ntdll inline implementation: this file
 * includes dlls/ntdll/locale_private.h with the same include preamble that
 * dlls/ntdll/locale.c uses, so the compiled fast path is byte-for-byte the one
 * ntdll builds, with no copy drift.
 *
 * Verified:
 * - Equivalence of the vectorized and scalar paths on every generated corpus,
 *   checking the returned NTSTATUS, *reslen, and every written WCHAR, plus
 *   guard bytes that prove neither path writes outside the destination buffer.
 * - Pure ASCII strings, every prefix length 1..1024.
 * - Mixed ASCII + Cyrillic (2 byte) + Greek/CJK (3 byte) + Emoji (4 byte,
 *   surrogate pairs), including the U+007F/U+0080/U+07FF/U+0800/U+FFFF/
 *   U+10000/U+10FFFF boundaries.
 * - Boundary lengths 0..32 and the first-non-ASCII position swept across the
 *   16/32/48/64 byte vector block edges.
 * - Unaligned sources (byte offsets 0..15) and unaligned destinations
 *   (WCHAR offsets 0..15).
 * - Malformed UTF-8: lone continuations, overlongs, encoded surrogates,
 *   out-of-range (> U+10FFFF), 0xfe/0xff, truncated tail sequences.
 * - Buffer-too-small: every dstlen from 0 to needed+2, including the split
 *   surrogate pair case, asserting STATUS_BUFFER_TOO_SMALL and exact reslen.
 * - Seeded random fuzz over valid and invalid byte streams.
 * - Self-consistency of the pair ntdll uses: utf8_mbstowcs_size() must predict
 *   the exact dstlen that makes utf8_mbstowcs() return STATUS_SUCCESS.
 *
 * Benchmarked:
 * - Throughput in MB/s on large JSON, HTML and Cyrillic/emoji-heavy payloads,
 *   for both utf8_mbstowcs() and utf8_mbstowcs_size().
 *
 * Build (from a configured Wine source tree; $SRC = wine source, $BLD = build dir):
 *
 *   x86_64-w64-mingw32-gcc -O2 -fno-strict-aliasing -Wall -o bench_utf8_simd.exe \
 *     tools/bench_utf8_simd.c \
 *     -I$BLD/include -I$SRC/dlls/ntdll -I$SRC/include -I$SRC/include/msvcrt \
 *     -D__WINESRC__ -D__WINE_PE_BUILD -D_MSVCR_VER=0 -D_ACRTIMP= -DWINBASEAPI= -D_NTSYSTEM_
 *
 * A/B baseline that compiles the vector path out of the very same source
 * (the header honours NO_SSE2_UTF8): add -DNO_SSE2_UTF8 and build a second exe.
 * The in-file scalar reference (scalar_mbstowcs / scalar_mbstowcs_size) is a
 * verbatim copy of the pre-vector ntdll code and serves as the equivalence
 * oracle inside a single binary.
 *
 * Run: wine bench_utf8_simd.exe
 */

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winternl.h"

#include "locale_private.h"

#define PASS 0
#define FAIL 1

static int g_checks = 0;
static int g_failures = 0;
static int g_verbose = 0;

#define MAXSRC   4096          /* max source bytes per test conversion   */
#define GUARD    32            /* guard elements on each side of dst     */
#define FILLPAT  0xcd          /* byte pattern pre-filled into dst       */

static char  src_buf[MAXSRC + 64];
static WCHAR dst_simd[MAXSRC + 2 * GUARD + 64];
static WCHAR dst_ref[MAXSRC + 2 * GUARD + 64];
static WCHAR dst_siz[MAXSRC + 2 * GUARD + 64];

/* ------------------------------------------------------------------ *
 * Reference implementation: verbatim copy of the pre-vector ntdll code.
 * ------------------------------------------------------------------ */

static NTSTATUS scalar_mbstowcs( WCHAR *dst, unsigned int dstlen, unsigned int *reslen,
                                 const char *src, unsigned int srclen )
{
    unsigned int res;
    NTSTATUS status = STATUS_SUCCESS;
    const char *srcend = src + srclen;
    WCHAR *dstend = dst + dstlen;

    while ((dst < dstend) && (src < srcend))
    {
        unsigned char ch = *src++;
        if (ch < 0x80)  /* special fast case for 7-bit ASCII */
        {
            *dst++ = ch;
            continue;
        }
        if ((res = decode_utf8_char( ch, &src, srcend )) <= 0xffff)
        {
            *dst++ = res;
        }
        else if (res <= 0x10ffff)  /* we need surrogates */
        {
            res -= 0x10000;
            *dst++ = 0xd800 | (res >> 10);
            if (dst == dstend)
            {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            *dst++ = 0xdc00 | (res & 0x3ff);
        }
        else
        {
            *dst++ = 0xfffd;
            status = STATUS_SOME_NOT_MAPPED;
        }
    }
    if (src < srcend) status = STATUS_BUFFER_TOO_SMALL;  /* overflow */
    *reslen = dstlen - (dstend - dst);
    return status;
}

static NTSTATUS scalar_mbstowcs_size( const char *src, unsigned int srclen, unsigned int *reslen )
{
    unsigned int res, len;
    NTSTATUS status = STATUS_SUCCESS;
    const char *srcend = src + srclen;

    for (len = 0; src < srcend; len++)
    {
        unsigned char ch = *src++;
        if (ch < 0x80) continue;
        if ((res = decode_utf8_char( ch, &src, srcend )) > 0x10ffff)
            status = STATUS_SOME_NOT_MAPPED;
        else
            if (res > 0xffff) len++;
    }
    *reslen = len;
    return status;
}

/* ------------------------------------------------------------------ *
 * Deterministic PRNG (xorshift64*) - identical on every platform.
 * ------------------------------------------------------------------ */

static unsigned long long rng_state = 0x9e3779b97f4a7c15ULL;

static unsigned int rng_next( unsigned int limit )
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (unsigned int)((rng_state * 0x2545f4914f6cdd1dULL) >> 32) % limit;
}

static void rng_seed( unsigned long long s )
{
    rng_state = s ? s : 1;
}

/* ------------------------------------------------------------------ *
 * Test helpers
 * ------------------------------------------------------------------ */

static void fill_dst( WCHAR *buf, unsigned int dstlen )
{
    WCHAR *base = buf + GUARD;
    unsigned int i;
    unsigned char *p = (unsigned char *)base;

    for (i = 0; i < (dstlen + GUARD) * sizeof(WCHAR); i++) p[i] = FILLPAT;
    for (i = 0; i < GUARD * sizeof(WCHAR); i++) ((unsigned char *)buf)[i] = FILLPAT;
}

/* returns 0 when the left guard, every slot past `written` up to dstlen, and the
   GUARD slots beyond dstlen are all still filled with FILLPAT. The last region is
   what catches a vector block writing past the end of a tight destination. */
static int guards_intact( const WCHAR *buf, unsigned int dstlen, unsigned int written )
{
    const unsigned char *p = (const unsigned char *)buf;
    unsigned int i, limit;

    for (i = 0; i < GUARD * sizeof(WCHAR); i++)
        if (p[i] != FILLPAT) return 1;

    p = (const unsigned char *)(buf + GUARD);
    limit = (dstlen > written ? dstlen : written) * sizeof(WCHAR);
    for (i = written * sizeof(WCHAR); i < limit; i++)
        if (p[i] != FILLPAT) return 1;

    /* slots strictly past the requested destination must never be touched */
    for (i = dstlen * sizeof(WCHAR); i < (dstlen + GUARD) * sizeof(WCHAR); i++)
        if (p[i] != FILLPAT) return 2;
    return 0;
}

static void report( const char *what, int ok, const char *fmt, ... )
{
    if (ok) return;
    g_failures++;
    if (fmt && *fmt)
    {
        va_list args;
        va_start( args, fmt );
        fprintf( stderr, "FAILED [%s] ", what );
        vfprintf( stderr, fmt, args );
        fprintf( stderr, "\n" );
        va_end( args );
    }
    else fprintf( stderr, "FAILED [%s]\n", what );
}

/* UTF-8 encode one codepoint; returns byte count (0 for invalid cp) */
static int put_utf8( char *p, unsigned int cp )
{
    if (cp < 0x80) { p[0] = (char)cp; return 1; }
    if (cp < 0x800) { p[0] = (char)(0xc0 | (cp >> 6)); p[1] = (char)(0x80 | (cp & 0x3f)); return 2; }
    if (cp < 0x10000)
    {
        p[0] = (char)(0xe0 | (cp >> 12));
        p[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        p[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    if (cp <= 0x10ffff)
    {
        p[0] = (char)(0xf0 | (cp >> 18));
        p[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        p[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
        p[3] = (char)(0x80 | (cp & 0x3f));
        return 4;
    }
    return 0;
}

/* expected UTF-16 length of one codepoint, 0xfffd-replacement = 1 */
static unsigned int utf16_len( unsigned int cp )
{
    return cp >= 0x10000 ? 2 : 1;
}

/* ------------------------------------------------------------------ *
 * Core comparison: run the real (possibly vectorized) implementation and
 * the scalar reference side by side and compare everything.
 * ------------------------------------------------------------------ */

static int compare_conv( const char *tag, const char *src, unsigned int srclen, unsigned int dstlen )
{
    NTSTATUS st_a, st_b;
    unsigned int len_a = 0xdead, len_b = 0xdead;
    WCHAR *a = dst_simd + GUARD, *b = dst_ref + GUARD;
    unsigned int i, cmp_len;
    int ok = 1, g = 0;

    g_checks++;
    fill_dst( dst_simd, dstlen );
    fill_dst( dst_ref, dstlen );

    st_a = utf8_mbstowcs( a, dstlen, &len_a, src, srclen );
    st_b = scalar_mbstowcs( b, dstlen, &len_b, src, srclen );

    if (st_a != st_b) { report( tag, 0, "status %08x != scalar %08x (srclen %u dstlen %u)",
                                (unsigned int)st_a, (unsigned int)st_b, srclen, dstlen ); ok = 0; }
    if (len_a != len_b) { report( tag, 0, "reslen %u != scalar %u (srclen %u dstlen %u)",
                                  len_a, len_b, srclen, dstlen ); ok = 0; }
    if (len_a > dstlen)
        { report( tag, 0, "reslen %u exceeds dstlen %u (srclen %u)", len_a, dstlen, srclen ); ok = 0; }
    g = guards_intact( dst_simd, dstlen, len_a );
    if (g)
        { report( tag, 0, "%s outside destination (srclen %u dstlen %u reslen %u)",
                  g == 2 ? "wrote past end of buffer" : "clobbered guard", srclen, dstlen, len_a ); ok = 0; }
    if (guards_intact( dst_ref, dstlen, len_b ))
        { report( tag, 0, "scalar wrote outside destination (srclen %u dstlen %u)", srclen, dstlen ); ok = 0; }

    cmp_len = len_a < len_b ? len_a : len_b;
    for (i = 0; ok && i < cmp_len; i++)
        if (a[i] != b[i])
            { report( tag, 0, "dst[%u] = %04x != scalar %04x (srclen %u dstlen %u)",
                      i, a[i], b[i], srclen, dstlen ); ok = 0; }
    return ok;
}

static int compare_size( const char *tag, const char *src, unsigned int srclen )
{
    NTSTATUS st_a, st_b;
    unsigned int len_a = 0xdead, len_b = 0xdead;

    g_checks++;
    st_a = utf8_mbstowcs_size( src, srclen, &len_a );
    st_b = scalar_mbstowcs_size( src, srclen, &len_b );

    if (st_a != st_b)
        { report( tag, 0, "size status %08x != scalar %08x (srclen %u)",
                  (unsigned int)st_a, (unsigned int)st_b, srclen ); return 0; }
    if (len_a != len_b)
        { report( tag, 0, "size %u != scalar %u (srclen %u)", len_a, len_b, srclen ); return 0; }
    return 1;
}

static int compare_all( const char *tag, const char *src, unsigned int srclen, unsigned int dstlen )
{
    int ok = compare_size( tag, src, srclen );
    ok &= compare_conv( tag, src, srclen, dstlen );
    return ok;
}

/* ------------------------------------------------------------------ *
 * Test 1: pure ASCII, every length 1..1024 (+0)
 * ------------------------------------------------------------------ */

static void test_pure_ascii( void )
{
    static const char tag[] = "ascii";
    int ck = g_checks, fc = g_failures;
    unsigned int len, i;
    unsigned int size;
    NTSTATUS st;
    WCHAR *d;

    for (len = 0; len <= 1024; len++)
    {
        for (i = 0; i < len; i++) src_buf[i] = (char)(33 + (i % 90));  /* printable, never NUL */
        compare_all( tag, src_buf, len, len + 8 );

        /* direct oracle: ASCII widens 1:1, no surrogates, no errors */
        st = utf8_mbstowcs( dst_siz + GUARD, len + 8, &size, src_buf, len );
        d = dst_siz + GUARD;
        if (st != STATUS_SUCCESS) { report( tag, 0, "ascii len %u status %08x", len, (unsigned int)st ); break; }
        if (size != len) { report( tag, 0, "ascii len %u reslen %u", len, size ); break; }
        for (i = 0; i < len; i++)
            if (d[i] != (WCHAR)(unsigned char)src_buf[i])
                { report( tag, 0, "ascii len %u dst[%u] = %04x want %02x", len, i, d[i], (unsigned char)src_buf[i] ); break; }

        /* NUL bytes must widen to 0 too (movemask must not treat them specially) */
        memset( src_buf, 0, len );
        compare_all( tag, src_buf, len, len + 8 );
        st = utf8_mbstowcs( dst_siz + GUARD, len + 8, &size, src_buf, len );
        d = dst_siz + GUARD;
        if (st != STATUS_SUCCESS || size != len)
            { report( tag, 0, "NUL run len %u status %08x reslen %u", len, (unsigned int)st, size ); break; }
        for (i = 0; i < len; i++) if (d[i]) { report( tag, 0, "NUL run len %u dst[%u] = %04x", len, i, d[i] ); break; }
    }

    /* 7-bit top of the ASCII range: 0x7f must stay ASCII, 0x80 must not */
    src_buf[0] = 0x7f;
    compare_all( tag, src_buf, 1, 4 );
    src_buf[0] = (char)0x80;
    compare_all( tag, src_buf, 1, 4 );

    printf( "  pure ASCII (0..1024, NUL runs, 0x7f/0x80 edges): %d checks, %d failures\n",
            g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Test 2: mixed ASCII + Cyrillic / Greek / CJK / emoji, every prefix
 * ------------------------------------------------------------------ */

static const unsigned int cp_pool[] =
{
    'a','b','z','A','Z','0','9',' ','{','}','"',',',':','/','\n','\t',
    0x410,0x420,0x430,0x44f,0x401,0x451,          /* Cyrillic */
    0x391,0x3a3,0x3c9,                              /* Greek */
    0x5d0,0x5e9,                                   /* Hebrew */
    0x627,0x644,                                   /* Arabic */
    0x3042,0x304a,0x4e00,0x4e2d,0x6587,0x7fd4,     /* CJK/kana */
    0x1f600,0x1f60d,0x1f680,0x1f1e9,0x1f1f5,       /* emoji (surrogates) */
    0x10000,0x10ffff,                              /* astral boundaries */
    0x7f,0x80,0x7ff,0x800,0xfffd,0xffff            /* UTF-8 length boundaries */
};

static unsigned int build_mixed( unsigned int cps, unsigned int seed, int *cp_out, unsigned int *len_out )
{
    unsigned int i, n = 0, total = 0;
    int cp;

    rng_seed( seed );
    for (i = 0; i < cps; i++)
    {
        cp = cp_pool[rng_next( (unsigned int)(sizeof(cp_pool)/sizeof(cp_pool[0])) )];
        /* keep everything inside the scratch buffer */
        if (total + 4 > MAXSRC) break;
        total += put_utf8( src_buf + total, (unsigned int)cp );
        if (cp_out) cp_out[i] = cp;
        n++;
    }
    *len_out = total;
    return n;
}

static void test_mixed( void )
{
    static const char tag[] = "mixed";
    int ck = g_checks, fc = g_failures;
    unsigned int len, pref, seed;

    for (seed = 1; seed <= 40; seed++)
    {
        build_mixed( 400, seed, NULL, &len );
        compare_all( tag, src_buf, len, len + 8 );
        compare_all( tag, src_buf, len, len );          /* exact fit */

        /* every prefix length: exercises block alignment at every offset */
        for (pref = 0; pref <= len; pref++)
            compare_all( tag, src_buf, pref, pref + 8 );

        /* verify the surrogate pairs for astral codepoints directly, on the
           full string, against the encoder definition (independent oracle) */
        if (seed == 1)
        {
            unsigned int pos = 0, cps, produced, cp;
            WCHAR *d = dst_siz + GUARD;

            build_mixed( 400, 1, NULL, &len );
            utf8_mbstowcs( d, len + 8, &produced, src_buf, len );
            rng_seed( 1 );
            for (cps = 0; cps < 400; cps++)
            {
                unsigned int v;
                cp = cp_pool[rng_next( (unsigned int)(sizeof(cp_pool)/sizeof(cp_pool[0])) )];
                if (cp >= 0x10000)
                {
                    v = cp - 0x10000;
                    if (pos + 2 > produced ||
                        d[pos] != (WCHAR)(0xd800 | (v >> 10)) ||
                        d[pos+1] != (WCHAR)(0xdc00 | (v & 0x3ff)))
                        {
                            report( tag, 0, "surrogate pair for cp %05x at %u: %04x %04x",
                                    cp, pos, d[pos], d[pos+1] );
                            break;
                        }
                    pos += 2;
                }
                else
                {
                    if (pos >= produced || d[pos] != (WCHAR)cp)
                        { report( tag, 0, "codepoint %05x at %u: %04x", cp, pos, d[pos] ); break; }
                    pos++;
                }
            }
            if (pos != produced)
                report( tag, 0, "consumed %u UTF-16 units, produced %u", pos, produced );
        }
    }
    printf("  mixed ASCII + Cyrillic/Greek/Hebrew/Arabic/CJK/emoji, all prefixes: %d checks, %d failures\n",
           g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Test 3: first non-ASCII byte swept across the 16/32/48/64 block edges
 * ------------------------------------------------------------------ */

static void test_first_nonascii_boundary( void )
{
    static const char tag[] = "boundary";
    int ck = g_checks, fc = g_failures;
    static const unsigned int tails[] = { 0x410, 0x4e2d, 0x1f600, 0x451, 0x3042 };
    unsigned int pos, k, total, tl, tail_bytes, size;
    NTSTATUS st;
    int ok;

    for (pos = 0; pos <= 64; pos++)
        for (k = 0; k < sizeof(tails)/sizeof(tails[0]); k++)
        {
            memset( src_buf, 'x', pos );
            tail_bytes = (unsigned int)put_utf8( src_buf + pos, tails[k] );
            for (tl = 0; tl < 24; tl++) src_buf[pos + tail_bytes + tl] = 'y';
            total = pos + tail_bytes + 24;

            ok = compare_all( tag, src_buf, total, total + 8 );

            /* the vector path must stop exactly at the block containing the
               non-ASCII byte: check the whole prefix was widened correctly */
            if (ok)
            {
                WCHAR *d = dst_siz + GUARD;
                st = utf8_mbstowcs( d, total + 8, &size, src_buf, total );
                if (st != STATUS_SUCCESS) report( tag, 0, "pos %u tail %05x status %08x", pos, tails[k], (unsigned int)st );
                for (tl = 0; tl < pos; tl++)
                    if (d[tl] != 'x') { report( tag, 0, "pos %u prefix byte %u = %04x", pos, tl, d[tl] ); break; }
                for (tl = 0; tl < 24; tl++)
                {
                    unsigned int idx = pos + utf16_len( tails[k] ) + tl;
                    if (idx < size && d[idx] != 'y')
                        { report( tag, 0, "pos %u tail %05x suffix byte %u = %04x", pos, tails[k], tl, d[idx] ); break; }
                }
            }

            /* non-ASCII exactly at the last byte of a 16-byte block */
            compare_all( tag, src_buf, pos + tail_bytes, pos + tail_bytes );
        }
    printf("  first non-ASCII position swept 0..64 across 16-byte block edges: %d checks, %d failures\n",
           g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Test 4: exhaustive short lengths 0..32 over a byte-class alphabet
 * ------------------------------------------------------------------ */

static void test_boundary_lengths( void )
{
    static const char tag[] = "short";
    int ck = g_checks, fc = g_failures;
    static const unsigned char classes[] =
    {
        0x00, 0x41, 0x7f, 0x80, 0x8f, 0xa0, 0xbf,
        0xc0, 0xc2, 0xdf, 0xe0, 0xe5, 0xef,
        0xf0, 0xf4, 0xf7, 0xfe, 0xff
    };
    unsigned int len, i, rep, a, b, c;

    /* all 1-byte inputs, all 2-byte inputs, sampled 3-byte inputs, per length */
    for (len = 0; len <= 32; len++)
    {
        for (i = 0; i < sizeof(classes); i++)
            for (rep = 0; rep <= len; rep++) src_buf[rep] = (char)classes[i];
        if (len) compare_all( tag, src_buf, len, len + 4 );

        for (a = 0; a < sizeof(classes); a++)
            for (b = 0; b < sizeof(classes); b++)
            {
                for (rep = 0; rep < len; rep++) src_buf[rep] = (char)classes[a + b >= sizeof(classes) ? a : b];
                for (rep = 0; rep < len; rep++)
                {
                    src_buf[rep] = (char)classes[a];
                    if (rep + 1 < len) src_buf[rep + 1] = (char)classes[b];
                    if (rep + 2 < len) src_buf[rep + 2] = (char)classes[(a * 5 + b * 3) % sizeof(classes)];
                }
                if (len) compare_all( tag, src_buf, len, len + 4 );
                compare_all( tag, src_buf, len, len / 2 );     /* tight buffer */
            }
        c = len; (void)c;
    }

    /* every valid 2/3/4-byte sequence, padded to every length up to 32 */
    {
        static const unsigned int seqs[] = { 0x80, 0x7ff, 0x800, 0x8000, 0xffff, 0x10000, 0x10ffff, 0x1f600 };
        unsigned int s, pad;
        for (s = 0; s < sizeof(seqs)/sizeof(seqs[0]); s++)
            for (pad = 0; pad <= 32; pad++)
            {
                unsigned int n = 0;
                memset( src_buf, 'a', pad ); n += pad;
                n += (unsigned int)put_utf8( src_buf + n, seqs[s] );
                memset( src_buf + n, 'b', 32 );
                compare_all( tag, src_buf, n + 32, n + 32 + 4 );
                compare_all( tag, src_buf, n, n );
            }
    }
    printf("  boundary lengths 0..32 over byte-class alphabet + valid 2/3/4-byte seqs: %d checks, %d failures\n",
           g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Test 5: malformed UTF-8
 * ------------------------------------------------------------------ */

static void test_malformed( void )
{
    static const char tag[] = "malformed";
    int ck = g_checks, fc = g_failures;
    static const char *cases[] =
    {
        "\x80",                                   /* lone continuation            */
        "\xbf",
        "\xc0\x80",                               /* overlong NUL                 */
        "\xc1\xbf",                               /* overlong                     */
        "\xc2",                                   /* truncated 2-byte at end      */
        "\xe0\x80",                               /* truncated 3-byte             */
        "\xe0\xa0\x80",                           /* overlong 3-byte              */
        "\xed\xa0\x80",                           /* encoded surrogate D800       */
        "\xed\xbf\xbf",                           /* encoded surrogate DFFF       */
        "\xf0\x80\x80\x80",                       /* overlong 4-byte              */
        "\xf4\x90\x80\x80",                       /* above U+10FFFF               */
        "\xf7\xbf\xbf\xbf",                       /* F4..FF range                 */
        "\xfe", "\xff",
        "abcdefghij\xffklmno",                    /* invalid in the middle        */
        "abcdefghijklmnop\xffrst",                /* invalid just past a 16 block */
        "abcdefghijklmnopqrstuvwxyz\xff",         /* invalid at end               */
        "\xc2\xa9" "\x41" "\xe2\x82\xac" "\xf0\x9f\x98\x80" "\x80" "\xc2",
        "aaaaaaaaaaaaaaaaaaaa\xf0\x9f\x98",       /* truncated astral at end      */
        "aaaaaaaaaaaaaaaa\xf4\x8f\xbf\xbf" "bb",  /* U+10FFFF at block edge       */
        "aaaaaaaaaaaaaaaa\xf5\x8a\xa4\x8c" "bb",  /* invalid astral at block edge */
    };
    unsigned int i, len;
    NTSTATUS st;
    unsigned int size;

    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
    {
        len = (unsigned int)strlen( cases[i] );
        memcpy( src_buf, cases[i], len );
        compare_all( tag, src_buf, len, len + 8 );
        if (len) compare_all( tag, src_buf, len, len );
        for (len = 0; len <= (unsigned int)strlen(cases[i]) + 2; len++)
            compare_conv( tag, src_buf, len, len );

        /* expected policy for each case, checked against the real decoder:
           STATUS_SUCCESS for the two canonical inputs, STATUS_SOME_NOT_MAPPED
           plus a U+FFFD replacement for every malformed one */
        {
            static const int expect_valid[] =
            {
                0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0
            };
            unsigned int j, fffd = 0;
            int valid = expect_valid[i];
            const char *str = cases[i];

            st = utf8_mbstowcs( dst_siz + GUARD, 64, &size, str, (unsigned int)strlen(str) );
            if ((st == STATUS_SUCCESS) != valid)
                report( tag, 0, "case %u [%s] status %08x, expected %s",
                        i, str, (unsigned int)st, valid ? "SUCCESS" : "SOME_NOT_MAPPED" );
            for (j = 0; j < size; j++) if (dst_siz[GUARD + j] == 0xfffd) fffd++;
            if (!valid && !fffd)
                report( tag, 0, "case %u [%s] produced no U+FFFD replacement", i, str );
            if (valid && fffd)
                report( tag, 0, "case %u [%s] replaced a valid sequence", i, str );
        }
    }

    /* every single byte value as a 1-byte input, and as a 17-byte tail */
    for (i = 0; i < 256; i++)
    {
        src_buf[0] = (char)i;
        compare_all( tag, src_buf, 1, 4 );
        memset( src_buf, 'a', 16 );
        src_buf[16] = (char)i;
        compare_all( tag, src_buf, 17, 24 );
        memset( src_buf, 'a', 32 );
        src_buf[32] = (char)i;
        compare_all( tag, src_buf, 33, 40 );
    }
    printf("  malformed UTF-8 + all 256 byte values: %d checks, %d failures\n", g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Test 6: STATUS_BUFFER_TOO_SMALL for every dstlen
 * ------------------------------------------------------------------ */

static void test_buffer_too_small( void )
{
    static const char tag[] = "toosmall";
    int ck = g_checks, fc = g_failures;
    unsigned int seed, len, needed, dstlen, size;
    NTSTATUS st;
    unsigned int needed_status;
    unsigned int ref_len;
    NTSTATUS ref_st;
    WCHAR *d = dst_ref + GUARD;

    for (seed = 1; seed <= 12; seed++)
    {
        build_mixed( 200, seed, NULL, &len );
        ref_st = scalar_mbstowcs_size( src_buf, len, &needed );
        if (ref_st != STATUS_SUCCESS && ref_st != STATUS_SOME_NOT_MAPPED)
            { report( tag, 0, "seed %u unexpected size status %08x", seed, (unsigned int)ref_st ); break; }
        needed_status = ref_st;

        for (dstlen = 0; dstlen <= needed + 2; dstlen++)
        {
            fill_dst( dst_simd, dstlen );
            fill_dst( dst_ref, dstlen );
            st = utf8_mbstowcs( dst_simd + GUARD, dstlen, &size, src_buf, len );
            ref_st = scalar_mbstowcs( dst_ref + GUARD, dstlen, &ref_len, src_buf, len );
            g_checks++;

            if (st != ref_st || size != ref_len)
                { report( tag, 0, "seed %u dstlen %u: status %08x/%08x reslen %u/%u",
                          seed, dstlen, (unsigned int)st, (unsigned int)ref_st, size, ref_len ); break; }

            if (memcmp( dst_simd + GUARD, dst_ref + GUARD, size * sizeof(WCHAR) ))
                { report( tag, 0, "seed %u dstlen %u: partial output differs", seed, dstlen ); break; }

            if (guards_intact( dst_simd, dstlen, size ))
                { report( tag, 0, "seed %u dstlen %u: wrote past buffer", seed, dstlen ); break; }

            /* ntdll contract: the dstlen predicted by the size helper is exactly
               the point where the conversion stops reporting BUFFER_TOO_SMALL */
            if (dstlen >= needed && size != needed)
                { report( tag, 0, "seed %u dstlen %u: reslen %u want %u", seed, dstlen, size, needed ); break; }
            if (dstlen < needed)
            {
                if (st != STATUS_BUFFER_TOO_SMALL)
                    { report( tag, 0, "seed %u dstlen %u < %u: status %08x not BUFFER_TOO_SMALL",
                              seed, dstlen, needed, (unsigned int)st ); break; }
            }
            else if ((int)st != (int)needed_status)
                { report( tag, 0, "seed %u dstlen %u: status %08x want %08x",
                          seed, dstlen, (unsigned int)st, (unsigned int)needed_status ); break; }
        }

        /* size() must predict the exact dstlen that yields STATUS_SUCCESS */
        needed = 0;
        st = utf8_mbstowcs_size( src_buf, len, &needed );
        (void)st;
        st = utf8_mbstowcs( d, needed, &ref_len, src_buf, len );
        if (needed && st != STATUS_SUCCESS && st != STATUS_SOME_NOT_MAPPED)
            { report( tag, 0, "seed %u: size %u did not fit (status %08x)", seed, needed, (unsigned int)st ); break; }
        if (needed && ref_len != needed)
            { report( tag, 0, "seed %u: size %u != convert reslen %u", seed, needed, ref_len ); break; }
        st = utf8_mbstowcs( d, needed ? needed - 1 : 0, &ref_len, src_buf, len );
        if (needed && st != STATUS_BUFFER_TOO_SMALL)
            { report( tag, 0, "seed %u: size-1 buffer status %08x", seed, (unsigned int)st ); break; }
    }

    /* split surrogate pair: high surrogate written, low surrogate must not fit */
    {
        unsigned int n = 0;
        n += (unsigned int)put_utf8( src_buf + n, 'a' );
        n += (unsigned int)put_utf8( src_buf + n, 0x1f600 );
        for (dstlen = 0; dstlen <= 4; dstlen++) compare_conv( tag, src_buf, n, dstlen );

        /* 16 ASCII bytes then an astral cp, cut right after the high surrogate */
        memset( src_buf, 'a', 16 );
        n = 16 + (unsigned int)put_utf8( src_buf + 16, 0x1f600 );
        for (dstlen = 0; dstlen <= 20; dstlen++) compare_conv( tag, src_buf, n, dstlen );

        /* astral cp landing exactly at the end of a vector block */
        memset( src_buf, 'a', 15 );
        n = 15 + (unsigned int)put_utf8( src_buf + 15, 0x1f600 );
        for (dstlen = 0; dstlen <= 20; dstlen++) compare_conv( tag, src_buf, n, dstlen );
    }
    printf("  buffer-too-small sweep + split surrogate pairs: %d checks, %d failures\n", g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Test 7: unaligned source and destination pointers
 * ------------------------------------------------------------------ */

static void test_alignment( void )
{
    static const char tag[] = "align";
    int ck = g_checks, fc = g_failures;
    static char raw[MAXSRC + 64];
    unsigned int len, so, dof, seed;

    for (seed = 1; seed <= 6; seed++)
    {
        build_mixed( 300, seed, NULL, &len );
        memcpy( raw + 7, src_buf, len );          /* deliberately unaligned copy */

        for (so = 0; so < 16; so++)
            for (dof = 0; dof < 16; dof++)
            {
                NTSTATUS st_a, st_b;
                unsigned int la, lb;

                g_checks++;
                st_a = utf8_mbstowcs( dst_simd + GUARD + dof, len + 24, &la, raw + so, len );
                st_b = scalar_mbstowcs( dst_ref + GUARD + dof, len + 24, &lb, raw + so, len );
                if (st_a != st_b || la != lb)
                    { report( tag, 0, "seed %u src+%u dst+%u: %08x/%08x %u/%u",
                              seed, so, dof, (unsigned int)st_a, (unsigned int)st_b, la, lb ); }
                if (memcmp( dst_simd + GUARD + dof, dst_ref + GUARD + dof, la * sizeof(WCHAR) ))
                    { report( tag, 0, "seed %u src+%u dst+%u: output differs", seed, so, dof ); }
            }
    }

    /* Guard-page test: place the source so its last byte is the last byte of a
       committed page, with the next page set PAGE_NOACCESS. Any vector load that
       reads past srcend faults immediately. */
    {
        SYSTEM_INFO si;
        DWORD pagesz, old;
        char *region;
        unsigned int i, l;

        GetSystemInfo( &si );
        pagesz = si.dwPageSize;
        region = (char *)VirtualAlloc( NULL, pagesz * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
        if (!region) { report( tag, 0, "VirtualAlloc failed" ); }
        else
        {
            VirtualProtect( region + pagesz, pagesz, PAGE_NOACCESS, &old );
            for (l = 1; l <= 128; l++)
            {
                char *p = region + pagesz - l;
                for (i = 0; i < l; i++) p[i] = 'q';                /* pure ASCII run */
                compare_all( tag, p, l, l + 8 );
                p = region + pagesz - l;
                for (i = 0; i < l; i++) p[i] = 'q';
                p[l - 1] = (char)0xa9;                             /* invalid tail byte */
                compare_all( tag, p, l, l + 8 );
                p = region + pagesz - l;
                for (i = 0; i < l; i++) p[i] = 'q';
                if (l >= 3) { p[l - 3] = (char)0xc2; p[l - 2] = (char)0xa9; }  /* 2-byte seq at end */
                compare_all( tag, p, l, l + 8 );
            }
            VirtualFree( region, 0, MEM_RELEASE );
        }
    }
    printf("  unaligned src (16 offsets) x dst (16 offsets) + page-edge reads: %d checks, %d failures\n",
           g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Test 8: seeded fuzz
 * ------------------------------------------------------------------ */

static void test_fuzz( unsigned int rounds )
{
    static const char tag[] = "fuzz";
    unsigned int r, len, i, kind, dstlen;
    int ck = g_checks, fc = g_failures;

    for (r = 0; r < rounds; r++)
    {
        kind = rng_next( 4 );
        len = 1 + rng_next( 300 );

        switch (kind)
        {
        case 0:  /* pure random bytes (mostly invalid) */
            for (i = 0; i < len; i++) src_buf[i] = (char)rng_next( 256 );
            break;
        case 1:  /* ASCII with random invalid bytes sprinkled in */
            for (i = 0; i < len; i++) src_buf[i] = (char)('a' + rng_next( 26 ));
            for (i = 0; i < len / 8 + 1; i++) src_buf[rng_next( len )] = (char)rng_next( 256 );
            break;
        case 2:  /* valid UTF-8 mix */
            build_mixed( 100, 0x1234 + r, NULL, &len );
            break;
        default: /* ASCII runs with embedded valid multibyte, biased to block edges */
            for (i = 0; i < len; i++) src_buf[i] = 'z';
            {
                unsigned int n = 0, hops = 1 + rng_next( 12 );
                for (i = 0; i < hops && n + 4 < len; i++)
                {
                    n = rng_next( len - 4 );
                    n += (unsigned int)put_utf8( src_buf + n, (unsigned int)cp_pool[rng_next( (unsigned int)(sizeof(cp_pool)/sizeof(cp_pool[0])) )] );
                }
            }
            break;
        }

        compare_size( tag, src_buf, len );
        dstlen = len + 8;
        compare_conv( tag, src_buf, len, dstlen );
        compare_conv( tag, src_buf, len, rng_next( len + 1 ) );
        compare_conv( tag, src_buf, len, 16 );
        compare_conv( tag, src_buf, len, 32 );
        compare_conv( tag, src_buf, len, 0 );
    }
    printf("  seeded fuzz (%u rounds x 6 buffer sizes): %d checks, %d failures\n", rounds, g_checks - ck, g_failures - fc );
}

/* ------------------------------------------------------------------ *
 * Benchmark payloads
 * ------------------------------------------------------------------ */

typedef struct
{
    const char *name;
    char  *data;
    unsigned int len;
} payload_t;

static char *make_payload_json( unsigned int target, unsigned int *len_out )
{
    static const char *words[] = { "alpha","bravo","charlie","delta","echo","foxtrot","golf","hotel","india","juliet" };
    char *buf = malloc( target + 4096 );
    unsigned int n = 0, i = 0;

    while (n + 256 < target)
    {
        n += (unsigned int)sprintf( buf + n,
            "{\"id\":%u,\"name\":\"%s %s\",\"path\":\"/api/v1/%s/%u/items\","
            "\"ok\":true,\"msg\":\"the quick brown fox jumps over the lazy dog %u\"},",
            i, words[i % 10], words[(i * 3) % 10], words[(i * 7) % 10], i, i * 7 );
        i++;
    }
    *len_out = n;
    return buf;
}

static char *make_payload_html( unsigned int target, unsigned int *len_out )
{
    char *buf = malloc( target + 4096 );
    unsigned int n = 0, i = 0;

    while (n + 512 < target)
    {
        n += (unsigned int)sprintf( buf + n,
            "<div class=\"row\" id=\"r%u\"><span class=\"cell\">%u</span>"
            "<a href=\"https://example.com/a/b/c?x=%u&amp;y=%u\">link text %u</a>"
            "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
            "tempor incididunt ut labore et dolore magna aliqua %u</p></div>\n",
            i, i, i, i * 3, i, i * 5 );
        i++;
    }
    *len_out = n;
    return buf;
}

/* Cyrillic + emoji + CJK mixed with ASCII: realistic chat/JSON traffic */
static char *make_payload_mixed( unsigned int target, unsigned int *len_out )
{
    char *buf = malloc( target + 4096 );
    unsigned int n = 0, i = 0, j;
    static const unsigned int heavy[] = { 0x410,0x430,0x444,0x44b,0x451,0x401,0x1f600,0x1f680,0x4e2d,0x6587,0x1f4a9 };

    while (n + 512 < target)
    {
        n += (unsigned int)sprintf( buf + n, "{\"u\":%u,\"t\":\"", i );
        for (j = 0; j < 12; j++)
        {
            n += (unsigned int)put_utf8( buf + n, heavy[(i + j) % (sizeof(heavy)/sizeof(heavy[0]))] );
            n += (unsigned int)sprintf( buf + n, " word%u ", j );
        }
        n += (unsigned int)sprintf( buf + n, "\",\"x\":true}," );
        i++;
    }
    *len_out = n;
    return buf;
}

/* ASCII dominated with a little multibyte: the common real-world case */
static char *make_payload_mostly_ascii( unsigned int target, unsigned int *len_out )
{
    char *buf = malloc( target + 4096 );
    unsigned int n = 0, i = 0;

    while (n + 128 < target)
    {
        memcpy( buf + n, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()", 70 );
        n += 70;
        n += (unsigned int)put_utf8( buf + n, 0x410 + (i % 32) );
        memcpy( buf + n, " more ascii text here", 21 );
        n += 21;
        i++;
    }
    *len_out = n;
    return buf;
}

static double now_seconds( void )
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter( &c );
    QueryPerformanceFrequency( &f );
    return (double)c.QuadPart / (double)f.QuadPart;
}

static void bench_payload( const payload_t *p, int size_only )
{
    unsigned int res, iters, it, i;
    double t0, t1, best = 1e30, secs, mbs;
    WCHAR *dst = malloc( (p->len + 64) * sizeof(WCHAR) );
    NTSTATUS st = STATUS_SUCCESS;

    /* calibrate: ~0.15 s per sample */
    {
        unsigned int n = 0;
        t0 = now_seconds();
        do
        {
            if (size_only) utf8_mbstowcs_size( p->data, p->len, &res );
            else utf8_mbstowcs( dst, p->len + 64, &res, p->data, p->len );
            n++;
        } while (now_seconds() - t0 < 0.02);
        iters = (unsigned int)(0.15 / (now_seconds() - t0) * n) + 1;
        if (iters > 100000) iters = 100000;
    }

    for (it = 0; it < 5; it++)
    {
        t0 = now_seconds();
        for (i = 0; i < iters; i++)
        {
            if (size_only) utf8_mbstowcs_size( p->data, p->len, &res );
            else utf8_mbstowcs( dst, p->len + 64, &res, p->data, p->len );
        }
        t1 = now_seconds();
        if (t1 - t0 < best) best = t1 - t0;
    }
    secs = best / iters;
    mbs = (double)p->len / secs / 1e6;
    printf( "  %-22s %-16s %8.2f MB  %9.3f us/iter  %10.1f MB/s  (status %08x, reslen %u)\n",
            p->name, size_only ? "size" : "convert", (double)p->len / 1e6,
            secs * 1e6, mbs, (unsigned int)st, res );
    fflush( stdout );
    free( dst );
}

/* scalar reference throughput, same binary, same flags */
static void bench_ref_payload( const payload_t *p, int size_only )
{
    unsigned int res, iters, it, i;
    double t0, t1, best = 1e30, secs, mbs;
    WCHAR *dst = malloc( (p->len + 64) * sizeof(WCHAR) );

    {
        unsigned int n = 0;
        t0 = now_seconds();
        do
        {
            if (size_only) scalar_mbstowcs_size( p->data, p->len, &res );
            else scalar_mbstowcs( dst, p->len + 64, &res, p->data, p->len );
            n++;
        } while (now_seconds() - t0 < 0.02);
        iters = (unsigned int)(0.15 / (now_seconds() - t0) * n) + 1;
        if (iters > 100000) iters = 100000;
    }

    for (it = 0; it < 5; it++)
    {
        t0 = now_seconds();
        for (i = 0; i < iters; i++)
        {
            if (size_only) scalar_mbstowcs_size( p->data, p->len, &res );
            else scalar_mbstowcs( dst, p->len + 64, &res, p->data, p->len );
        }
        t1 = now_seconds();
        if (t1 - t0 < best) best = t1 - t0;
    }
    secs = best / iters;
    mbs = (double)p->len / secs / 1e6;
    printf( "  %-22s %-16s %8.2f MB  %9.3f us/iter  %10.1f MB/s  (scalar reference)\n",
            p->name, size_only ? "size" : "convert", (double)p->len / 1e6,
            secs * 1e6, mbs );
    fflush( stdout );
    free( dst );
}

int main( int argc, char **argv )
{
    unsigned int i;
    unsigned int fuzz_rounds = 30000;
    unsigned int payload_mb = 8;
    payload_t payloads[4];
    int npayloads = 0;
    int total_checks = 0, total_fail = 0;
#ifdef HAVE_SSE2_UTF8
    const char *mode = "SSE2 vector fast path ENABLED";
#elif defined(NO_SSE2_UTF8)
    const char *mode = "SSE2 vector fast path DISABLED (NO_SSE2_UTF8 baseline build)";
#else
    const char *mode = "scalar only (no SSE2 target available)";
#endif

    for (i = 1; i < (unsigned int)argc; i++)
    {
        if (!strcmp( argv[i], "-v" )) g_verbose = 1;
        else if (!strcmp( argv[i], "-f" ) && i + 1 < (unsigned int)argc) fuzz_rounds = (unsigned int)atoi( argv[++i] );
        else if (!strcmp( argv[i], "-m" ) && i + 1 < (unsigned int)argc) payload_mb = (unsigned int)atoi( argv[++i] );
        else fprintf( stderr, "unknown option %s (use -v, -f rounds, -m MB)\n", argv[i] );
    }

    printf( "=== Wine ntdll UTF-8 -> UTF-16 conversion: verification & benchmark ===\n" );
    printf( "build mode: %s\n\n", mode );

    printf( "[verification]\n" );
    test_pure_ascii();
    test_mixed();
    test_first_nonascii_boundary();
    test_boundary_lengths();
    test_malformed();
    test_buffer_too_small();
    test_alignment();
    test_fuzz( fuzz_rounds );

    printf( "\n  total: %d comparisons, %d failures -> %s\n",
            g_checks, g_failures, g_failures ? "MISMATCH" : "100% equivalence" );

    printf( "\n[benchmark]  best of 5 samples\n" );
    payloads[npayloads].name = "json-ascii";
    payloads[npayloads].data = make_payload_json( payload_mb * 1024 * 1024, &payloads[npayloads].len );
    npayloads++;
    payloads[npayloads].name = "html-ascii";
    payloads[npayloads].data = make_payload_html( payload_mb * 1024 * 1024, &payloads[npayloads].len );
    npayloads++;
    payloads[npayloads].name = "mostly-ascii-1pct";
    payloads[npayloads].data = make_payload_mostly_ascii( payload_mb * 1024 * 1024, &payloads[npayloads].len );
    npayloads++;
    payloads[npayloads].name = "cyrillic-emoji-mix";
    payloads[npayloads].data = make_payload_mixed( payload_mb * 1024 * 1024, &payloads[npayloads].len );
    npayloads++;

    for (i = 0; i < (unsigned int)npayloads; i++)
    {
        bench_payload( &payloads[i], 1 );
        bench_payload( &payloads[i], 0 );
    }
    printf( "\n[benchmark]  scalar reference baseline (identical source, pre-vector code)\n" );
    for (i = 0; i < (unsigned int)npayloads; i++)
    {
        bench_ref_payload( &payloads[i], 1 );
        bench_ref_payload( &payloads[i], 0 );
    }

    for (i = 0; i < (unsigned int)npayloads; i++) free( payloads[i].data );

    total_checks = g_checks;
    total_fail = g_failures;
    printf( "\n%s: %d checks, %d failures\n", g_failures ? "RESULT FAIL" : "RESULT PASS",
            total_checks, total_fail );
    return g_failures ? 1 : 0;
}
