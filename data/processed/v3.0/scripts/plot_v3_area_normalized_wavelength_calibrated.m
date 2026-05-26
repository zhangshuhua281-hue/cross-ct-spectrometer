clear; clc;

scriptDir = fileparts(mfilename('fullpath'));
rootDir = fileparts(scriptDir);
inputDir = fullfile(rootDir, 'matlab_area_normalized_pixel_distribution_without_group12');
calibrationDir = fullfile(rootDir, 'wavelength_calibration_405_780');
outputDir = fullfile(rootDir, 'matlab_area_normalized_wavelength_calibrated_without_group12');
pairOutputDir = fullfile(outputDir, 'pairwise');
cnFont = pickChineseFont();

pairList = [4 6; 1 5; 3 10; 9 8];
whiteGroup = 2;

if ~isfolder(outputDir)
    mkdir(outputDir);
end
if ~isfolder(pairOutputDir)
    mkdir(pairOutputDir);
end

comparisonPath = fullfile(inputDir, 'V3.0_去掉12组_面积归一化像素分布对比.csv');
calibrationPath = fullfile(calibrationDir, '405_780_线性波长标定公式.csv');
if ~isfile(comparisonPath)
    error('Cannot find area-normalized comparison table: %s', comparisonPath);
end
if ~isfile(calibrationPath)
    error('Cannot find wavelength calibration table: %s', calibrationPath);
end

comparison = readtable(comparisonPath, 'VariableNamingRule', 'preserve');
calibration = readtable(calibrationPath, 'VariableNamingRule', 'preserve');
slope = calibration.Slope_nm_per_pixel(1);
intercept = calibration.Intercept_nm(1);
validMinNm = calibration.ValidMin_nm(1);
validMaxNm = calibration.ValidMax_nm(1);

pixel = comparison.Pixel;
wavelength = slope .* pixel + intercept;
validMask = wavelength >= validMinNm & wavelength <= validMaxNm;
[wavelengthValid, order] = sort(wavelength(validMask), 'ascend');

curveVars = comparison.Properties.VariableNames(2:end);
wavelengthTable = table(wavelengthValid(:), 'VariableNames', {'Wavelength_nm'});
for i = 1:numel(curveVars)
    y = comparison.(curveVars{i});
    yValid = y(validMask);
    wavelengthTable.(curveVars{i}) = yValid(order);
end

writetable(wavelengthTable, fullfile(outputDir, 'V3.0_去掉12组_405-780nm线性标定_面积归一化对比.csv'), 'Encoding', 'UTF-8');

groups = parseGroupsFromVariables(curveVars);
shapeMap = containers.Map('KeyType', 'double', 'ValueType', 'any');
for i = 1:numel(groups)
    colName = sprintf('Group%02d_AreaNormShape', groups(i));
    shapeMap(groups(i)) = wavelengthTable.(colName);
end

plotAllGroups(wavelengthTable.Wavelength_nm, groups, shapeMap, whiteGroup, outputDir, cnFont, ...
    slope, intercept, validMinNm, validMaxNm);
plotPairwise(pairList, wavelengthTable.Wavelength_nm, shapeMap, pairOutputDir, cnFont, ...
    slope, intercept, validMinNm, validMaxNm);

fprintf('Done.\n');
fprintf('Wavelength-calibrated output directory: %s\n', outputDir);

