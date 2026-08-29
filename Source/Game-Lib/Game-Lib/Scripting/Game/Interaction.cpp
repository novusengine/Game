#include "Interaction.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/Database/CostSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/InteractionUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/Interaction/Interaction.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>

#include <limits>

namespace Scripting::Game
{
    void Interaction::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, interactionGlobalFunctions, "Interaction");
    }

    namespace InteractionMethods
    {
        i32 GetVersion(Zenith* zenith)
        {
            const entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            zenith->Push(registry.ctx().get<ECS::Singletons::NetworkState>().interactionState.changeVersion);
            return 1;
        }

        i32 GetState(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            const auto& networkState = registry.ctx().get<ECS::Singletons::NetworkState>();
            const ECS::Singletons::InteractionState& state = networkState.interactionState;
            const auto* costs = ServiceLocator::GetEnttRegistries()->dbRegistry->ctx().find<ECS::Singletons::CostSingleton>();

            zenith->CreateTable();
            zenith->AddTableField("version", state.changeVersion);

            zenith->CreateTable();
            zenith->AddTableField("revision", state.questLogRevision);
            zenith->CreateTable();
            i32 questLogIndex = 0;
            for (const auto& quest : state.questLog)
            {
                zenith->CreateTable();
                zenith->AddTableField("questID", quest.id);
                zenith->AddTableField("state", static_cast<u8>(quest.state));
                zenith->AddTableField("title", quest.title.c_str());
                zenith->AddTableField("logText", quest.logText.c_str());
                zenith->CreateTable();
                i32 logObjectiveIndex = 0;
                for (const auto& objective : quest.objectives)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("objectiveID", objective.id);
                    zenith->AddTableField("text", objective.text.c_str());
                    zenith->AddTableField("displayText", objective.displayText.c_str());
                    zenith->AddTableField("progress", objective.progress);
                    zenith->AddTableField("required", objective.required);
                    zenith->AddTableField("optional", objective.optional);
                    zenith->SetTableKey(++logObjectiveIndex);
                }

                zenith->SetTableKey("objectives");
                zenith->SetTableKey(++questLogIndex);
            }

            zenith->SetTableKey("quests");
            zenith->SetTableKey("questLog");

            if (state.activeSession)
            {
                const ECS::Singletons::InteractionSessionState& session = *state.activeSession;
                const auto sourceItr = networkState.networkIDToEntity.find(session.sourceGUID);
                const entt::id_type sourceUnitID = sourceItr == networkState.networkIDToEntity.end()
                    ? std::numeric_limits<entt::id_type>().max()
                    : entt::to_integral(sourceItr->second);

                zenith->CreateTable();
                zenith->AddTableField("sessionID", session.id);
                zenith->AddTableField("revision", session.revision);
                zenith->AddTableField("sourceUnitID", sourceUnitID);
                zenith->AddTableField("surfaceType", static_cast<u8>(session.surfaceType));
                zenith->AddTableField("questMask", static_cast<u8>(session.questMask));
                zenith->AddTableField("heading", session.heading.c_str());
                zenith->AddTableField("greeting", session.greeting.c_str());

                zenith->CreateTable();
                i32 optionIndex = 0;
                for (const ECS::Singletons::InteractionOptionState& option : session.options)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("token", option.token);
                    zenith->AddTableField("icon", option.icon);
                    zenith->AddTableField("kind", static_cast<u8>(option.kind));
                    zenith->AddTableField("enabled", option.enabled);
                    zenith->AddTableField("itemTemplateID", option.itemTemplateID);
                    zenith->AddTableField("quantity", option.quantity);
                    zenith->AddTableField("costID", option.costID);
                    zenith->AddTableField("costDataAvailable", option.costDataAvailable);
                    zenith->CreateTable();
                    i32 currencyComponentIndex = 0;
                    for (const auto& component : option.costCurrencyComponents)
                    {
                        zenith->CreateTable();
                        zenith->AddTableField("currencyID", component.currencyID);
                        const char* currencyName = "Unknown currency";
                        if (costs)
                        {
                            const auto currencyItr = costs->currencyNamesByID.find(component.currencyID);
                            if (currencyItr != costs->currencyNamesByID.end())
                                currencyName = currencyItr->second.c_str();
                        }

                        zenith->AddTableField("name", currencyName);
                        zenith->AddTableField("amount", component.amount);
                        zenith->SetTableKey(++currencyComponentIndex);
                    }

                    zenith->SetTableKey("costCurrencyComponents");
                    zenith->CreateTable();
                    i32 itemComponentIndex = 0;
                    for (const auto& component : option.costItemComponents)
                    {
                        zenith->CreateTable();
                        zenith->AddTableField("itemTemplateID", component.itemTemplateID);
                        zenith->AddTableField("amount", component.amount);
                        zenith->SetTableKey(++itemComponentIndex);
                    }

                    zenith->SetTableKey("costItemComponents");
                    zenith->AddTableField("text", option.text.c_str());
                    zenith->AddTableField("disabledReason", option.disabledReason.c_str());
                    zenith->SetTableKey(++optionIndex);
                }

                zenith->SetTableKey("options");

                zenith->CreateTable();
                i32 objectiveIndex = 0;
                for (const auto& objective : session.objectives)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("text", objective.text.c_str());
                    zenith->AddTableField("displayText", objective.displayText.c_str());
                    zenith->AddTableField("progress", objective.progress);
                    zenith->AddTableField("required", objective.required);
                    zenith->AddTableField("optional", objective.optional);
                    zenith->SetTableKey(++objectiveIndex);
                }

                zenith->SetTableKey("objectives");

                zenith->CreateTable();
                i32 rewardIndex = 0;
                for (const auto& reward : session.rewards)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("groupID", reward.groupID);
                    zenith->AddTableField("selectionID", reward.selectionID);
                    zenith->AddTableField("type", static_cast<u8>(reward.type));
                    zenith->AddTableField("resourceID", reward.resourceID);
                    zenith->AddTableField("amount", reward.amount);
                    zenith->AddTableField("amountNegative", reward.amountNegative);
                    zenith->AddTableField("selectionCount", reward.selectionCount);
                    zenith->AddTableField("flags", reward.flags);
                    zenith->AddTableField("selected", session.selectedRewardSelectionIDs.contains(reward.selectionID));
                    zenith->SetTableKey(++rewardIndex);
                }

                zenith->SetTableKey("rewards");
                zenith->SetTableKey("activeSession");
            }

            if (state.lastClosedSessionID != 0)
            {
                zenith->CreateTable();
                zenith->AddTableField("sessionID", state.lastClosedSessionID);
                zenith->AddTableField("reason", static_cast<u8>(state.lastCloseReason));
                zenith->SetTableKey("lastClose");
            }

            if (state.lastResult != MetaGen::Shared::Interaction::InteractionResultEnum::Count)
            {
                zenith->CreateTable();
                zenith->AddTableField("sessionID", state.lastResultSessionID);
                zenith->AddTableField("revision", state.lastResultRevision);
                zenith->AddTableField("result", static_cast<u8>(state.lastResult));
                zenith->SetTableKey("lastResult");
            }

            if (state.lastQuestCommandResult != MetaGen::Shared::Interaction::InteractionResultEnum::Count)
            {
                zenith->CreateTable();
                zenith->AddTableField("questID", state.lastQuestCommandQuestID);
                zenith->AddTableField("result", static_cast<u8>(state.lastQuestCommandResult));
                zenith->SetTableKey("lastQuestCommand");
            }

            return 1;
        }

        i32 Open(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            const entt::entity source = static_cast<entt::entity>(zenith->CheckVal<u32>(1));
            zenith->Push(ECS::Util::Interaction::Open(registry, source));
            return 1;
        }

        i32 Select(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            zenith->Push(ECS::Util::Interaction::Select(registry, zenith->CheckVal<u64>(1), zenith->CheckVal<u32>(2), zenith->CheckVal<u64>(3)));
            return 1;
        }

        i32 Close(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            zenith->Push(ECS::Util::Interaction::Close(registry, zenith->CheckVal<u64>(1)));
            return 1;
        }

        i32 Purchase(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            zenith->Push(ECS::Util::Interaction::Purchase(registry, zenith->CheckVal<u64>(1), zenith->CheckVal<u32>(2), zenith->CheckVal<u64>(3), zenith->CheckVal<u32>(4)));
            return 1;
        }

        i32 ToggleReward(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            zenith->Push(ECS::Util::Interaction::ToggleReward(registry, zenith->CheckVal<u32>(1)));
            return 1;
        }

        i32 CompleteQuest(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            zenith->Push(ECS::Util::Interaction::CompleteQuest(registry, zenith->CheckVal<u64>(1), zenith->CheckVal<u32>(2), zenith->CheckVal<u64>(3)));
            return 1;
        }

        i32 AbandonQuest(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            zenith->Push(ECS::Util::Interaction::AbandonQuest(registry, zenith->CheckVal<u32>(1)));
            return 1;
        }
    }
}
