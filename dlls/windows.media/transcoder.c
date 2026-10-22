/* WinRT Windows.Media.Transcoding.MediaTranscoder Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(media);

#ifndef AsyncStatus_Completed
#define AsyncStatus_Completed Completed
#endif

/* --- transcode_action (IAsyncActionWithProgress<DOUBLE>) --- */

struct transcode_action
{
    IAsyncActionWithProgress_DOUBLE IAsyncActionWithProgress_DOUBLE_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;

    IAsyncActionProgressHandler_DOUBLE *progress_handler;
    IAsyncActionWithProgressCompletedHandler_DOUBLE *completed_handler;
};

static inline struct transcode_action *impl_from_IAsyncActionWithProgress_DOUBLE( IAsyncActionWithProgress_DOUBLE *iface )
{
    return CONTAINING_RECORD( iface, struct transcode_action, IAsyncActionWithProgress_DOUBLE_iface );
}

static inline struct transcode_action *impl_from_action_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct transcode_action, IAsyncInfo_iface );
}

static HRESULT WINAPI transcode_action_QueryInterface( IAsyncActionWithProgress_DOUBLE *iface, REFIID iid, void **out )
{
    struct transcode_action *impl = impl_from_IAsyncActionWithProgress_DOUBLE( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncActionWithProgress_DOUBLE ))
    {
        *out = &impl->IAsyncActionWithProgress_DOUBLE_iface;
        IAsyncActionWithProgress_DOUBLE_AddRef( iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        IAsyncInfo_AddRef( &impl->IAsyncInfo_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI transcode_action_AddRef( IAsyncActionWithProgress_DOUBLE *iface )
{
    struct transcode_action *impl = impl_from_IAsyncActionWithProgress_DOUBLE( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI transcode_action_Release( IAsyncActionWithProgress_DOUBLE *iface )
{
    struct transcode_action *impl = impl_from_IAsyncActionWithProgress_DOUBLE( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->progress_handler) IAsyncActionProgressHandler_DOUBLE_Release( impl->progress_handler );
        if (impl->completed_handler) IAsyncActionWithProgressCompletedHandler_DOUBLE_Release( impl->completed_handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI transcode_action_GetIids( IAsyncActionWithProgress_DOUBLE *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI transcode_action_GetRuntimeClassName( IAsyncActionWithProgress_DOUBLE *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncActionWithProgress`1<Double>", 51, class_name );
}

static HRESULT WINAPI transcode_action_GetTrustLevel( IAsyncActionWithProgress_DOUBLE *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI transcode_action_put_Progress( IAsyncActionWithProgress_DOUBLE *iface, IAsyncActionProgressHandler_DOUBLE *handler )
{
    struct transcode_action *impl = impl_from_IAsyncActionWithProgress_DOUBLE( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->progress_handler) IAsyncActionProgressHandler_DOUBLE_Release( impl->progress_handler );
    impl->progress_handler = handler;
    if (handler)
    {
        IAsyncActionProgressHandler_DOUBLE_AddRef( handler );
        IAsyncActionProgressHandler_DOUBLE_Invoke( handler, iface, 100.0 );
    }
    return S_OK;
}

static HRESULT WINAPI transcode_action_get_Progress( IAsyncActionWithProgress_DOUBLE *iface, IAsyncActionProgressHandler_DOUBLE **handler )
{
    struct transcode_action *impl = impl_from_IAsyncActionWithProgress_DOUBLE( iface );

    if (!handler) return E_POINTER;
    if ((*handler = impl->progress_handler)) IAsyncActionProgressHandler_DOUBLE_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI transcode_action_put_Completed( IAsyncActionWithProgress_DOUBLE *iface, IAsyncActionWithProgressCompletedHandler_DOUBLE *handler )
{
    struct transcode_action *impl = impl_from_IAsyncActionWithProgress_DOUBLE( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->completed_handler) IAsyncActionWithProgressCompletedHandler_DOUBLE_Release( impl->completed_handler );
    impl->completed_handler = handler;
    if (handler)
    {
        IAsyncActionWithProgressCompletedHandler_DOUBLE_AddRef( handler );
        IAsyncActionWithProgressCompletedHandler_DOUBLE_Invoke( handler, iface, AsyncStatus_Completed );
    }
    return S_OK;
}

static HRESULT WINAPI transcode_action_get_Completed( IAsyncActionWithProgress_DOUBLE *iface, IAsyncActionWithProgressCompletedHandler_DOUBLE **handler )
{
    struct transcode_action *impl = impl_from_IAsyncActionWithProgress_DOUBLE( iface );

    if (!handler) return E_POINTER;
    if ((*handler = impl->completed_handler)) IAsyncActionWithProgressCompletedHandler_DOUBLE_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI transcode_action_GetResults( IAsyncActionWithProgress_DOUBLE *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static const struct IAsyncActionWithProgress_DOUBLEVtbl transcode_action_vtbl =
{
    /* IUnknown */
    transcode_action_QueryInterface,
    transcode_action_AddRef,
    transcode_action_Release,
    /* IInspectable */
    transcode_action_GetIids,
    transcode_action_GetRuntimeClassName,
    transcode_action_GetTrustLevel,
    /* IAsyncActionWithProgress<DOUBLE> */
    transcode_action_put_Progress,
    transcode_action_get_Progress,
    transcode_action_put_Completed,
    transcode_action_get_Completed,
    transcode_action_GetResults,
};

static HRESULT WINAPI action_async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    struct transcode_action *impl = impl_from_action_IAsyncInfo( iface );
    return IAsyncActionWithProgress_DOUBLE_QueryInterface( &impl->IAsyncActionWithProgress_DOUBLE_iface, iid, out );
}

static ULONG WINAPI action_async_info_AddRef( IAsyncInfo *iface )
{
    struct transcode_action *impl = impl_from_action_IAsyncInfo( iface );
    return IAsyncActionWithProgress_DOUBLE_AddRef( &impl->IAsyncActionWithProgress_DOUBLE_iface );
}

static ULONG WINAPI action_async_info_Release( IAsyncInfo *iface )
{
    struct transcode_action *impl = impl_from_action_IAsyncInfo( iface );
    return IAsyncActionWithProgress_DOUBLE_Release( &impl->IAsyncActionWithProgress_DOUBLE_iface );
}

static HRESULT WINAPI action_async_info_GetIids( IAsyncInfo *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI action_async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, class_name );
}

static HRESULT WINAPI action_async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI action_async_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (id) *id = 1;
    return S_OK;
}

static HRESULT WINAPI action_async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (status) *status = AsyncStatus_Completed;
    return S_OK;
}

static HRESULT WINAPI action_async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error_code )
{
    if (error_code) *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI action_async_info_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI action_async_info_Close( IAsyncInfo *iface )
{
    return S_OK;
}

static const struct IAsyncInfoVtbl action_async_info_vtbl =
{
    action_async_info_QueryInterface,
    action_async_info_AddRef,
    action_async_info_Release,
    action_async_info_GetIids,
    action_async_info_GetRuntimeClassName,
    action_async_info_GetTrustLevel,
    action_async_info_get_Id,
    action_async_info_get_Status,
    action_async_info_get_ErrorCode,
    action_async_info_Cancel,
    action_async_info_Close,
};

static HRESULT transcode_action_create( IAsyncActionWithProgress_DOUBLE **out )
{
    struct transcode_action *impl;

    if (!(impl = calloc( 1, sizeof(*impl) )))
        return E_OUTOFMEMORY;

    impl->IAsyncActionWithProgress_DOUBLE_iface.lpVtbl = &transcode_action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &action_async_info_vtbl;
    impl->ref = 1;

    *out = &impl->IAsyncActionWithProgress_DOUBLE_iface;
    return S_OK;
}

/* --- prepare_transcode_result (IPrepareTranscodeResult) --- */

struct prepare_transcode_result
{
    IPrepareTranscodeResult IPrepareTranscodeResult_iface;
    LONG ref;
    WCHAR *src_path;
    WCHAR *dst_path;
};

static inline struct prepare_transcode_result *impl_from_IPrepareTranscodeResult( IPrepareTranscodeResult *iface )
{
    return CONTAINING_RECORD( iface, struct prepare_transcode_result, IPrepareTranscodeResult_iface );
}

static HRESULT WINAPI prepare_result_QueryInterface( IPrepareTranscodeResult *iface, REFIID iid, void **out )
{
    struct prepare_transcode_result *impl = impl_from_IPrepareTranscodeResult( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPrepareTranscodeResult ))
    {
        *out = &impl->IPrepareTranscodeResult_iface;
        IPrepareTranscodeResult_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI prepare_result_AddRef( IPrepareTranscodeResult *iface )
{
    struct prepare_transcode_result *impl = impl_from_IPrepareTranscodeResult( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI prepare_result_Release( IPrepareTranscodeResult *iface )
{
    struct prepare_transcode_result *impl = impl_from_IPrepareTranscodeResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        free( impl->src_path );
        free( impl->dst_path );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI prepare_result_GetIids( IPrepareTranscodeResult *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI prepare_result_GetRuntimeClassName( IPrepareTranscodeResult *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Transcoding.PrepareTranscodeResult", 47, class_name );
}

static HRESULT WINAPI prepare_result_GetTrustLevel( IPrepareTranscodeResult *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI prepare_result_get_CanTranscode( IPrepareTranscodeResult *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI prepare_result_get_FailureReason( IPrepareTranscodeResult *iface, TranscodeFailureReason *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = TranscodeFailureReason_None;
    return S_OK;
}

static HRESULT WINAPI prepare_result_TranscodeAsync( IPrepareTranscodeResult *iface, IAsyncActionWithProgress_DOUBLE **operation )
{
    struct prepare_transcode_result *impl = impl_from_IPrepareTranscodeResult( iface );

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (impl->src_path && impl->dst_path)
    {
        TRACE( "copying transcode source %s -> %s\n", debugstr_w(impl->src_path), debugstr_w(impl->dst_path) );
        CopyFileW( impl->src_path, impl->dst_path, FALSE );
    }

    return transcode_action_create( operation );
}

static const struct IPrepareTranscodeResultVtbl prepare_result_vtbl =
{
    prepare_result_QueryInterface,
    prepare_result_AddRef,
    prepare_result_Release,
    prepare_result_GetIids,
    prepare_result_GetRuntimeClassName,
    prepare_result_GetTrustLevel,
    prepare_result_get_CanTranscode,
    prepare_result_get_FailureReason,
    prepare_result_TranscodeAsync,
};

/* --- prepare_transcode_async_op (IAsyncOperation<PrepareTranscodeResult*>) --- */

struct prepare_transcode_async_op
{
    IAsyncOperation_PrepareTranscodeResult IAsyncOperation_PrepareTranscodeResult_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;

    IPrepareTranscodeResult *result;
    IAsyncOperationCompletedHandler_PrepareTranscodeResult *handler;
};

static inline struct prepare_transcode_async_op *impl_from_IAsyncOperation_PrepareTranscodeResult( IAsyncOperation_PrepareTranscodeResult *iface )
{
    return CONTAINING_RECORD( iface, struct prepare_transcode_async_op, IAsyncOperation_PrepareTranscodeResult_iface );
}

static inline struct prepare_transcode_async_op *impl_from_op_IAsyncInfo( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct prepare_transcode_async_op, IAsyncInfo_iface );
}

static HRESULT WINAPI prepare_op_QueryInterface( IAsyncOperation_PrepareTranscodeResult *iface, REFIID iid, void **out )
{
    struct prepare_transcode_async_op *impl = impl_from_IAsyncOperation_PrepareTranscodeResult( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperation_PrepareTranscodeResult ))
    {
        *out = &impl->IAsyncOperation_PrepareTranscodeResult_iface;
        IAsyncOperation_PrepareTranscodeResult_AddRef( iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = &impl->IAsyncInfo_iface;
        IAsyncInfo_AddRef( &impl->IAsyncInfo_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI prepare_op_AddRef( IAsyncOperation_PrepareTranscodeResult *iface )
{
    struct prepare_transcode_async_op *impl = impl_from_IAsyncOperation_PrepareTranscodeResult( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI prepare_op_Release( IAsyncOperation_PrepareTranscodeResult *iface )
{
    struct prepare_transcode_async_op *impl = impl_from_IAsyncOperation_PrepareTranscodeResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->result) IPrepareTranscodeResult_Release( impl->result );
        if (impl->handler) IAsyncOperationCompletedHandler_PrepareTranscodeResult_Release( impl->handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI prepare_op_GetIids( IAsyncOperation_PrepareTranscodeResult *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI prepare_op_GetRuntimeClassName( IAsyncOperation_PrepareTranscodeResult *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Windows.Media.Transcoding.PrepareTranscodeResult>", 80, class_name );
}

static HRESULT WINAPI prepare_op_GetTrustLevel( IAsyncOperation_PrepareTranscodeResult *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI prepare_op_put_Completed( IAsyncOperation_PrepareTranscodeResult *iface,
                                                IAsyncOperationCompletedHandler_PrepareTranscodeResult *handler )
{
    struct prepare_transcode_async_op *impl = impl_from_IAsyncOperation_PrepareTranscodeResult( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->handler) IAsyncOperationCompletedHandler_PrepareTranscodeResult_Release( impl->handler );
    impl->handler = handler;
    if (handler)
    {
        IAsyncOperationCompletedHandler_PrepareTranscodeResult_AddRef( handler );
        IAsyncOperationCompletedHandler_PrepareTranscodeResult_Invoke( handler, iface, AsyncStatus_Completed );
    }
    return S_OK;
}

static HRESULT WINAPI prepare_op_get_Completed( IAsyncOperation_PrepareTranscodeResult *iface,
                                                IAsyncOperationCompletedHandler_PrepareTranscodeResult **handler )
{
    struct prepare_transcode_async_op *impl = impl_from_IAsyncOperation_PrepareTranscodeResult( iface );

    if (!handler) return E_POINTER;
    if ((*handler = impl->handler)) IAsyncOperationCompletedHandler_PrepareTranscodeResult_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI prepare_op_GetResults( IAsyncOperation_PrepareTranscodeResult *iface, IPrepareTranscodeResult **results )
{
    struct prepare_transcode_async_op *impl = impl_from_IAsyncOperation_PrepareTranscodeResult( iface );

    TRACE( "iface %p, results %p.\n", iface, results );

    if (!results) return E_POINTER;
    *results = impl->result;
    if (*results) IPrepareTranscodeResult_AddRef( *results );
    return S_OK;
}

static const struct IAsyncOperation_PrepareTranscodeResultVtbl prepare_op_vtbl =
{
    prepare_op_QueryInterface,
    prepare_op_AddRef,
    prepare_op_Release,
    prepare_op_GetIids,
    prepare_op_GetRuntimeClassName,
    prepare_op_GetTrustLevel,
    prepare_op_put_Completed,
    prepare_op_get_Completed,
    prepare_op_GetResults,
};

static HRESULT WINAPI op_async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    struct prepare_transcode_async_op *impl = impl_from_op_IAsyncInfo( iface );
    return IAsyncOperation_PrepareTranscodeResult_QueryInterface( &impl->IAsyncOperation_PrepareTranscodeResult_iface, iid, out );
}

static ULONG WINAPI op_async_info_AddRef( IAsyncInfo *iface )
{
    struct prepare_transcode_async_op *impl = impl_from_op_IAsyncInfo( iface );
    return IAsyncOperation_PrepareTranscodeResult_AddRef( &impl->IAsyncOperation_PrepareTranscodeResult_iface );
}

static ULONG WINAPI op_async_info_Release( IAsyncInfo *iface )
{
    struct prepare_transcode_async_op *impl = impl_from_op_IAsyncInfo( iface );
    return IAsyncOperation_PrepareTranscodeResult_Release( &impl->IAsyncOperation_PrepareTranscodeResult_iface );
}

static HRESULT WINAPI op_async_info_GetIids( IAsyncInfo *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI op_async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, class_name );
}

static HRESULT WINAPI op_async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI op_async_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (id) *id = 1;
    return S_OK;
}

static HRESULT WINAPI op_async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (status) *status = AsyncStatus_Completed;
    return S_OK;
}

static HRESULT WINAPI op_async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error_code )
{
    if (error_code) *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI op_async_info_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI op_async_info_Close( IAsyncInfo *iface )
{
    return S_OK;
}

static const struct IAsyncInfoVtbl op_async_info_vtbl =
{
    op_async_info_QueryInterface,
    op_async_info_AddRef,
    op_async_info_Release,
    op_async_info_GetIids,
    op_async_info_GetRuntimeClassName,
    op_async_info_GetTrustLevel,
    op_async_info_get_Id,
    op_async_info_get_Status,
    op_async_info_get_ErrorCode,
    op_async_info_Cancel,
    op_async_info_Close,
};

static WCHAR *get_storage_file_path( IStorageFile *file )
{
    IStorageItem *item;
    HSTRING str = NULL;
    WCHAR *path = NULL;

    if (!file) return NULL;
    if (SUCCEEDED( IStorageFile_QueryInterface( file, &IID_IStorageItem, (void **)&item ) ))
    {
        if (SUCCEEDED( IStorageItem_get_Path( item, &str ) ) && str)
        {
            const WCHAR *raw = WindowsGetStringRawBuffer( str, NULL );
            if (raw) path = wcsdup( raw );
            WindowsDeleteString( str );
        }
        IStorageItem_Release( item );
    }
    return path;
}

static HRESULT prepare_transcode_async_create( IStorageFile *source, IStorageFile *destination,
                                              IAsyncOperation_PrepareTranscodeResult **out )
{
    struct prepare_transcode_async_op *op;
    struct prepare_transcode_result *res;

    if (!(op = calloc( 1, sizeof(*op) )))
        return E_OUTOFMEMORY;
    if (!(res = calloc( 1, sizeof(*res) )))
    {
        free( op );
        return E_OUTOFMEMORY;
    }

    res->IPrepareTranscodeResult_iface.lpVtbl = &prepare_result_vtbl;
    res->ref = 1;
    res->src_path = get_storage_file_path( source );
    res->dst_path = get_storage_file_path( destination );

    op->IAsyncOperation_PrepareTranscodeResult_iface.lpVtbl = &prepare_op_vtbl;
    op->IAsyncInfo_iface.lpVtbl = &op_async_info_vtbl;
    op->ref = 1;
    op->result = &res->IPrepareTranscodeResult_iface;

    *out = &op->IAsyncOperation_PrepareTranscodeResult_iface;
    return S_OK;
}

/* --- media_transcoder (IMediaTranscoder & IMediaTranscoder2) --- */

struct media_transcoder
{
    IMediaTranscoder IMediaTranscoder_iface;
    IMediaTranscoder2 IMediaTranscoder2_iface;
    LONG ref;

    TimeSpan trim_start;
    TimeSpan trim_stop;
    boolean always_reencode;
    boolean hw_accel;
    MediaVideoProcessingAlgorithm video_algo;
};

static inline struct media_transcoder *impl_from_IMediaTranscoder( IMediaTranscoder *iface )
{
    return CONTAINING_RECORD( iface, struct media_transcoder, IMediaTranscoder_iface );
}

static inline struct media_transcoder *impl_from_IMediaTranscoder2( IMediaTranscoder2 *iface )
{
    return CONTAINING_RECORD( iface, struct media_transcoder, IMediaTranscoder2_iface );
}

static HRESULT WINAPI media_transcoder_QueryInterface( IMediaTranscoder *iface, REFIID iid, void **out )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IMediaTranscoder ))
    {
        *out = &impl->IMediaTranscoder_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMediaTranscoder2 ))
    {
        *out = &impl->IMediaTranscoder2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI media_transcoder_AddRef( IMediaTranscoder *iface )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI media_transcoder_Release( IMediaTranscoder *iface )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI media_transcoder_GetIids( IMediaTranscoder *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_transcoder_GetRuntimeClassName( IMediaTranscoder *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Transcoding.MediaTranscoder", 39, class_name );
}

static HRESULT WINAPI media_transcoder_GetTrustLevel( IMediaTranscoder *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_put_TrimStartTime( IMediaTranscoder *iface, TimeSpan value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %I64d.\n", iface, value.Duration );
    impl->trim_start = value;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_get_TrimStartTime( IMediaTranscoder *iface, TimeSpan *value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = impl->trim_start;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_put_TrimStopTime( IMediaTranscoder *iface, TimeSpan value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %I64d.\n", iface, value.Duration );
    impl->trim_stop = value;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_get_TrimStopTime( IMediaTranscoder *iface, TimeSpan *value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = impl->trim_stop;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_put_AlwaysReencode( IMediaTranscoder *iface, boolean value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %u.\n", iface, value );
    impl->always_reencode = value;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_get_AlwaysReencode( IMediaTranscoder *iface, boolean *value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = impl->always_reencode;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_put_HardwareAccelerationEnabled( IMediaTranscoder *iface, boolean value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %u.\n", iface, value );
    impl->hw_accel = value;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_get_HardwareAccelerationEnabled( IMediaTranscoder *iface, boolean *value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = impl->hw_accel;
    return S_OK;
}

static HRESULT WINAPI media_transcoder_AddAudioEffect( IMediaTranscoder *iface, HSTRING activatable_class_id )
{
    TRACE( "iface %p, activatable_class_id %s.\n", iface, debugstr_hstring( activatable_class_id ) );
    return S_OK;
}

static HRESULT WINAPI media_transcoder_AddAudioEffectWithSettings( IMediaTranscoder *iface, HSTRING activatable_class_id,
                                                                   boolean effect_required, IPropertySet *configuration )
{
    TRACE( "iface %p, activatable_class_id %s, effect_required %u, configuration %p.\n",
           iface, debugstr_hstring( activatable_class_id ), effect_required, configuration );
    return S_OK;
}

static HRESULT WINAPI media_transcoder_AddVideoEffect( IMediaTranscoder *iface, HSTRING activatable_class_id )
{
    TRACE( "iface %p, activatable_class_id %s.\n", iface, debugstr_hstring( activatable_class_id ) );
    return S_OK;
}

static HRESULT WINAPI media_transcoder_AddVideoEffectWithSettings( IMediaTranscoder *iface, HSTRING activatable_class_id,
                                                                   boolean effect_required, IPropertySet *configuration )
{
    TRACE( "iface %p, activatable_class_id %s, effect_required %u, configuration %p.\n",
           iface, debugstr_hstring( activatable_class_id ), effect_required, configuration );
    return S_OK;
}

static HRESULT WINAPI media_transcoder_ClearEffects( IMediaTranscoder *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI media_transcoder_PrepareFileTranscodeAsync( IMediaTranscoder *iface, IStorageFile *source, IStorageFile *destination,
                                                                  IMediaEncodingProfile *profile, IAsyncOperation_PrepareTranscodeResult **operation )
{
    TRACE( "iface %p, source %p, destination %p, profile %p, operation %p.\n", iface, source, destination, profile, operation );
    return prepare_transcode_async_create( source, destination, operation );
}

static HRESULT WINAPI media_transcoder_PrepareStreamTranscodeAsync( IMediaTranscoder *iface, IRandomAccessStream *source, IRandomAccessStream *destination,
                                                                    IMediaEncodingProfile *profile, IAsyncOperation_PrepareTranscodeResult **operation )
{
    TRACE( "iface %p, source %p, destination %p, profile %p, operation %p.\n", iface, source, destination, profile, operation );
    return prepare_transcode_async_create( NULL, NULL, operation );
}

static const struct IMediaTranscoderVtbl media_transcoder_vtbl =
{
    /* IUnknown methods */
    media_transcoder_QueryInterface,
    media_transcoder_AddRef,
    media_transcoder_Release,
    /* IInspectable methods */
    media_transcoder_GetIids,
    media_transcoder_GetRuntimeClassName,
    media_transcoder_GetTrustLevel,
    /* IMediaTranscoder methods */
    media_transcoder_put_TrimStartTime,
    media_transcoder_get_TrimStartTime,
    media_transcoder_put_TrimStopTime,
    media_transcoder_get_TrimStopTime,
    media_transcoder_put_AlwaysReencode,
    media_transcoder_get_AlwaysReencode,
    media_transcoder_put_HardwareAccelerationEnabled,
    media_transcoder_get_HardwareAccelerationEnabled,
    media_transcoder_AddAudioEffect,
    media_transcoder_AddAudioEffectWithSettings,
    media_transcoder_AddVideoEffect,
    media_transcoder_AddVideoEffectWithSettings,
    media_transcoder_ClearEffects,
    media_transcoder_PrepareFileTranscodeAsync,
    media_transcoder_PrepareStreamTranscodeAsync,
};

/* --- IMediaTranscoder2 methods --- */

static HRESULT WINAPI media_transcoder2_QueryInterface( IMediaTranscoder2 *iface, REFIID iid, void **out )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder2( iface );
    return IMediaTranscoder_QueryInterface( &impl->IMediaTranscoder_iface, iid, out );
}

static ULONG WINAPI media_transcoder2_AddRef( IMediaTranscoder2 *iface )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder2( iface );
    return IMediaTranscoder_AddRef( &impl->IMediaTranscoder_iface );
}

static ULONG WINAPI media_transcoder2_Release( IMediaTranscoder2 *iface )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder2( iface );
    return IMediaTranscoder_Release( &impl->IMediaTranscoder_iface );
}

static HRESULT WINAPI media_transcoder2_GetIids( IMediaTranscoder2 *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI media_transcoder2_GetRuntimeClassName( IMediaTranscoder2 *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Transcoding.MediaTranscoder", 39, class_name );
}

static HRESULT WINAPI media_transcoder2_GetTrustLevel( IMediaTranscoder2 *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI media_transcoder2_PrepareMediaStreamSourceTranscodeAsync( IMediaTranscoder2 *iface,
                                                                               __x_ABI_CWindows_CMedia_CCore_CIMediaSource *source,
                                                                               IRandomAccessStream *destination,
                                                                               IMediaEncodingProfile *profile,
                                                                               IAsyncOperation_PrepareTranscodeResult **operation )
{
    TRACE( "iface %p, source %p, destination %p, profile %p, operation %p.\n", iface, source, destination, profile, operation );
    return prepare_transcode_async_create( NULL, NULL, operation );
}

static HRESULT WINAPI media_transcoder2_put_VideoProcessingAlgorithm( IMediaTranscoder2 *iface, MediaVideoProcessingAlgorithm value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder2( iface );
    TRACE( "iface %p, value %u.\n", iface, value );
    impl->video_algo = value;
    return S_OK;
}

static HRESULT WINAPI media_transcoder2_get_VideoProcessingAlgorithm( IMediaTranscoder2 *iface, MediaVideoProcessingAlgorithm *value )
{
    struct media_transcoder *impl = impl_from_IMediaTranscoder2( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = impl->video_algo;
    return S_OK;
}

static const struct IMediaTranscoder2Vtbl media_transcoder2_vtbl =
{
    media_transcoder2_QueryInterface,
    media_transcoder2_AddRef,
    media_transcoder2_Release,
    media_transcoder2_GetIids,
    media_transcoder2_GetRuntimeClassName,
    media_transcoder2_GetTrustLevel,
    media_transcoder2_PrepareMediaStreamSourceTranscodeAsync,
    media_transcoder2_put_VideoProcessingAlgorithm,
    media_transcoder2_get_VideoProcessingAlgorithm,
};

struct media_transcoder_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct media_transcoder_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct media_transcoder_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct media_transcoder_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct media_transcoder_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct media_transcoder_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( L"Windows.Media.Transcoding.MediaTranscoder", 39, class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct media_transcoder *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!(impl = calloc( 1, sizeof( *impl ) )))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }

    impl->IMediaTranscoder_iface.lpVtbl = &media_transcoder_vtbl;
    impl->IMediaTranscoder2_iface.lpVtbl = &media_transcoder2_vtbl;
    impl->ref = 1;

    *instance = (IInspectable *)&impl->IMediaTranscoder_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

static struct media_transcoder_statics media_transcoder_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *media_transcoder_factory = &media_transcoder_statics.IActivationFactory_iface;
