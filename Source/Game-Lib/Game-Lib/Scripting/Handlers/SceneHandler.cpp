#include "SceneHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/Decal.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/Unit.h"
#include "Game-Lib/ECS/Util/CameraUtil.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <lualib.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Scripting::Scene
{
    void SceneHandler::Register(Zenith* zenith)
    {
        LuaManager* luaManager = ServiceLocator::GetLuaManager();
        const bool inDeveloperMode = luaManager && luaManager->IsDeveloperMode();
        const Scripting::LuaMethodFlags excludeFlags = inDeveloperMode
            ? Scripting::LuaMethodFlags::None
            : Scripting::LuaMethodFlags::DeveloperOnly;

        LuaMethodTable::Set(zenith, sceneGlobalMethods, "Scene", excludeFlags);
    }

    static entt::registry* GetGameRegistry()
    {
        EnttRegistries* registries = ServiceLocator::GetEnttRegistries();
        return registries ? registries->gameRegistry : nullptr;
    }

    // Reads a u32 entity id at the given stack index and returns it only if it maps to a
    // live entity; otherwise returns entt::null.
    static entt::entity GetEntityArg(Zenith* zenith, entt::registry* registry, i32 index)
    {
        if (!registry || !zenith->IsNumber(index))
            return entt::null;

        entt::entity entity = entt::entity(zenith->CheckVal<u32>(index));
        return registry->valid(entity) ? entity : entt::null;
    }

    i32 SceneHandler::GetEntities(Zenith* zenith)
    {
        zenith->CreateTable();

        entt::registry* registry = GetGameRegistry();
        if (!registry)
            return 1;

        auto view = registry->view<ECS::Components::Name>();
        i32 index = 0;
        view.each([&](entt::entity entity, ECS::Components::Name& name)
        {
            zenith->CreateTable();
            zenith->AddTableField("id", entt::to_integral(entity));
            zenith->AddTableField("name", name.name.c_str());
            zenith->SetTableKey(++index);
        });
        return 1;
    }

    i32 SceneHandler::CenterCameraOnEntity(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        if (entity == entt::null)
            return 0;

        ECS::Components::WorldAABB* worldAABB = registry->try_get<ECS::Components::WorldAABB>(entity);
        if (!worldAABB)
            return 0;

        vec3 position = (worldAABB->min + worldAABB->max) * 0.5f;
        f32 radius = glm::distance(worldAABB->min, worldAABB->max) * 0.5f;
        ECS::Util::CameraUtil::CenterOnObject(position, radius);
        return 0;
    }

    i32 SceneHandler::GetName(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Name* name = (entity != entt::null) ? registry->try_get<ECS::Components::Name>(entity) : nullptr;
        if (!name)
        {
            zenith->Push();
            return 1;
        }
        zenith->Push(name->name.c_str());
        return 1;
    }

    i32 SceneHandler::SetName(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Name* name = (entity != entt::null) ? registry->try_get<ECS::Components::Name>(entity) : nullptr;
        if (!name)
            return 0;

        const char* newName = zenith->CheckVal<const char*>(2);
        if (newName)
            name->name = newName;
        return 0;
    }

    i32 SceneHandler::GetTransform(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Transform* transform = (entity != entt::null) ? registry->try_get<ECS::Components::Transform>(entity) : nullptr;
        if (!transform)
        {
            zenith->Push();
            return 1;
        }

        vec3 eulerDegrees = glm::degrees(glm::eulerAngles(transform->GetLocalRotation()));

        zenith->CreateTable();
        zenith->AddTableField("position", transform->GetLocalPosition());
        zenith->AddTableField("rotation", eulerDegrees);
        zenith->AddTableField("scale", transform->GetLocalScale());
        return 1;
    }

    i32 SceneHandler::SetTransform(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Transform* transform = (entity != entt::null) ? registry->try_get<ECS::Components::Transform>(entity) : nullptr;
        if (!transform)
            return 0;

        vec3 position = zenith->CheckVal<vec3>(2);
        vec3 eulerDegrees = zenith->CheckVal<vec3>(3);
        vec3 scale = zenith->CheckVal<vec3>(4);

        ECS::TransformSystem::Get(*registry).SetLocalTransform(entity, position, glm::quat(glm::radians(eulerDegrees)), scale);
        return 0;
    }

    i32 SceneHandler::GetSceneNode(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::SceneNode* node = (entity != entt::null) ? registry->try_get<ECS::Components::SceneNode>(entity) : nullptr;
        if (!node)
        {
            zenith->Push();
            return 1;
        }

        zenith->CreateTable();

        ECS::Components::Transform* transform = registry->try_get<ECS::Components::Transform>(entity);
        ECS::Components::Transform* parentTransform = transform ? transform->GetParentTransform() : nullptr;
        if (parentTransform && parentTransform->ownerNode)
        {
            entt::entity parentEntity = parentTransform->ownerNode->GetOwnerEntity();
            ECS::Components::Name* parentName = registry->try_get<ECS::Components::Name>(parentEntity);

            zenith->CreateTable();
            zenith->AddTableField("id", entt::to_integral(parentEntity));
            zenith->AddTableField("name", parentName ? parentName->name.c_str() : "");
            zenith->SetTableKey("parent");
        }

        zenith->CreateTable();
        i32 childIndex = 0;
        ECS::TransformSystem::Get(*registry).IterateChildren(entity, [&](ECS::Components::SceneNode* child)
        {
            if (!child)
                return;

            entt::entity childEntity = child->GetOwnerEntity();
            if (childEntity == entt::null)
                return;

            ECS::Components::Name* childName = registry->try_get<ECS::Components::Name>(childEntity);

            zenith->CreateTable();
            zenith->AddTableField("id", entt::to_integral(childEntity));
            zenith->AddTableField("name", childName ? childName->name.c_str() : "");
            zenith->SetTableKey(++childIndex);
        });
        zenith->SetTableKey("children");

        return 1;
    }

    i32 SceneHandler::GetAABB(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::AABB* aabb = (entity != entt::null) ? registry->try_get<ECS::Components::AABB>(entity) : nullptr;
        if (!aabb)
        {
            zenith->Push();
            return 1;
        }

        zenith->CreateTable();
        zenith->AddTableField("center", aabb->centerPos);
        zenith->AddTableField("extents", aabb->extents);
        return 1;
    }

    i32 SceneHandler::SetAABB(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::AABB* aabb = (entity != entt::null) ? registry->try_get<ECS::Components::AABB>(entity) : nullptr;
        if (!aabb)
            return 0;

        aabb->centerPos = zenith->CheckVal<vec3>(2);
        aabb->extents = zenith->CheckVal<vec3>(3);

        registry->emplace_or_replace<ECS::Components::DirtyAABB>(entity);
        return 0;
    }

    i32 SceneHandler::GetModel(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Model* model = (entity != entt::null) ? registry->try_get<ECS::Components::Model>(entity) : nullptr;
        if (!model)
        {
            zenith->Push();
            return 1;
        }

        zenith->CreateTable();
        zenith->AddTableField("modelID", model->modelID);
        zenith->AddTableField("modelHash", model->modelHash);
        zenith->AddTableField("instanceID", model->instanceID);
        zenith->AddTableField("visible", static_cast<bool>(model->flags.visible));
        zenith->AddTableField("forcedTransparency", static_cast<bool>(model->flags.forcedTransparency));
        zenith->AddTableField("opacity", model->opacity);
        return 1;
    }

    i32 SceneHandler::SetModelVisible(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Model* model = (entity != entt::null) ? registry->try_get<ECS::Components::Model>(entity) : nullptr;
        if (!model)
            return 0;

        bool visible = zenith->CheckVal<bool>(2);
        model->flags.visible = visible;
        ServiceLocator::GetGameRenderer()->GetModelLoader()->SetModelVisible(*model, visible);
        return 0;
    }

    i32 SceneHandler::SetModelTransparent(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Model* model = (entity != entt::null) ? registry->try_get<ECS::Components::Model>(entity) : nullptr;
        if (!model)
            return 0;

        bool forcedTransparency = zenith->CheckVal<bool>(2);
        f32 opacity = zenith->CheckVal<f32>(3);
        model->flags.forcedTransparency = forcedTransparency;
        model->opacity = opacity;
        ServiceLocator::GetGameRenderer()->GetModelLoader()->SetModelTransparent(*model, forcedTransparency, opacity);
        return 0;
    }

    i32 SceneHandler::GetDecal(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Decal* decal = (entity != entt::null) ? registry->try_get<ECS::Components::Decal>(entity) : nullptr;
        if (!decal)
        {
            zenith->Push();
            return 1;
        }

        zenith->CreateTable();
        zenith->AddTableField("texturePath", decal->texturePath.c_str());
        zenith->AddTableField("color", vec3(decal->colorMultiplier.r, decal->colorMultiplier.g, decal->colorMultiplier.b));
        zenith->AddTableField("thresholdMin", f32(decal->thresholdMinMax.x));
        zenith->AddTableField("thresholdMax", f32(decal->thresholdMinMax.y));
        zenith->AddTableField("minU", f32(decal->minUV.x));
        zenith->AddTableField("minV", f32(decal->minUV.y));
        zenith->AddTableField("maxU", f32(decal->maxUV.x));
        zenith->AddTableField("maxV", f32(decal->maxUV.y));
        zenith->AddTableField("flags", decal->flags);
        return 1;
    }

    i32 SceneHandler::SetDecal(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Decal* decal = (entity != entt::null) ? registry->try_get<ECS::Components::Decal>(entity) : nullptr;
        if (!decal || !zenith->IsTable(2))
            return 0;

        if (zenith->GetTableField("texturePath", 2))
        {
            const char* texturePath = zenith->Get<const char*>(-1);
            if (texturePath)
                decal->texturePath = texturePath;
        }
        zenith->Pop();

        if (zenith->GetTableField("color", 2))
        {
            vec3 color = zenith->Get<vec3>(-1);
            decal->colorMultiplier = Color(color.r, color.g, color.b, decal->colorMultiplier.a);
        }
        zenith->Pop();

        vec2 thresholds = vec2(f32(decal->thresholdMinMax.x), f32(decal->thresholdMinMax.y));
        if (zenith->GetTableField("thresholdMin", 2))
            thresholds.x = zenith->Get<f32>(-1);
        zenith->Pop();
        if (zenith->GetTableField("thresholdMax", 2))
            thresholds.y = zenith->Get<f32>(-1);
        zenith->Pop();
        decal->thresholdMinMax = hvec2(thresholds);

        vec2 minUV = vec2(f32(decal->minUV.x), f32(decal->minUV.y));
        if (zenith->GetTableField("minU", 2))
            minUV.x = zenith->Get<f32>(-1);
        zenith->Pop();
        if (zenith->GetTableField("minV", 2))
            minUV.y = zenith->Get<f32>(-1);
        zenith->Pop();
        decal->minUV = hvec2(minUV);

        vec2 maxUV = vec2(f32(decal->maxUV.x), f32(decal->maxUV.y));
        if (zenith->GetTableField("maxU", 2))
            maxUV.x = zenith->Get<f32>(-1);
        zenith->Pop();
        if (zenith->GetTableField("maxV", 2))
            maxUV.y = zenith->Get<f32>(-1);
        zenith->Pop();
        decal->maxUV = hvec2(maxUV);

        if (zenith->GetTableField("flags", 2))
            decal->flags = zenith->Get<u32>(-1);
        zenith->Pop();

        registry->emplace_or_replace<ECS::Components::DirtyDecal>(entity);
        return 0;
    }

    i32 SceneHandler::GetUnit(Zenith* zenith)
    {
        entt::registry* registry = GetGameRegistry();
        entt::entity entity = GetEntityArg(zenith, registry, 1);
        ECS::Components::Unit* unit = (entity != entt::null) ? registry->try_get<ECS::Components::Unit>(entity) : nullptr;
        if (!unit)
        {
            zenith->Push();
            return 1;
        }

        zenith->CreateTable();
        zenith->AddTableField("bodyID", unit->bodyID);
        return 1;
    }
}
