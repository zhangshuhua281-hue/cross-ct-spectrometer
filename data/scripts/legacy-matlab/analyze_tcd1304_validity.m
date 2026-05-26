clear; clc;

rootDir = fileparts(mfilename('fullpath'));
dataRoot = fullfile(rootDir, 'V1.0采集数据');
outputDir = fullfile(rootDir, 'validity_analysis');
skipFirstRows = 20;
yColumn = 'ADC';

if ~isfolder(outputDir)
    mkdir(outputDir);
end

files = dir(fullfile(dataRoot, '*.txt'));
rows = table('Size', [0 14], ...
    'VariableTypes', {'string','string','double','double','double','double','double','double','double','double','double','double','double','double'}, ...
    'VariableNames', {'File','Group','Rows','SkippedRows','First20Mean','First20Max','PostMin','PostMax','PostMean','PostStd','PostRange','RangeOverMax','SaturatedCount','PeakPixel'});

curveMap = containers.Map('KeyType', 'char', 'ValueType', 'any');
fileMap = containers.Map('KeyType', 'char', 'ValueType', 'any');

for k = 1:numel(files)
    filePath = fullfile(files(k).folder, files(k).name);
    data = readtable(filePath, 'Delimiter', ',', 'VariableNamingRule', 'preserve');
    yAll = double(data.(yColumn));
    xAll = double(data.Pixel);

    if startsWith(string(files(k).name), "镜片")
        token = regexp(files(k).name, '^(镜片\d*)TCD1304', 'tokens', 'once');
        if isempty(token)
            groupName = "镜片";
        else
            groupName = string(token{1});
        end
    else
        token = regexp(files(k).name, '^(\d+)-(\d+)TCD1304', 'tokens', 'once');
        if isempty(token)
            groupName = "unknown";
        else
            groupName = string(token{1});
        end
    end

    first20 = yAll(1:min(skipFirstRows, numel(yAll)));
    x = xAll(skipFirstRows + 1:end);
    y = yAll(skipFirstRows + 1:end);
    yNorm = y ./ max(y, [], 'omitnan');
    ySmooth = sgolayfilt(smoothdata(yNorm, 'movmedian', 9), 3, 151);
    ySmooth = ySmooth ./ max(ySmooth, [], 'omitnan');

    [~, peakIndex] = max(ySmooth, [], 'omitnan');
    postMax = max(y, [], 'omitnan');
    postMin = min(y, [], 'omitnan');

    rows = [rows; {string(files(k).name), groupName, numel(yAll), skipFirstRows, ...
        mean(first20, 'omitnan'), max(first20, [], 'omitnan'), postMin, postMax, ...
        mean(y, 'omitnan'), std(y, 'omitnan'), postMax - postMin, ...
        (postMax - postMin) / postMax, sum(yAll >= 4090), x(peakIndex)}]; %#ok<AGROW>

    key = char(groupName);
    if isKey(curveMap, key)
        curveMap(key) = [curveMap(key), ySmooth];
        fileMap(key) = [fileMap(key), string(files(k).name)];
    else
        curveMap(key) = ySmooth;
        fileMap(key) = string(files(k).name);
    end
end

writetable(rows, fullfile(outputDir, 'file_level_stats.csv'));

groups = keys(curveMap);
groupRows = table('Size', [0 9], ...
    'VariableTypes', {'string','double','double','double','double','double','double','double','double'}, ...
    'VariableNames', {'Group','Replicates','MeanPostMax','MeanPostRange','MeanRangeOverMax','MeanPeakPixel','PeakPixelStd','MeanPairCorr','MeanPointStd'});

for i = 1:numel(groups)
    key = groups{i};
    curves = curveMap(key);
    groupData = rows(rows.Group == string(key), :);

    if size(curves, 2) > 1
        corrMat = corr(curves, 'Rows', 'pairwise');
        upperMask = triu(true(size(corrMat)), 1);
        meanPairCorr = mean(corrMat(upperMask), 'omitnan');
        meanPointStd = mean(std(curves, 0, 2, 'omitnan'), 'omitnan');
    else
        meanPairCorr = NaN;
        meanPointStd = NaN;
    end

    groupRows = [groupRows; {string(key), height(groupData), ...
        mean(groupData.PostMax, 'omitnan'), mean(groupData.PostRange, 'omitnan'), ...
        mean(groupData.RangeOverMax, 'omitnan'), mean(groupData.PeakPixel, 'omitnan'), ...
        std(groupData.PeakPixel, 'omitnan'), meanPairCorr, meanPointStd}]; %#ok<AGROW>
end

groupRows = sortrows(groupRows, 'Group');
writetable(groupRows, fullfile(outputDir, 'group_repeatability_stats.csv'));

% Difference between group average curves.
digitGroups = ["1","2","3","4","6","7","8"];
meanCurves = [];
validNames = strings(0, 1);
for i = 1:numel(digitGroups)
    key = char(digitGroups(i));
    if isKey(curveMap, key)
        curves = curveMap(key);
        meanCurves(:, end + 1) = mean(curves, 2, 'omitnan'); %#ok<SAGROW>
        validNames(end + 1, 1) = digitGroups(i); %#ok<SAGROW>
    end
end

if size(meanCurves, 2) > 1
    groupCorr = array2table(corr(meanCurves, 'Rows', 'pairwise'), ...
        'VariableNames', matlab.lang.makeValidName("G" + validNames), ...
        'RowNames', cellstr("G" + validNames));
    writetable(groupCorr, fullfile(outputDir, 'between_group_correlation.csv'), 'WriteRowNames', true);

    fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 980 560]);
    ax = axes(fig);
    hold(ax, 'on');
    colors = lines(size(meanCurves, 2));
    x = (skipFirstRows + 1):(skipFirstRows + size(meanCurves, 1));
    for i = 1:size(meanCurves, 2)
        plot(ax, x, meanCurves(:, i), 'LineWidth', 1.8, 'Color', colors(i, :), ...
            'DisplayName', "Group " + validNames(i));
    end
    grid(ax, 'on');
    box(ax, 'on');
    ylim(ax, [0 1]);
    xlabel(ax, 'Pixel');
    ylabel(ax, 'Mean smoothed normalized ADC');
    title(ax, 'Mean curves by color-card group', 'Interpreter', 'none');
    legend(ax, 'Location', 'eastoutside');
    exportgraphics(fig, fullfile(outputDir, 'mean_curves_by_group.png'), 'Resolution', 300);
    savefig(fig, fullfile(outputDir, 'mean_curves_by_group.fig'));
    close(fig);
end

disp('File-level stats:');
disp(rows(:, {'File','Group','First20Mean','PostMin','PostMax','PostRange','RangeOverMax','SaturatedCount','PeakPixel'}));
disp('Group repeatability stats:');
disp(groupRows);
fprintf('Saved analysis to:\n%s\n', outputDir);
