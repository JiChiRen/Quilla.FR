# 把出厂镜像几何配置移植到另一颗 MCU

**[English](mcu-porting.md)**

`tools/qboot_image.py` 负责合成整片镜像（QBOOT + META + APP）。其中的分区常量
分为三类。移植到新 MCU 时只需修改**芯片相关组**，然后同步 QBOOT 侧与 APP 侧的
对应定义。本文即移植清单。

`Quilla.ZP`（GD32F502VE）使用同一个工具，下面同时列出两套配置以便直接对照。

## 1. `tools/qboot_image.py` 中的常量

### 芯片相关 —— 必须修改

| 常量 | 含义 | Quilla.FR（PY32F040xB） | Quilla.ZP（GD32F502VE） |
| --- | --- | --- | --- |
| `FLASH_BASE` | Flash 基址 / 整片镜像烧录地址 | `0x08000000` | `0x08000000` |
| `FLASH_SIZE` | Flash 总容量；**同时决定 `combined.bin` 的精确字节长度** | `0x00020000`（128 KiB） | `0x00080000`（512 KiB） |
| `APP_SIZE` | APP 区长度，约束 `image_len` | `0x00019000`（100 KiB） | `0x00079000`（484 KiB） |
| `RAM_START` | SRAM 基址，初始 MSP 校验下界 | `0x20000000` | `0x20000000` |
| `RAM_SIZE` | SRAM 容量，**初始 MSP 校验上界** | `0x00004000`（16 KiB） | `0x00010000`（64 KiB） |

必须满足两个关系：

```
APP_SIZE   = FLASH_SIZE - (APP_START - FLASH_BASE) = FLASH_SIZE - 0x7000
SOC_FLASH_END（QBOOT soc.h） = FLASH_BASE + FLASH_SIZE - 1
```

`FLASH_SIZE` 不只是注释：工具按 `bytearray(b"\xFF" * FLASH_SIZE)` 分配，
`verify_combined()` 会拒绝任何其它长度的镜像。填错会产出被截断或超长的出厂
镜像。`RAM_SIZE` 同样关键——`validate_app()` 拒绝
`SP >= RAM_START + RAM_SIZE`，与 QBOOT 的
`quilla_qboot_hal_app_image_valid()` 一致。

### 分区常量 —— 仅在改动分区布局时才改

| 常量 | 值 | 说明 |
| --- | --- | --- |
| `QBOOT_SIZE` | `0x00006000`（24 KiB） | QBOOT 镜像预算，由 QBOOT 链接脚本固定 |
| `META_BASE` | `0x08006000` | 紧接 QBOOT 之后 |
| `META_SIZE` | `0x00001000`（4 KiB） | 双 2 KiB slot |
| `APP_START` | `0x08007000` | 必须按擦除页对齐 |
| `META_OFFSET` / `APP_OFFSET` | 推导值 | `*_BASE - FLASH_BASE`；`FLASH_BASE` 改变时同步 |

这四个常量在 Quilla.FR 与 Quilla.ZP 中**完全相同**。正是保持不变，才让 QBOOT
的策略层与 MCU 无关。若要移动分区，必须同时修改：QBOOT 构建宏、APP 链接脚本、
APP 内 META 写入模块，以及上位机的 `--meta-base` / `--app-start`。
`tools/qboot_image.py` 提供这两个参数用于自定义布局。

### 协议常量 —— 禁止修改

`RECORD_SIZE`（256 B）、`META_SLOT_SIZE`（2 KiB）、`SECVER_OFFSET`（`0x400`）、
`SECVER_MAGIC`（`QAV1`）、QMTL/QMF1 魔数与字段偏移、CRC-32（IEEE 802.3
反射形式）、flag 取值、manifest 的哈希/签名布局，以及 36 字节签名描述子。
它们构成与 QBOOT 的二进制契约，与 MCU 无关。

## 2. 同一组数值必须重复出现的位置

只改上面的常量不够，以下五处必须一致。

