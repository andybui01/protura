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
#include <protura/mm/user_ptr.h>
#include <arch/task.h>

#include <protura/fs/block.h>
#include <protura/fs/super.h>
#include <protura/fs/file.h>
#include <protura/fs/stat.h>
#include <protura/fs/fcntl.h>
#include <protura/fs/inode.h>
#include <protura/fs/inode_table.h>
#include <protura/fs/namei.h>
#include <protura/fs/vfs.h>
#include <protura/fs/ioctl.h>
#include <protura/fs/sys.h>

/* These functions connect the 'vfs_*' functions to the syscall verisons. Note
 * that these 'sys_*' functions are responsable for checking the userspace
 * pointers. */

int __sys_open(struct inode *inode, unsigned int file_flags, struct file **filp)
{
    int ret = 0;
    int fd;

    if (file_flags & F(FILE_WRITABLE) && S_ISDIR(inode->mode))
        return -EISDIR;

    fd = fd_get_empty();

    if (fd == -1)
        return -ENFILE;

    ret = vfs_open(inode, file_flags, filp);

    if (ret < 0)
        goto ret_fd_release;

    fd_assign(fd, *filp);
    return fd;

  ret_fd_release:
    fd_release(fd);
    return ret;
}

int sys_open(const char *__user path, int flags, mode_t mode)
{
    int ret;
    unsigned int file_flags = 0;
    struct file *filp;
    struct task *current = cpu_get_local()->current;
    struct nameidata name;

    ret = user_check_strn(path, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    /* Note, the state of no read/write flag set is valid. Other operations can
     * still be performed. */

    if (IS_RDWR(flags))
        file_flags = F(FILE_READABLE) | F(FILE_WRITABLE);
    else if (IS_WRONLY(flags))
        file_flags = F(FILE_WRITABLE);
    else if (IS_RDONLY(flags))
        file_flags = F(FILE_READABLE);

    if (flags & O_APPEND)
        file_flags |= F(FILE_APPEND);

    if (flags & O_NONBLOCK)
        file_flags |= F(FILE_NONBLOCK);

    memset(&name, 0, sizeof(name));
    name.path = path;
    name.cwd = current->cwd;

    ret = namei_full(&name, F(NAMEI_GET_INODE) | F(NAMEI_GET_PARENT) | F(NAMEI_ALLOW_TRAILING_SLASH));

    if ((flags & O_EXCL) && name.found) {
        ret = -EEXIST;
        goto cleanup_namei;
    }

    if (!name.found) {
        if (!(flags & O_CREAT) || !name.parent)
            goto cleanup_namei;

        kp(KP_TRACE, "Did not find %s, creating %s\n", path, name.name_start);
        ret = vfs_create(name.parent, name.name_start, name.name_len, mode, &name.found);
        kp(KP_TRACE, "Result: %d, name.found: %p\n", ret, name.found);
        if (ret)
            goto cleanup_namei;
    }

    kp(KP_TRACE, "Result: %d, name.found: %p, name.parent: %p\n", ret, name.found, name.parent);

    ret = __sys_open(name.found, file_flags, &filp);

    if (ret < 0)
        goto cleanup_namei;

    if (flags & O_CLOEXEC) {
        FD_SET(ret, &current->close_on_exec);
        kp(KP_TRACE, "Setting Close-on-exec for %d\n", ret);
    }

    if (flags & O_TRUNC)
        vfs_truncate(filp->inode, 0);

  cleanup_namei:
    if (name.found)
        inode_put(name.found);

    if (name.parent)
        inode_put(name.parent);

    return ret;
}

int sys_close(int fd)
{
    struct file *filp;
    int ret;

    ret = fd_get_checked(fd, &filp);

    if (ret)
        return ret;

    fd_release(fd);

    return vfs_close(filp);
}

int sys_read(int fd, void *__user buf, size_t len)
{
    struct file *filp;
    int ret;

    ret = user_check_region(buf, len, F(VM_MAP_WRITE));
    if (ret)
        return ret;

    ret = fd_get_checked(fd, &filp);

    if (ret)
        return ret;

    return vfs_read(filp, buf, len);
}

int sys_read_dent(int fd, struct dent *__user dent, size_t size)
{
    struct file *filp;
    int ret;

    ret = user_check_region(dent, size, F(VM_MAP_WRITE));
    if (ret)
        return ret;

    ret = fd_get_checked(fd, &filp);

    if (ret)
        return ret;

    return vfs_read_dent(filp, dent, size);
}

int sys_write(int fd, void *__user buf, size_t len)
{
    struct file *filp;
    int ret;

    ret = user_check_region(buf, len, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = fd_get_checked(fd, &filp);
    if (ret)
        return ret;

    return vfs_write(filp, buf, len);
}

off_t sys_lseek(int fd, off_t off, int whence)
{
    struct file *filp;
    int ret;

    ret = fd_get_checked(fd, &filp);

    if (ret)
        return ret;

    return vfs_lseek(filp, off, whence);
}

int sys_truncate(const char *__user path, off_t length)
{
    struct task *current = cpu_get_local()->current;
    struct inode *i;
    int ret;

    ret = user_check_strn(path, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = namex(path, current->cwd, &i);
    if (ret)
        return ret;

    ret = vfs_truncate(i, length);

    inode_put(i);

    return ret;
}

int sys_ftruncate(int fd, off_t length)
{
    struct file *filp;
    int ret;

    ret = fd_get_checked(fd, &filp);

    if (ret)
        return ret;

    return vfs_truncate(filp->inode, length);
}

int sys_mkdir(const char *__user name, mode_t mode)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata dirname;
    int ret;

    ret = user_check_strn(name, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&dirname, 0, sizeof(dirname));
    dirname.path = name;
    dirname.cwd = current->cwd;

    ret = namei_full(&dirname, F(NAMEI_GET_INODE) | F(NAMEI_GET_PARENT) | F(NAMEI_ALLOW_TRAILING_SLASH));
    if (!dirname.parent)
        goto cleanup_namei;

    if (dirname.found) {
        ret = -EEXIST;
        goto cleanup_namei;
    }

    if (dirname.name_len == 0) {
        ret = -ENOENT;
        goto cleanup_namei;
    }

    ret = vfs_mkdir(dirname.parent, dirname.name_start, dirname.name_len, mode);

  cleanup_namei:
    if (dirname.parent)
        inode_put(dirname.parent);

    if (dirname.found)
        inode_put(dirname.found);

    return ret;
}

int sys_rmdir(const char *__user name)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata dirname;
    int ret;

    ret = user_check_strn(name, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&dirname, 0, sizeof(dirname));

    dirname.path = name;
    dirname.cwd = current->cwd;

    ret = namei_full(&dirname, F(NAMEI_GET_INODE) | F(NAMEI_GET_PARENT) | F(NAMEI_ALLOW_TRAILING_SLASH));
    if (!dirname.parent)
        goto cleanup_namei;

    if (!dirname.found) {
        ret = -ENOENT;
        goto cleanup_namei;
    }

    if (dirname.name_len == 0) {
        ret = -ENOENT;
        goto cleanup_namei;
    }

    ret = vfs_rmdir(dirname.parent, dirname.found, dirname.name_start, dirname.name_len);

  cleanup_namei:
    if (dirname.found)
        inode_put(dirname.found);

    if (dirname.parent)
        inode_put(dirname.parent);

    return ret;
}

int sys_link(const char *__user old, const char *__user new)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata newname, oldname;
    int ret;

    ret = user_check_strn(old, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = user_check_strn(new, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&oldname, 0, sizeof(oldname));
    oldname.path = old;
    oldname.cwd = current->cwd;

    ret = namei_full(&oldname, F(NAMEI_GET_INODE) | F(NAMEI_ALLOW_TRAILING_SLASH));
    if (!oldname.found)
        return ret;

    memset(&newname, 0, sizeof(newname));

    newname.path = new;
    newname.cwd = current->cwd;

    ret = namei_full(&newname, F(NAMEI_GET_INODE) | F(NAMEI_GET_PARENT) | F(NAMEI_ALLOW_TRAILING_SLASH));
    if (!newname.parent)
        goto release_oldname;

    if (newname.found) {
        ret = -EEXIST;
        goto release_namei;
    }

    if (newname.name_len == 0) {
        ret = -ENOENT;
        goto release_namei;
    }

    ret = vfs_link(newname.parent, oldname.found, newname.name_start, newname.name_len);

  release_namei:
    if (newname.found)
        inode_put(newname.found);
    inode_put(newname.parent);
  release_oldname:
    if (oldname.found)
        inode_put(oldname.found);
    return ret;
}

int sys_mknod(const char *__user node, mode_t mode, dev_t dev)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata name;
    int ret;

    ret = user_check_strn(node, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&name, 0, sizeof(name));
    name.path = node;
    name.cwd = current->cwd;

    ret = namei_full(&name, F(NAMEI_GET_INODE) | F(NAMEI_GET_PARENT));

    if (!name.parent)
        goto cleanup;

    if (name.found) {
        ret = -EEXIST;
        goto cleanup_namei;
    }

    if (name.name_len == 0) {
        ret = -ENOENT;
        goto cleanup_namei;
    }

    ret = vfs_mknod(name.parent, name.name_start, name.name_len, mode, DEV_FROM_USERSPACE(dev));

  cleanup_namei:
    if (name.parent)
        inode_put(name.parent);

    if (name.found)
        inode_put(name.found);

  cleanup:
    return ret;
}

