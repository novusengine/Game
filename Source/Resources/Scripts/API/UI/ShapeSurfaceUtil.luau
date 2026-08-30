-- Helpers for drawing into and interacting with ShapeSurface-backed panels: coordinate
-- conversion, geometry/hit-test math, and composite draws (polylines, wires, arrows, reveals)
-- built from the native ShapeSurface calls. Draw helpers take a StrokeStyle table instead of the
-- native positional stroke arguments.
local MathUtil = require("@src/API/MathUtil")

export type StrokeStyle =
{
    width : number,
    color : vec,
    alpha : number?,
    dashLength : number?,
    gapLength : number?,
    dashOffset : number?,
    endColor : vec?,   -- blends the stroke from color to endColor along its length
    endAlpha : number?,
    capStyle : number?, -- 0 = round (default), 1 = butt, 2 = square; single lines only
}

export type ShapeSurfaceUtilAPI =
{
    ScreenToSurface : (panel : Panel, surface : ShapeSurface, screenPos : vec) -> vec,
    GetSurfaceScale : (panel : Panel, surface : ShapeSurface) -> vec,

    IsPointInCircle : (point : vec, center : vec, radius : number) -> boolean,
    IsPointInRect : (point : vec, min : vec, max : vec) -> boolean,
    PointOnSegment : (a : vec, b : vec, t : number) -> vec,
    ClosestPointOnSegment : (point : vec, a : vec, b : vec) -> vec,
    ClosestPointOnPath : (point : vec, points : { vec }) -> (vec, number, number),
    PathLength : (points : { vec }) -> number,
    PointOnPath : (points : { vec }, distance : number) -> vec,
    DirectionOnPath : (points : { vec }, distance : number) -> vec,
    ExpandRect : (min : vec, max : vec, amount : number) -> (vec, vec),
    PolygonArea : (points : { vec }) -> number,
    PolygonCentroid : (points : { vec }) -> vec,
    BezierTangent : (p0 : vec, c0 : vec, c1 : vec, p1 : vec, t : number) -> vec,
    CubicPoint : (p0 : vec, c0 : vec, c1 : vec, p1 : vec, t : number) -> vec,
    BezierPoints : (p0 : vec, c0 : vec, c1 : vec, p1 : vec, count : number?) -> { vec },
    DistanceToSegment : (point : vec, a : vec, b : vec) -> number,
    DistanceToPath : (point : vec, points : { vec }) -> number,
    DistanceToWire : (point : vec, from : vec, to : vec) -> number,
    IsPointInPolygon : (point : vec, points : { vec }) -> boolean,
    IsPointInPolygonWithHoles : (point : vec, points : { vec }, holes : { { vec } }) -> boolean,
    IsPointInRotatedRect : (point : vec, min : vec, max : vec, rotation : number, cornerRadius : number?) -> boolean,
    IsPointInEllipse : (point : vec, center : vec, radii : vec, rotation : number?) -> boolean,
    IsPointNearEllipse : (point : vec, center : vec, radii : vec, tolerance : number, rotation : number?) -> boolean,
    IsPointInRoundedRect : (point : vec, min : vec, max : vec, cornerRadius : number) -> boolean,
    IsPointInArcSegment : (point : vec, center : vec, innerRadius : number, outerRadius : number, startAngle : number, endAngle : number) -> boolean,
    IsPointNearPath : (point : vec, points : { vec }, tolerance : number) -> boolean,
    IsPointNearWire : (point : vec, from : vec, to : vec, tolerance : number) -> boolean,
    RectsOverlap : (minA : vec, maxA : vec, minB : vec, maxB : vec) -> boolean,
    SegmentsIntersect : (a1 : vec, a2 : vec, b1 : vec, b2 : vec) -> boolean,
    PolygonsOverlap : (pointsA : { vec }, pointsB : { vec }) -> boolean,

    DrawLine : (surface : ShapeSurface, from : vec, to : vec, style : StrokeStyle) -> (),
    DrawPolyline : (surface : ShapeSurface, points : { vec }, style : StrokeStyle) -> (),
    DrawWire : (surface : ShapeSurface, from : vec, to : vec, style : StrokeStyle) -> (),
    DrawArrow : (surface : ShapeSurface, from : vec, to : vec, style : StrokeStyle, headSize : number?) -> (),
    DrawReveal : (surface : ShapeSurface, points : { vec }, revealedLength : number, style : StrokeStyle) -> vec,
    DrawHandle : (surface : ShapeSurface, pos : vec, size : number, fillColor : vec, alpha : number?) -> (),
    DrawCross : (surface : ShapeSurface, pos : vec, size : number, style : StrokeStyle) -> (),
    DrawPieOutline : (surface : ShapeSurface, center : vec, radius : number, startAngle : number, endAngle : number, style : StrokeStyle) -> (),
}

