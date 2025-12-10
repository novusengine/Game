#include "Container.h"

#include "Game-Lib/ECS/Components/Container.h"
#include "Game-Lib/ECS/Components/Item.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Shared/Packet/Packet.h>

#include <Network/Client.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>

namespace Scripting::Game
{

    void Container::Register(Zenith* zenith)
    {
        LuaMethodTable::Set(zenith, containerGlobalFunctions, "Container");
    }

    namespace ContainerMethods
    {
        i32 RequestSwapSlots(Zenith* zenith)
        {
            u32 srcContainerIndex = zenith->CheckVal<u32>(1) - 1;
            u32 destContainerIndex = zenith->CheckVal<u32>(2) - 1;
            u32 srcSlotIndex = zenith->CheckVal<u32>(3) - 1;
            u32 destSlotIndex = zenith->CheckVal<u32>(4) - 1;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
            if (!networkState.client->IsConnected())
            {
                zenith->Push(false);
                return 1;
            }

            if (srcContainerIndex > 5 || destContainerIndex > 5)
            {
                zenith->Push(false);
                return 1;
            }

            auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

            bool isSameContainer = srcContainerIndex == destContainerIndex;
            if (isSameContainer)
            {
                if (srcSlotIndex == destSlotIndex)
                {
                    zenith->Push(false);
                    return 1;
                }

                entt::entity containerEntity = entt::null;
                if (srcContainerIndex == 0)
                {
                    containerEntity = characterSingleton.baseContainerEntity;
                }
                else
                {
                    ObjectGUID containerGUID = characterSingleton.containers[srcContainerIndex];
                    if (!containerGUID.IsValid())
                    {
                        zenith->Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGUID))
                    {
                        zenith->Push(false);
                        return 1;
                    }

                    containerEntity = networkState.networkIDToEntity[containerGUID];
                }

                if (!registry->valid(containerEntity))
                {
                    zenith->Push(false);
                    return 1;
                }

                auto& container = registry->get<ECS::Components::Container>(containerEntity);
                if (srcSlotIndex >= container.numSlots || destSlotIndex >= container.numSlots)
                {
                    zenith->Push(false);
                    return 1;
                }

                ObjectGUID srcObjectGUID = container.items[srcSlotIndex];

                if (!srcObjectGUID.IsValid())
                {
                    zenith->Push(false);
                    return 1;
                }
            }
            else
            {
                entt::entity srcContainerEntity = entt::null;
                entt::entity destContainerEntity = entt::null;

                if (srcContainerIndex == 0)
                {
                    srcContainerEntity = characterSingleton.baseContainerEntity;
                }
                else
                {
                    ObjectGUID containerGUID = characterSingleton.containers[srcContainerIndex];
                    if (!containerGUID.IsValid())
                    {
                        zenith->Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGUID))
                    {
                        zenith->Push(false);
                        return 1;
                    }

                    srcContainerEntity = networkState.networkIDToEntity[containerGUID];
                }

                if (destContainerIndex == 0)
                {
                    destContainerEntity = characterSingleton.baseContainerEntity;
                }
                else
                {
                    ObjectGUID containerGUID = characterSingleton.containers[destContainerIndex];
                    if (!containerGUID.IsValid())
                    {
                        zenith->Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGUID))
                    {
                        zenith->Push(false);
                        return 1;
                    }

                    destContainerEntity = networkState.networkIDToEntity[containerGUID];
                }

                if (!registry->valid(srcContainerEntity) || !registry->valid(destContainerEntity))
                {
                    zenith->Push(false);
                    return 1;
                }

                auto& srcContainer = registry->get<ECS::Components::Container>(srcContainerEntity);
                auto& destContainer = registry->get<ECS::Components::Container>(destContainerEntity);
                if (srcSlotIndex >= srcContainer.numSlots || destSlotIndex >= destContainer.numSlots)
                {
                    zenith->Push(false);
                    return 1;
                }

                ObjectGUID srcObjectGUID = srcContainer.items[srcSlotIndex];
                if (!srcObjectGUID.IsValid())
                {
                    zenith->Push(false);
                    return 1;
                }
            }

            bool result = ECS::Util::Network::SendPacket(networkState, MetaGen::Shared::Packet::SharedContainerSwapSlotsPacket{
                .srcContainer = static_cast<u16>(srcContainerIndex),
                .dstContainer = static_cast<u8>(destContainerIndex),
                .srcSlot = static_cast<u8>(srcSlotIndex),
                .dstSlot = static_cast<u8>(destSlotIndex)
            });

            zenith->Push(result);
            return 1;
        }

        i32 GetContainerItems(Zenith* zenith)
        {
            u32 containerIndex = zenith->CheckVal<u32>(1) - 1;
            if (containerIndex >= 6)
                return 0;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
            if (!networkState.client->IsConnected())
                return 0;

            auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();
            if (characterSingleton.baseContainerEntity == entt::null)
                return 0;

            entt::entity containerEntity = entt::null;
            if (containerIndex == 0)
            {
                containerEntity = characterSingleton.baseContainerEntity;
            }
            else
            {
                ObjectGUID containerGUID = characterSingleton.containers[containerIndex];
                if (!containerGUID.IsValid())
                    return 0;

                if (!networkState.networkIDToEntity.contains(containerGUID))
                    return 0;

                containerEntity = networkState.networkIDToEntity[containerGUID];
            }

            if (!registry->valid(containerEntity))
                return 0;

            auto& container = registry->get<ECS::Components::Container>(containerEntity);

            zenith->Push((u32)container.numSlots);

            zenith->CreateTable();

            u32 numItems = static_cast<u32>(container.items.size());

            for (u32 i = 0; i < numItems; i++)
            {
                const auto& containerItemGUID = container.items[i];
                if (!containerItemGUID.IsValid())
                    continue;

                if (!networkState.networkIDToEntity.contains(containerItemGUID))
                    continue;

                entt::entity itemEntity = networkState.networkIDToEntity[containerItemGUID];
                const auto& item = registry->get<ECS::Components::Item>(itemEntity);

                zenith->CreateTable();
                zenith->AddTableField("slot", i + 1);
                zenith->AddTableField("itemID", item.itemID);
                zenith->AddTableField("count", item.count);
                zenith->AddTableField("durability", item.durability);

                zenith->SetTableKey(i + 1);
            }

            return 2;
        }
    }
}