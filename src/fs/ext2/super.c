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
#include <protura/mutex.h>
#include <protura/dump_mem.h>
#include <protura/mm/kmalloc.h>
#include <protura/time.h>

#include <arch/spinlock.h>
#include <protura/fs/block.h>
#include <protura/fs/char.h>
#include <protura/fs/stat.h>
#include <protura/fs/file.h>
#include <protura/fs/inode_table.h>
#include <protura/fs/file_system.h>
#include <protura/fs/vfs.h>
#include <protura/fs/pipe.h>
#include <protura/fs/ext2.h>
#include "ext2_internal.h"

static struct super_block_ops ext2_sb_ops;


void ext2_inode_setup_ops(struct inode *inode)
{
    if (S_ISBLK(inode->mode)) {
        inode->bdev = block_dev_get(inode->dev_no);
        inode->default_fops = inode->bdev->fops;
        inode->ops = &inode_ops_null;
    } else if (S_ISCHR(inode->mode)) {
        inode->cdev = char_dev_get(inode->dev_no);
        inode->default_fops = inode->cdev->fops;
        inode->ops = &inode_ops_null;
    } else if (S_ISDIR(inode->mode)) {
        inode->default_fops = &ext2_file_ops_dir;
        inode->ops = &ext2_inode_ops_dir;
    } else if (S_ISREG(inode->mode)) {
        inode->default_fops = &ext2_file_ops_file;
        inode->ops = &ext2_inode_ops_file;
    } else if (S_ISLNK(inode->mode)) {
        inode->default_fops = NULL;
        inode->ops = &ext2_inode_ops_symlink;
    } else if (S_ISFIFO(inode->mode)) {
        inode->default_fops = &fifo_default_file_ops;
        inode->ops = &inode_ops_null;
    } else {
        kp(KP_NORMAL, "Uhh found the problem. mode: 0x%04x\n", inode->mode & S_IFMT);
    }
}

static int ext2_inode_read(struct super_block *super, struct inode *i)
{
    struct ext2_super_block *sb = container_of(super, struct ext2_super_block, sb);
    struct ext2_inode *inode = container_of(i, struct ext2_inode, i);
    struct ext2_disk_inode *disk_inode;
    struct block *b;
    ino_t ino = i->ino;
    int inode_group_blk_nr;
    int inode_group, inode_entry, inode_offset;

    kp_ext2(sb, "ext2_inode_read(%d)\n", ino);

    inode_group = (ino - 1) / sb->disksb.inodes_per_block_group;
    inode_entry = (ino - 1) % sb->disksb.inodes_per_block_group;

    using_super_block(super) {
        inode_group_blk_nr = sb->groups[inode_group].block_nr_inode_table;

        inode_group_blk_nr += (inode_entry * sizeof(struct ext2_disk_inode)) / sb->block_size;

        inode_offset = inode_entry % (sb->block_size / sizeof(struct ext2_disk_inode));

        kp_ext2(sb, "Inode group block: %d\n", sb->groups[inode_group].block_nr_inode_table);
        kp_ext2(sb, "Inode group block number: %d\n", inode_group_blk_nr);
        kp_ext2(sb, "Inode group block offset: %d\n", inode_offset);

        inode->i.sb = super;
        inode->i.sb_dev = super->dev;
        inode->i.block_size = sb->block_size;
        inode->inode_group_blk_nr = inode_group_blk_nr;
        inode->inode_group_blk_offset = inode_offset;

        using_block(super->dev, inode_group_blk_nr, b) {
            disk_inode = (struct ext2_disk_inode *)b->data;
            disk_inode += inode_offset;

            kp_ext2(sb, "inode per block: %d\n", (int)(sb->block_size / sizeof(struct ext2_disk_inode)));
            kp_ext2(sb, "Using block %d\n", inode_group_blk_nr);

            inode->i.mode = disk_inode->mode;
            inode->i.size = disk_inode->size;
            atomic32_set(&inode->i.nlinks, disk_inode->links_count);
            inode->i.blocks = disk_inode->blocks;

            inode->i.atime = disk_inode->atime;
            inode->i.mtime = disk_inode->mtime;
            inode->i.ctime = disk_inode->ctime;

            inode->dtime = disk_inode->dtime;

            inode->i.uid = disk_inode->uid;
            inode->i.gid = disk_inode->gid;

            if (S_ISCHR(disk_inode->mode) || S_ISBLK(disk_inode->mode)) {
                /* Two possible dev formats: (Found in Linux Kernel source code)
                 * Major=M, Minor=I
                 *
                 * First format: 0xMMII
                 * Second foramt: 0xIIIMMMII
                 *
                 * If blk_ptrs[0] is empty, then use second format, stored in blk_ptrs[1].
                 */
                if (disk_inode->blk_ptrs[0]) {
                    inode->i.dev_no = DEV_MAKE((disk_inode->blk_ptrs[0] & 0xFF00) >> 8, disk_inode->blk_ptrs[0] & 0xFF);
                } else {
                    inode->i.dev_no = DEV_MAKE((disk_inode->blk_ptrs[1] & 0xFFF00) >> 8, (disk_inode->blk_ptrs[1] & 0xFF) | ((disk_inode->blk_ptrs[1] >> 12) & 0xFFF00));
                }
            } else {
                unsigned int i;
                for (i = 0; i < ARRAY_SIZE(disk_inode->blk_ptrs); i++)
                    inode->blk_ptrs[i] = disk_inode->blk_ptrs[i];
            }

            ext2_inode_setup_ops(&inode->i);

            kp_ext2(sb, "mode=%d, size=%d, blocks=%d\n", \
                    disk_inode->mode, disk_inode->size, disk_inode->blocks);
        }
    }

    inode_set_valid(&inode->i);

    return 0;
}

