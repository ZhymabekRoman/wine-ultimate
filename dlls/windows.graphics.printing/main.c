/* WinRT Windows.Graphics.Printing PrintManager Implementation
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

#include "initguid.h"
#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(printing);

/*
 * PrintManager is the entry point of the WinRT print contract: the app
 * activates the class, calls IPrintManagerStatics::GetForCurrentView for the
 * manager of the current view, registers a PrintTaskRequested handler and
 * calls ShowPrintUIAsync to start a print job. On Windows the print
 * experience (the print flyout) raises PrintTaskRequested; the app answers
 * with PrintTaskRequest::CreatePrintTask and the print system pulls the
 * IPrintDocumentSource from the task.
 *
 * A prefix has no print flyout. The manager here owns the handler list, and
 * ShowPrintUIAsync raises PrintTaskRequested on it synchronously, so an app
 * driving the contract reaches CreatePrintTask and the source-requested
 * handler; the destination behind them is the winspool/CUPS queue list.
 *
 * One object answers every interface of the class: the activation factory,
 * IPrintManagerStatics/IPrintManagerStatics2 (the statics Windows hands out
 * through the factory) and IPrintManager (the instance surface, which
 * GetForCurrentView also returns a fresh object of).
 */

struct print_manager_handler
{
    EventRegistrationToken token;
    PrintTaskRequestedEventHandler *handler; /* owned */
    struct print_manager_handler *next;
};

struct print_manager
{
    IActivationFactory IActivationFactory_iface;
    IPrintManagerStatics IPrintManagerStatics_iface;
    IPrintManagerStatics2 IPrintManagerStatics2_iface;
    IPrintManager IPrintManager_iface;
    LONG ref;
    struct print_manager_handler *handlers;
    LONG next_token;
};

static const struct IPrintManagerStaticsVtbl statics_vtbl;
static const struct IPrintManagerStatics2Vtbl statics2_vtbl;
static const struct IPrintManagerVtbl manager_vtbl;

static inline struct print_manager *impl_from_IInspectable( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct print_manager, IActivationFactory_iface );
}

static inline struct print_manager *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct print_manager, IActivationFactory_iface );
}

static void print_manager_destroy( struct print_manager *impl )
{
    struct print_manager_handler *cursor = impl->handlers;

    while (cursor)
    {
        struct print_manager_handler *next = cursor->next;
        IUnknown_Release( (IUnknown *)cursor->handler );
        free( cursor );
        cursor = next;
    }
    free( impl );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct print_manager *impl = impl_from_IActivationFactory( iface );

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

    if (IsEqualGUID( iid, &IID_IPrintManagerStatics ))
    {
        *out = &impl->IPrintManagerStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPrintManagerStatics2 ))
    {
        *out = &impl->IPrintManagerStatics2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    /* The class surface is served out of the same object: an app that casts the
     * factory to IPrintManager (the C++/CX projection does this for a
     * RoActivateInstance'd PrintManager) gets the manager it expects. */
    if (IsEqualGUID( iid, &IID_IPrintManager ))
    {
        *out = &impl->IPrintManager_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct print_manager *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );

    TRACE( "iface %p, new ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct print_manager *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, new ref %lu.\n", iface, ref );

    if (!ref) print_manager_destroy( impl );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    static const IID * const iids_array[] =
    {
        &IID_IPrintManager,
        &IID_IPrintManagerStatics,
        &IID_IPrintManagerStatics2,
    };
    ULONG size, count = ARRAYSIZE( iids_array );
    IID *array;

    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (!iid_count || !iids) return E_POINTER;
    size = count * sizeof(**iids);
    if (!(array = CoTaskMemAlloc( size ))) return E_OUTOFMEMORY;

    memcpy( array, iids_array, size );
    *iid_count = count;
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Graphics_Printing_PrintManager,
                                sizeof(RuntimeClass_Windows_Graphics_Printing_PrintManager)/sizeof(WCHAR) - 1,
                                class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    TRACE( "iface %p, trust_level %p.\n", iface, trust_level );

    if (!trust_level) return E_POINTER;
    *trust_level = FullTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct print_manager *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = (IInspectable *)&impl->IPrintManager_iface;
    IInspectable_AddRef( *instance );
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

DEFINE_IINSPECTABLE( statics, IPrintManagerStatics, struct print_manager, IActivationFactory_iface )
DEFINE_IINSPECTABLE( statics2, IPrintManagerStatics2, struct print_manager, IActivationFactory_iface )
DEFINE_IINSPECTABLE( manager, IPrintManager, struct print_manager, IActivationFactory_iface )

static struct print_manager *create_print_manager( void )
{
    struct print_manager *impl;

    if ((impl = calloc( 1, sizeof(*impl) )))
    {
        impl->ref = 1;
        impl->IActivationFactory_iface.lpVtbl = &factory_vtbl;
        impl->IPrintManagerStatics_iface.lpVtbl = &statics_vtbl;
        impl->IPrintManagerStatics2_iface.lpVtbl = &statics2_vtbl;
        impl->IPrintManager_iface.lpVtbl = &manager_vtbl;
    }
    return impl;
}

/* The manager of the "current view": GetForCurrentView hands out a fresh
 * object and remembers it, so the static ShowPrintUIAsync raises
 * PrintTaskRequested on the manager the app registered its handlers on. */
static struct print_manager *current_view_manager;

static HRESULT WINAPI statics_GetForCurrentView( IPrintManagerStatics *iface, PrintManager **value )
{
    struct print_manager *impl;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!(impl = create_print_manager())) return E_OUTOFMEMORY;
    current_view_manager = impl;

    *value = (PrintManager *)&impl->IPrintManager_iface;
    TRACE( "returning manager %p.\n", impl );
    return S_OK;
}

