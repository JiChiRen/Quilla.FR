/*
 * Interrupt handlers for the appliance.
 *
 * PendSV_Handler / SVC_Handler are provided by the FreeRTOS CM0 port through
 * the xPortPendSVHandler / vPortSVCHandler macros in FreeRTOSConfig.h; they
 * must NOT be redefined here. SysTick feeds both the HAL timebase and the RTOS
 * tick.
 */
#include "main.h"
#include "recver.h"
#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

void
NMI_Handler(void)
{
}

void
HardFault_Handler(void)
{
  for(;;) {
  }
}

void
SysTick_Handler(void)
{
  HAL_IncTick();
  if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    xPortSysTickHandler();
  }
}

void
USART1_IRQHandler(void)
{
  BaseType_t woken = pdFALSE;

  if(__HAL_UART_GET_FLAG(&g_uart, UART_FLAG_IDLE) != RESET) {
    /* Reading SR then DR clears IDLE together with ORE/PE/FE/NE. If an error
     * flag is left latched, HAL_UART_IRQHandler() may tear down the RX DMA. */
    volatile uint32_t sr = g_uart.Instance->SR;
    volatile uint32_t dr = g_uart.Instance->DR;
    (void)sr;
    (void)dr;
    __HAL_UART_CLEAR_IDLEFLAG(&g_uart);
    recver_notify_from_isr(&woken);
  }

  HAL_UART_IRQHandler(&g_uart);

  /* Belt-and-suspenders: keep the circular receiver alive if the HAL error
   * path cleared DMAR. */
  if((g_uart.Instance->CR3 & USART_CR3_DMAR) == 0U) {
    SET_BIT(g_uart.Instance->CR3, USART_CR3_DMAR);
  }

  portYIELD_FROM_ISR(woken);
}
