local RootedWidget = require("@src/API/UI/RootedWidget");

export type Options =
{
    text : string?,
    panelTemplate : string?,
    textTemplate : string?,
    textAlign : ("left" | "center" | "right")?,
    textPadX : number?,
    color : vec?,
    hoverColor : vec?,
    highlightColor : vec?,
    alpha : number?,
    hoverAlpha : number?,
    highlightAlpha : number?,
    disabledAlpha : number?,
    borderColor : vec?,
    borderSize : number?,
}

export type Button =
{
    root : Panel,
    text : TextWidget,
    SetText : (self : Button, text : string) -> (),
    SetFontSize : (self : Button, size : number) -> (),
    SetEnabled : (self : Button, enabled : boolean) -> (),
    SetVisible : (self : Button, visible : boolean) -> (),
    SetInteractable : (self : Button, interactable : boolean) -> (),
    SetVisualEnabled : (self : Button, enabled : boolean) -> (),
    SetFocusable : (self : Button, focusable : boolean) -> (),
    SetHighlighted : (self : Button, highlighted : boolean) -> (),
    SetSize : (self : Button, size : vec) -> (),
    NewWidget : (self : Button, position : vec, layer : integer) -> Widget,
    NewPanel : (self : Button, position : vec, size : vec, layer : integer, template : string?) -> Panel,
    NewText : (self : Button, text : string, position : vec, layer : integer, template : string) -> TextWidget,
    GetAnchor : (self : Button) -> vec,
    GetRelativePoint : (self : Button) -> vec,
    SetAnchor : (self : Button, anchor : vec) -> (),
    SetRelativePoint : (self : Button, point : vec) -> (),
    GetPos : (self : Button) -> vec,
    GetPosX : (self : Button) -> number,
    GetPosY : (self : Button) -> number,
    SetPos : (self : Button, position : vec) -> (),
    SetPosX : (self : Button, x : number) -> (),
    SetPosY : (self : Button, y : number) -> (),
    GetWorldPos : (self : Button) -> vec,
    GetWorldPosX : (self : Button) -> number,
    GetWorldPosY : (self : Button) -> number,
    SetWorldPos : (self : Button, position : vec) -> (),
    SetWorldPosX : (self : Button, x : number) -> (),
    SetWorldPosY : (self : Button, y : number) -> (),
    SetOnMouseDown : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnMouseUp : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnMouseHeld : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnMouseScroll : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnHoverBegin : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnHoverEnd : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnHoverHeld : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnFocusBegin : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnFocusEnd : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnFocusHeld : (self : Button, callback : MouseEventCallback?) -> (),
    SetOnKeyboard : (self : Button, callback : KeyboardEventCallback?) -> (),
    ForceRefresh : (self : Button) -> (),
}

export type ButtonAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options?) -> Button,
}

local buttonAPI : ButtonAPI = { };
local DEFAULT_BASE_COLOR = vec.New(0.095, 0.105, 0.12);
local DEFAULT_HOVER_COLOR = vec.New(0.13, 0.20, 0.29);
local DEFAULT_BASE_ALPHA = 1.0;
local DEFAULT_HOVER_ALPHA = 1.0;
local DEFAULT_DISABLED_ALPHA = 0.08;

function buttonAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options?) : Button
    local opts = options or { };
    local root = parent:NewPanel(position, size, layer, opts.panelTemplate or "DefaultButtonPanel");
    local text = root:NewText(opts.text or "", vec.New(0, 0), 0i, opts.textTemplate or "DefaultButtonText");
    local textAlign = opts.textAlign or "center";
    local textPadding = opts.textPadX or 6;
    local usesDefaultVisualStyle = opts.panelTemplate == nil or opts.panelTemplate == "DefaultButtonPanel";

    if textAlign == "left" then
        text:SetAnchor(vec.New(0.0, 0.5));
        text:SetRelativePoint(vec.New(0.0, 0.5));
        text:SetPos(vec.New(textPadding, 0));
    elseif textAlign == "right" then
        text:SetAnchor(vec.New(1.0, 0.5));
        text:SetRelativePoint(vec.New(1.0, 0.5));
        text:SetPos(vec.New(-textPadding, 0));
    else
        text:SetAnchor(vec.New(0.5, 0.5));
        text:SetRelativePoint(vec.New(0.5, 0.5));
        text:SetPos(vec.New(0, 0));
    end

    local self = {
        root = root,
        text = text,
        _baseColor = opts.color or (if usesDefaultVisualStyle then DEFAULT_BASE_COLOR else nil),
        _hoverColor = opts.hoverColor or opts.color or (if usesDefaultVisualStyle then DEFAULT_HOVER_COLOR else nil),
        _highlightColor = opts.highlightColor or opts.hoverColor or opts.color or (if usesDefaultVisualStyle then DEFAULT_HOVER_COLOR else nil),
        _baseAlpha = opts.alpha or (if usesDefaultVisualStyle then DEFAULT_BASE_ALPHA else nil),
        _hoverAlpha = opts.hoverAlpha or (if usesDefaultVisualStyle then DEFAULT_HOVER_ALPHA else nil),
        _highlightAlpha = opts.highlightAlpha or opts.hoverAlpha or (if usesDefaultVisualStyle then DEFAULT_HOVER_ALPHA else nil),
        _disabledAlpha = opts.disabledAlpha or (if usesDefaultVisualStyle then DEFAULT_DISABLED_ALPHA else nil),
        _highlighted = false,
        _hovered = false,
        _interactable = true,
        _onHoverBegin = nil,
        _onHoverEnd = nil,
    } :: any;
    RootedWidget.Attach(self);

    if opts.color ~= nil then root:SetColor(self._baseColor); end
    if opts.alpha ~= nil then root:SetAlpha(self._baseAlpha); end
    if opts.borderColor ~= nil then root:SetBorderColor(opts.borderColor); end
    if opts.borderSize ~= nil then root:SetBorderSize(opts.borderSize); end

    local function ApplyVisualState()
        if not self._interactable then
            if self._baseColor ~= nil then root:SetColor(self._baseColor); end
            if self._disabledAlpha ~= nil then root:SetAlpha(self._disabledAlpha); end
            text:SetAlpha(0.4);
        elseif self._highlighted then
            if self._highlightColor ~= nil then root:SetColor(self._highlightColor); end
            if self._highlightAlpha ~= nil then root:SetAlpha(self._highlightAlpha); end
            text:SetAlpha(1.0);
        elseif self._hovered then
            if self._hoverColor ~= nil then root:SetColor(self._hoverColor); end
            if self._hoverAlpha ~= nil then root:SetAlpha(self._hoverAlpha); end
            text:SetAlpha(1.0);
        else
            if self._baseColor ~= nil then root:SetColor(self._baseColor); end
            if self._baseAlpha ~= nil then root:SetAlpha(self._baseAlpha); end
            text:SetAlpha(1.0);
        end
    end

    root:SetOnHoverBegin(function(...)
        self._hovered = true;
        ApplyVisualState();
        if self._onHoverBegin ~= nil then self._onHoverBegin(...); end
    end);
    root:SetOnHoverEnd(function(...)
        self._hovered = false;
        ApplyVisualState();
        if self._onHoverEnd ~= nil then self._onHoverEnd(...); end
    end);

    function self:SetText(value : string) text:SetText(value); end
    function self:SetFontSize(value : number) text:SetFontSize(value); end
    function self:SetEnabled(value : boolean) root:SetEnabled(value); end
    function self:SetVisible(value : boolean) root:SetVisible(value); end
    function self:SetFocusable(value : boolean) root:SetFocusable(value); end
    function self:SetSize(sizeValue : vec) root:SetSize(sizeValue); end
    function self:SetInteractable(value : boolean)
        self._interactable = value;
        if not value then self._hovered = false; end
        root:SetInteractable(value);
        ApplyVisualState();
    end
    function self:SetVisualEnabled(value : boolean)
        self._interactable = value;
        if not value then self._hovered = false; end
        ApplyVisualState();
    end
    function self:SetHighlighted(value : boolean)
        self._highlighted = value;
        ApplyVisualState();
    end
    function self:NewWidget(positionValue : vec, childLayer : integer) return root:NewWidget(positionValue, childLayer); end
    function self:NewPanel(positionValue : vec, sizeValue : vec, childLayer : integer, template : string?) return root:NewPanel(positionValue, sizeValue, childLayer, template); end
    function self:NewText(value : string, positionValue : vec, childLayer : integer, template : string) return root:NewText(value, positionValue, childLayer, template); end
    function self:GetAnchor() return root:GetAnchor(); end
    function self:GetRelativePoint() return root:GetRelativePoint(); end
    function self:SetAnchor(anchorValue : vec) root:SetAnchor(anchorValue); end
    function self:SetRelativePoint(pointValue : vec) root:SetRelativePoint(pointValue); end
    function self:GetPos() return root:GetPos(); end
    function self:GetPosX() return root:GetPosX(); end
    function self:GetPosY() return root:GetPosY(); end
    function self:SetPos(positionValue : vec) root:SetPos(positionValue); end
    function self:SetPosX(x : number) root:SetPosX(x); end
    function self:SetPosY(y : number) root:SetPosY(y); end
    function self:GetWorldPos() return root:GetWorldPos(); end
    function self:GetWorldPosX() return root:GetWorldPosX(); end
    function self:GetWorldPosY() return root:GetWorldPosY(); end
    function self:SetWorldPos(positionValue : vec) root:SetWorldPos(positionValue); end
    function self:SetWorldPosX(x : number) root:SetWorldPosX(x); end
    function self:SetWorldPosY(y : number) root:SetWorldPosY(y); end
    function self:SetOnMouseDown(callback : MouseEventCallback?) root:SetOnMouseDown(callback); end
    function self:SetOnMouseUp(callback : MouseEventCallback?) root:SetOnMouseUp(callback); end
    function self:SetOnMouseHeld(callback : MouseEventCallback?) root:SetOnMouseHeld(callback); end
    function self:SetOnMouseScroll(callback : MouseEventCallback?) root:SetOnMouseScroll(callback); end
    function self:SetOnHoverBegin(callback : MouseEventCallback?) self._onHoverBegin = callback; end
    function self:SetOnHoverEnd(callback : MouseEventCallback?) self._onHoverEnd = callback; end
    function self:SetOnHoverHeld(callback : MouseEventCallback?) root:SetOnHoverHeld(callback); end
    function self:SetOnFocusBegin(callback : MouseEventCallback?) root:SetOnFocusBegin(callback); end
    function self:SetOnFocusEnd(callback : MouseEventCallback?) root:SetOnFocusEnd(callback); end
    function self:SetOnFocusHeld(callback : MouseEventCallback?) root:SetOnFocusHeld(callback); end
    function self:SetOnKeyboard(callback : KeyboardEventCallback?) root:SetOnKeyboard(callback); end
    function self:ForceRefresh() root:ForceRefresh(); text:ForceRefresh(); end

    return self :: Button;
end

return buttonAPI;
