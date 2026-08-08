local Input = require("@src/API/Input/Input");
local Slot = require("@src/API/UI/Slot");
local DragContext = require("@src/API/Game/UI/DragContext");
local ItemAction = require("@src/API/Game/UI/ItemAction");
local RootedWidget = require("@src/API/UI/RootedWidget");

export type Options =
{
    rows : number?,
    columns : number?,
    slotSize : number?,
    padding : vec?,
    panelTemplate : string?,
    textTemplate : string?,
    emptyTexture : string?,
    emptyColor : vec?,
    emptyAlpha : number?,
    payloadColor : vec?,
}

export type ActionSlot =
{
    slot : Slot.Slot,
    actionName : string,
    inputConnection : integer,
}

export type ActionBar =
{
    root : Widget,
    slots : { ActionSlot },
    SetEnabled : (self : ActionBar, enabled : boolean) -> (),
    SetPayload : (self : ActionBar, index : number, payload : Slot.Payload?) -> (),
    SetKey : (self : ActionBar, index : number, key : integer) -> (),
}

export type ActionBarAPI = { New : (parent : Widget, position : vec, options : Options?) -> ActionBar }
local actionBarAPI : ActionBarAPI = { };
local DEFAULT_EMPTY_TEXTURE = "texture/ui/icons/itemslots/slot_empty.png";
local DEFAULT_EMPTY_COLOR = vec.New(1.0, 1.0, 1.0);
local DEFAULT_PAYLOAD_COLOR = vec.New(1.0, 1.0, 1.0);
local PRESSED_PAYLOAD_COLOR = vec.New(0.5, 0.5, 0.5);

function actionBarAPI.New(parent : Widget, position : vec, options : Options?) : ActionBar
    local opts = options or { };
    local rows = math.max(1, opts.rows or 1);
    local columns = math.max(1, opts.columns or 1);
    local slotSize = opts.slotSize or 40;
    local padding = opts.padding or vec.New(0, 0);
    local root = parent:NewWidget(position, 0i);
    local self = { root = root, slots = { } } :: any;
    RootedWidget.Attach(self);

    function self:SetEnabled(enabled : boolean) root:SetEnabled(enabled); end
    function self:SetPayload(index : number, payload : Slot.Payload?)
        local actionSlot = self.slots[index];
        if actionSlot == nil then error(`ActionBar: slot {index} is out of bounds`); end
        actionSlot.slot:SetPayload(payload);
    end
    function self:SetKey(index : number, key : integer)
        local actionSlot = self.slots[index];
        if actionSlot == nil then error(`ActionBar: slot {index} is out of bounds`); end
        local binding = Input.Keyboard(key, InputModifier.None, InputModifierMatch.Any);
        if actionSlot.inputConnection == 0i then
            Input.RegisterAction(actionSlot.actionName, `Action Bar Slot {index}`, "Action Bar", binding);
            actionSlot.inputConnection = Input.Connect(actionSlot.actionName, function(phase)
                if phase == InputPhase.Pressed or phase == InputPhase.Repeated then
                    actionSlot.slot.root:SetColor(PRESSED_PAYLOAD_COLOR);
                    return InputReply.Consumed;
                end
                if phase == InputPhase.Released or phase == InputPhase.Canceled then
                    actionSlot.slot:Refresh();
                    if phase == InputPhase.Released then actionSlot.slot:Activate(); end
                end
                return InputReply.Consumed;
            end);
        else
            Input.SetBinding(actionSlot.actionName, 1i, binding);
        end
        local activeBinding = Input.GetBinding(actionSlot.actionName, 1i) or binding;
        actionSlot.slot.button:SetText(Input.GetKeyName(activeBinding.code));
    end

    for row = 1, rows do
        for column = 1, columns do
            local index = (row - 1) * columns + column;
            local slot = Slot.New(root, vec.New((column - 1) * (slotSize + padding.x), (row - 1) * (slotSize + padding.y)), vec.New(slotSize, slotSize), 0i, {
                panelTemplate = opts.panelTemplate or "ActionBarButton",
                textTemplate = opts.textTemplate or "ActionBarButtonText",
                emptyTexture = opts.emptyTexture or DEFAULT_EMPTY_TEXTURE,
                emptyColor = opts.emptyColor or DEFAULT_EMPTY_COLOR,
                emptyAlpha = opts.emptyAlpha or 1.0,
                payloadColor = opts.payloadColor or DEFAULT_PAYLOAD_COLOR,
            });
            slot:Refresh();
            slot.button.text:SetAnchor(vec.New(1.0, 0.0));
            slot.button.text:SetRelativePoint(vec.New(1.0, 0.0));
            local pressPosition = vec.New(0, 0);
            local dragging = false;
            slot.root:SetOnMouseDown(function(_, _, button, mouseX, mouseY)
                if button ~= 0i then return; end
                pressPosition = vec.New(mouseX, mouseY);
                dragging = false;
            end);
            slot.root:SetOnMouseHeld(function(_, _, _, mouseX, mouseY)
                if dragging or slot.payload == nil then return; end
                if math.abs(mouseX - pressPosition.x) + math.abs(mouseY - pressPosition.y) > 3 then
                    dragging = true;
                    DragContext:BeginActionReference(slot.payload, slot.payload:GetIcon());
                end
            end);
            slot.root:SetOnMouseUp(function(_, _, button)
                if button ~= 0i then return; end
                local operation = DragContext:GetOperation();
                if operation ~= nil and operation.kind == "ActionReference" then
                    slot:SetPayload(DragContext:TakeActionReference());
                elseif operation ~= nil and operation.kind == "InventoryMove" then
                    slot:SetPayload(ItemAction.New(operation.source.itemID));
                    operation.source:SetSelected(false);
                    DragContext:Clear();
                elseif not dragging then
                    slot:Activate();
                end
                dragging = false;
            end);
            self.slots[index] = { slot = slot, actionName = `ActionBarSlot{index}`, inputConnection = 0i };
        end
    end

    return self :: ActionBar;
end

return actionBarAPI;
