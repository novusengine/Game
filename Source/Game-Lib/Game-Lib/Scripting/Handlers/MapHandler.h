#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Map
{
    class MapHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 GetCurrent(Zenith* zenith);
        static i32 GetLoadingProgress(Zenith* zenith);
        static i32 GetList(Zenith* zenith);
        static i32 Load(Zenith* zenith);
        static i32 LoadByID(Zenith* zenith);
        static i32 Unload(Zenith* zenith);
        static i32 SetOnCurrentMapChanged(Zenith* zenith);

        void OnCurrentMapChanged(Zenith* zenith);

    private:
        i32 _onCurrentMapChangedRef = LUA_NOREF;
    };

    static LuaRegister<> mapGlobalMethods[] =
    {
        { "GetCurrent",             MapHandler::GetCurrent },
        { "GetLoadingProgress",     MapHandler::GetLoadingProgress },
        { "GetList",                MapHandler::GetList },
        { "Load",                   MapHandler::Load,             Scripting::LuaMethodFlags::DeveloperOnly },
        { "LoadByID",               MapHandler::LoadByID,         Scripting::LuaMethodFlags::DeveloperOnly },
        { "Unload",                 MapHandler::Unload,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetOnCurrentMapChanged", MapHandler::SetOnCurrentMapChanged },
    };
}
