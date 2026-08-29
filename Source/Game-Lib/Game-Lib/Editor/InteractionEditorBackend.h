#pragma once

#include "Game-Lib/Editor/InteractionEditorData.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace Editor
{
    struct CostCurrencyComponentInput
    {
    public:
        i32 priority = 0;
        u16 currencyID = 0;
        u64 amount = 0;
    };

    struct CostItemComponentInput
    {
    public:
        i32 priority = 0;
        u32 itemTemplateID = 0;
        u32 amount = 0;
    };

    struct VendorListItemInput
    {
    public:
        i32 priority = 0;
        u32 itemTemplateID = 0;
        u32 purchaseQuantity = 0;
        u32 costID = 0;
        u32 visibilityConditionSetID = 0;
        u32 enabledConditionSetID = 0;
        u32 disabledReasonTextID = 0;
        u32 flags = 0;
    };

    struct QuestObjectiveTargetInput
    {
    public:
        i32 priority = 0;
        u32 targetID = 0;
        u32 contribution = 0;
    };

    struct QuestRewardItemInput
    {
    public:
        i32 priority = 0;
        u32 itemTemplateID = 0;
        u32 quantity = 0;
        u32 flags = 0;
    };

    struct QuestRewardCurrencyInput
    {
    public:
        i32 priority = 0;
        u16 currencyID = 0;
        u64 amount = 0;
        u32 flags = 0;
    };

    struct QuestRewardReputationInput
    {
    public:
        i32 priority = 0;
        u16 factionID = 0;
        i32 amount = 0;
        u32 flags = 0;
    };

    struct QuestRewardExperienceInput
    {
    public:
        i32 priority = 0;
        u64 amount = 0;
        u32 flags = 0;
    };

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
        u32 CreateGossipMenuOption(u32 menuID, i32 priority, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters);
        u32 UpdateGossipMenuOption(u32 optionID, u32 menuID, i32 priority, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters);
        u32 DeleteGossipMenuOption(u32 optionID);
        u32 CreateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 defaultGreetingTextID, u32 flags, u32 rootMenuID, u32 gossipFlags);
        u32 UpdateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 defaultGreetingTextID, u32 flags);
        u32 DeleteCreatureTemplateInteraction(u32 creatureTemplateID);
        u32 CreateCreatureTemplateGossip(u32 creatureTemplateID, u32 rootMenuID, u32 flags);
        u32 UpdateCreatureTemplateGossip(u32 creatureTemplateID, u32 rootMenuID, u32 flags);
        u32 DeleteCreatureTemplateGossip(u32 creatureTemplateID);
        u32 CreateCost(std::string_view internalName, std::span<const CostCurrencyComponentInput> currencyComponents, std::span<const CostItemComponentInput> itemComponents);
        u32 UpdateCost(u32 costID, std::string_view internalName);
        u32 DeleteCost(u32 costID);
        u32 CreateCostCurrencyComponent(u32 costID, i32 priority, u16 currencyID, u64 amount);
        u32 UpdateCostCurrencyComponent(u32 componentID, u32 costID, i32 priority, u16 currencyID, u64 amount);
        u32 DeleteCostCurrencyComponent(u32 componentID);
        u32 CreateCostItemComponent(u32 costID, i32 priority, u32 itemTemplateID, u32 amount);
        u32 UpdateCostItemComponent(u32 componentID, u32 costID, i32 priority, u32 itemTemplateID, u32 amount);
        u32 DeleteCostItemComponent(u32 componentID);
        u32 CreateVendorList(std::string_view internalName, u32 flags, std::span<const VendorListItemInput> items);
        u32 UpdateVendorList(u32 vendorListID, std::string_view internalName, u32 flags);
        u32 DeleteVendorList(u32 vendorListID);
        u32 CreateVendorListItem(u32 vendorListID, i32 priority, u32 itemTemplateID, u32 purchaseQuantity, u32 costID, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u32 flags);
        u32 UpdateVendorListItem(u32 itemID, u32 vendorListID, i32 priority, u32 itemTemplateID, u32 purchaseQuantity, u32 costID, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u32 flags);
        u32 DeleteVendorListItem(u32 itemID);
        u32 CreateCreatureTemplateVendor(u32 creatureTemplateID, u32 vendorListID, u32 flags);
        u32 UpdateCreatureTemplateVendor(u32 creatureTemplateID, u32 vendorListID, u32 flags);
        u32 DeleteCreatureTemplateVendor(u32 creatureTemplateID);
        u32 CreateQuestTemplate(std::string_view internalName, u32 titleTextID, u32 summaryTextID, u32 detailsTextID, u32 logTextID, u32 progressTextID, u32 completionTextID, u32 visibilityConditionSetID, u32 availabilityConditionSetID, u32 unavailableReasonTextID, u16 level, u32 flags);
        u32 UpdateQuestTemplate(u32 questID, std::string_view internalName, u32 titleTextID, u32 summaryTextID, u32 detailsTextID, u32 logTextID, u32 progressTextID, u32 completionTextID, u32 visibilityConditionSetID, u32 availabilityConditionSetID, u32 unavailableReasonTextID, u16 level, u32 flags);
        u32 DeleteQuestTemplate(u32 questID);
        u32 CreateQuestPrerequisite(u32 questID, u32 prerequisiteQuestID);
        u32 UpdateQuestPrerequisite(u32 prerequisiteID, u32 questID, u32 prerequisiteQuestID);
        u32 DeleteQuestPrerequisite(u32 prerequisiteID);
        u32 CreateCreatureTemplateQuestGiver(u32 creatureTemplateID, u32 flags);
        u32 UpdateCreatureTemplateQuestGiver(u32 creatureTemplateID, u32 flags);
        u32 DeleteCreatureTemplateQuestGiver(u32 creatureTemplateID);
        u32 CreateCreatureTemplateQuest(u32 creatureTemplateID, u32 questID, u8 roleFlags, i32 priority, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 flags);
        u32 UpdateCreatureTemplateQuest(u32 associationID, u32 creatureTemplateID, u32 questID, u8 roleFlags, i32 priority, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 flags);
        u32 DeleteCreatureTemplateQuest(u32 associationID);
        u32 CreateQuestObjective(u32 questID, i32 priority, u8 objectiveType, u32 requiredCount, u32 textID, u32 flags, std::span<const QuestObjectiveTargetInput> targets);
        u32 UpdateQuestObjective(u32 objectiveID, u32 questID, i32 priority, u8 objectiveType, u32 requiredCount, u32 textID, u32 flags);
        u32 DeleteQuestObjective(u32 objectiveID);
        u32 CreateQuestObjectiveTarget(u32 objectiveID, i32 priority, u32 targetID, u32 contribution);
        u32 UpdateQuestObjectiveTarget(u32 objectiveTargetID, u32 objectiveID, i32 priority, u32 targetID, u32 contribution);
        u32 DeleteQuestObjectiveTarget(u32 objectiveTargetID);
        u32 CreateQuestRewardGroup(u32 questID, i32 priority, u8 selectionCount, u32 flags, std::span<const QuestRewardItemInput> items, std::span<const QuestRewardCurrencyInput> currencies, std::span<const QuestRewardReputationInput> reputations, std::span<const QuestRewardExperienceInput> experience);
        u32 UpdateQuestRewardGroup(u32 groupID, u32 questID, i32 priority, u8 selectionCount, u32 flags);
        u32 DeleteQuestRewardGroup(u32 groupID);
        u32 CreateQuestRewardItem(u32 groupID, i32 priority, u32 itemTemplateID, u32 quantity, u32 flags);
        u32 UpdateQuestRewardItem(u32 rewardID, u32 groupID, i32 priority, u32 itemTemplateID, u32 quantity, u32 flags);
        u32 DeleteQuestRewardItem(u32 rewardID);
        u32 CreateQuestRewardCurrency(u32 groupID, i32 priority, u16 currencyID, u64 amount, u32 flags);
        u32 UpdateQuestRewardCurrency(u32 rewardID, u32 groupID, i32 priority, u16 currencyID, u64 amount, u32 flags);
        u32 DeleteQuestRewardCurrency(u32 rewardID);
        u32 CreateQuestRewardReputation(u32 groupID, i32 priority, u16 factionID, i32 amount, u32 flags);
        u32 UpdateQuestRewardReputation(u32 rewardID, u32 groupID, i32 priority, u16 factionID, i32 amount, u32 flags);
        u32 DeleteQuestRewardReputation(u32 rewardID);
        u32 CreateQuestRewardExperience(u32 groupID, i32 priority, u64 amount, u32 flags);
        u32 UpdateQuestRewardExperience(u32 rewardID, u32 groupID, i32 priority, u64 amount, u32 flags);
        u32 DeleteQuestRewardExperience(u32 rewardID);
        std::optional<DatabaseEditorMutationResult> TakeMutationResult(u32 requestID);

    private:
        InteractionEditorData* GetData(bool create) const;
        u32 SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, const u8* bytes, u32 size);
        u32 SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, u32 recordID, u32 payloadCapacity, std::function<bool(Bytebuffer&)>&& writeFields);
    };
}
