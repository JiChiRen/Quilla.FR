/*
 * LED heart-beat: the FreeRTOS counterpart of Quilla's own QAPP demo.
 *
 * quilla/user/<mcu>/Src/main.c runs `project_process`, a Contiki process with
 * a 1 s etimer that toggles the board LED. This file does the same job with
 * one FreeRTOS task and vTaskDelay(1 s); only the scheduler differs.
 *
 * Board LED pin: upstream Quilla QAPP uses PB2 (its BSP defines LED3_PIN as
 * GPIO_PIN_2). The BSP header shipped with this appliance says GPIO_PIN_7,
 * but the original appliance project never called any BSP_LED_* function
 * (it drove a WS2812 string over SPI), so that value is unverified. Define
 * QFR_LED_PIN at build time if the LED sits on a different pin; MCU SDK
 * headers are deliberately left untouched.
 *
 * The LED is active-low: driving the pin low turns it ON. Explicit ON/OFF
 * writes keep the state defined after every reset.
 */
#include "main.h"
#include "led.h"
#include "FreeRTOS.h"
#include "task.h"

#include "py32f040xx_Start_Kit.h"

#define QFR_LED_PORT GPIOB
#ifndef QFR_LED_PIN
#define QFR_LED_PIN  GPIO_PIN_2
#endif

#define LED_ON_LEVEL    GPIO_PIN_RESET
#define LED_OFF_LEVEL   GPIO_PIN_SET
#define LED_BEAT_MS     1000U

static void
led_set(int on)
{
  HAL_GPIO_WritePin(QFR_LED_PORT, QFR_LED_PIN,
                    on ? LED_ON_LEVEL : LED_OFF_LEVEL);
}

void
led_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  gpio.Pin   = QFR_LED_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(QFR_LED_PORT, &gpio);

  led_set(0);                             /* defined OFF state after reset */
}

void
led_task(void *arg)
{
  int on = 0;

  (void)arg;

  for(;;) {
    led_set(on);                                /* ON, OFF, ON, OFF ... */
    on = !on;
    vTaskDelay(pdMS_TO_TICKS(LED_BEAT_MS));     /* 1 s beat, as in QAPP */
  }
}
