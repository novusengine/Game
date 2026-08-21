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
            MatchesRecord<MetaGen::Shared::ClientDB::CreatureTemplateGossipEditorRecord>(storages[static_cast<u8>(Artifact::CreatureTemplateGossip)]);
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
            u32 values32[8] = {};
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
                    (!changePayload.GetU32(change.values32[0]) || !changePayload.GetU16(change.values16[0]) || !changePayload.GetU32(change.values32[1]) ||
                        !changePayload.GetU16(change.values16[1]) || !changePayload.GetU32(change.values32[2]) || !changePayload.GetU32(change.values32[3]) ||
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

            ::ClientDB::Data* options = GetStorage(Artifact::GossipMenuOption);
            if (change.mutationType == MutationType::Delete)
            {
                options->Remove(change.artifactID);
                continue;
            }

            MetaGen::Shared::ClientDB::GossipMenuOptionEditorRecord record = {};
            record.menuID = change.values32[0];
            record.orderIndex = change.values16[0];
            record.textID = change.values32[1];
            record.icon = change.values16[1];
            record.flags = change.values32[2];
            record.visibilityConditionSetID = change.values32[3];
            record.enabledConditionSetID = change.values32[4];
            record.disabledReasonTextID = change.values32[5];
            record.actionType = change.values8[0];
            std::ranges::copy(change.parameters, record.actionParameters.begin());
            options->Replace(change.artifactID, record);
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
