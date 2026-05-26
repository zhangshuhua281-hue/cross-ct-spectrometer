clear; clc;

scriptDir = fileparts(mfilename('fullpath'));
rootDir = fileparts(scriptDir);
dataRoot = fileparts(rootDir);
outputDir = fullfile(rootDir, 'wavelength_calibration_405_780');
cnFont = pickChineseFont();

configs = [
    struct('FolderName', '405nm波长', 'SearchRange', [2300 2700], ...
        'BaselineRanges', [2300 2380; 2630 2700], 'CentroidHalfWindow', 60, ...
        'NominalWavelength_nm', 405)
    struct('FolderName', '780nm波长', 'SearchRange', [900 1350], ...
        'BaselineRanges', [900 980; 1280 1350], 'CentroidHalfWindow', 80, ...
        'NominalWavelength_nm', 780)
];

skipFirstRows = 20;
darkRows = 16;
medianWindow = 9;
sgolayWindow = 101;
sgolayOrder = 3;

if ~isfolder(outputDir)
    mkdir(outputDir);
end

fileSummary = table('Size', [0 11], ...
    'VariableTypes', {'string','double','string','double','double','double','double','double','double','double','double'}, ...
    'VariableNames', {'File','NominalWavelength_nm','Folder','DarkADC','RawPeakPixel', ...
    'LocalPeakPixel','CentroidPixel','PeakExcessIntensity','FWHM_pixel', ...
    'LeftHalfPixel','RightHalfPixel'});

curves = struct('File', {}, 'NominalWavelength_nm', {}, 'Pixel', {}, 'PeakExcess', {}, ...
    'CentroidPixel', {}, 'SearchRange', {});

for c = 1:numel(configs)
    inputDir = fullfile(dataRoot, configs(c).FolderName);
    files = dir(fullfile(inputDir, '*.txt'));
    if isempty(files)
        warning('No .txt files found in %s', inputDir);
        continue;
    end

    for k = 1:numel(files)
        filePath = fullfile(files(k).folder, files(k).name);
        nominalNm = configs(c).NominalWavelength_nm;
        data = readtable(filePath, 'FileType', 'text', 'Delimiter', '\t', 'VariableNamingRule', 'preserve');
        xAll = double(data.Pixel);
        adcAll = double(data.ADC);

        darkADC = median(adcAll(1:darkRows), 'omitnan');
        validIdx = skipFirstRows + 1:height(data);
        x = xAll(validIdx);
        adc = adcAll(validIdx);

        intensity = max(darkADC - adc, 0);
        intensityMed = smoothdata(intensity, 'movmedian', medianWindow);
        intensitySmooth = sgolayfilt(intensityMed, sgolayOrder, sgolayWindow);
        intensitySmooth = max(intensitySmooth, 0);

        [~, rawIdxRel] = max(intensity, [], 'omitnan');
        rawPeakPixel = x(rawIdxRel);

        searchMask = x >= configs(c).SearchRange(1) & x <= configs(c).SearchRange(2);
        xLocal = x(searchMask);
        yLocal = intensitySmooth(searchMask);
        baselineMask = false(size(xLocal));
        for b = 1:size(configs(c).BaselineRanges, 1)
            baselineMask = baselineMask | ...
                (xLocal >= configs(c).BaselineRanges(b, 1) & xLocal <= configs(c).BaselineRanges(b, 2));
        end

        baselineFit = polyfit(xLocal(baselineMask), yLocal(baselineMask), 1);
        yBaseline = polyval(baselineFit, xLocal);
        peakExcess = max(yLocal - yBaseline, 0);

        [peakExcessIntensity, peakIdxLocal] = max(peakExcess, [], 'omitnan');
        localPeakPixel = xLocal(peakIdxLocal);

        winMask = abs(xLocal - localPeakPixel) <= configs(c).CentroidHalfWindow;
        weights = peakExcess(winMask);
        centroidPixel = sum(xLocal(winMask) .* weights, 'omitnan') / sum(weights, 'omitnan');

        [leftHalfPixel, rightHalfPixel, fwhmPixel] = estimateFwhm(xLocal, peakExcess, peakIdxLocal);

        fileSummary = [fileSummary; {string(files(k).name), nominalNm, string(configs(c).FolderName), ...
            darkADC, rawPeakPixel, localPeakPixel, centroidPixel, peakExcessIntensity, ...
            fwhmPixel, leftHalfPixel, rightHalfPixel}]; %#ok<AGROW>

        curves(end + 1) = struct('File', string(files(k).name), ...
            'NominalWavelength_nm', nominalNm, 'Pixel', xLocal, 'PeakExcess', peakExcess, ...
            'CentroidPixel', centroidPixel, 'SearchRange', configs(c).SearchRange); %#ok<SAGROW>
    end
