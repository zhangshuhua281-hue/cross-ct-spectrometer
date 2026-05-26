clear; clc;

scriptDir = fileparts(mfilename('fullpath'));
rootDir = fileparts(scriptDir);
dataRoot = fileparts(rootDir);
inputDir = fullfile(dataRoot, 'V3.0采集数据');
outputDir = fullfile(rootDir, 'matlab_white_reference_without_group12', 'pairwise');

skipFirstRows = 20;
darkRows = 16;
medianWindow = 9;
sgolayWindow = 151;
sgolayOrder = 3;
interpFactor = 6;
whiteGroup = 2;
excludeGroups = 12;
pairList = [4 6; 1 5; 3 10; 6 8];
cnFont = pickChineseFont();

if ~isfolder(outputDir)
    mkdir(outputDir);
end

records = readRecords(inputDir, skipFirstRows, darkRows, medianWindow, sgolayWindow, sgolayOrder);
groups = setdiff(unique([records.Group]), excludeGroups);
groups = sort(groups);

groupMean = makeGroupMean(records, groups);
whiteIdx = find([groupMean.Group] == whiteGroup, 1, 'first');
if isempty(whiteIdx)
    error('White reference group %d was not found.', whiteGroup);
end

whitePixel = groupMean(whiteIdx).Pixel;
whiteMean = groupMean(whiteIdx).MeanSmoothed;
xFine = linspace(min(whitePixel), max(whitePixel), numel(whitePixel) * interpFactor);

whiteNormMap = containers.Map('KeyType', 'double', 'ValueType', 'any');
for i = 1:numel(groupMean)
    ratio = groupMean(i).MeanSmoothed ./ whiteMean;
    ratioFine = interp1(whitePixel, ratio, xFine, 'pchip');
    ratioFine = smoothdata(ratioFine, 'movmedian', 11);
    ratioFine = sgolayfilt(ratioFine, 3, 61);
    whiteNormMap(groupMean(i).Group) = ratioFine;
end

summary = table('Size', [0 9], ...
    'VariableTypes', {'double','double','string','string','double','double','double','double','string'}, ...
    'VariableNames', {'GroupA','GroupB','NameA','NameB','RMSE','MaxAbsDiff','MeanAbsDiff','AreaDiff','OutputPng'});

