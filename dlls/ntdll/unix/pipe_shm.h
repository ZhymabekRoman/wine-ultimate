/*
 * Lockless Shared Memory IPC for Wine Named Pipes (Opt-in via WINE_PIPE_SHM=1)
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_PIPE_SHM_H
#define __WINE_PIPE_SHM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <errno.h>

#define WINE_PIPE_SHM_MAGIC 0x57505348 /* 'WPSH' */
#define WINE_PIPE_RING_SIZE (256 * 1024)
#define WINE_PIPE_RING_MASK (WINE_PIPE_RING_SIZE - 1)
#define WINE_PIPE_CACHE_MAX 128

struct wine_pipe_ring {
    _Alignas(64) volatile uint32_t head;
    _Alignas(64) volatile uint32_t tail;
    _Alignas(64) volatile uint32_t waiters;
    uint8_t data[WINE_PIPE_RING_SIZE];
};

struct wine_pipe_shm_seg {
    uint32_t magic;
    uint32_t version;
    struct wine_pipe_ring c2s;
    struct wine_pipe_ring s2c;
};

struct wine_pipe_handle_entry {
    void *handle;
    struct wine_pipe_shm_seg *seg;
    unsigned int is_server;
    size_t size;
};

static struct wine_pipe_handle_entry pipe_handle_cache[WINE_PIPE_CACHE_MAX];
static unsigned int pipe_handle_cache_count = 0;

static inline int pipe_shm_is_enabled(void)
{
    static int enabled = -1;
    if (enabled == -1)
    {
        const char *env = getenv("WINE_PIPE_SHM");
        enabled = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
    }
    return enabled;
}

static inline struct wine_pipe_handle_entry *pipe_handle_lookup(void *handle)
{
    unsigned int i;
    for (i = 0; i < pipe_handle_cache_count; i++)
    {
        if (pipe_handle_cache[i].handle == handle)
            return &pipe_handle_cache[i];
    }
    return NULL;
}

static inline struct wine_pipe_handle_entry *pipe_handle_insert(void *handle, void *seg, unsigned int is_srv, size_t sz)
{
    if (pipe_handle_cache_count < WINE_PIPE_CACHE_MAX)
    {
        unsigned int idx = pipe_handle_cache_count++;
        pipe_handle_cache[idx].handle = handle;
        pipe_handle_cache[idx].seg = (struct wine_pipe_shm_seg *)seg;
        pipe_handle_cache[idx].is_server = is_srv;
        pipe_handle_cache[idx].size = sz;
        return &pipe_handle_cache[idx];
    }
    return NULL;
}

static inline void pipe_handle_remove(void *handle)
{
    unsigned int i;
    for (i = 0; i < pipe_handle_cache_count; i++)
    {
        if (pipe_handle_cache[i].handle == handle)
        {
            if (pipe_handle_cache[i].seg)
                munmap(pipe_handle_cache[i].seg, pipe_handle_cache[i].size);
            pipe_handle_cache[i] = pipe_handle_cache[--pipe_handle_cache_count];
            break;
        }
    }
}

static inline ssize_t pipe_shm_ring_write(struct wine_pipe_ring *ring, const uint8_t *buf, size_t len)
{
    uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
    uint32_t available = WINE_PIPE_RING_SIZE - (head - tail);
    uint32_t off;

    if (len > available)
        len = available;
    if (!len)
        return -EAGAIN;

    off = head & WINE_PIPE_RING_MASK;
    if (off + len <= WINE_PIPE_RING_SIZE)
    {
        memcpy(&ring->data[off], buf, len);
    }
    else
    {
        uint32_t part = WINE_PIPE_RING_SIZE - off;
        memcpy(&ring->data[off], buf, part);
        memcpy(ring->data, buf + part, len - part);
    }

    __atomic_store_n(&ring->head, head + len, __ATOMIC_RELEASE);

    if (__atomic_load_n(&ring->waiters, __ATOMIC_ACQUIRE))
    {
        __atomic_store_n(&ring->waiters, 0, __ATOMIC_RELEASE);
        syscall(__NR_futex, &ring->head, FUTEX_WAKE, 1, NULL, NULL, 0);
    }

    return (ssize_t)len;
}

static inline ssize_t pipe_shm_ring_read(struct wine_pipe_ring *ring, uint8_t *buf, size_t len)
{
    uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
    uint32_t available = head - tail;
    uint32_t off;

    if (!available)
        return -EAGAIN;

    if (len > available)
        len = available;

    off = tail & WINE_PIPE_RING_MASK;
    if (off + len <= WINE_PIPE_RING_SIZE)
    {
        memcpy(buf, &ring->data[off], len);
    }
    else
    {
        uint32_t part = WINE_PIPE_RING_SIZE - off;
        memcpy(buf, &ring->data[off], part);
        memcpy(buf + part, ring->data, len - part);
    }

    __atomic_store_n(&ring->tail, tail + len, __ATOMIC_RELEASE);

    return (ssize_t)len;
}

#endif /* __WINE_PIPE_SHM_H */
