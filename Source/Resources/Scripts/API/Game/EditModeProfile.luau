local EditModeDefaults = require("@src/API/Game/EditModeDefaults")

export type ElementLayout =
{
    posX : number,
    posY : number,
}

export type Profile =
{
    name    : string,
    layouts : { [EditModeDefaults.ElementID] : ElementLayout },
}

export type ProfileStore =
{
    activeProfile : string,
    profiles      : { [string] : Profile },
}

export type EditModeProfileAPI =
{
    CreateProfile            : (name : string) -> Profile,
    DeleteProfile            : (name : string) -> (),
    RenameProfile            : (oldName : string, newName : string) -> (),
    GetProfile               : (name : string) -> Profile?,
    GetProfileNames          : () -> { string },
    GetActiveProfileName     : () -> string,
    GetActiveProfile         : () -> Profile,
    SetActiveProfile         : (name : string) -> boolean,
    SaveActiveLayout         : (layouts : { [EditModeDefaults.ElementID] : ElementLayout }) -> boolean,
    ApplyActiveLayout        : (applyPositions : (layouts : { [EditModeDefaults.ElementID] : ElementLayout }) -> ()) -> (),
    Serialize                : () -> string,
    Deserialize              : (json : string) -> (),
    ExportProfile            : (profileName : string?) -> string,
    ImportAsOverride         : (code : string) -> boolean,
    ImportAsNewProfile       : (code : string, name : string?) -> boolean,
    IsDefaultProfile         : (name : string) -> boolean,
    GenerateDefaultProfile   : () -> Profile,
}

local profileAPI : EditModeProfileAPI = { }

local DEFAULT_PROFILE_NAME = "Default"
local CODE_PREFIX = "NVP1:"

local store : ProfileStore = {
    activeProfile = DEFAULT_PROFILE_NAME,
    profiles = {},
}

local nextProfileNumber : number = 1

local function GenerateDefaultLayouts() : { [EditModeDefaults.ElementID] : ElementLayout }
    local layouts : { [EditModeDefaults.ElementID] : ElementLayout } = {}
    for _, elem in EditModeDefaults.Elements do
        layouts[elem.id] = { posX = elem.defaultPosX, posY = elem.defaultPosY }
    end
    return layouts
end

local function GenerateDefaultProfile() : Profile
    local layouts = GenerateDefaultLayouts()
    return { name = DEFAULT_PROFILE_NAME, layouts = layouts }
end

profileAPI.GenerateDefaultProfile = GenerateDefaultProfile

local function EnsureDefaultExists()
    if store.profiles[DEFAULT_PROFILE_NAME] == nil then
        store.profiles[DEFAULT_PROFILE_NAME] = GenerateDefaultProfile()
    end
end

function profileAPI.IsDefaultProfile(name : string) : boolean
    return name == DEFAULT_PROFILE_NAME
end

function profileAPI.CreateProfile(name : string) : Profile
    EnsureDefaultExists()

    if name == DEFAULT_PROFILE_NAME then
        return store.profiles[DEFAULT_PROFILE_NAME]
    end
    if store.profiles[name] ~= nil then
        return store.profiles[name]
    end

    local layouts = GenerateDefaultLayouts()
    local profile : Profile = { name = name, layouts = layouts }
    store.profiles[name] = profile
    return profile
end

function profileAPI.DeleteProfile(name : string)
    if name == DEFAULT_PROFILE_NAME then
        return
    end

    store.profiles[name] = nil

    if store.activeProfile == name then
        store.activeProfile = DEFAULT_PROFILE_NAME
        EnsureDefaultExists()
    end
end

function profileAPI.RenameProfile(oldName : string, newName : string)
    if oldName == DEFAULT_PROFILE_NAME or newName == DEFAULT_PROFILE_NAME then
        return
    end

    local profile = store.profiles[oldName]
    if profile == nil then
        return
    end

    profile.name = newName
    store.profiles[newName] = profile
    store.profiles[oldName] = nil

    if store.activeProfile == oldName then
        store.activeProfile = newName
    end
end

function profileAPI.GetProfile(name : string) : Profile?
    EnsureDefaultExists()
    return store.profiles[name]
end

function profileAPI.GetProfileNames() : { string }
    EnsureDefaultExists()
    local names : { string } = {}
    for name in store.profiles do
        if name ~= DEFAULT_PROFILE_NAME then
            table.insert(names, name)
        end
    end
    table.sort(names)
    return names
end

function profileAPI.GetActiveProfileName() : string
    return store.activeProfile
end

