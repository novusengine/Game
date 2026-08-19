#pragma once
#include <Base/Types.h>

#include <deque>
#include <string>
#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct FrameTimes
    {
        f32 deltaTimeS;
        f32 simulationFrameTimeS;
        f32 uploadFrameTimeS;
        f32 renderFrameTimeS;
        f32 renderWaitTimeS;
        f32 gpuFrameTimeMS;
    };

    struct EngineStats
    {
        static const u32 MAX_ENTRIES = 120;
        std::deque<FrameTimes> frameStats;

        robin_hood::unordered_map<u32, std::deque<f32>> namedStats;

        void AddTimings(f32 deltaTimeS, f32 simulationFrameTimeS, f32 uploadFrameTimeS, f32 renderFrameTimeS, f32 renderWaitTimeS, f32 gpuFrameTimeMS);
        void AddNamedStat(const std::string& name, f32 time);

        //averages a frame timing from the last {numFrames} frames
        FrameTimes AverageFrame(u32 numFrames) const;
        bool AverageNamed(const std::string& name, u32 numFrames, f32& average) const;
    };
}
