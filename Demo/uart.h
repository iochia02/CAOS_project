#ifndef __PRINTF__
#define __PRINTF__

#include "FreeRTOS.h"

void UART_init(void);
void UART_printf(const char *s);
void UART_getRxBuffer(char* usr_buf, uint32_t len);

#endif
