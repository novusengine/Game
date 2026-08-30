local Button = require("@src/API/UI/Button");
local RootedWidget = require("@src/API/UI/RootedWidget");

export type Payload =
{
    GetIcon : (self : Payload) -> string?,
    IsAvailable : (self : Payload) -> boolean,
    Activate : (self : Payload) -> boolean,
    ShowTooltip : ((self : Payload) -> ())?,
    HideTooltip : ((self : Payload) -> ())?,
}

export type Slot =
{
    root : Panel,
    button : Button.Button,
    payload : Payload?,
    SetPayload : (self : Slot, payload : Payload?) -> (),
    GetPayload : (self : Slot) -> Payload?,
    Refresh : (self : Slot) -> (),
    Activate : (self : Slot) -> boolean,
}

export type Options =
{
    panelTemplate : string?,
    textTemplate : string?,
    text : string?,
    emptyTexture : string?,
    emptyColor : vec?,
    emptyAlpha : number?,
    payloadColor : vec?,
}
export type SlotAPI = { New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options?) -> Slot }
local slotAPI : SlotAPI = { };
local DEFAULT_PAYLOAD_COLOR = vec.New(1.0, 1.0, 1.0);

function slotAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options?)
    local opts = options or { };
    local button = Button.New(parent, position, size, layer, {
        panelTemplate = opts.panelTemplate or "TexturePanel",
        textTemplate = opts.textTemplate or "DefaultButtonText",
        text = opts.text or "",
    });
    local self = { root = button.root, button = button, payload = nil } :: any;
    RootedWidget.Attach(self);

    function self:Refresh()
        local payload = self.payload;
        if payload == nil then
            self.root:SetBackground(opts.emptyTexture);
            if opts.emptyColor ~= nil then self.root:SetColor(opts.emptyColor); end
            self.root:SetAlpha(opts.emptyAlpha or 0.65);
            return;
        end
        self.root:SetBackground(payload:GetIcon());
        self.root:SetColor(opts.payloadColor or DEFAULT_PAYLOAD_COLOR);
        self.root:SetAlpha(if payload:IsAvailable() then 1.0 else 0.4);
    end

    function self:SetPayload(payload : Payload?)
        self.payload = payload;
        self:Refresh();
    end
    function self:GetPayload() return self.payload; end
    function self:Activate() : boolean
        if self.payload == nil or not self.payload:IsAvailable() then return false; end
        return self.payload:Activate();
    end

    button:SetOnMouseUp(function(_, _, mouseButton)
        if mouseButton == 0i then self:Activate(); end
    end);
    button:SetOnHoverBegin(function()
        if self.payload ~= nil and self.payload.ShowTooltip ~= nil then self.payload:ShowTooltip(); end
    end);
    button:SetOnHoverEnd(function()
        if self.payload ~= nil and self.payload.HideTooltip ~= nil then self.payload:HideTooltip(); end
    end);
    return self :: Slot;
end

return slotAPI;
