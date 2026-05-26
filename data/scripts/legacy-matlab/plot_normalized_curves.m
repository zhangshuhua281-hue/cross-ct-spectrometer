clear; clc;

rootDir = fileparts(mfilename('fullpath'));
outputDir = fullfile(rootDir, 'normalized_plots_skip20');
yColumn = 'ADC';  % Change to 'VoltageV' if you want to normalize voltage.
skipFirstRows = 20;

if ~isfolder(outputDir)
    mkdir(outputDir);
end

files = dir(fullfile(rootDir, '**', '*.txt'));
summary = table('Size', [0 6], ...
    'VariableTypes', {'string', 'string', 'double', 'double', 'double', 'double'}, ...
    'VariableNames', {'File', 'YColumn', 'SkippedRows', 'MaxValue', 'MinNormalized', 'MaxNormalized'});

for k = 1:numel(files)
    filePath = fullfile(files(k).folder, files(k).name);

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
    maxValue = max(y, [], 'omitnan');
    if isempty(maxValue) || isnan(maxValue) || maxValue == 0
        warning('Skip %s: max value is invalid for normalization.', filePath);
        continue;
    end

    yNorm = y ./ maxValue;

    relFolder = erase(string(files(k).folder), string(rootDir));
    relFolder = strip(relFolder, 'left', filesep);
    outSubDir = fullfile(outputDir, char(relFolder));
    if ~isfolder(outSubDir)
        mkdir(outSubDir);
    end

    [~, baseName] = fileparts(files(k).name);
    fig = figure('Visible', 'off', 'Color', 'w');
    plot(x, yNorm, 'LineWidth', 1.2);
    grid on;
    ylim([0 1]);
    xlabel('Pixel');
    ylabel("Normalized " + yColumn);
    title(baseName + " (after skipping first " + string(skipFirstRows) + ...
        " rows, " + yColumn + " max = " + string(maxValue) + ")", ...
        'Interpreter', 'none');

    pngPath = fullfile(outSubDir, baseName + "_skip20_normalized.png");
    figPath = fullfile(outSubDir, baseName + "_skip20_normalized.fig");
    exportgraphics(fig, pngPath, 'Resolution', 300);
    savefig(fig, figPath);
    close(fig);

    summary = [summary; {string(filePath), string(yColumn), skipFirstRows, maxValue, ...
        min(yNorm, [], 'omitnan'), max(yNorm, [], 'omitnan')}]; %#ok<AGROW>
end

writetable(summary, fullfile(outputDir, 'normalization_summary.csv'));
fprintf('Done. Saved %d normalized plots to:\n%s\n', height(summary), outputDir);
