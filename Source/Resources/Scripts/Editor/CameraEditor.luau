local EditorWindow = require("@src/API/Editor/UI/Window")
--[[
CameraEditor - Lua port of the C++ ImGui Camera Info window. Three sections
(Transform, Map Position, Saves) using the Collapsible widget; the saves list
lives in a ScrollBox so it can grow without resizing the window.

Layer 1100 sits above the dev top bar (1000) so the window and dropdowns draw
over the bar.
]]

local Collapsible = require("@src/API/UI/Collapsible")
local ContextMenu = require("@src/API/UI/ContextMenu")
local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")


local PANEL_W       = 280
local TITLE_H       = 16
local ROW_H         = 14
local BUTTON_H      = 20
local SIDE_PAD      = 6
local SECTION_PAD   = 2
local BOTTOM_PAD    = 6
local SAVES_LIST_H  = 100
local LAYER         = 1100i
local BTN_TEXT_TPL  = "DevToolsText"

local WINDOW_BG_COLOR  = vec.New(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA  = 0.96
local BORDER_COLOR     = vec.New(0.30, 0.30, 0.35)
local BORDER_SIZE      = 1
local LABEL_COLOR_TINT = vec.New(0.85, 0.85, 0.90)
local SELECTED_COLOR   = vec.New(0.30, 0.55, 0.85)
local SELECTED_ALPHA   = 0.55

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("CameraEditor", vec.New(0, 0), vec.New(1920, 1080), false)

local editorWindow = EditorWindow.New(canvas, vec.New(700, -80), vec.New(PANEL_W, TITLE_H), LAYER, { title = "Camera Info" })
local windowPanel = editorWindow.root
local titleBar = editorWindow.titleBar
local titleText = editorWindow.title
local closeBtn = editorWindow.closeButton

local function FormatVec3(v)
    return string.format("(%.2f, %.2f, %.2f)", v.x, v.y, v.z)
end

local transformSection = Collapsible.New(windowPanel, vec.New(0, -TITLE_H), PANEL_W, 1i, {
    title = "Transform",
    expanded = true,
})

local posText      = transformSection.body:NewText("Pos: -", vec.New(SIDE_PAD, -2), 0i, BTN_TEXT_TPL)
posText:SetAnchor(vec.New(0.0, 1.0))
posText:SetRelativePoint(vec.New(0.0, 1.0))
posText:SetColor(LABEL_COLOR_TINT)
posText:SetInteractable(false)

local rotText      = transformSection.body:NewText("Rot: -", vec.New(SIDE_PAD, -2 - ROW_H), 0i, BTN_TEXT_TPL)
rotText:SetAnchor(vec.New(0.0, 1.0))
rotText:SetRelativePoint(vec.New(0.0, 1.0))
rotText:SetColor(LABEL_COLOR_TINT)
rotText:SetInteractable(false)

local fwdText      = transformSection.body:NewText("F: -", vec.New(SIDE_PAD, -2 - ROW_H * 2), 0i, BTN_TEXT_TPL)
fwdText:SetAnchor(vec.New(0.0, 1.0))
fwdText:SetRelativePoint(vec.New(0.0, 1.0))
fwdText:SetColor(LABEL_COLOR_TINT)
fwdText:SetInteractable(false)

local rightText    = transformSection.body:NewText("R: -", vec.New(SIDE_PAD, -2 - ROW_H * 3), 0i, BTN_TEXT_TPL)
rightText:SetAnchor(vec.New(0.0, 1.0))
rightText:SetRelativePoint(vec.New(0.0, 1.0))
rightText:SetColor(LABEL_COLOR_TINT)
rightText:SetInteractable(false)

local upText       = transformSection.body:NewText("U: -", vec.New(SIDE_PAD, -2 - ROW_H * 4), 0i, BTN_TEXT_TPL)
upText:SetAnchor(vec.New(0.0, 1.0))
upText:SetRelativePoint(vec.New(0.0, 1.0))
upText:SetColor(LABEL_COLOR_TINT)
upText:SetInteractable(false)

transformSection:SetBodyContentHeight(ROW_H * 5)

local mapSection = Collapsible.New(windowPanel, vec.New(0, 0), PANEL_W, 1i, {
    title = "Map Position",
    expanded = true,
})

local chunkText = mapSection.body:NewText("Chunk -", vec.New(SIDE_PAD, -2), 0i, BTN_TEXT_TPL)
chunkText:SetAnchor(vec.New(0.0, 1.0))
chunkText:SetRelativePoint(vec.New(0.0, 1.0))
chunkText:SetColor(LABEL_COLOR_TINT)
chunkText:SetInteractable(false)

local cellText  = mapSection.body:NewText("Cell -", vec.New(SIDE_PAD, -2 - ROW_H), 0i, BTN_TEXT_TPL)
cellText:SetAnchor(vec.New(0.0, 1.0))
cellText:SetRelativePoint(vec.New(0.0, 1.0))
cellText:SetColor(LABEL_COLOR_TINT)
cellText:SetInteractable(false)

local patchText = mapSection.body:NewText("Patch -", vec.New(SIDE_PAD, -2 - ROW_H * 2), 0i, BTN_TEXT_TPL)
patchText:SetAnchor(vec.New(0.0, 1.0))
patchText:SetRelativePoint(vec.New(0.0, 1.0))
patchText:SetColor(LABEL_COLOR_TINT)
patchText:SetInteractable(false)

mapSection:SetBodyContentHeight(ROW_H * 3)

local savesSection = Collapsible.New(windowPanel, vec.New(0, 0), PANEL_W, 1i, {
    title = "Saves",
    expanded = true,
})

local savesBody = savesSection.body
local LIST_PAD_TOP = 2
local LIST_INNER_W = PANEL_W - SIDE_PAD * 2

local savesList = ScrollBox.New(savesBody, vec.New(SIDE_PAD, -LIST_PAD_TOP), vec.New(LIST_INNER_W, SAVES_LIST_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = LIST_INNER_W,
    contentSizeY = SAVES_LIST_H,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0.0,
})
savesList:SetAnchor(vec.New(0.0, 1.0))
savesList:SetRelativePoint(vec.New(0.0, 1.0))

local selectedSaveName = nil
local saveRowBtns = {}
local lastSaveClickName = nil
local lastSaveClickTime = 0
local DOUBLE_CLICK_S = 0.4

local gotoBtn   = Button.New(savesBody, vec.New(SIDE_PAD, -(LIST_PAD_TOP + SAVES_LIST_H + 4)), vec.New((LIST_INNER_W - 4) / 2, BUTTON_H), 1i, MakeBtnOpts("Goto"))
gotoBtn:SetAnchor(vec.New(0.0, 1.0))
gotoBtn:SetRelativePoint(vec.New(0.0, 1.0))

local deleteBtn = Button.New(savesBody, vec.New(-SIDE_PAD, -(LIST_PAD_TOP + SAVES_LIST_H + 4)), vec.New((LIST_INNER_W - 4) / 2, BUTTON_H), 1i, MakeBtnOpts("Delete"))
deleteBtn:SetAnchor(vec.New(1.0, 1.0))
deleteBtn:SetRelativePoint(vec.New(1.0, 1.0))

local INPUT_H = BUTTON_H
local ADD_BTN_W = 50
local INPUT_W = LIST_INNER_W - ADD_BTN_W - 4
local inputY = -(LIST_PAD_TOP + SAVES_LIST_H + 4 + BUTTON_H + 4)

local nameInput = InputBox.New(savesBody, vec.New(SIDE_PAD, inputY), vec.New(INPUT_W, INPUT_H), 1i, {
    panelTemplate = "DebugDarkGrey",
    textTemplate = BTN_TEXT_TPL,
    defaultText = "",
})
nameInput:SetAnchor(vec.New(0.0, 1.0))
nameInput:SetRelativePoint(vec.New(0.0, 1.0))

local addBtn = Button.New(savesBody, vec.New(-SIDE_PAD, inputY), vec.New(ADD_BTN_W, BUTTON_H), 1i, MakeBtnOpts("Add"))
addBtn:SetAnchor(vec.New(1.0, 1.0))
addBtn:SetRelativePoint(vec.New(1.0, 1.0))

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
    windowPanel:SetSize(vec.New(PANEL_W, -y))
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
            row.root:SetColor(SELECTED_COLOR)
            row.root:SetAlpha(SELECTED_ALPHA)
            row:SetHighlighted(true)
        else
            row:SetHighlighted(false)
        end
    end
end

local rowMenu = ContextMenu.New(canvas, { items = {}, layer = integer.add(LAYER, 100i) })

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
        UI.DestroyWidget(row.root)
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
        local btn = Button.New(content, vec.New(2, -((i - 1) * rowHeight) - 1), vec.New(LIST_INNER_W - 14, rowHeight), 1i, {
            text = label,
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
        })
        btn:SetAnchor(vec.New(0.0, 1.0))
        btn:SetRelativePoint(vec.New(0.0, 1.0))
        btn.name = name
        btn:SetOnMouseUp(function(_, _, button, mouseX, mouseY)
            if button == 1i then
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
