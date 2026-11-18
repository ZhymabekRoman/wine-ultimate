/* WinRT Windows.Web.Http Implementation — shared declarations
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

#ifndef __WINE_WINDOWS_WEB_HTTP_PRIVATE_H
#define __WINE_WINDOWS_WEB_HTTP_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "activation.h"
#include "roapi.h"
#include "wine/debug.h"
#include "wine/exception.h"
#include "winhttp.h"

#define WDBG(...) TRACE(__VA_ARGS__)

WINE_DEFAULT_DEBUG_CHANNEL(web);

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Web_Http
#define WIDL_using_Windows_Web_Http_Headers
#define WIDL_using_Windows_Web_Http_Filters
#include "windows.web.http.h"

/* streams.c */
HRESULT create_mem_buffer_from_hstring( HSTRING str, void **out );
HRESULT create_mem_buffer_from_bytes( const void *data, unsigned long len, void **out );
HRESULT create_empty_mem_buffer( unsigned long cap, void **out );
HRESULT create_async_http_op_obj( void *result_obj, void **out );
HRESULT create_ras_stream_from_buffer( void *buffer, void **out );

/* IReferenceTracker is not covered by the generated headers. */
static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};

/* constructors shared across the http_*.c files */
struct async_http_op;
HRESULT create_async_http_op_ex( void *result_obj, HSTRING result_str, BOOL is_str,
                                 BOOL handler_at_0x10, void **out );
HRESULT create_async_http_op( void *result_obj, HSTRING result_str, BOOL is_str, void **out );
HRESULT create_http_response( INT32 status_code, void **out );
HRESULT create_http_request( void *method, void *uri, void **out );
HRESULT create_http_string_content( HSTRING content, void **out );
HRESULT create_http_stream_content( void *stream, void **out );
HRESULT create_http_method( HSTRING method, void **out );
HRESULT create_http_headers( const GUID *collection_iid, void **out );
HRESULT create_uri( HSTRING uri, void **out );
HRESULT create_http_client( void **out );

/* accessors used by the winhttp fetch path (http_client.c) */
HRESULT resp_get_Headers( void *iface, void **headers );
HRESULT resp_set_Content( void *iface, void *content );
HRESULT content_get_Headers( void *iface, void **headers );
HRESULT headers_Append( IHttpHeaderCollection *iface, HSTRING name, HSTRING value );
void http_string_content_set_raw( void *iface, BYTE *data, ULONG len );
const struct hdr_pair *http_headers_pairs( void *iface, UINT32 *count );

/* cheap shape check for combase hstring_header: {flags, len, pad, pad, WCHAR *str} */
static inline BOOL is_hstring( const void *p )
{
    struct hshape { UINT32 flags, len, pad1, pad2; const WCHAR *str; } *h = (void *)p;
    return p && !(h->flags & ~1u) && h->len < 0x100000 && h->str;
}

/* Accepts either a raw HSTRING handle or a Windows.Foundation.Uri-like object
 * with a get_String accessor at slot 6; stores a private copy in *dst. */
static inline HRESULT store_string_arg( void *arg, HSTRING *dst, const WCHAR *fallback )
{
    HRESULT hr;
    BOOL ok = FALSE;
    WDBG( "store_string_arg arg %p dst %p fb %s\n", arg, dst, debugstr_w( fallback ) );
    *dst = NULL;
    if (!arg) goto fallback;
    __TRY
    {
        if (is_hstring( arg ))
        {
            if (SUCCEEDED( WindowsDuplicateString( (HSTRING)arg, dst ) ) && *dst) ok = TRUE;
        }
        else
        {
            void **vtbl = *(void ***)arg;
            HRESULT (STDMETHODCALLTYPE *get_string)( void *, HSTRING * ) = (void *)vtbl[6];
            HSTRING s = NULL;
            hr = get_string( arg, &s );
            if (SUCCEEDED( hr ) && s)
            {
                WindowsDuplicateString( s, dst );
                WindowsDeleteString( s );
                if (*dst) ok = TRUE;
            }
        }
    }
    __EXCEPT_ALL { ok = FALSE; }
    __ENDTRY
    if (ok) return S_OK;
fallback:
    return WindowsCreateString( fallback, lstrlenW( fallback ), dst );
}

/* ---------------------------------------------------------------------
 * Implementation structures (one object per WinRT runtimeclass).
 * Multi-face objects keep their vtable pointers as the leading struct
 * members; secondary faces are located at fixed member offsets.
 * ------------------------------------------------------------------- */

/* IAsyncOperationWithProgress`2 merged projection face (Win11 22621):
 * slots 0-10 carry the IAsyncInfo shape, 11-39 the projection region
 * (NativeAOT GetResults at 11, WPM CLog::Trace at 39, IBuffer-length
 * GetResults at 21). */
struct async_http_op
{
    const struct async_op_vtbl *vtbl_op;
    const IAsyncInfoVtbl *vtbl_info;
    LONG ref;
    IInspectable *result_obj;
    HSTRING result_str;
    BOOL is_str;
    BOOL handler_at_0x10; /* WPM awaiter delegate keeps its vtbl at +0x10 */
};

