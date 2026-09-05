--[[
Animation Debug — interactive ModelV2 direct-animation validation.

The staged Mixamo fixture paths are editable. The actor is intentionally not added
to the editor selection because the current selection overlay does not consume the
deformed geometry stream and would draw a bind-pose duplicate over the animation.
]]

local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local PreviewSession = require("@src/API/Editor/AnimationPreviewSession")
local EditorWindow = require("@src/API/Editor/UI/Window")
local InputBox = require("@src/API/UI/InputBox")

require("@src/API/Editor/UI/MenuBar")

local PANEL_W = 560
local PANEL_H = 320
local TITLE_H = 16
local ROW_H = 20
local GAP = 4
local SIDE_PAD = 8
local LABEL_W = 76
local LAYER = 1100i
local BTN_TEXT_TPL = "DevToolsText"

local WINDOW_BG_COLOR = vec.New(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR = vec.New(0.30, 0.30, 0.35)
local LABEL_TINT = vec.New(0.82, 0.82, 0.88)
local MUTED_TINT = vec.New(0.62, 0.62, 0.68)

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize = 1,
    }
end

local function NewButton(parent, x, y, width, height, layer, options)
    return Button.New(parent, vec.New(x, y), vec.New(width, height), layer, options)
end

local function NewInputBox(parent, x, y, width, height, layer, options)
    return InputBox.New(parent, vec.New(x, y), vec.New(width, height), layer, options)
end

local canvas = UI.GetCanvas("AnimationDebugEditor", vec.New(0, 0), vec.New(1920, 1080), false)
local editorWindow = EditorWindow.New(canvas, vec.New(520, -110), vec.New(PANEL_W, PANEL_H), LAYER, {
    title = "Animation Debug",
    titleHeight = TITLE_H,
    padding = SIDE_PAD,
    windowColor = WINDOW_BG_COLOR,
    titleColor = vec.New(0.06, 0.06, 0.08),
    alpha = WINDOW_BG_ALPHA,
})
local windowPanel = editorWindow.root
local closeBtn = editorWindow.closeButton

local function AddLabel(text, y)
    local label = windowPanel:NewText(text, vec.New(SIDE_PAD, y), 1i, BTN_TEXT_TPL)
    label:SetAnchor(vec.New(0.0, 1.0))
    label:SetRelativePoint(vec.New(0.0, 1.0))
    label:SetColor(LABEL_TINT)
    label:SetInteractable(false)
    return label
end

local function AddPathInput(label, y, defaultText)
    AddLabel(label, y - 3)
    local input = NewInputBox(windowPanel, SIDE_PAD + LABEL_W, y, PANEL_W - SIDE_PAD * 2 - LABEL_W, ROW_H, 1i, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = BTN_TEXT_TPL,
        defaultText = defaultText,
    })
    input:SetAnchor(vec.New(0.0, 1.0))
    input:SetRelativePoint(vec.New(0.0, 1.0))
    input:SetTextVerticallyCentered()
    return input
end

local firstRowY = -(TITLE_H + 8)
local modelInput = AddPathInput("Model", firstRowY, "character.model")
local skeletonInput = AddPathInput("Skeleton", firstRowY - ROW_H - GAP, "character.skeleton")
local clipInput = AddPathInput("Clip", firstRowY - (ROW_H + GAP) * 2, "orc_idle.animationclip")
local setInput = AddPathInput("Set", firstRowY - (ROW_H + GAP) * 3, "animations/mixamo/mixamo.animationset")

