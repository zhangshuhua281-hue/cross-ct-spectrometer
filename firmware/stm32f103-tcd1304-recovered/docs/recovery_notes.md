# 固件恢复说明

## 来源

恢复依据是当前实物 STM32F103C8T6 读出的完整 Flash：

- `tcd1304_mcu_flash_64k.bin`
- `tcd1304_mcu_flash_64k.hex`

反汇编与分析文件包括中断向量、函数反汇编和恢复版伪代码。当前开源仓库只保留整理后的恢复版源码和说明。

## 与旧工程的关系

旧 Keil 工程的 `OBJ/USART.hex` 和当前实物单片机里的固件不同。

从旧工程源码看，它更像是通过 USART2 向另一个采集模块请求数据的程序；而实物固件本身使用 ADC1、DMA1 Channel1、TIM2/TIM3/TIM4/TIM1 和 USART1，直接完成 TCD1304 采集和数据返回。

所以本目录没有直接覆盖旧工程，而是单独整理恢复版源码，避免把两个不同固件混在一起。

## 已确认硬件/外设

| 项目 | 恢复结果 |
|---|---|
| MCU | STM32F103C8T6，Flash 64KB |
| 串口 | USART1，921600 8N1 |
| ADC | ADC1 |
| DMA | DMA1 Channel1 |
| 定时器 | TIM1、TIM2、TIM3、TIM4 均有使用 |
| 中断 | USART1、DMA1 Channel1、TIM2 |
| ADC DMA 源地址 | `0x4001244C`，即 ADC1 数据寄存器 |
| DMA 目标缓冲 | RAM `0x20000064` |
| 发送工作缓冲 | RAM `0x20001D40` |
| 原始 DMA 点数 | `0xE6E`，即 3694 点 |
| 实际发送像素 | 3648 点 |
| 发送起始偏移 | 跳过原始缓冲前 32 点 |

## 未完全还原的部分

这些内容在 HEX 里可以看到寄存器行为，但没有原始语义名，需要用示波器或实物电路继续确认：

- PA8、PA0、PB0、PB9 分别对应 TCD1304 的哪个具体时序脚。
- TIM1/TIM2/TIM3/TIM4 之间的触发关系。
- `timing_a`、`timing_b`、`timing_c` 三个参数在原始源码中的变量名。
- 长积分时间下，传感器暗电流、饱和、时钟稳定性对采集结果的影响。

## 后续改源码的建议

如果要继续修改积分时间，优先改 `src/tcd1304_firmware_recovered.c` 里的 `kIntegrationPresets` 表。

如果要把恢复版变成可重新编译的 Keil 工程，建议步骤是：

1. 复制旧 Keil 工程为新目录，不要覆盖原目录。
2. 把 `src/tcd1304_firmware_recovered.c` 拆成 `main.c`、`tcd1304.c`、`tcd1304.h`。
3. 在工程中加入 `stm32f10x_adc.c`、`stm32f10x_dma.c`、`stm32f10x_tim.c`。
4. 用 STM32 标准外设库实现源码中的 `board_*` 底层函数。
5. 编译后先在示波器上确认 TCD1304 时序，再接上位机采集。

