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



static struct http_headers *map_impl( void *iface )
{
    /* The IMapView/IIterable face is the vtbl_map member, not the struct base. */
    return (struct http_headers *)((char *)iface - offsetof( struct http_headers, vtbl_map ));
}

static HRESULT WINAPI headers_QueryInterface( IHttpHeaderCollection *This, REFIID iid, void **out )
{
    struct http_headers *impl = (struct http_headers *)This;
    WDBG( "headers_QI {%08x-%04x-%04x-%02x%02x}\n", (unsigned)iid->Data1, iid->Data2, iid->Data3,
          iid->Data4[6], iid->Data4[7] );
    if (!iid)
    {
        *out = This;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    /* IMapView`2<K,V> view (parameterized IID base e9bdaaf0-cbf6-*) over the
     * same pair storage; WPM reads the response headers through this shape. */
    if (iid->Data1 == 0xe9bdaaf0)
    {
        *out = &impl->vtbl_map;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (iid->Data1 == 0xf6d1f700)
    {
        /* WPM's HttpClientWrapper QIs the headers face for
         * IAsyncOperation`1<IBuffer> (GetContentLengthAsync shape); answering
         * with the headers pointer makes it virtual-call into string data.
         * Serve a real async op whose result is an IBuffer over the
         * Content-Length value. */
        HSTRING val = NULL;
        void *buffer = NULL, *op = NULL;
        HRESULT hr;
        UINT32 i;
        (void)val; (void)buffer; (void)op; (void)hr; (void)i;
        *out = This;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = This;
    InterlockedIncrement( &impl->ref );
    return S_OK;
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
static HRESULT WINAPI headers_GetIids( IHttpHeaderCollection *This, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI headers_GetRuntimeClassName( IHttpHeaderCollection *This, HSTRING *n ) { return E_NOTIMPL; }
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
    struct http_headers *impl = (struct http_headers *)This;
    TRACE( "name %s, value %s\n", debugstr_hstring( name ), debugstr_hstring( value ) );
    if (name && value)
    {
        WindowsDeleteString( impl->last_name );
        WindowsDeleteString( impl->last_value );
        WindowsDuplicateString( name, &impl->last_name );
        WindowsDuplicateString( value, &impl->last_value );
    }
    if (succeeded) *succeeded = TRUE;
    return S_OK;
}
static HRESULT WINAPI headers_stub_ok( void *iface ) { TRACE( "headers_stub_ok\n" ); return S_OK; }
static HRESULT WINAPI headers_stub_null( void *iface, void **out ) { TRACE( "headers_stub_null\n" ); if (out) *out = NULL; return S_OK; }

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
    headers_stub_null,
    headers_stub_null,
    headers_stub_null,
    headers_stub_null,
    headers_Get,
    headers_stub_null,
    headers_stub_null,
    headers_stub_null,
    headers_stub_null,
    headers_stub_ok,
    headers_stub_null,
    headers_stub_null,
    headers_stub_ok,
    headers_stub_null,
    headers_stub_ok,
    headers_stub_null,
    headers_stub_ok,
    headers_stub_null,
    headers_stub_ok,
    headers_stub_null,
    headers_stub_ok,
    headers_stub_null,
    headers_stub_ok,
    headers_stub_null,
    headers_Get,
    headers_Get,
    headers_stub_null,
    headers_stub_null,
    headers_stub_null,
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
    WDBG( "kvp_QI {%08x-%04x-%04x-%02x%02x}\n", (unsigned)iid->Data1, iid->Data2, iid->Data3,
          iid->Data4[6], iid->Data4[7] );
    *out = This;
    ((struct hdr_kvp *)This)->ref++;
    return S_OK;
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
const struct http_headers_map_vtbl http_headers_map_vtbl = {
  {
    (HRESULT (STDMETHODCALLTYPE *)(IUnknown *, REFIID, void **))headers_QueryInterface,
    (ULONG (STDMETHODCALLTYPE *)(IUnknown *))headers_AddRef,
    (ULONG (STDMETHODCALLTYPE *)(IUnknown *))headers_Release
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
