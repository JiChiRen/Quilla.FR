# Architecture

## Two images, one MCU

| | QBOOT | appliance (APP) |
| --- | --- | --- |
| Source | `quilla/` (upstream fork) | `appliance/` |
| Runtime | Quilla cooperative processes, no heap | FreeRTOS, preemptive, `heap_4` |
| Flash | `0x08000000` .. `0x08005FFF` | `0x08007000` .. `0x0801FFFF` |
| Serial driver | `quilla/arch/cpu/arm/py32f040xb/shellport.c` | `appliance/Src/recver.c` + `py32f040_hal_msp.c` |
| Console | interactive QBOOT shell + QIAP binary protocol | line commands (`part`, `version`, `swqboot`) |

Both images drive **USART1 / PA9 TX / PA10 RX / DMA1_Channel3** at 115200.
A single host connector therefore serves normal operation and firmware update.

## The layer boundary

```
quilla/os/services/quilla-qboot/   boot policy: QIAP, manifest, anti-rollback,
                                   signature, image jump. MCU-independent.
quilla/arch/cpu/arm/<mcu>/         clock, UART, DMA, GPIO, Flash, vectors.
                                   The ONLY layer changed for a new board.
quilla/user/<mcu>/                 build targets (CHECK / SEALED / SEC).
```

Changing the console UART does not require editing the boot policy. In this
demo the whole hardware adaptation is four files: three under `arch/cpu` and
the Keil QBOOT project file. See `doc/qboot-adaptation.md`.

The same separation holds on the APP side: `appliance/Src/recver.c` owns the
serial protocol, `py32f040_hal_msp.c` owns pins and DMA channels.

## Zero code coupling

Neither image includes a header or a source file from the other. QBOOT is the
unmodified upstream policy plus one `arch/cpu` USART1 patch; the appliance is
an ordinary customer FreeRTOS project that compiles on its own. What they
share is a **binary contract**, implemented independently on each side:

| Contract | QBOOT side | Appliance side |
| --- | --- | --- |
| Partition | `0x08000000` / META `0x08006000` / APP `0x08007000` | link origin `0x08007000`, VTOR `0x7000` |
| META record | QMTL + QMF1, 256 B, CRC32 | `app_qboot_meta.c` writes `flag=QBOOT` with locally defined constants |
| secver | reads `QAV1` + version at image offset `0x400` | `qfr_secver.h` / `app_qboot_secver.c` emit the same 8 bytes |
| Console | USART1 PA9/PA10, 115200 | same pins and baud, CIRCULAR DMA ring |
| Core clock | 48 MHz (HSI 24 MHz + PLL x2) | 72 MHz (HSI 24 MHz + PLL x3, 2 Flash WS) |

Core clocks are deliberately different. QBOOT jumps without resetting RCC, so
`app_clock_init()` calls `HAL_RCC_DeInit()` and then builds the 72 MHz tree
(HSI 24 MHz + PLL x3, `FLASH_LATENCY_2`), setting `SystemCoreClock` to
72 MHz. Each image derives 115200 baud from its own PCLK (BRR = 72000000 / 115200 = 625,
exact). No QBOOT clock setting was changed.

## Application tasks

The APP has the same two jobs as Quilla's own QAPP, implemented as FreeRTOS
tasks instead of Contiki processes:

| Quilla QAPP | FreeRTOS appliance |
| --- | --- |
| `project_process` + 1 s `etimer` toggles LED3 | `led_task` + `vTaskDelay(1000 ms)` drives LED3 ON / OFF |
| `shellport` feeds the line shell | `recver_task` drains the CIRCULAR DMA ring and parses lines |

The board LED is active-low (RESET = ON, SET = OFF). `led_task` writes the
level explicitly rather than toggling it, so its state is defined after every
reset. Both tasks are created in `main()` before `vTaskStartScheduler()`; the
LED task runs at the lower priority so GPIO work cannot delay serial reception.

The LED is on **PB2**, exactly as on the upstream demo board. `led.c` defines
`QFR_LED_PIN` locally: the BSP header copied with this appliance carries
`GPIO_PIN_7`, an unverified leftover from a WS2812-string project that never
used the board LED. Define `QFR_LED_PIN` at build time for another board;
MCU SDK headers are not modified.

## Receive path (APP)

```
USART1 RX -- DMA1_Channel3 (CIRCULAR, 512 B ring, never re-armed)
          -- USART IDLE interrupt -- task notification
          -- recver task: drain ring via live CNDTR, assemble printable lines
          -- "swqboot": write META flag=QBOOT, then NVIC_SystemReset()
```

No DMA TC/HT interrupt is enabled. In CIRCULAR mode the HAL's receive-complete
callback would otherwise clear `CR3.DMAR` on the first wrap and silently stop
reception. The IDLE interrupt is sufficient for short host commands; QBOOT owns
the link during the actual firmware transfer.

## Boot and update sequence

1. QBOOT reads the META log. With a valid manifest (CHECK: SHA-256 only) it
   jumps to `0x08007000`; otherwise it stays in its shell.
2. APP relocates VTOR to `0x7000`, resets RCC and builds a 72 MHz tree
   (`app_clock_init()`), enables interrupts, starts two tasks and prints
   `[QAPP] ...`. The clock step is mandatory: QBOOT leaves a 48 MHz PLL
   running, and this HAL derives the UART baud rate from the
   `SystemCoreClock` variable rather than the live clock tree.
3. Host sends `part` -> APP answers `app1`; host sends `swqboot`.
4. APP suspends the scheduler, appends a META record with `flag=QBOOT`, resets.
5. QBOOT sees `flag=QBOOT`, keeps its shell; the host sends `qiap` and runs
   ENTER / DATA / END [/ SIGN].
6. QBOOT writes the manifest, sets `flag=APP1`, sends the completion ACK and
   reboots into the new APP. The ACK is the completion verdict; the `[QAPP]`
   stamp is only a non-fatal confirmation.

## Security boundary

CHECK verifies transport integrity (SHA-256) only. SEALED and SEC add Ed25519
authentication and anti-rollback, and require the APP image to carry the
`.quilla_secver` region at offset `0x400`. The default keypair shipped with
Quilla is public; replace it before shipping hardware.
