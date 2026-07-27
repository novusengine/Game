#include "SchedulerHandler.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Scripting::Scheduler
{
    namespace
    {
        SchedulerHandler* GetSelf(Zenith* zenith)
        {
            zenith->GetGlobalKey("Zenith");
            LuaManager* luaManager = zenith->IsLightUserData(-1)
                ? static_cast<LuaManager*>(zenith->ToLightUserData(-1))
                : nullptr;
            zenith->Pop();

            if (!luaManager)
                return nullptr;

            return luaManager->GetLuaHandler<SchedulerHandler>(
                static_cast<LuaHandlerID>(MetaGen::Game::Lua::LuaHandlerTypeEnum::Scheduler));
        }
    }

    void SchedulerHandler::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, schedulerGlobalMethods, "Scheduler");
    }

    void SchedulerHandler::Clear(Zenith* zenith)
    {
        for (auto iterator = _callbacks.begin(); iterator != _callbacks.end();)
        {
            if (iterator->second.owner != zenith)
            {
                ++iterator;
                continue;
            }

            Scripting::Util::Zenith::Unref(zenith, iterator->second.callbackRef);
            iterator = _callbacks.erase(iterator);
        }
        _frames.erase(zenith);
    }

    void SchedulerHandler::Update(Zenith* zenith, f32)
    {
        const u64 currentFrame = ++_frames[zenith];
        const Clock::time_point now = Clock::now();
        std::vector<u64> dueCallbacks;
        dueCallbacks.reserve(_callbacks.size());

        for (const auto& [handle, callback] : _callbacks)
        {
            if (callback.owner != zenith)
                continue;

            const bool isDue = callback.type == ScheduleType::Seconds
                ? callback.deadline <= now
                : callback.targetFrame <= currentFrame;
            if (isDue)
                dueCallbacks.push_back(handle);
        }

        std::sort(dueCallbacks.begin(), dueCallbacks.end());

        for (const u64 handle : dueCallbacks)
        {
            auto iterator = _callbacks.find(handle);
            if (iterator == _callbacks.end() || iterator->second.owner != zenith)
                continue;

            const i32 callbackRef = iterator->second.callbackRef;
            zenith->GetRawI(LUA_REGISTRYINDEX, callbackRef);
            Scripting::Util::Zenith::Unref(zenith, callbackRef);
            _callbacks.erase(iterator);
            zenith->PCall();
        }
    }

    i32 SchedulerHandler::AfterSeconds(Zenith* zenith)
    {
        if (zenith->GetTop() != 2)
        {
            luaL_error(zenith->state, "Scheduler.AfterSeconds expects seconds and callback");
            return 0;
        }

        const f64 seconds = zenith->CheckVal<f64>(1);
        if (!std::isfinite(seconds) || seconds < 0.0)
        {
            luaL_error(zenith->state, "Scheduler.AfterSeconds seconds must be finite and non-negative");
            return 0;
        }

        const Clock::time_point now = Clock::now();
        const f64 maxSeconds = std::chrono::duration<f64>(
            Clock::time_point::max() - now).count();
        if (seconds > maxSeconds)
        {
            luaL_error(zenith->state, "Scheduler.AfterSeconds seconds exceed the supported clock range");
            return 0;
        }

        if (!zenith->IsFunction(2))
        {
            luaL_error(zenith->state, "Scheduler.AfterSeconds callback must be a function");
            return 0;
        }

        SchedulerHandler* self = GetSelf(zenith);
        if (!self)
        {
            luaL_error(zenith->state, "Scheduler handler is unavailable");
            return 0;
        }

        const auto duration = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<f64>(seconds));
        const u64 handle = self->AllocateHandle();
        const i32 callbackRef = zenith->GetRef(2);
        self->_callbacks.emplace(handle, PendingCallback{
            .owner = zenith,
            .type = ScheduleType::Seconds,
            .deadline = now + duration,
            .callbackRef = callbackRef,
        });

        zenith->Push(handle);
        return 1;
    }

    i32 SchedulerHandler::AfterFrames(Zenith* zenith)
    {
        if (zenith->GetTop() != 2)
        {
            luaL_error(zenith->state, "Scheduler.AfterFrames expects frames and callback");
            return 0;
        }

        const f64 frameValue = zenith->CheckVal<f64>(1);
        if (!std::isfinite(frameValue) ||
            frameValue < 1.0 ||
            frameValue > static_cast<f64>(std::numeric_limits<u32>::max()) ||
            std::floor(frameValue) != frameValue)
        {
            luaL_error(zenith->state, "Scheduler.AfterFrames frames must be a positive integer");
            return 0;
        }

        if (!zenith->IsFunction(2))
        {
            luaL_error(zenith->state, "Scheduler.AfterFrames callback must be a function");
            return 0;
        }

        SchedulerHandler* self = GetSelf(zenith);
        if (!self)
        {
            luaL_error(zenith->state, "Scheduler handler is unavailable");
            return 0;
        }

        const u64 frames = static_cast<u64>(frameValue);
        const u64 currentFrame = self->_frames[zenith];
        if (frames > std::numeric_limits<u64>::max() - currentFrame)
        {
            luaL_error(zenith->state, "Scheduler.AfterFrames frames exceed the supported range");
            return 0;
        }

        const u64 handle = self->AllocateHandle();
        const i32 callbackRef = zenith->GetRef(2);
        self->_callbacks.emplace(handle, PendingCallback{
            .owner = zenith,
            .type = ScheduleType::Frames,
            .targetFrame = currentFrame + frames,
            .callbackRef = callbackRef,
        });

        zenith->Push(handle);
        return 1;
    }

    i32 SchedulerHandler::Cancel(Zenith* zenith)
    {
        if (zenith->GetTop() != 1)
        {
            luaL_error(zenith->state, "Scheduler.Cancel expects a scheduler handle");
            return 0;
        }

        SchedulerHandler* self = GetSelf(zenith);
        if (!self)
        {
            zenith->Push(false);
            return 1;
        }

        const u64 handle = zenith->CheckVal<u64>(1);
        auto iterator = self->_callbacks.find(handle);
        if (iterator == self->_callbacks.end() || iterator->second.owner != zenith)
        {
            zenith->Push(false);
            return 1;
        }

        Scripting::Util::Zenith::Unref(zenith, iterator->second.callbackRef);
        self->_callbacks.erase(iterator);
        zenith->Push(true);
        return 1;
    }

    u64 SchedulerHandler::AllocateHandle()
    {
        u64 handle = 0;
        do
        {
            handle = _nextHandle++;
        } while (handle == 0 || _callbacks.contains(handle));
        return handle;
    }
}
