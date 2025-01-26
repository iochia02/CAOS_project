/*
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
#include "hw/char/s32k358_uart.h"
#include "hw/irq.h"
#include "hw/qdev-properties-system.h"

REG32(VERID, 0x0) // Indicates the version integrated for this instance
REG32(PARAM, 0x4) // Indicates the parameter configuration for this instance on the chi
REG32(GLOBAL, 0x8) // Performs global functions
    FIELD(GLOBAL, RST, 1, 1)
REG32(BAUD, 0x10) // Configures the baud rate
    FIELD(BAUD, SBR, 0, 13) // Baud Rate Modulo Divisor
    FIELD(BAUD, SBNS, 13, 1) // Stop Bit Number Select
    FIELD(BAUD, BOTHEDGE, 17, 1) // Both Edge Sampling
    FIELD(BAUD, OSR, 24, 5) // Oversampling Ratio
REG32(STAT, 0x14) // Provides the module status.
    FIELD(STAT, RDRF, 21, 1) // Receive Data Register Full Flag
    FIELD(STAT, TC, 22, 1) // Transmission Complete Flag
    FIELD(STAT, TDRE, 23, 1) // Transmit Data Register Empty Flag
REG32(CTRL, 0x18) // Controls various optional features of the LPUART system.
    FIELD(CTRL, PT, 0, 1) // Parity Type
    FIELD(CTRL, PE, 1, 1) // Parity Enable
    FIELD(CTRL, RE, 18, 1) // Receiver Enable
    FIELD(CTRL, TE, 19, 1) // Transmitter Enable
    FIELD(CTRL, RIE, 21, 1) // Receiver Interrupt Enable
    FIELD(CTRL, TCIE, 22, 1) // Transmission Complete Interrupt Enable
    FIELD(CTRL, TIE, 23, 1) // Transmit Interrupt Enable
REG32(DATA, 0x1C)  // Read receive FIFO bits 0-7 or write transmit FIFO bit 0-7
    FIELD(DATA, R07T07, 0, 8)
REG32(FIFO, 0x28) // Provides you the ability to turn on and turn off the FIFO functionality.
    FIELD(FIFO, RXFIFOSIZE, 0, 3) // Receive FIFO Buffer Depth
    FIELD(FIFO, RXFE, 3, 1) // Receive FIFO Enable
    FIELD(FIFO, TXFIFOSIZE, 4, 3) // Transmit FIFO Buffer Depth
    FIELD(FIFO, TXFE, 7, 1) // Transmit FIFO Enable
    FIELD(FIFO, RXUFE, 8, 1) // Receive FIFO Underflow Interrupt Enable
    FIELD(FIFO, TXOFE, 9, 1) // Transmit FIFO Overflow Interrupt Enable
    FIELD(FIFO, RXFLUSH, 14, 1) // Receive FIFO Flush
    FIELD(FIFO, TXFLUSH, 15, 1) // Transmit FIFO Flush
    FIELD(FIFO, RXUF, 16, 1) // Receiver FIFO Underflow Flag
    FIELD(FIFO, TXOF, 17, 1) // Transmitter FIFO Overflow Flag
    FIELD(FIFO, RXEMPT, 22, 1) // Receive FIFO Or Buffer Empty
    FIELD(FIFO, TXEMPT, 23, 1) // Transmit FIFO Or Buffer Empty
REG32(WATER, 0x2C) // Provides the ability to set a programmable threshold for notification, or sets the programmable thresholds to indicate that transmit data can be written or receive data can be read.
    FIELD(WATER, TXWATER, 0, 4) // Transmit Watermark
    FIELD(WATER, TXWATER_SHORT, 0, 2) // Transmit Watermark
    FIELD(WATER, TXCOUNT, 8, 5) // Transmit Counter
    FIELD(WATER, RXWATER, 16, 4) // Receive Watermark
    FIELD(WATER, RXWATER_SHORT, 16, 2) // Receive Watermark
    FIELD(WATER, RXCOUNT, 24, 5) // Receive Counter

// Update the configuration of the UART
static void lpuart_update_parameters(S32K358LPUART *s)
{
    QEMUSerialSetParams ssp;

    uint8_t osr = (s->baud & R_BAUD_OSR_MASK) >> R_BAUD_OSR_SHIFT;

    if (!osr)
        osr = 15;

    // Configure the parity bit
    if (s->ctrl & R_CTRL_PE_MASK) {
        if (s->ctrl & R_CTRL_PT_MASK) {
            ssp.parity = 'O';
        } else {
            ssp.parity = 'E';
        }
    } else {
        ssp.parity = 'N';
    }

    // By default, the data size is always 8
    ssp.data_bits = 8;

    // Configure one or two stop bits
    if (!(s->baud & R_BAUD_SBNS_MASK))
        ssp.stop_bits = 1;
    else
        ssp.stop_bits = 2;

    // Configure the baud rate
    // Computation at page 4618 of the reference manual: baud_rate = clock / ((OSR+1) * SBR)
    if ((s->baud & R_BAUD_SBR_MASK))
        ssp.speed = s->pclk_frq / ((((s->baud & R_BAUD_OSR_MASK) >> R_BAUD_OSR_SHIFT) + 1) * (s->baud & R_BAUD_SBR_MASK));
    else
        ssp.speed = s->pclk_frq;

    //  Issue a device specific ioctl to a backend.  This function is thread-safe.
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
}

// Check if the FIFO level is higher, equal or lower than the watermark and update the flags
static void lpuart_update_watermark(S32K358LPUART *s)
{
    if (s->tx_fifo_written > s->tx_fifo_watermark)
        s->stat &= ~R_STAT_TDRE_MASK;
    else
        s->stat |= R_STAT_TDRE_MASK;

    if (s->rx_fifo_written > s->rx_fifo_watermark)
        s->stat |= R_STAT_RDRF_MASK;
    else
        s->stat &= ~R_STAT_RDRF_MASK;
}

// Set the IRQ if necessary
static void lpuart_update_irq(S32K358LPUART *s)
{
    if (((s->ctrl & R_CTRL_TIE_MASK) && (s->stat & R_STAT_TDRE_MASK)) || // there is room in the transmit FIFO to write another transmit character to Data
        ((s->ctrl & R_CTRL_TCIE_MASK) && (s->stat & R_STAT_TC_MASK)) || // the transmitter is finished transmitting all data and is idle
        ((s->ctrl & R_CTRL_RIE_MASK) && (s->stat & R_STAT_RDRF_MASK)) || // the receive FIFO level is greater than the watermark
        ((s->fifo & R_FIFO_TXOFE_MASK) && (s->fifo & R_FIFO_TXOF_MASK)) || // transmitter FIFO overflow
        ((s->fifo & R_FIFO_RXUFE_MASK) && (s->fifo & R_FIFO_RXUF_MASK))) // receiver FIFO underflow
         qemu_set_irq(s->uartint, 1);

    else
        qemu_set_irq(s->uartint, 0);
}

static void lpuart_reset(DeviceState *dev)
{
    S32K358LPUART *s = S32K358_LPUART(dev);

    // reset values for lpuart0 and lpuart1
    if (s->id < 2) {
        s->verid = 0x04040007;
        s->param = 0x00000404;
        s->fifo = 0x00C00033;
    } else { // reset values for lpuart2 ... lpuart15
        s->verid = 0x04040003;
        s->param = 0x00000202;
        s->fifo = 0x00C00011;
    }
    s->global = 0;
    s->baud = 0x0F000004;
    s->stat = 0x00C00000;
    s->ctrl = 0;
    s->data = 0x00001000;
    s->tx_fifo_written = 0;
    s->rx_fifo_written = 0;
    s->rx_fifo_watermark = 0;
    s->tx_fifo_watermark = 0;
    // Fifo is disabled
    s->tx_fifo_size = 1;
    s->rx_fifo_size = 1;

    lpuart_update_parameters(s);
    lpuart_update_irq(s);
}

static int lpuart_can_receive(void *opaque)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);

    // Check that the receiver is enabled
    if (!(s->ctrl & R_CTRL_RE_MASK))
        return 0;

    // Returns the amount of data that the frontend can receive
    return s->rx_fifo_size - s->rx_fifo_written;
}

static void lpuart_receive(void *opaque, const uint8_t *buf, int size)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);

    /* In fact lpuart_can_receive() ensures that we can't be
      * called unless RX is enabled and the buffer is empty,
      * but we include this logic as documentation of what the
      * hardware does if a character arrives in these circumstances.
      */
    if (!(s->ctrl & R_CTRL_RE_MASK)) {
        // Just drop the character on the floor
        return;
    }

    // Copy the buffer into the receive fifo
    memcpy(s->rx_fifo + s->rx_fifo_written, buf, 1);
    s->rx_fifo_written++;
    // the receive fifo is no more empty
    s->fifo &= ~R_FIFO_RXEMPT_MASK;

    lpuart_update_watermark(s);
    lpuart_update_irq(s);
}

