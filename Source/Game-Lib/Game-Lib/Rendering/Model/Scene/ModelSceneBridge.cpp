#include "ModelSceneBridge.h"

#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/ECS/Util/Transforms.h"

#include <entt/entt.hpp>

namespace ModelScene
{
    RenderScenes::ModelInstanceHandle ModelSceneBridge::Add(entt::entity entity, RenderAssets::ModelHandle model,
                                                            const mat4x4& transform, bool visible)
    {
        return AddToScene(entity, _scene, model, transform, visible);
    }

    RenderScenes::ModelInstanceHandle ModelSceneBridge::AddToScene(entt::entity entity,
                                                                   RenderScenes::RenderScene* scene,
                                                                   RenderAssets::ModelHandle model,
                                                                   const mat4x4& transform, bool visible)
    {
        if (!scene || entity == entt::null || _instances.contains(entity))
            return RenderScenes::InvalidModelInstanceHandle();

        RenderScenes::ModelInstanceDesc desc;
        desc.model = model;
        desc.worldTransform = transform;
        desc.visible = visible;
        const RenderScenes::ModelInstanceHandle handle = scene->CreateModelInstance(desc);
        if (RenderScenes::GetModelInstanceSlot(handle) != RenderScenes::INVALID_SCENE_INDEX)
            _instances[entity] = Binding{scene, handle};
        return handle;
    }

    bool ModelSceneBridge::Remove(entt::entity entity)
    {
        const auto existing = _instances.find(entity);
        if (existing == _instances.end() || !existing->second.scene)
            return false;

        const bool removed = existing->second.scene->DestroyModelInstance(existing->second.handle);
        if (removed)
        {
            _instances.erase(existing);
        }
        return removed;
    }

    bool ModelSceneBridge::SetTransform(entt::entity entity, const mat4x4& transform, bool teleported)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene && existing->second.scene->SetModelTransform(existing->second.handle, transform, teleported);
    }

    bool ModelSceneBridge::SetVisible(entt::entity entity, bool visible)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene && existing->second.scene->SetModelVisible(existing->second.handle, visible);
    }

    bool ModelSceneBridge::SetHighlight(entt::entity entity, f32 intensity, u32 packedColor)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene &&
               existing->second.scene->SetModelHighlight(existing->second.handle, intensity, packedColor);
    }

    bool ModelSceneBridge::SetCastsShadows(entt::entity entity, bool castsShadows)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene &&
               existing->second.scene->SetModelCastsShadows(existing->second.handle, castsShadows);
    }

    bool ModelSceneBridge::SetMaterial(entt::entity entity, u32 slot,
                                       RenderAssets::MaterialInstanceHandle material)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene &&
               existing->second.scene->SetModelMaterial(existing->second.handle, slot, material);
    }

    bool ModelSceneBridge::ResetMaterials(entt::entity entity)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene &&
               existing->second.scene->ResetModelMaterials(existing->second.handle);
    }

    bool ModelSceneBridge::SetOpacity(entt::entity entity, f32 opacity, bool forceTransparent)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene &&
               existing->second.scene->SetModelOpacity(existing->second.handle, opacity, forceTransparent);
    }

    bool ModelSceneBridge::SetGeometryGroupEnabled(entt::entity entity, u32 groupID, bool enabled)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene && existing->second.scene->SetGeometryGroupEnabled(existing->second.handle, groupID, enabled);
    }

    bool ModelSceneBridge::SetGeometryGroupRangeEnabled(entt::entity entity, u32 firstGroupID, u32 lastGroupID, bool enabled)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene && existing->second.scene->SetGeometryGroupRangeEnabled(existing->second.handle, firstGroupID, lastGroupID, enabled);
    }

    bool ModelSceneBridge::SetAllGeometryGroups(entt::entity entity, bool enabled)
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene && existing->second.scene->SetAllGeometryGroups(existing->second.handle, enabled);
    }

    void ModelSceneBridge::SyncTransforms(entt::registry& registry)
    {
        registry.view<ECS::Components::Transform, ECS::Components::DirtyTransform>().each(
            [this](entt::entity entity, const ECS::Components::Transform& transform,
                   const ECS::Components::DirtyTransform&) {
                const auto existing = _instances.find(entity);
                if (existing != _instances.end() && existing->second.scene)
                    existing->second.scene->SetModelTransform(existing->second.handle, transform.GetMatrix());
            });
    }

    RenderScenes::ModelInstanceHandle ModelSceneBridge::Get(entt::entity entity) const
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() ? existing->second.handle : RenderScenes::InvalidModelInstanceHandle();
    }

    RenderScenes::RenderScene* ModelSceneBridge::GetScene(entt::entity entity) const
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() ? existing->second.scene : nullptr;
    }

    bool ModelSceneBridge::Describe(entt::entity entity, RenderScenes::ModelRenderDescription& description) const
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() && existing->second.scene && existing->second.scene->DescribeModelInstance(existing->second.handle, description);
    }

    entt::entity ModelSceneBridge::GetEntity(RenderScenes::ModelInstanceHandle handle) const
    {
        for (const auto& [entity, binding] : _instances)
        {
            if (binding.handle == handle)
                return entity;
        }
        return entt::null;
    }
} // namespace ModelScene
