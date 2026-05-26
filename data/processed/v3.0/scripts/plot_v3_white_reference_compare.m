clear; clc;

scriptDir = fileparts(mfilename('fullpath'));
rootDir = fileparts(scriptDir);
dataRoot = fileparts(rootDir);
inputDir = fullfile(dataRoot, 'V3.0采集数据');
outputDir = fullfile(rootDir, 'matlab_white_reference_without_group12');

skipFirstRows = 20;
darkRows = 16;
medianWindow = 9;
sgolayWindow = 151;
sgolayOrder = 3;
interpFactor = 6;
whiteGroup = 2;
excludeGroups = 12;
cnFont = pickChineseFont();

if ~isfolder(outputDir)
    mkdir(outputDir);
end

files = dir(fullfile(inputDir, '*.txt'));
if isempty(files)
    error('No .txt files found in %s', inputDir);
end

records = struct('File', {}, 'Group', {}, 'Replicate', {}, 'Pixel', {}, ...
    'RawADC', {}, 'DarkADC', {}, 'Intensity', {}, 'SmoothedIntensity', {});

for k = 1:numel(files)
    filePath = fullfile(files(k).folder, files(k).name);
    [groupId, repId] = parseGroupReplicate(files(k).name);
    data = readtable(filePath, 'FileType', 'text', 'Delimiter', '\t', 'VariableNamingRule', 'preserve');

    if height(data) <= skipFirstRows
        error('File %s has only %d rows, cannot skip %d rows.', files(k).name, height(data), skipFirstRows);
    end

    if height(data) <= darkRows
        error('File %s has only %d rows, cannot estimate dark level from first %d rows.', ...
            files(k).name, height(data), darkRows);
    end

    xAll = double(data.Pixel);
    adcAll = double(data.ADC);

    darkADC = median(adcAll(1:darkRows), 'omitnan');
    validIdx = skipFirstRows + 1:height(data);
    x = xAll(validIdx);
    adc = adcAll(validIdx);

    intensity = darkADC - adc;
    intensityMed = smoothdata(intensity, 'movmedian', medianWindow);
    intensitySmooth = sgolayfilt(intensityMed, sgolayOrder, sgolayWindow);
    intensitySmooth = max(intensitySmooth, 0);

    records(end + 1) = struct( ...
        'File', string(files(k).name), ...
        'Group', groupId, ...
        'Replicate', repId, ...
        'Pixel', x, ...
        'RawADC', adc, ...
        'DarkADC', darkADC, ...
        'Intensity', intensity, ...
        'SmoothedIntensity', intensitySmooth); %#ok<SAGROW>
end

groups = setdiff(unique([records.Group]), excludeGroups);
groups = sort(groups);
if ~ismember(whiteGroup, groups)
    error('White reference group %d not found.', whiteGroup);
end

% Group mean curves on the original pixel axis.
groupMean = struct('Group', {}, 'Pixel', {}, 'MeanIntensity', {}, 'MeanSmoothed', {});
for g = groups
    idx = [records.Group] == g;
    x = records(find(idx, 1, 'first')).Pixel;
    stack = cat(2, records(idx).SmoothedIntensity);
    meanSmooth = mean(stack, 2, 'omitnan');
    groupMean(end + 1) = struct( ...
        'Group', g, ...
        'Pixel', x, ...
        'MeanIntensity', mean(stack, 2, 'omitnan'), ...
        'MeanSmoothed', meanSmooth); %#ok<SAGROW>
end

whiteIdx = find([groupMean.Group] == whiteGroup, 1, 'first');
whitePixel = groupMean(whiteIdx).Pixel;
whiteMean = groupMean(whiteIdx).MeanSmoothed;

% White-normalized, interpolated, lightly re-smoothed group curves.
xFine = linspace(min(whitePixel), max(whitePixel), numel(whitePixel) * interpFactor);
comparison = table(xFine(:), 'VariableNames', {'Pixel'});
summaryRows = table('Size', [0 5], ...
    'VariableTypes', {'double', 'double', 'double', 'double', 'double'}, ...
    'VariableNames', {'Group', 'PeakPixel', 'CurveArea', 'MaxValue', 'MedianValue'});

for i = 1:numel(groupMean)
    ratio = groupMean(i).MeanSmoothed ./ whiteMean;
    ratioFine = interp1(whitePixel, ratio, xFine, 'pchip');
    ratioFine = smoothdata(ratioFine, 'movmedian', 11);
    ratioFine = sgolayfilt(ratioFine, 3, 61);

    colName = sprintf('Group%02d_WhiteNorm', groupMean(i).Group);
    comparison.(colName) = ratioFine(:);

    [mx, maxIdx] = max(ratioFine, [], 'omitnan');
    summaryRows = [summaryRows; {groupMean(i).Group, xFine(maxIdx), ...
        trapz(xFine, ratioFine), mx, median(ratioFine, 'omitnan')}]; %#ok<AGROW>
end

