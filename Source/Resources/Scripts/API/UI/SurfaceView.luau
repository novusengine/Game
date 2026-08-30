-- Pan/zoom viewport for a ShapeSurface-backed panel: continuous cursor-anchored scroll zoom with
-- smooth (frame-rate-independent) easing toward the target zoom, drag panning on a configurable
-- button, view-offset clamping (small content centers, large content clamps to the edges), and
-- world <-> surface coordinate mapping. The consumer draws in world coordinates through the
-- mapping helpers and redraws when onViewChanged fires; all other SurfaceInteraction options pass
-- through untouched.
local MathUtil = require("@src/API/MathUtil")
local SurfaceInteraction = require("@src/API/UI/SurfaceInteraction")

export type Options = {
    minZoom : number?,         -- pixels per world unit, default 1
    maxZoom : number?,         -- default minZoom
    zoom : number?,            -- initial zoom, default minZoom
    zoomStep : number?,        -- zoom factor per wheel notch, default 1.25
    smoothing : number?,       -- easing speed toward the target zoom (see MathUtil.Damp), default 12; 0 snaps
    contentSize : vec,         -- content extents in world units
    panButton : integer?,      -- mouse button that pans the view, default 2 (middle)
    onViewChanged : (() -> ())?,

    -- Passed through to SurfaceInteraction (hitTest/onDrag run in surface coordinates; convert
    -- with SurfaceToWorld as needed)
    button : integer?,
    movePanel : boolean?,
    hitTest : ((surfacePos : vec, mouseButton : integer) -> any?)?,
    onDragStart : ((target : any, surfacePos : vec) -> ())?,
    onDrag : ((target : any, surfacePos : vec) -> ())?,
    onDragEnd : ((target : any, surfacePos : vec) -> ())?,
    onHover : ((surfacePos : vec) -> ())?,
    onHoverEnd : (() -> ())?,
    getCursor : ((surfacePos : vec) -> string?)?,
    onDoubleClick : ((surfacePos : vec, mouseButton : integer) -> ())?,
    doubleClickTime : number?,
}

export type SurfaceView = {
    WorldToSurface : (self : SurfaceView, worldPos : vec) -> vec,
    SurfaceToWorld : (self : SurfaceView, surfacePos : vec) -> vec,
    GetPixelsPerUnit : (self : SurfaceView) -> number,
    GetVisibleRange : (self : SurfaceView) -> (vec, vec), -- world-space min, max clamped to the content
    Reset : (self : SurfaceView) -> (),
}

export type SurfaceViewAPI = {
    Attach : (panel : Panel, surface : ShapeSurface, options : Options) -> SurfaceView,
}

local surfaceViewAPI : SurfaceViewAPI = { }

-- One module-level frame handler drives every easing view; views enroll on scroll and drop out
-- when the easing settles, so idle views (and destroyed panels) cost nothing per frame
local easingSteps : { [any] : (deltaTime : number) -> boolean } = {};
RegisterEvent(GameEvent.Updated, function(eventID : integer, data : any)
    for view, step in pairs(easingSteps) do
        if step(data.deltaTime) then
            easingSteps[view] = nil;
        end
    end
end)

