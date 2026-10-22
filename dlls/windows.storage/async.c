/* WinRT Windows.Storage Async Implementation
 *
 * Copyright (C) 2026 Wine Ultimate
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

enum async_kind
{
    ASYNC_INSPECTABLE,
    ASYNC_ACTION,
    ASYNC_HSTRING,
    ASYNC_UINT32,
    ASYNC_BOOLEAN,
    ASYNC_PROGRESS_BUFFER,
    ASYNC_PROGRESS_UINT32,
};

struct async_op
{
    IInspectable IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IID iid;
    const WCHAR *class_name;
    enum async_kind kind;
    CRITICAL_SECTION cs;
    AsyncStatus status;
    HRESULT hr;
    IUnknown *completed_handler;

    union
    {
        IInspectable *inspectable;
        HSTRING hstr;
        UINT32 u32;
        boolean b;
    } result;
};

static inline struct async_op *impl_from_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct async_op, IInspectable_iface );
}

static inline struct async_op *impl_from_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct async_op, IAsyncInfo_iface );
}

static HRESULT WINAPI async_op_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct async_op *impl = impl_from_IInspectable( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &impl->iid ))
    {
        *out = &impl->IInspectable_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    TRACE( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_op_AddRef( IInspectable *iface )
{
    struct async_op *impl = impl_from_IInspectable( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI async_op_Release( IInspectable *iface )
{
    struct async_op *impl = impl_from_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->completed_handler)
            IUnknown_Release( impl->completed_handler );
        if ((impl->kind == ASYNC_INSPECTABLE || impl->kind == ASYNC_PROGRESS_BUFFER) && impl->result.inspectable)
            IInspectable_Release( impl->result.inspectable );
        if (impl->kind == ASYNC_HSTRING && impl->result.hstr)
            WindowsDeleteString( impl->result.hstr );

        DeleteCriticalSection( &impl->cs );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI async_op_GetIids( IInspectable *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_INVALIDARG;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI async_op_GetRuntimeClassName( IInspectable *iface, HSTRING *class_name )
{
    struct async_op *impl = impl_from_IInspectable( iface );
    return WindowsCreateString( impl->class_name, wcslen( impl->class_name ), class_name );
}

static HRESULT WINAPI async_op_GetTrustLevel( IInspectable *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

typedef HRESULT (WINAPI *pfn_async_completed_invoke)( IUnknown *this, void *sender, AsyncStatus status );

static HRESULT WINAPI async_op_put_Completed( IInspectable *iface, void *handler )
{
    struct async_op *impl = impl_from_IInspectable( iface );
    IUnknown *h = (IUnknown *)handler;

    TRACE( "iface %p, handler %p.\n", iface, handler );

    EnterCriticalSection( &impl->cs );
    if (impl->completed_handler)
    {
        IUnknown_Release( impl->completed_handler );
        impl->completed_handler = NULL;
    }
    if (h)
    {
        impl->completed_handler = h;
        IUnknown_AddRef( h );
    }
    if (h && impl->status == Completed)
    {
        pfn_async_completed_invoke invoke;
        void **vtbl = *(void ***)h;
        invoke = (pfn_async_completed_invoke)vtbl[3];
        LeaveCriticalSection( &impl->cs );
        invoke( h, &impl->IInspectable_iface, impl->status );
        return S_OK;
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI async_op_get_Completed( IInspectable *iface, void **handler )
{
    struct async_op *impl = impl_from_IInspectable( iface );

    if (!handler) return E_POINTER;

    EnterCriticalSection( &impl->cs );
    *handler = impl->completed_handler;
    if (*handler)
        IUnknown_AddRef( (IUnknown *)*handler );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI async_op_GetResults_Inspectable( IInspectable *iface, IInspectable **results )
{
    struct async_op *impl = impl_from_IInspectable( iface );

    if (!results) return E_POINTER;
    *results = impl->result.inspectable;
    if (*results)
        IInspectable_AddRef( *results );
    return impl->hr;
}

static HRESULT WINAPI async_op_GetResults_Action( IInspectable *iface )
{
    struct async_op *impl = impl_from_IInspectable( iface );
    return impl->hr;
}

static HRESULT WINAPI async_op_GetResults_HString( IInspectable *iface, HSTRING *results )
{
    struct async_op *impl = impl_from_IInspectable( iface );

    if (!results) return E_POINTER;
    return WindowsDuplicateString( impl->result.hstr, results );
}

static HRESULT WINAPI async_op_GetResults_UInt32( IInspectable *iface, UINT32 *results )
{
    struct async_op *impl = impl_from_IInspectable( iface );

    if (!results) return E_POINTER;
    *results = impl->result.u32;
    return impl->hr;
}

static HRESULT WINAPI async_op_GetResults_Boolean( IInspectable *iface, boolean *results )
{
    struct async_op *impl = impl_from_IInspectable( iface );

    if (!results) return E_POINTER;
    *results = impl->result.b;
    return impl->hr;
}

static HRESULT WINAPI async_op_put_Progress( IInspectable *iface, void *handler )
{
    return S_OK;
}

static HRESULT WINAPI async_op_get_Progress( IInspectable *iface, void **handler )
{
    if (handler) *handler = NULL;
    return S_OK;
}

/* IAsyncInfo methods */
static HRESULT WINAPI async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );
    return async_op_QueryInterface( &impl->IInspectable_iface, iid, out );
}

