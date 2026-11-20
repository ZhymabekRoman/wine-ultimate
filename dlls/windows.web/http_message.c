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

#define IRAS __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream
#define HttpResponseHeaderCollection __x_ABI_CWindows_CWeb_CHttp_CHeaders_CIHttpResponseHeaderCollection

/* --- HttpMethod --- */


static inline struct http_method *impl_from_IHttpMethod( void *iface )
{
    return (struct http_method *)iface;
}
static inline struct http_method *impl_from_method_IStringable( void *iface )
{
    return (struct http_method *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI method_QueryInterface( IHttpMethod *This, REFIID iid, void **out )
{
    struct http_method *impl = impl_from_IHttpMethod( This );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpMethod ))
    {
        *out = &impl->vtbl_method;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStringable ))
    {
        *out = &impl->vtbl_stringable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI method_AddRef( IHttpMethod *This )
{
    struct http_method *impl = impl_from_IHttpMethod( This );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI method_Release( IHttpMethod *This )
{
    struct http_method *impl = impl_from_IHttpMethod( This );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->method_name) WindowsDeleteString( impl->method_name );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI method_GetIids( IHttpMethod *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI method_GetRuntimeClassName( IHttpMethod *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpMethod";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI method_GetTrustLevel( IHttpMethod *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI method_get_Method( IHttpMethod *This, HSTRING *value )
{
    struct http_method *impl = impl_from_IHttpMethod( This );
    if (!value) return E_POINTER;
    if (impl->method_name) return WindowsDuplicateString( impl->method_name, value );
    return WindowsCreateString( L"GET", 3, value );
}

static const IHttpMethodVtbl http_method_vtbl = {
    method_QueryInterface,
    method_AddRef,
    method_Release,
    method_GetIids,
    method_GetRuntimeClassName,
    method_GetTrustLevel,
    method_get_Method
};

static HRESULT WINAPI method_str_QueryInterface( IStringable *This, REFIID iid, void **out )
{
    return method_QueryInterface( (IHttpMethod *)&impl_from_method_IStringable( This )->vtbl_method, iid, out );
}
static ULONG WINAPI method_str_AddRef( IStringable *This )
{
    return method_AddRef( (IHttpMethod *)&impl_from_method_IStringable( This )->vtbl_method );
}
static ULONG WINAPI method_str_Release( IStringable *This )
{
    return method_Release( (IHttpMethod *)&impl_from_method_IStringable( This )->vtbl_method );
}
static HRESULT WINAPI method_str_GetIids( IStringable *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI method_str_GetRuntimeClassName( IStringable *This, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI method_str_GetTrustLevel( IStringable *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI method_str_ToString( IStringable *This, HSTRING *value )
{
    return method_get_Method( (IHttpMethod *)&impl_from_method_IStringable( This )->vtbl_method, value );
}

static const IStringableVtbl http_method_stringable_vtbl = {
    method_str_QueryInterface,
    method_str_AddRef,
    method_str_Release,
    method_str_GetIids,
    method_str_GetRuntimeClassName,
    method_str_GetTrustLevel,
    method_str_ToString
};

HRESULT create_http_method( HSTRING method, void **out )
{
    struct http_method *impl;
    WDBG( "create_http_method method %p out %p\n", method, out );
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_method = &http_method_vtbl;
    impl->vtbl_stringable = &http_method_stringable_vtbl;
    impl->ref = 1;
    __TRY
    {
        const WCHAR *s = method ? WindowsGetStringRawBuffer( method, NULL ) : NULL;
        if (s && s[0]) WindowsCreateString( s, lstrlenW( s ), &impl->method_name );
    }
    __EXCEPT_ALL
    {
        impl->method_name = NULL;
    }
    __ENDTRY
    if (!impl->method_name) WindowsCreateString( L"GET", 3, &impl->method_name );
    if (out) *out = &impl->vtbl_method;
    return S_OK;
}

/* --- HttpStringContent (IHttpContent) --- */


static inline struct http_string_content *impl_from_content_closable( void *iface )
{
    return (struct http_string_content *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI content_QueryInterface( IHttpContent *This, REFIID iid, void **out )
{
    struct http_string_content *impl = impl_from_content( This );
    WDBG( "content_QueryInterface\n" );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpContent ))
    {
        *out = &impl->vtbl_content;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable ))
    {
        *out = &impl->vtbl_closable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IStringable ))
    {
        *out = &impl->vtbl_stringable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI content_AddRef( IHttpContent *This )
{
    struct http_string_content *impl = impl_from_content( This );
    WDBG( "content_AddRef\n" );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI content_Release( IHttpContent *This )
{
    struct http_string_content *impl;
    ULONG ref;

    WDBG( "content_Release\n" );
    impl = impl_from_content( This );
    ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->content) WindowsDeleteString( impl->content );
        if (impl->headers) IUnknown_Release( (IUnknown *)impl->headers );
        free( impl->raw_bytes );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI content_GetIids( IHttpContent *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI content_GetRuntimeClassName( IHttpContent *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpStringContent";
    WDBG( "content_GetRuntimeClassName\n" );
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI content_GetTrustLevel( IHttpContent *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI content_BufferAllAsync( IHttpContent *This, void **op )
{
    struct http_string_content *impl = impl_from_content( This );
    WDBG( "content_BufferAllAsync\n" );
    /* WPM's 8.1-era caller polls this op and hands the result straight to its
     * string parsing; an IBuffer result object destabilises the trace path. */
    return create_async_http_op_ex( impl->content, NULL, TRUE, TRUE, op );
}
static HRESULT WINAPI content_ReadAsBufferAsync( IHttpContent *This, void **op )
{
    struct http_string_content *impl = impl_from_content( This );
    void *buffer = NULL;
    HRESULT hr;

    WDBG( "content_ReadAsBufferAsync\n" );
    if (impl->raw_bytes)
        hr = create_mem_buffer_from_bytes( impl->raw_bytes, impl->raw_len, &buffer );
    else
        hr = create_mem_buffer_from_hstring( impl->content, &buffer );
    if (FAILED( hr )) return hr;
    hr = create_async_http_op( buffer, NULL, FALSE, op );
    IUnknown_Release( (IUnknown *)buffer );
    return hr;
}
static HRESULT WINAPI content_ReadAsInputStreamAsync( IHttpContent *This, void **op )
{
    struct http_string_content *impl = impl_from_content( This );
    void *buffer = NULL, *ras = NULL;
    HRESULT hr;

    WDBG( "content_ReadAsInputStreamAsync\n" );
    if (impl->raw_bytes)
        hr = create_mem_buffer_from_bytes( impl->raw_bytes, impl->raw_len, &buffer );
    else
        hr = create_mem_buffer_from_hstring( impl->content, &buffer );
    if (FAILED( hr )) return hr;
    hr = create_ras_stream_from_buffer( buffer, &ras );
    IUnknown_Release( (IUnknown *)buffer );
    if (FAILED( hr )) return hr;
    hr = create_async_http_op( ras, NULL, FALSE, op );
    IUnknown_Release( (IUnknown *)ras );
    return hr;
}
static HRESULT WINAPI content_ReadAsStringAsync( IHttpContent *This, void **op )
{
    struct http_string_content *impl = impl_from_content( This );
    WDBG( "content_ReadAsStringAsync\n" );
    return create_async_http_op( NULL, impl->content, TRUE, op );
}
static HRESULT WINAPI content_TryComputeLength( IHttpContent *This, UINT64 *length, boolean *succeeded )
{
    struct http_string_content *impl;
    const WCHAR *buffer;
    UINT32 len = 0;
    int utf8_len;

    WDBG( "content_TryComputeLength\n" );
    impl = impl_from_content( This );

    /* HttpStringContent defaults to UTF-8, so the length is the UTF-8 byte count */
    if (length) *length = 0;
    if (succeeded) *succeeded = FALSE;
    if (!impl->content) return S_OK;

    buffer = WindowsGetStringRawBuffer( impl->content, &len );
    utf8_len = WideCharToMultiByte( CP_UTF8, 0, buffer, len, NULL, 0, NULL, NULL );
    if (utf8_len > 0)
    {
        if (length) *length = (UINT64)utf8_len;
        if (succeeded) *succeeded = TRUE;
    }
    return S_OK;
}
static HRESULT WINAPI content_WriteToStreamAsync( IHttpContent *This, void *stream, void **op )
{
    static const GUID IID_IOutputStream_m =
        { 0x905a0fe6, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
    struct http_string_content *impl = impl_from_content( This );
    void *buffer = NULL, *out_stream = NULL, *write_op = NULL;
    UINT64 written = 0;
    HRESULT hr;

    WDBG( "content_WriteToStreamAsync stream %p raw_len %lu\n", stream, (unsigned long)impl->raw_len );
    if (stream)
    {
        if (impl->raw_bytes && impl->raw_len)
        {
            hr = create_mem_buffer_from_bytes( impl->raw_bytes, impl->raw_len, &buffer );
            written = impl->raw_len;
        }
        else if (impl->content)
        {
            UINT32 wlen = 0;
            const WCHAR *wbuf = WindowsGetStringRawBuffer( impl->content, &wlen );
            hr = create_mem_buffer_from_hstring( impl->content, &buffer );
            written = (UINT64)WideCharToMultiByte( CP_UTF8, 0, wbuf, wlen, NULL, 0, NULL, NULL );
        }
        else
            hr = E_FAIL;

        if (SUCCEEDED( hr ) && buffer)
        {
            if (SUCCEEDED( IUnknown_QueryInterface( (IUnknown *)stream, &IID_IOutputStream_m, &out_stream ) ) && out_stream)
            {
                /* IOutputStream::WriteAsync is slot 6 */
                HRESULT (WINAPI *write_async)( void *, void *, void ** ) =
                    (void *)((void *const *)(*(void *const **)out_stream))[6];
                if (SUCCEEDED( write_async( out_stream, buffer, &write_op ) ) && write_op)
                    IUnknown_Release( (IUnknown *)write_op );
                IUnknown_Release( (IUnknown *)out_stream );
            }
            IUnknown_Release( (IUnknown *)buffer );
        }
    }
    return create_async_http_op_u64( written, op );
}
HRESULT WINAPI content_get_Headers( void *iface, void **headers )
{
    struct http_string_content *impl = impl_from_content( iface );
    WDBG( "content_get_Headers\n" );
    if (!headers) return E_POINTER;
    if (!impl->headers) create_http_headers( &IID_IHttpContentHeaderCollection, &impl->headers );
    IUnknown_AddRef( (IUnknown *)impl->headers );
    *headers = impl->headers;
    return S_OK;
}

/* 19045 SDK IHttpContent ABI: ContentType, ContentLength, Headers, GetBuffer,
 * TryComputeSize, ReadAsInputStream, ReadAsString, WriteToStream (slots 6-14).
 * WPM (NativeAOT, 19045) calls ReadAsStringAsync at slot 13. */
static HRESULT WINAPI content_get_ContentType( IHttpContent *This, HSTRING *value )
{
    WDBG( "content_get_ContentType\n" );
    if (!value) return E_POINTER;
    return WindowsCreateString( L"application/json", 16, value );
}
static HRESULT WINAPI content_put_ContentType( IHttpContent *This, HSTRING value )
{
    WDBG( "content_put_ContentType\n" );
    return S_OK;
}
static HRESULT WINAPI content_get_ContentLength( IHttpContent *This, UINT64 *value )
{
    struct http_string_content *impl = impl_from_content( This );
    WDBG( "content_get_ContentLength\n" );
    if (!value) return E_POINTER;
    *value = impl->content ? WindowsGetStringLen( impl->content ) : 0;
    return S_OK;
}

static const struct http_content_vtbl http_content_vtbl = {
  {
    content_QueryInterface,
    content_AddRef,
    content_Release,
    content_GetIids,
    content_GetRuntimeClassName,
    content_GetTrustLevel,
    /* Standard WinRT IHttpContent ({6B14A441-FBA7-4BD2-AF0A-839DE7C295DA}) ABI:
     * 6 get_Headers, 7 BufferAllAsync, 8 ReadAsBufferAsync,
     * 9 ReadAsInputStreamAsync, 10 ReadAsStringAsync,
     * 11 TryComputeLength, 12 WriteToStreamAsync. */
    (void *)content_get_Headers,
    (void *)content_BufferAllAsync,
    (void *)content_ReadAsBufferAsync,
    (void *)content_ReadAsInputStreamAsync,
    (void *)content_ReadAsStringAsync,
    (void *)content_TryComputeLength,
    (void *)content_WriteToStreamAsync
  },
  {
    /* 12 ContentType */
    content_get_Headers,
    /* 13 Headers */
    /* Projection-region fill: WPM virtual-calls IHttpContent helper slots
     * past the 8.1 declared face (formatter path). */
    content_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))content_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))content_ReadAsBufferAsync,
    content_TryComputeLength,
    content_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))content_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))content_BufferAllAsync,
    content_TryComputeLength,
    content_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))content_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))content_BufferAllAsync,
    content_TryComputeLength,
    content_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))content_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))content_BufferAllAsync,
    content_TryComputeLength,
    content_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))content_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))content_BufferAllAsync,
    content_TryComputeLength,
    content_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))content_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))content_BufferAllAsync,
    content_put_ContentType,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))content_ReadAsStringAsync,
    /* 39 */
  }
};

