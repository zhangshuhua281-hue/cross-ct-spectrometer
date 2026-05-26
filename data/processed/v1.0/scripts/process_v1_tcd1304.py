from __future__ import annotations

import hashlib
import math
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd


INPUT_FOLDER = "raw/v1.0-cards"
OUTPUT_FOLDER = "V1.0\u6570\u636e\u5904\u7406"
LENS_PREFIX = "\u955c\u7247"

SKIP_FIRST_ROWS = 20
DARK_ROWS = 16
MEDIAN_WINDOW = 9
SAVGOL_WINDOW = 151
SAVGOL_ORDER = 3
REFERENCE_CORR_THRESHOLD = 0.98
CSV_ENCODING = "utf-8-sig"


@dataclass
class SpectrumRecord:
    path: Path
    base_name: str
    curve_id: str
    sample_type: str
    color_id: str
    replicate_id: str
    used_for_reference: bool
    pixel_all: np.ndarray
    adc_all: np.ndarray
    voltage_all: np.ndarray
    pixel: np.ndarray
    adc: np.ndarray
    voltage: np.ndarray
    dark_adc: float
    dark_voltage: float
    intensity: np.ndarray
    intensity_smooth: np.ndarray
    intensity_norm: np.ndarray
    intensity_smooth_norm: np.ndarray
    adc_hash: str


def main() -> None:
    script_path = Path(__file__).resolve()
    output_dir = script_path.parents[1]
    data_dir = script_path.parents[3]
    input_dir = data_dir / INPUT_FOLDER

    dirs = make_dirs(output_dir)
    records = load_records(input_dir)
    if not records:
        raise RuntimeError(f"No .txt files found in {input_dir}")

    pixel = records[0].pixel
    for rec in records:
        if not np.array_equal(rec.pixel, pixel):
            raise RuntimeError(f"Pixel axis mismatch in {rec.path}")

    reference_records = select_reference_records(records)
    if reference_records:
        reference_stack = np.vstack([rec.intensity_smooth for rec in reference_records])
        reference_spectrum = np.nanmedian(reference_stack, axis=0)
        reference_std = np.nanstd(reference_stack, axis=0, ddof=1) if len(reference_records) > 1 else np.zeros_like(reference_spectrum)
    else:
        reference_spectrum = np.full_like(pixel, np.nan, dtype=float)
        reference_std = np.full_like(pixel, np.nan, dtype=float)

    ratio_by_id: dict[str, np.ndarray] = {}
    ratio_norm_by_id: dict[str, np.ndarray] = {}
    for rec in records:
        ratio = safe_divide(rec.intensity_smooth, reference_spectrum)
        ratio_by_id[rec.curve_id] = ratio
        ratio_norm_by_id[rec.curve_id] = normalize_max(ratio)

    write_manifest(records, dirs["manifest"])
    write_cleaned_per_file(records, dirs["cleaned"])
    write_reference_corrected(records, reference_spectrum, reference_std, ratio_by_id, ratio_norm_by_id, dirs["reference"])
    group_tables = write_group_means(records, ratio_by_id, ratio_norm_by_id, dirs["group_means"])
    qc_tables = write_quality_control(records, ratio_by_id, reference_spectrum, dirs["qc"])
    write_figures(records, group_tables, ratio_by_id, ratio_norm_by_id, reference_spectrum, reference_records, dirs)
    write_reports(output_dir, input_dir, records, reference_records, qc_tables)

    print(f"Processed {len(records)} files.")
    print(f"Color-card files: {sum(1 for r in records if r.sample_type == 'color_card')}")
    print(f"Reference candidates used: {len(reference_records)}")
    print(f"Output: {output_dir}")


