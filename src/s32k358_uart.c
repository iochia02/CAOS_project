/*
 * ARM CMSDK APB UART emulation
  * S32K358 LPUART emulation
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
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "hw/char/cmsdk-apb-uart.h"
#include "hw/char/s32k358_uart.h"
#include "hw/irq.h"
#include "hw/qdev-properties-system.h"

REG32(VERID, 0x0)
REG32(PARAM, 0x4)
REG32(GLOBAL, 0x8)
    FIELD(GLOBAL, RST, 1, 1)
REG32(BAUD, 0x10)
    FIELD(BAUD, SBR, 0, 12)
    FIELD(BAUD, SBNS, 13, 1)
    FIELD(BAUD, RXEDGIE, 14, 1)
    FIELD(BAUD, LBKDIE, 15, 1)
    FIELD(BAUD, OSR, 24, 5)
REG32(STAT, 0x14)
    FIELD(STAT, PF, 16, 1)
    FIELD(STAT, TC, 22, 1)
    FIELD(STAT, RAF, 24, 1)
    FIELD(STAT, MSBF, 29, 1)
    FIELD(STAT, RXEDGIF, 30, 1)
    FIELD(STAT, LBKDIF, 31, 1)
REG32(CTRL, 0x18)
    FIELD(CTRL, PE, 1, 1)
    FIELD(CTRL, RE, 18, 1)
    FIELD(CTRL, TE, 19, 1)
    FIELD(CTRL, ILIE, 20, 1)
    FIELD(CTRL, RIE, 21, 1)
    FIELD(CTRL, TCIE, 22, 1)
    FIELD(CTRL, TIE, 23, 1)
    FIELD(CTRL, PEIE, 24, 1)
REG32(DATA, 0x1C)
    FIELD(DATA, R0T0, 0, 1)
    FIELD(DATA, R1T1, 1, 1)
    FIELD(DATA, R2T2, 2, 1)
    FIELD(DATA, R3T3, 3, 1)
    FIELD(DATA, R4T4, 4, 1)
    FIELD(DATA, R5T5, 5, 1)
    FIELD(DATA, R6T6, 6, 1)
    FIELD(DATA, R7T7, 7, 1)
    FIELD(DATA, R8T8, 8, 1)
    FIELD(DATA, R9T9, 9, 1)
    FIELD(DATA, PARITYE, 14, 1)
REG32(FIFO, 0x28)
    FIELD(FIFO, RXFIFOSIZE, 0, 3)
    FIELD(FIFO, RXFE, 3, 1)
    FIELD(FIFO, TXFIFOSIZE, 4, 3)
    FIELD(FIFO, TXFE, 7, 1)
    FIELD(FIFO, RXFLUSH, 14, 1)
    FIELD(FIFO, TXFLUSH, 15, 1)

/*
REG32(STATE, 4)
    FIELD(STATE, TXFULL, 0, 1)
    FIELD(STATE, RXFULL, 1, 1)
    FIELD(STATE, TXOVERRUN, 2, 1)
    FIELD(STATE, RXOVERRUN, 3, 1)
REG32(CTRL, 8)
    FIELD(CTRL, TX_EN, 0, 1)
    FIELD(CTRL, RX_EN, 1, 1)
    FIELD(CTRL, TX_INTEN, 2, 1)
    FIELD(CTRL, RX_INTEN, 3, 1)
    FIELD(CTRL, TXO_INTEN, 4, 1)
    FIELD(CTRL, RXO_INTEN, 5, 1)
    FIELD(CTRL, HSTEST, 6, 1)
REG32(INTSTATUS, 0xc)
    FIELD(INTSTATUS, TX, 0, 1)
    FIELD(INTSTATUS, RX, 1, 1)
    FIELD(INTSTATUS, TXO, 2, 1)
    FIELD(INTSTATUS, RXO, 3, 1)
REG32(BAUDDIV, 0x10)
REG32(PID4, 0xFD0)
REG32(PID5, 0xFD4)
REG32(PID6, 0xFD8)
REG32(PID7, 0xFDC)
REG32(PID0, 0xFE0)
REG32(PID1, 0xFE4)
REG32(PID2, 0xFE8)
REG32(PID3, 0xFEC)
REG32(CID0, 0xFF0)
REG32(CID1, 0xFF4)
REG32(CID2, 0xFF8)
REG32(CID3, 0xFFC)
*/
/* PID/CID values */

static const int uart_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x21, 0xb8, 0x1b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};


