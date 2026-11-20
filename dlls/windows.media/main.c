/* WinRT Windows.Media Implementation
 *
 * Copyright (C) 2022 Mohamad Al-Jaf
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

#include "initguid.h"
#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(media);

/* ============================================================================
 * Windows.Media.Core.CodecQuery
 * ============================================================================ */

static const GUID IID_ICodecQuery_local =
    { 0x222a953a, 0xaf61, 0x4e04, {0x80, 0x8a, 0xa4, 0x63, 0x4e, 0x2f, 0x3a, 0xc4} };
static const GUID IID_IVectorView_CodecInfo =
    { 0x1fd24a93, 0xc3da, 0x513d, {0xb2, 0x16, 0x0c, 0x4c, 0x7f, 0x41, 0x2b, 0x88} };
static const GUID IID_IIterable_CodecInfo =
    { 0x552f7709, 0x3bc3, 0x59fb, {0x93, 0xe6, 0x1e, 0xbd, 0x28, 0xd3, 0xc0, 0x08} };
static const GUID IID_IIterator_CodecInfo =
    { 0xd4141d5b, 0x23f6, 0x564b, {0xb8, 0x5c, 0x5d, 0x46, 0x09, 0xf8, 0x18, 0x63} };
static const GUID IID_IAsyncOperation_IVectorView_CodecInfo =
    { 0xc1c59efc, 0x71df, 0x5534, {0xb0, 0x3f, 0xe1, 0x8a, 0x8f, 0xbe, 0x78, 0xbb} };
static const GUID IID_IAsyncInfo_local =
    { 0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46} };

static const GUID IID_ICodecInfo_local =
    { 0x51e89f85, 0xea97, 0x499c, {0x86, 0xac, 0x4c, 0xe5, 0xe7, 0x3f, 0x3a, 0x42} };