function profileAPI.GetActiveProfile() : Profile
    EnsureDefaultExists()
    local profile = store.profiles[store.activeProfile]
    if profile == nil then
        store.activeProfile = DEFAULT_PROFILE_NAME
        return store.profiles[DEFAULT_PROFILE_NAME]
    end
    return profile
end

function profileAPI.SetActiveProfile(name : string) : boolean
    EnsureDefaultExists()
    if store.profiles[name] == nil then
        return false
    end
    store.activeProfile = name
    return true
end

function profileAPI.SaveActiveLayout(layouts : { [EditModeDefaults.ElementID] : ElementLayout }) : boolean
    local profile = profileAPI.GetActiveProfile()
    if profileAPI.IsDefaultProfile(profile.name) then
        return false
    end
    for elementID, layout in layouts do
        profile.layouts[elementID] = { posX = layout.posX, posY = layout.posY }
    end
    return true
end

function profileAPI.ApplyActiveLayout(applyPositions : (layouts : { [EditModeDefaults.ElementID] : ElementLayout }) -> ())
    local profile = profileAPI.GetActiveProfile()
    applyPositions(profile.layouts)
end

local function SerializeImpl(storeToSerialize : ProfileStore) : string
    local parts : { string } = {}
    table.insert(parts, '{"v":1,"activeProfile":')
    table.insert(parts, string.format("%q", storeToSerialize.activeProfile))
    table.insert(parts,',"profiles":{')

    local firstProfile = true
    for name, profile in storeToSerialize.profiles do
        if not firstProfile then
            table.insert(parts, ",")
        end
        firstProfile = false

        table.insert(parts, string.format("%q", name))
        table.insert(parts, ':{' .. string.format("%q", "layouts") .. ':{')
        local firstLayout = true
        for elementID, layout in profile.layouts do
            if not firstLayout then
                table.insert(parts, ",")
            end
            firstLayout = false
            table.insert(parts, string.format("%q:{%q:%g,%q:%g}", elementID, "posX", layout.posX, "posY", layout.posY))
        end
        table.insert(parts, "}}")
    end
    table.insert(parts, "}}")

    return table.concat(parts)
end

function profileAPI.Serialize() : string
    return SerializeImpl(store)
end

function profileAPI.Deserialize(json : string)
    -- Minimal JSON parse: extract the fields we care about.
    -- This is intentionally simple; a real implementation would use a proper JSON library.
    -- For now, we support round-tripping through our own Serialize output.

    -- Find activeProfile
    local activeMatch = json:match('"activeProfile"%s*:%s*"([^"]*)"')
    if activeMatch then
        store.activeProfile = activeMatch
    end

    -- Reset profiles
    store.profiles = {}

    -- Parse profiles block: find each "name":{...} pair
    local profilesBlock = json:match('"profiles"%s*:%s*(%b{})')
    if profilesBlock == nil then
        store.activeProfile = DEFAULT_PROFILE_NAME
        store.profiles[DEFAULT_PROFILE_NAME] = GenerateDefaultProfile()
        return
    end

    -- Iterate profile entries
    for profileName, profileBody in profilesBlock:gmatch('"([^"]+)"%s*:%s*(%b{})') do
        if profileName ~= "layouts" then
            local layouts : { [EditModeDefaults.ElementID] : ElementLayout } = {}

            local layoutsBlock = profileBody:match('"layouts"%s*:%s*(%b{})')
            if layoutsBlock then
                for elementID, posBlock in layoutsBlock:gmatch('"([^"]+)"%s*:%s*(%b{})') do
                    local posX = tonumber(posBlock:match('"posX"%s*:%s*([%d%.%-]+)')) or 0
                    local posY = tonumber(posBlock:match('"posY"%s*:%s*([%d%.%-]+)')) or 0
                    layouts[elementID] = { posX = posX, posY = posY }
                end
            end

            if profileName ~= DEFAULT_PROFILE_NAME then
                local normalizedLayouts = GenerateDefaultLayouts()
                for elementID, layout in layouts do
                    normalizedLayouts[elementID] = layout
                end
                store.profiles[profileName] = { name = profileName, layouts = normalizedLayouts }
            end
        end
    end

    store.profiles[DEFAULT_PROFILE_NAME] = GenerateDefaultProfile()
    if store.profiles[store.activeProfile] == nil then
        store.activeProfile = DEFAULT_PROFILE_NAME
    end
end

