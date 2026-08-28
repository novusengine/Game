--[[
Animation Debug — interactive ModelV2 direct-animation validation.

The staged Mixamo fixture paths are editable. The actor is intentionally not added
to the editor selection because the current selection overlay does not consume the
deformed geometry stream and would draw a bind-pose duplicate over the animation.
]]

local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local InputBox = require("@src/API/UI/InputBox")

require("@src/API/UI/MenuBar")

local PANEL_W = 560
local PANEL_H = 244
local TITLE_H = 16
local ROW_H = 20
local GAP = 4
local SIDE_PAD = 8
local LABEL_W = 76
local LAYER = 1100
local BTN_TEXT_TPL = "DevToolsText"

local WINDOW_BG_COLOR = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR = vector.create(0.30, 0.30, 0.35)
local LABEL_TINT = vector.create(0.82, 0.82, 0.88)
local MUTED_TINT = vector.create(0.62, 0.62, 0.68)

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize = 1,
    }
end

local canvas = UI.GetCanvas("AnimationDebugEditor", 0, 0, 1920, 1080, false)
local windowPanel = canvas:NewPanel(520, -110, PANEL_W, PANEL_H, LAYER, "DebugDarkGrey")
windowPanel:SetAnchor(0.0, 1.0)
windowPanel:SetRelativePoint(0.0, 1.0)
windowPanel:SetVisible(false)
windowPanel:SetColor(WINDOW_BG_COLOR)
windowPanel:SetAlpha(WINDOW_BG_ALPHA)
windowPanel:SetBorderColor(BORDER_COLOR)
windowPanel:SetBorderSize(1)

local titleBar = windowPanel:NewPanel(0, 0, PANEL_W, TITLE_H, 1, "DebugDarkGrey")
titleBar:SetAnchor(0.0, 1.0)
titleBar:SetRelativePoint(0.0, 1.0)
titleBar:SetColor(vector.create(0.06, 0.06, 0.08))
titleBar:SetAlpha(WINDOW_BG_ALPHA)

local titleText = titleBar:NewText("Animation Debug", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
titleText:SetInteractable(false)

local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, 12, 12, 1, MakeBtnOpts("X"))
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)
windowPanel:MakeDraggable(titleBar)

local function AddLabel(text, y)
    local label = windowPanel:NewText(text, SIDE_PAD, y, 1, BTN_TEXT_TPL)
    label:SetAnchor(0.0, 1.0)
    label:SetRelativePoint(0.0, 1.0)
    label:SetColor(LABEL_TINT)
    label:SetInteractable(false)
    return label
end

local function AddPathInput(label, y, defaultText)
    AddLabel(label, y - 3)
    local input = InputBox.NewInputBox(windowPanel, SIDE_PAD + LABEL_W, y, PANEL_W - SIDE_PAD * 2 - LABEL_W, ROW_H, 1, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = BTN_TEXT_TPL,
        defaultText = defaultText,
    })
    input:SetAnchor(0.0, 1.0)
    input:SetRelativePoint(0.0, 1.0)
    input:SetTextVerticallyCentered()
    return input
end

local firstRowY = -(TITLE_H + 8)
local modelInput = AddPathInput("Model", firstRowY, "character.model")
local skeletonInput = AddPathInput("Skeleton", firstRowY - ROW_H - GAP, "character.skeleton")
local clipInput = AddPathInput("Clip", firstRowY - (ROW_H + GAP) * 2, "orc_idle.animationclip")

local entity = nil
local operationGeneration = 0
local looping = true
local skeletonVisible = false
local statusText
local stateText
local loopBtn
local skeletonBtn

local function SetStatus(text)
    statusText:SetText("Status: " .. text)
end

local function FindCVar(name)
    for _, cvar in Editor.GetCVars() do
        if cvar.name == name then return cvar end
    end
    return nil
end

local function SetCVar(name, value)
    local cvar = FindCVar(name)
    if cvar == nil then
        SetStatus("CVAR not found: " .. name)
        return false
    end
    Editor.SetCVar(cvar.category, cvar.name, value)
    return true
end

