/* WinRT Windows.Web.Http Implementation
 *
 * Copyright (C) 2026 Wine Developers
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


#include "http_private.h"

/* --- HttpClient --- */


static inline struct http_client *impl_from_client( void *iface )
{
    return (struct http_client *)iface;
}
#define HttpCompletionOption __x_ABI_CWindows_CWeb_CHttp_CHttpCompletionOption
#define HttpRequestMessage __x_ABI_CWindows_CWeb_CHttp_CIHttpRequestMessage
#define HttpRequestHeaderCollection __x_ABI_CWindows_CWeb_CHttp_CHeaders_CIHttpRequestHeaderCollection
#define AutomaticDecompression __x_ABI_CWindows_CWeb_CHttp_CFilters_CAutomaticDecompression
#define RESP_OP __FIAsyncOperationWithProgress_2_Windows__CWeb__CHttp__CHttpResponseMessage_HttpProgress
#define BUF_OP __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress
#define ISTRM_OP __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIInputStream_HttpProgress
#define STR_OP __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress
#define RESP_AO __FIAsyncOperation_1_Windows__CWeb__CHttp__CHttpResponseMessage
#define BUF_AO __FIAsyncOperation_1_Windows__CStorage__CStreams__CIBuffer
#define ISTRM_AO __FIAsyncOperation_1_Windows__CStorage__CStreams__CIInputStream
#define STR_AO __FIAsyncOperation_1_HSTRING

static inline struct http_client *impl_from_client2( void *iface )
{
    return (struct http_client *)((BYTE *)iface - sizeof(void *));
}
static inline struct http_client *impl_from_client_closable( void *iface )
{
    return (struct http_client *)((BYTE *)iface - 2 * sizeof(void *));
}

