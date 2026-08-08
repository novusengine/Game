export type Popup =
{
    shields : { Panel },
    content : Widget?,
    SetContent : (self : Popup, content : Widget) -> (),
    Close : (self : Popup) -> (),
}

export type PopupLayerAPI =
{
    Open : (parent : Widget, layer : integer, onClose : (() -> ())?, inputPassthrough : Panel?, closeOnHover : boolean?) -> Popup,
    CloseActive : () -> (),
}

local popupLayerAPI : PopupLayerAPI = { };
local activePopup : Popup? = nil;
local SHIELD_SIZE = 8192;
local SHIELD_MIN = -SHIELD_SIZE / 2;
local SHIELD_MAX = SHIELD_MIN + SHIELD_SIZE;

function popupLayerAPI.CloseActive()
    if activePopup ~= nil then activePopup:Close(); end
end

function popupLayerAPI.Open(parent : Widget, layer : integer, onClose : (() -> ())?, inputPassthrough : Panel?, closeOnHover : boolean?) : Popup
    popupLayerAPI.CloseActive();

    local self = {
        shields = { },
        content = nil,
        _onClose = onClose,
        _closed = false,
    } :: any;

    function self:SetContent(content : Widget)
        self.content = content;
    end

    function self:Close()
        if self._closed then return; end
        self._closed = true;
        if self.content ~= nil then UI.DestroyWidget(self.content); end
        for _, shield in self.shields do
            UI.DestroyWidget(shield);
        end
        if activePopup == self then activePopup = nil; end
        if self._onClose ~= nil then self._onClose(); end
    end

    local function AddShield(x : number, y : number, width : number, height : number)
        if width <= 0 or height <= 0 then return; end

        local shield = parent:NewPanel(vec.New(x, y), vec.New(width, height), layer, "DebugDarkGrey");
        shield:SetAnchor(vec.New(0.0, 0.0));
        shield:SetRelativePoint(vec.New(0.0, 0.0));
        shield:SetAlpha(0.0);
        shield:SetOnMouseUp(function() self:Close(); end);
        if closeOnHover then shield:SetOnHoverBegin(function() self:Close(); end); end
        table.insert(self.shields, shield);
    end

    if inputPassthrough == nil then
        AddShield(SHIELD_MIN, SHIELD_MIN, SHIELD_SIZE, SHIELD_SIZE);
    else
        local position = inputPassthrough:GetWorldPos();
        local size = inputPassthrough:GetSize();
        local right = position.x + size.x;
        local top = position.y + size.y;

        AddShield(SHIELD_MIN, SHIELD_MIN, SHIELD_SIZE, position.y - SHIELD_MIN);
        AddShield(SHIELD_MIN, top, SHIELD_SIZE, SHIELD_MAX - top);
        AddShield(SHIELD_MIN, position.y, position.x - SHIELD_MIN, size.y);
        AddShield(right, position.y, SHIELD_MAX - right, size.y);
    end

    activePopup = self :: Popup;
    return self :: Popup;
end

return popupLayerAPI;
