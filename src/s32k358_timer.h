/*
 *  s32k358 PIT timer emulation
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

#ifndef S32K358_TIMER_H
#define S32K358_TIMER_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/clock.h"
#include "qom/object.h"

#define TYPE_S32K358_TIMER "s32k358-timer"
OBJECT_DECLARE_SIMPLE_TYPE(S32K358Timer, S32K358_TIMER)

/*
 * QEMU interface:
 *  + Clock input "pclk": clock for the timer
 *  + sysbus MMIO region 0: the register bank
 *  + sysbus IRQ 0: timer interrupt
 */

struct S32K358Timer;

// Data structure representing each timer's channel
struct sub_timer {
    struct S32K358Timer *parent;
    struct ptimer_state *timer;
    uint32_t ctrl;
    uint32_t flag;
};

// Data structure representing the PIT (periodic interrupt timer)
struct S32K358Timer {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq timer_irq;

    Clock *pclk;
    uint32_t timer_ctrl;
    struct sub_timer timers[4]; // the timer has 4 channels
};

#endif

