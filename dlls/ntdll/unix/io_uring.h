/*
 * io_uring acceleration for NtReadFile / NtWriteFile (Opt-in via WINE_IO_URING=1)
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_IO_URING_H
#define __WINE_IO_URING_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>

struct wine_uring {
    int ring_fd;
    uint32_t *sq_khead;
    uint32_t *sq_ktail;
    uint32_t *sq_kring_mask;
    uint32_t *sq_karray;
    struct io_uring_sqe *sqes;

    uint32_t *cq_khead;
    uint32_t *cq_ktail;
    uint32_t *cq_kring_mask;
    struct io_uring_cqe *cqes;
    int inited;
};

static inline int wine_uring_is_enabled(void)
{
    static int enabled = -1;
    if (enabled == -1)
    {
        const char *env = getenv("WINE_IO_URING");
        enabled = (!env || strcmp(env, "0") != 0) ? 1 : 0;
    }
    return enabled;
}

static inline int wine_uring_init(struct wine_uring *ring, unsigned int entries)
{
    struct io_uring_params p;
    size_t sq_size, cq_size;
    void *sq_ptr, *cq_ptr, *sqes;
    int fd;

    memset(&p, 0, sizeof(p));
    fd = syscall(__NR_io_uring_setup, entries, &p);
    if (fd < 0) return -1;
    ring->ring_fd = fd;

    sq_size = p.sq_off.array + p.sq_entries * sizeof(uint32_t);
    cq_size = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);

    sq_ptr = mmap(NULL, sq_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    cq_ptr = mmap(NULL, cq_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    sqes = mmap(NULL, p.sq_entries * sizeof(struct io_uring_sqe), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, IORING_OFF_SQES);

    if (sq_ptr == MAP_FAILED || cq_ptr == MAP_FAILED || sqes == MAP_FAILED)
    {
        close(fd);
        ring->ring_fd = -1;
        return -1;
    }

    ring->sq_khead = (uint32_t *)((char *)sq_ptr + p.sq_off.head);
    ring->sq_ktail = (uint32_t *)((char *)sq_ptr + p.sq_off.tail);
    ring->sq_kring_mask = (uint32_t *)((char *)sq_ptr + p.sq_off.ring_mask);
    ring->sq_karray = (uint32_t *)((char *)sq_ptr + p.sq_off.array);
    ring->sqes = (struct io_uring_sqe *)sqes;

    ring->cq_khead = (uint32_t *)((char *)cq_ptr + p.cq_off.head);
    ring->cq_ktail = (uint32_t *)((char *)cq_ptr + p.cq_off.tail);
    ring->cq_kring_mask = (uint32_t *)((char *)cq_ptr + p.cq_off.ring_mask);
    ring->cqes = (struct io_uring_cqe *)((char *)cq_ptr + p.cq_off.cqes);

    ring->inited = 1;
    return 0;
}

static inline struct wine_uring *wine_uring_get_thread_ring(void)
{
    static __thread struct wine_uring thread_ring;
    if (!thread_ring.inited)
    {
        if (wine_uring_init(&thread_ring, 32) < 0)
            return NULL;
    }
    return &thread_ring;
}

static inline ssize_t wine_uring_pread(int fd, void *buf, size_t len, off_t off)
{
    struct wine_uring *ring = wine_uring_get_thread_ring();
    struct io_uring_sqe *sqe;
    uint32_t tail, mask, index, head;
    int ret;

    if (!ring) return -1;

    tail = *ring->sq_ktail;
    mask = *ring->sq_kring_mask;
    index = tail & mask;

    sqe = &ring->sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_READ;
    sqe->fd = fd;
    sqe->addr = (uintptr_t)buf;
    sqe->len = len;
    sqe->off = off;

    ring->sq_karray[index] = index;
    __atomic_store_n(ring->sq_ktail, tail + 1, __ATOMIC_RELEASE);

    ret = syscall(__NR_io_uring_enter, ring->ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
    if (ret < 0) return -1;

    head = *ring->cq_khead;
    if (head != *ring->cq_ktail)
    {
        struct io_uring_cqe *cqe = &ring->cqes[head & *ring->cq_kring_mask];
        ssize_t res = cqe->res;
        __atomic_store_n(ring->cq_khead, head + 1, __ATOMIC_RELEASE);
        return res;
    }
    return -1;
}

static inline ssize_t wine_uring_pwrite(int fd, const void *buf, size_t len, off_t off)
{
    struct wine_uring *ring = wine_uring_get_thread_ring();
    struct io_uring_sqe *sqe;
    uint32_t tail, mask, index, head;
    int ret;

    if (!ring) return -1;

    tail = *ring->sq_ktail;
    mask = *ring->sq_kring_mask;
    index = tail & mask;

    sqe = &ring->sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = fd;
    sqe->addr = (uintptr_t)buf;
    sqe->len = len;
    sqe->off = off;

    ring->sq_karray[index] = index;
    __atomic_store_n(ring->sq_ktail, tail + 1, __ATOMIC_RELEASE);

    ret = syscall(__NR_io_uring_enter, ring->ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL);
    if (ret < 0) return -1;

    head = *ring->cq_khead;
    if (head != *ring->cq_ktail)
    {
        struct io_uring_cqe *cqe = &ring->cqes[head & *ring->cq_kring_mask];
        ssize_t res = cqe->res;
        __atomic_store_n(ring->cq_khead, head + 1, __ATOMIC_RELEASE);
        return res;
    }
    return -1;
}

#endif /* __WINE_IO_URING_H */
