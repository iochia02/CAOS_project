//#include <stdlib.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

#include "uart.h"
#define mainTASK_PRIORITY    ( tskIDLE_PRIORITY + 2 )

void vTaskA(void *pvParameters) {
	(void) pvParameters;

	UART_printf("\nHello world from task A!\n");

	// Delete this task
	vTaskDelete(NULL);
}

void vTaskB(void *pvParameters) {
	(void) pvParameters;
	uint8_t i;

	for (i = 0; i < 5; i++)
		UART_printf("Hello world from task B!\n");

	// Delete this task
	vTaskDelete(NULL);
}

int main(int argc, char **argv){

	(void) argc;
	(void) argv;

    UART_init();
	xTaskCreate(vTaskA,"TaskA", configMINIMAL_STACK_SIZE, NULL, mainTASK_PRIORITY, NULL);

	xTaskCreate(vTaskB, "vTaskB", configMINIMAL_STACK_SIZE, NULL, mainTASK_PRIORITY, NULL);

	// Give control to the scheduler
	vTaskStartScheduler();

	// If everything ok should never reach here
    for( ; ; );
}
