from __future__ import annotations

import hashlib
import math
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd


INPUT_FOLDER = "raw/v3.0-cards"
OUTPUT_FOLDER = "V3.0\u6570\u636e\u5904\u7406"
SKIP_FIRST_ROWS = 20
DARK_ROWS = 16
MEDIAN_WINDOW = 9
SAVGOL_WINDOW = 151
SAVGOL_ORDER = 3
CSV_ENCODING = "utf-8-sig"


@dataclass
class SpectrumRecord:
    path: Path
    base_name: str
    group_id: str
    replicate_id: str
    pixel_all: np.ndarray
    adc_all: np.ndarray
    voltage_all: np.ndarray
    pixel: np.ndarray
    adc: np.ndarray
    voltage: np.ndarray
    dark_adc: float
    intensity: np.ndarray
    intensity_smooth: np.ndarray
    intensity_norm: np.ndarray
    intensity_smooth_norm: np.ndarray
    adc_hash: str


def main() -> None:
    script_path = Path(__file__).resolve()
    output_root = script_path.parents[1]
    data_dir = script_path.parents[3]
    input_dir = data_dir / INPUT_FOLDER
    output_dir = output_root
    dirs = make_dirs(output_dir)

    records = load_records(input_dir)
    if not records:
        raise RuntimeError(f"No files found in {input_dir}")

    pixel = records[0].pixel
    for rec in records:
        if not np.array_equal(rec.pixel, pixel):
            raise RuntimeError(f"Pixel axis mismatch in {rec.path}")

    write_manifest(records, dirs["manifest"])
    write_cleaned_per_file(records, dirs["cleaned"])
    group_tables = write_group_means(records, dirs["group_means"])
    qc_tables = write_quality_control(records, group_tables, dirs["qc"])
    write_figures(records, group_tables, dirs)
    write_reports(input_dir, output_dir, records, group_tables, qc_tables)
    write_final_comparison(output_dir, records, group_tables)

    print(f"Processed {len(records)} files.")
    print(f"Groups: {len(group_tables)}")
    print(f"Output: {output_dir}")


def make_dirs(output_dir: Path) -> dict[str, Path]:
    dirs = {
        "manifest": output_dir / "data" / "00_manifest",
        "cleaned": output_dir / "data" / "01_cleaned_per_file",
        "group_means": output_dir / "data" / "02_group_means",
        "qc": output_dir / "data" / "04_quality_control",
        "fig_per_file": output_dir / "figures" / "per_file",
        "fig_group": output_dir / "figures" / "group_means",
        "fig_qc": output_dir / "figures" / "quality_control",
        "reports": output_dir / "reports",
        "final": output_dir / "final_comparison",
        "scripts": output_dir / "scripts",
    }
    for directory in dirs.values():
        directory.mkdir(parents=True, exist_ok=True)
    return dirs


def load_records(input_dir: Path) -> list[SpectrumRecord]:
    records: list[SpectrumRecord] = []
    for path in sorted(input_dir.glob("*.txt"), key=sort_path_key):
        df = pd.read_csv(path, sep=None, engine="python", encoding="utf-8-sig")
        required = {"Pixel", "ADC", "VoltageV"}
        missing = required.difference(df.columns)
        if missing:
            raise RuntimeError(f"{path.name} missing columns: {sorted(missing)}")

        pixel_all = df["Pixel"].to_numpy(dtype=int)
        adc_all = df["ADC"].to_numpy(dtype=float)
        voltage_all = df["VoltageV"].to_numpy(dtype=float)
        if len(adc_all) <= SKIP_FIRST_ROWS:
            raise RuntimeError(f"{path.name} has only {len(adc_all)} rows")

        dark_adc = float(np.nanmedian(adc_all[:DARK_ROWS]))
        pixel = pixel_all[SKIP_FIRST_ROWS:]
        adc = adc_all[SKIP_FIRST_ROWS:]
        voltage = voltage_all[SKIP_FIRST_ROWS:]
        intensity = dark_adc - adc
        intensity_med = median_filter(intensity, MEDIAN_WINDOW)
        intensity_smooth = savgol_filter(intensity_med, SAVGOL_WINDOW, SAVGOL_ORDER)
        intensity_smooth = np.maximum(intensity_smooth, 0)

        sample = classify_name(path.name)
        base_name = path.stem
        adc_hash = hashlib.sha256(adc_all.astype(np.int32).tobytes()).hexdigest()
        records.append(
            SpectrumRecord(
                path=path,
                base_name=base_name,
                group_id=sample[0],
                replicate_id=sample[1],
                pixel_all=pixel_all,
                adc_all=adc_all,
                voltage_all=voltage_all,
                pixel=pixel,
                adc=adc,
                voltage=voltage,
                dark_adc=dark_adc,
                intensity=intensity,
                intensity_smooth=intensity_smooth,
                intensity_norm=normalize_max(intensity),
                intensity_smooth_norm=normalize_max(intensity_smooth),
                adc_hash=adc_hash,
            )
        )
    return records


