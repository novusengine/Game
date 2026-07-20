local Type = require("Type")
local D = require("Definition")

return D.Definitions
{
    D.LuaEnum("LuaHandlerTypeEnum", Type.U16,
    {
        D.Field("Global"),
        D.Field("Event"),
        D.Field("Database"),
        D.Field("UI"),
        D.Field("Game"),
        D.Field("Unit"),
        D.Field("Time"),
        D.Field("Camera"),
        D.Field("Map"),
        D.Field("Scene"),
        D.Field("Editor"),
        D.Field("Asset"),
        D.Field("Skybox"),
        D.Field("Network"),
        D.Field("Perf"),
        D.Field("Count")
    }),

    D.LuaEnum("GameEvent", Type.U8,
    {
        D.Field("Invalid"),
        D.Field("Loaded"),
        D.Field("Updated"),
        D.Field("CharacterListChanged"),
        D.Field("MapLoading"),
        D.Field("ChatMessageReceived"),
        D.Field("LocalMoverChanged"),
        D.Field("Count")
    }),

    D.LuaEnum("UnitEvent", Type.U8,
    {
        D.Field("Invalid"),
        D.Field("Add"),
        D.Field("Remove"),
        D.Field("TargetChanged"),
        D.Field("PowerUpdate"),
        D.Field("ResistanceUpdate"),
        D.Field("StatUpdate"),
        D.Field("AuraAdd"),
        D.Field("AuraUpdate"),
        D.Field("AuraRemove"),
        D.Field("ReactionChanged"),
        D.Field("Count")
    }),

    D.LuaEnum("ReputationEvent", Type.U8,
    {
        D.Field("Invalid"),
        D.Field("Changed"),
        D.Field("Count")
    }),

    D.LuaEnum("ContainerEvent", Type.U8,
    {
        D.Field("Invalid"),
        D.Field("Add"),
        D.Field("AddToSlot"),
        D.Field("RemoveFromSlot"),
        D.Field("SwapSlots"),
        D.Field("Count")
    }),

    D.LuaEnum("TriggerEvent", Type.U8,
    {
        D.Field("Invalid"),
        D.Field("OnEnter"),
        D.Field("OnExit"),
        D.Field("OnStay"),
        D.Field("Count")
    })
}
