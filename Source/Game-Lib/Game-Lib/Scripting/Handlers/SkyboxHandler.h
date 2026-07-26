#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Skybox
{
    class SkyboxHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 GetCurrent(Zenith* zenith);
        static i32 Enumerate(Zenith* zenith);
        static i32 Load(Zenith* zenith);
        static i32 Unload(Zenith* zenith);
    };

    static LuaRegister<> skyboxGlobalMethods[] =
    {
        { "GetCurrent",  SkyboxHandler::GetCurrent,  Scripting::LuaMethodFlags::DeveloperOnly },
        { "Enumerate",   SkyboxHandler::Enumerate,   Scripting::LuaMethodFlags::DeveloperOnly },
        { "Load",        SkyboxHandler::Load,        Scripting::LuaMethodFlags::DeveloperOnly },
        { "Unload",      SkyboxHandler::Unload,      Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
