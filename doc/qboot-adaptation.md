# Adapting Quilla QBOOT to the Quilla.FR board

Upstream Quilla uses USART2 / PA2-PA3 / DMA1_Channel6 on its reference board.
This board exposes USART1 / PA9-PA10 / DMA1_Channel3, which the FreeRTOS
appliance already owns. QBOOT must use the same pins.

**Quilla keeps all MCU-specific code in `arch/cpu/arm/<mcu>/`.** The boot
policy in `os/services/quilla-qboot/` is hardware-independent and is not
modified. This makes the adaptation a board-porting task, not a bootloader
change.

## 1. Clone and reconfigure

```
git clone -b develop git@github.com:JiChiRen/Quilla.git quilla
python tools/configure_quilla_usart1.py
python tools/configure_quilla_usart1.py --check
```

`tools/quilla-usart1.patch` is a plain git diff against upstream. The script
applies it **in place**; QBOOT keeps compiling
`quilla/arch/cpu/arm/py32f040xb/` exactly as upstream does. No directory
outside the fork is a build dependency.

| Change | Files |
| --- | --- |
| USART1 hardware selection | `quilla/arch/cpu/arm/py32f040xb/shellport.c`, `shellport.h`, `quilla-qboot-hal.c` |
| Keil auto-bin (AfterMake `fromelf`) | `user/py32f040xb/MDK-ARM/QBOOT.uvprojx` |

Inside those three arch/cpu files, the USART1 wiring is:

| Item | Appliance board |
| --- | --- | --- |
| USART | USART1 |
| TX / RX | PA9 / PA10 |
| DMA RX | DMA1_Channel3, `USART1_RD` |
| IRQs | `USART1_IRQHandler`, `DMA1_Channel2_3_IRQHandler` |
| Handover cleanup | USART1 / DMA1_Channel3 |


### IRQ vector names and SDK files

`SHELLPORT_USART_IRQ` / `SHELLPORT_DMA_IRQ` are C preprocessor macros, not new
vector names. They appear as `void SHELLPORT_USART_IRQ(void)` in `shellport.c`,
so preprocessing produces the hardware symbols `USART1_IRQHandler` and
`DMA1_Channel2_3_IRQHandler`. The QBOOT startup files already list both pairs
in the vector table; **no startup file, CMSIS header or HAL driver is modified**.
`arm-none-eabi-nm` on a built QBOOT shows those two as strong `T` symbols and
the unused USART2 vector as weak `W`; the map file attributes them to
`shellport.o`.

The appliance follows the same rule: its GCC and Keil startup files are
byte-identical to the vendor copies, and `py32f040_it.c` defines the strong
`USART1_IRQHandler`. When porting to another board, keep MCU SDK files
untouched and change only `arch/cpu`.

**No build defines are involved.** The USART1 wiring is baked into the
`shellport.c` macro block, so every QBOOT target (Keil or EIDE) picks it up
automatically; there is no per-target define to forget. When porting to
another board, change the macro block and the matching hand-over cleanup in
`quilla-qboot-hal.c`, then regenerate the patch.

## 2. Build QBOOT

Build CHECK, SEALED and SEC from `quilla/user/py32f040xb/`. After switching
Keil targets, use "Rebuild all target files": the three targets share an
object directory and an incremental build can link a mixed-variant binary.
Confirm the banner string (`[CHECK]` / `[SEALED]` / `[SEC]`) inside the binary
before packing.

## 3. APP-side requirements

| Requirement | Where |
| --- | --- |
| Link origin `0x08007000`, length `0x19000` | `appliance/gcc/py32f040xb.ld` |
| `VECT_TAB_OFFSET = 0x7000` under `QAPP_BUILD` | `appliance/Src/system_py32f040.c` |
| Initial MSP strictly below `0x20004000` | `._user_heap_stack` provides `_estack`; linker ASSERT enforces it |
| secver at `0x08007400` (magic `QAV1` + version) | `appliance/Inc/qfr_secver.h` + `app_qboot_secver.c`, both appliance-owned; fixed section in the linker script |
| META flag writer | `appliance/Src/app_qboot_meta.c`, binary-compatible with Quilla's QMTL/QMF1 record |

