local function OnGameLoaded(eventID : number, motd : string)
    local luaRamUsage : number = gcinfo()
    local version : Vector3 = Engine.Version
    
    print(`Message of the day : "{motd}"`)
    print(`Engine : {Engine.Name} (Version : {version.x}.{version.y}.{version.z}) - Lua Ram Usage : {luaRamUsage}KB)`)
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)