static bool uart_baudrate_ok(S32K358LPUART *s)
{
    /* The minimum permitted bauddiv setting is 16, so we just ignore
     * settings below that (usually this means the device has just
     * been reset and not yet programmed).
     */
    return s->bauddiv >= 16 && s->bauddiv <= s->pclk_frq;
}

static void uart_update_parameters(S32K358LPUART *s)
{
    QEMUSerialSetParams ssp;

    /* This UART is always 8N1 but the baud rate is programmable. */
    if (!uart_baudrate_ok(s)) {
        return;
    }

    ssp.data_bits = 8;
    ssp.parity = 'N';
    ssp.stop_bits = 1;
    // Computation at page 4618: baud_rate = clock / ((OSR+1) * SBR)
    if ((s->bauddiv & R_BAUD_SBR_MASK))
        ssp.speed = s->pclk_frq / (((s->bauddiv & R_BAUD_OSR_MASK) + 1) * (s->bauddiv & R_BAUD_SBR_MASK));
    else
        ssp.speed = s->pclk_frq;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
    trace_s32k358_lpuart_set_params(ssp.speed);
}

static void s32k358_lpuart_update(S32K358LPUART *s)
{
    /* update outbound irqs, including handling the way the rxo and txo
     * interrupt status bits are just logical AND of the overrun bit in
     * STATE and the overrun interrupt enable bit in CTRL.
     */
    uint32_t omask = (R_INTSTATUS_RXO_MASK | R_INTSTATUS_TXO_MASK);
    s->intstatus &= ~omask;
    s->intstatus |= (s->state & (s->ctrl >> 2) & omask);

    qemu_set_irq(s->txint, !!(s->intstatus & R_INTSTATUS_TX_MASK));
    qemu_set_irq(s->rxint, !!(s->intstatus & R_INTSTATUS_RX_MASK));
    qemu_set_irq(s->txovrint, !!(s->intstatus & R_INTSTATUS_TXO_MASK));
    qemu_set_irq(s->rxovrint, !!(s->intstatus & R_INTSTATUS_RXO_MASK));
    qemu_set_irq(s->uartint, !!(s->intstatus));
}

static int uart_can_receive(void *opaque)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);

    /* We can take a char if RX is enabled and the buffer is empty */
    if (s->ctrl & R_CTRL_RX_EN_MASK && !(s->state & R_STATE_RXFULL_MASK)) {
        return 1;
    }
    return 0;
}

static void uart_receive(void *opaque, const uint8_t *buf, int size)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);

    trace_s32k358_lpuart_receive(*buf);

    /* In fact uart_can_receive() ensures that we can't be
     * called unless RX is enabled and the buffer is empty,
     * but we include this logic as documentation of what the
     * hardware does if a character arrives in these circumstances.
     */
    if (!(s->ctrl & R_CTRL_RE_MASK)) {
        /* Just drop the character on the floor */
        return;
    }

    if (s->state & R_STATE_RXFULL_MASK) {
        s->state |= R_STATE_RXOVERRUN_MASK;
    }

    s->rxbuf = *buf;
    s->state |= R_STATE_RXFULL_MASK;
    if (s->ctrl & R_CTRL_RX_INTEN_MASK) {
        s->intstatus |= R_INTSTATUS_RX_MASK;
    }
    s32k358_lpuart_update(s);
}

static uint64_t uart_read(void *opaque, hwaddr offset, unsigned size)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);
    uint64_t r;

    switch (offset) {
    case A_DATA:
        r = s->rxbuf;
        s->state &= ~R_STATE_RXFULL_MASK;
        s32k358_lpuart_update(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case A_STATE:
        r = s->state;
        break;
    case A_CTRL:
        r = s->ctrl;
        break;
    case A_INTSTATUS:
        r = s->intstatus;
        break;
    case A_BAUDDIV:
        r = s->bauddiv;
        break;
    case A_PID4 ... A_CID3:
        r = uart_id[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB UART read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }
    trace_s32k358_lpuart_read(offset, r, size);
    return r;
}

/* Try to send tx data, and arrange to be called back later if
 * we can't (ie the char backend is busy/blocking).
 */
static gboolean uart_transmit(void *do_not_use, GIOCondition cond, void *opaque)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);
    int ret;

    /* instant drain the fifo when there's no back-end */
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        s->tx_count = 0;
        return G_SOURCE_REMOVE;
    }

    // Verify that the receiver is enabled
    if (!(s->ctrl & R_CTRL_TE_MASK)) {
        return G_SOURCE_REMOVE;
    }

    if (!s->tx_count) {
        return G_SOURCE_REMOVE;
    }

    ret = qemu_chr_fe_write(&s->chr, s->tx_fifo, s->tx_count);

    if (ret >= 0) {
        s->tx_count -= ret;
        memmove(s->tx_fifo, s->tx_fifo + ret, s->tx_count);
    }

    if (s->tx_count) {
        guint r = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                        cadence_uart_xmit, s);
        if (!r) {
            s->tx_count = 0;
            return G_SOURCE_REMOVE;
        }
    }

    uart_update_status(s);
    return G_SOURCE_REMOVE;
    }