static HRESULT WINAPI client_QueryInterface( IHttpClient *This, REFIID iid, void **out )
{
    struct http_client *impl = impl_from_client( This );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpClient ))
    {
        *out = &impl->vtbl_client;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpClient2 ))
    {
        *out = &impl->vtbl_client2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI client_AddRef( IHttpClient *This )
{
    struct http_client *impl = impl_from_client( This );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI client_Release( IHttpClient *This )
{
    struct http_client *impl = impl_from_client( This );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->default_headers) IUnknown_Release( (IUnknown *)impl->default_headers );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI client_GetIids( IHttpClient *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI client_GetRuntimeClassName( IHttpClient *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpClient";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI client_GetTrustLevel( IHttpClient *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT http_fetch_to_response( const WCHAR *method, const WCHAR *uri,
                                       const void *body_data, DWORD body_len,
                                       void *req_headers, void *content_headers, void **out );

static HRESULT WINAPI client_DeleteAsync( IHttpClient *This, IUriRuntimeClass *uri, RESP_OP **op )
{
    void *resp = NULL;
    HRESULT hr = create_http_response( 200, &resp );
    if (FAILED(hr)) return hr;
    hr = create_async_http_op( resp, NULL, FALSE, (void **)op );
    IUnknown_Release( (IUnknown *)resp );
    return hr;
}
static HRESULT WINAPI client_GetAsync( IHttpClient *This, IUriRuntimeClass *uri, RESP_OP **op )
{
    struct http_client *impl = impl_from_client( This );
    void *resp = NULL;
    HSTRING huri = NULL;
    const WCHAR *uri_str = NULL;
    HRESULT hr;

    WDBG( "client_GetAsync uri %p\n", uri );
    if (uri && SUCCEEDED( IUriRuntimeClass_get_AbsoluteUri( uri, &huri ) ) && huri)
        uri_str = WindowsGetStringRawBuffer( huri, NULL );
    if (uri_str && uri_str[0])
        hr = http_fetch_to_response( L"GET", uri_str, NULL, 0, impl->default_headers, NULL, &resp );
    else
        hr = E_INVALIDARG;
    if (huri) WindowsDeleteString( huri );
    if (FAILED(hr) || !resp)
    {
        hr = create_http_response( 200, &resp );
        if (FAILED(hr)) return hr;
    }
    hr = create_async_http_op( resp, NULL, FALSE, (void **)op );
    IUnknown_Release( (IUnknown *)resp );
    return hr;
}
static HRESULT WINAPI client_GetAsync2( IHttpClient *This, IUriRuntimeClass *uri, HttpCompletionOption opt, RESP_OP **op )
{
    return client_GetAsync( This, uri, op );
}
static HRESULT WINAPI client_GetBufferAsync( IHttpClient *This, IUriRuntimeClass *uri, BUF_OP **op )
{
    WDBG( "client_GetBufferAsync\n" );
    return create_async_http_op( NULL, NULL, FALSE, (void **)op );
}
static HRESULT WINAPI client_GetInputStreamAsync( IHttpClient *This, IUriRuntimeClass *uri, ISTRM_OP **op )
{
    WDBG( "client_GetInputStreamAsync\n" );
    return create_async_http_op( NULL, NULL, FALSE, (void **)op );
}
static HRESULT WINAPI client_GetStringAsync( IHttpClient *This, IUriRuntimeClass *uri, STR_OP **op )
{
    struct http_client *impl = impl_from_client( This );
    void *resp = NULL;
    HSTRING huri = NULL, str = NULL;
    const WCHAR *uri_str = NULL;
    HRESULT hr;

    WDBG( "client_GetStringAsync uri %p\n", uri );
    if (uri && SUCCEEDED( IUriRuntimeClass_get_AbsoluteUri( uri, &huri ) ) && huri)
        uri_str = WindowsGetStringRawBuffer( huri, NULL );
    if (uri_str && uri_str[0])
        hr = http_fetch_to_response( L"GET", uri_str, NULL, 0, impl->default_headers, NULL, &resp );
    else
        hr = E_INVALIDARG;
    if (huri) WindowsDeleteString( huri );
    if (SUCCEEDED(hr) && resp)
    {
        struct http_response_message *rm = (struct http_response_message *)resp;
        if (rm->content)
        {
            struct http_string_content *sc = impl_from_content( rm->content );
            if (sc && sc->content)
                WindowsDuplicateString( sc->content, &str );
        }
        IUnknown_Release( (IUnknown *)resp );
    }
    if (!str)
    {
        hr = WindowsCreateString( L"", 0, &str );
        if (FAILED(hr)) return hr;
    }
    hr = create_async_http_op( NULL, str, TRUE, (void **)op );
    WindowsDeleteString( str );
    return hr;
}
static HRESULT WINAPI client_PostAsync( IHttpClient *This, IUriRuntimeClass *uri, IHttpContent *content, RESP_OP **op )
{
    void *resp = NULL;
    HRESULT hr = create_http_response( 200, &resp );
    if (FAILED(hr)) return hr;
    hr = create_async_http_op( resp, NULL, FALSE, (void **)op );
    IUnknown_Release( (IUnknown *)resp );
    return hr;
}
static HRESULT WINAPI client_PutAsync( IHttpClient *This, IUriRuntimeClass *uri, IHttpContent *content, RESP_OP **op )
{
    return client_PostAsync( This, uri, content, op );
}

/* Perform the HTTP exchange for a request message via winhttp. */
static HRESULT http_fetch_to_response( const WCHAR *method, const WCHAR *uri,
                                       const void *body_data, DWORD body_len,
                                       void *req_headers, void *content_headers, void **out )
{
    static const WCHAR ua[] = L"WinGet/1.29";
    URL_COMPONENTS uc = { sizeof(uc) };
    WCHAR host[256];
    DWORD uri_len = uri ? lstrlenW( uri ) : 0;
    DWORD path_cap = uri_len + 256;
    DWORD extra_cap = uri_len + 256;
    WCHAR *path = NULL, *extra = NULL;
    WCHAR *hdr = NULL;
    char *body = NULL;
    WCHAR *bodyW = NULL;
    HSTRING bodyH = NULL;
    HINTERNET session = NULL, conn = NULL, req = NULL;
    DWORD status = 200, avail = 0, got = 0, total = 0, size = 0;
    void *resp = NULL, *content = NULL;
    HRESULT hr = E_FAIL;

    WDBG( "http_fetch %s %s\n", debugstr_w( method ), debugstr_w( uri ) );

    if (!(path = malloc( path_cap * sizeof(WCHAR) )) ||
        !(extra = malloc( extra_cap * sizeof(WCHAR) )))
    {
        free( path );
        free( extra );
        return E_OUTOFMEMORY;
    }

    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = path_cap;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = extra_cap;

    /* the winhttp builtin's TLS connect is intermittently flaky (12029 cannot-connect),
     * retry the whole exchange a few times before giving up */
    for (unsigned attempt = 0; attempt < 3; attempt++)
    {
    uc.dwHostNameLength = ARRAYSIZE(host);
    uc.dwUrlPathLength = path_cap;
    uc.dwExtraInfoLength = extra_cap;
    if (session) { WinHttpCloseHandle( session ); session = NULL; }
    if (!(session = WinHttpOpen( ua, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0 )))
        goto done;
    if (!WinHttpCrackUrl( uri, 0, 0, &uc ))
        goto done;
    if (!(conn = WinHttpConnect( session, host, uc.nPort, 0 )))
        goto retry;
    if (extra[0]) lstrcatW( path, extra );
    if (!(req = WinHttpOpenRequest( conn, method, path, NULL, WINHTTP_NO_REQUEST_DATA, 0,
                                    uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 )))
        goto retry;
    if (req_headers)
    {
        UINT32 npairs = 0;
        const struct hdr_pair *pairs = http_headers_pairs( req_headers, &npairs );

        for (UINT32 i = 0; i < npairs; i++)
        {
            const WCHAR *k = WindowsGetStringRawBuffer( pairs[i].k, NULL );
            const WCHAR *v = WindowsGetStringRawBuffer( pairs[i].v, NULL );
            SIZE_T n = lstrlenW( k ) + lstrlenW( v ) + 4;
            if ((hdr = malloc( n * sizeof(WCHAR) )))
            {
                lstrcpyW( hdr, k );
                lstrcatW( hdr, L": " );
                lstrcatW( hdr, v );
                WinHttpAddRequestHeaders( req, hdr, -1, WINHTTP_ADDREQ_FLAG_ADD );
                free( hdr );
                hdr = NULL;
            }
        }
    }
    if (content_headers)
    {
        UINT32 npairs = 0;
        const struct hdr_pair *pairs = http_headers_pairs( content_headers, &npairs );

        for (UINT32 i = 0; i < npairs; i++)
        {
            const WCHAR *k = WindowsGetStringRawBuffer( pairs[i].k, NULL );
            const WCHAR *v = WindowsGetStringRawBuffer( pairs[i].v, NULL );
            SIZE_T n = lstrlenW( k ) + lstrlenW( v ) + 4;
            if ((hdr = malloc( n * sizeof(WCHAR) )))
            {
                lstrcpyW( hdr, k );
                lstrcatW( hdr, L": " );
                lstrcatW( hdr, v );
                WinHttpAddRequestHeaders( req, hdr, -1, WINHTTP_ADDREQ_FLAG_ADD );
                free( hdr );
                hdr = NULL;
            }
        }
    }
    if (!WinHttpSendRequest( req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             body_data ? (void *)body_data : WINHTTP_NO_REQUEST_DATA, body_len, body_len, 0 ))
    {
        WDBG( "send_request failed err %u attempt %u\n", (unsigned)GetLastError(), (unsigned)attempt );
        goto retry;
    }
    if (!WinHttpReceiveResponse( req, NULL ))
    {
        WDBG( "receive_response failed err %u attempt %u\n", (unsigned)GetLastError(), (unsigned)attempt );
        goto retry;
    }
    WDBG( "receive_response ok, status query\n" );
    break;
retry:
    if (req) { WinHttpCloseHandle( req ); req = NULL; }
    if (conn) { WinHttpCloseHandle( conn ); conn = NULL; }
    }
    if (!req) goto done;
    size = sizeof(status);
    WinHttpQueryHeaders( req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX );
    size = 65536;
    if (!(body = malloc( size ))) goto done;
    for (;;)
    {
        if (!WinHttpQueryDataAvailable( req, &avail )) break;
        if (!avail) avail = 65536;
        if (total + avail > size)
        {
            char *nb = realloc( body, size += avail + 65536 );
            if (!nb) goto done;
            body = nb;
        }
        if (!WinHttpReadData( req, body + total, avail, &got )) { WDBG( "read fail avail %u\n", (unsigned)avail ); break; }
        WDBG( "read loop avail %u got %u total %u\n", (unsigned)avail, (unsigned)got, (unsigned)total );
        if (!got) break;
        total += got;
    }
    size = MultiByteToWideChar( CP_UTF8, 0, body, total, NULL, 0 );
    if (!(bodyW = malloc( (size + 1) * sizeof(WCHAR) ))) goto done;
    MultiByteToWideChar( CP_UTF8, 0, body, total, bodyW, size );
    bodyW[size] = 0;
    hr = WindowsCreateString( bodyW, size, &bodyH );
    if (FAILED( hr )) goto done;
    hr = create_http_response( status, &resp );
    if (FAILED( hr )) goto done;
    {
        void *hdrs = NULL;
        DWORD idx = 0, rawsize = 1024;
        WCHAR raw[512];
        UINT32 nhdr = 0;
        if (SUCCEEDED( resp_get_Headers( resp, &hdrs ) ) && hdrs)
        {
            while (nhdr < 64 && WinHttpQueryHeaders( req, WINHTTP_QUERY_RAW_HEADERS,
                        WINHTTP_HEADER_NAME_BY_INDEX, raw, &rawsize, &idx ))
            {
                WCHAR *colon, *vs;
                HSTRING k = NULL, v = NULL;
                nhdr++;
                colon = raw;
                while (*colon && *colon != L':') colon++;
                if (*colon == L':')
                {
                    vs = colon + 1;
                    *colon = 0;
                    while (*vs == L' ' || *vs == L'\t') vs++;
                    if (SUCCEEDED( WindowsCreateString( raw, lstrlenW( raw ), &k ) ))
                    {
                        if (SUCCEEDED( WindowsCreateString( vs, lstrlenW( vs ), &v ) ))
                            headers_Append( hdrs, k, v );
                        WindowsDeleteString( k );
                    }
                    WindowsDeleteString( v );
                }
                idx++;
                rawsize = sizeof(raw);
            }
            IUnknown_Release( (IUnknown *)hdrs );
        }
    }
    hr = create_http_string_content( bodyH, &content );
    if (SUCCEEDED( hr ))
    {
        if (body && total)
        {
            struct http_string_content *sc = impl_from_content( content );
            sc->raw_bytes = (BYTE *)body;
            sc->raw_len = total;
            body = NULL;
        }
        {
            WCHAR raw[4096];
            DWORD rawsize = sizeof(raw);
            if (WinHttpQueryHeaders( req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                     raw, &rawsize, WINHTTP_NO_HEADER_INDEX ))
            {
                void *rhdrs = NULL, *chdrs = NULL;
                WCHAR *line = raw;
                resp_get_Headers( resp, &rhdrs );
                content_get_Headers( content, &chdrs );
                while (line && *line)
                {
                    WCHAR *eol = wcsstr( line, L"\r\n" );
                    WCHAR *colon;
                    if (eol) *eol = 0;
                    colon = wcschr( line, L':' );
                    if (colon)
                    {
                        HSTRING k = NULL, v = NULL;
                        WCHAR *vs = colon + 1;
                        *colon = 0;
                        while (*vs == L' ' || *vs == L'\t') vs++;
                        if (SUCCEEDED( WindowsCreateString( line, lstrlenW( line ), &k ) ) &&
                            SUCCEEDED( WindowsCreateString( vs, lstrlenW( vs ), &v ) ))
                        {
                            if (!_wcsnicmp( line, L"Content-", 8 ))
                            {
                                if (chdrs) headers_Append( chdrs, k, v );
                            }
                            else
                            {
                                if (rhdrs) headers_Append( rhdrs, k, v );
                            }
                        }
                        if (k) WindowsDeleteString( k );
                        if (v) WindowsDeleteString( v );
                    }
                    if (!eol) break;
                    line = eol + 2;
                }
                if (rhdrs) IUnknown_Release( (IUnknown *)rhdrs );
                if (chdrs) IUnknown_Release( (IUnknown *)chdrs );
            }
        }
        resp_set_Content( resp, content );
        IUnknown_Release( (IUnknown *)content );
    }
done:
    WDBG( "http_fetch -> status %u bytes %u hr %08x\n", (unsigned)status, (unsigned)total, (unsigned)hr );
    if (req) WinHttpCloseHandle( req );
    if (conn) WinHttpCloseHandle( conn );
    if (session) WinHttpCloseHandle( session );
    free( path );
    free( extra );
    free( hdr );
    free( body );
    free( bodyW );
    if (bodyH) WindowsDeleteString( bodyH );
    if (SUCCEEDED( hr )) *out = resp; /* the create reference transfers to the caller */
    return hr;
}

static HRESULT send_request( void *req, void **op )
{
    struct http_request_message *r = impl_from_request( req );
    const WCHAR *method, *uri;
    void *resp = NULL, *content_headers = NULL;
    BYTE *body_data = NULL;
    ULONG body_len = 0;
    HRESULT hr;

    method = r->method ? WindowsGetStringRawBuffer( (HSTRING)r->method, NULL ) : NULL;
    uri = r->uri ? WindowsGetStringRawBuffer( (HSTRING)r->uri, NULL ) : NULL;
    if (!(method && *method)) method = L"GET";
    if (!(uri && *uri)) uri = L"https://cdn.winget.microsoft.com/cache/index.db";

    if (r->content)
    {
        static const GUID IID_IBufferByteAccess_c =
            { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
        HSTRING cls = NULL;
        const WCHAR *cls_w = L"";
        BOOL is_stream_c = FALSE;

        if (SUCCEEDED( IInspectable_GetRuntimeClassName( (IInspectable *)r->content, &cls ) ) && cls)
        {
            cls_w = WindowsGetStringRawBuffer( cls, NULL );
            if (cls_w && !wcscmp( cls_w, L"Windows.Web.Http.HttpStreamContent" ))
                is_stream_c = TRUE;
        }

        if (is_stream_c)
        {
            /* HttpStreamContent hosts ReadAsStringAsync at slot 9 and get_Headers at slot 13. */
            HRESULT (WINAPI *read_str)( void *, void ** ) =
                (void *)((void *const *)(*(void *const **)r->content))[9];
            HRESULT (WINAPI *get_hdrs)( void *, void ** ) =
                (void *)((void *const *)(*(void *const **)r->content))[13];
            void *c_op = NULL;
            HSTRING str = NULL;

            if (SUCCEEDED( read_str( r->content, &c_op ) ) && c_op)
            {
                HRESULT (WINAPI *op_res)( void *, HSTRING * ) =
                    (void *)((void *const *)(*(void *const **)c_op))[10];
                op_res( c_op, &str );
                IUnknown_Release( (IUnknown *)c_op );
            }
            if (str)
            {
                UINT wlen = WindowsGetStringLen( str );
                const WCHAR *w = WindowsGetStringRawBuffer( str, NULL );
                int n = WideCharToMultiByte( CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL );
                if (n > 0 && (body_data = HeapAlloc( GetProcessHeap(), 0, n )))
                {
                    WideCharToMultiByte( CP_UTF8, 0, w, wlen, (char *)body_data, n, NULL, NULL );
                    body_len = n;
                }
                WindowsDeleteString( str );
            }
            get_hdrs( r->content, &content_headers );
        }
        else
        {
            /* Standard WinRT IHttpContent: slot 6 get_Headers, slot 8 ReadAsBufferAsync, slot 10 ReadAsStringAsync. */
            HRESULT (WINAPI *get_hdrs)( void *, void ** ) =
                (void *)((void *const *)(*(void *const **)r->content))[6];
            HRESULT (WINAPI *read_buf)( void *, void ** ) =
                (void *)((void *const *)(*(void *const **)r->content))[8];
            HRESULT (WINAPI *read_str)( void *, void ** ) =
                (void *)((void *const *)(*(void *const **)r->content))[10];
            void *c_op = NULL, *ibuf = NULL, *bba = NULL;

            get_hdrs( r->content, &content_headers );
            if (SUCCEEDED( read_buf( r->content, &c_op ) ) && c_op)
            {
                HRESULT (WINAPI *op_res)( void *, void ** ) =
                    (void *)((void *const *)(*(void *const **)c_op))[10];
                if (SUCCEEDED( op_res( c_op, &ibuf ) ) && ibuf)
                {
                    HRESULT (WINAPI *get_len)( void *, ULONG * ) =
                        (void *)((void *const *)(*(void *const **)ibuf))[7];
                    ULONG blen = 0;
                    BYTE *bptr = NULL;
                    if (SUCCEEDED( get_len( ibuf, &blen ) ) && blen > 0 &&
                        SUCCEEDED( IUnknown_QueryInterface( (IUnknown *)ibuf, &IID_IBufferByteAccess_c, &bba ) ) && bba)
                    {
                        HRESULT (WINAPI *buf_ptr)( void *, BYTE ** ) =
                            (void *)((void *const *)(*(void *const **)bba))[3];
                        if (SUCCEEDED( buf_ptr( bba, &bptr ) ) && bptr &&
                            (body_data = HeapAlloc( GetProcessHeap(), 0, blen )))
                        {
                            memcpy( body_data, bptr, blen );
                            body_len = blen;
                        }
                        IUnknown_Release( (IUnknown *)bba );
                    }
                    IUnknown_Release( (IUnknown *)ibuf );
                }
                IUnknown_Release( (IUnknown *)c_op );
            }
            if (!body_data)
            {
                HSTRING str = NULL;
                c_op = NULL;
                if (SUCCEEDED( read_str( r->content, &c_op ) ) && c_op)
                {
                    HRESULT (WINAPI *op_res)( void *, HSTRING * ) =
                        (void *)((void *const *)(*(void *const **)c_op))[10];
                    op_res( c_op, &str );
                    IUnknown_Release( (IUnknown *)c_op );
                }
                if (str)
                {
                    UINT wlen = WindowsGetStringLen( str );
                    const WCHAR *w = WindowsGetStringRawBuffer( str, NULL );
                    int n = WideCharToMultiByte( CP_UTF8, 0, w, wlen, NULL, 0, NULL, NULL );
                    if (n > 0 && (body_data = HeapAlloc( GetProcessHeap(), 0, n )))
                    {
                        WideCharToMultiByte( CP_UTF8, 0, w, wlen, (char *)body_data, n, NULL, NULL );
                        body_len = n;
                    }
                    WindowsDeleteString( str );
                }
            }
        }
        if (cls) WindowsDeleteString( cls );
        WDBG( "send_request content pump len %u\n", (unsigned)body_len );
    }
    if (FAILED( (hr = http_fetch_to_response( method, uri, body_data, body_len, r->headers, content_headers, &resp ) ) ))
    {
        if (FAILED( (hr = create_http_response( 200, &resp ) ) ))
        {
            if (content_headers) IUnknown_Release( (IUnknown *)content_headers );
            if (body_data) HeapFree( GetProcessHeap(), 0, body_data );
            return hr;
        }
    }
    if (content_headers) IUnknown_Release( (IUnknown *)content_headers );
    if (body_data) HeapFree( GetProcessHeap(), 0, body_data );
    return create_async_http_op( resp, NULL, FALSE, (void **)op );
}

static HRESULT WINAPI client_SendRequestAsync( IHttpClient *This, HttpRequestMessage *req, RESP_OP **op )
{
    WDBG( "client_SendRequestAsync req %p\n", req );
    return send_request( req, (void **)op );
}
static HRESULT WINAPI client_SendRequestAsync2( IHttpClient *This, HttpRequestMessage *req, HttpCompletionOption opt, RESP_OP **op )
{
    return client_SendRequestAsync( This, req, op );
}
static HRESULT WINAPI client_get_DefaultRequestHeaders( IHttpClient *This, HttpRequestHeaderCollection **headers )
{
    struct http_client *impl = impl_from_client( This );
    if (!headers) return E_POINTER;
    if (!impl->default_headers) create_http_headers( &IID_IHttpRequestHeaderCollection, &impl->default_headers );
    IUnknown_AddRef( (IUnknown *)impl->default_headers );
    *headers = impl->default_headers;
    return S_OK;
}

static const IHttpClientVtbl http_client_vtbl = {
    client_QueryInterface,
    client_AddRef,
    client_Release,
    client_GetIids,
    client_GetRuntimeClassName,
    client_GetTrustLevel,
    client_DeleteAsync,
    client_GetAsync,
    client_GetAsync2,
    client_GetBufferAsync,
    client_GetInputStreamAsync,
    client_GetStringAsync,
    client_PostAsync,
    client_PutAsync,
    client_SendRequestAsync,
    client_SendRequestAsync2,
    client_get_DefaultRequestHeaders
};

static HRESULT WINAPI client2_QueryInterface( IHttpClient2 *This, REFIID iid, void **out )
{
    return client_QueryInterface( (IHttpClient *)&impl_from_client2( This )->vtbl_client, iid, out );
}
static ULONG WINAPI client2_AddRef( IHttpClient2 *This )
{
    return client_AddRef( (IHttpClient *)&impl_from_client2( This )->vtbl_client );
}
static ULONG WINAPI client2_Release( IHttpClient2 *This )
{
    return client_Release( (IHttpClient *)&impl_from_client2( This )->vtbl_client );
}
static HRESULT WINAPI client2_GetIids( IHttpClient2 *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI client2_GetRuntimeClassName( IHttpClient2 *This, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI client2_GetTrustLevel( IHttpClient2 *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI client2_TryDeleteAsync( IHttpClient2 *This, IUriRuntimeClass *uri, RESP_AO **op ) { return client_DeleteAsync( (IHttpClient *)This, uri, (RESP_OP **)op ); }
static HRESULT WINAPI client2_TryGetAsync( IHttpClient2 *This, IUriRuntimeClass *uri, RESP_AO **op ) { return client_GetAsync( (IHttpClient *)This, uri, (RESP_OP **)op ); }
static HRESULT WINAPI client2_TryGetAsync2( IHttpClient2 *This, IUriRuntimeClass *uri, HttpCompletionOption opt, RESP_AO **op ) { return client_GetAsync2( (IHttpClient *)This, uri, opt, (RESP_OP **)op ); }
static HRESULT WINAPI client2_TryGetBufferAsync( IHttpClient2 *This, IUriRuntimeClass *uri, BUF_AO **op ) { return client_GetBufferAsync( (IHttpClient *)This, uri, (BUF_OP **)op ); }
static HRESULT WINAPI client2_TryGetInputStreamAsync( IHttpClient2 *This, IUriRuntimeClass *uri, ISTRM_AO **op ) { return client_GetInputStreamAsync( (IHttpClient *)This, uri, (ISTRM_OP **)op ); }
static HRESULT WINAPI client2_TryGetStringAsync( IHttpClient2 *This, IUriRuntimeClass *uri, STR_AO **op ) { return client_GetStringAsync( (IHttpClient *)This, uri, (STR_OP **)op ); }
static HRESULT WINAPI client2_TryPostAsync( IHttpClient2 *This, IUriRuntimeClass *uri, IHttpContent *content, RESP_AO **op ) { return client_PostAsync( (IHttpClient *)This, uri, content, (RESP_OP **)op ); }
static HRESULT WINAPI client2_TryPutAsync( IHttpClient2 *This, IUriRuntimeClass *uri, IHttpContent *content, RESP_AO **op ) { return client_PutAsync( (IHttpClient *)This, uri, content, (RESP_OP **)op ); }
static HRESULT WINAPI client2_TrySendRequestAsync( IHttpClient2 *This, HttpRequestMessage *req, RESP_AO **op ) { return client_SendRequestAsync( (IHttpClient *)This, req, (RESP_OP **)op ); }
static HRESULT WINAPI client2_TrySendRequestAsync2( IHttpClient2 *This, HttpRequestMessage *req, HttpCompletionOption opt, RESP_AO **op ) { return client_SendRequestAsync2( (IHttpClient *)This, req, opt, (RESP_OP **)op ); }

static const IHttpClient2Vtbl http_client2_vtbl = {
    client2_QueryInterface,
    client2_AddRef,
    client2_Release,
    client2_GetIids,
    client2_GetRuntimeClassName,
    client2_GetTrustLevel,
    client2_TryDeleteAsync,
    client2_TryGetAsync,
    client2_TryGetAsync2,
    client2_TryGetBufferAsync,
    client2_TryGetInputStreamAsync,
    client2_TryGetStringAsync,
    client2_TryPostAsync,
    client2_TryPutAsync,
    client2_TrySendRequestAsync,
    client2_TrySendRequestAsync2
};

static HRESULT WINAPI client_closable_QueryInterface( IClosable *This, REFIID iid, void **out )
{
    return client_QueryInterface( (IHttpClient *)&impl_from_client_closable( This )->vtbl_client, iid, out );
}
static ULONG WINAPI client_closable_AddRef( IClosable *This )
{
    return client_AddRef( (IHttpClient *)&impl_from_client_closable( This )->vtbl_client );
}
static ULONG WINAPI client_closable_Release( IClosable *This )
{
    return client_Release( (IHttpClient *)&impl_from_client_closable( This )->vtbl_client );
}
static HRESULT WINAPI client_closable_GetIids( IClosable *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI client_closable_GetRuntimeClassName( IClosable *This, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI client_closable_GetTrustLevel( IClosable *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI client_closable_Close( IClosable *This ) { return S_OK; }

static const IClosableVtbl http_client_closable_vtbl = {
    client_closable_QueryInterface,
    client_closable_AddRef,
    client_closable_Release,
    client_closable_GetIids,
    client_closable_GetRuntimeClassName,
    client_closable_GetTrustLevel,
    client_closable_Close
};

HRESULT create_http_client( void **out )
{
    struct http_client *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_client = &http_client_vtbl;
    impl->vtbl_client2 = &http_client2_vtbl;
    impl->vtbl_closable = &http_client_closable_vtbl;
    impl->ref = 1;
    *out = &impl->vtbl_client;
    return S_OK;
}

/* --- Activation Factories --- */

/* HttpClient factory */

static inline struct http_client_factory_impl *impl_from_client_act_factory( void *iface )
{
    return (struct http_client_factory_impl *)iface;
}
static inline struct http_client_factory_impl *impl_from_client_factory( void *iface )
{
    return (struct http_client_factory_impl *)((char *)iface - sizeof(void *));
}

static HRESULT WINAPI http_client_factory_QueryInterface( IActivationFactory *This, REFIID iid, void **out )
{
    struct http_client_factory_impl *impl = impl_from_client_act_factory( This );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->vtbl_activation;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpClientFactory ))
    {
        *out = &impl->vtbl_factory;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI http_client_factory_iface_QueryInterface( IHttpClientFactory *This, REFIID iid, void **out )
{
    struct http_client_factory_impl *impl = impl_from_client_factory( This );
    return http_client_factory_QueryInterface( (IActivationFactory *)&impl->vtbl_activation, iid, out );
}
static ULONG WINAPI http_client_factory_AddRef( IActivationFactory *This ) { return 2; }
static ULONG WINAPI http_client_factory_Release( IActivationFactory *This ) { return 1; }
static HRESULT WINAPI http_client_factory_GetIids( IActivationFactory *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_client_factory_GetRuntimeClassName( IActivationFactory *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpClient";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_client_factory_GetTrustLevel( IActivationFactory *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_client_factory_ActivateInstance( IActivationFactory *This, IInspectable **instance )
{
    return create_http_client( (void **)instance );
}
static HRESULT WINAPI http_client_factory_Create( IHttpClientFactory *This, IHttpBaseProtocolFilter *filter, IHttpClient **instance )
{
    TRACE( "This %p, filter %p, instance %p\n", This, filter, instance );
    return create_http_client( (void **)instance );
}

static const IActivationFactoryVtbl http_client_activation_vtbl = {
    http_client_factory_QueryInterface,
    http_client_factory_AddRef,
    http_client_factory_Release,
    http_client_factory_GetIids,
    http_client_factory_GetRuntimeClassName,
    http_client_factory_GetTrustLevel,
    http_client_factory_ActivateInstance
};

static const IHttpClientFactoryVtbl http_client_factory_vtbl = {
    http_client_factory_iface_QueryInterface,
    (ULONG (STDMETHODCALLTYPE *)(IHttpClientFactory *))http_client_factory_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IHttpClientFactory *))http_client_factory_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpClientFactory *, ULONG *, IID **))http_client_factory_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpClientFactory *, HSTRING *))http_client_factory_GetRuntimeClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpClientFactory *, TrustLevel *))http_client_factory_GetTrustLevel,
    http_client_factory_Create
};

static const struct http_client_factory_impl http_client_factory_obj =
{
    &http_client_activation_vtbl,
    &http_client_factory_vtbl,
    1,
};
IActivationFactory *http_client_factory = (IActivationFactory *)&http_client_factory_obj;

/* HttpRequestMessage factory */
static const void *http_request_activation_obj;
static const void *http_request_factory_obj;

static HRESULT WINAPI http_request_factory_QueryInterface( IActivationFactory *This, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &http_request_activation_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpRequestMessageFactory ))
    {
        *out = &http_request_factory_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_request_factory_AddRef( IActivationFactory *This ) { return 2; }
static ULONG WINAPI http_request_factory_Release( IActivationFactory *This ) { return 1; }
static HRESULT WINAPI http_request_factory_GetIids( IActivationFactory *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_request_factory_GetRuntimeClassName( IActivationFactory *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpRequestMessage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_request_factory_GetTrustLevel( IActivationFactory *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_request_factory_ActivateInstance( IActivationFactory *This, IInspectable **instance )
{
    return create_http_request( NULL, NULL, (void **)instance );
}
static HRESULT WINAPI http_request_factory_Create( IActivationFactory *This, void *a, void *b, void *c )
{
    /* IHttpRequestMessageFactory::Create(method, uri, &out) — slot 6, objects passed through */
    WDBG( "request_factory_Create a %p b %p c %p\n", a, b, c );
    return create_http_request( a, b, (void **)c );
}

static const IActivationFactoryVtbl http_request_activation_vtbl = {
    http_request_factory_QueryInterface,
    http_request_factory_AddRef,
    http_request_factory_Release,
    http_request_factory_GetIids,
    http_request_factory_GetRuntimeClassName,
    http_request_factory_GetTrustLevel,
    http_request_factory_ActivateInstance,
};
static const struct request_factory_vtbl http_request_factory_vtbl = {
  {
    http_request_factory_QueryInterface,
    http_request_factory_AddRef,
    http_request_factory_Release,
    http_request_factory_GetIids,
    http_request_factory_GetRuntimeClassName,
    http_request_factory_GetTrustLevel,
    (HRESULT (STDMETHODCALLTYPE *)(IActivationFactory *, IInspectable **))http_request_factory_Create
  },
  {
    http_request_factory_ActivateInstance,
    http_request_factory_Create
  }
};
static const void *http_request_activation_obj = (const void *)&http_request_activation_vtbl;
static const void *http_request_factory_obj = (const void *)&http_request_factory_vtbl;
IActivationFactory *http_request_factory = (IActivationFactory *)&http_request_activation_obj;

/* HttpResponseMessage factory */
static HRESULT WINAPI http_response_factory_QueryInterface( IActivationFactory *This, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpResponseMessageFactory ))
    {
        *out = This;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_response_factory_AddRef( IActivationFactory *This ) { return 2; }
static ULONG WINAPI http_response_factory_Release( IActivationFactory *This ) { return 1; }
static HRESULT WINAPI http_response_factory_GetIids( IActivationFactory *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_response_factory_GetRuntimeClassName( IActivationFactory *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpResponseMessage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_response_factory_GetTrustLevel( IActivationFactory *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_response_factory_ActivateInstance( IActivationFactory *This, IInspectable **instance )
{
    return create_http_response( 200, (void **)instance );
}
static HRESULT WINAPI http_response_factory_Create( IActivationFactory *This, void *a, void *b )
{
    /* serves ActivateInstance(out) and Create(UInt32 status_code, &out) */
    UINT_PTR v = (UINT_PTR)a;
    UINT32 status = v < 0x100000 ? (UINT32)v : 200;
    void **out = v < 0x100000 ? (void **)b : (void **)a;
    WDBG( "response_factory_Create a %p b %p -> status %u out %p\n", a, b, status, out );
    return create_http_response( status, out );
}

static const struct response_factory_vtbl http_response_factory_vtbl = {
  {
    http_response_factory_QueryInterface,
    http_response_factory_AddRef,
    http_response_factory_Release,
    http_response_factory_GetIids,
    http_response_factory_GetRuntimeClassName,
    http_response_factory_GetTrustLevel,
    (HRESULT (STDMETHODCALLTYPE *)(IActivationFactory *, IInspectable **))http_response_factory_Create
  },
  {
    http_response_factory_ActivateInstance
  }
};
static const void *http_response_factory_obj = (const void *)&http_response_factory_vtbl;
IActivationFactory *http_response_factory = (IActivationFactory *)&http_response_factory_obj;

/* HttpStringContent factory */
static HRESULT WINAPI http_string_content_factory_QueryInterface( IActivationFactory *This, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpStringContentFactory ))
    {
        *out = This;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_string_content_factory_AddRef( IActivationFactory *This ) { return 2; }
static ULONG WINAPI http_string_content_factory_Release( IActivationFactory *This ) { return 1; }
static HRESULT WINAPI http_string_content_factory_GetIids( IActivationFactory *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_string_content_factory_GetRuntimeClassName( IActivationFactory *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpStringContent";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_string_content_factory_GetTrustLevel( IActivationFactory *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_string_content_factory_ActivateInstance( IActivationFactory *This, IInspectable **instance )
{
    return create_http_string_content( NULL, (void **)instance );
}
static HRESULT WINAPI http_string_content_factory_CreateFromString( IActivationFactory *This, void *a, void *b )
{
    /* serves ActivateInstance(out) and CreateFromString(content, &out) */
    void *content = is_hstring( a ) ? a : NULL;
    void **out = content ? (void **)b : (void **)a;
    WDBG( "string_content_Create a %p b %p -> c %p out %p\n", a, b, content, out );
    return create_http_string_content( content, out );
}
static HRESULT WINAPI http_string_content_factory_CreateFromStringWithEncoding( IActivationFactory *This, HSTRING content, INT32 encoding, void **instance )
{
    return create_http_string_content( content, instance );
}
static HRESULT WINAPI http_string_content_factory_CreateFromStringWithEncodingAndMediaType( IActivationFactory *This, HSTRING content, INT32 encoding, HSTRING media_type, void **instance )
{
    HRESULT hr = create_http_string_content( content, instance );
    if (SUCCEEDED( hr ) && instance && *instance && media_type && WindowsGetStringLen( media_type ))
    {
        void *chdrs = NULL;
        if (SUCCEEDED( content_get_Headers( *instance, &chdrs ) ) && chdrs)
        {
            HSTRING k = NULL;
            if (SUCCEEDED( WindowsCreateString( L"Content-Type", 12, &k ) ))
            {
                headers_Append( chdrs, k, media_type );
                WindowsDeleteString( k );
            }
            IUnknown_Release( (IUnknown *)chdrs );
        }
    }
    return hr;
}

static const struct string_content_factory_vtbl http_string_content_factory_vtbl = {
  {
    http_string_content_factory_QueryInterface,
    http_string_content_factory_AddRef,
    http_string_content_factory_Release,
    http_string_content_factory_GetIids,
    http_string_content_factory_GetRuntimeClassName,
    http_string_content_factory_GetTrustLevel,
    (HRESULT (STDMETHODCALLTYPE *)(IActivationFactory *, IInspectable **))http_string_content_factory_CreateFromString
  },
  {
    http_string_content_factory_CreateFromStringWithEncoding,
    http_string_content_factory_CreateFromStringWithEncodingAndMediaType,
    http_string_content_factory_ActivateInstance
  }
};
static const void *http_string_content_factory_obj = (const void *)&http_string_content_factory_vtbl;
IActivationFactory *http_string_content_factory = (IActivationFactory *)&http_string_content_factory_obj;

/* HttpBufferContent factory (IHttpBufferContentFactory {bc20c193-c41f-4ff7-9123-6435736eadc2}) */
HRESULT create_http_buffer_content( void *ibuf, UINT32 offset, UINT32 count, BOOL has_range, void **out );
static const GUID IID_IHttpBufferContentFactory =
    { 0xbc20c193, 0xc41f, 0x4ff7, { 0x91, 0x23, 0x64, 0x35, 0x73, 0x6e, 0xad, 0xc2 } };
static const void *http_buffer_content_activation_obj;
static const void *http_buffer_content_factory_obj;

static HRESULT WINAPI http_buffer_content_factory_QueryInterface( IActivationFactory *This, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &http_buffer_content_activation_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpBufferContentFactory ))
    {
        *out = &http_buffer_content_factory_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_buffer_content_factory_AddRef( IActivationFactory *This ) { return 2; }
static ULONG WINAPI http_buffer_content_factory_Release( IActivationFactory *This ) { return 1; }
static HRESULT WINAPI http_buffer_content_factory_GetIids( IActivationFactory *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_buffer_content_factory_GetRuntimeClassName( IActivationFactory *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpBufferContent";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_buffer_content_factory_GetTrustLevel( IActivationFactory *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_buffer_content_factory_ActivateInstance( IActivationFactory *This, IInspectable **instance )
{
    return create_http_buffer_content( NULL, 0, 0, FALSE, (void **)instance );
}
static HRESULT WINAPI http_buffer_content_factory_CreateFromBuffer( void *This, void *content, void **value )
{
    return create_http_buffer_content( content, 0, 0, FALSE, value );
}
static HRESULT WINAPI http_buffer_content_factory_CreateFromBufferWithOffset( void *This, void *content, UINT32 offset, UINT32 count, void **value )
{
    return create_http_buffer_content( content, offset, count, TRUE, value );
}
static const void *http_buffer_content_activation_vtbl[7] =
{
    http_buffer_content_factory_QueryInterface,
    http_buffer_content_factory_AddRef,
    http_buffer_content_factory_Release,
    http_buffer_content_factory_GetIids,
    http_buffer_content_factory_GetRuntimeClassName,
    http_buffer_content_factory_GetTrustLevel,
    http_buffer_content_factory_ActivateInstance,
};
static const void *http_buffer_content_factory_vtbl[8] =
{
    http_buffer_content_factory_QueryInterface,
    http_buffer_content_factory_AddRef,
    http_buffer_content_factory_Release,
    http_buffer_content_factory_GetIids,
    http_buffer_content_factory_GetRuntimeClassName,
    http_buffer_content_factory_GetTrustLevel,
    http_buffer_content_factory_CreateFromBuffer,
    http_buffer_content_factory_CreateFromBufferWithOffset,
};
static const void *http_buffer_content_activation_obj = (const void *)http_buffer_content_activation_vtbl;
static const void *http_buffer_content_factory_obj = (const void *)http_buffer_content_factory_vtbl;
IActivationFactory *http_buffer_content_factory = (IActivationFactory *)&http_buffer_content_activation_obj;

/* HttpMethod factory */
static const void *http_method_activation_obj;
static const void *http_method_factory_obj;
static const void *http_method_statics_obj;

static HRESULT WINAPI http_method_factory_QueryInterface( IActivationFactory *This, REFIID iid, void **out )
{
    if (iid) WDBG( "method_factory_QI {%08x-%04x-%04x-%02x%02x}\n", (unsigned)iid->Data1, iid->Data2, iid->Data3, iid->Data4[0], iid->Data4[1] );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &http_method_activation_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpMethodFactory ))
    {
        *out = &http_method_factory_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpMethodStatics ))
    {
        *out = &http_method_statics_obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_method_factory_AddRef( IActivationFactory *This ) { return 2; }
static ULONG WINAPI http_method_factory_Release( IActivationFactory *This ) { return 1; }
static HRESULT WINAPI http_method_factory_GetIids( IActivationFactory *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_method_factory_GetRuntimeClassName( IActivationFactory *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpMethod";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_method_factory_GetTrustLevel( IActivationFactory *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_method_factory_ActivateInstance( IActivationFactory *This, IInspectable **instance )
{
    WDBG( "method_factory_ActivateInstance This %p\n", This );
    return create_http_method( NULL, (void **)instance );
}
static HRESULT WINAPI http_method_factory_Create( IHttpMethodFactory *This, HSTRING method, IHttpMethod **instance )
{
    /* IHttpMethodFactory::Create(HSTRING method, &out) */
    WDBG( "method_factory_Create method %p out %p\n", method, instance );
    {
        UINT32 len = 0; const WCHAR *raw;
        raw = WindowsGetStringRawBuffer( method, &len );
        WDBG( "decoded len=%u raw=%s\n", len, debugstr_wn( raw, len ) );
    }
    return create_http_method( method, (void **)instance );
}

static HRESULT WINAPI http_method_factory_get_named( const WCHAR *name, void **val )
{
    HSTRING str;
    HRESULT hr;
    WDBG( "method_factory_get_named %s\n", debugstr_w( name ) );
    hr = WindowsCreateString( name, wcslen( name ), &str );
    if (FAILED(hr)) return hr;
    hr = create_http_method( str, (void **)val );
    WindowsDeleteString( str );
    return hr;
}

static HRESULT WINAPI http_method_factory_get_Delete( IHttpMethodStatics *This, IHttpMethod **val ) { return http_method_factory_get_named( L"DELETE", (void **)val ); }
static HRESULT WINAPI http_method_factory_get_Get( IHttpMethodStatics *This, IHttpMethod **val ) { return http_method_factory_get_named( L"GET", (void **)val ); }
static HRESULT WINAPI http_method_factory_get_Head( IHttpMethodStatics *This, IHttpMethod **val ) { return http_method_factory_get_named( L"HEAD", (void **)val ); }
static HRESULT WINAPI http_method_factory_get_Options( IHttpMethodStatics *This, IHttpMethod **val ) { return http_method_factory_get_named( L"OPTIONS", (void **)val ); }
static HRESULT WINAPI http_method_factory_get_Patch( IHttpMethodStatics *This, IHttpMethod **val ) { return http_method_factory_get_named( L"PATCH", (void **)val ); }
static HRESULT WINAPI http_method_factory_get_Post( IHttpMethodStatics *This, IHttpMethod **val ) { return http_method_factory_get_named( L"POST", (void **)val ); }
static HRESULT WINAPI http_method_factory_get_Put( IHttpMethodStatics *This, IHttpMethod **val ) { return http_method_factory_get_named( L"PUT", (void **)val ); }

static const IActivationFactoryVtbl http_method_activation_vtbl = {
    http_method_factory_QueryInterface,
    http_method_factory_AddRef,
    http_method_factory_Release,
    http_method_factory_GetIids,
    http_method_factory_GetRuntimeClassName,
    http_method_factory_GetTrustLevel,
    http_method_factory_ActivateInstance
};
static const IHttpMethodFactoryVtbl http_method_factory_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodFactory *, REFIID, void **))http_method_factory_QueryInterface,
    (ULONG (STDMETHODCALLTYPE *)(IHttpMethodFactory *))http_method_factory_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IHttpMethodFactory *))http_method_factory_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodFactory *, ULONG *, IID **))http_method_factory_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodFactory *, HSTRING *))http_method_factory_GetRuntimeClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodFactory *, TrustLevel *))http_method_factory_GetTrustLevel,
    http_method_factory_Create
};
static const IHttpMethodStaticsVtbl http_method_statics_vtbl = {
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodStatics *, REFIID, void **))http_method_factory_QueryInterface,
    (ULONG (STDMETHODCALLTYPE *)(IHttpMethodStatics *))http_method_factory_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IHttpMethodStatics *))http_method_factory_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodStatics *, ULONG *, IID **))http_method_factory_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodStatics *, HSTRING *))http_method_factory_GetRuntimeClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpMethodStatics *, TrustLevel *))http_method_factory_GetTrustLevel,
    http_method_factory_get_Delete,
    http_method_factory_get_Get,
    http_method_factory_get_Head,
    http_method_factory_get_Options,
    http_method_factory_get_Patch,
    http_method_factory_get_Post,
    http_method_factory_get_Put
};
static const void *http_method_activation_obj = (const void *)&http_method_activation_vtbl;
static const void *http_method_factory_obj = (const void *)&http_method_factory_vtbl;
static const void *http_method_statics_obj = (const void *)&http_method_statics_vtbl;
IActivationFactory *http_method_factory = (IActivationFactory *)&http_method_activation_obj;

