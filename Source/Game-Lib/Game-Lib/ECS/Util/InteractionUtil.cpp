#include "InteractionUtil.h"

#include "Game-Lib/ECS/Components/InteractionCapabilities.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"

#include <MetaGen/Shared/Interaction/Interaction.h>
#include <MetaGen/Shared/Packet/Packet.h>

#include <entt/entt.hpp>

#include <algorithm>

namespace ECS::Util::Interaction
{
    bool Open(entt::registry& registry, entt::entity source)
    {
        if (!registry.valid(source))
            return false;

        const auto* capabilities = registry.try_get<Components::InteractionCapabilities>(source);
        if (!capabilities || capabilities->value == MetaGen::Shared::Interaction::InteractionCapabilityMaskEnum::None)
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

    bool Purchase(entt::registry& registry, u64 sessionID, u32 revision, u64 purchaseToken, u32 bundleCount)
    {
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();
        const std::optional<Singletons::InteractionSessionState>& activeSession = networkState.interactionState.activeSession;
        const bool sessionMatches = activeSession && activeSession->id == sessionID && activeSession->revision == revision;
        const bool vendorSession = sessionMatches && activeSession->surfaceType == MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum::Vendor;
        if (!networkState.isInWorld || !vendorSession || bundleCount == 0 || bundleCount > 200)
            return false;

        const auto optionItr = std::ranges::find(activeSession->options, purchaseToken, &Singletons::InteractionOptionState::token);
        if (optionItr == activeSession->options.end() || !optionItr->enabled || !optionItr->costDataAvailable)
            return false;

        return Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientVendorPurchasePacket{ .sessionID = sessionID, .revision = revision, .purchaseToken = purchaseToken, .bundleCount = bundleCount });
    }

    bool ToggleReward(entt::registry& registry, u32 selectionID)
    {
        auto& interactionState = registry.ctx().get<Singletons::NetworkState>().interactionState;
        auto& session = interactionState.activeSession;
        if (!session || session->surfaceType != MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum::Quest)
            return false;

        const auto rewardItr = std::ranges::find(session->rewards, selectionID, &Singletons::InteractionSessionState::QuestReward::selectionID);
        if (rewardItr == session->rewards.end() || rewardItr->selectionCount == 0)
            return false;

        if (session->selectedRewardSelectionIDs.erase(selectionID) != 0)
        {
            interactionState.Touch();
            return true;
        }

        const size_t selectedInGroup = std::ranges::count_if(session->rewards,
            [&](const auto& reward)
            {
                return reward.groupID == rewardItr->groupID && session->selectedRewardSelectionIDs.contains(reward.selectionID);
            });
        if (selectedInGroup >= rewardItr->selectionCount)
            return false;

        session->selectedRewardSelectionIDs.insert(selectionID);
        interactionState.Touch();
        return true;
    }

    bool CompleteQuest(entt::registry& registry, u64 sessionID, u32 revision, u64 completionToken)
    {
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();
        const auto& session = networkState.interactionState.activeSession;
        const bool sessionMatches = session && session->id == sessionID && session->revision == revision;
        const bool questSession = sessionMatches && session->surfaceType == MetaGen::Shared::Interaction::InteractionSurfaceTypeEnum::Quest;
        if (!networkState.isInWorld || !questSession || session->selectedRewardSelectionIDs.size() > 64)
            return false;

        for (const auto& reward : session->rewards)
        {
            if (reward.selectionCount == 0)
                continue;

            const size_t selectedInGroup = std::ranges::count_if(session->rewards,
                [&](const auto& candidate)
                {
                    return candidate.groupID == reward.groupID && session->selectedRewardSelectionIDs.contains(candidate.selectionID);
                });
            if (selectedInGroup != reward.selectionCount)
                return false;
        }

        size_t index = 0;
        MetaGen::Shared::Packet::ClientQuestCompletePacket packet{ .sessionID = sessionID, .revision = revision, .completionToken = completionToken, .selectedRewardCount = static_cast<u8>(session->selectedRewardSelectionIDs.size()) };
        for (u32 selectionID : session->selectedRewardSelectionIDs)
        {
            packet.selectedRewardIDs[index++] = selectionID;
        }

        return Network::SendPacket(networkState, packet);
    }

    bool AbandonQuest(entt::registry& registry, u32 questID)
    {
        auto& networkState = registry.ctx().get<Singletons::NetworkState>();
        return networkState.isInWorld && questID != 0 && Network::SendPacket(networkState, MetaGen::Shared::Packet::ClientQuestAbandonPacket{ .questID = questID });
    }
}
