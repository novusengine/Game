#include "ModelRenderSystem.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelDiagnosticWorkBuilder.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <algorithm>

namespace ModelRendering
{
    ModelRenderSystem::ModelRenderSystem(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                         RenderAssets::RenderAssetResources* assets, RenderScenes::RenderScene* scene,
                                         RenderResources& resources, bool validateTransfers)
        : _renderer(renderer), _assets(assets), _scene(scene),
          _mainView(RenderScenes::RenderViewDesc{
              .viewID = 1,
              .scene = scene,
              .cameraIndex = 0,
              .colorTarget = resources.sceneColor,
              .depthTarget = resources.depth,
              .lifetime = RenderScenes::RenderViewLifetime::Persistent }),
          _mainViewState(validateTransfers), _diagnosticPass(renderer, gameRenderer)
    {
    }

    void ModelRenderSystem::Update()
    {
        if (!_mainViewState.IsWorkDirty())
            return;

        ModelPipeline::DiagnosticWorkBuildResult work = ModelPipeline::ModelDiagnosticWorkBuilder::Build(
            *_scene, _assets->GetGeometryStorage(), _assets->GetMaterialStorage(),
            _mainViewState.GetDiagnosticSelection());
        _mainViewState.SetDiagnosticWork(work.oneSided, work.twoSided, work.stats);
    }

    void ModelRenderSystem::Upload()
    {
        _diagnosticPass.Upload(_mainViewState, _assets->GetGeometryStorage(), *_scene);
    }

    void ModelRenderSystem::AddPasses(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
    {
        _diagnosticPass.AddPass(renderGraph, resources, _mainView, _mainViewState,
                                _assets->GetGeometryStorage(), *_scene, frameIndex);
    }

    RenderScenes::ModelInstanceHandle ModelRenderSystem::SetDiagnosticModel(RenderAssets::ModelHandle model,
                                                                            const vec3& worldBoundsCenter,
                                                                            f32 worldBoundsRadius)
    {
        const ModelLoading::ModelGeometryStorage& geometry = _assets->GetGeometryStorage();
        if (!geometry.HasModel(model) || worldBoundsRadius <= 0.0f)
            return RenderScenes::InvalidModelInstanceHandle();

        if (_scene->IsAlive(_diagnosticInstance) || _scene->IsPending(_diagnosticInstance))
        {
            _scene->DestroyModelInstance(_diagnosticInstance, 0);
            _scene->ReleaseRetiredHistory(0);
        }

        const FileFormat::Model::Bounds& bounds = geometry.GetRecord(model).bounds;
        const f32 scale = worldBoundsRadius / std::max(bounds.sphereRadius, 0.000001f);
        mat4x4 transform(scale);
        transform[3] = vec4(worldBoundsCenter - bounds.center * scale, 1.0f);

        RenderScenes::ModelInstanceDesc desc;
        desc.model = model;
        desc.worldTransform = transform;
        _diagnosticInstance = _scene->CreateModelInstance(desc);
        if (!_scene->IsPending(_diagnosticInstance))
            return RenderScenes::InvalidModelInstanceHandle();

        // No temporal View buffers exist during diagnostic bring-up, so acknowledging these ranges publishes
        // the instance after its complete record is uploaded and before it can be consumed by this pass.
        _scene->AcknowledgeClearsAndPublish();
        _mainViewState.SetDiagnosticSelection(_diagnosticInstance);
        return _diagnosticInstance;
    }
} // namespace ModelRendering
