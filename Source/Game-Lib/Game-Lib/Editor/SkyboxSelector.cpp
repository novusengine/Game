#include "SkyboxSelector.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Singletons/Skybox.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <filesystem>
#include <string>

using namespace ECS::Singletons;
namespace fs = std::filesystem;

namespace Editor
{
    static const fs::path complexModelPath = fs::path("Data/ComplexModel/");
    static const fs::path skyboxFolderPath = complexModelPath / "environments/stars";
    static const fs::path fileExtension = ".complexmodel";

    struct SkyboxSelectorData : SkyboxSelectorDataBase
    {
        std::vector<fs::path> skyboxPaths;
    };

    SkyboxSelector::SkyboxSelector()
        : BaseEditor(GetName(), true)
    {
        SkyboxSelectorData* data = new SkyboxSelectorData();
        _data = data;

        // Find all skyboxes
        if (!fs::exists(skyboxFolderPath))
        {
            fs::create_directories(skyboxFolderPath);
        }

        // First recursively iterate the directory and find all paths
        std::vector<fs::path> paths;
        std::filesystem::recursive_directory_iterator dirpos{ skyboxFolderPath };
        std::copy(begin(dirpos), end(dirpos), std::back_inserter(paths));

        // Then add them to the list
        for (fs::path& path : paths)
        {
            if (path.extension() != fileExtension)
                continue;

            path = fs::relative(path, complexModelPath);
            data->skyboxPaths.push_back(path);
        }
    }

    bool DrawSkyboxItem(const fs::path& skyboxPath, u32 skyboxID, std::string& filter, u32& selectedSkyboxID, u32& popupSkyboxID)
    {
        const std::string& skyboxName = skyboxPath.stem().string();

        bool hasFilter = filter.length() > 0;
        if (hasFilter)
        {
            std::string skyboxNameLowered = skyboxName;
            std::transform(skyboxNameLowered.begin(), skyboxNameLowered.end(), skyboxNameLowered.begin(), ::tolower);

            if (!StringUtils::Contains(skyboxNameLowered.c_str(), filter))
                return false;
        }

        ImGui::PushID(skyboxID);

        // Render the title and description
        ImGui::Text("%s", skyboxName.c_str());

        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        max.x = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x; // Extend to full width

        bool isSelected = selectedSkyboxID == skyboxID;
        bool isHovered = ImGui::IsMouseHoveringRect(min, max);
        static u32 hoveredColor = ImGui::GetColorU32(ImVec4(0.7f, 0.8f, 1.0f, 0.3f));

        if (!isSelected && isHovered)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                selectedSkyboxID = skyboxID;
        }

