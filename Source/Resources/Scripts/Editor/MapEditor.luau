--[[
MapEditor — Lua port of the C++ ImGui Map selector. Two sections (List with
filter, Actions). Right-click row opens a ContextMenu with Load/Unload.
]]

local Collapsible = require("@src/API/UI/Collapsible")
local ContextMenu = require("@src/API/UI/ContextMenu")
local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")

require("@src/API/UI/MenuBar")

local PANEL_W       = 320
local TITLE_H       = 16
local ROW_H         = 18
local ICON_SIZE     = 14
local BUTTON_H      = 20
local SIDE_PAD      = 6
local SECTION_PAD   = 2
local BOTTOM_PAD    = 6
local LIST_H        = 200
local LAYER         = 1100
local CONTEXT_LAYER = LAYER + 50
local BTN_TEXT_TPL  = "DevToolsText"
local DOUBLE_CLICK_S = 0.4

local WINDOW_BG_COLOR  = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA  = 0.96
local BORDER_COLOR     = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE      = 1
local SELECTED_COLOR   = vector.create(0.30, 0.55, 0.85)
local SELECTED_ALPHA   = 0.55
local CURRENT_COLOR    = vector.create(0.30, 0.85, 0.55)
local CURRENT_ALPHA    = 0.45

-- 0=OpenWorld, 1=Dungeon, 2=Raid, 3=Battleground, 4=Arena. Filled in MapSelector.cpp.
local ICON_TEMPLATES = {
    [0] = "MapIcon_OpenWorld",
    [1] = "MapIcon_Dungeon",
    [2] = "MapIcon_Raid",
    [3] = "MapIcon_Battleground",
    [4] = "MapIcon_Arena",
}
local DEFAULT_ICON_TEMPLATE = ICON_TEMPLATES[0]

UI.RegisterPanelTemplate("MapIcon_OpenWorld",    { background = "Data/Texture/interface/worldmap/worldmap-icon.dds",          cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Dungeon",      { background = "Data/Texture/interface/minimap/dungeon_icon.dds",            cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Raid",         { background = "Data/Texture/interface/minimap/raid_icon.dds",               cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Battleground", { background = "Data/Texture/interface/battlefieldframe/ui-battlefield-icon.dds", cornerRadius = 0.0 })
UI.RegisterPanelTemplate("MapIcon_Arena",        { background = "Data/Texture/interface/calendar/ui-calendar-event-pvp.dds",  cornerRadius = 0.0 })

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("MapEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(820, -80, PANEL_W, TITLE_H, LAYER, "DebugDarkGrey")
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

local titleText = titleBar:NewText("Map", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
titleText:SetInteractable(false)

local CLOSE_BTN_SIZE = 12
local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)

windowPanel:MakeDraggable(titleBar)

local listSection = Collapsible.NewCollapsible(windowPanel, 0, -TITLE_H, PANEL_W, 1, {
    title = "List",
    expanded = true,
})

local listBody = listSection.body
local LIST_INNER_W = PANEL_W - SIDE_PAD * 2
local FILTER_H = 20

local filterInput = InputBox.NewInputBox(listBody, SIDE_PAD, -2, LIST_INNER_W, FILTER_H, 1, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
filterInput:SetAnchor(0.0, 1.0)
filterInput:SetRelativePoint(0.0, 1.0)

local scrollBox = ScrollBox.NewScrollBox(listBody, SIDE_PAD, -(2 + FILTER_H + 4), LIST_INNER_W, LIST_H, 1, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = LIST_INNER_W,
    contentSizeY = LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
scrollBox:SetAnchor(0.0, 1.0)
scrollBox:SetRelativePoint(0.0, 1.0)

local LIST_BODY_H = 2 + FILTER_H + 4 + LIST_H + 4
listSection:SetBodyContentHeight(LIST_BODY_H)

local actionsSection = Collapsible.NewCollapsible(windowPanel, 0, 0, PANEL_W, 1, {
    title = "Actions",
    expanded = true,
})

local actionsBody = actionsSection.body
local ACTION_BTN_W = (LIST_INNER_W - 4) / 2

local loadBtn = EditorButton.NewEditorButton(actionsBody, SIDE_PAD, -4, ACTION_BTN_W, BUTTON_H, 1, MakeBtnOpts("Load Map"))
loadBtn:SetAnchor(0.0, 1.0)
loadBtn:SetRelativePoint(0.0, 1.0)

local unloadBtn = EditorButton.NewEditorButton(actionsBody, -SIDE_PAD, -4, ACTION_BTN_W, BUTTON_H, 1, MakeBtnOpts("Unload Map"))
unloadBtn:SetAnchor(1.0, 1.0)
unloadBtn:SetRelativePoint(1.0, 1.0)

actionsSection:SetBodyContentHeight(4 + BUTTON_H + 4)

local function Restack()
    local y = -TITLE_H
    listSection:SetPosY(y)
    y -= listSection:GetTotalHeight() + SECTION_PAD
    actionsSection:SetPosY(y)
    y -= actionsSection:GetTotalHeight() + BOTTOM_PAD
    windowPanel:SetSize(PANEL_W, -y)
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

local function ContainsCI(haystack, needle)
    if needle == nil or #needle == 0 then return true end
    return string.find(string.lower(haystack), string.lower(needle), 1, true) ~= nil
end

local function PaintRow(row, currentInternalName)
    if row.data.internalName == currentInternalName then
        row.panel:SetColor(CURRENT_COLOR)
        row.panel:SetAlpha(CURRENT_ALPHA)
        row:SetHighlighted(true)
    elseif row.data.id == selectedRowID then
        row.panel:SetColor(SELECTED_COLOR)
        row.panel:SetAlpha(SELECTED_ALPHA)
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
        UI.DestroyWidget(row.panel)
    end
    rowBtns = {}

    if allMaps == nil then return end

    local content = scrollBox.content
    local visibleIndex = 0
    for _, mapData in allMaps do
        if ContainsCI(mapData.name, filterText) or ContainsCI(mapData.internalName, filterText) then
            local btn = EditorButton.NewEditorButton(content, 2, -(visibleIndex * ROW_H) - 1, LIST_INNER_W - 14, ROW_H, 1, {
                text = mapData.name,
                textTemplate = BTN_TEXT_TPL,
                textAlign = "left",
                textPadX = ICON_SIZE + 8,
            })
            btn:SetAnchor(0.0, 1.0)
            btn:SetRelativePoint(0.0, 1.0)
            btn.data = mapData

            local iconTemplate = ICON_TEMPLATES[mapData.instanceType] or DEFAULT_ICON_TEMPLATE
            local icon = btn.panel:NewPanel(4, 0, ICON_SIZE, ICON_SIZE, 1, iconTemplate)
            icon:SetAnchor(0.0, 0.5)
            icon:SetRelativePoint(0.0, 0.5)
            icon:SetInteractable(false)

            btn.panel:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
                if button == 1 then
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
