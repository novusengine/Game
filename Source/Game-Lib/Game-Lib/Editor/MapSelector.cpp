#include "MapSelector.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Util/Database/MapUtil.h"
#include "Game-Lib/Gameplay/MapLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <Input/InputManager.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>

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
        : BaseEditor(GetName(), BaseEditorFlags_DefaultVisible)
    {
    }

    bool DrawMapItem(ClientDB::Data* mapStorage, u32 mapID, const MetaGen::Shared::ClientDB::MapRecord& map, std::string& filter, u32& selectedMapID, u32& popupMapID, ImTextureID* mapIcons)
    {
        static const char* InstanceTypeToName[] =
        {
            "Open World",
            "Dungeon",
            "Raid",
            "Battleground",
            "Arena"
        };

        const std::string& mapName = mapStorage->GetString(map.name);

        bool hasFilter = filter.length() > 0;
        if (hasFilter)
        {
            std::string mapNameLowered = mapName;
            std::transform(mapNameLowered.begin(), mapNameLowered.end(), mapNameLowered.begin(), ::tolower);

            if (!StringUtils::Contains(mapNameLowered.c_str(), filter))
                return false;
        }

        ImGui::PushID(mapID);
        ImGui::BeginGroup(); // Start group for the whole row

        u32 instanceType = map.instanceType;
        if (instanceType >= 5)
            instanceType = 0;

        // Render the icon
        ImGui::Image(mapIcons[instanceType], vec2(32, 32)); // Adjust size as needed
        ImGui::SameLine();

        // Render the title and description
        ImGui::BeginGroup();

        ImGui::Text("%s", mapName.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Grey color for description

        ImGui::TextWrapped("%u - %s", mapID, InstanceTypeToName[instanceType]);
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        ImGui::EndGroup(); // End group for the whole row

        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        max.x = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x; // Extend to full width

        bool isSelected = selectedMapID == mapID;
        bool isHovered = !ServiceLocator::GetInputManager()->IsCursorVirtual() && ImGui::IsMouseHoveringRect(min, max);
        static u32 hoveredColor = ImGui::GetColorU32(ImVec4(0.7f, 0.8f, 1.0f, 0.3f));

        if (!isSelected && isHovered)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                selectedMapID = mapID;
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
                selectedMapID = mapID;
                popupMapID = mapID;

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
                _mapIcons[0] = renderer->GetImguiTextureID(textureID);
                Renderer::TextureBaseDesc textureBaseDesc = renderer->GetDesc(textureID);
                _mapIconSizes[0] = ImVec2(static_cast<f32>(textureBaseDesc.width), static_cast<f32>(textureBaseDesc.height));
            }

            // Load Dungeon Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/minimap/dungeon_icon.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[1] = renderer->GetImguiTextureID(textureID);
                Renderer::TextureBaseDesc textureBaseDesc = renderer->GetDesc(textureID);
                _mapIconSizes[1] = ImVec2(static_cast<f32>(textureBaseDesc.width), static_cast<f32>(textureBaseDesc.height));
            }

            // Load Raid Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/minimap/raid_icon.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[2] = renderer->GetImguiTextureID(textureID);
                Renderer::TextureBaseDesc textureBaseDesc = renderer->GetDesc(textureID);
                _mapIconSizes[2] = ImVec2(static_cast<f32>(textureBaseDesc.width), static_cast<f32>(textureBaseDesc.height));
            }

            // Load Battleground Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/battlefieldframe/ui-battlefield-icon.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[3] = renderer->GetImguiTextureID(textureID);
                Renderer::TextureBaseDesc textureBaseDesc = renderer->GetDesc(textureID);
                _mapIconSizes[3] = ImVec2(static_cast<f32>(textureBaseDesc.width), static_cast<f32>(textureBaseDesc.height));
            }

            // Load Arena Map Icon
            {
                fs::path path = fs::absolute("Data/Texture/interface/calendar/ui-calendar-event-pvp.dds");
                textureDesc.path = path.string();
                Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
                _mapIcons[4] = renderer->GetImguiTextureID(textureID);
                Renderer::TextureBaseDesc textureBaseDesc = renderer->GetDesc(textureID);
                _mapIconSizes[4] = ImVec2(static_cast<f32>(textureBaseDesc.width), static_cast<f32>(textureBaseDesc.height));
            }
        }

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->dbRegistry;

        entt::registry::context& ctx = registry.ctx();

        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();
        auto* mapStorage = clientDBSingleton.Get(ClientDBHash::Map);

        // Begin a list box
        ImGui::Text("Select a map (Optionally use the filter below)");

        u32 totalNumMaps = mapStorage->GetNumRows();
        u32 numVisibleItems = 10; // Define number of visible items
        f32 itemHeight = 37.0f; // Estimate height of one item
        f32 heightConstraint = itemHeight * (totalNumMaps - 1u);

        // Set the size of the ListBox
        ImGui::SetNextItemWidth(-1); // Full width

        static std::string currentFilter;
        static bool hasFilter = false;
        static f32 scrollPosition = 0.0f;
        ImGui::InputText("##Filter", &currentFilter);

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
                    mapStorage->Each([this, &mapStorage](u32 id, const MetaGen::Shared::ClientDB::MapRecord& map) -> bool
                    {
                        DrawMapItem(mapStorage, id, map, currentFilter, currentMapIDSelected, popupMapID, reinterpret_cast<ImTextureID*>(&_mapIcons[0]));
                        return true;
                    });
                }
                else
                {
                    ImGuiListClipper clipper;
                    clipper.Begin(totalNumMaps, itemHeight);

                    while (clipper.Step())
                    {
                        u32 start = clipper.DisplayStart;
                        u32 count = (clipper.DisplayEnd - clipper.DisplayStart);

                        mapStorage->EachInRange(start, count, [this, &mapStorage](u32 id, const MetaGen::Shared::ClientDB::MapRecord& map) -> bool
                        {
                            DrawMapItem(mapStorage, id, map, currentFilter, currentMapIDSelected, popupMapID, reinterpret_cast<ImTextureID*>(&_mapIcons[0]));
                            return true;
                        });
                    }
                }

                if (!ImGui::GetCurrentWindow()->SkipItems)
                {
                    ImGui::PushID(popupMapID);

                    ImGuiID popupContextID = ImGui::GetCurrentWindow()->GetID("MapSelectorItemContextMenu");

                    if (ImGui::BeginPopupEx(popupContextID, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
                    {
                        const auto& map = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(popupMapID);
                        const std::string& mapInternalName = mapStorage->GetString(map.nameInternal);
                        const std::string& mapName = mapStorage->GetString(map.name);

                        ImGui::Text("Actions for %s", mapName.c_str());
                        ImGui::Separator();

                        if (ImGui::MenuItem("Load"))
                        {
                            MapLoader* mapLoader = ServiceLocator::GetGameRenderer()->GetMapLoader();

                            u32 internalMapNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
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

            const auto& map = mapStorage->Get<MetaGen::Shared::ClientDB::MapRecord>(_currentSelectedMapID);
            const std::string& mapInternalName = mapStorage->GetString(map.nameInternal);

            u32 internalMapNameHash = StringUtils::fnv1a_32(mapInternalName.c_str(), mapInternalName.length());
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
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            ShowListViewWithIcons();
        }
        ImGui::End();
    }
}
