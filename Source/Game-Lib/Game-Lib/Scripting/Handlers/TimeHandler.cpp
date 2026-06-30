#include "TimeHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/DayNightCycle.h"
#include "Game-Lib/ECS/Systems/UpdateDayNightCycle.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <MetaGen/Game/Lua/Lua.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting::Time
{
    void TimeHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, timeGlobalMethods, "Time", excludeFlags);

        zenith->GetGlobalKey("Time");
        zenith->AddTableField("SecondsPerDay", ECS::Singletons::DayNightCycle::SecondsPerDay);
        zenith->Pop();

        _onSecondChangedRef = LUA_NOREF;
    }

    void TimeHandler::Clear(Zenith* zenith)
    {
        Scripting::Util::Zenith::Unref(zenith, _onSecondChangedRef);
        _onSecondChangedRef = LUA_NOREF;
    }

    static ECS::Singletons::DayNightCycle* GetDayNightCycle()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->gameRegistry)
            return nullptr;
        auto& ctx = registries->gameRegistry->ctx();
        if (!ctx.contains<ECS::Singletons::DayNightCycle>())
            return nullptr;
        return &ctx.get<ECS::Singletons::DayNightCycle>();
    }

    static TimeHandler* GetSelf()
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        if (!luaManager)
            return nullptr;
        return luaManager->GetLuaHandler<TimeHandler>(static_cast<LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Time));
    }

    i32 TimeHandler::GetSeconds(Zenith* zenith)
    {
        ECS::Singletons::DayNightCycle* dnc = GetDayNightCycle();
        zenith->Push(dnc ? dnc->timeInSeconds : 0.0);
        return 1;
    }

    i32 TimeHandler::SetSeconds(Zenith* zenith)
    {
        f64 seconds = zenith->CheckVal<f64>(1);
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (registries && registries->gameRegistry)
            ECS::Systems::UpdateDayNightCycle::SetTime(*registries->gameRegistry, seconds);
        return 0;
    }

    i32 TimeHandler::Reset(Zenith* /*zenith*/)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (registries && registries->gameRegistry)
            ECS::Systems::UpdateDayNightCycle::SetTimeToDefault(*registries->gameRegistry);
        return 0;
    }

    i32 TimeHandler::SetToNoon(Zenith* /*zenith*/)
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (registries && registries->gameRegistry)
        {
            f64 noon = static_cast<f64>(ECS::Singletons::DayNightCycle::SecondsPerDay) / 2.0;
            ECS::Systems::UpdateDayNightCycle::SetTime(*registries->gameRegistry, noon);
        }
        return 0;
    }

    i32 TimeHandler::GetSpeedModifier(Zenith* zenith)
    {
        ECS::Singletons::DayNightCycle* dnc = GetDayNightCycle();
        zenith->Push(dnc ? dnc->speedModifier : 1.0f);
        return 1;
    }

    i32 TimeHandler::SetSpeedModifier(Zenith* zenith)
    {
        f32 multiplier = zenith->CheckVal<f32>(1);
        ECS::Singletons::DayNightCycle* dnc = GetDayNightCycle();
        if (dnc)
            dnc->speedModifier = multiplier;
        return 0;
    }

    i32 TimeHandler::GetSecondsPerDay(Zenith* zenith)
    {
        zenith->Push(ECS::Singletons::DayNightCycle::SecondsPerDay);
        return 1;
    }

    i32 TimeHandler::SetOnSecondChanged(Zenith* zenith)
    {
        TimeHandler* self = GetSelf();
        if (!self)
            return 0;

        Scripting::Util::Zenith::Unref(zenith, self->_onSecondChangedRef);
        self->_onSecondChangedRef = LUA_NOREF;

        if (zenith->IsFunction(1))
        {
            self->_onSecondChangedRef = zenith->GetRef(1);
        }
        return 0;
    }

    void TimeHandler::OnSecondChanged(Zenith* zenith, f64 timeInSeconds)
    {
        if (_onSecondChangedRef == LUA_NOREF)
            return;

        zenith->GetRawI(LUA_REGISTRYINDEX, _onSecondChangedRef);
        zenith->Push(timeInSeconds);
        zenith->PCall(1);
    }
}