local entity = nil
local operationGeneration = 0
local looping = true
local skeletonVisible = false
local statusText
local stateText
local loopBtn
local skeletonBtn
local controllerMode = false
local controllerGraphMode = "states"
local speedParameter = nil
local lateralParameter = nil
local forwardParameter = nil
local magnitudeParameter = nil
local ikGoalParameter = nil
local ikWeightParameter = nil
local headGoalParameter = nil
local leftGripGoalParameter = nil
local leftGripWeightParameter = nil
local upperBodyWeightParameter = nil
local additiveWeightParameter = nil
local attachmentEntities = {}
local FrameWhenReady

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
    local controllerState = Asset.GetAnimationControllerState(entity)
    if controllerState ~= nil then
        local transition = controllerState.transitioning and string.format(" -> %d  %.0f/%.0f ms", integer.tonumber(controllerState.transitionTarget), integer.tonumber(controllerState.transitionTimeUS) / 1000, integer.tonumber(controllerState.transitionDurationUS) / 1000) or ""
        stateText:SetText(string.format("Entity %d | state %d%s | %.2f s | slot %s>%s clip %s variant %d depth %d result %d | samples %d tri %s weights %.2f/%.2f/%.2f | leader %s markers %s>%s rates %.2f/%.2f/%.2f fallback %s snapshot %s", integer.tonumber(entity), integer.tonumber(controllerState.currentState), transition, integer.tonumber(controllerState.stateTimeUS) / 1000000, tostring(controllerState.requestedSlotSemanticID), tostring(controllerState.resolvedSlotSemanticID), tostring(controllerState.resolvedClipAssetID), integer.tonumber(controllerState.resolvedVariationID), integer.tonumber(controllerState.resolvedFallbackDepth), integer.tonumber(controllerState.slotResolution), integer.tonumber(controllerState.activeBlendSamples), tostring(controllerState.activeBlendTriangle), controllerState.blendWeight0, controllerState.blendWeight1, controllerState.blendWeight2, tostring(controllerState.synchronizationLeader), tostring(controllerState.previousSynchronizationMarker), tostring(controllerState.nextSynchronizationMarker), controllerState.playbackRate0, controllerState.playbackRate1, controllerState.playbackRate2, controllerState.synchronizationFallback and "yes" or "no", controllerState.hasContinuitySnapshot and "yes" or "no"))
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
    for _, attachmentEntity in attachmentEntities do
        Asset.DestroyAnimatedModel(attachmentEntity)
    end
    table.clear(attachmentEntities)
    if entity ~= nil then
        PreviewSession.Clear(entity)
        Asset.DestroyAnimatedModel(entity)
        entity = nil
    end
    controllerMode = false
    controllerGraphMode = "states"
    speedParameter = nil
    lateralParameter = nil
    forwardParameter = nil
    magnitudeParameter = nil
    ikGoalParameter = nil
    ikWeightParameter = nil
    headGoalParameter = nil
    leftGripGoalParameter = nil
    leftGripWeightParameter = nil
    upperBodyWeightParameter = nil
    additiveWeightParameter = nil
    RefreshState()
end

