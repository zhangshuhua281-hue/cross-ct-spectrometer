clear; clc;

scriptDir = fileparts(mfilename('fullpath'));
rootDir = fileparts(scriptDir);
dataRoot = fileparts(rootDir);
inputDir = fullfile(dataRoot, 'V3.0采集数据');
outputDir = fullfile(rootDir, 'matlab_area_normalized_pixel_distribution_without_group12');
pairOutputDir = fullfile(outputDir, 'pairwise');

skipFirstRows = 20;
darkRows = 16;
medianWindow = 9;
sgolayWindow = 151;
sgolayOrder = 3;
interpFactor = 6;
whiteGroup = 2;
excludeGroups = 12;
pairList = [4 6; 1 5; 3 10; 9 8];
cnFont = pickChineseFont();
anchorTable = readWavelengthAnchors(rootDir);

if ~isfolder(outputDir)
    mkdir(outputDir);
end
if ~isfolder(pairOutputDir)
    mkdir(pairOutputDir);
end

records = readRecords(inputDir, skipFirstRows, darkRows, medianWindow, sgolayWindow, sgolayOrder);
groups = setdiff(unique([records.Group]), excludeGroups);
groups = sort(groups);

if ~ismember(whiteGroup, groups)
    error('White reference group %d not found.', whiteGroup);
end

groupMean = makeGroupMean(records, groups);
whiteIdx = find([groupMean.Group] == whiteGroup, 1, 'first');
whitePixel = groupMean(whiteIdx).Pixel;
whiteMean = groupMean(whiteIdx).MeanSmoothed;
xFine = linspace(min(whitePixel), max(whitePixel), numel(whitePixel) * interpFactor);
pixelSpan = max(xFine) - min(xFine);

comparison = table(xFine(:), 'VariableNames', {'Pixel'});
summaryRows = table('Size', [0 7], ...
    'VariableTypes', {'double', 'string', 'double', 'double', 'double', 'double', 'double'}, ...
    'VariableNames', {'Group', 'ColorName', 'PeakPixel', 'MinPixel', ...
    'AreaBeforeNorm', 'MaxShapeValue', 'MinShapeValue'});

shapeMap = containers.Map('KeyType', 'double', 'ValueType', 'any');

for i = 1:numel(groupMean)
    g = groupMean(i).Group;
    ratio = groupMean(i).MeanSmoothed ./ whiteMean;
    ratioFine = interp1(whitePixel, ratio, xFine, 'pchip');
    ratioFine = smoothdata(ratioFine, 'movmedian', 11);
    ratioFine = sgolayfilt(ratioFine, 3, 61);
    ratioFine = max(ratioFine, 0);

    areaBeforeNorm = trapz(xFine, ratioFine);
    if areaBeforeNorm <= 0 || ~isfinite(areaBeforeNorm)
        error('Group %d has invalid area before normalization.', g);
    end

    % Area-normalized distribution, scaled by pixel span so the average is near 1.
    shapeCurve = ratioFine ./ areaBeforeNorm .* pixelSpan;
    shapeMap(g) = shapeCurve;

    colName = sprintf('Group%02d_AreaNormShape', g);
    comparison.(colName) = shapeCurve(:);

    [mx, maxIdx] = max(shapeCurve, [], 'omitnan');
    [mn, minIdx] = min(shapeCurve, [], 'omitnan');
    summaryRows = [summaryRows; {g, string(getGroupColorName(g)), xFine(maxIdx), ...
        xFine(minIdx), areaBeforeNorm, mx, mn}]; %#ok<AGROW>
end

writetable(comparison, fullfile(outputDir, 'V3.0_去掉12组_面积归一化像素分布对比.csv'), 'Encoding', 'UTF-8');
writetable(summaryRows, fullfile(outputDir, 'V3.0_去掉12组_面积归一化像素分布摘要.csv'), 'Encoding', 'UTF-8');

plotAllGroups(comparison, groupMean, shapeMap, xFine, whiteGroup, outputDir, cnFont, anchorTable);
plotPairwise(pairList, shapeMap, xFine, pairOutputDir, cnFont, anchorTable);

fprintf('Done.\n');
fprintf('Output directory: %s\n', outputDir);

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

