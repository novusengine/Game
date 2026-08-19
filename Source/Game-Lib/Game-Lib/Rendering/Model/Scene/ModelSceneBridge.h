#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <entt/entt.hpp>

#include <robinhood/robinhood.h>

namespace RenderScenes
{
    struct ModelRenderDescription;
    class RenderScene;
}

namespace ModelScene
{
    // Owns the CPU-side mapping between gameplay entities and generation-checked RenderScene model instances.
    // It keeps renderer instance state synchronized with entity lifetime and component changes.
    class ModelSceneBridge
    {
      public:
        explicit ModelSceneBridge(RenderScenes::RenderScene* scene) : _scene(scene) { }

        RenderScenes::ModelInstanceHandle Add(entt::entity entity, RenderAssets::ModelHandle model,
                                               const mat4x4& transform, bool visible = true);
        RenderScenes::ModelInstanceHandle AddToScene(entt::entity entity, RenderScenes::RenderScene* scene,
                                                      RenderAssets::ModelHandle model, const mat4x4& transform,
                                                      bool visible = true);
        bool Remove(entt::entity entity);
        bool SetTransform(entt::entity entity, const mat4x4& transform, bool teleported = false);
        bool SetVisible(entt::entity entity, bool visible);
        bool SetHighlight(entt::entity entity, f32 intensity, u32 packedColor = 0xFFFFFFFFu);
        bool SetOpacity(entt::entity entity, f32 opacity, bool forceTransparent = false);
        bool SetCastsShadows(entt::entity entity, bool castsShadows);
        bool SetMaterial(entt::entity entity, u32 slot, RenderAssets::MaterialInstanceHandle material);
        bool ResetMaterials(entt::entity entity);
        bool SetGeometryGroupEnabled(entt::entity entity, u32 groupID, bool enabled);
        bool SetGeometryGroupRangeEnabled(entt::entity entity, u32 firstGroupID, u32 lastGroupID, bool enabled);
        bool SetAllGeometryGroups(entt::entity entity, bool enabled);
        void SyncTransforms(entt::registry& registry);
        RenderScenes::ModelInstanceHandle Get(entt::entity entity) const;
        RenderScenes::RenderScene* GetScene(entt::entity entity) const;
        bool Describe(entt::entity entity, RenderScenes::ModelRenderDescription& description) const;
        entt::entity GetEntity(RenderScenes::ModelInstanceHandle handle) const;

      private:
        struct Binding
        {
            RenderScenes::RenderScene* scene = nullptr;
            RenderScenes::ModelInstanceHandle handle = RenderScenes::InvalidModelInstanceHandle();
        };

        RenderScenes::RenderScene* _scene = nullptr;
        robin_hood::unordered_map<entt::entity, Binding> _instances;
    };
} // namespace ModelScene
