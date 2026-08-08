#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <entt/entt.hpp>

#include <robinhood/robinhood.h>

namespace RenderScenes
{
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
        bool Remove(entt::entity entity, u64 retireValue);
        bool SetTransform(entt::entity entity, const mat4x4& transform, bool teleported = false);
        bool SetVisible(entt::entity entity, bool visible);
        bool SetGeometryGroupEnabled(entt::entity entity, u32 groupID, bool enabled);
        void SyncTransforms(entt::registry& registry);
        RenderScenes::ModelInstanceHandle Get(entt::entity entity) const;

      private:
        RenderScenes::RenderScene* _scene = nullptr;
        robin_hood::unordered_map<entt::entity, RenderScenes::ModelInstanceHandle> _instances;
    };
} // namespace ModelScene
