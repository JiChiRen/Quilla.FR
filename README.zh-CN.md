# Quilla.FR

最小演示工程：用 **Quilla QBOOT** 启动一个基于 **FreeRTOS** 的 appliance，而不是 Quilla 自带的 Contiki APP。

硬件仍是上游 Quilla 那块 PY32F040 demo 板，LED 在 PB2。变的只是 APP 侧：运行时（Contiki 进程 → FreeRTOS 任务）、主频（48 MHz → 72 MHz）、控制台串口（USART2 → USART1；QBOOT 侧同步在 `arch/cpu` 层重配置，两个镜像共用一个连接器）。QBOOT 的启动策略与分区布局不变。

演示的重点是两个镜像之间的边界：

- **零代码耦合**。QBOOT 不引用 appliance 的任何头文件或源文件，appliance 也不引用 Quilla 的任何头文件或源文件。两者只约定一组二进制接口：分区地址、META 记录格式、镜像偏移 `0x400` 的 8 字节 secver 区、USART1 上的 115200 串口。把 `appliance/` 单独复制到没有 `quilla/` 的目录，仍能独立编译。
- **主频不同**。QBOOT 保持上游的 48 MHz（HSI 24 MHz + PLL×2）；appliance 跑 72 MHz（HSI 24 MHz + PLL×3，2 个 Flash 等待周期）。两边各自按自己的 PCLK 计算 115200 波特率，72 MHz 下 BRR 恰为 625，共用串口仍然互通。
- **职责相同，运行时不同**。Quilla QAPP 的 Contiki `project_process`（1 秒 LED 心跳）对应 FreeRTOS 的 `led_task`；行 shell 对应 `recver_task`。详见 `doc/architecture.md`。

QBOOT 的串口位于 `quilla/arch/cpu/arm/<mcu>/`，不属于启动策略层。只重配置这一层，就能让 QBOOT 与第三方 RTOS APP 共用同一个串口连接器；`quilla/os/` 下所有文件保持不变，本演示也没有改动 QBOOT 的任何时钟设置。

## 目录

| 路径 | 内容 |
| --- | --- |
| `quilla/` | `github.com/JiChiRen/Quilla` 的独立 clone（`develop` 分支），不纳入本仓库版本管理 |
| `appliance/` | 可独立编译的 FreeRTOS APP：PY32F040 HAL 子集、FreeRTOS 核心、USART1 环形 DMA 接收、LED 心跳；不含任何 Quilla 头文件或源文件 |
| `appliance/gcc/`、`appliance/MDK-ARM/` | 同一 APP 的两条构建路径：GNU Arm GCC Makefile 与 Keil uVision（ARM Compiler 5） |
| `tools/` | `quilla-usart1.patch`（arch/cpu 重配置的完整补丁）、`configure_quilla_usart1.py`（应用 / 检查 / 还原）与 `qboot_image.py`（出厂镜像打包） |
| `doc/` | 架构、QBOOT 适配、镜像打包说明（中英双语） |

## FreeRTOS 配置

两条构建路径共用唯一配置：`appliance/Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOSConfig.h`。`FreeRTOS.h` 以引号包含该文件，同目录副本优先于编译器的 include 路径；主频（72 MHz）、tick、堆大小与任务选项都在此文件修改。

## 分区（PY32F040xB，128 KiB）

| 区域 | 起始 | 大小 |
| --- | --- | --- |
| QBOOT | `0x08000000` | 24 KiB |
| META | `0x08006000` | 4 KiB（双 2 KiB slot，记录 256 B） |
| APP | `0x08007000` | 100 KiB |

`tools/qboot_image.py` 中的容量按 PY32F040xB（128 KiB Flash / 16 KiB SRAM）
固化。移植到其它 MCU 时，常量表、QBOOT 与 APP 侧必须同步修改的位置，以及与
Quilla.ZP（GD32F502VE）的对照，见 [doc/mcu-porting.zh-CN.md](doc/mcu-porting.zh-CN.md)。

## 快速开始

```
git clone -b develop git@github.com:JiChiRen/Quilla.git quilla
python tools/configure_quilla_usart1.py           # 就地给 quilla/arch/cpu 打补丁
python tools/configure_quilla_usart1.py --check   # 核对补丁状态

# 需 GNU Arm Embedded 工具链；EIDE Makefile，此处示例为 CHECK
make -C quilla/user/py32f040xb/EIDE -f Makefile.qboot
make -C appliance/gcc
```

APP 任选一条构建路径。Keil uVision 使用 ARM Compiler 5：打开 `appliance/MDK-ARM/Quilla.FR.uvprojx` 按 F7，或在该目录执行 `UV4.exe -b Quilla.FR.uvprojx -j0 -o build.log`。详见 `appliance/MDK-ARM/README.md`。

用 `Makefile.qboot.sealed` / `Makefile.qboot.sec` 编译另外两个 QBOOT 变体，再合成出厂镜像：