for p = 1:size(pairList, 1)
    groupA = pairList(p, 1);
    groupB = pairList(p, 2);
    if ~isKey(whiteNormMap, groupA) || ~isKey(whiteNormMap, groupB)
        warning('Skip pair %d vs %d because one group is missing.', groupA, groupB);
        continue;
    end

    yA = whiteNormMap(groupA);
    yB = whiteNormMap(groupB);
    diffY = yA - yB;
    rmse = sqrt(mean(diffY.^2, 'omitnan'));
    maxAbsDiff = max(abs(diffY), [], 'omitnan');
    meanAbsDiff = mean(abs(diffY), 'omitnan');
    areaDiff = trapz(xFine, diffY);

    nameA = getGroupColorName(groupA);
    nameB = getGroupColorName(groupB);
    fileStem = sprintf('V3.0_%02d-%02d_%s_vs_%s_白纸归一化两两对比', ...
        groupA, groupB, sanitizeName(nameA), sanitizeName(nameB));
    pngPath = fullfile(outputDir, fileStem + ".png");
    figPath = fullfile(outputDir, fileStem + ".fig");
    csvPath = fullfile(outputDir, fileStem + ".csv");

    pairTable = table(xFine(:), yA(:), yB(:), diffY(:), ...
        'VariableNames', {'Pixel', sprintf('Group%02d_WhiteNorm', groupA), ...
        sprintf('Group%02d_WhiteNorm', groupB), 'Difference_A_minus_B'});
    writetable(pairTable, csvPath, 'Encoding', 'UTF-8');

    fig = figure('Color', 'w', 'Position', [90 90 1120 520]);
    t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
    title(t, sprintf('V3.0 两两对比：%d-X %s 与 %d-X %s', groupA, nameA, groupB, nameB), ...
        'FontName', cnFont, 'Interpreter', 'none');
    subtitle(t, '横轴为TCD1304像素位置，即C-T结构展开后的空间分布；当前未做波长标定。', ...
        'FontName', cnFont, 'Interpreter', 'none');

    ax1 = nexttile;
    hold(ax1, 'on');
    grid(ax1, 'on');
    box(ax1, 'on');
    plot(ax1, xFine, yA, 'Color', getGroupLineColor(groupA), 'LineWidth', 2.2, ...
        'DisplayName', sprintf('%d-X %s', groupA, nameA));
    plot(ax1, xFine, yB, 'Color', getGroupLineColor(groupB), 'LineWidth', 2.2, ...
        'DisplayName', sprintf('%d-X %s', groupB, nameB));
    yline(ax1, 1.0, 'k:', 'LineWidth', 1.0, 'HandleVisibility', 'off');
    xlabel(ax1, 'TCD1304像素位置（C-T展开方向，未标定波长）', 'FontName', cnFont, 'Interpreter', 'none');
    ylabel(ax1, '白纸归一化相对响应', 'FontName', cnFont, 'Interpreter', 'none');
    legend(ax1, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
    set(ax1, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
    xlim(ax1, [min(xFine), max(xFine)]);

    exportgraphics(fig, pngPath, 'Resolution', 300);
    savefig(fig, figPath);
    close(fig);

    summary = [summary; {groupA, groupB, string(nameA), string(nameB), rmse, ...
        maxAbsDiff, meanAbsDiff, areaDiff, string(pngPath)}]; %#ok<AGROW>
end

writetable(summary, fullfile(outputDir, 'V3.0_白纸归一化_两两对比摘要.csv'), 'Encoding', 'UTF-8');

fprintf('Done. Pairwise figures saved to:\n%s\n', outputDir);

function records = readRecords(inputDir, skipFirstRows, darkRows, medianWindow, sgolayWindow, sgolayOrder)
    files = dir(fullfile(inputDir, '*.txt'));
    if isempty(files)
        error('No .txt files found in %s', inputDir);
    end

    records = struct('File', {}, 'Group', {}, 'Replicate', {}, 'Pixel', {}, ...
        'DarkADC', {}, 'SmoothedIntensity', {});

    for k = 1:numel(files)
        filePath = fullfile(files(k).folder, files(k).name);
        [groupId, repId] = parseGroupReplicate(files(k).name);
        data = readtable(filePath, 'FileType', 'text', 'Delimiter', '\t', 'VariableNamingRule', 'preserve');

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
            'DarkADC', darkADC, ...
            'SmoothedIntensity', intensitySmooth); %#ok<AGROW>
    end
end

function groupMean = makeGroupMean(records, groups)
    groupMean = struct('Group', {}, 'Pixel', {}, 'MeanSmoothed', {});
    for g = groups
        idx = [records.Group] == g;
        x = records(find(idx, 1, 'first')).Pixel;
        stack = cat(2, records(idx).SmoothedIntensity);
        groupMean(end + 1) = struct( ...
            'Group', g, ...
            'Pixel', x, ...
            'MeanSmoothed', mean(stack, 2, 'omitnan')); %#ok<AGROW>
    end
end

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
            color = [0.78 0.05 0.34];
        case 2
            color = [0.00 0.00 0.00];
        case 3
            color = [0.93 0.38 0.05];
        case 4
            color = [0.43 0.18 0.68];
        case 5
            color = [0.82 0.31 0.72];
        case 6
            color = [0.26 0.30 0.82];
        case 7
            color = [0.20 0.62 0.95];
        case 8
            color = [0.05 0.34 0.17];
        case 9
            color = [0.66 0.75 0.10];
        case 10
            color = [0.67 0.57 0.34];
        case 11
            color = [0.96 0.42 0.62];
        otherwise
            color = [0.35 0.35 0.35];
    end
end

function safeName = sanitizeName(name)
    safeName = regexprep(char(name), '[^\w\u4e00-\u9fa5]+', '');
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
