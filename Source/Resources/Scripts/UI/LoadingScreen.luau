export type LoadingScreenAPI =
{
    SetLoadingScreen : (texturePath : string?) -> (),
    IsVisible        : () -> (boolean),
}

local loadingScreenAPI : LoadingScreenAPI = { }

local UIProgressBar = require("@src/API/UI/ProgressBar")

local LOADING_TIPS : { string } =
{
    "Tip: If the dungeon suddenly goes quiet, check what the rogue touched.",
    "Tip: A locked chest is just a treasure chest asking for a rogue.",
    "Tip: The healer cannot mend wounds caused by standing in obvious fire forever.",
    "Tip: When the tank says run, ask which direction after you start running.",
    "Tip: Every ancient ruin has a lever. Few ancient ruins label what it does.",
    "Tip: Pack spare food. Heroism is difficult on an empty stomach.",
    "Tip: If a merchant calls an item priceless, prepare to pay for the compliment.",
    "Tip: A glowing sword is impressive, but it still needs sharpening.",
    "Tip: Never trust a quiet cave, a smiling goblin, or an unlocked treasure room.",
    "Tip: The shortest road often has the longest list of ambushes.",
    "Tip: Before fighting the dragon, check whether your boots are fireproof.",
    "Tip: Mana returns with patience. Angry ogres usually do not.",
    "Tip: If the wizard says the spell is mostly safe, stand behind the warrior.",
    "Tip: Keep one empty bag slot. The rarest loot appears when your bags are full.",
    "Tip: A tavern rumor is free because the dangerous part comes later.",
    "Tip: Skeletons have no pockets, yet adventurers keep searching them.",
    "Tip: The dungeon map is accurate until the walls begin moving.",
    "Tip: Interrupting a villain's speech may save time, but you could miss the quest.",
    "Tip: A party with a plan is strong. A party with snacks is unstoppable.",
    "Tip: When all else fails, blame the warlock. They are usually expecting it.",
}

local previousTipIndex = 0

local function GetRandomLoadingTip() : string
    local tipIndex = math.random(1, #LOADING_TIPS)
    if tipIndex == previousTipIndex then
        tipIndex = tipIndex % #LOADING_TIPS + 1
    end

    previousTipIndex = tipIndex
    return LOADING_TIPS[tipIndex]
end

local function RegisterTemplates()
    UI.RegisterPanelTemplate("LoadingScreenPanel",
    {
        background = "texture/ui/loadingscreen.png",
    });

    UI.RegisterPanelTemplate("LoadingBarFrame",
    {
        color = vec.New(0.025, 0.030, 0.037),
        alpha = 0.96,
        cornerRadius = 3.0,
        borderColor = vec.New(0.24, 0.28, 0.34),
        borderSize = 1.0,
    });

    UI.RegisterPanelTemplate("LoadingBarBackground",
    {
        color = vec.New(0.045, 0.055, 0.068),
        alpha = 1.0,
        cornerRadius = 2.0,
    });

    UI.RegisterPanelTemplate("LoadingBarFill",
    {
        color = vec.New(0.20, 0.46, 0.76),
        alpha = 1.0,
        cornerRadius = 2.0,
    });

    UI.RegisterTextTemplate("LoadingTipText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 14.0,
        color = vec.New(0.92, 0.88, 0.72),
        borderSize = 1.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });
end

--[[
usage:
.SetLoadingScreen(texture)
]]--

RegisterTemplates()

local canvas = UI.GetCanvas("LoadingScreen", vec.New(0, 0), vec.New(1920, 1080)); -- TODO: We don't want to hardcode the reference resolution here

local loadScreenPanel = canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 0i, "LoadingScreenPanel")
loadScreenPanel:SetAnchor(vec.New(0.5, 0.5))
loadScreenPanel:SetRelativePoint(vec.New(0.5, 0.5))
loadScreenPanel:SetEnabled(false)

local loadBarFrame = loadScreenPanel:NewPanel(vec.New(0, 88), vec.New(720, 48), 1i, "LoadingBarFrame")
loadBarFrame:SetAnchor(vec.New(0.5, 0.0))
loadBarFrame:SetRelativePoint(vec.New(0.5, 0.0))

local loadTipText = loadBarFrame:NewText(LOADING_TIPS[1], vec.New(0, 4), 1i, "LoadingTipText")
loadTipText:SetAnchor(vec.New(0.0, 1.0))
loadTipText:SetRelativePoint(vec.New(0.0, 0.0))

local loadBar = UIProgressBar.New(loadBarFrame, vec.New(6, 6), vec.New(708, 36), 0i, {
    backgroundTemplate = "LoadingBarBackground",
    fillTemplate = "LoadingBarFill",
    inset = vec.New(2, 2),
})

local loadingScreenData =
{
    loadingScreenPanel = loadScreenPanel,
    loadingBarFramePanel = loadBarFrame,
    loadingBar = loadBar,

    nameToTextureMap =
    {
        ["None"]        = "texture/ui/loadingscreen.png",
        ["Default"]     = "texture/ui/loadingscreen.png"
    }
}

function loadingScreenAPI.SetLoadingScreen(texturePath)
    local texture = texturePath or "Default"
    local nameToTextureVal = loadingScreenData.nameToTextureMap[texture]
    texture = nameToTextureVal or texture

    loadingScreenData.loadingBar:SetProgress(0)
    loadTipText:SetText(GetRandomLoadingTip())
    loadingScreenData.loadingScreenPanel:SetBackground(texture)
    loadingScreenData.loadingScreenPanel:SetEnabled(true)
end

function loadingScreenAPI.IsVisible() : boolean
    return loadingScreenData.loadingScreenPanel:IsEnabled()
end

local function OnGameUpdated(eventID : integer, data : any)
    local progress = loadingScreenData.loadingBar:GetProgress()
    if (progress >= 1.0) then
        loadingScreenData.loadingScreenPanel:SetEnabled(false)
        return
    end

    local progress = Map.GetLoadingProgress()
    loadingScreenData.loadingBar:SetProgress(progress)
end

local function OnMapLoading(eventID : integer, data : any)
    local mapInternalName = loadingScreenData.nameToTextureMap[data.mapInternalName]
    if mapInternalName == nil then
        mapInternalName = "Default"
    end

    loadingScreenAPI.SetLoadingScreen(mapInternalName)
end

RegisterEvent(GameEvent.Updated, OnGameUpdated)
RegisterEvent(GameEvent.MapLoading, OnMapLoading)

return loadingScreenAPI
