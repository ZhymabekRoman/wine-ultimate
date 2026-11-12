/* WinRT Windows.Graphics.Printing print task objects
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

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(printing);

/*
 * The task side of the print contract.
 *
 * Windows raises PrintTaskRequested on the manager, the app answers with
 * PrintTaskRequest::CreatePrintTask(title, handler) and the print system calls
 * that handler with PrintTaskSourceRequestedArgs to be handed the
 * IPrintDocumentSource. With no print flyout in a prefix, this module runs the
 * whole chain synchronously inside ShowPrintUIAsync: the event args carry one
 * request object, CreatePrintTask builds a PrintTask and immediately calls the
 * source-requested handler with fresh args, and args->SetSource() stores the
 * source straight onto the task. The app sees the same call order as on
 * Windows, minus the UI.
 */

static HRESULT DateTime_now( WindowsFoundationDateTime *value )
{
    FILETIME ft;

    GetSystemTimeAsFileTime( &ft );
    /* Windows.Foundation.DateTime counts 100ns intervals from 1601-01-01, as FILETIME. */
    value->UniversalTime = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* PrintTask                                                                  */
/* -------------------------------------------------------------------------- */

struct print_task
{
    IPrintTask IPrintTask_iface;
    LONG ref;
    HSTRING id;
    HSTRING title;
    IPrintDocumentSource *source; /* owned */
};

static inline struct print_task *task_from_impl( IPrintTask *iface )
{
    return CONTAINING_RECORD( iface, struct print_task, IPrintTask_iface );
}

static HRESULT WINAPI print_task_QueryInterface( IPrintTask *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPrintTask ))
    {
        *out = iface;
        IPrintTask_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI print_task_AddRef( IPrintTask *iface )
{
    struct print_task *impl = task_from_impl( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI print_task_Release( IPrintTask *iface )
{
    struct print_task *impl = task_from_impl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->id );
        WindowsDeleteString( impl->title );
        if (impl->source) IPrintDocumentSource_Release( impl->source );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI print_task_GetIids( IPrintTask *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI print_task_GetRuntimeClassName( IPrintTask *iface, HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Printing_PrintTask,
                                sizeof(RuntimeClass_Windows_Graphics_Printing_PrintTask)/sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI print_task_GetTrustLevel( IPrintTask *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI print_task_get_Id( IPrintTask *iface, HSTRING *value )
{
    struct print_task *impl = task_from_impl( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->id;
    return S_OK;
}

static HRESULT WINAPI print_task_put_Title( IPrintTask *iface, HSTRING value )
{
    struct print_task *impl = task_from_impl( iface );

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    WindowsDeleteString( impl->title );
    impl->title = value;
    if (value) WindowsDuplicateString( value, &impl->title );
    return S_OK;
}

static HRESULT WINAPI print_task_get_Title( IPrintTask *iface, HSTRING *value )
{
    struct print_task *impl = task_from_impl( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->title;
    return S_OK;
}

static HRESULT WINAPI print_task_put_Source( IPrintTask *iface, IPrintDocumentSource *value )
{
    struct print_task *impl = task_from_impl( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (impl->source) IPrintDocumentSource_Release( impl->source );
    impl->source = value;
    if (value) IPrintDocumentSource_AddRef( value );
    return S_OK;
}

static HRESULT WINAPI print_task_get_Source( IPrintTask *iface, IPrintDocumentSource **value )
{
    struct print_task *impl = task_from_impl( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->source;
    if (*value) IPrintDocumentSource_AddRef( *value );
    return S_OK;
}

static const struct IPrintTaskVtbl print_task_vtbl =
{
    print_task_QueryInterface,
    print_task_AddRef,
    print_task_Release,
    /* IInspectable methods */
    print_task_GetIids,
    print_task_GetRuntimeClassName,
    print_task_GetTrustLevel,
    /* IPrintTask methods */
    print_task_get_Id,
    print_task_put_Title,
    print_task_get_Title,
    print_task_put_Source,
    print_task_get_Source,
};

static HRESULT create_print_task( HSTRING title, struct print_task **value )
{
    static LONG next_id;
    struct print_task *impl;
    WCHAR id[32];

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->ref = 1;
    impl->IPrintTask_iface.lpVtbl = &print_task_vtbl;
    wsprintfW( id, L"%04x-%04x", 0x0002, ++next_id ); /* a generated per-task id, as the print system hands out */
    WindowsCreateString( id, lstrlenW( id ), &impl->id );
    if (title) WindowsDuplicateString( title, &impl->title );

    *value = impl;
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* Deferrals                                                                  */
/* -------------------------------------------------------------------------- */

struct requested_deferral
{
    IPrintTaskRequestedDeferral IPrintTaskRequestedDeferral_iface;
    LONG ref;
};

static inline struct requested_deferral *deferral_from_impl( IPrintTaskRequestedDeferral *iface )
{
    return CONTAINING_RECORD( iface, struct requested_deferral, IPrintTaskRequestedDeferral_iface );
}

static HRESULT WINAPI requested_deferral_QueryInterface( IPrintTaskRequestedDeferral *iface,
                                                         REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPrintTaskRequestedDeferral ))
    {
        *out = iface;
        IPrintTaskRequestedDeferral_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI requested_deferral_AddRef( IPrintTaskRequestedDeferral *iface )
{
    struct requested_deferral *impl = deferral_from_impl( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI requested_deferral_Release( IPrintTaskRequestedDeferral *iface )
{
    struct requested_deferral *impl = deferral_from_impl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

DEFINE_IINSPECTABLE_( deferral, IPrintTaskRequestedDeferral, struct requested_deferral,
                      deferral_from_impl, IPrintTaskRequestedDeferral_iface,
                      &impl->IPrintTaskRequestedDeferral_iface )

static HRESULT WINAPI requested_deferral_Complete( IPrintTaskRequestedDeferral *iface )
{
    TRACE( "iface %p.\n", iface );
    /* Nothing is deferred: the whole chain completes synchronously. */
    return S_OK;
}

static const struct IPrintTaskRequestedDeferralVtbl requested_deferral_vtbl =
{
    requested_deferral_QueryInterface,
    requested_deferral_AddRef,
    requested_deferral_Release,
    /* IInspectable methods */
    deferral_GetIids,
    deferral_GetRuntimeClassName,
    deferral_GetTrustLevel,
    /* IPrintTaskRequestedDeferral methods */
    requested_deferral_Complete,
};

static HRESULT create_requested_deferral( PrintTaskRequestedDeferral **value )
{
    struct requested_deferral *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ref = 1;
    impl->IPrintTaskRequestedDeferral_iface.lpVtbl = &requested_deferral_vtbl;
    *value = (PrintTaskRequestedDeferral *)&impl->IPrintTaskRequestedDeferral_iface;
    return S_OK;
}

struct source_deferral
{
    IPrintTaskSourceRequestedDeferral IPrintTaskSourceRequestedDeferral_iface;
    LONG ref;
};

static inline struct source_deferral *source_deferral_from_impl( IPrintTaskSourceRequestedDeferral *iface )
{
    return CONTAINING_RECORD( iface, struct source_deferral, IPrintTaskSourceRequestedDeferral_iface );
}

static HRESULT WINAPI source_deferral_QueryInterface( IPrintTaskSourceRequestedDeferral *iface,
                                                      REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPrintTaskSourceRequestedDeferral ))
    {
        *out = iface;
        IPrintTaskSourceRequestedDeferral_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI source_deferral_AddRef( IPrintTaskSourceRequestedDeferral *iface )
{
    struct source_deferral *impl = source_deferral_from_impl( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI source_deferral_Release( IPrintTaskSourceRequestedDeferral *iface )
{
    struct source_deferral *impl = source_deferral_from_impl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

DEFINE_IINSPECTABLE_( sdeferral, IPrintTaskSourceRequestedDeferral, struct source_deferral,
                      source_deferral_from_impl, IPrintTaskSourceRequestedDeferral_iface,
                      &impl->IPrintTaskSourceRequestedDeferral_iface )

static HRESULT WINAPI source_deferral_Complete( IPrintTaskSourceRequestedDeferral *iface )
{
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static const struct IPrintTaskSourceRequestedDeferralVtbl source_deferral_vtbl =
{
    source_deferral_QueryInterface,
    source_deferral_AddRef,
    source_deferral_Release,
    /* IInspectable methods */
    sdeferral_GetIids,
    sdeferral_GetRuntimeClassName,
    sdeferral_GetTrustLevel,
    /* IPrintTaskSourceRequestedDeferral methods */
    source_deferral_Complete,
};

static HRESULT create_source_deferral( PrintTaskSourceRequestedDeferral **value )
{
    struct source_deferral *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ref = 1;
    impl->IPrintTaskSourceRequestedDeferral_iface.lpVtbl = &source_deferral_vtbl;
    *value = (PrintTaskSourceRequestedDeferral *)&impl->IPrintTaskSourceRequestedDeferral_iface;
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* PrintTaskSourceRequestedArgs                                               */
/* -------------------------------------------------------------------------- */

struct source_requested_args
{
    IPrintTaskSourceRequestedArgs IPrintTaskSourceRequestedArgs_iface;
    LONG ref;
    struct print_task *task; /* owned; SetSource() stores the source onto it */
};

static inline struct source_requested_args *args_from_impl( IPrintTaskSourceRequestedArgs *iface )
{
    return CONTAINING_RECORD( iface, struct source_requested_args, IPrintTaskSourceRequestedArgs_iface );
}

static HRESULT WINAPI source_args_QueryInterface( IPrintTaskSourceRequestedArgs *iface,
                                                  REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPrintTaskSourceRequestedArgs ))
    {
        *out = iface;
        IPrintTaskSourceRequestedArgs_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI source_args_AddRef( IPrintTaskSourceRequestedArgs *iface )
{
    struct source_requested_args *impl = args_from_impl( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI source_args_Release( IPrintTaskSourceRequestedArgs *iface )
{
    struct source_requested_args *impl = args_from_impl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        IPrintTask_Release( &impl->task->IPrintTask_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI source_args_GetIids( IPrintTaskSourceRequestedArgs *iface,
                                           ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI source_args_GetRuntimeClassName( IPrintTaskSourceRequestedArgs *iface,
                                                       HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Printing_PrintTaskSourceRequestedArgs,
                                sizeof(RuntimeClass_Windows_Graphics_Printing_PrintTaskSourceRequestedArgs)/sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI source_args_GetTrustLevel( IPrintTaskSourceRequestedArgs *iface,
                                                 TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI source_args_get_Deadline( IPrintTaskSourceRequestedArgs *iface,
                                                WindowsFoundationDateTime *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return DateTime_now( value );
}

static HRESULT WINAPI source_args_SetSource( IPrintTaskSourceRequestedArgs *iface,
                                             IPrintDocumentSource *value )
{
    struct source_requested_args *impl = args_from_impl( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    return IPrintTask_put_Source( &impl->task->IPrintTask_iface, value );
}

static HRESULT WINAPI source_args_GetDeferral( IPrintTaskSourceRequestedArgs *iface,
        PrintTaskSourceRequestedDeferral **deferral )
{
    TRACE( "iface %p, deferral %p.\n", iface, deferral );

    if (!deferral) return E_POINTER;
    return create_source_deferral( deferral );
}

static const struct IPrintTaskSourceRequestedArgsVtbl source_args_vtbl =
{
    source_args_QueryInterface,
    source_args_AddRef,
    source_args_Release,
    /* IInspectable methods */
    source_args_GetIids,
    source_args_GetRuntimeClassName,
    source_args_GetTrustLevel,
    /* IPrintTaskSourceRequestedArgs methods */
    source_args_get_Deadline,
    source_args_SetSource,
    source_args_GetDeferral,
};

/* The print system calls the app's source-requested handler with these args;
 * SetSource() on them stores the IPrintDocumentSource onto the task the handler
 * was registered for. The task outlives the call, so the args hold a ref. */
static HRESULT create_source_requested_args( struct print_task *task,
        PrintTaskSourceRequestedArgs **value )
{
    struct source_requested_args *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ref = 1;
    impl->IPrintTaskSourceRequestedArgs_iface.lpVtbl = &source_args_vtbl;
    impl->task = task;
    IPrintTask_AddRef( &task->IPrintTask_iface );
    *value = (PrintTaskSourceRequestedArgs *)&impl->IPrintTaskSourceRequestedArgs_iface;
    return S_OK;
}

/* -------------------------------------------------------------------------- */
/* PrintTaskRequest                                                           */
/* -------------------------------------------------------------------------- */

struct print_task_request
{
    IPrintTaskRequest IPrintTaskRequest_iface;
    LONG ref;
};

static inline struct print_task_request *request_from_impl( IPrintTaskRequest *iface )
{
    return CONTAINING_RECORD( iface, struct print_task_request, IPrintTaskRequest_iface );
}

static HRESULT WINAPI request_QueryInterface( IPrintTaskRequest *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPrintTaskRequest ))
    {
        *out = iface;
        IPrintTaskRequest_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI request_AddRef( IPrintTaskRequest *iface )
{
    struct print_task_request *impl = request_from_impl( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI request_Release( IPrintTaskRequest *iface )
{
    struct print_task_request *impl = request_from_impl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI request_GetIids( IPrintTaskRequest *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI request_GetRuntimeClassName( IPrintTaskRequest *iface, HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Printing_PrintTaskRequest,
                                sizeof(RuntimeClass_Windows_Graphics_Printing_PrintTaskRequest)/sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI request_GetTrustLevel( IPrintTaskRequest *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI request_CreatePrintTask( IPrintTaskRequest *iface, HSTRING title,
        PrintTaskSourceRequestedHandler *handler, PrintTask **value )
{
    PrintTaskSourceRequestedArgs *args;
    struct print_task *task;
    HRESULT hr;

    TRACE( "iface %p, title %s, handler %p, value %p.\n", iface, debugstr_hstring( title ), handler, value );

    if (!value) return E_POINTER;
    if (FAILED( hr = create_print_task( title, &task ) )) return hr;
    *value = (PrintTask *)&task->IPrintTask_iface;

    if (handler)
    {
        /* Hand the app the args it fills with a document source, as the print
         * system does after CreatePrintTask returns. */
        if (FAILED( hr = create_source_requested_args( task, &args ) ))
        {
            IPrintTask_Release( (PrintTask *)&task->IPrintTask_iface );
            return hr;
        }
        hr = PrintTaskSourceRequestedHandler_Invoke( handler, args );
        IPrintTaskSourceRequestedArgs_Release( args );
    }
    return S_OK;
}

static HRESULT WINAPI request_get_Deadline( IPrintTaskRequest *iface, WindowsFoundationDateTime *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return DateTime_now( value );
}

static HRESULT WINAPI request_GetDeferral( IPrintTaskRequest *iface,
        PrintTaskRequestedDeferral **deferral )
{
    TRACE( "iface %p, deferral %p.\n", iface, deferral );

    if (!deferral) return E_POINTER;
    return create_requested_deferral( deferral );
}

static const struct IPrintTaskRequestVtbl request_vtbl =
{
    request_QueryInterface,
    request_AddRef,
    request_Release,
    /* IInspectable methods */
    request_GetIids,
    request_GetRuntimeClassName,
    request_GetTrustLevel,
    /* IPrintTaskRequest methods */
    request_CreatePrintTask,
    request_get_Deadline,
    request_GetDeferral,
};

IInspectable *create_print_task_request( void )
{
    struct print_task_request *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ref = 1;
    impl->IPrintTaskRequest_iface.lpVtbl = &request_vtbl;
    return (IInspectable *)&impl->IPrintTaskRequest_iface;
}

/* -------------------------------------------------------------------------- */
/* PrintTaskRequestedEventArgs                                                */
/* -------------------------------------------------------------------------- */

struct print_task_requested_event_args
{
    IPrintTaskRequestedEventArgs IPrintTaskRequestedEventArgs_iface;
    LONG ref;
    IInspectable *request; /* PrintTaskRequest *, owned, created on demand */
};

static inline struct print_task_requested_event_args *event_args_from_impl(
        IPrintTaskRequestedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct print_task_requested_event_args,
                              IPrintTaskRequestedEventArgs_iface );
}

static HRESULT WINAPI event_args_QueryInterface( IPrintTaskRequestedEventArgs *iface,
                                                 REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPrintTaskRequestedEventArgs ))
    {
        *out = iface;
        IPrintTaskRequestedEventArgs_AddRef( iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI event_args_AddRef( IPrintTaskRequestedEventArgs *iface )
{
    struct print_task_requested_event_args *impl = event_args_from_impl( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI event_args_Release( IPrintTaskRequestedEventArgs *iface )
{
    struct print_task_requested_event_args *impl = event_args_from_impl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->request) IInspectable_Release( impl->request );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI event_args_GetIids( IPrintTaskRequestedEventArgs *iface,
                                          ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI event_args_GetRuntimeClassName( IPrintTaskRequestedEventArgs *iface,
                                                      HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Printing_PrintTaskRequestedEventArgs,
                                sizeof(RuntimeClass_Windows_Graphics_Printing_PrintTaskRequestedEventArgs)/sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI event_args_GetTrustLevel( IPrintTaskRequestedEventArgs *iface,
                                                TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI event_args_get_Request( IPrintTaskRequestedEventArgs *iface,
                                              PrintTaskRequest **value )
{
    struct print_task_requested_event_args *impl = event_args_from_impl( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->request && !(impl->request = create_print_task_request())) return E_OUTOFMEMORY;

    *value = (PrintTaskRequest *)impl->request;
    IInspectable_AddRef( impl->request );
    return S_OK;
}

static const struct IPrintTaskRequestedEventArgsVtbl event_args_vtbl =
{
    event_args_QueryInterface,
    event_args_AddRef,
    event_args_Release,
    /* IInspectable methods */
    event_args_GetIids,
    event_args_GetRuntimeClassName,
    event_args_GetTrustLevel,
    /* IPrintTaskRequestedEventArgs methods */
    event_args_get_Request,
};

IInspectable *create_print_task_requested_event_args( void )
{
    struct print_task_requested_event_args *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->ref = 1;
    impl->IPrintTaskRequestedEventArgs_iface.lpVtbl = &event_args_vtbl;
    return (IInspectable *)&impl->IPrintTaskRequestedEventArgs_iface;
}

/* -------------------------------------------------------------------------- */
/* Born-completed IAsyncOperation<boolean>                                    */
/* -------------------------------------------------------------------------- */

/*
 * ShowPrintUIAsync has no print flyout to wait for in a prefix, so the
 * operation it returns is created completed with the result TRUE:
 * IAsyncInfo::Status reports Completed, put_Completed invokes the handler
 * synchronously and GetResults returns TRUE. Both the C++/CX and the C++/WinRT
 * projection resume the caller on the spot. The operation is the same
 * two-vtable object web.core uses: the IAsyncOperation`1<Boolean> vtable first,
 * the IAsyncInfo vtable second.
 */

struct bool_async_op
{
    const void *self_vtbl;
    const void *info_vtbl;
    LONG ref;
    void *handler; /* AsyncOperationCompletedHandler<boolean> *, owned */
};

static inline struct bool_async_op *bool_op_from_self( const void *iface )
{
    return (struct bool_async_op *)iface;
}

static inline struct bool_async_op *bool_op_from_info( const void *iface )
{
    return (struct bool_async_op *)((const BYTE *)iface - sizeof(void *));
}

static void bool_op_invoke_completed_handler( struct bool_async_op *impl )
{
    void **vtbl;
    HRESULT (STDMETHODCALLTYPE *invoke)( void *handler, void *async, enum AsyncStatus status );

    if (!impl->handler) return;

    /* The completed handler is { QueryInterface, AddRef, Release, Invoke }. */
    vtbl = *(void ***)impl->handler;
    invoke = (void *)vtbl[3];
    invoke( impl->handler, (void *)impl, Completed );
}

static HRESULT WINAPI bool_op_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct bool_async_op *impl = bool_op_from_self( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FIAsyncOperation_1_boolean ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (void *)&impl->info_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI bool_op_AddRef( const void *iface )
{
    struct bool_async_op *impl = bool_op_from_self( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI bool_op_Release( const void *iface )
{
    struct bool_async_op *impl = bool_op_from_self( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI bool_op_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI bool_op_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Boolean>",
                                lstrlenW( L"Windows.Foundation.IAsyncOperation`1<Boolean>" ), class_name );
}

static HRESULT WINAPI bool_op_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI bool_op_put_Completed( const void *iface, void *handler )
{
    struct bool_async_op *impl = bool_op_from_self( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (impl->handler) IUnknown_Release( (IUnknown *)impl->handler );
    impl->handler = handler;
    if (handler) IUnknown_AddRef( (IUnknown *)handler );

    /* Created completed: a handler attached afterwards is called right away. */
    bool_op_invoke_completed_handler( impl );
    return S_OK;
}

static HRESULT WINAPI bool_op_get_Completed( const void *iface, void **handler )
{
    struct bool_async_op *impl = bool_op_from_self( iface );

    TRACE( "iface %p, handler %p.\n", iface, handler );

    if (!handler) return E_POINTER;
    *handler = impl->handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    return S_OK;
}

static HRESULT WINAPI bool_op_GetResults( const void *iface, boolean *results )
{
    TRACE( "iface %p, results %p.\n", iface, results );

    if (!results) return E_POINTER;
    *results = TRUE;
    return S_OK;
}

const void *bool_async_op_vtbl[] =
{
    bool_op_QueryInterface,
    bool_op_AddRef,
    bool_op_Release,
    bool_op_GetIids,
    bool_op_GetRuntimeClassName,
    bool_op_GetTrustLevel,
    bool_op_put_Completed,
    bool_op_get_Completed,
    bool_op_GetResults,
};

static HRESULT WINAPI bool_info_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct bool_async_op *impl = bool_op_from_info( iface );
    return bool_op_QueryInterface( (void *)impl, iid, out );
}

static ULONG WINAPI bool_info_AddRef( const void *iface )
{
    struct bool_async_op *impl = bool_op_from_info( iface );
    return bool_op_AddRef( (void *)impl );
}

static ULONG WINAPI bool_info_Release( const void *iface )
{
    struct bool_async_op *impl = bool_op_from_info( iface );
    return bool_op_Release( (void *)impl );
}

static HRESULT WINAPI bool_info_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    struct bool_async_op *impl = bool_op_from_info( iface );
    return bool_op_GetIids( (void *)impl, iid_count, iids );
}

static HRESULT WINAPI bool_info_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 29, class_name );
}

static HRESULT WINAPI bool_info_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI bool_info_get_Id( const void *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI bool_info_get_Status( const void *iface, enum AsyncStatus *status )
{
    if (!status) return E_POINTER;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI bool_info_get_ErrorCode( const void *iface, HRESULT *error_code )
{
    if (!error_code) return E_POINTER;
    *error_code = S_OK;
    return S_OK;
}

static HRESULT WINAPI bool_info_Cancel( const void *iface )
{
    return S_OK;
}

static HRESULT WINAPI bool_info_Close( const void *iface )
{
    return S_OK;
}

const void *bool_async_info_vtbl[] =
{
    bool_info_QueryInterface,
    bool_info_AddRef,
    bool_info_Release,
    bool_info_GetIids,
    bool_info_GetRuntimeClassName,
    bool_info_GetTrustLevel,
    bool_info_get_Id,
    bool_info_get_Status,
    bool_info_get_ErrorCode,
    bool_info_Cancel,
    bool_info_Close,
};

struct bool_async_op *create_bool_async_op( void )
{
    struct bool_async_op *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;

    impl->self_vtbl = bool_async_op_vtbl;
    impl->info_vtbl = bool_async_info_vtbl;
    impl->ref = 1;

    TRACE( "created completed bool op %p.\n", impl );
    return impl;
}

void *bool_async_op_iface( struct bool_async_op *impl )
{
    return (void *)impl;
}