function plotAllGroups(comparison, groupMean, shapeMap, xFine, whiteGroup, outputDir, cnFont, anchorTable)
    fig = figure('Color', 'w', 'Position', [80 80 1380 720]);
    t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
    ax = nexttile(t);
    hold(ax, 'on');
    grid(ax, 'on');
    box(ax, 'on');

    for i = 1:numel(groupMean)
        g = groupMean(i).Group;
        y = shapeMap(g);
        if g == whiteGroup
            plot(ax, xFine, y, 'k-', 'LineWidth', 2.8, ...
                'DisplayName', sprintf('%d-X %s / 白纸基准', g, getGroupColorName(g)));
        else
            plot(ax, xFine, y, 'Color', getGroupLineColor(g), 'LineWidth', 1.8, ...
                'DisplayName', sprintf('%d-X %s', g, getGroupColorName(g)));
        end
    end

    yline(ax, 1.0, 'k:', 'LineWidth', 1.2, 'HandleVisibility', 'off');
    for a = 1:height(anchorTable)
        xline(ax, anchorTable.MeanCentroidPixel(a), '--', ...
            sprintf('%.0fnm锚点', anchorTable.NominalWavelength_nm(a)), ...
            'Color', [0.10 0.10 0.10], 'LineWidth', 1.4, ...
            'LabelOrientation', 'horizontal', 'LabelVerticalAlignment', 'bottom', ...
            'FontName', cnFont, 'HandleVisibility', 'off');
    end
    xlabel(ax, 'TCD1304像素位置（C-T展开方向，未标定波长）', 'FontName', cnFont, 'Interpreter', 'none');
    ylabel(ax, '面积归一化像素分布（平均值=1）', 'FontName', cnFont, 'Interpreter', 'none');
    title(t, 'V3.0 不同色卡的TCD1304像素分布形状对比', ...
        'FontName', cnFont, 'Interpreter', 'none');
    subtitle(t, '处理：暗电平校正 + 2-X白纸校正 + 面积归一化；竖线为405/780nm标定锚点。', ...
        'FontName', cnFont, 'Interpreter', 'none');
    legend(ax, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
    set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
    xlim(ax, [min(xFine), max(xFine)]);

    allY = comparison{:, 2:end};
    allY = allY(isfinite(allY));
    yMin = min(allY);
    yMax = max(allY);
    pad = 0.06 * (yMax - yMin);
    if pad == 0
        pad = 0.05;
    end
    ylim(ax, [yMin - pad, yMax + pad]);

    exportgraphics(fig, fullfile(outputDir, 'V3.0_去掉12组_面积归一化像素分布对比.png'), 'Resolution', 300);
    savefig(fig, fullfile(outputDir, 'V3.0_去掉12组_面积归一化像素分布对比.fig'));
    close(fig);
end

function plotPairwise(pairList, shapeMap, xFine, pairOutputDir, cnFont, anchorTable)
    summary = table('Size', [0 9], ...
        'VariableTypes', {'double','double','string','string','double','double','double','double','string'}, ...
        'VariableNames', {'GroupA','GroupB','NameA','NameB','RMSE','MaxAbsDiff','MeanAbsDiff','AreaDiff','OutputPng'});

    for p = 1:size(pairList, 1)
        groupA = pairList(p, 1);
        groupB = pairList(p, 2);
        if ~isKey(shapeMap, groupA) || ~isKey(shapeMap, groupB)
            warning('Skip pair %d vs %d because one group is missing.', groupA, groupB);
            continue;
        end

        yA = shapeMap(groupA);
        yB = shapeMap(groupB);
        diffY = yA - yB;
        rmse = sqrt(mean(diffY.^2, 'omitnan'));
        maxAbsDiff = max(abs(diffY), [], 'omitnan');
        meanAbsDiff = mean(abs(diffY), 'omitnan');
        areaDiff = trapz(xFine, diffY);

        nameA = getGroupColorName(groupA);
        nameB = getGroupColorName(groupB);
        fileStem = sprintf('V3.0_%02d-%02d_%s_vs_%s_面积归一化像素分布对比', ...
            groupA, groupB, sanitizeName(nameA), sanitizeName(nameB));
        pngPath = fullfile(pairOutputDir, fileStem + ".png");
        figPath = fullfile(pairOutputDir, fileStem + ".fig");
        csvPath = fullfile(pairOutputDir, fileStem + ".csv");

        pairTable = table(xFine(:), yA(:), yB(:), diffY(:), ...
            'VariableNames', {'Pixel', sprintf('Group%02d_AreaNormShape', groupA), ...
            sprintf('Group%02d_AreaNormShape', groupB), 'Difference_A_minus_B'});
        writetable(pairTable, csvPath, 'Encoding', 'UTF-8');

        fig = figure('Color', 'w', 'Position', [90 90 1120 520]);
        t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
        title(t, sprintf('V3.0 像素分布形状对比：%d-X %s 与 %d-X %s', ...
            groupA, nameA, groupB, nameB), 'FontName', cnFont, 'Interpreter', 'none');
        subtitle(t, '两条曲线均已做白纸校正和面积归一化，总亮度影响被压制，主要比较峰谷位置和形状。', ...
            'FontName', cnFont, 'Interpreter', 'none');

        ax = nexttile(t);
        hold(ax, 'on');
        grid(ax, 'on');
        box(ax, 'on');
        plot(ax, xFine, yA, 'Color', getGroupLineColor(groupA), 'LineWidth', 2.2, ...
            'DisplayName', sprintf('%d-X %s', groupA, nameA));
        plot(ax, xFine, yB, 'Color', getGroupLineColor(groupB), 'LineWidth', 2.2, ...
            'DisplayName', sprintf('%d-X %s', groupB, nameB));
        yline(ax, 1.0, 'k:', 'LineWidth', 1.0, 'HandleVisibility', 'off');
        for a = 1:height(anchorTable)
            xline(ax, anchorTable.MeanCentroidPixel(a), '--', ...
                sprintf('%.0fnm锚点', anchorTable.NominalWavelength_nm(a)), ...
                'Color', [0.10 0.10 0.10], 'LineWidth', 1.3, ...
                'LabelOrientation', 'horizontal', 'LabelVerticalAlignment', 'bottom', ...
                'FontName', cnFont, 'HandleVisibility', 'off');
        end
        xlabel(ax, 'TCD1304像素位置（C-T展开方向，未标定波长）', 'FontName', cnFont, 'Interpreter', 'none');
        ylabel(ax, '面积归一化像素分布（平均值=1）', 'FontName', cnFont, 'Interpreter', 'none');
        legend(ax, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
        set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
        xlim(ax, [min(xFine), max(xFine)]);

        yMin = min([yA(:); yB(:)], [], 'omitnan');
        yMax = max([yA(:); yB(:)], [], 'omitnan');
        pad = 0.08 * (yMax - yMin);
        if pad == 0
            pad = 0.05;
        end
        ylim(ax, [yMin - pad, yMax + pad]);

        exportgraphics(fig, pngPath, 'Resolution', 300);
        savefig(fig, figPath);
        close(fig);

        summary = [summary; {groupA, groupB, string(nameA), string(nameB), rmse, ...
            maxAbsDiff, meanAbsDiff, areaDiff, string(pngPath)}]; %#ok<AGROW>
    end

    writetable(summary, fullfile(pairOutputDir, 'V3.0_面积归一化像素分布_两两对比摘要.csv'), 'Encoding', 'UTF-8');
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

function anchorTable = readWavelengthAnchors(rootDir)
    anchorPath = fullfile(rootDir, 'wavelength_calibration_405_780', '405_780_波长锚点汇总.csv');
    if isfile(anchorPath)
        anchorTable = readtable(anchorPath, 'VariableNamingRule', 'preserve');
        anchorTable = anchorTable(:, {'NominalWavelength_nm', 'MeanCentroidPixel'});
        return;
    end

    anchorPath = fullfile(rootDir, 'wavelength_calibration_405nm', '405nm_单点波长锚定结果.csv');
    if isfile(anchorPath)
        singleAnchor = readtable(anchorPath, 'VariableNamingRule', 'preserve');
        anchorTable = table(singleAnchor.AnchorWavelength_nm(1), singleAnchor.MeanCentroidPixel(1), ...
            'VariableNames', {'NominalWavelength_nm', 'MeanCentroidPixel'});
    else
        anchorTable = table('Size', [0 2], ...
            'VariableTypes', {'double', 'double'}, ...
            'VariableNames', {'NominalWavelength_nm', 'MeanCentroidPixel'});
    end
end
