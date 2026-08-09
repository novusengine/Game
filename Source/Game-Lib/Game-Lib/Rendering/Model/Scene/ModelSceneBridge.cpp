#include "ModelSceneBridge.h"

#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/ECS/Util/Transforms.h"

#include <entt/entt.hpp>

namespace ModelScene
{
    RenderScenes::ModelInstanceHandle ModelSceneBridge::Add(entt::entity entity, RenderAssets::ModelHandle model,
                                                            const mat4x4& transform, bool visible)
    {
        if (!_scene || entity == entt::null || _instances.contains(entity))
            return RenderScenes::InvalidModelInstanceHandle();

        RenderScenes::ModelInstanceDesc desc;
        desc.model = model;
        desc.worldTransform = transform;
        desc.visible = visible;
        const RenderScenes::ModelInstanceHandle handle = _scene->CreateModelInstance(desc);
        if (RenderScenes::GetModelInstanceSlot(handle) != RenderScenes::INVALID_SCENE_INDEX)
            _instances[entity] = handle;
        return handle;
    }

    bool ModelSceneBridge::Remove(entt::entity entity, u64 retireValue)
    {
        const auto existing = _instances.find(entity);
        if (!_scene || existing == _instances.end())
            return false;

        const bool removed = _scene->DestroyModelInstance(existing->second, retireValue);
        if (removed)
            _instances.erase(existing);
        return removed;
    }

    bool ModelSceneBridge::SetTransform(entt::entity entity, const mat4x4& transform, bool teleported)
    {
        const auto existing = _instances.find(entity);
        return _scene && existing != _instances.end() && _scene->SetModelTransform(existing->second, transform, teleported);
    }

    bool ModelSceneBridge::SetVisible(entt::entity entity, bool visible)
    {
        const auto existing = _instances.find(entity);
        return _scene && existing != _instances.end() && _scene->SetModelVisible(existing->second, visible);
    }

    bool ModelSceneBridge::SetGeometryGroupEnabled(entt::entity entity, u32 groupID, bool enabled)
    {
        const auto existing = _instances.find(entity);
        return _scene && existing != _instances.end() && _scene->SetGeometryGroupEnabled(existing->second, groupID, enabled);
    }

    bool ModelSceneBridge::SetAllGeometryGroups(entt::entity entity, bool enabled)
    {
        const auto existing = _instances.find(entity);
        return _scene && existing != _instances.end() && _scene->SetAllGeometryGroups(existing->second, enabled);
    }

    void ModelSceneBridge::SyncTransforms(entt::registry& registry)
    {
        registry.view<ECS::Components::Transform, ECS::Components::DirtyTransform>().each(
            [this](entt::entity entity, const ECS::Components::Transform& transform,
                   const ECS::Components::DirtyTransform&) {
                const auto existing = _instances.find(entity);
                if (_scene && existing != _instances.end())
                    _scene->SetModelTransform(existing->second, transform.GetMatrix());
            });
    }

    RenderScenes::ModelInstanceHandle ModelSceneBridge::Get(entt::entity entity) const
    {
        const auto existing = _instances.find(entity);
        return existing != _instances.end() ? existing->second : RenderScenes::InvalidModelInstanceHandle();
    }
} // namespace ModelScene
