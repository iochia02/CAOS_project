/*
 * FreeRTOS application s32k358 lpuart.
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

#ifndef __UART__
#define __UART__

#include "FreeRTOS.h"

void UART_init(void);
void UART_printf(const char *s);
void UART_getRxBuffer(char* usr_buf, uint32_t len);

#endif
