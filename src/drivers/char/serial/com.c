/*
 * Copyright (C) 2015 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */

/* Driver for the PC UART COM port */

#include <protura/types.h>
#include <protura/debug.h>
#include <protura/string.h>
#include <protura/scheduler.h>
#include <protura/signal.h>
#include <protura/wait.h>
#include <protura/basic_printf.h>
#include <protura/work.h>
#include <protura/dev.h>
#include <protura/kparam.h>
#include <protura/initcall.h>

#include <protura/event/device.h>
#include <arch/spinlock.h>
#include <arch/drivers/keyboard.h>
#include <arch/asm.h>
#include <arch/drivers/pic8259.h>
#include <arch/idt.h>
#include <protura/fs/char.h>
#include <protura/drivers/tty.h>
#include <protura/drivers/com.h>

#define COM_CLOCK 115200

static int com_init_was_early = 0;

enum {
    COM1,
    COM2,
};

struct com_port {
    io_t ioport;
    int baud;
    int irq;

    int exists :1;

    struct tty *tty;

    struct irq_handler handler;
};

static struct com_port com_ports[];

static int com_write_ready(struct com_port *com)
{
    return inb(com->ioport + UART_LSR) & UART_LSR_THRE;
}

static void com_int_handler(struct irq_frame *frame, void *param)
{
    char b;
    struct com_port *com = param;

    do {
        b = inb(com->ioport + UART_RX);

        struct tty *tty = com->tty;
        if (tty)
            tty_add_input(tty, &b, 1);

    } while (inb(com->ioport + UART_LSR) & UART_LSR_DR);
}

static struct com_port com_ports[] = {
    [COM1] = {
        .ioport = 0x3F8,
        .baud = 38400,
        .irq = 4,
        .exists = 1,
        .handler = IRQ_HANDLER_INIT(com_ports[COM1].handler, "COM1", com_int_handler, com_ports + COM1, IRQ_INTERRUPT, 0),
    },
    [COM2] = {
        .ioport = 0x2F8,
        .baud = 38400,
        .irq = 3,
        .exists = 1,
        .handler = IRQ_HANDLER_INIT(com_ports[COM2].handler, "COM2", com_int_handler, com_ports + COM2, IRQ_INTERRUPT, 0),
    },
};

static void com_init_ports_tx(void)
{
    size_t i;
    irq_flags_t irq_flags;

    irq_flags = irq_save();
    irq_disable();

    for (i = 0; i < ARRAY_SIZE(com_ports); i++) {
        if (!com_ports[i].exists)
            continue;

        outb(com_ports[i].ioport + UART_IER, 0x00);
        outb(com_ports[i].ioport + UART_LCR, UART_LCR_DLAB);
        outb(com_ports[i].ioport + UART_DLL, (COM_CLOCK / com_ports[i].baud) & 0xFF);
        outb(com_ports[i].ioport + UART_DLM, (COM_CLOCK / com_ports[i].baud) >> 8);
        outb(com_ports[i].ioport + UART_LCR, UART_LCR_WLEN8);
        outb(com_ports[i].ioport + UART_FCR, UART_FCR_TRIGGER_14
                | UART_FCR_CLEAR_XMIT
                | UART_FCR_CLEAR_RCVR
                | UART_FCR_ENABLE_FIFO);
        outb(com_ports[i].ioport + 4, 0x0B);

        if (inb(com_ports[i].ioport + UART_LSR) == 0xFF) {
            kp(KP_WARNING, "COM%d not found\n", i);
            com_ports[i].exists = 0;
            continue;
        }
    }

    irq_restore(irq_flags);
}

static void com_init_ports_rx(void)
{
    size_t i;
    irq_flags_t irq_flags;

    irq_flags = irq_save();
    irq_disable();

    for (i = 0; i < ARRAY_SIZE(com_ports); i++) {
        if (!com_ports[i].exists)
            continue;

        /* All that's left to do here is register the RX interrupt */

        int err = irq_register_handler(com_ports[i].irq, &com_ports[i].handler);
        if (err) {
            kp(KP_ERROR, "COM%d: Interrupt %d is already taken!\n", i, com_ports[i].irq);
            continue;
        }

        outb(com_ports[i].ioport + UART_IER, UART_IER_RDI);

        /* Clear interrupt registers after we enable them. */
        inb(com_ports[i].ioport + UART_LSR);
        inb(com_ports[i].ioport + UART_RX);
        inb(com_ports[i].ioport + UART_IIR);
        inb(com_ports[i].ioport + UART_MSR);
    }

    irq_restore(irq_flags);
}

static int com_tty_write(struct tty *tty, const char *buf, size_t len)
{
    struct com_port *com = com_ports + DEV_MINOR(tty->device_no);

    size_t i;
    for (i = 0; i < len; i++) {
        while (!com_write_ready(com))
            ;

        outb(com->ioport + UART_TX, buf[i]);
    }

    return len;
}

static void com_tty_ops_init(struct tty *tty)
{
    struct com_port *com = com_ports + DEV_MINOR(tty->device_no);
    com->tty = tty;
}

static struct tty_ops ops = {
    .write = com_tty_write,
    .init = com_tty_ops_init,
};

static struct tty_driver driver = {
    .major = CHAR_DEV_SERIAL_TTY,
    .minor_start = 0,
    .minor_end = 1,
    .ops = &ops,
};

static void com_init(void)
{
    if (!com_init_was_early)
        com_init_ports_tx();

    com_init_ports_rx();
}
initcall_device(com, com_init);

static void com_tty_init(void)
{
    kp(KP_TRACE, "COM TTY INIT\n");
    tty_driver_register(&driver);
}
initcall_device(com_tty, com_tty_init);

int com_init_early(void)
{
    com_init_ports_tx();
    com_init_was_early = 1;

    return 0;
}

static spinlock_t com1_kp_sync = SPINLOCK_INIT();

static void com1_print(struct kp_output *output, const char *str)
{
    using_spinlock(&com1_kp_sync) {
        for (; *str; str++) {
            while (!com_write_ready(com_ports + COM1))
                ;

            outb(com_ports[COM1].ioport + UART_TX, *str);
        }
    }
}

static struct kp_output_ops com1_kp_output_ops = {
    .print = com1_print,
};

static struct kp_output com_kp_output = KP_OUTPUT_INIT(com_kp_output, KP_NORMAL, "com1", &com1_kp_output_ops);
KPARAM("com1.loglevel", &com_kp_output.max_level, KPARAM_LOGLEVEL);

void com_kp_register(void)
{
    if (com_ports[COM1].exists)
        kp_output_register(&com_kp_output);
}

void com_kp_unregister(void)
{
    if (com_ports[COM1].exists)
        kp_output_unregister(&com_kp_output);
}
