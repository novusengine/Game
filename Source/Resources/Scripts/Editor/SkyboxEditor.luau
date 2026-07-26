--[[
SkyboxEditor -- Lua port of the C++ ImGui Skybox selector, styled like the Map editor: a List collapsible
(filter + scroll list) and an Actions collapsible (Load/Unload), in an auto-sized window. Left-click selects
a row, double-click (or Actions > Load, or right-click > Load) loads it; the loaded skybox is highlighted
green and named in the "Current:" line.

Data goes through the dev-only Skybox handler (Skybox.Enumerate / GetCurrent / Load / Unload). Skybox.Load
is an async model load, so GetCurrent lags a few frames after a load; rather than poll, we track the current
path optimistically at the click site (the only thing that changes it) and reconcile with GetCurrent on Show.

Layer 1100 sits above the dev top bar (1000).
]]

local Collapsible = require("@src/API/UI/Collapsible")
local ContextMenu = require("@src/API/UI/ContextMenu")
local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")

require("@src/API/UI/MenuBar")

local PANEL_W       = 300
local TITLE_H       = 16
local CURRENT_H     = 16
local ROW_H         = 18
local BUTTON_H      = 20
local SIDE_PAD      = 6
local SECTION_PAD   = 2
local BOTTOM_PAD    = 6
local LIST_H        = 200
local FILTER_H      = 20
local LAYER         = 1100
local CONTEXT_LAYER = LAYER + 50
local BTN_TEXT_TPL  = "DevToolsText"
local DOUBLE_CLICK_S = 0.4

local WINDOW_BG_COLOR     = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA     = 0.96
local BORDER_COLOR        = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE         = 1
local LABEL_TINT          = vector.create(0.82, 0.82, 0.88)
local SELECTED_COLOR      = vector.create(0.30, 0.55, 0.85)
local SELECTED_ALPHA      = 0.55
local CURRENT_COLOR       = vector.create(0.30, 0.85, 0.55)
local CURRENT_ALPHA       = 0.45
local DEFAULT_HOVER_COLOR = vector.create(0.30, 0.55, 0.85)
local DEFAULT_HOVER_ALPHA = 0.45

