#include "Container.h"

#include "Game-Lib/ECS/Components/Container.h"
#include "Game-Lib/ECS/Components/Item.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/ECS/Util/Network/NetworkUtil.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Meta/Generated/Shared/NetworkPacket.h>

#include <Network/Client.h>

#include <entt/entt.hpp>

namespace Scripting::Game
{
    static LuaMethod containerStaticFunctions[] =
    {
        { "RequestSwapSlots", ContainerMethods::RequestSwapSlots },
        { "GetContainerItems", ContainerMethods::GetContainerItems }
    };

    void Container::Register(lua_State* state)
    {
        LuaState ctx(state);

        LuaMethodTable::Set(state, containerStaticFunctions, "Container");
    }

    namespace ContainerMethods
    {
        i32 RequestSwapSlots(lua_State* state)
        {
            LuaState ctx(state);

            u32 srcContainerIndex = ctx.Get(0, 1) - 1;
            u32 destContainerIndex = ctx.Get(0, 2) - 1;
            u32 srcSlotIndex = ctx.Get(0, 3) - 1;
            u32 destSlotIndex = ctx.Get(0, 4) - 1;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;

            auto& networkState = registry->ctx().get<ECS::Singletons::NetworkState>();
            if (!networkState.client->IsConnected())
            {
                ctx.Push(false);
                return 1;
            }

            if (srcContainerIndex > 5 || destContainerIndex > 5)
            {
                ctx.Push(false);
                return 1;
            }

            auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

            bool isSameContainer = srcContainerIndex == destContainerIndex;
            if (isSameContainer)
            {
                if (srcSlotIndex == destSlotIndex)
                {
                    ctx.Push(false);
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
                        ctx.Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGUID))
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    containerEntity = networkState.networkIDToEntity[containerGUID];
                }

                if (!registry->valid(containerEntity))
                {
                    ctx.Push(false);
                    return 1;
                }

                auto& container = registry->get<ECS::Components::Container>(containerEntity);
                if (srcSlotIndex >= container.numSlots || destSlotIndex >= container.numSlots)
                {
                    ctx.Push(false);
                    return 1;
                }

                ObjectGUID srcObjectGUID = container.items[srcSlotIndex];

                if (!srcObjectGUID.IsValid())
                {
                    ctx.Push(false);
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
                        ctx.Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGUID))
                    {
                        ctx.Push(false);
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
                        ctx.Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGUID))
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    destContainerEntity = networkState.networkIDToEntity[containerGUID];
                }

                if (!registry->valid(srcContainerEntity) || !registry->valid(destContainerEntity))
                {
                    ctx.Push(false);
                    return 1;
                }

                auto& srcContainer = registry->get<ECS::Components::Container>(srcContainerEntity);
                auto& destContainer = registry->get<ECS::Components::Container>(destContainerEntity);
                if (srcSlotIndex >= srcContainer.numSlots || destSlotIndex >= destContainer.numSlots)
                {
                    ctx.Push(false);
                    return 1;
                }

                ObjectGUID srcObjectGUID = srcContainer.items[srcSlotIndex];
                if (!srcObjectGUID.IsValid())
                {
                    ctx.Push(false);
                    return 1;
                }
            }

            bool result = ECS::Util::Network::SendPacket(networkState, Generated::ClientContainerSwapSlotsPacket{
                .srcContainer = static_cast<u16>(srcContainerIndex),
                .dstContainer = static_cast<u8>(destContainerIndex),
                .srcSlot = static_cast<u8>(srcSlotIndex),
                .dstSlot = static_cast<u8>(destSlotIndex)
            });

            ctx.Push(result);
            return 1;
        }

        i32 GetContainerItems(lua_State* state)
        {
            LuaState ctx(state);

            u32 containerIndex = ctx.Get(0, 1) - 1;
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

            ctx.Push((u32)container.numSlots);
            ctx.CreateTableAndPopulate([&]()
            {
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

                    ctx.CreateTableAndPopulate([&ctx, &item, i]()
                    {
                        ctx.SetTable("slot", i + 1);
                        ctx.SetTable("itemID", item.itemID);
                        ctx.SetTable("count", item.count);
                        ctx.SetTable("durability", item.durability);
                    });

                    ctx.SetTable(i + 1);
                }
            });

            return 2;
        }
    }
}