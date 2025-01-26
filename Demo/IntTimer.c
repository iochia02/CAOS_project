/*
 * FreeRTOS application s32k358 timer.
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

/* Scheduler includes. */
#include "FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
#include "semphr.h"
#include "uart.h"

/* Demo includes. */
#include "IntTimer.h"
/* Library includes. */
#include "nvic.h"

#define tmrTIMER_00_FREQUENCY	( 1UL )
#define tmrTIMER_01_FREQUENCY	( 10UL )
#define tmrTIMER_10_FREQUENCY	( 7UL )
// IRQ lines
#define TIMER0_IRQn 			(96)
#define TIMER1_IRQn 			(97)

typedef struct
{
	__IO uint32_t RELOAD;						// Offset: 0x1x0 (R/W) Timer load value (specifies the length of the timeout period in clock cycles)
	__O  uint32_t  VALUE;                     	// Offset: 0x1x4 (R) Current timer value (indicates the current timer value)
	__IO uint32_t  CTRL;                 	    // Offset: 0x1x8 (R/W) Timer control (controls timer behaviour)
	union {
		__I    uint32_t  INTSTATUS;             // Offset: 0x1xC (R/ ) Interrupt Status Register
		__O    uint32_t  INTCLEAR;              // Offset: 0x1xC ( /W) Interrupt Clear Register
	};
} S32K358_CHANNEL_TypeDef;
// Data structure modelling the timer's registers
typedef struct
{
	__IO uint32_t PIT_CTRL; 					// Offset: 0x000 (R/W) PIT module control: enables the PIT timer clock
	char UNIMPLEMENTED[0x100 - 0x4];			// We do not model the registers between 0x004 to 0x100
	S32K358_CHANNEL_TypeDef channels[4];
} S32K358_TIMER_TypeDef;

// Timers' memory mapping
#define TIMER_0_BASE_ADDRESS (0x400B0000UL)
#define S32K358_TIMER0       ((S32K358_TIMER_TypeDef  *) TIMER_0_BASE_ADDRESS  )
#define TIMER_1_BASE_ADDRESS (0x400B4000UL)
#define S32K358_TIMER1       ((S32K358_TIMER_TypeDef  *) TIMER_1_BASE_ADDRESS  )
#define TIMER_2_BASE_ADDRESS (0x402FC000UL)
#define S32K358_TIMER2       ((S32K358_TIMER_TypeDef  *) TIMER_2_BASE_ADDRESS  )

S32K358_TIMER_TypeDef* tGetTimer(uint32_t timer) {
	switch (timer) {
		case 0:
			return S32K358_TIMER0;
		case 1:
			return S32K358_TIMER1;
		case 2:
			return S32K358_TIMER2;
		default:
			return NULL;
	}
}

S32K358_CHANNEL_TypeDef* cGetChannel(S32K358_TIMER_TypeDef *timer, uint32_t channel) {
	switch (channel) {
		case 0 ... 3:
			return &(timer->channels[channel]);
		default:
			return NULL;
	}
}

extern SemaphoreHandle_t xBinarySemaphoreA;
extern SemaphoreHandle_t xBinarySemaphoreB;
extern SemaphoreHandle_t xBinarySemaphoreC;

void vTimer0Handler() {
	BaseType_t xHigherPriorityTaskWoken0 = pdFALSE, xHigherPriorityTaskWoken1 = pdFALSE;
	S32K358_CHANNEL_TypeDef * channel0 = cGetChannel(S32K358_TIMER0, CHANNEL0);
	S32K358_CHANNEL_TypeDef * channel1 = cGetChannel(S32K358_TIMER0, CHANNEL1);

	// The timer's four channels share the same irq, so we have to check which one triggered the interrupt
	if (channel0->INTSTATUS) {
		channel0->INTCLEAR = ( 1ul << 0 );
		xSemaphoreGiveFromISR(xBinarySemaphoreA, &xHigherPriorityTaskWoken0);
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken0 );
	}

	if (channel1->INTSTATUS) {
		channel1->INTCLEAR = ( 1ul << 0 );
		xSemaphoreGiveFromISR(xBinarySemaphoreB, &xHigherPriorityTaskWoken1);
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken1 );
	}
}

