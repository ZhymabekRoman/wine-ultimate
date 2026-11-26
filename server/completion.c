/*
 * Server-side IO completion ports implementation
 *
 * Copyright (C) 2007 Andrey Turkin
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
 *
 */

/* FIXME: "max concurrent active threads" parameter is not used */

#include "config.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "ntstatus.h"
#include "windef.h"
#include "winternl.h"

#include "object.h"
#include "file.h"
#include "handle.h"
#include "process.h"
#include "request.h"
#include "thread.h"

/* wv2io: one WINEDEBUG channel for the WebView2 transceive wall (see the
 * wv2io comment in server/named_pipe.c for the line format).  GetQueued-
 * CompletionStatus outcomes on the IOCP legs (the mojo/DriverTransport
 * completion packets exchanged between Root and msedgewebview2):
 *   WA_WV2: t=<sec>.<ms> G pid=<winpid> ckey=<16hex> ov=<ptr> st=<ntst> inf=<n>
 */
static int wv2io_on( void )
{
    static int on = -1;
    if (on < 0)
    {
        const char *dbg = getenv( "WINEDEBUG" );
        on = (dbg && strstr( dbg, "wv2io" )) ? 1 : 0;
    }
    return on;
}

static void wv2io_print( const char *fmt, ... )
{
    struct timespec ts;
    va_list args;

    clock_gettime( CLOCK_MONOTONIC, &ts );
    fprintf( stderr, "WA_WV2: t=%u.%03u ", (unsigned int)ts.tv_sec,
             (unsigned int)(ts.tv_nsec / 1000000) );
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
}

#define WV2IO(...) do { if (wv2io_on()) wv2io_print( __VA_ARGS__ ); } while (0)


static const WCHAR completion_name[] = {'I','o','C','o','m','p','l','e','t','i','o','n'};

struct type_descr completion_type =
{
    { completion_name, sizeof(completion_name) },   /* name */
    IO_COMPLETION_ALL_ACCESS,                       /* valid_access */
    {                                               /* mapping */
        STANDARD_RIGHTS_READ | IO_COMPLETION_QUERY_STATE,
        STANDARD_RIGHTS_WRITE | IO_COMPLETION_MODIFY_STATE,
        STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
        IO_COMPLETION_ALL_ACCESS
    },
};

struct comp_msg
{
    struct   list queue_entry;
    apc_param_t   ckey;
    apc_param_t   cvalue;
    apc_param_t   information;
    unsigned int  status;
};

struct completion_wait
{
    struct object      obj;
    obj_handle_t       handle;
    struct completion *completion;
    struct thread     *thread;
    struct comp_msg   *msg;
    struct list        wait_queue_entry;
    struct list        snapshot_entry;  /* entry in a private walk list, see take_waiters_snapshot() */
    int                snapshotted;     /* snapshot_entry is linked and obj holds an extra ref */
};

struct completion
{
    struct object       obj;
    struct object      *sync;
    struct list         queue;
    struct list         wait_queue;
    unsigned int        depth;
    unsigned int        concurrent;
    unsigned int        wait_count;
    int                 shm_idx;
    int                 has_sync_waiter;
};

#define IOCP_SHM_MAGIC          0x494f4350u /* 'IOCP' */
#define IOCP_SHM_VERSION        1u
#define IOCP_SHM_MAX_PORTS      256u
#define IOCP_SHM_RING_SLOTS     256u
#define IOCP_SHM_HANDLE_BUCKETS 1024u
#define IOCP_SHM_HANDLE_WAYS    4u
#define IOCP_SHM_HANDLE_SLOTS   (IOCP_SHM_HANDLE_BUCKETS * IOCP_SHM_HANDLE_WAYS)
#define IOCP_SHM_SPIN_LIMIT     10000

struct iocp_shm_msg
{
    uint64_t ckey;
    uint64_t cvalue;
    uint64_t info;
    uint32_t status;
    uint32_t pad;
};

struct iocp_shm_port
{
    uint32_t seq;
    uint32_t allocated;
    uint64_t gen;

    uint32_t lock;
    uint32_t ring_disabled;
    uint32_t waiting_threads;
    uint32_t has_waiters;
    uint32_t server_depth;
    uint32_t head;
    uint32_t tail;
    uint32_t ring_count;

    struct iocp_shm_msg ring[IOCP_SHM_RING_SLOTS];
};

struct iocp_shm_handle
{
    uint32_t seq;
    uint32_t pid;
    uint32_t handle;
    uint32_t port_idx;
    uint64_t port_gen;
    uint32_t access;
    uint32_t pad;
};

struct iocp_shm
{
    uint32_t magic;
    uint32_t version;
    struct iocp_shm_port   ports[IOCP_SHM_MAX_PORTS];
    struct iocp_shm_handle handles[IOCP_SHM_HANDLE_SLOTS];
};

