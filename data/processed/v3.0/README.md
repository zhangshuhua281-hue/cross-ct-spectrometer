# V3.0 数据处理

处理入口：`scripts/process_v3_tcd1304.py`

主要结果优先看：

1. `reports/processing_report.md`
2. `data/02_group_means/all_group_means_smoothed_normalized_wide.csv`
3. `data/04_quality_control/group_repeatability_stats.csv`
4. `figures/group_means/group_mean_smoothed_normalized_overlay.svg`
5. `final_comparison/V3.0_色卡最终对比图_平滑归一化.svg`
6. `wavelength_calibration_405_780/405_780_线性波长标定公式.csv`

当前脚本默认从同级数据目录推导输入路径。若移动目录或只复制脚本，请同步调整 `INPUT_FOLDER` 和目录层级。