buffer_drained:
    /* Character successfully sent */
    trace_s32k358_lpuart_tx(s->txbuf);
    s->state &= ~R_STATE_TXFULL_MASK;
    /* Going from TXFULL set to clear triggers the tx interrupt */
    if (s->ctrl & R_CTRL_TX_INTEN_MASK) {
        s->intstatus |= R_INTSTATUS_TX_MASK;
    }
    s32k358_lpuart_update(s);
    return G_SOURCE_REMOVE;
}

static void uart_cancel_transmit(S32K358LPUART *s)
{
    if (s->watch_tag) {
        g_source_remove(s->watch_tag);
        s->watch_tag = 0;
    }
}

static void uart_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);

    trace_s32k358_lpuart_write(offset, value, size);

    switch (offset) {
    case A_VERID:
        qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: VERID is a read-only register\n");
        break;
    case A_PARAM:
        qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: PARAM is a read-only register\n");
        break;
    case A_GLOBAL:
        // setting the RST bit to 1 triggers the reset of all registers but global
        s->global &= !R_GLOBAL_RST_MASK;
        s->global |= value & R_GLOBAL_RST_MASK;
        if (s->global & R_GLOBAL_RST_MASK) {
            s->verid = 0x04040007;
            s->param = 0x00000404;
            s->baud = 0x0F000004;
            s->stat = 0x00C00000;
            s->ctrl = 0;
            s->data = 0x00001000;
            s->fifo = 0x00C00033;
            s->rx_fifo = {0};
            s->tx_fifo = {0};
        }
        break;
    case A_BAUD:
        if (value & R_BAUD_SBR_MASK)
            s32k358_lpuart_update(s);
        // Disable receiver and transmitter
        if ((s->ctrl & R_CTRL_RE_MASK) == 0 || (s->ctrl & R_CTRL_TE_MASK) == 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: to change the baud register transmitter and receiver must be disabled.\n");
                break;
        }
        if (value & R_BAUD_SBNS_MASK) {
            s->baud &= ! R_BAUD_SBNS_MASK;
            s->baud |= (value & R_BAUD_SBNS_MASK);
        }
        if (value & R_BAUD_RXEDGIE_MASK) {
            s->baud &= ! R_BAUD_RXEDGIE_MASK;
            s->baud |= (value & R_BAUD_RXEDGIE_MASK);
        }
        if (value & R_BAUD_LBKDIE_MASK) {
            s->baud &= ! R_BAUD_LBKDIE_MASK;
            s->baud |= (value & R_BAUD_LBKDIE_MASK);
        }
        if (value & R_BAUD_OSR_MASK) {
            if ((value & R_BAUD_OSR_MASK) == 0x1b || (value & R_BAUD_OSR_MASK) == 0x10b) {
                qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: OSR 0x1b and 0x10b values are reserved\n");
                break;
            }
            s->baud &= ! R_BAUD_OSR_MASK;
            s->baud |= (value & R_BAUD_OSR_MASK);
        }
        break;
    case A_STAT:
        // If value & R_STAT_PF_MASK = 1 clear the flag, otherwise do nothing
        if (value & R_STAT_PF_MASK)
            s->stat &= ! R_STAT_PF_MASK;

        if ((value & R_STAT_TC_MASK) || (value & R_STAT_RAF_MASK)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: STAT TC and RAF are readonly\n");
                break;
        }

        // Check other registers

        break;
    case A_CTRL:
        // enable/disable parity bit
        s->ctrl &= ! R_CTRL_PE_MASK;
        s->ctrl |= (value & R_CTRL_PE_MASK);

        // enable/disable receiver (see how to make it be 1 till the receive is completed)
        s->ctrl &= ! R_CTRL_RE_MASK;
        s->ctrl |= (value & R_CTRL_RE_MASK);

        // enable/disable transmitter
        s->ctrl &= ! R_CTRL_TE_MASK;
        s->ctrl |= (value & R_CTRL_TE_MASK);

        s->ctrl &= ! R_CTRL_ILIE_MASK;
        s->ctrl |= (value & R_CTRL_ILIE_MASK);

        s->ctrl &= ! R_CTRL_RIE_MASK;
        s->ctrl |= (value & R_CTRL_RIE_MASK);

        s->ctrl &= ! R_CTRL_TCIE_MASK;
        s->ctrl |= (value & R_CTRL_TCIE_MASK);

        s->ctrl &= ! R_CTRL_TIE_MASK;
        s->ctrl |= (value & R_CTRL_TIE_MASK);

        s->ctrl &= ! R_CTRL_PEIE_MASK;
        s->ctrl |= (value & R_CTRL_PEIE_MASK);
        break;
    case A_DATA:
        for(int i = 0; i < 10; i++) {
            s->data &= ! R_DATA_R0T0_MASK << i;
            s->data |= (value & (R_DATA_R0T0_MASK << i));
        }
        break;

    case A_FIFO:
        s->fifo &= ! R_FIFO_RXFE_MASK;
        s->fifo |= (value & R_FIFO_RXFE_MASK);

        s->fifo &= ! R_FIFO_TXFE_MASK;
        s->fifo |= (value & R_FIFO_TXFE_MASK);

        if (value & R_FIFO_RXFLUSH_MASK) {
            rx_fifo = {0};
        }

        if (value & R_FIFO_TXFLUSH_MASK) {
            tx_fifo = {0};
        }

        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "S32K358 LPUART write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps uart_ops = {
    .read = uart_read,
    .write = uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void s32k358_lpuart_reset(DeviceState *dev)
{
    S32K358LPUART *s = S32K358_LPUART(dev);

    uart_cancel_transmit(s);
    // TODO: implement values for lpuart 2...15
    s->verid = 0x04040007;
    s->param = 0x00000404;
    s->global = 0;
    s->baud = 0x0F000004;
    s->stat = 0x00C00000;
    s->ctrl = 0;
    s->data = 0x00001000;
    s->fifo = 0x00C00033;
    s->rx_fifo = {0};
    s->tx_fifo = {0};
}

static void s32k358_lpuart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    S32K358LPUART *s = S32K358_LPUART(obj);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s, "uart", 0x0800);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->uartint);
}