static void verify_ext2_inode(struct super_block *super, struct ext2_inode *inode)
{
    struct block *b;
    using_block(super->dev, inode->inode_group_blk_nr, b) {
        struct ext2_disk_inode *dinode = (struct ext2_disk_inode *)b->data + inode->inode_group_blk_offset;

#define inode_assert(inode, cond) \
    kassert(cond, "inode %d:%d not set dirty!\n", (inode)->i.sb_dev, (inode)->i.ino);

        inode_assert(inode, dinode->mode == inode->i.mode);
        inode_assert(inode, (off_t)dinode->size == inode->i.size);
        inode_assert(inode, dinode->links_count == atomic32_get(&inode->i.nlinks));
        inode_assert(inode, dinode->blocks == inode->i.blocks);

        inode_assert(inode, dinode->atime == inode->i.atime);
        inode_assert(inode, dinode->mtime == inode->i.mtime);
        inode_assert(inode, dinode->ctime == inode->i.ctime);
        inode_assert(inode, dinode->dtime == inode->dtime);

        if (S_ISCHR(dinode->mode) || S_ISBLK(dinode->mode)) {
            /* FIXME: Only handles the simple cases, not the case that dev is
             * in blk_ptrs[1]. Note this isn't really important, considering we
             * never store the dev in that format. */
            if (dinode->blk_ptrs[0])
                inode_assert(inode, dinode->blk_ptrs[0] == inode->i.dev_no);
        } else {
            unsigned int i;
            for (i = 0; i < ARRAY_SIZE(inode->blk_ptrs); i++)
                inode_assert(inode, dinode->blk_ptrs[i] == inode->blk_ptrs[i]);
        }
#undef inode_assert
    }
}
#ifdef CONFIG_INODE_CHANGE_CHECK
# define ext2_inode_check(super, inode) verify_ext2_inode(super, inode)
#else
# define ext2_inode_check(super, inode) do { ; } while (0)
#endif

