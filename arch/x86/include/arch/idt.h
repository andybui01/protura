/*
 * Copyright (C) 2014 Matt Kilgore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 */
#ifndef INCLUDE_ARCH_IDT_H
#define INCLUDE_ARCH_IDT_H

#ifndef ASM

#include <protura/compiler.h>
#include <protura/types.h>

#define DPL_USER 3
#define DPL_KERNEL 0

#define STS_TG32 0xF
#define STS_IG32 0xE

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  type :4;
    uint8_t  s :1;
    uint8_t  dpl :2;
    uint8_t  p :1;
    uint16_t  base_high;
} __packed;

#define IDT_SET_ENT(ent, istrap, cs, off, d) do { \
        (ent).base_low = (uint32_t)(off) & 0xFFFF; \
        (ent).base_high = ((uint32_t)(off) >> 16) & 0xFFFF; \
        (ent).sel = cs; \
        (ent).zero = 0; \
        (ent).type = (istrap) ? STS_TG32 : STS_IG32; \
        (ent).s = 0; \
        (ent).dpl = (d); \
        (ent).p = 1; \
    } while (0)

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __packed;

struct idt_frame {
    uint32_t edi, esi, ebp, oesp /* Ignore */, ebx, edx, ecx, eax;

    uint16_t gs, pad1, fs, pad2, es, pad3, ds, pad4;

    uint32_t intno;
    uint32_t err;

    uint32_t eip;
    uint16_t cs, padding5;
    uint32_t eflags;

    uint32_t esp;
    uint16_t ss, padding6;
} __packed;

void idt_flush(uint32_t);

void idt_init(void);

void isr_handler(struct idt_frame *);

#endif

#endif
