#include "InteractionEditorBackend.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Memory/Bytebuffer.h>

#include <Network/Client.h>

#include <entt/entt.hpp>

namespace Editor
{
    namespace
    {
        constexpr u32 GOSSIP_OPTION_REORDER_SENTINEL = 0;

        ECS::Singletons::NetworkState* GetNetworkState()
        {
            EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
            if (!registries || !registries->gameRegistry)
                return nullptr;

            auto& context = registries->gameRegistry->ctx();
            return context.contains<ECS::Singletons::NetworkState>() ? &context.get<ECS::Singletons::NetworkState>() : nullptr;
        }

        bool WriteConditionFields(Bytebuffer& payload, u32 conditionGroupID, u16 orderIndex, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters)
        {
            return payload.PutU32(conditionGroupID) && payload.PutU16(orderIndex) && payload.PutU16(conditionType) && payload.PutU8(comparison) &&
                payload.PutI64(parameters[0]) && payload.PutI64(parameters[1]) && payload.PutI64(parameters[2]) && payload.PutI64(parameters[3]);
        }

        bool WriteGossipMenuOptionFields(Bytebuffer& payload, u32 menuID, u16 orderIndex, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters)
        {
            return payload.PutU32(menuID) && payload.PutU16(orderIndex) && payload.PutU32(textID) && payload.PutU16(icon) && payload.PutU32(flags) &&
                payload.PutU32(visibilityConditionSetID) && payload.PutU32(enabledConditionSetID) && payload.PutU32(disabledReasonTextID) && payload.PutU8(actionType) &&
                payload.PutI64(actionParameters[0]) && payload.PutI64(actionParameters[1]) && payload.PutI64(actionParameters[2]) && payload.PutI64(actionParameters[3]);
        }
    }

    InteractionEditorData* InteractionEditorBackend::GetData(bool create) const
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        if (!registries || !registries->dbRegistry)
            return nullptr;

        auto& context = registries->dbRegistry->ctx();
        if (context.contains<InteractionEditorData>())
            return &context.get<InteractionEditorData>();

