#include "InteractionEditorData.h"

#include <Base/Memory/Bytebuffer.h>

#include <MetaGen/Shared/ClientDB/ClientDB.h>
#include <MetaGen/Shared/Localization/Localization.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Editor
{
    namespace
    {
        template <typename Record>
        bool MatchesRecord(::ClientDB::Data& storage)
        {
            const auto& fields = storage.GetFields();
            const auto& expectedFields = Record::FIELD_LIST;
            if (fields.size() != expectedFields.size())
                return false;

            for (size_t index = 0; index < fields.size(); ++index)
            {
                if (fields[index].name != expectedFields[index].name || fields[index].type != expectedFields[index].type || fields[index].count != expectedFields[index].count)
                    return false;
            }

            return true;
        }

        template <typename Record, typename Populate>
        void ApplyRecordChange(::ClientDB::Data& storage, u32 recordID, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, Populate&& populate)
        {
            if (mutationType == MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete)
            {
                storage.Remove(recordID);
                return;
            }

            Record record{};
            populate(record);
            storage.Replace(recordID, record);
        }
    }

    InteractionEditorData::InteractionEditorData()
        : DatabaseEditorData(static_cast<u8>(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::Count))
    {
    }

    bool InteractionEditorData::ValidateSnapshot(std::vector<::ClientDB::Data>& storages) const
    {
        using Artifact = MetaGen::Shared::Interaction::InteractionEditorArtifactEnum;
        if (storages.size() != static_cast<size_t>(Artifact::Count))
            return false;

        return MatchesRecord<MetaGen::Shared::ClientDB::LocalizedTextEditorRecord>(storages[static_cast<u8>(Artifact::LocalizedText)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord>(storages[static_cast<u8>(Artifact::LocalizedTextTranslation)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::ConditionDescriptorEditorRecord>(storages[static_cast<u8>(Artifact::ConditionDescriptor)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::ConditionSetEditorRecord>(storages[static_cast<u8>(Artifact::ConditionSet)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::ConditionGroupEditorRecord>(storages[static_cast<u8>(Artifact::ConditionGroup)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::ConditionEditorRecord>(storages[static_cast<u8>(Artifact::Condition)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::GossipActionDescriptorEditorRecord>(storages[static_cast<u8>(Artifact::GossipActionDescriptor)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::GossipMenuEditorRecord>(storages[static_cast<u8>(Artifact::GossipMenu)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord>(storages[static_cast<u8>(Artifact::GossipMenuOption)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CreatureTemplateDescriptorEditorRecord>(storages[static_cast<u8>(Artifact::CreatureTemplateDescriptor)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CreatureTemplateInteractionEditorRecord>(storages[static_cast<u8>(Artifact::CreatureTemplateInteraction)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CreatureTemplateGossipEditorRecord>(storages[static_cast<u8>(Artifact::CreatureTemplateGossip)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CostEditorRecord>(storages[static_cast<u8>(Artifact::Cost)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CostCurrencyComponentEditorRecord>(storages[static_cast<u8>(Artifact::CostCurrencyComponent)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CostItemComponentEditorRecord>(storages[static_cast<u8>(Artifact::CostItemComponent)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::VendorListEditorRecord>(storages[static_cast<u8>(Artifact::VendorList)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::VendorListItemEditorRecord>(storages[static_cast<u8>(Artifact::VendorListItem)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CreatureTemplateVendorEditorRecord>(storages[static_cast<u8>(Artifact::CreatureTemplateVendor)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestTemplateEditorRecord>(storages[static_cast<u8>(Artifact::QuestTemplate)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestPrerequisiteEditorRecord>(storages[static_cast<u8>(Artifact::QuestPrerequisite)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CreatureTemplateQuestGiverEditorRecord>(storages[static_cast<u8>(Artifact::CreatureTemplateQuestGiver)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::CreatureTemplateQuestEditorRecord>(storages[static_cast<u8>(Artifact::CreatureTemplateQuest)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestObjectiveEditorRecord>(storages[static_cast<u8>(Artifact::QuestObjective)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestObjectiveTargetEditorRecord>(storages[static_cast<u8>(Artifact::QuestObjectiveTarget)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestRewardGroupEditorRecord>(storages[static_cast<u8>(Artifact::QuestRewardGroup)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestRewardItemEditorRecord>(storages[static_cast<u8>(Artifact::QuestRewardItem)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestRewardCurrencyEditorRecord>(storages[static_cast<u8>(Artifact::QuestRewardCurrency)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestRewardReputationEditorRecord>(storages[static_cast<u8>(Artifact::QuestRewardReputation)]) &&
            MatchesRecord<MetaGen::Shared::ClientDB::QuestRewardExperienceEditorRecord>(storages[static_cast<u8>(Artifact::QuestRewardExperience)]);
    }

    bool InteractionEditorData::ApplyChangeSet(u16 changeCount, Bytebuffer& payload)
    {
        using Artifact = MetaGen::Shared::Interaction::InteractionEditorArtifactEnum;
        using MutationType = MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum;

        struct StagedChange
        {
        public:
            Artifact artifact = Artifact::LocalizedText;
            MutationType mutationType = MutationType::Update;
            u32 artifactID = 0;
            std::string primary;
            std::string secondary;
            std::string tertiary;
            u64 values64[1] = {};
            u32 values32[12] = {};
            i32 signedValues32[2] = {};
            u16 values16[2] = {};
            u8 values8[2] = {};
            i64 parameters[4] = {};
        };

        std::vector<StagedChange> changes;
        changes.reserve(changeCount);
        for (u16 changeIndex = 0; changeIndex < changeCount; ++changeIndex)
        {
            u8 artifactValue = 0;
            u8 mutationValue = 0;
            u32 payloadLength = 0;
            StagedChange change;
            if (!payload.GetU8(artifactValue) || !payload.GetU8(mutationValue) || !payload.GetU32(change.artifactID) || !payload.GetU32(payloadLength) || artifactValue >= static_cast<u8>(Artifact::Count) || mutationValue >= static_cast<u8>(MutationType::Count) || change.artifactID == 0 || payloadLength > payload.GetActiveSize())
            {
                return false;
            }

            change.artifact = static_cast<Artifact>(artifactValue);
            change.mutationType = static_cast<MutationType>(mutationValue);
            Bytebuffer changePayload = Bytebuffer::CreateReadOnlyView(payload.GetReadPointer(), payloadLength);
            if (!payload.SkipRead(payloadLength))
                return false;

            if (change.artifact == Artifact::LocalizedText)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetString(change.primary) || !changePayload.GetString(change.secondary) || !changePayload.GetString(change.tertiary)))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::LocalizedTextTranslation)
            {
                if (!changePayload.GetU32(change.values32[0]) || !changePayload.GetU8(change.values8[0]) || change.values32[0] != change.artifactID || change.values8[0] >= static_cast<u8>(MetaGen::Shared::Localization::LocaleEnum::Count) || (change.mutationType != MutationType::Delete && !changePayload.GetString(change.primary)))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::GossipMenu)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetString(change.primary) || !changePayload.GetU32(change.values32[0]) || !changePayload.GetU32(change.values32[1])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::GossipMenuOption)
            {
                if (change.mutationType != MutationType::Delete &&
                    (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) || !changePayload.GetU32(change.values32[1]) ||
                        !changePayload.GetU16(change.values16[0]) || !changePayload.GetU32(change.values32[2]) || !changePayload.GetU32(change.values32[3]) ||
                        !changePayload.GetU32(change.values32[4]) || !changePayload.GetU32(change.values32[5]) || !changePayload.GetU8(change.values8[0])))
                {
                    return false;
                }
                for (i64& parameter : change.parameters)
                {
                    if (change.mutationType != MutationType::Delete && !changePayload.GetI64(parameter))
                        return false;
                }
                if (change.mutationType != MutationType::Delete && change.values32[0] == 0)
                    return false;
            }
            else if (change.artifact == Artifact::Cost || change.artifact == Artifact::VendorList)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetString(change.primary) ||
                    (change.artifact == Artifact::VendorList && !changePayload.GetU32(change.values32[0]))))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::CostCurrencyComponent)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU16(change.values16[0]) || !changePayload.GetU64(change.values64[0])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::CostItemComponent)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU32(change.values32[1]) || !changePayload.GetU32(change.values32[2])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::VendorListItem)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU32(change.values32[1]) || !changePayload.GetU32(change.values32[2]) || !changePayload.GetU32(change.values32[3]) ||
                    !changePayload.GetU32(change.values32[4]) || !changePayload.GetU32(change.values32[5]) || !changePayload.GetU32(change.values32[6]) ||
                    !changePayload.GetU32(change.values32[7])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::CreatureTemplateVendor)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetU32(change.values32[1])))
                    return false;
            }
            else if (change.artifact == Artifact::QuestTemplate)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetString(change.primary) || !changePayload.GetU32(change.values32[0]) ||
                    !changePayload.GetU32(change.values32[1]) || !changePayload.GetU32(change.values32[2]) || !changePayload.GetU32(change.values32[3]) ||
                    !changePayload.GetU32(change.values32[4]) || !changePayload.GetU32(change.values32[5]) || !changePayload.GetU32(change.values32[6]) ||
                    !changePayload.GetU32(change.values32[7]) || !changePayload.GetU32(change.values32[8]) || !changePayload.GetU16(change.values16[0]) ||
                    !changePayload.GetU32(change.values32[9])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestPrerequisite)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetU32(change.values32[1])))
                    return false;
            }
            else if (change.artifact == Artifact::CreatureTemplateQuestGiver)
            {
                if (change.mutationType != MutationType::Delete && !changePayload.GetU32(change.values32[0]))
                    return false;
            }
            else if (change.artifact == Artifact::CreatureTemplateQuest)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetU32(change.values32[1]) ||
                    !changePayload.GetU8(change.values8[0]) || !changePayload.GetI32(change.signedValues32[0]) || !changePayload.GetU32(change.values32[2]) ||
                    !changePayload.GetU32(change.values32[3]) || !changePayload.GetU32(change.values32[4])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestObjective)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU8(change.values8[0]) || !changePayload.GetU32(change.values32[1]) || !changePayload.GetU32(change.values32[2]) ||
                    !changePayload.GetU32(change.values32[3])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestObjectiveTarget)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU32(change.values32[1]) || !changePayload.GetU32(change.values32[2])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestRewardGroup)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU8(change.values8[0]) || !changePayload.GetU32(change.values32[1])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestRewardItem)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU32(change.values32[1]) || !changePayload.GetU32(change.values32[2]) || !changePayload.GetU32(change.values32[3])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestRewardCurrency)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU16(change.values16[0]) || !changePayload.GetU64(change.values64[0]) || !changePayload.GetU32(change.values32[1])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestRewardReputation)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU16(change.values16[0]) || !changePayload.GetI32(change.signedValues32[1]) || !changePayload.GetU32(change.values32[1])))
                {
                    return false;
                }
            }
            else if (change.artifact == Artifact::QuestRewardExperience)
            {
                if (change.mutationType != MutationType::Delete && (!changePayload.GetU32(change.values32[0]) || !changePayload.GetI32(change.signedValues32[0]) ||
                    !changePayload.GetU64(change.values64[0]) || !changePayload.GetU32(change.values32[1])))
                {
                    return false;
                }
            }
            else
            {
                return false;
            }

            if (changePayload.GetActiveSize() != 0)
                return false;

            changes.push_back(std::move(change));
        }

        if (payload.GetActiveSize() != 0)
            return false;

        ::ClientDB::Data* translations = GetStorage(Artifact::LocalizedTextTranslation);
        std::vector<std::pair<u32, u8>> translationKeys;
        translations->Each([&](u32, const MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord& record)
        {
            translationKeys.emplace_back(record.textID, record.locale);
            return true;
        });

        u64 nextTranslationRowID = translations->GetHeader().maxID;
        for (const StagedChange& change : changes)
        {
            if (change.artifact != Artifact::LocalizedTextTranslation)
                continue;

            const std::pair<u32, u8> key = { change.values32[0], change.values8[0] };
            if (change.mutationType == MutationType::Delete)
            {
                std::erase(translationKeys, key);
                continue;
            }

            if (std::ranges::find(translationKeys, key) != translationKeys.end())
                continue;

            if (nextTranslationRowID == std::numeric_limits<u32>::max())
                return false;
            ++nextTranslationRowID;
            translationKeys.push_back(key);
        }

        for (const StagedChange& change : changes)
        {
            if (change.artifact == Artifact::LocalizedText)
            {
                ::ClientDB::Data* texts = GetStorage(Artifact::LocalizedText);
                if (change.mutationType == MutationType::Delete)
                {
                    texts->Remove(change.artifactID);
                    continue;
                }

                MetaGen::Shared::ClientDB::LocalizedTextEditorRecord record = {};
                record.internalName = texts->AddString(change.primary);
                record.englishValue = texts->AddString(change.secondary);
                record.translatorContext = texts->AddString(change.tertiary);
                texts->Replace(change.artifactID, record);
                continue;
            }

            if (change.artifact == Artifact::LocalizedTextTranslation)
            {
                ::ClientDB::Data* translations = GetStorage(Artifact::LocalizedTextTranslation);
                u32 rowID = 0;
                translations->Each([&](u32 candidateID, const MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord& record)
                {
                    if (record.textID == change.values32[0] && record.locale == change.values8[0])
                    {
                        rowID = candidateID;
                        return false;
                    }

                    return true;
                });
                if (change.mutationType == MutationType::Delete)
                {
                    if (rowID != 0)
                        translations->Remove(rowID);
                    continue;
                }

                if (rowID == 0)
                    rowID = translations->GetHeader().maxID + 1;

                MetaGen::Shared::ClientDB::LocalizedTextTranslationEditorRecord record = {};
                record.textID = change.values32[0];
                record.locale = change.values8[0];
                record.value = translations->AddString(change.primary);
                translations->Replace(rowID, record);
                continue;
            }

            if (change.artifact == Artifact::GossipMenu)
            {
                ::ClientDB::Data* menus = GetStorage(Artifact::GossipMenu);
                if (change.mutationType == MutationType::Delete)
                {
                    menus->Remove(change.artifactID);
                    continue;
                }

                MetaGen::Shared::ClientDB::GossipMenuEditorRecord record = {};
                record.internalName = menus->AddString(change.primary);
                record.greetingTextID = change.values32[0];
                record.flags = change.values32[1];
                menus->Replace(change.artifactID, record);
                continue;
            }

            if (change.artifact == Artifact::GossipMenuOption)
            {
                ::ClientDB::Data* options = GetStorage(Artifact::GossipMenuOption);
                ApplyRecordChange<MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord>(*options, change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.menuID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.textID = change.values32[1];
                    record.icon = change.values16[0];
                    record.flags = change.values32[2];
                    record.visibilityConditionSetID = change.values32[3];
                    record.enabledConditionSetID = change.values32[4];
                    record.disabledReasonTextID = change.values32[5];
                    record.actionType = change.values8[0];
                    std::ranges::copy(change.parameters, record.actionParameters.begin());
                });
                continue;
            }

            if (change.artifact == Artifact::Cost)
            {
                ::ClientDB::Data* storage = GetStorage(change.artifact);
                ApplyRecordChange<MetaGen::Shared::ClientDB::CostEditorRecord>(*storage, change.artifactID, change.mutationType,
                    [&](auto& record) { record.internalName = storage->AddString(change.primary); });
                continue;
            }

            if (change.artifact == Artifact::CostCurrencyComponent)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::CostCurrencyComponentEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.costID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.currencyID = change.values16[0];
                    record.amount = change.values64[0];
                });
                continue;
            }

            if (change.artifact == Artifact::CostItemComponent)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::CostItemComponentEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.costID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.itemTemplateID = change.values32[1];
                    record.amount = change.values32[2];
                });
                continue;
            }

            if (change.artifact == Artifact::VendorList)
            {
                ::ClientDB::Data* storage = GetStorage(change.artifact);
                ApplyRecordChange<MetaGen::Shared::ClientDB::VendorListEditorRecord>(*storage, change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.internalName = storage->AddString(change.primary);
                    record.flags = change.values32[0];
                });
                continue;
            }

            if (change.artifact == Artifact::VendorListItem)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::VendorListItemEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.vendorListID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.itemTemplateID = change.values32[1];
                    record.purchaseQuantity = change.values32[2];
                    record.costID = change.values32[3];
                    record.visibilityConditionSetID = change.values32[4];
                    record.enabledConditionSetID = change.values32[5];
                    record.disabledReasonTextID = change.values32[6];
                    record.flags = change.values32[7];
                });
                continue;
            }

            if (change.artifact == Artifact::CreatureTemplateVendor)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::CreatureTemplateVendorEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.vendorListID = change.values32[0];
                    record.flags = change.values32[1];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestTemplate)
            {
                ::ClientDB::Data* storage = GetStorage(change.artifact);
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestTemplateEditorRecord>(*storage, change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.internalName = storage->AddString(change.primary);
                    record.titleTextID = change.values32[0];
                    record.summaryTextID = change.values32[1];
                    record.detailsTextID = change.values32[2];
                    record.logTextID = change.values32[3];
                    record.progressTextID = change.values32[4];
                    record.completionTextID = change.values32[5];
                    record.visibilityConditionSetID = change.values32[6];
                    record.availabilityConditionSetID = change.values32[7];
                    record.unavailableReasonTextID = change.values32[8];
                    record.level = change.values16[0];
                    record.flags = change.values32[9];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestPrerequisite)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestPrerequisiteEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.questID = change.values32[0];
                    record.prerequisiteQuestID = change.values32[1];
                });
                continue;
            }

            if (change.artifact == Artifact::CreatureTemplateQuestGiver)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::CreatureTemplateQuestGiverEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType,
                    [&](auto& record) { record.flags = change.values32[0]; });
                continue;
            }

            if (change.artifact == Artifact::CreatureTemplateQuest)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::CreatureTemplateQuestEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.creatureTemplateID = change.values32[0];
                    record.questID = change.values32[1];
                    record.roleFlags = change.values8[0];
                    record.priority = change.signedValues32[0];
                    record.visibilityConditionSetID = change.values32[2];
                    record.enabledConditionSetID = change.values32[3];
                    record.flags = change.values32[4];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestObjective)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestObjectiveEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.questID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.objectiveType = change.values8[0];
                    record.requiredCount = change.values32[1];
                    record.textID = change.values32[2];
                    record.flags = change.values32[3];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestObjectiveTarget)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestObjectiveTargetEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.objectiveID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.targetID = change.values32[1];
                    record.contribution = change.values32[2];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestRewardGroup)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestRewardGroupEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.questID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.selectionCount = change.values8[0];
                    record.flags = change.values32[1];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestRewardItem)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestRewardItemEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.rewardGroupID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.itemTemplateID = change.values32[1];
                    record.quantity = change.values32[2];
                    record.flags = change.values32[3];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestRewardCurrency)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestRewardCurrencyEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.rewardGroupID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.currencyID = change.values16[0];
                    record.amount = change.values64[0];
                    record.flags = change.values32[1];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestRewardReputation)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestRewardReputationEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.rewardGroupID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.factionID = change.values16[0];
                    record.amount = change.signedValues32[1];
                    record.flags = change.values32[1];
                });
                continue;
            }

            if (change.artifact == Artifact::QuestRewardExperience)
            {
                ApplyRecordChange<MetaGen::Shared::ClientDB::QuestRewardExperienceEditorRecord>(*GetStorage(change.artifact), change.artifactID, change.mutationType, [&](auto& record)
                {
                    record.rewardGroupID = change.values32[0];
                    record.priority = change.signedValues32[0];
                    record.amount = change.values64[0];
                    record.flags = change.values32[1];
                });
                continue;
            }

            return false;
        }

        return true;
    }

    ::ClientDB::Data* InteractionEditorData::GetStorage(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact)
    {
        return DatabaseEditorData::GetStorage(static_cast<u8>(artifact));
    }

    const ::ClientDB::Data* InteractionEditorData::GetStorage(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact) const
    {
        return DatabaseEditorData::GetStorage(static_cast<u8>(artifact));
    }
}
