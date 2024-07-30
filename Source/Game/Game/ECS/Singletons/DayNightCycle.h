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

        f32 timeInSeconds = 0.0f;
        f32 speedModifier = 1.0f;
    };
}