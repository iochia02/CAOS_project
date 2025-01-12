/*
 * S32K358 LPUART emulation
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

#ifndef S32K358_UART_H
#define S32K358_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_S32K358_LPUART "s32k358_lpuart"
OBJECT_DECLARE_SIMPLE_TYPE(S32K358LPUART, S32K358_LPUART)

#define S32K358_LPUART_0_1_RX_FIFO_SIZE           16
#define S32K358_LPUART_0_1_TX_FIFO_SIZE           16
#define S32K358_LPUART_2_15_RX_FIFO_SIZE           4
#define S32K358_LPUART_2_15_TX_FIFO_SIZE           4

struct S32K358LPUART {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq uartint;
    guint watch_tag;

    uint32_t id;
    uint32_t verid;
    uint32_t param;
    uint32_t pclk_frq;
    uint32_t baud;
    uint32_t global;
    uint32_t stat;
    uint32_t ctrl;
    uint32_t data;
    uint32_t fifo;
    uint32_t txcnt;
    uint32_t rxcnt;

    /* This UART has a FIFO */
    uint8_t rx_fifo[S32K358_LPUART_0_1_RX_FIFO_SIZE];
    uint8_t tx_fifo[S32K358_LPUART_0_1_TX_FIFO_SIZE];
    uint8_t tx_fifo_size;
    uint8_t rx_fifo_size;
    uint8_t tx_fifo_written;
    uint8_t rx_fifo_written;
    uint8_t tx_fifo_watermark;
    uint8_t rx_fifo_watermark;
};

#endif