def make_dirs(output_dir: Path) -> dict[str, Path]:
    dirs = {
        "manifest": output_dir / "data" / "00_manifest",
        "cleaned": output_dir / "data" / "01_cleaned_per_file",
        "group_means": output_dir / "data" / "02_group_means",
        "reference": output_dir / "data" / "03_reference_corrected",
        "qc": output_dir / "data" / "04_quality_control",
        "fig_per_file": output_dir / "figures" / "per_file",
        "fig_group": output_dir / "figures" / "group_means",
        "fig_reference": output_dir / "figures" / "reference_corrected",
        "fig_qc": output_dir / "figures" / "quality_control",
        "reports": output_dir / "reports",
        "scripts": output_dir / "scripts",
    }
    for directory in dirs.values():
        directory.mkdir(parents=True, exist_ok=True)
    dirs["figures"] = output_dir / "figures"
    return dirs


def load_records(input_dir: Path) -> list[SpectrumRecord]:
    records: list[SpectrumRecord] = []
    for path in sorted(input_dir.glob("*.txt"), key=sort_path_key):
        df = pd.read_csv(path)
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
        dark_voltage = float(np.nanmedian(voltage_all[:DARK_ROWS]))
        pixel = pixel_all[SKIP_FIRST_ROWS:]
        adc = adc_all[SKIP_FIRST_ROWS:]
        voltage = voltage_all[SKIP_FIRST_ROWS:]

        intensity = dark_adc - adc
        intensity_med = median_filter(intensity, MEDIAN_WINDOW)
        intensity_smooth = savgol_filter(intensity_med, SAVGOL_WINDOW, SAVGOL_ORDER)
        intensity_smooth = np.maximum(intensity_smooth, 0)

        base_name = path.stem
        sample_type, color_id, replicate_id, used_for_reference = classify_name(path.name)
        curve_id = make_curve_id(sample_type, color_id, replicate_id, base_name)
        adc_hash = hashlib.sha256(adc_all.astype(np.int32).tobytes()).hexdigest()

        records.append(
            SpectrumRecord(
                path=path,
                base_name=base_name,
                curve_id=curve_id,
                sample_type=sample_type,
                color_id=color_id,
                replicate_id=replicate_id,
                used_for_reference=used_for_reference,
                pixel_all=pixel_all,
                adc_all=adc_all,
                voltage_all=voltage_all,
                pixel=pixel,
                adc=adc,
                voltage=voltage,
                dark_adc=dark_adc,
                dark_voltage=dark_voltage,
                intensity=intensity,
                intensity_smooth=intensity_smooth,
                intensity_norm=normalize_max(intensity),
                intensity_smooth_norm=normalize_max(intensity_smooth),
                adc_hash=adc_hash,
            )
        )
    return records


def classify_name(name: str) -> tuple[str, str, str, bool]:
    color = re.match(r"^(\d+)-(\d+)TCD1304", name)
    if color:
        return "color_card", color.group(1), color.group(2), False

    lens = re.match(rf"^{LENS_PREFIX}(\d*)TCD1304", name)
    if lens:
        replicate = lens.group(1)
        # Final reference membership is decided after repeatability screening.
        return "lens_reference", "lens", replicate if replicate else "unlabelled", False

    return "other", "other", "", False


def sort_path_key(path: Path) -> tuple[int, int, int, str]:
    sample_type, color_id, replicate_id, _ = classify_name(path.name)
    if sample_type == "color_card":
        return (0, int(color_id), int(replicate_id), path.name)
    if sample_type == "lens_reference":
        rep_num = int(replicate_id) if replicate_id.isdigit() else 999
        return (1, rep_num, 0, path.name)
    return (2, 0, 0, path.name)


def make_curve_id(sample_type: str, color_id: str, replicate_id: str, base_name: str) -> str:
    if sample_type == "color_card":
        return f"card{int(color_id):02d}_rep{int(replicate_id):02d}"
    if sample_type == "lens_reference":
        if replicate_id.isdigit():
            return f"lens_rep{int(replicate_id):02d}"
        return "lens_unlabelled"
    safe = re.sub(r"[^0-9A-Za-z_]+", "_", base_name).strip("_")
    return safe or "unknown"


