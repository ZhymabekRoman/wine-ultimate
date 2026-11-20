/* WinRT Windows.UI.Popups Implementation
 *
 * Copyright (C) 2026 Zhymabek Roman
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
#include "asyncinfo.h"
#include "wine/exception.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

static HSTRING hstring_dup( HSTRING str )
{
    const WCHAR *buffer;
    UINT32 len;

    if (!str) return NULL;
    buffer = WindowsGetStringRawBuffer( str, &len );
    if (!buffer) return NULL;
    WindowsCreateString( buffer, len, &str );
    return str;
}

/********************************************************************
 * UICommand
 */

struct uicmd
{
    IUICommand IUICommand_iface;
    LONG ref;
    HSTRING label;
    IUICommandInvokedHandler *invoked;
    IInspectable *id;
};

static inline struct uicmd *uicmd_from_iface( IUICommand *iface )
{
    return CONTAINING_RECORD( iface, struct uicmd, IUICommand_iface );
}

static HRESULT WINAPI uicmd_QueryInterface( IUICommand *iface, REFIID iid, void **out )
{
    struct uicmd *This = uicmd_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IUICommand ))
        *out = &This->IUICommand_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI uicmd_AddRef( IUICommand *iface )
{
    struct uicmd *This = uicmd_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI uicmd_Release( IUICommand *iface )
{
    struct uicmd *This = uicmd_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        WindowsDeleteString( This->label );
        if (This->invoked) IUICommandInvokedHandler_Release( This->invoked );
        if (This->id) IInspectable_Release( This->id );
        HeapFree( GetProcessHeap(), 0, This );
    }
    return ref;
}

