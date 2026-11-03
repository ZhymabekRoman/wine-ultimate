/* WinRT Clipboard statics
 *
 * Copyright 2026 Wine Developers
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

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

/*
 * The Clipboard the app copies to and pastes from.  The prefix has no clipboard
 * service and no roaming/history back end, so the statics keep the content
 * in-process: SetContent stores the package and its view, GetContent hands
 * out the cached view (an empty view before the first SetContent, as on
 * Windows), Clear empties it, Flush answers S_OK and the event registration
 * points accept handlers and hand out non-zero tokens.  The history points of
 * statics2 report a completed, enabled, empty history - GetHistoryItemsAsync
 * is a born-completed operation whose result object is complete at creation,
 * ClearHistory answers the boolean it was contracted to.
 */

struct clipboard_statics
{
    const void *factory_vtbl;   /* IActivationFactory at offset 0 */
    const void *statics_vtbl;   /* IClipboardStatics at offset 1 pointer */
    const void *statics2_vtbl;  /* IClipboardStatics2 at offset 2 pointers */
    LONG ref;
};

static const void *clipboard_factory_vtbl[];
static const void *clipboard_statics_vtbl[];
static const void *clipboard_statics2_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct clipboard_statics *impl_from_any( const void *iface )
{
    static const void *groups[] = { clipboard_factory_vtbl, clipboard_statics_vtbl, clipboard_statics2_vtbl };
    static const int offsets[] = { 0, 1, 2 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct clipboard_statics *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct clipboard_statics *)iface;
}

static const WCHAR clipboard_class[] = L"Windows.ApplicationModel.DataTransfer.Clipboard";