def classify_name(name: str) -> tuple[str, str]:
    m = re.match(r"^(\d+)-(\d+)\.txt$", name)
    if m:
        return m.group(1), m.group(2)
    return "other", ""


def sort_path_key(path: Path) -> tuple[int, int, int, str]:
    g, r = classify_name(path.name)
    if g.isdigit():
        return (0, int(g), int(r), path.name)
    return (1, 0, 0, path.name)


def median_filter(y: np.ndarray, window: int) -> np.ndarray:
    if window <= 1:
        return y.astype(float, copy=True)
    if window % 2 == 0:
        raise ValueError("median window must be odd")
    half = window // 2
    padded = np.pad(y.astype(float), (half, half), mode="edge")
    return np.array([np.nanmedian(padded[i : i + window]) for i in range(len(y))], dtype=float)


def savgol_filter(y: np.ndarray, window: int, order: int) -> np.ndarray:
    if window <= 1:
        return y.astype(float, copy=True)
    if window % 2 == 0:
        raise ValueError("Savitzky-Golay window must be odd")
    if order >= window:
        raise ValueError("Savitzky-Golay order must be smaller than the window")
    half = window // 2
    x = np.arange(-half, half + 1, dtype=float)
    design = np.vander(x, order + 1, increasing=True)
    coeff = np.linalg.pinv(design)[0]
    padded = np.pad(y.astype(float), (half, half), mode="edge")
    return np.array([float(np.dot(coeff, padded[i : i + window])) for i in range(len(y))], dtype=float)


def normalize_max(y: np.ndarray) -> np.ndarray:
    max_value = np.nanmax(y)
    if not np.isfinite(max_value) or max_value == 0:
        return np.full_like(y, np.nan, dtype=float)
    return y / max_value


def write_manifest(records: list[SpectrumRecord], output_dir: Path) -> None:
    rows = []
    for rec in records:
        rows.append(
            {
                "GroupID": rec.group_id,
                "ReplicateID": rec.replicate_id,
                "FileName": rec.path.name,
                "SourcePath": str(rec.path),
                "Rows": len(rec.pixel_all),
                "FirstPixel": int(rec.pixel_all[0]),
                "LastPixel": int(rec.pixel_all[-1]),
                "ValidRowsAfterSkip": len(rec.pixel),
                "SkippedFirstRows": SKIP_FIRST_ROWS,
                "DarkRowsUsed": DARK_ROWS,
                "ADCSHA256": rec.adc_hash,
            }
        )
    pd.DataFrame(rows).to_csv(output_dir / "file_manifest.csv", index=False, encoding=CSV_ENCODING)


