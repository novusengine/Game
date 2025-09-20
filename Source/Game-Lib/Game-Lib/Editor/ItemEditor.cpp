#include "ItemEditor.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Scripting/Database/Item.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <Meta/Generated/Shared/ClientDB.h>

#include <Renderer/Renderer.h>
#include <Renderer/Descriptors/TextureDesc.h>

#include <entt/entt.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <string>

using namespace ClientDB;
using namespace ECS::Singletons;

namespace Editor
{
    ItemEditor::ItemEditor()
        : BaseEditor(GetName())
    {

    }

    struct Option
    {
    public:
        u32 id;
        std::string label;
    };

    static robin_hood::unordered_map<u32, u64> images;
    static bool iconPickerEnabled = false;

    static i32 currentIndex = 0;
    static std::string filter = "";
    static std::string iconFilter = "";
    static std::string lastFilterValue = "";

    static f32 iconSize = 32.0;
    static i32 desiredIconsPerRow = 9;
    static i32 desiredVisibleRows = 10;
    static std::vector<u32> filteredIconIDs;

    static bool statTemplateEditorEnabled = false;
    static bool armorTemplateEditorEnabled = false;
    static bool weaponTemplateEditorEnabled = false;
    static bool shieldTemplateEditorEnabled = false;

    // Dummy icon functions
    u64 GetItemIconTexture(u32 iconID)
    {
        if (!images[iconID])
        {
            auto* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& registry = *registries->dbRegistry;
            entt::registry::context& ctx = registry.ctx();

            auto& clientDBSingleton = ctx.get<ClientDBSingleton>();
            auto* iconStorage = clientDBSingleton.Get(ClientDBHash::Icon);
            const auto& icon = iconStorage->Get<Generated::IconRecord>(iconID);

            Renderer::TextureDesc textureDesc;
            textureDesc.path = iconStorage->GetString(icon.texture);

            Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
            images[iconID] = renderer->GetImguiTextureID(textureID);
        }

        return images[iconID];
    }

    void OpenItemIconPicker()
    {
        iconPickerEnabled = true;
        ImGui::OpenPopup("Icon Picker##ItemEditor");
    }

    void OpenStatTemplateEditor()
    {
        statTemplateEditorEnabled = true;
        ImGui::OpenPopup("Stat Template Editor##ItemEditor");
    }

    void OpenArmorTemplateEditor()
    {
        armorTemplateEditorEnabled = true;
        ImGui::OpenPopup("Armor Template Editor##ItemEditor");
    }

    void OpenWeaponTemplateEditor()
    {
        weaponTemplateEditorEnabled = true;
        ImGui::OpenPopup("Weapon Template Editor##ItemEditor");
    }

    void OpenShieldTemplateEditor()
    {
        shieldTemplateEditorEnabled = true;
        ImGui::OpenPopup("Shield Template Editor##ItemEditor");
    }

    template <typename T>
    bool RenderOptionDropdown(const std::string& label, const char* previewLabel, T& currentID, const std::vector<Option>& options)
    {
        bool result = false;
        T prevVal = currentID;

        ImGui::Text("%s", label.c_str());

        if (ImGui::BeginCombo(("##" + label).c_str(), previewLabel))
        {
            for (const auto& option : options)
            {
                bool isSelected = (currentID == static_cast<T>(option.id));
                if (ImGui::Selectable(option.label.c_str(), isSelected))
                {
                    currentID = static_cast<T>(option.id);
                    result = currentID != prevVal;
                }

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        return result;
    }

    // Main rendering function for the Item Editor UI.
    void RenderItemEditor()
    {
        auto* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->dbRegistry;
        entt::registry::context& ctx = registry.ctx();

        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();

        auto* itemStorage = clientDBSingleton.Get(ClientDBHash::Item);
        auto* itemStatTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemStatTemplate);
        auto* itemArmorTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemArmorTemplate);
        auto* itemWeaponTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemWeaponTemplate);
        auto* itemShieldTemplateStorage = clientDBSingleton.Get(ClientDBHash::ItemShieldTemplate);
        auto* iconStorage = clientDBSingleton.Get(ClientDBHash::Icon);

