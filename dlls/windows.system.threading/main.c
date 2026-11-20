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
        void **lpVtbl = *(void ***)handler;
        HRESULT (STDMETHODCALLTYPE *invoke)( void *, IThreadPoolTimer * ) = (void *)lpVtbl[3];
        HRESULT hr = invoke( handler, &timer->IThreadPoolTimer_iface );

        TRACE( "TimerElapsedHandler invocation returned %#lx.\n", hr );
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
    static const IID IID_IThreadPoolTimer = {0x594ebe78, 0x55ea, 0x4a88, {0xa5, 0x0d, 0x34, 0x02, 0xae, 0x1f, 0x9c, 0xf2}};

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IThreadPoolTimer ))
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

/* ============================================================================
 * Windows.System.Threading.ThreadPool (IThreadPoolStatics)
 * ============================================================================ */

static const GUID IID_IThreadPoolStatics_local =
    { 0xb6bf67dd, 0x84bd, 0x44f8, {0xac, 0x1c, 0x93, 0xeb, 0xcb, 0x9d, 0xba, 0x91} };
static const GUID IID_IAsyncAction_real =
    { 0x5a648006, 0x843a, 0x4da9, {0x86, 0x5b, 0x9d, 0x26, 0xe5, 0xdf, 0xad, 0x7b} };
static const GUID IID_IAsyncInfo_real =
    { 0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46} };

struct tp_async_action
{
    const void *lpVtbl_action;
    const void *lpVtbl_info;
    LONG ref;
    SRWLOCK lock;
    AsyncStatus status;
    HRESULT error_code;
    void *work_handler;
    void *completed_handler;
};

static HRESULT WINAPI tpa_QI( void *iface, REFIID iid, void **out )
{
    struct tp_async_action *impl = CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_action );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncAction_real ))
    {
        *out = &impl->lpVtbl_action;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAsyncInfo_real ))
    {
        *out = &impl->lpVtbl_info;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI tpa_AddRef( void *iface )
{
    return InterlockedIncrement( &CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_action )->ref );
}
static ULONG WINAPI tpa_Release( void *iface )
{
    struct tp_async_action *impl = CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_action );
    ULONG r = InterlockedDecrement( &impl->ref );
    if (!r)
    {
        if (impl->work_handler) IUnknown_Release( (IUnknown *)impl->work_handler );
        if (impl->completed_handler) IUnknown_Release( (IUnknown *)impl->completed_handler );
        free( impl );
    }
    return r;
}
static HRESULT WINAPI tpa_GetIids( void *iface, ULONG *c, IID **i )
{
    if (!c || !i) return E_POINTER;
    *i = CoTaskMemAlloc( 2 * sizeof(IID) );
    if (!*i) { *c = 0; return E_OUTOFMEMORY; }
    (*i)[0] = IID_IAsyncAction_real;
    (*i)[1] = IID_IAsyncInfo_real;
    *c = 2;
    return S_OK;
}
static HRESULT WINAPI tpa_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.Foundation.IAsyncAction";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI tpa_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI tpa_put_Completed( void *iface, void *handler )
{
    struct tp_async_action *impl = CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_action );
    AsyncStatus st;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->completed_handler) IUnknown_Release( (IUnknown *)impl->completed_handler );
    impl->completed_handler = handler;
    if (handler) IUnknown_AddRef( (IUnknown *)handler );
    st = impl->status;
    ReleaseSRWLockExclusive( &impl->lock );
    if (handler && st != ASYNC_STATUS_STARTED)
    {
        HRESULT (WINAPI *invoke)(void *, void *, AsyncStatus) = (void *)(*(void ***)handler)[3];
        if (invoke) invoke( handler, &impl->lpVtbl_action, st );
    }
    return S_OK;
}
static HRESULT WINAPI tpa_get_Completed( void *iface, void **handler )
{
    struct tp_async_action *impl = CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_action );
    if (!handler) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *handler = impl->completed_handler;
    if (*handler) IUnknown_AddRef( (IUnknown *)*handler );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}
static HRESULT WINAPI tpa_GetResults( void *iface )
{
    struct tp_async_action *impl = CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_action );
    return impl->error_code;
}

static HRESULT WINAPI tpa_info_QI( void *iface, REFIID iid, void **out )
{
    return tpa_QI( &CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_info )->lpVtbl_action, iid, out );
}
static ULONG WINAPI tpa_info_AddRef( void *iface )
{
    return tpa_AddRef( &CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_info )->lpVtbl_action );
}
static ULONG WINAPI tpa_info_Release( void *iface )
{
    return tpa_Release( &CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_info )->lpVtbl_action );
}
static HRESULT WINAPI tpa_info_get_Id( void *iface, UINT32 *id ) { if (id) *id = 1; return S_OK; }
static HRESULT WINAPI tpa_info_get_Status( void *iface, AsyncStatus *st )
{
    struct tp_async_action *impl = CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_info );
    if (st) *st = impl->status;
    return S_OK;
}
static HRESULT WINAPI tpa_info_get_ErrorCode( void *iface, HRESULT *hr )
{
    struct tp_async_action *impl = CONTAINING_RECORD( iface, struct tp_async_action, lpVtbl_info );
    if (hr) *hr = impl->error_code;
    return S_OK;
}
static HRESULT WINAPI tpa_info_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI tpa_info_Close( void *iface ) { return S_OK; }

