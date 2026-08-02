#pragma once

#include "Game-Lib/Rendering/Model/View/ModelViewState.h"

#include <vector>

namespace MaterialLoading
{
    class MaterialStorage;
}

namespace ModelLoading
{
    class ModelGeometryStorage;
}

namespace RenderScenes
{
    class RenderScene;
}

namespace ModelPipeline
{
    struct DiagnosticWorkBuildResult
    {
        std::vector<ModelView::DiagnosticMeshletWork> oneSided;
        std::vector<ModelView::DiagnosticMeshletWork> twoSided;
        ModelView::DiagnosticWorkStats stats;
    };

    // Builds CPU-side diagnostic meshlet work from selected Scene instances and immutable asset records.
    // It provides a small trusted queue for validating decode and raster behavior before GPU culling exists.
    class ModelDiagnosticWorkBuilder
    {
      public:
        static DiagnosticWorkBuildResult Build(const RenderScenes::RenderScene& scene,
                                               const ModelLoading::ModelGeometryStorage& geometry,
                                               const MaterialLoading::MaterialStorage& materials,
                                               std::span<const RenderScenes::ModelInstanceHandle> selection);
    };
} // namespace ModelPipeline