local function SpawnController(mode)
    DestroyActor()
    entity = Asset.SpawnModelAtWorldOrigin(modelInput:GetText())
    if entity == nil then
        SetStatus("model spawn rejected")
        return
    end
    operationGeneration += 1
    local generation = operationGeneration
    local targetEntity = entity
    controllerMode = true
    controllerGraphMode = mode
    SetStatus("waiting for model, Skeleton, Set, and clips")
    SetCVar("animationDebugSkeletonPath", skeletonInput:GetText())
    SetCVar("animationDebugSkeletonPose", 1i)
    Camera.ActivateFreeflying()
    local function BindWhenReady(attempt)
        if generation ~= operationGeneration or entity ~= targetEntity then return end
        if Asset.BindAnimationController(targetEntity, skeletonInput:GetText(), setInput:GetText(), mode) then
            if mode == "ik" or mode == "ik_showcase" then
                ikGoalParameter = Asset.ResolveAnimationParameter(targetEntity, "rightHandGoal")
                ikWeightParameter = Asset.ResolveAnimationParameter(targetEntity, "ikWeight")
                if ikGoalParameter == nil or ikWeightParameter == nil then SetStatus("IK parameters did not resolve"); return end
                Asset.AnimationSetFloat3Parameter(targetEntity, ikGoalParameter, -0.35, 1.45, 0.45)
                Asset.AnimationSetFloatParameter(targetEntity, ikWeightParameter, 1.0)
                if mode == "ik_showcase" then
                    headGoalParameter = Asset.ResolveAnimationParameter(targetEntity, "headGoal")
                    if headGoalParameter == nil then SetStatus("head goal parameter did not resolve"); return end
                    Asset.AnimationSetFloat3Parameter(targetEntity, headGoalParameter, 0.0, 1.65, 1.0)
                end
            elseif mode == "layers" then
                speedParameter = Asset.ResolveAnimationParameter(targetEntity, "speed")
                upperBodyWeightParameter = Asset.ResolveAnimationParameter(targetEntity, "upperBodyWeight")
                additiveWeightParameter = Asset.ResolveAnimationParameter(targetEntity, "additiveWeight")
                if speedParameter == nil or upperBodyWeightParameter == nil or additiveWeightParameter == nil then SetStatus("layer parameters did not resolve"); return end
                Asset.AnimationSetFloatParameter(targetEntity, speedParameter, 1.0)
                Asset.AnimationSetFloatParameter(targetEntity, upperBodyWeightParameter, 0.0)
                Asset.AnimationSetFloatParameter(targetEntity, additiveWeightParameter, 0.0)
            elseif mode == "blend2d" then
                lateralParameter = Asset.ResolveAnimationParameter(targetEntity, "lateral")
                forwardParameter = Asset.ResolveAnimationParameter(targetEntity, "forward")
                magnitudeParameter = Asset.ResolveAnimationParameter(targetEntity, "magnitude")
                if lateralParameter == nil or forwardParameter == nil or magnitudeParameter == nil then SetStatus("2D parameters did not resolve"); return end
                Asset.AnimationSetFloatParameter(targetEntity, lateralParameter, 0.0)
                Asset.AnimationSetFloatParameter(targetEntity, forwardParameter, 0.0)
                Asset.AnimationSetFloatParameter(targetEntity, magnitudeParameter, 0.0)
            else
                speedParameter = Asset.ResolveAnimationParameter(targetEntity, "speed")
                if speedParameter == nil then SetStatus("speed parameter did not resolve"); return end
                Asset.AnimationSetFloatParameter(targetEntity, speedParameter, 0.0)
                if mode == "attachment" then
                    leftGripGoalParameter = Asset.ResolveAnimationParameter(targetEntity, "leftGripGoal")
                    leftGripWeightParameter = Asset.ResolveAnimationParameter(targetEntity, "leftGripWeight")
                    if leftGripGoalParameter == nil or leftGripWeightParameter == nil then SetStatus("secondary grip parameters did not resolve"); return end
                    Asset.AnimationSetFloatParameter(targetEntity, leftGripWeightParameter, 0.0)
                end
            end
            PreviewSession.SetEntity(targetEntity, mode)
            SetStatus("compiled " .. mode .. " graph bound")
            FrameWhenReady(targetEntity, generation, 1)
            RefreshState()
        elseif attempt < 240 then
            Scheduler.AfterFrames(1, function() BindWhenReady(attempt + 1) end)
        else
            SetStatus("timed out binding controller; check Set/clip paths and log")
        end
    end
    Scheduler.AfterFrames(1, function() BindWhenReady(1) end)
end

FrameWhenReady = function(targetEntity, generation, attempts)
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
    entity = Asset.SpawnModelAtWorldOrigin(modelInput:GetText())
    if entity == nil then
        SetStatus("model spawn rejected")
        RefreshState()
        return
    end
    operationGeneration += 1
    local generation = operationGeneration
    local targetEntity = entity
    SetStatus("waiting for model and animation assets")
    SetCVar("animationDebugSkeletonPath", skeletonInput:GetText())
    SetCVar("animationDebugSkeletonPose", 1i)
    Camera.ActivateFreeflying()

    local function BindWhenReady(attempt)
        if generation ~= operationGeneration or entity ~= targetEntity then return end
        if Asset.BindAnimation(targetEntity, skeletonInput:GetText(), clipInput:GetText()) then
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
    entity = Asset.SpawnModelAtWorldOrigin(modelInput:GetText())
    if entity == nil then
        SetStatus("model spawn rejected")
        RefreshState()
        return
    end
    operationGeneration += 1
    local generation = operationGeneration
    local targetEntity = entity
    SetStatus("waiting to bind rest pose")
    SetCVar("animationDebugSkeletonPath", skeletonInput:GetText())
    SetCVar("animationDebugSkeletonPose", 0i)
    Camera.ActivateFreeflying()

    local function BindWhenReady(attempt)
        if generation ~= operationGeneration or entity ~= targetEntity then return end
        if Asset.BindRestPose(targetEntity, skeletonInput:GetText()) then
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

