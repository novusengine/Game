#pragma once
#include <Base/Types.h>

#include <Renderer/Descriptors/TextureDesc.h>

namespace ECS::Components::UI
{
    struct Canvas
    {
    public:
        std::string name;
        Renderer::TextureID renderTexture;
    };

    struct CanvasRenderTargetTag {};
    struct DirtyCanvasTag {};
}