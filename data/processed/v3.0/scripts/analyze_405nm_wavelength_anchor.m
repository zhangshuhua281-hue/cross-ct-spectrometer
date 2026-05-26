clear; clc;

scriptDir = fileparts(mfilename('fullpath'));
rootDir = fileparts(scriptDir);
dataRoot = fileparts(rootDir);
inputDir = fullfile(dataRoot, '405nm波长');
outputDir = fullfile(rootDir, 'wavelength_calibration_405nm');
cnFont = pickChineseFont();

skipFirstRows = 20;
darkRows = 16;
medianWindow = 9;
sgolayWindow = 101;
sgolayOrder = 3;
centroidHalfWindow = 60;
searchPixelRange = [2300 2700];
baselinePixelRange = [2300 2380; 2630 2700];

if ~isfolder(outputDir)
    mkdir(outputDir);
end

files = dir(fullfile(inputDir, '*.txt'));
if isempty(files)
    error('No .txt files found in %s', inputDir);
end

summary = table('Size', [0 10], ...
    'VariableTypes', {'string','double','double','double','double','double','double','double','double','double'}, ...
    'VariableNames', {'File','LabelFromName_nm','DarkADC','PeakPixelRaw','PeakPixelSmooth', ...
    'CentroidPixel','PeakExcessIntensity','FWHM_pixel','LeftHalfPixel','RightHalfPixel'});

curveMap = containers.Map('KeyType', 'char', 'ValueType', 'any');

for k = 1:numel(files)
    filePath = fullfile(files(k).folder, files(k).name);
    data = readtable(filePath, 'FileType', 'text', 'Delimiter', '\t', 'VariableNamingRule', 'preserve');
    xAll = double(data.Pixel);
    adcAll = double(data.ADC);

    darkADC = median(adcAll(1:darkRows), 'omitnan');
    validIdx = skipFirstRows + 1:height(data);
    x = xAll(validIdx);
    adc = adcAll(validIdx);

    intensity = darkADC - adc;
    intensity = max(intensity, 0);
    intensityMed = smoothdata(intensity, 'movmedian', medianWindow);
    intensitySmooth = sgolayfilt(intensityMed, sgolayOrder, sgolayWindow);
    intensitySmooth = max(intensitySmooth, 0);

    [rawPeakIntensity, rawIdxRel] = max(intensity, [], 'omitnan');
    rawPeakPixel = x(rawIdxRel);

    searchMask = x >= searchPixelRange(1) & x <= searchPixelRange(2);
    xLocal = x(searchMask);
    intensityLocal = intensitySmooth(searchMask);
    baselineMask = (xLocal >= baselinePixelRange(1, 1) & xLocal <= baselinePixelRange(1, 2)) | ...
        (xLocal >= baselinePixelRange(2, 1) & xLocal <= baselinePixelRange(2, 2));
    baselineFit = polyfit(xLocal(baselineMask), intensityLocal(baselineMask), 1);
    baselineLocal = polyval(baselineFit, xLocal);
    peakExcess = max(intensityLocal - baselineLocal, 0);

    [smoothPeakIntensity, peakIdxLocal] = max(peakExcess, [], 'omitnan');
    smoothPeakPixel = xLocal(peakIdxLocal);

    winMask = abs(xLocal - smoothPeakPixel) <= centroidHalfWindow;
    weights = peakExcess(winMask);
    centroidPixel = sum(xLocal(winMask) .* weights, 'omitnan') / sum(weights, 'omitnan');

    [leftHalfPixel, rightHalfPixel, fwhmPixel] = estimateFwhm(xLocal, peakExcess, peakIdxLocal);
    labelNm = 405;

    summary = [summary; {string(files(k).name), labelNm, darkADC, rawPeakPixel, ...
        smoothPeakPixel, centroidPixel, smoothPeakIntensity, fwhmPixel, leftHalfPixel, rightHalfPixel}]; %#ok<AGROW>

    curveMap(files(k).name) = struct('Pixel', x, 'Intensity', intensity, ...
        'SmoothedIntensity', intensitySmooth, 'LabelNm', labelNm, ...
        'CentroidPixel', centroidPixel, 'SmoothPeakPixel', smoothPeakPixel, ...
        'LocalPixel', xLocal, 'LocalPeakExcess', peakExcess, ...
        'SearchPixelRange', searchPixelRange);