struct hdr_pair
{
    HSTRING k;
    HSTRING v;
};

struct http_headers
{
    const struct http_headers_vtbl *vtbl_headers;
    const struct http_headers_map_vtbl *vtbl_map;
    LONG ref;
    GUID iid;
    struct hdr_pair *pairs;
    UINT32 count;
    UINT32 cap;
    HSTRING last_name;   /* most recently appended name (per-instance) */
    HSTRING last_value;  /* most recently appended value (per-instance) */
};

struct hdr_kvp
{
    const IKeyValuePair_HSTRING_HSTRINGVtbl *vtbl;
    LONG ref;
    HSTRING k;
    HSTRING v;
};

struct hdr_iter
{
    const struct hdr_iter_vtbl *vtbl;
    LONG ref;
    struct http_headers *owner;
    UINT32 idx;
};

struct http_method
{
    const IHttpMethodVtbl *vtbl_method;
    const IStringableVtbl *vtbl_stringable;
    LONG ref;
    HSTRING method_name;
};

struct http_string_content
{
    const struct http_content_vtbl *vtbl_content;
    const IClosableVtbl *vtbl_closable;
    const IStringableVtbl *vtbl_stringable;
    LONG ref;
    HSTRING content;
    void *headers;
    BYTE *raw_bytes;
    ULONG raw_len;
};

struct http_stream_content
{
    const struct http_content_vtbl *vtbl_content;
    const IClosableVtbl *vtbl_closable;
    const void *reserved;
    LONG ref;
    void *stream;   /* IInspectable* over IRandomAccessStream(WithContentType) */
    void *headers;
    BYTE *cached;
    ULONG cached_len;
};

struct http_response_message
{
    const struct http_response_vtbl *vtbl_response;
    const IClosableVtbl *vtbl_closable;
    LONG ref;
    INT32 status_code;
    HSTRING reason_phrase;
    void *content;
    void *headers;
    void *request_message;
    INT32 source;
    INT32 version;
};

struct uri_obj
{
    const struct uri_vtbl *vtbl;
    LONG ref;
    HSTRING uri;
};

struct http_request_message
{
    const IHttpRequestMessageVtbl *vtbl_request;
    const IClosableVtbl *vtbl_closable;
    LONG ref;
    void *content;
    void *headers;
    void *method;   /* HSTRING */
    void *uri;      /* HSTRING */
    IInspectable *properties;
};

struct http_client
{
    const IHttpClientVtbl *vtbl_client;
    const IHttpClient2Vtbl *vtbl_client2;
    const IClosableVtbl *vtbl_closable;
    LONG ref;
    void *default_headers;
};

struct http_client_factory_impl
{
    const IActivationFactoryVtbl *vtbl_activation;
    const IHttpClientFactoryVtbl *vtbl_factory;
    LONG ref;
};

struct http_base_protocol_filter
{
    const IHttpBaseProtocolFilterVtbl *vtbl_base;
    const IHttpFilterVtbl *vtbl_filter;
    const IClosableVtbl *vtbl_closable;
    LONG ref;
    boolean allow_auto_redirect;
    boolean allow_ui;
    boolean auto_decompression;
    UINT32 max_connections_per_server;
    boolean use_proxy;
};

/* ---------------------------------------------------------------------
 * Extension vtable structs: the declared (generated) face plus the
 * Win11 projection fill region that NativeAOT / C++/WinRT callers
 * virtual-call beyond the declared slots.
 * ------------------------------------------------------------------- */

struct async_op_face
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IAsyncInfo *This, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IAsyncInfo *This );
    ULONG (STDMETHODCALLTYPE *Release)( IAsyncInfo *This );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IAsyncInfo *This, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IAsyncInfo *This, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IAsyncInfo *This, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *get_Id)( IAsyncInfo *This, UINT32 *id );
    HRESULT (STDMETHODCALLTYPE *get_Status)( IAsyncInfo *This, AsyncStatus *status );
    HRESULT (STDMETHODCALLTYPE *put_Completed)( IAsyncInfo *This, void *handler );
    HRESULT (STDMETHODCALLTYPE *Cancel)( IAsyncInfo *This );
    HRESULT (STDMETHODCALLTYPE *GetResults)( IAsyncInfo *This, void *results );
};

struct async_op_vtbl
{
    struct async_op_face base;   /* 0-10 merged IAsyncOperationWithProgress shape */
    void *fill[29];              /* 11-39 projection region */
};

struct http_content_vtbl
{
    IHttpContentVtbl base; /* 0-13 */
    void *fill[26];        /* 14-39 projection fill */
};

struct http_response_vtbl
{
    IHttpResponseMessageVtbl base; /* 0-19 */
    void *fill[20];                /* 20-39 projection fill */
};

struct http_headers_vtbl
{
    IHttpHeaderCollectionVtbl base; /* 0-7: base6 + Append, TryAppendWithoutValidation */
    void *fill[44];                 /* 8-51 merged projection collection faces */
};

struct stub_inspectable
{
    HRESULT (STDMETHODCALLTYPE *GetIids)( IInspectable *This, ULONG *count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IInspectable *This, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IInspectable *This, TrustLevel *level );
};

