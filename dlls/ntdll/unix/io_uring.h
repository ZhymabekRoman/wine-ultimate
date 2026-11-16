/*
 * io_uring acceleration for regular file NtReadFile / NtWriteFile
 *
 * Copyright 2026 Zhymabek Roman
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

#ifndef __WINE_IO_URING_H
#define __WINE_IO_URING_H

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>

struct wine_uring
{
    int ring_fd;
    uint32_t *sq_khead;
    uint32_t *sq_ktail;
    uint32_t *sq_kring_mask;
    uint32_t *sq_karray;
    struct io_uring_sqe *sqes;
    void *sq_ptr;
    size_t sq_size;

    uint32_t *cq_khead;
    uint32_t *cq_ktail;
    uint32_t *cq_kring_mask;
    struct io_uring_cqe *cqes;
    void *cq_ptr;
    size_t cq_size;

    size_t sqes_size;
    int inited;
    int failed;
};

static pthread_key_t wine_uring_tls_key;
static pthread_once_t wine_uring_tls_once = PTHREAD_ONCE_INIT;
static __thread struct wine_uring t_uring;

#define WINE_URING_ASYNC_ENTRIES 256

struct wine_uring_req
{
    HANDLE    handle;
    HANDLE    event;
    HANDLE    iocp_port;
    ULONG_PTR ckey;
    ULONG_PTR cvalue;
    void     *iosb_target;
    void     *buffer;
    uint64_t  offset;
    ULONG     length;
    int       unix_fd;
    int       needs_close;
    uint8_t   is_write;
    uint8_t   is_wow64_io;
    uint8_t   in_use;
    uint8_t   completing;
};

struct wine_uring_async
{
    struct wine_uring ring;
    pthread_mutex_t lock;
    struct wine_uring_req reqs[WINE_URING_ASYNC_ENTRIES];
    uint64_t free_mask[WINE_URING_ASYNC_ENTRIES / 64];
    int inflight;
};

static struct wine_uring_async g_async_uring =
{
    .ring = { .ring_fd = -1 },
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .free_mask = { ~0ULL, ~0ULL, ~0ULL, ~0ULL },
};

static void wine_uring_close_ring(struct wine_uring *ring)
{
    if (ring->inited)
    {
        if (ring->sq_ptr && ring->sq_ptr != MAP_FAILED)
            munmap(ring->sq_ptr, ring->sq_size);
        if (ring->cq_ptr && ring->cq_ptr != MAP_FAILED && ring->cq_ptr != ring->sq_ptr)
            munmap(ring->cq_ptr, ring->cq_size);
        if (ring->sqes && ring->sqes != MAP_FAILED)
            munmap(ring->sqes, ring->sqes_size);
        if (ring->ring_fd >= 0)
            close(ring->ring_fd);
    }
    ring->ring_fd = -1;
    ring->inited = 0;
    ring->sq_ptr = NULL;
    ring->cq_ptr = NULL;
    ring->sqes = NULL;
}

static void wine_uring_atfork_child(void)
{
    wine_uring_close_ring(&t_uring);
    wine_uring_close_ring(&g_async_uring.ring);
    pthread_mutex_init(&g_async_uring.lock, NULL);
    memset(g_async_uring.reqs, 0, sizeof(g_async_uring.reqs));
    memset(g_async_uring.free_mask, 0xff, sizeof(g_async_uring.free_mask));
    __atomic_store_n(&g_async_uring.inflight, 0, __ATOMIC_RELAXED);
}

static void wine_uring_tls_destroy(void *arg)
{
    struct wine_uring *ring = arg;
    if (!ring || !ring->inited) return;

    wine_uring_close_ring(ring);
    ring->failed = 1; /* avoid re-init during thread teardown */
}

static void wine_uring_tls_init_key(void)
{
    pthread_key_create(&wine_uring_tls_key, wine_uring_tls_destroy);
    pthread_atfork(NULL, NULL, wine_uring_atfork_child);
}

static inline int wine_uring_is_enabled(void)
{
    static int enabled = -1;
    if (enabled == -1)
    {
        const char *env = getenv("WINE_IO_URING");
        /* Enabled by default; set WINE_IO_URING=0 to disable. */
        enabled = (!env || strcmp(env, "0") != 0) ? 1 : 0;
    }
    return enabled;
}

