clear; clc;

rootDir = fileparts(mfilename('fullpath'));
dataDir = fullfile(rootDir, 'origin_smooth_selected_skip20', 'data');
outputDir = fullfile(rootDir, 'origin_smooth_selected_skip20', 'compare_4_1_6_1');

if ~isfolder(outputDir)
    mkdir(outputDir);
end

curveFiles = [
    "4-1TCD1304*_skip20_smoothed.csv"
    "6-1TCD1304*_skip20_smoothed.csv"
];
curveLabels = ["4-1", "6-1"];
curveColors = [
    0.0000 0.3000 0.8000
    0.9500 0.7000 0.0500
];

fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 980 560]);
ax = axes(fig);
hold(ax, 'on');

peakSummary = table('Size', [0 4], ...
    'VariableTypes', {'string', 'double', 'double', 'string'}, ...
    'VariableNames', {'Curve', 'PeakPixel', 'PeakValue', 'SourceCsv'});

for i = 1:numel(curveFiles)
    matches = dir(fullfile(dataDir, curveFiles(i)));
    if isempty(matches)
        error('Cannot find data file matching %s in %s', curveFiles(i), dataDir);
    end

    csvPath = fullfile(matches(1).folder, matches(1).name);
    data = readtable(csvPath, 'VariableNamingRule', 'preserve');
    x = data.Pixel;
    y = data.SmoothedNormalizedADC;

    [peakValue, peakIndex] = max(y, [], 'omitnan');
    peakPixel = x(peakIndex);

    plot(ax, x, y, 'LineWidth', 2.4, ...
        'Color', curveColors(i, :), ...
        'DisplayName', curveLabels(i) + " peak x=" + string(peakPixel));

    peakSummary = [peakSummary; {curveLabels(i), peakPixel, peakValue, string(csvPath)}]; %#ok<AGROW>
end

deltaPixel = abs(peakSummary.PeakPixel(1) - peakSummary.PeakPixel(2));

grid(ax, 'on');
box(ax, 'on');
ylim(ax, [0 1]);
xlabel(ax, 'Pixel');
ylabel(ax, 'Smoothed normalized ADC');
title(ax, "4-1 vs 6-1 smoothed normalized curves (peak offset " + ...
    string(deltaPixel) + " pixels)", 'Interpreter', 'none');
legend(ax, 'Location', 'best', 'Interpreter', 'none');
set(ax, 'FontName', 'Arial', 'FontSize', 11, 'LineWidth', 1);

pngPath = fullfile(outputDir, '4-1_vs_6-1_skip20_smoothed_blue_yellow_compare.png');
figPath = fullfile(outputDir, '4-1_vs_6-1_skip20_smoothed_blue_yellow_compare.fig');
csvPath = fullfile(outputDir, '4-1_vs_6-1_peak_summary.csv');

exportgraphics(fig, pngPath, 'Resolution', 300);
savefig(fig, figPath);
close(fig);
writetable(peakSummary, csvPath);

fprintf('Done. Peak difference: %.0f pixels.\n', deltaPixel);
fprintf('Saved comparison plot to:\n%s\n', pngPath);
