#pragma once
#include "Game-Lib/Scripting/LuaDefines.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>

#include <vector>

namespace Scripting
{
    class LuaSystemBase
    {
    public:
        LuaSystemBase();

    private:
        friend LuaManager;

        void Init();
        void Update(f32 deltaTime, lua_State* state);
        void PushEvent(LuaSystemEvent systemEvent);

    protected:
        virtual void Prepare(f32 deltaTime, lua_State* state) = 0;
        virtual void Run(f32 deltaTime, lua_State* state) = 0;

        moodycamel::ConcurrentQueue<LuaSystemEvent> _events;
    };
}