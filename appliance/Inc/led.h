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
/* LED heart-beat task. FreeRTOS equivalent of Quilla QAPP's `project_process`:
 * drives the demo-board LED (PB2) ON / OFF once per second. Uses no Quilla
 * headers; the pin is defined in led.c so MCU SDK headers stay untouched. */
#ifndef QUILLA_QFR_LED_H_
#define QUILLA_QFR_LED_H_

void led_gpio_init(void);
void led_task(void *arg);

#endif /* QUILLA_QFR_LED_H_ */