local shapeSurfaceUtilAPI : ShapeSurfaceUtilAPI = { }

-- Wires share their control-point construction between drawing and hit-testing
local function GetWireControls(from : vec, to : vec) : (vec, vec)
    local tangent = math.max(math.abs(to.x - from.x) * 0.5, 30.0);
    return vec.New(from.x + tangent, from.y), vec.New(to.x - tangent, to.y);
end

-- Per-axis factor that maps panel-local pixels to surface pixels (vec.New(1, 1) when they match)
function shapeSurfaceUtilAPI.GetSurfaceScale(panel : Panel, surface : ShapeSurface) : vec
    local panelSize = panel:GetSize();
    local surfaceSize = surface:GetSize();
    return vec.New(surfaceSize.x / panelSize.x, surfaceSize.y / panelSize.y);
end

function shapeSurfaceUtilAPI.ScreenToSurface(panel : Panel, surface : ShapeSurface, screenPos : vec) : vec
    -- Componentwise multiply; the scale's z is 0, matching the position's unused z
    return panel:ScreenToLocal(screenPos) * shapeSurfaceUtilAPI.GetSurfaceScale(panel, surface);
end

function shapeSurfaceUtilAPI.IsPointInCircle(point : vec, center : vec, radius : number) : boolean
    return vec.magnitude(point - center) <= radius;
end

function shapeSurfaceUtilAPI.IsPointInRect(point : vec, min : vec, max : vec) : boolean
    return vec.clamp(point, min, max) == point;
end

function shapeSurfaceUtilAPI.PointOnSegment(a : vec, b : vec, t : number) : vec
    return a + (b - a) * math.clamp(t, 0.0, 1.0);
end

function shapeSurfaceUtilAPI.ClosestPointOnSegment(point : vec, a : vec, b : vec) : vec
    local ab = b - a;
    local lengthSq = vec.dot(ab, ab);
    local t = if lengthSq > 0.0 then math.clamp(vec.dot(point - a, ab) / lengthSq, 0.0, 1.0) else 0.0;
    return a + ab * t;
end

-- Returns the closest point on the path, the distance ALONG the path to it, and the distance TO
-- it. E.g. snapping a marker onto a wire, or finding where to insert a waypoint
function shapeSurfaceUtilAPI.ClosestPointOnPath(point : vec, points : { vec }) : (vec, number, number)
    local bestPoint = points[1];
    local bestDistanceAlong = 0.0;
    local bestDistance = math.huge;

    local accumulated = 0.0;
    for index = 1, #points - 1 do
        local closest = shapeSurfaceUtilAPI.ClosestPointOnSegment(point, points[index], points[index + 1]);
        local distance = vec.magnitude(point - closest);
        if distance < bestDistance then
            bestDistance = distance;
            bestPoint = closest;
            bestDistanceAlong = accumulated + vec.magnitude(closest - points[index]);
        end
        accumulated += vec.magnitude(points[index + 1] - points[index]);
    end
    return bestPoint, bestDistanceAlong, bestDistance;
end

function shapeSurfaceUtilAPI.PathLength(points : { vec }) : number
    local total = 0.0;
    for index = 1, #points - 1 do
        total += vec.magnitude(points[index + 1] - points[index]);
    end
    return total;
end

