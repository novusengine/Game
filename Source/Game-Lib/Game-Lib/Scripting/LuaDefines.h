#pragma once
#include <Base/Types.h>

#include <Meta/Generated/Game/LuaEnum.h>

struct lua_State;
typedef i32 (*lua_CFunction)(lua_State* L);

namespace Scripting
{
    class LuaManager;
    class LuaHandlerBase;
    class LuaSystemBase;
    using LuaUserDataDtor = void(void*);

    enum class LuaHandlerType
    {
        Global,
        GameEvent,
        PlayerEvent,
        TriggerEvent,
        UI,
        Unit,
        Database,
        Game,
        Count
    };

    enum class LuaSystemEvent
    {
        Invalid,
        Reload
    };

    enum class LuaPlayerEvent
    {
        Invalid,
        ContainerCreate,
        ContainerAddToSlot,
        ContainerRemoveFromSlot,
        ContainerSwapSlots,
        Count
    };

    struct LuaEventData
    {
    public:
    };

    struct LuaGameEventLoadedData : LuaEventData
    {
    public:
        std::string motd;
    };

    struct LuaGameEventUpdatedData : LuaEventData
    {
    public:
        f32 deltaTime;
    };

    struct LuaPlayerEventContainerItemInfo
    {
    public:
        u32 slot;
        u32 itemID;
        u32 count;
    };

    struct LuaPlayerEventContainerCreateData : LuaEventData
    {
    public:
        u32 index;
        u32 numSlots;
        u32 itemID;
        std::vector<LuaPlayerEventContainerItemInfo> items;
    };

    struct LuaPlayerEventContainerAddToSlotData : LuaEventData
    {
    public:
        u32 containerIndex;
        u32 slotIndex;
        u32 itemID;
        u32 count;
    };

    struct LuaPlayerEventContainerRemoveFromSlotData : LuaEventData
    {
    public:
        u32 containerIndex;
        u32 slotIndex;
    };

    struct LuaPlayerEventContainerSwapSlotsData : LuaEventData
    {
    public:
        u32 srcContainerIndex;
        u32 destContainerIndex;
        u32 srcSlotIndex;
        u32 destSlotIndex;
    };

    struct LuaTriggerEventOnTriggerEnterData : LuaEventData
    {
    public:
        u32 triggerID;
        u32 playerID;
    };

    struct LuaTriggerEventOnTriggerExitData : LuaEventData
    {
    public:
        u32 triggerID;
        u32 playerID;
    };

    struct LuaTriggerEventOnTriggerStayData : LuaEventData
    {
    public:
        u32 triggerID;
        u32 playerID;
    };

    using LuaGameEventHandlerFn = std::function<void(lua_State*, Generated::LuaGameEventEnum, LuaEventData*)>;
}