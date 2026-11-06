/* Interrupt handler declarations for the appliance. */
#ifndef QUILLA_QFR_PY32F040_IT_H_
#define QUILLA_QFR_PY32F040_IT_H_

void NMI_Handler(void);
void HardFault_Handler(void);
void SysTick_Handler(void);
void USART1_IRQHandler(void);

#endif /* QUILLA_QFR_PY32F040_IT_H_ */
