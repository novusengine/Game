--[[
Collapsible — header row with a chevron + title; clicking toggles the body's
visibility. Caller adds children to `collapsible.body` and tells the widget how
tall the content is via `SetBodyContentHeight`. `GetTotalHeight()` returns the
effective height (header alone when collapsed, header+body when expanded) so
callers can stack siblings below.

Anchored top-left so positive contentHeight grows the body downward; matches
the convention used by ClockEditor / CameraEditor windows.

    local s = Collapsible.New(parent, vec.New(posX, posY), width, layer, {
        title = "Transform",
        expanded = true,
        bodyPadding = 4,
        onToggle = function(isExpanded, totalHeight) ... end,
    })
    s.body:NewText(...)
    s:SetBodyContentHeight(rowCount * ROW_H)
]]

local Button = require("@src/API/UI/Button")
local RootedWidget = require("@src/API/UI/RootedWidget")

export type Options =
{
    title : string?,
    expanded : boolean?,
    bodyPadding : number?,
    onToggle : ((boolean, number) -> ())?,
}

export type Collapsible =
{
    root : Panel,
    body : Panel,
    headerBtn : Button.Button,
    SetExpanded : (self : Collapsible, expanded : boolean) -> (),
    IsExpanded : (self : Collapsible) -> boolean,
    SetBodyContentHeight : (self : Collapsible, height : number) -> (),
    GetTotalHeight : (self : Collapsible) -> number,
    SetPosY : (self : Collapsible, y : number) -> (),
}

export type CollapsibleAPI = { New : (parent : Widget, position : vec, width : number, layer : integer, options : Options?) -> Collapsible }
local collapsibleAPI : CollapsibleAPI = {}

local HEADER_H        = 16
local DEFAULT_PADDING = 4
local CHEVRON_OPEN    = "v "
local CHEVRON_CLOSED  = "> "
local BTN_TEXT_TPL    = "DevToolsText"

local HEADER_COLOR        = vec.New(0.18, 0.18, 0.22)
local HEADER_HOVER_COLOR  = vec.New(0.28, 0.40, 0.60)
local HEADER_BASE_ALPHA   = 0.6
local HEADER_HOVER_ALPHA  = 0.85

function collapsibleAPI.New(parent : Widget, position : vec, width : number, layer : integer, opts : Options?)
    opts = opts or {}

    local title       = opts.title or ""
    local expanded    = opts.expanded
    if expanded == nil then expanded = true end
    local bodyPadding = opts.bodyPadding or DEFAULT_PADDING
    local onToggle    = opts.onToggle

    local rootPanel = parent:NewPanel(vec.New(position.x, position.y), vec.New(width, HEADER_H), layer, "DebugDarkGrey")
    rootPanel:SetAnchor(vec.New(0.0, 1.0))
    rootPanel:SetRelativePoint(vec.New(0.0, 1.0))
    rootPanel:SetAlpha(0.0)

    local headerBtn = Button.New(rootPanel, vec.New(0, 0), vec.New(width, HEADER_H), 1i, {
        text = (expanded and CHEVRON_OPEN or CHEVRON_CLOSED) .. title,
        textTemplate = BTN_TEXT_TPL,
        textAlign = "left",
        textPadX = 4,
        color = HEADER_COLOR,
        hoverColor = HEADER_HOVER_COLOR,
        alpha = HEADER_BASE_ALPHA,
        hoverAlpha = HEADER_HOVER_ALPHA,
    })
    headerBtn:SetAnchor(vec.New(0.0, 1.0))
    headerBtn:SetRelativePoint(vec.New(0.0, 1.0))

    local bodyPanel = rootPanel:NewPanel(vec.New(0, -HEADER_H), vec.New(width, 1), 1i, "DebugDarkGrey")
    bodyPanel:SetAnchor(vec.New(0.0, 1.0))
    bodyPanel:SetRelativePoint(vec.New(0.0, 1.0))
    bodyPanel:SetAlpha(0.0)
    bodyPanel:SetVisible(expanded)

    local self = {
        root        = rootPanel,
        body        = bodyPanel,
        headerBtn   = headerBtn,
        _title      = title,
        _expanded   = expanded,
        _bodyHeight = 0,
        _padding    = bodyPadding,
        _width      = width,
        _onToggle   = onToggle,
    }
    RootedWidget.Attach(self)

    local function RefreshHeaderText()
        headerBtn:SetText((self._expanded and CHEVRON_OPEN or CHEVRON_CLOSED) .. self._title)
    end

    function self:GetTotalHeight()
        if self._expanded then
            return HEADER_H + self._bodyHeight
        end
        return HEADER_H
    end

    function self:IsExpanded()
        return self._expanded
    end

    function self:SetBodyContentHeight(h)
        self._bodyHeight = math.max(0, h + self._padding * 2)
        self.body:SetSize(vec.New(self._width, math.max(1, self._bodyHeight)))
        self.root:SetSize(vec.New(self._width, self:GetTotalHeight()))
    end

    function self:SetExpanded(value)
        local newState = value and true or false
        if self._expanded == newState then return end
        self._expanded = newState
        self.body:SetVisible(newState)
        self.root:SetSize(vec.New(self._width, self:GetTotalHeight()))
        RefreshHeaderText()
        if self._onToggle ~= nil then
            self._onToggle(self._expanded, self:GetTotalHeight())
        end
    end

    function self:Toggle()
        self:SetExpanded(not self._expanded)
    end

    function self:SetPos(position)    self.root:SetPos(position) end
    function self:SetPosX(x)          self.root:SetPosX(x) end
    function self:SetPosY(y)          self.root:SetPosY(y) end
    function self:SetAnchor(anchor)      self.root:SetAnchor(anchor) end
    function self:SetRelativePoint(point) self.root:SetRelativePoint(point) end
    function self:SetVisible(v)          self.root:SetVisible(v) end

    headerBtn:SetOnMouseUp(function(_, _, mouseButton)
        if mouseButton == 0i then self:Toggle() end
    end)

    return self
end

return collapsibleAPI
