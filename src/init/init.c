/*
 * Copyright (C) 2015 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

#include <protura/types.h>
#include <protura/debug.h>
#include <protura/stddef.h>
#include <protura/string.h>
#include <protura/scheduler.h>
#include <protura/task.h>
#include <protura/mm/kmalloc.h>
#include <arch/init.h>
#include <arch/asm.h>
#include <protura/fs/fs.h>
#include <protura/fs/namei.h>
#include <protura/fs/vfs.h>
#include <protura/block/bdev.h>
#include <protura/drivers/console.h>
#include <protura/kparam.h>
#include <protura/ktest.h>
#include <protura/initcall.h>

/* Initial user task */
struct task *task_pid1;

int kernel_is_booting = 1;

static int root_major = CONFIG_ROOT_MAJOR;
static int root_minor = CONFIG_ROOT_MINOR;
static const char *root_fstype = CONFIG_ROOT_FSTYPE;
static const char *init_prog = "/bin/init";

KPARAM("major", &root_major, KPARAM_INT);
KPARAM("minor", &root_minor, KPARAM_INT);
KPARAM("fstype", &root_fstype, KPARAM_STRING);
KPARAM("init", &init_prog, KPARAM_STRING);
KPARAM("reboot_on_panic", &reboot_on_panic, KPARAM_BOOL);

static int start_user_init(void *unused)
{
    void (**ic) (void);

    for (ic = initcalls; *ic; ic++)
        (*ic) ();

    kp(KP_NORMAL, "Mounting root device %d:%d, fs type \"%s\"\n", root_major, root_minor, root_fstype);

    /* Mount the current IDE drive as an ext2 filesystem */
    int ret = mount_root(DEV_MAKE(root_major, root_minor), root_fstype);
    if (ret)
        panic("UNABLE TO MOUNT ROOT FILESYSTEM, (%d, %d): %s\n", root_major, root_minor, root_fstype);

#ifdef CONFIG_KERNEL_TESTS
    ktest_init();
#endif

    kp(KP_NORMAL, "Kernel is done booting!\n");
    kp(KP_NORMAL, "Starting \"%s\"...\n", init_prog);

    task_pid1 = task_user_new_exec(init_prog);
    task_pid1->pid = 1;

    scheduler_task_add(task_pid1);

    return 0;
}

/* 
 * We want to get to a process context as soon as possible, as not being in one
 * complicates what we can and can't do (For example, cpu_get_local()->current
 * is NULL until we enter a process context, so we can't sleep and we can't
 * register for wait queues, take mutex's, etc...). This is similar to an
 * interrupt context.
 */
void kmain(void)
{
    cpu_setup_idle();

    struct task *t = kmalloc(sizeof(*t), PAL_KERNEL | PAL_ATOMIC);
    if (!t)
        panic("Unable to allocate kernel init task!\n");

    task_init(t);
    task_kernel_init(t, "Kernel init", start_user_init, NULL);
    scheduler_task_add(t);

    kp(KP_NORMAL, "Starting scheduler\n");
    cpu_start_scheduler();

    panic("ERROR! cpu_start_scheduler() returned!\n");
    while (1)
        hlt();
}

