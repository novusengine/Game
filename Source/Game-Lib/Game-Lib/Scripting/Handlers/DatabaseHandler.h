#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Database
{
    class DatabaseHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith) {}

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}
    };
}