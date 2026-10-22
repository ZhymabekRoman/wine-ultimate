/*
 * Copyright 2026 the Wine authors
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

#include <initguid.h>
#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(threading);

/* 100 ns ticks between 1601-01-01 and 1970-01-01, and per second. */
#define FILETIME_UNIX_EPOCH 116444736000000000LL
#define TICKS_PER_SECOND      10000000LL
#define TICKS_PER_MS              10000LL

static const WCHAR class_name[] = L"Windows.System.Threading.ThreadPoolTimer";

/* Windows.Foundation.IAsyncAction, passed to the WorkItemHandler when the timer fires. */
typedef enum
{
    ASYNC_STATUS_STARTED = 0,
    ASYNC_STATUS_COMPLETED = 1,
    ASYNC_STATUS_CANCELED = 2,
    ASYNC_STATUS_ERROR = 3
} AsyncStatus;

/* Windows.Foundation.IAsyncInfo: get_Id, get_Status, get_ErrorCode, Close.
 * Windows.Foundation.IAsyncAction: GetResults. */
typedef struct IAsyncAction IAsyncAction;
typedef struct IAsyncActionVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IAsyncAction *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IAsyncAction *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IAsyncAction *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IAsyncAction *iface, ULONG *count, GUID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IAsyncAction *iface, HSTRING *name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IAsyncAction *iface, TrustLevel *level );
    HRESULT (STDMETHODCALLTYPE *get_Id)( IAsyncAction *iface, UINT32 *value );
    HRESULT (STDMETHODCALLTYPE *get_Status)( IAsyncAction *iface, AsyncStatus *value );
    HRESULT (STDMETHODCALLTYPE *get_ErrorCode)( IAsyncAction *iface, HRESULT *value );
    HRESULT (STDMETHODCALLTYPE *Close)( IAsyncAction *iface );
    HRESULT (STDMETHODCALLTYPE *GetResults)( IAsyncAction *iface );
} IAsyncActionVtbl;

struct IAsyncAction
{
    const struct IAsyncActionVtbl *lpVtbl;
};

struct async_action
{
    IAsyncAction IAsyncAction_iface;
    LONG ref;
    AsyncStatus status;
};

static inline struct async_action *impl_from_IAsyncAction( IAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct async_action, IAsyncAction_iface );
}

static const IID IID_IAsyncAction = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static HRESULT WINAPI async_action_QueryInterface( IAsyncAction *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not supported, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_action_AddRef( IAsyncAction *iface )
{
    return InterlockedIncrement( &impl_from_IAsyncAction( iface )->ref );
}

static ULONG WINAPI async_action_Release( IAsyncAction *iface )
{
    struct async_action *impl = impl_from_IAsyncAction( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "Destroying async action %p.\n", impl );
        HeapFree( GetProcessHeap(), 0, impl );
    }
    return ref;
}

static HRESULT WINAPI async_action_GetIids( IAsyncAction *iface, ULONG *count, GUID **iids )
{
    FIXME( "iface %p, count %p, iids %p, stub.\n", iface, count, iids );
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI async_action_GetRuntimeClassName( IAsyncAction *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncAction",
            lstrlenW( L"Windows.Foundation.IAsyncAction" ), name );
}

