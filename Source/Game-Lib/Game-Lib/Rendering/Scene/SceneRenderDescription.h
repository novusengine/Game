#pragma once

#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"

#include <Base/Types.h>

#include <vector>

namespace RenderScenes
{
    // Describes one CPU-side model placement using shareable asset handles and fully resolved appearance state.
    // It can be instantiated independently into any RenderScene without retaining a source Scene instance.
    struct ModelRenderDescription
    {
        RenderAssets::ModelHandle model;
        mat4x4 transform = mat4x4(1.0f);
        std::vector<RenderAssets::MaterialInstanceHandle> materials;
        std::vector<u32> enabledGeometryGroups;
        f32 opacity = 1.0f;
        f32 highlightIntensity = 0.0f;
        u32 packedHighlightColor = 0xFFFFFFFFu;
        bool visible = true;
        bool castsShadows = true;
    };

    // Stores registry-independent CPU-side content ready to instantiate into a RenderScene.
    // Bounds provide stable framing for retained previews without querying another Scene.
    struct SceneRenderDescription
    {
        std::vector<ModelRenderDescription> models;
        vec3 boundsCenter = vec3(0.0f);
        f32 boundsRadius = 1.0f;
        u64 revision = 0;
    };
} // namespace RenderScenes
