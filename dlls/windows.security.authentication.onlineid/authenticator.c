/* WinRT Windows.Security.Authentication.Onlineid Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "bcrypt.h"
#include "wincrypt.h"
#include "winhttp.h"
#include "winreg.h"
#include "objbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(onlineid);

#define DEVICE_RSA_BLOB_SIZE 539
#define TICKET_CACHE_SLOTS 8
#define TICKET_CACHE_TTL_MS 300000

struct device_cred
{
    BOOL loaded;
    char username[32];
    char puid[64];
    BYTE rsa_blob[DEVICE_RSA_BLOB_SIZE];
};

struct ticket_cache_entry
{
    WCHAR *service;
    WCHAR *policy;
    WCHAR *ticket;
    WCHAR *puid;
    ULONGLONG tick_ms;
};

static CRITICAL_SECTION onlineid_cs = { NULL, -1, 0, 0, 0, 0 };
static struct device_cred g_device_cred;
static struct ticket_cache_entry g_ticket_cache[TICKET_CACHE_SLOTS];
static UINT32 g_next_op_id = 1;

static const WCHAR idcrl_user_agent[] =
    L"Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 10.0; Win64; .NET4.0C; .NET4.0E; "
    L"IDCRL 24.10.0.19044.0.0; IDCRL-cfg 16.000.29743.00; "
    L"App svchost.exe, 10.0.19041.546, {DF60E2DF-88AD-4526-AE21-83D130EF0F68})";

static const WCHAR reg_onlineid_key[] = L"Software\\Wine\\OnlineId";
static const char ppauth_digest_b64[] = "t5asfqFBe60nFJWZVeFsf+1VSF89/PX1hlHo1/19AuU=";

static DWORD read_be32( const BYTE *p )
{
    return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3];
}

static void write_be32( BYTE *p, DWORD v )
{
    p[0] = (BYTE)((v >> 24) & 0xff);
    p[1] = (BYTE)((v >> 16) & 0xff);
    p[2] = (BYTE)((v >> 8) & 0xff);
    p[3] = (BYTE)(v & 0xff);
}

static DWORD read_le32( const BYTE *p )
{
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static void bytes_to_hex( const BYTE *in, SIZE_T len, char *out )
{
    static const char hex[] = "0123456789abcdef";
    SIZE_T i;

    for (i = 0; i < len; i++)
    {
        out[i * 2] = hex[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[in[i] & 0xf];
    }
    out[len * 2] = 0;
}

static char *wide_to_utf8( const WCHAR *w )
{
    int len;
    char *buf;

    if (!w) w = L"";
    len = WideCharToMultiByte( CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL );
    if (len <= 0) return NULL;
    if (!(buf = malloc( len ))) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, w, -1, buf, len, NULL, NULL );
    return buf;
}

static WCHAR *utf8_to_wide( const char *s )
{
    int len;
    WCHAR *buf;

    if (!s) s = "";
    len = MultiByteToWideChar( CP_UTF8, 0, s, -1, NULL, 0 );
    if (len <= 0) return NULL;
    if (!(buf = malloc( (SIZE_T)len * sizeof(WCHAR) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, s, -1, buf, len );
    return buf;
}

static char *xml_escape( const char *s )
{
    SIZE_T cap = 64, len = 0, i;
    char *out;

    if (!s) s = "";
    cap = strlen( s ) * 6 + 1;
    if (!(out = malloc( cap ))) return NULL;
    for (i = 0; s[i]; i++)
    {
        const char *rep = NULL;
        switch (s[i])
        {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&apos;"; break;
        default: break;
        }
        if (rep)
        {
            SIZE_T rlen = strlen( rep );
            memcpy( out + len, rep, rlen );
            len += rlen;
        }
        else out[len++] = s[i];
    }
    out[len] = 0;
    return out;
}

static void xml_unescape_inplace( char *s )
{
    char *r = s, *w = s;
    while (*r)
    {
        if (*r == '&')
        {
            if (!strncmp( r, "&amp;", 5 ))       { *w++ = '&';  r += 5; continue; }
            if (!strncmp( r, "&lt;", 4 ))        { *w++ = '<';  r += 4; continue; }
            if (!strncmp( r, "&gt;", 4 ))        { *w++ = '>';  r += 4; continue; }
            if (!strncmp( r, "&quot;", 6 ))      { *w++ = '"';  r += 6; continue; }
            if (!strncmp( r, "&apos;", 6 ))      { *w++ = '\''; r += 6; continue; }
        }
        *w++ = *r++;
    }
    *w = 0;
}

static char *base64_encode_nocrlf( const BYTE *data, DWORD len )
{
    DWORD out_len = 0;
    char *buf;

    if (!CryptBinaryToStringA( data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &out_len ))
        return NULL;
    if (!(buf = malloc( out_len + 1 ))) return NULL;
    if (!CryptBinaryToStringA( data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, buf, &out_len ))
    {
        free( buf );
        return NULL;
    }
    buf[out_len] = 0;
    return buf;
}

static BOOL sha256_hash( const BYTE *data, DWORD len, BYTE out[32] )
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    BOOL ok = FALSE;

    if (BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 ) != 0)
        return FALSE;
    if (BCryptCreateHash( alg, &h, NULL, 0, NULL, 0, 0 ) == 0 &&
        BCryptHashData( h, (PUCHAR)data, len, 0 ) == 0 &&
        BCryptFinishHash( h, out, 32, 0 ) == 0)
        ok = TRUE;
    if (h) BCryptDestroyHash( h );
    BCryptCloseAlgorithmProvider( alg, 0 );
    return ok;
}

static BOOL http_post_xml( const WCHAR *host, const WCHAR *path, const WCHAR *content_type,
                           const char *body, DWORD body_len, char **resp_out, DWORD *resp_len_out,
                           DWORD *http_status_out )
{
    HINTERNET ses = NULL, con = NULL, req = NULL;
    WCHAR hdrs[160];
    BYTE *buf = NULL;
    DWORD cap = 0, total = 0, status = 0, slen = sizeof(status);
    BOOL ok = FALSE;

    *resp_out = NULL;
    if (resp_len_out) *resp_len_out = 0;
    if (http_status_out) *http_status_out = 0;

    if (!(ses = WinHttpOpen( idcrl_user_agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 )))
        goto done;
    if (!(con = WinHttpConnect( ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0 )))
        goto done;
    if (!(req = WinHttpOpenRequest( con, L"POST", path, NULL, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE )))
        goto done;

    WinHttpSetTimeouts( req, 10000, 10000, 15000, 15000 );
    swprintf( hdrs, ARRAY_SIZE(hdrs), L"Content-Type: %s", content_type );
    if (!WinHttpSendRequest( req, hdrs, -1L, (LPVOID)body, body_len, body_len, 0 ))
        goto done;
    if (!WinHttpReceiveResponse( req, NULL ))
        goto done;

    WinHttpQueryHeaders( req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX );
    if (http_status_out) *http_status_out = status;

    for (;;)
    {
        DWORD avail = 0, got = 0;
        BYTE *tmp;

        if (!WinHttpQueryDataAvailable( req, &avail )) break;
        if (!avail) { ok = TRUE; break; }
        if (total + avail + 1 > cap)
        {
            DWORD ncap = cap ? cap * 2 : 8192;
            while (ncap < total + avail + 1) ncap *= 2;
            if (!(tmp = realloc( buf, ncap ))) goto done;
            buf = tmp;
            cap = ncap;
        }
        if (!WinHttpReadData( req, buf + total, avail, &got )) goto done;
        if (!got) { ok = TRUE; break; }
        total += got;
    }

    if (ok)
    {
        if (!buf)
        {
            if (!(buf = malloc( 1 ))) goto done;
        }
        buf[total] = 0;
        *resp_out = (char *)buf;
        if (resp_len_out) *resp_len_out = total;
        buf = NULL;
    }

done:
    free( buf );
    if (req) WinHttpCloseHandle( req );
    if (con) WinHttpCloseHandle( con );
    if (ses) WinHttpCloseHandle( ses );
    return ok;
}

static BOOL extract_xml_tag( const char *xml, const char *tag_local, char *out, SIZE_T out_cap )
{
    char open_pat1[96], open_pat2[96], close_pat[96];
    const char *p, *gt, *end;
    SIZE_T len;

    snprintf( open_pat1, sizeof(open_pat1), "<%s", tag_local );
    snprintf( open_pat2, sizeof(open_pat2), ":%s", tag_local );

    p = xml;
    while ((p = strchr( p, '<' )))
    {
        if (p[1] == '/') { p++; continue; }
        if (!strncmp( p, open_pat1, strlen( open_pat1 ) ))
        {
            char next = p[strlen( open_pat1 )];
            if (next == '>' || next == ' ' || next == '\t' || next == '\r' || next == '\n')
                break;
        }
        else
        {
            const char *colon = strchr( p + 1, ':' );
            const char *space_or_gt = strpbrk( p + 1, " \t\r\n>" );
            if (colon && (!space_or_gt || colon < space_or_gt) &&
                !strncmp( colon, open_pat2, strlen( open_pat2 ) ))
            {
                char next = colon[strlen( open_pat2 )];
                if (next == '>' || next == ' ' || next == '\t' || next == '\r' || next == '\n')
                    break;
            }
        }
        p++;
    }
    if (!p) return FALSE;
    if (!(gt = strchr( p, '>' ))) return FALSE;
    if (gt > p && gt[-1] == '/') return FALSE;
    gt++;

    snprintf( close_pat, sizeof(close_pat), "%s>", tag_local );
    end = gt;
    while ((end = strstr( end, "</" )))
    {
        const char *name = end + 2;
        const char *colon = strchr( name, ':' );
        const char *gt2 = strchr( name, '>' );
        if (colon && gt2 && colon < gt2) name = colon + 1;
        if (!strncmp( name, close_pat, strlen( close_pat ) ))
            break;
        end += 2;
    }
    if (!end || end < gt) return FALSE;
    len = (SIZE_T)(end - gt);
    if (len >= out_cap) return FALSE;
    memcpy( out, gt, len );
    out[len] = 0;
    xml_unescape_inplace( out );
    return TRUE;
}

static char *extract_xml_tag_alloc( const char *xml, const char *tag_local )
{
    char open_pat1[96], open_pat2[96], close_pat[96];
    const char *p, *gt, *end;
    SIZE_T len;
    char *out;

    snprintf( open_pat1, sizeof(open_pat1), "<%s", tag_local );
    snprintf( open_pat2, sizeof(open_pat2), ":%s", tag_local );

    p = xml;
    while ((p = strchr( p, '<' )))
    {
        if (p[1] == '/') { p++; continue; }
        if (!strncmp( p, open_pat1, strlen( open_pat1 ) ))
        {
            char next = p[strlen( open_pat1 )];
            if (next == '>' || next == ' ' || next == '\t' || next == '\r' || next == '\n')
                break;
        }
        else
        {
            const char *colon = strchr( p + 1, ':' );
            const char *space_or_gt = strpbrk( p + 1, " \t\r\n>" );
            if (colon && (!space_or_gt || colon < space_or_gt) &&
                !strncmp( colon, open_pat2, strlen( open_pat2 ) ))
            {
                char next = colon[strlen( open_pat2 )];
                if (next == '>' || next == ' ' || next == '\t' || next == '\r' || next == '\n')
                    break;
            }
        }
        p++;
    }
    if (!p || !(gt = strchr( p, '>' )) || (gt > p && gt[-1] == '/')) return NULL;
    gt++;

    snprintf( close_pat, sizeof(close_pat), "%s>", tag_local );
    end = gt;
    while ((end = strstr( end, "</" )))
    {
        const char *name = end + 2;
        const char *colon = strchr( name, ':' );
        const char *gt2 = strchr( name, '>' );
        if (colon && gt2 && colon < gt2) name = colon + 1;
        if (!strncmp( name, close_pat, strlen( close_pat ) ))
            break;
        end += 2;
    }
    if (!end || end < gt) return NULL;
    len = (SIZE_T)(end - gt);
    if (!(out = malloc( len + 1 ))) return NULL;
    memcpy( out, gt, len );
    out[len] = 0;
    xml_unescape_inplace( out );
    return out;
}

static void read_reg_str_utf8( HKEY root, const WCHAR *subkey, const WCHAR *valname,
                               const char *fallback, char *out, SIZE_T out_cap )
{
    WCHAR wbuf[256];
    DWORD type = 0, cb = sizeof(wbuf);
    HKEY hk;

    if (RegOpenKeyExW( root, subkey, 0, KEY_READ, &hk ) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW( hk, valname, NULL, &type, (BYTE *)wbuf, &cb ) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ) && cb >= sizeof(WCHAR))
        {
            wbuf[ARRAY_SIZE(wbuf) - 1] = 0;
            if (WideCharToMultiByte( CP_UTF8, 0, wbuf, -1, out, (int)out_cap, NULL, NULL ) > 0 && out[0])
            {
                RegCloseKey( hk );
                return;
            }
        }
        RegCloseKey( hk );
    }
    snprintf( out, out_cap, "%s", fallback );
}

static BOOL load_device_cred_from_reg( struct device_cred *cred )
{
    HKEY hk;
    DWORD type = 0, cb;
    WCHAR wuser[64], wpuid[64];

    if (RegOpenKeyExW( HKEY_CURRENT_USER, reg_onlineid_key, 0, KEY_READ, &hk ) != ERROR_SUCCESS)
        return FALSE;

    cb = sizeof(wuser);
    if (RegQueryValueExW( hk, L"Username", NULL, &type, (BYTE *)wuser, &cb ) != ERROR_SUCCESS ||
        type != REG_SZ)
    {
        RegCloseKey( hk );
        return FALSE;
    }
    wuser[ARRAY_SIZE(wuser) - 1] = 0;
    if (WideCharToMultiByte( CP_UTF8, 0, wuser, -1, cred->username, sizeof(cred->username), NULL, NULL ) <= 0 ||
        !cred->username[0])
    {
        RegCloseKey( hk );
        return FALSE;
    }

    cb = sizeof(cred->rsa_blob);
    if (RegQueryValueExW( hk, L"RsaBlob", NULL, &type, cred->rsa_blob, &cb ) != ERROR_SUCCESS ||
        type != REG_BINARY || cb != DEVICE_RSA_BLOB_SIZE || memcmp( cred->rsa_blob, "RSA2", 4 ))
    {
        RegCloseKey( hk );
        return FALSE;
    }

    cb = sizeof(wpuid);
    if (RegQueryValueExW( hk, L"Puid", NULL, &type, (BYTE *)wpuid, &cb ) == ERROR_SUCCESS && type == REG_SZ)
    {
        wpuid[ARRAY_SIZE(wpuid) - 1] = 0;
        WideCharToMultiByte( CP_UTF8, 0, wpuid, -1, cred->puid, sizeof(cred->puid), NULL, NULL );
    }
    if (!cred->puid[0])
        snprintf( cred->puid, sizeof(cred->puid), "%s", cred->username );

    RegCloseKey( hk );
    cred->loaded = TRUE;
    return TRUE;
}

static void save_device_cred_to_reg( const struct device_cred *cred )
{
    HKEY hk;
    WCHAR wuser[64], wpuid[64];

    if (RegCreateKeyExW( HKEY_CURRENT_USER, reg_onlineid_key, 0, NULL, 0, KEY_WRITE, NULL, &hk, NULL ) != ERROR_SUCCESS)
        return;

    MultiByteToWideChar( CP_UTF8, 0, cred->username, -1, wuser, ARRAY_SIZE(wuser) );
    MultiByteToWideChar( CP_UTF8, 0, cred->puid, -1, wpuid, ARRAY_SIZE(wpuid) );
    RegSetValueExW( hk, L"Username", 0, REG_SZ, (const BYTE *)wuser, (wcslen( wuser ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hk, L"Puid", 0, REG_SZ, (const BYTE *)wpuid, (wcslen( wpuid ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hk, L"RsaBlob", 0, REG_BINARY, cred->rsa_blob, DEVICE_RSA_BLOB_SIZE );
    RegCloseKey( hk );
}

static BOOL decrypt_sp_license_rsa( const BYTE *lic, DWORD lic_len, BYTE out_rsa[DEVICE_RSA_BLOB_SIZE] )
{
    DWORD pos = 8;
    const BYTE *lic_data = NULL, *enc_key, *cfg;
    DWORD tag_len = 0, dec_len = 0;
    BYTE aes_key[16], iv[16], dec[544];
    BCRYPT_ALG_HANDLE aes_alg = NULL;
    BCRYPT_KEY_HANDLE aes_key_h = NULL;
    BOOL ok = FALSE;

    while (pos + 8 <= lic_len)
    {
        DWORD tag = read_le32( lic + pos );
        tag_len = read_le32( lic + pos + 4 );
        if (pos + 8 + tag_len > lic_len) return FALSE;
        if (tag == 0x12d)
        {
            lic_data = lic + pos + 8;
            break;
        }
        pos += 8 + tag_len;
    }
    if (!lic_data || tag_len < 548 + 232) return FALSE;

    enc_key = lic_data + 4;
    cfg = lic_data + 548;

    write_be32( aes_key + 0,  read_be32( cfg + 0xE0 ) ^ read_be32( cfg + 0xB8 ) ^ 0x97DCBF2Eu );
    write_be32( aes_key + 4,  read_be32( cfg + 0xBC ) ^ read_be32( cfg + 0x90 ) ^ 0x390E08DFu );
    write_be32( aes_key + 8,  read_be32( cfg + 0xCC ) ^ read_be32( cfg + 0xA0 ) ^ 0x4CC8E847u );
    write_be32( aes_key + 12, read_be32( cfg + 0xA4 ) ^ read_be32( cfg + 0x78 ) ^ 0xEC8C2837u );

    memset( iv, 0, sizeof(iv) );
    if (BCryptOpenAlgorithmProvider( &aes_alg, BCRYPT_AES_ALGORITHM, NULL, 0 ) != 0)
        return FALSE;
    if (BCryptSetProperty( aes_alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                           sizeof(BCRYPT_CHAIN_MODE_CBC), 0 ) == 0 &&
        BCryptGenerateSymmetricKey( aes_alg, &aes_key_h, NULL, 0, aes_key, sizeof(aes_key), 0 ) == 0 &&
        BCryptDecrypt( aes_key_h, (PUCHAR)enc_key, 544, NULL, iv, sizeof(iv), dec, sizeof(dec), &dec_len, 0 ) == 0 &&
        dec_len >= DEVICE_RSA_BLOB_SIZE && !memcmp( dec, "RSA2", 4 ))
    {
        memcpy( out_rsa, dec, DEVICE_RSA_BLOB_SIZE );
        ok = TRUE;
    }

    if (aes_key_h) BCryptDestroyKey( aes_key_h );
    BCryptCloseAlgorithmProvider( aes_alg, 0 );
    return ok;
}

static BOOL register_device_credential( struct device_cred *cred, HRESULT *err_hr,
                                        OnlineIdSystemTicketStatus *status_out )
{
    BYTE rand_user[14], rand_pass[15];
    char password[32], bios[128], mfg[128], prod[128], mguid[128], disk_str[256];
    char *b_disk = NULL, *b_bios = NULL, *b_mfg = NULL, *b_prod = NULL, *b_ser = NULL, *b_uuid = NULL;
    GUID dev_guid;
    char req_xml[4096];
    char *resp = NULL, *lic_b64 = NULL;
    BYTE *lic = NULL;
    DWORD resp_len = 0, http_status = 0, lic_len = 0;
    BOOL ok = FALSE;

    if (BCryptGenRandom( NULL, rand_user, sizeof(rand_user), BCRYPT_USE_SYSTEM_PREFERRED_RNG ) != 0 ||
        BCryptGenRandom( NULL, rand_pass, sizeof(rand_pass), BCRYPT_USE_SYSTEM_PREFERRED_RNG ) != 0)
    {
        *err_hr = E_FAIL;
        *status_out = OnlineIdSystemTicketStatus_Error;
        return FALSE;
    }
    bytes_to_hex( rand_user, sizeof(rand_user), cred->username );
    bytes_to_hex( rand_pass, sizeof(rand_pass), password );

    read_reg_str_utf8( HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemBiosVersion",
                       "Wine", bios, sizeof(bios) );
    read_reg_str_utf8( HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer",
                       "Wine", mfg, sizeof(mfg) );
    read_reg_str_utf8( HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName",
                       "PC", prod, sizeof(prod) );
    read_reg_str_utf8( HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                       "00000000-0000-0000-0000-000000000000", mguid, sizeof(mguid) );
    snprintf( disk_str, sizeof(disk_str), "%s|%s|%s", mguid, prod, mfg );
    if (FAILED( CoCreateGuid( &dev_guid ) ))
        BCryptGenRandom( NULL, (BYTE *)&dev_guid, sizeof(dev_guid), BCRYPT_USE_SYSTEM_PREFERRED_RNG );

    b_disk = base64_encode_nocrlf( (const BYTE *)disk_str, (DWORD)strlen( disk_str ) );
    b_bios = base64_encode_nocrlf( (const BYTE *)bios, (DWORD)strlen( bios ) );
    b_mfg  = base64_encode_nocrlf( (const BYTE *)mfg, (DWORD)strlen( mfg ) );
    b_prod = base64_encode_nocrlf( (const BYTE *)prod, (DWORD)strlen( prod ) );
    b_ser  = base64_encode_nocrlf( (const BYTE *)mguid, (DWORD)strlen( mguid ) );
    b_uuid = base64_encode_nocrlf( (const BYTE *)&dev_guid, sizeof(dev_guid) );
    if (!b_disk || !b_bios || !b_mfg || !b_prod || !b_ser || !b_uuid)
    {
        *err_hr = E_OUTOFMEMORY;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    snprintf( req_xml, sizeof(req_xml),
        "<DeviceAddRequest>"
          "<ClientInfo name=\"IDCRL\" version=\"1.0\"><BinaryVersion>24</BinaryVersion></ClientInfo>"
          "<Authentication><Membername>%s</Membername><Password>%s</Password></Authentication>"
          "<DeviceInfo Id=\"DeviceInfo\">"
            "<Component name=\"4113\">%s</Component>"
            "<Component name=\"4097\">%s</Component>"
            "<Component name=\"4098\">%s</Component>"
            "<Component name=\"4099\">%s</Component>"
            "<Component name=\"4100\">MS4w</Component>"
            "<Component name=\"4101\">%s</Component>"
            "<Component name=\"4102\">%s</Component>"
            "<Component name=\"8197\">BAAAA</Component>"
          "</DeviceInfo>"
        "</DeviceAddRequest>",
        cred->username, password, b_disk, b_bios, b_mfg, b_prod, b_ser, b_uuid );

    if (!http_post_xml( L"login.live.com", L"/ppsecure/deviceaddcredential.srf",
                        L"application/xml; charset=utf-8", req_xml, (DWORD)strlen( req_xml ),
                        &resp, &resp_len, &http_status ))
    {
        WARN( "DeviceAddCredential request failed (err %lu)\n", GetLastError() );
        *err_hr = HRESULT_FROM_WIN32( GetLastError() );
        *status_out = OnlineIdSystemTicketStatus_ServiceConnectionError;
        goto done;
    }

    if (http_status != 200 || !(lic_b64 = extract_xml_tag_alloc( resp, "SPLicenseBlock" )))
    {
        WARN( "DeviceAddCredential unexpected status %lu\n", http_status );
        *err_hr = E_FAIL;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    if (!CryptStringToBinaryA( lic_b64, 0, CRYPT_STRING_BASE64, NULL, &lic_len, NULL, NULL ) || !lic_len ||
        !(lic = malloc( lic_len )) ||
        !CryptStringToBinaryA( lic_b64, 0, CRYPT_STRING_BASE64, lic, &lic_len, NULL, NULL ))
    {
        *err_hr = E_FAIL;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    if (!decrypt_sp_license_rsa( lic, lic_len, cred->rsa_blob ))
    {
        WARN( "Failed to decrypt SPLicenseBlock RSA2 key\n" );
        *err_hr = E_FAIL;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    if (!extract_xml_tag( resp, "puid", cred->puid, sizeof(cred->puid) ) &&
        !extract_xml_tag( resp, "HWDeviceID", cred->puid, sizeof(cred->puid) ))
        snprintf( cred->puid, sizeof(cred->puid), "%s", cred->username );

    cred->loaded = TRUE;
    save_device_cred_to_reg( cred );
    ok = TRUE;
    TRACE( "Registered device cred username=%s puid=%s\n", cred->username, cred->puid );

done:
    free( lic );
    free( lic_b64 );
    free( resp );
    free( b_disk );
    free( b_bios );
    free( b_mfg );
    free( b_prod );
    free( b_ser );
    free( b_uuid );
    return ok;
}

static BOOL rsa_sign_sha256( const BYTE rsa_blob[DEVICE_RSA_BLOB_SIZE], const char *msg, DWORD msg_len,
                             char **sig_b64_out )
{
    BYTE hash[32], sig[256];
    BCRYPT_ALG_HANDLE rsa_alg = NULL;
    BCRYPT_KEY_HANDLE rsa_key = NULL;
    BCRYPT_PKCS1_PADDING_INFO pad = { BCRYPT_SHA256_ALGORITHM };
    DWORD sig_len = 0;
    BOOL ok = FALSE;

    if (!sha256_hash( (const BYTE *)msg, msg_len, hash )) return FALSE;
    if (BCryptOpenAlgorithmProvider( &rsa_alg, BCRYPT_RSA_ALGORITHM, NULL, 0 ) != 0)
        return FALSE;
    if (BCryptImportKeyPair( rsa_alg, NULL, BCRYPT_RSAPRIVATE_BLOB, &rsa_key,
                             (PUCHAR)rsa_blob, DEVICE_RSA_BLOB_SIZE, 0 ) == 0 &&
        BCryptSignHash( rsa_key, &pad, hash, sizeof(hash), sig, sizeof(sig),
                        &sig_len, BCRYPT_PAD_PKCS1 ) == 0 &&
        sig_len == 256)
    {
        *sig_b64_out = base64_encode_nocrlf( sig, sig_len );
        ok = (*sig_b64_out != NULL);
    }

    if (rsa_key) BCryptDestroyKey( rsa_key );
    BCryptCloseAlgorithmProvider( rsa_alg, 0 );
    return ok;
}

static BOOL request_rst2_ticket( const struct device_cred *cred, const char *service, const char *policy,
                                 WCHAR **ticket_w_out, HRESULT *err_hr,
                                 OnlineIdSystemTicketStatus *status_out )
{
    SYSTEMTIME st_now, st_exp;
    FILETIME ft;
    ULARGE_INTEGER uli;
    char created[32], expires[32], msg_uuid[48];
    char *esc_svc = NULL, *esc_pol = NULL;
    char ts_xml[256], rst_xml[1024], signed_info[1536], env_xml[4096];
    BYTE ts_hash[32], rst_hash[32];
    char *ts_dig_b64 = NULL, *rst_dig_b64 = NULL, *sig_b64 = NULL;
    char *resp = NULL, *tok_utf8 = NULL;
    DWORD resp_len = 0, http_status = 0;
    GUID msg_guid;
    BOOL ok = FALSE;

    GetSystemTime( &st_now );
    SystemTimeToFileTime( &st_now, &ft );
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uli.QuadPart += 300ULL * 10000000ULL;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    FileTimeToSystemTime( &ft, &st_exp );

    snprintf( created, sizeof(created), "%04u-%02u-%02uT%02u:%02u:%02uZ",
              st_now.wYear, st_now.wMonth, st_now.wDay, st_now.wHour, st_now.wMinute, st_now.wSecond );
    snprintf( expires, sizeof(expires), "%04u-%02u-%02uT%02u:%02u:%02uZ",
              st_exp.wYear, st_exp.wMonth, st_exp.wDay, st_exp.wHour, st_exp.wMinute, st_exp.wSecond );

    if (FAILED( CoCreateGuid( &msg_guid ) ))
        BCryptGenRandom( NULL, (BYTE *)&msg_guid, sizeof(msg_guid), BCRYPT_USE_SYSTEM_PREFERRED_RNG );
    snprintf( msg_uuid, sizeof(msg_uuid), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
              (unsigned long)msg_guid.Data1, msg_guid.Data2, msg_guid.Data3,
              msg_guid.Data4[0], msg_guid.Data4[1], msg_guid.Data4[2], msg_guid.Data4[3],
              msg_guid.Data4[4], msg_guid.Data4[5], msg_guid.Data4[6], msg_guid.Data4[7] );

    esc_svc = xml_escape( service );
    esc_pol = (policy && policy[0]) ? xml_escape( policy ) : NULL;
    if (!esc_svc || (policy && policy[0] && !esc_pol))
    {
        *err_hr = E_OUTOFMEMORY;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    snprintf( ts_xml, sizeof(ts_xml),
        "<wsu:Timestamp xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" wsu:Id=\"Timestamp\">"
          "<wsu:Created>%s</wsu:Created><wsu:Expires>%s</wsu:Expires>"
        "</wsu:Timestamp>",
        created, expires );

    if (esc_pol)
        snprintf( rst_xml, sizeof(rst_xml),
            "<wst:RequestSecurityToken Id=\"RST0\">"
              "<wst:RequestType>http://schemas.xmlsoap.org/ws/2005/02/trust/Issue</wst:RequestType>"
              "<wsp:AppliesTo><wsa:EndpointReference><wsa:Address>%s</wsa:Address></wsa:EndpointReference></wsp:AppliesTo>"
              "<wsp:PolicyReference URI=\"%s\"></wsp:PolicyReference>"
            "</wst:RequestSecurityToken>",
            esc_svc, esc_pol );
    else
        snprintf( rst_xml, sizeof(rst_xml),
            "<wst:RequestSecurityToken Id=\"RST0\">"
              "<wst:RequestType>http://schemas.xmlsoap.org/ws/2005/02/trust/Issue</wst:RequestType>"
              "<wsp:AppliesTo><wsa:EndpointReference><wsa:Address>%s</wsa:Address></wsa:EndpointReference></wsp:AppliesTo>"
            "</wst:RequestSecurityToken>",
            esc_svc );

    if (!sha256_hash( (const BYTE *)ts_xml, (DWORD)strlen( ts_xml ), ts_hash ) ||
        !sha256_hash( (const BYTE *)rst_xml, (DWORD)strlen( rst_xml ), rst_hash ) ||
        !(ts_dig_b64 = base64_encode_nocrlf( ts_hash, sizeof(ts_hash) )) ||
        !(rst_dig_b64 = base64_encode_nocrlf( rst_hash, sizeof(rst_hash) )))
    {
        *err_hr = E_FAIL;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    snprintf( signed_info, sizeof(signed_info),
        "<SignedInfo xmlns=\"http://www.w3.org/2000/09/xmldsig#\">"
          "<CanonicalizationMethod Algorithm=\"http://www.w3.org/2001/10/xml-exc-c14n#\"></CanonicalizationMethod>"
          "<SignatureMethod Algorithm=\"http://www.w3.org/2001/04/xmldsig-more#rsa-sha256\"></SignatureMethod>"
          "<Reference URI=\"#RST0\">"
            "<Transforms><Transform Algorithm=\"http://www.w3.org/2001/10/xml-exc-c14n#\"></Transform></Transforms>"
            "<DigestMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#sha256\"></DigestMethod>"
            "<DigestValue>%s</DigestValue>"
          "</Reference>"
          "<Reference URI=\"#PPAuthInfo\">"
            "<Transforms><Transform Algorithm=\"http://www.w3.org/2001/10/xml-exc-c14n#\"></Transform></Transforms>"
            "<DigestMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#sha256\"></DigestMethod>"
            "<DigestValue>%s</DigestValue>"
          "</Reference>"
          "<Reference URI=\"#Timestamp\">"
            "<Transforms><Transform Algorithm=\"http://www.w3.org/2001/10/xml-exc-c14n#\"></Transform></Transforms>"
            "<DigestMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#sha256\"></DigestMethod>"
            "<DigestValue>%s</DigestValue>"
          "</Reference>"
        "</SignedInfo>",
        rst_dig_b64, ppauth_digest_b64, ts_dig_b64 );

    if (!rsa_sign_sha256( cred->rsa_blob, signed_info, (DWORD)strlen( signed_info ), &sig_b64 ))
    {
        *err_hr = E_FAIL;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    snprintf( env_xml, sizeof(env_xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" "
        "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\" "
        "xmlns:saml=\"urn:oasis:names:tc:SAML:1.0:assertion\" "
        "xmlns:wsp=\"http://schemas.xmlsoap.org/ws/2004/09/policy\" "
        "xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" "
        "xmlns:wsa=\"http://www.w3.org/2005/08/addressing\" "
        "xmlns:wssc=\"http://schemas.xmlsoap.org/ws/2005/02/sc\" "
        "xmlns:wst=\"http://schemas.xmlsoap.org/ws/2005/02/trust\">"
        "<s:Header>"
          "<wsa:Action s:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/02/trust/RST/Issue</wsa:Action>"
          "<wsa:To s:mustUnderstand=\"1\">https://login.live.com:443/RST2.srf</wsa:To>"
          "<wsa:MessageID>%s</wsa:MessageID>"
          "<ps:AuthInfo xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" Id=\"PPAuthInfo\">"
            "<ps:HostingApp>{DF60E2DF-88AD-4526-AE21-83D130EF0F68}</ps:HostingApp>"
            "<ps:BinaryVersion>24</ps:BinaryVersion>"
            "<ps:UIVersion>1</ps:UIVersion>"
            "<ps:InlineUX>TokenBroker</ps:InlineUX>"
            "<ps:IsAdmin>1</ps:IsAdmin>"
            "<ps:Cookies></ps:Cookies>"
            "<ps:RequestParams>AQAAAAIAAABsYwQAAAAxMDMz</ps:RequestParams>"
            "<ps:LicenseSignatureKeyVersion>2</ps:LicenseSignatureKeyVersion>"
            "<ps:ClientCapabilities>1</ps:ClientCapabilities>"
          "</ps:AuthInfo>"
          "<wsse:Security>"
            "<wsse:UsernameToken wsu:Id=\"devicesoftware\"><wsse:Username>%s</wsse:Username></wsse:UsernameToken>"
            "%s"
            "<Signature xmlns=\"http://www.w3.org/2000/09/xmldsig#\">%s<SignatureValue>%s</SignatureValue></Signature>"
          "</wsse:Security>"
        "</s:Header>"
        "<s:Body>%s</s:Body></s:Envelope>",
        msg_uuid, cred->username, ts_xml, signed_info, sig_b64, rst_xml );

    if (!http_post_xml( L"login.live.com", L"/RST2.srf",
                        L"application/soap+xml; charset=utf-8", env_xml, (DWORD)strlen( env_xml ),
                        &resp, &resp_len, &http_status ))
    {
        WARN( "RST2.srf request failed (err %lu)\n", GetLastError() );
        *err_hr = HRESULT_FROM_WIN32( GetLastError() );
        *status_out = OnlineIdSystemTicketStatus_ServiceConnectionError;
        goto done;
    }

    if (http_status != 200 || !(tok_utf8 = extract_xml_tag_alloc( resp, "BinarySecurityToken" )) || !tok_utf8[0])
    {
        WARN( "RST2.srf failed http=%lu\n", http_status );
        *err_hr = E_FAIL;
        *status_out = OnlineIdSystemTicketStatus_Error;
        goto done;
    }

    *ticket_w_out = utf8_to_wide( tok_utf8 );
    ok = (*ticket_w_out != NULL);
    if (!ok)
    {
        *err_hr = E_OUTOFMEMORY;
        *status_out = OnlineIdSystemTicketStatus_Error;
    }

done:
    free( tok_utf8 );
    free( resp );
    free( sig_b64 );
    free( rst_dig_b64 );
    free( ts_dig_b64 );
    free( esc_pol );
    free( esc_svc );
    return ok;
}

static BOOL lookup_ticket_cache( const WCHAR *service, const WCHAR *policy, WCHAR **ticket_out, WCHAR **puid_out )
{
    ULONGLONG now = GetTickCount64();
    int i;

    EnterCriticalSection( &onlineid_cs );
    for (i = 0; i < TICKET_CACHE_SLOTS; i++)
    {
        struct ticket_cache_entry *e = &g_ticket_cache[i];
        if (!e->ticket) continue;
        if (now - e->tick_ms > TICKET_CACHE_TTL_MS) continue;
        if (!wcscmp( e->service, service ) && !wcscmp( e->policy, policy ))
        {
            *ticket_out = wcsdup( e->ticket );
            *puid_out = wcsdup( e->puid );
            LeaveCriticalSection( &onlineid_cs );
            return (*ticket_out && *puid_out);
        }
    }
    LeaveCriticalSection( &onlineid_cs );
    return FALSE;
}

static void store_ticket_cache( const WCHAR *service, const WCHAR *policy, const WCHAR *ticket, const WCHAR *puid )
{
    ULONGLONG now = GetTickCount64(), oldest = ~0ULL;
    int i, slot = 0;

    EnterCriticalSection( &onlineid_cs );
    for (i = 0; i < TICKET_CACHE_SLOTS; i++)
    {
        struct ticket_cache_entry *e = &g_ticket_cache[i];
        if (!e->ticket || (!wcscmp( e->service, service ) && !wcscmp( e->policy, policy )))
        {
            slot = i;
            break;
        }
        if (e->tick_ms < oldest) { oldest = e->tick_ms; slot = i; }
    }
    free( g_ticket_cache[slot].service );
    free( g_ticket_cache[slot].policy );
    free( g_ticket_cache[slot].ticket );
    free( g_ticket_cache[slot].puid );
    g_ticket_cache[slot].service = wcsdup( service );
    g_ticket_cache[slot].policy  = wcsdup( policy );
    g_ticket_cache[slot].ticket  = wcsdup( ticket );
    g_ticket_cache[slot].puid    = wcsdup( puid );
    g_ticket_cache[slot].tick_ms = now;
    LeaveCriticalSection( &onlineid_cs );
}

static BOOL fetch_device_ticket( const WCHAR *service_w, const WCHAR *policy_w,
                                 WCHAR **ticket_w_out, WCHAR **puid_w_out,
                                 HRESULT *err_hr, OnlineIdSystemTicketStatus *status_out )
{
    struct device_cred cred;
    char *svc_utf8 = NULL, *pol_utf8 = NULL;
    BOOL have_cred = FALSE, ok = FALSE;

    *ticket_w_out = NULL;
    *puid_w_out = NULL;
    *err_hr = S_OK;
    *status_out = OnlineIdSystemTicketStatus_Error;

    if (!service_w || !service_w[0])
    {
        *err_hr = E_INVALIDARG;
        return FALSE;
    }
    if (!policy_w) policy_w = L"";

    if (lookup_ticket_cache( service_w, policy_w, ticket_w_out, puid_w_out ))
    {
        *status_out = OnlineIdSystemTicketStatus_Success;
        return TRUE;
    }

    svc_utf8 = wide_to_utf8( service_w );
    pol_utf8 = wide_to_utf8( policy_w );
    if (!svc_utf8 || !pol_utf8)
    {
        *err_hr = E_OUTOFMEMORY;
        goto done;
    }

    EnterCriticalSection( &onlineid_cs );
    if (!g_device_cred.loaded) load_device_cred_from_reg( &g_device_cred );
    if (g_device_cred.loaded)
    {
        cred = g_device_cred;
        have_cred = TRUE;
    }
    LeaveCriticalSection( &onlineid_cs );

    if (!have_cred)
    {
        memset( &cred, 0, sizeof(cred) );
        if (!register_device_credential( &cred, err_hr, status_out ))
            goto done;
        EnterCriticalSection( &onlineid_cs );
        g_device_cred = cred;
        LeaveCriticalSection( &onlineid_cs );
    }

    if (!request_rst2_ticket( &cred, svc_utf8, pol_utf8, ticket_w_out, err_hr, status_out ))
    {
        /* If a cached credential was rejected, re-register once and retry. */
        if (have_cred && *status_out == OnlineIdSystemTicketStatus_Error)
        {
            memset( &cred, 0, sizeof(cred) );
            if (register_device_credential( &cred, err_hr, status_out ))
            {
                EnterCriticalSection( &onlineid_cs );
                g_device_cred = cred;
                LeaveCriticalSection( &onlineid_cs );
                ok = request_rst2_ticket( &cred, svc_utf8, pol_utf8, ticket_w_out, err_hr, status_out );
            }
        }
    }
    else ok = TRUE;

    if (ok)
    {
        *puid_w_out = utf8_to_wide( cred.puid );
        if (!*puid_w_out)
        {
            free( *ticket_w_out );
            *ticket_w_out = NULL;
            *err_hr = E_OUTOFMEMORY;
            *status_out = OnlineIdSystemTicketStatus_Error;
            ok = FALSE;
        }
        else
        {
            *err_hr = S_OK;
            *status_out = OnlineIdSystemTicketStatus_Success;
            store_ticket_cache( service_w, policy_w, *ticket_w_out, *puid_w_out );
        }
    }