        if (isSelected)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, hoveredColor);
        }

        bool isPopupOpen = popupSkyboxID != std::numeric_limits<u32>().max();
        if (isHovered && !isPopupOpen)
        {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                selectedSkyboxID = skyboxID;
                popupSkyboxID = skyboxID;

                ImGuiID popupContextID = ImGui::GetCurrentWindow()->GetID("SkyboxSelectorItemContextMenu");
                ImGui::OpenPopupEx(popupContextID, ImGuiPopupFlags_MouseButtonRight);
            }
        }

        ImGui::PopID();

        return true;
    }

    void SkyboxSelector::ShowListView()
    {
        SkyboxSelectorData& data = *static_cast<SkyboxSelectorData*>(_data);

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->gameRegistry;

        entt::registry::context& ctx = registry.ctx();
        auto& skybox = ctx.get<ECS::Singletons::Skybox>();
        std::string loadedSkyboxPath = "NONE";

        auto& name = registry.get<ECS::Components::Name>(skybox.entity);
        if (name.fullName != "")
        {
            loadedSkyboxPath = name.fullName;
        }

        ImGui::Text("Currently loaded skybox: %s", loadedSkyboxPath.c_str());

        // Begin a list box
        ImGui::Text("Select a skybox (Optionally use the filter below)");

        u32 totalNumSkyboxes = static_cast<u32>(data.skyboxPaths.size());
        u32 numVisibleItems = 14; // Define number of visible items
        f32 itemHeight = ImGui::GetTextLineHeight();
        f32 heightConstraint = itemHeight * (totalNumSkyboxes - 1u);

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
        if (ImGui::BeginListBox("##listbox", vec2(-1.0f, itemHeight * numVisibleItems)))
        {
            static u32 currentSkyboxIDSelected = 0;
            static u32 popupSkyboxID = 0;

            if (totalNumSkyboxes)
            {
                if (hasFilter)
                {
                    for (u32 i = 0; i < data.skyboxPaths.size(); i++)
                    {
                        DrawSkyboxItem(data.skyboxPaths[i], i, currentFilter, currentSkyboxIDSelected, popupSkyboxID);
                    }
                }
                else
                {
                    u32 maxSkyboxEntry = totalNumSkyboxes - 1u;

                    if (hadFilterLastFrame)
                        ImGui::SetScrollY(scrollPosition);

                    scrollPosition = ImGui::GetScrollY();
                    u32 firstItemToView = static_cast<u32>(glm::floor(scrollPosition / itemHeight));
                    u32 lastItemToView = glm::clamp(firstItemToView + numVisibleItems, 0u, maxSkyboxEntry);

                    if (firstItemToView > 0)
                    {
                        ImGui::Dummy(ImVec2(0.0f, firstItemToView * itemHeight));
                    }

                    auto skyboxItr = data.skyboxPaths.begin();
                    std::advance(skyboxItr, firstItemToView);

                    for (u32 i = firstItemToView; i <= lastItemToView; i++, skyboxItr++)
                    {
                        DrawSkyboxItem(*skyboxItr, i, currentFilter, currentSkyboxIDSelected, popupSkyboxID);
                    }

                    if (lastItemToView < maxSkyboxEntry)
                    {
                        u32 numElementsToEnd = maxSkyboxEntry - lastItemToView;

                        ImGui::Dummy(ImVec2(0.0f, numElementsToEnd * itemHeight));
                    }
                }

                if (!ImGui::GetCurrentWindow()->SkipItems)
                {
                    ImGui::PushID(popupSkyboxID);

                    ImGuiID popupContextID = ImGui::GetCurrentWindow()->GetID("SkyboxSelectorItemContextMenu");

                    if (ImGui::BeginPopupEx(popupContextID, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
                    {
                        const fs::path& skyboxPath = data.skyboxPaths[popupSkyboxID];
                        std::string skyboxName = skyboxPath.stem().string();

                        ImGui::Text("Actions for %s", skyboxName.c_str());
                        ImGui::Separator();

                        if (ImGui::MenuItem("Load"))
                        {
                            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

                            auto& model = registry.get<ECS::Components::Model>(skybox.entity);
                            u32 modelHash = modelLoader->GetModelHashFromModelPath(skyboxPath.generic_string());
                            if (!modelLoader->LoadModelForEntity(skybox.entity, model, modelHash))
                            {
                                std::string pathStr = skyboxPath.string();
                                NC_LOG_ERROR("Failed to load skybox model: {0}", pathStr);
                            }
                        }
                        if (ImGui::MenuItem("Unload"))
                        {
                            ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

                            auto& model = registry.get<ECS::Components::Model>(skybox.entity);
                            modelLoader->UnloadModelForEntity(skybox.entity, model);
                        }

                        ImGui::EndPopup();
                    }
                    else
                    {
                        popupSkyboxID = std::numeric_limits<u32>().max();
                    }

                    ImGui::PopID();
                }
            }

            ImGui::EndListBox();
        }
    }

    void SkyboxSelector::DrawImGui()
    {
        if (ImGui::Begin(GetName()))
        {
            ShowListView();
        }
        ImGui::End();
    }
}