static void lpuart_read_rx_fifo(S32K358LPUART *s) {
    /* We tried to read from an empty receive FIFO:
     * set the underflow flag and return
     */

    if (s->rx_fifo_written == 0) {
        s->fifo |= R_FIFO_RXUF_MASK;
        lpuart_update_irq(s);
        return;
    }

    // Copy the first byte from the receive FIFO to the data register (to be read by the user application)
    s->data &= ~R_DATA_R07T07_MASK;
    s->data |= s->rx_fifo[0];
    s->rx_fifo_written--;
    if (s->rx_fifo_written == 0)
        s->fifo |= R_FIFO_RXEMPT_MASK;
    else // Update the fifo
        memmove(s->rx_fifo, s->rx_fifo + 1, s->rx_fifo_written);

    lpuart_update_watermark(s);
    lpuart_update_irq(s);
}

// Return the value of the requested register
static uint64_t lpuart_read(void *opaque, hwaddr offset, unsigned size)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);
    uint64_t r;

    switch (offset) {
    case A_BAUD:
        r = s->baud;
        break;
    case A_CTRL:
        r = s->ctrl;
        break;
    case A_DATA:
        lpuart_read_rx_fifo(s);
        r = s->data;
        break;
    case A_FIFO:
        r = s->fifo;
        break;
    case A_GLOBAL:
        r = s->global;
        break;
    case A_PARAM:
        r = s->param;
        break;
    case A_STAT:
        r = s->stat;
        break;
    case A_VERID:
        r = s->verid;
        break;
    case A_WATER:
        r = ((s->rx_fifo_written) << R_WATER_RXCOUNT_SHIFT) | ((s->rx_fifo_watermark) << R_WATER_RXWATER_SHIFT) |
                ((s->tx_fifo_written) << R_WATER_TXCOUNT_SHIFT) | ((s->tx_fifo_watermark) << R_WATER_TXWATER_SHIFT);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "s32k358 LPUART read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }

    return r;
}

