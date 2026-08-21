#include "InteractionUtil.h"

#include "Game-Lib/ECS/Components/InteractionCapabilities.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"

#include <MetaGen/Shared/Interaction/Interaction.h>
#include <MetaGen/Shared/Packet/Packet.h>

#include <entt/entt.hpp>

namespace ECS::Util::Interaction
{
    bool Open(entt::registry& registry, entt::entity source)
    {
        if (!registry.valid(source))
            return false;

        const auto* capabilities = registry.try_get<Components::InteractionCapabilities>(source);
        constexpr auto gossipCapability = MetaGen::Shared::Interaction::InteractionCapabilityMaskEnum::Gossip;
        if (!capabilities || (capabilities->value & gossipCapability) == MetaGen::Shared::Interaction::InteractionCapabilityMaskEnum::None)
            return false;

        auto& networkState = registry.ctx().get<Singletons::NetworkState>();
        if (!networkState.isInWorld)
            return false;

        ObjectGUID sourceGUID;
        if (!Network::GetObjectGUIDFromEntityID(networkState, source, sourceGUID))
            return false;

        return Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientInteractionOpenPacket{ .sourceGUID = sourceGUID });
    }

    bool Select(entt::registry& registry, u64 sessionID, u32 revision, u64 optionToken)
    {
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();
        const std::optional<Singletons::InteractionSessionState>& activeSession = networkState.interactionState.activeSession;
        if (!networkState.isInWorld || !activeSession || activeSession->id != sessionID || activeSession->revision != revision)
            return false;

        bool enabledOption = false;
        for (const Singletons::InteractionOptionState& option : activeSession->options)
        {
            if (option.token == optionToken)
            {
                enabledOption = option.enabled;
                break;
            }
        }
        if (!enabledOption)
            return false;

        return Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientInteractionSelectPacket{ .sessionID = sessionID, .revision = revision, .optionToken = optionToken });
    }

    bool Close(entt::registry& registry, u64 sessionID)
    {
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();
        const std::optional<Singletons::InteractionSessionState>& activeSession = networkState.interactionState.activeSession;
        if (!networkState.isInWorld || !activeSession || activeSession->id != sessionID)
            return false;

        return Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientInteractionClosePacket{ .sessionID = sessionID });
    }
}
