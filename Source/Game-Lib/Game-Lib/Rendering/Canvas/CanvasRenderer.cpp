#include "CanvasRenderer.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/LayoutEventInfo.h"
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
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Scripting/Zenith.h>

#include <Input/InputManager.h>

#include <Renderer/Renderer.h>
#include <Renderer/GPUVector.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/Font.h>

#include <entt/entt.hpp>
#include <utfcpp/utf8.h>

#include <algorithm>
#include <cstring>

using namespace ECS::Components::UI;

void CanvasRenderer::Clear()
{
    _vertices.Clear();
    _widgetDrawDatas.Clear();
    _textureNameHashToIndex.clear();
    _textureIDToIndex.clear();

    _renderer->UnloadTexturesInArray(_textures, 2);
}

CanvasRenderer::CanvasRenderer(Renderer::Renderer* renderer, GameRenderer* gameRenderer, DebugRenderer* debugRenderer)
    : _renderer(renderer)
    , _gameRenderer(gameRenderer)
    , _debugRenderer(debugRenderer)
    , _widgetDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS)
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

    // Lua layouts: refresh any whose Lua-side state was invalidated since the last frame.
    // Layouts are lazy-refresh-on-read in Lua (LinearLayout/GridLayout), so they otherwise
    // never run unless something explicitly reads GetMeasured*. The refresh closure (set
    // via Widget:RegisterLayoutRefresh) runs `self:Refresh()` which writes positions/sizes
    // via SetPos/SetWidth/SetHeight; those calls cascade into DirtyCanvasTag /
    // DirtyCanvasSort / DirtyWidgetData and are picked up by the passes below.
    {
        auto layoutView = uiRegistry->view<LayoutEventInfo, DirtyLayoutTag>();
        if (layoutView.begin() != layoutView.end())
        {
            ZoneScopedN("CanvasRenderer::Update::LayoutRefresh");
            ::Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();
            if (zenith != nullptr)
            {
                // TODO(layouts perf): each invalidated layout costs one Lua PCall. For
                // deeply nested UI where a single mutation cascades up N levels, that's N
                // PCalls per frame. Profile if it shows up; the obvious optimisation is to
                // gather all dirty refresh closures into a single PCall (push the closures
                // as a stack of values and let one Lua function dispatch them).
                for (entt::entity entity : layoutView)
                {
                    i32 ref = layoutView.get<LayoutEventInfo>(entity).onLayoutRefresh;
                    if (ref == -1) continue;

                    zenith->GetRawI(LUA_REGISTRYINDEX, ref);
                    zenith->PCall(0, 0);
                }
                uiRegistry->clear<DirtyLayoutTag>();
            }
        }
    }

    uiRegistry->view<Widget, DestroyWidget>().each([&](entt::entity entity, Widget& widget)
    {
        if (widget.type == WidgetType::Canvas)
        {
            // RT canvases own retained GPU buffers (finalSortedArgs + finalCount). Destroy them
            // and erase the bucket so we don't leak per-canvas allocations on dynamic UI churn.
            // Non-RT canvases all share _mainBucket, which is process-lifetime and not freed here.
            if (uiRegistry->all_of<CanvasRenderTargetTag>(entity))
            {
                auto it = _rtBuckets.find(entity);
                if (it != _rtBuckets.end())
                {
                    if (it->second.finalSortedArgs != Renderer::BufferID::Invalid())
                        _renderer->QueueDestroyBuffer(it->second.finalSortedArgs);
                    if (it->second.finalCount != Renderer::BufferID::Invalid())
                        _renderer->QueueDestroyBuffer(it->second.finalCount);
                    _rtBuckets.erase(it);
                }
            }
            return;
        }

        if (widget.type == WidgetType::Panel)
        {
            auto& panel = uiRegistry->get<Panel>(entity);

            if (panel.gpuDataIndex != -1)
                _widgetDrawDatas.Remove(panel.gpuDataIndex);

            if (panel.gpuVertexIndex != -1)
                _vertices.Remove(panel.gpuVertexIndex, 6);
        }
        else if (widget.type == WidgetType::Text)
        {
            auto& text = uiRegistry->get<Text>(entity);

            if (text.gpuDataIndex != -1)
                _widgetDrawDatas.Remove(text.gpuDataIndex, text.numCharsNonWhitespace);

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
        _widgetDescriptorSet.Bind("_vertices", _vertices.GetBuffer());
    }

    if (_widgetDrawDatas.SyncToGPU(_renderer))
    {
        _widgetDescriptorSet.Bind("_widgetDrawDatas", _widgetDrawDatas.GetBuffer());
    }

    if (_widgetWorldPositions.SyncToGPU(_renderer))
    {
        _widgetDescriptorSet.Bind("_widgetWorldPositions", _widgetWorldPositions.GetBuffer());
    }

    // Rebuild sort-keys + refresh dirty buckets in one combined pass.
    //
    // DirtyCanvasSort is set by every operation that changes a canvas's draw ORDER (widget
    // create/destroy, focus change, reparent). DirtyCanvasOrderFlag is a registry-context
    // singleton set when the canvas SET itself changed (canvas create/destroy/SetLayer); it
    // gates the (relatively expensive) RebuildCanvasOrder pass.
    //
    // Bucket refresh is driven by DirtyCanvasSort -- NOT DirtyCanvasTag. DirtyCanvasTag fires
    // for any visual mutation (color, text content, etc.) which doesn't require a re-sort; its
    // only remaining job is gating which RT canvases get re-DRAWN by AddCanvasPass.
    {
        auto dirtySortView = uiRegistry->view<Canvas, DirtyCanvasSort>();
        if (dirtySortView.begin() != dirtySortView.end())
        {
            if (uiRegistry->ctx().contains<DirtyCanvasOrderFlag>())
            {
                RebuildCanvasOrder(uiRegistry);
                uiRegistry->ctx().erase<DirtyCanvasOrderFlag>();
            }

            bool mainBucketDirty = false;
            dirtySortView.each([&](entt::entity canvasEntity, Canvas&)
            {
                u8 canvasOrder = _canvasOrderByEntity.at(canvasEntity);
                u32 traversalIndex = 0;
                u8 rootPriority = ResolvePriority(uiRegistry, canvasEntity);
                DfsAssignSortKey(uiRegistry, canvasEntity, canvasOrder, traversalIndex, rootPriority);

                if (uiRegistry->all_of<CanvasRenderTargetTag>(canvasEntity))
                    RefreshBucketCPU(uiRegistry, canvasEntity, /*isRT=*/true);
                else
                    mainBucketDirty = true;
            });
            if (mainBucketDirty)
                RefreshBucketCPU(uiRegistry, entt::null, /*isRT=*/false);

            uiRegistry->clear<DirtyCanvasSort>();
        }
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
    // --- "Canvases" (graphics) -----------------------------------------------------------------
    // Per bucket, bind its retained finalSortedArgs + finalCount and issue one DrawIndirectCount.
    // finalSortedArgs is populated CPU-side by RefreshBucketCPU via std::sort + UploadToBuffer;
    // this pass just consumes it.
    struct Data
    {
        Renderer::ImageMutableResource target;

        // Per-bucket buffer resources, in the same order as _drawBuckets below. Each element i
        // corresponds to a {RT canvas or main} DrawIndirectCount call.
        std::vector<Renderer::BufferResource> argBuffers;
        std::vector<Renderer::BufferResource> countBuffers;
        std::vector<entt::entity>             bucketCanvasEntities; // entt::null for the main bucket

        Renderer::DescriptorSetResource globalDescriptorSet;
        Renderer::DescriptorSetResource widgetDescriptorSet;
    };
    renderGraph->AddPass<Data>("Canvases",
        [this, &resources](Data& data, Renderer::RenderGraphBuilder& builder) // Setup
        {
            using BufferUsage = Renderer::BufferPassUsage;

            data.target = builder.Write(resources.sceneColor, Renderer::PipelineType::GRAPHICS, Renderer::LoadMode::LOAD);

            builder.Read(resources.cameras.GetBuffer(), BufferUsage::GRAPHICS);

            builder.Read(_vertices.GetBuffer(), BufferUsage::GRAPHICS);

            builder.Read(_widgetDrawDatas.GetBuffer(), BufferUsage::GRAPHICS);
            builder.Read(_widgetWorldPositions.GetBuffer(), BufferUsage::GRAPHICS);

            // Register each drawable bucket's retained final buffers.
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            // RT canvases: only dirty ones draw this frame.
            registry->view<Canvas, CanvasRenderTargetTag, DirtyCanvasTag>().each(
                [&](entt::entity canvasEntity, Canvas&)
                {
                    auto it = _rtBuckets.find(canvasEntity);
                    if (it == _rtBuckets.end() || it->second.drawCount == 0)
                        return;
                    BucketResources& b = it->second;
                    data.argBuffers.push_back(builder.Read(b.finalSortedArgs, BufferUsage::GRAPHICS));
                    data.countBuffers.push_back(builder.Read(b.finalCount,     BufferUsage::GRAPHICS));
                    data.bucketCanvasEntities.push_back(canvasEntity);
                });

            // Main bucket: always drawn if non-empty.
            if (_mainBucket.drawCount > 0)
            {
                data.argBuffers.push_back(builder.Read(_mainBucket.finalSortedArgs, BufferUsage::GRAPHICS));
                data.countBuffers.push_back(builder.Read(_mainBucket.finalCount,     BufferUsage::GRAPHICS));
                data.bucketCanvasEntities.push_back(entt::null);
            }

            data.globalDescriptorSet = builder.Use(resources.globalDescriptorSet);
            data.widgetDescriptorSet = builder.Use(_widgetDescriptorSet);

            return true;// Return true from setup to enable this pass, return false to disable it
        },
        [this, &resources, frameIndex](Data& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
        {
            GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender2D);
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            vec2 renderSize = _renderer->GetRenderSize();

            // Single instance, used for the main bucket's BeginRenderPass during the loop AND for
            // EndRenderPass after the loop. Avoids the previous "init this struct three separate
            // times" dance.
            Renderer::RenderPassDesc mainRenderPassDesc;
            graphResources.InitializeRenderPassDesc(mainRenderPassDesc);
            mainRenderPassDesc.renderTargets[0] = data.target;

            bool mainRenderPassOpen = false;

            for (size_t i = 0; i < data.bucketCanvasEntities.size(); ++i)
            {
                entt::entity canvasEntity = data.bucketCanvasEntities[i];
                const bool isMain = (canvasEntity == entt::null);

                u32 drawCount = 0;
                if (isMain)
                {
                    drawCount = _mainBucket.drawCount;
                }
                else
                {
                    auto it = _rtBuckets.find(canvasEntity);
                    drawCount = (it == _rtBuckets.end()) ? 0 : it->second.drawCount;
                }
                if (drawCount == 0)
                    continue;

                if (!isMain)
                {
                    auto& canvas = registry->get<Canvas>(canvasEntity);
                    Renderer::TextureBaseDesc textureDesc = _renderer->GetDesc(canvas.renderTexture);
                    commandList.SetViewport(0, 0, static_cast<f32>(textureDesc.width), static_cast<f32>(textureDesc.height), 0.0f, 1.0f);
                    commandList.SetScissorRect(0, static_cast<u32>(textureDesc.width), 0, static_cast<u32>(textureDesc.height));

                    Renderer::TextureRenderPassDesc renderPassDesc;
                    renderPassDesc.renderTargets[0] = canvas.renderTexture;
                    renderPassDesc.clearRenderTargets[0] = true;

                    commandList.PushMarker("RT Canvas: " + canvas.name, Color::PastelOrange);
                    commandList.BeginRenderPass(renderPassDesc);
                    commandList.BeginPipeline(_widgetPipeline);
                    commandList.BindDescriptorSet(data.globalDescriptorSet, frameIndex);
                    commandList.BindDescriptorSet(data.widgetDescriptorSet, frameIndex);
                    commandList.DrawIndirectCount(data.argBuffers[i], 0, data.countBuffers[i], 0, drawCount);
                    commandList.EndPipeline(_widgetPipeline);
                    commandList.EndRenderPass(renderPassDesc);
                    commandList.PopMarker();
                }
                else
                {
                    if (!mainRenderPassOpen)
                    {
                        commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
                        commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
                        commandList.BeginRenderPass(mainRenderPassDesc);
                        mainRenderPassOpen = true;
                    }
                    commandList.BeginPipeline(_widgetPipeline);
                    commandList.BindDescriptorSet(data.globalDescriptorSet, frameIndex);
                    commandList.BindDescriptorSet(data.widgetDescriptorSet, frameIndex);
                    commandList.DrawIndirectCount(data.argBuffers[i], 0, data.countBuffers[i], 0, drawCount);
                    commandList.EndPipeline(_widgetPipeline);
                }
            }

            // Always end the frame with the main render pass closed. If nothing got drawn into
            // main (zero non-RT canvas draws), still open+close so downstream passes see a clean
            // sceneColor attachment state.
            if (!mainRenderPassOpen)
            {
                commandList.SetViewport(0, 0, renderSize.x, renderSize.y, 0.0f, 1.0f);
                commandList.SetScissorRect(0, static_cast<u32>(renderSize.x), 0, static_cast<u32>(renderSize.y));
                commandList.BeginRenderPass(mainRenderPassDesc);
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
    _widgetDescriptorSet.Bind("_textures", _textures);

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
    _widgetDescriptorSet.Bind("_sampler"_h, _sampler);

    textureArrayDesc.size = 256;
    _fontTextures = _renderer->CreateTextureArray(textureArrayDesc);

    _font = Renderer::Font::GetDefaultFont(_renderer);
    _renderer->AddTextureToArray(_font->GetTextureID(), _fontTextures);

    _widgetDescriptorSet.Bind("_fontTextures"_h, _fontTextures);

    _vertices.SetDebugName("UIVertices");
    _vertices.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _widgetDrawDatas.SetDebugName("WidgetDrawDatas");
    _widgetDrawDatas.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    _widgetWorldPositions.SetDebugName("WidgetWorldPositions");
    _widgetWorldPositions.SetUsage(Renderer::BufferUsage::STORAGE_BUFFER);

    // Push a debug position
    _widgetWorldPositions.Add(vec4(0, 0, 0, 1));
}

void CanvasRenderer::CreatePipelines()
{
    // Create the merged Widget pipeline
    Renderer::ImageFormat renderTargetFormat = _renderer->GetSwapChainImageFormat();

    Renderer::GraphicsPipelineDesc pipelineDesc;

    // Rasterizer state
    pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

    // Render targets.
    pipelineDesc.states.renderTargetFormats[0] = renderTargetFormat;

    // Shader
    Renderer::VertexShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("UI/Widget.vs"_h, "UI/Widget.vs");
    pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);

    Renderer::PixelShaderDesc pixelShaderDesc;
    pixelShaderDesc.shaderEntry = _gameRenderer->GetShaderEntry("UI/Widget.ps"_h, "UI/Widget.ps");
    pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

    // Blending
    pipelineDesc.states.blendState.renderTargets[0].blendEnable = true;
    pipelineDesc.states.blendState.renderTargets[0].srcBlend = Renderer::BlendMode::SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].destBlend = Renderer::BlendMode::INV_SRC_ALPHA;
    pipelineDesc.states.blendState.renderTargets[0].srcBlendAlpha = Renderer::BlendMode::ONE;
    pipelineDesc.states.blendState.renderTargets[0].destBlendAlpha = Renderer::BlendMode::INV_SRC_ALPHA;

    _widgetPipeline = _renderer->CreatePipeline(pipelineDesc);
}

void CanvasRenderer::InitDescriptorSets()
{
    _widgetDescriptorSet.RegisterPipeline(_renderer, _widgetPipeline);
    _widgetDescriptorSet.Init(_renderer);
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
        panel.gpuDataIndex = _widgetDrawDatas.Add();
    }
    vec2 size = transform.GetSize();
    vec2 cornerRadius = (size.x > 0.0f && size.y > 0.0f)
        ? vec2(panelTemplate.cornerRadius / size.x, panelTemplate.cornerRadius / size.y)
        : vec2(0.0f);
    vec2 normalizedBorderSize = (panelTemplate.borderSize > 0.0f && size.x > 0.0f && size.y > 0.0f)
        ? vec2(panelTemplate.borderSize / size.x, panelTemplate.borderSize / size.y)
        : vec2(0.0f);

    // Update draw data
    auto& drawData = _widgetDrawDatas[panel.gpuDataIndex];
    drawData.packed0.x = static_cast<u32>(WidgetDrawType::Panel);
    drawData.packed0.y = static_cast<u32>(panel.gpuVertexIndex); // vertexBase
    drawData.packed1.y = panelTemplate.borderColor.ToRGBA32();
    drawData.packed1.z = panelTemplate.color.ToRGBA32();
    drawData.cornerRadiusAndBorder = vec4(cornerRadius, normalizedBorderSize);

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

    drawData.packed1.x = (textureIndex & 0xFFFF) | ((additiveTextureIndex & 0xFFFF) << 16);

    // Nine slicing
    const vec2& widgetSize = transform.GetSize();

    Renderer::TextureID textureID = _renderer->GetTextureID(_textures, textureIndex);
    Renderer::TextureBaseDesc textureBaseDesc = _renderer->GetDesc(textureID);
    vec2 texSize = vec2(textureBaseDesc.width, textureBaseDesc.height);

    vec2 textureScaleToWidgetSize = texSize / widgetSize;
    hvec2 packedScale = hvec2(textureScaleToWidgetSize.x, textureScaleToWidgetSize.y);
    static_assert(sizeof(hvec2) == sizeof(u32), "hvec2 must be 4 bytes for packed storage");
    std::memcpy(&drawData.packed1.w, &packedScale, sizeof(u32));
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

    drawData.packed0.z = (clipper->hasClipMaskTexture) ? LoadTexture(clipper->clipMaskTexture) : 0;
    drawData.clipRegionRect = hvec4(clipRegionMin.x, clipRegionMin.y, clipRegionMax.x, clipRegionMax.y);
    drawData.clipMaskRegionRect = hvec4(scaledClipMaskRegionMin.x, scaledClipMaskRegionMin.y, scaledClipMaskRegionMax.x, scaledClipMaskRegionMax.y);

    // World position UI (UINT_MAX bit-pattern == -1 when reinterpreted as int in the shader)
    auto& widget = registry->get<ECS::Components::UI::Widget>(entity);
    drawData.packed0.w = widget.worldTransformIndex;

    _widgetDrawDatas.SetDirtyElement(panel.gpuDataIndex);
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
        text.gpuDataIndex = _widgetDrawDatas.AddCount(text.numCharsNonWhitespace);
    }

    // Update WidgetDrawData entries (one per non-whitespace char)
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

        auto& drawData = _widgetDrawDatas[text.gpuDataIndex + charIndex];
        drawData.packed0.x = static_cast<u32>(WidgetDrawType::Text);
        drawData.packed0.y = static_cast<u32>(text.gpuVertexIndex) + (charIndex * 6); // vertexBase
        drawData.packed1.x = (fontTextureIndex & 0xFFFF);
        drawData.packed1.z = textTemplate.color.ToRGBA32();
        drawData.packed1.w = textTemplate.borderColor.ToRGBA32();

        // borderSize in cornerRadiusAndBorder.x; unitRange in .zw
        f32 distanceRange = font->upperPixelRange - font->lowerPixelRange;
        drawData.cornerRadiusAndBorder.x = textTemplate.borderSize;
        drawData.cornerRadiusAndBorder.y = 0.0f;
        drawData.cornerRadiusAndBorder.z = distanceRange / font->width;
        drawData.cornerRadiusAndBorder.w = distanceRange / font->height;

        // Clipping
        drawData.packed0.z = (clipper->hasClipMaskTexture) ? LoadTexture(clipper->clipMaskTexture) : 0;
        drawData.clipRegionRect = hvec4(clipRegionMin.x, clipRegionMin.y, clipRegionMax.x, clipRegionMax.y);
        drawData.clipMaskRegionRect = hvec4(scaledClipMaskRegionMin.x, scaledClipMaskRegionMin.y, scaledClipMaskRegionMax.x, scaledClipMaskRegionMax.y);

        // World position UI (UINT_MAX bit-pattern == -1 when reinterpreted as int in the shader)
        auto& widget = registry->get<ECS::Components::UI::Widget>(entity);
        drawData.packed0.w = widget.worldTransformIndex;

        charIndex++;
    }
    _widgetDrawDatas.SetDirtyElements(text.gpuDataIndex, text.numCharsNonWhitespace);
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

// ----------------------------------------------------------------------------
// Sortkey layout (u32):
//   MSB                                               LSB
//   [ priority 5 | canvasOrder 8 | traversalIndex 15 | reserved 4 ]
//
// - priority: 0 = normal, >0 = promoted (focus/drag/modal...). At the top so a dragged/focused
//   widget floats above every normal widget, across canvases.
// - canvasOrder: 0 = bottom canvas, grows upward. Makes per-canvas sort a natural consequence
//   of the sort key - no explicit grouping loop needed in the render pass.
// - traversalIndex: DFS pre-order index within a canvas, Z-sorted at each sibling level.
//   Unique within the canvas. Encodes parent-before-child containment. Caps at 2^15-1 = 32,767
//   widgets per canvas; runaway counts are clamped so they never leak into canvasOrder bits.
// - reserved: for future use (clip bucket, atlas bucket, ...).
// ----------------------------------------------------------------------------
u8 CanvasRenderer::ResolvePriority(entt::registry* registry, entt::entity entity) const
{
    auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();
    if (entity != entt::null && entity == uiSingleton.focusedEntity)
    {
        return 1; // Focus tier. Drag/modal/tooltip slots reserved for future systems.
    }
    return 0;
}

void CanvasRenderer::RebuildCanvasOrder(entt::registry* registry)
{
    _canvasOrderByEntity.clear();

    // Collect canvases + their layer. We iterate via registry->view so the natural
    // ordering from entt is used as the tiebreaker when two canvases share a layer.
    struct CanvasOrderEntry { entt::entity entity; u32 layer; u32 iterSeenIndex; };
    std::vector<CanvasOrderEntry> canvases;

    u32 iterSeenIndex = 0;
    registry->view<Canvas>().each([&](entt::entity canvasEntity, Canvas&)
    {
        auto& transform = registry->get<ECS::Components::Transform2D>(canvasEntity);
        canvases.push_back({ canvasEntity, transform.GetLayer(), iterSeenIndex++ });
    });

    // Sort: layer asc, then iteration order asc. Unique keys by construction.
    std::sort(canvases.begin(), canvases.end(), [](const CanvasOrderEntry& a, const CanvasOrderEntry& b)
    {
        if (a.layer != b.layer) 
            return a.layer < b.layer;
        return a.iterSeenIndex < b.iterSeenIndex;
    });

    for (size_t i = 0; i < canvases.size(); ++i)
    {
        _canvasOrderByEntity[canvases[i].entity] = static_cast<u8>(std::min<size_t>(i, 255));
    }
}

void CanvasRenderer::DfsAssignSortKey(entt::registry* registry, entt::entity entity, u8 canvasOrder, u32& traversalIndex, u8 inheritedPriority)
{
    auto& transform2DSystem = ECS::Transform2DSystem::Get(*registry);

    auto& widget = registry->get<Widget>(entity);
    u8 effectivePriority = std::max(inheritedPriority, ResolvePriority(registry, entity));

    // Canvases aren't drawn themselves, so we don't produce a sort key for them (they're iteration hubs).
    if (widget.type != WidgetType::Canvas)
    {
        // u32 sortkey layout (MSB -> LSB):
        //   [ priority 5 | canvasOrder 8 | traversalIndex 15 | reserved 4 ]
        // 32,768 widgets per canvas max; runaway scripts get clamped so the key never bleeds
        // into the canvasOrder bits. In practice real UIs are well under 1000 widgets per canvas.
        constexpr u32 kMaxTraversalIndex = (1u << 15) - 1;
        const u32 clampedTraversal = std::min(traversalIndex, kMaxTraversalIndex);
        widget.sortKey = (static_cast<u32>(effectivePriority) << 27)
                      |  (static_cast<u32>(canvasOrder)       << 19)
                      |  (clampedTraversal                    << 4);
        ++traversalIndex;
    }

    // Gather children, sort by (Transform2D::layer asc, SceneNode2D::siblingIndex asc). Not stable_sort -
    // the siblingIndex tiebreaker already guarantees a total order.
    //
    // Recursion-safe via stack discipline on the shared _siblingScratch:
    //   - record `start` before pushing this level's children,
    //   - sort only [start, end),
    //   - copy each child by VALUE before recursing (the recursive call will push more entries
    //     and may reallocate the underlying buffer; the by-value copy is unaffected),
    //   - resize back to `start` before returning so the caller's frame is intact.
    const size_t start = _siblingScratch.size();
    transform2DSystem.IterateChildren(entity, [&](entt::entity childEntity)
    {
        _siblingScratch.push_back(childEntity);
    });
    const size_t count = _siblingScratch.size() - start;

    std::sort(_siblingScratch.begin() + start, _siblingScratch.end(), [&](entt::entity a, entt::entity b)
    {
        const auto& ta = registry->get<ECS::Components::Transform2D>(a);
        const auto& tb = registry->get<ECS::Components::Transform2D>(b);
        if (ta.GetLayer() != tb.GetLayer())
            return ta.GetLayer() < tb.GetLayer();

        const auto& na = registry->get<ECS::Components::SceneNode2D>(a);
        const auto& nb = registry->get<ECS::Components::SceneNode2D>(b);
        return na.GetSiblingIndex() < nb.GetSiblingIndex();
    });

    for (size_t i = 0; i < count; ++i)
    {
        // By-value copy is mandatory: the recursive call will push to _siblingScratch and may
        // reallocate the backing buffer, invalidating any reference into it.
        const entt::entity child = _siblingScratch[start + i];
        DfsAssignSortKey(registry, child, canvasOrder, traversalIndex, effectivePriority);
    }

    _siblingScratch.resize(start);
}

void CanvasRenderer::RefreshBucketCPU(entt::registry* registry, entt::entity canvasEntity, bool isRT)
{
    ECS::Transform2DSystem& transformSystem2D = ECS::Transform2DSystem::Get(*registry);

    // Resolve target bucket (insert empty on first encounter for this RT canvas).
    BucketResources* bucket = isRT ? &_rtBuckets.try_emplace(canvasEntity).first->second
                                   : &_mainBucket;

    // --- Gather (sortKey, IndirectDraw) pairs into _sortScratch -------------------------------
    _sortScratch.clear();

    auto gather = [&](entt::entity root)
    {
        transformSystem2D.IterateChildrenRecursiveDepth(root, [&](entt::entity childEntity)
        {
            auto& w = registry->get<Widget>(childEntity);
            if (!w.IsVisible()) 
                return false;
            if (w.type != WidgetType::Panel && w.type != WidgetType::Text) 
                return true;

            Renderer::IndirectDraw args{};
            args.vertexCount = 6;
            args.firstVertex = 0;

            if (w.type == WidgetType::Panel)
            {
                auto& panel = registry->get<Panel>(childEntity);
                if (panel.gpuDataIndex < 0) 
                    return true;
                args.instanceCount = 1;
                args.firstInstance = static_cast<u32>(panel.gpuDataIndex);
            }
            else // Text
            {
                auto& text = registry->get<Text>(childEntity);
                if (text.numCharsNonWhitespace <= 0 || text.gpuDataIndex < 0) 
                    return true;
                args.instanceCount = static_cast<u32>(text.numCharsNonWhitespace);
                args.firstInstance = static_cast<u32>(text.gpuDataIndex);
            }

            _sortScratch.push_back({ w.sortKey, args });
            return true;
        });
    };

    if (isRT)
    {
        gather(canvasEntity);
    }
    else
    {
        registry->view<Canvas>(entt::exclude<CanvasRenderTargetTag>).each([&](entt::entity c, Canvas&) 
        { 
            gather(c);
        });
    }

    const u32 drawCount = static_cast<u32>(_sortScratch.size());
    bucket->drawCount = drawCount;

    if (drawCount == 0)
    {
        // Nothing to draw. Leave retained buffers as-is; the draw pass checks drawCount==0 and skips.
        return;
    }

    // --- Sort on CPU ---------------------------------------------------------------------------
    // std::sort beats our GPU radix sort at UI scale (N up to a few thousand) because the GPU pipe
    // is dispatch-overhead-bound regardless of N. See git history for the GPU path (RadixSort.*).
    std::sort(_sortScratch.begin(), _sortScratch.end(), [](const SortEntry& a, const SortEntry& b) 
    { 
        return a.key < b.key; 
    });

    // --- Extract sorted IndirectDraws into contiguous upload vector ----------------------------
    _uploadScratch.clear();
    _uploadScratch.reserve(drawCount);
    for (const SortEntry& e : _sortScratch)
        _uploadScratch.push_back(e.draw);

    // --- (Re)create retained finalSortedArgs / finalCount if needed ----------------------------
    if (bucket->finalSortedArgsCapacity < drawCount || bucket->finalSortedArgs == Renderer::BufferID::Invalid())
    {
        Renderer::BufferDesc argsDesc;
        argsDesc.name  = isRT ? "UISort.RT.FinalSortedArgs" : "UISort.Main.FinalSortedArgs";
        argsDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER
                       | Renderer::BufferUsage::TRANSFER_DESTINATION;
        argsDesc.size  = static_cast<u64>(drawCount) * sizeof(Renderer::IndirectDraw);
        bucket->finalSortedArgs = _renderer->CreateBuffer(bucket->finalSortedArgs, argsDesc);
        bucket->finalSortedArgsCapacity = drawCount;
    }
    if (bucket->finalCount == Renderer::BufferID::Invalid())
    {
        Renderer::BufferDesc countDesc;
        countDesc.name  = isRT ? "UISort.RT.FinalCount" : "UISort.Main.FinalCount";
        countDesc.usage = Renderer::BufferUsage::INDIRECT_ARGUMENT_BUFFER
                        | Renderer::BufferUsage::TRANSFER_DESTINATION;
        countDesc.size  = sizeof(u32);
        bucket->finalCount = _renderer->CreateBuffer(countDesc);
    }

    // --- Upload --------------------------------------------------------------------------------
    // UploadToBuffer queues a staged copy that completes before the next frame's command list
    // runs. Same mechanism we already use for everything else here.
    _renderer->UploadToBuffer(bucket->finalSortedArgs, 0, _uploadScratch.data(), 0, static_cast<u64>(drawCount) * sizeof(Renderer::IndirectDraw));
    _renderer->UploadToBuffer(bucket->finalCount, 0, &bucket->drawCount, 0, sizeof(u32));
}