% Save tables.
outCsv = fullfile(outputDir, 'V3.0_去掉12组_2X白纸基准_归一化对比.csv');
writetable(comparison, outCsv, 'Encoding', 'UTF-8');
writetable(summaryRows, fullfile(outputDir, 'V3.0_去掉12组_2X白纸基准_差异摘要.csv'), 'Encoding', 'UTF-8');

% Plot.
fig = figure('Color', 'w', 'Position', [80 80 1380 720]);
t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
ax = nexttile(t);
hold(ax, 'on');
grid(ax, 'on');
box(ax, 'on');

legendNames = strings(0, 1);

for i = 1:numel(groupMean)
    g = groupMean(i).Group;
    colName = sprintf('Group%02d_WhiteNorm', g);
    y = comparison.(colName);
    displayName = sprintf('%d-X %s', g, getGroupColorName(g));
    if g == whiteGroup
        plot(ax, xFine, y, 'k-', 'LineWidth', 2.8, ...
            'DisplayName', sprintf('%d-X %s / 白纸基准', g, getGroupColorName(g)));
    else
        plot(ax, xFine, y, 'Color', getGroupLineColor(g), 'LineWidth', 1.8, ...
            'DisplayName', displayName);
    end
    legendNames(end + 1, 1) = displayName; %#ok<SAGROW>
end

yline(ax, 1.0, 'k:', 'LineWidth', 1.2, 'HandleVisibility', 'off');

xlabel(ax, 'TCD1304像素位置（C-T展开方向，未标定波长）', 'FontName', cnFont, 'Interpreter', 'none');
ylabel(ax, '白纸归一化相对响应', 'FontName', cnFont, 'Interpreter', 'none');
titleHandle = title(t, 'V3.0 不同色卡在TCD1304上的像素响应分布对比', ...
    'FontName', cnFont, 'Interpreter', 'none');
subtitleHandle = subtitle(t, '说明：2-X 为白纸基准；横轴代表光谱被C-T结构展开后落到线阵上的位置，不等同于已标定波长。', ...
    'FontName', cnFont, 'Interpreter', 'none');
legend(ax, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
set([titleHandle, subtitleHandle], 'FontWeight', 'normal');
xlim(ax, [min(xFine), max(xFine)]);

allY = comparison{:, 2:end};
allY = allY(isfinite(allY));
yMin = min(allY);
yMax = max(allY);
pad = 0.06 * (yMax - yMin);
if pad == 0
    pad = 0.05;
end
ylim(ax, [max(0, yMin - pad), yMax + pad]);

exportgraphics(fig, fullfile(outputDir, 'V3.0_去掉12组_2X白纸基准_归一化对比.png'), 'Resolution', 300);
savefig(fig, fullfile(outputDir, 'V3.0_去掉12组_2X白纸基准_归一化对比.fig'));

fprintf('Done.\n');
fprintf('Output directory: %s\n', outputDir);
fprintf('CSV: %s\n', outCsv);

function [groupId, repId] = parseGroupReplicate(fileName)
    token = regexp(fileName, '^(\d+)-(\d+)\.txt$', 'tokens', 'once');
    if isempty(token)
        error('Cannot parse file name: %s', fileName);
    end
    groupId = str2double(token{1});
    repId = str2double(token{2});
end

function name = getGroupColorName(groupId)
    switch groupId
        case 1
            name = '深粉红色';
        case 2
            name = '白色';
        case 3
            name = '橙色';
        case 4
            name = '紫色';
        case 5
            name = '粉色偏紫色';
        case 6
            name = '紫色偏蓝色';
        case 7
            name = '天蓝色';
        case 8
            name = '深绿色';
        case 9
            name = '黄色偏绿色';
        case 10
            name = '卡其色';
        case 11
            name = '粉色';
        otherwise
            name = '未知颜色';
    end
end

function color = getGroupLineColor(groupId)
    switch groupId
        case 1
            color = [0.78 0.05 0.34];  % deep pink
        case 2
            color = [0.00 0.00 0.00];  % white baseline shown as black
        case 3
            color = [0.93 0.38 0.05];  % orange
        case 4
            color = [0.43 0.18 0.68];  % purple
        case 5
            color = [0.82 0.31 0.72];  % pink-purple
        case 6
            color = [0.26 0.30 0.82];  % blue-purple
        case 7
            color = [0.20 0.62 0.95];  % sky blue
        case 8
            color = [0.05 0.34 0.17];  % dark green
        case 9
            color = [0.66 0.75 0.10];  % yellow-green
        case 10
            color = [0.67 0.57 0.34];  % khaki
        case 11
            color = [0.96 0.42 0.62];  % pink
        otherwise
            color = [0.35 0.35 0.35];
    end
end

function fontName = pickChineseFont()
    availableFonts = listfonts;
    candidates = {'Microsoft YaHei UI', 'Noto Sans SC', 'SimHei', 'PingFang SC', 'Arial'};
    fontName = 'Arial';
    for i = 1:numel(candidates)
        if any(strcmp(availableFonts, candidates{i}))
            fontName = candidates{i};
            return;
        end
    end
end
