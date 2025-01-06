/*
 * FreeRTOS application s32k358 timer.
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

#ifndef INT_TIMER_H
#define INT_TIMER_H

enum Timer {
  TIMER0,
  TIMER1,
  TIMER2
};

enum Channel {
  CHANNEL0,
  CHANNEL1,
  CHANNEL2,
  CHANNEL3
};

void vInitialiseTimers( void );
uint32_t ulGetReload( uint32_t n_timer, uint32_t n_channel );
BaseType_t xSetReload( uint32_t n_timer, uint32_t n_channel, uint32_t value );
uint32_t ulGetCount( uint32_t n_timer, uint32_t n_channel );

#endif

