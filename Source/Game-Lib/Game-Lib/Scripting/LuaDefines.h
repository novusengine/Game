#pragma once
#include <Base/Types.h>

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
        UI,
        Database,
        Game,
        Count
    };

    enum class LuaSystemEvent
    {
        Invalid,
        Reload
    };

    enum class LuaGameEvent
    {
        Invalid,
        Loaded,
        Updated,
        Count
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

    using LuaGameEventHandlerFn = std::function<void(lua_State*, LuaGameEvent, LuaEventData*)>;
}