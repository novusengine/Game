#include "EngineStats.h"

#include <Base/Util/StringUtils.h>

#include <algorithm>

namespace ECS::Singletons
{
    void EngineStats::AddTimings(f32 deltaTimeS, f32 simulationFrameTimeS, f32 uploadFrameTimeS, f32 renderFrameTimeS, f32 renderWaitTimeS, f32 gpuFrameTimeMS)
    {
        FrameTimes newFrame;
        newFrame.deltaTimeS = deltaTimeS;
        newFrame.simulationFrameTimeS = simulationFrameTimeS;
        newFrame.uploadFrameTimeS = uploadFrameTimeS;
        newFrame.renderFrameTimeS = renderFrameTimeS;
        newFrame.renderWaitTimeS = renderWaitTimeS;
        newFrame.gpuFrameTimeMS = gpuFrameTimeMS;

        if (frameStats.size() >= MAX_ENTRIES)
        {
            frameStats.pop_back();
        }

        frameStats.push_front(newFrame);
    }

    void EngineStats::AddNamedStat(const std::string& name, f32 time)
    {
        const u32 hashedName = StringUtils::fnv1a_32(name.c_str(), name.size());
        std::deque<f32>& deque = namedStats[hashedName];

        if (deque.size() >= MAX_ENTRIES)
        {
            deque.pop_back();
        }

        deque.push_front(time);
    }

    FrameTimes EngineStats::AverageFrame(u32 numFrames) const
    {
        if (frameStats.empty() || numFrames == 0)
        {
            return FrameTimes{ 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
        }

        const size_t count = std::min<size_t>(numFrames, frameStats.size());
        FrameTimes averaged = frameStats.front();

        for (u32 i = 1; i < count; i++)
        {
            const FrameTimes& frame = frameStats[i];

            averaged.deltaTimeS += frame.deltaTimeS;
            averaged.simulationFrameTimeS += frame.simulationFrameTimeS;
            averaged.uploadFrameTimeS += frame.uploadFrameTimeS;
            averaged.renderFrameTimeS += frame.renderFrameTimeS;
            averaged.renderWaitTimeS += frame.renderWaitTimeS;
            averaged.gpuFrameTimeMS += frame.gpuFrameTimeMS;
        }

        averaged.deltaTimeS /= count;
        averaged.simulationFrameTimeS /= count;
        averaged.uploadFrameTimeS /= count;
        averaged.renderFrameTimeS /= count;
        averaged.renderWaitTimeS /= count;
        averaged.gpuFrameTimeMS /= count;

        return averaged;
    }

    bool EngineStats::AverageNamed(const std::string& name, u32 numFrames, f32& average) const
    {
        average = 0.0f;

        const u32 hashedName = StringUtils::fnv1a_32(name.c_str(), name.size());
        const auto existing = namedStats.find(hashedName);
        if (existing == namedStats.end() || existing->second.empty() || numFrames == 0)
        {
            return false;
        }

        const std::deque<f32>& deque = existing->second;
        const size_t count = std::min<size_t>(numFrames, deque.size());

        average = deque.front();
        for (u32 i = 1; i < count; i++)
        {
            average += deque[i];
        }
        average /= count;

        return true;
    }
}
