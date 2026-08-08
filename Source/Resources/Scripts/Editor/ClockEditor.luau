local EditorWindow = require("@src/API/Editor/UI/Window")
--[[
ClockEditor — Luau Clock window, hidden by default; toggled via EditorRegistry.

Layer 1100 sits above the dev top bar (1000) so the window draws over the bar
and open dropdowns. Speed control is +/- step buttons rather than a slider:
the Slider widget's inner-panel anchoring doesn't compose with non-default
anchors on the slider itself, follow-up once NumericInput exists.
]]

local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")

-- MenuBar's module-load side effect registers the DevToolsText template we use below.

local PANEL_W       = 220
local PANEL_H       = 80
local TITLE_H       = 16
local ROW_H         = 20
local SIDE_PAD      = 6
local ROW3_PAD      = 5
local BTN_TEXT_TPL  = "DevToolsText"
local SPEED_FACTOR  = 10.0
local LAYER         = 1100i

local WINDOW_BG_COLOR  = vec.New(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA  = 0.96
local BORDER_COLOR     = vec.New(0.30, 0.30, 0.35)
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
    return string.format("Speed: %gx", speed)
end

local canvas = UI.GetCanvas("ClockEditor", vec.New(0, 0), vec.New(1920, 1080), false)

local editorWindow = EditorWindow.New(canvas, vec.New(420, -80), vec.New(PANEL_W, PANEL_H), LAYER, { title = "Clock" })
local windowPanel = editorWindow.root
local titleBar = editorWindow.titleBar
local titleText = editorWindow.title
local closeBtn = editorWindow.closeButton

-- Row 1: Reset Time | Set Time to Noon
local row1Y = -(TITLE_H + 2)
local btnW = (PANEL_W - SIDE_PAD * 2 - 4) / 2
local resetTimeBtn = Button.New(windowPanel, vec.New(SIDE_PAD, row1Y), vec.New(btnW, ROW_H), 1i, MakeBtnOpts("Reset Time"))
resetTimeBtn:SetAnchor(vec.New(0.0, 1.0))
resetTimeBtn:SetRelativePoint(vec.New(0.0, 1.0))
resetTimeBtn:SetOnMouseUp(function() Time.Reset() end)

local setNoonBtn = Button.New(windowPanel, vec.New(-SIDE_PAD, row1Y), vec.New(btnW, ROW_H), 1i, MakeBtnOpts("Set To Noon"))
setNoonBtn:SetAnchor(vec.New(1.0, 1.0))
setNoonBtn:SetRelativePoint(vec.New(1.0, 1.0))
setNoonBtn:SetOnMouseUp(function() Time.SetToNoon() end)

-- Row 2: Reset Speed | Speed - | Speed +
local row2Y = row1Y - ROW_H - 2
local stepBtnW = (btnW - 4) / 2

local resetSpeedBtn = Button.New(windowPanel, vec.New(SIDE_PAD, row2Y), vec.New(btnW, ROW_H), 1i, MakeBtnOpts("Reset Speed"))
resetSpeedBtn:SetAnchor(vec.New(0.0, 1.0))
resetSpeedBtn:SetRelativePoint(vec.New(0.0, 1.0))

local speedDownBtn = Button.New(windowPanel, vec.New(-SIDE_PAD - stepBtnW - 4, row2Y), vec.New(stepBtnW, ROW_H), 1i, MakeBtnOpts("-"))
speedDownBtn:SetAnchor(vec.New(1.0, 1.0))
speedDownBtn:SetRelativePoint(vec.New(1.0, 1.0))
speedDownBtn.text:SetPosX(1)        -- single-glyph nudge, see closeBtn

local speedUpBtn = Button.New(windowPanel, vec.New(-SIDE_PAD, row2Y), vec.New(stepBtnW, ROW_H), 1i, MakeBtnOpts("+"))
speedUpBtn:SetAnchor(vec.New(1.0, 1.0))
speedUpBtn:SetRelativePoint(vec.New(1.0, 1.0))
speedUpBtn.text:SetPosX(1)

-- Row 3: Speed readout (left) and HH:MM:SS readout (right).
local row3Y = row2Y - ROW_H - ROW3_PAD
local speedText = windowPanel:NewText("Speed: 1x", vec.New(SIDE_PAD, row3Y), 1i, BTN_TEXT_TPL)
speedText:SetAnchor(vec.New(0.0, 1.0))
speedText:SetRelativePoint(vec.New(0.0, 1.0))

local timeText = windowPanel:NewText("00:00:00", vec.New(-SIDE_PAD, row3Y), 1i, BTN_TEXT_TPL)
timeText:SetAnchor(vec.New(1.0, 1.0))
timeText:SetRelativePoint(vec.New(1.0, 1.0))

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
    local speed = Time.GetSpeedModifier() / SPEED_FACTOR
    if speed < 1.0 then
        speed = 0.0 -- Pause instead of crawling below 1x
    end
    Time.SetSpeedModifier(speed)
    RefreshSpeedText()
end)
speedUpBtn:SetOnMouseUp(function()
    local speed = math.max(1.0, Time.GetSpeedModifier() * SPEED_FACTOR)
    Time.SetSpeedModifier(math.min(speed, Time.SecondsPerDay)) -- Cap at a full day per real second
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