static int ext2_inode_write(struct super_block *super, struct inode *i)
{
    struct ext2_super_block *sb = container_of(super, struct ext2_super_block, sb);
    struct ext2_inode *inode = container_of(i, struct ext2_inode, i);
    struct block *b;

    kp_ext2(sb, "writing inode: %d\n", i->ino);

    if (!inode_is_dirty(i)) {
        kp_ext2(sb, "inode was not dirty\n");
        ext2_inode_check(super, inode);
        return 0;
    }

    kp_ext2(sb, "Inode group block: %d, inode group block offset: %d\n", inode->inode_group_blk_nr, inode->inode_group_blk_offset);
    kp_ext2(sb, "Inode links: %d\n", atomic32_get(&i->ref));
    kp_ext2(sb, "Inode nlinks: %d\n", atomic32_get(&i->nlinks));

    using_block(super->dev, inode->inode_group_blk_nr, b) {
        struct ext2_disk_inode *dinode = (struct ext2_disk_inode *)b->data + inode->inode_group_blk_offset;

        dinode->mode = inode->i.mode;
        dinode->size = inode->i.size;
        dinode->links_count = atomic32_get(&inode->i.nlinks);
        dinode->blocks = inode->i.blocks;

        dinode->atime = inode->i.atime;
        dinode->ctime = inode->i.ctime;
        dinode->mtime = inode->i.mtime;
        dinode->dtime = inode->dtime;

        dinode->uid = inode->i.uid;
        dinode->gid = inode->i.gid;

        if (S_ISCHR(inode->i.mode) || S_ISBLK(inode->i.mode)) {
            dinode->blk_ptrs[0] = (uint32_t)inode->i.dev_no;
        } else {
            unsigned int i;
            for (i = 0; i < ARRAY_SIZE(inode->blk_ptrs); i++)
                dinode->blk_ptrs[i] = inode->blk_ptrs[i];
        }

        block_mark_dirty(b);
    }

    inode_clear_dirty(i);

    return 0;
}

static int ext2_inode_delete(struct super_block *super, struct inode *i)
{
    struct ext2_super_block *sb = container_of(super, struct ext2_super_block, sb);
    struct ext2_inode *ext2i = container_of(i, struct ext2_inode, i);
    struct block *b;
    int inode_group, inode_entry;

    if (atomic32_get(&i->nlinks) != 0)
        panic("EXT2 (%p): Error, attempted to delete an inode(%d) with a non-zero number of hard-links\n", sb, i->ino);

    if (i->ino == EXT2_ACL_IDX_INO || i->ino == EXT2_ACL_DATA_INO)
        return 0;

    __ext2_inode_truncate(ext2i, 0);

    ext2i->dtime = protura_current_time_get();

    using_super_block(super) {
        if (inode_is_dirty(i)) {
            ext2_inode_write(super, i);
            list_del(&i->sb_dirty_entry);
        }

        inode_group = (i->ino - 1) / sb->disksb.inodes_per_block_group;
        inode_entry = (i->ino - 1) % sb->disksb.inodes_per_block_group;

        kp_ext2(sb, "Removing inode: "PRinode", inode_group: %d, inode_entry: %d\n", Pinode(i), inode_group, inode_entry);

        using_block(super->dev, sb->groups[inode_group].block_nr_inode_bitmap, b) {
            if (bit_test(b->data, inode_entry) == 0)
                panic("EXT2 (%p): Error, attempted to delete inode with ino(%d) not currently used!\n", sb, i->ino);

            bit_clear(b->data, inode_entry);
            block_mark_dirty(b);
        }

        sb->groups[inode_group].inode_unused_total++;
        sb->disksb.inode_unused_total++;
    }

    return 0;
}

static int ext2_inode_dealloc(struct super_block *super, struct inode *i)
{
    struct ext2_inode *inode = container_of(i, struct ext2_inode, i);

    kfree(inode);

    return 0;
}

static struct inode *ext2_inode_alloc(struct super_block *super)
{
    struct ext2_inode *inode = kzalloc(sizeof(*inode), PAL_KERNEL);

    inode_init(&inode->i);