int sys_unlink(const char *__user file)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata name;
    int ret;

    ret = user_check_strn(file, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&name, 0, sizeof(name));
    name.path = file;
    name.cwd = current->cwd;

    ret = namei_full(&name, F(NAMEI_GET_INODE) | F(NAMEI_GET_PARENT));
    if (!name.parent)
        return ret;

    if (!name.found) {
        ret = -ENOENT;
        goto cleanup_namei;
    }

    ret = vfs_unlink(name.parent, name.found, name.name_start, name.name_len);

  cleanup_namei:
    if (name.found)
        inode_put(name.found);
    inode_put(name.parent);
    return ret;
}

int sys_rename(const char *__user old, const char *__user new)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata old_name, new_name;
    int ret;

    ret = user_check_strn(old, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = user_check_strn(new, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&old_name, 0, sizeof(old_name));
    old_name.path = old;
    old_name.cwd = current->cwd;

    ret = namei_full(&old_name, F(NAMEI_GET_PARENT));
    if (!old_name.parent)
        goto cleanup_old_name;

    if (old_name.name_len == 0) {
        ret = -ENOENT;
        goto cleanup_old_name;
    }

    memset(&new_name, 0, sizeof(new_name));
    new_name.path = new;
    new_name.cwd = current->cwd;

    ret = namei_full(&new_name, F(NAMEI_GET_PARENT));
    if (!new_name.parent)
        goto cleanup_old_name;

    if (new_name.name_len == 0) {
        ret = -ENOENT;
        goto cleanup_new_name;
    }

    ret = vfs_rename(old_name.parent, old_name.name_start, old_name.name_len, new_name.parent, new_name.name_start, new_name.name_len);

  cleanup_new_name:
    if (new_name.parent)
        inode_put(new_name.parent);

  cleanup_old_name:
    if (old_name.parent)
        inode_put(old_name.parent);

    return ret;
}