static HRESULT WINAPI content_closable_QueryInterface( IClosable *This, REFIID iid, void **out )
{
    WDBG( "content_closable_QueryInterface\n" );
    return content_QueryInterface( (IHttpContent *)&impl_from_content_closable( This )->vtbl_content, iid, out );
}
static ULONG WINAPI content_closable_AddRef( IClosable *This )
{
    WDBG( "content_closable_AddRef\n" );
    return content_AddRef( (IHttpContent *)&impl_from_content_closable( This )->vtbl_content );
}
static ULONG WINAPI content_closable_Release( IClosable *This )
{
    WDBG( "content_closable_Release\n" );
    return content_Release( (IHttpContent *)&impl_from_content_closable( This )->vtbl_content );
}
static HRESULT WINAPI content_closable_GetIids( IClosable *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI content_closable_GetRuntimeClassName( IClosable *This, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI content_closable_GetTrustLevel( IClosable *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI content_closable_Close( IClosable *This ) { return S_OK; }

static const IClosableVtbl http_content_closable_vtbl = {
    content_closable_QueryInterface,
    content_closable_AddRef,
    content_closable_Release,
    content_closable_GetIids,
    content_closable_GetRuntimeClassName,
    content_closable_GetTrustLevel,
    content_closable_Close
};

static HRESULT WINAPI content_str_ToString( IStringable *This, HSTRING *val )
{
    struct http_string_content *impl = (struct http_string_content *)((BYTE *)This - 2 * sizeof(void *));
    WDBG( "content_str_ToString\n" );
    if (!val) return E_POINTER;
    if (impl->content) return WindowsDuplicateString( impl->content, val );
    return WindowsCreateString( L"", 0, val );
}

static inline struct http_string_content *impl_from_content_stringable( void *iface )
{
    return (struct http_string_content *)((BYTE *)iface - 2 * sizeof(void *));
}

static HRESULT WINAPI content_str_QueryInterface( IStringable *This, REFIID iid, void **out )
{
    WDBG( "content_str_QueryInterface\n" );
    return content_QueryInterface( (IHttpContent *)&impl_from_content_stringable( This )->vtbl_content, iid, out );
}
static ULONG WINAPI content_str_AddRef( IStringable *This )
{
    WDBG( "content_str_AddRef\n" );
    return content_AddRef( (IHttpContent *)&impl_from_content_stringable( This )->vtbl_content );
}
static ULONG WINAPI content_str_Release( IStringable *This )
{
    WDBG( "content_str_Release\n" );
    return content_Release( (IHttpContent *)&impl_from_content_stringable( This )->vtbl_content );
}

static const IStringableVtbl http_content_stringable_vtbl = {
    content_str_QueryInterface,
    content_str_AddRef,
    content_str_Release,
    (HRESULT (STDMETHODCALLTYPE *)(IStringable *, ULONG *, IID **))content_closable_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IStringable *, HSTRING *))content_closable_GetRuntimeClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IStringable *, TrustLevel *))content_closable_GetTrustLevel,
    content_str_ToString
};