static HRESULT WINAPI async_action_GetTrustLevel( IAsyncAction *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_action_get_Id( IAsyncAction *iface, UINT32 *value )
{
    *value = 1;
    return S_OK;
}

static HRESULT WINAPI async_action_get_Status( IAsyncAction *iface, AsyncStatus *value )
{
    struct async_action *impl = impl_from_IAsyncAction( iface );

    *value = impl->status;
    return S_OK;
}

static HRESULT WINAPI async_action_get_ErrorCode( IAsyncAction *iface, HRESULT *value )
{
    *value = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_action_Close( IAsyncAction *iface )
{
    return S_OK;
}

static HRESULT WINAPI async_action_GetResults( IAsyncAction *iface )
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
    async_action_get_Id,
    async_action_get_Status,
    async_action_get_ErrorCode,
    async_action_Close,
    async_action_GetResults,
};

static struct async_action *create_async_action( AsyncStatus status )
{
    struct async_action *action;

    if (!(action = HeapAlloc( GetProcessHeap(), 0, sizeof(*action) ))) return NULL;
    action->IAsyncAction_iface.lpVtbl = &async_action_vtbl;
    action->ref = 1;
    action->status = status;
    return action;
}

/* Windows.System.Threading.WorkItemHandler: a delegate with a single Invoke slot. */
typedef struct WorkItemHandler WorkItemHandler;
typedef struct WorkItemHandlerVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( WorkItemHandler *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( WorkItemHandler *iface );
    ULONG (STDMETHODCALLTYPE *Release)( WorkItemHandler *iface );
    HRESULT (STDMETHODCALLTYPE *Invoke)( WorkItemHandler *iface, IAsyncAction *action );
} WorkItemHandlerVtbl;

static INT64 get_universal_time( void )
{
    ULARGE_INTEGER u;

    GetSystemTimePreciseAsFileTime( (FILETIME *)&u );
    return (INT64)u.QuadPart;
}

static void fire_completed_event( struct threadpool_timer *timer, AsyncStatus status )
{
    IInspectable *handler;

    EnterCriticalSection( &timer->cs );
    handler = timer->completed_handler;
    if (handler) IInspectable_AddRef( handler );
    LeaveCriticalSection( &timer->cs );

    if (handler)
    {
        /* Completed handler: Invoked(IAsyncAction, AsyncStatus). */
        struct async_action *action = create_async_action( status );

        if (action)
        {
            void **lpVtbl = *(void ***)handler;
            HRESULT (STDMETHODCALLTYPE *invoke)( void *, IAsyncAction *, AsyncStatus ) = (void *)lpVtbl[3];
            invoke( handler, (IAsyncAction *)&action->IAsyncAction_iface, status );
            IUnknown_Release( (IUnknown *)&action->IAsyncAction_iface );
        }
        IInspectable_Release( handler );
    }
}

static void fire_cancelled_event( struct threadpool_timer *timer )
{
    IInspectable *handler;

    EnterCriticalSection( &timer->cs );
    handler = timer->cancelled_handler;
    if (handler) IInspectable_AddRef( handler );
    LeaveCriticalSection( &timer->cs );

    if (handler)
    {
        struct async_action *action = create_async_action( ASYNC_STATUS_CANCELED );

        if (action)
        {
            void **lpVtbl = *(void ***)handler;
            HRESULT (STDMETHODCALLTYPE *invoke)( void *, IAsyncAction *, AsyncStatus ) = (void *)lpVtbl[3];
            invoke( handler, (IAsyncAction *)&action->IAsyncAction_iface, ASYNC_STATUS_CANCELED );
            IUnknown_Release( (IUnknown *)&action->IAsyncAction_iface );
        }
        IInspectable_Release( handler );
    }
}

static void run_handler( struct threadpool_timer *timer )
{
    IInspectable *handler;

    EnterCriticalSection( &timer->cs );
    handler = timer->handler;
    if (handler) IInspectable_AddRef( handler );
    LeaveCriticalSection( &timer->cs );

    if (!handler) return;

    {
        struct async_action *action = create_async_action( ASYNC_STATUS_STARTED );

        if (action)
        {
            void **lpVtbl = *(void ***)handler;
            HRESULT (STDMETHODCALLTYPE *invoke)( void *, IAsyncAction * ) = (void *)lpVtbl[3];
            HRESULT hr = invoke( handler, (IAsyncAction *)&action->IAsyncAction_iface );

            TRACE( "WorkItemHandler invocation returned %#lx.\n", hr );
            IUnknown_Release( (IUnknown *)&action->IAsyncAction_iface );
        }
    }
    IInspectable_Release( handler );
}

static DWORD WINAPI timer_thread_proc( void *arg )
{
    struct threadpool_timer *timer = arg;
    INT64 due, period;

    EnterCriticalSection( &timer->cs );
    due = timer->due_time;
    period = timer->period;
    LeaveCriticalSection( &timer->cs );

    for (;;)
    {
        INT64 now, wait_ticks;
        DWORD ms;

        now = get_universal_time();
        wait_ticks = due - now;
        if (wait_ticks < 0) wait_ticks = 0;
        ms = (DWORD)(wait_ticks / TICKS_PER_MS);
        if (!ms) ms = 1;

        if (WaitForSingleObject( timer->stop_event, ms ) == WAIT_OBJECT_0) break;

        if (timer->cancelled) break;

        run_handler( timer );
        fire_completed_event( timer, ASYNC_STATUS_COMPLETED );

        if (!period) break;
        due += period;
        if (due < get_universal_time()) due = get_universal_time() + period;
    }

    if (timer->cancelled) fire_cancelled_event( timer );
    return 0;
}

static HRESULT WINAPI timer_QueryInterface( IThreadPoolTimer *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    /*
     * The interface identifier of the Windows.System.Threading.IThreadPoolTimer
     * interface is not in Wine's metadata; report the requested identifier so that
     * it can be added, and answer E_NOINTERFACE for it.
     */
    FIXME( "%s not supported, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI timer_AddRef( IThreadPoolTimer *iface )
{
    return InterlockedIncrement( &impl_from_IThreadPoolTimer( iface )->ref );
}

static ULONG WINAPI timer_Release( IThreadPoolTimer *iface )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );
    ULONG ref = InterlockedDecrement( &timer->ref );

    if (!ref)
    {
        TRACE( "Destroying timer %p.\n", timer );
        SetEvent( timer->stop_event );
        if (timer->thread)
        {
            WaitForSingleObject( timer->thread, 5000 );
            CloseHandle( timer->thread );
        }
        CloseHandle( timer->stop_event );
        if (timer->handler) IInspectable_Release( timer->handler );
        if (timer->completed_handler) IInspectable_Release( timer->completed_handler );
        if (timer->cancelled_handler) IInspectable_Release( timer->cancelled_handler );
        DeleteCriticalSection( &timer->cs );
        HeapFree( GetProcessHeap(), 0, timer );
    }
    return ref;
}

static HRESULT WINAPI timer_GetIids( IThreadPoolTimer *iface, ULONG *count, GUID **iids )
{
    FIXME( "iface %p, count %p, iids %p, stub.\n", iface, count, iids );
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI timer_GetRuntimeClassName( IThreadPoolTimer *iface, HSTRING *name )
{
    return WindowsCreateString( class_name, lstrlenW( class_name ), name );
}

static HRESULT WINAPI timer_GetTrustLevel( IThreadPoolTimer *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI timer_get_Delay( IThreadPoolTimer *iface, WindowsFoundation_TimeSpan *value )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );
    INT64 delay;

    EnterCriticalSection( &timer->cs );
    delay = timer->due_time - get_universal_time();
    LeaveCriticalSection( &timer->cs );

    if (delay < 0) delay = 0;
    value->duration = delay;
    return S_OK;
}

static HRESULT WINAPI timer_get_Period( IThreadPoolTimer *iface, WindowsFoundation_TimeSpan *value )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );

    EnterCriticalSection( &timer->cs );
    value->duration = timer->period;
    LeaveCriticalSection( &timer->cs );
    return S_OK;
}