static const void * const tpa_action_vtbl[] =
{
    tpa_QI, tpa_AddRef, tpa_Release, tpa_GetIids, tpa_GetRuntimeClassName, tpa_GetTrustLevel,
    tpa_put_Completed, tpa_get_Completed, tpa_GetResults,
};
static const void * const tpa_info_vtbl[] =
{
    tpa_info_QI, tpa_info_AddRef, tpa_info_Release, tpa_GetIids, tpa_GetRuntimeClassName, tpa_GetTrustLevel,
    tpa_info_get_Id, tpa_info_get_Status, tpa_info_get_ErrorCode, tpa_info_Cancel, tpa_info_Close,
};

static DWORD WINAPI tp_work_proc( void *param )
{
    struct tp_async_action *impl = (struct tp_async_action *)param;
    HRESULT hr = S_OK;
    void *completed = NULL;
    AsyncStatus st;

    if (impl->work_handler)
    {
        HRESULT (WINAPI *invoke)(void *, void *) = (void *)(*(void ***)impl->work_handler)[3];
        if (invoke) hr = invoke( impl->work_handler, &impl->lpVtbl_action );
    }
    AcquireSRWLockExclusive( &impl->lock );
    impl->error_code = hr;
    impl->status = SUCCEEDED(hr) ? ASYNC_STATUS_COMPLETED : ASYNC_STATUS_ERROR;
    st = impl->status;
    completed = impl->completed_handler;
    if (completed) IUnknown_AddRef( (IUnknown *)completed );
    ReleaseSRWLockExclusive( &impl->lock );

    if (completed)
    {
        HRESULT (WINAPI *invoke)(void *, void *, AsyncStatus) = (void *)(*(void ***)completed)[3];
        if (invoke) invoke( completed, &impl->lpVtbl_action, st );
        IUnknown_Release( (IUnknown *)completed );
    }
    tpa_Release( &impl->lpVtbl_action );
    return 0;
}

struct tp_factory
{
    const void *lpVtbl_factory;
    const void *lpVtbl_statics;
};
static struct tp_factory tp_factory_obj;

static HRESULT WINAPI tp_fac_QI( void *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &tp_factory_obj.lpVtbl_factory;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IThreadPoolStatics_local ))
    {
        *out = &tp_factory_obj.lpVtbl_statics;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI tp_fac_AddRef( void *iface ) { return 2; }
static ULONG WINAPI tp_fac_Release( void *iface ) { return 1; }
static HRESULT WINAPI tp_fac_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI tp_fac_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR cls[] = L"Windows.System.Threading.ThreadPool";
    return WindowsCreateString( cls, ARRAY_SIZE(cls) - 1, n );
}
static HRESULT WINAPI tp_fac_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
static HRESULT WINAPI tp_fac_ActivateInstance( void *iface, IInspectable **out ) { if (out) *out = NULL; return E_NOTIMPL; }

static HRESULT WINAPI tp_st_RunWithPriorityAndOptionsAsync( void *iface, void *handler, INT32 priority, UINT32 options, void **op )
{
    struct tp_async_action *impl;
    if (!op) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl_action = tpa_action_vtbl;
    impl->lpVtbl_info = tpa_info_vtbl;
    impl->ref = 2; /* 1 for caller, 1 for worker */
    InitializeSRWLock( &impl->lock );
    impl->status = ASYNC_STATUS_STARTED;
    impl->work_handler = handler;
    if (handler) IUnknown_AddRef( (IUnknown *)handler );
    if (!QueueUserWorkItem( tp_work_proc, impl, WT_EXECUTEDEFAULT ))
    {
        tpa_Release( &impl->lpVtbl_action );
        tpa_Release( &impl->lpVtbl_action );
        *op = NULL;
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    *op = &impl->lpVtbl_action;
    return S_OK;
}
static HRESULT WINAPI tp_st_RunWithPriorityAsync( void *iface, void *handler, INT32 priority, void **op )
{
    return tp_st_RunWithPriorityAndOptionsAsync( iface, handler, priority, 0, op );
}
static HRESULT WINAPI tp_st_RunAsync( void *iface, void *handler, void **op )
{
    return tp_st_RunWithPriorityAndOptionsAsync( iface, handler, 0, 0, op );
}

static const void * const tp_fac_vtbl[] = { tp_fac_QI, tp_fac_AddRef, tp_fac_Release, tp_fac_GetIids, tp_fac_GetRuntimeClassName, tp_fac_GetTrustLevel, tp_fac_ActivateInstance };
static const void * const tp_st_vtbl[] = { tp_fac_QI, tp_fac_AddRef, tp_fac_Release, tp_fac_GetIids, tp_fac_GetRuntimeClassName, tp_fac_GetTrustLevel, tp_st_RunAsync, tp_st_RunWithPriorityAsync, tp_st_RunWithPriorityAndOptionsAsync };
static struct tp_factory tp_factory_obj = { tp_fac_vtbl, tp_st_vtbl };

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
    if (name && !wcscmp( name, L"Windows.System.Threading.ThreadPool" ))
    {
        *factory = (IActivationFactory *)&tp_factory_obj.lpVtbl_factory;
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
