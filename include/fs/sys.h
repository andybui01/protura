/*
 * Copyright (C) 2015 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */
#ifndef INCLUDE_FS_SYS_H
#define INCLUDE_FS_SYS_H

#include <protura/irq.h>
#include <fs/inode.h>
#include <fs/file.h>
#include <mm/user_ptr.h>

#define PATH_MAX 256

int sys_open(const char *__user file, int flags, mode_t mode);
int sys_close(int fd);
int sys_read(int fd, void *__user buf, size_t len);
int sys_write(int fd, void *__user buf, size_t len);
off_t sys_lseek(int fd, off_t off, int whence);
int sys_read_dent(int fd, struct dent *__user dent, size_t size);

int sys_truncate(const char *__user file, off_t length);

/* Called internally by sys_open - Performs the same function, but takes
 * arguments relating to inode's and file's rather then a path name and
 * userspace flags. If you have to use sys_open in the kernel. */
int __sys_open(struct inode *inode, unsigned int file_flags, struct file **filp);

int sys_exec(const char *__user executable, char *const __user argv[], struct irq_frame *);

#endif