local function RefreshButtons()
    loopBtn:SetHighlighted(looping)
    skeletonBtn:SetHighlighted(skeletonVisible)
end

local function RefreshState()
    if entity == nil then
        stateText:SetText("No debug actor")
        return
    end
    local state = Asset.GetAnimationState(entity)
    if state == nil then
        stateText:SetText("Entity " .. tostring(entity) .. " | model loaded, no clip bound")
        return
    end
    local mode = state.playing and "playing" or "paused"
    stateText:SetText(string.format("Entity %d | %s | %.3f / %.3f s | %.2fx | loop %s", entity, mode, state.time, state.duration, state.playbackRate, state.looping and "on" or "off"))
end

local function DestroyActor()
    operationGeneration += 1
    if entity ~= nil then
        Asset.DestroyAnimatedModel(entity)
        entity = nil
    end
    RefreshState()
end

local function FrameWhenReady(targetEntity, generation, attempts)
    if generation ~= operationGeneration or entity ~= targetEntity then return end
    if Scene.CenterCameraOnEntity(targetEntity, 0.58) then return end
    if attempts < 240 then
        Scheduler.AfterFrames(1, function() FrameWhenReady(targetEntity, generation, attempts + 1) end)
    else
        SetStatus("actor spawned, camera framing timed out")
    end
end

local function SpawnAnimated(shouldPlay)
    DestroyActor()
    entity = Asset.SpawnModelAtWorldOrigin(modelInput.text)
    if entity == nil then
        SetStatus("model spawn rejected")
        RefreshState()
        return
    end
    operationGeneration += 1
    local generation = operationGeneration
    local targetEntity = entity
    SetStatus("waiting for model and animation assets")
    SetCVar("animationDebugSkeletonPath", skeletonInput.text)
    SetCVar("animationDebugSkeletonPose", 1)
    Camera.ActivateFreeflying()

    local function BindWhenReady(attempt)
        if generation ~= operationGeneration or entity ~= targetEntity then return end
        if Asset.BindAnimation(targetEntity, skeletonInput.text, clipInput.text) then
            Asset.AnimationSetLooping(targetEntity, looping)
            Asset.AnimationSetPlaybackRate(targetEntity, 1.0)
            Asset.AnimationSeek(targetEntity, 0.0)
            if shouldPlay then
                Asset.AnimationPlay(targetEntity)
                SetStatus("animation bound and playing")
            else
                Asset.AnimationPause(targetEntity)
                SetStatus("animation bound and paused at 0")
            end
            FrameWhenReady(targetEntity, generation, 1)
            RefreshState()
        elseif attempt < 240 then
            Scheduler.AfterFrames(1, function() BindWhenReady(attempt + 1) end)
        else
            SetStatus("timed out binding animation; check paths and log")
            RefreshState()
        end
    end
    Scheduler.AfterFrames(1, function() BindWhenReady(1) end)
end

local function SpawnRestPose()
    DestroyActor()
    entity = Asset.SpawnModelAtWorldOrigin(modelInput.text)
    if entity == nil then
        SetStatus("model spawn rejected")
        RefreshState()
        return
    end
    operationGeneration += 1
    local generation = operationGeneration
    local targetEntity = entity
    SetStatus("waiting to bind rest pose")
    SetCVar("animationDebugSkeletonPath", skeletonInput.text)
    SetCVar("animationDebugSkeletonPose", 0)
    Camera.ActivateFreeflying()

    local function BindWhenReady(attempt)
        if generation ~= operationGeneration or entity ~= targetEntity then return end
        if Asset.BindRestPose(targetEntity, skeletonInput.text) then
            SetStatus("rest pose bound")
            FrameWhenReady(targetEntity, generation, 1)
            RefreshState()
        elseif attempt < 240 then
            Scheduler.AfterFrames(1, function() BindWhenReady(attempt + 1) end)
        else
            SetStatus("timed out binding rest pose; check paths and log")
        end
    end
    Scheduler.AfterFrames(1, function() BindWhenReady(1) end)
end

