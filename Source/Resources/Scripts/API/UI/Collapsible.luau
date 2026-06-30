--[[
Collapsible — header row with a chevron + title; clicking toggles the body's
visibility. Caller adds children to `collapsible.body` and tells the widget how
tall the content is via `SetBodyContentHeight`. `GetTotalHeight()` returns the
effective height (header alone when collapsed, header+body when expanded) so
callers can stack siblings below.

Anchored top-left so positive contentHeight grows the body downward; matches
the convention used by ClockEditor / CameraEditor windows.

    local s = Collapsible.NewCollapsible(parent, posX, posY, width, layer, {
        title = "Transform",
        expanded = true,
        bodyPadding = 4,
        onToggle = function(isExpanded, totalHeight) ... end,
    })
    s.body:NewText(...)
    s:SetBodyContentHeight(rowCount * ROW_H)
]]

local EditorButton = require("@src/API/UI/EditorButton")

local collapsibleAPI = {}

local HEADER_H        = 16
local DEFAULT_PADDING = 4
local CHEVRON_OPEN    = "v "
local CHEVRON_CLOSED  = "> "
local BTN_TEXT_TPL    = "DevToolsText"

local HEADER_COLOR        = vector.create(0.18, 0.18, 0.22)
local HEADER_HOVER_COLOR  = vector.create(0.28, 0.40, 0.60)
local HEADER_BASE_ALPHA   = 0.6
local HEADER_HOVER_ALPHA  = 0.85

function collapsibleAPI.NewCollapsible(parent, posX, posY, width, layer, opts)
    opts = opts or {}

    local title       = opts.title or ""
    local expanded    = opts.expanded
    if expanded == nil then expanded = true end
    local bodyPadding = opts.bodyPadding or DEFAULT_PADDING
    local onToggle    = opts.onToggle

    local rootPanel = parent:NewPanel(posX, posY, width, HEADER_H, layer, "DebugDarkGrey")
    rootPanel:SetAnchor(0.0, 1.0)
    rootPanel:SetRelativePoint(0.0, 1.0)
    rootPanel:SetAlpha(0.0)

    local headerBtn = EditorButton.NewEditorButton(rootPanel, 0, 0, width, HEADER_H, 1, {
        text = (expanded and CHEVRON_OPEN or CHEVRON_CLOSED) .. title,
        textTemplate = BTN_TEXT_TPL,
        textAlign = "left",
        textPadX = 4,
        color = HEADER_COLOR,
        hoverColor = HEADER_HOVER_COLOR,
        alpha = HEADER_BASE_ALPHA,
        hoverAlpha = HEADER_HOVER_ALPHA,
    })
    headerBtn:SetAnchor(0.0, 1.0)
    headerBtn:SetRelativePoint(0.0, 1.0)

    local bodyPanel = rootPanel:NewPanel(0, -HEADER_H, width, 1, 1, "DebugDarkGrey")
    bodyPanel:SetAnchor(0.0, 1.0)
    bodyPanel:SetRelativePoint(0.0, 1.0)
    bodyPanel:SetAlpha(0.0)
    bodyPanel:SetVisible(expanded)

    local self = {
        panel       = rootPanel,
        body        = bodyPanel,
        headerBtn   = headerBtn,
        _title      = title,
        _expanded   = expanded,
        _bodyHeight = 0,
        _padding    = bodyPadding,
        _width      = width,
        _onToggle   = onToggle,
    }

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
        self.body:SetSize(self._width, math.max(1, self._bodyHeight))
        self.panel:SetSize(self._width, self:GetTotalHeight())
    end

    function self:SetExpanded(value)
        local newState = value and true or false
        if self._expanded == newState then return end
        self._expanded = newState
        self.body:SetVisible(newState)
        self.panel:SetSize(self._width, self:GetTotalHeight())
        RefreshHeaderText()
        if self._onToggle ~= nil then
            self._onToggle(self._expanded, self:GetTotalHeight())
        end
    end

    function self:Toggle()
        self:SetExpanded(not self._expanded)
    end

    function self:SetPos(x, y)        self.panel:SetPos(x, y) end
    function self:SetPosX(x)          self.panel:SetPosX(x) end
    function self:SetPosY(y)          self.panel:SetPosY(y) end
    function self:SetAnchor(x, y)        self.panel:SetAnchor(x, y) end
    function self:SetRelativePoint(x, y) self.panel:SetRelativePoint(x, y) end
    function self:SetVisible(v)          self.panel:SetVisible(v) end

    headerBtn:SetOnMouseUp(function() self:Toggle() end)

    return self
end

return collapsibleAPI