static struct iocp_shm *iocp_shm = NULL;
static int iocp_shm_inited = 0;

static inline uint32_t iocp_shm_hash_handle( uint32_t pid, uint32_t handle )
{
    uint64_t x = ((uint64_t)pid << 32) | handle;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (uint32_t)x;
}

static inline int iocp_shm_spin_lock( uint32_t *lock )
{
    int i;
    for (i = 0; i < IOCP_SHM_SPIN_LIMIT; i++)
    {
        if (__atomic_exchange_n( lock, 1, __ATOMIC_ACQUIRE ) == 0)
            return 1;
        while (__atomic_load_n( lock, __ATOMIC_RELAXED ))
        {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
    }
    return 0;
}

static inline void iocp_shm_spin_unlock( uint32_t *lock )
{
    __atomic_store_n( lock, 0, __ATOMIC_RELEASE );
}

static void init_iocp_shm(void)
{
    void *ptr;
    uint32_t i;

    if (iocp_shm_inited) return;
    iocp_shm_inited = 1;
    if (!(ptr = server_create_shm_arena( "iocp_shm", sizeof(struct iocp_shm) ))) return;

    iocp_shm = ptr;
    memset( iocp_shm, 0, sizeof(*iocp_shm) );
    for (i = 0; i < IOCP_SHM_HANDLE_SLOTS; i++)
        iocp_shm->handles[i].port_idx = UINT32_MAX;
    iocp_shm->version = IOCP_SHM_VERSION;
    __atomic_store_n( &iocp_shm->magic, IOCP_SHM_MAGIC, __ATOMIC_RELEASE );
}

static void iocp_shm_sync_state( struct completion *completion )
{
    struct iocp_shm_port *sp;

    if (!iocp_shm || !completion || completion->shm_idx < 0 ||
        (uint32_t)completion->shm_idx >= IOCP_SHM_MAX_PORTS)
        return;

    sp = &iocp_shm->ports[completion->shm_idx];
    __atomic_store_n( &sp->waiting_threads, completion->wait_count, __ATOMIC_RELEASE );
    __atomic_store_n( &sp->has_waiters, completion->has_sync_waiter ? 1u : 0u, __ATOMIC_RELEASE );
    __atomic_store_n( &sp->server_depth, completion->depth, __ATOMIC_RELEASE );
}

static void iocp_shm_alloc_slot( struct completion *completion )
{
    static uint32_t next_hint = 0;
    uint32_t i;

    if (!iocp_shm || completion->shm_idx >= 0) return;

    for (i = 0; i < IOCP_SHM_MAX_PORTS; i++)
    {
        uint32_t idx = (next_hint + i) % IOCP_SHM_MAX_PORTS;
        struct iocp_shm_port *sp = &iocp_shm->ports[idx];
        if (!sp->allocated)
        {
            uint32_t s = sp->seq;
            next_hint = (idx + 1) % IOCP_SHM_MAX_PORTS;
            __atomic_store_n( &sp->seq, s + 1, __ATOMIC_RELEASE );
            sp->gen++;
            sp->allocated       = 1;
            sp->lock            = 0;
            sp->ring_disabled   = 0;
            sp->waiting_threads = 0;
            sp->has_waiters     = 0;
            sp->server_depth    = 0;
            sp->head            = 0;
            sp->tail            = 0;
            sp->ring_count      = 0;
            __atomic_store_n( &sp->seq, s + 2, __ATOMIC_RELEASE );
            completion->shm_idx = (int)idx;
            iocp_shm_sync_state( completion );
            return;
        }
    }
}

static void iocp_shm_free_slot( struct completion *completion )
{
    struct iocp_shm_port *sp;
    uint32_t s;

    if (!iocp_shm || completion->shm_idx < 0 ||
        (uint32_t)completion->shm_idx >= IOCP_SHM_MAX_PORTS)
        return;

    sp = &iocp_shm->ports[completion->shm_idx];
    iocp_shm_spin_lock( &sp->lock );
    s = sp->seq;
    __atomic_store_n( &sp->seq, s + 1, __ATOMIC_RELEASE );
    sp->allocated       = 0;
    sp->ring_disabled   = 1;
    sp->gen++;
    sp->waiting_threads = 0;
    sp->has_waiters     = 0;
    sp->server_depth    = 0;
    sp->head            = 0;
    sp->tail            = 0;
    sp->ring_count      = 0;
    __atomic_store_n( &sp->seq, s + 2, __ATOMIC_RELEASE );
    iocp_shm_spin_unlock( &sp->lock );
    completion->shm_idx = -1;
}

static void iocp_shm_register_handle( struct process *process, obj_handle_t handle,
                                      struct completion *completion, unsigned int access )
{
    uint32_t pid, hval, h, base, way;
    struct iocp_shm_handle *slot = NULL;
    uint32_t s;

    if (!iocp_shm || !process || !handle || completion->shm_idx < 0) return;
    pid  = get_process_id( process );
    hval = (uint32_t)handle;
    h    = iocp_shm_hash_handle( pid, hval );
    base = (h % IOCP_SHM_HANDLE_BUCKETS) * IOCP_SHM_HANDLE_WAYS;

    for (way = 0; way < IOCP_SHM_HANDLE_WAYS; way++)
    {
        struct iocp_shm_handle *cand = &iocp_shm->handles[base + way];
        if (cand->port_idx != UINT32_MAX && cand->pid == pid && cand->handle == hval)
        {
            slot = cand;
            break;
        }
        if (!slot && cand->port_idx == UINT32_MAX)
            slot = cand;
    }
    if (!slot) slot = &iocp_shm->handles[base + ((h >> 16) & (IOCP_SHM_HANDLE_WAYS - 1))];

    s = slot->seq;
    __atomic_store_n( &slot->seq, s + 1, __ATOMIC_RELEASE );
    slot->pid      = pid;
    slot->handle   = hval;
    slot->port_idx = (uint32_t)completion->shm_idx;
    slot->port_gen = iocp_shm->ports[completion->shm_idx].gen;
    slot->access   = access;
    __atomic_store_n( &slot->seq, s + 2, __ATOMIC_RELEASE );
}

static void iocp_shm_unregister_handle( struct process *process, obj_handle_t handle )
{
    uint32_t pid, hval, h, base, way;

    if (!iocp_shm || !process || !handle) return;
    pid  = get_process_id( process );
    hval = (uint32_t)handle;
    h    = iocp_shm_hash_handle( pid, hval );
    base = (h % IOCP_SHM_HANDLE_BUCKETS) * IOCP_SHM_HANDLE_WAYS;

    for (way = 0; way < IOCP_SHM_HANDLE_WAYS; way++)
    {
        struct iocp_shm_handle *cand = &iocp_shm->handles[base + way];
        if (cand->port_idx != UINT32_MAX && cand->pid == pid && cand->handle == hval)
        {
            uint32_t s = cand->seq;
            __atomic_store_n( &cand->seq, s + 1, __ATOMIC_RELEASE );
            cand->pid      = 0;
            cand->handle   = 0;
            cand->port_idx = UINT32_MAX;
            cand->port_gen = 0;
            cand->access   = 0;
            __atomic_store_n( &cand->seq, s + 2, __ATOMIC_RELEASE );
        }
    }
}

static void iocp_shm_flush_to_queue( struct completion *completion )
{
    struct iocp_shm_port *sp;

    if (!iocp_shm || !completion || completion->shm_idx < 0 ||
        (uint32_t)completion->shm_idx >= IOCP_SHM_MAX_PORTS)
        return;

    sp = &iocp_shm->ports[completion->shm_idx];
    if (!__atomic_load_n( &sp->ring_count, __ATOMIC_ACQUIRE )) return;

    if (!iocp_shm_spin_lock( &sp->lock ))
    {
        sp->ring_disabled = 1;
        return;
    }

    while (sp->ring_count > 0)
    {
        struct iocp_shm_msg *smsg = &sp->ring[sp->head % IOCP_SHM_RING_SLOTS];
        struct comp_msg *cmsg = mem_alloc( sizeof(*cmsg) );
        if (!cmsg) break;
        cmsg->ckey        = smsg->ckey;
        cmsg->cvalue      = smsg->cvalue;
        cmsg->information = smsg->info;
        cmsg->status      = smsg->status;
        list_add_tail( &completion->queue, &cmsg->queue_entry );
        completion->depth++;
        sp->head = (sp->head + 1) % IOCP_SHM_RING_SLOTS;
        sp->ring_count--;
    }
    __atomic_store_n( &sp->server_depth, completion->depth, __ATOMIC_RELEASE );
    iocp_shm_spin_unlock( &sp->lock );
}

static void completion_wait_dump( struct object*, int );
static int completion_wait_signaled( struct object *obj, struct wait_queue_entry *entry );
static void completion_wait_satisfied( struct object *obj, struct wait_queue_entry *entry );
static void completion_wait_destroy( struct object * );
static int completion_wait_close_handle( struct object *obj, struct process *process, obj_handle_t handle );

static const struct object_ops completion_wait_ops =
{
    .size         = sizeof(struct completion_wait),
    .type         = &no_type,
    .dump         = completion_wait_dump,
    .add_queue    = add_queue,
    .remove_queue = remove_queue,
    .signaled     = completion_wait_signaled,
    .satisfied    = completion_wait_satisfied,
    .close_handle = completion_wait_close_handle,
    .destroy      = completion_wait_destroy,
};

static int completion_wait_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
    struct completion_wait *wait = (struct completion_wait *)obj;

    if (wait->completion)
    {
        struct completion *comp = wait->completion;
        list_remove( &wait->wait_queue_entry );
        wait->completion = NULL;
        if (comp->wait_count) comp->wait_count--;
        iocp_shm_sync_state( comp );
    }
    wait->handle = 0;
    if (wait->thread && wait->thread->completion_wait == wait)
        wait->thread->completion_wait = NULL;
    return 1;
}