def select_reference_records(records: list[SpectrumRecord]) -> list[SpectrumRecord]:
    for rec in records:
        rec.used_for_reference = False

    candidates = [
        rec
        for rec in records
        if rec.sample_type == "lens_reference" and rec.replicate_id.isdigit()
    ]
    if not candidates:
        return []
    if len(candidates) == 1:
        candidates[0].used_for_reference = True
        return candidates

    initial_stack = np.vstack([rec.intensity_smooth_norm for rec in candidates])
    initial_median = np.nanmedian(initial_stack, axis=0)
    corr = [float(np.corrcoef(rec.intensity_smooth_norm, initial_median)[0, 1]) for rec in candidates]
    selected = [rec for rec, value in zip(candidates, corr) if value >= REFERENCE_CORR_THRESHOLD]

    # Keep at least two references if the threshold is too strict for a future dataset.
    if len(selected) < 2:
        selected = candidates

    selected_ids = {id(rec) for rec in selected}
    for rec in candidates:
        rec.used_for_reference = id(rec) in selected_ids
    return selected


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
    y = y.astype(float)
    max_value = np.nanmax(y)
    if not np.isfinite(max_value) or max_value == 0:
        return np.full_like(y, np.nan, dtype=float)
    return y / max_value


def safe_divide(numerator: np.ndarray, denominator: np.ndarray) -> np.ndarray:
    out = np.full_like(numerator, np.nan, dtype=float)
    valid = np.isfinite(denominator) & (np.abs(denominator) > 1e-9)
    out[valid] = numerator[valid] / denominator[valid]
    return out


def write_manifest(records: list[SpectrumRecord], output_dir: Path) -> None:
    rows = []
    for rec in records:
        rows.append(
            {
                "CurveID": rec.curve_id,
                "FileName": rec.path.name,
                "SourcePath": str(rec.path),
                "SampleType": rec.sample_type,
                "ColorCardID": rec.color_id,
                "ReplicateID": rec.replicate_id,
                "UsedForReference": rec.used_for_reference,
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
                "DarkVoltageV": rec.dark_voltage,
                "DarkCorrectedIntensityADC": rec.intensity,
                "SmoothedIntensityADC": rec.intensity_smooth,
                "NormalizedIntensity": rec.intensity_norm,
                "SmoothedNormalizedIntensity": rec.intensity_smooth_norm,
            }
        )
        cleaned.to_csv(output_dir / f"{rec.curve_id}_cleaned.csv", index=False, encoding=CSV_ENCODING)
        wide[f"{rec.curve_id}_SmoothedNormalizedIntensity"] = rec.intensity_smooth_norm
    wide.to_csv(output_dir / "all_curves_smoothed_normalized_wide.csv", index=False, encoding=CSV_ENCODING)


def write_reference_corrected(
    records: list[SpectrumRecord],
    reference_spectrum: np.ndarray,
    reference_std: np.ndarray,
    ratio_by_id: dict[str, np.ndarray],
    ratio_norm_by_id: dict[str, np.ndarray],
    output_dir: Path,
) -> None:
    reference_table = pd.DataFrame(
        {
            "Pixel": records[0].pixel,
            "ReferenceIntensityADC_MedianLensReplicates": reference_spectrum,
            "ReferenceIntensityADC_StdLensReplicates": reference_std,
            "ReferenceCV": safe_divide(reference_std, reference_spectrum),
        }
    )
    reference_table.to_csv(output_dir / "reference_spectrum_from_numbered_lens.csv", index=False, encoding=CSV_ENCODING)

    wide = pd.DataFrame({"Pixel": records[0].pixel})
    for rec in records:
        ratio = ratio_by_id[rec.curve_id]
        ratio_norm = ratio_norm_by_id[rec.curve_id]
        table = pd.DataFrame(
            {
                "Pixel": rec.pixel,
                "SmoothedIntensityADC": rec.intensity_smooth,
                "ReferenceIntensityADC": reference_spectrum,
                "ReferenceCorrectedRatio": ratio,
                "ReferenceCorrectedRatioNormalizedMax": ratio_norm,
            }
        )
        table.to_csv(output_dir / f"{rec.curve_id}_reference_corrected.csv", index=False, encoding=CSV_ENCODING)
        wide[f"{rec.curve_id}_ReferenceCorrectedRatio"] = ratio
    wide.to_csv(output_dir / "all_curves_reference_corrected_wide.csv", index=False, encoding=CSV_ENCODING)