end

meanCentroid = mean(summary.CentroidPixel, 'omitnan');
stdCentroid = std(summary.CentroidPixel, 'omitnan');
meanPeak = mean(summary.PeakPixelSmooth, 'omitnan');
stdPeak = std(summary.PeakPixelSmooth, 'omitnan');
meanFwhm = mean(summary.FWHM_pixel, 'omitnan');

writetable(summary, fullfile(outputDir, '405nm_峰位检测摘要.csv'), 'Encoding', 'UTF-8');

anchor = table(405, meanCentroid, stdCentroid, meanPeak, stdPeak, meanFwhm, height(summary), ...
    'VariableNames', {'AnchorWavelength_nm','MeanCentroidPixel','StdCentroidPixel', ...
    'MeanSmoothedPeakPixel','StdSmoothedPeakPixel','MeanFWHM_pixel','FileCount'});
writetable(anchor, fullfile(outputDir, '405nm_单点波长锚定结果.csv'), 'Encoding', 'UTF-8');

plotLaserOverlay(files, curveMap, summary, meanCentroid, outputDir, cnFont, searchPixelRange);
writeReport(outputDir, meanCentroid, stdCentroid, meanPeak, stdPeak, meanFwhm, height(summary), searchPixelRange);

fprintf('Done.\n');
fprintf('405 nm centroid anchor pixel: %.2f +/- %.2f px\n', meanCentroid, stdCentroid);
fprintf('Output directory: %s\n', outputDir);