HRESULT create_http_string_content( HSTRING content, void **out )
{
    struct http_string_content *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_content = &http_content_vtbl;
    impl->vtbl_closable = &http_content_closable_vtbl;
    impl->vtbl_stringable = &http_content_stringable_vtbl;
    impl->ref = 1;
    if (content) WindowsDuplicateString( content, &impl->content );
    *out = &impl->vtbl_content;
    return S_OK;
}

HRESULT create_http_buffer_content( void *ibuf, UINT32 offset, UINT32 count, BOOL has_range, void **out )
{
    static const GUID IID_IBufferByteAccess_local =
        { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
    struct http_string_content *impl;
    BYTE *src = NULL;
    UINT32 len = 0;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_content = &http_content_vtbl;
    impl->vtbl_closable = &http_content_closable_vtbl;
    impl->vtbl_stringable = &http_content_stringable_vtbl;
    impl->ref = 1;

    if (ibuf)
    {
        /* IBuffer::get_Length is slot 7 */
        HRESULT (WINAPI *get_len)( void *, UINT32 * ) =
            (void *)((void *const *)(*(void *const **)ibuf))[7];
        void *bba = NULL;
        if (get_len) get_len( ibuf, &len );
        if (SUCCEEDED( IUnknown_QueryInterface( (IUnknown *)ibuf, &IID_IBufferByteAccess_local, &bba ) ) && bba)
        {
            /* IBufferByteAccess::Buffer is slot 3 */
            HRESULT (WINAPI *get_buf)( void *, BYTE ** ) =
                (void *)((void *const *)(*(void *const **)bba))[3];
            if (get_buf) get_buf( bba, &src );
            IUnknown_Release( (IUnknown *)bba );
        }
    }
    if (src && len > 0)
    {
        UINT32 start = has_range ? min( offset, len ) : 0;
        UINT32 avail = len - start;
        UINT32 take = has_range ? min( count, avail ) : avail;
        if (take > 0)
        {
            impl->raw_bytes = malloc( take );
            if (impl->raw_bytes)
            {
                int wlen;
                memcpy( impl->raw_bytes, src + start, take );
                impl->raw_len = take;
                wlen = MultiByteToWideChar( CP_UTF8, 0, (const char *)impl->raw_bytes, take, NULL, 0 );
                if (wlen > 0)
                {
                    WCHAR *wbuf = malloc( (wlen + 1) * sizeof(WCHAR) );
                    if (wbuf)
                    {
                        MultiByteToWideChar( CP_UTF8, 0, (const char *)impl->raw_bytes, take, wbuf, wlen );
                        wbuf[wlen] = 0;
                        WindowsCreateString( wbuf, wlen, &impl->content );
                        free( wbuf );
                    }
                }
            }
        }
    }
    *out = &impl->vtbl_content;
    return S_OK;
}

/* --- HttpStreamContent (IHttpContent over an IRandomAccessStream) ---
 * WPM's request bodies for source updates are HttpStreamContent over a
 * WinRT stream; the send path pumps it via the Win11 projection slots
 * (positioned ReadAsync / GetResults) on the content's stream. */


static inline struct http_stream_content *impl_from_stream_content( void *iface )
{
    return (struct http_stream_content *)iface;
}
static inline struct http_stream_content *impl_from_stream_content_closable( void *iface )
{
    return (struct http_stream_content *)((BYTE *)iface - sizeof(void *));
}

static HRESULT streamc_get_size( void *s, UINT64 *size )
{
    return ((HRESULT (WINAPI *)( void *, UINT64 * ))((void *const *)(*(void *const **)s))[6])( s, size );
}
static HRESULT streamc_read_at( void *s, void *buf, UINT64 pos, ULONG count, void **op )
{
    return ((HRESULT (WINAPI *)( void *, void *, UINT64, ULONG, void ** ))
            ((void *const *)(*(void *const **)s))[21])( s, buf, pos, count, op );
}
static HRESULT streamc_read( void *s, void *buf, ULONG count, void **op )
{
    return ((HRESULT (WINAPI *)( void *, void *, ULONG, INT32, void ** ))
            ((void *const *)(*(void *const **)s))[30])( s, buf, count, 0, op );
}
static HRESULT streamc_op_count( void *op, ULONG *n )
{
    return ((HRESULT (WINAPI *)( void *, ULONG * ))((void *const *)(*(void *const **)op))[21])( op, n );
}
static HRESULT streamc_buf_start( void *b, BYTE **p )
{
    return ((HRESULT (WINAPI *)( void *, BYTE ** ))((void *const *)(*(void *const **)b))[8])( b, p );
}
static HRESULT streamc_write( void *t, void *buf, void **op )
{
    return ((HRESULT (WINAPI *)( void *, void *, void ** ))((void *const *)(*(void *const **)t))[17])( t, buf, op );
}

static HRESULT streamc_pump( struct http_stream_content *impl, BYTE **out, ULONG *out_len )
{
    UINT64 size = 0;
    HRESULT hr;
    ULONG pos = 0;

    if (impl->cached)
    {
        *out = impl->cached;
        *out_len = impl->cached_len;
        return S_OK;
    }
    if (FAILED( (hr = streamc_get_size( impl->stream, &size )) )) return hr;
    if (!size)
    {
        *out = NULL;
        *out_len = 0;
        return S_OK;
    }
    if (!(impl->cached = HeapAlloc( GetProcessHeap(), 0, (SIZE_T)size ))) return E_OUTOFMEMORY;
    while ((UINT64)pos < size)
    {
        ULONG chunk = (UINT64)(size - pos) > 65536 ? 65536 : (ULONG)(size - pos);
        ULONG got = 0;
        void *buf = NULL, *op = NULL;
        BYTE *p = NULL;

        if (FAILED( (hr = create_empty_mem_buffer( chunk, &buf )) )) return hr;
        hr = streamc_read_at( impl->stream, buf, pos, chunk, &op );
        if (FAILED(hr))
        {
            IUnknown_Release( (IUnknown *)buf );
            buf = NULL;
            if (FAILED( (hr = create_empty_mem_buffer( chunk, &buf )) )) return hr;
            if (FAILED( (hr = streamc_read( impl->stream, buf, chunk, &op )) ))
            {
                IUnknown_Release( (IUnknown *)buf );
                return hr;
            }
        }
        streamc_op_count( op, &got );
        IUnknown_Release( (IUnknown *)op );
        streamc_buf_start( buf, &p );
        if (p && got && pos + got <= (ULONG)size) memcpy( impl->cached + pos, p, got );
        IUnknown_Release( (IUnknown *)buf );
        pos += got;
        if (!got) break;
    }
    impl->cached_len = pos;
    *out = impl->cached;
    *out_len = pos;
    return S_OK;
}

static HRESULT WINAPI streamc_QueryInterface( IHttpContent *This, REFIID iid, void **out )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    WDBG( "streamc_QI\n" );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpContent ))
    {
        *out = &impl->vtbl_content;
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
static ULONG WINAPI streamc_AddRef( IHttpContent *This )
{
    return InterlockedIncrement( &impl_from_stream_content( This )->ref );
}
static ULONG WINAPI streamc_Release( IHttpContent *This )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->stream) IUnknown_Release( (IUnknown *)impl->stream );
        if (impl->headers) IUnknown_Release( (IUnknown *)impl->headers );
        if (impl->cached) HeapFree( GetProcessHeap(), 0, impl->cached );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI streamc_GetIids( IHttpContent *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI streamc_GetRuntimeClassName( IHttpContent *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpStreamContent";
    if (!n) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE( class_name ) - 1, n );
}
static HRESULT WINAPI streamc_GetTrustLevel( IHttpContent *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI streamc_BufferAllAsync( IHttpContent *This, void **op )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    BYTE *data = NULL;
    ULONG len = 0;
    void *buf = NULL;
    HRESULT hr;

    WDBG( "streamc_BufferAllAsync\n" );
    if (FAILED( (hr = streamc_pump( impl, &data, &len )) )) return hr;
    if (FAILED( (hr = create_mem_buffer_from_bytes( data, len, &buf )) )) return hr;
    hr = create_async_http_op_obj( buf, op );
    IUnknown_Release( (IUnknown *)buf );
    return hr;
}
static HRESULT WINAPI streamc_TryComputeLength( IHttpContent *This, UINT64 *length, boolean *succeeded )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    UINT64 size = 0;

    WDBG( "streamc_TryComputeLength\n" );
    if (length) *length = 0;
    if (succeeded) *succeeded = FALSE;
    if (FAILED( streamc_get_size( impl->stream, &size ) )) return S_OK;
    if (length) *length = size;
    if (succeeded) *succeeded = TRUE;
    return S_OK;
}
static HRESULT WINAPI streamc_ReadAsInputStreamAsync( IHttpContent *This, void **op )
{
    struct http_stream_content *impl = impl_from_stream_content( This );

    WDBG( "streamc_ReadAsInputStreamAsync\n" );
    return create_async_http_op_obj( impl->stream, op );
}
static HRESULT WINAPI streamc_ReadAsStringAsync( IHttpContent *This, void **op )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    BYTE *data = NULL;
    ULONG len = 0;
    WCHAR *w = NULL;
    HSTRING str = NULL;
    int wn;
    HRESULT hr;

    WDBG( "streamc_ReadAsStringAsync\n" );
    if (FAILED( (hr = streamc_pump( impl, &data, &len )) )) return hr;
    wn = MultiByteToWideChar( CP_UTF8, 0, (const char *)data, len, NULL, 0 );
    if (wn < 0) wn = 0;
    if (!(w = HeapAlloc( GetProcessHeap(), 0, (wn + 1) * sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    MultiByteToWideChar( CP_UTF8, 0, (const char *)data, len, w, wn );
    w[wn] = 0;
    hr = WindowsCreateString( w, wn, &str );
    HeapFree( GetProcessHeap(), 0, w );
    if (FAILED( hr )) return hr;
    hr = create_async_http_op( NULL, str, TRUE, op );
    WindowsDeleteString( str );
    return hr;
}
static HRESULT WINAPI streamc_WriteToStreamAsync( IHttpContent *This, void *target, void **op )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    BYTE *data = NULL;
    ULONG len = 0, pos = 0;
    HRESULT hr;

    WDBG( "streamc_WriteToStreamAsync\n" );
    if (FAILED( (hr = streamc_pump( impl, &data, &len )) )) return hr;
    while (pos < len)
    {
        ULONG chunk = len - pos > 65536 ? 65536 : len - pos;
        void *buf = NULL, *wop = NULL;

        if (FAILED( (hr = create_mem_buffer_from_bytes( data + pos, chunk, &buf )) )) return hr;
        hr = streamc_write( target, buf, &wop );
        IUnknown_Release( (IUnknown *)buf );
        if (SUCCEEDED( hr ) && wop) IUnknown_Release( (IUnknown *)wop );
        if (FAILED( hr )) return hr;
        pos += chunk;
    }
    return create_async_http_op_obj( NULL, op );
}
static HRESULT WINAPI streamc_get_ContentLength( IHttpContent *This, UINT64 *value )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    if (!value) return E_POINTER;
    *value = impl->cached_len;
    streamc_get_size( impl->stream, value );
    return S_OK;
}
static HRESULT WINAPI streamc_get_ContentType( IHttpContent *This, HSTRING *value )
{
    static const WCHAR ct[] = L"application/octet-stream";
    if (!value) return E_POINTER;
    return WindowsCreateString( ct, ARRAY_SIZE( ct ) - 1, value );
}
static HRESULT WINAPI streamc_put_ContentType( IHttpContent *This, HSTRING value ) { return S_OK; }
static HRESULT WINAPI streamc_get_Headers( IHttpContent *This, void **headers )
{
    struct http_stream_content *impl = impl_from_stream_content( This );
    if (!headers) return E_POINTER;
    if (!impl->headers) create_http_headers( &IID_IHttpContentHeaderCollection, &impl->headers );
    IUnknown_AddRef( (IUnknown *)impl->headers );
    *headers = impl->headers;
    return S_OK;
}