int sys_chdir(const char *__user path)
{
    int ret;

    ret = user_check_strn(path, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    return vfs_chdir(path);
}

int sys_stat(const char *path, struct stat *buf)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata name;
    int ret;

    ret = user_check_strn(path, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = user_check_region(buf, sizeof(*buf), F(VM_MAP_WRITE));
    if (ret)
        return ret;

    memset(&name, 0, sizeof(name));
    name.path = path;
    name.cwd = current->cwd;

    ret = namei_full(&name, F(NAMEI_GET_INODE) | F(NAMEI_ALLOW_TRAILING_SLASH));
    if (!name.found)
        return ret;

    ret = vfs_stat(name.found, buf);

    inode_put(name.found);

    return ret;
}

int sys_fstat(int fd, struct stat *__user buf)
{
    struct file *filp;
    int ret;

    ret = user_check_region(buf, sizeof(*buf), F(VM_MAP_WRITE));
    if (ret)
        return ret;

    ret = fd_get_checked(fd, &filp);

    if (ret)
        return ret;

    ret = vfs_stat(filp->inode, buf);

    return ret;
}

int sys_lstat(const char *__user path, struct stat *__user buf)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata name;
    int ret;

    ret = user_check_strn(path, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = user_check_region(buf, sizeof(*buf), F(VM_MAP_WRITE));
    if (ret)
        return ret;

    memset(&name, 0, sizeof(name));
    name.path = path;
    name.cwd = current->cwd;

    ret = namei_full(&name, F(NAMEI_GET_INODE) | F(NAMEI_ALLOW_TRAILING_SLASH) | F(NAMEI_DONT_FOLLOW_LINK));
    if (!name.found)
        return ret;

    ret = vfs_stat(name.found, buf);

    inode_put(name.found);

    return ret;
}

int sys_readlink(const char *__user path, char *__user buf, size_t buf_len)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata name;
    int ret;

    ret = user_check_strn(path, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = user_check_region(buf, buf_len, F(VM_MAP_WRITE));
    if (ret)
        return ret;

    memset(&name, 0, sizeof(name));
    name.path = path;
    name.cwd = current->cwd;

    ret = namei_full(&name, F(NAMEI_GET_INODE) | F(NAMEI_DONT_FOLLOW_LINK));
    if (!name.found)
        return ret;

    ret = vfs_readlink(name.found, buf, buf_len);

    inode_put(name.found);

    return ret;
}

int sys_symlink(const char *__user target, const char *__user link)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata name;
    int ret;

    ret = user_check_strn(target, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    ret = user_check_strn(link, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&name, 0, sizeof(name));
    name.path = link;
    name.cwd = current->cwd;

    ret = namei_full(&name, F(NAMEI_GET_INODE) | F(NAMEI_GET_PARENT));
    if (!name.parent)
        return ret;

    if (name.found) {
        ret = -EEXIST;
        goto cleanup_name;
    }

    ret = vfs_symlink(name.parent, name.name_start, name.name_len, target);

  cleanup_name:
    if (name.found)
        inode_put(name.found);

    if (name.parent)
        inode_put(name.parent);

    return ret;
}

int sys_mount(const char *source, const char *target, const char *fsystem, unsigned long flags, const void *data)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata target_name;
    struct nameidata source_name;
    dev_t device;
    int ret;

    memset(&source_name, 0, sizeof(source_name));

    /* We accept a NULL souce for the cases like proc, which don't have a backing block device */
    if (source) {
        ret = user_check_strn(source, PATH_MAX, F(VM_MAP_READ));
        if (ret)
            return ret;

        source_name.path = source;
        source_name.cwd = current->cwd;

        ret = namei_full(&source_name, F(NAMEI_GET_INODE));
        if (!source_name.found) {
            device = 0;
        } else if (S_ISBLK(source_name.found->mode)) {
            device = source_name.found->dev_no;
        } else {
            ret = -ENOTBLK;
            goto cleanup_source_name;
        }
    } else {
        device = 0;
    }

    ret = user_check_strn(target, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&target_name, 0, sizeof(target_name));
    target_name.path = target;
    target_name.cwd = current->cwd;

    ret = namei_full(&target_name, F(NAMEI_GET_INODE));
    if (!target_name.found)
        goto cleanup_source_name;

    if (!S_ISDIR(target_name.found->mode)) {
        ret = -ENOTDIR;
        goto cleanup_target_name;
    }

    ret = vfs_mount(target_name.found, device, fsystem, source, target);

  cleanup_target_name:
    if (target_name.found)
        inode_put(target_name.found);

  cleanup_source_name:
    if (source_name.found)
        inode_put(source_name.found);

    return ret;
}

