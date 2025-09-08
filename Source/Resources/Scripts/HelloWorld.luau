local ItemUtil = require("@src/API/Game/ItemUtil")
local Bag = require("@src/API/Game/Bag")
local PlayerContainers = require("@src/API/Game/PlayerContainers")

local GameTooltip = require("@src/API/Game/GameTooltip")

local function OnGameLoaded(eventID : number, data : any)
    local luaRamUsage : number = gcinfo()
    local version : vector = Engine.Version

    print(`Message of the day : "{data.motd}"`)
    print(`Engine : {Engine.Name} (Version : {version.x}.{version.y}.{version.z}) - Lua Ram Usage : {luaRamUsage}KB)`)

    local canvas = UI.GetCanvas("Debug", 0, 0, 1920, 1080);
    local engineText = canvas:NewText(`{Engine.Name} (Version : {version.x}.{version.y}.{version.z})`, 5, 5, 0, "DefaultDebugText");
    engineText:SetAnchor(0.0, 0.0)
    engineText:SetRelativePoint(0.0, 0.0)
    engineText:SetAlpha(0.75)
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)


local function OnTriggerEnter(eventID : number, data : any)
    print(`Trigger entered: {data.triggerID}`)
end

local function OnTriggerStay(eventID : number, data : any)
    print(`Trigger staying: {data.triggerID}`)
end

local function OnTriggerExit(eventID : number, data : any)
    print(`Trigger exited: {data.triggerID}`)
end

RegisterEvent(TriggerEvent.OnEnter, OnTriggerEnter)
RegisterEvent(TriggerEvent.OnStay, OnTriggerStay)
RegisterEvent(TriggerEvent.OnExit, OnTriggerExit)