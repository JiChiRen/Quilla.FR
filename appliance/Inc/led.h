/* LED heart-beat task. FreeRTOS equivalent of Quilla QAPP's `project_process`:
 * drives the demo-board LED (PB2) ON / OFF once per second. Uses no Quilla
 * headers; the pin is defined in led.c so MCU SDK headers stay untouched. */
#ifndef QUILLA_QFR_LED_H_
#define QUILLA_QFR_LED_H_

void led_gpio_init(void);
void led_task(void *arg);

#endif /* QUILLA_QFR_LED_H_ */
