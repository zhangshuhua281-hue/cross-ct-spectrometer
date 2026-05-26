clear; clc;

rootDir = fileparts(mfilename('fullpath'));
dataDir = fullfile(rootDir, 'V1.0采集数据');
outputDir = fullfile(rootDir, 'difference_smooth_1_1_minus_4_1_6_1');
yColumn = 'ADC';
skipFirstRows = 20;

medianWindow = 9;
sgolayWindow = 151;
sgolayOrder = 3;

if ~isfolder(outputDir)
    mkdir(outputDir);
end

base = readCurve(dataDir, "1-1TCD1304*.txt", yColumn, skipFirstRows);
curve4 = readCurve(dataDir, "4-1TCD1304*.txt", yColumn, skipFirstRows);
curve6 = readCurve(dataDir, "6-1TCD1304*.txt", yColumn, skipFirstRows);

if ~isequal(base.x, curve4.x) || ~isequal(base.x, curve6.x)
    error('Pixel columns do not match among 1-1, 4-1, and 6-1.');
end

x = base.x;
diff4Raw = base.y - curve4.y;
diff6Raw = base.y - curve6.y;

diff4Norm = normalizeToZeroOne(diff4Raw);
diff6Norm = normalizeToZeroOne(diff6Raw);

diff4Smooth = smoothAndNormalize(diff4Norm, medianWindow, sgolayWindow, sgolayOrder);
diff6Smooth = smoothAndNormalize(diff6Norm, medianWindow, sgolayWindow, sgolayOrder);

[peak4, idx4] = max(diff4Smooth, [], 'omitnan');
[peak6, idx6] = max(diff6Smooth, [], 'omitnan');

outTable = table(x, diff4Raw, diff6Raw, diff4Norm, diff6Norm, diff4Smooth, diff6Smooth, ...
    'VariableNames', {'Pixel', 'RawDiff_1_1_minus_4_1', 'RawDiff_1_1_minus_6_1', ...
    'NormalizedDiff_1_1_minus_4_1', 'NormalizedDiff_1_1_minus_6_1', ...
    'SmoothedNormalizedDiff_1_1_minus_4_1', 'SmoothedNormalizedDiff_1_1_minus_6_1'});
writetable(outTable, fullfile(outputDir, '1-1_minus_4-1_and_6-1_smoothed_normalized_data.csv'));

summary = table(["1-1 - 4-1"; "1-1 - 6-1"], ...
    [min(diff4Raw, [], 'omitnan'); min(diff6Raw, [], 'omitnan')], ...
    [max(diff4Raw, [], 'omitnan'); max(diff6Raw, [], 'omitnan')], ...
    [x(idx4); x(idx6)], ...
    [peak4; peak6], ...
    'VariableNames', {'Curve', 'RawDiffMin', 'RawDiffMax', 'PeakPixel', 'PeakValue'});
writetable(summary, fullfile(outputDir, '1-1_minus_4-1_and_6-1_summary.csv'));

fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 980 560]);
ax = axes(fig);
hold(ax, 'on');

plot(ax, x, diff4Smooth, 'LineWidth', 2.3, 'Color', [0.0000 0.3000 0.8000], ...
    'DisplayName', "1-1 - 4-1");
plot(ax, x, diff6Smooth, 'LineWidth', 2.3, 'Color', [0.9500 0.7000 0.0500], ...
    'DisplayName', "1-1 - 6-1");

grid(ax, 'on');
box(ax, 'on');
ylim(ax, [0 1]);
xlim(ax, [min(x) max(x)]);
xlabel(ax, 'Pixel');
ylabel(ax, 'Smoothed normalized difference');
title(ax, 'Smoothed normalized difference curves after skipping first 20 points', ...
    'Interpreter', 'none');
legend(ax, 'Location', 'best', 'Interpreter', 'none');
set(ax, 'FontName', 'Arial', 'FontSize', 11, 'LineWidth', 1);

pngPath = fullfile(outputDir, '1-1_minus_4-1_and_6-1_smoothed_normalized_compare.png');
figPath = fullfile(outputDir, '1-1_minus_4-1_and_6-1_smoothed_normalized_compare.fig');
exportgraphics(fig, pngPath, 'Resolution', 300);
savefig(fig, figPath);
close(fig);

fprintf('Done. Saved comparison plot to:\n%s\n', pngPath);
disp(summary);

function curve = readCurve(dataDir, pattern, yColumn, skipFirstRows)
    matches = dir(fullfile(dataDir, pattern));
    if isempty(matches)
        error('Cannot find file matching %s in %s', pattern, dataDir);
    end

    filePath = fullfile(matches(1).folder, matches(1).name);
    data = readtable(filePath, 'Delimiter', ',', 'VariableNamingRule', 'preserve');
    data = data(skipFirstRows + 1:end, :);

    curve.file = filePath;
    curve.x = double(data.Pixel);
    curve.y = double(data.(yColumn));
end

function yNorm = normalizeToZeroOne(y)
    yMin = min(y, [], 'omitnan');
    yMax = max(y, [], 'omitnan');
    if isempty(yMin) || isempty(yMax) || isnan(yMin) || isnan(yMax) || yMax == yMin
        error('Cannot normalize a constant or invalid difference curve.');
    end
    yNorm = (y - yMin) ./ (yMax - yMin);
end

function ySmooth = smoothAndNormalize(yNorm, medianWindow, sgolayWindow, sgolayOrder)
    yDenoised = smoothdata(yNorm, 'movmedian', medianWindow);
    ySmooth = sgolayfilt(yDenoised, sgolayOrder, sgolayWindow);
    ySmooth = normalizeToZeroOne(ySmooth);
    ySmooth = min(max(ySmooth, 0), 1);
end