/* IMapView-shape merged face: 6 First (projection GetAt alias), 7 HasKey,
 * 8 Size, 9 Lookup, 10 Split. */
struct http_headers_map_vtbl
{
    IUnknownVtbl unk;
    struct
    {
        struct stub_inspectable insp;
        HRESULT (STDMETHODCALLTYPE *First)( void *This, void **current );
        HRESULT (STDMETHODCALLTYPE *HasKey)( void *This, HSTRING key, boolean *has );
        HRESULT (STDMETHODCALLTYPE *Size)( void *This, UINT32 *size );
        HRESULT (STDMETHODCALLTYPE *Lookup)( void *This, HSTRING key, HSTRING *value, boolean *has );
        HRESULT (STDMETHODCALLTYPE *Split)( void *This, void *split );
    } proj;
};

struct hdr_iter_vtbl
{
    IUnknownVtbl unk;
    struct
    {
        struct stub_inspectable insp; /* trio reused: QI/AR/Rel serve GetIids/GetRCN/GetTrust */
        HRESULT (STDMETHODCALLTYPE *get_Current)( void *This, void **current );
        HRESULT (STDMETHODCALLTYPE *HasCurrent)( void *This, boolean *has );
        HRESULT (STDMETHODCALLTYPE *MoveNext)( void *This, boolean *has );
    } proj;
};

/* Windows.Foundation.Uri projection face: every string-typed accessor is
 * served by get_String over the stored URI. */
struct uri_vtbl
{
    IUnknownVtbl unk;
    IInspectableVtbl inspectable;
    HRESULT (STDMETHODCALLTYPE *AbsoluteUri)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *AbsoluteUriWithFragment)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *DisplayScheme)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Domain)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Extension)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Host)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Password)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Path)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Query)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *QueryString)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *UserName)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Port)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *SchemeName)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Fragment14)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Fragment15)( void *This, HSTRING *value );
    HRESULT (STDMETHODCALLTYPE *Fragment16)( void *This, HSTRING *value );
};

/* HttpBaseProtocolFilter stub singletons (ICacheControl / ICookieManager /
 * IgnorableServerCertificateErrors vector) share the filter_stub unknown
 * implementation; their faces are the merged projection shapes below. */
struct cache_control_vtbl
{
    IUnknownVtbl unk;
    struct
    {
        struct stub_inspectable insp;
        void *fill[4];      /* 6-9 Behavior get/put pair, twice */
    } proj;
};

struct cert_errors_vector_vtbl
{
    IUnknownVtbl unk;
    struct
    {
        struct stub_inspectable insp;
        void *fill[12];     /* 6-17 IVector<UINT32>-shape projection slots */
    } proj;
};

struct cookie_manager_vtbl
{
    IUnknownVtbl unk;
    struct
    {
        struct stub_inspectable insp;
        void *fill[4];      /* 6-9 SetCookie/SetCookieWithThirdParty/DeleteCookie/GetCookies */
    } proj;
};

/* merged activation-factory faces (IActivationFactory at 0-6,
 * factory-specific slots after) for StreamContent/StringContent/
 * Request/Response/Client/Method classes. */
struct stream_content_factory_vtbl
{
    IActivationFactoryVtbl base; /* 0-6 */
    void *fill[3];               /* 7 GetCompressor, 8 PutCompressor, 9 CreateInstance */
};

struct string_content_factory_vtbl
{
    IActivationFactoryVtbl base; /* 0-6 */
    void *fill[3];               /* 7 CreateFromString, 8 WithEncoding, 9 AndMediaType */
};

struct request_factory_vtbl
{
    IActivationFactoryVtbl base; /* 0-5 base, 6 Create(method, uri) */
    void *fill[2];               /* 7 ActivateInstance alias, 8 Create alias */
};

struct response_factory_vtbl
{
    IActivationFactoryVtbl base; /* 0-5 base, 6 Create(status) */
    void *fill[1];               /* 7 ActivateInstance alias */
};

static inline struct http_string_content *impl_from_content( void *iface )
{
    return (struct http_string_content *)iface;
}

static inline struct http_request_message *impl_from_request( void *iface )
{
    return (struct http_request_message *)iface;
}

/* HttpStringContent/IPropertyValue dual value returned by the header
 * collection Get accessors: a standard IPropertyValue over an HSTRING. */
struct dstr_pv
{
    const IPropertyValueVtbl *vtbl;
    LONG ref;
    HSTRING str;
};

/* --- cross-file vtable exports (defined next to their implementations) --- */
extern const struct http_headers_map_vtbl http_headers_map_vtbl;

/* activation factories exported to main.c */
extern IActivationFactory *http_client_factory;
extern IActivationFactory *http_request_factory;
extern IActivationFactory *http_response_factory;
extern IActivationFactory *http_string_content_factory;
extern IActivationFactory *http_stream_content_factory;
extern IActivationFactory *http_method_factory;
extern IActivationFactory *http_base_protocol_filter_factory;

#endif /* __WINE_WINDOWS_WEB_HTTP_PRIVATE_H */
