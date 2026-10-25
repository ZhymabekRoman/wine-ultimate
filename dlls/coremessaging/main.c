/* WinRT CoreMessaging Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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
#include "dispatcherqueue.h"

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/list.h"
#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(messaging);

/*
 * Probe a caller supplied interface identifier.
 *
 * The CoreMessaging conversation engine dispatches vtable slot 0 of the
 * object a conversation reply carries as a plain call with its own registers
 * left in place (measured on the CoreMessagingXP receiver dispatch): the first
 * argument is the address of the reply slot, not an interface pointer of this
 * object, and the second argument is the caller's leftover register (measured
 * value 0x10, the verb of the pending request), not a readable REFIID.  A slot
 * 0 implementation may therefore not read through - or print - the second
 * argument before it has been probed.  Copy the guid through a guarded read;
 * a probe fault means the caller never intended a QueryInterface here.
 */
static BOOL probe_refiid( REFIID iid, GUID *buf )
{
    if (!iid) return FALSE;
    __TRY
    {
        *buf = *iid;
    }
    __EXCEPT_ALL
    {
        return FALSE;
    }
    __ENDTRY
    return TRUE;
}

struct dispatcher_queue_controller_statics
{
    IActivationFactory IActivationFactory_iface;
    IDispatcherQueueControllerStatics IDispatcherQueueControllerStatics_iface;
    LONG ref;
};

static inline struct dispatcher_queue_controller_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue_controller_statics, IActivationFactory_iface );
}

static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};
static const GUID IID_IMicrosoftDispatcherQueueControllerStatics = {0xf18d6145, 0x722b, 0x593d, {0xbc, 0xf2, 0xa6, 0x1e, 0x71, 0x3f, 0x00, 0x37}};

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct dispatcher_queue_controller_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDispatcherQueueControllerStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftDispatcherQueueControllerStatics ))
    {
        *out = &impl->IDispatcherQueueControllerStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct dispatcher_queue_controller_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing ref to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct dispatcher_queue_controller_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing ref to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
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

#define DQMSG_RUN      (WM_APP + 1)
#define DQMSG_SHUTDOWN (WM_APP + 2)
#define DQMSG_DESTROY  (WM_APP + 3)

#define DQ_PRIORITY_COUNT 3

/* Drain order index for DispatcherQueuePriority.  Accept both the Windows.System values
 * ( High = 10, Normal = 0, Low = -10 ) and the Microsoft.UI.Dispatching values
 * ( High = 1, Normal = 0, Low = -1 ) that the Windows App SDK passes through. */
static unsigned int dq_priority_index( DispatcherQueuePriority priority )
{
    if (priority > DispatcherQueuePriority_Normal) return 0;
    if (priority < DispatcherQueuePriority_Normal) return 2;
    return 1;
}

static const struct IDispatcherQueueVtbl dispatcher_queue_vtbl;
static const struct IDispatcherQueue2Vtbl dispatcher_queue2_vtbl;

struct dispatcher_queue_item
{
    struct list entry;
    IDispatcherQueueHandler *handler;
};

struct dispatcher_event_handler
{
    struct list entry;
    EventRegistrationToken token;
    IUnknown *handler;
};

struct dispatcher_queue_timer;

struct dispatcher_queue
{
    IDispatcherQueue IDispatcherQueue_iface;
    IDispatcherQueue2 IDispatcherQueue2_iface;
    LONG ref;
    DWORD tid;
    struct list tid_entry;
    HWND hwnd;
    CRITICAL_SECTION cs;
    struct list items[DQ_PRIORITY_COUNT];
    struct list timers;
    struct list shutdown_starting;
    struct list shutdown_completed;
    boolean shutdown;
    LONG64 next_timer_id;
};

static struct list dispatcher_queue_obj_list = LIST_INIT( dispatcher_queue_obj_list );
static CRITICAL_SECTION dispatcher_queue_list_lock;
static INIT_ONCE dispatcher_queue_init_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK dispatcher_queue_init_once_cb( INIT_ONCE *once, void *param, void **context )
{
    InitializeCriticalSection( &dispatcher_queue_list_lock );
    return TRUE;
}

static inline struct dispatcher_queue *impl_from_IDispatcherQueue( IDispatcherQueue *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue, IDispatcherQueue_iface );
}
static inline struct dispatcher_queue *impl_from_IDispatcherQueue2( IDispatcherQueue2 *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue, IDispatcherQueue2_iface );
}

static struct dispatcher_queue *dispatcher_queue_from_thread( DWORD tid )
{
    struct dispatcher_queue *queue;

    InitOnceExecuteOnce( &dispatcher_queue_init_once, dispatcher_queue_init_once_cb, NULL, NULL );
    EnterCriticalSection( &dispatcher_queue_list_lock );
    LIST_FOR_EACH_ENTRY( queue, &dispatcher_queue_obj_list, struct dispatcher_queue, tid_entry )
    {
        if (queue->tid == tid)
        {
            LeaveCriticalSection( &dispatcher_queue_list_lock );
            return queue;
        }
    }
    LeaveCriticalSection( &dispatcher_queue_list_lock );
    return NULL;
}

static const GUID IID_IMicrosoftDispatcherQueue = {0xf6ebf8fa, 0xbe1c, 0x5bf6, {0xa4, 0x67, 0x73, 0xda, 0x28, 0x73, 0x8a, 0xe8}};

