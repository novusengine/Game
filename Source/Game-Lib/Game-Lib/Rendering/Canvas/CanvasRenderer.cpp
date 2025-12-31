#include "CanvasRenderer.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Panel.h"
#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
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
    _textureIDToIndex.clear();

    _renderer->UnloadTexturesInArray(_textures, 2);
}

CanvasRenderer::CanvasRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
    , _panelDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
    , _textDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
{
    CreatePermanentResources();
}

void CanvasRenderer::Update(f32 deltaTime)
{
    ZoneScoped;
    // Get active camera
    //entt::registry* gameRegistry = ServiceLocator::GetEnttRegistries()->gameRegistry;
    //auto& activeCamera = gameRegistry->ctx().get<ECS::Singletons::ActiveCamera>();

    //auto& cameraComp = gameRegistry->get<ECS::Components::Camera>(activeCamera.entity);
    //const mat4x4& worldToClip = cameraComp.worldToClip;

    // UI stuff
    entt::registry* uiRegistry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& transformSystem2D = ECS::Transform2DSystem::Get(*uiRegistry);

    uiRegistry->view<Widget, DestroyWidget>().each([&](entt::entity entity, Widget& widget)
    {
        if (widget.type == WidgetType::Canvas)
            return;

        if (widget.type == WidgetType::Panel)
        {
            auto& panel = uiRegistry->get<Panel>(entity);

            if (panel.gpuDataIndex != -1)
                _panelDrawDatas.Remove(panel.gpuDataIndex);

            if (panel.gpuVertexIndex != -1)
                _vertices.Remove(panel.gpuVertexIndex, 6);
        }
        else if (widget.type == WidgetType::Text)
        {
            auto& text = uiRegistry->get<Text>(entity);

            if (text.gpuDataIndex != -1)
                _charDrawDatas.Remove(text.gpuDataIndex, text.numCharsNonWhitespace);

            if (text.gpuVertexIndex != -1)
                _vertices.Remove(text.gpuVertexIndex, text.numCharsNonWhitespace * 6); // * 6 because 6 vertices per char
        }
        if (widget.scriptWidget != nullptr)
        {
            delete widget.scriptWidget;
            widget.scriptWidget = nullptr;
        }

        uiRegistry->destroy(entity);
    });
    uiRegistry->clear<DestroyWidget>();

    // Dirty Widget World Transform Index flag
    uiRegistry->view<Widget, DirtyWidgetWorldTransformIndex>().each([&](entt::entity entity, Widget& widget)
    {
        transformSystem2D.IterateChildrenRecursiveBreadth(entity, [&](entt::entity childEntity)
        {
            auto& childWidget = uiRegistry->get<Widget>(childEntity);
            childWidget.worldTransformIndex = widget.worldTransformIndex;
        });
    });
    uiRegistry->clear<DirtyWidgetWorldTransformIndex>();

    // Dirty widget flags
    uiRegistry->view<Widget, EventInputInfo, DirtyWidgetFlags>().each([&](entt::entity entity, Widget& widget, EventInputInfo& eventInputInfo)
    {
        bool wasInteractable = eventInputInfo.isInteractable;
        eventInputInfo.isInteractable = (widget.flags & WidgetFlags::Interactable) == WidgetFlags::Interactable;

        if (wasInteractable != eventInputInfo.isInteractable)
        {
            ECS::Util::UI::RefreshTemplate(uiRegistry, entity, eventInputInfo);
        }
    });

    // Dirty transforms
    uiRegistry->view<DirtyWidgetTransform>().each([&](entt::entity entity)
    {
        auto& widget = uiRegistry->get<Widget>(entity);

        if (widget.type == WidgetType::Canvas)
        {
            return; // Nothing to do for canvas
        }

        auto& canvasComponent = uiRegistry->get<Canvas>(widget.scriptWidget->canvasEntity);
        vec2 refSize = vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
        vec2 size = refSize;

        if (canvasComponent.renderTexture != Renderer::TextureID::Invalid())
        {
            auto& canvasTransform = uiRegistry->get<ECS::Components::Transform2D>(widget.scriptWidget->canvasEntity);
            size = canvasTransform.GetSize();
        }

        auto& transform = uiRegistry->get<ECS::Components::Transform2D>(entity);
        if (widget.type == WidgetType::Panel)
        {
            auto& panel = uiRegistry->get<Panel>(entity);
            auto& panelTemplate = uiRegistry->get<PanelTemplate>(entity);

            // In pixel units
            vec2 panelPos = transform.GetWorldPosition();
            vec2 panelSize = transform.GetSize();

            // Convert to clip space units
            panelSize = PixelSizeToNDC(panelSize, size);

            if (widget.worldTransformIndex != std::numeric_limits<u32>().max())
            {
                panelPos = (panelPos / refSize) * 2.0f;
            }
            else
            {
                // Convert to clip space units
                panelPos = PixelPosToNDC(panelPos, size);
            }

            UpdatePanelVertices(panelPos, panelSize, panel, panelTemplate);
        }
        else if (widget.type == WidgetType::Text)
        {
            auto& text = uiRegistry->get<Text>(entity);
            auto& textTemplate = uiRegistry->get<TextTemplate>(entity);

            UpdateTextVertices(widget, transform, text, textTemplate, size);
        }
    });
    uiRegistry->clear<DirtyWidgetTransform>();

    // Update clipper data
    uiRegistry->view<DirtyChildClipper>().each([&](entt::entity entity)
    {
        auto& clipper = uiRegistry->get<Clipper>(entity);

        transformSystem2D.IterateChildrenRecursiveBreadth(entity, [&](entt::entity childEntity)
        {
            auto& childClipper = uiRegistry->get<Clipper>(childEntity);
            childClipper.clipRegionOverrideEntity = (clipper.clipChildren) ? entity : entt::null;

            uiRegistry->emplace_or_replace<DirtyWidgetData>(childEntity);
        });
    });

    // Dirty widget datas
    uiRegistry->view<DirtyWidgetData>().each([&](entt::entity entity)
    {
        auto& widget = uiRegistry->get<Widget>(entity);
        if (widget.type == WidgetType::Canvas)
        {
            //auto& canvas = registry->get<Canvas>(entity);
            // Update canvas data
        }
        else if (widget.type == WidgetType::Panel)
        {
            ECS::Components::Transform2D& transform = uiRegistry->get<ECS::Components::Transform2D>(entity);
            auto& panel = uiRegistry->get<Panel>(entity);
            auto& panelTemplate = uiRegistry->get<PanelTemplate>(entity);

            UpdatePanelData(entity, transform, panel, panelTemplate);
        }
        else if (widget.type == WidgetType::Text)
        {
            auto& text = uiRegistry->get<Text>(entity);
            auto& textTemplate = uiRegistry->get<TextTemplate>(entity);

            UpdateTextData(entity, text, textTemplate);
            text.hasGrown = false;
        }
    });

    if (_vertices.SyncToGPU(_renderer))
    {
        _panelDescriptorSet.Bind("_vertices", _vertices.GetBuffer());
        _textDescriptorSet.Bind("_vertices", _vertices.GetBuffer());
    }

    if (_panelDrawDatas.SyncToGPU(_renderer))
    {
        _panelDescriptorSet.Bind("_panelDrawDatas", _panelDrawDatas.GetBuffer());
    }

    if (_charDrawDatas.SyncToGPU(_renderer))
    {
        _textDescriptorSet.Bind("_charDrawDatas", _charDrawDatas.GetBuffer());
    }

    if (_widgetWorldPositions.SyncToGPU(_renderer))
    {
        _panelDescriptorSet.Bind("_widgetWorldPositions", _widgetWorldPositions.GetBuffer());
        _textDescriptorSet.Bind("_widgetWorldPositions", _widgetWorldPositions.GetBuffer());
    }

    uiRegistry->clear<DirtyWidgetData>();
    uiRegistry->clear<DirtyWidgetFlags>();
    uiRegistry->clear<DirtyChildClipper>();
    uiRegistry->clear<DirtyClipper>();
}