struct codec_info { const void *lpVtbl; LONG ref; };
static HRESULT WINAPI cinfo_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICodecInfo_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct codec_info *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cinfo_AddRef( void *iface ) { return InterlockedIncrement( &((struct codec_info *)iface)->ref ); }
static ULONG WINAPI cinfo_Release( void *iface )
{
    struct codec_info *impl = (struct codec_info *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI cinfo_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI cinfo_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Media.Core.CodecInfo";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cinfo_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cinfo_get_Kind( void *iface, UINT32 *v ) { if (v) *v = 0; return S_OK; }
static HRESULT WINAPI cinfo_get_Category( void *iface, UINT32 *v ) { if (v) *v = 0; return S_OK; }
static HRESULT WINAPI cinfo_get_Subtypes( void *iface, void **v ) { if (v) *v = NULL; return S_OK; }
static HRESULT WINAPI cinfo_get_DisplayName( void *iface, HSTRING *n )
{
    static const WCHAR name[] = L"H.264 Video Decoder";
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, n );
}
static HRESULT WINAPI cinfo_get_IsTrusted( void *iface, boolean *v ) { if (v) *v = TRUE; return S_OK; }
static const void * const cinfo_vtbl[] =
{
    cinfo_QI, cinfo_AddRef, cinfo_Release, cinfo_GetIids, cinfo_GetRuntimeClassName, cinfo_GetTrustLevel,
    cinfo_get_Kind, cinfo_get_Category, cinfo_get_Subtypes, cinfo_get_DisplayName, cinfo_get_IsTrusted,
};
static HRESULT codec_info_create( void **out )
{
    struct codec_info *ci;
    if (!out) return E_POINTER;
    if (!(ci = calloc( 1, sizeof(*ci) ))) return E_OUTOFMEMORY;
    ci->lpVtbl = cinfo_vtbl;
    ci->ref = 1;
    *out = ci;
    return S_OK;
}

struct codec_iter { const void *lpVtbl; LONG ref; UINT32 idx; };
static HRESULT WINAPI ci_iter_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterator_CodecInfo ))
    {
        *out = iface;
        InterlockedIncrement( &((struct codec_iter *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ci_iter_AddRef( void *iface ) { return InterlockedIncrement( &((struct codec_iter *)iface)->ref ); }
static ULONG WINAPI ci_iter_Release( void *iface )
{
    struct codec_iter *impl = (struct codec_iter *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI ci_iter_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ci_iter_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Media.Core.CodecInfo>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ci_iter_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ci_iter_get_Current( void *iface, void **cur )
{
    struct codec_iter *impl = (struct codec_iter *)iface;
    if (!cur) return E_POINTER;
    if (impl->idx >= 1) { *cur = NULL; return E_BOUNDS; }
    return codec_info_create( cur );
}
static HRESULT WINAPI ci_iter_get_HasCurrent( void *iface, boolean *has )
{
    if (has) *has = (((struct codec_iter *)iface)->idx < 1);
    return S_OK;
}
static HRESULT WINAPI ci_iter_MoveNext( void *iface, boolean *has )
{
    struct codec_iter *impl = (struct codec_iter *)iface;
    if (impl->idx < 1) impl->idx++;
    if (has) *has = (impl->idx < 1);
    return S_OK;
}
static HRESULT WINAPI ci_iter_GetMany( void *iface, UINT32 cap, void **items, UINT32 *actual )
{
    struct codec_iter *impl = (struct codec_iter *)iface;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (cap >= 1 && impl->idx == 0)
    {
        HRESULT hr = codec_info_create( &items[0] );
        if (FAILED(hr)) return hr;
        impl->idx = 1;
        *actual = 1;
    }
    return S_OK;
}
static const void * const ci_iter_vtbl[] =
{
    ci_iter_QI, ci_iter_AddRef, ci_iter_Release, ci_iter_GetIids, ci_iter_GetRuntimeClassName, ci_iter_GetTrustLevel,
    ci_iter_get_Current, ci_iter_get_HasCurrent, ci_iter_MoveNext, ci_iter_GetMany,
};

struct codec_vec_view
{
    const void *lpVtbl_view;
    const void *lpVtbl_iterable;
    LONG ref;
};
static HRESULT WINAPI cvv_QI( void *iface, REFIID iid, void **out )
{
    struct codec_vec_view *impl = CONTAINING_RECORD( iface, struct codec_vec_view, lpVtbl_view );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IVectorView_CodecInfo ))
    {
        *out = &impl->lpVtbl_view;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IIterable_CodecInfo ))
    {
        *out = &impl->lpVtbl_iterable;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cvv_AddRef( void *iface )
{
    struct codec_vec_view *impl = CONTAINING_RECORD( iface, struct codec_vec_view, lpVtbl_view );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cvv_Release( void *iface )
{
    struct codec_vec_view *impl = CONTAINING_RECORD( iface, struct codec_vec_view, lpVtbl_view );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI cvv_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI cvv_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.Media.Core.CodecInfo>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cvv_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cvv_GetAt( void *iface, UINT32 idx, void **out )
{
    if (!out) return E_POINTER;
    if (idx != 0) { *out = NULL; return E_BOUNDS; }
    return codec_info_create( out );
}
static HRESULT WINAPI cvv_get_Size( void *iface, UINT32 *size ) { if (size) *size = 1; return S_OK; }
static HRESULT WINAPI cvv_IndexOf( void *iface, void *val, UINT32 *idx, boolean *found ) { if (idx) *idx = 0; if (found) *found = FALSE; return S_OK; }
static HRESULT WINAPI cvv_GetMany( void *iface, UINT32 start, UINT32 cap, void **items, UINT32 *actual ) { if (actual) *actual = 0; return S_OK; }

static HRESULT WINAPI cvv_it_QI( void *iface, REFIID iid, void **out )
{
    struct codec_vec_view *impl = CONTAINING_RECORD( iface, struct codec_vec_view, lpVtbl_iterable );
    return cvv_QI( &impl->lpVtbl_view, iid, out );
}
static ULONG WINAPI cvv_it_AddRef( void *iface )
{
    struct codec_vec_view *impl = CONTAINING_RECORD( iface, struct codec_vec_view, lpVtbl_iterable );
    return cvv_AddRef( &impl->lpVtbl_view );
}
static ULONG WINAPI cvv_it_Release( void *iface )
{
    struct codec_vec_view *impl = CONTAINING_RECORD( iface, struct codec_vec_view, lpVtbl_iterable );
    return cvv_Release( &impl->lpVtbl_view );
}
static HRESULT WINAPI cvv_it_First( void *iface, void **out )
{
    struct codec_iter *it;
    if (!out) return E_POINTER;
    if (!(it = calloc( 1, sizeof(*it) ))) return E_OUTOFMEMORY;
    it->lpVtbl = ci_iter_vtbl;
    it->ref = 1;
    *out = it;
    return S_OK;
}
static const void * const cvv_view_vtbl[] =
{
    cvv_QI, cvv_AddRef, cvv_Release, cvv_GetIids, cvv_GetRuntimeClassName, cvv_GetTrustLevel,
    cvv_GetAt, cvv_get_Size, cvv_IndexOf, cvv_GetMany,
};
static const void * const cvv_iter_vtbl[] =
{
    cvv_it_QI, cvv_it_AddRef, cvv_it_Release, cvv_GetIids, cvv_GetRuntimeClassName, cvv_GetTrustLevel,
    cvv_it_First,
};

struct codec_async_op
{
    const void *lpVtbl_op;
    const void *lpVtbl_info;
    LONG ref;
    void *completed_handler;
};
static HRESULT WINAPI cao_QI( void *iface, REFIID iid, void **out )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_op );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncOperation_IVectorView_CodecInfo ))
    {
        *out = &impl->lpVtbl_op;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_local ))
    {
        *out = &impl->lpVtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cao_AddRef( void *iface )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_op );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI cao_Release( void *iface )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_op );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->completed_handler) IUnknown_Release( (IUnknown *)impl->completed_handler );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI cao_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI cao_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Media.Core.CodecInfo>>";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cao_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cao_put_Completed( void *iface, void *handler )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_op );
    if (impl->completed_handler) IUnknown_Release( (IUnknown *)impl->completed_handler );
    impl->completed_handler = handler;
    if (handler)
    {
        HRESULT (WINAPI *invoke)(void *, void *, UINT32);
        IUnknown_AddRef( (IUnknown *)handler );
        invoke = (void *)(*(void ***)handler)[3];
        if (invoke) invoke( handler, &impl->lpVtbl_op, 1 /* Completed */ );
    }
    return S_OK;
}
static HRESULT WINAPI cao_get_Completed( void *iface, void **handler )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_op );
    if (!handler) return E_POINTER;
    *handler = impl->completed_handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}
