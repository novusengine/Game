-- Rolling line chart drawn into a region of a ShapeSurface: sample ring buffer, auto-scaling Y
-- with a floor, framed plot area, horizontal guides, an optional dashed threshold line, and a
-- gradient-capable series stroke. The chart only draws — the caller owns the surface's
-- Clear/redraw cadence, so charts compose with other content on the same surface.
local ShapeSurfaceUtil = require("@src/API/UI/ShapeSurfaceUtil")

export type Options = {
    maxSamples : number?,       -- ring buffer length, default 120
    floorValue : number?,       -- minimum top-of-scale so a quiet series doesn't zoom into noise, default 1
    padding : number?,          -- inner padding of the plot area, default 4
    cornerRadius : number?,     -- default 4
    background : vec?, backgroundAlpha : number?,
    frameColor : vec?, frameAlpha : number?,
    guideCount : number?,       -- evenly spaced horizontal guide lines, default 3 (0 disables)
    guideColor : vec?, guideAlpha : number?,
    threshold : number?,        -- dashed horizontal reference line at this value
    thresholdColor : vec?, thresholdAlpha : number?,
    series : ShapeSurfaceUtil.StrokeStyle?,
}

export type LineChart = {
    PushSample : (self : LineChart, value : number) -> (),
    SetSamples : (self : LineChart, values : { number }) -> (),
    -- Returns (current, average, max, scaleMax); max is the true worst sample, scaleMax is the
    -- plot's top-of-scale (max clamped up to floorValue)
    GetStats : (self : LineChart) -> (number, number, number, number),
    Draw : (self : LineChart) -> (),
}

export type LineChartAPI = {
    New : (surface : ShapeSurface, plotMin : vec, plotMax : vec, options : Options?) -> LineChart,
}

local lineChartAPI : LineChartAPI = { }

function lineChartAPI.New(surface : ShapeSurface, plotMin : vec, plotMax : vec, options : Options?) : LineChart
    local opts : Options = options or { };
    local maxSamples = opts.maxSamples or 120;
    local floorValue = opts.floorValue or 1.0;
    local padding = opts.padding or 4.0;
    local samples = {};

    local chart = {};

    function chart:PushSample(value : number)
        table.insert(samples, value);
        if #samples > maxSamples then
            table.remove(samples, 1);
        end
    end

    function chart:SetSamples(values : { number })
        samples = table.clone(values);
        while #samples > maxSamples do
            table.remove(samples, 1);
        end
    end

    function chart:GetStats() : (number, number, number, number)
        local sum = 0.0;
        local maxValue = 0.0;
        for _, sample in samples do
            sum += sample;
            maxValue = math.max(maxValue, sample);
        end
        local average = if #samples > 0 then sum / #samples else 0.0;
        return samples[#samples] or 0.0, average, maxValue, math.max(maxValue, floorValue);
    end

    function chart:Draw()
        local _, _, _, scaleMax = chart:GetStats();
        local innerMin = plotMin + vec.New(padding, padding);
        local innerSize = plotMax - plotMin - vec.New(padding, padding) * 2.0;

        surface:DrawRect(plotMin, plotMax, opts.cornerRadius or 4.0, opts.background or vec.New(0.05, 0.05, 0.07), opts.backgroundAlpha or 0.9);

        -- Butt caps keep the guides flush with the plot edges
        local guideCount = opts.guideCount or 3;
        for guide = 1, guideCount do
            local y = plotMin.y + (plotMax.y - plotMin.y) * guide / (guideCount + 1);
            ShapeSurfaceUtil.DrawLine(surface, vec.New(innerMin.x, y), vec.New(innerMin.x + innerSize.x, y), { width = 1.0, color = opts.guideColor or vec.New(0.25, 0.25, 0.3), alpha = opts.guideAlpha or 0.6, capStyle = 1 });
        end

        if opts.threshold ~= nil then
            local y = innerMin.y + math.clamp(opts.threshold / scaleMax, 0.0, 1.0) * innerSize.y;
            ShapeSurfaceUtil.DrawLine(surface, vec.New(innerMin.x, y), vec.New(innerMin.x + innerSize.x, y), { width = 1.0, color = opts.thresholdColor or vec.New(0.9, 0.8, 0.3), alpha = opts.thresholdAlpha or 0.7, dashLength = 6.0, gapLength = 4.0, capStyle = 1 });
        end

        if #samples >= 2 then
            local linePoints = {};
            for index, sample in samples do
                local x = innerMin.x + (index - 1) / (maxSamples - 1) * innerSize.x;
                local y = innerMin.y + (sample / scaleMax) * innerSize.y;
                table.insert(linePoints, vec.New(x, y));
            end
            ShapeSurfaceUtil.DrawPolyline(surface, linePoints, opts.series or { width = 1.5, color = vec.New(0.3, 0.85, 0.5) });
        end

        surface:DrawRectOutline(plotMin, plotMax, opts.cornerRadius or 4.0, 1.5, opts.frameColor or vec.New(0.35, 0.35, 0.42), opts.frameAlpha);
    end

    return chart;
end

return lineChartAPI;
