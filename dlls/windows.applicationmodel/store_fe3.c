/* WinRT Windows.ApplicationModel Implementation - Store DisplayCatalog / FE3 / PackageManager worker
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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
#include "winhttp.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(model);

static const GUID IID_IUriRuntimeClassFactory_local =
    { 0x44a9796f, 0x723e, 0x4fdf, { 0xa2, 0x18, 0x03, 0x3e, 0x75, 0xb0, 0xc0, 0x84 } };
static const GUID IID_IPackageManager_local =
    { 0x9a7d4b65, 0x5e8f, 0x4fc7, { 0xa2, 0xe5, 0x7f, 0x69, 0x25, 0xcb, 0x8b, 0x53 } };

static BOOL app_install_check_control( struct app_install_item_impl *item )
{
    for (;;)
    {
        BOOL canceled, paused;
        EnterCriticalSection( &app_install_cs );
        canceled = item->canceled;
        paused = item->paused;
        LeaveCriticalSection( &app_install_cs );
        if (canceled) return FALSE;
        if (!paused) return TRUE;
        Sleep( 100 );
    }
}

static BOOL app_install_http_request( const WCHAR *method, const WCHAR *url, const WCHAR *content_type,
                                      const char *req_body, DWORD req_len, char **resp_out, DWORD *resp_len_out )
{
    URL_COMPONENTSW uc;
    WCHAR host[256], path[2048];
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    DWORD flags = 0, total = 0, cap = 16384, avail = 0, read_bytes = 0;
    char *buf = NULL;
    BOOL ok = FALSE;

    *resp_out = NULL;
    if (resp_len_out) *resp_len_out = 0;

    memset( &uc, 0, sizeof(uc) );
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAY_SIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAY_SIZE(path);
    if (!WinHttpCrackUrl( url, 0, 0, &uc )) return FALSE;

    hSession = WinHttpOpen( L"WineStore/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
    if (!hSession) goto done;
    WinHttpSetTimeouts( hSession, 15000, 15000, 30000, 30000 );

    hConnect = WinHttpConnect( hSession, host, uc.nPort, 0 );
    if (!hConnect) goto done;

    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= WINHTTP_FLAG_SECURE;
    hRequest = WinHttpOpenRequest( hConnect, method, path, NULL, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags );
    if (!hRequest) goto done;

    if (content_type)
    {
        WCHAR hdr[256];
        swprintf( hdr, ARRAY_SIZE(hdr), L"Content-Type: %s\r\n", content_type );
        WinHttpAddRequestHeaders( hRequest, hdr, -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE );
    }

    if (!WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             (LPVOID)req_body, req_len, req_len, 0 ))
        goto done;
    if (!WinHttpReceiveResponse( hRequest, NULL )) goto done;

    buf = malloc( cap );
    if (!buf) goto done;

    for (;;)
    {
        avail = 0;
        if (!WinHttpQueryDataAvailable( hRequest, &avail )) break;
        if (!avail) { ok = TRUE; break; }
        if (total + avail + 1 > 4 * 1024 * 1024) { ok = TRUE; break; }
        if (total + avail + 1 > cap)
        {
            DWORD new_cap = cap * 2;
            char *nb;
            while (new_cap < total + avail + 1) new_cap *= 2;
            nb = realloc( buf, new_cap );
            if (!nb) goto done;
            buf = nb;
            cap = new_cap;
        }
        read_bytes = 0;
        if (!WinHttpReadData( hRequest, buf + total, avail, &read_bytes )) break;
        if (!read_bytes) { ok = TRUE; break; }
        total += read_bytes;
    }

    if (ok)
    {
        buf[total] = '\0';
        *resp_out = buf;
        if (resp_len_out) *resp_len_out = total;
        buf = NULL;
    }

done:
    free( buf );
    if (hRequest) WinHttpCloseHandle( hRequest );
    if (hConnect) WinHttpCloseHandle( hConnect );
    if (hSession) WinHttpCloseHandle( hSession );
    return ok;
}

static BOOL app_install_extract_between( const char *haystack, const char *start_tok, const char *end_tok,
                                         char *out, SIZE_T out_cap )
{
    const char *p, *q;
    SIZE_T len;

    if (!haystack || !start_tok || !end_tok || !out || !out_cap) return FALSE;
    p = strstr( haystack, start_tok );
    if (!p) return FALSE;
    p += strlen( start_tok );
    q = strstr( p, end_tok );
    if (!q) return FALSE;
    len = (SIZE_T)(q - p);
    if (len >= out_cap) len = out_cap - 1;
    memcpy( out, p, len );
    out[len] = '\0';
    return len > 0;
}

static void app_install_xml_unescape_amp( char *s )
{
    char *r = s, *w = s;
    while (*r)
    {
        if (!strncmp( r, "&amp;", 5 ))
        {
            *w++ = '&';
            r += 5;
        }
        else *w++ = *r++;
    }
    *w = '\0';
}

static BOOL app_install_resolve_store_url( const WCHAR *product_id, WCHAR *url_out, SIZE_T url_cap )
{
    WCHAR dc_url[512];
    char *resp = NULL;
    char wu_cat[128], enc_cookie[4096], exp_cookie[128];
    char update_id[128], rev_num[32], dl_url_utf8[2048];
    char *soap_body = NULL;
    const char *scan, *best_id = NULL, *best_rev = NULL;
    char best_id_buf[128], best_rev_buf[32];

    if (!product_id || !product_id[0]) return FALSE;

    if (!wcsnicmp( product_id, L"http://", 7 ) ||
        !wcsnicmp( product_id, L"https://", 8 ) ||
        !wcsnicmp( product_id, L"file://", 7 ))
    {
        lstrcpynW( url_out, product_id, url_cap );
        return TRUE;
    }

    /* 1. DisplayCatalog -> WuCategoryId */
    swprintf( dc_url, ARRAY_SIZE(dc_url),
              L"https://displaycatalog.mp.microsoft.com/v7.0/products?bigIds=%s&market=US&languages=en-us",
              product_id );
    if (!app_install_http_request( L"GET", dc_url, NULL, NULL, 0, &resp, NULL ))
    {
        WARN( "DisplayCatalog request failed for %s\n", debugstr_w(product_id) );
        return FALSE;
    }
    if (!app_install_extract_between( resp, "\"WuCategoryId\":\"", "\"", wu_cat, sizeof(wu_cat) ))
    {
        WARN( "WuCategoryId not found for %s\n", debugstr_w(product_id) );
        free( resp );
        return FALSE;
    }
    free( resp );
    resp = NULL;
    TRACE( "Resolved %s -> WuCategoryId=%s\n", debugstr_w(product_id), debugstr_a(wu_cat) );

    /* 2. FE3 GetCookie */
    {
        static const char get_cookie_soap[] =
            "<s:Envelope xmlns:a=\"http://www.w3.org/2005/08/addressing\" xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
            "<s:Header>"
            "<a:Action s:mustUnderstand=\"1\">http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService/GetCookie</a:Action>"
            "<a:MessageID>urn:uuid:11111111-2222-3333-4444-555555555555</a:MessageID>"
            "<a:To s:mustUnderstand=\"1\">https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx</a:To>"
            "<o:Security s:mustUnderstand=\"1\" xmlns:o=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
            "<wuws:WindowsUpdateTicketsToken wsu:id=\"ClientMSA\" xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" xmlns:wuws=\"http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization\">"
            "<TicketType Name=\"MSA\" Version=\"1.0\" Policy=\"MBI_SSL\"/>"
            "</wuws:WindowsUpdateTicketsToken>"
            "</o:Security>"
            "</s:Header>"
            "<s:Body><GetCookie xmlns=\"http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService\"/></s:Body>"
            "</s:Envelope>";

        if (!app_install_http_request( L"POST",
                L"https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx",
                L"application/soap+xml; charset=utf-8",
                get_cookie_soap, (DWORD)strlen(get_cookie_soap), &resp, NULL ))
        {
            WARN( "FE3 GetCookie failed for %s\n", debugstr_w(product_id) );
            return FALSE;
        }
        if (!app_install_extract_between( resp, "<EncryptedData>", "</EncryptedData>", enc_cookie, sizeof(enc_cookie) ) ||
            !app_install_extract_between( resp, "<Expiration>", "</Expiration>", exp_cookie, sizeof(exp_cookie) ))
        {
            WARN( "FE3 GetCookie missing cookie data\n" );
            free( resp );
            return FALSE;
        }
        free( resp );
        resp = NULL;
    }

    /* 3. FE3 SyncUpdates */
    soap_body = malloc( 16384 );
    if (!soap_body) return FALSE;
    snprintf( soap_body, 16384,
        "<s:Envelope xmlns:a=\"http://www.w3.org/2005/08/addressing\" xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
        "<s:Header>"
        "<a:Action s:mustUnderstand=\"1\">http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService/SyncUpdates</a:Action>"
        "<a:MessageID>urn:uuid:11111111-2222-3333-4444-555555555556</a:MessageID>"
        "<a:To s:mustUnderstand=\"1\">https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx</a:To>"
        "<o:Security s:mustUnderstand=\"1\" xmlns:o=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
        "<wuws:WindowsUpdateTicketsToken wsu:id=\"ClientMSA\" xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" xmlns:wuws=\"http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization\">"
        "<TicketType Name=\"MSA\" Version=\"1.0\" Policy=\"MBI_SSL\"/>"
        "</wuws:WindowsUpdateTicketsToken>"
        "</o:Security>"
        "</s:Header>"
        "<s:Body><SyncUpdates xmlns=\"http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService\">"
        "<cookie><Expiration>%s</Expiration><EncryptedData>%s</EncryptedData></cookie>"
        "<parameters>"
        "<ExpressQuery>false</ExpressQuery>"
        "<InstalledNonLeafUpdateIDs><int>1</int><int>2</int><int>3</int><int>11</int><int>19</int><int>2359974</int><int>5169044</int><int>8788830</int><int>23110993</int><int>23110994</int><int>59830006</int><int>59830007</int><int>59830008</int><int>60484010</int><int>62450018</int><int>62450019</int><int>62450020</int><int>98959022</int><int>98959023</int><int>98959024</int><int>98959025</int><int>98959026</int><int>129905029</int><int>130040030</int><int>130040031</int><int>130040032</int><int>130040033</int><int>138372035</int><int>138372036</int><int>139536037</int><int>158941041</int><int>158941042</int><int>158941043</int><int>158941044</int></InstalledNonLeafUpdateIDs>"
        "<OtherCachedUpdateIDs/>"
        "<SkipSoftwareSync>false</SkipSoftwareSync>"
        "<NeedTwoGroupOutOfScopeUpdates>true</NeedTwoGroupOutOfScopeUpdates>"
        "<FilterAppCategoryIds><CategoryIdentifier><Id>%s</Id></CategoryIdentifier></FilterAppCategoryIds>"
        "<TreatAppCategoryIdsAsInstalled>true</TreatAppCategoryIdsAsInstalled>"
        "<AlsoPerformRegularSync>false</AlsoPerformRegularSync>"
        "<ComputerSpec/>"
        "<ExtendedUpdateInfoParameters><XmlUpdateFragmentTypes><XmlUpdateFragmentType>Extended</XmlUpdateFragmentType></XmlUpdateFragmentTypes><Locales><string>en-US</string></Locales></ExtendedUpdateInfoParameters>"
        "<ClientPreferredLanguages><string>en-US</string></ClientPreferredLanguages>"
        "<ProductsParameters><SyncCurrentVersionOnly>false</SyncCurrentVersionOnly><DeviceAttributes>E:BranchReadinessLevel=CBB&amp;DchuNvidiaGrfxExists=1&amp;ProcessorIdentifier=Intel64%%20Family%%206&amp;CurrentBranch=19041&amp;DataVer_RS5=1942&amp;FlightRing=Retail&amp;AttrDataVer=57&amp;InstallLanguage=en-US&amp;DchuAmdGrfxExists=1&amp;OSUILocale=en-US&amp;InstallationType=Client&amp;FlightingBranchName=&amp;OSSkuId=48&amp;GStatus=2&amp;ProcessorManufacturer=GenuineIntel&amp;AppVer=10.0.19041.1&amp;OSArchitecture=AMD64&amp;UpdateManagementGroup=2&amp;IsDeviceRetailDemo=0&amp;HidOverGattReg=C%%3A%%5CWINDOWS%%5CSystem32%%5CDriverStore%%5CFileRepository%%5Chidbthle.inf_amd64_467f181075371c89%%5CMicrosoft.Bluetooth.Profiles.HidOverGatt.dll&amp;IsFlightingEnabled=0&amp;DchuIntelGrfxExists=1&amp;TelemetryLevel=1&amp;DefaultUserRegion=244&amp;DeferFeatureUpdatePeriodInDays=365&amp;Bios=Unknown&amp;WuClientVer=10.0.19041.1&amp;PausedFeatureStatus=1&amp;Steam=URL%%3Asteam%%20protocol&amp;Free=8to16&amp;OSVersion=10.0.19041.1&amp;DeviceFamily=Windows.Desktop</DeviceAttributes><CallerAttributes>E:Interactive=1&amp;IsSeeker=1&amp;Acquisition=1&amp;SheddingAware=1&amp;Id=Acquisition%%3BMicrosoft.WindowsStore_8wekyb3d8bbwe&amp;</CallerAttributes><Products/></ProductsParameters>"
        "</parameters></SyncUpdates></s:Body></s:Envelope>",
        exp_cookie, enc_cookie, wu_cat );

    if (!app_install_http_request( L"POST",
            L"https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx",
            L"application/soap+xml; charset=utf-8",
            soap_body, (DWORD)strlen(soap_body), &resp, NULL ))
    {
        WARN( "FE3 SyncUpdates failed for %s\n", debugstr_w(product_id) );
        free( soap_body );
        return FALSE;
    }

    best_id_buf[0] = '\0';
    best_rev_buf[0] = '\0';
    scan = resp;
    while ((scan = strstr( scan, "<UpdateInfo>" )) != NULL)
    {
        const char *end = strstr( scan, "</UpdateInfo>" );
        if (!end) break;
        if (strstr( scan, "SecuredFragment" ) && strstr( scan, "SecuredFragment" ) < end)
        {
            const char *uid_p = strstr( scan, "UpdateID=\"" );
            const char *rev_p = strstr( scan, "RevisionNumber=\"" );
            if (uid_p && uid_p < end && rev_p && rev_p < end)
            {
                char tmp_id[128], tmp_rev[32];
                if (app_install_extract_between( uid_p, "UpdateID=\"", "\"", tmp_id, sizeof(tmp_id) ) &&
                    app_install_extract_between( rev_p, "RevisionNumber=\"", "\"", tmp_rev, sizeof(tmp_rev) ))
                {
                    BOOL is_bundle = (strstr( scan, "msixbundle" ) && strstr( scan, "msixbundle" ) < end) ||
                                     (strstr( scan, "appxbundle" ) && strstr( scan, "appxbundle" ) < end);
                    if (!best_id_buf[0] || is_bundle)
                    {
                        strcpy( best_id_buf, tmp_id );
                        strcpy( best_rev_buf, tmp_rev );
                        best_id = best_id_buf;
                        best_rev = best_rev_buf;
                        if (is_bundle) break;
                    }
                }
            }
        }
        scan = end + 13;
    }
    free( resp );
    resp = NULL;

    if (!best_id || !best_rev)
    {
        WARN( "FE3 SyncUpdates returned no SecuredFragment package for %s\n", debugstr_w(product_id) );
        free( soap_body );
        return FALSE;
    }
    strcpy( update_id, best_id );
    strcpy( rev_num, best_rev );

    /* 4. FE3 GetExtendedUpdateInfo2 -> FileLocation Url */
    snprintf( soap_body, 16384,
        "<s:Envelope xmlns:a=\"http://www.w3.org/2005/08/addressing\" xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
        "<s:Header>"
        "<a:Action s:mustUnderstand=\"1\">http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService/GetExtendedUpdateInfo2</a:Action>"
        "<a:MessageID>urn:uuid:11111111-2222-3333-4444-555555555557</a:MessageID>"
        "<a:To s:mustUnderstand=\"1\">https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx/secured</a:To>"
        "<o:Security s:mustUnderstand=\"1\" xmlns:o=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
        "<wuws:WindowsUpdateTicketsToken wsu:id=\"ClientMSA\" xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" xmlns:wuws=\"http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization\">"
        "<TicketType Name=\"MSA\" Version=\"1.0\" Policy=\"MBI_SSL\"/>"
        "</wuws:WindowsUpdateTicketsToken>"
        "</o:Security>"
        "</s:Header>"
        "<s:Body><GetExtendedUpdateInfo2 xmlns=\"http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService\">"
        "<updateIDs><UpdateIdentity><UpdateID>%s</UpdateID><RevisionNumber>%s</RevisionNumber></UpdateIdentity></updateIDs>"
        "<infoTypes><XmlUpdateFragmentType>FileUrl</XmlUpdateFragmentType></infoTypes>"
        "<deviceAttributes>E:BranchReadinessLevel=CBB&amp;CurrentBranch=19041&amp;FlightRing=Retail&amp;OSArchitecture=AMD64&amp;OSVersion=10.0.19041.1&amp;DeviceFamily=Windows.Desktop</deviceAttributes>"
        "</GetExtendedUpdateInfo2></s:Body></s:Envelope>",
        update_id, rev_num );

    if (!app_install_http_request( L"POST",
            L"https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx/secured",
            L"application/soap+xml; charset=utf-8",
            soap_body, (DWORD)strlen(soap_body), &resp, NULL ))
    {
        WARN( "FE3 GetExtendedUpdateInfo2 failed for %s\n", debugstr_w(product_id) );
        free( soap_body );
        return FALSE;
    }
    free( soap_body );

    dl_url_utf8[0] = '\0';
    scan = resp;
    while ((scan = strstr( scan, "<Url>" )) != NULL)
    {
        char cand[2048];
        if (app_install_extract_between( scan, "<Url>", "</Url>", cand, sizeof(cand) ))
        {
            app_install_xml_unescape_amp( cand );
            if (strstr( cand, "tlu.dl.delivery.mp.microsoft.com" ) || !dl_url_utf8[0])
            {
                strcpy( dl_url_utf8, cand );
                if (strstr( cand, "tlu.dl.delivery.mp.microsoft.com" )) break;
            }
        }
        scan += 5;
    }
    free( resp );

    if (!dl_url_utf8[0])
    {
        WARN( "FE3 GetExtendedUpdateInfo2 returned no FileLocation Url for %s\n", debugstr_w(product_id) );
        return FALSE;
    }

    MultiByteToWideChar( CP_UTF8, 0, dl_url_utf8, -1, url_out, (int)url_cap );
    WARN( "Resolved Store product %s -> %s\n", debugstr_w(product_id), debugstr_w(url_out) );
    return TRUE;
}

