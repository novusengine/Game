#include "CanvasRenderer.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Panel.h"
#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/GPUVector.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Font.h>

#include <entt/entt.hpp>
#include <utfcpp/utf8.h>

using namespace ECS::Components::UI;

void CanvasRenderer::Clear()
{
    _vertices.Clear();
    _panelDrawDatas.Clear();
    _charDrawDatas.Clear();
    _textureNameHashToIndex.clear();

    _renderer->UnloadTexturesInArray(_textures, 2);
}

CanvasRenderer::CanvasRenderer(Renderer::Renderer* renderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _debugRenderer(debugRenderer)
{
    CreatePermanentResources();
}

void CanvasRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& transformSystem = ECS::Transform2DSystem::Get(*registry);

    registry->view<Widget, DestroyWidget>().each([&](entt::entity entity, Widget& widget)
    {
        if (widget.type == WidgetType::Canvas)
            return;

        if (widget.type == WidgetType::Panel)
        {
            auto& panel = registry->get<Panel>(entity);

            if (panel.gpuDataIndex != -1)
                _panelDrawDatas.Remove(panel.gpuDataIndex);

            if (panel.gpuVertexIndex != -1)
                _vertices.Remove(panel.gpuVertexIndex, 6);
        }
        else if (widget.type == WidgetType::Text)
        {
            auto& text = registry->get<Text>(entity);

            if (text.gpuDataIndex != -1)
                _charDrawDatas.Remove(text.gpuDataIndex, text.numCharsNonWhitespace);

            if (text.gpuVertexIndex != -1)
                _vertices.Remove(text.gpuVertexIndex, text.numCharsNonWhitespace * 6); // * 6 because 6 vertices per char
        }

        registry->destroy(entity);
    });
    registry->clear<DestroyWidget>();

    // Dirty widget flags
    registry->view<Widget, EventInputInfo, DirtyWidgetFlags>().each([&](entt::entity entity, Widget& widget, EventInputInfo& eventInputInfo)
    {
        bool wasInteractable = eventInputInfo.isInteractable;
        eventInputInfo.isInteractable = (widget.flags & WidgetFlags::Interactable) == WidgetFlags::Interactable;

        if (wasInteractable != eventInputInfo.isInteractable)
        {
            ECS::Util::UI::RefreshTemplate(registry, entity, eventInputInfo);
        }
    });

    // Dirty transforms
    registry->view<DirtyWidgetTransform>().each([&](entt::entity entity)
    {
        auto& widget = registry->get<Widget>(entity);

        if (widget.type == WidgetType::Canvas)
        {
            return; // Nothing to do for canvas
        }

        auto& transform = registry->get<ECS::Components::Transform2D>(entity);
        if (widget.type == WidgetType::Panel)
        {
            auto& panel = registry->get<Panel>(entity);
            auto& panelTemplate = registry->get<PanelTemplate>(entity);

            UpdatePanelVertices(transform, panel, panelTemplate);
        }
        else if (widget.type == WidgetType::Text)
        {
            auto& text = registry->get<Text>(entity);
            auto& textTemplate = registry->get<TextTemplate>(entity);

            UpdateTextVertices(transform, text, textTemplate);
        }
    });
    registry->clear<DirtyWidgetTransform>();

    // Dirty widget datas
    registry->view<DirtyWidgetData>().each([&](entt::entity entity)
    {
        auto& widget = registry->get<Widget>(entity);
        if (widget.type == WidgetType::Canvas)
        {
            //auto& canvas = registry->get<Canvas>(entity);
            // Update canvas data
        }
        else if (widget.type == WidgetType::Panel)
        {
            ECS::Components::Transform2D& transform = registry->get<ECS::Components::Transform2D>(entity);
            auto& panel = registry->get<Panel>(entity);
            auto& panelTemplate = registry->get<PanelTemplate>(entity);

            UpdatePanelData(transform, panel, panelTemplate);
        }
        else if (widget.type == WidgetType::Text)
        {
            auto& text = registry->get<Text>(entity);
            auto& textTemplate = registry->get<TextTemplate>(entity);

            UpdateTextData(text, textTemplate);
            text.hasGrown = false;
        }
    });

    // Dirty child clippers
    vec2 referenceSize = vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
    registry->view<DirtyChildClipper>().each([&](entt::entity entity)
    {
        auto& clipper = registry->get<Clipper>(entity);

        if (clipper.clipChildren)
        {
            auto& rect = registry->get<BoundingRect>(entity);

            vec2 scaledClipRegionMin = (rect.min + (rect.max - rect.min) * clipper.clipRegionMin) / referenceSize;
            vec2 scaledClipRegionMax = (rect.min + (rect.max - rect.min) * clipper.clipRegionMax) / referenceSize;

            vec2 scaledClipMaskRegionMin = rect.min / referenceSize;
            vec2 scaledClipMaskRegionMax = rect.max / referenceSize;

            // Set childrens data depending on this widget
            transformSystem.IterateChildrenRecursiveBreadth(entity, [&](entt::entity childEntity)
            {
                auto& childWidget = registry->get<Widget>(childEntity);

                if (childWidget.type == WidgetType::Panel)
                {
                    auto& childPanel = registry->get<Panel>(childEntity);
                    auto& panelData = _panelDrawDatas[childPanel.gpuDataIndex];

                    panelData.packed0.y = (clipper.hasClipMaskTexture) ? LoadTexture(clipper.clipMaskTexture) : 0;
                    panelData.clipRegionRect = vec4(scaledClipRegionMin, scaledClipRegionMax);
                    panelData.clipMaskRegionRect = vec4(scaledClipMaskRegionMin, scaledClipMaskRegionMax);

                    _panelDrawDatas.SetDirtyElement(childPanel.gpuDataIndex);
                }
                
            });
        }
        else
        {
            // Set childrens data depending on themselves
            transformSystem.IterateChildrenRecursiveBreadth(entity, [&](entt::entity childEntity)
            {
                auto& childWidget = registry->get<Widget>(childEntity);

                if (childWidget.type == WidgetType::Panel)
                {
                    auto& clipper = registry->get<Clipper>(childEntity);
                    auto& rect = registry->get<BoundingRect>(childEntity);

                    vec2 scaledClipRegionMin = (rect.min + (rect.max - rect.min) * clipper.clipRegionMin) / referenceSize;
                    vec2 scaledClipRegionMax = (rect.min + (rect.max - rect.min) * clipper.clipRegionMax) / referenceSize;

                    vec2 scaledClipMaskRegionMin = rect.min / referenceSize;
                    vec2 scaledClipMaskRegionMax = rect.max / referenceSize;

                    auto& childPanel = registry->get<Panel>(childEntity);
                    auto& panelData = _panelDrawDatas[childPanel.gpuDataIndex];

                    panelData.packed0.y = (clipper.hasClipMaskTexture) ? LoadTexture(clipper.clipMaskTexture) : 0;
                    panelData.clipRegionRect = vec4(scaledClipRegionMin, scaledClipRegionMax);
                    panelData.clipMaskRegionRect = vec4(scaledClipMaskRegionMin, scaledClipMaskRegionMax);

                    _panelDrawDatas.SetDirtyElement(childPanel.gpuDataIndex);
                }
            });
        }
    });

    // Dirty clippers, set data depending on themselves
    registry->view<DirtyClipper>().each([&](entt::entity entity)
    {
        auto& clipper = registry->get<Clipper>(entity);
        if (clipper.clipChildren)
            return;

        auto& widget = registry->get<Widget>(entity);
        if (widget.type == WidgetType::Panel)
        {
            auto& rect = registry->get<BoundingRect>(entity);

            vec2 scaledClipRegionMin = (rect.min + (rect.max - rect.min) * clipper.clipRegionMin) / referenceSize;
            vec2 scaledClipRegionMax = (rect.min + (rect.max - rect.min) * clipper.clipRegionMax) / referenceSize;

            vec2 scaledClipMaskRegionMin = rect.min / referenceSize;
            vec2 scaledClipMaskRegionMax = rect.max / referenceSize;

            auto& panel = registry->get<Panel>(entity);
            auto& panelData = _panelDrawDatas[panel.gpuDataIndex];

            panelData.packed0.y = (clipper.hasClipMaskTexture) ? LoadTexture(clipper.clipMaskTexture) : 0;
            panelData.clipRegionRect = vec4(scaledClipRegionMin, scaledClipRegionMax);
            panelData.clipMaskRegionRect = vec4(scaledClipMaskRegionMin, scaledClipMaskRegionMax);

            _panelDrawDatas.SetDirtyElement(panel.gpuDataIndex);
        }
    });

    if (_vertices.SyncToGPU(_renderer))
    {
        _descriptorSet.Bind("_vertices", _vertices.GetBuffer());
    }

    if (_panelDrawDatas.SyncToGPU(_renderer))
    {
        _descriptorSet.Bind("_panelDrawDatas", _panelDrawDatas.GetBuffer());
    }

    if (_charDrawDatas.SyncToGPU(_renderer))
    {
        _descriptorSet.Bind("_charDrawDatas", _charDrawDatas.GetBuffer());
    }

    registry->clear<DirtyWidgetData>();
    registry->clear<DirtyWidgetFlags>();
    registry->clear<DirtyChildClipper>();
    registry->clear<DirtyClipper>();
}

