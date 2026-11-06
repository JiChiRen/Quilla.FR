# 把 Quilla QBOOT 适配到 Quilla.FR 板卡

上游 Quilla 参考板使用 USART2 / PA2-PA3 / DMA1_Channel6；本板卡只有 USART1 / PA9-PA10 / DMA1_Channel3，且 FreeRTOS appliance 已占用该串口。QBOOT 必须用同一组引脚。

**Quilla 把全部 MCU 相关代码放在 `arch/cpu/arm/<mcu>/`。** `os/services/quilla-qboot/` 中的启动策略与硬件无关，不作修改。因此这是板级移植，不是改 bootloader。

## 1. Clone 与重配置

```
git clone -b develop git@github.com:JiChiRen/Quilla.git quilla
python tools/configure_quilla_usart1.py
python tools/configure_quilla_usart1.py --check
```

`tools/quilla-usart1.patch` 是一份针对上游的普通 git diff。脚本**就地**应用该补丁；QBOOT 仍按上游方式直接编译 `quilla/arch/cpu/arm/py32f040xb/`，不依赖 fork 之外的任何目录。

| 改动 | 文件 |
| --- | --- |
| USART1 硬件选择 | `quilla/arch/cpu/arm/py32f040xb/shellport.c`、`shellport.h`、`quilla-qboot-hal.c` |
| Keil 自动出 bin（AfterMake `fromelf`） | `user/py32f040xb/MDK-ARM/QBOOT.uvprojx` |

`arch/cpu` 文件内部通过配置目标板卡USART：

| 项目 | 默认（目标板 USART1）|
| --- | --- | --- |
| 串口 | USART1 |
| TX / RX | PA9 / PA10 |
| RX DMA | DMA1_Channel3，`USART1_RD` |
| 中断 | `USART1_IRQHandler`、`DMA1_Channel2_3_IRQHandler` |
| 跳转前清理 | USART1 / DMA1_Channel3 |


### 中断向量名与 MCU SDK 文件

`SHELLPORT_USART_IRQ` / `SHELLPORT_DMA_IRQ` 是 C 预处理宏，**不是新的向量名**。它们以 `void SHELLPORT_USART_IRQ(void)` 的形式出现在 `shellport.c` 中，预处理后生成的仍是硬件符号 `USART1_IRQHandler` 与 `DMA1_Channel2_3_IRQHandler`。QBOOT 的 startup 向量表本来就同时列出这两组向量；**startup、CMSIS 头文件、HAL 驱动均未改动**。对编译产物执行 `arm-none-eabi-nm`，这两个符号是强符号 `T`，未使用的 USART2 向量仍是 weak `W`；map 文件标明它们来自 `shellport.o`。

appliance 侧同理：GCC 与 Keil 两份 startup 与厂商原文件字节级一致，`py32f040_it.c` 定义强符号 `USART1_IRQHandler`。移植到别的板卡时不要改 MCU SDK 文件，只改 `arch/cpu`。

**不涉及任何构建宏。** USART1 接线固化在 `shellport.c` 顶部宏块，三个 QBOOT target（Keil / EIDE）自动生效，不存在漏定义某处导致串口落在 USART2 的问题。移植到其他板卡时，改宏块及 `quilla-qboot-hal.c` 中对应的跳转前清理，然后重新生成 patch。

## 2. 编译 QBOOT

在 `quilla/user/py32f040xb/` 下编译 CHECK、SEALED、SEC。切换 Keil target 后必须 **Rebuild all target files**：三个 target 共用对象目录，增量 Build 可能链接出混用变体的产物。打包前用二进制查看器确认 banner 串（`[CHECK]` / `[SEALED]` / `[SEC]`）。

## 3. APP 侧约束

| 约束 | 位置 |
| --- | --- |
| 链接基址 `0x08007000`，长度 `0x19000` | `appliance/gcc/py32f040xb.ld` |
| `QAPP_BUILD` 下 `VECT_TAB_OFFSET = 0x7000` | `appliance/Src/system_py32f040.c` |
| 初始 MSP 严格小于 `0x20004000` | `._user_heap_stack` 提供 `_estack`，链接脚本 ASSERT 强制检查 |
| secver 固定在 `0x08007400`（magic `QAV1` + 版本号） | `appliance/Inc/qfr_secver.h` 与 `app_qboot_secver.c`，均属 appliance 自有文件；链接脚本固定段 |
| META flag 写入 | `appliance/Src/app_qboot_meta.c`，与 Quilla 的 QMTL/QMF1 记录二进制兼容 |

