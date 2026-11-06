/*
 * recver - USART1 CIRCULAR-DMA ring receiver.
 *
 * The DMA writes into a fixed ring forever. The USART IDLE interrupt only
 * notifies the recver task; harvesting and line parsing happen in task context,
 * never in the ISR. No DMA TC/HT interrupt is enabled, so the HAL UART state
 * machine can never clear CR3.DMAR and silently stop reception on a wrap.
 */
#ifndef QUILLA_QFR_RECVER_H_
#define QUILLA_QFR_RECVER_H_

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* Configure USART1 + DMA and start the circular ring. Returns 0 on success. */
int  recver_init(void);

/* Blocking console transmit (polling, safe before the scheduler starts). */
void recver_puts(const char *s);

/* IDLE ISR hook: wake the recver task. */
void recver_notify_from_isr(BaseType_t *higher_priority_task_woken);

/* FreeRTOS task: drain the ring, assemble printable lines, dispatch commands. */
void recver_task(void *arg);

#endif /* QUILLA_QFR_RECVER_H_ */
