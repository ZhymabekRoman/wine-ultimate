/*
 * Hardware AES (AES-NI) backend for bcrypt symmetric keys.
 *
 * Copyright 2026 Wine Project
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

#include <stdlib.h>
#include <string.h>

#include "bcrypt_internal.h"

#ifdef BCRYPT_HAVE_AESNI

#if defined(__clang__) || defined(__GNUC__)
#define AESNI_API __attribute__((target( "aes,sse4.1" )))
#else
#define AESNI_API
#endif

static BOOL aesni_supported(void)
{
    static BOOL cached = -1;

    if (cached == -1)
    {
#if defined(__x86_64__) || defined(__i386__)
        unsigned int eax = 1, ebx = 0, ecx = 0, edx = 0;
        __asm__( "cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (eax), "c" (0) );
        /* CPUID.1:ECX bit 25 (AES) and EDX bit 26 (SSE2) */
        cached = !!(ecx & (1u << 25)) && !!(edx & (1u << 26));
#else
        cached = FALSE;
#endif
    }
    return cached;
}

static inline void store( UCHAR *dst, __m128i v )
{
    _mm_storeu_si128( (__m128i *)dst, v );
}

static inline __m128i load( const UCHAR *src )
{
    return _mm_loadu_si128( (const __m128i *)src );
}

static inline __m128i AESNI_API aes_128_key_expand(__m128i key, __m128i keygened)
{
    keygened = _mm_shuffle_epi32(keygened, _MM_SHUFFLE(3, 3, 3, 3));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, keygened);
}

static inline void AESNI_API aes_256_key_expand(__m128i *key1, __m128i *key2, __m128i keygened)
{
    __m128i temp = _mm_shuffle_epi32(keygened, _MM_SHUFFLE(3, 3, 3, 3));
    *key1 = _mm_xor_si128(*key1, _mm_slli_si128(*key1, 4));
    *key1 = _mm_xor_si128(*key1, _mm_slli_si128(*key1, 4));
    *key1 = _mm_xor_si128(*key1, _mm_slli_si128(*key1, 4));
    *key1 = _mm_xor_si128(*key1, temp);

    temp = _mm_shuffle_epi32(_mm_aeskeygenassist_si128(*key1, 0), _MM_SHUFFLE(2, 2, 2, 2));
    *key2 = _mm_xor_si128(*key2, _mm_slli_si128(*key2, 4));
    *key2 = _mm_xor_si128(*key2, _mm_slli_si128(*key2, 4));
    *key2 = _mm_xor_si128(*key2, _mm_slli_si128(*key2, 4));
    *key2 = _mm_xor_si128(*key2, temp);
}

