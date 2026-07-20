--[[
MenuBar — horizontal menu bar with click-to-open dropdowns.

Click-outside dismissal uses a transparent full-screen "shield" panel as a
sibling of the bar. Bar buttons and dropdown items still receive their clicks
because they sit deeper in the widget hierarchy than the shield (hierarchy
depth dominates the canvas hit-test ranking).

    local bar = MenuBar.NewMenuBar(parent, 0, -22, 1280, 22, 1000, {
        menus = {
            { label = "Debug", items = {
                { label = "Reload Scripts", onClick = fn, enabled = true },
                { label = "Reload Shaders", enabled = false },           -- greyed
            }},
        },
    })
]]

local EditorButton = require("@src/API/UI/EditorButton")

local menuBarAPI = {}

UI.RegisterTextTemplate("DevToolsText",
{
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = 8.0,
    borderSize = 0.2,
    borderColor = vector.create(0.0, 0.0, 0.0),
})

local TOP_BTN_W       = 56
local TOP_BTN_LAYER   = 1
local TEXT_TEMPLATE   = "DevToolsText"
local ITEM_H          = 18
local ITEM_PAD_X      = 16          -- horizontal padding around the longest label in a dropdown
local ITEM_MIN_W      = 60          -- floor for very short labels
local DROPDOWN_LAYER  = 2
local SHIELD_DRAW_OFFSET = -1
local SHIELD_SIZE     = 8192

local BAR_BG_COLOR        = vector.create(0.10, 0.10, 0.12)
local BAR_BG_ALPHA        = 0.92
local DROPDOWN_BG_COLOR   = vector.create(0.10, 0.10, 0.12)
local DROPDOWN_BG_ALPHA   = 0.96
local BORDER_COLOR        = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE         = 1

local function CloseMenu(self)
    if self._dropdown ~= nil then
        UI.DestroyWidget(self._dropdown)
        self._dropdown = nil
    end
    if self._shield ~= nil then
        UI.DestroyWidget(self._shield)
        self._shield = nil
    end
    if self._openIdx ~= nil then
        self._buttons[self._openIdx]:SetHighlighted(false)
        self._openIdx = nil
    end
end

local function CreateDropdown(self, idx)
    local menuDef = self._menus[idx]
    local items = menuDef.items or {}
    local btn = self._buttons[idx]

    local widest = 0
    for _, it in items do
        local w, _ = UI.CalculateTextSize(it.label or "", TEXT_TEMPLATE)
        if w > widest then widest = w end
    end
    local itemW = math.max(ITEM_MIN_W, math.ceil(widest) + ITEM_PAD_X)

    -- Child of the bar so hierarchy depth ranks dropdown clicks above the shield.
    local dropdownH = #items * ITEM_H
    local btnX = btn:GetPosX()
    local dropdown = self.panel:NewPanel(btnX, 0, itemW, dropdownH, DROPDOWN_LAYER, "DebugDarkGrey")
    dropdown:SetAnchor(0.0, 0.0)
    dropdown:SetRelativePoint(0.0, 1.0)
    dropdown:SetColor(DROPDOWN_BG_COLOR)
    dropdown:SetAlpha(DROPDOWN_BG_ALPHA)
    dropdown:SetBorderColor(BORDER_COLOR)
    dropdown:SetBorderSize(BORDER_SIZE)
    dropdown:SetClipChildren(true)

    for i, it in items do
        local itemBtn = EditorButton.NewEditorButton(dropdown, 0, -((i - 1) * ITEM_H), itemW, ITEM_H, 0, {
            text = it.label or "",
            textTemplate = TEXT_TEMPLATE,
            textAlign = "left",
        })
        itemBtn:SetAnchor(0.0, 1.0)
        itemBtn:SetRelativePoint(0.0, 1.0)

        if it.enabled == false then
            itemBtn.text:SetAlpha(0.4)
            itemBtn:SetInteractable(false)
        else
            local onClick = it.onClick
            itemBtn:SetOnMouseUp(function()
                CloseMenu(self)
                if onClick ~= nil then onClick() end
            end)
        end
    end

    return dropdown
end

local function CreateShield(self)
    local shield = self._parent:NewPanel(0, 0, SHIELD_SIZE, SHIELD_SIZE, self._layer + SHIELD_DRAW_OFFSET, "DebugDarkGrey")
    shield:SetAnchor(0.0, 0.0)
    shield:SetRelativePoint(0.0, 0.0)
    shield:SetAlpha(0)
    shield:SetOnMouseUp(function() CloseMenu(self) end)
    return shield
end

local function OpenDropdown(self, idx)
    if self._openIdx == idx then return end

    if self._dropdown ~= nil then
        UI.DestroyWidget(self._dropdown)
        self._dropdown = nil
    end

    if self._openIdx ~= nil then
        self._buttons[self._openIdx]:SetHighlighted(false)
    end

    self._openIdx = idx
    self._buttons[idx]:SetHighlighted(true)
    self._dropdown = CreateDropdown(self, idx)

    if self._shield == nil then
        self._shield = CreateShield(self)
    end
end

local function ToggleDropdown(self, idx)
    if self._openIdx == idx then
        CloseMenu(self)
    else
        OpenDropdown(self, idx)
    end
end

function menuBarAPI.NewMenuBar(parent, posX, posY, width, height, layer, opts)
    opts = opts or {}

    local barPanel = parent:NewPanel(posX, posY, width, height, layer, "DebugDarkGrey")
    barPanel:SetColor(BAR_BG_COLOR)
    barPanel:SetAlpha(BAR_BG_ALPHA)

    local self = {
        panel        = barPanel,
        _parent      = parent,
        _opts        = opts,
        _menus       = opts.menus or {},
        _buttons     = {},
        _dropdown    = nil,
        _shield      = nil,
        _openIdx     = nil,
        _layer       = layer,
    }

    local paddingLeft = opts.paddingLeft or 4
    local spacing     = opts.spacing or 0

    local cursorX = paddingLeft
    for i, m in self._menus do
        local btn = EditorButton.NewEditorButton(barPanel, cursorX, 0, TOP_BTN_W, height, TOP_BTN_LAYER, {
            text = m.label or "",
            textTemplate = TEXT_TEMPLATE,
        })
        btn:SetAnchor(0.0, 0.0)
        btn:SetRelativePoint(0.0, 0.0)

        btn:SetOnMouseUp(function() ToggleDropdown(self, i) end)
        btn:SetOnHoverBegin(function()
            -- While a dropdown is open, hovering a sibling switches without a click.
            if self._openIdx ~= nil and self._openIdx ~= i then
                OpenDropdown(self, i)
            end
        end)

        self._buttons[i] = btn
        cursorX += TOP_BTN_W + spacing
    end

    -- Forward unknown method calls to .panel so the bar walks like a Widget when used as a parent.
    setmetatable(self, {
        __index = function(t, k)
            local p = rawget(t, "panel")
            if p == nil then return nil end
            local v = p[k]
            if type(v) ~= "function" then return v end
            return function(_, ...) return v(p, ...) end
        end,
    })

    return self
end

return menuBarAPI