local controlsY = firstRowY - (ROW_H + GAP) * 4 - 4
local spawnButtonW = (PANEL_W - SIDE_PAD * 2 - GAP * 7) / 8
local spawnPlayBtn = NewButton(windowPanel, SIDE_PAD, controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("Direct Play"))
local spawnPausedBtn = NewButton(windowPanel, SIDE_PAD + (spawnButtonW + GAP), controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("Direct Pause"))
local controllerBtn = NewButton(windowPanel, SIDE_PAD + (spawnButtonW + GAP) * 2, controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("States"))
local blend1DBtn = NewButton(windowPanel, SIDE_PAD + (spawnButtonW + GAP) * 3, controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("Blend 1D"))
local blend2DBtn = NewButton(windowPanel, SIDE_PAD + (spawnButtonW + GAP) * 4, controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("Blend 2D"))
local ikBtn = NewButton(windowPanel, SIDE_PAD + (spawnButtonW + GAP) * 5, controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("IK"))
local restPoseBtn = NewButton(windowPanel, SIDE_PAD + (spawnButtonW + GAP) * 6, controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("Rest Pose"))
local destroyBtn = NewButton(windowPanel, SIDE_PAD + (spawnButtonW + GAP) * 7, controlsY, spawnButtonW, ROW_H, 1i, MakeBtnOpts("Destroy"))

local playbackY = controlsY - ROW_H - GAP
local fiveButtonW = (PANEL_W - SIDE_PAD * 2 - GAP * 4) / 5
local playBtn = NewButton(windowPanel, SIDE_PAD, playbackY, fiveButtonW, ROW_H, 1i, MakeBtnOpts("Play"))
local pauseBtn = NewButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP), playbackY, fiveButtonW, ROW_H, 1i, MakeBtnOpts("Pause"))
local stopBtn = NewButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP) * 2, playbackY, fiveButtonW, ROW_H, 1i, MakeBtnOpts("Stop"))
loopBtn = NewButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP) * 3, playbackY, fiveButtonW, ROW_H, 1i, MakeBtnOpts("Loop"))
local frameBtn = NewButton(windowPanel, SIDE_PAD + (fiveButtonW + GAP) * 4, playbackY, fiveButtonW, ROW_H, 1i, MakeBtnOpts("Frame"))

for _, btn in { spawnPlayBtn, spawnPausedBtn, controllerBtn, blend1DBtn, blend2DBtn, ikBtn, restPoseBtn, destroyBtn, playBtn, pauseBtn, stopBtn, loopBtn, frameBtn } do
    btn:SetAnchor(vec.New(0.0, 1.0))
    btn:SetRelativePoint(vec.New(0.0, 1.0))
end

local seekY = playbackY - ROW_H - GAP
AddLabel("Seek (s)", seekY - 3)
local seekInput = NewInputBox(windowPanel, SIDE_PAD + LABEL_W, seekY, 104, ROW_H, 1i, {
    panelTemplate = "DebugDarkGrey", textTemplate = BTN_TEXT_TPL, defaultText = "0.0",
})
seekInput:SetAnchor(vec.New(0.0, 1.0))
seekInput:SetRelativePoint(vec.New(0.0, 1.0))
seekInput:SetTextVerticallyCentered()
local seekBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 108, seekY, 70, ROW_H, 1i, MakeBtnOpts("Apply"))
local stepBackBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 182, seekY, 70, ROW_H, 1i, MakeBtnOpts("-0.1s"))
local stepForwardBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 256, seekY, 70, ROW_H, 1i, MakeBtnOpts("+0.1s"))

local rateY = seekY - ROW_H - GAP
AddLabel("Rate", rateY - 3)
local rateInput = NewInputBox(windowPanel, SIDE_PAD + LABEL_W, rateY, 104, ROW_H, 1i, {
    panelTemplate = "DebugDarkGrey", textTemplate = BTN_TEXT_TPL, defaultText = "1.0",
})
rateInput:SetAnchor(vec.New(0.0, 1.0))
rateInput:SetRelativePoint(vec.New(0.0, 1.0))
rateInput:SetTextVerticallyCentered()
local rateBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 108, rateY, 70, ROW_H, 1i, MakeBtnOpts("Apply"))
skeletonBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 182, rateY, 144, ROW_H, 1i, MakeBtnOpts("Skeleton Debug"))
local graphBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 330, rateY, 130, ROW_H, 1i, MakeBtnOpts("Open Graph"))

