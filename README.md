# Cross C-T Spectrometer Prototype

交叉式 C-T 光谱仪原型设计。这个仓库整理了一个低成本光谱仪原型的光学设计、结构件、PCB、嵌入式固件、BOM 和实测数据处理结果，目标是方便复现、验证和继续改进。

本项目是实验原型，不是成熟仪器。当前资料适合学习、二次开发和原型复现；若用于定量测量，需要继续完成波长标定、白参考/暗参考流程、重复性验证和误差评估。

## What is included

- `hardware/optics/zemax`: Zemax 光学设计文件。
- `hardware/pcb`: TCD1304 驱动板、STM32G431 图像数据处理板的原理图和 PCB 工程压缩包。
- `hardware/mechanical/step`: 光谱仪外壳、镜架、隔离板、测试模块等 STEP 结构件。
- `firmware/stm32g431-image-control`: STM32G431 数据处理与显示链路固件，Keil MDK 工程。
- `firmware/esp32s3-monitor`: ESP32-S3 触摸屏监视器 Arduino 工程。
- `firmware/stm32f103-tcd1304-recovered`: STM32F103/TCD1304 采集固件的恢复版源码和串口协议说明。
- `data`: V1.0/V2.0/V3.0 原始采集数据、405 nm/780 nm 标定数据、处理后的 CSV/图像和分析脚本。
- `bom`: 镜片材料采购清单。
- `docs/project-report/project-report.pdf`: 项目报告 PDF。

## Repository layout

```text
.
├── bom/
├── data/
│   ├── raw/
│   ├── calibration/
│   ├── processed/
│   ├── validation/
│   └── legacy/
├── docs/
├── firmware/
├── hardware/
├── requirements-analysis.txt
└── README.md
```

## Quick start

1. 查看总体方案：打开 `docs/project-report/project-report.pdf`。
2. 查看硬件：先看 `hardware/pcb/schematics` 中的原理图，再看 `hardware/mechanical/step` 中的结构件。
3. 查看固件：STM32G431 工程入口是 `firmware/stm32g431-image-control/MDK-ARM/image-control.uvprojx`，ESP32-S3 工程入口是 `firmware/esp32s3-monitor/11_stm32_monitor.ino`。
4. 查看数据：优先看 `data/processed/v3.0/README.md`、`data/processed/v3.0/reports/processing_report.md` 和 `data/processed/v3.0/final_comparison`。
5. 重新跑 Python 数据处理脚本时，安装依赖：

```bash
pip install -r requirements-analysis.txt
```

## Default embedded chain

- 采集端：STM32F103/TCD1304 通过 `USART2` (`PA2/PA3`, `921600 8N1`) 向 STM32G431 提供 3648 点 12 位原始帧。
- 处理端：STM32G431 完成暗电平估计、中值滤波、Savitzky-Golay 平滑、归一化和下采样。
- 显示端：STM32G431 通过 `USART3` (`PB10/PB11`, `115200 8N1`) 向 ESP32-S3 输出文本包。
- 握手线：G431 `PA5(DATA_RDY)` 提示有待发送包，ESP32 `GPIO21(WAKE)` 拉高后 G431 才发送该包。

## Firmware build notes

- `firmware/esp32s3-monitor` 现在已经包含本地 `bsp_cst816.h/.cpp`，克隆仓库后不再缺少触摸驱动头文件。
- ESP32-S3 工程仍依赖 Arduino 环境中的 `lvgl` 与 `Arduino_GFX_Library`。
- `hardware/pcb/eda-archives` 保存当前公开版本的 PCB 工程压缩包，压缩包内部文件名可能与仓库文件名不同。

## Current status

- 已整理光学、结构、PCB、固件和测量数据。
- 已包含 TCD1304 采集链路、STM32G431 处理链路和 ESP32-S3 显示链路。
- V3.0 数据包含 12 组色卡、每组 5 次重复采集，以及 405 nm/780 nm 波长锚点数据。
- 当前仍需要继续完善定量标定、上位机源码、自动化构建和硬件复现文档。


## License

除文件内另有声明外，本仓库原创代码、文档、硬件设计资料和整理后的数据以 MIT License 发布。第三方文件保留其原始许可证声明，尤其是 STM32 HAL/CMSIS 相关文件。详见 `LICENSE` 和 `NOTICE.md`。