u32 CanvasRenderer::ReserveWorldTransform()
{
    return _widgetWorldPositions.Add();
}

void CanvasRenderer::ReleaseWorldTransform(u32 index)
{
    _widgetWorldPositions.Remove(index);
}

void CanvasRenderer::UpdateWorldTransform(u32 index, const vec3& position)
{
    _widgetWorldPositions[index] = vec4(position, 1.0);
    _widgetWorldPositions.SetDirtyElement(index);
}

void CanvasRenderer::AddCanvasPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
    struct Data
    {
        Renderer::ImageMutableResource target;

        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource panelDescriptorSet;
        Renderer::DescriptorSetResource textDescriptorSet;
    };
    renderGraph->AddPass<Data>("Canvases",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.target = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);

            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);

            builder.Read(_panelDrawDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_charDrawDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_widgetWorldPositions.GetBuffer(), BufferUsage::GRAPHICS);

            data.globalDescriptorSet = builder.Use(resources.globalDescriptorSet);
            data.panelDescriptorSet = builder.Use(_panelDescriptorSet);
            data.textDescriptorSet = builder.Use(_textDescriptorSet);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender2D);
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& transform2DSystem = ECS::Transform2DSystem::Get(*registry);

            Renderer::GraphicsPipelineID currentPipeline;
            _lastRenderedWidgetType = WidgetType::None;

            // Loop over dirty rendertarget canvases
            registry->view<Canvas, CanvasRenderTargetTag, DirtyCanvasTag>().each([&](auto entity, auto& canvas)
            {
                Renderer::TextureBaseDesc textureDesc = _renderer->GetDesc(canvas.renderTexture);
                commandList.SetViewport(0, 0, static_cast<f32>(textureDesc.width), static_cast<f32>(textureDesc.height), 0.0f, 1.0f);
                commandList.SetScissorRect(0, static_cast<u32>(textureDesc.width), 0, static_cast<u32>(textureDesc.height));

                Renderer::TextureRenderPassDesc renderPassDesc;
                renderPassDesc.renderTargets[0] = canvas.renderTexture;
                renderPassDesc.clearRenderTargets[0] = true;
                bool hasDrawn = false;

                // Loop over children recursively (depth first)
                transform2DSystem.IterateChildrenRecursiveDepth(entity, [&, registry](auto childEntity)
                {
                    auto& transform = registry->get<ECS::Components::Transform2D>(childEntity);
                    auto& childWidget = registry->get<Widget>(childEntity);

                    if (!childWidget.IsVisible())
                        return false; // Skip invisible widgets

                    if (childWidget.type == WidgetType::Canvas)
                        return true; // There is nothing to draw for a canvas

                    if (!hasDrawn)
                    {
                        commandList.PushMarker("RT Canvas: " + canvas.name, Color::PastelOrange);
                        commandList.BeginRenderPass(renderPassDesc);
                        hasDrawn = true;
                    }

                    if (ChangePipelineIfNecessary(commandList, currentPipeline, childWidget.type))
                    {
                        if (childWidget.type == WidgetType::Panel)
                        {
                            commandList.BindDescriptorSet(data.panelDescriptorSet, frameIndex);
                        }
                        else if (childWidget.type == WidgetType::Text)
                        {
                            commandList.BindDescriptorSet(data.textDescriptorSet, frameIndex);
                        }
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

                if (hasDrawn)
                {
                    commandList.EndPipeline(currentPipeline);
                    commandList.EndRenderPass(renderPassDesc);
                    commandList.PopMarker();
                }
            });

            _lastRenderedWidgetType = WidgetType::None;

            vec2 renderSize = _renderer->GetRenderSize();
            commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
            commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));

            // Loop over regular canvases
            Renderer::RenderPassDesc mainRenderPassDesc;
            graphResources.InitializeRenderPassDesc(mainRenderPassDesc);
            mainRenderPassDesc.renderTargets[0] = data.target;
            commandList.BeginRenderPass(mainRenderPassDesc);

            registry->view<Canvas>(entt::exclude<CanvasRenderTargetTag>).each([&](auto entity, auto& canvas)
            {
                bool hasDrawn = false;

                // Loop over children recursively (depth first)
                transform2DSystem.IterateChildrenRecursiveDepth(entity, [&, registry](auto childEntity)
                {
                    auto& transform = registry->get<ECS::Components::Transform2D>(childEntity);
                    auto& childWidget = registry->get<Widget>(childEntity);

                    if (!childWidget.IsVisible())
                        return false; // Skip invisible widgets

                    if (childWidget.type == WidgetType::Canvas)
                        return true; // There is nothing to draw for a canvas

                    if (!hasDrawn)
                    {
                        commandList.PushMarker("Canvas: " + canvas.name, Color::PastelOrange);
                        hasDrawn = true;
                    }

                    if (ChangePipelineIfNecessary(commandList, currentPipeline, childWidget.type))
                    {
                        commandList.BindDescriptorSet(data.globalDescriptorSet, frameIndex);
                        if (childWidget.type == WidgetType::Panel)
                        {
                            commandList.BindDescriptorSet(data.panelDescriptorSet, frameIndex);
                        }
                        else if (childWidget.type == WidgetType::Text)
                        {
                            commandList.BindDescriptorSet(data.textDescriptorSet, frameIndex);
                        }
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

                if (hasDrawn)
                {
                    commandList.PopMarker();
                }
            });

            if (_lastRenderedWidgetType != WidgetType::None)
            {
                commandList.EndPipeline(currentPipeline);
            }
            commandList.EndRenderPass(mainRenderPassDesc);

            registry->clear<DirtyCanvasTag>();
        });
}

void CanvasRenderer::CreatePermanentResources()
{
    CreatePipelines();
    InitDescriptorSets();

    Renderer::TextureArrayDesc textureArrayDesc;
    textureArrayDesc.size = 4096;

    _textures = _renderer->CreateTextureArray(textureArrayDesc);
    _panelDescriptorSet.Bind("_textures", _textures);
    _textDescriptorSet.Bind("_textures", _textures);

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
    _panelDescriptorSet.Bind("_sampler"_h, _sampler);
    _textDescriptorSet.Bind("_sampler"_h, _sampler);

    textureArrayDesc.size = 256;
    _fontTextures = _renderer->CreateTextureArray(textureArrayDesc);

    _font = Renderer::Font::GetDefaultFont(_renderer);
    _renderer->AddTextureToArray(_font->GetTextureID(), _fontTextures);

    _textDescriptorSet.Bind("_fontTextures"_h, _fontTextures);

    _vertices.SetDebugName("UIVertices");
    _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _panelDrawDatas.SetDebugName("PanelDrawDatas");
    _panelDrawDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _charDrawDatas.SetDebugName("CharDrawDatas");
    _charDrawDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _widgetWorldPositions.SetDebugName("WidgetWorldPositions");
    _widgetWorldPositions.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    // Push a debug position
    _widgetWorldPositions.Add(vec4(0, 0, 0, 1));
}

void CanvasRenderer::CreatePipelines()
{
    // Create pipelines
    Renderer::ImageFormat renderTargetFormat = _renderer->GetSwapChainImageFormat();

    {
        Renderer::GraphicsPipelineDesc pipelineDesc;

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

        // Render targets.
        pipelineDesc.states.renderTargetFormats[0] = renderTargetFormat;

        // Shader
        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("UI/Panel.vs"_h, "UI/Panel.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("UI/Panel.ps"_h, "UI/Panel.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        // Blending
        pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
        pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
        pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
        pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
        pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;

        _panelPipeline = _renderer->CreatePipeline(pipelineDesc);
    }

    {
        Renderer::GraphicsPipelineDesc pipelineDesc;

        // Rasterizer state
        pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

        // Render targets.
        pipelineDesc.states.renderTargetFormats[0] = renderTargetFormat;

        // Shader
        Renderer::VertexShaderDesc vertexShaderDesc;
        vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("UI/Text.vs"_h, "UI/Text.vs");
        pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

        Renderer::PixelShaderDesc pixelShaderDesc;
        pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("UI/Text.ps"_h, "UI/Text.ps");
        pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

        // Blending
        pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
        pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
        pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
        pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
        pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;

        _textPipeline = _renderer->CreatePipeline(pipelineDesc);
    }
}

void CanvasRenderer::InitDescriptorSets()
{
    _panelDescriptorSet.RegisterPipeline(_renderer, _panelPipeline);
    _panelDescriptorSet.Init(_renderer);
    _textDescriptorSet.RegisterPipeline(_renderer, _textPipeline);
    _textDescriptorSet.Init(_renderer);
    
}

void CanvasRenderer::UpdatePanelVertices(const vec2& clipPos, const vec2& clipSize, ECS::Components::UI::Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate)
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
    // Triangle 1
    _vertices[panel.gpuVertexIndex + 0] = vec4(clipPos, panelUVs[0]);
    _vertices[panel.gpuVertexIndex + 1] = vec4(clipPos + vec2(clipSize.x, clipSize.y), panelUVs[1]);
    _vertices[panel.gpuVertexIndex + 2] = vec4(clipPos + vec2(clipSize.x, 0), panelUVs[2]);

    // Triangle 2
    _vertices[panel.gpuVertexIndex + 3] = vec4(clipPos + vec2(0, clipSize.y), panelUVs[3]);
    _vertices[panel.gpuVertexIndex + 4] = vec4(clipPos + vec2(clipSize.x, clipSize.y), panelUVs[4]);
    _vertices[panel.gpuVertexIndex + 5] = vec4(clipPos, panelUVs[5]);

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

void CanvasRenderer::UpdateTextVertices(ECS::Components::UI::Widget& widget, ECS::Components::Transform2D& transform, ECS::Components::UI::Text& text, ECS::Components::UI::TextTemplate& textTemplate, const vec2& canvasSize)
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
    vec2 refSize = vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

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

            vec2 planeMin;
            vec2 planeMax;
            if (widget.worldTransformIndex != std::numeric_limits<u32>().max())
            {
                planeMin = (vec2(planeLeft, planeBottom) / refSize) * 2.0f;
                planeMax = (vec2(planeRight, planeTop) / refSize) * 2.0f;
            }
            else
            {
                // Convert the plane coordinates to NDC space
                planeMin = PixelPosToNDC(vec2(planeLeft, planeBottom), canvasSize);
                planeMax = PixelPosToNDC(vec2(planeRight, planeTop), canvasSize);
            }

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

void CanvasRenderer::UpdatePanelData(entt::entity entity, ECS::Components::Transform2D& transform, Panel& panel, ECS::Components::UI::PanelTemplate& panelTemplate)
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

    if (panelTemplate.setFlags.backgroundRT)
    {
        if (panelTemplate.backgroundRT != Renderer::TextureID::Invalid())
        {
            textureIndex = AddTexture(panelTemplate.backgroundRT);
        }
    }
    else
    {
        if (!panelTemplate.background.empty())
        {
            textureIndex = LoadTexture(panelTemplate.background);
        }
    }

    if (!panelTemplate.foreground.empty())
    {
        additiveTextureIndex = LoadTexture(panelTemplate.foreground);
    }

    drawData.packed0.x = (textureIndex & 0xFFFF) | ((additiveTextureIndex & 0xFFFF) << 16);

    // Nine slicing
    const vec2& widgetSize = transform.GetSize();

    Renderer::TextureID textureID = _renderer->GetTextureID(_textures, textureIndex);
    Renderer::TextureBaseDesc textureBaseDesc = _renderer->GetDesc(textureID);
    vec2 texSize = vec2(textureBaseDesc.width, textureBaseDesc.height);

    vec2 textureScaleToWidgetSize = texSize / widgetSize;
    drawData.textureScaleToWidgetSize = hvec2(textureScaleToWidgetSize.x, textureScaleToWidgetSize.y);
    drawData.texCoord = vec4(panelTemplate.texCoords.min, panelTemplate.texCoords.max);
    drawData.slicingCoord = vec4(panelTemplate.nineSliceCoords.min, panelTemplate.nineSliceCoords.max);

    // Clipping
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    // Get the correct clipper
    auto* clipper = &registry->get<Clipper>(entity);
    BoundingRect* boundingRect = &registry->get<BoundingRect>(entity);
    
    vec2 referenceSize = vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
    vec2 clipRegionMin = clipper->clipRegionMin;
    vec2 clipRegionMax = clipper->clipRegionMax;

    if (clipper->clipRegionOverrideEntity != entt::null)
    {
        boundingRect = &registry->get<BoundingRect>(clipper->clipRegionOverrideEntity);
        clipper = &registry->get<Clipper>(clipper->clipRegionOverrideEntity);

        clipRegionMin = (boundingRect->min + (boundingRect->max - boundingRect->min) * clipper->clipRegionMin) / referenceSize;
        clipRegionMax = (boundingRect->min + (boundingRect->max - boundingRect->min) * clipper->clipRegionMax) / referenceSize;
    }

    vec2 scaledClipMaskRegionMin = boundingRect->min / referenceSize;
    vec2 scaledClipMaskRegionMax = boundingRect->max / referenceSize;
    
    drawData.packed0.y = (clipper->hasClipMaskTexture) ? LoadTexture(clipper->clipMaskTexture) : 0;
    drawData.clipRegionRect = vec4(clipRegionMin, clipRegionMax);
    drawData.clipMaskRegionRect = vec4(scaledClipMaskRegionMin, scaledClipMaskRegionMax);

    // World position UI
    auto& widget = registry->get<ECS::Components::UI::Widget>(entity);
    drawData.worldPositionIndex = widget.worldTransformIndex;

    _panelDrawDatas.SetDirtyElement(panel.gpuDataIndex);
}

void CanvasRenderer::UpdateTextData(entt::entity entity, Text& text, ECS::Components::UI::TextTemplate& textTemplate)
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

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    // Get the correct clipper
    auto* clipper = &registry->get<Clipper>(entity);
    BoundingRect* boundingRect = &registry->get<BoundingRect>(entity);

    vec2 referenceSize = vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
    vec2 clipRegionMin = vec2(0.0f, 0.0f);
    vec2 clipRegionMax = vec2(1.0f, 1.0f);

    if (clipper->clipRegionOverrideEntity != entt::null)
    {
        boundingRect = &registry->get<BoundingRect>(clipper->clipRegionOverrideEntity);
        clipper = &registry->get<Clipper>(clipper->clipRegionOverrideEntity);

        clipRegionMin = (boundingRect->min + (boundingRect->max - boundingRect->min) * clipper->clipRegionMin) / referenceSize;
        clipRegionMax = (boundingRect->min + (boundingRect->max - boundingRect->min) * clipper->clipRegionMax) / referenceSize;
    }

    vec2 scaledClipMaskRegionMin = boundingRect->min / referenceSize;
    vec2 scaledClipMaskRegionMax = boundingRect->max / referenceSize;

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
        drawData.packed0.x = (fontTextureIndex & 0xFFFF) | ((charIndex & 0xFFFF) << 16);
        drawData.packed0.z = textTemplate.color.ToRGBA32();
        drawData.packed0.w = textTemplate.borderColor.ToRGBA32();

        drawData.packed1.x = textTemplate.borderSize;

        // Unit range
        f32 distanceRange = font->upperPixelRange - font->lowerPixelRange;
        drawData.packed1.z = distanceRange / font->width;
        drawData.packed1.w = distanceRange / font->height;

        // Clipping
        drawData.packed0.y = (clipper->hasClipMaskTexture) ? LoadTexture(clipper->clipMaskTexture) : 0;
        drawData.clipRegionRect = vec4(clipRegionMin, clipRegionMax);
        drawData.clipMaskRegionRect = vec4(scaledClipMaskRegionMin, scaledClipMaskRegionMax);

        // World position UI
        auto& widget = registry->get<ECS::Components::UI::Widget>(entity);
        drawData.worldPositionIndex = widget.worldTransformIndex;

        charIndex++;
    }
    _charDrawDatas.SetDirtyElements(text.gpuDataIndex, text.numCharsNonWhitespace);
}

