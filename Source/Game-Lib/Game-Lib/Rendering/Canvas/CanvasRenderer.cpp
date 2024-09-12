#include "CanvasRenderer.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Panel.h"
#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Rendering/RenderResources.h"
#include "Game-Lib/Util/ServiceLocator.h"

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

CanvasRenderer::CanvasRenderer(Renderer::Renderer* renderer)
    : _renderer(renderer)
{
    CreatePermanentResources();
}

void CanvasRenderer::Update(f32 deltaTime)
{
    ZoneScoped;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

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
                Renderer::GraphicsPipelineDesc desc;
                graphResources.InitializePipelineDesc(desc);

                // Rasterizer state
                desc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

                // Render targets.
                desc.renderTargets[0] = data.target;

                // Shader
                Renderer::VertexShaderDesc vertexShaderDesc;
                vertexShaderDesc.path = "UI/Panel.vs.hlsl";

                Renderer::PixelShaderDesc pixelShaderDesc;
                pixelShaderDesc.path = "UI/Panel.ps.hlsl";

                desc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
                desc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

                // Blending
                desc.states.blendState.renderTargets[0].blendEnable = true;
                desc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
                desc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
                desc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ZERO;
                desc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;

                panelPipeline = _renderer->CreatePipeline(desc);
            }

            Renderer::GraphicsPipelineID textPipeline;
            {
                Renderer::GraphicsPipelineDesc desc;
                graphResources.InitializePipelineDesc(desc);

                // Rasterizer state
                desc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

                // Render targets.
                desc.renderTargets[0] = data.target;

                // Shader
                Renderer::VertexShaderDesc vertexShaderDesc;
                vertexShaderDesc.path = "UI/Text.vs.hlsl";

                Renderer::PixelShaderDesc pixelShaderDesc;
                pixelShaderDesc.path = "UI/Text.ps.hlsl";

                desc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
                desc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

                // Blending
                desc.states.blendState.renderTargets[0].blendEnable = true;
                desc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
                desc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
                desc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ZERO;
                desc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::ONE;

                textPipeline = _renderer->CreatePipeline(desc);
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& transform2DSystem = ECS::Transform2DSystem::Get(*registry);

            Renderer::GraphicsPipelineID currentPipeline;

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
                        RenderText(commandList, transform, childWidget, text);
                    }

                    return true;
                });
            });

            if (_lastRenderedWidgetType != WidgetType::None)
            {
                commandList.EndPipeline(currentPipeline);
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

    _font = Renderer::Font::GetDefaultFont(_renderer, 64.0f);
    _descriptorSet.Bind("_fontTextures"_h, _font->GetTextureArray());

    _vertices.SetDebugName("UIVertices");
    _panelDrawDatas.SetDebugName("PanelDrawDatas");
    _panelDrawDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _charDrawDatas.SetDebugName("CharDrawDatas");
    _charDrawDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    //_data.textVertices.SetDebugName("TextVertices");
    //_data.textVertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);
}