    return &inode->i;
}

static struct super_block *ext2_sb_read(dev_t dev)
{
    struct ext2_super_block *sb;
    struct block *sb_block;
    int block_size;
    uint32_t ext2_magic;

    if (dev == 0)
        return NULL;

    sb = kzalloc(sizeof(*sb), PAL_KERNEL);
    ext2_super_block_init(sb);

    sb->sb.bdev = block_dev_get(dev);
    sb->sb.dev = dev;
    sb->sb.ops = &ext2_sb_ops;

    kp_ext2(sb, "Setting block_size to 1024\n");

    block_dev_set_block_size(dev, 1024);

    kp_ext2(sb, "Reading super_block...\n");
    using_block(dev, 1, sb_block) {
        struct ext2_disk_sb *disksb;

        disksb = (struct ext2_disk_sb *)sb_block->data;
        block_size = 1024 << disksb->block_size_shift;
        ext2_magic = disksb->ext2_magic;
        kp_ext2(sb, "block_size=%d\n", block_size);
    }

    kp_ext2(sb, "ext2_magic=%04x\n", ext2_magic);

    if (ext2_magic != EXT2_MAGIC)
        panic("EXT2: Error: Incorrect magic bits\n");

    block_dev_set_block_size(dev, block_size);
    sb->block_size = block_size;

    switch (block_size) {
    case 1024:
        sb->sb_block_nr = 1;
        using_block(dev, 1, sb_block)
            memcpy(&sb->disksb, sb_block->data, sizeof(sb->disksb));
        break;

    case 2048:
        sb->sb_block_nr = 0;
        using_block(dev, 0, sb_block)
            memcpy(&sb->disksb, sb_block->data + 1024, sizeof(sb->disksb));
        break;

    case 4096:
        sb->sb_block_nr = 0;
        using_block(dev, 0, sb_block)
            memcpy(&sb->disksb, sb_block->data + 1024, sizeof(sb->disksb));
        break;

    default:
        panic("EXT2: Error, unable to handle block_size\n");
    }

    kp_ext2(sb, "version_major=%d,      version_minor=%d\n", \
            sb->disksb.version_major, sb->disksb.version_minor);

    if (sb->disksb.version_major < 1) {
        sb->disksb.first_inode = EXT2_DEFAULT_FIRST_INO;
        sb->disksb.required_features = 0;
        sb->disksb.read_only_features = 0;
        sb->disksb.optional_features = 0;
    }
        /* panic("EXT2: Error, ext2 major version < 1 not supported!\n"); */

    kp_ext2(sb, "inode_total=%d,        block_total=%d\n", \
            sb->disksb.inode_total, sb->disksb.block_total);

    kp_ext2(sb, "inode_unused_total=%d, block_unused_total=%d\n", \
            sb->disksb.inode_unused_total, sb->disksb.block_unused_total);

    kp_ext2(sb, "required_features=%d\n", sb->disksb.required_features);

    if (sb->disksb.required_features & ~(EXT2_REQUIRED_FEATURE_DIR_TYPE))
        panic("EXT2: Error, unsupported ext2 required features!\n");

    kp_ext2(sb, "read_only_features=%d\n", \
            sb->disksb.read_only_features);

    if (sb->disksb.read_only_features & ~(EXT2_RO_FEATURE_SPARSE_SB))
        panic("EXT2: Error, unsupported ext2 read_only features!\n");

    sb->block_group_count = sb->disksb.block_total / sb->disksb.blocks_per_block_group;

    kp_ext2(sb, "block_group_count=%d\n", sb->block_group_count);

    int total_bg_blocks = (sb->block_group_count * sizeof(struct ext2_disk_block_group) + block_size - 1) / block_size;

    kp_ext2(sb, "total_bg_blocks=%d\n", total_bg_blocks);

    sb->groups = kmalloc(sizeof(struct ext2_disk_block_group) * sb->block_group_count, PAL_KERNEL);