```
python tools/qboot_image.py pack \
    --boot <QBOOT_CHECK.bin> \
    --app  appliance/gcc/build/quilla-fr-app.bin \
    --out  combined.bin
python tools/qboot_image.py verify --image combined.bin
```

整片擦除后把 `combined.bin` 烧到 `0x08000000`。上电后打印 `[QAPP] Quilla.FR V1.0.0001`，LED 闪烁。

从运行中的 APP 串口升级：

```
python quilla/tools/quilla_host_updater.py COM7 \
    appliance/gcc/build/quilla-fr-app.bin
```

- **CHECK** 变体：不带 `--key`，完成码为 `ACK_END 0x83`。
- **SEALED / SEC** 变体：带 `--key quilla/tools/_keytest/private.hex`，完成码为 `ACK_SIGN 0x84`。

上位机先用 `part` 探测（本 APP 回答 `app1`），再发 `swqboot`；APP 把 `flag=QBOOT` 追加写入 META 后复位。

## 同步上游

`quilla/` 是普通 clone，`origin` 指向上游。补丁会在 fork 内留下 4 个已修改文件（`arch/cpu` 下三个，另加 Keil QBOOT 工程文件），因此先还原再拉取，最后重放。QBOOT 只引用 `quilla/arch/cpu/` 下的文件，不依赖任何外部目录：

```
python tools/configure_quilla_usart1.py --revert
git -C quilla pull
python tools/configure_quilla_usart1.py
```

`--check` 报告记录的上游 HEAD、补丁是否完整，并核验 `shellport.c` 绑定 USART1 / PA9-PA10 / DMA1_Channel3；`git -C quilla status` 可看到具体改了哪些文件。`--revert` 可全部还原。`tools/quilla-usart1.patch` 是普通 git diff，可逐行审阅。

## 状态与限制

- 已用 `arm-none-eabi-gcc` 13.2 编译通过：appliance `text 11208 / data 24 / bss 13344`；QBOOT CHECK 21280 B、SEALED 21376 B、SEC 21328 B，均在 24 KiB 预算内，三个变体均按 USART1 接线编译。QBOOT 仍为 48 MHz，appliance 为 72 MHz。
- 初始 MSP 的链接期 ASSERT 通过；APP bin 通过向量表、secver、打包与校验检查，用真实 QBOOT_CHECK bin 合成的 `combined.bin` 校验通过。
- Keil MDK-ARM 工程（`appliance/MDK-ARM/Quilla.FR.uvprojx`，AC5）同样编译通过，0 errors / 0 warnings：bin 11108 B（`Output/Quilla.FR.bin`）、MSP `0x20003218`、复位向量 `0x08007151`、secver `QAV1` v1，其产物也通过打包与校验。
- 解耦已实测：把 `appliance/` 单独复制到旁边没有 `quilla/` 的临时目录，仍能编译出 12072 B 的 bin；两个构建系统的 include 路径全部落在 `appliance/` 内部。
- `appliance/Src/main.c` 的 `app_clock_init()` 在串口初始化前复位 RCC，再建立 72 MHz 时钟树（HSI 24 MHz + PLL×3，`FLASH_LATENCY_2`）。HAL 用 `SystemCoreClock` 变量计算波特率；若继承 48 MHz 时钟而变量仍是复位值，波特率会变成约 6 倍，启动横幅即为乱码。
- **真机验证（2026-11-06，PY32F040 + QBOOT_SEC）**：串口依次抓到 `[BOOT] Quilla V0.11.0002 [SEC]`、`[QAPP] Quilla.FR V1.0.0001` 与 `>` 提示符；发送 `swqboot` 后回 `[QAPP] switching to QBOOT...` 并重新打印 `[BOOT] ... [SEC]`。这坐实了 72 MHz 时钟树、115200 串口、SEC 验签、APP 跳转与 META `flag=QBOOT` 触发链路；板上 LED（PB2）由 `led_task` 驱动，实测亮 1 秒 / 灭 1 秒。SEC 每次启动都跑 Ed25519，`[BOOT]` 之后约 20 秒无输出属正常。
- **完整 QIAP 升级闭环已真机验证（2026-11-06，GUI 上位机，Keil 72 MHz 镜像 11108 B，带签名）**：连续两次升级均 `ACK_SIGN 0x84`；第二次升级前串口实抓到 APP→QBOOT 切换（`[QAPP] switching to QBOOT...` → `[BOOT] ... [SEC]`），QBOOT→APP→QBOOT 闭环成立。上位机提示「未收到启动横幅」属预期：该镜像的开机 banner 是运行时拼接的，镜像里唯一完整的 `[QAPP]` 字面量是 `switching to QBOOT...`，复位后不会出现；升级成功以 ACK_SIGN 为准（CR-0047 起启动戳仅为二次确认）。
- 仓库默认密钥对（`quilla/tools/_keytest/`）是公开的，仅供开发验证；出货前必须换成自有密钥并重建 QBOOT。
- CHECK 只校验传输完整性，不构成安全启动。