static void completion_wait_destroy( struct object *obj )
{
    struct completion_wait *wait = (struct completion_wait *)obj;

    if (wait->completion)
    {
        struct completion *comp = wait->completion;
        list_remove( &wait->wait_queue_entry );
        wait->completion = NULL;
        if (comp->wait_count) comp->wait_count--;
        iocp_shm_sync_state( comp );
    }
    free( wait->msg );
}

static void completion_wait_dump( struct object *obj, int verbose )
{
    struct completion_wait *wait = (struct completion_wait *)obj;

    assert( obj->ops == &completion_wait_ops );
    fprintf( stderr, "Completion wait completion=%p\n", wait->completion );
}

static int completion_wait_signaled( struct object *obj, struct wait_queue_entry *entry )
{
    struct completion_wait *wait = (struct completion_wait *)obj;

    assert( obj->ops == &completion_wait_ops );
    if (!wait->completion) return 1;
    return wait->completion->depth;
}

static void completion_wait_satisfied( struct object *obj, struct wait_queue_entry *entry )
{
    struct completion_wait *wait = (struct completion_wait *)obj;
    struct list *msg_entry;
    struct comp_msg *msg;

    assert( obj->ops == &completion_wait_ops );
    if (!wait->completion)
    {
        make_wait_abandoned( entry );
        return;
    }
    msg_entry = list_head( &wait->completion->queue );
    assert( msg_entry );
    msg = LIST_ENTRY( msg_entry, struct comp_msg, queue_entry );
    --wait->completion->depth;
    list_remove( &msg->queue_entry );
    iocp_shm_sync_state( wait->completion );
    if (wait->msg) free( wait->msg );
    wait->msg = msg;
}

