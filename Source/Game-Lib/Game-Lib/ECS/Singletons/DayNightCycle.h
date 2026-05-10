#pragma once
#include <Base/Types.h>

namespace ECS::Singletons
{
    struct DayNightCycle
    {
    public:
        static constexpr f32 HoursPerDay = 24.0f;
        static constexpr f32 MinutesPerHour = 60.0f;
        static constexpr f32 SecondsPerMinute = 60.0f;
        static constexpr f32 SecondsPerDay = SecondsPerMinute * MinutesPerHour * HoursPerDay;

        // f64: f32 ULP at 86400 is ~4-8 ms, comparable to deltaTime, so an f32
        // accumulator drifts by seconds per minute.
        f64 timeInSeconds = 0.0;
        f32 speedModifier = 1.0f;

        // -1 so the first Update fires the second-changed callback.
        i32 lastIntegerSecond = -1;

        f32 GetTimeInSecondsF32() const { return static_cast<f32>(timeInSeconds); }
    };
}
