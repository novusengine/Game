#pragma once
#include <Base/Types.h>

#include <robinhood/robinhood.h>
#include <any>

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
        Canvas,
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

    using LuaGameEventHandlerFn = std::function<void(lua_State*, LuaGameEvent, LuaEventData*)>;
}