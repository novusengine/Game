-- Radial progress indicator drawn into a ShapeSurface: a track ring with a clockwise-from-top
-- sweep arc, optional gradient along the sweep, and an optional inner fill that brightens with
-- progress. Draw-only, like LineChart — the caller owns the surface's Clear/redraw cadence.
-- Cooldown spinners, cast bars, budget gauges.
export type Options = {
    trackWidth : number?,       -- ring thickness, default radius * 0.18
    trackColor : vec?, trackAlpha : number?,
    fillColor : vec?, fillAlpha : number?,
    fillEndColor : vec?,        -- angular gradient along the sweep (overridable per Draw call)
    background : vec?, backgroundAlpha : number?, -- disc behind the ring; nil = none
    innerFill : boolean?,       -- fill the ring's center, brightening with progress
}

export type RadialProgress = {
    Draw : (self : RadialProgress, progress : number, fillEndOverride : vec?) -> (),
}

export type RadialProgressAPI = {
    New : (surface : ShapeSurface, center : vec, radius : number, options : Options?) -> RadialProgress,
}

local radialProgressAPI : RadialProgressAPI = { }

local TOP = math.pi * 0.5;

function radialProgressAPI.New(surface : ShapeSurface, center : vec, radius : number, options : Options?) : RadialProgress
    local opts : Options = options or { };
    local trackWidth = opts.trackWidth or radius * 0.18;
    local fillColor = opts.fillColor or vec.New(0.3, 0.75, 0.95);

    local indicator = {};

    function indicator:Draw(progress : number, fillEndOverride : vec?)
        progress = math.clamp(progress, 0.0, 1.0);

        if opts.background ~= nil then
            surface:DrawCircle(center, radius + trackWidth, opts.background, opts.backgroundAlpha or 0.85);
        end

        surface:DrawCircleOutline(center, radius, trackWidth, opts.trackColor or vec.New(0.22, 0.22, 0.27), opts.trackAlpha);

        if progress > 0.005 then
            surface:DrawArc(center, radius, TOP - progress * math.pi * 2.0, TOP, trackWidth, fillColor, opts.fillAlpha, nil, nil, nil, fillEndOverride or opts.fillEndColor);
        end

        if opts.innerFill then
            surface:DrawCircle(center, radius - trackWidth * 2.0, fillColor, 0.15 + progress * 0.25);
        end
    end

    return indicator;
end

return radialProgressAPI;
