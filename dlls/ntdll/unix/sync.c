/*
 * Process synchronisation
 *
 * Copyright 1996, 1997, 1998 Marcus Meissner
 * Copyright 1997, 1999 Alexandre Julliard
 * Copyright 1999, 2000 Juergen Schmied
 * Copyright 2003 Eric Pouech
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#ifdef HAVE_SCHED_H
# include <sched.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
# include <mach/mach_time.h>
#endif
#ifdef HAVE_KQUEUE
# include <sys/event.h>
#endif
#ifdef HAVE_LINUX_NTSYNC_H
# include <linux/ntsync.h>
#endif

#include "ntstatus.h"
#include "windef.h"
#include "winternl.h"
#include "ddk/wdm.h"
#include "wine/server.h"
#include "wine/debug.h"
#include "unix_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(sync);

HANDLE keyed_event = 0;
int inproc_device_fd = -1;

static const char *debugstr_timeout( const LARGE_INTEGER *timeout )
{
    if (!timeout) return "(infinite)";
    return wine_dbg_sprintf( "%lld.%07ld", (long long)(timeout->QuadPart / TICKSPERSEC),
                             (long)(timeout->QuadPart % TICKSPERSEC) );
}


/* return a monotonic time counter, in Win32 ticks */
static inline ULONGLONG monotonic_counter(void)
{
    struct timeval now;
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase;

    if (!timebase.denom) mach_timebase_info( &timebase );
    return mach_continuous_time() * timebase.numer / timebase.denom / 100;
#elif defined(HAVE_CLOCK_GETTIME)
    struct timespec ts;
#ifdef CLOCK_BOOTTIME
    if (!clock_gettime( CLOCK_BOOTTIME, &ts ))
        return ts.tv_sec * (ULONGLONG)TICKSPERSEC + ts.tv_nsec / 100;
#endif
#ifdef CLOCK_MONOTONIC_RAW
    if (!clock_gettime( CLOCK_MONOTONIC_RAW, &ts ))
        return ts.tv_sec * (ULONGLONG)TICKSPERSEC + ts.tv_nsec / 100;
#endif
    if (!clock_gettime( CLOCK_MONOTONIC, &ts ))
        return ts.tv_sec * (ULONGLONG)TICKSPERSEC + ts.tv_nsec / 100;
#endif
    gettimeofday( &now, 0 );
    return ticks_from_time_t( now.tv_sec ) + now.tv_usec * 10 - server_start_time;
}

#ifdef __linux__

#define USE_FUTEX

#include <linux/futex.h>

static inline int futex_wait( const LONG *addr, int val, struct timespec *timeout )
{
#if (defined(__i386__) || defined(__arm__)) && _TIME_BITS==64
    if (timeout && sizeof(*timeout) != 8)
    {
        struct {
            long tv_sec;
            long tv_nsec;
        } timeout32 = { timeout->tv_sec, timeout->tv_nsec };

        return syscall( __NR_futex, addr, FUTEX_WAIT_PRIVATE, val, &timeout32, 0, 0 );
    }
#endif
    return syscall( __NR_futex, addr, FUTEX_WAIT_PRIVATE, val, timeout, 0, 0 );
}

static inline int futex_wake_one( const LONG *addr )
{
    return syscall( __NR_futex, addr, FUTEX_WAKE_PRIVATE, 1, NULL, 0, 0 );
}

static inline int futex_wake_n( const LONG *addr, int count )
{
    return syscall( __NR_futex, addr, FUTEX_WAKE_PRIVATE, count, NULL, 0, 0 );
}

#elif defined(__APPLE__)

#define USE_FUTEX

#include <AvailabilityMacros.h>

#ifdef MAC_OS_VERSION_14_4
#include <os/os_sync_wait_on_address.h>
#endif

#define UL_COMPARE_AND_WAIT 1

extern int __ulock_wait( uint32_t operation, void *addr, uint64_t value, uint32_t timeout );

extern int __ulock_wake( uint32_t operation, void *addr, uint64_t wake_value );

static inline int futex_wait( const LONG *addr, int val, struct timespec *timeout )
{
#ifdef MAC_OS_VERSION_14_4
    if (__builtin_available( macOS 14.4, * ))
    {
        /* 18446744073 seconds could overflow a uint64_t in nanoseconds */
        if (timeout && timeout->tv_sec < 18446744073)
        {
            uint64_t ns_timeout = (timeout->tv_sec * 1000000000) + timeout->tv_nsec;

            if (!ns_timeout)
            {
                errno = ETIMEDOUT;
                return -1;
            }
            return os_sync_wait_on_address_with_timeout( (void *)addr, (uint64_t)val, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                                         OS_CLOCK_MACH_ABSOLUTE_TIME, ns_timeout );
        }

        return os_sync_wait_on_address( (void *)addr, (uint64_t)val, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE );
    }
#endif

    /* 4294 seconds could overflow a uint32_t in microseconds */
    if (timeout && timeout->tv_sec < 4294)
    {
        uint32_t us_timeout = ((uint32_t)timeout->tv_sec * 1000000) + ((uint32_t)timeout->tv_nsec / 1000);

        if (!us_timeout)
        {
            errno = ETIMEDOUT;
            return -1;
        }
        return __ulock_wait( UL_COMPARE_AND_WAIT, (void *)addr, (uint64_t)val, us_timeout );
    }

    return __ulock_wait( UL_COMPARE_AND_WAIT, (void *)addr, (uint64_t)val, 0 );
}

static inline int futex_wake_one( const LONG *addr )
{
#ifdef MAC_OS_VERSION_14_4
    if (__builtin_available( macOS 14.4, * ))
        return os_sync_wake_by_address_any( (void *)addr, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE );
#endif
    return __ulock_wake( UL_COMPARE_AND_WAIT, (void *)addr, 0 );
}

#endif /* __APPLE__ */

/* create a struct security_descriptor and contained information in one contiguous piece of memory */
unsigned int wine_server_alloc_object_attributes( const OBJECT_ATTRIBUTES *attr, struct object_attributes **ret,
                                                  data_size_t *ret_len )
{
    unsigned int len = sizeof(**ret);
    SID *owner = NULL, *group = NULL;
    ACL *dacl = NULL, *sacl = NULL;
    SECURITY_DESCRIPTOR *sd;

    *ret = NULL;
    *ret_len = 0;

    if (!attr) return STATUS_SUCCESS;

    if (attr->Length != sizeof(*attr)) return STATUS_INVALID_PARAMETER;

    if ((sd = attr->SecurityDescriptor))
    {
        len += sizeof(struct security_descriptor);
	if (sd->Revision != SECURITY_DESCRIPTOR_REVISION) return STATUS_UNKNOWN_REVISION;
        if (sd->Control & SE_SELF_RELATIVE)
        {
            SECURITY_DESCRIPTOR_RELATIVE *rel = (SECURITY_DESCRIPTOR_RELATIVE *)sd;
            if (rel->Owner) owner = (PSID)((BYTE *)rel + rel->Owner);
            if (rel->Group) group = (PSID)((BYTE *)rel + rel->Group);
            if ((sd->Control & SE_SACL_PRESENT) && rel->Sacl) sacl = (PSID)((BYTE *)rel + rel->Sacl);
            if ((sd->Control & SE_DACL_PRESENT) && rel->Dacl) dacl = (PSID)((BYTE *)rel + rel->Dacl);
        }
        else
        {
            owner = sd->Owner;
            group = sd->Group;
            if (sd->Control & SE_SACL_PRESENT) sacl = sd->Sacl;
            if (sd->Control & SE_DACL_PRESENT) dacl = sd->Dacl;
        }

        if (owner) len += offsetof( SID, SubAuthority[owner->SubAuthorityCount] );
        if (group) len += offsetof( SID, SubAuthority[group->SubAuthorityCount] );
        if (sacl) len += sacl->AclSize;
        if (dacl) len += dacl->AclSize;

        /* fix alignment for the Unicode name that follows the structure */
        len = (len + sizeof(WCHAR) - 1) & ~(sizeof(WCHAR) - 1);
    }

    if (attr->ObjectName)
    {
        if ((ULONG_PTR)attr->ObjectName->Buffer & (sizeof(WCHAR) - 1)) return STATUS_DATATYPE_MISALIGNMENT;
        if (attr->ObjectName->Length & (sizeof(WCHAR) - 1)) return STATUS_OBJECT_NAME_INVALID;
        len += attr->ObjectName->Length;
    }
    else if (attr->RootDirectory) return STATUS_OBJECT_NAME_INVALID;

    len = (len + 3) & ~3;  /* DWORD-align the entire structure */

    if (!(*ret = calloc( len, 1 ))) return STATUS_NO_MEMORY;

    (*ret)->rootdir = wine_server_obj_handle( attr->RootDirectory );
    (*ret)->attributes = attr->Attributes;

    if (attr->SecurityDescriptor)
    {
        struct security_descriptor *descr = (struct security_descriptor *)(*ret + 1);
        unsigned char *ptr = (unsigned char *)(descr + 1);

        descr->control = sd->Control & ~SE_SELF_RELATIVE;
        if (owner) descr->owner_len = offsetof( SID, SubAuthority[owner->SubAuthorityCount] );
        if (group) descr->group_len = offsetof( SID, SubAuthority[group->SubAuthorityCount] );
        if (sacl) descr->sacl_len = sacl->AclSize;
        if (dacl) descr->dacl_len = dacl->AclSize;

        memcpy( ptr, owner, descr->owner_len );
        ptr += descr->owner_len;
        memcpy( ptr, group, descr->group_len );
        ptr += descr->group_len;
        memcpy( ptr, sacl, descr->sacl_len );
        ptr += descr->sacl_len;
        memcpy( ptr, dacl, descr->dacl_len );
        (*ret)->sd_len = (sizeof(*descr) + descr->owner_len + descr->group_len + descr->sacl_len +
                          descr->dacl_len + sizeof(WCHAR) - 1) & ~(sizeof(WCHAR) - 1);
    }

    if (attr->ObjectName)
    {
        unsigned char *ptr = (unsigned char *)(*ret + 1) + (*ret)->sd_len;
        (*ret)->name_len = attr->ObjectName->Length;
        memcpy( ptr, attr->ObjectName->Buffer, (*ret)->name_len );
    }

    *ret_len = len;
    return STATUS_SUCCESS;
}


static unsigned int validate_open_object_attributes( const OBJECT_ATTRIBUTES *attr )
{
    if (!attr || attr->Length != sizeof(*attr)) return STATUS_INVALID_PARAMETER;

    if (attr->ObjectName)
    {
        if ((ULONG_PTR)attr->ObjectName->Buffer & (sizeof(WCHAR) - 1)) return STATUS_DATATYPE_MISALIGNMENT;
        if (attr->ObjectName->Length & (sizeof(WCHAR) - 1)) return STATUS_OBJECT_NAME_INVALID;
    }
    else if (attr->RootDirectory) return STATUS_OBJECT_NAME_INVALID;

    return STATUS_SUCCESS;
}

#ifdef NTSYNC_IOC_EVENT_READ

static NTSTATUS linux_release_semaphore_obj( int obj, ULONG count, ULONG *prev_count )
{
    if (ioctl( obj, NTSYNC_IOC_SEM_RELEASE, &count ) < 0)
    {
        if (errno == EOVERFLOW) return STATUS_SEMAPHORE_LIMIT_EXCEEDED;
        return errno_to_status( errno );
    }
    if (prev_count) *prev_count = count;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_query_semaphore_obj( int obj, SEMAPHORE_BASIC_INFORMATION *info )
{
    struct ntsync_sem_args args = {0};
    if (ioctl( obj, NTSYNC_IOC_SEM_READ, &args ) < 0) return errno_to_status( errno );
    info->CurrentCount = args.count;
    info->MaximumCount = args.max;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_set_event_obj( int obj, LONG *prev_state )
{
    __u32 prev;
    if (ioctl( obj, NTSYNC_IOC_EVENT_SET, &prev ) < 0) return errno_to_status( errno );
    if (prev_state) *prev_state = prev;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_reset_event_obj( int obj, LONG *prev_state )
{
    __u32 prev;
    if (ioctl( obj, NTSYNC_IOC_EVENT_RESET, &prev ) < 0) return errno_to_status( errno );
    if (prev_state) *prev_state = prev;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_pulse_event_obj( int obj, LONG *prev_state )
{
    __u32 prev;
    if (ioctl( obj, NTSYNC_IOC_EVENT_PULSE, &prev ) < 0) return errno_to_status( errno );
    if (prev_state) *prev_state = prev;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_query_event_obj( int obj, EVENT_BASIC_INFORMATION *info )
{
    struct ntsync_event_args args = {0};
    if (ioctl( obj, NTSYNC_IOC_EVENT_READ, &args ) < 0) return errno_to_status( errno );
    info->EventType = args.manual ? NotificationEvent : SynchronizationEvent;
    info->EventState = args.signaled;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_release_mutex_obj( int obj, LONG *prev_count )
{
    struct ntsync_mutex_args args = {.owner = GetCurrentThreadId()};
    if (ioctl( obj, NTSYNC_IOC_MUTEX_UNLOCK, &args ) < 0)
    {
        if (errno == EOVERFLOW) return STATUS_MUTANT_LIMIT_EXCEEDED;
        if (errno == EPERM) return STATUS_MUTANT_NOT_OWNED;
        return errno_to_status( errno );
    }
    if (prev_count) *prev_count = 1 - args.count;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_query_mutex_obj( int obj, MUTANT_BASIC_INFORMATION *info )
{
    struct ntsync_mutex_args args = {0};
    if (ioctl( obj, NTSYNC_IOC_MUTEX_READ, &args ) < 0)
    {
        if (errno == EOWNERDEAD)
        {
            info->AbandonedState = TRUE;
            info->OwnedByCaller = FALSE;
            info->CurrentCount = 1;
            return STATUS_SUCCESS;
        }
        return errno_to_status( errno );
    }
    info->AbandonedState = FALSE;
    info->OwnedByCaller = (args.owner == GetCurrentThreadId());
    info->CurrentCount = 1 - args.count;
    return STATUS_SUCCESS;
}

static NTSTATUS linux_wait_objs( int device, DWORD count, const int *objs, WAIT_TYPE type,
                                 int alert_fd, const LARGE_INTEGER *timeout )
{
    struct ntsync_wait_args args = {0};
    unsigned long request;
    struct timespec now;
    int ret;

    if (!timeout || timeout->QuadPart == TIMEOUT_INFINITE)
    {
        args.timeout = ~(__u64)0;
    }
    else if (timeout->QuadPart <= 0)
    {
        clock_gettime( CLOCK_MONOTONIC, &now );
        args.timeout = ((ULONGLONG)now.tv_sec * NSECPERSEC) + now.tv_nsec + (-timeout->QuadPart * 100);
    }
    else
    {
        args.timeout = (timeout->QuadPart * 100) - (SECS_1601_TO_1970 * NSECPERSEC);
        args.flags |= NTSYNC_WAIT_REALTIME;
    }

    args.objs = (uintptr_t)objs;
    args.count = count;
    args.owner = GetCurrentThreadId();
    args.index = ~0u;
    args.alert = alert_fd;

    if (type != WaitAll || count == 1) request = NTSYNC_IOC_WAIT_ANY;
    else request = NTSYNC_IOC_WAIT_ALL;

    do { ret = ioctl( device, request, &args ); }
    while (ret < 0 && errno == EINTR);

    if (!ret)
    {
        if (args.index == count)
        {
            static const LARGE_INTEGER timeout;

            ret = server_wait( NULL, 0, SELECT_INTERRUPTIBLE | SELECT_ALERTABLE, &timeout );
            assert( ret == STATUS_USER_APC );
            return ret;
        }

        return type != WaitAll ? args.index : 0;
    }
    if (errno == EOWNERDEAD) return STATUS_ABANDONED + (type != WaitAll ? args.index : 0);
    if (errno == ETIMEDOUT) return STATUS_TIMEOUT;
    return errno_to_status( errno );
}

#else /* NTSYNC_IOC_EVENT_READ */

static NTSTATUS linux_release_semaphore_obj( int obj, ULONG count, ULONG *prev_count )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_query_semaphore_obj( int obj, SEMAPHORE_BASIC_INFORMATION *info )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_set_event_obj( int obj, LONG *prev_state )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_reset_event_obj( int obj, LONG *prev_state )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_pulse_event_obj( int obj, LONG *prev_state )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_query_event_obj( int obj, EVENT_BASIC_INFORMATION *info )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_release_mutex_obj( int obj, LONG *prev_count )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_query_mutex_obj( int obj, MUTANT_BASIC_INFORMATION *info )
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS linux_wait_objs( int device, DWORD count, const int *objs, WAIT_TYPE type,
                                 int alert_fd, const LARGE_INTEGER *timeout )
{
    return STATUS_NOT_IMPLEMENTED;
}

#endif /* NTSYNC_IOC_EVENT_READ */

/* It's possible for synchronization primitives to remain alive even after being
 * closed, because a thread is still waiting on them. It's rare in practice, and
 * documented as being undefined behaviour by Microsoft, but it works, and some
 * applications rely on it. This means we need to refcount handles, and defer
 * deleting them on the server side until the refcount reaches zero. We do this
 * by having each client process hold a handle to the in-process synchronization
 * object, as well as a private refcount. When the client refcount reaches zero,
 * it closes the handle; when all handles are closed, the server deletes the
 * in-process synchronization object.
 *
 * We also need this for signal-and-wait. The signal and wait operations aren't
 * atomic, but we can't perform the signal and then return STATUS_INVALID_HANDLE
 * for the wait—we need to either do both operations or neither. That means we
 * need to grab references to both objects, and prevent them from being
 * destroyed before we're done with them.
 *
 * We want lookup of objects from the cache to be very fast; ideally, it should
 * be lock-free. We achieve this by using atomic modifications to "refcount",
 * and guaranteeing that all other fields are valid and correct *as long as*
 * refcount is nonzero, and we store the entire structure in memory which will
 * never be freed.
 *
 * This means that acquiring the object can't use a simple atomic increment; it
 * has to use a compare-and-swap loop to ensure that it doesn't try to increment
 * an object with a zero refcount. That's still leagues better than a real lock,
 * though, and release can be a single atomic decrement.
 *
 * It also means that threads modifying the cache need to take a lock, to
 * prevent other threads from writing to it concurrently.
 *
 * It's possible for an object currently in use (by a waiter) to be closed and
 * the same handle immediately reallocated to a different object. This should be
 * a very rare situation, and in that case we simply don't cache the handle.
 */
struct inproc_sync
{
    LONG           refcount;  /* reference count of the sync object */
    int            fd;        /* unix file descriptor */
    unsigned int   access;    /* handle access rights */
    unsigned short type;      /* enum inproc_sync_type as short to save space */
    unsigned short closed;    /* fd has been closed but sync is still referenced */
    unsigned short has_wait_packet; /* handle is associated with a wait completion packet */
    unsigned short unsupported;     /* server has no inproc sync object for this handle (negative cache) */
    unsigned short non_waitable;    /* a single-object wait on this handle was rejected as OBJECT_TYPE_MISMATCH */
};

#define INPROC_SYNC_CACHE_BLOCK_SIZE  (65536 / sizeof(struct inproc_sync))
#define INPROC_SYNC_CACHE_ENTRIES     128

static struct inproc_sync *inproc_sync_cache[INPROC_SYNC_CACHE_ENTRIES];
static struct inproc_sync inproc_sync_cache_initial_block[INPROC_SYNC_CACHE_BLOCK_SIZE];

static inline unsigned int inproc_sync_handle_to_index( HANDLE handle, unsigned int *entry )
{
    unsigned int idx = (wine_server_obj_handle(handle) >> 2) - 1;
    *entry = idx / INPROC_SYNC_CACHE_BLOCK_SIZE;
    return idx % INPROC_SYNC_CACHE_BLOCK_SIZE;
}

static BOOL is_pseudo_handle( HANDLE handle )
{
    return ((ULONG)(ULONG_PTR)handle >= 0xfffffffa);
}

static struct inproc_sync *cache_inproc_sync( HANDLE handle, struct inproc_sync *sync )
{
    unsigned int entry, idx = inproc_sync_handle_to_index( handle, &entry );
    struct inproc_sync *cache;
    int refcount;

    /* don't cache pseudo-handles; waiting on them is pointless anyway */
    if (is_pseudo_handle( handle )) return sync;

    if (entry >= INPROC_SYNC_CACHE_ENTRIES)
    {
        FIXME( "too many allocated handles, not caching %p\n", handle );
        return sync;
    }

    if (!inproc_sync_cache[entry])  /* do we need to allocate a new block of entries? */
    {
        if (!entry) inproc_sync_cache[0] = inproc_sync_cache_initial_block;
        else
        {
            static const size_t size = INPROC_SYNC_CACHE_BLOCK_SIZE * sizeof(struct inproc_sync);
            void *ptr = anon_mmap_alloc( size, PROT_READ | PROT_WRITE );
            if (ptr == MAP_FAILED) return sync;
            inproc_sync_cache[entry] = ptr;
        }
    }

    cache = &inproc_sync_cache[entry][idx];

    if (InterlockedCompareExchange( &cache->refcount, 0, 0 ))
    {
        /* The handle is currently being used for another object (i.e. it was
         * closed and then reused, but some thread is waiting on the old handle
         * or otherwise simultaneously using the old object). We can't cache
         * this object until the old one is completely destroyed. */
        return sync;
    }

    cache->fd = sync->fd;
    cache->access = sync->access;
    cache->type = sync->type;
    cache->closed = sync->closed;
    cache->has_wait_packet = sync->has_wait_packet;
    cache->unsupported = sync->unsupported;
    cache->non_waitable = sync->non_waitable;
    /* Make sure we set the other members before the refcount; this store needs
     * release semantics [paired with the load in get_cached_inproc_sync()].
     * Set the refcount to 2 (one for the handle, one for the caller). */
    refcount = InterlockedExchange( &cache->refcount, 2 );
    assert( !refcount );

    assert( sync->refcount == 1 );
    memset( sync, 0, sizeof(*sync) );

    return cache;
}

/* returns the previous value */
static inline LONG interlocked_inc_if_nonzero( LONG *dest )
{
    LONG val, tmp;
    for (val = *dest;; val = tmp)
    {
        if (!val || (tmp = InterlockedCompareExchange( dest, val + 1, val )) == val)
            break;
    }
    return val;
}

static void release_inproc_sync( struct inproc_sync *sync )
{
    /* save the fd now; as soon as the refcount hits 0 we cannot
     * access the cache anymore */
    int fd = sync->fd;
    LONG ref = InterlockedDecrement( &sync->refcount );

    assert( ref >= 0 );
    /* negative cache entries carry no fd */
    if (!ref && fd >= 0) close( fd );
}

static struct inproc_sync *get_cached_inproc_sync( HANDLE handle )
{
    unsigned int entry, idx = inproc_sync_handle_to_index( handle, &entry );
    struct inproc_sync *cache;

    if (entry >= INPROC_SYNC_CACHE_ENTRIES || !inproc_sync_cache[entry]) return NULL;

    cache = &inproc_sync_cache[entry][idx];

    /* this load needs acquire semantics [paired with the store in
     * cache_inproc_sync()] */
    if (!interlocked_inc_if_nonzero( &cache->refcount )) return NULL;

    if (cache->closed)
    {
        /* The object is still being used, but "handle" has been closed. The
         * handle value might have been reused for another object in the
         * meantime, in which case we have to report that valid object, so
         * force the caller to check the server. */
        release_inproc_sync( cache );
        return NULL;
    }

    return cache;
}

/* fd_cache_mutex must be held to avoid races with other thread receiving fds */
static NTSTATUS get_server_inproc_sync( HANDLE handle, struct inproc_sync *sync )
{
    NTSTATUS ret;

    SERVER_START_REQ( get_inproc_sync_fd )
    {
        req->handle = wine_server_obj_handle( handle );
        if (!(ret = wine_server_call( req )))
        {
            obj_handle_t fd_handle;
            sync->refcount = 1;
            sync->fd = wine_server_receive_fd( &fd_handle );
            assert( wine_server_ptr_handle(fd_handle) == handle );
            sync->access = reply->access;
            sync->type = reply->type;
            sync->closed = 0;
            sync->has_wait_packet = 0;
            sync->unsupported = 0;
            sync->non_waitable = 0;
        }
    }
    SERVER_END_REQ;

    return ret;
}

/* returns a pointer to a cache entry; if the object could not be cached,
 * returns "cache" instead, which should be allocated on stack */
static NTSTATUS get_inproc_sync( HANDLE handle, enum inproc_sync_type desired_type, ACCESS_MASK desired_access,
                                 struct inproc_sync *stack, struct inproc_sync **out )
{
    struct inproc_sync *sync;
    sigset_t sigset;
    NTSTATUS ret;

    /* try to find it in the cache already */
    if ((sync = get_cached_inproc_sync( handle ))) ret = STATUS_SUCCESS;
    else
    {
        /* We need to use fd_cache_mutex here to protect against races with
         * other threads trying to receive fds for the fd cache,
         * and we need to use an uninterrupted section to prevent reentrancy.
         * We also need fd_cache_mutex to protect against the same race with
         * NtClose, that is, to prevent the object from being cached again between
         * close_inproc_sync() and close_handle.
         *
         * The mutex also protects cache_inproc_sync(). Accessing the cache is
         * done without a lock, but populating it currently is not. */
        server_enter_uninterrupted_section( &fd_cache_mutex, &sigset );
        if (!(sync = get_cached_inproc_sync( handle )))
        {
            if ((ret = get_server_inproc_sync( handle, stack )))
            {
                if (ret == STATUS_NOT_IMPLEMENTED)
                {
                    /* The server answered that this object has no synchronisation
                     * object at all (type 0): tokens, sections, registry keys, ALPC
                     * ports. Every object that can be waited on - event, mutex,
                     * semaphore, thread, process, and every fd-backed object
                     * (pipe, serial, console, socket, file) - owns an internal
                     * sync object and therefore gets an fd here. Record that
                     * fact, so the fast path stops issuing one get_inproc_sync_fd
                     * request per call: a Chromium MsgWait that includes a token
                     * handle asked 11.7M times in 100 s, and the server round
                     * trips starved the relay. */
                    stack->refcount = 1;
                    stack->fd = -1;
                    stack->access = 0;
                    stack->type = INPROC_SYNC_UNKNOWN;
                    stack->closed = 0;
                    stack->has_wait_packet = 0;
                    stack->unsupported = 1;
                    stack->non_waitable = 0;
                    sync = cache_inproc_sync( handle, stack );
                    release_inproc_sync( sync );
                }
                server_leave_uninterrupted_section( &fd_cache_mutex, &sigset );
                return ret;
            }
            sync = cache_inproc_sync( handle, stack );
        }
        server_leave_uninterrupted_section( &fd_cache_mutex, &sigset );
    }

    if (sync->non_waitable)
    {
        /* A single-object wait on this handle was already rejected by the server
         * as STATUS_OBJECT_TYPE_MISMATCH. Windows validates the object type when
         * the wait is entered, so report the same failure right away instead of
         * re-arming the identical wait; the caller's loop then does not turn a
         * rejected handle into a server round trip per iteration. */
        release_inproc_sync( sync );
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    if (sync->unsupported)
    {
        /* Cached "no sync object"; no server request needed to learn it again.
         * The caller falls back to the server-side wait, which is authoritative. */
        release_inproc_sync( sync );
        return STATUS_NOT_IMPLEMENTED;
    }

    if (desired_type != INPROC_SYNC_UNKNOWN && desired_type != sync->type)
    {
        release_inproc_sync( sync );
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if ((sync->access & desired_access) != desired_access)
    {
        release_inproc_sync( sync );
        return STATUS_ACCESS_DENIED;
    }

    *out = sync;
    return STATUS_SUCCESS;
}

extern NTSTATUS check_signal_access( struct inproc_sync *sync )
{
    switch (sync->type)
    {
    case INPROC_SYNC_INTERNAL:
        return STATUS_OBJECT_TYPE_MISMATCH;
    case INPROC_SYNC_EVENT:
        if (!(sync->access & EVENT_MODIFY_STATE)) return STATUS_ACCESS_DENIED;
        return STATUS_SUCCESS;
    case INPROC_SYNC_MUTEX:
        if (!(sync->access & SYNCHRONIZE)) return STATUS_ACCESS_DENIED;
        return STATUS_SUCCESS;
    case INPROC_SYNC_SEMAPHORE:
        if (!(sync->access & SEMAPHORE_MODIFY_STATE)) return STATUS_ACCESS_DENIED;
        return STATUS_SUCCESS;
    }

    assert( 0 );
    return STATUS_OBJECT_TYPE_MISMATCH;
}

/* caller must hold fd_cache_mutex */
void close_inproc_sync( HANDLE handle )
{
    struct inproc_sync *cache;

    if (inproc_device_fd < 0) return;
    if ((cache = get_cached_inproc_sync( handle )))
    {
        cache->closed = 1;
        /* once for the reference we just grabbed, and once for the handle */
        release_inproc_sync( cache );
        release_inproc_sync( cache );
    }
}

/* Remember that a single-object wait on "handle" was rejected as
 * STATUS_OBJECT_TYPE_MISMATCH, so the next one fails without a server request.
 * Only ever called for single-handle waits: multi-handle sets can contain
 * handles that the server waits on without a sync object (wait completion
 * packets, message queues), and those must keep working. */
static void cache_nonwaitable_handle( HANDLE handle )
{
    struct inproc_sync *cache;
    sigset_t sigset;

    if (inproc_device_fd < 0) return;
    if (is_pseudo_handle( handle )) return;

    server_enter_uninterrupted_section( &fd_cache_mutex, &sigset );
    if ((cache = get_cached_inproc_sync( handle )))
    {
        if (cache->unsupported && !cache->has_wait_packet) cache->non_waitable = 1;
        release_inproc_sync( cache );
    }
    server_leave_uninterrupted_section( &fd_cache_mutex, &sigset );
}

static NTSTATUS inproc_release_semaphore( HANDLE handle, ULONG count, ULONG *prev_count )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_SEMAPHORE, SEMAPHORE_MODIFY_STATE, &stack, &sync ))) return ret;
    ret = linux_release_semaphore_obj( sync->fd, count, prev_count );
    release_inproc_sync( sync );
    return ret;
}

static NTSTATUS inproc_query_semaphore( HANDLE handle, SEMAPHORE_BASIC_INFORMATION *info )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_SEMAPHORE, SEMAPHORE_QUERY_STATE, &stack, &sync ))) return ret;
    ret = linux_query_semaphore_obj( sync->fd, info );
    release_inproc_sync( sync );
    return ret;
}

static NTSTATUS inproc_set_event( HANDLE handle, LONG *prev_state )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_EVENT, EVENT_MODIFY_STATE, &stack, &sync ))) return ret;
    ret = linux_set_event_obj( sync->fd, prev_state );
    release_inproc_sync( sync );
    return ret;
}