def write_group_means(
    records: list[SpectrumRecord],
    ratio_by_id: dict[str, np.ndarray],
    ratio_norm_by_id: dict[str, np.ndarray],
    output_dir: Path,
) -> dict[str, pd.DataFrame]:
    color_groups = sorted({rec.color_id for rec in records if rec.sample_type == "color_card"}, key=int)
    wide_norm = pd.DataFrame({"Pixel": records[0].pixel})
    wide_ratio = pd.DataFrame({"Pixel": records[0].pixel})
    group_tables: dict[str, pd.DataFrame] = {}

    for group in color_groups:
        group_records = [rec for rec in records if rec.sample_type == "color_card" and rec.color_id == group]
        intensity = np.vstack([rec.intensity for rec in group_records])
        smooth = np.vstack([rec.intensity_smooth for rec in group_records])
        smooth_norm = np.vstack([rec.intensity_smooth_norm for rec in group_records])
        ratio = np.vstack([ratio_by_id[rec.curve_id] for rec in group_records])
        ratio_norm = np.vstack([ratio_norm_by_id[rec.curve_id] for rec in group_records])

        table = pd.DataFrame(
            {
                "Pixel": group_records[0].pixel,
                "MeanIntensityADC": np.nanmean(intensity, axis=0),
                "StdIntensityADC": np.nanstd(intensity, axis=0, ddof=1),
                "CVIntensity": safe_divide(np.nanstd(intensity, axis=0, ddof=1), np.nanmean(intensity, axis=0)),
                "MeanSmoothedIntensityADC": np.nanmean(smooth, axis=0),
                "MeanSmoothedNormalizedIntensity": np.nanmean(smooth_norm, axis=0),
                "StdSmoothedNormalizedIntensity": np.nanstd(smooth_norm, axis=0, ddof=1),
                "MeanReferenceCorrectedRatio": np.nanmean(ratio, axis=0),
                "StdReferenceCorrectedRatio": np.nanstd(ratio, axis=0, ddof=1),
                "MeanReferenceCorrectedRatioNormalizedMax": np.nanmean(ratio_norm, axis=0),
            }
        )
        table.to_csv(output_dir / f"card{int(group):02d}_group_mean_spectrum.csv", index=False, encoding=CSV_ENCODING)
        group_tables[group] = table
        wide_norm[f"card{int(group):02d}_MeanSmoothedNormalizedIntensity"] = table["MeanSmoothedNormalizedIntensity"]
        wide_ratio[f"card{int(group):02d}_MeanReferenceCorrectedRatio"] = table["MeanReferenceCorrectedRatio"]

    wide_norm.to_csv(output_dir / "all_group_means_smoothed_normalized_wide.csv", index=False, encoding=CSV_ENCODING)
    wide_ratio.to_csv(output_dir / "all_group_means_reference_corrected_wide.csv", index=False, encoding=CSV_ENCODING)
    return group_tables


