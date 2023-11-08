#include "MapSelector.h"

#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/ClientDBCollection.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/ECS/Util/MapUtil.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <string>

using namespace ClientDB;
using namespace ECS::Singletons;

namespace Editor
{
    MapSelector::MapSelector()
        : BaseEditor(GetName(), true)
	{

	}

	void MapSelector::DrawImGui()
	{
        // Print position
        if (ImGui::Begin(GetName()))
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& registry = *registries->gameRegistry;
            
            entt::registry::context& ctx = registry.ctx();
            
            auto& clientDBCollection = ctx.get<ClientDBCollection>();
            auto mapStorage = clientDBCollection.Get<Definitions::Map>(ClientDBHash::Map);

            u32 numMaps = mapStorage.Count();
            if (numMaps > 0)
            {
                static u32 currentMapID = 0;
                static u32 previewID = 0;
            
                static std::string currentFilter;
                static std::string tempString;
            
                if (!mapStorage.Contains(currentMapID))
                {
                    currentMapID = 0;
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
                    previewID = currentMapID;
                }
            
                ImGui::Text("Select a map");
            
                const Definitions::Map& currentPreviewMap = mapStorage.GetByID(previewID);
                const std::string& previewMapName = mapStorage.GetString(currentPreviewMap.name);
                std::string preview = std::to_string(previewID) + " - " + previewMapName;
            
                if (ImGui::BeginCombo("##maplist", preview.c_str()))
                {
                    std::string selectableName;
                    selectableName.reserve(128);
            
                    for (const Definitions::Map& map : mapStorage)
                    {
                        if (!mapStorage.IsValid(map))
                            continue;
            
                        if (!mapStorage.HasString(map.name))
                            continue;
            
                        const std::string& mapName = mapStorage.GetString(map.name);
            
                        tempString.resize(mapName.length());
                        std::transform(mapName.begin(), mapName.end(), tempString.begin(), [](char c) { return std::tolower((i32)c); });
            
                        if (tempString.find(currentFilter) == std::string::npos)
                            continue;
            
                        bool isSelected = currentMapID == map.GetID();
            
                        selectableName = std::to_string(map.GetID()) + " - " + mapName;
                        if (ImGui::Selectable(selectableName.c_str(), &isSelected))
                        {
                            currentMapID = map.GetID();
                            previewID = map.GetID();
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
                        const Definitions::Map& currentPreviewMap = mapStorage.GetByID(currentMapID);
            
                        if (mapStorage.HasString(currentPreviewMap.name))
                        {
                            const std::string& previewMapName = mapStorage.GetString(currentPreviewMap.name);
            
                            tempString.resize(previewMapName.length());
                            std::transform(previewMapName.begin(), previewMapName.end(), tempString.begin(), [](char c) { return std::tolower((i32)c); });
            
                            preview = std::to_string(currentMapID) + " - " + previewMapName;
                        }
                        else if (tempString.size() > 0)
                        {
                            tempString.clear();
                        }
            
                        if (tempString.find(currentFilter) != std::string::npos)
                        {
                            previewID = currentMapID;
                            preview.clear();
                        }
                        else
                        {
                            for (const Definitions::Map& map : mapStorage)
                            {
                                if (!mapStorage.IsValid(map))
                                    continue;
            
                                if (!mapStorage.HasString(map.name))
                                    continue;
            
                                const std::string& mapName = mapStorage.GetString(map.name);
                                tempString.resize(mapName.length());
                                std::transform(mapName.begin(), mapName.end(), tempString.begin(), [](char c) { return std::tolower((i32)c); });
            
                                if (tempString.find(currentFilter) == std::string::npos)
                                    continue;
            
                                previewID = map.GetID();
                            }
                        }
                    }
                }

                if (ImGui::Button("Load"))
                {
                    if (mapStorage.Contains(previewID))
                    {
                        Definitions::Map& previewMap = mapStorage.GetByID(previewID);
                        previewMap.expansion = 2;
            
                        if (mapStorage.HasString(previewMap.internalName))
                        {
                            const std::string& internalName = mapStorage.GetString(previewMap.internalName);
            
                            MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
            
                            u32 internalMapNameHash = StringUtils::fnv1a_32(internalName.c_str(), internalName.length());
                            mapLoader->LoadMap(internalMapNameHash);
                        }
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
