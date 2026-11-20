/*
 * WinRT Windows.UI.Text.Core.CoreTextEditContext implementation
 *
 * Copyright 2026 CodeWeavers
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
#include "wine/list.h"
#include "wine/debug.h"

#include "winuser.h"
#include "imm.h"

WINE_DEFAULT_DEBUG_CHANNEL(coreinputview);

/*
 * CoreTextEditContext carries the WinRT text-input state of a UWP/WinUI 3
 * text control.  It is bridged to the active Win32 window's IMM32 message
 * stream so that Linux IMEs (Fcitx5 / IBus / XIM, surfaced by Wine's
 * winex11.drv IMM32 layer) drive composition: WM_IME_STARTCOMPOSITION ->
 * CompositionStarted + candidate-window placement, WM_IME_COMPOSITION ->
 * TextUpdating (preedit / commit), WM_IME_ENDCOMPOSITION ->
 * CompositionCompleted.
 */

/* Per-thread "active IME context".  WM_IME_* messages are delivered on the
 * UI thread that called NotifyFocusEnter(), and the subclassed window
 * procedure runs on that same thread, so a thread-local pointer is enough
 * to map the message back to the owning CoreTextEditContext object. */
static __thread struct core_text_edit_context *ime_current;

struct event_entry
{
    struct list entry;
    IUnknown *handler;
    INT64 token;
};

struct core_text_edit_context
{
    ICoreTextEditContext ICoreTextEditContext;
    LONG ref;
    CRITICAL_SECTION cs;

    HSTRING name;
    CoreTextInputScope input_scope;
    CoreTextInputPaneDisplayPolicy display_policy;
    BOOL is_readonly;
    BOOL has_focus;

    CoreTextRange selection;
    CoreTextRange composition;
    HSTRING preedit;
    BOOL composing;

    HWND hwnd;
    WNDPROC old_proc;

    INT64 token_counter;
    struct list text_requested;
    struct list selection_requested;
    struct list layout_requested;
    struct list text_updating;
    struct list selection_updating;
    struct list format_updating;
    struct list composition_started;
    struct list composition_completed;
    struct list focus_removed;
};

static inline struct core_text_edit_context *impl_from_ICoreTextEditContext( ICoreTextEditContext *iface )
{
    return CONTAINING_RECORD( iface, struct core_text_edit_context, ICoreTextEditContext );
}

/* ------------------------------------------------------------------ */
/* Generic event registration helpers                                  */
/* ------------------------------------------------------------------ */

static void event_list_add( struct core_text_edit_context *impl, struct list *list,
                            IUnknown *handler, EventRegistrationToken *token )
{
    struct event_entry *entry;

    if (!(entry = malloc( sizeof(*entry) )))
    {
        token->value = 0;
        return;
    }

    EnterCriticalSection( &impl->cs );
    entry->token = ++impl->token_counter;
    IUnknown_AddRef( entry->handler = handler );
    list_add_tail( list, &entry->entry );
    LeaveCriticalSection( &impl->cs );

    token->value = entry->token;
}

static void event_list_remove( struct core_text_edit_context *impl, struct list *list,
                               EventRegistrationToken token )
{
    struct event_entry *entry, *next;

    EnterCriticalSection( &impl->cs );
    LIST_FOR_EACH_ENTRY_SAFE( entry, next, list, struct event_entry, entry )
    {
        if (entry->token == token.value)
        {
            list_remove( &entry->entry );
            IUnknown_Release( entry->handler );
            free( entry );
            break;
        }
    }
    LeaveCriticalSection( &impl->cs );
}

static void event_list_clear( struct core_text_edit_context *impl, struct list *list )
{
    struct event_entry *entry, *next;

    EnterCriticalSection( &impl->cs );
    LIST_FOR_EACH_ENTRY_SAFE( entry, next, list, struct event_entry, entry )
    {
        list_remove( &entry->entry );
        IUnknown_Release( entry->handler );
        free( entry );
    }
    LeaveCriticalSection( &impl->cs );
}

/* Invoke every registered TypedEventHandler<CoreTextEditContext, TArgs>.  All
 * of these handler instantiations share the same Invoke layout
 * (This, sender, args), so a single generic call site suffices. */
static void event_list_invoke( struct core_text_edit_context *impl, struct list *list, void *args )
{
    struct event_entry *entry, *next;
    struct list snapshot;

    list_init( &snapshot );
    EnterCriticalSection( &impl->cs );
    LIST_FOR_EACH_ENTRY( entry, list, struct event_entry, entry )
    {
        struct event_entry *copy = malloc( sizeof(*copy) );
        if (!copy) continue;
        copy->handler = entry->handler;
        IUnknown_AddRef( copy->handler );
        list_add_tail( &snapshot, &copy->entry );
    }
    LeaveCriticalSection( &impl->cs );

    LIST_FOR_EACH_ENTRY( entry, &snapshot, struct event_entry, entry )
    {
        ITypedEventHandler_CoreTextEditContext_IInspectable *handler =
            (ITypedEventHandler_CoreTextEditContext_IInspectable *)entry->handler;
        handler->lpVtbl->Invoke( handler, (ICoreTextEditContext *)&impl->ICoreTextEditContext, args );
    }

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &snapshot, struct event_entry, entry )
    {
        IUnknown_Release( entry->handler );
        free( entry );
    }
}

/* Small helper: duplicate an HSTRING, returning a fresh reference. */
static HSTRING hstring_dup( HSTRING src )
{
    HSTRING dup = NULL;
    if (src) WindowsDuplicateString( src, &dup );
    return dup;
}

/* ------------------------------------------------------------------ */
/* ICoreTextEditContext                                                */
/* ------------------------------------------------------------------ */

