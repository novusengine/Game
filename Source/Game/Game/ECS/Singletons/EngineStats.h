#pragma once
#include <Base/Types.h>

#include <deque>
#include <robinhood/robinhood.h>

namespace ECS::Singletons
{
    struct EngineStats
    {
        static const u32 MAX_ENTRIES = 120;

        struct Frame
        {
            f32 deltaTimeS;
            f32 simulationFrameTimeS;
            f32 renderFrameTimeS;
            f32 gpuFrameTimeMS;
        };
        std::deque<Frame> frameStats;

        robin_hood::unordered_map<u32, std::deque<f32>> namedStats;

        void AddTimings(f32 deltaTimeS, f32 simulationFrameTimeS, f32 renderFrameTimeS, f32 gpuFrameTimeMS)
        {
            Frame newFrame;
            newFrame.deltaTimeS = deltaTimeS;
            newFrame.simulationFrameTimeS = simulationFrameTimeS;
            newFrame.renderFrameTimeS = renderFrameTimeS;
            newFrame.gpuFrameTimeMS = gpuFrameTimeMS;

            if (frameStats.size() > MAX_ENTRIES)
            {
                frameStats.pop_back();
            }

            frameStats.push_front(newFrame);
        }

        void AddNamedStat(const std::string& name, f32 time)
        {
            u32 hashedName = StringUtils::fnv1a_32(name.c_str(), name.size());
            std::deque<f32>& deque = namedStats[hashedName];

            if (deque.size() > MAX_ENTRIES)
            {
                deque.pop_back();
            }

            deque.push_front(time);
        }

        //averages a frame timing from the last {numFrames} frames
        Frame AverageFrame(int numFrames)
        {
            if (frameStats.size() > 0)
            {
                size_t count = (size_t)numFrames;
                if (numFrames > frameStats.size())
                {
                    count = frameStats.size();
                }

                Frame averaged = frameStats.front();

                for (uint32_t i = 1; i < count; i++)
                {
                    Frame f = frameStats[i];

                    averaged.deltaTimeS += f.deltaTimeS;
                    averaged.simulationFrameTimeS += f.simulationFrameTimeS;
                    averaged.renderFrameTimeS += f.renderFrameTimeS;
                    averaged.gpuFrameTimeMS += f.gpuFrameTimeMS;
                }

                averaged.deltaTimeS /= count;
                averaged.simulationFrameTimeS /= count;
                averaged.renderFrameTimeS /= count;
                averaged.gpuFrameTimeMS /= count;

                return averaged;
            }
            else
            {
                return Frame{ 0.f,0.f,0.f,0.f };
            }
        }

        bool AverageNamed(const std::string& name, int numFrames, f32& average)
        {
            average = 0.0f;

            u32 hashedName = StringUtils::fnv1a_32(name.c_str(), name.size());

            if (!namedStats.contains(hashedName))
            {
                return false;
            }

            std::deque<f32>& deque = namedStats[hashedName];

            size_t count = (size_t)numFrames;
            if (numFrames > deque.size())
            {
                count = deque.size();
            }

            average = deque.front();
            for (u32 i = 1; i < count; i++)
            {
                average += deque[i];
            }
            average /= count;

            return true;
        }
    };
}