struct completion_init_data
{
    unsigned int concurrent;
};

static void completion_dump( struct object*, int );
static bool completion_init( struct object *obj, const void *init_data );
static struct object *completion_get_sync( struct object * );
static int completion_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
static void completion_destroy( struct object * );

static const struct object_ops completion_ops =
{
    .size         = sizeof(struct completion),
    .type         = &completion_type,
    .dump         = completion_dump,
    .init         = completion_init,
    .get_sync     = completion_get_sync,
    .close_handle = completion_close_handle,
    .destroy      = completion_destroy,
};

static void completion_destroy( struct object *obj)
{
    struct completion *completion = (struct completion *) obj;
    struct comp_msg *tmp, *next;
    struct completion_wait *wait, *wait_next;

    LIST_FOR_EACH_ENTRY_SAFE( tmp, next, &completion->queue, struct comp_msg, queue_entry )
    {
        free( tmp );
    }

    LIST_FOR_EACH_ENTRY_SAFE( wait, wait_next, &completion->wait_queue, struct completion_wait, wait_queue_entry )
    {
        wait->completion = NULL;
        list_remove( &wait->wait_queue_entry );
    }
    completion->wait_count = 0;
    iocp_shm_free_slot( completion );

    if (completion->sync) release_object( completion->sync );
}

static void completion_dump( struct object *obj, int verbose )
{
    struct completion *completion = (struct completion *) obj;

    assert( obj->ops == &completion_ops );
    fprintf( stderr, "Completion depth=%u\n", completion->depth );
}

static bool completion_init( struct object *obj, const void *init_data )
{
    struct completion *completion = (struct completion *)obj;
    const struct completion_init_data *data = init_data;

    completion->depth = 0;
    completion->concurrent = data->concurrent;
    completion->wait_count = 0;
    completion->shm_idx = -1;
    completion->has_sync_waiter = 0;
    list_init( &completion->queue );
    list_init( &completion->wait_queue );
    if (!(completion->sync = create_internal_sync( 1, 0 ))) return false;
    init_iocp_shm();
    iocp_shm_alloc_slot( completion );
    return true;
}

static struct object *completion_get_sync( struct object *obj )
{
    struct completion *completion = (struct completion *)obj;
    assert( obj->ops == &completion_ops );
    iocp_shm_flush_to_queue( completion );
    completion->has_sync_waiter = 1;
    if (!list_empty( &completion->queue )) signal_sync( completion->sync );
    iocp_shm_sync_state( completion );
    return grab_object( completion->sync );
}

