#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Time
{
    class TimeHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 GetSeconds(Zenith* zenith);
        static i32 SetSeconds(Zenith* zenith);
        static i32 Reset(Zenith* zenith);
        static i32 SetToNoon(Zenith* zenith);
        static i32 GetSpeedModifier(Zenith* zenith);
        static i32 SetSpeedModifier(Zenith* zenith);
        static i32 GetSecondsPerDay(Zenith* zenith);
        static i32 SetOnSecondChanged(Zenith* zenith);

        void OnSecondChanged(Zenith* zenith, f64 timeInSeconds);

    private:
        i32 _onSecondChangedRef = LUA_NOREF;
    };

    static LuaRegister<> timeGlobalMethods[] =
    {
        { "GetSeconds",          TimeHandler::GetSeconds },
        { "SetSeconds",          TimeHandler::SetSeconds,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "Reset",               TimeHandler::Reset,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetToNoon",           TimeHandler::SetToNoon,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetSpeedModifier",    TimeHandler::GetSpeedModifier },
        { "SetSpeedModifier",    TimeHandler::SetSpeedModifier,  Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetSecondsPerDay",    TimeHandler::GetSecondsPerDay },
        { "SetOnSecondChanged",  TimeHandler::SetOnSecondChanged },
    };
}