/* Try to send tx data, and arrange to be called back later if
 * we can't (i.e., the char backend is busy/blocking).
 */
static gboolean lpuart_transmit(void *do_not_use, GIOCondition cond, void *opaque)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);
    int ret;

    // instant drain the FIFO when there's no back-end
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        s->tx_fifo_written = 0;
        return G_SOURCE_REMOVE;
    }

    // Verify that the transmitter is enabled
    if (!(s->ctrl & R_CTRL_TE_MASK)) {
        return G_SOURCE_REMOVE;
    }

    // Verify that there is something to transmit
    if (s->fifo & R_FIFO_TXEMPT_MASK) {
        return G_SOURCE_REMOVE;
    }

    // Transmission from front-end to back-end
    ret = qemu_chr_fe_write(&s->chr, s->tx_fifo, s->tx_fifo_written);

    // Update the number of elements in the fifo and shift the fifo
    if (ret >= 0) {
        s->tx_fifo_written -= ret;
        memmove(s->tx_fifo, s->tx_fifo + ret, s->tx_fifo_written);
    }

    // If there are still elements in the fifo, try to retransmit
    if (s->tx_fifo_written) {
        guint r = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                        lpuart_transmit, s);
        if (!r) {
            s->tx_fifo_written = 0;
            return G_SOURCE_REMOVE;
        }
    } else {
        // There are no more elements in the fifo
        // Transmission ended
        s->stat |= R_STAT_TC_MASK;
        // Fifo empty
        s->fifo |= R_FIFO_TXEMPT_MASK;
    }

    lpuart_update_watermark(s);
    lpuart_update_irq(s);

    return G_SOURCE_REMOVE;
}