local controlsY = firstRowY - (ROW_H + GAP) * 3 - 4
local fourButtonW = (PANEL_W - SIDE_PAD * 2 - GAP * 3) / 4
local spawnPlayBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD, controlsY, fourButtonW, ROW_H, 1, MakeBtnOpts("Spawn + Play"))
local spawnPausedBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + (fourButtonW + GAP), controlsY, fourButtonW, ROW_H, 1, MakeBtnOpts("Spawn Paused"))
local restPoseBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + (fourButtonW + GAP) * 2, controlsY, fourButtonW, ROW_H, 1, MakeBtnOpts("Rest Pose"))
local destroyBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + (fourButtonW + GAP) * 3, controlsY, fourButtonW, ROW_H, 1, MakeBtnOpts("Destroy"))

local playbackY = controlsY - ROW_H - GAP
local fiveButtonW = (PANEL_W - SIDE_PAD * 2 - GAP * 4) / 5
local playBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD, playbackY, fiveButtonW, ROW_H, 1, MakeBtnOpts("Play"))
local pauseBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP), playbackY, fiveButtonW, ROW_H, 1, MakeBtnOpts("Pause"))
local stopBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP) * 2, playbackY, fiveButtonW, ROW_H, 1, MakeBtnOpts("Stop"))
loopBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP) * 3, playbackY, fiveButtonW, ROW_H, 1, MakeBtnOpts("Loop"))
local frameBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP) * 4, playbackY, fiveButtonW, ROW_H, 1, MakeBtnOpts("Frame"))

for _, btn in { spawnPlayBtn, spawnPausedBtn, restPoseBtn, destroyBtn, playBtn, pauseBtn, stopBtn, loopBtn, frameBtn } do
    btn:SetAnchor(0.0, 1.0)
    btn:SetRelativePoint(0.0, 1.0)
end

local seekY = playbackY - ROW_H - GAP
AddLabel("Seek (s)", seekY - 3)
local seekInput = InputBox.NewInputBox(windowPanel, SIDE_PAD + LABEL_W, seekY, 104, ROW_H, 1, {
    panelTemplate = "DebugDarkGrey", textTemplate = BTN_TEXT_TPL, defaultText = "0.0",
})
seekInput:SetAnchor(0.0, 1.0)
seekInput:SetRelativePoint(0.0, 1.0)
seekInput:SetTextVerticallyCentered()
local seekBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + LABEL_W + 108, seekY, 70, ROW_H, 1, MakeBtnOpts("Apply"))
local stepBackBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + LABEL_W + 182, seekY, 70, ROW_H, 1, MakeBtnOpts("-0.1s"))
local stepForwardBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + LABEL_W + 256, seekY, 70, ROW_H, 1, MakeBtnOpts("+0.1s"))

local rateY = seekY - ROW_H - GAP
AddLabel("Rate", rateY - 3)
local rateInput = InputBox.NewInputBox(windowPanel, SIDE_PAD + LABEL_W, rateY, 104, ROW_H, 1, {
    panelTemplate = "DebugDarkGrey", textTemplate = BTN_TEXT_TPL, defaultText = "1.0",
})
rateInput:SetAnchor(0.0, 1.0)
rateInput:SetRelativePoint(0.0, 1.0)
rateInput:SetTextVerticallyCentered()
local rateBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + LABEL_W + 108, rateY, 70, ROW_H, 1, MakeBtnOpts("Apply"))
skeletonBtn = EditorButton.NewEditorButton(windowPanel, SIDE_PAD + LABEL_W + 182, rateY, 144, ROW_H, 1, MakeBtnOpts("Skeleton Debug"))

for _, btn in { seekBtn, stepBackBtn, stepForwardBtn, rateBtn, skeletonBtn } do
    btn:SetAnchor(0.0, 1.0)
    btn:SetRelativePoint(0.0, 1.0)
end

stateText = windowPanel:NewText("No debug actor", SIDE_PAD, rateY - ROW_H - 8, 1, BTN_TEXT_TPL)
stateText:SetAnchor(0.0, 1.0)
stateText:SetRelativePoint(0.0, 1.0)
stateText:SetColor(LABEL_TINT)
stateText:SetInteractable(false)