        bool isItemDirty = false;
        bool isItemStatsDirty = false;
        bool isItemArmorDirty = false;
        bool isItemWeaponDirty = false;
        bool isItemShieldDirty = false;

        if (ImGui::BeginPopupModal("Icon Picker##ItemEditor", &iconPickerEnabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

            ImGui::Text("Filter (ID or Name)");
            if (ImGui::InputText("##Icon Filter (ID or Name)", &iconFilter))
            {
                // Update cache if filter changes
                if (iconFilter.length() > 0 && iconFilter != lastFilterValue)
                {
                    lastFilterValue = iconFilter;
                    filteredIconIDs.clear();
                    filteredIconIDs.reserve(iconStorage->GetNumRows());

                    iconStorage->Each([&iconStorage](u32 id, const Generated::IconRecord& icon) -> bool
                    {
                        const std::string& iconPath = iconStorage->GetString(icon.texture);
                        if (std::to_string(id).find(iconFilter) != std::string::npos || iconPath.find(iconFilter) != std::string::npos)
                        {
                            filteredIconIDs.push_back(id);
                        }
                        return true;
                    });
                }
            }

            ImGui::PopItemWidth();

            bool hasFilter = iconFilter.length() > 0;

            const f32 padX = ImGui::GetStyle().WindowPadding.x;
            const f32 padY = ImGui::GetStyle().WindowPadding.y;
            const f32 scrollbarW = ImGui::GetStyle().ScrollbarSize;

            f32 popupWidth = 100.0f + (desiredIconsPerRow * (iconSize + ImGui::GetStyle().FramePadding.x)) + (2 * padX) + scrollbarW;
            f32 popupHeight = (desiredVisibleRows * ((iconSize + ImGui::GetStyle().FramePadding.y + padY) - 1));
            ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);

            ImGui::BeginChild("IconGrid", ImVec2(popupWidth, popupHeight), true);

            if (!hasFilter)
            {
                i32 totalIcons = iconStorage->GetNumRows();
                i32 totalRows = (totalIcons + desiredIconsPerRow - 1) / desiredIconsPerRow; // Round up

                ImGuiListClipper clipper;
                clipper.Begin(totalRows);

                while (clipper.Step())
                {
                    for (i32 row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        i32 startIndex = row * desiredIconsPerRow;
                        i32 endIndex = glm::min(startIndex + desiredIconsPerRow, totalIcons);
                        i32 numIconsAdded = 0;

                        iconStorage->EachInRange(startIndex, endIndex - startIndex, [&itemStorage, &iconStorage, &numIconsAdded, &isItemDirty](u32 id, const Generated::IconRecord& icon) -> bool
                        {
                            ImGui::PushID(id);

                            if (ImGui::ImageButton("Icon", GetItemIconTexture(id), ImVec2(iconSize, iconSize)))
                            {
                                itemStorage->Get<Generated::ItemRecord>(currentIndex).iconID = id;
                                isItemDirty = true;
                            }

                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("ID: %u", id);
                                ImGui::Text("Path: %s", iconStorage->GetString(icon.texture).c_str());
                                ImGui::EndTooltip();
                            }

                            bool isNextIconOnSameLine = (++numIconsAdded % desiredIconsPerRow) != 0;
                            if (isNextIconOnSameLine)
                                ImGui::SameLine();

                            ImGui::PopID();
                            return true;
                        });
                    }
                }
            }
            else
            {
                i32 totalFilteredIcons = static_cast<i32>(filteredIconIDs.size());
                i32 totalRows = (totalFilteredIcons + desiredIconsPerRow - 1) / desiredIconsPerRow;
                if (totalRows > 0)
                {
                    i32 numIconsAdded = 0;

                    ImGuiListClipper clipper;
                    clipper.Begin(totalRows);
                    clipper.ItemsHeight = (iconSize + ImGui::GetStyle().FramePadding.y + padY) - 1;

                    while (clipper.Step())
                    {
                        for (i32 row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                        {
                            i32 startIndex = row * desiredIconsPerRow;
                            i32 endIndex = glm::min(startIndex + desiredIconsPerRow, totalFilteredIcons);
                            i32 numIconsAdded = 0;

                            for (i32 i = startIndex; i < endIndex; ++i)
                            {
                                u32 id = filteredIconIDs[i];
                                const auto& icon = iconStorage->Get<Generated::IconRecord>(id);

                                ImGui::PushID(id);

                                if (ImGui::ImageButton("Icon", GetItemIconTexture(id), ImVec2(iconSize, iconSize)))
                                {
                                    itemStorage->Get<Generated::ItemRecord>(currentIndex).iconID = id;
                                    isItemDirty = true;
                                }

                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("ID: %u", id);
                                    ImGui::Text("Path: %s", iconStorage->GetString(icon.texture).c_str());
                                    ImGui::EndTooltip();
                                }

                                bool isNextIconOnSameLine = (++numIconsAdded % desiredIconsPerRow) != 0;
                                if (isNextIconOnSameLine)
                                    ImGui::SameLine();

                                ImGui::PopID();
                            }
                        }
                    }
                }
            }