void CanvasRenderer::AddCanvasPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    _lastRenderedWidgetType = WidgetType::None;

    struct Data
    {
        Renderer::ImageMutableResource target; // TODO: Target should be grabbed from canvases

        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource descriptorSet;
    };
    renderGraph->AddPass<Data>("Canvases",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.target = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);

            builder.Read(_panelDrawDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_charDrawDatas.GetBuffer(), BufferUsage::GRAPHICS);

            data.globalDescriptorSet = builder.Use(resources.globalDescriptorSet);
            data.descriptorSet = builder.Use(_descriptorSet);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender2D);

            // Set up pipelines
            Renderer::GraphicsPipelineID panelPipeline;
            {
                Renderer::GraphicsPipelineDesc pipelineDesc;

                // Rasterizer state
                pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

                // Render targets.
                const Renderer::ImageDesc& desc = graphResources.GetImageDesc(data.target);
                pipelineDesc.states.renderTargetFormats[0] = desc.format;

                // Shader
                Renderer::VertexShaderDesc vertexShaderDesc;
                vertexShaderDesc.path = "UI/Panel.vs.hlsl";

                Renderer::PixelShaderDesc pixelShaderDesc;
                pixelShaderDesc.path = "UI/Panel.ps.hlsl";

                pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
                pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

                // Blending
                pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
                pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
                pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
                pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ZERO;
                pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;

                panelPipeline = _renderer->CreatePipeline(pipelineDesc);
            }

            Renderer::GraphicsPipelineID textPipeline;
            {
                Renderer::GraphicsPipelineDesc pipelineDesc;

                // Rasterizer state
                pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

                // Render targets.
                const Renderer::ImageDesc& desc = graphResources.GetImageDesc(data.target);
                pipelineDesc.states.renderTargetFormats[0] = desc.format;

                // Shader
                Renderer::VertexShaderDesc vertexShaderDesc;
                vertexShaderDesc.path = "UI/Text.vs.hlsl";

                Renderer::PixelShaderDesc pixelShaderDesc;
                pixelShaderDesc.path = "UI/Text.ps.hlsl";

                pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
                pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

                // Blending
                pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
                pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::ONE;
                pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
                pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
                pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;

                textPipeline = _renderer->CreatePipeline(pipelineDesc);
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& transform2DSystem = ECS::Transform2DSystem::Get(*registry);

            Renderer::GraphicsPipelineID currentPipeline;
            Renderer::RenderPassDesc currentRenderPassDesc;
            graphResources.InitializeRenderPassDesc(currentRenderPassDesc);
            currentRenderPassDesc.renderTargets[0] = data.target;

            // Loop over widget roots
            registry->view<Widget, WidgetRoot>().each([&](auto entity, auto& widget)
            {
                // Loop over children recursively (depth first)
                transform2DSystem.IterateChildrenRecursiveDepth(entity, [&, registry](auto childEntity)
                {
                    auto& transform = registry->get<ECS::Components::Transform2D>(childEntity);
                    auto& childWidget = registry->get<Widget>(childEntity);

                    if (!childWidget.IsVisible())
                        return false; // Skip invisible widgets

                    if (childWidget.type == WidgetType::Canvas)
                        return true; // There is nothing to draw for a canvas

                    if (_lastRenderedWidgetType != childWidget.type)
                    {
                        if (_lastRenderedWidgetType != WidgetType::None)
                        {
                            commandList.EndPipeline(currentPipeline);
                            commandList.EndRenderPass(currentRenderPassDesc);
                        }

                        _lastRenderedWidgetType = childWidget.type;
                        
                        if (childWidget.type == WidgetType::Panel)
                        {
                            currentPipeline = panelPipeline;
                        }
                        else
                        {
                            currentPipeline = textPipeline;
                        }

                        commandList.BeginRenderPass(currentRenderPassDesc);
                        commandList.BeginPipeline(currentPipeline);
                        commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, data.descriptorSet, frameIndex);
                    }

                    if (childWidget.type == WidgetType::Panel)
                    {
                        auto& panel = registry->get<Panel>(childEntity);
                        RenderPanel(commandList, transform, childWidget, panel);
                    }
                    else if (childWidget.type == WidgetType::Text)
                    {
                        auto& text = registry->get<Text>(childEntity);
                        if (text.numCharsNonWhitespace > 0)
                        {
                            RenderText(commandList, transform, childWidget, text);
                        }
                    }

                    return true;
                });
            });

            if (_lastRenderedWidgetType != WidgetType::None)
            {
                commandList.EndPipeline(currentPipeline);
                commandList.EndRenderPass(currentRenderPassDesc);
            }
        });
}

