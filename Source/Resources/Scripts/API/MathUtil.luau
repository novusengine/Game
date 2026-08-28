-- Scalar and angle math for scripts: interpolation, remapping, frame-rate-independent smoothing,
-- angle wrapping, and gauge (value <-> angle) mapping. Lives Luau-side because this is tuning and
-- glue math for UI/gameplay scripts, not engine-coupled or performance-critical work.
export type MathUtilAPI =
{
    Lerp : (a : number, b : number, t : number) -> number,
    InverseLerp : (a : number, b : number, value : number) -> number,
    Remap : (value : number, fromMin : number, fromMax : number, toMin : number, toMax : number) -> number,
    MoveToward : (current : number, target : number, maxDelta : number) -> number,
    Damp : (current : number, target : number, smoothing : number, deltaTime : number) -> number,
    SnapToStep : (value : number, step : number) -> number,

    SmoothStep : (t : number) -> number,
    EaseInOutQuad : (t : number) -> number,
    EaseOutCubic : (t : number) -> number,

    Wrap : (value : number, min : number, max : number) -> number,
    PingPong : (t : number, length : number) -> number,
    Approximately : (a : number, b : number, epsilon : number?) -> boolean,
    RemapClamped : (value : number, fromMin : number, fromMax : number, toMin : number, toMax : number) -> number,

    WrapAngle : (angle : number) -> number,
    AngleDifference : (from : number, to : number) -> number,
    LerpAngle : (from : number, to : number, t : number) -> number,
    MoveTowardAngle : (current : number, target : number, maxDelta : number) -> number,

    -- 2D vec helpers for what the native `vec` library (magnitude/normalize/dot/clamp/lerp/...)
    -- does not cover
    FromAngle : (angle : number, length : number?) -> vec,
    Rotate : (v : vec, angle : number) -> vec,
    Perp : (v : vec) -> vec,
    DampVec : (current : vec, target : vec, smoothing : number, deltaTime : number) -> vec,

    ValueToAngle : (value : number, startAngle : number, sweepAngle : number) -> number,
    AngleToValue : (angle : number, startAngle : number, sweepAngle : number) -> number,
}

local mathUtilAPI : MathUtilAPI = { }

local TWO_PI = math.pi * 2.0;

function mathUtilAPI.Lerp(a : number, b : number, t : number) : number
    return a + (b - a) * t;
end

-- Where value sits between a and b as 0..1 (unclamped; 0 when a == b)
function mathUtilAPI.InverseLerp(a : number, b : number, value : number) : number
    if a == b then return 0.0; end
    return (value - a) / (b - a);
end

function mathUtilAPI.Remap(value : number, fromMin : number, fromMax : number, toMin : number, toMax : number) : number
    return mathUtilAPI.Lerp(toMin, toMax, mathUtilAPI.InverseLerp(fromMin, fromMax, value));
end

function mathUtilAPI.RemapClamped(value : number, fromMin : number, fromMax : number, toMin : number, toMax : number) : number
    return mathUtilAPI.Lerp(toMin, toMax, math.clamp(mathUtilAPI.InverseLerp(fromMin, fromMax, value), 0.0, 1.0));
end

-- Float comparison with tolerance; epsilon defaults to 1e-4 (plenty for pixel-scale UI math)
function mathUtilAPI.Approximately(a : number, b : number, epsilon : number?) : boolean
    return math.abs(a - b) <= (epsilon or 1e-4);
end

-- Steps current toward target by at most maxDelta without overshooting
function mathUtilAPI.MoveToward(current : number, target : number, maxDelta : number) : number
    local delta = target - current;
    if math.abs(delta) <= maxDelta then return target; end
    return current + math.sign(delta) * maxDelta;
end

-- Frame-rate-independent exponential smoothing; smoothing is the fraction of the remaining
-- distance covered per second (e.g. 10 = fast follow, 2 = lazy follow)
function mathUtilAPI.Damp(current : number, target : number, smoothing : number, deltaTime : number) : number
    return mathUtilAPI.Lerp(current, target, 1.0 - math.exp(-smoothing * deltaTime));
end

