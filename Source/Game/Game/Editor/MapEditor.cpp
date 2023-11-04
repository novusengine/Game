#include "MapEditor.h"

#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/ECS/Util/MapDBUtil.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <string>

namespace Editor
{
    MapEditor::MapEditor()
        : BaseEditor(GetName(), true)
	{

	}

	void MapEditor::DrawImGui()
	{
        // Print position
        if (ImGui::Begin(GetName()))
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& registry = *registries->gameRegistry;

            entt::registry::context& ctx = registry.ctx();

            auto& mapDB = ctx.get<ECS::Singletons::MapDB>();
            const std::vector<std::string>& mapNames = mapDB.mapNames;

            u32 numMaps = static_cast<u32>(mapNames.size());
            if (numMaps > 0)
            {
                static const std::string* currentMap = nullptr;
                static const std::string* preview = nullptr;
                static std::string currentFilter = "";
                static std::string tempString = "";

                if (currentMap == nullptr)
                {
                    currentMap = &mapNames[0];
                }

                ImGui::Text("Filter");
                ImGui::InputText("##filter", &currentFilter);

                bool hasFilter = currentFilter.length() != 0;
                if (hasFilter)
                {
                    std::transform(currentFilter.begin(), currentFilter.end(), currentFilter.begin(), [](char c) { return std::tolower((i32)c); });
                }
                else
                {
                    preview = currentMap;
                }

                ImGui::Text("Select a map");

                if (ImGui::BeginCombo("##maplist", preview->c_str()))
                {
                    for (u32 i = 0; i < numMaps; i++)
                    {
                        const std::string& mapName = mapNames[i];

                        tempString.resize(mapName.length());
                        std::transform(mapName.begin(), mapName.end(), tempString.begin(), [](char c) { return std::tolower((i32)c); });

                        if (tempString.find(currentFilter) == std::string::npos)
                            continue;

                        bool isSelected = *currentMap == mapName;

                        if (ImGui::Selectable(mapName.c_str(), &isSelected))
                        {
                            currentMap = &mapName;
                            preview = &mapName;
                        }

                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }

                    ImGui::EndCombo();
                }
                else
                {
                    if (hasFilter)
                    {
                        tempString.resize(currentMap->length());
                        std::transform(currentMap->begin(), currentMap->end(), tempString.begin(), [](char c) { return std::tolower((i32)c); });

                        if (tempString.find(currentFilter) != std::string::npos)
                        {
                            preview = currentMap;
                        }
                        else
                        {
                            for (u32 i = 0; i < numMaps; i++)
                            {
                                const std::string& mapName = mapNames[i];

                                tempString.resize(mapName.length());
                                std::transform(mapName.begin(), mapName.end(), tempString.begin(), [](char c) { return std::tolower((i32)c); });

                                if (tempString.find(currentFilter) == std::string::npos)
                                    continue;

                                preview = &mapName;
                            }
                        }
                    }
                }

                if (ImGui::Button("Load"))
                {
                    if (preview && preview->length() > 0)
                    {
                        MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();

                        u32 mapNameHash = StringUtils::fnv1a_32(preview->c_str(), preview->length());
                        mapLoader->LoadMap(mapNameHash);
                    }
                }

                ImGui::SameLine();

                if (ImGui::Button("Unload"))
                {
                    MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
                    mapLoader->UnloadMap();
                }
            }
            else
            {
                ImGui::Text("No Map Entries were found!");
            }
        }
        ImGui::End();
	}
}