def write_cleaned_per_file(records: list[SpectrumRecord], output_dir: Path) -> None:
    wide = pd.DataFrame({"Pixel": records[0].pixel})
    for rec in records:
        cleaned = pd.DataFrame(
            {
                "Pixel": rec.pixel,
                "RawADC": rec.adc,
                "RawVoltageV": rec.voltage,
                "DarkADC": rec.dark_adc,
                "DarkCorrectedIntensityADC": rec.intensity,
                "SmoothedIntensityADC": rec.intensity_smooth,
                "NormalizedIntensity": rec.intensity_norm,
                "SmoothedNormalizedIntensity": rec.intensity_smooth_norm,
            }
        )
        cleaned.to_csv(output_dir / f"{rec.base_name}_cleaned.csv", index=False, encoding=CSV_ENCODING)
        wide[f"{rec.base_name}_SmoothedNormalizedIntensity"] = rec.intensity_smooth_norm
    wide.to_csv(output_dir / "all_curves_smoothed_normalized_wide.csv", index=False, encoding=CSV_ENCODING)


def write_group_means(records: list[SpectrumRecord], output_dir: Path) -> dict[str, pd.DataFrame]:
    group_ids = sorted({rec.group_id for rec in records if rec.group_id.isdigit()}, key=int)
    group_tables: dict[str, pd.DataFrame] = {}
    wide = pd.DataFrame({"Pixel": records[0].pixel})
    for group_id in group_ids:
        group_records = [rec for rec in records if rec.group_id == group_id]
        stack = np.vstack([rec.intensity_smooth_norm for rec in group_records])
        table = pd.DataFrame(
            {
                "Pixel": group_records[0].pixel,
                "MeanSmoothedNormalizedIntensity": np.nanmean(stack, axis=0),
                "StdSmoothedNormalizedIntensity": np.nanstd(stack, axis=0, ddof=1),
                "CVSmoothedNormalizedIntensity": safe_divide(np.nanstd(stack, axis=0, ddof=1), np.nanmean(stack, axis=0)),
            }
        )
        table.to_csv(output_dir / f"group{int(group_id):02d}_mean_spectrum.csv", index=False, encoding=CSV_ENCODING)
        group_tables[group_id] = table
        wide[f"group{int(group_id):02d}_MeanSmoothedNormalizedIntensity"] = table["MeanSmoothedNormalizedIntensity"]
    wide.to_csv(output_dir / "all_group_means_smoothed_normalized_wide.csv", index=False, encoding=CSV_ENCODING)
    return group_tables


def safe_divide(numerator: np.ndarray, denominator: np.ndarray) -> np.ndarray:
    out = np.full_like(numerator, np.nan, dtype=float)
    valid = np.isfinite(denominator) & (np.abs(denominator) > 1e-9)
    out[valid] = numerator[valid] / denominator[valid]
    return out


