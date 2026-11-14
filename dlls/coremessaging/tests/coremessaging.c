/*
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
#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_System
#include "windows.system.h"

#include "dispatcherqueue.h"

#include "wine/test.h"

HRESULT WINAPI CoreMsgCreateSession( ULONG flags, void **session );
HRESULT WINAPI CoreMsgGetExistingSession( void **session );
HRESULT WINAPI CoreUICallCreateEndpointHost( void *session, void **send_out, void **recv_out );
HRESULT WINAPI CoreUICallCreateConversationHost( void *session, IUnknown *peer, void **send_out, void **recv_out );
HRESULT WINAPI CoreUICallComputeMaximumMessageSize( void *host, const unsigned char **type_defs, ULONG count,
                                                    ULONG max_string, ULONG max_array, ULONG max_blob, ULONG *out );
HRESULT WINAPI CoreUICallGetAddressOfParameterInBuffer( const void *msg, ULONG offset, ULONG index, void **out );
HRESULT WINAPI CoreUICallSendVaList( void *host, ULONG_PTR conn, ULONG flag, USHORT verb, USHORT count,
                                     const unsigned char *type_def, void *args );

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )
static void check_interface_( unsigned int line, void *obj, const IID *iid )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    IUnknown_Release( unk );
}

struct typed_event_handler_dispatcher_queue
{
    ITypedEventHandler_DispatcherQueue_IInspectable ITypedEventHandler_DispatcherQueue_IInspectable_iface;
    LONG ref;

    HANDLE event;
};

static struct typed_event_handler_dispatcher_queue *impl_from_ITypedEventHandler_DispatcherQueue_IInspectable( ITypedEventHandler_DispatcherQueue_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct typed_event_handler_dispatcher_queue, ITypedEventHandler_DispatcherQueue_IInspectable_iface );
}

static HRESULT WINAPI typed_event_handler_dispatcher_queue_QueryInterface( ITypedEventHandler_DispatcherQueue_IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_ITypedEventHandler_DispatcherQueue_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = iface;
        ITypedEventHandler_DispatcherQueue_IInspectable_AddRef( iface );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI typed_event_handler_dispatcher_queue_AddRef( ITypedEventHandler_DispatcherQueue_IInspectable *iface )
{
    struct typed_event_handler_dispatcher_queue *handler = impl_from_ITypedEventHandler_DispatcherQueue_IInspectable( iface );
    return InterlockedIncrement( &handler->ref );
}

static ULONG WINAPI typed_event_handler_dispatcher_queue_Release( ITypedEventHandler_DispatcherQueue_IInspectable *iface )
{
    struct typed_event_handler_dispatcher_queue *handler = impl_from_ITypedEventHandler_DispatcherQueue_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &handler->ref );

    if (!ref)
    {
        CloseHandle( handler->event );
        free( handler );
    }

    return ref;
}

static HRESULT WINAPI typed_event_handler_dispatcher_queue_Invoke( ITypedEventHandler_DispatcherQueue_IInspectable *iface, IDispatcherQueue *queue, IInspectable *inspectable )
{
    struct typed_event_handler_dispatcher_queue *handler = impl_from_ITypedEventHandler_DispatcherQueue_IInspectable( iface );

    SetEvent( handler->event );
    return S_OK;
}

static const ITypedEventHandler_DispatcherQueue_IInspectableVtbl typed_event_handler_dispatcher_queue_vtbl =
{
    typed_event_handler_dispatcher_queue_QueryInterface,
    typed_event_handler_dispatcher_queue_AddRef,
    typed_event_handler_dispatcher_queue_Release,
    typed_event_handler_dispatcher_queue_Invoke,
};

static HRESULT create_typed_event_handler_dispatcher_queue( ITypedEventHandler_DispatcherQueue_IInspectable **handler )
{
    struct typed_event_handler_dispatcher_queue *impl;

    *handler = NULL;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;

    impl->ITypedEventHandler_DispatcherQueue_IInspectable_iface.lpVtbl = &typed_event_handler_dispatcher_queue_vtbl;
    impl->ref = 1;
    impl->event = CreateEventW( NULL, TRUE, FALSE, NULL );

    *handler = &impl->ITypedEventHandler_DispatcherQueue_IInspectable_iface;
    return S_OK;
}

struct dispatcher_queue_handler
{
    IDispatcherQueueHandler IDispatcherQueueHandler_iface;
    LONG ref;

    HANDLE event;
};

static struct dispatcher_queue_handler *impl_from_IDispatcherQueueHandler( IDispatcherQueueHandler *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue_handler, IDispatcherQueueHandler_iface );
}

static HRESULT WINAPI dispatcher_queue_handler_QueryInterface( IDispatcherQueueHandler *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IDispatcherQueueHandler ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = iface;
        IDispatcherQueueHandler_AddRef( iface );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dispatcher_queue_handler_AddRef( IDispatcherQueueHandler *iface )
{
    struct dispatcher_queue_handler *handler = impl_from_IDispatcherQueueHandler( iface );
    return InterlockedIncrement( &handler->ref );
}

static ULONG WINAPI dispatcher_queue_handler_Release( IDispatcherQueueHandler *iface )
{
    struct dispatcher_queue_handler *handler = impl_from_IDispatcherQueueHandler( iface );
    ULONG ref = InterlockedDecrement( &handler->ref );

    if (!ref)
    {
        CloseHandle( handler->event );
        free( handler );
    }

    return ref;
}

static HRESULT WINAPI dispatcher_queue_handler_Invoke( IDispatcherQueueHandler *iface )
{
    struct dispatcher_queue_handler *handler = impl_from_IDispatcherQueueHandler( iface );
    DWORD ret;

    ret = WaitForSingleObject( handler->event, 5000 );
    ok( !ret, "Unexpected wait result %lu.\n", ret );

    return S_OK;
}

static const IDispatcherQueueHandlerVtbl dispatcher_queue_handler_vtbl =
{
    dispatcher_queue_handler_QueryInterface,
    dispatcher_queue_handler_AddRef,
    dispatcher_queue_handler_Release,
    dispatcher_queue_handler_Invoke,
};

static HRESULT create_dispatcher_queue_handler( IDispatcherQueueHandler **handler )
{
    struct dispatcher_queue_handler *impl;

    *handler = NULL;

    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;

    impl->IDispatcherQueueHandler_iface.lpVtbl = &dispatcher_queue_handler_vtbl;
    impl->ref = 1;
    impl->event = CreateEventW( NULL, TRUE, FALSE, NULL );

    *handler = &impl->IDispatcherQueueHandler_iface;
    return S_OK;
}

#define wait_messages( a ) msg_wait_for_events_( __FILE__, __LINE__, 0, NULL, a )
#define msg_wait_for_events( a, b, c ) msg_wait_for_events_( __FILE__, __LINE__, a, b, c )
static DWORD msg_wait_for_events_( const char *file, int line, DWORD count, HANDLE *events, DWORD timeout )
{
    DWORD ret, end = GetTickCount() + min( timeout, 5000 );
    MSG msg;

    while ((ret = MsgWaitForMultipleObjects( count, events, FALSE, min( timeout, 5000 ), QS_ALLINPUT )) <= count)
    {
        while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        if (ret < count) return ret;
        if (timeout >= 5000) continue;
        if (end <= GetTickCount()) timeout = 0;
        else timeout = end - GetTickCount();
    }

    if (timeout >= 5000) ok_(file, line)( 0, "MsgWaitForMultipleObjects returned %#lx\n", ret );
    else ok_(file, line)( ret == WAIT_TIMEOUT, "MsgWaitForMultipleObjects returned %#lx\n", ret );
    return ret;
}

#define check_create_dispatcher_queue_controller( size, thread_type, apartment_type, expected_hr ) \
        check_create_dispatcher_queue_controller_( __LINE__, size, thread_type, apartment_type, expected_hr )
static void check_create_dispatcher_queue_controller_( unsigned int line, DWORD size, DISPATCHERQUEUE_THREAD_TYPE thread_type,
                                                       DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartment_type, HRESULT expected_hr )
{
    ITypedEventHandler_DispatcherQueue_IInspectable *event_handler_iface = NULL;
    struct typed_event_handler_dispatcher_queue *event_handler = NULL;
    IDispatcherQueueController *dispatcher_queue_controller = NULL;
    struct dispatcher_queue_handler *queue_handler = NULL;
    IDispatcherQueueHandler *handler_iface = NULL;
    struct DispatcherQueueOptions options = { 0 };
    IDispatcherQueue2 *dispatcher_queue2 = NULL;
    IDispatcherQueue *dispatcher_queue = NULL;
    IAsyncAction *operation = NULL;
    IAsyncInfo *async_info = NULL;
    EventRegistrationToken token;
    AsyncStatus status;
    boolean result;
    HRESULT hr;
    DWORD ret;
    LONG ref;

    options.dwSize = size;
    options.threadType = thread_type;
    options.apartmentType = apartment_type;

    hr = CreateDispatcherQueueController( options, &dispatcher_queue_controller );
    ok_(__FILE__, line)( hr == expected_hr, "got CreateDispatcherQueueController hr %#lx.\n", hr );
    if (hr == E_INVALIDARG) return;

    hr = IDispatcherQueueController_get_DispatcherQueue( dispatcher_queue_controller, &dispatcher_queue );
    todo_wine
    ok_(__FILE__, line)( hr == S_OK, "got IDispatcherQueueController_get_DispatcherQueue hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;

    hr = IDispatcherQueue_QueryInterface( dispatcher_queue, &IID_IDispatcherQueue2, (void **)&dispatcher_queue2 );
    ok_(__FILE__, line)( hr == S_OK || broken(hr == E_NOINTERFACE) /* w1064v1809 */, "got IDispatcherQueue_QueryInterface hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IDispatcherQueue2_get_HasThreadAccess( dispatcher_queue2, &result );
        ok_(__FILE__, line)( hr == S_OK, "got IDispatcherQueue2_get_HasThreadAccess hr %#lx.\n", hr );
        ok_(__FILE__, line)( result == (thread_type == DQTYPE_THREAD_CURRENT ? TRUE : FALSE), "got IDispatcherQueue2_get_HasThreadAccess result %d.\n", result );
        ref = IDispatcherQueue2_Release( dispatcher_queue2 );
        ok_(__FILE__, line)( ref == (thread_type == DQTYPE_THREAD_CURRENT ? 2 : 3), "got IDispatcherQueue2_Release ref %ld.\n", ref );
    }

    hr = create_dispatcher_queue_handler( &handler_iface );
    ok_(__FILE__, line)( hr == S_OK, "create_dispatcher_queue_handler failed, hr %#lx.\n", hr );
    queue_handler = impl_from_IDispatcherQueueHandler( handler_iface );

    hr = IDispatcherQueue_TryEnqueue( dispatcher_queue, handler_iface, &result );
    ok_(__FILE__, line)( hr == S_OK, "got IDispatcherQueue_TryEnqueue hr %#lx.\n", hr );
    ok_(__FILE__, line)( result == TRUE, "got IDispatcherQueue_TryEnqueue result %d.\n", result );

    hr = create_typed_event_handler_dispatcher_queue( &event_handler_iface );
    ok_(__FILE__, line)( hr == S_OK, "create_typed_event_handler_dispatcher_queue failed, hr %#lx.\n", hr );
    event_handler = impl_from_ITypedEventHandler_DispatcherQueue_IInspectable( event_handler_iface );

    hr = IDispatcherQueue_add_ShutdownCompleted( dispatcher_queue, event_handler_iface, &token );
    ok_(__FILE__, line)( hr == S_OK, "got IDispatcherQueue_add_ShutdownCompleted hr %#lx.\n", hr );
    hr = IDispatcherQueueController_ShutdownQueueAsync( dispatcher_queue_controller, &operation );
    ok_(__FILE__, line)( hr == S_OK, "got IDispatcherQueueController_ShutdownQueueAsync hr %#lx.\n", hr );

    hr = IAsyncAction_QueryInterface( operation, &IID_IAsyncInfo, (void **)&async_info );
    ok_(__FILE__, line)( hr == S_OK, "got IAsyncAction_QueryInterface hr %#lx.\n", hr );

    hr = IAsyncInfo_get_Status( async_info, &status );
    ok_(__FILE__, line)( hr == S_OK, "got IAsyncInfo_get_Status hr %#lx.\n", hr );
    ok_(__FILE__, line)( status == Started, "got IAsyncInfo_get_Status status %d.\n", status );

    /* shutdown waits for queued handlers */
    if (winetest_platform_is_wine) Sleep( 200 );
    ret = WaitForSingleObject( event_handler->event, 100 );
    todo_wine ok_(__FILE__, line)( ret == WAIT_TIMEOUT, "Unexpected wait result %lu.\n", ret );
    SetEvent( queue_handler->event );

    /* queue uses the message loop when dispatched on current thread */
    if (thread_type == DQTYPE_THREAD_CURRENT)
    {
        ret = WaitForSingleObject( event_handler->event, 100 );
        ok_(__FILE__, line)( ret == WAIT_TIMEOUT, "Unexpected wait result %lu.\n", ret );
        ret = msg_wait_for_events( 1, &event_handler->event, 5000 );
        ok_(__FILE__, line)( !ret, "Unexpected wait result %lu.\n", ret );
    }
    else
    {
        ret = WaitForSingleObject( event_handler->event, 5000 );
        ok_(__FILE__, line)( !ret, "Unexpected wait result %lu.\n", ret );
    }

    hr = IAsyncInfo_get_Status( async_info, &status );
    ok_(__FILE__, line)( hr == S_OK, "got IAsyncInfo_get_Status hr %#lx.\n", hr );
    ok_(__FILE__, line)( status == Completed, "got IAsyncInfo_get_Status status %d.\n", status );

    hr = IAsyncInfo_Close( async_info );
    ok_(__FILE__, line)( hr == S_OK, "got IAsyncInfo_Close hr %#lx.\n", hr );
    ref = IAsyncInfo_Release( async_info );
    ok_(__FILE__, line)( ref == 2, "got IAsyncInfo_Release ref %ld.\n", ref );
    ref = IAsyncAction_Release( operation );
    ok_(__FILE__, line)( ref == 1, "got IAsyncAction_Release ref %ld.\n", ref );
    hr = IDispatcherQueue_remove_ShutdownCompleted( dispatcher_queue, token );
    ok_(__FILE__, line)( hr == S_OK, "got IDispatcherQueue_remove_ShutdownCompleted hr %#lx.\n", hr );

    ref = ITypedEventHandler_DispatcherQueue_IInspectable_Release( event_handler_iface );
    ok_(__FILE__, line)( ref == 0, "got ITypedEventHandler_DispatcherQueue_IInspectable_Release ref %ld.\n", ref );
    ref = IDispatcherQueueHandler_Release( handler_iface );
    ok_(__FILE__, line)( ref == 0, "got IDispatcherQueueHandler_Release ref %ld.\n", ref );
    IDispatcherQueue_Release( dispatcher_queue );