end

writetable(fileSummary, fullfile(outputDir, '405_780_逐文件峰位检测摘要.csv'), 'Encoding', 'UTF-8');

nominalList = unique(fileSummary.NominalWavelength_nm);
anchorSummary = table('Size', [0 8], ...
    'VariableTypes', {'double','double','double','double','double','double','double','double'}, ...
    'VariableNames', {'NominalWavelength_nm','MeanCentroidPixel','StdCentroidPixel', ...
    'MeanPeakPixel','StdPeakPixel','MeanFWHM_pixel','FileCount','MeanPeakExcessIntensity'});

for i = 1:numel(nominalList)
    nm = nominalList(i);
    idx = fileSummary.NominalWavelength_nm == nm;
    anchorSummary = [anchorSummary; {nm, mean(fileSummary.CentroidPixel(idx), 'omitnan'), ...
        std(fileSummary.CentroidPixel(idx), 'omitnan'), mean(fileSummary.LocalPeakPixel(idx), 'omitnan'), ...
        std(fileSummary.LocalPeakPixel(idx), 'omitnan'), mean(fileSummary.FWHM_pixel(idx), 'omitnan'), ...
        sum(idx), mean(fileSummary.PeakExcessIntensity(idx), 'omitnan')}]; %#ok<AGROW>
end

anchorSummary = sortrows(anchorSummary, 'NominalWavelength_nm');
writetable(anchorSummary, fullfile(outputDir, '405_780_波长锚点汇总.csv'), 'Encoding', 'UTF-8');

fitCoeff = polyfit(anchorSummary.MeanCentroidPixel, anchorSummary.NominalWavelength_nm, 1);
slopeNmPerPixel = fitCoeff(1);
interceptNm = fitCoeff(2);
predictedNm = polyval(fitCoeff, anchorSummary.MeanCentroidPixel);
residualNm = anchorSummary.NominalWavelength_nm - predictedNm;
rmseNm = sqrt(mean(residualNm.^2, 'omitnan'));

calibration = table(slopeNmPerPixel, interceptNm, abs(slopeNmPerPixel), rmseNm, height(anchorSummary), ...
    min(anchorSummary.NominalWavelength_nm), max(anchorSummary.NominalWavelength_nm), ...
    'VariableNames', {'Slope_nm_per_pixel','Intercept_nm','NmPerPixel_abs','AnchorRMSE_nm', ...
    'AnchorCount','ValidMin_nm','ValidMax_nm'});
writetable(calibration, fullfile(outputDir, '405_780_线性波长标定公式.csv'), 'Encoding', 'UTF-8');

anchorFitTable = anchorSummary;
anchorFitTable.PredictedWavelength_nm = predictedNm;
anchorFitTable.Residual_nm = residualNm;
writetable(anchorFitTable, fullfile(outputDir, '405_780_线性标定残差.csv'), 'Encoding', 'UTF-8');

plotPeakOverlays(curves, outputDir, cnFont);
plotCalibration(anchorFitTable, slopeNmPerPixel, interceptNm, outputDir, cnFont);
writeReport(outputDir, calibration, anchorFitTable);

fprintf('Done.\n');
fprintf('Linear calibration: wavelength_nm = %.8f * pixel + %.4f\n', slopeNmPerPixel, interceptNm);
fprintf('Output directory: %s\n', outputDir);

function plotPeakOverlays(curves, outputDir, cnFont)
    nominalList = unique([curves.NominalWavelength_nm]);
    for i = 1:numel(nominalList)
        nm = nominalList(i);
        idx = [curves.NominalWavelength_nm] == nm;
        these = curves(idx);

        fig = figure('Color', 'w', 'Position', [80 80 1100 620]);
        t = tiledlayout(fig, 1, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
        ax = nexttile(t);
        hold(ax, 'on');
        grid(ax, 'on');
        box(ax, 'on');

        colors = lines(numel(these));
        centroidValues = zeros(numel(these), 1);
        for k = 1:numel(these)
            y = these(k).PeakExcess ./ max(these(k).PeakExcess, [], 'omitnan');
            plot(ax, these(k).Pixel, y, 'LineWidth', 1.2, 'Color', colors(k, :), ...
                'DisplayName', erase(these(k).File, '.txt'));
            centroidValues(k) = these(k).CentroidPixel;
        end
        meanCentroid = mean(centroidValues, 'omitnan');
        xline(ax, meanCentroid, 'k-', 'LineWidth', 2.0, ...
            'DisplayName', sprintf('平均质心 %.1f px', meanCentroid));

        xlabel(ax, 'TCD1304像素位置', 'FontName', cnFont, 'Interpreter', 'none');
        ylabel(ax, '局部背景扣除后的归一化响应', 'FontName', cnFont, 'Interpreter', 'none');
        title(t, sprintf('%.0fnm标定光源峰位检测', nm), 'FontName', cnFont, 'Interpreter', 'none');
        subtitle(t, '仅在局部搜索窗口内扣除慢变化背景后计算峰位质心。', ...
            'FontName', cnFont, 'Interpreter', 'none');
        legend(ax, 'Location', 'eastoutside', 'FontName', cnFont, 'Interpreter', 'none');
        set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);
        xlim(ax, these(1).SearchRange);
        ylim(ax, [-0.03 1.08]);

        exportgraphics(fig, fullfile(outputDir, sprintf('%.0fnm_峰位检测.png', nm)), 'Resolution', 300);
        savefig(fig, fullfile(outputDir, sprintf('%.0fnm_峰位检测.fig', nm)));
        close(fig);
    end
