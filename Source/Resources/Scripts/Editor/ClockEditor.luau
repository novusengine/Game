--[[
ClockEditor — Luau Clock window, hidden by default; toggled via EditorRegistry.

Layer 1100 sits above the dev top bar (1000) so the window draws over the bar
and open dropdowns. Speed control is +/- step buttons rather than a slider:
the Slider widget's inner-panel anchoring doesn't compose with non-default
anchors on the slider itself, follow-up once NumericInput exists.
]]

local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")

-- MenuBar's module-load side effect registers the DevToolsText template we use below.
require("@src/API/UI/MenuBar")

local PANEL_W       = 220
local PANEL_H       = 80
local TITLE_H       = 16
local ROW_H         = 20
local SIDE_PAD      = 6
local ROW3_PAD      = 5
local BTN_TEXT_TPL  = "DevToolsText"
local SPEED_STEP    = 0.25
local LAYER         = 1100

local WINDOW_BG_COLOR  = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA  = 0.96
local BORDER_COLOR     = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE      = 1

local BTN_OPTS = {
    textTemplate = BTN_TEXT_TPL,
    borderColor = BORDER_COLOR,
    borderSize = BORDER_SIZE,
}

local function MakeBtnOpts(text)
    -- Shallow-copy so per-button `text` doesn't smash the shared template.
    return {
        text = text,
        textTemplate = BTN_OPTS.textTemplate,
        borderColor = BTN_OPTS.borderColor,
        borderSize  = BTN_OPTS.borderSize,
    }
end

local function FormatTime(seconds)
    local total = math.floor(seconds)
    local h = math.floor(total / 3600)
    local m = math.floor(total / 60) % 60
    local s = total % 60
    return string.format("%02d:%02d:%02d", h, m, s)
end

local function FormatSpeed(speed)
    return string.format("Speed: %.2fx", speed)
end

local canvas = UI.GetCanvas("ClockEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(420, -80, PANEL_W, PANEL_H, LAYER, "DebugDarkGrey")
windowPanel:SetAnchor(0.0, 1.0)
windowPanel:SetRelativePoint(0.0, 1.0)
windowPanel:SetVisible(false)
windowPanel:SetColor(WINDOW_BG_COLOR)
windowPanel:SetAlpha(WINDOW_BG_ALPHA)
windowPanel:SetBorderColor(BORDER_COLOR)
windowPanel:SetBorderSize(BORDER_SIZE)

-- Title bar — drag handle for the window.
local titleBar = windowPanel:NewPanel(0, 0, PANEL_W, TITLE_H, 1, "DebugDarkGrey")
titleBar:SetAnchor(0.0, 1.0)
titleBar:SetRelativePoint(0.0, 1.0)
titleBar:SetColor(vector.create(0.06, 0.06, 0.08))
titleBar:SetAlpha(WINDOW_BG_ALPHA)

local titleText = titleBar:NewText("Clock", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)

local CLOSE_BTN_SIZE = 12
local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
-- Single-glyph widgets bake the glyph's right-side bearing into their width,
-- which leaves the visible glyph ~1px left of center.
closeBtn.text:SetPosX(1)

windowPanel:MakeDraggable(titleBar)

-- Row 1: Reset Time | Set Time to Noon
local row1Y = -(TITLE_H + 2)
local btnW = (PANEL_W - SIDE_PAD * 2 - 4) / 2
local resetTimeBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD, row1Y, btnW, ROW_H, 1, MakeBtnOpts("Reset Time"))
resetTimeBtn:SetAnchor(0.0, 1.0)
resetTimeBtn:SetRelativePoint(0.0, 1.0)
resetTimeBtn:SetOnMouseUp(function() Time.Reset() end)

local setNoonBtn = EditorButton.NewEditorButton(windowPanel, -SIDE_PAD, row1Y, btnW, ROW_H, 1, MakeBtnOpts("Set To Noon"))
setNoonBtn:SetAnchor(1.0, 1.0)
setNoonBtn:SetRelativePoint(1.0, 1.0)
setNoonBtn:SetOnMouseUp(function() Time.SetToNoon() end)

-- Row 2: Reset Speed | Speed - | Speed +
local row2Y = row1Y - ROW_H - 2
local stepBtnW = (btnW - 4) / 2

local resetSpeedBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD, row2Y, btnW, ROW_H, 1, MakeBtnOpts("Reset Speed"))
resetSpeedBtn:SetAnchor(0.0, 1.0)
resetSpeedBtn:SetRelativePoint(0.0, 1.0)

local speedDownBtn = EditorButton.NewEditorButton(windowPanel, -SIDE_PAD - stepBtnW - 4, row2Y, stepBtnW, ROW_H, 1, MakeBtnOpts("-"))
speedDownBtn:SetAnchor(1.0, 1.0)
speedDownBtn:SetRelativePoint(1.0, 1.0)
speedDownBtn.text:SetPosX(1)        -- single-glyph nudge, see closeBtn

local speedUpBtn = EditorButton.NewEditorButton(windowPanel, -SIDE_PAD, row2Y, stepBtnW, ROW_H, 1, MakeBtnOpts("+"))
speedUpBtn:SetAnchor(1.0, 1.0)
speedUpBtn:SetRelativePoint(1.0, 1.0)
speedUpBtn.text:SetPosX(1)

-- Row 3: Speed readout (left) and HH:MM:SS readout (right).
local row3Y = row2Y - ROW_H - ROW3_PAD
local speedText = windowPanel:NewText("Speed: 1.00x", SIDE_PAD, row3Y, 1, BTN_TEXT_TPL)
speedText:SetAnchor(0.0, 1.0)
speedText:SetRelativePoint(0.0, 1.0)

local timeText = windowPanel:NewText("00:00:00", -SIDE_PAD, row3Y, 1, BTN_TEXT_TPL)
timeText:SetAnchor(1.0, 1.0)
timeText:SetRelativePoint(1.0, 1.0)

local function RefreshTimeText(seconds)
    timeText:SetText(FormatTime(seconds))
end

local function RefreshSpeedText()
    speedText:SetText(FormatSpeed(Time.GetSpeedModifier()))
end

resetSpeedBtn:SetOnMouseUp(function()
    Time.SetSpeedModifier(1.0)
    RefreshSpeedText()
end)
speedDownBtn:SetOnMouseUp(function()
    Time.SetSpeedModifier(math.max(0.0, Time.GetSpeedModifier() - SPEED_STEP))
    RefreshSpeedText()
end)
speedUpBtn:SetOnMouseUp(function()
    Time.SetSpeedModifier(Time.GetSpeedModifier() + SPEED_STEP)
    RefreshSpeedText()
end)

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    RefreshTimeText(Time.GetSeconds())
    RefreshSpeedText()
    Time.SetOnSecondChanged(RefreshTimeText)
end

function methods:Hide()
    windowPanel:SetVisible(false)
    Time.SetOnSecondChanged(nil)
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("Clock", methods)