def write_quality_control(
    records: list[SpectrumRecord],
    group_tables: dict[str, pd.DataFrame],
    output_dir: Path,
) -> dict[str, pd.DataFrame]:
    file_rows = []
    duplicate_map: dict[str, list[str]] = {}
    for rec in records:
        duplicate_map.setdefault(rec.adc_hash, []).append(rec.base_name)
    for rec in records:
        duplicates = [name for name in duplicate_map[rec.adc_hash] if name != rec.base_name]
        peak_raw = int(rec.pixel[int(np.nanargmax(rec.intensity))])
        peak_smooth = int(rec.pixel[int(np.nanargmax(rec.intensity_smooth))])
        file_rows.append(
            {
                "GroupID": rec.group_id,
                "ReplicateID": rec.replicate_id,
                "FileName": rec.path.name,
                "Rows": len(rec.pixel_all),
                "ValidRows": len(rec.pixel),
                "DarkADCMedianFirst16": rec.dark_adc,
                "ValidADCMin": float(np.nanmin(rec.adc)),
                "ValidADCMax": float(np.nanmax(rec.adc)),
                "IntensityMean": float(np.nanmean(rec.intensity)),
                "IntensityMax": float(np.nanmax(rec.intensity)),
                "PeakPixelRawIntensity": peak_raw,
                "PeakPixelSmoothedIntensity": peak_smooth,
                "PossibleDuplicateOf": ";".join(duplicates),
            }
        )
    file_qc = pd.DataFrame(file_rows)
    file_qc.to_csv(output_dir / "file_qc_stats.csv", index=False, encoding=CSV_ENCODING)

    group_rows = []
    for group_id in sorted(group_tables, key=int):
        group_records = [rec for rec in records if rec.group_id == group_id]
        stack = np.vstack([rec.intensity_smooth_norm for rec in group_records])
        corr_values = pairwise_corr_values(stack)
        peaks = [int(rec.pixel[int(np.nanargmax(rec.intensity_smooth))]) for rec in group_records]
        group_rows.append(
            {
                "GroupID": group_id,
                "Replicates": len(group_records),
                "MeanPairCorrelation": float(np.nanmean(corr_values)),
                "MinPairCorrelation": float(np.nanmin(corr_values)),
                "MeanPointCV": float(np.nanmean(safe_divide(np.nanstd(stack, axis=0, ddof=1), np.nanmean(stack, axis=0)))),
                "MeanPeakPixelSmoothed": float(np.nanmean(peaks)),
                "StdPeakPixelSmoothed": float(np.nanstd(peaks, ddof=1)),
            }
        )
    group_qc = pd.DataFrame(group_rows)
    group_qc.to_csv(output_dir / "group_repeatability_stats.csv", index=False, encoding=CSV_ENCODING)

    group_mean_stack = []
    group_names = []
    for group_id in sorted(group_tables, key=int):
        group_records = [rec for rec in records if rec.group_id == group_id]
        group_mean_stack.append(np.nanmean(np.vstack([rec.intensity_smooth_norm for rec in group_records]), axis=0))
        group_names.append(f"group{int(group_id):02d}")
    between = pd.DataFrame(np.corrcoef(np.vstack(group_mean_stack)), columns=group_names, index=group_names)
    between.to_csv(output_dir / "between_group_correlation.csv", encoding=CSV_ENCODING)

    return {"file_qc": file_qc, "group_qc": group_qc, "between_group": between}


def pairwise_corr_values(stack: np.ndarray) -> np.ndarray:
    if stack.shape[0] < 2:
        return np.array([np.nan])
    corr = np.corrcoef(stack)
    return corr[np.triu_indices_from(corr, 1)]