void CanvasRenderer::CreatePermanentResources()
{
    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 4096;

    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    _descriptorSet.Bind("_textures", _textures);

    Renderer::DataTextureDesc dataTextureDesc;
    dataTextureDesc.width = 1;
    dataTextureDesc.height = 1;
    dataTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM_SRGB;
    dataTextureDesc.data = new u8[4]{ 255, 255, 255, 255 }; // White, because UI color is multiplied with this
    dataTextureDesc.debugName = "UIDebugTexture";

    u32 arrayIndex = 0;
    _renderer->CreateDataTextureIntoArray(dataTextureDesc, _textures, arrayIndex);

    Renderer::DataTextureDesc additiveDataTextureDesc;
    additiveDataTextureDesc.width = 1;
    additiveDataTextureDesc.height = 1;
    additiveDataTextureDesc.format = Renderer::ImageFormat::R8G8B8A8_UNORM_SRGB;
    additiveDataTextureDesc.data = new u8[4]{ 0, 0, 0, 0 }; // Black, because this is additive
    additiveDataTextureDesc.debugName = "UIDebugAdditiveTexture";

    _renderer->CreateDataTextureIntoArray(additiveDataTextureDesc, _textures, arrayIndex);

    // Samplers
    Renderer::SamplerDesc samplerDesc;
    samplerDesc.enabled = true;
    samplerDesc.filter = Renderer::SamplerFilter::MIN_MAG_MIP_LINEAR;
    samplerDesc.addressU = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressV = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.addressW = Renderer::TextureAddressMode::CLAMP;
    samplerDesc.shaderVisibility = Renderer::ShaderVisibility::PIXEL;

    _sampler = _renderer->CreateSampler(samplerDesc);
    _descriptorSet.Bind("_sampler"_h, _sampler);

    textureArrayDesc.size = 256;
    _fontTextures = _renderer->CreateTextureArray(textureArrayDesc);

    _font = Renderer::Font::GetDefaultFont(_renderer);
    _renderer->AddTextureToArray(_font->GetTextureID(), _fontTextures);

    _descriptorSet.Bind("_fontTextures"_h, _fontTextures);

    _vertices.SetDebugName("UIVertices");
    _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _panelDrawDatas.SetDebugName("PanelDrawDatas");
    _panelDrawDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _charDrawDatas.SetDebugName("CharDrawDatas");
    _charDrawDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    //_data.textVertices.SetDebugName("TextVertices");
    //_data.textVertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}

void CanvasRenderer::UpdatePanelVertices(ECS::Components::Transform2D& transform, ECS::Components::UI::Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate)
{
    // Add vertices if necessary
    if (panel.gpuVertexIndex == -1)
    {
        panel.gpuVertexIndex = _vertices.AddCount(6);
    }

    const vec2 min = panelTemplate.texCoords.min;
    const vec2 max = panelTemplate.texCoords.max;

    vec2 panelUVs[6];
    panelUVs[0] = vec2(min.x, max.y); // Left Top 
    panelUVs[1] = vec2(max.x, min.y); // Right Bottom
    panelUVs[2] = vec2(max.x, max.y); // Right Top

    panelUVs[3] = vec2(min.x, min.y); // Left Bottom
    panelUVs[4] = vec2(max.x, min.y); // Right Bottom
    panelUVs[5] = vec2(min.x, max.y); // Left Top

    // Update vertices
    vec2 position = PixelPosToNDC(transform.GetWorldPosition());
    vec2 size = PixelSizeToNDC(transform.GetSize());

    // Triangle 1
    _vertices[panel.gpuVertexIndex + 0] = vec4(position, panelUVs[0]);
    _vertices[panel.gpuVertexIndex + 1] = vec4(position + vec2(size.x, size.y), panelUVs[1]);
    _vertices[panel.gpuVertexIndex + 2] = vec4(position + vec2(size.x, 0), panelUVs[2]);

    // Triangle 2
    _vertices[panel.gpuVertexIndex + 3] = vec4(position + vec2(0, size.y), panelUVs[3]);
    _vertices[panel.gpuVertexIndex + 4] = vec4(position + vec2(size.x, size.y), panelUVs[4]);
    _vertices[panel.gpuVertexIndex + 5] = vec4(position, panelUVs[5]);

    _vertices.SetDirtyElements(panel.gpuVertexIndex, 6);
}

void CalculateVertices(const vec4& pos, const vec4& uv, Renderer::GPUVector<vec4>& vertices, u32 vertexIndex)
{
    const f32& posLeft = pos.x;
    const f32& posBottom = pos.y;
    const f32& posRight = pos.z;
    const f32& posTop = pos.w;

    const f32& uvLeft = uv.x;
    const f32& uvBottom = uv.y;
    const f32& uvRight = uv.z;
    const f32& uvTop = uv.w;

    vertices[vertexIndex + 0] = vec4(posLeft, posBottom, uvLeft, uvBottom);
    vertices[vertexIndex + 1] = vec4(posLeft, posTop, uvLeft, uvTop);
    vertices[vertexIndex + 2] = vec4(posRight, posBottom, uvRight, uvBottom);

    vertices[vertexIndex + 3] = vec4(posRight, posTop, uvRight, uvTop);
    vertices[vertexIndex + 4] = vec4(posRight, posBottom, uvRight, uvBottom);
    vertices[vertexIndex + 5] = vec4(posLeft, posTop, uvLeft, uvTop);
}

void CanvasRenderer::UpdateTextVertices(ECS::Components::Transform2D& transform, ECS::Components::UI::Text& text, ECS::Components::UI::TextTemplate& textTemplate)
{
    if (text.text.size() == 0)
    {
        return;
    }

    if (text.sizeChanged)
    {
        utf8::iterator countIt(text.text.begin(), text.text.begin(), text.text.end());
        utf8::iterator coundEndIt(text.text.end(), text.text.begin(), text.text.end());

        // Count how many non whitspace characters there are
        i32 numCharsNonWhitespace = 0;
        i32 numCharsNewLine = 0;
        for (; countIt != coundEndIt; countIt++)
        {
            unsigned int c = *countIt;

            bool isNewLine = c == '\n';
            numCharsNewLine += 1 * isNewLine;

            bool isNonWhiteSpace = c != ' ' && c != '\r' && !isNewLine;
            numCharsNonWhitespace += 1 * isNonWhiteSpace;
        }

        text.numCharsNonWhitespace = numCharsNonWhitespace;
        text.numCharsNewLine = numCharsNewLine;
        text.sizeChanged = false;
    }

    if (text.numCharsNonWhitespace == 0)
    {
        return;
    }

    // Add vertices if necessary
    if (text.gpuVertexIndex == -1 || text.hasGrown)
    {
        u32 numVertices = text.numCharsNonWhitespace * 6;
        text.gpuVertexIndex = _vertices.AddCount(numVertices);
    }

    vec2 worldPos = transform.GetWorldPosition();

    Renderer::Font* font = Renderer::Font::GetFont(_renderer, textTemplate.font);
    vec2 atlasSize = vec2(font->width, font->height);
    vec2 texelSize = 1.0f / atlasSize;

    const Renderer::FontMetrics& metrics = font->metrics;

    f32 fontSize = textTemplate.size;

    utf8::iterator it(text.text.begin(), text.text.begin(), text.text.end());
    utf8::iterator endIt(text.text.end(), text.text.begin(), text.text.end());

    f32 textLineHeightOffset = (fontSize * static_cast<f32>(metrics.lineHeight)) * text.numCharsNewLine;
    vec2 penPos = vec2(0.0f, textLineHeightOffset);// fontScale* metrics.ascenderY);

    u32 vertexIndex = static_cast<u32>(text.gpuVertexIndex);
    for (; it != endIt; it++)
    {   
        u32 c = *it;

        // Skip carriage return characters
        if (c == '\r')
        {
            continue;
        }

        // Handle newline characters
        if (c == '\n')
        {
            penPos.x = 0.0f;
            penPos.y -= fontSize * static_cast<f32>(metrics.lineHeight);
            continue;
        }

        const Renderer::Glyph& glyph = font->GetGlyph(c);
        if (!glyph.isWhitespace())
        {
            f64 planeLeft, planeBottom, planeRight, planeTop;
            f64 atlasLeft, atlasBottom, atlasRight, atlasTop;

            // Get the glyph's quad bounds in plane space (vertex positions)
            glyph.getQuadPlaneBounds(planeLeft, planeBottom, planeRight, planeTop);

            // Get the glyph's quad bounds in atlas space (texture UVs)
            glyph.getQuadAtlasBounds(atlasLeft, atlasBottom, atlasRight, atlasTop);

            // Scale the plane coordinates by the font scale
            planeLeft *= fontSize;
            planeBottom *= fontSize;
            planeRight *= fontSize;
            planeTop *= fontSize;

            planeBottom += textTemplate.borderSize;
            planeTop += textTemplate.borderSize;

            // Translate the plane coordinates by the current pen position and the world position
            planeLeft += penPos.x + worldPos.x;
            planeBottom += penPos.y + worldPos.y;
            planeRight += penPos.x + worldPos.x;
            planeTop += penPos.y + worldPos.y;

            // Convert the plane coordinates to NDC space
            vec2 planeMin = PixelPosToNDC(vec2(planeLeft, planeBottom));
            vec2 planeMax = PixelPosToNDC(vec2(planeRight, planeTop));

            // Scale the atlas coordinates by the texel dimensions
            atlasLeft *= texelSize.x;
            atlasBottom *= texelSize.y;
            atlasRight *= texelSize.x;
            atlasTop *= texelSize.y;

            // Add vertices
            CalculateVertices(vec4(planeMin, planeMax), vec4(atlasLeft, atlasBottom, atlasRight, atlasTop), _vertices, vertexIndex);
            vertexIndex += 6;
        }

        f32 advance = static_cast<f32>(glyph.getAdvance());
        //advance = font->GetFontAdvance(advance, c, *(it+1)); // TODO: Kerning
        
        penPos.x += (fontSize * advance) + textTemplate.borderSize;
    }

    _vertices.SetDirtyElements(text.gpuVertexIndex, text.numCharsNonWhitespace * 6);
}

void CanvasRenderer::UpdatePanelData(ECS::Components::Transform2D& transform, Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate)
{
    // Add draw data if necessary
    if (panel.gpuDataIndex == -1)
    {
        panel.gpuDataIndex = _panelDrawDatas.Add();
    }
    vec2 size = transform.GetSize();
    vec2 cornerRadius = vec2(panelTemplate.cornerRadius / size.x, panelTemplate.cornerRadius /size.y);

    // Update draw data
    auto& drawData = _panelDrawDatas[panel.gpuDataIndex];
    drawData.packed0.z = panelTemplate.color.ToRGBA32();
    drawData.cornerRadiusAndBorder = vec4(cornerRadius, 0.0f, 0.0f);

    // Update textures
    u16 textureIndex = 0;
    u16 additiveTextureIndex = 1;

    if (!panelTemplate.background.empty())
    {
        textureIndex = LoadTexture(panelTemplate.background);
    }

    if (!panelTemplate.foreground.empty())
    {
        additiveTextureIndex = LoadTexture(panelTemplate.foreground);
    }

    drawData.packed0.x = (textureIndex & 0xFFFF) | ((additiveTextureIndex & 0xFFFF) << 16);

    // Nine slicing
    const vec2& widgetSize = transform.GetSize();

    Renderer::TextureID textureID = _renderer->GetTextureID(_textures, textureIndex);
    Renderer::TextureBaseDesc textureBaseDesc = _renderer->GetTextureDesc(textureID);
    vec2 texSize = vec2(textureBaseDesc.width, textureBaseDesc.height);

    vec2 textureScaleToWidgetSize = texSize / widgetSize;
    drawData.textureScaleToWidgetSize = hvec2(textureScaleToWidgetSize.x, textureScaleToWidgetSize.y);
    drawData.texCoord = vec4(panelTemplate.texCoords.min, panelTemplate.texCoords.max);
    drawData.slicingCoord = vec4(panelTemplate.nineSliceCoords.min, panelTemplate.nineSliceCoords.max);

    _panelDrawDatas.SetDirtyElement(panel.gpuDataIndex);
}

void CanvasRenderer::UpdateTextData(Text& text, ECS::Components::UI::TextTemplate& textTemplate)
{
    if (text.sizeChanged)
    {
        // Count how many non whitspace characters there are
        utf8::iterator countIt(text.text.begin(), text.text.begin(), text.text.end());
        utf8::iterator countEndIt(text.text.end(), text.text.begin(), text.text.end());

        text.numCharsNonWhitespace = 0;
        text.numCharsNewLine = 0;
        for (; countIt != countEndIt; countIt++)
        {
            unsigned int c = *countIt;

            bool isNewLine = c == '\n';
            text.numCharsNewLine += 1 * isNewLine;

            bool isNonWhiteSpace = c != ' ' && c != '\r' && !isNewLine;
            text.numCharsNonWhitespace += 1 * isNonWhiteSpace;
        }

        text.sizeChanged = false;
    }

    if (text.numCharsNonWhitespace == 0)
    {
        return;
    }

    // Add or update draw data if necessary
    if (text.gpuDataIndex == -1 || text.hasGrown)
    {
        text.gpuDataIndex = _charDrawDatas.AddCount(text.numCharsNonWhitespace);
    }

    // Update CharDrawData
    Renderer::Font* font = Renderer::Font::GetFont(_renderer, textTemplate.font);

    Renderer::TextureID fontTextureID = font->GetTextureID();

    u32 fontTextureIndex;

    auto textureIt = _textureIDToFontTexturesIndex.find(static_cast<Renderer::TextureID::type>(fontTextureID));
    if (textureIt != _textureIDToFontTexturesIndex.end())
    {
        fontTextureIndex = textureIt->second;
    }
    else
    {
        fontTextureIndex = _renderer->AddTextureToArray(fontTextureID, _fontTextures);
        _textureIDToFontTexturesIndex[static_cast<Renderer::TextureID::type>(fontTextureID)] = fontTextureIndex;
    }

    utf8::iterator it(text.text.begin(), text.text.begin(), text.text.end());
    utf8::iterator endIt(text.text.end(), text.text.begin(), text.text.end());

    u32 charIndex = 0;
    for (; it != endIt; it++)
    {
        unsigned int c = *it;

        if (c == '\n' || c == '\r')
            continue;

        Renderer::Glyph glyph = font->GetGlyph(c);

        if (glyph.isWhitespace())
        {
            continue;
        }

        auto& drawData = _charDrawDatas[text.gpuDataIndex + charIndex];
        drawData.packed0.x = fontTextureIndex;
        drawData.packed0.y = charIndex;
        drawData.packed0.z = textTemplate.color.ToRGBA32();
        drawData.packed0.w = textTemplate.borderColor.ToRGBA32();

        drawData.packed1.x = textTemplate.borderSize;

        // Unit range
        f32 distanceRange = font->upperPixelRange - font->lowerPixelRange;
        drawData.packed1.z = distanceRange / font->width;
        drawData.packed1.w = distanceRange / font->height;

        charIndex++;
    }
    _charDrawDatas.SetDirtyElements(text.gpuDataIndex, text.numCharsNonWhitespace);
}

void CanvasRenderer::RenderPanel(Renderer::CommandList& commandList, ECS::Components::Transform2D& transform, Widget& widget, Panel& panel)
{
    commandList.PushMarker("Panel", Color::White);
    commandList.Draw(6, 1, panel.gpuVertexIndex, panel.gpuDataIndex);
    commandList.PopMarker();
}

void CanvasRenderer::RenderText(Renderer::CommandList& commandList, ECS::Components::Transform2D& transform, Widget& widget, Text& text)
{
    commandList.PushMarker("Text", Color::White);
    commandList.Draw(6, text.numCharsNonWhitespace, text.gpuVertexIndex, text.gpuDataIndex);
    commandList.PopMarker();
}

vec2 CanvasRenderer::PixelPosToNDC(const vec2& pixelPosition) const
{
    constexpr vec2 screenSize = vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

    return vec2(2.0 * pixelPosition.x / screenSize.x - 1.0, 2.0 * pixelPosition.y / screenSize.y - 1.0);
}

vec2 CanvasRenderer::PixelSizeToNDC(const vec2& pixelSize) const
{
    constexpr vec2 screenSize = vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

    return vec2(2.0 * pixelSize.x / screenSize.x, 2.0 * pixelSize.y / screenSize.y);
}

u32 CanvasRenderer::LoadTexture(std::string_view path)
{
    u32 textureNameHash = StringUtils::fnv1a_32(path.data(), path.size());

    if (_textureNameHashToIndex.contains(textureNameHash))
    {
        // Use already loaded texture
        return _textureNameHashToIndex[textureNameHash];
    }
    
    // Load texture
    Renderer::TextureDesc desc;
    desc.path = path;

    u32 textureIndex;
    _renderer->LoadTextureIntoArray(desc, _textures, textureIndex);

    _textureNameHashToIndex[textureNameHash] = textureIndex;
    return textureIndex;
}
