/*
 *  s32k358 PIT timer emulation
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/qdev-clock.h"
#include "hw/timer/s32k358_timer.h"
#include "migration/vmstate.h"

// Timer's registers: page 2816 manual
// PIT module control: enables the PIT timer clocks and specifies the behaviour of the timers when PIT enters Debug mode
REG32(MCR, 0)
    FIELD(MCR, FRZ, 0, 1) // freeze
    FIELD(MCR, MDIS, 1, 1) // module disable
    FIELD(MCR, MDIS_RTI, 2, 1) // module disable RTI

// The RIT and the chaining are not modeled, so we do not create the relative registers

// Timer load value (specifies the length of the timeout peiod in clock cycles)
REG32(LDVAL0, 0x100)
REG32(LDVAL1, 0x110)
REG32(LDVAL2, 0x120)
REG32(LDVAL3, 0x130)

// Current timer value (indicates the current timer value)
REG32(CVAL0, 0x104)
REG32(CVAL1, 0x114)
REG32(CVAL2, 0x124)
REG32(CVAL3, 0x134)

// Timer control (controls timer behaviour)
REG32(TCTRL0, 0x108)
    FIELD(TCTRL0, TEN, 0, 1) // timer enable
    FIELD(TCTRL0, TIE, 1, 1) // timer interrupt enable
    FIELD(TCTRL0, CHN, 2, 1) // chain mode
REG32(TCTRL1, 0x118)
    FIELD(TCTRL1, TEN, 0, 1)
    FIELD(TCTRL1, TIE, 1, 1)
    FIELD(TCTRL1, CHN, 2, 1)
REG32(TCTRL2, 0x128)
    FIELD(TCTRL2, TEN, 0, 1)
    FIELD(TCTRL2, TIE, 1, 1)
    FIELD(TCTRL2, CHN, 2, 1)
REG32(TCTRL3, 0x138)
    FIELD(TCTRL3, TEN, 0, 1)
    FIELD(TCTRL3, TIE, 1, 1)
    FIELD(TCTRL3, CHN, 2, 1)

// Timer flag (indicates the PIT had expired)
REG32(TFLG0, 0x10C)
    FIELD(TFLG0, TIF, 0, 1) // timer interrupt flag
REG32(TFLG1, 0x11C)
    FIELD(TFLG1, TIF, 0, 1)
REG32(TFLG2, 0x12C)
    FIELD(TFLG2, TIF, 0, 1)
REG32(TFLG3, 0x13C)
    FIELD(TFLG3, TIF, 0, 1)

static void s32k358_irq_update(S32K358Timer *s)
{
    // check if timer is enabled (0 = enabled)
    if ((s->timer_ctrl & R_MCR_MDIS_MASK)) {
        // since it is not, clear the interrupt
        qemu_irq_lower(s->timer_irq);
        return;
    }

    // If at least one timer is enabled, has interrupts enabled and the timer interrupt flag set, raise the interrupt
    for (int i = 0; i < ARRAY_SIZE(s->timers); i++) {
        if ((s->timers[i].ctrl & R_TCTRL0_TEN_MASK) &&
             (s->timers[i].ctrl & R_TCTRL0_TIE_MASK) &&
             (s->timers[i].flag & R_TFLG0_TIF_MASK)) {
                qemu_irq_raise(s->timer_irq);
                return;
        }
    }

    // clear the interrupt
    qemu_irq_lower(s->timer_irq);
}

// Function that allows to read the registers' values
static uint64_t s32k358_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    S32K358Timer *s = S32K358_TIMER(opaque);
    uint64_t r;

    switch (offset) {
        // Pit Module Control (MCR)
        case A_MCR:
            r = s->timer_ctrl;
            break;
        // Timer Control (TCTRL0, TCTRL1, TCTRL2, TCTRL3)
        case A_TCTRL0:
        case A_TCTRL1:
        case A_TCTRL2:
        case A_TCTRL3:
            r = s->timers[(offset-A_TCTRL0) / (A_TCTRL1-A_TCTRL0)].ctrl;
            break;
        // Current Timer Value (CVAL0, CVAL1, CVAL2, CVAL3)
        case A_CVAL0:
        case A_CVAL1:
        case A_CVAL2:
        case A_CVAL3:
            r = ptimer_get_count(s->timers[(offset-A_CVAL0) / (A_CVAL1-A_CVAL0)].timer);
            break;
        // Timer Load Value (LDVAL0, LDVAL1, LDVAL2, LDVAL3)
        case A_LDVAL0:
        case A_LDVAL1:
        case A_LDVAL2:
        case A_LDVAL3:
            r = ptimer_get_limit(s->timers[(offset-A_LDVAL0) / (A_LDVAL1-A_LDVAL0)].timer);
            break;
        // Timer Flag (TFLG0, TFLG1, TFLG2, TFLG3)
        case A_TFLG0:
        case A_TFLG1:
        case A_TFLG2:
        case A_TFLG3:
            r = s->timers[(offset-A_TFLG0) / (A_TFLG1-A_TFLG0)].flag;
            break;
        default: {
            qemu_log_mask(LOG_GUEST_ERROR,
                        "CMSDK APB timer read: bad offset %x\n", (int) offset);
            r = 0;
            break;
        }
    }

    return r;
}

// To switch on/off the timer (since it may be enabled/disabled through the s32k358_timer_write funtion)
static void s32k358_timer_switch_on_off(S32K358Timer *s, uint32_t idx) {
    ptimer_transaction_begin(s->timers[idx].timer);
    if ((s->timers[idx].ctrl & R_TCTRL0_TEN_MASK) && !(s->timer_ctrl & R_MCR_MDIS_MASK)) {
        ptimer_run(s->timers[idx].timer, 0 /* reloadable timer */);
    } else {
        ptimer_stop(s->timers[idx].timer);
    }
    ptimer_transaction_commit(s->timers[idx].timer);
}