static void s32k358_lpuart_realize(DeviceState *dev, Error **errp)
{
    S32K358LPUART *s = S32K358_LPUART(dev);

    if (s->pclk_frq == 0) {
        error_setg(errp, "S32K358 LPUART: pclk-frq property must be set");
        return;
    }

    /* Flow control not implemented
     */
    qemu_chr_fe_set_handlers(&s->chr, uart_can_receive, uart_receive,
                             NULL, NULL, s, NULL, true);
}

static int s32k358_lpuart_post_load(void *opaque, int version_id)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);

    /* If we have a pending character, arrange to resend it. */
    if (s->state & R_STATE_TXFULL_MASK) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                             uart_transmit, s);
    }
    uart_update_parameters(s);
    return 0;
}

static const VMStateDescription s32k358_lpuart_vmstate = {
    .name = "s32k358-lpuart",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = s32k358_lpuart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk_frq, S32K358Timer),
        VMSTATE_UINT32(verid, S32K358LPUART),
        VMSTATE_UINT32(param, S32K358LPUART),
        VMSTATE_UINT32(global, S32K358LPUART),
        VMSTATE_UINT32(baud, S32K358LPUART),
        VMSTATE_UINT32(stat, S32K358LPUART),
        VMSTATE_UINT32(ctrl, S32K358LPUART),
        VMSTATE_UINT32(data, S32K358LPUART),
        VMSTATE_UINT32(fifo, S32K358LPUART),
        VMSTATE_UINT8_ARRAY(rxbuf, S32K358LPUART, S32K358_LPUART_RX_FIFO_SIZE),
        VMSTATE_UINT8_ARRAY(txbuf, S32K358LPUART, S32K358_LPUART_TX_FIFO_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static Property s32k358_lpuart_properties[] = {
    DEFINE_PROP_CHR("chardev", S32K358LPUART, chr),
    DEFINE_PROP_UINT32("pclk-frq", S32K358LPUART, pclk_frq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void s32k358_lpuart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s32k358_lpuart_realize;
    dc->vmsd = &s32k358_lpuart_vmstate;
    dc->reset = s32k358_lpuart_reset;
    device_class_set_props(dc, s32k358_lpuart_properties);
}

static const TypeInfo s32k358_lpuart_info = {
    .name = TYPE_S32K358_LPUART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32K358LPUART),
    .instance_init = s32k358_lpuart_init,
    .class_init = s32k358_lpuart_class_init,
};

static void s32k358_lpuart_register_types(void)
{
    type_register_static(&s32k358_lpuart_info);
}

type_init(s32k358_lpuart_register_types);