/* Pin the current waiters of a completion port onto a private walk list.
 *
 * Waking a waiter completes its request, and completing that request can close the
 * completion-wait handle and release the object.  When the woken thread dies on the
 * way - a closed wakeup socket kills it - its whole process is torn down
 * (terminate_process -> kill_thread -> remove_process_thread -> process_killed ->
 * release_job_process -> add_completion), which releases the completion-waits owned
 * by that process' other threads as well.  All of those objects are unlinked from
 * this completion's wait queue while a caller is walking that queue, so walking it
 * directly follows pointers into freed memory.  Holding a reference per waiter keeps
 * the objects alive across the walk; the per-object flag stops a nested
 * add_completion() from re-linking a list node that is already on a snapshot list.
 */
static void take_waiters_snapshot( struct completion *completion, struct list *waiting )
{
    struct completion_wait *wait;

    list_init( waiting );
    LIST_FOR_EACH_ENTRY( wait, &completion->wait_queue, struct completion_wait, wait_queue_entry )
    {
        if (wait->snapshotted) continue;
        wait->snapshotted = 1;
        grab_object( &wait->obj );
        list_add_tail( waiting, &wait->snapshot_entry );
    }
}

static void release_waiters_snapshot( struct list *waiting )
{
    struct completion_wait *wait, *next;

    LIST_FOR_EACH_ENTRY_SAFE( wait, next, waiting, struct completion_wait, snapshot_entry )
    {
        list_remove( &wait->snapshot_entry );
        wait->snapshotted = 0;
        release_object( &wait->obj );
    }
}

static int completion_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
    struct completion *completion = (struct completion *)obj;
    struct completion_wait *wait, *wait_next;
    struct list waiting;

    iocp_shm_unregister_handle( process, handle );
    if (completion->obj.handle_count != 1) return 1;

    /* cleanup_thread_completion() below releases the object, and the wake can tear the
     * woken thread's process down, releasing other waiters of this port the same way;
     * walk a refcounted snapshot of the queue instead of the queue itself. */
    take_waiters_snapshot( completion, &waiting );
    LIST_FOR_EACH_ENTRY_SAFE( wait, wait_next, &waiting, struct completion_wait, snapshot_entry )
    {
        if (!wait->completion) continue;
        wait->completion = NULL;
        list_remove( &wait->wait_queue_entry );
        if (completion->wait_count) completion->wait_count--;
        if (!wait->msg)
        {
            wake_up( &wait->obj, 0 );
            cleanup_thread_completion( wait->thread );
        }
    }
    release_waiters_snapshot( &waiting );
    iocp_shm_sync_state( completion );
    signal_sync( completion->sync );
    return 1;
}

void cleanup_thread_completion( struct thread *thread )
{
    struct completion_wait *wait = thread->completion_wait;
    obj_handle_t handle;

    if (!wait) return;

    thread->completion_wait = NULL;
    if (wait->completion)
    {
        struct completion *comp = wait->completion;
        list_remove( &wait->wait_queue_entry );
        wait->completion = NULL;
        if (comp->wait_count) comp->wait_count--;
        iocp_shm_sync_state( comp );
    }
    if ((handle = wait->handle))
    {
        wait->handle = 0;
        close_handle( thread->process, handle );
    }
    release_object( &wait->obj );
}

static struct completion_wait *create_completion_wait( struct thread *thread )
{
    struct completion_wait *wait;

    if (!(wait = alloc_object( &completion_wait_ops ))) return NULL;
    wait->completion = NULL;
    wait->thread = thread;
    wait->msg = NULL;
    wait->snapshotted = 0;
    list_init( &wait->wait_queue_entry );
    list_init( &wait->snapshot_entry );
    if (!(wait->handle = alloc_handle( current->process, wait, SYNCHRONIZE, 0 )))
    {
        release_object( &wait->obj );
        return NULL;
    }
    return wait;
}

struct completion *get_completion_obj( struct process *process, obj_handle_t handle, unsigned int access )
{
    return (struct completion *) get_handle_obj( process, handle, access, &completion_ops );
}

void add_completion( struct completion *completion, apc_param_t ckey, apc_param_t cvalue,
                     unsigned int status, apc_param_t information )
{
    struct comp_msg *msg;
    struct completion_wait *wait, *wait_next;
    struct list waiting;

    iocp_shm_flush_to_queue( completion );

    msg = mem_alloc( sizeof( *msg ) );
    if (!msg)
        return;

    msg->ckey = ckey;
    msg->cvalue = cvalue;
    msg->status = status;
    msg->information = information;

    list_add_tail( &completion->queue, &msg->queue_entry );
    completion->depth++;
    take_waiters_snapshot( completion, &waiting );
    LIST_FOR_EACH_ENTRY_SAFE( wait, wait_next, &waiting, struct completion_wait, snapshot_entry )
    {
        if (wait->completion) wake_up( &wait->obj, 1 );
        if (wait->msg)
        {
            if (wait->completion)
            {
                list_remove( &wait->wait_queue_entry );
                wait->completion = NULL;
                if (completion->wait_count) completion->wait_count--;
            }
            list_remove( &wait->snapshot_entry );
            wait->snapshotted = 0;
            release_object( &wait->obj );
        }
        if (list_empty( &completion->queue )) break;
    }
    release_waiters_snapshot( &waiting );
    if (!list_empty( &completion->queue )) signal_sync( completion->sync );
    iocp_shm_sync_state( completion );
}