static HRESULT WINAPI cao_GetResults( void *iface, void **out )
{
    struct codec_vec_view *v;
    if (!out) return E_POINTER;
    if (!(v = calloc( 1, sizeof(*v) ))) return E_OUTOFMEMORY;
    v->lpVtbl_view = cvv_view_vtbl;
    v->lpVtbl_iterable = cvv_iter_vtbl;
    v->ref = 1;
    *out = &v->lpVtbl_view;
    return S_OK;
}
static HRESULT WINAPI cao_info_QI( void *iface, REFIID iid, void **out )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_info );
    return cao_QI( &impl->lpVtbl_op, iid, out );
}
static ULONG WINAPI cao_info_AddRef( void *iface )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_info );
    return cao_AddRef( &impl->lpVtbl_op );
}
static ULONG WINAPI cao_info_Release( void *iface )
{
    struct codec_async_op *impl = CONTAINING_RECORD( iface, struct codec_async_op, lpVtbl_info );
    return cao_Release( &impl->lpVtbl_op );
}
static HRESULT WINAPI cao_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI cao_info_get_Status( void *iface, UINT32 *st ) { if (st) *st = 1; /* Completed */ return S_OK; }
static HRESULT WINAPI cao_info_get_ErrorCode( void *iface, HRESULT *hr ) { if (hr) *hr = S_OK; return S_OK; }
static HRESULT WINAPI cao_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI cao_info_Close( void *iface ) { return S_OK; }

static const void * const cao_op_vtbl[] =
{
    cao_QI, cao_AddRef, cao_Release, cao_GetIids, cao_GetRuntimeClassName, cao_GetTrustLevel,
    cao_put_Completed, cao_get_Completed, cao_GetResults,
};
static const void * const cao_info_vtbl[] =
{
    cao_info_QI, cao_info_AddRef, cao_info_Release, cao_GetIids, cao_GetRuntimeClassName, cao_GetTrustLevel,
    cao_info_get_Id, cao_info_get_Status, cao_info_get_ErrorCode, cao_info_Cancel, cao_info_Close,
};

