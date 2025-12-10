#include "SpellEditor.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/ClientDBSingleton.h"
#include "Game-Lib/ECS/Singletons/Database/SpellSingleton.h"
#include "Game-Lib/ECS/Util/Database/SpellUtil.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Scripting/Database/Item.h"

#include <Base/CVarSystem/CVarSystemPrivate.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Spell/Spell.h>

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
    SpellEditor::SpellEditor()
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
    static i32 currentEffectID = 0;
    static std::string filter = "";
    static std::string iconFilter = "";
    static std::string lastFilterValue = "";

    static f32 iconSize = 32.0;
    static i32 desiredIconsPerRow = 9;
    static i32 desiredVisibleRows = 10;
    static std::vector<u32> filteredIconIDs;

    static bool effectEditorEnabled = false;

    // Dummy icon functions
    u64 GetSpellIconTexture(u32 iconID)
    {
        if (!images[iconID])
        {
            auto* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            entt::registry& registry = *registries->dbRegistry;
            entt::registry::context& ctx = registry.ctx();

            auto& clientDBSingleton = ctx.get<ClientDBSingleton>();
            auto* iconStorage = clientDBSingleton.Get(ClientDBHash::Icon);
            const auto& icon = iconStorage->Get<MetaGen::Shared::ClientDB::IconRecord>(iconID);

            Renderer::TextureDesc textureDesc;
            textureDesc.path = iconStorage->GetString(icon.texture);

            Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);
            images[iconID] = renderer->GetImguiTextureID(textureID);
        }

        return images[iconID];
    }

    void OpenSpellIconPicker()
    {
        iconPickerEnabled = true;
        ImGui::OpenPopup("Icon Picker##SpellEditor");
    }

    void OpenSpellEffectsEditor()
    {
        effectEditorEnabled = true;
        ImGui::OpenPopup("Effects Editor##SpellEditor");
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

    template <typename T>
    concept GeneratedEnumMetaConcept = requires(T t)
    {
        typename T::Type;

        { T::EnumID } -> std::convertible_to<u16>;
        { T::EnumName } -> std::convertible_to<std::string_view>;
        { T::EnumList } -> std::same_as<const std::array<std::pair<std::string_view, typename T::Type>, std::tuple_size_v<decltype(T::EnumList)>>&>;
    };

    template <GeneratedEnumMetaConcept T>
    bool RenderEnumDropdown(const std::string& label, typename T::Type& currentID)
    {
        bool result = false;
        typename T::Type prevVal = currentID;

        ImGui::Text("%s", label.c_str());

        std::string_view previewLabel = T::EnumList[currentID].first;
        if (ImGui::BeginCombo(("##" + label).c_str(), previewLabel.data()))
        {
            u32 numEnumValues = static_cast<u32>(T::EnumList.size());
            u32 lastEntry = static_cast<u32>(glm::max(0, static_cast<i32>(numEnumValues - 1)));

            for (u32 i = 1; i < lastEntry; i++)
            {
                const auto& option = T::EnumList[i];

                bool isSelected = (currentID == option.second);
                if (ImGui::Selectable(option.first.data(), isSelected))
                {
                    currentID = option.second;
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

    bool RenderEffectEnumDropDown(const std::string& label, MetaGen::Shared::Spell::SpellEffectTypeEnumMeta::Type& currentID)
    {
        bool result = false;
        MetaGen::Shared::Spell::SpellEffectTypeEnumMeta::Type prevVal = currentID;

        ImGui::Text("%s", label.c_str());

        MetaGen::Shared::Spell::SpellEffectTypeEnumMeta::Type enumlistIndex = currentID;
        if (enumlistIndex >= static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnumMeta::Type>(MetaGen::Shared::Spell::SpellEffectTypeEnum::AuraApply))
            enumlistIndex -= 125;

        std::string_view previewLabel = MetaGen::Shared::Spell::SpellEffectTypeEnumMeta::ENUM_FIELD_LIST[enumlistIndex].first;

        if (ImGui::BeginCombo(("##" + label).c_str(), previewLabel.data()))
        {
            u32 numEnumValues = static_cast<u32>(MetaGen::Shared::Spell::SpellEffectTypeEnumMeta::ENUM_FIELD_LIST.size());
            u32 lastEntry = static_cast<u32>(glm::max(0, static_cast<i32>(numEnumValues - 1)));

            for (u32 i = 1; i < lastEntry; i++)
            {
                const auto& option = MetaGen::Shared::Spell::SpellEffectTypeEnumMeta::ENUM_FIELD_LIST[i];

                bool isSelected = (currentID == option.second);
                if (ImGui::Selectable(option.first.data(), isSelected))
                {
                    currentID = option.second;
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

    // Main rendering function for the Spell Editor UI.
    void RenderSpellEditor()
    {
        auto* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        entt::registry& registry = *registries->dbRegistry;
        entt::registry::context& ctx = registry.ctx();

        auto& spellSingleton = ctx.get<SpellSingleton>();
        auto& clientDBSingleton = ctx.get<ClientDBSingleton>();

        auto* spellStorage = clientDBSingleton.Get(ClientDBHash::Spell);
        auto* spellEffectsStorage = clientDBSingleton.Get(ClientDBHash::SpellEffects);
        auto* iconStorage = clientDBSingleton.Get(ClientDBHash::Icon);

        bool isSpellDirty = false;
        bool isSpellEffectsDirty = false;

        if (ImGui::BeginPopupModal("Icon Picker##SpellEditor", &iconPickerEnabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
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

                    iconStorage->Each([&iconStorage](u32 id, const MetaGen::Shared::ClientDB::IconRecord& icon) -> bool
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

                        iconStorage->EachInRange(startIndex, endIndex - startIndex, [&spellStorage, &iconStorage, &numIconsAdded, &isSpellDirty](u32 id, const MetaGen::Shared::ClientDB::IconRecord& icon) -> bool
                        {
                            ImGui::PushID(id);

                            if (ImGui::ImageButton("Icon", GetSpellIconTexture(id), ImVec2(iconSize, iconSize)))
                            {
                                spellStorage->Get<MetaGen::Shared::ClientDB::SpellRecord>(currentIndex).iconID = id;
                                isSpellDirty = true;
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
                                const auto& icon = iconStorage->Get<MetaGen::Shared::ClientDB::IconRecord>(id);

                                ImGui::PushID(id);

                                if (ImGui::ImageButton("Icon", GetSpellIconTexture(id), ImVec2(iconSize, iconSize)))
                                {
                                    spellStorage->Get<MetaGen::Shared::ClientDB::SpellRecord>(currentIndex).iconID = id;
                                    isSpellDirty = true;
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

        bool effectEditorWasOpen = effectEditorEnabled;
        ImGui::SetNextWindowSizeConstraints(ImVec2(500, 300), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginPopupModal("Effects Editor##SpellEditor", &effectEditorEnabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            static robin_hood::unordered_map<MetaGen::Shared::Spell::SpellEffectTypeEnum, std::vector<std::string>> effectTypeToFieldNames =
            {
                { MetaGen::Shared::Spell::SpellEffectTypeEnum::Dummy, { "Unused", "Unused", "Unused", "Unused", "Unused", "Unused" }},
                { MetaGen::Shared::Spell::SpellEffectTypeEnum::WeaponDamage, { "Calculation Type", "Weapon Slot", "Unused", "Unused", "Unused", "Unused" }},
                { MetaGen::Shared::Spell::SpellEffectTypeEnum::AuraApply, { "Spell ID", "Stacks", "Unused", "Unused", "Unused", "Unused" }},
                { MetaGen::Shared::Spell::SpellEffectTypeEnum::AuraRemove, { "Spell ID", "Stacks", "Unused", "Unused", "Unused", "Unused" }},
                { MetaGen::Shared::Spell::SpellEffectTypeEnum::AuraPeriodicDamage, { "Interval (MS)", "Min Damage", "Max Damage", "Damage School", "Unused", "Unused" }},
                { MetaGen::Shared::Spell::SpellEffectTypeEnum::AuraPeriodicHeal, { "Interval (MS)", "Min Heal", "Max Heal", "Heal School", "Unused", "Unused" }}
            };
            
            auto& currentSpell = spellStorage->Get<MetaGen::Shared::ClientDB::SpellRecord>(currentIndex);
            auto& currentSpellEffect = spellEffectsStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(currentEffectID);
            
            ImGui::LabelText("##", "Effect ID : %d", currentEffectID);
            ImGui::Separator();

            ImGui::Columns(2, nullptr, false);
            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                ImGui::Text("Effect Priority");
                if (ImGui::InputScalar("##EffectPriority", ImGuiDataType_U8, &currentSpellEffect.effectPriority))
                {
                    isSpellEffectsDirty = true;
                }
                ImGui::PopItemWidth();

                ImGui::NextColumn();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                if (RenderEffectEnumDropDown("Effect Type", currentSpellEffect.effectType))
                {
                    isSpellEffectsDirty = true;
                }

                ImGui::PopItemWidth();
            }
            ImGui::Columns(1);

            ImGui::Columns(3, nullptr, false);
            {
                auto spellEffectType = static_cast<MetaGen::Shared::Spell::SpellEffectTypeEnum>(currentSpellEffect.effectType);
                const std::vector<std::string>& fieldNames = effectTypeToFieldNames[spellEffectType];

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::Text(fieldNames[0].c_str());
                if (ImGui::InputScalar("##EffectValue1", ImGuiDataType_S32, &currentSpellEffect.effectValues[0]))
                {
                    isSpellEffectsDirty = true;
                }
                ImGui::PopItemWidth();

                ImGui::NextColumn();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::Text(fieldNames[1].c_str());
                if (ImGui::InputScalar("##EffectValue2", ImGuiDataType_S32, &currentSpellEffect.effectValues[1]))
                {
                    isSpellEffectsDirty = true;
                }
                ImGui::PopItemWidth();

                ImGui::NextColumn();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::Text(fieldNames[2].c_str());
                if (ImGui::InputScalar("##EffectValue3", ImGuiDataType_S32, &currentSpellEffect.effectValues[2]))
                {
                    isSpellEffectsDirty = true;
                }
                ImGui::PopItemWidth();

                ImGui::NextColumn();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::Text(fieldNames[3].c_str());
                if (ImGui::InputScalar("##EffectMiscValue1", ImGuiDataType_S32, &currentSpellEffect.effectMiscValues[0]))
                {
                    isSpellEffectsDirty = true;
                }
                ImGui::PopItemWidth();

                ImGui::NextColumn();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::Text(fieldNames[4].c_str());
                if (ImGui::InputScalar("##EffectMiscValue2", ImGuiDataType_S32, &currentSpellEffect.effectMiscValues[1]))
                {
                    isSpellEffectsDirty = true;
                }
                ImGui::PopItemWidth();

                ImGui::NextColumn();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::Text(fieldNames[5].c_str());
                if (ImGui::InputScalar("##EffectMiscValue3", ImGuiDataType_S32, &currentSpellEffect.effectMiscValues[2]))
                {
                    isSpellEffectsDirty = true;
                }
                ImGui::PopItemWidth();
            }

            ImGui::Columns(1);
            ImGui::EndPopup();
        }

        if (effectEditorWasOpen && !effectEditorEnabled && currentIndex >= 0)
        {
            ECSUtil::Spell::SortSpellEffects(spellSingleton, spellEffectsStorage, currentIndex);
        }

        // Item selection combo with search filter.
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

            ImGui::Text("Filter (ID or Name)");
            ImGui::InputText("##Item Filter (ID or Name)", &filter);

            auto& currentSpell = spellStorage->Get<MetaGen::Shared::ClientDB::SpellRecord>(currentIndex);
            std::string currentlabel = std::to_string(currentIndex) + " - " + spellStorage->GetString(currentSpell.name);

            ImGui::Text("Select Item");
            if (ImGui::BeginCombo("##Select Item", currentIndex >= 0 ? currentlabel.c_str() : "None"))
            {
                char comboLabel[128];
                spellStorage->Each([&](u32 id, MetaGen::Shared::ClientDB::SpellRecord& spell) -> bool
                {
                    std::snprintf(comboLabel, sizeof(comboLabel), "%u - %s", id, spellStorage->GetString(spell.name).c_str());
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
            auto& spell = spellStorage->Get<MetaGen::Shared::ClientDB::SpellRecord>(currentIndex);

            // Group: General Information
            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                char nameBuffer[256];
                std::strcpy(nameBuffer, spellStorage->GetString(spell.name).c_str());

                ImGui::Columns(2, nullptr, false);
                {
                    // Name field
                    ImGui::Text("Name");
                    if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        spell.name = spellStorage->AddString(std::string(nameBuffer, strlen(nameBuffer)));
                        isSpellDirty = true;
                    }
                }

                ImGui::NextColumn();

                {
                    ImTextureID iconTexture = GetSpellIconTexture(spell.iconID);
                    ImGui::Image(iconTexture, ImVec2(64.0f, 64.0f));

                    if (ImGui::IsItemHovered())
                    {
                        auto& currentIcon = iconStorage->Get<MetaGen::Shared::ClientDB::IconRecord>(spell.iconID);

                        ImGui::BeginTooltip();
                        ImGui::Text("ID: %u", spell.iconID);
                        ImGui::Text("Path: %s", iconStorage->GetString(currentIcon.texture).c_str());
                        ImGui::EndTooltip();
                    }

                    if (ImGui::Button("Pick Icon", ImVec2(64.0f, 20.0f)))
                    {
                        OpenSpellIconPicker();
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    ImGui::Text("CastTime");
                    if (ImGui::InputScalar("##CastTime", ImGuiDataType_Float, &spell.castTime))
                    {
                        isSpellDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    ImGui::Text("Cooldown");
                    if (ImGui::InputScalar("##Cooldown", ImGuiDataType_Float, &spell.cooldown))
                    {
                        isSpellDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                {
                    ImGui::Text("Duration");
                    if (ImGui::InputScalar("##Duration", ImGuiDataType_Float, &spell.duration))
                    {
                        isSpellDirty = true;
                    }
                }

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                //{
                //    static std::vector<std::vector<Option>> CategoryTypeOptions =    
                //    {
                //        // Miscellaneous
                //        {
                //            { .id = 1, .label = "Miscellaneous" },
                //            { .id = 2, .label = "Reagent" },
                //            { .id = 3, .label = "Pet" },
                //            { .id = 4, .label = "Mount" },
                //            { .id = 5, .label = "Junk" }
                //        },
                //
                //        // Trade Goods
                //        {
                //            { .id = 1, .label = "Trade Goods" },
                //            { .id = 2, .label = "Meat" },
                //            { .id = 3, .label = "Cloth" },
                //            { .id = 4, .label = "Leather" },
                //            { .id = 5, .label = "Metal and Stone" },
                //            { .id = 6, .label = "Herb" },
                //            { .id = 7, .label = "Materials" }
                //        },
                //
                //        // Consumable
                //        {
                //            { .id = 1, .label = "Consumable" },
                //            { .id = 2, .label = "Food and Drink" },
                //            { .id = 3, .label = "Bandage" },
                //            { .id = 4, .label = "Flask" },
                //            { .id = 5, .label = "Elixir" },
                //            { .id = 6, .label = "Potion" },
                //            { .id = 7, .label = "Scroll" }
                //        },
                //
                //        // Reagent
                //        {
                //            { .id = 1, .label = "Reagent" }
                //        },
                //
                //        // Container
                //        {
                //            { .id = 1, .label = "Bag" }
                //        },
                //
                //        // Quest
                //        {
                //            { .id = 1, .label = "Quest" }
                //        },
                //
                //        // Armor
                //        {
                //            {.id = 1, .label = "Armor" },
                //            {.id = 2, .label = "Cloth" },
                //            {.id = 3, .label = "Leather" },
                //            {.id = 4, .label = "Mail" },
                //            {.id = 5, .label = "Plate" },
                //            {.id = 6, .label = "Shield" },
                //            {.id = 7, .label = "Libram" },
                //            {.id = 8, .label = "Idol" },
                //            {.id = 9, .label = "Totem" }
                //        },
                //
                //        // Weapon
                //        {
                //            {.id = 1,  .label = "Weapon" },
                //            {.id = 2,  .label = "Sword (One-Handed)" },
                //            {.id = 3,  .label = "Sword (Two-Handed)" },
                //            {.id = 4,  .label = "Mace (One-Handed)" },
                //            {.id = 5,  .label = "Mace (Two-Handed)" },
                //            {.id = 6,  .label = "Axe (One-Handed)" },
                //            {.id = 7,  .label = "Axe (Two-Handed)" },
                //            {.id = 8,  .label = "Dagger" },
                //            {.id = 9,  .label = "Fist Weapon" },
                //            {.id = 10, .label = "Polearm" },
                //            {.id = 11, .label = "Staff" },
                //            {.id = 12, .label = "Bow" },
                //            {.id = 13, .label = "Crossbow" },
                //            {.id = 14, .label = "Gun" },
                //            {.id = 15, .label = "Wand" },
                //            {.id = 16, .label = "Miscellaneous Tool" },
                //            {.id = 17, .label = "Fishing Pole" }
                //        }
                //    };
                //
                //    const std::vector<Option>& typeOptions = CategoryTypeOptions[item.category - 1];
                //    const std::string& currentLabel = CategoryTypeOptions[item.category - 1][item.categoryType - 1].label;
                //    if (RenderOptionDropdown<u8>("Category Type", currentLabel.c_str(), item.categoryType, typeOptions))
                //    {
                //        isSpellDirty = true;
                //    }
                //}

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                //{
                //    ImGui::Text("Virtual Level");
                //    if (ImGui::InputScalar("##Virtual Level", ImGuiDataType_U16, &item.virtualLevel))
                //    {
                //        isSpellDirty = true;
                //    }
                //}

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                //{
                //    ImGui::Text("Required Level");
                //    if (ImGui::InputScalar("##Required Level", ImGuiDataType_U16, &item.requiredLevel))
                //    {
                //        isSpellDirty = true;
                //    }
                //}

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                //{
                //    ImGui::Text("Armor");
                //    if (ImGui::InputScalar("##Armor", ImGuiDataType_U32, &item.armor))
                //    {
                //        isSpellDirty = true;
                //    }
                //}

                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                //{
                //    ImGui::Text("Durability");
                //    if (ImGui::InputScalar("##Durability", ImGuiDataType_U32, &item.durability))
                //    {
                //        isSpellDirty = true;
                //    }
                //}

                ImGui::Columns(1);

                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                // Description field with extra space
                char descBuffer[512];
                std::strcpy(descBuffer, spellStorage->GetString(spell.description).c_str());
                ImGui::Text("Description");
                if (ImGui::InputTextMultiline("##Description", descBuffer, sizeof(descBuffer),ImVec2(0.0f, 100.0f), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    spell.description = spellStorage->AddString(std::string(descBuffer, strlen(descBuffer)));
                    isSpellDirty = true;
                }

                ImGui::NextColumn();

                std::strcpy(descBuffer, spellStorage->GetString(spell.auraDescription).c_str());
                ImGui::Text("Aura Description");
                if (ImGui::InputTextMultiline("##AuraDescription", descBuffer, sizeof(descBuffer), ImVec2(0.0f, 100.0f), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    spell.auraDescription = spellStorage->AddString(std::string(descBuffer, strlen(descBuffer)));
                    isSpellDirty = true;
                }

                ImGui::PopItemWidth();
            }

            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_SpanAvailWidth))
            {
                ImGui::BeginColumns("Effects Field Columns", 2, ImGuiOldColumnFlags_NoBorder);
                const std::vector<u32>* spellEffectIDs = ECSUtil::Spell::GetSpellEffectList(spellSingleton, currentIndex);
                u32 numSpellEffects = spellEffectIDs ? static_cast<u32>(spellEffectIDs->size()) : 0;

                for (u32 i = 0; i < numSpellEffects; i++)
                {
                    u32 spellEffectID = spellEffectIDs->at(i);

                    const auto& spellEffect = spellEffectsStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(spellEffectID);

                    ImGui::PushID(spellEffectID);
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::Text("Effect ID: %u", spellEffectID);

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("ID: %u", spellEffectID);
                        ImGui::Text("Effect Priority: %u", spellEffect.effectPriority);
                        ImGui::Text("Effect Type: %u", spellEffect.effectType);
                        ImGui::Text("Value 1: %d", spellEffect.effectValues[0]);
                        ImGui::Text("Value 2: %d", spellEffect.effectValues[1]);
                        ImGui::Text("Value 3: %d", spellEffect.effectValues[2]);
                        ImGui::Text("Misc Value 1: %d", spellEffect.effectMiscValues[0]);
                        ImGui::Text("Misc Value 2: %d", spellEffect.effectMiscValues[1]);
                        ImGui::Text("Misc Value 3: %d", spellEffect.effectMiscValues[2]);
                        ImGui::EndTooltip();
                    }

                    if (ImGui::Button("Edit##Effect"))
                    {
                        ImGui::PopID();
                        currentEffectID = spellEffectID;
                        OpenSpellEffectsEditor();
                        ImGui::PushID(spellEffectID);
                    }

                    ImGui::SameLine();

                    if (ImGui::Button("Remove##Effect"))
                    {
                        ImGui::PopID();

                        ECSUtil::Spell::RemoveSpellEffect(spellSingleton, currentIndex, i);
                        spellEffectsStorage->Remove(spellEffectID);
                        isSpellEffectsDirty = true;

                        ImGui::PushID(spellEffectID);
                    }

                    ImGui::PopItemWidth();
                    ImGui::PopID();
                    ImGui::NextColumn();
                }
                ImGui::EndColumns();

                {
                    ImGui::Dummy(ImVec2(0.0f, 10.0f));
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

                    if (ImGui::Button("Add Effect"))
                    {
                        u32 newEffectID = 0;
                        if (spellEffectsStorage->Copy(0, newEffectID))
                        {
                            auto& newEffect = spellEffectsStorage->Get<MetaGen::Shared::ClientDB::SpellEffectsRecord>(newEffectID);
                            newEffect.spellID = currentIndex;
                            newEffect.effectPriority = 0; // Default to low priority, user can change it later.
                            newEffect.effectType = 1;
                            ECSUtil::Spell::AddSpellEffect(spellSingleton, currentIndex, newEffectID);

                            isSpellEffectsDirty = true;
                        }
                    }

                    ImGui::PopItemWidth();
                }
            }
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::Separator();

            ImGui::BeginColumns("Effects Field Columns", 2, ImGuiOldColumnFlags_NoBorder);
            {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                {
                    if (ImGui::Button("Add New Spell"))
                    {
                        u32 newSpellID = 0;
                        if (spellStorage->Copy(0, newSpellID))
                        {
                            currentIndex = newSpellID;
                            isSpellDirty = true;
                        }
                    }
                }
                ImGui::PopItemWidth();
                ImGui::NextColumn();

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                {
                    if (ImGui::Button("Remove Spell") && currentIndex > 0)
                    {
                        spellStorage->Remove(currentIndex);

                        currentIndex = 0;
                        isSpellDirty = true;
                    }
                }
                ImGui::PopItemWidth();
            }
            ImGui::EndColumns();
        }

        if (isSpellDirty)
        {
            spellStorage->MarkDirty();
        }

        if (isSpellEffectsDirty)
        {
            spellEffectsStorage->MarkDirty();
        }
    }

    void SpellEditor::DrawImGui()
    {
        if (ImGui::Begin(GetName(), &IsVisible()))
        {
            RenderSpellEditor();
        }
        ImGui::End();
    }
}