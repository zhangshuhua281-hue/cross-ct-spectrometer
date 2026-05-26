clear; clc;

rootDir = fileparts(mfilename('fullpath'));
dataDir = fullfile(rootDir, 'V1.0采集数据');
outputDir = fullfile(rootDir, 'unified_baseline_2_1_skip20');
plotDir = fullfile(outputDir, 'individual_plots');
dataOutDir = fullfile(outputDir, 'data');
yColumn = 'ADC';
skipFirstRows = 20;

medianWindow = 9;
sgolayWindow = 151;
sgolayOrder = 3;

if ~isfolder(plotDir)
    mkdir(plotDir);
end
if ~isfolder(dataOutDir)
    mkdir(dataOutDir);
end

ref = readCurve(dataDir, "2-1TCD1304*.txt", yColumn, skipFirstRows);
files = dir(fullfile(dataDir, '*.txt'));

curves = struct('file', {}, 'baseName', {}, 'group', {}, 'x', {}, ...
    'raw', {}, 'rawDiff', {}, 'smoothDiff', {});

for k = 1:numel(files)
    curve = readCurveByPath(fullfile(files(k).folder, files(k).name), yColumn, skipFirstRows);
    if ~isequal(curve.x, ref.x)
        warning('Skip %s: Pixel column does not match 2-1 reference.', curve.file);
        continue;
    end

    curve.rawDiff = curve.raw - ref.raw;
    curve.smoothDiff = sgolayfilt(smoothdata(curve.rawDiff, 'movmedian', medianWindow), ...
        sgolayOrder, sgolayWindow);
    curves(end + 1) = curve; %#ok<SAGROW>
end

if isempty(curves)
    error('No curves were processed.');
end

globalMaxAbs = max(abs(vertcat(curves.smoothDiff)), [], 'all', 'omitnan');
if isempty(globalMaxAbs) || isnan(globalMaxAbs) || globalMaxAbs == 0
    error('Global normalization scale is invalid.');
end

x = curves(1).x;
wideTable = table(x, 'VariableNames', {'Pixel'});
summary = table('Size', [0 11], ...
    'VariableTypes', {'string','string','string','logical','double','double','double','double','double','double','string'}, ...
    'VariableNames', {'File','CurveID','Group','IsReference','RawDiffMin','RawDiffMax', ...
    'SmoothDiffMin','SmoothDiffMax','SignedNormPeakAbs','Display01Peak','OutputCsv'});

for k = 1:numel(curves)
    signedNorm = curves(k).smoothDiff ./ globalMaxAbs;
    displayNorm01 = (signedNorm + 1) ./ 2;
    displayNorm01 = min(max(displayNorm01, 0), 1);

    curveId = makeCurveId(curves(k).baseName, k);
    perFileTable = table(x, curves(k).raw, curves(k).rawDiff, curves(k).smoothDiff, ...
        signedNorm, displayNorm01, ...
        'VariableNames', {'Pixel','RawADC','RawDiffFrom2_1','SmoothedDiffFrom2_1', ...
        'SignedNormalizedDiff','DisplayNormalized01'});

    outCsv = fullfile(dataOutDir, curves(k).baseName + "_minus_2-1_unified_normalized.csv");
    writetable(perFileTable, outCsv);

    wideTable.(curveId + "_SignedNormalizedDiff") = signedNorm;
    wideTable.(curveId + "_DisplayNormalized01") = displayNorm01;

    fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 900 520]);
    ax = axes(fig);
    plot(ax, x, displayNorm01, 'LineWidth', 2.0, 'Color', [0.0000 0.3000 0.8000]);
    grid(ax, 'on');
    box(ax, 'on');
    ylim(ax, [0 1]);
    xlim(ax, [min(x) max(x)]);
    xlabel(ax, 'Pixel');
    ylabel(ax, 'Unified normalized difference');
    title(ax, curves(k).baseName + " minus 2-1 (0.5 = baseline)", 'Interpreter', 'none');
    set(ax, 'FontName', 'Arial', 'FontSize', 11, 'LineWidth', 1);

    exportgraphics(fig, fullfile(plotDir, curves(k).baseName + "_minus_2-1_unified_normalized.png"), ...
        'Resolution', 300);
    savefig(fig, fullfile(plotDir, curves(k).baseName + "_minus_2-1_unified_normalized.fig"));
    close(fig);

    summary = [summary; {string(curves(k).file), string(curveId), string(curves(k).group), ...
        strcmp(curves(k).baseName, ref.baseName), ...
        min(curves(k).rawDiff, [], 'omitnan'), max(curves(k).rawDiff, [], 'omitnan'), ...
        min(curves(k).smoothDiff, [], 'omitnan'), max(curves(k).smoothDiff, [], 'omitnan'), ...
        max(abs(signedNorm), [], 'omitnan'), max(displayNorm01, [], 'omitnan'), string(outCsv)}]; %#ok<AGROW>
end

writetable(wideTable, fullfile(dataOutDir, 'all_curves_minus_2-1_unified_normalized_wide.csv'));
writetable(summary, fullfile(outputDir, 'unified_baseline_2_1_summary.csv'));

makeOverlayPlot(curves, x, globalMaxAbs, outputDir);
makeGroupMeanPlot(curves, x, globalMaxAbs, outputDir);

