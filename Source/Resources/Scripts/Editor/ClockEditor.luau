local EditorWindow = require("@src/API/Editor/UI/Window")
local ClockSync = require("@src/API/Editor/ClockSync")
local Declarative = require("@src/API/UI/Declarative")
local EditorRegistry = require("@src/API/Editor/Registry")

local PANEL_W       = 220
local PANEL_H       = 80
local TITLE_H       = 16
local ROW_H         = 20
local SIDE_PAD      = 6
local BTN_TEXT_TPL  = "DevToolsText"
local SPEED_FACTOR  = 10.0
local LAYER         = 1100i

local BORDER_COLOR = vec.New(0.30, 0.30, 0.35)
local BORDER_SIZE = 1
local readoutTextSize = UI.CalculateTextSize("00:00:00", BTN_TEXT_TPL)
local readoutTextY = -math.floor(math.max(0, ROW_H - readoutTextSize.y) / 2)

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

local declarativeView : Declarative.View?

local function RefreshTimeText(seconds)
    if declarativeView == nil then return end

    declarativeView:Get("timeText"):SetText(FormatTime(seconds))
end

local function RefreshSpeedText()
    if declarativeView == nil then return end

    declarativeView:Get("speedText"):SetText(FormatSpeed(Time.GetSpeedModifier()))
end

local canvas = UI.GetCanvas("ClockEditor", vec.New(0, 0), vec.New(1920, 1080), false)
local editorWindow = EditorWindow.New(canvas, vec.New(420, -80), vec.New(PANEL_W, PANEL_H), LAYER, { title = "Clock" })
local windowPanel = editorWindow.root
local btnW = (PANEL_W - SIDE_PAD * 2 - 4) / 2
local stepBtnW = (btnW - 4) / 2

local viewDescription = Declarative.Vertical({
    position = vec.New(0, -TITLE_H),
    size = vec.New(PANEL_W, PANEL_H - TITLE_H),
    anchor = Declarative.Anchor.TopLeft,
    relativePoint = Declarative.Anchor.TopLeft,
    paddingTop = 2,
    paddingLeft = SIDE_PAD,
    paddingRight = SIDE_PAD,
    spacing = 1,

    setup = function(layout)
        layout:SetAlpha(0)
        layout:SetBorderSize(0)
    end,
},

    Declarative.Horizontal({ height = ROW_H, spacing = 4, template = "DebugDarkGrey", alpha = 0 },
        Declarative.Button("Reset Time", {
            width = btnW,
            textTemplate = BTN_TEXT_TPL,
            borderColor = BORDER_COLOR,
            borderSize = BORDER_SIZE,

            onClick = function() Time.Reset() end,
        }),
        Declarative.Button("Set To Noon", {
            width = btnW,
            textTemplate = BTN_TEXT_TPL,
            borderColor = BORDER_COLOR,
            borderSize = BORDER_SIZE,

            onClick = function() Time.SetToNoon() end,
        })
    ),

    Declarative.Horizontal({ height = ROW_H, spacing = 4, template = "DebugDarkGrey", alpha = 0 },
        Declarative.Button("Reset Speed", {
            width = btnW,
            textTemplate = BTN_TEXT_TPL,
            borderColor = BORDER_COLOR,
            borderSize = BORDER_SIZE,
            onClick = function()
                Time.SetSpeedModifier(1.0)
                RefreshSpeedText()
            end,
        }),

        Declarative.Horizontal({ width = btnW, spacing = 4, template = "DebugDarkGrey", alpha = 0 },
            Declarative.Button("-", {
                width = stepBtnW,
                textTemplate = BTN_TEXT_TPL,
                borderColor = BORDER_COLOR,
                borderSize = BORDER_SIZE,

                onClick = function()
                    local speed = Time.GetSpeedModifier() / SPEED_FACTOR
                    if speed < 1.0 then
                        speed = 0.0
                    end
                    Time.SetSpeedModifier(speed)
                    RefreshSpeedText()
                end,
            }),

            Declarative.Button("+", {
                width = stepBtnW,
                textTemplate = BTN_TEXT_TPL,
                borderColor = BORDER_COLOR,
                borderSize = BORDER_SIZE,

                onClick = function()
                    local speed = math.max(1.0, Time.GetSpeedModifier() * SPEED_FACTOR)
                    Time.SetSpeedModifier(math.min(speed, Time.SecondsPerDay))
                    RefreshSpeedText()
                end,
            })
        )
    ),

    Declarative.Panel({ height = ROW_H, template = "DebugDarkGrey", alpha = 0 },
        Declarative.Text("Speed: 1x", { id = "speedText", position = vec.New(0, readoutTextY), anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, template = BTN_TEXT_TPL }),
        Declarative.Text("00:00:00", { id = "timeText", position = vec.New(0, readoutTextY), anchor = Declarative.Anchor.TopRight, relativePoint = Declarative.Anchor.TopRight, template = BTN_TEXT_TPL })
    )
)

declarativeView = Declarative.Build(windowPanel, viewDescription)

local methods = {
    Show = function(self)
        windowPanel:SetVisible(true)
        RefreshTimeText(Time.GetSeconds())
        RefreshSpeedText()
        ClockSync.Subscribe("Clock", RefreshTimeText)
    end,

    Hide = function(self)
        windowPanel:SetVisible(false)
        ClockSync.Unsubscribe("Clock")
    end,

    IsVisible = function(self)
        return windowPanel:IsVisible()
    end
}

editorWindow.closeButton:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("Clock", methods)
