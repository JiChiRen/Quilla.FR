# Quilla.FR

A minimal reference design showing **Quilla QBOOT** booting a **FreeRTOS
appliance** instead of Quilla's own Contiki-based APP.

It runs on the same PY32F040 demo board as upstream Quilla, with the same LED
on PB2. Only the APP side changes: the runtime (Contiki processes -> FreeRTOS
tasks), the core clock (48 MHz -> 72 MHz) and the console UART (USART2 ->
USART1, reconfigured in QBOOT's `arch/cpu` layer so both images share one
connector). QBOOT's boot policy and the partition layout are unchanged.

The point of the demo is the boundary between the two images:

- **Zero code coupling.** QBOOT includes no appliance header or source, and the
  appliance includes no Quilla header or source. They agree only on a binary
  contract: partition addresses, META record layout, the 8-byte secver region
  at image offset `0x400`, and a 115200 console on USART1. `appliance/`
  compiles standalone in a directory with no `quilla/` next to it.
- **Different core clocks.** QBOOT keeps upstream's 48 MHz (HSI 24 MHz + PLL
  x2). The appliance runs at 72 MHz (HSI 24 MHz + PLL x3, 2 Flash wait
  states). Each image derives 115200 baud from its own PCLK; 72 MHz gives an
  exact BRR of 625.
- **Same jobs, different runtime.** Quilla QAPP's Contiki `project_process`
  (1 s LED heart-beat) becomes the FreeRTOS `led_task`; its line shell becomes
  `recver_task`. See `doc/architecture.md`.

QBOOT's console UART lives in `quilla/arch/cpu/arm/<mcu>/`, not in the boot
policy. Reconfiguring that one layer is enough to put QBOOT and a third-party
RTOS APP on the same connector. Everything under `quilla/os/` is untouched,
and no QBOOT clock setting was changed for this demo.

## Layout

| Path | Contents |
| --- | --- |
| `quilla/` | Independent clone of `github.com/JiChiRen/Quilla` (branch `develop`). Not tracked by this repository. |
| `appliance/` | Standalone FreeRTOS APP: PY32F040 HAL subset, FreeRTOS core, USART1 CIRCULAR-DMA receiver, heart-beat LED. No Quilla headers or sources |
| `appliance/gcc/`, `appliance/MDK-ARM/` | Two build routes for the same APP: GNU Arm GCC makefile and Keil uVision (ARM Compiler 5) |
| `tools/` | `quilla-usart1.patch` (the complete arch/cpu reconfiguration), `configure_quilla_usart1.py` (apply / check / revert) and `qboot_image.py` (factory image packer) |
| `doc/` | Architecture, QBOOT adaptation and image-packing guides (EN / zh-CN) |

## FreeRTOS configuration

`appliance/Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOSConfig.h` is
the only configuration, shared by the GCC and Keil builds. `FreeRTOS.h` includes
it with quotes, so this copy next to `FreeRTOS.h` takes precedence over the
compiler include paths. Clock (72 MHz), tick rate, heap and task options are
all set in that file.

## Flash layout (PY32F040xB, 128 KiB)

| Region | Start | Size |
| --- | --- | --- |
| QBOOT | `0x08000000` | 24 KiB |
| META | `0x08006000` | 4 KiB (dual 2 KiB slots, 256 B records) |
| APP | `0x08007000` | 100 KiB |

## Quick start

```
git clone -b develop git@github.com:JiChiRen/Quilla.git quilla
python tools/configure_quilla_usart1.py           # patch quilla/arch/cpu in place
python tools/configure_quilla_usart1.py --check   # verify the patch state

# GCC Arm Embedded toolchain, EIDE makefiles (CHECK shown; .sealed / .sec too)
make -C quilla/user/py32f040xb/EIDE -f Makefile.qboot
make -C appliance/gcc
```

For the APP, either build route is valid. Keil uVision uses ARM Compiler 5:
open `appliance/MDK-ARM/Quilla.FR.uvprojx` and press F7, or run
`UV4.exe -b Quilla.FR.uvprojx -j0 -o build.log` from that directory.
See `appliance/MDK-ARM/README.md`.

Build all three QBOOT targets (CHECK / SEALED / SEC) before packing a factory
image:

```
python tools/qboot_image.py pack \
    --boot <QBOOT_CHECK.bin> \
    --app  appliance/gcc/build/quilla-fr-app.bin \
    --out  combined.bin
python tools/qboot_image.py verify --image combined.bin
```

Flash `combined.bin` at `0x08000000` with a full-chip erase. On power-up the
board prints `[QAPP] Quilla.FR V1.0.0001` and blinks the user LED.

