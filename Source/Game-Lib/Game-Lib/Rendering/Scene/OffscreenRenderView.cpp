#include "OffscreenRenderView.h"

#include "Game-Lib/Rendering/GameRenderer.h"

#include <Renderer/Renderer.h>

#include <utility>

namespace RenderScenes
{
    OffscreenRenderView::OffscreenRenderView(Renderer::Renderer* renderer, GameRenderer* gameRenderer,
                                             RenderViewDesc desc)
        : _renderer(renderer), _gameRenderer(gameRenderer), _desc(std::move(desc))
    {
    }

    OffscreenRenderView::~OffscreenRenderView()
    {
        if (_view)
            _gameRenderer->DestroyRenderView(_view->GetID());
    }

    bool OffscreenRenderView::SetTarget(Renderer::TextureID target)
    {
        if (target == Renderer::TextureID::Invalid())
            return false;
        if (_view && target == _target)
            return true;
        if (_view)
            return false;

        const Renderer::TextureBaseDesc targetDesc = _renderer->GetDesc(target);
        if (targetDesc.width == 0 || targetDesc.height == 0)
            return false;

        _target = target;
        _desc.dimensions = uvec2(targetDesc.width, targetDesc.height);

        Renderer::ImageDesc imageDesc;
        imageDesc.dimensions = _desc.dimensions;
        imageDesc.format = Renderer::ImageFormat::R32_UINT;
        imageDesc.clearUInts = uvec4(0);
        imageDesc.debugName = _desc.debugName + " Visibility";
        _visibility = _renderer->CreateImage(imageDesc);

        imageDesc.format = Renderer::ImageFormat::R11G11B10_UFLOAT;
        imageDesc.clearColor = Color::Clear;
        imageDesc.debugName = _desc.debugName + " Normals";
        _normals = _renderer->CreateImage(imageDesc);

        imageDesc.format = _renderer->GetSwapChainImageFormat();
        imageDesc.clearColor = Color(0.035f, 0.035f, 0.045f, 1.0f);
        imageDesc.debugName = _desc.debugName + " Color";
        _color = _renderer->CreateImage(imageDesc);

        Renderer::DepthImageDesc depthDesc;
        depthDesc.debugName = _desc.debugName + " Depth";
        depthDesc.dimensions = _desc.dimensions;
        depthDesc.format = Renderer::DepthImageFormat::D32_FLOAT;
        depthDesc.depthClearValue = 0.0f;
        _depth = _renderer->CreateDepthImage(depthDesc);

        _desc.visibilityTarget = _visibility;
        _desc.normalTarget = _normals;
        _desc.colorTarget = _color;
        _desc.depthTarget = _depth;
        _desc.retainedOutput = _target;
        _view = _gameRenderer->CreateRenderView(_desc);
        return _view != nullptr;
    }
} // namespace RenderScenes