end

function plotCalibration(anchorFitTable, slopeNmPerPixel, interceptNm, outputDir, cnFont)
    fig = figure('Color', 'w', 'Position', [100 100 980 620]);
    ax = axes(fig);
    hold(ax, 'on');
    grid(ax, 'on');
    box(ax, 'on');

    px = anchorFitTable.MeanCentroidPixel;
    nm = anchorFitTable.NominalWavelength_nm;
    scatter(ax, px, nm, 90, 'filled', 'MarkerFaceColor', [0.20 0.45 0.85], ...
        'DisplayName', '标定锚点');

    pxFit = linspace(min(px) - 80, max(px) + 80, 200);
    nmFit = polyval([slopeNmPerPixel, interceptNm], pxFit);
    plot(ax, pxFit, nmFit, 'k-', 'LineWidth', 1.8, 'DisplayName', '线性拟合');

    for i = 1:height(anchorFitTable)
        text(ax, px(i), nm(i), sprintf('  %.0fnm', nm(i)), ...
            'FontName', cnFont, 'FontSize', 10, 'Interpreter', 'none');
    end

    xlabel(ax, 'TCD1304像素位置', 'FontName', cnFont, 'Interpreter', 'none');
    ylabel(ax, '标定波长 / nm', 'FontName', cnFont, 'Interpreter', 'none');
    title(ax, '405/780nm双点线性波长标定', 'FontName', cnFont, 'Interpreter', 'none');
    subtitle(ax, sprintf('近似公式：λ = %.6f × Pixel + %.2f', slopeNmPerPixel, interceptNm), ...
        'FontName', cnFont, 'Interpreter', 'none');
    legend(ax, 'Location', 'best', 'FontName', cnFont, 'Interpreter', 'none');
    set(ax, 'FontName', cnFont, 'FontSize', 11, 'LineWidth', 1);

    exportgraphics(fig, fullfile(outputDir, '405_780_线性波长标定图.png'), 'Resolution', 300);
    savefig(fig, fullfile(outputDir, '405_780_线性波长标定图.fig'));
    close(fig);
end

function writeReport(outputDir, calibration, anchorFitTable)
    reportPath = fullfile(outputDir, '405_780_波长标定报告.txt');
    fid = fopen(reportPath, 'w', 'n', 'UTF-8');
    cleaner = onCleanup(@() fclose(fid)); %#ok<NASGU>

    fprintf(fid, '405/780nm线性波长标定报告\n');
    fprintf(fid, '========================\n\n');
    fprintf(fid, '线性公式：\n');
    fprintf(fid, 'lambda_nm = %.8f * Pixel + %.4f\n\n', ...
        calibration.Slope_nm_per_pixel(1), calibration.Intercept_nm(1));
    fprintf(fid, '等效色散：%.4f nm/pixel\n', calibration.NmPerPixel_abs(1));
    fprintf(fid, '有效标定范围：%.0f-%.0f nm\n\n', calibration.ValidMin_nm(1), calibration.ValidMax_nm(1));

    fprintf(fid, '锚点汇总：\n');
    for i = 1:height(anchorFitTable)
        fprintf(fid, '- %.0fnm: 平均像素 %.2f px, 残差 %.3f nm, 文件数 %d\n', ...
            anchorFitTable.NominalWavelength_nm(i), anchorFitTable.MeanCentroidPixel(i), ...
            anchorFitTable.Residual_nm(i), anchorFitTable.FileCount(i));
    end

    fprintf(fid, '\n判断：\n');
    fprintf(fid, '405nm与780nm峰位明显分离，可以支持初步的像素-波长线性标定。\n');
    fprintf(fid, '但当前只有可见近紫端和近红外端少数锚点，且自制C-T结构可能存在非线性色散，');
    fprintf(fid, '因此该标定应表述为“近似线性波长标定”，不宜称为标准定量光谱标定。\n');
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
