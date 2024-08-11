#include "MapSelector.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Singletons/MapDB.h"
#include "Game-Lib/ECS/Util/MapUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <filesystem>
#include <string>

using namespace ClientDB;
using namespace ECS::Singletons;
namespace fs = std::filesystem;

namespace Editor
{
    MapSelector::MapSelector()
        : BaseEditor(GetName(), true)
    {
    }

    bool DrawMapItem(const ClientDB::Storage<ClientDB::Definitions::Map>* mapStorage, const ClientDB::Definitions::Map* map, std::string& filter, u32& selectedMapID, u32& popupMapID, void** mapIcons)
    {
        static const char* InstanceTypeToName[] =
        {
            "Open World",
            "Dungeon",
            "Raid",
            "Battleground",
            "Arena"
        };

        bool hasFilter = filter.length() > 0;
        if (hasFilter)
        {
            std::string mapNameLowered = map->name;
            std::transform(mapNameLowered.begin(), mapNameLowered.end(), mapNameLowered.begin(), ::tolower);

            if (!StringUtils::Contains(mapNameLowered.c_str(), filter))
                return false;
        }

        ImGui::PushID(map->id);
        ImGui::BeginGroup(); // Start group for the whole row

        u32 instanceType = map->instanceType;
        if (instanceType >= 5)
            instanceType = 0;

        // Render the icon
        ImGui::Image(mapIcons[instanceType], vec2(32, 32)); // Adjust size as needed
        ImGui::SameLine();

        // Render the title and description
        ImGui::BeginGroup();

        ImGui::Text("%s", map->name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Grey color for description

        ImGui::TextWrapped("%u - %s", map->id, InstanceTypeToName[instanceType]);
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        ImGui::EndGroup(); // End group for the whole row

        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        max.x = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x; // Extend to full width

        bool isSelected = selectedMapID == map->id;
        bool isHovered = ImGui::IsMouseHoveringRect(min, max);
        static u32 hoveredColor = ImGui::GetColorU32(ImVec4(0.7f, 0.8f, 1.0f, 0.3f));

        if (!isSelected && isHovered)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                selectedMapID = map->id;
        }

        if (isSelected)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);
        }

        bool isPopupOpen = popupMapID != std::numeric_limits<u32>().max();
        if (isHovered && !isPopupOpen)
        {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                selectedMapID = map->id;
                popupMapID = map->id;

                ImGuiID popupContextID = ImGui::GetCurrentWindow()->GetID("MapSelectorItemContextMenu");
                ImGui::OpenPopupEx(popupContextID, ImGuiPopupFlags_MouseButtonRight);
            }
        }

        ImGui::PopID();

        return true;
    }

    void MapSelector::ShowListViewWithIcons()
    {
        if (!_mapIcons[0])
        {
            Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

            Renderer::TextureDesc textureDesc;

            // Load Continent Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/worldmap/worldmap-icon.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[0] = renderer->GetImguiImageHandle(textureID);
                _mapIconSizes[0] = ImVec2(static_cast<f32>(renderer->GetTextureWidth(textureID)), static_cast<f32>(renderer->GetTextureHeight(textureID)));
            }

            // Load Dungeon Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/minimap/dungeon_icon.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[1] = renderer->GetImguiImageHandle(textureID);
                _mapIconSizes[1] = ImVec2(static_cast<f32>(renderer->GetTextureWidth(textureID)), static_cast<f32>(renderer->GetTextureHeight(textureID)));
            }

            // Load Raid Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/minimap/raid_icon.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[2] = renderer->GetImguiImageHandle(textureID);
                _mapIconSizes[2] = ImVec2(static_cast<f32>(renderer->GetTextureWidth(textureID)), static_cast<f32>(renderer->GetTextureHeight(textureID)));
            }

            // Load Battleground Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/battlefieldframe/ui-battlefield-icon.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[3] = renderer->GetImguiImageHandle(textureID);
                _mapIconSizes[3] = ImVec2(static_cast<f32>(renderer->GetTextureWidth(textureID)), static_cast<f32>(renderer->GetTextureHeight(textureID)));
            }

            // Load Arena Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/calendar/ui-calendar-event-pvp.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[4] = renderer->GetImguiImageHandle(textureID);
                _mapIconSizes[4] = ImVec2(static_cast<f32>(renderer->GetTextureWidth(textureID)), static_cast<f32>(renderer->GetTextureHeight(textureID)));
            }
        }

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->gameRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBCollection = ctx.get<ClientDBCollection>();
        auto* mapStorage = clientDBCollection.Get<Definitions::Map>(ClientDBHash::Map);

        // Begin a list box
        ImGui::Text("Select a map (Optionally use the filter below)");

        u32 totalNumMaps = mapStorage->GetNumRows();
        u32 numVisibleItems = 7; // Define number of visible items
        f32 itemHeight = 42.25f; // Estimate height of one item
        f32 heightConstraint = itemHeight * (totalNumMaps - 1u);

        // Set the size of the ListBox
        ImGui::SetNextItemWidth(-1); // Full width

        static std::string currentFilter;
        static bool hasFilter = false;
        static f32 scrollPosition = 0.0f;
        ImGui::InputText("Filter", &currentFilter);

        bool hadFilterLastFrame = hasFilter;
        hasFilter = currentFilter.length() > 0;
        if (hasFilter)
        {
            std::transform(currentFilter.begin(), currentFilter.end(), currentFilter.begin(), ::tolower);
        }

        ImGui::SetNextItemWidth(-1); // Full width
        if (ImGui::BeginListBox("##listboxwithicons", vec2(-1.0f, itemHeight * numVisibleItems)))
        {
            static u32 currentMapIDSelected = 0;
            static u32 popupMapID = 0;

            if (totalNumMaps)
            {
                if (hasFilter)
                {
                    mapStorage->Each([this](auto* storage, const Definitions::Map* map)
                    {
                        DrawMapItem(storage, map, currentFilter, currentMapIDSelected, popupMapID, _mapIcons);
                    });
                }
                else
                {
                    u32 maxMapEntry = totalNumMaps - 1u;

                    if (hadFilterLastFrame)
                        ImGui::SetScrollY(scrollPosition);

                    scrollPosition = ImGui::GetScrollY();
                    u32 firstItemToView = static_cast<u32>(glm::floor(scrollPosition / itemHeight));
                    u32 lastItemToView = glm::clamp(firstItemToView + numVisibleItems, 0u, maxMapEntry);

                    if (firstItemToView > 0)
                    {
                        ImGui::Dummy(ImVec2(0.0f, firstItemToView * itemHeight));
                    }

                    u32 numMapsToView = lastItemToView - firstItemToView;
                    mapStorage->EachInRange(firstItemToView, lastItemToView, [this](auto* storage, const Definitions::Map* map)
                    {
                        DrawMapItem(storage, map, currentFilter, currentMapIDSelected, popupMapID, _mapIcons);
                    });

                    if (lastItemToView < maxMapEntry)
                    {
                        u32 numElementsToEnd = maxMapEntry - lastItemToView;

                        ImGui::Dummy(ImVec2(0.0f, numElementsToEnd * itemHeight));
                    }
                }

                if (!ImGui::GetCurrentWindow()->SkipItems)
                {
                    ImGui::PushID(popupMapID);

                    ImGuiID popupContextID = ImGui::GetCurrentWindow()->GetID("MapSelectorItemContextMenu");

                    if (ImGui::BeginPopupEx(popupContextID, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
                    {
                        const Definitions::Map* map = mapStorage->GetRow(popupMapID);

                        ImGui::Text("Actions for %s", map->name.c_str());
                        ImGui::Separator();

                        if (ImGui::MenuItem("Load"))
                        {
                            MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();

                            u32 internalMapNameHash = StringUtils::fnv1a_32(map->internalName.c_str(), map->internalName.length());
                            mapLoader->LoadMap(internalMapNameHash);
                        }

                        if (ImGui::MenuItem("Unload"))
                        {
                            MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
                            mapLoader->UnloadMap();
                        }

                        ImGui::EndPopup();
                    }
                    else
                    {
                        popupMapID = std::numeric_limits<u32>().max();
                    }

                    _currentSelectedMapID = currentMapIDSelected;

                    ImGui::PopID();
                }
            }

            ImGui::EndListBox();
        }

        f32 mapHandlerButtonWidth = ImGui::GetWindowSize().x * 0.5f - 19.5f; // Black Magic GUI Math

        if (ImGui::Button("Load Map", ImVec2(mapHandlerButtonWidth, 50.0f)))
        {
            MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();

            const Definitions::Map* map = mapStorage->GetRow(_currentSelectedMapID);

            u32 internalMapNameHash = StringUtils::fnv1a_32(map->internalName.c_str(), map->internalName.length());
            mapLoader->LoadMap(internalMapNameHash);
        }

        ImGui::SameLine();
        if (ImGui::Button("Unload Map", ImVec2(mapHandlerButtonWidth, 50.0f)))
        {
            MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();
            mapLoader->UnloadMap();
        }
    }

    void MapSelector::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
        {
            ShowListViewWithIcons();
        }
        ImGui::End();
    }
}