static HRESULT WINAPI uicmd_GetIids( IUICommand *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI uicmd_GetRuntimeClassName( IUICommand *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.UI.Popups.UICommand";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI uicmd_GetTrustLevel( IUICommand *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI uicmd_get_Label( IUICommand *iface, HSTRING *value )
{
    struct uicmd *This = uicmd_from_iface( iface );

    if (!value) return E_POINTER;
    *value = hstring_dup( This->label );
    return S_OK;
}

static HRESULT WINAPI uicmd_put_Label( IUICommand *iface, HSTRING value )
{
    struct uicmd *This = uicmd_from_iface( iface );
    HSTRING dup = hstring_dup( value );

    WindowsDeleteString( This->label );
    This->label = dup;
    return S_OK;
}

static HRESULT WINAPI uicmd_get_Invoked( IUICommand *iface, IUICommandInvokedHandler **value )
{
    struct uicmd *This = uicmd_from_iface( iface );

    if (!value) return E_POINTER;
    *value = This->invoked;
    if (*value) IUICommandInvokedHandler_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI uicmd_put_Invoked( IUICommand *iface, IUICommandInvokedHandler *value )
{
    struct uicmd *This = uicmd_from_iface( iface );

    if (value) IUICommandInvokedHandler_AddRef( value );
    if (This->invoked) IUICommandInvokedHandler_Release( This->invoked );
    This->invoked = value;
    return S_OK;
}

static HRESULT WINAPI uicmd_get_Id( IUICommand *iface, IInspectable **value )
{
    struct uicmd *This = uicmd_from_iface( iface );

    if (!value) return E_POINTER;
    *value = This->id;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI uicmd_put_Id( IUICommand *iface, IInspectable *value )
{
    struct uicmd *This = uicmd_from_iface( iface );

    if (value) IInspectable_AddRef( value );
    if (This->id) IInspectable_Release( This->id );
    This->id = value;
    return S_OK;
}

static const struct IUICommandVtbl uicmd_vtbl =
{
    uicmd_QueryInterface,
    uicmd_AddRef,
    uicmd_Release,
    uicmd_GetIids,
    uicmd_GetRuntimeClassName,
    uicmd_GetTrustLevel,
    uicmd_get_Label,
    uicmd_put_Label,
    uicmd_get_Invoked,
    uicmd_put_Invoked,
    uicmd_get_Id,
    uicmd_put_Id,
};

static HRESULT uicmd_create( HSTRING label, IUICommandInvokedHandler *invoked, IInspectable *id,
        IUICommand **out )
{
    struct uicmd *cmd;

    if (!(cmd = HeapAlloc( GetProcessHeap(), 0, sizeof(*cmd) ))) return E_OUTOFMEMORY;
    cmd->IUICommand_iface.lpVtbl = &uicmd_vtbl;
    cmd->ref = 1;
    cmd->label = hstring_dup( label );
    cmd->invoked = invoked;
    cmd->id = id;
    __TRY
    {
        if (cmd->invoked) IUICommandInvokedHandler_AddRef( cmd->invoked );
        if (cmd->id) IInspectable_AddRef( cmd->id );
    }
    __EXCEPT_ALL
    {
    }
    __ENDTRY
    *out = &cmd->IUICommand_iface;
    return S_OK;
}

/********************************************************************
 * IIterable<IUICommand*> command list
 */

struct cmd_list
{
    IIterable_IUICommand IIterable_IUICommand_iface;
    LONG ref;
    IUICommand **items;
    UINT32 count;
    UINT32 capacity;
};

static inline struct cmd_list *cmd_list_from_iface( IIterable_IUICommand *iface )
{
    return CONTAINING_RECORD( iface, struct cmd_list, IIterable_IUICommand_iface );
}

struct cmd_iterator
{
    IIterator_IUICommand IIterator_IUICommand_iface;
    struct cmd_list *list;
    LONG pos;
    LONG ref;
};

static inline struct cmd_iterator *cmd_iter_from_iface( IIterator_IUICommand *iface )
{
    return CONTAINING_RECORD( iface, struct cmd_iterator, IIterator_IUICommand_iface );
}

static HRESULT WINAPI cmd_iterator_QueryInterface( IIterator_IUICommand *iface, REFIID iid, void **out )
{
    struct cmd_iterator *This = cmd_iter_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterator_IUICommand ))
        *out = &This->IIterator_IUICommand_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI cmd_iterator_AddRef( IIterator_IUICommand *iface )
{
    struct cmd_iterator *This = cmd_iter_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI cmd_iterator_Release( IIterator_IUICommand *iface )
{
    struct cmd_iterator *This = cmd_iter_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        IIterable_IUICommand_Release( &This->list->IIterable_IUICommand_iface );
        HeapFree( GetProcessHeap(), 0, This );
    }
    return ref;
}

static HRESULT WINAPI cmd_iterator_GetIids( IIterator_IUICommand *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI cmd_iterator_GetRuntimeClassName( IIterator_IUICommand *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.Foundation.Collections.IIterator";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI cmd_iterator_GetTrustLevel( IIterator_IUICommand *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI cmd_iterator_get_Current( IIterator_IUICommand *iface, IUICommand **item )
{
    struct cmd_iterator *This = cmd_iter_from_iface( iface );

    if (!item) return E_POINTER;
    if (This->pos < 0 || This->pos >= (LONG)This->list->count) return E_FAIL;
    *item = This->list->items[This->pos];
    IUICommand_AddRef( *item );
    return S_OK;
}

static HRESULT WINAPI cmd_iterator_get_HasCurrent( IIterator_IUICommand *iface, boolean *value )
{
    struct cmd_iterator *This = cmd_iter_from_iface( iface );

    if (!value) return E_POINTER;
    *value = (This->pos >= 0 && This->pos < (LONG)This->list->count);
    return S_OK;
}

static HRESULT WINAPI cmd_iterator_MoveNext( IIterator_IUICommand *iface, boolean *value )
{
    struct cmd_iterator *This = cmd_iter_from_iface( iface );

    if (!value) return E_POINTER;
    This->pos++;
    *value = (This->pos < (LONG)This->list->count);
    return S_OK;
}

static HRESULT WINAPI cmd_iterator_GetMany( IIterator_IUICommand *iface, UINT32 count,
        IUICommand **items, UINT32 *actual )
{
    struct cmd_iterator *This = cmd_iter_from_iface( iface );
    UINT32 i;

    if (!items || !actual) return E_POINTER;
    for (i = 0; i < count && This->pos + (LONG)i < (LONG)This->list->count; i++)
    {
        items[i] = This->list->items[This->pos + i];
        IUICommand_AddRef( items[i] );
    }
    This->pos += i;
    *actual = i;
    return S_OK;
}

static const struct IIterator_IUICommandVtbl cmd_iterator_vtbl =
{
    cmd_iterator_QueryInterface,
    cmd_iterator_AddRef,
    cmd_iterator_Release,
    cmd_iterator_GetIids,
    cmd_iterator_GetRuntimeClassName,
    cmd_iterator_GetTrustLevel,
    cmd_iterator_get_Current,
    cmd_iterator_get_HasCurrent,
    cmd_iterator_MoveNext,
    cmd_iterator_GetMany,
};

static HRESULT WINAPI cmd_list_QueryInterface( IIterable_IUICommand *iface, REFIID iid, void **out )
{
    struct cmd_list *This = cmd_list_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterable_IUICommand ))
        *out = &This->IIterable_IUICommand_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI cmd_list_AddRef( IIterable_IUICommand *iface )
{
    struct cmd_list *This = cmd_list_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI cmd_list_Release( IIterable_IUICommand *iface )
{
    struct cmd_list *This = cmd_list_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );
    UINT32 i;

    if (!ref)
    {
        for (i = 0; i < This->count; i++) IUICommand_Release( This->items[i] );
        HeapFree( GetProcessHeap(), 0, This->items );
        HeapFree( GetProcessHeap(), 0, This );
    }
    return ref;
}

static HRESULT WINAPI cmd_list_GetIids( IIterable_IUICommand *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI cmd_list_GetRuntimeClassName( IIterable_IUICommand *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.Foundation.Collections.IIterable";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI cmd_list_GetTrustLevel( IIterable_IUICommand *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI cmd_list_First( IIterable_IUICommand *iface, IIterator_IUICommand **iterator )
{
    struct cmd_list *This = cmd_list_from_iface( iface );
    struct cmd_iterator *iter;

    if (!iterator) return E_POINTER;
    if (!(iter = HeapAlloc( GetProcessHeap(), 0, sizeof(*iter) ))) return E_OUTOFMEMORY;
    iter->IIterator_IUICommand_iface.lpVtbl = &cmd_iterator_vtbl;
    iter->list = This;
    iter->pos = 0;
    iter->ref = 1;
    InterlockedIncrement( &This->ref );
    *iterator = &iter->IIterator_IUICommand_iface;
    return S_OK;
}

static const struct IIterable_IUICommandVtbl cmd_list_vtbl =
{
    cmd_list_QueryInterface,
    cmd_list_AddRef,
    cmd_list_Release,
    cmd_list_GetIids,
    cmd_list_GetRuntimeClassName,
    cmd_list_GetTrustLevel,
    cmd_list_First,
};

static struct cmd_list *cmd_list_create(void)
{
    struct cmd_list *list;

    if (!(list = HeapAlloc( GetProcessHeap(), 0, sizeof(*list) ))) return NULL;
    list->IIterable_IUICommand_iface.lpVtbl = &cmd_list_vtbl;
    list->ref = 1;
    list->items = NULL;
    list->count = list->capacity = 0;
    return list;
}

HRESULT cmd_list_append( struct cmd_list *list, IUICommand *cmd )
{
    IUICommand **items;

    if (list->count == list->capacity)
    {
        UINT32 capacity = list->capacity ? list->capacity * 2 : 4;
        if (!(items = HeapReAlloc( GetProcessHeap(), 0, list->items, capacity * sizeof(*items) )))
            return E_OUTOFMEMORY;
        list->items = items;
        list->capacity = capacity;
    }
    IUICommand_AddRef( cmd );
    list->items[list->count++] = cmd;
    return S_OK;
}

/********************************************************************
 * IAsyncOperation<IUICommand*> returned by ShowAsync
 *
 * The dialog completes synchronously: the selected command is the default
 * command index entry, the first command, or none. The command's Invoked
 * delegate is fired before the awaited operation completes.
 */

struct dialog_op
{
    IAsyncOperation_IUICommand IAsyncOperation_IUICommand_iface;
    LONG ref;
    AsyncStatus status;
    IAsyncOperationCompletedHandler_IUICommand *handler;
    IUICommand *result;
};

static inline struct dialog_op *dialog_op_from_iface( IAsyncOperation_IUICommand *iface )
{
    return CONTAINING_RECORD( iface, struct dialog_op, IAsyncOperation_IUICommand_iface );
}

static HRESULT WINAPI dialog_op_QueryInterface( IAsyncOperation_IUICommand *iface, REFIID iid, void **out )
{
    struct dialog_op *This = dialog_op_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncInfo )
            || IsEqualGUID( iid, &IID_IAsyncOperation_IUICommand ))
        *out = &This->IAsyncOperation_IUICommand_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI dialog_op_AddRef( IAsyncOperation_IUICommand *iface )
{
    struct dialog_op *This = dialog_op_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI dialog_op_Release( IAsyncOperation_IUICommand *iface )
{
    struct dialog_op *This = dialog_op_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        if (This->handler) IAsyncOperationCompletedHandler_IUICommand_Release( This->handler );
        if (This->result) IUICommand_Release( This->result );
        HeapFree( GetProcessHeap(), 0, This );
    }
    return ref;
}

static HRESULT WINAPI dialog_op_GetIids( IAsyncOperation_IUICommand *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI dialog_op_GetRuntimeClassName( IAsyncOperation_IUICommand *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.Foundation.IAsyncOperation`1<Windows.UI.Popups.IUICommand>";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI dialog_op_GetTrustLevel( IAsyncOperation_IUICommand *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dialog_op_put_Completed( IAsyncOperation_IUICommand *iface,
        IAsyncOperationCompletedHandler_IUICommand *handler )
{
    struct dialog_op *This = dialog_op_from_iface( iface );

    if (This->handler) IAsyncOperationCompletedHandler_IUICommand_Release( This->handler );
    This->handler = handler;
    if (This->handler)
    {
        IAsyncOperationCompletedHandler_IUICommand_AddRef( This->handler );
        if (This->status != Started)
            IAsyncOperationCompletedHandler_IUICommand_Invoke( This->handler, iface, This->status );
    }
    return S_OK;
}

static HRESULT WINAPI dialog_op_get_Completed( IAsyncOperation_IUICommand *iface,
        IAsyncOperationCompletedHandler_IUICommand **handler )
{
    struct dialog_op *This = dialog_op_from_iface( iface );

    if (!handler) return E_POINTER;
    *handler = This->handler;
    if (*handler) IAsyncOperationCompletedHandler_IUICommand_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI dialog_op_GetResults( IAsyncOperation_IUICommand *iface, IUICommand **command )
{
    struct dialog_op *This = dialog_op_from_iface( iface );

    if (!command) return E_POINTER;
    if (This->status == Started) return E_ILLEGAL_METHOD_CALL;
    *command = This->result;
    if (*command) IUICommand_AddRef( *command );
    return S_OK;
}

static HRESULT WINAPI dialog_op_get_Id( IAsyncOperation_IUICommand *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI dialog_op_get_Status( IAsyncOperation_IUICommand *iface, AsyncStatus *status )
{
    struct dialog_op *This = dialog_op_from_iface( iface );

    if (!status) return E_POINTER;
    *status = This->status;
    return S_OK;
}

static HRESULT WINAPI dialog_op_get_ErrorCode( IAsyncOperation_IUICommand *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    *error = S_OK;
    return S_OK;
}

static HRESULT WINAPI dialog_op_Cancel( IAsyncOperation_IUICommand *iface )
{
    return S_OK;
}

static HRESULT WINAPI dialog_op_Close( IAsyncOperation_IUICommand *iface )
{
    struct dialog_op *This = dialog_op_from_iface( iface );

    This->status = AsyncStatus_Closed;
    return S_OK;
}

static const void *dialog_op_vtbl[] =
{
    dialog_op_QueryInterface,
    dialog_op_AddRef,
    dialog_op_Release,
    dialog_op_GetIids,
    dialog_op_GetRuntimeClassName,
    dialog_op_GetTrustLevel,
    dialog_op_put_Completed,
    dialog_op_get_Completed,
    dialog_op_GetResults,
    dialog_op_get_Id,
    dialog_op_get_Status,
    dialog_op_get_ErrorCode,
    dialog_op_Cancel,
    dialog_op_Close,
};

static HRESULT dialog_op_create( struct cmd_list *commands, UINT32 default_index,
        IAsyncOperation_IUICommand **out )
{
    struct dialog_op *op;
    IUICommandInvokedHandler *invoked;
    HRESULT hr;

    if (!(op = HeapAlloc( GetProcessHeap(), 0, sizeof(*op) ))) return E_OUTOFMEMORY;
    op->IAsyncOperation_IUICommand_iface.lpVtbl = (const struct IAsyncOperation_IUICommandVtbl *)&dialog_op_vtbl;
    op->ref = 1;
    op->status = Started;
    op->handler = NULL;
    op->result = NULL;

    if (default_index < commands->count) op->result = commands->items[default_index];
    else if (commands->count) op->result = commands->items[0];
    if (op->result)
    {
        IUICommand_AddRef( op->result );
        if (SUCCEEDED( hr = IUICommand_get_Invoked( op->result, &invoked )) && invoked)
        {
            IUICommandInvokedHandler_Invoke( invoked, op->result );
            IUICommandInvokedHandler_Release( invoked );
        }
    }
    op->status = Completed;
    *out = &op->IAsyncOperation_IUICommand_iface;
    return S_OK;
}

/********************************************************************
 * MessageDialog
 */

struct message_dialog
{
    IMessageDialog IMessageDialog_iface;
    LONG ref;
    struct cmd_list *commands;
    HSTRING content;
    HSTRING title;
    UINT32 default_index;
    UINT32 cancel_index;
    MessageDialogOptions options;
};

static inline struct message_dialog *message_dialog_from_iface( IMessageDialog *iface )
{
    return CONTAINING_RECORD( iface, struct message_dialog, IMessageDialog_iface );
}

static HRESULT WINAPI message_dialog_QueryInterface( IMessageDialog *iface, REFIID iid, void **out )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IMessageDialog ))
        *out = &This->IMessageDialog_iface;
    else if (IsEqualGUID( iid, &IID_IIterable_IUICommand ))
        *out = &This->commands->IIterable_IUICommand_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI message_dialog_AddRef( IMessageDialog *iface )
{
    struct message_dialog *This = message_dialog_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI message_dialog_Release( IMessageDialog *iface )
{
    struct message_dialog *This = message_dialog_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        IIterable_IUICommand_Release( &This->commands->IIterable_IUICommand_iface );
        WindowsDeleteString( This->content );
        WindowsDeleteString( This->title );
        HeapFree( GetProcessHeap(), 0, This );
    }
    return ref;
}

static HRESULT WINAPI message_dialog_GetIids( IMessageDialog *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI message_dialog_GetRuntimeClassName( IMessageDialog *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.UI.Popups.MessageDialog";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI message_dialog_GetTrustLevel( IMessageDialog *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI message_dialog_get_Title( IMessageDialog *iface, HSTRING *value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    if (!value) return E_POINTER;
    *value = hstring_dup( This->title );
    return S_OK;
}

static HRESULT WINAPI message_dialog_put_Title( IMessageDialog *iface, HSTRING value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );
    HSTRING dup = hstring_dup( value );

    WindowsDeleteString( This->title );
    This->title = dup;
    return S_OK;
}

static HRESULT WINAPI message_dialog_get_Commands( IMessageDialog *iface, IIterable_IUICommand **value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    if (!value) return E_POINTER;
    *value = &This->commands->IIterable_IUICommand_iface;
    IIterable_IUICommand_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI message_dialog_get_DefaultCommandIndex( IMessageDialog *iface, UINT32 *value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    if (!value) return E_POINTER;
    *value = This->default_index;
    return S_OK;
}

static HRESULT WINAPI message_dialog_put_DefaultCommandIndex( IMessageDialog *iface, UINT32 value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    This->default_index = value;
    return S_OK;
}

static HRESULT WINAPI message_dialog_get_CancelCommandIndex( IMessageDialog *iface, UINT32 *value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    if (!value) return E_POINTER;
    *value = This->cancel_index;
    return S_OK;
}

static HRESULT WINAPI message_dialog_put_CancelCommandIndex( IMessageDialog *iface, UINT32 value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    This->cancel_index = value;
    return S_OK;
}

static HRESULT WINAPI message_dialog_get_Content( IMessageDialog *iface, HSTRING *value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    if (!value) return E_POINTER;
    *value = hstring_dup( This->content );
    return S_OK;
}

static HRESULT WINAPI message_dialog_put_Content( IMessageDialog *iface, HSTRING value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );
    HSTRING dup = hstring_dup( value );

    WindowsDeleteString( This->content );
    This->content = dup;
    return S_OK;
}

static HRESULT WINAPI message_dialog_ShowAsync( IMessageDialog *iface,
        IAsyncOperation_IUICommand **action )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    TRACE( "iface %p, action %p: content %s title %s.\n", iface, action,
            debugstr_hstring( This->content ), debugstr_hstring( This->title ) );

    if (!action) return E_POINTER;
    return dialog_op_create( This->commands, This->default_index, action );
}

static HRESULT WINAPI message_dialog_get_Options( IMessageDialog *iface, MessageDialogOptions *value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    if (!value) return E_POINTER;
    *value = This->options;
    return S_OK;
}

static HRESULT WINAPI message_dialog_put_Options( IMessageDialog *iface, MessageDialogOptions value )
{
    struct message_dialog *This = message_dialog_from_iface( iface );

    This->options = value;
    return S_OK;
}

static const struct IMessageDialogVtbl message_dialog_vtbl =
{
    message_dialog_QueryInterface,
    message_dialog_AddRef,
    message_dialog_Release,
    message_dialog_GetIids,
    message_dialog_GetRuntimeClassName,
    message_dialog_GetTrustLevel,
    message_dialog_get_Title,
    message_dialog_put_Title,
    message_dialog_get_Commands,
    message_dialog_get_DefaultCommandIndex,
    message_dialog_put_DefaultCommandIndex,
    message_dialog_get_CancelCommandIndex,
    message_dialog_put_CancelCommandIndex,
    message_dialog_get_Content,
    message_dialog_put_Content,
    message_dialog_ShowAsync,
    message_dialog_get_Options,
    message_dialog_put_Options,
};

static HRESULT message_dialog_create( HSTRING content, HSTRING title, IMessageDialog **out )
{
    struct message_dialog *dialog;

    if (!(dialog = HeapAlloc( GetProcessHeap(), 0, sizeof(*dialog) ))) return E_OUTOFMEMORY;
    if (!(dialog->commands = cmd_list_create()))
    {
        HeapFree( GetProcessHeap(), 0, dialog );
        return E_OUTOFMEMORY;
    }
    dialog->IMessageDialog_iface.lpVtbl = &message_dialog_vtbl;
    dialog->ref = 1;
    dialog->content = hstring_dup( content );
    dialog->title = hstring_dup( title );
    dialog->default_index = 0;
    dialog->cancel_index = 1;
    dialog->options = MessageDialogOptions_None;
    *out = &dialog->IMessageDialog_iface;
    return S_OK;
}

/********************************************************************
 * PopupMenu
 */

struct popup_menu
{
    IPopupMenu IPopupMenu_iface;
    LONG ref;
    struct cmd_list *commands;
};

static inline struct popup_menu *popup_menu_from_iface( IPopupMenu *iface )
{
    return CONTAINING_RECORD( iface, struct popup_menu, IPopupMenu_iface );
}

static HRESULT WINAPI popup_menu_QueryInterface( IPopupMenu *iface, REFIID iid, void **out )
{
    struct popup_menu *This = popup_menu_from_iface( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPopupMenu ))
        *out = &This->IPopupMenu_iface;
    else if (IsEqualGUID( iid, &IID_IIterable_IUICommand ))
        *out = &This->commands->IIterable_IUICommand_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI popup_menu_AddRef( IPopupMenu *iface )
{
    struct popup_menu *This = popup_menu_from_iface( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI popup_menu_Release( IPopupMenu *iface )
{
    struct popup_menu *This = popup_menu_from_iface( iface );
    ULONG ref = InterlockedDecrement( &This->ref );

    if (!ref)
    {
        IIterable_IUICommand_Release( &This->commands->IIterable_IUICommand_iface );
        HeapFree( GetProcessHeap(), 0, This );
    }
    return ref;
}

static HRESULT WINAPI popup_menu_GetIids( IPopupMenu *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI popup_menu_GetRuntimeClassName( IPopupMenu *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.UI.Popups.PopupMenu";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI popup_menu_GetTrustLevel( IPopupMenu *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI popup_menu_get_Commands( IPopupMenu *iface, IIterable_IUICommand **value )
{
    struct popup_menu *This = popup_menu_from_iface( iface );

    if (!value) return E_POINTER;
    *value = &This->commands->IIterable_IUICommand_iface;
    IIterable_IUICommand_AddRef( *value );
    return S_OK;
}

static HRESULT popup_menu_show( struct popup_menu *This, IAsyncOperation_IUICommand **action )
{
    return dialog_op_create( This->commands, 0, action );
}

static HRESULT WINAPI popup_menu_ShowAsync( IPopupMenu *iface, Point invocation_point,
        IAsyncOperation_IUICommand **action )
{
    struct popup_menu *This = popup_menu_from_iface( iface );

    TRACE( "iface %p, invocation_point %p, action %p.\n", iface, &invocation_point, action );
    return popup_menu_show( This, action );
}

static HRESULT WINAPI popup_menu_ShowAsyncWithRect( IPopupMenu *iface, Rect selection,
        IAsyncOperation_IUICommand **action )
{
    struct popup_menu *This = popup_menu_from_iface( iface );

    TRACE( "iface %p, selection %p, action %p.\n", iface, &selection, action );
    return popup_menu_show( This, action );
}

static HRESULT WINAPI popup_menu_ShowAsyncWithRectAndPlacement( IPopupMenu *iface, Rect selection,
        Placement preferred_placement, IAsyncOperation_IUICommand **action )
{
    struct popup_menu *This = popup_menu_from_iface( iface );

    TRACE( "iface %p, selection %p, preferred_placement %d, action %p.\n",
            iface, &selection, preferred_placement, action );
    return popup_menu_show( This, action );
}

static const struct IPopupMenuVtbl popup_menu_vtbl =
{
    popup_menu_QueryInterface,
    popup_menu_AddRef,
    popup_menu_Release,
    popup_menu_GetIids,
    popup_menu_GetRuntimeClassName,
    popup_menu_GetTrustLevel,
    popup_menu_get_Commands,
    popup_menu_ShowAsync,
    popup_menu_ShowAsyncWithRect,
    popup_menu_ShowAsyncWithRectAndPlacement,
};

/********************************************************************
 * Activation factories
 */

HRESULT popups_activate_instance( IInspectable **out, const WCHAR *class_name );

/* One activation factory object serves the MessageDialog, UICommand,
 * UICommandSeparator and PopupMenu classes; the registry routes every
 * Windows.UI.Popups class id to this module (windows.ui.dll).
 */

struct popup_factory
{
    const void **vtbl_act;
    const void **vtbl_dialog;
    const void **vtbl_cmd;
};

static HRESULT WINAPI popup_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct popup_factory *This = iface;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &This->vtbl_act;
    else if (IsEqualGUID( iid, &IID_IMessageDialogFactory ))
        *out = &This->vtbl_dialog;
    else if (IsEqualGUID( iid, &IID_IUICommandFactory ))
        *out = &This->vtbl_cmd;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

static ULONG WINAPI popup_factory_AddRef( void *iface ) { return 2; }
static ULONG WINAPI popup_factory_Release( void *iface ) { return 1; }

static HRESULT WINAPI popup_factory_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI popup_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR class_w[] = L"Windows.UI.Popups.MessageDialog";
    return WindowsCreateString( class_w, ARRAY_SIZE(class_w) - 1, name );
}

static HRESULT WINAPI popup_factory_GetTrustLevel( void *iface, TrustLevel *trust )
{
    *trust = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI popup_factory_ActivateInstance( void *iface, IInspectable **out )
{
    return popups_activate_instance( out, L"Windows.UI.Popups.MessageDialog" );
}

static HRESULT WINAPI dialog_factory_Create( void *iface, HSTRING content, IMessageDialog **dialog )
{
    TRACE( "iface %p, content %s, dialog %p.\n", iface, debugstr_hstring( content ), dialog );
    if (!dialog) return E_POINTER;
    return message_dialog_create( content, NULL, dialog );
}

static HRESULT WINAPI dialog_factory_CreateWithTitle( void *iface, HSTRING content,
        HSTRING title, IMessageDialog **dialog )
{
    TRACE( "iface %p, content %s, title %s, dialog %p.\n", iface, debugstr_hstring( content ),
            debugstr_hstring( title ), dialog );
    if (!dialog) return E_POINTER;
    return message_dialog_create( content, title, dialog );
}

static HRESULT WINAPI cmd_factory_Create( void *iface, HSTRING label, IUICommand **cmd )
{
    TRACE( "iface %p, label %s, cmd %p.\n", iface, debugstr_hstring( label ), cmd );
    if (!cmd) return E_POINTER;
    return uicmd_create( label, NULL, NULL, cmd );
}

static HRESULT WINAPI cmd_factory_CreateWithHandler( void *iface, HSTRING label,
        IUICommandInvokedHandler *handler, IUICommand **cmd )
{
    const void *slots = handler ? *(const void **)handler : NULL;
    const void *iunk = handler ? *(const void **)((char *)handler + 8) : NULL;

    TRACE( "iface %p, label %s, handler %p, cmd %p.\n", iface, debugstr_hstring( label ), handler, cmd );
    TRACE( "handler { object %p, iunknown %p }\n", slots, iunk );
    if (!cmd) return E_POINTER;
    return uicmd_create( label, handler, NULL, cmd );
}

static HRESULT WINAPI cmd_factory_CreateWithHandlerAndId( void *iface, HSTRING label,
        IUICommandInvokedHandler *handler, IInspectable *id, IUICommand **cmd )
{
    TRACE( "iface %p, label %s, handler %p, id %p, cmd %p.\n",
            iface, debugstr_hstring( label ), handler, id, cmd );
    if (!cmd) return E_POINTER;
    return uicmd_create( label, handler, id, cmd );
}

struct popup_class_factory
{
    const void **vtbl;
    const WCHAR *class_name;
};

static HRESULT WINAPI class_factory_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct popup_class_factory *This = iface;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &This->vtbl;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

static HRESULT WINAPI class_factory_ActivateInstance( void *iface, IInspectable **out )
{
    struct popup_class_factory *This = iface;

    return popups_activate_instance( out, This->class_name );
}

static HRESULT WINAPI class_factory_GetRuntimeClassName( void *iface, HSTRING *name )
{
    struct popup_class_factory *This = iface;

    return WindowsCreateString( This->class_name, lstrlenW( This->class_name ), name );
}

static const void *popup_class_factory_vtbl[] =
{
    class_factory_QueryInterface,
    popup_factory_AddRef,
    popup_factory_Release,
    popup_factory_GetIids,
    class_factory_GetRuntimeClassName,
    popup_factory_GetTrustLevel,
    class_factory_ActivateInstance,
};

static struct popup_class_factory uicmd_separator_factory_obj =
{
    popup_class_factory_vtbl,
    L"Windows.UI.Popups.UICommandSeparator"
};

static struct popup_class_factory popupmenu_factory_obj =
{
    popup_class_factory_vtbl,
    L"Windows.UI.Popups.PopupMenu"
};

IActivationFactory *uicmd_separator_factory = (IActivationFactory *)&uicmd_separator_factory_obj.vtbl;
IActivationFactory *popupmenu_factory = (IActivationFactory *)&popupmenu_factory_obj.vtbl;

static const void *popup_factory_act_vtbl[] =
{
    popup_factory_QueryInterface,
    popup_factory_AddRef,
    popup_factory_Release,
    popup_factory_GetIids,
    popup_factory_GetRuntimeClassName,
    popup_factory_GetTrustLevel,
    popup_factory_ActivateInstance,
};

static const void *dialog_factory_vtbl[] =
{
    popup_factory_QueryInterface,
    popup_factory_AddRef,
    popup_factory_Release,
    popup_factory_GetIids,
    popup_factory_GetRuntimeClassName,
    popup_factory_GetTrustLevel,
    dialog_factory_Create,
    dialog_factory_CreateWithTitle,
};

static const void *cmd_factory_vtbl[] =
{
    popup_factory_QueryInterface,
    popup_factory_AddRef,
    popup_factory_Release,
    popup_factory_GetIids,
    popup_factory_GetRuntimeClassName,
    popup_factory_GetTrustLevel,
    cmd_factory_Create,
    cmd_factory_CreateWithHandler,
    cmd_factory_CreateWithHandlerAndId,
};

static struct popup_factory popup_factory_obj =
{
    popup_factory_act_vtbl,
    dialog_factory_vtbl,
    cmd_factory_vtbl,
};

IActivationFactory *popup_factory = (IActivationFactory *)&popup_factory_obj.vtbl_act;

HRESULT popups_activate_instance( IInspectable **out, const WCHAR *class_name )
{
    HRESULT hr;

    if (!wcscmp( class_name, L"Windows.UI.Popups.MessageDialog" ))
    {
        IMessageDialog *dialog;

        if (FAILED( hr = message_dialog_create( NULL, NULL, &dialog ))) return hr;
        *out = (IInspectable *)dialog;
        return S_OK;
    }

    if (!wcscmp( class_name, L"Windows.UI.Popups.UICommand" ) ||
        !wcscmp( class_name, L"Windows.UI.Popups.UICommandSeparator" ))
    {
        IUICommand *cmd;

        if (FAILED( hr = uicmd_create( NULL, NULL, NULL, &cmd ))) return hr;
        *out = (IInspectable *)cmd;
        return S_OK;
    }

    if (!wcscmp( class_name, L"Windows.UI.Popups.PopupMenu" ))
    {
        struct popup_menu *menu;

        if (!(menu = HeapAlloc( GetProcessHeap(), 0, sizeof(*menu) ))) return E_OUTOFMEMORY;
        if (!(menu->commands = cmd_list_create()))
        {
            HeapFree( GetProcessHeap(), 0, menu );
            return E_OUTOFMEMORY;
        }
        menu->IPopupMenu_iface.lpVtbl = &popup_menu_vtbl;
        menu->ref = 1;
        *out = (IInspectable *)&menu->IPopupMenu_iface;
        return S_OK;
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}