void CanvasRenderer::UpdatePanelVertices(ECS::Components::Transform2D& transform, ECS::Components::UI::Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate)
{
    std::vector<vec4>& vertices = _vertices.Get();

    // Add vertices if necessary
    if (panel.gpuVertexIndex == -1)
    {
        panel.gpuVertexIndex = static_cast<i32>(vertices.size());
        vertices.resize(vertices.size() + 6); // TODO: Indexing?
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
    vertices[panel.gpuVertexIndex + 0] = vec4(position, panelUVs[0]);
    vertices[panel.gpuVertexIndex + 1] = vec4(position + vec2(size.x, size.y), panelUVs[1]);
    vertices[panel.gpuVertexIndex + 2] = vec4(position + vec2(size.x, 0), panelUVs[2]);

    // Triangle 2
    vertices[panel.gpuVertexIndex + 3] = vec4(position + vec2(0, size.y), panelUVs[3]);
    vertices[panel.gpuVertexIndex + 4] = vec4(position + vec2(size.x, size.y), panelUVs[4]);
    vertices[panel.gpuVertexIndex + 5] = vec4(position, panelUVs[5]);

    _vertices.SetDirtyElements(panel.gpuVertexIndex, 6);
}

void CalculateVertices(const vec2& pos, const vec2& size, const vec2& relativePoint, std::vector<vec4>& vertices, u32 vertexIndex)
{
    vec2 lowerLeftPos = vec2(pos.x, pos.y);
    vec2 lowerRightPos = lowerLeftPos + vec2(size.x, 0.0f);
    vec2 upperLeftPos = lowerLeftPos + vec2(0.0f, size.y);
    vec2 upperRightPos = lowerLeftPos + vec2(size.x, size.y);

    // Vertices
    vec4 lowerLeft = vec4(lowerLeftPos, 0, 1);
    vec4 lowerRight = vec4(lowerRightPos, 1, 1);
    vec4 upperLeft = vec4(upperLeftPos, 0, 0);
    vec4 upperRight = vec4(upperRightPos, 1, 0);

    vertices[vertexIndex + 0] = upperLeft;
    vertices[vertexIndex + 1] = upperRight;
    vertices[vertexIndex + 2] = lowerRight;

    vertices[vertexIndex + 3] = lowerLeft;
    vertices[vertexIndex + 4] = upperLeft;
    vertices[vertexIndex + 5] = lowerRight;
}

void CanvasRenderer::UpdateTextVertices(ECS::Components::Transform2D& transform, ECS::Components::UI::Text& text, ECS::Components::UI::TextTemplate& textTemplate)
{
    std::vector<vec4>& vertices = _vertices.Get();

    if (text.sizeChanged)
    {
        utf8::iterator countIt(text.text.begin(), text.text.begin(), text.text.end());
        utf8::iterator coundEndIt(text.text.end(), text.text.begin(), text.text.end());

        // Count how many non whitspace characters there are
        i32 numCharsNonWhitespace = 0;
        for (; countIt != coundEndIt; countIt++)
        {
            unsigned int c = *countIt;

            if (c != ' ')
            {
                numCharsNonWhitespace++;
            }
        }

        text.numCharsNonWhitespace = numCharsNonWhitespace;
        text.sizeChanged = false;
    }

    // Add vertices if necessary
    if (text.gpuVertexIndex == -1 || text.hasGrown)
    {
        u32 numVertices = text.numCharsNonWhitespace * 6;

        text.gpuVertexIndex = static_cast<i32>(vertices.size());
        vertices.resize(vertices.size() + numVertices);
    }

    Renderer::Font* font = Renderer::Font::GetFont(_renderer, textTemplate.font, textTemplate.size);

    vec2 relativePoint = transform.GetRelativePoint();

    f32 scaledDescent = glm::ceil(font->descent * font->scale);

    utf8::iterator it(text.text.begin(), text.text.begin(), text.text.end());
    utf8::iterator endIt(text.text.end(), text.text.begin(), text.text.end());

    vec2 currentPos = transform.GetWorldPosition();
    
    f32 tallestCharacter = 0.0f;
    u32 vertexIndex = static_cast<u32>(text.gpuVertexIndex);
    for (; it != endIt; it++)
    {
        u32 c = *it;

        if (c == ' ')
        {
            currentPos.x += font->spaceGap;
            continue;
        }

        Renderer::FontChar& fontChar = font->GetChar(c);

        //vec2 pixelPos = currentPos + vec2(fontChar.xOffset, -(fontChar.yOffset + fontChar.height));
        vec2 pixelPos = currentPos - vec2(-(static_cast<f32>(fontChar.xOffset) - fontChar.leftSideBearing), static_cast<f32>(fontChar.yOffset) + fontChar.height + scaledDescent);
        vec2 pixelSize = vec2(fontChar.width, fontChar.height);
        tallestCharacter = std::max(tallestCharacter, pixelSize.y);

        vec2 position = PixelPosToNDC(pixelPos);
        vec2 size = PixelSizeToNDC(pixelSize);

        // Add vertices
        CalculateVertices(position, size, relativePoint, vertices, vertexIndex);
        vertexIndex += 6;

        currentPos.x += fontChar.advance;
    }

    _vertices.SetDirtyElements(text.gpuVertexIndex, text.numCharsNonWhitespace * 6);
}

void CanvasRenderer::UpdatePanelData(ECS::Components::Transform2D& transform, Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate)
{
    std::vector<PanelDrawData>& panelDrawDatas = _panelDrawDatas.Get();

    // Add draw data if necessary
    if (panel.gpuDataIndex == -1)
    {
        panel.gpuDataIndex = static_cast<i32>(panelDrawDatas.size());
        panelDrawDatas.resize(panelDrawDatas.size() + 1);
    }
    vec2 size = transform.GetSize();
    vec2 cornerRadius = vec2(panelTemplate.cornerRadius / size.x, panelTemplate.cornerRadius /size.y);

    // Update draw data
    auto& drawData = panelDrawDatas[panel.gpuDataIndex];
    drawData.packed.z = panelTemplate.color.ToRGBA32();
    drawData.cornerRadiusAndBorder = vec4(cornerRadius, 0.0f, 0.0f);

    // Update textures
    if (panelTemplate.background.empty())
    {
        drawData.packed.x = 0;
    }
    else
    {
        u32 textureNameHash = StringUtils::fnv1a_32(panelTemplate.background.c_str(), panelTemplate.background.size());

        if (_textureNameHashToIndex.contains(textureNameHash))
        {
            // Use already loaded texture
            drawData.packed.x = _textureNameHashToIndex[textureNameHash];
        }
        else
        {
            // Load texture
            Renderer::TextureDesc desc;
            desc.path = panelTemplate.background;

            u32 textureIndex;
            _renderer->LoadTextureIntoArray(desc, _textures, textureIndex);

            _textureNameHashToIndex[textureNameHash] = textureIndex;
            drawData.packed.x = textureIndex;
        }
    }

    if (panelTemplate.foreground.empty())
    {
        drawData.packed.y = 1;
    }
    else
    {
        u32 textureNameHash = StringUtils::fnv1a_32(panelTemplate.foreground.c_str(), panelTemplate.foreground.size());

        if (_textureNameHashToIndex.contains(textureNameHash))
        {
            // Use already loaded texture
            drawData.packed.y = _textureNameHashToIndex[textureNameHash];
        }
        else
        {
            // Load texture
            Renderer::TextureDesc desc;
            desc.path = panelTemplate.foreground;

            u32 textureIndex;
            _renderer->LoadTextureIntoArray(desc, _textures, textureIndex);

            _textureNameHashToIndex[textureNameHash] = textureIndex;
            drawData.packed.y = textureIndex;
        }
    }

    // Nine slicing
    const vec2& widgetSize = transform.GetSize();

    Renderer::TextureID textureID = _renderer->GetTextureID(_textures, drawData.packed.x);
    i32 texWidth = _renderer->GetTextureWidth(textureID);
    i32 texHeight = _renderer->GetTextureHeight(textureID);
    vec2 texSize = vec2(texWidth, texHeight);

    vec2 textureScaleToWidgetSize = texSize / widgetSize;
    drawData.textureScaleToWidgetSize = hvec2(textureScaleToWidgetSize.x, textureScaleToWidgetSize.y);
    drawData.texCoord = vec4(panelTemplate.texCoords.min, panelTemplate.texCoords.max);
    drawData.slicingCoord = vec4(panelTemplate.nineSliceCoords.min, panelTemplate.nineSliceCoords.max);

    _panelDrawDatas.SetDirtyElement(panel.gpuDataIndex);
}

void CanvasRenderer::UpdateTextData(Text& text, ECS::Components::UI::TextTemplate& textTemplate)
{
    std::vector<CharDrawData>& charDrawDatas = _charDrawDatas.Get();

    if (text.sizeChanged)
    {
        // Count how many non whitspace characters there are
        utf8::iterator countIt(text.text.begin(), text.text.begin(), text.text.end());
        utf8::iterator countEndIt(text.text.end(), text.text.begin(), text.text.end());

        text.numCharsNonWhitespace = 0;
        for (; countIt != countEndIt; countIt++)
        {
            unsigned int c = *countIt;

            if (c != ' ')
            {
                text.numCharsNonWhitespace++;
            }
        }

        text.sizeChanged = false;
    }

    // Add or update draw data if necessary
    if (text.gpuDataIndex == -1 || text.hasGrown)
    {
        text.gpuDataIndex = static_cast<i32>(charDrawDatas.size());
        charDrawDatas.resize(charDrawDatas.size() + text.numCharsNonWhitespace);
    }

    // Update CharDrawData
    Renderer::Font* font = Renderer::Font::GetFont(_renderer, textTemplate.font, textTemplate.size);

    // TODO: Manage these better to support drawing more than 1 font at the same time
    _descriptorSet.Bind("_fontTextures", font->GetTextureArray());

    utf8::iterator it(text.text.begin(), text.text.begin(), text.text.end());
    utf8::iterator endIt(text.text.end(), text.text.begin(), text.text.end());

    u32 charIndex = 0;
    for (; it != endIt; it++)
    {
        unsigned int c = *it;

        if (c == ' ')
        {
            continue;
        }

        Renderer::FontChar& fontChar = font->GetChar(c);

        auto& drawData = charDrawDatas[text.gpuDataIndex + charIndex];
        drawData.packed0.x = fontChar.textureIndex;
        drawData.packed0.y = charIndex;
        drawData.packed0.z = textTemplate.color.ToRGBA32();
        drawData.packed0.w = textTemplate.borderColor.ToRGBA32();

        //vec2 borderSizePixels = vec2(textTemplate.borderSize, textTemplate.borderSize);
        //vec2 borderSize = PixelSizeToNDC(borderSizePixels);//vec2(textTemplate.borderSize / fontChar.width, textTemplate.borderSize / fontChar.height);
        drawData.packed1.x = textTemplate.borderSize;

        f32 borderFade = glm::min(textTemplate.borderFade, 0.999f);
        drawData.packed1.y = borderFade;

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