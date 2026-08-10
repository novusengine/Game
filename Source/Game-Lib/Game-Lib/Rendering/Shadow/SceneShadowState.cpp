#include "SceneShadowState.h"

#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <algorithm>

namespace ShadowRendering
{
    namespace
    {
        constexpr u32 DYNAMIC_CASTER_QUIET_FRAMES = 3;

        u64 HandleKey(RenderScenes::ModelInstanceHandle handle)
        {
            return static_cast<RenderScenes::ModelInstanceHandle::type>(handle);
        }
    }

    void SceneShadowState::TransformBounds(const FileFormat::Model::Bounds& bounds, const mat4x4& transform,
                                           vec3& outMin, vec3& outMax)
    {
        const vec3 center = vec3(transform * vec4(bounds.center, 1.0f));
        mat3x3 absoluteTransform(transform);
        absoluteTransform[0] = abs(absoluteTransform[0]);
        absoluteTransform[1] = abs(absoluteTransform[1]);
        absoluteTransform[2] = abs(absoluteTransform[2]);
        const vec3 extents = absoluteTransform * bounds.extents;
        outMin = center - extents;
        outMax = center + extents;
    }

    void SceneShadowState::QueueInvalidation(const FileFormat::Model::Bounds& bounds, const mat4x4& transform)
    {
        vec3 min;
        vec3 max;
        TransformBounds(bounds, transform, min, max);
        _invalidations.emplace_back(min, 0.0f);
        _invalidations.emplace_back(max, 0.0f);
    }

    void SceneShadowState::ModelCreated(RenderScenes::ModelInstanceHandle, const FileFormat::Model::Bounds& bounds,
                                        const mat4x4& transform, bool visible)
    {
        if (visible)
            QueueInvalidation(bounds, transform);
    }

    void SceneShadowState::ModelDestroyed(RenderScenes::ModelInstanceHandle handle,
                                          const FileFormat::Model::Bounds& bounds, const mat4x4& transform,
                                          bool visible)
    {
        if (visible)
            QueueInvalidation(bounds, transform);
        if (_dynamicCasters.erase(HandleKey(handle)) != 0)
            ++_transitionsOut;
        RebuildDynamicAABBs();
    }

    bool SceneShadowState::ModelTransformChanged(RenderScenes::ModelInstanceHandle handle,
                                                 const FileFormat::Model::Bounds& bounds,
                                                 const mat4x4& oldTransform, const mat4x4& newTransform, bool visible)
    {
        if (!visible)
            return false;

        const u64 key = HandleKey(handle);
        auto [it, inserted] = _dynamicCasters.try_emplace(key);
        if (inserted)
        {
            QueueInvalidation(bounds, oldTransform);
            ++_transitionsIn;
        }
        TransformBounds(bounds, newTransform, it->second.min, it->second.max);
        it->second.quietFrames = 0;
        RebuildDynamicAABBs();
        return inserted;
    }

    bool SceneShadowState::ModelVisibilityChanged(RenderScenes::ModelInstanceHandle handle,
                                                  const FileFormat::Model::Bounds& bounds,
                                                  const mat4x4& transform, bool oldVisible, bool newVisible)
    {
        if (oldVisible == newVisible)
            return false;
        QueueInvalidation(bounds, transform);
        if (!newVisible && _dynamicCasters.erase(HandleKey(handle)) != 0)
        {
            ++_transitionsOut;
            RebuildDynamicAABBs();
            return true;
        }
        return false;
    }

    void SceneShadowState::ModelAppearanceChanged(const FileFormat::Model::Bounds& bounds, const mat4x4& transform,
                                                  bool visible)
    {
        if (visible)
            QueueInvalidation(bounds, transform);
    }

    void SceneShadowState::AdvanceFrame(RenderScenes::RenderScene& scene)
    {
        _retiredCasterKeys.clear();
        for (auto& [key, caster] : _dynamicCasters)
            if (++caster.quietFrames >= DYNAMIC_CASTER_QUIET_FRAMES)
                _retiredCasterKeys.push_back(key);

        for (u64 key : _retiredCasterKeys)
        {
            const RenderScenes::ModelInstanceHandle handle(key);
            const DynamicCaster caster = _dynamicCasters.at(key);
            _invalidations.emplace_back(caster.min, 0.0f);
            _invalidations.emplace_back(caster.max, 0.0f);
            scene.SetModelShadowDynamic(handle, false);
            _dynamicCasters.erase(key);
            ++_transitionsOut;
        }
        if (!_retiredCasterKeys.empty())
            RebuildDynamicAABBs();
    }

    u32 SceneShadowState::DrainInvalidations(std::vector<vec4>& outMinMaxPairs, u32 maxPairs)
    {
        const u32 pairCount = static_cast<u32>(_invalidations.size() / 2);
        const u32 copyPairs = std::min(pairCount, maxPairs);
        outMinMaxPairs.insert(outMinMaxPairs.end(), _invalidations.begin(),
                              _invalidations.begin() + static_cast<size_t>(copyPairs) * 2);
        _invalidations.clear();
        return pairCount;
    }

    SceneShadowStats SceneShadowState::GetStats() const
    {
        return {.dynamicCasters = static_cast<u32>(_dynamicCasters.size()),
                .transitionsIn = _transitionsIn,
                .transitionsOut = _transitionsOut};
    }

    void SceneShadowState::RebuildDynamicAABBs()
    {
        _dynamicAABBs.clear();
        _dynamicAABBs.reserve(_dynamicCasters.size() * 2);
        for (const auto& [key, caster] : _dynamicCasters)
        {
            _dynamicAABBs.emplace_back(caster.min, 0.0f);
            _dynamicAABBs.emplace_back(caster.max, 0.0f);
        }
    }
} // namespace ShadowRendering
