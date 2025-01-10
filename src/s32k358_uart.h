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
OBJECT_DECLARE_SIMPLE_TYPE(S32K358_LPUART, TYPE_S32K358_LPUART)

#define S32K358_LPUART_RX_FIFO_SIZE           4
#define S32K358_LPUART_TX_FIFO_SIZE           4

struct S32K358LPUART {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq uartint;
    guint watch_tag;

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

    /* This UART has a FIFO, only a 1-character buffer for each of Tx and Rx */
    uint8_t rx_fifo[S32K358_LPUART_RX_FIFO_SIZE];
    uint8_t tx_fifo[S32K358_LPUART_TX_FIFO_SIZE];
};

#endif
