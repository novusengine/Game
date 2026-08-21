export type Options =
{
    minimum : vec?,
    maximum : vec?,
    moveTarget : boolean?,
    button : integer?,
    threshold : number?,
    onBegin : ((target : Widget, mousePosition : vec) -> ())?,
    onMove : ((target : Widget, mousePosition : vec, delta : vec) -> ())?,
    onEnd : ((target : Widget, mousePosition : vec, delta : vec) -> ())?,
}

export type DraggableAPI =
{
    Attach : (target : Widget, handle : Widget?, options : Options?) -> (),
}

local draggableAPI : DraggableAPI = { };

function draggableAPI.Attach(target : Widget, handle : Widget?, options : Options?)
    local dragHandle = handle or target;
    local opts = options or { };
    local mouseStart = vec.New(0, 0);
    local positionStart = vec.New(0, 0);
    local pressed = false;
    local dragging = false;
    local button = opts.button or 0i;
    local threshold = math.max(0, opts.threshold or 0);
    local moveTarget = opts.moveTarget ~= false;

    local function Begin(mousePosition : vec)
        dragging = true;
        if opts.onBegin ~= nil then opts.onBegin(target, mousePosition); end
    end

    dragHandle:SetOnMouseDown(function(_, _, mouseButton)
        if mouseButton ~= button then return; end
        pressed = true;
        dragging = false;
        mouseStart = UI.GetMousePos();
        positionStart = vec.New(target:GetPosX(), target:GetPosY());
        if threshold == 0 then Begin(mouseStart); end
    end);

    dragHandle:SetOnMouseHeld(function()
        if not pressed then return; end
        local mousePosition = UI.GetMousePos();
        local delta = mousePosition - mouseStart;
        if not dragging then
            local distanceSquared = delta.x * delta.x + delta.y * delta.y;
            if distanceSquared < threshold * threshold then return; end
            Begin(mousePosition);
        end

        if moveTarget then
            local position = positionStart + delta;
            if opts.minimum ~= nil then
                position = vec.New(math.max(position.x, opts.minimum.x), math.max(position.y, opts.minimum.y));
            end
            if opts.maximum ~= nil then
                position = vec.New(math.min(position.x, opts.maximum.x), math.min(position.y, opts.maximum.y));
            end
            target:SetPos(position);
        end

        if opts.onMove ~= nil then opts.onMove(target, mousePosition, delta); end
    end);

    dragHandle:SetOnMouseRelease(function(_, _, mouseButton)
        if mouseButton ~= button or not pressed then return; end
        local mousePosition = UI.GetMousePos();
        local delta = mousePosition - mouseStart;
        pressed = false;
        if not dragging then return; end
        dragging = false;
        if opts.onEnd ~= nil then opts.onEnd(target, mousePosition, delta); end
    end);
end

return draggableAPI;
