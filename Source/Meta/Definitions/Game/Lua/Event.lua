local Type = require("Type")
local D = require("Definition")

return D.Definitions
{
    D.LuaEvent("GameEventDataLoaded",
    {
        D.Field("motd", Type.STRING)
    }),

    D.LuaEvent("GameEventDataUpdated",
    {
        D.Field("deltaTime", Type.F32)
    }),

    D.LuaEvent("GameEventDataCharacterListChanged",
    {}),

    D.LuaEvent("GameEventDataMapLoading",
    {
        D.Field("mapInternalName", Type.STRING)
    }),

    D.LuaEvent("GameEventDataChatMessageReceived",
    {
        D.Field("sender", Type.STRING),
        D.Field("channel", Type.STRING),
        D.Field("message", Type.STRING)
    }),

    D.LuaEvent("GameEventDataLocalMoverChanged",
    {
        D.Field("moverID", Type.U32)
    }),

    D.LuaEvent("UnitEventDataAdd",
    {
        D.Field("unitID", Type.U32)
    }),

    D.LuaEvent("UnitEventDataRemove",
    {
        D.Field("unitID", Type.U32)
    }),

    D.LuaEvent("UnitEventDataTargetChanged",
    {
        D.Field("unitID", Type.U32),
        D.Field("targetID", Type.U32)
    }),

    D.LuaEvent("UnitEventDataPowerUpdate",
    {
        D.Field("unitID", Type.U32),
        D.Field("powerType", Type.U8),
        D.Field("base", Type.F64),
        D.Field("current", Type.F64),
        D.Field("max", Type.F64)
    }),

    D.LuaEvent("UnitEventDataResistanceUpdate",
    {
        D.Field("unitID", Type.U32),
        D.Field("resistanceType", Type.U8),
        D.Field("base", Type.F64),
        D.Field("current", Type.F64),
        D.Field("max", Type.F64)
    }),

    D.LuaEvent("UnitEventDataStatUpdate",
    {
        D.Field("unitID", Type.U32),
        D.Field("statType", Type.U8),
        D.Field("base", Type.F64),
        D.Field("current", Type.F64)
    }),

    D.LuaEvent("UnitEventDataAuraAdd",
    {
        D.Field("unitID", Type.U32),
        D.Field("auraID", Type.U32),
        D.Field("spellID", Type.U32),
        D.Field("duration", Type.F32),
        D.Field("stacks", Type.U16)
    }),

    D.LuaEvent("UnitEventDataAuraUpdate",
    {
        D.Field("unitID", Type.U32),
        D.Field("auraID", Type.U32),
        D.Field("duration", Type.F32),
        D.Field("stacks", Type.U16)
    }),

    D.LuaEvent("UnitEventDataAuraRemove",
    {
        D.Field("unitID", Type.U32),
        D.Field("auraID", Type.U32)
    }),

    D.LuaEvent("UnitEventDataReactionChanged",
    {
        D.Field("unitID", Type.U32),
        D.Field("oldReaction", Type.U8),
        D.Field("newReaction", Type.U8)
    }),

    D.LuaEvent("ReputationEventDataChanged",
    {
        D.Field("factionID", Type.U16),
        D.Field("oldValue", Type.I32),
        D.Field("newValue", Type.I32),
        D.Field("oldFlags", Type.U16),
        D.Field("newFlags", Type.U16),
        D.Field("oldPersistentStandingID", Type.U16),
        D.Field("newPersistentStandingID", Type.U16),
        D.Field("oldEffectiveStandingID", Type.U16),
        D.Field("newEffectiveStandingID", Type.U16),
        D.Field("oldPerceptionFields", Type.U8),
        D.Field("newPerceptionFields", Type.U8),
        D.Field("wasPresent", Type.BOOL),
        D.Field("isPresent", Type.BOOL)
    }),

    D.LuaEvent("ContainerEventDataAdd",
    {
        D.Field("index", Type.U32),
        D.Field("numSlots", Type.U32),
        D.Field("itemID", Type.U32)
    }),

    D.LuaEvent("ContainerEventDataAddToSlot",
    {
        D.Field("containerIndex", Type.U32),
        D.Field("slotIndex", Type.U32),
        D.Field("itemID", Type.U32),
        D.Field("count", Type.U32)
    }),

    D.LuaEvent("ContainerEventDataRemoveFromSlot",
    {
        D.Field("containerIndex", Type.U32),
        D.Field("slotIndex", Type.U32)
    }),

    D.LuaEvent("ContainerEventDataSwapSlots",
    {
        D.Field("srcContainerIndex", Type.U32),
        D.Field("destContainerIndex", Type.U32),
        D.Field("srcSlotIndex", Type.U32),
        D.Field("destSlotIndex", Type.U32)
    }),

    D.LuaEvent("TriggerEventDataOnEnter",
    {
        D.Field("triggerID", Type.U32),
        D.Field("playerID", Type.U32)
    }),

    D.LuaEvent("TriggerEventDataOnExit",
    {
        D.Field("triggerID", Type.U32),
        D.Field("playerID", Type.U32)
    }),

    D.LuaEvent("TriggerEventDataOnStay",
    {
        D.Field("triggerID", Type.U32),
        D.Field("playerID", Type.U32)
    })
}
