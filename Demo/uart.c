#include "uart.h"
#include "nvic.h"
#include "semphr.h"

// Data structure modelling the lpuart's registers
typedef struct
{
	__O uint32_t VERID;
	__O uint32_t PARAM;
    __IO uint32_t GLOBAL;
    char UNIMPLEMENTED1[0x4];
    __IO uint32_t BAUD;
    __IO uint32_t STAT;
    __IO uint32_t CTRL;
    __IO uint32_t DATA;
    char UNIMPLEMENTED2[0x28 - 0x20];
    __IO uint32_t FIFO;
    __IO uint32_t WATER;
} S32K358_UART_Typedef;

// Lpuart's memory mapping
#define UART_0_BASE_ADDRESS (0x40328000UL)
#define S32K358_UART0       ((S32K358_UART_Typedef  *) UART_0_BASE_ADDRESS  )

#define TE_SHIFT 19
#define RE_SHIFT 18
#define RIE_SHIFT 21
#define TXFE_SHIFT 7
#define RXFE_SHIFT 3
#define TDRE_SHIFT 23
#define RXEMPT_SHIFT 22
#define RXFLUSH_SHIFT 14
#define TXFLUSH_SHIFT 15

#define UART0_IRQn 			(141)
#define BUF_LEN 100

extern SemaphoreHandle_t xBinarySemaphoreD;
char buf[BUF_LEN];
uint32_t buf_index = 0;


void UART_init( void )
{
    /* initialize the UART:
        * enable the receive and transmit FIFO
        * set as watermark the FIFO length-1
        * enable the transmitter, the receiver and the receiver interrupt
    */
    uint32_t len_fifo = S32K358_UART0->PARAM & 0x000000FF;
    S32K358_UART0->FIFO = (1 << TXFE_SHIFT) | (1 << RXFE_SHIFT) | (1 << RXFLUSH_SHIFT) |  (1 << TXFLUSH_SHIFT);
    S32K358_UART0->WATER = ( (1 << len_fifo) - 1);
    S32K358_UART0->CTRL = (1 << TE_SHIFT) | (1 << RE_SHIFT) | (1 << RIE_SHIFT);

    // Set the interrupt priority and enable the irq
    NVIC_SetPriority( UART0_IRQn, configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
	NVIC_EnableIRQ( UART0_IRQn );
}

void UART_printf(const char *s) {
    while(*s != '\0') {
        // Wait to have enough room in the transmit fifo
        while(!(S32K358_UART0->STAT & (1 << TDRE_SHIFT)));

        S32K358_UART0->DATA = (unsigned int)(*s);
        s++;
    }
}

void vUart0Handler() {
    // When the user writes something, the interrupt is set
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Keep collecting characters till the fifo is full / the user pressed enter
    while (!(S32K358_UART0->FIFO & (1 << RXEMPT_SHIFT))) {
        buf[buf_index] = (char) (S32K358_UART0->DATA & 0xFF);

        if (buf[buf_index] == '\r' || buf_index == BUF_LEN-2) {
            buf[buf_index] = '\0';
            // Disable the interrupt and activate taskD
            NVIC_DisableIRQ( UART0_IRQn );
            xSemaphoreGiveFromISR(xBinarySemaphoreD, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
            return;
        }

        buf_index++;
    }
}

void UART_getRxBuffer(char* usr_buf, uint32_t len) {
    // Read the data from the receive buffer
    uint32_t i;
    buf_index = 0;
    for(i = 0; i < len-1 && buf[i] != '\0'; i++) {
        usr_buf[i] = buf[i];
    }
    usr_buf[i] = '\0';
    // enable the interrupt again
    NVIC_EnableIRQ( UART0_IRQn );
}