local LIST_INNER_W = PANEL_W - SIDE_PAD * 2

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("SkyboxEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(620, -140, PANEL_W, TITLE_H + CURRENT_H, LAYER, "DebugDarkGrey")
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

local titleText = titleBar:NewText("Skybox", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
titleText:SetInteractable(false)

local CLOSE_BTN_SIZE = 12
local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)

windowPanel:MakeDraggable(titleBar)

-- ---- current skybox label (under the title bar) ----
local currentText = windowPanel:NewText("Current: -", SIDE_PAD, -(TITLE_H + 2), 1, BTN_TEXT_TPL)
currentText:SetAnchor(0.0, 1.0)
currentText:SetRelativePoint(0.0, 1.0)
currentText:SetColor(LABEL_TINT)
currentText:SetInteractable(false)

-- ---- List section: filter + scroll list ----
local listSection = Collapsible.NewCollapsible(windowPanel, 0, 0, PANEL_W, 1, {
    title = "List",
    expanded = true,
})
local listBody = listSection.body

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

listSection:SetBodyContentHeight(2 + FILTER_H + 4 + LIST_H + 4)

-- ---- Actions section: Load / Unload ----
local actionsSection = Collapsible.NewCollapsible(windowPanel, 0, 0, PANEL_W, 1, {
    title = "Actions",
    expanded = true,
})
local actionsBody = actionsSection.body
local ACTION_BTN_W = (LIST_INNER_W - 4) / 2

local loadBtn = EditorButton.NewEditorButton(actionsBody, SIDE_PAD, -4, ACTION_BTN_W, BUTTON_H, 1, MakeBtnOpts("Load"))
loadBtn:SetAnchor(0.0, 1.0)
loadBtn:SetRelativePoint(0.0, 1.0)

local unloadBtn = EditorButton.NewEditorButton(actionsBody, -SIDE_PAD, -4, ACTION_BTN_W, BUTTON_H, 1, MakeBtnOpts("Unload"))
unloadBtn:SetAnchor(1.0, 1.0)
unloadBtn:SetRelativePoint(1.0, 1.0)

actionsSection:SetBodyContentHeight(4 + BUTTON_H + 4)

local function Restack()
    local y = -(TITLE_H + CURRENT_H)
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

local skyboxes = {}
local rowBtns = {}
local selectedPath = nil
local currentPath = nil            -- item.path of the loaded skybox (optimistic; set at the click site)
local lastClickPath = nil
local lastClickTime = 0

local function ContainsCI(haystack, needle)
    if needle == nil or #needle == 0 then return true end
    return string.find(string.lower(haystack), string.lower(needle), 1, true) ~= nil
end

-- GetCurrent returns the loaded model's full name; match it back to an enumerated entry so Show can seed
-- the current highlight/label from whatever was already loaded.
local function MatchCurrent(cur)
    if cur == nil then return nil end
    for _, item in skyboxes do
        if item.path == cur or item.name == cur then return item.path end
    end
    for _, item in skyboxes do
        if string.find(cur, item.name, 1, true) then return item.path end
    end
    return nil
end

local function PaintRow(row)
    if row.data.path == currentPath then
        row._hoverColor = CURRENT_COLOR
        row._hoverAlpha = CURRENT_ALPHA
        row:SetHighlighted(true)
    elseif row.data.path == selectedPath then
        row._hoverColor = SELECTED_COLOR
        row._hoverAlpha = SELECTED_ALPHA
        row:SetHighlighted(true)
    else
        row._hoverColor = DEFAULT_HOVER_COLOR
        row._hoverAlpha = DEFAULT_HOVER_ALPHA
        row:SetHighlighted(false)
    end
end

local function RefreshHighlights()
    for _, row in rowBtns do PaintRow(row) end
end

local function RefreshCurrent()
    local name = "-"
    if currentPath ~= nil then
        for _, item in skyboxes do
            if item.path == currentPath then name = item.name break end
        end
    end
    currentText:SetText("Current: " .. name)
    RefreshHighlights()
end

local function LoadPath(path)
    Skybox.Load(path)
    currentPath = path
    RefreshCurrent()
end

local function UnloadCurrent()
    Skybox.Unload()
    currentPath = nil
    RefreshCurrent()
end

local function OpenRowMenu(item, mouseX, mouseY)
    rowMenu:SetItems({
        { label = "Load",   onClick = function() LoadPath(item.path) end },
        { label = "Unload", onClick = function() UnloadCurrent() end, enabled = currentPath ~= nil },
    })
    rowMenu:Open(mouseX, mouseY)
end

local function BuildRows(filterText)
    for _, row in rowBtns do UI.DestroyWidget(row.panel) end
    rowBtns = {}

    local content = scrollBox.content
    local visibleIndex = 0
    for _, item in skyboxes do
        if ContainsCI(item.name, filterText) then
            local btn = EditorButton.NewEditorButton(content, 2, -(visibleIndex * ROW_H) - 1, LIST_INNER_W - 14, ROW_H, 1, {
                text = item.name,
                textTemplate = BTN_TEXT_TPL,
                textAlign = "left",
                textPadX = 6,
            })
            btn:SetAnchor(0.0, 1.0)
            btn:SetRelativePoint(0.0, 1.0)
            btn.data = item

            btn.panel:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
                if button == 1 then
                    selectedPath = item.path
                    RefreshHighlights()
                    OpenRowMenu(item, mouseX, mouseY)
                    return
                end
                local now = os.clock()
                if lastClickPath == item.path and (now - lastClickTime) <= DOUBLE_CLICK_S then
                    LoadPath(item.path)
                    lastClickPath = nil
                else
                    lastClickPath = item.path
                end
                lastClickTime = now
                selectedPath = item.path
                RefreshHighlights()
            end)

            table.insert(rowBtns, btn)
            visibleIndex += 1
        end
    end

    scrollBox:SetContentHeight(math.max(LIST_H, visibleIndex * ROW_H + 2))
    RefreshHighlights()
end

filterInput:SetOnTextChanged(function() BuildRows(filterInput.text) end)

loadBtn:SetOnMouseUp(function()
    if selectedPath ~= nil then LoadPath(selectedPath) end
end)

unloadBtn:SetOnMouseUp(function()
    UnloadCurrent()
end)

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    skyboxes = Skybox.Enumerate() or {}
    currentPath = MatchCurrent(Skybox.GetCurrent())
    BuildRows(filterInput.text)
    RefreshCurrent()
end

function methods:Hide()
    windowPanel:SetVisible(false)
    rowMenu:Close()
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("Skybox", methods)
