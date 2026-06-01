--[[
CameraEditor — Lua port of the C++ ImGui Camera Info window. Three sections
(Transform, Map Position, Saves) using the Collapsible widget; the saves list
lives in a ScrollBox so it can grow without resizing the window.

Layer 1100 sits above the dev top bar (1000) so the window and dropdowns draw
over the bar.
]]

local Collapsible = require("@src/API/UI/Collapsible")
local ContextMenu = require("@src/API/UI/ContextMenu")
local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")

require("@src/API/UI/MenuBar")

local PANEL_W       = 280
local TITLE_H       = 16
local ROW_H         = 14
local BUTTON_H      = 20
local SIDE_PAD      = 6
local SECTION_PAD   = 2
local BOTTOM_PAD    = 6
local SAVES_LIST_H  = 100
local LAYER         = 1100
local BTN_TEXT_TPL  = "DevToolsText"

local WINDOW_BG_COLOR  = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA  = 0.96
local BORDER_COLOR     = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE      = 1
local LABEL_COLOR_TINT = vector.create(0.85, 0.85, 0.90)
local SELECTED_COLOR   = vector.create(0.30, 0.55, 0.85)
local SELECTED_ALPHA   = 0.55

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("CameraEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(700, -80, PANEL_W, TITLE_H, LAYER, "DebugDarkGrey")
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

local titleText = titleBar:NewText("Camera Info", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
-- Block the label from eating the title-bar drag input.
titleText:SetInteractable(false)

local CLOSE_BTN_SIZE = 12
local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)

windowPanel:MakeDraggable(titleBar)

local function FormatVec3(v)
    return string.format("(%.2f, %.2f, %.2f)", v.x, v.y, v.z)
end

local transformSection = Collapsible.NewCollapsible(windowPanel, 0, -TITLE_H, PANEL_W, 1, {
    title = "Transform",
    expanded = true,
})

local posText      = transformSection.body:NewText("Pos: -", SIDE_PAD, -2, 0, BTN_TEXT_TPL)
posText:SetAnchor(0.0, 1.0)
posText:SetRelativePoint(0.0, 1.0)
posText:SetColor(LABEL_COLOR_TINT)
posText:SetInteractable(false)

local rotText      = transformSection.body:NewText("Rot: -", SIDE_PAD, -2 - ROW_H, 0, BTN_TEXT_TPL)
rotText:SetAnchor(0.0, 1.0)
rotText:SetRelativePoint(0.0, 1.0)
rotText:SetColor(LABEL_COLOR_TINT)
rotText:SetInteractable(false)

local fwdText      = transformSection.body:NewText("F: -", SIDE_PAD, -2 - ROW_H * 2, 0, BTN_TEXT_TPL)
fwdText:SetAnchor(0.0, 1.0)
fwdText:SetRelativePoint(0.0, 1.0)
fwdText:SetColor(LABEL_COLOR_TINT)
fwdText:SetInteractable(false)

local rightText    = transformSection.body:NewText("R: -", SIDE_PAD, -2 - ROW_H * 3, 0, BTN_TEXT_TPL)
rightText:SetAnchor(0.0, 1.0)
rightText:SetRelativePoint(0.0, 1.0)
rightText:SetColor(LABEL_COLOR_TINT)
rightText:SetInteractable(false)

local upText       = transformSection.body:NewText("U: -", SIDE_PAD, -2 - ROW_H * 4, 0, BTN_TEXT_TPL)
upText:SetAnchor(0.0, 1.0)
upText:SetRelativePoint(0.0, 1.0)
upText:SetColor(LABEL_COLOR_TINT)
upText:SetInteractable(false)

transformSection:SetBodyContentHeight(ROW_H * 5)

local mapSection = Collapsible.NewCollapsible(windowPanel, 0, 0, PANEL_W, 1, {
    title = "Map Position",
    expanded = true,
})

local chunkText = mapSection.body:NewText("Chunk -", SIDE_PAD, -2, 0, BTN_TEXT_TPL)
chunkText:SetAnchor(0.0, 1.0)
chunkText:SetRelativePoint(0.0, 1.0)
chunkText:SetColor(LABEL_COLOR_TINT)
chunkText:SetInteractable(false)

local cellText  = mapSection.body:NewText("Cell -", SIDE_PAD, -2 - ROW_H, 0, BTN_TEXT_TPL)
cellText:SetAnchor(0.0, 1.0)
cellText:SetRelativePoint(0.0, 1.0)
cellText:SetColor(LABEL_COLOR_TINT)
cellText:SetInteractable(false)

local patchText = mapSection.body:NewText("Patch -", SIDE_PAD, -2 - ROW_H * 2, 0, BTN_TEXT_TPL)
patchText:SetAnchor(0.0, 1.0)
patchText:SetRelativePoint(0.0, 1.0)
patchText:SetColor(LABEL_COLOR_TINT)
patchText:SetInteractable(false)

mapSection:SetBodyContentHeight(ROW_H * 3)

local savesSection = Collapsible.NewCollapsible(windowPanel, 0, 0, PANEL_W, 1, {
    title = "Saves",
    expanded = true,
})

local savesBody = savesSection.body
local LIST_PAD_TOP = 2
local LIST_INNER_W = PANEL_W - SIDE_PAD * 2

local savesList = ScrollBox.NewScrollBox(savesBody, SIDE_PAD, -LIST_PAD_TOP, LIST_INNER_W, SAVES_LIST_H, 1, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = LIST_INNER_W,
    contentSizeY = SAVES_LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
savesList:SetAnchor(0.0, 1.0)
savesList:SetRelativePoint(0.0, 1.0)

local selectedSaveName = nil
local saveRowBtns = {}
local lastSaveClickName = nil
local lastSaveClickTime = 0
local DOUBLE_CLICK_S = 0.4

local gotoBtn   = EditorButton.NewEditorButton(savesBody, SIDE_PAD, -(LIST_PAD_TOP + SAVES_LIST_H + 4), (LIST_INNER_W - 4) / 2, BUTTON_H, 1, MakeBtnOpts("Goto"))
gotoBtn:SetAnchor(0.0, 1.0)
gotoBtn:SetRelativePoint(0.0, 1.0)

local deleteBtn = EditorButton.NewEditorButton(savesBody, -SIDE_PAD, -(LIST_PAD_TOP + SAVES_LIST_H + 4), (LIST_INNER_W - 4) / 2, BUTTON_H, 1, MakeBtnOpts("Delete"))
deleteBtn:SetAnchor(1.0, 1.0)
deleteBtn:SetRelativePoint(1.0, 1.0)

local INPUT_H = BUTTON_H
local ADD_BTN_W = 50
local INPUT_W = LIST_INNER_W - ADD_BTN_W - 4
local inputY = -(LIST_PAD_TOP + SAVES_LIST_H + 4 + BUTTON_H + 4)

local nameInput = InputBox.NewInputBox(savesBody, SIDE_PAD, inputY, INPUT_W, INPUT_H, 1, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
nameInput:SetAnchor(0.0, 1.0)
nameInput:SetRelativePoint(0.0, 1.0)

local addBtn = EditorButton.NewEditorButton(savesBody, -SIDE_PAD, inputY, ADD_BTN_W, BUTTON_H, 1, MakeBtnOpts("Add"))
addBtn:SetAnchor(1.0, 1.0)
addBtn:SetRelativePoint(1.0, 1.0)

local SAVES_BODY_H = LIST_PAD_TOP + SAVES_LIST_H + 4 + BUTTON_H + 4 + INPUT_H + 4
savesSection:SetBodyContentHeight(SAVES_BODY_H)

local function Restack()
    local y = -TITLE_H
    transformSection:SetPosY(y)
    y -= transformSection:GetTotalHeight() + SECTION_PAD
    mapSection:SetPosY(y)
    y -= mapSection:GetTotalHeight() + SECTION_PAD
    savesSection:SetPosY(y)
    y -= savesSection:GetTotalHeight() + BOTTOM_PAD
    windowPanel:SetSize(PANEL_W, -y)
end

transformSection._onToggle = function() Restack() end
mapSection._onToggle       = function() Restack() end
savesSection._onToggle     = function() Restack() end

Restack()

local function RefreshTransform()
    local pos     = Camera.GetPosition()
    local pitch   = Camera.GetPitch()
    local yaw     = Camera.GetYaw()
    local roll    = Camera.GetRoll()
    local forward = Camera.GetForward()
    local right   = Camera.GetRight()
    local up      = Camera.GetUp()

    posText:SetText(string.format("Pos: %s", FormatVec3(pos)))
    rotText:SetText(string.format("Pitch %.2f  Yaw %.2f  Roll %.2f", pitch, yaw, roll))
    fwdText:SetText("F: " .. FormatVec3(forward))
    rightText:SetText("R: " .. FormatVec3(right))
    upText:SetText("U: " .. FormatVec3(up))

    local info = Camera.GetMapInfo()
    chunkText:SetText(string.format("Chunk #%d  (%.3f, %.3f)", info.chunkID, info.chunkX, info.chunkY))
    cellText:SetText(string.format("Cell #%d  (%.3f, %.3f)", info.cellID, info.cellX, info.cellY))
    patchText:SetText(string.format("Patch #%d  (%.3f, %.3f)", info.patchID, info.patchX, info.patchY))
end

local function UpdateRowSelection()
    for _, row in saveRowBtns do
        if row.name == selectedSaveName then
            row.panel:SetColor(SELECTED_COLOR)
            row.panel:SetAlpha(SELECTED_ALPHA)
            row:SetHighlighted(true)
        else
            row:SetHighlighted(false)
        end
    end
end

local rowMenu = ContextMenu.New(canvas, { items = {}, layer = LAYER + 100 })

local RebuildSavesList -- forward declaration; OpenSaveMenu's actions rebuild the list

local function OpenSaveMenu(name, mouseX, mouseY)
    local isStartup = Camera.GetStartupSave() == name
    rowMenu:SetItems({
        { label = "Goto", onClick = function() Camera.LoadSave(name) end },
        {
            label = isStartup and "Clear Goto on Start" or "Set Goto on Start",
            onClick = function()
                Camera.SetStartupSave(isStartup and "" or name)
                RebuildSavesList()
            end,
        },
        { divider = true },
        {
            label = "Delete",
            onClick = function()
                if Camera.GetStartupSave() == name then Camera.SetStartupSave("") end
                if selectedSaveName == name then selectedSaveName = nil end
                Camera.DeleteSave(name) -- fires OnSavesChanged -> RebuildSavesList
            end,
        },
    })
    rowMenu:Open(mouseX, mouseY)
end

RebuildSavesList = function()
    for _, row in saveRowBtns do
        UI.DestroyWidget(row.panel)
    end
    saveRowBtns = {}

    local names = Camera.GetSaveNames()
    local startupSave = Camera.GetStartupSave()
    local rowHeight = 16
    local content = savesList.content

    local stillSelected = false
    for i, name in names do
        -- "* " marks the save that auto-goto's on startup.
        local label = (name == startupSave and "* " or "") .. name
        local btn = EditorButton.NewEditorButton(content, 2, -((i - 1) * rowHeight) - 1, LIST_INNER_W - 14, rowHeight, 1, {
            text = label,
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
        })
        btn:SetAnchor(0.0, 1.0)
        btn:SetRelativePoint(0.0, 1.0)
        btn.name = name
        btn:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
            if button == 1 then
                selectedSaveName = name
                UpdateRowSelection()
                OpenSaveMenu(name, mouseX, mouseY)
                return
            end

            local now = os.clock()
            if lastSaveClickName == name and (now - lastSaveClickTime) <= DOUBLE_CLICK_S then
                Camera.LoadSave(name)
                lastSaveClickName = nil
            else
                lastSaveClickName = name
            end
            lastSaveClickTime = now
            selectedSaveName = name
            UpdateRowSelection()
        end)
        table.insert(saveRowBtns, btn)
        if name == selectedSaveName then stillSelected = true end
    end

    if not stillSelected then
        selectedSaveName = nil
    end

    local contentH = math.max(SAVES_LIST_H, #names * rowHeight + 2)
    savesList:SetContentHeight(contentH)
    UpdateRowSelection()
end

gotoBtn:SetOnMouseUp(function()
    if selectedSaveName ~= nil then
        Camera.LoadSave(selectedSaveName)
    end
end)

deleteBtn:SetOnMouseUp(function()
    if selectedSaveName ~= nil then
        Camera.DeleteSave(selectedSaveName)
        selectedSaveName = nil
    end
end)

addBtn:SetOnMouseUp(function()
    local name = nameInput.text or ""
    if #name == 0 then return end
    if Camera.AddSave(name) then
        nameInput:SetText("")
    end
end)

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    RefreshTransform()
    RebuildSavesList()
    Camera.SetOnTransformChanged(RefreshTransform)
    Camera.SetOnSavesChanged(RebuildSavesList)
end

function methods:Hide()
    windowPanel:SetVisible(false)
    rowMenu:Close()
    Camera.SetOnTransformChanged(nil)
    Camera.SetOnSavesChanged(nil)
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("CameraInfo", methods)
