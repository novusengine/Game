#include "OffscreenRenderView.h"

#include "Game-Lib/Rendering/GameRenderer.h"

#include <Renderer/Renderer.h>

#include <algorithm>
#include <bit>
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
        DestroyTargets();
    }

    void OffscreenRenderView::DestroyTargets()
    {
        if (_visibility != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_visibility);
        if (_normals != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_normals);
        if (_color != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_color);
        if (_transparencyAccumulation != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_transparencyAccumulation);
        if (_transparencyRevealage != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_transparencyRevealage);
        if (_depthPyramid != Renderer::ImageID::Invalid())
            _renderer->DestroyImage(_depthPyramid);
        if (_depth != Renderer::DepthImageID::Invalid())
            _renderer->DestroyDepthImage(_depth);
        _visibility = Renderer::ImageID::Invalid();
        _normals = Renderer::ImageID::Invalid();
        _color = Renderer::ImageID::Invalid();
        _transparencyAccumulation = Renderer::ImageID::Invalid();
        _transparencyRevealage = Renderer::ImageID::Invalid();
        _depthPyramid = Renderer::ImageID::Invalid();
        _depth = Renderer::DepthImageID::Invalid();
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

        imageDesc.format = Renderer::ImageFormat::R32_UINT;
        imageDesc.clearUInts = uvec4(0u);
        imageDesc.debugName = _desc.debugName + " Normals";
        _normals = _renderer->CreateImage(imageDesc);

        imageDesc.format = _renderer->GetSwapChainImageFormat();
        imageDesc.clearColor = Color(0.035f, 0.035f, 0.045f, 1.0f);
        imageDesc.debugName = _desc.debugName + " Color";
        _color = _renderer->CreateImage(imageDesc);

        imageDesc.format = Renderer::ImageFormat::R16G16B16A16_FLOAT;
        imageDesc.clearColor = Color::Clear;
        imageDesc.debugName = _desc.debugName + " Transparency";
        _transparencyAccumulation = _renderer->CreateImage(imageDesc);

        imageDesc.format = Renderer::ImageFormat::R16_FLOAT;
        imageDesc.clearColor = Color::Red;
        imageDesc.debugName = _desc.debugName + " Transparency Revealage";
        _transparencyRevealage = _renderer->CreateImage(imageDesc);

        const uvec2 pyramidDimensions(std::bit_floor(_desc.dimensions.x), std::bit_floor(_desc.dimensions.y));
        imageDesc.dimensions = pyramidDimensions;
        imageDesc.mipLevels = std::bit_width(std::max(pyramidDimensions.x, pyramidDimensions.y));
        imageDesc.format = Renderer::ImageFormat::R32_FLOAT;
        imageDesc.debugName = _desc.debugName + " Depth Pyramid";
        _depthPyramid = _renderer->CreateImage(imageDesc);

        Renderer::DepthImageDesc depthDesc;
        depthDesc.debugName = _desc.debugName + " Depth";
        depthDesc.dimensions = _desc.dimensions;
        depthDesc.format = Renderer::DepthImageFormat::D32_FLOAT;
        depthDesc.depthClearValue = 0.0f;
        _depth = _renderer->CreateDepthImage(depthDesc);

        _desc.visibilityTarget = _visibility;
        _desc.normalTarget = _normals;
        _desc.colorTarget = _color;
        _desc.transparencyAccumulationTarget = _transparencyAccumulation;
        _desc.transparencyRevealageTarget = _transparencyRevealage;
        _desc.depthPyramidTarget = _depthPyramid;
        _desc.depthTarget = _depth;
        _desc.retainedOutput = _target;
        _view = _gameRenderer->CreateRenderView(_desc);
        if (!_view)
        {
            DestroyTargets();
            _target = Renderer::TextureID::Invalid();
        }
        return _view != nullptr;
    }
} // namespace RenderScenes