static HRESULT WINAPI timer_Cancel( IThreadPoolTimer *iface )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );

    TRACE( "iface %p.\n", iface );

    EnterCriticalSection( &timer->cs );
    timer->cancelled = TRUE;
    LeaveCriticalSection( &timer->cs );
    SetEvent( timer->stop_event );
    return S_OK;
}

static HRESULT WINAPI timer_add_event( IThreadPoolTimer *iface, IInspectable *handler,
        __MLEVENT_COOKIE *cookie, IInspectable **storage )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );
    static __MLEVENT_COOKIE cookie_counter = 1;
    __MLEVENT_COOKIE new_cookie = cookie_counter++;

    TRACE( "iface %p, handler %p, cookie %p.\n", iface, handler, cookie );

    EnterCriticalSection( &timer->cs );
    if (*storage)
    {
        LeaveCriticalSection( &timer->cs );
        return E_INVALIDARG;
    }
    *storage = handler;
    if (handler) IInspectable_AddRef( handler );
    LeaveCriticalSection( &timer->cs );

    *cookie = new_cookie;
    return S_OK;
}

static HRESULT WINAPI timer_remove_event( IThreadPoolTimer *iface, IInspectable **storage )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );

    TRACE( "iface %p.\n", iface );

    EnterCriticalSection( &timer->cs );
    if (*storage)
    {
        IInspectable_Release( *storage );
        *storage = NULL;
    }
    LeaveCriticalSection( &timer->cs );
    return S_OK;
}