secver 区位于签名覆盖的镜像范围内。SEALED / SEC 缺少该区时，QIAP SIGN 返回 `RC_VER`；CHECK 不读取。

APP 提供两条构建路径，都必须满足上表约束：

| 路径 | 工具链 | FreeRTOS port | secver 放置 |
| --- | --- | --- | --- |
| `appliance/gcc/Makefile` | GNU Arm GCC | `portable/GCC/ARM_CM0` | `py32f040xb.ld` 固定 `.quilla_secver` 段 |
| `appliance/MDK-ARM/Quilla.FR.uvprojx` | Keil ARM Compiler 5 | `portable/RVDS/ARM_CM0` | armlink 按 `.ARM.__at_0x08007400` 绝对定位 |

Keil target 的 IROM1 为 `0x08007000` / `0x19000`，宏只有 `USE_HAL_DRIVER,PY32F040xB,QAPP_BUILD`，include 路径全部落在 `appliance/` 内部。构建后用 `fromelf` 产出 `Output/Quilla.FR.bin`。详见 `appliance/MDK-ARM/README.md`。

两个构建系统都不会进入 `quilla/` 目录取文件。secver 区、META 记录常量、分区地址都由 appliance 本地定义；把 `appliance/` 复制到没有 Quilla 仓库的目录，仍然可以编译。这正是本演示的目的：QBOOT 安全引导一个客户 RTOS，双方不共享任何头文件或源文件。

### 时钟交接：刻意使用不同主频

QBOOT 跑 48 MHz（HSI 24 MHz + PLL×2），跳转时不复位 RCC；appliance 跑 **72 MHz**（HSI 24 MHz + PLL×3，器件上限，配 `FLASH_LATENCY_2`）。`app_clock_init()` 先用 `HAL_RCC_DeInit()` 清掉 QBOOT 遗留的时钟状态，再配置 HSI 24 MHz + PLL×3，并把 `SystemCoreClock` 设为 `72000000U`。两条构建路径的 `configCPU_CLOCK_HZ` 均为 72 MHz。

这次复位还有第二个必要性：本 HAL 的 `HAL_RCC_GetHCLKFreq()` 返回的是变量 `SystemCoreClock`，`HAL_UART_Init()` 用它算 BRR。若继承 48 MHz 时钟而变量仍是复位值，实际波特率约为配置值的 6 倍，升级明明成功，`[QAPP]` 启动横幅却是乱码。两边各自按自己的 PCLK 计算 115200（72 MHz 下 BRR 恰为 625），共用串口仍然互通。QBOOT 的时钟设置一行未改。

## 4. 协议兼容

Quilla 上位机要求运行中的 APP 对 `part` 回显含 `app1`，并接受文本命令 `swqboot`。`recver.c` 已按此实现。不能向该 APP shell 发送二进制 ENTER 帧：其中可打印字节会污染行缓冲；上位机在 `part` 回答 `app1` 后本来也会跳过该帧。

收到 `swqboot` 后，APP 挂起调度器，追加一条 `flag=QBOOT` 的 META 记录（manifest、seal、floor 原样继承），仅在 Flash 写入成功后才复位。

## 5. 升级命令

CHECK 未编入公钥，END 直接提交，完成码是 `ACK_END 0x83`。SEALED / SEC 编入公钥，END 只确认，必须继续发 SIGN，完成码是 `ACK_SIGN 0x84`。

```
# CHECK
python quilla/tools/quilla_host_updater.py COM7 \
    appliance/gcc/build/quilla-fr-app.bin

# SEALED / SEC
python quilla/tools/quilla_host_updater.py COM7 \
    appliance/gcc/build/quilla-fr-app.bin \
    --key quilla/tools/_keytest/private.hex
```

CHECK 下带 `--key` 会在设备已复位进 APP 后再发 SIGN 帧并超时；SEALED / SEC 下不带 `--key` 则镜像不会被提交。参数必须与 QBOOT 变体一致。

## 6. 同步上游之后

补丁会在 fork 工作树留下 7 个已修改文件，直接 `git pull` 可能冲突。按以下顺序操作：

```
python tools/configure_quilla_usart1.py --revert   # 还原上游文件
git -C quilla pull
python tools/configure_quilla_usart1.py           # 重放补丁
python tools/configure_quilla_usart1.py --check
```

`--check` 会显示记录的上游 HEAD。若上游改动了这些 `arch/cpu` 文件，`git apply` 会直接拒绝并提示先审阅差异、重新生成 `tools/quilla-usart1.patch`，不会静默覆盖。