done:
    IDispatcherQueueController_Release( dispatcher_queue_controller );
}

static void test_CreateDispatcherQueueController(void)
{
    IDispatcherQueueController *dispatcher_queue_controller = (void *)0xdeadbeef;
    struct DispatcherQueueOptions options = { 0 };
    HRESULT hr;

    hr = CreateDispatcherQueueController( options, NULL );
    ok( hr == E_POINTER || hr == 0x80000005 /* win10 22h2 */, "got hr %#lx.\n", hr );
    hr = CreateDispatcherQueueController( options, &dispatcher_queue_controller );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    ok( dispatcher_queue_controller == (void *)0xdeadbeef, "got dispatcher_queue_controller %p.\n", dispatcher_queue_controller );

    /* Invalid args */

    check_create_dispatcher_queue_controller( 0,                                    0,                       DQTAT_COM_NONE, E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    0,                       DQTAT_COM_ASTA, E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    0,                       DQTAT_COM_STA,  E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    DQTYPE_THREAD_CURRENT,   DQTAT_COM_NONE, E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    DQTYPE_THREAD_CURRENT,   DQTAT_COM_ASTA, E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    DQTYPE_THREAD_CURRENT,   DQTAT_COM_STA,  E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE, E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    DQTYPE_THREAD_DEDICATED, DQTAT_COM_ASTA, E_INVALIDARG );
    check_create_dispatcher_queue_controller( 0,                                    DQTYPE_THREAD_DEDICATED, DQTAT_COM_STA,  E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ),     0,                       DQTAT_COM_NONE, E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ),     0,                       DQTAT_COM_ASTA, E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ),     0,                       DQTAT_COM_STA,  E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ),     0xdeadbeef,              DQTAT_COM_NONE, E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ),     0xdeadbeef,              DQTAT_COM_ASTA, E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ),     0xdeadbeef,              DQTAT_COM_STA,  E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ) - 1, DQTYPE_THREAD_CURRENT,   DQTAT_COM_NONE, E_INVALIDARG );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ) + 1, DQTYPE_THREAD_CURRENT,   DQTAT_COM_NONE, E_INVALIDARG );

    if (0) /* Silently crashes in Windows */
    {
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_DEDICATED, 0xdeadbeef,     S_OK );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE, S_OK );
    }

    /* Valid args */

    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_CURRENT,   0xdeadbeef,     S_OK );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_CURRENT,   DQTAT_COM_NONE, S_OK );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_CURRENT,   DQTAT_COM_ASTA, S_OK );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_CURRENT,   DQTAT_COM_STA,  S_OK );

    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_DEDICATED, DQTAT_COM_ASTA, S_OK );
    check_create_dispatcher_queue_controller( sizeof( DispatcherQueueOptions ), DQTYPE_THREAD_DEDICATED, DQTAT_COM_STA,  S_OK );
}