static void lpuart_write_tx_fifo(S32K358LPUART *s) {
    // if the transmitter is not enabled, return
    if (!(s->ctrl & R_CTRL_TE_MASK)) {
        return;
    }

    // if the fifo is full, return and set the fifo overflow flag
    if (s->tx_fifo_written == s->tx_fifo_size) {
        s->fifo |= R_FIFO_TXOF_MASK;
        lpuart_update_irq(s);
        qemu_log_mask(LOG_GUEST_ERROR, "s32k358 lpuart: TxFIFO full");
        return;
    }

    // Transmitting
    s->stat &= ~R_STAT_TC_MASK;
    // The fifo is no more empty
    s->fifo &= ~R_FIFO_TXEMPT_MASK;

    uint8_t msg = s->data & R_DATA_R07T07_MASK;

    // Write the data into the fifo
    memcpy(s->tx_fifo + s->tx_fifo_written, &msg, 1);
    s->tx_fifo_written += 1;

    lpuart_update_watermark(s);
    lpuart_update_irq(s);
    // Transmit the data contained in the transmit FIFO
    lpuart_transmit(NULL, G_IO_OUT, s);
}

// Write to the UART registers
static void lpuart_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);
    uint8_t osr;

    //  The reset takes effect immediately and remains asserted until you negate it.
    if (s->global & R_GLOBAL_RST_MASK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: reset is active\n");
                return;
    }

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
        if (value & ~R_GLOBAL_RST_MASK) {
             qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: GLOBAL reserved fields\n");
            break;
        }

        if (value) {
            lpuart_reset((DeviceState *)s);
        }
        // Set again the global value (in case it has been reset)
        s->global = value;
        break;

    case A_BAUD:
        // Check if receiver and transmitter are disabled
        if ((s->ctrl & R_CTRL_RE_MASK) || (s->ctrl & R_CTRL_TE_MASK)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: to change the baud register transmitter and receiver must be disabled.\n");
                break;
        }

        if (value & ~(R_BAUD_BOTHEDGE_MASK | R_BAUD_OSR_MASK |
            R_BAUD_SBNS_MASK | R_BAUD_SBR_MASK)) {
             qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: BAUD unimplemented fields\n");
            break;
        }

        osr = (value & R_BAUD_OSR_MASK) >> R_BAUD_OSR_SHIFT;
        if (osr == 0x1 || osr == 0x2) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: OSR 0x1b and 0x10b values are reserved\n");
            break;
        } else if (osr >= 0x3 && osr <= 0x6) {
            if (!(value & R_BAUD_BOTHEDGE_MASK)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: OSR 0x3...0x06 can be set only if baud[BOTHEDGE]]=1\n");
                break;
            }
        }

        s->baud = value;

        lpuart_update_parameters(s);
        break;

    case A_STAT:
        if (value & (R_STAT_TC_MASK | R_STAT_TDRE_MASK | R_STAT_RDRF_MASK)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: STAT TC, TDRE and RDRF are readonly\n");
                break;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: STAT unimplemented fields\n");

        break;
    case A_CTRL:
        if (value & ~(R_CTRL_PT_MASK | R_CTRL_PE_MASK | R_CTRL_TE_MASK |
            R_CTRL_RE_MASK | R_CTRL_TCIE_MASK | R_CTRL_TIE_MASK | R_CTRL_RIE_MASK)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: CTRL unimplemented fields\n");
                break;
            }

        s->ctrl = value;
        lpuart_update_parameters(s);
        lpuart_update_irq(s);
        break;

    case A_DATA:
        if (value & ~R_DATA_R07T07_MASK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: DATA unimplemented fields\n");
            break;
        }
        s->data = value;
        // Write the new data into the fifo, if there is enough space
        lpuart_write_tx_fifo(s);
        break;

    case A_FIFO:
        if (value & ~(R_FIFO_TXFLUSH_MASK | R_FIFO_RXFLUSH_MASK | R_FIFO_TXOF_MASK |
             R_FIFO_RXUF_MASK | R_FIFO_TXFE_MASK | R_FIFO_RXFE_MASK |
             R_FIFO_TXOFE_MASK | R_FIFO_RXUFE_MASK)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: FIFO unimplemented or read only fields\n");
                break;
        }

        // Check if receiver and transmitter are disabled
        if ((s->ctrl & R_CTRL_RE_MASK) || (s->ctrl & R_CTRL_TE_MASK)) {
            if (value & (R_FIFO_RXFE_MASK | R_FIFO_TXFE_MASK)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: to enable/disable fifo, transmitter and receiver must be disabled.\n");
                break;
            }
        }

        if (value & R_FIFO_TXOF_MASK)
            s->fifo &= ~R_FIFO_TXOF_MASK;
        if (value & R_FIFO_RXUF_MASK)
            s->fifo &= ~R_FIFO_RXUF_MASK;

        // Flush all data inside the fifo
        if (value & R_FIFO_RXFLUSH_MASK) {
            s->rx_fifo_written = 0;
            s->fifo |= R_FIFO_RXEMPT_MASK;
            s->stat &= R_STAT_RDRF_MASK;
        }
        if (value & R_FIFO_TXFLUSH_MASK) {
            s->tx_fifo_written = 0;
            s->fifo |= R_FIFO_TXEMPT_MASK;
            s->stat |= R_STAT_TDRE_MASK;
        }

        s->fifo &= ~(R_FIFO_TXOFE_MASK | R_FIFO_RXUFE_MASK | R_FIFO_TXFE_MASK | R_FIFO_RXFE_MASK);
        s->fifo |= value & (R_FIFO_TXOFE_MASK | R_FIFO_RXUFE_MASK | R_FIFO_TXFE_MASK | R_FIFO_RXFE_MASK);

        // Change the rx fifo dimension
        if (value & R_FIFO_RXFE_MASK) {
            if (s->id < 2)
                s->rx_fifo_size = S32K358_LPUART_0_1_RX_FIFO_SIZE;
            else
                s->rx_fifo_size = S32K358_LPUART_2_15_RX_FIFO_SIZE;
        } else
            s->rx_fifo_size = 1;

        // Change the tx fifo dimension
        if (value & R_FIFO_RXFE_MASK) {
            if (s->id < 2)
                s->tx_fifo_size = S32K358_LPUART_0_1_TX_FIFO_SIZE;
            else
                s->tx_fifo_size = S32K358_LPUART_2_15_TX_FIFO_SIZE;
        } else
            s->tx_fifo_size = 1;

        lpuart_update_irq(s);
        break;

    case A_WATER:
        if (value & ~(R_WATER_RXWATER_MASK | R_WATER_TXWATER_MASK)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: WATER reserved or read only fields\n");
                break;
        } else if ((s->id >= 2) & (value & ~(R_WATER_RXWATER_SHORT_MASK | R_WATER_TXWATER_SHORT_MASK))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "S32K358 LPUART: WATER must be smaller for lpuart2...lpuart15\n");
                break;
        }
        if (s->id < 2) {
            s->tx_fifo_watermark = (value & R_WATER_TXWATER_MASK) >> R_WATER_TXWATER_SHIFT;
            s->rx_fifo_watermark = (value & R_WATER_RXWATER_MASK) >> R_WATER_RXWATER_SHIFT;
        } else {
            s->tx_fifo_watermark = (value & R_WATER_TXWATER_SHORT_MASK) >> R_WATER_TXWATER_SHORT_SHIFT;
            s->rx_fifo_watermark = (value & R_WATER_RXWATER_SHORT_MASK) >> R_WATER_RXWATER_SHORT_SHIFT;
        }


        lpuart_update_watermark(s);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "S32K358 LPUART write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps lpuart_ops = {
    .read = lpuart_read,
    .write = lpuart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void lpuart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    S32K358LPUART *s = S32K358_LPUART(obj);
    // Memory map the device and connect the IRQ
    memory_region_init_io(&s->iomem, obj, &lpuart_ops, s, "uart", 0x0800);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->uartint);
}