/* Builds the 128/192/256 bit key schedules. Returns the AES round count. */
static unsigned int AESNI_API aesni_expand( __m128i *enc, __m128i *dec, const UCHAR *secret, ULONG secret_len )
{
    unsigned int rounds, i;

    switch (secret_len)
    {
    case 16:
        rounds = 10;
        enc[0] = load( secret );
        enc[1] = aes_128_key_expand( enc[0], _mm_aeskeygenassist_si128( enc[0], 0x01 ) );
        enc[2] = aes_128_key_expand( enc[1], _mm_aeskeygenassist_si128( enc[1], 0x02 ) );
        enc[3] = aes_128_key_expand( enc[2], _mm_aeskeygenassist_si128( enc[2], 0x04 ) );
        enc[4] = aes_128_key_expand( enc[3], _mm_aeskeygenassist_si128( enc[3], 0x08 ) );
        enc[5] = aes_128_key_expand( enc[4], _mm_aeskeygenassist_si128( enc[4], 0x10 ) );
        enc[6] = aes_128_key_expand( enc[5], _mm_aeskeygenassist_si128( enc[5], 0x20 ) );
        enc[7] = aes_128_key_expand( enc[6], _mm_aeskeygenassist_si128( enc[6], 0x40 ) );
        enc[8] = aes_128_key_expand( enc[7], _mm_aeskeygenassist_si128( enc[7], 0x80 ) );
        enc[9] = aes_128_key_expand( enc[8], _mm_aeskeygenassist_si128( enc[8], 0x1b ) );
        enc[10] = aes_128_key_expand( enc[9], _mm_aeskeygenassist_si128( enc[9], 0x36 ) );
        break;

    case 32:
    {
        __m128i k1, k2;
        rounds = 14;
        enc[0] = k1 = load( secret );
        enc[1] = k2 = load( secret + 16 );
        aes_256_key_expand( &k1, &k2, _mm_aeskeygenassist_si128( k2, 0x01 ) ); enc[2] = k1; enc[3] = k2;
        aes_256_key_expand( &k1, &k2, _mm_aeskeygenassist_si128( k2, 0x02 ) ); enc[4] = k1; enc[5] = k2;
        aes_256_key_expand( &k1, &k2, _mm_aeskeygenassist_si128( k2, 0x04 ) ); enc[6] = k1; enc[7] = k2;
        aes_256_key_expand( &k1, &k2, _mm_aeskeygenassist_si128( k2, 0x08 ) ); enc[8] = k1; enc[9] = k2;
        aes_256_key_expand( &k1, &k2, _mm_aeskeygenassist_si128( k2, 0x10 ) ); enc[10] = k1; enc[11] = k2;
        aes_256_key_expand( &k1, &k2, _mm_aeskeygenassist_si128( k2, 0x20 ) ); enc[12] = k1; enc[13] = k2;
        k1 = _mm_xor_si128( k1, _mm_slli_si128( k1, 4 ) );
        k1 = _mm_xor_si128( k1, _mm_slli_si128( k1, 4 ) );
        k1 = _mm_xor_si128( k1, _mm_slli_si128( k1, 4 ) );
        enc[14] = _mm_xor_si128( k1, _mm_shuffle_epi32( _mm_aeskeygenassist_si128( k2, 0x40 ), _MM_SHUFFLE( 3, 3, 3, 3 ) ) );
        break;
    }

    default:
        return 0;
    }

    /* The AESDEC instruction expects the InvMixColumns transformed keys, in reverse order. */
    dec[0] = enc[rounds];
    for (i = 1; i < rounds; i++) dec[i] = _mm_aesimc_si128( enc[rounds - i] );
    dec[rounds] = enc[0];

    return rounds;
}

void aesni_prepare_key( struct key *key )
{
    unsigned int rounds;

    if (key->alg_id != ALG_ID_AES) return;
    if (key->u.s.mode != CHAIN_MODE_ECB && key->u.s.mode != CHAIN_MODE_CBC) return;
    if (!aesni_supported()) return;

    rounds = aesni_expand( key->u.s.round_keys_enc, key->u.s.round_keys_dec, key->u.s.secret, key->u.s.secret_len );
    key->u.s.aesni_rounds = rounds;
}

AESNI_API void aesni_encrypt_block( const struct key *key, const UCHAR *input, UCHAR *output, UCHAR *chain )
{
    const unsigned int rounds = key->u.s.aesni_rounds;
    const __m128i *rk = key->u.s.round_keys_enc;
    __m128i state;
    unsigned int i;

    state = load( input );
    if (key->u.s.mode == CHAIN_MODE_CBC) state = _mm_xor_si128( state, load( chain ) );
    state = _mm_xor_si128( state, rk[0] );
    for (i = 1; i < rounds; i++) state = _mm_aesenc_si128( state, rk[i] );
    state = _mm_aesenclast_si128( state, rk[rounds] );

    store( output, state );
    if (key->u.s.mode == CHAIN_MODE_CBC) store( chain, state );
}

AESNI_API void aesni_decrypt_block( const struct key *key, const UCHAR *input, UCHAR *output, UCHAR *chain )
{
    const unsigned int rounds = key->u.s.aesni_rounds;
    const __m128i *rk = key->u.s.round_keys_dec;
    __m128i state, next;
    unsigned int i;

    next = load( input );
    state = _mm_xor_si128( next, rk[0] );
    for (i = 1; i < rounds; i++) state = _mm_aesdec_si128( state, rk[i] );
    state = _mm_aesdeclast_si128( state, rk[rounds] );
    if (key->u.s.mode == CHAIN_MODE_CBC) state = _mm_xor_si128( state, load( chain ) );

    store( output, state );
    if (key->u.s.mode == CHAIN_MODE_CBC) store( chain, next );
}

#endif /* BCRYPT_HAVE_AESNI */
