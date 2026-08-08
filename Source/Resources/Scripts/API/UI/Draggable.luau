export type Options =
{
    minimum : vec?,
    maximum : vec?,
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

    dragHandle:SetOnMouseDown(function()
        mouseStart = UI.GetMousePos();
        positionStart = vec.New(target:GetPosX(), target:GetPosY());
    end);

    dragHandle:SetOnMouseHeld(function()
        local mousePosition = UI.GetMousePos();
        local position = positionStart + (mousePosition - mouseStart);
        if opts.minimum ~= nil then
            position = vec.New(math.max(position.x, opts.minimum.x), math.max(position.y, opts.minimum.y));
        end
        if opts.maximum ~= nil then
            position = vec.New(math.min(position.x, opts.maximum.x), math.min(position.y, opts.maximum.y));
        end
        target:SetPos(position);
    end);
end

return draggableAPI;
