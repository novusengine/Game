--[[
Hierarchy — Lua port of the C++ ImGui Hierarchy. A filterable, scrollable list of
every named entity, virtualized via VirtualList (only visible rows are realized, so
cost is bounded by the viewport, not the entity count). Clicking a row selects it
(shared with the Inspector via EditorSelection); double-clicking centers the camera on it.

Layer 1100 sits above the dev top bar (1000) so the window draws over the bar.
]]

local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local InputBox = require("@src/API/UI/InputBox")
local VirtualList = require("@src/API/UI/VirtualList")
local Selection = require("@src/API/UI/EditorSelection")

require("@src/API/UI/MenuBar")

local PANEL_W        = 300
local TITLE_H        = 16
local ROW_H          = 16
local FILTER_H       = 20
local SIDE_PAD       = 6
local TOP_PAD        = 2
local WINDOW_H       = 460
local LAYER          = 1100
local BTN_TEXT_TPL   = "DevToolsText"
local DOUBLE_CLICK_S = 0.4

local WINDOW_BG_COLOR = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR    = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE     = 1
local SELECTED_COLOR  = vector.create(0.30, 0.55, 0.85)
local SELECTED_ALPHA  = 0.55

local LIST_INNER_W = PANEL_W - SIDE_PAD * 2
local LIST_H = WINDOW_H - TITLE_H - FILTER_H - TOP_PAD * 2 - 4

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("HierarchyEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(1280, -80, PANEL_W, WINDOW_H, LAYER, "DebugDarkGrey")
windowPanel:SetAnchor(0.0, 1.0)
windowPanel:SetRelativePoint(0.0, 1.0)
windowPanel:SetVisible(false)
windowPanel:SetColor(WINDOW_BG_COLOR)
windowPanel:SetAlpha(WINDOW_BG_ALPHA)
windowPanel:SetBorderColor(BORDER_COLOR)
windowPanel:SetBorderSize(BORDER_SIZE)

local titleBar = windowPanel:NewPanel(0, 0, PANEL_W, TITLE_H, 1, "DebugDarkGrey")
titleBar:SetAnchor(0.0, 1.0)
titleBar:SetRelativePoint(0.0, 1.0)
titleBar:SetColor(vector.create(0.06, 0.06, 0.08))
titleBar:SetAlpha(WINDOW_BG_ALPHA)

local titleText = titleBar:NewText("Hierarchy", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
titleText:SetInteractable(false)

local CLOSE_BTN_SIZE = 12
local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)

local refreshBtn = EditorButton.NewEditorButton(titleBar, -CLOSE_BTN_SIZE - 6, 0, 50, CLOSE_BTN_SIZE, 1, MakeBtnOpts("Refresh"))
refreshBtn:SetAnchor(1.0, 0.5)
refreshBtn:SetRelativePoint(1.0, 0.5)

windowPanel:MakeDraggable(titleBar)

local filterInput = InputBox.NewInputBox(windowPanel, SIDE_PAD, -(TITLE_H + TOP_PAD), LIST_INNER_W, FILTER_H, 1, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
filterInput:SetAnchor(0.0, 1.0)
filterInput:SetRelativePoint(0.0, 1.0)

local allEntities = nil
local lastClickID = nil
local lastClickTime = 0

local function ContainsCI(haystack, needle)
    if needle == nil or #needle == 0 then return true end
    return string.find(string.lower(haystack), string.lower(needle), 1, true) ~= nil
end

local function PaintRow(row, selectedID)
    -- Set the full state both ways; SetHighlighted(false) restores the base look (used when a
    -- previously-selected row is repainted after the selection moves elsewhere).
    if row.boundItem ~= nil and row.boundItem.id == selectedID then
        row.panel:SetColor(SELECTED_COLOR)
        row.panel:SetAlpha(SELECTED_ALPHA)
        row:SetHighlighted(true)
    else
        row:SetHighlighted(false)
    end
end

-- Rows are recycled: created once per pool slot, then re-bound to different entities as the window
-- slides. The click handler reads btn.boundItem (set by BindRow), never a captured item.
local function CreateRow(parent)
    local btn = EditorButton.NewEditorButton(parent, 2, 0, LIST_INNER_W - 14, ROW_H, 1, {
        text = "",
        textTemplate = BTN_TEXT_TPL,
        textAlign = "left",
        textPadX = 4,
    })
    btn:SetAnchor(0.0, 1.0)
    btn:SetRelativePoint(0.0, 1.0)

    btn.panel:SetOnMouseUp(function(_, _, button)
        if button ~= 0 then return end

        local item = btn.boundItem
        if item == nil then return end

        local now = os.clock()
        if lastClickID == item.id and (now - lastClickTime) <= DOUBLE_CLICK_S then
            Scene.CenterCameraOnEntity(item.id)
            lastClickID = nil
        else
            lastClickID = item.id
        end
        lastClickTime = now
        Selection.Set(item.id)
    end)

    return btn
end

local function BindRow(row, index, item)
    row.boundItem = item
    row:SetText(item.name)
    PaintRow(row, Selection.Get())
end

local list = VirtualList.NewVirtualList(windowPanel, SIDE_PAD, -(TITLE_H + TOP_PAD + FILTER_H + 4), LIST_INNER_W, LIST_H, 1, {
    rowHeight = ROW_H,
    scrollBox = {
        panelTemplate = "DebugDarkGrey",
        contentSizeX = LIST_INNER_W,
        contentSizeY = LIST_H,
        verticalScrollBar = true,
        scrollBarWidth = 10,
        alpha = 0.0,
    },
    createRow = CreateRow,
    bindRow = BindRow,
})
list:SetAnchor(0.0, 1.0)
list:SetRelativePoint(0.0, 1.0)

local function FilteredEntities(filterText)
    local out = {}
    if allEntities ~= nil then
        for _, e in allEntities do
            if ContainsCI(e.name, filterText) then
                table.insert(out, e)
            end
        end
    end
    return out
end

local function Refresh()
    allEntities = Scene.GetEntities()
    list:SetItems(FilteredEntities(filterInput.text))
end

filterInput:SetOnTextChanged(function(t)
    list:SetItems(FilteredEntities(t.text))
end)

refreshBtn:SetOnMouseUp(function() Refresh() end)

Selection.Subscribe(function() list:Refresh() end)

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    Refresh()
end

function methods:Hide()
    windowPanel:SetVisible(false)
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("Hierarchy", methods)