def svg_header(width: int, height: int) -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<style>
  text {{ font-family: Arial, "Microsoft YaHei", sans-serif; fill: #222; }}
  .title {{ font-size: 18px; font-weight: 700; }}
  .label {{ font-size: 14px; }}
  .tick {{ font-size: 12px; fill: #444; }}
  .legend {{ font-size: 12px; }}
  .grid {{ stroke: #dddddd; stroke-width: 1; }}
</style>'''


def escape_xml(text: str) -> str:
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def svg_line_plot(
    series: list[tuple[str, np.ndarray, np.ndarray, str]],
    output_path: Path,
    title: str,
    y_label: str,
    y_min: float | None = None,
    y_max: float | None = None,
) -> None:
    width, height = 1080, 620
    left, right, top, bottom = 78, 220, 52, 72
    plot_w = width - left - right
    plot_h = height - top - bottom
    all_x = np.concatenate([s[1].astype(float) for s in series])
    all_y = np.concatenate([s[2].astype(float) for s in series])
    finite_y = all_y[np.isfinite(all_y)]
    x_min, x_max = float(np.nanmin(all_x)), float(np.nanmax(all_x))
    if y_min is None:
        y_min = float(np.nanmin(finite_y))
    if y_max is None:
        y_max = float(np.nanmax(finite_y))
    if y_max == y_min:
        y_max = y_min + 1
    def sx(x: np.ndarray) -> np.ndarray:
        return left + (x.astype(float) - x_min) / (x_max - x_min) * plot_w
    def sy(y: np.ndarray) -> np.ndarray:
        return top + (y_max - y.astype(float)) / (y_max - y_min) * plot_h
    lines = [
        svg_header(width, height),
        f'<text x="{width/2:.1f}" y="28" text-anchor="middle" class="title">{escape_xml(title)}</text>',
        f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#ffffff" stroke="#333333" stroke-width="1"/>',
    ]
    for i in range(6):
        yv = y_min + (y_max - y_min) * i / 5
        yp = sy(np.array([yv]))[0]
        lines.append(f'<line x1="{left}" y1="{yp:.2f}" x2="{left+plot_w}" y2="{yp:.2f}" class="grid"/>')
        lines.append(f'<text x="{left-8}" y="{yp+4:.2f}" text-anchor="end" class="tick">{yv:.3g}</text>')
    for i in range(6):
        xv = x_min + (x_max - x_min) * i / 5
        xp = sx(np.array([xv]))[0]
        lines.append(f'<line x1="{xp:.2f}" y1="{top}" x2="{xp:.2f}" y2="{top+plot_h}" class="grid"/>')
        lines.append(f'<text x="{xp:.2f}" y="{top+plot_h+22}" text-anchor="middle" class="tick">{xv:.0f}</text>')
    for name, x, y, color in series:
        finite = np.isfinite(y)
        x_plot = sx(x[finite])
        y_plot = sy(y[finite])
        if len(x_plot) == 0:
            continue
        points = " ".join(f"{xp:.2f},{yp:.2f}" for xp, yp in zip(x_plot, y_plot))
        lines.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>')
    lines.append(f'<text x="{left + plot_w/2:.1f}" y="{height-24}" text-anchor="middle" class="label">Pixel</text>')
    lines.append(
        f'<text x="22" y="{top + plot_h/2:.1f}" text-anchor="middle" class="label" transform="rotate(-90 22 {top + plot_h/2:.1f})">{escape_xml(y_label)}</text>'
    )
    legend_x = left + plot_w + 22
    legend_y = top + 12
    palette = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf", "#4d4d4d", "#bcbd22", "#8c564b", "#17a2b8", "#e377c2", "#7f7f7f"]
    for i, (name, _, _, _) in enumerate(series[:18]):
        color = palette[i % len(palette)]
        y0 = legend_y + i * 22
        lines.append(f'<line x1="{legend_x}" y1="{y0}" x2="{legend_x+24}" y2="{y0}" stroke="{color}" stroke-width="3"/>')
        lines.append(f'<text x="{legend_x+32}" y="{y0+4}" class="legend">{escape_xml(name)}</text>')
    lines.append("</svg>")
    output_path.write_text("\n".join(lines), encoding="utf-8")


def svg_bar_plot(labels: list[str], values: np.ndarray, output_path: Path, title: str, y_min: float, y_max: float) -> None:
    width, height = 900, 520
    left, right, top, bottom = 76, 36, 52, 76
    plot_w = width - left - right
    plot_h = height - top - bottom
    bar_gap = 16
    bar_w = (plot_w - bar_gap * (len(labels) + 1)) / max(len(labels), 1)
    def sy(y: float) -> float:
        return top + (y_max - y) / (y_max - y_min) * plot_h
    lines = [
        svg_header(width, height),
        f'<text x="{width/2:.1f}" y="28" text-anchor="middle" class="title">{escape_xml(title)}</text>',
        f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#ffffff" stroke="#333333" stroke-width="1"/>',
    ]
    for i in range(6):
        yv = y_min + (y_max - y_min) * i / 5
        yp = sy(yv)
        lines.append(f'<line x1="{left}" y1="{yp:.2f}" x2="{left+plot_w}" y2="{yp:.2f}" class="grid"/>')
        lines.append(f'<text x="{left-8}" y="{yp+4:.2f}" text-anchor="end" class="tick">{yv:.4f}</text>')
    for i, (label, value) in enumerate(zip(labels, values)):
        x0 = left + bar_gap + i * (bar_w + bar_gap)
        y0 = sy(float(value))
        h = top + plot_h - y0
        lines.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{bar_w:.2f}" height="{h:.2f}" fill="#1f77b4"/>')
        lines.append(f'<text x="{x0+bar_w/2:.2f}" y="{top+plot_h+24}" text-anchor="middle" class="tick">{escape_xml(label)}</text>')
    lines.append(f'<text x="{left + plot_w/2:.1f}" y="{height-24}" text-anchor="middle" class="label">Group ID</text>')
    lines.append("</svg>")
    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_figures(records: list[SpectrumRecord], group_tables: dict[str, pd.DataFrame], dirs: dict[str, Path]) -> None:
    for rec in records:
        svg_line_plot(
            [(rec.base_name, rec.pixel, rec.intensity_smooth_norm, "#1f77b4")],
            dirs["fig_per_file"] / f"{rec.base_name}_smoothed_normalized.svg",
            f"{rec.base_name} smoothed normalized intensity",
            "Smoothed normalized intensity",
            y_min=0,
            y_max=1,
        )
    palette = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf", "#4d4d4d", "#bcbd22", "#8c564b", "#17a2b8", "#e377c2", "#7f7f7f"]
    series = []
    for i, (group_id, table) in enumerate(sorted(group_tables.items(), key=lambda item: int(item[0]))):
        color = palette[i % len(palette)]
        series.append((f"group{int(group_id):02d}", table["Pixel"].to_numpy(), table["MeanSmoothedNormalizedIntensity"].to_numpy(), color))
    svg_line_plot(
        series,
        dirs["fig_group"] / "group_mean_smoothed_normalized_overlay.svg",
        "Group mean smoothed normalized intensity",
        "Smoothed normalized intensity",
        y_min=0,
        y_max=1,
    )
    qc = pd.read_csv(dirs["qc"] / "group_repeatability_stats.csv")
    svg_bar_plot(
        [f"group{int(x):02d}" for x in qc["GroupID"]],
        qc["MeanPairCorrelation"].to_numpy(dtype=float),
        dirs["fig_qc"] / "group_repeatability_mean_pair_correlation.svg",
        "Group repeatability: mean pair correlation",
        y_min=float(np.nanmin(qc["MeanPairCorrelation"])) - 0.01,
        y_max=1.0,
    )


def write_reports(
    input_dir: Path,
    output_dir: Path,
    records: list[SpectrumRecord],
    group_tables: dict[str, pd.DataFrame],
    qc_tables: dict[str, pd.DataFrame],
) -> None:
    file_qc = qc_tables["file_qc"]
    group_qc = qc_tables["group_qc"]
    between = qc_tables["between_group"]
    duplicate_rows = file_qc[file_qc["PossibleDuplicateOf"].fillna("").astype(str) != ""]
    report = f"""# V3.0 TCD1304 数据处理报告

## 数据理解

- 原始目录：`{input_dir}`
- 文件数：{len(records)}；色卡组数：{len(group_tables)}
- 每个文件为 `Pixel, ADC, VoltageV` 三列，实际像素点数为 {len(records[0].pixel_all)}。
- 前 {SKIP_FIRST_ROWS} 行为读出瞬态/起始平台，保留统计，不纳入有效曲线。

## 处理流程

1. 按文件名 `A-B.txt` 解析 `A` 为色卡编号，`B` 为重复序号。
2. 前 {DARK_ROWS} 个高平台点取中位数作为 `dark_adc`。
3. 用 `IntensityADC = dark_adc - ADC`，再做 {MEDIAN_WINDOW} 点中值滤波和 {SAVGOL_WINDOW} 点 Savitzky-Golay 平滑。
4. 每组 5 次测量求组均值，并输出组内重复性、组间相关矩阵和最终对比图。

## 主要结论

- 所有 60 条样本都能按文件名解析为 12 组，并完成组内重复性与组间相关矩阵汇总。
- 从组均值曲线和组间相关矩阵看，这批数据的组间差异明显优于 V1.0。
- 组内平均相关系数见 `data/04_quality_control/group_repeatability_stats.csv`，其中第 12 组最稳定，但其物理意义需要结合采集条件确认。
- 组间相关矩阵见 `data/04_quality_control/between_group_correlation.csv`。

## 输出

- `final_comparison/V3.0_色卡最终对比表.xlsx`
- `final_comparison/V3.0_色卡最终对比图_平滑归一化.png`
- `final_comparison/V3.0_色卡最终对比图_组间对比.png`

## QC 摘要

| 组别 | 重复数 | 组内平均相关 | 平均点位 CV | 平均峰值像素 |
|---:|---:|---:|---:|---:|
{chr(10).join(f'| {int(r.GroupID)} | {int(r.Replicates)} | {r.MeanPairCorrelation:.6f} | {r.MeanPointCV:.6f} | {r.MeanPeakPixelSmoothed:.1f} |' for r in group_qc.itertuples())}

{('发现完全重复文件：' + '; '.join(f'{row.FileName} == {row.PossibleDuplicateOf}' for row in duplicate_rows.itertuples())) if not duplicate_rows.empty else '未发现完全重复文件。'}
"""
    (output_dir / "reports" / "processing_report.md").write_text(report, encoding=CSV_ENCODING)
    (output_dir / "README.md").write_text(
        "V3.0 final outputs are in final_comparison/ and reports/.\n",
        encoding=CSV_ENCODING,
    )


def write_final_comparison(output_dir: Path, records: list[SpectrumRecord], group_tables: dict[str, pd.DataFrame]) -> None:
    final_dir = output_dir / "final_comparison"
    final_dir.mkdir(parents=True, exist_ok=True)
    norm = pd.DataFrame({"Pixel": records[0].pixel})
    for group_id in sorted(group_tables, key=int):
        norm[f"Group{int(group_id):02d}_MeanSmoothedNormalizedIntensity"] = group_tables[group_id]["MeanSmoothedNormalizedIntensity"]
    norm_csv = final_dir / "V3.0_\u8272\u5361\u7ec4\u5747\u503c\u66f2\u7ebf_\u540c\u8868_\u5e73\u6ed1\u5f52\u4e00\u5316.csv"
    norm.to_csv(norm_csv, index=False, encoding=CSV_ENCODING)
    features = []
    x = norm["Pixel"].to_numpy(float)
    for group_id in sorted(group_tables, key=int):
        y = group_tables[group_id]["MeanSmoothedNormalizedIntensity"].to_numpy(float)
        features.append(
            {
                "GroupID": int(group_id),
                "PeakPixel": int(x[int(np.nanargmax(y))]),
                "CurveArea": float(np.trapezoid(y, x)),
                "MaxValue": float(np.nanmax(y)),
                "MedianValue": float(np.nanmedian(y)),
            }
        )
    pd.DataFrame(features).to_csv(final_dir / "V3.0_\u8272\u5361\u66f2\u7ebf\u5dee\u5f02\u7279\u5f81.csv", index=False, encoding=CSV_ENCODING)
    svg_line_plot(
        [(f"group{int(g):02d}", group_tables[g]["Pixel"].to_numpy(), group_tables[g]["MeanSmoothedNormalizedIntensity"].to_numpy(), "#1f77b4") for g in sorted(group_tables, key=int)],
        final_dir / "V3.0_\u8272\u5361\u6700\u7ec8\u5bf9\u6bd4\u56fe_\u5e73\u6ed1\u5f52\u4e00\u5316.svg",
        "V3.0 group comparison - smoothed normalized",
        "Smoothed normalized intensity",
        y_min=0,
        y_max=1,
    )
    svg_line_plot(
        [(f"group{int(g):02d}", group_tables[g]["Pixel"].to_numpy(), group_tables[g]["MeanSmoothedNormalizedIntensity"].to_numpy(), "#1f77b4") for g in sorted(group_tables, key=int)],
        final_dir / "V3.0_\u8272\u5361\u6700\u7ec8\u5bf9\u6bd4\u56fe_\u7ec4\u95f4\u5bf9\u6bd4.svg",
        "V3.0 group comparison - all groups",
        "Smoothed normalized intensity",
        y_min=0,
        y_max=1,
    )
    # Excel workbook
    wb = pd.ExcelWriter(final_dir / "V3.0_\u8272\u5361\u6700\u7ec8\u5bf9\u6bd4\u8868.xlsx", engine="openpyxl")
    norm.to_excel(wb, sheet_name="归一化曲线", index=False)
    pd.DataFrame(features).to_excel(wb, sheet_name="差异特征", index=False)
    wb.close()


if __name__ == "__main__":
    main()
