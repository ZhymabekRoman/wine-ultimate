/*
 * Copyright 2024 Zhiyi Zhang for CodeWeavers
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

#include <stdint.h>

#include "private.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

static HRESULT create_core_dragdrop_target(IInspectable **out);

/* Exported by combase, not declared by the fork's activation.h. */
HRESULT WINAPI RoGetActivationFactory(HSTRING class_id, REFIID iid, void **factory);

struct dataexchange
{
    IActivationFactory IActivationFactory_iface;
    ICoreDragDropManagerStatics ICoreDragDropManagerStatics_iface;
    IDragDropManagerInterop IDragDropManagerInterop_iface;
    LONG ref;
};

static inline struct dataexchange *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct dataexchange, IActivationFactory_iface);
}

static HRESULT STDMETHODCALLTYPE dataexchange_QueryInterface(IActivationFactory *iface, REFIID iid,
                                                             void **out)
{
    struct dataexchange *impl = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
        || IsEqualGUID(iid, &IID_IInspectable)
        || IsEqualGUID(iid, &IID_IAgileObject)
        || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_ICoreDragDropManagerStatics))
    {
        ICoreDragDropManagerStatics_AddRef(&impl->ICoreDragDropManagerStatics_iface);
        *out = &impl->ICoreDragDropManagerStatics_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_IDragDropManagerInterop))
    {
        IDragDropManagerInterop_AddRef(&impl->IDragDropManagerInterop_iface);
        *out = &impl->IDragDropManagerInterop_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dataexchange_AddRef(IActivationFactory *iface)
{
    struct dataexchange *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE dataexchange_Release(IActivationFactory *iface)
{
    struct dataexchange *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT STDMETHODCALLTYPE dataexchange_GetIids(IActivationFactory *iface, ULONG *iid_count,
                                                      IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dataexchange_GetRuntimeClassName(IActivationFactory *iface,
                                                                  HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dataexchange_GetTrustLevel(IActivationFactory *iface,
                                                            TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dataexchange_ActivateInstance(IActivationFactory *iface,
                                                               IInspectable **instance)
{
    TRACE("iface %p, instance %p.\n", iface, instance);

    /* Activating CoreDragDropTarget yields a fresh default drop target. */
    return create_core_dragdrop_target(instance);
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    dataexchange_QueryInterface,
    dataexchange_AddRef,
    dataexchange_Release,
    /* IInspectable methods */
    dataexchange_GetIids,
    dataexchange_GetRuntimeClassName,
    dataexchange_GetTrustLevel,
    /* IActivationFactory methods */
    dataexchange_ActivateInstance,
};

struct async_operation
{
    IAsyncInfo IAsyncInfo_iface;
    __FIAsyncOperation_1_DataPackageOperation IAsyncOperation_iface;
    LONG ref;
    __FIAsyncOperationCompletedHandler_1_DataPackageOperation *completed;
};

static inline struct async_operation *async_operation_from_IAsyncInfo(IAsyncInfo *iface)
{
    return CONTAINING_RECORD(iface, struct async_operation, IAsyncInfo_iface);
}

static inline struct async_operation *async_operation_from_IAsyncOperation(
        __FIAsyncOperation_1_DataPackageOperation *iface)
{
    return CONTAINING_RECORD(iface, struct async_operation, IAsyncOperation_iface);
}

static const struct IAsyncInfoVtbl async_operation_async_info_vtbl;
static const struct __FIAsyncOperation_1_DataPackageOperationVtbl async_operation_vtbl;

static HRESULT WINAPI async_operation_QueryInterface(IAsyncInfo *iface, REFIID iid, void **out)
{
    struct async_operation *impl = async_operation_from_IAsyncInfo(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IAsyncInfo))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID___FIAsyncOperation_1_DataPackageOperation))
    {
        IAsyncInfo_AddRef(&impl->IAsyncInfo_iface);
        *out = &impl->IAsyncOperation_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_operation_AddRef(IAsyncInfo *iface)
{
    struct async_operation *impl = async_operation_from_IAsyncInfo(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI async_operation_Release(IAsyncInfo *iface)
{
    struct async_operation *impl = async_operation_from_IAsyncInfo(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        __FIAsyncOperationCompletedHandler_1_DataPackageOperation_Release(impl->completed);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI async_operation_GetIids(IAsyncInfo *iface, ULONG *count, IID **iids)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_operation_GetRuntimeClassName(IAsyncInfo *iface, HSTRING *name)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_operation_GetTrustLevel(IAsyncInfo *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_operation_get_Id(IAsyncInfo *iface, UINT32 *value)
{
    *value = 1;
    return S_OK;
}

static HRESULT WINAPI async_operation_get_Status(IAsyncInfo *iface, AsyncStatus *value)
{
    *value = Started;
    return S_OK;
}

static HRESULT WINAPI async_operation_get_ErrorCode(IAsyncInfo *iface, HRESULT *value)
{
    *value = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_operation_get_Completed(IAsyncInfo *iface,
        __FIAsyncOperationCompletedHandler_1_DataPackageOperation **handler)
{
    struct async_operation *impl = async_operation_from_IAsyncInfo(iface);

    *handler = impl->completed;
    if (*handler) __FIAsyncOperationCompletedHandler_1_DataPackageOperation_AddRef(*handler);
    return S_OK;
}

static HRESULT WINAPI async_operation_put_Completed(IAsyncInfo *iface,
        __FIAsyncOperationCompletedHandler_1_DataPackageOperation *handler)
{
    struct async_operation *impl = async_operation_from_IAsyncInfo(iface);

    __FIAsyncOperationCompletedHandler_1_DataPackageOperation_AddRef(handler);
    __FIAsyncOperationCompletedHandler_1_DataPackageOperation_Release(impl->completed);
    impl->completed = handler;
    return S_OK;
}

static HRESULT WINAPI async_operation_Cancel(IAsyncInfo *iface)
{
    return S_OK;
}

static HRESULT WINAPI async_operation_Close(IAsyncInfo *iface)
{
    return S_OK;
}

static const struct IAsyncInfoVtbl async_operation_async_info_vtbl =
{
    async_operation_QueryInterface,
    async_operation_AddRef,
    async_operation_Release,
    async_operation_GetIids,
    async_operation_GetRuntimeClassName,
    async_operation_GetTrustLevel,
    async_operation_get_Id,
    async_operation_get_Status,
    async_operation_get_ErrorCode,
    async_operation_get_Completed,
    async_operation_put_Completed,
    async_operation_Cancel,
    async_operation_Close,
};

static HRESULT WINAPI async_operation_op_QueryInterface(__FIAsyncOperation_1_DataPackageOperation *iface,
        REFIID iid, void **out)
{
    struct async_operation *impl = async_operation_from_IAsyncOperation(iface);
    return IAsyncInfo_QueryInterface(&impl->IAsyncInfo_iface, iid, out);
}

static ULONG WINAPI async_operation_op_AddRef(__FIAsyncOperation_1_DataPackageOperation *iface)
{
    struct async_operation *impl = async_operation_from_IAsyncOperation(iface);
    return IAsyncInfo_AddRef(&impl->IAsyncInfo_iface);
}

static ULONG WINAPI async_operation_op_Release(__FIAsyncOperation_1_DataPackageOperation *iface)
{
    struct async_operation *impl = async_operation_from_IAsyncOperation(iface);
    return IAsyncInfo_Release(&impl->IAsyncInfo_iface);
}

static HRESULT WINAPI async_operation_op_GetIids(__FIAsyncOperation_1_DataPackageOperation *iface,
        ULONG *count, IID **iids)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_operation_op_GetRuntimeClassName(__FIAsyncOperation_1_DataPackageOperation *iface,
        HSTRING *name)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_operation_op_GetTrustLevel(__FIAsyncOperation_1_DataPackageOperation *iface,
        TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_operation_op_GetResults(__FIAsyncOperation_1_DataPackageOperation *iface,
        DataPackageOperation *value)
{
    *value = DataPackageOperation_Copy;
    return S_OK;
}

static HRESULT WINAPI async_operation_op_put_Completed(__FIAsyncOperation_1_DataPackageOperation *iface,
        __FIAsyncOperationCompletedHandler_1_DataPackageOperation *handler)
{
    struct async_operation *impl = async_operation_from_IAsyncOperation(iface);

    __FIAsyncOperationCompletedHandler_1_DataPackageOperation_AddRef(handler);
    __FIAsyncOperationCompletedHandler_1_DataPackageOperation_Release(impl->completed);
    impl->completed = handler;
    return S_OK;
}

static HRESULT WINAPI async_operation_op_get_Completed(__FIAsyncOperation_1_DataPackageOperation *iface,
        __FIAsyncOperationCompletedHandler_1_DataPackageOperation **handler)
{
    struct async_operation *impl = async_operation_from_IAsyncOperation(iface);

    *handler = impl->completed;
    if (*handler) __FIAsyncOperationCompletedHandler_1_DataPackageOperation_AddRef(*handler);
    return S_OK;
}

static const struct __FIAsyncOperation_1_DataPackageOperationVtbl async_operation_vtbl =
{
    async_operation_op_QueryInterface,
    async_operation_op_AddRef,
    async_operation_op_Release,
    async_operation_op_GetIids,
    async_operation_op_GetRuntimeClassName,
    async_operation_op_GetTrustLevel,
    /* IAsyncInfo methods */
    async_operation_get_Id,
    async_operation_get_Status,
    async_operation_get_ErrorCode,
    async_operation_get_Completed,
    async_operation_put_Completed,
    async_operation_Cancel,
    async_operation_Close,
    /* __FIAsyncOperation_1_DataPackageOperation methods */
    async_operation_op_GetResults,
    async_operation_op_put_Completed,
    async_operation_op_get_Completed,
};

static HRESULT create_async_operation(__FIAsyncOperation_1_DataPackageOperation **out)
{
    struct async_operation *impl;

    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IAsyncInfo_iface.lpVtbl = &async_operation_async_info_vtbl;
    impl->IAsyncOperation_iface.lpVtbl = &async_operation_vtbl;
    impl->ref = 1;
    *out = &impl->IAsyncOperation_iface;
    return S_OK;
}

struct async_action
{
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IAsyncActionCompletedHandler *completed;
};

static inline struct async_action *async_action_from_IAsyncAction(IAsyncAction *iface)
{
    return CONTAINING_RECORD(iface, struct async_action, IAsyncAction_iface);
}

static const struct IAsyncInfoVtbl async_action_async_info_vtbl;
static const struct IAsyncActionVtbl async_action_vtbl;

static HRESULT WINAPI async_action_QueryInterface(IAsyncAction *iface, REFIID iid, void **out)
{
    struct async_action *impl = async_action_from_IAsyncAction(iface);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_IAsyncAction))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_IAsyncInfo))
    {
        IAsyncAction_AddRef(iface);
        *out = &impl->IAsyncInfo_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_action_AddRef(IAsyncAction *iface)
{
    struct async_action *impl = async_action_from_IAsyncAction(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI async_action_Release(IAsyncAction *iface)
{
    struct async_action *impl = async_action_from_IAsyncAction(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        IAsyncActionCompletedHandler_Release(impl->completed);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI async_action_GetIids(IAsyncAction *iface, ULONG *count, IID **iids)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_action_GetRuntimeClassName(IAsyncAction *iface, HSTRING *name)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_action_GetTrustLevel(IAsyncAction *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_action_put_Completed(IAsyncAction *iface, IAsyncActionCompletedHandler *handler)
{
    struct async_action *impl = async_action_from_IAsyncAction(iface);

    IAsyncActionCompletedHandler_AddRef(handler);
    IAsyncActionCompletedHandler_Release(impl->completed);
    impl->completed = handler;
    return S_OK;
}

static HRESULT WINAPI async_action_get_Completed(IAsyncAction *iface, IAsyncActionCompletedHandler **handler)
{
    struct async_action *impl = async_action_from_IAsyncAction(iface);

    *handler = impl->completed;
    if (*handler) IAsyncActionCompletedHandler_AddRef(*handler);
    return S_OK;
}

static HRESULT WINAPI async_action_GetResults(IAsyncAction *iface)
{
    return S_OK;
}

static const struct IAsyncActionVtbl async_action_vtbl =
{
    async_action_QueryInterface,
    async_action_AddRef,
    async_action_Release,
    async_action_GetIids,
    async_action_GetRuntimeClassName,
    async_action_GetTrustLevel,
    async_action_put_Completed,
    async_action_get_Completed,
    async_action_GetResults,
};

static HRESULT WINAPI async_action_info_QueryInterface(IAsyncInfo *iface, REFIID iid, void **out)
{
    struct async_action *impl = CONTAINING_RECORD(iface, struct async_action, IAsyncInfo_iface);
    return IAsyncAction_QueryInterface(&impl->IAsyncAction_iface, iid, out);
}

static ULONG WINAPI async_action_info_AddRef(IAsyncInfo *iface)
{
    struct async_action *impl = CONTAINING_RECORD(iface, struct async_action, IAsyncInfo_iface);
    return IAsyncAction_AddRef(&impl->IAsyncAction_iface);
}

static ULONG WINAPI async_action_info_Release(IAsyncInfo *iface)
{
    struct async_action *impl = CONTAINING_RECORD(iface, struct async_action, IAsyncInfo_iface);
    return IAsyncAction_Release(&impl->IAsyncAction_iface);
}

static HRESULT WINAPI async_action_info_GetIids(IAsyncInfo *iface, ULONG *count, IID **iids)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_action_info_GetRuntimeClassName(IAsyncInfo *iface, HSTRING *name)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_action_info_GetTrustLevel(IAsyncInfo *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_action_info_get_Id(IAsyncInfo *iface, UINT32 *value)
{
    *value = 1;
    return S_OK;
}

static HRESULT WINAPI async_action_info_get_Status(IAsyncInfo *iface, AsyncStatus *value)
{
    *value = Started;
    return S_OK;
}

static HRESULT WINAPI async_action_info_get_ErrorCode(IAsyncInfo *iface, HRESULT *value)
{
    *value = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_action_info_get_Completed(IAsyncInfo *iface,
        IAsyncActionCompletedHandler **handler)
{
    struct async_action *impl = CONTAINING_RECORD(iface, struct async_action, IAsyncInfo_iface);
    return IAsyncAction_get_Completed(&impl->IAsyncAction_iface, handler);
}

static HRESULT WINAPI async_action_info_put_Completed(IAsyncInfo *iface,
        IAsyncActionCompletedHandler *handler)
{
    struct async_action *impl = CONTAINING_RECORD(iface, struct async_action, IAsyncInfo_iface);
    return IAsyncAction_put_Completed(&impl->IAsyncAction_iface, handler);
}

static HRESULT WINAPI async_action_info_Cancel(IAsyncInfo *iface)
{
    return S_OK;
}

static HRESULT WINAPI async_action_info_Close(IAsyncInfo *iface)
{
    return S_OK;
}

static const struct IAsyncInfoVtbl async_action_async_info_vtbl =
{
    async_action_info_QueryInterface,
    async_action_info_AddRef,
    async_action_info_Release,
    async_action_info_GetIids,
    async_action_info_GetRuntimeClassName,
    async_action_info_GetTrustLevel,
    async_action_info_get_Id,
    async_action_info_get_Status,
    async_action_info_get_ErrorCode,
    async_action_info_get_Completed,
    async_action_info_put_Completed,
    async_action_info_Cancel,
    async_action_info_Close,
};

static HRESULT create_async_action(IAsyncAction **out)
{
    struct async_action *impl;

    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IAsyncAction_iface.lpVtbl = &async_action_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &async_action_async_info_vtbl;
    impl->ref = 1;
    *out = &impl->IAsyncAction_iface;
    return S_OK;
}

struct core_dragdrop_target
{
    ICoreDropOperationTarget ICoreDropOperationTarget_iface;
    LONG ref;
};

static inline struct core_dragdrop_target *core_dragdrop_target_from_iface(ICoreDropOperationTarget *iface)
{
    return CONTAINING_RECORD(iface, struct core_dragdrop_target, ICoreDropOperationTarget_iface);
}

static HRESULT WINAPI core_dragdrop_target_QueryInterface(ICoreDropOperationTarget *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable)
            || IsEqualGUID(iid, &IID_ICoreDropOperationTarget))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI core_dragdrop_target_AddRef(ICoreDropOperationTarget *iface)
{
    struct core_dragdrop_target *impl = core_dragdrop_target_from_iface(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI core_dragdrop_target_Release(ICoreDropOperationTarget *iface)
{
    struct core_dragdrop_target *impl = core_dragdrop_target_from_iface(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
        free(impl);
    return ref;
}

static HRESULT WINAPI core_dragdrop_target_GetIids(ICoreDropOperationTarget *iface, ULONG *count, IID **iids)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI core_dragdrop_target_GetRuntimeClassName(ICoreDropOperationTarget *iface, HSTRING *name)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI core_dragdrop_target_GetTrustLevel(ICoreDropOperationTarget *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI core_dragdrop_target_EnterAsync(ICoreDropOperationTarget *iface,
        __x_ABI_CWindows_CApplicationModel_CDataTransfer_CDragDrop_CCore_CCoreDragInfo *info, __x_ABI_CWindows_CApplicationModel_CDataTransfer_CDragDrop_CCore_CCoreDragUIOverride *override,
        __FIAsyncOperation_1_DataPackageOperation **return_value)
{
    TRACE("iface %p, info %p, override %p, return_value %p.\n", iface, info, override, return_value);
    return create_async_operation(return_value);
}

static HRESULT WINAPI core_dragdrop_target_OverAsync(ICoreDropOperationTarget *iface,
        __x_ABI_CWindows_CApplicationModel_CDataTransfer_CDragDrop_CCore_CCoreDragInfo *info, __x_ABI_CWindows_CApplicationModel_CDataTransfer_CDragDrop_CCore_CCoreDragUIOverride *override,
        __FIAsyncOperation_1_DataPackageOperation **return_value)
{
    TRACE("iface %p, info %p, override %p, return_value %p.\n", iface, info, override, return_value);
    return create_async_operation(return_value);
}

static HRESULT WINAPI core_dragdrop_target_LeaveAsync(ICoreDropOperationTarget *iface,
        __x_ABI_CWindows_CApplicationModel_CDataTransfer_CDragDrop_CCore_CCoreDragInfo *info, IAsyncAction **return_value)
{
    TRACE("iface %p, info %p, return_value %p.\n", iface, info, return_value);
    return create_async_action(return_value);
}

static HRESULT WINAPI core_dragdrop_target_DropAsync(ICoreDropOperationTarget *iface,
        __x_ABI_CWindows_CApplicationModel_CDataTransfer_CDragDrop_CCore_CCoreDragInfo *info, __FIAsyncOperation_1_DataPackageOperation **return_value)
{
    TRACE("iface %p, info %p, return_value %p.\n", iface, info, return_value);
    return create_async_operation(return_value);
}

static const struct ICoreDropOperationTargetVtbl core_dragdrop_target_vtbl =
{
    core_dragdrop_target_QueryInterface,
    core_dragdrop_target_AddRef,
    core_dragdrop_target_Release,
    core_dragdrop_target_GetIids,
    core_dragdrop_target_GetRuntimeClassName,
    core_dragdrop_target_GetTrustLevel,
    core_dragdrop_target_EnterAsync,
    core_dragdrop_target_OverAsync,
    core_dragdrop_target_LeaveAsync,
    core_dragdrop_target_DropAsync,
};

static HRESULT create_core_dragdrop_target(IInspectable **out)
{
    struct core_dragdrop_target *impl;

    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->ICoreDropOperationTarget_iface.lpVtbl = &core_dragdrop_target_vtbl;
    impl->ref = 1;
    *out = (IInspectable *)&impl->ICoreDropOperationTarget_iface;
    return S_OK;
}

struct core_dragdrop_manager
{
    ICoreDragDropManager ICoreDragDropManager_iface;
    struct list registry_entry;           /* linked when registered under a CoreWindow */
    IUnknown *window;                     /* registry key, reference owned while registered */
    BOOL registered;
    BOOL concurrent;
    ICoreDropOperationTarget *target;     /* default CoreDragDropTarget, created on demand */
    ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs *handler;
    int64_t token;                        /* token handed out for the registered handler */
    HWND hwnd;
    LONG ref;
};

struct manager_registry_entry
{
    struct list entry;
    IUnknown *key;
    struct core_dragdrop_manager *manager;
};

/* Managers are created once per CoreWindow, keyed by the CoreWindow identity
 * pointer; the client expects the same manager back for the same window. */
static struct list manager_list = LIST_INIT( manager_list );
static CRITICAL_SECTION manager_cs;
static INIT_ONCE manager_cs_once = INIT_ONCE_STATIC_INIT;
static LONG64 manager_token;

static BOOL CALLBACK manager_cs_init( INIT_ONCE *once, void *param, void **context )
{
    InitializeCriticalSection( &manager_cs );
    return TRUE;
}

static inline struct core_dragdrop_manager *impl_from_ICoreDragDropManager(ICoreDragDropManager *iface)
{
    return CONTAINING_RECORD(iface, struct core_dragdrop_manager, ICoreDragDropManager_iface);
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_QueryInterface(ICoreDragDropManager *iface,
                                                                      REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
        || IsEqualGUID(iid, &IID_IInspectable)
        || IsEqualGUID(iid, &IID_ICoreDragDropManager))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE core_dragdrop_manager_AddRef(ICoreDragDropManager *iface)
{
    struct core_dragdrop_manager *impl = impl_from_ICoreDragDropManager(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE core_dragdrop_manager_Release(ICoreDragDropManager *iface)
{
    struct core_dragdrop_manager *impl = impl_from_ICoreDragDropManager(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    TRACE("iface %p, ref %lu.\n", iface, ref);

    if (!ref)
    {
        if (impl->registered)
        {
            EnterCriticalSection(&manager_cs);
            list_remove(&impl->registry_entry);
            LeaveCriticalSection(&manager_cs);
            IUnknown_Release(impl->window);
        }
        ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs_Release(impl->handler);
        ICoreDropOperationTarget_Release(impl->target);
        free(impl);
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_GetIids(ICoreDragDropManager *iface,
                                                               ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_GetRuntimeClassName(ICoreDragDropManager *iface,
                                                                           HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_GetTrustLevel(ICoreDragDropManager *iface,
                                                                     TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_add_TargetRequested(ICoreDragDropManager *iface,
                                                                           ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs *value,
                                                                           EventRegistrationToken *return_value)
{
    struct core_dragdrop_manager *impl = impl_from_ICoreDragDropManager(iface);
    int64_t token;

    TRACE("iface %p, value %p, return_value %p.\n", iface, value, return_value);

    /* Monotonic nonzero tokens, matching the Windows event registration. */
    token = InterlockedIncrement64(&manager_token);
    ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs_AddRef(value);
    ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs_Release(impl->handler);
    impl->handler = value;
    impl->token = token;
    return_value->value = token;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_remove_TargetRequested(ICoreDragDropManager *iface,
                                                                              EventRegistrationToken value)
{
    struct core_dragdrop_manager *impl = impl_from_ICoreDragDropManager(iface);

    TRACE("iface %p, value %#I64x.\n", iface, value.value);

    if (value.value != impl->token)
        return E_INVALIDARG;

    ITypedEventHandler_CoreDragDropManager_CoreDropOperationTargetRequestedEventArgs_Release(impl->handler);
    impl->handler = NULL;
    impl->token = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_get_AreConcurrentOperationsEnabled(ICoreDragDropManager *iface,
                                                                                          boolean *value)
{
    struct core_dragdrop_manager *impl = impl_from_ICoreDragDropManager(iface);

    TRACE("iface %p, value %p.\n", iface, value);

    *value = impl->concurrent;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_put_AreConcurrentOperationsEnabled(ICoreDragDropManager *iface,
                                                                                          boolean value)
{
    struct core_dragdrop_manager *impl = impl_from_ICoreDragDropManager(iface);

    TRACE("iface %p, value %d.\n", iface, value);

    impl->concurrent = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_get_Target(ICoreDragDropManager *iface,
                                                                  ICoreDropOperationTarget **value)
{
    struct core_dragdrop_manager *impl = impl_from_ICoreDragDropManager(iface);
    HRESULT hr;

    TRACE("iface %p, value %p.\n", iface, value);

    if (impl->target)
    {
        ICoreDropOperationTarget_AddRef(impl->target);
        *value = impl->target;
        return S_OK;
    }

    if (FAILED(hr = create_core_dragdrop_target((IInspectable **)&impl->target)))
        return hr;

    ICoreDropOperationTarget_AddRef(impl->target);
    *value = impl->target;
    return S_OK;
}

static const struct ICoreDragDropManagerVtbl core_dragdrop_manager_vtbl =
{
    core_dragdrop_manager_QueryInterface,
    core_dragdrop_manager_AddRef,
    core_dragdrop_manager_Release,
    /* IInspectable methods */
    core_dragdrop_manager_GetIids,
    core_dragdrop_manager_GetRuntimeClassName,
    core_dragdrop_manager_GetTrustLevel,
    /* ICoreDragDropManager methods */
    core_dragdrop_manager_add_TargetRequested,
    core_dragdrop_manager_remove_TargetRequested,
    core_dragdrop_manager_get_AreConcurrentOperationsEnabled,
    core_dragdrop_manager_put_AreConcurrentOperationsEnabled,
    core_dragdrop_manager_get_Target,
};

static HRESULT create_core_dragdrop_manager(HWND hwnd, REFIID iid, void **out)
{
    struct core_dragdrop_manager *manager;
    HRESULT hr;

    if (!IsWindow(hwnd))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

    if (hwnd == GetDesktopWindow())
        return E_ACCESSDENIED;

    if (!(manager = calloc(1, sizeof(*manager))))
        return E_OUTOFMEMORY;

    manager->ICoreDragDropManager_iface.lpVtbl = &core_dragdrop_manager_vtbl;
    manager->hwnd = hwnd;
    manager->ref = 1;
    hr = ICoreDragDropManager_QueryInterface(&manager->ICoreDragDropManager_iface, iid, out);
    ICoreDragDropManager_Release(&manager->ICoreDragDropManager_iface);
    return hr;
}

DEFINE_IINSPECTABLE(core_dragdrop_manager_statics, ICoreDragDropManagerStatics, struct dataexchange,
                    IActivationFactory_iface)

static HRESULT manager_for_window( __x_ABI_CWindows_CUI_CCore_CICoreWindow *window,
        ICoreDragDropManager **value)
{
    struct manager_registry_entry *entry;
    struct core_dragdrop_manager *manager;
    IUnknown *key = (IUnknown *)window;
    HRESULT hr;

    InitOnceExecuteOnce( &manager_cs_once, manager_cs_init, NULL, NULL );

    EnterCriticalSection( &manager_cs );
    LIST_FOR_EACH_ENTRY( entry, &manager_list, struct manager_registry_entry, entry )
    {
        if (entry->key == key)
        {
            hr = ICoreDragDropManager_QueryInterface( &entry->manager->ICoreDragDropManager_iface,
                    &IID_ICoreDragDropManager, (void **)value );
            LeaveCriticalSection( &manager_cs );
            return hr;
        }
    }

    if (!(manager = calloc( 1, sizeof(*manager) )))
    {
        LeaveCriticalSection( &manager_cs );
        return E_OUTOFMEMORY;
    }
    manager->ICoreDragDropManager_iface.lpVtbl = &core_dragdrop_manager_vtbl;
    manager->ref = 1;

    if (!(entry = malloc( sizeof(*entry) )))
    {
        LeaveCriticalSection( &manager_cs );
        free( manager );
        return E_OUTOFMEMORY;
    }
    IUnknown_AddRef( key );
    entry->key = key;
    entry->manager = manager;
    list_add_tail( &manager_list, &entry->entry );
    manager->registry_entry = entry->entry;
    manager->window = key;
    manager->registered = TRUE;

    hr = ICoreDragDropManager_QueryInterface( &manager->ICoreDragDropManager_iface,
            &IID_ICoreDragDropManager, (void **)value );
    LeaveCriticalSection( &manager_cs );
    return hr;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_statics_GetForCurrentView(ICoreDragDropManagerStatics *iface,
                                                                                 ICoreDragDropManager **value)
{
    __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic *corewindow_statics;
    __x_ABI_CWindows_CUI_CCore_CICoreWindow *window;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    TRACE("iface %p, value %p.\n", iface, value);

    if (!value)
        return E_POINTER;

    hr = WindowsCreateString( RuntimeClass_Windows_UI_Core_CoreWindow,
            wcslen( RuntimeClass_Windows_UI_Core_CoreWindow ), &str );
    if (FAILED( hr ))
        return hr;
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (FAILED( hr ))
        return hr;

    hr = IActivationFactory_QueryInterface( factory, &IID___x_ABI_CWindows_CUI_CCore_CICoreWindowStatic,
            (void **)&corewindow_statics );
    IActivationFactory_Release( factory );
    if (FAILED( hr ))
        return hr;

    hr = __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_GetForCurrentThread( corewindow_statics, &window );
    __x_ABI_CWindows_CUI_CCore_CICoreWindowStatic_Release( corewindow_statics );
    if (FAILED( hr ))
        return hr;

    hr = manager_for_window( window, value );
    __x_ABI_CWindows_CUI_CCore_CICoreWindow_Release( window );
    return hr;
}

static HRESULT STDMETHODCALLTYPE core_dragdrop_manager_statics_GetForCoreWindow(ICoreDragDropManagerStatics *iface,
                                                                                __x_ABI_CWindows_CUI_CCore_CICoreWindow *window,
                                                                                ICoreDragDropManager **value)
{
    TRACE("iface %p, window %p, value %p.\n", iface, window, value);

    if (!window || !value)
        return E_POINTER;

    return manager_for_window( window, value );
}

static const struct ICoreDragDropManagerStaticsVtbl core_dragdrop_manager_statics_vtbl =
{
    core_dragdrop_manager_statics_QueryInterface,
    core_dragdrop_manager_statics_AddRef,
    core_dragdrop_manager_statics_Release,
    /* IInspectable methods */
    core_dragdrop_manager_statics_GetIids,
    core_dragdrop_manager_statics_GetRuntimeClassName,
    core_dragdrop_manager_statics_GetTrustLevel,
    /* ICoreDragDropManagerStatics methods */
    core_dragdrop_manager_statics_GetForCurrentView,
    core_dragdrop_manager_statics_GetForCoreWindow,
};

DEFINE_IINSPECTABLE(dragdrop_manager_interop, IDragDropManagerInterop, struct dataexchange,
                    IActivationFactory_iface)

static HRESULT STDMETHODCALLTYPE dragdrop_manager_interop_GetForWindow(IDragDropManagerInterop *iface,
                                                                       HWND hwnd, REFIID iid, void **out)
{
    FIXME("iface %p, hwnd %p, iid %s, out %p semi-stub!\n", iface, hwnd, debugstr_guid(iid), out);

    return create_core_dragdrop_manager(hwnd, iid, out);
}

static const struct IDragDropManagerInteropVtbl dragdrop_manager_interop_vtbl =
{
    dragdrop_manager_interop_QueryInterface,
    dragdrop_manager_interop_AddRef,
    dragdrop_manager_interop_Release,
    /* IInspectable methods */
    dragdrop_manager_interop_GetIids,
    dragdrop_manager_interop_GetRuntimeClassName,
    dragdrop_manager_interop_GetTrustLevel,
    /* IDragDropManagerInterop methods */
    dragdrop_manager_interop_GetForWindow,
};

static struct dataexchange dataexchange =
{
    {&activation_factory_vtbl},
    {&core_dragdrop_manager_statics_vtbl},
    {&dragdrop_manager_interop_vtbl},
    1
};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    TRACE("classid %s, factory %p.\n", debugstr_hstring(classid), factory);
    *factory = &dataexchange.IActivationFactory_iface;
    IUnknown_AddRef(*factory);
    return S_OK;
}
