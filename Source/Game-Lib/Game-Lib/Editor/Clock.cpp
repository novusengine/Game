#include "Clock.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/DayNightCycle.h"
#include "Game-Lib/ECS/Systems/UpdateDayNightCycle.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <imgui/imgui.h>

#include <string>

using namespace ECS;

namespace Editor
{
    Clock::Clock()
        : BaseEditor(GetName(), true)
    {

    }

    void Clock::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& registry = *registries->gameRegistry;
            entt::registry::context& ctx = registry.ctx();

            auto& dayNightCycle = ctx.get<Singletons::DayNightCycle>();

            // Direct Time Manipulations
            {
                if (ImGui::Button("Reset Time"))
                {
                    Systems::UpdateDayNightCycle::SetTimeToDefault(registry);
                }

                ImGui::SameLine();

                if (ImGui::Button("Set Time to Noon"))
                {
                    f32 noonTime = Singletons::DayNightCycle::SecondsPerDay / 2.0f;
                    Systems::UpdateDayNightCycle::SetTime(registry, noonTime);
                }
            }

            // Time Speed Manipulation
            {
                f32 timeMultiplier = dayNightCycle.speedModifier;

                bool resetTime = ImGui::Button("Reset Speed");
                if (resetTime)
                {
                    timeMultiplier = 1.0;
                }

                ImGui::SameLine();
                bool inputChanged = ImGui::InputFloat("##", &timeMultiplier, 1.0f, 10.f, "%.2f");

                if (resetTime || inputChanged)
                    dayNightCycle.speedModifier = timeMultiplier;
            }

            u32 totalSeconds = static_cast<u32>(dayNightCycle.timeInSeconds);

            u32 hours = totalSeconds / 60 / 60;
            u32 mins = (totalSeconds / 60) - hours * 60;
            u32 seconds = totalSeconds - (hours * 60 * 60) - (mins * 60);

            std::string hoursStr = hours < 10 ? std::string("0").append(std::to_string(hours)) : std::to_string(hours);
            std::string minsStr = mins < 10 ? std::string("0").append(std::to_string(mins)) : std::to_string(mins);
            std::string secondsStr = seconds < 10 ? std::string("0").append(std::to_string(seconds)) : std::to_string(seconds);

            ImGui::Text("%s:%s:%s", hoursStr.c_str(), minsStr.c_str(), secondsStr.c_str());
        }
        ImGui::End();
    }
}