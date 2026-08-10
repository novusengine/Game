#pragma once

#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"

#include <FileFormat/Novus/Model/Model.h>

#include <robinhood/robinhood.h>

#include <span>
#include <vector>

namespace RenderScenes
{
    class RenderScene;
}

namespace ShadowRendering
{
    struct SceneShadowStats
    {
        u32 dynamicCasters = 0;
        u32 transitionsIn = 0;
        u32 transitionsOut = 0;
    };

    // Owns CPU-side shadow invalidations and short-lived dynamic-caster classifications for one Scene.
    // Cached static pages use the invalidations while transient shadow pages follow moving caster bounds.
    class SceneShadowState
    {
      public:
        void ModelCreated(RenderScenes::ModelInstanceHandle handle, const FileFormat::Model::Bounds& bounds,
                          const mat4x4& transform, bool visible);
        void ModelDestroyed(RenderScenes::ModelInstanceHandle handle, const FileFormat::Model::Bounds& bounds,
                            const mat4x4& transform, bool visible);
        bool ModelTransformChanged(RenderScenes::ModelInstanceHandle handle, const FileFormat::Model::Bounds& bounds,
                                   const mat4x4& oldTransform, const mat4x4& newTransform, bool visible);
        bool ModelVisibilityChanged(RenderScenes::ModelInstanceHandle handle, const FileFormat::Model::Bounds& bounds,
                                    const mat4x4& transform, bool oldVisible, bool newVisible);
        void ModelAppearanceChanged(const FileFormat::Model::Bounds& bounds, const mat4x4& transform, bool visible);
        void AdvanceFrame(RenderScenes::RenderScene& scene);

        u32 DrainInvalidations(std::vector<vec4>& outMinMaxPairs, u32 maxPairs);
        std::span<const vec4> GetDynamicAABBs() const { return _dynamicAABBs; }
        SceneShadowStats GetStats() const;

      private:
        struct DynamicCaster
        {
            vec3 min = {};
            vec3 max = {};
            u32 quietFrames = 0;
        };

        static void TransformBounds(const FileFormat::Model::Bounds& bounds, const mat4x4& transform, vec3& outMin,
                                    vec3& outMax);
        void QueueInvalidation(const FileFormat::Model::Bounds& bounds, const mat4x4& transform);
        void RebuildDynamicAABBs();

        robin_hood::unordered_flat_map<u64, DynamicCaster> _dynamicCasters;
        std::vector<u64> _retiredCasterKeys;
        std::vector<vec4> _invalidations;
        std::vector<vec4> _dynamicAABBs;
        u32 _transitionsIn = 0;
        u32 _transitionsOut = 0;
    };
} // namespace ShadowRendering