static const struct http_content_vtbl http_stream_content_vtbl = {
  {
    streamc_QueryInterface,
    streamc_AddRef,
    streamc_Release,
    streamc_GetIids,
    streamc_GetRuntimeClassName,
    streamc_GetTrustLevel,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))streamc_BufferAllAsync,
    /* 6  GetBufferAsync/BufferAllAsync */
    streamc_TryComputeLength,
    /* 7 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIInputStream_HttpProgress**))streamc_ReadAsInputStreamAsync,
    /* 8 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    /* 9 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, IRAS *, __FIAsyncOperationWithProgress_2_UINT64_UINT64**))streamc_WriteToStreamAsync,
    /* 10 */
    streamc_get_ContentLength,
    /* 11 */
    streamc_get_ContentType,
    /* 12 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, IHttpContentHeaderCollection **))streamc_get_Headers
  },
  {
    /* 13 */
    /* projection-region fill (same cyclic map as HttpStringContent) */
    streamc_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))streamc_BufferAllAsync,
    streamc_put_ContentType,
    streamc_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))streamc_BufferAllAsync,
    streamc_TryComputeLength,
    streamc_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))streamc_BufferAllAsync,
    streamc_TryComputeLength,
    streamc_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))streamc_BufferAllAsync,
    streamc_TryComputeLength,
    streamc_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))streamc_BufferAllAsync,
    streamc_TryComputeLength,
    streamc_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress**))streamc_BufferAllAsync,
    streamc_TryComputeLength,
    streamc_get_ContentLength,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpContent *, __FIAsyncOperationWithProgress_2_HSTRING_HttpProgress**))streamc_ReadAsStringAsync,
    /* 39 */
  }
};