/*
 * winspool/CUPS bridge.
 *
 * winspool.drv maps the printer list onto the CUPS queues of the host:
 * GetDefaultPrinterW reports the lpadmin default and EnumPrintersW enumerates
 * every queue. The WinRT surface exposes exactly this much of it today - the
 * destination ShowPrintUIAsync prints to, and how many queues the print system
 * offers - and logs it, so a run shows which CUPS queue the job lands on.
 */
HRESULT printing_get_default_printer( WCHAR *buffer, DWORD *size )
{
    DWORD cch = size ? *size : 0;

    if (!GetDefaultPrinterW( buffer, &cch ))
    {
        DWORD err = GetLastError();

        if (err != ERROR_INSUFFICIENT_BUFFER)
        {
            WARN("No default printer, error %#lx.\n", err);
            return E_FAIL;
        }
        if (size) *size = cch;
        return E_OUTOFMEMORY;
    }
    if (size) *size = cch;
    return S_OK;
}

ULONG printing_get_printer_count( void )
{
    PRINTER_INFO_2W *info = NULL;
    DWORD needed = 0, count = 0, size = 0;

    /* Ask for the size first, then the list; level 2 is what the print UI
     * surface uses, as every queue entry carries its name. */
    EnumPrintersW( PRINTER_ENUM_LOCAL, NULL, 2, NULL, 0, &needed, &count );
    if (!needed) return 0;

    size = needed;
    if (!(info = HeapAlloc( GetProcessHeap(), 0, size ))) return 0;

    if (EnumPrintersW( PRINTER_ENUM_LOCAL, NULL, 2, (BYTE *)info, size, &needed, &count ))
    {
        TRACE( "%lu printer(s) available.\n", count );
        HeapFree( GetProcessHeap(), 0, info );
        return count;
    }

    HeapFree( GetProcessHeap(), 0, info );
    return 0;
}

