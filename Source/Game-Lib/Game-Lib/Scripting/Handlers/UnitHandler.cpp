#include "UnitHandler.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Singletons/CharacterSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Util/AttachmentUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <lualib.h>
#include <entt/entt.hpp>

namespace Scripting
{
    static LuaMethod unitMethods[] =
    {
        { "GetLocal", UnitHandler::GetLocal },
        { "GetNamePosition", UnitHandler::GetNamePosition }
    };

    void UnitHandler::Register(lua_State* state)
    {
        LuaMethodTable::Set(state, unitMethods, "Unit");
    }

    void UnitHandler::Clear()
    {

    }

    i32 UnitHandler::GetLocal(lua_State* state)
    {
        LuaState ctx(state);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        auto& characterSingleton = registry->ctx().get<ECS::Singletons::CharacterSingleton>();

        if (!registry->valid(characterSingleton.moverEntity))
        {
            ctx.Push();
        }
        else
        {
            ctx.Push(entt::to_integral(characterSingleton.moverEntity));
        }

        return 1;
    }

    i32 UnitHandler::GetNamePosition(lua_State* state)
    {
        LuaState ctx(state);

        u32 unitID = ctx.Get(std::numeric_limits<u32>().max(), 1);
        if (unitID == std::numeric_limits<u32>().max())
            return 0;

        entt::entity entityID = entt::entity(unitID);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
        if (!registry->valid(entityID))
            return 0;

        if (!registry->all_of<ECS::Components::Model, ECS::Components::AttachmentData>(entityID))
            return 0;

        ECS::Components::Model& model = registry->get<ECS::Components::Model>(entityID);
        ECS::Components::AttachmentData& attachmentData = registry->get<ECS::Components::AttachmentData>(entityID);

        if (!Util::Attachment::EnableAttachment(entityID, model, attachmentData, Attachment::Defines::Type::PlayerName))
            return 0;

        const mat4x4* mat = Util::Attachment::GetAttachmentMatrix(attachmentData, Attachment::Defines::Type::PlayerName);
        vec3 position = (*mat)[3];

        const auto& transform = registry->get<ECS::Components::Transform>(entityID);
        position += transform.GetWorldPosition();

        ctx.Push(position);
        return 1;
    }
}