The secver region is inside the signed image. SEALED / SEC refuse an image
without it (QIAP SIGN returns `RC_VER`). CHECK does not read it.

Two build routes are provided for the APP; both must honour the table above:

| Route | Toolchain | FreeRTOS port | secver placement |
| --- | --- | --- | --- |
| `appliance/gcc/Makefile` | GNU Arm GCC | `portable/GCC/ARM_CM0` | `.quilla_secver` pinned by `py32f040xb.ld` |
| `appliance/MDK-ARM/Quilla.FR.uvprojx` | Keil ARM Compiler 5 | `portable/RVDS/ARM_CM0` | `.ARM.__at_0x08007400` placed by armlink |

The Keil target sets IROM1 to `0x08007000` / `0x19000`, defines only
`USE_HAL_DRIVER,PY32F040xB,QAPP_BUILD`, and keeps every include path inside
`appliance/`. Its post-build step runs `fromelf` to produce
`Output/Quilla.FR.bin`. See `appliance/MDK-ARM/README.md`.

Neither build system reaches into `quilla/`. The secver region, META record
constants and partition addresses are defined locally in the appliance;
copying `appliance/` to a directory with no Quilla checkout still builds.
This is the whole point of the demo: QBOOT safely boots a customer RTOS
without sharing a single header or source file with it.

### Clock handover: deliberately different frequencies

QBOOT runs at 48 MHz (HSI 24 MHz + PLL x2) and jumps without resetting RCC.
The appliance runs at **72 MHz** (HSI 24 MHz + PLL x3, the device maximum,
with `FLASH_LATENCY_2`). `app_clock_init()` first calls `HAL_RCC_DeInit()`
to discard QBOOT's clock state, then configures HSI 24 MHz + PLL x3 and sets
`SystemCoreClock = 72000000U`. `configCPU_CLOCK_HZ` is 72 MHz in both build
routes.

The reset is mandatory for a second reason: this HAL's
`HAL_RCC_GetHCLKFreq()` returns the *variable* `SystemCoreClock`, and
`HAL_UART_Init()` derives BRR from it. Inheriting a 48 MHz clock with a stale
reset-value variable yields roughly 6x the configured baud rate, so the
`[QAPP]` boot stamp arrives as garbage even though the upgrade succeeded.
Each image computes 115200 from its own PCLK (72 MHz gives an exact BRR of
625), so the shared console still works. No QBOOT clock setting was changed.

## 4. Protocol compatibility

Quilla's host updater expects a running APP to answer `part` with a string
containing `app1`, and to accept the text command `swqboot`. `recver.c`
implements exactly that. A binary ENTER frame must not be sent into this APP
shell: its printable bytes would pollute the line buffer. The host updater
already avoids that when `part` answers `app1`.

On `swqboot` the APP suspends the scheduler, appends a META record carrying
`flag=QBOOT` (manifest, seal and floor are inherited unchanged), and resets
only if the Flash write succeeded.

## 5. Upgrade commands

CHECK has no public key compiled in; END commits and the completion code is
`ACK_END 0x83`. SEALED / SEC compile a public key; END only acknowledges and
SIGN must follow, with completion code `ACK_SIGN 0x84`.

```
# CHECK
python quilla/tools/quilla_host_updater.py COM7 \
    appliance/gcc/build/quilla-fr-app.bin

# SEALED / SEC
python quilla/tools/quilla_host_updater.py COM7 \
    appliance/gcc/build/quilla-fr-app.bin \
    --key quilla/tools/_keytest/private.hex
```

Passing `--key` on a CHECK build sends a SIGN frame after the device has
already rebooted into the APP and times out; omitting it on SEALED / SEC leaves
the image uncommitted. Match the argument to the QBOOT variant.

## 6. After an upstream sync

The patch leaves four modified files in the fork's working tree, so a plain
`git pull` can conflict. Use this order:

```
python tools/configure_quilla_usart1.py --revert   # restore upstream files
git -C quilla pull
python tools/configure_quilla_usart1.py           # re-apply the patch
python tools/configure_quilla_usart1.py --check
```

`--check` reports the recorded upstream HEAD. If upstream changed those
`arch/cpu` files, `git apply` refuses and asks you to review the diff and
regenerate `tools/quilla-usart1.patch` rather than silently overwriting.
