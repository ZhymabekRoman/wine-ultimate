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

/* --- Header Collections --- */

/* Parameterized (UUIDv5) face that WindowsPackageManager.dll (winget) queries on
 * the header *value* object returned by the header-collection Get accessor, and
 * on which it virtual-calls slot 15 (+0x78). */
static const GUID IID_http_headers_iterator =
    { 0xc312da8d, 0xc82f, 0x51c9, { 0xb7, 0x05, 0x5a, 0x26, 0xd5, 0x2f, 0xbe, 0xe6 } };



static struct http_headers *map_impl( void *iface )
{
    /* The IMapView/IIterable face is the vtbl_map member, not the struct base. */
    return (struct http_headers *)((char *)iface - offsetof( struct http_headers, vtbl_map ));
}

static struct http_headers *imap_impl( void *iface )
{
    return (struct http_headers *)((char *)iface - offsetof( struct http_headers, vtbl_imap ));
}

static struct http_headers *req_hdr_impl( void *iface )
{
    return (struct http_headers *)((char *)iface - offsetof( struct http_headers, vtbl_req_headers ));
}

static const void *const http_headers_imap_vtbl[];
static const void *const http_req_headers_vtbl[];

static HRESULT WINAPI headers_QueryInterface( IHttpHeaderCollection *This, REFIID iid, void **out )
{
    static const GUID IID_IMap_HSTRING_HSTRING_h =
        { 0xf6d1f700, 0x49c2, 0x52ae, { 0x81, 0x54, 0x82, 0x6f, 0x99, 0x08, 0x77, 0x3c } };
    static const GUID IID_IIterable_KVP_HSTRING_HSTRING_h =
        { 0xe9bdaaf0, 0xcbf6, 0x5c72, { 0xbe, 0x90, 0x29, 0xcb, 0xf3, 0xa1, 0x31, 0x9b } };
    static const GUID IID_IMapView_HSTRING_HSTRING_h =
        { 0xac7f26f2, 0xfeb7, 0x5b2a, { 0x8a, 0xc4, 0x34, 0x5b, 0xc6, 0x2c, 0xae, 0xde } };
    struct http_headers *impl = (struct http_headers *)This;
    WDBG( "headers_QI %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (!iid)
    {
        *out = NULL;
        return E_INVALIDARG;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IHttpResponseHeaderCollection ) ||
        IsEqualGUID( iid, &IID_IHttpContentHeaderCollection ) ||
        IsEqualGUID( iid, &IID_http_headers_iterator ))
    {
        *out = This;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IHttpRequestHeaderCollection ))
    {
        *out = &impl->vtbl_req_headers;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMap_HSTRING_HSTRING_h ))
    {
        *out = &impl->vtbl_imap;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IIterable_KVP_HSTRING_HSTRING_h ) ||
        IsEqualGUID( iid, &IID_IMapView_HSTRING_HSTRING_h ))
    {
        *out = &impl->vtbl_map;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI headers_AddRef( IHttpHeaderCollection *This )
{
    struct http_headers *impl = (struct http_headers *)This;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI headers_Release( IHttpHeaderCollection *This )
{
    struct http_headers *impl = (struct http_headers *)This;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;
        for (i = 0; i < impl->count; i++)
        {
            WindowsDeleteString( impl->pairs[i].k );
            WindowsDeleteString( impl->pairs[i].v );
        }
        WindowsDeleteString( impl->last_name );
        WindowsDeleteString( impl->last_value );
        free( impl->pairs );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI headers_GetIids( IHttpHeaderCollection *This, ULONG *c, IID **i )
{
    if (c) *c = 0;
    if (i) *i = NULL;
    return S_OK;
}
static HRESULT WINAPI headers_GetRuntimeClassName( IHttpHeaderCollection *This, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Web.Http.Headers.HttpRequestHeaderCollection";
    if (!n) return E_POINTER;
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI headers_GetTrustLevel( IHttpHeaderCollection *This, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

/* Standard IPropertyValue wrapper over an HSTRING (no VA tricks): the
 * header-collection Get accessors hand out the value as a
 * Windows.Foundation.IPropertyValue. */
static HRESULT WINAPI pv_QueryInterface( IPropertyValue *This, REFIID iid, void **out )
{
    struct dstr_pv *impl = (struct dstr_pv *)This;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPropertyValue ))
    {
        *out = impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_http_headers_iterator ))
    {
        *out = &impl->vtbl_iter;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pv_AddRef( IPropertyValue *This )
{
    struct dstr_pv *impl = (struct dstr_pv *)This;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI pv_Release( IPropertyValue *This )
{
    struct dstr_pv *impl = (struct dstr_pv *)This;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->str );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI pv_GetIids( IPropertyValue *This, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI pv_GetRuntimeClassName( IPropertyValue *This, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.PropertyValue";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI pv_GetTrustLevel( IPropertyValue *This, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI pv_get_Type( IPropertyValue *This, PropertyType *type )
{
    if (type) *type = PropertyType_String;
    return S_OK;
}
static HRESULT WINAPI pv_get_IsNumericScalar( IPropertyValue *This, boolean *has )
{
    if (has) *has = FALSE;
    return S_OK;
}
static HRESULT WINAPI pv_get_String( IPropertyValue *This, HSTRING *value )
{
    struct dstr_pv *impl = (struct dstr_pv *)This;
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->str, value );
}
/* every scalar getter hands back the stored string (WinRT does not
 * cross-convert here; consumers call GetString). */
static HRESULT WINAPI pv_get_scalar( IPropertyValue *This, void *value )
{
    struct dstr_pv *impl = (struct dstr_pv *)This;
    HSTRING *out = value;
    if (!out) return E_POINTER;
    return WindowsDuplicateString( impl->str, out );
}
static HRESULT WINAPI pv_get_array( IPropertyValue *This, UINT32 *size, void **value )
{
    if (size) *size = 0;
    if (value) *value = NULL;
    return S_OK;
}

#define PV_SCALAR(t) (HRESULT (STDMETHODCALLTYPE *)(IPropertyValue *, t *))pv_get_scalar
#define PV_ARRAY(t) (HRESULT (STDMETHODCALLTYPE *)(IPropertyValue *, UINT32 *, t **))pv_get_array

static const IPropertyValueVtbl dstr_pv_vtbl = {
    pv_QueryInterface,
    pv_AddRef,
    pv_Release,
    pv_GetIids,
    pv_GetRuntimeClassName,
    pv_GetTrustLevel,
    pv_get_Type,
    pv_get_IsNumericScalar,
    PV_SCALAR(BYTE),
    PV_SCALAR(INT16),
    PV_SCALAR(UINT16),
    PV_SCALAR(INT32),
    PV_SCALAR(UINT32),
    PV_SCALAR(INT64),
    PV_SCALAR(UINT64),
    PV_SCALAR(FLOAT),
    PV_SCALAR(DOUBLE),
    PV_SCALAR(WCHAR),
    PV_SCALAR(boolean),
    pv_get_String,
    PV_SCALAR(GUID),
    PV_SCALAR(DateTime),
    PV_SCALAR(TimeSpan),
    PV_SCALAR(Point),
    PV_SCALAR(Size),
    PV_SCALAR(Rect),
    PV_ARRAY(BYTE),
    PV_ARRAY(INT16),
    PV_ARRAY(UINT16),
    PV_ARRAY(INT32),
    PV_ARRAY(UINT32),
    PV_ARRAY(INT64),
    PV_ARRAY(UINT64),
    PV_ARRAY(FLOAT),
    PV_ARRAY(DOUBLE),
    PV_ARRAY(WCHAR),
    PV_ARRAY(boolean),
    PV_ARRAY(HSTRING),
    PV_ARRAY(IInspectable *),
    PV_ARRAY(GUID),
    PV_ARRAY(DateTime),
    PV_ARRAY(TimeSpan),
    PV_ARRAY(Point),
    PV_ARRAY(Size),
    PV_ARRAY(Rect),
};

/* Face served for IID_http_headers_iterator on the header value object.
 * WindowsPackageManager.dll QIs for it right after the Get accessor and then
 * virtual-calls slot 15 (+0x78); each slot logs so the live ABI is visible. */
static HRESULT WINAPI pv_iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct dstr_pv *impl = dstr_pv_from_iter( iface );
    TRACE( "pv_iter_QI %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPropertyValue ) ||
        IsEqualGUID( iid, &IID_http_headers_iterator ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pv_iter_AddRef( void *iface )
{
    return InterlockedIncrement( &dstr_pv_from_iter( iface )->ref );
}
static ULONG WINAPI pv_iter_Release( void *iface )
{
    struct dstr_pv *impl = dstr_pv_from_iter( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->str );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI pv_iter_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI pv_iter_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.Foundation.IPropertyValue";
    if (!name) return E_POINTER;
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, name );
}
static HRESULT WINAPI pv_iter_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}

#define PV_ITER_STUB(n) \
static HRESULT WINAPI pv_iter_slot##n( void *iface, void *a, void *b, void *c, void *d ) \
{ \
    struct dstr_pv *impl = dstr_pv_from_iter( iface ); \
    TRACE( "pv_iter_slot%u this %p str %s a %s b %p c %p d %p\n", (n), impl, \
           debugstr_w( impl->str ? WindowsGetStringRawBuffer( impl->str, NULL ) : NULL ), \
           is_hstring( a ) ? debugstr_w( WindowsGetStringRawBuffer( (HSTRING)a, NULL ) ) : debugstr_a( "?" ), \
           b, c, d ); \
    return S_OK; \
}
PV_ITER_STUB(6)
PV_ITER_STUB(7)
PV_ITER_STUB(8)
PV_ITER_STUB(9)
PV_ITER_STUB(10)
PV_ITER_STUB(11)
PV_ITER_STUB(12)
PV_ITER_STUB(13)
PV_ITER_STUB(14)
/* Slot 15 (+0x78): keyed getter — (this, REFIID key, void **object, UINT32, UINT32).
 * WindowsPackageManager.dll (winget) virtual-calls it on the header value object right
 * after QI and hands the returned object on; the boxed header value is our
 * IPropertyValue face. */
static HRESULT WINAPI pv_iter_GetValue( void *iface, REFIID key, void **object )
{
    struct dstr_pv *impl = dstr_pv_from_iter( iface );
    TRACE( "pv_iter_GetValue key %s value %s\n", debugstr_guid( key ),
           debugstr_w( impl->str ? WindowsGetStringRawBuffer( impl->str, NULL ) : NULL ) );
    if (!object) return E_POINTER;
    *object = impl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
PV_ITER_STUB(16)
PV_ITER_STUB(17)
PV_ITER_STUB(18)
PV_ITER_STUB(19)
PV_ITER_STUB(20)
PV_ITER_STUB(21)
PV_ITER_STUB(22)

static const void *const pv_iter_vtbl[] =
{
    pv_iter_QueryInterface,   /* 0 */
    pv_iter_AddRef,           /* 1 */
    pv_iter_Release,          /* 2 */
    pv_iter_GetIids,          /* 3 */
    pv_iter_GetRuntimeClassName, /* 4 */
    pv_iter_GetTrustLevel,    /* 5 */
    pv_iter_slot6,  pv_iter_slot7,  pv_iter_slot8,  pv_iter_slot9,   /* 6-9  */
    pv_iter_slot10, pv_iter_slot11, pv_iter_slot12, pv_iter_slot13,  /* 10-13 */
    pv_iter_slot14, pv_iter_GetValue, pv_iter_slot16, pv_iter_slot17,  /* 14-17 */
    pv_iter_slot18, pv_iter_slot19, pv_iter_slot20, pv_iter_slot21,  /* 18-21 */
    pv_iter_slot22,           /* 22 */
};

static struct dstr_pv *create_dstr_pv( const WCHAR *str )
{
    struct dstr_pv *obj;

    if (!(obj = malloc( sizeof(*obj) ))) return NULL;
    if (FAILED( WindowsCreateString( str, lstrlenW( str ), &obj->str ) ))
    {
        free( obj );
        return NULL;
    }
    obj->vtbl = &dstr_pv_vtbl;
    obj->vtbl_iter = pv_iter_vtbl;
    obj->ref = 1;
    return obj;
}

/* last header appended by the consumer (the x-ms-meta-sourceversion probe),
 * per-instance so concurrent collections do not clobber each other. */
HRESULT WINAPI headers_Append( IHttpHeaderCollection *This, HSTRING name, HSTRING value )
{
    struct http_headers *impl = (struct http_headers *)This;
    TRACE( "name %s, value %s\n", debugstr_hstring( name ), debugstr_hstring( value ) );
    if (name && value)
    {
        WindowsDeleteString( impl->last_name );
        WindowsDeleteString( impl->last_value );
        WindowsDuplicateString( name, &impl->last_name );
        WindowsDuplicateString( value, &impl->last_value );
        if (impl->count == impl->cap)
        {
            struct hdr_pair *np;
            UINT32 cap = impl->cap ? impl->cap * 2 : 16;
            if (!(np = realloc( impl->pairs, cap * sizeof(*np) ))) return E_OUTOFMEMORY;
            impl->pairs = np;
            impl->cap = cap;
        }
        WindowsDuplicateString( name, &impl->pairs[impl->count].k );
        WindowsDuplicateString( value, &impl->pairs[impl->count].v );
        impl->count++;
    }
    return S_OK;
}
static HRESULT WINAPI headers_TryAppendWithoutValidation( IHttpHeaderCollection *This, HSTRING name, HSTRING value, boolean *succeeded )
{
    HRESULT hr = headers_Append( This, name, value );
    if (succeeded) *succeeded = SUCCEEDED( hr );
    return S_OK;
}
static HRESULT WINAPI headers_stub_ok( void *iface ) { TRACE( "headers_stub_ok\n" ); return S_OK; }

struct header_subcol { const void *lpVtbl; LONG ref; const WCHAR *cls_name; };
static HRESULT WINAPI subcol_QI( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IReferenceTracker_local =
        { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6 } };
    static const GUID IID_IMarshal_local =
        { 0x00000003, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    struct header_subcol *impl = iface;
    TRACE( "subcol_QI %s (%s)\n", debugstr_guid( iid ), debugstr_w( impl->cls_name ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IReferenceTracker_local ) || IsEqualGUID( iid, &IID_IMarshal_local ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI subcol_AddRef( void *iface ) { return InterlockedIncrement( &((struct header_subcol *)iface)->ref ); }
static ULONG WINAPI subcol_Release( void *iface )
{
    struct header_subcol *impl = iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI subcol_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI subcol_GetRuntimeClassName( void *iface, HSTRING *n )
{
    struct header_subcol *impl = iface;
    const WCHAR *cls = impl->cls_name ? impl->cls_name : L"Windows.Web.Http.Headers.HttpMediaTypeWithQualityHeaderValueCollection";
    TRACE( "subcol_GetRuntimeClassName -> %s\n", debugstr_w( cls ) );
    return WindowsCreateString( cls, wcslen( cls ), n );
}
static HRESULT WINAPI subcol_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI subcol_noop_ok( void *iface, void *a, void *b, void *c )
{
    TRACE( "subcol_noop_ok a=%p b=%p c=%p\n", a, b, c );
    return S_OK;
}
static const void * const subcol_vtbl[24] =
{
    subcol_QI, subcol_AddRef, subcol_Release, subcol_GetIids, subcol_GetRuntimeClassName, subcol_GetTrustLevel,
    subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok,
    subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok,
    subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok, subcol_noop_ok,
};

static HRESULT headers_make_subcol( const WCHAR *cls_name, void **out )
{
    struct header_subcol *sc;
    TRACE( "headers_make_subcol -> %s\n", debugstr_w( cls_name ) );
    if (!out) return E_POINTER;
    if (!(sc = calloc( 1, sizeof(*sc) ))) return E_OUTOFMEMORY;
    sc->lpVtbl = subcol_vtbl;
    sc->ref = 1;
    sc->cls_name = cls_name;
    *out = sc;
    return S_OK;
}

#define DEFINE_HDR_SUBCOL(name, cls) \
static HRESULT WINAPI headers_get_##name( void *iface, void **out ) \
{ \
    return headers_make_subcol( cls, out ); \
}

DEFINE_HDR_SUBCOL( Accept, L"Windows.Web.Http.Headers.HttpMediaTypeWithQualityHeaderValueCollection" )
DEFINE_HDR_SUBCOL( AcceptEncoding, L"Windows.Web.Http.Headers.HttpContentCodingWithQualityHeaderValueCollection" )
DEFINE_HDR_SUBCOL( AcceptLanguage, L"Windows.Web.Http.Headers.HttpLanguageRangeWithQualityHeaderValueCollection" )
DEFINE_HDR_SUBCOL( CacheControl, L"Windows.Web.Http.Headers.HttpCacheDirectiveHeaderValueCollection" )
DEFINE_HDR_SUBCOL( Connection, L"Windows.Web.Http.Headers.HttpConnectionOptionHeaderValueCollection" )
DEFINE_HDR_SUBCOL( Cookie, L"Windows.Web.Http.Headers.HttpCookiePairHeaderValueCollection" )
DEFINE_HDR_SUBCOL( Expect, L"Windows.Web.Http.Headers.HttpExpectationHeaderValueCollection" )
DEFINE_HDR_SUBCOL( Pragma, L"Windows.Web.Http.Headers.HttpNameValueHeaderValue" )
DEFINE_HDR_SUBCOL( TE, L"Windows.Web.Http.Headers.HttpTransferCodingHeaderValueCollection" )
DEFINE_HDR_SUBCOL( TransferEncoding, L"Windows.Web.Http.Headers.HttpTransferCodingHeaderValueCollection" )
DEFINE_HDR_SUBCOL( UserAgent, L"Windows.Web.Http.Headers.HttpProductInfoHeaderValueCollection" )
static HRESULT WINAPI headers_stub_null( void *iface, void **out )
{
    TRACE( "headers_stub_null -> NULL\n" );
    if (out) *out = NULL;
    return S_OK;
}

static HRESULT WINAPI headers_Get( void *iface, void **out )
{
    struct http_headers *impl = (struct http_headers *)iface;
    TRACE( "headers_Get\n" );
    if (out)
    {
        const WCHAR *val = impl->last_value ? WindowsGetStringRawBuffer( impl->last_value, NULL ) : NULL;
        struct dstr_pv *pv = create_dstr_pv( val ? val : L"1.0.0" );
        *out = pv;
        TRACE( "headers_Get -> %s\n", debugstr_w( val ? val : L"1.0.0" ) );
    }
    return S_OK;
}

const struct hdr_pair *http_headers_pairs( void *iface, UINT32 *count )
{
    struct http_headers *impl = (struct http_headers *)iface;
    if (count) *count = impl->count;
    return impl->pairs;
}

static const struct http_headers_vtbl http_headers_vtbl = {
  {
    headers_QueryInterface,
    headers_AddRef,
    headers_Release,
    headers_GetIids,
    headers_GetRuntimeClassName,
    headers_GetTrustLevel,
    headers_Append,
    headers_TryAppendWithoutValidation
  },
  {
    headers_get_Accept, /* 8: get_Accept */
    headers_get_AcceptEncoding, /* 9: get_AcceptEncoding */
    headers_get_AcceptLanguage, /* 10: get_AcceptLanguage */
    headers_stub_null,  /* 11: get_Authorization */
    headers_stub_ok,    /* 12: put_Authorization */
    headers_get_CacheControl, /* 13: get_CacheControl */
    headers_get_Connection, /* 14: get_Connection */
    headers_get_Cookie, /* 15: get_Cookie */
    headers_stub_null,  /* 16: get_Date */
    headers_stub_ok,    /* 17: put_Date */
    headers_get_Expect, /* 18: get_Expect */
    headers_stub_null,  /* 19: get_From */
    headers_stub_ok,    /* 20: put_From */
    headers_stub_null,  /* 21: get_Host */
    headers_stub_ok,    /* 22: put_Host */
    headers_stub_null,  /* 23: get_IfModifiedSince */
    headers_stub_ok,    /* 24: put_IfModifiedSince */
    headers_stub_null,  /* 25: get_IfUnmodifiedSince */
    headers_stub_ok,    /* 26: put_IfUnmodifiedSince */
    headers_stub_null,  /* 27: get_MaxForwards */
    headers_stub_ok,    /* 28: put_MaxForwards */
    headers_get_Pragma, /* 29: get_Pragma */
    headers_stub_null,  /* 30: get_ProxyAuthorization */
    headers_stub_ok,    /* 31: put_ProxyAuthorization */
    headers_stub_null,  /* 32: get_Referer */
    headers_Append,     /* 33: Append */
    headers_TryAppendWithoutValidation, /* 34: TryAppendWithoutValidation */
    headers_get_TransferEncoding, /* 35: get_TransferEncoding */
    headers_stub_null,  /* 36 */
    headers_stub_null,
    headers_stub_null,
    headers_stub_null,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get,
    headers_Get
  }
};

HRESULT create_http_headers( const GUID *collection_iid, void **out )
{
    struct http_headers *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_headers = &http_headers_vtbl;
    impl->vtbl_map = &http_headers_map_vtbl;
    impl->vtbl_imap = http_headers_imap_vtbl;
    impl->vtbl_req_headers = http_req_headers_vtbl;
    impl->ref = 1;
    impl->iid = *collection_iid;
    *out = impl;
    return S_OK;
}

/* IMapView`2<K,V> (Win11 22621 merged projection) over the header pairs:
 * 6 GetAt(key)->value, 7 HasKey, 8 Size, 9 Lookup(key,&value,&has), 10 Split. */
static HRESULT WINAPI map_HasKey( void *iface, HSTRING key, boolean *has )
{
    struct http_headers *impl = map_impl( iface );
    UINT32 i;
    WDBG( "map_HasKey\n" );
    if (!has) return E_POINTER;
    *has = false;
    for (i = 0; i < impl->count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->pairs[i].k, key, &cmp );
        if (!cmp) { *has = true; break; }
    }
    return S_OK;
}
static HRESULT WINAPI map_Size( void *iface, UINT32 *size )
{
    struct http_headers *impl = map_impl( iface );
    WDBG( "map_Size %u\n", impl->count );
    if (!size) return E_POINTER;
    *size = impl->count;
    return S_OK;
}
static HRESULT WINAPI map_Lookup( void *iface, HSTRING key, HSTRING *value, boolean *has )
{
    struct http_headers *impl = map_impl( iface );
    UINT32 i;
    WDBG( "map_Lookup\n" );
    if (!value || !has) return E_POINTER;
    *value = NULL;
    *has = false;
    for (i = 0; i < impl->count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->pairs[i].k, key, &cmp );
        if (!cmp)
        {
            *has = true;
            return WindowsDuplicateString( impl->pairs[i].v, value );
        }
    }
    return S_OK;
}
static HRESULT WINAPI map_Split( void *iface, void *split )
{
    WDBG( "map_Split\n" );
    return create_http_headers( &IID_IUnknown, (void **)split );
}

/* IIterable`1<IKeyValuePair<String,String>> view: slot 6 = First. */


static HRESULT WINAPI kvp_QI( IKeyValuePair_HSTRING_HSTRING *This, REFIID iid, void **out )
{
    static const GUID IID_IKVP_HSTRING_HSTRING =
        { 0x60310303, 0x49c5, 0x52e6, { 0xab, 0xc6, 0xa9, 0xb3, 0x6e, 0xcc, 0xc7, 0x16 } };
    WDBG( "kvp_QI %s\n", debugstr_guid( iid ) );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IKVP_HSTRING_HSTRING ))
    {
        *out = This;
        InterlockedIncrement( &((struct hdr_kvp *)This)->ref );
        return S_OK;
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI kvp_AddRef( IKeyValuePair_HSTRING_HSTRING *This )
{
    return InterlockedIncrement( &((struct hdr_kvp *)This)->ref );
}
static ULONG WINAPI kvp_Release( IKeyValuePair_HSTRING_HSTRING *This )
{
    struct hdr_kvp *impl = (struct hdr_kvp *)This;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->k );
        WindowsDeleteString( impl->v );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI kvp_Key( IKeyValuePair_HSTRING_HSTRING *This, HSTRING *out )
{
    struct hdr_kvp *impl = (struct hdr_kvp *)This;
    WDBG( "kvp_Key\n" );
    if (!out) return E_POINTER;
    return WindowsDuplicateString( impl->k, out );
}
static HRESULT WINAPI kvp_Value( IKeyValuePair_HSTRING_HSTRING *This, HSTRING *out )
{
    struct hdr_kvp *impl = (struct hdr_kvp *)This;
    WDBG( "kvp_Value\n" );
    if (!out) return E_POINTER;
    return WindowsDuplicateString( impl->v, out );
}
static HRESULT WINAPI kvp_GetIids( IKeyValuePair_HSTRING_HSTRING *This, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI kvp_GetRuntimeClassName( IKeyValuePair_HSTRING_HSTRING *This, HSTRING *name )
{
    static const WCHAR nm[] = L"Windows.Foundation.Collections.IKeyValuePair`2<System.String,System.String>";
    if (!name) return E_POINTER;
    return WindowsCreateString( nm, ARRAY_SIZE(nm) - 1, name );
}
static HRESULT WINAPI kvp_GetTrustLevel( IKeyValuePair_HSTRING_HSTRING *This, TrustLevel *level )
{
    if (level) *level = 0 /* BaseTrust */;
    return S_OK;
}
static const IKeyValuePair_HSTRING_HSTRINGVtbl hdr_kvp_vtbl = {
    kvp_QI,
    kvp_AddRef,
    kvp_Release,
    kvp_GetIids,
    kvp_GetRuntimeClassName,
    kvp_GetTrustLevel,
    kvp_Key,
    /* 6 = IKeyValuePair::Key */
    kvp_Value,
    /* 7 = IKeyValuePair::Value */
};

static HRESULT WINAPI iter_QI( IUnknown *iface, REFIID iid, void **out )
{
    WDBG( "iter_QI {%08x-%04x-%04x-%02x%02x}\n", (unsigned)iid->Data1, iid->Data2, iid->Data3,
          iid->Data4[6], iid->Data4[7] );
    *out = iface;
    ((struct hdr_iter *)iface)->ref++;
    return S_OK;
}
static ULONG WINAPI iter_AddRef( IUnknown *iface )
{
    return InterlockedIncrement( &((struct hdr_iter *)iface)->ref );
}
static ULONG WINAPI iter_Release( IUnknown *iface )
{
    struct hdr_iter *impl = (struct hdr_iter *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        IUnknown_Release( (IUnknown *)impl->owner );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI iter_MoveNext( void *iface, boolean *has )
{
    struct hdr_iter *impl = (struct hdr_iter *)iface;
    if (!has) return E_POINTER;
    if (impl->idx < impl->owner->count) impl->idx++;
    *has = (impl->idx < impl->owner->count);
    WDBG( "iter_MoveNext idx %u\n", impl->idx );
    return S_OK;
}
static HRESULT WINAPI iter_get_Current( void *iface, void **out )
{
    struct hdr_iter *impl = (struct hdr_iter *)iface;
    UINT32 i = impl->idx;
    struct hdr_kvp *kvp;
    WDBG( "iter_get_Current idx %u\n", i );
    if (!out) return E_POINTER;
    *out = NULL;
    if (i >= impl->owner->count) return E_BOUNDS;
    if (!(kvp = calloc( 1, sizeof(*kvp) ))) return E_OUTOFMEMORY;
    kvp->vtbl = &hdr_kvp_vtbl;
    kvp->ref = 1;
    WindowsDuplicateString( impl->owner->pairs[i].k, &kvp->k );
    WindowsDuplicateString( impl->owner->pairs[i].v, &kvp->v );
    *out = kvp;
    return S_OK;
}
static HRESULT WINAPI iter_HasCurrent( void *iface, boolean *has )
{
    struct hdr_iter *impl = (struct hdr_iter *)iface;
    if (!has) return E_POINTER;
    *has = (impl->idx < impl->owner->count);
    return S_OK;
}
static const struct hdr_iter_vtbl hdr_iter_vtbl = {
  {
    iter_QI,
    iter_AddRef,
    iter_Release
  },
  {
    {
    (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, ULONG *, IID **))iter_QI,
    (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, HSTRING *))iter_AddRef,
    (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, TrustLevel *))iter_Release
    },
    iter_get_Current,
    iter_HasCurrent,
    iter_MoveNext
  }
};

static HRESULT WINAPI map_First( void *iface, void **out )
{
    struct http_headers *impl = map_impl( iface );
    struct hdr_iter *it;
    WDBG( "map_First count %u\n", impl->count );
    if (!out) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->vtbl = &hdr_iter_vtbl;
    it->ref = 1;
    it->owner = impl;
    IUnknown_AddRef( (IUnknown *)impl );
    *out = it;
    return S_OK;
}
static HRESULT WINAPI map_QI( void *iface, REFIID iid, void **out )
{
    return headers_QueryInterface( (IHttpHeaderCollection *)map_impl( iface ), iid, out );
}
static ULONG WINAPI map_AddRef( void *iface )
{
    return headers_AddRef( (IHttpHeaderCollection *)map_impl( iface ) );
}
static ULONG WINAPI map_Release( void *iface )
{
    return headers_Release( (IHttpHeaderCollection *)map_impl( iface ) );
}

const struct http_headers_map_vtbl http_headers_map_vtbl = {
  {
    (HRESULT (STDMETHODCALLTYPE *)(IUnknown *, REFIID, void **))map_QI,
    (ULONG (STDMETHODCALLTYPE *)(IUnknown *))map_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IUnknown *))map_Release
  },
  {
    {
    (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, ULONG *, IID **))headers_GetIids,
    (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, HSTRING *))headers_GetRuntimeClassName,
    (HRESULT (STDMETHODCALLTYPE *)(IInspectable *, TrustLevel *))headers_GetTrustLevel
    },
    map_First,
    map_HasKey,
    map_Size,
    map_Lookup,
    map_Split
  }
};

/* IMap<HSTRING, HSTRING> ({f6d1f700-49c2-52ae-8154-826f9908773c}) */
static HRESULT WINAPI imap_QI( void *iface, REFIID iid, void **out )
{
    return headers_QueryInterface( (IHttpHeaderCollection *)imap_impl( iface ), iid, out );
}
static ULONG WINAPI imap_AddRef( void *iface )
{
    return headers_AddRef( (IHttpHeaderCollection *)imap_impl( iface ) );
}
static ULONG WINAPI imap_Release( void *iface )
{
    return headers_Release( (IHttpHeaderCollection *)imap_impl( iface ) );
}
static HRESULT WINAPI imap_Lookup( void *iface, HSTRING key, HSTRING *value )
{
    struct http_headers *impl = imap_impl( iface );
    UINT32 i;
    if (!value) return E_POINTER;
    *value = NULL;
    for (i = 0; i < impl->count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->pairs[i].k, key, &cmp );
        if (!cmp) return WindowsDuplicateString( impl->pairs[i].v, value );
    }
    return E_BOUNDS;
}
static HRESULT WINAPI imap_get_Size( void *iface, UINT32 *size )
{
    struct http_headers *impl = imap_impl( iface );
    if (!size) return E_POINTER;
    *size = impl->count;
    return S_OK;
}
static HRESULT WINAPI imap_HasKey( void *iface, HSTRING key, boolean *found )
{
    struct http_headers *impl = imap_impl( iface );
    UINT32 i;
    if (!found) return E_POINTER;
    *found = FALSE;
    for (i = 0; i < impl->count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->pairs[i].k, key, &cmp );
        if (!cmp) { *found = TRUE; break; }
    }
    return S_OK;
}
static HRESULT WINAPI imap_GetView( void *iface, void **view )
{
    struct http_headers *impl = imap_impl( iface );
    if (!view) return E_POINTER;
    *view = &impl->vtbl_map;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static HRESULT WINAPI imap_Insert( void *iface, HSTRING key, HSTRING value, boolean *replaced )
{
    struct http_headers *impl = imap_impl( iface );
    UINT32 i;
    TRACE( "imap_Insert key %s, value %s\n", debugstr_hstring( key ), debugstr_hstring( value ) );
    if (replaced) *replaced = FALSE;
    for (i = 0; i < impl->count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->pairs[i].k, key, &cmp );
        if (!cmp)
        {
            WindowsDeleteString( impl->pairs[i].v );
            WindowsDuplicateString( value, &impl->pairs[i].v );
            if (replaced) *replaced = TRUE;
            return S_OK;
        }
    }
    return headers_Append( (IHttpHeaderCollection *)impl, key, value );
}
static HRESULT WINAPI imap_Remove( void *iface, HSTRING key )
{
    struct http_headers *impl = imap_impl( iface );
    UINT32 i;
    for (i = 0; i < impl->count; i++)
    {
        INT32 cmp = 1;
        WindowsCompareStringOrdinal( impl->pairs[i].k, key, &cmp );
        if (!cmp)
        {
            WindowsDeleteString( impl->pairs[i].k );
            WindowsDeleteString( impl->pairs[i].v );
            if (i + 1 < impl->count)
                memmove( &impl->pairs[i], &impl->pairs[i + 1], (impl->count - i - 1) * sizeof(*impl->pairs) );
            impl->count--;
            return S_OK;
        }
    }
    return E_BOUNDS;
}
static HRESULT WINAPI imap_Clear( void *iface )
{
    struct http_headers *impl = imap_impl( iface );
    UINT32 i;
    for (i = 0; i < impl->count; i++)
    {
        WindowsDeleteString( impl->pairs[i].k );
        WindowsDeleteString( impl->pairs[i].v );
    }
    impl->count = 0;
    return S_OK;
}
static const void *const http_headers_imap_vtbl[] =
{
    imap_QI,
    imap_AddRef,
    imap_Release,
    headers_GetIids,
    headers_GetRuntimeClassName,
    headers_GetTrustLevel,
    imap_Lookup,
    imap_get_Size,
    imap_HasKey,
    imap_GetView,
    imap_Insert,
    imap_Remove,
    imap_Clear,
};

/* Lightweight header value collection (Accept, AcceptEncoding, AcceptLanguage, UserAgent, etc.) */
struct hdr_val_coll
{
    const void *const *vtbl;
    LONG ref;
    struct http_headers *owner;
    const WCHAR *header_name;
};

static HRESULT WINAPI hdr_val_coll_QI( void *iface, REFIID iid, void **out )
{
    static const GUID allowed[] =
    {
        { 0x9a586b89, 0xd5d0, 0x4fbe, { 0xbd, 0x9d, 0xb5, 0xb3, 0x63, 0x68, 0x11, 0xb4 } }, /* IHttpCacheDirectiveHeaderValueCollection */
        { 0xe4f56c1d, 0x5142, 0x4e00, { 0x8e, 0x0f, 0x01, 0x95, 0x09, 0x33, 0x76, 0x29 } }, /* IHttpConnectionOptionHeaderValueCollection */
        { 0x7c0d753e, 0xe899, 0x4378, { 0xb5, 0xc8, 0x41, 0x2d, 0x82, 0x07, 0x11, 0xcc } }, /* IHttpContentCodingWithQualityHeaderValueCollection */
        { 0xf3f44350, 0x581e, 0x4ecc, { 0x9f, 0x59, 0xe5, 0x07, 0xd0, 0x4f, 0x06, 0xe6 } }, /* IHttpCookiePairHeaderValueCollection */
        { 0xe78521b3, 0xa0e2, 0x4ac4, { 0x9e, 0x66, 0x79, 0x70, 0x6c, 0xb9, 0xfd, 0x58 } }, /* IHttpExpectationHeaderValueCollection */
        { 0x885d5abd, 0x4b4f, 0x480a, { 0x89, 0xce, 0x8a, 0xed, 0xce, 0xe6, 0xe3, 0xa0 } }, /* IHttpLanguageRangeWithQualityHeaderValueCollection */
        { 0x3c0c6b73, 0x1342, 0x4587, { 0xa0, 0x56, 0x18, 0xd0, 0x2f, 0xf6, 0x71, 0x65 } }, /* IHttpMediaTypeWithQualityHeaderValueCollection */
        { 0x877df74a, 0xd69b, 0x44f8, { 0xad, 0x4f, 0x45, 0x3a, 0xf9, 0xc4, 0x2e, 0xd0 } },  /* IHttpProductInfoHeaderValueCollection */
        { 0x202c8c34, 0x2c03, 0x49b8, { 0x96, 0x65, 0x73, 0xe2, 0x7c, 0xb2, 0xfc, 0x79 } }, /* IHttpTransferCodingHeaderValueCollection */
    };
    struct hdr_val_coll *impl = iface;
    size_t i;
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        InterlockedIncrement( &impl->ref );
        *out = impl;
        return S_OK;
    }
    for (i = 0; i < ARRAY_SIZE(allowed); i++)
    {
        if (IsEqualGUID( iid, &allowed[i] ))
        {
            InterlockedIncrement( &impl->ref );
            *out = impl;
            return S_OK;
        }
    }
    FIXME( "unsupported iid %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI hdr_val_coll_AddRef( void *iface ) { return InterlockedIncrement( &((struct hdr_val_coll *)iface)->ref ); }
static ULONG WINAPI hdr_val_coll_Release( void *iface )
{
    struct hdr_val_coll *impl = iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->owner) headers_Release( (IHttpHeaderCollection *)impl->owner );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI hdr_val_coll_ParseAdd( void *iface, HSTRING input )
{
    struct hdr_val_coll *impl = iface;
    HSTRING hname = NULL;
    TRACE( "hdr_val_coll_ParseAdd %s: %s\n", debugstr_w( impl->header_name ), debugstr_hstring( input ) );
    if (impl->owner && impl->header_name && input)
    {
        if (SUCCEEDED( WindowsCreateString( impl->header_name, lstrlenW( impl->header_name ), &hname ) ))
        {
            headers_Append( (IHttpHeaderCollection *)impl->owner, hname, input );
            WindowsDeleteString( hname );
        }
    }
    return S_OK;
}
static HRESULT WINAPI hdr_val_coll_TryParseAdd( void *iface, HSTRING input, boolean *succeeded )
{
    hdr_val_coll_ParseAdd( iface, input );
    if (succeeded) *succeeded = TRUE;
    return S_OK;
}
static HRESULT WINAPI hdr_val_coll_noop( void *iface ) { return S_OK; }

static const void *const hdr_val_coll_vtbl[] =
{
    hdr_val_coll_QI,
    hdr_val_coll_AddRef,
    hdr_val_coll_Release,
    headers_GetIids,
    headers_GetRuntimeClassName,
    headers_GetTrustLevel,
    hdr_val_coll_ParseAdd,
    hdr_val_coll_TryParseAdd,
    hdr_val_coll_noop,
    hdr_val_coll_noop,
    hdr_val_coll_noop,
    hdr_val_coll_noop,
    hdr_val_coll_noop,
    hdr_val_coll_noop,
    hdr_val_coll_noop,
    hdr_val_coll_noop,
};

static HRESULT create_hdr_val_coll( struct http_headers *owner, const WCHAR *header_name, void **out )
{
    struct hdr_val_coll *coll;
    if (!out) return E_POINTER;
    if (!(coll = calloc( 1, sizeof(*coll) ))) return E_OUTOFMEMORY;
    coll->vtbl = hdr_val_coll_vtbl;
    coll->ref = 1;
    coll->owner = owner;
    coll->header_name = header_name;
    if (owner) headers_AddRef( (IHttpHeaderCollection *)owner );
    *out = coll;
    return S_OK;
}

/* IHttpRequestHeaderCollection ({AF40329B-B544-469B-86B9-AC3D466FEA36}) */
static HRESULT WINAPI req_hdr_QI( void *iface, REFIID iid, void **out )
{
    return headers_QueryInterface( (IHttpHeaderCollection *)req_hdr_impl( iface ), iid, out );
}
static ULONG WINAPI req_hdr_AddRef( void *iface ) { return headers_AddRef( (IHttpHeaderCollection *)req_hdr_impl( iface ) ); }
static ULONG WINAPI req_hdr_Release( void *iface ) { return headers_Release( (IHttpHeaderCollection *)req_hdr_impl( iface ) ); }
static HRESULT WINAPI req_hdr_get_Accept( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Accept", out ); }
static HRESULT WINAPI req_hdr_get_AcceptEncoding( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Accept-Encoding", out ); }
static HRESULT WINAPI req_hdr_get_AcceptLanguage( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Accept-Language", out ); }
static HRESULT WINAPI req_hdr_get_CacheControl( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Cache-Control", out ); }
static HRESULT WINAPI req_hdr_get_Connection( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Connection", out ); }
static HRESULT WINAPI req_hdr_get_Cookie( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Cookie", out ); }
static HRESULT WINAPI req_hdr_get_Expect( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Expect", out ); }
static HRESULT WINAPI req_hdr_get_TransferEncoding( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"Transfer-Encoding", out ); }
static HRESULT WINAPI req_hdr_get_UserAgent( void *iface, void **out ) { return create_hdr_val_coll( req_hdr_impl( iface ), L"User-Agent", out ); }
static HRESULT WINAPI req_hdr_Append( void *iface, HSTRING name, HSTRING value )
{
    return headers_Append( (IHttpHeaderCollection *)req_hdr_impl( iface ), name, value );
}
static HRESULT WINAPI req_hdr_TryAppendWithoutValidation( void *iface, HSTRING name, HSTRING value, boolean *succeeded )
{
    return headers_TryAppendWithoutValidation( (IHttpHeaderCollection *)req_hdr_impl( iface ), name, value, succeeded );
}

static const void *const http_req_headers_vtbl[] =
{
    req_hdr_QI,                         /* 0 */
    req_hdr_AddRef,                     /* 1 */
    req_hdr_Release,                    /* 2 */
    headers_GetIids,                    /* 3 */
    headers_GetRuntimeClassName,        /* 4 */
    headers_GetTrustLevel,              /* 5 */
    req_hdr_get_Accept,                 /* 6 */
    req_hdr_get_AcceptEncoding,         /* 7 */
    req_hdr_get_AcceptLanguage,         /* 8 */
    headers_stub_null,                  /* 9: get_Authorization */
    headers_stub_ok,                    /* 10: put_Authorization */
    req_hdr_get_CacheControl,           /* 11 */
    req_hdr_get_Connection,             /* 12 */
    req_hdr_get_Cookie,                 /* 13 */
    headers_stub_null,                  /* 14: get_Date */
    headers_stub_ok,                    /* 15: put_Date */
    req_hdr_get_Expect,                 /* 16 */
    headers_stub_null,                  /* 17: get_From */
    headers_stub_ok,                    /* 18: put_From */
    headers_stub_null,                  /* 19: get_Host */
    headers_stub_ok,                    /* 20: put_Host */
    headers_stub_null,                  /* 21: get_IfModifiedSince */
    headers_stub_ok,                    /* 22: put_IfModifiedSince */
    headers_stub_null,                  /* 23: get_IfUnmodifiedSince */
    headers_stub_ok,                    /* 24: put_IfUnmodifiedSince */
    headers_stub_null,                  /* 25: get_MaxForwards */
    headers_stub_ok,                    /* 26: put_MaxForwards */
    headers_stub_null,                  /* 27: get_ProxyAuthorization */
    headers_stub_ok,                    /* 28: put_ProxyAuthorization */
    headers_stub_null,                  /* 29: get_Referer */
    headers_stub_ok,                    /* 30: put_Referer */
    req_hdr_get_TransferEncoding,       /* 31 */
    req_hdr_get_UserAgent,              /* 32 */
    req_hdr_Append,                     /* 33 */
    req_hdr_TryAppendWithoutValidation, /* 34 */
};
