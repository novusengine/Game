export type CategoryID = string
export type ElementID = string

export type CategoryDef =
{
    id             : CategoryID,
    displayName    : string,
    defaultEnabled : boolean,
    color          : vec,
}

export type ElementDef =
{
    id             : ElementID,
    category       : CategoryID,
    displayName    : string,
    defaultEnabled : boolean,
    defaultPosX    : number,
    defaultPosY    : number,
}

export type EditModeDefaultsAPI =
{
    CategoryIDs : { [string] : CategoryID },
    ElementIDs  : { [string] : ElementID },
    Categories  : { CategoryDef },
    Elements    : { ElementDef },
    GetCategory : (id : CategoryID) -> CategoryDef?,
    GetElement  : (id : ElementID) -> ElementDef?,
    GetElementsByCategory : (categoryID : CategoryID) -> { ElementDef },
}

local defaultsAPI : EditModeDefaultsAPI = { }

defaultsAPI.CategoryIDs = {
    UnitFrames  = "UnitFrames"  :: CategoryID,
    ActionBars  = "ActionBars"  :: CategoryID,
    Chat        = "Chat"        :: CategoryID,
    MicroMenu   = "MicroMenu"   :: CategoryID,
    Nameplates  = "Nameplates"  :: CategoryID,
    Bags        = "Bags"        :: CategoryID,
}

defaultsAPI.ElementIDs = {
    PlayerUnitFrame    = "PlayerUnitFrame"    :: ElementID,
    TargetUnitFrame    = "TargetUnitFrame"    :: ElementID,
    PlayerBuffBar      = "PlayerBuffBar"      :: ElementID,
    PlayerDebuffBar    = "PlayerDebuffBar"    :: ElementID,
    TargetBuffBar      = "TargetBuffBar"      :: ElementID,
    TargetDebuffBar    = "TargetDebuffBar"    :: ElementID,
    ActionBar          = "ActionBar"          :: ElementID,
    ChatWindow         = "ChatWindow"         :: ElementID,
    MicroMenuBar       = "MicroMenuBar"       :: ElementID,
    Nameplates         = "Nameplates"         :: ElementID,
}

local CATEGORY_COLORS = {
    [defaultsAPI.CategoryIDs.UnitFrames]  = vec.New(0.20, 0.50, 0.80),
    [defaultsAPI.CategoryIDs.ActionBars]  = vec.New(0.80, 0.50, 0.20),
    [defaultsAPI.CategoryIDs.Chat]        = vec.New(0.30, 0.70, 0.30),
    [defaultsAPI.CategoryIDs.MicroMenu]   = vec.New(0.60, 0.60, 0.60),
    [defaultsAPI.CategoryIDs.Nameplates]  = vec.New(0.70, 0.30, 0.70),
    [defaultsAPI.CategoryIDs.Bags]        = vec.New(0.80, 0.80, 0.20),
}

local CID = defaultsAPI.CategoryIDs
local EID = defaultsAPI.ElementIDs

defaultsAPI.Categories = {
    { id = CID.UnitFrames, displayName = "Unit Frames", defaultEnabled = true,  color = CATEGORY_COLORS[CID.UnitFrames] },
    { id = CID.ActionBars, displayName = "Action Bars", defaultEnabled = true,  color = CATEGORY_COLORS[CID.ActionBars] },
    { id = CID.Chat,       displayName = "Chat",        defaultEnabled = false, color = CATEGORY_COLORS[CID.Chat]       },
    { id = CID.MicroMenu,  displayName = "Micro Menu",  defaultEnabled = false, color = CATEGORY_COLORS[CID.MicroMenu]  },
    { id = CID.Nameplates, displayName = "Nameplates",  defaultEnabled = false, color = CATEGORY_COLORS[CID.Nameplates] },
    { id = CID.Bags,       displayName = "Bags",        defaultEnabled = false, color = CATEGORY_COLORS[CID.Bags]       },
}

defaultsAPI.Elements = {
    { id = EID.PlayerUnitFrame,   category = CID.UnitFrames, displayName = "Player Frame",   defaultEnabled = true,  defaultPosX = 770,  defaultPosY = 500 },
    { id = EID.TargetUnitFrame,   category = CID.UnitFrames, displayName = "Target Frame",   defaultEnabled = true,  defaultPosX = 1150, defaultPosY = 500 },
    { id = EID.PlayerBuffBar,     category = CID.UnitFrames, displayName = "Player Buffs",   defaultEnabled = true,  defaultPosX = 685,  defaultPosY = 530 },
    { id = EID.PlayerDebuffBar,   category = CID.UnitFrames, displayName = "Player Debuffs", defaultEnabled = true,  defaultPosX = 685,  defaultPosY = 565 },
    { id = EID.TargetBuffBar,     category = CID.UnitFrames, displayName = "Target Buffs",   defaultEnabled = true,  defaultPosX = 1065, defaultPosY = 530 },
    { id = EID.TargetDebuffBar,   category = CID.UnitFrames, displayName = "Target Debuffs", defaultEnabled = true,  defaultPosX = 1065, defaultPosY = 565 },
    { id = EID.ActionBar,         category = CID.ActionBars, displayName = "Action Bar",     defaultEnabled = true,  defaultPosX = 800,  defaultPosY = 360 },
    { id = EID.ChatWindow,        category = CID.Chat,       displayName = "Chat Window",    defaultEnabled = false, defaultPosX = 20,   defaultPosY = 20  },
    { id = EID.MicroMenuBar,      category = CID.MicroMenu,  displayName = "Micro Menu",     defaultEnabled = false, defaultPosX = -24,  defaultPosY = 24  },
    { id = EID.Nameplates,        category = CID.Nameplates, displayName = "Nameplates",     defaultEnabled = false, defaultPosX = 0,    defaultPosY = 0   },
}

local categoryLookup : { [CategoryID] : CategoryDef } = {}
local elementLookup : { [ElementID] : ElementDef } = {}
local elementsByCategory : { [CategoryID] : { ElementDef } } = {}

for _, cat in defaultsAPI.Categories do
    categoryLookup[cat.id] = cat
end

for _, elem in defaultsAPI.Elements do
    elementLookup[elem.id] = elem
    if elementsByCategory[elem.category] == nil then
        elementsByCategory[elem.category] = {}
    end
    table.insert(elementsByCategory[elem.category], elem)
end

function defaultsAPI.GetCategory(id : CategoryID) : CategoryDef?
    return categoryLookup[id]
end

function defaultsAPI.GetElement(id : ElementID) : ElementDef?
    return elementLookup[id]
end

function defaultsAPI.GetElementsByCategory(categoryID : CategoryID) : { ElementDef }
    return elementsByCategory[categoryID] or {}
end

return defaultsAPI