static NTSTATUS inproc_reset_event( HANDLE handle, LONG *prev_state )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_EVENT, EVENT_MODIFY_STATE, &stack, &sync ))) return ret;
    ret = linux_reset_event_obj( sync->fd, prev_state );
    release_inproc_sync( sync );
    return ret;
}

static NTSTATUS inproc_pulse_event( HANDLE handle, LONG *prev_state )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_EVENT, EVENT_MODIFY_STATE, &stack, &sync ))) return ret;
    ret = linux_pulse_event_obj( sync->fd, prev_state );
    release_inproc_sync( sync );
    return ret;
}

static NTSTATUS inproc_query_event( HANDLE handle, EVENT_BASIC_INFORMATION *info )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_EVENT, EVENT_QUERY_STATE, &stack, &sync ))) return ret;
    ret = linux_query_event_obj( sync->fd, info );
    release_inproc_sync( sync );
    return ret;
}

static NTSTATUS inproc_release_mutex( HANDLE handle, LONG *prev_count )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_MUTEX, 0, &stack, &sync ))) return ret;
    ret = linux_release_mutex_obj( sync->fd, prev_count );
    release_inproc_sync( sync );
    return ret;
}

static NTSTATUS inproc_query_mutex( HANDLE handle, MUTANT_BASIC_INFORMATION *info )
{
    struct inproc_sync stack, *sync;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;
    if ((ret = get_inproc_sync( handle, INPROC_SYNC_MUTEX, MUTANT_QUERY_STATE, &stack, &sync ))) return ret;
    ret = linux_query_mutex_obj( sync->fd, info );
    release_inproc_sync( sync );
    return ret;
}

static int get_inproc_alert_fd(void)
{
    struct thread_data *data = get_thread_data();
    obj_handle_t token;
    sigset_t sigset;
    int fd;

    if ((fd = data->alert_fd) < 0)
    {
        server_enter_uninterrupted_section( &fd_cache_mutex, &sigset );

        SERVER_START_REQ( get_inproc_alert_fd )
        {
            if (!server_call_unlocked( req ))
            {
                data->alert_fd = fd = wine_server_receive_fd( &token );
                assert( token == reply->handle );
            }
        }
        SERVER_END_REQ;

        server_leave_uninterrupted_section( &fd_cache_mutex, &sigset );
    }

    return fd;
}

static NTSTATUS inproc_wait( DWORD count, const HANDLE *handles, WAIT_TYPE type,
                             BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    struct inproc_sync *syncs[64], stack[ARRAY_SIZE(syncs)];
    int objs[ARRAY_SIZE(syncs)], alert_fd = 0;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;

    assert( count <= ARRAY_SIZE(syncs) );
    objs[0] = -1;  /* make gcc happy, otherwise it thinks objs is not initialized */
    for (int i = 0; i < count; ++i)
    {
        if ((ret = get_inproc_sync( handles[i], INPROC_SYNC_UNKNOWN, SYNCHRONIZE, &stack[i], &syncs[i] )))
        {
            while (i--) release_inproc_sync( syncs[i] );
            return ret;
        }
        objs[i] = syncs[i]->fd;
    }

    if (alertable) alert_fd = get_inproc_alert_fd();
    ret = linux_wait_objs( inproc_device_fd, count, objs, type, alert_fd, timeout );

    while (count--) release_inproc_sync( syncs[count] );
    return ret;
}

static NTSTATUS inproc_signal_and_wait( HANDLE signal, HANDLE wait,
                                        BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    struct inproc_sync stack_signal, stack_wait, *signal_sync = &stack_signal, *wait_sync = &stack_wait;
    int alert_fd = 0;
    NTSTATUS ret;

    if (inproc_device_fd < 0) return STATUS_NOT_IMPLEMENTED;

    if ((ret = get_inproc_sync( signal, INPROC_SYNC_UNKNOWN, 0, &stack_signal, &signal_sync ))) return ret;
    if ((ret = check_signal_access( signal_sync ))) goto done;

    if ((ret = get_inproc_sync( wait, INPROC_SYNC_UNKNOWN, SYNCHRONIZE, &stack_wait, &wait_sync ))) goto done;

    switch (signal_sync->type)
    {
    case INPROC_SYNC_EVENT:     ret = linux_set_event_obj( signal_sync->fd, NULL ); break;
    case INPROC_SYNC_MUTEX:     ret = linux_release_mutex_obj( signal_sync->fd, NULL ); break;
    case INPROC_SYNC_SEMAPHORE: ret = linux_release_semaphore_obj( signal_sync->fd, 1, NULL ); break;
    default: assert( 0 ); break;
    }

    if (!ret)
    {
        if (alertable) alert_fd = get_inproc_alert_fd();
        ret = linux_wait_objs( inproc_device_fd, 1, &wait_sync->fd, WaitAny, alert_fd, timeout );
    }

    release_inproc_sync( wait_sync );
done:
    release_inproc_sync( signal_sync );
    return ret;
}


#ifdef __linux__

#define LOCAL_SYNC_HANDLE_TAG   0x40000000u
#define LOCAL_SYNC_BLOCK_BITS   9
#define LOCAL_SYNC_BLOCK_SIZE   (1u << LOCAL_SYNC_BLOCK_BITS)
#define LOCAL_SYNC_MAX_BLOCKS   32u
#define LOCAL_SYNC_MAX_SLOTS    (LOCAL_SYNC_BLOCK_SIZE * LOCAL_SYNC_MAX_BLOCKS) /* 16384 */
#define LOCAL_SYNC_INVALID_IDX  0xffffffffu

enum local_sync_type
{
    LOCAL_SYNC_FREE = 0,
    LOCAL_SYNC_EVENT_MANUAL,
    LOCAL_SYNC_EVENT_AUTO,
    LOCAL_SYNC_SEMAPHORE,
};

struct local_sync_slot
{
    LONG lock;
    LONG refcount;
    LONG state;
    LONG waiters;
    LONG pulse_credits;
    LONG max_count;
    LONG promoting;
    LONG active_mutators;
    unsigned short type;
    unsigned short gen;
    ACCESS_MASK access;
    uint32_t next_free;
    HANDLE promoted_handle;
} __attribute__((aligned(64)));

static struct local_sync_slot local_sync_block0[LOCAL_SYNC_BLOCK_SIZE];
static struct local_sync_slot *local_sync_blocks[LOCAL_SYNC_MAX_BLOCKS] = { local_sync_block0 };
static uint64_t local_sync_free_head = LOCAL_SYNC_INVALID_IDX;
static uint32_t local_sync_next_idx = 0;
static LONG local_sync_alloc_lock = 0;

static inline void local_sync_spin_lock( LONG *lock )
{
    while (__atomic_exchange_n( lock, 1, __ATOMIC_ACQUIRE ))
    {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#endif
    }
}

static inline void local_sync_spin_unlock( LONG *lock )
{
    __atomic_store_n( lock, 0, __ATOMIC_RELEASE );
}

BOOL is_local_sync_handle( HANDLE handle )
{
    ULONG_PTR v = (ULONG_PTR)handle;
    return (v & ~(ULONG_PTR)0x3fffffffULL) == LOCAL_SYNC_HANDLE_TAG;
}

static inline HANDLE make_local_sync_handle( uint32_t idx, unsigned short gen )
{
    return (HANDLE)(ULONG_PTR)(LOCAL_SYNC_HANDLE_TAG | (((uint32_t)(gen & 0x3fffu)) << 16) | (idx << 2));
}

static inline struct local_sync_slot *local_sync_slot_ptr( uint32_t idx )
{
    uint32_t block = idx >> LOCAL_SYNC_BLOCK_BITS;
    struct local_sync_slot *ptr;

    if (block >= LOCAL_SYNC_MAX_BLOCKS) return NULL;
    ptr = __atomic_load_n( &local_sync_blocks[block], __ATOMIC_ACQUIRE );
    if (!ptr) return NULL;
    return &ptr[idx & (LOCAL_SYNC_BLOCK_SIZE - 1)];
}