static const IClosableVtbl http_stream_content_closable_vtbl = {
    content_closable_QueryInterface,
    content_closable_AddRef,
    content_closable_Release,
    content_closable_GetIids,
    content_closable_GetRuntimeClassName,
    content_closable_GetTrustLevel,
    content_closable_Close
};

HRESULT create_http_stream_content( void *stream, void **out )
{
    struct http_stream_content *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_content = &http_stream_content_vtbl;
    impl->vtbl_closable = &http_stream_content_closable_vtbl;
    impl->ref = 1;
    impl->stream = stream;
    if (stream) IUnknown_AddRef( (IUnknown *)stream );
    *out = &impl->vtbl_content;
    return S_OK;
}

/* IHttpStreamContentFactory + IActivationFactory share one vtable:
 * 6 CreateInstance(), 7 get_Compressor, 8 put_Compressor, 9 CreateInstance(stream). */
static HRESULT WINAPI http_stream_content_factory_QueryInterface( IActivationFactory *This, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpStreamContentFactory ))
    {
        *out = This;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_stream_content_factory_AddRef( IActivationFactory *This ) { return 2; }
static ULONG WINAPI http_stream_content_factory_Release( IActivationFactory *This ) { return 1; }
static HRESULT WINAPI http_stream_content_factory_GetIids( IActivationFactory *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_stream_content_factory_GetRuntimeClassName( IActivationFactory *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpStreamContent";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_stream_content_factory_GetTrustLevel( IActivationFactory *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_stream_content_factory_ActivateInstance( IActivationFactory *This, IInspectable **instance )
{
    WDBG( "stream_content_ActivateInstance\n" );
    return create_http_stream_content( NULL, (void **)instance );
}
static HRESULT WINAPI http_stream_content_factory_GetCompressor( IActivationFactory *This, void **value )
{
    if (value) *value = NULL;
    return S_OK;
}
static HRESULT WINAPI http_stream_content_factory_PutCompressor( IActivationFactory *This, void *value ) { return S_OK; }
static HRESULT WINAPI http_stream_content_factory_CreateInstanceWithStream( IActivationFactory *This, void *stream, void **instance )
{
    WDBG( "stream_content_CreateWithStream stream %p\n", stream );
    return create_http_stream_content( stream, instance );
}

static const struct stream_content_factory_vtbl http_stream_content_factory_vtbl = {
  {
    http_stream_content_factory_QueryInterface,
    http_stream_content_factory_AddRef,
    http_stream_content_factory_Release,
    http_stream_content_factory_GetIids,
    http_stream_content_factory_GetRuntimeClassName,
    http_stream_content_factory_GetTrustLevel,
    http_stream_content_factory_ActivateInstance
  },
  {
    http_stream_content_factory_GetCompressor,
    http_stream_content_factory_PutCompressor,
    http_stream_content_factory_CreateInstanceWithStream
  }
};
static const void *http_stream_content_factory_obj = (const void *)&http_stream_content_factory_vtbl;
IActivationFactory *http_stream_content_factory = (IActivationFactory *)&http_stream_content_factory_obj;

/* --- HttpResponseMessage --- */


static inline struct http_response_message *impl_from_response( void *iface )
{
    return (struct http_response_message *)iface;
}
static inline struct http_response_message *impl_from_response_closable( void *iface )
{
    return (struct http_response_message *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI resp_QueryInterface( IHttpResponseMessage *This, REFIID iid, void **out )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "resp_QI {%08x-%04x}\n", (unsigned)iid->Data1, iid->Data4[6] );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpResponseMessage ))
    {
        *out = &impl->vtbl_response;
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
static ULONG WINAPI resp_AddRef( IHttpResponseMessage *This )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "resp_AddRef\n" );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI resp_Release( IHttpResponseMessage *This )
{
    struct http_response_message *impl;
    ULONG ref;

    WDBG( "resp_Release\n" );
    impl = impl_from_response( This );
    ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->reason_phrase) WindowsDeleteString( impl->reason_phrase );
        if (impl->content) IUnknown_Release( (IUnknown *)impl->content );
        if (impl->headers) IUnknown_Release( (IUnknown *)impl->headers );
        if (impl->request_message) IUnknown_Release( (IUnknown *)impl->request_message );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI resp_GetIids( IHttpResponseMessage *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI resp_GetRuntimeClassName( IHttpResponseMessage *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpResponseMessage";
    WDBG( "resp_GetRuntimeClassName\n" );
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI resp_GetTrustLevel( IHttpResponseMessage *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI resp_EnsureSuccessStatusCode( IHttpResponseMessage *This, void **value )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "EnsureSuccess\n" );
    if (impl->status_code < 200 || impl->status_code >= 300) return E_FAIL;
    if (value)
    {
        *value = This;
        resp_AddRef( (IHttpResponseMessage *)This );
    }
    return S_OK;
}
static HRESULT WINAPI resp_get_Content( IHttpResponseMessage *This, void **content )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "resp_get_Content\n" );
    if (!content) return E_POINTER;
    if (!impl->content)
    {
        /* Documented fallback when no response body was stored by the caller/transport. */
        static const WCHAR fallback_html[] = L"<html><head><title>Response</title></head><body></body></html>";
        HSTRING dummy_html;
        WindowsCreateString( fallback_html, ARRAY_SIZE(fallback_html) - 1, &dummy_html );
        create_http_string_content( dummy_html, &impl->content );
        WindowsDeleteString( dummy_html );
    }
    IUnknown_AddRef( (IUnknown *)impl->content );
    *content = impl->content;
    return S_OK;
}
HRESULT WINAPI resp_set_Content( void *iface, void *content )
{
    struct http_response_message *impl = impl_from_response( iface );
    WDBG( "resp_set_Content\n" );
    if (impl->content) IUnknown_Release( (IUnknown *)impl->content );
    impl->content = content;
    if (impl->content) IUnknown_AddRef( (IUnknown *)impl->content );
    return S_OK;
}
HRESULT WINAPI resp_get_Headers( void *iface, void **headers )
{
    struct http_response_message *impl = impl_from_response( iface );
    WDBG( "resp_get_Headers\n" );
    if (!headers) return E_POINTER;
    if (!impl->headers) create_http_headers( &IID_IHttpResponseHeaderCollection, &impl->headers );
    IUnknown_AddRef( (IUnknown *)impl->headers );
    *headers = impl->headers;
    return S_OK;
}
static HRESULT WINAPI resp_get_IsSuccessStatusCode( IHttpResponseMessage *This, boolean *val )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "IsSuccess\n" );
    if (val) *val = (impl->status_code >= 200 && impl->status_code < 300);
    return S_OK;
}
static HRESULT WINAPI resp_get_ReasonPhrase( IHttpResponseMessage *This, HSTRING *val )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "ReasonPhrase\n" );
    if (!val) return E_POINTER;
    if (impl->reason_phrase) return WindowsDuplicateString( impl->reason_phrase, val );
    return WindowsCreateString( L"OK", 2, val );
}
static HRESULT WINAPI resp_set_ReasonPhrase( IHttpResponseMessage *This, HSTRING val )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "resp_set_ReasonPhrase\n" );
    if (impl->reason_phrase) WindowsDeleteString( impl->reason_phrase );
    impl->reason_phrase = NULL;
    if (val) WindowsDuplicateString( val, &impl->reason_phrase );
    return S_OK;
}
static HRESULT WINAPI resp_get_RequestMessage( IHttpResponseMessage *This, void **req )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "RequestMessage\n" );
    if (!req) return E_POINTER;
    if (impl->request_message) IUnknown_AddRef( (IUnknown *)impl->request_message );
    *req = impl->request_message;
    return S_OK;
}
static HRESULT WINAPI resp_set_RequestMessage( IHttpResponseMessage *This, void *req )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "resp_set_RequestMessage\n" );
    if (impl->request_message) IUnknown_Release( (IUnknown *)impl->request_message );
    impl->request_message = req;
    if (impl->request_message) IUnknown_AddRef( (IUnknown *)impl->request_message );
    return S_OK;
}
static HRESULT WINAPI resp_get_Source( IHttpResponseMessage *This, INT32 *src ) { if (src) *src = 2 /* Network */; return S_OK; }
static HRESULT WINAPI resp_set_Source( IHttpResponseMessage *This, INT32 src ) { return S_OK; }
static HRESULT WINAPI resp_get_StatusCode( IHttpResponseMessage *This, INT32 *code )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "StatusCode\n" );
    if (code) *code = impl->status_code;
    return S_OK;
}
static HRESULT WINAPI resp_set_StatusCode( IHttpResponseMessage *This, INT32 code )
{
    struct http_response_message *impl = impl_from_response( This );
    WDBG( "set_StatusCode\n" );
    impl->status_code = code;
    return S_OK;
}
static HRESULT WINAPI resp_get_Version( IHttpResponseMessage *This, INT32 *ver ) { if (ver) *ver = 1 /* Http11 */; return S_OK; }
static HRESULT WINAPI resp_set_Version( IHttpResponseMessage *This, INT32 ver ) { return S_OK; }