/* HttpBaseProtocolFilter implementation */


static inline struct http_base_protocol_filter *impl_from_filter_base( void *This )
{
    return (struct http_base_protocol_filter *)This;
}
static HRESULT WINAPI base_filter_QueryInterface( IHttpBaseProtocolFilter *This, REFIID iid, void **out )
{
    struct http_base_protocol_filter *impl = impl_from_filter_base( This );
    TRACE( "This %p, iid %s, out %p\n", This, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpBaseProtocolFilter ) ||
        IsEqualGUID( iid, &IID_IHttpBaseProtocolFilter2 ) ||
        IsEqualGUID( iid, &IID_IHttpBaseProtocolFilter3 ) ||
        IsEqualGUID( iid, &IID_IHttpBaseProtocolFilter4 ))
    {
        *out = &impl->vtbl_base;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpFilter ))
    {
        *out = &impl->vtbl_filter;
        InterlockedIncrement( &impl->ref );
        TRACE( "QI IHttpFilter returning *out = %p\n", *out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        TRACE( "QI IClosable returning *out = %p\n", *out );
        return S_OK;
    }
    FIXME( "unimplemented iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI base_filter_AddRef( IHttpBaseProtocolFilter *This )
{
    struct http_base_protocol_filter *impl = impl_from_filter_base( This );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI base_filter_Release( IHttpBaseProtocolFilter *This )
{
    struct http_base_protocol_filter *impl = impl_from_filter_base( This );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI base_filter_GetIids( IHttpBaseProtocolFilter *This, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI base_filter_GetRuntimeClassName( IHttpBaseProtocolFilter *This, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.Filters.HttpBaseProtocolFilter";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI base_filter_GetTrustLevel( IHttpBaseProtocolFilter *This, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

/* IVector`1<UINT32> is not instantiated by the generated headers. */
static const GUID IID_FIVector_UINT32 =
    { 0xf7bb005b, 0xc0c7, 0x49c6, { 0x87, 0xe5, 0x4d, 0x5d, 0x5a, 0x47, 0xa9, 0xf1 } };

static HRESULT WINAPI filter_stub_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICacheControl ) ||
        IsEqualGUID( iid, &IID_ICookieManager ) ||
        IsEqualGUID( iid, &IID_FIVector_UINT32 ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI filter_stub_AddRef( IUnknown *iface ) { return 2; }
static ULONG WINAPI filter_stub_Release( IUnknown *iface ) { return 1; }
static HRESULT WINAPI filter_stub_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI filter_stub_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.PropertySet";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI filter_stub_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI cache_control_get_behavior( void *iface, INT32 *val ) { if (val) *val = 0; return S_OK; }
static HRESULT WINAPI cache_control_put_behavior( void *iface, INT32 val ) { return S_OK; }
static const struct cache_control_vtbl http_cache_control_vtbl = {
  {
    filter_stub_QueryInterface,
    filter_stub_AddRef,
    filter_stub_Release
  },
  {
    {
    filter_stub_GetIids,
    filter_stub_GetRuntimeClassName,
    filter_stub_GetTrustLevel
    },
    {
    cache_control_get_behavior,
    cache_control_put_behavior,
    cache_control_get_behavior,
    cache_control_put_behavior
    }
  }
};

static const void *http_cache_control_obj = (const void *)&http_cache_control_vtbl;

static HRESULT WINAPI cert_errors_GetAt( void *iface, UINT32 index, UINT32 *item )
{
    if (item) *item = 0;
    return 0x8000000b /* E_BOUNDS */;
}
static HRESULT WINAPI cert_errors_get_Size( void *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = 0;
    return S_OK;
}
static HRESULT WINAPI cert_errors_GetView( void *iface, void **view )
{
    if (!view) return E_POINTER;
    *view = iface;
    return S_OK;
}
static HRESULT WINAPI cert_errors_IndexOf( void *iface, UINT32 value, UINT32 *index, boolean *found )
{
    if (index) *index = 0;
    if (found) *found = FALSE;
    return S_OK;
}
static HRESULT WINAPI cert_errors_SetAt( void *iface, UINT32 index, UINT32 value ) { return S_OK; }
static HRESULT WINAPI cert_errors_InsertAt( void *iface, UINT32 index, UINT32 value ) { return S_OK; }
static HRESULT WINAPI cert_errors_RemoveAt( void *iface, UINT32 index ) { return S_OK; }
static HRESULT WINAPI cert_errors_Append( void *iface, UINT32 value ) { return S_OK; }
static HRESULT WINAPI cert_errors_RemoveAtEnd( void *iface ) { return S_OK; }
static HRESULT WINAPI cert_errors_Clear( void *iface ) { return S_OK; }
static HRESULT WINAPI cert_errors_GetMany( void *iface, UINT32 start, UINT32 cap, UINT32 *items, UINT32 *actual )
{
    if (actual) *actual = 0;
    return S_OK;
}
static HRESULT WINAPI cert_errors_ReplaceAll( void *iface, UINT32 count, UINT32 *items ) { return S_OK; }

static const struct cert_errors_vector_vtbl cert_errors_vector_vtbl = {
  {
    filter_stub_QueryInterface,
    filter_stub_AddRef,
    filter_stub_Release
  },
  {
    {
    filter_stub_GetIids,
    filter_stub_GetRuntimeClassName,
    filter_stub_GetTrustLevel
    },
    {
    cert_errors_GetAt,
    cert_errors_get_Size,
    cert_errors_GetView,
    cert_errors_IndexOf,
    cert_errors_SetAt,
    cert_errors_InsertAt,
    cert_errors_RemoveAt,
    cert_errors_Append,
    cert_errors_RemoveAtEnd,
    cert_errors_Clear,
    cert_errors_GetMany,
    cert_errors_ReplaceAll
    }
  }
};

static const void *cert_errors_vector_obj = (const void *)&cert_errors_vector_vtbl;

static HRESULT WINAPI cookie_manager_SetCookie( void *iface, void *cookie, boolean *replaced )
{
    if (replaced) *replaced = FALSE;
    return S_OK;
}
static HRESULT WINAPI cookie_manager_SetCookieWithThirdParty( void *iface, void *cookie, boolean third_party, boolean *replaced )
{
    if (replaced) *replaced = FALSE;
    return S_OK;
}
static HRESULT WINAPI cookie_manager_DeleteCookie( void *iface, void *cookie ) { return S_OK; }
static HRESULT WINAPI cookie_manager_GetCookies( void *iface, void *uri, void **cookies )
{
    if (!cookies) return E_POINTER;
    *cookies = (void *)&cert_errors_vector_obj;
    return S_OK;
}
static const struct cookie_manager_vtbl http_cookie_manager_vtbl = {
  {
    filter_stub_QueryInterface,
    filter_stub_AddRef,
    filter_stub_Release
  },
  {
    {
    filter_stub_GetIids,
    filter_stub_GetRuntimeClassName,
    filter_stub_GetTrustLevel
    },
    {
    cookie_manager_SetCookie,
    cookie_manager_SetCookieWithThirdParty,
    cookie_manager_DeleteCookie,
    cookie_manager_GetCookies
    }
  }
};

static const void *http_cookie_manager_obj = (const void *)&http_cookie_manager_vtbl;

static HRESULT WINAPI base_filter_get_AllowAutoRedirect( IHttpBaseProtocolFilter *This, boolean *val ) { if (val) *val = TRUE; return S_OK; }
static HRESULT WINAPI base_filter_put_AllowAutoRedirect( IHttpBaseProtocolFilter *This, boolean val ) { return S_OK; }
static HRESULT WINAPI base_filter_get_AllowUI( IHttpBaseProtocolFilter *This, boolean *val ) { if (val) *val = TRUE; return S_OK; }
static HRESULT WINAPI base_filter_put_AllowUI( IHttpBaseProtocolFilter *This, boolean val ) { return S_OK; }
static HRESULT WINAPI base_filter_get_AutomaticDecompression( IHttpBaseProtocolFilter *This, AutomaticDecompression *val ) { if (val) *val = AutomaticDecompression_AutomaticDecompressionGZip; return S_OK; }
static HRESULT WINAPI base_filter_put_AutomaticDecompression( IHttpBaseProtocolFilter *This, AutomaticDecompression val ) { return S_OK; }
static HRESULT WINAPI base_filter_get_CacheControl( IHttpBaseProtocolFilter *This, ICacheControl **val )
{
    if (!val) return E_POINTER;
    *val = (ICacheControl *)&http_cache_control_obj;
    return S_OK;
}
static HRESULT WINAPI base_filter_get_CookieManager( IHttpBaseProtocolFilter *This, ICookieManager **val )
{
    if (!val) return E_POINTER;
    *val = (ICookieManager *)&http_cookie_manager_obj;
    return S_OK;
}
static HRESULT WINAPI base_filter_get_ClientCertificate( IHttpBaseProtocolFilter *This, IInspectable **val ) { if (val) *val = NULL; return S_OK; }
static HRESULT WINAPI base_filter_put_ClientCertificate( IHttpBaseProtocolFilter *This, IInspectable *val ) { return S_OK; }
static HRESULT WINAPI base_filter_get_IgnorableServerCertificateErrors( IHttpBaseProtocolFilter *This, IInspectable **val )
{
    if (!val) return E_POINTER;
    *val = (IInspectable *)&cert_errors_vector_obj;
    return S_OK;
}
static HRESULT WINAPI base_filter_get_MaxConnectionsPerServer( IHttpBaseProtocolFilter *This, UINT32 *val ) { if (val) *val = 6; return S_OK; }
static HRESULT WINAPI base_filter_put_MaxConnectionsPerServer( IHttpBaseProtocolFilter *This, UINT32 val ) { return S_OK; }
static HRESULT WINAPI base_filter_get_ProxyCredential( IHttpBaseProtocolFilter *This, IInspectable **val ) { if (val) *val = NULL; return S_OK; }
static HRESULT WINAPI base_filter_put_ProxyCredential( IHttpBaseProtocolFilter *This, IInspectable *val ) { return S_OK; }
static HRESULT WINAPI base_filter_get_ServerCredential( IHttpBaseProtocolFilter *This, IInspectable **val ) { if (val) *val = NULL; return S_OK; }
static HRESULT WINAPI base_filter_put_ServerCredential( IHttpBaseProtocolFilter *This, IInspectable *val ) { return S_OK; }
static HRESULT WINAPI base_filter_get_UseProxy( IHttpBaseProtocolFilter *This, boolean *val ) { if (val) *val = TRUE; return S_OK; }
static HRESULT WINAPI base_filter_put_UseProxy( IHttpBaseProtocolFilter *This, boolean val ) { return S_OK; }

static const IHttpBaseProtocolFilterVtbl http_base_protocol_filter_vtbl = {
    base_filter_QueryInterface,
    base_filter_AddRef,
    base_filter_Release,
    base_filter_GetIids,
    base_filter_GetRuntimeClassName,
    base_filter_GetTrustLevel,
    base_filter_get_AllowAutoRedirect,
    base_filter_put_AllowAutoRedirect,
    base_filter_get_AllowUI,
    base_filter_put_AllowUI,
    base_filter_get_AutomaticDecompression,
    base_filter_put_AutomaticDecompression,
    base_filter_get_CacheControl,
    base_filter_get_CookieManager,
    base_filter_get_ClientCertificate,
    base_filter_put_ClientCertificate,
    base_filter_get_IgnorableServerCertificateErrors,
    base_filter_get_MaxConnectionsPerServer,
    base_filter_put_MaxConnectionsPerServer,
    base_filter_get_ProxyCredential,
    base_filter_put_ProxyCredential,
    base_filter_get_ServerCredential,
    base_filter_put_ServerCredential,
    base_filter_get_UseProxy,
    base_filter_put_UseProxy
};

static HRESULT WINAPI filter_QueryInterface( IHttpFilter *This, REFIID iid, void **out )
{
    struct http_base_protocol_filter *impl = (struct http_base_protocol_filter *)((char *)This - sizeof(void *));
    TRACE( "This %p, iid %s, out %p\n", This, debugstr_guid( iid ), out );
    return base_filter_QueryInterface( (IHttpBaseProtocolFilter *)&impl->vtbl_base, iid, out );
}
static ULONG WINAPI filter_AddRef( IHttpFilter *This )
{
    struct http_base_protocol_filter *impl = (struct http_base_protocol_filter *)((char *)This - sizeof(void *));
    TRACE( "This %p\n", This );
    return base_filter_AddRef( (IHttpBaseProtocolFilter *)&impl->vtbl_base );
}
static ULONG WINAPI filter_Release( IHttpFilter *This )
{
    struct http_base_protocol_filter *impl = (struct http_base_protocol_filter *)((char *)This - sizeof(void *));
    TRACE( "This %p\n", This );
    return base_filter_Release( (IHttpBaseProtocolFilter *)&impl->vtbl_base );
}
static HRESULT WINAPI filter_SendRequestAsync( IHttpFilter *This, IHttpRequestMessage *request, RESP_OP **op )
{
    return client_SendRequestAsync( NULL, request, op );
}

static const IHttpFilterVtbl http_filter_vtbl = {
    filter_QueryInterface,
    filter_AddRef,
    filter_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpFilter *, ULONG *, IID **))base_filter_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpFilter *, HSTRING *))base_filter_GetRuntimeClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpFilter *, TrustLevel *))base_filter_GetTrustLevel,
    filter_SendRequestAsync
};