| 层 | 文件 / 符号 | 要设置的值 |
| --- | --- | --- |
| QBOOT | `quilla/arch/cpu/arm/<mcu>/soc.h` → `SOC_FLASH_END` | `FLASH_BASE + FLASH_SIZE - 1`；QBOOT 由此推导 `APP_END`，缺失时直接 `#error` |
| QBOOT | 构建宏 `QUILLA_QBOOT_RAM_START`、`QUILLA_QBOOT_RAM_SIZE` | 必须与工具的 `RAM_START` / `RAM_SIZE` 相等。PY32 用 16 KiB 默认值；GD32 构建传 `-DQUILLA_QBOOT_RAM_SIZE=0x00010000U` |
| QBOOT | 构建宏 `QUILLA_QBOOT_PAGE_SIZE` | 物理擦除页：PY32F040 为 **256 B**，GD32F502VE 为 **2 KiB**。决定 QIAP 页合并与 META 追加策略 |
| QBOOT | 构建宏 `QUILLA_QBOOT_APP_START`、`QUILLA_QBOOT_FLAG_ADDR`、`QUILLA_QBOOT_APP_END` | 与 `APP_START` / `META_BASE` / `SOC_FLASH_END` 一致 |
| 上位机打包 | `tools/qboot_image.py` 上述常量 | `FLASH_SIZE`、`APP_SIZE`、`RAM_SIZE`、`FLASH_BASE` |
| APP（GCC） | `appliance/gcc/py32f040xb.ld`（FR） | `FLASH ORIGIN = 0x08007000`、`LENGTH = APP_SIZE`；`RAM ORIGIN = RAM_START`、`LENGTH = RAM_SIZE` |
| APP（Keil） | `appliance/MDK-ARM/*.uvprojx` | IROM1 = `0x08007000` / `APP_SIZE`；IRAM1 = `RAM_START` / `RAM_SIZE`；带 `QAPP_BUILD` |
| APP（Zephyr） | `app/prj.conf`、`app/boards/**/*.dts`、`app/Kconfig`（ZP） | `CONFIG_FLASH_LOAD_OFFSET=0x7000`、`CONFIG_FLASH_LOAD_SIZE=APP_SIZE`；DTS 的 flash/SRAM `reg`；Kconfig 分区几何 |
| APP | META 写入模块的擦除页常量 | FR：`appliance/Src/app_qboot_meta.c` 的 `QB_PAGE_SIZE`（256）；ZP：`app/src/quilla_meta.c` 的 `QZP_ERASE_PAGE_SIZE`（2048），并带 `BUILD_ASSERT(slot == page)` |
| APP | secver 段 | `APP_START + 0x400`，每个 APP 构建都要产出，且被签名覆盖 |

## 3. 移植步骤

1. 从数据手册记录 Flash 基址/容量、SRAM 基址/容量，以及**物理擦除页大小**。
2. 在工具中设置 `FLASH_BASE`、`FLASH_SIZE`、`RAM_START`、`RAM_SIZE`，重算
   `APP_SIZE = FLASH_SIZE - 0x7000`。
3. 在新 `arch/cpu/arm/<mcu>/soc.h` 补齐 `SOC_FLASH_END`，并为每个 QBOOT
   target（CHECK / SEALED / SEC，GCC 与 Keil）设置上表所列构建宏。
4. 更新 APP 链接脚本与 Keil IROM 设置，或 Zephyr 的 `FLASH_LOAD_*` 与板级
   DTS；secver 记录保持在 `+0x400`。
5. 更新 APP META 写入模块的擦除页常量。GD32 的 2 KiB 页恰好等于一个 META
   slot；PY32 的 256 B 页恰好等于一条记录。追加规则是「活动 slot 永不擦除」，
   页大小不一致会静默写坏日志。
6. 编译 QBOOT 与 APP，烧录前先校验。

## 4. 验证命令

```
python tools/qboot_image.py selftest
python tools/qboot_image.py pack --boot <QBOOT.bin> --app <APP.bin> --out combined.bin
python tools/qboot_image.py verify --image combined.bin
```

Quilla.ZP 额外提供 `tools/check_contract.py`，对两个上游 clone 比对分区几何、
META 布局、flag 取值、secver 魔数与串口接线共 22 项。每次拉取上游后都应重跑。

另外用 `nm` / `objdump` 检查 APP ELF：初始 MSP 落在
`[RAM_START, RAM_START + RAM_SIZE)` 内，复位向量是 APP 区内的 Thumb 地址，
`QAV1` 记录位于 `APP_START + 0x400`。

## 5. 时钟交接是另一件事

几何不是唯一的芯片相关项。QBOOT **不复位 RCC** 直接跳转，而 HAL 用
`SystemCoreClock` 变量计算串口波特率。因此 Quilla.FR 在 APP 启动时重建
72 MHz 时钟树（QBOOT 为 48 MHz）；Quilla.ZP 在板级 DTS 声明 160 MHz 与
QBOOT 对齐。新移植必须确认继承的时钟与 APP 的波特率推导一致，否则即使升级
成功，启动横幅也会是乱码。