/* Slot order must match the Windows.Web.Http 19045 SDK IHttpResponseMessage ABI:
 * Content leads, EnsureSuccessStatusCode follows, and there is no set_Content
 * slot (the ctor takes the content).  WPM (NativeAOT, 19045) calls by slot. */
static const struct http_response_vtbl http_response_vtbl = {
  {
    resp_QueryInterface,
    resp_AddRef,
    resp_Release,
    resp_GetIids,
    resp_GetRuntimeClassName,
    resp_GetTrustLevel,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpContent **))resp_get_Content,
    /* 6  */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpResponseMessage **))resp_EnsureSuccessStatusCode,
    /* 7  */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, HttpResponseHeaderCollection **))resp_get_Headers,
    /* 8  */
    resp_get_IsSuccessStatusCode,
    /* 9  */
    resp_get_ReasonPhrase,
    /* 10 */
    resp_set_ReasonPhrase,
    /* 11 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpRequestMessage **))resp_get_RequestMessage,
    /* 12 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpRequestMessage *))resp_set_RequestMessage,
    /* 13 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, __x_ABI_CWindows_CWeb_CHttp_CHttpSource *))resp_get_Source,
    /* 14 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, __x_ABI_CWindows_CWeb_CHttp_CHttpSource))resp_set_Source,
    /* 15 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, UINT32 *))resp_get_StatusCode,
    /* 16 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, UINT32))resp_set_StatusCode,
    /* 17 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, __x_ABI_CWindows_CWeb_CHttp_CHTTPVersion *))resp_get_Version,
    /* 18 */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, __x_ABI_CWindows_CWeb_CHttp_CHTTPVersion))resp_set_Version
  },
  {
    /* 19 */
    /* WPM's HttpClientWrapper coroutine states virtual-call projection helper
     * slots beyond IHttpResponseMessage (the CLog::Trace formatter uses slot
     * 39); fill the projection region so calls land on real accessors. */
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, UINT32 *))resp_get_StatusCode,
    resp_get_ReasonPhrase,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpContent **))resp_get_Content,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, HttpResponseHeaderCollection **))resp_get_Headers,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, UINT32 *))resp_get_StatusCode,
    resp_get_ReasonPhrase,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpContent **))resp_get_Content,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, HttpResponseHeaderCollection **))resp_get_Headers,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, UINT32 *))resp_get_StatusCode,
    resp_get_ReasonPhrase,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpContent **))resp_get_Content,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, HttpResponseHeaderCollection **))resp_get_Headers,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, UINT32 *))resp_get_StatusCode,
    resp_get_ReasonPhrase,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpContent **))resp_get_Content,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, HttpResponseHeaderCollection **))resp_get_Headers,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, UINT32 *))resp_get_StatusCode,
    resp_get_ReasonPhrase,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, IHttpContent **))resp_get_Content,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpResponseMessage *, HttpResponseHeaderCollection **))resp_get_Headers,
    /* 39 */
  }
};