/* create a completion */
DECL_HANDLER(create_completion)
{
    struct completion_init_data data = { .concurrent = req->concurrent };
    struct object_params params = { .ops = &completion_ops, .access = req->access, .init_data = &data };

    if (!get_req_object_attributes( &params )) return;
    reply->handle = create_named_obj_handle( current->process, &params );
    if (reply->handle)
    {
        struct completion *completion = get_completion_obj( current->process, reply->handle, 0 );
        if (completion)
        {
            iocp_shm_register_handle( current->process, reply->handle, completion,
                                      default_map_access( &completion->obj, req->access ) );
            release_object( completion );
        }
    }
    if (params.root) release_object( params.root );
}

/* open a completion */
DECL_HANDLER(open_completion)
{
    reply->handle = open_object( current->process, req->rootdir, req->access,
                                 &completion_ops, get_req_unicode_str(), req->attributes );
    if (reply->handle)
    {
        struct completion *completion = get_completion_obj( current->process, reply->handle, 0 );
        if (completion)
        {
            iocp_shm_register_handle( current->process, reply->handle, completion,
                                      default_map_access( &completion->obj, req->access ) );
            release_object( completion );
        }
    }
}


/* add completion to completion port */
DECL_HANDLER(add_completion)
{
    struct completion* completion = get_completion_obj( current->process, req->handle, IO_COMPLETION_MODIFY_STATE );
    struct reserve *reserve = NULL;

    if (!completion) return;

    if (req->reserve_handle && !(reserve = get_completion_reserve_obj( current->process, req->reserve_handle, 0 )))
    {
        release_object( completion );
        return;
    }

    add_completion( completion, req->ckey, req->cvalue, req->status, req->information );

    if (reserve) release_object( reserve );
    release_object( completion );
}

/* get completion from completion port */
DECL_HANDLER(remove_completion)
{
    struct completion* completion = get_completion_obj( current->process, req->handle, IO_COMPLETION_MODIFY_STATE );
    struct list *entry;
    struct comp_msg *msg;

    if (!completion) return;

    iocp_shm_flush_to_queue( completion );
    entry = list_head( &completion->queue );
    if (req->alertable && !list_empty( &current->user_apc )
        && !(entry && current->completion_wait && current->completion_wait->completion == completion))
    {
        set_error( STATUS_USER_APC );
        iocp_shm_sync_state( completion );
        release_object( completion );
        return;
    }
    if (current->completion_wait)
    {
        struct completion *old_comp = current->completion_wait->completion;
        list_remove( &current->completion_wait->wait_queue_entry );
        current->completion_wait->completion = NULL;
        if (old_comp)
        {
            if (old_comp->wait_count) old_comp->wait_count--;
            if (old_comp != completion) iocp_shm_sync_state( old_comp );
        }
    }
    else if (!(current->completion_wait = create_completion_wait( current )))
    {
        iocp_shm_sync_state( completion );
        release_object( completion );
        return;
    }
    if (!entry)
    {
        current->completion_wait->completion = completion;
        list_add_head( &completion->wait_queue, &current->completion_wait->wait_queue_entry );
        completion->wait_count++;
        reply->wait_handle = current->completion_wait->handle;
        /* A packet consumed through the wake path (completion_wait_satisfied
         * handing the message to the thread, get_thread_completion taking it)
         * leaves the queue empty without clearing the sync object that the
         * deferred remove_completion protocol parks polling clients on.  The
         * stale signal then completes every subsequent arm of that wait
         * immediately: a client-side spin that saturates the server with
         * PENDING round-trips on the thread that owns a message pump.  The
         * queue is empty at this point, so take the stale signal back;
         * add_completion() signals it again when a packet actually arrives. */
        if (list_empty( &completion->queue )) reset_sync( completion->sync );
        set_error( STATUS_PENDING );
    }
    else
    {
        list_remove( entry );
        completion->depth--;
        msg = LIST_ENTRY( entry, struct comp_msg, queue_entry );
        reply->ckey = msg->ckey;
        reply->cvalue = msg->cvalue;
        reply->status = msg->status;
        reply->information = msg->information;
        WV2IO( "G pid=%04x ckey=%08x%08x ov=%p st=%x inf=%u\n", current->process->id,
               (unsigned int)(msg->ckey >> 32), (unsigned int)msg->ckey,
               (void *)(client_ptr_t)msg->cvalue, msg->status, msg->information );
        free( msg );
        reply->wait_handle = 0;
        if (list_empty( &completion->queue )) reset_sync( completion->sync );
    }

    iocp_shm_sync_state( completion );
    release_object( completion );
}

