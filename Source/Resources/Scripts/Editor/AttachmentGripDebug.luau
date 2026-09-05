local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local EditorWindow = require("@src/API/Editor/UI/Window")
local InputBox = require("@src/API/UI/InputBox")
local Profiles = require("@src/API/Editor/AttachmentGripProfiles")

require("@src/API/Editor/UI/MenuBar")

local PANEL_W = 700
local PANEL_H = 520
local TITLE_H = 16
local ROW_H = 20
local GAP = 5
local PAD = 9
local LABEL_W = 118
local LAYER = 1110i
local TEXT_TEMPLATE = "DevToolsText"
local profile = Profiles.CorruptedAshbringer

local canvas = UI.GetCanvas("AttachmentGripDebugEditor", vec.New(0, 0), vec.New(1920, 1080), false)
local editorWindow = EditorWindow.New(canvas, vec.New(1160, -90), vec.New(PANEL_W, PANEL_H), LAYER, {
    title = "Attachment Grip Inspector",
    titleHeight = TITLE_H,
    padding = PAD,
    windowColor = vec.New(0.075, 0.08, 0.095),
    titleColor = vec.New(0.045, 0.05, 0.065),
    alpha = 0.97,
})
local panel = editorWindow.root
local closeButton = editorWindow.closeButton
local generation = 0
local swordEntity = nil
local overlayEnabled = true
local frozen = false
local metricsText
local detailText
local statusText

local function MakeButton(text)
    return {
        text = text,
        textTemplate = TEXT_TEMPLATE,
        borderColor = vec.New(0.28, 0.31, 0.38),
        borderSize = 1,
    }
end

local function AddText(text, x, y, color)
    local label = panel:NewText(text, vec.New(x, y), 1i, TEXT_TEMPLATE)
    label:SetAnchor(vec.New(0.0, 1.0))
    label:SetRelativePoint(vec.New(0.0, 1.0))
    label:SetColor(color or vec.New(0.82, 0.84, 0.9))
    label:SetInteractable(false)
    return label
end

local function AddInput(x, y, width, value)
    local initialText = if type(value) == "number" then string.format("%.4f", value) else tostring(value)
    local input = InputBox.New(panel, vec.New(x, y), vec.New(width, ROW_H), 1i, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = TEXT_TEMPLATE,
        defaultText = initialText,
    })
    input:SetAnchor(vec.New(0.0, 1.0))
    input:SetRelativePoint(vec.New(0.0, 1.0))
    input:SetTextVerticallyCentered()
    return input
end

local function AddButton(x, y, width, text)
    local button = Button.New(panel, vec.New(x, y), vec.New(width, ROW_H), 1i, MakeButton(text))
    button:SetAnchor(vec.New(0.0, 1.0))
    button:SetRelativePoint(vec.New(0.0, 1.0))
    return button
end

local function AddVectorRow(label, y, value)
    AddText(label, PAD, y - 3)
    local available = PANEL_W - PAD * 2 - LABEL_W
    local width = (available - GAP * 2) / 3
    local x = PAD + LABEL_W
    return {
        AddInput(x, y, width, value.x),
        AddInput(x + width + GAP, y, width, value.y),
        AddInput(x + (width + GAP) * 2, y, width, value.z),
    }
end

local function ReadNumber(input)
    return tonumber(input:GetText())
end

local function ReadVector(inputs)
    local x = ReadNumber(inputs[1])
    local y = ReadNumber(inputs[2])
    local z = ReadNumber(inputs[3])
    if x == nil or y == nil or z == nil then return nil end
    return vec.New(x, y, z)
end

local y = -(TITLE_H + 10)
AddText("Grip profile (editable)", PAD, y - 3, vec.New(0.48, 0.76, 1.0))
AddText("RGB = frames   cyan = hands   orange = weapon grips   MAGENTA = HANDLE   YELLOW ARROW = THUMB", 170, y - 3, vec.New(0.62, 0.66, 0.74))
y -= ROW_H + GAP

