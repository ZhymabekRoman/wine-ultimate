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

#include <stdarg.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "activation.h"
#include "roapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

static const GUID IID_IClosable = {0x3028b027, 0x6029, 0x47e6, {0x81, 0xa1, 0x65, 0x5b, 0x3b, 0x3c, 0x52, 0x0f}};
static const GUID IID_IStringable = {0x96369f54, 0x8eb6, 0x48f0, {0xac, 0x91, 0xdd, 0x13, 0x7f, 0x61, 0xdf, 0xb7}};

static const GUID IID_IHttpClient = {0x7FDA1151, 0x3574, 0x4880, {0xA8, 0xBA, 0xE6, 0xB1, 0xE0, 0x06, 0x1F, 0x3D}};
static const GUID IID_IHttpClient2 = {0xCDD83348, 0xE8B7, 0x4CEC, {0xB1, 0xB0, 0xDC, 0x45, 0x5F, 0xE7, 0x2C, 0x92}};
static const GUID IID_IHttpClientFactory = {0xC30C4ECA, 0xE3FA, 0x4F99, {0xAF, 0xB4, 0x63, 0xCC, 0x65, 0x00, 0x94, 0x62}};

static const GUID IID_IHttpRequestMessage = {0xF5762B3C, 0x74D4, 0x4811, {0xB5, 0xDC, 0x9F, 0x8B, 0x4E, 0x2F, 0x9A, 0xBF}};
static const GUID IID_IHttpRequestMessageFactory = {0x5BAC994E, 0x3886, 0x412E, {0xAE, 0xC3, 0x52, 0xEC, 0x7F, 0x25, 0x61, 0x6F}};

static const GUID IID_IHttpResponseMessage = {0xFEE200FB, 0x8664, 0x44E0, {0x95, 0xD9, 0x42, 0x69, 0x61, 0x99, 0xBF, 0xFC}};
static const GUID IID_IHttpResponseMessageFactory = {0x52A8AF99, 0xF095, 0x43DA, {0xB6, 0x0F, 0x7C, 0xFC, 0x2B, 0xC7, 0xEA, 0x2F}};

static const GUID IID_IHttpContent = {0x6B14A441, 0xFBA7, 0x4BD2, {0xAF, 0x0A, 0x83, 0x9D, 0xE7, 0xC2, 0x95, 0xDA}};
static const GUID IID_IHttpStringContentFactory = {0x46649D5B, 0x2E93, 0x48EB, {0x8E, 0x61, 0x19, 0x67, 0x78, 0x78, 0xE5, 0x7F}};

static const GUID IID_IHttpMethod = {0x728D4022, 0x700D, 0x4FE0, {0xAF, 0xA5, 0x40, 0x29, 0x9C, 0x58, 0xDB, 0xFD}};
static const GUID IID_IHttpMethodFactory = {0x3C51D10D, 0x36D7, 0x40F8, {0xA8, 0x6D, 0xE7, 0x59, 0xCA, 0xF2, 0xF8, 0x3F}};
static const GUID IID_IHttpMethodStatics = {0x64D171F0, 0xD99A, 0x4153, {0x8D, 0xC6, 0xD6, 0x8C, 0xC4, 0xCC, 0xE3, 0x17}};

static const GUID IID_IHttpRequestHeaderCollection = {0xAF40329B, 0xB544, 0x469B, {0x86, 0xB9, 0xAC, 0x3D, 0x46, 0x6F, 0xEA, 0x36}};
static const GUID IID_IHttpResponseHeaderCollection = {0x7A990969, 0xFA3F, 0x41ED, {0xAA, 0xC6, 0xBF, 0x95, 0x79, 0x75, 0xC1, 0x6B}};
static const GUID IID_IHttpContentHeaderCollection = {0x40612A44, 0x47AE, 0x4B7E, {0x91, 0x24, 0x69, 0x62, 0x8B, 0x64, 0xAA, 0x18}};

static const GUID IID_IAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};

static HRESULT create_http_response( INT32 status_code, void **out );
static HRESULT create_http_string_content( HSTRING content, void **out );
static HRESULT create_http_headers( const GUID *collection_iid, void **out );
static HRESULT create_http_method( HSTRING method, void **out );
static HRESULT create_async_http_op( void *result_obj, HSTRING result_str, BOOL is_str, void **out );

/* --- Async Operation with Progress for HTTP --- */

struct async_http_op
{
    const void *vtbl_op;
    const void *vtbl_info;
    LONG ref;
    IInspectable *result_obj;
    HSTRING result_str;
    BOOL is_str;
};

static inline struct async_http_op *impl_from_async_op( void *iface )
{
    return (struct async_http_op *)iface;
}

