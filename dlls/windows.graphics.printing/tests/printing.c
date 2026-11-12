/*
 * WinRT Windows.Graphics.Printing tests
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

#define COBJMACROS
#include <windows.h>
#include <initguid.h>

#include "winstring.h"

#include "activation.h"
#include "asyncinfo.h"
#include "eventtoken.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Graphics_Printing
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.graphics.printing.h"

#include "wine/test.h"

/* Short names for the ns-prefixed C types (runtimeclasses surface as their
 * primary interface in the generated signatures). */
#define PrintManager IPrintManager
#define PrintTask IPrintTask
#define PrintTaskRequest IPrintTaskRequest
#define PrintTaskRequestedEventArgs IPrintTaskRequestedEventArgs
#define PrintTaskSourceRequestedArgs IPrintTaskSourceRequestedArgs
#define PrintTaskSourceRequestedDeferral IPrintTaskSourceRequestedDeferral
#define WindowsFoundationDateTime __x_ABI_CWindows_CFoundation_CDateTime

static HRESULT (WINAPI *pGetActivationFactory)(HSTRING classid, IActivationFactory **factory);

typedef __FITypedEventHandler_2_Windows__CGraphics__CPrinting__CPrintManager_Windows__CGraphics__CPrinting__CPrintTaskRequestedEventArgs
        PrintTaskRequestedEventHandler;

static const WCHAR manager_name[] = L"Windows.Graphics.Printing.PrintManager";

static IActivationFactory *get_factory(void)
{
    IActivationFactory *factory;
    HSTRING str;

    ok(S_OK == WindowsCreateString(manager_name, lstrlenW(manager_name), &str), "Failed to create string\n");
    ok(S_OK == pGetActivationFactory(str, &factory), "Failed to get activation factory\n");
    WindowsDeleteString(str);
    return factory;
}

/* IPrintDocumentSource object handed to SetSource and set on the task. */
static const IID *source_iids[] = { &IID_IPrintDocumentSource };

struct source
{
    IPrintDocumentSource IPrintDocumentSource_iface;
    LONG ref;
};

static HRESULT WINAPI source_QueryInterface( IPrintDocumentSource *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IPrintDocumentSource ))
    {
        *out = iface;
        IPrintDocumentSource_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI source_AddRef( IPrintDocumentSource *iface ) { return InterlockedIncrement( &((struct source *)iface)->ref ); }
static ULONG WINAPI source_Release( IPrintDocumentSource *iface ) { return InterlockedDecrement( &((struct source *)iface)->ref ); }

static HRESULT WINAPI source_GetIids( IPrintDocumentSource *iface, ULONG *count, IID **iids )
{
    ULONG size = ARRAYSIZE( source_iids ) * sizeof(**iids);
    IID *array;

    if (!(array = CoTaskMemAlloc( size ))) return E_OUTOFMEMORY;
    memcpy( array, source_iids, size );
    *count = ARRAYSIZE( source_iids );
    *iids = array;
    return S_OK;
}

static HRESULT WINAPI source_GetRuntimeClassName( IPrintDocumentSource *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Graphics.Printing.PrintDocumentSource",
                                lstrlenW( L"Windows.Graphics.Printing.PrintDocumentSource" ), name );
}

static HRESULT WINAPI source_GetTrustLevel( IPrintDocumentSource *iface, TrustLevel *level )
{
    *level = FullTrust;
    return S_OK;
}

static const struct IPrintDocumentSourceVtbl source_vtbl =
{
    source_QueryInterface, source_AddRef, source_Release,
    source_GetIids, source_GetRuntimeClassName, source_GetTrustLevel,
};

static struct source *create_source( void )
{
    struct source *impl = calloc( 1, sizeof(*impl) );

    impl->ref = 1;
    impl->IPrintDocumentSource_iface.lpVtbl = &source_vtbl;
    return impl;
}

/* Source-requested handler: stores the source onto the task through the args. */
static int source_handler_calls;
static IPrintDocumentSource *source_handler_source;