void vTimer1Handler() {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	S32K358_CHANNEL_TypeDef * channel = cGetChannel(S32K358_TIMER1, CHANNEL0);
	channel->INTCLEAR = ( 1ul << 0 );

	xSemaphoreGiveFromISR(xBinarySemaphoreC, &xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

void vInitialiseChannel(S32K358_CHANNEL_TypeDef *channel, uint32_t frequency) {
	channel->INTCLEAR = ( 1ul << 0 );
	channel->RELOAD   = ( configCPU_CLOCK_HZ * frequency);
	channel->CTRL     = ( ( 1ul <<  1 ) | /* Enable Timer interrupt. */
						     ( 1ul <<  0 ) );  /* Enable Timer. */
}

void vInitialiseTimers( void )
{
	S32K358_TIMER0->PIT_CTRL &= ~2; // the second bit is the one that enables/disables the module (0 = enabled)
	vInitialiseChannel(cGetChannel(S32K358_TIMER0, CHANNEL0), tmrTIMER_00_FREQUENCY);
	vInitialiseChannel(cGetChannel(S32K358_TIMER0, CHANNEL1), tmrTIMER_01_FREQUENCY);

	S32K358_TIMER1->PIT_CTRL &= ~2;
	vInitialiseChannel(cGetChannel(S32K358_TIMER1, CHANNEL0), tmrTIMER_10_FREQUENCY);

	// Set the interrupt priority and enable the irq
	NVIC_SetPriority( TIMER0_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_SetPriority( TIMER1_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);

	NVIC_EnableIRQ( TIMER0_IRQn );
	NVIC_EnableIRQ( TIMER1_IRQn );
}

uint32_t ulTimerOK( S32K358_TIMER_TypeDef * timer, S32K358_CHANNEL_TypeDef * channel ) {
	if (timer->PIT_CTRL & 2) {
		UART_printf("Timer not enabled\n");
		return 0;
	}
	// Check if the channel is enabled
	if (!(channel->CTRL & 1 )) {
		UART_printf("Channel not enabled\n");
		return 0;
	}
	return 1;
}

BaseType_t xSetReload( uint32_t n_timer, uint32_t n_channel, uint32_t value ) {
	char msg[100];

	S32K358_TIMER_TypeDef * timer = tGetTimer(n_timer);
	if (timer == NULL) {
		snprintf(msg, 100, "Timer %ld does not exists; the board only supports three timers (0-2)\n", n_timer);
		UART_printf(msg);
		return pdFALSE;
	}

	S32K358_CHANNEL_TypeDef * channel = cGetChannel(timer, n_channel);
	if (channel == NULL) {
		snprintf(msg, 100, "Channel %ld does not exists; the board only supports four channels (0-3)\n", n_channel);
		UART_printf(msg);
		return pdFALSE;
	}

	if (!ulTimerOK(timer, channel)) {
		return pdFALSE;
	}

	channel->RELOAD = value;
	return pdTRUE;
}

uint32_t ulGetReload( uint32_t n_timer, uint32_t n_channel ) {
	char msg[100];

	S32K358_TIMER_TypeDef * timer = tGetTimer(n_timer);
	if (timer == NULL) {
		snprintf(msg, 100, "Timer %ld does not exists; the board only supports three timers (0-2)\n", n_timer);
		UART_printf(msg);
		return 0;
	}

	S32K358_CHANNEL_TypeDef * channel = cGetChannel(timer, n_channel);
	if (channel == NULL) {
		snprintf(msg, 100, "Channel %ld does not exists; the board only supports four channels (0-3)\n", n_channel);
		UART_printf(msg);
		return 0;
	}

	if (!ulTimerOK(timer, channel)) {
		return 0;
	}

	return channel->RELOAD;
}

uint32_t ulGetCount( uint32_t n_timer, uint32_t n_channel ) {
	char msg[100];

	S32K358_TIMER_TypeDef * timer = tGetTimer(n_timer);
	if (timer == NULL) {
		snprintf(msg, 100, "Timer %ld does not exists; the board only supports three timers (0-2)\n", n_timer);
		UART_printf(msg);
		return 0;
	}

	S32K358_CHANNEL_TypeDef * channel = cGetChannel(timer, n_channel);
	if (channel == NULL) {
		snprintf(msg, 100, "Channel %ld does not exists; the board only supports four channels (0-3)\n", n_channel);
		UART_printf(msg);
		return 0;
	}

	if (!ulTimerOK(timer, channel)) {
		return 0;
	}

	return channel->VALUE;
}