static inline struct async_http_op *impl_from_async_info( void *iface )
{
    return (struct async_http_op *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI async_http_op_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct async_http_op *impl = impl_from_async_op( iface );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vtbl_op;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->vtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    /* Accept parameterized IAsyncOperation or IAsyncOperationWithProgress */
    *out = &impl->vtbl_op;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI async_http_op_AddRef( void *iface )
{
    struct async_http_op *impl = impl_from_async_op( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_http_op_Release( void *iface )
{
    struct async_http_op *impl = impl_from_async_op( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->result_obj) IInspectable_Release( impl->result_obj );
        if (impl->result_str) WindowsDeleteString( impl->result_str );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI async_http_op_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI async_http_op_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR str_name[] = L"Windows.Foundation.IAsyncOperationWithProgress`2<String, UInt64>";
    static const WCHAR resp_name[] = L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Web.Http.HttpResponseMessage, Windows.Web.Http.HttpProgress>";
    struct async_http_op *impl = impl_from_async_op( iface );
    if (!name) return E_POINTER;
    if (impl->is_str)
        return WindowsCreateString( str_name, ARRAY_SIZE(str_name) - 1, name );
    return WindowsCreateString( resp_name, ARRAY_SIZE(resp_name) - 1, name );
}
static HRESULT WINAPI async_http_op_GetTrustLevel( void *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }

static HRESULT WINAPI async_http_op_put_Progress( void *iface, void *handler ) { return S_OK; }
static HRESULT WINAPI async_http_op_get_Progress( void *iface, void **handler ) { if (handler) *handler = NULL; return S_OK; }

static HRESULT WINAPI async_http_op_put_Completed( void *iface, void *handler )
{
    if (handler)
    {
        typedef HRESULT (WINAPI *invoke_fn)( void *handler, void *async, int status );
        void **vtbl = *(void ***)handler;
        invoke_fn invoke = (invoke_fn)vtbl[3];
        invoke( handler, iface, 1 /* Completed */ );
    }
    return S_OK;
}

static HRESULT WINAPI async_http_op_get_Completed( void *iface, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}

static HRESULT WINAPI async_http_op_GetResults( void *iface, void *results )
{
    struct async_http_op *impl = impl_from_async_op( iface );

    if (!results) return E_POINTER;
    if (impl->is_str)
    {
        HSTRING *out_str = results;
        if (impl->result_str)
            return WindowsDuplicateString( impl->result_str, out_str );
        return WindowsCreateString( L"", 0, out_str );
    }
    else
    {
        IInspectable **out_obj = results;
        if (impl->result_obj)
            IInspectable_AddRef( impl->result_obj );
        *out_obj = impl->result_obj;
        return S_OK;
    }
}

static const void *async_http_op_vtbl[] =
{
    async_http_op_QueryInterface,
    async_http_op_AddRef,
    async_http_op_Release,
    async_http_op_GetIids,
    async_http_op_GetRuntimeClassName,
    async_http_op_GetTrustLevel,
    async_http_op_put_Progress,
    async_http_op_get_Progress,
    async_http_op_put_Completed,
    async_http_op_get_Completed,
    async_http_op_GetResults,
};

static HRESULT WINAPI async_http_info_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct async_http_op *impl = impl_from_async_info( iface );
    return async_http_op_QueryInterface( &impl->vtbl_op, iid, out );
}
static ULONG WINAPI async_http_info_AddRef( void *iface )
{
    struct async_http_op *impl = impl_from_async_info( iface );
    return async_http_op_AddRef( &impl->vtbl_op );
}
static ULONG WINAPI async_http_info_Release( void *iface )
{
    struct async_http_op *impl = impl_from_async_info( iface );
    return async_http_op_Release( &impl->vtbl_op );
}
static HRESULT WINAPI async_http_info_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI async_http_info_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncInfo";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI async_http_info_GetTrustLevel( void *iface, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI async_http_info_get_Id( void *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}
static HRESULT WINAPI async_http_info_get_Status( void *iface, int *status )
{
    if (!status) return E_POINTER;
    *status = 1 /* Completed */;
    return S_OK;
}
static HRESULT WINAPI async_http_info_get_ErrorCode( void *iface, HRESULT *code )
{
    if (!code) return E_POINTER;
    *code = S_OK;
    return S_OK;
}
static HRESULT WINAPI async_http_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI async_http_info_Close( void *iface ) { return S_OK; }

static const void *async_http_info_vtbl[] =
{
    async_http_info_QueryInterface,
    async_http_info_AddRef,
    async_http_info_Release,
    async_http_info_GetIids,
    async_http_info_GetRuntimeClassName,
    async_http_info_GetTrustLevel,
    async_http_info_get_Id,
    async_http_info_get_Status,
    async_http_info_get_ErrorCode,
    async_http_info_Cancel,
    async_http_info_Close,
};

static HRESULT create_async_http_op( void *result_obj, HSTRING result_str, BOOL is_str, void **out )
{
    struct async_http_op *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_op = async_http_op_vtbl;
    impl->vtbl_info = async_http_info_vtbl;
    impl->ref = 1;
    impl->is_str = is_str;
    if (is_str && result_str)
        WindowsDuplicateString( result_str, &impl->result_str );
    else if (!is_str && result_obj)
    {
        IInspectable_AddRef( (IInspectable *)result_obj );
        impl->result_obj = (IInspectable *)result_obj;
    }
    *out = &impl->vtbl_op;
    return S_OK;
}

/* --- Header Collections --- */

struct http_headers
{
    const void *vtbl_headers;
    LONG ref;
    GUID iid;
};

static HRESULT WINAPI headers_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct http_headers *impl = iface;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->iid ) ||
        IsEqualGUID( iid, &IID_IStringable ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI headers_AddRef( void *iface )
{
    struct http_headers *impl = iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI headers_Release( void *iface )
{
    struct http_headers *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}
static HRESULT WINAPI headers_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI headers_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI headers_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI headers_Append( void *iface, HSTRING name, HSTRING value )
{
    TRACE( "name %s, value %s\n", debugstr_hstring( name ), debugstr_hstring( value ) );
    return S_OK;
}
static HRESULT WINAPI headers_TryAppendWithoutValidation( void *iface, HSTRING name, HSTRING value, boolean *succeeded )
{
    TRACE( "name %s, value %s\n", debugstr_hstring( name ), debugstr_hstring( value ) );
    if (succeeded) *succeeded = TRUE;
    return S_OK;
}
static HRESULT WINAPI headers_stub_ok( void *iface ) { return S_OK; }
static HRESULT WINAPI headers_stub_null( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }

static const void *http_headers_vtbl[] =
{
    headers_QueryInterface,
    headers_AddRef,
    headers_Release,
    headers_GetIids,
    headers_GetRuntimeClassName,
    headers_GetTrustLevel,
    headers_Append,
    headers_TryAppendWithoutValidation,
    headers_stub_null, headers_stub_null, headers_stub_null, headers_stub_null,
    headers_stub_ok,   headers_stub_null, headers_stub_null, headers_stub_null,
    headers_stub_null, headers_stub_ok,   headers_stub_null, headers_stub_null,
    headers_stub_ok,   headers_stub_null, headers_stub_ok,   headers_stub_null,
    headers_stub_ok,   headers_stub_null, headers_stub_ok,   headers_stub_null,
    headers_stub_ok,   headers_stub_null, headers_stub_ok,   headers_stub_null,
    headers_stub_ok,   headers_stub_null, headers_stub_null, headers_stub_null,
    headers_stub_null, headers_stub_null, headers_stub_null, headers_stub_null,
};

static HRESULT create_http_headers( const GUID *collection_iid, void **out )
{
    struct http_headers *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_headers = http_headers_vtbl;
    impl->ref = 1;
    impl->iid = *collection_iid;
    *out = impl;
    return S_OK;
}

/* --- HttpMethod --- */

struct http_method
{
    const void *vtbl_method;
    const void *vtbl_stringable;
    LONG ref;
    HSTRING method_name;
};

static inline struct http_method *impl_from_IHttpMethod( void *iface )
{
    return (struct http_method *)iface;
}
static inline struct http_method *impl_from_method_IStringable( void *iface )
{
    return (struct http_method *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI method_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct http_method *impl = impl_from_IHttpMethod( iface );
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
static ULONG WINAPI method_AddRef( void *iface )
{
    struct http_method *impl = impl_from_IHttpMethod( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI method_Release( void *iface )
{
    struct http_method *impl = impl_from_IHttpMethod( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->method_name) WindowsDeleteString( impl->method_name );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI method_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI method_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpMethod";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI method_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI method_get_Method( void *iface, HSTRING *value )
{
    struct http_method *impl = impl_from_IHttpMethod( iface );
    if (!value) return E_POINTER;
    if (impl->method_name) return WindowsDuplicateString( impl->method_name, value );
    return WindowsCreateString( L"GET", 3, value );
}

static const void *http_method_vtbl[] =
{
    method_QueryInterface,
    method_AddRef,
    method_Release,
    method_GetIids,
    method_GetRuntimeClassName,
    method_GetTrustLevel,
    method_get_Method,
};

static HRESULT WINAPI method_str_QueryInterface( void *iface, REFIID iid, void **out )
{
    return method_QueryInterface( &impl_from_method_IStringable( iface )->vtbl_method, iid, out );
}
static ULONG WINAPI method_str_AddRef( void *iface )
{
    return method_AddRef( &impl_from_method_IStringable( iface )->vtbl_method );
}
static ULONG WINAPI method_str_Release( void *iface )
{
    return method_Release( &impl_from_method_IStringable( iface )->vtbl_method );
}
static HRESULT WINAPI method_str_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI method_str_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI method_str_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI method_str_ToString( void *iface, HSTRING *value )
{
    return method_get_Method( &impl_from_method_IStringable( iface )->vtbl_method, value );
}

static const void *http_method_stringable_vtbl[] =
{
    method_str_QueryInterface,
    method_str_AddRef,
    method_str_Release,
    method_str_GetIids,
    method_str_GetRuntimeClassName,
    method_str_GetTrustLevel,
    method_str_ToString,
};

static HRESULT create_http_method( HSTRING method, void **out )
{
    struct http_method *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_method = http_method_vtbl;
    impl->vtbl_stringable = http_method_stringable_vtbl;
    impl->ref = 1;
    if (method) WindowsDuplicateString( method, &impl->method_name );
    *out = &impl->vtbl_method;
    return S_OK;
}

/* --- HttpStringContent (IHttpContent) --- */

struct http_string_content
{
    const void *vtbl_content;
    const void *vtbl_closable;
    const void *vtbl_stringable;
    LONG ref;
    HSTRING content;
    void *headers;
};

static inline struct http_string_content *impl_from_content( void *iface )
{
    return (struct http_string_content *)iface;
}
static inline struct http_string_content *impl_from_content_closable( void *iface )
{
    return (struct http_string_content *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI content_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct http_string_content *impl = impl_from_content( iface );

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
static ULONG WINAPI content_AddRef( void *iface )
{
    struct http_string_content *impl = impl_from_content( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI content_Release( void *iface )
{
    struct http_string_content *impl = impl_from_content( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->content) WindowsDeleteString( impl->content );
        if (impl->headers) IUnknown_Release( (IUnknown *)impl->headers );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI content_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI content_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpStringContent";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI content_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI content_BufferAllAsync( void *iface, void **op )
{
    return create_async_http_op( NULL, NULL, FALSE, op );
}
static HRESULT WINAPI content_ReadAsBufferAsync( void *iface, void **op )
{
    return create_async_http_op( NULL, NULL, FALSE, op );
}
static HRESULT WINAPI content_ReadAsInputStreamAsync( void *iface, void **op )
{
    return create_async_http_op( NULL, NULL, FALSE, op );
}
static HRESULT WINAPI content_ReadAsStringAsync( void *iface, void **op )
{
    struct http_string_content *impl = impl_from_content( iface );
    return create_async_http_op( NULL, impl->content, TRUE, op );
}
static HRESULT WINAPI content_TryComputeLength( void *iface, UINT64 *length, boolean *succeeded )
{
    struct http_string_content *impl = impl_from_content( iface );
    const WCHAR *buffer;
    UINT32 len = 0;
    int utf8_len;

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
static HRESULT WINAPI content_WriteToStreamAsync( void *iface, void *stream, void **op )
{
    return create_async_http_op( NULL, NULL, FALSE, op );
}
static HRESULT WINAPI content_get_Headers( void *iface, void **headers )
{
    struct http_string_content *impl = impl_from_content( iface );
    if (!headers) return E_POINTER;
    if (!impl->headers) create_http_headers( &IID_IHttpContentHeaderCollection, &impl->headers );
    IUnknown_AddRef( (IUnknown *)impl->headers );
    *headers = impl->headers;
    return S_OK;
}

static const void *http_content_vtbl[] =
{
    content_QueryInterface,
    content_AddRef,
    content_Release,
    content_GetIids,
    content_GetRuntimeClassName,
    content_GetTrustLevel,
    content_BufferAllAsync,
    content_ReadAsBufferAsync,
    content_ReadAsInputStreamAsync,
    content_ReadAsStringAsync,
    content_TryComputeLength,
    content_WriteToStreamAsync,
    content_get_Headers,
};

static HRESULT WINAPI content_closable_QueryInterface( void *iface, REFIID iid, void **out )
{
    return content_QueryInterface( &impl_from_content_closable( iface )->vtbl_content, iid, out );
}
static ULONG WINAPI content_closable_AddRef( void *iface )
{
    return content_AddRef( &impl_from_content_closable( iface )->vtbl_content );
}
static ULONG WINAPI content_closable_Release( void *iface )
{
    return content_Release( &impl_from_content_closable( iface )->vtbl_content );
}
static HRESULT WINAPI content_closable_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI content_closable_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI content_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI content_closable_Close( void *iface ) { return S_OK; }

static const void *http_content_closable_vtbl[] =
{
    content_closable_QueryInterface,
    content_closable_AddRef,
    content_closable_Release,
    content_closable_GetIids,
    content_closable_GetRuntimeClassName,
    content_closable_GetTrustLevel,
    content_closable_Close,
};

static HRESULT WINAPI content_str_ToString( void *iface, HSTRING *val )
{
    struct http_string_content *impl = (struct http_string_content *)((BYTE *)iface - 2 * sizeof(void *));
    if (!val) return E_POINTER;
    if (impl->content) return WindowsDuplicateString( impl->content, val );
    return WindowsCreateString( L"", 0, val );
}

static inline struct http_string_content *impl_from_content_stringable( void *iface )
{
    return (struct http_string_content *)((BYTE *)iface - 2 * sizeof(void *));
}

static HRESULT WINAPI content_str_QueryInterface( void *iface, REFIID iid, void **out )
{
    return content_QueryInterface( &impl_from_content_stringable( iface )->vtbl_content, iid, out );
}
static ULONG WINAPI content_str_AddRef( void *iface )
{
    return content_AddRef( &impl_from_content_stringable( iface )->vtbl_content );
}
static ULONG WINAPI content_str_Release( void *iface )
{
    return content_Release( &impl_from_content_stringable( iface )->vtbl_content );
}

static const void *http_content_stringable_vtbl[] =
{
    content_str_QueryInterface,
    content_str_AddRef,
    content_str_Release,
    content_closable_GetIids,
    content_closable_GetRuntimeClassName,
    content_closable_GetTrustLevel,
    content_str_ToString,
};

static HRESULT create_http_string_content( HSTRING content, void **out )
{
    struct http_string_content *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_content = http_content_vtbl;
    impl->vtbl_closable = http_content_closable_vtbl;
    impl->vtbl_stringable = http_content_stringable_vtbl;
    impl->ref = 1;
    if (content) WindowsDuplicateString( content, &impl->content );
    *out = &impl->vtbl_content;
    return S_OK;
}

/* --- HttpResponseMessage --- */

struct http_response_message
{
    const void *vtbl_response;
    const void *vtbl_closable;
    LONG ref;
    INT32 status_code;
    HSTRING reason_phrase;
    void *content;
    void *headers;
    void *request_message;
    INT32 source;
    INT32 version;
};

static inline struct http_response_message *impl_from_response( void *iface )
{
    return (struct http_response_message *)iface;
}
static inline struct http_response_message *impl_from_response_closable( void *iface )
{
    return (struct http_response_message *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI resp_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct http_response_message *impl = impl_from_response( iface );
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
static ULONG WINAPI resp_AddRef( void *iface )
{
    struct http_response_message *impl = impl_from_response( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI resp_Release( void *iface )
{
    struct http_response_message *impl = impl_from_response( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
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
static HRESULT WINAPI resp_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI resp_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpResponseMessage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI resp_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI resp_EnsureSuccessStatusCode( void *iface, void **value )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (impl->status_code < 200 || impl->status_code >= 300) return E_FAIL;
    if (value)
    {
        *value = iface;
        resp_AddRef( iface );
    }
    return S_OK;
}
static HRESULT WINAPI resp_get_Content( void *iface, void **content )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (!content) return E_POINTER;
    if (!impl->content)
    {
        HSTRING dummy_html;
        WindowsCreateString( L"<html><head><title>WhatsApp</title></head><body></body></html>", 60, &dummy_html );
        create_http_string_content( dummy_html, &impl->content );
        WindowsDeleteString( dummy_html );
    }
    IUnknown_AddRef( (IUnknown *)impl->content );
    *content = impl->content;
    return S_OK;
}
static HRESULT WINAPI resp_set_Content( void *iface, void *content )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (impl->content) IUnknown_Release( (IUnknown *)impl->content );
    impl->content = content;
    if (impl->content) IUnknown_AddRef( (IUnknown *)impl->content );
    return S_OK;
}
static HRESULT WINAPI resp_get_Headers( void *iface, void **headers )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (!headers) return E_POINTER;
    if (!impl->headers) create_http_headers( &IID_IHttpResponseHeaderCollection, &impl->headers );
    IUnknown_AddRef( (IUnknown *)impl->headers );
    *headers = impl->headers;
    return S_OK;
}
static HRESULT WINAPI resp_get_IsSuccessStatusCode( void *iface, boolean *val )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (val) *val = (impl->status_code >= 200 && impl->status_code < 300);
    return S_OK;
}
static HRESULT WINAPI resp_get_ReasonPhrase( void *iface, HSTRING *val )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (!val) return E_POINTER;
    if (impl->reason_phrase) return WindowsDuplicateString( impl->reason_phrase, val );
    return WindowsCreateString( L"OK", 2, val );
}
static HRESULT WINAPI resp_set_ReasonPhrase( void *iface, HSTRING val )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (impl->reason_phrase) WindowsDeleteString( impl->reason_phrase );
    impl->reason_phrase = NULL;
    if (val) WindowsDuplicateString( val, &impl->reason_phrase );
    return S_OK;
}
static HRESULT WINAPI resp_get_RequestMessage( void *iface, void **req )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (!req) return E_POINTER;
    if (impl->request_message) IUnknown_AddRef( (IUnknown *)impl->request_message );
    *req = impl->request_message;
    return S_OK;
}
static HRESULT WINAPI resp_set_RequestMessage( void *iface, void *req )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (impl->request_message) IUnknown_Release( (IUnknown *)impl->request_message );
    impl->request_message = req;
    if (impl->request_message) IUnknown_AddRef( (IUnknown *)impl->request_message );
    return S_OK;
}
static HRESULT WINAPI resp_get_Source( void *iface, INT32 *src ) { if (src) *src = 2 /* Network */; return S_OK; }
static HRESULT WINAPI resp_set_Source( void *iface, INT32 src ) { return S_OK; }
static HRESULT WINAPI resp_get_StatusCode( void *iface, INT32 *code )
{
    struct http_response_message *impl = impl_from_response( iface );
    if (code) *code = impl->status_code;
    return S_OK;
}
static HRESULT WINAPI resp_set_StatusCode( void *iface, INT32 code )
{
    struct http_response_message *impl = impl_from_response( iface );
    impl->status_code = code;
    return S_OK;
}
static HRESULT WINAPI resp_get_Version( void *iface, INT32 *ver ) { if (ver) *ver = 1 /* Http11 */; return S_OK; }
static HRESULT WINAPI resp_set_Version( void *iface, INT32 ver ) { return S_OK; }

static const void *http_response_vtbl[] =
{
    resp_QueryInterface,
    resp_AddRef,
    resp_Release,
    resp_GetIids,
    resp_GetRuntimeClassName,
    resp_GetTrustLevel,
    resp_EnsureSuccessStatusCode,
    resp_get_Content,
    resp_set_Content,
    resp_get_Headers,
    resp_get_IsSuccessStatusCode,
    resp_get_ReasonPhrase,
    resp_set_ReasonPhrase,
    resp_get_RequestMessage,
    resp_set_RequestMessage,
    resp_get_Source,
    resp_set_Source,
    resp_get_StatusCode,
    resp_set_StatusCode,
    resp_get_Version,
    resp_set_Version,
};

static HRESULT WINAPI resp_closable_QueryInterface( void *iface, REFIID iid, void **out )
{
    return resp_QueryInterface( &impl_from_response_closable( iface )->vtbl_response, iid, out );
}
static ULONG WINAPI resp_closable_AddRef( void *iface )
{
    return resp_AddRef( &impl_from_response_closable( iface )->vtbl_response );
}
static ULONG WINAPI resp_closable_Release( void *iface )
{
    return resp_Release( &impl_from_response_closable( iface )->vtbl_response );
}
static HRESULT WINAPI resp_closable_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI resp_closable_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI resp_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI resp_closable_Close( void *iface ) { return S_OK; }

static const void *http_response_closable_vtbl[] =
{
    resp_closable_QueryInterface,
    resp_closable_AddRef,
    resp_closable_Release,
    resp_closable_GetIids,
    resp_closable_GetRuntimeClassName,
    resp_closable_GetTrustLevel,
    resp_closable_Close,
};

static HRESULT create_http_response( INT32 status_code, void **out )
{
    struct http_response_message *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_response = http_response_vtbl;
    impl->vtbl_closable = http_response_closable_vtbl;
    impl->ref = 1;
    impl->status_code = status_code ? status_code : 200;
    impl->source = 2;
    impl->version = 1;
    *out = &impl->vtbl_response;
    return S_OK;
}

/* --- HttpRequestMessage --- */

struct http_request_message
{
    const void *vtbl_request;
    const void *vtbl_closable;
    LONG ref;
    void *content;
    void *headers;
    void *method;
    void *uri;
};

static inline struct http_request_message *impl_from_request( void *iface )
{
    return (struct http_request_message *)iface;
}
static inline struct http_request_message *impl_from_request_closable( void *iface )
{
    return (struct http_request_message *)((BYTE *)iface - sizeof(void *));
}

static HRESULT WINAPI req_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct http_request_message *impl = impl_from_request( iface );
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
static ULONG WINAPI req_AddRef( void *iface )
{
    struct http_request_message *impl = impl_from_request( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI req_Release( void *iface )
{
    struct http_request_message *impl = impl_from_request( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->content) IUnknown_Release( (IUnknown *)impl->content );
        if (impl->headers) IUnknown_Release( (IUnknown *)impl->headers );
        if (impl->method) IUnknown_Release( (IUnknown *)impl->method );
        if (impl->uri) IUnknown_Release( (IUnknown *)impl->uri );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI req_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI req_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpRequestMessage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI req_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI req_get_Content( void *iface, void **val )
{
    struct http_request_message *impl = impl_from_request( iface );
    if (!val) return E_POINTER;
    if (impl->content) IUnknown_AddRef( (IUnknown *)impl->content );
    *val = impl->content;
    return S_OK;
}
static HRESULT WINAPI req_set_Content( void *iface, void *val )
{
    struct http_request_message *impl = impl_from_request( iface );
    if (impl->content) IUnknown_Release( (IUnknown *)impl->content );
    impl->content = val;
    if (impl->content) IUnknown_AddRef( (IUnknown *)impl->content );
    return S_OK;
}
static HRESULT WINAPI req_get_Headers( void *iface, void **val )
{
    struct http_request_message *impl = impl_from_request( iface );
    if (!val) return E_POINTER;
    if (!impl->headers) create_http_headers( &IID_IHttpRequestHeaderCollection, &impl->headers );
    IUnknown_AddRef( (IUnknown *)impl->headers );
    *val = impl->headers;
    return S_OK;
}
static HRESULT WINAPI req_get_Method( void *iface, void **val )
{
    struct http_request_message *impl = impl_from_request( iface );
    if (!val) return E_POINTER;
    if (!impl->method) create_http_method( NULL, &impl->method );
    IUnknown_AddRef( (IUnknown *)impl->method );
    *val = impl->method;
    return S_OK;
}
static HRESULT WINAPI req_set_Method( void *iface, void *val )
{
    struct http_request_message *impl = impl_from_request( iface );
    if (impl->method) IUnknown_Release( (IUnknown *)impl->method );
    impl->method = val;
    if (impl->method) IUnknown_AddRef( (IUnknown *)impl->method );
    return S_OK;
}
static HRESULT WINAPI req_get_Properties( void *iface, void **val ) { if (val) *val = NULL; return S_OK; }
static HRESULT WINAPI req_get_RequestUri( void *iface, void **val )
{
    struct http_request_message *impl = impl_from_request( iface );
    if (!val) return E_POINTER;
    if (impl->uri) IUnknown_AddRef( (IUnknown *)impl->uri );
    *val = impl->uri;
    return S_OK;
}
static HRESULT WINAPI req_set_RequestUri( void *iface, void *val )
{
    struct http_request_message *impl = impl_from_request( iface );
    if (impl->uri) IUnknown_Release( (IUnknown *)impl->uri );
    impl->uri = val;
    if (impl->uri) IUnknown_AddRef( (IUnknown *)impl->uri );
    return S_OK;
}
static HRESULT WINAPI req_get_TransportInformation( void *iface, void **val ) { if (val) *val = NULL; return S_OK; }

static const void *http_request_vtbl[] =
{
    req_QueryInterface,
    req_AddRef,
    req_Release,
    req_GetIids,
    req_GetRuntimeClassName,
    req_GetTrustLevel,
    req_get_Content,
    req_set_Content,
    req_get_Headers,
    req_get_Method,
    req_set_Method,
    req_get_Properties,
    req_get_RequestUri,
    req_set_RequestUri,
    req_get_TransportInformation,
};

static HRESULT WINAPI req_closable_QueryInterface( void *iface, REFIID iid, void **out )
{
    return req_QueryInterface( &impl_from_request_closable( iface )->vtbl_request, iid, out );
}
static ULONG WINAPI req_closable_AddRef( void *iface )
{
    return req_AddRef( &impl_from_request_closable( iface )->vtbl_request );
}
static ULONG WINAPI req_closable_Release( void *iface )
{
    return req_Release( &impl_from_request_closable( iface )->vtbl_request );
}
static HRESULT WINAPI req_closable_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI req_closable_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI req_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI req_closable_Close( void *iface ) { return S_OK; }

static const void *http_request_closable_vtbl[] =
{
    req_closable_QueryInterface,
    req_closable_AddRef,
    req_closable_Release,
    req_closable_GetIids,
    req_closable_GetRuntimeClassName,
    req_closable_GetTrustLevel,
    req_closable_Close,
};

static HRESULT create_http_request( void *method, void *uri, void **out )
{
    struct http_request_message *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_request = http_request_vtbl;
    impl->vtbl_closable = http_request_closable_vtbl;
    impl->ref = 1;
    if (method)
    {
        impl->method = method;
        IUnknown_AddRef( (IUnknown *)impl->method );
    }
    if (uri)
    {
        impl->uri = uri;
        IUnknown_AddRef( (IUnknown *)impl->uri );
    }
    *out = &impl->vtbl_request;
    return S_OK;
}

/* --- HttpClient --- */

struct http_client
{
    const void *vtbl_client;
    const void *vtbl_client2;
    const void *vtbl_closable;
    LONG ref;
    void *default_headers;
};

static inline struct http_client *impl_from_client( void *iface )
{
    return (struct http_client *)iface;
}
static inline struct http_client *impl_from_client2( void *iface )
{
    return (struct http_client *)((BYTE *)iface - sizeof(void *));
}
static inline struct http_client *impl_from_client_closable( void *iface )
{
    return (struct http_client *)((BYTE *)iface - 2 * sizeof(void *));
}

static HRESULT WINAPI client_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct http_client *impl = impl_from_client( iface );
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
static ULONG WINAPI client_AddRef( void *iface )
{
    struct http_client *impl = impl_from_client( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI client_Release( void *iface )
{
    struct http_client *impl = impl_from_client( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->default_headers) IUnknown_Release( (IUnknown *)impl->default_headers );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI client_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI client_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpClient";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI client_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI client_DeleteAsync( void *iface, void *uri, void **op )
{
    void *resp = NULL;
    HRESULT hr = create_http_response( 200, &resp );
    if (FAILED(hr)) return hr;
    hr = create_async_http_op( resp, NULL, FALSE, op );
    IUnknown_Release( (IUnknown *)resp );
    return hr;
}
static HRESULT WINAPI client_GetAsync( void *iface, void *uri, void **op )
{
    void *resp = NULL;
    HRESULT hr = create_http_response( 200, &resp );
    if (FAILED(hr)) return hr;
    hr = create_async_http_op( resp, NULL, FALSE, op );
    IUnknown_Release( (IUnknown *)resp );
    return hr;
}
static HRESULT WINAPI client_GetAsync2( void *iface, void *uri, INT32 opt, void **op )
{
    return client_GetAsync( iface, uri, op );
}
static HRESULT WINAPI client_GetBufferAsync( void *iface, void *uri, void **op )
{
    return create_async_http_op( NULL, NULL, FALSE, op );
}
static HRESULT WINAPI client_GetInputStreamAsync( void *iface, void *uri, void **op )
{
    return create_async_http_op( NULL, NULL, FALSE, op );
}
static HRESULT WINAPI client_GetStringAsync( void *iface, void *uri, void **op )
{
    HSTRING str;
    HRESULT hr = WindowsCreateString( L"", 0, &str );
    if (FAILED(hr)) return hr;
    hr = create_async_http_op( NULL, str, TRUE, op );
    WindowsDeleteString( str );
    return hr;
}
static HRESULT WINAPI client_PostAsync( void *iface, void *uri, void *content, void **op )
{
    void *resp = NULL;
    HRESULT hr = create_http_response( 200, &resp );
    if (FAILED(hr)) return hr;
    hr = create_async_http_op( resp, NULL, FALSE, op );
    IUnknown_Release( (IUnknown *)resp );
    return hr;
}
static HRESULT WINAPI client_PutAsync( void *iface, void *uri, void *content, void **op )
{
    return client_PostAsync( iface, uri, content, op );
}
static HRESULT WINAPI client_SendRequestAsync( void *iface, void *req, void **op )
{
    void *resp = NULL;
    HRESULT hr = create_http_response( 200, &resp );
    if (FAILED(hr)) return hr;
    hr = create_async_http_op( resp, NULL, FALSE, op );
    IUnknown_Release( (IUnknown *)resp );
    return hr;
}
static HRESULT WINAPI client_SendRequestAsync2( void *iface, void *req, INT32 opt, void **op )
{
    return client_SendRequestAsync( iface, req, op );
}
static HRESULT WINAPI client_get_DefaultRequestHeaders( void *iface, void **headers )
{
    struct http_client *impl = impl_from_client( iface );
    if (!headers) return E_POINTER;
    if (!impl->default_headers) create_http_headers( &IID_IHttpRequestHeaderCollection, &impl->default_headers );
    IUnknown_AddRef( (IUnknown *)impl->default_headers );
    *headers = impl->default_headers;
    return S_OK;
}

static const void *http_client_vtbl[] =
{
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
    client_get_DefaultRequestHeaders,
};

static HRESULT WINAPI client2_QueryInterface( void *iface, REFIID iid, void **out )
{
    return client_QueryInterface( &impl_from_client2( iface )->vtbl_client, iid, out );
}
static ULONG WINAPI client2_AddRef( void *iface )
{
    return client_AddRef( &impl_from_client2( iface )->vtbl_client );
}
static ULONG WINAPI client2_Release( void *iface )
{
    return client_Release( &impl_from_client2( iface )->vtbl_client );
}
static HRESULT WINAPI client2_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI client2_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI client2_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

static HRESULT WINAPI client2_TryDeleteAsync( void *iface, void *uri, void **op ) { return client_DeleteAsync( iface, uri, op ); }
static HRESULT WINAPI client2_TryGetAsync( void *iface, void *uri, void **op ) { return client_GetAsync( iface, uri, op ); }
static HRESULT WINAPI client2_TryGetAsync2( void *iface, void *uri, INT32 opt, void **op ) { return client_GetAsync2( iface, uri, opt, op ); }
static HRESULT WINAPI client2_TryGetBufferAsync( void *iface, void *uri, void **op ) { return client_GetBufferAsync( iface, uri, op ); }
static HRESULT WINAPI client2_TryGetInputStreamAsync( void *iface, void *uri, void **op ) { return client_GetInputStreamAsync( iface, uri, op ); }
static HRESULT WINAPI client2_TryGetStringAsync( void *iface, void *uri, void **op ) { return client_GetStringAsync( iface, uri, op ); }
static HRESULT WINAPI client2_TryPostAsync( void *iface, void *uri, void *content, void **op ) { return client_PostAsync( iface, uri, content, op ); }
static HRESULT WINAPI client2_TryPutAsync( void *iface, void *uri, void *content, void **op ) { return client_PutAsync( iface, uri, content, op ); }
static HRESULT WINAPI client2_TrySendRequestAsync( void *iface, void *req, void **op ) { return client_SendRequestAsync( iface, req, op ); }
static HRESULT WINAPI client2_TrySendRequestAsync2( void *iface, void *req, INT32 opt, void **op ) { return client_SendRequestAsync2( iface, req, opt, op ); }

static const void *http_client2_vtbl[] =
{
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
    client2_TrySendRequestAsync2,
};

static HRESULT WINAPI client_closable_QueryInterface( void *iface, REFIID iid, void **out )
{
    return client_QueryInterface( &impl_from_client_closable( iface )->vtbl_client, iid, out );
}
static ULONG WINAPI client_closable_AddRef( void *iface )
{
    return client_AddRef( &impl_from_client_closable( iface )->vtbl_client );
}
static ULONG WINAPI client_closable_Release( void *iface )
{
    return client_Release( &impl_from_client_closable( iface )->vtbl_client );
}
static HRESULT WINAPI client_closable_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI client_closable_GetRuntimeClassName( void *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI client_closable_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI client_closable_Close( void *iface ) { return S_OK; }

static const void *http_client_closable_vtbl[] =
{
    client_closable_QueryInterface,
    client_closable_AddRef,
    client_closable_Release,
    client_closable_GetIids,
    client_closable_GetRuntimeClassName,
    client_closable_GetTrustLevel,
    client_closable_Close,
};

static HRESULT create_http_client( void **out )
{
    struct http_client *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_client = http_client_vtbl;
    impl->vtbl_client2 = http_client2_vtbl;
    impl->vtbl_closable = http_client_closable_vtbl;
    impl->ref = 1;
    *out = &impl->vtbl_client;
    return S_OK;
}

/* --- Activation Factories --- */

/* HttpClient factory */
static HRESULT WINAPI http_client_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpClientFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_client_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI http_client_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI http_client_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_client_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpClient";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_client_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_client_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_http_client( (void **)instance );
}
static HRESULT WINAPI http_client_factory_Create( void *iface, void *filter, void **instance )
{
    return create_http_client( instance );
}

static const void *http_client_factory_vtbl[] =
{
    http_client_factory_QueryInterface,
    http_client_factory_AddRef,
    http_client_factory_Release,
    http_client_factory_GetIids,
    http_client_factory_GetRuntimeClassName,
    http_client_factory_GetTrustLevel,
    http_client_factory_ActivateInstance,
    http_client_factory_Create,
};
static const void *http_client_factory_obj = http_client_factory_vtbl;
IActivationFactory *http_client_factory = (IActivationFactory *)&http_client_factory_obj;

/* HttpRequestMessage factory */
static HRESULT WINAPI http_request_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpRequestMessageFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_request_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI http_request_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI http_request_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_request_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpRequestMessage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_request_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_request_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_http_request( NULL, NULL, (void **)instance );
}
static HRESULT WINAPI http_request_factory_Create( void *iface, void *method, void *uri, void **instance )
{
    return create_http_request( method, uri, instance );
}

static const void *http_request_factory_vtbl[] =
{
    http_request_factory_QueryInterface,
    http_request_factory_AddRef,
    http_request_factory_Release,
    http_request_factory_GetIids,
    http_request_factory_GetRuntimeClassName,
    http_request_factory_GetTrustLevel,
    http_request_factory_ActivateInstance,
    http_request_factory_Create,
};
static const void *http_request_factory_obj = http_request_factory_vtbl;
IActivationFactory *http_request_factory = (IActivationFactory *)&http_request_factory_obj;

/* HttpResponseMessage factory */
static HRESULT WINAPI http_response_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpResponseMessageFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_response_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI http_response_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI http_response_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_response_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpResponseMessage";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_response_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_response_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_http_response( 200, (void **)instance );
}
static HRESULT WINAPI http_response_factory_Create( void *iface, INT32 status_code, void **instance )
{
    return create_http_response( status_code, instance );
}

static const void *http_response_factory_vtbl[] =
{
    http_response_factory_QueryInterface,
    http_response_factory_AddRef,
    http_response_factory_Release,
    http_response_factory_GetIids,
    http_response_factory_GetRuntimeClassName,
    http_response_factory_GetTrustLevel,
    http_response_factory_ActivateInstance,
    http_response_factory_Create,
};
static const void *http_response_factory_obj = http_response_factory_vtbl;
IActivationFactory *http_response_factory = (IActivationFactory *)&http_response_factory_obj;

/* HttpStringContent factory */
static HRESULT WINAPI http_string_content_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpStringContentFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_string_content_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI http_string_content_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI http_string_content_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_string_content_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpStringContent";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_string_content_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_string_content_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_http_string_content( NULL, (void **)instance );
}
static HRESULT WINAPI http_string_content_factory_CreateFromString( void *iface, HSTRING content, void **instance )
{
    return create_http_string_content( content, instance );
}
static HRESULT WINAPI http_string_content_factory_CreateFromStringWithEncoding( void *iface, HSTRING content, INT32 encoding, void **instance )
{
    return create_http_string_content( content, instance );
}
static HRESULT WINAPI http_string_content_factory_CreateFromStringWithEncodingAndMediaType( void *iface, HSTRING content, INT32 encoding, HSTRING media_type, void **instance )
{
    return create_http_string_content( content, instance );
}

static const void *http_string_content_factory_vtbl[] =
{
    http_string_content_factory_QueryInterface,
    http_string_content_factory_AddRef,
    http_string_content_factory_Release,
    http_string_content_factory_GetIids,
    http_string_content_factory_GetRuntimeClassName,
    http_string_content_factory_GetTrustLevel,
    http_string_content_factory_ActivateInstance,
    http_string_content_factory_CreateFromString,
    http_string_content_factory_CreateFromStringWithEncoding,
    http_string_content_factory_CreateFromStringWithEncodingAndMediaType,
};
static const void *http_string_content_factory_obj = http_string_content_factory_vtbl;
IActivationFactory *http_string_content_factory = (IActivationFactory *)&http_string_content_factory_obj;

/* HttpMethod factory */
static HRESULT WINAPI http_method_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IHttpMethodFactory ) ||
        IsEqualGUID( iid, &IID_IHttpMethodStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI http_method_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI http_method_factory_Release( void *iface ) { return 1; }
static HRESULT WINAPI http_method_factory_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI http_method_factory_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR class_name[] = L"Windows.Web.Http.HttpMethod";
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, n );
}
static HRESULT WINAPI http_method_factory_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI http_method_factory_ActivateInstance( void *iface, IInspectable **instance )
{
    return create_http_method( NULL, (void **)instance );
}
static HRESULT WINAPI http_method_factory_Create( void *iface, HSTRING method, void **instance )
{
    return create_http_method( method, instance );
}

static HRESULT WINAPI http_method_factory_get_named( const WCHAR *name, void **val )
{
    HSTRING str;
    HRESULT hr = WindowsCreateString( name, wcslen( name ), &str );
    if (FAILED(hr)) return hr;
    hr = create_http_method( str, val );
    WindowsDeleteString( str );
    return hr;
}

static HRESULT WINAPI http_method_factory_get_Delete( void *iface, void **val ) { return http_method_factory_get_named( L"DELETE", val ); }
static HRESULT WINAPI http_method_factory_get_Get( void *iface, void **val ) { return http_method_factory_get_named( L"GET", val ); }
static HRESULT WINAPI http_method_factory_get_Head( void *iface, void **val ) { return http_method_factory_get_named( L"HEAD", val ); }
static HRESULT WINAPI http_method_factory_get_Options( void *iface, void **val ) { return http_method_factory_get_named( L"OPTIONS", val ); }
static HRESULT WINAPI http_method_factory_get_Patch( void *iface, void **val ) { return http_method_factory_get_named( L"PATCH", val ); }
static HRESULT WINAPI http_method_factory_get_Post( void *iface, void **val ) { return http_method_factory_get_named( L"POST", val ); }
static HRESULT WINAPI http_method_factory_get_Put( void *iface, void **val ) { return http_method_factory_get_named( L"PUT", val ); }

static const void *http_method_factory_vtbl[] =
{
    http_method_factory_QueryInterface,
    http_method_factory_AddRef,
    http_method_factory_Release,
    http_method_factory_GetIids,
    http_method_factory_GetRuntimeClassName,
    http_method_factory_GetTrustLevel,
    http_method_factory_ActivateInstance,
    http_method_factory_Create,
    http_method_factory_get_Delete,
    http_method_factory_get_Get,
    http_method_factory_get_Head,
    http_method_factory_get_Options,
    http_method_factory_get_Patch,
    http_method_factory_get_Post,
    http_method_factory_get_Put,
};
static const void *http_method_factory_obj = http_method_factory_vtbl;
IActivationFactory *http_method_factory = (IActivationFactory *)&http_method_factory_obj;
