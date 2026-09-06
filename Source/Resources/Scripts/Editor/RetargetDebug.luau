local Button = require("@src/API/UI/Button")
local Registry = require("@src/API/Editor/Registry")
local Window = require("@src/API/Editor/UI/Window")

local canvas = UI.GetCanvas("RetargetDebugEditor", vec.New(0,0), vec.New(1920,1080), false)
local window = Window.New(canvas, vec.New(22,-85), vec.New(350,290), 1100i, {title="Rig Family Preview", titleHeight=20, padding=10, windowColor=vec.New(0.065,0.08,0.10), titleColor=vec.New(0.035,0.045,0.06), alpha=0.96})
local panel = window.root
local methods = {}
local entity = nil
local speed = nil
local layerWeight = nil
local generation = 0
local variant = "proportions"

local function Label(text, y, tint)
    local item = panel:NewText(text, vec.New(12,y), 1i, "DevToolsText")
    item:SetAnchor(vec.New(0,1)); item:SetRelativePoint(vec.New(0,1))
    item:SetColor(tint or vec.New(0.82,0.86,0.91)); item:SetInteractable(false)
    return item
end
Label("BIPEDAL HUMANOID", -32, vec.New(0.30,0.85,0.90))
local targetLabel = Label("Target: proportions", -54)
local stateLabel = Label("No preview actor", -76)
Label("Cyan: canonical   Yellow: arm / leg chains", -218, vec.New(0.65,0.72,0.78))
Label("Purple: optional joints   Pink: helper rules", -238, vec.New(0.65,0.72,0.78))
Label("Mapping uses cooked indices, never bone names", -258, vec.New(0.65,0.72,0.78))

local function CVar(name, value)
    for _, cvar in Editor.GetCVars() do
        if cvar.name == name then Editor.SetCVar(cvar.category,name,value); return end
    end
end
local function ButtonAt(text,x,y,width,callback)
    local button = Button.New(panel,vec.New(x,y),vec.New(width,25),1i,{text=text,textTemplate="DevToolsText",borderColor=vec.New(0.22,0.30,0.38),borderSize=1})
    button:SetAnchor(vec.New(0,1)); button:SetRelativePoint(vec.New(0,1)); button:SetOnMouseUp(callback)
end
function methods:DestroyActor()
    generation += 1
    if entity ~= nil then Asset.DestroyAnimatedModel(entity) end
    entity = nil; speed = nil; layerWeight = nil
end
function methods:Spawn(target, setName, mode)
    self:DestroyActor()
    variant = target or variant
    targetLabel:SetText("Target: " .. variant)
    entity = Asset.SpawnModelAtWorldOrigin("animation/retarget/" .. variant .. ".model")
    local current = generation
    local function Bind(attempt)
        if current ~= generation or entity == nil then return end
        if Asset.BindAnimationController(entity,"animation/retarget/" .. variant .. ".skeleton","animation/retarget/" .. (setName or "showcase") .. ".animationset",mode or "states") then
            speed = Asset.ResolveAnimationParameter(entity,"speed")
            layerWeight = Asset.ResolveAnimationParameter(entity,"upperBodyWeight")
            Camera.ActivateFreeflying()
            Scene.CenterCameraOnEntity(entity,0.55)
            CVar("animationDebugSkeletonPath","animation/retarget/" .. variant .. ".skeleton")
            CVar("animationDebugSkeletonPose",1i)
            CVar("animationDebugSkeletonAxes",0i)
            CVar("animationDebugSkeletonUseSelection",0i)
            stateLabel:SetText("Idle / source Skeleton")
        elseif attempt < 300 then Scheduler.AfterFrames(1,function() Bind(attempt+1) end)
        else stateLabel:SetText("Binding rejected; inspect the asset domains") end
    end
    Scheduler.AfterFrames(2,function() Bind(1) end)
end
function methods:SetSpeed(value)
    if entity == nil or speed == nil then return false end
    stateLabel:SetText(string.format("Speed %.2f / %s",value,if value < 0.1 then "source Skeleton" elseif value < 2 then "renamed Skeleton" else "canonical family"))
    return Asset.AnimationSetFloatParameter(entity,speed,value)
end
function methods:SetLayerWeight(value)
    if entity == nil or layerWeight == nil then return false end
    stateLabel:SetText(string.format("Target arm mask: %.0f%% running pose",value*100))
    return Asset.AnimationSetFloatParameter(entity,layerWeight,value)
end
function methods:SetOverlay(mode)
    CVar("animationDebugSkeletonEnabled",if mode == 0i then 0i else 1i)
    CVar("animationDebugRigFamily",mode)
end
function methods:GetEntity() return entity end
function methods:Show() panel:SetVisible(true) end
function methods:Hide() panel:SetVisible(false) end
function methods:IsVisible() return panel:IsVisible() end
ButtonAt("Reference",12,-103,102,function() methods:Spawn("reference") end)
ButtonAt("Proportions",124,-103,102,function() methods:Spawn("proportions") end)
ButtonAt("New frames",236,-103,102,function() methods:Spawn("frames") end)
ButtonAt("Idle",12,-137,72,function() methods:SetSpeed(0) end)
ButtonAt("Walk",94,-137,72,function() methods:SetSpeed(1) end)
ButtonAt("Run",176,-137,72,function() methods:SetSpeed(3) end)
ButtonAt("Remove",258,-137,80,function() methods:DestroyActor() end)
ButtonAt("Clean",12,-175,72,function() methods:SetOverlay(0i) end)
ButtonAt("Family",94,-175,72,function() methods:SetOverlay(1i) end)
ButtonAt("Mapping",176,-175,72,function() methods:SetOverlay(3i) end)
ButtonAt("Frames",258,-175,80,function() methods:SetOverlay(5i) end)
window.closeButton:SetOnMouseUp(function() methods:Hide() end)
panel:SetVisible(false)
Registry.Register("RetargetDebug",methods)
