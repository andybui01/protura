/*
 * Copyright (C) 2015 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <protura/types.h>
#include <protura/debug.h>
#include <protura/list.h>
#include <protura/hlist.h>
#include <protura/string.h>
#include <arch/spinlock.h>
#include <protura/atomic.h>
#include <protura/mm/kmalloc.h>

#include <protura/fs/block.h>
#include <protura/fs/super.h>
#include <protura/fs/inode.h>
#include <protura/fs/file.h>
#include <protura/fs/vfs.h>

/* Generic read implemented using bmap */
int fs_file_generic_read(struct file *filp, void *vbuf, size_t len)
{
    char *buf = vbuf;
    size_t have_read = 0;
    dev_t dev = filp->inode->sb_dev;

    if (!file_is_readable(filp))
        return -EBADF;

    /* Guard against reading past the end of the file */
    if (filp->offset + len > filp->inode->size)
        len = filp->inode->size - filp->offset;

    /* Access the block device for this file, and get it's block size */
    size_t block_size = filp->inode->sb->bdev->block_size;
    sector_t sec = filp->offset / block_size;
    off_t sec_off = filp->offset - sec * block_size;

    using_inode_lock_read(filp->inode) {
        while (have_read < len) {
            struct block *b;
            sector_t on_dev = vfs_bmap(filp->inode, sec);

            off_t left = (len - have_read > block_size - sec_off)?
                            block_size - sec_off:
                            len - have_read;

            /* Invalid sectors are treated as though they're a block of all zeros.
             *
             * This allows 'sparse' files, which don't have sectors allocated for
             * every position to save space. */
            if (on_dev != SECTOR_INVALID)
                using_block(dev, on_dev, b)
                    memcpy(buf + have_read, b->data + sec_off, left);
            else
                memset(buf + have_read, 0, left);

            have_read += left;

            sec_off = 0;
            sec++;
        }
    }

    filp->offset += have_read;

    filp->inode->atime = protura_current_time_get();
    inode_set_dirty(filp->inode);

    return have_read;
}

int fs_file_generic_write(struct file *filp, void *vbuf, size_t len)
{
    int ret = 0;
    char *buf = vbuf;
    size_t have_written = 0;
    dev_t dev = filp->inode->sb_dev;

    if (!file_is_writable(filp))
        return -EBADF;

    if (flag_test(&filp->flags, FILE_APPEND)) {
        kp(KP_TRACE, "write append: lseek(SEEK_END)\n");
        ret = vfs_lseek(filp, 0, SEEK_END);
        if (ret < 0)
            return ret;

#if 0
        kp(KP_TRACE, "write append: truncate(%d)\n", filp->offset + len);
        ret = vfs_truncate(filp->inode, filp->offset + len);
        if (ret)
            return ret;
#endif
    }

    if (filp->inode->size - filp->offset < len) {
        ret = vfs_truncate(filp->inode, filp->offset + len);
        if (ret)
            return ret;
    }

    size_t block_size = filp->inode->sb->bdev->block_size;
    sector_t sec = filp->offset / block_size;
    off_t sec_off = filp->offset - sec * block_size;

    using_inode_lock_write(filp->inode) {
        while (have_written < len) {
            struct block *b;
            sector_t on_dev = vfs_bmap_alloc(filp->inode, sec);

            if (on_dev == SECTOR_INVALID) {
                ret = -ENOSPC;
                break;
            }

            off_t left = (len - have_written > block_size - sec_off)?
                            block_size - sec_off:
                            len - have_written;


            using_block(dev, on_dev, b) {
                kp(KP_TRACE, "Write: %d: sec_off=%d, have_written=%d, b->data=%p\n", dev, sec_off, have_written, b->data);
                memcpy(b->data + sec_off, buf + have_written, left);
                b->dirty = 1;
            }

            have_written += left;

            sec_off = 0;
            sec++;
        }
    }

    filp->inode->ctime = filp->inode->mtime = protura_current_time_get();
    inode_set_dirty(filp->inode);

    if (ret == 0) {
        filp->offset += have_written;
        ret = have_written;
    }

    return ret;
}

off_t fs_file_generic_lseek(struct file *filp, off_t off, int whence)
{
    switch (whence) {
    case SEEK_SET:
        filp->offset = off;
        break;

    case SEEK_END:
        filp->offset = filp->inode->size;
        break;

    case SEEK_CUR:
        filp->offset += off;
        break;

    default:
        return -EINVAL;
    }

    if (filp->offset < 0)
        filp->offset = 0;

    return filp->offset;
}

struct file *file_dup(struct file *filp)
{
    atomic_inc(&filp->ref);
    kp(KP_TRACE, "File dup i:"PRinode", %d\n", Pinode(filp->inode), atomic_get(&filp->ref));
    return filp;
}