static ULONG WINAPI async_info_AddRef( IAsyncInfo *iface )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );
    return async_op_AddRef( &impl->IInspectable_iface );
}

static ULONG WINAPI async_info_Release( IAsyncInfo *iface )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );
    return async_op_Release( &impl->IInspectable_iface );
}

static HRESULT WINAPI async_info_GetIids( IAsyncInfo *iface, ULONG *iid_count, IID **iids )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );
    return async_op_GetIids( &impl->IInspectable_iface, iid_count, iids );
}

static HRESULT WINAPI async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *class_name )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );
    return async_op_GetRuntimeClassName( &impl->IInspectable_iface, class_name );
}

static HRESULT WINAPI async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *trust_level )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );
    return async_op_GetTrustLevel( &impl->IInspectable_iface, trust_level );
}

static HRESULT WINAPI async_info_get_Id( IAsyncInfo *iface, unsigned int *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );

    if (!status) return E_POINTER;
    *status = impl->status;
    return S_OK;
}

static HRESULT WINAPI async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error_code )
{
    struct async_op *impl = impl_from_IAsyncInfo( iface );

    if (!error_code) return E_POINTER;
    *error_code = impl->hr;
    return S_OK;
}

static HRESULT WINAPI async_info_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI async_info_Close( IAsyncInfo *iface )
{
    return S_OK;
}

static const struct IAsyncInfoVtbl async_info_vtbl =
{
    async_info_QueryInterface,
    async_info_AddRef,
    async_info_Release,
    async_info_GetIids,
    async_info_GetRuntimeClassName,
    async_info_GetTrustLevel,
    async_info_get_Id,
    async_info_get_Status,
    async_info_get_ErrorCode,
    async_info_Cancel,
    async_info_Close,
};

static const void *async_op_vtbl_inspectable[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults_Inspectable,
};

static const void *async_op_vtbl_action[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults_Action,
};

static const void *async_op_vtbl_hstring[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults_HString,
};

static const void *async_op_vtbl_uint32[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults_UInt32,
};

static const void *async_op_vtbl_boolean[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults_Boolean,
};

static const void *async_op_vtbl_progress_buffer[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Progress,
    async_op_get_Progress,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults_Inspectable,
};

static const void *async_op_vtbl_progress_uint32[] =
{
    async_op_QueryInterface,
    async_op_AddRef,
    async_op_Release,
    async_op_GetIids,
    async_op_GetRuntimeClassName,
    async_op_GetTrustLevel,
    async_op_put_Progress,
    async_op_get_Progress,
    async_op_put_Completed,
    async_op_get_Completed,
    async_op_GetResults_UInt32,
};

