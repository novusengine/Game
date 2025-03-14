#pragma once
#include "Game-Lib/ECS/Components/UI/Widget.h"

#include <Base/Types.h>

#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <Renderer/DescriptorSet.h>
#include <Renderer/Font.h>
#include <Renderer/GPUVector.h>

#include <entt/fwd.hpp>

namespace Renderer
{
    class RenderGraph;
    class Renderer;
}
namespace ECS
{
    namespace Components
    {
        struct Transform2D;
        namespace UI
        {
            struct Panel;
            struct PanelTemplate;
            struct Text;
            struct TextTemplate;
            struct Clipper;
            struct BoundingRect;
        }
    }
}
struct RenderResources;
class Window;
class DebugRenderer;

class CanvasRenderer
{
public:
    CanvasRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer);
    void Clear();

    void Update(f32 deltaTime);

    //CanvasTextureID AddTexture(Renderer::TextureID textureID);

    void AddCanvasPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

private:
    void CreatePermanentResources();

    void UpdatePanelVertices(ECS::Components::Transform2D& transform, ECS::Components::UI::Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate);
    void UpdateTextVertices(ECS::Components::Transform2D& transform, ECS::Components::UI::Text& text, ECS::Components::UI::TextTemplate& textTemplate); // Returns the size of the text in pixels

    void UpdatePanelData(entt::entity entity, ECS::Components::Transform2D& transform, ECS::Components::UI::Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate);
    void UpdateTextData(entt::entity entity, ECS::Components::UI::Text& text, ECS::Components::UI::TextTemplate& textTemplate);

    void RenderPanel(Renderer::CommandList& commandList, ECS::Components::Transform2D& transform, ECS::Components::UI::Widget& widget, ECS::Components::UI::Panel& panel);
    void RenderText(Renderer::CommandList& commandList, ECS::Components::Transform2D& transform, ECS::Components::UI::Widget& widget, ECS::Components::UI::Text& text);

    vec2 PixelPosToNDC(const vec2& pixelPosition) const;
    vec2 PixelSizeToNDC(const vec2& pixelPosition) const;

    u32 LoadTexture(std::string_view path);

private:
    struct PanelDrawData
    {
    public:
        uvec3 packed0; // x: textureIndex & additiveTextureIndex, y: clipMaskTextureIndex, z: color
        hvec2 textureScaleToWidgetSize = hvec2(0.0f, 0.0f);
        vec4 texCoord; // uv
        vec4 slicingCoord; // uv
        //vec4 color; // xyz: color, w: unused
        vec4 cornerRadiusAndBorder; // xy: cornerRadius, zw: border
        hvec4 clipRegionRect = hvec4(0.0f, 0.0f, 1.0f, 1.0f); // xy: min, zw: max
        hvec4 clipMaskRegionRect = hvec4(0.0f, 0.0f, 1.0f, 1.0f); // xy: min, zw: max
    };

    struct CharDrawData
    {
    public:
        uvec4 packed0; // x: textureIndex & clipMaskTextureIndex, y: charIndex, z: textColor, w: borderColor
        vec4 packed1; // x: borderSize, y: padding, zw: unitRangeXY
        hvec4 clipRegionRect = hvec4(0.0f, 0.0f, 1.0f, 1.0f); // xy: min, zw: max
        hvec4 clipMaskRegionRect = hvec4(0.0f, 0.0f, 1.0f, 1.0f); // xy: min, zw: max
    };

private:
    Renderer::Renderer* _renderer;
    DebugRenderer* _debugRenderer;

    Renderer::GPUVector<vec4> _vertices;
    Renderer::GPUVector<PanelDrawData> _panelDrawDatas;
    
    Renderer::GPUVector<CharDrawData> _charDrawDatas;
   
    Renderer::Font* _font;
    Renderer::SamplerID _sampler;
    Renderer::TextureArrayID _textures;
    robin_hood::unordered_map<u32, u32> _textureNameHashToIndex;

    Renderer::TextureArrayID _fontTextures;
    robin_hood::unordered_map<Renderer::TextureID::type, u32> _textureIDToFontTexturesIndex;

    Renderer::DescriptorSet _descriptorSet;

    ECS::Components::UI::WidgetType _lastRenderedWidgetType = ECS::Components::UI::WidgetType::None;
};