static void lpuart_realize(DeviceState *dev, Error **errp)
{
    S32K358LPUART *s = S32K358_LPUART(dev);

    if (s->pclk_frq == 0) {
        error_setg(errp, "S32K358 LPUART: pclk-frq property must be set");
        return;
    }

    // Flow control not implemented
    // Handlers to allow the UART work in the receive direction
    qemu_chr_fe_set_handlers(&s->chr, lpuart_can_receive, lpuart_receive,
                             NULL, NULL, s, NULL, true);
}

// To recover after a problem
static int lpuart_post_load(void *opaque, int version_id)
{
    S32K358LPUART *s = S32K358_LPUART(opaque);

    lpuart_update_parameters(s);
    lpuart_update_irq(s);
    return 0;
}

// To make the device snapshoptable: it is not fully implemented
static const VMStateDescription lpuart_vmstate = {
    .name = "s32k358-lpuart",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = lpuart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(id, S32K358LPUART),
        VMSTATE_UINT32(verid, S32K358LPUART),
        VMSTATE_UINT32(param, S32K358LPUART),
        VMSTATE_UINT32(global, S32K358LPUART),
        VMSTATE_UINT32(baud, S32K358LPUART),
        VMSTATE_UINT32(stat, S32K358LPUART),
        VMSTATE_UINT32(ctrl, S32K358LPUART),
        VMSTATE_UINT32(data, S32K358LPUART),
        VMSTATE_UINT32(fifo, S32K358LPUART),
        VMSTATE_UINT8_ARRAY(rx_fifo, S32K358LPUART, S32K358_LPUART_0_1_RX_FIFO_SIZE),
        VMSTATE_UINT8_ARRAY(tx_fifo, S32K358LPUART, S32K358_LPUART_0_1_TX_FIFO_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static Property lpuart_properties[] = {
    DEFINE_PROP_CHR("chardev", S32K358LPUART, chr),
    DEFINE_PROP_UINT32("pclk-frq", S32K358LPUART, pclk_frq, 0),
    DEFINE_PROP_UINT32("id", S32K358LPUART, id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void lpuart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lpuart_realize;
    dc->vmsd = &lpuart_vmstate;
    dc->reset = lpuart_reset;
    device_class_set_props(dc, lpuart_properties);
}

static const TypeInfo lpuart_info = {
    .name = TYPE_S32K358_LPUART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S32K358LPUART),
    .instance_init = lpuart_init,
    .class_init = lpuart_class_init,
};

static void lpuart_register_types(void)
{
    type_register_static(&lpuart_info);
}

type_init(lpuart_register_types);