static HRESULT WINAPI dispatcher_queue_QueryInterface( IDispatcherQueue *iface, REFIID iid, void **out )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDispatcherQueue ) ||
        IsEqualGUID( iid, &IID_IMicrosoftDispatcherQueue ))
    {
        *out = &impl->IDispatcherQueue_iface;
        IDispatcherQueue_AddRef( *out );
        return S_OK;
    }
    else if (IsEqualGUID( iid, &IID_IDispatcherQueue2 ))
    {
        *out = &impl->IDispatcherQueue2_iface;
        IDispatcherQueue2_AddRef( *out );
        return S_OK;
    }
    FIXME( "%s not implemented.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dispatcher_queue_AddRef( IDispatcherQueue *iface )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI dispatcher_queue_Release( IDispatcherQueue *iface )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->hwnd)
        {
            PostMessageW( impl->hwnd, DQMSG_DESTROY, 0, 0 );
            if (impl->tid) PostThreadMessageW( impl->tid, WM_QUIT, 0, 0 );
            EnterCriticalSection( &dispatcher_queue_list_lock );
            list_remove( &impl->tid_entry );
            LeaveCriticalSection( &dispatcher_queue_list_lock );
        }
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI dispatcher_queue_GetIids( IDispatcherQueue *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_queue_GetRuntimeClassName( IDispatcherQueue *iface, HSTRING *name ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_queue_GetTrustLevel( IDispatcherQueue *iface, TrustLevel *level ) { return E_NOTIMPL; }

/* ---------------- dispatcher queue timer ---------------- */

struct dispatcher_queue_timer
{
    IDispatcherQueueTimer IDispatcherQueueTimer_iface;
    struct dispatcher_queue *queue;
    struct list entry;
    LONG ref;
    TimeSpan interval;
    boolean repeating;
    boolean running;
    UINT timer_id;
    struct list handlers;
};

static inline struct dispatcher_queue_timer *impl_from_IDispatcherQueueTimer( IDispatcherQueueTimer *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue_timer, IDispatcherQueueTimer_iface );
}

static HRESULT WINAPI dispatcher_queue_timer_QueryInterface( IDispatcherQueueTimer *iface, REFIID iid, void **out )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IDispatcherQueueTimer ))
    {
        *out = &impl->IDispatcherQueueTimer_iface;
        IDispatcherQueueTimer_AddRef( *out );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dispatcher_queue_timer_AddRef( IDispatcherQueueTimer *iface )
{
    return InterlockedIncrement( &impl_from_IDispatcherQueueTimer( iface )->ref );
}
static ULONG WINAPI dispatcher_queue_timer_Release( IDispatcherQueueTimer *iface )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        EnterCriticalSection( &impl->queue->cs );
        if (impl->running && impl->queue->hwnd) KillTimer( impl->queue->hwnd, impl->timer_id );
        list_remove( &impl->entry );
        LeaveCriticalSection( &impl->queue->cs );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI dispatcher_queue_timer_GetIids( IDispatcherQueueTimer *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_queue_timer_GetRuntimeClassName( IDispatcherQueueTimer *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.System.DispatcherQueueTimer", 34, n );
}
static HRESULT WINAPI dispatcher_queue_timer_GetTrustLevel( IDispatcherQueueTimer *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_get_Interval( IDispatcherQueueTimer *iface, TimeSpan *value )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    if (!value) return E_POINTER;
    *value = impl->interval;
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_put_Interval( IDispatcherQueueTimer *iface, TimeSpan value )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    impl->interval = value;
    if (impl->running && impl->repeating && impl->queue->hwnd)
        SetTimer( impl->queue->hwnd, impl->timer_id, (UINT)(value.Duration / 10000), NULL );
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_get_IsRunning( IDispatcherQueueTimer *iface, boolean *value )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    if (!value) return E_POINTER;
    *value = impl->running;
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_get_IsRepeating( IDispatcherQueueTimer *iface, boolean *value )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    if (!value) return E_POINTER;
    *value = impl->repeating;
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_put_IsRepeating( IDispatcherQueueTimer *iface, boolean value )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    impl->repeating = value;
    if (impl->running && impl->queue->hwnd)
        SetTimer( impl->queue->hwnd, impl->timer_id, (UINT)(impl->interval.Duration / 10000), NULL );
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_Start( IDispatcherQueueTimer *iface )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    UINT ms = (UINT)(impl->interval.Duration / 10000);
    if (impl->running) return S_OK;
    if (!impl->queue->hwnd) return E_FAIL;
    impl->running = TRUE;
    if (!impl->timer_id) impl->timer_id = (UINT)InterlockedIncrement64( &impl->queue->next_timer_id );
    SetTimer( impl->queue->hwnd, impl->timer_id, ms ? ms : 1, NULL );
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_Stop( IDispatcherQueueTimer *iface )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    if (!impl->running) return S_OK;
    impl->running = FALSE;
    if (impl->queue->hwnd && impl->timer_id) KillTimer( impl->queue->hwnd, impl->timer_id );
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_add_Tick( IDispatcherQueueTimer *iface,
        __FITypedEventHandler_2_Windows__CSystem__CDispatcherQueueTimer_IInspectable *handler, EventRegistrationToken *token )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    struct dispatcher_event_handler *entry;
    if (!handler) return E_INVALIDARG;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return E_OUTOFMEMORY;
    entry->handler = (IUnknown *)handler;
    IUnknown_AddRef( entry->handler );
    entry->token.value = InterlockedIncrement64( (LONG64 *)&impl->queue->next_timer_id );
    list_add_tail( &impl->handlers, &entry->entry );
    if (token) *token = entry->token;
    return S_OK;
}
static HRESULT WINAPI dispatcher_queue_timer_remove_Tick( IDispatcherQueueTimer *iface, EventRegistrationToken token )
{
    struct dispatcher_queue_timer *impl = impl_from_IDispatcherQueueTimer( iface );
    struct dispatcher_event_handler *entry, *tmp;
    LIST_FOR_EACH_ENTRY_SAFE( entry, tmp, &impl->handlers, struct dispatcher_event_handler, entry )
    {
        if (entry->token.value == token.value)
        {
            list_remove( &entry->entry );
            IUnknown_Release( entry->handler );
            free( entry );
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

static const struct IDispatcherQueueTimerVtbl dispatcher_queue_timer_vtbl =
{
    dispatcher_queue_timer_QueryInterface,
    dispatcher_queue_timer_AddRef,
    dispatcher_queue_timer_Release,
    /* IInspectable methods */
    dispatcher_queue_timer_GetIids,
    dispatcher_queue_timer_GetRuntimeClassName,
    dispatcher_queue_timer_GetTrustLevel,
    /* IDispatcherQueueTimer methods */
    dispatcher_queue_timer_get_Interval,
    dispatcher_queue_timer_put_Interval,
    dispatcher_queue_timer_get_IsRunning,
    dispatcher_queue_timer_get_IsRepeating,
    dispatcher_queue_timer_put_IsRepeating,
    dispatcher_queue_timer_Start,
    dispatcher_queue_timer_Stop,
    dispatcher_queue_timer_add_Tick,
    dispatcher_queue_timer_remove_Tick,
};

static void dispatcher_queue_timer_tick( struct dispatcher_queue_timer *timer )
{
    struct dispatcher_event_handler *entry;
    IDispatcherQueueTimer *iface = &timer->IDispatcherQueueTimer_iface;

    if (!timer->repeating)
    {
        timer->running = FALSE;
        if (timer->queue->hwnd && timer->timer_id) KillTimer( timer->queue->hwnd, timer->timer_id );
    }
    IDispatcherQueueTimer_AddRef( iface );
    LIST_FOR_EACH_ENTRY( entry, &timer->handlers, struct dispatcher_event_handler, entry )
    {
        __FITypedEventHandler_2_Windows__CSystem__CDispatcherQueueTimer_IInspectable *handler =
            (__FITypedEventHandler_2_Windows__CSystem__CDispatcherQueueTimer_IInspectable *)entry->handler;
        handler->lpVtbl->Invoke( handler, iface, (IInspectable *)iface );
    }
    IDispatcherQueueTimer_Release( iface );
}

/* ---------------- queue pumping ---------------- */

static struct dispatcher_queue_timer *dispatcher_queue_find_timer( struct dispatcher_queue *queue, UINT id )
{
    struct dispatcher_queue_timer *timer;
    LIST_FOR_EACH_ENTRY( timer, &queue->timers, struct dispatcher_queue_timer, entry )
        if (timer->timer_id == id) return timer;
    return NULL;
}

static void dispatcher_queue_run_next( struct dispatcher_queue *queue )
{
    struct dispatcher_queue_item *item = NULL;
    unsigned int i;

    EnterCriticalSection( &queue->cs );
    for (i = 0; i < DQ_PRIORITY_COUNT; i++)
    {
        if (!list_empty( &queue->items[i] ))
        {
            item = CONTAINING_RECORD( list_head( &queue->items[i] ), struct dispatcher_queue_item, entry );
            list_remove( &item->entry );
            break;
        }
    }
    LeaveCriticalSection( &queue->cs );

    if (!item) return;
    IDispatcherQueueHandler_Invoke( item->handler );
    IDispatcherQueueHandler_Release( item->handler );
    free( item );
}

static void dispatcher_queue_drain( struct dispatcher_queue *queue )
{
    struct dispatcher_queue_item *item;
    unsigned int i;

    for (;;)
    {
        EnterCriticalSection( &queue->cs );
        item = NULL;
        for (i = 0; i < DQ_PRIORITY_COUNT; i++)
            if (!list_empty( &queue->items[i] ))
            {
                item = CONTAINING_RECORD( list_head( &queue->items[i] ), struct dispatcher_queue_item, entry );
                list_remove( &item->entry );
                break;
            }
        LeaveCriticalSection( &queue->cs );
        if (!item) break;
        IDispatcherQueueHandler_Invoke( item->handler );
        IDispatcherQueueHandler_Release( item->handler );
        free( item );
    }
}

static void dispatcher_queue_fire_shutdown_events( struct dispatcher_queue *queue, boolean starting )
{
    struct list *list = starting ? &queue->shutdown_starting : &queue->shutdown_completed;
    struct dispatcher_event_handler *entry;
    IDispatcherQueue *iface = &queue->IDispatcherQueue_iface;

    IDispatcherQueue_AddRef( iface );
    if (starting)
    {
        LIST_FOR_EACH_ENTRY( entry, list, struct dispatcher_event_handler, entry )
        {
            ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs *handler =
                (void *)entry->handler;
            handler->lpVtbl->Invoke( handler, iface, NULL );
        }
    }
    else
    {
        LIST_FOR_EACH_ENTRY( entry, list, struct dispatcher_event_handler, entry )
        {
            ITypedEventHandler_DispatcherQueue_IInspectable *handler = (void *)entry->handler;
            handler->lpVtbl->Invoke( handler, iface, NULL );
        }
    }
    IDispatcherQueue_Release( iface );
}

static HRESULT dispatcher_queue_shutdown( struct dispatcher_queue *queue )
{
    if (!queue) return S_OK;
    EnterCriticalSection( &queue->cs );
    if (queue->shutdown)
    {
        LeaveCriticalSection( &queue->cs );
        return S_OK;
    }
    queue->shutdown = TRUE;
    LeaveCriticalSection( &queue->cs );
    if (queue->hwnd) SendMessageW( queue->hwnd, DQMSG_SHUTDOWN, 0, 0 );
    return S_OK;
}

static LRESULT WINAPI dispatcher_queue_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct dispatcher_queue *queue = (struct dispatcher_queue *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );

    switch (msg)
    {
    case DQMSG_RUN:
        dispatcher_queue_run_next( queue );
        EnterCriticalSection( &queue->cs );
        {
            unsigned int i;
            boolean more = FALSE;
            for (i = 0; i < DQ_PRIORITY_COUNT; i++)
                if (!list_empty( &queue->items[i] )) { more = TRUE; break; }
            if (more) PostMessageW( hwnd, DQMSG_RUN, 0, 0 );
        }
        LeaveCriticalSection( &queue->cs );
        return 0;

    case DQMSG_SHUTDOWN:
        dispatcher_queue_fire_shutdown_events( queue, TRUE );
        dispatcher_queue_drain( queue );
        dispatcher_queue_fire_shutdown_events( queue, FALSE );
        return 0;

    case DQMSG_DESTROY:
        {
            struct dispatcher_queue_timer *timer, *tmp;
            EnterCriticalSection( &queue->cs );
            LIST_FOR_EACH_ENTRY_SAFE( timer, tmp, &queue->timers, struct dispatcher_queue_timer, entry )
                if (timer->running) KillTimer( hwnd, timer->timer_id );
            LeaveCriticalSection( &queue->cs );
            DestroyWindow( hwnd );
        }
        return 0;

    case WM_TIMER:
        {
            struct dispatcher_queue_timer *timer;
            EnterCriticalSection( &queue->cs );
            timer = dispatcher_queue_find_timer( queue, (UINT)wparam );
            if (timer) IDispatcherQueueTimer_AddRef( &timer->IDispatcherQueueTimer_iface );
            LeaveCriticalSection( &queue->cs );
            if (timer)
            {
                dispatcher_queue_timer_tick( timer );
                IDispatcherQueueTimer_Release( &timer->IDispatcherQueueTimer_iface );
            }
        }
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static HRESULT dispatcher_queue_bind_thread( struct dispatcher_queue *queue )
{
    static const WCHAR class_name[] = L"wineCoreMessagingDispatcherQueue";
    static WNDCLASSEXW wc = { sizeof(wc) };
    static volatile LONG class_registered;
    HINSTANCE inst = GetModuleHandleW( NULL );

    if (!InterlockedExchange( &class_registered, 1 ))
    {
        wc.style = 0;
        wc.lpfnWndProc = dispatcher_queue_wndproc;
        wc.hInstance = inst;
        wc.lpszClassName = class_name;
        if (!RegisterClassExW( &wc )) return E_FAIL;
    }

    queue->tid = GetCurrentThreadId();
    queue->hwnd = CreateWindowExW( 0, class_name, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL );
    if (!queue->hwnd) return E_FAIL;
    SetWindowLongPtrW( queue->hwnd, GWLP_USERDATA, (LPARAM)queue );

    InitOnceExecuteOnce( &dispatcher_queue_init_once, dispatcher_queue_init_once_cb, NULL, NULL );
    EnterCriticalSection( &dispatcher_queue_list_lock );
    list_add_tail( &dispatcher_queue_obj_list, &queue->tid_entry );
    LeaveCriticalSection( &dispatcher_queue_list_lock );
    return S_OK;
}

static struct dispatcher_queue *dispatcher_queue_alloc(void)
{
    struct dispatcher_queue *queue;
    unsigned int i;

    if (!(queue = calloc( 1, sizeof(*queue) ))) return NULL;
    queue->IDispatcherQueue_iface.lpVtbl = &dispatcher_queue_vtbl;
    queue->IDispatcherQueue2_iface.lpVtbl = &dispatcher_queue2_vtbl;
    queue->ref = 1;
    queue->tid = 0;
    queue->hwnd = NULL;
    InitializeCriticalSection( &queue->cs );
    for (i = 0; i < DQ_PRIORITY_COUNT; i++) list_init( &queue->items[i] );
    list_init( &queue->timers );
    list_init( &queue->shutdown_starting );
    list_init( &queue->shutdown_completed );
    list_init( &queue->tid_entry );
    return queue;
}

struct dq_dedicated_thread_init
{
    struct dispatcher_queue *queue;
    HANDLE ready;
    HRESULT hr;
};

static DWORD WINAPI dispatcher_queue_thread_proc( void *param )
{
    struct dq_dedicated_thread_init *init = param;
    MSG msg;

    init->hr = dispatcher_queue_bind_thread( init->queue );
    SetEvent( init->ready );
    if (FAILED( init->hr )) return 0;

    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}

static HRESULT dispatcher_queue_create_dedicated( struct dispatcher_queue **out )
{
    struct dq_dedicated_thread_init init;
    HANDLE thread;
    DWORD tid;

    if (!(*out = dispatcher_queue_alloc())) return E_OUTOFMEMORY;
    init.queue = *out;
    init.hr = E_FAIL;
    if (!(init.ready = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        free( *out );
        return E_OUTOFMEMORY;
    }
    if (!(thread = CreateThread( NULL, 0, dispatcher_queue_thread_proc, &init, 0, &tid )))
    {
        CloseHandle( init.ready );
        free( *out );
        return E_FAIL;
    }
    CloseHandle( thread );
    WaitForSingleObject( init.ready, INFINITE );
    CloseHandle( init.ready );
    if (FAILED( init.hr ))
    {
        free( *out );
        *out = NULL;
    }
    return init.hr;
}

static HRESULT WINAPI dispatcher_queue_CreateTimer( IDispatcherQueue *iface, IDispatcherQueueTimer **result )
{
    struct dispatcher_queue *queue = impl_from_IDispatcherQueue( iface );
    struct dispatcher_queue_timer *timer;

    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    if (!(timer = calloc( 1, sizeof(*timer) ))) return E_OUTOFMEMORY;
    timer->IDispatcherQueueTimer_iface.lpVtbl = &dispatcher_queue_timer_vtbl;
    timer->queue = queue;
    timer->ref = 1;
    list_init( &timer->handlers );
    EnterCriticalSection( &queue->cs );
    list_add_tail( &queue->timers, &timer->entry );
    LeaveCriticalSection( &queue->cs );
    *result = &timer->IDispatcherQueueTimer_iface;
    return S_OK;
}

static HRESULT dispatcher_queue_try_enqueue( struct dispatcher_queue *queue, DispatcherQueuePriority priority,
        IDispatcherQueueHandler *callback, boolean *result )
{
    struct dispatcher_queue_item *item;

    TRACE( "queue %p, priority %d, callback %p, result %p.\n", queue, priority, callback, result );
    if (!callback || !result) return E_INVALIDARG;

    EnterCriticalSection( &queue->cs );
    if (queue->shutdown)
    {
        LeaveCriticalSection( &queue->cs );
        *result = FALSE;
        return S_OK;
    }
    if (!(item = calloc( 1, sizeof(*item) )))
    {
        LeaveCriticalSection( &queue->cs );
        return E_OUTOFMEMORY;
    }
    item->handler = callback;
    IDispatcherQueueHandler_AddRef( callback );
    list_add_tail( &queue->items[dq_priority_index( priority )], &item->entry );
    LeaveCriticalSection( &queue->cs );

    if (!queue->hwnd || !PostMessageW( queue->hwnd, DQMSG_RUN, 0, 0 ))
    {
        /* queue window gone: run inline to keep the callback contract */
        dispatcher_queue_run_next( queue );
    }
    *result = TRUE;
    return S_OK;
}

static HRESULT WINAPI dispatcher_queue_TryEnqueue( IDispatcherQueue *iface, IDispatcherQueueHandler *callback, boolean *result )
{
    return dispatcher_queue_try_enqueue( impl_from_IDispatcherQueue( iface ), DispatcherQueuePriority_Normal, callback, result );
}
static HRESULT WINAPI dispatcher_queue_TryEnqueueWithPriority( IDispatcherQueue *iface, DispatcherQueuePriority priority,
        IDispatcherQueueHandler *callback, boolean *result )
{
    return dispatcher_queue_try_enqueue( impl_from_IDispatcherQueue( iface ), priority, callback, result );
}

static HRESULT dispatcher_queue_add_handler( struct dispatcher_queue *queue, struct list *list,
        IUnknown *handler, EventRegistrationToken *token )
{
    struct dispatcher_event_handler *entry;
    if (!handler) return E_INVALIDARG;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return E_OUTOFMEMORY;
    entry->handler = handler;
    IUnknown_AddRef( entry->handler );
    entry->token.value = InterlockedIncrement64( (LONG64 *)&queue->next_timer_id );
    list_add_tail( list, &entry->entry );
    if (token) *token = entry->token;
    return S_OK;
}

static HRESULT dispatcher_queue_remove_handler( struct dispatcher_queue *queue, struct list *list,
        EventRegistrationToken token )
{
    struct dispatcher_event_handler *entry, *tmp;
    LIST_FOR_EACH_ENTRY_SAFE( entry, tmp, list, struct dispatcher_event_handler, entry )
    {
        if (entry->token.value == token.value)
        {
            list_remove( &entry->entry );
            IUnknown_Release( entry->handler );
            free( entry );
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI dispatcher_queue_add_ShutdownStarting( IDispatcherQueue *iface,
        ITypedEventHandler_DispatcherQueue_DispatcherQueueShutdownStartingEventArgs *handler, EventRegistrationToken *token )
{
    struct dispatcher_queue *queue = impl_from_IDispatcherQueue( iface );
    return dispatcher_queue_add_handler( queue, &queue->shutdown_starting, (IUnknown *)handler, token );
}
static HRESULT WINAPI dispatcher_queue_remove_ShutdownStarting( IDispatcherQueue *iface, EventRegistrationToken token )
{
    struct dispatcher_queue *queue = impl_from_IDispatcherQueue( iface );
    return dispatcher_queue_remove_handler( queue, &queue->shutdown_starting, token );
}
static HRESULT WINAPI dispatcher_queue_add_ShutdownCompleted( IDispatcherQueue *iface,
        ITypedEventHandler_DispatcherQueue_IInspectable *handler, EventRegistrationToken *token )
{
    struct dispatcher_queue *queue = impl_from_IDispatcherQueue( iface );
    return dispatcher_queue_add_handler( queue, &queue->shutdown_completed, (IUnknown *)handler, token );
}
static HRESULT WINAPI dispatcher_queue_remove_ShutdownCompleted( IDispatcherQueue *iface, EventRegistrationToken token )
{
    struct dispatcher_queue *queue = impl_from_IDispatcherQueue( iface );
    return dispatcher_queue_remove_handler( queue, &queue->shutdown_completed, token );
}

static const struct IDispatcherQueueVtbl dispatcher_queue_vtbl =
{
    dispatcher_queue_QueryInterface,
    dispatcher_queue_AddRef,
    dispatcher_queue_Release,
    dispatcher_queue_GetIids,
    dispatcher_queue_GetRuntimeClassName,
    dispatcher_queue_GetTrustLevel,
    dispatcher_queue_CreateTimer,
    dispatcher_queue_TryEnqueue,
    dispatcher_queue_TryEnqueueWithPriority,
    dispatcher_queue_add_ShutdownStarting,
    dispatcher_queue_remove_ShutdownStarting,
    dispatcher_queue_add_ShutdownCompleted,
    dispatcher_queue_remove_ShutdownCompleted,
};

static HRESULT WINAPI dispatcher_queue2_QueryInterface( IDispatcherQueue2 *iface, REFIID iid, void **out )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue2( iface );
    return IDispatcherQueue_QueryInterface( &impl->IDispatcherQueue_iface, iid, out );
}
static ULONG WINAPI dispatcher_queue2_AddRef( IDispatcherQueue2 *iface )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue2( iface );
    return IDispatcherQueue_AddRef( &impl->IDispatcherQueue_iface );
}
static ULONG WINAPI dispatcher_queue2_Release( IDispatcherQueue2 *iface )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue2( iface );
    return IDispatcherQueue_Release( &impl->IDispatcherQueue_iface );
}
static HRESULT WINAPI dispatcher_queue2_GetIids( IDispatcherQueue2 *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_queue2_GetRuntimeClassName( IDispatcherQueue2 *iface, HSTRING *n ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_queue2_GetTrustLevel( IDispatcherQueue2 *iface, TrustLevel *l ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_queue2_get_HasThreadAccess( IDispatcherQueue2 *iface, boolean *value )
{
    struct dispatcher_queue *impl = impl_from_IDispatcherQueue2( iface );
    if (!value) return E_POINTER;
    *value = (impl->tid == GetCurrentThreadId());
    return S_OK;
}

static const struct IDispatcherQueue2Vtbl dispatcher_queue2_vtbl =
{
    dispatcher_queue2_QueryInterface,
    dispatcher_queue2_AddRef,
    dispatcher_queue2_Release,
    dispatcher_queue2_GetIids,
    dispatcher_queue2_GetRuntimeClassName,
    dispatcher_queue2_GetTrustLevel,
    dispatcher_queue2_get_HasThreadAccess,
};

static HRESULT dispatcher_queue_create_current( struct dispatcher_queue **out )
{
    struct dispatcher_queue *queue;
    HRESULT hr;

    if (!(queue = dispatcher_queue_alloc())) return E_OUTOFMEMORY;
    if (FAILED( hr = dispatcher_queue_bind_thread( queue ) ))
    {
        DeleteCriticalSection( &queue->cs );
        free( queue );
        return hr;
    }
    *out = queue;
    return S_OK;
}

struct dispatcher_queue_controller
{
    IDispatcherQueueController IDispatcherQueueController_iface;
    const void *IDispatcherQueueController2_vtbl;
    struct dispatcher_queue *queue;
    DispatcherQueueOptions options;
    LONG ref;
};

static inline struct dispatcher_queue_controller *impl_from_IDispatcherQueueController( IDispatcherQueueController *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue_controller, IDispatcherQueueController_iface );
}

static inline struct dispatcher_queue_controller *impl_from_IDispatcherQueueController2( void *iface )
{
    return CONTAINING_RECORD( iface, struct dispatcher_queue_controller, IDispatcherQueueController2_vtbl );
}

static const GUID IID_IMicrosoftDispatcherQueueController = {0xbce8178d, 0x2183, 0x584c, {0x9e, 0x5b, 0xf9, 0x36, 0x6f, 0x6a, 0xe4, 0x84}};
static const GUID IID_IMicrosoftDispatcherQueueController2 = {0x4c68ee2a, 0x1cb1, 0x5591, {0xa3, 0xa2, 0x9b, 0x59, 0x0b, 0x8f, 0x8b, 0x9a}};

static HRESULT WINAPI dispatcher_queue_controller_QueryInterface( IDispatcherQueueController *iface, REFIID iid, void **out )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDispatcherQueueController ) ||
        IsEqualGUID( iid, &IID_IMicrosoftDispatcherQueueController ))
    {
        *out = &impl->IDispatcherQueueController_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMicrosoftDispatcherQueueController2 ))
    {
        *out = &impl->IDispatcherQueueController2_vtbl;
        IDispatcherQueueController_AddRef( &impl->IDispatcherQueueController_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI dispatcher_queue_controller2_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController2( iface );
    return IDispatcherQueueController_QueryInterface( &impl->IDispatcherQueueController_iface, iid, out );
}
static ULONG WINAPI dispatcher_queue_controller2_AddRef( void *iface )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController2( iface );
    return IDispatcherQueueController_AddRef( &impl->IDispatcherQueueController_iface );
}
static ULONG WINAPI dispatcher_queue_controller2_Release( void *iface )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController2( iface );
    return IDispatcherQueueController_Release( &impl->IDispatcherQueueController_iface );
}
static HRESULT WINAPI dispatcher_queue_controller2_ShutdownQueue( void *iface )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController2( iface );
    TRACE( "iface %p\n", iface );
    return dispatcher_queue_shutdown( impl->queue );
}
static HRESULT WINAPI dispatcher_queue_controller_GetIids( IDispatcherQueueController *iface, ULONG *iid_count, IID **iids );
static HRESULT WINAPI dispatcher_queue_controller_GetRuntimeClassName( IDispatcherQueueController *iface, HSTRING *class_name );
static HRESULT WINAPI dispatcher_queue_controller_GetTrustLevel( IDispatcherQueueController *iface, TrustLevel *trust_level );

static const void *dispatcher_queue_controller2_vtbl_array[] =
{
    dispatcher_queue_controller2_QueryInterface,
    dispatcher_queue_controller2_AddRef,
    dispatcher_queue_controller2_Release,
    dispatcher_queue_controller_GetIids,
    dispatcher_queue_controller_GetRuntimeClassName,
    dispatcher_queue_controller_GetTrustLevel,
    dispatcher_queue_controller2_ShutdownQueue,
};

static ULONG WINAPI dispatcher_queue_controller_AddRef( IDispatcherQueueController *iface )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing ref to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI dispatcher_queue_controller_Release( IDispatcherQueueController *iface )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing ref to %lu.\n", iface, ref );

    if (!ref)
    {
        if (impl->queue)
        {
            dispatcher_queue_shutdown( impl->queue );
            IDispatcherQueue_Release( &impl->queue->IDispatcherQueue_iface );
        }
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI dispatcher_queue_controller_GetIids( IDispatcherQueueController *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI dispatcher_queue_controller_GetRuntimeClassName( IDispatcherQueueController *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI dispatcher_queue_controller_GetTrustLevel( IDispatcherQueueController *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI dispatcher_queue_controller_get_DispatcherQueue( IDispatcherQueueController *iface, IDispatcherQueue **value )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( iface );
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_POINTER;
    if (!impl->queue)
    {
        if (impl->options.threadType == DQTYPE_THREAD_DEDICATED)
            hr = dispatcher_queue_create_dedicated( &impl->queue );
        else
            hr = dispatcher_queue_create_current( &impl->queue );
        if (FAILED( hr )) return hr;
    }
    IDispatcherQueue_AddRef( &impl->queue->IDispatcherQueue_iface );
    *value = &impl->queue->IDispatcherQueue_iface;
    return S_OK;
}

static HRESULT shutdown_queue_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result, BOOL called_async )
{
    struct dispatcher_queue_controller *impl = impl_from_IDispatcherQueueController( (IDispatcherQueueController *)invoker );
    if (!called_async) return STATUS_PENDING;
    return dispatcher_queue_shutdown( impl->queue );
}

static HRESULT WINAPI dispatcher_queue_controller_ShutdownQueueAsync( IDispatcherQueueController *iface, IAsyncAction **operation )
{
    TRACE( "iface %p, operation %p\n", iface, operation );

    if (!operation) return E_POINTER;
    *operation = NULL;

    return async_action_create( (IUnknown *)iface, shutdown_queue_async, operation );
}

static const struct IDispatcherQueueControllerVtbl dispatcher_queue_controller_vtbl =
{
    dispatcher_queue_controller_QueryInterface,
    dispatcher_queue_controller_AddRef,
    dispatcher_queue_controller_Release,
    /* IInspectable methods */
    dispatcher_queue_controller_GetIids,
    dispatcher_queue_controller_GetRuntimeClassName,
    dispatcher_queue_controller_GetTrustLevel,
    /* IDispatcherQueueController methods */
    dispatcher_queue_controller_get_DispatcherQueue,
    dispatcher_queue_controller_ShutdownQueueAsync,
};

DEFINE_IINSPECTABLE( dispatcher_queue_controller_statics, IDispatcherQueueControllerStatics, struct dispatcher_queue_controller_statics, IActivationFactory_iface )

static HRESULT WINAPI dispatcher_queue_controller_statics_CreateOnDedicatedThread( IDispatcherQueueControllerStatics *iface, IDispatcherQueueController **result )
{
    DispatcherQueueOptions opts = { sizeof(opts), DQTYPE_THREAD_DEDICATED, DQTAT_COM_NONE };
    TRACE( "iface %p, result %p\n", iface, result );
    return CreateDispatcherQueueController( opts, (PDISPATCHERQUEUECONTROLLER *)result );
}

static HRESULT WINAPI dispatcher_queue_controller_statics_CreateOnCurrentThread( IDispatcherQueueControllerStatics *iface, IDispatcherQueueController **result )
{
    DispatcherQueueOptions opts = { sizeof(opts), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
    return CreateDispatcherQueueController( opts, (PDISPATCHERQUEUECONTROLLER *)result );
}

static const void *dispatcher_queue_controller_statics_vtbl[] =
{
    dispatcher_queue_controller_statics_QueryInterface,
    dispatcher_queue_controller_statics_AddRef,
    dispatcher_queue_controller_statics_Release,
    /* IInspectable methods */
    dispatcher_queue_controller_statics_GetIids,
    dispatcher_queue_controller_statics_GetRuntimeClassName,
    dispatcher_queue_controller_statics_GetTrustLevel,
    /* IDispatcherQueueControllerStatics methods */
    dispatcher_queue_controller_statics_CreateOnDedicatedThread,
    dispatcher_queue_controller_statics_CreateOnCurrentThread,
};

static struct dispatcher_queue_controller_statics dispatcher_queue_controller_statics =
{
    {&factory_vtbl},
    {(const struct IDispatcherQueueControllerStaticsVtbl *)dispatcher_queue_controller_statics_vtbl},
    1,
};

static IActivationFactory *dispatcher_queue_controller_factory = &dispatcher_queue_controller_statics.IActivationFactory_iface;

static const GUID IID_IMicrosoftDispatcherQueueStatics = {0xcd3382ea, 0xa455, 0x5124, {0xb6, 0x3a, 0xca, 0x40, 0xd3, 0x4c, 0xa2, 0x3c}};

static HRESULT WINAPI dispatcher_queue_statics_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IDispatcherQueueStatics = {0xa96d83d7, 0x9371, 0x4517, {0x92, 0x45, 0xd0, 0x82, 0x4a, 0xc1, 0x2c, 0x74}};
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ) ||
        IsEqualGUID( iid, &IID_IDispatcherQueueStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftDispatcherQueueStatics ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dispatcher_queue_statics_AddRef( void *iface ) { return 2; }
static ULONG WINAPI dispatcher_queue_statics_Release( void *iface ) { return 1; }
static HRESULT WINAPI dispatcher_queue_statics_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI dispatcher_queue_statics_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Windows.System.DispatcherQueue", 30, n );
}
static HRESULT WINAPI dispatcher_queue_statics_GetTrustLevel( void *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}
static HRESULT WINAPI dispatcher_queue_statics_GetForCurrentThread( void *iface, IDispatcherQueue **result )
{
    struct dispatcher_queue *queue = dispatcher_queue_from_thread( GetCurrentThreadId() );

    if (!result) return E_POINTER;
    if (!queue) return E_FAIL;
    *result = &queue->IDispatcherQueue_iface;
    IDispatcherQueue_AddRef( *result );
    return S_OK;
}

static const void *dispatcher_queue_statics_vtbl[] =
{
    dispatcher_queue_statics_QueryInterface,
    dispatcher_queue_statics_AddRef,
    dispatcher_queue_statics_Release,
    dispatcher_queue_statics_GetIids,
    dispatcher_queue_statics_GetRuntimeClassName,
    dispatcher_queue_statics_GetTrustLevel,
    dispatcher_queue_statics_GetForCurrentThread,
};

static struct { const void *vtbl; } dispatcher_queue_statics = { dispatcher_queue_statics_vtbl };

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *name = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( name, RuntimeClass_Windows_System_DispatcherQueueController ) ||
        !wcscmp( name, L"Microsoft.UI.Dispatching.DispatcherQueueController" ))
        IActivationFactory_QueryInterface( dispatcher_queue_controller_factory, &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( name, L"Windows.System.DispatcherQueue" ) ||
             !wcscmp( name, L"Microsoft.UI.Dispatching.DispatcherQueue" ))
        *factory = (IActivationFactory *)&dispatcher_queue_statics;

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI CreateDispatcherQueueController( DispatcherQueueOptions options, PDISPATCHERQUEUECONTROLLER *queue_controller )
{
    struct dispatcher_queue_controller *impl;

    FIXME( "options.dwSize = %lu, options.threadType = %d, options.apartmentType = %d, queue_controller %p semi-stub!\n",
            options.dwSize, options.threadType, options.apartmentType, queue_controller );

    if (!queue_controller) return E_POINTER;
    if (options.dwSize != sizeof( DispatcherQueueOptions )) return E_INVALIDARG;
    if (options.threadType != DQTYPE_THREAD_DEDICATED && options.threadType != DQTYPE_THREAD_CURRENT) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;

    impl->IDispatcherQueueController_iface.lpVtbl = &dispatcher_queue_controller_vtbl;
    impl->IDispatcherQueueController2_vtbl = dispatcher_queue_controller2_vtbl_array;
    impl->options = options;
    impl->ref = 1;

    *queue_controller = &impl->IDispatcherQueueController_iface;
    TRACE( "created IDispatcherQueueController %p, threadType %d.\n", *queue_controller, options.threadType );
    return S_OK;
}

struct core_ui
{
    IUnknown IUnknown_iface;
    LONG ref;
};

static inline struct core_ui *impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct core_ui, IUnknown_iface );
}

static const struct IUnknownVtbl core_ui_vtbl;

static HRESULT WINAPI core_ui_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    struct core_ui *impl;
    GUID guid;

    /* Vtable-ownership guard: the engine dispatches the vtable slot 0 of an
     * object handed out in a conversation reply as a plain call with its own
     * registers left in place; 'iface' is then the address of the reply slot
     * (not an interface pointer of this object) and 'iid' is the caller's
     * leftover register.  Verify that 'iface' really is this object before
     * touching the struct, probe 'iid' before reading or printing it, and
     * never write through an unverified out-pointer. */
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &core_ui_vtbl) return E_NOINTERFACE;
    if (!probe_refiid( iid, &guid )) return E_POINTER;
    TRACE( "iface %p, iid %s.\n", iface, debugstr_guid( &guid ) );
    impl = impl_from_IUnknown( iface );
    if (IsEqualGUID( &guid, &IID_IUnknown ))
    {
        if (!out) return E_POINTER;
        InterlockedIncrement( &impl->ref );
        *out = &impl->IUnknown_iface;
        return S_OK;
    }
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &guid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI core_ui_AddRef( IUnknown *iface )
{
    /* Ownership guard as in core_ui_QueryInterface: vtable slots 1 and 2 are
     * dispatched on the same path, so 'iface' may name foreign memory.  Do
     * not touch a foreign object's reference count. */
    if (!iface || !iface->lpVtbl || iface->lpVtbl != &core_ui_vtbl) return 1;
    return InterlockedIncrement( &impl_from_IUnknown( iface )->ref );
}

static ULONG WINAPI core_ui_Release( IUnknown *iface )
{
    struct core_ui *impl;
    ULONG ref;

    if (!iface || !iface->lpVtbl || iface->lpVtbl != &core_ui_vtbl) return 0;
    impl = impl_from_IUnknown( iface );
    ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static const struct IUnknownVtbl core_ui_vtbl =
{
    core_ui_QueryInterface,
    core_ui_AddRef,
    core_ui_Release,
};

HRESULT WINAPI CoreUICreate( void **out )
{
    struct core_ui *impl;
    TRACE( "out %p\n", out );
    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IUnknown_iface.lpVtbl = &core_ui_vtbl;
    impl->ref = 1;
    *out = &impl->IUnknown_iface;
    return S_OK;
}