        return create ? &context.emplace<InteractionEditorData>() : nullptr;
    }

    bool InteractionEditorBackend::RequestSnapshot()
    {
        InteractionEditorData* data = GetData(true);
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!data || !networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld)
            return false;
        if (data->state == DatabaseEditorDataState::Loading)
            return true;

        const u32 requestID = data->StartRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<64>();
        if (!ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorSnapshotRequest(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Interaction, requestID))
        {
            data->FailSnapshot(requestID);
            return false;
        }

        networkState->client->Send(buffer);
        return true;
    }

    u32 InteractionEditorBackend::SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, const u8* bytes, u32 size)
    {
        InteractionEditorData* data = GetData(false);
        ECS::Singletons::NetworkState* networkState = GetNetworkState();
        if (!data || data->state != DatabaseEditorDataState::Ready || !networkState || !networkState->client || !networkState->client->IsConnected() || !networkState->isInWorld || !bytes || size == 0)
            return 0;

        const u32 requestID = data->StartMutationRequest();
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(size + 128);
        if (!buffer || !ECS::Util::MessageBuilder::Cheat::BuildDatabaseEditorMutation(buffer, MetaGen::Shared::DatabaseEditor::DatabaseEditorTypeEnum::Interaction, static_cast<u8>(artifact), mutationType, requestID, bytes, size))
        {
            return 0;
        }

        networkState->client->Send(buffer);
        return requestID;
    }

    u32 InteractionEditorBackend::CreateLocalizedText(std::string_view internalName, std::string_view englishValue, std::string_view translatorContext)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(internalName.size() + englishValue.size() + translatorContext.size() + 32));
        if (!payload || !payload->PutString(internalName) || !payload->PutString(englishValue) || !payload->PutString(translatorContext))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::LocalizedText, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateLocalizedText(u32 textID, std::string_view internalName, std::string_view englishValue, std::string_view translatorContext)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(internalName.size() + englishValue.size() + translatorContext.size() + 40));
        if (!payload || !payload->PutU32(textID) || !payload->PutString(internalName) || !payload->PutString(englishValue) || !payload->PutString(translatorContext))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::LocalizedText, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteLocalizedText(u32 textID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(textID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::LocalizedText, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateTranslation(u32 textID, u8 locale, std::string_view value)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(value.size() + 24));
        if (!payload || !payload->PutU32(textID) || !payload->PutU8(locale) || !payload->PutString(value))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::LocalizedTextTranslation, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateTranslation(u32 textID, u8 locale, std::string_view value)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(value.size() + 24));
        if (!payload || !payload->PutU32(textID) || !payload->PutU8(locale) || !payload->PutString(value))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::LocalizedTextTranslation, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteTranslation(u32 textID, u8 locale)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(textID) || !payload->PutU8(locale))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::LocalizedTextTranslation, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateConditionSet(std::string_view internalName, u8 groupOperator, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(internalName.size() + 64));
        if (!payload || !payload->PutString(internalName) || !payload->PutU8(groupOperator) || !payload->PutU16(conditionType) || !payload->PutU8(comparison) || !payload->PutI64(parameters[0]) || !payload->PutI64(parameters[1]) || !payload->PutI64(parameters[2]) || !payload->PutI64(parameters[3]))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::ConditionSet, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateConditionSet(u32 conditionSetID, std::string_view internalName)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(internalName.size() + 24));
        if (!payload || !payload->PutU32(conditionSetID) || !payload->PutString(internalName))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::ConditionSet, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteConditionSet(u32 conditionSetID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(conditionSetID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::ConditionSet, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateConditionGroup(u32 conditionSetID, u32 parentGroupID, u8 groupOperator, bool negated, u16 orderIndex, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<96>();
        if (!payload->PutU32(conditionSetID) || !payload->PutU32(parentGroupID) || !payload->PutU8(groupOperator) || !payload->PutU8(negated ? 1 : 0) || !payload->PutU16(orderIndex) || !WriteConditionFields(*payload, 0, 0, conditionType, comparison, parameters))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::ConditionGroup, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateConditionGroup(u32 conditionGroupID, u8 groupOperator, bool negated, u16 orderIndex)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<16>();
        if (!payload->PutU32(conditionGroupID) || !payload->PutU8(groupOperator) || !payload->PutU8(negated ? 1 : 0) || !payload->PutU16(orderIndex))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::ConditionGroup, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteConditionGroup(u32 conditionGroupID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(conditionGroupID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::ConditionGroup, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateCondition(u32 conditionGroupID, u16 orderIndex, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<64>();
        if (!WriteConditionFields(*payload, conditionGroupID, orderIndex, conditionType, comparison, parameters))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::Condition, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateCondition(u32 conditionID, u32 conditionGroupID, u16 orderIndex, u16 conditionType, u8 comparison, const std::array<i64, 4>& parameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<72>();
        if (!payload->PutU32(conditionID) || !WriteConditionFields(*payload, conditionGroupID, orderIndex, conditionType, comparison, parameters))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::Condition, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteCondition(u32 conditionID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(conditionID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::Condition, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateGossipMenu(std::string_view internalName, u32 greetingTextID, u32 flags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(internalName.size() + 24));
        if (!payload || !payload->PutString(internalName) || !payload->PutU32(greetingTextID) || !payload->PutU32(flags))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenu, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateGossipMenu(u32 gossipMenuID, std::string_view internalName, u32 greetingTextID, u32 flags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(static_cast<u32>(internalName.size() + 32));
        if (!payload || !payload->PutU32(gossipMenuID) || !payload->PutString(internalName) || !payload->PutU32(greetingTextID) || !payload->PutU32(flags))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenu, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteGossipMenu(u32 gossipMenuID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(gossipMenuID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenu, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateGossipMenuOption(u32 menuID, u16 orderIndex, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<96>();
        if (!WriteGossipMenuOptionFields(*payload, menuID, orderIndex, textID, icon, flags, visibilityConditionSetID, enabledConditionSetID, disabledReasonTextID, actionType, actionParameters))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenuOption, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateGossipMenuOption(u32 optionID, u32 menuID, u16 orderIndex, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<104>();
        if (!payload->PutU32(optionID) || !WriteGossipMenuOptionFields(*payload, menuID, orderIndex, textID, icon, flags, visibilityConditionSetID, enabledConditionSetID, disabledReasonTextID, actionType, actionParameters))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenuOption, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::ReorderGossipMenuOption(u32 optionID, u32 menuID, u16 orderIndex, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<108>();
        if (!payload->PutU32(GOSSIP_OPTION_REORDER_SENTINEL) || !payload->PutU32(optionID) || !WriteGossipMenuOptionFields(*payload, menuID, orderIndex, textID, icon, flags, visibilityConditionSetID, enabledConditionSetID, disabledReasonTextID, actionType, actionParameters))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenuOption, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteGossipMenuOption(u32 optionID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(optionID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenuOption, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 flags, u32 rootMenuID, u32 gossipFlags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<32>();
        if (!payload->PutU32(creatureTemplateID) || !payload->PutU8(rangePolicy) || !payload->PutF32(interactionRange) || !payload->PutU32(flags) || !payload->PutU32(rootMenuID) || !payload->PutU32(gossipFlags))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateInteraction, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 flags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<24>();
        if (!payload->PutU32(creatureTemplateID) || !payload->PutU8(rangePolicy) || !payload->PutF32(interactionRange) || !payload->PutU32(flags))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateInteraction, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteCreatureTemplateInteraction(u32 creatureTemplateID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(creatureTemplateID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateInteraction, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::CreateCreatureTemplateGossip(u32 creatureTemplateID, u32 rootMenuID, u32 flags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<16>();
        if (!payload->PutU32(creatureTemplateID) || !payload->PutU32(rootMenuID) || !payload->PutU32(flags))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateGossip, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateCreatureTemplateGossip(u32 creatureTemplateID, u32 rootMenuID, u32 flags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<16>();
        if (!payload->PutU32(creatureTemplateID) || !payload->PutU32(rootMenuID) || !payload->PutU32(flags))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateGossip, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::DeleteCreatureTemplateGossip(u32 creatureTemplateID)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<8>();
        if (!payload->PutU32(creatureTemplateID))
            return 0;

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateGossip, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    std::optional<DatabaseEditorMutationResult> InteractionEditorBackend::TakeMutationResult(u32 requestID)
    {
        InteractionEditorData* data = GetData(false);
        return data ? data->TakeMutationResult(requestID) : std::nullopt;
    }
}
