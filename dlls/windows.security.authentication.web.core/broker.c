/* WinRT Windows.Security.Authentication.Web.Core OAuth2 PKCE loopback broker & prefix token cache
 *
 * Copyright 2026 Wine Developers
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

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"
#include "winreg.h"
#include "winhttp.h"
#include "bcrypt.h"
#include "shellapi.h"

WINE_DEFAULT_DEBUG_CHANNEL(webauthcore);

#define WAM_CACHE_ROOT L"Software\\Wine\\WAM\\Tokens"
#define WAM_MAX_TOKEN  8192
#define WAM_MAX_STR    512

struct wam_token_entry
{
    WCHAR access_token[WAM_MAX_TOKEN];
    WCHAR refresh_token[WAM_MAX_TOKEN];
    WCHAR user_name[WAM_MAX_STR];
    WCHAR account_id[WAM_MAX_STR];
    WCHAR scope[WAM_MAX_STR];
    ULONGLONG expires_at;
};

static ULONGLONG now_unix_seconds( void )
{
    FILETIME ft;
    ULARGE_INTEGER ui;

    GetSystemTimeAsFileTime( &ft );
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    if (ui.QuadPart < 116444736000000000ULL) return 0;
    return (ui.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

static void sanitize_key_component( const WCHAR *src, WCHAR *dst, size_t cap )
{
    size_t i = 0;

    if (!src || !*src)
    {
        wcsncpy( dst, L"default", cap - 1 );
        dst[cap - 1] = 0;
        return;
    }

    while (*src && i + 1 < cap)
    {
        WCHAR c = *src++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            dst[i++] = c;
        else
            dst[i++] = '_';
    }
    dst[i] = 0;
}

static void build_cache_subkey( const WCHAR *authority, const WCHAR *client_id, WCHAR *out, size_t cap )
{
    WCHAR auth_san[128], cid_san[128];

    sanitize_key_component( authority, auth_san, ARRAY_SIZE(auth_san) );
    sanitize_key_component( client_id, cid_san, ARRAY_SIZE(cid_san) );
    swprintf( out, cap, L"%ls\\%ls\\%ls", WAM_CACHE_ROOT, auth_san, cid_san );
}

static void extract_tenant( const WCHAR *authority, WCHAR *tenant, size_t cap )
{
    if (!authority || !*authority)
    {
        wcsncpy( tenant, L"consumers", cap - 1 );
        tenant[cap - 1] = 0;
        return;
    }
    if (!wcsicmp( authority, L"consumers" ) || !wcsicmp( authority, L"organizations" ) ||
        !wcsicmp( authority, L"common" ))
    {
        wcsncpy( tenant, authority, cap - 1 );
        tenant[cap - 1] = 0;
        return;
    }
    if (wcsstr( authority, L"login.microsoftonline.com/" ))
    {
        const WCHAR *p = wcsstr( authority, L"login.microsoftonline.com/" ) + 26;
        size_t i = 0;
        while (*p && *p != '/' && *p != '?' && i + 1 < cap)
            tenant[i++] = *p++;
        tenant[i] = 0;
        if (tenant[0]) return;
    }
    wcsncpy( tenant, L"consumers", cap - 1 );
    tenant[cap - 1] = 0;
}

static void reg_read_wstr( HKEY hkey, const WCHAR *name, WCHAR *out, size_t cap )
{
    DWORD type = 0, cb = (DWORD)(cap * sizeof(WCHAR));

    out[0] = 0;
    if (RegQueryValueExW( hkey, name, NULL, &type, (BYTE *)out, &cb ) != ERROR_SUCCESS || type != REG_SZ)
        out[0] = 0;
    else
        out[cap - 1] = 0;
}

static void wam_cache_save( const WCHAR *authority, const WCHAR *client_id, const struct wam_token_entry *entry )
{
    WCHAR subkey[384];
    HKEY hkey;

    build_cache_subkey( authority, client_id, subkey, ARRAY_SIZE(subkey) );
    if (RegCreateKeyExW( HKEY_CURRENT_USER, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL ) != ERROR_SUCCESS)
        return;

    RegSetValueExW( hkey, L"access_token", 0, REG_SZ, (const BYTE *)entry->access_token,
                    (lstrlenW( entry->access_token ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hkey, L"refresh_token", 0, REG_SZ, (const BYTE *)entry->refresh_token,
                    (lstrlenW( entry->refresh_token ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hkey, L"user_name", 0, REG_SZ, (const BYTE *)entry->user_name,
                    (lstrlenW( entry->user_name ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hkey, L"account_id", 0, REG_SZ, (const BYTE *)entry->account_id,
                    (lstrlenW( entry->account_id ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hkey, L"scope", 0, REG_SZ, (const BYTE *)entry->scope,
                    (lstrlenW( entry->scope ) + 1) * sizeof(WCHAR) );
    RegSetValueExW( hkey, L"expires_at", 0, REG_QWORD, (const BYTE *)&entry->expires_at,
                    sizeof(entry->expires_at) );
    RegCloseKey( hkey );
}

void wam_cache_invalidate( const WCHAR *authority, const WCHAR *client_id )
{
    WCHAR subkey[384];

    if (!authority || !client_id) return;
    build_cache_subkey( authority, client_id, subkey, ARRAY_SIZE(subkey) );
    RegDeleteKeyW( HKEY_CURRENT_USER, subkey );
}

static void base64url_encode( const BYTE *in, size_t len, char *out, size_t cap )
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t i = 0, o = 0;

    while (i < len && o + 4 < cap)
    {
        unsigned int v = ((unsigned int)in[i]) << 16;
        if (i + 1 < len) v |= ((unsigned int)in[i + 1]) << 8;
        if (i + 2 < len) v |= (unsigned int)in[i + 2];

        out[o++] = tbl[(v >> 18) & 0x3f];
        out[o++] = tbl[(v >> 12) & 0x3f];
        if (i + 1 < len) out[o++] = tbl[(v >> 6) & 0x3f];
        if (i + 2 < len) out[o++] = tbl[v & 0x3f];
        i += 3;
    }
    out[o] = 0;
}

static BOOL wam_generate_pkce( char *verifier, size_t verifier_cap, char *challenge, size_t challenge_cap )
{
    BYTE rand_buf[32], hash_buf[32];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BOOL ok = FALSE;

    if (BCryptGenRandom( NULL, rand_buf, sizeof(rand_buf), BCRYPT_USE_SYSTEM_PREFERRED_RNG ) != 0)
        return FALSE;

    base64url_encode( rand_buf, sizeof(rand_buf), verifier, verifier_cap );

    if (BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 ) == 0)
    {
        if (BCryptCreateHash( alg, &hash, NULL, 0, NULL, 0, 0 ) == 0)
        {
            if (BCryptHashData( hash, (PUCHAR)verifier, (ULONG)strlen( verifier ), 0 ) == 0 &&
                BCryptFinishHash( hash, hash_buf, sizeof(hash_buf), 0 ) == 0)
            {
                base64url_encode( hash_buf, sizeof(hash_buf), challenge, challenge_cap );
                ok = TRUE;
            }
            BCryptDestroyHash( hash );
        }
        BCryptCloseAlgorithmProvider( alg, 0 );
    }
    return ok;
}

static void url_encode_utf8( const char *src, char *dst, size_t cap )
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;

    while (*src && o + 4 < cap)
    {
        unsigned char c = (unsigned char)*src++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            dst[o++] = (char)c;
        }
        else
        {
            dst[o++] = '%';
            dst[o++] = hex[(c >> 4) & 0xf];
            dst[o++] = hex[c & 0xf];
        }
    }
    dst[o] = 0;
}

static void url_decode_inplace( char *s )
{
    char *r = s, *w = s;

    while (*r)
    {
        if (*r == '%' && r[1] && r[2])
        {
            char buf[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol( buf, NULL, 16 );
            r += 3;
        }
        else if (*r == '+')
        {
            *w++ = ' ';
            r++;
        }
        else
        {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static BOOL json_extract_string( const char *json, const char *key, char *out, size_t cap )
{
    char pat[128];
    const char *p;
    size_t o = 0;

    snprintf( pat, sizeof(pat), "\"%s\"", key );
    p = strstr( json, pat );
    if (!p) return FALSE;
    p += strlen( pat );
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return FALSE;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return FALSE;
    p++;

    while (*p && *p != '"' && o + 1 < cap)
    {
        if (*p == '\\' && p[1])
        {
            p++;
            if (*p == 'n') out[o++] = '\n';
            else if (*p == 'r') out[o++] = '\r';
            else if (*p == 't') out[o++] = '\t';
            else out[o++] = *p;
            p++;
        }
        else
        {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    return o > 0;
}

static BOOL json_extract_uint64( const char *json, const char *key, ULONGLONG *out )
{
    char pat[128];
    const char *p;

    snprintf( pat, sizeof(pat), "\"%s\"", key );
    p = strstr( json, pat );
    if (!p) return FALSE;
    p += strlen( pat );
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return FALSE;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '"') p++;
    if (*p < '0' || *p > '9') return FALSE;
    *out = _strtoui64( p, NULL, 10 );
    return TRUE;
}

static BOOL wam_winhttp_post_form( const WCHAR *host, const WCHAR *path,
                                   const char *form_body, char *resp_buf, size_t resp_cap )
{
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    DWORD status = 0, status_sz = sizeof(status);
    DWORD body_len = (DWORD)strlen( form_body );
    size_t total = 0;
    BOOL ok = FALSE;

    hSession = WinHttpOpen( L"Wine-WAM-Broker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
    if (!hSession) return FALSE;

    hConnect = WinHttpConnect( hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0 );
    if (!hConnect) goto done;

    hRequest = WinHttpOpenRequest( hConnect, L"POST", path, NULL, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE );
    if (!hRequest) goto done;

    if (!WinHttpAddRequestHeaders( hRequest, L"Content-Type: application/x-www-form-urlencoded\r\n",
                                   (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD ))
        goto done;

    if (!WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             (LPVOID)form_body, body_len, body_len, 0 ))
        goto done;

    if (!WinHttpReceiveResponse( hRequest, NULL ))
        goto done;

    if (!WinHttpQueryHeaders( hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX ))
        goto done;

    for (;;)
    {
        DWORD avail = 0, read = 0;
        if (!WinHttpQueryDataAvailable( hRequest, &avail ) || !avail) break;
        if (total + 1 >= resp_cap) break;
        if (avail > resp_cap - 1 - total) avail = (DWORD)(resp_cap - 1 - total);
        if (!WinHttpReadData( hRequest, resp_buf + total, avail, &read ) || !read) break;
        total += read;
    }
    resp_buf[total] = 0;
    ok = (status >= 200 && status < 300);

done:
    if (hRequest) WinHttpCloseHandle( hRequest );
    if (hConnect) WinHttpCloseHandle( hConnect );
    if (hSession) WinHttpCloseHandle( hSession );
    return ok;
}

static BOOL parse_token_response( const char *json, struct wam_token_entry *entry )
{
    char tok[WAM_MAX_TOKEN], rtok[WAM_MAX_TOKEN];
    ULONGLONG expires_in = 3600;

    if (!json_extract_string( json, "access_token", tok, sizeof(tok) ))
        return FALSE;

    MultiByteToWideChar( CP_UTF8, 0, tok, -1, entry->access_token, ARRAY_SIZE(entry->access_token) );
    if (json_extract_string( json, "refresh_token", rtok, sizeof(rtok) ))
        MultiByteToWideChar( CP_UTF8, 0, rtok, -1, entry->refresh_token, ARRAY_SIZE(entry->refresh_token) );

    json_extract_uint64( json, "expires_in", &expires_in );
    entry->expires_at = now_unix_seconds() + expires_in;
    return TRUE;
}

static BOOL wam_oauth2_refresh( const WCHAR *authority, const WCHAR *client_id,
                                struct wam_token_entry *entry )
{
    WCHAR tenant[128], path[256];
    char cid_utf8[256], rtok_utf8[WAM_MAX_TOKEN], scope_utf8[512];
    char cid_enc[512], rtok_enc[WAM_MAX_TOKEN], scope_enc[1024];
    char body[WAM_MAX_TOKEN + 2048];
    char *resp;
    BOOL ok;

    if (!entry->refresh_token[0]) return FALSE;

    extract_tenant( authority, tenant, ARRAY_SIZE(tenant) );
    swprintf( path, ARRAY_SIZE(path), L"/%ls/oauth2/v2.0/token", tenant );

    WideCharToMultiByte( CP_UTF8, 0, client_id, -1, cid_utf8, sizeof(cid_utf8), NULL, NULL );
    WideCharToMultiByte( CP_UTF8, 0, entry->refresh_token, -1, rtok_utf8, sizeof(rtok_utf8), NULL, NULL );
    WideCharToMultiByte( CP_UTF8, 0, entry->scope[0] ? entry->scope : L"openid profile offline_access",
                         -1, scope_utf8, sizeof(scope_utf8), NULL, NULL );

    url_encode_utf8( cid_utf8, cid_enc, sizeof(cid_enc) );
    url_encode_utf8( rtok_utf8, rtok_enc, sizeof(rtok_enc) );
    url_encode_utf8( scope_utf8, scope_enc, sizeof(scope_enc) );

    snprintf( body, sizeof(body),
              "grant_type=refresh_token&client_id=%s&refresh_token=%s&scope=%s",
              cid_enc, rtok_enc, scope_enc );

    if (!(resp = malloc( 65536 ))) return FALSE;
    ok = wam_winhttp_post_form( L"login.microsoftonline.com", path, body, resp, 65536 );
    if (ok) ok = parse_token_response( resp, entry );
    free( resp );

    if (ok) wam_cache_save( authority, client_id, entry );
    return ok;
}

static BOOL wam_cache_load( const WCHAR *authority, const WCHAR *client_id, struct wam_token_entry *entry )
{
    WCHAR subkey[384];
    HKEY hkey;
    DWORD type = 0, cb = sizeof(entry->expires_at);
    ULONGLONG now = now_unix_seconds();

    memset( entry, 0, sizeof(*entry) );
    build_cache_subkey( authority, client_id, subkey, ARRAY_SIZE(subkey) );

    if (RegOpenKeyExW( HKEY_CURRENT_USER, subkey, 0, KEY_READ, &hkey ) != ERROR_SUCCESS)
        return FALSE;

    reg_read_wstr( hkey, L"access_token", entry->access_token, ARRAY_SIZE(entry->access_token) );
    reg_read_wstr( hkey, L"refresh_token", entry->refresh_token, ARRAY_SIZE(entry->refresh_token) );
    reg_read_wstr( hkey, L"user_name", entry->user_name, ARRAY_SIZE(entry->user_name) );
    reg_read_wstr( hkey, L"account_id", entry->account_id, ARRAY_SIZE(entry->account_id) );
    reg_read_wstr( hkey, L"scope", entry->scope, ARRAY_SIZE(entry->scope) );
    if (RegQueryValueExW( hkey, L"expires_at", NULL, &type, (BYTE *)&entry->expires_at, &cb ) != ERROR_SUCCESS ||
        type != REG_QWORD)
        entry->expires_at = 0;
    RegCloseKey( hkey );

    if (entry->access_token[0] && (entry->expires_at == 0 || entry->expires_at > now + 60))
        return TRUE;

    if (entry->refresh_token[0])
        return wam_oauth2_refresh( authority, client_id, entry );

    return FALSE;
}

static BOOL wam_is_interactive_enabled( void )
{
    WCHAR val[16];
    DWORD len = GetEnvironmentVariableW( L"WINE_WAM_INTERACTIVE", val, ARRAY_SIZE(val) );
    return (len > 0 && val[0] == '1');
}

static BOOL wam_oauth2_interactive( const WCHAR *authority, const WCHAR *client_id,
                                    const WCHAR *scope, struct wam_token_entry *entry )
{
    WSADATA wsa;
    SOCKET lsock = INVALID_SOCKET, csock = INVALID_SOCKET;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    unsigned short port;
    char verifier[128], challenge[128];
    WCHAR tenant[128], path[256], url[2048];
    char cid_utf8[256], scope_utf8[512], redir_utf8[128];
    char cid_enc[512], scope_enc[1024], redir_enc[256];
    char url_utf8[2048], req_buf[4096], code[1024], code_enc[2048];
    char body[4096];
    char *resp = NULL, *p, *end;
    fd_set rfds;
    struct timeval tv;
    int timeout_sec = 120;
    WCHAR tmo_env[16];
    BOOL ok = FALSE;

    if (!wam_generate_pkce( verifier, sizeof(verifier), challenge, sizeof(challenge) ))
        return FALSE;

    if (WSAStartup( MAKEWORD(2, 2), &wsa ) != 0)
        return FALSE;

    lsock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if (lsock == INVALID_SOCKET) goto done;

    memset( &addr, 0, sizeof(addr) );
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    addr.sin_port = 0;

    if (bind( lsock, (struct sockaddr *)&addr, sizeof(addr) ) != 0) goto done;
    if (getsockname( lsock, (struct sockaddr *)&addr, &addrlen ) != 0) goto done;
    if (listen( lsock, 1 ) != 0) goto done;
    port = ntohs( addr.sin_port );

    extract_tenant( authority, tenant, ARRAY_SIZE(tenant) );
    WideCharToMultiByte( CP_UTF8, 0, client_id, -1, cid_utf8, sizeof(cid_utf8), NULL, NULL );
    WideCharToMultiByte( CP_UTF8, 0, (scope && *scope) ? scope : L"openid profile offline_access",
                         -1, scope_utf8, sizeof(scope_utf8), NULL, NULL );
    snprintf( redir_utf8, sizeof(redir_utf8), "http://127.0.0.1:%u/callback", (unsigned int)port );

    url_encode_utf8( cid_utf8, cid_enc, sizeof(cid_enc) );
    url_encode_utf8( scope_utf8, scope_enc, sizeof(scope_enc) );
    url_encode_utf8( redir_utf8, redir_enc, sizeof(redir_enc) );

    {
        char tenant_utf8[128];
        WideCharToMultiByte( CP_UTF8, 0, tenant, -1, tenant_utf8, sizeof(tenant_utf8), NULL, NULL );
        snprintf( url_utf8, sizeof(url_utf8),
                  "https://login.microsoftonline.com/%s/oauth2/v2.0/authorize"
                  "?client_id=%s&response_type=code&redirect_uri=%s&scope=%s"
                  "&code_challenge=%s&code_challenge_method=S256",
                  tenant_utf8, cid_enc, redir_enc, scope_enc, challenge );
    }

    MultiByteToWideChar( CP_UTF8, 0, url_utf8, -1, url, ARRAY_SIZE(url) );
    TRACE( "launching interactive WAM OAuth2 browser: %s.\n", debugstr_w( url ) );
    ShellExecuteW( NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL );

    if (GetEnvironmentVariableW( L"WINE_WAM_TIMEOUT_SEC", tmo_env, ARRAY_SIZE(tmo_env) ) > 0)
    {
        int v = _wtoi( tmo_env );
        if (v > 0 && v <= 600) timeout_sec = v;
    }

    FD_ZERO( &rfds );
    FD_SET( lsock, &rfds );
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    if (select( 0, &rfds, NULL, NULL, &tv ) <= 0) goto done;
    csock = accept( lsock, NULL, NULL );
    if (csock == INVALID_SOCKET) goto done;

    {
        int n = recv( csock, req_buf, sizeof(req_buf) - 1, 0 );
        static const char html_ok[] =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
            "<html><body><h3>Sign-in complete. You may close this window.</h3></body></html>";
        send( csock, html_ok, (int)strlen( html_ok ), 0 );
        if (n <= 0) goto done;
        req_buf[n] = 0;
    }

    p = strstr( req_buf, "code=" );
    if (!p) goto done;
    p += 5;
    end = p;
    while (*end && *end != '&' && *end != ' ' && *end != '\r' && *end != '\n') end++;
    if (end == p || (size_t)(end - p) >= sizeof(code)) goto done;
    memcpy( code, p, (size_t)(end - p) );
    code[end - p] = 0;
    url_decode_inplace( code );
    url_encode_utf8( code, code_enc, sizeof(code_enc) );

    swprintf( path, ARRAY_SIZE(path), L"/%ls/oauth2/v2.0/token", tenant );
    snprintf( body, sizeof(body),
              "grant_type=authorization_code&client_id=%s&code=%s&redirect_uri=%s&code_verifier=%s",
              cid_enc, code_enc, redir_enc, verifier );

    if (!(resp = malloc( 65536 ))) goto done;
    if (wam_winhttp_post_form( L"login.microsoftonline.com", path, body, resp, 65536 ))
    {
        memset( entry, 0, sizeof(*entry) );
        if (parse_token_response( resp, entry ))
        {
            if (scope && *scope)
                wcsncpy( entry->scope, scope, ARRAY_SIZE(entry->scope) - 1 );
            wam_cache_save( authority, client_id, entry );
            ok = TRUE;
        }
    }

done:
    free( resp );
    if (csock != INVALID_SOCKET) closesocket( csock );
    if (lsock != INVALID_SOCKET) closesocket( lsock );
    WSACleanup();
    return ok;
}

IInspectable *wam_broker_acquire_token(
        __x_ABI_CWindows_CSecurity_CAuthentication_CWeb_CCore_CIWebTokenRequest *request,
        BOOL allow_interactive )
{
    static const GUID IID_IWebAccountProvider2_local =
        { 0x4a01eb05, 0x4e42, 0x41d4, { 0xb5, 0x18, 0xe0, 0x08, 0xa5, 0x16, 0x36, 0x14 } };
    __x_ABI_CWindows_CSecurity_CCredentials_CIWebAccountProvider *provider = NULL;
    HSTRING h_client_id = NULL, h_scope = NULL, h_auth = NULL;
    const WCHAR *client_id = L"", *scope = L"", *authority = L"consumers";
    struct wam_token_entry entry;
    BOOL have_token = FALSE;
    IInspectable *result = NULL;

    if (!request) return create_web_token_request_result();

    request->lpVtbl->get_ClientId( request, &h_client_id );
    request->lpVtbl->get_Scope( request, &h_scope );
    request->lpVtbl->get_WebAccountProvider( request, &provider );

    if (h_client_id) client_id = WindowsGetStringRawBuffer( h_client_id, NULL );
    if (h_scope) scope = WindowsGetStringRawBuffer( h_scope, NULL );

    if (provider)
    {
        void *prov2 = NULL;
        if (SUCCEEDED( IUnknown_QueryInterface( (IUnknown *)provider, &IID_IWebAccountProvider2_local, &prov2 ) ) && prov2)
        {
            HRESULT (WINAPI *get_auth)( void *, HSTRING * ) =
                (HRESULT (WINAPI *)( void *, HSTRING * ))(*(void ***)prov2)[7];
            get_auth( prov2, &h_auth );
            IUnknown_Release( (IUnknown *)prov2 );
        }
        if (!h_auth || !*WindowsGetStringRawBuffer( h_auth, NULL ))
        {
            if (h_auth) { WindowsDeleteString( h_auth ); h_auth = NULL; }
            provider->lpVtbl->get_Id( provider, &h_auth );
        }
        if (h_auth && *WindowsGetStringRawBuffer( h_auth, NULL ))
            authority = WindowsGetStringRawBuffer( h_auth, NULL );
    }

    if (client_id && *client_id)
    {
        have_token = wam_cache_load( authority, client_id, &entry );
        if (!have_token && allow_interactive && wam_is_interactive_enabled())
            have_token = wam_oauth2_interactive( authority, client_id, scope, &entry );
    }

    if (have_token)
    {
        IInspectable *account = create_web_account_ex( (IInspectable *)provider,
                                                       entry.user_name,
                                                       entry.account_id,
                                                       WebAccountState_Connected );
        IInspectable *response = create_web_token_response_ex( entry.access_token, account );
        IVectorView_WebTokenResponse *vec = create_web_token_response_vector_view( response );

        result = create_web_token_request_result_ex( WebTokenRequestStatus_Success, vec,
                                                     authority, client_id );
        if (vec) IVectorView_WebTokenResponse_Release( vec );
        if (response) IInspectable_Release( response );
        if (account) IInspectable_Release( account );
    }
    else
    {
        result = create_web_token_request_result();
    }

    if (h_auth) WindowsDeleteString( h_auth );
    if (h_scope) WindowsDeleteString( h_scope );
    if (h_client_id) WindowsDeleteString( h_client_id );
    if (provider) IUnknown_Release( (IUnknown *)provider );
    return result;
}
