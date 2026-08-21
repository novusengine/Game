#pragma once

#include "Game-Lib/Editor/InteractionEditorData.h"

#include <array>
#include <optional>
#include <string_view>

namespace Editor
{
    class InteractionEditorBackend
    {
    public:
        bool RequestSnapshot();
        u32 CreateLocalizedText(std::string_view internalName, std::string_view englishValue, std::string_view translatorContext);
        u32 UpdateLocalizedText(u32 textID, std::string_view internalName, std::string_view englishValue, std::string_view translatorContext);
        u32 DeleteLocalizedText(u32 textID);
        u32 CreateTranslation(u32 textID, u8 locale, std::string_view value);
        u32 UpdateTranslation(u32 textID, u8 locale, std::string_view value);
        u32 DeleteTranslation(u32 textID, u8 locale);
        u32 CreateConditionSet(std::string_view internalName, u8 groupOperator, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters);
        u32 UpdateConditionSet(u32 conditionSetID, std::string_view internalName);
        u32 DeleteConditionSet(u32 conditionSetID);
        u32 CreateConditionGroup(u32 conditionSetID, u32 parentGroupID, u8 groupOperator, bool negated, u16 orderIndex, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters);
        u32 UpdateConditionGroup(u32 conditionGroupID, u8 groupOperator, bool negated, u16 orderIndex);
        u32 DeleteConditionGroup(u32 conditionGroupID);
        u32 CreateCondition(u32 conditionGroupID, u16 orderIndex, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters);
        u32 UpdateCondition(u32 conditionID, u32 conditionGroupID, u16 orderIndex, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters);
        u32 DeleteCondition(u32 conditionID);
        u32 CreateGossipMenu(std::string_view internalName, u32 greetingTextID, u32 flags);
        u32 UpdateGossipMenu(u32 gossipMenuID, std::string_view internalName, u32 greetingTextID, u32 flags);
        u32 DeleteGossipMenu(u32 gossipMenuID);
        u32 CreateGossipMenuOption(u32 menuID, u16 orderIndex, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters);
        u32 UpdateGossipMenuOption(u32 optionID, u32 menuID, u16 orderIndex, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters);
        u32 ReorderGossipMenuOption(u32 optionID, u32 menuID, u16 orderIndex, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters);
        u32 DeleteGossipMenuOption(u32 optionID);
        u32 CreateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 flags, u32 rootMenuID, u32 gossipFlags);
        u32 UpdateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 flags);
        u32 DeleteCreatureTemplateInteraction(u32 creatureTemplateID);
        u32 CreateCreatureTemplateGossip(u32 creatureTemplateID, u32 rootMenuID, u32 flags);
        u32 UpdateCreatureTemplateGossip(u32 creatureTemplateID, u32 rootMenuID, u32 flags);
        u32 DeleteCreatureTemplateGossip(u32 creatureTemplateID);
        std::optional<DatabaseEditorMutationResult> TakeMutationResult(u32 requestID);

    private:
        InteractionEditorData* GetData(bool create) const;
        u32 SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, const u8* bytes, u32 size);
    };
}