static HRESULT WINAPI text_edit_context_QueryInterface( ICoreTextEditContext *iface, REFIID iid, void **out )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_ICoreTextEditContext ))
    {
        IInspectable_AddRef( (*out = &impl->ICoreTextEditContext) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI text_edit_context_AddRef( ICoreTextEditContext *iface )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI text_edit_context_Release( ICoreTextEditContext *iface )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        struct core_text_edit_context *saved = ime_current;
        event_list_clear( impl, &impl->text_requested );
        event_list_clear( impl, &impl->selection_requested );
        event_list_clear( impl, &impl->layout_requested );
        event_list_clear( impl, &impl->text_updating );
        event_list_clear( impl, &impl->selection_updating );
        event_list_clear( impl, &impl->format_updating );
        event_list_clear( impl, &impl->composition_started );
        event_list_clear( impl, &impl->composition_completed );
        event_list_clear( impl, &impl->focus_removed );
        WindowsDeleteString( impl->name );
        WindowsDeleteString( impl->preedit );
        DeleteCriticalSection( &impl->cs );
        if (saved == impl) ime_current = NULL;
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI text_edit_context_GetIids( ICoreTextEditContext *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI text_edit_context_GetRuntimeClassName( ICoreTextEditContext *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Text_Core_CoreTextEditContext,
                                wcslen( RuntimeClass_Windows_UI_Text_Core_CoreTextEditContext ), class_name );
}

static HRESULT WINAPI text_edit_context_GetTrustLevel( ICoreTextEditContext *iface, TrustLevel *trust_level )
{
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI text_edit_context_get_Name( ICoreTextEditContext *iface, HSTRING *value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    EnterCriticalSection( &impl->cs );
    *value = hstring_dup( impl->name );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_put_Name( ICoreTextEditContext *iface, HSTRING value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    HSTRING dup = hstring_dup( value );
    EnterCriticalSection( &impl->cs );
    WindowsDeleteString( impl->name );
    impl->name = dup;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_get_InputScope( ICoreTextEditContext *iface, CoreTextInputScope *value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    EnterCriticalSection( &impl->cs );
    *value = impl->input_scope;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_put_InputScope( ICoreTextEditContext *iface, CoreTextInputScope value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    EnterCriticalSection( &impl->cs );
    impl->input_scope = value;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_get_IsReadOnly( ICoreTextEditContext *iface, boolean *value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    EnterCriticalSection( &impl->cs );
    *value = impl->is_readonly;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_put_IsReadOnly( ICoreTextEditContext *iface, boolean value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    EnterCriticalSection( &impl->cs );
    impl->is_readonly = value;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_get_InputPaneDisplayPolicy( ICoreTextEditContext *iface,
                                                                    CoreTextInputPaneDisplayPolicy *value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    EnterCriticalSection( &impl->cs );
    *value = impl->display_policy;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_put_InputPaneDisplayPolicy( ICoreTextEditContext *iface,
                                                                    CoreTextInputPaneDisplayPolicy value )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    EnterCriticalSection( &impl->cs );
    impl->display_policy = value;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

#define DEFINE_TEXT_EDIT_CONTEXT_EVENT( name, handler_type, evt ) \
    static HRESULT WINAPI text_edit_context_add_##name( ICoreTextEditContext *iface, \
            handler_type *handler, EventRegistrationToken *token ) \
    { \
        struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface ); \
        event_list_add( impl, &impl->evt, (IUnknown *)handler, token ); \
        return S_OK; \
    } \
    static HRESULT WINAPI text_edit_context_remove_##name( ICoreTextEditContext *iface, \
            EventRegistrationToken token ) \
    { \
        struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface ); \
        event_list_remove( impl, &impl->evt, token ); \
        return S_OK; \
    }

DEFINE_TEXT_EDIT_CONTEXT_EVENT( TextRequested, ITypedEventHandler_CoreTextEditContext_CoreTextTextRequestedEventArgs, text_requested )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( SelectionRequested, ITypedEventHandler_CoreTextEditContext_CoreTextSelectionRequestedEventArgs, selection_requested )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( LayoutRequested, ITypedEventHandler_CoreTextEditContext_CoreTextLayoutRequestedEventArgs, layout_requested )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( TextUpdating, ITypedEventHandler_CoreTextEditContext_CoreTextTextUpdatingEventArgs, text_updating )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( SelectionUpdating, ITypedEventHandler_CoreTextEditContext_CoreTextSelectionUpdatingEventArgs, selection_updating )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( FormatUpdating, ITypedEventHandler_CoreTextEditContext_CoreTextFormatUpdatingEventArgs, format_updating )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( CompositionStarted, ITypedEventHandler_CoreTextEditContext_CoreTextCompositionStartedEventArgs, composition_started )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( CompositionCompleted, ITypedEventHandler_CoreTextEditContext_CoreTextCompositionCompletedEventArgs, composition_completed )
DEFINE_TEXT_EDIT_CONTEXT_EVENT( FocusRemoved, ITypedEventHandler_CoreTextEditContext_IInspectable, focus_removed )

/* Forward declaration of the layout/caret helpers (defined after the args
 * objects below). */
static Rect query_layout_text_bounds( struct core_text_edit_context *impl );
static void update_ime_composition_window( struct core_text_edit_context *impl );
static LRESULT WINAPI ime_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam );

static HRESULT WINAPI text_edit_context_NotifyFocusEnter( ICoreTextEditContext *iface )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );

    EnterCriticalSection( &impl->cs );
    impl->has_focus = TRUE;
    if (!impl->hwnd)
    {
        HWND hwnd = GetFocus();
        if (!hwnd) hwnd = GetActiveWindow();
        if (hwnd && hwnd != impl->hwnd)
        {
            impl->old_proc = (WNDPROC)SetWindowLongPtrW( hwnd, GWLP_WNDPROC, (LONG_PTR)ime_wndproc );
            impl->hwnd = hwnd;
        }
    }
    if (impl->hwnd) ime_current = impl;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_NotifyFocusLeave( ICoreTextEditContext *iface )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );
    HWND hwnd;
    WNDPROC old;

    EnterCriticalSection( &impl->cs );
    hwnd = impl->hwnd;
    old = impl->old_proc;
    if (hwnd)
    {
        if (impl->composing)
        {
            HIMC himc = ImmGetContext( hwnd );
            if (himc)
            {
                ImmNotifyIME( himc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0 );
                ImmReleaseContext( hwnd, himc );
            }
        }
        SetWindowLongPtrW( hwnd, GWLP_WNDPROC, (LONG_PTR)old );
        impl->hwnd = NULL;
        impl->old_proc = NULL;
    }
    impl->has_focus = FALSE;
    if (ime_current == impl) ime_current = NULL;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_NotifyTextChanged( ICoreTextEditContext *iface,
                                                           CoreTextRange modified_range, INT32 new_length,
                                                           CoreTextRange new_selection )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );

    EnterCriticalSection( &impl->cs );
    impl->selection = new_selection;
    /* The text at [modified_range] was replaced; the composition that ended
     * there is committed and no longer active. */
    if (impl->composing)
    {
        impl->composing = FALSE;
        WindowsDeleteString( impl->preedit );
        impl->preedit = NULL;
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_NotifySelectionChanged( ICoreTextEditContext *iface,
                                                                CoreTextRange selection )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );

    EnterCriticalSection( &impl->cs );
    impl->selection = selection;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI text_edit_context_NotifyLayoutChanged( ICoreTextEditContext *iface )
{
    struct core_text_edit_context *impl = impl_from_ICoreTextEditContext( iface );

    EnterCriticalSection( &impl->cs );
    if (impl->hwnd && impl->composing)
    {
        LeaveCriticalSection( &impl->cs );
        update_ime_composition_window( impl );
        return S_OK;
    }
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const struct ICoreTextEditContextVtbl core_text_edit_context_vtbl =
{
    text_edit_context_QueryInterface,
    text_edit_context_AddRef,
    text_edit_context_Release,
    /* IInspectable methods */
    text_edit_context_GetIids,
    text_edit_context_GetRuntimeClassName,
    text_edit_context_GetTrustLevel,
    /* ICoreTextEditContext methods */
    text_edit_context_get_Name,
    text_edit_context_put_Name,
    text_edit_context_get_InputScope,
    text_edit_context_put_InputScope,
    text_edit_context_get_IsReadOnly,
    text_edit_context_put_IsReadOnly,
    text_edit_context_get_InputPaneDisplayPolicy,
    text_edit_context_put_InputPaneDisplayPolicy,
    text_edit_context_add_TextRequested,
    text_edit_context_remove_TextRequested,
    text_edit_context_add_SelectionRequested,
    text_edit_context_remove_SelectionRequested,
    text_edit_context_add_LayoutRequested,
    text_edit_context_remove_LayoutRequested,
    text_edit_context_add_TextUpdating,
    text_edit_context_remove_TextUpdating,
    text_edit_context_add_SelectionUpdating,
    text_edit_context_remove_SelectionUpdating,
    text_edit_context_add_FormatUpdating,
    text_edit_context_remove_FormatUpdating,
    text_edit_context_add_CompositionStarted,
    text_edit_context_remove_CompositionStarted,
    text_edit_context_add_CompositionCompleted,
    text_edit_context_remove_CompositionCompleted,
    text_edit_context_add_FocusRemoved,
    text_edit_context_remove_FocusRemoved,
    text_edit_context_NotifyFocusEnter,
    text_edit_context_NotifyFocusLeave,
    text_edit_context_NotifyTextChanged,
    text_edit_context_NotifySelectionChanged,
    text_edit_context_NotifyLayoutChanged,
};

/* ------------------------------------------------------------------ */
/* Event-args / request / bounds / segment objects                     */
/* ------------------------------------------------------------------ */

/* Boilerplate reducer: each auxiliary object has a refcount and a single
 * primary interface with a strict QueryInterface allowlist. */
#define BEGIN_AUX( type, iface_field, iid_primary ) \
    static inline type *aux_from_##iface_field( iface_field *iface ) \
    { return CONTAINING_RECORD( iface, type, iface_field ); } \
    static HRESULT WINAPI iface_field##_QI( iface_field *iface, REFIID iid, void **out ) \
    { \
        type *impl = aux_from( iface ); \
        if (IsEqualGUID( iid, &IID_IUnknown ) || \
            IsEqualGUID( iid, &IID_IInspectable ) || \
            IsEqualGUID( iid, &IID_IAgileObject ) || \
            IsEqualGUID( iid, &iid_primary )) \
        { \
            IInspectable_AddRef( (*out = &impl->iface_field) ); \
            return S_OK; \
        } \
        *out = NULL; \
        return E_NOINTERFACE; \
    } \
    static ULONG WINAPI iface_field##_AR( iface_field *iface ) \
    { type *impl = aux_from( iface ); return InterlockedIncrement( &impl->ref ); } \
    static ULONG WINAPI iface_field##_Rel( iface_field *iface ) \
    { type *impl = aux_from( iface ); return InterlockedDecrement( &impl->ref ); } \
    static HRESULT WINAPI iface_field##_GIids( iface_field *iface, ULONG *n, IID **iids ) \
    { return E_NOTIMPL; } \
    static HRESULT WINAPI iface_field##_GRCN( iface_field *iface, HSTRING *name ) \
    { return E_NOTIMPL; } \
    static HRESULT WINAPI iface_field##_GTL( iface_field *iface, TrustLevel *level ) \
    { *level = FullTrust; return S_OK; }

/* ---- CompositionStarted args ---- */
struct composition_started_args
{
    ICoreTextCompositionStartedEventArgs ICoreTextCompositionStartedEventArgs;
    LONG ref;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextCompositionStartedEventArgs(iface)
BEGIN_AUX( struct composition_started_args, ICoreTextCompositionStartedEventArgs, IID_ICoreTextCompositionStartedEventArgs )

static HRESULT WINAPI started_get_IsCanceled( ICoreTextCompositionStartedEventArgs *iface, boolean *value )
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI started_GetDeferral( ICoreTextCompositionStartedEventArgs *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextCompositionStartedEventArgsVtbl composition_started_args_vtbl =
{
    ICoreTextCompositionStartedEventArgs_QI,
    ICoreTextCompositionStartedEventArgs_AR,
    ICoreTextCompositionStartedEventArgs_Rel,
    ICoreTextCompositionStartedEventArgs_GIids,
    ICoreTextCompositionStartedEventArgs_GRCN,
    ICoreTextCompositionStartedEventArgs_GTL,
    started_get_IsCanceled,
    started_GetDeferral,
};


static HRESULT composition_started_args_create( ICoreTextCompositionStartedEventArgs **out )
{
    struct composition_started_args *args = calloc( 1, sizeof(*args) );
    if (!args) return E_OUTOFMEMORY;
    args->ICoreTextCompositionStartedEventArgs.lpVtbl = &composition_started_args_vtbl;
    args->ref = 1;
    *out = &args->ICoreTextCompositionStartedEventArgs;
    return S_OK;
}

/* ---- CompositionCompleted args ---- */
struct composition_completed_args
{
    ICoreTextCompositionCompletedEventArgs ICoreTextCompositionCompletedEventArgs;
    LONG ref;
    BOOL canceled;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextCompositionCompletedEventArgs(iface)
BEGIN_AUX( struct composition_completed_args, ICoreTextCompositionCompletedEventArgs, IID_ICoreTextCompositionCompletedEventArgs )

static HRESULT WINAPI completed_get_IsCanceled( ICoreTextCompositionCompletedEventArgs *iface, boolean *value )
{
    struct composition_completed_args *impl = aux_from( iface );
    *value = impl->canceled;
    return S_OK;
}
static HRESULT WINAPI completed_get_CompositionSegments( ICoreTextCompositionCompletedEventArgs *iface,
                                                         IVectorView_CoreTextCompositionSegment **value )
{
    static const struct vector_iids iids =
    {
        .vector = &IID_IVectorView_CoreTextCompositionSegment,
        .view = &IID_IVectorView_CoreTextCompositionSegment,
        .iterable = &IID_IVectorView_CoreTextCompositionSegment,
        .iterator = &IID_IVectorView_CoreTextCompositionSegment,
    };
    IVector_IInspectable *vector;
    IVectorView_IInspectable *view;
    HRESULT hr;

    *value = NULL;
    if (FAILED( hr = vector_create( &iids, (void **)&vector ) )) return hr;

    hr = IVector_IInspectable_GetView( vector, &view );
    IVector_IInspectable_Release( vector );
    if (FAILED( hr )) return hr;

    hr = IVectorView_IInspectable_QueryInterface( view, &IID_IVectorView_CoreTextCompositionSegment, (void **)value );
    IVectorView_IInspectable_Release( view );
    return hr;
}
static HRESULT WINAPI completed_GetDeferral( ICoreTextCompositionCompletedEventArgs *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextCompositionCompletedEventArgsVtbl composition_completed_args_vtbl =
{
    ICoreTextCompositionCompletedEventArgs_QI,
    ICoreTextCompositionCompletedEventArgs_AR,
    ICoreTextCompositionCompletedEventArgs_Rel,
    ICoreTextCompositionCompletedEventArgs_GIids,
    ICoreTextCompositionCompletedEventArgs_GRCN,
    ICoreTextCompositionCompletedEventArgs_GTL,
    completed_get_IsCanceled,
    completed_get_CompositionSegments,
    completed_GetDeferral,
};


static HRESULT composition_completed_args_create( BOOL canceled, ICoreTextCompositionCompletedEventArgs **out )
{
    struct composition_completed_args *args = calloc( 1, sizeof(*args) );
    if (!args) return E_OUTOFMEMORY;
    args->ICoreTextCompositionCompletedEventArgs.lpVtbl = &composition_completed_args_vtbl;
    args->ref = 1;
    args->canceled = canceled;
    *out = &args->ICoreTextCompositionCompletedEventArgs;
    return S_OK;
}

/* ---- TextUpdating args ---- */
struct text_updating_args
{
    ICoreTextTextUpdatingEventArgs ICoreTextTextUpdatingEventArgs;
    LONG ref;
    CoreTextRange range;
    HSTRING text;
    CoreTextRange new_selection;
    CoreTextTextUpdatingResult result;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextTextUpdatingEventArgs(iface)
BEGIN_AUX( struct text_updating_args, ICoreTextTextUpdatingEventArgs, IID_ICoreTextTextUpdatingEventArgs )

static HRESULT WINAPI text_updating_get_Range( ICoreTextTextUpdatingEventArgs *iface, CoreTextRange *value )
{ struct text_updating_args *impl = aux_from( iface ); *value = impl->range; return S_OK; }
static HRESULT WINAPI text_updating_get_Text( ICoreTextTextUpdatingEventArgs *iface, HSTRING *value )
{ struct text_updating_args *impl = aux_from( iface ); *value = hstring_dup( impl->text ); return S_OK; }
static HRESULT WINAPI text_updating_get_NewSelection( ICoreTextTextUpdatingEventArgs *iface, CoreTextRange *value )
{ struct text_updating_args *impl = aux_from( iface ); *value = impl->new_selection; return S_OK; }
static HRESULT WINAPI text_updating_get_InputLanguage( ICoreTextTextUpdatingEventArgs *iface, ILanguage **value )
{ *value = NULL; return S_OK; }
static HRESULT WINAPI text_updating_get_Result( ICoreTextTextUpdatingEventArgs *iface, CoreTextTextUpdatingResult *value )
{ struct text_updating_args *impl = aux_from( iface ); *value = impl->result; return S_OK; }
static HRESULT WINAPI text_updating_put_Result( ICoreTextTextUpdatingEventArgs *iface, CoreTextTextUpdatingResult value )
{ struct text_updating_args *impl = aux_from( iface ); impl->result = value; return S_OK; }
static HRESULT WINAPI text_updating_get_IsCanceled( ICoreTextTextUpdatingEventArgs *iface, boolean *value )
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI text_updating_GetDeferral( ICoreTextTextUpdatingEventArgs *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextTextUpdatingEventArgsVtbl text_updating_args_vtbl =
{
    ICoreTextTextUpdatingEventArgs_QI,
    ICoreTextTextUpdatingEventArgs_AR,
    ICoreTextTextUpdatingEventArgs_Rel,
    ICoreTextTextUpdatingEventArgs_GIids,
    ICoreTextTextUpdatingEventArgs_GRCN,
    ICoreTextTextUpdatingEventArgs_GTL,
    text_updating_get_Range,
    text_updating_get_Text,
    text_updating_get_NewSelection,
    text_updating_get_InputLanguage,
    text_updating_get_Result,
    text_updating_put_Result,
    text_updating_get_IsCanceled,
    text_updating_GetDeferral,
};


static HRESULT text_updating_args_create( CoreTextRange range, HSTRING text, CoreTextRange new_selection,
                                          ICoreTextTextUpdatingEventArgs **out )
{
    struct text_updating_args *args = calloc( 1, sizeof(*args) );
    if (!args) return E_OUTOFMEMORY;
    args->ICoreTextTextUpdatingEventArgs.lpVtbl = &text_updating_args_vtbl;
    args->ref = 1;
    args->range = range;
    args->text = hstring_dup( text );
    args->new_selection = new_selection;
    args->result = CoreTextTextUpdatingResult_Succeeded;
    *out = &args->ICoreTextTextUpdatingEventArgs;
    return S_OK;
}

/* ---- SelectionUpdating args ---- */
struct selection_updating_args
{
    ICoreTextSelectionUpdatingEventArgs ICoreTextSelectionUpdatingEventArgs;
    LONG ref;
    CoreTextRange selection;
    CoreTextSelectionUpdatingResult result;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextSelectionUpdatingEventArgs(iface)
BEGIN_AUX( struct selection_updating_args, ICoreTextSelectionUpdatingEventArgs, IID_ICoreTextSelectionUpdatingEventArgs )

static HRESULT WINAPI sel_updating_get_Selection( ICoreTextSelectionUpdatingEventArgs *iface, CoreTextRange *value )
{ struct selection_updating_args *impl = aux_from( iface ); *value = impl->selection; return S_OK; }
static HRESULT WINAPI sel_updating_get_Result( ICoreTextSelectionUpdatingEventArgs *iface, CoreTextSelectionUpdatingResult *value )
{ struct selection_updating_args *impl = aux_from( iface ); *value = impl->result; return S_OK; }
static HRESULT WINAPI sel_updating_put_Result( ICoreTextSelectionUpdatingEventArgs *iface, CoreTextSelectionUpdatingResult value )
{ struct selection_updating_args *impl = aux_from( iface ); impl->result = value; return S_OK; }
static HRESULT WINAPI sel_updating_get_IsCanceled( ICoreTextSelectionUpdatingEventArgs *iface, boolean *value )
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI sel_updating_GetDeferral( ICoreTextSelectionUpdatingEventArgs *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextSelectionUpdatingEventArgsVtbl selection_updating_args_vtbl =
{
    ICoreTextSelectionUpdatingEventArgs_QI,
    ICoreTextSelectionUpdatingEventArgs_AR,
    ICoreTextSelectionUpdatingEventArgs_Rel,
    ICoreTextSelectionUpdatingEventArgs_GIids,
    ICoreTextSelectionUpdatingEventArgs_GRCN,
    ICoreTextSelectionUpdatingEventArgs_GTL,
    sel_updating_get_Selection,
    sel_updating_get_Result,
    sel_updating_put_Result,
    sel_updating_get_IsCanceled,
    sel_updating_GetDeferral,
};


static HRESULT selection_updating_args_create( CoreTextRange selection, ICoreTextSelectionUpdatingEventArgs **out )
{
    struct selection_updating_args *args = calloc( 1, sizeof(*args) );
    if (!args) return E_OUTOFMEMORY;
    args->ICoreTextSelectionUpdatingEventArgs.lpVtbl = &selection_updating_args_vtbl;
    args->ref = 1;
    args->selection = selection;
    args->result = CoreTextSelectionUpdatingResult_Succeeded;
    *out = &args->ICoreTextSelectionUpdatingEventArgs;
    return S_OK;
}

/* ---- FormatUpdating args ---- */
struct format_updating_args
{
    ICoreTextFormatUpdatingEventArgs ICoreTextFormatUpdatingEventArgs;
    LONG ref;
    CoreTextRange range;
    CoreTextFormatUpdatingReason reason;
    CoreTextFormatUpdatingResult result;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextFormatUpdatingEventArgs(iface)
BEGIN_AUX( struct format_updating_args, ICoreTextFormatUpdatingEventArgs, IID_ICoreTextFormatUpdatingEventArgs )

static HRESULT WINAPI format_updating_get_Range( ICoreTextFormatUpdatingEventArgs *iface, CoreTextRange *value )
{ struct format_updating_args *impl = aux_from( iface ); *value = impl->range; return S_OK; }
static HRESULT WINAPI format_updating_get_TextColor( ICoreTextFormatUpdatingEventArgs *iface, IReference_UIElementType **value )
{ *value = NULL; return S_OK; }
static HRESULT WINAPI format_updating_get_BackgroundColor( ICoreTextFormatUpdatingEventArgs *iface, IReference_UIElementType **value )
{ *value = NULL; return S_OK; }
static HRESULT WINAPI format_updating_get_UnderlineColor( ICoreTextFormatUpdatingEventArgs *iface, IReference_UIElementType **value )
{ *value = NULL; return S_OK; }
static HRESULT WINAPI format_updating_get_UnderlineType( ICoreTextFormatUpdatingEventArgs *iface, IReference_UnderlineType **value )
{ *value = NULL; return S_OK; }
static HRESULT WINAPI format_updating_get_Reason( ICoreTextFormatUpdatingEventArgs *iface, CoreTextFormatUpdatingReason *value )
{ struct format_updating_args *impl = aux_from( iface ); *value = impl->reason; return S_OK; }
static HRESULT WINAPI format_updating_get_Result( ICoreTextFormatUpdatingEventArgs *iface, CoreTextFormatUpdatingResult *value )
{ struct format_updating_args *impl = aux_from( iface ); *value = impl->result; return S_OK; }
static HRESULT WINAPI format_updating_put_Result( ICoreTextFormatUpdatingEventArgs *iface, CoreTextFormatUpdatingResult value )
{ struct format_updating_args *impl = aux_from( iface ); impl->result = value; return S_OK; }
static HRESULT WINAPI format_updating_get_IsCanceled( ICoreTextFormatUpdatingEventArgs *iface, boolean *value )
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI format_updating_GetDeferral( ICoreTextFormatUpdatingEventArgs *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextFormatUpdatingEventArgsVtbl format_updating_args_vtbl =
{
    ICoreTextFormatUpdatingEventArgs_QI,
    ICoreTextFormatUpdatingEventArgs_AR,
    ICoreTextFormatUpdatingEventArgs_Rel,
    ICoreTextFormatUpdatingEventArgs_GIids,
    ICoreTextFormatUpdatingEventArgs_GRCN,
    ICoreTextFormatUpdatingEventArgs_GTL,
    format_updating_get_Range,
    format_updating_get_TextColor,
    format_updating_get_BackgroundColor,
    format_updating_get_UnderlineColor,
    format_updating_get_UnderlineType,
    format_updating_get_Reason,
    format_updating_get_Result,
    format_updating_put_Result,
    format_updating_get_IsCanceled,
    format_updating_GetDeferral,
};


/* ---- LayoutBounds ---- */
struct layout_bounds
{
    ICoreTextLayoutBounds ICoreTextLayoutBounds;
    LONG ref;
    Rect text_bounds;
    Rect control_bounds;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextLayoutBounds(iface)
BEGIN_AUX( struct layout_bounds, ICoreTextLayoutBounds, IID_ICoreTextLayoutBounds )

static HRESULT WINAPI layout_bounds_get_TextBounds( ICoreTextLayoutBounds *iface, Rect *value )
{ struct layout_bounds *impl = aux_from( iface ); *value = impl->text_bounds; return S_OK; }
static HRESULT WINAPI layout_bounds_put_TextBounds( ICoreTextLayoutBounds *iface, Rect value )
{ struct layout_bounds *impl = aux_from( iface ); impl->text_bounds = value; return S_OK; }
static HRESULT WINAPI layout_bounds_get_ControlBounds( ICoreTextLayoutBounds *iface, Rect *value )
{ struct layout_bounds *impl = aux_from( iface ); *value = impl->control_bounds; return S_OK; }
static HRESULT WINAPI layout_bounds_put_ControlBounds( ICoreTextLayoutBounds *iface, Rect value )
{ struct layout_bounds *impl = aux_from( iface ); impl->control_bounds = value; return S_OK; }

static const struct ICoreTextLayoutBoundsVtbl layout_bounds_vtbl =
{
    ICoreTextLayoutBounds_QI,
    ICoreTextLayoutBounds_AR,
    ICoreTextLayoutBounds_Rel,
    ICoreTextLayoutBounds_GIids,
    ICoreTextLayoutBounds_GRCN,
    ICoreTextLayoutBounds_GTL,
    layout_bounds_get_TextBounds,
    layout_bounds_put_TextBounds,
    layout_bounds_get_ControlBounds,
    layout_bounds_put_ControlBounds,
};


static HRESULT layout_bounds_create( ICoreTextLayoutBounds **out )
{
    struct layout_bounds *bounds = calloc( 1, sizeof(*bounds) );
    if (!bounds) return E_OUTOFMEMORY;
    bounds->ICoreTextLayoutBounds.lpVtbl = &layout_bounds_vtbl;
    bounds->ref = 1;
    *out = &bounds->ICoreTextLayoutBounds;
    return S_OK;
}

/* ---- LayoutRequest ---- */
struct layout_request
{
    ICoreTextLayoutRequest ICoreTextLayoutRequest;
    LONG ref;
    CoreTextRange range;
    ICoreTextLayoutBounds *bounds;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextLayoutRequest(iface)
BEGIN_AUX( struct layout_request, ICoreTextLayoutRequest, IID_ICoreTextLayoutRequest )

static HRESULT WINAPI layout_request_get_Range( ICoreTextLayoutRequest *iface, CoreTextRange *value )
{ struct layout_request *impl = aux_from( iface ); *value = impl->range; return S_OK; }
static HRESULT WINAPI layout_request_get_LayoutBounds( ICoreTextLayoutRequest *iface, ICoreTextLayoutBounds **value )
{ struct layout_request *impl = aux_from( iface ); IInspectable_AddRef( (IInspectable *)(*value = impl->bounds) ); return S_OK; }
static HRESULT WINAPI layout_request_get_IsCanceled( ICoreTextLayoutRequest *iface, boolean *value )
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI layout_request_GetDeferral( ICoreTextLayoutRequest *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextLayoutRequestVtbl layout_request_vtbl =
{
    ICoreTextLayoutRequest_QI,
    ICoreTextLayoutRequest_AR,
    ICoreTextLayoutRequest_Rel,
    ICoreTextLayoutRequest_GIids,
    ICoreTextLayoutRequest_GRCN,
    ICoreTextLayoutRequest_GTL,
    layout_request_get_Range,
    layout_request_get_LayoutBounds,
    layout_request_get_IsCanceled,
    layout_request_GetDeferral,
};


/* ---- LayoutRequested args ---- */
struct layout_requested_args
{
    ICoreTextLayoutRequestedEventArgs ICoreTextLayoutRequestedEventArgs;
    LONG ref;
    struct layout_request *request;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextLayoutRequestedEventArgs(iface)
BEGIN_AUX( struct layout_requested_args, ICoreTextLayoutRequestedEventArgs, IID_ICoreTextLayoutRequestedEventArgs )

static HRESULT WINAPI layout_requested_get_Request( ICoreTextLayoutRequestedEventArgs *iface, ICoreTextLayoutRequest **value )
{ struct layout_requested_args *impl = aux_from( iface ); IInspectable_AddRef( (IInspectable *)(*value = &impl->request->ICoreTextLayoutRequest) ); return S_OK; }

static const struct ICoreTextLayoutRequestedEventArgsVtbl layout_requested_args_vtbl =
{
    ICoreTextLayoutRequestedEventArgs_QI,
    ICoreTextLayoutRequestedEventArgs_AR,
    ICoreTextLayoutRequestedEventArgs_Rel,
    ICoreTextLayoutRequestedEventArgs_GIids,
    ICoreTextLayoutRequestedEventArgs_GRCN,
    ICoreTextLayoutRequestedEventArgs_GTL,
    layout_requested_get_Request,
};


/* ---- TextRequest ---- */
struct text_request
{
    ICoreTextTextRequest ICoreTextTextRequest;
    LONG ref;
    CoreTextRange range;
    HSTRING text;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextTextRequest(iface)
BEGIN_AUX( struct text_request, ICoreTextTextRequest, IID_ICoreTextTextRequest )

static HRESULT WINAPI text_request_get_Range( ICoreTextTextRequest *iface, CoreTextRange *value )
{ struct text_request *impl = aux_from( iface ); *value = impl->range; return S_OK; }
static HRESULT WINAPI text_request_get_Text( ICoreTextTextRequest *iface, HSTRING *value )
{ struct text_request *impl = aux_from( iface ); *value = hstring_dup( impl->text ); return S_OK; }
static HRESULT WINAPI text_request_put_Text( ICoreTextTextRequest *iface, HSTRING value )
{
    struct text_request *impl = aux_from( iface );
    HSTRING dup = hstring_dup( value );
    WindowsDeleteString( impl->text );
    impl->text = dup;
    return S_OK;
}
static HRESULT WINAPI text_request_get_IsCanceled( ICoreTextTextRequest *iface, boolean *value )
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI text_request_GetDeferral( ICoreTextTextRequest *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextTextRequestVtbl text_request_vtbl =
{
    ICoreTextTextRequest_QI,
    ICoreTextTextRequest_AR,
    ICoreTextTextRequest_Rel,
    ICoreTextTextRequest_GIids,
    ICoreTextTextRequest_GRCN,
    ICoreTextTextRequest_GTL,
    text_request_get_Range,
    text_request_get_Text,
    text_request_put_Text,
    text_request_get_IsCanceled,
    text_request_GetDeferral,
};


/* ---- TextRequested args ---- */
struct text_requested_args
{
    ICoreTextTextRequestedEventArgs ICoreTextTextRequestedEventArgs;
    LONG ref;
    struct text_request *request;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextTextRequestedEventArgs(iface)
BEGIN_AUX( struct text_requested_args, ICoreTextTextRequestedEventArgs, IID_ICoreTextTextRequestedEventArgs )

static HRESULT WINAPI text_requested_get_Request( ICoreTextTextRequestedEventArgs *iface, ICoreTextTextRequest **value )
{ struct text_requested_args *impl = aux_from( iface ); IInspectable_AddRef( (IInspectable *)(*value = &impl->request->ICoreTextTextRequest) ); return S_OK; }

static const struct ICoreTextTextRequestedEventArgsVtbl text_requested_args_vtbl =
{
    ICoreTextTextRequestedEventArgs_QI,
    ICoreTextTextRequestedEventArgs_AR,
    ICoreTextTextRequestedEventArgs_Rel,
    ICoreTextTextRequestedEventArgs_GIids,
    ICoreTextTextRequestedEventArgs_GRCN,
    ICoreTextTextRequestedEventArgs_GTL,
    text_requested_get_Request,
};


/* ---- SelectionRequest ---- */
struct selection_request
{
    ICoreTextSelectionRequest ICoreTextSelectionRequest;
    LONG ref;
    CoreTextRange selection;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextSelectionRequest(iface)
BEGIN_AUX( struct selection_request, ICoreTextSelectionRequest, IID_ICoreTextSelectionRequest )

static HRESULT WINAPI selection_request_get_Selection( ICoreTextSelectionRequest *iface, CoreTextRange *value )
{ struct selection_request *impl = aux_from( iface ); *value = impl->selection; return S_OK; }
static HRESULT WINAPI selection_request_put_Selection( ICoreTextSelectionRequest *iface, CoreTextRange value )
{ struct selection_request *impl = aux_from( iface ); impl->selection = value; return S_OK; }
static HRESULT WINAPI selection_request_get_IsCanceled( ICoreTextSelectionRequest *iface, boolean *value )
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI selection_request_GetDeferral( ICoreTextSelectionRequest *iface, IDeferral **value )
{ *value = NULL; return S_OK; }

static const struct ICoreTextSelectionRequestVtbl selection_request_vtbl =
{
    ICoreTextSelectionRequest_QI,
    ICoreTextSelectionRequest_AR,
    ICoreTextSelectionRequest_Rel,
    ICoreTextSelectionRequest_GIids,
    ICoreTextSelectionRequest_GRCN,
    ICoreTextSelectionRequest_GTL,
    selection_request_get_Selection,
    selection_request_put_Selection,
    selection_request_get_IsCanceled,
    selection_request_GetDeferral,
};


/* ---- SelectionRequested args ---- */
struct selection_requested_args
{
    ICoreTextSelectionRequestedEventArgs ICoreTextSelectionRequestedEventArgs;
    LONG ref;
    struct selection_request *request;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextSelectionRequestedEventArgs(iface)
BEGIN_AUX( struct selection_requested_args, ICoreTextSelectionRequestedEventArgs, IID_ICoreTextSelectionRequestedEventArgs )

static HRESULT WINAPI selection_requested_get_Request( ICoreTextSelectionRequestedEventArgs *iface, ICoreTextSelectionRequest **value )
{ struct selection_requested_args *impl = aux_from( iface ); IInspectable_AddRef( (IInspectable *)(*value = &impl->request->ICoreTextSelectionRequest) ); return S_OK; }

static const struct ICoreTextSelectionRequestedEventArgsVtbl selection_requested_args_vtbl =
{
    ICoreTextSelectionRequestedEventArgs_QI,
    ICoreTextSelectionRequestedEventArgs_AR,
    ICoreTextSelectionRequestedEventArgs_Rel,
    ICoreTextSelectionRequestedEventArgs_GIids,
    ICoreTextSelectionRequestedEventArgs_GRCN,
    ICoreTextSelectionRequestedEventArgs_GTL,
    selection_requested_get_Request,
};


/* ---- CompositionSegment ---- */
struct composition_segment
{
    ICoreTextCompositionSegment ICoreTextCompositionSegment;
    LONG ref;
    HSTRING preconversion;
    CoreTextRange range;
};
#undef aux_from
#define aux_from(iface) aux_from_ICoreTextCompositionSegment(iface)
BEGIN_AUX( struct composition_segment, ICoreTextCompositionSegment, IID_ICoreTextCompositionSegment )

static HRESULT WINAPI segment_get_PreconversionString( ICoreTextCompositionSegment *iface, HSTRING *value )
{ struct composition_segment *impl = aux_from( iface ); *value = hstring_dup( impl->preconversion ); return S_OK; }
static HRESULT WINAPI segment_get_Range( ICoreTextCompositionSegment *iface, CoreTextRange *value )
{ struct composition_segment *impl = aux_from( iface ); *value = impl->range; return S_OK; }

static const struct ICoreTextCompositionSegmentVtbl composition_segment_vtbl =
{
    ICoreTextCompositionSegment_QI,
    ICoreTextCompositionSegment_AR,
    ICoreTextCompositionSegment_Rel,
    ICoreTextCompositionSegment_GIids,
    ICoreTextCompositionSegment_GRCN,
    ICoreTextCompositionSegment_GTL,
    segment_get_PreconversionString,
    segment_get_Range,
};


/* ------------------------------------------------------------------ */
/* IMM32 bridging                                                      */
/* ------------------------------------------------------------------ */

/* Fire LayoutRequested synchronously and read back the caret TextBounds the
 * registered handlers fill into the request's LayoutBounds object. */
static Rect query_layout_text_bounds( struct core_text_edit_context *impl )
{
    struct layout_requested_args *args;
    struct layout_request *request;
    ICoreTextLayoutBounds *bounds;
    Rect result = {0};

    args = calloc( 1, sizeof(*args) );
    request = calloc( 1, sizeof(*request) );
    if (!args || !request)
    {
        free( args );
        free( request );
        return result;
    }

    if (FAILED( layout_bounds_create( &bounds ) ))
    {
        free( args );
        free( request );
        return result;
    }

    args->ICoreTextLayoutRequestedEventArgs.lpVtbl = &layout_requested_args_vtbl;
    args->ref = 1;
    args->request = request;

    request->ICoreTextLayoutRequest.lpVtbl = &layout_request_vtbl;
    request->ref = 1;
    request->bounds = bounds;
    EnterCriticalSection( &impl->cs );
    request->range = impl->composition.StartCaretPosition || impl->composition.EndCaretPosition
                     ? impl->composition : impl->selection;
    LeaveCriticalSection( &impl->cs );

    event_list_invoke( impl, &impl->layout_requested,
                       (IUnknown *)&args->ICoreTextLayoutRequestedEventArgs );

    ICoreTextLayoutBounds_get_TextBounds( bounds, &result );

    ICoreTextLayoutBounds_Release( bounds );
    request->ref--;
    free( request );
    args->ref--;
    free( args );
    return result;
}

static void update_ime_composition_window( struct core_text_edit_context *impl )
{
    COMPOSITIONFORM form;
    HIMC himc;
    Rect bounds;

    EnterCriticalSection( &impl->cs );
    if (!impl->hwnd)
    {
        LeaveCriticalSection( &impl->cs );
        return;
    }
    LeaveCriticalSection( &impl->cs );

    bounds = query_layout_text_bounds( impl );

    himc = ImmGetContext( impl->hwnd );
    if (!himc) return;

    ZeroMemory( &form, sizeof(form) );
    form.dwStyle = CFS_POINT;
    form.ptCurrentPos.x = (LONG)bounds.X;
    form.ptCurrentPos.y = (LONG)(bounds.Y + bounds.Height);
    ImmSetCompositionWindow( himc, &form );
    ImmReleaseContext( impl->hwnd, himc );
}

/* Read a wide-string composition string (preedit or result) from the IME
 * context, appending it into *out.  Returns a newly allocated buffer and its
 * character length, or NULL. */
static WCHAR *read_composition_string( HIMC himc, DWORD flag, DWORD *out_len )
{
    BYTE buffer[ 512 ];
    LONG bytes;
    WCHAR *str;
    DWORD len;

    *out_len = 0;
    bytes = ImmGetCompositionStringW( himc, flag, NULL, 0 );
    if (bytes <= 0) return NULL;

    if (bytes <= sizeof(buffer))
    {
        len = bytes / sizeof(WCHAR);
        ImmGetCompositionStringW( himc, flag, buffer, bytes );
        if (!(str = malloc( bytes + sizeof(WCHAR) ))) return NULL;
        memcpy( str, buffer, bytes );
        str[len] = 0;
        *out_len = len;
        return str;
    }

    if (!(str = malloc( bytes + sizeof(WCHAR) ))) return NULL;
    ImmGetCompositionStringW( himc, flag, str, bytes );
    len = bytes / sizeof(WCHAR);
    str[len] = 0;
    *out_len = len;
    return str;
}

static void fire_text_updating( struct core_text_edit_context *impl, CoreTextRange range,
                                const WCHAR *text, DWORD len, CoreTextRange new_selection )
{
    ICoreTextTextUpdatingEventArgs *args;
    HSTRING str = NULL;

    if (text && len)
        WindowsCreateString( text, len, &str );
    else
        WindowsCreateString( L"", 0, &str );

    if (SUCCEEDED( text_updating_args_create( range, str, new_selection, &args ) ))
    {
        event_list_invoke( impl, &impl->text_updating, args );
        ICoreTextTextUpdatingEventArgs_Release( args );
    }
    WindowsDeleteString( str );
}

static void handle_ime_composition( struct core_text_edit_context *impl, LPARAM lp )
{
    HIMC himc = ImmGetContext( impl->hwnd );
    CoreTextRange range, new_sel;

    if (!himc) return;

    if (lp & GCS_COMPSTR)
    {
        DWORD len = 0;
        WCHAR *preedit = read_composition_string( himc, GCS_COMPSTR, &len );
        LONG cursor = 0;

        if (lp & GCS_CURSORPOS) cursor = ImmGetCompositionStringW( himc, GCS_CURSORPOS, NULL, 0 );
        if (cursor < 0) cursor = 0;

        EnterCriticalSection( &impl->cs );
        range = impl->composition;
        range.EndCaretPosition = range.StartCaretPosition + len;
        impl->composition = range;
        WindowsDeleteString( impl->preedit );
        impl->preedit = NULL;
        if (preedit) WindowsCreateString( preedit, len, &impl->preedit );
        LeaveCriticalSection( &impl->cs );

        new_sel.StartCaretPosition = new_sel.EndCaretPosition = range.StartCaretPosition + cursor;

        fire_text_updating( impl, range, preedit, len, new_sel );
        free( preedit );
    }

    if (lp & GCS_RESULTSTR)
    {
        DWORD len = 0;
        WCHAR *result = read_composition_string( himc, GCS_RESULTSTR, &len );

        EnterCriticalSection( &impl->cs );
        range = impl->composition;
        LeaveCriticalSection( &impl->cs );

        new_sel.StartCaretPosition = new_sel.EndCaretPosition = range.StartCaretPosition + len;

        fire_text_updating( impl, range, result, len, new_sel );

        EnterCriticalSection( &impl->cs );
        impl->composition.EndCaretPosition = range.StartCaretPosition + len;
        impl->selection = new_sel;
        LeaveCriticalSection( &impl->cs );

        free( result );
    }

    ImmReleaseContext( impl->hwnd, himc );
}

static LRESULT WINAPI ime_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct core_text_edit_context *impl = ime_current;
    WNDPROC old;

    if (impl && impl->hwnd == hwnd)
    {
        switch (msg)
        {
        case WM_IME_STARTCOMPOSITION:
            EnterCriticalSection( &impl->cs );
            impl->composing = TRUE;
            impl->composition = impl->selection;
            WindowsDeleteString( impl->preedit );
            impl->preedit = NULL;
            LeaveCriticalSection( &impl->cs );

            {
                ICoreTextCompositionStartedEventArgs *args;
                if (SUCCEEDED( composition_started_args_create( &args ) ))
                {
                    event_list_invoke( impl, &impl->composition_started,
                                       (IUnknown *)args );
                    ICoreTextCompositionStartedEventArgs_Release( args );
                }
            }
            update_ime_composition_window( impl );
            break;

        case WM_IME_COMPOSITION:
            handle_ime_composition( impl, lparam );
            break;

        case WM_IME_ENDCOMPOSITION:
        {
            ICoreTextCompositionCompletedEventArgs *args;
            EnterCriticalSection( &impl->cs );
            impl->composing = FALSE;
            WindowsDeleteString( impl->preedit );
            impl->preedit = NULL;
            LeaveCriticalSection( &impl->cs );

            if (SUCCEEDED( composition_completed_args_create( lparam == 0, &args ) ))
            {
                event_list_invoke( impl, &impl->composition_completed, (IUnknown *)args );
                ICoreTextCompositionCompletedEventArgs_Release( args );
            }
            break;
        }
        case WM_NCDESTROY:
            EnterCriticalSection( &impl->cs );
            impl->hwnd = NULL;
            impl->old_proc = NULL;
            impl->composing = FALSE;
            impl->has_focus = FALSE;
            LeaveCriticalSection( &impl->cs );
            if (ime_current == impl) ime_current = NULL;
            break;
        }
    }

    old = impl ? impl->old_proc : NULL;
    return CallWindowProcW( old ? old : DefWindowProcW, hwnd, msg, wparam, lparam );
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

HRESULT core_text_edit_context_create( ICoreTextEditContext **out )
{
    struct core_text_edit_context *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->ICoreTextEditContext.lpVtbl = &core_text_edit_context_vtbl;
    impl->ref = 1;
    InitializeCriticalSection( &impl->cs );
    list_init( &impl->text_requested );
    list_init( &impl->selection_requested );
    list_init( &impl->layout_requested );
    list_init( &impl->text_updating );
    list_init( &impl->selection_updating );
    list_init( &impl->format_updating );
    list_init( &impl->composition_started );
    list_init( &impl->composition_completed );
    list_init( &impl->focus_removed );

    *out = &impl->ICoreTextEditContext;
    return S_OK;
}