static HRESULT WINAPI source_handler_QueryInterface( IPrintTaskSourceRequestedHandler *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI source_handler_AddRef( IPrintTaskSourceRequestedHandler *iface ) { return 1; }
static ULONG WINAPI source_handler_Release( IPrintTaskSourceRequestedHandler *iface ) { return 1; }

static HRESULT WINAPI source_handler_Invoke( IPrintTaskSourceRequestedHandler *iface,
        PrintTaskSourceRequestedArgs *args )
{
    PrintTaskSourceRequestedDeferral *deferral;
    WindowsFoundationDateTime deadline;
    HRESULT hr;

    source_handler_calls++;
    hr = IPrintTaskSourceRequestedArgs_SetSource( args, source_handler_source );
    ok( SUCCEEDED( hr ), "SetSource failed, hr %#lx\n", hr );

    hr = IPrintTaskSourceRequestedArgs_get_Deadline( args, &deadline );
    ok( SUCCEEDED( hr ) && deadline.UniversalTime, "get_Deadline failed, hr %#lx\n", hr );

    hr = IPrintTaskSourceRequestedArgs_GetDeferral( args, &deferral );
    ok( SUCCEEDED( hr ) && deferral, "GetDeferral failed, hr %#lx\n", hr );
    if (SUCCEEDED( hr ))
    {
        hr = IPrintTaskSourceRequestedDeferral_Complete( deferral );
        ok( SUCCEEDED( hr ), "Complete failed, hr %#lx\n", hr );
        IPrintTaskSourceRequestedDeferral_Release( deferral );
    }
    return S_OK;
}

static const struct IPrintTaskSourceRequestedHandlerVtbl source_handler_vtbl =
{
    source_handler_QueryInterface, source_handler_AddRef, source_handler_Release, source_handler_Invoke,
};

static IPrintTaskSourceRequestedHandler source_handler = { &source_handler_vtbl };

/* PrintTaskRequested handler: answers with CreatePrintTask. */
static int print_task_requested_calls;
static PrintTask *created_task;

static HRESULT WINAPI handler_QueryInterface( PrintTaskRequestedEventHandler *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID___FITypedEventHandler_2_Windows__CGraphics__CPrinting__CPrintManager_Windows__CGraphics__CPrinting__CPrintTaskRequestedEventArgs ))
    {
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef( PrintTaskRequestedEventHandler *iface ) { return 1; }
static ULONG WINAPI handler_Release( PrintTaskRequestedEventHandler *iface ) { return 1; }

static HRESULT WINAPI handler_Invoke( PrintTaskRequestedEventHandler *iface, PrintManager *sender,
                                      PrintTaskRequestedEventArgs *args )
{
    PrintTaskRequest *request;
    HSTRING title;
    HRESULT hr;

    print_task_requested_calls++;
    ok( sender != NULL, "Expected sender\n" );

    hr = IPrintTaskRequestedEventArgs_get_Request( args, &request );
    ok( SUCCEEDED( hr ) && request, "get_Request failed, hr %#lx\n", hr );

    hr = WindowsCreateString( L"wine-test", lstrlenW( L"wine-test" ), &title );
    ok( SUCCEEDED( hr ), "Failed to create title\n" );
    hr = IPrintTaskRequest_CreatePrintTask( request, title, &source_handler, &created_task );
    ok( SUCCEEDED( hr ) && created_task, "CreatePrintTask failed, hr %#lx\n", hr );

    if (created_task)
    {
        IPrintDocumentSource *source = NULL;
        HSTRING got = NULL;

        hr = IPrintTask_get_Title( created_task, &got );
        ok( SUCCEEDED( hr ) && got, "get_Title failed, hr %#lx\n", hr );
        if (got)
        {
            ok( !wcscmp( WindowsGetStringRawBuffer( got, NULL ), L"wine-test" ), "Unexpected title\n" );
            WindowsDeleteString( got );
        }

        hr = IPrintTask_get_Source( created_task, &source );
        ok( SUCCEEDED( hr ) && source, "Expected source set by the source-requested handler\n" );
        if (source) IPrintDocumentSource_Release( source );
    }

    IPrintTaskRequest_Release( request );
    WindowsDeleteString( title );
    return S_OK;
}

static const struct __FITypedEventHandler_2_Windows__CGraphics__CPrinting__CPrintManager_Windows__CGraphics__CPrinting__CPrintTaskRequestedEventArgsVtbl
        handler_vtbl =
{
    handler_QueryInterface, handler_AddRef, handler_Release, handler_Invoke,
};

static PrintTaskRequestedEventHandler print_task_handler = { &handler_vtbl };

static void test_print_manager(void)
{
    IPrintManagerStatics2 *statics2;
    IPrintManagerStatics *statics;
    IPrintManager *manager;
    IActivationFactory *factory;
    EventRegistrationToken token;
    __FIAsyncOperation_1_boolean *op;
    IAsyncInfo *info;
    boolean supported;
    HRESULT hr;

    factory = get_factory();

    hr = IActivationFactory_QueryInterface( factory, &IID_IPrintManagerStatics, (void **)&statics );
    ok( SUCCEEDED( hr ), "Failed to get IPrintManagerStatics, hr %#lx\n", hr );

    /* GetForCurrentView */
    manager = NULL;
    hr = IPrintManagerStatics_GetForCurrentView( statics, &manager );
    ok( SUCCEEDED( hr ) && manager, "GetForCurrentView failed, hr %#lx\n", hr );

    hr = IPrintManager_QueryInterface( manager, &IID_IPrintManager, (void **)&manager );
    ok( SUCCEEDED( hr ), "Expected IPrintManager on the manager\n" );

    /* add_PrintTaskRequested / remove_PrintTaskRequested */
    token.value = 0;
    hr = IPrintManager_add_PrintTaskRequested( manager, &print_task_handler, &token );
    ok( SUCCEEDED( hr ) && token.value, "add_PrintTaskRequested failed, hr %#lx\n", hr );

    hr = IPrintManager_remove_PrintTaskRequested( manager, token );
    ok( SUCCEEDED( hr ), "remove_PrintTaskRequested failed, hr %#lx\n", hr );

    hr = IPrintManager_add_PrintTaskRequested( manager, &print_task_handler, &token );
    ok( SUCCEEDED( hr ) && token.value, "add_PrintTaskRequested failed, hr %#lx\n", hr );

    /* ShowPrintUIAsync raises PrintTaskRequested and returns a completed op. */
    source_handler_source = (IPrintDocumentSource *)create_source();
    op = NULL;
    hr = IPrintManagerStatics_ShowPrintUIAsync( statics, &op );
    ok( SUCCEEDED( hr ) && op, "ShowPrintUIAsync failed, hr %#lx\n", hr );
    ok( print_task_requested_calls == 1, "Expected 1 PrintTaskRequested call, got %d\n", print_task_requested_calls );
    ok( source_handler_calls == 1, "Expected 1 source-requested call, got %d\n", source_handler_calls );
    ok( created_task != NULL, "Expected a PrintTask\n" );

    hr = __FIAsyncOperation_1_boolean_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    ok( SUCCEEDED( hr ), "Expected IAsyncInfo on the op\n" );
    {
        enum AsyncStatus status;
        boolean result = FALSE;

        hr = IAsyncInfo_get_Status( info, &status );
        ok( SUCCEEDED( hr ) && status == Completed, "Expected Completed status, hr %#lx\n", hr );
        hr = __FIAsyncOperation_1_boolean_GetResults( op, &result );
        ok( SUCCEEDED( hr ) && result, "Expected TRUE result, hr %#lx\n", hr );
    }
    IAsyncInfo_Release( info );
    __FIAsyncOperation_1_boolean_Release( op );

    /* IsSupported */
    statics2 = NULL;
    hr = IActivationFactory_QueryInterface( factory, &IID_IPrintManagerStatics2, (void **)&statics2 );
    ok( SUCCEEDED( hr ), "Failed to get IPrintManagerStatics2, hr %#lx\n", hr );
    supported = FALSE;
    hr = IPrintManagerStatics2_get_IsSupported( statics2, &supported );
    ok( SUCCEEDED( hr ) && supported, "Expected IsSupported TRUE, hr %#lx\n", hr );
    IPrintManagerStatics2_Release( statics2 );

    /* A manager from a second GetForCurrentView answers the next ShowPrintUIAsync. */
    IPrintManager_Release( manager );
    manager = NULL;
    hr = IPrintManagerStatics_GetForCurrentView( statics, &manager );
    ok( SUCCEEDED( hr ) && manager, "Second GetForCurrentView failed, hr %#lx\n", hr );

    IPrintManager_Release( manager );
    IPrintManagerStatics_Release( statics );
    IActivationFactory_Release( factory );
}

START_TEST( printing )
{
    HMODULE module;

    if (!(module = LoadLibraryW( L"windows.graphics.printing.dll" )))
    {
        win_skip( "Failed to load windows.graphics.printing.dll, error %#lx\n", GetLastError() );
        return;
    }

    if (!(pGetActivationFactory = (void *)GetProcAddress( module, "DllGetActivationFactory" )))
    {
        win_skip( "DllGetActivationFactory not exported\n" );
        return;
    }

    test_print_manager();
}
