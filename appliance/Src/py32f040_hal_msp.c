/*
 * HAL MSP for the appliance: USART1 + CIRCULAR RX DMA only.
 *
 * This is the APP-side twin of quilla/arch/cpu/arm/py32f040xb/shellport.c.
 * Both images must drive the same pins/channel, otherwise the host cannot use
 * one serial connector for both the running APP and the QBOOT upgrade.
 */
#include "main.h"
#include "py32f0xx_hal.h"

extern DMA_HandleTypeDef g_dma_rx;

void
HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef gpio = {0};

  if(huart->Instance != QFR_USART) {
    return;
  }

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_DMA_CLK_ENABLE();

  /* PA9 = USART1_TX */
  gpio.Pin       = GPIO_PIN_9;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_PULLUP;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF1_USART1;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* PA10 = USART1_RX. The pull-up is mandatory: with a ring DMA that never
   * stops, a floating RX line samples noise as start bits forever. */
  gpio.Pin       = GPIO_PIN_10;
  gpio.Mode      = GPIO_MODE_AF_OD;
  gpio.Pull      = GPIO_PULLUP;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF1_USART1;
  HAL_GPIO_Init(GPIOA, &gpio);

  g_dma_rx.Instance                 = DMA1_Channel3;
  g_dma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  g_dma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
  g_dma_rx.Init.MemInc              = DMA_MINC_ENABLE;
  g_dma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  g_dma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
  g_dma_rx.Init.Mode                = DMA_CIRCULAR;
  g_dma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
  if(HAL_DMA_Init(&g_dma_rx) != HAL_OK) {
    return;
  }
  __HAL_LINKDMA(huart, hdmarx, g_dma_rx);
  HAL_DMA_ChannelMap(&g_dma_rx, DMA_CHANNEL_MAP_USART1_RD);

  HAL_NVIC_SetPriority(USART1_IRQn, 8, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}
