# Porting the factory image geometry to another MCU

**[中文](mcu-porting.zh-CN.md)**

`tools/qboot_image.py` builds a full-chip image (QBOOT + META + APP). Its
partition constants fall into three groups. Porting to a new MCU means
changing **only the MCU-specific group** and then updating the matching
definitions on the QBOOT side and the APP side. This document is the checklist.

The same tool is used in `Quilla.ZP` (GD32F502VE). Both configurations are
listed below so they can be compared directly.

## 1. Constants in `tools/qboot_image.py`

### MCU-specific — must be changed

| Constant | Meaning | Quilla.FR (PY32F040xB) | Quilla.ZP (GD32F502VE) |
| --- | --- | --- | --- |
| `FLASH_BASE` | Flash base address / combined image load address | `0x08000000` | `0x08000000` |
| `FLASH_SIZE` | Total Flash size; **also the exact byte length of `combined.bin`** | `0x00020000` (128 KiB) | `0x00080000` (512 KiB) |
| `APP_SIZE` | APP region length; bounds `image_len` | `0x00019000` (100 KiB) | `0x00079000` (484 KiB) |
| `RAM_START` | SRAM base; lower bound of the initial MSP check | `0x20000000` | `0x20000000` |
| `RAM_SIZE` | SRAM size; **upper bound of the initial MSP check** | `0x00004000` (16 KiB) | `0x00010000` (64 KiB) |

Two relations must hold:

```
APP_SIZE   = FLASH_SIZE - (APP_START - FLASH_BASE) = FLASH_SIZE - 0x7000
SOC_FLASH_END (QBOOT soc.h) = FLASH_BASE + FLASH_SIZE - 1
```

`FLASH_SIZE` is not merely documentation: the tool allocates
`bytearray(b"\xFF" * FLASH_SIZE)` and `verify_combined()` rejects an image of
any other length. A wrong value produces a truncated or oversized factory
image. `RAM_SIZE` is equally load-bearing — `validate_app()` rejects
`SP >= RAM_START + RAM_SIZE`, matching QBOOT's
`quilla_qboot_hal_app_image_valid()`.

### Layout constants — only change if the partition map itself changes

| Constant | Value | Note |
| --- | --- | --- |
| `QBOOT_SIZE` | `0x00006000` (24 KiB) | QBOOT image budget, fixed by the QBOOT linker script |
| `META_BASE` | `0x08006000` | Immediately after QBOOT |
| `META_SIZE` | `0x00001000` (4 KiB) | Two 2 KiB slots |
| `APP_START` | `0x08007000` | Must be erase-page aligned |
| `META_OFFSET` / `APP_OFFSET` | derived | `*_BASE - FLASH_BASE`; update if `FLASH_BASE` changes |

These four are **identical in Quilla.FR and Quilla.ZP**. Keeping them fixed is
what lets QBOOT stay MCU-independent at the policy layer. If you move them,
change all of the following together: QBOOT build defines, the APP linker
script, the META writer in the APP, and `--meta-base` / `--app-start` on the
host. `tools/qboot_image.py` offers both flags for a custom layout.

### Protocol constants — do not change

`RECORD_SIZE` (256 B), `META_SLOT_SIZE` (2 KiB), `SECVER_OFFSET` (`0x400`),
`SECVER_MAGIC` (`QAV1`), the QMTL/QMF1 magics and field offsets, CRC-32
(IEEE 802.3 reflected), flag values, manifest hash/signature layout, and the
36-byte signed descriptor. These form the binary contract with QBOOT and are
independent of the MCU.

## 2. Where the same numbers must be repeated

Changing the constants above is not enough. Five places must agree.