def write_quality_control(
    records: list[SpectrumRecord],
    ratio_by_id: dict[str, np.ndarray],
    reference_spectrum: np.ndarray,
    output_dir: Path,
) -> dict[str, pd.DataFrame]:
    duplicate_map: dict[str, list[str]] = {}
    for rec in records:
        duplicate_map.setdefault(rec.adc_hash, []).append(rec.curve_id)

    file_rows = []
    for rec in records:
        peak_raw = int(rec.pixel[int(np.nanargmax(rec.intensity))])
        peak_smooth = int(rec.pixel[int(np.nanargmax(rec.intensity_smooth))])
        duplicates = [item for item in duplicate_map[rec.adc_hash] if item != rec.curve_id]
        ratio = ratio_by_id[rec.curve_id]
        file_rows.append(
            {
                "CurveID": rec.curve_id,
                "FileName": rec.path.name,
                "SampleType": rec.sample_type,
                "ColorCardID": rec.color_id,
                "ReplicateID": rec.replicate_id,
                "UsedForReference": rec.used_for_reference,
                "Rows": len(rec.pixel_all),
                "ValidRows": len(rec.pixel),
                "DarkADCMedianFirst16": rec.dark_adc,
                "DarkADCStdFirst16": float(np.nanstd(rec.adc_all[:DARK_ROWS], ddof=1)),
                "ValidADCMin": float(np.nanmin(rec.adc)),
                "ValidADCMax": float(np.nanmax(rec.adc)),
                "ValidADCMean": float(np.nanmean(rec.adc)),
                "IntensityMin": float(np.nanmin(rec.intensity)),
                "IntensityMax": float(np.nanmax(rec.intensity)),
                "IntensityMean": float(np.nanmean(rec.intensity)),
                "PeakPixelRawIntensity": peak_raw,
                "PeakPixelSmoothedIntensity": peak_smooth,
                "SaturatedCountADC_GE_4090": int(np.sum(rec.adc_all >= 4090)),
                "PossibleDuplicateOf": ";".join(duplicates),
                "ReferenceCorrectedRatioMedian": float(np.nanmedian(ratio)) if np.any(np.isfinite(ratio)) else np.nan,
                "ReferenceCorrectedRatioMax": float(np.nanmax(ratio)) if np.any(np.isfinite(ratio)) else np.nan,
            }
        )
    file_qc = pd.DataFrame(file_rows)
    file_qc.to_csv(output_dir / "file_qc_stats.csv", index=False, encoding=CSV_ENCODING)

    group_rows = []
    color_groups = sorted({rec.color_id for rec in records if rec.sample_type == "color_card"}, key=int)
    for group in color_groups:
        group_records = [rec for rec in records if rec.sample_type == "color_card" and rec.color_id == group]
        norm_stack = np.vstack([rec.intensity_smooth_norm for rec in group_records])
        corr_values = pairwise_corr_values(norm_stack)
        point_cv = safe_divide(np.nanstd(norm_stack, axis=0, ddof=1), np.nanmean(norm_stack, axis=0))
        peaks = [int(rec.pixel[int(np.nanargmax(rec.intensity_smooth))]) for rec in group_records]
        group_rows.append(
            {
                "ColorCardID": group,
                "Replicates": len(group_records),
                "MeanPairCorrelation": float(np.nanmean(corr_values)),
                "MinPairCorrelation": float(np.nanmin(corr_values)),
                "MeanPointCV_SmoothedNormalized": float(np.nanmean(point_cv)),
                "MeanIntensityADC": float(np.nanmean([np.nanmean(rec.intensity) for rec in group_records])),
                "MeanPeakPixelSmoothed": float(np.nanmean(peaks)),
                "StdPeakPixelSmoothed": float(np.nanstd(peaks, ddof=1)),
            }
        )
    group_qc = pd.DataFrame(group_rows)
    group_qc.to_csv(output_dir / "group_repeatability_stats.csv", index=False, encoding=CSV_ENCODING)

    group_mean_stack = []
    group_names = []
    for group in color_groups:
        group_records = [rec for rec in records if rec.sample_type == "color_card" and rec.color_id == group]
        group_mean_stack.append(np.nanmean(np.vstack([rec.intensity_smooth_norm for rec in group_records]), axis=0))
        group_names.append(f"card{int(group):02d}")
    between = pd.DataFrame(np.corrcoef(np.vstack(group_mean_stack)), columns=group_names, index=group_names)
    between.to_csv(output_dir / "between_group_correlation.csv", encoding=CSV_ENCODING)

    reference_rows = []
    reference_records = [rec for rec in records if rec.sample_type == "lens_reference"]
    if reference_records and np.any(np.isfinite(reference_spectrum)):
        ref_norm = normalize_max(reference_spectrum)
        for rec in reference_records:
            corr = float(np.corrcoef(rec.intensity_smooth_norm, ref_norm)[0, 1])
            reference_rows.append(
                {
                    "CurveID": rec.curve_id,
                    "FileName": rec.path.name,
                    "ReplicateID": rec.replicate_id,
                    "UsedForReference": rec.used_for_reference,
                    "CorrelationToReferenceMedian": corr,
                    "IntensityMean": float(np.nanmean(rec.intensity)),
                    "PeakPixelSmoothedIntensity": int(rec.pixel[int(np.nanargmax(rec.intensity_smooth))]),
                }
            )
    reference_qc = pd.DataFrame(reference_rows)
    reference_qc.to_csv(output_dir / "reference_candidate_stats.csv", index=False, encoding=CSV_ENCODING)

    return {
        "file_qc": file_qc,
        "group_qc": group_qc,
        "between_group": between,
        "reference_qc": reference_qc,
    }