static HRESULT WINAPI timer_add_Completed( IThreadPoolTimer *iface, IInspectable *handler, __MLEVENT_COOKIE *cookie )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );
    (void)timer;
    return timer_add_event( iface, handler, cookie, &timer->completed_handler );
}

static HRESULT WINAPI timer_remove_Completed( IThreadPoolTimer *iface, __MLEVENT_COOKIE cookie )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );
    (void)timer;
    return timer_remove_event( iface, &timer->completed_handler );
}

static HRESULT WINAPI timer_add_Cancelled( IThreadPoolTimer *iface, IInspectable *handler, __MLEVENT_COOKIE *cookie )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );
    (void)timer;
    return timer_add_event( iface, handler, cookie, &timer->cancelled_handler );
}

static HRESULT WINAPI timer_remove_Cancelled( IThreadPoolTimer *iface, __MLEVENT_COOKIE cookie )
{
    struct threadpool_timer *timer = impl_from_IThreadPoolTimer( iface );
    (void)timer;
    return timer_remove_event( iface, &timer->cancelled_handler );
}

const struct IThreadPoolTimerVtbl threadpool_timer_vtbl =
{
    timer_QueryInterface,
    timer_AddRef,
    timer_Release,
    timer_GetIids,
    timer_GetRuntimeClassName,
    timer_GetTrustLevel,
    timer_get_Delay,
    timer_get_Period,
    timer_Cancel,
    timer_add_Completed,
    timer_remove_Completed,
    timer_add_Cancelled,
    timer_remove_Cancelled,
};

static HRESULT create_timer( IInspectable *handler, WindowsFoundation_DateTime due_time,
        WindowsFoundation_TimeSpan period, IThreadPoolTimer **out )
{
    struct threadpool_timer *timer;

    if (!handler || !out) return E_INVALIDARG;
    *out = NULL;
    if (period.duration < 0) return E_INVALIDARG;

    if (!(timer = HeapAlloc( GetProcessHeap(), 0, sizeof(*timer) ))) return E_OUTOFMEMORY;
    timer->IThreadPoolTimer_iface.lpVtbl = &threadpool_timer_vtbl;
    timer->ref = 1;
    InitializeCriticalSection( &timer->cs );
    timer->handler = handler;
    IInspectable_AddRef( handler );
    timer->completed_handler = timer->cancelled_handler = NULL;
    timer->completed_cookie = timer->cancelled_cookie = 0;
    timer->due_time = due_time.universal_time;
    timer->period = period.duration;
    timer->cancelled = FALSE;
    timer->stop_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    timer->thread = CreateThread( NULL, 0, timer_thread_proc, timer, 0, NULL );
    if (!timer->thread)
    {
        CloseHandle( timer->stop_event );
        IInspectable_Release( handler );
        DeleteCriticalSection( &timer->cs );
        HeapFree( GetProcessHeap(), 0, timer );
        return E_OUTOFMEMORY;
    }

    *out = &timer->IThreadPoolTimer_iface;
    return S_OK;
}

