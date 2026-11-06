/*
 * Quilla.FR appliance - shared definitions.
 *
 * This image is a FreeRTOS APP living behind Quilla QBOOT. It intentionally
 * owns only one peripheral: USART1 (PA9 TX / PA10 RX) with a CIRCULAR RX DMA.
 * QBOOT must be rebuilt for the same pins; see doc/qboot-adaptation.md.
 */
#ifndef QUILLA_QFR_MAIN_H_
#define QUILLA_QFR_MAIN_H_

#include "py32f0xx_hal.h"

/* Host console. Keep in sync with the QBOOT arch/cpu reconfiguration. */
#define QFR_USART            USART1
#define QFR_BAUDRATE         115200U
#define QFR_RX_RING_SIZE     512U   /* power of two; CIRCULAR DMA never re-arms */
#define QFR_LINE_MAX         64U

#define QFR_APP_NAME         "Quilla.FR"
#define QFR_APP_VERSION      "V1.0.0001"

extern UART_HandleTypeDef g_uart;

/* Fatal bring-up error: report on the console and stop (no silent hang). */
void app_error_handler(const char *where);

#endif /* QUILLA_QFR_MAIN_H_ */