static HRESULT WINAPI resp_closable_QueryInterface( IClosable *This, REFIID iid, void **out )
{
    WDBG( "resp_closable_QueryInterface\n" );
    return resp_QueryInterface( (IHttpResponseMessage *)&impl_from_response_closable( This )->vtbl_response, iid, out );
}
static ULONG WINAPI resp_closable_AddRef( IClosable *This )
{
    WDBG( "resp_closable_AddRef\n" );
    return resp_AddRef( (IHttpResponseMessage *)&impl_from_response_closable( This )->vtbl_response );
}
static ULONG WINAPI resp_closable_Release( IClosable *This )
{
    WDBG( "resp_closable_Release\n" );
    return resp_Release( (IHttpResponseMessage *)&impl_from_response_closable( This )->vtbl_response );
}
static HRESULT WINAPI resp_closable_GetIids( IClosable *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI resp_closable_GetRuntimeClassName( IClosable *This, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI resp_closable_GetTrustLevel( IClosable *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI resp_closable_Close( IClosable *This ) { return S_OK; }

static const IClosableVtbl http_response_closable_vtbl = {
    resp_closable_QueryInterface,
    resp_closable_AddRef,
    resp_closable_Release,
    resp_closable_GetIids,
    resp_closable_GetRuntimeClassName,
    resp_closable_GetTrustLevel,
    resp_closable_Close
};

HRESULT create_http_response( INT32 status_code, void **out )
{
    struct http_response_message *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_response = &http_response_vtbl;
    impl->vtbl_closable = &http_response_closable_vtbl;
    impl->ref = 1;
    impl->status_code = status_code ? status_code : 200;
    impl->source = 2;
    impl->version = 1;
    /* WPM's HttpClientWrapper reads response.RequestMessage and keeps using it;
     * a fresh request message keeps that chain non-null. */
    create_http_request( NULL, NULL, &impl->request_message );
    *out = &impl->vtbl_response;
    return S_OK;
}

static BOOL is_hstring( const void *p );


/* --- minimal UriRuntimeClass wrapper over an HSTRING --- */

static inline struct uri_obj *impl_from_uri( void *iface ) { return (struct uri_obj *)iface; }

static HRESULT WINAPI uri_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct uri_obj *impl = impl_from_uri( iface );
    WDBG( "uri_QueryInterface %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUriRuntimeClass ))
    {
        *out = &impl->vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI uri_AddRef( void *iface ) { return InterlockedIncrement( &impl_from_uri( iface )->ref ); }
static ULONG WINAPI uri_Release( void *iface )
{
    struct uri_obj *impl;
    ULONG ref;

    WDBG( "uri_Release\n" );
    impl = impl_from_uri( iface );
    ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->uri );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI uri_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI uri_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Uri";
    WDBG( "uri_GetRuntimeClassName\n" );
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI uri_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI uri_get_String( void *iface, HSTRING *value )
{
    struct uri_obj *impl = impl_from_uri( iface );
    WDBG( "uri_get_String\n" );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->uri, value );
}

static const struct uri_vtbl uri_vtbl = {
  {
    (HRESULT (STDMETHODCALLTYPE *)(IUnknown *, REFIID, void **))uri_QueryInterface,
    (ULONG (STDMETHODCALLTYPE *)(IUnknown *))uri_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IUnknown *))uri_Release
  },
    {
      (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, REFIID, void **))uri_GetIids,
      (ULONG (STDMETHODCALLTYPE *)(IInspectable *))uri_GetRuntimeClassName,
      (ULONG (STDMETHODCALLTYPE *)(IInspectable *))uri_GetTrustLevel,
      (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, ULONG *, IID **))uri_get_String,
      (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, HSTRING *))uri_get_String,
      (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, TrustLevel *))uri_get_String,
    },
    /* DisplayScheme */
    uri_get_String,
    /* Domain */
    uri_get_String,
    /* Extension */
    uri_get_String,
    /* Host */
    uri_get_String,
    /* Password */
    uri_get_String,
    /* Path */
    uri_get_String,
    /* Query */
    uri_get_String,
    /* QueryString */
    uri_get_String,
    /* UserName */
    uri_get_String,
    /* Port */
    uri_get_String,
    /* SchemeName */
    uri_get_String,
    /* Fragment14 */
    uri_get_String,
    /* Fragment15 */
    uri_get_String,
    /* Fragment16 */
};

HRESULT create_uri( HSTRING uri, void **out )
{
    struct uri_obj *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl = &uri_vtbl;
    impl->ref = 1;
    WindowsDuplicateString( uri, &impl->uri );
    *out = &impl->vtbl;
    return S_OK;
}

