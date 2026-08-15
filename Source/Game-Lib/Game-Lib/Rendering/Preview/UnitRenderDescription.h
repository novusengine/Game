#pragma once

#include "Game-Lib/Rendering/Scene/SceneRenderDescription.h"

namespace PreviewRendering
{
    // Stores a registry-independent CPU-side unit appearance assembled from customization and equipment state.
    // The resolved content can be instantiated independently into world, inspection, or editor Scenes.
    struct UnitRenderDescription
    {
        RenderScenes::SceneRenderDescription scene;
    };
} // namespace PreviewRendering