function surfaceViewAPI.Attach(panel : Panel, surface : ShapeSurface, options : Options) : SurfaceView
    local surfaceSize = surface:GetSize();

    local minZoom = options.minZoom or 1.0;
    local maxZoom = options.maxZoom or minZoom;
    local initialZoom = math.clamp(options.zoom or minZoom, minZoom, maxZoom);
    local zoomStep = options.zoomStep or 1.25;
    local smoothing = options.smoothing or 12.0;

    local pixelsPerUnit = initialZoom;
    local targetZoom = initialZoom;
    local zoomAnchorWorld = nil;   -- world point pinned under the cursor while easing
    local zoomAnchorSurface = vec.New(0, 0);
    local offset = vec.New(0, 0);

    local function ClampAxis(value : number, contentExtent : number, viewExtent : number) : number
        local extent = contentExtent * pixelsPerUnit;
        if extent <= viewExtent then return math.floor((viewExtent - extent) / 2); end
        return math.clamp(value, viewExtent - extent, 0);
    end

    local function ClampOffset(candidate : vec) : vec
        return vec.New(ClampAxis(candidate.x, options.contentSize.x, surfaceSize.x), ClampAxis(candidate.y, options.contentSize.y, surfaceSize.y));
    end

    local function ViewChanged()
        if options.onViewChanged ~= nil then options.onViewChanged(); end
    end

    local view = {};

    function view:WorldToSurface(worldPos : vec) : vec
        return offset + worldPos * pixelsPerUnit;
    end

    function view:SurfaceToWorld(surfacePos : vec) : vec
        return (surfacePos - offset) / pixelsPerUnit;
    end

    function view:GetPixelsPerUnit() : number
        return pixelsPerUnit;
    end

    function view:GetVisibleRange() : (vec, vec)
        local visibleMin = vec.max((vec.New(0, 0) - offset) / pixelsPerUnit, vec.New(0, 0));
        local visibleMax = vec.min((surfaceSize - offset) / pixelsPerUnit, options.contentSize);
        return visibleMin, visibleMax;
    end

    function view:Reset()
        pixelsPerUnit = initialZoom;
        targetZoom = initialZoom;
        zoomAnchorWorld = nil;
        offset = ClampOffset(vec.New(0, 0));
        ViewChanged();
    end

    local function ApplyZoom(newZoom : number)
        pixelsPerUnit = newZoom;
        if zoomAnchorWorld ~= nil then
            offset = ClampOffset(zoomAnchorSurface - zoomAnchorWorld * pixelsPerUnit);
        else
            offset = ClampOffset(offset);
        end
        ViewChanged();
    end

    local panButton = options.panButton or 2i;
    local StepEasing; -- defined below; enrolled into easingSteps while a zoom eases
    local function IsPanTarget(target : any) : boolean
        return type(target) == "table" and target.__surfaceViewPan == true;
    end

    SurfaceInteraction.Attach(panel, surface,
    {
        button = options.button,
        movePanel = options.movePanel,
        onHover = options.onHover,
        onHoverEnd = options.onHoverEnd,
        getCursor = options.getCursor,
        onDoubleClick = options.onDoubleClick,
        doubleClickTime = options.doubleClickTime,

        hitTest = function(surfacePos, mouseButton)
            if mouseButton == panButton then
                -- Finish any in-flight zoom easing so the pan and the easing don't fight over the offset
                if pixelsPerUnit ~= targetZoom then
                    ApplyZoom(targetZoom);
                end
                zoomAnchorWorld = nil;
                return { __surfaceViewPan = true, startOffset = offset, pressPos = surfacePos };
            end
            return if options.hitTest ~= nil then options.hitTest(surfacePos, mouseButton) else nil;
        end,

        onDragStart = function(target, surfacePos)
            if IsPanTarget(target) then return; end
            if options.onDragStart ~= nil then options.onDragStart(target, surfacePos); end
        end,

        onDrag = function(target, surfacePos)
            if IsPanTarget(target) then
                offset = ClampOffset(target.startOffset + surfacePos - target.pressPos);
                ViewChanged();
                return;
            end
            if options.onDrag ~= nil then options.onDrag(target, surfacePos); end
        end,

        onDragEnd = function(target, surfacePos)
            if IsPanTarget(target) then return; end
            if options.onDragEnd ~= nil then options.onDragEnd(target, surfacePos); end
        end,

        -- Cursor-anchored zoom: the world point under the mouse stays under the mouse. The zoom
        -- eases toward the target over the following frames (see the Updated handler below)
        onScroll = function(surfacePos, delta)
            local nextTarget = math.clamp(targetZoom * zoomStep ^ delta, minZoom, maxZoom);
            if nextTarget == targetZoom then return; end

            targetZoom = nextTarget;
            zoomAnchorSurface = surfacePos;
            zoomAnchorWorld = view:SurfaceToWorld(surfacePos);
            if smoothing <= 0.0 then
                ApplyZoom(targetZoom);
            else
                easingSteps[view] = StepEasing;
            end
        end,
    });

    function StepEasing(deltaTime : number) : boolean
        if pixelsPerUnit == targetZoom then return true; end

        local eased = MathUtil.Damp(pixelsPerUnit, targetZoom, smoothing, deltaTime);
        if MathUtil.Approximately(eased, targetZoom, targetZoom * 0.001) then
            eased = targetZoom;
            zoomAnchorWorld = nil;
        end
        ApplyZoom(eased);
        return pixelsPerUnit == targetZoom;
    end

    offset = ClampOffset(vec.New(0, 0));
    return view;
end

return surfaceViewAPI;