bool CanvasRenderer::ChangePipelineIfNecessary(Renderer::CommandList& commandList, Renderer::GraphicsPipelineID& currentPipeline, ECS::Components::UI::WidgetType widgetType)
{
    if (_lastRenderedWidgetType != widgetType)
    {
        if (_lastRenderedWidgetType != WidgetType::None)
        {
            commandList.EndPipeline(currentPipeline);
        }

        _lastRenderedWidgetType = widgetType;

        if (widgetType == WidgetType::Panel)
        {
            currentPipeline = _panelPipeline;
        }
        else
        {
            currentPipeline = _textPipeline;
        }

        commandList.BeginPipeline(currentPipeline);
        return true;
    }
    return false;
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

vec2 CanvasRenderer::PixelPosToNDC(const vec2& pixelPosition, const vec2& screenSize) const
{
    return vec2(2.0 * pixelPosition.x / screenSize.x - 1.0, 2.0 * pixelPosition.y / screenSize.y - 1.0);
}

vec2 CanvasRenderer::PixelSizeToNDC(const vec2& pixelSize, const vec2& screenSize) const
{
    return vec2(2.0 * pixelSize.x / screenSize.x, 2.0 * pixelSize.y / screenSize.y);
}

u32 CanvasRenderer::AddTexture(Renderer::TextureID textureID)
{
    Renderer::TextureID::type typedID = static_cast<Renderer::TextureID::type>(textureID);
    if (_textureIDToIndex.contains(typedID))
    {
        // Use already added texture
        return _textureIDToIndex[typedID];
    }

    // Add texture
    u32 textureIndex = _renderer->AddTextureToArray(textureID, _textures);

    _textureIDToIndex[typedID] = textureIndex;
    return textureIndex;
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
