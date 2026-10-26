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
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <errno.h>

#define WINE_PIPE_SHM_MAGIC 0x57505348 /* 'WPSH' */
#define WINE_PIPE_RING_SIZE (256 * 1024)
#define WINE_PIPE_RING_MASK (WINE_PIPE_RING_SIZE - 1)
#define WINE_PIPE_MAX_FDS   4096

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

struct wine_pipe_shm_entry {
    int memfd;
    int is_server;
    struct wine_pipe_shm_seg *seg;
};

static struct wine_pipe_shm_entry *pipe_shm_table[WINE_PIPE_MAX_FDS];

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

static inline void pipe_shm_close(int fd)
{
    if (fd >= 0 && fd < WINE_PIPE_MAX_FDS && pipe_shm_table[fd])
    {
        struct wine_pipe_shm_entry *entry = pipe_shm_table[fd];
        pipe_shm_table[fd] = NULL;
        if (entry->seg)
            munmap(entry->seg, sizeof(*entry->seg));
        if (entry->memfd >= 0)
            close(entry->memfd);
        free(entry);
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

static inline ssize_t pipe_shm_write(int unix_fd, const void *buf, size_t len)
{
    struct wine_pipe_shm_entry *entry;
    struct wine_pipe_ring *ring;

    if (!pipe_shm_is_enabled() || unix_fd < 0 || unix_fd >= WINE_PIPE_MAX_FDS)
        return -1;

    entry = pipe_shm_table[unix_fd];
    if (!entry || !entry->seg)
        return -1;

    ring = entry->is_server ? &entry->seg->s2c : &entry->seg->c2s;
    return pipe_shm_ring_write(ring, (const uint8_t *)buf, len);
}

static inline ssize_t pipe_shm_read(int unix_fd, void *buf, size_t len)
{
    struct wine_pipe_shm_entry *entry;
    struct wine_pipe_ring *ring;

    if (!pipe_shm_is_enabled() || unix_fd < 0 || unix_fd >= WINE_PIPE_MAX_FDS)
        return -1;

    entry = pipe_shm_table[unix_fd];
    if (!entry || !entry->seg)
        return -1;

    ring = entry->is_server ? &entry->seg->c2s : &entry->seg->s2c;
    return pipe_shm_ring_read(ring, (uint8_t *)buf, len);
}

#endif /* __WINE_PIPE_SHM_H */
