export type InventoryMove =
{
    kind : "InventoryMove",
    source : any,
}

export type ActionReference =
{
    kind : "ActionReference",
    payload : any,
}

export type Operation = InventoryMove | ActionReference;

local InputAPI = require("@src/API/Input/Input");

export type DragContextAPI =
{
    Initialize : (self : DragContextAPI) -> (),
    HasOperation : (self : DragContextAPI) -> boolean,
    GetOperation : (self : DragContextAPI) -> Operation?,
    GetItem : (self : DragContextAPI) -> integer,
    BeginInventoryMove : (self : DragContextAPI, itemSlot : any) -> (),
    CommitInventoryMove : (self : DragContextAPI, itemSlot : any) -> (),
    BeginActionReference : (self : DragContextAPI, payload : any, icon : string?) -> (),
    TakeActionReference : (self : DragContextAPI) -> any?,
    Clear : (self : DragContextAPI) -> (),
}

local dragContextAPI : DragContextAPI = { } :: any;
local state = {
    cursorPanel = nil,
    operation = nil,
} :: any;
local initialized = false;

local function CancelHeldOperation(phase : integer) : integer
    if phase ~= InputPhase.Pressed or state.operation == nil then
        return InputReply.Ignored;
    end

    if state.operation.kind == "InventoryMove" then
        state.operation.source:SetSelected(false);
    end

    dragContextAPI:Clear();
    return InputReply.Consumed;
end

local function SetVisual(itemID : integer?, icon : string?)
    if state.cursorPanel == nil then dragContextAPI:Initialize(); end
    local texture = icon;
    if texture == nil and itemID ~= nil then
        local item = Item.GetItemInfo(itemID);
        local itemIcon = item and Item.GetIconInfo(item.IconID) or nil;
        texture = itemIcon and itemIcon.Texture or "texture/unknown.png";
    end
    state.cursorPanel:SetBackground(texture);
    state.cursorPanel:SetEnabled(true);
    SetCursor("item");
end

function dragContextAPI:Initialize()
    if initialized then return; end
    initialized = true;

    local cancelInputOptions =
    {
        context = "Global",
        defaultReply = InputReply.Ignored,
        rebindable = false,
    };
    InputAPI.Bind("CancelHeldOperationLeftMouse", "Cancel Held Operation", "UI",
        InputAPI.Mouse(0i, InputModifier.None, InputModifierMatch.Any), CancelHeldOperation, cancelInputOptions);
    InputAPI.Bind("CancelHeldOperationRightMouse", "Cancel Held Operation", "UI",
        InputAPI.Mouse(1i, InputModifier.None, InputModifierMatch.Any), CancelHeldOperation, cancelInputOptions);

    local canvas = UI.GetCanvas("CursorCanvas", vec.New(0, 0), vec.New(1920, 1080));
    state.cursorPanel = canvas:NewPanel(vec.New(0, 0), vec.New(48, 48), 0i, "TexturePanel");
    state.cursorPanel:SetEnabled(false);
    state.cursorPanel:SetInteractable(false);
    state.cursorPanel:SetAnchor(vec.New(0.0, 0.0));
    state.cursorPanel:SetRelativePoint(vec.New(0.05, 0.95));
end

function dragContextAPI:HasOperation() : boolean return state.operation ~= nil; end
function dragContextAPI:GetOperation() : Operation? return state.operation; end
function dragContextAPI:GetItem() : integer
    local operation = state.operation;
    return if operation ~= nil and operation.kind == "InventoryMove" then operation.source.itemID else 0i;
end

function dragContextAPI:BeginInventoryMove(itemSlot : any)
    if state.operation ~= nil or itemSlot.itemID == 0i then return; end
    state.operation = { kind = "InventoryMove", source = itemSlot };
    SetVisual(itemSlot.itemID, nil);
end

function dragContextAPI:CommitInventoryMove(itemSlot : any)
    local operation = state.operation;
    if operation == nil or operation.kind ~= "InventoryMove" then return; end
    local source = operation.source;
    local sourceBag = source.container;
    local destinationBag = itemSlot.container;
    local sourceIndex = source.slotIndex;
    local destinationIndex = itemSlot.slotIndex;
    local sentRequest = false;

    if sourceBag ~= destinationBag or sourceIndex ~= destinationIndex then
        local sourceItemID = source.itemID;
        local sourceCount = sourceBag:GetItemCount(sourceIndex);
        local destinationItemID = itemSlot.itemID;
        local destinationCount = destinationBag:GetItemCount(destinationIndex);
        local sourceFits = destinationBag:CanFitInSlot(sourceItemID, sourceCount, destinationIndex);
        local destinationFits = destinationItemID == 0i or sourceBag:CanFitInSlot(destinationItemID, destinationCount, sourceIndex);
        if sourceFits and destinationFits and Container.RequestSwapSlots(
            sourceBag.containerIndex,
            destinationBag.containerIndex,
            integer.create(sourceIndex),
            integer.create(destinationIndex)
        ) then
            sourceBag:LockItem(sourceIndex);
            destinationBag:LockItem(destinationIndex);
            sentRequest = true;
        end
    end
    if not sentRequest then
        source:SetSelected(false);
        itemSlot:SetSelected(false);
    end
    self:Clear();
end

function dragContextAPI:BeginActionReference(payload : any, icon : string?)
    if state.operation ~= nil then return; end
    state.operation = { kind = "ActionReference", payload = payload };
    SetVisual(nil, icon);
end

function dragContextAPI:TakeActionReference()
    local operation = state.operation;
    if operation == nil or operation.kind ~= "ActionReference" then return nil; end
    local payload = operation.payload;
    self:Clear();
    return payload;
end

function dragContextAPI:Clear()
    state.operation = nil;
    if state.cursorPanel ~= nil then state.cursorPanel:SetEnabled(false); end
    SetCursor("point");
end

return dragContextAPI;