static struct async_op *async_op_alloc( const GUID *iid, const WCHAR *class_name, enum async_kind kind, const void *vtbl )
{
    struct async_op *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->IInspectable_iface.lpVtbl = (const IInspectableVtbl *)vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &async_info_vtbl;
    impl->ref = 1;
    impl->iid = *iid;
    impl->class_name = class_name;
    impl->kind = kind;
    impl->status = Completed;
    impl->hr = S_OK;
    InitializeCriticalSection( &impl->cs );
    return impl;
}

HRESULT async_operation_inspectable_create( const GUID *iid, const WCHAR *class_name, IInspectable *result, void **out )
{
    struct async_op *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = async_op_alloc( iid, class_name, ASYNC_INSPECTABLE, async_op_vtbl_inspectable )))
        return E_OUTOFMEMORY;

    impl->result.inspectable = result;
    if (result) IInspectable_AddRef( result );

    *out = &impl->IInspectable_iface;
    return S_OK;
}

HRESULT async_action_create( const WCHAR *class_name, HRESULT hr_result, IAsyncAction **out )
{
    struct async_op *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = async_op_alloc( &IID_IAsyncAction, class_name, ASYNC_ACTION, async_op_vtbl_action )))
        return E_OUTOFMEMORY;

    impl->hr = hr_result;
    *out = (IAsyncAction *)&impl->IInspectable_iface;
    return S_OK;
}

HRESULT async_operation_hstring_create( const WCHAR *class_name, HSTRING result, IAsyncOperation_HSTRING **out )
{
    struct async_op *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = async_op_alloc( &IID_IAsyncOperation_HSTRING, class_name, ASYNC_HSTRING, async_op_vtbl_hstring )))
        return E_OUTOFMEMORY;

    impl->result.hstr = result;
    *out = (IAsyncOperation_HSTRING *)&impl->IInspectable_iface;
    return S_OK;
}

HRESULT async_operation_uint32_create( const WCHAR *class_name, UINT32 result, IAsyncOperation_UINT32 **out )
{
    struct async_op *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = async_op_alloc( &IID_IAsyncOperation_UINT32, class_name, ASYNC_UINT32, async_op_vtbl_uint32 )))
        return E_OUTOFMEMORY;

    impl->result.u32 = result;
    *out = (IAsyncOperation_UINT32 *)&impl->IInspectable_iface;
    return S_OK;
}

HRESULT async_operation_boolean_create( const WCHAR *class_name, boolean result, IAsyncOperation_boolean **out )
{
    struct async_op *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = async_op_alloc( &IID_IAsyncOperation_boolean, class_name, ASYNC_BOOLEAN, async_op_vtbl_boolean )))
        return E_OUTOFMEMORY;

    impl->result.b = result;
    *out = (IAsyncOperation_boolean *)&impl->IInspectable_iface;
    return S_OK;
}

HRESULT async_operation_progress_buffer_create( IBuffer *result, IAsyncOperationWithProgress_IBuffer_UINT32 **out )
{
    struct async_op *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = async_op_alloc( &IID_IAsyncOperationWithProgress_IBuffer_UINT32,
                                 L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Storage.Streams.IBuffer, UInt32>",
                                 ASYNC_PROGRESS_BUFFER, async_op_vtbl_progress_buffer )))
        return E_OUTOFMEMORY;

    impl->result.inspectable = (IInspectable *)result;
    if (result) IBuffer_AddRef( result );

    *out = (IAsyncOperationWithProgress_IBuffer_UINT32 *)&impl->IInspectable_iface;
    return S_OK;
}

HRESULT async_operation_progress_uint32_create( UINT32 result, IAsyncOperationWithProgress_UINT32_UINT32 **out )
{
    struct async_op *impl;

    if (!out) return E_POINTER;
    *out = NULL;

    if (!(impl = async_op_alloc( &IID_IAsyncOperationWithProgress_UINT32_UINT32,
                                 L"Windows.Foundation.IAsyncOperationWithProgress`2<UInt32, UInt32>",
                                 ASYNC_PROGRESS_UINT32, async_op_vtbl_progress_uint32 )))
        return E_OUTOFMEMORY;

    impl->result.u32 = result;
    *out = (IAsyncOperationWithProgress_UINT32_UINT32 *)&impl->IInspectable_iface;
    return S_OK;
}