    int i;
    int groups_per_block = block_size / sizeof(struct ext2_disk_block_group);
    for (i = 0; i < total_bg_blocks; i++) {
        int g;
        struct block *block;

        g = groups_per_block;
        if (g + i * groups_per_block > sb->block_group_count)
            g = sb->block_group_count % groups_per_block;

        kp_ext2(sb, "Reading %d groups\n", g);
        kp_ext2(sb, "Group block %d\n", sb->sb_block_nr + 1 + i);

        using_block(dev, sb->sb_block_nr + 1 + i, block)
            memcpy(sb->groups + i * groups_per_block, block->data, g * sizeof(*sb->groups));
    }

    for (i = 0; i < sb->block_group_count; i++) {
        kp_ext2(sb, "Block group %d: blocks=%d, inodes=%d, inode_table=%d\n", \
                i, \
                sb->groups[i].block_nr_block_bitmap, \
                sb->groups[i].block_nr_inode_bitmap, \
                sb->groups[i].block_nr_inode_table);
    }

    sb->disksb.last_mount_time = protura_current_time_get();

    kp_ext2(sb, "Reading root inode\n");
    sb->sb.root = inode_get(&sb->sb, EXT2_ROOT_INO);

    return &sb->sb;
}

static int ext2_sb_write(struct super_block *sb)
{
    struct ext2_super_block *ext2sb = container_of(sb, struct ext2_super_block, sb);
    struct block *b;

    kp_ext2(ext2sb, "Writing ext2 block-groups...\n");
    int i;
    int groups_per_block = ext2sb->block_size / sizeof(struct ext2_disk_block_group);
    int total_bg_blocks = (ext2sb->block_group_count * sizeof(struct ext2_disk_block_group) + ext2sb->block_size - 1) / ext2sb->block_size;

    kp_ext2(ext2sb, "groups_per_block=%d\n", groups_per_block);

    ext2sb->disksb.last_write_time = protura_current_time_get();

    for (i = 0; i < total_bg_blocks; i++) {
        int offset = i * groups_per_block;
        int count = groups_per_block;

        if (count > ext2sb->block_group_count - offset)
            count = ext2sb->block_group_count - offset;

        kp_ext2(ext2sb, "count=%d\n", count);

        using_block(ext2sb->sb.dev, ext2sb->sb_block_nr + 1 + i, b) {
            memcpy(b->data, ext2sb->groups + i * groups_per_block, count * sizeof(*ext2sb->groups));
            block_mark_dirty(b);
        }
    }

    kp_ext2(ext2sb, "Writing ext2 super-block...\n");
    using_block(ext2sb->sb.dev, ext2sb->sb_block_nr, b) {
        if (ext2sb->block_size >= 1024)
            memcpy(b->data, &ext2sb->disksb, sizeof(struct ext2_disk_sb));
        else
            memcpy(b->data + 1024, &ext2sb->disksb, sizeof(struct ext2_disk_sb));

        block_mark_dirty(b);
    }
    kp_ext2(ext2sb, "Done writing ext2 super-block.\n");

    return 0;
}

static int ext2_sb_put(struct super_block *sb)
{
    struct ext2_super_block *ext2sb = container_of(sb, struct ext2_super_block, sb);

    ext2_sb_write(sb);

    kfree(ext2sb);

    return 0;
}

static struct super_block_ops ext2_sb_ops = {
    .inode_read = ext2_inode_read,
    .inode_write = ext2_inode_write,
    .inode_delete = ext2_inode_delete,
    .inode_dealloc = ext2_inode_dealloc,
    .inode_alloc = ext2_inode_alloc,
    .sb_write = ext2_sb_write,
    .sb_put = ext2_sb_put,
};

static struct file_system ext2_fs = {
    .name = "ext2",
    .read_sb = ext2_sb_read,
    .fs_list_entry = LIST_NODE_INIT(ext2_fs.fs_list_entry),
};

void ext2_init(void)
{
    file_system_register(&ext2_fs);
}

