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

#define WM_APP_SET_CLIPBOARD   (WM_APP + 1)
#define WM_APP_FLUSH_CLIPBOARD (WM_APP + 2)

struct content_changed_entry
{
    __FIEventHandler_1_IInspectable *handler; /* owned, may be NULL */
    EventRegistrationToken token;
};

static SRWLOCK clipboard_lock = SRWLOCK_INIT;
static struct data_package_view *clipboard_view; /* owned */
static DWORD clipboard_seq_no;
static BOOL clipboard_view_empty;

static struct content_changed_entry *cc_entries;
static ULONG cc_count, cc_alloc;
static LONG next_cc_token;

static INIT_ONCE watcher_init_once = INIT_ONCE_STATIC_INIT;
static HWND watcher_hwnd;
static DWORD watcher_tid;
static HANDLE watcher_ready_event;

static void fire_content_changed( void )
{
    __FIEventHandler_1_IInspectable **snap = NULL;
    ULONG snap_count = 0, i;
    DWORD seq = GetClipboardSequenceNumber();

    AcquireSRWLockExclusive( &clipboard_lock );
    if (clipboard_view && clipboard_seq_no != seq && !(clipboard_view_empty && !CountClipboardFormats()))
    {
        IInspectable_Release( (IInspectable *)clipboard_view );
        clipboard_view = NULL;
        clipboard_seq_no = 0;
        clipboard_view_empty = FALSE;
    }
    if (cc_count)
    {
        snap = malloc( cc_count * sizeof(*snap) );
        if (snap)
        {
            for (i = 0; i < cc_count; i++)
            {
                if (cc_entries[i].handler)
                {
                    snap[snap_count++] = cc_entries[i].handler;
                    __FIEventHandler_1_IInspectable_AddRef( cc_entries[i].handler );
                }
            }
        }
    }
    ReleaseSRWLockExclusive( &clipboard_lock );

    for (i = 0; i < snap_count; i++)
    {
        __FIEventHandler_1_IInspectable_Invoke( snap[i], NULL, NULL );
        __FIEventHandler_1_IInspectable_Release( snap[i] );
    }
    free( snap );
}

static HRESULT ole_set_clipboard_retry( IDataObject *data_obj )
{
    HRESULT hr = E_FAIL;
    int i;

    for (i = 0; i < 10; i++)
    {
        hr = OleSetClipboard( data_obj );
        if (hr != CLIPBRD_E_CANT_OPEN) break;
        Sleep( 2 );
    }
    return hr;
}

static HRESULT ole_flush_clipboard_retry( void )
{
    HRESULT hr = E_FAIL;
    int i;

    for (i = 0; i < 10; i++)
    {
        hr = OleFlushClipboard();
        if (hr != CLIPBRD_E_CANT_OPEN) break;
        Sleep( 2 );
    }
    return hr;
}

static LRESULT CALLBACK clipboard_watcher_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_CLIPBOARDUPDATE:
        TRACE( "WM_CLIPBOARDUPDATE received on watcher %p.\n", hwnd );
        fire_content_changed();
        return 0;
    case WM_APP_SET_CLIPBOARD:
        return (LRESULT)ole_set_clipboard_retry( (IDataObject *)wparam );
    case WM_APP_FLUSH_CLIPBOARD:
        return (LRESULT)ole_flush_clipboard_retry();
    default:
        return DefWindowProcW( hwnd, msg, wparam, lparam );
    }
}

static DWORD WINAPI clipboard_watcher_thread( void *arg )
{
    static const WCHAR wnd_cls[] = L"WineDataExchangeClipboardWatcher";
    HMODULE hmod = NULL;
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;

    OleInitialize( NULL );

    GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (const WCHAR *)clipboard_watcher_wndproc, &hmod );
    memset( &wc, 0, sizeof(wc) );
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = clipboard_watcher_wndproc;
    wc.hInstance = hmod;
    wc.lpszClassName = wnd_cls;
    RegisterClassExW( &wc );

    hwnd = CreateWindowExW( 0, wnd_cls, L"WinRTClipboardWatcher", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, NULL, hmod, NULL );
    if (hwnd)
    {
        AddClipboardFormatListener( hwnd );
        watcher_hwnd = hwnd;
    }
    SetEvent( watcher_ready_event );

    while (hwnd && GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }

    if (hwnd)
    {
        RemoveClipboardFormatListener( hwnd );
        DestroyWindow( hwnd );
    }
    OleUninitialize();
    return 0;
}