int sys_umount(const char *target)
{
    struct task *current = cpu_get_local()->current;
    struct nameidata target_name;
    struct super_block *sb;
    int ret;

    ret = user_check_strn(target, PATH_MAX, F(VM_MAP_READ));
    if (ret)
        return ret;

    memset(&target_name, 0, sizeof(target_name));
    target_name.path = target;
    target_name.cwd = current->cwd;

    ret = namei_full(&target_name, F(NAMEI_GET_INODE));
    if (!target_name.found)
        return ret;

    kp(KP_TRACE, "umount: inode: "PRinode"\n", Pinode(target_name.found));

    sb = target_name.found->sb;

    if (target_name.found != sb->root) {
        ret = -EINVAL;
        goto cleanup_target_name;
    }

    /* We get rid of this because we can't hold any inode refs when we umount,
     * and we don't need it. */
    inode_put(target_name.found);
    target_name.found = NULL;

    ret = vfs_umount(sb);

  cleanup_target_name:
    if (target_name.found)
        inode_put(target_name.found);

    return ret;
}

int sys_fcntl(int fd, int cmd, uintptr_t arg)
{
    struct task *current = cpu_get_local()->current;
    struct file *filp;
    int ret;

    kp(KP_TRACE, "fcntl: %d, %d\n", fd, cmd);

    ret = fd_get_checked(fd, &filp);
    if (ret)
        return ret;

    switch (cmd) {
    case F_DUPFD:
        return sys_dup(fd);

    case F_GETFD:
        return FD_ISSET(fd,&current->close_on_exec);

    case F_SETFD:
        if (arg & 1)
            FD_SET(fd, &current->close_on_exec);
        else
            FD_CLR(fd, &current->close_on_exec);
        return 0;

    case F_GETFL:
        ret = 0;

        if (flag_test(&filp->flags, FILE_READABLE)
            && flag_test(&filp->flags, FILE_WRITABLE))
            ret |= O_RDWR;
        else if (flag_test(&filp->flags, FILE_READABLE))
            ret |= O_RDONLY;
        else if (flag_test(&filp->flags, FILE_WRITABLE))
            ret |= O_WRONLY;

        if (flag_test(&filp->flags, FILE_APPEND))
            ret |= O_APPEND;

        if (flag_test(&filp->flags, FILE_NONBLOCK))
            ret |= O_NONBLOCK;

        return ret;

    case F_SETFL:
        if (arg & O_APPEND)
            flag_set(&filp->flags, FILE_APPEND);
        else
            flag_clear(&filp->flags, FILE_APPEND);

        if (arg & O_NONBLOCK)
            flag_set(&filp->flags, FILE_NONBLOCK);
        else
            flag_clear(&filp->flags, FILE_NONBLOCK);

        return 0;

    }

    return -EINVAL;
}

int sys_ioctl(int fd, int cmd, uintptr_t arg)
{
    struct task *current = cpu_get_local()->current;
    struct file *filp;
    int ret;

    kp(KP_TRACE, "ioctl: %d, %d, %d\n", fd, cmd, arg);

    ret = fd_get_checked(fd, &filp);
    if (ret)
        return ret;

    switch (cmd) {
    case FIOCLEX:
        FD_SET(fd, &current->close_on_exec);
        return 0;

    case FIONCLEX:
        FD_CLR(fd, &current->close_on_exec);
        return 0;
    }

    if (filp->ops->ioctl)
        return (filp->ops->ioctl) (filp, cmd, arg);

    return -EINVAL;
}

