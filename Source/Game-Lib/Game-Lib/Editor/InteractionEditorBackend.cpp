#include "InteractionEditorBackend.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Memory/Bytebuffer.h>

#include <Network/Client.h>

#include <entt/entt.hpp>

#include <limits>

namespace Editor
{
    namespace
    {

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

        bool WriteGossipMenuOptionFields(Bytebuffer& payload, u32 menuID, i32 priority, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters)
        {
            return payload.PutU32(menuID) && payload.PutI32(priority) && payload.PutU32(textID) && payload.PutU16(icon) && payload.PutU32(flags) &&
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

    u32 InteractionEditorBackend::SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum artifact, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum mutationType, u32 recordID, u32 payloadCapacity, std::function<bool(Bytebuffer&)>&& writeFields)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::BorrowRuntime(payloadCapacity + sizeof(u32));
        if (!payload || (mutationType != MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create && !payload->PutU32(recordID)) || (mutationType != MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete && !writeFields(*payload)))
        {
            return 0;
        }

        return SendMutation(artifact, mutationType, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
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

    u32 InteractionEditorBackend::CreateGossipMenuOption(u32 menuID, i32 priority, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<96>();
        if (!WriteGossipMenuOptionFields(*payload, menuID, priority, textID, icon, flags, visibilityConditionSetID, enabledConditionSetID, disabledReasonTextID, actionType, actionParameters))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::GossipMenuOption, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateGossipMenuOption(u32 optionID, u32 menuID, i32 priority, u32 textID, u16 icon, u32 flags, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u8 actionType, const std::array<i64, 4>& actionParameters)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<104>();
        if (!payload->PutU32(optionID) || !WriteGossipMenuOptionFields(*payload, menuID, priority, textID, icon, flags, visibilityConditionSetID, enabledConditionSetID, disabledReasonTextID, actionType, actionParameters))
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

    u32 InteractionEditorBackend::CreateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 defaultGreetingTextID, u32 flags, u32 rootMenuID, u32 gossipFlags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<32>();
        if (!payload->PutU32(creatureTemplateID) || !payload->PutU8(rangePolicy) || !payload->PutF32(interactionRange) || !payload->PutU32(defaultGreetingTextID) || !payload->PutU32(flags) || !payload->PutU32(rootMenuID) || !payload->PutU32(gossipFlags))
        {
            return 0;
        }

        return SendMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateInteraction, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, payload->GetDataPointer(), static_cast<u32>(payload->writtenData));
    }

    u32 InteractionEditorBackend::UpdateCreatureTemplateInteraction(u32 creatureTemplateID, u8 rangePolicy, f32 interactionRange, u32 defaultGreetingTextID, u32 flags)
    {
        std::shared_ptr<Bytebuffer> payload = Bytebuffer::Borrow<24>();
        if (!payload->PutU32(creatureTemplateID) || !payload->PutU8(rangePolicy) || !payload->PutF32(interactionRange) || !payload->PutU32(defaultGreetingTextID) || !payload->PutU32(flags))
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

    u32 InteractionEditorBackend::CreateCost(std::string_view internalName, std::span<const CostCurrencyComponentInput> currencyComponents, std::span<const CostItemComponentInput> itemComponents)
    {
        if (currencyComponents.size() + itemComponents.size() < 1 || currencyComponents.size() + itemComponents.size() > 8 ||
            currencyComponents.size() > std::numeric_limits<u8>::max() || itemComponents.size() > std::numeric_limits<u8>::max())
        {
            return 0;
        }

        const u32 payloadCapacity = static_cast<u32>(internalName.size() + 3 + currencyComponents.size() * 14 + itemComponents.size() * 12);
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::Cost, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create,
            0, payloadCapacity, [&](Bytebuffer& payload)
            {
                if (payload.PutString(internalName) == 0 || !payload.PutU8(static_cast<u8>(currencyComponents.size())))
                    return false;
                for (const CostCurrencyComponentInput& component : currencyComponents)
                {
                    if (!payload.PutI32(component.priority) || !payload.PutU16(component.currencyID) || !payload.PutU64(component.amount))
                        return false;
                }
                if (!payload.PutU8(static_cast<u8>(itemComponents.size())))
                    return false;
                for (const CostItemComponentInput& component : itemComponents)
                {
                    if (!payload.PutI32(component.priority) || !payload.PutU32(component.itemTemplateID) || !payload.PutU32(component.amount))
                        return false;
                }

                return true;
            });
    }