local function EncodeBase64(data : string) : string
    local b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    return ((data:gsub(".", function(x)
        local r, b2 = "", x:byte()
        for i = 8, 1, -1 do r = r .. (b2 % 2 ^ i - b2 % 2 ^ (i - 1) > 0 and "1" or "0") end
        return r
    end) .. "0000"):gsub("%d%d%d?%d?%d?%d?", function(x)
        if #x < 6 then return "" end
        local c = 0
        for i = 1, 6 do c = c + (x:sub(i, i) == "1" and 2 ^ (6 - i) or 0) end
        return b:sub(c + 1, c + 1)
    end) .. ({ "", "==", "=" })[#data % 3 + 1])
end

local function DecodeBase64(data : string) : string
    local b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    data = string.gsub(data, "[^" .. b .. "=]", "")
    return (data:gsub(".", function(x)
        if x == "=" then return "" end
        local r, f = "", (b:find(x) - 1)
        for i = 6, 1, -1 do r = r .. (f % 2 ^ i - f % 2 ^ (i - 1) > 0 and "1" or "0") end
        return r
    end):gsub("%d%d%d?%d?%d?%d?%d?%d?", function(x)
        if #x ~= 8 then return "" end
        local c = 0
        for i = 1, 8 do c = c + (x:sub(i, i) == "1" and 2 ^ (8 - i) or 0) end
        return string.char(c)
    end))
end

function profileAPI.ExportProfile(profileName : string?) : string
    EnsureDefaultExists()
    local name = profileName or store.activeProfile
    local profile = store.profiles[name]
    if profile == nil then
        return CODE_PREFIX
    end

    local exportData = SerializeImpl({ activeProfile = name, profiles = { [name] = profile } })
    return CODE_PREFIX .. EncodeBase64(exportData)
end

local function ParseImportCode(code : string) : Profile?
    if string.sub(code, 1, #CODE_PREFIX) ~= CODE_PREFIX then
        return nil
    end

    local encoded = string.sub(code, #CODE_PREFIX + 1)
    local decoded = DecodeBase64(encoded)
    if decoded == nil or decoded == "" then
        return nil
    end

    -- Parse the exported profile
    local tempStore : ProfileStore = { activeProfile = "", profiles = {} }

    local activeMatch = decoded:match('"activeProfile"%s*:%s*"([^"]*)"')
    if activeMatch then
        tempStore.activeProfile = activeMatch
    end

    local profilesBlock = decoded:match('"profiles"%s*:%s*(%b{})')
    if profilesBlock == nil then
        return nil
    end

    for profileName, profileBody in profilesBlock:gmatch('"([^"]+)"%s*:%s*(%b{})') do
        if profileName ~= "layouts" then
            local layouts : { [EditModeDefaults.ElementID] : ElementLayout } = {}
            local layoutsBlock = profileBody:match('"layouts"%s*:%s*(%b{})')
            if layoutsBlock then
                for elementID, posBlock in layoutsBlock:gmatch('"([^"]+)"%s*:%s*(%b{})') do
                    local posX = tonumber(posBlock:match('"posX"%s*:%s*([%d%.%-]+)')) or 0
                    local posY = tonumber(posBlock:match('"posY"%s*:%s*([%d%.%-]+)')) or 0
                    layouts[elementID] = { posX = posX, posY = posY }
                end
            end
            return { name = profileName, layouts = layouts }
        end
    end

    return nil
end

function profileAPI.ImportAsOverride(code : string) : boolean
    local imported = ParseImportCode(code)
    if imported == nil then
        return false
    end

    EnsureDefaultExists()
    local active = profileAPI.GetActiveProfile()
    if profileAPI.IsDefaultProfile(active.name) then
        return false
    end
    for elementID, layout in imported.layouts do
        active.layouts[elementID] = { posX = layout.posX, posY = layout.posY }
    end
    return true
end

function profileAPI.ImportAsNewProfile(code : string, name : string?) : boolean
    local imported = ParseImportCode(code)
    if imported == nil then
        return false
    end

    EnsureDefaultExists()
    local profileName = name
    if profileName == nil or profileName == "" then
        repeat
            profileName = "Profile " .. tostring(nextProfileNumber)
            nextProfileNumber += 1
        until store.profiles[profileName] == nil
    end
    if profileName == DEFAULT_PROFILE_NAME then
        return false
    end

    local layouts = GenerateDefaultLayouts()
    for elementID, layout in imported.layouts do
        layouts[elementID] = { posX = layout.posX, posY = layout.posY }
    end
    local profile : Profile = { name = profileName, layouts = layouts }
    store.profiles[profileName] = profile
    store.activeProfile = profileName
    return true
end

return profileAPI