static HRESULT WINAPI statics_ShowPrintUIAsync( IPrintManagerStatics *iface,
                                                __FIAsyncOperation_1_boolean **operation )
{
    struct print_manager *impl = current_view_manager ? current_view_manager : statics_impl_from( iface );
    WCHAR printer[256];
    HRESULT hr;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    DWORD size = ARRAYSIZE( printer );
    hr = printing_get_default_printer( printer, &size );
    if (SUCCEEDED( hr ))
        TRACE( "Printing to default queue %s, %lu queue(s) available.\n",
               debugstr_w( printer ), printing_get_printer_count() );
    else
        TRACE( "No default queue, %lu queue(s) available.\n", printing_get_printer_count() );

    if (FAILED( hr = raise_print_task_requested( (IInspectable *)&impl->IPrintManager_iface ) ))
        return hr;

    if (!(*operation = (__FIAsyncOperation_1_boolean *)bool_async_op_iface( create_bool_async_op() )))
        return E_OUTOFMEMORY;

    return S_OK;
}

static const struct IPrintManagerStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    /* IInspectable methods */
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    /* IPrintManagerStatics methods */
    statics_GetForCurrentView,
    statics_ShowPrintUIAsync,
};

static HRESULT WINAPI statics2_get_IsSupported( IPrintManagerStatics2 *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    /* The prefix answers the print contract, so the surface is supported. */
    *value = TRUE;
    return S_OK;
}

static const struct IPrintManagerStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface,
    statics2_AddRef,
    statics2_Release,
    /* IInspectable methods */
    statics2_GetIids,
    statics2_GetRuntimeClassName,
    statics2_GetTrustLevel,
    /* IPrintManagerStatics2 methods */
    statics2_get_IsSupported,
};

static HRESULT WINAPI manager_add_PrintTaskRequested( IPrintManager *iface,
        PrintTaskRequestedEventHandler *handler, EventRegistrationToken *token )
{
    struct print_manager *impl = manager_impl_from( iface );
    struct print_manager_handler *entry;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!token) return E_POINTER;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return E_OUTOFMEMORY;

    entry->token.value = InterlockedIncrement( &impl->next_token );
    if ((entry->handler = handler)) IUnknown_AddRef( (IUnknown *)handler );
    entry->next = impl->handlers;
    impl->handlers = entry;

    TRACE( "registered handler %p, token %s.\n", handler, wine_dbgstr_longlong( entry->token.value ) );
    *token = entry->token;
    return S_OK;
}

static HRESULT WINAPI manager_remove_PrintTaskRequested( IPrintManager *iface,
                                                         EventRegistrationToken token )
{
    struct print_manager *impl = manager_impl_from( iface );
    struct print_manager_handler **slot = &impl->handlers;

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );

    while (*slot)
    {
        struct print_manager_handler *entry = *slot;

        if (entry->token.value == token.value)
        {
            *slot = entry->next;
            IUnknown_Release( (IUnknown *)entry->handler );
            free( entry );
            return S_OK;
        }
        slot = &entry->next;
    }
    return E_INVALIDARG;
}

static const struct IPrintManagerVtbl manager_vtbl =
{
    manager_QueryInterface,
    manager_AddRef,
    manager_Release,
    /* IInspectable methods */
    manager_GetIids,
    manager_GetRuntimeClassName,
    manager_GetTrustLevel,
    /* IPrintManager methods */
    manager_add_PrintTaskRequested,
    manager_remove_PrintTaskRequested,
};

HRESULT raise_print_task_requested( IInspectable *manager )
{
    struct print_manager *impl = impl_from_IInspectable( manager );
    struct print_manager_handler *cursor;
    HRESULT hr;

    for (cursor = impl->handlers; cursor; cursor = cursor->next)
    {
        IInspectable *args = create_print_task_requested_event_args();
        if (!args) return E_OUTOFMEMORY;

        hr = PrintTaskRequestedEventHandler_Invoke( cursor->handler, (PrintManager *)manager,
                                                    (PrintTaskRequestedEventArgs *)args );
        IInspectable_Release( args );
        if (FAILED( hr )) return hr;
    }
    return S_OK;
}

static struct print_manager print_manager_statics =
{
    { &factory_vtbl },
    { &statics_vtbl },
    { &statics2_vtbl },
    { &manager_vtbl },
    1,
};

IActivationFactory *print_manager_factory = (IActivationFactory *)&print_manager_statics;

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_Graphics_Printing_PrintManager ))
        IActivationFactory_QueryInterface( print_manager_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}