    u32 InteractionEditorBackend::UpdateCost(u32 costID, std::string_view internalName)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::Cost, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update,
            costID, static_cast<u32>(internalName.size() + 1), [&](Bytebuffer& payload) { return payload.PutString(internalName) > 0; });
    }

    u32 InteractionEditorBackend::DeleteCost(u32 costID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::Cost, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete,
            costID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateCostCurrencyComponent(u32 costID, i32 priority, u16 currencyID, u64 amount)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CostCurrencyComponent,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 32,
            [&](Bytebuffer& payload) { return payload.PutU32(costID) && payload.PutI32(priority) && payload.PutU16(currencyID) && payload.PutU64(amount); });
    }

    u32 InteractionEditorBackend::UpdateCostCurrencyComponent(u32 componentID, u32 costID, i32 priority, u16 currencyID, u64 amount)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CostCurrencyComponent,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, componentID, 32,
            [&](Bytebuffer& payload) { return payload.PutU32(costID) && payload.PutI32(priority) && payload.PutU16(currencyID) && payload.PutU64(amount); });
    }

    u32 InteractionEditorBackend::DeleteCostCurrencyComponent(u32 componentID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CostCurrencyComponent,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, componentID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateCostItemComponent(u32 costID, i32 priority, u32 itemTemplateID, u32 amount)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CostItemComponent,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 32,
            [&](Bytebuffer& payload) { return payload.PutU32(costID) && payload.PutI32(priority) && payload.PutU32(itemTemplateID) && payload.PutU32(amount); });
    }

    u32 InteractionEditorBackend::UpdateCostItemComponent(u32 componentID, u32 costID, i32 priority, u32 itemTemplateID, u32 amount)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CostItemComponent,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, componentID, 32,
            [&](Bytebuffer& payload) { return payload.PutU32(costID) && payload.PutI32(priority) && payload.PutU32(itemTemplateID) && payload.PutU32(amount); });
    }

    u32 InteractionEditorBackend::DeleteCostItemComponent(u32 componentID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CostItemComponent,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, componentID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateVendorList(std::string_view internalName, u32 flags, std::span<const VendorListItemInput> items)
    {
        if (items.empty() || items.size() > 256)
            return 0;

        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::VendorList,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, static_cast<u32>(internalName.size() + 7 + items.size() * 32),
            [&](Bytebuffer& payload)
            {
                if (payload.PutString(internalName) == 0 || !payload.PutU32(flags) || !payload.PutU16(static_cast<u16>(items.size())))
                    return false;
                for (const VendorListItemInput& item : items)
                {
                    if (!payload.PutI32(item.priority) || !payload.PutU32(item.itemTemplateID) || !payload.PutU32(item.purchaseQuantity) || !payload.PutU32(item.costID) ||
                        !payload.PutU32(item.visibilityConditionSetID) || !payload.PutU32(item.enabledConditionSetID) || !payload.PutU32(item.disabledReasonTextID) || !payload.PutU32(item.flags))
                    {
                        return false;
                    }
                }

                return true;
            });
    }

    u32 InteractionEditorBackend::UpdateVendorList(u32 vendorListID, std::string_view internalName, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::VendorList,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, vendorListID, static_cast<u32>(internalName.size() + 8),
            [&](Bytebuffer& payload) { return payload.PutString(internalName) > 0 && payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::DeleteVendorList(u32 vendorListID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::VendorList,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, vendorListID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateVendorListItem(u32 vendorListID, i32 priority, u32 itemTemplateID, u32 purchaseQuantity, u32 costID, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::VendorListItem,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 64, [&](Bytebuffer& payload)
            {
                return payload.PutU32(vendorListID) && payload.PutI32(priority) && payload.PutU32(itemTemplateID) && payload.PutU32(purchaseQuantity) &&
                    payload.PutU32(costID) && payload.PutU32(visibilityConditionSetID) && payload.PutU32(enabledConditionSetID) &&
                    payload.PutU32(disabledReasonTextID) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::UpdateVendorListItem(u32 itemID, u32 vendorListID, i32 priority, u32 itemTemplateID, u32 purchaseQuantity, u32 costID, u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 disabledReasonTextID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::VendorListItem,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, itemID, 64, [&](Bytebuffer& payload)
            {
                return payload.PutU32(vendorListID) && payload.PutI32(priority) && payload.PutU32(itemTemplateID) && payload.PutU32(purchaseQuantity) &&
                    payload.PutU32(costID) && payload.PutU32(visibilityConditionSetID) && payload.PutU32(enabledConditionSetID) &&
                    payload.PutU32(disabledReasonTextID) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::DeleteVendorListItem(u32 itemID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::VendorListItem,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, itemID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateCreatureTemplateVendor(u32 creatureTemplateID, u32 vendorListID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateVendor,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, creatureTemplateID, 16,
            [&](Bytebuffer& payload) { return payload.PutU32(creatureTemplateID) && payload.PutU32(vendorListID) && payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::UpdateCreatureTemplateVendor(u32 creatureTemplateID, u32 vendorListID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateVendor,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, creatureTemplateID, 8,
            [&](Bytebuffer& payload) { return payload.PutU32(vendorListID) && payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::DeleteCreatureTemplateVendor(u32 creatureTemplateID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateVendor,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, creatureTemplateID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestTemplate(std::string_view internalName, u32 titleTextID, u32 summaryTextID, u32 detailsTextID, u32 logTextID, u32 progressTextID, u32 completionTextID, u32 visibilityConditionSetID, u32 availabilityConditionSetID, u32 unavailableReasonTextID, u16 level, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestTemplate,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, static_cast<u32>(internalName.size() + 64), [&](Bytebuffer& payload)
            {
                return payload.PutString(internalName) > 0 && payload.PutU32(titleTextID) && payload.PutU32(summaryTextID) && payload.PutU32(detailsTextID) &&
                    payload.PutU32(logTextID) && payload.PutU32(progressTextID) && payload.PutU32(completionTextID) && payload.PutU32(visibilityConditionSetID) &&
                    payload.PutU32(availabilityConditionSetID) && payload.PutU32(unavailableReasonTextID) && payload.PutU16(level) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::UpdateQuestTemplate(u32 questID, std::string_view internalName, u32 titleTextID, u32 summaryTextID, u32 detailsTextID, u32 logTextID, u32 progressTextID, u32 completionTextID, u32 visibilityConditionSetID, u32 availabilityConditionSetID, u32 unavailableReasonTextID, u16 level, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestTemplate,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, questID, static_cast<u32>(internalName.size() + 64), [&](Bytebuffer& payload)
            {
                return payload.PutString(internalName) > 0 && payload.PutU32(titleTextID) && payload.PutU32(summaryTextID) && payload.PutU32(detailsTextID) &&
                    payload.PutU32(logTextID) && payload.PutU32(progressTextID) && payload.PutU32(completionTextID) && payload.PutU32(visibilityConditionSetID) &&
                    payload.PutU32(availabilityConditionSetID) && payload.PutU32(unavailableReasonTextID) && payload.PutU16(level) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::DeleteQuestTemplate(u32 questID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestTemplate,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, questID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestPrerequisite(u32 questID, u32 prerequisiteQuestID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestPrerequisite,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 8,
            [&](Bytebuffer& payload) { return payload.PutU32(questID) && payload.PutU32(prerequisiteQuestID); });
    }

    u32 InteractionEditorBackend::UpdateQuestPrerequisite(u32 prerequisiteID, u32 questID, u32 prerequisiteQuestID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestPrerequisite,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, prerequisiteID, 8,
            [&](Bytebuffer& payload) { return payload.PutU32(questID) && payload.PutU32(prerequisiteQuestID); });
    }

    u32 InteractionEditorBackend::DeleteQuestPrerequisite(u32 prerequisiteID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestPrerequisite,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, prerequisiteID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateCreatureTemplateQuestGiver(u32 creatureTemplateID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateQuestGiver,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 8,
            [&](Bytebuffer& payload) { return payload.PutU32(creatureTemplateID) && payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::UpdateCreatureTemplateQuestGiver(u32 creatureTemplateID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateQuestGiver,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, creatureTemplateID, 4,
            [&](Bytebuffer& payload) { return payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::DeleteCreatureTemplateQuestGiver(u32 creatureTemplateID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateQuestGiver,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, creatureTemplateID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateCreatureTemplateQuest(u32 creatureTemplateID, u32 questID, u8 roleFlags, i32 priority,
        u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateQuest,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(creatureTemplateID) && payload.PutU32(questID) && payload.PutU8(roleFlags) && payload.PutI32(priority) &&
                    payload.PutU32(visibilityConditionSetID) && payload.PutU32(enabledConditionSetID) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::UpdateCreatureTemplateQuest(u32 associationID, u32 creatureTemplateID, u32 questID, u8 roleFlags, i32 priority,
        u32 visibilityConditionSetID, u32 enabledConditionSetID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateQuest,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, associationID, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(creatureTemplateID) && payload.PutU32(questID) && payload.PutU8(roleFlags) && payload.PutI32(priority) &&
                    payload.PutU32(visibilityConditionSetID) && payload.PutU32(enabledConditionSetID) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::DeleteCreatureTemplateQuest(u32 associationID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::CreatureTemplateQuest,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, associationID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestObjective(u32 questID, i32 priority, u8 objectiveType, u32 requiredCount, u32 textID, u32 flags, std::span<const QuestObjectiveTargetInput> targets)
    {
        if (targets.empty() || targets.size() > 32)
            return 0;

        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestObjective,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, static_cast<u32>(32 + targets.size() * 12), [&](Bytebuffer& payload)
            {
                if (!payload.PutU32(questID) || !payload.PutI32(priority) || !payload.PutU8(objectiveType) || !payload.PutU32(requiredCount) ||
                    !payload.PutU32(textID) || !payload.PutU32(flags) || !payload.PutU8(static_cast<u8>(targets.size())))
                {
                    return false;
                }

                for (const QuestObjectiveTargetInput& target : targets)
                {
                    if (!payload.PutI32(target.priority) || !payload.PutU32(target.targetID) || !payload.PutU32(target.contribution))
                        return false;
                }

                return true;
            });
    }

    u32 InteractionEditorBackend::UpdateQuestObjective(u32 objectiveID, u32 questID, i32 priority, u8 objectiveType, u32 requiredCount, u32 textID, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestObjective,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, objectiveID, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(questID) && payload.PutI32(priority) && payload.PutU8(objectiveType) && payload.PutU32(requiredCount) &&
                    payload.PutU32(textID) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::DeleteQuestObjective(u32 objectiveID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestObjective,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, objectiveID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestObjectiveTarget(u32 objectiveID, i32 priority, u32 targetID, u32 contribution)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestObjectiveTarget,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 24,
            [&](Bytebuffer& payload) { return payload.PutU32(objectiveID) && payload.PutI32(priority) && payload.PutU32(targetID) && payload.PutU32(contribution); });
    }

    u32 InteractionEditorBackend::UpdateQuestObjectiveTarget(u32 objectiveTargetID, u32 objectiveID, i32 priority, u32 targetID, u32 contribution)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestObjectiveTarget,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, objectiveTargetID, 24,
            [&](Bytebuffer& payload) { return payload.PutU32(objectiveID) && payload.PutI32(priority) && payload.PutU32(targetID) && payload.PutU32(contribution); });
    }

    u32 InteractionEditorBackend::DeleteQuestObjectiveTarget(u32 objectiveTargetID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestObjectiveTarget, MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, objectiveTargetID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestRewardGroup(u32 questID, i32 priority, u8 selectionCount, u32 flags, std::span<const QuestRewardItemInput> items, std::span<const QuestRewardCurrencyInput> currencies, std::span<const QuestRewardReputationInput> reputations, std::span<const QuestRewardExperienceInput> experience)
    {
        const size_t rewardCount = items.size() + currencies.size() + reputations.size() + experience.size();
        if (rewardCount == 0 || rewardCount > 16 || items.size() > std::numeric_limits<u8>::max() || currencies.size() > std::numeric_limits<u8>::max() ||
            reputations.size() > std::numeric_limits<u8>::max() || experience.size() > std::numeric_limits<u8>::max())
        {
            return 0;
        }

        const size_t payloadCapacity = 32 + items.size() * 16 + currencies.size() * 18 + reputations.size() * 14 + experience.size() * 16;
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardGroup,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, static_cast<u32>(payloadCapacity), [&](Bytebuffer& payload)
            {
                if (!payload.PutU32(questID) || !payload.PutI32(priority) || !payload.PutU8(selectionCount) || !payload.PutU32(flags) ||
                    !payload.PutU8(static_cast<u8>(items.size())) || !payload.PutU8(static_cast<u8>(currencies.size())) ||
                    !payload.PutU8(static_cast<u8>(reputations.size())) || !payload.PutU8(static_cast<u8>(experience.size())))
                {
                    return false;
                }

                for (const QuestRewardItemInput& item : items)
                {
                    if (!payload.PutI32(item.priority) || !payload.PutU32(item.itemTemplateID) || !payload.PutU32(item.quantity) || !payload.PutU32(item.flags))
                        return false;
                }

                for (const QuestRewardCurrencyInput& currency : currencies)
                {
                    if (!payload.PutI32(currency.priority) || !payload.PutU16(currency.currencyID) || !payload.PutU64(currency.amount) || !payload.PutU32(currency.flags))
                        return false;
                }

                for (const QuestRewardReputationInput& reputation : reputations)
                {
                    if (!payload.PutI32(reputation.priority) || !payload.PutU16(reputation.factionID) || !payload.PutI32(reputation.amount) || !payload.PutU32(reputation.flags))
                        return false;
                }

                for (const QuestRewardExperienceInput& reward : experience)
                {
                    if (!payload.PutI32(reward.priority) || !payload.PutU64(reward.amount) || !payload.PutU32(reward.flags))
                        return false;
                }

                return true;
            });
    }

    u32 InteractionEditorBackend::UpdateQuestRewardGroup(u32 groupID, u32 questID, i32 priority, u8 selectionCount, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardGroup,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, groupID, 24,
            [&](Bytebuffer& payload) { return payload.PutU32(questID) && payload.PutI32(priority) && payload.PutU8(selectionCount) && payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::DeleteQuestRewardGroup(u32 groupID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardGroup,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, groupID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestRewardItem(u32 groupID, i32 priority, u32 itemTemplateID, u32 quantity, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardItem,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU32(itemTemplateID) && payload.PutU32(quantity) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::UpdateQuestRewardItem(u32 rewardID, u32 groupID, i32 priority, u32 itemTemplateID, u32 quantity, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardItem,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, rewardID, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU32(itemTemplateID) && payload.PutU32(quantity) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::DeleteQuestRewardItem(u32 rewardID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardItem,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, rewardID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestRewardCurrency(u32 groupID, i32 priority, u16 currencyID, u64 amount, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardCurrency,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU16(currencyID) && payload.PutU64(amount) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::UpdateQuestRewardCurrency(u32 rewardID, u32 groupID, i32 priority, u16 currencyID, u64 amount, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardCurrency,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, rewardID, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU16(currencyID) && payload.PutU64(amount) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::DeleteQuestRewardCurrency(u32 rewardID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardCurrency,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, rewardID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestRewardReputation(u32 groupID, i32 priority, u16 factionID, i32 amount, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardReputation,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU16(factionID) && payload.PutI32(amount) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::UpdateQuestRewardReputation(u32 rewardID, u32 groupID, i32 priority, u16 factionID, i32 amount, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardReputation,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, rewardID, 32, [&](Bytebuffer& payload)
            {
                return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU16(factionID) && payload.PutI32(amount) && payload.PutU32(flags);
            });
    }

    u32 InteractionEditorBackend::DeleteQuestRewardReputation(u32 rewardID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardReputation,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, rewardID, 0, [](Bytebuffer&) { return true; });
    }

    u32 InteractionEditorBackend::CreateQuestRewardExperience(u32 groupID, i32 priority, u64 amount, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardExperience,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Create, 0, 32,
            [&](Bytebuffer& payload) { return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU64(amount) && payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::UpdateQuestRewardExperience(u32 rewardID, u32 groupID, i32 priority, u64 amount, u32 flags)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardExperience,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Update, rewardID, 32,
            [&](Bytebuffer& payload) { return payload.PutU32(groupID) && payload.PutI32(priority) && payload.PutU64(amount) && payload.PutU32(flags); });
    }

    u32 InteractionEditorBackend::DeleteQuestRewardExperience(u32 rewardID)
    {
        return SendRecordMutation(MetaGen::Shared::Interaction::InteractionEditorArtifactEnum::QuestRewardExperience,
            MetaGen::Shared::DatabaseEditor::DatabaseEditorMutationTypeEnum::Delete, rewardID, 0, [](Bytebuffer&) { return true; });
    }

    std::optional<DatabaseEditorMutationResult> InteractionEditorBackend::TakeMutationResult(u32 requestID)
    {
        InteractionEditorData* data = GetData(false);
        return data ? data->TakeMutationResult(requestID) : std::nullopt;
    }
}
