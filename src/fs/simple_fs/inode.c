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
#include <protura/list.h>
#include <mm/kmalloc.h>

#include <arch/spinlock.h>
#include <fs/block.h>
#include <fs/file_system.h>
#include <fs/simple_fs.h>
#include <fs/stat.h>
#include <fs/file.h>

static int simple_fs_truncate(struct inode *i, off_t len)
{
    i->size = len;

    return 0;
}

static sector_t simple_fs_bmap(struct inode *i, sector_t sec)
{
    struct simple_fs_inode *inode = container_of(i, struct simple_fs_inode, i);

    if (sec > ARRAY_SIZE(inode->contents))
        return SECTOR_INVALID;

    return inode->contents[sec];
}

struct inode_ops simple_fs_inode_ops = {
    .truncate = simple_fs_truncate,
    .lookup = inode_lookup_generic,
    .bmap = simple_fs_bmap,
};