static HRESULT WINAPI statics_QueryInterface( IThreadPoolTimerStatics *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IThreadPoolTimerStatics ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not supported, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI statics_AddRef( IThreadPoolTimerStatics *iface )
{
    return InterlockedIncrement( &impl_from_IThreadPoolTimerStatics( iface )->ref );
}

static ULONG WINAPI statics_Release( IThreadPoolTimerStatics *iface )
{
    return InterlockedDecrement( &impl_from_IThreadPoolTimerStatics( iface )->ref );
}

static HRESULT WINAPI statics_GetIids( IThreadPoolTimerStatics *iface, ULONG *count, GUID **iids )
{
    FIXME( "iface %p, count %p, iids %p, stub.\n", iface, count, iids );
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI statics_GetRuntimeClassName( IThreadPoolTimerStatics *iface, HSTRING *name )
{
    return WindowsCreateString( class_name, lstrlenW( class_name ), name );
}

static HRESULT WINAPI statics_GetTrustLevel( IThreadPoolTimerStatics *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI statics_CreatePeriodicTimer( IThreadPoolTimerStatics *iface, IInspectable *handler,
        WindowsFoundation_DateTime due_time, WindowsFoundation_TimeSpan period, IThreadPoolTimer **value )
{
    TRACE( "iface %p, handler %p, period %s.\n", iface, handler, wine_dbgstr_longlong( period.duration ) );

    return create_timer( handler, due_time, period, value );
}

static HRESULT WINAPI statics_CreateTimer( IThreadPoolTimerStatics *iface, IInspectable *handler,
        WindowsFoundation_DateTime due_time, IThreadPoolTimer **value )
{
    WindowsFoundation_TimeSpan period = {.duration = 0};

    TRACE( "iface %p, handler %p.\n", iface, handler );

    return create_timer( handler, due_time, period, value );
}

static HRESULT WINAPI statics_CreatePeriodicTimerWithPriority( IThreadPoolTimerStatics *iface, IInspectable *handler,
        WindowsFoundation_DateTime due_time, WindowsFoundation_TimeSpan period, INT32 priority, IThreadPoolTimer **value )
{
    TRACE( "iface %p, handler %p, priority %d.\n", iface, handler, priority );

    return create_timer( handler, due_time, period, value );
}

static HRESULT WINAPI statics_CreateTimerWithPriority( IThreadPoolTimerStatics *iface, IInspectable *handler,
        WindowsFoundation_DateTime due_time, INT32 priority, IThreadPoolTimer **value )
{
    WindowsFoundation_TimeSpan period = {.duration = 0};

    TRACE( "iface %p, handler %p, priority %d.\n", iface, handler, priority );

    return create_timer( handler, due_time, period, value );
}

const struct IThreadPoolTimerStaticsVtbl threadpool_timer_statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_CreatePeriodicTimer,
    statics_CreateTimer,
    statics_CreatePeriodicTimerWithPriority,
    statics_CreateTimerWithPriority,
};

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct threadpool_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IThreadPoolTimerStatics ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IThreadPoolTimerStatics_iface;
        return S_OK;
    }

    FIXME( "%s not supported, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_IActivationFactory( iface )->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_IActivationFactory( iface )->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, GUID **iids )
{
    FIXME( "iface %p, count %p, iids %p, stub.\n", iface, count, iids );
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( class_name, lstrlenW( class_name ), name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_CreateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p, stub.\n", iface, instance );
    *instance = NULL;
    return CLASS_E_NOAGGREGATION;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_CreateInstance,
};

static struct threadpool_statics statics =
{
    {&factory_vtbl},
    {&threadpool_timer_statics_vtbl},
    1,
};

static struct threadpool_statics *impl_from_statics_iface( void )
{
    return &statics;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_w( name ), factory );

    *factory = NULL;

    if (name && !wcscmp( name, class_name ))
    {
        struct threadpool_statics *impl = impl_from_statics_iface();

        InterlockedIncrement( &impl->ref );
        *factory = &impl->IActivationFactory_iface;
        return S_OK;
    }

    FIXME( "Returning CLASS_E_CLASSNOTAVAILABLE for class %s.\n", debugstr_w( name ) );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow( void )
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID iid, void **out )
{
    FIXME( "clsid %s, iid %s, out %p, stub.\n", debugstr_guid( clsid ), debugstr_guid( iid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}