for _, btn in { seekBtn, stepBackBtn, stepForwardBtn, rateBtn, skeletonBtn, graphBtn } do
    btn:SetAnchor(vec.New(0.0, 1.0))
    btn:SetRelativePoint(vec.New(0.0, 1.0))
end

local speedY = rateY - ROW_H - GAP
AddLabel("Input", speedY - 3)
local speedInput = NewInputBox(windowPanel, SIDE_PAD + LABEL_W, speedY, 104, ROW_H, 1i, {
    panelTemplate = "DebugDarkGrey", textTemplate = BTN_TEXT_TPL, defaultText = "0.0",
})
speedInput:SetAnchor(vec.New(0.0, 1.0))
speedInput:SetRelativePoint(vec.New(0.0, 1.0))
speedInput:SetTextVerticallyCentered()
local speedBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 108, speedY, 70, ROW_H, 1i, MakeBtnOpts("Apply"))
local idleBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 182, speedY, 70, ROW_H, 1i, MakeBtnOpts("Idle"))
local walkBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 256, speedY, 70, ROW_H, 1i, MakeBtnOpts("Walk"))
local runBtn = NewButton(windowPanel, SIDE_PAD + LABEL_W + 330, speedY, 70, ROW_H, 1i, MakeBtnOpts("Run"))
for _, btn in { speedBtn, idleBtn, walkBtn, runBtn } do
    btn:SetAnchor(vec.New(0.0, 1.0))
    btn:SetRelativePoint(vec.New(0.0, 1.0))
end

stateText = windowPanel:NewText("No debug actor", vec.New(SIDE_PAD, speedY - ROW_H - 8), 1i, BTN_TEXT_TPL)
stateText:SetAnchor(vec.New(0.0, 1.0))
stateText:SetRelativePoint(vec.New(0.0, 1.0))
stateText:SetColor(LABEL_TINT)
stateText:SetInteractable(false)

statusText = windowPanel:NewText("Status: ready", vec.New(SIDE_PAD, speedY - ROW_H - 28), 1i, BTN_TEXT_TPL)
statusText:SetAnchor(vec.New(0.0, 1.0))
statusText:SetRelativePoint(vec.New(0.0, 1.0))
statusText:SetColor(MUTED_TINT)
statusText:SetInteractable(false)

spawnPlayBtn:SetOnMouseUp(function() SpawnAnimated(true) end)
spawnPausedBtn:SetOnMouseUp(function() SpawnAnimated(false) end)
controllerBtn:SetOnMouseUp(function() SpawnController("states") end)
blend1DBtn:SetOnMouseUp(function() SpawnController("blend1d") end)
blend2DBtn:SetOnMouseUp(function() SpawnController("blend2d") end)
ikBtn:SetOnMouseUp(function() SpawnController("ik") end)
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