function mathUtilAPI.SnapToStep(value : number, step : number) : number
    if step <= 0.0 then return value; end
    return math.round(value / step) * step;
end

function mathUtilAPI.SmoothStep(t : number) : number
    t = math.clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
end

function mathUtilAPI.EaseInOutQuad(t : number) : number
    t = math.clamp(t, 0.0, 1.0);
    return if t < 0.5 then 2.0 * t * t else 1.0 - (-2.0 * t + 2.0) ^ 2 / 2.0;
end

function mathUtilAPI.EaseOutCubic(t : number) : number
    t = math.clamp(t, 0.0, 1.0);
    return 1.0 - (1.0 - t) ^ 3;
end

-- Wraps value into [min, max)
function mathUtilAPI.Wrap(value : number, min : number, max : number) : number
    local range = max - min;
    if range <= 0.0 then return min; end
    local wrapped = math.fmod(value - min, range);
    if wrapped < 0.0 then wrapped += range; end
    return min + wrapped;
end

-- Oscillates t between 0 and length (0 -> length -> 0 -> ...)
function mathUtilAPI.PingPong(t : number, length : number) : number
    local wrapped = mathUtilAPI.Wrap(t, 0.0, length * 2.0);
    return length - math.abs(wrapped - length);
end

-- Wraps an angle in radians to [-pi, pi]
function mathUtilAPI.WrapAngle(angle : number) : number
    local wrapped = math.fmod(angle + math.pi, TWO_PI);
    if wrapped < 0.0 then wrapped += TWO_PI; end
    return wrapped - math.pi;
end

-- Shortest signed difference from one angle to another, in [-pi, pi]
function mathUtilAPI.AngleDifference(from : number, to : number) : number
    return mathUtilAPI.WrapAngle(to - from);
end

function mathUtilAPI.LerpAngle(from : number, to : number, t : number) : number
    return from + mathUtilAPI.AngleDifference(from, to) * t;
end

-- MoveToward along the shortest angular direction; for constant-speed needle/rotation animation
function mathUtilAPI.MoveTowardAngle(current : number, target : number, maxDelta : number) : number
    local difference = mathUtilAPI.AngleDifference(current, target);
    if math.abs(difference) <= maxDelta then
        return target;
    end
    return current + math.sign(difference) * maxDelta;
end

-- Unit direction (CCW from +X) for an angle, optionally scaled
function mathUtilAPI.FromAngle(angle : number, length : number?) : vec
    return vec.New(math.cos(angle), math.sin(angle)) * (length or 1.0);
end

function mathUtilAPI.Rotate(v : vec, angle : number) : vec
    local cos = math.cos(angle);
    local sin = math.sin(angle);
    return vec.New(v.x * cos - v.y * sin, v.x * sin + v.y * cos, v.z);
end

-- 90 degrees counter-clockwise
function mathUtilAPI.Perp(v : vec) : vec
    return vec.New(-v.y, v.x, v.z);
end

function mathUtilAPI.DampVec(current : vec, target : vec, smoothing : number, deltaTime : number) : vec
    return vec.lerp(current, target, 1.0 - math.exp(-smoothing * deltaTime));
end

-- Gauge mapping: value 0 sits at startAngle and sweeps CLOCKWISE by sweepAngle as the value grows
-- (radians, angles CCW from +X like the ShapeSurface draw calls). E.g. a classic 270-degree dial
-- is startAngle = math.rad(225), sweepAngle = math.rad(270).
function mathUtilAPI.ValueToAngle(value : number, startAngle : number, sweepAngle : number) : number
    return startAngle - math.clamp(value, 0.0, 1.0) * sweepAngle;
end

function mathUtilAPI.AngleToValue(angle : number, startAngle : number, sweepAngle : number) : number
    local rel = math.fmod(startAngle - angle, TWO_PI);
    if rel < 0.0 then rel += TWO_PI; end

    if rel > sweepAngle then
        -- In the dead zone outside the sweep: snap to whichever end is closer
        return if rel - sweepAngle > (TWO_PI - sweepAngle) * 0.5 then 0.0 else 1.0;
    end
    return rel / sweepAngle;
end

return mathUtilAPI;