function plotLaserOverlay(files, curveMap, summary, meanCentroid, outputDir, cnFont, searchPixelRange)
    fig = figure('Color', 'w', 'Position', [80 80 1260 680]);
    t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
    ax = nexttile(t);
    hold(ax, 'on');
    grid(ax, 'on');
    box(ax, 'on');

    colors = lines(numel(files));
    for k = 1:numel(files)
        c = curveMap(files(k).name);
        y = c.LocalPeakExcess ./ max(c.LocalPeakExcess, [], 'omitnan');
        plot(ax, c.LocalPixel, y, 'LineWidth', 1.2, 'Color', colors(k, :), ...
            'DisplayName', erase(files(k).name, '.txt'));
    end

    xline(ax, meanCentroid, 'k-', 'LineWidth', 2.2, ...
        'DisplayName', sprintf('405nm平均质心 %.1f px', meanCentroid));
    xlabel(ax, 'TCD1304像素位置', 'FontName', cnFont, 'Interpreter', 'none');
    ylabel(ax, '归一化激光响应', 'FontName', cnFont, 'Interpreter', 'none');
    title(t, '405nm激光局部峰在TCD1304上的落点检测', 'FontName', cnFont, 'Interpreter', 'none');
    subtitle(t, sprintf('仅在%d-%d像素内扣除慢变化背景后检测峰位；单点锚定不能单独推出完整波长轴。', ...
        searchPixelRange(1), searchPixelRange(2)), ...
        'FontName', cnFont, 'Interpreter', 'none');
    legend(ax, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
    set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
    xlim(ax, searchPixelRange);
    ylim(ax, [-0.03, 1.08]);

    exportgraphics(fig, fullfile(outputDir, '405nm_激光峰落点检测.png'), 'Resolution', 300);
    savefig(fig, fullfile(outputDir, '405nm_激光峰落点检测.fig'));
    close(fig);

    fig2 = figure('Color', 'w', 'Position', [120 120 820 520]);
    ax2 = axes(fig2);
    hold(ax2, 'on');
    grid(ax2, 'on');
    box(ax2, 'on');
    labels = categorical(summary.File);
    labels = reordercats(labels, cellstr(summary.File));
    bar(ax2, labels, summary.CentroidPixel, 'FaceColor', [0.25 0.45 0.80]);
    yline(ax2, meanCentroid, 'k-', 'LineWidth', 1.8, ...
        'DisplayName', sprintf('平均 %.2f px', meanCentroid));
    ylabel(ax2, '405nm峰质心像素', 'FontName', cnFont, 'Interpreter', 'none');
    title(ax2, '各次405nm测量的峰位稳定性', 'FontName', cnFont, 'Interpreter', 'none');
    set(ax2, 'FontName', cnFont, 'FontSize', 10, 'LineWidth', 1);
    xtickangle(ax2, 35);
    exportgraphics(fig2, fullfile(outputDir, '405nm_峰位稳定性.png'), 'Resolution', 300);
    savefig(fig2, fullfile(outputDir, '405nm_峰位稳定性.fig'));
    close(fig2);
end

function writeReport(outputDir, meanCentroid, stdCentroid, meanPeak, stdPeak, meanFwhm, fileCount, searchPixelRange)
    reportPath = fullfile(outputDir, '405nm_波长标定判断.txt');
    fid = fopen(reportPath, 'w', 'n', 'UTF-8');
    cleaner = onCleanup(@() fclose(fid));

    fprintf(fid, '405nm波长标定判断\n');
    fprintf(fid, '====================\n\n');
    fprintf(fid, '检测结果：\n');
    fprintf(fid, '- 有效文件数：%d\n', fileCount);
    fprintf(fid, '- 峰搜索范围：%d-%d px\n', searchPixelRange(1), searchPixelRange(2));
    fprintf(fid, '- 405nm峰质心平均像素：%.2f px\n', meanCentroid);
    fprintf(fid, '- 405nm峰质心标准差：%.2f px\n', stdCentroid);
    fprintf(fid, '- 平滑峰值平均像素：%.2f px\n', meanPeak);
    fprintf(fid, '- 平滑峰值标准差：%.2f px\n', stdPeak);
    fprintf(fid, '- 平均半高宽：%.2f px\n\n', meanFwhm);

    fprintf(fid, '结论：\n');
    fprintf(fid, '这组405nm数据可以用于单点波长锚定，即把TCD1304上约 %.2f px 的位置标记为405nm。\n', meanCentroid);
    fprintf(fid, '但是，仅靠一个已知波长点，不能唯一确定完整的像素-波长转换关系。\n\n');

    fprintf(fid, '原因：\n');
    fprintf(fid, '完整标定至少需要确定零点和斜率；若考虑C-T结构和光栅非线性，还需要更多标定点来拟合多项式或光栅方程。\n\n');

    fprintf(fid, '建议：\n');
    fprintf(fid, '1. 当前比赛图中可以增加一条“405nm锚点”竖线，证明系统把405nm映射到了固定像素位置。\n');
    fprintf(fid, '2. 若要把横轴正式改成波长nm，至少再测一个已知波长，例如532nm绿光或650nm红光。\n');
    fprintf(fid, '3. 有两个点可做线性近似标定；有三个及以上点可做更可靠的非线性/多项式标定。\n');
end

function [leftHalfPixel, rightHalfPixel, fwhmPixel] = estimateFwhm(x, y, peakIdx)
    peakValue = y(peakIdx);
    halfValue = peakValue / 2;

    leftIdx = find(y(1:peakIdx) <= halfValue, 1, 'last');
    if isempty(leftIdx) || leftIdx == peakIdx
        leftHalfPixel = NaN;
    else
        leftHalfPixel = interp1(y(leftIdx:leftIdx+1), x(leftIdx:leftIdx+1), halfValue, 'linear', 'extrap');
    end

    rightRel = find(y(peakIdx:end) <= halfValue, 1, 'first');
    if isempty(rightRel) || rightRel == 1
        rightHalfPixel = NaN;
    else
        rightIdx = peakIdx + rightRel - 1;
        rightHalfPixel = interp1(y(rightIdx-1:rightIdx), x(rightIdx-1:rightIdx), halfValue, 'linear', 'extrap');
    end

    fwhmPixel = rightHalfPixel - leftHalfPixel;
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