/* get completion after successful waiting for it */
DECL_HANDLER(get_thread_completion)
{
    struct comp_msg *msg;

    if (!current->completion_wait || !(msg = current->completion_wait->msg))
    {
        set_error( STATUS_INVALID_HANDLE );
        return;
    }

    reply->ckey = msg->ckey;
    reply->cvalue = msg->cvalue;
    reply->status = msg->status;
    reply->information = msg->information;
    free( msg );
    current->completion_wait->msg = NULL;
    if (!current->completion_wait->completion) cleanup_thread_completion( current );
}

/* get queue depth for completion port */
DECL_HANDLER(query_completion)
{
    struct completion* completion = get_completion_obj( current->process, req->handle, IO_COMPLETION_QUERY_STATE );

    if (!completion) return;

    iocp_shm_flush_to_queue( completion );
    reply->depth = completion->depth;
    iocp_shm_sync_state( completion );

    release_object( completion );
}


/* wait completion packet: posts an I/O completion packet when a target object
 * becomes signaled. Used by NtCreateWaitCompletionPacket et al. */

struct wait_completion_packet
{
    struct object       obj;            /* object header */
    struct list         entry;          /* entry in wait_packet_list while associated */
    struct completion  *completion;     /* target completion port (ref held) */
    struct object      *target;         /* object the wake signals (ref held) */
    struct object      *receive_port;   /* port whose receive in flight at association
                                         * time is the wake's completion source (ref held) */
    apc_param_t         ckey;           /* completion key */
    apc_param_t         cvalue;         /* completion value (apc context) */
    unsigned int        status;         /* completion status */
    apc_param_t         information;     /* completion information */
    int                 wake_up_app;    /* an I/O wake signals the target too */
    int                 associated;     /* currently on wait_packet_list */
};

static struct list wait_packet_list = LIST_INIT(wait_packet_list);

static void wait_completion_packet_dump( struct object *obj, int verbose );
static void wait_completion_packet_destroy( struct object *obj );

static const struct object_ops wait_completion_packet_ops =
{
    .size    = sizeof(struct wait_completion_packet),
    .type    = &no_type,
    .dump    = wait_completion_packet_dump,
    .destroy = wait_completion_packet_destroy,
};

static void wait_completion_packet_dump( struct object *obj, int verbose )
{
    struct wait_completion_packet *packet = (struct wait_completion_packet *)obj;
    fprintf( stderr, "Wait completion packet target=%p completion=%p\n",
             packet->target, packet->completion );
}

static void wait_packet_disassociate( struct wait_completion_packet *packet )
{
    if (!packet->associated) return;
    list_remove( &packet->entry );
    if (packet->completion) { release_object( packet->completion ); packet->completion = NULL; }
    if (packet->target) { release_object( packet->target ); packet->target = NULL; }
    if (packet->receive_port) { release_object( packet->receive_port ); packet->receive_port = NULL; }
    packet->associated = 0;
}

/* Fire one associated packet: the packet completion is delivered to the I/O
 * completion object, and the packet disassociates (autorepeat is off — the
 * only mode this interface drives — so the app re-associates it at the end of
 * every dispatch cycle).
 *
 * Signalling the target belongs to one wake source only.  When the wake's
 * source is an I/O — the receive the packet was bound to — the completion of
 * that I/O is what the kernel reports, and the target (the event the
 * dispatcher parks on) has to be signaled for the parked thread to wake,
 * because posting an I/O completion packet alone never wakes it.  When the
 * wake's source is the target's own signal the target is signaled already:
 * signalling it again re-arms the very condition that fired the wake, and an
 * app that re-associates the packet after every dispatch cycle then fires it
 * forever, at the cost of the thread that owns the wake. */
static void wake_packet_fire( struct wait_completion_packet *packet, int signal_target )
{
    if (debug_level)
        fprintf( stderr, "WAKE_PACKET_FIRE: packet=%p target=%p comp=%p key=%lx apc=%lx sig=%d\n",
                 packet, packet->target, packet->completion, (unsigned long)packet->ckey, (unsigned long)packet->cvalue, signal_target );
    if (signal_target && packet->target)
    {
        struct object *sync = get_obj_sync( packet->target );
        if (sync)
        {
            signal_sync( sync );
            release_object( sync );
        }
    }
    if (packet->completion)
        add_completion( packet->completion, packet->ckey, packet->cvalue,
                        packet->status, packet->information );
    wait_packet_disassociate( packet );
}

static void wait_completion_packet_destroy( struct object *obj )
{
    struct wait_completion_packet *packet = (struct wait_completion_packet *)obj;
    wait_packet_disassociate( packet );
}

