/*
 * Wineserver Slab Allocator for fast, zero-fragmentation object allocation
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINESERVER_SLAB_H
#define __WINESERVER_SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#define WINE_SLAB_PAGE_SIZE (64 * 1024) /* 64KB per slab page */

struct slab_node {
    struct slab_node *next;
};

struct slab_page {
    struct slab_page *next;
    char *page_base;
};

struct wine_slab_pool {
    size_t obj_size;
    struct slab_node *free_list;
    struct slab_page *pages;
    unsigned int total_alloc;
    unsigned int free_count;
};

/* Size classes: 32, 64, 128, 256 */
static struct wine_slab_pool slab_pools[4] = {
    { 32,  NULL, NULL, 0, 0 },
    { 64,  NULL, NULL, 0, 0 },
    { 128, NULL, NULL, 0, 0 },
    { 256, NULL, NULL, 0, 0 }
};

static inline int slab_index(size_t size)
{
    if (size <= 32)  return 0;
    if (size <= 64)  return 1;
    if (size <= 128) return 2;
    if (size <= 256) return 3;
    return -1;
}

static void *slab_grow(struct wine_slab_pool *pool)
{
    char *page;
    struct slab_page *sp;
    struct slab_node *head = NULL;
    size_t count, i;

    page = mmap(NULL, WINE_SLAB_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED)
        return NULL;

    sp = malloc(sizeof(*sp));
    if (!sp)
    {
        munmap(page, WINE_SLAB_PAGE_SIZE);
        return NULL;
    }
    sp->page_base = page;
    sp->next = pool->pages;
    pool->pages = sp;

    count = WINE_SLAB_PAGE_SIZE / pool->obj_size;
    for (i = 1; i < count; i++)
    {
        struct slab_node *node = (struct slab_node *)(page + i * pool->obj_size);
        node->next = head;
        head = node;
    }

    pool->free_list = head;
    pool->free_count += count - 1;
    pool->total_alloc += count;

    return (void *)page;
}

static inline void *wine_slab_alloc(size_t size)
{
    struct wine_slab_pool *pool;
    struct slab_node *node;
    int idx = slab_index(size);

    if (idx < 0)
        return malloc(size);

    pool = &slab_pools[idx];
    node = pool->free_list;
    if (__builtin_expect(node != NULL, 1))
    {
        pool->free_list = node->next;
        pool->free_count--;
        return (void *)node;
    }

    return slab_grow(pool);
}

static inline void wine_slab_free(void *ptr, size_t size)
{
    struct wine_slab_pool *pool;
    struct slab_node *node;
    int idx;

    if (!ptr) return;

    idx = slab_index(size);
    if (idx < 0)
    {
        free(ptr);
        return;
    }

    pool = &slab_pools[idx];
    node = (struct slab_node *)ptr;
    node->next = pool->free_list;
    pool->free_list = node;
    pool->free_count++;
}

#endif /* __WINESERVER_SLAB_H */
