-- Self-contained dial input control: a gauge-style panel (face, dashed track, value arc, ticks,
-- needle, hub) whose value is set by pointing at an angle around the center. Owns its panel,
-- shape surface, and interaction; pressing outside the face drags the panel unless movePanel is
-- false. Angles follow MathUtil's gauge convention (value 0 at startAngle, sweeping clockwise).
local MathUtil = require("@src/API/MathUtil")
local ShapeSurfaceUtil = require("@src/API/UI/ShapeSurfaceUtil")
local SurfaceInteraction = require("@src/API/UI/SurfaceInteraction")

export type Options = {
    value : number?,            -- initial value 0..1, default 0
    onChanged : ((value : number) -> ())?,
    startAngle : number?,       -- default math.rad(225): classic 270-degree gauge
    sweepAngle : number?,       -- default math.rad(270)
    tickCount : number?,        -- default 5 (0 disables)
    template : string?,         -- panel template name
    movePanel : boolean?,

    faceColor : vec?, faceAlpha : number?,
    trackColor : vec?,
    fillColor : vec?,
    needleColor : vec?,
    tickColor : vec?,
}

export type Dial = {
    root : Panel,
    GetValue : (self : Dial) -> number,
    SetValue : (self : Dial, value : number) -> (),
}

export type DialAPI = {
    -- name keys the underlying shape surface, so it must be unique per dial (and stable across
    -- script reloads)
    New : (parent : Canvas | Widget, pos : vec, size : number, layer : integer, name : string, options : Options?) -> Dial,
}

local dialAPI : DialAPI = { }

function dialAPI.New(parent : Canvas | Widget, pos : vec, size : number, layer : integer, name : string, options : Options?) : Dial
    local opts : Options = options or { };
    local startAngle = opts.startAngle or math.rad(225.0);
    local sweepAngle = opts.sweepAngle or math.rad(270.0);
    local value = math.clamp(opts.value or 0.0, 0.0, 1.0);

    -- Proportions relative to the control size
    local center = vec.New(size * 0.5, size * 0.5);
    local faceRadius = size * 0.44;
    local dialRadius = size * 0.34;
    local trackWidth = size * 0.05;
    local fillColor = opts.fillColor or vec.New(0.95, 0.6, 0.2);

    local surface = UI.GetShapeSurface(name, vec.New(size, size));
    local panel = (parent :: Widget):NewPanel(pos, vec.New(size, size), layer, opts.template);
    panel:SetBackground(surface);

    local function Redraw()
        surface:Clear();
        surface:DrawCircle(center, faceRadius, opts.faceColor or vec.New(0.1, 0.1, 0.13), opts.faceAlpha or 0.85);
        surface:DrawArc(center, dialRadius, startAngle - sweepAngle, startAngle, trackWidth, opts.trackColor or vec.New(0.3, 0.3, 0.36), 1.0, 5.0, 4.0);
        if value > 0.001 then -- a zero-span arc would leave a faint stub at the start angle
            surface:DrawArc(center, dialRadius, MathUtil.ValueToAngle(value, startAngle, sweepAngle), startAngle, trackWidth, fillColor);
        end

        -- Butt caps keep the ticks and needle at their exact lengths
        local tickCount = opts.tickCount or 5;
        for tick = 0, tickCount - 1 do
            local direction = MathUtil.FromAngle(MathUtil.ValueToAngle(tick / math.max(tickCount - 1, 1), startAngle, sweepAngle));
            ShapeSurfaceUtil.DrawLine(surface, center + direction * (size * 0.275), center + direction * (size * 0.3), { width = 2.0, color = opts.tickColor or vec.New(0.6, 0.6, 0.65), capStyle = 1 });
        end

        ShapeSurfaceUtil.DrawLine(surface, center, center + MathUtil.FromAngle(MathUtil.ValueToAngle(value, startAngle, sweepAngle), size * 0.25), { width = 3.0, color = opts.needleColor or vec.New(0.95, 0.95, 1.0), capStyle = 1 });
        surface:DrawCircle(center, size * 0.044, fillColor);
    end

    local dial = { root = panel };

    function dial:GetValue() : number
        return value;
    end

    function dial:SetValue(newValue : number)
        newValue = math.clamp(newValue, 0.0, 1.0);
        if newValue == value then return; end
        value = newValue;
        Redraw();
    end

    local function ApplyMouseAngle(surfacePos : vec)
        local mouseAngle = math.atan2(surfacePos.y - center.y, surfacePos.x - center.x);
        local newValue = MathUtil.AngleToValue(mouseAngle, startAngle, sweepAngle);
        if newValue == value then return; end
        value = newValue;
        Redraw();
        if opts.onChanged ~= nil then opts.onChanged(value); end
    end

    SurfaceInteraction.Attach(panel, surface,
    {
        movePanel = opts.movePanel,

        hitTest = function(surfacePos, mouseButton)
            if mouseButton ~= 0i then return nil; end
            if ShapeSurfaceUtil.IsPointInCircle(surfacePos, center, faceRadius) then
                return true;
            end
            return nil;
        end,

        onDragStart = function(_, surfacePos) ApplyMouseAngle(surfacePos); end,
        onDrag = function(_, surfacePos) ApplyMouseAngle(surfacePos); end,
    });

    Redraw();
    return dial;
end

return dialAPI;
