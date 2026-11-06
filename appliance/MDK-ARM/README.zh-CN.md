# Keil MDK-ARM 工程（appliance）

`Quilla.FR.uvprojx` 与 `../gcc/Makefile` 编译同一份 FreeRTOS APP，工具链为 **ARM Compiler 5**（armcc v5，`uAC6 = 0`）与 Puya PY32F0xx DFP。器件：`PY32F040CxB`，128 KiB Flash / 16 KiB RAM。

## 关键配置

| 项目 | 取值 |
| --- | --- |
| IROM1 | `0x08007000`，大小 `0x19000`（QBOOT + META 之后） |
| IRAM1 | `0x20000000`，大小 `0x4000` |
| 宏定义 | `USE_HAL_DRIVER,PY32F040xB,QAPP_BUILD` |
| 分散加载 | `.\Objects\Quilla.FR.sct`（Keil 自动生成） |
| FreeRTOS port | `portable/RVDS/ARM_CM0`（不是 GCC 版） |
| 构建后 | `fromelf --bin -o "$L\@L.bin" "#L"` → `Output/Quilla.FR.bin` |

`QAPP_BUILD` 在 `../Src/system_py32f040.c` 中把 VTOR 重定位到 `0x7000`。

### FreeRTOS 配置

`../Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOSConfig.h` 是唯一配置，与 GCC 构建共用。`FreeRTOS.h` 以引号包含该文件，同目录副本优先于 `../Inc`；主频、tick 与堆大小都在此修改。

### secver 由 appliance 自行定义

`../Inc/qfr_secver.h` 是 appliance 自有的头文件，不引用任何 Quilla 头文件，因此 armcc v5 不需要任何兼容宏。`QFR_SECVER_PLACE()` 把 8 字节 `QAV1` 区放进 `.ARM.__at_0x08007400` 段，armlink 按绝对地址放置，不需要手工编辑 scatter，Keil 重新生成 `.sct` 也不会丢。GCC 则由同一头文件改用 `.quilla_secver` 段名，由 `../gcc/py32f040xb.ld` 固定地址。该区位于签名覆盖的镜像范围内，是 SEALED / SEC 的必需项。

所有 include 路径都落在 `appliance/` 内部。把本目录复制到没有 Quilla 仓库的位置，仍能编译。

### 时钟交接：刻意使用不同主频

QBOOT 保持上游的 **48 MHz**（HSI 24 MHz + PLL×2），本 appliance 跑 **72 MHz**（HSI 24 MHz + PLL×3，`FLASH_LATENCY_2`）。QBOOT 跳转前不复位 RCC，因此 `app_clock_init()` 在 `HAL_Init()` 之后先调用 `HAL_RCC_DeInit()`，再从头建立 72 MHz 时钟树，并把 `SystemCoreClock` 设为 `72000000U`；`configCPU_CLOCK_HZ` 也是 72 MHz。两边都没有为对方改动时钟设置。

这次复位还修掉一个 HAL 特性：`HAL_RCC_GetHCLKFreq()` 返回的是变量 `SystemCoreClock`，`HAL_UART_Init()` 用它算 BRR。若继承 48 MHz 时钟而变量仍是复位值，波特率会变成约 6 倍，`[QAPP]` 横幅即为乱码。两边各自按自己的 PCLK 计算 115200（72 MHz 下 BRR 恰为 625），共用串口仍然互通。

## 构建

用 uVision 打开 `Quilla.FR.uvprojx` 按 F7，或命令行：

```
UV4.exe -b Quilla.FR.uvprojx -j0 -o build.log
```

产物：`Output/Quilla.FR.bin`。首次使用 Keil 会生成 `.uvoptx`、`Objects/`、`Output/`、`DebugConfig/`、`RTE/`，均已被 git 忽略。

## 已验证的构建

`UV4 -b` 完成，**0 errors、0 warnings**：`Code=10548 RO-data=444 RW-data=120 ZI-data=12704`，bin 11112 B。二进制中 MSP `0x20003218`（< `0x20004000`，QBOOT 允许跳转）、复位向量 `0x08007151`、偏移 `0x400` 处 `QAV1` 版本 1。用真实 `QBOOT_CHECK.bin` 打包并跑 `tools/qboot_image.py verify` 均通过。

**真机验证（2026-11-06）**：PY32F040 + QBOOT_SEC 下，72 MHz appliance 正常打印 `[QAPP] Quilla.FR V1.0.0001`，发送 `swqboot` 后回到 QBOOT banner，板上 LED（PB2）实测亮 1 秒 / 灭 1 秒。该镜像的完整 QIAP 升级循环仍待验证。

## 不要混用工具链

GCC 工程链接 `portable/GCC/ARM_CM0/port.c`，Keil 工程链接 `portable/RVDS/ARM_CM0/port.c`。两边中间目录要分开。两个工程编译的是同一套 `../Src` 与 `../Inc` 源文件，include 路径全部落在 `appliance/` 内部。