| Layer | File / symbol | What to set |
| --- | --- | --- |
| QBOOT | `quilla/arch/cpu/arm/<mcu>/soc.h` → `SOC_FLASH_END` | `FLASH_BASE + FLASH_SIZE - 1`. QBOOT derives `APP_END` from it and fails with `#error` if it is missing. |
| QBOOT | build defines `QUILLA_QBOOT_RAM_START`, `QUILLA_QBOOT_RAM_SIZE` | Must equal `RAM_START` / `RAM_SIZE` in the tool. PY32 uses the 16 KiB default; GD32 builds pass `-DQUILLA_QBOOT_RAM_SIZE=0x00010000U`. |
| QBOOT | build define `QUILLA_QBOOT_PAGE_SIZE` | Physical erase page: **256 B** on PY32F040, **2 KiB** on GD32F502VE. Drives QIAP page coalescing and the META append strategy. |
| QBOOT | build defines `QUILLA_QBOOT_APP_START`, `QUILLA_QBOOT_FLAG_ADDR`, `QUILLA_QBOOT_APP_END` | Match `APP_START` / `META_BASE` / `SOC_FLASH_END`. |
| Host packer | `tools/qboot_image.py` constants above | `FLASH_SIZE`, `APP_SIZE`, `RAM_SIZE`, `FLASH_BASE`. |
| APP (GCC) | `appliance/gcc/py32f040xb.ld` (FR) | `FLASH ORIGIN = 0x08007000`, `LENGTH = APP_SIZE`; `RAM ORIGIN = RAM_START`, `LENGTH = RAM_SIZE`. |
| APP (Keil) | `appliance/MDK-ARM/*.uvprojx` | IROM1 = `0x08007000` / `APP_SIZE`; IRAM1 = `RAM_START` / `RAM_SIZE`. Build with `QAPP_BUILD`. |
| APP (Zephyr) | `app/prj.conf`, `app/boards/**/*.dts`, `app/Kconfig` (ZP) | `CONFIG_FLASH_LOAD_OFFSET=0x7000`, `CONFIG_FLASH_LOAD_SIZE=APP_SIZE`; DTS `reg` for flash/SRAM; Kconfig geometry. |
| APP | META writer page constant | FR: `QB_PAGE_SIZE` in `appliance/Src/app_qboot_meta.c` (256). ZP: `QZP_ERASE_PAGE_SIZE` in `app/src/quilla_meta.c` (2048) with `BUILD_ASSERT(slot == page)`. |
| APP | secver section | `APP_START + 0x400`, emitted by every APP build and covered by the signature. |

## 3. Porting procedure

1. From the datasheet, record Flash base/size, SRAM base/size and the
   **physical erase page size**.
2. Set `FLASH_BASE`, `FLASH_SIZE`, `RAM_START`, `RAM_SIZE` in the tool;
   recompute `APP_SIZE = FLASH_SIZE - 0x7000`.
3. Add or fix `SOC_FLASH_END` in the new `arch/cpu/arm/<mcu>/soc.h`, and set
   the QBOOT build defines listed above for every QBOOT target (CHECK /
   SEALED / SEC, GCC and Keil).
4. Update the APP linker script and Keil/IROM settings, or the Zephyr
   `FLASH_LOAD_*` plus board DTS. Keep the secver record at `+0x400`.
5. Update the APP META writer's erase-page constant. On GD32 the 2 KiB page
   equals one META slot; on PY32 the 256 B page equals one record. The
   append-only rule is "never erase the active slot", so a mismatch silently
   corrupts the log.
6. Build QBOOT and APP, then verify before flashing.

## 4. Verification commands

```
python tools/qboot_image.py selftest
python tools/qboot_image.py pack --boot <QBOOT.bin> --app <APP.bin> --out combined.bin
python tools/qboot_image.py verify --image combined.bin
```

The Quilla.ZP repository additionally ships `tools/check_contract.py`, which
compares the geometry, META layout, flag values, secver magic and console
wiring against both upstream clones (22 checks). Run it after every upstream
pull.

Also check with `nm`/`objdump` on the APP ELF: the initial MSP lies inside
`[RAM_START, RAM_START + RAM_SIZE)`, the reset vector is a Thumb address inside
the APP region, and the `QAV1` record sits at `APP_START + 0x400`.

## 5. Clock handover is a separate concern

Geometry is not the only MCU-specific item. QBOOT jumps **without resetting
RCC**, and the HAL derives the UART baud rate from the `SystemCoreClock`
variable. Quilla.FR therefore rebuilds a 72 MHz clock tree in APP startup
(QBOOT runs at 48 MHz); Quilla.ZP declares 160 MHz in its board DTS to match
QBOOT. Any new port must confirm that the inherited clock and the APP's baud
calculation agree, otherwise the boot stamp arrives as garbage even though the
upgrade succeeded.
