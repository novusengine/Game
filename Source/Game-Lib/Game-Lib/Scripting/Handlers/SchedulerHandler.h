#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

#include <chrono>
#include <unordered_map>

namespace Scripting::Scheduler
{
    class SchedulerHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime);

        static i32 AfterSeconds(Zenith* zenith);
        static i32 AfterFrames(Zenith* zenith);
        static i32 Cancel(Zenith* zenith);

    private:
        using Clock = std::chrono::steady_clock;

        enum class ScheduleType : u8
        {
            Seconds,
            Frames,
        };

        struct PendingCallback
        {
            Zenith* owner = nullptr;
            ScheduleType type = ScheduleType::Seconds;
            Clock::time_point deadline;
            u64 targetFrame = 0;
            i32 callbackRef = -1;
        };

        u64 AllocateHandle();

        u64 _nextHandle = 1;
        std::unordered_map<u64, PendingCallback> _callbacks;
        std::unordered_map<Zenith*, u64> _frames;
    };

    static LuaRegister<> schedulerGlobalMethods[] =
    {
        { "AfterSeconds", SchedulerHandler::AfterSeconds },
        { "AfterFrames", SchedulerHandler::AfterFrames },
        { "Cancel", SchedulerHandler::Cancel },
    };
}
