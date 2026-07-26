--[[
NetworkEditor — Lua port of the C++ ImGui "Networked Info". Two states: disconnected (connect IP /
account inputs + Connect) and connected (ping, character/target readout + Disconnect). Live fields
refresh off the per-frame GameEvent.Updated tick (throttled, only while visible).

Data goes through the dev-only Network handler. Layer 1100 sits above the dev top bar (1000).
]]

local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local InputBox = require("@src/API/UI/InputBox")

require("@src/API/UI/MenuBar")

local PANEL_W      = 330
local WINDOW_H     = 320
local TITLE_H      = 16
local ROW_H        = 15
local SIDE_PAD     = 6
local LAYER        = 1100
local BTN_TEXT_TPL = "DevToolsText"
local REFRESH_INTERVAL = 0.1

local WINDOW_BG_COLOR = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR    = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE     = 1
local LABEL_TINT      = vector.create(0.82, 0.82, 0.88)

local function MakeBtnOpts(text)
    return { text = text, textTemplate = BTN_TEXT_TPL, borderColor = BORDER_COLOR, borderSize = BORDER_SIZE }
end

local canvas = UI.GetCanvas("NetworkEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(680, -160, PANEL_W, WINDOW_H, LAYER, "DebugDarkGrey")
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

local titleText = titleBar:NewText("Networked Info", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
titleText:SetInteractable(false)

local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, 12, 12, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)

windowPanel:MakeDraggable(titleBar)

-- A simple top-anchored text label at row `row` (0-based) under the title bar.
local function MakeLabel(parent, row, text)
    local label = parent:NewText(text, SIDE_PAD, -(TITLE_H + 2 + row * ROW_H), 1, BTN_TEXT_TPL)
    label:SetAnchor(0.0, 1.0)
    label:SetRelativePoint(0.0, 1.0)
    label:SetColor(LABEL_TINT)
    label:SetInteractable(false)
    return label
end

local statusText = MakeLabel(windowPanel, 0, "Connection: -")

-- ---- connected group ----
local connGroup = windowPanel:NewPanel(0, 0, PANEL_W, WINDOW_H, 1, "DebugDarkGrey")
connGroup:SetAnchor(0.0, 1.0)
connGroup:SetRelativePoint(0.0, 1.0)
connGroup:SetAlpha(0.0)
connGroup:SetInteractable(false) -- pass clicks through to the title bar; children stay interactable
connGroup:SetVisible(false)

local pingText   = MakeLabel(connGroup, 1, "Ping: -")
local posText    = MakeLabel(connGroup, 3, "Pos: -")
local speedText  = MakeLabel(connGroup, 4, "Speed: -")
local oriText    = MakeLabel(connGroup, 5, "Pitch/Yaw: -")
local fwdText    = MakeLabel(connGroup, 6, "Forward: -")
local moverText  = MakeLabel(connGroup, 8, "Mover: -")
local targetText = MakeLabel(connGroup, 9, "Target: -")

local aabbBtn = EditorButton.NewEditorButton(connGroup, SIDE_PAD, -(TITLE_H + 2 + 11 * ROW_H), 160, ROW_H, 1, MakeBtnOpts("Draw Target AABB: ?"))
aabbBtn:SetAnchor(0.0, 1.0)
aabbBtn:SetRelativePoint(0.0, 1.0)
aabbBtn:SetOnMouseUp(function()
    Network.SetDrawTargetAABB(not Network.GetDrawTargetAABB())
    aabbBtn:SetText("Draw Target AABB: " .. (Network.GetDrawTargetAABB() and "ON" or "OFF"))
end)

local disconnectBtn = EditorButton.NewEditorButton(connGroup, SIDE_PAD, -(TITLE_H + 2 + 13 * ROW_H), 90, ROW_H + 2, 1, MakeBtnOpts("Disconnect"))
disconnectBtn:SetAnchor(0.0, 1.0)
disconnectBtn:SetRelativePoint(0.0, 1.0)
disconnectBtn:SetOnMouseUp(function() Network.Disconnect() end)

-- ---- disconnected group ----
local disGroup = windowPanel:NewPanel(0, 0, PANEL_W, WINDOW_H, 1, "DebugDarkGrey")
disGroup:SetAnchor(0.0, 1.0)
disGroup:SetRelativePoint(0.0, 1.0)
disGroup:SetAlpha(0.0)
disGroup:SetInteractable(false) -- pass clicks through to the title bar; children stay interactable
disGroup:SetVisible(false)

MakeLabel(disGroup, 1, "Connect IP:")
local ipInput = InputBox.NewInputBox(disGroup, SIDE_PAD, -(TITLE_H + 2 + 2 * ROW_H), PANEL_W - SIDE_PAD * 2, ROW_H + 4, 1, {
    panelTemplate = "DebugDarkGrey", textTemplate = BTN_TEXT_TPL, defaultText = "",
})
ipInput:SetAnchor(0.0, 1.0)
ipInput:SetRelativePoint(0.0, 1.0)
ipInput:SetOnTextChanged(function() Network.SetConnectIP(ipInput.text) end)

MakeLabel(disGroup, 4, "Account Name:")
local accountInput = InputBox.NewInputBox(disGroup, SIDE_PAD, -(TITLE_H + 2 + 5 * ROW_H), PANEL_W - SIDE_PAD * 2, ROW_H + 4, 1, {
    panelTemplate = "DebugDarkGrey", textTemplate = BTN_TEXT_TPL, defaultText = "",
})
accountInput:SetAnchor(0.0, 1.0)
accountInput:SetRelativePoint(0.0, 1.0)
accountInput:SetOnTextChanged(function() Network.SetAccountName(accountInput.text) end)

local connectBtn = EditorButton.NewEditorButton(disGroup, SIDE_PAD, -(TITLE_H + 2 + 7 * ROW_H), 90, ROW_H + 2, 1, MakeBtnOpts("Connect"))
connectBtn:SetAnchor(0.0, 1.0)
connectBtn:SetRelativePoint(0.0, 1.0)
connectBtn:SetOnMouseUp(function() Network.Connect() end)

local function fmt(n) return string.format("%.2f", n or 0) end

local function Refresh()
    local connected = Network.IsConnected()
    statusText:SetText("Connection: " .. (connected and "Connected" or "Disconnected"))
    connGroup:SetVisible(connected)
    disGroup:SetVisible(not connected)

    if connected then
        local p = Network.GetPingInfo()
        pingText:SetText(string.format("Ping: %dms   Server diff: %dms", p.ping, p.serverUpdateDiff))

        local c = Network.GetCharacterInfo()
        if c then
            posText:SetText(string.format("Pos: (%s, %s, %s)", fmt(c.posX), fmt(c.posY), fmt(c.posZ)))
            speedText:SetText("Speed: " .. fmt(c.speed))
            oriText:SetText(string.format("Pitch/Yaw: (%s, %s)", fmt(c.pitchDegrees), fmt(c.yawDegrees)))
            fwdText:SetText(string.format("Forward: (%s, %s, %s)", fmt(c.forwardX), fmt(c.forwardY), fmt(c.forwardZ)))
        else
            posText:SetText("No active character")
            speedText:SetText("")
            oriText:SetText("")
            fwdText:SetText("")
        end

        local mover = Network.GetMoverUnit()
        moverText:SetText(mover and string.format("Mover: %s  HP %s/%s", mover.name, fmt(mover.healthCurrent), fmt(mover.healthMax)) or "Mover: -")
        local target = Network.GetTargetUnit()
        targetText:SetText(target and string.format("Target: %s  HP %s/%s", target.name, fmt(target.healthCurrent), fmt(target.healthMax)) or "Target: -")

        aabbBtn:SetText("Draw Target AABB: " .. (Network.GetDrawTargetAABB() and "ON" or "OFF"))
    end
end

local refreshAccum = 0
RegisterEvent(GameEvent.Updated, function(_, data)
    if not windowPanel:IsVisible() then return end
    refreshAccum += (data and data.deltaTime or 0)
    if refreshAccum < REFRESH_INTERVAL then return end
    refreshAccum = 0
    Refresh()
end)

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    ipInput:SetText(Network.GetConnectIP())
    accountInput:SetText(Network.GetAccountName())
    Refresh()
end

function methods:Hide()
    windowPanel:SetVisible(false)
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("NetworkedInfo", methods)
