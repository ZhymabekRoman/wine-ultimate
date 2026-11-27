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

static HRESULT WINAPI async_http_info_get_Id( IAsyncInfo *This, UINT32 *id );
static HRESULT WINAPI async_http_info_get_Status( IAsyncInfo *This, AsyncStatus *status );
static HRESULT WINAPI async_http_info_get_ErrorCode( IAsyncInfo *This, HRESULT *code );
static HRESULT WINAPI async_http_info_Cancel( IAsyncInfo *This );
static HRESULT WINAPI async_http_info_Close( IAsyncInfo *This );


/* --- Async Operation with Progress for HTTP --- */


static inline struct async_http_op *impl_from_async_op( IAsyncInfo *This )
{
    return (struct async_http_op *)This;
}

static inline struct async_http_op *impl_from_async_info( IAsyncInfo *This )
{
    return (struct async_http_op *)((BYTE *)This - sizeof(void *));
}

static HRESULT WINAPI async_http_op_QueryInterface( IAsyncInfo *This, REFIID iid, void **out )
{
    static const GUID IID_IAsyncOpWithProg_IBuffer_UINT32 =
        { 0xd26b2819, 0x897f, 0x5c7d, { 0x84, 0xd6, 0x56, 0xd7, 0x96, 0x56, 0x14, 0x31 } };
    static const GUID IID_IAsyncOpWithProg_UINT64_UINT64 =
        { 0x8f1db6e3, 0x6556, 0x5516, { 0xb2, 0x5c, 0x10, 0x21, 0xee, 0x27, 0xcd, 0x0c } };
    struct async_http_op *impl = impl_from_async_op( This );

    WDBG( "op_QI {%08x-%04x-%04x-%02x%02x}\n", (unsigned)iid->Data1, iid->Data2, iid->Data3, (unsigned)iid->Data4[6], (unsigned)iid->Data4[7] );

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

    /* instantiated parameterized async faces */
    if (IsEqualGUID( iid, &IID___FIAsyncOperationWithProgress_2_Windows__CWeb__CHttp__CHttpResponseMessage_HttpProgress ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIInputStream_HttpProgress ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperationWithProgress_2_HSTRING_HttpProgress ) ||
        IsEqualGUID( iid, &IID_IAsyncOpWithProg_IBuffer_UINT32 ) ||
        IsEqualGUID( iid, &IID_IAsyncOpWithProg_UINT64_UINT64 ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperation_1_Windows__CWeb__CHttp__CHttpResponseMessage ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperation_1_Windows__CStorage__CStreams__CIBuffer ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperation_1_Windows__CStorage__CStreams__CIInputStream ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperation_1_HSTRING ))
    {
        *out = &impl->vtbl_op;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_http_op_AddRef( IAsyncInfo *This )
{
    struct async_http_op *impl = impl_from_async_op( This );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI async_http_op_Release( IAsyncInfo *This )
{
    struct async_http_op *impl = impl_from_async_op( This );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->result_obj) IInspectable_Release( impl->result_obj );
        if (impl->result_str) WindowsDeleteString( impl->result_str );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI async_http_op_GetIids( IAsyncInfo *This, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI async_http_op_GetRuntimeClassName( IAsyncInfo *This, HSTRING *name )
{
    static const WCHAR str_name[] = L"Windows.Foundation.IAsyncOperationWithProgress`2<String, UInt64>";
    static const WCHAR resp_name[] = L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Web.Http.HttpResponseMessage, Windows.Web.Http.HttpProgress>";
    struct async_http_op *impl = impl_from_async_op( This );
    if (!name) return E_POINTER;
    if (impl->is_str)
        return WindowsCreateString( str_name, ARRAY_SIZE(str_name) - 1, name );
    return WindowsCreateString( resp_name, ARRAY_SIZE(resp_name) - 1, name );
}
static HRESULT WINAPI async_http_op_GetTrustLevel( IAsyncInfo *This, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }


static HRESULT WINAPI async_http_op_put_Completed( IAsyncInfo *This, void *handler )
{
    struct async_http_op *impl = impl_from_async_op( This );
    WDBG( "async put_Completed handler %p off10 %d\n", handler, impl->handler_at_0x10 );
    /* The completed handler is { QueryInterface, AddRef, Release, Invoke }.
     * Plain delegates keep the vtbl at +0; WindowsPackageManager's C++/WinRT
     * coroutine awaiter (async_base) is passed as the awaiter object whose
     * handler subobject (and vtbl) lives at +0x10. */
    if (handler && !impl->handler_at_0x10)
    {
        __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler *h =
                (__x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler *)handler;
        h->lpVtbl->Invoke( h, (__x_ABI_CWindows_CFoundation_CIAsyncAction *)This,
                           1 /* Completed */ );
    }
    return S_OK;
}

static HRESULT WINAPI async_http_op_get_Completed( IAsyncInfo *This, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}

static HRESULT WINAPI async_http_op_GetResults2( IAsyncInfo *This, void *results );
static HRESULT WINAPI async_http_op_GetResults( IAsyncInfo *This, void *results )
{
    struct async_http_op *impl = impl_from_async_op( This );
    WDBG( "async GetResults results %p is_str %d\n", results, impl->is_str );

    if (!results) return E_POINTER;
    if (impl->is_u64)
    {
        *(UINT64 *)results = impl->result_u64;
        return S_OK;
    }
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

/* Win11 projection GetResults for UINT32-result ops (ReadAsync pumps): the
 * result object is an IBuffer whose Length is the byte count. */
static HRESULT WINAPI async_http_op_GetResults2( IAsyncInfo *This, void *results )
{
    struct async_http_op *impl = impl_from_async_op( This );
    ULONG *out = results;
    ULONG len = 0;

    if (!results) return E_POINTER;
    if (impl->result_obj)
        ((HRESULT (WINAPI *)( void *, ULONG * ))((void *const *)(*(void *const **)impl->result_obj))[6])(
                impl->result_obj, &len );
    *out = len;
    return S_OK;
}

/* Merged IAsyncOperationWithProgress`2<TProgress,TResult> ABI (Win11 22621
 * projection, used by WindowsPackageManager.dll): slots 6-10 = IAsyncInfo,
 * 11-15 = v1 methods, 16 = get_Results (v2 addition, same shape as
 * GetResults). Callers index the op face directly, so the IAsyncInfo
 * implementations must be reachable from this vtable too. */

static HRESULT WINAPI async_http_op_get_Id( IAsyncInfo *This, UINT32 *id )
{
    /* Slot 6 of IAsyncOperationWithProgress is put_Progress(handler); do NOT
     * overwrite *id (which would clobber the handler's vtable pointer). */
    WDBG( "async_http_op slot6 (put_Progress / get_Id) arg %p\n", id );
    return S_OK;
}
static HRESULT WINAPI async_http_op_get_Status( IAsyncInfo *This, AsyncStatus *status )
{
    return async_http_info_get_Status( (IAsyncInfo *)((BYTE *)This + sizeof(void *)), status );
}

static const struct async_op_vtbl async_http_op_vtbl = {
  {
    async_http_op_QueryInterface,
    async_http_op_AddRef,
    async_http_op_Release,
    async_http_op_GetIids,
    async_http_op_GetRuntimeClassName,
    async_http_op_GetTrustLevel,
    async_http_op_get_Id,
    /* 6  get_Id */
    async_http_op_get_Status,
    /* 7  get_Status */
    async_http_op_put_Completed,
    /* 8  put_Completed (WPM awaiter) / get_ErrorCode */
    (HRESULT (WINAPI *)( IAsyncInfo * ))async_http_op_get_Completed,
    /* 9  get_Completed */
    async_http_op_GetResults
  },
  {
    /* 10 GetResults (WPM awaiter) / Close */
    async_http_op_GetResults,
    /* 11 .NET NativeAOT IAsyncOperation`1::GetResults */
    async_http_op_put_Completed,
    /* 12 put_Completed / put_Progress */
    async_http_op_get_Completed,
    /* 13 get_Completed / get_Progress */
    async_http_op_GetResults,
    /* 14 WithProgress GetResults */
    async_http_op_put_Completed,
    /* 15 WithProgress put_Completed */
    async_http_op_GetResults,
    /* 16 IAsyncOperationWithProgress2 get_Results */
    /* C++/WinRT coroutines in WPM virtual-call projection helpers well past
     * the declared interface slots (e.g. the CLog::Trace formatter calls slot
     * 39 on the awaited op's RCW). Polyfill the projection region so those
     * calls land on a no-op instead of string data. */
    async_http_op_QueryInterface,
    /* 17 */ async_http_op_AddRef,
    async_http_op_Release,
    /* 19 */ async_http_op_QueryInterface,
    async_http_op_GetResults2,
    /* 21 Win11 projection GetResults (UINT32) */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 23 */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 25 */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 27 */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 29 */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 31 */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 33 */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 35 */ async_http_op_QueryInterface,
    async_http_op_QueryInterface,
    /* 37 */ async_http_op_QueryInterface,
    async_http_op_GetResults,
    /* 39 CLog::Trace projection call */
  }
};

static HRESULT WINAPI async_http_info_QueryInterface( IAsyncInfo *This, REFIID iid, void **out )
{
    struct async_http_op *impl = impl_from_async_info( This );
    return async_http_op_QueryInterface( (IAsyncInfo *)&impl->vtbl_op, iid, out );
}
static ULONG WINAPI async_http_info_AddRef( IAsyncInfo *This )
{
    struct async_http_op *impl = impl_from_async_info( This );
    return async_http_op_AddRef( (IAsyncInfo *)&impl->vtbl_op );
}
static ULONG WINAPI async_http_info_Release( IAsyncInfo *This )
{
    struct async_http_op *impl = impl_from_async_info( This );
    return async_http_op_Release( (IAsyncInfo *)&impl->vtbl_op );
}
static HRESULT WINAPI async_http_info_GetIids( IAsyncInfo *This, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI async_http_info_GetRuntimeClassName( IAsyncInfo *This, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncInfo";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI async_http_info_GetTrustLevel( IAsyncInfo *This, TrustLevel *level ) { if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI async_http_info_get_Id( IAsyncInfo *This, UINT32 *id )
{
    WDBG( "op_get_Id\n" );
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}
static HRESULT WINAPI async_http_info_get_Status( IAsyncInfo *This, AsyncStatus *status )
{
    WDBG( "op_get_Status\n" );
    if (!status) return E_POINTER;
    *status = 1 /* Completed */;
    return S_OK;
}
static HRESULT WINAPI async_http_info_get_ErrorCode( IAsyncInfo *This, HRESULT *code )
{
    WDBG( "op_get_ErrorCode\n" );
    if (!code) return E_POINTER;
    *code = S_OK;
    return S_OK;
}
static HRESULT WINAPI async_http_info_Cancel( IAsyncInfo *This ) { WDBG( "op_Cancel\n" ); return S_OK; }
static HRESULT WINAPI async_http_info_Close( IAsyncInfo *This ) { WDBG( "op_Close\n" ); return S_OK; }

static const IAsyncInfoVtbl async_http_info_vtbl = {
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
    async_http_info_Close
};

HRESULT create_async_http_op( void *result_obj, HSTRING result_str, BOOL is_str, void **out )
{
    return create_async_http_op_ex( result_obj, result_str, is_str, FALSE, out );
}
HRESULT create_async_http_op_obj( void *result_obj, void **out )
{
    return create_async_http_op_ex( result_obj, NULL, FALSE, FALSE, out );
}
HRESULT create_async_http_op_u64( UINT64 val, void **out )
{
    struct async_http_op *impl;
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_op = &async_http_op_vtbl;
    impl->vtbl_info = &async_http_info_vtbl;
    impl->ref = 1;
    impl->is_u64 = TRUE;
    impl->result_u64 = val;
    *out = &impl->vtbl_op;
    return S_OK;
}
HRESULT create_async_http_op_ex( void *result_obj, HSTRING result_str, BOOL is_str,
                                 BOOL handler_at_0x10, void **out )
{
    struct async_http_op *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->vtbl_op = &async_http_op_vtbl;
    impl->vtbl_info = &async_http_info_vtbl;
    impl->ref = 1;
    impl->is_str = is_str;
    impl->handler_at_0x10 = handler_at_0x10;
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
