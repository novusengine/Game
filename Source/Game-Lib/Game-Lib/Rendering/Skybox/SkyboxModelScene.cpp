#include "SkyboxModelScene.h"

#include "Game-Lib/Rendering/Asset/RenderAssetResources.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Renderer/RenderGraph.h>
#include <Renderer/Renderer.h>

namespace SkyboxRendering
{
    SkyboxModelScene::SkyboxModelScene(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                       RenderAssets::RenderAssetResources* assets, RenderResources& resources,
                                       bool validateTransfers)
        : _renderer(renderer), _gameRenderer(gameRenderer)
    {
        _scene = new RenderScenes::RenderScene(2, &assets->GetModelGeometryStorage(),
                                               &assets->GetMaterialStorage(), validateTransfers);

        Renderer::ImageDesc imageDesc;
        imageDesc.dimensions = vec2(1.0f);
        imageDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
        imageDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
        imageDesc.clearColor = Color::Clear;
        imageDesc.debugName = "Skybox Model Transparency";
        _transparencyAccumulation = renderer->CreateImage(imageDesc);

        imageDesc.format = Renderer::ImageFormat::R16_FLOAT;
        imageDesc.clearColor = Color::Red;
        imageDesc.debugName = "Skybox Model Transparency Revealage";
        _transparencyRevealage = renderer->CreateImage(imageDesc);

        imageDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_PYRAMID_RENDERSIZE;
        imageDesc.format = Renderer::ImageFormat::R32_FLOAT;
        imageDesc.debugName = "Skybox Model Depth Pyramid";
        _depthPyramid = renderer->CreateImage(imageDesc);

        RenderScenes::RenderViewDesc viewDesc;
        viewDesc.debugName = "Sky";
        viewDesc.scene = _scene;
        viewDesc.cameraIndex = 0;
        viewDesc.dimensions = static_cast<uvec2>(renderer->GetRenderSize());
        viewDesc.dimensionType = Renderer::ImageDimensionType::DIMENSION_SCALE_RENDERSIZE;
        viewDesc.colorTarget = resources.skyboxColor;
        viewDesc.transparencyAccumulationTarget = _transparencyAccumulation;
        viewDesc.transparencyRevealageTarget = _transparencyRevealage;
        viewDesc.depthPyramidTarget = _depthPyramid;
        viewDesc.depthTarget = resources.skyboxDepth;
        viewDesc.passFamilies = RenderScenes::RenderViewPassFamily::ForwardModels;
        viewDesc.lifetime = RenderScenes::RenderViewLifetime::Transient;
        viewDesc.refresh = RenderScenes::RenderViewRefresh::Continuous;
        RenderScenes::RenderView* view = gameRenderer->CreateRenderView(viewDesc);
        NC_ASSERT(view, "Failed to create skybox model View");
        if (view)
            _viewID = view->GetID();
    }

    SkyboxModelScene::~SkyboxModelScene()
    {
        if (_viewID != 0)
            _gameRenderer->DestroyRenderView(_viewID);
        delete _scene;
        if (_transparencyAccumulation != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_transparencyAccumulation);
        if (_transparencyRevealage != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_transparencyRevealage);
        if (_depthPyramid != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_depthPyramid);
    }

    void SkyboxModelScene::AddStartFramePass(Renderer::RenderGraph* renderGraph)
    {
        struct Data
        {
            Renderer::ImageMutableResource accumulation;
            Renderer::ImageMutableResource revealage;
        };
        renderGraph->AddPass<Data>("Clear Skybox Model Transparency",
            [this](Data& data, Renderer::RenderGraphBuilder& builder) {
                data.accumulation = builder.Write(_transparencyAccumulation, Renderer::PipelineType::GRAPHICS,
                                                  Renderer::LoadMode::CLEAR);
                data.revealage = builder.Write(_transparencyRevealage, Renderer::PipelineType::GRAPHICS,
                                               Renderer::LoadMode::CLEAR);
                return true;
            },
            [](Data&, Renderer::RenderGraphResources&, Renderer::CommandList&) { });
    }
} // namespace SkyboxRendering