local translationInputs = AddVectorRow("Attachment T", y, profile.translation)
y -= ROW_H + GAP
local rotationInputs = AddVectorRow("Attachment R deg", y, profile.rotation)
y -= ROW_H + GAP
AddText("Uniform scale", PAD, y - 3)
local scaleInput = AddInput(PAD + LABEL_W, y, 110, profile.scale)
y -= ROW_H + GAP
local primaryInputs = AddVectorRow("Primary grip", y, profile.primaryGrip)
y -= ROW_H + GAP
local secondaryInputs = AddVectorRow("Secondary grip", y, profile.secondaryGrip)
y -= ROW_H + GAP
local handleInputs = AddVectorRow("Handle axis", y, profile.handleAxis)
y -= ROW_H + GAP
local edgeInputs = AddVectorRow("Blade edge axis", y, profile.bladeEdgeAxis)
y -= ROW_H + GAP
local rightGripInputs = AddVectorRow("Right thumb axis", y, profile.rightGripAxis)
y -= ROW_H + GAP

AddText("Position tolerance", PAD, y - 3)
local positionToleranceInput = AddInput(PAD + LABEL_W, y, 90, profile.positionTolerance)
AddText("m", PAD + LABEL_W + 96, y - 3, vec.New(0.62, 0.66, 0.74))
AddText("Angle tolerance", 330, y - 3)
local angleToleranceInput = AddInput(448, y, 90, profile.angleTolerance)
AddText("deg", 544, y - 3, vec.New(0.62, 0.66, 0.74))
y -= ROW_H + GAP

local spawnButton = AddButton(PAD, y, 102, "Spawn test")
local applyButton = AddButton(PAD + 107, y, 88, "Apply")
local comboButton = AddButton(PAD + 200, y, 102, "Sword combo")
local freezeButton = AddButton(PAD + 307, y, 82, "Freeze")
local overlayButton = AddButton(PAD + 394, y, 92, "Overlay")
local printButton = AddButton(PAD + 491, y, 92, "Print profile")
local destroyButton = AddButton(PAD + 588, y, 102, "Destroy")
y -= ROW_H + GAP

local frontButton = AddButton(PAD, y, 92, "Front")
local backButton = AddButton(PAD + 97, y, 92, "Back")
local leftButton = AddButton(PAD + 194, y, 92, "Left")
local rightButton = AddButton(PAD + 291, y, 92, "Right")
local topButton = AddButton(PAD + 388, y, 92, "Top")
local frameButton = AddButton(PAD + 485, y, 102, "Current view")
y -= ROW_H + 10

AddText("Live validation (updates every frame)", PAD, y, vec.New(0.48, 0.76, 1.0))
y -= ROW_H
metricsText = AddText("No attachment to inspect", PAD, y, vec.New(0.82, 0.84, 0.9))
detailText = AddText("The gate checks both contacts, handle alignment, and the closest wrist axes.", PAD, y - 22, vec.New(0.62, 0.66, 0.74))
statusText = AddText("Status: ready", PAD, y - 44, vec.New(0.62, 0.66, 0.74))

local function SetStatus(text)
    statusText:SetText("Status: " .. text)
end

local function SetCVar(name, value)
    for _, cvar in Editor.GetCVars() do
        if cvar.name == name then
            Editor.SetCVar(cvar.category, cvar.name, value)
            return true
        end
    end
    return false
end

local function ReadConfiguration()
    local translation = ReadVector(translationInputs)
    local rotation = ReadVector(rotationInputs)
    local primary = ReadVector(primaryInputs)
    local secondary = ReadVector(secondaryInputs)
    local handle = ReadVector(handleInputs)
    local edge = ReadVector(edgeInputs)
    local rightGrip = ReadVector(rightGripInputs)
    local scale = ReadNumber(scaleInput)
    local positionTolerance = ReadNumber(positionToleranceInput)
    local angleTolerance = ReadNumber(angleToleranceInput)
    if translation == nil or rotation == nil or primary == nil or secondary == nil or handle == nil or edge == nil or rightGrip == nil or scale == nil or positionTolerance == nil or angleTolerance == nil then return nil end
    return {
        translation = translation,
        rotation = rotation,
        primary = primary,
        secondary = secondary,
        handle = handle,
        edge = edge,
        rightGrip = rightGrip,
        scale = scale,
        positionTolerance = positionTolerance,
        angleTolerance = angleTolerance,
    }
end

local function GetAnimationDebug()
    return EditorRegistry.Get("AnimationDebug")
end