Serial upgrade from the running APP:

```
python quilla/tools/quilla_host_updater.py COM7 \
    appliance/gcc/build/quilla-fr-app.bin
```

- **CHECK** variant: no `--key`. The completion code is `ACK_END 0x83`.
- **SEALED / SEC** variants: `--key quilla/tools/_keytest/private.hex`.
  The completion code is `ACK_SIGN 0x84`.

The host probes the APP with `part` (this APP answers `app1`) and then sends
`swqboot`; the APP writes `flag=QBOOT` into the META log and resets.

## Keeping the fork in sync

`quilla/` is an ordinary clone with `origin` pointing at upstream. The patch
leaves four modified files inside that fork (three under `arch/cpu`, plus the
Keil QBOOT project), so revert before pulling, then re-apply. QBOOT never
references anything outside `quilla/arch/cpu/`:

```
python tools/configure_quilla_usart1.py --revert
git -C quilla pull
python tools/configure_quilla_usart1.py
```

`--check` reports the recorded upstream HEAD, whether the patch is applied,
and that `shellport.c` binds USART1 / PA9-PA10 / DMA1_Channel3. `git -C quilla status` shows exactly which
`arch/cpu` and build files the reconfiguration changed. Use `--revert` to
restore all of them. `tools/quilla-usart1.patch` is a plain git diff and can
be reviewed line by line.

## Status and limits

- Built with `arm-none-eabi-gcc` 13.2: appliance `text 11208 / data 24 /
  bss 13344`; QBOOT CHECK 21280 B, SEALED 21376 B, SEC 21328 B (all below the
  24 KiB budget, all built with the USART1 wiring). QBOOT is
  still 48 MHz; the appliance is 72 MHz.
- The linker ASSERT for the initial MSP passes. The APP binary passes vector,
  secver and pack/verify checks; a `combined.bin` produced from the real
  QBOOT_CHECK binary verifies cleanly.
- The Keil MDK-ARM project (`appliance/MDK-ARM/Quilla.FR.uvprojx`, AC5) also
  builds with 0 errors / 0 warnings: bin 11108 B (`Output/Quilla.FR.bin`), MSP `0x20003218`, reset
  `0x08007151`, secver `QAV1` v1; its binary passes pack/verify too.
- Decoupling was verified by copying `appliance/` alone into a temporary
  directory with no `quilla/` beside it and rebuilding successfully (12072 B).
  Both build systems have include paths only inside `appliance/`.
- `app_clock_init()` in `appliance/Src/main.c` resets RCC and builds a 72 MHz
  tree (HSI 24 MHz + PLL x3, `FLASH_LATENCY_2`) before any UART setup. The HAL
  derives the UART baud rate from the `SystemCoreClock` variable, so leaving a
  48 MHz clock with a stale reset-value variable produces ~6x baud and a
  garbled boot stamp.
- **Hardware check (2026-11-06, PY32F040 + QBOOT_SEC).** A serial capture shows
  `[BOOT] Quilla V0.11.0002 [SEC]`, then `[QAPP] Quilla.FR V1.0.0001` with
  a `>` prompt; sending `swqboot` produced `[QAPP] switching to QBOOT...` and
  a fresh `[BOOT] ... [SEC]` banner. This confirms the 72 MHz clock tree, the
  115200 console, the SEC signature check, the APP handover and the META
  `flag=QBOOT` trigger on hardware. The LED task was observed driving the
  board LED (PB2) with a 1 s ON / 1 s OFF beat. SEC re-verifies Ed25519 on
  every boot, so ~20 s of serial silence after `[BOOT]` is expected.
- Full QIAP upgrade cycle verified on hardware (2026-11-06, GUI updater,
  Keil 72 MHz image, 11108 B, signed): two consecutive upgrades both reached
  `ACK_SIGN 0x84`, and the serial capture shows the APP -> QBOOT switch
  (`[QAPP] switching to QBOOT...` -> `[BOOT] ... [SEC]`) before the second
  upgrade, closing the QBOOT -> APP -> QBOOT loop. Note: the updater reports
  "no boot banner received" - the appliance banner is composed at runtime, so
  the stamp string extracted from the image (`[QAPP] switching to QBOOT...`)
  never appears after reset. The warning is expected and non-fatal; the
  upgrade itself is confirmed by ACK_SIGN.
- The repository default keypair (`quilla/tools/_keytest/`) is public. Use it
  for development only; replace it and rebuild QBOOT before any shipment.
- CHECK verifies transport integrity only. It is not secure boot.
