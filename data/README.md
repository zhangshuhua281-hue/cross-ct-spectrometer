# Measurement Data

测量数据按原始数据、标定数据、处理结果和旧版分析结果分开。

## Raw data

- `raw/v1.0-cards`: V1.0 色卡与镜片参考采集数据。
- `raw/v2.0-cards`: V2.0 色卡采集数据。
- `raw/v3.0-cards`: V3.0 色卡采集数据，共 12 组，每组 5 次重复。

原始 TXT 通常包含 `Pixel, ADC, VoltageV` 三列。数据处理时默认跳过前 20 行瞬态/平台点，并使用前 16 点估计暗电平。

## Calibration data

- `calibration/405nm`: 405 nm 光源锚点数据。
- `calibration/780nm`: 780 nm 光源锚点数据。

## Processed data

- `processed/v1.0`: V1.0 数据处理脚本、CSV、报告和图像。
- `processed/v3.0`: V3.0 数据处理脚本、CSV、报告、最终对比和 405/780 nm 线性标定结果。

优先查看：

- `processed/v3.0/reports/processing_report.md`
- `processed/v3.0/final_comparison`
- `processed/v3.0/wavelength_calibration_405_780`

## Legacy and validation

- `legacy/origin-smooth-selected-skip20`: 早期平滑与差异分析结果。
- `validation/tcd1304-validity`: TCD1304 有效性和组间相关性分析结果。