-- Point `distance` pixels along the path from its first waypoint, clamped to the ends
function shapeSurfaceUtilAPI.PointOnPath(points : { vec }, distance : number) : vec
    if distance <= 0.0 then
        return points[1];
    end

    local remaining = distance;
    for index = 1, #points - 1 do
        local length = vec.magnitude(points[index + 1] - points[index]);
        if remaining <= length then
            return shapeSurfaceUtilAPI.PointOnSegment(points[index], points[index + 1], remaining / length);
        end
        remaining -= length;
    end
    return points[#points];
end

-- Normalized travel direction at `distance` pixels along the path; e.g. orienting arrowheads or
-- markers that move along a route
function shapeSurfaceUtilAPI.DirectionOnPath(points : { vec }, distance : number) : vec
    local remaining = math.max(distance, 0.0);
    for index = 1, #points - 1 do
        local segment = points[index + 1] - points[index];
        local length = vec.magnitude(segment);
        if remaining <= length or index == #points - 1 then
            return if length > 0.0 then segment / length else vec.New(1, 0);
        end
        remaining -= length;
    end
    return vec.New(1, 0);
end

-- Returns (min - amount, max + amount); the outset/inset pattern selection outlines use
function shapeSurfaceUtilAPI.ExpandRect(min : vec, max : vec, amount : number) : (vec, vec)
    local expand = vec.New(amount, amount);
    return min - expand, max + expand;
end

-- Shoelace area (convex or concave, no self-intersection)
function shapeSurfaceUtilAPI.PolygonArea(points : { vec }) : number
    local doubleArea = 0.0;
    for index = 1, #points do
        doubleArea += vec.cross(points[index], points[(index % #points) + 1]).z;
    end
    return math.abs(doubleArea) * 0.5;
end

-- Area-weighted centroid; e.g. placing a label or marker inside a zone polygon. Degenerate
-- (zero-area) polygons fall back to the vertex average
function shapeSurfaceUtilAPI.PolygonCentroid(points : { vec }) : vec
    local doubleArea = 0.0;
    local centroid = vec.New(0, 0);
    for index = 1, #points do
        local current = points[index];
        local nextPoint = points[(index % #points) + 1];
        local cross = vec.cross(current, nextPoint).z;
        doubleArea += cross;
        centroid += (current + nextPoint) * cross;
    end

    if math.abs(doubleArea) < 1e-4 then
        local average = vec.New(0, 0);
        for _, point in points do
            average += point;
        end
        return average / #points;
    end
    return centroid / (3.0 * doubleArea);
end

function shapeSurfaceUtilAPI.CubicPoint(p0 : vec, c0 : vec, c1 : vec, p1 : vec, t : number) : vec
    local u = 1.0 - t;
    return p0 * (u * u * u) + c0 * (3.0 * u * u * t) + c1 * (3.0 * u * t * t) + p1 * (t * t * t);
end

-- Normalized tangent (travel direction) of the cubic at t; pairs with CubicPoint for orienting
-- markers along curves
function shapeSurfaceUtilAPI.BezierTangent(p0 : vec, c0 : vec, c1 : vec, p1 : vec, t : number) : vec
    local u = 1.0 - t;
    local derivative = (c0 - p0) * (3.0 * u * u) + (c1 - c0) * (6.0 * u * t) + (p1 - c1) * (3.0 * t * t);
    local length = vec.magnitude(derivative);
    return if length > 0.0 then derivative / length else vec.New(1, 0);
end

-- Flattens a cubic bezier into count segments (count + 1 points), e.g. for DistanceToPath or
-- PointOnPath on curved shapes
function shapeSurfaceUtilAPI.BezierPoints(p0 : vec, c0 : vec, c1 : vec, p1 : vec, count : number?) : { vec }
    local numSegments = count or 16;
    local points = { p0 };
    for index = 1, numSegments do
        table.insert(points, shapeSurfaceUtilAPI.CubicPoint(p0, c0, c1, p1, index / numSegments));
    end
    return points;
end

function shapeSurfaceUtilAPI.DistanceToSegment(point : vec, a : vec, b : vec) : number
    return vec.magnitude(point - shapeSurfaceUtilAPI.ClosestPointOnSegment(point, a, b));
end

function shapeSurfaceUtilAPI.DistanceToPath(point : vec, points : { vec }) : number
    local best = math.huge;
    for index = 1, #points - 1 do
        best = math.min(best, shapeSurfaceUtilAPI.DistanceToSegment(point, points[index], points[index + 1]));
    end
    return best;
end

-- Distance to the bezier a DrawWire between the same endpoints would produce; use for
-- click-selecting connections in node editors
function shapeSurfaceUtilAPI.DistanceToWire(point : vec, from : vec, to : vec) : number
    local c0, c1 = GetWireControls(from, to);
    return shapeSurfaceUtilAPI.DistanceToPath(point, shapeSurfaceUtilAPI.BezierPoints(from, c0, c1, to));
end

-- Crossing test; works for convex and concave polygons without self-intersection
function shapeSurfaceUtilAPI.IsPointInPolygon(point : vec, points : { vec }) : boolean
    local inside = false;
    local previous = points[#points];
    for _, current in points do
        if (current.y > point.y) ~= (previous.y > point.y) then
            local intersectX = previous.x + (current.x - previous.x) * (point.y - current.y) / (previous.y - current.y);
            if point.x < intersectX then
                inside = not inside;
            end
        end
        previous = current;
    end
    return inside;
end

-- Hit test for pies, rings, arcs and donut slices: inside the radius band AND the angular span
-- (angles CCW from +X, matching the draw calls; innerRadius 0 = a pie wedge)
function shapeSurfaceUtilAPI.IsPointInArcSegment(point : vec, center : vec, innerRadius : number, outerRadius : number, startAngle : number, endAngle : number) : boolean
    local toPoint = point - center;
    local distance = vec.magnitude(toPoint);
    if distance < innerRadius or distance > outerRadius then
        return false;
    end

    local span = endAngle - startAngle;
    if span >= math.pi * 2.0 then
        return true;
    end

    local rel = math.fmod(math.atan2(toPoint.y, toPoint.x) - startAngle, math.pi * 2.0);
    if rel < 0.0 then rel += math.pi * 2.0; end
    return rel <= span;
end

function shapeSurfaceUtilAPI.IsPointInPolygonWithHoles(point : vec, points : { vec }, holes : { { vec } }) : boolean
    if not shapeSurfaceUtilAPI.IsPointInPolygon(point, points) then
        return false;
    end
    for _, hole in holes do
        if shapeSurfaceUtilAPI.IsPointInPolygon(point, hole) then
            return false;
        end
    end
    return true;
end

-- Matches the visual bounds of DrawRect with rotation: the point is transformed into the rect's
-- local frame, then tested like an axis-aligned (optionally rounded) rect
function shapeSurfaceUtilAPI.IsPointInRotatedRect(point : vec, min : vec, max : vec, rotation : number, cornerRadius : number?) : boolean
    local center = (min + max) * 0.5;
    local localPoint = center + MathUtil.Rotate(point - center, -rotation);
    if cornerRadius ~= nil and cornerRadius > 0.0 then
        return shapeSurfaceUtilAPI.IsPointInRoundedRect(localPoint, min, max, cornerRadius);
    end
    return shapeSurfaceUtilAPI.IsPointInRect(localPoint, min, max);
end

function shapeSurfaceUtilAPI.IsPointInEllipse(point : vec, center : vec, radii : vec, rotation : number?) : boolean
    local localPoint = point - center;
    if rotation ~= nil and rotation ~= 0.0 then
        localPoint = MathUtil.Rotate(localPoint, -rotation);
    end
    local normalized = vec.New(localPoint.x / radii.x, localPoint.y / radii.y);
    return vec.dot(normalized, normalized) <= 1.0;
end

-- "Did I click this ellipse outline": within tolerance pixels of the edge (same distance
-- approximation the renderer draws with)
function shapeSurfaceUtilAPI.IsPointNearEllipse(point : vec, center : vec, radii : vec, tolerance : number, rotation : number?) : boolean
    local localPoint = point - center;
    if rotation ~= nil and rotation ~= 0.0 then
        localPoint = MathUtil.Rotate(localPoint, -rotation);
    end
    local k0 = vec.magnitude(vec.New(localPoint.x / radii.x, localPoint.y / radii.y));
    local k1 = vec.magnitude(vec.New(localPoint.x / (radii.x * radii.x), localPoint.y / (radii.y * radii.y)));
    if k1 < 1e-6 then
        return math.min(radii.x, radii.y) <= tolerance; -- at the exact center
    end
    return math.abs(k0 * (k0 - 1.0) / k1) <= tolerance;
end

-- Matches the visual bounds of DrawRect with a corner radius
function shapeSurfaceUtilAPI.IsPointInRoundedRect(point : vec, min : vec, max : vec, cornerRadius : number) : boolean
    if not shapeSurfaceUtilAPI.IsPointInRect(point, min, max) then
        return false;
    end

    local halfExtents = (max - min) * 0.5;
    local radius = math.min(cornerRadius, math.min(halfExtents.x, halfExtents.y));
    local q = vec.abs(point - (min + halfExtents)) - (halfExtents - vec.New(radius, radius));
    if q.x <= 0.0 or q.y <= 0.0 then
        return true; -- edge bands, outside the corner circles
    end
    return vec.magnitude(vec.New(q.x, q.y)) <= radius;
end

-- "Did I click this stroke": within tolerance pixels of the path (use half the stroke width plus
-- a few pixels of grab slack)
function shapeSurfaceUtilAPI.IsPointNearPath(point : vec, points : { vec }, tolerance : number) : boolean
    return shapeSurfaceUtilAPI.DistanceToPath(point, points) <= tolerance;
end

function shapeSurfaceUtilAPI.IsPointNearWire(point : vec, from : vec, to : vec, tolerance : number) : boolean
    return shapeSurfaceUtilAPI.DistanceToWire(point, from, to) <= tolerance;
end

-- Axis-aligned rect overlap, e.g. marquee selection against node bounds
function shapeSurfaceUtilAPI.RectsOverlap(minA : vec, maxA : vec, minB : vec, maxB : vec) : boolean
    return minA.x <= maxB.x and maxA.x >= minB.x and minA.y <= maxB.y and maxA.y >= minB.y;
end

function shapeSurfaceUtilAPI.SegmentsIntersect(a1 : vec, a2 : vec, b1 : vec, b2 : vec) : boolean
    local function Orientation(p : vec, q : vec, r : vec) : number
        return math.sign(vec.cross(q - p, r - p).z);
    end

    local o1 = Orientation(a1, a2, b1);
    local o2 = Orientation(a1, a2, b2);
    local o3 = Orientation(b1, b2, a1);
    local o4 = Orientation(b1, b2, a2);
    if o1 ~= o2 and o3 ~= o4 then
        return true;
    end

    -- Collinear touching cases
    local function OnSegment(p : vec, q : vec, r : vec) : boolean
        return math.min(p.x, r.x) <= q.x and q.x <= math.max(p.x, r.x) and math.min(p.y, r.y) <= q.y and q.y <= math.max(p.y, r.y);
    end
    if o1 == 0 and OnSegment(a1, b1, a2) then return true; end
    if o2 == 0 and OnSegment(a1, b2, a2) then return true; end
    if o3 == 0 and OnSegment(b1, a1, b2) then return true; end
    if o4 == 0 and OnSegment(b1, a2, b2) then return true; end
    return false;
end

-- True when the polygons intersect or one contains the other (convex or concave, no self-intersection)
function shapeSurfaceUtilAPI.PolygonsOverlap(pointsA : { vec }, pointsB : { vec }) : boolean
    for indexA = 1, #pointsA do
        local a1 = pointsA[indexA];
        local a2 = pointsA[(indexA % #pointsA) + 1];
        for indexB = 1, #pointsB do
            local b1 = pointsB[indexB];
            local b2 = pointsB[(indexB % #pointsB) + 1];
            if shapeSurfaceUtilAPI.SegmentsIntersect(a1, a2, b1, b2) then
                return true;
            end
        end
    end

    -- No edge crossings: overlap only if one fully contains the other
    return shapeSurfaceUtilAPI.IsPointInPolygon(pointsA[1], pointsB) or shapeSurfaceUtilAPI.IsPointInPolygon(pointsB[1], pointsA);
end

function shapeSurfaceUtilAPI.DrawLine(surface : ShapeSurface, from : vec, to : vec, style : StrokeStyle)
    surface:DrawLine(from, to, style.width, style.color, style.alpha, style.dashLength, style.gapLength, style.dashOffset, style.endColor, style.endAlpha, style.capStyle);
end

-- Dash patterns and gradients both flow continuously through the joins (native implementation)
function shapeSurfaceUtilAPI.DrawPolyline(surface : ShapeSurface, points : { vec }, style : StrokeStyle)
    surface:DrawPolyline(points, style.width, style.color, style.alpha, style.dashLength, style.gapLength, style.dashOffset, style.endColor, style.endAlpha);
end

-- Node-editor style connection: a bezier with horizontal tangents scaled by the pin distance.
-- style.endColor blends from the output pin's color to the input pin's
function shapeSurfaceUtilAPI.DrawWire(surface : ShapeSurface, from : vec, to : vec, style : StrokeStyle)
    local c0, c1 = GetWireControls(from, to);
    surface:DrawBezier(from, c0, c1, to, style.width, style.color, style.alpha, style.dashLength, style.gapLength, style.dashOffset, style.endColor, style.endAlpha);
end

function shapeSurfaceUtilAPI.DrawArrow(surface : ShapeSurface, from : vec, to : vec, style : StrokeStyle, headSize : number?)
    local size = headSize or math.max(8.0, style.width * 3.0);
    shapeSurfaceUtilAPI.DrawLine(surface, from, to, style);

    local angle = math.atan2(to.y - from.y, to.x - from.x);
    local headAngle = 2.6; -- ~150 degrees back from the direction of travel
    local left = to + MathUtil.FromAngle(angle + headAngle, size);
    local right = to + MathUtil.FromAngle(angle - headAngle, size);
    surface:DrawLine(to, left, style.width, style.color, style.alpha);
    surface:DrawLine(to, right, style.width, style.color, style.alpha);
end

-- Progress-reveal: draws only the first `revealedLength` pixels of the path (one giant dash with
-- a gap no path will ever reach), so animating revealedLength makes the line draw itself.
-- Returns the position of the reveal front, e.g. for a tip marker.
function shapeSurfaceUtilAPI.DrawReveal(surface : ShapeSurface, points : { vec }, revealedLength : number, style : StrokeStyle) : vec
    if revealedLength > 0.0 then
        local offset = 0.0;
        for index = 1, #points - 1 do
            surface:DrawLine(points[index], points[index + 1], style.width, style.color, style.alpha, revealedLength, 1000000.0, offset);
            offset += vec.magnitude(points[index + 1] - points[index]);
        end
    end
    return shapeSurfaceUtilAPI.PointOnPath(points, revealedLength);
end

-- Square grab handle, curve-editor style
function shapeSurfaceUtilAPI.DrawHandle(surface : ShapeSurface, pos : vec, size : number, fillColor : vec, alpha : number?)
    local extent = vec.New(size, size) * 0.5;
    surface:DrawRect(pos - extent, pos + extent, 2.0, fillColor, alpha);
end

function shapeSurfaceUtilAPI.DrawCross(surface : ShapeSurface, pos : vec, size : number, style : StrokeStyle)
    local half = size * 0.5;
    shapeSurfaceUtilAPI.DrawLine(surface, pos - vec.New(half, 0), pos + vec.New(half, 0), style);
    shapeSurfaceUtilAPI.DrawLine(surface, pos - vec.New(0, half), pos + vec.New(0, half), style);
end

-- Stroked pie wedge: the arc plus its two radial edges. Complements DrawPie (fills don't dash;
-- this is the dashable counterpart)
function shapeSurfaceUtilAPI.DrawPieOutline(surface : ShapeSurface, center : vec, radius : number, startAngle : number, endAngle : number, style : StrokeStyle)
    surface:DrawArc(center, radius, startAngle, endAngle, style.width, style.color, style.alpha, style.dashLength, style.gapLength, style.dashOffset);
    if endAngle - startAngle < math.pi * 2.0 then
        shapeSurfaceUtilAPI.DrawLine(surface, center, center + MathUtil.FromAngle(startAngle, radius), style);
        shapeSurfaceUtilAPI.DrawLine(surface, center, center + MathUtil.FromAngle(endAngle, radius), style);
    end
end

return shapeSurfaceUtilAPI;