def pairwise_corr_values(stack: np.ndarray) -> np.ndarray:
    if stack.shape[0] < 2:
        return np.array([np.nan])
    corr = np.corrcoef(stack)
    return corr[np.triu_indices_from(corr, 1)]


def write_figures(
    records: list[SpectrumRecord],
    group_tables: dict[str, pd.DataFrame],
    ratio_by_id: dict[str, np.ndarray],
    ratio_norm_by_id: dict[str, np.ndarray],
    reference_spectrum: np.ndarray,
    reference_records: list[SpectrumRecord],
    dirs: dict[str, Path],
) -> None:
    for rec in records:
        svg_line_plot(
            [(rec.curve_id, rec.pixel, rec.intensity_smooth_norm, "#1f77b4")],
            dirs["fig_per_file"] / f"{rec.curve_id}_smoothed_normalized.svg",
            f"{rec.curve_id} smoothed normalized intensity",
            "Smoothed normalized intensity",
            y_min=0,
            y_max=1,
        )

    palette = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf", "#4d4d4d", "#bcbd22"]
    group_series = []
    ratio_series = []
    for i, (group, table) in enumerate(sorted(group_tables.items(), key=lambda item: int(item[0]))):
        color = palette[i % len(palette)]
        group_series.append((f"card{int(group):02d}", table["Pixel"].to_numpy(), table["MeanSmoothedNormalizedIntensity"].to_numpy(), color))
        ratio_series.append((f"card{int(group):02d}", table["Pixel"].to_numpy(), table["MeanReferenceCorrectedRatio"].to_numpy(), color))

    svg_line_plot(
        group_series,
        dirs["fig_group"] / "group_mean_smoothed_normalized_overlay.svg",
        "Group mean smoothed normalized intensity",
        "Smoothed normalized intensity",
        y_min=0,
        y_max=1,
    )

    ratio_max = np.nanmax([np.nanmax(item[2]) for item in ratio_series]) if ratio_series else 1
    svg_line_plot(
        ratio_series,
        dirs["fig_reference"] / "group_mean_reference_corrected_overlay.svg",
        "Group mean reference-corrected ratio",
        "Reference-corrected ratio",
        y_min=0,
        y_max=max(1.0, float(ratio_max) * 1.05),
    )

    if reference_records:
        ref_norm = normalize_max(reference_spectrum)
        ref_series = [("reference_median", reference_records[0].pixel, ref_norm, "#000000")]
        for i, rec in enumerate(reference_records):
            ref_series.append((rec.curve_id, rec.pixel, rec.intensity_smooth_norm, palette[i % len(palette)]))
        svg_line_plot(
            ref_series,
            dirs["fig_reference"] / "numbered_lens_reference_replicates.svg",
            "Numbered lens reference replicates",
            "Smoothed normalized intensity",
            y_min=0,
            y_max=1,
        )

    # A compact QC bar plot is generated after QC CSV exists.
    group_qc_path = dirs["qc"] / "group_repeatability_stats.csv"
    if group_qc_path.exists():
        group_qc = pd.read_csv(group_qc_path)
        svg_bar_plot(
            labels=[f"card{int(x):02d}" for x in group_qc["ColorCardID"]],
            values=group_qc["MeanPairCorrelation"].to_numpy(dtype=float),
            output_path=dirs["fig_qc"] / "group_repeatability_mean_pair_correlation.svg",
            title="Group repeatability: mean pair correlation",
            y_min=0.995,
            y_max=1.0,
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
    for i, (name, _, _, color) in enumerate(series[:18]):
        y0 = legend_y + i * 22
        lines.append(f'<line x1="{legend_x}" y1="{y0}" x2="{legend_x+24}" y2="{y0}" stroke="{color}" stroke-width="3"/>')
        lines.append(f'<text x="{legend_x+32}" y="{y0+4}" class="legend">{escape_xml(name)}</text>')

    lines.append("</svg>")
    output_path.write_text("\n".join(lines), encoding="utf-8")


def svg_bar_plot(
    labels: list[str],
    values: np.ndarray,
    output_path: Path,
    title: str,
    y_min: float,
    y_max: float,
) -> None:
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
    lines.append(f'<text x="{left + plot_w/2:.1f}" y="{height-24}" text-anchor="middle" class="label">Color card group</text>')
    lines.append("</svg>")
    output_path.write_text("\n".join(lines), encoding="utf-8")


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


def write_reports(
    output_dir: Path,
    input_dir: Path,
    records: list[SpectrumRecord],
    reference_records: list[SpectrumRecord],
    qc_tables: dict[str, pd.DataFrame],
) -> None:
    color_records = [rec for rec in records if rec.sample_type == "color_card"]
    lens_records = [rec for rec in records if rec.sample_type == "lens_reference"]
    color_groups = sorted({rec.color_id for rec in color_records}, key=int)
    file_qc = qc_tables["file_qc"]
    group_qc = qc_tables["group_qc"]
    reference_qc = qc_tables["reference_qc"]

    duplicate_rows = file_qc[file_qc["PossibleDuplicateOf"].fillna("").astype(str) != ""]
    duplicate_note = "未发现完全重复的 ADC 序列。"
    if not duplicate_rows.empty:
        duplicate_note = "发现可能完全重复的 ADC 序列：" + "; ".join(
            f"{row.CurveID} == {row.PossibleDuplicateOf}" for row in duplicate_rows.itertuples()
        )

    reference_notes = []
    if not reference_qc.empty:
        not_used_reference = reference_qc[~reference_qc["UsedForReference"].astype(bool)]
        if not not_used_reference.empty:
            reference_notes.append(
                "以下镜片参考未进入最终参考均值，建议复测或检查光路：" +
                ", ".join(f"{row.CurveID}({row.CorrelationToReferenceMedian:.3f})" for row in not_used_reference.itertuples())
            )
        else:
            reference_notes.append("编号镜片参考曲线通过一致性筛选。")
    else:
        reference_notes.append("未找到可用的编号镜片参考曲线，因此未生成可靠的参考校正。")

    group_summary_lines = []
    for row in group_qc.itertuples():
        group_summary_lines.append(
            f"| {int(row.ColorCardID)} | {int(row.Replicates)} | {row.MeanPairCorrelation:.6f} | "
            f"{row.MeanPointCV_SmoothedNormalized:.6f} | {row.MeanIntensityADC:.2f} | {row.MeanPeakPixelSmoothed:.1f} |"
        )

    report = f"""# V1.0 TCD1304 数据处理报告

## 数据理解

- 原始目录：`{input_dir}`
- 本次处理文件数：{len(records)}；其中色卡文件 {len(color_records)} 个，镜片/参考文件 {len(lens_records)} 个。
- 色卡编号：{", ".join(color_groups)}。当前目录没有编号 5 的色卡文件。
- 每个文件包含 `Pixel, ADC, VoltageV` 三列；本批数据实际像素记录为 {len(records[0].pixel_all)} 行，末端像素为 {int(records[0].pixel_all[-1])}。
- 前 {SKIP_FIRST_ROWS} 行存在读出起始瞬态/暗平台，本次保留其统计信息，但不进入有效光谱曲线。

## 处理流程

1. 按文件名解析 `A-B`：`A` 为色卡编号，`B` 为重复检测组别。
2. 用每个文件前 {DARK_ROWS} 个高平台点的中位数估计 `dark_adc`。
3. 将 ADC 转为光强方向信号：`IntensityADC = dark_adc - ADC`。这样曲线越高表示 TCD1304 接收到的反射光越强。
4. 丢弃前 {SKIP_FIRST_ROWS} 行后，对光强曲线做 {MEDIAN_WINDOW} 点中值滤波，再做 {SAVGOL_WINDOW} 点、{SAVGOL_ORDER} 阶 Savitzky-Golay 平滑。
5. 对每条曲线生成最大值归一化结果，用于比较谱形；对同一色卡的 5 次检测生成均值、标准差和变异系数。
6. 对编号镜片参考先做一致性筛选，相关性阈值为 {REFERENCE_CORR_THRESHOLD:.2f}；用通过筛选的平滑光强中位数作为系统参考，生成 `ReferenceCorrectedRatio = sample / reference`。未通过筛选或未编号的 `镜片` 文件只进入 QC，不参与参考均值。

## 输出结构

- `data/00_manifest/file_manifest.csv`：文件清单、解析出的色卡编号/重复编号、哈希和行数。
- `data/01_cleaned_per_file/`：每个文件的暗校正、平滑、归一化曲线。
- `data/02_group_means/`：每个色卡的组均值曲线和宽表。
- `data/03_reference_corrected/`：镜片参考谱和参考校正后的相对反射曲线。
- `data/04_quality_control/`：文件级 QC、组内重复性、组间相关矩阵、参考候选统计。
- `figures/`：对应 SVG 图，浏览器可直接打开。
- `scripts/process_v1_tcd1304.py`：可重复运行的处理脚本。

## QC 摘要

| 色卡 | 重复数 | 组内平均相关 | 平均点位 CV | 平均光强 ADC | 平滑峰值像素 |
|---:|---:|---:|---:|---:|---:|
{chr(10).join(group_summary_lines)}

{duplicate_note}

{" ".join(reference_notes)}

## 使用建议

- 当前结果仍是 `Pixel` 坐标，不是波长坐标。要得到真正的光谱反射率，需要用汞灯、氖灯、激光或已知窄带滤光片建立 `Pixel -> wavelength` 标定。
- 建议之后每批采集同时保存暗场（关光源/遮光）和白板参考（标准白板或稳定漫反射白片）。暗场应从每个像素扣除，白板参考用于绝对或相对反射率归一化。
- 编号镜片参考里存在一致性偏低的曲线时，优先检查光路位置、色卡/镜片放置角度、积分时间和光源稳定性。
- 组内重复性已经很高，说明色卡采样本身可重复；下一步提升有效性的重点是参考测量和波长标定。
"""

    (output_dir / "reports" / "processing_report.md").write_text(report, encoding=CSV_ENCODING)

    readme = """# V1.0 数据处理

处理入口：`scripts/process_v1_tcd1304.py`

主要结果优先看：

1. `reports/processing_report.md`
2. `data/02_group_means/all_group_means_smoothed_normalized_wide.csv`
3. `data/03_reference_corrected/all_group_means_reference_corrected_wide.csv`
4. `figures/group_means/group_mean_smoothed_normalized_overlay.svg`
5. `figures/reference_corrected/group_mean_reference_corrected_overlay.svg`
"""
    (output_dir / "README.md").write_text(readme, encoding=CSV_ENCODING)


if __name__ == "__main__":
    main()
