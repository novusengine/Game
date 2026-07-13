local Type = require("Type")
local D = require("Definition")

return D.Definitions
{
    D.GameCommand("HelpCommand", { "help" },
    {}),

    D.GameCommand("PingCommand", { "ping" },
    {}),

    D.GameCommand("LuaCommand", { "lua", "eval" },
    {
        D.Field("code", Type.STRING)
    }),

    D.GameCommand("ScriptReloadCommand", { "script reload" },
    {}),

    D.GameCommand("DatabaseReloadCommand", { "database reload" },
    {}),

    D.GameCommand("CameraSaveCommand", { "camera save" },
    {
        D.Field("name", Type.STRING)
    }),

    D.GameCommand("CameraLoadByCodeCommand", { "camera loadbycode" },
    {
        D.Field("code", Type.STRING)
    }),

    D.GameCommand("MapClearCommand", { "map clear" },
    {}),

    D.GameCommand("MapSyncCommand", { "map sync" },
    {
        D.Field("mapID", Type.U32)
    }),

    D.GameCommand("MapSyncAllCommand", { "map sync all" },
    {}),

    D.GameCommand("CharacterAddCommand", { "character add" },
    {
        D.Field("name", Type.STRING)
    }),

    D.GameCommand("CharacterRemoveCommand", { "character remove", "character rem" },
    {
        D.Field("name", Type.STRING)
    }),

    D.GameCommand("CheatLoginCommand", { "cheat login" },
    {
        D.Field("accountName", Type.STRING)
    }),

    D.GameCommand("CheatFlyCommand", { "cheat fly" },
    {
        D.Field("enable", Type.BOOL)
    }),

    D.GameCommand("CheatDamageCommand", { "cheat damage" },
    {
        D.Field("amount", Type.U32)
    }),

    D.GameCommand("CheatKillCommand", { "cheat kill" },
    {}),

    D.GameCommand("CheatResurrectCommand", { "cheat resurrect" },
    {}),

    D.GameCommand("CheatCastCommand", { "cheat cast" },
    {
        D.Field("spellID", Type.U32)
    }),

    D.GameCommand("CheatMorphCommand", { "cheat morph" },
    {
        D.Field("displayID", Type.U32)
    }),

    D.GameCommand("CheatDemorphCommand", { "cheat demorph" },
    {}),

    D.GameCommand("CheatSetRaceCommand", { "cheat setrace" },
    {
        D.Field("race", Type.STRING)
    }),

    D.GameCommand("CheatSetGenderCommand", { "cheat setgender" },
    {
        D.Field("gender", Type.STRING)
    }),

    D.GameCommand("CheatPathGenerateCommand", { "cheat path generate" },
    {}),

    D.GameCommand("ItemSyncCommand", { "item sync" },
    {
        D.Field("itemID", Type.U32)
    }),

    D.GameCommand("ItemSyncAllCommand", { "item sync all" },
    {}),

    D.GameCommand("ItemAddCommand", { "item add" },
    {
        D.Field("itemID", Type.U32)
    }),

    D.GameCommand("ItemRemoveCommand", { "item remove", "item rem" },
    {
        D.Field("itemID", Type.U32)
    }),

    D.GameCommand("CreatureAddCommand", { "creature add" },
    {
        D.Field("creatureTemplateID", Type.U32)
    }),

    D.GameCommand("CreatureRemoveCommand", { "creature remove", "creature rem" },
    {}),

    D.GameCommand("CreatureInfoCommand", { "creature info" },
    {}),

    D.GameCommand("CreatureAddScriptCommand", { "creature add script" },
    {
        D.Field("scriptName", Type.STRING)
    }),

    D.GameCommand("CreatureRemoveScriptCommand", { "creature remove script" },
    {}),

    D.GameCommand("CreatureMoveCommand", { "creature move" },
    {}),

    D.GameCommand("CreatureFollowCommand", { "creature follow" },
    {}),

    D.GameCommand("CreatureWanderCommand", { "creature wander" },
    {}),

    D.GameCommand("CreatureStopCommand", { "creature stop" },
    {}),

    D.GameCommand("GotoAddCommand", { "goto add" },
    {
        D.Field("location", Type.STRING),
        D.Field("mapID", Type.U32),
        D.Field("x", Type.F32),
        D.Field("y", Type.F32),
        D.Field("z", Type.F32),
        D.Field("orientation", Type.F32)
    }),

    D.GameCommand("GotoAddHereCommand", { "goto add here" },
    {
        D.Field("location", Type.STRING)
    }),

    D.GameCommand("GotoRemoveCommand", { "goto remove", "goto rem" },
    {
        D.Field("location", Type.STRING)
    }),

    D.GameCommand("GotoMapCommand", { "goto map" },
    {
        D.Field("mapID", Type.U32)
    }),

    D.GameCommand("GotoLocationCommand", { "goto location", "goto loc" },
    {
        D.Field("location", Type.STRING)
    }),

    D.GameCommand("GotoXYZCommand", { "goto xyz" },
    {
        D.Field("x", Type.F32),
        D.Field("y", Type.F32),
        D.Field("z", Type.F32)
    }),

    D.GameCommand("TriggerAddCommand", { "trigger add" },
    {
        D.Field("name", Type.STRING),
        D.Field("flags", Type.U16),
        D.Field("mapID", Type.U32),
        D.Field("posX", Type.F32),
        D.Field("posY", Type.F32),
        D.Field("posZ", Type.F32),
        D.Field("extX", Type.F32),
        D.Field("extY", Type.F32),
        D.Field("extZ", Type.F32),
    }),

    D.GameCommand("TriggerRemoveCommand", { "trigger remove", "trigger rem" },
    {
        D.Field("triggerID", Type.U32)
    }),

    D.GameCommand("SpellSyncCommand", { "spell sync" },
    {
        D.Field("spellID", Type.U32)
    }),

    D.GameCommand("SpellSyncAllCommand", { "spell sync all" },
    {})
}