struct codec_query { const void *lpVtbl; LONG ref; };
static HRESULT WINAPI cq_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICodecQuery_local ))
    {
        *out = iface;
        InterlockedIncrement( &((struct codec_query *)iface)->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cq_AddRef( void *iface ) { return InterlockedIncrement( &((struct codec_query *)iface)->ref ); }
static ULONG WINAPI cq_Release( void *iface )
{
    struct codec_query *impl = (struct codec_query *)iface;
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r) free( impl );
    return r;
}
static HRESULT WINAPI cq_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_ICodecQuery_local;
    *c = 1;
    return S_OK;
}
static HRESULT WINAPI cq_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Media.Core.CodecQuery";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cq_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cq_FindAllAsync( void *iface, UINT32 kind, UINT32 category, HSTRING sub_type, void **op )
{
    struct codec_async_op *impl;
    if (!op) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl_op = cao_op_vtbl;
    impl->lpVtbl_info = cao_info_vtbl;
    impl->ref = 1;
    *op = &impl->lpVtbl_op;
    return S_OK;
}
static const void * const cq_vtbl[] =
{
    cq_QI, cq_AddRef, cq_Release, cq_GetIids, cq_GetRuntimeClassName, cq_GetTrustLevel,
    cq_FindAllAsync,
};

static HRESULT WINAPI cq_fac_QI( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cq_fac_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI cq_fac_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI cq_fac_GetIids( IActivationFactory *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI cq_fac_GetRuntimeClassName( IActivationFactory *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Media.Core.CodecQuery";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cq_fac_GetTrustLevel( IActivationFactory *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cq_fac_ActivateInstance( IActivationFactory *iface, IInspectable **out )
{
    struct codec_query *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = cq_vtbl;
    impl->ref = 1;
    *out = (IInspectable *)impl;
    return S_OK;
}
static const IActivationFactoryVtbl cq_fac_vtbl =
{
    cq_fac_QI, cq_fac_AddRef, cq_fac_Release,
    cq_fac_GetIids, cq_fac_GetRuntimeClassName, cq_fac_GetTrustLevel,
    cq_fac_ActivateInstance,
};
static IActivationFactory codec_query_factory = { &cq_fac_vtbl };

/* ============================================================================
 * Windows.Media.Core.CodecSubtypes (ICodecSubtypesStatics)
 * ============================================================================ */

static const GUID IID_ICodecSubtypesStatics_local =
    { 0xa66ac4f2, 0x888b, 0x4224, {0x8c, 0xf6, 0x2a, 0x8d, 0x4e, 0xb0, 0x23, 0x82} };

struct codec_subtypes_factory
{
    const void *lpVtbl_factory;
    const void *lpVtbl_statics;
};
static struct codec_subtypes_factory codec_subtypes_factory_obj;

static HRESULT WINAPI cs_fac_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &codec_subtypes_factory_obj.lpVtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICodecSubtypesStatics_local ))
    {
        *out = &codec_subtypes_factory_obj.lpVtbl_statics;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cs_fac_AddRef( void *iface ) { return 2; }
static ULONG WINAPI cs_fac_Release( void *iface ) { return 1; }
static HRESULT WINAPI cs_fac_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI cs_fac_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Media.Core.CodecSubtypes";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI cs_fac_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI cs_fac_ActivateInstance( void *iface, IInspectable **out ) { if (out) *out = NULL; return E_NOTIMPL; }

static HRESULT WINAPI cs_get_generic( void *iface, HSTRING *out )
{
    static const WCHAR s[] = L"H264";
    if (!out) return E_POINTER;
    return WindowsCreateString( s, ARRAY_SIZE(s) - 1, out );
}

static const void * const cs_fac_vtbl[] =
{
    cs_fac_QI, cs_fac_AddRef, cs_fac_Release,
    cs_fac_GetIids, cs_fac_GetRuntimeClassName, cs_fac_GetTrustLevel,
    cs_fac_ActivateInstance,
};
static const void * const cs_st_vtbl[56] =
{
    [0] = cs_fac_QI,
    [1] = cs_fac_AddRef,
    [2] = cs_fac_Release,
    [3] = cs_fac_GetIids,
    [4] = cs_fac_GetRuntimeClassName,
    [5] = cs_fac_GetTrustLevel,
    [6 ... 55] = cs_get_generic,
};
static struct codec_subtypes_factory codec_subtypes_factory_obj = { cs_fac_vtbl, cs_st_vtbl };

/* ============================================================================
 * Windows.Media.Core.MediaSource (IMediaSourceStatics, IMediaSource2/3/4,
 *                                 IMediaPlaybackSource, IClosable)
 * ============================================================================ */

static const GUID IID_IMediaSourceStatics_local =
    { 0xf77d6fa4, 0x4652, 0x410e, {0xb1, 0xd8, 0xe9, 0xa5, 0xe2, 0x45, 0xa4, 0x5c} };
static const GUID IID_IMediaSourceStatics2_local =
    { 0xeee161a4, 0x7f13, 0x4896, {0xb8, 0xcb, 0xdf, 0x0d, 0xe5, 0xbc, 0xb9, 0xf1} };
static const GUID IID_IMediaSource2_local =
    { 0x2eb61048, 0x655f, 0x4c37, {0xb8, 0x13, 0xb4, 0xe4, 0x5d, 0xfa, 0x0a, 0xbe} };
static const GUID IID_IMediaSource3_local =
    { 0xb59f0d9b, 0x4b6e, 0x41ed, {0xbb, 0xb4, 0x7c, 0x75, 0x09, 0xa9, 0x94, 0xad} };
static const GUID IID_IMediaSource4_local =
    { 0xbdafad57, 0x8eff, 0x4c63, {0x85, 0xa6, 0x84, 0xde, 0x0a, 0xe3, 0xe4, 0xf2} };
static const GUID IID_IMediaPlaybackSource_local =
    { 0xef9dc2bc, 0x9317, 0x4696, {0xb0, 0x51, 0x2b, 0xad, 0x64, 0x31, 0x77, 0xb5} };
static const GUID IID_IClosable_local =
    { 0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e} };

struct media_source
{
    const void *lpVtbl_ms2;
    const void *lpVtbl_ms3;
    const void *lpVtbl_ms4;
    const void *lpVtbl_pb;
    const void *lpVtbl_close;
    LONG ref;
    IInspectable *uri;
    INT32 state;
};

static HRESULT WINAPI ms_QI_impl( struct media_source *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IMediaSource2_local ))
    {
        *out = &impl->lpVtbl_ms2;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMediaSource3_local ))
    {
        *out = &impl->lpVtbl_ms3;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMediaSource4_local ))
    {
        *out = &impl->lpVtbl_ms4;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMediaPlaybackSource_local ))
    {
        *out = &impl->lpVtbl_pb;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IClosable_local ))
    {
        *out = &impl->lpVtbl_close;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ms_AddRef_impl( struct media_source *impl )
{
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI ms_Release_impl( struct media_source *impl )
{
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->uri) IInspectable_Release( impl->uri );
        free( impl );
    }
    return r;
}