            ImGui::EndChild();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Stat Template Editor##ItemEditor", &statTemplateEditorEnabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            static std::vector<Option> StatTypeOptions =
            {
                {.id = 0, .label = "None" },
                {.id = 1, .label = "Health" },
                {.id = 2, .label = "Mana" },
                {.id = 3, .label = "Stamina" },
                {.id = 4, .label = "Strength" },
                {.id = 5, .label = "Agility" },
                {.id = 6, .label = "Intellect" },
                {.id = 7, .label = "Spirit" }
            };

            auto& currentItem = itemStorage->Get<Generated::ItemRecord>(currentIndex);
            auto& currentStatTemplate = itemStatTemplateStorage->Get<Generated::ItemStatTemplateRecord>(currentItem.statTemplateID);

            ImGui::LabelText("##", "Template ID : %d", currentItem.statTemplateID);
            ImGui::Separator();

            ImGui::Columns(2, nullptr, false);
            for (u32 statIndex = 0; statIndex < 4; statIndex++)
            {
                ImGui::PushID(statIndex);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                u8 currentStatID = currentStatTemplate.statTypeID[statIndex];

                const std::string& currentLabel = StatTypeOptions[currentStatID].label;
                if (RenderOptionDropdown<u8>("StatType", currentLabel.c_str(), currentStatTemplate.statTypeID[statIndex], StatTypeOptions))
                {
                    isItemStatsDirty = true;
                }

                if (ImGui::InputScalar("##StatValue", ImGuiDataType_U32, &currentStatTemplate.value[statIndex]))
                {
                    isItemStatsDirty = true;
                }

                ImGui::PopItemWidth();
                ImGui::PopID();

                bool isOdd = (statIndex % 2) != 0;
                if (isOdd)
                    ImGui::SameLine();

                ImGui::NextColumn();
            }

            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::NextColumn();

            ImGui::Columns(1);
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Armor Template Editor##ItemEditor", &armorTemplateEditorEnabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            static std::vector<Option> EquipTypeOptions =
            {
                {.id = 0,  .label = "None" },
                {.id = 1,  .label = "Head" },
                {.id = 2,  .label = "Necklace" },
                {.id = 3,  .label = "Shoulders" },
                {.id = 4,  .label = "Cloak" },
                {.id = 5,  .label = "Chest" },
                {.id = 6,  .label = "Shirt" },
                {.id = 7,  .label = "Tabard" },
                {.id = 8,  .label = "Bracers" },
                {.id = 9,  .label = "Gloves" },
                {.id = 10, .label = "Belt" },
                {.id = 11, .label = "Pants" },
                {.id = 12, .label = "Boots" },
                {.id = 13, .label = "Ring" },
                {.id = 14, .label = "Trinket" },
                {.id = 15, .label = "Weapon" },
                {.id = 16, .label = "OffHand" },
                {.id = 17, .label = "Ranged" },
                {.id = 18, .label = "Ammo" }
            };

            auto& currentItem = itemStorage->Get<Generated::ItemRecord>(currentIndex);
            auto& currentArmorTemplate = itemArmorTemplateStorage->Get<Generated::ItemArmorTemplateRecord>(currentItem.armorTemplateID);

