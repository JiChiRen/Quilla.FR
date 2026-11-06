/*
 * Copyright (c) 2026, JiChiRen Info. Tech.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions of the BSD-3
 * license referenced in the project LICENSE file are met.
 *
 * This file is part of the Quilla.FR appliance.
 */
/*
 * recver - USART1 CIRCULAR-DMA ring receiver.
 *
 * Hardware contract (must match the QBOOT arch/cpu reconfiguration):
 *   USART1, PA9 TX / PA10 RX, DMA1_Channel3 mapped to USART1_RD, 115200 8-N-1.
 *
 * The DMA writes into s_rx_ring forever and is never re-armed. A wrap is
 * harmless: the receiver derives the DMA head from the live CNDTR counter.
 * Only the USART IDLE interrupt is enabled - no DMA TC/HT interrupt, so the
 * HAL UART state machine is never entered from the receive path and cannot
 * clear CR3.DMAR on a wrap (the classic "RX silently stopped" failure of the
 * NORMAL ping-pong design).
 *
 * Commands are deliberately tiny. Quilla's host updater probes the running APP
 * with "part" and triggers an upgrade with "swqboot"; both must be answered in
 * the same shape as Quilla's own Contiki shell, otherwise the host mistakes
 * the APP shell for an upgrade-idle QBOOT and sends a binary ENTER frame into
 * the text parser.
 */
#include "recver.h"
#include "main.h"
#include "app_qboot_meta.h"

#include <string.h>

/* Globals shared with py32f040_hal_msp.c and py32f040_it.c. */
UART_HandleTypeDef g_uart;
DMA_HandleTypeDef  g_dma_rx;

static uint8_t           s_rx_ring[QFR_RX_RING_SIZE];
static volatile uint16_t s_tail;
static TaskHandle_t      s_task;
static uint8_t           s_line[QFR_LINE_MAX + 1U];
static uint16_t          s_line_len;

/* ---------------------------------------------------------------- console */
void
recver_puts(const char *s)
{
  size_t n = strlen(s);
  if(n != 0U) {
    (void)HAL_UART_Transmit(&g_uart, (uint8_t *)s, (uint16_t)n, 200U);
  }
}

void
recver_notify_from_isr(BaseType_t *higher_priority_task_woken)
{
  if(s_task != NULL) {
    vTaskNotifyGiveFromISR(s_task, higher_priority_task_woken);
  }
}

/* ------------------------------------------------------------- QBOOT entry */
static void
enter_qboot(void)
{
  recver_puts("\r\n[QAPP] switching to QBOOT...\r\n");

  /* Quiesce the scheduler before touching Flash. A failed META write must NOT
   * reset: QBOOT would still boot the old APP, but the unit would otherwise
   * lose its upgrade request and look stuck. */
  vTaskSuspendAll();
  if(AppQBoot_MetaSetFlag(APP_QBOOT_FLAG_QBOOT) == 0) {
    volatile uint32_t drain;
    for(drain = 0U; drain < 200000UL; drain++) {
    }  /* let the console shift out before the reset kills the UART */
    NVIC_SystemReset();
  }
  (void)xTaskResumeAll();
  recver_puts("[QAPP] META flag write failed, staying in APP\r\n> ");
}

/* ------------------------------------------------------------ line parser */
static void
handle_line(void)
{
  s_line[s_line_len] = 0U;

  if(strcmp((const char *)s_line, "part") == 0) {
    /* Fingerprint used by quilla_host_updater.py: it must see "app1". */
    recver_puts("part\r\napp1\r\n> ");
  } else if(strcmp((const char *)s_line, "swqboot") == 0) {
    enter_qboot();                 /* does not return on success */
  } else if(strcmp((const char *)s_line, "version") == 0) {
    recver_puts(QFR_APP_NAME " " QFR_APP_VERSION "\r\n> ");
  } else {
    recver_puts("?\r\n> ");
  }
  s_line_len = 0U;
}

static void
line_feed(uint8_t b)
{
  if(b == '\r' || b == '\n') {
    if(s_line_len != 0U) {
      handle_line();
    }
    s_line_len = 0U;
    return;
  }
  /* Printable ASCII only. Binary QIAP frames must not corrupt the line buffer. */
  if(b >= 0x20U && b <= 0x7EU) {
    if(s_line_len < QFR_LINE_MAX) {
      s_line[s_line_len++] = b;
    } else {
      s_line_len = 0U;            /* overlong line: drop, never overflow */
    }
  }
}

/* ------------------------------------------------------------- ring logic */
static uint16_t
ring_head(void)
{
  uint16_t cndtr = (uint16_t)g_dma_rx.Instance->CNDTR;
  return (uint16_t)((QFR_RX_RING_SIZE - cndtr) &
                    (QFR_RX_RING_SIZE - 1U));
}

/* ------------------------------------------------------------------- init */
int
recver_init(void)
{
  g_uart.Instance                    = QFR_USART;
  g_uart.Init.BaudRate               = QFR_BAUDRATE;
  g_uart.Init.WordLength             = UART_WORDLENGTH_8B;
  g_uart.Init.StopBits               = UART_STOPBITS_1;
  g_uart.Init.Parity                 = UART_PARITY_NONE;
  g_uart.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
  g_uart.Init.Mode                   = UART_MODE_TX_RX;
  g_uart.Init.OverSampling           = UART_OVERSAMPLING_16;
  g_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if(HAL_UART_Init(&g_uart) != HAL_OK) {
    return -1;
  }

  /* Start the CIRCULAR ring directly. HAL_UART_Receive_DMA() would enable
   * TC/HT interrupts and its completion callback would tear down CR3.DMAR on
   * the first wrap, so it is deliberately not used here. */
  __HAL_DMA_DISABLE(&g_dma_rx);
  g_dma_rx.Instance->CMAR  = (uint32_t)s_rx_ring;
  g_dma_rx.Instance->CNDTR = (uint32_t)QFR_RX_RING_SIZE;
  g_dma_rx.Instance->CCR  |= (DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN);
  SET_BIT(g_uart.Instance->CR3, USART_CR3_DMAR);
  __HAL_UART_ENABLE_IT(&g_uart, UART_IT_IDLE);

  s_tail = 0U;
  s_line_len = 0U;
  return 0;
}

/* ------------------------------------------------------------------- task */
void
recver_task(void *arg)
{
  int pass;

  (void)arg;
  s_task = xTaskGetCurrentTaskHandle();

  for(;;) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Drain until we catch up with the DMA head. Bound the number of passes so
     * a sustained stream cannot starve the LED task; any residue is picked up
     * by the next IDLE interrupt. */
    for(pass = 0; pass < 4; pass++) {
      uint16_t head = ring_head();
      uint16_t tail = (uint16_t)s_tail;

      if(head == tail) {
        break;
      }
      do {
        uint8_t b = s_rx_ring[tail];
        tail = (uint16_t)((tail + 1U) & (QFR_RX_RING_SIZE - 1U));
        line_feed(b);
      } while(tail != head);

      s_tail = tail;
    }
  }
}