static HRESULT WINAPI ms_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( 5 * sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_IMediaSource2_local;
    (*i)[1] = IID_IMediaSource3_local;
    (*i)[2] = IID_IMediaSource4_local;
    (*i)[3] = IID_IMediaPlaybackSource_local;
    (*i)[4] = IID_IClosable_local;
    *c = 5;
    return S_OK;
}
static HRESULT WINAPI ms_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Media.Core.MediaSource";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ms_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }

#define DEFINE_MS_FACE_IUNKNOWN( prefix, field ) \
static HRESULT WINAPI prefix##_QI( void *iface, REFIID iid, void **out ) \
{ \
    return ms_QI_impl( CONTAINING_RECORD( iface, struct media_source, field ), iid, out ); \
} \
static ULONG WINAPI prefix##_AddRef( void *iface ) \
{ \
    return ms_AddRef_impl( CONTAINING_RECORD( iface, struct media_source, field ) ); \
} \
static ULONG WINAPI prefix##_Release( void *iface ) \
{ \
    return ms_Release_impl( CONTAINING_RECORD( iface, struct media_source, field ) ); \
}

DEFINE_MS_FACE_IUNKNOWN( ms2, lpVtbl_ms2 )
DEFINE_MS_FACE_IUNKNOWN( ms3, lpVtbl_ms3 )
DEFINE_MS_FACE_IUNKNOWN( ms4, lpVtbl_ms4 )
DEFINE_MS_FACE_IUNKNOWN( mspb, lpVtbl_pb )
DEFINE_MS_FACE_IUNKNOWN( mscl, lpVtbl_close )

