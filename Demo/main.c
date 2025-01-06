/*
 * FreeRTOS application: s32k358 main application.
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 * Copyright (c) 2025 Braidotti Sara, Iorio Chiara, Pani Matteo.
 *
 */

//#include <stdlib.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "IntTimer.h"

#include "uart.h"
#define mainTASK_PRIORITY    ( tskIDLE_PRIORITY + 2 )

SemaphoreHandle_t xBinarySemaphoreA;
SemaphoreHandle_t xBinarySemaphoreB;
SemaphoreHandle_t xBinarySemaphoreC;

uint32_t n_timer0 = 0, n_timer1 = 0;
char msgA[150], msgB[150], msgC[150];

void vTaskA(void *pvParameters) {
	(void) pvParameters;
	UART_printf("Hello world from task A\n");

	while(1) {
		if (xSemaphoreTake(xBinarySemaphoreA, portMAX_DELAY) == pdTRUE) {
			// The semaphore was successfully taken, meaning the ISR occurred
			// When timer 0 channel 0 expires, the task prints the current value of the other two timers
			snprintf (msgC, 150, "Task A (timer 00): timer B (01) value=%10ld, timer C (10) value=%10ld\n",
					  ulGetCount(TIMER0,CHANNEL1), ulGetCount(TIMER1,CHANNEL0));
			UART_printf(msgC);
		}
	}
}

void vTaskB(void *pvParameters) {
	(void) pvParameters;
	uint32_t period;
	UART_printf("Hello world from task B\n");

	while(1) {
		if (xSemaphoreTake(xBinarySemaphoreB, portMAX_DELAY) == pdTRUE) {
			// When the timer 0 channel 1 expires, the task changes the period of timer 1 channel 0
			period = ulGetReload(TIMER1, CHANNEL0);
			if (n_timer1 % 2 == 0) {
				if (xSetReload(TIMER1, CHANNEL0, period / 2) == pdFALSE)
					UART_printf("Failed setting new reload value\n");
			} else {
				if (xSetReload(TIMER1, CHANNEL0, period * 2) == pdFALSE)
					UART_printf("Failed setting new reload value\n");
			}
			snprintf (msgB, 150, "Task B (timer 01): Period of timer C (10) changed from %ld to %ld\n", period, ulGetReload(TIMER1, CHANNEL0));
			UART_printf(msgB);
			n_timer1++;
		}
	}
}

void vTaskC(void *pvParameters) {
	(void) pvParameters;
	UART_printf("Hello world from task C\n");

	while(1) {
		if (xSemaphoreTake(xBinarySemaphoreC, portMAX_DELAY) == pdTRUE) {
			// The task prints the number of times that the timer 0 channel 0 expired
			snprintf (msgA, 150, "Task C (timer 10): timer 10 expired %ld times\n", n_timer0);
			UART_printf(msgA);
			n_timer0++;
		}
	}
}

int main(int argc, char **argv){

	(void) argc;
	(void) argv;

    UART_init();
	vInitialiseTimers();

	xTaskCreate(vTaskA, "TaskA", configMINIMAL_STACK_SIZE*5, NULL, mainTASK_PRIORITY, NULL);
	xTaskCreate(vTaskB, "TaskB", configMINIMAL_STACK_SIZE*5, NULL, mainTASK_PRIORITY, NULL);
	xTaskCreate(vTaskC, "TaskC", configMINIMAL_STACK_SIZE*5, NULL, mainTASK_PRIORITY, NULL);

	xBinarySemaphoreA = xSemaphoreCreateBinary();
	xBinarySemaphoreB = xSemaphoreCreateBinary();
	xBinarySemaphoreC = xSemaphoreCreateBinary();
    if (xBinarySemaphoreA == NULL || xBinarySemaphoreB == NULL || xBinarySemaphoreC == NULL) {
		UART_printf("Something went wrong in the semaphores creation\n");
		return -1;
	}

	// Give control to the scheduler
	vTaskStartScheduler();

	// If everything ok should never reach here
    for( ; ; );
}