static HRESULT WINAPI filter_closable_QueryInterface( IClosable *This, REFIID iid, void **out )
{
    struct http_base_protocol_filter *impl = (struct http_base_protocol_filter *)((char *)This - 2 * sizeof(void *));
    return base_filter_QueryInterface( (IHttpBaseProtocolFilter *)&impl->vtbl_base, iid, out );
}
static ULONG WINAPI filter_closable_AddRef( IClosable *This )
{
    struct http_base_protocol_filter *impl = (struct http_base_protocol_filter *)((char *)This - 2 * sizeof(void *));
    return base_filter_AddRef( (IHttpBaseProtocolFilter *)&impl->vtbl_base );
}
static ULONG WINAPI filter_closable_Release( IClosable *This )
{
    struct http_base_protocol_filter *impl = (struct http_base_protocol_filter *)((char *)This - 2 * sizeof(void *));
    return base_filter_Release( (IHttpBaseProtocolFilter *)&impl->vtbl_base );
}
static HRESULT WINAPI filter_closable_Close( IClosable *This ) { return S_OK; }

static const IClosableVtbl http_filter_closable_vtbl = {
    filter_closable_QueryInterface,
    filter_closable_AddRef,
    filter_closable_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IClosable *, ULONG *, IID **))base_filter_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IClosable *, HSTRING *))base_filter_GetRuntimeClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IClosable *, TrustLevel *))base_filter_GetTrustLevel,
    filter_closable_Close
};