statusText = windowPanel:NewText("Status: ready", SIDE_PAD, rateY - ROW_H - 28, 1, BTN_TEXT_TPL)
statusText:SetAnchor(0.0, 1.0)
statusText:SetRelativePoint(0.0, 1.0)
statusText:SetColor(MUTED_TINT)
statusText:SetInteractable(false)

spawnPlayBtn:SetOnMouseUp(function() SpawnAnimated(true) end)
spawnPausedBtn:SetOnMouseUp(function() SpawnAnimated(false) end)
restPoseBtn:SetOnMouseUp(SpawnRestPose)
destroyBtn:SetOnMouseUp(function()
    DestroyActor()
    SetStatus("actor destroyed")
end)
playBtn:SetOnMouseUp(function()
    if entity ~= nil and Asset.AnimationPlay(entity) then SetStatus("playing") else SetStatus("play rejected") end
    RefreshState()
end)
pauseBtn:SetOnMouseUp(function()
    if entity ~= nil and Asset.AnimationPause(entity) then SetStatus("paused") else SetStatus("pause rejected") end
    RefreshState()
end)
stopBtn:SetOnMouseUp(function()
    if entity ~= nil and Asset.AnimationStop(entity) then SetStatus("stopped at 0") else SetStatus("stop rejected") end
    RefreshState()
end)
loopBtn:SetOnMouseUp(function()
    looping = not looping
    if entity ~= nil then Asset.AnimationSetLooping(entity, looping) end
    RefreshButtons()
    RefreshState()
end)
frameBtn:SetOnMouseUp(function()
    if entity == nil then
        SetStatus("no actor to frame")
    elseif not Scene.CenterCameraOnEntity(entity, 0.58) then
        SetStatus("actor bounds not ready")
    else
        Camera.ActivateFreeflying()
        SetStatus("camera framed")
    end
end)

local function SeekTo(time)
    if entity == nil or time == nil then
        SetStatus("seek requires an animated actor and numeric time")
        return
    end
    Asset.AnimationPause(entity)
    if Asset.AnimationSeek(entity, time) then SetStatus("paused and seeked") else SetStatus("seek rejected") end
    RefreshState()
end

seekBtn:SetOnMouseUp(function() SeekTo(tonumber(seekInput.text)) end)
stepBackBtn:SetOnMouseUp(function()
    local state = entity ~= nil and Asset.GetAnimationState(entity) or nil
    if state ~= nil then SeekTo(math.max(0.0, state.time - 0.1)) else SetStatus("no animation to step") end
end)
stepForwardBtn:SetOnMouseUp(function()
    local state = entity ~= nil and Asset.GetAnimationState(entity) or nil
    if state ~= nil then SeekTo(math.min(state.duration, state.time + 0.1)) else SetStatus("no animation to step") end
end)
rateBtn:SetOnMouseUp(function()
    local rate = tonumber(rateInput.text)
    if entity ~= nil and rate ~= nil and Asset.AnimationSetPlaybackRate(entity, rate) then SetStatus("playback rate applied") else SetStatus("rate rejected") end
    RefreshState()
end)
skeletonBtn:SetOnMouseUp(function()
    skeletonVisible = not skeletonVisible
    SetCVar("animationDebugSkeletonPath", skeletonInput.text)
    SetCVar("animationDebugSkeletonAxes", 1)
    SetCVar("animationDebugSkeletonEnabled", skeletonVisible and 1 or 0)
    RefreshButtons()
end)

local refreshGeneration = 0
local function BeginStateRefresh()
    refreshGeneration += 1
    local generation = refreshGeneration
    local function Tick()
        if generation ~= refreshGeneration or not windowPanel:IsVisible() then return end
        RefreshState()
        Scheduler.AfterFrames(6, Tick)
    end
    Tick()
end

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    RefreshButtons()
    BeginStateRefresh()
end

function methods:Hide()
    windowPanel:SetVisible(false)
    refreshGeneration += 1
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)
RefreshButtons()
EditorRegistry.Register("AnimationDebug", methods)
