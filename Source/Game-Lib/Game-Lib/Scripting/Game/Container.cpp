#include "Container.h"

#include "Game-Lib/ECS/Components/Container.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Singletons/NetworkState.h"
#include "Game-Lib/ECS/Util/MessageBuilderUtil.h"
#include "Game-Lib/ECS/Util/Database/ItemUtil.h"
#include "Game-Lib/Gameplay/Database/Item.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <Network/Client.h>

#include <entt/entt.hpp>
#include <Game-Lib/ECS/Components/Item.h>

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
                    GameDefine::ObjectGuid containerGuid = characterSingleton.containers[srcContainerIndex];
                    if (!containerGuid.IsValid())
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGuid))
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    containerEntity = networkState.networkIDToEntity[containerGuid];
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

                GameDefine::ObjectGuid srcObjectGuid = container.items[srcSlotIndex];

                if (!srcObjectGuid.IsValid())
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
                    GameDefine::ObjectGuid containerGuid = characterSingleton.containers[srcContainerIndex];
                    if (!containerGuid.IsValid())
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGuid))
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    srcContainerEntity = networkState.networkIDToEntity[containerGuid];
                }

                if (destContainerIndex == 0)
                {
                    destContainerEntity = characterSingleton.baseContainerEntity;
                }
                else
                {
                    GameDefine::ObjectGuid containerGuid = characterSingleton.containers[destContainerIndex];
                    if (!containerGuid.IsValid())
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    if (!networkState.networkIDToEntity.contains(containerGuid))
                    {
                        ctx.Push(false);
                        return 1;
                    }

                    destContainerEntity = networkState.networkIDToEntity[containerGuid];
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

                GameDefine::ObjectGuid srcObjectGuid = srcContainer.items[srcSlotIndex];
                if (!srcObjectGuid.IsValid())
                {
                    ctx.Push(false);
                    return 1;
                }
            }

            std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<32>();
            if (!ECS::Util::MessageBuilder::Container::BuildRequestSwapSlots(buffer, srcContainerIndex, destContainerIndex, srcSlotIndex, destSlotIndex))
            {
                ctx.Push(false);
                return 1;
            }

            networkState.client->Send(buffer);
            ctx.Push(true);
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
            entt::entity containerEntity = entt::null;
            if (containerIndex == 0)
            {
                containerEntity = characterSingleton.baseContainerEntity;
            }
            else
            {
                GameDefine::ObjectGuid containerGuid = characterSingleton.containers[containerIndex];
                if (!containerGuid.IsValid())
                    return 0;

                if (!networkState.networkIDToEntity.contains(containerGuid))
                    return 0;

                containerEntity = networkState.networkIDToEntity[containerGuid];
            }

            if (!registry->valid(containerEntity))
                return 0;

            auto& container = registry->get<ECS::Components::Container>(containerEntity);

            ctx.CreateTableAndPopulate([&]()
            {
                u32 numItems = static_cast<u32>(container.items.size());

                for (u32 i = 0; i < numItems; i++)
                {
                    const auto& containerItemGuid = container.items[i];
                    if (!containerItemGuid.IsValid())
                        continue;

                    if (!networkState.networkIDToEntity.contains(containerItemGuid))
                        continue;

                    entt::entity itemEntity = networkState.networkIDToEntity[containerItemGuid];
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

            return 1;
        }
    }
}