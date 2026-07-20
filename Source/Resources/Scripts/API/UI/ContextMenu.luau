--[[
ContextMenu — right-click context menu. Build a description once with New,
call Open(x, y) on a right-click handler, and the menu spawns at the cursor
with a transparent full-screen shield behind it for click-outside dismissal.

    local menu = ContextMenu.New(canvas, {
        items = {
            { label = "Load",   onClick = function() ... end },
            { label = "Unload", onClick = function() ... end, enabled = false },
            { divider = true },
            { label = "Copy",   onClick = function() ... end },
        },
        layer = 1200,
    })

    panel:SetOnMouseUp(function(_, _, button, mx, my)
        if button == 1 then menu:Open(mx, my) end
    end)

Same shield pattern as MenuBar's dropdown: the shield is a sibling panel
sized larger than any plausible canvas, drawn one layer below the menu, so
clicks outside the menu hit it and dismiss without leaking through to the
widgets behind.
]]

local EditorButton = require("@src/API/UI/EditorButton")

local contextMenuAPI = {}

local ITEM_H            = 18
local ITEM_PAD_X        = 16
local ITEM_MIN_W        = 80
local DIVIDER_H         = 5
local DIVIDER_LINE_H    = 1
local SHIELD_SIZE       = 8192
local DEFAULT_LAYER     = 1200
local TEXT_TEMPLATE     = "DevToolsText"

local BG_COLOR          = vector.create(0.10, 0.10, 0.12)
local BG_ALPHA          = 0.96
local BORDER_COLOR      = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE       = 1
local DIVIDER_COLOR     = vector.create(0.30, 0.30, 0.35)

local function CanvasSize(canvas)
    if canvas.GetSize then
        return canvas:GetSize()
    end
    return 1920, 1080
end

function contextMenuAPI.New(parent, opts)
    opts = opts or {}

    local self = {
        _parent     = parent,
        _items      = opts.items or {},
        _layer      = opts.layer or DEFAULT_LAYER,
        _minWidth   = opts.minWidth or ITEM_MIN_W,
        _menuPanel  = nil,
        _shield     = nil,
    }

    local function CalculateWidth()
        local widest = 0
        for _, it in self._items do
            if not it.divider then
                local w, _ = UI.CalculateTextSize(it.label or "", TEXT_TEMPLATE)
                if w > widest then widest = w end
            end
        end
        return math.max(self._minWidth, math.ceil(widest) + ITEM_PAD_X)
    end

    local function CalculateHeight()
        local h = 0
        for _, it in self._items do
            h += if it.divider then DIVIDER_H else ITEM_H
        end
        return h
    end

    function self:Close()
        if self._menuPanel ~= nil then
            UI.DestroyWidget(self._menuPanel)
            self._menuPanel = nil
        end
        if self._shield ~= nil then
            UI.DestroyWidget(self._shield)
            self._shield = nil
        end
    end

    function self:SetItems(newItems)
        self._items = newItems or {}
        if self._menuPanel ~= nil then
            -- Currently open — rebuild at the previous position.
            local x, y = self._menuPanel:GetPos()
            self:Close()
            self:Open(x, y)
        end
    end

    function self:Open(x, y)
        if self._menuPanel ~= nil then
            self:Close()
        end

        local width = CalculateWidth()
        local height = CalculateHeight()

        local canvasW, canvasH = CanvasSize(self._parent)
        if x + width > canvasW then x = math.max(0, x - width) end
        if y - height < 0 then y = math.min(canvasH, y + height) end

        self._shield = self._parent:NewPanel(0, 0, SHIELD_SIZE, SHIELD_SIZE, self._layer - 1, "DebugDarkGrey")
        self._shield:SetAnchor(0.0, 0.0)
        self._shield:SetRelativePoint(0.0, 0.0)
        self._shield:SetAlpha(0)
        self._shield:SetOnMouseUp(function() self:Close() end)

        -- anchor(0,0)=parent bottom-left, relPoint(0,1)=menu top-left.
        -- With pos=(x,y), the menu's top-left lands at canvas-world (x, y) — cursor position.
        self._menuPanel = self._parent:NewPanel(x, y, width, height, self._layer, "DebugDarkGrey")
        self._menuPanel:SetAnchor(0.0, 0.0)
        self._menuPanel:SetRelativePoint(0.0, 1.0)
        self._menuPanel:SetColor(BG_COLOR)
        self._menuPanel:SetAlpha(BG_ALPHA)
        self._menuPanel:SetBorderColor(BORDER_COLOR)
        self._menuPanel:SetBorderSize(BORDER_SIZE)
        self._menuPanel:SetClipChildren(true)

        local cursorY = 0
        for _, it in self._items do
            if it.divider then
                local divider = self._menuPanel:NewPanel(0, -cursorY - math.floor((DIVIDER_H - DIVIDER_LINE_H) / 2), width, DIVIDER_LINE_H, 1, "DebugDarkGrey")
                divider:SetAnchor(0.0, 1.0)
                divider:SetRelativePoint(0.0, 1.0)
                divider:SetColor(DIVIDER_COLOR)
                divider:SetAlpha(0.8)
                divider:SetInteractable(false)
                cursorY += DIVIDER_H
            else
                local enabled = it.enabled
                if enabled == nil then enabled = true end

                local item = EditorButton.NewEditorButton(self._menuPanel, 0, -cursorY, width, ITEM_H, 1, {
                    text = it.label or "",
                    textTemplate = TEXT_TEMPLATE,
                    textAlign = "left",
                })
                item:SetAnchor(0.0, 1.0)
                item:SetRelativePoint(0.0, 1.0)
                item:SetHighlighted(it.selected == true)

                if not enabled then
                    item.text:SetAlpha(0.4)
                    item:SetInteractable(false)
                else
                    local cb = it.onClick
                    item:SetOnMouseUp(function()
                        self:Close()
                        if cb ~= nil then cb() end
                    end)
                end
                cursorY += ITEM_H
            end
        end
    end

    return self
end

return contextMenuAPI
