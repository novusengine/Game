local CursorInfo = require("@src/API/UI/CursorInfo")

local Cursors =
{
    { "architect", "texture/interface/cursor/architect.dds" },
    { "argusteleporter", "texture/interface/cursor/argusteleporter.dds" },
    { "attack", "texture/interface/cursor/attack.dds" },
    { "unableattack", "texture/interface/cursor/unableattack.dds" },
    { "buy", "texture/interface/cursor/buy.dds" },
    { "unablebuy", "texture/interface/cursor/unablebuy.dds" },
    { "cast", "texture/interface/cursor/cast.dds" },
    { "unablecast", "texture/interface/cursor/unablecast.dds" },
    { "crosshairs", "texture/interface/cursor/crosshairs.dds" },
    { "unablecrosshairs", "texture/interface/cursor/unablecrosshairs.dds" },
    { "directions", "texture/interface/cursor/directions.dds" },
    { "unabledirections", "texture/interface/cursor/unabledirections.dds" },
    { "driver", "texture/interface/cursor/driver.dds" },
    { "engineerskin", "texture/interface/cursor/engineerskin.dds" },
    { "unableengineerskin", "texture/interface/cursor/unableengineerskin.dds" },
    { "gatherherbs", "texture/interface/cursor/gatherherbs.dds" },
    { "unablegatherherbs", "texture/interface/cursor/unablegatherherbs.dds" },
    { "gunner", "texture/interface/cursor/gunner.dds" },
    { "unablegunner", "texture/interface/cursor/unablegunner.dds" },
    { "innkeeper", "texture/interface/cursor/innkeeper.dds" },
    { "unableinnkeeper", "texture/interface/cursor/unableinnkeeper.dds" },
    { "inspect", "texture/interface/cursor/inspect.dds" },
    { "unableinspect", "texture/interface/cursor/unableinspect.dds" },
    { "interact", "texture/interface/cursor/interact.dds" },
    { "unableinteract", "texture/interface/cursor/unableinteract.dds" },
    { "lootall", "texture/interface/cursor/lootall.dds" },
    { "unablelootall", "texture/interface/cursor/unablelootall.dds" },
    { "mail", "texture/interface/cursor/mail.dds" },
    { "unablemail", "texture/interface/cursor/unablemail.dds" },
    { "mine", "texture/interface/cursor/mine.dds" },
    { "unablemine", "texture/interface/cursor/unablemine.dds" },
    { "missions", "texture/interface/cursor/missions.dds" },
    { "unablemissions", "texture/interface/cursor/unablemissions.dds" },
    { "openhand", "texture/interface/cursor/openhand.dds" },
    { "unableopenhand", "texture/interface/cursor/unableopenhand.dds" },
    { "openhandglow", "texture/interface/cursor/openhandglow.dds" },
    { "unableopenhandglow", "texture/interface/cursor/unableopenhandglow.dds" },
    { "picklock", "texture/interface/cursor/picklock.dds" },
    { "unablepicklock", "texture/interface/cursor/unablepicklock.dds" },
    { "unablepickup", "texture/interface/cursor/unablepickup.dds" },
    { "point", "texture/interface/cursor/point.dds" },
    { "unablepoint", "texture/interface/cursor/unablepoint.dds" },
    { "pvp", "texture/interface/cursor/pvp.dds" },
    { "unablepvp", "texture/interface/cursor/unablepvp.dds" },
    { "quest", "texture/interface/cursor/quest.dds" },
    { "unablequest", "texture/interface/cursor/unablequest.dds" },
    { "questinteract", "texture/interface/cursor/questinteract.dds" },
    { "unablequestinteract", "texture/interface/cursor/unablequestinteract.dds" },
    { "questrepeatable", "texture/interface/cursor/questrepeatable.dds" },
    { "unablequestrepeatable", "texture/interface/cursor/unablequestrepeatable.dds" },
    { "questturnin", "texture/interface/cursor/questturnin.dds" },
    { "unablequestturnin", "texture/interface/cursor/unablequestturnin.dds" },
    { "reforge", "texture/interface/cursor/reforge.dds" },
    { "unablereforge", "texture/interface/cursor/unablereforge.dds" },
    { "repair", "texture/interface/cursor/repair.dds" },
    { "unablerepair", "texture/interface/cursor/unablerepair.dds" },
    { "repairnpc", "texture/interface/cursor/repairnpc.dds" },
    { "unablerepairnpc", "texture/interface/cursor/unablerepairnpc.dds" },
    { "skin", "texture/interface/cursor/skin.dds" },
    { "unableskin", "texture/interface/cursor/unableskin.dds" },
    { "speak", "texture/interface/cursor/speak.dds" },
    { "unablespeak", "texture/interface/cursor/unablespeak.dds" },
    { "taxi", "texture/interface/cursor/taxi.dds" },
    { "unabletaxi", "texture/interface/cursor/unabletaxi.dds" },
    { "trainer", "texture/interface/cursor/trainer.dds" },
    { "unabletrainer", "texture/interface/cursor/unabletrainer.dds" },
    { "transmogrify", "texture/interface/cursor/transmogrify.dds" },
    { "unabletransmogrify", "texture/interface/cursor/unabletransmogrify.dds" },
    { "move", "texture/interface/cursor/ui-cursor-move.dds" },
    { "unablemove", "texture/interface/cursor/unableui-cursor-move.dds" },
    { "item", "texture/interface/cursor/item.dds" },
    { "unableitem", "texture/interface/cursor/unableitem.dds" }
}

local function RegisterCursors()
    for k, v in Cursors do
        local name = v[1]
        local path = v[2]
        
        -- Ignoring Return Value on purpose
        AddCursor(name, path)
    end
end

local function OnGameLoaded(eventID : number, data : any)
    RegisterCursors()
    SetCursor("point")
end

local currentWorldCursor = "point"
local function OnGameUpdated(eventID : number, data : any)
    if CursorInfo:HasItemInHand() then
        currentWorldCursor = ""
        return
    end

    local hoveredUnit = Unit.GetHovered()
    local canAttackUnit = false
    if hoveredUnit ~= INVALID_UNIT_ID then
        local health = Unit.GetHealth(hoveredUnit)
        canAttackUnit = health > 0 and Unit.CanAttack(hoveredUnit)
    end

    local desiredCursor = if canAttackUnit then "attack" else "point"
    if desiredCursor == currentWorldCursor then
        return
    end

    if SetCursor(desiredCursor) then
        currentWorldCursor = desiredCursor
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)
RegisterEvent(GameEvent.Updated, OnGameUpdated)