seekBtn:SetOnMouseUp(function() SeekTo(tonumber(seekInput:GetText())) end)
stepBackBtn:SetOnMouseUp(function()
    local state = entity ~= nil and Asset.GetAnimationState(entity) or nil
    if state ~= nil then SeekTo(math.max(0.0, state.time - 0.1)) else SetStatus("no animation to step") end
end)
stepForwardBtn:SetOnMouseUp(function()
    local state = entity ~= nil and Asset.GetAnimationState(entity) or nil
    if state ~= nil then SeekTo(math.min(state.duration, state.time + 0.1)) else SetStatus("no animation to step") end
end)
rateBtn:SetOnMouseUp(function()
    local rate = tonumber(rateInput:GetText())
    if entity ~= nil and rate ~= nil and Asset.AnimationSetPlaybackRate(entity, rate) then SetStatus("playback rate applied") else SetStatus("rate rejected") end
    RefreshState()
end)
local function SetControllerSpeed(value)
    if not controllerMode or entity == nil then
        SetStatus("spawn a controller actor first")
        return
    end
    if controllerGraphMode == "ik" or controllerGraphMode == "ik_showcase" then
        if ikGoalParameter == nil then SetStatus("IK goal parameter unavailable"); return end
        local input = speedInput:GetText()
        local first = string.find(input, ",", 1, true)
        local second = first ~= nil and string.find(input, ",", first + 1, true) or nil
        local x = first ~= nil and tonumber(string.sub(input, 1, first - 1)) or nil
        local y = first ~= nil and second ~= nil and tonumber(string.sub(input, first + 1, second - 1)) or nil
        local z = second ~= nil and tonumber(string.sub(input, second + 1)) or nil
        if x ~= nil and y ~= nil and z ~= nil and Asset.AnimationSetFloat3Parameter(entity, ikGoalParameter, x, y, z) then
            SetStatus(string.format("IK goal %.2f, %.2f, %.2f", x, y, z))
        else
            SetStatus("IK input must be x,y,z")
        end
    elseif controllerGraphMode == "blend2d" then
        if lateralParameter == nil or forwardParameter == nil or magnitudeParameter == nil then SetStatus("2D parameters unavailable"); return end
        local speedText = speedInput:GetText()
        local comma = string.find(speedText, ",", 1, true)
        local lateral = comma ~= nil and tonumber(string.sub(speedText, 1, comma - 1)) or 0.0
        local forward = comma ~= nil and tonumber(string.sub(speedText, comma + 1)) or value
        local magnitude = if lateral ~= nil and forward ~= nil then math.sqrt(lateral * lateral + forward * forward) else 0.0
        if lateral ~= nil and forward ~= nil and Asset.AnimationSetFloatParameter(entity, lateralParameter, lateral) and Asset.AnimationSetFloatParameter(entity, forwardParameter, forward) and Asset.AnimationSetFloatParameter(entity, magnitudeParameter, magnitude) then
            PreviewSession.SetInput("lateral", lateral)
            PreviewSession.SetInput("forward", forward)
            PreviewSession.SetInput("magnitude", magnitude)
            SetStatus(string.format("2D input %.2f, %.2f", lateral, forward))
        else
            SetStatus("2D write rejected")
        end
    elseif speedParameter ~= nil and Asset.AnimationSetFloatParameter(entity, speedParameter, value) then
        PreviewSession.SetInput("speed", value)
        SetStatus(string.format("controller speed %.2f", value))
    else
        SetStatus("speed write rejected")
    end
end
speedBtn:SetOnMouseUp(function()
    local value = tonumber(speedInput:GetText())
    if value ~= nil or controllerGraphMode == "blend2d" or controllerGraphMode == "ik" then SetControllerSpeed(value or 0.0) else SetStatus("input must be numeric") end
end)
idleBtn:SetOnMouseUp(function() speedInput:SetText("0.0"); SetControllerSpeed(0.0) end)
walkBtn:SetOnMouseUp(function() speedInput:SetText("1.0"); SetControllerSpeed(1.0) end)
runBtn:SetOnMouseUp(function() speedInput:SetText("3.0"); SetControllerSpeed(3.0) end)
skeletonBtn:SetOnMouseUp(function()
    skeletonVisible = not skeletonVisible
    SetCVar("animationDebugSkeletonPath", skeletonInput:GetText())
    SetCVar("animationDebugSkeletonAxes", 1i)
    SetCVar("animationDebugSkeletonEnabled", skeletonVisible and 1i or 0i)
    RefreshButtons()
end)
graphBtn:SetOnMouseUp(function() EditorRegistry.Toggle("AnimationGraph") end)

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

function methods:SpawnController(mode)
    SpawnController(mode or "states")
end

function methods:SetSpeed(value)
    speedInput:SetText(tostring(value))
    SetControllerSpeed(value)
end

function methods:SetDirection(lateral, forward)
    speedInput:SetText(string.format("%.3f,%.3f", lateral, forward))
    SetControllerSpeed(0.0)
end

function methods:SetIKGoal(x, y, z)
    if entity == nil or ikGoalParameter == nil then return false end
    speedInput:SetText(string.format("%.3f,%.3f,%.3f", x, y, z))
    return Asset.AnimationSetFloat3Parameter(entity, ikGoalParameter, x, y, z)
end

function methods:SetHeadGoal(x, y, z)
    if entity == nil or headGoalParameter == nil then return false end
    return Asset.AnimationSetFloat3Parameter(entity, headGoalParameter, x, y, z)