done:
    free( pol_utf8 );
    free( svc_utf8 );
    return ok;
}

struct authenticator_statics
{
    IActivationFactory IActivationFactory_iface;
    IOnlineIdSystemAuthenticatorStatics IOnlineIdSystemAuthenticatorStatics_iface;
    LONG ref;
};

static inline struct authenticator_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IOnlineIdSystemAuthenticatorStatics ))
    {
        *out = &impl->IOnlineIdSystemAuthenticatorStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

struct service_ticket
{
    IOnlineIdServiceTicket IOnlineIdServiceTicket_iface;
    LONG ref;
    HSTRING value;
    IOnlineIdServiceTicketRequest *request;
    INT32 error_code;
};

static inline struct service_ticket *impl_from_service_ticket( IOnlineIdServiceTicket *iface )
{
    return CONTAINING_RECORD( iface, struct service_ticket, IOnlineIdServiceTicket_iface );
}

static HRESULT WINAPI service_ticket_QueryInterface( IOnlineIdServiceTicket *iface, REFIID iid, void **out )
{
    struct service_ticket *impl = impl_from_service_ticket( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdServiceTicket ))
    {
        *out = &impl->IOnlineIdServiceTicket_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI service_ticket_AddRef( IOnlineIdServiceTicket *iface )
{ return InterlockedIncrement( &impl_from_service_ticket( iface )->ref ); }

static ULONG WINAPI service_ticket_Release( IOnlineIdServiceTicket *iface )
{
    struct service_ticket *impl = impl_from_service_ticket( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->value );
        if (impl->request) IOnlineIdServiceTicketRequest_Release( impl->request );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI service_ticket_GetIids( IOnlineIdServiceTicket *iface, ULONG *c, IID **i )
{ if (c) *c = 0; if (i) *i = NULL; return S_OK; }

static HRESULT WINAPI service_ticket_GetRuntimeClassName( IOnlineIdServiceTicket *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Security.Authentication.OnlineId.OnlineIdServiceTicket";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}

static HRESULT WINAPI service_ticket_GetTrustLevel( IOnlineIdServiceTicket *iface, TrustLevel *l )
{ if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI service_ticket_get_Value( IOnlineIdServiceTicket *iface, HSTRING *value )
{
    struct service_ticket *impl = impl_from_service_ticket( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->value, value );
}

static HRESULT WINAPI service_ticket_get_Request( IOnlineIdServiceTicket *iface,
                                                  IOnlineIdServiceTicketRequest **value )
{
    struct service_ticket *impl = impl_from_service_ticket( iface );
    if (!value) return E_POINTER;
    *value = impl->request;
    if (*value) IOnlineIdServiceTicketRequest_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI service_ticket_get_ErrorCode( IOnlineIdServiceTicket *iface, INT32 *value )
{
    struct service_ticket *impl = impl_from_service_ticket( iface );
    if (!value) return E_POINTER;
    *value = impl->error_code;
    return S_OK;
}

static const struct IOnlineIdServiceTicketVtbl service_ticket_vtbl =
{
    service_ticket_QueryInterface, service_ticket_AddRef, service_ticket_Release,
    service_ticket_GetIids, service_ticket_GetRuntimeClassName, service_ticket_GetTrustLevel,
    service_ticket_get_Value, service_ticket_get_Request, service_ticket_get_ErrorCode,
};

struct system_identity
{
    IOnlineIdSystemIdentity IOnlineIdSystemIdentity_iface;
    LONG ref;
    IOnlineIdServiceTicket *ticket;
    HSTRING id;
};

static inline struct system_identity *impl_from_system_identity( IOnlineIdSystemIdentity *iface )
{
    return CONTAINING_RECORD( iface, struct system_identity, IOnlineIdSystemIdentity_iface );
}

static HRESULT WINAPI system_identity_QueryInterface( IOnlineIdSystemIdentity *iface, REFIID iid, void **out )
{
    struct system_identity *impl = impl_from_system_identity( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdSystemIdentity ))
    {
        *out = &impl->IOnlineIdSystemIdentity_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI system_identity_AddRef( IOnlineIdSystemIdentity *iface )
{ return InterlockedIncrement( &impl_from_system_identity( iface )->ref ); }

static ULONG WINAPI system_identity_Release( IOnlineIdSystemIdentity *iface )
{
    struct system_identity *impl = impl_from_system_identity( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->ticket) IOnlineIdServiceTicket_Release( impl->ticket );
        WindowsDeleteString( impl->id );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI system_identity_GetIids( IOnlineIdSystemIdentity *iface, ULONG *c, IID **i )
{ if (c) *c = 0; if (i) *i = NULL; return S_OK; }

static HRESULT WINAPI system_identity_GetRuntimeClassName( IOnlineIdSystemIdentity *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemIdentity";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}

static HRESULT WINAPI system_identity_GetTrustLevel( IOnlineIdSystemIdentity *iface, TrustLevel *l )
{ if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI system_identity_get_Ticket( IOnlineIdSystemIdentity *iface,
                                                  IOnlineIdServiceTicket **value )
{
    struct system_identity *impl = impl_from_system_identity( iface );
    if (!value) return E_POINTER;
    *value = impl->ticket;
    if (*value) IOnlineIdServiceTicket_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI system_identity_get_Id( IOnlineIdSystemIdentity *iface, HSTRING *value )
{
    struct system_identity *impl = impl_from_system_identity( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->id, value );
}

static const struct IOnlineIdSystemIdentityVtbl system_identity_vtbl =
{
    system_identity_QueryInterface, system_identity_AddRef, system_identity_Release,
    system_identity_GetIids, system_identity_GetRuntimeClassName, system_identity_GetTrustLevel,
    system_identity_get_Ticket, system_identity_get_Id,
};

struct ticket_result
{
    IOnlineIdSystemTicketResult IOnlineIdSystemTicketResult_iface;
    LONG ref;
    IOnlineIdSystemIdentity *identity;
    OnlineIdSystemTicketStatus status;
    HRESULT extended_error;
};

static inline struct ticket_result *impl_from_ticket_result( IOnlineIdSystemTicketResult *iface )
{
    return CONTAINING_RECORD( iface, struct ticket_result, IOnlineIdSystemTicketResult_iface );
}
static HRESULT WINAPI ticket_result_QueryInterface( IOnlineIdSystemTicketResult *iface, REFIID iid, void **out )
{
    struct ticket_result *impl = impl_from_ticket_result( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdSystemTicketResult ))
    {
        *out = &impl->IOnlineIdSystemTicketResult_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ticket_result_AddRef( IOnlineIdSystemTicketResult *iface )
{ return InterlockedIncrement( &impl_from_ticket_result( iface )->ref ); }
static ULONG WINAPI ticket_result_Release( IOnlineIdSystemTicketResult *iface )
{
    struct ticket_result *impl = impl_from_ticket_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->identity) IOnlineIdSystemIdentity_Release( impl->identity );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI ticket_result_GetIids( IOnlineIdSystemTicketResult *iface, ULONG *c, IID **i )
{ if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ticket_result_GetRuntimeClassName( IOnlineIdSystemTicketResult *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemTicketResult";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ticket_result_GetTrustLevel( IOnlineIdSystemTicketResult *iface, TrustLevel *l )
{ if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ticket_result_get_Identity( IOnlineIdSystemTicketResult *iface,
        __x_ABI_CWindows_CSecurity_CAuthentication_COnlineId_CIOnlineIdSystemIdentity **value )
{
    struct ticket_result *impl = impl_from_ticket_result( iface );
    if (!value) return E_POINTER;
    *value = impl->identity;
    if (*value) IOnlineIdSystemIdentity_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI ticket_result_get_Status( IOnlineIdSystemTicketResult *iface,
        OnlineIdSystemTicketStatus *value )
{
    struct ticket_result *impl = impl_from_ticket_result( iface );
    if (!value) return E_POINTER;
    *value = impl->status;
    return S_OK;
}
static HRESULT WINAPI ticket_result_get_ExtendedError( IOnlineIdSystemTicketResult *iface, HRESULT *value )
{
    struct ticket_result *impl = impl_from_ticket_result( iface );
    if (!value) return E_POINTER;
    *value = impl->extended_error;
    return S_OK;
}
static const struct IOnlineIdSystemTicketResultVtbl ticket_result_vtbl =
{
    ticket_result_QueryInterface, ticket_result_AddRef, ticket_result_Release,
    ticket_result_GetIids, ticket_result_GetRuntimeClassName, ticket_result_GetTrustLevel,
    ticket_result_get_Identity, ticket_result_get_Status, ticket_result_get_ExtendedError,
};

static IOnlineIdSystemTicketResult *create_ticket_result( IOnlineIdServiceTicketRequest *request,
                                                        const WCHAR *ticket_w, const WCHAR *puid_w,
                                                        OnlineIdSystemTicketStatus status, HRESULT ext_hr )
{
    struct ticket_result *res;
    struct system_identity *ident = NULL;
    struct service_ticket *tkt = NULL;

    if (!(res = calloc( 1, sizeof(*res) ))) return NULL;
    res->IOnlineIdSystemTicketResult_iface.lpVtbl = &ticket_result_vtbl;
    res->ref = 1;
    res->status = status;
    res->extended_error = ext_hr;

    if (status == OnlineIdSystemTicketStatus_Success && ticket_w && puid_w)
    {
        if (!(tkt = calloc( 1, sizeof(*tkt) ))) goto oom;
        tkt->IOnlineIdServiceTicket_iface.lpVtbl = &service_ticket_vtbl;
        tkt->ref = 1;
        tkt->request = request;
        if (request) IOnlineIdServiceTicketRequest_AddRef( request );
        if (FAILED( WindowsCreateString( ticket_w, wcslen( ticket_w ), &tkt->value ) )) goto oom;

        if (!(ident = calloc( 1, sizeof(*ident) ))) goto oom;
        ident->IOnlineIdSystemIdentity_iface.lpVtbl = &system_identity_vtbl;
        ident->ref = 1;
        ident->ticket = &tkt->IOnlineIdServiceTicket_iface;
        tkt = NULL;
        if (FAILED( WindowsCreateString( puid_w, wcslen( puid_w ), &ident->id ) )) goto oom;

        res->identity = &ident->IOnlineIdSystemIdentity_iface;
    }

    return &res->IOnlineIdSystemTicketResult_iface;

oom:
    if (tkt) IOnlineIdServiceTicket_Release( &tkt->IOnlineIdServiceTicket_iface );
    if (ident) IOnlineIdSystemIdentity_Release( &ident->IOnlineIdSystemIdentity_iface );
    IOnlineIdSystemTicketResult_Release( &res->IOnlineIdSystemTicketResult_iface );
    return NULL;
}

struct ticket_op
{
    IAsyncOperation_OnlineIdSystemTicketResult IAsyncOperation_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    UINT32 op_id;
    CRITICAL_SECTION cs;
    AsyncStatus status;
    HRESULT error_code;
    IOnlineIdServiceTicketRequest *request;
    IOnlineIdSystemTicketResult *result;
    IAsyncOperationCompletedHandler_OnlineIdSystemTicketResult *handler;
};
static inline struct ticket_op *impl_from_ticket_op( IAsyncOperation_OnlineIdSystemTicketResult *iface )
{ return CONTAINING_RECORD( iface, struct ticket_op, IAsyncOperation_iface ); }
static inline struct ticket_op *impl_from_ticket_info( IAsyncInfo *iface )
{ return CONTAINING_RECORD( iface, struct ticket_op, IAsyncInfo_iface ); }

static HRESULT WINAPI ticket_op_QueryInterface( IAsyncOperation_OnlineIdSystemTicketResult *iface, REFIID iid, void **out )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_OnlineIdSystemTicketResult ))
    {
        *out = &impl->IAsyncOperation_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ticket_op_AddRef( IAsyncOperation_OnlineIdSystemTicketResult *iface )
{ return InterlockedIncrement( &impl_from_ticket_op( iface )->ref ); }
static ULONG WINAPI ticket_op_Release( IAsyncOperation_OnlineIdSystemTicketResult *iface )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler) impl->handler->lpVtbl->Release( impl->handler );
        if (impl->result) IOnlineIdSystemTicketResult_Release( impl->result );
        if (impl->request) IOnlineIdServiceTicketRequest_Release( impl->request );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI ticket_op_GetIids( IAsyncOperation_OnlineIdSystemTicketResult *iface, ULONG *c, IID **i )
{ if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ticket_op_GetRuntimeClassName( IAsyncOperation_OnlineIdSystemTicketResult *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Authentication.OnlineId.OnlineIdSystemTicketResult>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ticket_op_GetTrustLevel( IAsyncOperation_OnlineIdSystemTicketResult *iface, TrustLevel *l )
{ if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ticket_op_put_Completed( IAsyncOperation_OnlineIdSystemTicketResult *iface,
        IAsyncOperationCompletedHandler_OnlineIdSystemTicketResult *handler )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    AsyncStatus st;

    EnterCriticalSection( &impl->cs );
    st = impl->status;
    if (st == Started)
    {
        if (impl->handler) impl->handler->lpVtbl->Release( impl->handler );
        impl->handler = handler;
        if (handler) handler->lpVtbl->AddRef( handler );
        LeaveCriticalSection( &impl->cs );
        return S_OK;
    }
    LeaveCriticalSection( &impl->cs );
    if (handler) handler->lpVtbl->Invoke( handler, iface, st );
    return S_OK;
}
static HRESULT WINAPI ticket_op_get_Completed( IAsyncOperation_OnlineIdSystemTicketResult *iface,
        IAsyncOperationCompletedHandler_OnlineIdSystemTicketResult **handler )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    if (!handler) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *handler = impl->handler;
    if (*handler) (*handler)->lpVtbl->AddRef( *handler );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}
static HRESULT WINAPI ticket_op_GetResults( IAsyncOperation_OnlineIdSystemTicketResult *iface,
        IOnlineIdSystemTicketResult **results )
{
    struct ticket_op *impl = impl_from_ticket_op( iface );
    if (!results) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    if (impl->status == Started)
    {
        *results = NULL;
        LeaveCriticalSection( &impl->cs );
        return E_ILLEGAL_METHOD_CALL;
    }
    *results = impl->result;
    if (*results) IOnlineIdSystemTicketResult_AddRef( *results );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}
static const struct __FIAsyncOperation_1_Windows__CSecurity__CAuthentication__COnlineId__COnlineIdSystemTicketResultVtbl ticket_op_vtbl =
{
    ticket_op_QueryInterface, ticket_op_AddRef, ticket_op_Release,
    ticket_op_GetIids, ticket_op_GetRuntimeClassName, ticket_op_GetTrustLevel,
    ticket_op_put_Completed, ticket_op_get_Completed, ticket_op_GetResults,
};

static HRESULT WINAPI ticket_info_QI( IAsyncInfo *iface, REFIID iid, void **out )
{ return ticket_op_QueryInterface( &impl_from_ticket_info( iface )->IAsyncOperation_iface, iid, out ); }
static ULONG WINAPI ticket_info_AddRef( IAsyncInfo *iface ) { return ticket_op_AddRef( &impl_from_ticket_info( iface )->IAsyncOperation_iface ); }
static ULONG WINAPI ticket_info_Release( IAsyncInfo *iface ) { return ticket_op_Release( &impl_from_ticket_info( iface )->IAsyncOperation_iface ); }
static HRESULT WINAPI ticket_info_GetIids( IAsyncInfo *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ticket_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *n )
{ static const WCHAR cls[] = L"Windows.Foundation.IAsyncInfo"; return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n ); }
static HRESULT WINAPI ticket_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ticket_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    struct ticket_op *impl = impl_from_ticket_info( iface );
    if (!id) return E_POINTER;
    *id = impl->op_id;
    return S_OK;
}
static HRESULT WINAPI ticket_info_get_Status( IAsyncInfo *iface, AsyncStatus *s )
{
    struct ticket_op *impl = impl_from_ticket_info( iface );
    if (!s) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *s = impl->status;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}
static HRESULT WINAPI ticket_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *e )
{
    struct ticket_op *impl = impl_from_ticket_info( iface );
    if (!e) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *e = impl->error_code;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}
static HRESULT WINAPI ticket_info_Cancel( IAsyncInfo *iface ) { return S_OK; }
static HRESULT WINAPI ticket_info_Close( IAsyncInfo *iface ) { return S_OK; }
static const struct IAsyncInfoVtbl ticket_info_vtbl =
{
    ticket_info_QI, ticket_info_AddRef, ticket_info_Release,
    ticket_info_GetIids, ticket_info_GetRuntimeClassName, ticket_info_GetTrustLevel,
    ticket_info_get_Id, ticket_info_get_Status, ticket_info_get_ErrorCode, ticket_info_Cancel, ticket_info_Close,
};

static DWORD WINAPI ticket_worker( void *arg )
{
    struct ticket_op *op = arg;
    HSTRING h_svc = NULL, h_pol = NULL;
    const WCHAR *svc_w = L"", *pol_w = L"";
    WCHAR *ticket_w = NULL, *puid_w = NULL;
    OnlineIdSystemTicketStatus st = OnlineIdSystemTicketStatus_Error;
    HRESULT ext_hr = E_FAIL;
    IOnlineIdSystemTicketResult *res;
    IAsyncOperationCompletedHandler_OnlineIdSystemTicketResult *handler = NULL;

    if (op->request)
    {
        if (SUCCEEDED( IOnlineIdServiceTicketRequest_get_Service( op->request, &h_svc ) ) && h_svc)
            svc_w = WindowsGetStringRawBuffer( h_svc, NULL );
        if (SUCCEEDED( IOnlineIdServiceTicketRequest_get_Policy( op->request, &h_pol ) ) && h_pol)
            pol_w = WindowsGetStringRawBuffer( h_pol, NULL );
    }

    fetch_device_ticket( svc_w, pol_w, &ticket_w, &puid_w, &ext_hr, &st );
    res = create_ticket_result( op->request, ticket_w, puid_w, st, ext_hr );

    free( ticket_w );
    free( puid_w );
    if (h_svc) WindowsDeleteString( h_svc );
    if (h_pol) WindowsDeleteString( h_pol );

    EnterCriticalSection( &op->cs );
    op->result = res;
    op->error_code = S_OK;
    op->status = Completed;
    handler = op->handler;
    op->handler = NULL;
    LeaveCriticalSection( &op->cs );

    if (handler)
    {
        handler->lpVtbl->Invoke( handler, &op->IAsyncOperation_iface, Completed );
        handler->lpVtbl->Release( handler );
    }
    IAsyncOperation_OnlineIdSystemTicketResult_Release( &op->IAsyncOperation_iface );
    return 0;
}

struct authenticator
{
    IOnlineIdSystemAuthenticatorForUser IOnlineIdSystemAuthenticatorForUser_iface;
    LONG ref;
    GUID app_id;
    __x_ABI_CWindows_CSystem_CIUser *user;
};

static inline struct authenticator *impl_from_IOnlineIdSystemAuthenticatorForUser( IOnlineIdSystemAuthenticatorForUser *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator, IOnlineIdSystemAuthenticatorForUser_iface );
}

static HRESULT WINAPI authenticator_QueryInterface( IOnlineIdSystemAuthenticatorForUser *iface, REFIID iid, void **out )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdSystemAuthenticatorForUser ))
    {
        *out = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI authenticator_AddRef( IOnlineIdSystemAuthenticatorForUser *iface )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI authenticator_Release( IOnlineIdSystemAuthenticatorForUser *iface )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->user) IUnknown_Release( (IUnknown *)impl->user );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI authenticator_GetIids( IOnlineIdSystemAuthenticatorForUser *iface, ULONG *iid_count, IID **iids )
{
    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI authenticator_GetRuntimeClassName( IOnlineIdSystemAuthenticatorForUser *iface, HSTRING *class_name )
{
    static const WCHAR cls[] = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticatorForUser";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, class_name );
}

static HRESULT WINAPI authenticator_GetTrustLevel( IOnlineIdSystemAuthenticatorForUser *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI authenticator_GetTicketAsync( IOnlineIdSystemAuthenticatorForUser *iface, IOnlineIdServiceTicketRequest *request,
                                                    IAsyncOperation_OnlineIdSystemTicketResult **operation )
{
    struct ticket_op *op;
    HSTRING h_svc = NULL, h_pol = NULL;
    const WCHAR *svc_w = L"", *pol_w = L"";
    WCHAR *ticket_w = NULL, *puid_w = NULL;

    TRACE( "iface %p, request %p, operation %p\n", iface, request, operation );
    if (!operation) return E_POINTER;
    if (!(op = calloc( 1, sizeof(*op) ))) return E_OUTOFMEMORY;

    op->IAsyncOperation_iface.lpVtbl = &ticket_op_vtbl;
    op->IAsyncInfo_iface.lpVtbl = &ticket_info_vtbl;
    op->ref = 1;
    op->op_id = (UINT32)InterlockedIncrement( (LONG *)&g_next_op_id );
    InitializeCriticalSection( &op->cs );
    op->request = request;
    if (request) IOnlineIdServiceTicketRequest_AddRef( request );

    if (request)
    {
        if (SUCCEEDED( IOnlineIdServiceTicketRequest_get_Service( request, &h_svc ) ) && h_svc)
            svc_w = WindowsGetStringRawBuffer( h_svc, NULL );
        if (SUCCEEDED( IOnlineIdServiceTicketRequest_get_Policy( request, &h_pol ) ) && h_pol)
            pol_w = WindowsGetStringRawBuffer( h_pol, NULL );
    }

    if (!request || !svc_w[0])
    {
        op->status = Completed;
        op->result = create_ticket_result( request, NULL, NULL, OnlineIdSystemTicketStatus_Error, E_INVALIDARG );
    }
    else if (lookup_ticket_cache( svc_w, pol_w, &ticket_w, &puid_w ))
    {
        op->status = Completed;
        op->result = create_ticket_result( request, ticket_w, puid_w, OnlineIdSystemTicketStatus_Success, S_OK );
        free( ticket_w );
        free( puid_w );
    }
    else
    {
        op->status = Started;
        IAsyncOperation_OnlineIdSystemTicketResult_AddRef( &op->IAsyncOperation_iface );
        if (!QueueUserWorkItem( ticket_worker, op, WT_EXECUTEDEFAULT ))
        {
            IAsyncOperation_OnlineIdSystemTicketResult_Release( &op->IAsyncOperation_iface );
            op->status = Completed;
            op->result = create_ticket_result( request, NULL, NULL, OnlineIdSystemTicketStatus_Error,
                                               HRESULT_FROM_WIN32( GetLastError() ) );
        }
    }

    if (h_svc) WindowsDeleteString( h_svc );
    if (h_pol) WindowsDeleteString( h_pol );
    *operation = &op->IAsyncOperation_iface;
    return S_OK;
}

static HRESULT WINAPI authenticator_put_ApplicationId( IOnlineIdSystemAuthenticatorForUser *iface, GUID value )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    impl->app_id = value;
    return S_OK;
}

static HRESULT WINAPI authenticator_get_ApplicationId( IOnlineIdSystemAuthenticatorForUser *iface, GUID *value )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    if (!value) return E_POINTER;
    *value = impl->app_id;
    return S_OK;
}

static HRESULT WINAPI authenticator_get_User( IOnlineIdSystemAuthenticatorForUser *iface, __x_ABI_CWindows_CSystem_CIUser **user )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    if (!user) return E_POINTER;
    *user = impl->user;
    if (*user) IUnknown_AddRef( (IUnknown *)*user );
    return S_OK;
}

static const struct IOnlineIdSystemAuthenticatorForUserVtbl authenticator_vtbl =
{
    authenticator_QueryInterface,
    authenticator_AddRef,
    authenticator_Release,
    /* IInspectable methods */
    authenticator_GetIids,
    authenticator_GetRuntimeClassName,
    authenticator_GetTrustLevel,
    /* IOnlineIdSystemAuthenticatorForUser methods */
    authenticator_GetTicketAsync,
    authenticator_put_ApplicationId,
    authenticator_get_ApplicationId,
    authenticator_get_User,
};

DEFINE_IINSPECTABLE( authenticator_statics, IOnlineIdSystemAuthenticatorStatics, struct authenticator_statics, IActivationFactory_iface )

static HRESULT WINAPI authenticator_statics_get_Default( IOnlineIdSystemAuthenticatorStatics *iface,
                                                         IOnlineIdSystemAuthenticatorForUser **value )
{
    struct authenticator *impl;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IOnlineIdSystemAuthenticatorForUser_iface.lpVtbl = &authenticator_vtbl;
    impl->ref = 1;

    *value = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
    TRACE( "created IOnlineIdSystemAuthenticatorForUser %p.\n", *value );
    return S_OK;
}

static HRESULT WINAPI authenticator_statics_GetForUser( IOnlineIdSystemAuthenticatorStatics *iface, __x_ABI_CWindows_CSystem_CIUser *user,
                                                        IOnlineIdSystemAuthenticatorForUser **value )
{
    struct authenticator *impl;

    TRACE( "iface %p, user %p, value %p\n", iface, user, value );
    if (!value) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IOnlineIdSystemAuthenticatorForUser_iface.lpVtbl = &authenticator_vtbl;
    impl->ref = 1;
    impl->user = user;
    if (user) IUnknown_AddRef( (IUnknown *)user );
    *value = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
    return S_OK;
}

static const struct IOnlineIdSystemAuthenticatorStaticsVtbl authenticator_statics_vtbl =
{
    authenticator_statics_QueryInterface,
    authenticator_statics_AddRef,
    authenticator_statics_Release,
    /* IInspectable methods */
    authenticator_statics_GetIids,
    authenticator_statics_GetRuntimeClassName,
    authenticator_statics_GetTrustLevel,
    /* IOnlineIdSystemAuthenticatorStatics methods */
    authenticator_statics_get_Default,
    authenticator_statics_GetForUser,
};

static struct authenticator_statics authenticator_statics =
{
    {&factory_vtbl},
    {&authenticator_statics_vtbl},
    1,
};

IActivationFactory *authenticator_factory = &authenticator_statics.IActivationFactory_iface;
