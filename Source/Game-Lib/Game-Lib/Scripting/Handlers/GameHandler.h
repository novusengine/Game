#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Game
{
    class GameHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith);
        void Update(Zenith* zenith, f32 deltaTime);

        static i32 IsLoaded(Zenith* zenith);

    private:
        bool _isLoaded = false;
    };

    static LuaRegister<> gameGlobalMethods[] =
    {
        { "IsLoaded", GameHandler::IsLoaded },
    };
}
