/*
 * Copyright (C) 2015 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <protura/types.h>
#include <protura/debug.h>
#include <protura/string.h>
#include <protura/spinlock.h>
#include <protura/list.h>
#include <mm/kmalloc.h>
#include <drivers/ide.h>

#include <arch/task.h>
#include <arch/scheduler.h>
#include <arch/cpu.h>
#include <fs/block.h>

#define BLOCK_HASH_TABLE_SIZE 256

#define BLOCK_CACHE_MAX_SIZE 4096
#define BLOCK_CACHE_SHRINK_COUNT 512

static struct {
    struct spinlock lock;

    /* List of cached blocks ready to be checked-out.
     *
     * Note: Some of the cached blocks may be currently locked by another
     * process. */
    int cache_count;
    struct hlist_head cache[BLOCK_HASH_TABLE_SIZE];
    struct list_head lru;
} block_cache = {
    .lock = SPINLOCK_INIT("Block cache"),
    .cache_count = 0,
    .cache = { { NULL }, },
    .lru = LIST_HEAD_INIT(block_cache.lru),
};

static inline int block_hash(kdev_t device, ksector_t sector)
{
    return ((DEV_MAJOR(device) + DEV_MINOR(device)) ^ (sector)) % BLOCK_HASH_TABLE_SIZE;
}

void block_cache_init(void)
{

}

void __block_cache_shrink(void)
{
    int i = 0;
    struct block *b, *next, *first;

    /* We loop over the list backwards. We start at the 'first' entry so that
     * when we call 'list_prev_entry()' we get the last entry in the list. */
    first = list_first_entry(&block_cache.lru, struct block, block_lru_node);

    next = list_last_entry(&block_cache.lru, struct block, block_lru_node);

    for (i = 0; i < BLOCK_CACHE_SHRINK_COUNT; i++) {
        b = next;
        next = list_prev_entry(b, block_lru_node);

        /* If we hit the first entry, then we exit the loop.
         *
         * Note: There's probably a error somewhere else in the kernel causing
         * lots of blocks to not be unlocked/released, because a large majority
         * of the blocks in the cache are currently locked. */
        if (b == first)
            break;

        if (block_try_lock(b) != SUCCESS)
            continue;

        if (block_waiting(b)) {
            /* We got 'lucky' and happened to get the lock, but there are
             * sleeping processes still waiting to use this block - Don't get
             * rid of it. */
            block_unlock(b);
            continue;
        }

        /* Remove this block from the cache */
        hlist_del(&b->cache);
        block_cache.cache_count--;

        kfree(b->data);
        kfree(b);
    }
}

static struct block *block_new(void)
{
    struct block *b = kzmalloc(sizeof(*b), PMAL_KERNEL);

    mutex_init(&b->block_mutex);

    return b;
}

static struct block *__bread(kdev_t device, ksize_t block_size, ksector_t sector)
{
    struct block *b = NULL;
    int hash = block_hash(device, sector);

    hlist_foreach_entry(block_cache.cache + hash, b, cache)
        if (b->dev == device && b->sector == sector)
            break;

    if (b)
        return b;

    b = block_new();
    b->block_size = block_size;
    b->data = kzmalloc(block_size, PMAL_KERNEL);
    b->sector = sector;
    b->dev = device;

    /* Insert our new block into the cache */
    hlist_add(block_cache.cache + hash, &b->cache);
    block_cache.cache_count++;

    if (block_cache.cache_count > BLOCK_CACHE_MAX_SIZE)
        __block_cache_shrink();

    return b;
}

struct block *bread(kdev_t device, ksector_t sector)
{
    struct block *b;
    struct block_device *dev = block_dev_get(device);

    kprintf("Asking for block: %d\n", sector);
    kprintf("Dev: %p\n", dev);
    if (dev)
        kprintf("Dev: %s\n", dev->name);

    if (!dev)
        return NULL;


    kprintf("Grabbing block_cache lock...\n");
    using_spinlock(&block_cache.lock) {

        kprintf("calling unlocked bread\n");
        b = __bread(device, dev->block_size, sector);
    }

    /* We can't attempt to lock the block while we have block_cache.lock, or
     * else we might sleep with the block_cache still locked, which will bring
     * any block-IO to a halt until we're woke-up.
     *
     * Note that even if we sleep, this block will always be valid when we're
     * woke-up because a block won't be removed from the cache unless it has an
     * empty wait_queue. */
    block_lock(b);

    using_spinlock(&block_cache.lock) {
        if (list_node_is_in_list(&b->block_lru_node))
            list_del(&b->block_lru_node);
        list_add(&block_cache.lru, &b->block_lru_node);
    }

    block_dev_sync_block(dev, b);

    return b;
}

void brelease(struct block *b)
{
    block_unlock(b);
}

void block_cache_sync(void)
{
    int i;
    struct block_device *dev;

    for (i = 0; i < BLOCK_HASH_TABLE_SIZE; i++) {
        struct block *b;
        hlist_foreach_entry(block_cache.cache + i, b, cache) {
            /* We just skip any currently locked blocks, since we don't want to
             * sleep and wait for them when doing a sync. */
            if (block_try_lock(b) != SUCCESS)
                continue;

            dev = block_dev_get(b->dev);

            block_dev_sync_block(dev, b);

            block_unlock(b);
        }
    }
}