static void test_DispatcherQueueController_Statics(void)
{
    static const WCHAR *dispatcher_queue_controller_statics_name = L"Windows.System.DispatcherQueueController";
    IDispatcherQueueControllerStatics *dispatcher_queue_controller_statics = (void *)0xdeadbeef;
    ITypedEventHandler_DispatcherQueue_IInspectable *event_handler_iface = (void *)0xdeadbeef;
    struct typed_event_handler_dispatcher_queue *event_handler = (void *)0xdeadbeef;
    IDispatcherQueueController *dispatcher_queue_controller = (void *)0xdeadbeef;
    struct dispatcher_queue_handler *queue_handler = (void *)0xdeadbeef;
    IDispatcherQueueHandler *handler_iface = (void *)0xdeadbeef;
    IDispatcherQueue2 *dispatcher_queue2 = (void *)0xdeadbeef;
    IDispatcherQueue *dispatcher_queue = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IAsyncAction *operation = (void *)0xdeadbeef;
    IAsyncInfo *async_info = (void *)0xdeadbeef;
    EventRegistrationToken token;
    AsyncStatus status;
    HSTRING str = NULL;
    boolean result;
    HRESULT hr;
    DWORD ret;
    LONG ref;

    hr = WindowsCreateString( dispatcher_queue_controller_statics_name, wcslen( dispatcher_queue_controller_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( dispatcher_queue_controller_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IDispatcherQueueControllerStatics, (void **)&dispatcher_queue_controller_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IDispatcherQueueControllerStatics_CreateOnDedicatedThread( dispatcher_queue_controller_statics, NULL );
    todo_wine
    ok( hr == E_POINTER || hr == 0x80000005 /* win10 22h2 */, "got hr %#lx.\n", hr );
    hr = IDispatcherQueueControllerStatics_CreateOnDedicatedThread( dispatcher_queue_controller_statics, &dispatcher_queue_controller );
    todo_wine
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;

    hr = IDispatcherQueueController_get_DispatcherQueue( dispatcher_queue_controller, NULL );
    todo_wine
    ok( hr == E_POINTER || hr == 0x80000005 /* win10 22h2 */, "got hr %#lx.\n", hr );
    hr = IDispatcherQueueController_get_DispatcherQueue( dispatcher_queue_controller, &dispatcher_queue );
    todo_wine
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( dispatcher_queue, &IID_IUnknown );
    check_interface( dispatcher_queue, &IID_IInspectable );
    check_interface( dispatcher_queue, &IID_IAgileObject );

    hr = create_dispatcher_queue_handler( &handler_iface );
    ok( hr == S_OK, "Unexpected hr %#lx.\n", hr );
    queue_handler = impl_from_IDispatcherQueueHandler( handler_iface );

    hr = IDispatcherQueue_TryEnqueue( dispatcher_queue, handler_iface, &result );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( result == TRUE, "got result %d.\n", result );

    hr = IDispatcherQueue_QueryInterface( dispatcher_queue, &IID_IDispatcherQueue2, (void **)&dispatcher_queue2 );
    ok( hr == S_OK || broken(hr == E_NOINTERFACE) /* w1064v1809 */, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IDispatcherQueue2_get_HasThreadAccess( dispatcher_queue2, &result );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( result == FALSE, "got result %d.\n", result );
        ref = IDispatcherQueue2_Release( dispatcher_queue2 );
        ok( ref == 3, "got ref %ld.\n", ref );
    }

    hr = create_typed_event_handler_dispatcher_queue( &event_handler_iface );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    event_handler = impl_from_ITypedEventHandler_DispatcherQueue_IInspectable( event_handler_iface );
    hr = IDispatcherQueue_add_ShutdownCompleted( dispatcher_queue, event_handler_iface, &token );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IDispatcherQueueController_ShutdownQueueAsync( dispatcher_queue_controller, NULL );
    ok( hr == E_POINTER || hr == 0x80000005 /* win10 22h2 */, "got hr %#lx.\n", hr );
    hr = IDispatcherQueueController_ShutdownQueueAsync( dispatcher_queue_controller, &operation );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( operation, &IID_IInspectable );
    check_interface( operation, &IID_IAgileObject );
    check_interface( operation, &IID_IAsyncAction );

    hr = IAsyncAction_QueryInterface( operation, &IID_IAsyncInfo, (void **)&async_info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IAsyncInfo_get_Status( async_info, &status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( status == Started, "got status %d.\n", status );

    /* shutdown waits for queued handlers */
    if (winetest_platform_is_wine) Sleep( 200 );
    ret = WaitForSingleObject( event_handler->event, 100 );
    todo_wine ok( ret == WAIT_TIMEOUT, "Unexpected wait result %lu.\n", ret );

    SetEvent( queue_handler->event );
    ret = WaitForSingleObject( event_handler->event, 5000 );
    ok( !ret, "Unexpected wait result %lu.\n", ret );

    hr = IAsyncInfo_get_Status( async_info, &status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( status == Completed, "got status %d.\n", status );

    hr = IAsyncInfo_Close( async_info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IAsyncInfo_Release( async_info );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IAsyncAction_Release( operation );
    ok( ref == 1, "got ref %ld.\n", ref );
    hr = IDispatcherQueue_remove_ShutdownCompleted( dispatcher_queue, token );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IDispatcherQueue_TryEnqueue( dispatcher_queue, handler_iface, &result );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( result == FALSE, "got result %d.\n", result );

    ref = ITypedEventHandler_DispatcherQueue_IInspectable_Release( event_handler_iface );
    ok( ref == 0, "got ref %ld.\n", ref );
    ref = IDispatcherQueueHandler_Release( handler_iface );
    ok( ref == 0, "got ref %ld.\n", ref );
    IDispatcherQueue_Release( dispatcher_queue );
    IDispatcherQueueController_Release( dispatcher_queue_controller );
done:
    ref = IDispatcherQueueControllerStatics_Release( dispatcher_queue_controller_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_coreuicall_session( void )
{
    void *session = NULL, *session2 = NULL;
    HRESULT hr;

    hr = CoreMsgCreateSession( 0, &session );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !!session, "got NULL session.\n" );

    hr = CoreMsgCreateSession( 4, &session2 );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    ok( !session2, "got non-NULL session.\n" );

    hr = CoreMsgCreateSession( 0, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    hr = CoreMsgGetExistingSession( &session2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !!session2, "got NULL session.\n" );
    IUnknown_Release( (IUnknown *)session2 );
    IUnknown_Release( (IUnknown *)session );
}

static void test_coreuicall_conversation( void )
{
    static const unsigned char td_dword[] = {1, 3, 0};
    static const unsigned char td_byte_word[] = {2, 1, 2, 0};
    void *session = NULL, *send = NULL, *recv = NULL, *peer_send = NULL;
    const unsigned char *tds[] = {td_dword};
    const unsigned char *tds2[] = {td_byte_word};
    ULONG max = 0, val = 7;
    void *slots[] = {&val};
    void *addr = NULL;
    HRESULT hr;

    hr = CoreMsgCreateSession( 0, &session );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = CoreUICallCreateEndpointHost( session, &send, &recv );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !!send && !!recv, "got NULL host.\n" );

    hr = CoreUICallCreateEndpointHost( NULL, &send, &recv );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );

    hr = CoreUICallComputeMaximumMessageSize( send, tds, 1, 0, 0, 0, &max );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( max == 16, "got max %u.\n", max );

    hr = CoreUICallComputeMaximumMessageSize( send, tds2, 1, 0, 0, 0, &max );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( max == 24, "got max %u.\n", max );

    /* 8 byte header + [u32 size=4][u32] + [u32 size=2][u16][pad] */
    {
        unsigned char msg[8 + 4 + 4 + 4 + 4] = {0};
        *(ULONG *)(msg + 8) = 4;
        *(ULONG *)(msg + 12) = 0x11223344;
        *(ULONG *)(msg + 16) = 2;
        *(USHORT *)(msg + 20) = 0x5566;

        hr = CoreUICallGetAddressOfParameterInBuffer( msg, sizeof(msg), 0, &addr );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( addr == msg + 12, "got addr %p.\n", addr );
        hr = CoreUICallGetAddressOfParameterInBuffer( msg, sizeof(msg), 1, &addr );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( addr == msg + 20, "got addr %p.\n", addr );
        hr = CoreUICallGetAddressOfParameterInBuffer( msg, sizeof(msg), 2, &addr );
        ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    }

    /* a send over an endpoint host with no peer has no destination */
    hr = CoreUICallSendVaList( send, 0, 0, 1, 1, td_dword, slots );
    ok( hr == E_NOTIMPL, "got hr %#lx.\n", hr );

    /* A conversation send is delivered to the peer, which completes the
     * message in place: the call succeeds and the slot value round-trips.
     * (A negative completion here is fatal for real callers: the
     * CoreMessagingXP dispatch layer fail-fasts on it.) */
    hr = CoreUICallCreateConversationHost( session, (IUnknown *)recv, &peer_send, NULL );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !!peer_send, "got NULL host.\n" );
    hr = CoreUICallSendVaList( peer_send, 0, 0, 1, 1, td_dword, slots );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( val == 7, "got val %u.\n", val );

    hr = CoreUICallSendVaList( NULL, 0, 0, 1, 1, td_dword, slots );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );

    IUnknown_Release( (IUnknown *)peer_send );
    IUnknown_Release( (IUnknown *)recv );
    IUnknown_Release( (IUnknown *)send );
    IUnknown_Release( (IUnknown *)session );
}

/* ---------------- priority / timer / inspectable helpers ---------------- */

static char recorded[16];
static LONG recorded_count;
static DWORD recorded_tid;
static HANDLE recorded_event;

struct record_handler
{
    IDispatcherQueueHandler IDispatcherQueueHandler_iface;
    LONG ref;
    char tag;
};

static struct record_handler *impl_from_record_handler( IDispatcherQueueHandler *iface )
{
    return CONTAINING_RECORD( iface, struct record_handler, IDispatcherQueueHandler_iface );
}

static HRESULT WINAPI record_handler_QueryInterface( IDispatcherQueueHandler *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IDispatcherQueueHandler ) || IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IInspectable ) || IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = iface;
        IDispatcherQueueHandler_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI record_handler_AddRef( IDispatcherQueueHandler *iface )
{
    return InterlockedIncrement( &impl_from_record_handler( iface )->ref );
}
static ULONG WINAPI record_handler_Release( IDispatcherQueueHandler *iface )
{
    struct record_handler *handler = impl_from_record_handler( iface );
    return InterlockedDecrement( &handler->ref );
}
static HRESULT WINAPI record_handler_Invoke( IDispatcherQueueHandler *iface )
{
    struct record_handler *handler = impl_from_record_handler( iface );
    LONG i = InterlockedIncrement( &recorded_count ) - 1;

    if (i < (LONG)ARRAY_SIZE( recorded ) - 1) recorded[i] = handler->tag;
    recorded_tid = GetCurrentThreadId();
    SetEvent( recorded_event );
    return S_OK;
}
static const IDispatcherQueueHandlerVtbl record_handler_vtbl =
{
    record_handler_QueryInterface,
    record_handler_AddRef,
    record_handler_Release,
    record_handler_Invoke,
};

static HRESULT create_record_handler( char tag, IDispatcherQueueHandler **handler )
{
    struct record_handler *impl;

    *handler = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IDispatcherQueueHandler_iface.lpVtbl = &record_handler_vtbl;
    impl->ref = 1;
    impl->tag = tag;
    *handler = &impl->IDispatcherQueueHandler_iface;
    return S_OK;
}

struct tick_handler
{
    ITypedEventHandler_DispatcherQueueTimer_IInspectable iface;
    LONG ref;
    LONG ticks;
    DWORD tid;
    HANDLE event;
};

static struct tick_handler *impl_from_tick_handler( ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct tick_handler, iface );
}

static HRESULT WINAPI tick_handler_QueryInterface( ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface,
        REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_ITypedEventHandler_DispatcherQueueTimer_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IUnknown ))
    {
        *out = iface;
        ITypedEventHandler_DispatcherQueueTimer_IInspectable_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI tick_handler_AddRef( ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_tick_handler( iface )->ref );
}
static ULONG WINAPI tick_handler_Release( ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface )
{
    struct tick_handler *handler = impl_from_tick_handler( iface );
    ULONG ref = InterlockedDecrement( &handler->ref );

    if (!ref)
    {
        CloseHandle( handler->event );
        free( handler );
    }
    return ref;
}
static HRESULT WINAPI tick_handler_Invoke( ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface,
        IDispatcherQueueTimer *sender, IInspectable *args )
{
    struct tick_handler *handler = impl_from_tick_handler( iface );

    handler->tid = GetCurrentThreadId();
    InterlockedIncrement( &handler->ticks );
    SetEvent( handler->event );
    return S_OK;
}
static const ITypedEventHandler_DispatcherQueueTimer_IInspectableVtbl tick_handler_vtbl =
{
    tick_handler_QueryInterface,
    tick_handler_AddRef,
    tick_handler_Release,
    tick_handler_Invoke,
};

static HRESULT create_tick_handler( ITypedEventHandler_DispatcherQueueTimer_IInspectable **handler )
{
    struct tick_handler *impl;

    *handler = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->iface.lpVtbl = &tick_handler_vtbl;
    impl->ref = 1;
    impl->event = CreateEventW( NULL, FALSE, FALSE, NULL );
    *handler = &impl->iface;
    return S_OK;
}

static IDispatcherQueue *create_queue( IDispatcherQueueController **controller )
{
    struct DispatcherQueueOptions options = { sizeof(options), DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE };
    IDispatcherQueue *queue = NULL;
    HRESULT hr;

    hr = CreateDispatcherQueueController( options, controller );
    ok( hr == S_OK, "got CreateDispatcherQueueController hr %#lx.\n", hr );
    if (FAILED( hr )) return NULL;
    hr = IDispatcherQueueController_get_DispatcherQueue( *controller, &queue );
    ok( hr == S_OK, "got get_DispatcherQueue hr %#lx.\n", hr );
    return queue;
}

static void test_DispatcherQueue_priority( void )
{
    IDispatcherQueueHandler *gate = NULL, *low = NULL, *normal = NULL, *high = NULL;
    IDispatcherQueueController *controller = NULL;
    struct dispatcher_queue_handler *queue_handler;
    IDispatcherQueue *queue;
    boolean result;
    HRESULT hr;
    DWORD ret;
    unsigned int i;

    recorded_count = 0;
    recorded[0] = 0;
    recorded_tid = 0;
    recorded_event = CreateEventW( NULL, FALSE, FALSE, NULL );

    if (!(queue = create_queue( &controller ))) goto done;

    /* Block the pump, then queue one item per priority band. */
    hr = create_dispatcher_queue_handler( &gate );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    queue_handler = impl_from_IDispatcherQueueHandler( gate );
    hr = IDispatcherQueue_TryEnqueue( queue, gate, &result );
    ok( hr == S_OK && result, "got hr %#lx result %d.\n", hr, result );

    hr = create_record_handler( 'L', &low );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = create_record_handler( 'N', &normal );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = create_record_handler( 'H', &high );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IDispatcherQueue_TryEnqueueWithPriority( queue, DispatcherQueuePriority_Low, low, &result );
    ok( hr == S_OK && result, "got hr %#lx result %d.\n", hr, result );
    hr = IDispatcherQueue_TryEnqueueWithPriority( queue, DispatcherQueuePriority_Normal, normal, &result );
    ok( hr == S_OK && result, "got hr %#lx result %d.\n", hr, result );
    hr = IDispatcherQueue_TryEnqueueWithPriority( queue, DispatcherQueuePriority_High, high, &result );
    ok( hr == S_OK && result, "got hr %#lx result %d.\n", hr, result );

    SetEvent( queue_handler->event );

    /* One signal per dispatched item: the pump must run the three handlers in
     * High, Normal, Low order. */
    for (i = 0; i < 3; i++)
    {
        ret = WaitForSingleObject( recorded_event, 5000 );
        ok_( __FILE__, __LINE__ )( !ret, "got wait result %lu for item %u.\n", ret, i );
    }

    /* High dispatches before Normal, Normal before Low. */
    ok( !strcmp( recorded, "HNL" ), "got dispatch order \"%s\".\n", recorded );

    IDispatcherQueue_Release( queue );
    IDispatcherQueueController_Release( controller );
    IDispatcherQueueHandler_Release( gate );
    IDispatcherQueueHandler_Release( low );
    IDispatcherQueueHandler_Release( normal );
    IDispatcherQueueHandler_Release( high );
done:
    CloseHandle( recorded_event );
}

static void test_DispatcherQueueTimer( void )
{
    TimeSpan interval = { 30 * 10000 }; /* 30 ms, in 100 ns units */
    ITypedEventHandler_DispatcherQueueTimer_IInspectable *tick = NULL;
    IDispatcherQueueController *controller = NULL;
    IDispatcherQueueHandler *probe = NULL;
    struct tick_handler *handler;
    EventRegistrationToken token;
    IDispatcherQueueTimer *timer;
    IDispatcherQueue *queue;
    DWORD queue_tid;
    boolean result;
    HRESULT hr;
    DWORD ret;

    if (!(queue = create_queue( &controller ))) return;

    /* Learn the queue thread id from a handler running on it. */
    hr = create_record_handler( '#', &probe );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    recorded_event = CreateEventW( NULL, FALSE, FALSE, NULL );
    recorded_count = 0;
    hr = IDispatcherQueue_TryEnqueue( queue, probe, &result );
    ok( hr == S_OK && result, "got hr %#lx result %d.\n", hr, result );
    ret = WaitForSingleObject( recorded_event, 5000 );
    ok( !ret, "got wait result %lu.\n", ret );
    queue_tid = recorded_tid;

    hr = IDispatcherQueue_CreateTimer( queue, &timer );
    ok( hr == S_OK, "got CreateTimer hr %#lx.\n", hr );

    interval.Duration = 0;
    hr = IDispatcherQueueTimer_get_Interval( timer, &interval );
    ok( hr == S_OK, "got get_Interval hr %#lx.\n", hr );
    ok( interval.Duration == 0, "got interval %s.\n", wine_dbgstr_longlong( interval.Duration ) );
    result = TRUE;
    hr = IDispatcherQueueTimer_get_IsRunning( timer, &result );
    ok( hr == S_OK && !result, "got hr %#lx IsRunning %d.\n", hr, result );
    result = TRUE;
    hr = IDispatcherQueueTimer_get_IsRepeating( timer, &result );
    ok( hr == S_OK && !result, "got hr %#lx IsRepeating %d.\n", hr, result );

    interval.Duration = 30 * 10000;
    hr = IDispatcherQueueTimer_put_Interval( timer, interval );
    ok( hr == S_OK, "got put_Interval hr %#lx.\n", hr );
    interval.Duration = 0;
    hr = IDispatcherQueueTimer_get_Interval( timer, &interval );
    ok( hr == S_OK && interval.Duration == 30 * 10000, "got hr %#lx duration %s.\n", hr,
        wine_dbgstr_longlong( interval.Duration ) );

    hr = create_tick_handler( &tick );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    handler = impl_from_tick_handler( tick );

    hr = IDispatcherQueueTimer_add_Tick( timer, tick, &token );
    ok( hr == S_OK, "got add_Tick hr %#lx.\n", hr );

    hr = IDispatcherQueueTimer_Start( timer );
    ok( hr == S_OK, "got Start hr %#lx.\n", hr );
    result = FALSE;
    hr = IDispatcherQueueTimer_get_IsRunning( timer, &result );
    ok( hr == S_OK && result, "got hr %#lx IsRunning %d.\n", hr, result );

    ret = WaitForSingleObject( handler->event, 5000 );
    ok( !ret, "got tick wait result %lu.\n", ret );
    ok( handler->tid == queue_tid, "got tick tid %04x, queue tid %04x.\n", handler->tid, queue_tid );

    /* Non-repeating: a single tick, then IsRunning drops back to FALSE. */
    hr = IDispatcherQueueTimer_get_IsRunning( timer, &result );
    ok( hr == S_OK && !result, "got hr %#lx IsRunning %d.\n", hr, result );

    /* Start on a stopped timer restarts the countdown. */
    hr = IDispatcherQueueTimer_Start( timer );
    ok( hr == S_OK, "got Start hr %#lx.\n", hr );
    ret = WaitForSingleObject( handler->event, 5000 );
    ok( !ret, "got second tick wait result %lu.\n", ret );

    hr = IDispatcherQueueTimer_put_IsRepeating( timer, TRUE );
    ok( hr == S_OK, "got put_IsRepeating hr %#lx.\n", hr );
    hr = IDispatcherQueueTimer_Start( timer );
    ok( hr == S_OK, "got Start hr %#lx.\n", hr );
    ret = WaitForSingleObject( handler->event, 5000 );
    ok( !ret, "got repeating tick wait result %lu.\n", ret );
    hr = IDispatcherQueueTimer_get_IsRunning( timer, &result );
    ok( hr == S_OK && result, "got hr %#lx IsRunning %d.\n", hr, result );

    hr = IDispatcherQueueTimer_Stop( timer );
    ok( hr == S_OK, "got Stop hr %#lx.\n", hr );
    result = TRUE;
    hr = IDispatcherQueueTimer_get_IsRunning( timer, &result );
    ok( hr == S_OK && !result, "got hr %#lx IsRunning %d.\n", hr, result );

    hr = IDispatcherQueueTimer_remove_Tick( timer, token );
    ok( hr == S_OK, "got remove_Tick hr %#lx.\n", hr );
    hr = IDispatcherQueueTimer_remove_Tick( timer, token );
    ok( hr == E_INVALIDARG, "got remove_Tick hr %#lx.\n", hr );

    hr = IDispatcherQueueTimer_Start( timer );
    ok( hr == S_OK, "got Start hr %#lx.\n", hr );
    hr = IDispatcherQueueTimer_Stop( timer );
    ok( hr == S_OK, "got Stop hr %#lx.\n", hr );

    IDispatcherQueueTimer_Release( timer );
    ITypedEventHandler_DispatcherQueueTimer_IInspectable_Release( tick );
    IDispatcherQueueHandler_Release( probe );
    CloseHandle( recorded_event );
    IDispatcherQueue_Release( queue );
    IDispatcherQueueController_Release( controller );
}

static void check_runtime_class( unsigned int line, IInspectable *inspectable, const WCHAR *name, ULONG expected_min_iids )
{
    IID *iids = NULL;
    ULONG count = 0;
    TrustLevel level;
    HSTRING str = NULL;
    HRESULT hr;

    hr = IInspectable_GetRuntimeClassName( inspectable, &str );
    ok_( __FILE__, line )( hr == S_OK, "got GetRuntimeClassName hr %#lx.\n", hr );
    if (SUCCEEDED( hr ))
    {
        ok_( __FILE__, line )( !wcscmp( WindowsGetStringRawBuffer( str, NULL ), name ),
                              "got runtime class name %s.\n", debugstr_w( WindowsGetStringRawBuffer( str, NULL ) ) );
        WindowsDeleteString( str );
    }

    hr = IInspectable_GetIids( inspectable, &count, &iids );
    ok_( __FILE__, line )( hr == S_OK, "got GetIids hr %#lx.\n", hr );
    ok_( __FILE__, line )( count >= expected_min_iids, "got iid count %lu.\n", count );
    CoTaskMemFree( iids );

    hr = IInspectable_GetTrustLevel( inspectable, &level );
    ok_( __FILE__, line )( hr == S_OK, "got GetTrustLevel hr %#lx.\n", hr );
}
#define check_runtime_class( a, b, c ) check_runtime_class( __LINE__, a, b, c )

static void test_DispatcherQueue_inspectable( void )
{
    static const WCHAR queue_name[] = L"Windows.System.DispatcherQueue";
    static const WCHAR controller_name[] = L"Windows.System.DispatcherQueueController";
    static const WCHAR timer_name[] = L"Windows.System.DispatcherQueueTimer";
    IDispatcherQueueController *controller = NULL;
    IDispatcherQueueTimer *timer = NULL;
    IActivationFactory *factory = NULL;
    IDispatcherQueue *queue;
    HSTRING str = NULL;
    HRESULT hr;

    if (!(queue = create_queue( &controller ))) return;

    hr = IDispatcherQueue_CreateTimer( queue, &timer );
    ok( hr == S_OK, "got CreateTimer hr %#lx.\n", hr );

    check_runtime_class( (IInspectable *)queue, queue_name, 1 );
    check_runtime_class( (IInspectable *)controller, controller_name, 1 );
    check_runtime_class( (IInspectable *)timer, timer_name, 1 );

    hr = WindowsCreateString( queue_name, ARRAY_SIZE( queue_name ) - 1, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (SUCCEEDED( hr ))
    {
        check_runtime_class( (IInspectable *)factory, queue_name, 0 );
        IActivationFactory_Release( factory );
    }

    IDispatcherQueueTimer_Release( timer );
    IDispatcherQueue_Release( queue );
    IDispatcherQueueController_Release( controller );
}

START_TEST(coremessaging)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_CreateDispatcherQueueController();
    test_DispatcherQueueController_Statics();
    test_DispatcherQueue_priority();
    test_DispatcherQueueTimer();
    test_DispatcherQueue_inspectable();
    test_coreuicall_session();
    test_coreuicall_conversation();

    RoUninitialize();
}
