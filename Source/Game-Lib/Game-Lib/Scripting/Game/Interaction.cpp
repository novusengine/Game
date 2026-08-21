#include "Interaction.h"

#include "Game-Lib/Application/EnttRegistries.h"
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
        i32 GetState(Zenith* zenith)
        {
            entt::registry& registry = *ServiceLocator::GetEnttRegistries()->gameRegistry;
            const auto& networkState = registry.ctx().get<ECS::Singletons::NetworkState>();
            const ECS::Singletons::InteractionState& state = networkState.interactionState;

            zenith->CreateTable();
            zenith->AddTableField("version", state.changeVersion);

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
                zenith->AddTableField("greeting", session.greeting.c_str());

                zenith->CreateTable();
                i32 optionIndex = 0;
                for (const ECS::Singletons::InteractionOptionState& option : session.options)
                {
                    zenith->CreateTable();
                    zenith->AddTableField("token", option.token);
                    zenith->AddTableField("icon", option.icon);
                    zenith->AddTableField("enabled", option.enabled);
                    zenith->AddTableField("text", option.text.c_str());
                    zenith->AddTableField("disabledReason", option.disabledReason.c_str());
                    zenith->SetTableKey(++optionIndex);
                }
                zenith->SetTableKey("options");
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
    }
}
