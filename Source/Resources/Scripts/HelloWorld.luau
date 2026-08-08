local ItemUtil = require("@src/API/Game/ItemUtil")
local Bag = require("@src/API/Game/UI/Bag")
local PlayerContainers = require("@src/API/Game/PlayerContainers")

local GameTooltip = require("@src/API/Game/UI/GameTooltip")

local function OnGameLoaded(eventID : integer, data : any)
    local luaRamUsage : number = gcinfo()
    local version : vec = Engine.Version

    print(`Message of the day : "{data.motd}"`)
    print(`Engine : {Engine.Name} (Version : {version.x}.{version.y}.{version.z}) - Lua Ram Usage : {luaRamUsage}KB)`)
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)


local function OnTriggerEnter(eventID : integer, data : any)
    print(`Trigger entered: {data.triggerID}`)
end

local function OnTriggerStay(eventID : integer, data : any)
    print(`Trigger staying: {data.triggerID}`)
end

local function OnTriggerExit(eventID : integer, data : any)
    print(`Trigger exited: {data.triggerID}`)
end

RegisterEvent(TriggerEvent.OnEnter, OnTriggerEnter)
RegisterEvent(TriggerEvent.OnStay, OnTriggerStay)
RegisterEvent(TriggerEvent.OnExit, OnTriggerExit)