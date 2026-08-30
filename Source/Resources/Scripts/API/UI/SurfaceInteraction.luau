-- Input scaffold for panels backed by a ShapeSurface: converts the mouse to surface space, runs a
-- hit test on press, and routes the drag lifecycle to the hit target — falling back to moving the
-- panel itself when nothing was hit. Collapses the mouse down/held/release boilerplate that every
-- interactive surface (node graph, curve editor, dial, minimap) otherwise reimplements.
local ShapeSurfaceUtil = require("@src/API/UI/ShapeSurfaceUtil")

export type Options =
{
    button : integer?,     -- mouse button that drags the panel on an unhit press, default 0 (left)
    movePanel : boolean?,  -- default true: a press that hits nothing drags the panel

    -- Return a non-nil target to begin an interaction with it; return nil to fall through to the
    -- panel drag. Runs on press for every mouse button, in surface coordinates.
    hitTest : ((surfacePos : vec, mouseButton : integer) -> any?)?,

    onDragStart : ((target : any, surfacePos : vec) -> ())?,
    onDrag : ((target : any, surfacePos : vec) -> ())?,
    onDragEnd : ((target : any, surfacePos : vec) -> ())?,

    -- Fired while the cursor is over the panel (every frame, no button needed) and when it leaves
    onHover : ((surfacePos : vec) -> ())?,
    onHoverEnd : (() -> ())?,

    -- Mouse wheel over the panel, in surface coordinates (e.g. cursor-anchored zoom)
    onScroll : ((surfacePos : vec, delta : number) -> ())?,

    -- Cursor feedback: return a cursor name (see Misc/Cursors.luau) to apply while hovering, or
    -- nil for the default; the default cursor is restored when the hover ends
    getCursor : ((surfacePos : vec) -> string?)?,

    -- Two presses of the same button within doubleClickTime seconds and 6px of each other.
    -- Fires on the second press, after that press's hitTest/onDragStart
    onDoubleClick : ((surfacePos : vec, mouseButton : integer) -> ())?,
    doubleClickTime : number?, -- seconds, default 0.35
}

-- Callbacks that need modifier keys (shift/ctrl variants of a drag) should query the Input global
-- themselves, e.g. InputAPI.IsDown, the way TerrainEditor's chunk operations do.

export type SurfaceInteractionAPI =
{
    Attach : (panel : Panel, surface : ShapeSurface, options : Options) -> (),
}

local surfaceInteractionAPI : SurfaceInteractionAPI = { }

function surfaceInteractionAPI.Attach(panel : Panel, surface : ShapeSurface, options : Options)
    local button = options.button or 0i;
    local movePanel = options.movePanel ~= false;

    local target = nil;
    local targetButton = 0i;
    local dragMouseStart = nil;
    local dragPanelStart = vec.New(0, 0);
    local lastPressTime = -math.huge;
    local lastPressButton = 0i;
    local lastPressPos = vec.New(0, 0);

    local function GetSurfacePos() : vec
        return ShapeSurfaceUtil.ScreenToSurface(panel, surface, UI.GetMousePos());
    end

    panel:SetOnMouseDown(function(_, _, mouseButton)
        local surfacePos = GetSurfacePos();
        local hit = if options.hitTest ~= nil then options.hitTest(surfacePos, mouseButton) else nil;
        if hit ~= nil then
            target = hit;
            targetButton = mouseButton;
            if options.onDragStart ~= nil then options.onDragStart(target, surfacePos); end
        elseif mouseButton == button and movePanel then
            dragMouseStart = UI.GetMousePos();
            dragPanelStart = vec.New(panel:GetPosX(), panel:GetPosY());
        end

        if options.onDoubleClick ~= nil then
            local now = os.clock();
            if mouseButton == lastPressButton and now - lastPressTime <= (options.doubleClickTime or 0.35) and vec.magnitude(surfacePos - lastPressPos) <= 6.0 then
                lastPressTime = -math.huge; -- a triple press is not two double clicks
                options.onDoubleClick(surfacePos, mouseButton);
            else
                lastPressTime = now;
                lastPressButton = mouseButton;
                lastPressPos = surfacePos;
            end
        end
    end);

    panel:SetOnMouseHeld(function()
        if target ~= nil then
            if options.onDrag ~= nil then options.onDrag(target, GetSurfacePos()); end
        elseif dragMouseStart ~= nil then
            panel:SetPos(dragPanelStart + UI.GetMousePos() - dragMouseStart);
        end
    end);

    panel:SetOnMouseRelease(function(_, _, mouseButton)
        if target ~= nil and mouseButton == targetButton then
            if options.onDragEnd ~= nil then options.onDragEnd(target, GetSurfacePos()); end
            target = nil;
        end
        if mouseButton == button then
            dragMouseStart = nil;
        end
    end);

    local activeCursor = nil;
    if options.onHover ~= nil or options.getCursor ~= nil then
        panel:SetOnHoverHeld(function()
            local surfacePos = GetSurfacePos();
            if options.onHover ~= nil then options.onHover(surfacePos); end
            if options.getCursor ~= nil then
                local desired = options.getCursor(surfacePos) or "point";
                if desired ~= activeCursor then
                    activeCursor = desired;
                    SetCursor(desired);
                end
            end
        end);
    end
    if options.onHoverEnd ~= nil or options.getCursor ~= nil then
        panel:SetOnHoverEnd(function()
            if options.onHoverEnd ~= nil then options.onHoverEnd(); end
            if activeCursor ~= nil and activeCursor ~= "point" then
                SetCursor("point");
            end
            activeCursor = nil;
        end);
    end
    if options.onScroll ~= nil then
        panel:SetOnMouseScroll(function(_, _, _, scrollY)
            if scrollY ~= 0 then
                options.onScroll(GetSurfacePos(), scrollY);
            end
        end);
    end
end

return surfaceInteractionAPI;
