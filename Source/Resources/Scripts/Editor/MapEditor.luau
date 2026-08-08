local EditorWindow = require("@src/API/Editor/UI/Window")
--[[
MapEditor — Lua port of the C++ ImGui Map selector. Two sections (List with
filter, Actions). Right-click row opens a ContextMenu with Load/Unload.
]]

local Collapsible = require("@src/API/UI/Collapsible")
local ContextMenu = require("@src/API/UI/ContextMenu")
local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")
local TextUtil = require("@src/API/UI/TextUtil")


local PANEL_W       = 320
local TITLE_H       = 16
local ROW_H         = 18
local ICON_SIZE     = 16
local BUTTON_H      = 20
local SIDE_PAD      = 6
local SECTION_PAD   = 2
local BOTTOM_PAD    = 6
local LIST_H        = 200
local LAYER         = 1100i
local CONTEXT_LAYER = integer.add(LAYER, 50i)
local BTN_TEXT_TPL  = "DevToolsText"
local DOUBLE_CLICK_S = 0.4

local WINDOW_BG_COLOR  = vec.New(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA  = 0.96
local BORDER_COLOR     = vec.New(0.30, 0.30, 0.35)
local BORDER_SIZE      = 1
local SELECTED_COLOR   = vec.New(0.30, 0.55, 0.85)
local SELECTED_ALPHA   = 0.55
local CURRENT_COLOR    = vec.New(0.30, 0.85, 0.55)
local CURRENT_ALPHA    = 0.45
local ROW_COLOR        = vec.New(0.095, 0.105, 0.12)
local ROW_HOVER_COLOR  = vec.New(0.13, 0.20, 0.29)

-- 0=OpenWorld, 1=Dungeon, 2=Raid, 3=Battleground, 4=Arena. Filled in MapSelector.cpp.
local ICON_TEMPLATES = {
    [0i] = "MapIcon_OpenWorld",
    [1i] = "MapIcon_Dungeon",
    [2i] = "MapIcon_Raid",
    [3i] = "MapIcon_Battleground",
    [4i] = "MapIcon_Arena",
}
local DEFAULT_ICON_TEMPLATE = ICON_TEMPLATES[0i]

UI.RegisterPanelTemplate("MapIcon_OpenWorld",    { background = "texture/ui/icons/map.png",        cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Dungeon",      { background = "texture/ui/icons/instance.png",   cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Raid",         { background = "texture/ui/icons/raid.png",       cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Battleground", { background = "texture/ui/icons/pvp.png",        cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Arena",        { background = "texture/ui/icons/pvp.png",        cornerRadius = 0.0 })

UI.RegisterPanelTemplate("MapEditorRowPanel",
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

local canvas = UI.GetCanvas("MapEditor", vec.New(0, 0), vec.New(1920, 1080), false)

local editorWindow = EditorWindow.New(canvas, vec.New(820, -80), vec.New(PANEL_W, TITLE_H), LAYER, { title = "Map" })
local windowPanel = editorWindow.root
local titleBar = editorWindow.titleBar
local titleText = editorWindow.title
local closeBtn = editorWindow.closeButton

local listSection = Collapsible.New(windowPanel, vec.New(0, -TITLE_H), PANEL_W, 1i, {
    title = "List",
    expanded = true,
})

local listBody = listSection.body
local LIST_INNER_W = PANEL_W - SIDE_PAD * 2
local FILTER_H = 20

local filterInput = InputBox.New(listBody, vec.New(SIDE_PAD, -2), vec.New(LIST_INNER_W, FILTER_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
filterInput:SetAnchor(vec.New(0.0, 1.0))
filterInput:SetRelativePoint(vec.New(0.0, 1.0))

local scrollBox = ScrollBox.New(listBody, vec.New(SIDE_PAD, -(2 + FILTER_H + 4)), vec.New(LIST_INNER_W, LIST_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = LIST_INNER_W,
    contentSizeY = LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
scrollBox:SetAnchor(vec.New(0.0, 1.0))
scrollBox:SetRelativePoint(vec.New(0.0, 1.0))

local LIST_BODY_H = 2 + FILTER_H + 4 + LIST_H + 4
listSection:SetBodyContentHeight(LIST_BODY_H)

local actionsSection = Collapsible.New(windowPanel, vec.New(0, 0), PANEL_W, 1i, {
    title = "Actions",
    expanded = true,
})

local actionsBody = actionsSection.body
local ACTION_BTN_W = (LIST_INNER_W - 4) / 2

local loadBtn = Button.New(actionsBody, vec.New(SIDE_PAD, -4), vec.New(ACTION_BTN_W, BUTTON_H), 1i, MakeBtnOpts("Load Map"))
loadBtn:SetAnchor(vec.New(0.0, 1.0))
loadBtn:SetRelativePoint(vec.New(0.0, 1.0))

local unloadBtn = Button.New(actionsBody, vec.New(-SIDE_PAD, -4), vec.New(ACTION_BTN_W, BUTTON_H), 1i, MakeBtnOpts("Unload Map"))
unloadBtn:SetAnchor(vec.New(1.0, 1.0))
unloadBtn:SetRelativePoint(vec.New(1.0, 1.0))

actionsSection:SetBodyContentHeight(4 + BUTTON_H + 4)

local function Restack()
    local y = -TITLE_H
    listSection:SetPosY(y)
    y -= listSection:GetTotalHeight() + SECTION_PAD
    actionsSection:SetPosY(y)
    y -= actionsSection:GetTotalHeight() + BOTTOM_PAD
    windowPanel:SetSize(vec.New(PANEL_W, -y))
end

listSection._onToggle    = function() Restack() end
actionsSection._onToggle = function() Restack() end

Restack()

local rowMenu = ContextMenu.New(canvas, { items = {}, layer = CONTEXT_LAYER })

local allMaps = nil
local rowBtns = {}
local selectedRowID = nil
local lastClickRowID = nil
local lastClickTime = 0

local function PaintRow(row, currentInternalName)
    if row.data.internalName == currentInternalName then
        row.root:SetColor(CURRENT_COLOR)
        row.root:SetAlpha(CURRENT_ALPHA)
        row:SetHighlighted(true)
    elseif row.data.id == selectedRowID then
        row.root:SetColor(SELECTED_COLOR)
        row.root:SetAlpha(SELECTED_ALPHA)
        row:SetHighlighted(true)
    else
        row:SetHighlighted(false)
    end
end

local function RefreshHighlights()
    local current = Map.GetCurrent()
    for _, row in rowBtns do
        PaintRow(row, current)
    end
end

local function OpenRowMenu(rowData, mouseX, mouseY)
    local current = Map.GetCurrent()
    rowMenu:SetItems({
        { label = "Load",   onClick = function() Map.Load(rowData.internalName) end },
        { label = "Unload", onClick = function() Map.Unload() end, enabled = current ~= "" },
    })
    rowMenu:Open(mouseX, mouseY)
end

local function BuildRows(filterText)
    for _, row in rowBtns do
        UI.DestroyWidget(row.root)
    end
    rowBtns = {}

    if allMaps == nil then return end

    local content = scrollBox.content
    local visibleIndex = 0
    for _, mapData in allMaps do
        if TextUtil.ContainsIgnoreCase(mapData.name, filterText) or TextUtil.ContainsIgnoreCase(mapData.internalName, filterText) then
            local btn = Button.New(content, vec.New(2, -(visibleIndex * ROW_H) - 1), vec.New(LIST_INNER_W - 14, ROW_H), 1i, {
                text = mapData.name,
                panelTemplate = "MapEditorRowPanel",
                textTemplate = BTN_TEXT_TPL,
                textAlign = "left",
                textPadX = ICON_SIZE + 8,
                color = ROW_COLOR,
                hoverColor = ROW_HOVER_COLOR,
                highlightColor = ROW_HOVER_COLOR,
                alpha = 1.0,
                hoverAlpha = 1.0,
                highlightAlpha = 1.0,
                borderSize = 0,
            })
            btn:SetAnchor(vec.New(0.0, 1.0))
            btn:SetRelativePoint(vec.New(0.0, 1.0))
            btn.data = mapData

            local iconTemplate = ICON_TEMPLATES[mapData.instanceType] or DEFAULT_ICON_TEMPLATE
            local icon = btn.root:NewPanel(vec.New(4, 0), vec.New(ICON_SIZE, ICON_SIZE), 1i, iconTemplate)
            icon:SetAnchor(vec.New(0.0, 0.5))
            icon:SetRelativePoint(vec.New(0.0, 0.5))
            icon:SetInteractable(false)

            btn.root:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
                if button == 1i then
                    selectedRowID = mapData.id
                    RefreshHighlights()
                    OpenRowMenu(mapData, mouseX, mouseY)
                    return
                end
                local now = os.clock()
                if lastClickRowID == mapData.id and (now - lastClickTime) <= DOUBLE_CLICK_S then
                    Map.Load(mapData.internalName)
                    lastClickRowID = nil
                else
                    lastClickRowID = mapData.id
                end
                lastClickTime = now
                selectedRowID = mapData.id
                RefreshHighlights()
            end)

            table.insert(rowBtns, btn)
            visibleIndex += 1
        end
    end

    scrollBox:SetContentHeight(math.max(LIST_H, visibleIndex * ROW_H + 2))
    RefreshHighlights()
end

filterInput:SetOnTextChanged(function(t)
    BuildRows(t.text)
end)

loadBtn:SetOnMouseUp(function()
    if selectedRowID == nil then return end
    for _, row in rowBtns do
        if row.data.id == selectedRowID then
            Map.Load(row.data.internalName)
            break
        end
    end
end)

unloadBtn:SetOnMouseUp(function()
    Map.Unload()
end)

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    allMaps = Map.GetList()
    BuildRows(filterInput.text)
    Map.SetOnCurrentMapChanged(RefreshHighlights)
end

function methods:Hide()
    windowPanel:SetVisible(false)
    rowMenu:Close()
    Map.SetOnCurrentMapChanged(nil)
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("Map", methods)