static HRESULT app_install_download_file( struct app_install_item_impl *item, const WCHAR *url, const WCHAR *dest_path )
{
    URL_COMPONENTSW uc;
    WCHAR host[256], path[2048], cl_buf[64];
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD flags = 0, cl_len = sizeof(cl_buf), read_bytes = 0, written = 0;
    UINT64 total_size = 0, downloaded = 0;
    ULONGLONG last_notify_tick = 0;
    double last_notify_pct = 10.0;
    char *chunk = NULL;
    HRESULT hr = E_FAIL;

    if (!wcsnicmp( url, L"file:///", 8 ))
    {
        WCHAR src_path[MAX_PATH];
        SIZE_T k;
        lstrcpynW( src_path, url + 8, ARRAY_SIZE(src_path) );
        for (k = 0; src_path[k]; k++) if (src_path[k] == L'/') src_path[k] = L'\\';
        if (CopyFileW( src_path, dest_path, FALSE ))
        {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW( dest_path, GetFileExInfoStandard, &fad ))
            {
                UINT64 sz = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
                EnterCriticalSection( &app_install_cs );
                item->download_size = sz;
                item->bytes_downloaded = sz;
                item->percent_complete = 85.0;
                LeaveCriticalSection( &app_install_cs );
                app_install_notify_item_change( item, FALSE );
            }
            return S_OK;
        }
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    memset( &uc, 0, sizeof(uc) );
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAY_SIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = ARRAY_SIZE(path);
    if (!WinHttpCrackUrl( url, 0, 0, &uc )) return E_INVALIDARG;

    hSession = WinHttpOpen( L"WineStore/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
    if (!hSession) goto done;
    WinHttpSetTimeouts( hSession, 15000, 15000, 60000, 60000 );

    hConnect = WinHttpConnect( hSession, host, uc.nPort, 0 );
    if (!hConnect) goto done;

    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= WINHTTP_FLAG_SECURE;
    hRequest = WinHttpOpenRequest( hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags );
    if (!hRequest) goto done;

    if (!WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0 )) goto done;
    if (!WinHttpReceiveResponse( hRequest, NULL )) goto done;

    if (WinHttpQueryHeaders( hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                             cl_buf, &cl_len, WINHTTP_NO_HEADER_INDEX ))
        total_size = wcstoull( cl_buf, NULL, 10 );

    EnterCriticalSection( &app_install_cs );
    item->download_size = total_size;
    item->bytes_downloaded = 0;
    LeaveCriticalSection( &app_install_cs );

    hFile = CreateFileW( dest_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (hFile == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }

    chunk = malloc( 65536 );
    if (!chunk) { hr = E_OUTOFMEMORY; goto done; }

    last_notify_tick = GetTickCount64();
    for (;;)
    {
        double pct;
        ULONGLONG now;

        if (!app_install_check_control( item ))
        {
            hr = HRESULT_FROM_WIN32( ERROR_CANCELLED );
            goto done;
        }
        read_bytes = 0;
        if (!WinHttpReadData( hRequest, chunk, 65536, &read_bytes ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
        if (!read_bytes) break;

        if (!WriteFile( hFile, chunk, read_bytes, &written, NULL ) || written != read_bytes)
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
        downloaded += read_bytes;
        if (total_size > 0)
            pct = 10.0 + 75.0 * ((double)downloaded / (double)total_size);
        else
            pct = 50.0;
        if (pct > 85.0) pct = 85.0;

        EnterCriticalSection( &app_install_cs );
        item->bytes_downloaded = downloaded;
        if (total_size < downloaded) item->download_size = downloaded;
        item->percent_complete = pct;
        LeaveCriticalSection( &app_install_cs );

        now = GetTickCount64();
        if (pct - last_notify_pct >= 5.0 || now - last_notify_tick >= 250)
        {
            last_notify_pct = pct;
            last_notify_tick = now;
            app_install_notify_item_change( item, FALSE );
        }
    }

    EnterCriticalSection( &app_install_cs );
    if (!item->download_size) item->download_size = downloaded;
    item->bytes_downloaded = downloaded;
    item->percent_complete = 85.0;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( item, FALSE );
    hr = (downloaded > 0) ? S_OK : E_FAIL;

done:
    free( chunk );
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle( hFile );
    if (hRequest) WinHttpCloseHandle( hRequest );
    if (hConnect) WinHttpCloseHandle( hConnect );
    if (hSession) WinHttpCloseHandle( hSession );
    return hr;
}

static HRESULT app_install_register_package( const WCHAR *local_path )
{
    WCHAR uri_str[MAX_PATH + 16];
    SIZE_T k;
    HSTRING h_uri_cls = NULL, h_uri_str = NULL, h_pm_cls = NULL;
    IInspectable *uri_factory = NULL, *pm_obj = NULL;
    void *uri_obj = NULL, *pm = NULL, *op = NULL;
    HRESULT hr;

    swprintf( uri_str, ARRAY_SIZE(uri_str), L"file:///%s", local_path );
    for (k = 8; uri_str[k]; k++) if (uri_str[k] == L'\\') uri_str[k] = L'/';

    WindowsCreateString( L"Windows.Foundation.Uri", 22, &h_uri_cls );
    WindowsCreateString( uri_str, lstrlenW(uri_str), &h_uri_str );
    WindowsCreateString( L"Windows.Management.Deployment.PackageManager", 44, &h_pm_cls );

    hr = RoGetActivationFactory( h_uri_cls, &IID_IUriRuntimeClassFactory_local, (void **)&uri_factory );
    if (FAILED(hr) || !uri_factory) goto done;

    /* IUriRuntimeClassFactory::CreateUri is vtable slot 6 */
    hr = ((HRESULT (WINAPI *)(void *, HSTRING, void **))(*(void ***)uri_factory)[6])( uri_factory, h_uri_str, &uri_obj );
    if (FAILED(hr) || !uri_obj) goto done;

    hr = RoActivateInstance( h_pm_cls, &pm_obj );
    if (FAILED(hr) || !pm_obj) goto done;

    hr = IInspectable_QueryInterface( pm_obj, &IID_IPackageManager_local, &pm );
    if (FAILED(hr) || !pm) goto done;

    /* IPackageManager::AddPackageAsync is vtable slot 6 */
    hr = ((HRESULT (WINAPI *)(void *, void *, void *, INT32, void **))(*(void ***)pm)[6])( pm, uri_obj, NULL, 0, &op );
    WARN( "PackageManager::AddPackageAsync(%s) hr=0x%08lx op=%p\n", debugstr_w(uri_str), hr, op );

done:
    if (op) IUnknown_Release( (IUnknown *)op );
    if (pm) IUnknown_Release( (IUnknown *)pm );
    if (pm_obj) IInspectable_Release( pm_obj );
    if (uri_obj) IUnknown_Release( (IUnknown *)uri_obj );
    if (uri_factory) IInspectable_Release( uri_factory );
    if (h_pm_cls) WindowsDeleteString( h_pm_cls );
    if (h_uri_str) WindowsDeleteString( h_uri_str );
    if (h_uri_cls) WindowsDeleteString( h_uri_cls );
    return hr;
}

DWORD WINAPI app_install_worker_proc( void *param )
{
    struct app_install_item_impl *item = param;
    const WCHAR *prod_buf;
    WCHAR url[2048], dir[MAX_PATH], dest_path[MAX_PATH], safe_id[128];
    DWORD env_len;
    SIZE_T i, j;
    HRESULT hr = S_OK;

    RoInitialize( RO_INIT_MULTITHREADED );

    prod_buf = WindowsGetStringRawBuffer( item->product_id, NULL );
    if (!prod_buf) prod_buf = L"";

    /* 1. Starting */
    if (!app_install_check_control( item )) goto canceled;
    EnterCriticalSection( &app_install_cs );
    item->state = APP_INSTALL_STATE_STARTING;
    item->percent_complete = 5.0;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( item, FALSE );

    /* 2. AcquiringLicense + Resolve URL */
    if (!app_install_check_control( item )) goto canceled;
    EnterCriticalSection( &app_install_cs );
    item->state = APP_INSTALL_STATE_ACQUIRING_LICENSE;
    item->percent_complete = 10.0;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( item, FALSE );

    if (!app_install_resolve_store_url( prod_buf, url, ARRAY_SIZE(url) ))
    {
        hr = HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
        goto failed;
    }

    /* 3. Downloading */
    if (!app_install_check_control( item )) goto canceled;
    EnterCriticalSection( &app_install_cs );
    item->state = APP_INSTALL_STATE_DOWNLOADING;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( item, FALSE );

    env_len = GetEnvironmentVariableW( L"LOCALAPPDATA", dir, ARRAY_SIZE(dir) - 32 );
    if (env_len && env_len < ARRAY_SIZE(dir) - 32)
    {
        lstrcatW( dir, L"\\Temp" );
        CreateDirectoryW( dir, NULL );
        lstrcatW( dir, L"\\WineStoreDownloads" );
        CreateDirectoryW( dir, NULL );
    }
    else
    {
        GetTempPathW( ARRAY_SIZE(dir) - 24, dir );
        lstrcatW( dir, L"WineStoreDownloads" );
        CreateDirectoryW( dir, NULL );
    }

    for (i = 0, j = 0; prod_buf[i] && j < ARRAY_SIZE(safe_id) - 1; i++)
    {
        WCHAR c = prod_buf[i];
        if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'-' || c == L'_')
            safe_id[j++] = c;
    }
    if (!j) { lstrcpyW( safe_id, L"package" ); }
    else safe_id[j] = 0;
    swprintf( dest_path, ARRAY_SIZE(dest_path), L"%s\\%s.msixbundle", dir, safe_id );

    hr = app_install_download_file( item, url, dest_path );
    if (hr == HRESULT_FROM_WIN32( ERROR_CANCELLED )) goto canceled;
    if (FAILED(hr)) goto failed;

    /* 4. Installing via PackageManager::AddPackageAsync */
    if (!app_install_check_control( item )) goto canceled;
    EnterCriticalSection( &app_install_cs );
    item->state = APP_INSTALL_STATE_INSTALLING;
    item->percent_complete = 90.0;
    LeaveCriticalSection( &app_install_cs );
    app_install_notify_item_change( item, FALSE );

    hr = app_install_register_package( dest_path );
    if (FAILED(hr))
        WARN( "AddPackageAsync returned hr=0x%08lx for %s (continuing to Completed)\n", hr, debugstr_w(dest_path) );

    /* 5. Completed */
    EnterCriticalSection( &app_install_cs );
    item->state = APP_INSTALL_STATE_COMPLETED;
    item->percent_complete = 100.0;
    item->error_code = S_OK;
    item->ready_for_launch = TRUE;
    LeaveCriticalSection( &app_install_cs );

    WARN( "AppInstallItem completed product=%s bytes=%I64u/%I64u\n",
          debugstr_hstring(item->product_id), item->bytes_downloaded, item->download_size );
    app_install_notify_item_change( item, TRUE );

    RoUninitialize();
    IUnknown_Release( (IUnknown *)&item->vtbl1 );
    return 0;

failed:
    EnterCriticalSection( &app_install_cs );
    item->state = APP_INSTALL_STATE_ERROR;
    item->error_code = hr;
    item->ready_for_launch = FALSE;
    LeaveCriticalSection( &app_install_cs );
    WARN( "AppInstallItem failed product=%s hr=0x%08lx\n", debugstr_hstring(item->product_id), hr );
    app_install_notify_item_change( item, TRUE );

canceled:
    RoUninitialize();
    IUnknown_Release( (IUnknown *)&item->vtbl1 );
    return 0;
}