local function ApplyConfiguration()
    local animationDebug = GetAnimationDebug()
    local config = ReadConfiguration()
    if animationDebug == nil or swordEntity == nil or config == nil then SetStatus("invalid configuration or no attachment"); return false end
    if not animationDebug:SetAttachmentTransform(swordEntity, config.translation, config.rotation, config.scale) then SetStatus("attachment transform rejected"); return false end
    if not animationDebug:UpdateSecondaryGrip(swordEntity, config.secondary) then SetStatus("secondary grip rejected"); return false end
    SetStatus("live attachment and grip updated")
    return true
end

local function RefreshDiagnostics()
    local animationDebug = GetAnimationDebug()
    local config = ReadConfiguration()
    if animationDebug == nil or swordEntity == nil or config == nil then
        metricsText:SetText("No attachment to inspect")
        return nil
    end
    local diagnostics = animationDebug:GetAttachmentGripDiagnostics(swordEntity, config.primary, config.secondary, config.handle, config.edge, config.rightGrip, overlayEnabled, profile.axisLength, config.positionTolerance, config.angleTolerance)
    if diagnostics == nil then
        metricsText:SetText("Waiting for current pose and attachment transforms")
        return nil
    end
    local verdict = diagnostics.passed and "PASS" or "FAIL"
    metricsText:SetColor(diagnostics.passed and vec.New(0.38, 0.9, 0.5) or vec.New(1.0, 0.38, 0.32))
    metricsText:SetText(string.format("%s | primary %.2f cm | secondary %.2f cm | handle vs hands %.1f deg", verdict, diagnostics.primaryPositionError * 100.0, diagnostics.secondaryPositionError * 100.0, diagnostics.handleHandSpanDegrees))
    local ikError = diagnostics.ikFinalError
    local reachStatus = if ikError == nil then "pending" elseif diagnostics.ikClamped then "clamped" else "solved"
    local ikText = if ikError == nil then "--" else string.format("%.2f", ikError * 100.0)
    detailText:SetText(string.format("Right thumb vs handle %.1f deg | left grip axis %s %.1f deg | IK %s cm (%s)", diagnostics.rightGripHandleDegrees, ({ "X", "Y", "Z" })[integer.tonumber(diagnostics.secondaryClosestAxis)], diagnostics.secondaryClosestAxisDegrees, ikText, reachStatus))
    return diagnostics
end

local refreshGeneration = 0
local function BeginRefresh()
    refreshGeneration += 1
    local refresh = refreshGeneration
    local function Tick()
        if refresh ~= refreshGeneration or not panel:IsVisible() then return end
        RefreshDiagnostics()
        Scheduler.AfterFrames(1, Tick)
    end
    Tick()
end

local function SpawnTest()
    local animationDebug = GetAnimationDebug()
    if animationDebug == nil then SetStatus("Animation Debug editor is unavailable"); return end
    generation += 1
    local request = generation
    swordEntity = nil
    animationDebug:SpawnController("attachment")
    local function WaitForController(attempt)
        if request ~= generation then return end
        local actor = animationDebug:GetEntity()
        if actor ~= nil and Asset.GetAnimationControllerState(actor) ~= nil then
            animationDebug:SetSpeed(0.0)
            local config = ReadConfiguration()
            if config == nil then SetStatus("invalid numeric configuration"); return end
            swordEntity = animationDebug:AttachModel(profile.model, profile.semantic, config.translation, config.rotation, config.scale)
            if swordEntity == nil then SetStatus("weapon spawn rejected"); return end
            Scheduler.AfterSeconds(0.4, function()
                if request ~= generation then return end
                ApplyConfiguration()
                Scene.CenterCameraOnEntity(actor, 0.68)
            end)
        elseif attempt < 360 then
            Scheduler.AfterFrames(1, function() WaitForController(attempt + 1) end)
        else
            SetStatus("controller bind timed out")
        end
    end
    Scheduler.AfterFrames(1, function() WaitForController(1) end)
end

local function Frame(yaw, pitch)
    local animationDebug = GetAnimationDebug()
    local actor = animationDebug ~= nil and animationDebug:GetEntity() or nil
    if actor == nil then SetStatus("actor is unavailable"); return end
    Scene.SetTransform(actor, vec.New(0.0, 0.0, 0.0), vec.New(pitch, yaw, 0.0), vec.New(1.0, 1.0, 1.0))
    Scheduler.AfterFrames(2, function()
        if Scene.CenterCameraOnEntity(actor, 0.68) then SetStatus(string.format("inspection view yaw %.0f pitch %.0f", yaw, pitch)) else SetStatus("actor bounds not ready") end
    end)