static uint32_t local_sync_alloc_slot( struct local_sync_slot **slot_out )
{
    uint64_t old = __atomic_load_n( &local_sync_free_head, __ATOMIC_ACQUIRE );
    uint32_t idx, block;
    struct local_sync_slot *ptr;

    for (;;)
    {
        uint64_t next;
        idx = (uint32_t)old;
        if (idx == LOCAL_SYNC_INVALID_IDX) break;
        ptr = local_sync_slot_ptr( idx );
        next = ((old + (1ULL << 32)) & ~0xffffffffULL) | ptr->next_free;
        if (__atomic_compare_exchange_n( &local_sync_free_head, &old, next, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE ))
        {
            *slot_out = ptr;
            return idx;
        }
    }

    idx = __atomic_fetch_add( &local_sync_next_idx, 1, __ATOMIC_RELAXED );
    if (idx >= LOCAL_SYNC_MAX_SLOTS)
    {
        __atomic_store_n( &local_sync_next_idx, LOCAL_SYNC_MAX_SLOTS, __ATOMIC_RELAXED );
        return LOCAL_SYNC_INVALID_IDX;
    }

    block = idx >> LOCAL_SYNC_BLOCK_BITS;
    ptr = __atomic_load_n( &local_sync_blocks[block], __ATOMIC_ACQUIRE );
    if (!ptr)
    {
        local_sync_spin_lock( &local_sync_alloc_lock );
        ptr = local_sync_blocks[block];
        if (!ptr)
        {
            ptr = mmap( NULL, LOCAL_SYNC_BLOCK_SIZE * sizeof(struct local_sync_slot),
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
            if (ptr == MAP_FAILED) ptr = NULL;
            else __atomic_store_n( &local_sync_blocks[block], ptr, __ATOMIC_RELEASE );
        }
        local_sync_spin_unlock( &local_sync_alloc_lock );
        if (!ptr) return LOCAL_SYNC_INVALID_IDX;
    }

    *slot_out = &ptr[idx & (LOCAL_SYNC_BLOCK_SIZE - 1)];
    return idx;
}

static void local_sync_free_slot( struct local_sync_slot *slot, uint32_t idx )
{
    uint64_t old = __atomic_load_n( &local_sync_free_head, __ATOMIC_RELAXED );
    for (;;)
    {
        uint64_t next;
        slot->next_free = (uint32_t)old;
        next = ((old + (1ULL << 32)) & ~0xffffffffULL) | idx;
        if (__atomic_compare_exchange_n( &local_sync_free_head, &old, next, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED ))
            break;
    }
}

static inline struct local_sync_slot *acquire_local_sync_slot( HANDLE handle, uint32_t *idx_out )
{
    ULONG_PTR v = (ULONG_PTR)handle;
    uint32_t idx = (v >> 2) & 0x3fffu;
    unsigned short gen = (v >> 16) & 0x3fffu;
    struct local_sync_slot *slot = local_sync_slot_ptr( idx );
    LONG r;

    if (!slot) return NULL;
    r = __atomic_load_n( &slot->refcount, __ATOMIC_ACQUIRE );
    while (r > 0)
    {
        if (__atomic_compare_exchange_n( &slot->refcount, &r, r + 1, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE ))
        {
            if (__atomic_load_n( &slot->type, __ATOMIC_ACQUIRE ) == LOCAL_SYNC_FREE || slot->gen != gen)
            {
                if (__atomic_sub_fetch( &slot->refcount, 1, __ATOMIC_RELEASE ) == 0)
                {
                    __atomic_thread_fence( __ATOMIC_ACQUIRE );
                    local_sync_free_slot( slot, idx );
                }
                return NULL;
            }
            *idx_out = idx;
            return slot;
        }
    }
    return NULL;
}

static inline void release_local_sync_slot( struct local_sync_slot *slot, uint32_t idx )
{
    if (__atomic_sub_fetch( &slot->refcount, 1, __ATOMIC_RELEASE ) == 0)
    {
        __atomic_thread_fence( __ATOMIC_ACQUIRE );
        local_sync_free_slot( slot, idx );
    }
}

static inline BOOL is_local_sync_eligible( const OBJECT_ATTRIBUTES *attr )
{
    if (!attr) return TRUE;
    if (attr->Length != sizeof(*attr)) return FALSE;
    if (attr->RootDirectory || attr->ObjectName || attr->SecurityDescriptor || attr->SecurityQualityOfService)
        return FALSE;
    if (attr->Attributes & ~(ULONG)(OBJ_CASE_INSENSITIVE | OBJ_OPENIF))
        return FALSE;
    return TRUE;
}

static inline BOOL normalize_event_access( ACCESS_MASK access, ACCESS_MASK *out )
{
    if (access & (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE)) return FALSE;
    if (access & (GENERIC_ALL | MAXIMUM_ALLOWED))
        access = (access & ~(GENERIC_ALL | MAXIMUM_ALLOWED)) | EVENT_ALL_ACCESS;
    *out = access;
    return TRUE;
}

static inline BOOL normalize_semaphore_access( ACCESS_MASK access, ACCESS_MASK *out )
{
    if (access & (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE)) return FALSE;
    if (access & (GENERIC_ALL | MAXIMUM_ALLOWED))
        access = (access & ~(GENERIC_ALL | MAXIMUM_ALLOWED)) | SEMAPHORE_ALL_ACCESS;
    *out = access;
    return TRUE;
}

static NTSTATUS promote_slot_locked( struct local_sync_slot *slot, HANDLE *real_handle )
{
    HANDLE real = NULL;
    unsigned int ret = STATUS_SUCCESS;

    if (slot->promoted_handle)
    {
        *real_handle = slot->promoted_handle;
        return STATUS_SUCCESS;
    }

    __atomic_store_n( &slot->promoting, 1, __ATOMIC_SEQ_CST );
    while (__atomic_load_n( &slot->active_mutators, __ATOMIC_ACQUIRE ) > 0)
    {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#endif
    }

    if (slot->type == LOCAL_SYNC_EVENT_MANUAL || slot->type == LOCAL_SYNC_EVENT_AUTO)
    {
        BOOLEAN state = (__atomic_load_n( &slot->state, __ATOMIC_ACQUIRE ) & 1) != 0;
        SERVER_START_REQ( create_event )
        {
            req->access        = slot->access;
            req->manual_reset  = (slot->type == LOCAL_SYNC_EVENT_MANUAL);
            req->initial_state = state;
            ret = wine_server_call( req );
            real = wine_server_ptr_handle( reply->handle );
        }
        SERVER_END_REQ;
    }
    else if (slot->type == LOCAL_SYNC_SEMAPHORE)
    {
        LONG cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        SERVER_START_REQ( create_semaphore )
        {
            req->access  = slot->access;
            req->initial = cur;
            req->max     = slot->max_count;
            ret = wine_server_call( req );
            real = wine_server_ptr_handle( reply->handle );
        }
        SERVER_END_REQ;
    }
    else
    {
        ret = STATUS_INVALID_HANDLE;
    }

    if (!ret)
    {
        __atomic_store_n( &slot->promoted_handle, real, __ATOMIC_RELEASE );
        if (__atomic_load_n( &slot->waiters, __ATOMIC_SEQ_CST ) > 0)
            futex_wake_n( &slot->state, INT_MAX );
        *real_handle = real;
    }
    else
    {
        __atomic_store_n( &slot->promoting, 0, __ATOMIC_RELEASE );
    }
    return ret;
}

NTSTATUS promote_local_sync_handle( HANDLE handle, HANDLE *real_handle )
{
    uint32_t idx;
    struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
    unsigned short gen = ((ULONG_PTR)handle >> 16) & 0x3fffu;
    NTSTATUS ret;

    if (!slot) return STATUS_INVALID_HANDLE;

    if ((*real_handle = __atomic_load_n( &slot->promoted_handle, __ATOMIC_ACQUIRE )))
    {
        release_local_sync_slot( slot, idx );
        return STATUS_SUCCESS;
    }

    local_sync_spin_lock( &slot->lock );
    if (slot->type == LOCAL_SYNC_FREE || slot->gen != gen)
        ret = STATUS_INVALID_HANDLE;
    else
        ret = promote_slot_locked( slot, real_handle );
    local_sync_spin_unlock( &slot->lock );

    release_local_sync_slot( slot, idx );
    return ret;
}

NTSTATUS close_local_sync_handle( HANDLE handle )
{
    uint32_t idx;
    struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
    unsigned short gen = ((ULONG_PTR)handle >> 16) & 0x3fffu;
    HANDLE promoted = NULL;
    NTSTATUS ret = STATUS_SUCCESS;

    if (!slot) return STATUS_INVALID_HANDLE;

    local_sync_spin_lock( &slot->lock );
    if (slot->type == LOCAL_SYNC_FREE || slot->gen != gen)
    {
        local_sync_spin_unlock( &slot->lock );
        release_local_sync_slot( slot, idx );
        return STATUS_INVALID_HANDLE;
    }
    promoted = slot->promoted_handle;
    slot->promoted_handle = NULL;
    __atomic_store_n( &slot->type, LOCAL_SYNC_FREE, __ATOMIC_RELEASE );
    slot->gen = (slot->gen + 1) & 0x3fffu;
    local_sync_spin_unlock( &slot->lock );

    /* Drop creation ref (1) and acquire_local_sync_slot ref (1). */
    __atomic_sub_fetch( &slot->refcount, 1, __ATOMIC_RELEASE );
    release_local_sync_slot( slot, idx );

    if (promoted) ret = NtClose( promoted );
    return ret;
}

NTSTATUS retire_local_sync_handle( HANDLE handle, HANDLE *real_handle )
{
    uint32_t idx;
    struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
    unsigned short gen = ((ULONG_PTR)handle >> 16) & 0x3fffu;
    NTSTATUS ret;

    if (!slot) return STATUS_INVALID_HANDLE;

    local_sync_spin_lock( &slot->lock );
    if (slot->type == LOCAL_SYNC_FREE || slot->gen != gen)
    {
        local_sync_spin_unlock( &slot->lock );
        release_local_sync_slot( slot, idx );
        return STATUS_INVALID_HANDLE;
    }
    ret = promote_slot_locked( slot, real_handle );
    if (!ret)
    {
        slot->promoted_handle = NULL;
        __atomic_store_n( &slot->type, LOCAL_SYNC_FREE, __ATOMIC_RELEASE );
        slot->gen = (slot->gen + 1) & 0x3fffu;
    }
    local_sync_spin_unlock( &slot->lock );

    if (!ret) __atomic_sub_fetch( &slot->refcount, 1, __ATOMIC_RELEASE );
    release_local_sync_slot( slot, idx );
    return ret;
}

static NTSTATUS local_sync_wait( HANDLE handle, const LARGE_INTEGER *timeout )
{
    uint32_t idx;
    struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
    HANDLE promoted;
    unsigned short type;
    struct timespec deadline;
    clockid_t clk = CLOCK_MONOTONIC;
    BOOL has_deadline = FALSE;
    NTSTATUS ret = STATUS_SUCCESS;

    if (!slot) return STATUS_INVALID_HANDLE;

    if (!(slot->access & SYNCHRONIZE))
    {
        release_local_sync_slot( slot, idx );
        return STATUS_ACCESS_DENIED;
    }

    if ((promoted = __atomic_load_n( &slot->promoted_handle, __ATOMIC_ACQUIRE )))
    {
        release_local_sync_slot( slot, idx );
        return NtWaitForSingleObject( promoted, FALSE, timeout );
    }

    type = slot->type;

    /* Fast-path uncontended acquire */
    if (type == LOCAL_SYNC_EVENT_MANUAL)
    {
        if (__atomic_load_n( &slot->state, __ATOMIC_ACQUIRE ) & 1)
        {
            release_local_sync_slot( slot, idx );
            return STATUS_SUCCESS;
        }
    }
    else if (type == LOCAL_SYNC_EVENT_AUTO)
    {
        LONG cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        while (cur & 1)
        {
            if (__atomic_compare_exchange_n( &slot->state, &cur, cur & ~1L, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE ))
            {
                release_local_sync_slot( slot, idx );
                return STATUS_SUCCESS;
            }
        }
    }
    else if (type == LOCAL_SYNC_SEMAPHORE)
    {
        LONG cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        while (cur > 0)
        {
            if (__atomic_compare_exchange_n( &slot->state, &cur, cur - 1, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE ))
            {
                release_local_sync_slot( slot, idx );
                return STATUS_SUCCESS;
            }
        }
    }

    if (timeout && timeout->QuadPart == 0)
    {
        release_local_sync_slot( slot, idx );
        return STATUS_TIMEOUT;
    }

    if (timeout && timeout->QuadPart != TIMEOUT_INFINITE)
    {
        has_deadline = TRUE;
        if (timeout->QuadPart < 0)
        {
            ULONGLONG ns = (ULONGLONG)(-timeout->QuadPart) * 100ULL;
            clk = CLOCK_MONOTONIC;
            clock_gettime( clk, &deadline );
            deadline.tv_sec += ns / NSECPERSEC;
            deadline.tv_nsec += ns % NSECPERSEC;
            if (deadline.tv_nsec >= NSECPERSEC)
            {
                deadline.tv_sec++;
                deadline.tv_nsec -= NSECPERSEC;
            }
        }
        else
        {
            LONGLONG unix_100ns = timeout->QuadPart - (LONGLONG)SECS_1601_TO_1970 * TICKSPERSEC;
            clk = CLOCK_REALTIME;
            if (unix_100ns <= 0)
            {
                release_local_sync_slot( slot, idx );
                return STATUS_TIMEOUT;
            }
            deadline.tv_sec = unix_100ns / TICKSPERSEC;
            deadline.tv_nsec = (unix_100ns % TICKSPERSEC) * 100;
        }
    }

    for (;;)
    {
        LONG cur;
        struct timespec rem, *pts = NULL;

        if ((promoted = __atomic_load_n( &slot->promoted_handle, __ATOMIC_ACQUIRE )))
        {
            release_local_sync_slot( slot, idx );
            if (has_deadline && timeout->QuadPart < 0)
            {
                struct timespec now;
                LARGE_INTEGER rem_li;
                clock_gettime( clk, &now );
                if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
                    rem_li.QuadPart = 0;
                else
                    rem_li.QuadPart = -((LONGLONG)(deadline.tv_sec - now.tv_sec) * TICKSPERSEC +
                                        (deadline.tv_nsec - now.tv_nsec) / 100);
                return NtWaitForSingleObject( promoted, FALSE, &rem_li );
            }
            return NtWaitForSingleObject( promoted, FALSE, timeout );
        }

        if (type == LOCAL_SYNC_EVENT_MANUAL)
        {
            cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
            if (cur & 1) { ret = STATUS_SUCCESS; break; }
        }
        else if (type == LOCAL_SYNC_EVENT_AUTO)
        {
            cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
            if ((cur & 1) && __atomic_compare_exchange_n( &slot->state, &cur, cur & ~1L, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED ))
            {
                ret = STATUS_SUCCESS;
                break;
            }
            if (cur & 1) continue;
        }
        else /* LOCAL_SYNC_SEMAPHORE */
        {
            cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
            if (cur > 0 && __atomic_compare_exchange_n( &slot->state, &cur, cur - 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED ))
            {
                ret = STATUS_SUCCESS;
                break;
            }
            if (cur > 0) continue;
        }

        if (has_deadline)
        {
            struct timespec now;
            clock_gettime( clk, &now );
            if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
            {
                ret = STATUS_TIMEOUT;
                break;
            }
            rem.tv_sec = deadline.tv_sec - now.tv_sec;
            rem.tv_nsec = deadline.tv_nsec - now.tv_nsec;
            if (rem.tv_nsec < 0)
            {
                rem.tv_sec--;
                rem.tv_nsec += NSECPERSEC;
            }
            pts = &rem;
        }

        __atomic_add_fetch( &slot->waiters, 1, __ATOMIC_SEQ_CST );
        cur = __atomic_load_n( &slot->state, __ATOMIC_SEQ_CST );
        if (__atomic_load_n( &slot->promoted_handle, __ATOMIC_ACQUIRE ) ||
            (type != LOCAL_SYNC_SEMAPHORE && (cur & 1)) ||
            (type == LOCAL_SYNC_SEMAPHORE && cur > 0))
        {
            __atomic_sub_fetch( &slot->waiters, 1, __ATOMIC_SEQ_CST );
            continue;
        }

        {
            int f_ret = futex_wait( &slot->state, cur, pts );
            int f_err = errno;
            __atomic_sub_fetch( &slot->waiters, 1, __ATOMIC_SEQ_CST );

            if (__atomic_load_n( &slot->promoted_handle, __ATOMIC_ACQUIRE ))
                continue;

            if (f_ret == 0 && type != LOCAL_SYNC_SEMAPHORE)
            {
                LONG new_cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
                if (new_cur != cur && !(new_cur & 1))
                {
                    if (type == LOCAL_SYNC_EVENT_MANUAL)
                    {
                        ret = STATUS_SUCCESS;
                        break;
                    }
                    else
                    {
                        LONG pc = __atomic_load_n( &slot->pulse_credits, __ATOMIC_ACQUIRE );
                        while (pc > 0)
                        {
                            if (__atomic_compare_exchange_n( &slot->pulse_credits, &pc, pc - 1, 1,
                                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE ))
                            {
                                release_local_sync_slot( slot, idx );
                                return STATUS_SUCCESS;
                            }
                        }
                    }
                }
            }
            if (f_ret < 0 && f_err == ETIMEDOUT)
            {
                ret = STATUS_TIMEOUT;
                break;
            }
        }
    }

    release_local_sync_slot( slot, idx );
    return ret;
}

#else

BOOL is_local_sync_handle( HANDLE handle ) { return FALSE; }
NTSTATUS close_local_sync_handle( HANDLE handle ) { return STATUS_INVALID_HANDLE; }
NTSTATUS promote_local_sync_handle( HANDLE handle, HANDLE *real_handle ) { return STATUS_INVALID_HANDLE; }
NTSTATUS retire_local_sync_handle( HANDLE handle, HANDLE *real_handle ) { return STATUS_INVALID_HANDLE; }

#endif /* __linux__ */


/******************************************************************************
 *              NtCreateSemaphore (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateSemaphore( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                                   LONG initial, LONG max )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "access %#x, name %s, initial %d, max %d\n", access,
           attr ? debugstr_us(attr->ObjectName) : "(null)", initial, max );

    *handle = 0;
    if (max <= 0 || initial < 0 || initial > max) return STATUS_INVALID_PARAMETER;
#ifdef __linux__
    {
        ACCESS_MASK norm_access;
        if (is_local_sync_eligible( attr ) && normalize_semaphore_access( access, &norm_access ))
        {
            struct local_sync_slot *slot;
            uint32_t idx = local_sync_alloc_slot( &slot );
            if (idx != LOCAL_SYNC_INVALID_IDX)
            {
                slot->lock = 0;
                slot->state = initial;
                slot->waiters = 0;
                slot->pulse_credits = 0;
                slot->max_count = max;
                slot->promoting = 0;
                slot->active_mutators = 0;
                slot->access = norm_access;
                slot->promoted_handle = NULL;
                __atomic_store_n( &slot->type, LOCAL_SYNC_SEMAPHORE, __ATOMIC_RELEASE );
                __atomic_store_n( &slot->refcount, 1, __ATOMIC_RELEASE );
                *handle = make_local_sync_handle( idx, slot->gen );
                return STATUS_SUCCESS;
            }
        }
    }
#endif

    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_semaphore )
    {
        req->access  = access;
        req->initial = initial;
        req->max     = max;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return ret;
}


/******************************************************************************
 *              NtOpenSemaphore (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenSemaphore( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    TRACE( "access %#x, name %s\n", access, attr ? debugstr_us(attr->ObjectName) : "(null)" );

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_semaphore )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtQuerySemaphore (NTDLL.@)
 */
NTSTATUS WINAPI NtQuerySemaphore( HANDLE handle, SEMAPHORE_INFORMATION_CLASS class,
                                  void *info, ULONG len, ULONG *ret_len )
{
    unsigned int ret;
    SEMAPHORE_BASIC_INFORMATION *out = info;

    TRACE("(%p, %u, %p, %u, %p)\n", handle, class, info, len, ret_len);

    if (class != SemaphoreBasicInformation)
    {
        FIXME("(%p,%d,%u) Unknown class\n", handle, class, len);
        return STATUS_INVALID_INFO_CLASS;
    }

    if (len != sizeof(SEMAPHORE_BASIC_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;

#ifdef __linux__
    if (is_local_sync_handle( handle ))
    {
        uint32_t idx;
        struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
        HANDLE promoted;

        if (!slot) return STATUS_INVALID_HANDLE;
        if ((promoted = __atomic_load_n( &slot->promoted_handle, __ATOMIC_ACQUIRE )))
        {
            release_local_sync_slot( slot, idx );
            return NtQuerySemaphore( promoted, class, info, len, ret_len );
        }
        if (slot->type != LOCAL_SYNC_SEMAPHORE)
        {
            release_local_sync_slot( slot, idx );
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!(slot->access & SEMAPHORE_QUERY_STATE))
        {
            release_local_sync_slot( slot, idx );
            return STATUS_ACCESS_DENIED;
        }
        out->CurrentCount = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        out->MaximumCount = slot->max_count;
        release_local_sync_slot( slot, idx );
        if (ret_len) *ret_len = sizeof(SEMAPHORE_BASIC_INFORMATION);
        return STATUS_SUCCESS;
    }
#endif

    if ((ret = inproc_query_semaphore( handle, out )) != STATUS_NOT_IMPLEMENTED)
    {
        if (!ret && ret_len) *ret_len = sizeof(SEMAPHORE_BASIC_INFORMATION);
        return ret;
    }

    SERVER_START_REQ( query_semaphore )
    {
        req->handle = wine_server_obj_handle( handle );
        if (!(ret = wine_server_call( req )))
        {
            out->CurrentCount = reply->current;
            out->MaximumCount = reply->max;
            if (ret_len) *ret_len = sizeof(SEMAPHORE_BASIC_INFORMATION);
        }
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtReleaseSemaphore (NTDLL.@)
 */
NTSTATUS WINAPI NtReleaseSemaphore( HANDLE handle, LONG count, ULONG *previous )
{
    unsigned int ret;

    TRACE( "handle %p, count %d, prev_count %p\n", handle, (int)count, previous );

    if (count <= 0)
        return STATUS_INVALID_PARAMETER;

#ifdef __linux__
    if (is_local_sync_handle( handle ))
    {
        uint32_t idx;
        struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
        HANDLE promoted;
        LONG cur;

        if (!slot) return STATUS_INVALID_HANDLE;
        if (slot->type != LOCAL_SYNC_SEMAPHORE)
        {
            release_local_sync_slot( slot, idx );
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!(slot->access & SEMAPHORE_MODIFY_STATE))
        {
            release_local_sync_slot( slot, idx );
            return STATUS_ACCESS_DENIED;
        }

        __atomic_add_fetch( &slot->active_mutators, 1, __ATOMIC_SEQ_CST );
        if (__atomic_load_n( &slot->promoting, __ATOMIC_SEQ_CST ))
        {
            __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );
            local_sync_spin_lock( &slot->lock );
            promoted = slot->promoted_handle;
            local_sync_spin_unlock( &slot->lock );
            release_local_sync_slot( slot, idx );
            if (promoted) return NtReleaseSemaphore( promoted, count, previous );
            return STATUS_INVALID_HANDLE;
        }

        cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        for (;;)
        {
            uint64_t sum = (uint64_t)(ULONG)cur + (uint64_t)(ULONG)count;
            if (sum > (uint64_t)slot->max_count)
            {
                __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );
                release_local_sync_slot( slot, idx );
                return STATUS_SEMAPHORE_LIMIT_EXCEEDED;
            }
            if (__atomic_compare_exchange_n( &slot->state, &cur, (LONG)sum, 1, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE ))
                break;
        }
        __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );

        if (previous) *previous = cur;
        if (__atomic_load_n( &slot->waiters, __ATOMIC_SEQ_CST ) > 0)
            futex_wake_n( &slot->state, (int)count );
        release_local_sync_slot( slot, idx );
        return STATUS_SUCCESS;
    }
#endif

    if ((ret = inproc_release_semaphore( handle, count, previous )) != STATUS_NOT_IMPLEMENTED)
        return ret;

    SERVER_START_REQ( release_semaphore )
    {
        req->handle = wine_server_obj_handle( handle );
        req->count  = count;
        if (!(ret = wine_server_call( req )))
        {
            if (previous) *previous = reply->prev_count;
        }
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *              NtCreateEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateEvent( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                               EVENT_TYPE type, BOOLEAN state )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "access %#x, name %s, type %u, state %u\n", access,
           attr ? debugstr_us(attr->ObjectName) : "(null)", type, state );

    *handle = 0;
    if (type != NotificationEvent && type != SynchronizationEvent) return STATUS_INVALID_PARAMETER;
#ifdef __linux__
    {
        ACCESS_MASK norm_access;
        if (is_local_sync_eligible( attr ) && normalize_event_access( access, &norm_access ))
        {
            struct local_sync_slot *slot;
            uint32_t idx = local_sync_alloc_slot( &slot );
            if (idx != LOCAL_SYNC_INVALID_IDX)
            {
                slot->lock = 0;
                slot->state = state ? 1 : 0;
                slot->waiters = 0;
                slot->pulse_credits = 0;
                slot->max_count = 0;
                slot->promoting = 0;
                slot->active_mutators = 0;
                slot->access = norm_access;
                slot->promoted_handle = NULL;
                __atomic_store_n( &slot->type, (type == NotificationEvent) ? LOCAL_SYNC_EVENT_MANUAL : LOCAL_SYNC_EVENT_AUTO, __ATOMIC_RELEASE );
                __atomic_store_n( &slot->refcount, 1, __ATOMIC_RELEASE );
                *handle = make_local_sync_handle( idx, slot->gen );
                return STATUS_SUCCESS;
            }
        }
    }
#endif

    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_event )
    {
        req->access = access;
        req->manual_reset = (type == NotificationEvent);
        req->initial_state = state;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return ret;
}


/******************************************************************************
 *              NtOpenEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenEvent( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    TRACE( "access %#x, name %s\n", access, attr ? debugstr_us(attr->ObjectName) : "(null)" );

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_event )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtSetEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtSetEvent( HANDLE handle, LONG *prev_state )
{
    struct inproc_sync stack, *sync;
    BOOL has_wait_packet = 0;
    unsigned int ret;

    TRACE( "handle %p, prev_state %p\n", handle, prev_state );

#ifdef __linux__
    if (is_local_sync_handle( handle ))
    {
        uint32_t idx;
        struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
        HANDLE promoted;
        LONG cur;

        if (!slot) return STATUS_INVALID_HANDLE;
        if (slot->type != LOCAL_SYNC_EVENT_MANUAL && slot->type != LOCAL_SYNC_EVENT_AUTO)
        {
            release_local_sync_slot( slot, idx );
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!(slot->access & EVENT_MODIFY_STATE))
        {
            release_local_sync_slot( slot, idx );
            return STATUS_ACCESS_DENIED;
        }

        __atomic_add_fetch( &slot->active_mutators, 1, __ATOMIC_SEQ_CST );
        if (__atomic_load_n( &slot->promoting, __ATOMIC_SEQ_CST ))
        {
            __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );
            local_sync_spin_lock( &slot->lock );
            promoted = slot->promoted_handle;
            local_sync_spin_unlock( &slot->lock );
            release_local_sync_slot( slot, idx );
            if (promoted) return NtSetEvent( promoted, prev_state );
            return STATUS_INVALID_HANDLE;
        }

        cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        while (!(cur & 1))
        {
            if (__atomic_compare_exchange_n( &slot->state, &cur, cur | 1L, 1, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE ))
                break;
        }
        __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );

        if (prev_state) *prev_state = cur & 1;
        if (!(cur & 1) && __atomic_load_n( &slot->waiters, __ATOMIC_SEQ_CST ) > 0)
            futex_wake_n( &slot->state, (slot->type == LOCAL_SYNC_EVENT_MANUAL) ? INT_MAX : 1 );
        release_local_sync_slot( slot, idx );
        return STATUS_SUCCESS;
    }
#endif

    if (!get_inproc_sync( handle, INPROC_SYNC_EVENT, 0, &stack, &sync ))
    {
        has_wait_packet = sync->has_wait_packet;
        release_inproc_sync( sync );
    }

    if ((ret = inproc_set_event( handle, prev_state )) != STATUS_NOT_IMPLEMENTED)
    {
        if (!has_wait_packet) return ret;
    }

    SERVER_START_REQ( event_op )
    {
        req->handle = wine_server_obj_handle( handle );
        req->op     = SET_EVENT;
        ret = wine_server_call( req );
        if (!ret && prev_state) *prev_state = reply->state;
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtSetEventBoostPriority (NTDLL.@)
 */
NTSTATUS WINAPI NtSetEventBoostPriority( HANDLE handle )
{
    return NtSetEvent( handle, NULL );
}


/******************************************************************************
 *              NtResetEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtResetEvent( HANDLE handle, LONG *prev_state )
{
    unsigned int ret;

    TRACE( "handle %p, prev_state %p\n", handle, prev_state );

#ifdef __linux__
    if (is_local_sync_handle( handle ))
    {
        uint32_t idx;
        struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
        HANDLE promoted;
        LONG cur;

        if (!slot) return STATUS_INVALID_HANDLE;
        if (slot->type != LOCAL_SYNC_EVENT_MANUAL && slot->type != LOCAL_SYNC_EVENT_AUTO)
        {
            release_local_sync_slot( slot, idx );
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!(slot->access & EVENT_MODIFY_STATE))
        {
            release_local_sync_slot( slot, idx );
            return STATUS_ACCESS_DENIED;
        }

        __atomic_add_fetch( &slot->active_mutators, 1, __ATOMIC_SEQ_CST );
        if (__atomic_load_n( &slot->promoting, __ATOMIC_SEQ_CST ))
        {
            __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );
            local_sync_spin_lock( &slot->lock );
            promoted = slot->promoted_handle;
            local_sync_spin_unlock( &slot->lock );
            release_local_sync_slot( slot, idx );
            if (promoted) return NtResetEvent( promoted, prev_state );
            return STATUS_INVALID_HANDLE;
        }

        cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        while (cur & 1)
        {
            if (__atomic_compare_exchange_n( &slot->state, &cur, cur & ~1L, 1, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE ))
                break;
        }
        __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );

        if (prev_state) *prev_state = cur & 1;
        release_local_sync_slot( slot, idx );
        return STATUS_SUCCESS;
    }
#endif

    if ((ret = inproc_reset_event( handle, prev_state )) != STATUS_NOT_IMPLEMENTED)
        return ret;

    SERVER_START_REQ( event_op )
    {
        req->handle = wine_server_obj_handle( handle );
        req->op     = RESET_EVENT;
        ret = wine_server_call( req );
        if (!ret && prev_state) *prev_state = reply->state;
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtClearEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtClearEvent( HANDLE handle )
{
    /* FIXME: same as NtResetEvent ??? */
    return NtResetEvent( handle, NULL );
}


/******************************************************************************
 *              NtPulseEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtPulseEvent( HANDLE handle, LONG *prev_state )
{
    unsigned int ret;

    TRACE( "handle %p, prev_state %p\n", handle, prev_state );

#ifdef __linux__
    if (is_local_sync_handle( handle ))
    {
        uint32_t idx;
        struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
        HANDLE promoted;
        LONG cur, waiters;

        if (!slot) return STATUS_INVALID_HANDLE;
        if (slot->type != LOCAL_SYNC_EVENT_MANUAL && slot->type != LOCAL_SYNC_EVENT_AUTO)
        {
            release_local_sync_slot( slot, idx );
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!(slot->access & EVENT_MODIFY_STATE))
        {
            release_local_sync_slot( slot, idx );
            return STATUS_ACCESS_DENIED;
        }

        __atomic_add_fetch( &slot->active_mutators, 1, __ATOMIC_SEQ_CST );
        if (__atomic_load_n( &slot->promoting, __ATOMIC_SEQ_CST ))
        {
            __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );
            local_sync_spin_lock( &slot->lock );
            promoted = slot->promoted_handle;
            local_sync_spin_unlock( &slot->lock );
            release_local_sync_slot( slot, idx );
            if (promoted) return NtPulseEvent( promoted, prev_state );
            return STATUS_INVALID_HANDLE;
        }

        waiters = __atomic_load_n( &slot->waiters, __ATOMIC_SEQ_CST );
        if (waiters > 0 && slot->type == LOCAL_SYNC_EVENT_AUTO)
            __atomic_add_fetch( &slot->pulse_credits, 1, __ATOMIC_SEQ_CST );

        cur = __atomic_load_n( &slot->state, __ATOMIC_ACQUIRE );
        for (;;)
        {
            LONG next = (waiters > 0) ? ((cur & ~1L) + 2) : (cur & ~1L);
            if (__atomic_compare_exchange_n( &slot->state, &cur, next, 1, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE ))
                break;
        }
        __atomic_sub_fetch( &slot->active_mutators, 1, __ATOMIC_RELEASE );

        if (prev_state) *prev_state = cur & 1;
        if (waiters > 0)
            futex_wake_n( &slot->state, (slot->type == LOCAL_SYNC_EVENT_MANUAL) ? INT_MAX : 1 );
        release_local_sync_slot( slot, idx );
        return STATUS_SUCCESS;
    }
#endif

    if ((ret = inproc_pulse_event( handle, prev_state )) != STATUS_NOT_IMPLEMENTED)
        return ret;

    SERVER_START_REQ( event_op )
    {
        req->handle = wine_server_obj_handle( handle );
        req->op     = PULSE_EVENT;
        ret = wine_server_call( req );
        if (!ret && prev_state) *prev_state = reply->state;
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtQueryEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryEvent( HANDLE handle, EVENT_INFORMATION_CLASS class,
                              void *info, ULONG len, ULONG *ret_len )
{
    unsigned int ret;
    EVENT_BASIC_INFORMATION *out = info;

    TRACE("(%p, %u, %p, %u, %p)\n", handle, class, info, len, ret_len);

    if (class != EventBasicInformation)
    {
        FIXME("(%p, %d, %d) Unknown class\n", handle, class, len);
        return STATUS_INVALID_INFO_CLASS;
    }

    if (len != sizeof(EVENT_BASIC_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;

#ifdef __linux__
    if (is_local_sync_handle( handle ))
    {
        uint32_t idx;
        struct local_sync_slot *slot = acquire_local_sync_slot( handle, &idx );
        HANDLE promoted;

        if (!slot) return STATUS_INVALID_HANDLE;
        if ((promoted = __atomic_load_n( &slot->promoted_handle, __ATOMIC_ACQUIRE )))
        {
            release_local_sync_slot( slot, idx );
            return NtQueryEvent( promoted, class, info, len, ret_len );
        }
        if (slot->type != LOCAL_SYNC_EVENT_MANUAL && slot->type != LOCAL_SYNC_EVENT_AUTO)
        {
            release_local_sync_slot( slot, idx );
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        if (!(slot->access & EVENT_QUERY_STATE))
        {
            release_local_sync_slot( slot, idx );
            return STATUS_ACCESS_DENIED;
        }
        out->EventType = (slot->type == LOCAL_SYNC_EVENT_MANUAL) ? NotificationEvent : SynchronizationEvent;
        out->EventState = (__atomic_load_n( &slot->state, __ATOMIC_ACQUIRE ) & 1) != 0;
        release_local_sync_slot( slot, idx );
        if (ret_len) *ret_len = sizeof(EVENT_BASIC_INFORMATION);
        return STATUS_SUCCESS;
    }
#endif

    if ((ret = inproc_query_event( handle, out )) != STATUS_NOT_IMPLEMENTED)
    {
        if (!ret && ret_len) *ret_len = sizeof(EVENT_BASIC_INFORMATION);
        return ret;
    }

    SERVER_START_REQ( query_event )
    {
        req->handle = wine_server_obj_handle( handle );
        if (!(ret = wine_server_call( req )))
        {
            out->EventType  = reply->manual_reset ? NotificationEvent : SynchronizationEvent;
            out->EventState = reply->state;
            if (ret_len) *ret_len = sizeof(EVENT_BASIC_INFORMATION);
        }
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *              NtCreateMutant (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateMutant( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                                BOOLEAN owned )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "access %#x, name %s, owned %u\n", access,
           attr ? debugstr_us(attr->ObjectName) : "(null)", owned );

    *handle = 0;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_mutex )
    {
        req->access  = access;
        req->owned   = owned;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return ret;
}


/**************************************************************************
 *              NtOpenMutant (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenMutant( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    TRACE( "access %#x, name %s\n", access, attr ? debugstr_us(attr->ObjectName) : "(null)" );

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_mutex )
    {
        req->access  = access;
        req->attributes = attr->Attributes;
        req->rootdir = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *              NtReleaseMutant (NTDLL.@)
 */
NTSTATUS WINAPI NtReleaseMutant( HANDLE handle, LONG *prev_count )
{
    unsigned int ret;

    TRACE( "handle %p, prev_count %p\n", handle, prev_count );

    if ((ret = inproc_release_mutex( handle, prev_count )) != STATUS_NOT_IMPLEMENTED)
        return ret;

    SERVER_START_REQ( release_mutex )
    {
        req->handle = wine_server_obj_handle( handle );
        ret = wine_server_call( req );
        if (prev_count) *prev_count = 1 - reply->prev_count;
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************
 *              NtQueryMutant (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryMutant( HANDLE handle, MUTANT_INFORMATION_CLASS class,
                               void *info, ULONG len, ULONG *ret_len )
{
    unsigned int ret;
    MUTANT_BASIC_INFORMATION *out = info;

    TRACE("(%p, %u, %p, %u, %p)\n", handle, class, info, len, ret_len);

    if (class != MutantBasicInformation)
    {
        FIXME( "(%p, %d, %d) Unknown class\n", handle, class, len );
        return STATUS_INVALID_INFO_CLASS;
    }

    if (len != sizeof(MUTANT_BASIC_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;

    if ((ret = inproc_query_mutex( handle, out )) != STATUS_NOT_IMPLEMENTED)
    {
        if (!ret && ret_len) *ret_len = sizeof(MUTANT_BASIC_INFORMATION);
        return ret;
    }

    SERVER_START_REQ( query_mutex )
    {
        req->handle = wine_server_obj_handle( handle );
        if (!(ret = wine_server_call( req )))
        {
            out->CurrentCount   = 1 - reply->count;
            out->OwnedByCaller  = reply->owned;
            out->AbandonedState = reply->abandoned;
            if (ret_len) *ret_len = sizeof(MUTANT_BASIC_INFORMATION);
        }
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *		NtCreateJobObject (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateJobObject( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    *handle = 0;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_job )
    {
        req->access = access;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    free( objattr );
    return ret;
}


/**************************************************************************
 *		NtOpenJobObject (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenJobObject( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_job )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *		NtTerminateJobObject (NTDLL.@)
 */
NTSTATUS WINAPI NtTerminateJobObject( HANDLE handle, NTSTATUS status )
{
    unsigned int ret;

    TRACE( "(%p, %d)\n", handle, status );

    SERVER_START_REQ( terminate_job )
    {
        req->handle = wine_server_obj_handle( handle );
        req->status = status;
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;

    return ret;
}


/**************************************************************************
 *		NtQueryInformationJobObject (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryInformationJobObject( HANDLE handle, JOBOBJECTINFOCLASS class, void *info,
                                             ULONG len, ULONG *ret_len )
{
    unsigned int ret;

    TRACE( "semi-stub: %p %u %p %u %p\n", handle, class, info, len, ret_len );

    if (class >= MaxJobObjectInfoClass) return STATUS_INVALID_PARAMETER;

    switch (class)
    {
    case JobObjectBasicAccountingInformation:
    {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION *accounting = info;

        if (len < sizeof(*accounting)) return STATUS_INFO_LENGTH_MISMATCH;
        SERVER_START_REQ(get_job_info)
        {
            req->handle = wine_server_obj_handle( handle );
            if (!(ret = wine_server_call( req )))
            {
                memset( accounting, 0, sizeof(*accounting) );
                accounting->TotalProcesses = reply->total_processes;
                accounting->ActiveProcesses = reply->active_processes;
            }
        }
        SERVER_END_REQ;
        if (ret_len) *ret_len = sizeof(*accounting);
        return ret;
    }
    case JobObjectBasicProcessIdList:
    {
        JOBOBJECT_BASIC_PROCESS_ID_LIST *process = info;
        DWORD count, i;

        if (len < sizeof(*process)) return STATUS_INFO_LENGTH_MISMATCH;

        count  = len - offsetof( JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList );
        count /= sizeof(process->ProcessIdList[0]);

        SERVER_START_REQ( get_job_info )
        {
            req->handle = wine_server_user_handle(handle);
            wine_server_set_reply(req, process->ProcessIdList, count * sizeof(process_id_t));
            if (!(ret = wine_server_call(req)))
            {
                process->NumberOfAssignedProcesses = reply->active_processes;
                process->NumberOfProcessIdsInList = min(count, reply->active_processes);
            }
        }
        SERVER_END_REQ;

        if (ret != STATUS_SUCCESS) return ret;

        if (sizeof(process_id_t) < sizeof(process->ProcessIdList[0]))
        {
            /* start from the end to not overwrite */
            for (i = process->NumberOfProcessIdsInList; i--;)
            {
                ULONG_PTR id = ((process_id_t *)process->ProcessIdList)[i];
                process->ProcessIdList[i] = id;
            }
        }

        if (ret_len)
            *ret_len = offsetof( JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList[process->NumberOfProcessIdsInList] );
        return count < process->NumberOfAssignedProcesses ? STATUS_MORE_ENTRIES : STATUS_SUCCESS;
    }
    case JobObjectExtendedLimitInformation:
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION *extended_limit = info;

        if (len < sizeof(*extended_limit)) return STATUS_INFO_LENGTH_MISMATCH;
        memset( extended_limit, 0, sizeof(*extended_limit) );
        SERVER_START_REQ( get_job_info )
        {
            req->handle = wine_server_obj_handle( handle );
            ret = wine_server_call( req );
            if (ret == STATUS_SUCCESS)
                extended_limit->BasicLimitInformation.LimitFlags = reply->limit_flags;
        }
        SERVER_END_REQ;
        if (ret != STATUS_SUCCESS) return ret;
        if (ret_len) *ret_len = sizeof(*extended_limit);
        return STATUS_SUCCESS;
    }
    case JobObjectBasicLimitInformation:
    {
        JOBOBJECT_BASIC_LIMIT_INFORMATION *basic_limit = info;

        if (len < sizeof(*basic_limit)) return STATUS_INFO_LENGTH_MISMATCH;
        memset( basic_limit, 0, sizeof(*basic_limit) );
        SERVER_START_REQ( get_job_info )
        {
            req->handle = wine_server_obj_handle( handle );
            ret = wine_server_call( req );
            if (ret == STATUS_SUCCESS)
                basic_limit->LimitFlags = reply->limit_flags;
        }
        SERVER_END_REQ;
        if (ret != STATUS_SUCCESS) return ret;
        if (ret_len) *ret_len = sizeof(*basic_limit);
        return STATUS_SUCCESS;
    }
    case JobObjectSecurityLimitInformation:
    {
        JOBOBJECT_SECURITY_LIMIT_INFORMATION *sec_limit = info;

        if (len < sizeof(*sec_limit)) return STATUS_INFO_LENGTH_MISMATCH;
        memset( sec_limit, 0, sizeof(*sec_limit) );
        if (ret_len) *ret_len = sizeof(*sec_limit);
        return STATUS_SUCCESS;
    }
    case JobObjectBasicUIRestrictions:
    {
        JOBOBJECT_BASIC_UI_RESTRICTIONS *ui_limit = info;

        if (len < sizeof(*ui_limit)) return STATUS_INFO_LENGTH_MISMATCH;
        memset( ui_limit, 0, sizeof(*ui_limit) );
        if (ret_len) *ret_len = sizeof(*ui_limit);
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_NOT_IMPLEMENTED;
    }
}


/**************************************************************************
 *		NtSetInformationJobObject (NTDLL.@)
 */
NTSTATUS WINAPI NtSetInformationJobObject( HANDLE handle, JOBOBJECTINFOCLASS class, void *info, ULONG len )
{
    unsigned int status = STATUS_NOT_IMPLEMENTED;
    JOBOBJECT_BASIC_LIMIT_INFORMATION *basic_limit;
    ULONG info_size = sizeof(JOBOBJECT_BASIC_LIMIT_INFORMATION);
    DWORD limit_flags = JOB_OBJECT_BASIC_LIMIT_VALID_FLAGS;

    TRACE( "(%p, %u, %p, %u)\n", handle, class, info, len );

    if (class >= MaxJobObjectInfoClass) return STATUS_INVALID_PARAMETER;

    switch (class)
    {

    case JobObjectExtendedLimitInformation:
        info_size = sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION);
        limit_flags = JOB_OBJECT_LIMIT_VALID_FLAGS | 0x00ff0000;
        /* fall through */
    case JobObjectBasicLimitInformation:
        if (class == JobObjectBasicLimitInformation)
            limit_flags = (JOB_OBJECT_LIMIT_VALID_FLAGS & ~(JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY)) | 0x00ff0000;
        if (len != info_size) return STATUS_INVALID_PARAMETER;
        basic_limit = info;
        if (basic_limit->LimitFlags & ~limit_flags)
        {
            WARN( "Ignoring unsupported limit flags %#x\n", (unsigned int)(basic_limit->LimitFlags & ~limit_flags) );
        }
        SERVER_START_REQ( set_job_limits )
        {
            req->handle = wine_server_obj_handle( handle );
            req->limit_flags = basic_limit->LimitFlags;
            status = wine_server_call( req );
        }
        SERVER_END_REQ;
        break;
    case JobObjectAssociateCompletionPortInformation:
        if (len != sizeof(JOBOBJECT_ASSOCIATE_COMPLETION_PORT)) return STATUS_INVALID_PARAMETER;
        SERVER_START_REQ( set_job_completion_port )
        {
            JOBOBJECT_ASSOCIATE_COMPLETION_PORT *port_info = info;
            req->job = wine_server_obj_handle( handle );
            req->port = wine_server_obj_handle( port_info->CompletionPort );
            req->key = wine_server_client_ptr( port_info->CompletionKey );
            status = wine_server_call( req );
        }
        SERVER_END_REQ;
        break;
    case JobObjectSecurityLimitInformation:
        if (len < sizeof(JOBOBJECT_SECURITY_LIMIT_INFORMATION)) return STATUS_INVALID_PARAMETER;
        FIXME( "JobObjectSecurityLimitInformation stub: %p %p %u\n", handle, info, len );
        status = STATUS_SUCCESS;
        break;
    case JobObjectBasicUIRestrictions:
        status = STATUS_SUCCESS;
        break;
    default:
        FIXME( "stub: %p %u %p %u\n", handle, class, info, len );
    }
    return status;
}


/**************************************************************************
 *		NtIsProcessInJob (NTDLL.@)
 */
NTSTATUS WINAPI NtIsProcessInJob( HANDLE process, HANDLE job )
{
    unsigned int status;

    TRACE( "(%p %p)\n", job, process );

    SERVER_START_REQ( process_in_job )
    {
        req->job     = wine_server_obj_handle( job );
        req->process = wine_server_obj_handle( process );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}


/**************************************************************************
 *		NtAssignProcessToJobObject (NTDLL.@)
 */
NTSTATUS WINAPI NtAssignProcessToJobObject( HANDLE job, HANDLE process )
{
    unsigned int status;

    TRACE( "(%p %p)\n", job, process );

    SERVER_START_REQ( assign_job )
    {
        req->job     = wine_server_obj_handle( job );
        req->process = wine_server_obj_handle( process );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}


/**********************************************************************
 *           NtCreateDebugObject  (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateDebugObject( HANDLE *handle, ACCESS_MASK access,
                                     OBJECT_ATTRIBUTES *attr, ULONG flags )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    *handle = 0;
    if (flags & ~DEBUG_KILL_ON_CLOSE) return STATUS_INVALID_PARAMETER;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_debug_obj )
    {
        req->access = access;
        req->flags  = flags;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    free( objattr );
    return ret;
}


/**********************************************************************
 *           NtSetInformationDebugObject  (NTDLL.@)
 */
NTSTATUS WINAPI NtSetInformationDebugObject( HANDLE handle, DEBUGOBJECTINFOCLASS class,
                                             void *info, ULONG len, ULONG *ret_len )
{
    unsigned int ret;
    ULONG flags;

    if (class != DebugObjectKillProcessOnExitInformation) return STATUS_INVALID_PARAMETER;
    if (len != sizeof(ULONG))
    {
        if (ret_len) *ret_len = sizeof(ULONG);
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    flags = *(ULONG *)info;
    if (flags & ~DEBUG_KILL_ON_CLOSE) return STATUS_INVALID_PARAMETER;

    SERVER_START_REQ( set_debug_obj_info )
    {
        req->debug = wine_server_obj_handle( handle );
        req->flags = flags;
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    if (!ret && ret_len) *ret_len = 0;
    return ret;
}


/* convert the server event data to an NT state change; helper for NtWaitForDebugEvent */
static NTSTATUS event_data_to_state_change( const union debug_event_data *data, DBGUI_WAIT_STATE_CHANGE *state )
{
    int i;

    switch (data->code)
    {
    case DbgIdle:
    case DbgReplyPending:
        return STATUS_PENDING;
    case DbgCreateThreadStateChange:
    {
        DBGUI_CREATE_THREAD *info = &state->StateInfo.CreateThread;
        info->HandleToThread         = wine_server_ptr_handle( data->create_thread.handle );
        info->NewThread.StartAddress = wine_server_get_ptr( data->create_thread.start );
        return STATUS_SUCCESS;
    }
    case DbgCreateProcessStateChange:
    {
        DBGUI_CREATE_PROCESS *info = &state->StateInfo.CreateProcessInfo;
        info->HandleToProcess                       = wine_server_ptr_handle( data->create_process.process );
        info->HandleToThread                        = wine_server_ptr_handle( data->create_process.thread );
        info->NewProcess.FileHandle                 = wine_server_ptr_handle( data->create_process.file );
        info->NewProcess.BaseOfImage                = wine_server_get_ptr( data->create_process.base );
        info->NewProcess.DebugInfoFileOffset        = data->create_process.dbg_offset;
        info->NewProcess.DebugInfoSize              = data->create_process.dbg_size;
        info->NewProcess.InitialThread.StartAddress = wine_server_get_ptr( data->create_process.start );
        return STATUS_SUCCESS;
    }
    case DbgExitThreadStateChange:
        state->StateInfo.ExitThread.ExitStatus = data->exit.exit_code;
        return STATUS_SUCCESS;
    case DbgExitProcessStateChange:
        state->StateInfo.ExitProcess.ExitStatus = data->exit.exit_code;
        return STATUS_SUCCESS;
    case DbgExceptionStateChange:
    case DbgBreakpointStateChange:
    case DbgSingleStepStateChange:
    {
        DBGKM_EXCEPTION *info = &state->StateInfo.Exception;
        info->FirstChance = data->exception.first;
        info->ExceptionRecord.ExceptionCode    = data->exception.exc_code;
        info->ExceptionRecord.ExceptionFlags   = data->exception.flags;
        info->ExceptionRecord.ExceptionRecord  = wine_server_get_ptr( data->exception.record );
        info->ExceptionRecord.ExceptionAddress = wine_server_get_ptr( data->exception.address );
        info->ExceptionRecord.NumberParameters = data->exception.nb_params;
        for (i = 0; i < data->exception.nb_params; i++)
            info->ExceptionRecord.ExceptionInformation[i] = data->exception.params[i];
        return STATUS_SUCCESS;
    }
    case DbgLoadDllStateChange:
    {
        DBGKM_LOAD_DLL *info = &state->StateInfo.LoadDll;
        info->FileHandle          = wine_server_ptr_handle( data->load_dll.handle );
        info->BaseOfDll           = wine_server_get_ptr( data->load_dll.base );
        info->DebugInfoFileOffset = data->load_dll.dbg_offset;
        info->DebugInfoSize       = data->load_dll.dbg_size;
        info->NamePointer         = wine_server_get_ptr( data->load_dll.name );
        if ((DWORD_PTR)data->load_dll.base != data->load_dll.base)
            return STATUS_PARTIAL_COPY;
        return STATUS_SUCCESS;
    }
    case DbgUnloadDllStateChange:
        state->StateInfo.UnloadDll.BaseAddress = wine_server_get_ptr( data->unload_dll.base );
        if ((DWORD_PTR)data->unload_dll.base != data->unload_dll.base)
            return STATUS_PARTIAL_COPY;
        return STATUS_SUCCESS;
    }
    return STATUS_INTERNAL_ERROR;
}

#ifndef _WIN64
/* helper to NtWaitForDebugEvent; retrieve machine from PE image */
static NTSTATUS get_image_machine( HANDLE handle, USHORT *machine )
{
    IMAGE_DOS_HEADER dos_hdr;
    IMAGE_NT_HEADERS nt_hdr;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER offset;
    FILE_POSITION_INFORMATION pos_info;
    NTSTATUS status;

    offset.QuadPart = 0;
    status = NtReadFile( handle, NULL, NULL, NULL,
                         &iosb, &dos_hdr, sizeof(dos_hdr), &offset, NULL );
    if (!status)
    {
        offset.QuadPart = dos_hdr.e_lfanew;
        status = NtReadFile( handle, NULL, NULL, NULL, &iosb,
                             &nt_hdr, FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader), &offset, NULL );
        if (!status)
            *machine = nt_hdr.FileHeader.Machine;
        /* Reset file pos at beginning of file */
        pos_info.CurrentByteOffset.QuadPart = 0;
        NtSetInformationFile( handle, &iosb, &pos_info, sizeof(pos_info), FilePositionInformation );
    }
    return status;
}
#endif

/**********************************************************************
 *           NtWaitForDebugEvent  (NTDLL.@)
 */
NTSTATUS WINAPI NtWaitForDebugEvent( HANDLE handle, BOOLEAN alertable, LARGE_INTEGER *timeout,
                                     DBGUI_WAIT_STATE_CHANGE *state )
{
    union debug_event_data data;
    unsigned int ret;
    BOOL wait = TRUE;

    for (;;)
    {
        SERVER_START_REQ( wait_debug_event )
        {
            req->debug = wine_server_obj_handle( handle );
            wine_server_set_reply( req, &data, sizeof(data) );
            ret = wine_server_call( req );
            if (!ret)
            {
                ret = event_data_to_state_change( &data, state );
                state->NewState = data.code;
                state->AppClientId = make_client_id( reply->pid, reply->tid );
            }
        }
        SERVER_END_REQ;

#ifndef _WIN64
        /* don't pass 64bit load events to 32bit callers */
        if (!ret && state->NewState == DbgLoadDllStateChange)
        {
            USHORT machine;
            if (!get_image_machine( state->StateInfo.LoadDll.FileHandle, &machine ) &&
                machine != current_machine)
                ret = STATUS_PARTIAL_COPY;
        }
        if (ret == STATUS_PARTIAL_COPY)
        {
            if (state->NewState == DbgLoadDllStateChange)
                NtClose( state->StateInfo.LoadDll.FileHandle );
            NtDebugContinue( handle, &state->AppClientId, DBG_CONTINUE );
            wait = TRUE;
            continue;
        }
#endif
        if (ret != STATUS_PENDING) return ret;
        if (!wait) return STATUS_TIMEOUT;
        wait = FALSE;
        ret = NtWaitForSingleObject( handle, alertable, timeout );
        if (ret != STATUS_WAIT_0) return ret;
    }
}


/**************************************************************************
 *           NtCreateDirectoryObject   (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateDirectoryObject( HANDLE *handle, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    *handle = 0;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_directory )
    {
        req->access = access;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    free( objattr );
    return ret;
}


/**************************************************************************
 *           NtOpenDirectoryObject   (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenDirectoryObject( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_directory )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *           NtQueryDirectoryObject   (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryDirectoryObject( HANDLE handle, DIRECTORY_BASIC_INFORMATION *buffer,
                                        ULONG size, BOOLEAN single_entry, BOOLEAN restart,
                                        ULONG *context, ULONG *ret_size )
{
    unsigned int status, i, count, total_len, pos, used_size, used_count, strpool_head;
    ULONG index = restart ? 0 : *context;
    struct directory_entry *entries;

    if (!(entries = malloc( size ))) return STATUS_NO_MEMORY;

    SERVER_START_REQ( get_directory_entries )
    {
        req->handle = wine_server_obj_handle( handle );
        req->index = index;
        req->max_count = single_entry ? 1 : UINT_MAX;
        wine_server_set_reply( req, entries, size );
        status = wine_server_call( req );
        count = reply->count;
        total_len = reply->total_len;
    }
    SERVER_END_REQ;

    if (status && status != STATUS_MORE_ENTRIES)
    {
        free( entries );
        return status;
    }

    used_count = 0;
    used_size = sizeof(*buffer);  /* "null terminator" entry */
    for (i = pos = 0; i < count; i++)
    {
        const struct directory_entry *entry = (const struct directory_entry *)((char *)entries + pos);
        unsigned int entry_size = sizeof(*buffer) + entry->name_len + entry->type_len + 2 * sizeof(WCHAR);

        if (used_size + entry_size > size)
        {
            status = STATUS_MORE_ENTRIES;
            break;
        }
        used_count++;
        used_size += entry_size;
        pos += sizeof(*entry) + ((entry->name_len + entry->type_len + 3) & ~3);
    }

    /*
     * Avoid making strpool_head a pointer, since it can point beyond end
     * of the buffer.  Out-of-bounds pointers trigger undefined behavior
     * just by existing, even when they are never dereferenced.
     */
    strpool_head = sizeof(*buffer) * (used_count + 1);  /* after the "null terminator" entry */
    for (i = pos = 0; i < used_count; i++)
    {
        const struct directory_entry *entry = (const struct directory_entry *)((char *)entries + pos);

        buffer[i].ObjectName.Buffer = (WCHAR *)((char *)buffer + strpool_head);
        buffer[i].ObjectName.Length = entry->name_len;
        buffer[i].ObjectName.MaximumLength = entry->name_len + sizeof(WCHAR);
        memcpy( buffer[i].ObjectName.Buffer, (entry + 1), entry->name_len );
        buffer[i].ObjectName.Buffer[entry->name_len / sizeof(WCHAR)] = 0;
        strpool_head += entry->name_len + sizeof(WCHAR);

        buffer[i].ObjectTypeName.Buffer = (WCHAR *)((char *)buffer + strpool_head);
        buffer[i].ObjectTypeName.Length = entry->type_len;
        buffer[i].ObjectTypeName.MaximumLength = entry->type_len + sizeof(WCHAR);
        memcpy( buffer[i].ObjectTypeName.Buffer, (char *)(entry + 1) + entry->name_len, entry->type_len );
        buffer[i].ObjectTypeName.Buffer[entry->type_len / sizeof(WCHAR)] = 0;
        strpool_head += entry->type_len + sizeof(WCHAR);

        pos += sizeof(*entry) + ((entry->name_len + entry->type_len + 3) & ~3);
    }

    if (size >= sizeof(*buffer))
        memset( &buffer[used_count], 0, sizeof(buffer[used_count]) );

    free( entries );

    if (!count && !status)
    {
        if (ret_size) *ret_size = sizeof(*buffer);
        return STATUS_NO_MORE_ENTRIES;
    }

    if (single_entry && !used_count)
    {
        if (ret_size) *ret_size = 2 * sizeof(*buffer) + 2 * sizeof(WCHAR) + total_len;
        return STATUS_BUFFER_TOO_SMALL;
    }

    *context = index + used_count;
    if (ret_size) *ret_size = strpool_head;
    return status;
}


/**************************************************************************
 *           NtCreateSymbolicLinkObject   (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateSymbolicLinkObject( HANDLE *handle, ACCESS_MASK access,
                                            OBJECT_ATTRIBUTES *attr, UNICODE_STRING *target )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    *handle = 0;
    if (!target->MaximumLength) return STATUS_INVALID_PARAMETER;
    if (!target->Buffer) return STATUS_ACCESS_VIOLATION;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_symlink )
    {
        req->access = access;
        wine_server_add_data( req, objattr, len );
        wine_server_add_data( req, target->Buffer, target->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    free( objattr );
    return ret;
}


/**************************************************************************
 *           NtOpenSymbolicLinkObject   (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenSymbolicLinkObject( HANDLE *handle, ACCESS_MASK access,
                                          const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_symlink )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *           NtQuerySymbolicLinkObject   (NTDLL.@)
 */
NTSTATUS WINAPI NtQuerySymbolicLinkObject( HANDLE handle, UNICODE_STRING *target, ULONG *length )
{
    unsigned int ret;

    if (!target) return STATUS_ACCESS_VIOLATION;

    SERVER_START_REQ( query_symlink )
    {
        req->handle = wine_server_obj_handle( handle );
        if (target->MaximumLength >= sizeof(WCHAR))
            wine_server_set_reply( req, target->Buffer, target->MaximumLength - sizeof(WCHAR) );
        if (!(ret = wine_server_call( req )))
        {
            target->Length = wine_server_reply_size(reply);
            target->Buffer[target->Length / sizeof(WCHAR)] = 0;
            if (length) *length = reply->total + sizeof(WCHAR);
        }
        else if (length && ret == STATUS_BUFFER_TOO_SMALL) *length = reply->total + sizeof(WCHAR);
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *		NtMakePermanentObject (NTDLL.@)
 */
NTSTATUS WINAPI NtMakePermanentObject( HANDLE handle )
{
    unsigned int ret;

    TRACE("%p\n", handle);

    SERVER_START_REQ( set_object_permanence )
    {
        req->handle = wine_server_obj_handle( handle );
        req->permanent = 1;
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *		NtMakeTemporaryObject (NTDLL.@)
 */
NTSTATUS WINAPI NtMakeTemporaryObject( HANDLE handle )
{
    unsigned int ret;

    TRACE("%p\n", handle);

    SERVER_START_REQ( set_object_permanence )
    {
        req->handle = wine_server_obj_handle( handle );
        req->permanent = 0;
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *		NtCreateTimer (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateTimer( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                               TIMER_TYPE type )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "access %#x, name %s, type %u\n", access,
           attr ? debugstr_us(attr->ObjectName) : "(null)", type );

    *handle = 0;
    if (type != NotificationTimer && type != SynchronizationTimer) return STATUS_INVALID_PARAMETER;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_timer )
    {
        req->access  = access;
        req->manual  = (type == NotificationTimer);
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return ret;

}


/**************************************************************************
 *		NtOpenTimer (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenTimer( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    TRACE( "access %#x, name %s\n", access, attr ? debugstr_us(attr->ObjectName) : "(null)" );

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_timer )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/**************************************************************************
 *		NtSetTimer (NTDLL.@)
 */
NTSTATUS WINAPI NtSetTimer( HANDLE handle, const LARGE_INTEGER *when, PTIMER_APC_ROUTINE callback,
                            void *arg, BOOLEAN resume, ULONG period, BOOLEAN *state )
{
    unsigned int ret = STATUS_SUCCESS;

    TRACE( "(%p,%p,%p,%p,%08x,0x%08x,%p)\n", handle, when, callback, arg, resume, period, state );

    SERVER_START_REQ( set_timer )
    {
        req->handle   = wine_server_obj_handle( handle );
        req->period   = period;
        req->expire   = when->QuadPart;
        req->callback = wine_server_client_ptr( callback );
        req->arg      = wine_server_client_ptr( arg );
        ret = wine_server_call( req );
        if (state) *state = reply->signaled;
    }
    SERVER_END_REQ;

    /* set error but can still succeed */
    if (resume && ret == STATUS_SUCCESS) return STATUS_TIMER_RESUME_IGNORED;
    return ret;
}


/**************************************************************************
 *		NtCancelTimer (NTDLL.@)
 */
NTSTATUS WINAPI NtCancelTimer( HANDLE handle, BOOLEAN *state )
{
    unsigned int ret;

    TRACE( "handle %p, state %p\n", handle, state );

    SERVER_START_REQ( cancel_timer )
    {
        req->handle = wine_server_obj_handle( handle );
        ret = wine_server_call( req );
        if (state) *state = reply->signaled;
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************************
 *		NtQueryTimer (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryTimer( HANDLE handle, TIMER_INFORMATION_CLASS class,
                              void *info, ULONG len, ULONG *ret_len )
{
    TIMER_BASIC_INFORMATION *basic_info = info;
    unsigned int ret;
    LARGE_INTEGER now;

    TRACE( "(%p,%d,%p,0x%08x,%p)\n", handle, class, info, len, ret_len );

    switch (class)
    {
    case TimerBasicInformation:
        if (len < sizeof(TIMER_BASIC_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;

        SERVER_START_REQ( get_timer_info )
        {
            req->handle = wine_server_obj_handle( handle );
            ret = wine_server_call(req);
            /* convert server time to absolute NTDLL time */
            basic_info->RemainingTime.QuadPart = reply->when;
            basic_info->TimerState = reply->signaled;
        }
        SERVER_END_REQ;

        /* convert into relative time */
        if (basic_info->RemainingTime.QuadPart > 0) NtQuerySystemTime( &now );
        else
        {
            NtQueryPerformanceCounter( &now, NULL );
            basic_info->RemainingTime.QuadPart = -basic_info->RemainingTime.QuadPart;
        }

        if (now.QuadPart > basic_info->RemainingTime.QuadPart)
            basic_info->RemainingTime.QuadPart = 0;
        else
            basic_info->RemainingTime.QuadPart -= now.QuadPart;

        if (ret_len) *ret_len = sizeof(TIMER_BASIC_INFORMATION);
        return ret;
    }

    FIXME( "Unhandled class %d\n", class );
    return STATUS_INVALID_INFO_CLASS;
}


/******************************************************************
 *		NtWaitForMultipleObjects (NTDLL.@)
 */
NTSTATUS WINAPI NtWaitForMultipleObjects( DWORD count, const HANDLE *handles, WAIT_TYPE type,
                                          BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    HANDLE real_handles[MAXIMUM_WAIT_OBJECTS];
    union select_op select_op;
    UINT i, flags = SELECT_INTERRUPTIBLE;
    unsigned int ret;

    if (!count || count > MAXIMUM_WAIT_OBJECTS) return STATUS_INVALID_PARAMETER_1;
    if (type != WaitAll && type != WaitAny) FIXME( "Unsupported wait type %u\n", type );

    if (TRACE_ON(sync))
    {
        TRACE( "type %u, alertable %u, handles {%p", type, alertable, handles[0] );
        for (i = 1; i < count; i++) TRACE( ", %p", handles[i] );
        TRACE( "}, timeout %s\n", debugstr_timeout(timeout) );
    }

    /* Reject pseudo-handles up front. These are not valid for multi-object waits. */
    for (i = 0; i < count; i++)
    {
        if (is_pseudo_handle( handles[i] )) return STATUS_INVALID_HANDLE;
    }

    if (wine_uring_async_inflight())
    {
        wine_uring_drain_nonblocking();
        if (!timeout || timeout->QuadPart != 0)
        {
            for (i = 0; i < count; i++)
            {
                if (wine_uring_has_pending_event( handles[i] ))
                {
                    wine_uring_drain_for_event( handles[i], timeout );
                    if (type == WaitAny) break;
                }
            }
        }
    }

#ifdef __linux__
    if (count == 1 && !alertable && is_local_sync_handle( handles[0] ))
    {
        ret = local_sync_wait( handles[0], timeout );
        TRACE( "-> %#x\n", ret );
        return ret;
    }
    {
        BOOL has_local = FALSE;
        for (i = 0; i < count; i++)
        {
            if (is_local_sync_handle( handles[i] ))
            {
                if (!has_local)
                {
                    memcpy( real_handles, handles, count * sizeof(HANDLE) );
                    has_local = TRUE;
                }
                if ((ret = promote_local_sync_handle( handles[i], &real_handles[i] )))
                {
                    TRACE( "-> %#x\n", ret );
                    return ret;
                }
            }
        }
        if (has_local) handles = real_handles;
    }
#endif

    if ((ret = inproc_wait( count, handles, type, alertable, timeout )) != STATUS_NOT_IMPLEMENTED)
    {
        TRACE( "-> %#x\n", ret );
        return ret;
    }

    if (alertable) flags |= SELECT_ALERTABLE;
    select_op.wait.op = type == WaitAll ? SELECT_WAIT_ALL : SELECT_WAIT;
    for (i = 0; i < count; i++) select_op.wait.handles[i] = wine_server_obj_handle( handles[i] );
    ret = server_wait( &select_op, offsetof( union select_op, wait.handles[count] ), flags, timeout );
    if (ret == STATUS_OBJECT_TYPE_MISMATCH && count == 1) cache_nonwaitable_handle( handles[0] );
    TRACE( "-> %#x\n", ret );
    return ret;
}


/******************************************************************
 *		NtWaitForSingleObject (NTDLL.@)
 */
NTSTATUS WINAPI NtWaitForSingleObject( HANDLE handle, BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    union select_op select_op;
    UINT flags = SELECT_INTERRUPTIBLE;
    unsigned int ret;

    TRACE( "handle %p, alertable %u, timeout %s\n", handle, alertable, debugstr_timeout(timeout) );

    if (wine_uring_async_inflight())
    {
        wine_uring_drain_nonblocking();
        if (wine_uring_has_pending_event( handle ) && (!timeout || timeout->QuadPart != 0))
            wine_uring_drain_for_event( handle, timeout );
    }

#ifdef __linux__
    if (is_local_sync_handle( handle ))
    {
        if (!alertable)
        {
            ret = local_sync_wait( handle, timeout );
            TRACE( "-> %#x\n", ret );
            return ret;
        }
        else
        {
            HANDLE real;
            if ((ret = promote_local_sync_handle( handle, &real ))) return ret;
            handle = real;
        }
    }
#endif

    if ((ret = inproc_wait( 1, &handle, WaitAny, alertable, timeout )) != STATUS_NOT_IMPLEMENTED)
    {
        TRACE( "-> %#x\n", ret );
        return ret;
    }

    if (alertable) flags |= SELECT_ALERTABLE;
    select_op.wait.op = SELECT_WAIT;
    select_op.wait.handles[0] = wine_server_obj_handle( handle );
    ret = server_wait( &select_op, offsetof( union select_op, wait.handles[1] ), flags, timeout );
    if (ret == STATUS_OBJECT_TYPE_MISMATCH) cache_nonwaitable_handle( handle );
    TRACE( "-> %#x\n", ret );
    return ret;
}


/******************************************************************
 *		NtSignalAndWaitForSingleObject (NTDLL.@)
 */
NTSTATUS WINAPI NtSignalAndWaitForSingleObject( HANDLE signal, HANDLE wait,
                                                BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    union select_op select_op;
    UINT flags = SELECT_INTERRUPTIBLE;
    NTSTATUS ret;

    TRACE( "signal %p, wait %p, alertable %u, timeout %s\n", signal, wait, alertable, debugstr_timeout(timeout) );

    if (!signal) return STATUS_INVALID_HANDLE;

#ifdef __linux__
    if (is_local_sync_handle( signal ))
    {
        HANDLE real;
        if ((ret = promote_local_sync_handle( signal, &real ))) return ret;
        signal = real;
    }
    if (is_local_sync_handle( wait ))
    {
        HANDLE real;
        if ((ret = promote_local_sync_handle( wait, &real ))) return ret;
        wait = real;
    }
#endif

    if ((ret = inproc_signal_and_wait( signal, wait, alertable, timeout )) != STATUS_NOT_IMPLEMENTED)
        return ret;

    if (alertable) flags |= SELECT_ALERTABLE;
    select_op.signal_and_wait.op = SELECT_SIGNAL_AND_WAIT;
    select_op.signal_and_wait.wait = wine_server_obj_handle( wait );
    select_op.signal_and_wait.signal = wine_server_obj_handle( signal );
    return server_wait( &select_op, sizeof(select_op.signal_and_wait), flags, timeout );
}


/******************************************************************
 *		NtYieldExecution (NTDLL.@)
 */
NTSTATUS WINAPI NtYieldExecution(void)
{
#ifdef HAVE_SCHED_YIELD
    sched_yield();
    return STATUS_SUCCESS;
#else
    return STATUS_NO_YIELD_PERFORMED;
#endif
}


/******************************************************************
 *		NtDelayExecution (NTDLL.@)
 */
NTSTATUS WINAPI NtDelayExecution( BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    unsigned int status = STATUS_SUCCESS;

    /* if alertable, we need to query the server */
    if (alertable)
    {
        /* Since server_wait will result in an unconditional implicit yield,
           we never return STATUS_NO_YIELD_PERFORMED */
        if ((status = server_wait( NULL, 0, SELECT_INTERRUPTIBLE | SELECT_ALERTABLE, timeout )) == STATUS_TIMEOUT)
            status = STATUS_SUCCESS;
        return status;
    }

    if (!timeout || timeout->QuadPart == TIMEOUT_INFINITE)  /* sleep forever */
    {
        for (;;) select( 0, NULL, NULL, NULL, NULL );
    }
    else
    {
        LARGE_INTEGER now;
        timeout_t when, diff;

        if ((when = timeout->QuadPart) < 0)
        {
            NtQuerySystemTime( &now );
            when = now.QuadPart - when;
        }

        /* Note that we yield after establishing the desired timeout, but
           we only care about the result of the yield for zero timeouts */
        status = NtYieldExecution();
        if (!when) return status;

        for (;;)
        {
            struct timeval tv;
            NtQuerySystemTime( &now );
            diff = (when - now.QuadPart + 9) / 10;
            if (diff <= 0) break;
            tv.tv_sec  = diff / 1000000;
            tv.tv_usec = diff % 1000000;
            if (select( 0, NULL, NULL, NULL, &tv ) != -1) break;
        }
    }
    return STATUS_SUCCESS;
}


/******************************************************************************
 *              NtQueryPerformanceCounter (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryPerformanceCounter( LARGE_INTEGER *counter, LARGE_INTEGER *frequency )
{
#if defined(__x86_64__) || defined(__i386__)
    if (user_shared_data && (user_shared_data->QpcBypassEnabled & SHARED_GLOBAL_FLAGS_QPC_BYPASS_ENABLED))
    {
        ULONGLONG tsc;
        if (user_shared_data->QpcBypassEnabled & SHARED_GLOBAL_FLAGS_QPC_BYPASS_USE_RDTSCP)
        {
            unsigned int aux;
            tsc = __rdtscp( &aux );
        }
        else
        {
            if (user_shared_data->QpcBypassEnabled & SHARED_GLOBAL_FLAGS_QPC_BYPASS_USE_LFENCE)
                _mm_lfence();
            tsc = __rdtsc();
        }
        counter->QuadPart = (tsc + user_shared_data->QpcBias) >> user_shared_data->QpcShift;
        if (frequency) frequency->QuadPart = user_shared_data->QpcFrequency;
        return STATUS_SUCCESS;
    }
#elif defined(__aarch64__)
    if (user_shared_data && (user_shared_data->QpcBypassEnabled & SHARED_GLOBAL_FLAGS_QPC_BYPASS_ENABLED))
    {
        UINT64 cnt;
        __asm__ volatile( "mrs %[Res], CNTVCT_EL0" : [Res] "=r" (cnt) );
        counter->QuadPart = (cnt + user_shared_data->QpcBias) >> user_shared_data->QpcShift;
        if (frequency) frequency->QuadPart = user_shared_data->QpcFrequency;
        return STATUS_SUCCESS;
    }
#endif
    counter->QuadPart = monotonic_counter();
    if (frequency) frequency->QuadPart = TICKSPERSEC;
    return STATUS_SUCCESS;
}


static inline void update_user_shared_data_time(void)
{
    LARGE_INTEGER system_time, interrupt_time;
    ULONGLONG lock;
    LONGLONG now_mono, elapsed, current_time;
    timeout_t tick_count;

    if (!user_shared_data_rw) return;

    now_mono = monotonic_counter();

    lock = __atomic_load_n( &user_shared_data_rw->TimeUpdateLock, __ATOMIC_RELAXED );
    if (lock & 1) return;

    if (!__atomic_compare_exchange_n( &user_shared_data_rw->TimeUpdateLock, &lock, lock + 1,
                                      FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED ))
        return;

    do {
        system_time.HighPart = user_shared_data->SystemTime.High1Time;
        system_time.LowPart  = user_shared_data->SystemTime.LowPart;
    } while (system_time.HighPart != user_shared_data->SystemTime.High2Time);

    do {
        interrupt_time.HighPart = user_shared_data->InterruptTime.High1Time;
        interrupt_time.LowPart  = user_shared_data->InterruptTime.LowPart;
    } while (interrupt_time.HighPart != user_shared_data->InterruptTime.High2Time);

    elapsed = now_mono - interrupt_time.QuadPart;
    if (elapsed > 0)
    {
        current_time = system_time.QuadPart + elapsed;
        tick_count = (timeout_t)now_mono / 10000;

        user_shared_data_rw->SystemTime.High2Time = current_time >> 32;
        user_shared_data_rw->SystemTime.LowPart   = current_time;
        __atomic_store_n( (volatile LONG *)&user_shared_data_rw->SystemTime.High1Time, current_time >> 32, __ATOMIC_RELEASE );

        user_shared_data_rw->InterruptTime.High2Time = now_mono >> 32;
        user_shared_data_rw->InterruptTime.LowPart   = now_mono;
        __atomic_store_n( (volatile LONG *)&user_shared_data_rw->InterruptTime.High1Time, now_mono >> 32, __ATOMIC_RELEASE );

        user_shared_data_rw->TickCount.High2Time = tick_count >> 32;
#if defined(__x86_64__) || defined(__aarch64__)
        __atomic_store_n( (volatile ULONG64 *)&user_shared_data_rw->TickCountQuad, tick_count, __ATOMIC_RELEASE );
#else
        user_shared_data_rw->TickCount.LowPart   = tick_count;
        __atomic_store_n( (volatile LONG *)&user_shared_data_rw->TickCount.High1Time, tick_count >> 32, __ATOMIC_RELEASE );
#endif
        user_shared_data_rw->TickCountLowDeprecated = tick_count;
    }

    __atomic_store_n( &user_shared_data_rw->TimeUpdateLock, lock + 2, __ATOMIC_RELEASE );
}


/***********************************************************************
 *              NtQuerySystemTime (NTDLL.@)
 */
NTSTATUS WINAPI NtQuerySystemTime( LARGE_INTEGER *time )
{
#ifdef HAVE_CLOCK_GETTIME
    struct timespec ts;
    static clockid_t clock_id = CLOCK_MONOTONIC; /* placeholder */
#endif

    if (user_shared_data)
    {
        LARGE_INTEGER system_time;
        LONGLONG elapsed = 0;

        update_user_shared_data_time();

        do {
            system_time.HighPart = user_shared_data->SystemTime.High1Time;
            system_time.LowPart  = user_shared_data->SystemTime.LowPart;
        } while (system_time.HighPart != user_shared_data->SystemTime.High2Time);

        if (!user_shared_data_rw)
        {
            LARGE_INTEGER interrupt_time;
            do {
                interrupt_time.HighPart = user_shared_data->InterruptTime.High1Time;
                interrupt_time.LowPart  = user_shared_data->InterruptTime.LowPart;
            } while (interrupt_time.HighPart != user_shared_data->InterruptTime.High2Time);

            elapsed = (LONGLONG)monotonic_counter() - interrupt_time.QuadPart;
            if (elapsed < 0) elapsed = 0;
        }

        time->QuadPart = system_time.QuadPart + elapsed;
        return STATUS_SUCCESS;
    }

#ifdef HAVE_CLOCK_GETTIME
    if (clock_id == CLOCK_MONOTONIC)
    {
#ifdef CLOCK_REALTIME_COARSE
        struct timespec res;

        /* Use CLOCK_REALTIME_COARSE if it has 1 ms or better resolution */
        if (!clock_getres( CLOCK_REALTIME_COARSE, &res ) && res.tv_sec == 0 && res.tv_nsec <= 1000000)
            clock_id = CLOCK_REALTIME_COARSE;
        else
#endif /* CLOCK_REALTIME_COARSE */
            clock_id = CLOCK_REALTIME;
    }

    if (!clock_gettime( clock_id, &ts ))
    {
        time->QuadPart = ticks_from_time_t( ts.tv_sec ) + (ts.tv_nsec + 50) / 100;
    }
    else
#endif /* HAVE_CLOCK_GETTIME */
    {
        struct timeval now;

        gettimeofday( &now, 0 );
        time->QuadPart = ticks_from_time_t( now.tv_sec ) + now.tv_usec * 10;
    }
    return STATUS_SUCCESS;
}


/***********************************************************************
 *              NtSetSystemTime (NTDLL.@)
 */
NTSTATUS WINAPI NtSetSystemTime( const LARGE_INTEGER *new, LARGE_INTEGER *old )
{
    LARGE_INTEGER now;
    LONGLONG diff;

    NtQuerySystemTime( &now );
    if (old) *old = now;
    diff = new->QuadPart - now.QuadPart;
    if (diff > -TICKSPERSEC / 2 && diff < TICKSPERSEC / 2) return STATUS_SUCCESS;
    ERR( "not allowed: difference %d ms\n", (int)(diff / 10000) );
    return STATUS_PRIVILEGE_NOT_HELD;
}


/***********************************************************************
 *              NtQueryTimerResolution (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryTimerResolution( ULONG *min_res, ULONG *max_res, ULONG *current_res )
{
    TRACE( "(%p,%p,%p)\n", min_res, max_res, current_res );
    *max_res = *current_res = 10000; /* See NtSetTimerResolution() */
    *min_res = 156250;
    return STATUS_SUCCESS;
}


/***********************************************************************
 *              NtSetTimerResolution (NTDLL.@)
 */
NTSTATUS WINAPI NtSetTimerResolution( ULONG res, BOOLEAN set, ULONG *current_res )
{
    static BOOL has_request = FALSE;

    TRACE( "(%u,%u,%p), semi-stub!\n", res, set, current_res );

    /* Wine has no support for anything other that 1 ms and does not keep of
     * track resolution requests anyway.
     * Fortunately NtSetTimerResolution() should ignore requests to lower the
     * timer resolution. So by claiming that 'some other process' requested the
     * max resolution already, there no need to actually change it.
     */
    *current_res = 10000;

    /* Just keep track of whether this process requested a specific timer
     * resolution.
     */
    if (!has_request && !set)
        return STATUS_TIMER_RESOLUTION_NOT_SET;
    has_request = set;

    return STATUS_SUCCESS;
}


/******************************************************************************
 *              NtSetIntervalProfile (NTDLL.@)
 */
NTSTATUS WINAPI NtSetIntervalProfile( ULONG interval, KPROFILE_SOURCE source )
{
    FIXME( "%u,%d\n", interval, source );
    return STATUS_SUCCESS;
}


/******************************************************************************
 *              NtGetTickCount (NTDLL.@)
 */
ULONG WINAPI NtGetTickCount(void)
{
    /* note: we ignore TickCountMultiplier */
    if (!user_shared_data) return monotonic_counter() / 10000;
    return user_shared_data->TickCount.LowPart;
}


/******************************************************************************
 *              RtlGetSystemTimePrecise (NTDLL.@)
 */
NTSTATUS system_time_precise( void *args )
{
    LONGLONG *ret = args;
    struct timeval now;
#ifdef HAVE_CLOCK_GETTIME
    struct timespec ts;

    if (!clock_gettime( CLOCK_REALTIME, &ts ))
    {
        *ret = ticks_from_time_t( ts.tv_sec ) + (ts.tv_nsec + 50) / 100;
        return STATUS_SUCCESS;
    }
#endif
    gettimeofday( &now, 0 );
    *ret = ticks_from_time_t( now.tv_sec ) + now.tv_usec * 10;
    return STATUS_SUCCESS;
}


/******************************************************************************
 *              NtCreateKeyedEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateKeyedEvent( HANDLE *handle, ACCESS_MASK access,
                                    const OBJECT_ATTRIBUTES *attr, ULONG flags )
{
    unsigned int ret;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "access %#x, name %s, flags %#x\n", access,
           attr ? debugstr_us(attr->ObjectName) : "(null)", flags );

    *handle = 0;
    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_keyed_event )
    {
        req->access = access;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return ret;
}


/******************************************************************************
 *              NtOpenKeyedEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenKeyedEvent( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    TRACE( "access %#x, name %s\n", access, attr ? debugstr_us(attr->ObjectName) : "(null)" );

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_keyed_event )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}

/******************************************************************************
 *              NtWaitForKeyedEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtWaitForKeyedEvent( HANDLE handle, const void *key,
                                     BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    union select_op select_op;
    UINT flags = SELECT_INTERRUPTIBLE;

    TRACE( "handle %p, key %p, alertable %u, timeout %s\n", handle, key, alertable, debugstr_timeout(timeout) );

    if (!handle) handle = keyed_event;
    if ((ULONG_PTR)key & 1) return STATUS_INVALID_PARAMETER_1;
    if (alertable) flags |= SELECT_ALERTABLE;
    select_op.keyed_event.op     = SELECT_KEYED_EVENT_WAIT;
    select_op.keyed_event.handle = wine_server_obj_handle( handle );
    select_op.keyed_event.key    = wine_server_client_ptr( key );
    return server_wait( &select_op, sizeof(select_op.keyed_event), flags, timeout );
}


/******************************************************************************
 *              NtReleaseKeyedEvent (NTDLL.@)
 */
NTSTATUS WINAPI NtReleaseKeyedEvent( HANDLE handle, const void *key,
                                     BOOLEAN alertable, const LARGE_INTEGER *timeout )
{
    union select_op select_op;
    UINT flags = SELECT_INTERRUPTIBLE;

    TRACE( "handle %p, key %p, alertable %u, timeout %s\n", handle, key, alertable, debugstr_timeout(timeout) );

    if (!handle) handle = keyed_event;
    if ((ULONG_PTR)key & 1) return STATUS_INVALID_PARAMETER_1;
    if (alertable) flags |= SELECT_ALERTABLE;
    select_op.keyed_event.op     = SELECT_KEYED_EVENT_RELEASE;
    select_op.keyed_event.handle = wine_server_obj_handle( handle );
    select_op.keyed_event.key    = wine_server_client_ptr( key );
    return server_wait( &select_op, sizeof(select_op.keyed_event), flags, timeout );
}


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
static int iocp_shm_disabled = 0;
static pthread_mutex_t iocp_shm_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct iocp_shm *get_iocp_shm(void)
{
    struct iocp_shm *shm = __atomic_load_n( &iocp_shm, __ATOMIC_ACQUIRE );
    struct stat st;
    char path[1024];
    int fd;
    struct iocp_shm *ptr;

    if (shm) return shm;
    if (__atomic_load_n( &iocp_shm_disabled, __ATOMIC_RELAXED )) return NULL;

    pthread_mutex_lock( &iocp_shm_mutex );
    shm = iocp_shm;
    if (!shm && !iocp_shm_disabled)
    {
        if (!config_dir || stat( config_dir, &st ) == -1)
        {
            iocp_shm_disabled = 1;
            pthread_mutex_unlock( &iocp_shm_mutex );
            return NULL;
        }
#ifdef __ANDROID__
        snprintf( path, sizeof(path), "%s/.wineserver/server-%llx-%llx/iocp_shm",
                  config_dir, (unsigned long long)st.st_dev, (unsigned long long)st.st_ino );
#else
        snprintf( path, sizeof(path), "/tmp/.wine-%u/server-%llx-%llx/iocp_shm",
                  getuid(), (unsigned long long)st.st_dev, (unsigned long long)st.st_ino );
#endif
        fd = open( path, O_RDWR | O_CLOEXEC );
        if (fd != -1)
        {
            if (fstat( fd, &st ) != -1 && st.st_size >= (off_t)sizeof(struct iocp_shm))
            {
                ptr = mmap( NULL, sizeof(struct iocp_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
                if (ptr != MAP_FAILED)
                {
                    if (__atomic_load_n( &ptr->magic, __ATOMIC_ACQUIRE ) == IOCP_SHM_MAGIC &&
                        ptr->version == IOCP_SHM_VERSION)
                    {
                        __atomic_store_n( &iocp_shm, ptr, __ATOMIC_RELEASE );
                        shm = ptr;
                    }
                    else
                    {
                        munmap( ptr, sizeof(struct iocp_shm) );
                        iocp_shm_disabled = 1;
                    }
                }
                else iocp_shm_disabled = 1;
            }
            close( fd );
        }
        else if (errno != ENOENT)
        {
            iocp_shm_disabled = 1;
        }
    }
    pthread_mutex_unlock( &iocp_shm_mutex );
    return shm;
}

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

static inline struct iocp_shm_port *iocp_shm_lookup_port( struct iocp_shm *shm, HANDLE handle,
                                                          uint64_t *gen_out, uint32_t *access_out )
{
    uint32_t hval = (uint32_t)wine_server_obj_handle( handle );
    uint32_t base = (iocp_shm_hash_handle( pid, hval ) % IOCP_SHM_HANDLE_BUCKETS) * IOCP_SHM_HANDLE_WAYS;
    uint32_t way;

    for (way = 0; way < IOCP_SHM_HANDLE_WAYS; way++)
    {
        struct iocp_shm_handle *h = &shm->handles[base + way];
        int tries;
        for (tries = 0; tries < 64; tries++)
        {
            uint32_t s1 = __atomic_load_n( &h->seq, __ATOMIC_ACQUIRE );
            uint32_t h_pid, h_handle, p_idx, h_acc;
            uint64_t p_gen;
            uint32_t s2;

            if (s1 & 1) continue;
            h_pid    = h->pid;
            h_handle = h->handle;
            p_idx    = h->port_idx;
            p_gen    = h->port_gen;
            h_acc    = h->access;
            __atomic_thread_fence( __ATOMIC_ACQUIRE );
            s2 = __atomic_load_n( &h->seq, __ATOMIC_ACQUIRE );
            if (s1 != s2) continue;
            if (p_idx < IOCP_SHM_MAX_PORTS && h_pid == pid && h_handle == hval)
            {
                *gen_out = p_gen;
                *access_out = h_acc;
                return &shm->ports[p_idx];
            }
            break;
        }
    }
    return NULL;
}

/***********************************************************************
 *             NtCreateIoCompletion (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateIoCompletion( HANDLE *handle, ACCESS_MASK access, OBJECT_ATTRIBUTES *attr,
                                      ULONG threads )
{
    unsigned int status;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "(%p, %x, %p, %d)\n", handle, access, attr, threads );

    *handle = 0;
    if ((status = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return status;

    SERVER_START_REQ( create_completion )
    {
        req->access     = access;
        req->concurrent = threads;
        wine_server_add_data( req, objattr, len );
        status = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return status;
}


/***********************************************************************
 *             NtOpenIoCompletion (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenIoCompletion( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int status;

    *handle = 0;
    if ((status = validate_open_object_attributes( attr ))) return status;

    SERVER_START_REQ( open_completion )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        status = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return status;
}


BOOL iocp_shm_try_push( HANDLE handle, ULONG_PTR key, ULONG_PTR value,
                        NTSTATUS status, ULONG_PTR count )
{
    struct iocp_shm *shm = get_iocp_shm();
    struct iocp_shm_port *port;
    uint64_t gen;
    uint32_t access;

    if (shm && (port = iocp_shm_lookup_port( shm, handle, &gen, &access )) &&
        (access & IO_COMPLETION_MODIFY_STATE))
    {
        if (iocp_shm_spin_lock( &port->lock ))
        {
            if (__atomic_load_n( &port->allocated, __ATOMIC_ACQUIRE ) &&
                port->gen == gen && !port->ring_disabled &&
                __atomic_load_n( &port->waiting_threads, __ATOMIC_ACQUIRE ) == 0 &&
                __atomic_load_n( &port->has_waiters, __ATOMIC_ACQUIRE ) == 0 &&
                port->ring_count < IOCP_SHM_RING_SLOTS)
            {
                struct iocp_shm_msg *msg = &port->ring[port->tail % IOCP_SHM_RING_SLOTS];
                msg->ckey   = key;
                msg->cvalue = value;
                msg->info   = count;
                msg->status = (uint32_t)status;
                msg->pad    = 0;
                port->tail  = (port->tail + 1) % IOCP_SHM_RING_SLOTS;
                __atomic_store_n( &port->ring_count, port->ring_count + 1, __ATOMIC_RELEASE );
                iocp_shm_spin_unlock( &port->lock );
                return TRUE;
            }
            iocp_shm_spin_unlock( &port->lock );
        }
    }
    return FALSE;
}

static NTSTATUS iocp_shm_try_pop( HANDLE handle, ULONG_PTR *key, ULONG_PTR *value,
                                  IO_STATUS_BLOCK *io, LARGE_INTEGER *timeout )
{
    struct iocp_shm *shm = get_iocp_shm();
    struct iocp_shm_port *port;
    uint64_t gen;
    uint32_t access;

    if (shm && (port = iocp_shm_lookup_port( shm, handle, &gen, &access )) &&
        (access & IO_COMPLETION_MODIFY_STATE))
    {
        if (iocp_shm_spin_lock( &port->lock ))
        {
            if (__atomic_load_n( &port->allocated, __ATOMIC_ACQUIRE ) &&
                port->gen == gen && !port->ring_disabled &&
                __atomic_load_n( &port->server_depth, __ATOMIC_ACQUIRE ) == 0)
            {
                if (port->ring_count > 0)
                {
                    struct iocp_shm_msg msg = port->ring[port->head % IOCP_SHM_RING_SLOTS];
                    port->head = (port->head + 1) % IOCP_SHM_RING_SLOTS;
                    __atomic_store_n( &port->ring_count, port->ring_count - 1, __ATOMIC_RELEASE );
                    iocp_shm_spin_unlock( &port->lock );
                    *key            = (ULONG_PTR)msg.ckey;
                    *value          = (ULONG_PTR)msg.cvalue;
                    io->Information = (ULONG_PTR)msg.info;
                    io->Status      = (NTSTATUS)msg.status;
                    return STATUS_SUCCESS;
                }
                if (timeout && timeout->QuadPart == 0)
                {
                    iocp_shm_spin_unlock( &port->lock );
                    return STATUS_TIMEOUT;
                }
            }
            iocp_shm_spin_unlock( &port->lock );
        }
    }
    return STATUS_PENDING;
}

static NTSTATUS iocp_shm_try_pop_ex( HANDLE handle, FILE_IO_COMPLETION_INFORMATION *info, ULONG count,
                                     ULONG *written, LARGE_INTEGER *timeout )
{
    struct iocp_shm *shm = get_iocp_shm();
    struct iocp_shm_port *port;
    uint64_t gen;
    uint32_t access;

    if (shm && (port = iocp_shm_lookup_port( shm, handle, &gen, &access )) &&
        (access & IO_COMPLETION_MODIFY_STATE))
    {
        if (iocp_shm_spin_lock( &port->lock ))
        {
            if (__atomic_load_n( &port->allocated, __ATOMIC_ACQUIRE ) &&
                port->gen == gen && !port->ring_disabled &&
                __atomic_load_n( &port->server_depth, __ATOMIC_ACQUIRE ) == 0)
            {
                if (port->ring_count > 0)
                {
                    ULONG n = 0;
                    while (n < count && port->ring_count > 0)
                    {
                        struct iocp_shm_msg msg = port->ring[port->head % IOCP_SHM_RING_SLOTS];
                        port->head = (port->head + 1) % IOCP_SHM_RING_SLOTS;
                        port->ring_count--;
                        info[n].CompletionKey             = (ULONG_PTR)msg.ckey;
                        info[n].CompletionValue           = (ULONG_PTR)msg.cvalue;
                        info[n].IoStatusBlock.Information = (ULONG_PTR)msg.info;
                        info[n].IoStatusBlock.Status      = (NTSTATUS)msg.status;
                        n++;
                    }
                    __atomic_thread_fence( __ATOMIC_RELEASE );
                    iocp_shm_spin_unlock( &port->lock );
                    *written = n;
                    return STATUS_SUCCESS;
                }
                if (timeout && timeout->QuadPart == 0)
                {
                    iocp_shm_spin_unlock( &port->lock );
                    *written = 1;
                    return STATUS_TIMEOUT;
                }
            }
            iocp_shm_spin_unlock( &port->lock );
        }
    }
    return STATUS_PENDING;
}


/***********************************************************************
 *             NtSetIoCompletion (NTDLL.@)
 */
NTSTATUS WINAPI NtSetIoCompletion( HANDLE handle, ULONG_PTR key, ULONG_PTR value,
                                   NTSTATUS status, SIZE_T count )
{
    unsigned int ret;

    TRACE( "(%p, %lx, %lx, %x, %lx)\n", handle, key, value, status, count );

    if (iocp_shm_try_push( handle, key, value, status, count ))
        return STATUS_SUCCESS;

    SERVER_START_REQ( add_completion )
    {
        req->handle      = wine_server_obj_handle( handle );
        req->ckey        = key;
        req->cvalue      = value;
        req->status      = status;
        req->information = count;
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *             NtSetIoCompletionEx (NTDLL.@)
 *
 * completion_reserve_handle is a handle allocated by NtAllocateReserveObject() for pre-allocating
 * memory for completion objects to deal with low-memory situations. It's not in use for now.
 */
NTSTATUS WINAPI NtSetIoCompletionEx( HANDLE completion_handle, HANDLE completion_reserve_handle,
                                     ULONG_PTR key, ULONG_PTR value, NTSTATUS status, SIZE_T count )
{
    unsigned int ret;

    TRACE( "(%p, %p, %lx, %lx, %x, %lx)\n", completion_handle, completion_reserve_handle,
           key, value, status, count );

    if (!completion_reserve_handle) return STATUS_INVALID_HANDLE;

    SERVER_START_REQ( add_completion )
    {
        req->handle         = wine_server_obj_handle( completion_handle );
        req->ckey           = key;
        req->cvalue         = value;
        req->status         = status;
        req->information    = count;
        req->reserve_handle = wine_server_obj_handle( completion_reserve_handle );
        ret = wine_server_call( req );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *             NtRemoveIoCompletion (NTDLL.@)
 */
NTSTATUS WINAPI NtRemoveIoCompletion( HANDLE handle, ULONG_PTR *key, ULONG_PTR *value,
                                      IO_STATUS_BLOCK *io, LARGE_INTEGER *timeout )
{
    HANDLE wait_handle = NULL;
    unsigned int status;

    TRACE( "(%p, %p, %p, %p, %p)\n", handle, key, value, io, timeout );

    if (wine_uring_async_inflight())
        wine_uring_drain_nonblocking();

    status = iocp_shm_try_pop( handle, key, value, io, timeout );
    if (status != STATUS_PENDING) return status;

    if (wine_uring_has_pending_iocp( handle ) && (!timeout || timeout->QuadPart != 0))
    {
        wine_uring_drain_for_iocp( handle, timeout );
        status = iocp_shm_try_pop( handle, key, value, io, timeout );
        if (status != STATUS_PENDING) return status;
    }

    SERVER_START_REQ( remove_completion )
    {
        req->handle = wine_server_obj_handle( handle );
        req->alertable = 0;
        if (!(status = wine_server_call( req )))
        {
            *key            = reply->ckey;
            *value          = reply->cvalue;
            io->Information = reply->information;
            io->Status      = reply->status;
        }
        else wait_handle = wine_server_ptr_handle( reply->wait_handle );
    }
    SERVER_END_REQ;
    if (status != STATUS_PENDING) return status;
    if (!timeout || timeout->QuadPart) status = server_wait_for_object( wait_handle, FALSE, timeout );
    else                               status = STATUS_TIMEOUT;
    if (status != WAIT_OBJECT_0) return status;

    SERVER_START_REQ( get_thread_completion )
    {
        if (!(status = wine_server_call( req )))
        {
            *key            = reply->ckey;
            *value          = reply->cvalue;
            io->Information = reply->information;
            io->Status      = reply->status;
        }
    }
    SERVER_END_REQ;

    return status;
}


/***********************************************************************
 *             NtRemoveIoCompletionEx (NTDLL.@)
 */
NTSTATUS WINAPI NtRemoveIoCompletionEx( HANDLE handle, FILE_IO_COMPLETION_INFORMATION *info, ULONG count,
                                        ULONG *written, LARGE_INTEGER *timeout, BOOLEAN alertable )
{
    HANDLE wait_handle = NULL;
    unsigned int status;
    ULONG i = 0;

    TRACE( "%p %p %u %p %p %u\n", handle, info, count, written, timeout, alertable );

    if (!count) return STATUS_INVALID_PARAMETER;

    if (wine_uring_async_inflight())
        wine_uring_drain_nonblocking();

    if (!alertable)
    {
        status = iocp_shm_try_pop_ex( handle, info, count, written, timeout );
        if (status != STATUS_PENDING) return status;

        if (wine_uring_has_pending_iocp( handle ) && (!timeout || timeout->QuadPart != 0))
        {
            wine_uring_drain_for_iocp( handle, timeout );
            status = iocp_shm_try_pop_ex( handle, info, count, written, timeout );
            if (status != STATUS_PENDING) return status;
        }
    }

    while (i < count)
    {
        SERVER_START_REQ( remove_completion )
        {
            req->handle = wine_server_obj_handle( handle );
            req->alertable = alertable;
            if (!(status = wine_server_call( req )))
            {
                info[i].CompletionKey             = reply->ckey;
                info[i].CompletionValue           = reply->cvalue;
                info[i].IoStatusBlock.Information = reply->information;
                info[i].IoStatusBlock.Status      = reply->status;
            }
            else wait_handle = wine_server_ptr_handle( reply->wait_handle );
        }
        SERVER_END_REQ;
        if (status != STATUS_SUCCESS) break;
        ++i;
    }
    if (i || (status != STATUS_PENDING && status != STATUS_USER_APC))
    {
        if (i) status = STATUS_SUCCESS;
        goto done;
    }
    if (status == STATUS_USER_APC)
    {
        status = NtDelayExecution( TRUE, NULL );
        assert( status == STATUS_USER_APC );
        goto done;
    }
    if (!timeout || timeout->QuadPart) status = server_wait_for_object( wait_handle, alertable, timeout );
    else                               status = STATUS_TIMEOUT;
    if (status != WAIT_OBJECT_0) goto done;

    SERVER_START_REQ( get_thread_completion )
    {
        if (!(status = wine_server_call( req )))
        {
            info[i].CompletionKey             = reply->ckey;
            info[i].CompletionValue           = reply->cvalue;
            info[i].IoStatusBlock.Information = reply->information;
            info[i].IoStatusBlock.Status      = reply->status;
            ++i;
        }
    }
    SERVER_END_REQ;

done:
    *written = i ? i : 1;
    return status;
}


/***********************************************************************
 *             NtQueryIoCompletion (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryIoCompletion( HANDLE handle, IO_COMPLETION_INFORMATION_CLASS class,
                                     void *buffer, ULONG len, ULONG *ret_len )
{
    unsigned int status;

    TRACE( "(%p, %d, %p, 0x%x, %p)\n", handle, class, buffer, len, ret_len );

    if (!buffer) return STATUS_INVALID_PARAMETER;

    switch (class)
    {
    case IoCompletionBasicInformation:
    {
        ULONG *info = buffer;
        if (ret_len) *ret_len = sizeof(*info);
        if (len == sizeof(*info))
        {
            struct iocp_shm *shm = get_iocp_shm();
            struct iocp_shm_port *port;
            uint64_t gen;
            uint32_t access;

            if (shm && (port = iocp_shm_lookup_port( shm, handle, &gen, &access )) &&
                (access & IO_COMPLETION_QUERY_STATE))
            {
                if (iocp_shm_spin_lock( &port->lock ))
                {
                    if (__atomic_load_n( &port->allocated, __ATOMIC_ACQUIRE ) &&
                        port->gen == gen && !port->ring_disabled)
                    {
                        *info = __atomic_load_n( &port->server_depth, __ATOMIC_ACQUIRE ) + port->ring_count;
                        iocp_shm_spin_unlock( &port->lock );
                        return STATUS_SUCCESS;
                    }
                    iocp_shm_spin_unlock( &port->lock );
                }
            }

            SERVER_START_REQ( query_completion )
            {
                req->handle = wine_server_obj_handle( handle );
                if (!(status = wine_server_call( req ))) *info = reply->depth;
            }
            SERVER_END_REQ;
        }
        else status = STATUS_INFO_LENGTH_MISMATCH;
        break;
    }
    default:
        return STATUS_INVALID_PARAMETER;
    }
    return status;
}


/***********************************************************************
 *             NtCreateWaitCompletionPacket (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateWaitCompletionPacket( HANDLE *handle, ACCESS_MASK access,
                                              OBJECT_ATTRIBUTES *attr )
{
    unsigned int status;
    data_size_t len;
    struct object_attributes *objattr;

    TRACE( "(%p, %x, %p)\n", handle, (int)access, attr );

    if (!handle) return STATUS_ACCESS_VIOLATION;
    *handle = 0;
    if ((status = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return status;

    SERVER_START_REQ( create_wait_completion_packet )
    {
        req->access = access;
        wine_server_add_data( req, objattr, len );
        if (!(status = wine_server_call( req )))
            *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return status;
}


/***********************************************************************
 *             NtAssociateWaitCompletionPacket (NTDLL.@)
 */
NTSTATUS WINAPI NtAssociateWaitCompletionPacket( HANDLE handle, HANDLE completion_handle,
                                                 HANDLE target_handle, void *key_context,
                                                 void *apc_context, NTSTATUS io_status,
                                                 ULONG_PTR io_status_info, BOOLEAN wake_up_app )
{
    unsigned int status;
    struct inproc_sync stack, *sync;

    TRACE( "(%p, %p, %p, %p, %p, %x, %lx, %d)\n", handle, completion_handle, target_handle,
           key_context, apc_context, (int)io_status, (unsigned long)io_status_info, wake_up_app );

#ifdef __linux__
    if (is_local_sync_handle( target_handle ))
    {
        HANDLE real;
        if ((status = promote_local_sync_handle( target_handle, &real ))) return status;
        target_handle = real;
    }
#endif

    if (!get_inproc_sync( target_handle, INPROC_SYNC_UNKNOWN, 0, &stack, &sync ))
    {
        sync->has_wait_packet = 1;
        release_inproc_sync( sync );
    }

    SERVER_START_REQ( associate_wait_completion_packet )
    {
        req->handle       = wine_server_obj_handle( handle );
        req->completion   = wine_server_obj_handle( completion_handle );
        req->target       = wine_server_obj_handle( target_handle );
        req->status       = io_status;
        req->wake_up_app  = wake_up_app;
        req->key          = (apc_param_t)(ULONG_PTR)key_context;
        req->apc          = (apc_param_t)(ULONG_PTR)apc_context;
        req->information  = io_status_info;
        status = wine_server_call( req );
    }
    SERVER_END_REQ;

    return status;
}


/***********************************************************************
 *             NtCancelWaitCompletionPacket (NTDLL.@)
 */
NTSTATUS WINAPI NtCancelWaitCompletionPacket( HANDLE handle, BOOLEAN remove_signaled_packet )
{
    unsigned int status;
    int cancelled = 0;

    TRACE( "(%p, %d)\n", handle, remove_signaled_packet );

    SERVER_START_REQ( cancel_wait_completion_packet )
    {
        req->handle          = wine_server_obj_handle( handle );
        req->remove_signaled = remove_signaled_packet;
        status = wine_server_call( req );
        cancelled = reply->cancelled;
    }
    SERVER_END_REQ;

    if (status == STATUS_SUCCESS && !cancelled)
        return STATUS_CANCELLED;

    return status;
}


/***********************************************************************
 *             NtCreateSection (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateSection( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                                 const LARGE_INTEGER *size, ULONG protect,
                                 ULONG sec_flags, HANDLE file )
{
    unsigned int ret;
    unsigned int file_access;
    data_size_t len;
    struct object_attributes *objattr;

    *handle = 0;

    switch (protect & 0xff)
    {
    case PAGE_READONLY:
    case PAGE_EXECUTE_READ:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_WRITECOPY:
        file_access = FILE_READ_DATA;
        break;
    case PAGE_READWRITE:
    case PAGE_EXECUTE_READWRITE:
        if (sec_flags & SEC_IMAGE) file_access = FILE_READ_DATA;
        else file_access = FILE_READ_DATA | FILE_WRITE_DATA;
        break;
    case PAGE_EXECUTE:
    case PAGE_NOACCESS:
        file_access = 0;
        break;
    default:
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    if ((ret = wine_server_alloc_object_attributes( attr, &objattr, &len ))) return ret;

    SERVER_START_REQ( create_mapping )
    {
        req->access      = access;
        req->flags       = sec_flags;
        req->file_handle = wine_server_obj_handle( file );
        req->file_access = file_access;
        req->size        = size ? size->QuadPart : 0;
        wine_server_add_data( req, objattr, len );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    free( objattr );
    return ret;
}


/***********************************************************************
 *             NtCreateSectionEx (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateSectionEx( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr,
                                   const LARGE_INTEGER *size, ULONG protect, ULONG sec_flags,
                                   HANDLE file, MEM_EXTENDED_PARAMETER *parameters, ULONG count )
{
    if (count) FIXME( "extended params not supported\n" );
    return NtCreateSection( handle, access, attr, size, protect, sec_flags, file );
}


/***********************************************************************
 *             NtOpenSection (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenSection( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr )
{
    unsigned int ret;

    *handle = 0;
    if ((ret = validate_open_object_attributes( attr ))) return ret;

    SERVER_START_REQ( open_mapping )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        if (attr->ObjectName)
            wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        ret = wine_server_call( req );
        *handle = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}


/***********************************************************************
 *             NtCreatePort (NTDLL.@)
 */
NTSTATUS WINAPI NtCreatePort( HANDLE *handle, OBJECT_ATTRIBUTES *attr, ULONG info_len,
                              ULONG data_len, ULONG *reserved )
{
    FIXME( "(%p,%p,%u,%u,%p),stub!\n", handle, attr, info_len, data_len, reserved );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtConnectPort (NTDLL.@)
 */
NTSTATUS WINAPI NtConnectPort( HANDLE *handle, UNICODE_STRING *name, SECURITY_QUALITY_OF_SERVICE *qos,
                               LPC_SECTION_WRITE *write, LPC_SECTION_READ *read, ULONG *max_len,
                               void *info, ULONG *info_len )
{
    FIXME( "(%p,%s,%p,%p,%p,%p,%p,%p),stub!\n", handle, debugstr_us(name), qos,
           write, read, max_len, info, info_len );
    if (info && info_len) TRACE("msg = %s\n", debugstr_an( info, *info_len ));
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtSecureConnectPort (NTDLL.@)
 */
NTSTATUS WINAPI NtSecureConnectPort( HANDLE *handle, UNICODE_STRING *name, SECURITY_QUALITY_OF_SERVICE *qos,
                                     LPC_SECTION_WRITE *write, PSID sid, LPC_SECTION_READ *read,
                                     ULONG *max_len, void *info, ULONG *info_len )
{
    FIXME( "(%p,%s,%p,%p,%p,%p,%p,%p,%p),stub!\n", handle, debugstr_us(name), qos,
           write, sid, read, max_len, info, info_len );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtListenPort (NTDLL.@)
 */
NTSTATUS WINAPI NtListenPort( HANDLE handle, LPC_MESSAGE *msg )
{
    FIXME("(%p,%p),stub!\n", handle, msg );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtAcceptConnectPort (NTDLL.@)
 */
NTSTATUS WINAPI NtAcceptConnectPort( HANDLE *handle, ULONG id, LPC_MESSAGE *msg, BOOLEAN accept,
                                     LPC_SECTION_WRITE *write, LPC_SECTION_READ *read )
{
    FIXME("(%p,%u,%p,%d,%p,%p),stub!\n", handle, id, msg, accept, write, read );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtCompleteConnectPort (NTDLL.@)
 */
NTSTATUS WINAPI NtCompleteConnectPort( HANDLE handle )
{
    FIXME( "(%p),stub!\n", handle );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtImpersonateClientOfPort (NTDLL.@)
 */
NTSTATUS WINAPI NtImpersonateClientOfPort( HANDLE handle, LPC_MESSAGE *request )
{
    FIXME( "(%p,%p),stub!\n", handle, request );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtReadRequestData (NTDLL.@)
 */
NTSTATUS WINAPI NtReadRequestData( HANDLE handle, LPC_MESSAGE *request, ULONG id,
                                   void *buffer, ULONG len, ULONG *retlen )
{
    FIXME( "(%p,%p,%u,%p,%u,%p),stub!\n", handle, request, id, buffer, len, retlen );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtRegisterThreadTerminatePort (NTDLL.@)
 */
NTSTATUS WINAPI NtRegisterThreadTerminatePort( HANDLE handle )
{
    FIXME( "(%p),stub!\n", handle );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtRequestWaitReplyPort (NTDLL.@)
 */
NTSTATUS WINAPI NtRequestWaitReplyPort( HANDLE handle, LPC_MESSAGE *msg_in, LPC_MESSAGE *msg_out )
{
    FIXME( "(%p,%p,%p),stub!\n", handle, msg_in, msg_out );
    if (msg_in)
        TRACE("datasize %u msgsize %u type %u ranges %u client %p/%p msgid %lu size %lu data %s\n",
              msg_in->DataSize, msg_in->MessageSize, msg_in->MessageType, msg_in->VirtualRangesOffset,
              msg_in->ClientId.UniqueProcess, msg_in->ClientId.UniqueThread, msg_in->MessageId,
              msg_in->SectionSize, debugstr_an( (const char *)msg_in->Data, msg_in->DataSize ));
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtReplyPort (NTDLL.@)
 */
NTSTATUS WINAPI NtReplyPort( HANDLE handle, LPC_MESSAGE *reply )
{
    FIXME("(%p,%p),stub!\n", handle, reply );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtReplyWaitReceivePort (NTDLL.@)
 */
NTSTATUS WINAPI NtReplyWaitReceivePort( HANDLE handle, ULONG *id, LPC_MESSAGE *reply, LPC_MESSAGE *msg )
{
    FIXME("(%p,%p,%p,%p),stub!\n", handle, id, reply, msg );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtReplyWaitReceivePortEx (NTDLL.@)
 */
NTSTATUS WINAPI NtReplyWaitReceivePortEx( HANDLE handle, ULONG *id, LPC_MESSAGE *reply, LPC_MESSAGE *msg,
                                          LARGE_INTEGER *timeout )
{
    FIXME("(%p,%p,%p,%p,%p),stub!\n", handle, id, reply, msg, timeout );
    return STATUS_NOT_IMPLEMENTED;
}


/***********************************************************************
 *             NtWriteRequestData (NTDLL.@)
 */
NTSTATUS WINAPI NtWriteRequestData( HANDLE handle, LPC_MESSAGE *request, ULONG id,
                                    void *buffer, ULONG len, ULONG *retlen )
{
    FIXME( "(%p,%p,%u,%p,%u,%p),stub!\n", handle, request, id, buffer, len, retlen );
    return STATUS_NOT_IMPLEMENTED;
}

#define MAX_ATOM_LEN  255
#define IS_INTATOM(x) (((ULONG_PTR)(x) >> 16) == 0)

static unsigned int is_integral_atom( const WCHAR *atomstr, ULONG len, RTL_ATOM *ret_atom )
{
    RTL_ATOM atom;

    if ((ULONG_PTR)atomstr >> 16)
    {
        const WCHAR* ptr = atomstr;
        if (!len) return STATUS_OBJECT_NAME_INVALID;

        if (*ptr++ == '#')
        {
            atom = 0;
            while (ptr < atomstr + len && *ptr >= '0' && *ptr <= '9')
            {
                atom = atom * 10 + *ptr++ - '0';
            }
            if (ptr > atomstr + 1 && ptr == atomstr + len) goto done;
        }
        if (len > MAX_ATOM_LEN) return STATUS_INVALID_PARAMETER;
        return STATUS_MORE_ENTRIES;
    }
    else if ((atom = LOWORD( atomstr )) >= MAXINTATOM) return STATUS_INVALID_PARAMETER;
done:
    if (atom >= MAXINTATOM) atom = 0;
    if (!(*ret_atom = atom)) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static ULONG integral_atom_name( WCHAR *buffer, ULONG len, RTL_ATOM atom )
{
    char tmp[16];
    int ret = snprintf( tmp, sizeof(tmp), "#%u", atom );

    len /= sizeof(WCHAR);
    if (len)
    {
        if (len <= ret) ret = len - 1;
        ascii_to_unicode( buffer, tmp, ret );
        buffer[ret] = 0;
    }
    return ret * sizeof(WCHAR);
}


/***********************************************************************
 *             NtAddAtom (NTDLL.@)
 */
NTSTATUS WINAPI NtAddAtom( const WCHAR *name, ULONG length, RTL_ATOM *atom )
{
    unsigned int status = is_integral_atom( name, length / sizeof(WCHAR), atom );

    if (status == STATUS_MORE_ENTRIES)
    {
        SERVER_START_REQ( add_atom )
        {
            wine_server_add_data( req, name, length );
            status = wine_server_call( req );
            *atom = reply->atom;
        }
        SERVER_END_REQ;
    }
    TRACE( "%s -> %x\n", debugstr_wn(name, length/sizeof(WCHAR)), status == STATUS_SUCCESS ? *atom : 0 );
    return status;
}


/***********************************************************************
 *             NtDeleteAtom (NTDLL.@)
 */
NTSTATUS WINAPI NtDeleteAtom( RTL_ATOM atom )
{
    unsigned int status;

    if (!atom) status = STATUS_INVALID_HANDLE;
    else if (atom < MAXINTATOM) status = STATUS_SUCCESS;
    else SERVER_START_REQ( delete_atom )
    {
        req->atom = atom;
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}


/***********************************************************************
 *             NtFindAtom (NTDLL.@)
 */
NTSTATUS WINAPI NtFindAtom( const WCHAR *name, ULONG length, RTL_ATOM *atom )
{
    unsigned int status = is_integral_atom( name, length / sizeof(WCHAR), atom );

    if (status == STATUS_MORE_ENTRIES)
    {
        SERVER_START_REQ( find_atom )
        {
            wine_server_add_data( req, name, length );
            status = wine_server_call( req );
            *atom = reply->atom;
        }
        SERVER_END_REQ;
    }
    TRACE( "%s -> %x\n", debugstr_wn(name, length/sizeof(WCHAR)), status == STATUS_SUCCESS ? *atom : 0 );
    return status;
}


/***********************************************************************
 *             NtQueryInformationAtom (NTDLL.@)
 */
NTSTATUS WINAPI NtQueryInformationAtom( RTL_ATOM atom, ATOM_INFORMATION_CLASS class,
                                        void *ptr, ULONG size, ULONG *retsize )
{
    unsigned int status;

    switch (class)
    {
    case AtomBasicInformation:
    {
        ULONG name_len;
        ATOM_BASIC_INFORMATION *abi = ptr;

        if (size < sizeof(ATOM_BASIC_INFORMATION)) return STATUS_INVALID_PARAMETER;
        name_len = size - sizeof(ATOM_BASIC_INFORMATION);

        if (atom < MAXINTATOM)
        {
            if (atom)
            {
                abi->NameLength = integral_atom_name( abi->Name, name_len, atom );
                status = name_len ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
                abi->ReferenceCount = 1;
                abi->Pinned = 1;
            }
            else status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            SERVER_START_REQ( get_atom_information )
            {
                req->atom = atom;
                if (name_len) wine_server_set_reply( req, abi->Name, name_len );
                status = wine_server_call( req );
                if (status == STATUS_SUCCESS)
                {
                    name_len = wine_server_reply_size( reply );
                    if (name_len)
                    {
                        abi->NameLength = name_len;
                        abi->Name[name_len / sizeof(WCHAR)] = 0;
                    }
                    else
                    {
                        name_len = reply->total;
                        abi->NameLength = name_len;
                        status = STATUS_BUFFER_TOO_SMALL;
                    }
                    abi->ReferenceCount = reply->count;
                    abi->Pinned = reply->pinned;
                }
                else name_len = 0;
            }
            SERVER_END_REQ;
        }
        TRACE( "%x -> %s (%u)\n", atom, debugstr_wn(abi->Name, abi->NameLength / sizeof(WCHAR)), status );
        if (retsize) *retsize = sizeof(ATOM_BASIC_INFORMATION) + name_len;
        break;
    }

    default:
        FIXME( "Unsupported class %u\n", class );
        status = STATUS_INVALID_INFO_CLASS;
        break;
    }
    return status;
}


union tid_alert_entry
{
#ifdef USE_FUTEX
    LONG futex;
#elif defined(HAVE_KQUEUE)
    int kq;
#else
    HANDLE event;
#endif
};

#define TID_ALERT_BLOCK_SIZE (65536 / sizeof(union tid_alert_entry))
static union tid_alert_entry *tid_alert_blocks[4096];

static unsigned int handle_to_index( HANDLE handle, unsigned int *block_idx )
{
    unsigned int idx = (wine_server_obj_handle(handle) >> 2) - 1;
    *block_idx = idx / TID_ALERT_BLOCK_SIZE;
    return idx % TID_ALERT_BLOCK_SIZE;
}

static BOOL is_alert_tid_valid( HANDLE tid )
{
    unsigned int block_idx;

    handle_to_index( tid, &block_idx );
    return block_idx <= ARRAY_SIZE(tid_alert_blocks);
}

static union tid_alert_entry *get_tid_alert_entry( HANDLE tid )
{
    unsigned int block_idx, idx = handle_to_index( tid, &block_idx );
    union tid_alert_entry *entry;

    if (block_idx > ARRAY_SIZE(tid_alert_blocks))
    {
        FIXME( "tid %p is too high\n", tid );
        return NULL;
    }

    if (!tid_alert_blocks[block_idx])
    {
        static const size_t size = TID_ALERT_BLOCK_SIZE * sizeof(union tid_alert_entry);
        void *ptr = anon_mmap_alloc( size, PROT_READ | PROT_WRITE );
        if (ptr == MAP_FAILED) return NULL;
        if (InterlockedCompareExchangePointer( (void **)&tid_alert_blocks[block_idx], ptr, NULL ))
            munmap( ptr, size ); /* someone beat us to it */
    }

    entry = &tid_alert_blocks[block_idx][idx % TID_ALERT_BLOCK_SIZE];

#ifdef USE_FUTEX
    return entry;
#elif defined(HAVE_KQUEUE)
    if (!entry->kq)
    {
        int kq = kqueue();
        static const struct kevent init_event =
        {
            .ident = 1,
            .filter = EVFILT_USER,
            .flags = EV_ADD | EV_CLEAR,
            .fflags = 0,
            .data = 0,
            .udata = NULL
        };

        if (kq == -1)
        {
            ERR( "kqueue failed with error: %d (%s)\n", errno, strerror( errno ) );
            return NULL;
        }

        if (kevent( kq, &init_event, 1, NULL, 0, NULL) == -1)
        {
            ERR( "kevent creation failed with error: %d (%s)\n", errno, strerror( errno ) );
            close( kq );
            return NULL;
        }

        if (InterlockedCompareExchange( (LONG *)&entry->kq, kq, 0 ))
            close( kq );
    }
#else
    if (!entry->event)
    {
        HANDLE event;

        if (NtCreateEvent( &event, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE ))
            return NULL;
        if (InterlockedCompareExchangePointer( &entry->event, event, NULL ))
            NtClose( event );
    }
#endif

    return entry;
}


#ifdef USE_FUTEX
/* Returns the per-thread alert futex word used by NtAlertThreadByThreadId,
 * so that the RtlWaitOnAddress() fast path can wait on it directly. */
LONG *get_thread_alert_futex( void )
{
    union tid_alert_entry *entry = get_tid_alert_entry( ULongToHandle(get_thread_data()->tid) );
    return entry ? &entry->futex : NULL;
}
#endif


/***********************************************************************
 *             NtAlertMultipleThreadByThreadId (NTDLL.@)
 */
NTSTATUS WINAPI NtAlertMultipleThreadByThreadId( HANDLE *tids, ULONG count, void *unk1, void *unk2 )
{
    unsigned int i;

    TRACE( "%p %d %p %p\n", tids, (int)count, unk1, unk2 );

    if (unk1 || unk2) FIXME( "unk1 %p, unk2 %p.\n", unk1, unk2 );
    for (i = 0; i < count; ++i)
    {
        if (!is_alert_tid_valid( tids[i] )) return STATUS_INVALID_CID;
    }
    for (i = 0; i < count; ++i) NtAlertThreadByThreadId( tids[i] );
    return STATUS_SUCCESS;
}


/***********************************************************************
 *             NtAlertThreadByThreadId (NTDLL.@)
 */
NTSTATUS WINAPI NtAlertThreadByThreadId( HANDLE tid )
{
    union tid_alert_entry *entry = get_tid_alert_entry( tid );

    TRACE( "%p\n", tid );

    if (!entry) return STATUS_INVALID_CID;

#ifdef USE_FUTEX
    {
        LONG *futex = &entry->futex;
        if (!InterlockedExchange( futex, 1 ))
            futex_wake_one( futex );
        return STATUS_SUCCESS;
    }
#elif defined(HAVE_KQUEUE)
    {
        static const struct kevent signal_event =
        {
            .ident = 1,
            .filter = EVFILT_USER,
            .flags = 0,
            .fflags = NOTE_TRIGGER,
            .data = 0,
            .udata = NULL
        };

        kevent( entry->kq, &signal_event, 1, NULL, 0, NULL );
        return STATUS_SUCCESS;
    }
#else
    return NtSetEvent( entry->event, NULL );
#endif
}


#if defined(USE_FUTEX) || defined(HAVE_KQUEUE)
static LONGLONG get_absolute_timeout( const LARGE_INTEGER *timeout )
{
    LARGE_INTEGER now;

    if (timeout->QuadPart >= 0) return timeout->QuadPart;
    NtQuerySystemTime( &now );
    return now.QuadPart - timeout->QuadPart;
}

static LONGLONG update_timeout( ULONGLONG end )
{
    LARGE_INTEGER now;
    LONGLONG timeleft;

    NtQuerySystemTime( &now );
    timeleft = end - now.QuadPart;
    if (timeleft < 0) timeleft = 0;
    return timeleft;
}
#endif


/***********************************************************************
 *             NtWaitForAlertByThreadId (NTDLL.@)
 */
NTSTATUS WINAPI NtWaitForAlertByThreadId( const void *address, const LARGE_INTEGER *timeout )
{
    union tid_alert_entry *entry = get_tid_alert_entry( ULongToHandle(get_thread_data()->tid) );

    TRACE( "%p %s\n", address, debugstr_timeout( timeout ) );

    if (!entry) return STATUS_INVALID_CID;

#ifdef USE_FUTEX
    {
        LONG *futex = &entry->futex;
        ULONGLONG end;
        int ret;

        if (timeout)
        {
            if (timeout->QuadPart == TIMEOUT_INFINITE)
                timeout = NULL;
            else
                end = get_absolute_timeout( timeout );
        }

        while (!InterlockedExchange( futex, 0 ))
        {
            if (timeout)
            {
                LONGLONG timeleft = update_timeout( end );
                struct timespec timespec;

                timespec.tv_sec = timeleft / (ULONGLONG)TICKSPERSEC;
                timespec.tv_nsec = (timeleft % TICKSPERSEC) * 100;
                ret = futex_wait( futex, 0, &timespec );
            }
            else
                ret = futex_wait( futex, 0, NULL );

            if (ret == -1 && errno == ETIMEDOUT) return STATUS_TIMEOUT;
        }
        return STATUS_ALERTED;
    }
#elif defined(HAVE_KQUEUE)
    {
        ULONGLONG end;
        int ret;
        struct timespec timespec;
        struct kevent wait_event;

        if (timeout)
        {
            if (timeout->QuadPart == TIMEOUT_INFINITE)
                timeout = NULL;
            else
                end = get_absolute_timeout( timeout );
        }

        do
        {
            if (timeout)
            {
                LONGLONG timeleft = update_timeout( end );

                timespec.tv_sec = timeleft / (ULONGLONG)TICKSPERSEC;
                timespec.tv_nsec = (timeleft % TICKSPERSEC) * 100;
                if (timespec.tv_sec > 0x7FFFFFFF) timeout = NULL;
            }

            ret = kevent( entry->kq, NULL, 0, &wait_event, 1, timeout ? &timespec : NULL );
        } while (ret == -1 && errno == EINTR);

        switch (ret)
        {
        case 1:
            return STATUS_ALERTED;
        case 0:
            return STATUS_TIMEOUT;
        default:
            ERR( "kevent failed with error: %d (%s)\n", errno, strerror( errno ) );
            return STATUS_INVALID_HANDLE;
        }
    }
#else
    {
        NTSTATUS status = NtWaitForSingleObject( entry->event, FALSE, timeout );
        if (!status) return STATUS_ALERTED;
        return status;
    }
#endif
}


/***********************************************************************
 *           NtCreateTransaction (NTDLL.@)
 */
NTSTATUS WINAPI NtCreateTransaction( HANDLE *handle, ACCESS_MASK mask, OBJECT_ATTRIBUTES *obj_attr, GUID *guid, HANDLE tm,
        ULONG options, ULONG isol_level, ULONG isol_flags, PLARGE_INTEGER timeout, UNICODE_STRING *description )
{
    FIXME( "%p, %#x, %p, %s, %p, 0x%08x, 0x%08x, 0x%08x, %p, %p stub.\n", handle, mask, obj_attr, debugstr_guid(guid), tm,
            options, isol_level, isol_flags, timeout, description );

    *handle = ULongToHandle(1);

    return STATUS_SUCCESS;
}

/***********************************************************************
 *           NtCommitTransaction (NTDLL.@)
 */
NTSTATUS WINAPI NtCommitTransaction( HANDLE transaction, BOOLEAN wait )
{
    FIXME( "%p, %d stub.\n", transaction, wait );

    return STATUS_SUCCESS;
}

/***********************************************************************
 *           NtRollbackTransaction (NTDLL.@)
 */
NTSTATUS WINAPI NtRollbackTransaction( HANDLE transaction, BOOLEAN wait )
{
    FIXME( "%p, %d stub.\n", transaction, wait );

    return STATUS_ACCESS_VIOLATION;
}

/***********************************************************************
 *           NtConvertBetweenAuxiliaryCounterAndPerformanceCounter (NTDLL.@)
 */
NTSTATUS WINAPI NtConvertBetweenAuxiliaryCounterAndPerformanceCounter( ULONG flag, ULONGLONG *from, ULONGLONG *to, ULONGLONG *error )
{
    FIXME( "%#x, %p, %p, %p.\n",  flag, from, to, error );

    if (!from) return STATUS_ACCESS_VIOLATION;

    return STATUS_NOT_SUPPORTED;
}

/***********************************************************************
 *           NtOpenPrivateNamespace (NTDLL.@)
 */
NTSTATUS WINAPI NtOpenPrivateNamespace( HANDLE *handle, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr, const OBJECT_BOUNDARY_DESCRIPTOR *descriptor )
{
    FIXME( "%p, %#x, %p, %p stub.\n", handle, access, attr, descriptor );

    return STATUS_NOT_IMPLEMENTED;
}