/* called when a target object becomes signaled; fires the packets
 * associated with it.  The signal that fires the wake is the caller's, so the
 * target is signaled already and is not signalled again here. */
void signal_wait_completion_packets( struct object *target )
{
    struct wait_completion_packet *packet, *next;

    if (debug_level)
        fprintf( stderr, "SIGNAL_WAIT_COMPLETION_PACKETS: target=%p\n", target );
    LIST_FOR_EACH_ENTRY_SAFE( packet, next, &wait_packet_list, struct wait_completion_packet, entry )
    {
        if (packet->target != target) continue;
        if (debug_level)
            fprintf( stderr, "  -> firing packet %p for target %p\n", packet, target );
        wake_packet_fire( packet, 0 );
    }
}

/* called when a message is queued on an ALPC port: readiness of the port is
 * completion of the IRP of the receive armed on it, which fires the wake
 * packets bound to that port at association time.  Completing an I/O is the
 * one wake source that also signals the target, and only when the app asked
 * for it (SignalAndWakeUpApp). */
void fire_wait_completion_packets( struct object *port )
{
    struct wait_completion_packet *packet, *next;

    if (debug_level)
        fprintf( stderr, "FIRE_WAIT_COMPLETION_PACKETS: port=%p\n", port );
    LIST_FOR_EACH_ENTRY_SAFE( packet, next, &wait_packet_list, struct wait_completion_packet, entry )
    {
        if (packet->receive_port != port) continue;
        if (debug_level)
            fprintf( stderr, "  -> firing packet %p for receive_port %p\n", packet, port );
        wake_packet_fire( packet, packet->wake_up_app );
    }
}

DECL_HANDLER(create_wait_completion_packet)
{
    struct wait_completion_packet *packet;
    struct object_params params;

    if (!get_req_object_attributes( &params )) return;

    if ((packet = alloc_object( &wait_completion_packet_ops )))
    {
        packet->completion   = NULL;
        packet->target       = NULL;
        packet->receive_port = NULL;
        packet->wake_up_app  = 0;
        packet->associated   = 0;
        list_init( &packet->entry );
        reply->handle = alloc_handle( current->process, packet, req->access, params.attr );
        release_object( packet );
    }

    if (params.root) release_object( params.root );
}

DECL_HANDLER(associate_wait_completion_packet)
{
    struct wait_completion_packet *packet;
    struct completion *completion;
    struct object *target;

    packet = (struct wait_completion_packet *)get_handle_obj( current->process, req->handle,
                                                              0, &wait_completion_packet_ops );
    if (!packet) return;

    /* one association per packet */
    wait_packet_disassociate( packet );

    completion = get_completion_obj( current->process, req->completion, IO_COMPLETION_MODIFY_STATE );
    if (!completion)
    {
        release_object( packet );
        return;
    }

    target = get_handle_obj( current->process, req->target, SYNCHRONIZE, NULL );
    if (!target)
    {
        release_object( completion );
        release_object( packet );
        return;
    }

    packet->completion   = completion;  /* ref held */
    packet->target       = target;      /* ref held */
    packet->ckey         = req->key;
    packet->cvalue       = req->apc;
    packet->status       = req->status;
    packet->information  = req->information;
    packet->wake_up_app  = req->wake_up_app;
    packet->associated   = 1;
    list_add_tail( &wait_packet_list, &packet->entry );

    /* the wake's completion source is the receive in flight on the
     * associating thread: the kernel binds the packet to that IRP, so
     * readiness of the port this thread is receiving on fires the packet */
    packet->receive_port = find_thread_alpc_receive_port( current );

    /* the kernel fires immediately when the wake is already complete at
     * association time, i.e. the target object is signaled.  A receive that
     * is still in flight is not a completed wake — its completion is what
     * fires the packet later — so the bound port is not examined here.  The
     * fire consumes the signal that completed the wait, so re-arming the
     * packet after processing reports only a wake that completed again. */
    reply->signaled = 0;
    {
        struct object *sync = get_obj_sync( target );
        int ready = (sync && sync->ops->signaled && sync->ops->signaled( sync, NULL ));
        if (ready)
        {
            reply->signaled = 1;
            if (sync) reset_sync( sync );  /* the wake takes the signal */
            wake_packet_fire( packet, 0 );
        }
        if (sync) release_object( sync );
    }

    release_object( packet );
}

DECL_HANDLER(cancel_wait_completion_packet)
{
    struct wait_completion_packet *packet;

    packet = (struct wait_completion_packet *)get_handle_obj( current->process, req->handle,
                                                              0, &wait_completion_packet_ops );
    if (!packet) return;

    reply->cancelled = packet->associated ? 1 : 0;
    wait_packet_disassociate( packet );

    release_object( packet );
}
