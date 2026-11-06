# 架构

## 两个镜像，一颗 MCU

| | QBOOT | appliance（APP） |
| --- | --- | --- |
| 源码 | `quilla/`（上游 fork） | `appliance/` |
| 运行时 | Quilla 协作式进程，无堆 | FreeRTOS，抢占式，`heap_4` |
| Flash | `0x08000000` .. `0x08005FFF` | `0x08007000` .. `0x0801FFFF` |
| 串口驱动 | `quilla/arch/cpu/arm/py32f040xb/shellport.c` | `appliance/Src/recver.c` + `py32f040_hal_msp.c` |
| 控制台 | QBOOT 交互 shell + QIAP 二进制协议 | 行命令（`part`、`version`、`swqboot`） |

两个镜像都使用 **USART1 / PA9 TX / PA10 RX / DMA1_Channel3**，波特率 115200。因此一个主机串口连接器同时承担正常运行与固件升级。

## 分层边界

```
quilla/os/services/quilla-qboot/   启动策略：QIAP、manifest、防回滚、验签、跳转。与 MCU 无关。
quilla/arch/cpu/arm/<mcu>/         时钟、串口、DMA、GPIO、Flash、中断向量。换板只需改这一层。
quilla/user/<mcu>/                 构建 target（CHECK / SEALED / SEC）。
```

改控制台串口不需要动启动策略。本演示的全部硬件适配就是四个文件：`arch/cpu` 下三个，另加 Keil QBOOT 工程文件。详见 `doc/qboot-adaptation.md`。

APP 侧采用同样的划分：`appliance/Src/recver.c` 负责协议，`py32f040_hal_msp.c` 负责引脚与 DMA 通道。

## 零代码耦合

两个镜像互不引用对方的头文件或源文件。QBOOT 是未改动的上游启动策略加一份 `arch/cpu` USART1 补丁；appliance 是一个可独立编译的普通 FreeRTOS 工程。双方只共享一组**二进制约定**，各自独立实现：

| 约定 | QBOOT 侧 | appliance 侧 |
| --- | --- | --- |
| 分区 | `0x08000000` / META `0x08006000` / APP `0x08007000` | 链接基址 `0x08007000`，VTOR `0x7000` |
| META 记录 | QMTL + QMF1，256 B，CRC32 | `app_qboot_meta.c` 用本地定义的常量写 `flag=QBOOT` |
| secver | 读镜像偏移 `0x400` 处的 `QAV1` + 版本号 | `qfr_secver.h` / `app_qboot_secver.c` 生成同样的 8 字节 |
| 串口 | USART1 PA9/PA10，115200 | 同一组引脚与波特率，CIRCULAR DMA 环形 |
| 主频 | 48 MHz（HSI 24 MHz + PLL×2） | 72 MHz（HSI 24 MHz + PLL×3，2 个 Flash 等待周期） |

主频刻意不同。QBOOT 跳转时不复位 RCC，因此 `app_clock_init()` 先调用 `HAL_RCC_DeInit()` 清掉遗留状态，再建立 72 MHz 时钟树（HSI 24 MHz + PLL×3，`FLASH_LATENCY_2`），并把 `SystemCoreClock` 置为 72 MHz。两边各自按自己的 PCLK 计算 115200（72 MHz 下 BRR 恰为 625）。QBOOT 的时钟设置一行未改。

## 应用任务

APP 与 Quilla 自带 QAPP 承担相同的两项工作，只是用 FreeRTOS 任务替代 Contiki 进程：

| Quilla QAPP | FreeRTOS appliance |
| --- | --- |
| `project_process` + 1 s `etimer` 翻转 LED3 | `led_task` + `vTaskDelay(1000 ms)` 显式置 LED3 ON / OFF |
| `shellport` 送数据给行 shell | `recver_task` 取 CIRCULAR DMA 环形数据并解析命令行 |

板上 LED 低电平点亮（RESET = ON，SET = OFF）。`led_task` 直接写电平而非翻转，每次复位后状态都是确定的。两个任务都在 `main()` 中于 `vTaskStartScheduler()` 之前创建；LED 任务优先级更低，GPIO 操作不会拖延串口接收。

LED 在 **PB2**，与上游 demo 板一致。`led.c` 在本地定义 `QFR_LED_PIN`：appliance 自带的 BSP 头写的是 `GPIO_PIN_7`，那是另一个 WS2812 灯带工程从未使用过的残留值；换板时在构建期定义 `QFR_LED_PIN` 即可，不改 MCU SDK 头文件。

## 接收链路（APP）

```
USART1 RX -- DMA1_Channel3（CIRCULAR，512 B 环形，永不重装）
          -- USART IDLE 中断 -- 任务通知
          -- recver 任务：按实时 CNDTR 取环形数据，拼装可打印行
          -- "swqboot"：写 META flag=QBOOT，随后 NVIC_SystemReset()
```

不启用 DMA TC/HT 中断。CIRCULAR 模式下 HAL 的接收完成回调会在第一次回绕时清掉 `CR3.DMAR`，导致接收静默停止。短命令用 IDLE 分割即可；实际固件传输期间链路由 QBOOT 接管。

## 启动与升级时序

1. QBOOT 读 META 日志；manifest 有效（CHECK 仅比 SHA-256）则跳到 `0x08007000`，否则停留 shell。
2. APP 把 VTOR 重定位到 `0x7000`，用 `app_clock_init()` 复位 RCC 并建立 72 MHz 时钟树，开中断，启动两个任务，打印 `[QAPP] ...`。时钟这一步不能省：QBOOT 留下的是 48 MHz PLL，而本 HAL 用 `SystemCoreClock` 变量（而非实时时钟树）计算串口波特率。
3. 上位机发 `part` → APP 回答 `app1`；上位机发 `swqboot`。
4. APP 挂起调度器，追加一条 `flag=QBOOT` 的 META 记录后复位。
5. QBOOT 见 `flag=QBOOT` 停留在 shell；上位机发 `qiap`，执行 ENTER / DATA / END [/ SIGN]。
6. QBOOT 写 manifest、置 `flag=APP1`、送完成码后复位进入新 APP。完成判定以 ACK 为准，`[QAPP]` 戳只是非致命的二次确认。

## 安全边界

CHECK 只校验传输完整性（SHA-256）。SEALED / SEC 增加 Ed25519 真实性校验与防回滚，要求 APP 镜像在偏移 `0x400` 带 `.quilla_secver` 区。Quilla 仓库自带的默认密钥对是公开的，出货前必须替换。
