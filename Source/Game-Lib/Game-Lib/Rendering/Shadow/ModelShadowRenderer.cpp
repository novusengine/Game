#include "ModelShadowRenderer.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <algorithm>
#include <limits>

namespace
{
    constexpr i32 DEFAULT_MODEL_SHADOW_QUEUE_CAPACITY = 1024 * 1024;
}

AutoCVar_Int CVAR_ModelShadowMeshletQueueCapacity(
    CVarCategory::Client | CVarCategory::Rendering, "modelShadowMeshletQueueCapacity",
    "Maximum allocated model meshlet entries shared by all SVSM clipmaps", DEFAULT_MODEL_SHADOW_QUEUE_CAPACITY);
AutoCVar_Int CVAR_ModelShadowForceLOD(CVarCategory::Client | CVarCategory::Rendering, "modelShadowForceLOD",
                                     "Force model shadow LOD (-1 automatic, values clamp to each Mesh)", 0);
AutoCVar_Float CVAR_ModelShadowLODTargetTexels(
    CVarCategory::Client | CVarCategory::Rendering, "modelShadowLODTargetTexels",
    "Maximum projected geometric error in SVSM texels before selecting a finer model LOD", 1.0f);
// TODO: Remove this comparison control after the Phase 15 opacity-shadow verification.
AutoCVar_Int CVAR_ModelShadowOpacityDither(
    CVarCategory::Client | CVarCategory::Rendering, "modelShadowOpacityDither",
    "Fade model shadows using stable shadow-texel dithering", 1, CVarFlags::EditCheckbox);

namespace ShadowRendering
{
    ModelShadowRenderer::ModelShadowRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                             RenderAssets::RenderAssetResources* assets,
                                             RenderScenes::RenderScene* scene)
        : _renderer(renderer), _assets(assets), _scene(scene), _work(renderer),
          _pass(renderer, gameRenderer)
    {
    }

    void ModelShadowRenderer::Update(u32 numClipmaps)
    {
        const u64 revision = _scene->GetModelInstances().GetMembershipRevision();
        if (revision == _preparedMembershipRevision && numClipmaps == _numClipmaps)
            return;

        _preparedMembershipRevision = revision;
        _numClipmaps = numClipmaps;
        const u64 required = 1 + _scene->GetModelInstances().GetActiveMeshletCount() * numClipmaps;
        const u32 configured = static_cast<u32>(std::max(CVAR_ModelShadowMeshletQueueCapacity.Get(), 1));
        _work.EnsureCapacity(static_cast<u32>(std::min<u64>(required, configured)));
    }

    void ModelShadowRenderer::Upload(Renderer::BufferID svsmData, Renderer::BufferID staticPageTable,
                                     Renderer::BufferID dynamicPageTable)
    {
        const ModelLoading::ModelGeometryStorage& geometry = _assets->GetModelGeometryStorage();
        _descriptorsReady = _pass.Upload(_work, geometry, *_scene, svsmData, staticPageTable, dynamicPageTable);
    }

    void ModelShadowRenderer::AddPasses(Renderer::RenderGraph* renderGraph, RenderResources& resources,
                                        Renderer::BufferID svsmData, Renderer::BufferID staticPageTable,
                                        Renderer::BufferID dynamicPageTable, Renderer::ImageID staticPagePool,
                                        Renderer::ImageID dynamicPagePool, u32 numClipmaps, u32 virtualSize,
                                        bool dynamicSplit, u8 frameIndex)
    {
        if (!_descriptorsReady || _work.GetCapacity() == 0)
            return;
        _work.ReadbackStats(frameIndex);
        const ModelLoading::ModelGeometryStorage& geometry = _assets->GetModelGeometryStorage();
        const MaterialLoading::MaterialStorage& materials = _assets->GetMaterialStorage();
        _pass.AddCullPass(renderGraph, resources, _work, geometry, materials, *_scene, svsmData, numClipmaps,
                          dynamicSplit, frameIndex, std::max(CVAR_ModelShadowForceLOD.Get(), -1),
                          std::max(CVAR_ModelShadowLODTargetTexels.GetFloat(), 0.01f));
        _pass.AddRasterPass(renderGraph, resources, _work, geometry, materials, *_scene, svsmData, staticPageTable,
                            dynamicPageTable, staticPagePool, dynamicPagePool, virtualSize, dynamicSplit,
                            CVAR_ModelShadowOpacityDither.Get() != 0, frameIndex);
    }
} // namespace ShadowRendering
