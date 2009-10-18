/***************************************************************
 *
 * OpenBeacon.org - OpenBeacon UART driver
 *
 * Copyright 2007 Milosch Meriac <meriac@openbeacon.de>
 *
 ***************************************************************

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#include <string.h>
#include <board.h>
#include <beacontypes.h>
#include <USB-CDC.h>

#define UART_QUEUE_SIZE 128

static xQueueHandle uart_queue_rx;

void uart_isr_handler(void)
{
    u_int8_t data,woken;
    u_int32_t reason;

    // get reason for IRQ
    reason=AT91C_BASE_US0->US_CSR;

    // on RX character
    if(reason & AT91C_US_RXRDY)
    {
	data=(unsigned char)(AT91C_BASE_US0->US_RHR);
	xQueueSend(uart_queue_rx, &data, 0);
	woken=pdTRUE;
    }
    else
	woken=pdFALSE;

    // ack IRQ
    AT91C_BASE_AIC->AIC_EOICR = 0;
    // wake up ?
    if(woken)
	portYIELD_FROM_ISR();
}

static void __attribute__((naked))
uart_isr(void)
{
    portSAVE_CONTEXT();
    uart_isr_handler();
    portRESTORE_CONTEXT();
}

static void
uart_task (void *parameter)
{
    (void) parameter;
    u_int8_t data;

    vTaskDelay(11000/portTICK_RATE_MS);

    AT91F_AIC_ConfigureIt(AT91C_ID_US0, 4, AT91C_AIC_SRCTYPE_INT_POSITIVE_EDGE, uart_isr );
    // enable UART0
    AT91C_BASE_US0->US_CR = AT91C_US_RXEN|AT91C_US_TXEN;
    // enable IRQ
    AT91C_BASE_US0->US_IER = AT91C_US_RXRDY|AT91C_US_TXRDY|AT91C_US_ENDRX|AT91C_US_ENDTX;

    // remove reset line
    AT91F_PIO_SetOutput(WLAN_PIO, WLAN_RESET|WLAN_WAKE);
    // tickle On button for WLAN module
    vTaskDelay(1/portTICK_RATE_MS);
    AT91F_PIO_ClearOutput(WLAN_PIO, WLAN_WAKE);

    for(;;)
    {
	if( xQueueReceive(uart_queue_rx, &data, ( portTickType ) 1000 ) )
	    vUSBSendByte(data);

	vUSBSendByte('#');

	AT91C_BASE_US0->US_THR = 0x55;
    }
}


static inline void
uart_set_baudrate(unsigned int Baudrate)
{
    AT91F_US_SetBaudrate(AT91C_BASE_US0, MCK, Baudrate);
}

void
uart_init (void)
{
    // configure UART peripheral
    AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_US0);
    AT91F_PIO_CfgPeriph(WLAN_PIO, (WLAN_RXD|WLAN_TXD|WLAN_RTS|WLAN_CTS), 0);

    // configure IOs
    AT91F_PIO_CfgOutput(WLAN_PIO, WLAN_ADHOC|WLAN_RESET|WLAN_WAKE);
    AT91F_PIO_ClearOutput(WLAN_PIO, WLAN_RESET|WLAN_ADHOC|WLAN_WAKE);

    // reset and disable UART0
    AT91C_BASE_US0->US_CR = AT91C_US_RSTRX|AT91C_US_RSTTX|AT91C_US_RXDIS|AT91C_US_TXDIS;

    // Standard Asynchronous Mode : 8 bits , 1 stop , no parity
    AT91C_BASE_US0->US_MR = AT91C_US_ASYNC_MODE;
    uart_set_baudrate(WLAN_BAUDRATE);

    // no IRQs
    AT91C_BASE_US0->US_IDR = 0xFFFF;

    // receiver time-out (disabled)
    AT91C_BASE_US0->US_RTOR = 0;
    // transmitter timeguard (disabled)
    AT91C_BASE_US0->US_TTGR = 0;
    // FI over DI Ratio Value (disabled)
    AT91C_BASE_US0->US_FIDI = 0;
    // IrDA Filter value (disabled)
    AT91C_BASE_US0->US_IF = 0;

    // create UART queue with minimum buffer size
    uart_queue_rx = xQueueCreate (UART_QUEUE_SIZE,
	(unsigned portCHAR) sizeof (signed portCHAR));

    xTaskCreate (uart_task, (signed portCHAR *) "uart_task", TASK_NRF_STACK,
	NULL, TASK_NRF_PRIORITY, NULL);
}
