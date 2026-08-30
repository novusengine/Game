local EditorWindow = require("@src/API/Editor/UI/Window")
--[[
Hierarchy - Lua port of the C++ ImGui Hierarchy. A filterable, scrollable list of
every named entity, virtualized via VirtualList (only visible rows are realized, so
cost is bounded by the viewport, not the entity count). Clicking a row selects it
(shared with the Inspector via EditorSelection); double-clicking centers the camera on it.

Layer 1100 sits above the dev top bar (1000) so the window draws over the bar.
]]

local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local InputBox = require("@src/API/UI/InputBox")
local VirtualList = require("@src/API/UI/VirtualList")
local Selection = require("@src/API/Editor/Selection")
local TextUtil = require("@src/API/UI/TextUtil")


local PANEL_W        = 300
local TITLE_H        = 16
local ROW_H          = 16
local FILTER_H       = 20
local SIDE_PAD       = 6
local TOP_PAD        = 2
local WINDOW_H       = 460
local LAYER          = 1100i
local BTN_TEXT_TPL   = "DevToolsText"
local DOUBLE_CLICK_S = 0.4

local WINDOW_BG_COLOR = vec.New(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR    = vec.New(0.30, 0.30, 0.35)
local BORDER_SIZE     = 1
local SELECTED_COLOR  = vec.New(0.30, 0.55, 0.85)
local SELECTED_ALPHA  = 0.55
local ROW_COLOR       = vec.New(0.095, 0.105, 0.12)
local ROW_HOVER_COLOR = vec.New(0.13, 0.20, 0.29)

local LIST_INNER_W = PANEL_W - SIDE_PAD * 2
local LIST_H = WINDOW_H - TITLE_H - FILTER_H - TOP_PAD * 2 - 4

UI.RegisterPanelTemplate("HierarchyRowPanel",
{
    color = ROW_COLOR,
    alpha = 1.0,
    cornerRadius = 2.0,
    borderSize = 0.0,
    onClickTemplate = "DefaultButtonDownPanel",
    onHoverTemplate = "DefaultButtonHoveredPanel",
    onUninteractableTemplate = "DefaultButtonDisabledPanel",
})

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("HierarchyEditor", vec.New(0, 0), vec.New(1920, 1080), false)

local editorWindow = EditorWindow.New(canvas, vec.New(1280, -80), vec.New(PANEL_W, WINDOW_H), LAYER, { title = "Hierarchy" })
local windowPanel = editorWindow.root
local titleBar = editorWindow.titleBar
local titleText = editorWindow.title
local closeBtn = editorWindow.closeButton
local refreshBtn = Button.New(titleBar, vec.New(-18, 0), vec.New(50, 12), 1i, MakeBtnOpts("Refresh"))
refreshBtn:SetAnchor(vec.New(1.0, 0.5))
refreshBtn:SetRelativePoint(vec.New(1.0, 0.5))

local filterInput = InputBox.New(windowPanel, vec.New(SIDE_PAD, -(TITLE_H + TOP_PAD)), vec.New(LIST_INNER_W, FILTER_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
filterInput:SetAnchor(vec.New(0.0, 1.0))
filterInput:SetRelativePoint(vec.New(0.0, 1.0))

local allEntities = nil
local lastClickID = nil
local lastClickTime = 0

local function PaintRow(row, selectedID)
    row:SetHighlighted(row.boundItem ~= nil and row.boundItem.id == selectedID)
end

-- Rows are recycled: created once per pool slot, then re-bound to different entities as the window
-- slides. The click handler reads btn.boundItem (set by BindRow), never a captured item.
local function CreateRow(parent)
    local btn = Button.New(parent, vec.New(2, 0), vec.New(LIST_INNER_W - 14, ROW_H), 1i, {
        text = "",
        panelTemplate = "HierarchyRowPanel",
        textTemplate = BTN_TEXT_TPL,
        textAlign = "left",
        textPadX = 4,
        color = ROW_COLOR,
        hoverColor = ROW_HOVER_COLOR,
        highlightColor = SELECTED_COLOR,
        alpha = 1.0,
        hoverAlpha = 1.0,
        highlightAlpha = SELECTED_ALPHA,
        borderSize = 0,
    })
    btn:SetAnchor(vec.New(0.0, 1.0))
    btn:SetRelativePoint(vec.New(0.0, 1.0))

    btn.root:SetOnMouseUp(function(_, _, button)
        if button ~= 0i then return end

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

local list = VirtualList.New(windowPanel, vec.New(SIDE_PAD, -(TITLE_H + TOP_PAD + FILTER_H + 4)), vec.New(LIST_INNER_W, LIST_H), 1i, {
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
list:SetAnchor(vec.New(0.0, 1.0))
list:SetRelativePoint(vec.New(0.0, 1.0))

local function FilteredEntities(filterText)
    local out = {}
    if allEntities ~= nil then
        for _, e in allEntities do
            if TextUtil.ContainsIgnoreCase(e.name, filterText) then
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
