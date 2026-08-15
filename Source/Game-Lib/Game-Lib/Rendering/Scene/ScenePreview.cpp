#include "ScenePreview.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/Scene/OffscreenRenderView.h"
#include "Game-Lib/Rendering/Scene/OrbitCamera.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"
#include "Game-Lib/Rendering/Scene/SceneRenderDescription.h"

#include <algorithm>
#include <utility>

namespace RenderScenes
{
    ScenePreview::ScenePreview(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                               RenderAssets::RenderAssetResources* assets, RenderResources& resources,
                               std::string debugName, u64 sceneID, bool validateTransfers)
        : _renderer(renderer), _assets(assets), _resources(&resources), _validateTransfers(validateTransfers)
    {
        _scene = std::make_unique<RenderScene>(sceneID, &_assets->GetModelGeometryStorage(),
                                              &_assets->GetMaterialStorage(), _validateTransfers);
        RenderViewDesc viewDesc;
        viewDesc.debugName = std::move(debugName);
        viewDesc.scene = _scene.get();
        viewDesc.cameraIndex = INVALID_RENDER_VIEW_CAMERA;
        viewDesc.passFamilies = RenderViewPassFamily::Models;
        viewDesc.lifetime = RenderViewLifetime::Transient;
        viewDesc.refresh = RenderViewRefresh::Retained;
        viewDesc.clearTargets = true;
        _renderView = std::make_unique<OffscreenRenderView>(renderer, gameRenderer, std::move(viewDesc));
    }

    ScenePreview::~ScenePreview() = default;

    bool ScenePreview::SetTarget(Renderer::TextureID target)
    {
        if (!_renderView->SetTarget(target))
            return false;
        if (!_camera)
            _camera = std::make_unique<OrbitCamera>(*_resources, *_renderView->GetView());
        return true;
    }

    bool ScenePreview::SetContent(const SceneRenderDescription& description)
    {
        if (!_camera || !_renderView->GetView())
            return false;
        if (_contentRevision == description.revision && !_instances.empty())
            return true;

        Clear();
        for (const ModelRenderDescription& model : description.models)
        {
            ModelInstanceDesc desc;
            desc.model = model.model;
            desc.worldTransform = model.transform;
            desc.visible = model.visible;
            const ModelInstanceHandle instance = _scene->CreateModelInstance(desc);
            if (!_scene->IsPending(instance))
                continue;
            if (!model.materials.empty())
                _scene->SetModelMaterials(instance, model.materials);
            _scene->SetAllGeometryGroups(instance, false);
            for (u32 group : model.enabledGeometryGroups)
                _scene->SetGeometryGroupEnabled(instance, group, true);
            _scene->SetModelOpacity(instance, model.opacity);
            _scene->SetModelCastsShadows(instance, model.castsShadows);
            if (model.highlightIntensity > 0.0f)
                _scene->SetModelHighlight(instance, model.highlightIntensity, model.packedHighlightColor);
            _instances.push_back(instance);
        }

        _camera->FrameSphere(description.boundsCenter, description.boundsRadius);
        _contentRevision = description.revision;
        _renderView->GetView()->RequestTemporalReset();
        return !_instances.empty();
    }

    void ScenePreview::Clear()
    {
        for (ModelInstanceHandle instance : _instances)
            _scene->DestroyModelInstance(instance, 0);
        _scene->ReleaseRetiredHistory(0);
        _instances.clear();
        _contentRevision = 0;
        if (_renderView->GetView())
            _renderView->GetView()->MarkDirty();
    }

    void ScenePreview::Orbit(f32 deltaYaw, f32 deltaPitch)
    {
        if (_camera)
            _camera->Orbit(deltaYaw, deltaPitch);
    }
} // namespace RenderScenes