fprintf('Done. Processed %d files.\n', height(summary));
fprintf('Global max abs smoothed difference: %.6g ADC.\n', globalMaxAbs);
fprintf('Output folder:\n%s\n', outputDir);

function curve = readCurve(dataDir, pattern, yColumn, skipFirstRows)
    matches = dir(fullfile(dataDir, pattern));
    if isempty(matches)
        error('Cannot find reference file matching %s in %s', pattern, dataDir);
    end
    curve = readCurveByPath(fullfile(matches(1).folder, matches(1).name), yColumn, skipFirstRows);
end

function curve = readCurveByPath(filePath, yColumn, skipFirstRows)
    data = readtable(filePath, 'Delimiter', ',', 'VariableNamingRule', 'preserve');
    if height(data) <= skipFirstRows
        error('%s only has %d rows, cannot remove first %d rows.', ...
            filePath, height(data), skipFirstRows);
    end

    data = data(skipFirstRows + 1:end, :);
    [~, baseName] = fileparts(filePath);

    curve.file = filePath;
    curve.baseName = string(baseName);
    curve.group = getGroupName(string(baseName));
    curve.x = double(data.Pixel);
    curve.raw = double(data.(yColumn));
    curve.rawDiff = [];
    curve.smoothDiff = [];
end

function group = getGroupName(baseName)
    token = regexp(char(baseName), '^(\d+)-\d+TCD1304', 'tokens', 'once');
    if ~isempty(token)
        group = string(token{1});
        return;
    end

    if startsWith(baseName, "镜片")
        group = "镜片";
    else
        group = "other";
    end
end

function curveId = makeCurveId(baseName, index)
    curveId = matlab.lang.makeValidName("C" + string(index) + "_" + baseName);
end

function makeOverlayPlot(curves, x, globalMaxAbs, outputDir)
    fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 1100 620]);
    ax = axes(fig);
    hold(ax, 'on');
    colors = lines(numel(curves));

    for i = 1:numel(curves)
        displayNorm01 = ((curves(i).smoothDiff ./ globalMaxAbs) + 1) ./ 2;
        plot(ax, x, displayNorm01, 'LineWidth', 1.1, 'Color', colors(i, :), ...
            'DisplayName', curves(i).baseName);
    end

    grid(ax, 'on');
    box(ax, 'on');
    ylim(ax, [0 1]);
    xlim(ax, [min(x) max(x)]);
    xlabel(ax, 'Pixel');
    ylabel(ax, 'Unified normalized difference');
    title(ax, 'All curves minus 2-1, unified normalization (0.5 = baseline)', ...
        'Interpreter', 'none');
    legend(ax, 'Location', 'eastoutside', 'Interpreter', 'none');
    set(ax, 'FontName', 'Arial', 'FontSize', 10, 'LineWidth', 1);

    exportgraphics(fig, fullfile(outputDir, 'all_curves_minus_2-1_unified_normalized_overlay.png'), ...
        'Resolution', 300);
    savefig(fig, fullfile(outputDir, 'all_curves_minus_2-1_unified_normalized_overlay.fig'));
    close(fig);
end

function makeGroupMeanPlot(curves, x, globalMaxAbs, outputDir)
    groups = unique([curves.group], 'stable');
    fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 980 560]);
    ax = axes(fig);
    hold(ax, 'on');
    colors = lines(numel(groups));

    groupSummary = table('Size', [0 5], ...
        'VariableTypes', {'string','double','double','double','double'}, ...
        'VariableNames', {'Group','Replicates','MeanDisplay01Min','MeanDisplay01Max','MeanSignedNormMean'});

    for g = 1:numel(groups)
        idx = [curves.group] == groups(g);
        smoothDiffMat = horzcat(curves(idx).smoothDiff);
        signedMean = mean(smoothDiffMat ./ globalMaxAbs, 2, 'omitnan');
        displayMean = (signedMean + 1) ./ 2;

        plot(ax, x, displayMean, 'LineWidth', 2.0, 'Color', colors(g, :), ...
            'DisplayName', groups(g));

        groupSummary = [groupSummary; {groups(g), sum(idx), min(displayMean, [], 'omitnan'), ...
            max(displayMean, [], 'omitnan'), mean(signedMean, 'omitnan')}]; %#ok<AGROW>
    end

    grid(ax, 'on');
    box(ax, 'on');
    ylim(ax, [0 1]);
    xlim(ax, [min(x) max(x)]);
    xlabel(ax, 'Pixel');
    ylabel(ax, 'Group mean unified normalized difference');
    title(ax, 'Group mean curves minus 2-1, unified normalization', 'Interpreter', 'none');
    legend(ax, 'Location', 'eastoutside', 'Interpreter', 'none');
    set(ax, 'FontName', 'Arial', 'FontSize', 11, 'LineWidth', 1);

    exportgraphics(fig, fullfile(outputDir, 'group_mean_minus_2-1_unified_normalized_overlay.png'), ...
        'Resolution', 300);
    savefig(fig, fullfile(outputDir, 'group_mean_minus_2-1_unified_normalized_overlay.fig'));
    close(fig);

    writetable(groupSummary, fullfile(outputDir, 'group_mean_minus_2-1_summary.csv'));
end