static BOOL WINAPI init_clipboard_watcher( INIT_ONCE *once, void *param, void **ctx )
{
    HANDLE thread;

    watcher_ready_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    if (!watcher_ready_event) return FALSE;

    thread = CreateThread( NULL, 0, clipboard_watcher_thread, NULL, 0, &watcher_tid );
    if (!thread)
    {
        CloseHandle( watcher_ready_event );
        watcher_ready_event = NULL;
        return FALSE;
    }
    WaitForSingleObject( watcher_ready_event, 5000 );
    CloseHandle( watcher_ready_event );
    watcher_ready_event = NULL;
    CloseHandle( thread );
    return watcher_hwnd != NULL;
}

static BOOL ensure_clipboard_watcher( void )
{
    return InitOnceExecuteOnce( &watcher_init_once, init_clipboard_watcher, NULL, NULL ) && watcher_hwnd != NULL;
}

static HRESULT dispatch_set_clipboard( IDataObject *data_obj )
{
    if (!ensure_clipboard_watcher()) return E_FAIL;
    if (GetCurrentThreadId() == watcher_tid) return ole_set_clipboard_retry( data_obj );
    return (HRESULT)SendMessageW( watcher_hwnd, WM_APP_SET_CLIPBOARD, (WPARAM)data_obj, 0 );
}

static HRESULT dispatch_flush_clipboard( void )
{
    if (!ensure_clipboard_watcher()) return E_FAIL;
    if (GetCurrentThreadId() == watcher_tid) return ole_flush_clipboard_retry();
    return (HRESULT)SendMessageW( watcher_hwnd, WM_APP_FLUSH_CLIPBOARD, 0, 0 );
}

static HRESULT clipboard_push_package( void *content )
{
    struct data_package_view *view;
    IDataObject *data_obj = NULL;
    HRESULT hr;

    if (!content) return E_POINTER;
    if (!(view = create_package_view_object( (struct data_package *)content )))
        return E_OUTOFMEMORY;
    hr = IInspectable_QueryInterface( (IInspectable *)view, &IID_IDataObject, (void **)&data_obj );
    IInspectable_Release( (IInspectable *)view );
    if (FAILED( hr )) return hr;

    hr = dispatch_set_clipboard( data_obj );
    IDataObject_Release( data_obj );
    if (FAILED( hr )) return hr;

    AcquireSRWLockExclusive( &clipboard_lock );
    if (clipboard_view)
    {
        IInspectable_Release( (IInspectable *)clipboard_view );
        clipboard_view = NULL;
    }
    clipboard_seq_no = 0;
    clipboard_view_empty = FALSE;
    ReleaseSRWLockExclusive( &clipboard_lock );
    return S_OK;
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
    struct data_package_view *new_view;
    IDataObject *clip_obj = NULL;
    DWORD seq;
    BOOL is_empty;
    HRESULT hr;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;

    seq = GetClipboardSequenceNumber();
    is_empty = CountClipboardFormats() == 0;
    AcquireSRWLockExclusive( &clipboard_lock );
    if (clipboard_view && (clipboard_seq_no == seq || (clipboard_view_empty && is_empty)))
    {
        clipboard_seq_no = seq;
        *result = (void *)clipboard_view;
        IInspectable_AddRef( (IInspectable *)clipboard_view );
        ReleaseSRWLockExclusive( &clipboard_lock );
        return S_OK;
    }
    ReleaseSRWLockExclusive( &clipboard_lock );

    hr = OleGetClipboard( &clip_obj );
    new_view = create_package_view_from_data_object( SUCCEEDED( hr ) ? clip_obj : NULL );
    if (clip_obj) IDataObject_Release( clip_obj );
    if (!new_view) return E_OUTOFMEMORY;

    AcquireSRWLockExclusive( &clipboard_lock );
    if (clipboard_view && (clipboard_seq_no == seq || (clipboard_view_empty && is_empty)))
    {
        clipboard_seq_no = seq;
        IInspectable_Release( (IInspectable *)new_view );
        *result = (void *)clipboard_view;
        IInspectable_AddRef( (IInspectable *)clipboard_view );
    }
    else
    {
        if (clipboard_view) IInspectable_Release( (IInspectable *)clipboard_view );
        clipboard_view = new_view;
        clipboard_seq_no = seq;
        clipboard_view_empty = is_empty;
        *result = (void *)clipboard_view;
        IInspectable_AddRef( (IInspectable *)clipboard_view );
    }
    ReleaseSRWLockExclusive( &clipboard_lock );
    return S_OK;
}