static HRESULT WINAPI ms2_add_OpenOperationCompleted( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}
static HRESULT WINAPI ms2_remove_OpenOperationCompleted( void *iface, EventRegistrationToken tok ) { return S_OK; }
static HRESULT WINAPI ms2_get_CustomProperties( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ms2_get_Duration( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ms2_get_IsOpen( void *iface, boolean *out ) { if (out) *out = TRUE; return S_OK; }
static HRESULT WINAPI ms2_get_ExternalTimedTextSources( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ms2_get_ExternalTimedMetadataTracks( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }

static HRESULT WINAPI ms3_add_StateChanged( void *iface, void *handler, EventRegistrationToken *tok )
{
    if (tok) tok->value = 1;
    return S_OK;
}
static HRESULT WINAPI ms3_remove_StateChanged( void *iface, EventRegistrationToken tok ) { return S_OK; }
static HRESULT WINAPI ms3_get_State( void *iface, INT32 *out )
{
    struct media_source *impl = CONTAINING_RECORD( iface, struct media_source, lpVtbl_ms3 );
    if (out) *out = impl->state;
    return S_OK;
}
static HRESULT WINAPI ms3_Reset( void *iface ) { return S_OK; }

static HRESULT WINAPI ms4_get_AdaptiveMediaSource( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ms4_get_MediaStreamSource( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ms4_get_MseStreamSource( void *iface, void **out ) { if (out) *out = NULL; return S_OK; }
static HRESULT WINAPI ms4_get_Uri( void *iface, void **out )
{
    struct media_source *impl = CONTAINING_RECORD( iface, struct media_source, lpVtbl_ms4 );
    if (!out) return E_POINTER;
    *out = impl->uri;
    if (impl->uri) IInspectable_AddRef( impl->uri );
    return S_OK;
}
static HRESULT WINAPI ms4_OpenAsync( void *iface, void **out )
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI mscl_Close( void *iface )
{
    struct media_source *impl = CONTAINING_RECORD( iface, struct media_source, lpVtbl_close );
    impl->state = 4; /* Closed */
    return S_OK;
}

static const void * const ms2_vtbl[] =
{
    ms2_QI, ms2_AddRef, ms2_Release, ms_GetIids, ms_GetRuntimeClassName, ms_GetTrustLevel,
    ms2_add_OpenOperationCompleted, ms2_remove_OpenOperationCompleted,
    ms2_get_CustomProperties, ms2_get_Duration, ms2_get_IsOpen,
    ms2_get_ExternalTimedTextSources, ms2_get_ExternalTimedMetadataTracks,
};
static const void * const ms3_vtbl[] =
{
    ms3_QI, ms3_AddRef, ms3_Release, ms_GetIids, ms_GetRuntimeClassName, ms_GetTrustLevel,
    ms3_add_StateChanged, ms3_remove_StateChanged, ms3_get_State, ms3_Reset,
};
static const void * const ms4_vtbl[] =
{
    ms4_QI, ms4_AddRef, ms4_Release, ms_GetIids, ms_GetRuntimeClassName, ms_GetTrustLevel,
    ms4_get_AdaptiveMediaSource, ms4_get_MediaStreamSource, ms4_get_MseStreamSource,
    ms4_get_Uri, ms4_OpenAsync,
};
static const void * const mspb_vtbl[] =
{
    mspb_QI, mspb_AddRef, mspb_Release, ms_GetIids, ms_GetRuntimeClassName, ms_GetTrustLevel,
};
static const void * const mscl_vtbl[] =
{
    mscl_QI, mscl_AddRef, mscl_Release, ms_GetIids, ms_GetRuntimeClassName, ms_GetTrustLevel,
    mscl_Close,
};

static HRESULT media_source_create( IInspectable *uri, void **out )
{
    struct media_source *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl_ms2 = ms2_vtbl;
    impl->lpVtbl_ms3 = ms3_vtbl;
    impl->lpVtbl_ms4 = ms4_vtbl;
    impl->lpVtbl_pb = mspb_vtbl;
    impl->lpVtbl_close = mscl_vtbl;
    impl->ref = 1;
    impl->state = 2; /* Opened */
    impl->uri = uri;
    if (impl->uri) IInspectable_AddRef( impl->uri );
    *out = &impl->lpVtbl_ms2;
    return S_OK;
}

struct media_source_factory
{
    const void *lpVtbl_factory;
    const void *lpVtbl_statics;
    const void *lpVtbl_statics2;
};
static struct media_source_factory media_source_factory_obj;

static HRESULT WINAPI ms_fac_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &media_source_factory_obj.lpVtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMediaSourceStatics_local ))
    {
        *out = &media_source_factory_obj.lpVtbl_statics;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMediaSourceStatics2_local ))
    {
        *out = &media_source_factory_obj.lpVtbl_statics2;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ms_fac_AddRef( void *iface ) { return 2; }
static ULONG WINAPI ms_fac_Release( void *iface ) { return 1; }
static HRESULT WINAPI ms_fac_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ms_fac_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Media.Core.MediaSource";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI ms_fac_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI ms_fac_ActivateInstance( void *iface, IInspectable **out ) { if (out) *out = NULL; return E_NOTIMPL; }

static HRESULT WINAPI ms_st_CreateFromOne( void *iface, void *arg, void **out )
{
    return media_source_create( NULL, out );
}
static HRESULT WINAPI ms_st_CreateFromStream( void *iface, void *stream, HSTRING content_type, void **out )
{
    return media_source_create( NULL, out );
}
static HRESULT WINAPI ms_st_CreateFromUri( void *iface, IInspectable *uri, void **out )
{
    return media_source_create( uri, out );
}

static const void * const ms_fac_vtbl[] =
{
    ms_fac_QI, ms_fac_AddRef, ms_fac_Release,
    ms_fac_GetIids, ms_fac_GetRuntimeClassName, ms_fac_GetTrustLevel,
    ms_fac_ActivateInstance,
};
static const void * const ms_st_vtbl[] =
{
    ms_fac_QI, ms_fac_AddRef, ms_fac_Release,
    ms_fac_GetIids, ms_fac_GetRuntimeClassName, ms_fac_GetTrustLevel,
    ms_st_CreateFromOne,    /* 6: CreateFromAdaptiveMediaSource */
    ms_st_CreateFromOne,    /* 7: CreateFromMediaStreamSource */
    ms_st_CreateFromOne,    /* 8: CreateFromMseStreamSource */
    ms_st_CreateFromOne,    /* 9: CreateFromIMediaSource */
    ms_st_CreateFromOne,    /* 10: CreateFromStorageFile */
    ms_st_CreateFromStream, /* 11: CreateFromStream */
    ms_st_CreateFromStream, /* 12: CreateFromStreamReference */
    ms_st_CreateFromUri,    /* 13: CreateFromUri */
};
static const void * const ms_st2_vtbl[] =
{
    ms_fac_QI, ms_fac_AddRef, ms_fac_Release,
    ms_fac_GetIids, ms_fac_GetRuntimeClassName, ms_fac_GetTrustLevel,
    ms_st_CreateFromOne,    /* 6: CreateFromMediaBinder */
};
static struct media_source_factory media_source_factory_obj = { ms_fac_vtbl, ms_st_vtbl, ms_st2_vtbl };

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring(classid), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Media_ClosedCaptioning_ClosedCaptionProperties ))
        IActivationFactory_QueryInterface( captions_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, RuntimeClass_Windows_Media_Transcoding_MediaTranscoder ))
        IActivationFactory_QueryInterface( media_transcoder_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, L"Windows.Media.Editing.MediaComposition" ))
        IActivationFactory_QueryInterface( media_composition_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, L"Windows.Media.Editing.MediaClip" ))
        IActivationFactory_QueryInterface( media_clip_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, L"Windows.Media.Audio.AudioGraph" ))
        IActivationFactory_QueryInterface( audio_graph_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, L"Windows.Media.Audio.AudioGraphSettings" ))
        IActivationFactory_QueryInterface( audio_graph_settings_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, L"Windows.Media.Core.CodecQuery" ))
        IActivationFactory_QueryInterface( &codec_query_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, L"Windows.Media.Core.CodecSubtypes" ))
        IActivationFactory_QueryInterface( (IActivationFactory *)&codec_subtypes_factory_obj.lpVtbl_factory, &IID_IActivationFactory, (void **)factory );
    if (!wcscmp( buffer, L"Windows.Media.Core.MediaSource" ))
        IActivationFactory_QueryInterface( (IActivationFactory *)&media_source_factory_obj.lpVtbl_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}
