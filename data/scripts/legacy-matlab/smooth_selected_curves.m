clear; clc;

rootDir = fileparts(mfilename('fullpath'));
outputDir = fullfile(rootDir, 'origin_smooth_selected_skip20');
plotDir = fullfile(outputDir, 'plots');
dataDir = fullfile(outputDir, 'data');
yColumn = 'ADC';
skipFirstRows = 20;

% Origin-style smoothing settings.
medianWindow = 9;
sgolayWindow = 151;
sgolayOrder = 3;

if ~isfolder(plotDir)
    mkdir(plotDir);
end
if ~isfolder(dataDir)
    mkdir(dataDir);
end

targetPrefixes = ["1-1", "2-1", "3-1", "4-1", "5-1", "6-1", "7-1", "8-1"];
files = dir(fullfile(rootDir, '**', '*.txt'));

selected = struct('folder', {}, 'name', {});
foundPrefixes = strings(0, 1);

for k = 1:numel(files)
    name = string(files(k).name);
    isDigitStart = ~isempty(regexp(char(name), '^\d', 'once'));
    matchedPrefix = "";

    for p = 1:numel(targetPrefixes)
        if startsWith(name, targetPrefixes(p) + "TCD1304")
            matchedPrefix = targetPrefixes(p);
            break;
        end
    end

    isLens1 = contains(name, "1TCD1304") && ~isDigitStart;
    if matchedPrefix ~= "" || isLens1
        selected(end + 1).folder = files(k).folder; %#ok<SAGROW>
        selected(end).name = files(k).name;
        if matchedPrefix ~= ""
            foundPrefixes(end + 1, 1) = matchedPrefix; %#ok<SAGROW>
        end
    end
end

summary = table('Size', [0 9], ...
    'VariableTypes', {'string', 'string', 'double', 'double', 'double', 'double', 'double', 'double', 'string'}, ...
    'VariableNames', {'File', 'YColumn', 'SkippedRows', 'RawMaxValue', ...
    'MedianWindow', 'SGolayWindow', 'SGolayOrder', 'MaxSmoothedNormalized', 'OutputCsv'});

overlayFig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 980 560]);
overlayAx = axes(overlayFig);
hold(overlayAx, 'on');
colorOrder = lines(max(numel(selected), 1));

for k = 1:numel(selected)
    filePath = fullfile(selected(k).folder, selected(k).name);

    opts = detectImportOptions(filePath, ...
        'FileType', 'text', ...
        'Delimiter', ',', ...
        'VariableNamingRule', 'preserve');
    data = readtable(filePath, opts);

    if height(data) <= skipFirstRows
        warning('Skip %s: only %d rows, cannot remove first %d rows.', ...
            filePath, height(data), skipFirstRows);
        continue;
    end

    data = data(skipFirstRows + 1:end, :);
    vars = string(data.Properties.VariableNames);

    if ismember("Pixel", vars)
        x = data.("Pixel");
    else
        x = (1:height(data)).';
    end

    if ~ismember(yColumn, vars)
        warning('Skip %s: column "%s" was not found.', filePath, yColumn);
        continue;
    end

    y = double(data.(yColumn));
    rawMaxValue = max(y, [], 'omitnan');
    if isempty(rawMaxValue) || isnan(rawMaxValue) || rawMaxValue == 0
        warning('Skip %s: max value is invalid for normalization.', filePath);
        continue;
    end

    yNorm = y ./ rawMaxValue;
    yDenoised = smoothdata(yNorm, 'movmedian', medianWindow);
    ySmooth = sgolayfilt(yDenoised, sgolayOrder, sgolayWindow);
    ySmooth = ySmooth ./ max(ySmooth, [], 'omitnan');
    ySmooth = min(max(ySmooth, 0), 1);

    [~, baseName] = fileparts(selected(k).name);
    outCsvPath = fullfile(dataDir, baseName + "_skip20_smoothed.csv");
    outTable = table(x, yNorm, ySmooth, ...
        'VariableNames', {'Pixel', 'NormalizedADC', 'SmoothedNormalizedADC'});
    writetable(outTable, outCsvPath);

    fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 900 520]);
    ax = axes(fig);
    plot(ax, x, ySmooth, 'LineWidth', 2.0, 'Color', [0.0000 0.4470 0.7410]);
    grid(ax, 'on');
    box(ax, 'on');
    ylim(ax, [0 1]);
    xlim(ax, [min(x) max(x)]);
    xlabel(ax, 'Pixel');
    ylabel(ax, 'Smoothed normalized ADC');
    title(ax, baseName + " smoothed", 'Interpreter', 'none');
    set(ax, 'FontName', 'Arial', 'FontSize', 11, 'LineWidth', 1);

    pngPath = fullfile(plotDir, baseName + "_skip20_smoothed.png");
    figPath = fullfile(plotDir, baseName + "_skip20_smoothed.fig");
    exportgraphics(fig, pngPath, 'Resolution', 300);
    savefig(fig, figPath);
    close(fig);

    plot(overlayAx, x, ySmooth, 'LineWidth', 1.8, ...
        'Color', colorOrder(k, :), 'DisplayName', baseName);

    summary = [summary; {string(filePath), string(yColumn), skipFirstRows, ...
        rawMaxValue, medianWindow, sgolayWindow, sgolayOrder, ...
        max(ySmooth, [], 'omitnan'), string(outCsvPath)}]; %#ok<AGROW>
end

grid(overlayAx, 'on');
box(overlayAx, 'on');
ylim(overlayAx, [0 1]);
xlabel(overlayAx, 'Pixel');
ylabel(overlayAx, 'Smoothed normalized ADC');
title(overlayAx, 'Selected smoothed normalized curves', 'Interpreter', 'none');
legend(overlayAx, 'Location', 'eastoutside', 'Interpreter', 'none');
set(overlayAx, 'FontName', 'Arial', 'FontSize', 11, 'LineWidth', 1);
exportgraphics(overlayFig, fullfile(plotDir, 'selected_curves_skip20_smoothed_overlay.png'), 'Resolution', 300);
savefig(overlayFig, fullfile(plotDir, 'selected_curves_skip20_smoothed_overlay.fig'));
close(overlayFig);

missingPrefixes = setdiff(targetPrefixes, unique(foundPrefixes), 'stable');
writetable(summary, fullfile(outputDir, 'smoothing_summary.csv'));

fprintf('Done. Smoothed %d selected files.\n', height(summary));
fprintf('Output folder:\n%s\n', outputDir);
if ~isempty(missingPrefixes)
    fprintf('Missing numeric targets: %s\n', strjoin(missingPrefixes, ', '));
end