end

function methods:SetLayerWeights(upperBodyWeight, additiveWeight)
    if entity == nil or upperBodyWeightParameter == nil or additiveWeightParameter == nil then return false end
    return Asset.AnimationSetFloatParameter(entity, upperBodyWeightParameter, upperBodyWeight) and Asset.AnimationSetFloatParameter(entity, additiveWeightParameter, additiveWeight)
end

function methods:QueueAction(name, payload)
    return entity ~= nil and Asset.AnimationQueueAction(entity, name, payload or 0)
end

function methods:AttachModel(modelPath, semantic, translation, rotationDegrees, uniformScale)
    if entity == nil then return nil end
    local child = Asset.SpawnModelAtWorldOrigin(modelPath)
    if child == nil then return nil end
    table.insert(attachmentEntities, child)
    local generation = operationGeneration
    local parent = entity
    local offset = translation or vec.New(0.0, 0.0, 0.0)
    local rotation = rotationDegrees or vec.New(0.0, 0.0, 0.0)
    local scale = uniformScale or 1.0
    local function BindWhenReady(attempt)
        if generation ~= operationGeneration or entity ~= parent then return end
        if Asset.BindModelAttachment(parent, child, skeletonInput:GetText(), semantic, offset.x, offset.y, offset.z, rotation.x, rotation.y, rotation.z, scale) then
            SetStatus("attached " .. modelPath .. " to " .. semantic)
        elseif attempt < 240 then
            Scheduler.AfterSeconds(0.05, function() BindWhenReady(attempt + 1) end)
        else
            SetStatus("attachment timed out: " .. modelPath)
        end
    end
    BindWhenReady(1)
    return child
end

function methods:DestroyAttachment(child)
    if child == nil then return end
    for index, attachmentEntity in attachmentEntities do
        if attachmentEntity == child then
            table.remove(attachmentEntities, index)
            break
        end
    end
    Asset.DestroyAnimatedModel(child)
end

function methods:UpdateSecondaryGrip(child, localPoint)
    if entity == nil or child == nil or leftGripGoalParameter == nil or leftGripWeightParameter == nil then return false end
    local point = localPoint or vec.New(0.0, 0.0, 0.0)
    local goal = Asset.GetModelAttachmentLocalPoint(child, point.x, point.y, point.z)
    if goal == nil then return false end
    return Asset.AnimationSetFloat3Parameter(entity, leftGripGoalParameter, goal.x, goal.y, goal.z) and Asset.AnimationSetFloatParameter(entity, leftGripWeightParameter, 1.0)
end

function methods:SetAttachmentTransform(child, translation, rotationDegrees, uniformScale)
    if child == nil then return false end
    return Asset.SetModelAttachmentTransform(child, translation.x, translation.y, translation.z, rotationDegrees.x, rotationDegrees.y, rotationDegrees.z, uniformScale)
end

function methods:GetAttachmentGripDiagnostics(child, primaryPoint, secondaryPoint, handleAxis, bladeEdgeAxis, rightGripAxis, draw, axisLength, positionTolerance, angleTolerance)
    if child == nil then return nil end
    return Asset.GetModelAttachmentGripDiagnostics(child, primaryPoint.x, primaryPoint.y, primaryPoint.z, secondaryPoint.x, secondaryPoint.y, secondaryPoint.z, handleAxis.x, handleAxis.y, handleAxis.z, bladeEdgeAxis.x, bladeEdgeAxis.y, bladeEdgeAxis.z, rightGripAxis.x, rightGripAxis.y, rightGripAxis.z, draw, axisLength, positionTolerance, angleTolerance)
end

function methods:SetDebugView(skeleton, weights, attachments, ik)
    skeletonVisible = skeleton
    SetCVar("animationDebugSkeletonEnabled", skeleton and 1i or 0i)
    SetCVar("animationDebugSkeletonWeights", weights and 1i or 0i)
    SetCVar("animationDebugAttachments", attachments and 1i or 0i)
    SetCVar("animationDebugIK", ik and 1i or 0i)
    RefreshButtons()
end

function methods:GetEntity()
    return entity
end

function methods:DestroyActor()
    DestroyActor()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)
RefreshButtons()
EditorRegistry.Register("AnimationDebug", methods)