            ImGui::LabelText("##", "Template ID : %d", currentItem.armorTemplateID);
            ImGui::Separator();

            ImGui::Columns(2, nullptr, false);

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                const std::string& currentLabel = EquipTypeOptions[(u32)currentArmorTemplate.equipType].label;
                if (RenderOptionDropdown<u8>("Equip Type", currentLabel.c_str(), currentArmorTemplate.equipType, EquipTypeOptions))
                {
                    isItemArmorDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::NextColumn();

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                ImGui::Text("Bonus Armor");
                if (ImGui::InputScalar("##BonusArmor", ImGuiDataType_U32, &currentArmorTemplate.bonusArmor))
                {
                    isItemArmorDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::Columns(1);
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Weapon Template Editor##ItemEditor", &weaponTemplateEditorEnabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            static std::vector<Option> WeaponStyleOptions =
            {
                {.id = 1, .label = "Unspecified" },
                {.id = 2, .label = "One-Hand" },
                {.id = 3, .label = "Two-Hand" },
                {.id = 4, .label = "Main Hand" },
                {.id = 5, .label = "Off Hand" },
                {.id = 6, .label = "Ranged" },
                {.id = 7, .label = "Wand" },
                {.id = 8, .label = "Tool" }
            };

            auto& currentItem = itemStorage->Get<Generated::ItemRecord>(currentIndex);
            auto& currentWeaponTemplate = itemWeaponTemplateStorage->Get<Generated::ItemWeaponTemplateRecord>(currentItem.weaponTemplateID);

            ImGui::LabelText("##", "Template ID : %d", currentItem.weaponTemplateID);
            ImGui::Separator();

            ImGui::Columns(2, nullptr, false);

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                const std::string& currentLabel = WeaponStyleOptions[(u32)currentWeaponTemplate.weaponStyle - 1].label;
                if (RenderOptionDropdown<u8>("Weapon Style", currentLabel.c_str(), currentWeaponTemplate.weaponStyle, WeaponStyleOptions))
                {
                    isItemWeaponDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::NextColumn();

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                ImGui::Text("Speed");
                if (ImGui::InputScalar("##Speed", ImGuiDataType_Float, &currentWeaponTemplate.speed))
                {
                    isItemWeaponDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::NextColumn();

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                ImGui::Text("Min Damage");
                if (ImGui::InputScalar("##MinDamage", ImGuiDataType_U32, &currentWeaponTemplate.damageRange.x))
                {
                    isItemWeaponDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::NextColumn();

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                ImGui::Text("Max Damage");
                if (ImGui::InputScalar("##MaxDamage", ImGuiDataType_U32, &currentWeaponTemplate.damageRange.y))
                {
                    isItemWeaponDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::Columns(1);
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Shield Template Editor##ItemEditor", &shieldTemplateEditorEnabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            auto& currentItem = itemStorage->Get<Generated::ItemRecord>(currentIndex);
            auto& currentShieldTemplate = itemShieldTemplateStorage->Get<Generated::ItemShieldTemplateRecord>(currentItem.shieldTemplateID);

            ImGui::LabelText("##", "Template ID : %d", currentItem.shieldTemplateID);
            ImGui::Separator();

            ImGui::Columns(2, nullptr, false);

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                ImGui::Text("Bonus Armor");
                if (ImGui::InputScalar("##BonusArmor", ImGuiDataType_U32, &currentShieldTemplate.bonusArmor))
                {
                    isItemShieldDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::NextColumn();

            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                ImGui::Text("Block");
                if (ImGui::InputScalar("##Block", ImGuiDataType_U32, &currentShieldTemplate.block))
                {
                    isItemShieldDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::Columns(1);
            ImGui::EndPopup();
        }

        // Item selection combo with search filter.
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

            ImGui::Text("Filter (ID or Name)");
            ImGui::InputText("##Item Filter (ID or Name)", &filter);

            auto& currentItem = itemStorage->Get<Generated::ItemRecord>(currentIndex);
            std::string currentItemlabel = std::to_string(currentIndex) + " - " + itemStorage->GetString(currentItem.name);

            ImGui::Text("Select Item");
            if (ImGui::BeginCombo("##Select Item", currentIndex >= 0 ? currentItemlabel.c_str() : "None"))
            {
                char comboLabel[128];
                itemStorage->Each([&](u32 id, Generated::ItemRecord& item) -> bool
                {
                    std::snprintf(comboLabel, sizeof(comboLabel), "%u - %s", id, itemStorage->GetString(item.name).c_str());
                    if (std::strstr(comboLabel, filter.c_str()) != nullptr)
                    {
                        bool isSelected = (currentIndex == id);
                        if (ImGui::Selectable(comboLabel, isSelected))
                        {
                            currentIndex = id;
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    return true;
                });

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::Separator();

        // If an item is selected, show the editing UI.
        if (currentIndex >= 0)
        {
            auto& item = itemStorage->Get<Generated::ItemRecord>(currentIndex);

            // Group: General Information
            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                char nameBuffer[256];
                std::strcpy(nameBuffer, itemStorage->GetString(item.name).c_str());

                ImGui::Columns(2, nullptr, false);
                {
                    // Name field
                    ImGui::Text("Name");
                    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        item.name = itemStorage->AddString(std::string(nameBuffer, strlen(nameBuffer)));
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();

                {
                    ImTextureID iconTexture = GetItemIconTexture(item.iconID);
                    ImGui::Image(iconTexture, ImVec2(64.0f, 64.0f));

                    if (ImGui::IsItemHovered())
                    {
                        auto& currentIcon = iconStorage->Get<Generated::IconRecord>(item.iconID);

                        ImGui::BeginTooltip();
                        ImGui::Text("ID: %u", item.iconID);
                        ImGui::Text("Path: %s", iconStorage->GetString(currentIcon.texture).c_str());
                        ImGui::EndTooltip();
                    }

                    if (ImGui::Button("Pick Icon", ImVec2(64.0f, 20.0f)))
                    {
                        OpenItemIconPicker();
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    static std::vector<Option> BindOptions =
                    {
                        {.id = 0, .label = "None" },
                        {.id = 1, .label = "Binds when pickup up" },
                        {.id = 2, .label = "Binds when equipped" },
                        {.id = 3, .label = "Binds when used" }
                    };

                    const std::string& currentLabel = BindOptions[item.bind].label;
                    if (RenderOptionDropdown<u8>("Bind", currentLabel.c_str(), item.bind, BindOptions))
                    {
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    static std::vector<Option> RarityOptions =
                    {
                        {.id = 1, .label = "Poor" },
                        {.id = 2, .label = "Common" },
                        {.id = 3, .label = "Uncommon" },
                        {.id = 4, .label = "Rare" },
                        {.id = 5, .label = "Epic" },
                        {.id = 6, .label = "Legendary" },
                        {.id = 7, .label = "Artifact" },
                        {.id = 8, .label = "Unique" },
                    };

                    const std::string& currentLabel = RarityOptions[item.rarity - 1].label;
                    if (RenderOptionDropdown<u8>("Rarity", currentLabel.c_str(), item.rarity, RarityOptions))
                    {
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    static std::vector<Option> CategoryOptions =
                    {
                        {.id = 1, .label = "Miscellaneous" },
                        {.id = 2, .label = "Trade Goods" },
                        {.id = 3, .label = "Consumable" },
                        {.id = 4, .label = "Reagent" },
                        {.id = 5, .label = "Container" },
                        {.id = 6, .label = "Quest" },
                        {.id = 7, .label = "Armor" },
                        {.id = 8, .label = "Weapon" }
                    };

                    const std::string& currentLabel = CategoryOptions[item.category - 1].label;
                    if (RenderOptionDropdown<u8>("Category", currentLabel.c_str(), item.category, CategoryOptions))
                    {
                        item.categoryType = 1;
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    static std::vector<std::vector<Option>> CategoryTypeOptions =    
                    {
                        // Miscellaneous
                        {
                            { .id = 1, .label = "Miscellaneous" },
                            { .id = 2, .label = "Reagent" },
                            { .id = 3, .label = "Pet" },
                            { .id = 4, .label = "Mount" },
                            { .id = 5, .label = "Junk" }
                        },

                        // Trade Goods
                        {
                            { .id = 1, .label = "Trade Goods" },
                            { .id = 2, .label = "Meat" },
                            { .id = 3, .label = "Cloth" },
                            { .id = 4, .label = "Leather" },
                            { .id = 5, .label = "Metal and Stone" },
                            { .id = 6, .label = "Herb" },
                            { .id = 7, .label = "Materials" }
                        },

                        // Consumable
                        {
                            { .id = 1, .label = "Consumable" },
                            { .id = 2, .label = "Food and Drink" },
                            { .id = 3, .label = "Bandage" },
                            { .id = 4, .label = "Flask" },
                            { .id = 5, .label = "Elixir" },
                            { .id = 6, .label = "Potion" },
                            { .id = 7, .label = "Scroll" }
                        },

                        // Reagent
                        {
                            { .id = 1, .label = "Reagent" }
                        },

                        // Container
                        {
                            { .id = 1, .label = "Bag" }
                        },

                        // Quest
                        {
                            { .id = 1, .label = "Quest" }
                        },

                        // Armor
                        {
                            {.id = 1, .label = "Armor" },
                            {.id = 2, .label = "Cloth" },
                            {.id = 3, .label = "Leather" },
                            {.id = 4, .label = "Mail" },
                            {.id = 5, .label = "Plate" },
                            {.id = 6, .label = "Shield" },
                            {.id = 7, .label = "Libram" },
                            {.id = 8, .label = "Idol" },
                            {.id = 9, .label = "Totem" }
                        },

                        // Weapon
                        {
                            {.id = 1,  .label = "Weapon" },
                            {.id = 2,  .label = "Sword (One-Handed)" },
                            {.id = 3,  .label = "Sword (Two-Handed)" },
                            {.id = 4,  .label = "Mace (One-Handed)" },
                            {.id = 5,  .label = "Mace (Two-Handed)" },
                            {.id = 6,  .label = "Axe (One-Handed)" },
                            {.id = 7,  .label = "Axe (Two-Handed)" },
                            {.id = 8,  .label = "Dagger" },
                            {.id = 9,  .label = "Fist Weapon" },
                            {.id = 10, .label = "Polearm" },
                            {.id = 11, .label = "Staff" },
                            {.id = 12, .label = "Bow" },
                            {.id = 13, .label = "Crossbow" },
                            {.id = 14, .label = "Gun" },
                            {.id = 15, .label = "Wand" },
                            {.id = 16, .label = "Miscellaneous Tool" },
                            {.id = 17, .label = "Fishing Pole" }
                        }
                    };

                    const std::vector<Option>& typeOptions = CategoryTypeOptions[item.category - 1];
                    const std::string& currentLabel = CategoryTypeOptions[item.category - 1][item.categoryType - 1].label;
                    if (RenderOptionDropdown<u8>("Category Type", currentLabel.c_str(), item.categoryType, typeOptions))
                    {
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    ImGui::Text("Virtual Level");
                    if (ImGui::InputScalar("##Virtual Level", ImGuiDataType_U16, &item.virtualLevel))
                    {
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    ImGui::Text("Required Level");
                    if (ImGui::InputScalar("##Required Level", ImGuiDataType_U16, &item.requiredLevel))
                    {
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    ImGui::Text("Armor");
                    if (ImGui::InputScalar("##Armor", ImGuiDataType_U32, &item.armor))
                    {
                        isItemDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    ImGui::Text("Durability");
                    if (ImGui::InputScalar("##Durability", ImGuiDataType_U32, &item.durability))
                    {
                        isItemDirty = true;
                    }
                }

                ImGui::Columns(1);

                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                // Description field with extra space
                char descBuffer[512];
                std::strcpy(descBuffer, itemStorage->GetString(item.description).c_str());
                ImGui::Text("Description");
                if (ImGui::InputTextMultiline("##Description", descBuffer, sizeof(descBuffer),ImVec2(0.0f, 100.0f), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    item.description = itemStorage->AddString(std::string(descBuffer, strlen(descBuffer)));
                    isItemDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Template Data", ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                {
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                    ImGui::Text("Display ID");
                    if (ImGui::InputScalar("##DisplayID", ImGuiDataType_U32, &item.displayID))
                    {
                        isItemDirty = true;
                    }

                    ImGui::PopItemWidth();
                }

                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                ImGui::BeginColumns("Template Field Columns", 2, ImGuiOldColumnFlags_NoBorder);

                {
                    if (item.statTemplateID > 0)
                    {
                        ImGui::Text("Stat Template ID : %u", item.statTemplateID);

                        {
                            if (ImGui::Button("Detach##StatTemplate"))
                            {
                                item.statTemplateID = 0;
                            }

                            ImGui::SameLine();

                            if (ImGui::Button("Edit##StatTemplate"))
                            {
                                OpenStatTemplateEditor();
                            }
                        }
                    }
                    else
                    {
                        ImGui::Text("Stat Template");

                        if (ImGui::Button("Add##AddStatTemplate"))
                        {
                            itemStatTemplateStorage->Copy(0, item.statTemplateID);
                        }
                    }
                }

                {
                    ImGui::NextColumn();

                    if (item.armorTemplateID > 0)
                    {
                        ImGui::Text("Armor Template ID : %u", item.armorTemplateID);

                        {
                            if (ImGui::Button("Detach##ArmorTemplate"))
                            {
                                item.armorTemplateID = 0;
                            }

                            ImGui::SameLine();

                            if (ImGui::Button("Edit##ArmorTemplate"))
                            {
                                OpenArmorTemplateEditor();
                            }
                        }
                    }
                    else
                    {
                        ImGui::Text("Armor Template");

                        if (ImGui::Button("Add##AddArmorTemplate"))
                        {
                            itemArmorTemplateStorage->Copy(0, item.armorTemplateID);
                        }
                    }
                }

                {
                    ImGui::NextColumn();
                    ImGui::Dummy(ImVec2(0.0f, 15.0f));

                    if (item.weaponTemplateID > 0)
                    {
                        ImGui::Text("Weapon Template ID : %u", item.weaponTemplateID);

                        {
                            if (ImGui::Button("Detach##WeaponTemplate"))
                            {
                                item.weaponTemplateID = 0;
                            }

                            ImGui::SameLine();

                            if (ImGui::Button("Edit##WeaponTemplate"))
                            {
                                OpenWeaponTemplateEditor();
                            }
                        }
                    }
                    else
                    {
                        ImGui::Text("Weapon Template");

                        if (ImGui::Button("Add##AddWeaponTemplate"))
                        {
                            itemWeaponTemplateStorage->Copy(0, item.weaponTemplateID);
                        }
                    }
                }

                {
                    ImGui::NextColumn();
                    ImGui::Dummy(ImVec2(0.0f, 15.0f));

                    if (item.shieldTemplateID > 0)
                    {
                        ImGui::Text("Shield Template ID : %u", item.shieldTemplateID);

                        {
                            if (ImGui::Button("Detach##ShieldTemplate"))
                            {
                                item.shieldTemplateID = 0;
                            }

                            ImGui::SameLine();

                            if (ImGui::Button("Edit##ShieldTemplate"))
                            {
                                OpenShieldTemplateEditor();
                            }
                        }
                    }
                    else
                    {
                        ImGui::Text("Shield Template");

                        if (ImGui::Button("Add##AddShieldTemplate"))
                        {
                            itemShieldTemplateStorage->Copy(0, item.shieldTemplateID);
                        }
                    }
                }

                ImGui::EndColumns();
            }

            ImGui::Dummy(ImVec2(0.0f, 10.0f));
        }

        if (isItemDirty)
        {
            itemStorage->MarkDirty();
        }

        if (isItemStatsDirty)
        {
            itemStatTemplateStorage->MarkDirty();
        }

        if (isItemArmorDirty)
        {
            itemArmorTemplateStorage->MarkDirty();
        }

        if (isItemWeaponDirty)
        {
            itemWeaponTemplateStorage->MarkDirty();
        }

        if (isItemShieldDirty)
        {
            itemShieldTemplateStorage->MarkDirty();
        }
    }

    void ItemEditor::DrawImGui()
    {
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            RenderItemEditor();
        }
        ImGui::End();
    }
}