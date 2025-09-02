local loadingScreenAPI = { }

local UISlider = require("@src/API/UI/Slider")

--[[
usage:
.SetLoadingScreen(texture)
]]--

local canvas = UI.GetCanvas("LoadingScreen", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here

local loadScreenPanel = canvas:NewPanel(0, 0, 1920, 1080, 0, "DefaultLoadingScreen")
loadScreenPanel:SetAnchor(0.5, 0.5)
loadScreenPanel:SetRelativePoint(0.5, 0.5)
loadScreenPanel:SetEnabled(false)

local loadBarFrame = loadScreenPanel:NewPanel(0, 64, 1024, 64, 1, "DefaultLoadingBarFrame")
loadBarFrame:SetAnchor(0.5, 0.0)
loadBarFrame:SetRelativePoint(0.5, 0.0)

local loadBarSlider = UISlider.NewSlider(loadBarFrame, -1, 10, 1020, 48, 0, {
    backgroundTemplate = "DefaultLoadingBarBackground",
    fillTemplate = "DefaultLoadingBarFill",
    fillSizeX = 954,
    fillSizeY = 25,
    fillPosX = 32
});
loadBarSlider.slider:SetAnchor(0.0, 0.0)
loadBarSlider.slider:SetRelativePoint(0.0, 0.0)
loadBarSlider.fill:SetAnchor(0.0, 0.5)
loadBarSlider.fill:SetRelativePoint(0, 0.5)

__LoadingScreenData =
{
    loadingScreenPanel = loadScreenPanel,
    loadingBarFramePanel = loadBarFrame,
    loadingBarSlider = loadBarSlider,

    nameToTextureMap =
    {
        ["None"]                = "Data/Texture/interface/glues/loading.dds",
        ["Default"]             = "Data/Texture/interface/glues/loading.dds",
        ["Azeroth"]             = "Data/Texture/interface/glues/loadingscreens/loadscreeneasternkingdom.dds",
        ["Kalimdor"]            = "Data/Texture/interface/glues/loadingscreens/loadscreenkalimdor.dds",
        ["Outland"]             = "Data/Texture/interface/glues/loadingscreens/loadscreenoutland.dds",
        ["Northrend"]           = "Data/Texture/interface/glues/loadingscreens/loadscreennorthrend.dds",
        ["PVPZone04"]           = "Data/Texture/interface/glues/loadingscreens/loadscreenarathibasin.dds",
        ["PVPZone03"]           = "Data/Texture/interface/glues/loadingscreens/loadscreenwarsonggulch.dds",
        ["ScarletMonastery"]    = "Data/Texture/interface/glues/loadingscreens/loadscreenmonastery.dds"
    }
}

function loadingScreenAPI.SetLoadingScreen(texturePath)
    local texture = texturePath or "Default"
    local nameToTextureVal = __LoadingScreenData.nameToTextureMap[texture]
    texture = nameToTextureVal or texture
    
    __LoadingScreenData.loadingBarSlider:SetProgress(0)
    __LoadingScreenData.loadingScreenPanel:SetBackground(texture)
    __LoadingScreenData.loadingScreenPanel:SetEnabled(true)
end

local function OnGameUpdated(eventID : number, deltaTime : number)

    local progress = __LoadingScreenData.loadingBarSlider:GetProgress()
    if (progress >= 1.0) then
        __LoadingScreenData.loadingScreenPanel:SetEnabled(false)
        return
    end

    local progress = GetMapLoadingProgress()
    __LoadingScreenData.loadingBarSlider:SetProgress(progress)
end

local function OnMapLoading(eventID : number, mapName : string)

    local mapInternalName = __LoadingScreenData.nameToTextureMap[mapName]
    if mapInternalName == nil then
        mapInternalName = "Default"
    end

    loadingScreenAPI.SetLoadingScreen(mapInternalName)
end

RegisterGameEvent(GameEvent.Updated, OnGameUpdated)
RegisterGameEvent(GameEvent.MapLoading, OnMapLoading)

return loadingScreenAPI