/* --- HttpRequestMessage --- */


static inline struct http_request_message *impl_from_request_closable( void *iface )
{
    return (struct http_request_message *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI req_QueryInterface( IHttpRequestMessage *This, REFIID iid, void **out )
{
    struct http_request_message *impl = impl_from_request( This );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpRequestMessage ))
    {
        *out = &impl->vtbl_request;
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
static ULONG WINAPI req_AddRef( IHttpRequestMessage *This )
{
    struct http_request_message *impl = impl_from_request( This );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI req_Release( IHttpRequestMessage *This )
{
    struct http_request_message *impl = impl_from_request( This );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->content) IUnknown_Release( (IUnknown *)impl->content );
        if (impl->headers) IUnknown_Release( (IUnknown *)impl->headers );
        if (impl->method) WindowsDeleteString( (HSTRING)impl->method );
        if (impl->uri) WindowsDeleteString( (HSTRING)impl->uri );
        if (impl->properties) IInspectable_Release( impl->properties );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI req_GetIids( IHttpRequestMessage *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI req_GetRuntimeClassName( IHttpRequestMessage *This, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpRequestMessage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI req_GetTrustLevel( IHttpRequestMessage *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI req_get_Content( IHttpRequestMessage *This, void **val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (!val) return E_POINTER;
    if (impl->content) IUnknown_AddRef( (IUnknown *)impl->content );
    *val = impl->content;
    return S_OK;
}
static HRESULT WINAPI req_set_Content( IHttpRequestMessage *This, void *val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (impl->content) IUnknown_Release( (IUnknown *)impl->content );
    impl->content = val;
    if (impl->content) IUnknown_AddRef( (IUnknown *)impl->content );
    return S_OK;
}
static HRESULT WINAPI req_get_Headers( IHttpRequestMessage *This, void **val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (!val) return E_POINTER;
    if (!impl->headers) create_http_headers( &IID_IHttpRequestHeaderCollection, &impl->headers );
    IUnknown_AddRef( (IUnknown *)impl->headers );
    *val = impl->headers;
    return S_OK;
}
static HRESULT WINAPI req_get_Method( IHttpRequestMessage *This, void **val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (!val) return E_POINTER;
    return create_http_method( (HSTRING)impl->method, val );
}
static HRESULT WINAPI req_set_Method( IHttpRequestMessage *This, void *val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (impl->method) WindowsDeleteString( (HSTRING)impl->method );
    impl->method = NULL;
    store_string_arg( val, (HSTRING *)&impl->method, L"GET" );
    return S_OK;
}
static HRESULT WINAPI req_get_Properties( IHttpRequestMessage *This, void **val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (!val) return E_POINTER;
    if (!impl->properties)
    {
        HSTRING class_str;
        IInspectable *props = NULL;
        if (SUCCEEDED( WindowsCreateString( L"Windows.Foundation.Collections.PropertySet", 42, &class_str ) ))
        {
            if (SUCCEEDED( RoActivateInstance( class_str, &props ) ) && props)
            {
                if (InterlockedCompareExchangePointer( (void **)&impl->properties, props, NULL ))
                    IInspectable_Release( props );
            }
            WindowsDeleteString( class_str );
        }
    }
    if (impl->properties) IInspectable_AddRef( impl->properties );
    *val = impl->properties;
    return S_OK;
}
static HRESULT WINAPI req_get_RequestUri( IHttpRequestMessage *This, void **val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (!val) return E_POINTER;
    return create_uri( (HSTRING)impl->uri, val );
}
static HRESULT WINAPI req_set_RequestUri( IHttpRequestMessage *This, void *val )
{
    struct http_request_message *impl = impl_from_request( This );
    if (impl->uri) WindowsDeleteString( (HSTRING)impl->uri );
    impl->uri = NULL;
    store_string_arg( val, (HSTRING *)&impl->uri, L"https://storecdn.win/" );
    return S_OK;
}
static HRESULT WINAPI req_get_TransportInformation( IHttpRequestMessage *This, void **val ) { if (val) *val = NULL; return S_OK; }

static const IHttpRequestMessageVtbl http_request_vtbl = {
    req_QueryInterface,
    req_AddRef,
    req_Release,
    req_GetIids,
    req_GetRuntimeClassName,
    req_GetTrustLevel,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, IHttpContent **))req_get_Content,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, IHttpContent *))req_set_Content,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, __x_ABI_CWindows_CWeb_CHttp_CHeaders_CIHttpRequestHeaderCollection **))req_get_Headers,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, IHttpMethod **))req_get_Method,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, IHttpMethod *))req_set_Method,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, __FIMap_2_HSTRING_IInspectable **))req_get_Properties,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, __x_ABI_CWindows_CFoundation_CIUriRuntimeClass **))req_get_RequestUri,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *))req_set_RequestUri,
    (HRESULT (STDMETHODCALLTYPE *)(IHttpRequestMessage *, IHttpTransportInformation **))req_get_TransportInformation
};

static HRESULT WINAPI req_closable_QueryInterface( IClosable *This, REFIID iid, void **out )
{
    return req_QueryInterface( (IHttpRequestMessage *)&impl_from_request_closable( This )->vtbl_request, iid, out );
}
static ULONG WINAPI req_closable_AddRef( IClosable *This )
{
    return req_AddRef( (IHttpRequestMessage *)&impl_from_request_closable( This )->vtbl_request );
}
static ULONG WINAPI req_closable_Release( IClosable *This )
{
    return req_Release( (IHttpRequestMessage *)&impl_from_request_closable( This )->vtbl_request );
}
static HRESULT WINAPI req_closable_GetIids( IClosable *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI req_closable_GetRuntimeClassName( IClosable *This, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI req_closable_GetTrustLevel( IClosable *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI req_closable_Close( IClosable *This ) { return S_OK; }

static const IClosableVtbl http_request_closable_vtbl = {
    req_closable_QueryInterface,
    req_closable_AddRef,
    req_closable_Release,
    req_closable_GetIids,
    req_closable_GetRuntimeClassName,
    req_closable_GetTrustLevel,
    req_closable_Close
};

HRESULT create_http_request( void *method, void *uri, void **out )
{
    struct http_request_message *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    WDBG( "create_http_request impl %p\n", impl );
    impl->vtbl_request = &http_request_vtbl;
    impl->vtbl_closable = &http_request_closable_vtbl;
    impl->ref = 1;
    store_string_arg( method, (HSTRING *)&impl->method, L"GET" );
    store_string_arg( uri, (HSTRING *)&impl->uri, L"https://storecdn.win/" );
    *out = &impl->vtbl_request;
    return S_OK;
}