end

spawnButton:SetOnMouseUp(SpawnTest)
applyButton:SetOnMouseUp(ApplyConfiguration)
comboButton:SetOnMouseUp(function()
    local animationDebug = GetAnimationDebug()
    if animationDebug ~= nil and animationDebug:QueueAction("sword_combo", 1) then SetStatus("sword combo queued") else SetStatus("action rejected") end
end)
freezeButton:SetOnMouseUp(function()
    frozen = not frozen
    SetCVar("animationControllerTimeScale", frozen and 0.0 or 1.0)
    freezeButton:SetHighlighted(frozen)
    SetStatus(frozen and "controller frozen for inspection" or "controller resumed")
end)
overlayButton:SetOnMouseUp(function()
    overlayEnabled = not overlayEnabled
    overlayButton:SetHighlighted(overlayEnabled)
    SetStatus(overlayEnabled and "diagnostic overlay enabled" or "diagnostic overlay hidden")
end)
printButton:SetOnMouseUp(function()
    local config = ReadConfiguration()
    if config == nil then SetStatus("invalid numeric configuration"); return end
    print(string.format("ATTACHMENT_GRIP_PROFILE translation=vec.New(%.6f, %.6f, %.6f) rotation=vec.New(%.3f, %.3f, %.3f) scale=%.6f primary=vec.New(%.6f, %.6f, %.6f) secondary=vec.New(%.6f, %.6f, %.6f) handle=vec.New(%.6f, %.6f, %.6f) edge=vec.New(%.6f, %.6f, %.6f) rightGrip=vec.New(%.6f, %.6f, %.6f)", config.translation.x, config.translation.y, config.translation.z, config.rotation.x, config.rotation.y, config.rotation.z, config.scale, config.primary.x, config.primary.y, config.primary.z, config.secondary.x, config.secondary.y, config.secondary.z, config.handle.x, config.handle.y, config.handle.z, config.edge.x, config.edge.y, config.edge.z, config.rightGrip.x, config.rightGrip.y, config.rightGrip.z))
    SetStatus("profile written to log")
end)
destroyButton:SetOnMouseUp(function()
    generation += 1
    swordEntity = nil
    local animationDebug = GetAnimationDebug()
    if animationDebug ~= nil then animationDebug:DestroyActor() end
    SetStatus("test actor destroyed")
end)
frontButton:SetOnMouseUp(function() Frame(0.0, 0.0) end)
backButton:SetOnMouseUp(function() Frame(180.0, 0.0) end)
leftButton:SetOnMouseUp(function() Frame(90.0, 0.0) end)
rightButton:SetOnMouseUp(function() Frame(-90.0, 0.0) end)
topButton:SetOnMouseUp(function() Frame(0.0, -70.0) end)
frameButton:SetOnMouseUp(function()
    local animationDebug = GetAnimationDebug()
    local actor = animationDebug ~= nil and animationDebug:GetEntity() or nil
    if actor ~= nil and Scene.CenterCameraOnEntity(actor, 0.68) then SetStatus("camera reframed without changing angle") else SetStatus("actor bounds not ready") end
end)
closeButton:SetOnMouseUp(function() panel:SetVisible(false); refreshGeneration += 1 end)

local methods = {}

function methods:Show()
    panel:SetVisible(true)
    overlayButton:SetHighlighted(overlayEnabled)
    freezeButton:SetHighlighted(frozen)
    BeginRefresh()
end

function methods:Hide()
    panel:SetVisible(false)
    refreshGeneration += 1
end

function methods:IsVisible()
    return panel:IsVisible()
end

function methods:SpawnTest()
    SpawnTest()
end

function methods:GetSwordEntity()
    return swordEntity
end

function methods:Apply()
    return ApplyConfiguration()
end

function methods:GetDiagnostics()
    return RefreshDiagnostics()
end

function methods:SetView(yaw, pitch)
    Frame(yaw, pitch)
end

function methods:QueueCombo()
    local animationDebug = GetAnimationDebug()
    return animationDebug ~= nil and animationDebug:QueueAction("sword_combo", 1)
end

function methods:DestroyTest()
    generation += 1
    swordEntity = nil
    local animationDebug = GetAnimationDebug()
    if animationDebug ~= nil then animationDebug:DestroyActor() end
end

EditorRegistry.Register("AttachmentGripDebug", methods)