static HRESULT create_http_base_protocol_filter( void **out )
{
    struct http_base_protocol_filter *impl = calloc( 1, sizeof(*impl) );
    if (!impl) return E_OUTOFMEMORY;
    impl->vtbl_base = &http_base_protocol_filter_vtbl;
    impl->vtbl_filter = &http_filter_vtbl;
    impl->vtbl_closable = &http_filter_closable_vtbl;
    impl->ref = 1;
    impl->allow_auto_redirect = TRUE;
    impl->allow_ui = TRUE;
    impl->auto_decompression = TRUE;
    impl->max_connections_per_server = 6;
    impl->use_proxy = TRUE;
    *out = &impl->vtbl_base;
    return S_OK;
}

/* HttpBaseProtocolFilter Factory */
static HRESULT WINAPI filter_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    FIXME( "unimplemented iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI filter_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI filter_factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI filter_factory_GetIids( IActivationFactory *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI filter_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.Filters.HttpBaseProtocolFilter";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI filter_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI filter_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p\n", iface, instance );
    return create_http_base_protocol_filter( (void **)instance );
}

static const IActivationFactoryVtbl http_base_protocol_filter_factory_vtbl = {
    filter_factory_QueryInterface,
    filter_factory_AddRef,
    filter_factory_Release,
    filter_factory_GetIids,
    filter_factory_GetRuntimeClassName,
    filter_factory_GetTrustLevel,
    filter_factory_ActivateInstance
};
static const void *http_base_protocol_filter_factory_obj = (const void *)&http_base_protocol_filter_factory_vtbl;
IActivationFactory *http_base_protocol_filter_factory = (IActivationFactory *)&http_base_protocol_filter_factory_obj;
