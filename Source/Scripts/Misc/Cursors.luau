local Cursors =
{
    { "architect", "Data/Texture/interface/cursor/architect.dds" },
    { "argusteleporter", "Data/Texture/interface/cursor/argusteleporter.dds" },
    { "attack", "Data/Texture/interface/cursor/attack.dds" },
    { "unableattack", "Data/Texture/interface/cursor/unableattack.dds" },
    { "buy", "Data/Texture/interface/cursor/buy.dds" },
    { "unablebuy", "Data/Texture/interface/cursor/unablebuy.dds" },
    { "cast", "Data/Texture/interface/cursor/cast.dds" },
    { "unablecast", "Data/Texture/interface/cursor/unablecast.dds" },
    { "crosshairs", "Data/Texture/interface/cursor/crosshairs.dds" },
    { "unablecrosshairs", "Data/Texture/interface/cursor/unablecrosshairs.dds" },
    { "directions", "Data/Texture/interface/cursor/directions.dds" },
    { "unabledirections", "Data/Texture/interface/cursor/unabledirections.dds" },
    { "driver", "Data/Texture/interface/cursor/driver.dds" },
    { "engineerskin", "Data/Texture/interface/cursor/engineerskin.dds" },
    { "unableengineerskin", "Data/Texture/interface/cursor/unableengineerskin.dds" },
    { "gatherherbs", "Data/Texture/interface/cursor/gatherherbs.dds" },
    { "unablegatherherbs", "Data/Texture/interface/cursor/unablegatherherbs.dds" },
    { "gunner", "Data/Texture/interface/cursor/gunner.dds" },
    { "unablegunner", "Data/Texture/interface/cursor/unablegunner.dds" },
    { "innkeeper", "Data/Texture/interface/cursor/innkeeper.dds" },
    { "unableinnkeeper", "Data/Texture/interface/cursor/unableinnkeeper.dds" },
    { "inspect", "Data/Texture/interface/cursor/inspect.dds" },
    { "unableinspect", "Data/Texture/interface/cursor/unableinspect.dds" },
    { "interact", "Data/Texture/interface/cursor/interact.dds" },
    { "unableinteract", "Data/Texture/interface/cursor/unableinteract.dds" },
    { "lootall", "Data/Texture/interface/cursor/lootall.dds" },
    { "unablelootall", "Data/Texture/interface/cursor/unablelootall.dds" },
    { "mail", "Data/Texture/interface/cursor/mail.dds" },
    { "unablemail", "Data/Texture/interface/cursor/unablemail.dds" },
    { "mine", "Data/Texture/interface/cursor/mine.dds" },
    { "unablemine", "Data/Texture/interface/cursor/unablemine.dds" },
    { "missions", "Data/Texture/interface/cursor/missions.dds" },
    { "unablemissions", "Data/Texture/interface/cursor/unablemissions.dds" },
    { "openhand", "Data/Texture/interface/cursor/openhand.dds" },
    { "unableopenhand", "Data/Texture/interface/cursor/unableopenhand.dds" },
    { "openhandglow", "Data/Texture/interface/cursor/openhandglow.dds" },
    { "unableopenhandglow", "Data/Texture/interface/cursor/unableopenhandglow.dds" },
    { "picklock", "Data/Texture/interface/cursor/picklock.dds" },
    { "unablepicklock", "Data/Texture/interface/cursor/unablepicklock.dds" },
    { "unablepickup", "Data/Texture/interface/cursor/unablepickup.dds" },
    { "point", "Data/Texture/interface/cursor/point.dds" },
    { "unablepoint", "Data/Texture/interface/cursor/unablepoint.dds" },
    { "pvp", "Data/Texture/interface/cursor/pvp.dds" },
    { "unablepvp", "Data/Texture/interface/cursor/unablepvp.dds" },
    { "quest", "Data/Texture/interface/cursor/quest.dds" },
    { "unablequest", "Data/Texture/interface/cursor/unablequest.dds" },
    { "questinteract", "Data/Texture/interface/cursor/questinteract.dds" },
    { "unablequestinteract", "Data/Texture/interface/cursor/unablequestinteract.dds" },
    { "questrepeatable", "Data/Texture/interface/cursor/questrepeatable.dds" },
    { "unablequestrepeatable", "Data/Texture/interface/cursor/unablequestrepeatable.dds" },
    { "questturnin", "Data/Texture/interface/cursor/questturnin.dds" },
    { "unablequestturnin", "Data/Texture/interface/cursor/unablequestturnin.dds" },
    { "reforge", "Data/Texture/interface/cursor/reforge.dds" },
    { "unablereforge", "Data/Texture/interface/cursor/unablereforge.dds" },
    { "repair", "Data/Texture/interface/cursor/repair.dds" },
    { "unablerepair", "Data/Texture/interface/cursor/unablerepair.dds" },
    { "repairnpc", "Data/Texture/interface/cursor/repairnpc.dds" },
    { "unablerepairnpc", "Data/Texture/interface/cursor/unablerepairnpc.dds" },
    { "skin", "Data/Texture/interface/cursor/skin.dds" },
    { "unableskin", "Data/Texture/interface/cursor/unableskin.dds" },
    { "speak", "Data/Texture/interface/cursor/speak.dds" },
    { "unablespeak", "Data/Texture/interface/cursor/unablespeak.dds" },
    { "taxi", "Data/Texture/interface/cursor/taxi.dds" },
    { "unabletaxi", "Data/Texture/interface/cursor/unabletaxi.dds" },
    { "trainer", "Data/Texture/interface/cursor/trainer.dds" },
    { "unabletrainer", "Data/Texture/interface/cursor/unabletrainer.dds" },
    { "transmogrify", "Data/Texture/interface/cursor/transmogrify.dds" },
    { "unabletransmogrify", "Data/Texture/interface/cursor/unabletransmogrify.dds" },
    { "move", "Data/Texture/interface/cursor/ui-cursor-move.dds" },
    { "unablemove", "Data/Texture/interface/cursor/unableui-cursor-move.dds" }
}

local function RegisterCursors()
    for k, v in Cursors do
        local name = v[1]
        local path = v[2]
        
        -- Ignoring Return Value on purpose
        AddCursor(name, path)
    end
end

local function OnGameLoaded(eventID : number, motd : string)
    RegisterCursors()
    SetCursor("point")
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)