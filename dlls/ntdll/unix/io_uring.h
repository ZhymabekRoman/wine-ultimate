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

static void wine_uring_tls_destroy(void *arg)
{
    struct wine_uring *ring = arg;
    if (!ring || !ring->inited) return;

    if (ring->sq_ptr && ring->sq_ptr != MAP_FAILED)
        munmap(ring->sq_ptr, ring->sq_size);
    if (ring->cq_ptr && ring->cq_ptr != MAP_FAILED && ring->cq_ptr != ring->sq_ptr)
        munmap(ring->cq_ptr, ring->cq_size);
    if (ring->sqes && ring->sqes != MAP_FAILED)
        munmap(ring->sqes, ring->sqes_size);
    if (ring->ring_fd >= 0)
        close(ring->ring_fd);

    ring->ring_fd = -1;
    ring->inited = 0;
    ring->failed = 1; /* avoid re-init during thread teardown */
}

static void wine_uring_tls_init_key(void)
{
    pthread_key_create(&wine_uring_tls_key, wine_uring_tls_destroy);
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
    pthread_setspecific(wine_uring_tls_key, ring);

    ring->inited = 1;
    return 0;
}

static inline struct wine_uring *wine_uring_get_thread_ring(void)
{
    static __thread struct wine_uring thread_ring;
    if (thread_ring.failed) return NULL;
    if (!thread_ring.inited)
    {
        if (wine_uring_init(&thread_ring, 32) < 0)
            return NULL;
    }
    return &thread_ring;
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

#endif /* __WINE_IO_URING_H */