static void s32k358_timer_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size)
{
    S32K358Timer *s = S32K358_TIMER(opaque);
    uint32_t idx;

    switch (offset) {
    // Pit Module Control (MCR)
    case R_MCR:
        // Write the first 3 bits, the others are reserved
        s->timer_ctrl = value & (R_MCR_FRZ_MASK | R_MCR_MDIS_MASK | R_MCR_MDIS_RTI_MASK);
        // Changing the FRZ and the RTI will have no effect
        if (value & (R_MCR_FRZ_MASK | R_MCR_MDIS_RTI_MASK)) {
            // We don't model this.
            qemu_log_mask(LOG_UNIMP,
                          "S32K358 timer: FRZ and MDIS_RTI input not supported\n");
        }

        // If enable the PIT, reset the four timers
        for (idx = 0; idx < ARRAY_SIZE(s->timers); idx++)
            s32k358_timer_switch_on_off(s, idx);

        break;
    // Timer Control (TCTRL0, TCTRL1, TCTRL2, TCTRL3)
    case A_TCTRL0:
    case A_TCTRL1:
    case A_TCTRL2:
    case A_TCTRL3:
        // derive the channel
        idx = (offset-A_TCTRL0) / (A_TCTRL1-A_TCTRL0);
        // modify only the last three bits, the others are reserved
        s->timers[idx].ctrl = value & (R_TCTRL0_CHN_MASK | R_TCTRL0_TEN_MASK | R_TCTRL0_TIE_MASK);
        // Changing the CHN will have no effect
        if (value & R_TCTRL0_CHN_MASK) {
            // We don't model this.
            qemu_log_mask(LOG_UNIMP,
                          "S32K358 timer: CHN input not supported\n");
        }

        // if TIF is enabled, changing the timer interrupt enable triggers an interupt (manual - page 2830)
        s32k358_irq_update(s);
        // If enable the timer, reset it
        s32k358_timer_switch_on_off(s, idx);

        break;
    // Timer Flag (TFLG0, TFLG1, TFLG2, TFLG3)
    case A_TFLG0:
    case A_TFLG1:
    case A_TFLG2:
    case A_TFLG3:
        value &= R_TFLG0_TIF_MASK;
        s->timers[(offset-A_TFLG0) / (A_TFLG1-A_TFLG0)].flag &= ~value;
        // if TIF=1, it may have to trigger the interrupt
        s32k358_irq_update(s);
        break;
     // Timer Load Value (LDVAL0, LDVAL1, LDVAL2, LDVAL3)
    case A_LDVAL0:
    case A_LDVAL1:
    case A_LDVAL2:
    case A_LDVAL3:
        idx = (offset-A_LDVAL0) / (A_LDVAL1-A_LDVAL0);

        ptimer_transaction_begin(s->timers[idx].timer);
        // Do not reload immediately the timer, wait that it expires before loading the new value
        // Hence, change the reload value but not the CVAL one
        ptimer_set_limit(s->timers[idx].timer, value, 0);
        // Check if the pit and line of the timer are enabled
        // R_TCTRL0_TEN_MASK is the same for all the registers
        if (!(s->timer_ctrl & R_MCR_MDIS_MASK) && (s->timers[idx].ctrl & R_TCTRL0_TEN_MASK)) {
            // Make sure timer is running (it doesn't reset it if it was already)
            ptimer_run(s->timers[idx].timer, 0 /* reloadable timer */);
        }
        ptimer_transaction_commit(s->timers[idx].timer);
        break;
    // Current Timer Value (CVAL0, CVAL1, CVAL2, CVAL3)
    case A_CVAL0:
    case A_CVAL1:
    case A_CVAL2:
    case A_CVAL3:
        // It is a read-only register
        qemu_log_mask(LOG_GUEST_ERROR,
                      "S32K358 timer write: write to Read-Only offset 0x%x\n",
                      (int)offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "S32K358 timer write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps s32k358_timer_ops = {
    .read = s32k358_timer_read,
    .write = s32k358_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

// Function called when the timer expires
static void s32k358_timer_tick(void *opaque)
{
    struct sub_timer *st = (struct sub_timer*)opaque;

    // set interrupt flag (always)
    st->flag |= R_TFLG0_TIF_MASK;
    // if interrupts are enabled trigger interrupt
    if (st->ctrl & R_TCTRL0_TIE_MASK) {
        qemu_irq_raise(st->parent->timer_irq);
    }
}

static void s32k358_timer_reset(DeviceState *dev)
{
    S32K358Timer *s = S32K358_TIMER(dev);

    for (int i = 0; i < ARRAY_SIZE(s->timers); i++) {
        /* Set the ctrl and tif */
        s->timers[i].ctrl = 0;
        s->timers[i].flag = 0;
        ptimer_transaction_begin(s->timers[i].timer);
        ptimer_stop(s->timers[i].timer);
        /* Set the limit */
        ptimer_set_limit(s->timers[i].timer, 0, 1);
        ptimer_transaction_commit(s->timers[i].timer);
    }
}

// Model the timer increment
static void s32k358_timer_clk_update(void *opaque, ClockEvent event)
{
    S32K358Timer *s = S32K358_TIMER(opaque);
    for (int i = 0; i < ARRAY_SIZE(s->timers); i++) {
        ptimer_transaction_begin(s->timers[i].timer);
        ptimer_set_period_from_clock(s->timers[i].timer, s->pclk, 1);
        ptimer_transaction_commit(s->timers[i].timer);
    }
}

static void s32k358_timer_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    S32K358Timer *s = S32K358_TIMER(obj);

    // Create the memory region in which the timers are memory-mapped
    memory_region_init_io(&s->iomem, obj, &s32k358_timer_ops,
                          s, "s32k358-timer", 0x140);
    sysbus_init_mmio(sbd, &s->iomem);
    // Connect the irq line and clock
    sysbus_init_irq(sbd, &s->timer_irq);
    s->pclk = qdev_init_clock_in(DEVICE(s), "pclk",
                                 s32k358_timer_clk_update, s, ClockUpdate);
}

static void s32k358_timer_realize(DeviceState *dev, Error **errp)
{
    S32K358Timer *s = S32K358_TIMER(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, "S32K358 timer: pclk clock must be connected");
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(s -> timers); i++) {
        s->timers[i].parent = s;
        // Init the four channels
        s->timers[i].timer = ptimer_init(s32k358_timer_tick, &(s->timers[i]),
                           PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                           PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

        ptimer_transaction_begin(s->timers[i].timer);
        ptimer_set_period_from_clock(s->timers[i].timer, s->pclk, 1);
        ptimer_transaction_commit(s->timers[i].timer);
    }
}

static const VMStateDescription s32k358_timer_vmstate = {
    .name = "s32k358-timer",
    .version_id = 2,
    .minimum_version_id = 2,
    // only public field, irq and pclk
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, S32K358Timer),
        VMSTATE_END_OF_LIST()
    }
};

static void s32k358_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32k358_timer_realize;
    dc->vmsd = &s32k358_timer_vmstate;
    dc->reset = s32k358_timer_reset;
}

static const TypeInfo s32k358_timer_info = {
    .name = TYPE_S32K358_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32K358Timer),
    .instance_init = s32k358_timer_init,
    .class_init = s32k358_timer_class_init,
};

static void s32k358_timer_register_types(void)
{
    type_register_static(&s32k358_timer_info);
}

type_init(s32k358_timer_register_types);

