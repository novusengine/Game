#include "UpdateDayNightCycle.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/DayNightCycle.h"
#include "Game-Lib/Scripting/Handlers/UIHandler.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <ctime>

namespace ECS::Systems
{
    u32 GetSecondsSinceMidnightUTC()
    {
        time_t timeNow = std::time(nullptr);
        tm* timestampUTC = std::gmtime(&timeNow);

        u32 seconds = timestampUTC->tm_sec + (((timestampUTC->tm_hour * 60) + timestampUTC->tm_min) * 60);
        return seconds;
    }

    void UpdateDayNightCycle::Init(entt::registry& registry)
    {
        entt::registry::context& context = registry.ctx();

        auto& dayNightCycle = context.emplace<Singletons::DayNightCycle>();

        SetTimeToDefault(registry);
        SetSpeedModifier(registry, 1.0f);
    }

    void UpdateDayNightCycle::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::UpdateDayNightCycle");

        entt::registry::context& context = registry.ctx();
        auto& dayNightCycle = context.get<Singletons::DayNightCycle>();

        dayNightCycle.timeInSeconds += static_cast<f64>(dayNightCycle.speedModifier) * static_cast<f64>(deltaTime);

        while (dayNightCycle.timeInSeconds > Singletons::DayNightCycle::SecondsPerDay)
        {
            dayNightCycle.timeInSeconds -= Singletons::DayNightCycle::SecondsPerDay;
        }

        i32 currentInteger = static_cast<i32>(dayNightCycle.timeInSeconds);
        if (currentInteger != dayNightCycle.lastIntegerSecond)
        {
            dayNightCycle.lastIntegerSecond = currentInteger;

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
            if (luaManager && zenith)
            {
                Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>(static_cast<u16>(MetaGen::Game::Lua::LuaHandlerTypeEnum::UI));
                if (uiHandler)
                {
                    uiHandler->OnSecondChanged(zenith, dayNightCycle.timeInSeconds);
                }
            }
        }
    }

    void UpdateDayNightCycle::SetTimeToDefault(entt::registry& registry)
    {
        f64 secondsSinceMidnightUTC = static_cast<f64>(GetSecondsSinceMidnightUTC());
        while (secondsSinceMidnightUTC > Singletons::DayNightCycle::SecondsPerDay)
        {
            secondsSinceMidnightUTC -= Singletons::DayNightCycle::SecondsPerDay;
        }

        SetTime(registry, secondsSinceMidnightUTC);
    }

    void UpdateDayNightCycle::SetTime(entt::registry& registry, f64 time)
    {
        entt::registry::context& context = registry.ctx();
        auto& dayNightCycle = context.get<Singletons::DayNightCycle>();

        dayNightCycle.timeInSeconds = time;
    }
    void UpdateDayNightCycle::SetSpeedModifier(entt::registry& registry, f32 speedModifier)
    {
        entt::registry::context& context = registry.ctx();
        auto& dayNightCycle = context.get<Singletons::DayNightCycle>();

        dayNightCycle.speedModifier = speedModifier;
    }
    void UpdateDayNightCycle::SetTimeAndSpeedModifier(entt::registry& registry, f64 time, f32 speedModifier)
    {
        SetTime(registry, time);
        SetSpeedModifier(registry, speedModifier);
    }
}
