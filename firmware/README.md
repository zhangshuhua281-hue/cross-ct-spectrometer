# Firmware

## STM32G431 image-control

路径：`stm32g431-image-control`

用途：从 STM32F103/TCD1304 采集板读取 3648 点 12 位数据，完成暗电平估计、中值滤波、Savitzky-Golay 平滑、归一化和下采样，并通过串口输出给 ESP32-S3 显示端。

工程入口：

- Keil MDK: `stm32g431-image-control/MDK-ARM/image-control.uvprojx`
- 应用逻辑：`stm32g431-image-control/App`
- 串口/板级驱动：`stm32g431-image-control/BSP`

## ESP32-S3 monitor

路径：`esp32s3-monitor`

用途：触摸屏监视器，接收 STM32G431 串口输出并显示曲线、分组统计和链路状态。

工程入口：

- Arduino sketch: `esp32s3-monitor/11_stm32_monitor.ino`

## STM32F103 TCD1304 recovered firmware

路径：`stm32f103-tcd1304-recovered`

用途：记录从实物 Flash 反汇编整理出来的 TCD1304 采集固件逻辑、串口命令和积分时间表。

注意：这是恢复版源码，适合理解和移植，不保证直接编译得到与实物导出固件逐字节一致的 HEX。