static inline int wine_uring_init(struct wine_uring *ring, unsigned int entries)
{
    struct io_uring_params p;
    size_t sq_size, cq_size, sqes_size;
    void *sq_ptr, *cq_ptr, *sqes;
    int fd;

    memset(&p, 0, sizeof(p));
    fd = syscall(__NR_io_uring_setup, entries, &p);
    if (fd < 0)
    {
        ring->failed = 1;
        return -1;
    }

    sq_size = p.sq_off.array + p.sq_entries * sizeof(uint32_t);
    cq_size = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    sqes_size = p.sq_entries * sizeof(struct io_uring_sqe);

    sq_ptr = mmap(NULL, sq_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    cq_ptr = (p.features & IORING_FEAT_SINGLE_MMAP)
        ? sq_ptr
        : mmap(NULL, cq_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    sqes = mmap(NULL, sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);

    if (sq_ptr == MAP_FAILED || cq_ptr == MAP_FAILED || sqes == MAP_FAILED)
    {
        if (sq_ptr != MAP_FAILED) munmap(sq_ptr, sq_size);
        if (cq_ptr != MAP_FAILED && cq_ptr != sq_ptr) munmap(cq_ptr, cq_size);
        if (sqes != MAP_FAILED) munmap(sqes, sqes_size);
        close(fd);
        ring->ring_fd = -1;
        ring->failed = 1;
        return -1;
    }

    ring->ring_fd = fd;
    ring->sq_ptr = sq_ptr;
    ring->sq_size = sq_size;
    ring->cq_ptr = cq_ptr;
    ring->cq_size = cq_size;
    ring->sqes_size = sqes_size;

    ring->sq_khead = (uint32_t *)((char *)sq_ptr + p.sq_off.head);
    ring->sq_ktail = (uint32_t *)((char *)sq_ptr + p.sq_off.tail);
    ring->sq_kring_mask = (uint32_t *)((char *)sq_ptr + p.sq_off.ring_mask);
    ring->sq_karray = (uint32_t *)((char *)sq_ptr + p.sq_off.array);
    ring->sqes = (struct io_uring_sqe *)sqes;

    ring->cq_khead = (uint32_t *)((char *)cq_ptr + p.cq_off.head);
    ring->cq_ktail = (uint32_t *)((char *)cq_ptr + p.cq_off.tail);
    ring->cq_kring_mask = (uint32_t *)((char *)cq_ptr + p.cq_off.ring_mask);
    ring->cqes = (struct io_uring_cqe *)((char *)cq_ptr + p.cq_off.cqes);

    pthread_once(&wine_uring_tls_once, wine_uring_tls_init_key);
    if (ring == &t_uring)
        pthread_setspecific(wine_uring_tls_key, ring);

    ring->inited = 1;
    return 0;
}

static inline struct wine_uring *wine_uring_get_thread_ring(void)
{
    if (t_uring.failed) return NULL;
    if (!t_uring.inited)
    {
        if (wine_uring_init(&t_uring, 32) < 0)
            return NULL;
    }
    return &t_uring;
}

/* Submit a single SQE and wait for its CQE. Returns >= 0 on success, or -1 with
 * errno set so callers fall back cleanly on EFAULT / EINTR / EOPNOTSUPP. */
static inline ssize_t wine_uring_submit_one(struct wine_uring *ring, unsigned int opcode,
                                            int fd, uint64_t addr, uint32_t len, uint64_t off)
{
    struct io_uring_sqe *sqe;
    uint32_t tail, mask, index, head;
    int ret;

    tail = *ring->sq_ktail;
    mask = *ring->sq_kring_mask;
    index = tail & mask;

    sqe = &ring->sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = opcode;
    sqe->fd = fd;
    sqe->addr = addr;
    sqe->len = len;
    sqe->off = off;

    ring->sq_karray[index] = index;
    __atomic_store_n(ring->sq_ktail, tail + 1, __ATOMIC_RELEASE);

    do
    {
        ret = syscall(__NR_io_uring_enter, ring->ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) return -1;

    head = __atomic_load_n(ring->cq_khead, __ATOMIC_RELAXED);
    if (head != __atomic_load_n(ring->cq_ktail, __ATOMIC_ACQUIRE))
    {
        struct io_uring_cqe *cqe = &ring->cqes[head & *ring->cq_kring_mask];
        ssize_t res = cqe->res;
        __atomic_store_n(ring->cq_khead, head + 1, __ATOMIC_RELEASE);
        if (res < 0)
        {
            errno = (int)(-res);
            return -1;
        }
        return res;
    }

    errno = EIO;
    return -1;
}

static inline ssize_t wine_uring_pread(int fd, void *buf, size_t len, off_t off)
{
    struct wine_uring *ring = wine_uring_get_thread_ring();
    if (!ring)
    {
        errno = ENOSYS;
        return -1;
    }
    return wine_uring_submit_one(ring, IORING_OP_READ, fd, (uintptr_t)buf, len, (uint64_t)off);
}

static inline ssize_t wine_uring_pwrite(int fd, const void *buf, size_t len, off_t off)
{
    struct wine_uring *ring = wine_uring_get_thread_ring();
    if (!ring)
    {
        errno = ENOSYS;
        return -1;
    }
    return wine_uring_submit_one(ring, IORING_OP_WRITE, fd, (uintptr_t)buf, len, (uint64_t)off);
}

static inline int wine_uring_async_alloc_slot_locked(void)
{
    int w;
    for (w = 0; w < (WINE_URING_ASYNC_ENTRIES / 64); w++)
    {
        if (g_async_uring.free_mask[w])
        {
            int bit = __builtin_ctzll(g_async_uring.free_mask[w]);
            g_async_uring.free_mask[w] &= ~(1ULL << bit);
            return w * 64 + bit;
        }
    }
    return -1;
}

static inline void wine_uring_async_free_slot_locked(int slot)
{
    g_async_uring.reqs[slot].in_use = 0;
    g_async_uring.reqs[slot].completing = 0;
    g_async_uring.free_mask[slot >> 6] |= (1ULL << (slot & 63));
    __atomic_sub_fetch(&g_async_uring.inflight, 1, __ATOMIC_RELEASE);
}

static inline int wine_uring_async_submit(const struct wine_uring_req *template_req)
{
    struct wine_uring *ring = &g_async_uring.ring;
    struct io_uring_sqe *sqe;
    uint32_t tail, mask, index;
    int slot, ret;

    pthread_mutex_lock(&g_async_uring.lock);
    if (ring->failed)
    {
        pthread_mutex_unlock(&g_async_uring.lock);
        return -1;
    }
    if (!ring->inited)
    {
        if (wine_uring_init(ring, WINE_URING_ASYNC_ENTRIES) < 0)
        {
            pthread_mutex_unlock(&g_async_uring.lock);
            return -1;
        }
    }

    slot = wine_uring_async_alloc_slot_locked();
    if (slot < 0)
    {
        pthread_mutex_unlock(&g_async_uring.lock);
        return -1;
    }

    g_async_uring.reqs[slot] = *template_req;
    g_async_uring.reqs[slot].in_use = 1;
    g_async_uring.reqs[slot].completing = 0;
    __atomic_add_fetch(&g_async_uring.inflight, 1, __ATOMIC_RELEASE);

    tail = *ring->sq_ktail;
    mask = *ring->sq_kring_mask;
    index = tail & mask;

    sqe = &ring->sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = template_req->is_write ? IORING_OP_WRITE : IORING_OP_READ;
    sqe->fd = template_req->unix_fd;
    sqe->addr = (uintptr_t)template_req->buffer;
    sqe->len = template_req->length;
    sqe->off = template_req->offset;
    sqe->user_data = (uint64_t)(unsigned int)slot;

    ring->sq_karray[index] = index;
    __atomic_store_n(ring->sq_ktail, tail + 1, __ATOMIC_RELEASE);

    do
    {
        ret = syscall(__NR_io_uring_enter, ring->ring_fd, 1, 0, 0, NULL);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0)
    {
        wine_uring_async_free_slot_locked(slot);
        pthread_mutex_unlock(&g_async_uring.lock);
        return -1;
    }

    pthread_mutex_unlock(&g_async_uring.lock);
    return 0;
}

static inline int wine_uring_async_pop_cqe(struct wine_uring_req *out_req, int32_t *out_res, int *out_slot)
{
    struct wine_uring *ring = &g_async_uring.ring;
    uint32_t head, tail;

    if (!__atomic_load_n(&g_async_uring.inflight, __ATOMIC_ACQUIRE))
        return 0;

    pthread_mutex_lock(&g_async_uring.lock);
    if (!ring->inited)
    {
        pthread_mutex_unlock(&g_async_uring.lock);
        return 0;
    }

    head = __atomic_load_n(ring->cq_khead, __ATOMIC_RELAXED);
    tail = __atomic_load_n(ring->cq_ktail, __ATOMIC_ACQUIRE);
    if (head != tail)
    {
        struct io_uring_cqe *cqe = &ring->cqes[head & *ring->cq_kring_mask];
        int slot = (int)cqe->user_data;
        *out_res = cqe->res;
        __atomic_store_n(ring->cq_khead, head + 1, __ATOMIC_RELEASE);
        if (slot >= 0 && slot < WINE_URING_ASYNC_ENTRIES && g_async_uring.reqs[slot].in_use)
        {
            g_async_uring.reqs[slot].completing = 1;
            *out_req = g_async_uring.reqs[slot];
            *out_slot = slot;
            pthread_mutex_unlock(&g_async_uring.lock);
            return 1;
        }
        pthread_mutex_unlock(&g_async_uring.lock);
        return -1;
    }

    pthread_mutex_unlock(&g_async_uring.lock);
    return 0;
}

static inline void wine_uring_async_finish_slot(int slot)
{
    pthread_mutex_lock(&g_async_uring.lock);
    wine_uring_async_free_slot_locked(slot);
    pthread_mutex_unlock(&g_async_uring.lock);
}

#endif /* __WINE_IO_URING_H */
