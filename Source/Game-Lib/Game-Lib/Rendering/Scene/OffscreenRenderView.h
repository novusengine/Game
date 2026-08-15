#pragma once

#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>

class GameRenderer;

namespace Renderer { class Renderer; }

namespace RenderScenes
{
    // Owns the GPU render targets and registered CPU-side View used to render any Scene into an offscreen texture.
    // It makes continuous and retained render-to-texture Views use the same attachment and scheduling path.
    class OffscreenRenderView
    {
      public:
        OffscreenRenderView(Renderer::Renderer* renderer, GameRenderer* gameRenderer, RenderViewDesc desc);
        ~OffscreenRenderView();

        bool SetTarget(Renderer::TextureID target);

        RenderView* GetView() const { return _view; }
        Renderer::TextureID GetTarget() const { return _target; }

      private:
        Renderer::Renderer* _renderer = nullptr;
        GameRenderer* _gameRenderer = nullptr;
        RenderViewDesc _desc;
        RenderView* _view = nullptr;
        Renderer::TextureID _target = Renderer::TextureID::Invalid();
        Renderer::ImageID _visibility = Renderer::ImageID::Invalid();
        Renderer::ImageID _normals = Renderer::ImageID::Invalid();
        Renderer::ImageID _color = Renderer::ImageID::Invalid();
        Renderer::ImageID _transparencyAccumulation = Renderer::ImageID::Invalid();
        Renderer::ImageID _transparencyRevealage = Renderer::ImageID::Invalid();
        Renderer::ImageID _depthPyramid = Renderer::ImageID::Invalid();
        Renderer::DepthImageID _depth = Renderer::DepthImageID::Invalid();
    };
} // namespace RenderScenes