static HRESULT WINAPI statics_SetContent( const void *iface, void *content )
{
    TRACE( "iface %p, content %p.\n", iface, content );

    return clipboard_push_package( content );
}

static HRESULT WINAPI statics_Flush( const void *iface )
{
    HRESULT hr;

    TRACE( "iface %p.\n", iface );

    hr = dispatch_flush_clipboard();
    AcquireSRWLockExclusive( &clipboard_lock );
    if (clipboard_view)
    {
        IInspectable_Release( (IInspectable *)clipboard_view );
        clipboard_view = NULL;
    }
    clipboard_seq_no = 0;
    clipboard_view_empty = FALSE;
    ReleaseSRWLockExclusive( &clipboard_lock );
    return hr;
}

static HRESULT WINAPI statics_Clear( const void *iface )
{
    HRESULT hr;

    TRACE( "iface %p.\n", iface );

    hr = dispatch_set_clipboard( NULL );
    AcquireSRWLockExclusive( &clipboard_lock );
    if (clipboard_view)
    {
        IInspectable_Release( (IInspectable *)clipboard_view );
        clipboard_view = NULL;
    }
    clipboard_seq_no = 0;
    clipboard_view_empty = FALSE;
    ReleaseSRWLockExclusive( &clipboard_lock );
    return hr;
}

static HRESULT WINAPI statics_add_ContentChanged( const void *iface, void *handler,
                                                  EventRegistrationToken *token )
{
    __FIEventHandler_1_IInspectable *cc_handler = NULL;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!token) return E_POINTER;
    ensure_clipboard_watcher();

    if (handler)
    {
        if (FAILED( IUnknown_QueryInterface( (IUnknown *)handler, &IID___FIEventHandler_1_IInspectable, (void **)&cc_handler ) ))
        {
            cc_handler = (__FIEventHandler_1_IInspectable *)handler;
            IUnknown_AddRef( (IUnknown *)cc_handler );
        }
    }

    AcquireSRWLockExclusive( &clipboard_lock );
    if (cc_count == cc_alloc)
    {
        ULONG alloc = max( cc_alloc * 2, 8 );
        struct content_changed_entry *entries = realloc( cc_entries, alloc * sizeof(*entries) );
        if (!entries)
        {
            ReleaseSRWLockExclusive( &clipboard_lock );
            if (cc_handler) __FIEventHandler_1_IInspectable_Release( cc_handler );
            return E_OUTOFMEMORY;
        }
        cc_entries = entries;
        cc_alloc = alloc;
    }
    token->value = InterlockedIncrement( &next_cc_token );
    cc_entries[cc_count].handler = cc_handler;
    cc_entries[cc_count].token = *token;
    cc_count++;
    ReleaseSRWLockExclusive( &clipboard_lock );
    return S_OK;
}

static HRESULT WINAPI statics_remove_ContentChanged( const void *iface, EventRegistrationToken token )
{
    __FIEventHandler_1_IInspectable *removed = NULL;
    ULONG i;

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );

    if (!token.value) return S_OK;

    AcquireSRWLockExclusive( &clipboard_lock );
    for (i = 0; i < cc_count; i++)
    {
        if (cc_entries[i].token.value == token.value)
        {
            removed = cc_entries[i].handler;
            for (; i + 1 < cc_count; i++) cc_entries[i] = cc_entries[i + 1];
            cc_count--;
            break;
        }
    }
    ReleaseSRWLockExclusive( &clipboard_lock );

    if (removed) __FIEventHandler_1_IInspectable_Release( removed );
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
    HRESULT hr;

    TRACE( "iface %p, content %p, options %p, result %p.\n", iface, content, options, result );

    if (!result) return E_POINTER;
    hr = clipboard_push_package( content );
    *result = SUCCEEDED( hr );
    return hr;
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