function plotAllGroups(wavelengthNm, groups, shapeMap, whiteGroup, outputDir, cnFont, ...
    slope, intercept, validMinNm, validMaxNm)
    fig = figure('Color', 'w', 'Position', [80 80 1380 720]);
    t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
    ax = nexttile(t);
    hold(ax, 'on');
    grid(ax, 'on');
    box(ax, 'on');

    for i = 1:numel(groups)
        g = groups(i);
        y = shapeMap(g);
        if g == whiteGroup
            plot(ax, wavelengthNm, y, 'k-', 'LineWidth', 2.8, ...
                'DisplayName', sprintf('%d-X %s / 白纸基准', g, getGroupColorName(g)));
        else
            plot(ax, wavelengthNm, y, 'Color', getGroupLineColor(g), 'LineWidth', 1.8, ...
                'DisplayName', sprintf('%d-X %s', g, getGroupColorName(g)));
        end
    end

    yline(ax, 1.0, 'k:', 'LineWidth', 1.2, 'HandleVisibility', 'off');
    xline(ax, validMinNm, '--', '405nm锚点', 'Color', [0.15 0.15 0.15], ...
        'LineWidth', 1.2, 'FontName', cnFont, 'LabelVerticalAlignment', 'bottom', ...
        'HandleVisibility', 'off');
    xline(ax, validMaxNm, '--', '780nm锚点', 'Color', [0.15 0.15 0.15], ...
        'LineWidth', 1.2, 'FontName', cnFont, 'LabelVerticalAlignment', 'bottom', ...
        'HandleVisibility', 'off');

    xlabel(ax, '近似波长 / nm（405/780nm双点线性标定）', 'FontName', cnFont, 'Interpreter', 'none');
    ylabel(ax, '面积归一化响应（平均值=1）', 'FontName', cnFont, 'Interpreter', 'none');
    title(t, 'V3.0 不同色卡的近似波长分布形状对比', ...
        'FontName', cnFont, 'Interpreter', 'none');
    subtitle(t, sprintf('λ = %.6f × Pixel + %.2f；当前为两点线性近似，主要用于展示分光趋势。', ...
        slope, intercept), 'FontName', cnFont, 'Interpreter', 'none');
    legend(ax, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
    set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
    xlim(ax, [validMinNm, validMaxNm]);

    allY = [];
    for i = 1:numel(groups)
        y = shapeMap(groups(i));
        allY = [allY; y(:)]; %#ok<AGROW>
    end
    yMin = min(allY, [], 'omitnan');
    yMax = max(allY, [], 'omitnan');
    pad = 0.06 * (yMax - yMin);
    if pad == 0
        pad = 0.05;
    end
    ylim(ax, [yMin - pad, yMax + pad]);

    exportgraphics(fig, fullfile(outputDir, 'V3.0_去掉12组_405-780nm线性标定_面积归一化对比.png'), 'Resolution', 300);
    savefig(fig, fullfile(outputDir, 'V3.0_去掉12组_405-780nm线性标定_面积归一化对比.fig'));
    close(fig);
end

function plotPairwise(pairList, wavelengthNm, shapeMap, pairOutputDir, cnFont, ...
    slope, intercept, validMinNm, validMaxNm)
    summary = table('Size', [0 9], ...
        'VariableTypes', {'double','double','string','string','double','double','double','double','string'}, ...
        'VariableNames', {'GroupA','GroupB','NameA','NameB','RMSE','MaxAbsDiff','MeanAbsDiff','AreaDiff_nm','OutputPng'});

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
        areaDiff = trapz(wavelengthNm, diffY);

        nameA = getGroupColorName(groupA);
        nameB = getGroupColorName(groupB);
        fileStem = sprintf('V3.0_%02d-%02d_%s_vs_%s_405-780nm线性标定面积归一化对比', ...
            groupA, groupB, sanitizeName(nameA), sanitizeName(nameB));
        pngPath = fullfile(pairOutputDir, fileStem + ".png");
        figPath = fullfile(pairOutputDir, fileStem + ".fig");
        csvPath = fullfile(pairOutputDir, fileStem + ".csv");

        pairTable = table(wavelengthNm(:), yA(:), yB(:), diffY(:), ...
            'VariableNames', {'Wavelength_nm', sprintf('Group%02d_AreaNormShape', groupA), ...
            sprintf('Group%02d_AreaNormShape', groupB), 'Difference_A_minus_B'});
        writetable(pairTable, csvPath, 'Encoding', 'UTF-8');

        fig = figure('Color', 'w', 'Position', [90 90 1120 520]);
        t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
        title(t, sprintf('V3.0 近似波长分布对比：%d-X %s 与 %d-X %s', ...
            groupA, nameA, groupB, nameB), 'FontName', cnFont, 'Interpreter', 'none');
        subtitle(t, sprintf('405/780nm双点线性标定：λ = %.6f × Pixel + %.2f；比较405-780nm范围内形状。', ...
            slope, intercept), 'FontName', cnFont, 'Interpreter', 'none');

        ax = nexttile(t);
        hold(ax, 'on');
        grid(ax, 'on');
        box(ax, 'on');
        plot(ax, wavelengthNm, yA, 'Color', getGroupLineColor(groupA), 'LineWidth', 2.2, ...
            'DisplayName', sprintf('%d-X %s', groupA, nameA));
        plot(ax, wavelengthNm, yB, 'Color', getGroupLineColor(groupB), 'LineWidth', 2.2, ...
            'DisplayName', sprintf('%d-X %s', groupB, nameB));
        yline(ax, 1.0, 'k:', 'LineWidth', 1.0, 'HandleVisibility', 'off');
        xline(ax, validMinNm, '--', '405nm', 'Color', [0.15 0.15 0.15], ...
            'LineWidth', 1.0, 'FontName', cnFont, 'LabelVerticalAlignment', 'bottom', ...
            'HandleVisibility', 'off');
        xline(ax, validMaxNm, '--', '780nm', 'Color', [0.15 0.15 0.15], ...
            'LineWidth', 1.0, 'FontName', cnFont, 'LabelVerticalAlignment', 'bottom', ...
            'HandleVisibility', 'off');
        xlabel(ax, '近似波长 / nm（405/780nm双点线性标定）', 'FontName', cnFont, 'Interpreter', 'none');
        ylabel(ax, '面积归一化响应（平均值=1）', 'FontName', cnFont, 'Interpreter', 'none');
        legend(ax, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
        set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
        xlim(ax, [validMinNm, validMaxNm]);

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

    writetable(summary, fullfile(pairOutputDir, 'V3.0_405-780nm线性标定_两两对比摘要.csv'), 'Encoding', 'UTF-8');
end

function groups = parseGroupsFromVariables(curveVars)
    groups = zeros(numel(curveVars), 1);
    for i = 1:numel(curveVars)
        token = regexp(curveVars{i}, '^Group(\d+)_', 'tokens', 'once');
        if isempty(token)
            error('Cannot parse group from variable name: %s', curveVars{i});
        end
        groups(i) = str2double(token{1});
    end
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