static HRESULT WINAPI clipboard_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct clipboard_statics *impl = impl_from_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IClipboardStatics ))
    {
        *out = (void *)&impl->statics_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IClipboardStatics2 ))
    {
        *out = (void *)&impl->statics2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI clipboard_AddRef( const void *iface )
{
    struct clipboard_statics *impl = impl_from_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI clipboard_Release( const void *iface )
{
    struct clipboard_statics *impl = impl_from_any( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI clipboard_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI clipboard_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( clipboard_class, lstrlenW( clipboard_class ), class_name );
}

static HRESULT WINAPI clipboard_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

/* The Clipboard class is statics-only; ActivateInstance fails, the
 * projection throws the standard "no default constructor" failure. */
static HRESULT WINAPI clipboard_ActivateInstance( const void *iface, void **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );
    return E_FAIL;
}

static const void *clipboard_factory_vtbl[] =
{
    clipboard_QueryInterface,
    clipboard_AddRef,
    clipboard_Release,
    clipboard_GetIids,
    clipboard_GetRuntimeClassName,
    clipboard_GetTrustLevel,
    clipboard_ActivateInstance,
};

/* ------------------------------------------------- clipboard content --- */

static SRWLOCK clipboard_lock = SRWLOCK_INIT;
static struct data_package *clipboard_package;   /* owned */
static struct data_package_view *clipboard_view; /* owned */

static void clipboard_set_content_locked( struct data_package *package )
{
    struct data_package_view *view = create_package_view_object( package );

    if (!view) return;
    if (clipboard_view) IInspectable_Release( (IInspectable *)clipboard_view );
    clipboard_view = view;
    if (clipboard_package) IInspectable_Release( (IInspectable *)clipboard_package );
    clipboard_package = package; /* view holds the reference */
}

static HRESULT WINAPI cstatics_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return clipboard_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI cstatics_AddRef( const void *iface )
{
    return clipboard_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI cstatics_Release( const void *iface )
{
    return clipboard_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI cstatics2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return clipboard_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI cstatics2_AddRef( const void *iface )
{
    return clipboard_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI cstatics2_Release( const void *iface )
{
    return clipboard_Release( (void *)impl_from_any( iface ) );
}

/* ------------------------------------------------- IClipboardStatics --- */


static HRESULT WINAPI statics_GetContent( const void *iface, void **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;

    AcquireSRWLockExclusive( &clipboard_lock );
    if (!clipboard_view) clipboard_set_content_locked( NULL );
    *result = (void *)clipboard_view;
    IInspectable_AddRef( (IInspectable *)clipboard_view );
    ReleaseSRWLockExclusive( &clipboard_lock );
    return S_OK;
}

static HRESULT WINAPI statics_SetContent( const void *iface, void *content )
{
    TRACE( "iface %p, content %p.\n", iface, content );

    if (!content) return E_POINTER;

    AcquireSRWLockExclusive( &clipboard_lock );
    IInspectable_AddRef( (IInspectable *)content );
    clipboard_set_content_locked( (struct data_package *)content );
    ReleaseSRWLockExclusive( &clipboard_lock );
    return S_OK;
}

static HRESULT WINAPI statics_Flush( const void *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI statics_Clear( const void *iface )
{
    TRACE( "iface %p.\n", iface );

    AcquireSRWLockExclusive( &clipboard_lock );
    clipboard_set_content_locked( NULL );
    ReleaseSRWLockExclusive( &clipboard_lock );
    return S_OK;
}

static HRESULT WINAPI statics_add_ContentChanged( const void *iface, void *handler,
                                                  EventRegistrationToken *token )
{
    static LONG next_token;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!token) return E_POINTER;
    token->value = InterlockedIncrement( &next_token );
    return S_OK;
}

static HRESULT WINAPI statics_remove_ContentChanged( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const void *clipboard_statics_vtbl[] =
{
    cstatics_QueryInterface,
    cstatics_AddRef,
    cstatics_Release,
    clipboard_GetIids,
    clipboard_GetRuntimeClassName,
    clipboard_GetTrustLevel,
    statics_GetContent,
    statics_SetContent,
    statics_Flush,
    statics_Clear,
    statics_add_ContentChanged,
    statics_remove_ContentChanged,
};

/* ---------------------------------------------- IClipboardStatics2 ----- */

struct clipboard_history_result
{
    const void *result_vtbl;
    LONG ref;
    struct winrt_vector *items;
};

static inline struct clipboard_history_result *impl_from_result( const void *iface )
{
    return (struct clipboard_history_result *)iface;
}

static HRESULT WINAPI result_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct clipboard_history_result *impl = impl_from_result( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IClipboardHistoryItemsResult ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI result_AddRef( const void *iface )
{
    struct clipboard_history_result *impl = impl_from_result( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI result_Release( const void *iface )
{
    struct clipboard_history_result *impl = impl_from_result( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->items) vector_release( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI result_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI result_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    static const WCHAR *cls = RuntimeClass_Windows_ApplicationModel_DataTransfer_ClipboardHistoryItemsResult;

    if (!class_name) return E_POINTER;
    return WindowsCreateString( cls, lstrlenW( cls ), class_name );
}

static HRESULT WINAPI result_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI result_get_Status( const void *iface,
                                         ClipboardHistoryItemsResultStatus *status )
{
    TRACE( "iface %p, status %p.\n", iface, status );

    if (!status) return E_POINTER;
    *status = ClipboardHistoryItemsResultStatus_Success;
    return S_OK;
}

static HRESULT WINAPI result_get_Items( const void *iface, void **items )
{
    struct clipboard_history_result *impl = impl_from_result( iface );

    TRACE( "iface %p, items %p.\n", iface, items );

    if (!items) return E_POINTER;
    *items = vector_view_iface( impl->items );
    return S_OK;
}

static const void *clipboard_history_result_vtbl[] =
{
    result_QueryInterface,
    result_AddRef,
    result_Release,
    result_GetIids,
    result_GetRuntimeClassName,
    result_GetTrustLevel,
    result_get_Status,
    result_get_Items,
};

static HRESULT WINAPI statics2_GetHistoryItemsAsync( const void *iface, void **operation )
{
    struct clipboard_history_result *result;
    struct async_op *op;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    if (!(result = calloc( 1, sizeof(*result) ))) return E_OUTOFMEMORY;
    result->result_vtbl = clipboard_history_result_vtbl;
    result->ref = 1;
    result->items = create_vector( NULL,
                                   &IID___FIVectorView_1_Windows__CApplicationModel__CDataTransfer__CClipboardHistoryItem,
                                   &IID___FIIterable_1_Windows__CApplicationModel__CDataTransfer__CClipboardHistoryItem,
                                   &IID___FIIterator_1_Windows__CApplicationModel__CDataTransfer__CClipboardHistoryItem,
                                   ELEM_COM, L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.DataTransfer.ClipboardHistoryItem>" );
    if (!result->items)
    {
        free( result );
        return E_OUTOFMEMORY;
    }

    op = create_async_op( &IID___FIAsyncOperation_1_Windows__CApplicationModel__CDataTransfer__CClipboardHistoryItemsResult,
                          L"Windows.Foundation.IAsyncOperation`1<Windows.ApplicationModel.DataTransfer.ClipboardHistoryItemsResult>",
                          result, OP_RESULT_COM );
    IUnknown_Release( (IUnknown *)result );
    if (!op) return E_OUTOFMEMORY;
    *operation = async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI statics2_ClearHistory( const void *iface, BOOLEAN *result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI statics2_DeleteItemFromHistory( const void *iface, void *item, BOOLEAN *result )
{
    TRACE( "iface %p, item %p, result %p.\n", iface, item, result );

    if (!result) return E_POINTER;
    *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI statics2_SetHistoryItemAsContent( const void *iface, void *item,
                                                        SetHistoryItemAsContentStatus *result )
{
    TRACE( "iface %p, item %p, result %p.\n", iface, item, result );

    if (!result) return E_POINTER;
    *result = SetHistoryItemAsContentStatus_Success;
    return S_OK;
}

static HRESULT WINAPI statics2_IsHistoryEnabled( const void *iface, BOOLEAN *result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI statics2_IsRoamingEnabled( const void *iface, BOOLEAN *result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *result = FALSE;
    return S_OK;
}

static HRESULT WINAPI statics2_SetContentWithOptions( const void *iface, void *content, void *options,
                                                      BOOLEAN *result )
{
    TRACE( "iface %p, content %p, options %p, result %p.\n", iface, content, options, result );

    if (!result) return E_POINTER;
    if (!content) return E_POINTER;

    AcquireSRWLockExclusive( &clipboard_lock );
    IInspectable_AddRef( (IInspectable *)content );
    clipboard_set_content_locked( (struct data_package *)content );
    ReleaseSRWLockExclusive( &clipboard_lock );
    *result = TRUE;
    return S_OK;
}

static HRESULT statics2_add_event( EventRegistrationToken *token )
{
    static LONG next_token;

    if (!token) return E_POINTER;
    token->value = InterlockedIncrement( &next_token );
    return S_OK;
}

static HRESULT WINAPI statics2_add_HistoryChanged( const void *iface, void *handler,
                                                   EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return statics2_add_event( token );
}

static HRESULT WINAPI statics2_remove_HistoryChanged( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static HRESULT WINAPI statics2_add_RoamingEnabledChanged( const void *iface, void *handler,
                                                          EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return statics2_add_event( token );
}

static HRESULT WINAPI statics2_remove_RoamingEnabledChanged( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static HRESULT WINAPI statics2_add_HistoryEnabledChanged( const void *iface, void *handler,
                                                          EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return statics2_add_event( token );
}

static HRESULT WINAPI statics2_remove_HistoryEnabledChanged( const void *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return S_OK;
}

static const void *clipboard_statics2_vtbl[] =
{
    cstatics2_QueryInterface,
    cstatics2_AddRef,
    cstatics2_Release,
    clipboard_GetIids,
    clipboard_GetRuntimeClassName,
    clipboard_GetTrustLevel,
    statics2_GetHistoryItemsAsync,
    statics2_ClearHistory,
    statics2_DeleteItemFromHistory,
    statics2_SetHistoryItemAsContent,
    statics2_IsHistoryEnabled,
    statics2_IsRoamingEnabled,
    statics2_SetContentWithOptions,
    statics2_add_HistoryChanged,
    statics2_remove_HistoryChanged,
    statics2_add_RoamingEnabledChanged,
    statics2_remove_RoamingEnabledChanged,
    statics2_add_HistoryEnabledChanged,
    statics2_remove_HistoryEnabledChanged,
};

static struct clipboard_statics clipboard_statics =
{
    clipboard_factory_vtbl,
    clipboard_statics_vtbl,
    clipboard_statics2_vtbl,
    1,
};

IActivationFactory *clipboard_statics_factory = (IActivationFactory *)&clipboard_statics;
