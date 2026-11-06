/*
 * Quilla.FR appliance - minimal FreeRTOS APP behind Quilla QBOOT.
 *
 * Scope is deliberately the same as Quilla's own QAPP demo (heart-beat LED,
 * console, "swqboot" upgrade trigger), but the runtime is FreeRTOS instead of
 * Contiki. Three things make this image bootable by QBOOT:
 *   1. link origin 0x08007000 and VTOR offset 0x7000 (gcc/py32f040xb.ld,
 *      system_py32f040.c under QAPP_BUILD);
 *   2. a .quilla_secver region at image offset 0x400 (app_qboot_secver.c);
 *   3. initial MSP strictly below the RAM end (ASSERT in the linker script).
 */
#include "main.h"
#include "recver.h"
#include "led.h"
#include "FreeRTOS.h"
#include "task.h"

/* The security-version region lives in app_qboot_secver.c and is defined by
 * the appliance-owned qfr_secver.h. It is covered by the QBOOT digest. */

void
app_error_handler(const char *where)
{
  recver_puts("\r\nERR: ");
  recver_puts(where != NULL ? where : "unknown");
  recver_puts("\r\n");
  for(;;) {
  }
}

/* The appliance deliberately runs at a DIFFERENT frequency from QBOOT.
 *
 * QBOOT (upstream Quilla) runs this MCU at 48 MHz (HSI 24 MHz + PLL x2) and
 * jumps here without resetting RCC. This appliance runs at 72 MHz
 * (HSI 24 MHz + PLL x3, the device maximum, with 2 Flash wait states).
 * The bootloader and a customer RTOS need not share a clock tree, a header,
 * or a source file.
 *
 * HAL_RCC_DeInit() first discards whatever QBOOT left behind: it selects
 * HSI, turns the PLL off, sets SystemCoreClock = HSI_VALUE and re-arms
 * SysTick. The 72 MHz tree is then built from scratch. This matters because
 * this HAL's HAL_RCC_GetHCLKFreq() returns the *variable* SystemCoreClock,
 * from which HAL_UART_Init() derives BRR - an inherited 48 MHz clock with an
 * 8 MHz variable would give ~6x the configured baud rate. Each image derives
 * 115200 from its own PCLK (72 MHz gives an exact BRR of 625).
 *
 * Runs before the UART and the scheduler: no interrupt or peripheral is live. */
static void
app_clock_init(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  if(HAL_RCC_DeInit() != HAL_OK) {
    for(;;) {                  /* UART not up yet: no place to report */
    }
  }

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI |
                       RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE;
  osc.HSIState = RCC_HSI_ON;
  osc.HSIDiv = RCC_HSI_DIV1;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
  osc.HSEState = RCC_HSE_OFF;
  osc.LSIState = RCC_LSI_OFF;
  osc.LSEState = RCC_LSE_OFF;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  osc.PLL.PLLMUL = RCC_PLL_MUL3;          /* 24 MHz x 3 = 72 MHz */
  if(HAL_RCC_OscConfig(&osc) != HAL_OK) {
    for(;;) {
    }
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                  RCC_CLOCKTYPE_PCLK1;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  if(HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
    for(;;) {
    }
  }

  /* HAL_RCC_GetPCLK1Freq() -> HAL_RCC_GetHCLKFreq() -> this variable. */
  SystemCoreClock = 72000000U;
}

int
main(void)
{
  HAL_Init();                 /* arms SysTick before the PLL switch */
  app_clock_init();           /* 72 MHz, before any UART baud calculation */

  /* QBOOT hands over with PRIMASK possibly masked. HAL_Init() needs SysTick
   * running before any HAL_Delay(), and the scheduler needs interrupts too. */
  __enable_irq();

  led_gpio_init();

  if(recver_init() != 0) {
    app_error_handler("recver_init");
  }

  /* Boot stamp. Quilla's host updater looks for "[QAPP]" as a best-effort
   * post-upgrade confirmation. The completion verdict is QBOOT's ACK, not this
   * line; see tools/quilla_host_updater.py in the quilla tree. */
  recver_puts("\r\n[QAPP] " QFR_APP_NAME " " QFR_APP_VERSION "\r\n> ");

  (void)xTaskCreate(led_task,    "led",    128, NULL, 2, NULL);
  (void)xTaskCreate(recver_task, "recver", 192, NULL, 3, NULL);

  vTaskStartScheduler();

  /* Only reachable when the scheduler cannot start (heap exhausted). */
  app_error_handler("scheduler");
  return 0;
}
